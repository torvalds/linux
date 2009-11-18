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
 *         	ppopov@mvista.com or source@mvista.com
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

#include <asm/cpu.h>
#include <asm/mipsregs.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/processor.h>

#include <au1000.h>
#include <prom.h>

#include "au1000_eth.h"

#ifdef AU1000_ETH_DEBUG
static int au1000_debug = 5;
#else
static int au1000_debug = 3;
#endif

#define DRV_NAME	"au1000_eth"
#define DRV_VERSION	"1.6"
#define DRV_AUTHOR	"Pete Popov <ppopov@embeddedalley.com>"
#define DRV_DESC	"Au1xxx on-chip Ethernet driver"

MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
MODULE_LICENSE("GPL");

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

/* These addresses are only used if yamon doesn't tell us what
 * the mac address is, and the mac address is not passed on the
 * command line.
 */
static unsigned char au1000_mac_addr[6] __devinitdata = {
	0x00, 0x50, 0xc2, 0x0c, 0x30, 0x00
};

struct au1000_private *au_macs[NUM_ETH_INTERFACES];

/*
 * board-specific configurations
 *
 * PHY detection algorithm
 *
 * If AU1XXX_PHY_STATIC_CONFIG is undefined, the PHY setup is
 * autodetected:
 *
 * mii_probe() first searches the current MAC's MII bus for a PHY,
 * selecting the first (or last, if AU1XXX_PHY_SEARCH_HIGHEST_ADDR is
 * defined) PHY address not already claimed by another netdev.
 *
 * If nothing was found that way when searching for the 2nd ethernet
 * controller's PHY and AU1XXX_PHY1_SEARCH_ON_MAC0 is defined, then
 * the first MII bus is searched as well for an unclaimed PHY; this is
 * needed in case of a dual-PHY accessible only through the MAC0's MII
 * bus.
 *
 * Finally, if no PHY is found, then the corresponding ethernet
 * controller is not registered to the network subsystem.
 */

/* autodetection defaults */
#undef  AU1XXX_PHY_SEARCH_HIGHEST_ADDR
#define AU1XXX_PHY1_SEARCH_ON_MAC0

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

#if defined(CONFIG_MIPS_BOSPORUS)
/*
 * Micrel/Kendin 5 port switch attached to MAC0,
 * MAC0 is associated with PHY address 5 (== WAN port)
 * MAC1 is not associated with any PHY, since it's connected directly
 * to the switch.
 * no interrupts are used
 */
# define AU1XXX_PHY_STATIC_CONFIG

# define AU1XXX_PHY0_ADDR  5
# define AU1XXX_PHY0_BUSID 0
#  undef AU1XXX_PHY0_IRQ

#  undef AU1XXX_PHY1_ADDR
#  undef AU1XXX_PHY1_BUSID
#  undef AU1XXX_PHY1_IRQ
#endif

#if defined(AU1XXX_PHY0_BUSID) && (AU1XXX_PHY0_BUSID > 0)
# error MAC0-associated PHY attached 2nd MACs MII bus not supported yet
#endif

static void enable_mac(struct net_device *dev, int force_reset)
{
	unsigned long flags;
	struct au1000_private *aup = netdev_priv(dev);

	spin_lock_irqsave(&aup->lock, flags);

	if(force_reset || (!aup->mac_enabled)) {
		*aup->enable = MAC_EN_CLOCK_ENABLE;
		au_sync_delay(2);
		*aup->enable = (MAC_EN_RESET0 | MAC_EN_RESET1 | MAC_EN_RESET2
				| MAC_EN_CLOCK_ENABLE);
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
	volatile u32 *const mii_control_reg = &aup->mac->mii_control;
	volatile u32 *const mii_data_reg = &aup->mac->mii_data;
	u32 timedout = 20;
	u32 mii_control;

	while (*mii_control_reg & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			printk(KERN_ERR "%s: read_MII busy timeout!!\n",
					dev->name);
			return -1;
		}
	}

	mii_control = MAC_SET_MII_SELECT_REG(reg) |
		MAC_SET_MII_SELECT_PHY(phy_addr) | MAC_MII_READ;

	*mii_control_reg = mii_control;

	timedout = 20;
	while (*mii_control_reg & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			printk(KERN_ERR "%s: mdio_read busy timeout!!\n",
					dev->name);
			return -1;
		}
	}
	return (int)*mii_data_reg;
}

