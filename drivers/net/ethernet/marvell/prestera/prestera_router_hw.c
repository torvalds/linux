// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2021 Marvell International Ltd. All rights reserved */

#include <linux/rhashtable.h>

#include "prestera.h"
#include "prestera_hw.h"
#include "prestera_router_hw.h"
#include "prestera_acl.h"

/*                                Nexthop is pointed
 *                                to port (not rif)
 *                                +-------+
 *                              +>|nexthop|
 *                              | +-------+
 *                              |
 *            +--+        +-----++
 *   +------->|vr|<-+   +>|nh_grp|
 *   |        +--+  |   | +------+
 *   |              |   |
 * +-+-------+   +--+---+-+
 * |rif_entry|   |fib_node|
 * +---------+   +--------+
 *  Rif is        Fib - is exit point
 *  used as
 *  entry point
 *  for vr in hw
 */

#define PRESTERA_NHGR_UNUSED (0)
#define PRESTERA_NHGR_DROP (0xFFFFFFFF)
/* Need to merge it with router_manager */
#define PRESTERA_NH_ACTIVE_JIFFER_FILTER 3000 /* ms */

static const struct rhashtable_params __prestera_fib_ht_params = {
	.key_offset  = offsetof(struct prestera_fib_node, key),
	.head_offset = offsetof(struct prestera_fib_node, ht_node),
	.key_len     = sizeof(struct prestera_fib_key),
	.automatic_shrinking = true,
};

static const struct rhashtable_params __prestera_nh_neigh_ht_params = {
	.key_offset  = offsetof(struct prestera_nh_neigh, key),
	.key_len     = sizeof(struct prestera_nh_neigh_key),
	.head_offset = offsetof(struct prestera_nh_neigh, ht_node),
};

static const struct rhashtable_params __prestera_nexthop_group_ht_params = {
	.key_offset  = offsetof(struct prestera_nexthop_group, key),
	.key_len     = sizeof(struct prestera_nexthop_group_key),
	.head_offset = offsetof(struct prestera_nexthop_group, ht_node),
};

static int prestera_nexthop_group_set(struct prestera_switch *sw,
				      struct prestera_nexthop_group *nh_grp);
static bool
prestera_nexthop_group_util_hw_state(struct prestera_switch *sw,
				     struct prestera_nexthop_group *nh_grp);
static void prestera_fib_node_destroy_ht_cb(void *ptr, void *arg);

/* TODO: move to router.h as macros */
static bool prestera_nh_neigh_key_is_valid(struct prestera_nh_neigh_key *key)
{
	return memchr_inv(key, 0, sizeof(*key)) ? true : false;
}

int prestera_router_hw_init(struct prestera_switch *sw)
{
	int err;

	err = rhashtable_init(&sw->router->nh_neigh_ht,
			      &__prestera_nh_neigh_ht_params);
	if (err)
		goto err_nh_neigh_ht_init;

	err = rhashtable_init(&sw->router->nexthop_group_ht,
			      &__prestera_nexthop_group_ht_params);
	if (err)
		goto err_nexthop_grp_ht_init;

	err = rhashtable_init(&sw->router->fib_ht,
			      &__prestera_fib_ht_params);
	if (err)
		goto err_fib_ht_init;

	INIT_LIST_HEAD(&sw->router->vr_list);
	INIT_LIST_HEAD(&sw->router->rif_entry_list);

	return 0;

err_fib_ht_init:
	rhashtable_destroy(&sw->router->nexthop_group_ht);
err_nexthop_grp_ht_init:
	rhashtable_destroy(&sw->router->nh_neigh_ht);
err_nh_neigh_ht_init:
	return 0;
}

