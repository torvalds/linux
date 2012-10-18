/*
 * Ethernet driver for the Atmel AT91RM9200 (Thunder)
 *
 *  Copyright (C) 2003 SAN People (Pty) Ltd
 *
 * Based on an earlier Atmel EMAC macrocell driver by Atmel and Lineo Inc.
 * Initial version by Rick Bronson 01/11/2003
 *
 * Intel LXT971A PHY support by Christopher Bahns & David Knickerbocker
 *   (Polaroid Corporation)
 *
 * Realtek RTL8201(B)L PHY support by Roman Avramenko <roman@imsystems.ru>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/ethtool.h>
#include <linux/platform_data/macb.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/gfp.h>
#include <linux/phy.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/mach-types.h>

#include "macb.h"

#define DRV_NAME	"at91_ether"
#define DRV_VERSION	"1.0"

/* ......................... ADDRESS MANAGEMENT ........................ */

/*
 * NOTE: Your bootloader must always set the MAC address correctly before
 * booting into Linux.
 *
 * - It must always set the MAC address after reset, even if it doesn't
 *   happen to access the Ethernet while it's booting.  Some versions of
 *   U-Boot on the AT91RM9200-DK do not do this.
 *
 * - Likewise it must store the addresses in the correct byte order.
 *   MicroMonitor (uMon) on the CSB337 does this incorrectly (and
 *   continues to do so, for bug-compatibility).
 */

static short __init unpack_mac_address(struct net_device *dev, unsigned int hi, unsigned int lo)
{
	char addr[6];

	if (machine_is_csb337()) {
		addr[5] = (lo & 0xff);			/* The CSB337 bootloader stores the MAC the wrong-way around */
		addr[4] = (lo & 0xff00) >> 8;
		addr[3] = (lo & 0xff0000) >> 16;
		addr[2] = (lo & 0xff000000) >> 24;
		addr[1] = (hi & 0xff);
		addr[0] = (hi & 0xff00) >> 8;
	}
	else {
		addr[0] = (lo & 0xff);
		addr[1] = (lo & 0xff00) >> 8;
		addr[2] = (lo & 0xff0000) >> 16;
		addr[3] = (lo & 0xff000000) >> 24;
		addr[4] = (hi & 0xff);
		addr[5] = (hi & 0xff00) >> 8;
	}

	if (is_valid_ether_addr(addr)) {
		memcpy(dev->dev_addr, &addr, 6);
		return 1;
	}
	return 0;
}

/*
 * Set the ethernet MAC address in dev->dev_addr
 */
static void __init get_mac_address(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);

	/* Check Specific-Address 1 */
	if (unpack_mac_address(dev, macb_readl(lp, SA1T), macb_readl(lp, SA1B)))
		return;
	/* Check Specific-Address 2 */
	if (unpack_mac_address(dev, macb_readl(lp, SA2T), macb_readl(lp, SA2B)))
		return;
	/* Check Specific-Address 3 */
	if (unpack_mac_address(dev, macb_readl(lp, SA3T), macb_readl(lp, SA3B)))
		return;
	/* Check Specific-Address 4 */
	if (unpack_mac_address(dev, macb_readl(lp, SA4T), macb_readl(lp, SA4B)))
		return;

	printk(KERN_ERR "at91_ether: Your bootloader did not configure a MAC address.\n");
}

/*
 * Program the hardware MAC address from dev->dev_addr.
 */
static void update_mac_address(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);

	macb_writel(lp, SA1B, (dev->dev_addr[3] << 24) | (dev->dev_addr[2] << 16)
					| (dev->dev_addr[1] << 8) | (dev->dev_addr[0]));
	macb_writel(lp, SA1T, (dev->dev_addr[5] << 8) | (dev->dev_addr[4]));

	macb_writel(lp, SA2B, 0);
	macb_writel(lp, SA2T, 0);
}

/*
 * Store the new hardware address in dev->dev_addr, and update the MAC.
 */
