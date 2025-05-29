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
#include <net/strparser.h>

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
 * @hash_entry_id: entry in the peer ID hashtable
 * @hash_entry_addr4: entry in the peer IPv4 hashtable
 * @hash_entry_addr6: entry in the peer IPv6 hashtable
 * @hash_entry_transp_addr: entry in the peer transport address hashtable
 * @sock: the socket being used to talk to this peer
 * @tcp: keeps track of TCP specific state
 * @tcp.strp: stream parser context (TCP only)
 * @tcp.user_queue: received packets that have to go to userspace (TCP only)
 * @tcp.out_queue: packets on hold while socket is taken by user (TCP only)
 * @tcp.tx_in_progress: true if TX is already ongoing (TCP only)
 * @tcp.out_msg.skb: packet scheduled for sending (TCP only)
 * @tcp.out_msg.offset: offset where next send should start (TCP only)
 * @tcp.out_msg.len: remaining data to send within packet (TCP only)
 * @tcp.sk_cb.sk_data_ready: pointer to original cb (TCP only)
 * @tcp.sk_cb.sk_write_space: pointer to original cb (TCP only)
 * @tcp.sk_cb.prot: pointer to original prot object (TCP only)
 * @tcp.sk_cb.ops: pointer to the original prot_ops object (TCP only)
 * @crypto: the crypto configuration (ciphers, keys, etc..)
 * @dst_cache: cache for dst_entry used to send to peer
 * @bind: remote peer binding
 * @keepalive_interval: seconds after which a new keepalive should be sent
 * @keepalive_xmit_exp: future timestamp when next keepalive should be sent
 * @last_sent: timestamp of the last successfully sent packet
 * @keepalive_timeout: seconds after which an inactive peer is considered dead
 * @keepalive_recv_exp: future timestamp when the peer should expire
 * @last_recv: timestamp of the last authenticated received packet
 * @vpn_stats: per-peer in-VPN TX/RX stats
 * @link_stats: per-peer link/transport TX/RX stats
 * @delete_reason: why peer was deleted (i.e. timeout, transport error, ..)
 * @lock: protects binding to peer (bind) and keepalive* fields
 * @refcount: reference counter
 * @rcu: used to free peer in an RCU safe way
 * @release_entry: entry for the socket release list
 * @keepalive_work: used to schedule keepalive sending
 */
struct ovpn_peer {
	struct ovpn_priv *ovpn;
	netdevice_tracker dev_tracker;
	u32 id;
	struct {
		struct in_addr ipv4;
		struct in6_addr ipv6;
	} vpn_addrs;
	struct hlist_node hash_entry_id;
	struct hlist_nulls_node hash_entry_addr4;
	struct hlist_nulls_node hash_entry_addr6;
	struct hlist_nulls_node hash_entry_transp_addr;
	struct ovpn_socket __rcu *sock;

	struct {
		struct strparser strp;
		struct sk_buff_head user_queue;
		struct sk_buff_head out_queue;
		bool tx_in_progress;

		struct {
			struct sk_buff *skb;
			int offset;
			int len;
		} out_msg;

		struct {
			void (*sk_data_ready)(struct sock *sk);
			void (*sk_write_space)(struct sock *sk);
			struct proto *prot;
			const struct proto_ops *ops;
		} sk_cb;

		struct work_struct defer_del_work;
	} tcp;
	struct ovpn_crypto_state crypto;
	struct dst_cache dst_cache;
	struct ovpn_bind __rcu *bind;
	unsigned long keepalive_interval;
	unsigned long keepalive_xmit_exp;
	time64_t last_sent;
	unsigned long keepalive_timeout;
	unsigned long keepalive_recv_exp;
	time64_t last_recv;
	struct ovpn_peer_stats vpn_stats;
	struct ovpn_peer_stats link_stats;
	enum ovpn_del_peer_reason delete_reason;
	spinlock_t lock; /* protects bind  and keepalive* */
	struct kref refcount;
	struct rcu_head rcu;
	struct llist_node release_entry;
	struct work_struct keepalive_work;
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

void ovpn_peer_release(struct ovpn_peer *peer);
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
void ovpn_peers_free(struct ovpn_priv *ovpn, struct sock *sock,
		     enum ovpn_del_peer_reason reason);

struct ovpn_peer *ovpn_peer_get_by_transp_addr(struct ovpn_priv *ovpn,
					       struct sk_buff *skb);
struct ovpn_peer *ovpn_peer_get_by_id(struct ovpn_priv *ovpn, u32 peer_id);
struct ovpn_peer *ovpn_peer_get_by_dst(struct ovpn_priv *ovpn,
				       struct sk_buff *skb);
void ovpn_peer_hash_vpn_ip(struct ovpn_peer *peer);
bool ovpn_peer_check_by_src(struct ovpn_priv *ovpn, struct sk_buff *skb,
			    struct ovpn_peer *peer);

void ovpn_peer_keepalive_set(struct ovpn_peer *peer, u32 interval, u32 timeout);
void ovpn_peer_keepalive_work(struct work_struct *work);

void ovpn_peer_endpoints_update(struct ovpn_peer *peer, struct sk_buff *skb);
int ovpn_peer_reset_sockaddr(struct ovpn_peer *peer,
			     const struct sockaddr_storage *ss,
			     const void *local_ip);

#endif /* _NET_OVPN_OVPNPEER_H_ */
