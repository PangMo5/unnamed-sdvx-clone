#include "stdafx.h"
#include "SongSelect.hpp"
#include "TitleScreen.hpp"
#include "Application.hpp"
#include <Shared/Profiling.hpp>
#include "Scoring.hpp"
#include "Input.hpp"
#include <GUI/GUI.hpp>
#include "SongSelectItem.hpp"
#include <Beatmap/MapDatabase.hpp>
#include "Game.hpp"
#include "TransitionScreen.hpp"
#include "GameConfig.hpp"
#include "SongFilter.hpp"
#include <Audio/Audio.hpp>
#ifdef _WIN32
#include "SDL_keycode.h"
#else
#include "SDL2/SDL_keycode.h"
#endif

/*
	Song preview player with fade-in/out
*/
class PreviewPlayer
{
public:
	void FadeTo(AudioStream stream)
	{
		// Already existing transition?
		if(m_nextStream)
		{
			if(m_currentStream)
			{
				m_currentStream.Destroy();
			}
			m_currentStream = m_nextStream;
		}
		m_nextStream = stream;
		m_nextSet = true;
		if(m_nextStream)
		{
			m_nextStream->SetVolume(0.0f);
			m_nextStream->Play();
		}
		m_fadeTimer = 0.0f;
	}
	void Update(float deltaTime)
	{
		if(m_nextSet)
		{
			m_fadeTimer += deltaTime;
			if(m_fadeTimer >= m_fadeDuration)
			{
				if(m_currentStream)
				{
					m_currentStream.Destroy();
				}
				m_currentStream = m_nextStream;
				if(m_currentStream)
					m_currentStream->SetVolume(1.0f);
				m_nextStream.Release();
				m_nextSet = false;
			}
			else
			{
				float fade = m_fadeTimer / m_fadeDuration;

				if(m_currentStream)
					m_currentStream->SetVolume(1.0f - fade);
				if(m_nextStream)
					m_nextStream->SetVolume(fade);
			}
		}
	}
	void Pause()
	{
		if(m_nextStream)
			m_nextStream->Pause();
		if(m_currentStream)
			m_currentStream->Pause();
	}
	void Restore()
	{
		if(m_nextStream)
			m_nextStream->Play();
		if(m_currentStream)
			m_currentStream->Play();
	}

private:
	static const float m_fadeDuration;
	float m_fadeTimer = 0.0f;
	AudioStream m_nextStream;
	AudioStream m_currentStream;
	bool m_nextSet = false;
};
const float PreviewPlayer::m_fadeDuration = 0.5f;

/*
	Song selection wheel
*/
class SelectionWheel : public Canvas
{
	Map<MapIndex*, Ref<SongSelectItem>> m_guiElements;

	Map<int32, MapIndex*> m_maps;

	Map<int32, MapIndex*> m_mapFilter;
	bool m_filterSet = false;

	// Currently selected map ID
	int32 m_currentlySelectedId = 0;
	// Currently selected sub-widget
	Ref<SongSelectItem> m_currentSelection;

	// Current difficulty index
	int32 m_currentlySelectedDiff = 0;

