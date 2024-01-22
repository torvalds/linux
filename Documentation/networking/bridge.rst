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

Bridge kAPI
===========

Here are some core structures of bridge code. Note that the kAPI is *unstable*,
and can be changed at any time.

.. kernel-doc:: net/bridge/br_private.h
   :identifiers: net_bridge_vlan

Bridge uAPI
===========

Modern Linux bridge uAPI is accessed via Netlink interface. You can find
below files where the bridge and bridge port netlink attributes are defined.

Bridge netlink attributes
-------------------------

.. kernel-doc:: include/uapi/linux/if_link.h
   :doc: Bridge enum definition

Bridge port netlink attributes
------------------------------

.. kernel-doc:: include/uapi/linux/if_link.h
   :doc: Bridge port enum definition

Bridge sysfs
------------

The sysfs interface is deprecated and should not be extended if new
options are added.

STP
===

The STP (Spanning Tree Protocol) implementation in the Linux bridge driver
is a critical feature that helps prevent loops and broadcast storms in
Ethernet networks by identifying and disabling redundant links. In a Linux
bridge context, STP is crucial for network stability and availability.

STP is a Layer 2 protocol that operates at the Data Link Layer of the OSI
model. It was originally developed as IEEE 802.1D and has since evolved into
multiple versions, including Rapid Spanning Tree Protocol (RSTP) and
`Multiple Spanning Tree Protocol (MSTP)
<https://lore.kernel.org/netdev/20220316150857.2442916-1-tobias@waldekranz.com/>`_.

The 802.1D-2004 removed the original Spanning Tree Protocol, instead
incorporating the Rapid Spanning Tree Protocol (RSTP). By 2014, all the
functionality defined by IEEE 802.1D has been incorporated into either
IEEE 802.1Q (Bridges and Bridged Networks) or IEEE 802.1AC (MAC Service
Definition). 802.1D has been officially withdrawn in 2022.

Bridge Ports and STP States
---------------------------

In the context of STP, bridge ports can be in one of the following states:
  * Blocking: The port is disabled for data traffic and only listens for
    BPDUs (Bridge Protocol Data Units) from other devices to determine the
    network topology.
  * Listening: The port begins to participate in the STP process and listens
    for BPDUs.
  * Learning: The port continues to listen for BPDUs and begins to learn MAC
    addresses from incoming frames but does not forward data frames.
  * Forwarding: The port is fully operational and forwards both BPDUs and
    data frames.
  * Disabled: The port is administratively disabled and does not participate
    in the STP process. The data frames forwarding are also disabled.

Root Bridge and Convergence
---------------------------

In the context of networking and Ethernet bridging in Linux, the root bridge
is a designated switch in a bridged network that serves as a reference point
for the spanning tree algorithm to create a loop-free topology.

Here's how the STP works and root bridge is chosen:
  1. Bridge Priority: Each bridge running a spanning tree protocol, has a
     configurable Bridge Priority value. The lower the value, the higher the
     priority. By default, the Bridge Priority is set to a standard value
     (e.g., 32768).
  2. Bridge ID: The Bridge ID is composed of two components: Bridge Priority
     and the MAC address of the bridge. It uniquely identifies each bridge
     in the network. The Bridge ID is used to compare the priorities of
     different bridges.
  3. Bridge Election: When the network starts, all bridges initially assume
     that they are the root bridge. They start advertising Bridge Protocol
     Data Units (BPDU) to their neighbors, containing their Bridge ID and
     other information.
  4. BPDU Comparison: Bridges exchange BPDUs to determine the root bridge.
     Each bridge examines the received BPDUs, including the Bridge Priority
     and Bridge ID, to determine if it should adjust its own priorities.
     The bridge with the lowest Bridge ID will become the root bridge.
  5. Root Bridge Announcement: Once the root bridge is determined, it sends
     BPDUs with information about the root bridge to all other bridges in the
     network. This information is used by other bridges to calculate the
     shortest path to the root bridge and, in doing so, create a loop-free
     topology.
  6. Forwarding Ports: After the root bridge is selected and the spanning tree
     topology is established, each bridge determines which of its ports should
     be in the forwarding state (used for data traffic) and which should be in
     the blocking state (used to prevent loops). The root bridge's ports are
     all in the forwarding state. while other bridges have some ports in the
     blocking state to avoid loops.
  7. Root Ports: After the root bridge is selected and the spanning tree
     topology is established, each non-root bridge processes incoming
     BPDUs and determines which of its ports provides the shortest path to the
     root bridge based on the information in the received BPDUs. This port is
     designated as the root port. And it is in the Forwarding state, allowing
     it to actively forward network traffic.
  8. Designated ports: A designated port is the port through which the non-root
     bridge will forward traffic towards the designated segment. Designated ports
     are placed in the Forwarding state. All other ports on the non-root
     bridge that are not designated for specific segments are placed in the
     Blocking state to prevent network loops.

