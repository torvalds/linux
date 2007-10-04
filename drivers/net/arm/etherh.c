/*
 *  linux/drivers/acorn/net/etherh.c
 *
 *  Copyright (C) 2000-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * NS8390 I-cubed EtherH and ANT EtherM specific driver
 * Thanks to I-Cubed for information on their cards.
 * EtherM conversion (C) 1999 Chris Kemp and Tim Watterton
 * EtherM integration (C) 2000 Aleph One Ltd (Tak-Shing Chan)
 * EtherM integration re-engineered by Russell King.
 *
 * Changelog:
 *  08-12-1996	RMK	1.00	Created
 *		RMK	1.03	Added support for EtherLan500 cards
 *  23-11-1997	RMK	1.04	Added media autodetection
 *  16-04-1998	RMK	1.05	Improved media autodetection
 *  10-02-2000	RMK	1.06	Updated for 2.3.43
 *  13-05-2000	RMK	1.07	Updated for 2.3.99-pre8
 *  12-10-1999  CK/TEW		EtherM driver first release
 *  21-12-2000	TTC		EtherH/EtherM integration
 *  25-12-2000	RMK	1.08	Clean integration of EtherM into this driver.
 *  03-01-2002	RMK	1.09	Always enable IRQs if we're in the nic slot.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/jiffies.h>

#include <asm/system.h>
#include <asm/ecard.h>
#include <asm/io.h>

#define EI_SHIFT(x)	(ei_local->reg_offset[x])

#define ei_inb(_p)	 readb((void __iomem *)_p)
#define ei_outb(_v,_p)	 writeb(_v,(void __iomem *)_p)
#define ei_inb_p(_p)	 readb((void __iomem *)_p)
#define ei_outb_p(_v,_p) writeb(_v,(void __iomem *)_p)

#define NET_DEBUG  0
#define DEBUG_INIT 2

#define DRV_NAME	"etherh"
#define DRV_VERSION	"1.11"

static char version[] __initdata =
	"EtherH/EtherM Driver (c) 2002-2004 Russell King " DRV_VERSION "\n";

#include "../lib8390.c"

static unsigned int net_debug = NET_DEBUG;

struct etherh_priv {
	void __iomem	*ioc_fast;
	void __iomem	*memc;
	void __iomem	*dma_base;
	unsigned int	id;
	void __iomem	*ctrl_port;
	unsigned char	ctrl;
	u32		supported;
};

struct etherh_data {
	unsigned long	ns8390_offset;
	unsigned long	dataport_offset;
	unsigned long	ctrlport_offset;
	int		ctrl_ioc;
	const char	name[16];
	u32		supported;
	unsigned char	tx_start_page;
	unsigned char	stop_page;
};

MODULE_AUTHOR("Russell King");
MODULE_DESCRIPTION("EtherH/EtherM driver");
MODULE_LICENSE("GPL");

#define ETHERH500_DATAPORT	0x800	/* MEMC */
#define ETHERH500_NS8390	0x000	/* MEMC */
#define ETHERH500_CTRLPORT	0x800	/* IOC  */

#define ETHERH600_DATAPORT	0x040	/* MEMC */
#define ETHERH600_NS8390	0x800	/* MEMC */
#define ETHERH600_CTRLPORT	0x200	/* MEMC */

#define ETHERH_CP_IE		1
#define ETHERH_CP_IF		2
#define ETHERH_CP_HEARTBEAT	2

#define ETHERH_TX_START_PAGE	1
#define ETHERH_STOP_PAGE	127

/*
 * These came from CK/TEW
 */
#define ETHERM_DATAPORT		0x200	/* MEMC */
#define ETHERM_NS8390		0x800	/* MEMC */
#define ETHERM_CTRLPORT		0x23c	/* MEMC */

#define ETHERM_TX_START_PAGE	64
#define ETHERM_STOP_PAGE	127

/* ------------------------------------------------------------------------ */

#define etherh_priv(dev) \
 ((struct etherh_priv *)(((char *)netdev_priv(dev)) + sizeof(struct ei_device)))

static inline void etherh_set_ctrl(struct etherh_priv *eh, unsigned char mask)
{
	unsigned char ctrl = eh->ctrl | mask;
	eh->ctrl = ctrl;
	writeb(ctrl, eh->ctrl_port);
}