	// Style to use for everything song select related
	Ref<SongSelectStyle> m_style;

public:
	SelectionWheel(Ref<SongSelectStyle> style) : m_style(style)
	{
	}
	void OnMapsAdded(Vector<MapIndex*> maps)
	{
		for(auto m : maps)
		{
			m_maps.Add(m->id, m);
		}
		if(!m_currentSelection)
			AdvanceSelection(0);
	}
	void OnMapsRemoved(Vector<MapIndex*> maps)
	{
		for(auto m : maps)
		{
			m_maps.erase(m->id);
			auto it = m_guiElements.find(m);
			if(it != m_guiElements.end())
			{
				// Clear selection if a removed item was selected
				if(m_currentSelection == it->second)
					m_currentSelection.Release();

				// Remove this item from the canvas that displays the items
				Remove(it->second.As<GUIElementBase>());
				m_guiElements.erase(it);
			}
		}
		if(!m_maps.Contains(m_currentlySelectedId))
		{
			AdvanceSelection(1);
		}
	}
	void OnMapsUpdated(Vector<MapIndex*> maps)
	{
		for(auto m : maps)
		{
			auto it = m_guiElements.find(m);
			if(it != m_guiElements.end())
			{
				it->second->SetMap(m);
			}
		}
	}
	void OnMapsCleared(Map<int32, MapIndex*> newList)
	{
		m_currentSelection.Release();
		for(auto g : m_guiElements)
		{
			Remove(g.second.As<GUIElementBase>());
		}
		m_guiElements.clear();
		m_filterSet = false;
		m_mapFilter.clear();
		m_maps.clear();
		m_maps = newList;
		if(m_maps.size() > 0)
		{
			AdvanceSelection(0);
		}
	}
	void SelectRandom()
	{
		if(m_SourceCollection().empty())
			return;
		uint32 selection = Random::IntRange(0, (int32)m_SourceCollection().size() - 1);
		auto it = m_SourceCollection().begin();
		std::advance(it, selection);
		SelectMap(it->first);
	}
	void SelectMap(int32 newIndex)
	{
		Set<MapIndex*> visibleMaps;
		auto& srcCollection = m_SourceCollection();
		auto it = srcCollection.find(newIndex);
		if(it != srcCollection.end())
		{
			const float initialSpacing = 0.65f * m_style->frameMain->GetSize().y;
			const float spacing = 0.8f * m_style->frameSub->GetSize().y;
			const Anchor anchor = Anchor(0.0f, 0.5f, 1.0f, 0.5f);

			static const int32 numItems = 10;

			int32 istart;
			for(istart = 0; istart > -numItems;)
			{
				if(it == srcCollection.begin())
					break;
				it--;
				istart--;
			}

			for(int32 i = istart; i <= numItems; i++)
			{
				if(it != srcCollection.end())
				{
					visibleMaps.Add(it->second);

					// Add a new map slot
					bool newItem = m_guiElements.find(it->second) == m_guiElements.end();
					Ref<SongSelectItem> item = m_GetMapGUIElement(it->second);
					float offset = 0;
					if(i != 0)
					{
						offset = initialSpacing * Math::Sign(i) +
							spacing * (i - Math::Sign(i));
					}
					Canvas::Slot* slot = Add(item.As<GUIElementBase>());

					int32 z = -abs(i);
					slot->SetZOrder(z);

					slot->anchor = anchor;
					slot->autoSizeX = true;
					slot->autoSizeY = true;
					slot->alignment = Vector2(0, 0.5f);
					if(newItem)
					{
						// Hard set target position
						slot->offset.pos = Vector2(0, offset);
						slot->offset.size.x = z * 50.0f;
					}
					else
					{
						// Animate towards target position
						item->AddAnimation(Ref<IGUIAnimation>(
							new GUIAnimation<Vector2>(&slot->offset.pos, Vector2(0, offset), 0.1f)), true);
						item->AddAnimation(Ref<IGUIAnimation>(
							new GUIAnimation<float>(&slot->offset.size.x, z * 50.0f, 0.1f)), true);
					}

					item->fade = 1.0f - ((float)abs(i) / (float)numItems);
					item->innerOffset = item->fade * 100.0f;

					if(i == 0)
					{
						m_currentlySelectedId = newIndex;
						m_OnMapSelected(it->second);
					}

					it++;
				}
			}
		}
		m_currentlySelectedId = newIndex;

		// Cleanup invisible elements
		for(auto it = m_guiElements.begin(); it != m_guiElements.end();)
		{
			if(!visibleMaps.Contains(it->first))
			{
				Remove(it->second.As<GUIElementBase>());
				it = m_guiElements.erase(it);
				continue;
			}
			it++;
		}
	}
	void AdvanceSelection(int32 offset)
	{
		auto& srcCollection = m_SourceCollection();
		auto it = srcCollection.find(m_currentlySelectedId);
		if(it == srcCollection.end())
		{
			if(srcCollection.empty())
			{
				// Remove all elements, empty
				m_currentSelection.Release();
				Clear();
				m_guiElements.clear();
				return;
			}
			it = srcCollection.begin();
		}
		for(uint32 i = 0; i < (uint32)abs(offset); i++)
		{
			auto itn = it;
			if(offset < 0)
			{
				if(itn == srcCollection.begin())
					break;
				itn--;
			}
			else
				itn++;
			if(itn == srcCollection.end())
				break;
			it = itn;
		}
		if(it != srcCollection.end())
		{
			SelectMap(it->first);
		}
	}
	void SelectDifficulty(int32 newDiff)
	{
		m_currentSelection->SetSelectedDifficulty(newDiff);
		m_currentlySelectedDiff = newDiff;

		MapIndex** map = m_maps.Find(m_currentlySelectedId);
		if(map)
		{
			OnDifficultySelected.Call(map[0]->difficulties[m_currentlySelectedDiff]);
		}
	}
	void AdvanceDifficultySelection(int32 offset)
	{
		if(!m_currentSelection)
			return;
		MapIndex* map = m_maps[m_currentlySelectedId];
		int32 newIdx = m_currentlySelectedDiff + offset;
		newIdx = Math::Clamp(newIdx, 0, (int32)map->difficulties.size() - 1);
		SelectDifficulty(newIdx);
	}

