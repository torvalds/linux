Kernel driver ina3221
=====================

Supported chips:

  * Texas Instruments INA3221

    Prefix: 'ina3221'

    Addresses: I2C 0x40 - 0x43

    Datasheet: Publicly available at the Texas Instruments website

	       https://www.ti.com/

Author: Andrew F. Davis <afd@ti.com>

Description
-----------

The Texas Instruments INA3221 monitors voltage, current, and power on the high
side of up to three D.C. power supplies. The INA3221 monitors both shunt drop
and supply voltage, with programmable conversion times and averaging, current
and power are calculated host-side from these.

Sysfs entries
-------------

======================= =======================================================
in[123]_label           Voltage channel labels
in[123]_enable          Voltage channel enable controls
in[123]_input           Bus voltage(mV) channels
curr[123]_input         Current(mA) measurement channels
shunt[123]_resistor     Shunt resistance(uOhm) channels
curr[123]_crit          Critical alert current(mA) setting, activates the
			corresponding alarm when the respective current
			is above this value
curr[123]_crit_alarm    Critical alert current limit exceeded
curr[123]_max           Warning alert current(mA) setting, activates the
			corresponding alarm when the respective current
			average is above this value.
curr[123]_max_alarm     Warning alert current limit exceeded
in[456]_input           Shunt voltage(uV) for channels 1, 2, and 3 respectively
in7_input               Sum of shunt voltage(uV) channels
in7_label               Channel label for sum of shunt voltage
curr4_input             Sum of current(mA) measurement channels,
                        (only available when all channels use the same resistor
                        value for their shunt resistors)
curr4_crit              Critical alert current(mA) setting for sum of current
                        measurements, activates the corresponding alarm
                        when the respective current is above this value
                        (only effective when all channels use the same resistor
                        value for their shunt resistors)
curr4_crit_alarm        Critical alert current limit exceeded for sum of
                        current measurements.
samples                 Number of samples using in the averaging mode.

                        Supports the list of number of samples:

                          1, 4, 16, 64, 128, 256, 512, 1024

update_interval         Data conversion time in millisecond, following:

                          update_interval = C x S x (BC + SC)

                          * C:	number of enabled channels
                          * S:	number of samples
                          * BC:	bus-voltage conversion time in millisecond
                          * SC:	shunt-voltage conversion time in millisecond

                        Affects both Bus- and Shunt-voltage conversion time.
                        Note that setting update_interval to 0ms sets both BC
                        and SC to 140 us (minimum conversion time).
======================= =======================================================