void prestera_router_hw_fini(struct prestera_switch *sw)
{
	rhashtable_free_and_destroy(&sw->router->fib_ht,
				    prestera_fib_node_destroy_ht_cb, sw);
	WARN_ON(!list_empty(&sw->router->vr_list));
	WARN_ON(!list_empty(&sw->router->rif_entry_list));
	rhashtable_destroy(&sw->router->fib_ht);
	rhashtable_destroy(&sw->router->nexthop_group_ht);
	rhashtable_destroy(&sw->router->nh_neigh_ht);
}

static struct prestera_vr *__prestera_vr_find(struct prestera_switch *sw,
					      u32 tb_id)
{
	struct prestera_vr *vr;

	list_for_each_entry(vr, &sw->router->vr_list, router_node) {
		if (vr->tb_id == tb_id)
			return vr;
	}

	return NULL;
}

static struct prestera_vr *__prestera_vr_create(struct prestera_switch *sw,
						u32 tb_id,
						struct netlink_ext_ack *extack)
{
	struct prestera_vr *vr;
	int err;

	vr = kzalloc(sizeof(*vr), GFP_KERNEL);
	if (!vr) {
		err = -ENOMEM;
		goto err_alloc_vr;
	}

	vr->tb_id = tb_id;

	err = prestera_hw_vr_create(sw, &vr->hw_vr_id);
	if (err)
		goto err_hw_create;

	list_add(&vr->router_node, &sw->router->vr_list);

	return vr;

err_hw_create:
	kfree(vr);
err_alloc_vr:
	return ERR_PTR(err);
}

static void __prestera_vr_destroy(struct prestera_switch *sw,
				  struct prestera_vr *vr)
{
	list_del(&vr->router_node);
	prestera_hw_vr_delete(sw, vr->hw_vr_id);
	kfree(vr);
}

static struct prestera_vr *prestera_vr_get(struct prestera_switch *sw, u32 tb_id,
					   struct netlink_ext_ack *extack)
{
	struct prestera_vr *vr;

	vr = __prestera_vr_find(sw, tb_id);
	if (vr) {
		refcount_inc(&vr->refcount);
	} else {
		vr = __prestera_vr_create(sw, tb_id, extack);
		if (IS_ERR(vr))
			return ERR_CAST(vr);

		refcount_set(&vr->refcount, 1);
	}

	return vr;
}

static void prestera_vr_put(struct prestera_switch *sw, struct prestera_vr *vr)
{
	if (refcount_dec_and_test(&vr->refcount))
		__prestera_vr_destroy(sw, vr);
}

/* iface is overhead struct. vr_id also can be removed. */
static int
__prestera_rif_entry_key_copy(const struct prestera_rif_entry_key *in,
			      struct prestera_rif_entry_key *out)
{
	memset(out, 0, sizeof(*out));

	switch (in->iface.type) {
	case PRESTERA_IF_PORT_E:
		out->iface.dev_port.hw_dev_num = in->iface.dev_port.hw_dev_num;
		out->iface.dev_port.port_num = in->iface.dev_port.port_num;
		break;
	case PRESTERA_IF_LAG_E:
		out->iface.lag_id = in->iface.lag_id;
		break;
	case PRESTERA_IF_VID_E:
		out->iface.vlan_id = in->iface.vlan_id;
		break;
	default:
		WARN(1, "Unsupported iface type");
		return -EINVAL;
	}

	out->iface.type = in->iface.type;
	return 0;
}

struct prestera_rif_entry *
prestera_rif_entry_find(const struct prestera_switch *sw,
			const struct prestera_rif_entry_key *k)
{
	struct prestera_rif_entry *rif_entry;
	struct prestera_rif_entry_key lk; /* lookup key */

	if (__prestera_rif_entry_key_copy(k, &lk))
		return NULL;

	list_for_each_entry(rif_entry, &sw->router->rif_entry_list,
			    router_node) {
		if (!memcmp(k, &rif_entry->key, sizeof(*k)))
			return rif_entry;
	}

	return NULL;
}

void prestera_rif_entry_destroy(struct prestera_switch *sw,
				struct prestera_rif_entry *e)
{
	struct prestera_iface iface;

