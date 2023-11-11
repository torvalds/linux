Kernel driver ina209
====================

Supported chips:

  * Burr-Brown / Texas Instruments INA209

    Prefix: 'ina209'

    Addresses scanned: -

    Datasheet:
	https://www.ti.com/lit/gpn/ina209

Author:
	- Paul Hays <Paul.Hays@cattail.ca>
	- Ira W. Snyder <iws@ovro.caltech.edu>
	- Guenter Roeck <linux@roeck-us.net>


Description
-----------

The TI / Burr-Brown INA209 monitors voltage, current, and power on the high side
of a D.C. power supply. It can perform measurements and calculations in the
background to supply readings at any time. It includes a programmable
calibration multiplier to scale the displayed current and power values.


Sysfs entries
-------------

The INA209 chip is highly configurable both via hardwiring and via
the I2C bus. See the datasheet for details.

This tries to expose most monitoring features of the hardware via
sysfs. It does not support every feature of this chip.

======================= =======================================================
in0_input		shunt voltage (mV)
in0_input_highest	shunt voltage historical maximum reading (mV)
in0_input_lowest	shunt voltage historical minimum reading (mV)
in0_reset_history	reset shunt voltage history
in0_max			shunt voltage max alarm limit (mV)
in0_min			shunt voltage min alarm limit (mV)
in0_crit_max		shunt voltage crit max alarm limit (mV)
in0_crit_min		shunt voltage crit min alarm limit (mV)
in0_max_alarm		shunt voltage max alarm limit exceeded
in0_min_alarm		shunt voltage min alarm limit exceeded
in0_crit_max_alarm	shunt voltage crit max alarm limit exceeded
in0_crit_min_alarm	shunt voltage crit min alarm limit exceeded

in1_input		bus voltage (mV)
in1_input_highest	bus voltage historical maximum reading (mV)
in1_input_lowest	bus voltage historical minimum reading (mV)
in1_reset_history	reset bus voltage history
in1_max			bus voltage max alarm limit (mV)
in1_min			bus voltage min alarm limit (mV)
in1_crit_max		bus voltage crit max alarm limit (mV)
in1_crit_min		bus voltage crit min alarm limit (mV)
in1_max_alarm		bus voltage max alarm limit exceeded
in1_min_alarm		bus voltage min alarm limit exceeded
in1_crit_max_alarm	bus voltage crit max alarm limit exceeded
in1_crit_min_alarm	bus voltage crit min alarm limit exceeded

power1_input		power measurement (uW)
power1_input_highest	power historical maximum reading (uW)
power1_reset_history	reset power history
power1_max		power max alarm limit (uW)
power1_crit		power crit alarm limit (uW)
power1_max_alarm	power max alarm limit exceeded
power1_crit_alarm	power crit alarm limit exceeded

curr1_input		current measurement (mA)

update_interval		data conversion time; affects number of samples used
			to average results for shunt and bus voltages.
======================= =======================================================

General Remarks
---------------

The power and current registers in this chip require that the calibration
register is programmed correctly before they are used. Normally this is expected
to be done in the BIOS. In the absence of BIOS programming, the shunt resistor
voltage can be provided using platform data. The driver uses platform data from
the ina2xx driver for this purpose. If calibration register data is not provided
via platform data, the driver checks if the calibration register has been
programmed (ie has a value not equal to zero). If so, this value is retained.
Otherwise, a default value reflecting a shunt resistor value of 10 mOhm is
programmed into the calibration register.


Output Pins
-----------

Output pin programming is a board feature which depends on the BIOS. It is
outside the scope of a hardware monitoring driver to enable or disable output
pins.
