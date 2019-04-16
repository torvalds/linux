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
#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/seqlock.h>
#include <linux/idr.h>
#include <linux/netdevice.h>
#include <linux/linkmode.h>

#include "swphy.h"

struct fixed_mdio_bus {
	struct mii_bus *mii_bus;
	struct list_head phys;
};

struct fixed_phy {
	int addr;
	struct phy_device *phydev;
	seqcount_t seqcount;
	struct fixed_phy_status status;
	bool no_carrier;
	int (*link_update)(struct net_device *, struct fixed_phy_status *);
	struct list_head node;
	struct gpio_desc *link_gpiod;
};

static struct platform_device *pdev;
static struct fixed_mdio_bus platform_fmb = {
	.phys = LIST_HEAD_INIT(platform_fmb.phys),
};

int fixed_phy_change_carrier(struct net_device *dev, bool new_carrier)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct phy_device *phydev = dev->phydev;
	struct fixed_phy *fp;

	if (!phydev || !phydev->mdio.bus)
		return -EINVAL;

	list_for_each_entry(fp, &fmb->phys, node) {
		if (fp->addr == phydev->mdio.addr) {
			fp->no_carrier = !new_carrier;
			return 0;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(fixed_phy_change_carrier);

static void fixed_phy_update(struct fixed_phy *fp)
{
	if (!fp->no_carrier && fp->link_gpiod)
		fp->status.link = !!gpiod_get_value_cansleep(fp->link_gpiod);
}

static int fixed_mdio_read(struct mii_bus *bus, int phy_addr, int reg_num)
{
	struct fixed_mdio_bus *fmb = bus->priv;
	struct fixed_phy *fp;

	list_for_each_entry(fp, &fmb->phys, node) {
		if (fp->addr == phy_addr) {
			struct fixed_phy_status state;
			int s;

			do {
				s = read_seqcount_begin(&fp->seqcount);
				fp->status.link = !fp->no_carrier;
				/* Issue callback if user registered it. */
				if (fp->link_update)
					fp->link_update(fp->phydev->attached_dev,
							&fp->status);
				/* Check the GPIO for change in status */
				fixed_phy_update(fp);
				state = fp->status;
			} while (read_seqcount_retry(&fp->seqcount, s));

			return swphy_read_reg(reg_num, &state);
		}
	}

	return 0xFFFF;
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
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct fixed_phy *fp;

	if (!phydev || !phydev->mdio.bus)
		return -EINVAL;

	list_for_each_entry(fp, &fmb->phys, node) {
		if (fp->addr == phydev->mdio.addr) {
			fp->link_update = link_update;
			fp->phydev = phydev;
			return 0;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(fixed_phy_set_link_update);

static int fixed_phy_add_gpiod(unsigned int irq, int phy_addr,
			       struct fixed_phy_status *status,
			       struct gpio_desc *gpiod)
{
	int ret;
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct fixed_phy *fp;

	ret = swphy_validate_state(status);
	if (ret < 0)
		return ret;

	fp = kzalloc(sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	seqcount_init(&fp->seqcount);

	if (irq != PHY_POLL)
		fmb->mii_bus->irq[phy_addr] = irq;

	fp->addr = phy_addr;
	fp->status = *status;
	fp->link_gpiod = gpiod;

	fixed_phy_update(fp);

	list_add_tail(&fp->node, &fmb->phys);

	return 0;
}

int fixed_phy_add(unsigned int irq, int phy_addr,
		  struct fixed_phy_status *status) {

	return fixed_phy_add_gpiod(irq, phy_addr, status, NULL);
}
EXPORT_SYMBOL_GPL(fixed_phy_add);

static DEFINE_IDA(phy_fixed_ida);

static void fixed_phy_del(int phy_addr)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct fixed_phy *fp, *tmp;

	list_for_each_entry_safe(fp, tmp, &fmb->phys, node) {
		if (fp->addr == phy_addr) {
			list_del(&fp->node);
			if (fp->link_gpiod)
				gpiod_put(fp->link_gpiod);
			kfree(fp);
			ida_simple_remove(&phy_fixed_ida, phy_addr);
			return;
		}
	}
}

#ifdef CONFIG_OF_GPIO
static struct gpio_desc *fixed_phy_get_gpiod(struct device_node *np)
{
	struct device_node *fixed_link_node;
	struct gpio_desc *gpiod;

	if (!np)
		return NULL;

	fixed_link_node = of_get_child_by_name(np, "fixed-link");
	if (!fixed_link_node)
		return NULL;

	/*
	 * As the fixed link is just a device tree node without any
	 * Linux device associated with it, we simply have obtain
	 * the GPIO descriptor from the device tree like this.
	 */
	gpiod = gpiod_get_from_of_node(fixed_link_node, "link-gpios", 0,
				       GPIOD_IN, "mdio");
	of_node_put(fixed_link_node);
	if (IS_ERR(gpiod)) {
		if (PTR_ERR(gpiod) == -EPROBE_DEFER)
			return gpiod;
		pr_err("error getting GPIO for fixed link %pOF, proceed without\n",
		       fixed_link_node);
		gpiod = NULL;
	}

	return gpiod;
}
#else
static struct gpio_desc *fixed_phy_get_gpiod(struct device_node *np)
{
	return NULL;
}
#endif

static struct phy_device *__fixed_phy_register(unsigned int irq,
					       struct fixed_phy_status *status,
					       struct device_node *np,
					       struct gpio_desc *gpiod)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct phy_device *phy;
	int phy_addr;
	int ret;

	if (!fmb->mii_bus || fmb->mii_bus->state != MDIOBUS_REGISTERED)
		return ERR_PTR(-EPROBE_DEFER);

	/* Check if we have a GPIO associated with this fixed phy */
	if (!gpiod) {
		gpiod = fixed_phy_get_gpiod(np);
		if (IS_ERR(gpiod))
			return ERR_CAST(gpiod);
	}

	/* Get the next available PHY address, up to PHY_MAX_ADDR */
	phy_addr = ida_simple_get(&phy_fixed_ida, 0, PHY_MAX_ADDR, GFP_KERNEL);
	if (phy_addr < 0)
		return ERR_PTR(phy_addr);

	ret = fixed_phy_add_gpiod(irq, phy_addr, status, gpiod);
	if (ret < 0) {
		ida_simple_remove(&phy_fixed_ida, phy_addr);
		return ERR_PTR(ret);
	}

	phy = get_phy_device(fmb->mii_bus, phy_addr, false);
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
		/* fall through */
	case SPEED_100:
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT,
				 phy->supported);
		linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
				 phy->supported);
		/* fall through */
	case SPEED_10:
	default:
		linkmode_set_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
				 phy->supported);
		linkmode_set_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
				 phy->supported);
	}

	linkmode_copy(phy->advertising, phy->supported);

	ret = phy_device_register(phy);
	if (ret) {
		phy_device_free(phy);
		of_node_put(np);
		fixed_phy_del(phy_addr);
		return ERR_PTR(ret);
	}

	return phy;
}

