.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver aquacomputer-d5next
=================================

Supported devices:

* Aquacomputer Aquaero 5/6 fan controllers
* Aquacomputer D5 Next watercooling pump
* Aquacomputer Farbwerk RGB controller
* Aquacomputer Farbwerk 360 RGB controller
* Aquacomputer Octo fan controller
* Aquacomputer Quadro fan controller
* Aquacomputer High Flow Next sensor
* Aquacomputer Aquastream XT watercooling pump
* Aquacomputer Aquastream Ultimate watercooling pump
* Aquacomputer Poweradjust 3 fan controller

Author: Aleksa Savic

Description
-----------

This driver exposes hardware sensors of listed Aquacomputer devices, which
communicate through proprietary USB HID protocols.

The Aquaero devices expose eight physical, eight virtual and four calculated
virtual temperature sensors, as well as two flow sensors. The fans expose their
speed (in RPM), power, voltage and current. Temperature offsets and fan speeds
can be controlled.

For the D5 Next pump, available sensors are pump and fan speed, power, voltage
and current, as well as coolant temperature and eight virtual temp sensors. Also
available through debugfs are the serial number, firmware version and power-on
count. Attaching a fan to it is optional and allows it to be controlled using
temperature curves directly from the pump. If it's not connected, the fan-related
sensors will report zeroes.

The pump can be configured either through software or via its physical
interface. Configuring the pump through this driver is not implemented, as it
seems to require sending it a complete configuration. That includes addressable
RGB LEDs, for which there is no standard sysfs interface. Thus, that task is
better suited for userspace tools.

The Octo exposes four physical and sixteen virtual temperature sensors, as well as
eight PWM controllable fans, along with their speed (in RPM), power, voltage and
current.

The Quadro exposes four physical and sixteen virtual temperature sensors, a flow
sensor and four PWM controllable fans, along with their speed (in RPM), power,
voltage and current. Flow sensor pulses are also available.

The Farbwerk and Farbwerk 360 expose four temperature sensors. Additionally,
sixteen virtual temperature sensors of the Farbwerk 360 are exposed.

The High Flow Next exposes +5V voltages, water quality, conductivity and flow readings.
A temperature sensor can be connected to it, in which case it provides its reading
and an estimation of the dissipated/absorbed power in the liquid cooling loop.

The Aquastream XT pump exposes temperature readings for the coolant, external sensor
and fan IC. It also exposes pump and fan speeds (in RPM), voltages, as well as pump
current.

The Aquastream Ultimate pump exposes coolant temp and an external temp sensor, along
with speed, power, voltage and current of both the pump and optionally connected fan.
It also exposes pressure and flow speed readings.

The Poweradjust 3 controller exposes a single external temperature sensor.

Depending on the device, not all sysfs and debugfs entries will be available.
Writing to virtual temperature sensors is not currently supported.

Usage notes
-----------

The devices communicate via HID reports. The driver is loaded automatically by
the kernel and supports hotswapping.

Sysfs entries
-------------

================ ==============================================================
temp[1-20]_input Physical/virtual temperature sensors (in millidegrees Celsius)
temp[1-8]_offset Temperature sensor correction offset (in millidegrees Celsius)
fan[1-8]_input   Pump/fan speed (in RPM) / Flow speed (in dL/h)
fan5_pulses      Quadro flow sensor pulses
power[1-8]_input Pump/fan power (in micro Watts)
in[0-7]_input    Pump/fan voltage (in milli Volts)
curr[1-8]_input  Pump/fan current (in milli Amperes)
pwm[1-8]         Fan PWM (0 - 255)
================ ==============================================================

Debugfs entries
---------------

================ =================================================
serial_number    Serial number of the device
firmware_version Version of installed firmware
power_cycles     Count of how many times the device was powered on
================ =================================================
