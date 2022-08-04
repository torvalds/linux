.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver corsair-psu
=========================

Supported devices:

* Corsair Power Supplies

  Corsair HX550i

  Corsair HX650i

  Corsair HX750i

  Corsair HX850i

  Corsair HX1000i

  Corsair HX1200i

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
power usage and 4 sensors for current levels and addtional non-sensor information
like uptimes.

Sysfs entries
-------------

=======================	========================================================
curr1_input		Total current usage
curr2_input		Current on the 12v psu rail
curr3_input		Current on the 5v psu rail
curr4_input		Current on the 3.3v psu rail
fan1_input		RPM of psu fan
in0_input		Voltage of the psu ac input
in1_input		Voltage of the 12v psu rail
in2_input		Voltage of the 5v psu rail
in3_input		Voltage of the 3.3 psu rail
power1_input		Total power usage
power2_input		Power usage of the 12v psu rail
power3_input		Power usage of the 5v psu rail
power4_input		Power usage of the 3.3v psu rail
temp1_input		Temperature of the psu vrm component
temp2_input		Temperature of the psu case
=======================	========================================================

Usage Notes
-----------

It is an USB HID device, so it is auto-detected and supports hot-swapping.

Flickering values in the rail voltage levels can be an indicator for a failing
PSU. The driver also provides some additional useful values via debugfs, which
do not fit into the hwmon class.

Debugfs entries
---------------

=======================	========================================================
uptime			Current uptime of the psu
uptime_total		Total uptime of the psu
vendor			Vendor name of the psu
product			Product name of the psu
=======================	========================================================