static inline void etherh_clr_ctrl(struct etherh_priv *eh, unsigned char mask)
{
	unsigned char ctrl = eh->ctrl & ~mask;
	eh->ctrl = ctrl;
	writeb(ctrl, eh->ctrl_port);
}

static inline unsigned int etherh_get_stat(struct etherh_priv *eh)
{
	return readb(eh->ctrl_port);
}




static void etherh_irq_enable(ecard_t *ec, int irqnr)
{
	struct etherh_priv *eh = ec->irq_data;

	etherh_set_ctrl(eh, ETHERH_CP_IE);
}

static void etherh_irq_disable(ecard_t *ec, int irqnr)
{
	struct etherh_priv *eh = ec->irq_data;

	etherh_clr_ctrl(eh, ETHERH_CP_IE);
}

static expansioncard_ops_t etherh_ops = {
	.irqenable	= etherh_irq_enable,
	.irqdisable	= etherh_irq_disable,
};




static void
etherh_setif(struct net_device *dev)
{
	struct ei_device *ei_local = netdev_priv(dev);
	unsigned long flags;
	void __iomem *addr;

	local_irq_save(flags);

	/* set the interface type */
	switch (etherh_priv(dev)->id) {
	case PROD_I3_ETHERLAN600:
	case PROD_I3_ETHERLAN600A:
		addr = (void __iomem *)dev->base_addr + EN0_RCNTHI;

		switch (dev->if_port) {
		case IF_PORT_10BASE2:
			writeb((readb(addr) & 0xf8) | 1, addr);
			break;
		case IF_PORT_10BASET:
			writeb((readb(addr) & 0xf8), addr);
			break;
		}
		break;

	case PROD_I3_ETHERLAN500:
		switch (dev->if_port) {
		case IF_PORT_10BASE2:
			etherh_clr_ctrl(etherh_priv(dev), ETHERH_CP_IF);
			break;

		case IF_PORT_10BASET:
			etherh_set_ctrl(etherh_priv(dev), ETHERH_CP_IF);
			break;
		}
		break;

	default:
		break;
	}

	local_irq_restore(flags);
}

static int
etherh_getifstat(struct net_device *dev)
{
	struct ei_device *ei_local = netdev_priv(dev);
	void __iomem *addr;
	int stat = 0;

	switch (etherh_priv(dev)->id) {
	case PROD_I3_ETHERLAN600:
	case PROD_I3_ETHERLAN600A:
		addr = (void __iomem *)dev->base_addr + EN0_RCNTHI;
		switch (dev->if_port) {
		case IF_PORT_10BASE2:
			stat = 1;
			break;
		case IF_PORT_10BASET:
			stat = readb(addr) & 4;
			break;
		}
		break;

	case PROD_I3_ETHERLAN500:
		switch (dev->if_port) {
		case IF_PORT_10BASE2:
			stat = 1;
			break;
		case IF_PORT_10BASET:
			stat = etherh_get_stat(etherh_priv(dev)) & ETHERH_CP_HEARTBEAT;
			break;
		}
		break;

	default:
		stat = 0;
		break;
	}

	return stat != 0;
}

/*
 * Configure the interface.  Note that we ignore the other
 * parts of ifmap, since its mostly meaningless for this driver.
 */
static int etherh_set_config(struct net_device *dev, struct ifmap *map)
{
	switch (map->port) {
	case IF_PORT_10BASE2:
	case IF_PORT_10BASET:
		/*
		 * If the user explicitly sets the interface
		 * media type, turn off automedia detection.
		 */
		dev->flags &= ~IFF_AUTOMEDIA;
		dev->if_port = map->port;
		break;

	default:
		return -EINVAL;
	}

	etherh_setif(dev);

	return 0;
}

/*
 * Reset the 8390 (hard reset).  Note that we can't actually do this.
 */
static void
etherh_reset(struct net_device *dev)
{
	struct ei_device *ei_local = netdev_priv(dev);
	void __iomem *addr = (void __iomem *)dev->base_addr;

	writeb(E8390_NODMA+E8390_PAGE0+E8390_STOP, addr);

	/*
	 * See if we need to change the interface type.
	 * Note that we use 'interface_num' as a flag
	 * to indicate that we need to change the media.
	 */
	if (dev->flags & IFF_AUTOMEDIA && ei_local->interface_num) {
		ei_local->interface_num = 0;

		if (dev->if_port == IF_PORT_10BASET)
			dev->if_port = IF_PORT_10BASE2;
		else
			dev->if_port = IF_PORT_10BASET;

		etherh_setif(dev);
	}
}

