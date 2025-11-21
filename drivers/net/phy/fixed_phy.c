// SPDX-License-Identifier: GPL-2.0+
/*
 * Fixed MDIO bus (MDIO bus emulation with fixed PHYs)
 *
 * Author: Vitaly Bordug <vbordug@ru.mvista.com>
 *         Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * Copyright (c) 2006-2007 MontaVista Software, Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/idr.h>
#include <linux/netdevice.h>
#include <linux/linkmode.h>

#include "swphy.h"

struct fixed_phy {
	int addr;
	struct phy_device *phydev;
	struct fixed_phy_status status;
	int (*link_update)(struct net_device *, struct fixed_phy_status *);
	struct list_head node;
};

static struct mii_bus *fmb_mii_bus;
static LIST_HEAD(fmb_phys);

static struct fixed_phy *fixed_phy_find(int addr)
{
	struct fixed_phy *fp;

	list_for_each_entry(fp, &fmb_phys, node) {
		if (fp->addr == addr)
			return fp;
	}

	return NULL;
}

int fixed_phy_change_carrier(struct net_device *dev, bool new_carrier)
{
	struct phy_device *phydev = dev->phydev;
	struct fixed_phy *fp;

	if (!phydev || !phydev->mdio.bus)
		return -EINVAL;

	fp = fixed_phy_find(phydev->mdio.addr);
	if (!fp)
		return -EINVAL;

	fp->status.link = new_carrier;

	return 0;
}
EXPORT_SYMBOL_GPL(fixed_phy_change_carrier);

static int fixed_mdio_read(struct mii_bus *bus, int phy_addr, int reg_num)
{
	struct fixed_phy *fp;

	fp = fixed_phy_find(phy_addr);
	if (!fp)
		return 0xffff;

	if (fp->link_update)
		fp->link_update(fp->phydev->attached_dev, &fp->status);

	return swphy_read_reg(reg_num, &fp->status);
}

static int fixed_mdio_write(struct mii_bus *bus, int phy_addr, int reg_num,
			    u16 val)
{
	return 0;
}

/*
 * If something weird is required to be done with link/speed,
 * network driver is able to assign a function to implement this.
 * May be useful for PHY's that need to be software-driven.
 */
int fixed_phy_set_link_update(struct phy_device *phydev,
			      int (*link_update)(struct net_device *,
						 struct fixed_phy_status *))
{
	struct fixed_phy *fp;

	if (!phydev || !phydev->mdio.bus)
		return -EINVAL;

	fp = fixed_phy_find(phydev->mdio.addr);
	if (!fp)
		return -ENOENT;

	fp->link_update = link_update;
	fp->phydev = phydev;

	return 0;
}
EXPORT_SYMBOL_GPL(fixed_phy_set_link_update);

static int __fixed_phy_add(int phy_addr,
			   const struct fixed_phy_status *status)
{
	struct fixed_phy *fp;
	int ret;

	ret = swphy_validate_state(status);
	if (ret < 0)
		return ret;

	fp = kzalloc(sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	fp->addr = phy_addr;
	fp->status = *status;

	list_add_tail(&fp->node, &fmb_phys);

	return 0;
}

void fixed_phy_add(const struct fixed_phy_status *status)
{
	__fixed_phy_add(0, status);
}
EXPORT_SYMBOL_GPL(fixed_phy_add);

static DEFINE_IDA(phy_fixed_ida);

static void fixed_phy_del(int phy_addr)
{
	struct fixed_phy *fp;

	fp = fixed_phy_find(phy_addr);
	if (!fp)
		return;

	list_del(&fp->node);
	kfree(fp);
	ida_free(&phy_fixed_ida, phy_addr);
}

struct phy_device *fixed_phy_register(const struct fixed_phy_status *status,
				      struct device_node *np)
{
	struct phy_device *phy;
	int phy_addr;
	int ret;

	if (!fmb_mii_bus || fmb_mii_bus->state != MDIOBUS_REGISTERED)
		return ERR_PTR(-EPROBE_DEFER);

	/* Get the next available PHY address, up to PHY_MAX_ADDR */
	phy_addr = ida_alloc_max(&phy_fixed_ida, PHY_MAX_ADDR - 1, GFP_KERNEL);
	if (phy_addr < 0)
		return ERR_PTR(phy_addr);

	ret = __fixed_phy_add(phy_addr, status);
	if (ret < 0) {
		ida_free(&phy_fixed_ida, phy_addr);
		return ERR_PTR(ret);
	}

	phy = get_phy_device(fmb_mii_bus, phy_addr, false);
	if (IS_ERR(phy)) {
		fixed_phy_del(phy_addr);
		return ERR_PTR(-EINVAL);
	}

	/* propagate the fixed link values to struct phy_device */
	phy->link = status->link;
	if (status->link) {
		phy->speed = status->speed;
		phy->duplex = status->duplex;
		phy->pause = status->pause;
		phy->asym_pause = status->asym_pause;
	}

	of_node_get(np);
	phy->mdio.dev.of_node = np;
	phy->is_pseudo_fixed_link = true;

	switch (status->speed) {
	case SPEED_1000:
		linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
				 phy->supported);
		linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
				 phy->supported);
		fallthrough;
	case SPEED_100:
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT,
				 phy->supported);
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
				 phy->supported);
		fallthrough;
	case SPEED_10:
	default:
		linkmode_set_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
				 phy->supported);
		linkmode_set_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
				 phy->supported);
	}

	phy_advertise_supported(phy);

	ret = phy_device_register(phy);
	if (ret) {
		phy_device_free(phy);
		of_node_put(np);
		fixed_phy_del(phy_addr);
		return ERR_PTR(ret);
	}

	return phy;
}
EXPORT_SYMBOL_GPL(fixed_phy_register);

void fixed_phy_unregister(struct phy_device *phy)
{
	phy_device_remove(phy);
	of_node_put(phy->mdio.dev.of_node);
	fixed_phy_del(phy->mdio.addr);
	phy_device_free(phy);
}
EXPORT_SYMBOL_GPL(fixed_phy_unregister);

static int __init fixed_mdio_bus_init(void)
{
	int ret;

	fmb_mii_bus = mdiobus_alloc();
	if (!fmb_mii_bus)
		return -ENOMEM;

	snprintf(fmb_mii_bus->id, MII_BUS_ID_SIZE, "fixed-0");
	fmb_mii_bus->name = "Fixed MDIO Bus";
	fmb_mii_bus->read = &fixed_mdio_read;
	fmb_mii_bus->write = &fixed_mdio_write;
	fmb_mii_bus->phy_mask = ~0;

	ret = mdiobus_register(fmb_mii_bus);
	if (ret)
		goto err_mdiobus_alloc;

	return 0;

err_mdiobus_alloc:
	mdiobus_free(fmb_mii_bus);
	return ret;
}
module_init(fixed_mdio_bus_init);

static void __exit fixed_mdio_bus_exit(void)
{
	struct fixed_phy *fp, *tmp;

	mdiobus_unregister(fmb_mii_bus);
	mdiobus_free(fmb_mii_bus);

	list_for_each_entry_safe(fp, tmp, &fmb_phys, node) {
		list_del(&fp->node);
		kfree(fp);
	}
	ida_destroy(&phy_fixed_ida);
}
module_exit(fixed_mdio_bus_exit);

MODULE_DESCRIPTION("Fixed MDIO bus (MDIO bus emulation with fixed PHYs)");
MODULE_AUTHOR("Vitaly Bordug");
MODULE_LICENSE("GPL");
