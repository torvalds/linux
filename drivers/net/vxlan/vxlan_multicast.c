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
int vxlan_igmp_join(struct vxlan_dev *vxlan, const union vxlan_addr *rip,
		    int rifindex)
{
	const struct vxlan_config *cfg = rtnl_dereference(vxlan->cfg);
	const union vxlan_addr *ip = (rip ? : &cfg->remote_ip);
	int ifindex = (rifindex ? : cfg->remote_ifindex);
	int ret = -EINVAL;
	struct sock *sk;

	if (ip->sa.sa_family == AF_INET) {
		struct vxlan_sock *sock4 = rtnl_dereference(vxlan->vn4_sock);
		struct ip_mreqn mreq = {
			.imr_multiaddr.s_addr	= ip->sin.sin_addr.s_addr,
			.imr_ifindex		= ifindex,
		};

		sk = sock4->sk;
		lock_sock(sk);
		ret = ip_mc_join_group(sk, &mreq);
		release_sock(sk);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		struct vxlan_sock *sock6 = rtnl_dereference(vxlan->vn6_sock);

		sk = sock6->sk;
		lock_sock(sk);
		ret = ipv6_sock_mc_join(sk, ifindex, &ip->sin6.sin6_addr);
		release_sock(sk);
#endif
	}

	return ret;
}

int vxlan_igmp_leave(struct vxlan_dev *vxlan, const union vxlan_addr *rip,
		     int rifindex)
{
	const struct vxlan_config *cfg = rtnl_dereference(vxlan->cfg);
	const union vxlan_addr *ip = (rip ? : &cfg->remote_ip);
	int ifindex = (rifindex ? : cfg->remote_ifindex);
	int ret = -EINVAL;
	struct sock *sk;

	if (ip->sa.sa_family == AF_INET) {
		struct vxlan_sock *sock4 = rtnl_dereference(vxlan->vn4_sock);
		struct ip_mreqn mreq = {
			.imr_multiaddr.s_addr	= ip->sin.sin_addr.s_addr,
			.imr_ifindex		= ifindex,
		};

		sk = sock4->sk;
		lock_sock(sk);
		ret = ip_mc_leave_group(sk, &mreq);
		release_sock(sk);
#if IS_ENABLED(CONFIG_IPV6)
	} else {
		struct vxlan_sock *sock6 = rtnl_dereference(vxlan->vn6_sock);

		sk = sock6->sk;
		lock_sock(sk);
		ret = ipv6_sock_mc_drop(sk, ifindex, &ip->sin6.sin6_addr);
		release_sock(sk);
#endif
	}

	return ret;
}

static bool vxlan_group_used_match(const union vxlan_addr *ip, int ifindex,
				   const union vxlan_addr *rip, int rifindex)
{
	if (!vxlan_addr_multicast(rip))
		return false;

	if (!vxlan_addr_equal(rip, ip))
		return false;

	if (rifindex != ifindex)
		return false;

	return true;
}

static bool vxlan_group_used_by_vnifilter(struct vxlan_dev *vxlan,
					  const struct vxlan_config *cfg,
					  const union vxlan_addr *ip,
					  int ifindex)
{
	struct vxlan_vni_group *vg = rtnl_dereference(vxlan->vnigrp);
	struct vxlan_vni_node *v, *tmp;

	if (vxlan_group_used_match(ip, ifindex,
				   &cfg->remote_ip,
				   cfg->remote_ifindex))
		return true;

	list_for_each_entry_safe(v, tmp, &vg->vni_list, vlist) {
		if (!vxlan_addr_multicast(&v->remote_ip))
			continue;

		if (vxlan_group_used_match(ip, ifindex,
					   &v->remote_ip,
					   cfg->remote_ifindex))
			return true;
	}

	return false;
}

/* See if multicast group is already in use by other ID */
bool vxlan_group_used(struct vxlan_net *vn, struct vxlan_dev *dev,
		      __be32 vni, const union vxlan_addr *rip, int rifindex)
{
	const struct vxlan_config *dev_cfg = rtnl_dereference(dev->cfg);
	const union vxlan_addr *ip = (rip ? : &dev_cfg->remote_ip);
	int ifindex = (rifindex ? : dev_cfg->remote_ifindex);
	struct vxlan_dev *vxlan;
	struct vxlan_sock *sock4;
#if IS_ENABLED(CONFIG_IPV6)
	struct vxlan_sock *sock6;
#endif
	unsigned short family = dev_cfg->remote_ip.sa.sa_family;

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
		const struct vxlan_config *cfg;

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
		cfg = rtnl_dereference(vxlan->cfg);

		if (cfg->flags & VXLAN_F_VNIFILTER) {
			if (!vxlan_group_used_by_vnifilter(vxlan, cfg, ip, ifindex))
				continue;
		} else {
			if (!vxlan_group_used_match(ip, ifindex,
						    &cfg->remote_ip,
						    cfg->remote_ifindex))
				continue;
		}

		return true;
	}

	return false;
}

