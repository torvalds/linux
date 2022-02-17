// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2021 Marvell International Ltd. All rights reserved */

#include <linux/rhashtable.h>

#include "prestera.h"
#include "prestera_hw.h"
#include "prestera_router_hw.h"
#include "prestera_acl.h"

/*            +--+
 *   +------->|vr|<-+
 *   |        +--+  |
 *   |              |
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

static const struct rhashtable_params __prestera_fib_ht_params = {
	.key_offset  = offsetof(struct prestera_fib_node, key),
	.head_offset = offsetof(struct prestera_fib_node, ht_node),
	.key_len     = sizeof(struct prestera_fib_key),
	.automatic_shrinking = true,
};

int prestera_router_hw_init(struct prestera_switch *sw)
{
	int err;

	err = rhashtable_init(&sw->router->fib_ht,
			      &__prestera_fib_ht_params);
	if (err)
		goto err_fib_ht_init;

	INIT_LIST_HEAD(&sw->router->vr_list);
	INIT_LIST_HEAD(&sw->router->rif_entry_list);

err_fib_ht_init:
	return 0;
}

void prestera_router_hw_fini(struct prestera_switch *sw)
{
	WARN_ON(!list_empty(&sw->router->vr_list));
	WARN_ON(!list_empty(&sw->router->rif_entry_list));
	rhashtable_destroy(&sw->router->fib_ht);
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

struct prestera_fib_node *
prestera_fib_node_find(struct prestera_switch *sw, struct prestera_fib_key *key)
{
	struct prestera_fib_node *fib_node;

	fib_node = rhashtable_lookup_fast(&sw->router->fib_ht, key,
					  __prestera_fib_ht_params);
	return IS_ERR(fib_node) ? NULL : fib_node;
}

static void __prestera_fib_node_destruct(struct prestera_switch *sw,
					 struct prestera_fib_node *fib_node)
{
	struct prestera_vr *vr;

	vr = fib_node->info.vr;
	prestera_hw_lpm_del(sw, vr->hw_vr_id, fib_node->key.addr.u.ipv4,
			    fib_node->key.prefix_len);
	switch (fib_node->info.type) {
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

struct prestera_fib_node *
prestera_fib_node_create(struct prestera_switch *sw,
			 struct prestera_fib_key *key,
			 enum prestera_fib_type fib_type)
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
err_nh_grp_get:
	prestera_vr_put(sw, vr);
err_vr_get:
	kfree(fib_node);
err_kzalloc:
	return NULL;
}
