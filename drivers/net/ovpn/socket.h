/* SPDX-License-Identifier: GPL-2.0-only */
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#ifndef _NET_OVPN_SOCK_H_
#define _NET_OVPN_SOCK_H_

#include <linux/net.h>
#include <linux/kref.h>
#include <net/sock.h>

struct ovpn_priv;
struct ovpn_peer;

/**
 * struct ovpn_socket - a kernel socket referenced in the ovpn code
 * @ovpn: ovpn instance owning this socket (UDP only)
 * @dev_tracker: reference tracker for associated dev (UDP only)
 * @peer: unique peer transmitting over this socket (TCP only)
 * @sk: the low level sock object
 * @refcount: amount of contexts currently referencing this object
 * @work: member used to schedule release routine (it may block)
 * @tcp_tx_work: work for deferring outgoing packet processing (TCP only)
 */
struct ovpn_socket {
	union {
		struct {
			struct ovpn_priv *ovpn;
			netdevice_tracker dev_tracker;
		};
		struct ovpn_peer *peer;
	};

	struct sock *sk;
	struct kref refcount;
	struct work_struct work;
	struct work_struct tcp_tx_work;
};

struct ovpn_socket *ovpn_socket_new(struct socket *sock,
				    struct ovpn_peer *peer);
void ovpn_socket_release(struct ovpn_peer *peer);

#endif /* _NET_OVPN_SOCK_H_ */
