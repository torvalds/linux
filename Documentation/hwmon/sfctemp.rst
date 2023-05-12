.. SPDX-License-Identifier: GPL-2.0

Kernel driver sfctemp
=====================

Supported chips:
 - StarFive JH7100
 - StarFive JH7110

Authors:
 - Emil Renner Berthing <kernel@esmil.dk>

Description
-----------

This driver adds support for reading the built-in temperature sensor on the
JH7100 and JH7110 RISC-V SoCs by StarFive Technology Co. Ltd.

``sysfs`` interface
-------------------

The temperature sensor can be enabled, disabled and queried via the standard
hwmon interface in sysfs under ``/sys/class/hwmon/hwmonX`` for some value of
``X``:

================ ==== =============================================
Name             Perm Description
================ ==== =============================================
temp1_enable     RW   Enable or disable temperature sensor.
                      Automatically enabled by the driver,
                      but may be disabled to save power.
temp1_input      RO   Temperature reading in milli-degrees Celsius.
================ ==== =============================================
