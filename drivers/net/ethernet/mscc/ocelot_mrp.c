// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch driver
 *
 * Copyright (c) 2017, 2019 Microsemi Corporation
 * Copyright 2020-2021 NXP
 */

#include <linux/if_bridge.h>
#include <linux/mrp_bridge.h>
#include <soc/mscc/ocelot_vcap.h>
#include <uapi/linux/mrp_bridge.h>
#include "ocelot.h"
#include "ocelot_vcap.h"

static const u8 mrp_test_dmac[] = { 0x01, 0x15, 0x4e, 0x00, 0x00, 0x01 };
static const u8 mrp_control_dmac[] = { 0x01, 0x15, 0x4e, 0x00, 0x00, 0x02 };

static int ocelot_mrp_find_partner_port(struct ocelot *ocelot,
					struct ocelot_port *p)
{
	int i;

	for (i = 0; i < ocelot->num_phys_ports; ++i) {
		struct ocelot_port *ocelot_port = ocelot->ports[i];

		if (!ocelot_port || p == ocelot_port)
			continue;

		if (ocelot_port->mrp_ring_id == p->mrp_ring_id)
			return i;
	}

	return -1;
}

static int ocelot_mrp_del_vcap(struct ocelot *ocelot, int id)
{
	struct ocelot_vcap_block *block_vcap_is2;
	struct ocelot_vcap_filter *filter;

	block_vcap_is2 = &ocelot->block[VCAP_IS2];
	filter = ocelot_vcap_block_find_filter_by_id(block_vcap_is2, id,
						     false);
	if (!filter)
		return 0;

	return ocelot_vcap_filter_del(ocelot, filter);
}

static int ocelot_mrp_redirect_add_vcap(struct ocelot *ocelot, int src_port,
					int dst_port)
{
	const u8 mrp_test_mask[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
	struct ocelot_vcap_filter *filter;
	int err;

	filter = kzalloc(sizeof(*filter), GFP_KERNEL);
	if (!filter)
		return -ENOMEM;

	filter->key_type = OCELOT_VCAP_KEY_ETYPE;
	filter->prio = 1;
	filter->id.cookie = OCELOT_VCAP_IS2_MRP_REDIRECT(ocelot, src_port);
	filter->id.tc_offload = false;
	filter->block_id = VCAP_IS2;
	filter->type = OCELOT_VCAP_FILTER_OFFLOAD;
	filter->ingress_port_mask = BIT(src_port);
	ether_addr_copy(filter->key.etype.dmac.value, mrp_test_dmac);
	ether_addr_copy(filter->key.etype.dmac.mask, mrp_test_mask);
	filter->action.mask_mode = OCELOT_MASK_MODE_REDIRECT;
	filter->action.port_mask = BIT(dst_port);

	err = ocelot_vcap_filter_add(ocelot, filter, NULL);
	if (err)
		kfree(filter);

	return err;
}

static void ocelot_populate_mrp_trap_key(struct ocelot_vcap_filter *filter)
{
	const u8 mrp_mask[] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };

	/* Here is possible to use control or test dmac because the mask
	 * doesn't cover the LSB
	 */
	ether_addr_copy(filter->key.etype.dmac.value, mrp_test_dmac);
	ether_addr_copy(filter->key.etype.dmac.mask, mrp_mask);
}

static int ocelot_mrp_trap_add(struct ocelot *ocelot, int port)
{
	unsigned long cookie = OCELOT_VCAP_IS2_MRP_TRAP(ocelot);

	return ocelot_trap_add(ocelot, port, cookie,
			       ocelot_populate_mrp_trap_key);
}

static int ocelot_mrp_trap_del(struct ocelot *ocelot, int port)
{
	unsigned long cookie = OCELOT_VCAP_IS2_MRP_TRAP(ocelot);

	return ocelot_trap_del(ocelot, port, cookie);
}

