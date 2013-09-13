/*
 *
 * Alchemy Au1x00 ethernet driver
 *
 * Copyright 2001-2003, 2006 MontaVista Software Inc.
 * Copyright 2002 TimeSys Corp.
 * Added ethtool/mii-tool support,
 * Copyright 2004 Matt Porter <mporter@kernel.crashing.org>
 * Update: 2004 Bjoern Riemer, riemer@fokus.fraunhofer.de
 * or riemer@riemer-nt.de: fixed the link beat detection with
 * ioctls (SIOCGMIIPHY)
 * Copyright 2006 Herbert Valerio Riedel <hvr@gnu.org>
 *  converted to use linux-2.6.x's PHY framework
 *
 * Author: MontaVista Software, Inc.
 *		ppopov@mvista.com or source@mvista.com
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 *
 *
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/capability.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/ioport.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/phy.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/io.h>

#include <asm/mipsregs.h>
#include <asm/irq.h>
#include <asm/processor.h>

#include <au1000.h>
#include <au1xxx_eth.h>
#include <prom.h>

#include "au1000_eth.h"

#ifdef AU1000_ETH_DEBUG
static int au1000_debug = 5;
#else
static int au1000_debug = 3;
#endif

#define AU1000_DEF_MSG_ENABLE	(NETIF_MSG_DRV	| \
				NETIF_MSG_PROBE	| \
				NETIF_MSG_LINK)

#define DRV_NAME	"au1000_eth"
#define DRV_VERSION	"1.7"
#define DRV_AUTHOR	"Pete Popov <ppopov@embeddedalley.com>"
#define DRV_DESC	"Au1xxx on-chip Ethernet driver"

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
MODULE_LICENSE("GPL");
MODULE_VERSION(DRV_VERSION);

/*
 * Theory of operation
 *
 * The Au1000 MACs use a simple rx and tx descriptor ring scheme.
 * There are four receive and four transmit descriptors.  These
 * descriptors are not in memory; rather, they are just a set of
 * hardware registers.
 *
 * Since the Au1000 has a coherent data cache, the receive and
 * transmit buffers are allocated from the KSEG0 segment. The
 * hardware registers, however, are still mapped at KSEG1 to
 * make sure there's no out-of-order writes, and that all writes
 * complete immediately.
 */

/*
 * board-specific configurations
 *
 * PHY detection algorithm
 *
 * If phy_static_config is undefined, the PHY setup is
 * autodetected:
 *
 * mii_probe() first searches the current MAC's MII bus for a PHY,
 * selecting the first (or last, if phy_search_highest_addr is
 * defined) PHY address not already claimed by another netdev.
 *
 * If nothing was found that way when searching for the 2nd ethernet
 * controller's PHY and phy1_search_mac0 is defined, then
 * the first MII bus is searched as well for an unclaimed PHY; this is
 * needed in case of a dual-PHY accessible only through the MAC0's MII
 * bus.
 *
 * Finally, if no PHY is found, then the corresponding ethernet
 * controller is not registered to the network subsystem.
 */

/* autodetection defaults: phy1_search_mac0 */

/* static PHY setup
 *
 * most boards PHY setup should be detectable properly with the
 * autodetection algorithm in mii_probe(), but in some cases (e.g. if
 * you have a switch attached, or want to use the PHY's interrupt
 * notification capabilities) you can provide a static PHY
 * configuration here
 *
 * IRQs may only be set, if a PHY address was configured
 * If a PHY address is given, also a bus id is required to be set
 *
 * ps: make sure the used irqs are configured properly in the board
 * specific irq-map
 */

static void au1000_enable_mac(struct net_device *dev, int force_reset)
{
	unsigned long flags;
	struct au1000_private *aup = netdev_priv(dev);

	spin_lock_irqsave(&aup->lock, flags);

	if (force_reset || (!aup->mac_enabled)) {
		writel(MAC_EN_CLOCK_ENABLE, aup->enable);
		au_sync_delay(2);
		writel((MAC_EN_RESET0 | MAC_EN_RESET1 | MAC_EN_RESET2
				| MAC_EN_CLOCK_ENABLE), aup->enable);
		au_sync_delay(2);

		aup->mac_enabled = 1;
	}

	spin_unlock_irqrestore(&aup->lock, flags);
}

/*
 * MII operations
 */
static int au1000_mdio_read(struct net_device *dev, int phy_addr, int reg)
{
	struct au1000_private *aup = netdev_priv(dev);
	u32 *const mii_control_reg = &aup->mac->mii_control;
	u32 *const mii_data_reg = &aup->mac->mii_data;
	u32 timedout = 20;
	u32 mii_control;

	while (readl(mii_control_reg) & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			netdev_err(dev, "read_MII busy timeout!!\n");
			return -1;
		}
	}

	mii_control = MAC_SET_MII_SELECT_REG(reg) |
		MAC_SET_MII_SELECT_PHY(phy_addr) | MAC_MII_READ;

	writel(mii_control, mii_control_reg);

	timedout = 20;
	while (readl(mii_control_reg) & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			netdev_err(dev, "mdio_read busy timeout!!\n");
			return -1;
		}
	}
	return readl(mii_data_reg);
}

