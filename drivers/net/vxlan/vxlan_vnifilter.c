// SPDX-License-Identifier: GPL-2.0-only
/*
 *	Vxlan vni filter for collect metadata mode
 *
 *	Authors: Roopa Prabhu <roopa@nvidia.com>
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/etherdevice.h>
#include <linux/rhashtable.h>
#include <net/rtnetlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>
#include <net/vxlan.h>

#include "vxlan_private.h"

static inline int vxlan_vni_cmp(struct rhashtable_compare_arg *arg,
				const void *ptr)
{
	const struct vxlan_vni_node *vnode = ptr;
	__be32 vni = *(__be32 *)arg->key;

	return vnode->vni != vni;
}

const struct rhashtable_params vxlan_vni_rht_params = {
	.head_offset = offsetof(struct vxlan_vni_node, vnode),
	.key_offset = offsetof(struct vxlan_vni_node, vni),
	.key_len = sizeof(__be32),
	.nelem_hint = 3,
	.max_size = VXLAN_N_VID,
	.obj_cmpfn = vxlan_vni_cmp,
	.automatic_shrinking = true,
};

static void vxlan_vs_add_del_vninode(struct vxlan_dev *vxlan,
				     struct vxlan_vni_node *v,
				     bool del)
{
	struct vxlan_net *vn = net_generic(vxlan->net, vxlan_net_id);
	struct vxlan_dev_node *node;
	struct vxlan_sock *vs;

	spin_lock(&vn->sock_lock);
	if (del) {
		if (!hlist_unhashed(&v->hlist4.hlist))
			hlist_del_init_rcu(&v->hlist4.hlist);
#if IS_ENABLED(CONFIG_IPV6)
		if (!hlist_unhashed(&v->hlist6.hlist))
			hlist_del_init_rcu(&v->hlist6.hlist);
#endif
		goto out;
	}

#if IS_ENABLED(CONFIG_IPV6)
	vs = rtnl_dereference(vxlan->vn6_sock);
	if (vs && v) {
		node = &v->hlist6;
		hlist_add_head_rcu(&node->hlist, vni_head(vs, v->vni));
	}
#endif
	vs = rtnl_dereference(vxlan->vn4_sock);
	if (vs && v) {
		node = &v->hlist4;
		hlist_add_head_rcu(&node->hlist, vni_head(vs, v->vni));
	}
out:
	spin_unlock(&vn->sock_lock);
}

void vxlan_vs_add_vnigrp(struct vxlan_dev *vxlan,
			 struct vxlan_sock *vs,
			 bool ipv6)
{
	struct vxlan_net *vn = net_generic(vxlan->net, vxlan_net_id);
	struct vxlan_vni_group *vg = rtnl_dereference(vxlan->vnigrp);
	struct vxlan_vni_node *v, *tmp;
	struct vxlan_dev_node *node;

	if (!vg)
		return;

	spin_lock(&vn->sock_lock);
	list_for_each_entry_safe(v, tmp, &vg->vni_list, vlist) {
#if IS_ENABLED(CONFIG_IPV6)
		if (ipv6)
			node = &v->hlist6;
		else
#endif
			node = &v->hlist4;
		node->vxlan = vxlan;
		hlist_add_head_rcu(&node->hlist, vni_head(vs, v->vni));
	}
	spin_unlock(&vn->sock_lock);
}

void vxlan_vs_del_vnigrp(struct vxlan_dev *vxlan)
{
	struct vxlan_vni_group *vg = rtnl_dereference(vxlan->vnigrp);
	struct vxlan_net *vn = net_generic(vxlan->net, vxlan_net_id);
	struct vxlan_vni_node *v, *tmp;

	if (!vg)
		return;

	spin_lock(&vn->sock_lock);
	list_for_each_entry_safe(v, tmp, &vg->vni_list, vlist) {
		hlist_del_init_rcu(&v->hlist4.hlist);
#if IS_ENABLED(CONFIG_IPV6)
		hlist_del_init_rcu(&v->hlist6.hlist);
#endif
	}
	spin_unlock(&vn->sock_lock);
}

static void vxlan_vnifilter_stats_get(const struct vxlan_vni_node *vninode,
				      struct vxlan_vni_stats *dest)
{
	int i;

	memset(dest, 0, sizeof(*dest));
	for_each_possible_cpu(i) {
		struct vxlan_vni_stats_pcpu *pstats;
		struct vxlan_vni_stats temp;
		unsigned int start;

		pstats = per_cpu_ptr(vninode->stats, i);
		do {
			start = u64_stats_fetch_begin(&pstats->syncp);
			memcpy(&temp, &pstats->stats, sizeof(temp));
		} while (u64_stats_fetch_retry(&pstats->syncp, start));

		dest->rx_packets += temp.rx_packets;
		dest->rx_bytes += temp.rx_bytes;
		dest->rx_drops += temp.rx_drops;
		dest->rx_errors += temp.rx_errors;
		dest->tx_packets += temp.tx_packets;
		dest->tx_bytes += temp.tx_bytes;
		dest->tx_drops += temp.tx_drops;
		dest->tx_errors += temp.tx_errors;
	}
}

static void vxlan_vnifilter_stats_add(struct vxlan_vni_node *vninode,
				      int type, unsigned int len)
{
	struct vxlan_vni_stats_pcpu *pstats = this_cpu_ptr(vninode->stats);

	u64_stats_update_begin(&pstats->syncp);
	switch (type) {
	case VXLAN_VNI_STATS_RX:
		pstats->stats.rx_bytes += len;
		pstats->stats.rx_packets++;
		break;
	case VXLAN_VNI_STATS_RX_DROPS:
		pstats->stats.rx_drops++;
		break;
	case VXLAN_VNI_STATS_RX_ERRORS:
		pstats->stats.rx_errors++;
		break;
	case VXLAN_VNI_STATS_TX:
		pstats->stats.tx_bytes += len;
		pstats->stats.tx_packets++;
		break;
	case VXLAN_VNI_STATS_TX_DROPS:
		pstats->stats.tx_drops++;
		break;
	case VXLAN_VNI_STATS_TX_ERRORS:
		pstats->stats.tx_errors++;
		break;
	}
	u64_stats_update_end(&pstats->syncp);
}

void vxlan_vnifilter_count(struct vxlan_dev *vxlan, __be32 vni,
			   struct vxlan_vni_node *vninode,
			   int type, unsigned int len)
{
	struct vxlan_vni_node *vnode;

	if (!(vxlan->cfg.flags & VXLAN_F_VNIFILTER))
		return;

	if (vninode) {
		vnode = vninode;
	} else {
		vnode = vxlan_vnifilter_lookup(vxlan, vni);
		if (!vnode)
			return;
	}

	vxlan_vnifilter_stats_add(vnode, type, len);
}

static u32 vnirange(struct vxlan_vni_node *vbegin,
		    struct vxlan_vni_node *vend)
{
	return (be32_to_cpu(vend->vni) - be32_to_cpu(vbegin->vni));
}

static size_t vxlan_vnifilter_entry_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct tunnel_msg))
		+ nla_total_size(0) /* VXLAN_VNIFILTER_ENTRY */
		+ nla_total_size(sizeof(u32)) /* VXLAN_VNIFILTER_ENTRY_START */
		+ nla_total_size(sizeof(u32)) /* VXLAN_VNIFILTER_ENTRY_END */
		+ nla_total_size(sizeof(struct in6_addr));/* VXLAN_VNIFILTER_ENTRY_GROUP{6} */
}