static void ocelot_mrp_save_mac(struct ocelot *ocelot,
				struct ocelot_port *port)
{
	ocelot_mact_learn(ocelot, PGID_BLACKHOLE, mrp_test_dmac,
			  OCELOT_VLAN_UNAWARE_PVID, ENTRYTYPE_LOCKED);
	ocelot_mact_learn(ocelot, PGID_BLACKHOLE, mrp_control_dmac,
			  OCELOT_VLAN_UNAWARE_PVID, ENTRYTYPE_LOCKED);
}

static void ocelot_mrp_del_mac(struct ocelot *ocelot,
			       struct ocelot_port *port)
{
	ocelot_mact_forget(ocelot, mrp_test_dmac, OCELOT_VLAN_UNAWARE_PVID);
	ocelot_mact_forget(ocelot, mrp_control_dmac, OCELOT_VLAN_UNAWARE_PVID);
}

int ocelot_mrp_add(struct ocelot *ocelot, int port,
		   const struct switchdev_obj_mrp *mrp)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_port_private *priv;
	struct net_device *dev;

	if (!ocelot_port)
		return -EOPNOTSUPP;

	priv = container_of(ocelot_port, struct ocelot_port_private, port);
	dev = priv->dev;

	if (mrp->p_port != dev && mrp->s_port != dev)
		return 0;

	ocelot_port->mrp_ring_id = mrp->ring_id;

	return 0;
}
EXPORT_SYMBOL(ocelot_mrp_add);

int ocelot_mrp_del(struct ocelot *ocelot, int port,
		   const struct switchdev_obj_mrp *mrp)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];

	if (!ocelot_port)
		return -EOPNOTSUPP;

	if (ocelot_port->mrp_ring_id != mrp->ring_id)
		return 0;

	ocelot_port->mrp_ring_id = 0;

	return 0;
}
EXPORT_SYMBOL(ocelot_mrp_del);

int ocelot_mrp_add_ring_role(struct ocelot *ocelot, int port,
			     const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int dst_port;
	int err;

	if (!ocelot_port)
		return -EOPNOTSUPP;

	if (mrp->ring_role != BR_MRP_RING_ROLE_MRC && !mrp->sw_backup)
		return -EOPNOTSUPP;

	if (ocelot_port->mrp_ring_id != mrp->ring_id)
		return 0;

	ocelot_mrp_save_mac(ocelot, ocelot_port);

	if (mrp->ring_role != BR_MRP_RING_ROLE_MRC)
		return ocelot_mrp_trap_add(ocelot, port);

	dst_port = ocelot_mrp_find_partner_port(ocelot, ocelot_port);
	if (dst_port == -1)
		return -EINVAL;

	err = ocelot_mrp_redirect_add_vcap(ocelot, port, dst_port);
	if (err)
		return err;

	err = ocelot_mrp_trap_add(ocelot, port);
	if (err) {
		ocelot_mrp_del_vcap(ocelot,
				    OCELOT_VCAP_IS2_MRP_REDIRECT(ocelot, port));
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(ocelot_mrp_add_ring_role);

int ocelot_mrp_del_ring_role(struct ocelot *ocelot, int port,
			     const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	int err, i;

	if (!ocelot_port)
		return -EOPNOTSUPP;

	if (mrp->ring_role != BR_MRP_RING_ROLE_MRC && !mrp->sw_backup)
		return -EOPNOTSUPP;

	if (ocelot_port->mrp_ring_id != mrp->ring_id)
		return 0;

	err = ocelot_mrp_trap_del(ocelot, port);
	if (err)
		return err;

	ocelot_mrp_del_vcap(ocelot, OCELOT_VCAP_IS2_MRP_REDIRECT(ocelot, port));

	for (i = 0; i < ocelot->num_phys_ports; ++i) {
		ocelot_port = ocelot->ports[i];

		if (!ocelot_port)
			continue;

		if (ocelot_port->mrp_ring_id != 0)
			goto out;
	}

	ocelot_mrp_del_mac(ocelot, ocelot->ports[port]);
out:
	return 0;
}
EXPORT_SYMBOL(ocelot_mrp_del_ring_role);
