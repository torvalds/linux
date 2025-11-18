.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver cros_ec_hwmon
===========================

Supported chips:

  * ChromeOS embedded controllers.

    Prefix: 'cros_ec'

    Addresses scanned: -

Author:

  - Thomas Weißschuh <linux@weissschuh.net>

Description
-----------

This driver implements support for hardware monitoring commands exposed by the
ChromeOS embedded controller used in Chromebooks and other devices.

The channel labels exposed via hwmon are retrieved from the EC itself.

Fan and temperature readings are supported. PWM fan control is also supported if
the EC also supports setting fan PWM values and fan mode. Note that EC will
switch fan control mode back to auto when suspended. This driver will restore
the fan state to what they were before suspended when resumed.
If a fan is controllable, this driver will register that fan as a cooling device
in the thermal framework as well.
