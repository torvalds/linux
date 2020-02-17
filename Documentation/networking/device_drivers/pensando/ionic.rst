.. SPDX-License-Identifier: GPL-2.0+

========================================================
Linux Driver for the Pensando(R) Ethernet adapter family
========================================================

Pensando Linux Ethernet driver.
Copyright(c) 2019 Pensando Systems, Inc

Contents
========

- Identifying the Adapter
- Support

Identifying the Adapter
=======================

To find if one or more Pensando PCI Ethernet devices are installed on the
host, check for the PCI devices::

  $ lspci -d 1dd8:
  b5:00.0 Ethernet controller: Device 1dd8:1002
  b6:00.0 Ethernet controller: Device 1dd8:1002

If such devices are listed as above, then the ionic.ko driver should find
and configure them for use.  There should be log entries in the kernel
messages such as these::

  $ dmesg | grep ionic
  ionic Pensando Ethernet NIC Driver, ver 0.15.0-k
  ionic 0000:b5:00.0 enp181s0: renamed from eth0
  ionic 0000:b6:00.0 enp182s0: renamed from eth0

Support
=======
For general Linux networking support, please use the netdev mailing
list, which is monitored by Pensando personnel::

  netdev@vger.kernel.org

For more specific support needs, please use the Pensando driver support
email::

  drivers@pensando.io
