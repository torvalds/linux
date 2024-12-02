Kernel driver lm63
==================

Supported chips:

  * National Semiconductor LM63

    Prefix: 'lm63'

    Addresses scanned: I2C 0x4c

    Datasheet: Publicly available at the National Semiconductor website

	       http://www.national.com/pf/LM/LM63.html

  * National Semiconductor LM64

    Prefix: 'lm64'

    Addresses scanned: I2C 0x18 and 0x4e

    Datasheet: Publicly available at the National Semiconductor website

	       http://www.national.com/pf/LM/LM64.html

  * National Semiconductor LM96163

    Prefix: 'lm96163'

    Addresses scanned: I2C 0x4c

    Datasheet: Publicly available at the National Semiconductor website

	       http://www.national.com/pf/LM/LM96163.html


Author: Jean Delvare <jdelvare@suse.de>

Thanks go to Tyan and especially Alex Buckingham for setting up a remote
access to their S4882 test platform for this driver.

  https://www.tyan.com/

Description
-----------

The LM63 is a digital temperature sensor with integrated fan monitoring
and control.

The LM63 is basically an LM86 with fan speed monitoring and control
capabilities added. It misses some of the LM86 features though:

 - No low limit for local temperature.
 - No critical limit for local temperature.
 - Critical limit for remote temperature can be changed only once. We
   will consider that the critical limit is read-only.

The datasheet isn't very clear about what the tachometer reading is.

An explanation from National Semiconductor: The two lower bits of the read
value have to be masked out. The value is still 16 bit in width.

All temperature values are given in degrees Celsius. Resolution is 1.0
degree for the local temperature, 0.125 degree for the remote temperature.

The fan speed is measured using a tachometer. Contrary to most chips which
store the value in an 8-bit register and have a selectable clock divider
to make sure that the result will fit in the register, the LM63 uses 16-bit
value for measuring the speed of the fan. It can measure fan speeds down to
83 RPM, at least in theory.

Note that the pin used for fan monitoring is shared with an alert out
function. Depending on how the board designer wanted to use the chip, fan
speed monitoring will or will not be possible. The proper chip configuration
is left to the BIOS, and the driver will blindly trust it. Only the original
LM63 suffers from this limitation, the LM64 and LM96163 have separate pins
for fan monitoring and alert out. On the LM64, monitoring is always enabled;
on the LM96163 it can be disabled.

A PWM output can be used to control the speed of the fan. The LM63 has two
PWM modes: manual and automatic. Automatic mode is not fully implemented yet
(you cannot define your custom PWM/temperature curve), and mode change isn't
supported either.

The lm63 driver will not update its values more frequently than configured with
the update_interval sysfs attribute; reading them more often will do no harm,
but will return 'old' values. Values in the automatic fan control lookup table
(attributes pwm1_auto_*) have their own independent lifetime of 5 seconds.

The LM64 is effectively an LM63 with GPIO lines. The driver does not
support these GPIO lines at present.

The LM96163 is an enhanced version of LM63 with improved temperature accuracy
and better PWM resolution. For LM96163, the external temperature sensor type is
configurable as CPU embedded diode(1) or 3904 transistor(2).
