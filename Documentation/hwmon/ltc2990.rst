Kernel driver ltc2990
=====================


Supported chips:

  * Linear Technology LTC2990

    Prefix: 'ltc2990'

    Addresses scanned: -

    Datasheet: http://www.linear.com/product/ltc2990



Author:

	- Mike Looijmans <mike.looijmans@topic.nl>
	- Tom Levens <tom.levens@cern.ch>


Description
-----------

LTC2990 is a Quad I2C Voltage, Current and Temperature Monitor.
The chip's inputs can measure 4 voltages, or two inputs together (1+2 and 3+4)
can be combined to measure a differential voltage, which is typically used to
measure current through a series resistor, or a temperature with an external
diode.


Usage Notes
-----------

This driver does not probe for PMBus devices. You will have to instantiate
devices explicitly.


Sysfs attributes
----------------

============= ==================================================
in0_input     Voltage at Vcc pin in millivolt (range 2.5V to 5V)
temp1_input   Internal chip temperature in millidegrees Celsius
============= ==================================================

A subset of the following attributes are visible, depending on the measurement
mode of the chip.

============= ==========================================================
in[1-4]_input Voltage at V[1-4] pin in millivolt
temp2_input   External temperature sensor TR1 in millidegrees Celsius
temp3_input   External temperature sensor TR2 in millidegrees Celsius
curr1_input   Current in mA across V1-V2 assuming a 1mOhm sense resistor
curr2_input   Current in mA across V3-V4 assuming a 1mOhm sense resistor
============= ==========================================================

The "curr*_input" measurements actually report the voltage drop across the
input pins in microvolts. This is equivalent to the current through a 1mOhm
sense resistor. Divide the reported value by the actual sense resistor value
in mOhm to get the actual value.
