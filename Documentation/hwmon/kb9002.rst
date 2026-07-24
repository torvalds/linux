.. SPDX-License-Identifier: GPL-2.0-or-later

Kernel driver kb9002
====================

Supported chips:

  * Kandou KB9002

    Prefix: 'kb9002'

    Addresses scanned: -

    Datasheet: KA-015171-PD (available from Kandou under NDA)

Author: Andy Chung <andy.chung@amd.com>

Description
-----------

The Kandou KB9002 is an 8-lane PCIe 5.0 retimer with an integrated
microcontroller. It exposes an SMBus 3.0 target (with mandatory PEC)
on its sideband interface. The internal firmware aggregates per-lane
die temperatures and publishes the maximum value through a 16-bit
addressed register window.

This driver reports that aggregated maximum as the only hwmon
temperature channel. The running firmware version and the firmware
boot status are exposed under debugfs.

sysfs interface
---------------

==================  ===============================================
temp1_input         Aggregated maximum die temperature across all
                    active lanes (millidegrees Celsius).
==================  ===============================================

debugfs interface
-----------------

Files live in the per-client debugfs directory created by the I2C
core: ``/sys/kernel/debug/i2c/i2c-<bus>/<bus>-<addr>/``.

==================  ===============================================
fw_ver              Running firmware version in
                    "major.minor.patch.suffix" format. Read-only.
fw_load_status      Firmware boot status: "normal" once firmware has
                    finished initialising, "abnormal" otherwise.
                    Read-only.
==================  ===============================================

Notes
-----

The driver requires ``I2C_FUNC_SMBUS_BLOCK_DATA``,
``I2C_FUNC_SMBUS_PEC`` and ``I2C_FUNC_I2C`` from the host adapter. The
last is needed only during probe for the host-interface mode switch;
runtime accesses use SMBus block transactions exclusively.

The retimer's SMBus address is configurable on three strap pins and
ranges from 0x20 to 0x27. The address is selected through device tree
(or i2c board info) the same way as any other I2C device; the driver
does not auto-detect.
