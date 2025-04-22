#define _WIN32_DCOM
#define _WIN32_WINNT 0x0502 // Target Windows XP SP2/SP3 minimum

#include <iostream>       // For standard I/O (cout, cerr, cin)
#include <string>         // For std::string and std::wstring
#include <vector>         // Potentially useful later
#include <windows.h>      // Core Windows API functions
#include <comdef.h>       // For _com_error, VARIANT types, _bstr_t
#include <wbemidl.h>      // Main WMI header
#include <sstream>        // For converting numbers to strings (stringstream)
#include <map>            // Needed for requirements DB (std::map)
#include <winreg.h>       // Needed for registry access (RegOpenKeyExW, etc.)
#include <wincon.h>       // Required for console color functions (GetStdHandle, etc.)
#include <limits>         // Required for numeric_limits (used for clearing cin errors)
#include <ios>            // Required for streamsize
#include <cwchar>         // For wcstok_s (needed in GetSecurityInfo)
#include <cctype>         // For toupper

// --- Console Color Definitions ---
#define FG_BLACK            0
#define FG_BLUE             FOREGROUND_BLUE
#define FG_GREEN            FOREGROUND_GREEN
#define FG_RED              FOREGROUND_RED
// Define combined colors explicitly
#define FOREGROUND_CYAN     (FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_MAGENTA  (FOREGROUND_RED | FOREGROUND_BLUE)
#define FOREGROUND_YELLOW   (FOREGROUND_RED | FOREGROUND_GREEN)
#define FOREGROUND_WHITE    (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)

#define FG_CYAN             FOREGROUND_CYAN
#define FG_MAGENTA          FOREGROUND_MAGENTA
#define FG_YELLOW           FOREGROUND_YELLOW
#define FG_GRAY             FOREGROUND_INTENSITY // Often looks gray on default background
#define FG_DARK_GRAY        FOREGROUND_INTENSITY // Alias for clarity
#define FG_LIGHT_BLUE       (FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define FG_LIGHT_GREEN      (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define FG_LIGHT_CYAN       (FOREGROUND_CYAN | FOREGROUND_INTENSITY)
#define FG_LIGHT_RED        (FOREGROUND_RED | FOREGROUND_INTENSITY)
#define FG_LIGHT_MAGENTA    (FOREGROUND_MAGENTA | FOREGROUND_INTENSITY)
#define FG_LIGHT_YELLOW     (FOREGROUND_YELLOW | FOREGROUND_INTENSITY)
#define FG_WHITE            (FOREGROUND_WHITE | FOREGROUND_INTENSITY)

const WORD COLOR_DEFAULT       = FG_GRAY;
const WORD COLOR_HEADING       = FG_LIGHT_CYAN;
const WORD COLOR_LABEL         = FG_WHITE;
const WORD COLOR_VALUE         = FG_LIGHT_GREEN;
const WORD COLOR_VALUE_WARN    = FG_LIGHT_YELLOW;
const WORD COLOR_VALUE_FAIL    = FG_LIGHT_RED;
const WORD COLOR_ERROR         = FG_LIGHT_RED;
const WORD COLOR_WARNING       = FG_LIGHT_YELLOW;
const WORD COLOR_INFO          = FG_LIGHT_BLUE;
const WORD COLOR_SUCCESS       = FG_LIGHT_GREEN;
const WORD COLOR_FAILURE       = FG_LIGHT_RED;
const WORD COLOR_NOTE          = FG_DARK_GRAY;

// --- Console Color Helper Variables and Functions ---
HANDLE hConsole = INVALID_HANDLE_VALUE;
WORD defaultConsoleAttributes = COLOR_DEFAULT;

void InitConsoleColor() {
    hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
        if (GetConsoleScreenBufferInfo(hConsole, &consoleInfo)) {
            defaultConsoleAttributes = consoleInfo.wAttributes;
        } else { defaultConsoleAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; } // Fallback to white
    }
}
void SetConsoleColor(WORD color) { if (hConsole != INVALID_HANDLE_VALUE) SetConsoleTextAttribute(hConsole, color); }
void ResetConsoleColor() { if (hConsole != INVALID_HANDLE_VALUE) SetConsoleTextAttribute(hConsole, defaultConsoleAttributes); }
// --- End Console Color ---


// --- Structures for Collected System Info ---
struct CpuInfo {
    std::wstring Name = L"N/A"; std::wstring Architecture = L"N/A"; UINT MaxClockSpeed = 0;
    UINT NumberOfCores = 0; UINT NumberOfLogicalProcessors = 0; bool Is64BitCapable = false;
    UINT MinCpuGenerationLevel = 0; // Used only in Simulation Mode for Win11 check proxy
};
struct RamInfo { ULONGLONG TotalPhysicalBytes = 0; };
struct DiskInfo { ULONGLONG TotalBytes = 0; ULONGLONG FreeBytesAvailableToUser = 0; wchar_t DriveLetter = L'?'; };
struct OsInfo { std::wstring Caption = L"N/A"; std::wstring Version = L"N/A"; std::wstring BuildNumber = L"N/A"; std::wstring OSArchitecture = L"N/A"; std::wstring ServicePackMajorVersion = L"N/A"; };
struct FirmwareInfo { std::wstring FirmwareType = L"Unknown"; };
struct GraphicsInfo { std::wstring Name = L"N/A"; UINT32 AdapterRAM = 0; std::wstring DriverVersion = L"N/A"; std::wstring VideoProcessor = L"N/A"; std::wstring DirectXFeatureLevel = L"N/A"; std::wstring WDDMVersion = L"N/A"; };
struct ScreenInfo { int Width = 0; int Height = 0; };
struct SecurityInfo { bool TpmEnabled = false; bool TpmFound = false; UINT32 TpmSpecVersionMajor = 0; UINT32 TpmSpecVersionMinor = 0; std::wstring TpmVersionString = L"N/A"; bool SecureBootEnabled = false; bool SecureBootCapable = false; std::wstring SecureBootStatus = L"N/A"; };
struct DirectXInfo { std::wstring InstalledVersion = L"N/A"; };


// --- Requirements Structure ---
struct WindowsRequirements {
    std::wstring Name;
    UINT MinCpuSpeedMHz = 0; UINT MinCpuCores = 0; bool Require64Bit = false; UINT MinCpuGenerationLevel = 0;
    ULONGLONG MinRamBytes = 0; ULONGLONG MinDiskFreeBytes = 0;
    UINT MinDirectXFeatureLevelMajor = 0; UINT MinWDDMVersionMajor = 0;
    UINT MinScreenWidth = 0; UINT MinScreenHeight = 0;
    bool RequireUEFI = false; bool RequireSecureBoot = false; bool RequireTpm = false; UINT MinTpmVersionMajor = 0;
    bool RequireInternetForSetup = false;
};

// --- Global Requirements Database ---
std::map<std::wstring, WindowsRequirements> windowsRequirementsDB;

