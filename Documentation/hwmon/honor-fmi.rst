.. SPDX-License-Identifier: GPL-2.0-only

Kernel driver honor-fmi
=======================

Supported systems:

  * HONOR FMI-XX

Author: Nikita Dubrovskih <testname142@gmail.com>

Description
-----------

The driver provides read-only monitoring of the fan speed on the HONOR FMI-XX.
The system firmware implements a ``GFNS`` ACPI method which returns the speed
of one of two firmware fan channels in RPM. Embedded Controller access and
serialization are handled by the firmware method.

The driver does not expose fan control or direct Embedded Controller access.

Sysfs entries
-------------

The following attributes are supported:

======================= ======= =============================================
Name                    Perm    Description
======================= ======= =============================================
``fan1_input``          RO      Fan channel 0 speed in RPM
``fan2_input``          RO      Fan channel 1 speed in RPM
======================= ======= =============================================
