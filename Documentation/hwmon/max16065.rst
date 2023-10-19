Kernel driver max16065
======================


Supported chips:

  * Maxim MAX16065, MAX16066

    Prefixes: 'max16065', 'max16066'

    Addresses scanned: -

    Datasheet:

	http://datasheets.maxim-ic.com/en/ds/MAX16065-MAX16066.pdf

 *  Maxim MAX16067

    Prefix: 'max16067'

    Addresses scanned: -

    Datasheet:

	http://datasheets.maxim-ic.com/en/ds/MAX16067.pdf

 *  Maxim MAX16068

    Prefix: 'max16068'

    Addresses scanned: -

    Datasheet:

	http://datasheets.maxim-ic.com/en/ds/MAX16068.pdf

 *  Maxim MAX16070/MAX16071

    Prefixes: 'max16070', 'max16071'

    Addresses scanned: -

    Datasheet:

	http://datasheets.maxim-ic.com/en/ds/MAX16070-MAX16071.pdf

Author: Guenter Roeck <linux@roeck-us.net>


Description
-----------

[From datasheets] The MAX16065/MAX16066 flash-configurable system managers
monitor and sequence multiple system voltages. The MAX16065/MAX16066 can also
accurately monitor (+/-2.5%) one current channel using a dedicated high-side
current-sense amplifier. The MAX16065 manages up to twelve system voltages
simultaneously, and the MAX16066 manages up to eight supply voltages.

The MAX16067 flash-configurable system manager monitors and sequences multiple
system voltages. The MAX16067 manages up to six system voltages simultaneously.

The MAX16068 flash-configurable system manager monitors and manages up to six
system voltages simultaneously.

The MAX16070/MAX16071 flash-configurable system monitors supervise multiple
system voltages. The MAX16070/MAX16071 can also accurately monitor (+/-2.5%)
one current channel using a dedicated high-side current-sense amplifier. The
MAX16070 monitors up to twelve system voltages simultaneously, and the MAX16071
monitors up to eight supply voltages.

Each monitored channel has its own low and high critical limits. MAX16065,
MAX16066, MAX16070, and MAX16071 support an additional limit which is
configurable as either low or high secondary limit. MAX16065, MAX16066,
MAX16070, and MAX16071 also support supply current monitoring.


Usage Notes
-----------

This driver does not probe for devices, since there is no register which
can be safely used to identify the chip. You will have to instantiate
the devices explicitly. Please see Documentation/i2c/instantiating-devices.rst for
details.

WARNING: Do not access chip registers using the i2cdump command, and do not use
any of the i2ctools commands on a command register (0xa5 to 0xac). The chips
supported by this driver interpret any access to a command register (including
read commands) as request to execute the command in question. This may result in
power loss, board resets, and/or Flash corruption. Worst case, your board may
turn into a brick.


Sysfs entries
-------------

======================= ========================================================
in[0-11]_input		Input voltage measurements.

in12_input		Voltage on CSP (Current Sense Positive) pin.
			Only if the chip supports current sensing and if
			current sensing is enabled.

in[0-11]_min		Low warning limit.
			Supported on MAX16065, MAX16066, MAX16070, and MAX16071
			only.

in[0-11]_max		High warning limit.
			Supported on MAX16065, MAX16066, MAX16070, and MAX16071
			only.

			Either low or high warning limits are supported
			(depending on chip configuration), but not both.

in[0-11]_lcrit		Low critical limit.

in[0-11]_crit		High critical limit.

in[0-11]_alarm		Input voltage alarm.

curr1_input		Current sense input; only if the chip supports current
			sensing and if current sensing is enabled.
			Displayed current assumes 0.001 Ohm current sense
			resistor.

curr1_alarm		Overcurrent alarm; only if the chip supports current
			sensing and if current sensing is enabled.
======================= ========================================================
