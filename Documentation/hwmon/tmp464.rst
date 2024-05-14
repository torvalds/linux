.. SPDX-License-Identifier: GPL-2.0

Kernel driver tmp464
====================

Supported chips:

  * Texas Instruments TMP464

    Prefix: 'tmp464'

    Addresses scanned: I2C 0x48, 0x49, 0x4a and 0x4b

    Datasheet: http://focus.ti.com/docs/prod/folders/print/tmp464.html

  * Texas Instruments TMP468

    Prefix: 'tmp468'

    Addresses scanned: I2C 0x48, 0x49, 0x4a and 0x4b

    Datasheet: http://focus.ti.com/docs/prod/folders/print/tmp468.html

Authors:

	Agathe Porte <agathe.porte@nokia.com>
	Guenter Roeck <linux@roeck-us.net>

Description
-----------

This driver implements support for Texas Instruments TMP464 and TMP468
temperature sensor chips. TMP464 provides one local and four remote
sensors. TMP468 provides one local and eight remote sensors.
Temperature is measured in degrees Celsius. The chips are wired over
I2C/SMBus and specified over a temperature range of -40 to +125 degrees
Celsius. Resolution for both the local and remote channels is 0.0625
degree C.

The chips support only temperature measurements. The driver exports
temperature values, limits, and alarms via the following sysfs files:

**temp[1-9]_input**

**temp[1-9]_max**

**temp[1-9]_max_hyst**

**temp[1-9]_max_alarm**

**temp[1-9]_crit**

**temp[1-9]_crit_alarm**

**temp[1-9]_crit_hyst**

**temp[2-9]_offset**

**temp[2-9]_fault**

Each sensor can be individually disabled via Devicetree or from sysfs
via:

**temp[1-9]_enable**

If labels were specified in Devicetree, additional sysfs files will
be present:

**temp[1-9]_label**

The update interval is configurable with the following sysfs attribute.

**update_interval**
