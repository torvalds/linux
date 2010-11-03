/* drivers/net/ax88796.c
 *
 * Copyright 2005,2007 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * Asix AX88796 10/100 Ethernet controller support
 *	Based on ne.c, by Donald Becker, et-al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/isapnp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/eeprom_93cx6.h>
#include <linux/slab.h>

#include <net/ax88796.h>

#include <asm/system.h>
#include <asm/io.h>

static int phy_debug = 0;

/* Rename the lib8390.c functions to show that they are in this driver */
#define __ei_open       ax_ei_open
#define __ei_close      ax_ei_close
#define __ei_poll	ax_ei_poll
#define __ei_start_xmit ax_ei_start_xmit
#define __ei_tx_timeout ax_ei_tx_timeout
#define __ei_get_stats  ax_ei_get_stats
#define __ei_set_multicast_list ax_ei_set_multicast_list
#define __ei_interrupt  ax_ei_interrupt
#define ____alloc_ei_netdev ax__alloc_ei_netdev
#define __NS8390_init   ax_NS8390_init

/* force unsigned long back to 'void __iomem *' */
#define ax_convert_addr(_a) ((void __force __iomem *)(_a))

#define ei_inb(_a)	readb(ax_convert_addr(_a))
#define ei_outb(_v, _a) writeb(_v, ax_convert_addr(_a))

#define ei_inb_p(_a)	ei_inb(_a)
#define ei_outb_p(_v, _a) ei_outb(_v, _a)

/* define EI_SHIFT() to take into account our register offsets */
#define EI_SHIFT(x)     (ei_local->reg_offset[(x)])

/* Ensure we have our RCR base value */
#define AX88796_PLATFORM

static unsigned char version[] = "ax88796.c: Copyright 2005,2007 Simtec Electronics\n";

#include "lib8390.c"

#define DRV_NAME "ax88796"
#define DRV_VERSION "1.00"

/* from ne.c */
#define NE_CMD		EI_SHIFT(0x00)
#define NE_RESET	EI_SHIFT(0x1f)
#define NE_DATAPORT	EI_SHIFT(0x10)

#define NE1SM_START_PG	0x20	/* First page of TX buffer */
#define NE1SM_STOP_PG 	0x40	/* Last page +1 of RX ring */
#define NESM_START_PG	0x40	/* First page of TX buffer */
#define NESM_STOP_PG	0x80	/* Last page +1 of RX ring */

/* device private data */

struct ax_device {
	struct timer_list	 mii_timer;
	spinlock_t		 mii_lock;
	struct mii_if_info	 mii;

	u32			 msg_enable;
	void __iomem		*map2;
	struct platform_device	*dev;
	struct resource		*mem;
	struct resource		*mem2;
	struct ax_plat_data	*plat;

	unsigned char		 running;
	unsigned char		 resume_open;
	unsigned int		 irqflags;

	u32			 reg_offsets[0x20];
};

static inline struct ax_device *to_ax_dev(struct net_device *dev)
{
	struct ei_device *ei_local = netdev_priv(dev);
	return (struct ax_device *)(ei_local+1);
}

/* ax_initial_check
 *
 * do an initial probe for the card to check wether it exists
 * and is functional
 */

static int ax_initial_check(struct net_device *dev)
{
	struct ei_device *ei_local = netdev_priv(dev);
	void __iomem *ioaddr = ei_local->mem;
	int reg0;
	int regd;

	reg0 = ei_inb(ioaddr);
	if (reg0 == 0xFF)
		return -ENODEV;

	ei_outb(E8390_NODMA+E8390_PAGE1+E8390_STOP, ioaddr + E8390_CMD);
	regd = ei_inb(ioaddr + 0x0d);
	ei_outb(0xff, ioaddr + 0x0d);
	ei_outb(E8390_NODMA+E8390_PAGE0, ioaddr + E8390_CMD);
	ei_inb(ioaddr + EN0_COUNTER0); /* Clear the counter by reading. */
	if (ei_inb(ioaddr + EN0_COUNTER0) != 0) {
		ei_outb(reg0, ioaddr);
		ei_outb(regd, ioaddr + 0x0d);	/* Restore the old values. */
		return -ENODEV;
	}

	return 0;
}