/*
 * Write a block of data out to the 8390
 */
static void
etherh_block_output (struct net_device *dev, int count, const unsigned char *buf, int start_page)
{
	struct ei_device *ei_local = netdev_priv(dev);
	unsigned long dma_start;
	void __iomem *dma_base, *addr;

	if (ei_local->dmaing) {
		printk(KERN_ERR "%s: DMAing conflict in etherh_block_input: "
			" DMAstat %d irqlock %d\n", dev->name,
			ei_local->dmaing, ei_local->irqlock);
		return;
	}

	/*
	 * Make sure we have a round number of bytes if we're in word mode.
	 */
	if (count & 1 && ei_local->word16)
		count++;

	ei_local->dmaing = 1;

	addr = (void __iomem *)dev->base_addr;
	dma_base = etherh_priv(dev)->dma_base;

	count = (count + 1) & ~1;
	writeb (E8390_NODMA | E8390_PAGE0 | E8390_START, addr + E8390_CMD);

	writeb (0x42, addr + EN0_RCNTLO);
	writeb (0x00, addr + EN0_RCNTHI);
	writeb (0x42, addr + EN0_RSARLO);
	writeb (0x00, addr + EN0_RSARHI);
	writeb (E8390_RREAD | E8390_START, addr + E8390_CMD);

	udelay (1);

	writeb (ENISR_RDC, addr + EN0_ISR);
	writeb (count, addr + EN0_RCNTLO);
	writeb (count >> 8, addr + EN0_RCNTHI);
	writeb (0, addr + EN0_RSARLO);
	writeb (start_page, addr + EN0_RSARHI);
	writeb (E8390_RWRITE | E8390_START, addr + E8390_CMD);

	if (ei_local->word16)
		writesw (dma_base, buf, count >> 1);
	else
		writesb (dma_base, buf, count);

	dma_start = jiffies;

	while ((readb (addr + EN0_ISR) & ENISR_RDC) == 0)
		if (time_after(jiffies, dma_start + 2*HZ/100)) { /* 20ms */
			printk(KERN_ERR "%s: timeout waiting for TX RDC\n",
				dev->name);
			etherh_reset (dev);
			__NS8390_init (dev, 1);
			break;
		}

	writeb (ENISR_RDC, addr + EN0_ISR);
	ei_local->dmaing = 0;
}

/*
 * Read a block of data from the 8390
 */
static void
etherh_block_input (struct net_device *dev, int count, struct sk_buff *skb, int ring_offset)
{
	struct ei_device *ei_local = netdev_priv(dev);
	unsigned char *buf;
	void __iomem *dma_base, *addr;

	if (ei_local->dmaing) {
		printk(KERN_ERR "%s: DMAing conflict in etherh_block_input: "
			" DMAstat %d irqlock %d\n", dev->name,
			ei_local->dmaing, ei_local->irqlock);
		return;
	}

	ei_local->dmaing = 1;

	addr = (void __iomem *)dev->base_addr;
	dma_base = etherh_priv(dev)->dma_base;

	buf = skb->data;
	writeb (E8390_NODMA | E8390_PAGE0 | E8390_START, addr + E8390_CMD);
	writeb (count, addr + EN0_RCNTLO);
	writeb (count >> 8, addr + EN0_RCNTHI);
	writeb (ring_offset, addr + EN0_RSARLO);
	writeb (ring_offset >> 8, addr + EN0_RSARHI);
	writeb (E8390_RREAD | E8390_START, addr + E8390_CMD);

	if (ei_local->word16) {
		readsw (dma_base, buf, count >> 1);
		if (count & 1)
			buf[count - 1] = readb (dma_base);
	} else
		readsb (dma_base, buf, count);

	writeb (ENISR_RDC, addr + EN0_ISR);
	ei_local->dmaing = 0;
}

/*
 * Read a header from the 8390
 */
