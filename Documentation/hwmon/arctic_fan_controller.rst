.. SPDX-License-Identifier: GPL-2.0-or-later OR BSD-2-Clause

Kernel driver arctic_fan_controller
=====================================

Supported devices:

* ARCTIC Fan Controller (USB HID, VID 0x3904, PID 0xF001)

Author: Aureo Serrano de Souza <aureo.serrano@arctic.de>

Description
-----------

This driver provides hwmon support for the ARCTIC Fan Controller, a USB
Custom HID device with 10 fan channels. The device sends IN reports about
once per second containing current RPM values (bytes 11-30, 10 x uint16 LE).
Fan speed control is manual-only: the device does not change PWM
autonomously; it only applies a new duty cycle when it receives an OUT
report from the host.

After the device applies an OUT report, it sends back a 2-byte ACK IN
report (Report ID 0x02, byte 1 = 0x00 on success) confirming the command
was applied.

Usage notes
-----------

Since it is a USB device, hotplug is supported. The device is autodetected.

The device does not support GET_REPORT, so the driver cannot read back the
current hardware PWM state at probe time. Each OUT report carries all 10
channels, so pwm[1-10] is a host cache. It starts at the MCU factory
default of 40% (sysfs 102). After a successful write, the cache reflects
that value. Users should set all channels to the desired values before
relying on the cached state.

On system suspend, the device may lose power and reset PWM to the factory
default. The driver restores the cache to 40% on resume. If the device
kept power across suspend, userspace should re-apply the desired duty
cycles.

Sysfs entries
-------------

================ ==============================================================
fan[1-10]_input  Fan speed in RPM (read-only). Updated from IN reports at ~1 Hz.
pwm[1-10]        PWM duty cycle (0-255). Write: sends an OUT report setting the
                 duty cycle (scaled from 0-255 to 0-100% for the device);
                 the cached value is updated only after the device ACKs the
                 command with a success status. Read: returns the last
                 successfully written value; initialized to 102 (40%) at
                 driver load and after resume (MCU factory default).
================ ==============================================================
