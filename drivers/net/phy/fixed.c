/*
 * drivers/net/phy/fixed.c
 *
 * Driver for fixed PHYs, when transceiver is able to operate in one fixed mode.
 *
 * Author: Vitaly Bordug
 *
 * Copyright (c) 2006 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

/* we need to track the allocated pointers in order to free them on exit */
static struct fixed_info *fixed_phy_ptrs[CONFIG_FIXED_MII_AMNT*MAX_PHY_AMNT];

/*-----------------------------------------------------------------------------
 *  If something weird is required to be done with link/speed,
 * network driver is able to assign a function to implement this.
 * May be useful for PHY's that need to be software-driven.
 *-----------------------------------------------------------------------------*/
int fixed_mdio_set_link_update(struct phy_device *phydev,
			       int (*link_update) (struct net_device *,
						   struct fixed_phy_status *))
{
	struct fixed_info *fixed;

	if (link_update == NULL)
		return -EINVAL;

	if (phydev) {
		if (phydev->bus) {
			fixed = phydev->bus->priv;
			fixed->link_update = link_update;
			return 0;
		}
	}
	return -EINVAL;
}

EXPORT_SYMBOL(fixed_mdio_set_link_update);

struct fixed_info *fixed_mdio_get_phydev (int phydev_ind)
{
	if (phydev_ind >= MAX_PHY_AMNT)
		return NULL;
	return fixed_phy_ptrs[phydev_ind];
}

EXPORT_SYMBOL(fixed_mdio_get_phydev);

/*-----------------------------------------------------------------------------
 *  This is used for updating internal mii regs from the status
 *-----------------------------------------------------------------------------*/
#if defined(CONFIG_FIXED_MII_100_FDX) || defined(CONFIG_FIXED_MII_10_FDX) || defined(CONFIG_FIXED_MII_1000_FDX)
static int fixed_mdio_update_regs(struct fixed_info *fixed)
{
	u16 *regs = fixed->regs;
	u16 bmsr = 0;
	u16 bmcr = 0;

	if (!regs) {
		printk(KERN_ERR "%s: regs not set up", __FUNCTION__);
		return -EINVAL;
	}

	if (fixed->phy_status.link)
		bmsr |= BMSR_LSTATUS;

	if (fixed->phy_status.duplex) {
		bmcr |= BMCR_FULLDPLX;

		switch (fixed->phy_status.speed) {
		case 100:
			bmsr |= BMSR_100FULL;
			bmcr |= BMCR_SPEED100;
			break;

		case 10:
			bmsr |= BMSR_10FULL;
			break;
		}
	} else {
		switch (fixed->phy_status.speed) {
		case 100:
			bmsr |= BMSR_100HALF;
			bmcr |= BMCR_SPEED100;
			break;

		case 10:
			bmsr |= BMSR_100HALF;
			break;
		}
	}

	regs[MII_BMCR] = bmcr;
	regs[MII_BMSR] = bmsr | 0x800;	/*we are always capable of 10 hdx */

	return 0;
}

static int fixed_mii_read(struct mii_bus *bus, int phy_id, int location)
{
	struct fixed_info *fixed = bus->priv;

	/* if user has registered link update callback, use it */
	if (fixed->phydev)
		if (fixed->phydev->attached_dev) {
			if (fixed->link_update) {
				fixed->link_update(fixed->phydev->attached_dev,
						   &fixed->phy_status);
				fixed_mdio_update_regs(fixed);
			}
		}

	if ((unsigned int)location >= fixed->regs_num)
		return -1;
	return fixed->regs[location];
}

static int fixed_mii_write(struct mii_bus *bus, int phy_id, int location,
			   u16 val)
{
	/* do nothing for now */
	return 0;
}

static int fixed_mii_reset(struct mii_bus *bus)
{
	/*nothing here - no way/need to reset it */
	return 0;
}
#endif

static int fixed_config_aneg(struct phy_device *phydev)
{
	/* :TODO:03/13/2006 09:45:37 PM::
	   The full autoneg funcionality can be emulated,
	   but no need to have anything here for now
	 */
	return 0;
}

/*-----------------------------------------------------------------------------
 * the manual bind will do the magic - with phy_id_mask == 0
 * match will never return true...
 *-----------------------------------------------------------------------------*/
static struct phy_driver fixed_mdio_driver = {
	.name = "Fixed PHY",
#ifdef CONFIG_FIXED_MII_1000_FDX
	.features = PHY_GBIT_FEATURES,
#else
	.features = PHY_BASIC_FEATURES,
#endif
	.config_aneg = fixed_config_aneg,
	.read_status = genphy_read_status,
	.driver = { .owner = THIS_MODULE, },
};

static void fixed_mdio_release(struct device *dev)
{
	struct phy_device *phydev = container_of(dev, struct phy_device, dev);
	struct mii_bus *bus = phydev->bus;
	struct fixed_info *fixed = bus->priv;

	kfree(phydev);
	kfree(bus->dev);
	kfree(bus);
	kfree(fixed->regs);
	kfree(fixed);
}

/*-----------------------------------------------------------------------------
 *  This func is used to create all the necessary stuff, bind
 * the fixed phy driver and register all it on the mdio_bus_type.
 * speed is either 10 or 100 or 1000, duplex is boolean.
 * number is used to create multiple fixed PHYs, so that several devices can
 * utilize them simultaneously.
 *
 * The device on mdio bus will look like [bus_id]:[phy_id],
 * bus_id = number
 * phy_id = speed+duplex.
 *-----------------------------------------------------------------------------*/