static void
etherh_get_header (struct net_device *dev, struct e8390_pkt_hdr *hdr, int ring_page)
{
	struct ei_device *ei_local = netdev_priv(dev);
	void __iomem *dma_base, *addr;

	if (ei_local->dmaing) {
		printk(KERN_ERR "%s: DMAing conflict in etherh_get_header: "
			" DMAstat %d irqlock %d\n", dev->name,
			ei_local->dmaing, ei_local->irqlock);
		return;
	}

	ei_local->dmaing = 1;

	addr = (void __iomem *)dev->base_addr;
	dma_base = etherh_priv(dev)->dma_base;

	writeb (E8390_NODMA | E8390_PAGE0 | E8390_START, addr + E8390_CMD);
	writeb (sizeof (*hdr), addr + EN0_RCNTLO);
	writeb (0, addr + EN0_RCNTHI);
	writeb (0, addr + EN0_RSARLO);
	writeb (ring_page, addr + EN0_RSARHI);
	writeb (E8390_RREAD | E8390_START, addr + E8390_CMD);

	if (ei_local->word16)
		readsw (dma_base, hdr, sizeof (*hdr) >> 1);
	else
		readsb (dma_base, hdr, sizeof (*hdr));

	writeb (ENISR_RDC, addr + EN0_ISR);
	ei_local->dmaing = 0;
}

/*
 * Open/initialize the board.  This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
static int
etherh_open(struct net_device *dev)
{
	struct ei_device *ei_local = netdev_priv(dev);

	if (!is_valid_ether_addr(dev->dev_addr)) {
		printk(KERN_WARNING "%s: invalid ethernet MAC address\n",
			dev->name);
		return -EINVAL;
	}

	if (request_irq(dev->irq, __ei_interrupt, 0, dev->name, dev))
		return -EAGAIN;

	/*
	 * Make sure that we aren't going to change the
	 * media type on the next reset - we are about to
	 * do automedia manually now.
	 */
	ei_local->interface_num = 0;

	/*
	 * If we are doing automedia detection, do it now.
	 * This is more reliable than the 8390's detection.
	 */
	if (dev->flags & IFF_AUTOMEDIA) {
		dev->if_port = IF_PORT_10BASET;
		etherh_setif(dev);
		mdelay(1);
		if (!etherh_getifstat(dev)) {
			dev->if_port = IF_PORT_10BASE2;
			etherh_setif(dev);
		}
	} else
		etherh_setif(dev);

	etherh_reset(dev);
	__ei_open(dev);

	return 0;
}

/*
 * The inverse routine to etherh_open().
 */
static int
etherh_close(struct net_device *dev)
{
	__ei_close (dev);
	free_irq (dev->irq, dev);
	return 0;
}

/*
 * Initialisation
 */

static void __init etherh_banner(void)
{
	static int version_printed;

	if (net_debug && version_printed++ == 0)
		printk(KERN_INFO "%s", version);
}

/*
 * Read the ethernet address string from the on board rom.
 * This is an ascii string...
 */
static int __init etherh_addr(char *addr, struct expansion_card *ec)
{
	struct in_chunk_dir cd;
	char *s;
	
	if (!ecard_readchunk(&cd, ec, 0xf5, 0)) {
		printk(KERN_ERR "%s: unable to read podule description string\n",
		       ec->dev.bus_id);
		goto no_addr;
	}

	s = strchr(cd.d.string, '(');
	if (s) {
		int i;

		for (i = 0; i < 6; i++) {
			addr[i] = simple_strtoul(s + 1, &s, 0x10);
			if (*s != (i == 5? ')' : ':'))
				break;
		}

		if (i == 6)
			return 0;
	}

	printk(KERN_ERR "%s: unable to parse MAC address: %s\n",
	       ec->dev.bus_id, cd.d.string);

 no_addr:
	return -ENODEV;
}

/*
 * Create an ethernet address from the system serial number.
 */
static int __init etherm_addr(char *addr)
{
	unsigned int serial;

	if (system_serial_low == 0 && system_serial_high == 0)
		return -ENODEV;

	serial = system_serial_low | system_serial_high;

	addr[0] = 0;
	addr[1] = 0;
	addr[2] = 0xa4;
	addr[3] = 0x10 + (serial >> 24);
	addr[4] = serial >> 16;
	addr[5] = serial >> 8;
	return 0;
}

static void etherh_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev->dev.parent->bus_id,
		sizeof(info->bus_info));
}

static int etherh_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	cmd->supported	= etherh_priv(dev)->supported;
	cmd->speed	= SPEED_10;
	cmd->duplex	= DUPLEX_HALF;
	cmd->port	= dev->if_port == IF_PORT_10BASET ? PORT_TP : PORT_BNC;
	cmd->autoneg	= dev->flags & IFF_AUTOMEDIA ? AUTONEG_ENABLE : AUTONEG_DISABLE;
	return 0;
}

