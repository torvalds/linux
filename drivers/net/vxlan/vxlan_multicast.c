// SPDX-License-Identifier: GPL-2.0-only
/*
 *	Vxlan multicast group handling
 *
 */
#include <linux/kernel.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <linux/igmp.h>
#include <net/vxlan.h>

#include "vxlan_private.h"

/* Update multicast group membership when first VNI on
 * multicast address is brought up
 */
int vxlan_igmp_join(struct vxlan_dev *vxlan, union vxlan_addr *rip,
		    int rifindex)
{
	union vxlan_addr *ip = (rip ? : &vxlan->default_dst.remote_ip);
	int ifindex = (rifindex ? : vxlan->default_dst.remote_ifindex);
	int ret = -EINVAL;
	struct sock *sk;

	if (ip->sa.sa_family == AF_INET) {
		struct vxlan_sock *sock4 = rtnl_dereference(vxlan->vn4_sock);
		struct ip_mreqn mreq = {
			.imr_multiaddr.s_addr	= ip->sin.sin_addr.s_addr,
			.imr_ifindex		= ifindex,
		};

		sk = sock4->sock->sk;
		lock_sock(sk);
		ret = ip_mc_join_group(sk, &mreq);
		release_sock(sk);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		struct vxlan_sock *sock6 = rtnl_dereference(vxlan->vn6_sock);

		sk = sock6->sock->sk;
		lock_sock(sk);
		ret = ipv6_stub->ipv6_sock_mc_join(sk, ifindex,
						   &ip->sin6.sin6_addr);
		release_sock(sk);
#endif
	}

	return ret;
}

int vxlan_igmp_leave(struct vxlan_dev *vxlan, union vxlan_addr *rip,
		     int rifindex)
{
	union vxlan_addr *ip = (rip ? : &vxlan->default_dst.remote_ip);
	int ifindex = (rifindex ? : vxlan->default_dst.remote_ifindex);
	int ret = -EINVAL;
	struct sock *sk;

	if (ip->sa.sa_family == AF_INET) {
		struct vxlan_sock *sock4 = rtnl_dereference(vxlan->vn4_sock);
		struct ip_mreqn mreq = {
			.imr_multiaddr.s_addr	= ip->sin.sin_addr.s_addr,
			.imr_ifindex		= ifindex,
		};

		sk = sock4->sock->sk;
		lock_sock(sk);
		ret = ip_mc_leave_group(sk, &mreq);
		release_sock(sk);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		struct vxlan_sock *sock6 = rtnl_dereference(vxlan->vn6_sock);

		sk = sock6->sock->sk;
		lock_sock(sk);
		ret = ipv6_stub->ipv6_sock_mc_drop(sk, ifindex,
						   &ip->sin6.sin6_addr);
		release_sock(sk);
#endif
	}

	return ret;
}

/* See if multicast group is already in use by other ID */
bool vxlan_group_used(struct vxlan_net *vn, struct vxlan_dev *dev,
		      union vxlan_addr *rip, int rifindex)
{
	union vxlan_addr *ip = (rip ? : &dev->default_dst.remote_ip);
	int ifindex = (rifindex ? : dev->default_dst.remote_ifindex);
	struct vxlan_dev *vxlan;
	struct vxlan_sock *sock4;
#if IS_ENABLED(CONFIG_IPV6)
	struct vxlan_sock *sock6;
#endif
	unsigned short family = dev->default_dst.remote_ip.sa.sa_family;

	sock4 = rtnl_dereference(dev->vn4_sock);

	/* The vxlan_sock is only used by dev, leaving group has
	 * no effect on other vxlan devices.
	 */
	if (family == AF_INET && sock4 && refcount_read(&sock4->refcnt) == 1)
		return false;

#if IS_ENABLED(CONFIG_IPV6)
	sock6 = rtnl_dereference(dev->vn6_sock);
	if (family == AF_INET6 && sock6 && refcount_read(&sock6->refcnt) == 1)
		return false;
#endif

	list_for_each_entry(vxlan, &vn->vxlan_list, next) {
		if (!netif_running(vxlan->dev) || vxlan == dev)
			continue;

		if (family == AF_INET &&
		    rtnl_dereference(vxlan->vn4_sock) != sock4)
			continue;
#if IS_ENABLED(CONFIG_IPV6)
		if (family == AF_INET6 &&
		    rtnl_dereference(vxlan->vn6_sock) != sock6)
			continue;
#endif
		if (!vxlan_addr_equal(&vxlan->default_dst.remote_ip, ip))
			continue;

		if (vxlan->default_dst.remote_ifindex != ifindex)
			continue;

		return true;
	}

	return false;
}
