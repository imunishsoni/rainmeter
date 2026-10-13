// Copyright (c) Rainmeter Team. Source code licensed under GNU GPL v2 (see LICENSE file).

#include "StdAfx.h"
#include "PlayerWMP.h"
#include "Logger.h"

Player* PlayerWMP::c_Player = nullptr;
extern HINSTANCE g_Instance;

namespace {

// Definitions of ATL stuff we need to avoid dependency on atlbase.h (which is not included in free
// versions of Visual Studio).

MIDL_INTERFACE("B6EA2050-048A-11d1-82B9-00C04FB9942E")
IAxWinHostWindow : public IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE CreateControl(
		LPCOLESTR lpTricsData, HWND hWnd, IStream *pStream) = 0;

	virtual HRESULT STDMETHODCALLTYPE CreateControlEx(
		LPCOLESTR lpszTricsData, HWND hWnd, IStream* pStream, IUnknown** ppUnk, REFIID iidAdvise,
		IUnknown* punkSink) = 0;

	virtual HRESULT STDMETHODCALLTYPE AttachControl(IUnknown* pUnkControl, HWND hWnd) = 0;
	virtual HRESULT STDMETHODCALLTYPE QueryControl(REFIID riid, void** ppvObject) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetExternalDispatch(IDispatch* pDisp) = 0;
	virtual HRESULT STDMETHODCALLTYPE SetExternalUIHandler(void* pDisp) = 0;
};

typedef BOOL (WINAPI * AtlAxWinInitFunc)();
typedef HRESULT (WINAPI * AtlAxGetControlFunc)(HWND h, IUnknown** pp);
typedef HRESULT (WINAPI * AtlAxGetHostFunc)(HWND h, IUnknown** pp);

HMODULE InitializeAtlLibrary()
{
	static HMODULE s_ATL = LoadLibrary(L"atl");
	if (s_ATL)
	{
		auto atlAxWinInit = (AtlAxWinInitFunc)GetProcAddress(s_ATL, "AtlAxWinInit");
		atlAxWinInit();
	}
	return s_ATL;
}

}  // namespace

//
// PlayerWMP::CRemoteHost
//

PlayerWMP::CRemoteHost::CRemoteHost() :
	m_Player(),
	m_RefCount(0)
{
}

PlayerWMP::CRemoteHost::~CRemoteHost()
{
	LogNotice(L"NowPlaying WMP: Remote host destroyed");
}

ULONG STDMETHODCALLTYPE PlayerWMP::CRemoteHost::AddRef()
{
	++m_RefCount;
	return m_RefCount;
}

ULONG STDMETHODCALLTYPE PlayerWMP::CRemoteHost::Release()
{
	--m_RefCount;
	if (m_RefCount == 0)
	{
		delete this;
		return 0;
	}
	return m_RefCount;
}

HRESULT STDMETHODCALLTYPE PlayerWMP::CRemoteHost::QueryInterface(IID const& riid, void** object)
{
	if (!object)
	{
		return E_POINTER;
	}

	if (riid == __uuidof(IUnknown) ||
		riid == __uuidof(IServiceProvider))
	{
		*object = (IServiceProvider*)this;
	}
	else if (riid == __uuidof(IWMPRemoteMediaServices))
	{
		*object = (IWMPRemoteMediaServices*)this;
	}
	else if (riid == __uuidof(IWMPEvents))
	{
		*object = (IWMPEvents*)this;
	}
	else
	{
		WCHAR iid[40] = {};
		StringFromGUID2(riid, iid, _countof(iid));
		LogNoticeF(L"NowPlaying WMP: QueryInterface unsupported IID=%s", iid);
		*object = nullptr;
		return E_NOINTERFACE;
	}

	AddRef();
	return S_OK;
}

HRESULT PlayerWMP::CRemoteHost::QueryService(REFGUID guidService, REFIID riid, void** ppv)
{
	WCHAR service[40] = {};
	WCHAR iid[40] = {};
	StringFromGUID2(guidService, service, _countof(service));
	StringFromGUID2(riid, iid, _countof(iid));
	HRESULT hr = QueryInterface(riid, ppv);
	LogNoticeF(L"NowPlaying WMP: QueryService service=%s, IID=%s, result=0x%08x", service, iid, (UINT)hr);
	return hr;
}