	// Called when a new map is selected
	Delegate<MapIndex*> OnMapSelected;
	Delegate<DifficultyIndex*> OnDifficultySelected;

	// Set display filter
	void SetFilter(Map<int32, MapIndex *> filter)
	{
		m_mapFilter = filter;
		m_filterSet = true;
		AdvanceSelection(0);
	}
	void SetFilter(SongFilter* filter)
	{
		m_mapFilter = filter->GetFiltered(m_maps);
		m_filterSet = !filter->IsAll();
		AdvanceSelection(0);
	}
	void ClearFilter()
	{
		if(m_filterSet)
		{
			m_filterSet = false;
			AdvanceSelection(0);
		}
	}

	MapIndex* GetSelection() const
	{
		MapIndex*const* map = m_maps.Find(m_currentlySelectedId);
		if(map)
			return *map;
		return nullptr;
	}
	DifficultyIndex* GetSelectedDifficulty() const
	{
		MapIndex*const* map = m_maps.Find(m_currentlySelectedId);
		if(map)
		{
			return map[0]->difficulties[m_currentlySelectedDiff];
		}
		return nullptr;
	}

private:
	const Map<int32, MapIndex*>& m_SourceCollection()
	{
		return m_filterSet ? m_mapFilter : m_maps;
	}
	Ref<SongSelectItem> m_GetMapGUIElement(MapIndex* index)
	{
		auto it = m_guiElements.find(index);
		if(it != m_guiElements.end())
			return it->second;

		Ref<SongSelectItem> newItem = Ref<SongSelectItem>(new SongSelectItem(m_style));

		// Send first map as metadata settings
		const BeatmapSettings& firstSettings = index->difficulties[0]->settings;
		newItem->SetMap(index);
		m_guiElements.Add(index, newItem);
		return newItem;
	}
	void m_OnMapSelected(MapIndex* map)
	{
		// Update compact mode selection views
		if(m_currentSelection)
			m_currentSelection->SwitchCompact(true);
		m_currentSelection = m_guiElements[map];
		m_currentSelection->SwitchCompact(false);

		//if(map && map->id == m_currentlySelectedId)
		//	return;

		// Clamp diff selection
		int32 selectDiff = m_currentlySelectedDiff;
		if(m_currentlySelectedDiff >= (int32)map->difficulties.size())
		{
			selectDiff = (int32)map->difficulties.size() - 1;
		}
		SelectDifficulty(selectDiff);

		OnMapSelected.Call(map);
	}
};


/*
	Filter selection element
*/
class FilterSelection : public Canvas
{
public:
	FilterSelection(Ref<SelectionWheel> selectionWheel) : m_selectionWheel(selectionWheel)
	{
		AddFilter(new SongFilter());
		for (size_t i = 1; i <= 20; i++)
		{
			AddFilter(new LevelFilter(i));
		}
	}

