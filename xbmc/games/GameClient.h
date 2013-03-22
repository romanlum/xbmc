/*
 *      Copyright (C) 2012 Garrett Brown
 *      Copyright (C) 2012 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */
#pragma once

#include "addons/Addon.h"
#include "FileItem.h"
#include "GameClientDLL.h"
#include "GameFileLoader.h"
#include "games/savegames/Savestate.h"
#include "games/tags/GameInfoTagLoader.h"
#include "SerialState.h"
#include "threads/CriticalSection.h"

#include <set>

#define GAMECLIENT_MAX_PLAYERS  8

namespace ADDON
{
  /**
   * The core configuration parameters of game clients have been put in a self-
   * contained struct. This way, CGameManager can perform logic operations using
   * config objects as a representation of the game client, without actually
   * holding a pointer to an entire game client. Only data pertinent to
   * CGameManager is currently placed in this class.
   */
  struct GameClientConfig
  {
    CStdString                   id;          // Set from addon.xml
    std::set<CStdString>         extensions;  // Set from addon.xml. Updated when the DLL is loaded
    GAME_INFO::GamePlatformArray platforms;   // Set from addon.xml
    bool                         bAllowVFS;   // Set when the DLL is loaded
    /**
     * If false, and ROM is in a zip, ROM file must be loaded from within the
     * zip instead of extracted to a temporary cache. In XBMC's case, loading
     * from the VFS is like extraction because the relative paths to the
     * ROM's other files are not available to the emulator.
     */
    bool                         bRequireZip; // Set when the DLL is loaded
  };

  class CGameClient;
  typedef boost::shared_ptr<CGameClient> GameClientPtr;

  class CGameClient : public CAddon
  {
  public:
    /**
     * Callback container. Data is passed in and out of the game client through
     * these callbacks.
     */
    struct DataReceiver
    {
      typedef void    (*VideoFrame_t)          (const void *data, unsigned width, unsigned height, size_t pitch);
      typedef void    (*AudioSample_t)         (int16_t left, int16_t right);
      typedef size_t  (*AudioSampleBatch_t)    (const int16_t *data, size_t frames);
      // Actually a "data sender", but who's looking
      typedef int16_t (*GetInputState_t)       (unsigned port, unsigned device, unsigned index, unsigned id);
      typedef void    (*SetPixelFormat_t)      (retro_pixel_format format); // retro_pixel_format defined in libretro.h
      typedef void    (*SetKeyboardCallback_t) (retro_keyboard_event_t callback); // retro_keyboard_event_t defined in libretro.h

      VideoFrame_t          VideoFrame;
      AudioSample_t         AudioSample;
      AudioSampleBatch_t    AudioSampleBatch;
      GetInputState_t       GetInputState;
      SetPixelFormat_t      SetPixelFormat;
      SetKeyboardCallback_t SetKeyboardCallback;

      DataReceiver(VideoFrame_t vf, AudioSample_t as, AudioSampleBatch_t asb, GetInputState_t is, SetPixelFormat_t spf, SetKeyboardCallback_t skc)
        : VideoFrame(vf), AudioSample(as), AudioSampleBatch(asb), GetInputState(is), SetPixelFormat(spf), SetKeyboardCallback(skc) { }
    };

    CGameClient(const AddonProps &props);

    CGameClient(const cp_extension_t *props);

    virtual ~CGameClient() { DeInit(); }

    /**
     * Load the DLL and query basic parameters. After Init() is called, the
     * Get*() and CanOpen() functions may be called.
     */
    bool Init();

    // Cleanly shut down and unload the DLL.
    void DeInit();

    /**
     * Perform the gamut of checks on the file: "gameclient" property, platform,
     * extension, and a positive match on at least one of the CGameFileLoader
     * strategies. If config.bAllowVFS and config.bRequireZip are provided, then
     * useStrategies=true can be used to allow more lenient/accurate testing,
     * especially for files inside zips (when .zip isn't supported) and files
     * on the VFS.
     */
    bool CanOpen(const CFileItem &file, bool useStrategies = false) const;

    bool OpenFile(const CFileItem &file, const DataReceiver &callbacks);
    void CloseFile();

    const CStdString &GetFilePath() const { return m_gamePath; }

    // Returns true after Init() is called and until DeInit() is called.
    bool IsInitialized() const { return m_dll.IsLoaded(); }

    const GameClientConfig &GetConfig() const { return m_config; }

    // Precondition: Init() must be called first and return true.
    const CStdString &GetClientName() const { return m_clientName; }

    // Precondition: Init() must be called first and return true.
    const CStdString &GetClientVersion() const { return m_clientVersion; }

    /**
     * Find the region of a currently running game. The return value will be
     * RETRO_REGION_NTSC, RETRO_REGION_PAL or -1 for invalid.
     */
    int GetRegion() { return m_region; }

