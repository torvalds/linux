Kernel driver adt7470
=====================

Supported chips:

  * Analog Devices ADT7470

    Prefix: 'adt7470'

    Addresses scanned: I2C 0x2C, 0x2E, 0x2F

    Datasheet: Publicly available at the Analog Devices website

Author: Darrick J. Wong

Description
-----------

This driver implements support for the Analog Devices ADT7470 chip.  There may
be other chips that implement this interface.

The ADT7470 uses the 2-wire interface compatible with the SMBus 2.0
specification. Using an analog to digital converter it measures up to ten (10)
external temperatures. It has four (4) 16-bit counters for measuring fan speed.
There are four (4) PWM outputs that can be used to control fan speed.

A sophisticated control system for the PWM outputs is designed into the ADT7470
that allows fan speed to be adjusted automatically based on any of the ten
temperature sensors. Each PWM output is individually adjustable and
programmable. Once configured, the ADT7470 will adjust the PWM outputs in
response to the measured temperatures with further host intervention.  This
feature can also be disabled for manual control of the PWM's.

Each of the measured inputs (temperature, fan speed) has corresponding high/low
limit values. The ADT7470 will signal an ALARM if any measured value exceeds
either limit.

The ADT7470 samples all inputs continuously.  A kernel thread is started up for
the purpose of periodically querying the temperature sensors, thus allowing the
automatic fan pwm control to set the fan speed.  The driver will not read the
registers more often than once every 5 seconds.  Further, configuration data is
only read once per minute.

Special Features
----------------

The ADT7470 has a 8-bit ADC and is capable of measuring temperatures with 1
degC resolution.

The Analog Devices datasheet is very detailed and describes a procedure for
determining an optimal configuration for the automatic PWM control.

Configuration Notes
-------------------

Besides standard interfaces driver adds the following:

* PWM Control

* pwm#_auto_point1_pwm and pwm#_auto_point1_temp and
* pwm#_auto_point2_pwm and pwm#_auto_point2_temp -

  - point1: Set the pwm speed at a lower temperature bound.
  - point2: Set the pwm speed at a higher temperature bound.

The ADT7470 will scale the pwm between the lower and higher pwm speed when
the temperature is between the two temperature boundaries.  PWM values range
from 0 (off) to 255 (full speed).  Fan speed will be set to maximum when the
temperature sensor associated with the PWM control exceeds
pwm#_auto_point2_temp.

The driver also allows control of the PWM frequency:

* pwm1_freq

The PWM frequency is rounded to the nearest one of:

* 11.0 Hz
* 14.7 Hz
* 22.1 Hz
* 29.4 Hz
* 35.3 Hz
* 44.1 Hz
* 58.8 Hz
* 88.2 Hz
* 1.4 kHz
* 22.5 kHz

Notes
-----

The temperature inputs no longer need to be read periodically from userspace in
order for the automatic pwm algorithm to run.  This was the case for earlier
versions of the driver.
