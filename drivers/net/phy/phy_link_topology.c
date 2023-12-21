// SPDX-License-Identifier: GPL-2.0+
/*
 * Infrastructure to handle all PHY devices connected to a given netdev,
 * either directly or indirectly attached.
 *
 * Copyright (c) 2023 Maxime Chevallier<maxime.chevallier@bootlin.com>
 */

#include <linux/phy_link_topology.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/rtnetlink.h>
#include <linux/xarray.h>

int phy_link_topo_add_phy(struct phy_link_topology *topo,
			  struct phy_device *phy,
			  enum phy_upstream upt, void *upstream)
{
	struct phy_device_node *pdn;
	int ret;

	pdn = kzalloc(sizeof(*pdn), GFP_KERNEL);
	if (!pdn)
		return -ENOMEM;

	pdn->phy = phy;
	switch (upt) {
	case PHY_UPSTREAM_MAC:
		pdn->upstream.netdev = (struct net_device *)upstream;
		if (phy_on_sfp(phy))
			pdn->parent_sfp_bus = pdn->upstream.netdev->sfp_bus;
		break;
	case PHY_UPSTREAM_PHY:
		pdn->upstream.phydev = (struct phy_device *)upstream;
		if (phy_on_sfp(phy))
			pdn->parent_sfp_bus = pdn->upstream.phydev->sfp_bus;
		break;
	default:
		ret = -EINVAL;
		goto err;
	}
	pdn->upstream_type = upt;

	ret = xa_alloc_cyclic(&topo->phys, &phy->phyindex, pdn, xa_limit_32b,
			      &topo->next_phy_index, GFP_KERNEL);
	if (ret)
		goto err;

	return 0;

err:
	kfree(pdn);
	return ret;
}
EXPORT_SYMBOL_GPL(phy_link_topo_add_phy);

void phy_link_topo_del_phy(struct phy_link_topology *topo,
			   struct phy_device *phy)
{
	struct phy_device_node *pdn = xa_erase(&topo->phys, phy->phyindex);

	phy->phyindex = 0;

	kfree(pdn);
}
EXPORT_SYMBOL_GPL(phy_link_topo_del_phy);
