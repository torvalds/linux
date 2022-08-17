.. SPDX-License-Identifier: GPL-2.0

====================
t7xx devlink support
====================

This document describes the devlink features implemented by the ``t7xx``
device driver.

Flash Update
============

The ``t7xx`` driver implements the flash update using the ``devlink-flash``
interface.

The driver uses DEVLINK_SUPPORT_FLASH_UPDATE_COMPONENT to identify the type of
firmware image that need to be programmed upon the request by user space application.

The supported list of firmware image types is described below.

.. list-table:: Firmware Image types
    :widths: 15 85

    * - Name
      - Description
    * - ``preloader``
      - The first-stage bootloader image
    * - ``loader_ext1``
      - Preloader extension image
    * - ``tee1``
      - ARM trusted firmware and TEE (Trusted Execution Environment) image
    * - ``lk``
      - The second-stage bootloader image
    * - ``spmfw``
      - MediaTek in-house ASIC for power management image
    * - ``sspm_1``
      - MediaTek in-house ASIC for power management under secure world image
    * - ``mcupm_1``
      - MediaTek in-house ASIC for cpu power management image
    * - ``dpm_1``
      - MediaTek in-house ASIC for dram power management image
    * - ``boot``
      - The kernel and dtb image
    * - ``rootfs``
      - Root filesystem image
    * - ``md1img``
      - Modem image
    * - ``md1dsp``
      - Modem DSP image
    * - ``mcf1``
      - Modem OTA image (Modem Configuration Framework) for operators
    * - ``mcf2``
      - Modem OTA image (Modem Configuration Framework) for OEM vendors
    * - ``mcf3``
      - Modem OTA image (other usage) for OEM configurations

``t7xx`` driver uses fastboot protocol for fw flashing. In the fw flashing
procedure, fastboot command's & response's are exchanged between driver
and wwan device.

The wwan device is put into fastboot mode via devlink reload command, by
passing "driver_reinit" action.

$ devlink dev reload pci/0000:$bdf action driver_reinit

Upon completion of fw flashing or coredump collection the wwan device is
reset to normal mode using devlink reload command, by passing "fw_activate"
action.

$ devlink dev reload pci/0000:$bdf action fw_activate

Flash Commands:
===============

$ devlink dev flash pci/0000:$bdf file preloader_k6880v1_mdot2_datacard.bin component "preloader"

$ devlink dev flash pci/0000:$bdf file loader_ext-verified.img component "loader_ext1"

$ devlink dev flash pci/0000:$bdf file tee-verified.img component "tee1"

$ devlink dev flash pci/0000:$bdf file lk-verified.img component "lk"

$ devlink dev flash pci/0000:$bdf file spmfw-verified.img component "spmfw"

$ devlink dev flash pci/0000:$bdf file sspm-verified.img component "sspm_1"

$ devlink dev flash pci/0000:$bdf file mcupm-verified.img component "mcupm_1"

$ devlink dev flash pci/0000:$bdf file dpm-verified.img component "dpm_1"

$ devlink dev flash pci/0000:$bdf file boot-verified.img component "boot"

$ devlink dev flash pci/0000:$bdf file root.squashfs component "rootfs"

$ devlink dev flash pci/0000:$bdf file modem-verified.img component "md1img"

$ devlink dev flash pci/0000:$bdf file dsp-verified.bin component "md1dsp"

$ devlink dev flash pci/0000:$bdf file OP_OTA.img component "mcf1"

$ devlink dev flash pci/0000:$bdf file OEM_OTA.img component "mcf2"

$ devlink dev flash pci/0000:$bdf file DEV_OTA.img component "mcf3"

Note: component "value" represents the partition type to be programmed.

Regions
=======

The ``t7xx`` driver supports core dump collection when device encounters
an exception. When wwan device encounters an exception, a snapshot of device
internal data will be taken by the driver using fastboot commands.

Following regions are accessed for device internal data.

.. list-table:: Regions implemented
    :widths: 15 85

    * - Name
      - Description
    * - ``mr_dump``
      - The detailed modem components log are captured in this region
    * - ``lk_dump``
      - This region dumps the current snapshot of lk


Region commands
===============

$ devlink region show


$ devlink region new mr_dump

$ devlink region read mr_dump snapshot 0 address 0 length $len

$ devlink region del mr_dump snapshot 0

$ devlink region new lk_dump

$ devlink region read lk_dump snapshot 0 address 0 length $len

$ devlink region del lk_dump snapshot 0

Note: $len is actual len to be dumped.
