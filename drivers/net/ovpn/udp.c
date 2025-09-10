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
#include <net/transp_v6.h>
#include <net/udp.h>
#include <net/udp_tunnel.h>

#include "ovpnpriv.h"
#include "main.h"
#include "bind.h"
#include "io.h"
#include "peer.h"
#include "proto.h"
#include "socket.h"
#include "udp.h"

/* Retrieve the corresponding ovpn object from a UDP socket
 * rcu_read_lock must be held on entry
 */
static struct ovpn_socket *ovpn_socket_from_udp_sock(struct sock *sk)
{
	struct ovpn_socket *ovpn_sock;

	if (unlikely(READ_ONCE(udp_sk(sk)->encap_type) != UDP_ENCAP_OVPNINUDP))
		return NULL;

	ovpn_sock = rcu_dereference_sk_user_data(sk);
	if (unlikely(!ovpn_sock))
		return NULL;

	/* make sure that sk matches our stored transport socket */
	if (unlikely(!ovpn_sock->sk || sk != ovpn_sock->sk))
		return NULL;

	return ovpn_sock;
}

/**
 * ovpn_udp_encap_recv - Start processing a received UDP packet.
 * @sk: socket over which the packet was received
 * @skb: the received packet
 *
 * If the first byte of the payload is:
 * - DATA_V2 the packet is accepted for further processing,
 * - DATA_V1 the packet is dropped as not supported,
 * - anything else the packet is forwarded to the UDP stack for
 *   delivery to user space.
 *
 * Return:
 *  0 if skb was consumed or dropped
 * >0 if skb should be passed up to userspace as UDP (packet not consumed)
 * <0 if skb should be resubmitted as proto -N (packet not consumed)
 */
static int ovpn_udp_encap_recv(struct sock *sk, struct sk_buff *skb)
{
	struct ovpn_socket *ovpn_sock;
	struct ovpn_priv *ovpn;
	struct ovpn_peer *peer;
	u32 peer_id;
	u8 opcode;

	ovpn_sock = ovpn_socket_from_udp_sock(sk);
	if (unlikely(!ovpn_sock)) {
		net_err_ratelimited("ovpn: %s invoked on non ovpn socket\n",
				    __func__);
		goto drop_noovpn;
	}

	ovpn = ovpn_sock->ovpn;
	if (unlikely(!ovpn)) {
		net_err_ratelimited("ovpn: cannot obtain ovpn object from UDP socket\n");
		goto drop_noovpn;
	}

	/* Make sure the first 4 bytes of the skb data buffer after the UDP
	 * header are accessible.
	 * They are required to fetch the OP code, the key ID and the peer ID.
	 */
	if (unlikely(!pskb_may_pull(skb, sizeof(struct udphdr) +
				    OVPN_OPCODE_SIZE))) {
		net_dbg_ratelimited("%s: packet too small from UDP socket\n",
				    netdev_name(ovpn->dev));
		goto drop;
	}

	opcode = ovpn_opcode_from_skb(skb, sizeof(struct udphdr));
	if (unlikely(opcode != OVPN_DATA_V2)) {
		/* DATA_V1 is not supported */
		if (opcode == OVPN_DATA_V1)
			goto drop;

		/* unknown or control packet: let it bubble up to userspace */
		return 1;
	}

	peer_id = ovpn_peer_id_from_skb(skb, sizeof(struct udphdr));
	/* some OpenVPN server implementations send data packets with the
	 * peer-id set to UNDEF. In this case we skip the peer lookup by peer-id
	 * and we try with the transport address
	 */
	if (peer_id == OVPN_PEER_ID_UNDEF)
		peer = ovpn_peer_get_by_transp_addr(ovpn, skb);
	else
		peer = ovpn_peer_get_by_id(ovpn, peer_id);

	if (unlikely(!peer))
		goto drop;

	/* pop off outer UDP header */
	__skb_pull(skb, sizeof(struct udphdr));
	ovpn_recv(peer, skb);
	return 0;

drop:
	dev_dstats_rx_dropped(ovpn->dev);
drop_noovpn:
	kfree_skb(skb);
	return 0;
}

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
			    fl.fl4_dport, false, sk->sk_no_check_tx, 0);
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
	/* user IPv6 packets may be larger than the transport interface
	 * MTU (after encapsulation), however, since they are locally
	 * generated we should ensure they get fragmented.
	 * Setting the ignore_df flag to 1 will instruct ip6_fragment() to
	 * fragment packets if needed.
	 *
	 * NOTE: this is not needed for IPv4 because we pass df=0 to
	 * udp_tunnel_xmit_skb()
	 */
	skb->ignore_df = 1;
	udp_tunnel6_xmit_skb(dst, sk, skb, skb->dev, &fl.saddr, &fl.daddr, 0,
			     ip6_dst_hoplimit(dst), 0, fl.fl6_sport,
			     fl.fl6_dport, udp_get_no_check6_tx(sk), 0);
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
 * @sk: peer socket
 * @skb: the packet to send
 */
