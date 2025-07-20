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
with thermal control and overclocking.

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

WMI method GetFanSensors([in] uint32 arg2, [out] uint32 argr)
-------------------------------------------------------------

+--------------------+------------------------------------+--------------------+
| Operation (Byte 0) | Description                        | Arguments          |
+====================+====================================+====================+
| 0x01               | Get the number of temperature      | - Byte 1: Fan ID   |
|                    | sensors related with a fan ID      |                    |
+--------------------+------------------------------------+--------------------+
| 0x02               | Get the temperature sensor IDs     | - Byte 1: Fan ID   |
|                    | related to a fan sensor ID         | - Byte 2: Index    |
+--------------------+------------------------------------+--------------------+

WMI method Thermal_Information([in] uint32 arg2, [out] uint32 argr)
-------------------------------------------------------------------

+--------------------+------------------------------------+--------------------+
| Operation (Byte 0) | Description                        | Arguments          |
+====================+====================================+====================+
| 0x01               | Unknown.                           | - None             |
+--------------------+------------------------------------+--------------------+
| 0x02               | Get system description number with | - None             |
|                    | the following structure:           |                    |
|                    |                                    |                    |
|                    | - Byte 0: Number of fans           |                    |
|                    | - Byte 1: Number of temperature    |                    |
|                    |   sensors                          |                    |
|                    | - Byte 2: Unknown                  |                    |
|                    | - Byte 3: Number of thermal        |                    |
|                    |   profiles                         |                    |
+--------------------+------------------------------------+--------------------+
| 0x03               | List an ID or resource at a given  | - Byte 1: Index    |
|                    | index. Fan IDs, temperature IDs,   |                    |
|                    | unknown IDs and thermal profile    |                    |
|                    | IDs are listed in that exact       |                    |
|                    | order.                             |                    |
|                    |                                    |                    |
|                    | Operation 0x02 is used to know     |                    |
|                    | which indexes map to which         |                    |
|                    | resources.                         |                    |
|                    |                                    |                    |
|                    | **Returns:** ID at a given index   |                    |
+--------------------+------------------------------------+--------------------+
| 0x04               | Get the current temperature for a  | - Byte 1: Sensor   |
|                    | given temperature sensor.          |   ID               |
+--------------------+------------------------------------+--------------------+
| 0x05               | Get the current RPM for a given    | - Byte 1: Fan ID   |
|                    | fan.                               |                    |
+--------------------+------------------------------------+--------------------+
| 0x06               | Get fan speed percentage. (not     | - Byte 1: Fan ID   |
|                    | implemented in every model)        |                    |
+--------------------+------------------------------------+--------------------+
| 0x07               | Unknown.                           | - Unknown          |
+--------------------+------------------------------------+--------------------+
| 0x08               | Get minimum RPM for a given FAN    | - Byte 1: Fan ID   |
|                    | ID.                                |                    |
+--------------------+------------------------------------+--------------------+
| 0x09               | Get maximum RPM for a given FAN    | - Byte 1: Fan ID   |
|                    | ID.                                |                    |
+--------------------+------------------------------------+--------------------+
| 0x0A               | Get balanced thermal profile ID.   | - None             |
+--------------------+------------------------------------+--------------------+
| 0x0B               | Get current thermal profile ID.    | - None             |
+--------------------+------------------------------------+--------------------+
| 0x0C               | Get current `boost` value for a    | - Byte 1: Fan ID   |
|                    | given fan ID.                      |                    |
+--------------------+------------------------------------+--------------------+

WMI method Thermal_Control([in] uint32 arg2, [out] uint32 argr)
---------------------------------------------------------------

+--------------------+------------------------------------+--------------------+
| Operation (Byte 0) | Description                        | Arguments          |
+====================+====================================+====================+
| 0x01               | Activate a given thermal profile.  | - Byte 1: Thermal  |
|                    |                                    |   profile ID       |
+--------------------+------------------------------------+--------------------+
| 0x02               | Set a `boost` value for a given    | - Byte 1: Fan ID   |
|                    | fan ID.                            | - Byte 2: Boost    |
+--------------------+------------------------------------+--------------------+

These are the known thermal profile codes:

+------------------------------+----------+------+
| Thermal Profile              | Type     | ID   |
+==============================+==========+======+
| Custom                       | Special  | 0x00 |
+------------------------------+----------+------+
| G-Mode                       | Special  | 0xAB |
+------------------------------+----------+------+
| Quiet                        | Legacy   | 0x96 |
+------------------------------+----------+------+
| Balanced                     | Legacy   | 0x97 |
+------------------------------+----------+------+
| Balanced Performance         | Legacy   | 0x98 |
+------------------------------+----------+------+
| Performance                  | Legacy   | 0x99 |
+------------------------------+----------+------+
| Balanced                     | USTT     | 0xA0 |
+------------------------------+----------+------+
| Balanced Performance         | USTT     | 0xA1 |
+------------------------------+----------+------+
| Cool                         | USTT     | 0xA2 |
+------------------------------+----------+------+
| Quiet                        | USTT     | 0xA3 |
+------------------------------+----------+------+
| Performance                  | USTT     | 0xA4 |
+------------------------------+----------+------+
| Low Power                    | USTT     | 0xA5 |
+------------------------------+----------+------+