static void au1000_mdio_write(struct net_device *dev, int phy_addr,
			      int reg, u16 value)
{
	struct au1000_private *aup = netdev_priv(dev);
	volatile u32 *const mii_control_reg = &aup->mac->mii_control;
	volatile u32 *const mii_data_reg = &aup->mac->mii_data;
	u32 timedout = 20;
	u32 mii_control;

	while (*mii_control_reg & MAC_MII_BUSY) {
		mdelay(1);
		if (--timedout == 0) {
			printk(KERN_ERR "%s: mdio_write busy timeout!!\n",
					dev->name);
			return;
		}
	}

	mii_control = MAC_SET_MII_SELECT_REG(reg) |
		MAC_SET_MII_SELECT_PHY(phy_addr) | MAC_MII_WRITE;

	*mii_data_reg = value;
	*mii_control_reg = mii_control;
}

static int au1000_mdiobus_read(struct mii_bus *bus, int phy_addr, int regnum)
{
	/* WARNING: bus->phy_map[phy_addr].attached_dev == dev does
	 * _NOT_ hold (e.g. when PHY is accessed through other MAC's MII bus) */
	struct net_device *const dev = bus->priv;

	enable_mac(dev, 0); /* make sure the MAC associated with this
			     * mii_bus is enabled */
	return au1000_mdio_read(dev, phy_addr, regnum);
}

static int au1000_mdiobus_write(struct mii_bus *bus, int phy_addr, int regnum,
				u16 value)
{
	struct net_device *const dev = bus->priv;

	enable_mac(dev, 0); /* make sure the MAC associated with this
			     * mii_bus is enabled */
	au1000_mdio_write(dev, phy_addr, regnum, value);
	return 0;
}

static int au1000_mdiobus_reset(struct mii_bus *bus)
{
	struct net_device *const dev = bus->priv;

	enable_mac(dev, 0); /* make sure the MAC associated with this
			     * mii_bus is enabled */
	return 0;
}

static void hard_stop(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);

	if (au1000_debug > 4)
		printk(KERN_INFO "%s: hard stop\n", dev->name);

	aup->mac->control &= ~(MAC_RX_ENABLE | MAC_TX_ENABLE);
	au_sync_delay(10);
}

static void enable_rx_tx(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);

	if (au1000_debug > 4)
		printk(KERN_INFO "%s: enable_rx_tx\n", dev->name);

	aup->mac->control |= (MAC_RX_ENABLE | MAC_TX_ENABLE);
	au_sync_delay(10);
}

static void
au1000_adjust_link(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);
	struct phy_device *phydev = aup->phy_dev;
	unsigned long flags;

	int status_change = 0;

	BUG_ON(!aup->phy_dev);

	spin_lock_irqsave(&aup->lock, flags);

	if (phydev->link && (aup->old_speed != phydev->speed)) {
		// speed changed

		switch(phydev->speed) {
		case SPEED_10:
		case SPEED_100:
			break;
		default:
			printk(KERN_WARNING
			       "%s: Speed (%d) is not 10/100 ???\n",
			       dev->name, phydev->speed);
			break;
		}

		aup->old_speed = phydev->speed;

		status_change = 1;
	}

	if (phydev->link && (aup->old_duplex != phydev->duplex)) {
		// duplex mode changed

		/* switching duplex mode requires to disable rx and tx! */
		hard_stop(dev);

		if (DUPLEX_FULL == phydev->duplex)
			aup->mac->control = ((aup->mac->control
					     | MAC_FULL_DUPLEX)
					     & ~MAC_DISABLE_RX_OWN);
		else
			aup->mac->control = ((aup->mac->control
					      & ~MAC_FULL_DUPLEX)
					     | MAC_DISABLE_RX_OWN);
		au_sync_delay(1);

		enable_rx_tx(dev);
		aup->old_duplex = phydev->duplex;

		status_change = 1;
	}

	if(phydev->link != aup->old_link) {
		// link state changed

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
			printk(KERN_INFO "%s: link up (%d/%s)\n",
			       dev->name, phydev->speed,
			       DUPLEX_FULL == phydev->duplex ? "Full" : "Half");
		else
			printk(KERN_INFO "%s: link down\n", dev->name);
	}
}