static int etherh_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	switch (cmd->autoneg) {
	case AUTONEG_ENABLE:
		dev->flags |= IFF_AUTOMEDIA;
		break;

	case AUTONEG_DISABLE:
		switch (cmd->port) {
		case PORT_TP:
			dev->if_port = IF_PORT_10BASET;
			break;

		case PORT_BNC:
			dev->if_port = IF_PORT_10BASE2;
			break;

		default:
			return -EINVAL;
		}
		dev->flags &= ~IFF_AUTOMEDIA;
		break;

	default:
		return -EINVAL;
	}

	etherh_setif(dev);

	return 0;
}

static const struct ethtool_ops etherh_ethtool_ops = {
	.get_settings	= etherh_get_settings,
	.set_settings	= etherh_set_settings,
	.get_drvinfo	= etherh_get_drvinfo,
};

static u32 etherh_regoffsets[16];
static u32 etherm_regoffsets[16];

static int __init
etherh_probe(struct expansion_card *ec, const struct ecard_id *id)
{
	const struct etherh_data *data = id->data;
	struct ei_device *ei_local;
	struct net_device *dev;
	struct etherh_priv *eh;
	int i, ret;
	DECLARE_MAC_BUF(mac);

	etherh_banner();

	ret = ecard_request_resources(ec);
	if (ret)
		goto out;

	dev = ____alloc_ei_netdev(sizeof(struct etherh_priv));
	if (!dev) {
		ret = -ENOMEM;
		goto release;
	}

	SET_NETDEV_DEV(dev, &ec->dev);

	dev->open		= etherh_open;
	dev->stop		= etherh_close;
	dev->set_config		= etherh_set_config;
	dev->irq		= ec->irq;
	dev->ethtool_ops	= &etherh_ethtool_ops;

	if (data->supported & SUPPORTED_Autoneg)
		dev->flags |= IFF_AUTOMEDIA;
	if (data->supported & SUPPORTED_TP) {
		dev->flags |= IFF_PORTSEL;
		dev->if_port = IF_PORT_10BASET;
	} else if (data->supported & SUPPORTED_BNC) {
		dev->flags |= IFF_PORTSEL;
		dev->if_port = IF_PORT_10BASE2;
	} else
		dev->if_port = IF_PORT_UNKNOWN;

	eh = etherh_priv(dev);
	eh->supported		= data->supported;
	eh->ctrl		= 0;
	eh->id			= ec->cid.product;
	eh->memc		= ecardm_iomap(ec, ECARD_RES_MEMC, 0, PAGE_SIZE);
	if (!eh->memc) {
		ret = -ENOMEM;
		goto free;
	}

	eh->ctrl_port = eh->memc;
	if (data->ctrl_ioc) {
		eh->ioc_fast = ecardm_iomap(ec, ECARD_RES_IOCFAST, 0, PAGE_SIZE);
		if (!eh->ioc_fast) {
			ret = -ENOMEM;
			goto free;
		}
		eh->ctrl_port = eh->ioc_fast;
	}

	dev->base_addr = (unsigned long)eh->memc + data->ns8390_offset;
	eh->dma_base = eh->memc + data->dataport_offset;
	eh->ctrl_port += data->ctrlport_offset;

	/*
	 * IRQ and control port handling - only for non-NIC slot cards.
	 */
	if (ec->slot_no != 8) {
		ecard_setirq(ec, &etherh_ops, eh);
	} else {
		/*
		 * If we're in the NIC slot, make sure the IRQ is enabled
		 */
		etherh_set_ctrl(eh, ETHERH_CP_IE);
	}

	ei_local = netdev_priv(dev);
	spin_lock_init(&ei_local->page_lock);

	if (ec->cid.product == PROD_ANT_ETHERM) {
		etherm_addr(dev->dev_addr);
		ei_local->reg_offset = etherm_regoffsets;
	} else {
		etherh_addr(dev->dev_addr, ec);
		ei_local->reg_offset = etherh_regoffsets;
	}

	ei_local->name          = dev->name;
	ei_local->word16        = 1;
	ei_local->tx_start_page = data->tx_start_page;
	ei_local->rx_start_page = ei_local->tx_start_page + TX_PAGES;
	ei_local->stop_page     = data->stop_page;
	ei_local->reset_8390    = etherh_reset;
	ei_local->block_input   = etherh_block_input;
	ei_local->block_output  = etherh_block_output;
	ei_local->get_8390_hdr  = etherh_get_header;
	ei_local->interface_num = 0;

	etherh_reset(dev);
	__NS8390_init(dev, 0);

	ret = register_netdev(dev);
	if (ret)
		goto free;

	printk(KERN_INFO "%s: %s in slot %d, %s\n",
		dev->name, data->name, ec->slot_no, print_mac(mac, dev->dev_addr));

	ecard_set_drvdata(ec, dev);

	return 0;

 free:
	free_netdev(dev);
 release:
	ecard_release_resources(ec);
 out:
	return ret;
}

