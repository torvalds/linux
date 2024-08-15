.. SPDX-License-Identifier: GPL-2.0

Kernel driver pim4328
=====================

Supported chips:

  * Flex PIM4328

    Prefix: 'pim4328', 'bmr455'

    Addresses scanned: -

    Datasheet:

https://flexpowermodules.com/resources/fpm-techspec-pim4328

  * Flex PIM4820

    Prefixes: 'pim4820'

    Addresses scanned: -

    Datasheet: https://flexpowermodules.com/resources/fpm-techspec-pim4820

  * Flex PIM4006, PIM4106, PIM4206, PIM4306, PIM4406

    Prefixes: 'pim4006', 'pim4106', 'pim4206', 'pim4306', 'pim4406'

    Addresses scanned: -

    Datasheet: https://flexpowermodules.com/resources/fpm-techspec-pim4006

Author: Erik Rosen <erik.rosen@metormote.com>


Description
-----------

This driver supports hardware monitoring for Flex PIM4328 and
compatible digital power interface modules.

The driver is a client driver to the core PMBus driver. Please see
Documentation/hwmon/pmbus.rst and Documentation.hwmon/pmbus-core for details
on PMBus client drivers.


Usage Notes
-----------

This driver does not auto-detect devices. You will have to instantiate the
devices explicitly. Please see Documentation/i2c/instantiating-devices.rst for
details.


Platform data support
---------------------

The driver supports standard PMBus driver platform data.


Sysfs entries
-------------

The following attributes are supported. All attributes are read-only.

======================= ========================================================
in1_label		"vin"
in1_input		Measured input voltage.
in1_alarm		Input voltage alarm.

in2_label		"vin.0"
in2_input		Measured input voltage on input A.

			PIM4328 and PIM4X06

in3_label		"vin.1"
in3_input		Measured input voltage on input B.

			PIM4328 and PIM4X06

in4_label		"vcap"
in4_input		Measured voltage on holdup capacitor.

			PIM4328

curr1_label		"iin.0"
curr1_input		Measured input current on input A.

			PIM4X06

curr2_label		"iin.1"
curr2_input		Measured input current on input B.

			PIM4X06

currX_label		"iout1"
currX_input		Measured output current.
currX_alarm		Output current alarm.

			X is 1 for PIM4820, 3 otherwise.

temp1_input		Measured temperature.
temp1_alarm		High temperature alarm.
======================= ========================================================