/* Hard reset the card.  This used to pause for the same period that a
   8390 reset command required, but that shouldn't be necessary. */

static void ax_reset_8390(struct net_device *dev)
{
	struct ei_device *ei_local = netdev_priv(dev);
	struct ax_device  *ax = to_ax_dev(dev);
	unsigned long reset_start_time = jiffies;
	void __iomem *addr = (void __iomem *)dev->base_addr;

	if (ei_debug > 1)
		dev_dbg(&ax->dev->dev, "resetting the 8390 t=%ld\n", jiffies);

	ei_outb(ei_inb(addr + NE_RESET), addr + NE_RESET);

	ei_status.txing = 0;
	ei_status.dmaing = 0;

	/* This check _should_not_ be necessary, omit eventually. */
	while ((ei_inb(addr + EN0_ISR) & ENISR_RESET) == 0) {
		if (jiffies - reset_start_time > 2*HZ/100) {
			dev_warn(&ax->dev->dev, "%s: %s did not complete.\n",
			       __func__, dev->name);
			break;
		}
	}

	ei_outb(ENISR_RESET, addr + EN0_ISR);	/* Ack intr. */
}


static void ax_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
			    int ring_page)
{
	struct ei_device *ei_local = netdev_priv(dev);
	struct ax_device  *ax = to_ax_dev(dev);
	void __iomem *nic_base = ei_local->mem;

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		dev_err(&ax->dev->dev, "%s: DMAing conflict in %s "
			"[DMAstat:%d][irqlock:%d].\n",
			dev->name, __func__,
			ei_status.dmaing, ei_status.irqlock);
		return;
	}

	ei_status.dmaing |= 0x01;
	ei_outb(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
	ei_outb(sizeof(struct e8390_pkt_hdr), nic_base + EN0_RCNTLO);
	ei_outb(0, nic_base + EN0_RCNTHI);
	ei_outb(0, nic_base + EN0_RSARLO);		/* On page boundary */
	ei_outb(ring_page, nic_base + EN0_RSARHI);
	ei_outb(E8390_RREAD+E8390_START, nic_base + NE_CMD);

	if (ei_status.word16)
		readsw(nic_base + NE_DATAPORT, hdr, sizeof(struct e8390_pkt_hdr)>>1);
	else
		readsb(nic_base + NE_DATAPORT, hdr, sizeof(struct e8390_pkt_hdr));

	ei_outb(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;

	le16_to_cpus(&hdr->count);
}


/* Block input and output, similar to the Crynwr packet driver.  If you
   are porting to a new ethercard, look at the packet driver source for hints.
   The NEx000 doesn't share the on-board packet memory -- you have to put
   the packet out through the "remote DMA" dataport using ei_outb. */

static void ax_block_input(struct net_device *dev, int count,
			   struct sk_buff *skb, int ring_offset)
{
	struct ei_device *ei_local = netdev_priv(dev);
	struct ax_device  *ax = to_ax_dev(dev);
	void __iomem *nic_base = ei_local->mem;
	char *buf = skb->data;

	if (ei_status.dmaing) {
		dev_err(&ax->dev->dev,
			"%s: DMAing conflict in %s "
			"[DMAstat:%d][irqlock:%d].\n",
			dev->name, __func__,
			ei_status.dmaing, ei_status.irqlock);
		return;
	}

	ei_status.dmaing |= 0x01;

	ei_outb(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
	ei_outb(count & 0xff, nic_base + EN0_RCNTLO);
	ei_outb(count >> 8, nic_base + EN0_RCNTHI);
	ei_outb(ring_offset & 0xff, nic_base + EN0_RSARLO);
	ei_outb(ring_offset >> 8, nic_base + EN0_RSARHI);
	ei_outb(E8390_RREAD+E8390_START, nic_base + NE_CMD);

	if (ei_status.word16) {
		readsw(nic_base + NE_DATAPORT, buf, count >> 1);
		if (count & 0x01)
			buf[count-1] = ei_inb(nic_base + NE_DATAPORT);

	} else {
		readsb(nic_base + NE_DATAPORT, buf, count);
	}

	ei_status.dmaing &= ~1;
}

static void ax_block_output(struct net_device *dev, int count,
			    const unsigned char *buf, const int start_page)
{
	struct ei_device *ei_local = netdev_priv(dev);
	struct ax_device  *ax = to_ax_dev(dev);
	void __iomem *nic_base = ei_local->mem;
	unsigned long dma_start;

	/* Round the count up for word writes.  Do we need to do this?
	   What effect will an odd byte count have on the 8390?
	   I should check someday. */

	if (ei_status.word16 && (count & 0x01))
		count++;

	/* This *shouldn't* happen. If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		dev_err(&ax->dev->dev, "%s: DMAing conflict in %s."
			"[DMAstat:%d][irqlock:%d]\n",
			dev->name, __func__,
		       ei_status.dmaing, ei_status.irqlock);
		return;
	}

	ei_status.dmaing |= 0x01;
	/* We should already be in page 0, but to be safe... */
	ei_outb(E8390_PAGE0+E8390_START+E8390_NODMA, nic_base + NE_CMD);

	ei_outb(ENISR_RDC, nic_base + EN0_ISR);

	/* Now the normal output. */
	ei_outb(count & 0xff, nic_base + EN0_RCNTLO);
	ei_outb(count >> 8,   nic_base + EN0_RCNTHI);
	ei_outb(0x00, nic_base + EN0_RSARLO);
	ei_outb(start_page, nic_base + EN0_RSARHI);

	ei_outb(E8390_RWRITE+E8390_START, nic_base + NE_CMD);
	if (ei_status.word16) {
		writesw(nic_base + NE_DATAPORT, buf, count>>1);
	} else {
		writesb(nic_base + NE_DATAPORT, buf, count);
	}

	dma_start = jiffies;

	while ((ei_inb(nic_base + EN0_ISR) & ENISR_RDC) == 0) {
		if (jiffies - dma_start > 2*HZ/100) {		/* 20ms */
			dev_warn(&ax->dev->dev,
				 "%s: timeout waiting for Tx RDC.\n", dev->name);
			ax_reset_8390(dev);
			ax_NS8390_init(dev,1);
			break;
		}
	}

	ei_outb(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
}

/* definitions for accessing MII/EEPROM interface */

#define AX_MEMR			EI_SHIFT(0x14)
#define AX_MEMR_MDC		(1<<0)
#define AX_MEMR_MDIR		(1<<1)
#define AX_MEMR_MDI		(1<<2)
#define AX_MEMR_MDO		(1<<3)
#define AX_MEMR_EECS		(1<<4)
#define AX_MEMR_EEI		(1<<5)
#define AX_MEMR_EEO		(1<<6)
#define AX_MEMR_EECLK		(1<<7)

/* ax_mii_ei_outbits
 *
 * write the specified set of bits to the phy
*/

static void
ax_mii_ei_outbits(struct net_device *dev, unsigned int bits, int len)
{
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	void __iomem *memr_addr = (void __iomem *)dev->base_addr + AX_MEMR;
	unsigned int memr;

	/* clock low, data to output mode */
	memr = ei_inb(memr_addr);
	memr &= ~(AX_MEMR_MDC | AX_MEMR_MDIR);
	ei_outb(memr, memr_addr);

	for (len--; len >= 0; len--) {
		if (bits & (1 << len))
			memr |= AX_MEMR_MDO;
		else
			memr &= ~AX_MEMR_MDO;

		ei_outb(memr, memr_addr);

		/* clock high */

		ei_outb(memr | AX_MEMR_MDC, memr_addr);
		udelay(1);

		/* clock low */
		ei_outb(memr, memr_addr);
	}

	/* leaves the clock line low, mdir input */
	memr |= AX_MEMR_MDIR;
	ei_outb(memr, (void __iomem *)dev->base_addr + AX_MEMR);
}

/* ax_phy_ei_inbits
 *
 * read a specified number of bits from the phy
*/

static unsigned int
ax_phy_ei_inbits(struct net_device *dev, int no)
{
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	void __iomem *memr_addr = (void __iomem *)dev->base_addr + AX_MEMR;
	unsigned int memr;
	unsigned int result = 0;

	/* clock low, data to input mode */
	memr = ei_inb(memr_addr);
	memr &= ~AX_MEMR_MDC;
	memr |= AX_MEMR_MDIR;
	ei_outb(memr, memr_addr);

	for (no--; no >= 0; no--) {
		ei_outb(memr | AX_MEMR_MDC, memr_addr);

		udelay(1);

		if (ei_inb(memr_addr) & AX_MEMR_MDI)
			result |= (1<<no);

		ei_outb(memr, memr_addr);
	}

	return result;
}

/* ax_phy_issueaddr
 *
 * use the low level bit shifting routines to send the address
 * and command to the specified phy
*/

static void
ax_phy_issueaddr(struct net_device *dev, int phy_addr, int reg, int opc)
{
	if (phy_debug)
		pr_debug("%s: dev %p, %04x, %04x, %d\n",
			__func__, dev, phy_addr, reg, opc);

	ax_mii_ei_outbits(dev, 0x3f, 6);	/* pre-amble */
	ax_mii_ei_outbits(dev, 1, 2);		/* frame-start */
	ax_mii_ei_outbits(dev, opc, 2);		/* op code */
	ax_mii_ei_outbits(dev, phy_addr, 5);	/* phy address */
	ax_mii_ei_outbits(dev, reg, 5);		/* reg address */
}

static int
ax_phy_read(struct net_device *dev, int phy_addr, int reg)
{
	struct ei_device *ei_local = (struct ei_device *) netdev_priv(dev);
	unsigned long flags;
 	unsigned int result;

      	spin_lock_irqsave(&ei_local->page_lock, flags);

	ax_phy_issueaddr(dev, phy_addr, reg, 2);

	result = ax_phy_ei_inbits(dev, 17);
	result &= ~(3<<16);

      	spin_unlock_irqrestore(&ei_local->page_lock, flags);

	if (phy_debug)
		pr_debug("%s: %04x.%04x => read %04x\n", __func__,
			 phy_addr, reg, result);

	return result;
}

static void
ax_phy_write(struct net_device *dev, int phy_addr, int reg, int value)
{
	struct ei_device *ei = (struct ei_device *) netdev_priv(dev);
	struct ax_device  *ax = to_ax_dev(dev);
	unsigned long flags;

	dev_dbg(&ax->dev->dev, "%s: %p, %04x, %04x %04x\n",
		__func__, dev, phy_addr, reg, value);

      	spin_lock_irqsave(&ei->page_lock, flags);

	ax_phy_issueaddr(dev, phy_addr, reg, 1);
	ax_mii_ei_outbits(dev, 2, 2);		/* send TA */
	ax_mii_ei_outbits(dev, value, 16);

      	spin_unlock_irqrestore(&ei->page_lock, flags);
}

static void ax_mii_expiry(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct ax_device  *ax = to_ax_dev(dev);
	unsigned long flags;

	spin_lock_irqsave(&ax->mii_lock, flags);
	mii_check_media(&ax->mii, netif_msg_link(ax), 0);
	spin_unlock_irqrestore(&ax->mii_lock, flags);

	if (ax->running) {
		ax->mii_timer.expires = jiffies + HZ*2;
		add_timer(&ax->mii_timer);
	}
}

static int ax_open(struct net_device *dev)
{
	struct ax_device  *ax = to_ax_dev(dev);
	struct ei_device *ei_local = netdev_priv(dev);
	int ret;

	dev_dbg(&ax->dev->dev, "%s: open\n", dev->name);

	ret = request_irq(dev->irq, ax_ei_interrupt, ax->irqflags,
			  dev->name, dev);
	if (ret)
		return ret;

	ret = ax_ei_open(dev);
	if (ret) {
		free_irq(dev->irq, dev);
		return ret;
	}

	/* turn the phy on (if turned off) */

	ei_outb(ax->plat->gpoc_val, ei_local->mem + EI_SHIFT(0x17));
	ax->running = 1;

	/* start the MII timer */

	init_timer(&ax->mii_timer);

	ax->mii_timer.expires  = jiffies+1;
	ax->mii_timer.data     = (unsigned long) dev;
	ax->mii_timer.function = ax_mii_expiry;

	add_timer(&ax->mii_timer);

	return 0;
}

static int ax_close(struct net_device *dev)
{
	struct ax_device *ax = to_ax_dev(dev);
	struct ei_device *ei_local = netdev_priv(dev);

	dev_dbg(&ax->dev->dev, "%s: close\n", dev->name);

	/* turn the phy off */

	ei_outb(ax->plat->gpoc_val | (1<<6),
	       ei_local->mem + EI_SHIFT(0x17));

	ax->running = 0;
	wmb();

	del_timer_sync(&ax->mii_timer);
	ax_ei_close(dev);

	free_irq(dev->irq, dev);
	return 0;
}

static int ax_ioctl(struct net_device *dev, struct ifreq *req, int cmd)
{
	struct ax_device *ax = to_ax_dev(dev);
	unsigned long flags;
	int rc;

	if (!netif_running(dev))
		return -EINVAL;

	spin_lock_irqsave(&ax->mii_lock, flags);
	rc = generic_mii_ioctl(&ax->mii, if_mii(req), cmd, NULL);
	spin_unlock_irqrestore(&ax->mii_lock, flags);

	return rc;
}

/* ethtool ops */

static void ax_get_drvinfo(struct net_device *dev,
			   struct ethtool_drvinfo *info)
{
	struct ax_device *ax = to_ax_dev(dev);

	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);
	strcpy(info->bus_info, ax->dev->name);
}

static int ax_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct ax_device *ax = to_ax_dev(dev);
	unsigned long flags;

	spin_lock_irqsave(&ax->mii_lock, flags);
	mii_ethtool_gset(&ax->mii, cmd);
	spin_unlock_irqrestore(&ax->mii_lock, flags);

	return 0;
}

static int ax_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct ax_device *ax = to_ax_dev(dev);
	unsigned long flags;
	int rc;

	spin_lock_irqsave(&ax->mii_lock, flags);
	rc = mii_ethtool_sset(&ax->mii, cmd);
	spin_unlock_irqrestore(&ax->mii_lock, flags);

	return rc;
}

