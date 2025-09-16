.. SPDX-License-Identifier: GPL-2.0

Kernel driver mc33xs2410_hwmon
==============================

Supported devices:

  * NXPs MC33XS2410

    Datasheet: https://www.nxp.com/docs/en/data-sheet/MC33XS2410.pdf

Authors:

	Dimitri Fedrau <dimitri.fedrau@liebherr.com>

Description
-----------

The MC33XS2410 is a four channel self-protected high-side switch featuring
hardware monitoring functions such as temperature, current and voltages for each
of the four channels.

Sysfs entries
-------------

======================= ======================================================
temp1_label		"Central die temperature"
temp1_input		Measured temperature of central die

temp[2-5]_label		"Channel [1-4] temperature"
temp[2-5]_input		Measured temperature of a single channel
temp[2-5]_alarm		Temperature alarm
temp[2-5]_max		Maximal temperature
======================= ======================================================
