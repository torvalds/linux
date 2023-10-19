Kernel driver thmc50
=====================

Supported chips:

  * Analog Devices ADM1022

    Prefix: 'adm1022'

    Addresses scanned: I2C 0x2c - 0x2e

    Datasheet: http://www.analog.com/en/prod/0,2877,ADM1022,00.html

  * Texas Instruments THMC50

    Prefix: 'thmc50'

    Addresses scanned: I2C 0x2c - 0x2e

    Datasheet: https://www.ti.com/


Author: Krzysztof Helt <krzysztof.h1@wp.pl>

This driver was derived from the 2.4 kernel thmc50.c source file.

Credits:

  thmc50.c (2.4 kernel):

	- Frodo Looijaard <frodol@dds.nl>
	- Philip Edelbrock <phil@netroedge.com>

Module Parameters
-----------------

* adm1022_temp3: short array
    List of adapter,address pairs to force chips into ADM1022 mode with
    second remote temperature. This does not work for original THMC50 chips.

Description
-----------

The THMC50 implements: an internal temperature sensor, support for an
external diode-type temperature sensor (compatible w/ the diode sensor inside
many processors), and a controllable fan/analog_out DAC. For the temperature
sensors, limits can be set through the appropriate Overtemperature Shutdown
register and Hysteresis register. Each value can be set and read to half-degree
accuracy.  An alarm is issued (usually to a connected LM78) when the
temperature gets higher then the Overtemperature Shutdown value; it stays on
until the temperature falls below the Hysteresis value. All temperatures are in
degrees Celsius, and are guaranteed within a range of -55 to +125 degrees.

The THMC50 only updates its values each 1.5 seconds; reading it more often
will do no harm, but will return 'old' values.

The THMC50 is usually used in combination with LM78-like chips, to measure
the temperature of the processor(s).

The ADM1022 works the same as THMC50 but it is faster (5 Hz instead of
1 Hz for THMC50). It can be also put in a new mode to handle additional
remote temperature sensor. The driver use the mode set by BIOS by default.

In case the BIOS is broken and the mode is set incorrectly, you can force
the mode with additional remote temperature with adm1022_temp3 parameter.
A typical symptom of wrong setting is a fan forced to full speed.

Driver Features
---------------

The driver provides up to three temperatures:

temp1
	- internal
temp2
	- remote
temp3
	- 2nd remote only for ADM1022

pwm1
	- fan speed (0 = stop, 255 = full)
pwm1_mode
	- always 0 (DC mode)

The value of 0 for pwm1 also forces FAN_OFF signal from the chip,
so it stops fans even if the value 0 into the ANALOG_OUT register does not.

The driver was tested on Compaq AP550 with two ADM1022 chips (one works
in the temp3 mode), five temperature readings and two fans.
