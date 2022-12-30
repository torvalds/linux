// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2021 Marvell International Ltd. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/inetdevice.h>
#include <net/inet_dscp.h>
#include <net/switchdev.h>
#include <linux/rhashtable.h>
#include <net/nexthop.h>
#include <net/arp.h>
#include <linux/if_vlan.h>
#include <linux/if_macvlan.h>
#include <net/netevent.h>

#include "prestera.h"
#include "prestera_router_hw.h"

#define PRESTERA_IMPLICITY_RESOLVE_DEAD_NEIGH
#define PRESTERA_NH_PROBE_INTERVAL 5000 /* ms */

struct prestera_kern_neigh_cache_key {
	struct prestera_ip_addr addr;
	struct net_device *dev;
};

struct prestera_kern_neigh_cache {
	struct prestera_kern_neigh_cache_key key;
	struct rhash_head ht_node;
	struct list_head kern_fib_cache_list;
	/* Hold prepared nh_neigh info if is in_kernel */
	struct prestera_neigh_info nh_neigh_info;
	/* Indicate if neighbour is reachable by direct route */
	bool reachable;
	/* Lock cache if neigh is present in kernel */
	bool in_kernel;
};

struct prestera_kern_fib_cache_key {
	struct prestera_ip_addr addr;
	u32 prefix_len;
	u32 kern_tb_id; /* tb_id from kernel (not fixed) */
};

/* Subscribing on neighbours in kernel */
struct prestera_kern_fib_cache {
	struct prestera_kern_fib_cache_key key;
	struct {
		struct prestera_fib_key fib_key;
		enum prestera_fib_type fib_type;
		struct prestera_nexthop_group_key nh_grp_key;
	} lpm_info; /* hold prepared lpm info */
	/* Indicate if route is not overlapped by another table */
	struct rhash_head ht_node; /* node of prestera_router */
	struct prestera_kern_neigh_cache_head {
		struct prestera_kern_fib_cache *this;
		struct list_head head;
		struct prestera_kern_neigh_cache *n_cache;
	} kern_neigh_cache_head[PRESTERA_NHGR_SIZE_MAX];
	union {
		struct fib_notifier_info info; /* point to any of 4/6 */
		struct fib_entry_notifier_info fen4_info;
	};
	bool reachable;
};

static const struct rhashtable_params __prestera_kern_neigh_cache_ht_params = {
	.key_offset  = offsetof(struct prestera_kern_neigh_cache, key),
	.head_offset = offsetof(struct prestera_kern_neigh_cache, ht_node),
	.key_len     = sizeof(struct prestera_kern_neigh_cache_key),
	.automatic_shrinking = true,
};

static const struct rhashtable_params __prestera_kern_fib_cache_ht_params = {
	.key_offset  = offsetof(struct prestera_kern_fib_cache, key),
	.head_offset = offsetof(struct prestera_kern_fib_cache, ht_node),
	.key_len     = sizeof(struct prestera_kern_fib_cache_key),
	.automatic_shrinking = true,
};

/* This util to be used, to convert kernel rules for default vr in hw_vr */
static u32 prestera_fix_tb_id(u32 tb_id)
{
	if (tb_id == RT_TABLE_UNSPEC ||
	    tb_id == RT_TABLE_LOCAL ||
	    tb_id == RT_TABLE_DEFAULT)
		tb_id = RT_TABLE_MAIN;

	return tb_id;
}

static void
prestera_util_fen_info2fib_cache_key(struct fib_notifier_info *info,
				     struct prestera_kern_fib_cache_key *key)
{
	struct fib_entry_notifier_info *fen_info =
		container_of(info, struct fib_entry_notifier_info, info);

	memset(key, 0, sizeof(*key));
	key->addr.v = PRESTERA_IPV4;
	key->addr.u.ipv4 = cpu_to_be32(fen_info->dst);
	key->prefix_len = fen_info->dst_len;
	key->kern_tb_id = fen_info->tb_id;
}

static int prestera_util_nhc2nc_key(struct prestera_switch *sw,
				    struct fib_nh_common *nhc,
				    struct prestera_kern_neigh_cache_key *nk)
{
	memset(nk, 0, sizeof(*nk));
	if (nhc->nhc_gw_family == AF_INET) {
		nk->addr.v = PRESTERA_IPV4;
		nk->addr.u.ipv4 = nhc->nhc_gw.ipv4;
	} else {
		nk->addr.v = PRESTERA_IPV6;
		nk->addr.u.ipv6 = nhc->nhc_gw.ipv6;
	}

	nk->dev = nhc->nhc_dev;
	return 0;
}

static void
prestera_util_nc_key2nh_key(struct prestera_kern_neigh_cache_key *ck,
			    struct prestera_nh_neigh_key *nk)
{
	memset(nk, 0, sizeof(*nk));
	nk->addr = ck->addr;
	nk->rif = (void *)ck->dev;
}

static bool
prestera_util_nhc_eq_n_cache_key(struct prestera_switch *sw,
				 struct fib_nh_common *nhc,
				 struct prestera_kern_neigh_cache_key *nk)
{
	struct prestera_kern_neigh_cache_key tk;
	int err;

	err = prestera_util_nhc2nc_key(sw, nhc, &tk);
	if (err)
		return false;

	if (memcmp(&tk, nk, sizeof(tk)))
		return false;

	return true;
}

static int
prestera_util_neigh2nc_key(struct prestera_switch *sw, struct neighbour *n,
			   struct prestera_kern_neigh_cache_key *key)
{
	memset(key, 0, sizeof(*key));
	if (n->tbl->family == AF_INET) {
		key->addr.v = PRESTERA_IPV4;
		key->addr.u.ipv4 = *(__be32 *)n->primary_key;
	} else {
		return -ENOENT;
	}

	key->dev = n->dev;

	return 0;
}

static bool __prestera_fi_is_direct(struct fib_info *fi)
{
	struct fib_nh *fib_nh;

	if (fib_info_num_path(fi) == 1) {
		fib_nh = fib_info_nh(fi, 0);
		if (fib_nh->fib_nh_gw_family == AF_UNSPEC)
			return true;
	}

	return false;
}

static bool prestera_fi_is_direct(struct fib_info *fi)
{
	if (fi->fib_type != RTN_UNICAST)
		return false;

	return __prestera_fi_is_direct(fi);
}

static bool prestera_fi_is_nh(struct fib_info *fi)
{
	if (fi->fib_type != RTN_UNICAST)
		return false;

	return !__prestera_fi_is_direct(fi);
}

static bool __prestera_fi6_is_direct(struct fib6_info *fi)
{
	if (!fi->fib6_nh->nh_common.nhc_gw_family)
		return true;

	return false;
}

static bool prestera_fi6_is_direct(struct fib6_info *fi)
{
	if (fi->fib6_type != RTN_UNICAST)
		return false;

	return __prestera_fi6_is_direct(fi);
}

static bool prestera_fi6_is_nh(struct fib6_info *fi)
{
	if (fi->fib6_type != RTN_UNICAST)
		return false;

	return !__prestera_fi6_is_direct(fi);
}