	bool Active = false;

	bool IsAll()
	{
		return m_currentFilter->IsAll();
	}

	void AddFilter(SongFilter* filter)
	{
		m_filters.Add(filter);
		Label* label = new Label();
		label->SetFontSize(30);
		label->SetText(Utility::ConvertToWString(filter->GetName()));
		m_guiElements[filter] = label;
		Canvas::Slot* labelSlot = Add(label->MakeShared());
		labelSlot->allowOverflow = true;
		labelSlot->autoSizeX = true;
		labelSlot->autoSizeY = true;
		labelSlot->anchor = Anchors::MiddleLeft;
		labelSlot->alignment = Vector2(0.f, 0.5f);
		m_currentSelection = 0;
		SelectFilter(m_filters[0]);
	}

	void SelectFilter(SongFilter* filter)
	{
		m_selectionWheel->SetFilter(filter);
		if (m_currentFilter)
			m_guiElements[m_currentFilter]->SetText(Utility::ConvertToWString(m_currentFilter->GetName()));
		m_guiElements[filter]->SetText(Utility::ConvertToWString(Utility::Sprintf("->%s", filter->GetName())));

		for (size_t i = 0; i < m_filters.size(); i++)
		{
			Vector2 coordinate = Vector2(50, 0);
			SongFilter* songFilter = m_filters[i];

			coordinate.y = ((int)i - (int)m_currentSelection) * 40.f;
			coordinate.x -= ((int)m_currentSelection - i) * ((int)m_currentSelection - i) * 1.5;
			Canvas::Slot* labelSlot = Add(m_guiElements[songFilter]->MakeShared());
			AddAnimation(Ref<IGUIAnimation>(
				new GUIAnimation<Vector2>(&labelSlot->offset.pos, coordinate, 0.1f)), true);
			labelSlot->offset = Rect(coordinate, Vector2(0));
		}
		m_currentFilter = filter;
	}

	void AdvanceSelection(int32 offset)
	{
		m_currentSelection = ((int)m_currentSelection + offset) % (int)m_filters.size();
		if (m_currentSelection < 0)
			m_currentSelection = m_filters.size() + m_currentSelection;
		SelectFilter(m_filters[m_currentSelection]);
	}

private:
	Ref<SelectionWheel> m_selectionWheel;
	Vector<SongFilter*> m_filters;
	int32 m_currentSelection = 0;
	Map<SongFilter*, Label*> m_guiElements;
	SongFilter* m_currentFilter = nullptr;
};

/*
	Song select window/screen
*/
class SongSelect_Impl : public SongSelect
{
private:
	Timer m_dbUpdateTimer;
	Ref<Canvas> m_canvas;
	MapDatabase m_mapDatabase;

	Ref<SongSelectStyle> m_style;
	Ref<CommonGUIStyle> m_commonGUIStyle;

	// Shows additional information about a map
	Ref<SongStatistics> m_statisticsWindow;
	// Map selection wheel
	Ref<SelectionWheel> m_selectionWheel;
	// Filter selection
	Ref<FilterSelection> m_filterSelection;
	// Search field
	Ref<TextInputField> m_searchField;
	// Panel to fade out selection wheel
	Ref<Panel> m_fadePanel;


	// Score list canvas
	Ref<Canvas> m_scoreCanvas;
	Ref<LayoutBox> m_scoreList;

	// Player of preview music
	PreviewPlayer m_previewPlayer;

	// Current map that has music being preview played
	MapIndex* m_currentPreviewAudio;

	// Select sound
	Sample m_selectSound;