static int ax_nway_reset(struct net_device *dev)
{
	struct ax_device *ax = to_ax_dev(dev);
	return mii_nway_restart(&ax->mii);
}

static u32 ax_get_link(struct net_device *dev)
{
	struct ax_device *ax = to_ax_dev(dev);
	return mii_link_ok(&ax->mii);
}

static const struct ethtool_ops ax_ethtool_ops = {
	.get_drvinfo		= ax_get_drvinfo,
	.get_settings		= ax_get_settings,
	.set_settings		= ax_set_settings,
	.nway_reset		= ax_nway_reset,
	.get_link		= ax_get_link,
};

#ifdef CONFIG_AX88796_93CX6
static void ax_eeprom_register_read(struct eeprom_93cx6 *eeprom)
{
	struct ei_device *ei_local = eeprom->data;
	u8 reg = ei_inb(ei_local->mem + AX_MEMR);

	eeprom->reg_data_in = reg & AX_MEMR_EEI;
	eeprom->reg_data_out = reg & AX_MEMR_EEO; /* Input pin */
	eeprom->reg_data_clock = reg & AX_MEMR_EECLK;
	eeprom->reg_chip_select = reg & AX_MEMR_EECS;
}

static void ax_eeprom_register_write(struct eeprom_93cx6 *eeprom)
{
	struct ei_device *ei_local = eeprom->data;
	u8 reg = ei_inb(ei_local->mem + AX_MEMR);

	reg &= ~(AX_MEMR_EEI | AX_MEMR_EECLK | AX_MEMR_EECS);

	if (eeprom->reg_data_in)
		reg |= AX_MEMR_EEI;
	if (eeprom->reg_data_clock)
		reg |= AX_MEMR_EECLK;
	if (eeprom->reg_chip_select)
		reg |= AX_MEMR_EECS;

	ei_outb(reg, ei_local->mem + AX_MEMR);
	udelay(10);
}
#endif