static int __vnifilter_entry_fill_stats(struct sk_buff *skb,
					const struct vxlan_vni_node *vbegin)
{
	struct vxlan_vni_stats vstats;
	struct nlattr *vstats_attr;

	vstats_attr = nla_nest_start(skb, VXLAN_VNIFILTER_ENTRY_STATS);
	if (!vstats_attr)
		goto out_stats_err;

	vxlan_vnifilter_stats_get(vbegin, &vstats);
	if (nla_put_u64_64bit(skb, VNIFILTER_ENTRY_STATS_RX_BYTES,
			      vstats.rx_bytes, VNIFILTER_ENTRY_STATS_PAD) ||
	    nla_put_u64_64bit(skb, VNIFILTER_ENTRY_STATS_RX_PKTS,
			      vstats.rx_packets, VNIFILTER_ENTRY_STATS_PAD) ||
	    nla_put_u64_64bit(skb, VNIFILTER_ENTRY_STATS_RX_DROPS,
			      vstats.rx_drops, VNIFILTER_ENTRY_STATS_PAD) ||
	    nla_put_u64_64bit(skb, VNIFILTER_ENTRY_STATS_RX_ERRORS,
			      vstats.rx_errors, VNIFILTER_ENTRY_STATS_PAD) ||
	    nla_put_u64_64bit(skb, VNIFILTER_ENTRY_STATS_TX_BYTES,
			      vstats.tx_bytes, VNIFILTER_ENTRY_STATS_PAD) ||
	    nla_put_u64_64bit(skb, VNIFILTER_ENTRY_STATS_TX_PKTS,
			      vstats.tx_packets, VNIFILTER_ENTRY_STATS_PAD) ||
	    nla_put_u64_64bit(skb, VNIFILTER_ENTRY_STATS_TX_DROPS,
			      vstats.tx_drops, VNIFILTER_ENTRY_STATS_PAD) ||
	    nla_put_u64_64bit(skb, VNIFILTER_ENTRY_STATS_TX_ERRORS,
			      vstats.tx_errors, VNIFILTER_ENTRY_STATS_PAD))
		goto out_stats_err;

	nla_nest_end(skb, vstats_attr);

	return 0;

out_stats_err:
	nla_nest_cancel(skb, vstats_attr);
	return -EMSGSIZE;
}

