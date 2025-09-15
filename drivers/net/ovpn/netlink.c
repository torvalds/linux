// SPDX-License-Identifier: GPL-2.0
/*  OpenVPN data channel offload
 *
 *  Copyright (C) 2020-2025 OpenVPN, Inc.
 *
 *  Author:	Antonio Quartulli <antonio@openvpn.net>
 */

#include <linux/netdevice.h>
#include <linux/types.h>
#include <net/genetlink.h>

#include <uapi/linux/ovpn.h>

#include "ovpnpriv.h"
#include "main.h"
#include "netlink.h"
#include "netlink-gen.h"
#include "bind.h"
#include "crypto.h"
#include "peer.h"
#include "socket.h"

MODULE_ALIAS_GENL_FAMILY(OVPN_FAMILY_NAME);

/**
 * ovpn_get_dev_from_attrs - retrieve the ovpn private data from the netdevice
 *			     a netlink message is targeting
 * @net: network namespace where to look for the interface
 * @info: generic netlink info from the user request
 * @tracker: tracker object to be used for the netdev reference acquisition
 *
 * Return: the ovpn private data, if found, or an error otherwise
 */
static struct ovpn_priv *
ovpn_get_dev_from_attrs(struct net *net, const struct genl_info *info,
			netdevice_tracker *tracker)
{
	struct ovpn_priv *ovpn;
	struct net_device *dev;
	int ifindex;

	if (GENL_REQ_ATTR_CHECK(info, OVPN_A_IFINDEX))
		return ERR_PTR(-EINVAL);

	ifindex = nla_get_u32(info->attrs[OVPN_A_IFINDEX]);

	rcu_read_lock();
	dev = dev_get_by_index_rcu(net, ifindex);
	if (!dev) {
		rcu_read_unlock();
		NL_SET_ERR_MSG_MOD(info->extack,
				   "ifindex does not match any interface");
		return ERR_PTR(-ENODEV);
	}

	if (!ovpn_dev_is_valid(dev)) {
		rcu_read_unlock();
		NL_SET_ERR_MSG_MOD(info->extack,
				   "specified interface is not ovpn");
		NL_SET_BAD_ATTR(info->extack, info->attrs[OVPN_A_IFINDEX]);
		return ERR_PTR(-EINVAL);
	}

	ovpn = netdev_priv(dev);
	netdev_hold(dev, tracker, GFP_ATOMIC);
	rcu_read_unlock();

	return ovpn;
}

int ovpn_nl_pre_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		     struct genl_info *info)
{
	netdevice_tracker *tracker = (netdevice_tracker *)&info->user_ptr[1];
	struct ovpn_priv *ovpn = ovpn_get_dev_from_attrs(genl_info_net(info),
							 info, tracker);

	if (IS_ERR(ovpn))
		return PTR_ERR(ovpn);

	info->user_ptr[0] = ovpn;

	return 0;
}

void ovpn_nl_post_doit(const struct genl_split_ops *ops, struct sk_buff *skb,
		       struct genl_info *info)
{
	netdevice_tracker *tracker = (netdevice_tracker *)&info->user_ptr[1];
	struct ovpn_priv *ovpn = info->user_ptr[0];

	if (ovpn)
		netdev_put(ovpn->dev, tracker);
}

static bool ovpn_nl_attr_sockaddr_remote(struct nlattr **attrs,
					 struct sockaddr_storage *ss)
{
	struct sockaddr_in6 *sin6;
	struct sockaddr_in *sin;
	struct in6_addr *in6;
	__be16 port = 0;
	__be32 *in;

	ss->ss_family = AF_UNSPEC;

	if (attrs[OVPN_A_PEER_REMOTE_PORT])
		port = nla_get_be16(attrs[OVPN_A_PEER_REMOTE_PORT]);

	if (attrs[OVPN_A_PEER_REMOTE_IPV4]) {
		ss->ss_family = AF_INET;
		in = nla_data(attrs[OVPN_A_PEER_REMOTE_IPV4]);
	} else if (attrs[OVPN_A_PEER_REMOTE_IPV6]) {
		ss->ss_family = AF_INET6;
		in6 = nla_data(attrs[OVPN_A_PEER_REMOTE_IPV6]);
	} else {
		return false;
	}

	switch (ss->ss_family) {
	case AF_INET6:
		/* If this is a regular IPv6 just break and move on,
		 * otherwise switch to AF_INET and extract the IPv4 accordingly
		 */
		if (!ipv6_addr_v4mapped(in6)) {
			sin6 = (struct sockaddr_in6 *)ss;
			sin6->sin6_port = port;
			memcpy(&sin6->sin6_addr, in6, sizeof(*in6));
			break;
		}

		/* v4-mapped-v6 address */
		ss->ss_family = AF_INET;
		in = &in6->s6_addr32[3];
		fallthrough;
	case AF_INET:
		sin = (struct sockaddr_in *)ss;
		sin->sin_port = port;
		sin->sin_addr.s_addr = *in;
		break;
	}

	return true;
}

static u8 *ovpn_nl_attr_local_ip(struct nlattr **attrs)
{
	u8 *addr6;

	if (!attrs[OVPN_A_PEER_LOCAL_IPV4] && !attrs[OVPN_A_PEER_LOCAL_IPV6])
		return NULL;

	if (attrs[OVPN_A_PEER_LOCAL_IPV4])
		return nla_data(attrs[OVPN_A_PEER_LOCAL_IPV4]);

	addr6 = nla_data(attrs[OVPN_A_PEER_LOCAL_IPV6]);
	/* this is an IPv4-mapped IPv6 address, therefore extract the actual
	 * v4 address from the last 4 bytes
	 */
	if (ipv6_addr_v4mapped((struct in6_addr *)addr6))
		return addr6 + 12;

	return addr6;
}

