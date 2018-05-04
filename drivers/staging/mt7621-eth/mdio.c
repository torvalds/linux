/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   Copyright (C) 2009-2016 John Crispin <blogic@openwrt.org>
 *   Copyright (C) 2009-2016 Felix Fietkau <nbd@openwrt.org>
 *   Copyright (C) 2013-2016 Michael Lee <igvtee@gmail.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/phy.h>
#include <linux/of_net.h>
#include <linux/of_mdio.h>

#include "mtk_eth_soc.h"
#include "mdio.h"

static int mtk_mdio_reset(struct mii_bus *bus)
{
	/* TODO */
	return 0;
}

static void mtk_phy_link_adjust(struct net_device *dev)
{
	struct mtk_eth *eth = netdev_priv(dev);
	unsigned long flags;
	int i;

	spin_lock_irqsave(&eth->phy->lock, flags);
	for (i = 0; i < 8; i++) {
		if (eth->phy->phy_node[i]) {
			struct phy_device *phydev = eth->phy->phy[i];
			int status_change = 0;

			if (phydev->link)
				if (eth->phy->duplex[i] != phydev->duplex ||
				    eth->phy->speed[i] != phydev->speed)
					status_change = 1;

			if (phydev->link != eth->link[i])
				status_change = 1;

			switch (phydev->speed) {
			case SPEED_1000:
			case SPEED_100:
			case SPEED_10:
				eth->link[i] = phydev->link;
				eth->phy->duplex[i] = phydev->duplex;
				eth->phy->speed[i] = phydev->speed;

				if (status_change &&
				    eth->soc->mdio_adjust_link)
					eth->soc->mdio_adjust_link(eth, i);
				break;
			}
		}
	}
	spin_unlock_irqrestore(&eth->phy->lock, flags);
}

int mtk_connect_phy_node(struct mtk_eth *eth, struct mtk_mac *mac,
			 struct device_node *phy_node)
{
	const __be32 *_port = NULL;
	struct phy_device *phydev;
	int phy_mode, port;

	_port = of_get_property(phy_node, "reg", NULL);

	if (!_port || (be32_to_cpu(*_port) >= 0x20)) {
		pr_err("%s: invalid port id\n", phy_node->name);
		return -EINVAL;
	}
	port = be32_to_cpu(*_port);
	phy_mode = of_get_phy_mode(phy_node);
	if (phy_mode < 0) {
		dev_err(eth->dev, "incorrect phy-mode %d\n", phy_mode);
		eth->phy->phy_node[port] = NULL;
		return -EINVAL;
	}

	phydev = of_phy_connect(eth->netdev[mac->id], phy_node,
				mtk_phy_link_adjust, 0, phy_mode);
	if (!phydev) {
		dev_err(eth->dev, "could not connect to PHY\n");
		eth->phy->phy_node[port] = NULL;
		return -ENODEV;
	}

	phydev->supported &= PHY_GBIT_FEATURES;
	phydev->advertising = phydev->supported;

	dev_info(eth->dev,
		 "connected port %d to PHY at %s [uid=%08x, driver=%s]\n",
		 port, phydev_name(phydev), phydev->phy_id,
		 phydev->drv->name);

	eth->phy->phy[port] = phydev;
	eth->link[port] = 0;

	return 0;
}

static void phy_init(struct mtk_eth *eth, struct mtk_mac *mac,
		     struct phy_device *phy)
{
	phy_attach(eth->netdev[mac->id], phydev_name(phy),
		   PHY_INTERFACE_MODE_MII);

	phy->autoneg = AUTONEG_ENABLE;
	phy->speed = 0;
	phy->duplex = 0;
	phy->supported &= PHY_BASIC_FEATURES;
	phy->advertising = phy->supported | ADVERTISED_Autoneg;

	phy_start_aneg(phy);
}

static int mtk_phy_connect(struct mtk_mac *mac)
{
	struct mtk_eth *eth = mac->hw;
	int i;

	for (i = 0; i < 8; i++) {
		if (eth->phy->phy_node[i]) {
			if (!mac->phy_dev) {
				mac->phy_dev = eth->phy->phy[i];
				mac->phy_flags = MTK_PHY_FLAG_PORT;
			}
		} else if (eth->mii_bus) {
			struct phy_device *phy;
			phy = mdiobus_get_phy(eth->mii_bus, i);
			if (phy) {
				phy_init(eth, mac, phy);
				if (!mac->phy_dev) {
					mac->phy_dev = phy;
					mac->phy_flags = MTK_PHY_FLAG_ATTACH;
				}
			}
		}
	}

	return 0;
}