    /**
     * Each port (or player, if you will) must be associated with a device. The
     * default device is RETRO_DEVICE_JOYPAD. For a list of valid devices, see
     * libretro.h.
     *
     * Do not exceed the number of devices that the game client supports. A
     * quick analysis of SNES9x Next v2 showed that a third port will overflow
     * a buffer. Currently, there is no way to determine the number of ports a
     * client will support, so stick with 1.
     *
     * Precondition: OpenFile() must return true.
     */
    void SetDevice(unsigned int port, unsigned int device);

    /**
     * Allow the game to run and produce a video frame. Precondition:
     * OpenFile() returned true.
     */
    void RunFrame();

    /**
     * Load the serialized state from the auto-save slot (filename looks like
     * feba62c2.savestate). Returns true if the next call to Load() or Save()
     * is expected to succeed (such as if the file can't be loaded because it
     * doesn't exist, but Save() will create the file and Load() and Save()
     * will work after that).
     *
     * Savestates are placed in special://savegames/gameclient.id/
     */
    bool AutoLoad();

    /**
     * Load the serialized state from the numbered slot (filename looks like
     * feba62c2_1.savestate).
     */
    bool Load(unsigned int slot);

    /**
     * Load the serialized state from the specified path.
     */
    bool Load(const CStdString &saveStatePath);

    /**
     * Commit the current serialized state to the local drive (filename looks
     * like feba62c2.savestate).
     */
    bool AutoSave();

    /**
     * Commit the current serialized state to the local drive (filename looks
     * like feba62c2_1.savestate).
     */
    bool Save(unsigned int slot);

    /**
     * Commit the current serialized state to the local drive. The CRC of the
     * label is concatenated to the CRC of the game file, and the resulting
     * filename looks like feba62c2_bdcb488a.savestate
     */
    bool Save(const CStdString &label);

    /**
     * Rewind gameplay 'frames' frames.
     * As there is a fixed size buffer backing
     * save state deltas, it might not be possible to rewind as many
     * frames as desired. The function returns number of frames actually rewound.
     */
    unsigned int RewindFrames(unsigned int frames);

    // Returns how many frames it is possible to rewind with a call to RewindFrames().
    size_t GetAvailableFrames() const { return m_rewindSupported ? m_serialState.GetFramesAvailable() : 0; }

    // Returns the maximum amount of frames that can ever be rewound.
    size_t GetMaxFrames() const { return m_rewindSupported ? m_serialState.GetMaxFrames() : 0; }

    // Reset the game, if running.
    void Reset();

    // Video framerate is used to calculate savestate wall time
    double GetFrameRate() const { return m_frameRate; }
    void SetFrameRate(double framerate);
    double GetSampleRate() const { return m_sampleRate; }

    /**
     * If the game client was a bad boy and provided no extensions, this will
     * optimistically return true.
     */
    bool IsExtensionValid(const CStdString &ext) const;

  private:
    void Initialize();

    /**
     * Init the savestate file by setting the game path, game client and game
     * CRC.
     *
     * gameBuffer and length are convenience variables to avoid hitting the
     * disk for CRC calculation when the game file is already loaded in RAM.
     */
    bool InitSaveState(const void *gameBuffer = NULL, size_t length = 0);

    // Internal load function. 
    bool Load();

    // Internal save function.
    bool Save();

    /**
     * Given the strategies above, order them in the way that respects
     * g_guiSettings.GetBool("gamesdebug.prefervfs").
     */
    static void GetStrategy(CGameFileLoaderUseHD &hd, CGameFileLoaderUseParentZip &outerzip,
        CGameFileLoaderUseVFS &vfs, CGameFileLoaderEnterZip &innerzip, CGameFileLoader *strategies[4]);

    /**
     * Parse a pipe-separated list, returned from the game client, into an
     * array. The extensions list can contain both upper and lower case
     * extensions; only lower-case extensions are stored in m_validExtensions.
     */
    void SetExtensions(const CStdString &strExtensionList);
    void SetPlatforms(const CStdString &strPlatformList);

    GameClientConfig m_config;

    GameClientDLL    m_dll;
    bool             m_bIsInited; // Keep track of whether m_dll.retro_init() has been called
    bool             m_bIsPlaying; // This is true between retro_load_game() and retro_unload_game()
    CStdString       m_gamePath; // path of the current playing file

    // Returned by m_dll:
    CStdString       m_clientName;
    CStdString       m_clientVersion;
    double           m_frameRate; // Video framerate
    double           m_sampleRate; // Audio frequency
    int              m_region; // Region of the loaded game

    CCriticalSection m_critSection;
    bool             m_rewindSupported;
    CSerialState     m_serialState;
    CSavestate       m_saveState;

    // If rewinding is disabled, use a buffer to avoid re-allocation when saving games
    std::vector<uint8_t> m_savestateBuffer;

    /**
     * This callback exists to give XBMC a chance to poll for input. XBMC already
     * takes care of this, so the callback isn't needed.
     */
    static void NoopPoop() { }
  };
}
