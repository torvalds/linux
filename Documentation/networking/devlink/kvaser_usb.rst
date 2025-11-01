.. SPDX-License-Identifier: GPL-2.0

==========================
kvaser_usb devlink support
==========================

This document describes the devlink features implemented by the
``kvaser_usb`` device driver.

Info versions
=============

The ``kvaser_usb`` driver reports the following versions

.. list-table:: devlink info versions implemented
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``fw``
     - running
     - Version of the firmware running on the device. Also available
       through ``ethtool -i`` as ``firmware-version``.
   * - ``board.rev``
     - fixed
     - The device hardware revision.
   * - ``board.id``
     - fixed
     - The device EAN (product number).
   * - ``serial_number``
     - fixed
     - The device serial number.
