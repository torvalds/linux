.. SPDX-License-Identifier: GPL-2.0+

Linux* Base Driver for the Intel(R) Ethernet Connection E800 Series
===================================================================

Intel ice Linux driver.
Copyright(c) 2018 Intel Corporation.

Contents
========

- Enabling the driver
- Support

The driver in this release supports Intel's E800 Series of products. For
more information, visit Intel's support page at https://support.intel.com.

Enabling the driver
===================
The driver is enabled via the standard kernel configuration system,
using the make command::

  make oldconfig/menuconfig/etc.

The driver is located in the menu structure at:

  -> Device Drivers
    -> Network device support (NETDEVICES [=y])
      -> Ethernet driver support
        -> Intel devices
          -> Intel(R) Ethernet Connection E800 Series Support

Support
=======
For general information, go to the Intel support website at:

https://www.intel.com/support/

or the Intel Wired Networking project hosted by Sourceforge at:

https://sourceforge.net/projects/e1000

If an issue is identified with the released source code on a supported kernel
with a supported adapter, email the specific information related to the issue
to e1000-devel@lists.sf.net.
