// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	James Yonan <james@openvpn.net>
 *		Antonio Quartulli <antonio@openvpn.net>
 */

#include <linux/skbuff.h>
#include <linux/list.h>

#include "ovpnpriv.h"
#include "bind.h"
#include "io.h"
#include "main.h"
#include "netlink.h"
#include "peer.h"
#include "socket.h"

static void unlock_ovpn(struct ovpn_priv *ovpn,
			 struct llist_head *release_list)
	__releases(&ovpn->lock)
{
	struct ovpn_peer *peer;

	spin_unlock_bh(&ovpn->lock);

	llist_for_each_entry(peer, release_list->first, release_entry) {
		ovpn_socket_release(peer);
		ovpn_peer_put(peer);
	}
}

/**
 * ovpn_peer_new - allocate and initialize a new peer object
 * @ovpn: the openvpn instance inside which the peer should be created
 * @id: the ID assigned to this peer
 *
 * Return: a pointer to the new peer on success or an error code otherwise
 */
struct ovpn_peer *ovpn_peer_new(struct ovpn_priv *ovpn, u32 id)
{
	struct ovpn_peer *peer;
	int ret;

	/* alloc and init peer object */
	peer = kzalloc(sizeof(*peer), GFP_KERNEL);
	if (!peer)
		return ERR_PTR(-ENOMEM);

	peer->id = id;
	peer->ovpn = ovpn;

	peer->vpn_addrs.ipv4.s_addr = htonl(INADDR_ANY);
	peer->vpn_addrs.ipv6 = in6addr_any;

	RCU_INIT_POINTER(peer->bind, NULL);
	spin_lock_init(&peer->lock);
	kref_init(&peer->refcount);

	ret = dst_cache_init(&peer->dst_cache, GFP_KERNEL);
	if (ret < 0) {
		netdev_err(ovpn->dev,
			   "cannot initialize dst cache for peer %u\n",
			   peer->id);
		kfree(peer);
		return ERR_PTR(ret);
	}

	netdev_hold(ovpn->dev, &peer->dev_tracker, GFP_KERNEL);

	return peer;
}

/**
 * ovpn_peer_release_rcu - RCU callback performing last peer release steps
 * @head: RCU member of the ovpn_peer
 */
static void ovpn_peer_release_rcu(struct rcu_head *head)
{
	struct ovpn_peer *peer = container_of(head, struct ovpn_peer, rcu);

	/* this call will immediately free the dst_cache, therefore we
	 * perform it in the RCU callback, when all contexts are done
	 */
	dst_cache_destroy(&peer->dst_cache);
	kfree(peer);
}

/**
 * ovpn_peer_release - release peer private members
 * @peer: the peer to release
 */
static void ovpn_peer_release(struct ovpn_peer *peer)
{
	ovpn_bind_reset(peer, NULL);
	call_rcu(&peer->rcu, ovpn_peer_release_rcu);
	netdev_put(peer->ovpn->dev, &peer->dev_tracker);
}

/**
 * ovpn_peer_release_kref - callback for kref_put
 * @kref: the kref object belonging to the peer
 */
void ovpn_peer_release_kref(struct kref *kref)
{
	struct ovpn_peer *peer = container_of(kref, struct ovpn_peer, refcount);

	ovpn_peer_release(peer);
}

/**
 * ovpn_peer_skb_to_sockaddr - fill sockaddr with skb source address
 * @skb: the packet to extract data from
 * @ss: the sockaddr to fill
 *
 * Return: sockaddr length on success or -1 otherwise
 */
static int ovpn_peer_skb_to_sockaddr(struct sk_buff *skb,
				     struct sockaddr_storage *ss)
{
	struct sockaddr_in6 *sa6;
	struct sockaddr_in *sa4;

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		sa4 = (struct sockaddr_in *)ss;
		sa4->sin_family = AF_INET;
		sa4->sin_addr.s_addr = ip_hdr(skb)->saddr;
		sa4->sin_port = udp_hdr(skb)->source;
		return sizeof(*sa4);
	case htons(ETH_P_IPV6):
		sa6 = (struct sockaddr_in6 *)ss;
		sa6->sin6_family = AF_INET6;
		sa6->sin6_addr = ipv6_hdr(skb)->saddr;
		sa6->sin6_port = udp_hdr(skb)->source;
		return sizeof(*sa6);
	}

	return -1;
}

/**
 * ovpn_peer_transp_match - check if sockaddr and peer binding match
 * @peer: the peer to get the binding from
 * @ss: the sockaddr to match
 *
 * Return: true if sockaddr and binding match or false otherwise
 */
