.. SPDX-License-Identifier: GPL-2.0+

============================================================
Linux Driver for the AMD Pensando(R) Ethernet adapter family
============================================================

AMD Pensando RDMA driver.
Copyright (C) 2018-2025, Advanced Micro Devices, Inc.

Contents
========

- Identifying the Adapter
- Enabling the driver
- Support

Identifying the Adapter
=======================

See Documentation/networking/device_drivers/ethernet/pensando/ionic.rst
for more information on identifying the adapter.

Enabling the driver
===================

The driver is enabled via the standard kernel configuration system,
using the make command::

  make oldconfig/menuconfig/etc.

The driver is located in the menu structure at:

  -> Device Drivers
    -> InfiniBand support
      -> AMD Pensando DSC RDMA/RoCE Support

Support
=======

For general Linux rdma support, please use the rdma mailing
list, which is monitored by AMD Pensando personnel::

  linux-rdma@vger.kernel.org
