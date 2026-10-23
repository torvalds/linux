.. SPDX-License-Identifier: GPL-2.0

================================================================
Linux Base Driver for Nebula-matrix m18110-NIC/m18000-NIC family
================================================================

Overview:
=========
The m18110-NIC/m18000-NIC (marketed as SNIC s1000) is a series
of network interface cards for the Data Center Area.

This driver provides the core infrastructure for m18110/m18000-NIC
devices, including:

- PCI device enumeration and resource (BAR) management
- Firmware command interface via PF mailbox
- Channel-based communication between driver and firmware
- Device initialization and teardown

Support
=======

For more information about m18110-NIC/m18000-NIC, please visit the following URL:
https://www.nebula-matrix.com/snic_s1000_en

If an issue is identified with the released source code on the supported kernel
with a supported adapter, email the specific information related to the issue to
open@nebula-matrix.com.
