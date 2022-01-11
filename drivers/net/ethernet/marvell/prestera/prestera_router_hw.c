// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2019-2021 Marvell International Ltd. All rights reserved */

#include <linux/rhashtable.h>

#include "prestera.h"
#include "prestera_hw.h"
#include "prestera_router_hw.h"
#include "prestera_acl.h"

/*            +--+
 *   +------->|vr|
 *   |        +--+
 *   |
 * +-+-------+
 * |rif_entry|
 * +---------+
 *  Rif is
 *  used as
 *  entry point
 *  for vr in hw
 */

int prestera_router_hw_init(struct prestera_switch *sw)
{
	INIT_LIST_HEAD(&sw->router->vr_list);
	INIT_LIST_HEAD(&sw->router->rif_entry_list);

	return 0;
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
	u16 hw_vr_id;
	int err;

	err = prestera_hw_vr_create(sw, &hw_vr_id);
	if (err)
		return ERR_PTR(-ENOMEM);

	vr = kzalloc(sizeof(*vr), GFP_KERNEL);
	if (!vr) {
		err = -ENOMEM;
		goto err_alloc_vr;
	}

	vr->tb_id = tb_id;
	vr->hw_vr_id = hw_vr_id;

	list_add(&vr->router_node, &sw->router->vr_list);

	return vr;

err_alloc_vr:
	prestera_hw_vr_delete(sw, hw_vr_id);
	kfree(vr);
	return ERR_PTR(err);
}

static void __prestera_vr_destroy(struct prestera_switch *sw,
				  struct prestera_vr *vr)
{
	prestera_hw_vr_delete(sw, vr->hw_vr_id);
	list_del(&vr->router_node);
	kfree(vr);
}

static struct prestera_vr *prestera_vr_get(struct prestera_switch *sw, u32 tb_id,
					   struct netlink_ext_ack *extack)
{
	struct prestera_vr *vr;

	vr = __prestera_vr_find(sw, tb_id);
	if (!vr)
		vr = __prestera_vr_create(sw, tb_id, extack);
	if (IS_ERR(vr))
		return ERR_CAST(vr);

	return vr;
}

static void prestera_vr_put(struct prestera_switch *sw, struct prestera_vr *vr)
{
	if (!vr->ref_cnt)
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
		pr_err("Unsupported iface type");
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

	e->vr->ref_cnt--;
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

	e->vr->ref_cnt++;
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
	e->vr->ref_cnt--;
	prestera_vr_put(sw, e->vr);
err_vr_get:
err_key_copy:
	kfree(e);
err_kzalloc:
	return NULL;
}