struct phy_device *fixed_phy_register(unsigned int irq,
				      struct fixed_phy_status *status,
				      struct device_node *np)
{
	return __fixed_phy_register(irq, status, np, NULL);
}
EXPORT_SYMBOL_GPL(fixed_phy_register);

struct phy_device *
fixed_phy_register_with_gpiod(unsigned int irq,
			      struct fixed_phy_status *status,
			      struct gpio_desc *gpiod)
{
	return __fixed_phy_register(irq, status, NULL, gpiod);
}
EXPORT_SYMBOL_GPL(fixed_phy_register_with_gpiod);

void fixed_phy_unregister(struct phy_device *phy)
{
	phy_device_remove(phy);
	of_node_put(phy->mdio.dev.of_node);
	fixed_phy_del(phy->mdio.addr);
}
EXPORT_SYMBOL_GPL(fixed_phy_unregister);

static int __init fixed_mdio_bus_init(void)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	int ret;

	pdev = platform_device_register_simple("Fixed MDIO bus", 0, NULL, 0);
	if (IS_ERR(pdev))
		return PTR_ERR(pdev);

	fmb->mii_bus = mdiobus_alloc();
	if (fmb->mii_bus == NULL) {
		ret = -ENOMEM;
		goto err_mdiobus_reg;
	}

	snprintf(fmb->mii_bus->id, MII_BUS_ID_SIZE, "fixed-0");
	fmb->mii_bus->name = "Fixed MDIO Bus";
	fmb->mii_bus->priv = fmb;
	fmb->mii_bus->parent = &pdev->dev;
	fmb->mii_bus->read = &fixed_mdio_read;
	fmb->mii_bus->write = &fixed_mdio_write;

	ret = mdiobus_register(fmb->mii_bus);
	if (ret)
		goto err_mdiobus_alloc;

	return 0;

err_mdiobus_alloc:
	mdiobus_free(fmb->mii_bus);
err_mdiobus_reg:
	platform_device_unregister(pdev);
	return ret;
}
module_init(fixed_mdio_bus_init);

static void __exit fixed_mdio_bus_exit(void)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct fixed_phy *fp, *tmp;

	mdiobus_unregister(fmb->mii_bus);
	mdiobus_free(fmb->mii_bus);
	platform_device_unregister(pdev);

	list_for_each_entry_safe(fp, tmp, &fmb->phys, node) {
		list_del(&fp->node);
		kfree(fp);
	}
	ida_destroy(&phy_fixed_ida);
}
module_exit(fixed_mdio_bus_exit);

MODULE_DESCRIPTION("Fixed MDIO bus (MDIO bus emulation with fixed PHYs)");
MODULE_AUTHOR("Vitaly Bordug");
MODULE_LICENSE("GPL");