	// Navigation variables
	float m_advanceSong = 0.0f;
	float m_advanceDiff = 0.0f;
	MouseLockHandle m_lockMouse;
	bool m_suspended = false;
	bool m_previewLoaded = true;
	bool m_showScores = false;
	uint64_t m_previewDelayTicks = 0;

public:
	bool Init() override
	{
		m_commonGUIStyle = g_commonGUIStyle;
		m_canvas = Utility::MakeRef(new Canvas());

		// Load textures for song select
		m_style = SongSelectStyle::Get(g_application);

		// Split between statistics and selection wheel (in percentage)
		const float screenSplit = 0.0f;

		// Statistics window
		m_statisticsWindow = Ref<SongStatistics>(new SongStatistics(m_style));
		Canvas::Slot* statisticsSlot = m_canvas->Add(m_statisticsWindow.As<GUIElementBase>());
		statisticsSlot->anchor = Anchor(0, 0, screenSplit, 1.0f);
		statisticsSlot->SetZOrder(2);

        // Set up input
		g_input.OnButtonPressed.Add(this, &SongSelect_Impl::m_OnButtonPressed);
        
		Panel* background = new Panel();
		background->imageFillMode = FillMode::Fill;
		background->texture = g_application->LoadTexture("bg.png");
		background->color = Color(0.5f);
		Canvas::Slot* bgSlot = m_canvas->Add(background->MakeShared());
		bgSlot->anchor = Anchors::Full;
		bgSlot->SetZOrder(-2);

		LayoutBox* box = new LayoutBox();
		Canvas::Slot* boxSlot = m_canvas->Add(box->MakeShared());
		boxSlot->anchor = Anchor(screenSplit, 0, 1.0f, 1.0f);
		box->layoutDirection = LayoutBox::Vertical;
		{
			m_searchField = Ref<TextInputField>(new TextInputField(m_commonGUIStyle));
			LayoutBox::Slot* searchFieldSlot = box->Add(m_searchField.As<GUIElementBase>());
			searchFieldSlot->fillX = true;
			m_searchField->OnTextUpdated.Add(this, &SongSelect_Impl::OnSearchTermChanged);

			m_selectionWheel = Ref<SelectionWheel>(new SelectionWheel(m_style));
			LayoutBox::Slot* selectionSlot = box->Add(m_selectionWheel.As<GUIElementBase>());
			selectionSlot->fillY = true;
			m_selectionWheel->OnMapSelected.Add(this, &SongSelect_Impl::OnMapSelected);
			m_selectionWheel->OnDifficultySelected.Add(this, &SongSelect_Impl::OnDifficultySelected);
		}

		{
			m_fadePanel = Ref<Panel>(new Panel());
			m_fadePanel->color = Color(0.f);
			m_fadePanel->color.w = 0.0f;
			Canvas::Slot* panelSlot = m_canvas->Add(m_fadePanel->MakeShared());
			panelSlot->anchor = Anchors::Full;
		}

		{
			m_scoreCanvas = Ref<Canvas>(new Canvas());
			Canvas::Slot* slot = m_canvas->Add(m_scoreCanvas->MakeShared());
			slot->anchor = Anchor(1.0,0.0,2.0,10.0);

			Panel* scoreBg = new Panel();
			scoreBg->color = Color(Vector3(0.5), 1.0);
			slot = m_scoreCanvas->Add(scoreBg->MakeShared());
			slot->anchor = Anchors::Full;

			m_scoreList = Ref<LayoutBox>(new LayoutBox());
			m_scoreList->layoutDirection = LayoutBox::LayoutDirection::Vertical;
			slot = m_scoreCanvas->Add(m_scoreList->MakeShared());
			slot->anchor = Anchors::Full;
		}

		{
			m_filterSelection = Ref<FilterSelection>(new FilterSelection(m_selectionWheel));
			Canvas::Slot* slot = m_canvas->Add(m_filterSelection->MakeShared());
			slot->anchor = Anchor(-1.0, 0.0, 0.0, 1.0);
		}

		// Select interface sound
		m_selectSound = g_audio->CreateSample("audio/menu_click.wav");

		// Setup the map database
		m_mapDatabase.AddSearchPath(g_gameConfig.GetString(GameConfigKeys::SongFolder));

		m_mapDatabase.OnMapsAdded.Add(m_selectionWheel.GetData(), &SelectionWheel::OnMapsAdded);
		m_mapDatabase.OnMapsUpdated.Add(m_selectionWheel.GetData(), &SelectionWheel::OnMapsUpdated);
		m_mapDatabase.OnMapsRemoved.Add(m_selectionWheel.GetData(), &SelectionWheel::OnMapsRemoved);
		m_mapDatabase.OnMapsCleared.Add(m_selectionWheel.GetData(), &SelectionWheel::OnMapsCleared);
		m_mapDatabase.StartSearching();

		m_selectionWheel->SelectRandom();

		/// TODO: Check if debugmute is enabled
		g_audio->SetGlobalVolume(g_gameConfig.GetFloat(GameConfigKeys::MasterVolume));

		return true;
	}
	~SongSelect_Impl()
	{
		// Clear callbacks
		m_mapDatabase.OnMapsCleared.Clear();
		g_input.OnButtonPressed.RemoveAll(this);
	}