void ovpn_udp_send_skb(struct ovpn_peer *peer, struct sock *sk,
		       struct sk_buff *skb)
{
	int ret;

	skb->dev = peer->ovpn->dev;
	skb->mark = READ_ONCE(sk->sk_mark);
	/* no checksum performed at this layer */
	skb->ip_summed = CHECKSUM_NONE;

	/* crypto layer -> transport (UDP) */
	ret = ovpn_udp_output(peer, &peer->dst_cache, sk, skb);
	if (unlikely(ret < 0))
		kfree_skb(skb);
}

static void ovpn_udp_encap_destroy(struct sock *sk)
{
	struct ovpn_socket *sock;
	struct ovpn_priv *ovpn;

	rcu_read_lock();
	sock = rcu_dereference_sk_user_data(sk);
	if (!sock || !sock->ovpn) {
		rcu_read_unlock();
		return;
	}
	ovpn = sock->ovpn;
	rcu_read_unlock();

	ovpn_peers_free(ovpn, sk, OVPN_DEL_PEER_REASON_TRANSPORT_DISCONNECT);
}

/**
 * ovpn_udp_socket_attach - set udp-tunnel CBs on socket and link it to ovpn
 * @ovpn_sock: socket to configure
 * @sock: the socket container to be passed to setup_udp_tunnel_sock()
 * @ovpn: the openvp instance to link
 *
 * After invoking this function, the sock will be controlled by ovpn so that
 * any incoming packet may be processed by ovpn first.
 *
 * Return: 0 on success or a negative error code otherwise
 */
int ovpn_udp_socket_attach(struct ovpn_socket *ovpn_sock, struct socket *sock,
			   struct ovpn_priv *ovpn)
{
	struct udp_tunnel_sock_cfg cfg = {
		.encap_type = UDP_ENCAP_OVPNINUDP,
		.encap_rcv = ovpn_udp_encap_recv,
		.encap_destroy = ovpn_udp_encap_destroy,
	};
	struct ovpn_socket *old_data;
	int ret;

	/* make sure no pre-existing encapsulation handler exists */
	rcu_read_lock();
	old_data = rcu_dereference_sk_user_data(ovpn_sock->sk);
	if (!old_data) {
		/* socket is currently unused - we can take it */
		rcu_read_unlock();
		setup_udp_tunnel_sock(sock_net(ovpn_sock->sk), sock, &cfg);
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
	if ((READ_ONCE(udp_sk(ovpn_sock->sk)->encap_type) == UDP_ENCAP_OVPNINUDP) &&
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
	struct sock *sk = ovpn_sock->sk;

	/* Re-enable multicast loopback */
	inet_set_bit(MC_LOOP, sk);
	/* Disable CHECKSUM_UNNECESSARY to CHECKSUM_COMPLETE conversion */
	inet_dec_convert_csum(sk);

	WRITE_ONCE(udp_sk(sk)->encap_type, 0);
	WRITE_ONCE(udp_sk(sk)->encap_rcv, NULL);
	WRITE_ONCE(udp_sk(sk)->encap_destroy, NULL);

	rcu_assign_sk_user_data(sk, NULL);
}