	list_del(&e->router_node);

	memcpy(&iface, &e->key.iface, sizeof(iface));
	iface.vr_id = e->vr->hw_vr_id;
	prestera_hw_rif_delete(sw, e->hw_id, &iface);

	prestera_vr_put(sw, e->vr);
	kfree(e);
}

struct prestera_rif_entry *
prestera_rif_entry_create(struct prestera_switch *sw,
			  struct prestera_rif_entry_key *k,
			  u32 tb_id, const unsigned char *addr)
{
	int err;
	struct prestera_rif_entry *e;
	struct prestera_iface iface;

	e = kzalloc(sizeof(*e), GFP_KERNEL);
	if (!e)
		goto err_kzalloc;

	if (__prestera_rif_entry_key_copy(k, &e->key))
		goto err_key_copy;

	e->vr = prestera_vr_get(sw, tb_id, NULL);
	if (IS_ERR(e->vr))
		goto err_vr_get;

	memcpy(&e->addr, addr, sizeof(e->addr));

	/* HW */
	memcpy(&iface, &e->key.iface, sizeof(iface));
	iface.vr_id = e->vr->hw_vr_id;
	err = prestera_hw_rif_create(sw, &iface, e->addr, &e->hw_id);
	if (err)
		goto err_hw_create;

	list_add(&e->router_node, &sw->router->rif_entry_list);

	return e;

err_hw_create:
	prestera_vr_put(sw, e->vr);
err_vr_get:
err_key_copy:
	kfree(e);
err_kzalloc:
	return NULL;
}

static void __prestera_nh_neigh_destroy(struct prestera_switch *sw,
					struct prestera_nh_neigh *neigh)
{
	rhashtable_remove_fast(&sw->router->nh_neigh_ht,
			       &neigh->ht_node,
			       __prestera_nh_neigh_ht_params);
	kfree(neigh);
}

static struct prestera_nh_neigh *
__prestera_nh_neigh_create(struct prestera_switch *sw,
			   struct prestera_nh_neigh_key *key)
{
	struct prestera_nh_neigh *neigh;
	int err;

	neigh = kzalloc(sizeof(*neigh), GFP_KERNEL);
	if (!neigh)
		goto err_kzalloc;

	memcpy(&neigh->key, key, sizeof(*key));
	neigh->info.connected = false;
	INIT_LIST_HEAD(&neigh->nexthop_group_list);
	err = rhashtable_insert_fast(&sw->router->nh_neigh_ht,
				     &neigh->ht_node,
				     __prestera_nh_neigh_ht_params);
	if (err)
		goto err_rhashtable_insert;

	return neigh;

err_rhashtable_insert:
	kfree(neigh);
err_kzalloc:
	return NULL;
}

struct prestera_nh_neigh *
prestera_nh_neigh_find(struct prestera_switch *sw,
		       struct prestera_nh_neigh_key *key)
{
	struct prestera_nh_neigh *nh_neigh;

	nh_neigh = rhashtable_lookup_fast(&sw->router->nh_neigh_ht,
					  key, __prestera_nh_neigh_ht_params);
	return nh_neigh;
}

struct prestera_nh_neigh *
prestera_nh_neigh_get(struct prestera_switch *sw,
		      struct prestera_nh_neigh_key *key)
{
	struct prestera_nh_neigh *neigh;

	neigh = prestera_nh_neigh_find(sw, key);
	if (!neigh)
		return __prestera_nh_neigh_create(sw, key);

	return neigh;
}

void prestera_nh_neigh_put(struct prestera_switch *sw,
			   struct prestera_nh_neigh *neigh)
{
	if (list_empty(&neigh->nexthop_group_list))
		__prestera_nh_neigh_destroy(sw, neigh);
}

/* Updates new prestera_neigh_info */
int prestera_nh_neigh_set(struct prestera_switch *sw,
			  struct prestera_nh_neigh *neigh)
{
	struct prestera_nh_neigh_head *nh_head;
	struct prestera_nexthop_group *nh_grp;
	int err;

