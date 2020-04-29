.. SPDX-License-Identifier: GPL-2.0+

===========================================================
Linux Base Virtual Function Driver for Intel(R) 1G Ethernet
===========================================================

Intel Gigabit Virtual Function Linux driver.
Copyright(c) 1999-2018 Intel Corporation.

Contents
========
- Identifying Your Adapter
- Additional Configurations
- Support

This driver supports Intel 82576-based virtual function devices-based virtual
function devices that can only be activated on kernels that support SR-IOV.

SR-IOV requires the correct platform and OS support.

The guest OS loading this driver must support MSI-X interrupts.

For questions related to hardware requirements, refer to the documentation
supplied with your Intel adapter. All hardware requirements listed apply to use
with Linux.

Driver information can be obtained using ethtool, lspci, and ifconfig.
Instructions on updating ethtool can be found in the section Additional
Configurations later in this document.

NOTE: There is a limit of a total of 32 shared VLANs to 1 or more VFs.


Identifying Your Adapter
========================
For information on how to identify your adapter, and for the latest Intel
network drivers, refer to the Intel Support website:
http://www.intel.com/support


Additional Features and Configurations
======================================

ethtool
-------
The driver utilizes the ethtool interface for driver configuration and
diagnostics, as well as displaying statistical information. The latest ethtool
version is required for this functionality. Download it at:

https://www.kernel.org/pub/software/network/ethtool/


Support
=======
For general information, go to the Intel support website at:

https://www.intel.com/support/

or the Intel Wired Networking project hosted by Sourceforge at:

https://sourceforge.net/projects/e1000

If an issue is identified with the released source code on a supported kernel
with a supported adapter, email the specific information related to the issue
to e1000-devel@lists.sf.net.