static bool prestera_fib_info_is_direct(struct fib_notifier_info *info)
{
	struct fib6_entry_notifier_info *fen6_info =
		container_of(info, struct fib6_entry_notifier_info, info);
	struct fib_entry_notifier_info *fen_info =
		container_of(info, struct fib_entry_notifier_info, info);

	if (info->family == AF_INET)
		return prestera_fi_is_direct(fen_info->fi);
	else
		return prestera_fi6_is_direct(fen6_info->rt);
}

static bool prestera_fib_info_is_nh(struct fib_notifier_info *info)
{
	struct fib6_entry_notifier_info *fen6_info =
		container_of(info, struct fib6_entry_notifier_info, info);
	struct fib_entry_notifier_info *fen_info =
		container_of(info, struct fib_entry_notifier_info, info);

	if (info->family == AF_INET)
		return prestera_fi_is_nh(fen_info->fi);
	else
		return prestera_fi6_is_nh(fen6_info->rt);
}

/* must be called with rcu_read_lock() */
static int prestera_util_kern_get_route(struct fib_result *res, u32 tb_id,
					__be32 *addr)
{
	struct flowi4 fl4;

	/* TODO: walkthrough appropriate tables in kernel
	 * to know if the same prefix exists in several tables
	 */
	memset(&fl4, 0, sizeof(fl4));
	fl4.daddr = *addr;
	return fib_lookup(&init_net, &fl4, res, 0 /* FIB_LOOKUP_NOREF */);
}

static bool
__prestera_util_kern_n_is_reachable_v4(u32 tb_id, __be32 *addr,
				       struct net_device *dev)
{
	struct fib_nh *fib_nh;
	struct fib_result res;
	bool reachable;

	reachable = false;

	if (!prestera_util_kern_get_route(&res, tb_id, addr))
		if (prestera_fi_is_direct(res.fi)) {
			fib_nh = fib_info_nh(res.fi, 0);
			if (dev == fib_nh->fib_nh_dev)
				reachable = true;
		}

	return reachable;
}

/* Check if neigh route is reachable */
static bool
prestera_util_kern_n_is_reachable(u32 tb_id,
				  struct prestera_ip_addr *addr,
				  struct net_device *dev)
{
	if (addr->v == PRESTERA_IPV4)
		return __prestera_util_kern_n_is_reachable_v4(tb_id,
							      &addr->u.ipv4,
							      dev);
	else
		return false;
}

static void prestera_util_kern_set_neigh_offload(struct neighbour *n,
						 bool offloaded)
{
	if (offloaded)
		n->flags |= NTF_OFFLOADED;
	else
		n->flags &= ~NTF_OFFLOADED;
}

static void
prestera_util_kern_set_nh_offload(struct fib_nh_common *nhc, bool offloaded, bool trap)
{
		if (offloaded)
			nhc->nhc_flags |= RTNH_F_OFFLOAD;
		else
			nhc->nhc_flags &= ~RTNH_F_OFFLOAD;

		if (trap)
			nhc->nhc_flags |= RTNH_F_TRAP;
		else
			nhc->nhc_flags &= ~RTNH_F_TRAP;
}

static struct fib_nh_common *
prestera_kern_fib_info_nhc(struct fib_notifier_info *info, int n)
{
	struct fib6_entry_notifier_info *fen6_info;
	struct fib_entry_notifier_info *fen4_info;
	struct fib6_info *iter;

	if (info->family == AF_INET) {
		fen4_info = container_of(info, struct fib_entry_notifier_info,
					 info);
		return &fib_info_nh(fen4_info->fi, n)->nh_common;
	} else if (info->family == AF_INET6) {
		fen6_info = container_of(info, struct fib6_entry_notifier_info,
					 info);
		if (!n)
			return &fen6_info->rt->fib6_nh->nh_common;

		list_for_each_entry(iter, &fen6_info->rt->fib6_siblings,
				    fib6_siblings) {
			if (!--n)
				return &iter->fib6_nh->nh_common;
		}
	}

	/* if family is incorrect - than upper functions has BUG */
	/* if doesn't find requested index - there is alsi bug, because
	 * valid index must be produced by nhs, which checks list length
	 */
	WARN(1, "Invalid parameters passed to %s n=%d i=%p",
	     __func__, n, info);
	return NULL;
}

static int prestera_kern_fib_info_nhs(struct fib_notifier_info *info)
{
	struct fib6_entry_notifier_info *fen6_info;
	struct fib_entry_notifier_info *fen4_info;

	if (info->family == AF_INET) {
		fen4_info = container_of(info, struct fib_entry_notifier_info,
					 info);
		return fib_info_num_path(fen4_info->fi);
	} else if (info->family == AF_INET6) {
		fen6_info = container_of(info, struct fib6_entry_notifier_info,
					 info);
		return fen6_info->rt->fib6_nsiblings + 1;
	}

	return 0;
}

static unsigned char
prestera_kern_fib_info_type(struct fib_notifier_info *info)
{
	struct fib6_entry_notifier_info *fen6_info;
	struct fib_entry_notifier_info *fen4_info;

	if (info->family == AF_INET) {
		fen4_info = container_of(info, struct fib_entry_notifier_info,
					 info);
		return fen4_info->fi->fib_type;
	} else if (info->family == AF_INET6) {
		fen6_info = container_of(info, struct fib6_entry_notifier_info,
					 info);
		/* TODO: ECMP in ipv6 is several routes.
		 * Every route has single nh.
		 */
		return fen6_info->rt->fib6_type;
	}

	return RTN_UNSPEC;
}

/* Decided, that uc_nh route with key==nh is obviously neighbour route */
static bool
prestera_fib_node_util_is_neighbour(struct prestera_fib_node *fib_node)
{
	if (fib_node->info.type != PRESTERA_FIB_TYPE_UC_NH)
		return false;

	if (fib_node->info.nh_grp->nh_neigh_head[1].neigh)
		return false;

	if (!fib_node->info.nh_grp->nh_neigh_head[0].neigh)
		return false;

	if (memcmp(&fib_node->info.nh_grp->nh_neigh_head[0].neigh->key.addr,
		   &fib_node->key.addr, sizeof(struct prestera_ip_addr)))
		return false;

	return true;
}

static int prestera_dev_if_type(const struct net_device *dev)
{
	struct macvlan_dev *vlan;

	if (is_vlan_dev(dev) &&
	    netif_is_bridge_master(vlan_dev_real_dev(dev))) {
		return PRESTERA_IF_VID_E;
	} else if (netif_is_bridge_master(dev)) {
		return PRESTERA_IF_VID_E;
	} else if (netif_is_lag_master(dev)) {
		return PRESTERA_IF_LAG_E;
	} else if (netif_is_macvlan(dev)) {
		vlan = netdev_priv(dev);
		return prestera_dev_if_type(vlan->lowerdev);
	} else {
		return PRESTERA_IF_PORT_E;
	}
}

static int
prestera_neigh_iface_init(struct prestera_switch *sw,
			  struct prestera_iface *iface,
			  struct neighbour *n)
{
	struct prestera_port *port;