static int set_mac_address(struct net_device *dev, void* addr)
{
	struct sockaddr *address = addr;

	if (!is_valid_ether_addr(address->sa_data))
		return -EADDRNOTAVAIL;

	memcpy(dev->dev_addr, address->sa_data, dev->addr_len);
	update_mac_address(dev);

	printk("%s: Setting MAC address to %pM\n", dev->name,
	       dev->dev_addr);

	return 0;
}

static int inline hash_bit_value(int bitnr, __u8 *addr)
{
	if (addr[bitnr / 8] & (1 << (bitnr % 8)))
		return 1;
	return 0;
}

/*
 * The hash address register is 64 bits long and takes up two locations in the memory map.
 * The least significant bits are stored in EMAC_HSL and the most significant
 * bits in EMAC_HSH.
 *
 * The unicast hash enable and the multicast hash enable bits in the network configuration
 *  register enable the reception of hash matched frames. The destination address is
 *  reduced to a 6 bit index into the 64 bit hash register using the following hash function.
 * The hash function is an exclusive or of every sixth bit of the destination address.
 *   hash_index[5] = da[5] ^ da[11] ^ da[17] ^ da[23] ^ da[29] ^ da[35] ^ da[41] ^ da[47]
 *   hash_index[4] = da[4] ^ da[10] ^ da[16] ^ da[22] ^ da[28] ^ da[34] ^ da[40] ^ da[46]
 *   hash_index[3] = da[3] ^ da[09] ^ da[15] ^ da[21] ^ da[27] ^ da[33] ^ da[39] ^ da[45]
 *   hash_index[2] = da[2] ^ da[08] ^ da[14] ^ da[20] ^ da[26] ^ da[32] ^ da[38] ^ da[44]
 *   hash_index[1] = da[1] ^ da[07] ^ da[13] ^ da[19] ^ da[25] ^ da[31] ^ da[37] ^ da[43]
 *   hash_index[0] = da[0] ^ da[06] ^ da[12] ^ da[18] ^ da[24] ^ da[30] ^ da[36] ^ da[42]
 * da[0] represents the least significant bit of the first byte received, that is, the multicast/
 *  unicast indicator, and da[47] represents the most significant bit of the last byte
 *  received.
 * If the hash index points to a bit that is set in the hash register then the frame will be
 *  matched according to whether the frame is multicast or unicast.
 * A multicast match will be signalled if the multicast hash enable bit is set, da[0] is 1 and
 *  the hash index points to a bit set in the hash register.
 * A unicast match will be signalled if the unicast hash enable bit is set, da[0] is 0 and the
 *  hash index points to a bit set in the hash register.
 * To receive all multicast frames, the hash register should be set with all ones and the
 *  multicast hash enable bit should be set in the network configuration register.
 */

/*
 * Return the hash index value for the specified address.
 */
static int hash_get_index(__u8 *addr)
{
	int i, j, bitval;
	int hash_index = 0;

	for (j = 0; j < 6; j++) {
		for (i = 0, bitval = 0; i < 8; i++)
			bitval ^= hash_bit_value(i*6 + j, addr);

		hash_index |= (bitval << j);
	}

	return hash_index;
}

/*
 * Add multicast addresses to the internal multicast-hash table.
 */
static void at91ether_sethashtable(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	struct netdev_hw_addr *ha;
	unsigned long mc_filter[2];
	unsigned int bitnr;

	mc_filter[0] = mc_filter[1] = 0;

	netdev_for_each_mc_addr(ha, dev) {
		bitnr = hash_get_index(ha->addr);
		mc_filter[bitnr >> 5] |= 1 << (bitnr & 31);
	}

	macb_writel(lp, HRB, mc_filter[0]);
	macb_writel(lp, HRT, mc_filter[1]);
}

/*
 * Enable/Disable promiscuous and multicast modes.
 */