static bool vxlan_fill_vni_filter_entry(struct sk_buff *skb,
					struct vxlan_vni_node *vbegin,
					struct vxlan_vni_node *vend,
					bool fill_stats)
{
	struct nlattr *ventry;
	u32 vs = be32_to_cpu(vbegin->vni);
	u32 ve = 0;

	if (vbegin != vend)
		ve = be32_to_cpu(vend->vni);

	ventry = nla_nest_start(skb, VXLAN_VNIFILTER_ENTRY);
	if (!ventry)
		return false;

	if (nla_put_u32(skb, VXLAN_VNIFILTER_ENTRY_START, vs))
		goto out_err;

	if (ve && nla_put_u32(skb, VXLAN_VNIFILTER_ENTRY_END, ve))
		goto out_err;

	if (!vxlan_addr_any(&vbegin->remote_ip)) {
		if (vbegin->remote_ip.sa.sa_family == AF_INET) {
			if (nla_put_in_addr(skb, VXLAN_VNIFILTER_ENTRY_GROUP,
					    vbegin->remote_ip.sin.sin_addr.s_addr))
				goto out_err;
#if IS_ENABLED(CONFIG_IPV6)
		} else {
			if (nla_put_in6_addr(skb, VXLAN_VNIFILTER_ENTRY_GROUP6,
					     &vbegin->remote_ip.sin6.sin6_addr))
				goto out_err;
#endif
		}
	}

	if (fill_stats && __vnifilter_entry_fill_stats(skb, vbegin))
		goto out_err;

	nla_nest_end(skb, ventry);

	return true;

out_err:
	nla_nest_cancel(skb, ventry);

	return false;
}

static void vxlan_vnifilter_notify(const struct vxlan_dev *vxlan,
				   struct vxlan_vni_node *vninode, int cmd)
{
	struct tunnel_msg *tmsg;
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	struct net *net = dev_net(vxlan->dev);
	int err = -ENOBUFS;

	skb = nlmsg_new(vxlan_vnifilter_entry_nlmsg_size(), GFP_KERNEL);
	if (!skb)
		goto out_err;

	err = -EMSGSIZE;
	nlh = nlmsg_put(skb, 0, 0, cmd, sizeof(*tmsg), 0);
	if (!nlh)
		goto out_err;
	tmsg = nlmsg_data(nlh);
	memset(tmsg, 0, sizeof(*tmsg));
	tmsg->family = AF_BRIDGE;
	tmsg->ifindex = vxlan->dev->ifindex;

	if (!vxlan_fill_vni_filter_entry(skb, vninode, vninode, false))
		goto out_err;

	nlmsg_end(skb, nlh);
	rtnl_notify(skb, net, 0, RTNLGRP_TUNNEL, NULL, GFP_KERNEL);

	return;

out_err:
	rtnl_set_sk_err(net, RTNLGRP_TUNNEL, err);

	kfree_skb(skb);
}

static int vxlan_vnifilter_dump_dev(const struct net_device *dev,
				    struct sk_buff *skb,
				    struct netlink_callback *cb)
{
	struct vxlan_vni_node *tmp, *v, *vbegin = NULL, *vend = NULL;
	struct vxlan_dev *vxlan = netdev_priv(dev);
	struct tunnel_msg *new_tmsg, *tmsg;
	int idx = 0, s_idx = cb->args[1];
	struct vxlan_vni_group *vg;
	struct nlmsghdr *nlh;
	bool dump_stats;
	int err = 0;

	if (!(vxlan->cfg.flags & VXLAN_F_VNIFILTER))
		return -EINVAL;

