.. SPDX-License-Identifier: GPL-2.0

===================
ixd devlink support
===================

This document describes the devlink features implemented by the ``ixd``
device driver.

Info versions
=============

The ``ixd`` driver reports the following versions

.. list-table:: devlink info versions implemented
    :widths: 5 5 5 90

    * - Name
      - Type
      - Example
      - Description
    * - ``device.type``
      - fixed
      - MEV
      - The hardware type for this device
    * - ``fw.mgmt.api``
      - running
      - 2.0
      - 2-digit version number (major.minor) of the communication channel
        (virtchnl) used by the device.