static void au1000_mdio_write(struct net_device *dev, int phy_addr,
			      int reg, u16 value)
{
	struct au1000_private *aup = netdev_priv(dev);
	u32 *const mii_control_reg = &aup->mac->mii_control;
	u32 *const mii_data_reg = &aup->mac->mii_data;
	u32 timedout = 20;
	u32 mii_control;

	while (readl(mii_control_reg) & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			netdev_err(dev, "mdio_write busy timeout!!\n");
			return;
		}
	}

	mii_control = MAC_SET_MII_SELECT_REG(reg) |
		MAC_SET_MII_SELECT_PHY(phy_addr) | MAC_MII_WRITE;

	writel(value, mii_data_reg);
	writel(mii_control, mii_control_reg);
}

static int au1000_mdiobus_read(struct mii_bus *bus, int phy_addr, int regnum)
{
	/* WARNING: bus->phy_map[phy_addr].attached_dev == dev does
	 * _NOT_ hold (e.g. when PHY is accessed through other MAC's MII bus)
	 */
	struct net_device *const dev = bus->priv;

	/* make sure the MAC associated with this
	 * mii_bus is enabled
	 */
	au1000_enable_mac(dev, 0);

	return au1000_mdio_read(dev, phy_addr, regnum);
}

static int au1000_mdiobus_write(struct mii_bus *bus, int phy_addr, int regnum,
				u16 value)
{
	struct net_device *const dev = bus->priv;

	/* make sure the MAC associated with this
	 * mii_bus is enabled
	 */
	au1000_enable_mac(dev, 0);

	au1000_mdio_write(dev, phy_addr, regnum, value);
	return 0;
}

static int au1000_mdiobus_reset(struct mii_bus *bus)
{
	struct net_device *const dev = bus->priv;

	/* make sure the MAC associated with this
	 * mii_bus is enabled
	 */
	au1000_enable_mac(dev, 0);

	return 0;
}

static void au1000_hard_stop(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);
	u32 reg;

	netif_dbg(aup, drv, dev, "hard stop\n");

	reg = readl(&aup->mac->control);
	reg &= ~(MAC_RX_ENABLE | MAC_TX_ENABLE);
	writel(reg, &aup->mac->control);
	au_sync_delay(10);
}

static void au1000_enable_rx_tx(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);
	u32 reg;

	netif_dbg(aup, hw, dev, "enable_rx_tx\n");

	reg = readl(&aup->mac->control);
	reg |= (MAC_RX_ENABLE | MAC_TX_ENABLE);
	writel(reg, &aup->mac->control);
	au_sync_delay(10);
}

static void
au1000_adjust_link(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);
	struct phy_device *phydev = aup->phy_dev;
	unsigned long flags;
	u32 reg;

	int status_change = 0;

	BUG_ON(!aup->phy_dev);

	spin_lock_irqsave(&aup->lock, flags);

	if (phydev->link && (aup->old_speed != phydev->speed)) {
		/* speed changed */

		switch (phydev->speed) {
		case SPEED_10:
		case SPEED_100:
			break;
		default:
			netdev_warn(dev, "Speed (%d) is not 10/100 ???\n",
							phydev->speed);
			break;
		}

		aup->old_speed = phydev->speed;

		status_change = 1;
	}

	if (phydev->link && (aup->old_duplex != phydev->duplex)) {
		/* duplex mode changed */

		/* switching duplex mode requires to disable rx and tx! */
		au1000_hard_stop(dev);

		reg = readl(&aup->mac->control);
		if (DUPLEX_FULL == phydev->duplex) {
			reg |= MAC_FULL_DUPLEX;
			reg &= ~MAC_DISABLE_RX_OWN;
		} else {
			reg &= ~MAC_FULL_DUPLEX;
			reg |= MAC_DISABLE_RX_OWN;
		}
		writel(reg, &aup->mac->control);
		au_sync_delay(1);

		au1000_enable_rx_tx(dev);
		aup->old_duplex = phydev->duplex;

		status_change = 1;
	}

	if (phydev->link != aup->old_link) {
		/* link state changed */

		if (!phydev->link) {
			/* link went down */
			aup->old_speed = 0;
			aup->old_duplex = -1;
		}

		aup->old_link = phydev->link;
		status_change = 1;
	}

	spin_unlock_irqrestore(&aup->lock, flags);

	if (status_change) {
		if (phydev->link)
			netdev_info(dev, "link up (%d/%s)\n",
			       phydev->speed,
			       DUPLEX_FULL == phydev->duplex ? "Full" : "Half");
		else
			netdev_info(dev, "link down\n");
	}
}

