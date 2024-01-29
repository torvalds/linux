.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver nzxt-kraken3
==========================

Supported devices:

* NZXT Kraken X53
* NZXT Kraken X63
* NZXT Kraken X73
* NZXT Kraken Z53
* NZXT Kraken Z63
* NZXT Kraken Z73

Author: Jonas Malaco, Aleksa Savic

Description
-----------

This driver enables hardware monitoring support for NZXT Kraken X53/X63/X73 and
Z53/Z63/Z73 all-in-one CPU liquid coolers. All models expose liquid temperature
and pump speed (in RPM), as well as PWM control (either as a fixed value
or through a temp-PWM curve). The Z-series models additionally expose the speed
and duty of an optionally connected fan, with the same PWM control capabilities.

Pump and fan duty control mode can be set through pwm[1-2]_enable, where 1 is
for the manual control mode and 2 is for the liquid temp to PWM curve mode.
Writing a 0 disables control of the channel through the driver after setting its
duty to 100%.

The temperature of the curves relates to the fixed [20-59] range, correlating to
the detected liquid temperature. Only PWM values (ranging from 0-255) can be set.
If in curve mode, setting point values should be done in moderation - the devices
require complete curves to be sent for each change; they can lock up or discard
the changes if they are too numerous at once. Suggestion is to set them while
in an another mode, and then apply them by switching to curve.

The devices can report if they are faulty. The driver supports that situation
and will issue a warning. This can also happen when the USB cable is connected,
but SATA power is not.

The addressable RGB LEDs and LCD screen (only on Z-series models) are not
supported in this driver, but can be controlled through existing userspace tools,
such as `liquidctl`_.

.. _liquidctl: https://github.com/liquidctl/liquidctl

Usage Notes
-----------

As these are USB HIDs, the driver can be loaded automatically by the kernel and
supports hot swapping.

Possible pwm_enable values are:

====== ==========================================================================
0      Set fan to 100%
1      Direct PWM mode (applies value in corresponding PWM entry)
2      Curve control mode (applies the temp-PWM duty curve based on coolant temp)
====== ==========================================================================

Sysfs entries
-------------

============================== ================================================================
fan1_input                     Pump speed (in rpm)
fan2_input                     Fan speed (in rpm)
temp1_input                    Coolant temperature (in millidegrees Celsius)
pwm1                           Pump duty (value between 0-255)
pwm1_enable                    Pump duty control mode (0: disabled, 1: manual, 2: curve)
pwm2                           Fan duty (value between 0-255)
pwm2_enable                    Fan duty control mode (0: disabled, 1: manual, 2: curve)
temp[1-2]_auto_point[1-40]_pwm Temp-PWM duty curves (for pump and fan), related to coolant temp
============================== ================================================================
