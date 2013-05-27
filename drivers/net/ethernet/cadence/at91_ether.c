/*
 * Ethernet driver for the Atmel AT91RM9200 (Thunder)
 *
 *  Copyright (C) 2003 SAN People (Pty) Ltd
 *
 * Based on an earlier Atmel EMAC macrocell driver by Atmel and Lineo Inc.
 * Initial version by Rick Bronson 01/11/2003
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
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_net.h>
#include <linux/pinctrl/consumer.h>

#include "macb.h"

/* 1518 rounded up */
#define MAX_RBUFF_SZ	0x600
/* max number of receive buffers */
#define MAX_RX_DESCR	9

/* Initialize and start the Receiver and Transmit subsystems */
static int at91ether_start(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	dma_addr_t addr;
	u32 ctl;
	int i;

	lp->rx_ring = dma_alloc_coherent(&lp->pdev->dev,
					 (MAX_RX_DESCR *
					  sizeof(struct macb_dma_desc)),
					 &lp->rx_ring_dma, GFP_KERNEL);
	if (!lp->rx_ring)
		return -ENOMEM;

	lp->rx_buffers = dma_alloc_coherent(&lp->pdev->dev,
					    MAX_RX_DESCR * MAX_RBUFF_SZ,
					    &lp->rx_buffers_dma, GFP_KERNEL);
	if (!lp->rx_buffers) {
		dma_free_coherent(&lp->pdev->dev,
				  MAX_RX_DESCR * sizeof(struct macb_dma_desc),
				  lp->rx_ring, lp->rx_ring_dma);
		lp->rx_ring = NULL;
		return -ENOMEM;
	}

	addr = lp->rx_buffers_dma;
	for (i = 0; i < MAX_RX_DESCR; i++) {
		lp->rx_ring[i].addr = addr;
		lp->rx_ring[i].ctrl = 0;
		addr += MAX_RBUFF_SZ;
	}

	/* Set the Wrap bit on the last descriptor */
	lp->rx_ring[MAX_RX_DESCR - 1].addr |= MACB_BIT(RX_WRAP);

	/* Reset buffer index */
	lp->rx_tail = 0;

	/* Program address of descriptor list in Rx Buffer Queue register */
	macb_writel(lp, RBQP, lp->rx_ring_dma);

	/* Enable Receive and Transmit */
	ctl = macb_readl(lp, NCR);
	macb_writel(lp, NCR, ctl | MACB_BIT(RE) | MACB_BIT(TE));

	return 0;
}

/* Open the ethernet interface */
static int at91ether_open(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	u32 ctl;
	int ret;

	/* Clear internal statistics */
	ctl = macb_readl(lp, NCR);
	macb_writel(lp, NCR, ctl | MACB_BIT(CLRSTAT));

	macb_set_hwaddr(lp);

	ret = at91ether_start(dev);
	if (ret)
		return ret;

	/* Enable MAC interrupts */
	macb_writel(lp, IER, MACB_BIT(RCOMP)	|
			     MACB_BIT(RXUBR)	|
			     MACB_BIT(ISR_TUND)	|
			     MACB_BIT(ISR_RLE)	|
			     MACB_BIT(TCOMP)	|
			     MACB_BIT(ISR_ROVR)	|
			     MACB_BIT(HRESP));

	/* schedule a link state check */
	phy_start(lp->phy_dev);

	netif_start_queue(dev);

	return 0;
}

/* Close the interface */
static int at91ether_close(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	u32 ctl;

	/* Disable Receiver and Transmitter */
	ctl = macb_readl(lp, NCR);
	macb_writel(lp, NCR, ctl & ~(MACB_BIT(TE) | MACB_BIT(RE)));

	/* Disable MAC interrupts */
	macb_writel(lp, IDR, MACB_BIT(RCOMP)	|
			     MACB_BIT(RXUBR)	|
			     MACB_BIT(ISR_TUND)	|
			     MACB_BIT(ISR_RLE)	|
			     MACB_BIT(TCOMP)	|
			     MACB_BIT(ISR_ROVR) |
			     MACB_BIT(HRESP));

	netif_stop_queue(dev);

	dma_free_coherent(&lp->pdev->dev,
				MAX_RX_DESCR * sizeof(struct macb_dma_desc),
				lp->rx_ring, lp->rx_ring_dma);
	lp->rx_ring = NULL;

	dma_free_coherent(&lp->pdev->dev,
				MAX_RX_DESCR * MAX_RBUFF_SZ,
				lp->rx_buffers, lp->rx_buffers_dma);
	lp->rx_buffers = NULL;

	return 0;
}