static int mii_probe (struct net_device *dev)
{
	struct au1000_private *const aup = netdev_priv(dev);
	struct phy_device *phydev = NULL;

#if defined(AU1XXX_PHY_STATIC_CONFIG)
	BUG_ON(aup->mac_id < 0 || aup->mac_id > 1);

	if(aup->mac_id == 0) { /* get PHY0 */
# if defined(AU1XXX_PHY0_ADDR)
		phydev = au_macs[AU1XXX_PHY0_BUSID]->mii_bus->phy_map[AU1XXX_PHY0_ADDR];
# else
		printk (KERN_INFO DRV_NAME ":%s: using PHY-less setup\n",
			dev->name);
		return 0;
# endif /* defined(AU1XXX_PHY0_ADDR) */
	} else if (aup->mac_id == 1) { /* get PHY1 */
# if defined(AU1XXX_PHY1_ADDR)
		phydev = au_macs[AU1XXX_PHY1_BUSID]->mii_bus->phy_map[AU1XXX_PHY1_ADDR];
# else
		printk (KERN_INFO DRV_NAME ":%s: using PHY-less setup\n",
			dev->name);
		return 0;
# endif /* defined(AU1XXX_PHY1_ADDR) */
	}

#else /* defined(AU1XXX_PHY_STATIC_CONFIG) */
	int phy_addr;

	/* find the first (lowest address) PHY on the current MAC's MII bus */
	for (phy_addr = 0; phy_addr < PHY_MAX_ADDR; phy_addr++)
		if (aup->mii_bus->phy_map[phy_addr]) {
			phydev = aup->mii_bus->phy_map[phy_addr];
# if !defined(AU1XXX_PHY_SEARCH_HIGHEST_ADDR)
			break; /* break out with first one found */
# endif
		}

# if defined(AU1XXX_PHY1_SEARCH_ON_MAC0)
	/* try harder to find a PHY */
	if (!phydev && (aup->mac_id == 1)) {
		/* no PHY found, maybe we have a dual PHY? */
		printk (KERN_INFO DRV_NAME ": no PHY found on MAC1, "
			"let's see if it's attached to MAC0...\n");

		BUG_ON(!au_macs[0]);

		/* find the first (lowest address) non-attached PHY on
		 * the MAC0 MII bus */
		for (phy_addr = 0; phy_addr < PHY_MAX_ADDR; phy_addr++) {
			struct phy_device *const tmp_phydev =
				au_macs[0]->mii_bus->phy_map[phy_addr];

			if (!tmp_phydev)
				continue; /* no PHY here... */

			if (tmp_phydev->attached_dev)
				continue; /* already claimed by MAC0 */

			phydev = tmp_phydev;
			break; /* found it */
		}
	}
# endif /* defined(AU1XXX_PHY1_SEARCH_OTHER_BUS) */

#endif /* defined(AU1XXX_PHY_STATIC_CONFIG) */
	if (!phydev) {
		printk (KERN_ERR DRV_NAME ":%s: no PHY found\n", dev->name);
		return -1;
	}

	/* now we are supposed to have a proper phydev, to attach to... */
	BUG_ON(phydev->attached_dev);

	phydev = phy_connect(dev, dev_name(&phydev->dev), &au1000_adjust_link,
			0, PHY_INTERFACE_MODE_MII);

	if (IS_ERR(phydev)) {
		printk(KERN_ERR "%s: Could not attach to PHY\n", dev->name);
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

	printk(KERN_INFO "%s: attached PHY driver [%s] "
	       "(mii_bus:phy_addr=%s, irq=%d)\n", dev->name,
	       phydev->drv->name, dev_name(&phydev->dev), phydev->irq);

	return 0;
}


/*
 * Buffer allocation/deallocation routines. The buffer descriptor returned
 * has the virtual and dma address of a buffer suitable for
 * both, receive and transmit operations.
 */
static db_dest_t *GetFreeDB(struct au1000_private *aup)
{
	db_dest_t *pDB;
	pDB = aup->pDBfree;

	if (pDB) {
		aup->pDBfree = pDB->pnext;
	}
	return pDB;
}

void ReleaseDB(struct au1000_private *aup, db_dest_t *pDB)
{
	db_dest_t *pDBfree = aup->pDBfree;
	if (pDBfree)
		pDBfree->pnext = pDB;
	aup->pDBfree = pDB;
}

static void reset_mac_unlocked(struct net_device *dev)
{
	struct au1000_private *const aup = netdev_priv(dev);
	int i;

	hard_stop(dev);

	*aup->enable = MAC_EN_CLOCK_ENABLE;
	au_sync_delay(2);
	*aup->enable = 0;
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

static void reset_mac(struct net_device *dev)
{
	struct au1000_private *const aup = netdev_priv(dev);
	unsigned long flags;

	if (au1000_debug > 4)
		printk(KERN_INFO "%s: reset mac, aup %x\n",
		       dev->name, (unsigned)aup);

	spin_lock_irqsave(&aup->lock, flags);

	reset_mac_unlocked (dev);

	spin_unlock_irqrestore(&aup->lock, flags);
}

/*
 * Setup the receive and transmit "rings".  These pointers are the addresses
 * of the rx and tx MAC DMA registers so they are fixed by the hardware --
 * these are not descriptors sitting in memory.
 */
static void
setup_hw_rings(struct au1000_private *aup, u32 rx_base, u32 tx_base)
{
	int i;

	for (i = 0; i < NUM_RX_DMA; i++) {
		aup->rx_dma_ring[i] =
			(volatile rx_dma_t *) (rx_base + sizeof(rx_dma_t)*i);
	}
	for (i = 0; i < NUM_TX_DMA; i++) {
		aup->tx_dma_ring[i] =
			(volatile tx_dma_t *) (tx_base + sizeof(tx_dma_t)*i);
	}
}

static struct {
	u32 base_addr;
	u32 macen_addr;
	int irq;
	struct net_device *dev;
} iflist[2] = {
#ifdef CONFIG_SOC_AU1000
	{AU1000_ETH0_BASE, AU1000_MAC0_ENABLE, AU1000_MAC0_DMA_INT},
	{AU1000_ETH1_BASE, AU1000_MAC1_ENABLE, AU1000_MAC1_DMA_INT}
#endif
#ifdef CONFIG_SOC_AU1100
	{AU1100_ETH0_BASE, AU1100_MAC0_ENABLE, AU1100_MAC0_DMA_INT}
#endif
#ifdef CONFIG_SOC_AU1500
	{AU1500_ETH0_BASE, AU1500_MAC0_ENABLE, AU1500_MAC0_DMA_INT},
	{AU1500_ETH1_BASE, AU1500_MAC1_ENABLE, AU1500_MAC1_DMA_INT}
#endif
#ifdef CONFIG_SOC_AU1550
	{AU1550_ETH0_BASE, AU1550_MAC0_ENABLE, AU1550_MAC0_DMA_INT},
	{AU1550_ETH1_BASE, AU1550_MAC1_ENABLE, AU1550_MAC1_DMA_INT}
#endif
};

static int num_ifs;

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

	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	info->fw_version[0] = '\0';
	sprintf(info->bus_info, "%s %d", DRV_NAME, aup->mac_id);
	info->regdump_len = 0;
}

static const struct ethtool_ops au1000_ethtool_ops = {
	.get_settings = au1000_get_settings,
	.set_settings = au1000_set_settings,
	.get_drvinfo = au1000_get_drvinfo,
	.get_link = ethtool_op_get_link,
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

	if (au1000_debug > 4)
		printk("%s: au1000_init\n", dev->name);

	/* bring the device out of reset */
	enable_mac(dev, 1);

	spin_lock_irqsave(&aup->lock, flags);

	aup->mac->control = 0;
	aup->tx_head = (aup->tx_dma_ring[0]->buff_stat & 0xC) >> 2;
	aup->tx_tail = aup->tx_head;
	aup->rx_head = (aup->rx_dma_ring[0]->buff_stat & 0xC) >> 2;

	aup->mac->mac_addr_high = dev->dev_addr[5]<<8 | dev->dev_addr[4];
	aup->mac->mac_addr_low = dev->dev_addr[3]<<24 | dev->dev_addr[2]<<16 |
		dev->dev_addr[1]<<8 | dev->dev_addr[0];

	for (i = 0; i < NUM_RX_DMA; i++) {
		aup->rx_dma_ring[i]->buff_stat |= RX_DMA_ENABLE;
	}
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

	aup->mac->control = control;
	aup->mac->vlan1_tag = 0x8100; /* activate vlan support */
	au_sync();

	spin_unlock_irqrestore(&aup->lock, flags);
	return 0;
}

static inline void update_rx_stats(struct net_device *dev, u32 status)
{
	struct au1000_private *aup = netdev_priv(dev);
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
	}
	else
		ps->rx_bytes += status & RX_FRAME_LEN_MASK;

}