If a model supports the User Selectable Thermal Tables (USTT) profiles, it will
not support the Legacy profiles and vice-versa.

Every model supports the CUSTOM (0x00) thermal profile. GMODE replaces
PERFORMANCE in G-Series laptops.

WMI method GameShiftStatus([in] uint32 arg2, [out] uint32 argr)
---------------------------------------------------------------

+--------------------+------------------------------------+--------------------+
| Operation (Byte 0) | Description                        | Arguments          |
+====================+====================================+====================+
| 0x01               | Toggle *Game Shift*.               | - None             |
+--------------------+------------------------------------+--------------------+
| 0x02               | Get *Game Shift* status.           | - None             |
+--------------------+------------------------------------+--------------------+

Game Shift Status does not change the fan speed profile but it could be some
sort of CPU/GPU power profile. Benchmarks have not been done.

This method is only present on Dell's G-Series laptops and it's implementation
implies GMODE thermal profile is available, even if operation 0x03 of
Thermal_Information does not list it.

G-key on Dell's G-Series laptops also changes Game Shift status, so both are
directly related.

Overclocking Methods
====================

WMI method MemoryOCControl([in] uint32 arg2, [out] uint32 argr)
---------------------------------------------------------------

AWCC supports memory overclocking, but this method is very intricate and has
not been deciphered yet.

GPIO control Methods
====================

Alienware and Dell G Series devices with the AWCC interface usually have an
embedded STM32 RGB lighting controller with USB/HID capabilities. It's vendor ID
is ``187c`` while it's product ID may vary from model to model.

The control of two GPIO pins of this MCU is exposed as WMI methods for debugging
purposes.

+--------------+--------------------------------------------------------------+
| Pin          | Description                                                  |
+==============+===============================+==============================+
| 0            | Device Firmware Update (DFU)  | **HIGH**: Enables DFU mode   |
|              | mode pin.                     | on next MCU boot.            |
|              |                               +------------------------------+
|              |                               | **LOW**: Disables DFU mode   |
|              |                               | on next MCU boot.            |
+--------------+-------------------------------+------------------------------+
| 1            | Negative Reset (NRST) pin.    | **HIGH**: MCU is ON.         |
|              |                               |                              |
|              |                               +------------------------------+
|              |                               | **LOW**: MCU is OFF.         |
|              |                               |                              |
+--------------+-------------------------------+------------------------------+

See :ref:`acknowledgements` for more information on this MCU.

.. note::
   Some GPIO control methods break the usual argument structure and take a
   **Pin number** instead of an operation on the first byte.

WMI method FWUpdateGPIOtoggle([in] uint32 arg2, [out] uint32 argr)
------------------------------------------------------------------

+--------------------+------------------------------------+--------------------+
| Operation (Byte 0) | Description                        | Arguments          |
+====================+====================================+====================+
| Pin number         | Set the pin status                 | - Byte 1: Pin      |
|                    |                                    |   status           |
+--------------------+------------------------------------+--------------------+

WMI method ReadTotalofGPIOs([out] uint32 argr)
----------------------------------------------

+--------------------+------------------------------------+--------------------+
| Operation (Byte 0) | Description                        | Arguments          |
+====================+====================================+====================+
| N/A                | Get the total number of GPIOs      | - None             |
+--------------------+------------------------------------+--------------------+

.. note::
   Due to how WMI methods are implemented on the firmware level, this method
   requires a dummy uint32 input argument when invoked.

WMI method ReadGPIOpPinStatus([in] uint32 arg2, [out] uint32 argr)
------------------------------------------------------------------

+--------------------+------------------------------------+--------------------+
| Operation (Byte 0) | Description                        | Arguments          |
+====================+====================================+====================+
| Pin number         | Get the pin status                 | - None             |
+--------------------+------------------------------------+--------------------+

.. note::
   There known firmware bug in some laptops where reading the status of a pin
   also flips it.

Other information Methods
=========================

WMI method ReadChassisColor([out] uint32 argr)
----------------------------------------------

Returns the chassis color internal ID.

.. _acknowledgements:

Acknowledgements
================

Kudos to

* `AlexIII <https://github.com/AlexIII/tcc-g15>`_
* `T-Troll <https://github.com/T-Troll/alienfx-tools/>`_
* `Gabriel Marcano <https://gabriel.marcanobrady.family/blog/2024/12/16/dell-g5-5505-se-acpi-or-figuring-out-how-to-reset-the-rgb-controller/>`_

for documenting and testing some of this device's functionality, making it
possible to generalize this driver.