/* Transmit packet */
static int at91ether_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);

	if (macb_readl(lp, TSR) & MACB_BIT(RM9200_BNQ)) {
		netif_stop_queue(dev);

		/* Store packet information (to free when Tx completed) */
		lp->skb = skb;
		lp->skb_length = skb->len;
		lp->skb_physaddr = dma_map_single(NULL, skb->data, skb->len,
							DMA_TO_DEVICE);

		/* Set address of the data in the Transmit Address register */
		macb_writel(lp, TAR, lp->skb_physaddr);
		/* Set length of the packet in the Transmit Control register */
		macb_writel(lp, TCR, skb->len);

	} else {
		netdev_err(dev, "%s called, but device is busy!\n", __func__);
		return NETDEV_TX_BUSY;
	}

	return NETDEV_TX_OK;
}

/* Extract received frame from buffer descriptors and sent to upper layers.
 * (Called from interrupt context)
 */
static void at91ether_rx(struct net_device *dev)
{
	struct macb *lp = netdev_priv(dev);
	unsigned char *p_recv;
	struct sk_buff *skb;
	unsigned int pktlen;

	while (lp->rx_ring[lp->rx_tail].addr & MACB_BIT(RX_USED)) {
		p_recv = lp->rx_buffers + lp->rx_tail * MAX_RBUFF_SZ;
		pktlen = MACB_BF(RX_FRMLEN, lp->rx_ring[lp->rx_tail].ctrl);
		skb = netdev_alloc_skb(dev, pktlen + 2);
		if (skb) {
			skb_reserve(skb, 2);
			memcpy(skb_put(skb, pktlen), p_recv, pktlen);

			skb->protocol = eth_type_trans(skb, dev);
			lp->stats.rx_packets++;
			lp->stats.rx_bytes += pktlen;
			netif_rx(skb);
		} else {
			lp->stats.rx_dropped++;
		}

		if (lp->rx_ring[lp->rx_tail].ctrl & MACB_BIT(RX_MHASH_MATCH))
			lp->stats.multicast++;

		/* reset ownership bit */
		lp->rx_ring[lp->rx_tail].addr &= ~MACB_BIT(RX_USED);

		/* wrap after last buffer */
		if (lp->rx_tail == MAX_RX_DESCR - 1)
			lp->rx_tail = 0;
		else
			lp->rx_tail++;
	}
}

/* MAC interrupt handler */
static irqreturn_t at91ether_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct macb *lp = netdev_priv(dev);
	u32 intstatus, ctl;

	/* MAC Interrupt Status register indicates what interrupts are pending.
	 * It is automatically cleared once read.
	 */
	intstatus = macb_readl(lp, ISR);

	/* Receive complete */
	if (intstatus & MACB_BIT(RCOMP))
		at91ether_rx(dev);

	/* Transmit complete */
	if (intstatus & MACB_BIT(TCOMP)) {
		/* The TCOM bit is set even if the transmission failed */
		if (intstatus & (MACB_BIT(ISR_TUND) | MACB_BIT(ISR_RLE)))
			lp->stats.tx_errors++;

		if (lp->skb) {
			dev_kfree_skb_irq(lp->skb);
			lp->skb = NULL;
			dma_unmap_single(NULL, lp->skb_physaddr, lp->skb_length, DMA_TO_DEVICE);
			lp->stats.tx_packets++;
			lp->stats.tx_bytes += lp->skb_length;
		}
		netif_wake_queue(dev);
	}

	/* Work-around for EMAC Errata section 41.3.1 */
	if (intstatus & MACB_BIT(RXUBR)) {
		ctl = macb_readl(lp, NCR);
		macb_writel(lp, NCR, ctl & ~MACB_BIT(RE));
		macb_writel(lp, NCR, ctl | MACB_BIT(RE));
	}

	if (intstatus & MACB_BIT(ISR_ROVR))
		netdev_err(dev, "ROVR error\n");

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
	.ndo_get_stats		= macb_get_stats,
	.ndo_set_rx_mode	= macb_set_rx_mode,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_do_ioctl		= macb_ioctl,
	.ndo_validate_addr	= eth_validate_addr,
	.ndo_change_mtu		= eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= at91ether_poll_controller,
#endif
};

