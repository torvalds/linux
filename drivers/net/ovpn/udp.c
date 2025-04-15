// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2019-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 */

#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/udp.h>
#include <net/addrconf.h>
#include <net/dst_cache.h>
#include <net/route.h>
#include <net/ipv6_stubs.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>

#include "ovpnpriv.h"
#include "main.h"
#include "bind.h"
#include "io.h"
#include "peer.h"
#include "socket.h"
#include "udp.h"

/**
 * ovpn_udp4_output - send IPv4 packet over udp socket
 * @peer: the destination peer
 * @bind: the binding related to the destination peer
 * @cache: dst cache
 * @sk: the socket to send the packet over
 * @skb: the packet to send
 *
 * Return: 0 on success or a negative error code otherwise
 */
static int ovpn_udp4_output(struct ovpn_peer *peer, struct ovpn_bind *bind,
			    struct dst_cache *cache, struct sock *sk,
			    struct sk_buff *skb)
{
	struct rtable *rt;
	struct flowi4 fl = {
		.saddr = bind->local.ipv4.s_addr,
		.daddr = bind->remote.in4.sin_addr.s_addr,
		.fl4_sport = inet_sk(sk)->inet_sport,
		.fl4_dport = bind->remote.in4.sin_port,
		.flowi4_proto = sk->sk_protocol,
		.flowi4_mark = sk->sk_mark,
	};
	int ret;

	local_bh_disable();
	rt = dst_cache_get_ip4(cache, &fl.saddr);
	if (rt)
		goto transmit;

	if (unlikely(!inet_confirm_addr(sock_net(sk), NULL, 0, fl.saddr,
					RT_SCOPE_HOST))) {
		/* we may end up here when the cached address is not usable
		 * anymore. In this case we reset address/cache and perform a
		 * new look up
		 */
		fl.saddr = 0;
		spin_lock_bh(&peer->lock);
		bind->local.ipv4.s_addr = 0;
		spin_unlock_bh(&peer->lock);
		dst_cache_reset(cache);
	}

	rt = ip_route_output_flow(sock_net(sk), &fl, sk);
	if (IS_ERR(rt) && PTR_ERR(rt) == -EINVAL) {
		fl.saddr = 0;
		spin_lock_bh(&peer->lock);
		bind->local.ipv4.s_addr = 0;
		spin_unlock_bh(&peer->lock);
		dst_cache_reset(cache);

		rt = ip_route_output_flow(sock_net(sk), &fl, sk);
	}

	if (IS_ERR(rt)) {
		ret = PTR_ERR(rt);
		net_dbg_ratelimited("%s: no route to host %pISpc: %d\n",
				    netdev_name(peer->ovpn->dev),
				    &bind->remote.in4,
				    ret);
		goto err;
	}
	dst_cache_set_ip4(cache, &rt->dst, fl.saddr);

transmit:
	udp_tunnel_xmit_skb(rt, sk, skb, fl.saddr, fl.daddr, 0,
			    ip4_dst_hoplimit(&rt->dst), 0, fl.fl4_sport,
			    fl.fl4_dport, false, sk->sk_no_check_tx);
	ret = 0;
err:
	local_bh_enable();
	return ret;
}

#if IS_ENABLED(CONFIG_IPV6)
/**
 * ovpn_udp6_output - send IPv6 packet over udp socket
 * @peer: the destination peer
 * @bind: the binding related to the destination peer
 * @cache: dst cache
 * @sk: the socket to send the packet over
 * @skb: the packet to send
 *
 * Return: 0 on success or a negative error code otherwise
 */
static int ovpn_udp6_output(struct ovpn_peer *peer, struct ovpn_bind *bind,
			    struct dst_cache *cache, struct sock *sk,
			    struct sk_buff *skb)
{
	struct dst_entry *dst;
	int ret;

	struct flowi6 fl = {
		.saddr = bind->local.ipv6,
		.daddr = bind->remote.in6.sin6_addr,
		.fl6_sport = inet_sk(sk)->inet_sport,
		.fl6_dport = bind->remote.in6.sin6_port,
		.flowi6_proto = sk->sk_protocol,
		.flowi6_mark = sk->sk_mark,
		.flowi6_oif = bind->remote.in6.sin6_scope_id,
	};

	local_bh_disable();
	dst = dst_cache_get_ip6(cache, &fl.saddr);
	if (dst)
		goto transmit;

	if (unlikely(!ipv6_chk_addr(sock_net(sk), &fl.saddr, NULL, 0))) {
		/* we may end up here when the cached address is not usable
		 * anymore. In this case we reset address/cache and perform a
		 * new look up
		 */
		fl.saddr = in6addr_any;
		spin_lock_bh(&peer->lock);
		bind->local.ipv6 = in6addr_any;
		spin_unlock_bh(&peer->lock);
		dst_cache_reset(cache);
	}