static int au1000_mii_probe(struct net_device *dev)
{
	struct au1000_private *const aup = netdev_priv(dev);
	struct phy_device *phydev = NULL;
	int phy_addr;

	if (aup->phy_static_config) {
		BUG_ON(aup->mac_id < 0 || aup->mac_id > 1);

		if (aup->phy_addr)
			phydev = aup->mii_bus->phy_map[aup->phy_addr];
		else
			netdev_info(dev, "using PHY-less setup\n");
		return 0;
	}

	/* find the first (lowest address) PHY
	 * on the current MAC's MII bus
	 */
	for (phy_addr = 0; phy_addr < PHY_MAX_ADDR; phy_addr++)
		if (aup->mii_bus->phy_map[phy_addr]) {
			phydev = aup->mii_bus->phy_map[phy_addr];
			if (!aup->phy_search_highest_addr)
				/* break out with first one found */
				break;
		}

	if (aup->phy1_search_mac0) {
		/* try harder to find a PHY */
		if (!phydev && (aup->mac_id == 1)) {
			/* no PHY found, maybe we have a dual PHY? */
			dev_info(&dev->dev, ": no PHY found on MAC1, "
				"let's see if it's attached to MAC0...\n");

			/* find the first (lowest address) non-attached
			 * PHY on the MAC0 MII bus
			 */
			for (phy_addr = 0; phy_addr < PHY_MAX_ADDR; phy_addr++) {
				struct phy_device *const tmp_phydev =
					aup->mii_bus->phy_map[phy_addr];

				if (aup->mac_id == 1)
					break;

				/* no PHY here... */
				if (!tmp_phydev)
					continue;

				/* already claimed by MAC0 */
				if (tmp_phydev->attached_dev)
					continue;

				phydev = tmp_phydev;
				break; /* found it */
			}
		}
	}

	if (!phydev) {
		netdev_err(dev, "no PHY found\n");
		return -1;
	}

	/* now we are supposed to have a proper phydev, to attach to... */
	BUG_ON(phydev->attached_dev);

	phydev = phy_connect(dev, dev_name(&phydev->dev),
			     &au1000_adjust_link, PHY_INTERFACE_MODE_MII);

	if (IS_ERR(phydev)) {
		netdev_err(dev, "Could not attach to PHY\n");
		return PTR_ERR(phydev);
	}

	/* mask with MAC supported features */
	phydev->supported &= (SUPPORTED_10baseT_Half
			      | SUPPORTED_10baseT_Full
			      | SUPPORTED_100baseT_Half
			      | SUPPORTED_100baseT_Full
			      | SUPPORTED_Autoneg
			      /* | SUPPORTED_Pause | SUPPORTED_Asym_Pause */
			      | SUPPORTED_MII
			      | SUPPORTED_TP);

	phydev->advertising = phydev->supported;

	aup->old_link = 0;
	aup->old_speed = 0;
	aup->old_duplex = -1;
	aup->phy_dev = phydev;

	netdev_info(dev, "attached PHY driver [%s] "
	       "(mii_bus:phy_addr=%s, irq=%d)\n",
	       phydev->drv->name, dev_name(&phydev->dev), phydev->irq);

	return 0;
}


/*
 * Buffer allocation/deallocation routines. The buffer descriptor returned
 * has the virtual and dma address of a buffer suitable for
 * both, receive and transmit operations.
 */
static struct db_dest *au1000_GetFreeDB(struct au1000_private *aup)
{
	struct db_dest *pDB;
	pDB = aup->pDBfree;

	if (pDB)
		aup->pDBfree = pDB->pnext;

	return pDB;
}

void au1000_ReleaseDB(struct au1000_private *aup, struct db_dest *pDB)
{
	struct db_dest *pDBfree = aup->pDBfree;
	if (pDBfree)
		pDBfree->pnext = pDB;
	aup->pDBfree = pDB;
}

static void au1000_reset_mac_unlocked(struct net_device *dev)
{
	struct au1000_private *const aup = netdev_priv(dev);
	int i;

	au1000_hard_stop(dev);

	writel(MAC_EN_CLOCK_ENABLE, aup->enable);
	au_sync_delay(2);
	writel(0, aup->enable);
	au_sync_delay(2);

	aup->tx_full = 0;
	for (i = 0; i < NUM_RX_DMA; i++) {
		/* reset control bits */
		aup->rx_dma_ring[i]->buff_stat &= ~0xf;
	}
	for (i = 0; i < NUM_TX_DMA; i++) {
		/* reset control bits */
		aup->tx_dma_ring[i]->buff_stat &= ~0xf;
	}

	aup->mac_enabled = 0;

}

static void au1000_reset_mac(struct net_device *dev)
{
	struct au1000_private *const aup = netdev_priv(dev);
	unsigned long flags;

	netif_dbg(aup, hw, dev, "reset mac, aup %x\n",
					(unsigned)aup);

	spin_lock_irqsave(&aup->lock, flags);

	au1000_reset_mac_unlocked(dev);

	spin_unlock_irqrestore(&aup->lock, flags);
}

/*
 * Setup the receive and transmit "rings".  These pointers are the addresses
 * of the rx and tx MAC DMA registers so they are fixed by the hardware --
 * these are not descriptors sitting in memory.
 */
static void
au1000_setup_hw_rings(struct au1000_private *aup, void __iomem *tx_base)
{
	int i;

	for (i = 0; i < NUM_RX_DMA; i++) {
		aup->rx_dma_ring[i] = (struct rx_dma *)
			(tx_base + 0x100 + sizeof(struct rx_dma) * i);
	}
	for (i = 0; i < NUM_TX_DMA; i++) {
		aup->tx_dma_ring[i] = (struct tx_dma *)
			(tx_base + sizeof(struct tx_dma) * i);
	}
}

