.. SPDX-License-Identifier: GPL-2.0

Kernel driver mp5926
====================

Supported chips:

  * MPS mp5926

    Prefix: 'mp5926'

  * Datasheet
    https://www.monolithicpower.com/en/

Author:

	Yuxi Wang <Yuxi.Wang@monolithicpower.com>

Description
-----------

This driver implements support for Monolithic Power Systems, Inc. (MPS)
MP5926 Hot-Swap Controller.

Device compliant with:

- PMBus rev 1.3 interface.

The driver exports the following attributes via the 'sysfs' files
for input voltage:

**in1_input**

**in1_label**

**in1_crit**

**in1_crit_alarm**

The driver provides the following attributes for output voltage:

**in2_input**

**in2_label**

**in2_lcrit**

**in2_lcrit_alarm**

**in2_rated_max**

**in2_rated_min**

The driver provides the following attributes for input current:

**curr1_input**

**curr1_label**

**curr1_max**

**curr1_max_alarm**

The driver provides the following attributes for output current:

**curr2_input**

**curr2_label**

The driver provides the following attributes for input power:

**power1_input**

**power1_label**

The driver provides the following attributes for output power:

**power2_input**

**power2_label**

The driver provides the following attributes for temperature:

**temp1_input**

**temp1_crit**

**temp1_crit_alarm**

**temp1_max**

**temp1_max_alarm**
