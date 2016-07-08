/*
 * Fixed MDIO bus (MDIO bus emulation with fixed PHYs)
 *
 * Author: Vitaly Bordug <vbordug@ru.mvista.com>
 *         Anton Vorontsov <avorontsov@ru.mvista.com>
 *
 * Copyright (c) 2006-2007 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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
#include <linux/gpio.h>
#include <linux/idr.h>

#define MII_REGS_NUM 29

struct fixed_mdio_bus {
	struct mii_bus *mii_bus;
	struct list_head phys;
};

struct fixed_phy {
	int addr;
	u16 regs[MII_REGS_NUM];
	struct phy_device *phydev;
	struct fixed_phy_status status;
	int (*link_update)(struct net_device *, struct fixed_phy_status *);
	struct list_head node;
	int link_gpio;
};

static struct platform_device *pdev;
static struct fixed_mdio_bus platform_fmb = {
	.phys = LIST_HEAD_INIT(platform_fmb.phys),
};

static int fixed_phy_update_regs(struct fixed_phy *fp)
{
	u16 bmsr = BMSR_ANEGCAPABLE;
	u16 bmcr = 0;
	u16 lpagb = 0;
	u16 lpa = 0;

	if (gpio_is_valid(fp->link_gpio))
		fp->status.link = !!gpio_get_value_cansleep(fp->link_gpio);

	if (fp->status.duplex) {
		switch (fp->status.speed) {
		case 1000:
			bmsr |= BMSR_ESTATEN;
			break;
		case 100:
			bmsr |= BMSR_100FULL;
			break;
		case 10:
			bmsr |= BMSR_10FULL;
			break;
		default:
			break;
		}
	} else {
		switch (fp->status.speed) {
		case 1000:
			bmsr |= BMSR_ESTATEN;
			break;
		case 100:
			bmsr |= BMSR_100HALF;
			break;
		case 10:
			bmsr |= BMSR_10HALF;
			break;
		default:
			break;
		}
	}

	if (fp->status.link) {
		bmsr |= BMSR_LSTATUS | BMSR_ANEGCOMPLETE;

		if (fp->status.duplex) {
			bmcr |= BMCR_FULLDPLX;

			switch (fp->status.speed) {
			case 1000:
				bmcr |= BMCR_SPEED1000;
				lpagb |= LPA_1000FULL;
				break;
			case 100:
				bmcr |= BMCR_SPEED100;
				lpa |= LPA_100FULL;
				break;
			case 10:
				lpa |= LPA_10FULL;
				break;
			default:
				pr_warn("fixed phy: unknown speed\n");
				return -EINVAL;
			}
		} else {
			switch (fp->status.speed) {
			case 1000:
				bmcr |= BMCR_SPEED1000;
				lpagb |= LPA_1000HALF;
				break;
			case 100:
				bmcr |= BMCR_SPEED100;
				lpa |= LPA_100HALF;
				break;
			case 10:
				lpa |= LPA_10HALF;
				break;
			default:
				pr_warn("fixed phy: unknown speed\n");
			return -EINVAL;
			}
		}

		if (fp->status.pause)
			lpa |= LPA_PAUSE_CAP;

		if (fp->status.asym_pause)
			lpa |= LPA_PAUSE_ASYM;
	}

	fp->regs[MII_PHYSID1] = 0;
	fp->regs[MII_PHYSID2] = 0;

	fp->regs[MII_BMSR] = bmsr;
	fp->regs[MII_BMCR] = bmcr;
	fp->regs[MII_LPA] = lpa;
	fp->regs[MII_STAT1000] = lpagb;

	return 0;
}

static int fixed_mdio_read(struct mii_bus *bus, int phy_addr, int reg_num)
{
	struct fixed_mdio_bus *fmb = bus->priv;
	struct fixed_phy *fp;

	if (reg_num >= MII_REGS_NUM)
		return -1;

	/* We do not support emulating Clause 45 over Clause 22 register reads
	 * return an error instead of bogus data.
	 */
	switch (reg_num) {
	case MII_MMD_CTRL:
	case MII_MMD_DATA:
		return -1;
	default:
		break;
	}

	list_for_each_entry(fp, &fmb->phys, node) {
		if (fp->addr == phy_addr) {
			/* Issue callback if user registered it. */
			if (fp->link_update) {
				fp->link_update(fp->phydev->attached_dev,
						&fp->status);
				fixed_phy_update_regs(fp);
			}
			return fp->regs[reg_num];
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

int fixed_phy_update_state(struct phy_device *phydev,
			   const struct fixed_phy_status *status,
			   const struct fixed_phy_status *changed)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct fixed_phy *fp;

	if (!phydev || phydev->mdio.bus != fmb->mii_bus)
		return -EINVAL;

	list_for_each_entry(fp, &fmb->phys, node) {
		if (fp->addr == phydev->mdio.addr) {
#define _UPD(x) if (changed->x) \
	fp->status.x = status->x
			_UPD(link);
			_UPD(speed);
			_UPD(duplex);
			_UPD(pause);
			_UPD(asym_pause);
#undef _UPD
			fixed_phy_update_regs(fp);
			return 0;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL(fixed_phy_update_state);

int fixed_phy_add(unsigned int irq, int phy_addr,
		  struct fixed_phy_status *status,
		  int link_gpio)
{
	int ret;
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct fixed_phy *fp;

	fp = kzalloc(sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	memset(fp->regs, 0xFF,  sizeof(fp->regs[0]) * MII_REGS_NUM);

	if (irq != PHY_POLL)
		fmb->mii_bus->irq[phy_addr] = irq;

	fp->addr = phy_addr;
	fp->status = *status;
	fp->link_gpio = link_gpio;

	if (gpio_is_valid(fp->link_gpio)) {
		ret = gpio_request_one(fp->link_gpio, GPIOF_DIR_IN,
				       "fixed-link-gpio-link");
		if (ret)
			goto err_regs;
	}

	ret = fixed_phy_update_regs(fp);
	if (ret)
		goto err_gpio;

	list_add_tail(&fp->node, &fmb->phys);

	return 0;

err_gpio:
	if (gpio_is_valid(fp->link_gpio))
		gpio_free(fp->link_gpio);
err_regs:
	kfree(fp);
	return ret;
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
			if (gpio_is_valid(fp->link_gpio))
				gpio_free(fp->link_gpio);
			kfree(fp);
			ida_simple_remove(&phy_fixed_ida, phy_addr);
			return;
		}
	}
}

struct phy_device *fixed_phy_register(unsigned int irq,
				      struct fixed_phy_status *status,
				      int link_gpio,
				      struct device_node *np)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct phy_device *phy;
	int phy_addr;
	int ret;

	if (!fmb->mii_bus || fmb->mii_bus->state != MDIOBUS_REGISTERED)
		return ERR_PTR(-EPROBE_DEFER);

	/* Get the next available PHY address, up to PHY_MAX_ADDR */
	phy_addr = ida_simple_get(&phy_fixed_ida, 0, PHY_MAX_ADDR, GFP_KERNEL);
	if (phy_addr < 0)
		return ERR_PTR(phy_addr);

	ret = fixed_phy_add(irq, phy_addr, status, link_gpio);
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
		phy->supported = PHY_1000BT_FEATURES;
		break;
	case SPEED_100:
		phy->supported = PHY_100BT_FEATURES;
		break;
	case SPEED_10:
	default:
		phy->supported = PHY_10BT_FEATURES;
	}

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

	fixed_phy_del(phy->mdio.addr);
}
EXPORT_SYMBOL_GPL(fixed_phy_unregister);

static int __init fixed_mdio_bus_init(void)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	int ret;

	pdev = platform_device_register_simple("Fixed MDIO bus", 0, NULL, 0);
	if (IS_ERR(pdev)) {
		ret = PTR_ERR(pdev);
		goto err_pdev;
	}

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
err_pdev:
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