static const struct net_device_ops ax_netdev_ops = {
	.ndo_open		= ax_open,
	.ndo_stop		= ax_close,
	.ndo_do_ioctl		= ax_ioctl,

	.ndo_start_xmit		= ax_ei_start_xmit,
	.ndo_tx_timeout		= ax_ei_tx_timeout,
	.ndo_get_stats		= ax_ei_get_stats,
	.ndo_set_multicast_list = ax_ei_set_multicast_list,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_set_mac_address 	= eth_mac_addr,
	.ndo_change_mtu		= eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= ax_ei_poll,
#endif
};

/* setup code */

static void ax_initial_setup(struct net_device *dev, struct ei_device *ei_local)
{
	void __iomem *ioaddr = ei_local->mem;
	struct ax_device *ax = to_ax_dev(dev);

	/* Select page 0*/
	ei_outb(E8390_NODMA+E8390_PAGE0+E8390_STOP, ioaddr + E8390_CMD);

	/* set to byte access */
	ei_outb(ax->plat->dcr_val & ~1, ioaddr + EN0_DCFG);
	ei_outb(ax->plat->gpoc_val, ioaddr + EI_SHIFT(0x17));
}

/* ax_init_dev
 *
 * initialise the specified device, taking care to note the MAC
 * address it may already have (if configured), ensure
 * the device is ready to be used by lib8390.c and registerd with
 * the network layer.
 */