static bool ovpn_peer_transp_match(const struct ovpn_peer *peer,
				   const struct sockaddr_storage *ss)
{
	struct ovpn_bind *bind = rcu_dereference(peer->bind);
	struct sockaddr_in6 *sa6;
	struct sockaddr_in *sa4;

	if (unlikely(!bind))
		return false;

	if (ss->ss_family != bind->remote.in4.sin_family)
		return false;

	switch (ss->ss_family) {
	case AF_INET:
		sa4 = (struct sockaddr_in *)ss;
		if (sa4->sin_addr.s_addr != bind->remote.in4.sin_addr.s_addr)
			return false;
		if (sa4->sin_port != bind->remote.in4.sin_port)
			return false;
		break;
	case AF_INET6:
		sa6 = (struct sockaddr_in6 *)ss;
		if (!ipv6_addr_equal(&sa6->sin6_addr,
				     &bind->remote.in6.sin6_addr))
			return false;
		if (sa6->sin6_port != bind->remote.in6.sin6_port)
			return false;
		break;
	default:
		return false;
	}

	return true;
}

/**
 * ovpn_peer_get_by_transp_addr_p2p - get peer by transport address in a P2P
 *                                    instance
 * @ovpn: the openvpn instance to search
 * @ss: the transport socket address
 *
 * Return: the peer if found or NULL otherwise
 */
static struct ovpn_peer *
ovpn_peer_get_by_transp_addr_p2p(struct ovpn_priv *ovpn,
				 struct sockaddr_storage *ss)
{
	struct ovpn_peer *tmp, *peer = NULL;

	rcu_read_lock();
	tmp = rcu_dereference(ovpn->peer);
	if (likely(tmp && ovpn_peer_transp_match(tmp, ss) &&
		   ovpn_peer_hold(tmp)))
		peer = tmp;
	rcu_read_unlock();

	return peer;
}

/**
 * ovpn_peer_get_by_transp_addr - retrieve peer by transport address
 * @ovpn: the openvpn instance to search
 * @skb: the skb to retrieve the source transport address from
 *
 * Return: a pointer to the peer if found or NULL otherwise
 */
struct ovpn_peer *ovpn_peer_get_by_transp_addr(struct ovpn_priv *ovpn,
					       struct sk_buff *skb)
{
	struct ovpn_peer *peer = NULL;
	struct sockaddr_storage ss = { 0 };

	if (unlikely(!ovpn_peer_skb_to_sockaddr(skb, &ss)))
		return NULL;

	if (ovpn->mode == OVPN_MODE_P2P)
		peer = ovpn_peer_get_by_transp_addr_p2p(ovpn, &ss);

	return peer;
}

/**
 * ovpn_peer_get_by_id_p2p - get peer by ID in a P2P instance
 * @ovpn: the openvpn instance to search
 * @peer_id: the ID of the peer to find
 *
 * Return: the peer if found or NULL otherwise
 */
static struct ovpn_peer *ovpn_peer_get_by_id_p2p(struct ovpn_priv *ovpn,
						 u32 peer_id)
{
	struct ovpn_peer *tmp, *peer = NULL;

	rcu_read_lock();
	tmp = rcu_dereference(ovpn->peer);
	if (likely(tmp && tmp->id == peer_id && ovpn_peer_hold(tmp)))
		peer = tmp;
	rcu_read_unlock();

	return peer;
}

/**
 * ovpn_peer_get_by_id - retrieve peer by ID
 * @ovpn: the openvpn instance to search
 * @peer_id: the unique peer identifier to match
 *
 * Return: a pointer to the peer if found or NULL otherwise
 */
struct ovpn_peer *ovpn_peer_get_by_id(struct ovpn_priv *ovpn, u32 peer_id)
{
	struct ovpn_peer *peer = NULL;

	if (ovpn->mode == OVPN_MODE_P2P)
		peer = ovpn_peer_get_by_id_p2p(ovpn, peer_id);

	return peer;
}

static void ovpn_peer_remove(struct ovpn_peer *peer,
			     enum ovpn_del_peer_reason reason,
			     struct llist_head *release_list)
{
	switch (peer->ovpn->mode) {
	case OVPN_MODE_P2P:
		/* prevent double remove */
		if (peer != rcu_access_pointer(peer->ovpn->peer))
			return;

		RCU_INIT_POINTER(peer->ovpn->peer, NULL);
		/* in P2P mode the carrier is switched off when the peer is
		 * deleted so that third party protocols can react accordingly
		 */
		netif_carrier_off(peer->ovpn->dev);
		break;
	default:
		return;
	}

	peer->delete_reason = reason;

	/* append to provided list for later socket release and ref drop */
	llist_add(&peer->release_entry, release_list);
}

/**
 * ovpn_peer_get_by_dst - Lookup peer to send skb to
 * @ovpn: the private data representing the current VPN session
 * @skb: the skb to extract the destination address from
 *
 * This function takes a tunnel packet and looks up the peer to send it to
 * after encapsulation. The skb is expected to be the in-tunnel packet, without
 * any OpenVPN related header.
 *
 * Assume that the IP header is accessible in the skb data.
 *
 * Return: the peer if found or NULL otherwise.
 */
