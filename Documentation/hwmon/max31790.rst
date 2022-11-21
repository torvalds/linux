Kernel driver max31790
======================

Supported chips:

  * Maxim MAX31790

    Prefix: 'max31790'

    Addresses scanned: -

    Datasheet: https://pdfserv.maximintegrated.com/en/ds/MAX31790.pdf

Author: Il Han <corone.il.han@gmail.com>


Description
-----------

This driver implements support for the Maxim MAX31790 chip.

The MAX31790 controls the speeds of up to six fans using six independent
PWM outputs. The desired fan speeds (or PWM duty cycles) are written
through the I2C interface. The outputs drive "4-wire" fans directly,
or can be used to modulate the fan's power terminals using an external
pass transistor.

Tachometer inputs monitor fan tachometer logic outputs for precise (+/-1%)
monitoring and control of fan RPM as well as detection of fan failure.
Six pins are dedicated tachometer inputs. Any of the six PWM outputs can
also be configured to serve as tachometer inputs.


Sysfs entries
-------------

================== === =======================================================
fan[1-12]_input    RO  fan tachometer speed in RPM
fan[1-12]_fault    RO  fan experienced fault
fan[1-6]_target    RW  desired fan speed in RPM
pwm[1-6]_enable    RW  regulator mode, 0=disabled (duty cycle=0%), 1=manual mode, 2=rpm mode
pwm[1-6]           RW  read: current pwm duty cycle,
                       write: target pwm duty cycle (0-255)
================== === =======================================================
