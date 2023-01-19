.. SPDX-License-Identifier: GPL-2.0

==========================
etas_es58x devlink support
==========================

This document describes the devlink features implemented by the
``etas_es58x`` device driver.

Info versions
=============

The ``etas_es58x`` driver reports the following versions

.. list-table:: devlink info versions implemented
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``fw``
     - running
     - Version of the firmware running on the device. Also available
       through ``ethtool -i`` as the first member of the
       ``firmware-version``.
   * - ``fw.bootloader``
     - running
     - Version of the bootloader running on the device. Also available
       through ``ethtool -i`` as the second member of the
       ``firmware-version``.
   * - ``board.rev``
     - fixed
     - The hardware revision of the device.
   * - ``serial_number``
     - fixed
     - The USB serial number. Also available through ``lsusb -v``.