/*
 * Au1000 receive routine.
 */
static int au1000_rx(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);
	struct sk_buff *skb;
	volatile rx_dma_t *prxd;
	u32 buff_stat, status;
	db_dest_t *pDB;
	u32	frmlen;

	if (au1000_debug > 5)
		printk("%s: au1000_rx head %d\n", dev->name, aup->rx_head);

	prxd = aup->rx_dma_ring[aup->rx_head];
	buff_stat = prxd->buff_stat;
	while (buff_stat & RX_T_DONE)  {
		status = prxd->status;
		pDB = aup->rx_db_inuse[aup->rx_head];
		update_rx_stats(dev, status);
		if (!(status & RX_ERROR))  {

			/* good frame */
			frmlen = (status & RX_FRAME_LEN_MASK);
			frmlen -= 4; /* Remove FCS */
			skb = dev_alloc_skb(frmlen + 2);
			if (skb == NULL) {
				printk(KERN_ERR
				       "%s: Memory squeeze, dropping packet.\n",
				       dev->name);
				dev->stats.rx_dropped++;
				continue;
			}
			skb_reserve(skb, 2);	/* 16 byte IP header align */
			skb_copy_to_linear_data(skb,
				(unsigned char *)pDB->vaddr, frmlen);
			skb_put(skb, frmlen);
			skb->protocol = eth_type_trans(skb, dev);
			netif_rx(skb);	/* pass the packet to upper layers */
		}
		else {
			if (au1000_debug > 4) {
				if (status & RX_MISSED_FRAME)
					printk("rx miss\n");
				if (status & RX_WDOG_TIMER)
					printk("rx wdog\n");
				if (status & RX_RUNT)
					printk("rx runt\n");
				if (status & RX_OVERLEN)
					printk("rx overlen\n");
				if (status & RX_COLL)
					printk("rx coll\n");
				if (status & RX_MII_ERROR)
					printk("rx mii error\n");
				if (status & RX_CRC_ERROR)
					printk("rx crc error\n");
				if (status & RX_LEN_ERROR)
					printk("rx len error\n");
				if (status & RX_U_CNTRL_FRAME)
					printk("rx u control frame\n");
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

static void update_tx_stats(struct net_device *dev, u32 status)
{
	struct au1000_private *aup = netdev_priv(dev);
	struct net_device_stats *ps = &dev->stats;

	if (status & TX_FRAME_ABORTED) {
		if (!aup->phy_dev || (DUPLEX_FULL == aup->phy_dev->duplex)) {
			if (status & (TX_JAB_TIMEOUT | TX_UNDERRUN)) {
				/* any other tx errors are only valid
				 * in half duplex mode */
				ps->tx_errors++;
				ps->tx_aborted_errors++;
			}
		}
		else {
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
	volatile tx_dma_t *ptxd;

	ptxd = aup->tx_dma_ring[aup->tx_tail];

	while (ptxd->buff_stat & TX_T_DONE) {
		update_tx_stats(dev, ptxd->status);
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

	if (au1000_debug > 4)
		printk("%s: open: dev=%p\n", dev->name, dev);

	if ((retval = request_irq(dev->irq, &au1000_interrupt, 0,
					dev->name, dev))) {
		printk(KERN_ERR "%s: unable to get IRQ %d\n",
				dev->name, dev->irq);
		return retval;
	}

	if ((retval = au1000_init(dev))) {
		printk(KERN_ERR "%s: error in au1000_init\n", dev->name);
		free_irq(dev->irq, dev);
		return retval;
	}

	if (aup->phy_dev) {
		/* cause the PHY state machine to schedule a link state check */
		aup->phy_dev->state = PHY_CHANGELINK;
		phy_start(aup->phy_dev);
	}

	netif_start_queue(dev);

	if (au1000_debug > 4)
		printk("%s: open: Initialization done.\n", dev->name);

	return 0;
}

static int au1000_close(struct net_device *dev)
{
	unsigned long flags;
	struct au1000_private *const aup = netdev_priv(dev);

	if (au1000_debug > 4)
		printk("%s: close: dev=%p\n", dev->name, dev);

	if (aup->phy_dev)
		phy_stop(aup->phy_dev);

	spin_lock_irqsave(&aup->lock, flags);

	reset_mac_unlocked (dev);

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
	volatile tx_dma_t *ptxd;
	u32 buff_stat;
	db_dest_t *pDB;
	int i;

	if (au1000_debug > 5)
		printk("%s: tx: aup %x len=%d, data=%p, head %d\n",
				dev->name, (unsigned)aup, skb->len,
				skb->data, aup->tx_head);

	ptxd = aup->tx_dma_ring[aup->tx_head];
	buff_stat = ptxd->buff_stat;
	if (buff_stat & TX_DMA_ENABLE) {
		/* We've wrapped around and the transmitter is still busy */
		netif_stop_queue(dev);
		aup->tx_full = 1;
		return NETDEV_TX_BUSY;
	}
	else if (buff_stat & TX_T_DONE) {
		update_tx_stats(dev, ptxd->status);
		ptxd->len = 0;
	}

	if (aup->tx_full) {
		aup->tx_full = 0;
		netif_wake_queue(dev);
	}

	pDB = aup->tx_db_inuse[aup->tx_head];
	skb_copy_from_linear_data(skb, pDB->vaddr, skb->len);
	if (skb->len < ETH_ZLEN) {
		for (i=skb->len; i<ETH_ZLEN; i++) {
			((char *)pDB->vaddr)[i] = 0;
		}
		ptxd->len = ETH_ZLEN;
	}
	else
		ptxd->len = skb->len;

	ps->tx_packets++;
	ps->tx_bytes += ptxd->len;

	ptxd->buff_stat = pDB->dma_addr | TX_DMA_ENABLE;
	au_sync();
	dev_kfree_skb(skb);
	aup->tx_head = (aup->tx_head + 1) & (NUM_TX_DMA - 1);
	dev->trans_start = jiffies;
	return NETDEV_TX_OK;
}

/*
 * The Tx ring has been full longer than the watchdog timeout
 * value. The transmitter must be hung?
 */
static void au1000_tx_timeout(struct net_device *dev)
{
	printk(KERN_ERR "%s: au1000_tx_timeout: dev=%p\n", dev->name, dev);
	reset_mac(dev);
	au1000_init(dev);
	dev->trans_start = jiffies;
	netif_wake_queue(dev);
}

static void au1000_multicast_list(struct net_device *dev)
{
	struct au1000_private *aup = netdev_priv(dev);

	if (au1000_debug > 4)
		printk("%s: au1000_multicast_list: flags=%x\n", dev->name, dev->flags);

	if (dev->flags & IFF_PROMISC) {			/* Set promiscuous. */
		aup->mac->control |= MAC_PROMISCUOUS;
	} else if ((dev->flags & IFF_ALLMULTI)  ||
			   dev->mc_count > MULTICAST_FILTER_LIMIT) {
		aup->mac->control |= MAC_PASS_ALL_MULTI;
		aup->mac->control &= ~MAC_PROMISCUOUS;
		printk(KERN_INFO "%s: Pass all multicast\n", dev->name);
	} else {
		int i;
		struct dev_mc_list *mclist;
		u32 mc_filter[2];	/* Multicast hash filter */

		mc_filter[1] = mc_filter[0] = 0;
		for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
			 i++, mclist = mclist->next) {
			set_bit(ether_crc(ETH_ALEN, mclist->dmi_addr)>>26,
					(long *)mc_filter);
		}
		aup->mac->multi_hash_high = mc_filter[1];
		aup->mac->multi_hash_low = mc_filter[0];
		aup->mac->control &= ~MAC_PROMISCUOUS;
		aup->mac->control |= MAC_HASH_MODE;
	}
}

static int au1000_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct au1000_private *aup = netdev_priv(dev);

	if (!netif_running(dev)) return -EINVAL;

	if (!aup->phy_dev) return -EINVAL; // PHY not controllable

	return phy_mii_ioctl(aup->phy_dev, if_mii(rq), cmd);
}

static const struct net_device_ops au1000_netdev_ops = {
	.ndo_open		= au1000_open,
	.ndo_stop		= au1000_close,
	.ndo_start_xmit		= au1000_tx,
	.ndo_set_multicast_list	= au1000_multicast_list,
	.ndo_do_ioctl		= au1000_ioctl,
	.ndo_tx_timeout		= au1000_tx_timeout,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
};

static struct net_device * au1000_probe(int port_num)
{
	static unsigned version_printed = 0;
	struct au1000_private *aup = NULL;
	struct net_device *dev = NULL;
	db_dest_t *pDB, *pDBfree;
	char ethaddr[6];
	int irq, i, err;
	u32 base, macen;

	if (port_num >= NUM_ETH_INTERFACES)
		return NULL;

	base  = CPHYSADDR(iflist[port_num].base_addr );
	macen = CPHYSADDR(iflist[port_num].macen_addr);
	irq = iflist[port_num].irq;

	if (!request_mem_region( base, MAC_IOSIZE, "Au1x00 ENET") ||
	    !request_mem_region(macen, 4, "Au1x00 ENET"))
		return NULL;

	if (version_printed++ == 0)
		printk("%s version %s %s\n", DRV_NAME, DRV_VERSION, DRV_AUTHOR);

	dev = alloc_etherdev(sizeof(struct au1000_private));
	if (!dev) {
		printk(KERN_ERR "%s: alloc_etherdev failed\n", DRV_NAME);
		return NULL;
	}

	if ((err = register_netdev(dev)) != 0) {
		printk(KERN_ERR "%s: Cannot register net device, error %d\n",
				DRV_NAME, err);
		free_netdev(dev);
		return NULL;
	}

	printk("%s: Au1xx0 Ethernet found at 0x%x, irq %d\n",
		dev->name, base, irq);

	aup = netdev_priv(dev);

	spin_lock_init(&aup->lock);

	/* Allocate the data buffers */
	/* Snooping works fine with eth on all au1xxx */
	aup->vaddr = (u32)dma_alloc_noncoherent(NULL, MAX_BUF_SIZE *
						(NUM_TX_BUFFS + NUM_RX_BUFFS),
						&aup->dma_addr,	0);
	if (!aup->vaddr) {
		free_netdev(dev);
		release_mem_region( base, MAC_IOSIZE);
		release_mem_region(macen, 4);
		return NULL;
	}

	/* aup->mac is the base address of the MAC's registers */
	aup->mac = (volatile mac_reg_t *)iflist[port_num].base_addr;

	/* Setup some variables for quick register address access */
	aup->enable = (volatile u32 *)iflist[port_num].macen_addr;
	aup->mac_id = port_num;
	au_macs[port_num] = aup;

	if (port_num == 0) {
		if (prom_get_ethernet_addr(ethaddr) == 0)
			memcpy(au1000_mac_addr, ethaddr, sizeof(au1000_mac_addr));
		else {
			printk(KERN_INFO "%s: No MAC address found\n",
					 dev->name);
				/* Use the hard coded MAC addresses */
		}

		setup_hw_rings(aup, MAC0_RX_DMA_ADDR, MAC0_TX_DMA_ADDR);
	} else if (port_num == 1)
		setup_hw_rings(aup, MAC1_RX_DMA_ADDR, MAC1_TX_DMA_ADDR);

	/*
	 * Assign to the Ethernet ports two consecutive MAC addresses
	 * to match those that are printed on their stickers
	 */
	memcpy(dev->dev_addr, au1000_mac_addr, sizeof(au1000_mac_addr));
	dev->dev_addr[5] += port_num;

	*aup->enable = 0;
	aup->mac_enabled = 0;

	aup->mii_bus = mdiobus_alloc();
	if (aup->mii_bus == NULL)
		goto err_out;

	aup->mii_bus->priv = dev;
	aup->mii_bus->read = au1000_mdiobus_read;
	aup->mii_bus->write = au1000_mdiobus_write;
	aup->mii_bus->reset = au1000_mdiobus_reset;
	aup->mii_bus->name = "au1000_eth_mii";
	snprintf(aup->mii_bus->id, MII_BUS_ID_SIZE, "%x", aup->mac_id);
	aup->mii_bus->irq = kmalloc(sizeof(int)*PHY_MAX_ADDR, GFP_KERNEL);
	if (aup->mii_bus->irq == NULL)
		goto err_out;

	for(i = 0; i < PHY_MAX_ADDR; ++i)
		aup->mii_bus->irq[i] = PHY_POLL;

	/* if known, set corresponding PHY IRQs */
#if defined(AU1XXX_PHY_STATIC_CONFIG)
# if defined(AU1XXX_PHY0_IRQ)
	if (AU1XXX_PHY0_BUSID == aup->mac_id)
		aup->mii_bus->irq[AU1XXX_PHY0_ADDR] = AU1XXX_PHY0_IRQ;
# endif
# if defined(AU1XXX_PHY1_IRQ)
	if (AU1XXX_PHY1_BUSID == aup->mac_id)
		aup->mii_bus->irq[AU1XXX_PHY1_ADDR] = AU1XXX_PHY1_IRQ;
# endif
#endif
	mdiobus_register(aup->mii_bus);

	if (mii_probe(dev) != 0) {
		goto err_out;
	}

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

	for (i = 0; i < NUM_RX_DMA; i++) {
		pDB = GetFreeDB(aup);
		if (!pDB) {
			goto err_out;
		}
		aup->rx_dma_ring[i]->buff_stat = (unsigned)pDB->dma_addr;
		aup->rx_db_inuse[i] = pDB;
	}
	for (i = 0; i < NUM_TX_DMA; i++) {
		pDB = GetFreeDB(aup);
		if (!pDB) {
			goto err_out;
		}
		aup->tx_dma_ring[i]->buff_stat = (unsigned)pDB->dma_addr;
		aup->tx_dma_ring[i]->len = 0;
		aup->tx_db_inuse[i] = pDB;
	}

	dev->base_addr = base;
	dev->irq = irq;
	dev->netdev_ops = &au1000_netdev_ops;
	SET_ETHTOOL_OPS(dev, &au1000_ethtool_ops);
	dev->watchdog_timeo = ETH_TX_TIMEOUT;

	/*
	 * The boot code uses the ethernet controller, so reset it to start
	 * fresh.  au1000_init() expects that the device is in reset state.
	 */
	reset_mac(dev);

	return dev;

err_out:
	if (aup->mii_bus != NULL) {
		mdiobus_unregister(aup->mii_bus);
		mdiobus_free(aup->mii_bus);
	}

	/* here we should have a valid dev plus aup-> register addresses
	 * so we can reset the mac properly.*/
	reset_mac(dev);

	for (i = 0; i < NUM_RX_DMA; i++) {
		if (aup->rx_db_inuse[i])
			ReleaseDB(aup, aup->rx_db_inuse[i]);
	}
	for (i = 0; i < NUM_TX_DMA; i++) {
		if (aup->tx_db_inuse[i])
			ReleaseDB(aup, aup->tx_db_inuse[i]);
	}
	dma_free_noncoherent(NULL, MAX_BUF_SIZE * (NUM_TX_BUFFS + NUM_RX_BUFFS),
			     (void *)aup->vaddr, aup->dma_addr);
	unregister_netdev(dev);
	free_netdev(dev);
	release_mem_region( base, MAC_IOSIZE);
	release_mem_region(macen, 4);
	return NULL;
}

/*
 * Setup the base address and interrupt of the Au1xxx ethernet macs
 * based on cpu type and whether the interface is enabled in sys_pinfunc
 * register. The last interface is enabled if SYS_PF_NI2 (bit 4) is 0.
 */
static int __init au1000_init_module(void)
{
	int ni = (int)((au_readl(SYS_PINFUNC) & (u32)(SYS_PF_NI2)) >> 4);
	struct net_device *dev;
	int i, found_one = 0;

	num_ifs = NUM_ETH_INTERFACES - ni;

	for(i = 0; i < num_ifs; i++) {
		dev = au1000_probe(i);
		iflist[i].dev = dev;
		if (dev)
			found_one++;
	}
	if (!found_one)
		return -ENODEV;
	return 0;
}

static void __exit au1000_cleanup_module(void)
{
	int i, j;
	struct net_device *dev;
	struct au1000_private *aup;

	for (i = 0; i < num_ifs; i++) {
		dev = iflist[i].dev;
		if (dev) {
			aup = netdev_priv(dev);
			unregister_netdev(dev);
			mdiobus_unregister(aup->mii_bus);
			mdiobus_free(aup->mii_bus);
			for (j = 0; j < NUM_RX_DMA; j++)
				if (aup->rx_db_inuse[j])
					ReleaseDB(aup, aup->rx_db_inuse[j]);
			for (j = 0; j < NUM_TX_DMA; j++)
				if (aup->tx_db_inuse[j])
					ReleaseDB(aup, aup->tx_db_inuse[j]);
			dma_free_noncoherent(NULL, MAX_BUF_SIZE *
					     (NUM_TX_BUFFS + NUM_RX_BUFFS),
					     (void *)aup->vaddr, aup->dma_addr);
			release_mem_region(dev->base_addr, MAC_IOSIZE);
			release_mem_region(CPHYSADDR(iflist[i].macen_addr), 4);
			free_netdev(dev);
		}
	}
}

module_init(au1000_init_module);
module_exit(au1000_cleanup_module);