static int ax_init_dev(struct net_device *dev, int first_init)
{
	struct ei_device *ei_local = netdev_priv(dev);
	struct ax_device *ax = to_ax_dev(dev);
	void __iomem *ioaddr = ei_local->mem;
	unsigned int start_page;
	unsigned int stop_page;
	int ret;
	int i;

	ret = ax_initial_check(dev);
	if (ret)
		goto err_out;

	/* setup goes here */

	ax_initial_setup(dev, ei_local);

	/* read the mac from the card prom if we need it */

	if (first_init && ax->plat->flags & AXFLG_HAS_EEPROM) {
		unsigned char SA_prom[32];

		for(i = 0; i < sizeof(SA_prom); i+=2) {
			SA_prom[i] = ei_inb(ioaddr + NE_DATAPORT);
			SA_prom[i+1] = ei_inb(ioaddr + NE_DATAPORT);
		}

		if (ax->plat->wordlength == 2)
			for (i = 0; i < 16; i++)
				SA_prom[i] = SA_prom[i+i];

		memcpy(dev->dev_addr,  SA_prom, 6);
	}

#ifdef CONFIG_AX88796_93CX6
	if (first_init && ax->plat->flags & AXFLG_HAS_93CX6) {
		unsigned char mac_addr[6];
		struct eeprom_93cx6 eeprom;

		eeprom.data = ei_local;
		eeprom.register_read = ax_eeprom_register_read;
		eeprom.register_write = ax_eeprom_register_write;
		eeprom.width = PCI_EEPROM_WIDTH_93C56;

		eeprom_93cx6_multiread(&eeprom, 0,
				       (__le16 __force *)mac_addr,
				       sizeof(mac_addr) >> 1);

		memcpy(dev->dev_addr,  mac_addr, 6);
	}
#endif
	if (ax->plat->wordlength == 2) {
		/* We must set the 8390 for word mode. */
		ei_outb(ax->plat->dcr_val, ei_local->mem + EN0_DCFG);
		start_page = NESM_START_PG;
		stop_page = NESM_STOP_PG;
	} else {
		start_page = NE1SM_START_PG;
		stop_page = NE1SM_STOP_PG;
	}

	/* load the mac-address from the device if this is the
	 * first time we've initialised */

	if (first_init) {
		if (ax->plat->flags & AXFLG_MAC_FROMDEV) {
			ei_outb(E8390_NODMA + E8390_PAGE1 + E8390_STOP,
				ei_local->mem + E8390_CMD); /* 0x61 */
			for (i = 0; i < ETHER_ADDR_LEN; i++)
				dev->dev_addr[i] =
					ei_inb(ioaddr + EN1_PHYS_SHIFT(i));
		}

		if ((ax->plat->flags & AXFLG_MAC_FROMPLATFORM) &&
		     ax->plat->mac_addr)
			memcpy(dev->dev_addr, ax->plat->mac_addr,
				ETHER_ADDR_LEN);
	}

	ax_reset_8390(dev);

	ei_status.name = "AX88796";
	ei_status.tx_start_page = start_page;
	ei_status.stop_page = stop_page;
	ei_status.word16 = (ax->plat->wordlength == 2);
	ei_status.rx_start_page = start_page + TX_PAGES;

#ifdef PACKETBUF_MEMSIZE
	 /* Allow the packet buffer size to be overridden by know-it-alls. */
	ei_status.stop_page = ei_status.tx_start_page + PACKETBUF_MEMSIZE;
#endif

	ei_status.reset_8390	= &ax_reset_8390;
	ei_status.block_input	= &ax_block_input;
	ei_status.block_output	= &ax_block_output;
	ei_status.get_8390_hdr	= &ax_get_8390_hdr;
	ei_status.priv = 0;

	dev->netdev_ops		= &ax_netdev_ops;
	dev->ethtool_ops	= &ax_ethtool_ops;

	ax->msg_enable		= NETIF_MSG_LINK;
	ax->mii.phy_id_mask	= 0x1f;
	ax->mii.reg_num_mask	= 0x1f;
	ax->mii.phy_id		= 0x10;		/* onboard phy */
	ax->mii.force_media	= 0;
	ax->mii.full_duplex	= 0;
	ax->mii.mdio_read	= ax_phy_read;
	ax->mii.mdio_write	= ax_phy_write;
	ax->mii.dev		= dev;

	ax_NS8390_init(dev, 0);

	if (first_init)
		dev_info(&ax->dev->dev, "%dbit, irq %d, %lx, MAC: %pM\n",
			 ei_status.word16 ? 16:8, dev->irq, dev->base_addr,
			 dev->dev_addr);

	ret = register_netdev(dev);
	if (ret)
		goto out_irq;

	return 0;

 out_irq:
	/* cleanup irq */
	free_irq(dev->irq, dev);
 err_out:
	return ret;
}