static void mtk_phy_disconnect(struct mtk_mac *mac)
{
	struct mtk_eth *eth = mac->hw;
	unsigned long flags;
	int i;

	for (i = 0; i < 8; i++)
		if (eth->phy->phy_fixed[i]) {
			spin_lock_irqsave(&eth->phy->lock, flags);
			eth->link[i] = 0;
			if (eth->soc->mdio_adjust_link)
				eth->soc->mdio_adjust_link(eth, i);
			spin_unlock_irqrestore(&eth->phy->lock, flags);
		} else if (eth->phy->phy[i]) {
			phy_disconnect(eth->phy->phy[i]);
		} else if (eth->mii_bus) {
			struct phy_device *phy = mdiobus_get_phy(eth->mii_bus, i);
			if (phy)
				phy_detach(phy);
		}
}

static void mtk_phy_start(struct mtk_mac *mac)
{
	struct mtk_eth *eth = mac->hw;
	unsigned long flags;
	int i;

	for (i = 0; i < 8; i++) {
		if (eth->phy->phy_fixed[i]) {
			spin_lock_irqsave(&eth->phy->lock, flags);
			eth->link[i] = 1;
			if (eth->soc->mdio_adjust_link)
				eth->soc->mdio_adjust_link(eth, i);
			spin_unlock_irqrestore(&eth->phy->lock, flags);
		} else if (eth->phy->phy[i]) {
			phy_start(eth->phy->phy[i]);
		}
	}
}

static void mtk_phy_stop(struct mtk_mac *mac)
{
	struct mtk_eth *eth = mac->hw;
	unsigned long flags;
	int i;

	for (i = 0; i < 8; i++)
		if (eth->phy->phy_fixed[i]) {
			spin_lock_irqsave(&eth->phy->lock, flags);
			eth->link[i] = 0;
			if (eth->soc->mdio_adjust_link)
				eth->soc->mdio_adjust_link(eth, i);
			spin_unlock_irqrestore(&eth->phy->lock, flags);
		} else if (eth->phy->phy[i]) {
			phy_stop(eth->phy->phy[i]);
		}
}

static struct mtk_phy phy_ralink = {
	.connect = mtk_phy_connect,
	.disconnect = mtk_phy_disconnect,
	.start = mtk_phy_start,
	.stop = mtk_phy_stop,
};

int mtk_mdio_init(struct mtk_eth *eth)
{
	struct device_node *mii_np;
	int err;

	if (!eth->soc->mdio_read || !eth->soc->mdio_write)
		return 0;

	spin_lock_init(&phy_ralink.lock);
	eth->phy = &phy_ralink;

	mii_np = of_get_child_by_name(eth->dev->of_node, "mdio-bus");
	if (!mii_np) {
		dev_err(eth->dev, "no %s child node found", "mdio-bus");
		return -ENODEV;
	}

	if (!of_device_is_available(mii_np)) {
		err = 0;
		goto err_put_node;
	}

	eth->mii_bus = mdiobus_alloc();
	if (!eth->mii_bus) {
		err = -ENOMEM;
		goto err_put_node;
	}

	eth->mii_bus->name = "mdio";
	eth->mii_bus->read = eth->soc->mdio_read;
	eth->mii_bus->write = eth->soc->mdio_write;
	eth->mii_bus->reset = mtk_mdio_reset;
	eth->mii_bus->priv = eth;
	eth->mii_bus->parent = eth->dev;

	snprintf(eth->mii_bus->id, MII_BUS_ID_SIZE, "%s", mii_np->name);
	err = of_mdiobus_register(eth->mii_bus, mii_np);
	if (err)
		goto err_free_bus;

	return 0;

err_free_bus:
	kfree(eth->mii_bus);
err_put_node:
	of_node_put(mii_np);
	eth->mii_bus = NULL;
	return err;
}

void mtk_mdio_cleanup(struct mtk_eth *eth)
{
	if (!eth->mii_bus)
		return;

	mdiobus_unregister(eth->mii_bus);
	of_node_put(eth->mii_bus->dev.of_node);
	kfree(eth->mii_bus);
}