HRESULT PlayerWMP::CRemoteHost::GetServiceType(BSTR* pbstrType)
{
	LogNotice(L"NowPlaying WMP: GetServiceType requested RemoteNoDialogs");
	*pbstrType = SysAllocString(L"RemoteNoDialogs");
	return *pbstrType ? S_OK : E_POINTER;
}

HRESULT PlayerWMP::CRemoteHost::GetApplicationName(BSTR* pbstrName)
{
	LogNotice(L"NowPlaying WMP: GetApplicationName requested");
	*pbstrName = SysAllocString(L"Rainmeter NowPlaying");
	return *pbstrName ? S_OK : E_POINTER;
}

HRESULT PlayerWMP::CRemoteHost::GetScriptableObject(BSTR* pbstrName, IDispatch** ppDispatch)
{
	if (pbstrName)
	{
		*pbstrName = nullptr;
	}
	if (ppDispatch)
	{
		*ppDispatch = nullptr;
	}
	return E_NOTIMPL;
}

HRESULT PlayerWMP::CRemoteHost::GetCustomUIMode(BSTR* pbstrFile)
{
	return E_POINTER;
}

void PlayerWMP::CRemoteHost::CurrentItemChange(IDispatch* pdispMedia)
{
	m_Player->m_TrackChanged = true;
}

void PlayerWMP::CRemoteHost::PlayStateChange(long NewState)
{
	LogNoticeF(L"NowPlaying WMP: PlayStateChange state=%ld", NewState);
	switch (NewState)
	{
	case wmppsStopped:
	case wmppsMediaEnded:
		m_Player->ClearData(false);
		break;

	case wmppsPaused:
		m_Player->m_State = STATE_PAUSED;
		break;

	case wmppsPlaying:
		if (m_Player->m_State == STATE_STOPPED)
		{
			m_Player->m_TrackChanged = true;
		}
		m_Player->m_State = STATE_PLAYING;
		break;

	default:
		break;
	}
}

void PlayerWMP::CRemoteHost::SwitchedToControl()
{
	LogNotice(L"NowPlaying WMP: SwitchedToControl; releasing connection");
	m_Player->ClearData();
	m_Player->Uninitialize();
}

void PlayerWMP::CRemoteHost::Disconnect(long Result)
{
	LogNoticeF(L"NowPlaying WMP: Disconnect result=0x%08x", (UINT)Result);
}

void PlayerWMP::CRemoteHost::SwitchedToPlayerApplication()
{
	LogNotice(L"NowPlaying WMP: SwitchedToPlayerApplication");
}

void PlayerWMP::CRemoteHost::PlayerDockedStateChange()
{
	LogNotice(L"NowPlaying WMP: PlayerDockedStateChange");
}

void PlayerWMP::CRemoteHost::PlayerReconnect()
{
	LogNotice(L"NowPlaying WMP: PlayerReconnect");
}

//
// PlayerWMP
//

PlayerWMP::PlayerWMP() : Player(),
	m_TrackChanged(false),
	m_Window(nullptr),
	m_LastCheckTime(0),
	m_LastDiagnosticWindow(nullptr),
	m_LastDiagnosticState(-1),
	m_LastDiagnosticResult(S_OK),
	m_ConnectionCookie(0)
{
}

PlayerWMP::~PlayerWMP()
{
	c_Player = nullptr;
	Uninitialize();
}

Player* PlayerWMP::Create()
{
	if (!c_Player)
	{
		c_Player = new PlayerWMP();
	}

	return c_Player;
}

