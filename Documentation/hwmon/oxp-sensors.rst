.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver oxp-sensors
=========================

Authors:
    - Derek John Clark <derekjohn.clark@gmail.com>
    - Joaquín Ignacio Aramendía <samsagax@gmail.com>

Description:
------------

Handheld devices from One Netbook and Aya Neo provide fan readings and fan
control through their embedded controllers.

Currently only supports AMD boards from One X Player, AOK ZOE, and some Aya
Neo devices. One X Player Intel boards could be supported if we could figure
out the EC registers and values to write to since the EC layout and model is
different. Aya Neo devices preceding the AIR may not be supportable as the EC
model is different and do not appear to have manual control capabilities.

Some models have a toggle for changing the behaviour of the "Turbo/Silent"
button of the device. It will change the key event that it triggers with
a flip of the `tt_toggle` attribute. See below for boards that support this
function.

Supported devices
-----------------

Currently the driver supports the following handhelds:

 - AOK ZOE A1
 - AOK ZOE A1 PRO
 - Aya Neo 2
 - Aya Neo AIR
 - Aya Neo AIR Plus (Mendocino)
 - Aya Neo AIR Pro
 - Aya Neo Geek
 - OneXPlayer AMD
 - OneXPlayer mini AMD
 - OneXPlayer mini AMD PRO

"Turbo/Silent" button behaviour toggle is only supported on:
 - AOK ZOE A1
 - AOK ZOE A1 PRO
 - OneXPlayer mini AMD (only with updated alpha BIOS)
 - OneXPlayer mini AMD PRO

Sysfs entries
-------------

The following attributes are supported:

fan1_input
  Read Only. Reads current fan RMP.

pwm1_enable
  Read Write. Enable manual fan control. Write "1" to set to manual, write "0"
  to let the EC control de fan speed. Read this attribute to see current status.

pwm1
  Read Write. Read this attribute to see current duty cycle in the range [0-255].
  When pwm1_enable is set to "1" (manual) write any value in the range [0-255]
  to set fan speed.

tt_toggle
  Read Write. Read this attribute to check the status of the turbo/silent
  button behaviour function. Write "1" to activate the switch and "0" to
  deactivate it. The specific keycodes and behaviour is specific to the device
  both with this function on and off. This attribute is attached to the platform
  driver and not to the hwmon driver (/sys/devices/platform/oxp-platform/tt_toggle)