// --- Function Prototypes ---
bool InitializeWMI(IWbemServices*& pSvc);
void CleanupWMI(IWbemServices* pSvc);
bool GetCpuInfoWMI(IWbemServices* pSvc, CpuInfo& cpuInfo);
bool GetRamInfoAPI(RamInfo& ramInfo);
bool GetDiskInfoAPI(DiskInfo& diskInfo);
bool GetOsInfoWMI(IWbemServices* pSvc, OsInfo& osInfo);
bool GetFirmwareTypeAPI(FirmwareInfo& firmwareInfo);
bool GetGraphicsInfoWMI(IWbemServices* pSvc, GraphicsInfo& graphicsInfo);
bool GetScreenResolutionAPI(ScreenInfo& screenInfo);
bool GetSecurityInfo(IWbemServices* pSvc, SecurityInfo& secInfo, const FirmwareInfo& firmwareInfo);
bool GetDirectXVersionRegistry(DirectXInfo& dxInfo);
std::wstring GetProcessorArchitectureString(WORD processorArchitecture);
void PopulateRequirementsDB();
bool GetSimulatedSystemInfo(CpuInfo& cpu, RamInfo& ram, DiskInfo& disk, OsInfo& os, FirmwareInfo& firm, GraphicsInfo& graph, ScreenInfo& screen, SecurityInfo& sec, DirectXInfo& dx);
void CompareRequirements(const WindowsRequirements& target, const CpuInfo& cpu, const RamInfo& ram, const DiskInfo& disk, const OsInfo& os, const FirmwareInfo& firm, const GraphicsInfo& graph, const ScreenInfo& screen, const SecurityInfo& sec, const DirectXInfo& dx);
int GetIntInput(const std::string& prompt); // Helper for simulation
bool GetBoolInput(const std::string& prompt); // Helper for simulation


// --- Main Function ---
int main() {
    InitConsoleColor(); // Initialize console color handling first

    HRESULT hres;
    bool wmiInitialized = false;
    IWbemServices* pSvc = NULL;
    bool simulationMode = false; // Flag for simulation mode

    // --- Populate Requirements Database ---
    PopulateRequirementsDB();

    // --- Mode Selection ---
    char modeChoice = ' ';
    while (modeChoice != 'L' && modeChoice != 'S') {
        SetConsoleColor(COLOR_HEADING); std::cout << "\nSelect Mode:" << std::endl; ResetConsoleColor();
        SetConsoleColor(COLOR_LABEL); std::cout << "  [L] Live System Detection" << std::endl;
        SetConsoleColor(COLOR_LABEL); std::cout << "  [S] Simulation Mode (Manual Input)" << std::endl; ResetConsoleColor();
        std::cout << "Enter choice (L/S): ";
        std::cin >> modeChoice;
        modeChoice = toupper(modeChoice); // Convert to uppercase
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); // Clear input buffer
    }

    // --- Declare Info Structs ---
    CpuInfo cpuDetails = {}; RamInfo ramDetails = {}; DiskInfo diskDetails = {}; OsInfo osDetails = {};
    FirmwareInfo firmwareDetails = {}; GraphicsInfo graphicsDetails = {}; ScreenInfo screenDetails = {};
    SecurityInfo securityDetails = {}; DirectXInfo directXDetails = {};


    if (modeChoice == 'S') {
        // --- Simulation Mode ---
        simulationMode = true;
        SetConsoleColor(COLOR_HEADING); std::cout << "\n*** RUNNING IN SIMULATION MODE ***" << std::endl; ResetConsoleColor();
        if (!GetSimulatedSystemInfo(cpuDetails, ramDetails, diskDetails, osDetails, firmwareDetails, graphicsDetails, screenDetails, securityDetails, directXDetails)) {
            SetConsoleColor(COLOR_ERROR); std::cerr << "Failed to get simulation input. Exiting." << std::endl; ResetConsoleColor();
            std::cout << "\nPress Enter to exit..."; std::cin.get(); return 1;
        }
        osDetails.Caption = L"Simulated System"; // Override OS Caption for simulation

    } else {
        // --- Live Detection Mode ---
        simulationMode = false;
        SetConsoleColor(COLOR_HEADING); std::cout << "\n--- Running Live System Detection ---" << std::endl; ResetConsoleColor();

        // Initialize COM/WMI (only needed for live detection)
        hres = CoInitializeEx(0, COINIT_MULTITHREADED);
        if (FAILED(hres)) { SetConsoleColor(COLOR_ERROR); std::cerr << "Fatal Error: Failed to initialize COM library. Error code = 0x" << std::hex << hres << std::endl; ResetConsoleColor(); /* Decide if exit needed */ }
        hres = CoInitializeSecurity( NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL );
        if (FAILED(hres) && hres != RPC_E_TOO_LATE) { SetConsoleColor(COLOR_WARNING); std::cerr << "Warning: Failed to initialize security. Error code = 0x" << std::hex << hres << ". WMI queries might fail." << std::endl; ResetConsoleColor(); }
        else { SetConsoleColor(COLOR_SUCCESS); std::cout << "COM Initialized Successfully!" << std::endl; ResetConsoleColor(); }
        wmiInitialized = InitializeWMI(pSvc);
        if (!wmiInitialized) { SetConsoleColor(COLOR_ERROR); std::cerr << "Error: Could not initialize WMI connection. WMI-dependent info will be unavailable." << std::endl; ResetConsoleColor(); }
        else { SetConsoleColor(COLOR_SUCCESS); std::cout << "WMI Connected Successfully!" << std::endl; ResetConsoleColor(); }

        SetConsoleColor(COLOR_HEADING); std::cout << "\n--- Gathering System Information ---" << std::endl; ResetConsoleColor();

        // Call all Get... functions
        SetConsoleColor(COLOR_LABEL); std::cout << "Checking CPU..." << std::endl; ResetConsoleColor();
        GetCpuInfoWMI(pSvc, cpuDetails); // Handles WMI fail + API fallback inside

        SetConsoleColor(COLOR_LABEL); std::cout << "Checking RAM..." << std::endl; ResetConsoleColor();
        GetRamInfoAPI(ramDetails); // Logs error inside

        SetConsoleColor(COLOR_LABEL); std::cout << "Checking System Disk..." << std::endl; ResetConsoleColor();
        GetDiskInfoAPI(diskDetails); // Logs error inside

        SetConsoleColor(COLOR_LABEL); std::cout << "Checking Operating System..." << std::endl; ResetConsoleColor();
        GetOsInfoWMI(pSvc, osDetails); // Handles WMI fail + API fallback inside

        SetConsoleColor(COLOR_LABEL); std::cout << "Checking Firmware..." << std::endl; ResetConsoleColor();
        GetFirmwareTypeAPI(firmwareDetails); // Logs error/info inside

        SetConsoleColor(COLOR_LABEL); std::cout << "Checking Screen Resolution..." << std::endl; ResetConsoleColor();
        GetScreenResolutionAPI(screenDetails); // Logs warning inside

        SetConsoleColor(COLOR_LABEL); std::cout << "Checking DirectX Runtime..." << std::endl; ResetConsoleColor();
        GetDirectXVersionRegistry(directXDetails); // Logs info/error inside

        SetConsoleColor(COLOR_LABEL); std::cout << "Checking Security Features..." << std::endl; ResetConsoleColor();
        GetSecurityInfo(pSvc, securityDetails, firmwareDetails); // Logs status/errors inside

        SetConsoleColor(COLOR_LABEL); std::cout << "Checking Graphics Card..." << std::endl; ResetConsoleColor();
        if (wmiInitialized) { GetGraphicsInfoWMI(pSvc, graphicsDetails); } // Logs error inside
        else { SetConsoleColor(COLOR_WARNING); std::cerr << "  Skipping WMI Graphics check as WMI failed to initialize." << std::endl; ResetConsoleColor(); }

        SetConsoleColor(COLOR_HEADING); std::cout << "\n--- System Information Gathering Complete ---" << std::endl; ResetConsoleColor();

    } // End of Live Detection Mode block


    // --- Target OS Selection ---
    std::wstring targetOSKey;
    bool validTarget = false;
    while (!validTarget) {
        SetConsoleColor(COLOR_HEADING); std::cout << "\n--- Select Target Windows Version ---" << std::endl; ResetConsoleColor();
        SetConsoleColor(COLOR_LABEL); std::cout << "Available keys: ";
        for (const auto& pair : windowsRequirementsDB) { std::wcout << pair.first << L" "; }
        std::cout << std::endl; ResetConsoleColor();
        std::cout << "Enter target OS key (e.g., 11): ";
        std::wcin >> targetOSKey;
        std::wcin.ignore(std::numeric_limits<std::streamsize>::max(), L'\n'); // Clear buffer after wcin

        if (windowsRequirementsDB.count(targetOSKey)) {
            validTarget = true;
        } else {
            SetConsoleColor(COLOR_ERROR); std::wcerr << L"Invalid key '" << targetOSKey << L"'. Please try again." << std::endl; ResetConsoleColor();
            std::wcin.clear(); // Clear error flags if any
        }
    }
    const WindowsRequirements& targetReqs = windowsRequirementsDB[targetOSKey];


    // --- Call Comparison Function ---
    SetConsoleColor(COLOR_HEADING); std::wcout << L"\n--- Comparing System Specs against " << targetReqs.Name << L" ---" << std::endl; ResetConsoleColor();
    CompareRequirements(targetReqs, cpuDetails, ramDetails, diskDetails, osDetails, firmwareDetails, graphicsDetails, screenDetails, securityDetails, directXDetails);


    // --- Cleanup (Only if Live Detection ran) ---
    if (!simulationMode) {
        if (wmiInitialized) { CleanupWMI(pSvc); SetConsoleColor(COLOR_INFO); std::cout << "\nWMI Cleaned up." << std::endl; ResetConsoleColor(); }
        CoUninitialize(); SetConsoleColor(COLOR_INFO); std::cout << "COM Uninitialized." << std::endl; ResetConsoleColor();
    }

    // --- Wait for user ---
    SetConsoleColor(COLOR_DEFAULT); // Ensure default color before exit prompt
    std::cout << "\nPress Enter to exit...";
    std::cin.clear();
    std::cin.get();
    return 0;
}