/*
 * ethtool operations
 */

static int au1000_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct au1000_private *aup = netdev_priv(dev);

	if (aup->phy_dev)
		return phy_ethtool_gset(aup->phy_dev, cmd);

	return -EINVAL;
}

static int au1000_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct au1000_private *aup = netdev_priv(dev);

	if (!capable(CAP_NET_ADMIN))
		return -EPERM;

	if (aup->phy_dev)
		return phy_ethtool_sset(aup->phy_dev, cmd);

	return -EINVAL;
}

static void
au1000_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct au1000_private *aup = netdev_priv(dev);

	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	snprintf(info->bus_info, sizeof(info->bus_info), "%s %d", DRV_NAME,
		 aup->mac_id);
	info->regdump_len = 0;
}

static void au1000_set_msglevel(struct net_device *dev, u32 value)
{
	struct au1000_private *aup = netdev_priv(dev);
	aup->msg_enable = value;
}

static u32 au1000_get_msglevel(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);
	return aup->msg_enable;
}

static const struct ethtool_ops au1000_ethtool_ops = {
	.get_settings = au1000_get_settings,
	.set_settings = au1000_set_settings,
	.get_drvinfo = au1000_get_drvinfo,
	.get_link = ethtool_op_get_link,
	.get_msglevel = au1000_get_msglevel,
	.set_msglevel = au1000_set_msglevel,
};


/*
 * Initialize the interface.
 *
 * When the device powers up, the clocks are disabled and the
 * mac is in reset state.  When the interface is closed, we
 * do the same -- reset the device and disable the clocks to
 * conserve power. Thus, whenever au1000_init() is called,
 * the device should already be in reset state.
 */
static int au1000_init(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);
	unsigned long flags;
	int i;
	u32 control;

	netif_dbg(aup, hw, dev, "au1000_init\n");

	/* bring the device out of reset */
	au1000_enable_mac(dev, 1);

	spin_lock_irqsave(&aup->lock, flags);

	writel(0, &aup->mac->control);
	aup->tx_head = (aup->tx_dma_ring[0]->buff_stat & 0xC) >> 2;
	aup->tx_tail = aup->tx_head;
	aup->rx_head = (aup->rx_dma_ring[0]->buff_stat & 0xC) >> 2;

	writel(dev->dev_addr[5]<<8 | dev->dev_addr[4],
					&aup->mac->mac_addr_high);
	writel(dev->dev_addr[3]<<24 | dev->dev_addr[2]<<16 |
		dev->dev_addr[1]<<8 | dev->dev_addr[0],
					&aup->mac->mac_addr_low);


	for (i = 0; i < NUM_RX_DMA; i++)
		aup->rx_dma_ring[i]->buff_stat |= RX_DMA_ENABLE;

	au_sync();

	control = MAC_RX_ENABLE | MAC_TX_ENABLE;
#ifndef CONFIG_CPU_LITTLE_ENDIAN
	control |= MAC_BIG_ENDIAN;
#endif
	if (aup->phy_dev) {
		if (aup->phy_dev->link && (DUPLEX_FULL == aup->phy_dev->duplex))
			control |= MAC_FULL_DUPLEX;
		else
			control |= MAC_DISABLE_RX_OWN;
	} else { /* PHY-less op, assume full-duplex */
		control |= MAC_FULL_DUPLEX;
	}

	writel(control, &aup->mac->control);
	writel(0x8100, &aup->mac->vlan1_tag); /* activate vlan support */
	au_sync();

	spin_unlock_irqrestore(&aup->lock, flags);
	return 0;
}

static inline void au1000_update_rx_stats(struct net_device *dev, u32 status)
{
	struct net_device_stats *ps = &dev->stats;

	ps->rx_packets++;
	if (status & RX_MCAST_FRAME)
		ps->multicast++;

	if (status & RX_ERROR) {
		ps->rx_errors++;
		if (status & RX_MISSED_FRAME)
			ps->rx_missed_errors++;
		if (status & (RX_OVERLEN | RX_RUNT | RX_LEN_ERROR))
			ps->rx_length_errors++;
		if (status & RX_CRC_ERROR)
			ps->rx_crc_errors++;
		if (status & RX_COLL)
			ps->collisions++;
	} else
		ps->rx_bytes += status & RX_FRAME_LEN_MASK;

}

/*
 * Au1000 receive routine.
 */