#if defined(CONFIG_OF)
static const struct of_device_id at91ether_dt_ids[] = {
	{ .compatible = "cdns,at91rm9200-emac" },
	{ .compatible = "cdns,emac" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, at91ether_dt_ids);
#endif

/* Detect MAC & PHY and perform ethernet interface initialization */
static int __init at91ether_probe(struct platform_device *pdev)
{
	struct macb_platform_data *board_data = pdev->dev.platform_data;
	struct resource *regs;
	struct net_device *dev;
	struct phy_device *phydev;
	struct pinctrl *pinctrl;
	struct macb *lp;
	int res;
	u32 reg;
	const char *mac;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENOENT;

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		res = PTR_ERR(pinctrl);
		if (res == -EPROBE_DEFER)
			return res;

		dev_warn(&pdev->dev, "No pinctrl provided\n");
	}

	dev = alloc_etherdev(sizeof(struct macb));
	if (!dev)
		return -ENOMEM;

	lp = netdev_priv(dev);
	lp->pdev = pdev;
	lp->dev = dev;
	spin_lock_init(&lp->lock);

	/* physical base address */
	dev->base_addr = regs->start;
	lp->regs = devm_ioremap(&pdev->dev, regs->start, resource_size(regs));
	if (!lp->regs) {
		res = -ENOMEM;
		goto err_free_dev;
	}

	/* Clock */
	lp->pclk = devm_clk_get(&pdev->dev, "ether_clk");
	if (IS_ERR(lp->pclk)) {
		res = PTR_ERR(lp->pclk);
		goto err_free_dev;
	}
	clk_enable(lp->pclk);

	/* Install the interrupt handler */
	dev->irq = platform_get_irq(pdev, 0);
	res = devm_request_irq(&pdev->dev, dev->irq, at91ether_interrupt, 0, dev->name, dev);
	if (res)
		goto err_disable_clock;

	ether_setup(dev);
	dev->netdev_ops = &at91ether_netdev_ops;
	dev->ethtool_ops = &macb_ethtool_ops;
	platform_set_drvdata(pdev, dev);
	SET_NETDEV_DEV(dev, &pdev->dev);

	mac = of_get_mac_address(pdev->dev.of_node);
	if (mac)
		memcpy(lp->dev->dev_addr, mac, ETH_ALEN);
	else
		macb_get_hwaddr(lp);

	res = of_get_phy_mode(pdev->dev.of_node);
	if (res < 0) {
		if (board_data && board_data->is_rmii)
			lp->phy_interface = PHY_INTERFACE_MODE_RMII;
		else
			lp->phy_interface = PHY_INTERFACE_MODE_MII;
	} else {
		lp->phy_interface = res;
	}

	macb_writel(lp, NCR, 0);

	reg = MACB_BF(CLK, MACB_CLK_DIV32) | MACB_BIT(BIG);
	if (lp->phy_interface == PHY_INTERFACE_MODE_RMII)
		reg |= MACB_BIT(RM9200_RMII);

	macb_writel(lp, NCFGR, reg);

	/* Register the network interface */
	res = register_netdev(dev);
	if (res)
		goto err_disable_clock;

	res = macb_mii_init(lp);
	if (res)
		goto err_out_unregister_netdev;

	/* will be enabled in open() */
	netif_carrier_off(dev);

	phydev = lp->phy_dev;
	netdev_info(dev, "attached PHY driver [%s] (mii_bus:phy_addr=%s, irq=%d)\n",
				phydev->drv->name, dev_name(&phydev->dev),
				phydev->irq);

	/* Display ethernet banner */
	netdev_info(dev, "AT91 ethernet at 0x%08lx int=%d (%pM)\n",
				dev->base_addr, dev->irq, dev->dev_addr);

	return 0;

err_out_unregister_netdev:
	unregister_netdev(dev);
err_disable_clock:
	clk_disable(lp->pclk);
err_free_dev:
	free_netdev(dev);
	return res;
}

static int at91ether_remove(struct platform_device *pdev)
{
	struct net_device *dev = platform_get_drvdata(pdev);
	struct macb *lp = netdev_priv(dev);

	if (lp->phy_dev)
		phy_disconnect(lp->phy_dev);

	mdiobus_unregister(lp->mii_bus);
	kfree(lp->mii_bus->irq);
	mdiobus_free(lp->mii_bus);
	unregister_netdev(dev);
	clk_disable(lp->pclk);
	free_netdev(dev);

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
	.remove		= at91ether_remove,
	.suspend	= at91ether_suspend,
	.resume		= at91ether_resume,
	.driver		= {
		.name	= "at91_ether",
		.owner	= THIS_MODULE,
		.of_match_table	= of_match_ptr(at91ether_dt_ids),
	},
};

module_platform_driver_probe(at91ether_driver, at91ether_probe);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("AT91RM9200 EMAC Ethernet driver");
MODULE_AUTHOR("Andrew Victor");
MODULE_ALIAS("platform:at91_ether");
