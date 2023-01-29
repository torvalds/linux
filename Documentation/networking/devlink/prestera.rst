.. SPDX-License-Identifier: GPL-2.0

========================
prestera devlink support
========================

This document describes the devlink features implemented by the ``prestera``
device driver.

Driver-specific Traps
=====================

.. list-table:: List of Driver-specific Traps Registered by ``prestera``
   :widths: 5 5 90

   * - Name
     - Type
     - Description
.. list-table:: List of Driver-specific Traps Registered by ``prestera``
   :widths: 5 5 90

   * - Name
     - Type
     - Description
   * - ``arp_bc``
     - ``trap``
     - Traps ARP broadcast packets (both requests/responses)
   * - ``is_is``
     - ``trap``
     - Traps IS-IS packets
   * - ``ospf``
     - ``trap``
     - Traps OSPF packets
   * - ``ip_bc_mac``
     - ``trap``
     - Traps IPv4 packets with broadcast DA Mac address
   * - ``stp``
     - ``trap``
     - Traps STP BPDU
   * - ``lacp``
     - ``trap``
     - Traps LACP packets
   * - ``lldp``
     - ``trap``
     - Traps LLDP packets
   * - ``router_mc``
     - ``trap``
     - Traps multicast packets
   * - ``vrrp``
     - ``trap``
     - Traps VRRP packets
   * - ``dhcp``
     - ``trap``
     - Traps DHCP packets
   * - ``mtu_error``
     - ``trap``
     - Traps (exception) packets that exceeded port's MTU
   * - ``mac_to_me``
     - ``trap``
     -  Traps packets with switch-port's DA Mac address
   * - ``ttl_error``
     - ``trap``
     - Traps (exception) IPv4 packets whose TTL exceeded
   * - ``ipv4_options``
     - ``trap``
     - Traps (exception) packets due to the malformed IPV4 header options
   * - ``ip_default_route``
     - ``trap``
     - Traps packets that have no specific IP interface (IP to me) and no forwarding prefix
   * - ``local_route``
     - ``trap``
     - Traps packets that have been send to one of switch IP interfaces addresses
   * - ``ipv4_icmp_redirect``
     - ``trap``
     - Traps (exception) IPV4 ICMP redirect packets
   * - ``arp_response``
     - ``trap``
     - Traps ARP replies packets that have switch-port's DA Mac address
   * - ``acl_code_0``
     - ``trap``
     - Traps packets that have ACL priority set to 0 (tc pref 0)
   * - ``acl_code_1``
     - ``trap``
     - Traps packets that have ACL priority set to 1 (tc pref 1)
   * - ``acl_code_2``
     - ``trap``
     - Traps packets that have ACL priority set to 2 (tc pref 2)
   * - ``acl_code_3``
     - ``trap``
     - Traps packets that have ACL priority set to 3 (tc pref 3)
   * - ``acl_code_4``
     - ``trap``
     - Traps packets that have ACL priority set to 4 (tc pref 4)
   * - ``acl_code_5``
     - ``trap``
     - Traps packets that have ACL priority set to 5 (tc pref 5)
   * - ``acl_code_6``
     - ``trap``
     - Traps packets that have ACL priority set to 6 (tc pref 6)
   * - ``acl_code_7``
     - ``trap``
     - Traps packets that have ACL priority set to 7 (tc pref 7)
   * - ``ipv4_bgp``
     - ``trap``
     - Traps IPv4 BGP packets
   * - ``ssh``
     - ``trap``
     - Traps SSH packets
   * - ``telnet``
     - ``trap``
     - Traps Telnet packets
   * - ``icmp``
     - ``trap``
     - Traps ICMP packets
   * - ``rxdma_drop``
     - ``drop``
     - Drops packets (RxDMA) due to the lack of ingress buffers etc.
   * - ``port_no_vlan``
     - ``drop``
     - Drops packets due to faulty-configured network or due to internal bug (config issue).
   * - ``local_port``
     - ``drop``
     - Drops packets whose decision (FDB entry) is to bridge packet back to the incoming port/trunk.
   * - ``invalid_sa``
     - ``drop``
     - Drops packets with multicast source MAC address.
   * - ``illegal_ip_addr``
     - ``drop``
     - Drops packets with illegal SIP/DIP multicast/unicast addresses.
   * - ``illegal_ipv4_hdr``
     - ``drop``
     - Drops packets with illegal IPV4 header.
   * - ``ip_uc_dip_da_mismatch``
     - ``drop``
     - Drops packets with destination MAC being unicast, but destination IP address being multicast.
   * - ``ip_sip_is_zero``
     - ``drop``
     - Drops packets with zero (0) IPV4 source address.
   * - ``met_red``
     - ``drop``
     - Drops non-conforming packets (dropped by Ingress policer, metering drop), e.g. packet rate exceeded configured bandwidth.
