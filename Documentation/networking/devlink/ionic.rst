.. SPDX-License-Identifier: GPL-2.0

=====================
ionic devlink support
=====================

This document describes the devlink features implemented by the ``ionic``
device driver.

Info versions
=============

The ``ionic`` driver reports the following versions

.. list-table:: devlink info versions implemented
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``fw``
     - running
     - Version of firmware running on the device
   * - ``asic.id``
     - fixed
     - The ASIC type for this device
   * - ``asic.rev``
     - fixed
     - The revision of the ASIC for this device
