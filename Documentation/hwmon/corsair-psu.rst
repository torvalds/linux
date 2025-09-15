.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver corsair-psu
=========================

Supported devices:

* Corsair Power Supplies

  Corsair HX550i

  Corsair HX650i

  Corsair HX750i

  Corsair HX850i

  Corsair HX1000i (Legacy and Series 2023)

  Corsair HX1200i (Legacy, Series 2023 and Series 2025)

  Corsair HX1500i (Legacy and Series 2023)

  Corsair RM550i

  Corsair RM650i

  Corsair RM750i

  Corsair RM850i

  Corsair RM1000i

Author: Wilken Gottwalt

Description
-----------

This driver implements the sysfs interface for the Corsair PSUs with a HID protocol
interface of the HXi and RMi series.
These power supplies provide access to a micro-controller with 2 attached
temperature sensors, 1 fan rpm sensor, 4 sensors for volt levels, 4 sensors for
power usage and 4 sensors for current levels and additional non-sensor information
like uptimes.

Sysfs entries
-------------

=======================	========================================================
curr1_input		Total current usage
curr2_input		Current on the 12v psu rail
curr2_crit		Current max critical value on the 12v psu rail
curr3_input		Current on the 5v psu rail
curr3_crit		Current max critical value on the 5v psu rail
curr4_input		Current on the 3.3v psu rail
curr4_crit		Current max critical value on the 3.3v psu rail
fan1_input		RPM of psu fan
in0_input		Voltage of the psu ac input
in1_input		Voltage of the 12v psu rail
in1_crit		Voltage max critical value on the 12v psu rail
in1_lcrit		Voltage min critical value on the 12v psu rail
in2_input		Voltage of the 5v psu rail
in2_crit		Voltage max critical value on the 5v psu rail
in2_lcrit		Voltage min critical value on the 5v psu rail
in3_input		Voltage of the 3.3v psu rail
in3_crit		Voltage max critical value on the 3.3v psu rail
in3_lcrit		Voltage min critical value on the 3.3v psu rail
power1_input		Total power usage
power2_input		Power usage of the 12v psu rail
power3_input		Power usage of the 5v psu rail
power4_input		Power usage of the 3.3v psu rail
pwm1			PWM value, read only
pwm1_enable		PWM mode, read only
temp1_input		Temperature of the psu vrm component
temp1_crit		Temperature max cirtical value of the psu vrm component
temp2_input		Temperature of the psu case
temp2_crit		Temperature max critical value of psu case
=======================	========================================================

Usage Notes
-----------

It is an USB HID device, so it is auto-detected, supports hot-swapping and
several devices at once.

Flickering values in the rail voltage levels can be an indicator for a failing
PSU. Accordingly to the default automatic fan speed plan the fan starts at about
30% of the wattage rating. If this does not happen, a fan failure is likely. The
driver also provides some additional useful values via debugfs, which do not fit
into the hwmon class.

Debugfs entries
---------------

=======================	========================================================
ocpmode                 Single or multi rail mode of the PCIe power connectors
product                 Product name of the psu
uptime			Session uptime of the psu
uptime_total		Total uptime of the psu
vendor			Vendor name of the psu
=======================	========================================================