static sa_family_t ovpn_nl_family_get(struct nlattr *addr4,
				      struct nlattr *addr6)
{
	if (addr4)
		return AF_INET;

	if (addr6) {
		if (ipv6_addr_v4mapped((struct in6_addr *)nla_data(addr6)))
			return AF_INET;
		return AF_INET6;
	}

	return AF_UNSPEC;
}

static int ovpn_nl_peer_precheck(struct ovpn_priv *ovpn,
				 struct genl_info *info,
				 struct nlattr **attrs)
{
	sa_family_t local_fam, remote_fam;

	if (NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_PEER], attrs,
			      OVPN_A_PEER_ID))
		return -EINVAL;

	if (attrs[OVPN_A_PEER_REMOTE_IPV4] && attrs[OVPN_A_PEER_REMOTE_IPV6]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "cannot specify both remote IPv4 or IPv6 address");
		return -EINVAL;
	}

	if (!attrs[OVPN_A_PEER_REMOTE_IPV4] &&
	    !attrs[OVPN_A_PEER_REMOTE_IPV6] && attrs[OVPN_A_PEER_REMOTE_PORT]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "cannot specify remote port without IP address");
		return -EINVAL;
	}

	if ((attrs[OVPN_A_PEER_REMOTE_IPV4] ||
	     attrs[OVPN_A_PEER_REMOTE_IPV6]) &&
	    !attrs[OVPN_A_PEER_REMOTE_PORT]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "cannot specify remote IP address without port");
		return -EINVAL;
	}

	if (!attrs[OVPN_A_PEER_REMOTE_IPV4] &&
	    attrs[OVPN_A_PEER_LOCAL_IPV4]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "cannot specify local IPv4 address without remote");
		return -EINVAL;
	}

	if (!attrs[OVPN_A_PEER_REMOTE_IPV6] &&
	    attrs[OVPN_A_PEER_LOCAL_IPV6]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "cannot specify local IPV6 address without remote");
		return -EINVAL;
	}

	/* check that local and remote address families are the same even
	 * after parsing v4mapped IPv6 addresses.
	 * (if addresses are not provided, family will be AF_UNSPEC and
	 * the check is skipped)
	 */
	local_fam = ovpn_nl_family_get(attrs[OVPN_A_PEER_LOCAL_IPV4],
				       attrs[OVPN_A_PEER_LOCAL_IPV6]);
	remote_fam = ovpn_nl_family_get(attrs[OVPN_A_PEER_REMOTE_IPV4],
					attrs[OVPN_A_PEER_REMOTE_IPV6]);
	if (local_fam != AF_UNSPEC && remote_fam != AF_UNSPEC &&
	    local_fam != remote_fam) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "mismatching local and remote address families");
		return -EINVAL;
	}

	if (remote_fam != AF_INET6 && attrs[OVPN_A_PEER_REMOTE_IPV6_SCOPE_ID]) {
		NL_SET_ERR_MSG_MOD(info->extack,
				   "cannot specify scope id without remote IPv6 address");
		return -EINVAL;
	}

	/* VPN IPs are needed only in MP mode for selecting the right peer */
	if (ovpn->mode == OVPN_MODE_P2P && (attrs[OVPN_A_PEER_VPN_IPV4] ||
					    attrs[OVPN_A_PEER_VPN_IPV6])) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "unexpected VPN IP in P2P mode");
		return -EINVAL;
	}

	if ((attrs[OVPN_A_PEER_KEEPALIVE_INTERVAL] &&
	     !attrs[OVPN_A_PEER_KEEPALIVE_TIMEOUT]) ||
	    (!attrs[OVPN_A_PEER_KEEPALIVE_INTERVAL] &&
	     attrs[OVPN_A_PEER_KEEPALIVE_TIMEOUT])) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "keepalive interval and timeout are required together");
		return -EINVAL;
	}

	return 0;
}

/**
 * ovpn_nl_peer_modify - modify the peer attributes according to the incoming msg
 * @peer: the peer to modify
 * @info: generic netlink info from the user request
 * @attrs: the attributes from the user request
 *
 * Return: a negative error code in case of failure, 0 on success or 1 on
 *	   success and the VPN IPs have been modified (requires rehashing in MP
 *	   mode)
 */
static int ovpn_nl_peer_modify(struct ovpn_peer *peer, struct genl_info *info,
			       struct nlattr **attrs)
{
	struct sockaddr_storage ss = {};
	void *local_ip = NULL;
	u32 interv, timeout;
	bool rehash = false;
	int ret;

	spin_lock_bh(&peer->lock);

	if (ovpn_nl_attr_sockaddr_remote(attrs, &ss)) {
		/* we carry the local IP in a generic container.
		 * ovpn_peer_reset_sockaddr() will properly interpret it
		 * based on ss.ss_family
		 */
		local_ip = ovpn_nl_attr_local_ip(attrs);

		/* set peer sockaddr */
		ret = ovpn_peer_reset_sockaddr(peer, &ss, local_ip);
		if (ret < 0) {
			NL_SET_ERR_MSG_FMT_MOD(info->extack,
					       "cannot set peer sockaddr: %d",
					       ret);
			goto err_unlock;
		}
		dst_cache_reset(&peer->dst_cache);
	}

	if (attrs[OVPN_A_PEER_VPN_IPV4]) {
		rehash = true;
		peer->vpn_addrs.ipv4.s_addr =
			nla_get_in_addr(attrs[OVPN_A_PEER_VPN_IPV4]);
	}

	if (attrs[OVPN_A_PEER_VPN_IPV6]) {
		rehash = true;
		peer->vpn_addrs.ipv6 =
			nla_get_in6_addr(attrs[OVPN_A_PEER_VPN_IPV6]);
	}

