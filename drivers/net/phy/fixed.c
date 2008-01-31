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

#define MII_REGS_NUM 29

struct fixed_mdio_bus {
	int irqs[PHY_MAX_ADDR];
	struct mii_bus mii_bus;
	struct list_head phys;
};

struct fixed_phy {
	int id;
	u16 regs[MII_REGS_NUM];
	struct phy_device *phydev;
	struct fixed_phy_status status;
	int (*link_update)(struct net_device *, struct fixed_phy_status *);
	struct list_head node;
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

	if (fp->status.duplex) {
		bmcr |= BMCR_FULLDPLX;

		switch (fp->status.speed) {
		case 1000:
			bmsr |= BMSR_ESTATEN;
			bmcr |= BMCR_SPEED1000;
			lpagb |= LPA_1000FULL;
			break;
		case 100:
			bmsr |= BMSR_100FULL;
			bmcr |= BMCR_SPEED100;
			lpa |= LPA_100FULL;
			break;
		case 10:
			bmsr |= BMSR_10FULL;
			lpa |= LPA_10FULL;
			break;
		default:
			printk(KERN_WARNING "fixed phy: unknown speed\n");
			return -EINVAL;
		}
	} else {
		switch (fp->status.speed) {
		case 1000:
			bmsr |= BMSR_ESTATEN;
			bmcr |= BMCR_SPEED1000;
			lpagb |= LPA_1000HALF;
			break;
		case 100:
			bmsr |= BMSR_100HALF;
			bmcr |= BMCR_SPEED100;
			lpa |= LPA_100HALF;
			break;
		case 10:
			bmsr |= BMSR_10HALF;
			lpa |= LPA_10HALF;
			break;
		default:
			printk(KERN_WARNING "fixed phy: unknown speed\n");
			return -EINVAL;
		}
	}

	if (fp->status.link)
		bmsr |= BMSR_LSTATUS | BMSR_ANEGCOMPLETE;

	if (fp->status.pause)
		lpa |= LPA_PAUSE_CAP;

	if (fp->status.asym_pause)
		lpa |= LPA_PAUSE_ASYM;

	fp->regs[MII_PHYSID1] = fp->id >> 16;
	fp->regs[MII_PHYSID2] = fp->id;

	fp->regs[MII_BMSR] = bmsr;
	fp->regs[MII_BMCR] = bmcr;
	fp->regs[MII_LPA] = lpa;
	fp->regs[MII_STAT1000] = lpagb;

	return 0;
}

static int fixed_mdio_read(struct mii_bus *bus, int phy_id, int reg_num)
{
	struct fixed_mdio_bus *fmb = container_of(bus, struct fixed_mdio_bus,
						  mii_bus);
	struct fixed_phy *fp;

	if (reg_num >= MII_REGS_NUM)
		return -1;

	list_for_each_entry(fp, &fmb->phys, node) {
		if (fp->id == phy_id) {
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

static int fixed_mdio_write(struct mii_bus *bus, int phy_id, int reg_num,
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

	if (!link_update || !phydev || !phydev->bus)
		return -EINVAL;

	list_for_each_entry(fp, &fmb->phys, node) {
		if (fp->id == phydev->phy_id) {
			fp->link_update = link_update;
			fp->phydev = phydev;
			return 0;
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL_GPL(fixed_phy_set_link_update);

int fixed_phy_add(unsigned int irq, int phy_id,
		  struct fixed_phy_status *status)
{
	int ret;
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct fixed_phy *fp;

	fp = kzalloc(sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	memset(fp->regs, 0xFF,  sizeof(fp->regs[0]) * MII_REGS_NUM);

	fmb->irqs[phy_id] = irq;

	fp->id = phy_id;
	fp->status = *status;

	ret = fixed_phy_update_regs(fp);
	if (ret)
		goto err_regs;

	list_add_tail(&fp->node, &fmb->phys);

	return 0;

err_regs:
	kfree(fp);
	return ret;
}
EXPORT_SYMBOL_GPL(fixed_phy_add);

static int __init fixed_mdio_bus_init(void)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	int ret;

	pdev = platform_device_register_simple("Fixed MDIO bus", 0, NULL, 0);
	if (!pdev) {
		ret = -ENOMEM;
		goto err_pdev;
	}

	fmb->mii_bus.id = 0;
	fmb->mii_bus.name = "Fixed MDIO Bus";
	fmb->mii_bus.dev = &pdev->dev;
	fmb->mii_bus.read = &fixed_mdio_read;
	fmb->mii_bus.write = &fixed_mdio_write;
	fmb->mii_bus.irq = fmb->irqs;

	ret = mdiobus_register(&fmb->mii_bus);
	if (ret)
		goto err_mdiobus_reg;

	return 0;

err_mdiobus_reg:
	platform_device_unregister(pdev);
err_pdev:
	return ret;
}
module_init(fixed_mdio_bus_init);

static void __exit fixed_mdio_bus_exit(void)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct fixed_phy *fp;

	mdiobus_unregister(&fmb->mii_bus);
	platform_device_unregister(pdev);

	list_for_each_entry(fp, &fmb->phys, node) {
		list_del(&fp->node);
		kfree(fp);
	}
}
module_exit(fixed_mdio_bus_exit);

MODULE_DESCRIPTION("Fixed MDIO bus (MDIO bus emulation with fixed PHYs)");
MODULE_AUTHOR("Vitaly Bordug");
MODULE_LICENSE("GPL");