void PlayerWMP::Initialize()
{
	LogNotice(L"NowPlaying WMP: Initialize started");
	DWORD playerProcess = 0;
	HWND playerWindow = FindWindow(L"WMPlayerApp", nullptr);
	GetWindowThreadProcessId(playerWindow, &playerProcess);
	DWORD rainmeterSession = 0;
	DWORD playerSession = 0;
	BOOL rainmeterSessionFound = ProcessIdToSessionId(GetCurrentProcessId(), &rainmeterSession);
	BOOL playerSessionFound = ProcessIdToSessionId(playerProcess, &playerSession);
	LogNoticeF(L"NowPlaying WMP: Rainmeter PID=%lu, session=%lu, session found=%d; player PID=%lu, session=%lu, session found=%d", GetCurrentProcessId(), rainmeterSession, (int)rainmeterSessionFound, playerProcess, playerSession, (int)playerSessionFound);
	m_LastDiagnosticState = -1;
	HMODULE atl = InitializeAtlLibrary();
	if (!atl)
	{
		LogError(L"NowPlaying: ATL not found");
		return;
	}

	auto atlAxGetControl = (AtlAxGetControlFunc)GetProcAddress(atl, "AtlAxGetControl");
	auto atlAxGetHost = (AtlAxGetHostFunc)GetProcAddress(atl, "AtlAxGetHost");

	WNDCLASS wc = { 0 };
	wc.hInstance = g_Instance;
	wc.lpfnWndProc = DefWindowProc;
	wc.lpszClassName = L"NowPlayingWMP";
	RegisterClass(&wc);

	// Create the container window and the ATL host window.
	m_Window = CreateWindow(
		L"NowPlayingWMP", L"",
		WS_DISABLED,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		nullptr, nullptr, g_Instance, nullptr);

	HWND window = CreateWindow(
		L"AtlAxWin", L"",
		WS_DISABLED | WS_CHILD,
		CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
		m_Window, nullptr, g_Instance, nullptr);

	LogNoticeF(L"NowPlaying WMP: Container window=%p, ATL window=%p", (void*)m_Window, (void*)window);

	Microsoft::WRL::ComPtr<IUnknown> axHost;
	Microsoft::WRL::ComPtr<IObjectWithSite> hostObject;
	const WCHAR* stage = L"AtlAxGetHost";
	HRESULT hr = atlAxGetHost(window, axHost.GetAddressOf());
	if (SUCCEEDED(hr))
	{
		stage = L"Query IObjectWithSite";
		hr = axHost.As(&hostObject);
	}

	Microsoft::WRL::ComPtr<CRemoteHost> remoteHost(new CRemoteHost());
	if (SUCCEEDED(hr))
	{
		remoteHost->m_Player = this;
		stage = L"SetSite";
		hr = hostObject->SetSite((IWMPRemoteMediaServices*)remoteHost.Get());
	}

	Microsoft::WRL::ComPtr<IAxWinHostWindow> axWinHostWindow;
	if (SUCCEEDED(hr))
	{
		stage = L"Query IAxWinHostWindow";
		hr = axHost.As(&axWinHostWindow);
	}

	if (SUCCEEDED(hr))
	{
		stage = L"CreateControl";
		hr = axWinHostWindow->CreateControl(L"{6BF52A52-394A-11D3-B153-00C04F79FAA6}", window, nullptr);
	}

	Microsoft::WRL::ComPtr<IUnknown> axControl;
	if (SUCCEEDED(hr))
	{
		stage = L"AtlAxGetControl";
		hr = atlAxGetControl(window, axControl.GetAddressOf());
	}

	if (SUCCEEDED(hr))
	{
		stage = L"Query IWMPPlayer4";
		hr = axControl.As(&m_IPlayer);
	}

	if (SUCCEEDED(hr))
	{
		VARIANT_BOOL remote = VARIANT_FALSE;
		HRESULT remoteResult = m_IPlayer->get_isRemote(&remote);
		LogNoticeF(L"NowPlaying WMP: Remote connection remote=%d, result=0x%08x", (int)remote, (UINT)remoteResult);
		if (SUCCEEDED(remoteResult) && remote != VARIANT_TRUE)
		{
			Microsoft::WRL::ComPtr<IWMPError> errors;
			HRESULT errorResult = m_IPlayer->get_error(errors.GetAddressOf());
			long count = 0;
			if (SUCCEEDED(errorResult) && errors)
			{
				errorResult = errors->get_errorCount(&count);
			}

			LogNoticeF(L"NowPlaying WMP: Error queue count=%ld, result=0x%08x", count, (UINT)errorResult);
			for (long i = 0; i < count; ++i)
			{
				Microsoft::WRL::ComPtr<IWMPErrorItem> item;
				errorResult = errors->get_item(i, item.GetAddressOf());
				if (SUCCEEDED(errorResult) && item)
				{
					long code = 0;
					BSTR description = nullptr;
					HRESULT codeResult = item->get_errorCode(&code);
					HRESULT descriptionResult = item->get_errorDescription(&description);
					LogWarningF(L"NowPlaying WMP: Error queue item=%ld, code=0x%08x, description=%s, code result=0x%08x, description result=0x%08x", i, (UINT)code, description ? description : L"", (UINT)codeResult, (UINT)descriptionResult);
					SysFreeString(description);
				}
				else
				{
					LogWarningF(L"NowPlaying WMP: Cannot read error queue item=%ld, result=0x%08x", i, (UINT)errorResult);
				}
			}

			LogWarning(L"NowPlaying WMP: Control is running locally");
		}
	}

	// Connect the event interface.
	Microsoft::WRL::ComPtr<IConnectionPointContainer> wmpPlayerConnectionContainer;
	if (SUCCEEDED(hr))
	{
		stage = L"Query IConnectionPointContainer";
		hr = m_IPlayer.As(&wmpPlayerConnectionContainer);
	}

	if (SUCCEEDED(hr))
	{
		stage = L"FindConnectionPoint IWMPEvents";
		hr = wmpPlayerConnectionContainer->FindConnectionPoint(
			__uuidof(IWMPEvents), m_IConnectionPoint.GetAddressOf());
	}

	if (SUCCEEDED(hr))
	{
		stage = L"Advise IWMPEvents";
		hr = m_IConnectionPoint->Advise(remoteHost->GetUnknown(), &m_ConnectionCookie);
		if (!m_ConnectionCookie)
		{
			hr = E_FAIL;
		}
	}

	if (SUCCEEDED(hr))
	{
		stage = L"Get controls";
		hr = m_IPlayer->get_controls(&m_IControls);
	}

	if (SUCCEEDED(hr))
	{
		stage = L"Get settings";
		hr = m_IPlayer->get_settings(&m_ISettings);
	}

	if (SUCCEEDED(hr))
	{
		WMPPlayState state = wmppsUndefined;
		HRESULT stateResult = m_IPlayer->get_playState(&state);
		LogNoticeF(L"NowPlaying WMP: Initial playback state=%d, result=0x%08x", (int)state, (UINT)stateResult);
		if (state == wmppsPlaying)
		{
			m_State = STATE_PLAYING;
		}
		else if (state ==  wmppsPaused)
		{
			m_State = STATE_PAUSED;
		}

		if (m_State != STATE_STOPPED)
		{
			m_TrackChanged = true;
		}

		m_Initialized = true;
		LogNoticeF(L"NowPlaying WMP: Initialize completed; connection cookie=%lu", m_ConnectionCookie);
	}
	else
	{
		LogWarningF(L"NowPlaying WMP: Initialize failed at %s, result=0x%08x", stage, (UINT)hr);
		Uninitialize();
	}
}