	/* when setting the keepalive, both parameters have to be configured */
	if (attrs[OVPN_A_PEER_KEEPALIVE_INTERVAL] &&
	    attrs[OVPN_A_PEER_KEEPALIVE_TIMEOUT]) {
		interv = nla_get_u32(attrs[OVPN_A_PEER_KEEPALIVE_INTERVAL]);
		timeout = nla_get_u32(attrs[OVPN_A_PEER_KEEPALIVE_TIMEOUT]);
		ovpn_peer_keepalive_set(peer, interv, timeout);
	}

	netdev_dbg(peer->ovpn->dev,
		   "modify peer id=%u endpoint=%pIScp VPN-IPv4=%pI4 VPN-IPv6=%pI6c\n",
		   peer->id, &ss,
		   &peer->vpn_addrs.ipv4.s_addr, &peer->vpn_addrs.ipv6);

	spin_unlock_bh(&peer->lock);

	return rehash ? 1 : 0;
err_unlock:
	spin_unlock_bh(&peer->lock);
	return ret;
}

int ovpn_nl_peer_new_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[OVPN_A_PEER_MAX + 1];
	struct ovpn_priv *ovpn = info->user_ptr[0];
	struct ovpn_socket *ovpn_sock;
	struct socket *sock = NULL;
	struct ovpn_peer *peer;
	u32 sockfd, peer_id;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, OVPN_A_PEER))
		return -EINVAL;

	ret = nla_parse_nested(attrs, OVPN_A_PEER_MAX, info->attrs[OVPN_A_PEER],
			       ovpn_peer_new_input_nl_policy, info->extack);
	if (ret)
		return ret;

	ret = ovpn_nl_peer_precheck(ovpn, info, attrs);
	if (ret < 0)
		return ret;

	if (NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_PEER], attrs,
			      OVPN_A_PEER_SOCKET))
		return -EINVAL;

	/* in MP mode VPN IPs are required for selecting the right peer */
	if (ovpn->mode == OVPN_MODE_MP && !attrs[OVPN_A_PEER_VPN_IPV4] &&
	    !attrs[OVPN_A_PEER_VPN_IPV6]) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "VPN IP must be provided in MP mode");
		return -EINVAL;
	}

	peer_id = nla_get_u32(attrs[OVPN_A_PEER_ID]);
	peer = ovpn_peer_new(ovpn, peer_id);
	if (IS_ERR(peer)) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "cannot create new peer object for peer %u: %ld",
				       peer_id, PTR_ERR(peer));
		return PTR_ERR(peer);
	}

	/* lookup the fd in the kernel table and extract the socket object */
	sockfd = nla_get_u32(attrs[OVPN_A_PEER_SOCKET]);
	/* sockfd_lookup() increases sock's refcounter */
	sock = sockfd_lookup(sockfd, &ret);
	if (!sock) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "cannot lookup peer socket (fd=%u): %d",
				       sockfd, ret);
		ret = -ENOTSOCK;
		goto peer_release;
	}

	/* Only when using UDP as transport protocol the remote endpoint
	 * can be configured so that ovpn knows where to send packets to.
	 */
	if (sock->sk->sk_protocol == IPPROTO_UDP &&
	    !attrs[OVPN_A_PEER_REMOTE_IPV4] &&
	    !attrs[OVPN_A_PEER_REMOTE_IPV6]) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "missing remote IP address for UDP socket");
		sockfd_put(sock);
		ret = -EINVAL;
		goto peer_release;
	}

	/* In case of TCP, the socket is connected to the peer and ovpn
	 * will just send bytes over it, without the need to specify a
	 * destination.
	 */
	if (sock->sk->sk_protocol == IPPROTO_TCP &&
	    (attrs[OVPN_A_PEER_REMOTE_IPV4] ||
	     attrs[OVPN_A_PEER_REMOTE_IPV6])) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "unexpected remote IP address with TCP socket");
		sockfd_put(sock);
		ret = -EINVAL;
		goto peer_release;
	}

	ovpn_sock = ovpn_socket_new(sock, peer);
	/* at this point we unconditionally drop the reference to the socket:
	 * - in case of error, the socket has to be dropped
	 * - if case of success, the socket is configured and let
	 *   userspace own the reference, so that the latter can
	 *   trigger the final close()
	 */
	sockfd_put(sock);
	if (IS_ERR(ovpn_sock)) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "cannot encapsulate socket: %ld",
				       PTR_ERR(ovpn_sock));
		ret = -ENOTSOCK;
		goto peer_release;
	}

	rcu_assign_pointer(peer->sock, ovpn_sock);

	ret = ovpn_nl_peer_modify(peer, info, attrs);
	if (ret < 0)
		goto sock_release;

	ret = ovpn_peer_add(ovpn, peer);
	if (ret < 0) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "cannot add new peer (id=%u) to hashtable: %d",
				       peer->id, ret);
		goto sock_release;
	}

	return 0;

sock_release:
	ovpn_socket_release(peer);
peer_release:
	/* release right away because peer was not yet hashed, thus it is not
	 * used in any context
	 */
	ovpn_peer_release(peer);

	return ret;
}