// --- Function Implementations ---

void PopulateRequirementsDB() {
    SetConsoleColor(COLOR_INFO); std::cout << "Populating requirements database..." << std::endl; ResetConsoleColor();
    // --- Windows Vista ---
    windowsRequirementsDB[L"Vista"] = { L"Windows Vista", 800, 1, false, 0, 512ULL * 1024 * 1024, 15ULL * 1024 * 1024 * 1024, 9, 0, 800, 600, false, false, false, 0, false };
    // --- Windows 7 ---
    windowsRequirementsDB[L"7"] = { L"Windows 7", 1000, 1, false, 0, 1ULL * 1024 * 1024 * 1024, 16ULL * 1024 * 1024 * 1024, 9, 1, 800, 600, false, false, false, 0, false };
    // --- Windows 8.1 ---
    windowsRequirementsDB[L"8.1"] = { L"Windows 8.1", 1000, 1, false, 0, 1ULL * 1024 * 1024 * 1024, 16ULL * 1024 * 1024 * 1024, 9, 1, 1024, 768, false, false, false, 0, false };
    // --- Windows 10 ---
    windowsRequirementsDB[L"10"] = { L"Windows 10", 1000, 1, false, 0, 1ULL * 1024 * 1024 * 1024, 32ULL * 1024 * 1024 * 1024, 9, 1, 800, 600, false, false, false, 0, false };
    // --- Windows 11 ---
    windowsRequirementsDB[L"11"] = { L"Windows 11", 1000, 2, true, 8, 4ULL * 1024 * 1024 * 1024, 64ULL * 1024 * 1024 * 1024, 12, 2, 1280, 720, true, true, true, 2, true };
}

bool InitializeWMI(IWbemServices*& pSvc) {
    pSvc = NULL; HRESULT hres; IWbemLocator* pLoc = NULL;
    hres = CoCreateInstance( CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hres)) { SetConsoleColor(COLOR_ERROR); std::cerr << "Error: Failed to create IWbemLocator. Code=0x" << std::hex << hres << std::endl; ResetConsoleColor(); return false; }
    hres = pLoc->ConnectServer( _bstr_t(L"ROOT\\CIMV2"), NULL, NULL, NULL, 0L, NULL, NULL, &pSvc );
    if (FAILED(hres)) { SetConsoleColor(COLOR_ERROR); std::cerr << "Error: Could not connect WMI namespace. Code=0x" << std::hex << hres << std::endl; ResetConsoleColor(); pLoc->Release(); return false; }
    hres = CoSetProxyBlanket( pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE );
    if (FAILED(hres)) { SetConsoleColor(COLOR_ERROR); std::cerr << "Error: Could not set proxy blanket. Code=0x" << std::hex << hres << std::endl; ResetConsoleColor(); pSvc->Release(); pSvc = NULL; pLoc->Release(); return false; }
    pLoc->Release(); return true;
}

void CleanupWMI(IWbemServices* pSvc) { if (pSvc) { pSvc->Release(); } }

bool GetCpuInfoWMI(IWbemServices* pSvc, CpuInfo& cpuInfo) {
    bool wmiOk = false;
    if (pSvc) {
        IEnumWbemClassObject* pEnumerator = NULL;
        HRESULT hres = pSvc->ExecQuery( bstr_t(L"WQL"), bstr_t(L"SELECT Name, MaxClockSpeed, NumberOfCores, NumberOfLogicalProcessors FROM Win32_Processor"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
        if (SUCCEEDED(hres) && pEnumerator) {
            IWbemClassObject* pclsObj = NULL; ULONG uReturn = 0;
            if (SUCCEEDED(pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn != 0) {
                wmiOk = true; VARIANT vtProp; VariantInit(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"Name", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BSTR) { cpuInfo.Name = vtProp.bstrVal; } VariantClear(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"MaxClockSpeed", 0, &vtProp, 0, 0)) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) { cpuInfo.MaxClockSpeed = vtProp.uintVal; } VariantClear(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"NumberOfCores", 0, &vtProp, 0, 0)) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) { cpuInfo.NumberOfCores = vtProp.uintVal; } VariantClear(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"NumberOfLogicalProcessors", 0, &vtProp, 0, 0)) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) { cpuInfo.NumberOfLogicalProcessors = vtProp.uintVal; } VariantClear(&vtProp);
                pclsObj->Release();
            } else { if (!pEnumerator) { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: Failed WMI enumerator for CPU." << std::endl; ResetConsoleColor(); }}
            if (pEnumerator) { pEnumerator->Release(); }
        } else { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: WMI query Win32_Processor failed. Code=0x" << std::hex << hres << std::endl; ResetConsoleColor(); }
    }
    // Always get API info for architecture and fallback counts
    SYSTEM_INFO sysInfo; GetSystemInfo(&sysInfo);
    cpuInfo.Architecture = GetProcessorArchitectureString(sysInfo.wProcessorArchitecture);
    cpuInfo.Is64BitCapable = (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 || sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64);
    if (wmiOk && cpuInfo.NumberOfLogicalProcessors == 0) { cpuInfo.NumberOfLogicalProcessors = sysInfo.dwNumberOfProcessors; SetConsoleColor(COLOR_INFO); std::cout << "  (Info: API fallback for logical proc count)" << std::endl; ResetConsoleColor(); }
    if (!wmiOk) { // If WMI failed entirely, use API logical count
         cpuInfo.NumberOfLogicalProcessors = sysInfo.dwNumberOfProcessors;
         cpuInfo.NumberOfCores = 0; cpuInfo.MaxClockSpeed = 0; cpuInfo.Name = L"N/A (WMI Failed)";
    }
    return wmiOk;
}

std::wstring GetProcessorArchitectureString(WORD arch) {
    switch (arch) {
        case PROCESSOR_ARCHITECTURE_AMD64: return L"x64 (64-bit)"; case PROCESSOR_ARCHITECTURE_ARM: return L"ARM";
        case PROCESSOR_ARCHITECTURE_ARM64: return L"ARM64"; case PROCESSOR_ARCHITECTURE_IA64: return L"Itanium (IA-64)";
        case PROCESSOR_ARCHITECTURE_INTEL: return L"x86 (32-bit)"; default: return L"Unknown/Other";
    }
}

