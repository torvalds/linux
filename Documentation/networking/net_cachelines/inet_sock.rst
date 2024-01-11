.. SPDX-License-Identifier: GPL-2.0
.. Copyright (C) 2023 Google LLC

=====================================================
inet_connection_sock struct fast path usage breakdown
=====================================================

Type                    Name                  fastpath_tx_access  fastpath_rx_access  comment
..struct                ..inet_sock                                                     
struct_sock             sk                    read_mostly         read_mostly         tcp_init_buffer_space,tcp_init_transfer,tcp_finish_connect,tcp_connect,tcp_send_rcvq,tcp_send_syn_data
struct_ipv6_pinfo*      pinet6                -                   -                   
be16                    inet_sport            read_mostly         -                   __tcp_transmit_skb
be32                    inet_daddr            read_mostly         -                   ip_select_ident_segs
be32                    inet_rcv_saddr        -                   -                   
be16                    inet_dport            read_mostly         -                   __tcp_transmit_skb
u16                     inet_num              -                   -                   
be32                    inet_saddr            -                   -                   
s16                     uc_ttl                read_mostly         -                   __ip_queue_xmit/ip_select_ttl
u16                     cmsg_flags            -                   -                   
struct_ip_options_rcu*  inet_opt              read_mostly         -                   __ip_queue_xmit
u16                     inet_id               read_mostly         -                   ip_select_ident_segs
u8                      tos                   read_mostly         -                   ip_queue_xmit
u8                      min_ttl               -                   -                   
u8                      mc_ttl                -                   -                   
u8                      pmtudisc              -                   -                   
u8:1                    recverr               -                   -                   
u8:1                    is_icsk               -                   -                   
u8:1                    freebind              -                   -                   
u8:1                    hdrincl               -                   -                   
u8:1                    mc_loop               -                   -                   
u8:1                    transparent           -                   -                   
u8:1                    mc_all                -                   -                   
u8:1                    nodefrag              -                   -                   
u8:1                    bind_address_no_port  -                   -                   
u8:1                    recverr_rfc4884       -                   -                   
u8:1                    defer_connect         read_mostly         -                   tcp_sendmsg_fastopen
u8                      rcv_tos               -                   -                   
u8                      convert_csum          -                   -                   
int                     uc_index              -                   -                   
int                     mc_index              -                   -                   
be32                    mc_addr               -                   -                   
struct_ip_mc_socklist*  mc_list               -                   -                   
struct_inet_cork_full   cork                  read_mostly         -                   __tcp_transmit_skb
struct                  local_port_range      -                   -                   
