.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver aquacomputer-d5next
=================================

Supported devices:

* Aquacomputer D5 Next watercooling pump

Author: Aleksa Savic

Description
-----------

This driver exposes hardware sensors of the Aquacomputer D5 Next watercooling
pump, which communicates through a proprietary USB HID protocol.

Available sensors are pump and fan speed, power, voltage and current, as
well as coolant temperature. Also available through debugfs are the serial
number, firmware version and power-on count.

Attaching a fan is optional and allows it to be controlled using temperature
curves directly from the pump. If it's not connected, the fan-related sensors
will report zeroes.

The pump can be configured either through software or via its physical
interface. Configuring the pump through this driver is not implemented, as it
seems to require sending it a complete configuration. That includes addressable
RGB LEDs, for which there is no standard sysfs interface. Thus, that task is
better suited for userspace tools.

Usage notes
-----------

The pump communicates via HID reports. The driver is loaded automatically by
the kernel and supports hotswapping.

Sysfs entries
-------------

============ =============================================
temp1_input  Coolant temperature (in millidegrees Celsius)
fan1_input   Pump speed (in RPM)
fan2_input   Fan speed (in RPM)
power1_input Pump power (in micro Watts)
power2_input Fan power (in micro Watts)
in0_input    Pump voltage (in milli Volts)
in1_input    Fan voltage (in milli Volts)
in2_input    +5V rail voltage (in milli Volts)
curr1_input  Pump current (in milli Amperes)
curr2_input  Fan current (in milli Amperes)
============ =============================================

Debugfs entries
---------------

================ ===============================================
serial_number    Serial number of the pump
firmware_version Version of installed firmware
power_cycles     Count of how many times the pump was powered on
================ ===============================================
