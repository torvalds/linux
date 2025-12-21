.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver sy7636a-hwmon
===========================

Supported chips:

 * Silergy SY7636A PMIC


Description
-----------

This driver adds hardware temperature reading support for
the Silergy SY7636A PMIC.

The following sensors are supported

  * Temperature
      - Temperature of external NTC in milli-degree C

sysfs-Interface
---------------

temp0_input
	- Temperature of external NTC (milli-degree C)
