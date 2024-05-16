.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver aspeed-g6-pwm-tach
=================================

Supported chips:
	ASPEED AST2600

Authors:
	<billy_tsai@aspeedtech.com>

Description:
------------
This driver implements support for ASPEED AST2600 Fan Tacho controller.
The controller supports up to 16 tachometer inputs.

The driver provides the following sensor accesses in sysfs:

=============== ======= ======================================================
fanX_input	ro	provide current fan rotation value in RPM as reported
			by the fan to the device.
fanX_div	rw	Fan divisor: Supported value are power of 4 (1, 4, 16
                        64, ... 4194304)
                        The larger divisor, the less rpm accuracy and the less
                        affected by fan signal glitch.
=============== ======= ======================================================
