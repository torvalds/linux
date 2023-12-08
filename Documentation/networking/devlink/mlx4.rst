.. SPDX-License-Identifier: GPL-2.0

====================
mlx4 devlink support
====================

This document describes the devlink features implemented by the ``mlx4``
device driver.

Parameters
==========

.. list-table:: Generic parameters implemented

   * - Name
     - Mode
   * - ``internal_err_reset``
     - driverinit, runtime
   * - ``max_macs``
     - driverinit
   * - ``region_snapshot_enable``
     - driverinit, runtime

The ``mlx4`` driver also implements the following driver-specific
parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``enable_64b_cqe_eqe``
     - Boolean
     - driverinit
     - Enable 64 byte CQEs/EQEs, if the FW supports it.
   * - ``enable_4k_uar``
     - Boolean
     - driverinit
     - Enable using the 4k UAR.

The ``mlx4`` driver supports reloading via ``DEVLINK_CMD_RELOAD``

Regions
=======

The ``mlx4`` driver supports dumping the firmware PCI crspace and health
buffer during a critical firmware issue.

In case a firmware command times out, firmware getting stuck, or a non zero
value on the catastrophic buffer, a snapshot will be taken by the driver.

The ``cr-space`` region will contain the firmware PCI crspace contents. The
``fw-health`` region will contain the device firmware's health buffer.
Snapshots for both of these regions are taken on the same event triggers.