STP ensures network convergence by calculating the shortest path and disabling
redundant links. When network topology changes occur (e.g., a link failure),
STP recalculates the network topology to restore connectivity while avoiding loops.

Proper configuration of STP parameters, such as the bridge priority, can
influence network performance, path selection and which bridge becomes the
Root Bridge.

User space STP helper
---------------------

The user space STP helper *bridge-stp* is a program to control whether to use
user mode spanning tree. The ``/sbin/bridge-stp <bridge> <start|stop>`` is
called by the kernel when STP is enabled/disabled on a bridge
(via ``brctl stp <bridge> <on|off>`` or ``ip link set <bridge> type bridge
stp_state <0|1>``).  The kernel enables user_stp mode if that command returns
0, or enables kernel_stp mode if that command returns any other value.

VLAN
====

A LAN (Local Area Network) is a network that covers a small geographic area,
typically within a single building or a campus. LANs are used to connect
computers, servers, printers, and other networked devices within a localized
area. LANs can be wired (using Ethernet cables) or wireless (using Wi-Fi).

A VLAN (Virtual Local Area Network) is a logical segmentation of a physical
network into multiple isolated broadcast domains. VLANs are used to divide
a single physical LAN into multiple virtual LANs, allowing different groups of
devices to communicate as if they were on separate physical networks.

Typically there are two VLAN implementations, IEEE 802.1Q and IEEE 802.1ad
(also known as QinQ). IEEE 802.1Q is a standard for VLAN tagging in Ethernet
networks. It allows network administrators to create logical VLANs on a
physical network and tag Ethernet frames with VLAN information, which is
called *VLAN-tagged frames*. IEEE 802.1ad, commonly known as QinQ or Double
VLAN, is an extension of the IEEE 802.1Q standard. QinQ allows for the
stacking of multiple VLAN tags within a single Ethernet frame. The Linux
bridge supports both the IEEE 802.1Q and `802.1AD
<https://lore.kernel.org/netdev/1402401565-15423-1-git-send-email-makita.toshiaki@lab.ntt.co.jp/>`_
protocol for VLAN tagging.

`VLAN filtering <https://lore.kernel.org/netdev/1360792820-14116-1-git-send-email-vyasevic@redhat.com/>`_
on a bridge is disabled by default. After enabling VLAN filtering on a bridge,
it will start forwarding frames to appropriate destinations based on their
destination MAC address and VLAN tag (both must match).

Multicast
=========

The Linux bridge driver has multicast support allowing it to process Internet
Group Management Protocol (IGMP) or Multicast Listener Discovery (MLD)
messages, and to efficiently forward multicast data packets. The bridge
driver supports IGMPv2/IGMPv3 and MLDv1/MLDv2.

Multicast snooping
------------------

Multicast snooping is a networking technology that allows network switches
to intelligently manage multicast traffic within a local area network (LAN).

The switch maintains a multicast group table, which records the association
between multicast group addresses and the ports where hosts have joined these
groups. The group table is dynamically updated based on the IGMP/MLD messages
received. With the multicast group information gathered through snooping, the
switch optimizes the forwarding of multicast traffic. Instead of blindly
broadcasting the multicast traffic to all ports, it sends the multicast
traffic based on the destination MAC address only to ports which have
subscribed the respective destination multicast group.

When created, the Linux bridge devices have multicast snooping enabled by
default. It maintains a Multicast forwarding database (MDB) which keeps track
of port and group relationships.

IGMPv3/MLDv2 EHT support
------------------------

