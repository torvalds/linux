.. SPDX-License-Identifier: GPL-2.0

============
NET_FAILOVER
============

Overview
========

The net_failover driver provides an automated failover mechanism via APIs
to create and destroy a failover master netdev and mananges a primary and
standby slave netdevs that get registered via the generic failover
infrastructrure.

The failover netdev acts a master device and controls 2 slave devices. The
original paravirtual interface is registered as 'standby' slave netdev and
a passthru/vf device with the same MAC gets registered as 'primary' slave
netdev. Both 'standby' and 'failover' netdevs are associated with the same
'pci' device. The user accesses the network interface via 'failover' netdev.
The 'failover' netdev chooses 'primary' netdev as default for transmits when
it is available with link up and running.

This can be used by paravirtual drivers to enable an alternate low latency
datapath. It also enables hypervisor controlled live migration of a VM with
direct attached VF by failing over to the paravirtual datapath when the VF
is unplugged.
