.. SPDX-License-Identifier: GPL-2.0

Kernel driver xdpe152
=====================

Supported chips:

  * Infineon XDPE152C4

    Prefix: 'xdpe152c4'

  * Infineon XDPE15284

    Prefix: 'xdpe15284'

Authors:

    Greg Schwendimann <greg.schwendimann@infineon.com>

Description
-----------

This driver implements support for Infineon Digital Multi-phase Controller
XDPE152C4 and XDPE15284 dual loop voltage regulators.
The devices are compliant with:

- Intel VR13, VR13HC and VR14 rev 1.86
  converter specification.
- Intel SVID rev 1.93. protocol.
- PMBus rev 1.3.1 interface.

Devices support linear format for reading input and output voltage, input
and output current, input and output power and temperature.

Devices support two pages for telemetry.

The driver provides for current: input, maximum and critical thresholds
and maximum and critical alarms. Low Critical thresholds and Low critical alarm are
supported only for current output.
The driver exports the following attributes for via the sysfs files, where
indexes 1, 2 are for "iin" and 3, 4 for "iout":

**curr[1-4]_crit**

**curr[1-4]_crit_alarm**

**curr[1-4]_input**

**curr[1-4]_label**

**curr[1-4]_max**

**curr[1-4]_max_alarm**

**curr[3-4]_lcrit**

**curr[3-4]_lcrit_alarm**

**curr[3-4]_rated_max**

The driver provides for voltage: input, critical and low critical thresholds
and critical and low critical alarms.
The driver exports the following attributes for via the sysfs files, where
indexes 1, 2 are for "vin" and 3, 4 for "vout":

**in[1-4]_min**

**in[1-4]_crit**

**in[1-4_crit_alarm**

**in[1-4]_input**

**in[1-4]_label**

**in[1-4]_max**

**in[1-4]_max_alarm**

**in[1-4]_min**

**in[1-4]_min_alarm**

**in[3-4]_lcrit**

**in[3-4]_lcrit_alarm**

**in[3-4]_rated_max**

**in[3-4]_rated_min**

The driver provides for power: input and alarms.
The driver exports the following attributes for via the sysfs files, where
indexes 1, 2 are for "pin" and 3, 4 for "pout":

**power[1-2]_alarm**

**power[1-4]_input**

**power[1-4]_label**

**power[1-4]_max**

**power[1-4]_rated_max**

The driver provides for temperature: input, maximum and critical thresholds
and maximum and critical alarms.
The driver exports the following attributes for via the sysfs files:

**temp[1-2]_crit**

**temp[1-2]_crit_alarm**

**temp[1-2]_input**

**temp[1-2]_max**

**temp[1-2]_max_alarm**
