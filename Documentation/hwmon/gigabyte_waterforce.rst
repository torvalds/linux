.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver gigabyte_waterforce
=================================

Supported devices:

* Gigabyte AORUS WATERFORCE X240
* Gigabyte AORUS WATERFORCE X280
* Gigabyte AORUS WATERFORCE X360

Author: Aleksa Savic

Description
-----------

This driver enables hardware monitoring support for the listed Gigabyte Waterforce
all-in-one CPU liquid coolers. Available sensors are pump and fan speed in RPM, as
well as coolant temperature. Also available through debugfs is the firmware version.

Attaching a fan is optional and allows it to be controlled from the device. If
it's not connected, the fan-related sensors will report zeroes.

The addressable RGB LEDs and LCD screen are not supported in this driver and should
be controlled through userspace tools.

Usage notes
-----------

As these are USB HIDs, the driver can be loaded automatically by the kernel and
supports hot swapping.

Sysfs entries
-------------

=========== =============================================
fan1_input  Fan speed (in rpm)
fan2_input  Pump speed (in rpm)
temp1_input Coolant temperature (in millidegrees Celsius)
=========== =============================================

Debugfs entries
---------------

================ =======================
firmware_version Device firmware version
================ =======================
