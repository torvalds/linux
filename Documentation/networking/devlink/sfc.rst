.. SPDX-License-Identifier: GPL-2.0

===================
sfc devlink support
===================

This document describes the devlink features implemented by the ``sfc``
device driver for the ef10 and ef100 devices.

Info versions
=============

The ``sfc`` driver reports the following versions

.. list-table:: devlink info versions implemented
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``fw.bundle_id``
     - stored
     - Version of the firmware "bundle" image that was last used to update
       multiple components.
   * - ``fw.mgmt.suc``
     - running
     - For boards where the management function is split between multiple
       control units, this is the SUC control unit's firmware version.
   * - ``fw.mgmt.cmc``
     - running
     - For boards where the management function is split between multiple
       control units, this is the CMC control unit's firmware version.
   * - ``fpga.rev``
     - running
     - FPGA design revision.
   * - ``fpga.app``
     - running
     - Datapath programmable logic version.
   * - ``fw.app``
     - running
     - Datapath software/microcode/firmware version.
   * - ``coproc.boot``
     - running
     - SmartNIC application co-processor (APU) first stage boot loader version.
   * - ``coproc.uboot``
     - running
     - SmartNIC application co-processor (APU) co-operating system loader version.
   * - ``coproc.main``
     - running
     - SmartNIC application co-processor (APU) main operating system version.
   * - ``coproc.recovery``
     - running
     - SmartNIC application co-processor (APU) recovery operating system version.
   * - ``fw.exprom``
     - running
     - Expansion ROM version. For boards where the expansion ROM is split between
       multiple images (e.g. PXE and UEFI), this is the specifically the PXE boot
       ROM version.
   * - ``fw.uefi``
     - running
     - UEFI driver version (No UNDI support).

Flash Update
============

The ``sfc`` driver implements support for flash update using the
``devlink-flash`` interface. It supports updating the device flash using a
combined flash image ("bundle") that contains multiple components (on ef10,
typically ``fw.mgmt``, ``fw.app``, ``fw.exprom`` and ``fw.uefi``).

The driver does not support any overwrite mask flags.
