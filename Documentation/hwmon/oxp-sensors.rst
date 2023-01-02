.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver oxp-sensors
=========================

Author:
    - Joaquín Ignacio Aramendía <samsagax@gmail.com>

Description:
------------

One X Player devices from One Netbook provide fan readings and fan control
through its Embedded Controller.

Currently only supports AMD boards from the One X Player and AOK ZOE lineup.
Intel boards could be supported if we could figure out the EC registers and
values to write to since the EC layout and model is different.

Supported devices
-----------------

Currently the driver supports the following handhelds:

 - AOK ZOE A1
 - OneXPlayer AMD
 - OneXPlayer mini AMD
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
