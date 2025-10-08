.. SPDX-License-Identifier: GPL-2.0-or-later

==========================================================
Lenovo WMI Interface Gamezone Driver (lenovo-wmi-gamezone)
==========================================================

Introduction
============
The Lenovo WMI gamezone interface is broken up into multiple GUIDs,
The primary "Gamezone" GUID provides advanced features such as fan
profiles and overclocking. It is paired with multiple event GUIDs
and data block GUIDs that provide context for the various methods.

Gamezone Data
-------------

WMI GUID ``887B54E3-DDDC-4B2C-8B88-68A26A8835D0``

The Gamezone Data WMI interface provides platform-profile and fan curve
settings for devices that fall under the "Gaming Series" of Lenovo devices.
It uses a notifier chain to inform other Lenovo WMI interface drivers of the
current platform profile when it changes.

The following platform profiles are supported:
 - low-power
 - balanced
 - balanced-performance
 - performance
 - custom

Balanced-Performance
~~~~~~~~~~~~~~~~~~~~
Some newer Lenovo "Gaming Series" laptops have an "Extreme Mode" profile
enabled in their BIOS. For these devices, the performance platform profile
corresponds to the BIOS Extreme Mode, while the balanced-performance
platform profile corresponds to the BIOS Performance mode. For legacy
devices, the performance platform profile will correspond with the BIOS
Performance mode.

For some newer devices the "Extreme Mode" profile is incomplete in the BIOS
and setting it will cause undefined behavior. A BIOS bug quirk table is
provided to ensure these devices cannot set "Extreme Mode" from the driver.

Custom Profile
~~~~~~~~~~~~~~
The custom profile represents a hardware mode on Lenovo devices that enables
user modifications to Package Power Tracking (PPT) and fan curve settings.
When an attribute exposed by the Other Mode WMI interface is to be modified,
the Gamezone driver must first be switched to the "custom" profile manually,
or the setting will have no effect. If another profile is set from the list
of supported profiles, the BIOS will override any user PPT settings when
switching to that profile.

Gamezone Thermal Mode Event
---------------------------

WMI GUID ``D320289E-8FEA-41E0-86F9-911D83151B5F``

The Gamezone Thermal Mode Event interface notifies the system when the platform
profile has changed, either through the hardware event (Fn+Q for laptops or
Legion + Y for Go Series), or through the Gamezone WMI interface. This event is
implemented in the Lenovo WMI Events driver (lenovo-wmi-events).


WMI interface description
=========================

The WMI interface description can be decoded from the embedded binary MOF (bmof)
data using the `bmfdec <https://github.com/pali/bmfdec>`_ utility:

::

  [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"), Description("LENOVO_GAMEZONE_DATA class"), guid("{887B54E3-DDDC-4B2C-8B88-68A26A8835D0}")]
  class LENOVO_GAMEZONE_DATA {
    [key, read] string InstanceName;
    [read] boolean Active;

    [WmiMethodId(4), Implemented, Description("Is SupportGpu OverClock")] void IsSupportGpuOC([out, Description("Is SupportGpu OverClock")] uint32 Data);
    [WmiMethodId(11), Implemented, Description("Get AslCode Version")] void GetVersion ([out, Description("AslCode version")] UINT32 Data);
    [WmiMethodId(12), Implemented, Description("Fan cooling capability")] void IsSupportFanCooling([out, Description("Fan cooling capability")] UINT32 Data);
    [WmiMethodId(13), Implemented, Description("Set Fan cooling on/off")] void SetFanCooling ([in, Description("Set Fan cooling on/off")] UINT32 Data);
    [WmiMethodId(14), Implemented, Description("cpu oc capability")] void IsSupportCpuOC ([out, Description("cpu oc capability")] UINT32 Data);
    [WmiMethodId(15), Implemented, Description("bios has overclock capability")] void IsBIOSSupportOC ([out, Description("bios has overclock capability")] UINT32 Data);
    [WmiMethodId(16), Implemented, Description("enable or disable overclock in bios")] void SetBIOSOC ([in, Description("enable or disable overclock in bios")] UINT32 Data);
    [WmiMethodId(18), Implemented, Description("Get CPU temperature")] void GetCPUTemp ([out, Description("Get CPU temperature")] UINT32 Data);
    [WmiMethodId(19), Implemented, Description("Get GPU temperature")] void GetGPUTemp ([out, Description("Get GPU temperature")] UINT32 Data);
    [WmiMethodId(20), Implemented, Description("Get Fan cooling on/off status")] void GetFanCoolingStatus ([out, Description("Get Fan cooling on/off status")] UINT32 Data);
    [WmiMethodId(21), Implemented, Description("EC support disable windows key capability")] void IsSupportDisableWinKey ([out, Description("EC support disable windows key capability")] UINT32 Data);
    [WmiMethodId(22), Implemented, Description("Set windows key disable/enable")] void SetWinKeyStatus ([in, Description("Set windows key disable/enable")] UINT32 Data);
    [WmiMethodId(23), Implemented, Description("Get windows key disable/enable status")] void GetWinKeyStatus ([out, Description("Get windows key disable/enable status")] UINT32 Data);
    [WmiMethodId(24), Implemented, Description("EC support disable touchpad capability")] void IsSupportDisableTP ([out, Description("EC support disable touchpad capability")] UINT32 Data);
    [WmiMethodId(25), Implemented, Description("Set touchpad disable/enable")] void SetTPStatus ([in, Description("Set touchpad disable/enable")] UINT32 Data);
    [WmiMethodId(26), Implemented, Description("Get touchpad disable/enable status")] void GetTPStatus ([out, Description("Get touchpad disable/enable status")] UINT32 Data);
    [WmiMethodId(30), Implemented, Description("Get Keyboard feature list")] void GetKeyboardfeaturelist ([out, Description("Get Keyboard feature list")] UINT32 Data);
    [WmiMethodId(31), Implemented, Description("Get Memory OC Information")] void GetMemoryOCInfo ([out, Description("Get Memory OC Information")] UINT32 Data);
    [WmiMethodId(32), Implemented, Description("Water Cooling feature capability")] void IsSupportWaterCooling ([out, Description("Water Cooling feature capability")] UINT32 Data);
    [WmiMethodId(33), Implemented, Description("Set Water Cooling status")] void SetWaterCoolingStatus ([in, Description("Set Water Cooling status")] UINT32 Data);
    [WmiMethodId(34), Implemented, Description("Get Water Cooling status")] void GetWaterCoolingStatus ([out, Description("Get Water Cooling status")] UINT32 Data);
    [WmiMethodId(35), Implemented, Description("Lighting feature capability")] void IsSupportLightingFeature ([out, Description("Lighting feature capability")] UINT32 Data);
    [WmiMethodId(36), Implemented, Description("Set keyboard light off or on to max")] void SetKeyboardLight ([in, Description("keyboard light off or on switch")] UINT32 Data);
    [WmiMethodId(37), Implemented, Description("Get keyboard light on/off status")] void GetKeyboardLight ([out, Description("Get keyboard light on/off status")] UINT32 Data);
    [WmiMethodId(38), Implemented, Description("Get Macrokey scan code")] void GetMacrokeyScancode ([in, Description("Macrokey index")] UINT32 idx, [out, Description("Scan code")] UINT32 scancode);
    [WmiMethodId(39), Implemented, Description("Get Macrokey count")] void GetMacrokeyCount ([out, Description("Macrokey count")] UINT32 Data);
    [WmiMethodId(40), Implemented, Description("Support G-Sync feature")] void IsSupportGSync ([out, Description("Support G-Sync feature")] UINT32 Data);
    [WmiMethodId(41), Implemented, Description("Get G-Sync Status")] void GetGSyncStatus ([out, Description("Get G-Sync Status")] UINT32 Data);
    [WmiMethodId(42), Implemented, Description("Set G-Sync Status")] void SetGSyncStatus ([in, Description("Set G-Sync Status")] UINT32 Data);
    [WmiMethodId(43), Implemented, Description("Support Smart Fan feature")] void IsSupportSmartFan ([out, Description("Support Smart Fan feature")] UINT32 Data);
    [WmiMethodId(44), Implemented, Description("Set Smart Fan Mode")] void SetSmartFanMode ([in, Description("Set Smart Fan Mode")] UINT32 Data);
    [WmiMethodId(45), Implemented, Description("Get Smart Fan Mode")] void GetSmartFanMode ([out, Description("Get Smart Fan Mode")] UINT32 Data);
    [WmiMethodId(46), Implemented, Description("Get Smart Fan Setting Mode")] void GetSmartFanSetting ([out, Description("Get Smart Setting Mode")] UINT32 Data);
    [WmiMethodId(47), Implemented, Description("Get Power Charge Mode")] void GetPowerChargeMode ([out, Description("Get Power Charge Mode")] UINT32 Data);
    [WmiMethodId(48), Implemented, Description("Get Gaming Product Info")] void GetProductInfo ([out, Description("Get Gaming Product Info")] UINT32 Data);
    [WmiMethodId(49), Implemented, Description("Over Drive feature capability")] void IsSupportOD ([out, Description("Over Drive feature capability")] UINT32 Data);
    [WmiMethodId(50), Implemented, Description("Get Over Drive status")] void GetODStatus ([out, Description("Get Over Drive status")] UINT32 Data);
    [WmiMethodId(51), Implemented, Description("Set Over Drive status")] void SetODStatus ([in, Description("Set Over Drive status")] UINT32 Data);
    [WmiMethodId(52), Implemented, Description("Set Light Control Owner")] void SetLightControlOwner ([in, Description("Set Light Control Owner")] UINT32 Data);
    [WmiMethodId(53), Implemented, Description("Set DDS Control Owner")] void SetDDSControlOwner ([in, Description("Set DDS Control Owner")] UINT32 Data);
    [WmiMethodId(54), Implemented, Description("Get the flag of restore OC value")] void IsRestoreOCValue ([in, Description("Clean this flag")] UINT32 idx, [out, Description("Restore oc value flag")] UINT32 Data);
    [WmiMethodId(55), Implemented, Description("Get Real Thremal Mode")] void GetThermalMode ([out, Description("Real Thremal Mode")] UINT32 Data);
    [WmiMethodId(56), Implemented, Description("Get the OC switch status in BIOS")] void GetBIOSOCMode ([out, Description("OC Mode")] UINT32 Data);
    [WmiMethodId(59), Implemented, Description("Get hardware info support version")] void GetHardwareInfoSupportVersion ([out, Description("version")] UINT32 Data);
    [WmiMethodId(60), Implemented, Description("Get Cpu core 0 max frequency")] void GetCpuFrequency ([out, Description("frequency")] UINT32 Data);
    [WmiMethodId(62), Implemented, Description("Check the Adapter type fit for OC")] void IsACFitForOC ([out, Description("AC check result")] UINT32 Data);
    [WmiMethodId(63), Implemented, Description("Is support IGPU mode")] void IsSupportIGPUMode ([out, Description("IGPU modes")] UINT32 Data);
    [WmiMethodId(64), Implemented, Description("Get IGPU Mode Status")] void GetIGPUModeStatus([out, Description("IGPU Mode Status")] UINT32 Data);
    [WmiMethodId(65), Implemented, Description("Set IGPU Mode")] void SetIGPUModeStatus([in, Description("IGPU Mode")] UINT32 mode, [out, Description("return code")] UINT32 Data);
    [WmiMethodId(66), Implemented, Description("Notify DGPU Status")] void NotifyDGPUStatus([in, Description("DGPU status")] UINT32 status, [out, Description("return code")] UINT32 Data);
    [WmiMethodId(67), Implemented, Description("Is changed Y log")] void IsChangedYLog([out, Description("Is changed Y Log")] UINT32 Data);
    [WmiMethodId(68), Implemented, Description("Get DGPU Hardwawre ID")] void GetDGPUHWId([out, Description("Get DGPU Hardware ID")] string Data);
  };

  [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"), Description("Definition of CPU OC parameter list"), guid("{B7F3CA0A-ACDC-42D2-9217-77C6C628FBD2}")]
  class LENOVO_GAMEZONE_CPU_OC_DATA {
    [key, read] string InstanceName;
    [read] boolean Active;

    [WmiDataId(1), read, Description("OC tune id.")] uint32 Tuneid;
    [WmiDataId(2), read, Description("Default value.")] uint32 DefaultValue;
    [WmiDataId(3), read, Description("OC Value.")] uint32 OCValue;
    [WmiDataId(4), read, Description("Min Value.")] uint32 MinValue;
    [WmiDataId(5), read, Description("Max Value.")] uint32 MaxValue;
    [WmiDataId(6), read, Description("Scale Value.")] uint32 ScaleValue;
    [WmiDataId(7), read, Description("OC Order id.")] uint32 OCOrderid;
    [WmiDataId(8), read, Description("NON-OC Order id.")] uint32 NOCOrderid;
    [WmiDataId(9), read, Description("Delay time in ms.")] uint32 Interval;
  };

  [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"), Description("Definition of GPU OC parameter list"), guid("{887B54E2-DDDC-4B2C-8B88-68A26A8835D0}")]
  class LENOVO_GAMEZONE_GPU_OC_DATA {
    [key, read] string InstanceName;
    [read] boolean Active;

    [WmiDataId(1), read, Description("P-State ID.")] uint32 PStateID;
    [WmiDataId(2), read, Description("CLOCK ID.")] uint32 ClockID;
    [WmiDataId(3), read, Description("Default value.")] uint32 defaultvalue;
    [WmiDataId(4), read, Description("OC Offset freqency.")] uint32 OCOffsetFreq;
    [WmiDataId(5), read, Description("OC Min offset value.")] uint32 OCMinOffset;
    [WmiDataId(6), read, Description("OC Max offset value.")] uint32 OCMaxOffset;
    [WmiDataId(7), read, Description("OC Offset Scale.")] uint32 OCOffsetScale;
    [WmiDataId(8), read, Description("OC Order id.")] uint32 OCOrderid;
    [WmiDataId(9), read, Description("NON-OC Order id.")] uint32 NOCOrderid;
  };

  [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"), Description("Fancooling finish event"), guid("{BC72A435-E8C1-4275-B3E2-D8B8074ABA59}")]
  class LENOVO_GAMEZONE_FAN_COOLING_EVENT: WMIEvent {
    [key, read] string InstanceName;
    [read] boolean Active;

    [WmiDataId(1), read, Description("Fancooling clean finish event")] uint32 EventId;
  };

  [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"), Description("Smart Fan mode change event"), guid("{D320289E-8FEA-41E0-86F9-611D83151B5F}")]
  class LENOVO_GAMEZONE_SMART_FAN_MODE_EVENT: WMIEvent {
    [key, read] string InstanceName;
    [read] boolean Active;

    [WmiDataId(1), read, Description("Smart Fan Mode change event")] uint32 mode;
    [WmiDataId(2), read, Description("version of FN+Q")] uint32 version;
  };

  [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"), Description("Smart Fan setting mode change event"), guid("{D320289E-8FEA-41E1-86F9-611D83151B5F}")]
  class LENOVO_GAMEZONE_SMART_FAN_SETTING_EVENT: WMIEvent {
    [key, read] string InstanceName;
    [read] boolean Active;

    [WmiDataId(1), read, Description("Smart Fan Setting mode change event")] uint32 mode;
  };

  [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"), Description("POWER CHARGE MODE Change EVENT"), guid("{D320289E-8FEA-41E0-86F9-711D83151B5F}")]
  class LENOVO_GAMEZONE_POWER_CHARGE_MODE_EVENT: WMIEvent {
    [key, read] string InstanceName;
    [read] boolean Active;

    [WmiDataId(1), read, Description("POWER CHARGE MODE Change EVENT")] uint32 mode;
  };

  [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"), Description("Thermal Mode Real Mode change event"), guid("{D320289E-8FEA-41E0-86F9-911D83151B5F}")]
  class LENOVO_GAMEZONE_THERMAL_MODE_EVENT: WMIEvent {
    [key, read] string InstanceName;
    [read] boolean Active;

    [WmiDataId(1), read, Description("Thermal Mode Real Mode")] uint32 mode;
  };