static int vxlan_multicast_join_vnigrp(struct vxlan_dev *vxlan,
				       const struct vxlan_config *cfg)
{
	struct vxlan_vni_group *vg = rtnl_dereference(vxlan->vnigrp);
	struct vxlan_vni_node *v, *tmp, *vgood = NULL;
	int ret = 0;

	list_for_each_entry_safe(v, tmp, &vg->vni_list, vlist) {
		if (!vxlan_addr_multicast(&v->remote_ip))
			continue;
		/* skip if address is same as default address */
		if (vxlan_addr_equal(&v->remote_ip,
				     &cfg->remote_ip))
			continue;
		ret = vxlan_igmp_join(vxlan, &v->remote_ip, 0);
		if (ret == -EADDRINUSE)
			ret = 0;
		if (ret)
			goto out;
		vgood = v;
	}
out:
	if (ret) {
		list_for_each_entry_safe(v, tmp, &vg->vni_list, vlist) {
			if (!vxlan_addr_multicast(&v->remote_ip))
				continue;
			if (vxlan_addr_equal(&v->remote_ip,
					     &cfg->remote_ip))
				continue;
			vxlan_igmp_leave(vxlan, &v->remote_ip, 0);
			if (v == vgood)
				break;
		}
	}

	return ret;
}

static int vxlan_multicast_leave_vnigrp(struct vxlan_dev *vxlan,
					const struct vxlan_config *cfg)
{
	struct vxlan_net *vn = net_generic(vxlan->net, vxlan_net_id);
	struct vxlan_vni_group *vg = rtnl_dereference(vxlan->vnigrp);
	struct vxlan_vni_node *v, *tmp;
	int last_err = 0, ret;

	list_for_each_entry_safe(v, tmp, &vg->vni_list, vlist) {
		if (!vxlan_addr_multicast(&v->remote_ip))
			continue;
		/* skip if address is same as default address */
		if (vxlan_addr_equal(&v->remote_ip,
				     &cfg->remote_ip))
			continue;
		if (!vxlan_group_used(vn, vxlan, v->vni, &v->remote_ip,
				      0)) {
			ret = vxlan_igmp_leave(vxlan, &v->remote_ip, 0);
			if (ret == -EADDRNOTAVAIL)
				ret = 0;
			if (ret)
				last_err = ret;
		}
	}

	return last_err;
}

int vxlan_multicast_join(struct vxlan_dev *vxlan)
{
	const struct vxlan_config *cfg = rtnl_dereference(vxlan->cfg);
	int ret = 0;

	if (vxlan_addr_multicast(&cfg->remote_ip)) {
		ret = vxlan_igmp_join(vxlan, &cfg->remote_ip,
				      cfg->remote_ifindex);
		if (ret == -EADDRINUSE)
			ret = 0;
		if (ret)
			return ret;
	}

	if (cfg->flags & VXLAN_F_VNIFILTER)
		return vxlan_multicast_join_vnigrp(vxlan, cfg);

	return 0;
}

int vxlan_multicast_leave(struct vxlan_dev *vxlan)
{
	const struct vxlan_config *cfg = rtnl_dereference(vxlan->cfg);
	struct vxlan_net *vn = net_generic(vxlan->net, vxlan_net_id);
	int ret = 0;

	if (vxlan_addr_multicast(&cfg->remote_ip) &&
	    !vxlan_group_used(vn, vxlan, 0, NULL, 0)) {
		ret = vxlan_igmp_leave(vxlan, &cfg->remote_ip,
				       cfg->remote_ifindex);
		if (ret == -EADDRNOTAVAIL)
			ret = 0;
		if (ret)
			return ret;
	}

	if (cfg->flags & VXLAN_F_VNIFILTER)
		return vxlan_multicast_leave_vnigrp(vxlan, cfg);

	return 0;
}
