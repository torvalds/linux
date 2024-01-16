Kernel driver adt7462
=====================

Supported chips:

  * Analog Devices ADT7462

    Prefix: 'adt7462'

    Addresses scanned: I2C 0x58, 0x5C

    Datasheet: Publicly available at the Analog Devices website

Author: Darrick J. Wong

Description
-----------

This driver implements support for the Analog Devices ADT7462 chip family.

This chip is a bit of a beast.  It has 8 counters for measuring fan speed.  It
can also measure 13 voltages or 4 temperatures, or various combinations of the
two.  See the chip documentation for more details about the exact set of
configurations.  This driver does not allow one to configure the chip; that is
left to the system designer.

A sophisticated control system for the PWM outputs is designed into the ADT7462
that allows fan speed to be adjusted automatically based on any of the three
temperature sensors. Each PWM output is individually adjustable and
programmable. Once configured, the ADT7462 will adjust the PWM outputs in
response to the measured temperatures without further host intervention.  This
feature can also be disabled for manual control of the PWM's.

Each of the measured inputs (voltage, temperature, fan speed) has
corresponding high/low limit values. The ADT7462 will signal an ALARM if
any measured value exceeds either limit.

The ADT7462 samples all inputs continuously. The driver will not read
the registers more often than once every other second. Further,
configuration data is only read once per minute.

Special Features
----------------

The ADT7462 have a 10-bit ADC and can therefore measure temperatures
with 0.25 degC resolution.

The Analog Devices datasheet is very detailed and describes a procedure for
determining an optimal configuration for the automatic PWM control.

The driver will report sensor labels when it is able to determine that
information from the configuration registers.

Configuration Notes
-------------------

Besides standard interfaces driver adds the following:

* PWM Control

* pwm#_auto_point1_pwm and temp#_auto_point1_temp and
* pwm#_auto_point2_pwm and temp#_auto_point2_temp -

  - point1: Set the pwm speed at a lower temperature bound.
  - point2: Set the pwm speed at a higher temperature bound.

The ADT7462 will scale the pwm between the lower and higher pwm speed when
the temperature is between the two temperature boundaries.  PWM values range
from 0 (off) to 255 (full speed).  Fan speed will be set to maximum when the
temperature sensor associated with the PWM control exceeds temp#_max.
