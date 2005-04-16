/*
 * Fast Ethernet Controller (FEC) driver for Motorola MPC8xx.
 *
 * Copyright (c) 2003 Intracom S.A. 
 *  by Pantelis Antoniou <panto@intracom.gr>
 *
 * Heavily based on original FEC driver by Dan Malek <dan@embeddededge.com>
 * and modifications by Joakim Tjernlund <joakim.tjernlund@lumentis.se>
 *
 * Released under the GPL
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

#include <asm/8xx_immap.h>
#include <asm/pgtable.h>
#include <asm/mpc8xx.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/commproc.h>

/*************************************************/

#include "fec_8xx.h"

/*************************************************/

/* Make MII read/write commands for the FEC.
*/
#define mk_mii_read(REG)	(0x60020000 | ((REG & 0x1f) << 18))
#define mk_mii_write(REG, VAL)	(0x50020000 | ((REG & 0x1f) << 18) | (VAL & 0xffff))
#define mk_mii_end		0

/*************************************************/

/* XXX both FECs use the MII interface of FEC1 */
static DEFINE_SPINLOCK(fec_mii_lock);

#define FEC_MII_LOOPS	10000

int fec_mii_read(struct net_device *dev, int phy_id, int location)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	fec_t *fecp;
	int i, ret = -1;
	unsigned long flags;

	/* XXX MII interface is only connected to FEC1 */
	fecp = &((immap_t *) IMAP_ADDR)->im_cpm.cp_fec;

	spin_lock_irqsave(&fec_mii_lock, flags);

	if ((FR(fecp, r_cntrl) & FEC_RCNTRL_MII_MODE) == 0) {
		FS(fecp, r_cntrl, FEC_RCNTRL_MII_MODE);	/* MII enable */
		FS(fecp, ecntrl, FEC_ECNTRL_PINMUX | FEC_ECNTRL_ETHER_EN);
		FW(fecp, ievent, FEC_ENET_MII);
	}

	/* Add PHY address to register command.  */
	FW(fecp, mii_speed, fep->fec_phy_speed);
	FW(fecp, mii_data, (phy_id << 23) | mk_mii_read(location));

	for (i = 0; i < FEC_MII_LOOPS; i++)
		if ((FR(fecp, ievent) & FEC_ENET_MII) != 0)
			break;

	if (i < FEC_MII_LOOPS) {
		FW(fecp, ievent, FEC_ENET_MII);
		ret = FR(fecp, mii_data) & 0xffff;
	}

	spin_unlock_irqrestore(&fec_mii_lock, flags);

	return ret;
}

void fec_mii_write(struct net_device *dev, int phy_id, int location, int value)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	fec_t *fecp;
	unsigned long flags;
	int i;

	/* XXX MII interface is only connected to FEC1 */
	fecp = &((immap_t *) IMAP_ADDR)->im_cpm.cp_fec;

	spin_lock_irqsave(&fec_mii_lock, flags);

	if ((FR(fecp, r_cntrl) & FEC_RCNTRL_MII_MODE) == 0) {
		FS(fecp, r_cntrl, FEC_RCNTRL_MII_MODE);	/* MII enable */
		FS(fecp, ecntrl, FEC_ECNTRL_PINMUX | FEC_ECNTRL_ETHER_EN);
		FW(fecp, ievent, FEC_ENET_MII);
	}

	/* Add PHY address to register command.  */
	FW(fecp, mii_speed, fep->fec_phy_speed);	/* always adapt mii speed */
	FW(fecp, mii_data, (phy_id << 23) | mk_mii_write(location, value));

	for (i = 0; i < FEC_MII_LOOPS; i++)
		if ((FR(fecp, ievent) & FEC_ENET_MII) != 0)
			break;

	if (i < FEC_MII_LOOPS)
		FW(fecp, ievent, FEC_ENET_MII);

	spin_unlock_irqrestore(&fec_mii_lock, flags);
}

/*************************************************/

#ifdef CONFIG_FEC_8XX_GENERIC_PHY

/*
 * Generic PHY support.
 * Should work for all PHYs, but link change is detected by polling
 */

static void generic_timer_callback(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct fec_enet_private *fep = netdev_priv(dev);

	fep->phy_timer_list.expires = jiffies + HZ / 2;

	add_timer(&fep->phy_timer_list);

	fec_mii_link_status_change_check(dev, 0);
}

static void generic_startup(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);

	fep->phy_timer_list.expires = jiffies + HZ / 2;	/* every 500ms */
	fep->phy_timer_list.data = (unsigned long)dev;
	fep->phy_timer_list.function = generic_timer_callback;
	add_timer(&fep->phy_timer_list);
}

static void generic_shutdown(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);

	del_timer_sync(&fep->phy_timer_list);
}

#endif

#ifdef CONFIG_FEC_8XX_DM9161_PHY

/* ------------------------------------------------------------------------- */
/* The Davicom DM9161 is used on the NETTA board			     */

/* register definitions */

#define MII_DM9161_ACR		16	/* Aux. Config Register         */
#define MII_DM9161_ACSR		17	/* Aux. Config/Status Register  */
#define MII_DM9161_10TCSR	18	/* 10BaseT Config/Status Reg.   */
#define MII_DM9161_INTR		21	/* Interrupt Register           */
#define MII_DM9161_RECR		22	/* Receive Error Counter Reg.   */
#define MII_DM9161_DISCR	23	/* Disconnect Counter Register  */

