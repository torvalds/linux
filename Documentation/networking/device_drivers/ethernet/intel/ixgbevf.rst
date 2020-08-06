.. SPDX-License-Identifier: GPL-2.0+

============================================================
Linux Base Virtual Function Driver for Intel(R) 10G Ethernet
============================================================

Intel 10 Gigabit Virtual Function Linux driver.
Copyright(c) 1999-2018 Intel Corporation.

Contents
========

- Identifying Your Adapter
- Known Issues
- Support

This driver supports 82599, X540, X550, and X552-based virtual function devices
that can only be activated on kernels that support SR-IOV.

For questions related to hardware requirements, refer to the documentation
supplied with your Intel adapter. All hardware requirements listed apply to use
with Linux.


Identifying Your Adapter
========================
The driver is compatible with devices based on the following:

  * Intel(R) Ethernet Controller 82598
  * Intel(R) Ethernet Controller 82599
  * Intel(R) Ethernet Controller X520
  * Intel(R) Ethernet Controller X540
  * Intel(R) Ethernet Controller x550
  * Intel(R) Ethernet Controller X552
  * Intel(R) Ethernet Controller X553

For information on how to identify your adapter, and for the latest Intel
network drivers, refer to the Intel Support website:
https://www.intel.com/support

Known Issues/Troubleshooting
============================

SR-IOV requires the correct platform and OS support.

The guest OS loading this driver must support MSI-X interrupts.

This driver is only supported as a loadable module at this time. Intel is not
supplying patches against the kernel source to allow for static linking of the
drivers.

VLANs: There is a limit of a total of 64 shared VLANs to 1 or more VFs.


Support
=======
For general information, go to the Intel support website at:

https://www.intel.com/support/

or the Intel Wired Networking project hosted by Sourceforge at:

https://sourceforge.net/projects/e1000

If an issue is identified with the released source code on a supported kernel
with a supported adapter, email the specific information related to the issue
to e1000-devel@lists.sf.net.