	/* RCU needed because of the vni locking rules (rcu || rtnl) */
	vg = rcu_dereference(vxlan->vnigrp);
	if (!vg || !vg->num_vnis)
		return 0;

	tmsg = nlmsg_data(cb->nlh);
	dump_stats = !!(tmsg->flags & TUNNEL_MSG_FLAG_STATS);

	nlh = nlmsg_put(skb, NETLINK_CB(cb->skb).portid, cb->nlh->nlmsg_seq,
			RTM_NEWTUNNEL, sizeof(*new_tmsg), NLM_F_MULTI);
	if (!nlh)
		return -EMSGSIZE;
	new_tmsg = nlmsg_data(nlh);
	memset(new_tmsg, 0, sizeof(*new_tmsg));
	new_tmsg->family = PF_BRIDGE;
	new_tmsg->ifindex = dev->ifindex;

	list_for_each_entry_safe(v, tmp, &vg->vni_list, vlist) {
		if (idx < s_idx) {
			idx++;
			continue;
		}
		if (!vbegin) {
			vbegin = v;
			vend = v;
			continue;
		}
		if (!dump_stats && vnirange(vend, v) == 1 &&
		    vxlan_addr_equal(&v->remote_ip, &vend->remote_ip)) {
			goto update_end;
		} else {
			if (!vxlan_fill_vni_filter_entry(skb, vbegin, vend,
							 dump_stats)) {
				err = -EMSGSIZE;
				break;
			}
			idx += vnirange(vbegin, vend) + 1;
			vbegin = v;
		}
update_end:
		vend = v;
	}

	if (!err && vbegin) {
		if (!vxlan_fill_vni_filter_entry(skb, vbegin, vend, dump_stats))
			err = -EMSGSIZE;
	}

	cb->args[1] = err ? idx : 0;

	nlmsg_end(skb, nlh);

	return err;
}

static int vxlan_vnifilter_dump(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx = 0, err = 0, s_idx = cb->args[0];
	struct net *net = sock_net(skb->sk);
	struct tunnel_msg *tmsg;
	struct net_device *dev;

	if (cb->nlh->nlmsg_len < nlmsg_msg_size(sizeof(struct tunnel_msg))) {
		NL_SET_ERR_MSG(cb->extack, "Invalid msg length");
		return -EINVAL;
	}

	tmsg = nlmsg_data(cb->nlh);

	if (tmsg->flags & ~TUNNEL_MSG_VALID_USER_FLAGS) {
		NL_SET_ERR_MSG(cb->extack, "Invalid tunnelmsg flags in ancillary header");
		return -EINVAL;
	}

	rcu_read_lock();
	if (tmsg->ifindex) {
		dev = dev_get_by_index_rcu(net, tmsg->ifindex);
		if (!dev) {
			err = -ENODEV;
			goto out_err;
		}
		if (!netif_is_vxlan(dev)) {
			NL_SET_ERR_MSG(cb->extack,
				       "The device is not a vxlan device");
			err = -EINVAL;
			goto out_err;
		}
		err = vxlan_vnifilter_dump_dev(dev, skb, cb);
		/* if the dump completed without an error we return 0 here */
		if (err != -EMSGSIZE)
			goto out_err;
	} else {
		for_each_netdev_rcu(net, dev) {
			if (!netif_is_vxlan(dev))
				continue;
			if (idx < s_idx)
				goto skip;
			err = vxlan_vnifilter_dump_dev(dev, skb, cb);
			if (err == -EMSGSIZE)
				break;
skip:
			idx++;
		}
	}
	cb->args[0] = idx;
	rcu_read_unlock();

	return skb->len;

out_err:
	rcu_read_unlock();

	return err;
}

static const struct nla_policy vni_filter_entry_policy[VXLAN_VNIFILTER_ENTRY_MAX + 1] = {
	[VXLAN_VNIFILTER_ENTRY_START] = { .type = NLA_U32 },
	[VXLAN_VNIFILTER_ENTRY_END] = { .type = NLA_U32 },
	[VXLAN_VNIFILTER_ENTRY_GROUP]	= { .type = NLA_BINARY,
					    .len = sizeof_field(struct iphdr, daddr) },
	[VXLAN_VNIFILTER_ENTRY_GROUP6]	= { .type = NLA_BINARY,
					    .len = sizeof(struct in6_addr) },
};

static const struct nla_policy vni_filter_policy[VXLAN_VNIFILTER_MAX + 1] = {
	[VXLAN_VNIFILTER_ENTRY] = { .type = NLA_NESTED },
};