static void __devexit etherh_remove(struct expansion_card *ec)
{
	struct net_device *dev = ecard_get_drvdata(ec);

	ecard_set_drvdata(ec, NULL);

	unregister_netdev(dev);

	free_netdev(dev);

	ecard_release_resources(ec);
}

static struct etherh_data etherm_data = {
	.ns8390_offset		= ETHERM_NS8390,
	.dataport_offset	= ETHERM_NS8390 + ETHERM_DATAPORT,
	.ctrlport_offset	= ETHERM_NS8390 + ETHERM_CTRLPORT,
	.name			= "ANT EtherM",
	.supported		= SUPPORTED_10baseT_Half,
	.tx_start_page		= ETHERM_TX_START_PAGE,
	.stop_page		= ETHERM_STOP_PAGE,
};

static struct etherh_data etherlan500_data = {
	.ns8390_offset		= ETHERH500_NS8390,
	.dataport_offset	= ETHERH500_NS8390 + ETHERH500_DATAPORT,
	.ctrlport_offset	= ETHERH500_CTRLPORT,
	.ctrl_ioc		= 1,
	.name			= "i3 EtherH 500",
	.supported		= SUPPORTED_10baseT_Half,
	.tx_start_page		= ETHERH_TX_START_PAGE,
	.stop_page		= ETHERH_STOP_PAGE,
};

static struct etherh_data etherlan600_data = {
	.ns8390_offset		= ETHERH600_NS8390,
	.dataport_offset	= ETHERH600_NS8390 + ETHERH600_DATAPORT,
	.ctrlport_offset	= ETHERH600_NS8390 + ETHERH600_CTRLPORT,
	.name			= "i3 EtherH 600",
	.supported		= SUPPORTED_10baseT_Half | SUPPORTED_TP | SUPPORTED_BNC | SUPPORTED_Autoneg,
	.tx_start_page		= ETHERH_TX_START_PAGE,
	.stop_page		= ETHERH_STOP_PAGE,
};

static struct etherh_data etherlan600a_data = {
	.ns8390_offset		= ETHERH600_NS8390,
	.dataport_offset	= ETHERH600_NS8390 + ETHERH600_DATAPORT,
	.ctrlport_offset	= ETHERH600_NS8390 + ETHERH600_CTRLPORT,
	.name			= "i3 EtherH 600A",
	.supported		= SUPPORTED_10baseT_Half | SUPPORTED_TP | SUPPORTED_BNC | SUPPORTED_Autoneg,
	.tx_start_page		= ETHERH_TX_START_PAGE,
	.stop_page		= ETHERH_STOP_PAGE,
};

static const struct ecard_id etherh_ids[] = {
	{ MANU_ANT, PROD_ANT_ETHERM,      &etherm_data       },
	{ MANU_I3,  PROD_I3_ETHERLAN500,  &etherlan500_data  },
	{ MANU_I3,  PROD_I3_ETHERLAN600,  &etherlan600_data  },
	{ MANU_I3,  PROD_I3_ETHERLAN600A, &etherlan600a_data },
	{ 0xffff,   0xffff }
};

static struct ecard_driver etherh_driver = {
	.probe		= etherh_probe,
	.remove		= __devexit_p(etherh_remove),
	.id_table	= etherh_ids,
	.drv = {
		.name	= DRV_NAME,
	},
};

static int __init etherh_init(void)
{
	int i;

	for (i = 0; i < 16; i++) {
		etherh_regoffsets[i] = i << 2;
		etherm_regoffsets[i] = i << 5;
	}

	return ecard_register_driver(&etherh_driver);
}

static void __exit etherh_exit(void)
{
	ecard_remove_driver(&etherh_driver);
}

module_init(etherh_init);
module_exit(etherh_exit);