#if defined(CONFIG_FIXED_MII_100_FDX) || defined(CONFIG_FIXED_MII_10_FDX) || defined(CONFIG_FIXED_MII_1000_FDX)
struct fixed_info *fixed_mdio_register_device(
	int bus_id, int speed, int duplex, u8 phy_id)
{
	struct mii_bus *new_bus;
	struct fixed_info *fixed;
	struct phy_device *phydev;
	int err;

	struct device *dev = kzalloc(sizeof(struct device), GFP_KERNEL);

	if (dev == NULL)
		goto err_dev_alloc;

	new_bus = kzalloc(sizeof(struct mii_bus), GFP_KERNEL);

	if (new_bus == NULL)
		goto err_bus_alloc;

	fixed = kzalloc(sizeof(struct fixed_info), GFP_KERNEL);

	if (fixed == NULL)
		goto err_fixed_alloc;

	fixed->regs = kzalloc(MII_REGS_NUM * sizeof(int), GFP_KERNEL);
	if (NULL == fixed->regs)
		goto err_fixed_regs_alloc;

	fixed->regs_num = MII_REGS_NUM;
	fixed->phy_status.speed = speed;
	fixed->phy_status.duplex = duplex;
	fixed->phy_status.link = 1;

	new_bus->name = "Fixed MII Bus";
	new_bus->read = &fixed_mii_read;
	new_bus->write = &fixed_mii_write;
	new_bus->reset = &fixed_mii_reset;
	/*set up workspace */
	fixed_mdio_update_regs(fixed);
	new_bus->priv = fixed;

	new_bus->dev = dev;
	dev_set_drvdata(dev, new_bus);

	/* create phy_device and register it on the mdio bus */
	phydev = phy_device_create(new_bus, 0, 0);
	if (phydev == NULL)
		goto err_phy_dev_create;

	/*
	 * Put the phydev pointer into the fixed pack so that bus read/write
	 * code could be able to access for instance attached netdev. Well it
	 * doesn't have to do so, only in case of utilizing user-specified
	 * link-update...
	 */

	fixed->phydev = phydev;
	phydev->speed = speed;
	phydev->duplex = duplex;

	phydev->irq = PHY_IGNORE_INTERRUPT;
	phydev->dev.bus = &mdio_bus_type;

	snprintf(phydev->dev.bus_id, BUS_ID_SIZE,
		 PHY_ID_FMT, bus_id, phy_id);

	phydev->bus = new_bus;

	phydev->dev.driver = &fixed_mdio_driver.driver;
	phydev->dev.release = fixed_mdio_release;
	err = phydev->dev.driver->probe(&phydev->dev);
	if (err < 0) {
		printk(KERN_ERR "Phy %s: problems with fixed driver\n",
		       phydev->dev.bus_id);
		goto err_out;
	}
	err = device_register(&phydev->dev);
	if (err) {
		printk(KERN_ERR "Phy %s failed to register\n",
		       phydev->dev.bus_id);
		goto err_out;
	}
	//phydev->state = PHY_RUNNING; /* make phy go up quick, but in 10Mbit/HDX
	return fixed;

err_out:
	kfree(phydev);
err_phy_dev_create:
	kfree(fixed->regs);
err_fixed_regs_alloc:
	kfree(fixed);
err_fixed_alloc:
	kfree(new_bus);
err_bus_alloc:
	kfree(dev);
err_dev_alloc:

	return NULL;

}
#endif

MODULE_DESCRIPTION("Fixed PHY device & driver for PAL");
MODULE_AUTHOR("Vitaly Bordug");
MODULE_LICENSE("GPL");

static int __init fixed_init(void)
{
	int cnt = 0;
	int i;
/* register on the bus... Not expected to be matched
 * with anything there...
 *
 */
	phy_driver_register(&fixed_mdio_driver);

/* We will create several mdio devices here, and will bound the upper
 * driver to them.
 *
 * Then the external software can lookup the phy bus by searching
 * for 0:101, to be connected to the virtual 100M Fdx phy.
 *
 * In case several virtual PHYs required, the bus_id will be in form
 * [num]:[duplex]+[speed], which make it able even to define
 * driver-specific link control callback, if for instance PHY is
 * completely SW-driven.
 */
	for (i=1; i <= CONFIG_FIXED_MII_AMNT; i++) {
#ifdef CONFIG_FIXED_MII_1000_FDX
		fixed_phy_ptrs[cnt++] = fixed_mdio_register_device(0, 1000, 1, i);
#endif
#ifdef CONFIG_FIXED_MII_100_FDX
		fixed_phy_ptrs[cnt++] = fixed_mdio_register_device(1, 100, 1, i);
#endif
#ifdef CONFIG_FIXED_MII_10_FDX
		fixed_phy_ptrs[cnt++] = fixed_mdio_register_device(2, 10, 1, i);
#endif
	}

	return 0;
}

static void __exit fixed_exit(void)
{
	int i;

	phy_driver_unregister(&fixed_mdio_driver);
	for (i=0; i < MAX_PHY_AMNT; i++)
		if ( fixed_phy_ptrs[i] )
			device_unregister(&fixed_phy_ptrs[i]->phydev->dev);
}

module_init(fixed_init);
module_exit(fixed_exit);