static void dm9161_startup(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);

	fec_mii_write(dev, fep->mii_if.phy_id, MII_DM9161_INTR, 0x0000);
}

static void dm9161_ack_int(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);

	fec_mii_read(dev, fep->mii_if.phy_id, MII_DM9161_INTR);
}

static void dm9161_shutdown(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);

	fec_mii_write(dev, fep->mii_if.phy_id, MII_DM9161_INTR, 0x0f00);
}

#endif

/**********************************************************************************/

static const struct phy_info phy_info[] = {
#ifdef CONFIG_FEC_8XX_DM9161_PHY
	{
	 .id = 0x00181b88,
	 .name = "DM9161",
	 .startup = dm9161_startup,
	 .ack_int = dm9161_ack_int,
	 .shutdown = dm9161_shutdown,
	 },
#endif
#ifdef CONFIG_FEC_8XX_GENERIC_PHY
	{
	 .id = 0,
	 .name = "GENERIC",
	 .startup = generic_startup,
	 .shutdown = generic_shutdown,
	 },
#endif
};

/**********************************************************************************/

int fec_mii_phy_id_detect(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	const struct fec_platform_info *fpi = fep->fpi;
	int i, r, start, end, phytype, physubtype;
	const struct phy_info *phy;
	int phy_hwid, phy_id;

	/* if no MDIO */
	if (fpi->use_mdio == 0)
		return -1;

	phy_hwid = -1;
	fep->phy = NULL;

	/* auto-detect? */
	if (fpi->phy_addr == -1) {
		start = 0;
		end = 32;
	} else {		/* direct */
		start = fpi->phy_addr;
		end = start + 1;
	}

	for (phy_id = start; phy_id < end; phy_id++) {
		r = fec_mii_read(dev, phy_id, MII_PHYSID1);
		if (r == -1 || (phytype = (r & 0xffff)) == 0xffff)
			continue;
		r = fec_mii_read(dev, phy_id, MII_PHYSID2);
		if (r == -1 || (physubtype = (r & 0xffff)) == 0xffff)
			continue;
		phy_hwid = (phytype << 16) | physubtype;
		if (phy_hwid != -1)
			break;
	}

	if (phy_hwid == -1) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s No PHY detected!\n", dev->name);
		return -1;
	}

	for (i = 0, phy = phy_info; i < sizeof(phy_info) / sizeof(phy_info[0]);
	     i++, phy++)
		if (phy->id == (phy_hwid >> 4) || phy->id == 0)
			break;

	if (i >= sizeof(phy_info) / sizeof(phy_info[0])) {
		printk(KERN_ERR DRV_MODULE_NAME
		       ": %s PHY id 0x%08x is not supported!\n",
		       dev->name, phy_hwid);
		return -1;
	}

	fep->phy = phy;

	printk(KERN_INFO DRV_MODULE_NAME
	       ": %s Phy @ 0x%x, type %s (0x%08x)\n",
	       dev->name, phy_id, fep->phy->name, phy_hwid);

	return phy_id;
}

void fec_mii_startup(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	const struct fec_platform_info *fpi = fep->fpi;

	if (!fpi->use_mdio || fep->phy == NULL)
		return;

	if (fep->phy->startup == NULL)
		return;

	(*fep->phy->startup) (dev);
}

void fec_mii_shutdown(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	const struct fec_platform_info *fpi = fep->fpi;

	if (!fpi->use_mdio || fep->phy == NULL)
		return;

	if (fep->phy->shutdown == NULL)
		return;

	(*fep->phy->shutdown) (dev);
}

void fec_mii_ack_int(struct net_device *dev)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	const struct fec_platform_info *fpi = fep->fpi;

	if (!fpi->use_mdio || fep->phy == NULL)
		return;

	if (fep->phy->ack_int == NULL)
		return;

	(*fep->phy->ack_int) (dev);
}

/* helper function */
static int mii_negotiated(struct mii_if_info *mii)
{
	int advert, lpa, val;

	if (!mii_link_ok(mii))
		return 0;

	val = (*mii->mdio_read) (mii->dev, mii->phy_id, MII_BMSR);
	if ((val & BMSR_ANEGCOMPLETE) == 0)
		return 0;

	advert = (*mii->mdio_read) (mii->dev, mii->phy_id, MII_ADVERTISE);
	lpa = (*mii->mdio_read) (mii->dev, mii->phy_id, MII_LPA);

	return mii_nway_result(advert & lpa);
}

void fec_mii_link_status_change_check(struct net_device *dev, int init_media)
{
	struct fec_enet_private *fep = netdev_priv(dev);
	unsigned int media;
	unsigned long flags;

	if (mii_check_media(&fep->mii_if, netif_msg_link(fep), init_media) == 0)
		return;

	media = mii_negotiated(&fep->mii_if);

	if (netif_carrier_ok(dev)) {
		spin_lock_irqsave(&fep->lock, flags);
		fec_restart(dev, !!(media & ADVERTISE_FULL),
			    (media & (ADVERTISE_100FULL | ADVERTISE_100HALF)) ?
			    100 : 10);
		spin_unlock_irqrestore(&fep->lock, flags);

		netif_start_queue(dev);
	} else {
		netif_stop_queue(dev);

		spin_lock_irqsave(&fep->lock, flags);
		fec_stop(dev);
		spin_unlock_irqrestore(&fep->lock, flags);

	}
}