	// When a map is selected in the song wheel
	void OnMapSelected(MapIndex* map)
	{
		if (map == m_currentPreviewAudio){
			if (m_previewDelayTicks){
				--m_previewDelayTicks;
			}else if (!m_previewLoaded){
				// Set current preview audio
				DifficultyIndex* previewDiff = m_currentPreviewAudio->difficulties[0];
				String audioPath = m_currentPreviewAudio->path + Path::sep + previewDiff->settings.audioNoFX;

				AudioStream previewAudio = g_audio->CreateStream(audioPath);
				if (previewAudio)
				{
					previewAudio->SetPosition(previewDiff->settings.previewOffset);
					m_previewPlayer.FadeTo(previewAudio);
				}
				else
				{
					Logf("Failed to load preview audio from [%s]", Logger::Warning, audioPath);
					m_previewPlayer.FadeTo(AudioStream());
				}
				m_previewLoaded = true;
				// m_previewPlayer.Restore();
			}
		} else{
			// Wait at least 15 ticks before attempting to load song to prevent loading songs while scrolling very fast
			m_previewDelayTicks = 15;
			m_currentPreviewAudio = map;
			m_previewLoaded = false;
		}
	}
	// When a difficulty is selected in the song wheel
	void OnDifficultySelected(DifficultyIndex* diff)
	{
		m_scoreList->Clear();
		uint32 place = 1;

		WString gradeStrings[] =
		{
			L"AAA",
			L"AA",
			L"A",
			L"B",
			L"C",
			L"D",
		};

		for (auto it = diff->scores.rbegin(); it != diff->scores.rend(); ++it)
		{
			ScoreIndex s = **it;

			// Calculate grade
			uint32 value = (uint32)(s.score * 0.9 + s.gauge * 1000000.0);
			uint8 grade = 5;
			if (value > 9800000) // AAA
				grade = 0;
			else if (value > 9400000) // AA
				grade = 1;
			else if (value > 8900000) // A
				grade = 2;
			else if (value > 8000000) // B
				grade = 3;
			else if (value > 7000000) // C
				grade = 4;

			Label* text = new Label();
			text->SetText(Utility::WSprintf(L"--%d--\n%08d\n%d%%\n%ls",place, s.score, (int)(s.gauge * 100), gradeStrings[grade]));
			text->SetFontSize(32);
			LayoutBox::Slot* slot = m_scoreList->Add(text->MakeShared());
			slot->fillX = true;
			slot->padding = Margin(10, 5, 0, 0);
			
			if (place++ > 9)
				break;
		}


	}

	/// TODO: Fix some conflicts between search field and filter selection
	void OnSearchTermChanged(const WString& search)
	{
		if(search.empty())
			m_filterSelection->AdvanceSelection(0);
		else
		{
			String utf8Search = Utility::ConvertToUTF8(search);
			Map<int32, MapIndex*> filter = m_mapDatabase.FindMaps(utf8Search);
			m_selectionWheel->SetFilter(filter);
		}
	}
    