static int au1000_rx(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);
	struct sk_buff *skb;
	struct rx_dma *prxd;
	u32 buff_stat, status;
	struct db_dest *pDB;
	u32	frmlen;

	netif_dbg(aup, rx_status, dev, "au1000_rx head %d\n", aup->rx_head);

	prxd = aup->rx_dma_ring[aup->rx_head];
	buff_stat = prxd->buff_stat;
	while (buff_stat & RX_T_DONE)  {
		status = prxd->status;
		pDB = aup->rx_db_inuse[aup->rx_head];
		au1000_update_rx_stats(dev, status);
		if (!(status & RX_ERROR))  {

			/* good frame */
			frmlen = (status & RX_FRAME_LEN_MASK);
			frmlen -= 4; /* Remove FCS */
			skb = netdev_alloc_skb(dev, frmlen + 2);
			if (skb == NULL) {
				dev->stats.rx_dropped++;
				continue;
			}
			skb_reserve(skb, 2);	/* 16 byte IP header align */
			skb_copy_to_linear_data(skb,
				(unsigned char *)pDB->vaddr, frmlen);
			skb_put(skb, frmlen);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);	/* pass the packet to upper layers */
		} else {
			if (au1000_debug > 4) {
				pr_err("rx_error(s):");
				if (status & RX_MISSED_FRAME)
					pr_cont(" miss");
				if (status & RX_WDOG_TIMER)
					pr_cont(" wdog");
				if (status & RX_RUNT)
					pr_cont(" runt");
				if (status & RX_OVERLEN)
					pr_cont(" overlen");
				if (status & RX_COLL)
					pr_cont(" coll");
				if (status & RX_MII_ERROR)
					pr_cont(" mii error");
				if (status & RX_CRC_ERROR)
					pr_cont(" crc error");
				if (status & RX_LEN_ERROR)
					pr_cont(" len error");
				if (status & RX_U_CNTRL_FRAME)
					pr_cont(" u control frame");
				pr_cont("\n");
			}
		}
		prxd->buff_stat = (u32)(pDB->dma_addr | RX_DMA_ENABLE);
		aup->rx_head = (aup->rx_head + 1) & (NUM_RX_DMA - 1);
		au_sync();

		/* next descriptor */
		prxd = aup->rx_dma_ring[aup->rx_head];
		buff_stat = prxd->buff_stat;
	}
	return 0;
}

static void au1000_update_tx_stats(struct net_device *dev, u32 status)
{
	struct au1000_private *aup = netdev_priv(dev);
	struct net_device_stats *ps = &dev->stats;

	if (status & TX_FRAME_ABORTED) {
		if (!aup->phy_dev || (DUPLEX_FULL == aup->phy_dev->duplex)) {
			if (status & (TX_JAB_TIMEOUT | TX_UNDERRUN)) {
				/* any other tx errors are only valid
				 * in half duplex mode
				 */
				ps->tx_errors++;
				ps->tx_aborted_errors++;
			}
		} else {
			ps->tx_errors++;
			ps->tx_aborted_errors++;
			if (status & (TX_NO_CARRIER | TX_LOSS_CARRIER))
				ps->tx_carrier_errors++;
		}
	}
}

/*
 * Called from the interrupt service routine to acknowledge
 * the TX DONE bits.  This is a must if the irq is setup as
 * edge triggered.
 */
static void au1000_tx_ack(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);
	struct tx_dma *ptxd;

	ptxd = aup->tx_dma_ring[aup->tx_tail];

	while (ptxd->buff_stat & TX_T_DONE) {
		au1000_update_tx_stats(dev, ptxd->status);
		ptxd->buff_stat &= ~TX_T_DONE;
		ptxd->len = 0;
		au_sync();

		aup->tx_tail = (aup->tx_tail + 1) & (NUM_TX_DMA - 1);
		ptxd = aup->tx_dma_ring[aup->tx_tail];

		if (aup->tx_full) {
			aup->tx_full = 0;
			netif_wake_queue(dev);
		}
	}
}

/*
 * Au1000 interrupt service routine.
 */
static irqreturn_t au1000_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;

	/* Handle RX interrupts first to minimize chance of overrun */

	au1000_rx(dev);
	au1000_tx_ack(dev);
	return IRQ_RETVAL(1);
}

static int au1000_open(struct net_device *dev)
{
	int retval;
	struct au1000_private *aup = netdev_priv(dev);

	netif_dbg(aup, drv, dev, "open: dev=%p\n", dev);

	retval = request_irq(dev->irq, au1000_interrupt, 0,
					dev->name, dev);
	if (retval) {
		netdev_err(dev, "unable to get IRQ %d\n", dev->irq);
		return retval;
	}

	retval = au1000_init(dev);
	if (retval) {
		netdev_err(dev, "error in au1000_init\n");
		free_irq(dev->irq, dev);
		return retval;
	}

	if (aup->phy_dev) {
		/* cause the PHY state machine to schedule a link state check */
		aup->phy_dev->state = PHY_CHANGELINK;
		phy_start(aup->phy_dev);
	}

	netif_start_queue(dev);

	netif_dbg(aup, drv, dev, "open: Initialization done.\n");

	return 0;
}

static int au1000_close(struct net_device *dev)
{
	unsigned long flags;
	struct au1000_private *const aup = netdev_priv(dev);

	netif_dbg(aup, drv, dev, "close: dev=%p\n", dev);

	if (aup->phy_dev)
		phy_stop(aup->phy_dev);

	spin_lock_irqsave(&aup->lock, flags);

	au1000_reset_mac_unlocked(dev);

	/* stop the device */
	netif_stop_queue(dev);

	/* disable the interrupt */
	free_irq(dev->irq, dev);
	spin_unlock_irqrestore(&aup->lock, flags);

	return 0;
}

