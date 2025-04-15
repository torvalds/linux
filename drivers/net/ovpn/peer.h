/* SPDX-License-Identifier: GPL-2.0-only */
/* OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#ifndef _NET_OVPN_OVPNPEER_H_
#define _NET_OVPN_OVPNPEER_H_

#include <net/dst_cache.h>

#include "crypto.h"
#include "socket.h"
#include "stats.h"

/**
 * struct ovpn_peer - the main remote peer object
 * @ovpn: main openvpn instance this peer belongs to
 * @dev_tracker: reference tracker for associated dev
 * @id: unique identifier
 * @vpn_addrs: IP addresses assigned over the tunnel
 * @vpn_addrs.ipv4: IPv4 assigned to peer on the tunnel
 * @vpn_addrs.ipv6: IPv6 assigned to peer on the tunnel
 * @sock: the socket being used to talk to this peer
 * @crypto: the crypto configuration (ciphers, keys, etc..)
 * @dst_cache: cache for dst_entry used to send to peer
 * @bind: remote peer binding
 * @vpn_stats: per-peer in-VPN TX/RX stats
 * @link_stats: per-peer link/transport TX/RX stats
 * @delete_reason: why peer was deleted (i.e. timeout, transport error, ..)
 * @lock: protects binding to peer (bind)
 * @refcount: reference counter
 * @rcu: used to free peer in an RCU safe way
 * @release_entry: entry for the socket release list
 */
struct ovpn_peer {
	struct ovpn_priv *ovpn;
	netdevice_tracker dev_tracker;
	u32 id;
	struct {
		struct in_addr ipv4;
		struct in6_addr ipv6;
	} vpn_addrs;
	struct ovpn_socket __rcu *sock;
	struct ovpn_crypto_state crypto;
	struct dst_cache dst_cache;
	struct ovpn_bind __rcu *bind;
	struct ovpn_peer_stats vpn_stats;
	struct ovpn_peer_stats link_stats;
	enum ovpn_del_peer_reason delete_reason;
	spinlock_t lock; /* protects bind */
	struct kref refcount;
	struct rcu_head rcu;
	struct llist_node release_entry;
};

/**
 * ovpn_peer_hold - increase reference counter
 * @peer: the peer whose counter should be increased
 *
 * Return: true if the counter was increased or false if it was zero already
 */
static inline bool ovpn_peer_hold(struct ovpn_peer *peer)
{
	return kref_get_unless_zero(&peer->refcount);
}

void ovpn_peer_release_kref(struct kref *kref);

/**
 * ovpn_peer_put - decrease reference counter
 * @peer: the peer whose counter should be decreased
 */
static inline void ovpn_peer_put(struct ovpn_peer *peer)
{
	kref_put(&peer->refcount, ovpn_peer_release_kref);
}

struct ovpn_peer *ovpn_peer_new(struct ovpn_priv *ovpn, u32 id);
int ovpn_peer_add(struct ovpn_priv *ovpn, struct ovpn_peer *peer);
int ovpn_peer_del(struct ovpn_peer *peer, enum ovpn_del_peer_reason reason);
void ovpn_peer_release_p2p(struct ovpn_priv *ovpn, struct sock *sk,
			   enum ovpn_del_peer_reason reason);

struct ovpn_peer *ovpn_peer_get_by_transp_addr(struct ovpn_priv *ovpn,
					       struct sk_buff *skb);
struct ovpn_peer *ovpn_peer_get_by_id(struct ovpn_priv *ovpn, u32 peer_id);
struct ovpn_peer *ovpn_peer_get_by_dst(struct ovpn_priv *ovpn,
				       struct sk_buff *skb);
bool ovpn_peer_check_by_src(struct ovpn_priv *ovpn, struct sk_buff *skb,
			    struct ovpn_peer *peer);

#endif /* _NET_OVPN_OVPNPEER_H_ */