	iface->vlan_id = 0; /* TODO: vlan egress */
	iface->type = prestera_dev_if_type(n->dev);
	if (iface->type != PRESTERA_IF_PORT_E)
		return -EINVAL;

	if (!prestera_netdev_check(n->dev))
		return -EINVAL;

	port = netdev_priv(n->dev);
	iface->dev_port.hw_dev_num = port->dev_id;
	iface->dev_port.port_num = port->hw_id;

	return 0;
}

static struct prestera_kern_neigh_cache *
prestera_kern_neigh_cache_find(struct prestera_switch *sw,
			       struct prestera_kern_neigh_cache_key *key)
{
	struct prestera_kern_neigh_cache *n_cache;

	n_cache =
	 rhashtable_lookup_fast(&sw->router->kern_neigh_cache_ht, key,
				__prestera_kern_neigh_cache_ht_params);
	return n_cache;
}

static void
__prestera_kern_neigh_cache_destruct(struct prestera_switch *sw,
				     struct prestera_kern_neigh_cache *n_cache)
{
	dev_put(n_cache->key.dev);
}

static void
__prestera_kern_neigh_cache_destroy(struct prestera_switch *sw,
				    struct prestera_kern_neigh_cache *n_cache)
{
	rhashtable_remove_fast(&sw->router->kern_neigh_cache_ht,
			       &n_cache->ht_node,
			       __prestera_kern_neigh_cache_ht_params);
	__prestera_kern_neigh_cache_destruct(sw, n_cache);
	kfree(n_cache);
}

static struct prestera_kern_neigh_cache *
__prestera_kern_neigh_cache_create(struct prestera_switch *sw,
				   struct prestera_kern_neigh_cache_key *key)
{
	struct prestera_kern_neigh_cache *n_cache;
	int err;

	n_cache = kzalloc(sizeof(*n_cache), GFP_KERNEL);
	if (!n_cache)
		goto err_kzalloc;

	memcpy(&n_cache->key, key, sizeof(*key));
	dev_hold(n_cache->key.dev);

	INIT_LIST_HEAD(&n_cache->kern_fib_cache_list);
	err = rhashtable_insert_fast(&sw->router->kern_neigh_cache_ht,
				     &n_cache->ht_node,
				     __prestera_kern_neigh_cache_ht_params);
	if (err)
		goto err_ht_insert;

	return n_cache;

err_ht_insert:
	dev_put(n_cache->key.dev);
	kfree(n_cache);
err_kzalloc:
	return NULL;
}

static struct prestera_kern_neigh_cache *
prestera_kern_neigh_cache_get(struct prestera_switch *sw,
			      struct prestera_kern_neigh_cache_key *key)
{
	struct prestera_kern_neigh_cache *n_cache;

	n_cache = prestera_kern_neigh_cache_find(sw, key);
	if (!n_cache)
		n_cache = __prestera_kern_neigh_cache_create(sw, key);

	return n_cache;
}

static struct prestera_kern_neigh_cache *
prestera_kern_neigh_cache_put(struct prestera_switch *sw,
			      struct prestera_kern_neigh_cache *n_cache)
{
	if (!n_cache->in_kernel &&
	    list_empty(&n_cache->kern_fib_cache_list)) {
		__prestera_kern_neigh_cache_destroy(sw, n_cache);
		return NULL;
	}

	return n_cache;
}

static struct prestera_kern_fib_cache *
prestera_kern_fib_cache_find(struct prestera_switch *sw,
			     struct prestera_kern_fib_cache_key *key)
{
	struct prestera_kern_fib_cache *fib_cache;

	fib_cache =
	 rhashtable_lookup_fast(&sw->router->kern_fib_cache_ht, key,
				__prestera_kern_fib_cache_ht_params);
	return fib_cache;
}

static void
__prestera_kern_fib_cache_destruct(struct prestera_switch *sw,
				   struct prestera_kern_fib_cache *fib_cache)
{
	struct prestera_kern_neigh_cache *n_cache;
	int i;

	for (i = 0; i < PRESTERA_NHGR_SIZE_MAX; i++) {
		n_cache = fib_cache->kern_neigh_cache_head[i].n_cache;
		if (n_cache) {
			list_del(&fib_cache->kern_neigh_cache_head[i].head);
			prestera_kern_neigh_cache_put(sw, n_cache);
		}
	}

	fib_info_put(fib_cache->fen4_info.fi);
}

static void
prestera_kern_fib_cache_destroy(struct prestera_switch *sw,
				struct prestera_kern_fib_cache *fib_cache)
{
	rhashtable_remove_fast(&sw->router->kern_fib_cache_ht,
			       &fib_cache->ht_node,
			       __prestera_kern_fib_cache_ht_params);
	__prestera_kern_fib_cache_destruct(sw, fib_cache);
	kfree(fib_cache);
}

static int
__prestera_kern_fib_cache_create_nhs(struct prestera_switch *sw,
				     struct prestera_kern_fib_cache *fc)
{
	struct prestera_kern_neigh_cache_key nc_key;
	struct prestera_kern_neigh_cache *n_cache;
	struct fib_nh_common *nhc;
	int i, nhs, err;

	if (!prestera_fib_info_is_nh(&fc->info))
		return 0;

	nhs = prestera_kern_fib_info_nhs(&fc->info);
	if (nhs > PRESTERA_NHGR_SIZE_MAX)
		return 0;

	for (i = 0; i < nhs; i++) {
		nhc = prestera_kern_fib_info_nhc(&fc->fen4_info.info, i);
		err = prestera_util_nhc2nc_key(sw, nhc, &nc_key);
		if (err)
			return 0;

		n_cache = prestera_kern_neigh_cache_get(sw, &nc_key);
		if (!n_cache)
			return 0;

		fc->kern_neigh_cache_head[i].this = fc;
		fc->kern_neigh_cache_head[i].n_cache = n_cache;
		list_add(&fc->kern_neigh_cache_head[i].head,
			 &n_cache->kern_fib_cache_list);
	}

	return 0;
}

/* Operations on fi (offload, etc) must be wrapped in utils.
 * This function just create storage.
 */
static struct prestera_kern_fib_cache *
prestera_kern_fib_cache_create(struct prestera_switch *sw,
			       struct prestera_kern_fib_cache_key *key,
			       struct fib_notifier_info *info)
{
	struct fib_entry_notifier_info *fen_info =
		container_of(info, struct fib_entry_notifier_info, info);
	struct prestera_kern_fib_cache *fib_cache;
	int err;

	fib_cache = kzalloc(sizeof(*fib_cache), GFP_KERNEL);
	if (!fib_cache)
		goto err_kzalloc;

	memcpy(&fib_cache->key, key, sizeof(*key));
	fib_info_hold(fen_info->fi);
	memcpy(&fib_cache->fen4_info, fen_info, sizeof(*fen_info));

	err = rhashtable_insert_fast(&sw->router->kern_fib_cache_ht,
				     &fib_cache->ht_node,
				     __prestera_kern_fib_cache_ht_params);
	if (err)
		goto err_ht_insert;