bool GetRamInfoAPI(RamInfo& ramInfo) {
    MEMORYSTATUSEX statex; statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex)) { ramInfo.TotalPhysicalBytes = statex.ullTotalPhys; return true; }
    else { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: GlobalMemoryStatusEx failed. Code: " << GetLastError() << std::endl; ResetConsoleColor(); return false; }
}

bool GetDiskInfoAPI(DiskInfo& diskInfo) {
    wchar_t systemDrivePath[MAX_PATH]; UINT pathLen = GetSystemWindowsDirectoryW(systemDrivePath, MAX_PATH);
    if (pathLen == 0 || pathLen > MAX_PATH) { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: Failed to get System Windows Directory. Code: " << GetLastError() << std::endl; ResetConsoleColor(); return false; }
    if (pathLen >= 3 && systemDrivePath[1] == L':' && systemDrivePath[2] == L'\\') { systemDrivePath[3] = L'\0'; diskInfo.DriveLetter = systemDrivePath[0]; }
    else { SetConsoleColor(COLOR_ERROR); std::wcerr << L"  Error: Could not parse system drive path: " << systemDrivePath << std::endl; ResetConsoleColor(); return false; }
    ULARGE_INTEGER freeBytes, totalBytes, totalFreeBytes;
    if (GetDiskFreeSpaceExW( systemDrivePath, &freeBytes, &totalBytes, &totalFreeBytes )) { diskInfo.FreeBytesAvailableToUser = freeBytes.QuadPart; diskInfo.TotalBytes = totalBytes.QuadPart; return true; }
    else { SetConsoleColor(COLOR_ERROR); std::wcerr << L"  Error: GetDiskFreeSpaceExW failed for drive " << systemDrivePath << L". Code: " << GetLastError() << std::endl; ResetConsoleColor(); return false; }
}

bool GetOsInfoWMI(IWbemServices* pSvc, OsInfo& osInfo) {
    bool foundData = false;
    if (pSvc) { // Try WMI first
        IEnumWbemClassObject* pEnumerator = NULL;
        HRESULT hres = pSvc->ExecQuery( bstr_t(L"WQL"), bstr_t(L"SELECT Caption, Version, BuildNumber, OSArchitecture, ServicePackMajorVersion FROM Win32_OperatingSystem"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
        if (SUCCEEDED(hres) && pEnumerator) {
            IWbemClassObject* pclsObj = NULL; ULONG uReturn = 0;
            if (SUCCEEDED(pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn != 0) {
                foundData = true; VARIANT vtProp; VariantInit(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"Caption", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BSTR) { osInfo.Caption = vtProp.bstrVal; } VariantClear(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"Version", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BSTR) { osInfo.Version = vtProp.bstrVal; } VariantClear(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"BuildNumber", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BSTR) { osInfo.BuildNumber = vtProp.bstrVal; } VariantClear(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"OSArchitecture", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BSTR) { osInfo.OSArchitecture = vtProp.bstrVal; } VariantClear(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"ServicePackMajorVersion", 0, &vtProp, 0, 0))) { if (vtProp.vt==VT_I4||vtProp.vt==VT_UI4) { std::wstringstream ss; ss << vtProp.uintVal; osInfo.ServicePackMajorVersion = ss.str(); } else if (vtProp.vt==VT_NULL||vtProp.vt==VT_EMPTY) { osInfo.ServicePackMajorVersion = L"0"; } } VariantClear(&vtProp);
                pclsObj->Release();
            } else { if (!pEnumerator) { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: Failed WMI enumerator for OS." << std::endl; ResetConsoleColor(); }}
            if (pEnumerator) { pEnumerator->Release(); }
        } else { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: WMI query Win32_OperatingSystem failed. Code=0x" << std::hex << hres << std::endl; ResetConsoleColor(); }
    }
    // Fallback using API if WMI failed or wasn't available
    if (!foundData) {
         SetConsoleColor(COLOR_INFO); std::cerr << "  WMI OS query failed/skipped, attempting API fallback..." << std::endl; ResetConsoleColor();
         OSVERSIONINFOEXW osvi; ZeroMemory(&osvi, sizeof(OSVERSIONINFOEXW)); osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEXW);
         // Note: GetVersionExW is deprecated but needed for XP compatibility.
         if (GetVersionExW((OSVERSIONINFOW*)&osvi)) { // Removed pragma warning suppress
             std::wstringstream ssVer, ssBuild, ssSP; ssVer << osvi.dwMajorVersion << L"." << osvi.dwMinorVersion; ssBuild << osvi.dwBuildNumber; ssSP << osvi.wServicePackMajor;
             osInfo.Version = ssVer.str(); osInfo.BuildNumber = ssBuild.str(); osInfo.ServicePackMajorVersion = ssSP.str();
             osInfo.Caption = L"N/A (API Fallback)";
             SYSTEM_INFO sysInfo; GetSystemInfo(&sysInfo);
             osInfo.OSArchitecture = (sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 || sysInfo.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64) ? L"64-bit" : L"32-bit";
             foundData = true; SetConsoleColor(COLOR_SUCCESS); std::cout << "  API Fallback successful for basic OS version." << std::endl; ResetConsoleColor();
         } else { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: GetVersionExW failed. Code: " << GetLastError() << std::endl; ResetConsoleColor(); }
    }
    return foundData;
}

bool GetFirmwareTypeAPI(FirmwareInfo& firmwareInfo) {
    typedef BOOL (WINAPI *pGetFirmwareType)(PFIRMWARE_TYPE);
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    if (!hKernel32) { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: Could not get handle to kernel32.dll. Code: " << GetLastError() << std::endl; ResetConsoleColor(); firmwareInfo.FirmwareType = L"Unknown (Kernel32 Error)"; return false; }
    pGetFirmwareType pGFT = (pGetFirmwareType)GetProcAddress(hKernel32, "GetFirmwareType");
    if (pGFT == NULL) { SetConsoleColor(COLOR_INFO); std::cout << "  Info: GetFirmwareType API not available (likely Windows XP). Assuming BIOS." << std::endl; ResetConsoleColor(); firmwareInfo.FirmwareType = L"BIOS (Assumed)"; return true; }
    FIRMWARE_TYPE ft = FirmwareTypeUnknown;
    if (pGFT(&ft)) {
        switch (ft) { case FirmwareTypeBios: firmwareInfo.FirmwareType = L"BIOS"; break; case FirmwareTypeUefi: firmwareInfo.FirmwareType = L"UEFI"; break; default: firmwareInfo.FirmwareType = L"Unknown (API Result)"; break; } return true;
    } else { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: GetFirmwareType API call failed. Code: " << GetLastError() << std::endl; ResetConsoleColor(); firmwareInfo.FirmwareType = L"Unknown (API Error)"; return false; }
}

bool GetGraphicsInfoWMI(IWbemServices* pSvc, GraphicsInfo& graphicsInfo) {
    if (!pSvc) return false; IEnumWbemClassObject* pEnumerator = NULL;
    HRESULT hres = pSvc->ExecQuery( bstr_t(L"WQL"), bstr_t(L"SELECT Name, AdapterRAM, DriverVersion, VideoProcessor FROM Win32_VideoController"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
    if (FAILED(hres)) { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: WMI query Win32_VideoController failed. Code=0x" << std::hex << hres << std::endl; ResetConsoleColor(); return false; }
    IWbemClassObject* pclsObj = NULL; ULONG uReturn = 0; bool foundData = false;
    while (pEnumerator && SUCCEEDED(pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn != 0) {
        foundData = true; VARIANT vtProp; VariantInit(&vtProp);
        if (SUCCEEDED(pclsObj->Get(L"Name", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BSTR) { graphicsInfo.Name = vtProp.bstrVal; } VariantClear(&vtProp);
        if (SUCCEEDED(pclsObj->Get(L"AdapterRAM", 0, &vtProp, 0, 0)) && (vtProp.vt == VT_I4 || vtProp.vt == VT_UI4)) { graphicsInfo.AdapterRAM = vtProp.uintVal; } VariantClear(&vtProp);
        if (SUCCEEDED(pclsObj->Get(L"DriverVersion", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BSTR) { graphicsInfo.DriverVersion = vtProp.bstrVal; } VariantClear(&vtProp);
        if (SUCCEEDED(pclsObj->Get(L"VideoProcessor", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BSTR) { graphicsInfo.VideoProcessor = vtProp.bstrVal; } VariantClear(&vtProp);
        pclsObj->Release(); if (!graphicsInfo.Name.empty()) { break; } // Take first named adapter
    }
    if (pEnumerator) { pEnumerator->Release(); } // Fixed braces
    graphicsInfo.DirectXFeatureLevel = L"N/A"; graphicsInfo.WDDMVersion = L"N/A"; // Placeholders
    return foundData;
}

bool GetScreenResolutionAPI(ScreenInfo& screenInfo) {
    screenInfo.Width = GetSystemMetrics(SM_CXSCREEN); screenInfo.Height = GetSystemMetrics(SM_CYSCREEN);
    if (screenInfo.Width == 0 || screenInfo.Height == 0) { SetConsoleColor(COLOR_WARNING); std::cerr << "  Warning: GetSystemMetrics returned 0 for screen dimensions. Error (if any): " << GetLastError() << std::endl; ResetConsoleColor(); return false; } return true;
}

bool GetDirectXVersionRegistry(DirectXInfo& dxInfo) {
    HKEY hKey; LONG lResult = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\DirectX", 0, KEY_READ, &hKey);
    if (lResult != ERROR_SUCCESS) { if (lResult == ERROR_FILE_NOT_FOUND) { SetConsoleColor(COLOR_INFO); std::cerr << "  Info: DirectX registry key not found." << std::endl; ResetConsoleColor(); dxInfo.InstalledVersion = L"Not Found"; } else { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: Failed to open DirectX registry key. Code: " << lResult << std::endl; ResetConsoleColor(); } return false; }
    wchar_t versionStr[255]; DWORD dwBufferSize = sizeof(versionStr); lResult = RegQueryValueExW(hKey, L"Version", NULL, NULL, (LPBYTE)versionStr, &dwBufferSize);
    if (lResult == ERROR_SUCCESS) { dxInfo.InstalledVersion = versionStr; } else { SetConsoleColor(COLOR_ERROR); std::cerr << "  Error: Failed to query DirectX Version value. Code: " << lResult << std::endl; ResetConsoleColor(); dxInfo.InstalledVersion = L"Query Failed"; RegCloseKey(hKey); return false; }
    RegCloseKey(hKey); return true;
}

bool GetSecurityInfo(IWbemServices* pSvc, SecurityInfo& secInfo, const FirmwareInfo& firmwareInfo) {
    bool tpmCheckAttempted = false; bool sbCheckAttempted = false;
    // --- TPM Check ---
    if (pSvc) {
        tpmCheckAttempted = true; IEnumWbemClassObject* pEnumerator = NULL;
        HRESULT hres = pSvc->ExecQuery( bstr_t(L"WQL"), bstr_t(L"SELECT IsEnabled, IsActivated, IsOwned, SpecVersion FROM Win32_Tpm"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumerator);
        if (SUCCEEDED(hres) && pEnumerator) {
            IWbemClassObject* pclsObj = NULL; ULONG uReturn = 0;
            if (SUCCEEDED(pEnumerator->Next(WBEM_INFINITE, 1, &pclsObj, &uReturn)) && uReturn != 0) {
                secInfo.TpmFound = true; VARIANT vtProp; VariantInit(&vtProp); bool enabled=false, activated=false, owned=false;
                if (SUCCEEDED(pclsObj->Get(L"IsEnabled", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BOOL) { enabled = vtProp.boolVal; } VariantClear(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"IsActivated", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BOOL) { activated = vtProp.boolVal; } VariantClear(&vtProp);
                if (SUCCEEDED(pclsObj->Get(L"IsOwned", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BOOL) { owned = vtProp.boolVal; } VariantClear(&vtProp);
                secInfo.TpmEnabled = (enabled && activated && owned);
                if (SUCCEEDED(pclsObj->Get(L"SpecVersion", 0, &vtProp, 0, 0)) && vtProp.vt == VT_BSTR) {
                    secInfo.TpmVersionString = vtProp.bstrVal; wchar_t* context = NULL; wchar_t* token = wcstok_s(vtProp.bstrVal, L", .", &context);
                    if (token) secInfo.TpmSpecVersionMajor = _wtoi(token); token = wcstok_s(NULL, L", .", &context); if (token) secInfo.TpmSpecVersionMinor = _wtoi(token);
                } VariantClear(&vtProp); pclsObj->Release();
            } else { secInfo.TpmVersionString = L"Not Found (WMI Instance)"; secInfo.TpmFound = false; }
        } else { secInfo.TpmVersionString = L"Not Found (WMI Query Failed/Win7?)"; secInfo.TpmFound = false; }
        if (pEnumerator) { pEnumerator->Release(); }
    } else { secInfo.TpmVersionString = L"N/A (WMI Not Initialized)"; }
    // --- Secure Boot Check ---
    secInfo.SecureBootStatus = L"N/A";
    if (firmwareInfo.FirmwareType == L"UEFI") {
        sbCheckAttempted = true;
        typedef DWORD (WINAPI *pGFEVW)(LPCWSTR, LPCWSTR, PVOID, DWORD, PDWORD);
        HMODULE hK32 = GetModuleHandleW(L"kernel32.dll"); pGFEVW pGFEV = NULL; if(hK32) pGFEV = (pGFEVW)GetProcAddress(hK32, "GetFirmwareEnvironmentVariableW");
        if (pGFEV) {
             BYTE sbVal = 0; DWORD attr = 0; DWORD dataSize = sizeof(sbVal); DWORD ret = pGFEV(L"SecureBoot", L"{8be4df61-93ca-11d2-aa0d-00e098032b8c}", &sbVal, dataSize, &attr);
             if (ret > 0) { secInfo.SecureBootCapable = true; secInfo.SecureBootEnabled = (sbVal == 1); secInfo.SecureBootStatus = secInfo.SecureBootEnabled ? L"Enabled (API)" : L"Disabled (API)"; }
             else { DWORD err = GetLastError(); if (err == ERROR_ENVVAR_NOT_FOUND) { secInfo.SecureBootStatus = L"Not Found (API)"; secInfo.SecureBootCapable = false; } else if (err == ERROR_PRIVILEGE_NOT_HELD) { secInfo.SecureBootStatus = L"Requires Admin (API)"; } else { std::wstringstream ssErr; ssErr << L"Error (API Code: " << err << L")"; secInfo.SecureBootStatus = ssErr.str(); } }
        } else { secInfo.SecureBootStatus = L"N/A (API Unavailable)"; }
        // WMI as fallback if API didn't give conclusive Enabled/Disabled status
        if (pSvc && (secInfo.SecureBootStatus.find(L"Enabled") == std::wstring::npos && secInfo.SecureBootStatus.find(L"Disabled") == std::wstring::npos)) {
             IEnumWbemClassObject* pEnumSB = NULL; IWbemServices* pSvcWMI = NULL; IWbemLocator* pLocSB = NULL;
             if (SUCCEEDED(CoCreateInstance(CLSID_WbemLocator,0,CLSCTX_INPROC_SERVER,IID_IWbemLocator,(LPVOID*)&pLocSB))) {
                 if (SUCCEEDED(pLocSB->ConnectServer(_bstr_t(L"ROOT\\WMI"),NULL,NULL,0,0L,0,0,&pSvcWMI))) {
                     if (SUCCEEDED(CoSetProxyBlanket(pSvcWMI,RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,NULL,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,NULL,EOAC_NONE))) {
                          HRESULT hresSB = pSvcWMI->ExecQuery(bstr_t(L"WQL"), bstr_t(L"SELECT SecureBoot FROM MSAcpi_SecureBoot"), WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnumSB);
                          if (SUCCEEDED(hresSB) && pEnumSB) {
                               IWbemClassObject* pObjSB = NULL; ULONG uRetSB = 0;
                               if (SUCCEEDED(pEnumSB->Next(WBEM_INFINITE, 1, &pObjSB, &uRetSB)) && uRetSB != 0) {
                                    VARIANT vtPropSB; VariantInit(&vtPropSB); secInfo.SecureBootCapable = true;
                                    if (SUCCEEDED(pObjSB->Get(L"SecureBoot", 0, &vtPropSB, 0, 0)) && vtPropSB.vt == VT_BOOL) {
                                        secInfo.SecureBootEnabled = vtPropSB.boolVal; secInfo.SecureBootStatus = secInfo.SecureBootEnabled ? L"Enabled (WMI)" : L"Disabled (WMI)";
                                    } else {
                                        if (secInfo.SecureBootStatus == L"N/A") { secInfo.SecureBootStatus = L"Query Failed (WMI)"; }
                                    }
                                    VariantClear(&vtPropSB); pObjSB->Release();
                               } else {
                                    if (secInfo.SecureBootStatus == L"N/A") { secInfo.SecureBootStatus = L"Not Found (WMI Instance)"; }
                               }
                               if (pEnumSB) { pEnumSB->Release(); }
                          } else {
                               if (secInfo.SecureBootStatus == L"N/A") { secInfo.SecureBootStatus = L"Not Found (WMI Query Failed)"; }
                          }
                     } else {
                          if (secInfo.SecureBootStatus == L"N/A") { secInfo.SecureBootStatus = L"N/A (Proxy Blanket ROOT\\WMI failed)"; }
                     }
                     if (pSvcWMI) { pSvcWMI->Release(); } // Fixed braces
                 } else {
                      if (secInfo.SecureBootStatus == L"N/A") { secInfo.SecureBootStatus = L"N/A (ConnectServer ROOT\\WMI failed)"; }
                 }
                 if (pLocSB) { pLocSB->Release(); } // Fixed braces
             } else {
                  if (secInfo.SecureBootStatus == L"N/A") { secInfo.SecureBootStatus = L"N/A (CreateInstance for ROOT\\WMI failed)"; }
             }
        } // End WMI fallback
    } else if (firmwareInfo.FirmwareType == L"BIOS") { secInfo.SecureBootStatus = L"Not Applicable (BIOS)"; secInfo.SecureBootCapable = false; secInfo.SecureBootEnabled = false; sbCheckAttempted = true; }
    else { secInfo.SecureBootStatus = L"N/A (Firmware Unknown)"; }
    return tpmCheckAttempted || sbCheckAttempted;
}


// --- Simulation Input Helper Functions ---
int GetIntInput(const std::string& prompt) {
    int value = 0;
    while (true) {
        SetConsoleColor(COLOR_LABEL); std::cout << prompt; ResetConsoleColor();
        std::cin >> value;
        if (std::cin.good()) { std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); return value; }
        else { SetConsoleColor(COLOR_ERROR); std::cerr << "Invalid input. Please enter a whole number." << std::endl; ResetConsoleColor(); std::cin.clear(); std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); }
    }
}
bool GetBoolInput(const std::string& prompt) {
    char choice = ' ';
    while (choice != 'Y' && choice != 'N') {
        SetConsoleColor(COLOR_LABEL); std::cout << prompt << " (Y/N): "; ResetConsoleColor();
        std::cin >> choice; choice = toupper(choice); std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (choice != 'Y' && choice != 'N') { SetConsoleColor(COLOR_ERROR); std::cerr << "Invalid input. Please enter Y or N." << std::endl; ResetConsoleColor(); }
    } return (choice == 'Y');
}

// --- Simulation Input Function ---
bool GetSimulatedSystemInfo(CpuInfo& cpu, RamInfo& ram, DiskInfo& disk, OsInfo& os, FirmwareInfo& firm, GraphicsInfo& graph, ScreenInfo& screen, SecurityInfo& sec, DirectXInfo& dx) {
    SetConsoleColor(COLOR_HEADING); std::cout << "\nEnter Simulated System Specifications:" << std::endl; ResetConsoleColor();
    // --- CPU ---
    SetConsoleColor(COLOR_LABEL); std::cout << "--- CPU ---" << std::endl; ResetConsoleColor();
    cpu.MaxClockSpeed = GetIntInput("  CPU Speed (MHz, e.g., 3400): ");
    cpu.NumberOfCores = GetIntInput("  Number of Physical Cores (e.g., 4): ");
    cpu.NumberOfLogicalProcessors = GetIntInput("  Number of Logical Processors (Threads, e.g., 8): ");
    cpu.Is64BitCapable = GetBoolInput("  Is CPU 64-bit Capable?");
    cpu.Architecture = cpu.Is64BitCapable ? L"x64 (64-bit)" : L"x86 (32-bit)";
    SetConsoleColor(COLOR_NOTE); std::cout << "  CPU Generation Level (for Win11 check): 0=Pre-Win11, 8=Win11 Min (Intel 8th+/Ryzen 2k+)" << std::endl; ResetConsoleColor();
    cpu.MinCpuGenerationLevel = GetIntInput("  Enter Level (0 or 8): "); cpu.Name = L"Simulated CPU";
    // --- RAM ---
    SetConsoleColor(COLOR_LABEL); std::cout << "--- RAM ---" << std::endl; ResetConsoleColor();
    int ramGB = GetIntInput("  Total RAM (GB, e.g., 8): "); ram.TotalPhysicalBytes = (ULONGLONG)ramGB * 1024 * 1024 * 1024;
    // --- Disk ---
    SetConsoleColor(COLOR_LABEL); std::cout << "--- Disk ---" << std::endl; ResetConsoleColor();
    int diskGB = GetIntInput("  Free Space on System Drive (GB, e.g., 100): "); disk.FreeBytesAvailableToUser = (ULONGLONG)diskGB * 1024 * 1024 * 1024; disk.DriveLetter = L'C';
    // --- Firmware ---
    SetConsoleColor(COLOR_LABEL); std::cout << "--- Firmware ---" << std::endl; ResetConsoleColor();
    char fwChoice = ' '; while (fwChoice != 'B' && fwChoice != 'U') { SetConsoleColor(COLOR_LABEL); std::cout << "  Firmware Type ([B]IOS / [U]EFI): "; ResetConsoleColor(); std::cin >> fwChoice; fwChoice = toupper(fwChoice); std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); if (fwChoice == 'B') firm.FirmwareType = L"BIOS"; else if (fwChoice == 'U') firm.FirmwareType = L"UEFI"; else { SetConsoleColor(COLOR_ERROR); std::cerr << "Invalid input." << std::endl; ResetConsoleColor(); } }
    // --- Graphics ---
    SetConsoleColor(COLOR_LABEL); std::cout << "--- Graphics ---" << std::endl; ResetConsoleColor(); graph.Name = L"Simulated Graphics";
    int dxLevel = 0; while (dxLevel < 9 || dxLevel > 12) { dxLevel = GetIntInput("  DirectX Feature Level Supported (9, 10, 11, 12): "); if (dxLevel < 9 || dxLevel > 12) { SetConsoleColor(COLOR_ERROR); std::cerr << "Enter 9, 10, 11, or 12." << std::endl; ResetConsoleColor(); } } graph.DirectXFeatureLevel = std::to_wstring(dxLevel);
    int wddmLevel = 0; while (wddmLevel < 1 || wddmLevel > 2) { wddmLevel = GetIntInput("  WDDM Version Supported (1 or 2): "); if (wddmLevel < 1 || wddmLevel > 2) { SetConsoleColor(COLOR_ERROR); std::cerr << "Enter 1 or 2." << std::endl; ResetConsoleColor(); } } graph.WDDMVersion = std::to_wstring(wddmLevel) + L".0";
    // --- Display ---
    SetConsoleColor(COLOR_LABEL); std::cout << "--- Display ---" << std::endl; ResetConsoleColor(); screen.Width = GetIntInput("  Screen Width (pixels, e.g., 1920): "); screen.Height = GetIntInput("  Screen Height (pixels, e.g., 1080): ");
    // --- Security ---
    SetConsoleColor(COLOR_LABEL); std::cout << "--- Security ---" << std::endl; ResetConsoleColor(); sec.TpmFound = GetBoolInput("  TPM Found?");
    if (sec.TpmFound) { sec.TpmEnabled = GetBoolInput("    TPM Enabled & Ready?"); double tpmVer = 0.0; while (tpmVer != 1.2 && tpmVer != 2.0) { SetConsoleColor(COLOR_LABEL); std::cout << "    TPM Specification Version (1.2 or 2.0): "; ResetConsoleColor(); std::cin >> tpmVer; if (std::cin.fail() || (tpmVer != 1.2 && tpmVer != 2.0)) { SetConsoleColor(COLOR_ERROR); std::cerr << "Invalid input. Enter 1.2 or 2.0." << std::endl; ResetConsoleColor(); std::cin.clear(); std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); tpmVer = 0.0; } else { std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n'); } } sec.TpmSpecVersionMajor = (UINT32)tpmVer; sec.TpmSpecVersionMinor = (tpmVer == 1.2) ? 2 : 0; sec.TpmVersionString = (tpmVer == 1.2) ? L"1.2" : L"2.0"; }
    else { sec.TpmEnabled = false; sec.TpmSpecVersionMajor = 0; sec.TpmSpecVersionMinor = 0; sec.TpmVersionString = L"Not Found"; }
    if (firm.FirmwareType == L"UEFI") { sec.SecureBootCapable = true; sec.SecureBootEnabled = GetBoolInput("  Secure Boot Enabled?"); sec.SecureBootStatus = sec.SecureBootEnabled ? L"Enabled (Simulated)" : L"Disabled (Simulated)"; }
    else { sec.SecureBootCapable = false; sec.SecureBootEnabled = false; sec.SecureBootStatus = L"Not Applicable (BIOS)"; }
    // --- Other ---
    dx.InstalledVersion = L"N/A (Simulated)"; os.Caption = L"Simulated System"; os.Version = L"N/A"; os.BuildNumber = L"N/A"; os.OSArchitecture = cpu.Is64BitCapable ? L"64-bit" : L"32-bit"; os.ServicePackMajorVersion = L"N/A";
    SetConsoleColor(COLOR_SUCCESS); std::cout << "\nSimulation data entered successfully." << std::endl; ResetConsoleColor(); return true;
}


// --- Comparison Function Implementation (with Warning Tracking & Sign Compare Fix) ---
void CompareRequirements(const WindowsRequirements& target, const CpuInfo& cpu, const RamInfo& ram, const DiskInfo& disk, const OsInfo& os, const FirmwareInfo& firm, const GraphicsInfo& graph, const ScreenInfo& screen, const SecurityInfo& sec, const DirectXInfo& dx) {

    bool overallPass = true; // Assume pass initially
    bool anyWarnings = false; // Track if any warnings occurred

    // Helper lambda function to print a check result line
    auto PrintCheck = [&](const std::wstring& label, const std::wstring& required, const std::wstring& detected, bool pass, bool warning = false, const std::wstring& note = L"") {
        SetConsoleColor(COLOR_LABEL);
        // Pad label for alignment
        std::wstring paddedLabel = label;
        paddedLabel.resize(19, L' '); // Adjust size as needed for alignment
        std::wcout << L"  " << paddedLabel << L": ";

        SetConsoleColor(COLOR_NOTE);
        std::wcout << L"(Required: " << required << L") ";
        SetConsoleColor(COLOR_LABEL);
        std::wcout << L"Detected: ";

        WORD valueColor = COLOR_VALUE; WORD statusColor = COLOR_SUCCESS; std::wstring statusText = L" [PASS]";
        if (!pass) {
            if (warning) { valueColor = COLOR_VALUE_WARN; statusColor = COLOR_WARNING; statusText = L" [WARN]"; anyWarnings = true; } // Set warning flag
            else { valueColor = COLOR_VALUE_FAIL; statusColor = COLOR_FAILURE; statusText = L" [FAIL]"; overallPass = false; } // Hard failure
        }
        SetConsoleColor(valueColor); std::wcout << detected; SetConsoleColor(statusColor); std::wcout << statusText;
        if (!note.empty()) { SetConsoleColor(COLOR_NOTE); std::wcout << L" (" << note << L")"; }
        std::wcout << std::endl; ResetConsoleColor();
    };

    // --- CPU Checks ---
    SetConsoleColor(COLOR_HEADING); std::cout << "\nCPU Requirements:" << std::endl; ResetConsoleColor();
    bool cpuSpeedMet = cpu.MaxClockSpeed >= target.MinCpuSpeedMHz;
    PrintCheck(L"Speed", std::to_wstring(target.MinCpuSpeedMHz) + L" MHz", (cpu.MaxClockSpeed == 0 ? L"N/A" : std::to_wstring(cpu.MaxClockSpeed) + L" MHz"), cpuSpeedMet, (cpu.MaxClockSpeed == 0));
    bool cpuCoresMet = cpu.NumberOfCores >= target.MinCpuCores;
    PrintCheck(L"Cores", std::to_wstring(target.MinCpuCores), (cpu.NumberOfCores == 0 ? L"N/A" : std::to_wstring(cpu.NumberOfCores)), cpuCoresMet, (cpu.NumberOfCores == 0));
    bool cpuArchMet = !target.Require64Bit || cpu.Is64BitCapable;
    PrintCheck(L"Architecture", (target.Require64Bit ? L"64-bit" : L"Any"), cpu.Architecture, cpuArchMet);
    if (target.MinCpuGenerationLevel > 0) {
        bool cpuGenMet = cpu.MinCpuGenerationLevel >= target.MinCpuGenerationLevel; std::wstring detectedGen = (cpu.MinCpuGenerationLevel > 0) ? (L"Simulated Level " + std::to_wstring(cpu.MinCpuGenerationLevel)) : L"Unknown (Live Detection)";
        PrintCheck(L"CPU Generation", L"Supported List (Level " + std::to_wstring(target.MinCpuGenerationLevel) + L"+)", detectedGen, cpuGenMet, (cpu.MinCpuGenerationLevel == 0), L"Simplified Check");
    }

    // --- RAM Check ---
    SetConsoleColor(COLOR_HEADING); std::cout << "\nRAM Requirements:" << std::endl; ResetConsoleColor();
    bool ramMet = ram.TotalPhysicalBytes >= target.MinRamBytes;
    PrintCheck(L"Installed RAM", std::to_wstring(target.MinRamBytes / (1024*1024)) + L" MB", std::to_wstring(ram.TotalPhysicalBytes / (1024*1024)) + L" MB", ramMet);

    // --- Disk Check ---
    SetConsoleColor(COLOR_HEADING); std::cout << "\nDisk Requirements:" << std::endl; ResetConsoleColor();
    bool diskMet = disk.FreeBytesAvailableToUser >= target.MinDiskFreeBytes;
    PrintCheck(L"System Drive Free", std::to_wstring(target.MinDiskFreeBytes / (1024*1024*1024)) + L" GB", std::to_wstring(disk.FreeBytesAvailableToUser / (1024*1024*1024)) + L" GB", diskMet);

    // --- Firmware Check ---
    SetConsoleColor(COLOR_HEADING); std::cout << "\nFirmware Requirements:" << std::endl; ResetConsoleColor();
    bool firmwareMet = !target.RequireUEFI || firm.FirmwareType == L"UEFI";
    bool firmwareWarn = firm.FirmwareType == L"Unknown" || firm.FirmwareType == L"BIOS (Assumed)";
    PrintCheck(L"System Firmware", (target.RequireUEFI ? L"UEFI" : L"Any"), firm.FirmwareType, firmwareMet, (firmwareWarn && target.RequireUEFI));

    // --- Graphics Checks ---
    SetConsoleColor(COLOR_HEADING); std::cout << "\nGraphics Requirements:" << std::endl; ResetConsoleColor();
    bool dxMet = false; bool wddmMet = false; UINT detectedDXLevel = 0; UINT detectedWDDMLevel = 0;
    bool dxCheckPossible = graph.DirectXFeatureLevel != L"N/A"; bool wddmCheckPossible = graph.WDDMVersion != L"N/A";
    try { if (dxCheckPossible) { detectedDXLevel = std::stoi(graph.DirectXFeatureLevel); dxMet = detectedDXLevel >= target.MinDirectXFeatureLevelMajor; } if (wddmCheckPossible) { size_t dotPos = graph.WDDMVersion.find(L'.'); if (dotPos != std::wstring::npos) { detectedWDDMLevel = std::stoi(graph.WDDMVersion.substr(0, dotPos)); } else { detectedWDDMLevel = std::stoi(graph.WDDMVersion); } wddmMet = detectedWDDMLevel >= target.MinWDDMVersionMajor; } } catch(...) {}
    PrintCheck(L"DirectX Feature Lvl", L"v" + std::to_wstring(target.MinDirectXFeatureLevelMajor) + L".0+", (dxCheckPossible ? std::to_wstring(detectedDXLevel) : graph.DirectXFeatureLevel), dxMet, !dxCheckPossible, L"Manual check recommended");
    PrintCheck(L"WDDM Driver Model", L"v" + std::to_wstring(target.MinWDDMVersionMajor) + L".0+", (wddmCheckPossible ? (std::to_wstring(detectedWDDMLevel) + L".x") : graph.WDDMVersion), wddmMet, !wddmCheckPossible, L"Manual check recommended");

    // --- Display Check ---
    SetConsoleColor(COLOR_HEADING); std::cout << "\nDisplay Requirements:" << std::endl; ResetConsoleColor();
    // Fixed sign comparison warning by casting screen dimensions to UINT
    bool displayMet = (UINT)screen.Width >= target.MinScreenWidth && (UINT)screen.Height >= target.MinScreenHeight;
    PrintCheck(L"Screen Resolution", std::to_wstring(target.MinScreenWidth) + L"x" + std::to_wstring(target.MinScreenHeight), std::to_wstring(screen.Width) + L"x" + std::to_wstring(screen.Height), displayMet, (screen.Width == 0 || screen.Height == 0));

    // --- Security Checks ---
    SetConsoleColor(COLOR_HEADING); std::cout << "\nSecurity Requirements:" << std::endl; ResetConsoleColor();
    bool tpmMet = !target.RequireTpm || (sec.TpmFound && sec.TpmEnabled && sec.TpmSpecVersionMajor >= target.MinTpmVersionMajor);
    bool tpmWarn = target.RequireTpm && (!sec.TpmFound || !sec.TpmEnabled);
    std::wstring tpmDetectedStr = L"N/A"; if (sec.TpmVersionString != L"N/A") { tpmDetectedStr = sec.TpmFound ? (L"Yes, v" + std::to_wstring(sec.TpmSpecVersionMajor) + L"." + std::to_wstring(sec.TpmSpecVersionMinor) + (sec.TpmEnabled ? L", Enabled" : L", Disabled/Not Ready")) : sec.TpmVersionString; }
    PrintCheck(L"TPM", (target.RequireTpm ? (L"v" + std::to_wstring(target.MinTpmVersionMajor) + L".0+, Enabled") : L"Not Required"), tpmDetectedStr, tpmMet, tpmWarn);
    bool sbMet = !target.RequireSecureBoot || (firm.FirmwareType == L"UEFI" && sec.SecureBootEnabled);
    bool sbWarn = target.RequireSecureBoot && (firm.FirmwareType != L"UEFI" || !sec.SecureBootCapable || sec.SecureBootStatus.find(L"Error") != std::wstring::npos || sec.SecureBootStatus.find(L"Admin") != std::wstring::npos || sec.SecureBootStatus.find(L"Unknown") != std::wstring::npos || sec.SecureBootStatus.find(L"N/A") != std::wstring::npos);
    std::wstring sbDetectedStr = (firm.FirmwareType == L"UEFI") ? sec.SecureBootStatus : L"N/A (BIOS)";
    PrintCheck(L"Secure Boot", (target.RequireSecureBoot ? L"Enabled" : L"Not Required"), sbDetectedStr, sbMet, sbWarn);

    // --- Connectivity Check ---
    if (target.RequireInternetForSetup) {
        SetConsoleColor(COLOR_HEADING); std::cout << "\nOther Requirements:" << std::endl; ResetConsoleColor();
        SetConsoleColor(COLOR_LABEL); std::cout << "  Internet Connection: "; SetConsoleColor(COLOR_WARNING); std::cout << "(Required for Setup/Activation of this edition)" << std::endl; ResetConsoleColor();
    }

    // --- Final Result (Updated) ---
    SetConsoleColor(COLOR_HEADING); std::cout << "\n--- Overall Result ---" << std::endl; ResetConsoleColor();
    if (overallPass) {
        SetConsoleColor(COLOR_SUCCESS); std::wcout << L"This system appears to meet the minimum requirements for installing " << target.Name << L"." << std::endl;
        if (anyWarnings) { SetConsoleColor(COLOR_WARNING); std::cout << "However, please review the [WARN] items above as they indicate potential issues or uncertainties." << std::endl; }
    } else {
        SetConsoleColor(COLOR_FAILURE); std::wcout << L"This system does NOT meet the minimum requirements for installing " << target.Name << L"." << std::endl;
        SetConsoleColor(COLOR_NOTE); std::cout << "Please review the [FAIL] items above.";
        if (anyWarnings) { std::cout << " Also review any [WARN] items for additional context."; }
        std::cout << std::endl;
    }
    if (target.Name == L"Windows 11") { SetConsoleColor(COLOR_NOTE); std::cout << "Note: Windows 11 also has a specific CPU compatibility list. This tool uses a simplified generation check." << std::endl << "      For definitive CPU compatibility, check Microsoft's official list or PC Health Check app." << std::endl; }
    if (target.MinDirectXFeatureLevelMajor >= 12 || target.MinWDDMVersionMajor >= 2) { SetConsoleColor(COLOR_NOTE); std::cout << "Note: Graphics checks (DirectX Feature Level, WDDM Version) are basic. Manual verification using 'dxdiag' command is recommended." << std::endl; }
    if (target.RequireSecureBoot && sec.SecureBootStatus == L"Requires Admin (API)") { SetConsoleColor(COLOR_NOTE); std::cout << "Note: Run this tool as Administrator for a more accurate Secure Boot status check." << std::endl; }
    ResetConsoleColor();
}
// --- End Function Implementations ---
