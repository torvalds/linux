.. SPDX-License-Identifier: GPL-2.0

====================
bnxt devlink support
====================

This document describes the devlink features implemented by the ``bnxt``
device driver.

Parameters
==========

.. list-table:: Generic parameters implemented

   * - Name
     - Mode
   * - ``enable_sriov``
     - Permanent
   * - ``ignore_ari``
     - Permanent
   * - ``msix_vec_per_pf_max``
     - Permanent
   * - ``msix_vec_per_pf_min``
     - Permanent

The ``bnxt`` driver also implements the following driver-specific
parameters.

.. list-table:: Driver-specific parameters implemented
   :widths: 5 5 5 85

   * - Name
     - Type
     - Mode
     - Description
   * - ``gre_ver_check``
     - Boolean
     - Permanent
     - Generic Routing Encapsulation (GRE) version check will be enabled in
       the device. If disabled, the device will skip the version check for
       incoming packets.

Info versions
=============

The ``bnxt_en`` driver reports the following versions

.. list-table:: devlink info versions implemented
      :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``board.id``
     - fixed
     - Part number identifying the board design
   * - ``asic.id``
     - fixed
     - ASIC design identifier
   * - ``asic.rev``
     - fixed
     - ASIC design revision
   * - ``fw.psid``
     - stored, running
     - Firmware parameter set version of the board
   * - ``fw``
     - stored, running
     - Overall board firmware version
   * - ``fw.mgmt``
     - stored, running
     - NIC hardware resource management firmware version
   * - ``fw.mgmt.api``
     - running
     - Minimum firmware interface spec version supported between driver and firmware
   * - ``fw.nsci``
     - stored, running
     - General platform management firmware version
   * - ``fw.roce``
     - stored, running
     - RoCE management firmware version
