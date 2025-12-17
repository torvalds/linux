.. SPDX-License-Identifier: GPL-2.0-only

Kernel driver macsmc-hwmon
==========================

Supported hardware

    * Apple Silicon Macs (M1 and up)

Author: James Calligeros <jcalligeros99@gmail.com>

Description
-----------

macsmc-hwmon exposes the Apple System Management controller's
temperature, voltage, current and power sensors, as well as
fan speed and control capabilities, via hwmon.

Because each Apple Silicon Mac exposes a different set of sensors
(e.g. the MacBooks expose battery telemetry that is not present on
the desktop Macs), sensors present on any given machine are described
via Devicetree. The driver picks these up and registers them with
hwmon when probed.

Manual fan speed is supported via the fan_control module parameter. This
is disabled by default and marked as unsafe, as it cannot be proven that
the system will fail safe if overheating due to manual fan control being
used.

sysfs interface
---------------

currX_input
    Ammeter value

currX_label
    Ammeter label

fanX_input
    Current fan speed

fanX_label
    Fan label

fanX_min
    Minimum possible fan speed

fanX_max
    Maximum possible fan speed

fanX_target
    Current fan setpoint

inX_input
    Voltmeter value

inX_label
    Voltmeter label

powerX_input
    Power meter value

powerX_label
    Power meter label

tempX_input
    Temperature sensor value

tempX_label
    Temperature sensor label

