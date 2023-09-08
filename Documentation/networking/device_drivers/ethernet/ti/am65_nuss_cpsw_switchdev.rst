.. SPDX-License-Identifier: GPL-2.0

===================================================================
Texas Instruments K3 AM65 CPSW NUSS switchdev based ethernet driver
===================================================================

:Version: 1.0

Port renaming
=============

In order to rename via udev::

    ip -d link show dev sw0p1 | grep switchid

    SUBSYSTEM=="net", ACTION=="add", ATTR{phys_switch_id}==<switchid>, \
	    ATTR{phys_port_name}!="", NAME="sw0$attr{phys_port_name}"


Multi mac mode
==============

- The driver is operating in multi-mac mode by default, thus
  working as N individual network interfaces.

Devlink configuration parameters
================================

See Documentation/networking/devlink/am65-nuss-cpsw-switch.rst

Enabling "switch"
=================

The Switch mode can be enabled by configuring devlink driver parameter
"switch_mode" to 1/true::

        devlink dev param set platform/c000000.ethernet \
        name switch_mode value true cmode runtime

This can be done regardless of the state of Port's netdev devices - UP/DOWN, but
Port's netdev devices have to be in UP before joining to the bridge to avoid
overwriting of bridge configuration as CPSW switch driver completely reloads its
configuration when first port changes its state to UP.

When the both interfaces joined the bridge - CPSW switch driver will enable
marking packets with offload_fwd_mark flag.

All configuration is implemented via switchdev API.

Bridge setup
============

::

        devlink dev param set platform/c000000.ethernet \
        name switch_mode value true cmode runtime

	ip link add name br0 type bridge
	ip link set dev br0 type bridge ageing_time 1000
	ip link set dev sw0p1 up
	ip link set dev sw0p2 up
	ip link set dev sw0p1 master br0
	ip link set dev sw0p2 master br0

	[*] bridge vlan add dev br0 vid 1 pvid untagged self

	[*] if vlan_filtering=1. where default_pvid=1

	Note. Steps [*] are mandatory.


On/off STP
==========

::

	ip link set dev BRDEV type bridge stp_state 1/0

VLAN configuration
==================

::

  bridge vlan add dev br0 vid 1 pvid untagged self <---- add cpu port to VLAN 1

Note. This step is mandatory for bridge/default_pvid.

Add extra VLANs
===============

 1. untagged::

	bridge vlan add dev sw0p1 vid 100 pvid untagged master
	bridge vlan add dev sw0p2 vid 100 pvid untagged master
	bridge vlan add dev br0 vid 100 pvid untagged self <---- Add cpu port to VLAN100

 2. tagged::

	bridge vlan add dev sw0p1 vid 100 master
	bridge vlan add dev sw0p2 vid 100 master
	bridge vlan add dev br0 vid 100 pvid tagged self <---- Add cpu port to VLAN100

FDBs
----

FDBs are automatically added on the appropriate switch port upon detection

Manually adding FDBs::

    bridge fdb add aa:bb:cc:dd:ee:ff dev sw0p1 master vlan 100
    bridge fdb add aa:bb:cc:dd:ee:fe dev sw0p2 master <---- Add on all VLANs

MDBs
----

MDBs are automatically added on the appropriate switch port upon detection

Manually adding MDBs::

  bridge mdb add dev br0 port sw0p1 grp 239.1.1.1 permanent vid 100
  bridge mdb add dev br0 port sw0p1 grp 239.1.1.1 permanent <---- Add on all VLANs

Multicast flooding
==================
CPU port mcast_flooding is always on

Turning flooding on/off on switch ports:
bridge link set dev sw0p1 mcast_flood on/off

Access and Trunk port
=====================

::

 bridge vlan add dev sw0p1 vid 100 pvid untagged master
 bridge vlan add dev sw0p2 vid 100 master


 bridge vlan add dev br0 vid 100 self
 ip link add link br0 name br0.100 type vlan id 100

Note. Setting PVID on Bridge device itself works only for
default VLAN (default_pvid).
