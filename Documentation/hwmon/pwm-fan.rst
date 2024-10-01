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

The driver provides the following sensor accesses in sysfs:

=============== ======= =======================================================
fan1_input	ro	fan tachometer speed in RPM
pwm1_enable	rw	keep enable mode, defines behaviour when pwm1=0
			0 -> disable pwm and regulator
			1 -> enable pwm; if pwm==0, disable pwm, keep regulator enabled
			2 -> enable pwm; if pwm==0, keep pwm and regulator enabled
			3 -> enable pwm; if pwm==0, disable pwm and regulator
pwm1		rw	relative speed (0-255), 255=max. speed.
=============== ======= =======================================================
