Kernel driver zl6100
====================

Supported chips:

  * Renesas / Intersil / Zilker Labs ZL2004

    Prefix: 'zl2004'

    Addresses scanned: -

    Datasheet: https://www.renesas.com/us/en/document/dst/zl2004-datasheet.pdf

  * Renesas / Intersil / Zilker Labs ZL2005

    Prefix: 'zl2005'

    Addresses scanned: -

    Datasheet: https://www.renesas.com/us/en/document/dst/zl2005-datasheet.pdf

  * Renesas / Intersil / Zilker Labs ZL2006

    Prefix: 'zl2006'

    Addresses scanned: -

    Datasheet: https://www.renesas.com/us/en/document/dst/zl2006-datasheet.pdf

  * Renesas / Intersil / Zilker Labs ZL2008

    Prefix: 'zl2008'

    Addresses scanned: -

    Datasheet: https://www.renesas.com/us/en/document/dst/zl2008-datasheet.pdf

  * Renesas / Intersil / Zilker Labs ZL2105

    Prefix: 'zl2105'

    Addresses scanned: -

    Datasheet: https://www.renesas.com/us/en/document/dst/zl2105-datasheet.pdf

  * Renesas / Intersil / Zilker Labs ZL2106

    Prefix: 'zl2106'

    Addresses scanned: -

    Datasheet: https://www.renesas.com/us/en/document/dst/zl2106-datasheet.pdf

  * Renesas / Intersil / Zilker Labs ZL6100

    Prefix: 'zl6100'

    Addresses scanned: -

    Datasheet: https://www.renesas.com/us/en/document/dst/zl6100-datasheet.pdf

  * Renesas / Intersil / Zilker Labs ZL6105

    Prefix: 'zl6105'

    Addresses scanned: -

    Datasheet: https://www.renesas.com/us/en/document/dst/zl6105-datasheet.pdf

  * Renesas / Intersil / Zilker Labs ZL8802

    Prefix: 'zl8802'

    Addresses scanned: -

    Datasheet: https://www.renesas.com/us/en/document/dst/zl8802-datasheet

  * Renesas / Intersil / Zilker Labs ZL9101M

    Prefix: 'zl9101'

    Addresses scanned: -

    Datasheet: https://www.renesas.com/us/en/document/dst/zl9101m-datasheet

  * Renesas / Intersil / Zilker Labs ZL9117M

    Prefix: 'zl9117'

    Addresses scanned: -

    Datasheet: https://www.renesas.com/us/en/document/dst/zl9117m-datasheet

  * Renesas / Intersil / Zilker Labs ZLS1003, ZLS4009

    Prefix: 'zls1003', zls4009

    Addresses scanned: -

    Datasheet: Not published

  * Flex BMR450, BMR451

    Prefix: 'bmr450', 'bmr451'

    Addresses scanned: -

    Datasheet:

https://flexpowermodules.com/resources/fpm-techspec-bmr450-digital-pol-regulators-20a

  * Flex BMR462, BMR463, BMR464

    Prefixes: 'bmr462', 'bmr463', 'bmr464'

    Addresses scanned: -

    Datasheet: https://flexpowermodules.com/resources/fpm-techspec-bmr462

  * Flex BMR465, BMR467

    Prefixes: 'bmr465', 'bmr467'

    Addresses scanned: -

    Datasheet: https://flexpowermodules.com/resources/fpm-techspec-bmr465-digital-pol

  * Flex BMR466

    Prefixes: 'bmr466'

    Addresses scanned: -

    Datasheet: https://flexpowermodules.com/resources/fpm-techspec-bmr466-8x12

  * Flex BMR469

    Prefixes: 'bmr469'

    Addresses scanned: -

    Datasheet: https://flexpowermodules.com/resources/fpm-techspec-bmr4696001

Author: Guenter Roeck <linux@roeck-us.net>


Description
-----------

This driver supports hardware monitoring for Renesas / Intersil / Zilker Labs
ZL6100 and compatible digital DC-DC controllers.