	/* Handle nexthops */
	err = __prestera_kern_fib_cache_create_nhs(sw, fib_cache);
	if (err)
		goto out; /* Not critical */

out:
	return fib_cache;

err_ht_insert:
	fib_info_put(fen_info->fi);
	kfree(fib_cache);
err_kzalloc:
	return NULL;
}

static void
__prestera_k_arb_fib_nh_offload_set(struct prestera_switch *sw,
				    struct prestera_kern_fib_cache *fibc,
				    struct prestera_kern_neigh_cache *nc,
				    bool offloaded, bool trap)
{
	struct fib_nh_common *nhc;
	int i, nhs;

	nhs = prestera_kern_fib_info_nhs(&fibc->info);
	for (i = 0; i < nhs; i++) {
		nhc = prestera_kern_fib_info_nhc(&fibc->info, i);
		if (!nc) {
			prestera_util_kern_set_nh_offload(nhc, offloaded, trap);
			continue;
		}

		if (prestera_util_nhc_eq_n_cache_key(sw, nhc, &nc->key)) {
			prestera_util_kern_set_nh_offload(nhc, offloaded, trap);
			break;
		}
	}
}

static void
__prestera_k_arb_n_offload_set(struct prestera_switch *sw,
			       struct prestera_kern_neigh_cache *nc,
			       bool offloaded)
{
	struct neighbour *n;

	n = neigh_lookup(&arp_tbl, &nc->key.addr.u.ipv4,
			 nc->key.dev);
	if (!n)
		return;

	prestera_util_kern_set_neigh_offload(n, offloaded);
	neigh_release(n);
}

static void
__prestera_k_arb_fib_lpm_offload_set(struct prestera_switch *sw,
				     struct prestera_kern_fib_cache *fc,
				     bool fail, bool offload, bool trap)
{
	struct fib_rt_info fri;

	switch (fc->key.addr.v) {
	case PRESTERA_IPV4:
		fri.fi = fc->fen4_info.fi;
		fri.tb_id = fc->key.kern_tb_id;
		fri.dst = fc->key.addr.u.ipv4;
		fri.dst_len = fc->key.prefix_len;
		fri.dscp = fc->fen4_info.dscp;
		fri.type = fc->fen4_info.type;
		/* flags begin */
		fri.offload = offload;
		fri.trap = trap;
		fri.offload_failed = fail;
		/* flags end */
		fib_alias_hw_flags_set(&init_net, &fri);
		return;
	case PRESTERA_IPV6:
		/* TODO */
		return;
	}
}

static void
__prestera_k_arb_n_lpm_set(struct prestera_switch *sw,
			   struct prestera_kern_neigh_cache *n_cache,
			   bool enabled)
{
	struct prestera_nexthop_group_key nh_grp_key;
	struct prestera_kern_fib_cache_key fc_key;
	struct prestera_kern_fib_cache *fib_cache;
	struct prestera_fib_node *fib_node;
	struct prestera_fib_key fib_key;

	/* Exception for fc with prefix 32: LPM entry is already used by fib */
	memset(&fc_key, 0, sizeof(fc_key));
	fc_key.addr = n_cache->key.addr;
	fc_key.prefix_len = PRESTERA_IP_ADDR_PLEN(n_cache->key.addr.v);
	/* But better to use tb_id of route, which pointed to this neighbour. */
	/* We take it from rif, because rif inconsistent.
	 * Must be separated in_rif and out_rif.
	 * Also note: for each fib pointed to this neigh should be separated
	 *            neigh lpm entry (for each ingress vr)
	 */
	fc_key.kern_tb_id = l3mdev_fib_table(n_cache->key.dev);
	fib_cache = prestera_kern_fib_cache_find(sw, &fc_key);
	memset(&fib_key, 0, sizeof(fib_key));
	fib_key.addr = n_cache->key.addr;
	fib_key.prefix_len = PRESTERA_IP_ADDR_PLEN(n_cache->key.addr.v);
	fib_key.tb_id = prestera_fix_tb_id(fc_key.kern_tb_id);
	fib_node = prestera_fib_node_find(sw, &fib_key);
	if (!fib_cache || !fib_cache->reachable) {
		if (!enabled && fib_node) {
			if (prestera_fib_node_util_is_neighbour(fib_node))
				prestera_fib_node_destroy(sw, fib_node);
			return;
		}
	}

	if (enabled && !fib_node) {
		memset(&nh_grp_key, 0, sizeof(nh_grp_key));
		prestera_util_nc_key2nh_key(&n_cache->key,
					    &nh_grp_key.neigh[0]);
		fib_node = prestera_fib_node_create(sw, &fib_key,
						    PRESTERA_FIB_TYPE_UC_NH,
						    &nh_grp_key);
		if (!fib_node)
			pr_err("%s failed ip=%pI4n", "prestera_fib_node_create",
			       &fib_key.addr.u.ipv4);
		return;
	}
}

static void
__prestera_k_arb_nc_kern_fib_fetch(struct prestera_switch *sw,
				   struct prestera_kern_neigh_cache *nc)
{
	if (prestera_util_kern_n_is_reachable(l3mdev_fib_table(nc->key.dev),
					      &nc->key.addr, nc->key.dev))
		nc->reachable = true;
	else
		nc->reachable = false;
}

/* Kernel neighbour -> neigh_cache info */
static void
__prestera_k_arb_nc_kern_n_fetch(struct prestera_switch *sw,
				 struct prestera_kern_neigh_cache *nc)
{
	struct neighbour *n;
	int err;

	memset(&nc->nh_neigh_info, 0, sizeof(nc->nh_neigh_info));
	n = neigh_lookup(&arp_tbl, &nc->key.addr.u.ipv4, nc->key.dev);
	if (!n)
		goto out;

	read_lock_bh(&n->lock);
	if (n->nud_state & NUD_VALID && !n->dead) {
		err = prestera_neigh_iface_init(sw, &nc->nh_neigh_info.iface,
						n);
		if (err)
			goto n_read_out;

		memcpy(&nc->nh_neigh_info.ha[0], &n->ha[0], ETH_ALEN);
		nc->nh_neigh_info.connected = true;
	}
n_read_out:
	read_unlock_bh(&n->lock);
out:
	nc->in_kernel = nc->nh_neigh_info.connected;
	if (n)
		neigh_release(n);
}

/* neigh_cache info -> lpm update */
static void
__prestera_k_arb_nc_apply(struct prestera_switch *sw,
			  struct prestera_kern_neigh_cache *nc)
{
	struct prestera_kern_neigh_cache_head *nhead;
	struct prestera_nh_neigh_key nh_key;
	struct prestera_nh_neigh *nh_neigh;
	int err;

	__prestera_k_arb_n_lpm_set(sw, nc, nc->reachable && nc->in_kernel);
	__prestera_k_arb_n_offload_set(sw, nc, nc->reachable && nc->in_kernel);