void PlayerWMP::Uninitialize()
{
	LogNoticeF(L"NowPlaying WMP: Uninitialize started; initialized=%d, cookie=%lu, window=%p", (int)m_Initialized, m_ConnectionCookie, (void*)m_Window);
	if (m_Initialized)
	{
		m_Initialized = false;

		if (m_ConnectionCookie)
		{
			HRESULT hr = m_IConnectionPoint->Unadvise(m_ConnectionCookie);
			LogNoticeF(L"NowPlaying WMP: Unadvise result=0x%08x", (UINT)hr);
			m_ConnectionCookie = 0;
		}

		LogNotice(L"NowPlaying WMP: Releasing controls, settings, connection point and player");
		m_IControls.Reset();
		m_ISettings.Reset();
		m_IConnectionPoint.Reset();
		m_IPlayer.Reset();
		LogNotice(L"NowPlaying WMP: COM interfaces released");
		BOOL destroyed = DestroyWindow(m_Window);
		DWORD error = destroyed ? ERROR_SUCCESS : GetLastError();
		LogNoticeF(L"NowPlaying WMP: DestroyWindow succeeded=%d, error=%lu", (int)destroyed, error);
		m_Window = nullptr;
		UnregisterClass(L"NowPlayingWMP", g_Instance);
	}

	LogNotice(L"NowPlaying WMP: Uninitialize completed");
}

