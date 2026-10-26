.. SPDX-License-Identifier: GPL-2.0

Kernel driver axiado-tsadc
==========================

Supported chips:

  * Axiado AX3000 and AX3005 on-chip temperature sensor (TSADC)

    Prefix: 'axiado_tsadc'

    Addresses scanned: -

    Datasheet: Not publicly available

Authors:

  - Petar Stepanovic <pstepanovic@axiado.com>

Description
-----------

This driver supports the Temperature Sensor ADC (TSADC) block found on Axiado
AX3000 and AX3005 SoCs. The block monitors the on-die temperature of the SoC.

An SoC may instantiate several TSADC blocks at different locations on the die.
Each block is described by its own device tree node and is registered as a
separate hwmon device with a single temperature channel.

The sensor is configured for continuous conversion at probe time, averaging
16 samples per conversion. The averaged 12-bit ADC code is converted to
millidegrees Celsius using a lookup table with linear interpolation between
entries. The table covers -40 to +125 degrees Celsius; readings outside that
range are clamped to the nearest supported temperature.

sysfs entries
-------------

======================= =======================================================
temp1_input             Die temperature in millidegrees Celsius (read-only)
======================= =======================================================
