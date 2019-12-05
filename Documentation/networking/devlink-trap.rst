.. SPDX-License-Identifier: GPL-2.0

============
Devlink Trap
============

Background
==========

Devices capable of offloading the kernel's datapath and perform functions such
as bridging and routing must also be able to send specific packets to the
kernel (i.e., the CPU) for processing.

For example, a device acting as a multicast-aware bridge must be able to send
IGMP membership reports to the kernel for processing by the bridge module.
Without processing such packets, the bridge module could never populate its
MDB.

As another example, consider a device acting as router which has received an IP
packet with a TTL of 1. Upon routing the packet the device must send it to the
kernel so that it will route it as well and generate an ICMP Time Exceeded
error datagram. Without letting the kernel route such packets itself, utilities
such as ``traceroute`` could never work.

The fundamental ability of sending certain packets to the kernel for processing
is called "packet trapping".

Overview
========

The ``devlink-trap`` mechanism allows capable device drivers to register their
supported packet traps with ``devlink`` and report trapped packets to
``devlink`` for further analysis.

Upon receiving trapped packets, ``devlink`` will perform a per-trap packets and
bytes accounting and potentially report the packet to user space via a netlink
event along with all the provided metadata (e.g., trap reason, timestamp, input
port). This is especially useful for drop traps (see :ref:`Trap-Types`)
as it allows users to obtain further visibility into packet drops that would
otherwise be invisible.

The following diagram provides a general overview of ``devlink-trap``::

                                    Netlink event: Packet w/ metadata
                                                   Or a summary of recent drops
                                  ^
                                  |
         Userspace                |
        +---------------------------------------------------+
         Kernel                   |
                                  |
                          +-------+--------+
                          |                |
                          |  drop_monitor  |
                          |                |
                          +-------^--------+
                                  |
                                  |
                                  |
                             +----+----+
                             |         |      Kernel's Rx path
                             | devlink |      (non-drop traps)
                             |         |
                             +----^----+      ^
                                  |           |
                                  +-----------+
                                  |
                          +-------+-------+
                          |               |
                          | Device driver |
                          |               |
                          +-------^-------+
         Kernel                   |
        +---------------------------------------------------+
         Hardware                 |
                                  | Trapped packet
                                  |
                               +--+---+
                               |      |
                               | ASIC |
                               |      |
                               +------+

.. _Trap-Types:

Trap Types
==========

The ``devlink-trap`` mechanism supports the following packet trap types:

  * ``drop``: Trapped packets were dropped by the underlying device. Packets
    are only processed by ``devlink`` and not injected to the kernel's Rx path.
    The trap action (see :ref:`Trap-Actions`) can be changed.
  * ``exception``: Trapped packets were not forwarded as intended by the
    underlying device due to an exception (e.g., TTL error, missing neighbour
    entry) and trapped to the control plane for resolution. Packets are
    processed by ``devlink`` and injected to the kernel's Rx path. Changing the
    action of such traps is not allowed, as it can easily break the control
    plane.

.. _Trap-Actions:

Trap Actions
============

The ``devlink-trap`` mechanism supports the following packet trap actions:

  * ``trap``: The sole copy of the packet is sent to the CPU.
  * ``drop``: The packet is dropped by the underlying device and a copy is not
    sent to the CPU.

Generic Packet Traps
====================

Generic packet traps are used to describe traps that trap well-defined packets
or packets that are trapped due to well-defined conditions (e.g., TTL error).
Such traps can be shared by multiple device drivers and their description must
be added to the following table:

.. list-table:: List of Generic Packet Traps
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``source_mac_is_multicast``
     - ``drop``
     - Traps incoming packets that the device decided to drop because of a
       multicast source MAC
   * - ``vlan_tag_mismatch``
     - ``drop``
     - Traps incoming packets that the device decided to drop in case of VLAN
       tag mismatch: The ingress bridge port is not configured with a PVID and
       the packet is untagged or prio-tagged
   * - ``ingress_vlan_filter``
     - ``drop``
     - Traps incoming packets that the device decided to drop in case they are
       tagged with a VLAN that is not configured on the ingress bridge port
   * - ``ingress_spanning_tree_filter``
     - ``drop``
     - Traps incoming packets that the device decided to drop in case the STP
       state of the ingress bridge port is not "forwarding"
   * - ``port_list_is_empty``
     - ``drop``
     - Traps packets that the device decided to drop in case they need to be
       flooded (e.g., unknown unicast, unregistered multicast) and there are
       no ports the packets should be flooded to
   * - ``port_loopback_filter``
     - ``drop``
     - Traps packets that the device decided to drop in case after layer 2
       forwarding the only port from which they should be transmitted through
       is the port from which they were received
   * - ``blackhole_route``
     - ``drop``
     - Traps packets that the device decided to drop in case they hit a
       blackhole route
   * - ``ttl_value_is_too_small``
     - ``exception``
     - Traps unicast packets that should be forwarded by the device whose TTL
       was decremented to 0 or less
   * - ``tail_drop``
     - ``drop``
     - Traps packets that the device decided to drop because they could not be
       enqueued to a transmission queue which is full
   * - ``non_ip``
     - ``drop``
     - Traps packets that the device decided to drop because they need to
       undergo a layer 3 lookup, but are not IP or MPLS packets
   * - ``uc_dip_over_mc_dmac``
     - ``drop``
     - Traps packets that the device decided to drop because they need to be
       routed and they have a unicast destination IP and a multicast destination
       MAC
   * - ``dip_is_loopback_address``
     - ``drop``
     - Traps packets that the device decided to drop because they need to be
       routed and their destination IP is the loopback address (i.e., 127.0.0.0/8
       and ::1/128)
   * - ``sip_is_mc``
     - ``drop``
     - Traps packets that the device decided to drop because they need to be
       routed and their source IP is multicast (i.e., 224.0.0.0/8 and ff::/8)
   * - ``sip_is_loopback_address``
     - ``drop``
     - Traps packets that the device decided to drop because they need to be
       routed and their source IP is the loopback address (i.e., 127.0.0.0/8 and ::1/128)
   * - ``ip_header_corrupted``
     - ``drop``
     - Traps packets that the device decided to drop because they need to be
       routed and their IP header is corrupted: wrong checksum, wrong IP version
       or too short Internet Header Length (IHL)
   * - ``ipv4_sip_is_limited_bc``
     - ``drop``
     - Traps packets that the device decided to drop because they need to be
       routed and their source IP is limited broadcast (i.e., 255.255.255.255/32)
   * - ``ipv6_mc_dip_reserved_scope``
     - ``drop``
     - Traps IPv6 packets that the device decided to drop because they need to
       be routed and their IPv6 multicast destination IP has a reserved scope
       (i.e., ffx0::/16)
   * - ``ipv6_mc_dip_interface_local_scope``
     - ``drop``
     - Traps IPv6 packets that the device decided to drop because they need to
       be routed and their IPv6 multicast destination IP has an interface-local scope
       (i.e., ffx1::/16)
   * - ``mtu_value_is_too_small``
     - ``exception``
     - Traps packets that should have been routed by the device, but were bigger
       than the MTU of the egress interface
   * - ``unresolved_neigh``
     - ``exception``
     - Traps packets that did not have a matching IP neighbour after routing
   * - ``mc_reverse_path_forwarding``
     - ``exception``
     - Traps multicast IP packets that failed reverse-path forwarding (RPF)
       check during multicast routing
   * - ``reject_route``
     - ``exception``
     - Traps packets that hit reject routes (i.e., "unreachable", "prohibit")
   * - ``ipv4_lpm_miss``
     - ``exception``
     - Traps unicast IPv4 packets that did not match any route
   * - ``ipv6_lpm_miss``
     - ``exception``
     - Traps unicast IPv6 packets that did not match any route

Driver-specific Packet Traps
============================

Device drivers can register driver-specific packet traps, but these must be
clearly documented. Such traps can correspond to device-specific exceptions and
help debug packet drops caused by these exceptions. The following list includes
links to the description of driver-specific traps registered by various device
drivers:

  * :doc:`devlink-trap-netdevsim`

Generic Packet Trap Groups
==========================

Generic packet trap groups are used to aggregate logically related packet
traps. These groups allow the user to batch operations such as setting the trap
action of all member traps. In addition, ``devlink-trap`` can report aggregated
per-group packets and bytes statistics, in case per-trap statistics are too
narrow. The description of these groups must be added to the following table:

.. list-table:: List of Generic Packet Trap Groups
   :widths: 10 90

   * - Name
     - Description
   * - ``l2_drops``
     - Contains packet traps for packets that were dropped by the device during
       layer 2 forwarding (i.e., bridge)
   * - ``l3_drops``
     - Contains packet traps for packets that were dropped by the device or hit
       an exception (e.g., TTL error) during layer 3 forwarding
   * - ``buffer_drops``
     - Contains packet traps for packets that were dropped by the device due to
       an enqueue decision

Testing
=======

See ``tools/testing/selftests/drivers/net/netdevsim/devlink_trap.sh`` for a
test covering the core infrastructure. Test cases should be added for any new
functionality.

Device drivers should focus their tests on device-specific functionality, such
as the triggering of supported packet traps.