static int vxlan_update_default_fdb_entry(struct vxlan_dev *vxlan, __be32 vni,
					  union vxlan_addr *old_remote_ip,
					  union vxlan_addr *remote_ip,
					  struct netlink_ext_ack *extack)
{
	struct vxlan_rdst *dst = &vxlan->default_dst;
	u32 hash_index;
	int err = 0;

	hash_index = fdb_head_index(vxlan, all_zeros_mac, vni);
	spin_lock_bh(&vxlan->hash_lock[hash_index]);
	if (remote_ip && !vxlan_addr_any(remote_ip)) {
		err = vxlan_fdb_update(vxlan, all_zeros_mac,
				       remote_ip,
				       NUD_REACHABLE | NUD_PERMANENT,
				       NLM_F_APPEND | NLM_F_CREATE,
				       vxlan->cfg.dst_port,
				       vni,
				       vni,
				       dst->remote_ifindex,
				       NTF_SELF, 0, true, extack);
		if (err) {
			spin_unlock_bh(&vxlan->hash_lock[hash_index]);
			return err;
		}
	}

	if (old_remote_ip && !vxlan_addr_any(old_remote_ip)) {
		__vxlan_fdb_delete(vxlan, all_zeros_mac,
				   *old_remote_ip,
				   vxlan->cfg.dst_port,
				   vni, vni,
				   dst->remote_ifindex,
				   true);
	}
	spin_unlock_bh(&vxlan->hash_lock[hash_index]);

	return err;
}

static int vxlan_vni_update_group(struct vxlan_dev *vxlan,
				  struct vxlan_vni_node *vninode,
				  union vxlan_addr *group,
				  bool create, bool *changed,
				  struct netlink_ext_ack *extack)
{
	struct vxlan_net *vn = net_generic(vxlan->net, vxlan_net_id);
	struct vxlan_rdst *dst = &vxlan->default_dst;
	union vxlan_addr *newrip = NULL, *oldrip = NULL;
	union vxlan_addr old_remote_ip;
	int ret = 0;

	memcpy(&old_remote_ip, &vninode->remote_ip, sizeof(old_remote_ip));

	/* if per vni remote ip is not present use vxlan dev
	 * default dst remote ip for fdb entry
	 */
	if (group && !vxlan_addr_any(group)) {
		newrip = group;
	} else {
		if (!vxlan_addr_any(&dst->remote_ip))
			newrip = &dst->remote_ip;
	}

	/* if old rip exists, and no newrip,
	 * explicitly delete old rip
	 */
	if (!newrip && !vxlan_addr_any(&old_remote_ip))
		oldrip = &old_remote_ip;

	if (!newrip && !oldrip)
		return 0;

	if (!create && oldrip && newrip && vxlan_addr_equal(oldrip, newrip))
		return 0;

	ret = vxlan_update_default_fdb_entry(vxlan, vninode->vni,
					     oldrip, newrip,
					     extack);
	if (ret)
		goto out;

	if (group)
		memcpy(&vninode->remote_ip, group, sizeof(vninode->remote_ip));

	if (vxlan->dev->flags & IFF_UP) {
		if (vxlan_addr_multicast(&old_remote_ip) &&
		    !vxlan_group_used(vn, vxlan, vninode->vni,
				      &old_remote_ip,
				      vxlan->default_dst.remote_ifindex)) {
			ret = vxlan_igmp_leave(vxlan, &old_remote_ip,
					       0);
			if (ret)
				goto out;
		}

		if (vxlan_addr_multicast(&vninode->remote_ip)) {
			ret = vxlan_igmp_join(vxlan, &vninode->remote_ip, 0);
			if (ret == -EADDRINUSE)
				ret = 0;
			if (ret)
				goto out;
		}
	}

	*changed = true;

	return 0;
out:
	return ret;
}

int vxlan_vnilist_update_group(struct vxlan_dev *vxlan,
			       union vxlan_addr *old_remote_ip,
			       union vxlan_addr *new_remote_ip,
			       struct netlink_ext_ack *extack)
{
	struct list_head *headp, *hpos;
	struct vxlan_vni_group *vg;
	struct vxlan_vni_node *vent;
	int ret;

	vg = rtnl_dereference(vxlan->vnigrp);

