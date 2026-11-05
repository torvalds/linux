.. SPDX-License-Identifier: GPL-2.0-or-later

====================================================
Acer WMI Battery interface driver (acer-wmi-battery)
====================================================

Introduction
============

Some Acer laptops support setting battery charge control limits and allow a
user to calibrate their battery.

WMI interface description
=========================

The WMI interface description can be decoded from the embedded binary MOF (bmof)
data using the `bmfdec <https://github.com/pali/bmfdec>`_ utility:

::

  [WMI, Dynamic, Provider("WmiProv"), Locale("MS\\0x409"), Description("Class used to control smart battery, Version 2.88"), guid("{79772EC5-04B1-4bfd-843C-61E7F77B6CC9}")]
  class BatteryControl {
    [key, read] string InstanceName;
    [read] boolean Active;

    [WmiMethodId(19), Implemented, read, write, Description("Get battery Information Interface.")] void GetBattInfoInterface([in] uint32 uBatteryInfoIndex, [in] uint32 uBatteryNo, [out] uint32 uReturn);
    [WmiMethodId(20), Implemented, read, write, Description("Get Battery Health Control Status.")] void GetBatteryHealthControlStatus([in] uint8 uBatteryNo, [in] uint8 uFunctionQuery, [in] uint8 uReserved[2], [out] uint8 uFunctionList, [out] uint8 uReturn[2], [out] uint8 uFunctionStatus[5]);
    [WmiMethodId(21), Implemented, read, write, Description("Set Battery Health Control.")] void SetBatteryHealthControl([in] uint8 uBatteryNo, [in] uint8 uFunctionMask, [in] uint8 uFunctionStatus, [in] uint8 uReservedIn[5], [out] uint16 uReturn, [out] uint16 uReservedOut);
    [WmiMethodId(22), Implemented, read, write, Description("Get Battery Function Data.")] void GetBatteryFunctionData([in] uint8 uFunctionMask, [in] uint8 uReservedIn[3], [out] uint8 uReturnCode[2], [out] uint8 uBACStartTime[2], [out] uint8 uBACStopTime[2], [out] uint8 uBACStatus, [out] uint8 uReservedOut[9]);
    [WmiMethodId(23), Implemented, read, write, Description("Set Battery Function Data.")] void SetBatteryFunctionData([in] uint8 uFunctionMask, [in] uint8 uBACSwitch, [in] uint8 uReservedIn[6], [out] uint16 uReturnCode, [out] uint8 uReservedOut[2]);
  };

For all methods the ``uBatteryNo`` is always ``0x1``, there are no known Acer
laptop models with two batteries.

WMI method GetBatteryInfoInterface()
------------------------------------

Returns additional battery information, the data seems to be based on the
"Smart Battery Data Specification". The only known value for
``uBatteryInfoIndex`` is ``0x8``, the battery temperature.

WMI method GetBatteryHealthControlStatus()
------------------------------------------

This interface does not exist on all Acer laptops, it returns whether battery
charge limiting or calibration mode is enabled.

When calling this method, ``uFunctionList`` is a bitmask with known values:

+--------------------+------------------------------------+
| Value              | Description                        |
+====================+====================================+
| 0x01               | Health mode supported              |
+--------------------+------------------------------------+
| 0x02               | Calibration mode supported         |
+--------------------+------------------------------------+

``uFunctionStatus`` is a 5 byte array known elements are ``0`` for health mode
and ``1`` for calibration mode. A non-zero value means the mode is enabled.

WMI method SetBatteryHealthControl()
------------------------------------

This interface does not exist on all Acer laptops, it enables battery
charge limiting or battery calibration.

The argument ``uFunctionMask`` is known to accept either ``0x01`` for setting
health mode or ``0x02`` for calibration mode; other values are unknown.
``uFunctionStatus`` is an uint8 value, a non-zero value enables the given mode.
