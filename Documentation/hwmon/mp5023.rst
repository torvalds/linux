.. SPDX-License-Identifier: GPL-2.0

Kernel driver mp5023
====================

Supported chips:

  * MPS MP5023

    Prefix: 'mp5023'

  * Datasheet

    Publicly available at the MPS website : https://www.monolithicpower.com/en/mp5023.html

Author:

	Howard Chiu <howard.chiu@quantatw.com>

Description
-----------

This driver implements support for Monolithic Power Systems, Inc. (MPS)
MP5023 Hot-Swap Controller.

Device complaint with:

- PMBus rev 1.3 interface.

Device supports direct format for reading input voltage, output voltage,
output current, input power and temperature.

The driver exports the following attributes via the 'sysfs' files
for input voltage:

**in1_input**

**in1_label**

**in1_max**

**in1_max_alarm**

**in1_min**

**in1_min_alarm**

The driver provides the following attributes for output voltage:

**in2_input**

**in2_label**

**in2_alarm**

The driver provides the following attributes for output current:

**curr1_input**

**curr1_label**

**curr1_alarm**

**curr1_max**

The driver provides the following attributes for input power:

**power1_input**

**power1_label**

**power1_alarm**

The driver provides the following attributes for temperature:

**temp1_input**

**temp1_max**

**temp1_max_alarm**

**temp1_crit**

**temp1_crit_alarm**