int ovpn_nl_peer_set_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[OVPN_A_PEER_MAX + 1];
	struct ovpn_priv *ovpn = info->user_ptr[0];
	struct ovpn_socket *sock;
	struct ovpn_peer *peer;
	u32 peer_id;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, OVPN_A_PEER))
		return -EINVAL;

	ret = nla_parse_nested(attrs, OVPN_A_PEER_MAX, info->attrs[OVPN_A_PEER],
			       ovpn_peer_set_input_nl_policy, info->extack);
	if (ret)
		return ret;

	ret = ovpn_nl_peer_precheck(ovpn, info, attrs);
	if (ret < 0)
		return ret;

	if (attrs[OVPN_A_PEER_SOCKET]) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "socket cannot be modified");
		return -EINVAL;
	}

	peer_id = nla_get_u32(attrs[OVPN_A_PEER_ID]);
	peer = ovpn_peer_get_by_id(ovpn, peer_id);
	if (!peer) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "cannot find peer with id %u", peer_id);
		return -ENOENT;
	}

	/* when using a TCP socket the remote IP is not expected */
	rcu_read_lock();
	sock = rcu_dereference(peer->sock);
	if (sock && sock->sk->sk_protocol == IPPROTO_TCP &&
	    (attrs[OVPN_A_PEER_REMOTE_IPV4] ||
	     attrs[OVPN_A_PEER_REMOTE_IPV6])) {
		rcu_read_unlock();
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "unexpected remote IP address with TCP socket");
		ovpn_peer_put(peer);
		return -EINVAL;
	}
	rcu_read_unlock();

	spin_lock_bh(&ovpn->lock);
	ret = ovpn_nl_peer_modify(peer, info, attrs);
	if (ret < 0) {
		spin_unlock_bh(&ovpn->lock);
		ovpn_peer_put(peer);
		return ret;
	}

	/* ret == 1 means that VPN IPv4/6 has been modified and rehashing
	 * is required
	 */
	if (ret > 0)
		ovpn_peer_hash_vpn_ip(peer);
	spin_unlock_bh(&ovpn->lock);
	ovpn_peer_put(peer);

	return 0;
}

static int ovpn_nl_send_peer(struct sk_buff *skb, const struct genl_info *info,
			     const struct ovpn_peer *peer, u32 portid, u32 seq,
			     int flags)
{
	const struct ovpn_bind *bind;
	struct ovpn_socket *sock;
	int ret = -EMSGSIZE;
	struct nlattr *attr;
	__be16 local_port;
	void *hdr;
	int id;

	hdr = genlmsg_put(skb, portid, seq, &ovpn_nl_family, flags,
			  OVPN_CMD_PEER_GET);
	if (!hdr)
		return -ENOBUFS;

	attr = nla_nest_start(skb, OVPN_A_PEER);
	if (!attr)
		goto err;

	rcu_read_lock();
	sock = rcu_dereference(peer->sock);
	if (!sock) {
		ret = -EINVAL;
		goto err_unlock;
	}

	if (!net_eq(genl_info_net(info), sock_net(sock->sk))) {
		id = peernet2id_alloc(genl_info_net(info),
				      sock_net(sock->sk),
				      GFP_ATOMIC);
		if (nla_put_s32(skb, OVPN_A_PEER_SOCKET_NETNSID, id))
			goto err_unlock;
	}
	local_port = inet_sk(sock->sk)->inet_sport;
	rcu_read_unlock();

	if (nla_put_u32(skb, OVPN_A_PEER_ID, peer->id))
		goto err;

	if (peer->vpn_addrs.ipv4.s_addr != htonl(INADDR_ANY))
		if (nla_put_in_addr(skb, OVPN_A_PEER_VPN_IPV4,
				    peer->vpn_addrs.ipv4.s_addr))
			goto err;

	if (!ipv6_addr_equal(&peer->vpn_addrs.ipv6, &in6addr_any))
		if (nla_put_in6_addr(skb, OVPN_A_PEER_VPN_IPV6,
				     &peer->vpn_addrs.ipv6))
			goto err;

	if (nla_put_u32(skb, OVPN_A_PEER_KEEPALIVE_INTERVAL,
			peer->keepalive_interval) ||
	    nla_put_u32(skb, OVPN_A_PEER_KEEPALIVE_TIMEOUT,
			peer->keepalive_timeout))
		goto err;

	rcu_read_lock();
	bind = rcu_dereference(peer->bind);
	if (bind) {
		if (bind->remote.in4.sin_family == AF_INET) {
			if (nla_put_in_addr(skb, OVPN_A_PEER_REMOTE_IPV4,
					    bind->remote.in4.sin_addr.s_addr) ||
			    nla_put_net16(skb, OVPN_A_PEER_REMOTE_PORT,
					  bind->remote.in4.sin_port) ||
			    nla_put_in_addr(skb, OVPN_A_PEER_LOCAL_IPV4,
					    bind->local.ipv4.s_addr))
				goto err_unlock;
		} else if (bind->remote.in4.sin_family == AF_INET6) {
			if (nla_put_in6_addr(skb, OVPN_A_PEER_REMOTE_IPV6,
					     &bind->remote.in6.sin6_addr) ||
			    nla_put_u32(skb, OVPN_A_PEER_REMOTE_IPV6_SCOPE_ID,
					bind->remote.in6.sin6_scope_id) ||
			    nla_put_net16(skb, OVPN_A_PEER_REMOTE_PORT,
					  bind->remote.in6.sin6_port) ||
			    nla_put_in6_addr(skb, OVPN_A_PEER_LOCAL_IPV6,
					     &bind->local.ipv6))
				goto err_unlock;
		}
	}
	rcu_read_unlock();

	if (nla_put_net16(skb, OVPN_A_PEER_LOCAL_PORT, local_port) ||
	    /* VPN RX stats */
	    nla_put_uint(skb, OVPN_A_PEER_VPN_RX_BYTES,
			 atomic64_read(&peer->vpn_stats.rx.bytes)) ||
	    nla_put_uint(skb, OVPN_A_PEER_VPN_RX_PACKETS,
			 atomic64_read(&peer->vpn_stats.rx.packets)) ||
	    /* VPN TX stats */
	    nla_put_uint(skb, OVPN_A_PEER_VPN_TX_BYTES,
			 atomic64_read(&peer->vpn_stats.tx.bytes)) ||
	    nla_put_uint(skb, OVPN_A_PEER_VPN_TX_PACKETS,
			 atomic64_read(&peer->vpn_stats.tx.packets)) ||
	    /* link RX stats */
	    nla_put_uint(skb, OVPN_A_PEER_LINK_RX_BYTES,
			 atomic64_read(&peer->link_stats.rx.bytes)) ||
	    nla_put_uint(skb, OVPN_A_PEER_LINK_RX_PACKETS,
			 atomic64_read(&peer->link_stats.rx.packets)) ||
	    /* link TX stats */
	    nla_put_uint(skb, OVPN_A_PEER_LINK_TX_BYTES,
			 atomic64_read(&peer->link_stats.tx.bytes)) ||
	    nla_put_uint(skb, OVPN_A_PEER_LINK_TX_PACKETS,
			 atomic64_read(&peer->link_stats.tx.packets)))
		goto err;

	nla_nest_end(skb, attr);
	genlmsg_end(skb, hdr);

	return 0;
err_unlock:
	rcu_read_unlock();
err:
	genlmsg_cancel(skb, hdr);
	return ret;
}