/*
 * Au1000 transmit routine.
 */
static netdev_tx_t au1000_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);
	struct net_device_stats *ps = &dev->stats;
	struct tx_dma *ptxd;
	u32 buff_stat;
	struct db_dest *pDB;
	int i;

	netif_dbg(aup, tx_queued, dev, "tx: aup %x len=%d, data=%p, head %d\n",
				(unsigned)aup, skb->len,
				skb->data, aup->tx_head);

	ptxd = aup->tx_dma_ring[aup->tx_head];
	buff_stat = ptxd->buff_stat;
	if (buff_stat & TX_DMA_ENABLE) {
		/* We've wrapped around and the transmitter is still busy */
		netif_stop_queue(dev);
		aup->tx_full = 1;
		return NETDEV_TX_BUSY;
	} else if (buff_stat & TX_T_DONE) {
		au1000_update_tx_stats(dev, ptxd->status);
		ptxd->len = 0;
	}

	if (aup->tx_full) {
		aup->tx_full = 0;
		netif_wake_queue(dev);
	}

	pDB = aup->tx_db_inuse[aup->tx_head];
	skb_copy_from_linear_data(skb, (void *)pDB->vaddr, skb->len);
	if (skb->len < ETH_ZLEN) {
		for (i = skb->len; i < ETH_ZLEN; i++)
			((char *)pDB->vaddr)[i] = 0;

		ptxd->len = ETH_ZLEN;
	} else
		ptxd->len = skb->len;

	ps->tx_packets++;
	ps->tx_bytes += ptxd->len;

	ptxd->buff_stat = pDB->dma_addr | TX_DMA_ENABLE;
	au_sync();
	dev_kfree_skb(skb);
	aup->tx_head = (aup->tx_head + 1) & (NUM_TX_DMA - 1);
	return NETDEV_TX_OK;
}

/*
 * The Tx ring has been full longer than the watchdog timeout
 * value. The transmitter must be hung?
 */
static void au1000_tx_timeout(struct net_device *dev)
{
	netdev_err(dev, "au1000_tx_timeout: dev=%p\n", dev);
	au1000_reset_mac(dev);
	au1000_init(dev);
	dev->trans_start = jiffies; /* prevent tx timeout */
	netif_wake_queue(dev);
}

static void au1000_multicast_list(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);
	u32 reg;

	netif_dbg(aup, drv, dev, "%s: flags=%x\n", __func__, dev->flags);
	reg = readl(&aup->mac->control);
	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		reg |= MAC_PROMISCUOUS;
	} else if ((dev->flags & IFF_ALLMULTI)  ||
			   netdev_mc_count(dev) > MULTICAST_FILTER_LIMIT) {
		reg |= MAC_PASS_ALL_MULTI;
		reg &= ~MAC_PROMISCUOUS;
		netdev_info(dev, "Pass all multicast\n");
	} else {
		struct netdev_hw_addr *ha;
		u32 mc_filter[2];	/* Multicast hash filter */

		mc_filter[1] = mc_filter[0] = 0;
		netdev_for_each_mc_addr(ha, dev)
			set_bit(ether_crc(ETH_ALEN, ha->addr)>>26,
					(long *)mc_filter);
		writel(mc_filter[1], &aup->mac->multi_hash_high);
		writel(mc_filter[0], &aup->mac->multi_hash_low);
		reg &= ~MAC_PROMISCUOUS;
		reg |= MAC_HASH_MODE;
	}
	writel(reg, &aup->mac->control);
}

static int au1000_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct au1000_private *aup = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	if (!aup->phy_dev)
		return -EINVAL; /* PHY not controllable */

	return phy_mii_ioctl(aup->phy_dev, rq, cmd);
}