	prestera_util_nc_key2nh_key(&nc->key, &nh_key);
	nh_neigh = prestera_nh_neigh_find(sw, &nh_key);
	if (!nh_neigh)
		goto out;

	/* Do hw update only if something changed to prevent nh flap */
	if (memcmp(&nc->nh_neigh_info, &nh_neigh->info,
		   sizeof(nh_neigh->info))) {
		memcpy(&nh_neigh->info, &nc->nh_neigh_info,
		       sizeof(nh_neigh->info));
		err = prestera_nh_neigh_set(sw, nh_neigh);
		if (err) {
			pr_err("%s failed with err=%d ip=%pI4n mac=%pM",
			       "prestera_nh_neigh_set", err,
			       &nh_neigh->key.addr.u.ipv4,
			       &nh_neigh->info.ha[0]);
			goto out;
		}
	}

out:
	list_for_each_entry(nhead, &nc->kern_fib_cache_list, head) {
		__prestera_k_arb_fib_nh_offload_set(sw, nhead->this, nc,
						    nc->in_kernel,
						    !nc->in_kernel);
	}
}

static int
__prestera_pr_k_arb_fc_lpm_info_calc(struct prestera_switch *sw,
				     struct prestera_kern_fib_cache *fc)
{
	struct fib_nh_common *nhc;
	int nh_cnt;

	memset(&fc->lpm_info, 0, sizeof(fc->lpm_info));

	switch (prestera_kern_fib_info_type(&fc->info)) {
	case RTN_UNICAST:
		if (prestera_fib_info_is_direct(&fc->info) &&
		    fc->key.prefix_len ==
			PRESTERA_IP_ADDR_PLEN(fc->key.addr.v)) {
			/* This is special case.
			 * When prefix is 32. Than we will have conflict in lpm
			 * for direct route - once TRAP added, there is no
			 * place for neighbour entry. So represent direct route
			 * with prefix 32, as NH. So neighbour will be resolved
			 * as nexthop of this route.
			 */
			nhc = prestera_kern_fib_info_nhc(&fc->info, 0);
			fc->lpm_info.fib_type = PRESTERA_FIB_TYPE_UC_NH;
			fc->lpm_info.nh_grp_key.neigh[0].addr =
				fc->key.addr;
			fc->lpm_info.nh_grp_key.neigh[0].rif =
				nhc->nhc_dev;

			break;
		}

		/* We can also get nh_grp_key from fi. This will be correct to
		 * because cache not always represent, what actually written to
		 * lpm. But we use nh cache, as well for now (for this case).
		 */
		for (nh_cnt = 0; nh_cnt < PRESTERA_NHGR_SIZE_MAX; nh_cnt++) {
			if (!fc->kern_neigh_cache_head[nh_cnt].n_cache)
				break;

			fc->lpm_info.nh_grp_key.neigh[nh_cnt].addr =
				fc->kern_neigh_cache_head[nh_cnt].n_cache->key.addr;
			fc->lpm_info.nh_grp_key.neigh[nh_cnt].rif =
				fc->kern_neigh_cache_head[nh_cnt].n_cache->key.dev;
		}

		fc->lpm_info.fib_type = nh_cnt ?
					PRESTERA_FIB_TYPE_UC_NH :
					PRESTERA_FIB_TYPE_TRAP;
		break;
	/* Unsupported. Leave it for kernel: */
	case RTN_BROADCAST:
	case RTN_MULTICAST:
	/* Routes we must trap by design: */
	case RTN_LOCAL:
	case RTN_UNREACHABLE:
	case RTN_PROHIBIT:
		fc->lpm_info.fib_type = PRESTERA_FIB_TYPE_TRAP;
		break;
	case RTN_BLACKHOLE:
		fc->lpm_info.fib_type = PRESTERA_FIB_TYPE_DROP;
		break;
	default:
		dev_err(sw->dev->dev, "Unsupported fib_type");
		return -EOPNOTSUPP;
	}

	fc->lpm_info.fib_key.addr = fc->key.addr;
	fc->lpm_info.fib_key.prefix_len = fc->key.prefix_len;
	fc->lpm_info.fib_key.tb_id = prestera_fix_tb_id(fc->key.kern_tb_id);

	return 0;
}

static int __prestera_k_arb_f_lpm_set(struct prestera_switch *sw,
				      struct prestera_kern_fib_cache *fc,
				      bool enabled)
{
	struct prestera_fib_node *fib_node;

	fib_node = prestera_fib_node_find(sw, &fc->lpm_info.fib_key);
	if (fib_node)
		prestera_fib_node_destroy(sw, fib_node);

	if (!enabled)
		return 0;

	fib_node = prestera_fib_node_create(sw, &fc->lpm_info.fib_key,
					    fc->lpm_info.fib_type,
					    &fc->lpm_info.nh_grp_key);

	if (!fib_node) {
		dev_err(sw->dev->dev, "fib_node=NULL %pI4n/%d kern_tb_id = %d",
			&fc->key.addr.u.ipv4, fc->key.prefix_len,
			fc->key.kern_tb_id);
		return -ENOENT;
	}

	return 0;
}

static int __prestera_k_arb_fc_apply(struct prestera_switch *sw,
				     struct prestera_kern_fib_cache *fc)
{
	int err;

	err = __prestera_pr_k_arb_fc_lpm_info_calc(sw, fc);
	if (err)
		return err;

	err = __prestera_k_arb_f_lpm_set(sw, fc, fc->reachable);
	if (err) {
		__prestera_k_arb_fib_lpm_offload_set(sw, fc,
						     true, false, false);
		return err;
	}

	switch (fc->lpm_info.fib_type) {
	case PRESTERA_FIB_TYPE_UC_NH:
		__prestera_k_arb_fib_lpm_offload_set(sw, fc, false,
						     fc->reachable, false);
		break;
	case PRESTERA_FIB_TYPE_TRAP:
		__prestera_k_arb_fib_lpm_offload_set(sw, fc, false,
						     false, fc->reachable);
		break;
	case PRESTERA_FIB_TYPE_DROP:
		__prestera_k_arb_fib_lpm_offload_set(sw, fc, false, true,
						     fc->reachable);
		break;
	case PRESTERA_FIB_TYPE_INVALID:
		break;
	}

	return 0;
}

static struct prestera_kern_fib_cache *
__prestera_k_arb_util_fib_overlaps(struct prestera_switch *sw,
				   struct prestera_kern_fib_cache *fc)
{
	struct prestera_kern_fib_cache_key fc_key;
	struct prestera_kern_fib_cache *rfc;

	/* TODO: parse kernel rules */
	rfc = NULL;
	if (fc->key.kern_tb_id == RT_TABLE_LOCAL) {
		memcpy(&fc_key, &fc->key, sizeof(fc_key));
		fc_key.kern_tb_id = RT_TABLE_MAIN;
		rfc = prestera_kern_fib_cache_find(sw, &fc_key);
	}

	return rfc;
}

static struct prestera_kern_fib_cache *
__prestera_k_arb_util_fib_overlapped(struct prestera_switch *sw,
				     struct prestera_kern_fib_cache *fc)
{
	struct prestera_kern_fib_cache_key fc_key;
	struct prestera_kern_fib_cache *rfc;

