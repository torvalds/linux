Kernel driver emc2103
======================

Supported chips:

  * SMSC EMC2103

    Addresses scanned: I2C 0x2e

    Prefix: 'emc2103'

    Datasheet: Not public

Authors:
	Steve Glendinning <steve.glendinning@smsc.com>

Description
-----------

The Standard Microsystems Corporation (SMSC) EMC2103 chips
contain up to 4 temperature sensors and a single fan controller.

Fan rotation speeds are reported in RPM (rotations per minute). An alarm is
triggered if the rotation speed has dropped below a programmable limit. Fan
readings can be divided by a programmable divider (1, 2, 4 or 8) to give
the readings more range or accuracy. Not all RPM values can accurately be
represented, so some rounding is done. With a divider of 1, the lowest
representable value is 480 RPM.

This driver supports RPM based control, to use this a fan target
should be written to fan1_target and pwm1_enable should be set to 3.

The 2103-2 and 2103-4 variants have a third temperature sensor, which can
be connected to two anti-parallel diodes.  These values can be read
as temp3 and temp4.  If only one diode is attached to this channel, temp4
will show as "fault".  The module parameter "apd=0" can be used to suppress
this 4th channel when anti-parallel diodes are not fitted.