static int ax_remove(struct platform_device *_dev)
{
	struct net_device *dev = platform_get_drvdata(_dev);
	struct ax_device  *ax;

	ax = to_ax_dev(dev);

	unregister_netdev(dev);
	free_irq(dev->irq, dev);

	iounmap(ei_status.mem);
	release_resource(ax->mem);
	kfree(ax->mem);

	if (ax->map2) {
		iounmap(ax->map2);
		release_resource(ax->mem2);
		kfree(ax->mem2);
	}

	free_netdev(dev);

	return 0;
}

/* ax_probe
 *
 * This is the entry point when the platform device system uses to
 * notify us of a new device to attach to. Allocate memory, find
 * the resources and information passed, and map the necessary registers.
*/

static int ax_probe(struct platform_device *pdev)
{
	struct net_device *dev;
	struct ax_device  *ax;
	struct resource   *res;
	size_t size;
	int ret = 0;

	dev = ax__alloc_ei_netdev(sizeof(struct ax_device));
	if (dev == NULL)
		return -ENOMEM;

	/* ok, let's setup our device */
	ax = to_ax_dev(dev);

	memset(ax, 0, sizeof(struct ax_device));

	spin_lock_init(&ax->mii_lock);

	ax->dev = pdev;
	ax->plat = pdev->dev.platform_data;
	platform_set_drvdata(pdev, dev);

	ei_status.rxcr_base  = ax->plat->rcr_val;

	/* find the platform resources */

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no IRQ specified\n");
		ret = -ENXIO;
		goto exit_mem;
	}

	dev->irq = res->start;
	ax->irqflags = res->flags & IRQF_TRIGGER_MASK;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no MEM specified\n");
		ret = -ENXIO;
		goto exit_mem;
	}

	size = (res->end - res->start) + 1;

	/* setup the register offsets from either the platform data
	 * or by using the size of the resource provided */

	if (ax->plat->reg_offsets)
		ei_status.reg_offset = ax->plat->reg_offsets;
	else {
		ei_status.reg_offset = ax->reg_offsets;
		for (ret = 0; ret < 0x18; ret++)
			ax->reg_offsets[ret] = (size / 0x18) * ret;
	}

	ax->mem = request_mem_region(res->start, size, pdev->name);
	if (ax->mem == NULL) {
		dev_err(&pdev->dev, "cannot reserve registers\n");
 		ret = -ENXIO;
		goto exit_mem;
	}

	ei_status.mem = ioremap(res->start, size);
	dev->base_addr = (unsigned long)ei_status.mem;

	if (ei_status.mem == NULL) {
		dev_err(&pdev->dev, "Cannot ioremap area (%08llx,%08llx)\n",
			(unsigned long long)res->start,
			(unsigned long long)res->end);

 		ret = -ENXIO;
		goto exit_req;
	}

	/* look for reset area */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res == NULL) {
		if (!ax->plat->reg_offsets) {
			for (ret = 0; ret < 0x20; ret++)
				ax->reg_offsets[ret] = (size / 0x20) * ret;
		}

		ax->map2 = NULL;
	} else {
 		size = (res->end - res->start) + 1;

		ax->mem2 = request_mem_region(res->start, size, pdev->name);
		if (ax->mem2 == NULL) {
			dev_err(&pdev->dev, "cannot reserve registers\n");
			ret = -ENXIO;
			goto exit_mem1;
		}

		ax->map2 = ioremap(res->start, size);
		if (ax->map2 == NULL) {
			dev_err(&pdev->dev, "cannot map reset register\n");
			ret = -ENXIO;
			goto exit_mem2;
		}

		ei_status.reg_offset[0x1f] = ax->map2 - ei_status.mem;
	}

	/* got resources, now initialise and register device */

	ret = ax_init_dev(dev, 1);
	if (!ret)
		return 0;

	if (ax->map2 == NULL)
		goto exit_mem1;

	iounmap(ax->map2);

 exit_mem2:
	release_resource(ax->mem2);
	kfree(ax->mem2);

 exit_mem1:
	iounmap(ei_status.mem);

 exit_req:
	release_resource(ax->mem);
	kfree(ax->mem);

 exit_mem:
	free_netdev(dev);

	return ret;
}

