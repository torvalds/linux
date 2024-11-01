Kernel driver tps53679
======================

Supported chips:

  * Texas Instruments TPS53647

    Prefix: 'tps53647'

    Addresses scanned: -

    Datasheet: https://www.ti.com/lit/gpn/tps53647

  * Texas Instruments TPS53667

    Prefix: 'tps53667'

    Addresses scanned: -

    Datasheet: https://www.ti.com/lit/gpn/TPS53667

  * Texas Instruments TPS53676

    Prefix: 'tps53676'

    Addresses scanned: -

    Datasheet: https://www.ti.com/lit/gpn/TPS53676

  * Texas Instruments TPS53679

    Prefix: 'tps53679'

    Addresses scanned: -

    Datasheet: https://www.ti.com/lit/gpn/TPS53679 (short version)

  * Texas Instruments TPS53681

    Prefix: 'tps53681'

    Addresses scanned: -

    Datasheet: https://www.ti.com/lit/gpn/TPS53681

  * Texas Instruments TPS53688

    Prefix: 'tps53688'

    Addresses scanned: -

    Datasheet: Available under NDA


Authors:
	Vadim Pasternak <vadimp@mellanox.com>
	Guenter Roeck <linux@roeck-us.net>


Description
-----------

Chips in this series are multi-phase step-down converters with one or two
output channels and up to 8 phases per channel.


Usage Notes
-----------

This driver does not probe for PMBus devices. You will have to instantiate
devices explicitly.

Example: the following commands will load the driver for an TPS53681 at address
0x60 on I2C bus #1::

	# modprobe tps53679
	# echo tps53681 0x60 > /sys/bus/i2c/devices/i2c-1/new_device


Sysfs attributes
----------------

======================= ========================================================
in1_label		"vin"

in1_input		Measured input voltage.

in1_lcrit		Critical minimum input voltage

			TPS53679, TPS53681, TPS53688 only.

in1_lcrit_alarm		Input voltage critical low alarm.

			TPS53679, TPS53681, TPS53688 only.

in1_crit		Critical maximum input voltage.

in1_crit_alarm		Input voltage critical high alarm.

in[N]_label		"vout[1-2]"

			- TPS53647, TPS53667: N=2
			- TPS53679, TPS53588: N=2,3

in[N]_input		Measured output voltage.

in[N]_lcrit		Critical minimum input voltage.

			TPS53679, TPS53681, TPS53688 only.

in[N]_lcrit_alarm	Critical minimum voltage alarm.

			TPS53679, TPS53681, TPS53688 only.

in[N]_alarm		Output voltage alarm.

			TPS53647, TPS53667 only.

in[N]_crit		Critical maximum output voltage.

			TPS53679, TPS53681, TPS53688 only.

in[N]_crit_alarm	Output voltage critical high alarm.

			TPS53679, TPS53681, TPS53688 only.

temp[N]_input		Measured temperature.

			- TPS53647, TPS53667: N=1
			- TPS53679, TPS53681, TPS53588: N=1,2

temp[N]_max		Maximum temperature.

temp[N]_crit		Critical high temperature.

temp[N]_max_alarm	Temperature high alarm.

temp[N]_crit_alarm	Temperature critical high alarm.

power1_label		"pin".

power1_input		Measured input power.

power[N]_label		"pout[1-2]".

			- TPS53647, TPS53667: N=2
			- TPS53676, TPS53679, TPS53681, TPS53588: N=2,3

power[N]_input		Measured output power.

curr1_label		"iin".

curr1_input		Measured input current.

curr1_max		Maximum input current.

curr1_max_alarm		Input current high alarm.

curr1_crit		Critical input current.

curr1_crit_alarm	Input current critical alarm.

curr[N]_label		"iout[1-2]" or "iout1.[0-5]".

			The first digit is the output channel, the second
			digit is the phase within the channel. Per-phase
			telemetry supported on TPS53676 and TPS53681 only.

			- TPS53647, TPS53667: N=2
			- TPS53679, TPS53588: N=2,3
			- TPS53676: N=2-8
			- TPS53681: N=2-9

curr[N]_input		Measured output current.

curr[N]_max		Maximum output current.

curr[N]_crit		Critical high output current.

curr[N]_max_alarm	Output current high alarm.

curr[N]_crit_alarm	Output current critical high alarm.

			Limit and alarm attributes are only available for
			non-phase telemetry (iout1, iout2).

======================= ========================================================