static void at91ether_set_multicast_list(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	unsigned long cfg;

	cfg = macb_readl(lp, NCFGR);

	if (dev->flags & IFF_PROMISC)			/* Enable promiscuous mode */
		cfg |= MACB_BIT(CAF);
	else if (dev->flags & (~IFF_PROMISC))		/* Disable promiscuous mode */
		cfg &= ~MACB_BIT(CAF);

	if (dev->flags & IFF_ALLMULTI) {		/* Enable all multicast mode */
		macb_writel(lp, HRT, -1);
		macb_writel(lp, HRB, -1);
		cfg |= MACB_BIT(NCFGR_MTI);
	} else if (!netdev_mc_empty(dev)) { /* Enable specific multicasts */
		at91ether_sethashtable(dev);
		cfg |= MACB_BIT(NCFGR_MTI);
	} else if (dev->flags & (~IFF_ALLMULTI)) {	/* Disable all multicast mode */
		macb_writel(lp, HRT, 0);
		macb_writel(lp, HRB, 0);
		cfg &= ~MACB_BIT(NCFGR_MTI);
	}

	macb_writel(lp, NCFGR, cfg);
}

/* ................................ MAC ................................ */

/*
 * Initialize and start the Receiver and Transmit subsystems
 */
static void at91ether_start(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	struct recv_desc_bufs *dlist, *dlist_phys;
	int i;
	unsigned long ctl;

	dlist = lp->dlist;
	dlist_phys = lp->dlist_phys;

	for (i = 0; i < MAX_RX_DESCR; i++) {
		dlist->descriptors[i].addr = (unsigned int) &dlist_phys->recv_buf[i][0];
		dlist->descriptors[i].size = 0;
	}

	/* Set the Wrap bit on the last descriptor */
	dlist->descriptors[i-1].addr |= MACB_BIT(RX_WRAP);

	/* Reset buffer index */
	lp->rxBuffIndex = 0;

	/* Program address of descriptor list in Rx Buffer Queue register */
	macb_writel(lp, RBQP, (unsigned long) dlist_phys);

	/* Enable Receive and Transmit */
	ctl = macb_readl(lp, NCR);
	macb_writel(lp, NCR, ctl | MACB_BIT(RE) | MACB_BIT(TE));
}

/*
 * Open the ethernet interface
 */
static int at91ether_open(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	unsigned long ctl;

	if (!is_valid_ether_addr(dev->dev_addr))
		return -EADDRNOTAVAIL;

	/* Clear internal statistics */
	ctl = macb_readl(lp, NCR);
	macb_writel(lp, NCR, ctl | MACB_BIT(CLRSTAT));

	/* Update the MAC address (incase user has changed it) */
	update_mac_address(dev);

	/* Enable MAC interrupts */
	macb_writel(lp, IER, MACB_BIT(RCOMP) | MACB_BIT(RXUBR)
				| MACB_BIT(ISR_TUND) | MACB_BIT(ISR_RLE) | MACB_BIT(TCOMP)
				| MACB_BIT(ISR_ROVR) | MACB_BIT(HRESP));

	at91ether_start(dev);

	/* schedule a link state check */
	phy_start(lp->phy_dev);

	netif_start_queue(dev);

	return 0;
}

/*
 * Close the interface
 */
static int at91ether_close(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	unsigned long ctl;

	/* Disable Receiver and Transmitter */
	ctl = macb_readl(lp, NCR);
	macb_writel(lp, NCR, ctl & ~(MACB_BIT(TE) | MACB_BIT(RE)));

	/* Disable MAC interrupts */
	macb_writel(lp, IDR, MACB_BIT(RCOMP) | MACB_BIT(RXUBR)
				| MACB_BIT(ISR_TUND) | MACB_BIT(ISR_RLE)
				| MACB_BIT(TCOMP) | MACB_BIT(ISR_ROVR)
				| MACB_BIT(HRESP));

	netif_stop_queue(dev);

	return 0;
}

/*
 * Transmit packet.
 */
