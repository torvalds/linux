.. SPDX-License-Identifier: GPL-2.0-only

Kernel driver tps23861
======================

Supported chips:
  * Texas Instruments TPS23861

    Prefix: 'tps23861'

    Datasheet: https://www.ti.com/lit/gpn/tps23861

Author: Robert Marko <robert.marko@sartura.hr>

Description
-----------

This driver supports hardware monitoring for Texas Instruments TPS23861 PoE PSE.

TPS23861 is a quad port IEEE802.3at PSE controller with optional I2C control
and monitoring capabilities.

TPS23861 offers three modes of operation: Auto, Semi-Auto and Manual.

This driver only supports the Auto mode of operation providing monitoring
as well as enabling/disabling the four ports.

Sysfs entries
-------------

======================= =====================================================================
in[0-3]_input		Voltage on ports [1-4]
in[0-3]_label		"Port[1-4]"
in4_input		IC input voltage
in4_label		"Input"
temp1_input		IC die temperature
temp1_label		"Die"
curr[1-4]_input		Current on ports [1-4]
in[1-4]_label		"Port[1-4]"
in[0-3]_enable		Enable/disable ports [1-4]
======================= =====================================================================
