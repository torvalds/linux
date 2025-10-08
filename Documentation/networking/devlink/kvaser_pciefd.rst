.. SPDX-License-Identifier: GPL-2.0

=============================
kvaser_pciefd devlink support
=============================

This document describes the devlink features implemented by the
``kvaser_pciefd`` device driver.

Info versions
=============

The ``kvaser_pciefd`` driver reports the following versions

.. list-table:: devlink info versions implemented
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``fw``
     - running
     - Version of the firmware running on the device. Also available
       through ``ethtool -i`` as ``firmware-version``.
