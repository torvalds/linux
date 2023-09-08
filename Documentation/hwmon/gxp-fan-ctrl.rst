.. SPDX-License-Identifier: GPL-2.0-only

Kernel driver gxp-fan-ctrl
==========================

Supported chips:

  * HPE GXP SOC

Author: Nick Hawkins <nick.hawkins@hpe.com>


Description
-----------

gxp-fan-ctrl is a driver which provides fan control for the hpe gxp soc.
The driver allows the gathering of fan status and the use of fan
PWM control.


Sysfs attributes
----------------

======================= ===========================================================
pwm[0-7]		Fan 0 to 7 respective PWM value (0-255)
fan[0-7]_fault		Fan 0 to 7 respective fault status: 1 fail, 0 ok
fan[0-7]_enable         Fan 0 to 7 respective enabled status: 1 enabled, 0 disabled
======================= ===========================================================