	list_for_each_entry(nh_head, &neigh->nexthop_group_list, head) {
		nh_grp = nh_head->this;
		err = prestera_nexthop_group_set(sw, nh_grp);
		if (err)
			return err;
	}

	return 0;
}

bool prestera_nh_neigh_util_hw_state(struct prestera_switch *sw,
				     struct prestera_nh_neigh *nh_neigh)
{
	bool state;
	struct prestera_nh_neigh_head *nh_head, *tmp;

	state = false;
	list_for_each_entry_safe(nh_head, tmp,
				 &nh_neigh->nexthop_group_list, head) {
		state = prestera_nexthop_group_util_hw_state(sw, nh_head->this);
		if (state)
			goto out;
	}

out:
	return state;
}

static struct prestera_nexthop_group *
__prestera_nexthop_group_create(struct prestera_switch *sw,
				struct prestera_nexthop_group_key *key)
{
	struct prestera_nexthop_group *nh_grp;
	struct prestera_nh_neigh *nh_neigh;
	int nh_cnt, err, gid;

	nh_grp = kzalloc(sizeof(*nh_grp), GFP_KERNEL);
	if (!nh_grp)
		goto err_kzalloc;

	memcpy(&nh_grp->key, key, sizeof(*key));
	for (nh_cnt = 0; nh_cnt < PRESTERA_NHGR_SIZE_MAX; nh_cnt++) {
		if (!prestera_nh_neigh_key_is_valid(&nh_grp->key.neigh[nh_cnt]))
			break;

		nh_neigh = prestera_nh_neigh_get(sw,
						 &nh_grp->key.neigh[nh_cnt]);
		if (!nh_neigh)
			goto err_nh_neigh_get;

		nh_grp->nh_neigh_head[nh_cnt].neigh = nh_neigh;
		nh_grp->nh_neigh_head[nh_cnt].this = nh_grp;
		list_add(&nh_grp->nh_neigh_head[nh_cnt].head,
			 &nh_neigh->nexthop_group_list);
	}

	err = prestera_hw_nh_group_create(sw, nh_cnt, &nh_grp->grp_id);
	if (err)
		goto err_nh_group_create;

	err = prestera_nexthop_group_set(sw, nh_grp);
	if (err)
		goto err_nexthop_group_set;

	err = rhashtable_insert_fast(&sw->router->nexthop_group_ht,
				     &nh_grp->ht_node,
				     __prestera_nexthop_group_ht_params);
	if (err)
		goto err_ht_insert;

	/* reset cache for created group */
	gid = nh_grp->grp_id;
	sw->router->nhgrp_hw_state_cache[gid / 8] &= ~BIT(gid % 8);

	return nh_grp;

err_ht_insert:
err_nexthop_group_set:
	prestera_hw_nh_group_delete(sw, nh_cnt, nh_grp->grp_id);
err_nh_group_create:
err_nh_neigh_get:
	for (nh_cnt--; nh_cnt >= 0; nh_cnt--) {
		list_del(&nh_grp->nh_neigh_head[nh_cnt].head);
		prestera_nh_neigh_put(sw, nh_grp->nh_neigh_head[nh_cnt].neigh);
	}

	kfree(nh_grp);
err_kzalloc:
	return NULL;
}

static void
__prestera_nexthop_group_destroy(struct prestera_switch *sw,
				 struct prestera_nexthop_group *nh_grp)
{
	struct prestera_nh_neigh *nh_neigh;
	int nh_cnt;

	rhashtable_remove_fast(&sw->router->nexthop_group_ht,
			       &nh_grp->ht_node,
			       __prestera_nexthop_group_ht_params);

