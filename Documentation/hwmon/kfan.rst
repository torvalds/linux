.. SPDX-License-Identifier: GPL-2.0

Kernel driver kfan
==================

Supported chips:

  * KEBA fan controller (IP core in FPGA)

    Prefix: 'kfan'

Authors:

	Gerhard Engleder <eg@keba.com>
	Petar Bojanic <boja@keba.com>

Description
-----------

The KEBA fan controller is an IP core for FPGAs, which monitors the health
and controls the speed of a fan. The fan is typically used to cool the CPU
and the whole device. E.g., the CP500 FPGA includes this IP core to monitor
and control the fan of PLCs and the corresponding cp500 driver creates an
auxiliary device for the kfan driver.

This driver provides information about the fan health to user space.
The user space shall be informed if the fan is removed or blocked.
Additionally, the speed in RPM is reported for fans with tacho signal.

For fan control PWM is supported. For PWM 255 equals 100%. None-regulable
fans can be turned on with PWM 255 and turned off with PWM 0.

====================== ==== ===================================================
Attribute              R/W  Contents
====================== ==== ===================================================
fan1_fault             R    Fan fault
fan1_input             R    Fan tachometer input (in RPM)
pwm1                   RW   Fan target duty cycle (0..255)
====================== ==== ===================================================
