// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2021 Marvell International Ltd. All rights reserved */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/inetdevice.h>
#include <net/inet_dscp.h>
#include <net/switchdev.h>
#include <linux/rhashtable.h>
#include <net/nexthop.h>
#include <net/netevent.h>

#include "prestera.h"
#include "prestera_router_hw.h"

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
	} lpm_info; /* hold prepared lpm info */
	/* Indicate if route is not overlapped by another table */
	struct rhash_head ht_node; /* node of prestera_router */
	union {
		struct fib_notifier_info info; /* point to any of 4/6 */
		struct fib_entry_notifier_info fen4_info;
	};
	bool reachable;
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

	return fib_cache;

err_ht_insert:
	fib_info_put(fen_info->fi);
	kfree(fib_cache);
err_kzalloc:
	return NULL;
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

static int
__prestera_pr_k_arb_fc_lpm_info_calc(struct prestera_switch *sw,
				     struct prestera_kern_fib_cache *fc)
{
	memset(&fc->lpm_info, 0, sizeof(fc->lpm_info));

	switch (prestera_kern_fib_info_type(&fc->info)) {
	case RTN_UNICAST:
		fc->lpm_info.fib_type = PRESTERA_FIB_TYPE_TRAP;
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
					    fc->lpm_info.fib_type, NULL);

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

	return 0;
}

static void __prestera_k_arb_abort_fib_ht_cb(void *ptr, void *arg)
{
	struct prestera_kern_fib_cache *fib_cache = ptr;
	struct prestera_switch *sw = arg;

	__prestera_k_arb_fib_lpm_offload_set(sw, fib_cache,
					     false, false,
					     false);
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
	struct neighbour *n = net_work->n;

	/* neigh - its not hw related object. It stored only in kernel. So... */
	rtnl_lock();

	/* TODO: handler */

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

	nhgrp_cache_bytes = sw->size_tbl_router_nexthop / 8 + 1;
	router->nhgrp_hw_state_cache = kzalloc(nhgrp_cache_bytes, GFP_KERNEL);
	if (!router->nhgrp_hw_state_cache) {
		err = -ENOMEM;
		goto err_nh_state_cache_alloc;
	}

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
	kfree(router->nhgrp_hw_state_cache);
err_nh_state_cache_alloc:
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
	prestera_queue_drain();

	prestera_k_arb_abort(sw);

	kfree(sw->router->nhgrp_hw_state_cache);
	rhashtable_destroy(&sw->router->kern_fib_cache_ht);
	prestera_router_hw_fini(sw);
	kfree(sw->router);
	sw->router = NULL;
}