	/* TODO: parse kernel rules */
	rfc = NULL;
	if (fc->key.kern_tb_id == RT_TABLE_MAIN) {
		memcpy(&fc_key, &fc->key, sizeof(fc_key));
		fc_key.kern_tb_id = RT_TABLE_LOCAL;
		rfc = prestera_kern_fib_cache_find(sw, &fc_key);
	}

	return rfc;
}

static void __prestera_k_arb_hw_state_upd(struct prestera_switch *sw,
					  struct prestera_kern_neigh_cache *nc)
{
	struct prestera_nh_neigh_key nh_key;
	struct prestera_nh_neigh *nh_neigh;
	struct neighbour *n;
	bool hw_active;

	prestera_util_nc_key2nh_key(&nc->key, &nh_key);
	nh_neigh = prestera_nh_neigh_find(sw, &nh_key);
	if (!nh_neigh) {
		pr_err("Cannot find nh_neigh for cached %pI4n",
		       &nc->key.addr.u.ipv4);
		return;
	}

	hw_active = prestera_nh_neigh_util_hw_state(sw, nh_neigh);

#ifdef PRESTERA_IMPLICITY_RESOLVE_DEAD_NEIGH
	if (!hw_active && nc->in_kernel)
		goto out;
#else /* PRESTERA_IMPLICITY_RESOLVE_DEAD_NEIGH */
	if (!hw_active)
		goto out;
#endif /* PRESTERA_IMPLICITY_RESOLVE_DEAD_NEIGH */

	if (nc->key.addr.v == PRESTERA_IPV4) {
		n = neigh_lookup(&arp_tbl, &nc->key.addr.u.ipv4,
				 nc->key.dev);
		if (!n)
			n = neigh_create(&arp_tbl, &nc->key.addr.u.ipv4,
					 nc->key.dev);
	} else {
		n = NULL;
	}

	if (!IS_ERR(n) && n) {
		neigh_event_send(n, NULL);
		neigh_release(n);
	} else {
		pr_err("Cannot create neighbour %pI4n", &nc->key.addr.u.ipv4);
	}

out:
	return;
}

/* Propagate hw state to kernel */
static void prestera_k_arb_hw_evt(struct prestera_switch *sw)
{
	struct prestera_kern_neigh_cache *n_cache;
	struct rhashtable_iter iter;

	rhashtable_walk_enter(&sw->router->kern_neigh_cache_ht, &iter);
	rhashtable_walk_start(&iter);
	while (1) {
		n_cache = rhashtable_walk_next(&iter);

		if (!n_cache)
			break;

		if (IS_ERR(n_cache))
			continue;

		rhashtable_walk_stop(&iter);
		__prestera_k_arb_hw_state_upd(sw, n_cache);
		rhashtable_walk_start(&iter);
	}
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
}

/* Propagate kernel event to hw */
static void prestera_k_arb_n_evt(struct prestera_switch *sw,
				 struct neighbour *n)
{
	struct prestera_kern_neigh_cache_key n_key;
	struct prestera_kern_neigh_cache *n_cache;
	int err;

	err = prestera_util_neigh2nc_key(sw, n, &n_key);
	if (err)
		return;

	n_cache = prestera_kern_neigh_cache_find(sw, &n_key);
	if (!n_cache) {
		n_cache = prestera_kern_neigh_cache_get(sw, &n_key);
		if (!n_cache)
			return;
		__prestera_k_arb_nc_kern_fib_fetch(sw, n_cache);
	}

	__prestera_k_arb_nc_kern_n_fetch(sw, n_cache);
	__prestera_k_arb_nc_apply(sw, n_cache);

	prestera_kern_neigh_cache_put(sw, n_cache);
}

static void __prestera_k_arb_fib_evt2nc(struct prestera_switch *sw)
{
	struct prestera_kern_neigh_cache *n_cache;
	struct rhashtable_iter iter;

	rhashtable_walk_enter(&sw->router->kern_neigh_cache_ht, &iter);
	rhashtable_walk_start(&iter);
	while (1) {
		n_cache = rhashtable_walk_next(&iter);

		if (!n_cache)
			break;

		if (IS_ERR(n_cache))
			continue;

		rhashtable_walk_stop(&iter);
		__prestera_k_arb_nc_kern_fib_fetch(sw, n_cache);
		__prestera_k_arb_nc_apply(sw, n_cache);
		rhashtable_walk_start(&iter);
	}
	rhashtable_walk_stop(&iter);
	rhashtable_walk_exit(&iter);
}

static int
prestera_k_arb_fib_evt(struct prestera_switch *sw,
		       bool replace, /* replace or del */
		       struct fib_notifier_info *info)
{
	struct prestera_kern_fib_cache *tfib_cache, *bfib_cache; /* top/btm */
	struct prestera_kern_fib_cache_key fc_key;
	struct prestera_kern_fib_cache *fib_cache;
	int err;

	prestera_util_fen_info2fib_cache_key(info, &fc_key);
	fib_cache = prestera_kern_fib_cache_find(sw, &fc_key);
	if (fib_cache) {
		fib_cache->reachable = false;
		err = __prestera_k_arb_fc_apply(sw, fib_cache);
		if (err)
			dev_err(sw->dev->dev,
				"Applying destroyed fib_cache failed");

		bfib_cache = __prestera_k_arb_util_fib_overlaps(sw, fib_cache);
		tfib_cache = __prestera_k_arb_util_fib_overlapped(sw, fib_cache);
		if (!tfib_cache && bfib_cache) {
			bfib_cache->reachable = true;
			err = __prestera_k_arb_fc_apply(sw, bfib_cache);
			if (err)
				dev_err(sw->dev->dev,
					"Applying fib_cache btm failed");
		}

		prestera_kern_fib_cache_destroy(sw, fib_cache);
	}

	if (replace) {
		fib_cache = prestera_kern_fib_cache_create(sw, &fc_key, info);
		if (!fib_cache) {
			dev_err(sw->dev->dev, "fib_cache == NULL");
			return -ENOENT;
		}

		bfib_cache = __prestera_k_arb_util_fib_overlaps(sw, fib_cache);
		tfib_cache = __prestera_k_arb_util_fib_overlapped(sw, fib_cache);
		if (!tfib_cache)
			fib_cache->reachable = true;

		if (bfib_cache) {
			bfib_cache->reachable = false;
			err = __prestera_k_arb_fc_apply(sw, bfib_cache);
			if (err)
				dev_err(sw->dev->dev,
					"Applying fib_cache btm failed");
		}

		err = __prestera_k_arb_fc_apply(sw, fib_cache);
		if (err)
			dev_err(sw->dev->dev, "Applying fib_cache failed");
	}

	/* Update all neighs to resolve overlapped and apply related */
	__prestera_k_arb_fib_evt2nc(sw);

	return 0;
}

