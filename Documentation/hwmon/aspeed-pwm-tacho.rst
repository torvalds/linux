Kernel driver aspeed-pwm-tacho
==============================

Supported chips:
	ASPEED AST2400/2500

Authors:
	<jaghu@google.com>

Description:
------------
This driver implements support for ASPEED AST2400/2500 PWM and Fan Tacho
controller. The PWM controller supports upto 8 PWM outputs. The Fan tacho
controller supports up to 16 tachometer inputs.

The driver provides the following sensor accesses in sysfs:

=============== ======= =====================================================
fanX_input	ro	provide current fan rotation value in RPM as reported
			by the fan to the device.

pwmX		rw	get or set PWM fan control value. This is an integer
			value between 0(off) and 255(full speed).
=============== ======= =====================================================