	for (nh_cnt = 0; nh_cnt < PRESTERA_NHGR_SIZE_MAX; nh_cnt++) {
		nh_neigh = nh_grp->nh_neigh_head[nh_cnt].neigh;
		if (!nh_neigh)
			break;

		list_del(&nh_grp->nh_neigh_head[nh_cnt].head);
		prestera_nh_neigh_put(sw, nh_neigh);
	}

	prestera_hw_nh_group_delete(sw, nh_cnt, nh_grp->grp_id);
	kfree(nh_grp);
}

static struct prestera_nexthop_group *
__prestera_nexthop_group_find(struct prestera_switch *sw,
			      struct prestera_nexthop_group_key *key)
{
	struct prestera_nexthop_group *nh_grp;

	nh_grp = rhashtable_lookup_fast(&sw->router->nexthop_group_ht,
					key, __prestera_nexthop_group_ht_params);
	return nh_grp;
}

static struct prestera_nexthop_group *
prestera_nexthop_group_get(struct prestera_switch *sw,
			   struct prestera_nexthop_group_key *key)
{
	struct prestera_nexthop_group *nh_grp;

	nh_grp = __prestera_nexthop_group_find(sw, key);
	if (nh_grp) {
		refcount_inc(&nh_grp->refcount);
	} else {
		nh_grp = __prestera_nexthop_group_create(sw, key);
		if (!nh_grp)
			return ERR_PTR(-ENOMEM);

		refcount_set(&nh_grp->refcount, 1);
	}

	return nh_grp;
}

static void prestera_nexthop_group_put(struct prestera_switch *sw,
				       struct prestera_nexthop_group *nh_grp)
{
	if (refcount_dec_and_test(&nh_grp->refcount))
		__prestera_nexthop_group_destroy(sw, nh_grp);
}

/* Updates with new nh_neigh's info */
static int prestera_nexthop_group_set(struct prestera_switch *sw,
				      struct prestera_nexthop_group *nh_grp)
{
	struct prestera_neigh_info info[PRESTERA_NHGR_SIZE_MAX];
	struct prestera_nh_neigh *neigh;
	int nh_cnt;

	memset(&info[0], 0, sizeof(info));
	for (nh_cnt = 0; nh_cnt < PRESTERA_NHGR_SIZE_MAX; nh_cnt++) {
		neigh = nh_grp->nh_neigh_head[nh_cnt].neigh;
		if (!neigh)
			break;

		memcpy(&info[nh_cnt], &neigh->info, sizeof(neigh->info));
	}

	return prestera_hw_nh_entries_set(sw, nh_cnt, &info[0], nh_grp->grp_id);
}

static bool
prestera_nexthop_group_util_hw_state(struct prestera_switch *sw,
				     struct prestera_nexthop_group *nh_grp)
{
	int err;
	u32 buf_size = sw->size_tbl_router_nexthop / 8 + 1;
	u32 gid = nh_grp->grp_id;
	u8 *cache = sw->router->nhgrp_hw_state_cache;

	/* Antijitter
	 * Prevent situation, when we read state of nh_grp twice in short time,
	 * and state bit is still cleared on second call. So just stuck active
	 * state for PRESTERA_NH_ACTIVE_JIFFER_FILTER, after last occurred.
	 */
	if (!time_before(jiffies, sw->router->nhgrp_hw_cache_kick +
			msecs_to_jiffies(PRESTERA_NH_ACTIVE_JIFFER_FILTER))) {
		err = prestera_hw_nhgrp_blk_get(sw, cache, buf_size);
		if (err) {
			pr_err("Failed to get hw state nh_grp's");
			return false;
		}

		sw->router->nhgrp_hw_cache_kick = jiffies;
	}

	if (cache[gid / 8] & BIT(gid % 8))
		return true;

	return false;
}

struct prestera_fib_node *
prestera_fib_node_find(struct prestera_switch *sw, struct prestera_fib_key *key)
{
	struct prestera_fib_node *fib_node;

	fib_node = rhashtable_lookup_fast(&sw->router->fib_ht, key,
					  __prestera_fib_ht_params);
	return fib_node;
}