static const struct net_device_ops au1000_netdev_ops = {
	.ndo_open		= au1000_open,
	.ndo_stop		= au1000_close,
	.ndo_start_xmit		= au1000_tx,
	.ndo_set_rx_mode	= au1000_multicast_list,
	.ndo_do_ioctl		= au1000_ioctl,
	.ndo_tx_timeout		= au1000_tx_timeout,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static int au1000_probe(struct platform_device *pdev)
{
	static unsigned version_printed;
	struct au1000_private *aup = NULL;
	struct au1000_eth_platform_data *pd;
	struct net_device *dev = NULL;
	struct db_dest *pDB, *pDBfree;
	int irq, i, err = 0;
	struct resource *base, *macen, *macdma;

	base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!base) {
		dev_err(&pdev->dev, "failed to retrieve base register\n");
		err = -ENODEV;
		goto out;
	}

	macen = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!macen) {
		dev_err(&pdev->dev, "failed to retrieve MAC Enable register\n");
		err = -ENODEV;
		goto out;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to retrieve IRQ\n");
		err = -ENODEV;
		goto out;
	}

	macdma = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!macdma) {
		dev_err(&pdev->dev, "failed to retrieve MACDMA registers\n");
		err = -ENODEV;
		goto out;
	}

	if (!request_mem_region(base->start, resource_size(base),
							pdev->name)) {
		dev_err(&pdev->dev, "failed to request memory region for base registers\n");
		err = -ENXIO;
		goto out;
	}

	if (!request_mem_region(macen->start, resource_size(macen),
							pdev->name)) {
		dev_err(&pdev->dev, "failed to request memory region for MAC enable register\n");
		err = -ENXIO;
		goto err_request;
	}

	if (!request_mem_region(macdma->start, resource_size(macdma),
							pdev->name)) {
		dev_err(&pdev->dev, "failed to request MACDMA memory region\n");
		err = -ENXIO;
		goto err_macdma;
	}

	dev = alloc_etherdev(sizeof(struct au1000_private));
	if (!dev) {
		err = -ENOMEM;
		goto err_alloc;
	}

	SET_NETDEV_DEV(dev, &pdev->dev);
	platform_set_drvdata(pdev, dev);
	aup = netdev_priv(dev);

	spin_lock_init(&aup->lock);
	aup->msg_enable = (au1000_debug < 4 ?
				AU1000_DEF_MSG_ENABLE : au1000_debug);

	/* Allocate the data buffers
	 * Snooping works fine with eth on all au1xxx
	 */
	aup->vaddr = (u32)dma_alloc_noncoherent(NULL, MAX_BUF_SIZE *
						(NUM_TX_BUFFS + NUM_RX_BUFFS),
						&aup->dma_addr,	0);
	if (!aup->vaddr) {
		dev_err(&pdev->dev, "failed to allocate data buffers\n");
		err = -ENOMEM;
		goto err_vaddr;
	}

	/* aup->mac is the base address of the MAC's registers */
	aup->mac = (struct mac_reg *)
			ioremap_nocache(base->start, resource_size(base));
	if (!aup->mac) {
		dev_err(&pdev->dev, "failed to ioremap MAC registers\n");
		err = -ENXIO;
		goto err_remap1;
	}

	/* Setup some variables for quick register address access */
	aup->enable = (u32 *)ioremap_nocache(macen->start,
						resource_size(macen));
	if (!aup->enable) {
		dev_err(&pdev->dev, "failed to ioremap MAC enable register\n");
		err = -ENXIO;
		goto err_remap2;
	}
	aup->mac_id = pdev->id;

	aup->macdma = ioremap_nocache(macdma->start, resource_size(macdma));
	if (!aup->macdma) {
		dev_err(&pdev->dev, "failed to ioremap MACDMA registers\n");
		err = -ENXIO;
		goto err_remap3;
	}

	au1000_setup_hw_rings(aup, aup->macdma);

	writel(0, aup->enable);
	aup->mac_enabled = 0;

	pd = dev_get_platdata(&pdev->dev);
	if (!pd) {
		dev_info(&pdev->dev, "no platform_data passed,"
					" PHY search on MAC0\n");
		aup->phy1_search_mac0 = 1;
	} else {
		if (is_valid_ether_addr(pd->mac)) {
			memcpy(dev->dev_addr, pd->mac, 6);
		} else {
			/* Set a random MAC since no valid provided by platform_data. */
			eth_hw_addr_random(dev);
		}

		aup->phy_static_config = pd->phy_static_config;
		aup->phy_search_highest_addr = pd->phy_search_highest_addr;
		aup->phy1_search_mac0 = pd->phy1_search_mac0;
		aup->phy_addr = pd->phy_addr;
		aup->phy_busid = pd->phy_busid;
		aup->phy_irq = pd->phy_irq;
	}

	if (aup->phy_busid && aup->phy_busid > 0) {
		dev_err(&pdev->dev, "MAC0-associated PHY attached 2nd MACs MII bus not supported yet\n");
		err = -ENODEV;
		goto err_mdiobus_alloc;
	}

	aup->mii_bus = mdiobus_alloc();
	if (aup->mii_bus == NULL) {
		dev_err(&pdev->dev, "failed to allocate mdiobus structure\n");
		err = -ENOMEM;
		goto err_mdiobus_alloc;
	}

	aup->mii_bus->priv = dev;
	aup->mii_bus->read = au1000_mdiobus_read;
	aup->mii_bus->write = au1000_mdiobus_write;
	aup->mii_bus->reset = au1000_mdiobus_reset;
	aup->mii_bus->name = "au1000_eth_mii";
	snprintf(aup->mii_bus->id, MII_BUS_ID_SIZE, "%s-%x",
		pdev->name, aup->mac_id);
	aup->mii_bus->irq = kmalloc(sizeof(int)*PHY_MAX_ADDR, GFP_KERNEL);
	if (aup->mii_bus->irq == NULL) {
		err = -ENOMEM;
		goto err_out;
	}

	for (i = 0; i < PHY_MAX_ADDR; ++i)
		aup->mii_bus->irq[i] = PHY_POLL;
	/* if known, set corresponding PHY IRQs */
	if (aup->phy_static_config)
		if (aup->phy_irq && aup->phy_busid == aup->mac_id)
			aup->mii_bus->irq[aup->phy_addr] = aup->phy_irq;

	err = mdiobus_register(aup->mii_bus);
	if (err) {
		dev_err(&pdev->dev, "failed to register MDIO bus\n");
		goto err_mdiobus_reg;
	}

	err = au1000_mii_probe(dev);
	if (err != 0)
		goto err_out;

	pDBfree = NULL;
	/* setup the data buffer descriptors and attach a buffer to each one */
	pDB = aup->db;
	for (i = 0; i < (NUM_TX_BUFFS+NUM_RX_BUFFS); i++) {
		pDB->pnext = pDBfree;
		pDBfree = pDB;
		pDB->vaddr = (u32 *)((unsigned)aup->vaddr + MAX_BUF_SIZE*i);
		pDB->dma_addr = (dma_addr_t)virt_to_bus(pDB->vaddr);
		pDB++;
	}
	aup->pDBfree = pDBfree;

	err = -ENODEV;
	for (i = 0; i < NUM_RX_DMA; i++) {
		pDB = au1000_GetFreeDB(aup);
		if (!pDB)
			goto err_out;

		aup->rx_dma_ring[i]->buff_stat = (unsigned)pDB->dma_addr;
		aup->rx_db_inuse[i] = pDB;
	}

	err = -ENODEV;
	for (i = 0; i < NUM_TX_DMA; i++) {
		pDB = au1000_GetFreeDB(aup);
		if (!pDB)
			goto err_out;

		aup->tx_dma_ring[i]->buff_stat = (unsigned)pDB->dma_addr;
		aup->tx_dma_ring[i]->len = 0;
		aup->tx_db_inuse[i] = pDB;
	}

	dev->base_addr = base->start;
	dev->irq = irq;
	dev->netdev_ops = &au1000_netdev_ops;
	SET_ETHTOOL_OPS(dev, &au1000_ethtool_ops);
	dev->watchdog_timeo = ETH_TX_TIMEOUT;

	/*
	 * The boot code uses the ethernet controller, so reset it to start
	 * fresh.  au1000_init() expects that the device is in reset state.
	 */
	au1000_reset_mac(dev);

	err = register_netdev(dev);
	if (err) {
		netdev_err(dev, "Cannot register net device, aborting.\n");
		goto err_out;
	}

	netdev_info(dev, "Au1xx0 Ethernet found at 0x%lx, irq %d\n",
			(unsigned long)base->start, irq);
	if (version_printed++ == 0)
		pr_info("%s version %s %s\n",
					DRV_NAME, DRV_VERSION, DRV_AUTHOR);

	return 0;