void PlayerWMP::UpdateData()
{
	if (m_Initialized)
	{
		VARIANT_BOOL remote = VARIANT_FALSE;
		HRESULT remoteResult = m_IPlayer->get_isRemote(&remote);
		VARIANT_BOOL docked = VARIANT_FALSE;
		Microsoft::WRL::ComPtr<IWMPPlayerApplication> application;
		HRESULT hr = m_IPlayer->get_playerApplication(application.GetAddressOf());
		if (SUCCEEDED(hr) && application)
		{
			hr = application->get_playerDocked(&docked);
		}

		HWND window = FindWindow(L"WMPlayerApp", nullptr);
		BOOL visible = IsWindowVisible(window);
		int diagnosticState = (visible ? 1 : 0) | (docked == VARIANT_TRUE ? 2 : 0) | (remote == VARIANT_TRUE ? 4 : 0) | (application ? 8 : 0) | (FAILED(remoteResult) ? 16 : 0);
		if (window != m_LastDiagnosticWindow || diagnosticState != m_LastDiagnosticState || hr != m_LastDiagnosticResult)
		{
			LogNoticeF(L"NowPlaying WMP: Updated remote connection remote=%d, result=0x%08x", (int)remote, (UINT)remoteResult);
			LogNoticeF(L"NowPlaying WMP: Connection state window=%p, visible=%d, docked=%d, application=%d, result=0x%08x", (void*)window, (int)visible, (int)docked, application ? 1 : 0, (UINT)hr);
			m_LastDiagnosticWindow = window;
			m_LastDiagnosticState = diagnosticState;
			m_LastDiagnosticResult = hr;
		}

		application.Reset();

		if (!window)
		{
			LogNotice(L"NowPlaying WMP: Connection state triggered cleanup");
			ClearData();
			Uninitialize();
			return;
		}

		// Get the volume
		long volume = 0L;
		m_ISettings->get_volume(&volume);
		m_Volume = (UINT)volume;

		if (m_State != STATE_STOPPED)
		{
			double position = 0.0;
			m_IControls->get_currentPosition(&position);
			m_Position = (UINT)position;
		}

		if (m_TrackChanged)
		{
			m_TrackChanged = false;

			Microsoft::WRL::ComPtr<IWMPMedia> spMedia;
			m_IPlayer->get_currentMedia(spMedia.GetAddressOf());

			if (spMedia)
			{
				BSTR val = nullptr;

				spMedia->getItemInfo(_bstr_t(L"Artist"), &val);
				m_Artist = val;

				spMedia->getItemInfo(_bstr_t(L"Title"), &val);
				m_Title = val;

				spMedia->getItemInfo(_bstr_t(L"Album"), &val);
				m_Album = val;

				spMedia->getItemInfo(_bstr_t(L"Genre"), &val);
				m_Genre = val;

				spMedia->getItemInfo(_bstr_t(L"UserRating"), &val);
				int rating = _wtoi(val);

				if (rating > 75)
				{
					m_Rating = 5;
				}
				else if (rating > 50)
				{
					m_Rating = 4;
				}
				else if (rating > 25)
				{
					m_Rating = 3;
				}
				else if (rating > 1)
				{
					m_Rating = 2;
				}
				else
				{
					m_Rating = rating;
				}

				spMedia->getItemInfo(_bstr_t(L"WM/TrackNumber"), &val);
				m_Number = (UINT)_wtoi(val);

				spMedia->getItemInfo(_bstr_t(L"WM/Year"), &val);
				m_Year = (UINT)_wtoi(val);

				double duration = 0.0;
				spMedia->get_duration(&duration);
				m_Duration = (UINT)duration;

				BSTR url = nullptr;
				spMedia->get_sourceURL(&url);
				std::wstring targetPath = url;

				if (targetPath != m_FilePath)
				{
					++m_TrackCount;
					m_FilePath = targetPath;

					// Find cover if needed
					// TODO: Fix temp solution
					if (m_Measures & MEASURE_COVER || m_InstanceCount == 0)
					{
						spMedia->getItemInfo(_bstr_t(L"WM/WMCollectionID"), &val);
						targetPath.resize(targetPath.find_last_of(L'\\') + 1);
						targetPath += L"AlbumArt_";
						targetPath += val;
						targetPath += L"_Large.jpg";

						if (_waccess_s(targetPath.c_str(), 0) == 0)
						{
							m_CoverPath = targetPath;
						}
						else
						{
							FindCover();
						}
					}

					if (m_Measures & MEASURE_LYRICS)
					{
						FindLyrics();
					}
				}
			}
		}
	}
	else
	{
		ULONGLONG time = GetTickCount64();

		// Try to find WMP window every 5 seconds
		if (m_LastCheckTime == 0 || time - m_LastCheckTime > 5000)
		{
			m_LastCheckTime = time;

			HWND window = FindWindow(L"WMPlayerApp", nullptr);
			BOOL visible = IsWindowVisible(window);
			int diagnosticState = visible ? 1 : 0;
			if (window != m_LastDiagnosticWindow || diagnosticState != m_LastDiagnosticState)
			{
				LogNoticeF(L"NowPlaying WMP: Looking for player window=%p, visible=%d", (void*)window, (int)visible);
				m_LastDiagnosticWindow = window;
				m_LastDiagnosticState = diagnosticState;
			}

			if (window)
			{
				Initialize();
			}
		}
	}
}

