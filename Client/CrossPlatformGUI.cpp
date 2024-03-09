﻿#include "CrossPlatformGUI.h"
bool CrossPlatformGUI::performGUIPass()
{
	ImGui::NewFrame();

	static bool isConnected = false;

	bool open = true;

	ImGui::SetNextWindowPos({ 0,0 });
	ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
	{
		//TODO: Figure out how to get rid of the Windows window, make everything transparent, and just use ImGui for everything.
		//TODO: ImGuiWindowFlags_AlwaysAutoResize causes some flickering. Figure out how to stop it
		ImGui::Begin("Sony Headphones", &open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

		//Legal disclaimer
		if (_config.showDisclaimers) {
			ImGui::Text("! This product is not affiliated with Sony. Use at your own risk. !");
			ImGui::Text("Source (original) : https://github.com/Plutoberth/SonyHeadphonesClient");
			ImGui::Text("Source (this fork): https://github.com/mos9527/SonyHeadphonesClient");
			ImGui::Spacing();
		}

		try {
			_drawErrors();
			_drawDeviceDiscovery();

			if (_headphones)
			{
				ImGui::Spacing();
				// Polling & state updates are only done on the main thread
				auto event = _headphones->poll();
				switch (event.type)
				{
					case HeadphonesEvent::JSONMessage:
						// this->_mq.addMessage(std::get<std::string>(event.message));
						// These are kinda interesting actually, though not very useful for now
						// May future readers figure out how to use them...
						break;
					case HeadphonesEvent::HeadphoneInteractionEvent:
					{
						// Gotta use some cpp20 features somewhere...or should I?
						auto fv = std::get<std::string>(event.message) |
							std::ranges::views::filter(																								
								[](auto c) { return std::isprint(c); }
							);
						_handleHeadphoneInteraction(std::string(fv.begin(), fv.end()));
					}
						break;
				default:
					break;
				}
				_drawControls();
				_setHeadphoneSettings();
				// Timed sync
				static const double syncInterval = 1.0;
				static double lastSync = -syncInterval;
				if (_requestFuture.ready()) {
					if (ImGui::GetTime() - lastSync >= syncInterval) {
						_requestFuture.get();
						lastSync = ImGui::GetTime();
						_requestFuture.setFromAsync([this]() {this->_headphones->requestSync(); });
					}
				}
			}
		
			_drawConfig();
		}
		catch (RecoverableException& exc) {
			if (exc.shouldDisconnect)
			{
				this->_headphones.reset();
			}
			else {
				throw exc;
			}
			this->_mq.addMessage(exc.what());
		}
	}

	ImGui::End();
	ImGui::Render();
	return open;
}

ImFont* CrossPlatformGUI::_applyFont(const std::string& fontFile, float font_size)
{
	ImGuiIO& io = ImGui::GetIO();
	ImVector<ImWchar> ranges;
	ImFontGlyphRangesBuilder builder;
	// Attempt to support CJK characters
	// when a custom font is loaded
	builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
	builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
	builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
	builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
	builder.BuildRanges(&ranges);
	
	io.Fonts->Clear();	

	ImFont* font = io.Fonts->AddFontFromFileTTF(
		_config.imguiFontFile.c_str(),
		font_size,
		nullptr,
		&ranges[0]
	);

	if (font) {
		std::cout << "[imgui] using custom font: " << _config.imguiFontFile << std::endl;
	}
	else {
		std::cout << "[imgui] failed to load custom font. using default font.\n";
		//AddFontFromMemory will own the pointer, so there's no leak
		char* fileData = new char[sizeof(CascadiaCodeTTF)];
		memcpy(fileData, CascadiaCodeTTF, sizeof(CascadiaCodeTTF));

		font = io.Fonts->AddFontFromMemoryTTF(
			reinterpret_cast<void*>(fileData),
			sizeof(CascadiaCodeTTF),
			font_size,
			nullptr,
			io.Fonts->GetGlyphRangesDefault()
		);
	}
	io.Fonts->AddFontDefault(nullptr);
	io.Fonts->Build();
	return font;
}

void CrossPlatformGUI::_drawErrors()
{
	//There's a slight race condition here but I don't care, it'd only be for one frame.
	if (this->_mq.begin() != this->_mq.end())
	{
		ImGui::Text("Errors:");
		ImGui::Spacing();

		for (auto&& message : this->_mq)
		{
			ImGui::Text("%s", message.message.c_str());
		}		
	}
}

void CrossPlatformGUI::_drawDeviceDiscovery()
{
	if (ImGui::CollapsingHeader("Device Discovery", ImGuiTreeNodeFlags_DefaultOpen))
	{
		static std::vector<BluetoothDevice> connectedDevices;
		static int selectedDevice = -1;

		if (this->_headphones)
		{
			ImGui::Text("Connected to %s", this->_connectedDevice.name.c_str());
			if (ImGui::Button("Disconnect"))
			{
				selectedDevice = -1;				
				throw RecoverableException("Requested Disconnect", true);
			}			
		}
		else
		{
			ImGui::Text("Select from one of the available devices: ");

			int temp = 0;
			for (const auto& device : connectedDevices)
			{
				ImGui::RadioButton(device.name.c_str(), &selectedDevice, temp++);
			}

			ImGui::Spacing();

			if (this->_connectFuture.valid())
			{
				if (this->_connectFuture.ready())
				{
					this->_connectFuture.get();
				}
				else
				{
					ImGui::Text("Connecting %c", "|/-\\"[(int)(ImGui::GetTime() / 0.05f) & 3]);
				}
			}
			else
			{
				if (ImGui::Button("Connect"))
				{
					if (selectedDevice != -1)
					{
						this->_connectedDevice = connectedDevices[selectedDevice];
						this->_connectFuture.setFromAsync([this]() { 
							this->_bt.connect(this->_connectedDevice.mac);
							this->_headphones = std::make_unique<Headphones>(this->_bt);
							if (this->_requestFuture.valid())
								this->_requestFuture.get();
							this->_requestFuture.setFromAsync([this]() {
								this->_headphones->requestInit();
							});
						});
					}
				}
			}

			ImGui::SameLine();

			if (this->_connectedDevicesFuture.valid())
			{
				if (this->_connectedDevicesFuture.ready())
				{
					connectedDevices = this->_connectedDevicesFuture.get();
				}
				else
				{
					ImGui::Text("Discovering Devices %c", "|/-\\"[(int)(ImGui::GetTime() / 0.05f) & 3]);
				}
			}
			else
			{
				if (ImGui::Button("Refresh devices"))
				{
					selectedDevice = -1;
					this->_connectedDevicesFuture.setFromAsync([this]() { return this->_bt.getConnectedDevices(); });
				}
			}
		}
	}
}

void CrossPlatformGUI::_drawControls()
{
	assert(_headphones); 
	if (ImGui::CollapsingHeader("Stats", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::TreeNode("Playback")) {
			ImGui::Text("Title:  %s",_headphones->playback.title.c_str());
			ImGui::Text("Album:  %s",_headphones->playback.album.c_str());
			ImGui::Text("Artist: %s",_headphones->playback.artist.c_str());
			ImGui::Separator();
			ImGui::Text("Sound Pressure: %d", _headphones->playback.sndPressure);
			ImGui::TreePop();
		}
		if (ImGui::TreeNode("Battery")) {
			ImGui::Text("L:"); ImGui::SameLine();
			ImGui::ProgressBar(_headphones->statBatteryL.current / 100.0);
			ImGui::Text("R:"); ImGui::SameLine();
			ImGui::ProgressBar(_headphones->statBatteryR.current / 100.0);
			ImGui::Text("Case:"); ImGui::SameLine();
			ImGui::ProgressBar(_headphones->statBatteryCase.current / 100.0);
			ImGui::TreePop();
		}
		if (ImGui::Button("Power Off")) {
			if (_requestFuture.ready()) {
				_requestFuture.get();
				_requestFuture.setFromAsync([this]() {
					this->_headphones->requestPowerOff();
					throw RecoverableException("Requested Shutdown", true);
				});
			}
		}
	}	
	if (ImGui::CollapsingHeader("Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::TreeNode("Playback Control")) {
			using enum PLAYBACK_CONTROL;
			PLAYBACK_CONTROL control = NONE;
			if (ImGui::Button("Prev")) control = PREV;
			ImGui::SameLine();
			if (_headphones->playPause.current == true /*playing*/) {
				if (ImGui::Button("Pause")) control = PAUSE;
			}
			else {
				if (ImGui::Button("Play")) control = PLAY;
			}
			ImGui::SameLine();
			if (ImGui::Button("Next")) control = NEXT;
				
			if (_requestFuture.ready()) {		
				if (control != NONE) {
					_requestFuture.get();
					_requestFuture.setFromAsync([this, control]() {
						this->_headphones->requestPlaybackControl(control);
					});
				}
			}
			ImGui::SliderInt("Volume", &_headphones->volume.desired, 0, 30);			
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Ambient Sound / Noise Cancelling")) {
			ImGui::Checkbox("Enabled", &_headphones->asmEnabled.desired);
			ImGui::Checkbox("Voice Passthrough", &_headphones->asmFoucsOnVoice.desired);
			ImGui::Text("NOTE: Set to 0 to enable Noise Cancelling.");
			ImGui::SliderInt("Ambient Strength", &_headphones->asmLevel.desired, 0, 20);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Speak to Chat")) {
			ImGui::Checkbox("Enabled", &_headphones->stcEnabled.desired);
			ImGui::SliderInt("STC Level", &_headphones->stcLevel.desired, 0, 2);
			ImGui::SliderInt("STC Time", &_headphones->stcTime.desired, 0, 3);
			ImGui::TreePop();
		}
		
		if (ImGui::TreeNode("Equalizer")) {
			const float width = ImGui::GetContentRegionAvail().x;
			const float padding = ImGui::GetStyle().ItemSpacing.x;
			ImGui::SeparatorText("5-Band EQ");
			for (int i = 0;i < 5;i++){
				const char* bandNames[]  = {"400","1k","2.5k","6.3k","16k"};
				ImGui::PushID(i);
				ImGui::VSliderInt("##", ImVec2(width / 5 - padding, 160), &_headphones->eqConfig.desired.bands[i], -10, 10);
				if (ImGui::IsItemActive() || ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", bandNames[i]);
				ImGui::PopID();
				if (i != 4) ImGui::SameLine();
			}
			ImGui::SeparatorText("Clear Bass");
			ImGui::SetNextItemWidth(width);
			ImGui::SliderInt("##", &_headphones->eqConfig.desired.bassLevel, -10, 10);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Connected Devices")) {
			ImGui::Checkbox("Multipoint Connect", &_headphones->mpEnabled.desired);
			ImGui::Text("NOTE: Can't toggle yet. Sorry!");
			size_t i = 0;
			const auto default_flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
			for (auto& [mac, device] : _headphones->connectedDevices) {
				if (mac == _headphones->mpDeviceMac.current)
					ImGui::TreeNodeEx((void*)i++, default_flags | ImGuiTreeNodeFlags_Selected, "%s", device.name.c_str());
				else
					ImGui::TreeNodeEx((void*)i++, default_flags, "%s", device.name.c_str());
				if (ImGui::IsItemClicked()) {
					_headphones->mpDeviceMac.desired = mac;
				}
			}
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Misc")) {
			ImGui::SliderInt("Voice Guidance Volume", &_headphones->miscVoiceGuidanceVol.desired, -2, 2);

			if (ImGui::TreeNode("Touch Sensor")) {
				// This is a painful amount of boilerplate to write...
				static const char* TOUCH_SENSOR_FUNCTION_STR[] = {
					"Playback Control",
					"Ambient Sound / Noise Cancelling",
					"Not Assigned"
				};

				const auto TOUCH_SENSOR_FUNCTION_INDEX = [](const TOUCH_SENSOR_FUNCTION& tsf) {
					switch (tsf) {
					case TOUCH_SENSOR_FUNCTION::PLAYBACK_CONTROL:
						return 0;
					case TOUCH_SENSOR_FUNCTION::AMBIENT_NC_CONTROL:
						return 1;
					case TOUCH_SENSOR_FUNCTION::NOT_ASSIGNED:
						return 2;
					}
					};

				const auto TOUCH_SENSOR_FUNCTION_INV_INDEX = [](const int& index) {
					switch (index) {
					case 0:
						return TOUCH_SENSOR_FUNCTION::PLAYBACK_CONTROL;
					case 1:
						return TOUCH_SENSOR_FUNCTION::AMBIENT_NC_CONTROL;
					case 2:
						return TOUCH_SENSOR_FUNCTION::NOT_ASSIGNED;
					}
					};

				const auto draw_touch_sensor_combo = [&](auto& prop, const char* label) {
					if (ImGui::BeginCombo(label, TOUCH_SENSOR_FUNCTION_STR[TOUCH_SENSOR_FUNCTION_INDEX(prop.desired)])) {
						for (int i = 0; i < (int)TOUCH_SENSOR_FUNCTION::NUM_FUNCTIONS; i++) {
							const bool is_selected = (TOUCH_SENSOR_FUNCTION_INDEX(prop.desired) == i);
							if (ImGui::Selectable(TOUCH_SENSOR_FUNCTION_STR[i], is_selected)) {
								prop.desired = TOUCH_SENSOR_FUNCTION_INV_INDEX(i);
							}
							if (is_selected) {
								ImGui::SetItemDefaultFocus();
							}
						}
						ImGui::EndCombo();
					}
					};
				draw_touch_sensor_combo(_headphones->touchLeftFunc, "Left Touch Sensor");
				draw_touch_sensor_combo(_headphones->touchRightFunc, "Right Touch Sensor");
				ImGui::TreePop();
			}
			
			ImGui::TreePop();
		}
	}
}

void CrossPlatformGUI::_drawConfig()
{
	if (ImGui::TreeNode("Config")) {
		if (ImGui::TreeNode("UI")) {
			ImGui::SeparatorText("Misc");
			ImGui::Checkbox("Show Disclaimers", &_config.showDisclaimers);
			ImGui::SeparatorText("Font");
			ImGui::SliderFloat("Font Size", &_config.imguiFontSize, 10.0f, 30.0f);
			ImGui::InputText("Custom Font (filename)", &_config.imguiFontFile);
			ImGui::Text("NOTE: All font-related changes will only be applied on restart.");
			ImGui::TreePop();
		}
		if (ImGui::TreeNode("Shell Command")) {
			ImGui::Text("NOTE: Headphones may send events (displayed in the message queue)");
			ImGui::Text("NOTE: You may bind them to shell commands here.");
			static ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersV;
			if (ImGui::BeginTable("Commands", 3, flags)) {
				ImGui::TableSetupColumn("Event");
				ImGui::TableSetupColumn("Shell");
				ImGui::TableSetupColumn("Action");
				ImGui::TableHeadersRow();
				ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
				for (
					auto it = _config.headphoneInteractionShellCommands.begin();
					it != _config.headphoneInteractionShellCommands.end();
					it != _config.headphoneInteractionShellCommands.end() ?
					it++ : _config.headphoneInteractionShellCommands.end()) {

					ImGui::TableNextRow();
					ImGui::PushID((void*)it->first.data());
					ImGui::TableSetColumnIndex(0);
					ImGui::InputText("##", const_cast<std::string*>(&it->first));
					ImGui::PopID();

					ImGui::PushID((void*)it->second.data());
					ImGui::TableSetColumnIndex(1);
					ImGui::InputText("##", const_cast<std::string*>(&it->second));
					ImGui::TableSetColumnIndex(2);
					if (ImGui::Button("Remove##")) {
						it = _config.headphoneInteractionShellCommands.erase(it);
					}
					ImGui::PopID();
				}
				ImGui::PopStyleColor();
				ImGui::EndTable();
			}
			if (ImGui::Button("Add Command")) {
				_config.headphoneInteractionShellCommands.insert({ {},{} });
			}
			ImGui::TreePop();
		}
		if (ImGui::Button("Save Config"))
			_config.saveSettings();
		ImGui::SameLine();
		if (ImGui::Button("Load Config"))
			_config.loadSettings();
		ImGui::TreePop();
	}
}

void CrossPlatformGUI::_setHeadphoneSettings() {
	assert(_headphones);
	//Don't show if the event only takes a few frames to send
	static int commandLinger = 0;
	if (this->_sendCommandFuture.ready())
	{
		commandLinger = 0;
		this->_sendCommandFuture.get();
	}
	//This means that we're waiting
	else if (this->_sendCommandFuture.valid())
	{
		if (commandLinger++ > (FPS / 10))
		{
			ImGui::Text("Sending command %c", "|/-\\"[(int)(ImGui::GetTime() / 0.05f) & 3]);
		}
	}
	//We're not waiting, and there's no event in the air, so we can evaluate sending a new event
	else if (this->_headphones->isChanged())
	{
		this->_sendCommandFuture.setFromAsync([=, this]() {
			return this->_headphones->setChanges();
		});
	}
}

void CrossPlatformGUI::_handleHeadphoneInteraction(std::string&& event)
{
	if (_config.headphoneInteractionShellCommands.contains(event)) {
		auto const& shellCommand = _config.headphoneInteractionShellCommands[event];
		ExecuteShellCommand(std::string(shellCommand.data()));
	}
	this->_mq.addMessage("[event] " + event);
}

CrossPlatformGUI::CrossPlatformGUI(BluetoothWrapper bt, const float font_size) : _bt(std::move(bt))
{
	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();
	this->_mq = TimedMessageQueue(GUI_MAX_MESSAGES);	
	this->_connectedDevicesFuture.setFromAsync([this]() { return this->_bt.getConnectedDevices(); });

	io.IniFilename = nullptr;
	io.WantSaveIniSettings = false;

	_config.loadSettings();
	if (_config.imguiFontSize < 0) 
		_config.imguiFontSize = font_size;
	_applyFont(_config.imguiFontFile, _config.imguiFontSize);
}

void GUIConfig::loadSettings()
{
	std::ifstream file(SAVE_NAME);
	if (file.good()) {
		toml::table table = toml::parse(file);
		showDisclaimers = table["showDisclaimers"].value<bool>().value_or(true);
		
		imguiSettings = table["imguiSettings"].value<std::string>().value_or("");		
		ImGui::LoadIniSettingsFromMemory(imguiSettings.c_str(), imguiSettings.size());
		
		imguiFontFile = table["imguiFontFile"].value<std::string>().value_or("");
		imguiFontSize = table["imguiFontSize"].value<float>().value_or(-1);

		if (table["shellCommands"].as_table())
		{
			headphoneInteractionShellCommands.clear();
			for (auto& [k, v] : *table["shellCommands"].as_table()) {
				std::string ks{ k.str() };
				std::string vs = v.value<std::string>().value_or("");
				headphoneInteractionShellCommands[ks] = vs;
			}
		}
	}
}

void GUIConfig::saveSettings()
{
	toml::table table;
	table.insert("showDisclaimers", showDisclaimers);

	imguiSettings = ImGui::SaveIniSettingsToMemory();
	table.insert("imguiSettings", imguiSettings);
	table.insert("imguiFontSize", imguiFontSize);
	table.insert("shellCommands", toml::table{});
	for (auto& [k, v] : headphoneInteractionShellCommands) 
		table["shellCommands"].as_table()->insert(k.data(), v.data());	

	table.insert("imguiFontFile", imguiFontFile);

	std::ofstream file(SAVE_NAME);
	file << toml::toml_formatter{ table };
}
