.. SPDX-License-Identifier: GPL-2.0-only

Broadcom BCM54140 Quad SGMII/QSGMII PHY
=======================================

Supported chips:

   * Broadcom BCM54140

     Datasheet: not public

Author: Michael Walle <michael@walle.cc>

Description
-----------

The Broadcom BCM54140 is a Quad SGMII/QSGMII PHY which supports monitoring
its die temperature as well as two analog voltages.

The AVDDL is a 1.0V analogue voltage, the AVDDH is a 3.3V analogue voltage.
Both voltages and the temperature are measured in a round-robin fashion.

Sysfs entries
-------------

The following attributes are supported.

======================= ========================================================
in0_label		"AVDDL"
in0_input		Measured AVDDL voltage.
in0_min			Minimum AVDDL voltage.
in0_max			Maximum AVDDL voltage.
in0_alarm		AVDDL voltage alarm.

in1_label		"AVDDH"
in1_input		Measured AVDDH voltage.
in1_min			Minimum AVDDH voltage.
in1_max			Maximum AVDDH voltage.
in1_alarm		AVDDH voltage alarm.

temp1_input		Die temperature.
temp1_min		Minimum die temperature.
temp1_max		Maximum die temperature.
temp1_alarm		Die temperature alarm.
======================= ========================================================