static int at91ether_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);

	if (macb_readl(lp, TSR) & MACB_BIT(RM9200_BNQ)) {
		netif_stop_queue(dev);

		/* Store packet information (to free when Tx completed) */
		lp->skb = skb;
		lp->skb_length = skb->len;
		lp->skb_physaddr = dma_map_single(NULL, skb->data, skb->len, DMA_TO_DEVICE);
		dev->stats.tx_bytes += skb->len;

		/* Set address of the data in the Transmit Address register */
		macb_writel(lp, TAR, lp->skb_physaddr);
		/* Set length of the packet in the Transmit Control register */
		macb_writel(lp, TCR, skb->len);

	} else {
		printk(KERN_ERR "at91_ether.c: at91ether_start_xmit() called, but device is busy!\n");
		return NETDEV_TX_BUSY;	/* if we return anything but zero, dev.c:1055 calls kfree_skb(skb)
				on this skb, he also reports -ENETDOWN and printk's, so either
				we free and return(0) or don't free and return 1 */
	}

	return NETDEV_TX_OK;
}

/*
 * Update the current statistics from the internal statistics registers.
 */
static struct net_device_stats *at91ether_stats(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	int ale, lenerr, seqe, lcol, ecol;

	if (netif_running(dev)) {
		dev->stats.rx_packets += macb_readl(lp, FRO);	/* Good frames received */
		ale = macb_readl(lp, ALE);
		dev->stats.rx_frame_errors += ale;				/* Alignment errors */
		lenerr = macb_readl(lp, ELE) + macb_readl(lp, USF);
		dev->stats.rx_length_errors += lenerr;				/* Excessive Length or Undersize Frame error */
		seqe = macb_readl(lp, FCSE);
		dev->stats.rx_crc_errors += seqe;				/* CRC error */
		dev->stats.rx_fifo_errors += macb_readl(lp, RRE);/* Receive buffer not available */
		dev->stats.rx_errors += (ale + lenerr + seqe
			+ macb_readl(lp, RSE) + macb_readl(lp, RJA));

		dev->stats.tx_packets += macb_readl(lp, FTO);	/* Frames successfully transmitted */
		dev->stats.tx_fifo_errors += macb_readl(lp, TUND);	/* Transmit FIFO underruns */
		dev->stats.tx_carrier_errors += macb_readl(lp, CSE);	/* Carrier Sense errors */
		dev->stats.tx_heartbeat_errors += macb_readl(lp, STE);/* Heartbeat error */

		lcol = macb_readl(lp, LCOL);
		ecol = macb_readl(lp, EXCOL);
		dev->stats.tx_window_errors += lcol;			/* Late collisions */
		dev->stats.tx_aborted_errors += ecol;			/* 16 collisions */

		dev->stats.collisions += (macb_readl(lp, SCF) + macb_readl(lp, MCF) + lcol + ecol);
	}
	return &dev->stats;
}

/*
 * Extract received frame from buffer descriptors and sent to upper layers.
 * (Called from interrupt context)
 */
static void at91ether_rx(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	struct recv_desc_bufs *dlist;
	unsigned char *p_recv;
	struct sk_buff *skb;
	unsigned int pktlen;

	dlist = lp->dlist;
	while (dlist->descriptors[lp->rxBuffIndex].addr & MACB_BIT(RX_USED)) {
		p_recv = dlist->recv_buf[lp->rxBuffIndex];
		pktlen = dlist->descriptors[lp->rxBuffIndex].size & 0x7ff;	/* Length of frame including FCS */
		skb = netdev_alloc_skb(dev, pktlen + 2);
		if (skb != NULL) {
			skb_reserve(skb, 2);
			memcpy(skb_put(skb, pktlen), p_recv, pktlen);

			skb->protocol = eth_type_trans(skb, dev);
			dev->stats.rx_bytes += pktlen;
			netif_rx(skb);
		}
		else {
			dev->stats.rx_dropped += 1;
			printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n", dev->name);
		}

		if (dlist->descriptors[lp->rxBuffIndex].size & MACB_BIT(RX_MHASH_MATCH))
			dev->stats.multicast++;

		dlist->descriptors[lp->rxBuffIndex].addr &= ~MACB_BIT(RX_USED);	/* reset ownership bit */
		if (lp->rxBuffIndex == MAX_RX_DESCR-1)				/* wrap after last buffer */
			lp->rxBuffIndex = 0;
		else
			lp->rxBuffIndex++;
	}
}

/*
 * MAC interrupt handler
 */