void PlayerWMP::Pause()
{
	m_IControls->pause();
}

void PlayerWMP::Play()
{
	m_IControls->play();
}

void PlayerWMP::Stop()
{
	m_IControls->stop();
	// TODO: FIXME
	m_State = STATE_STOPPED;
}

void PlayerWMP::Next()
{
	m_IControls->next();
}

void PlayerWMP::Previous()
{
	m_IControls->previous();
}

void PlayerWMP::SetPosition(int position)
{
	m_IControls->put_currentPosition((double)position);
}

void PlayerWMP::SetRating(int rating)
{
	if (m_State != STATE_STOPPED)
	{
		Microsoft::WRL::ComPtr<IWMPMedia> spMedia;
		m_IPlayer->get_currentMedia(spMedia.GetAddressOf());

		if (spMedia)
		{
			const WCHAR* val;
			switch (rating)
			{
			case 0:
				val = L"0";
				break;

			case 1:
				val = L"1";
				break;

			case 2:
				val = L"25";
				break;

			case 3:
				val = L"50";
				break;

			case 4:
				val = L"75";
				break;

			default: // case 5:
				val = L"99";
				break;
			}

			spMedia->setItemInfo(_bstr_t(L"UserRating"), (BSTR)val);
			m_Rating = rating;
		}
	}
}

void PlayerWMP::SetVolume(int volume)
{
	m_ISettings->put_volume(volume);
}

void PlayerWMP::ClosePlayer()
{
	HWND wnd = FindWindow(L"WMPlayerApp", nullptr);
	LogNoticeF(L"NowPlaying WMP: ClosePlayer requested; window=%p", (void*)wnd);

	if (wnd)
	{
		ClearData();
		Uninitialize();
		LogNotice(L"NowPlaying WMP: Sending WM_CLOSE");
		SendMessage(wnd, WM_CLOSE, 0, 0);
		LogNoticeF(L"NowPlaying WMP: WM_CLOSE returned; window exists=%d, visible=%d", (int)IsWindow(wnd), (int)IsWindowVisible(wnd));
	}
}

void PlayerWMP::OpenPlayer(std::wstring& path)
{
	ShellExecute(nullptr, L"open", path.empty() ? L"wmplayer.exe" : path.c_str(), nullptr, nullptr, SW_SHOW);
}