    void m_OnButtonPressed(Input::Button buttonCode)
    {
		if (m_suspended)
			return;
	    if(buttonCode == Input::Button::BT_S && !IsSuspended())
        {
            
			bool autoplay = (g_gameWindow->GetModifierKeys() & ModifierKeys::Ctrl) == ModifierKeys::Ctrl;
			MapIndex* map = m_selectionWheel->GetSelection();
			if(map)
			{
				DifficultyIndex* diff = m_selectionWheel->GetSelectedDifficulty();

				Game* game = Game::Create(*diff);
				if(!game)
				{
					Logf("Failed to start game", Logger::Error);
					return;
				}
				game->GetScoring().autoplay = autoplay;

				// Transition to game
				TransitionScreen* transistion = TransitionScreen::Create(game);
				g_application->AddTickable(transistion);
            }
        }
		else
		{
			List<SongFilter*> filters;
			switch (buttonCode)
			{
			case Input::Button::FX_1:
				if (!m_showScores)
				{
					m_canvas->AddAnimation(Ref<IGUIAnimation>(
						new GUIAnimation<float>(&((Canvas::Slot*)m_scoreCanvas->slot)->padding.left, -200.0f, 0.2f)), true);
					m_showScores = !m_showScores;
				}
				else
				{
					m_canvas->AddAnimation(Ref<IGUIAnimation>(
						new GUIAnimation<float>(&((Canvas::Slot*)m_scoreCanvas->slot)->padding.left, 0.0f, 0.2f)), true);
					m_showScores = !m_showScores;
				}
				break;
			case Input::Button::FX_0:
				if (!m_filterSelection->Active)
				{
					g_guiRenderer->SetInputFocus(nullptr);

					m_canvas->AddAnimation(Ref<IGUIAnimation>(
						new GUIAnimation<float>(&((Canvas::Slot*)m_filterSelection->slot)->anchor.left, 0.0, 0.2f)), true);
					m_canvas->AddAnimation(Ref<IGUIAnimation>(
						new GUIAnimation<float>(&((Canvas::Slot*)m_filterSelection->slot)->anchor.right, 1.0f, 0.2f)), true);
					m_canvas->AddAnimation(Ref<IGUIAnimation>(
						new GUIAnimation<float>(&m_fadePanel->color.w, 0.75, 0.25)),true);
					m_filterSelection->Active = !m_filterSelection->Active;
				}
				else
				{
					m_canvas->AddAnimation(Ref<IGUIAnimation>(
						new GUIAnimation<float>(&((Canvas::Slot*)m_filterSelection->slot)->anchor.left, -1.0f, 0.2f)), true);
					m_canvas->AddAnimation(Ref<IGUIAnimation>(
						new GUIAnimation<float>(&((Canvas::Slot*)m_filterSelection->slot)->anchor.right, 0.0f, 0.2f)), true);
					m_canvas->AddAnimation(Ref<IGUIAnimation>(
						new GUIAnimation<float>(&m_fadePanel->color.w, 0.0, 0.25)),true);
					m_filterSelection->Active = !m_filterSelection->Active;
				}
				break;
			default:
				break;
			}

		}
    }

