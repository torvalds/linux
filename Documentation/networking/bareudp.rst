.. SPDX-License-Identifier: GPL-2.0

========================================
Bare UDP Tunnelling Module Documentation
========================================

There are various L3 encapsulation standards using UDP being discussed to
leverage the UDP based load balancing capability of different networks.
MPLSoUDP (__ https://tools.ietf.org/html/rfc7510) is one among them.

The Bareudp tunnel module provides a generic L3 encapsulation tunnelling
support for tunnelling different L3 protocols like MPLS, IP, NSH etc. inside
a UDP tunnel.

Special Handling
----------------
The bareudp device supports special handling for MPLS & IP as they can have
multiple ethertypes.
MPLS procotcol can have ethertypes ETH_P_MPLS_UC  (unicast) & ETH_P_MPLS_MC (multicast).
IP protocol can have ethertypes ETH_P_IP (v4) & ETH_P_IPV6 (v6).
This special handling can be enabled only for ethertypes ETH_P_IP & ETH_P_MPLS_UC
with a flag called multiproto mode.

Usage
------

1) Device creation & deletion

    a) ip link add dev bareudp0 type bareudp dstport 6635 ethertype 0x8847.

       This creates a bareudp tunnel device which tunnels L3 traffic with ethertype
       0x8847 (MPLS traffic). The destination port of the UDP header will be set to
       6635.The device will listen on UDP port 6635 to receive traffic.

    b) ip link delete bareudp0

2) Device creation with multiple proto mode enabled

There are two ways to create a bareudp device for MPLS & IP with multiproto mode
enabled.

    a) ip link add dev  bareudp0 type bareudp dstport 6635 ethertype 0x8847 multiproto

    b) ip link add dev  bareudp0 type bareudp dstport 6635 ethertype mpls

3) Device Usage

The bareudp device could be used along with OVS or flower filter in TC.
The OVS or TC flower layer must set the tunnel information in SKB dst field before
sending packet buffer to the bareudp device for transmission. On reception the
bareudp device extracts and stores the tunnel information in SKB dst field before
passing the packet buffer to the network stack.