static void __prestera_fib_node_destruct(struct prestera_switch *sw,
					 struct prestera_fib_node *fib_node)
{
	struct prestera_vr *vr;

	vr = fib_node->info.vr;
	prestera_hw_lpm_del(sw, vr->hw_vr_id, fib_node->key.addr.u.ipv4,
			    fib_node->key.prefix_len);
	switch (fib_node->info.type) {
	case PRESTERA_FIB_TYPE_UC_NH:
		prestera_nexthop_group_put(sw, fib_node->info.nh_grp);
		break;
	case PRESTERA_FIB_TYPE_TRAP:
		break;
	case PRESTERA_FIB_TYPE_DROP:
		break;
	default:
	      pr_err("Unknown fib_node->info.type = %d",
		     fib_node->info.type);
	}

	prestera_vr_put(sw, vr);
}

void prestera_fib_node_destroy(struct prestera_switch *sw,
			       struct prestera_fib_node *fib_node)
{
	__prestera_fib_node_destruct(sw, fib_node);
	rhashtable_remove_fast(&sw->router->fib_ht, &fib_node->ht_node,
			       __prestera_fib_ht_params);
	kfree(fib_node);
}

static void prestera_fib_node_destroy_ht_cb(void *ptr, void *arg)
{
	struct prestera_fib_node *node = ptr;
	struct prestera_switch *sw = arg;

	__prestera_fib_node_destruct(sw, node);
	kfree(node);
}

struct prestera_fib_node *
prestera_fib_node_create(struct prestera_switch *sw,
			 struct prestera_fib_key *key,
			 enum prestera_fib_type fib_type,
			 struct prestera_nexthop_group_key *nh_grp_key)
{
	struct prestera_fib_node *fib_node;
	u32 grp_id;
	struct prestera_vr *vr;
	int err;

	fib_node = kzalloc(sizeof(*fib_node), GFP_KERNEL);
	if (!fib_node)
		goto err_kzalloc;

	memcpy(&fib_node->key, key, sizeof(*key));
	fib_node->info.type = fib_type;

	vr = prestera_vr_get(sw, key->tb_id, NULL);
	if (IS_ERR(vr))
		goto err_vr_get;

	fib_node->info.vr = vr;

	switch (fib_type) {
	case PRESTERA_FIB_TYPE_TRAP:
		grp_id = PRESTERA_NHGR_UNUSED;
		break;
	case PRESTERA_FIB_TYPE_DROP:
		grp_id = PRESTERA_NHGR_DROP;
		break;
	case PRESTERA_FIB_TYPE_UC_NH:
		fib_node->info.nh_grp = prestera_nexthop_group_get(sw,
								   nh_grp_key);
		if (IS_ERR(fib_node->info.nh_grp))
			goto err_nh_grp_get;

		grp_id = fib_node->info.nh_grp->grp_id;
		break;
	default:
		pr_err("Unsupported fib_type %d", fib_type);
		goto err_nh_grp_get;
	}

	err = prestera_hw_lpm_add(sw, vr->hw_vr_id, key->addr.u.ipv4,
				  key->prefix_len, grp_id);
	if (err)
		goto err_lpm_add;

	err = rhashtable_insert_fast(&sw->router->fib_ht, &fib_node->ht_node,
				     __prestera_fib_ht_params);
	if (err)
		goto err_ht_insert;

	return fib_node;

err_ht_insert:
	prestera_hw_lpm_del(sw, vr->hw_vr_id, key->addr.u.ipv4,
			    key->prefix_len);
err_lpm_add:
	if (fib_type == PRESTERA_FIB_TYPE_UC_NH)
		prestera_nexthop_group_put(sw, fib_node->info.nh_grp);
err_nh_grp_get:
	prestera_vr_put(sw, vr);
err_vr_get:
	kfree(fib_node);
err_kzalloc:
	return NULL;
}
