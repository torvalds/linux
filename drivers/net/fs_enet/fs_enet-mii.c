/*
 * Combined Ethernet driver for Motorola MPC8xx and MPC82xx.
 *
 * Copyright (c) 2003 Intracom S.A. 
 *  by Pantelis Antoniou <panto@intracom.gr>
 * 
 * 2005 (c) MontaVista Software, Inc. 
 * Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * Heavily based on original FEC driver by Dan Malek <dan@embeddededge.com>
 * and modifications by Joakim Tjernlund <joakim.tjernlund@lumentis.se>
 *
 * This file is licensed under the terms of the GNU General Public License 
 * version 2. This program is licensed "as is" without any warranty of any 
 * kind, whether express or implied.
 */


#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/bitops.h>

#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

#include "fs_enet.h"

/*************************************************/

/*
 * Generic PHY support.
 * Should work for all PHYs, but link change is detected by polling
 */

static void generic_timer_callback(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct fs_enet_private *fep = netdev_priv(dev);

	fep->phy_timer_list.expires = jiffies + HZ / 2;

	add_timer(&fep->phy_timer_list);

	fs_mii_link_status_change_check(dev, 0);
}

static void generic_startup(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	fep->phy_timer_list.expires = jiffies + HZ / 2;	/* every 500ms */
	fep->phy_timer_list.data = (unsigned long)dev;
	fep->phy_timer_list.function = generic_timer_callback;
	add_timer(&fep->phy_timer_list);
}

static void generic_shutdown(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	del_timer_sync(&fep->phy_timer_list);
}

/* ------------------------------------------------------------------------- */
/* The Davicom DM9161 is used on the NETTA board			     */

/* register definitions */

#define MII_DM9161_ANAR		4	/* Aux. Config Register         */
#define MII_DM9161_ACR		16	/* Aux. Config Register         */
#define MII_DM9161_ACSR		17	/* Aux. Config/Status Register  */
#define MII_DM9161_10TCSR	18	/* 10BaseT Config/Status Reg.   */
#define MII_DM9161_INTR		21	/* Interrupt Register           */
#define MII_DM9161_RECR		22	/* Receive Error Counter Reg.   */
#define MII_DM9161_DISCR	23	/* Disconnect Counter Register  */

static void dm9161_startup(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	fs_mii_write(dev, fep->mii_if.phy_id, MII_DM9161_INTR, 0x0000);
	/* Start autonegotiation */
	fs_mii_write(dev, fep->mii_if.phy_id, MII_BMCR, 0x1200);

	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ*8);
}

static void dm9161_ack_int(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	fs_mii_read(dev, fep->mii_if.phy_id, MII_DM9161_INTR);
}

static void dm9161_shutdown(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	fs_mii_write(dev, fep->mii_if.phy_id, MII_DM9161_INTR, 0x0f00);
}

/**********************************************************************************/

static const struct phy_info phy_info[] = {
	{
		.id = 0x00181b88,
		.name = "DM9161",
		.startup = dm9161_startup,
		.ack_int = dm9161_ack_int,
		.shutdown = dm9161_shutdown,
	}, {
		.id = 0,
		.name = "GENERIC",
		.startup = generic_startup,
		.shutdown = generic_shutdown,
	},
};

/**********************************************************************************/

static int phy_id_detect(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;
	struct fs_enet_mii_bus *bus = fep->mii_bus;
	int i, r, start, end, phytype, physubtype;
	const struct phy_info *phy;
	int phy_hwid, phy_id;

	phy_hwid = -1;
	fep->phy = NULL;

	/* auto-detect? */
	if (fpi->phy_addr == -1) {
		start = 1;
		end = 32;
	} else {		/* direct */
		start = fpi->phy_addr;
		end = start + 1;
	}

	for (phy_id = start; phy_id < end; phy_id++) {
		/* skip already used phy addresses on this bus */ 
		if (bus->usage_map & (1 << phy_id))
			continue;
		r = fs_mii_read(dev, phy_id, MII_PHYSID1);
		if (r == -1 || (phytype = (r & 0xffff)) == 0xffff)
			continue;
		r = fs_mii_read(dev, phy_id, MII_PHYSID2);
		if (r == -1 || (physubtype = (r & 0xffff)) == 0xffff)
			continue;
		phy_hwid = (phytype << 16) | physubtype;
		if (phy_hwid != -1)
			break;
	}

	if (phy_hwid == -1) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s No PHY detected! range=0x%02x-0x%02x\n",
			dev->name, start, end);
		return -1;
	}

	for (i = 0, phy = phy_info; i < ARRAY_SIZE(phy_info); i++, phy++)
		if (phy->id == (phy_hwid >> 4) || phy->id == 0)
			break;

	if (i >= ARRAY_SIZE(phy_info)) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s PHY id 0x%08x is not supported!\n",
		       dev->name, phy_hwid);
		return -1;
	}

	fep->phy = phy;

	/* mark this address as used */
	bus->usage_map |= (1 << phy_id);

	printk(KERN_INFO DRV_MODULE_NAME
	       ": %s Phy @ 0x%x, type %s (0x%08x)%s\n",
	       dev->name, phy_id, fep->phy->name, phy_hwid,
	       fpi->phy_addr == -1 ? " (auto-detected)" : "");

	return phy_id;
}

