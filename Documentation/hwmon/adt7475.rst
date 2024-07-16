Kernel driver adt7475
=====================

Supported chips:

  * Analog Devices ADT7473

    Prefix: 'adt7473'

    Addresses scanned: I2C 0x2C, 0x2D, 0x2E

    Datasheet: Publicly available at the On Semiconductors website

  * Analog Devices ADT7475

    Prefix: 'adt7475'

    Addresses scanned: I2C 0x2E

    Datasheet: Publicly available at the On Semiconductors website

  * Analog Devices ADT7476

    Prefix: 'adt7476'

    Addresses scanned: I2C 0x2C, 0x2D, 0x2E

    Datasheet: Publicly available at the On Semiconductors website

  * Analog Devices ADT7490

    Prefix: 'adt7490'

    Addresses scanned: I2C 0x2C, 0x2D, 0x2E

    Datasheet: Publicly available at the On Semiconductors website

Authors:
	- Jordan Crouse
	- Hans de Goede
	- Darrick J. Wong (documentation)
	- Jean Delvare


Description
-----------

This driver implements support for the Analog Devices ADT7473, ADT7475,
ADT7476 and ADT7490 chip family. The ADT7473 and ADT7475 differ only in
minor details. The ADT7476 has additional features, including extra voltage
measurement inputs and VID support. The ADT7490 also has additional
features, including extra voltage measurement inputs and PECI support. All
the supported chips will be collectively designed by the name "ADT747x" in
the rest of this document.

The ADT747x uses the 2-wire interface compatible with the SMBus 2.0
specification. Using an analog to digital converter it measures three (3)
temperatures and two (2) or more voltages. It has four (4) 16-bit counters
for measuring fan speed. There are three (3) PWM outputs that can be used
to control fan speed.

A sophisticated control system for the PWM outputs is designed into the
ADT747x that allows fan speed to be adjusted automatically based on any of the
three temperature sensors. Each PWM output is individually adjustable and
programmable. Once configured, the ADT747x will adjust the PWM outputs in
response to the measured temperatures without further host intervention.
This feature can also be disabled for manual control of the PWM's.

Each of the measured inputs (voltage, temperature, fan speed) has
corresponding high/low limit values. The ADT747x will signal an ALARM if
any measured value exceeds either limit.

The ADT747x samples all inputs continuously. The driver will not read
the registers more often than once every other second. Further,
configuration data is only read once per minute.

Chip Differences Summary
------------------------

ADT7473:
  * 2 voltage inputs
  * system acoustics optimizations (not implemented)

ADT7475:
  * 2 voltage inputs

ADT7476:
  * 5 voltage inputs
  * VID support

ADT7490:
  * 6 voltage inputs
  * 1 Imon input (not implemented)
  * PECI support (not implemented)
  * 2 GPIO pins (not implemented)
  * system acoustics optimizations (not implemented)

Sysfs Mapping
-------------

==== =========== =========== ========= ==========
in   ADT7490     ADT7476     ADT7475   ADT7473
==== =========== =========== ========= ==========
in0  2.5VIN (22) 2.5VIN (22) -         -
in1  VCCP   (23) VCCP   (23) VCCP (14) VCCP (14)
in2  VCC    (4)  VCC    (4)  VCC  (4)  VCC  (3)
in3  5VIN   (20) 5VIN   (20)
in4  12VIN  (21) 12VIN  (21)
in5  VTT    (8)
==== =========== =========== ========= ==========

Special Features
----------------

The ADT747x has a 10-bit ADC and can therefore measure temperatures
with a resolution of 0.25 degree Celsius. Temperature readings can be
configured either for two's complement format or "Offset 64" format,
wherein 64 is subtracted from the raw value to get the temperature value.

The datasheet is very detailed and describes a procedure for determining
an optimal configuration for the automatic PWM control.

Fan Speed Control
-----------------

The driver exposes two trip points per PWM channel.

- point1: Set the PWM speed at the lower temperature bound
- point2: Set the PWM speed at the higher temperature bound

The ADT747x will scale the PWM linearly between the lower and higher PWM
speed when the temperature is between the two temperature boundaries.
Temperature boundaries are associated to temperature channels rather than
PWM outputs, and a given PWM output can be controlled by several temperature
channels. As a result, the ADT747x may compute more than one PWM value
for a channel at a given time, in which case the maximum value (fastest
fan speed) is applied. PWM values range from 0 (off) to 255 (full speed).

Fan speed may be set to maximum when the temperature sensor associated with
the PWM control exceeds temp#_max.

At Tmin - hysteresis the PWM output can either be off (0% duty cycle) or at the
minimum (i.e. auto_point1_pwm). This behaviour can be configured using the
`pwm[1-*]_stall_disable sysfs attribute`. A value of 0 means the fans will shut
off. A value of 1 means the fans will run at auto_point1_pwm.

The responsiveness of the ADT747x to temperature changes can be configured.
This allows smoothing of the fan speed transition. To set the transition time
set the value in ms in the `temp[1-*]_smoothing` sysfs attribute.

Notes
-----

The nVidia binary driver presents an ADT7473 chip via an on-card i2c bus.
Unfortunately, they fail to set the i2c adapter class, so this driver may
fail to find the chip until the nvidia driver is patched.
