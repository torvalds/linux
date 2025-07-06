/* SPDX-License-Identifier: GPL-2.0-only */
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2019-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 */

#ifndef _NET_OVPN_UDP_H_
#define _NET_OVPN_UDP_H_

#include <net/sock.h>

struct ovpn_peer;
struct ovpn_priv;
struct socket;

int ovpn_udp_socket_attach(struct ovpn_socket *ovpn_sock, struct socket *sock,
			   struct ovpn_priv *ovpn);
void ovpn_udp_socket_detach(struct ovpn_socket *ovpn_sock);

void ovpn_udp_send_skb(struct ovpn_peer *peer, struct sock *sk,
		       struct sk_buff *skb);

#endif /* _NET_OVPN_UDP_H_ */