struct ovpn_peer *ovpn_peer_get_by_dst(struct ovpn_priv *ovpn,
				       struct sk_buff *skb)
{
	struct ovpn_peer *peer = NULL;

	/* in P2P mode, no matter the destination, packets are always sent to
	 * the single peer listening on the other side
	 */
	if (ovpn->mode == OVPN_MODE_P2P) {
		rcu_read_lock();
		peer = rcu_dereference(ovpn->peer);
		if (unlikely(peer && !ovpn_peer_hold(peer)))
			peer = NULL;
		rcu_read_unlock();
	}

	return peer;
}

/**
 * ovpn_peer_add_p2p - add peer to related tables in a P2P instance
 * @ovpn: the instance to add the peer to
 * @peer: the peer to add
 *
 * Return: 0 on success or a negative error code otherwise
 */
static int ovpn_peer_add_p2p(struct ovpn_priv *ovpn, struct ovpn_peer *peer)
{
	LLIST_HEAD(release_list);
	struct ovpn_peer *tmp;

	spin_lock_bh(&ovpn->lock);
	/* in p2p mode it is possible to have a single peer only, therefore the
	 * old one is released and substituted by the new one
	 */
	tmp = rcu_dereference_protected(ovpn->peer,
					lockdep_is_held(&ovpn->lock));
	if (tmp)
		ovpn_peer_remove(tmp, OVPN_DEL_PEER_REASON_TEARDOWN,
				 &release_list);

	rcu_assign_pointer(ovpn->peer, peer);
	/* in P2P mode the carrier is switched on when the peer is added */
	netif_carrier_on(ovpn->dev);
	unlock_ovpn(ovpn, &release_list);

	return 0;
}

/**
 * ovpn_peer_add - add peer to the related tables
 * @ovpn: the openvpn instance the peer belongs to
 * @peer: the peer object to add
 *
 * Assume refcounter was increased by caller
 *
 * Return: 0 on success or a negative error code otherwise
 */
int ovpn_peer_add(struct ovpn_priv *ovpn, struct ovpn_peer *peer)
{
	switch (ovpn->mode) {
	case OVPN_MODE_P2P:
		return ovpn_peer_add_p2p(ovpn, peer);
	default:
		return -EOPNOTSUPP;
	}
}

/**
 * ovpn_peer_del_p2p - delete peer from related tables in a P2P instance
 * @peer: the peer to delete
 * @reason: reason why the peer was deleted (sent to userspace)
 * @release_list: list where delete peer should be appended
 *
 * Return: 0 on success or a negative error code otherwise
 */
static int ovpn_peer_del_p2p(struct ovpn_peer *peer,
			     enum ovpn_del_peer_reason reason,
			     struct llist_head *release_list)
{
	struct ovpn_peer *tmp;

	lockdep_assert_held(&peer->ovpn->lock);

	tmp = rcu_dereference_protected(peer->ovpn->peer,
					lockdep_is_held(&peer->ovpn->lock));
	if (tmp != peer)
		return -ENOENT;

	ovpn_peer_remove(peer, reason, release_list);

	return 0;
}

/**
 * ovpn_peer_del - delete peer from related tables
 * @peer: the peer object to delete
 * @reason: reason for deleting peer (will be sent to userspace)
 *
 * Return: 0 on success or a negative error code otherwise
 */
int ovpn_peer_del(struct ovpn_peer *peer, enum ovpn_del_peer_reason reason)
{
	LLIST_HEAD(release_list);
	int ret = -EOPNOTSUPP;

	spin_lock_bh(&peer->ovpn->lock);
	switch (peer->ovpn->mode) {
	case OVPN_MODE_P2P:
		ret = ovpn_peer_del_p2p(peer, reason, &release_list);
		break;
	default:
		break;
	}
	unlock_ovpn(peer->ovpn, &release_list);

	return ret;
}

/**
 * ovpn_peer_release_p2p - release peer upon P2P device teardown
 * @ovpn: the instance being torn down
 * @sk: if not NULL, release peer only if it's using this specific socket
 * @reason: the reason for releasing the peer
 */
void ovpn_peer_release_p2p(struct ovpn_priv *ovpn, struct sock *sk,
			   enum ovpn_del_peer_reason reason)
{
	struct ovpn_socket *ovpn_sock;
	LLIST_HEAD(release_list);
	struct ovpn_peer *peer;

	spin_lock_bh(&ovpn->lock);
	peer = rcu_dereference_protected(ovpn->peer,
					 lockdep_is_held(&ovpn->lock));
	if (!peer) {
		spin_unlock_bh(&ovpn->lock);
		return;
	}

	if (sk) {
		ovpn_sock = rcu_access_pointer(peer->sock);
		if (!ovpn_sock || ovpn_sock->sock->sk != sk) {
			spin_unlock_bh(&ovpn->lock);
			ovpn_peer_put(peer);
			return;
		}
	}

	ovpn_peer_remove(peer, reason, &release_list);
	unlock_ovpn(ovpn, &release_list);
}