static irqreturn_t at91ether_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct macb *lp = netdev_priv(dev);
	unsigned long intstatus, ctl;

	/* MAC Interrupt Status register indicates what interrupts are pending.
	   It is automatically cleared once read. */
	intstatus = macb_readl(lp, ISR);

	if (intstatus & MACB_BIT(RCOMP))		/* Receive complete */
		at91ether_rx(dev);

	if (intstatus & MACB_BIT(TCOMP)) {	/* Transmit complete */
		/* The TCOM bit is set even if the transmission failed. */
		if (intstatus & (MACB_BIT(ISR_TUND) | MACB_BIT(ISR_RLE)))
			dev->stats.tx_errors += 1;

		if (lp->skb) {
			dev_kfree_skb_irq(lp->skb);
			lp->skb = NULL;
			dma_unmap_single(NULL, lp->skb_physaddr, lp->skb_length, DMA_TO_DEVICE);
		}
		netif_wake_queue(dev);
	}

	/* Work-around for Errata #11 */
	if (intstatus & MACB_BIT(RXUBR)) {
		ctl = macb_readl(lp, NCR);
		macb_writel(lp, NCR, ctl & ~MACB_BIT(RE));
		macb_writel(lp, NCR, ctl | MACB_BIT(RE));
	}

	if (intstatus & MACB_BIT(ISR_ROVR))
		printk("%s: ROVR error\n", dev->name);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void at91ether_poll_controller(struct net_device *dev)
{
	unsigned long flags;

	local_irq_save(flags);
	at91ether_interrupt(dev->irq, dev);
	local_irq_restore(flags);
}
#endif

static const struct net_device_ops at91ether_netdev_ops = {
	.ndo_open		= at91ether_open,
	.ndo_stop		= at91ether_close,
	.ndo_start_xmit		= at91ether_start_xmit,
	.ndo_get_stats		= at91ether_stats,
	.ndo_set_rx_mode	= at91ether_set_multicast_list,
	.ndo_set_mac_address	= set_mac_address,
	.ndo_do_ioctl		= macb_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= at91ether_poll_controller,
#endif
};

/*
 * Detect MAC & PHY and perform ethernet interface initialization
 */
static int __init at91ether_probe(struct platform_device *pdev)
{
	struct macb_platform_data *board_data = pdev->dev.platform_data;
	struct resource *regs;
	struct net_device *dev;
	struct phy_device *phydev;
	struct macb *lp;
	int res;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENOENT;

	dev = alloc_etherdev(sizeof(struct macb));
	if (!dev)
		return -ENOMEM;

	lp = netdev_priv(dev);
	lp->pdev = pdev;
	lp->dev = dev;
	lp->board_data = *board_data;
	spin_lock_init(&lp->lock);

	dev->base_addr = regs->start;		/* physical base address */
	lp->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!lp->regs) {
		res = -ENOMEM;
		goto err_free_dev;
	}

	/* Clock */
	lp->pclk = clk_get(&pdev->dev, "ether_clk");
	if (IS_ERR(lp->pclk)) {
		res = PTR_ERR(lp->pclk);
		goto err_ioumap;
	}
	clk_enable(lp->pclk);

	/* Install the interrupt handler */
	dev->irq = platform_get_irq(pdev, 0);
	if (request_irq(dev->irq, at91ether_interrupt, 0, dev->name, dev)) {
		res = -EBUSY;
		goto err_disable_clock;
	}

	/* Allocate memory for DMA Receive descriptors */
	lp->dlist = (struct recv_desc_bufs *) dma_alloc_coherent(NULL, sizeof(struct recv_desc_bufs), (dma_addr_t *) &lp->dlist_phys, GFP_KERNEL);
	if (lp->dlist == NULL) {
		res = -ENOMEM;
		goto err_free_irq;
	}

	ether_setup(dev);
	dev->netdev_ops = &at91ether_netdev_ops;
	dev->ethtool_ops = &macb_ethtool_ops;
	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	get_mac_address(dev);		/* Get ethernet address and store it in dev->dev_addr */
	update_mac_address(dev);	/* Program ethernet address into MAC */

	macb_writel(lp, NCR, 0);

	if (board_data->is_rmii) {
		macb_writel(lp, NCFGR, MACB_BF(CLK, MACB_CLK_DIV32) | MACB_BIT(BIG) | MACB_BIT(RM9200_RMII));
		lp->phy_interface = PHY_INTERFACE_MODE_RMII;
	} else {
		macb_writel(lp, NCFGR, MACB_BF(CLK, MACB_CLK_DIV32) | MACB_BIT(BIG));
		lp->phy_interface = PHY_INTERFACE_MODE_MII;
	}

	/* Register the network interface */
	res = register_netdev(dev);
	if (res)
		goto err_free_dmamem;

	if (macb_mii_init(lp) != 0)
		goto err_out_unregister_netdev;

	netif_carrier_off(dev);		/* will be enabled in open() */

	phydev = lp->phy_dev;
	netdev_info(dev, "attached PHY driver [%s] (mii_bus:phy_addr=%s, irq=%d)\n",
		phydev->drv->name, dev_name(&phydev->dev), phydev->irq);

	/* Display ethernet banner */
	printk(KERN_INFO "%s: AT91 ethernet at 0x%08x int=%d %s%s (%pM)\n",
	       dev->name, (uint) dev->base_addr, dev->irq,
	       macb_readl(lp, NCFGR) & MACB_BIT(SPD) ? "100-" : "10-",
	       macb_readl(lp, NCFGR) & MACB_BIT(FD) ? "FullDuplex" : "HalfDuplex",
	       dev->dev_addr);

	return 0;

