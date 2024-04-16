.. SPDX-License-Identifier: GPL-2.0

====================
hns3 devlink support
====================

This document describes the devlink features implemented by the ``hns3``
device driver.

The ``hns3`` driver supports reloading via ``DEVLINK_CMD_RELOAD``.

Info versions
=============

The ``hns3`` driver reports the following versions

.. list-table:: devlink info versions implemented
   :widths: 10 10 80

   * - Name
     - Type
     - Description
   * - ``fw``
     - running
     - Used to represent the firmware version.