int ovpn_nl_peer_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[OVPN_A_PEER_MAX + 1];
	struct ovpn_priv *ovpn = info->user_ptr[0];
	struct ovpn_peer *peer;
	struct sk_buff *msg;
	u32 peer_id;
	int ret, i;

	if (GENL_REQ_ATTR_CHECK(info, OVPN_A_PEER))
		return -EINVAL;

	ret = nla_parse_nested(attrs, OVPN_A_PEER_MAX, info->attrs[OVPN_A_PEER],
			       ovpn_peer_nl_policy, info->extack);
	if (ret)
		return ret;

	if (NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_PEER], attrs,
			      OVPN_A_PEER_ID))
		return -EINVAL;

	/* OVPN_CMD_PEER_GET expects only the PEER_ID, therefore
	 * ensure that the user hasn't specified any other attribute.
	 *
	 * Unfortunately this check cannot be performed via netlink
	 * spec/policy and must be open-coded.
	 */
	for (i = 0; i < OVPN_A_PEER_MAX + 1; i++) {
		if (i == OVPN_A_PEER_ID)
			continue;

		if (attrs[i]) {
			NL_SET_ERR_MSG_FMT_MOD(info->extack,
					       "unexpected attribute %u", i);
			return -EINVAL;
		}
	}

	peer_id = nla_get_u32(attrs[OVPN_A_PEER_ID]);
	peer = ovpn_peer_get_by_id(ovpn, peer_id);
	if (!peer) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "cannot find peer with id %u", peer_id);
		return -ENOENT;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto err;
	}

	ret = ovpn_nl_send_peer(msg, info, peer, info->snd_portid,
				info->snd_seq, 0);
	if (ret < 0) {
		nlmsg_free(msg);
		goto err;
	}

	ret = genlmsg_reply(msg, info);
err:
	ovpn_peer_put(peer);
	return ret;
}

int ovpn_nl_peer_get_dumpit(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct genl_info *info = genl_info_dump(cb);
	int bkt, last_idx = cb->args[1], dumped = 0;
	netdevice_tracker tracker;
	struct ovpn_priv *ovpn;
	struct ovpn_peer *peer;

	ovpn = ovpn_get_dev_from_attrs(sock_net(cb->skb->sk), info, &tracker);
	if (IS_ERR(ovpn))
		return PTR_ERR(ovpn);

	if (ovpn->mode == OVPN_MODE_P2P) {
		/* if we already dumped a peer it means we are done */
		if (last_idx)
			goto out;

		rcu_read_lock();
		peer = rcu_dereference(ovpn->peer);
		if (peer) {
			if (ovpn_nl_send_peer(skb, info, peer,
					      NETLINK_CB(cb->skb).portid,
					      cb->nlh->nlmsg_seq,
					      NLM_F_MULTI) == 0)
				dumped++;
		}
		rcu_read_unlock();
	} else {
		rcu_read_lock();
		hash_for_each_rcu(ovpn->peers->by_id, bkt, peer,
				  hash_entry_id) {
			/* skip already dumped peers that were dumped by
			 * previous invocations
			 */
			if (last_idx > 0) {
				last_idx--;
				continue;
			}

			if (ovpn_nl_send_peer(skb, info, peer,
					      NETLINK_CB(cb->skb).portid,
					      cb->nlh->nlmsg_seq,
					      NLM_F_MULTI) < 0)
				break;

			/* count peers being dumped during this invocation */
			dumped++;
		}
		rcu_read_unlock();
	}

out:
	netdev_put(ovpn->dev, &tracker);

	/* sum up peers dumped in this message, so that at the next invocation
	 * we can continue from where we left
	 */
	cb->args[1] += dumped;
	return skb->len;
}

int ovpn_nl_peer_del_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[OVPN_A_PEER_MAX + 1];
	struct ovpn_priv *ovpn = info->user_ptr[0];
	struct ovpn_peer *peer;
	u32 peer_id;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, OVPN_A_PEER))
		return -EINVAL;

	ret = nla_parse_nested(attrs, OVPN_A_PEER_MAX, info->attrs[OVPN_A_PEER],
			       ovpn_peer_del_input_nl_policy, info->extack);
	if (ret)
		return ret;

	if (NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_PEER], attrs,
			      OVPN_A_PEER_ID))
		return -EINVAL;

	peer_id = nla_get_u32(attrs[OVPN_A_PEER_ID]);
	peer = ovpn_peer_get_by_id(ovpn, peer_id);
	if (!peer) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "cannot find peer with id %u", peer_id);
		return -ENOENT;
	}

	netdev_dbg(ovpn->dev, "del peer %u\n", peer->id);
	ret = ovpn_peer_del(peer, OVPN_DEL_PEER_REASON_USERSPACE);
	ovpn_peer_put(peer);

	return ret;
}

