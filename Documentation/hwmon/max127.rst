.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver max127
====================

Author:

  * Tao Ren <rentao.bupt@gmail.com>

Supported chips:

  * Maxim MAX127

    Prefix: 'max127'

    Datasheet: https://datasheets.maximintegrated.com/en/ds/MAX127-MAX128.pdf

Description
-----------

The MAX127 is a multirange, 12-bit data acquisition system (DAS) providing
8 analog input channels that are independently software programmable for
a variety of ranges. The available ranges are {0,5V}, {0,10V}, {-5,5V}
and {-10,10V}.

The MAX127 features a 2-wire, I2C-compatible serial interface that allows
communication among multiple devices using SDA and SCL lines.

Sysfs interface
---------------

  ============== ==============================================================
  in[0-7]_input  The input voltage (in mV) of the corresponding channel.
		 RO

  in[0-7]_min    The lower input limit (in mV) for the corresponding channel.
		 ADC range and LSB will be updated when the limit is changed.
		 For the MAX127, it will be adjusted to -10000, -5000, or 0.
		 RW

  in[0-7]_max    The higher input limit (in mV) for the corresponding channel.
		 ADC range and LSB will be updated when the limit is changed.
		 For the MAX127, it will be adjusted to 0, 5000, or 10000.
		 RW
  ============== ==============================================================