static void __prestera_k_arb_abort_neigh_ht_cb(void *ptr, void *arg)
{
	struct prestera_kern_neigh_cache *n_cache = ptr;
	struct prestera_switch *sw = arg;

	if (!list_empty(&n_cache->kern_fib_cache_list)) {
		WARN_ON(1); /* BUG */
		return;
	}
	__prestera_k_arb_n_offload_set(sw, n_cache, false);
	n_cache->in_kernel = false;
	/* No need to destroy lpm.
	 * It will be aborted by destroy_ht
	 */
	__prestera_kern_neigh_cache_destruct(sw, n_cache);
	kfree(n_cache);
}

static void __prestera_k_arb_abort_fib_ht_cb(void *ptr, void *arg)
{
	struct prestera_kern_fib_cache *fib_cache = ptr;
	struct prestera_switch *sw = arg;

	__prestera_k_arb_fib_lpm_offload_set(sw, fib_cache,
					     false, false,
					     false);
	__prestera_k_arb_fib_nh_offload_set(sw, fib_cache, NULL,
					    false, false);
	/* No need to destroy lpm.
	 * It will be aborted by destroy_ht
	 */
	__prestera_kern_fib_cache_destruct(sw, fib_cache);
	kfree(fib_cache);
}

static void prestera_k_arb_abort(struct prestera_switch *sw)
{
	/* Function to remove all arbiter entries and related hw objects. */
	/* Sequence:
	 *   1) Clear arbiter tables, but don't touch hw
	 *   2) Clear hw
	 * We use such approach, because arbiter object is not directly mapped
	 * to hw. So deletion of one arbiter object may even lead to creation of
	 * hw object (e.g. in case of overlapped routes).
	 */
	rhashtable_free_and_destroy(&sw->router->kern_fib_cache_ht,
				    __prestera_k_arb_abort_fib_ht_cb,
				    sw);
	rhashtable_free_and_destroy(&sw->router->kern_neigh_cache_ht,
				    __prestera_k_arb_abort_neigh_ht_cb,
				    sw);
}

static int __prestera_inetaddr_port_event(struct net_device *port_dev,
					  unsigned long event,
					  struct netlink_ext_ack *extack)
{
	struct prestera_port *port = netdev_priv(port_dev);
	struct prestera_rif_entry_key re_key = {};
	struct prestera_rif_entry *re;
	u32 kern_tb_id;
	int err;

	err = prestera_is_valid_mac_addr(port, port_dev->dev_addr);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "RIF MAC must have the same prefix");
		return err;
	}

	kern_tb_id = l3mdev_fib_table(port_dev);
	re_key.iface.type = PRESTERA_IF_PORT_E;
	re_key.iface.dev_port.hw_dev_num  = port->dev_id;
	re_key.iface.dev_port.port_num  = port->hw_id;
	re = prestera_rif_entry_find(port->sw, &re_key);

	switch (event) {
	case NETDEV_UP:
		if (re) {
			NL_SET_ERR_MSG_MOD(extack, "RIF already exist");
			return -EEXIST;
		}
		re = prestera_rif_entry_create(port->sw, &re_key,
					       prestera_fix_tb_id(kern_tb_id),
					       port_dev->dev_addr);
		if (!re) {
			NL_SET_ERR_MSG_MOD(extack, "Can't create RIF");
			return -EINVAL;
		}
		dev_hold(port_dev);
		break;
	case NETDEV_DOWN:
		if (!re) {
			NL_SET_ERR_MSG_MOD(extack, "Can't find RIF");
			return -EEXIST;
		}
		prestera_rif_entry_destroy(port->sw, re);
		dev_put(port_dev);
		break;
	}

	return 0;
}

static int __prestera_inetaddr_event(struct prestera_switch *sw,
				     struct net_device *dev,
				     unsigned long event,
				     struct netlink_ext_ack *extack)
{
	if (!prestera_netdev_check(dev) || netif_is_any_bridge_port(dev) ||
	    netif_is_lag_port(dev))
		return 0;

	return __prestera_inetaddr_port_event(dev, event, extack);
}

static int __prestera_inetaddr_cb(struct notifier_block *nb,
				  unsigned long event, void *ptr)
{
	struct in_ifaddr *ifa = (struct in_ifaddr *)ptr;
	struct net_device *dev = ifa->ifa_dev->dev;
	struct prestera_router *router = container_of(nb,
						      struct prestera_router,
						      inetaddr_nb);
	struct in_device *idev;
	int err = 0;

	if (event != NETDEV_DOWN)
		goto out;

	/* Ignore if this is not latest address */
	idev = __in_dev_get_rtnl(dev);
	if (idev && idev->ifa_list)
		goto out;

	err = __prestera_inetaddr_event(router->sw, dev, event, NULL);
out:
	return notifier_from_errno(err);
}

static int __prestera_inetaddr_valid_cb(struct notifier_block *nb,
					unsigned long event, void *ptr)
{
	struct in_validator_info *ivi = (struct in_validator_info *)ptr;
	struct net_device *dev = ivi->ivi_dev->dev;
	struct prestera_router *router = container_of(nb,
						      struct prestera_router,
						      inetaddr_valid_nb);
	struct in_device *idev;
	int err = 0;

	if (event != NETDEV_UP)
		goto out;

	/* Ignore if this is not first address */
	idev = __in_dev_get_rtnl(dev);
	if (idev && idev->ifa_list)
		goto out;

	if (ipv4_is_multicast(ivi->ivi_addr)) {
		NL_SET_ERR_MSG_MOD(ivi->extack,
				   "Multicast addr on RIF is not supported");
		err = -EINVAL;
		goto out;
	}

	err = __prestera_inetaddr_event(router->sw, dev, event, ivi->extack);
out:
	return notifier_from_errno(err);
}

struct prestera_fib_event_work {
	struct work_struct work;
	struct prestera_switch *sw;
	struct fib_entry_notifier_info fen_info;
	unsigned long event;
};

static void __prestera_router_fib_event_work(struct work_struct *work)
{
	struct prestera_fib_event_work *fib_work =
			container_of(work, struct prestera_fib_event_work, work);
	struct prestera_switch *sw = fib_work->sw;
	int err;

	rtnl_lock();

	switch (fib_work->event) {
	case FIB_EVENT_ENTRY_REPLACE:
		err = prestera_k_arb_fib_evt(sw, true,
					     &fib_work->fen_info.info);
		if (err)
			goto err_out;

		break;
	case FIB_EVENT_ENTRY_DEL:
		err = prestera_k_arb_fib_evt(sw, false,
					     &fib_work->fen_info.info);
		if (err)
			goto err_out;

		break;
	}

	goto out;

err_out:
	dev_err(sw->dev->dev, "Error when processing %pI4h/%d",
		&fib_work->fen_info.dst,
		fib_work->fen_info.dst_len);
out:
	fib_info_put(fib_work->fen_info.fi);
	rtnl_unlock();
	kfree(fib_work);
}