err_out_unregister_netdev:
	unregister_netdev(dev);
err_free_dmamem:
	platform_set_drvdata(pdev, NULL);
	dma_free_coherent(NULL, sizeof(struct recv_desc_bufs), lp->dlist, (dma_addr_t)lp->dlist_phys);
err_free_irq:
	free_irq(dev->irq, dev);
err_disable_clock:
	clk_disable(lp->pclk);
	clk_put(lp->pclk);
err_ioumap:
	iounmap(lp->regs);
err_free_dev:
	free_netdev(dev);
	return res;
}

static int __devexit at91ether_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct macb *lp = netdev_priv(dev);

	if (lp->phy_dev)
		phy_disconnect(lp->phy_dev);

	mdiobus_unregister(lp->mii_bus);
	kfree(lp->mii_bus->irq);
	mdiobus_free(lp->mii_bus);
	unregister_netdev(dev);
	free_irq(dev->irq, dev);
	dma_free_coherent(NULL, sizeof(struct recv_desc_bufs), lp->dlist, (dma_addr_t)lp->dlist_phys);
	iounmap(lp->regs);
	clk_disable(lp->pclk);
	clk_put(lp->pclk);
	free_netdev(dev);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM

static int at91ether_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	struct net_device *net_dev = platform_get_drvdata(pdev);
	struct macb *lp = netdev_priv(net_dev);

	if (netif_running(net_dev)) {
		netif_stop_queue(net_dev);
		netif_device_detach(net_dev);

		clk_disable(lp->pclk);
	}
	return 0;
}

static int at91ether_resume(struct platform_device *pdev)
{
	struct net_device *net_dev = platform_get_drvdata(pdev);
	struct macb *lp = netdev_priv(net_dev);

	if (netif_running(net_dev)) {
		clk_enable(lp->pclk);

		netif_device_attach(net_dev);
		netif_start_queue(net_dev);
	}
	return 0;
}

#else
#define at91ether_suspend	NULL
#define at91ether_resume	NULL
#endif

static struct platform_driver at91ether_driver = {
	.remove		= __devexit_p(at91ether_remove),
	.suspend	= at91ether_suspend,
	.resume		= at91ether_resume,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init at91ether_init(void)
{
	return platform_driver_probe(&at91ether_driver, at91ether_probe);
}

static void __exit at91ether_exit(void)
{
	platform_driver_unregister(&at91ether_driver);
}

module_init(at91ether_init)
module_exit(at91ether_exit)

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AT91RM9200 EMAC Ethernet driver");
MODULE_AUTHOR("Andrew Victor");
MODULE_ALIAS("platform:" DRV_NAME);
