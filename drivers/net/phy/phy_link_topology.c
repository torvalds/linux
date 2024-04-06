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

struct phy_link_topology *phy_link_topo_create(struct net_device *dev)
{
	struct phy_link_topology *topo;

	topo = kzalloc(sizeof(*topo), GFP_KERNEL);
	if (!topo)
		return ERR_PTR(-ENOMEM);

	xa_init_flags(&topo->phys, XA_FLAGS_ALLOC1);
	topo->next_phy_index = 1;

	return topo;
}

void phy_link_topo_destroy(struct phy_link_topology *topo)
{
	if (!topo)
		return;

	xa_destroy(&topo->phys);
	kfree(topo);
}

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

	/* Attempt to re-use a previously allocated phy_index */
	if (phy->phyindex) {
		ret = xa_insert(&topo->phys, phy->phyindex, pdn, GFP_KERNEL);

		/* Errors could be either -ENOMEM or -EBUSY. If the phy has an
		 * index, and there's another entry at the same index, this is
		 * unexpected and we still error-out
		 */
		if (ret)
			goto err;
		return 0;
	}

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

	/* We delete the PHY from the topology, however we don't re-set the
	 * phy->phyindex field. If the PHY isn't gone, we can re-assign it the
	 * same index next time it's added back to the topology
	 */

	kfree(pdn);
}
EXPORT_SYMBOL_GPL(phy_link_topo_del_phy);