	dst = ipv6_stub->ipv6_dst_lookup_flow(sock_net(sk), sk, &fl, NULL);
	if (IS_ERR(dst)) {
		ret = PTR_ERR(dst);
		net_dbg_ratelimited("%s: no route to host %pISpc: %d\n",
				    netdev_name(peer->ovpn->dev),
				    &bind->remote.in6, ret);
		goto err;
	}
	dst_cache_set_ip6(cache, dst, &fl.saddr);

transmit:
	udp_tunnel6_xmit_skb(dst, sk, skb, skb->dev, &fl.saddr, &fl.daddr, 0,
			     ip6_dst_hoplimit(dst), 0, fl.fl6_sport,
			     fl.fl6_dport, udp_get_no_check6_tx(sk));
	ret = 0;
err:
	local_bh_enable();
	return ret;
}
#endif

/**
 * ovpn_udp_output - transmit skb using udp-tunnel
 * @peer: the destination peer
 * @cache: dst cache
 * @sk: the socket to send the packet over
 * @skb: the packet to send
 *
 * rcu_read_lock should be held on entry.
 * On return, the skb is consumed.
 *
 * Return: 0 on success or a negative error code otherwise
 */
static int ovpn_udp_output(struct ovpn_peer *peer, struct dst_cache *cache,
			   struct sock *sk, struct sk_buff *skb)
{
	struct ovpn_bind *bind;
	int ret;

	/* set sk to null if skb is already orphaned */
	if (!skb->destructor)
		skb->sk = NULL;

	rcu_read_lock();
	bind = rcu_dereference(peer->bind);
	if (unlikely(!bind)) {
		net_warn_ratelimited("%s: no bind for remote peer %u\n",
				     netdev_name(peer->ovpn->dev), peer->id);
		ret = -ENODEV;
		goto out;
	}

	switch (bind->remote.in4.sin_family) {
	case AF_INET:
		ret = ovpn_udp4_output(peer, bind, cache, sk, skb);
		break;
#if IS_ENABLED(CONFIG_IPV6)
	case AF_INET6:
		ret = ovpn_udp6_output(peer, bind, cache, sk, skb);
		break;
#endif
	default:
		ret = -EAFNOSUPPORT;
		break;
	}

out:
	rcu_read_unlock();
	return ret;
}

/**
 * ovpn_udp_send_skb - prepare skb and send it over via UDP
 * @peer: the destination peer
 * @sock: the RCU protected peer socket
 * @skb: the packet to send
 */
void ovpn_udp_send_skb(struct ovpn_peer *peer, struct socket *sock,
		       struct sk_buff *skb)
{
	int ret = -1;

	skb->dev = peer->ovpn->dev;
	/* no checksum performed at this layer */
	skb->ip_summed = CHECKSUM_NONE;

	/* get socket info */
	if (unlikely(!sock)) {
		net_warn_ratelimited("%s: no sock for remote peer %u\n",
				     netdev_name(peer->ovpn->dev), peer->id);
		goto out;
	}

	/* crypto layer -> transport (UDP) */
	ret = ovpn_udp_output(peer, &peer->dst_cache, sock->sk, skb);
out:
	if (unlikely(ret < 0)) {
		kfree_skb(skb);
		return;
	}
}

/**
 * ovpn_udp_socket_attach - set udp-tunnel CBs on socket and link it to ovpn
 * @ovpn_sock: socket to configure
 * @ovpn: the openvp instance to link
 *
 * After invoking this function, the sock will be controlled by ovpn so that
 * any incoming packet may be processed by ovpn first.
 *
 * Return: 0 on success or a negative error code otherwise
 */
int ovpn_udp_socket_attach(struct ovpn_socket *ovpn_sock,
			   struct ovpn_priv *ovpn)
{
	struct socket *sock = ovpn_sock->sock;
	struct ovpn_socket *old_data;
	int ret;

	/* make sure no pre-existing encapsulation handler exists */
	rcu_read_lock();
	old_data = rcu_dereference_sk_user_data(sock->sk);
	if (!old_data) {
		/* socket is currently unused - we can take it */
		rcu_read_unlock();
		return 0;
	}

	/* socket is in use. We need to understand if it's owned by this ovpn
	 * instance or by something else.
	 * In the former case, we can increase the refcounter and happily
	 * use it, because the same UDP socket is expected to be shared among
	 * different peers.
	 *
	 * Unlikely TCP, a single UDP socket can be used to talk to many remote
	 * hosts and therefore openvpn instantiates one only for all its peers
	 */
	if ((READ_ONCE(udp_sk(sock->sk)->encap_type) == UDP_ENCAP_OVPNINUDP) &&
	    old_data->ovpn == ovpn) {
		netdev_dbg(ovpn->dev,
			   "provided socket already owned by this interface\n");
		ret = -EALREADY;
	} else {
		netdev_dbg(ovpn->dev,
			   "provided socket already taken by other user\n");
		ret = -EBUSY;
	}
	rcu_read_unlock();

	return ret;
}

/**
 * ovpn_udp_socket_detach - clean udp-tunnel status for this socket
 * @ovpn_sock: the socket to clean
 */
void ovpn_udp_socket_detach(struct ovpn_socket *ovpn_sock)
{
}