void fs_mii_startup(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	if (fep->phy->startup)
		(*fep->phy->startup) (dev);
}

void fs_mii_shutdown(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	if (fep->phy->shutdown)
		(*fep->phy->shutdown) (dev);
}

void fs_mii_ack_int(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);

	if (fep->phy->ack_int)
		(*fep->phy->ack_int) (dev);
}

#define MII_LINK	0x0001
#define MII_HALF	0x0002
#define MII_FULL	0x0004
#define MII_BASE4	0x0008
#define MII_10M		0x0010
#define MII_100M	0x0020
#define MII_1G		0x0040
#define MII_10G		0x0080

/* return full mii info at one gulp, with a usable form */
static unsigned int mii_full_status(struct mii_if_info *mii)
{
	unsigned int status;
	int bmsr, adv, lpa, neg;
	struct fs_enet_private* fep = netdev_priv(mii->dev);
	
	/* first, a dummy read, needed to latch some MII phys */
	(void)mii->mdio_read(mii->dev, mii->phy_id, MII_BMSR);
	bmsr = mii->mdio_read(mii->dev, mii->phy_id, MII_BMSR);

	/* no link */
	if ((bmsr & BMSR_LSTATUS) == 0)
		return 0;

	status = MII_LINK;
	
	/* Lets look what ANEG says if it's supported - otherwize we shall
	   take the right values from the platform info*/
	if(!mii->force_media) {
		/* autoneg not completed; don't bother */
		if ((bmsr & BMSR_ANEGCOMPLETE) == 0)
			return 0;

		adv = (*mii->mdio_read)(mii->dev, mii->phy_id, MII_ADVERTISE);
		lpa = (*mii->mdio_read)(mii->dev, mii->phy_id, MII_LPA);

		neg = lpa & adv;
	} else {
		neg = fep->fpi->bus_info->lpa;
	}

	if (neg & LPA_100FULL)
		status |= MII_FULL | MII_100M;
	else if (neg & LPA_100BASE4)
		status |= MII_FULL | MII_BASE4 | MII_100M;
	else if (neg & LPA_100HALF)
		status |= MII_HALF | MII_100M;
	else if (neg & LPA_10FULL)
		status |= MII_FULL | MII_10M;
	else
		status |= MII_HALF | MII_10M;
	
	return status;
}

void fs_mii_link_status_change_check(struct net_device *dev, int init_media)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct mii_if_info *mii = &fep->mii_if;
	unsigned int mii_status;
	int ok_to_print, link, duplex, speed;
	unsigned long flags;

	ok_to_print = netif_msg_link(fep);

	mii_status = mii_full_status(mii);

	if (!init_media && mii_status == fep->last_mii_status)
		return;

	fep->last_mii_status = mii_status;

	link = !!(mii_status & MII_LINK);
	duplex = !!(mii_status & MII_FULL);
	speed = (mii_status & MII_100M) ? 100 : 10;

	if (link == 0) {
		netif_carrier_off(mii->dev);
		netif_stop_queue(dev);
		if (!init_media) {
			spin_lock_irqsave(&fep->lock, flags);
			(*fep->ops->stop)(dev);
			spin_unlock_irqrestore(&fep->lock, flags);
		}

		if (ok_to_print)
			printk(KERN_INFO "%s: link down\n", mii->dev->name);

	} else {

		mii->full_duplex = duplex;

		netif_carrier_on(mii->dev);

		spin_lock_irqsave(&fep->lock, flags);
		fep->duplex = duplex;
		fep->speed = speed;
		(*fep->ops->restart)(dev);
		spin_unlock_irqrestore(&fep->lock, flags);

		netif_start_queue(dev);

		if (ok_to_print)
			printk(KERN_INFO "%s: link up, %dMbps, %s-duplex\n",
			       dev->name, speed, duplex ? "full" : "half");
	}
}

