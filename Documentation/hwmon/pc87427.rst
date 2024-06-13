Kernel driver pc87427
=====================

Supported chips:

  * National Semiconductor PC87427

    Prefix: 'pc87427'

    Addresses scanned: none, address read from Super I/O config space

    Datasheet: No longer available

Author: Jean Delvare <jdelvare@suse.de>

Thanks to Amir Habibi at Candelis for setting up a test system, and to
Michael Kress for testing several iterations of this driver.


Description
-----------

The National Semiconductor Super I/O chip includes complete hardware
monitoring capabilities. It can monitor up to 18 voltages, 8 fans and
6 temperature sensors. Only the fans and temperatures are supported at
the moment, voltages aren't.

This chip also has fan controlling features (up to 4 PWM outputs),
which are partly supported by this driver.

The driver assumes that no more than one chip is present, which seems
reasonable.


Fan Monitoring
--------------

Fan rotation speeds are reported as 14-bit values from a gated clock
signal. Speeds down to 83 RPM can be measured.

An alarm is triggered if the rotation speed drops below a programmable
limit. Another alarm is triggered if the speed is too low to be measured
(including stalled or missing fan).


Fan Speed Control
-----------------

Fan speed can be controlled by PWM outputs. There are 4 possible modes:
always off, always on, manual and automatic. The latter isn't supported
by the driver: you can only return to that mode if it was the original
setting, and the configuration interface is missing.


Temperature Monitoring
----------------------

The PC87427 relies on external sensors (following the SensorPath
standard), so the resolution and range depend on the type of sensor
connected. The integer part can be 8-bit or 9-bit, and can be signed or
not. I couldn't find a way to figure out the external sensor data
temperature format, so user-space adjustment (typically by a factor 2)
may be required.
