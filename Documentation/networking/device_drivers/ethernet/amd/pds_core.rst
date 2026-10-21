.. SPDX-License-Identifier: GPL-2.0+

========================================================
Linux Driver for the AMD/Pensando(R) DSC adapter family
========================================================

Copyright(c) 2023 Advanced Micro Devices, Inc

Identifying the Adapter
=======================

To find if one or more AMD/Pensando PCI Core devices are installed on the
host, check for the PCI devices::

  # lspci -d 1dd8:100c
  b5:00.0 Processing accelerators: Pensando Systems Device 100c
  b6:00.0 Processing accelerators: Pensando Systems Device 100c

If such devices are listed as above, then the pds_core.ko driver should find
and configure them for use.  There should be log entries in the kernel
messages such as these::

  $ dmesg | grep pds_core
  pds_core 0000:b5:00.0: 252.048 Gb/s available PCIe bandwidth (16.0 GT/s PCIe x16 link)
  pds_core 0000:b5:00.0: FW: 1.60.0-73
  pds_core 0000:b6:00.0: 252.048 Gb/s available PCIe bandwidth (16.0 GT/s PCIe x16 link)
  pds_core 0000:b6:00.0: FW: 1.60.0-73

Driver and firmware version information can be gathered with devlink::

  $ devlink dev info pci/0000:b5:00.0
  pci/0000:b5:00.0:
    driver pds_core
    serial_number FLM18420073
    versions:
        fixed:
          asic.id 0x0
          asic.rev 0x0
        running:
          fw 1.51.0-73
        stored:
          fw.goldfw 1.15.9-C-22
          fw.mainfwa 1.60.0-73
          fw.mainfwb 1.60.0-57

Info versions
=============

The ``pds_core`` driver reports the following versions

.. list-table:: devlink info versions implemented
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``fw``
     - running
     - Version of firmware running on the device
   * - ``fw.goldfw``
     - stored
     - Version of firmware stored in the goldfw slot
   * - ``fw.mainfwa``
     - stored
     - Version of firmware stored in the mainfwa slot
   * - ``fw.mainfwb``
     - stored
     - Version of firmware stored in the mainfwb slot
   * - ``asic.id``
     - fixed
     - The ASIC type for this device
   * - ``asic.rev``
     - fixed
     - The revision of the ASIC for this device

Parameters
==========

The ``pds_core`` driver implements the following generic
parameters for controlling the functionality to be made available
as auxiliary_bus devices.

.. list-table:: Generic parameters implemented
   :widths: 5 5 8 82

   * - Name
     - Mode
     - Type
     - Description
   * - ``enable_vnet``
     - runtime
     - Boolean
     - Enables vDPA functionality through an auxiliary_bus device

Firmware Management
===================

The ``flash`` command can update a the DSC firmware.  The downloaded firmware
will be saved into either of firmware bank 1 or bank 2, whichever is not
currently in use, and that bank will used for the next boot::

  # devlink dev flash pci/0000:b5:00.0 \
            file pensando/dsc_fw_1.63.0-22.tar

Firmware Management (PLDM)
==========================

Firmware that supports PLDM can be updated using the devlink flash command
with a PLDM firmware package. The entire package can be updated at once::

  # devlink dev flash pci/0000:b5:00.0 file firmware.pldmfw

Individual components can also be updated by specifying the component name::

  # devlink dev flash pci/0000:b5:00.0 \
            file firmware.pldmfw component fw.cpld

Per-component update uses driver-defined component names (fw, fw.cpld,
etc.). Not all components support per-component update -
devlink will reject the request if the specified component cannot
be updated.

Gold (recovery) components can be updated by specifying the base component
name (e.g., ``fw`` for ``fw.gold``) with a goldfw package file when the
device supports per-component update. The ``.gold`` suffix in devlink info
output indicates the gold slot version, not a flash target.

Info versions (PLDM)
====================

Firmware that supports PLDM reports component versions using driver-defined
names. The driver reports the following component versions:

.. list-table:: devlink info versions for PLDM-capable firmware
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``fw``
     - running, stored
     - Main firmware
   * - ``fw.gold``
     - stored
     - Gold (recovery) firmware
   * - ``fw.bootloader``
     - running, stored
     - Boot loader
   * - ``fw.cpld``
     - running, stored
     - CPLD
   * - ``fw.secure``
     - running, stored
     - Secure boot firmware
   * - ``fw.fpga``
     - running, stored
     - FPGA configuration
   * - ``fw.suc``
     - running, stored
     - System Unit Controller firmware
   * - ``fw.suc.bootloader``
     - running, stored
     - System Unit Controller bootloader
   * - ``fw.uboot``
     - running, stored
     - U-Boot bootloader
   * - ``asic.id``
     - fixed
     - The ASIC type for this device
   * - ``asic.rev``
     - fixed
     - The revision of the ASIC for this device

Example output::

  $ devlink dev info pci/0000:00:05.0
  pci/0000:00:05.0:
    driver pds_core
    serial_number FLM18420073
    versions:
        fixed:
          asic.id 0x0
          asic.rev 0x0
        running:
          fw.bootloader 1.2.3
          fw 1.3.0
          fw.cpld 3.18
        stored:
          fw.bootloader 1.2.3
          fw.gold 1.2.0
          fw 1.3.0
          fw.cpld 3.18

Health Reporters
================

The driver supports a devlink health reporter for FW status::

  # devlink health show pci/0000:2b:00.0 reporter fw
  pci/0000:2b:00.0:
    reporter fw
      state healthy error 0 recover 0
  # devlink health diagnose pci/0000:2b:00.0 reporter fw
   Status: healthy State: 1 Generation: 0 Recoveries: 0

Enabling the driver
===================

The driver is enabled via the standard kernel configuration system,
using the make command::

  make oldconfig/menuconfig/etc.

The driver is located in the menu structure at:

  -> Device Drivers
    -> Network device support (NETDEVICES [=y])
      -> Ethernet driver support
        -> AMD devices
          -> AMD/Pensando Ethernet PDS_CORE Support

Support
=======

For general Linux networking support, please use the netdev mailing
list, which is monitored by AMD/Pensando personnel::

  netdev@vger.kernel.org