The Linux bridge supports IGMPv3/MLDv2 EHT (Explicit Host Tracking), which
was added by `474ddb37fa3a ("net: bridge: multicast: add EHT allow/block handling")
<https://lore.kernel.org/netdev/20210120145203.1109140-1-razor@blackwall.org/>`_

The explicit host tracking enables the device to keep track of each
individual host that is joined to a particular group or channel. The main
benefit of the explicit host tracking in IGMP is to allow minimal leave
latencies when a host leaves a multicast group or channel.

The length of time between a host wanting to leave and a device stopping
traffic forwarding is called the IGMP leave latency. A device configured
with IGMPv3 or MLDv2 and explicit tracking can immediately stop forwarding
traffic if the last host to request to receive traffic from the device
indicates that it no longer wants to receive traffic. The leave latency
is thus bound only by the packet transmission latencies in the multiaccess
network and the processing time in the device.

Other multicast features
------------------------

The Linux bridge also supports `per-VLAN multicast snooping
<https://lore.kernel.org/netdev/20210719170637.435541-1-razor@blackwall.org/>`_,
which is disabled by default but can be enabled. And `Multicast Router Discovery
<https://lore.kernel.org/netdev/20190121062628.2710-1-linus.luessing@c0d3.blue/>`_,
which help identify the location of multicast routers.

Switchdev
=========

Linux Bridge Switchdev is a feature in the Linux kernel that extends the
capabilities of the traditional Linux bridge to work more efficiently with
hardware switches that support switchdev. With Linux Bridge Switchdev, certain
networking functions like forwarding, filtering, and learning of Ethernet
frames can be offloaded to a hardware switch. This offloading reduces the
burden on the Linux kernel and CPU, leading to improved network performance
and lower latency.

To use Linux Bridge Switchdev, you need hardware switches that support the
switchdev interface. This means that the switch hardware needs to have the
necessary drivers and functionality to work in conjunction with the Linux
kernel.

Please see the :ref:`switchdev` document for more details.

Netfilter
=========

The bridge netfilter module is a legacy feature that allows to filter bridged
packets with iptables and ip6tables. Its use is discouraged. Users should
consider using nftables for packet filtering.

The older ebtables tool is more feature-limited compared to nftables, but
just like nftables it doesn't need this module either to function.

The br_netfilter module intercepts packets entering the bridge, performs
minimal sanity tests on ipv4 and ipv6 packets and then pretends that
these packets are being routed, not bridged. br_netfilter then calls
the ip and ipv6 netfilter hooks from the bridge layer, i.e. ip(6)tables
rulesets will also see these packets.

br_netfilter is also the reason for the iptables *physdev* match:
This match is the only way to reliably tell routed and bridged packets
apart in an iptables ruleset.

Note that ebtables and nftables will work fine without the br_netfilter module.
iptables/ip6tables/arptables do not work for bridged traffic because they
plug in the routing stack. nftables rules in ip/ip6/inet/arp families won't
see traffic that is forwarded by a bridge either, but that's very much how it
should be.

Historically the feature set of ebtables was very limited (it still is),
this module was added to pretend packets are routed and invoke the ipv4/ipv6
netfilter hooks from the bridge so users had access to the more feature-rich
iptables matching capabilities (including conntrack). nftables doesn't have
this limitation, pretty much all features work regardless of the protocol family.

So, br_netfilter is only needed if users, for some reason, need to use
ip(6)tables to filter packets forwarded by the bridge, or NAT bridged
traffic. For pure link layer filtering, this module isn't needed.

Other Features
==============

The Linux bridge also supports `IEEE 802.11 Proxy ARP
<https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/commit/?id=958501163ddd6ea22a98f94fa0e7ce6d4734e5c4>`_,
`Media Redundancy Protocol (MRP)
<https://lore.kernel.org/netdev/20200426132208.3232-1-horatiu.vultur@microchip.com/>`_,
`Media Redundancy Protocol (MRP) LC mode
<https://lore.kernel.org/r/20201124082525.273820-1-horatiu.vultur@microchip.com>`_,
`IEEE 802.1X port authentication
<https://lore.kernel.org/netdev/20220218155148.2329797-1-schultz.hans+netdev@gmail.com/>`_,
and `MAC Authentication Bypass (MAB)
<https://lore.kernel.org/netdev/20221101193922.2125323-2-idosch@nvidia.com/>`_.

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