/* suspend and resume */

#ifdef CONFIG_PM
static int ax_suspend(struct platform_device *dev, pm_message_t state)
{
	struct net_device *ndev = platform_get_drvdata(dev);
	struct ax_device  *ax = to_ax_dev(ndev);

	ax->resume_open = ax->running;

	netif_device_detach(ndev);
	ax_close(ndev);

	return 0;
}

static int ax_resume(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ax_device  *ax = to_ax_dev(ndev);

	ax_initial_setup(ndev, netdev_priv(ndev));
	ax_NS8390_init(ndev, ax->resume_open);
	netif_device_attach(ndev);

	if (ax->resume_open)
		ax_open(ndev);

	return 0;
}

#else
#define ax_suspend NULL
#define ax_resume  NULL
#endif

static struct platform_driver axdrv = {
	.driver	= {
		.name		= "ax88796",
		.owner		= THIS_MODULE,
	},
	.probe		= ax_probe,
	.remove		= ax_remove,
	.suspend	= ax_suspend,
	.resume		= ax_resume,
};

static int __init axdrv_init(void)
{
	return platform_driver_register(&axdrv);
}

static void __exit axdrv_exit(void)
{
	platform_driver_unregister(&axdrv);
}

module_init(axdrv_init);
module_exit(axdrv_exit);

MODULE_DESCRIPTION("AX88796 10/100 Ethernet platform driver");
MODULE_AUTHOR("Ben Dooks, <ben@simtec.co.uk>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:ax88796");
