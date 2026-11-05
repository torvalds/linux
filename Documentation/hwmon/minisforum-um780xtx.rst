.. SPDX-License-Identifier: GPL-2.0-only

====================================
Kernel driver minisforum-um780xtx
====================================

Supported systems:

  * Minisforum UM780 XTX

    * DMI product name: ``Venus series``
    * Mainboard: ``F7BSD``, revision ``1.1``
    * BIOS version: ``1.06``

Author: Sebastián Peyrott <speyrott@gmail.com>

Description
-----------

This driver exposes hardware monitoring and fan-control data cached by the
IT5571E embedded controller. It uses the ACPI EC transport and only binds to
the exact system and firmware identity listed above.

The two temperature channels are the values used by the EC's fan-control
loops. Their physical sensor placement is not known. The CPU channel is the
filtered AMD SB-TSI temperature, while the system channel is an external
thermistor input.

The fan tachometers are read through OEM EC commands. Since each byte is
returned by a separate command, the driver uses a high-low-high sequence and
retries if the high byte changes.

Sysfs entries
-------------

==========================  ==============================================
``temp1_input``             CPU-fan control temperature
``temp2_input``             System-fan control temperature
``fan1_input``              CPU fan speed in RPM
``fan2_input``              System fan speed in RPM
``pwm1_enable``             CPU profile: 2 is OEM B1, 3 is OEM B2
``pwm2_auto_point1_temp``   Off-to-low system-fan transition temperature
``pwm2_auto_point2_temp``   Low-to-high system-fan transition temperature
==========================  ==============================================

CPU fan profiles
----------------

Values 2 and 3 of ``pwm1_enable`` select the two complete automatic profiles
implemented by the firmware. Writing either value reloads the whole CPU fan
curve, including firmware state which is not visible through the ACPI EC
window. Other values are rejected.

System fan thresholds
---------------------

The two writable system-fan temperatures are rounded to whole degrees Celsius
and clamped to preserve strict ordering between both visible thresholds and
the firmware's internal third threshold. The third threshold does not select
a new PWM target and is therefore not exposed as another auto point.

Resume state preservation
-------------------------

Fan settings are held in EC RAM. On the supported firmware, initialization
during s2idle resume reloads the factory system-fan curve. The driver retains
the last coherent CPU profile and system-fan thresholds selected through its
interface and restores them synchronously at resume.
