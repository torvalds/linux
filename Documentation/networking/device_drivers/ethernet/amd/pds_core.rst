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

Health Reporters
================

The driver supports a devlink health reporter for FW status::

  # devlink health show pci/0000:2b:00.0 reporter fw
  pci/0000:2b:00.0:
    reporter fw
      state healthy error 0 recover 0
  # devlink health diagnose pci/0000:2b:00.0 reporter fw
   Status: healthy State: 1 Generation: 0 Recoveries: 0

Support
=======

For general Linux networking support, please use the netdev mailing
list, which is monitored by AMD/Pensando personnel::

  netdev@vger.kernel.org
