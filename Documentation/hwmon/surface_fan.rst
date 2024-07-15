.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver surface_fan
=========================

Supported Devices:

  * Microsoft Surface Pro 9

Author: Ivor Wanders <ivor@iwanders.net>

Description
-----------

This provides monitoring of the fan found in some Microsoft Surface Pro devices,
like the Surface Pro 9. The fan is always controlled by the onboard controller.

Sysfs interface
---------------

======================= ======= =========================================
Name                    Perm    Description
======================= ======= =========================================
``fan1_input``          RO      Current fan speed in RPM.
======================= ======= =========================================
