.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver lattepanda-sigma-ec
=================================

Supported systems:

  * LattePanda Sigma (Intel 13th Gen i5-1340P)

    DMI vendor: LattePanda

    DMI product: LattePanda Sigma

    BIOS version: 5.27 (verified)

    Datasheet: Not available (EC registers discovered empirically)

Author: Mariano Abad <weimaraner@gmail.com>

Description
-----------

This driver provides hardware monitoring for the LattePanda Sigma
single-board computer made by DFRobot. The board uses an ITE IT8613E
Embedded Controller to manage a CPU cooling fan and thermal sensors.

The BIOS declares the ACPI Embedded Controller (``PNP0C09``) with
``_STA`` returning 0, preventing the kernel's ACPI EC subsystem from
initializing. This driver reads the EC directly via the standard ACPI
EC I/O ports (``0x62`` data, ``0x66`` command/status).

Sysfs attributes
----------------

======================= ===============================================
``fan1_input``          Fan speed in RPM (EC registers 0x2E:0x2F,
                        16-bit big-endian)
``fan1_label``          "CPU Fan"
``temp1_input``         Board/ambient temperature in millidegrees
                        Celsius (EC register 0x60, unsigned)
``temp1_label``         "Board Temp"
``temp2_input``         CPU proximity temperature in millidegrees
                        Celsius (EC register 0x70, unsigned)
``temp2_label``         "CPU Temp"
======================= ===============================================

Module parameters
-----------------

``force`` (bool, default false)
    Force loading on BIOS versions other than 5.27. The driver still
    requires DMI vendor and product name matching.

Known limitations
-----------------

* Fan speed control is not supported. The fan is always under EC
  automatic control.
* The EC register map was verified only on BIOS version 5.27.
  Other versions may use different register offsets; use the ``force``
  parameter at your own risk.
