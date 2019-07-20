Kernel driver tmp102
====================

Supported chips:

  * Texas Instruments TMP102

    Prefix: 'tmp102'

    Addresses scanned: none

    Datasheet: http://focus.ti.com/docs/prod/folders/print/tmp102.html

Author:

	Steven King <sfking@fdwdc.com>

Description
-----------

The Texas Instruments TMP102 implements one temperature sensor.  Limits can be
set through the Overtemperature Shutdown register and Hysteresis register.  The
sensor is accurate to 0.5 degree over the range of -25 to +85 C, and to 1.0
degree from -40 to +125 C. Resolution of the sensor is 0.0625 degree.  The
operating temperature has a minimum of -55 C and a maximum of +150 C.

The TMP102 has a programmable update rate that can select between 8, 4, 1, and
0.5 Hz. (Currently the driver only supports the default of 4 Hz).

The driver provides the common sysfs-interface for temperatures (see
Documentation/hwmon/sysfs-interface.rst under Temperatures).
