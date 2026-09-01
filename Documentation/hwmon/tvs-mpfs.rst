.. SPDX-License-Identifier: GPL-2.0

Kernel driver tvs-mpfs
======================

Supported chips:

  * PolarFire SoC

Authors:

   - Conor Dooley <conor.dooley@microchip.com>
   - Lars Randers <lranders@mail.dk>

Description
-----------

This driver implements support for the temperature and voltage sensors on
PolarFire SoC. The temperature reports how hot the die is, and the voltages are
the SoC's 1.05, 1.8 and 2.5 volt rails respectively.


Usage Notes
-----------

update_interval has a permitted range of 0 to 8 milliseconds.

Temperatures are read in millidegrees Celsius, but the hardware measures in
degrees Kelvin, storing the result as 11.4 fixed point data, for a maximum
value of 2047.9375 degrees Kelvin.

Voltages are read in millivolts. The hardware measures in millivolts, storing
the value as 12.3 fixed point data, for a maximum of 4095.875 millivolts.
The minimum value reportable by the driver is 0 volts, although the hardware
is capable of measuring negative values.

Sysfs entries
-------------

The following attributes are supported. update_interval is read-write, as are
the enables. All other attributes are read only.

======================= ====================================================
temp1_label		Fixed name for channel.
temp1_input		Measured temperature for channel.
temp1_enable		Enable/disable for channel.

in[0-2]_label		Fixed name for channel.
in[0-2]_input		Measured voltage for channel.
in[0-2]_enable		Enable/disable for channel.

update_interval		The interval at which the chip will update readings.
======================= ====================================================