	headp = &vg->vni_list;
	list_for_each_prev(hpos, headp) {
		vent = list_entry(hpos, struct vxlan_vni_node, vlist);
		if (vxlan_addr_any(&vent->remote_ip)) {
			ret = vxlan_update_default_fdb_entry(vxlan, vent->vni,
							     old_remote_ip,
							     new_remote_ip,
							     extack);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static void vxlan_vni_delete_group(struct vxlan_dev *vxlan,
				   struct vxlan_vni_node *vninode)
{
	struct vxlan_net *vn = net_generic(vxlan->net, vxlan_net_id);
	struct vxlan_rdst *dst = &vxlan->default_dst;

	/* if per vni remote_ip not present, delete the
	 * default dst remote_ip previously added for this vni
	 */
	if (!vxlan_addr_any(&vninode->remote_ip) ||
	    !vxlan_addr_any(&dst->remote_ip))
		__vxlan_fdb_delete(vxlan, all_zeros_mac,
				   (vxlan_addr_any(&vninode->remote_ip) ?
				   dst->remote_ip : vninode->remote_ip),
				   vxlan->cfg.dst_port,
				   vninode->vni, vninode->vni,
				   dst->remote_ifindex,
				   true);

	if (vxlan->dev->flags & IFF_UP) {
		if (vxlan_addr_multicast(&vninode->remote_ip) &&
		    !vxlan_group_used(vn, vxlan, vninode->vni,
				      &vninode->remote_ip,
				      dst->remote_ifindex)) {
			vxlan_igmp_leave(vxlan, &vninode->remote_ip, 0);
		}
	}
}

static int vxlan_vni_update(struct vxlan_dev *vxlan,
			    struct vxlan_vni_group *vg,
			    __be32 vni, union vxlan_addr *group,
			    bool *changed,
			    struct netlink_ext_ack *extack)
{
	struct vxlan_vni_node *vninode;
	int ret;

	vninode = rhashtable_lookup_fast(&vg->vni_hash, &vni,
					 vxlan_vni_rht_params);
	if (!vninode)
		return 0;

	ret = vxlan_vni_update_group(vxlan, vninode, group, false, changed,
				     extack);
	if (ret)
		return ret;

	if (changed)
		vxlan_vnifilter_notify(vxlan, vninode, RTM_NEWTUNNEL);

	return 0;
}

static void __vxlan_vni_add_list(struct vxlan_vni_group *vg,
				 struct vxlan_vni_node *v)
{
	struct list_head *headp, *hpos;
	struct vxlan_vni_node *vent;

	headp = &vg->vni_list;
	list_for_each_prev(hpos, headp) {
		vent = list_entry(hpos, struct vxlan_vni_node, vlist);
		if (be32_to_cpu(v->vni) < be32_to_cpu(vent->vni))
			continue;
		else
			break;
	}
	list_add_rcu(&v->vlist, hpos);
	vg->num_vnis++;
}

static void __vxlan_vni_del_list(struct vxlan_vni_group *vg,
				 struct vxlan_vni_node *v)
{
	list_del_rcu(&v->vlist);
	vg->num_vnis--;
}

static struct vxlan_vni_node *vxlan_vni_alloc(struct vxlan_dev *vxlan,
					      __be32 vni)
{
	struct vxlan_vni_node *vninode;

	vninode = kzalloc(sizeof(*vninode), GFP_KERNEL);
	if (!vninode)
		return NULL;
	vninode->stats = netdev_alloc_pcpu_stats(struct vxlan_vni_stats_pcpu);
	if (!vninode->stats) {
		kfree(vninode);
		return NULL;
	}
	vninode->vni = vni;
	vninode->hlist4.vxlan = vxlan;
#if IS_ENABLED(CONFIG_IPV6)
	vninode->hlist6.vxlan = vxlan;
#endif

	return vninode;
}

static void vxlan_vni_free(struct vxlan_vni_node *vninode)
{
	free_percpu(vninode->stats);
	kfree(vninode);
}

static int vxlan_vni_add(struct vxlan_dev *vxlan,
			 struct vxlan_vni_group *vg,
			 u32 vni, union vxlan_addr *group,
			 struct netlink_ext_ack *extack)
{
	struct vxlan_vni_node *vninode;
	__be32 v = cpu_to_be32(vni);
	bool changed = false;
	int err = 0;

	if (vxlan_vnifilter_lookup(vxlan, v))
		return vxlan_vni_update(vxlan, vg, v, group, &changed, extack);

	err = vxlan_vni_in_use(vxlan->net, vxlan, &vxlan->cfg, v);
	if (err) {
		NL_SET_ERR_MSG(extack, "VNI in use");
		return err;
	}

	vninode = vxlan_vni_alloc(vxlan, v);
	if (!vninode)
		return -ENOMEM;

	err = rhashtable_lookup_insert_fast(&vg->vni_hash,
					    &vninode->vnode,
					    vxlan_vni_rht_params);
	if (err) {
		vxlan_vni_free(vninode);
		return err;
	}

	__vxlan_vni_add_list(vg, vninode);

	if (vxlan->dev->flags & IFF_UP)
		vxlan_vs_add_del_vninode(vxlan, vninode, false);

	err = vxlan_vni_update_group(vxlan, vninode, group, true, &changed,
				     extack);

	if (changed)
		vxlan_vnifilter_notify(vxlan, vninode, RTM_NEWTUNNEL);

	return err;
}

static void vxlan_vni_node_rcu_free(struct rcu_head *rcu)
{
	struct vxlan_vni_node *v;

	v = container_of(rcu, struct vxlan_vni_node, rcu);
	vxlan_vni_free(v);
}

static int vxlan_vni_del(struct vxlan_dev *vxlan,
			 struct vxlan_vni_group *vg,
			 u32 vni, struct netlink_ext_ack *extack)
{
	struct vxlan_vni_node *vninode;
	__be32 v = cpu_to_be32(vni);
	int err = 0;

	vg = rtnl_dereference(vxlan->vnigrp);

	vninode = rhashtable_lookup_fast(&vg->vni_hash, &v,
					 vxlan_vni_rht_params);
	if (!vninode) {
		err = -ENOENT;
		goto out;
	}

	vxlan_vni_delete_group(vxlan, vninode);

	err = rhashtable_remove_fast(&vg->vni_hash,
				     &vninode->vnode,
				     vxlan_vni_rht_params);
	if (err)
		goto out;

	__vxlan_vni_del_list(vg, vninode);

	vxlan_vnifilter_notify(vxlan, vninode, RTM_DELTUNNEL);

	if (vxlan->dev->flags & IFF_UP)
		vxlan_vs_add_del_vninode(vxlan, vninode, true);

	call_rcu(&vninode->rcu, vxlan_vni_node_rcu_free);

	return 0;
out:
	return err;
}

static int vxlan_vni_add_del(struct vxlan_dev *vxlan, __u32 start_vni,
			     __u32 end_vni, union vxlan_addr *group,
			     int cmd, struct netlink_ext_ack *extack)
{
	struct vxlan_vni_group *vg;
	int v, err = 0;

	vg = rtnl_dereference(vxlan->vnigrp);

	for (v = start_vni; v <= end_vni; v++) {
		switch (cmd) {
		case RTM_NEWTUNNEL:
			err = vxlan_vni_add(vxlan, vg, v, group, extack);
			break;
		case RTM_DELTUNNEL:
			err = vxlan_vni_del(vxlan, vg, v, extack);
			break;
		default:
			err = -EOPNOTSUPP;
			break;
		}
		if (err)
			goto out;
	}

	return 0;
out:
	return err;
}

static int vxlan_process_vni_filter(struct vxlan_dev *vxlan,
				    struct nlattr *nlvnifilter,
				    int cmd, struct netlink_ext_ack *extack)
{
	struct nlattr *vattrs[VXLAN_VNIFILTER_ENTRY_MAX + 1];
	u32 vni_start = 0, vni_end = 0;
	union vxlan_addr group;
	int err;

	err = nla_parse_nested(vattrs,
			       VXLAN_VNIFILTER_ENTRY_MAX,
			       nlvnifilter, vni_filter_entry_policy,
			       extack);
	if (err)
		return err;

	if (vattrs[VXLAN_VNIFILTER_ENTRY_START]) {
		vni_start = nla_get_u32(vattrs[VXLAN_VNIFILTER_ENTRY_START]);
		vni_end = vni_start;
	}

	if (vattrs[VXLAN_VNIFILTER_ENTRY_END])
		vni_end = nla_get_u32(vattrs[VXLAN_VNIFILTER_ENTRY_END]);

	if (!vni_start && !vni_end) {
		NL_SET_ERR_MSG_ATTR(extack, nlvnifilter,
				    "vni start nor end found in vni entry");
		return -EINVAL;
	}

	if (vattrs[VXLAN_VNIFILTER_ENTRY_GROUP]) {
		group.sin.sin_addr.s_addr =
			nla_get_in_addr(vattrs[VXLAN_VNIFILTER_ENTRY_GROUP]);
		group.sa.sa_family = AF_INET;
	} else if (vattrs[VXLAN_VNIFILTER_ENTRY_GROUP6]) {
		group.sin6.sin6_addr =
			nla_get_in6_addr(vattrs[VXLAN_VNIFILTER_ENTRY_GROUP6]);
		group.sa.sa_family = AF_INET6;
	} else {
		memset(&group, 0, sizeof(group));
	}

	if (vxlan_addr_multicast(&group) && !vxlan->default_dst.remote_ifindex) {
		NL_SET_ERR_MSG(extack,
			       "Local interface required for multicast remote group");

		return -EINVAL;
	}

	err = vxlan_vni_add_del(vxlan, vni_start, vni_end, &group, cmd,
				extack);
	if (err)
		return err;

	return 0;
}

void vxlan_vnigroup_uninit(struct vxlan_dev *vxlan)
{
	struct vxlan_vni_node *v, *tmp;
	struct vxlan_vni_group *vg;

	vg = rtnl_dereference(vxlan->vnigrp);
	list_for_each_entry_safe(v, tmp, &vg->vni_list, vlist) {
		rhashtable_remove_fast(&vg->vni_hash, &v->vnode,
				       vxlan_vni_rht_params);
		hlist_del_init_rcu(&v->hlist4.hlist);
#if IS_ENABLED(CONFIG_IPV6)
		hlist_del_init_rcu(&v->hlist6.hlist);
#endif
		__vxlan_vni_del_list(vg, v);
		vxlan_vnifilter_notify(vxlan, v, RTM_DELTUNNEL);
		call_rcu(&v->rcu, vxlan_vni_node_rcu_free);
	}
	rhashtable_destroy(&vg->vni_hash);
	kfree(vg);
}

int vxlan_vnigroup_init(struct vxlan_dev *vxlan)
{
	struct vxlan_vni_group *vg;
	int ret;

	vg = kzalloc(sizeof(*vg), GFP_KERNEL);
	if (!vg)
		return -ENOMEM;
	ret = rhashtable_init(&vg->vni_hash, &vxlan_vni_rht_params);
	if (ret) {
		kfree(vg);
		return ret;
	}
	INIT_LIST_HEAD(&vg->vni_list);
	rcu_assign_pointer(vxlan->vnigrp, vg);

	return 0;
}

static int vxlan_vnifilter_process(struct sk_buff *skb, struct nlmsghdr *nlh,
				   struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct tunnel_msg *tmsg;
	struct vxlan_dev *vxlan;
	struct net_device *dev;
	struct nlattr *attr;
	int err, vnis = 0;
	int rem;

	/* this should validate the header and check for remaining bytes */
	err = nlmsg_parse(nlh, sizeof(*tmsg), NULL, VXLAN_VNIFILTER_MAX,
			  vni_filter_policy, extack);
	if (err < 0)
		return err;

	tmsg = nlmsg_data(nlh);
	dev = __dev_get_by_index(net, tmsg->ifindex);
	if (!dev)
		return -ENODEV;

	if (!netif_is_vxlan(dev)) {
		NL_SET_ERR_MSG_MOD(extack, "The device is not a vxlan device");
		return -EINVAL;
	}

	vxlan = netdev_priv(dev);

	if (!(vxlan->cfg.flags & VXLAN_F_VNIFILTER))
		return -EOPNOTSUPP;

	nlmsg_for_each_attr(attr, nlh, sizeof(*tmsg), rem) {
		switch (nla_type(attr)) {
		case VXLAN_VNIFILTER_ENTRY:
			err = vxlan_process_vni_filter(vxlan, attr,
						       nlh->nlmsg_type, extack);
			break;
		default:
			continue;
		}
		vnis++;
		if (err)
			break;
	}

	if (!vnis) {
		NL_SET_ERR_MSG_MOD(extack, "No vnis found to process");
		err = -EINVAL;
	}

	return err;
}

static const struct rtnl_msg_handler vxlan_vnifilter_rtnl_msg_handlers[] = {
	{THIS_MODULE, PF_BRIDGE, RTM_GETTUNNEL, NULL, vxlan_vnifilter_dump, 0},
	{THIS_MODULE, PF_BRIDGE, RTM_NEWTUNNEL, vxlan_vnifilter_process, NULL, 0},
	{THIS_MODULE, PF_BRIDGE, RTM_DELTUNNEL, vxlan_vnifilter_process, NULL, 0},
};

int vxlan_vnifilter_init(void)
{
	return rtnl_register_many(vxlan_vnifilter_rtnl_msg_handlers);
}

void vxlan_vnifilter_uninit(void)
{
	rtnl_unregister_many(vxlan_vnifilter_rtnl_msg_handlers);
}
