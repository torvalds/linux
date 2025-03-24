.. SPDX-License-Identifier: GPL-2.0-or-later

==============================================
Dell AWCC WMI interface driver (alienware-wmi)
==============================================

Introduction
============

The WMI device WMAX has been implemented for many Alienware and Dell's G-Series
models. Throughout these models, two implementations have been identified. The
first one, used by older systems, deals with HDMI, brightness, RGB, amplifier
and deep sleep control. The second one used by newer systems deals primarily
with thermal, overclocking, and GPIO control.

It is suspected that the latter is used by Alienware Command Center (AWCC) to
manage manufacturer predefined thermal profiles. The alienware-wmi driver
exposes Thermal_Information and Thermal_Control methods through the Platform
Profile API to mimic AWCC's behavior.

This newer interface, named AWCCMethodFunction has been reverse engineered, as
Dell has not provided any official documentation. We will try to describe to the
best of our ability its discovered inner workings.

.. note::
   The following method description may be incomplete and some operations have
   different implementations between devices.

WMI interface description
-------------------------

The WMI interface description can be decoded from the embedded binary MOF (bmof)
data using the `bmfdec <https://github.com/pali/bmfdec>`_ utility:

::

 [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"), Description("WMI Function"), guid("{A70591CE-A997-11DA-B012-B622A1EF5492}")]
 class AWCCWmiMethodFunction {
   [key, read] string InstanceName;
   [read] boolean Active;

   [WmiMethodId(13), Implemented, read, write, Description("Return Overclocking Report.")] void Return_OverclockingReport([out] uint32 argr);
   [WmiMethodId(14), Implemented, read, write, Description("Set OCUIBIOS Control.")] void Set_OCUIBIOSControl([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(15), Implemented, read, write, Description("Clear OC FailSafe Flag.")] void Clear_OCFailSafeFlag([out] uint32 argr);
   [WmiMethodId(19), Implemented, read, write, Description("Get Fan Sensors.")] void GetFanSensors([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(20), Implemented, read, write, Description("Thermal Information.")] void Thermal_Information([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(21), Implemented, read, write, Description("Thermal Control.")] void Thermal_Control([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(23), Implemented, read, write, Description("MemoryOCControl.")] void MemoryOCControl([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(26), Implemented, read, write, Description("System Information.")] void SystemInformation([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(28), Implemented, read, write, Description("Power Information.")] void PowerInformation([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(32), Implemented, read, write, Description("FW Update GPIO toggle.")] void FWUpdateGPIOtoggle([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(33), Implemented, read, write, Description("Read Total of GPIOs.")] void ReadTotalofGPIOs([out] uint32 argr);
   [WmiMethodId(34), Implemented, read, write, Description("Read GPIO pin Status.")] void ReadGPIOpPinStatus([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(35), Implemented, read, write, Description("Read Chassis Color.")] void ReadChassisColor([out] uint32 argr);
   [WmiMethodId(36), Implemented, read, write, Description("Read Platform Properties.")] void ReadPlatformProperties([out] uint32 argr);
   [WmiMethodId(37), Implemented, read, write, Description("Game Shift Status.")] void GameShiftStatus([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(128), Implemented, read, write, Description("Caldera SW installation.")] void CalderaSWInstallation([out] uint32 argr);
   [WmiMethodId(129), Implemented, read, write, Description("Caldera SW is released.")] void CalderaSWReleased([out] uint32 argr);
   [WmiMethodId(130), Implemented, read, write, Description("Caldera Connection Status.")] void CalderaConnectionStatus([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(131), Implemented, read, write, Description("Surprise Unplugged Flag Status.")] void SurpriseUnpluggedFlagStatus([out] uint32 argr);
   [WmiMethodId(132), Implemented, read, write, Description("Clear Surprise Unplugged Flag.")] void ClearSurpriseUnpluggedFlag([out] uint32 argr);
   [WmiMethodId(133), Implemented, read, write, Description("Cancel Undock Request.")] void CancelUndockRequest([out] uint32 argr);
   [WmiMethodId(135), Implemented, read, write, Description("Devices in Caldera.")] void DevicesInCaldera([in] uint32 arg2, [out] uint32 argr);
   [WmiMethodId(136), Implemented, read, write, Description("Notify BIOS for SW ready to disconnect Caldera.")] void NotifyBIOSForSWReadyToDisconnectCaldera([out] uint32 argr);
   [WmiMethodId(160), Implemented, read, write, Description("Tobii SW installation.")] void TobiiSWinstallation([out] uint32 argr);
   [WmiMethodId(161), Implemented, read, write, Description("Tobii SW Released.")] void TobiiSWReleased([out] uint32 argr);
   [WmiMethodId(162), Implemented, read, write, Description("Tobii Camera Power Reset.")] void TobiiCameraPowerReset([out] uint32 argr);
   [WmiMethodId(163), Implemented, read, write, Description("Tobii Camera Power On.")] void TobiiCameraPowerOn([out] uint32 argr);
   [WmiMethodId(164), Implemented, read, write, Description("Tobii Camera Power Off.")] void TobiiCameraPowerOff([out] uint32 argr);
 };

Some of these methods get quite intricate so we will describe them using
pseudo-code that vaguely resembles the original ASL code.

Methods not described in the following document have unknown behavior.

Argument Structure
------------------

All input arguments have type **uint32** and their structure is very similar
between methods. Usually, the first byte corresponds to a specific *operation*
the method performs, and the subsequent bytes correspond to *arguments* passed
to this *operation*. For example, if an operation has code 0x01 and requires an
ID 0xA0, the argument you would pass to the method is 0xA001.


Thermal Methods
===============

WMI method Thermal_Information([in] uint32 arg2, [out] uint32 argr)
-------------------------------------------------------------------

::

 if BYTE_0(arg2) == 0x01:
         argr = 1

 if BYTE_0(arg2) == 0x02:
         argr = SYSTEM_DESCRIPTION

 if BYTE_0(arg2) == 0x03:
         if BYTE_1(arg2) == 0x00:
                 argr = FAN_ID_0

         if BYTE_1(arg2) == 0x01:
                 argr = FAN_ID_1

         if BYTE_1(arg2) == 0x02:
                 argr = FAN_ID_2

         if BYTE_1(arg2) == 0x03:
                 argr = FAN_ID_3

         if BYTE_1(arg2) == 0x04:
                 argr = SENSOR_ID_CPU | 0x0100

         if BYTE_1(arg2) == 0x05:
                 argr = SENSOR_ID_GPU | 0x0100

         if BYTE_1(arg2) == 0x06:
                 argr = THERMAL_MODE_QUIET_ID

         if BYTE_1(arg2) == 0x07:
                 argr = THERMAL_MODE_BALANCED_ID

         if BYTE_1(arg2) == 0x08:
                 argr = THERMAL_MODE_BALANCED_PERFORMANCE_ID

         if BYTE_1(arg2) == 0x09:
                 argr = THERMAL_MODE_PERFORMANCE_ID

         if BYTE_1(arg2) == 0x0A:
                 argr = THERMAL_MODE_LOW_POWER_ID

         if BYTE_1(arg2) == 0x0B:
                 argr = THERMAL_MODE_GMODE_ID

         else:
                 argr = 0xFFFFFFFF

 if BYTE_0(arg2) == 0x04:
         if is_valid_sensor(BYTE_1(arg2)):
                 argr = SENSOR_TEMP_C
         else:
                 argr = 0xFFFFFFFF

 if BYTE_0(arg2) == 0x05:
         if is_valid_fan(BYTE_1(arg2)):
                 argr = FAN_RPM()

 if BYTE_0(arg2) == 0x06:
         skip

 if BYTE_0(arg2) == 0x07:
         argr = 0

 If BYTE_0(arg2) == 0x08:
         if is_valid_fan(BYTE_1(arg2)):
                 argr = 0
         else:
                 argr = 0xFFFFFFFF

 if BYTE_0(arg2) == 0x09:
         if is_valid_fan(BYTE_1(arg2)):
                 argr = FAN_UNKNOWN_STAT_0()

         else:
                 argr = 0xFFFFFFFF

 if BYTE_0(arg2) == 0x0A:
         argr = THERMAL_MODE_BALANCED_ID

 if BYTE_0(arg2) == 0x0B:
         argr = CURRENT_THERMAL_MODE()

 if BYTE_0(arg2) == 0x0C:
         if is_valid_fan(BYTE_1(arg2)):
                 argr = FAN_UNKNOWN_STAT_1()
         else:
                 argr = 0xFFFFFFFF

Operation 0x02 returns a *system description* buffer with the following
structure:

::

 out[0] -> Number of fans
 out[1] -> Number of sensors
 out[2] -> 0x00
 out[3] -> Number of thermal modes

Operation 0x03 list all available fan IDs, sensor IDs and thermal profile
codes in order, but different models may have different number of fans and
thermal profiles. These are the known ranges:

* Fan IDs: from 2 up to 4
* Sensor IDs: 2
* Thermal profile codes: from 1 up to 7

In total BYTE_1(ARG2) may range from 0x5 up to 0xD depending on the model.

WMI method Thermal_Control([in] uint32 arg2, [out] uint32 argr)
---------------------------------------------------------------

::

 if BYTE_0(arg2) == 0x01:
         if is_valid_thermal_profile(BYTE_1(arg2)):
                 SET_THERMAL_PROFILE(BYTE_1(arg2))
                 argr = 0

 if BYTE_0(arg2) == 0x02:
         if is_valid_fan(BYTE_1(arg2)):
                 SET_FAN_SPEED_MULTIPLIER(BYTE_2(arg2))
                 argr = 0
         else:
                 argr = 0xFFFFFFFF

.. note::
   While you can manually change the fan speed multiplier with this method,
   Dell's BIOS tends to overwrite this changes anyway.

These are the known thermal profile codes:

::

 CUSTOM                         0x00

 BALANCED_USTT                  0xA0
 BALANCED_PERFORMANCE_USTT      0xA1
 COOL_USTT                      0xA2
 QUIET_USTT                     0xA3
 PERFORMANCE_USTT               0xA4
 LOW_POWER_USTT                 0xA5

 QUIET                          0x96
 BALANCED                       0x97
 BALANCED_PERFORMANCE           0x98
 PERFORMANCE                    0x99

 GMODE                          0xAB

Usually if a model doesn't support the first four profiles they will support
the User Selectable Thermal Tables (USTT) profiles and vice-versa.

GMODE replaces PERFORMANCE in G-Series laptops.

WMI method GameShiftStatus([in] uint32 arg2, [out] uint32 argr)
---------------------------------------------------------------

::

 if BYTE_0(arg2) == 0x1:
         TOGGLE_GAME_SHIFT()
         argr = GET_GAME_SHIFT_STATUS()

 if BYTE_0(arg2) == 0x2:
         argr = GET_GAME_SHIFT_STATUS()

Game Shift Status does not change the fan speed profile but it could be some
sort of CPU/GPU power profile. Benchmarks have not been done.

This method is only present on Dell's G-Series laptops and it's implementation
implies GMODE thermal profile is available, even if operation 0x03 of
Thermal_Information does not list it.

G-key on Dell's G-Series laptops also changes Game Shift status, so both are
directly related.

WMI method GetFanSensors([in] uint32 arg2, [out] uint32 argr)
-------------------------------------------------------------

::

 if BYTE_0(arg2) == 0x1:
        if is_valid_fan(BYTE_1(arg2)):
                argr = 1
        else:
                argr = 0

 if BYTE_0(arg2) == 0x2:
        if is_valid_fan(BYTE_1(arg2)):
                if BYTE_2(arg2) == 0:
                        argr == SENSOR_ID
                else
                        argr == 0xFFFFFFFF
        else:
                argr = 0

Overclocking Methods
====================

.. warning::
   These methods have not been tested and are only partially reverse
   engineered.

WMI method Return_OverclockingReport([out] uint32 argr)
-------------------------------------------------------

::

 CSMI (0xE3, 0x99)
 argr = 0

CSMI is an unknown operation.

WMI method Set_OCUIBIOSControl([in] uint32 arg2, [out] uint32 argr)
-------------------------------------------------------------------

::

 CSMI (0xE3, 0x99)
 argr = 0

CSMI is an unknown operation.

WMI method Clear_OCFailSafeFlag([out] uint32 argr)
--------------------------------------------------

::

 CSMI (0xE3, 0x99)
 argr = 0

CSMI is an unknown operation.


WMI method MemoryOCControl([in] uint32 arg2, [out] uint32 argr)
---------------------------------------------------------------

AWCC supports memory overclocking, but this method is very intricate and has
not been deciphered yet.

GPIO methods
============

These methods are probably related to some kind of firmware update system,
through a GPIO device.

.. warning::
   These methods have not been tested and are only partially reverse
   engineered.

WMI method FWUpdateGPIOtoggle([in] uint32 arg2, [out] uint32 argr)
------------------------------------------------------------------

::

 if BYTE_0(arg2) == 0:
         if BYTE_1(arg2) == 1:
                 SET_PIN_A_HIGH()
         else:
                 SET_PIN_A_LOW()

 if BYTE_0(arg2) == 1:
         if BYTE_1(arg2) == 1:
                 SET_PIN_B_HIGH()

         else:
                 SET_PIN_B_LOW()

 else:
         argr = 1

WMI method ReadTotalofGPIOs([out] uint32 argr)
----------------------------------------------

::

 argr = 0x02

WMI method ReadGPIOpPinStatus([in] uint32 arg2, [out] uint32 argr)
------------------------------------------------------------------

::

 if BYTE_0(arg2) == 0:
         argr = PIN_A_STATUS

 if BYTE_0(arg2) == 1:
         argr = PIN_B_STATUS

Other information Methods
=========================

WMI method ReadChassisColor([out] uint32 argr)
----------------------------------------------

::

 argr = CHASSIS_COLOR_ID

Acknowledgements
================

Kudos to `AlexIII <https://github.com/AlexIII/tcc-g15>`_ for documenting
and testing available thermal profile codes.
