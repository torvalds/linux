.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver asus_rog_ryujin
=============================

Supported devices:

* ASUS ROG RYUJIN II 360
* ASUS ROG RYUJIN III EXTREME
* ASUS ROG RYUJIN III EVA EDITION
* ASUS ROG RYUJIN III WHITE EDITION

Author: Aleksa Savic

Description
-----------

This driver enables hardware monitoring support for the listed ASUS ROG RYUJIN
all-in-one CPU liquid coolers. Available sensors are pump, internal and external
(controller) fan speed in RPM, their duties in PWM, as well as coolant temperature.

The RYUJIN II includes a separate external fan controller. Attaching fans to
the controller is optional and allows them to be controlled from the device.
If not connected, the controller-related sensors will report zeroes. The
RYUJIN III does not expose these controller channels.

The addressable LCD screen is not supported in this driver and should
be controlled through userspace tools.

Usage notes
-----------

As these are USB HIDs, the driver can be loaded automatically by the kernel and
supports hot swapping.

Sysfs entries
-------------

=========== ==========================================================
fan1_input  Pump speed (in rpm)
fan2_input  Internal fan speed (in rpm)
fan3_input  External (controller) fan 1 speed (in rpm, RYUJIN II only)
fan4_input  External (controller) fan 2 speed (in rpm, RYUJIN II only)
fan5_input  External (controller) fan 3 speed (in rpm, RYUJIN II only)
fan6_input  External (controller) fan 4 speed (in rpm, RYUJIN II only)
temp1_input Coolant temperature (in millidegrees Celsius)
pwm1        Pump duty
pwm2        Internal fan duty
pwm3        External (controller) fan duty (RYUJIN II only)
=========== ==========================================================