static int ovpn_nl_get_key_dir(struct genl_info *info, struct nlattr *key,
			       enum ovpn_cipher_alg cipher,
			       struct ovpn_key_direction *dir)
{
	struct nlattr *attrs[OVPN_A_KEYDIR_MAX + 1];
	int ret;

	ret = nla_parse_nested(attrs, OVPN_A_KEYDIR_MAX, key,
			       ovpn_keydir_nl_policy, info->extack);
	if (ret)
		return ret;

	switch (cipher) {
	case OVPN_CIPHER_ALG_AES_GCM:
	case OVPN_CIPHER_ALG_CHACHA20_POLY1305:
		if (NL_REQ_ATTR_CHECK(info->extack, key, attrs,
				      OVPN_A_KEYDIR_CIPHER_KEY) ||
		    NL_REQ_ATTR_CHECK(info->extack, key, attrs,
				      OVPN_A_KEYDIR_NONCE_TAIL))
			return -EINVAL;

		dir->cipher_key = nla_data(attrs[OVPN_A_KEYDIR_CIPHER_KEY]);
		dir->cipher_key_size = nla_len(attrs[OVPN_A_KEYDIR_CIPHER_KEY]);

		/* These algorithms require a 96bit nonce,
		 * Construct it by combining 4-bytes packet id and
		 * 8-bytes nonce-tail from userspace
		 */
		dir->nonce_tail = nla_data(attrs[OVPN_A_KEYDIR_NONCE_TAIL]);
		dir->nonce_tail_size = nla_len(attrs[OVPN_A_KEYDIR_NONCE_TAIL]);
		break;
	default:
		NL_SET_ERR_MSG_MOD(info->extack, "unsupported cipher");
		return -EINVAL;
	}

	return 0;
}

/**
 * ovpn_nl_key_new_doit - configure a new key for the specified peer
 * @skb: incoming netlink message
 * @info: genetlink metadata
 *
 * This function allows the user to install a new key in the peer crypto
 * state.
 * Each peer has two 'slots', namely 'primary' and 'secondary', where
 * keys can be installed. The key in the 'primary' slot is used for
 * encryption, while both keys can be used for decryption by matching the
 * key ID carried in the incoming packet.
 *
 * The user is responsible for rotating keys when necessary. The user
 * may fetch peer traffic statistics via netlink in order to better
 * identify the right time to rotate keys.
 * The renegotiation follows these steps:
 * 1. a new key is computed by the user and is installed in the 'secondary'
 *    slot
 * 2. at user discretion (usually after a predetermined time) 'primary' and
 *    'secondary' contents are swapped and the new key starts being used for
 *    encryption, while the old key is kept around for decryption of late
 *    packets.
 *
 * Return: 0 on success or a negative error code otherwise.
 */
int ovpn_nl_key_new_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[OVPN_A_KEYCONF_MAX + 1];
	struct ovpn_priv *ovpn = info->user_ptr[0];
	struct ovpn_peer_key_reset pkr;
	struct ovpn_peer *peer;
	u32 peer_id;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, OVPN_A_KEYCONF))
		return -EINVAL;

	ret = nla_parse_nested(attrs, OVPN_A_KEYCONF_MAX,
			       info->attrs[OVPN_A_KEYCONF],
			       ovpn_keyconf_nl_policy, info->extack);
	if (ret)
		return ret;

	if (NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_KEYCONF], attrs,
			      OVPN_A_KEYCONF_PEER_ID))
		return -EINVAL;

	if (NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_KEYCONF], attrs,
			      OVPN_A_KEYCONF_SLOT) ||
	    NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_KEYCONF], attrs,
			      OVPN_A_KEYCONF_KEY_ID) ||
	    NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_KEYCONF], attrs,
			      OVPN_A_KEYCONF_CIPHER_ALG) ||
	    NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_KEYCONF], attrs,
			      OVPN_A_KEYCONF_ENCRYPT_DIR) ||
	    NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_KEYCONF], attrs,
			      OVPN_A_KEYCONF_DECRYPT_DIR))
		return -EINVAL;

	pkr.slot = nla_get_u32(attrs[OVPN_A_KEYCONF_SLOT]);
	pkr.key.key_id = nla_get_u32(attrs[OVPN_A_KEYCONF_KEY_ID]);
	pkr.key.cipher_alg = nla_get_u32(attrs[OVPN_A_KEYCONF_CIPHER_ALG]);

	ret = ovpn_nl_get_key_dir(info, attrs[OVPN_A_KEYCONF_ENCRYPT_DIR],
				  pkr.key.cipher_alg, &pkr.key.encrypt);
	if (ret < 0)
		return ret;

	ret = ovpn_nl_get_key_dir(info, attrs[OVPN_A_KEYCONF_DECRYPT_DIR],
				  pkr.key.cipher_alg, &pkr.key.decrypt);
	if (ret < 0)
		return ret;

	peer_id = nla_get_u32(attrs[OVPN_A_KEYCONF_PEER_ID]);
	peer = ovpn_peer_get_by_id(ovpn, peer_id);
	if (!peer) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "no peer with id %u to set key for",
				       peer_id);
		return -ENOENT;
	}

	ret = ovpn_crypto_state_reset(&peer->crypto, &pkr);
	if (ret < 0) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "cannot install new key for peer %u",
				       peer_id);
		goto out;
	}

	netdev_dbg(ovpn->dev, "new key installed (id=%u) for peer %u\n",
		   pkr.key.key_id, peer_id);
