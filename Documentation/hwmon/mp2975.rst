.. SPDX-License-Identifier: GPL-2.0

Kernel driver mp2975
====================

Supported chips:

  * MPS MP12254

    Prefix: 'mp2975'

Author:

	Vadim Pasternak <vadimp@nvidia.com>

Description
-----------

This driver implements support for Monolithic Power Systems, Inc. (MPS)
vendor dual-loop, digital, multi-phase controller MP2975.

This device:

- Supports up to two power rail.
- Provides 8 pulse-width modulations (PWMs), and can be configured up
  to 8-phase operation for rail 1 and up to 4-phase operation for rail
  2.
- Supports two pages 0 and 1 for telemetry and also pages 2 and 3 for
  configuration.
- Can configured VOUT readout in direct or VID format and allows
  setting of different formats on rails 1 and 2. For VID the following
  protocols are available: VR13 mode with 5-mV DAC; VR13 mode with
  10-mV DAC, IMVP9 mode with 5-mV DAC.

Device supports:

- SVID interface.
- AVSBus interface.

Device complaint with:

- PMBus rev 1.3 interface.

Device supports direct format for reading output current, output voltage,
input and output power and temperature.
Device supports linear format for reading input voltage and input power.
Device supports VID and direct formats for reading output voltage.
The below VID modes are supported: VR12, VR13, IMVP9.

The driver provides the next attributes for the current:

- for current in: input, maximum alarm;
- for current out input, maximum alarm and highest values;
- for phase current: input and label.
  attributes.

The driver exports the following attributes via the 'sysfs' files, where

- 'n' is number of telemetry pages (from 1 to 2);
- 'k' is number of configured phases (from 1 to 8);
- indexes 1, 1*n for "iin";
- indexes n+1, n+2 for "iout";
- indexes 2*n+1 ... 2*n + k for phases.

**curr[1-{2n}]_alarm**

**curr[{n+1}-{n+2}]_highest**

**curr[1-{2n+k}]_input**

**curr[1-{2n+k}]_label**

The driver provides the next attributes for the voltage:

- for voltage in: input, high critical threshold, high critical alarm, all only
  from page 0;
- for voltage out: input, low and high critical thresholds, low and high
  critical alarms, from pages 0 and 1;

The driver exports the following attributes via the 'sysfs' files, where

- 'n' is number of telemetry pages (from 1 to 2);
- indexes 1 for "iin";
- indexes n+1, n+2 for "vout";

**in[1-{2n+1}]_crit**

**in[1-{2n+1}]_crit_alarm**

**in[1-{2n+1}]_input**

**in[1-{2n+1}]_label**

**in[2-{n+1}]_lcrit**

**in[2-{n+1}1_lcrit_alarm**

The driver provides the next attributes for the power:

- for power in alarm and input.
- for power out: highest and input.

The driver exports the following attributes via the 'sysfs' files, where

- 'n' is number of telemetry pages (from 1 to 2);
- indexes 1 for "pin";
- indexes n+1, n+2 for "pout";

**power1_alarm**

**power[2-{n+1}]_highest**

**power[1-{2n+1}]_input**

**power[1-{2n+1}]_label**

The driver provides the next attributes for the temperature (only from page 0):


**temp1_crit**

**temp1_crit_alarm**

**temp1_input**

**temp1_max**

**temp1_max_alarm**
