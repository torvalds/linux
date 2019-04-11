Kernel driver pwm-fan
=====================

This driver enables the use of a PWM module to drive a fan. It uses the
generic PWM interface thus it is hardware independent. It can be used on
many SoCs, as long as the SoC supplies a PWM line driver that exposes
the generic PWM API.

Author: Kamil Debski <k.debski@samsung.com>

Description
-----------

The driver implements a simple interface for driving a fan connected to
a PWM output. It uses the generic PWM interface, thus it can be used with
a range of SoCs. The driver exposes the fan to the user space through
the hwmon's sysfs interface.

The fan rotation speed returned via the optional 'fan1_input' is extrapolated
from the sampled interrupts from the tachometer signal within 1 second.
