.. SPDX-License-Identifier: GPL-2.0

========================================
Bare UDP Tunnelling Module Documentation
========================================

There are various L3 encapsulation standards using UDP being discussed to
leverage the UDP based load balancing capability of different networks.
MPLSoUDP (https://tools.ietf.org/html/rfc7510) is one among them.

The Bareudp tunnel module provides a generic L3 encapsulation support for
tunnelling different L3 protocols like MPLS, IP, NSH etc. inside a UDP tunnel.

Special Handling
----------------

The bareudp device supports special handling for MPLS & IP as they can have
multiple ethertypes.
The MPLS protocol can have ethertypes ETH_P_MPLS_UC (unicast) & ETH_P_MPLS_MC (multicast).
IP protocol can have ethertypes ETH_P_IP (v4) & ETH_P_IPV6 (v6).
This special handling can be enabled only for ethertypes ETH_P_IP & ETH_P_MPLS_UC
with a flag called multiproto mode.

Usage
------

1) Device creation & deletion

    a) ip link add dev bareudp0 type bareudp dstport 6635 ethertype mpls_uc

       This creates a bareudp tunnel device which tunnels L3 traffic with ethertype
       0x8847 (MPLS traffic). The destination port of the UDP header will be set to
       6635.The device will listen on UDP port 6635 to receive traffic.

    b) ip link delete bareudp0

2) Device creation with multiproto mode enabled

The multiproto mode allows bareudp tunnels to handle several protocols of the
same family. It is currently only available for IP and MPLS. This mode has to
be enabled explicitly with the "multiproto" flag.

    a) ip link add dev bareudp0 type bareudp dstport 6635 ethertype ipv4 multiproto

       For an IPv4 tunnel the multiproto mode allows the tunnel to also handle
       IPv6.

    b) ip link add dev bareudp0 type bareudp dstport 6635 ethertype mpls_uc multiproto

       For MPLS, the multiproto mode allows the tunnel to handle both unicast
       and multicast MPLS packets.

3) Device Usage

The bareudp device could be used along with OVS or flower filter in TC.
The OVS or TC flower layer must set the tunnel information in the SKB dst field before
sending the packet buffer to the bareudp device for transmission. On reception, the
bareUDP device extracts and stores the tunnel information in the SKB dst field before
passing the packet buffer to the network stack.
