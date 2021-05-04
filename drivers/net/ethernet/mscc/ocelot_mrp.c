// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Microsemi Ocelot Switch driver
 *
 * This contains glue logic between the switchdev driver operations and the
 * mscc_ocelot_switch_lib.
 *
 * Copyright (c) 2017, 2019 Microsemi Corporation
 * Copyright 2020-2021 NXP Semiconductors
 */

#include <linux/if_bridge.h>
#include <linux/mrp_bridge.h>
#include <soc/mscc/ocelot_vcap.h>
#include <uapi/linux/mrp_bridge.h>
#include "ocelot.h"
#include "ocelot_vcap.h"

static int ocelot_mrp_del_vcap(struct ocelot *ocelot, int port)
{
	struct ocelot_vcap_block *block_vcap_is2;
	struct ocelot_vcap_filter *filter;

	block_vcap_is2 = &ocelot->block[VCAP_IS2];
	filter = ocelot_vcap_block_find_filter_by_id(block_vcap_is2, port,
						     false);
	if (!filter)
		return 0;

	return ocelot_vcap_filter_del(ocelot, filter);
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

	if (ocelot->mrp_ring_id != 0 &&
	    ocelot->mrp_s_port &&
	    ocelot->mrp_p_port)
		return -EINVAL;

	if (mrp->p_port == dev)
		ocelot->mrp_p_port = dev;

	if (mrp->s_port == dev)
		ocelot->mrp_s_port = dev;

	ocelot->mrp_ring_id = mrp->ring_id;

	return 0;
}
EXPORT_SYMBOL(ocelot_mrp_add);

int ocelot_mrp_del(struct ocelot *ocelot, int port,
		   const struct switchdev_obj_mrp *mrp)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_port_private *priv;
	struct net_device *dev;

	if (!ocelot_port)
		return -EOPNOTSUPP;

	priv = container_of(ocelot_port, struct ocelot_port_private, port);
	dev = priv->dev;

	if (ocelot->mrp_p_port != dev && ocelot->mrp_s_port != dev)
		return 0;

	if (ocelot->mrp_ring_id == 0 &&
	    !ocelot->mrp_s_port &&
	    !ocelot->mrp_p_port)
		return -EINVAL;

	if (ocelot_mrp_del_vcap(ocelot, priv->chip_port))
		return -EINVAL;

	if (ocelot->mrp_p_port == dev)
		ocelot->mrp_p_port = NULL;

	if (ocelot->mrp_s_port == dev)
		ocelot->mrp_s_port = NULL;

	ocelot->mrp_ring_id = 0;

	return 0;
}
EXPORT_SYMBOL(ocelot_mrp_del);

int ocelot_mrp_add_ring_role(struct ocelot *ocelot, int port,
			     const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_vcap_filter *filter;
	struct ocelot_port_private *priv;
	struct net_device *dev;
	int err;

	if (!ocelot_port)
		return -EOPNOTSUPP;

	priv = container_of(ocelot_port, struct ocelot_port_private, port);
	dev = priv->dev;

	if (ocelot->mrp_ring_id != mrp->ring_id)
		return -EINVAL;

	if (!mrp->sw_backup)
		return -EOPNOTSUPP;

	if (ocelot->mrp_p_port != dev && ocelot->mrp_s_port != dev)
		return 0;

	filter = kzalloc(sizeof(*filter), GFP_ATOMIC);
	if (!filter)
		return -ENOMEM;

	filter->key_type = OCELOT_VCAP_KEY_ETYPE;
	filter->prio = 1;
	filter->id.cookie = priv->chip_port;
	filter->id.tc_offload = false;
	filter->block_id = VCAP_IS2;
	filter->type = OCELOT_VCAP_FILTER_OFFLOAD;
	filter->ingress_port_mask = BIT(priv->chip_port);
	*(__be16 *)filter->key.etype.etype.value = htons(ETH_P_MRP);
	*(__be16 *)filter->key.etype.etype.mask = htons(0xffff);
	filter->action.mask_mode = OCELOT_MASK_MODE_PERMIT_DENY;
	filter->action.port_mask = 0x0;
	filter->action.cpu_copy_ena = true;
	filter->action.cpu_qu_num = OCELOT_MRP_CPUQ;

	err = ocelot_vcap_filter_add(ocelot, filter, NULL);
	if (err)
		kfree(filter);

	return err;
}
EXPORT_SYMBOL(ocelot_mrp_add_ring_role);

int ocelot_mrp_del_ring_role(struct ocelot *ocelot, int port,
			     const struct switchdev_obj_ring_role_mrp *mrp)
{
	struct ocelot_port *ocelot_port = ocelot->ports[port];
	struct ocelot_port_private *priv;
	struct net_device *dev;

	if (!ocelot_port)
		return -EOPNOTSUPP;

	priv = container_of(ocelot_port, struct ocelot_port_private, port);
	dev = priv->dev;

	if (ocelot->mrp_ring_id != mrp->ring_id)
		return -EINVAL;

	if (!mrp->sw_backup)
		return -EOPNOTSUPP;

	if (ocelot->mrp_p_port != dev && ocelot->mrp_s_port != dev)
		return 0;

	return ocelot_mrp_del_vcap(ocelot, priv->chip_port);
}
EXPORT_SYMBOL(ocelot_mrp_del_ring_role);