err_out:
	if (aup->mii_bus != NULL)
		mdiobus_unregister(aup->mii_bus);

	/* here we should have a valid dev plus aup-> register addresses
	 * so we can reset the mac properly.
	 */
	au1000_reset_mac(dev);

	for (i = 0; i < NUM_RX_DMA; i++) {
		if (aup->rx_db_inuse[i])
			au1000_ReleaseDB(aup, aup->rx_db_inuse[i]);
	}
	for (i = 0; i < NUM_TX_DMA; i++) {
		if (aup->tx_db_inuse[i])
			au1000_ReleaseDB(aup, aup->tx_db_inuse[i]);
	}
err_mdiobus_reg:
	mdiobus_free(aup->mii_bus);
err_mdiobus_alloc:
	iounmap(aup->macdma);
err_remap3:
	iounmap(aup->enable);
err_remap2:
	iounmap(aup->mac);
err_remap1:
	dma_free_noncoherent(NULL, MAX_BUF_SIZE * (NUM_TX_BUFFS + NUM_RX_BUFFS),
			     (void *)aup->vaddr, aup->dma_addr);
err_vaddr:
	free_netdev(dev);
err_alloc:
	release_mem_region(macdma->start, resource_size(macdma));
err_macdma:
	release_mem_region(macen->start, resource_size(macen));
err_request:
	release_mem_region(base->start, resource_size(base));
out:
	return err;
}

static int au1000_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct au1000_private *aup = netdev_priv(dev);
	int i;
	struct resource *base, *macen;

	unregister_netdev(dev);
	mdiobus_unregister(aup->mii_bus);
	mdiobus_free(aup->mii_bus);

	for (i = 0; i < NUM_RX_DMA; i++)
		if (aup->rx_db_inuse[i])
			au1000_ReleaseDB(aup, aup->rx_db_inuse[i]);

	for (i = 0; i < NUM_TX_DMA; i++)
		if (aup->tx_db_inuse[i])
			au1000_ReleaseDB(aup, aup->tx_db_inuse[i]);

	dma_free_noncoherent(NULL, MAX_BUF_SIZE *
			(NUM_TX_BUFFS + NUM_RX_BUFFS),
			(void *)aup->vaddr, aup->dma_addr);

	iounmap(aup->macdma);
	iounmap(aup->mac);
	iounmap(aup->enable);

	base = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	release_mem_region(base->start, resource_size(base));

	base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(base->start, resource_size(base));

	macen = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	release_mem_region(macen->start, resource_size(macen));

	free_netdev(dev);

	return 0;
}

static struct platform_driver au1000_eth_driver = {
	.probe  = au1000_probe,
	.remove = au1000_remove,
	.driver = {
		.name   = "au1000-eth",
		.owner  = THIS_MODULE,
	},
};

module_platform_driver(au1000_eth_driver);

MODULE_ALIAS("platform:au1000-eth");
