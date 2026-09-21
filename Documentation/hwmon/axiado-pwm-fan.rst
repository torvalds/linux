.. SPDX-License-Identifier: GPL-2.0

Kernel driver axiado-pwm-fan
============================

Supported chips:

  * Axiado AX3000
  * Axiado AX3005

    Datasheet: Not publicly available

Author:

  * Petar Stepanovic <pstepanovic@axiado.com>

Description
-----------

The Axiado AX3000 and AX3005 PWM fan controllers measure the rotational
speed of one fan using a hardware tachometer block. Fan speed is controlled
through a PWM signal supplied by an external PWM controller.

The number of tachometer pulses generated per fan revolution is configured
through the ``pulses-per-revolution`` devicetree property. If the property
is not specified, the driver uses two pulses per revolution.

Sysfs attributes
----------------

The driver provides the following standard hwmon attributes:

=============== ====== =====================================================
fan1_input      RO     Fan speed in revolutions per minute (RPM).

pwm1            RW     Relative PWM control value from 0 to 255. A value of
                       255 selects the maximum PWM duty cycle.
=============== ====== =====================================================