out:
	ovpn_peer_put(peer);
	return ret;
}

static int ovpn_nl_send_key(struct sk_buff *skb, const struct genl_info *info,
			    u32 peer_id, enum ovpn_key_slot slot,
			    const struct ovpn_key_config *keyconf)
{
	struct nlattr *attr;
	void *hdr;

	hdr = genlmsg_put(skb, info->snd_portid, info->snd_seq, &ovpn_nl_family,
			  0, OVPN_CMD_KEY_GET);
	if (!hdr)
		return -ENOBUFS;

	attr = nla_nest_start(skb, OVPN_A_KEYCONF);
	if (!attr)
		goto err;

	if (nla_put_u32(skb, OVPN_A_KEYCONF_PEER_ID, peer_id))
		goto err;

	if (nla_put_u32(skb, OVPN_A_KEYCONF_SLOT, slot) ||
	    nla_put_u32(skb, OVPN_A_KEYCONF_KEY_ID, keyconf->key_id) ||
	    nla_put_u32(skb, OVPN_A_KEYCONF_CIPHER_ALG, keyconf->cipher_alg))
		goto err;

	nla_nest_end(skb, attr);
	genlmsg_end(skb, hdr);

	return 0;
err:
	genlmsg_cancel(skb, hdr);
	return -EMSGSIZE;
}

int ovpn_nl_key_get_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[OVPN_A_KEYCONF_MAX + 1];
	struct ovpn_priv *ovpn = info->user_ptr[0];
	struct ovpn_key_config keyconf = { 0 };
	enum ovpn_key_slot slot;
	struct ovpn_peer *peer;
	struct sk_buff *msg;
	u32 peer_id;
	int ret, i;

	if (GENL_REQ_ATTR_CHECK(info, OVPN_A_KEYCONF))
		return -EINVAL;

	ret = nla_parse_nested(attrs, OVPN_A_KEYCONF_MAX,
			       info->attrs[OVPN_A_KEYCONF],
			       ovpn_keyconf_get_nl_policy, info->extack);
	if (ret)
		return ret;

	if (NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_KEYCONF], attrs,
			      OVPN_A_KEYCONF_PEER_ID))
		return -EINVAL;

	if (NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_KEYCONF], attrs,
			      OVPN_A_KEYCONF_SLOT))
		return -EINVAL;

	/* OVPN_CMD_KEY_GET expects only the PEER_ID and the SLOT, therefore
	 * ensure that the user hasn't specified any other attribute.
	 *
	 * Unfortunately this check cannot be performed via netlink
	 * spec/policy and must be open-coded.
	 */
	for (i = 0; i < OVPN_A_KEYCONF_MAX + 1; i++) {
		if (i == OVPN_A_KEYCONF_PEER_ID ||
		    i == OVPN_A_KEYCONF_SLOT)
			continue;

		if (attrs[i]) {
			NL_SET_ERR_MSG_FMT_MOD(info->extack,
					       "unexpected attribute %u", i);
			return -EINVAL;
		}
	}

	peer_id = nla_get_u32(attrs[OVPN_A_KEYCONF_PEER_ID]);
	peer = ovpn_peer_get_by_id(ovpn, peer_id);
	if (!peer) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "cannot find peer with id %u", peer_id);
		return -ENOENT;
	}

	slot = nla_get_u32(attrs[OVPN_A_KEYCONF_SLOT]);

	ret = ovpn_crypto_config_get(&peer->crypto, slot, &keyconf);
	if (ret < 0) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "cannot extract key from slot %u for peer %u",
				       slot, peer_id);
		goto err;
	}

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_KERNEL);
	if (!msg) {
		ret = -ENOMEM;
		goto err;
	}

	ret = ovpn_nl_send_key(msg, info, peer->id, slot, &keyconf);
	if (ret < 0) {
		nlmsg_free(msg);
		goto err;
	}

	ret = genlmsg_reply(msg, info);
err:
	ovpn_peer_put(peer);
	return ret;
}

int ovpn_nl_key_swap_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct ovpn_priv *ovpn = info->user_ptr[0];
	struct nlattr *attrs[OVPN_A_PEER_MAX + 1];
	struct ovpn_peer *peer;
	u32 peer_id;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, OVPN_A_KEYCONF))
		return -EINVAL;

	ret = nla_parse_nested(attrs, OVPN_A_KEYCONF_MAX,
			       info->attrs[OVPN_A_KEYCONF],
			       ovpn_keyconf_swap_input_nl_policy, info->extack);
	if (ret)
		return ret;

	if (NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_KEYCONF], attrs,
			      OVPN_A_KEYCONF_PEER_ID))
		return -EINVAL;

	peer_id = nla_get_u32(attrs[OVPN_A_KEYCONF_PEER_ID]);
	peer = ovpn_peer_get_by_id(ovpn, peer_id);
	if (!peer) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "no peer with id %u to swap keys for",
				       peer_id);
		return -ENOENT;
	}

	ovpn_crypto_key_slots_swap(&peer->crypto);
	ovpn_peer_put(peer);

	return 0;
}