	virtual void OnKeyPressed(int32 key)
	{
		if (m_filterSelection->Active)
		{
			if (key == SDLK_DOWN)
			{
				m_filterSelection->AdvanceSelection(1);

			}
			else if (key == SDLK_UP)
			{
				m_filterSelection->AdvanceSelection(-1);
			}
			else if (key == SDLK_ESCAPE)
			{
				m_canvas->AddAnimation(Ref<IGUIAnimation>(
					new GUIAnimation<float>(&((Canvas::Slot*)m_filterSelection->slot)->anchor.left, -1.0f, 0.2f)), true);
				m_canvas->AddAnimation(Ref<IGUIAnimation>(
					new GUIAnimation<float>(&((Canvas::Slot*)m_filterSelection->slot)->anchor.right, 0.0f, 0.2f)), true);
				m_canvas->AddAnimation(Ref<IGUIAnimation>(
					new GUIAnimation<float>(&m_fadePanel->color.w, 0.0, 0.25)), true);
				m_filterSelection->Active = !m_filterSelection->Active;
			}
		}
		else
		{
			if (key == SDLK_DOWN)
			{
				m_selectionWheel->AdvanceSelection(1);
			}
			else if (key == SDLK_UP)
			{
				m_selectionWheel->AdvanceSelection(-1);
			}
			else if (key == SDLK_PAGEDOWN)
			{
				m_selectionWheel->AdvanceSelection(5);
			}
			else if (key == SDLK_PAGEUP)
			{
				m_selectionWheel->AdvanceSelection(-5);
			}
			else if (key == SDLK_LEFT)
			{
				m_selectionWheel->AdvanceDifficultySelection(-1);
			}
			else if (key == SDLK_RIGHT)
			{
				m_selectionWheel->AdvanceDifficultySelection(1);
			}
			else if (key == SDLK_F5)
			{
				m_mapDatabase.StartSearching();
			}
			else if (key == SDLK_F2)
			{
				m_selectionWheel->SelectRandom();
			}
			else if (key == SDLK_ESCAPE)
			{
				m_suspended = true;
				g_application->RemoveTickable(this);
			}
			else if (key == SDLK_TAB)
			{
				if (m_searchField->HasInputFocus())
					g_guiRenderer->SetInputFocus(nullptr);
				else
					g_guiRenderer->SetInputFocus(m_searchField.GetData());
			}
		}
	}
	virtual void OnKeyReleased(int32 key)
	{
		
	}
	virtual void Tick(float deltaTime) override
	{
		if(m_dbUpdateTimer.Milliseconds() > 500)
		{
			m_mapDatabase.Update();
			m_dbUpdateTimer.Restart();
		}
        
        // Tick navigation
		if (!IsSuspended())
            TickNavigation(deltaTime);

		// Background
		m_previewPlayer.Update(deltaTime);
	}

    void TickNavigation(float deltaTime)
    {

		// Lock mouse to screen when active 
		if(g_gameConfig.GetEnum<Enum_InputDevice>(GameConfigKeys::LaserInputDevice) == InputDevice::Mouse && g_gameWindow->IsActive())
		{
			m_lockMouse = g_input.LockMouse();
		    g_gameWindow->SetCursorVisible(false);
		}
		else
		{
			if(m_lockMouse)
				m_lockMouse.Release();
			g_gameWindow->SetCursorVisible(true);
		}

		
		
        // Song navigation using laser inputs
        float diff_input = g_input.GetInputLaserDir(0);
        float song_input = g_input.GetInputLaserDir(1);
        
        m_advanceDiff += diff_input;
        m_advanceSong += song_input;

		if (!m_filterSelection->Active)
		{
			if ((int)m_advanceDiff != 0)
				m_selectionWheel->AdvanceDifficultySelection((int)m_advanceDiff);
			if ((int)m_advanceSong != 0)
				m_selectionWheel->AdvanceSelection((int)m_advanceSong);
		}
		else
		{
			if ((int)m_advanceDiff != 0)
				m_filterSelection->AdvanceSelection((int)m_advanceSong);
		}
        
        m_advanceDiff -= (int)m_advanceDiff;
        m_advanceSong -= (int)m_advanceSong; 
    }

	virtual void OnSuspend()
	{
		m_suspended = true;
		m_previewPlayer.Pause();
		m_mapDatabase.StopSearching();
		if (m_lockMouse)
			m_lockMouse.Release();

		g_rootCanvas->Remove(m_canvas.As<GUIElementBase>());
	}
	virtual void OnRestore()
	{
		m_suspended = false;
		m_previewPlayer.Restore();
		m_mapDatabase.StartSearching();

		OnSearchTermChanged(m_searchField->GetText());
		
		Canvas::Slot* slot = g_rootCanvas->Add(m_canvas.As<GUIElementBase>());
		slot->anchor = Anchors::Full;
	}
};

SongSelect* SongSelect::Create()
{
	SongSelect_Impl* impl = new SongSelect_Impl();
	return impl;
}
