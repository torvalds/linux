.. SPDX-License-Identifier: GPL-2.0

=================
Ethernet Bridging
=================

Introduction
============

The IEEE 802.1Q-2022 (Bridges and Bridged Networks) standard defines the
operation of bridges in computer networks. A bridge, in the context of this
standard, is a device that connects two or more network segments and operates
at the data link layer (Layer 2) of the OSI (Open Systems Interconnection)
model. The purpose of a bridge is to filter and forward frames between
different segments based on the destination MAC (Media Access Control) address.

FAQ
===

What does a bridge do?
----------------------

A bridge transparently forwards traffic between multiple network interfaces.
In plain English this means that a bridge connects two or more physical
Ethernet networks, to form one larger (logical) Ethernet network.

Is it L3 protocol independent?
------------------------------

Yes. The bridge sees all frames, but it *uses* only L2 headers/information.
As such, the bridging functionality is protocol independent, and there should
be no trouble forwarding IPX, NetBEUI, IP, IPv6, etc.

Contact Info
============

The code is currently maintained by Roopa Prabhu <roopa@nvidia.com> and
Nikolay Aleksandrov <razor@blackwall.org>. Bridge bugs and enhancements
are discussed on the linux-netdev mailing list netdev@vger.kernel.org and
bridge@lists.linux-foundation.org.

The list is open to anyone interested: http://vger.kernel.org/vger-lists.html#netdev

External Links
==============

The old Documentation for Linux bridging is on:
https://wiki.linuxfoundation.org/networking/bridge