/**********************************************************************************/

int fs_mii_read(struct net_device *dev, int phy_id, int location)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fs_enet_mii_bus *bus = fep->mii_bus;

	unsigned long flags;
	int ret;

	spin_lock_irqsave(&bus->mii_lock, flags);
	ret = (*bus->mii_read)(bus, phy_id, location);
	spin_unlock_irqrestore(&bus->mii_lock, flags);

	return ret;
}

void fs_mii_write(struct net_device *dev, int phy_id, int location, int value)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fs_enet_mii_bus *bus = fep->mii_bus;
	unsigned long flags;

	spin_lock_irqsave(&bus->mii_lock, flags);
	(*bus->mii_write)(bus, phy_id, location, value);
	spin_unlock_irqrestore(&bus->mii_lock, flags);
}

/*****************************************************************************/

/* list of all registered mii buses */
static LIST_HEAD(fs_mii_bus_list);

static struct fs_enet_mii_bus *lookup_bus(int method, int id)
{
	struct list_head *ptr;
	struct fs_enet_mii_bus *bus;

	list_for_each(ptr, &fs_mii_bus_list) {
		bus = list_entry(ptr, struct fs_enet_mii_bus, list);
		if (bus->bus_info->method == method &&
			bus->bus_info->id == id)
			return bus;
	}
	return NULL;
}

static struct fs_enet_mii_bus *create_bus(const struct fs_mii_bus_info *bi)
{
	struct fs_enet_mii_bus *bus;
	int ret = 0;

	bus = kmalloc(sizeof(*bus), GFP_KERNEL);
	if (bus == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	memset(bus, 0, sizeof(*bus));
	spin_lock_init(&bus->mii_lock);
	bus->bus_info = bi;
	bus->refs = 0;
	bus->usage_map = 0;

	/* perform initialization */
	switch (bi->method) {

		case fsmii_fixed:
			ret = fs_mii_fixed_init(bus);
			if (ret != 0)
				goto err;
			break;

		case fsmii_bitbang:
			ret = fs_mii_bitbang_init(bus);
			if (ret != 0)
				goto err;
			break;
#ifdef CONFIG_FS_ENET_HAS_FEC
		case fsmii_fec:
			ret = fs_mii_fec_init(bus);
			if (ret != 0)
				goto err;
			break;
#endif
		default:
			ret = -EINVAL;
			goto err;
	}

	list_add(&bus->list, &fs_mii_bus_list);

	return bus;

err:
	if (bus)
		kfree(bus);
	return ERR_PTR(ret);
}

static void destroy_bus(struct fs_enet_mii_bus *bus)
{
	/* remove from bus list */
	list_del(&bus->list);

	/* nothing more needed */
	kfree(bus);
}

int fs_mii_connect(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	const struct fs_platform_info *fpi = fep->fpi;
	struct fs_enet_mii_bus *bus = NULL;

	/* check method validity */
	switch (fpi->bus_info->method) {
		case fsmii_fixed:
		case fsmii_bitbang:
			break;
#ifdef CONFIG_FS_ENET_HAS_FEC
		case fsmii_fec:
			break;
#endif
		default:
			printk(KERN_ERR DRV_MODULE_NAME
			       ": %s Unknown MII bus method (%d)!\n",
			       dev->name, fpi->bus_info->method);
			return -EINVAL; 
	}

	bus = lookup_bus(fpi->bus_info->method, fpi->bus_info->id);

	/* if not found create new bus */
	if (bus == NULL) {
		bus = create_bus(fpi->bus_info);
		if (IS_ERR(bus)) {
			printk(KERN_ERR DRV_MODULE_NAME
			       ": %s MII bus creation failure!\n", dev->name);
			return PTR_ERR(bus);
		}
	}

	bus->refs++;

	fep->mii_bus = bus;

	fep->mii_if.dev = dev;
	fep->mii_if.phy_id_mask = 0x1f;
	fep->mii_if.reg_num_mask = 0x1f;
	fep->mii_if.mdio_read = fs_mii_read;
	fep->mii_if.mdio_write = fs_mii_write;
	fep->mii_if.force_media = fpi->bus_info->disable_aneg;
	fep->mii_if.phy_id = phy_id_detect(dev);

	return 0;
}

void fs_mii_disconnect(struct net_device *dev)
{
	struct fs_enet_private *fep = netdev_priv(dev);
	struct fs_enet_mii_bus *bus = NULL;

	bus = fep->mii_bus;
	fep->mii_bus = NULL;

	if (--bus->refs <= 0)
		destroy_bus(bus);
}
