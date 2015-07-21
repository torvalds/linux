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

#define MII_REGS_NUM 29

struct fixed_mdio_bus {
	int irqs[PHY_MAX_ADDR];
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

	if (!fp->status.link)
		goto done;
	bmsr |= BMSR_LSTATUS | BMSR_ANEGCOMPLETE;

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
			pr_warn("fixed phy: unknown speed\n");
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
			pr_warn("fixed phy: unknown speed\n");
			return -EINVAL;
		}
	}

	if (fp->status.pause)
		lpa |= LPA_PAUSE_CAP;

	if (fp->status.asym_pause)
		lpa |= LPA_PAUSE_ASYM;

done:
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

	if (!phydev || !phydev->bus)
		return -EINVAL;

	list_for_each_entry(fp, &fmb->phys, node) {
		if (fp->addr == phydev->addr) {
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

	if (!phydev || !phydev->bus)
		return -EINVAL;

	list_for_each_entry(fp, &fmb->phys, node) {
		if (fp->addr == phydev->addr) {
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
		  struct fixed_phy_status *status)
{
	int ret;
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct fixed_phy *fp;

	fp = kzalloc(sizeof(*fp), GFP_KERNEL);
	if (!fp)
		return -ENOMEM;

	memset(fp->regs, 0xFF,  sizeof(fp->regs[0]) * MII_REGS_NUM);

	fmb->irqs[phy_addr] = irq;

	fp->addr = phy_addr;
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

void fixed_phy_del(int phy_addr)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct fixed_phy *fp, *tmp;

	list_for_each_entry_safe(fp, tmp, &fmb->phys, node) {
		if (fp->addr == phy_addr) {
			list_del(&fp->node);
			kfree(fp);
			return;
		}
	}
}
EXPORT_SYMBOL_GPL(fixed_phy_del);

static int phy_fixed_addr;
static DEFINE_SPINLOCK(phy_fixed_addr_lock);

struct phy_device *fixed_phy_register(unsigned int irq,
				      struct fixed_phy_status *status,
				      struct device_node *np)
{
	struct fixed_mdio_bus *fmb = &platform_fmb;
	struct phy_device *phy;
	int phy_addr;
	int ret;

	/* Get the next available PHY address, up to PHY_MAX_ADDR */
	spin_lock(&phy_fixed_addr_lock);
	if (phy_fixed_addr == PHY_MAX_ADDR) {
		spin_unlock(&phy_fixed_addr_lock);
		return ERR_PTR(-ENOSPC);
	}
	phy_addr = phy_fixed_addr++;
	spin_unlock(&phy_fixed_addr_lock);

	ret = fixed_phy_add(PHY_POLL, phy_addr, status);
	if (ret < 0)
		return ERR_PTR(ret);

	phy = get_phy_device(fmb->mii_bus, phy_addr, false);
	if (!phy || IS_ERR(phy)) {
		fixed_phy_del(phy_addr);
		return ERR_PTR(-EINVAL);
	}

	of_node_get(np);
	phy->dev.of_node = np;

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
	fmb->mii_bus->irq = fmb->irqs;

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
}
module_exit(fixed_mdio_bus_exit);

MODULE_DESCRIPTION("Fixed MDIO bus (MDIO bus emulation with fixed PHYs)");
MODULE_AUTHOR("Vitaly Bordug");
MODULE_LICENSE("GPL");
