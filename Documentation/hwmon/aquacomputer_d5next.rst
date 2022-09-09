.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver aquacomputer-d5next
=================================

Supported devices:

* Aquacomputer D5 Next watercooling pump
* Aquacomputer Farbwerk RGB controller
* Aquacomputer Farbwerk 360 RGB controller
* Aquacomputer Octo fan controller
* Aquacomputer Quadro fan controller

Author: Aleksa Savic

Description
-----------

This driver exposes hardware sensors of listed Aquacomputer devices, which
communicate through proprietary USB HID protocols.

For the D5 Next pump, available sensors are pump and fan speed, power, voltage
and current, as well as coolant temperature. Also available through debugfs are
the serial number, firmware version and power-on count. Attaching a fan to it is
optional and allows it to be controlled using temperature curves directly from the
pump. If it's not connected, the fan-related sensors will report zeroes.

The pump can be configured either through software or via its physical
interface. Configuring the pump through this driver is not implemented, as it
seems to require sending it a complete configuration. That includes addressable
RGB LEDs, for which there is no standard sysfs interface. Thus, that task is
better suited for userspace tools.

The Octo exposes four temperature sensors and eight PWM controllable fans, along
with their speed (in RPM), power, voltage and current.

The Quadro exposes four temperature sensors, a flow sensor and four PWM controllable
fans, along with their speed (in RPM), power, voltage and current.

The Farbwerk and Farbwerk 360 expose four temperature sensors. Depending on the device,
not all sysfs and debugfs entries will be available.

Usage notes
-----------

The devices communicate via HID reports. The driver is loaded automatically by
the kernel and supports hotswapping.

Sysfs entries
-------------

================ ==============================================
temp[1-4]_input  Temperature sensors (in millidegrees Celsius)
fan[1-8]_input   Pump/fan speed (in RPM) / Flow speed (in dL/h)
power[1-8]_input Pump/fan power (in micro Watts)
in[0-7]_input    Pump/fan voltage (in milli Volts)
curr[1-8]_input  Pump/fan current (in milli Amperes)
pwm[1-8]         Fan PWM (0 - 255)
================ ==============================================

Debugfs entries
---------------

================ =================================================
serial_number    Serial number of the device
firmware_version Version of installed firmware
power_cycles     Count of how many times the device was powered on
================ =================================================
