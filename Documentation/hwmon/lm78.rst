Kernel driver lm78
==================

Supported chips:

  * National Semiconductor LM78 / LM78-J

    Prefix: 'lm78'

    Addresses scanned: I2C 0x28 - 0x2f, ISA 0x290 (8 I/O ports)

    Datasheet: Publicly available at the National Semiconductor website

	       http://www.national.com/

  * National Semiconductor LM79

    Prefix: 'lm79'

    Addresses scanned: I2C 0x28 - 0x2f, ISA 0x290 (8 I/O ports)

    Datasheet: Publicly available at the National Semiconductor website

	       http://www.national.com/


Authors:
	- Frodo Looijaard <frodol@dds.nl>
	- Jean Delvare <jdelvare@suse.de>

Description
-----------

This driver implements support for the National Semiconductor LM78, LM78-J
and LM79. They are described as 'Microprocessor System Hardware Monitors'.

There is almost no difference between the three supported chips. Functionally,
the LM78 and LM78-J are exactly identical. The LM79 has one more VID line,
which is used to report the lower voltages newer Pentium processors use.
From here on, LM7* means either of these three types.

The LM7* implements one temperature sensor, three fan rotation speed sensors,
seven voltage sensors, VID lines, alarms, and some miscellaneous stuff.

Temperatures are measured in degrees Celsius. An alarm is triggered once
when the Overtemperature Shutdown limit is crossed; it is triggered again
as soon as it drops below the Hysteresis value. A more useful behavior
can be found by setting the Hysteresis value to +127 degrees Celsius; in
this case, alarms are issued during all the time when the actual temperature
is above the Overtemperature Shutdown value. Measurements are guaranteed
between -55 and +125 degrees, with a resolution of 1 degree.

Fan rotation speeds are reported in RPM (rotations per minute). An alarm is
triggered if the rotation speed has dropped below a programmable limit. Fan
readings can be divided by a programmable divider (1, 2, 4 or 8) to give
the readings more range or accuracy. Not all RPM values can accurately be
represented, so some rounding is done. With a divider of 2, the lowest
representable value is around 2600 RPM.

Voltage sensors (also known as IN sensors) report their values in volts.
An alarm is triggered if the voltage has crossed a programmable minimum
or maximum limit. Note that minimum in this case always means 'closest to
zero'; this is important for negative voltage measurements. All voltage
inputs can measure voltages between 0 and 4.08 volts, with a resolution
of 0.016 volt.

The VID lines encode the core voltage value: the voltage level your processor
should work with. This is hardcoded by the mainboard and/or processor itself.
It is a value in volts. When it is unconnected, you will often find the
value 3.50 V here.

If an alarm triggers, it will remain triggered until the hardware register
is read at least once. This means that the cause for the alarm may
already have disappeared! Note that in the current implementation, all
hardware registers are read whenever any data is read (unless it is less
than 1.5 seconds since the last update). This means that you can easily
miss once-only alarms.

The LM7* only updates its values each 1.5 seconds; reading it more often
will do no harm, but will return 'old' values.