int ovpn_nl_key_del_doit(struct sk_buff *skb, struct genl_info *info)
{
	struct nlattr *attrs[OVPN_A_KEYCONF_MAX + 1];
	struct ovpn_priv *ovpn = info->user_ptr[0];
	enum ovpn_key_slot slot;
	struct ovpn_peer *peer;
	u32 peer_id;
	int ret;

	if (GENL_REQ_ATTR_CHECK(info, OVPN_A_KEYCONF))
		return -EINVAL;

	ret = nla_parse_nested(attrs, OVPN_A_KEYCONF_MAX,
			       info->attrs[OVPN_A_KEYCONF],
			       ovpn_keyconf_del_input_nl_policy, info->extack);
	if (ret)
		return ret;

	if (NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_KEYCONF], attrs,
			      OVPN_A_KEYCONF_PEER_ID))
		return -EINVAL;

	if (NL_REQ_ATTR_CHECK(info->extack, info->attrs[OVPN_A_KEYCONF], attrs,
			      OVPN_A_KEYCONF_SLOT))
		return -EINVAL;

	peer_id = nla_get_u32(attrs[OVPN_A_KEYCONF_PEER_ID]);
	slot = nla_get_u32(attrs[OVPN_A_KEYCONF_SLOT]);

	peer = ovpn_peer_get_by_id(ovpn, peer_id);
	if (!peer) {
		NL_SET_ERR_MSG_FMT_MOD(info->extack,
				       "no peer with id %u to delete key for",
				       peer_id);
		return -ENOENT;
	}

	ovpn_crypto_key_slot_delete(&peer->crypto, slot);
	ovpn_peer_put(peer);

	return 0;
}

/**
 * ovpn_nl_peer_del_notify - notify userspace about peer being deleted
 * @peer: the peer being deleted
 *
 * Return: 0 on success or a negative error code otherwise
 */
int ovpn_nl_peer_del_notify(struct ovpn_peer *peer)
{
	struct ovpn_socket *sock;
	struct sk_buff *msg;
	struct nlattr *attr;
	int ret = -EMSGSIZE;
	void *hdr;

	netdev_info(peer->ovpn->dev, "deleting peer with id %u, reason %d\n",
		    peer->id, peer->delete_reason);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &ovpn_nl_family, 0, OVPN_CMD_PEER_DEL_NTF);
	if (!hdr) {
		ret = -ENOBUFS;
		goto err_free_msg;
	}

	if (nla_put_u32(msg, OVPN_A_IFINDEX, peer->ovpn->dev->ifindex))
		goto err_cancel_msg;

	attr = nla_nest_start(msg, OVPN_A_PEER);
	if (!attr)
		goto err_cancel_msg;

	if (nla_put_u32(msg, OVPN_A_PEER_DEL_REASON, peer->delete_reason))
		goto err_cancel_msg;

	if (nla_put_u32(msg, OVPN_A_PEER_ID, peer->id))
		goto err_cancel_msg;

	nla_nest_end(msg, attr);

	genlmsg_end(msg, hdr);

	rcu_read_lock();
	sock = rcu_dereference(peer->sock);
	if (!sock) {
		ret = -EINVAL;
		goto err_unlock;
	}
	genlmsg_multicast_netns(&ovpn_nl_family, sock_net(sock->sk), msg, 0,
				OVPN_NLGRP_PEERS, GFP_ATOMIC);
	rcu_read_unlock();

	return 0;

err_unlock:
	rcu_read_unlock();
err_cancel_msg:
	genlmsg_cancel(msg, hdr);
err_free_msg:
	nlmsg_free(msg);
	return ret;
}

/**
 * ovpn_nl_key_swap_notify - notify userspace peer's key must be renewed
 * @peer: the peer whose key needs to be renewed
 * @key_id: the ID of the key that needs to be renewed
 *
 * Return: 0 on success or a negative error code otherwise
 */
int ovpn_nl_key_swap_notify(struct ovpn_peer *peer, u8 key_id)
{
	struct ovpn_socket *sock;
	struct nlattr *k_attr;
	struct sk_buff *msg;
	int ret = -EMSGSIZE;
	void *hdr;

	netdev_info(peer->ovpn->dev, "peer with id %u must rekey - primary key unusable.\n",
		    peer->id);

	msg = nlmsg_new(NLMSG_DEFAULT_SIZE, GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	hdr = genlmsg_put(msg, 0, 0, &ovpn_nl_family, 0, OVPN_CMD_KEY_SWAP_NTF);
	if (!hdr) {
		ret = -ENOBUFS;
		goto err_free_msg;
	}

	if (nla_put_u32(msg, OVPN_A_IFINDEX, peer->ovpn->dev->ifindex))
		goto err_cancel_msg;

	k_attr = nla_nest_start(msg, OVPN_A_KEYCONF);
	if (!k_attr)
		goto err_cancel_msg;

	if (nla_put_u32(msg, OVPN_A_KEYCONF_PEER_ID, peer->id))
		goto err_cancel_msg;

	if (nla_put_u16(msg, OVPN_A_KEYCONF_KEY_ID, key_id))
		goto err_cancel_msg;

	nla_nest_end(msg, k_attr);
	genlmsg_end(msg, hdr);

	rcu_read_lock();
	sock = rcu_dereference(peer->sock);
	if (!sock) {
		ret = -EINVAL;
		goto err_unlock;
	}
	genlmsg_multicast_netns(&ovpn_nl_family, sock_net(sock->sk), msg, 0,
				OVPN_NLGRP_PEERS, GFP_ATOMIC);
	rcu_read_unlock();

	return 0;
err_unlock:
	rcu_read_unlock();
err_cancel_msg:
	genlmsg_cancel(msg, hdr);
err_free_msg:
	nlmsg_free(msg);
	return ret;
}

/**
 * ovpn_nl_register - perform any needed registration in the NL subsustem
 *
 * Return: 0 on success, a negative error code otherwise
 */
int __init ovpn_nl_register(void)
{
	int ret = genl_register_family(&ovpn_nl_family);

	if (ret) {
		pr_err("ovpn: genl_register_family failed: %d\n", ret);
		return ret;
	}

	return 0;
}

/**
 * ovpn_nl_unregister - undo any module wide netlink registration
 */
void ovpn_nl_unregister(void)
{
	genl_unregister_family(&ovpn_nl_family);
}
