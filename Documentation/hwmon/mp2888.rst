.. SPDX-License-Identifier: GPL-2.0

Kernel driver mp2888
====================

Supported chips:

  * MPS MP12254

    Prefix: 'mp2888'

Author:

	Vadim Pasternak <vadimp@nvidia.com>

Description
-----------

This driver implements support for Monolithic Power Systems, Inc. (MPS)
vendor dual-loop, digital, multi-phase controller MP2888.

This device: supports:

- One power rail.
- Programmable Multi-Phase up to 10 Phases.
- PWM-VID Interface
- One pages 0 for telemetry.
- Programmable pins for PMBus Address.
- Built-In EEPROM to Store Custom Configurations.

Device complaint with:

- PMBus rev 1.3 interface.

Device supports direct format for reading output current, output voltage,
input and output power and temperature.
Device supports linear format for reading input voltage and input power.

The driver provides the next attributes for the current:

- for current out input and maximum alarm;
- for phase current: input and label.

The driver exports the following attributes via the 'sysfs' files, where:

- 'n' is number of configured phases (from 1 to 10);
- index 1 for "iout";
- indexes 2 ... 1 + n for phases.

**curr[1-{1+n}]_input**

**curr[1-{1+n}]_label**

**curr1_max**

**curr1_max_alarm**

The driver provides the next attributes for the voltage:

- for voltage in: input, low and high critical thresholds, low and high
  critical alarms;
- for voltage out: input and high alarm;

The driver exports the following attributes via the 'sysfs' files, where

**in1_crit**

**in1_crit_alarm**

**in1_input**

**in1_label**

**in1_min**

**in1_min_alarm**

**in2_alarm**

**in2_input**

**in2_label**

The driver provides the next attributes for the power:

- for power in alarm and input.
- for power out: cap, cap alarm an input.

The driver exports the following attributes via the 'sysfs' files, where
- indexes 1 for "pin";
- indexes 2 for "pout";

**power1_alarm**

**power1_input**

**power1_label**

**power2_input**

**power2_label**

**power2_max**

**power2_max_alarm**

The driver provides the next attributes for the temperature:

**temp1_input**

**temp1_max**

**temp1_max_alarm**
