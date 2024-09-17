.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver nzxt-smart2
=========================

Supported devices:

- NZXT RGB & Fan controller
- NZXT Smart Device v2

Description
-----------

This driver implements monitoring and control of fans plugged into the device.
Besides typical speed monitoring and PWM duty cycle control, voltage and current
is reported for every fan.

The device also has two connectors for RGB LEDs; support for them isn't
implemented (mainly because there is no standardized sysfs interface).

Also, the device has a noise sensor, but the sensor seems to be completely
useless (and very imprecise), so support for it isn't implemented too.

Usage Notes
-----------

The device should be autodetected, and the driver should load automatically.

If fans are plugged in/unplugged while the system is powered on, the driver
must be reloaded to detect configuration changes; otherwise, new fans can't
be controlled (`pwm*` changes will be ignored). It is necessary because the
device has a dedicated "detect fans" command, and currently, it is executed only
during initialization. Speed, voltage, current monitoring will work even without
reload. As an alternative to reloading the module, a userspace tool (like
`liquidctl`_) can be used to run "detect fans" command through hidraw interface.

The driver coexists with userspace tools that access the device through hidraw
interface with no known issues.

.. _liquidctl: https://github.com/liquidctl/liquidctl

Sysfs entries
-------------

=======================	========================================================
fan[1-3]_input		Fan speed monitoring (in rpm).
curr[1-3]_input		Current supplied to the fan (in milliamperes).
in[0-2]_input		Voltage supplied to the fan (in millivolts).
pwm[1-3]		Controls fan speed: PWM duty cycle for PWM-controlled
			fans, voltage for other fans. Voltage can be changed in
			9-12 V range, but the value of the sysfs attribute is
			always in 0-255 range (1 = 9V, 255 = 12V). Setting the
			attribute to 0 turns off the fan completely.
pwm[1-3]_enable		1 if the fan can be controlled by writing to the
			corresponding pwm* attribute, 0 otherwise. The device
			can control only the fans it detected itself, so the
			attribute is read-only.
pwm[1-3]_mode		Read-only, 1 for PWM-controlled fans, 0 for other fans
			(or if no fan connected).
update_interval		The interval at which all inputs are updated (in
			milliseconds). The default is 1000ms. Minimum is 250ms.
=======================	========================================================
