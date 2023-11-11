.. SPDX-License-Identifier: GPL-2.0

Kernel driver lan966x-hwmon
===========================

Supported chips:

  * Microchip LAN9668 (sensor in SoC)

    Prefix: 'lan9668-hwmon'

    Datasheet: https://microchip-ung.github.io/lan9668_reginfo

Authors:

	Michael Walle <michael@walle.cc>

Description
-----------

This driver implements support for the Microchip LAN9668 on-chip
temperature sensor as well as its fan controller. It provides one
temperature sensor and one fan controller. The temperature range
of the sensor is specified from -40 to +125 degrees Celsius and
its accuracy is +/- 5 degrees Celsius. The fan controller has a
tacho input and a PWM output with a customizable PWM output
frequency ranging from ~20Hz to ~650kHz.

No alarms are supported by the SoC.

The driver exports temperature values, fan tacho input and PWM
settings via the following sysfs files:

**temp1_input**

**fan1_input**

**pwm1**

**pwm1_freq**
