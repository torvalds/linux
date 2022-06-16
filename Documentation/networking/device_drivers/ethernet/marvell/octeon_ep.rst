.. SPDX-License-Identifier: GPL-2.0+

====================================================================
Linux kernel networking driver for Marvell's Octeon PCI Endpoint NIC
====================================================================

Network driver for Marvell's Octeon PCI EndPoint NIC.
Copyright (c) 2020 Marvell International Ltd.

Contents
========

- `Overview`_
- `Supported Devices`_
- `Interface Control`_

Overview
========
This driver implements networking functionality of Marvell's Octeon PCI
EndPoint NIC.

Supported Devices
=================
Currently, this driver support following devices:
 * Network controller: Cavium, Inc. Device b200

Interface Control
=================
Network Interface control like changing mtu, link speed, link down/up are
done by writing command to mailbox command queue, a mailbox interface
implemented through a reserved region in BAR4.
This driver writes the commands into the mailbox and the firmware on the
Octeon device processes them. The firmware also sends unsolicited notifications
to driver for events suchs as link change, through notification queue
implemented as part of mailbox interface.