/* Called with rcu_read_lock() */
static int __prestera_router_fib_event(struct notifier_block *nb,
				       unsigned long event, void *ptr)
{
	struct prestera_fib_event_work *fib_work;
	struct fib_entry_notifier_info *fen_info;
	struct fib_notifier_info *info = ptr;
	struct prestera_router *router;

	if (info->family != AF_INET)
		return NOTIFY_DONE;

	router = container_of(nb, struct prestera_router, fib_nb);

	switch (event) {
	case FIB_EVENT_ENTRY_REPLACE:
	case FIB_EVENT_ENTRY_DEL:
		fen_info = container_of(info, struct fib_entry_notifier_info,
					info);
		if (!fen_info->fi)
			return NOTIFY_DONE;

		fib_work = kzalloc(sizeof(*fib_work), GFP_ATOMIC);
		if (WARN_ON(!fib_work))
			return NOTIFY_BAD;

		fib_info_hold(fen_info->fi);
		fib_work->fen_info = *fen_info;
		fib_work->event = event;
		fib_work->sw = router->sw;
		INIT_WORK(&fib_work->work, __prestera_router_fib_event_work);
		prestera_queue_work(&fib_work->work);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_DONE;
}

struct prestera_netevent_work {
	struct work_struct work;
	struct prestera_switch *sw;
	struct neighbour *n;
};

static void prestera_router_neigh_event_work(struct work_struct *work)
{
	struct prestera_netevent_work *net_work =
		container_of(work, struct prestera_netevent_work, work);
	struct prestera_switch *sw = net_work->sw;
	struct neighbour *n = net_work->n;

	/* neigh - its not hw related object. It stored only in kernel. So... */
	rtnl_lock();

	prestera_k_arb_n_evt(sw, n);

	neigh_release(n);
	rtnl_unlock();
	kfree(net_work);
}

static int prestera_router_netevent_event(struct notifier_block *nb,
					  unsigned long event, void *ptr)
{
	struct prestera_netevent_work *net_work;
	struct prestera_router *router;
	struct neighbour *n = ptr;

	router = container_of(nb, struct prestera_router, netevent_nb);

	switch (event) {
	case NETEVENT_NEIGH_UPDATE:
		if (n->tbl->family != AF_INET)
			return NOTIFY_DONE;

		net_work = kzalloc(sizeof(*net_work), GFP_ATOMIC);
		if (WARN_ON(!net_work))
			return NOTIFY_BAD;

		neigh_clone(n);
		net_work->n = n;
		net_work->sw = router->sw;
		INIT_WORK(&net_work->work, prestera_router_neigh_event_work);
		prestera_queue_work(&net_work->work);
	}

	return NOTIFY_DONE;
}

static void prestera_router_update_neighs_work(struct work_struct *work)
{
	struct prestera_router *router;

	router = container_of(work, struct prestera_router,
			      neighs_update.dw.work);
	rtnl_lock();

	prestera_k_arb_hw_evt(router->sw);

	rtnl_unlock();
	prestera_queue_delayed_work(&router->neighs_update.dw,
				    msecs_to_jiffies(PRESTERA_NH_PROBE_INTERVAL));
}

static int prestera_neigh_work_init(struct prestera_switch *sw)
{
	INIT_DELAYED_WORK(&sw->router->neighs_update.dw,
			  prestera_router_update_neighs_work);
	prestera_queue_delayed_work(&sw->router->neighs_update.dw, 0);
	return 0;
}

static void prestera_neigh_work_fini(struct prestera_switch *sw)
{
	cancel_delayed_work_sync(&sw->router->neighs_update.dw);
}

int prestera_router_init(struct prestera_switch *sw)
{
	struct prestera_router *router;
	int err, nhgrp_cache_bytes;

	router = kzalloc(sizeof(*sw->router), GFP_KERNEL);
	if (!router)
		return -ENOMEM;

	sw->router = router;
	router->sw = sw;

	err = prestera_router_hw_init(sw);
	if (err)
		goto err_router_lib_init;

	err = rhashtable_init(&router->kern_fib_cache_ht,
			      &__prestera_kern_fib_cache_ht_params);
	if (err)
		goto err_kern_fib_cache_ht_init;

	err = rhashtable_init(&router->kern_neigh_cache_ht,
			      &__prestera_kern_neigh_cache_ht_params);
	if (err)
		goto err_kern_neigh_cache_ht_init;

	nhgrp_cache_bytes = sw->size_tbl_router_nexthop / 8 + 1;
	router->nhgrp_hw_state_cache = kzalloc(nhgrp_cache_bytes, GFP_KERNEL);
	if (!router->nhgrp_hw_state_cache) {
		err = -ENOMEM;
		goto err_nh_state_cache_alloc;
	}

	err = prestera_neigh_work_init(sw);
	if (err)
		goto err_neigh_work_init;

	router->inetaddr_valid_nb.notifier_call = __prestera_inetaddr_valid_cb;
	err = register_inetaddr_validator_notifier(&router->inetaddr_valid_nb);
	if (err)
		goto err_register_inetaddr_validator_notifier;

	router->inetaddr_nb.notifier_call = __prestera_inetaddr_cb;
	err = register_inetaddr_notifier(&router->inetaddr_nb);
	if (err)
		goto err_register_inetaddr_notifier;

	router->netevent_nb.notifier_call = prestera_router_netevent_event;
	err = register_netevent_notifier(&router->netevent_nb);
	if (err)
		goto err_register_netevent_notifier;

	router->fib_nb.notifier_call = __prestera_router_fib_event;
	err = register_fib_notifier(&init_net, &router->fib_nb,
				    /* TODO: flush fib entries */ NULL, NULL);
	if (err)
		goto err_register_fib_notifier;

	return 0;

err_register_fib_notifier:
	unregister_netevent_notifier(&router->netevent_nb);
err_register_netevent_notifier:
	unregister_inetaddr_notifier(&router->inetaddr_nb);
err_register_inetaddr_notifier:
	unregister_inetaddr_validator_notifier(&router->inetaddr_valid_nb);
err_register_inetaddr_validator_notifier:
	prestera_neigh_work_fini(sw);
err_neigh_work_init:
	kfree(router->nhgrp_hw_state_cache);
err_nh_state_cache_alloc:
	rhashtable_destroy(&router->kern_neigh_cache_ht);
err_kern_neigh_cache_ht_init:
	rhashtable_destroy(&router->kern_fib_cache_ht);
err_kern_fib_cache_ht_init:
	prestera_router_hw_fini(sw);
err_router_lib_init:
	kfree(sw->router);
	return err;
}

void prestera_router_fini(struct prestera_switch *sw)
{
	unregister_fib_notifier(&init_net, &sw->router->fib_nb);
	unregister_netevent_notifier(&sw->router->netevent_nb);
	unregister_inetaddr_notifier(&sw->router->inetaddr_nb);
	unregister_inetaddr_validator_notifier(&sw->router->inetaddr_valid_nb);
	prestera_neigh_work_fini(sw);
	prestera_queue_drain();

	prestera_k_arb_abort(sw);

	kfree(sw->router->nhgrp_hw_state_cache);
	rhashtable_destroy(&sw->router->kern_fib_cache_ht);
	prestera_router_hw_fini(sw);
	kfree(sw->router);
	sw->router = NULL;
}