The driver is a client driver to the core PMBus driver. Please see
Documentation/hwmon/pmbus.rst and Documentation.hwmon/pmbus-core for details
on PMBus client drivers.


Usage Notes
-----------

This driver does not auto-detect devices. You will have to instantiate the
devices explicitly. Please see Documentation/i2c/instantiating-devices.rst for
details.

.. warning::

  Do not access chip registers using the i2cdump command, and do not use
  any of the i2ctools commands on a command register used to save and restore
  configuration data (0x11, 0x12, 0x15, 0x16, and 0xf4). The chips supported by
  this driver interpret any access to those command registers (including read
  commands) as request to execute the command in question. Unless write accesses
  to those registers are protected, this may result in power loss, board resets,
  and/or Flash corruption. Worst case, your board may turn into a brick.


Platform data support
---------------------

The driver supports standard PMBus driver platform data.


Module parameters
-----------------

delay
-----

Renesas/Intersil/Zilker Labs DC-DC controllers require a minimum interval
between I2C bus accesses. According to Intersil, the minimum interval is 2 ms,
though 1 ms appears to be sufficient and has not caused any problems in testing.
The problem is known to affect all currently supported chips. For manual override,
the driver provides a writeable module parameter, 'delay', which can be used
to set the interval to a value between 0 and 65,535 microseconds.


Sysfs entries
-------------

The following attributes are supported. Limits are read-write; all other
attributes are read-only.

======================= ========================================================
in1_label		"vin"
in1_input		Measured input voltage.
in1_min			Minimum input voltage.
in1_max			Maximum input voltage.
in1_lcrit		Critical minimum input voltage.
in1_crit		Critical maximum input voltage.
in1_min_alarm		Input voltage low alarm.
in1_max_alarm		Input voltage high alarm.
in1_lcrit_alarm		Input voltage critical low alarm.
in1_crit_alarm		Input voltage critical high alarm.

in2_label		"vmon"
in2_input		Measured voltage on VMON (ZL2004) or VDRV (ZL9101M,
			ZL9117M) pin. Reported voltage is 16x the voltage on the
			pin (adjusted internally by the chip).
in2_lcrit		Critical minimum VMON/VDRV Voltage.
in2_crit		Critical maximum VMON/VDRV voltage.
in2_lcrit_alarm		VMON/VDRV voltage critical low alarm.
in2_crit_alarm		VMON/VDRV voltage critical high alarm.

			vmon attributes are supported on ZL2004, ZL8802,
			ZL9101M, ZL9117M and ZLS4009 only.

inX_label		"vout[12]"
inX_input		Measured output voltage.
inX_lcrit		Critical minimum output Voltage.
inX_crit		Critical maximum output voltage.
inX_lcrit_alarm		Critical output voltage critical low alarm.
inX_crit_alarm		Critical output voltage critical high alarm.

			X is 3 for ZL2004, ZL9101M, and ZL9117M,
			3, 4 for ZL8802 and 2 otherwise.

curr1_label		"iin"
curr1_input		Measured input current.

			iin attributes are supported on ZL8802 only

currY_label		"iout[12]"
currY_input		Measured output current.
currY_lcrit		Critical minimum output current.
currY_crit		Critical maximum output current.
currY_lcrit_alarm	Output current critical low alarm.
currY_crit_alarm	Output current critical high alarm.

			Y is 2, 3 for ZL8802, 1 otherwise

temp[12]_input		Measured temperature.
temp[12]_min		Minimum temperature.
temp[12]_max		Maximum temperature.
temp[12]_lcrit		Critical low temperature.
temp[12]_crit		Critical high temperature.
temp[12]_min_alarm	Chip temperature low alarm.
temp[12]_max_alarm	Chip temperature high alarm.
temp[12]_lcrit_alarm	Chip temperature critical low alarm.
temp[12]_crit_alarm	Chip temperature critical high alarm.
======================= ========================================================
