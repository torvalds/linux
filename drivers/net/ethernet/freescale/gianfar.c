/* drivers/net/ethernet/freescale/gianfar.c
 *
 * Gianfar Ethernet Driver
 * This driver is designed for the non-CPM ethernet controllers
 * on the 85xx and 83xx family of integrated processors
 * Based on 8260_io/fcc_enet.c
 *
 * Author: Andy Fleming
 * Maintainer: Kumar Gala
 * Modifier: Sandeep Gopalpet <sandeep.kumar@freescale.com>
 *
 * Copyright 2002-2009, 2011-2013 Freescale Semiconductor, Inc.
 * Copyright 2007 MontaVista Software, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 *  Gianfar:  AKA Lambda Draconis, "Dragon"
 *  RA 11 31 24.2
 *  Dec +69 19 52
 *  V 3.84
 *  B-V +1.62
 *
 *  Theory of operation
 *
 *  The driver is initialized through of_device. Configuration information
 *  is therefore conveyed through an OF-style device tree.
 *
 *  The Gianfar Ethernet Controller uses a ring of buffer
 *  descriptors.  The beginning is indicated by a register
 *  pointing to the physical address of the start of the ring.
 *  The end is determined by a "wrap" bit being set in the
 *  last descriptor of the ring.
 *
 *  When a packet is received, the RXF bit in the
 *  IEVENT register is set, triggering an interrupt when the
 *  corresponding bit in the IMASK register is also set (if
 *  interrupt coalescing is active, then the interrupt may not
 *  happen immediately, but will wait until either a set number
 *  of frames or amount of time have passed).  In NAPI, the
 *  interrupt handler will signal there is work to be done, and
 *  exit. This method will start at the last known empty
 *  descriptor, and process every subsequent descriptor until there
 *  are none left with data (NAPI will stop after a set number of
 *  packets to give time to other tasks, but will eventually
 *  process all the packets).  The data arrives inside a
 *  pre-allocated skb, and so after the skb is passed up to the
 *  stack, a new skb must be allocated, and the address field in
 *  the buffer descriptor must be updated to indicate this new
 *  skb.
 *
 *  When the kernel requests that a packet be transmitted, the
 *  driver starts where it left off last time, and points the
 *  descriptor at the buffer which was passed in.  The driver
 *  then informs the DMA engine that there are packets ready to
 *  be transmitted.  Once the controller is finished transmitting
 *  the packet, an interrupt may be triggered (under the same
 *  conditions as for reception, but depending on the TXF bit).
 *  The driver then cleans up the buffer.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#define DEBUG

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_vlan.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_mdio.h>
#include <linux/of_platform.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <linux/net_tstamp.h>

#include <asm/io.h>
#ifdef CONFIG_PPC
#include <asm/reg.h>
#include <asm/mpc85xx.h>
#endif
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>

#include "gianfar.h"

#define TX_TIMEOUT      (1*HZ)

const char gfar_driver_version[] = "1.3";

static int gfar_enet_open(struct net_device *dev);
static int gfar_start_xmit(struct sk_buff *skb, struct net_device *dev);
static void gfar_reset_task(struct work_struct *work);
static void gfar_timeout(struct net_device *dev);
static int gfar_close(struct net_device *dev);
static struct sk_buff *gfar_new_skb(struct net_device *dev,
				    dma_addr_t *bufaddr);
static int gfar_set_mac_address(struct net_device *dev);
static int gfar_change_mtu(struct net_device *dev, int new_mtu);
static irqreturn_t gfar_error(int irq, void *dev_id);
static irqreturn_t gfar_transmit(int irq, void *dev_id);
static irqreturn_t gfar_interrupt(int irq, void *dev_id);
static void adjust_link(struct net_device *dev);
static noinline void gfar_update_link_state(struct gfar_private *priv);
static int init_phy(struct net_device *dev);
static int gfar_probe(struct platform_device *ofdev);
static int gfar_remove(struct platform_device *ofdev);
static void free_skb_resources(struct gfar_private *priv);
static void gfar_set_multi(struct net_device *dev);
static void gfar_set_hash_for_addr(struct net_device *dev, u8 *addr);
static void gfar_configure_serdes(struct net_device *dev);
static int gfar_poll_rx(struct napi_struct *napi, int budget);
static int gfar_poll_tx(struct napi_struct *napi, int budget);
static int gfar_poll_rx_sq(struct napi_struct *napi, int budget);
static int gfar_poll_tx_sq(struct napi_struct *napi, int budget);
#ifdef CONFIG_NET_POLL_CONTROLLER
static void gfar_netpoll(struct net_device *dev);
#endif
int gfar_clean_rx_ring(struct gfar_priv_rx_q *rx_queue, int rx_work_limit);
static void gfar_clean_tx_ring(struct gfar_priv_tx_q *tx_queue);
static void gfar_process_frame(struct net_device *dev, struct sk_buff *skb,
			       int amount_pull, struct napi_struct *napi);
static void gfar_halt_nodisable(struct gfar_private *priv);
static void gfar_clear_exact_match(struct net_device *dev);
static void gfar_set_mac_for_addr(struct net_device *dev, int num,
				  const u8 *addr);
static int gfar_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);

MODULE_AUTHOR("Freescale Semiconductor, Inc");
MODULE_DESCRIPTION("Gianfar Ethernet Driver");
MODULE_LICENSE("GPL");

static void gfar_init_rxbdp(struct gfar_priv_rx_q *rx_queue, struct rxbd8 *bdp,
			    dma_addr_t buf)
{
	u32 lstatus;

	bdp->bufPtr = buf;

	lstatus = BD_LFLAG(RXBD_EMPTY | RXBD_INTERRUPT);
	if (bdp == rx_queue->rx_bd_base + rx_queue->rx_ring_size - 1)
		lstatus |= BD_LFLAG(RXBD_WRAP);

	gfar_wmb();

	bdp->lstatus = lstatus;
}

static int gfar_init_bds(struct net_device *ndev)
{
	struct gfar_private *priv = netdev_priv(ndev);
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	struct gfar_priv_tx_q *tx_queue = NULL;
	struct gfar_priv_rx_q *rx_queue = NULL;
	struct txbd8 *txbdp;
	struct rxbd8 *rxbdp;
	u32 __iomem *rfbptr;
	int i, j;
	dma_addr_t bufaddr;

	for (i = 0; i < priv->num_tx_queues; i++) {
		tx_queue = priv->tx_queue[i];
		/* Initialize some variables in our dev structure */
		tx_queue->num_txbdfree = tx_queue->tx_ring_size;
		tx_queue->dirty_tx = tx_queue->tx_bd_base;
		tx_queue->cur_tx = tx_queue->tx_bd_base;
		tx_queue->skb_curtx = 0;
		tx_queue->skb_dirtytx = 0;

		/* Initialize Transmit Descriptor Ring */
		txbdp = tx_queue->tx_bd_base;
		for (j = 0; j < tx_queue->tx_ring_size; j++) {
			txbdp->lstatus = 0;
			txbdp->bufPtr = 0;
			txbdp++;
		}

		/* Set the last descriptor in the ring to indicate wrap */
		txbdp--;
		txbdp->status |= TXBD_WRAP;
	}

	rfbptr = &regs->rfbptr0;
	for (i = 0; i < priv->num_rx_queues; i++) {
		rx_queue = priv->rx_queue[i];
		rx_queue->cur_rx = rx_queue->rx_bd_base;
		rx_queue->skb_currx = 0;
		rxbdp = rx_queue->rx_bd_base;

		for (j = 0; j < rx_queue->rx_ring_size; j++) {
			struct sk_buff *skb = rx_queue->rx_skbuff[j];

			if (skb) {
				bufaddr = rxbdp->bufPtr;
			} else {
				skb = gfar_new_skb(ndev, &bufaddr);
				if (!skb) {
					netdev_err(ndev, "Can't allocate RX buffers\n");
					return -ENOMEM;
				}
				rx_queue->rx_skbuff[j] = skb;
			}

			gfar_init_rxbdp(rx_queue, rxbdp, bufaddr);
			rxbdp++;
		}

		rx_queue->rfbptr = rfbptr;
		rfbptr += 2;
	}

	return 0;
}

static int gfar_alloc_skb_resources(struct net_device *ndev)
{
	void *vaddr;
	dma_addr_t addr;
	int i, j, k;
	struct gfar_private *priv = netdev_priv(ndev);
	struct device *dev = priv->dev;
	struct gfar_priv_tx_q *tx_queue = NULL;
	struct gfar_priv_rx_q *rx_queue = NULL;

	priv->total_tx_ring_size = 0;
	for (i = 0; i < priv->num_tx_queues; i++)
		priv->total_tx_ring_size += priv->tx_queue[i]->tx_ring_size;

	priv->total_rx_ring_size = 0;
	for (i = 0; i < priv->num_rx_queues; i++)
		priv->total_rx_ring_size += priv->rx_queue[i]->rx_ring_size;

	/* Allocate memory for the buffer descriptors */
	vaddr = dma_alloc_coherent(dev,
				   (priv->total_tx_ring_size *
				    sizeof(struct txbd8)) +
				   (priv->total_rx_ring_size *
				    sizeof(struct rxbd8)),
				   &addr, GFP_KERNEL);
	if (!vaddr)
		return -ENOMEM;

	for (i = 0; i < priv->num_tx_queues; i++) {
		tx_queue = priv->tx_queue[i];
		tx_queue->tx_bd_base = vaddr;
		tx_queue->tx_bd_dma_base = addr;
		tx_queue->dev = ndev;
		/* enet DMA only understands physical addresses */
		addr  += sizeof(struct txbd8) * tx_queue->tx_ring_size;
		vaddr += sizeof(struct txbd8) * tx_queue->tx_ring_size;
	}

	/* Start the rx descriptor ring where the tx ring leaves off */
	for (i = 0; i < priv->num_rx_queues; i++) {
		rx_queue = priv->rx_queue[i];
		rx_queue->rx_bd_base = vaddr;
		rx_queue->rx_bd_dma_base = addr;
		rx_queue->dev = ndev;
		addr  += sizeof(struct rxbd8) * rx_queue->rx_ring_size;
		vaddr += sizeof(struct rxbd8) * rx_queue->rx_ring_size;
	}

	/* Setup the skbuff rings */
	for (i = 0; i < priv->num_tx_queues; i++) {
		tx_queue = priv->tx_queue[i];
		tx_queue->tx_skbuff =
			kmalloc_array(tx_queue->tx_ring_size,
				      sizeof(*tx_queue->tx_skbuff),
				      GFP_KERNEL);
		if (!tx_queue->tx_skbuff)
			goto cleanup;

		for (k = 0; k < tx_queue->tx_ring_size; k++)
			tx_queue->tx_skbuff[k] = NULL;
	}

	for (i = 0; i < priv->num_rx_queues; i++) {
		rx_queue = priv->rx_queue[i];
		rx_queue->rx_skbuff =
			kmalloc_array(rx_queue->rx_ring_size,
				      sizeof(*rx_queue->rx_skbuff),
				      GFP_KERNEL);
		if (!rx_queue->rx_skbuff)
			goto cleanup;

		for (j = 0; j < rx_queue->rx_ring_size; j++)
			rx_queue->rx_skbuff[j] = NULL;
	}

	if (gfar_init_bds(ndev))
		goto cleanup;

	return 0;

cleanup:
	free_skb_resources(priv);
	return -ENOMEM;
}

static void gfar_init_tx_rx_base(struct gfar_private *priv)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 __iomem *baddr;
	int i;

	baddr = &regs->tbase0;
	for (i = 0; i < priv->num_tx_queues; i++) {
		gfar_write(baddr, priv->tx_queue[i]->tx_bd_dma_base);
		baddr += 2;
	}

	baddr = &regs->rbase0;
	for (i = 0; i < priv->num_rx_queues; i++) {
		gfar_write(baddr, priv->rx_queue[i]->rx_bd_dma_base);
		baddr += 2;
	}
}

static void gfar_init_rqprm(struct gfar_private *priv)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 __iomem *baddr;
	int i;

	baddr = &regs->rqprm0;
	for (i = 0; i < priv->num_rx_queues; i++) {
		gfar_write(baddr, priv->rx_queue[i]->rx_ring_size |
			   (DEFAULT_RX_LFC_THR << FBTHR_SHIFT));
		baddr++;
	}
}

static void gfar_rx_buff_size_config(struct gfar_private *priv)
{
	int frame_size = priv->ndev->mtu + ETH_HLEN + ETH_FCS_LEN;

	/* set this when rx hw offload (TOE) functions are being used */
	priv->uses_rxfcb = 0;

	if (priv->ndev->features & (NETIF_F_RXCSUM | NETIF_F_HW_VLAN_CTAG_RX))
		priv->uses_rxfcb = 1;

	if (priv->hwts_rx_en)
		priv->uses_rxfcb = 1;

	if (priv->uses_rxfcb)
		frame_size += GMAC_FCB_LEN;

	frame_size += priv->padding;

	frame_size = (frame_size & ~(INCREMENTAL_BUFFER_SIZE - 1)) +
		     INCREMENTAL_BUFFER_SIZE;

	priv->rx_buffer_size = frame_size;
}

static void gfar_mac_rx_config(struct gfar_private *priv)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 rctrl = 0;

	if (priv->rx_filer_enable) {
		rctrl |= RCTRL_FILREN;
		/* Program the RIR0 reg with the required distribution */
		if (priv->poll_mode == GFAR_SQ_POLLING)
			gfar_write(&regs->rir0, DEFAULT_2RXQ_RIR0);
		else /* GFAR_MQ_POLLING */
			gfar_write(&regs->rir0, DEFAULT_8RXQ_RIR0);
	}

	/* Restore PROMISC mode */
	if (priv->ndev->flags & IFF_PROMISC)
		rctrl |= RCTRL_PROM;

	if (priv->ndev->features & NETIF_F_RXCSUM)
		rctrl |= RCTRL_CHECKSUMMING;

	if (priv->extended_hash)
		rctrl |= RCTRL_EXTHASH | RCTRL_EMEN;

	if (priv->padding) {
		rctrl &= ~RCTRL_PAL_MASK;
		rctrl |= RCTRL_PADDING(priv->padding);
	}

	/* Enable HW time stamping if requested from user space */
	if (priv->hwts_rx_en)
		rctrl |= RCTRL_PRSDEP_INIT | RCTRL_TS_ENABLE;

	if (priv->ndev->features & NETIF_F_HW_VLAN_CTAG_RX)
		rctrl |= RCTRL_VLEX | RCTRL_PRSDEP_INIT;

	/* Clear the LFC bit */
	gfar_write(&regs->rctrl, rctrl);
	/* Init flow control threshold values */
	gfar_init_rqprm(priv);
	gfar_write(&regs->ptv, DEFAULT_LFC_PTVVAL);
	rctrl |= RCTRL_LFC;

	/* Init rctrl based on our settings */
	gfar_write(&regs->rctrl, rctrl);
}

static void gfar_mac_tx_config(struct gfar_private *priv)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 tctrl = 0;

	if (priv->ndev->features & NETIF_F_IP_CSUM)
		tctrl |= TCTRL_INIT_CSUM;

	if (priv->prio_sched_en)
		tctrl |= TCTRL_TXSCHED_PRIO;
	else {
		tctrl |= TCTRL_TXSCHED_WRRS;
		gfar_write(&regs->tr03wt, DEFAULT_WRRS_WEIGHT);
		gfar_write(&regs->tr47wt, DEFAULT_WRRS_WEIGHT);
	}

	if (priv->ndev->features & NETIF_F_HW_VLAN_CTAG_TX)
		tctrl |= TCTRL_VLINS;

	gfar_write(&regs->tctrl, tctrl);
}

static void gfar_configure_coalescing(struct gfar_private *priv,
			       unsigned long tx_mask, unsigned long rx_mask)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 __iomem *baddr;

	if (priv->mode == MQ_MG_MODE) {
		int i = 0;

		baddr = &regs->txic0;
		for_each_set_bit(i, &tx_mask, priv->num_tx_queues) {
			gfar_write(baddr + i, 0);
			if (likely(priv->tx_queue[i]->txcoalescing))
				gfar_write(baddr + i, priv->tx_queue[i]->txic);
		}

		baddr = &regs->rxic0;
		for_each_set_bit(i, &rx_mask, priv->num_rx_queues) {
			gfar_write(baddr + i, 0);
			if (likely(priv->rx_queue[i]->rxcoalescing))
				gfar_write(baddr + i, priv->rx_queue[i]->rxic);
		}
	} else {
		/* Backward compatible case -- even if we enable
		 * multiple queues, there's only single reg to program
		 */
		gfar_write(&regs->txic, 0);
		if (likely(priv->tx_queue[0]->txcoalescing))
			gfar_write(&regs->txic, priv->tx_queue[0]->txic);

		gfar_write(&regs->rxic, 0);
		if (unlikely(priv->rx_queue[0]->rxcoalescing))
			gfar_write(&regs->rxic, priv->rx_queue[0]->rxic);
	}
}

void gfar_configure_coalescing_all(struct gfar_private *priv)
{
	gfar_configure_coalescing(priv, 0xFF, 0xFF);
}

static struct net_device_stats *gfar_get_stats(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	unsigned long rx_packets = 0, rx_bytes = 0, rx_dropped = 0;
	unsigned long tx_packets = 0, tx_bytes = 0;
	int i;

	for (i = 0; i < priv->num_rx_queues; i++) {
		rx_packets += priv->rx_queue[i]->stats.rx_packets;
		rx_bytes   += priv->rx_queue[i]->stats.rx_bytes;
		rx_dropped += priv->rx_queue[i]->stats.rx_dropped;
	}

	dev->stats.rx_packets = rx_packets;
	dev->stats.rx_bytes   = rx_bytes;
	dev->stats.rx_dropped = rx_dropped;

	for (i = 0; i < priv->num_tx_queues; i++) {
		tx_bytes += priv->tx_queue[i]->stats.tx_bytes;
		tx_packets += priv->tx_queue[i]->stats.tx_packets;
	}

	dev->stats.tx_bytes   = tx_bytes;
	dev->stats.tx_packets = tx_packets;

	return &dev->stats;
}

static const struct net_device_ops gfar_netdev_ops = {
	.ndo_open = gfar_enet_open,
	.ndo_start_xmit = gfar_start_xmit,
	.ndo_stop = gfar_close,
	.ndo_change_mtu = gfar_change_mtu,
	.ndo_set_features = gfar_set_features,
	.ndo_set_rx_mode = gfar_set_multi,
	.ndo_tx_timeout = gfar_timeout,
	.ndo_do_ioctl = gfar_ioctl,
	.ndo_get_stats = gfar_get_stats,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = gfar_netpoll,
#endif
};

static void gfar_ints_disable(struct gfar_private *priv)
{
	int i;
	for (i = 0; i < priv->num_grps; i++) {
		struct gfar __iomem *regs = priv->gfargrp[i].regs;
		/* Clear IEVENT */
		gfar_write(&regs->ievent, IEVENT_INIT_CLEAR);

		/* Initialize IMASK */
		gfar_write(&regs->imask, IMASK_INIT_CLEAR);
	}
}

static void gfar_ints_enable(struct gfar_private *priv)
{
	int i;
	for (i = 0; i < priv->num_grps; i++) {
		struct gfar __iomem *regs = priv->gfargrp[i].regs;
		/* Unmask the interrupts we look for */
		gfar_write(&regs->imask, IMASK_DEFAULT);
	}
}

static void lock_tx_qs(struct gfar_private *priv)
{
	int i;

	for (i = 0; i < priv->num_tx_queues; i++)
		spin_lock(&priv->tx_queue[i]->txlock);
}

static void unlock_tx_qs(struct gfar_private *priv)
{
	int i;

	for (i = 0; i < priv->num_tx_queues; i++)
		spin_unlock(&priv->tx_queue[i]->txlock);
}

static int gfar_alloc_tx_queues(struct gfar_private *priv)
{
	int i;

	for (i = 0; i < priv->num_tx_queues; i++) {
		priv->tx_queue[i] = kzalloc(sizeof(struct gfar_priv_tx_q),
					    GFP_KERNEL);
		if (!priv->tx_queue[i])
			return -ENOMEM;

		priv->tx_queue[i]->tx_skbuff = NULL;
		priv->tx_queue[i]->qindex = i;
		priv->tx_queue[i]->dev = priv->ndev;
		spin_lock_init(&(priv->tx_queue[i]->txlock));
	}
	return 0;
}

static int gfar_alloc_rx_queues(struct gfar_private *priv)
{
	int i;

	for (i = 0; i < priv->num_rx_queues; i++) {
		priv->rx_queue[i] = kzalloc(sizeof(struct gfar_priv_rx_q),
					    GFP_KERNEL);
		if (!priv->rx_queue[i])
			return -ENOMEM;

		priv->rx_queue[i]->rx_skbuff = NULL;
		priv->rx_queue[i]->qindex = i;
		priv->rx_queue[i]->dev = priv->ndev;
	}
	return 0;
}

static void gfar_free_tx_queues(struct gfar_private *priv)
{
	int i;

	for (i = 0; i < priv->num_tx_queues; i++)
		kfree(priv->tx_queue[i]);
}

static void gfar_free_rx_queues(struct gfar_private *priv)
{
	int i;

	for (i = 0; i < priv->num_rx_queues; i++)
		kfree(priv->rx_queue[i]);
}

static void unmap_group_regs(struct gfar_private *priv)
{
	int i;

	for (i = 0; i < MAXGROUPS; i++)
		if (priv->gfargrp[i].regs)
			iounmap(priv->gfargrp[i].regs);
}

static void free_gfar_dev(struct gfar_private *priv)
{
	int i, j;

	for (i = 0; i < priv->num_grps; i++)
		for (j = 0; j < GFAR_NUM_IRQS; j++) {
			kfree(priv->gfargrp[i].irqinfo[j]);
			priv->gfargrp[i].irqinfo[j] = NULL;
		}

	free_netdev(priv->ndev);
}

static void disable_napi(struct gfar_private *priv)
{
	int i;

	for (i = 0; i < priv->num_grps; i++) {
		napi_disable(&priv->gfargrp[i].napi_rx);
		napi_disable(&priv->gfargrp[i].napi_tx);
	}
}

static void enable_napi(struct gfar_private *priv)
{
	int i;

	for (i = 0; i < priv->num_grps; i++) {
		napi_enable(&priv->gfargrp[i].napi_rx);
		napi_enable(&priv->gfargrp[i].napi_tx);
	}
}

static int gfar_parse_group(struct device_node *np,
			    struct gfar_private *priv, const char *model)
{
	struct gfar_priv_grp *grp = &priv->gfargrp[priv->num_grps];
	int i;

	for (i = 0; i < GFAR_NUM_IRQS; i++) {
		grp->irqinfo[i] = kzalloc(sizeof(struct gfar_irqinfo),
					  GFP_KERNEL);
		if (!grp->irqinfo[i])
			return -ENOMEM;
	}

	grp->regs = of_iomap(np, 0);
	if (!grp->regs)
		return -ENOMEM;

	gfar_irq(grp, TX)->irq = irq_of_parse_and_map(np, 0);

	/* If we aren't the FEC we have multiple interrupts */
	if (model && strcasecmp(model, "FEC")) {
		gfar_irq(grp, RX)->irq = irq_of_parse_and_map(np, 1);
		gfar_irq(grp, ER)->irq = irq_of_parse_and_map(np, 2);
		if (gfar_irq(grp, TX)->irq == NO_IRQ ||
		    gfar_irq(grp, RX)->irq == NO_IRQ ||
		    gfar_irq(grp, ER)->irq == NO_IRQ)
			return -EINVAL;
	}

	grp->priv = priv;
	spin_lock_init(&grp->grplock);
	if (priv->mode == MQ_MG_MODE) {
		u32 *rxq_mask, *txq_mask;
		rxq_mask = (u32 *)of_get_property(np, "fsl,rx-bit-map", NULL);
		txq_mask = (u32 *)of_get_property(np, "fsl,tx-bit-map", NULL);

		if (priv->poll_mode == GFAR_SQ_POLLING) {
			/* One Q per interrupt group: Q0 to G0, Q1 to G1 */
			grp->rx_bit_map = (DEFAULT_MAPPING >> priv->num_grps);
			grp->tx_bit_map = (DEFAULT_MAPPING >> priv->num_grps);
		} else { /* GFAR_MQ_POLLING */
			grp->rx_bit_map = rxq_mask ?
			*rxq_mask : (DEFAULT_MAPPING >> priv->num_grps);
			grp->tx_bit_map = txq_mask ?
			*txq_mask : (DEFAULT_MAPPING >> priv->num_grps);
		}
	} else {
		grp->rx_bit_map = 0xFF;
		grp->tx_bit_map = 0xFF;
	}

	/* bit_map's MSB is q0 (from q0 to q7) but, for_each_set_bit parses
	 * right to left, so we need to revert the 8 bits to get the q index
	 */
	grp->rx_bit_map = bitrev8(grp->rx_bit_map);
	grp->tx_bit_map = bitrev8(grp->tx_bit_map);

	/* Calculate RSTAT, TSTAT, RQUEUE and TQUEUE values,
	 * also assign queues to groups
	 */
	for_each_set_bit(i, &grp->rx_bit_map, priv->num_rx_queues) {
		if (!grp->rx_queue)
			grp->rx_queue = priv->rx_queue[i];
		grp->num_rx_queues++;
		grp->rstat |= (RSTAT_CLEAR_RHALT >> i);
		priv->rqueue |= ((RQUEUE_EN0 | RQUEUE_EX0) >> i);
		priv->rx_queue[i]->grp = grp;
	}

	for_each_set_bit(i, &grp->tx_bit_map, priv->num_tx_queues) {
		if (!grp->tx_queue)
			grp->tx_queue = priv->tx_queue[i];
		grp->num_tx_queues++;
		grp->tstat |= (TSTAT_CLEAR_THALT >> i);
		priv->tqueue |= (TQUEUE_EN0 >> i);
		priv->tx_queue[i]->grp = grp;
	}

	priv->num_grps++;

	return 0;
}

static int gfar_of_init(struct platform_device *ofdev, struct net_device **pdev)
{
	const char *model;
	const char *ctype;
	const void *mac_addr;
	int err = 0, i;
	struct net_device *dev = NULL;
	struct gfar_private *priv = NULL;
	struct device_node *np = ofdev->dev.of_node;
	struct device_node *child = NULL;
	const u32 *stash;
	const u32 *stash_len;
	const u32 *stash_idx;
	unsigned int num_tx_qs, num_rx_qs;
	u32 *tx_queues, *rx_queues;
	unsigned short mode, poll_mode;

	if (!np)
		return -ENODEV;

	if (of_device_is_compatible(np, "fsl,etsec2")) {
		mode = MQ_MG_MODE;
		poll_mode = GFAR_SQ_POLLING;
	} else {
		mode = SQ_SG_MODE;
		poll_mode = GFAR_SQ_POLLING;
	}

	/* parse the num of HW tx and rx queues */
	tx_queues = (u32 *)of_get_property(np, "fsl,num_tx_queues", NULL);
	rx_queues = (u32 *)of_get_property(np, "fsl,num_rx_queues", NULL);

	if (mode == SQ_SG_MODE) {
		num_tx_qs = 1;
		num_rx_qs = 1;
	} else { /* MQ_MG_MODE */
		/* get the actual number of supported groups */
		unsigned int num_grps = of_get_available_child_count(np);

		if (num_grps == 0 || num_grps > MAXGROUPS) {
			dev_err(&ofdev->dev, "Invalid # of int groups(%d)\n",
				num_grps);
			pr_err("Cannot do alloc_etherdev, aborting\n");
			return -EINVAL;
		}

		if (poll_mode == GFAR_SQ_POLLING) {
			num_tx_qs = num_grps; /* one txq per int group */
			num_rx_qs = num_grps; /* one rxq per int group */
		} else { /* GFAR_MQ_POLLING */
			num_tx_qs = tx_queues ? *tx_queues : 1;
			num_rx_qs = rx_queues ? *rx_queues : 1;
		}
	}

	if (num_tx_qs > MAX_TX_QS) {
		pr_err("num_tx_qs(=%d) greater than MAX_TX_QS(=%d)\n",
		       num_tx_qs, MAX_TX_QS);
		pr_err("Cannot do alloc_etherdev, aborting\n");
		return -EINVAL;
	}

	if (num_rx_qs > MAX_RX_QS) {
		pr_err("num_rx_qs(=%d) greater than MAX_RX_QS(=%d)\n",
		       num_rx_qs, MAX_RX_QS);
		pr_err("Cannot do alloc_etherdev, aborting\n");
		return -EINVAL;
	}

	*pdev = alloc_etherdev_mq(sizeof(*priv), num_tx_qs);
	dev = *pdev;
	if (NULL == dev)
		return -ENOMEM;

	priv = netdev_priv(dev);
	priv->ndev = dev;

	priv->mode = mode;
	priv->poll_mode = poll_mode;

	priv->num_tx_queues = num_tx_qs;
	netif_set_real_num_rx_queues(dev, num_rx_qs);
	priv->num_rx_queues = num_rx_qs;

	err = gfar_alloc_tx_queues(priv);
	if (err)
		goto tx_alloc_failed;

	err = gfar_alloc_rx_queues(priv);
	if (err)
		goto rx_alloc_failed;

	/* Init Rx queue filer rule set linked list */
	INIT_LIST_HEAD(&priv->rx_list.list);
	priv->rx_list.count = 0;
	mutex_init(&priv->rx_queue_access);

	model = of_get_property(np, "model", NULL);

	for (i = 0; i < MAXGROUPS; i++)
		priv->gfargrp[i].regs = NULL;

	/* Parse and initialize group specific information */
	if (priv->mode == MQ_MG_MODE) {
		for_each_child_of_node(np, child) {
			err = gfar_parse_group(child, priv, model);
			if (err)
				goto err_grp_init;
		}
	} else { /* SQ_SG_MODE */
		err = gfar_parse_group(np, priv, model);
		if (err)
			goto err_grp_init;
	}

	stash = of_get_property(np, "bd-stash", NULL);

	if (stash) {
		priv->device_flags |= FSL_GIANFAR_DEV_HAS_BD_STASHING;
		priv->bd_stash_en = 1;
	}

	stash_len = of_get_property(np, "rx-stash-len", NULL);

	if (stash_len)
		priv->rx_stash_size = *stash_len;

	stash_idx = of_get_property(np, "rx-stash-idx", NULL);

	if (stash_idx)
		priv->rx_stash_index = *stash_idx;

	if (stash_len || stash_idx)
		priv->device_flags |= FSL_GIANFAR_DEV_HAS_BUF_STASHING;

	mac_addr = of_get_mac_address(np);

	if (mac_addr)
		memcpy(dev->dev_addr, mac_addr, ETH_ALEN);

	if (model && !strcasecmp(model, "TSEC"))
		priv->device_flags |= FSL_GIANFAR_DEV_HAS_GIGABIT |
				     FSL_GIANFAR_DEV_HAS_COALESCE |
				     FSL_GIANFAR_DEV_HAS_RMON |
				     FSL_GIANFAR_DEV_HAS_MULTI_INTR;

	if (model && !strcasecmp(model, "eTSEC"))
		priv->device_flags |= FSL_GIANFAR_DEV_HAS_GIGABIT |
				     FSL_GIANFAR_DEV_HAS_COALESCE |
				     FSL_GIANFAR_DEV_HAS_RMON |
				     FSL_GIANFAR_DEV_HAS_MULTI_INTR |
				     FSL_GIANFAR_DEV_HAS_CSUM |
				     FSL_GIANFAR_DEV_HAS_VLAN |
				     FSL_GIANFAR_DEV_HAS_MAGIC_PACKET |
				     FSL_GIANFAR_DEV_HAS_EXTENDED_HASH |
				     FSL_GIANFAR_DEV_HAS_TIMER;

	ctype = of_get_property(np, "phy-connection-type", NULL);

	/* We only care about rgmii-id.  The rest are autodetected */
	if (ctype && !strcmp(ctype, "rgmii-id"))
		priv->interface = PHY_INTERFACE_MODE_RGMII_ID;
	else
		priv->interface = PHY_INTERFACE_MODE_MII;

	if (of_get_property(np, "fsl,magic-packet", NULL))
		priv->device_flags |= FSL_GIANFAR_DEV_HAS_MAGIC_PACKET;

	priv->phy_node = of_parse_phandle(np, "phy-handle", 0);

	/* In the case of a fixed PHY, the DT node associated
	 * to the PHY is the Ethernet MAC DT node.
	 */
	if (!priv->phy_node && of_phy_is_fixed_link(np)) {
		err = of_phy_register_fixed_link(np);
		if (err)
			goto err_grp_init;

		priv->phy_node = of_node_get(np);
	}

	/* Find the TBI PHY.  If it's not there, we don't support SGMII */
	priv->tbi_node = of_parse_phandle(np, "tbi-handle", 0);

	return 0;

err_grp_init:
	unmap_group_regs(priv);
rx_alloc_failed:
	gfar_free_rx_queues(priv);
tx_alloc_failed:
	gfar_free_tx_queues(priv);
	free_gfar_dev(priv);
	return err;
}

static int gfar_hwtstamp_set(struct net_device *netdev, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	struct gfar_private *priv = netdev_priv(netdev);

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	/* reserved for future extensions */
	if (config.flags)
		return -EINVAL;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
		priv->hwts_tx_en = 0;
		break;
	case HWTSTAMP_TX_ON:
		if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_TIMER))
			return -ERANGE;
		priv->hwts_tx_en = 1;
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		if (priv->hwts_rx_en) {
			priv->hwts_rx_en = 0;
			reset_gfar(netdev);
		}
		break;
	default:
		if (!(priv->device_flags & FSL_GIANFAR_DEV_HAS_TIMER))
			return -ERANGE;
		if (!priv->hwts_rx_en) {
			priv->hwts_rx_en = 1;
			reset_gfar(netdev);
		}
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	}

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

static int gfar_hwtstamp_get(struct net_device *netdev, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	struct gfar_private *priv = netdev_priv(netdev);

	config.flags = 0;
	config.tx_type = priv->hwts_tx_en ? HWTSTAMP_TX_ON : HWTSTAMP_TX_OFF;
	config.rx_filter = (priv->hwts_rx_en ?
			    HWTSTAMP_FILTER_ALL : HWTSTAMP_FILTER_NONE);

	return copy_to_user(ifr->ifr_data, &config, sizeof(config)) ?
		-EFAULT : 0;
}

static int gfar_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct gfar_private *priv = netdev_priv(dev);

	if (!netif_running(dev))
		return -EINVAL;

	if (cmd == SIOCSHWTSTAMP)
		return gfar_hwtstamp_set(dev, rq);
	if (cmd == SIOCGHWTSTAMP)
		return gfar_hwtstamp_get(dev, rq);

	if (!priv->phydev)
		return -ENODEV;

	return phy_mii_ioctl(priv->phydev, rq, cmd);
}

static u32 cluster_entry_per_class(struct gfar_private *priv, u32 rqfar,
				   u32 class)
{
	u32 rqfpr = FPR_FILER_MASK;
	u32 rqfcr = 0x0;

	rqfar--;
	rqfcr = RQFCR_CLE | RQFCR_PID_MASK | RQFCR_CMP_EXACT;
	priv->ftp_rqfpr[rqfar] = rqfpr;
	priv->ftp_rqfcr[rqfar] = rqfcr;
	gfar_write_filer(priv, rqfar, rqfcr, rqfpr);

	rqfar--;
	rqfcr = RQFCR_CMP_NOMATCH;
	priv->ftp_rqfpr[rqfar] = rqfpr;
	priv->ftp_rqfcr[rqfar] = rqfcr;
	gfar_write_filer(priv, rqfar, rqfcr, rqfpr);

	rqfar--;
	rqfcr = RQFCR_CMP_EXACT | RQFCR_PID_PARSE | RQFCR_CLE | RQFCR_AND;
	rqfpr = class;
	priv->ftp_rqfcr[rqfar] = rqfcr;
	priv->ftp_rqfpr[rqfar] = rqfpr;
	gfar_write_filer(priv, rqfar, rqfcr, rqfpr);

	rqfar--;
	rqfcr = RQFCR_CMP_EXACT | RQFCR_PID_MASK | RQFCR_AND;
	rqfpr = class;
	priv->ftp_rqfcr[rqfar] = rqfcr;
	priv->ftp_rqfpr[rqfar] = rqfpr;
	gfar_write_filer(priv, rqfar, rqfcr, rqfpr);

	return rqfar;
}

static void gfar_init_filer_table(struct gfar_private *priv)
{
	int i = 0x0;
	u32 rqfar = MAX_FILER_IDX;
	u32 rqfcr = 0x0;
	u32 rqfpr = FPR_FILER_MASK;

	/* Default rule */
	rqfcr = RQFCR_CMP_MATCH;
	priv->ftp_rqfcr[rqfar] = rqfcr;
	priv->ftp_rqfpr[rqfar] = rqfpr;
	gfar_write_filer(priv, rqfar, rqfcr, rqfpr);

	rqfar = cluster_entry_per_class(priv, rqfar, RQFPR_IPV6);
	rqfar = cluster_entry_per_class(priv, rqfar, RQFPR_IPV6 | RQFPR_UDP);
	rqfar = cluster_entry_per_class(priv, rqfar, RQFPR_IPV6 | RQFPR_TCP);
	rqfar = cluster_entry_per_class(priv, rqfar, RQFPR_IPV4);
	rqfar = cluster_entry_per_class(priv, rqfar, RQFPR_IPV4 | RQFPR_UDP);
	rqfar = cluster_entry_per_class(priv, rqfar, RQFPR_IPV4 | RQFPR_TCP);

	/* cur_filer_idx indicated the first non-masked rule */
	priv->cur_filer_idx = rqfar;

	/* Rest are masked rules */
	rqfcr = RQFCR_CMP_NOMATCH;
	for (i = 0; i < rqfar; i++) {
		priv->ftp_rqfcr[i] = rqfcr;
		priv->ftp_rqfpr[i] = rqfpr;
		gfar_write_filer(priv, i, rqfcr, rqfpr);
	}
}

#ifdef CONFIG_PPC
static void __gfar_detect_errata_83xx(struct gfar_private *priv)
{
	unsigned int pvr = mfspr(SPRN_PVR);
	unsigned int svr = mfspr(SPRN_SVR);
	unsigned int mod = (svr >> 16) & 0xfff6; /* w/o E suffix */
	unsigned int rev = svr & 0xffff;

	/* MPC8313 Rev 2.0 and higher; All MPC837x */
	if ((pvr == 0x80850010 && mod == 0x80b0 && rev >= 0x0020) ||
	    (pvr == 0x80861010 && (mod & 0xfff9) == 0x80c0))
		priv->errata |= GFAR_ERRATA_74;

	/* MPC8313 and MPC837x all rev */
	if ((pvr == 0x80850010 && mod == 0x80b0) ||
	    (pvr == 0x80861010 && (mod & 0xfff9) == 0x80c0))
		priv->errata |= GFAR_ERRATA_76;

	/* MPC8313 Rev < 2.0 */
	if (pvr == 0x80850010 && mod == 0x80b0 && rev < 0x0020)
		priv->errata |= GFAR_ERRATA_12;
}

static void __gfar_detect_errata_85xx(struct gfar_private *priv)
{
	unsigned int svr = mfspr(SPRN_SVR);

	if ((SVR_SOC_VER(svr) == SVR_8548) && (SVR_REV(svr) == 0x20))
		priv->errata |= GFAR_ERRATA_12;
	if (((SVR_SOC_VER(svr) == SVR_P2020) && (SVR_REV(svr) < 0x20)) ||
	    ((SVR_SOC_VER(svr) == SVR_P2010) && (SVR_REV(svr) < 0x20)))
		priv->errata |= GFAR_ERRATA_76; /* aka eTSEC 20 */
}
#endif

static void gfar_detect_errata(struct gfar_private *priv)
{
	struct device *dev = &priv->ofdev->dev;

	/* no plans to fix */
	priv->errata |= GFAR_ERRATA_A002;

#ifdef CONFIG_PPC
	if (pvr_version_is(PVR_VER_E500V1) || pvr_version_is(PVR_VER_E500V2))
		__gfar_detect_errata_85xx(priv);
	else /* non-mpc85xx parts, i.e. e300 core based */
		__gfar_detect_errata_83xx(priv);
#endif

	if (priv->errata)
		dev_info(dev, "enabled errata workarounds, flags: 0x%x\n",
			 priv->errata);
}

void gfar_mac_reset(struct gfar_private *priv)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 tempval;

	/* Reset MAC layer */
	gfar_write(&regs->maccfg1, MACCFG1_SOFT_RESET);

	/* We need to delay at least 3 TX clocks */
	udelay(3);

	/* the soft reset bit is not self-resetting, so we need to
	 * clear it before resuming normal operation
	 */
	gfar_write(&regs->maccfg1, 0);

	udelay(3);

	/* Compute rx_buff_size based on config flags */
	gfar_rx_buff_size_config(priv);

	/* Initialize the max receive frame/buffer lengths */
	gfar_write(&regs->maxfrm, priv->rx_buffer_size);
	gfar_write(&regs->mrblr, priv->rx_buffer_size);

	/* Initialize the Minimum Frame Length Register */
	gfar_write(&regs->minflr, MINFLR_INIT_SETTINGS);

	/* Initialize MACCFG2. */
	tempval = MACCFG2_INIT_SETTINGS;

	/* If the mtu is larger than the max size for standard
	 * ethernet frames (ie, a jumbo frame), then set maccfg2
	 * to allow huge frames, and to check the length
	 */
	if (priv->rx_buffer_size > DEFAULT_RX_BUFFER_SIZE ||
	    gfar_has_errata(priv, GFAR_ERRATA_74))
		tempval |= MACCFG2_HUGEFRAME | MACCFG2_LENGTHCHECK;

	gfar_write(&regs->maccfg2, tempval);

	/* Clear mac addr hash registers */
	gfar_write(&regs->igaddr0, 0);
	gfar_write(&regs->igaddr1, 0);
	gfar_write(&regs->igaddr2, 0);
	gfar_write(&regs->igaddr3, 0);
	gfar_write(&regs->igaddr4, 0);
	gfar_write(&regs->igaddr5, 0);
	gfar_write(&regs->igaddr6, 0);
	gfar_write(&regs->igaddr7, 0);

	gfar_write(&regs->gaddr0, 0);
	gfar_write(&regs->gaddr1, 0);
	gfar_write(&regs->gaddr2, 0);
	gfar_write(&regs->gaddr3, 0);
	gfar_write(&regs->gaddr4, 0);
	gfar_write(&regs->gaddr5, 0);
	gfar_write(&regs->gaddr6, 0);
	gfar_write(&regs->gaddr7, 0);

	if (priv->extended_hash)
		gfar_clear_exact_match(priv->ndev);

	gfar_mac_rx_config(priv);

	gfar_mac_tx_config(priv);

	gfar_set_mac_address(priv->ndev);

	gfar_set_multi(priv->ndev);

	/* clear ievent and imask before configuring coalescing */
	gfar_ints_disable(priv);

	/* Configure the coalescing support */
	gfar_configure_coalescing_all(priv);
}

static void gfar_hw_init(struct gfar_private *priv)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 attrs;

	/* Stop the DMA engine now, in case it was running before
	 * (The firmware could have used it, and left it running).
	 */
	gfar_halt(priv);

	gfar_mac_reset(priv);

	/* Zero out the rmon mib registers if it has them */
	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_RMON) {
		memset_io(&(regs->rmon), 0, sizeof(struct rmon_mib));

		/* Mask off the CAM interrupts */
		gfar_write(&regs->rmon.cam1, 0xffffffff);
		gfar_write(&regs->rmon.cam2, 0xffffffff);
	}

	/* Initialize ECNTRL */
	gfar_write(&regs->ecntrl, ECNTRL_INIT_SETTINGS);

	/* Set the extraction length and index */
	attrs = ATTRELI_EL(priv->rx_stash_size) |
		ATTRELI_EI(priv->rx_stash_index);

	gfar_write(&regs->attreli, attrs);

	/* Start with defaults, and add stashing
	 * depending on driver parameters
	 */
	attrs = ATTR_INIT_SETTINGS;

	if (priv->bd_stash_en)
		attrs |= ATTR_BDSTASH;

	if (priv->rx_stash_size != 0)
		attrs |= ATTR_BUFSTASH;

	gfar_write(&regs->attr, attrs);

	/* FIFO configs */
	gfar_write(&regs->fifo_tx_thr, DEFAULT_FIFO_TX_THR);
	gfar_write(&regs->fifo_tx_starve, DEFAULT_FIFO_TX_STARVE);
	gfar_write(&regs->fifo_tx_starve_shutoff, DEFAULT_FIFO_TX_STARVE_OFF);

	/* Program the interrupt steering regs, only for MG devices */
	if (priv->num_grps > 1)
		gfar_write_isrg(priv);
}

static void gfar_init_addr_hash_table(struct gfar_private *priv)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_EXTENDED_HASH) {
		priv->extended_hash = 1;
		priv->hash_width = 9;

		priv->hash_regs[0] = &regs->igaddr0;
		priv->hash_regs[1] = &regs->igaddr1;
		priv->hash_regs[2] = &regs->igaddr2;
		priv->hash_regs[3] = &regs->igaddr3;
		priv->hash_regs[4] = &regs->igaddr4;
		priv->hash_regs[5] = &regs->igaddr5;
		priv->hash_regs[6] = &regs->igaddr6;
		priv->hash_regs[7] = &regs->igaddr7;
		priv->hash_regs[8] = &regs->gaddr0;
		priv->hash_regs[9] = &regs->gaddr1;
		priv->hash_regs[10] = &regs->gaddr2;
		priv->hash_regs[11] = &regs->gaddr3;
		priv->hash_regs[12] = &regs->gaddr4;
		priv->hash_regs[13] = &regs->gaddr5;
		priv->hash_regs[14] = &regs->gaddr6;
		priv->hash_regs[15] = &regs->gaddr7;

	} else {
		priv->extended_hash = 0;
		priv->hash_width = 8;

		priv->hash_regs[0] = &regs->gaddr0;
		priv->hash_regs[1] = &regs->gaddr1;
		priv->hash_regs[2] = &regs->gaddr2;
		priv->hash_regs[3] = &regs->gaddr3;
		priv->hash_regs[4] = &regs->gaddr4;
		priv->hash_regs[5] = &regs->gaddr5;
		priv->hash_regs[6] = &regs->gaddr6;
		priv->hash_regs[7] = &regs->gaddr7;
	}
}

/* Set up the ethernet device structure, private data,
 * and anything else we need before we start
 */
static int gfar_probe(struct platform_device *ofdev)
{
	struct net_device *dev = NULL;
	struct gfar_private *priv = NULL;
	int err = 0, i;

	err = gfar_of_init(ofdev, &dev);

	if (err)
		return err;

	priv = netdev_priv(dev);
	priv->ndev = dev;
	priv->ofdev = ofdev;
	priv->dev = &ofdev->dev;
	SET_NETDEV_DEV(dev, &ofdev->dev);

	spin_lock_init(&priv->bflock);
	INIT_WORK(&priv->reset_task, gfar_reset_task);

	platform_set_drvdata(ofdev, priv);

	gfar_detect_errata(priv);

	/* Set the dev->base_addr to the gfar reg region */
	dev->base_addr = (unsigned long) priv->gfargrp[0].regs;

	/* Fill in the dev structure */
	dev->watchdog_timeo = TX_TIMEOUT;
	dev->mtu = 1500;
	dev->netdev_ops = &gfar_netdev_ops;
	dev->ethtool_ops = &gfar_ethtool_ops;

	/* Register for napi ...We are registering NAPI for each grp */
	for (i = 0; i < priv->num_grps; i++) {
		if (priv->poll_mode == GFAR_SQ_POLLING) {
			netif_napi_add(dev, &priv->gfargrp[i].napi_rx,
				       gfar_poll_rx_sq, GFAR_DEV_WEIGHT);
			netif_napi_add(dev, &priv->gfargrp[i].napi_tx,
				       gfar_poll_tx_sq, 2);
		} else {
			netif_napi_add(dev, &priv->gfargrp[i].napi_rx,
				       gfar_poll_rx, GFAR_DEV_WEIGHT);
			netif_napi_add(dev, &priv->gfargrp[i].napi_tx,
				       gfar_poll_tx, 2);
		}
	}

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_CSUM) {
		dev->hw_features = NETIF_F_IP_CSUM | NETIF_F_SG |
				   NETIF_F_RXCSUM;
		dev->features |= NETIF_F_IP_CSUM | NETIF_F_SG |
				 NETIF_F_RXCSUM | NETIF_F_HIGHDMA;
	}

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_VLAN) {
		dev->hw_features |= NETIF_F_HW_VLAN_CTAG_TX |
				    NETIF_F_HW_VLAN_CTAG_RX;
		dev->features |= NETIF_F_HW_VLAN_CTAG_RX;
	}

	gfar_init_addr_hash_table(priv);

	/* Insert receive time stamps into padding alignment bytes */
	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_TIMER)
		priv->padding = 8;

	if (dev->features & NETIF_F_IP_CSUM ||
	    priv->device_flags & FSL_GIANFAR_DEV_HAS_TIMER)
		dev->needed_headroom = GMAC_FCB_LEN;

	priv->rx_buffer_size = DEFAULT_RX_BUFFER_SIZE;

	/* Initializing some of the rx/tx queue level parameters */
	for (i = 0; i < priv->num_tx_queues; i++) {
		priv->tx_queue[i]->tx_ring_size = DEFAULT_TX_RING_SIZE;
		priv->tx_queue[i]->num_txbdfree = DEFAULT_TX_RING_SIZE;
		priv->tx_queue[i]->txcoalescing = DEFAULT_TX_COALESCE;
		priv->tx_queue[i]->txic = DEFAULT_TXIC;
	}

	for (i = 0; i < priv->num_rx_queues; i++) {
		priv->rx_queue[i]->rx_ring_size = DEFAULT_RX_RING_SIZE;
		priv->rx_queue[i]->rxcoalescing = DEFAULT_RX_COALESCE;
		priv->rx_queue[i]->rxic = DEFAULT_RXIC;
	}

	/* always enable rx filer */
	priv->rx_filer_enable = 1;
	/* Enable most messages by default */
	priv->msg_enable = (NETIF_MSG_IFUP << 1 ) - 1;
	/* use pritority h/w tx queue scheduling for single queue devices */
	if (priv->num_tx_queues == 1)
		priv->prio_sched_en = 1;

	set_bit(GFAR_DOWN, &priv->state);

	gfar_hw_init(priv);

	/* Carrier starts down, phylib will bring it up */
	netif_carrier_off(dev);

	err = register_netdev(dev);

	if (err) {
		pr_err("%s: Cannot register net device, aborting\n", dev->name);
		goto register_fail;
	}

	device_init_wakeup(&dev->dev,
			   priv->device_flags &
			   FSL_GIANFAR_DEV_HAS_MAGIC_PACKET);

	/* fill out IRQ number and name fields */
	for (i = 0; i < priv->num_grps; i++) {
		struct gfar_priv_grp *grp = &priv->gfargrp[i];
		if (priv->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
			sprintf(gfar_irq(grp, TX)->name, "%s%s%c%s",
				dev->name, "_g", '0' + i, "_tx");
			sprintf(gfar_irq(grp, RX)->name, "%s%s%c%s",
				dev->name, "_g", '0' + i, "_rx");
			sprintf(gfar_irq(grp, ER)->name, "%s%s%c%s",
				dev->name, "_g", '0' + i, "_er");
		} else
			strcpy(gfar_irq(grp, TX)->name, dev->name);
	}

	/* Initialize the filer table */
	gfar_init_filer_table(priv);

	/* Print out the device info */
	netdev_info(dev, "mac: %pM\n", dev->dev_addr);

	/* Even more device info helps when determining which kernel
	 * provided which set of benchmarks.
	 */
	netdev_info(dev, "Running with NAPI enabled\n");
	for (i = 0; i < priv->num_rx_queues; i++)
		netdev_info(dev, "RX BD ring size for Q[%d]: %d\n",
			    i, priv->rx_queue[i]->rx_ring_size);
	for (i = 0; i < priv->num_tx_queues; i++)
		netdev_info(dev, "TX BD ring size for Q[%d]: %d\n",
			    i, priv->tx_queue[i]->tx_ring_size);

	return 0;

register_fail:
	unmap_group_regs(priv);
	gfar_free_rx_queues(priv);
	gfar_free_tx_queues(priv);
	of_node_put(priv->phy_node);
	of_node_put(priv->tbi_node);
	free_gfar_dev(priv);
	return err;
}

static int gfar_remove(struct platform_device *ofdev)
{
	struct gfar_private *priv = platform_get_drvdata(ofdev);

	of_node_put(priv->phy_node);
	of_node_put(priv->tbi_node);

	unregister_netdev(priv->ndev);
	unmap_group_regs(priv);
	gfar_free_rx_queues(priv);
	gfar_free_tx_queues(priv);
	free_gfar_dev(priv);

	return 0;
}

#ifdef CONFIG_PM

static int gfar_suspend(struct device *dev)
{
	struct gfar_private *priv = dev_get_drvdata(dev);
	struct net_device *ndev = priv->ndev;
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	unsigned long flags;
	u32 tempval;

	int magic_packet = priv->wol_en &&
			   (priv->device_flags &
			    FSL_GIANFAR_DEV_HAS_MAGIC_PACKET);

	netif_device_detach(ndev);

	if (netif_running(ndev)) {

		local_irq_save(flags);
		lock_tx_qs(priv);

		gfar_halt_nodisable(priv);

		/* Disable Tx, and Rx if wake-on-LAN is disabled. */
		tempval = gfar_read(&regs->maccfg1);

		tempval &= ~MACCFG1_TX_EN;

		if (!magic_packet)
			tempval &= ~MACCFG1_RX_EN;

		gfar_write(&regs->maccfg1, tempval);

		unlock_tx_qs(priv);
		local_irq_restore(flags);

		disable_napi(priv);

		if (magic_packet) {
			/* Enable interrupt on Magic Packet */
			gfar_write(&regs->imask, IMASK_MAG);

			/* Enable Magic Packet mode */
			tempval = gfar_read(&regs->maccfg2);
			tempval |= MACCFG2_MPEN;
			gfar_write(&regs->maccfg2, tempval);
		} else {
			phy_stop(priv->phydev);
		}
	}

	return 0;
}

static int gfar_resume(struct device *dev)
{
	struct gfar_private *priv = dev_get_drvdata(dev);
	struct net_device *ndev = priv->ndev;
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	unsigned long flags;
	u32 tempval;
	int magic_packet = priv->wol_en &&
			   (priv->device_flags &
			    FSL_GIANFAR_DEV_HAS_MAGIC_PACKET);

	if (!netif_running(ndev)) {
		netif_device_attach(ndev);
		return 0;
	}

	if (!magic_packet && priv->phydev)
		phy_start(priv->phydev);

	/* Disable Magic Packet mode, in case something
	 * else woke us up.
	 */
	local_irq_save(flags);
	lock_tx_qs(priv);

	tempval = gfar_read(&regs->maccfg2);
	tempval &= ~MACCFG2_MPEN;
	gfar_write(&regs->maccfg2, tempval);

	gfar_start(priv);

	unlock_tx_qs(priv);
	local_irq_restore(flags);

	netif_device_attach(ndev);

	enable_napi(priv);

	return 0;
}

static int gfar_restore(struct device *dev)
{
	struct gfar_private *priv = dev_get_drvdata(dev);
	struct net_device *ndev = priv->ndev;

	if (!netif_running(ndev)) {
		netif_device_attach(ndev);

		return 0;
	}

	if (gfar_init_bds(ndev)) {
		free_skb_resources(priv);
		return -ENOMEM;
	}

	gfar_mac_reset(priv);

	gfar_init_tx_rx_base(priv);

	gfar_start(priv);

	priv->oldlink = 0;
	priv->oldspeed = 0;
	priv->oldduplex = -1;

	if (priv->phydev)
		phy_start(priv->phydev);

	netif_device_attach(ndev);
	enable_napi(priv);

	return 0;
}

static struct dev_pm_ops gfar_pm_ops = {
	.suspend = gfar_suspend,
	.resume = gfar_resume,
	.freeze = gfar_suspend,
	.thaw = gfar_resume,
	.restore = gfar_restore,
};

#define GFAR_PM_OPS (&gfar_pm_ops)

#else

#define GFAR_PM_OPS NULL

#endif

/* Reads the controller's registers to determine what interface
 * connects it to the PHY.
 */
static phy_interface_t gfar_get_interface(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 ecntrl;

	ecntrl = gfar_read(&regs->ecntrl);

	if (ecntrl & ECNTRL_SGMII_MODE)
		return PHY_INTERFACE_MODE_SGMII;

	if (ecntrl & ECNTRL_TBI_MODE) {
		if (ecntrl & ECNTRL_REDUCED_MODE)
			return PHY_INTERFACE_MODE_RTBI;
		else
			return PHY_INTERFACE_MODE_TBI;
	}

	if (ecntrl & ECNTRL_REDUCED_MODE) {
		if (ecntrl & ECNTRL_REDUCED_MII_MODE) {
			return PHY_INTERFACE_MODE_RMII;
		}
		else {
			phy_interface_t interface = priv->interface;

			/* This isn't autodetected right now, so it must
			 * be set by the device tree or platform code.
			 */
			if (interface == PHY_INTERFACE_MODE_RGMII_ID)
				return PHY_INTERFACE_MODE_RGMII_ID;

			return PHY_INTERFACE_MODE_RGMII;
		}
	}

	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_GIGABIT)
		return PHY_INTERFACE_MODE_GMII;

	return PHY_INTERFACE_MODE_MII;
}


/* Initializes driver's PHY state, and attaches to the PHY.
 * Returns 0 on success.
 */
static int init_phy(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	uint gigabit_support =
		priv->device_flags & FSL_GIANFAR_DEV_HAS_GIGABIT ?
		GFAR_SUPPORTED_GBIT : 0;
	phy_interface_t interface;

	priv->oldlink = 0;
	priv->oldspeed = 0;
	priv->oldduplex = -1;

	interface = gfar_get_interface(dev);

	priv->phydev = of_phy_connect(dev, priv->phy_node, &adjust_link, 0,
				      interface);
	if (!priv->phydev) {
		dev_err(&dev->dev, "could not attach to PHY\n");
		return -ENODEV;
	}

	if (interface == PHY_INTERFACE_MODE_SGMII)
		gfar_configure_serdes(dev);

	/* Remove any features not supported by the controller */
	priv->phydev->supported &= (GFAR_SUPPORTED | gigabit_support);
	priv->phydev->advertising = priv->phydev->supported;

	/* Add support for flow control, but don't advertise it by default */
	priv->phydev->supported |= (SUPPORTED_Pause | SUPPORTED_Asym_Pause);

	return 0;
}

/* Initialize TBI PHY interface for communicating with the
 * SERDES lynx PHY on the chip.  We communicate with this PHY
 * through the MDIO bus on each controller, treating it as a
 * "normal" PHY at the address found in the TBIPA register.  We assume
 * that the TBIPA register is valid.  Either the MDIO bus code will set
 * it to a value that doesn't conflict with other PHYs on the bus, or the
 * value doesn't matter, as there are no other PHYs on the bus.
 */
static void gfar_configure_serdes(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct phy_device *tbiphy;

	if (!priv->tbi_node) {
		dev_warn(&dev->dev, "error: SGMII mode requires that the "
				    "device tree specify a tbi-handle\n");
		return;
	}

	tbiphy = of_phy_find_device(priv->tbi_node);
	if (!tbiphy) {
		dev_err(&dev->dev, "error: Could not get TBI device\n");
		return;
	}

	/* If the link is already up, we must already be ok, and don't need to
	 * configure and reset the TBI<->SerDes link.  Maybe U-Boot configured
	 * everything for us?  Resetting it takes the link down and requires
	 * several seconds for it to come back.
	 */
	if (phy_read(tbiphy, MII_BMSR) & BMSR_LSTATUS)
		return;

	/* Single clk mode, mii mode off(for serdes communication) */
	phy_write(tbiphy, MII_TBICON, TBICON_CLK_SELECT);

	phy_write(tbiphy, MII_ADVERTISE,
		  ADVERTISE_1000XFULL | ADVERTISE_1000XPAUSE |
		  ADVERTISE_1000XPSE_ASYM);

	phy_write(tbiphy, MII_BMCR,
		  BMCR_ANENABLE | BMCR_ANRESTART | BMCR_FULLDPLX |
		  BMCR_SPEED1000);
}

static int __gfar_is_rx_idle(struct gfar_private *priv)
{
	u32 res;

	/* Normaly TSEC should not hang on GRS commands, so we should
	 * actually wait for IEVENT_GRSC flag.
	 */
	if (!gfar_has_errata(priv, GFAR_ERRATA_A002))
		return 0;

	/* Read the eTSEC register at offset 0xD1C. If bits 7-14 are
	 * the same as bits 23-30, the eTSEC Rx is assumed to be idle
	 * and the Rx can be safely reset.
	 */
	res = gfar_read((void __iomem *)priv->gfargrp[0].regs + 0xd1c);
	res &= 0x7f807f80;
	if ((res & 0xffff) == (res >> 16))
		return 1;

	return 0;
}

/* Halt the receive and transmit queues */
static void gfar_halt_nodisable(struct gfar_private *priv)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 tempval;
	unsigned int timeout;
	int stopped;

	gfar_ints_disable(priv);

	if (gfar_is_dma_stopped(priv))
		return;

	/* Stop the DMA, and wait for it to stop */
	tempval = gfar_read(&regs->dmactrl);
	tempval |= (DMACTRL_GRS | DMACTRL_GTS);
	gfar_write(&regs->dmactrl, tempval);

retry:
	timeout = 1000;
	while (!(stopped = gfar_is_dma_stopped(priv)) && timeout) {
		cpu_relax();
		timeout--;
	}

	if (!timeout)
		stopped = gfar_is_dma_stopped(priv);

	if (!stopped && !gfar_is_rx_dma_stopped(priv) &&
	    !__gfar_is_rx_idle(priv))
		goto retry;
}

/* Halt the receive and transmit queues */
void gfar_halt(struct gfar_private *priv)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 tempval;

	/* Dissable the Rx/Tx hw queues */
	gfar_write(&regs->rqueue, 0);
	gfar_write(&regs->tqueue, 0);

	mdelay(10);

	gfar_halt_nodisable(priv);

	/* Disable Rx/Tx DMA */
	tempval = gfar_read(&regs->maccfg1);
	tempval &= ~(MACCFG1_RX_EN | MACCFG1_TX_EN);
	gfar_write(&regs->maccfg1, tempval);
}

void stop_gfar(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);

	netif_tx_stop_all_queues(dev);

	smp_mb__before_atomic();
	set_bit(GFAR_DOWN, &priv->state);
	smp_mb__after_atomic();

	disable_napi(priv);

	/* disable ints and gracefully shut down Rx/Tx DMA */
	gfar_halt(priv);

	phy_stop(priv->phydev);

	free_skb_resources(priv);
}

static void free_skb_tx_queue(struct gfar_priv_tx_q *tx_queue)
{
	struct txbd8 *txbdp;
	struct gfar_private *priv = netdev_priv(tx_queue->dev);
	int i, j;

	txbdp = tx_queue->tx_bd_base;

	for (i = 0; i < tx_queue->tx_ring_size; i++) {
		if (!tx_queue->tx_skbuff[i])
			continue;

		dma_unmap_single(priv->dev, txbdp->bufPtr,
				 txbdp->length, DMA_TO_DEVICE);
		txbdp->lstatus = 0;
		for (j = 0; j < skb_shinfo(tx_queue->tx_skbuff[i])->nr_frags;
		     j++) {
			txbdp++;
			dma_unmap_page(priv->dev, txbdp->bufPtr,
				       txbdp->length, DMA_TO_DEVICE);
		}
		txbdp++;
		dev_kfree_skb_any(tx_queue->tx_skbuff[i]);
		tx_queue->tx_skbuff[i] = NULL;
	}
	kfree(tx_queue->tx_skbuff);
	tx_queue->tx_skbuff = NULL;
}

static void free_skb_rx_queue(struct gfar_priv_rx_q *rx_queue)
{
	struct rxbd8 *rxbdp;
	struct gfar_private *priv = netdev_priv(rx_queue->dev);
	int i;

	rxbdp = rx_queue->rx_bd_base;

	for (i = 0; i < rx_queue->rx_ring_size; i++) {
		if (rx_queue->rx_skbuff[i]) {
			dma_unmap_single(priv->dev, rxbdp->bufPtr,
					 priv->rx_buffer_size,
					 DMA_FROM_DEVICE);
			dev_kfree_skb_any(rx_queue->rx_skbuff[i]);
			rx_queue->rx_skbuff[i] = NULL;
		}
		rxbdp->lstatus = 0;
		rxbdp->bufPtr = 0;
		rxbdp++;
	}
	kfree(rx_queue->rx_skbuff);
	rx_queue->rx_skbuff = NULL;
}

/* If there are any tx skbs or rx skbs still around, free them.
 * Then free tx_skbuff and rx_skbuff
 */
static void free_skb_resources(struct gfar_private *priv)
{
	struct gfar_priv_tx_q *tx_queue = NULL;
	struct gfar_priv_rx_q *rx_queue = NULL;
	int i;

	/* Go through all the buffer descriptors and free their data buffers */
	for (i = 0; i < priv->num_tx_queues; i++) {
		struct netdev_queue *txq;

		tx_queue = priv->tx_queue[i];
		txq = netdev_get_tx_queue(tx_queue->dev, tx_queue->qindex);
		if (tx_queue->tx_skbuff)
			free_skb_tx_queue(tx_queue);
		netdev_tx_reset_queue(txq);
	}

	for (i = 0; i < priv->num_rx_queues; i++) {
		rx_queue = priv->rx_queue[i];
		if (rx_queue->rx_skbuff)
			free_skb_rx_queue(rx_queue);
	}

	dma_free_coherent(priv->dev,
			  sizeof(struct txbd8) * priv->total_tx_ring_size +
			  sizeof(struct rxbd8) * priv->total_rx_ring_size,
			  priv->tx_queue[0]->tx_bd_base,
			  priv->tx_queue[0]->tx_bd_dma_base);
}

void gfar_start(struct gfar_private *priv)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 tempval;
	int i = 0;

	/* Enable Rx/Tx hw queues */
	gfar_write(&regs->rqueue, priv->rqueue);
	gfar_write(&regs->tqueue, priv->tqueue);

	/* Initialize DMACTRL to have WWR and WOP */
	tempval = gfar_read(&regs->dmactrl);
	tempval |= DMACTRL_INIT_SETTINGS;
	gfar_write(&regs->dmactrl, tempval);

	/* Make sure we aren't stopped */
	tempval = gfar_read(&regs->dmactrl);
	tempval &= ~(DMACTRL_GRS | DMACTRL_GTS);
	gfar_write(&regs->dmactrl, tempval);

	for (i = 0; i < priv->num_grps; i++) {
		regs = priv->gfargrp[i].regs;
		/* Clear THLT/RHLT, so that the DMA starts polling now */
		gfar_write(&regs->tstat, priv->gfargrp[i].tstat);
		gfar_write(&regs->rstat, priv->gfargrp[i].rstat);
	}

	/* Enable Rx/Tx DMA */
	tempval = gfar_read(&regs->maccfg1);
	tempval |= (MACCFG1_RX_EN | MACCFG1_TX_EN);
	gfar_write(&regs->maccfg1, tempval);

	gfar_ints_enable(priv);

	priv->ndev->trans_start = jiffies; /* prevent tx timeout */
}

static void free_grp_irqs(struct gfar_priv_grp *grp)
{
	free_irq(gfar_irq(grp, TX)->irq, grp);
	free_irq(gfar_irq(grp, RX)->irq, grp);
	free_irq(gfar_irq(grp, ER)->irq, grp);
}

static int register_grp_irqs(struct gfar_priv_grp *grp)
{
	struct gfar_private *priv = grp->priv;
	struct net_device *dev = priv->ndev;
	int err;

	/* If the device has multiple interrupts, register for
	 * them.  Otherwise, only register for the one
	 */
	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
		/* Install our interrupt handlers for Error,
		 * Transmit, and Receive
		 */
		err = request_irq(gfar_irq(grp, ER)->irq, gfar_error, 0,
				  gfar_irq(grp, ER)->name, grp);
		if (err < 0) {
			netif_err(priv, intr, dev, "Can't get IRQ %d\n",
				  gfar_irq(grp, ER)->irq);

			goto err_irq_fail;
		}
		err = request_irq(gfar_irq(grp, TX)->irq, gfar_transmit, 0,
				  gfar_irq(grp, TX)->name, grp);
		if (err < 0) {
			netif_err(priv, intr, dev, "Can't get IRQ %d\n",
				  gfar_irq(grp, TX)->irq);
			goto tx_irq_fail;
		}
		err = request_irq(gfar_irq(grp, RX)->irq, gfar_receive, 0,
				  gfar_irq(grp, RX)->name, grp);
		if (err < 0) {
			netif_err(priv, intr, dev, "Can't get IRQ %d\n",
				  gfar_irq(grp, RX)->irq);
			goto rx_irq_fail;
		}
	} else {
		err = request_irq(gfar_irq(grp, TX)->irq, gfar_interrupt, 0,
				  gfar_irq(grp, TX)->name, grp);
		if (err < 0) {
			netif_err(priv, intr, dev, "Can't get IRQ %d\n",
				  gfar_irq(grp, TX)->irq);
			goto err_irq_fail;
		}
	}

	return 0;

rx_irq_fail:
	free_irq(gfar_irq(grp, TX)->irq, grp);
tx_irq_fail:
	free_irq(gfar_irq(grp, ER)->irq, grp);
err_irq_fail:
	return err;

}

static void gfar_free_irq(struct gfar_private *priv)
{
	int i;

	/* Free the IRQs */
	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
		for (i = 0; i < priv->num_grps; i++)
			free_grp_irqs(&priv->gfargrp[i]);
	} else {
		for (i = 0; i < priv->num_grps; i++)
			free_irq(gfar_irq(&priv->gfargrp[i], TX)->irq,
				 &priv->gfargrp[i]);
	}
}

static int gfar_request_irq(struct gfar_private *priv)
{
	int err, i, j;

	for (i = 0; i < priv->num_grps; i++) {
		err = register_grp_irqs(&priv->gfargrp[i]);
		if (err) {
			for (j = 0; j < i; j++)
				free_grp_irqs(&priv->gfargrp[j]);
			return err;
		}
	}

	return 0;
}

/* Bring the controller up and running */
int startup_gfar(struct net_device *ndev)
{
	struct gfar_private *priv = netdev_priv(ndev);
	int err;

	gfar_mac_reset(priv);

	err = gfar_alloc_skb_resources(ndev);
	if (err)
		return err;

	gfar_init_tx_rx_base(priv);

	smp_mb__before_atomic();
	clear_bit(GFAR_DOWN, &priv->state);
	smp_mb__after_atomic();

	/* Start Rx/Tx DMA and enable the interrupts */
	gfar_start(priv);

	phy_start(priv->phydev);

	enable_napi(priv);

	netif_tx_wake_all_queues(ndev);

	return 0;
}

/* Called when something needs to use the ethernet device
 * Returns 0 for success.
 */
static int gfar_enet_open(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	int err;

	err = init_phy(dev);
	if (err)
		return err;

	err = gfar_request_irq(priv);
	if (err)
		return err;

	err = startup_gfar(dev);
	if (err)
		return err;

	device_set_wakeup_enable(&dev->dev, priv->wol_en);

	return err;
}

static inline struct txfcb *gfar_add_fcb(struct sk_buff *skb)
{
	struct txfcb *fcb = (struct txfcb *)skb_push(skb, GMAC_FCB_LEN);

	memset(fcb, 0, GMAC_FCB_LEN);

	return fcb;
}

static inline void gfar_tx_checksum(struct sk_buff *skb, struct txfcb *fcb,
				    int fcb_length)
{
	/* If we're here, it's a IP packet with a TCP or UDP
	 * payload.  We set it to checksum, using a pseudo-header
	 * we provide
	 */
	u8 flags = TXFCB_DEFAULT;

	/* Tell the controller what the protocol is
	 * And provide the already calculated phcs
	 */
	if (ip_hdr(skb)->protocol == IPPROTO_UDP) {
		flags |= TXFCB_UDP;
		fcb->phcs = udp_hdr(skb)->check;
	} else
		fcb->phcs = tcp_hdr(skb)->check;

	/* l3os is the distance between the start of the
	 * frame (skb->data) and the start of the IP hdr.
	 * l4os is the distance between the start of the
	 * l3 hdr and the l4 hdr
	 */
	fcb->l3os = (u16)(skb_network_offset(skb) - fcb_length);
	fcb->l4os = skb_network_header_len(skb);

	fcb->flags = flags;
}

void inline gfar_tx_vlan(struct sk_buff *skb, struct txfcb *fcb)
{
	fcb->flags |= TXFCB_VLN;
	fcb->vlctl = skb_vlan_tag_get(skb);
}

static inline struct txbd8 *skip_txbd(struct txbd8 *bdp, int stride,
				      struct txbd8 *base, int ring_size)
{
	struct txbd8 *new_bd = bdp + stride;

	return (new_bd >= (base + ring_size)) ? (new_bd - ring_size) : new_bd;
}

static inline struct txbd8 *next_txbd(struct txbd8 *bdp, struct txbd8 *base,
				      int ring_size)
{
	return skip_txbd(bdp, 1, base, ring_size);
}

/* eTSEC12: csum generation not supported for some fcb offsets */
static inline bool gfar_csum_errata_12(struct gfar_private *priv,
				       unsigned long fcb_addr)
{
	return (gfar_has_errata(priv, GFAR_ERRATA_12) &&
	       (fcb_addr % 0x20) > 0x18);
}

/* eTSEC76: csum generation for frames larger than 2500 may
 * cause excess delays before start of transmission
 */
static inline bool gfar_csum_errata_76(struct gfar_private *priv,
				       unsigned int len)
{
	return (gfar_has_errata(priv, GFAR_ERRATA_76) &&
	       (len > 2500));
}

/* This is called by the kernel when a frame is ready for transmission.
 * It is pointed to by the dev->hard_start_xmit function pointer
 */
static int gfar_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar_priv_tx_q *tx_queue = NULL;
	struct netdev_queue *txq;
	struct gfar __iomem *regs = NULL;
	struct txfcb *fcb = NULL;
	struct txbd8 *txbdp, *txbdp_start, *base, *txbdp_tstamp = NULL;
	u32 lstatus;
	int i, rq = 0;
	int do_tstamp, do_csum, do_vlan;
	u32 bufaddr;
	unsigned long flags;
	unsigned int nr_frags, nr_txbds, bytes_sent, fcb_len = 0;

	rq = skb->queue_mapping;
	tx_queue = priv->tx_queue[rq];
	txq = netdev_get_tx_queue(dev, rq);
	base = tx_queue->tx_bd_base;
	regs = tx_queue->grp->regs;

	do_csum = (CHECKSUM_PARTIAL == skb->ip_summed);
	do_vlan = skb_vlan_tag_present(skb);
	do_tstamp = (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) &&
		    priv->hwts_tx_en;

	if (do_csum || do_vlan)
		fcb_len = GMAC_FCB_LEN;

	/* check if time stamp should be generated */
	if (unlikely(do_tstamp))
		fcb_len = GMAC_FCB_LEN + GMAC_TXPAL_LEN;

	/* make space for additional header when fcb is needed */
	if (fcb_len && unlikely(skb_headroom(skb) < fcb_len)) {
		struct sk_buff *skb_new;

		skb_new = skb_realloc_headroom(skb, fcb_len);
		if (!skb_new) {
			dev->stats.tx_errors++;
			dev_kfree_skb_any(skb);
			return NETDEV_TX_OK;
		}

		if (skb->sk)
			skb_set_owner_w(skb_new, skb->sk);
		dev_consume_skb_any(skb);
		skb = skb_new;
	}

	/* total number of fragments in the SKB */
	nr_frags = skb_shinfo(skb)->nr_frags;

	/* calculate the required number of TxBDs for this skb */
	if (unlikely(do_tstamp))
		nr_txbds = nr_frags + 2;
	else
		nr_txbds = nr_frags + 1;

	/* check if there is space to queue this packet */
	if (nr_txbds > tx_queue->num_txbdfree) {
		/* no space, stop the queue */
		netif_tx_stop_queue(txq);
		dev->stats.tx_fifo_errors++;
		return NETDEV_TX_BUSY;
	}

	/* Update transmit stats */
	bytes_sent = skb->len;
	tx_queue->stats.tx_bytes += bytes_sent;
	/* keep Tx bytes on wire for BQL accounting */
	GFAR_CB(skb)->bytes_sent = bytes_sent;
	tx_queue->stats.tx_packets++;

	txbdp = txbdp_start = tx_queue->cur_tx;
	lstatus = txbdp->lstatus;

	/* Time stamp insertion requires one additional TxBD */
	if (unlikely(do_tstamp))
		txbdp_tstamp = txbdp = next_txbd(txbdp, base,
						 tx_queue->tx_ring_size);

	if (nr_frags == 0) {
		if (unlikely(do_tstamp))
			txbdp_tstamp->lstatus |= BD_LFLAG(TXBD_LAST |
							  TXBD_INTERRUPT);
		else
			lstatus |= BD_LFLAG(TXBD_LAST | TXBD_INTERRUPT);
	} else {
		/* Place the fragment addresses and lengths into the TxBDs */
		for (i = 0; i < nr_frags; i++) {
			unsigned int frag_len;
			/* Point at the next BD, wrapping as needed */
			txbdp = next_txbd(txbdp, base, tx_queue->tx_ring_size);

			frag_len = skb_shinfo(skb)->frags[i].size;

			lstatus = txbdp->lstatus | frag_len |
				  BD_LFLAG(TXBD_READY);

			/* Handle the last BD specially */
			if (i == nr_frags - 1)
				lstatus |= BD_LFLAG(TXBD_LAST | TXBD_INTERRUPT);

			bufaddr = skb_frag_dma_map(priv->dev,
						   &skb_shinfo(skb)->frags[i],
						   0,
						   frag_len,
						   DMA_TO_DEVICE);
			if (unlikely(dma_mapping_error(priv->dev, bufaddr)))
				goto dma_map_err;

			/* set the TxBD length and buffer pointer */
			txbdp->bufPtr = bufaddr;
			txbdp->lstatus = lstatus;
		}

		lstatus = txbdp_start->lstatus;
	}

	/* Add TxPAL between FCB and frame if required */
	if (unlikely(do_tstamp)) {
		skb_push(skb, GMAC_TXPAL_LEN);
		memset(skb->data, 0, GMAC_TXPAL_LEN);
	}

	/* Add TxFCB if required */
	if (fcb_len) {
		fcb = gfar_add_fcb(skb);
		lstatus |= BD_LFLAG(TXBD_TOE);
	}

	/* Set up checksumming */
	if (do_csum) {
		gfar_tx_checksum(skb, fcb, fcb_len);

		if (unlikely(gfar_csum_errata_12(priv, (unsigned long)fcb)) ||
		    unlikely(gfar_csum_errata_76(priv, skb->len))) {
			__skb_pull(skb, GMAC_FCB_LEN);
			skb_checksum_help(skb);
			if (do_vlan || do_tstamp) {
				/* put back a new fcb for vlan/tstamp TOE */
				fcb = gfar_add_fcb(skb);
			} else {
				/* Tx TOE not used */
				lstatus &= ~(BD_LFLAG(TXBD_TOE));
				fcb = NULL;
			}
		}
	}

	if (do_vlan)
		gfar_tx_vlan(skb, fcb);

	/* Setup tx hardware time stamping if requested */
	if (unlikely(do_tstamp)) {
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
		fcb->ptp = 1;
	}

	bufaddr = dma_map_single(priv->dev, skb->data, skb_headlen(skb),
				 DMA_TO_DEVICE);
	if (unlikely(dma_mapping_error(priv->dev, bufaddr)))
		goto dma_map_err;

	txbdp_start->bufPtr = bufaddr;

	/* If time stamping is requested one additional TxBD must be set up. The
	 * first TxBD points to the FCB and must have a data length of
	 * GMAC_FCB_LEN. The second TxBD points to the actual frame data with
	 * the full frame length.
	 */
	if (unlikely(do_tstamp)) {
		txbdp_tstamp->bufPtr = txbdp_start->bufPtr + fcb_len;
		txbdp_tstamp->lstatus |= BD_LFLAG(TXBD_READY) |
					 (skb_headlen(skb) - fcb_len);
		lstatus |= BD_LFLAG(TXBD_CRC | TXBD_READY) | GMAC_FCB_LEN;
	} else {
		lstatus |= BD_LFLAG(TXBD_CRC | TXBD_READY) | skb_headlen(skb);
	}

	netdev_tx_sent_queue(txq, bytes_sent);

	/* We can work in parallel with gfar_clean_tx_ring(), except
	 * when modifying num_txbdfree. Note that we didn't grab the lock
	 * when we were reading the num_txbdfree and checking for available
	 * space, that's because outside of this function it can only grow,
	 * and once we've got needed space, it cannot suddenly disappear.
	 *
	 * The lock also protects us from gfar_error(), which can modify
	 * regs->tstat and thus retrigger the transfers, which is why we
	 * also must grab the lock before setting ready bit for the first
	 * to be transmitted BD.
	 */
	spin_lock_irqsave(&tx_queue->txlock, flags);

	gfar_wmb();

	txbdp_start->lstatus = lstatus;

	gfar_wmb(); /* force lstatus write before tx_skbuff */

	tx_queue->tx_skbuff[tx_queue->skb_curtx] = skb;

	/* Update the current skb pointer to the next entry we will use
	 * (wrapping if necessary)
	 */
	tx_queue->skb_curtx = (tx_queue->skb_curtx + 1) &
			      TX_RING_MOD_MASK(tx_queue->tx_ring_size);

	tx_queue->cur_tx = next_txbd(txbdp, base, tx_queue->tx_ring_size);

	/* reduce TxBD free count */
	tx_queue->num_txbdfree -= (nr_txbds);

	/* If the next BD still needs to be cleaned up, then the bds
	 * are full.  We need to tell the kernel to stop sending us stuff.
	 */
	if (!tx_queue->num_txbdfree) {
		netif_tx_stop_queue(txq);

		dev->stats.tx_fifo_errors++;
	}

	/* Tell the DMA to go go go */
	gfar_write(&regs->tstat, TSTAT_CLEAR_THALT >> tx_queue->qindex);

	/* Unlock priv */
	spin_unlock_irqrestore(&tx_queue->txlock, flags);

	return NETDEV_TX_OK;

dma_map_err:
	txbdp = next_txbd(txbdp_start, base, tx_queue->tx_ring_size);
	if (do_tstamp)
		txbdp = next_txbd(txbdp, base, tx_queue->tx_ring_size);
	for (i = 0; i < nr_frags; i++) {
		lstatus = txbdp->lstatus;
		if (!(lstatus & BD_LFLAG(TXBD_READY)))
			break;

		txbdp->lstatus = lstatus & ~BD_LFLAG(TXBD_READY);
		bufaddr = txbdp->bufPtr;
		dma_unmap_page(priv->dev, bufaddr, txbdp->length,
			       DMA_TO_DEVICE);
		txbdp = next_txbd(txbdp, base, tx_queue->tx_ring_size);
	}
	gfar_wmb();
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/* Stops the kernel queue, and halts the controller */
static int gfar_close(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);

	cancel_work_sync(&priv->reset_task);
	stop_gfar(dev);

	/* Disconnect from the PHY */
	phy_disconnect(priv->phydev);
	priv->phydev = NULL;

	gfar_free_irq(priv);

	return 0;
}

/* Changes the mac address if the controller is not running. */
static int gfar_set_mac_address(struct net_device *dev)
{
	gfar_set_mac_for_addr(dev, 0, dev->dev_addr);

	return 0;
}

static int gfar_change_mtu(struct net_device *dev, int new_mtu)
{
	struct gfar_private *priv = netdev_priv(dev);
	int frame_size = new_mtu + ETH_HLEN;

	if ((frame_size < 64) || (frame_size > JUMBO_FRAME_SIZE)) {
		netif_err(priv, drv, dev, "Invalid MTU setting\n");
		return -EINVAL;
	}

	while (test_and_set_bit_lock(GFAR_RESETTING, &priv->state))
		cpu_relax();

	if (dev->flags & IFF_UP)
		stop_gfar(dev);

	dev->mtu = new_mtu;

	if (dev->flags & IFF_UP)
		startup_gfar(dev);

	clear_bit_unlock(GFAR_RESETTING, &priv->state);

	return 0;
}

void reset_gfar(struct net_device *ndev)
{
	struct gfar_private *priv = netdev_priv(ndev);

	while (test_and_set_bit_lock(GFAR_RESETTING, &priv->state))
		cpu_relax();

	stop_gfar(ndev);
	startup_gfar(ndev);

	clear_bit_unlock(GFAR_RESETTING, &priv->state);
}

/* gfar_reset_task gets scheduled when a packet has not been
 * transmitted after a set amount of time.
 * For now, assume that clearing out all the structures, and
 * starting over will fix the problem.
 */
static void gfar_reset_task(struct work_struct *work)
{
	struct gfar_private *priv = container_of(work, struct gfar_private,
						 reset_task);
	reset_gfar(priv->ndev);
}

static void gfar_timeout(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);

	dev->stats.tx_errors++;
	schedule_work(&priv->reset_task);
}

static void gfar_align_skb(struct sk_buff *skb)
{
	/* We need the data buffer to be aligned properly.  We will reserve
	 * as many bytes as needed to align the data properly
	 */
	skb_reserve(skb, RXBUF_ALIGNMENT -
		    (((unsigned long) skb->data) & (RXBUF_ALIGNMENT - 1)));
}

/* Interrupt Handler for Transmit complete */
static void gfar_clean_tx_ring(struct gfar_priv_tx_q *tx_queue)
{
	struct net_device *dev = tx_queue->dev;
	struct netdev_queue *txq;
	struct gfar_private *priv = netdev_priv(dev);
	struct txbd8 *bdp, *next = NULL;
	struct txbd8 *lbdp = NULL;
	struct txbd8 *base = tx_queue->tx_bd_base;
	struct sk_buff *skb;
	int skb_dirtytx;
	int tx_ring_size = tx_queue->tx_ring_size;
	int frags = 0, nr_txbds = 0;
	int i;
	int howmany = 0;
	int tqi = tx_queue->qindex;
	unsigned int bytes_sent = 0;
	u32 lstatus;
	size_t buflen;

	txq = netdev_get_tx_queue(dev, tqi);
	bdp = tx_queue->dirty_tx;
	skb_dirtytx = tx_queue->skb_dirtytx;

	while ((skb = tx_queue->tx_skbuff[skb_dirtytx])) {
		unsigned long flags;

		frags = skb_shinfo(skb)->nr_frags;

		/* When time stamping, one additional TxBD must be freed.
		 * Also, we need to dma_unmap_single() the TxPAL.
		 */
		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS))
			nr_txbds = frags + 2;
		else
			nr_txbds = frags + 1;

		lbdp = skip_txbd(bdp, nr_txbds - 1, base, tx_ring_size);

		lstatus = lbdp->lstatus;

		/* Only clean completed frames */
		if ((lstatus & BD_LFLAG(TXBD_READY)) &&
		    (lstatus & BD_LENGTH_MASK))
			break;

		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS)) {
			next = next_txbd(bdp, base, tx_ring_size);
			buflen = next->length + GMAC_FCB_LEN + GMAC_TXPAL_LEN;
		} else
			buflen = bdp->length;

		dma_unmap_single(priv->dev, bdp->bufPtr,
				 buflen, DMA_TO_DEVICE);

		if (unlikely(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS)) {
			struct skb_shared_hwtstamps shhwtstamps;
			u64 *ns = (u64*) (((u32)skb->data + 0x10) & ~0x7);

			memset(&shhwtstamps, 0, sizeof(shhwtstamps));
			shhwtstamps.hwtstamp = ns_to_ktime(*ns);
			skb_pull(skb, GMAC_FCB_LEN + GMAC_TXPAL_LEN);
			skb_tstamp_tx(skb, &shhwtstamps);
			bdp->lstatus &= BD_LFLAG(TXBD_WRAP);
			bdp = next;
		}

		bdp->lstatus &= BD_LFLAG(TXBD_WRAP);
		bdp = next_txbd(bdp, base, tx_ring_size);

		for (i = 0; i < frags; i++) {
			dma_unmap_page(priv->dev, bdp->bufPtr,
				       bdp->length, DMA_TO_DEVICE);
			bdp->lstatus &= BD_LFLAG(TXBD_WRAP);
			bdp = next_txbd(bdp, base, tx_ring_size);
		}

		bytes_sent += GFAR_CB(skb)->bytes_sent;

		dev_kfree_skb_any(skb);

		tx_queue->tx_skbuff[skb_dirtytx] = NULL;

		skb_dirtytx = (skb_dirtytx + 1) &
			      TX_RING_MOD_MASK(tx_ring_size);

		howmany++;
		spin_lock_irqsave(&tx_queue->txlock, flags);
		tx_queue->num_txbdfree += nr_txbds;
		spin_unlock_irqrestore(&tx_queue->txlock, flags);
	}

	/* If we freed a buffer, we can restart transmission, if necessary */
	if (tx_queue->num_txbdfree &&
	    netif_tx_queue_stopped(txq) &&
	    !(test_bit(GFAR_DOWN, &priv->state)))
		netif_wake_subqueue(priv->ndev, tqi);

	/* Update dirty indicators */
	tx_queue->skb_dirtytx = skb_dirtytx;
	tx_queue->dirty_tx = bdp;

	netdev_tx_completed_queue(txq, howmany, bytes_sent);
}

static struct sk_buff *gfar_alloc_skb(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct sk_buff *skb;

	skb = netdev_alloc_skb(dev, priv->rx_buffer_size + RXBUF_ALIGNMENT);
	if (!skb)
		return NULL;

	gfar_align_skb(skb);

	return skb;
}

static struct sk_buff *gfar_new_skb(struct net_device *dev, dma_addr_t *bufaddr)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct sk_buff *skb;
	dma_addr_t addr;

	skb = gfar_alloc_skb(dev);
	if (!skb)
		return NULL;

	addr = dma_map_single(priv->dev, skb->data,
			      priv->rx_buffer_size, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(priv->dev, addr))) {
		dev_kfree_skb_any(skb);
		return NULL;
	}

	*bufaddr = addr;
	return skb;
}

static inline void count_errors(unsigned short status, struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct net_device_stats *stats = &dev->stats;
	struct gfar_extra_stats *estats = &priv->extra_stats;

	/* If the packet was truncated, none of the other errors matter */
	if (status & RXBD_TRUNCATED) {
		stats->rx_length_errors++;

		atomic64_inc(&estats->rx_trunc);

		return;
	}
	/* Count the errors, if there were any */
	if (status & (RXBD_LARGE | RXBD_SHORT)) {
		stats->rx_length_errors++;

		if (status & RXBD_LARGE)
			atomic64_inc(&estats->rx_large);
		else
			atomic64_inc(&estats->rx_short);
	}
	if (status & RXBD_NONOCTET) {
		stats->rx_frame_errors++;
		atomic64_inc(&estats->rx_nonoctet);
	}
	if (status & RXBD_CRCERR) {
		atomic64_inc(&estats->rx_crcerr);
		stats->rx_crc_errors++;
	}
	if (status & RXBD_OVERRUN) {
		atomic64_inc(&estats->rx_overrun);
		stats->rx_crc_errors++;
	}
}

irqreturn_t gfar_receive(int irq, void *grp_id)
{
	struct gfar_priv_grp *grp = (struct gfar_priv_grp *)grp_id;
	unsigned long flags;
	u32 imask;

	if (likely(napi_schedule_prep(&grp->napi_rx))) {
		spin_lock_irqsave(&grp->grplock, flags);
		imask = gfar_read(&grp->regs->imask);
		imask &= IMASK_RX_DISABLED;
		gfar_write(&grp->regs->imask, imask);
		spin_unlock_irqrestore(&grp->grplock, flags);
		__napi_schedule(&grp->napi_rx);
	} else {
		/* Clear IEVENT, so interrupts aren't called again
		 * because of the packets that have already arrived.
		 */
		gfar_write(&grp->regs->ievent, IEVENT_RX_MASK);
	}

	return IRQ_HANDLED;
}

/* Interrupt Handler for Transmit complete */
static irqreturn_t gfar_transmit(int irq, void *grp_id)
{
	struct gfar_priv_grp *grp = (struct gfar_priv_grp *)grp_id;
	unsigned long flags;
	u32 imask;

	if (likely(napi_schedule_prep(&grp->napi_tx))) {
		spin_lock_irqsave(&grp->grplock, flags);
		imask = gfar_read(&grp->regs->imask);
		imask &= IMASK_TX_DISABLED;
		gfar_write(&grp->regs->imask, imask);
		spin_unlock_irqrestore(&grp->grplock, flags);
		__napi_schedule(&grp->napi_tx);
	} else {
		/* Clear IEVENT, so interrupts aren't called again
		 * because of the packets that have already arrived.
		 */
		gfar_write(&grp->regs->ievent, IEVENT_TX_MASK);
	}

	return IRQ_HANDLED;
}

static inline void gfar_rx_checksum(struct sk_buff *skb, struct rxfcb *fcb)
{
	/* If valid headers were found, and valid sums
	 * were verified, then we tell the kernel that no
	 * checksumming is necessary.  Otherwise, it is [FIXME]
	 */
	if ((fcb->flags & RXFCB_CSUM_MASK) == (RXFCB_CIP | RXFCB_CTU))
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	else
		skb_checksum_none_assert(skb);
}


/* gfar_process_frame() -- handle one incoming packet if skb isn't NULL. */
static void gfar_process_frame(struct net_device *dev, struct sk_buff *skb,
			       int amount_pull, struct napi_struct *napi)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct rxfcb *fcb = NULL;

	/* fcb is at the beginning if exists */
	fcb = (struct rxfcb *)skb->data;

	/* Remove the FCB from the skb
	 * Remove the padded bytes, if there are any
	 */
	if (amount_pull) {
		skb_record_rx_queue(skb, fcb->rq);
		skb_pull(skb, amount_pull);
	}

	/* Get receive timestamp from the skb */
	if (priv->hwts_rx_en) {
		struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);
		u64 *ns = (u64 *) skb->data;

		memset(shhwtstamps, 0, sizeof(*shhwtstamps));
		shhwtstamps->hwtstamp = ns_to_ktime(*ns);
	}

	if (priv->padding)
		skb_pull(skb, priv->padding);

	if (dev->features & NETIF_F_RXCSUM)
		gfar_rx_checksum(skb, fcb);

	/* Tell the skb what kind of packet this is */
	skb->protocol = eth_type_trans(skb, dev);

	/* There's need to check for NETIF_F_HW_VLAN_CTAG_RX here.
	 * Even if vlan rx accel is disabled, on some chips
	 * RXFCB_VLN is pseudo randomly set.
	 */
	if (dev->features & NETIF_F_HW_VLAN_CTAG_RX &&
	    fcb->flags & RXFCB_VLN)
		__vlan_hwaccel_put_tag(skb, htons(ETH_P_8021Q), fcb->vlctl);

	/* Send the packet up the stack */
	napi_gro_receive(napi, skb);

}

/* gfar_clean_rx_ring() -- Processes each frame in the rx ring
 * until the budget/quota has been reached. Returns the number
 * of frames handled
 */
int gfar_clean_rx_ring(struct gfar_priv_rx_q *rx_queue, int rx_work_limit)
{
	struct net_device *dev = rx_queue->dev;
	struct rxbd8 *bdp, *base;
	struct sk_buff *skb;
	int pkt_len;
	int amount_pull;
	int howmany = 0;
	struct gfar_private *priv = netdev_priv(dev);

	/* Get the first full descriptor */
	bdp = rx_queue->cur_rx;
	base = rx_queue->rx_bd_base;

	amount_pull = priv->uses_rxfcb ? GMAC_FCB_LEN : 0;

	while (!((bdp->status & RXBD_EMPTY) || (--rx_work_limit < 0))) {
		struct sk_buff *newskb;
		dma_addr_t bufaddr;

		rmb();

		/* Add another skb for the future */
		newskb = gfar_new_skb(dev, &bufaddr);

		skb = rx_queue->rx_skbuff[rx_queue->skb_currx];

		dma_unmap_single(priv->dev, bdp->bufPtr,
				 priv->rx_buffer_size, DMA_FROM_DEVICE);

		if (unlikely(!(bdp->status & RXBD_ERR) &&
			     bdp->length > priv->rx_buffer_size))
			bdp->status = RXBD_LARGE;

		/* We drop the frame if we failed to allocate a new buffer */
		if (unlikely(!newskb || !(bdp->status & RXBD_LAST) ||
			     bdp->status & RXBD_ERR)) {
			count_errors(bdp->status, dev);

			if (unlikely(!newskb)) {
				newskb = skb;
				bufaddr = bdp->bufPtr;
			} else if (skb)
				dev_kfree_skb(skb);
		} else {
			/* Increment the number of packets */
			rx_queue->stats.rx_packets++;
			howmany++;

			if (likely(skb)) {
				pkt_len = bdp->length - ETH_FCS_LEN;
				/* Remove the FCS from the packet length */
				skb_put(skb, pkt_len);
				rx_queue->stats.rx_bytes += pkt_len;
				skb_record_rx_queue(skb, rx_queue->qindex);
				gfar_process_frame(dev, skb, amount_pull,
						   &rx_queue->grp->napi_rx);

			} else {
				netif_warn(priv, rx_err, dev, "Missing skb!\n");
				rx_queue->stats.rx_dropped++;
				atomic64_inc(&priv->extra_stats.rx_skbmissing);
			}

		}

		rx_queue->rx_skbuff[rx_queue->skb_currx] = newskb;

		/* Setup the new bdp */
		gfar_init_rxbdp(rx_queue, bdp, bufaddr);

		/* Update Last Free RxBD pointer for LFC */
		if (unlikely(rx_queue->rfbptr && priv->tx_actual_en))
			gfar_write(rx_queue->rfbptr, (u32)bdp);

		/* Update to the next pointer */
		bdp = next_bd(bdp, base, rx_queue->rx_ring_size);

		/* update to point at the next skb */
		rx_queue->skb_currx = (rx_queue->skb_currx + 1) &
				      RX_RING_MOD_MASK(rx_queue->rx_ring_size);
	}

	/* Update the current rxbd pointer to be the next one */
	rx_queue->cur_rx = bdp;

	return howmany;
}

static int gfar_poll_rx_sq(struct napi_struct *napi, int budget)
{
	struct gfar_priv_grp *gfargrp =
		container_of(napi, struct gfar_priv_grp, napi_rx);
	struct gfar __iomem *regs = gfargrp->regs;
	struct gfar_priv_rx_q *rx_queue = gfargrp->rx_queue;
	int work_done = 0;

	/* Clear IEVENT, so interrupts aren't called again
	 * because of the packets that have already arrived
	 */
	gfar_write(&regs->ievent, IEVENT_RX_MASK);

	work_done = gfar_clean_rx_ring(rx_queue, budget);

	if (work_done < budget) {
		u32 imask;
		napi_complete(napi);
		/* Clear the halt bit in RSTAT */
		gfar_write(&regs->rstat, gfargrp->rstat);

		spin_lock_irq(&gfargrp->grplock);
		imask = gfar_read(&regs->imask);
		imask |= IMASK_RX_DEFAULT;
		gfar_write(&regs->imask, imask);
		spin_unlock_irq(&gfargrp->grplock);
	}

	return work_done;
}

static int gfar_poll_tx_sq(struct napi_struct *napi, int budget)
{
	struct gfar_priv_grp *gfargrp =
		container_of(napi, struct gfar_priv_grp, napi_tx);
	struct gfar __iomem *regs = gfargrp->regs;
	struct gfar_priv_tx_q *tx_queue = gfargrp->tx_queue;
	u32 imask;

	/* Clear IEVENT, so interrupts aren't called again
	 * because of the packets that have already arrived
	 */
	gfar_write(&regs->ievent, IEVENT_TX_MASK);

	/* run Tx cleanup to completion */
	if (tx_queue->tx_skbuff[tx_queue->skb_dirtytx])
		gfar_clean_tx_ring(tx_queue);

	napi_complete(napi);

	spin_lock_irq(&gfargrp->grplock);
	imask = gfar_read(&regs->imask);
	imask |= IMASK_TX_DEFAULT;
	gfar_write(&regs->imask, imask);
	spin_unlock_irq(&gfargrp->grplock);

	return 0;
}

static int gfar_poll_rx(struct napi_struct *napi, int budget)
{
	struct gfar_priv_grp *gfargrp =
		container_of(napi, struct gfar_priv_grp, napi_rx);
	struct gfar_private *priv = gfargrp->priv;
	struct gfar __iomem *regs = gfargrp->regs;
	struct gfar_priv_rx_q *rx_queue = NULL;
	int work_done = 0, work_done_per_q = 0;
	int i, budget_per_q = 0;
	unsigned long rstat_rxf;
	int num_act_queues;

	/* Clear IEVENT, so interrupts aren't called again
	 * because of the packets that have already arrived
	 */
	gfar_write(&regs->ievent, IEVENT_RX_MASK);

	rstat_rxf = gfar_read(&regs->rstat) & RSTAT_RXF_MASK;

	num_act_queues = bitmap_weight(&rstat_rxf, MAX_RX_QS);
	if (num_act_queues)
		budget_per_q = budget/num_act_queues;

	for_each_set_bit(i, &gfargrp->rx_bit_map, priv->num_rx_queues) {
		/* skip queue if not active */
		if (!(rstat_rxf & (RSTAT_CLEAR_RXF0 >> i)))
			continue;

		rx_queue = priv->rx_queue[i];
		work_done_per_q =
			gfar_clean_rx_ring(rx_queue, budget_per_q);
		work_done += work_done_per_q;

		/* finished processing this queue */
		if (work_done_per_q < budget_per_q) {
			/* clear active queue hw indication */
			gfar_write(&regs->rstat,
				   RSTAT_CLEAR_RXF0 >> i);
			num_act_queues--;

			if (!num_act_queues)
				break;
		}
	}

	if (!num_act_queues) {
		u32 imask;
		napi_complete(napi);

		/* Clear the halt bit in RSTAT */
		gfar_write(&regs->rstat, gfargrp->rstat);

		spin_lock_irq(&gfargrp->grplock);
		imask = gfar_read(&regs->imask);
		imask |= IMASK_RX_DEFAULT;
		gfar_write(&regs->imask, imask);
		spin_unlock_irq(&gfargrp->grplock);
	}

	return work_done;
}

static int gfar_poll_tx(struct napi_struct *napi, int budget)
{
	struct gfar_priv_grp *gfargrp =
		container_of(napi, struct gfar_priv_grp, napi_tx);
	struct gfar_private *priv = gfargrp->priv;
	struct gfar __iomem *regs = gfargrp->regs;
	struct gfar_priv_tx_q *tx_queue = NULL;
	int has_tx_work = 0;
	int i;

	/* Clear IEVENT, so interrupts aren't called again
	 * because of the packets that have already arrived
	 */
	gfar_write(&regs->ievent, IEVENT_TX_MASK);

	for_each_set_bit(i, &gfargrp->tx_bit_map, priv->num_tx_queues) {
		tx_queue = priv->tx_queue[i];
		/* run Tx cleanup to completion */
		if (tx_queue->tx_skbuff[tx_queue->skb_dirtytx]) {
			gfar_clean_tx_ring(tx_queue);
			has_tx_work = 1;
		}
	}

	if (!has_tx_work) {
		u32 imask;
		napi_complete(napi);

		spin_lock_irq(&gfargrp->grplock);
		imask = gfar_read(&regs->imask);
		imask |= IMASK_TX_DEFAULT;
		gfar_write(&regs->imask, imask);
		spin_unlock_irq(&gfargrp->grplock);
	}

	return 0;
}


#ifdef CONFIG_NET_POLL_CONTROLLER
/* Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void gfar_netpoll(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	int i;

	/* If the device has multiple interrupts, run tx/rx */
	if (priv->device_flags & FSL_GIANFAR_DEV_HAS_MULTI_INTR) {
		for (i = 0; i < priv->num_grps; i++) {
			struct gfar_priv_grp *grp = &priv->gfargrp[i];

			disable_irq(gfar_irq(grp, TX)->irq);
			disable_irq(gfar_irq(grp, RX)->irq);
			disable_irq(gfar_irq(grp, ER)->irq);
			gfar_interrupt(gfar_irq(grp, TX)->irq, grp);
			enable_irq(gfar_irq(grp, ER)->irq);
			enable_irq(gfar_irq(grp, RX)->irq);
			enable_irq(gfar_irq(grp, TX)->irq);
		}
	} else {
		for (i = 0; i < priv->num_grps; i++) {
			struct gfar_priv_grp *grp = &priv->gfargrp[i];

			disable_irq(gfar_irq(grp, TX)->irq);
			gfar_interrupt(gfar_irq(grp, TX)->irq, grp);
			enable_irq(gfar_irq(grp, TX)->irq);
		}
	}
}
#endif

/* The interrupt handler for devices with one interrupt */
static irqreturn_t gfar_interrupt(int irq, void *grp_id)
{
	struct gfar_priv_grp *gfargrp = grp_id;

	/* Save ievent for future reference */
	u32 events = gfar_read(&gfargrp->regs->ievent);

	/* Check for reception */
	if (events & IEVENT_RX_MASK)
		gfar_receive(irq, grp_id);

	/* Check for transmit completion */
	if (events & IEVENT_TX_MASK)
		gfar_transmit(irq, grp_id);

	/* Check for errors */
	if (events & IEVENT_ERR_MASK)
		gfar_error(irq, grp_id);

	return IRQ_HANDLED;
}

/* Called every time the controller might need to be made
 * aware of new link state.  The PHY code conveys this
 * information through variables in the phydev structure, and this
 * function converts those variables into the appropriate
 * register values, and can bring down the device if needed.
 */
static void adjust_link(struct net_device *dev)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;

	if (unlikely(phydev->link != priv->oldlink ||
		     phydev->duplex != priv->oldduplex ||
		     phydev->speed != priv->oldspeed))
		gfar_update_link_state(priv);
}

/* Update the hash table based on the current list of multicast
 * addresses we subscribe to.  Also, change the promiscuity of
 * the device based on the flags (this function is called
 * whenever dev->flags is changed
 */
static void gfar_set_multi(struct net_device *dev)
{
	struct netdev_hw_addr *ha;
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 tempval;

	if (dev->flags & IFF_PROMISC) {
		/* Set RCTRL to PROM */
		tempval = gfar_read(&regs->rctrl);
		tempval |= RCTRL_PROM;
		gfar_write(&regs->rctrl, tempval);
	} else {
		/* Set RCTRL to not PROM */
		tempval = gfar_read(&regs->rctrl);
		tempval &= ~(RCTRL_PROM);
		gfar_write(&regs->rctrl, tempval);
	}

	if (dev->flags & IFF_ALLMULTI) {
		/* Set the hash to rx all multicast frames */
		gfar_write(&regs->igaddr0, 0xffffffff);
		gfar_write(&regs->igaddr1, 0xffffffff);
		gfar_write(&regs->igaddr2, 0xffffffff);
		gfar_write(&regs->igaddr3, 0xffffffff);
		gfar_write(&regs->igaddr4, 0xffffffff);
		gfar_write(&regs->igaddr5, 0xffffffff);
		gfar_write(&regs->igaddr6, 0xffffffff);
		gfar_write(&regs->igaddr7, 0xffffffff);
		gfar_write(&regs->gaddr0, 0xffffffff);
		gfar_write(&regs->gaddr1, 0xffffffff);
		gfar_write(&regs->gaddr2, 0xffffffff);
		gfar_write(&regs->gaddr3, 0xffffffff);
		gfar_write(&regs->gaddr4, 0xffffffff);
		gfar_write(&regs->gaddr5, 0xffffffff);
		gfar_write(&regs->gaddr6, 0xffffffff);
		gfar_write(&regs->gaddr7, 0xffffffff);
	} else {
		int em_num;
		int idx;

		/* zero out the hash */
		gfar_write(&regs->igaddr0, 0x0);
		gfar_write(&regs->igaddr1, 0x0);
		gfar_write(&regs->igaddr2, 0x0);
		gfar_write(&regs->igaddr3, 0x0);
		gfar_write(&regs->igaddr4, 0x0);
		gfar_write(&regs->igaddr5, 0x0);
		gfar_write(&regs->igaddr6, 0x0);
		gfar_write(&regs->igaddr7, 0x0);
		gfar_write(&regs->gaddr0, 0x0);
		gfar_write(&regs->gaddr1, 0x0);
		gfar_write(&regs->gaddr2, 0x0);
		gfar_write(&regs->gaddr3, 0x0);
		gfar_write(&regs->gaddr4, 0x0);
		gfar_write(&regs->gaddr5, 0x0);
		gfar_write(&regs->gaddr6, 0x0);
		gfar_write(&regs->gaddr7, 0x0);

		/* If we have extended hash tables, we need to
		 * clear the exact match registers to prepare for
		 * setting them
		 */
		if (priv->extended_hash) {
			em_num = GFAR_EM_NUM + 1;
			gfar_clear_exact_match(dev);
			idx = 1;
		} else {
			idx = 0;
			em_num = 0;
		}

		if (netdev_mc_empty(dev))
			return;

		/* Parse the list, and set the appropriate bits */
		netdev_for_each_mc_addr(ha, dev) {
			if (idx < em_num) {
				gfar_set_mac_for_addr(dev, idx, ha->addr);
				idx++;
			} else
				gfar_set_hash_for_addr(dev, ha->addr);
		}
	}
}


/* Clears each of the exact match registers to zero, so they
 * don't interfere with normal reception
 */
static void gfar_clear_exact_match(struct net_device *dev)
{
	int idx;
	static const u8 zero_arr[ETH_ALEN] = {0, 0, 0, 0, 0, 0};

	for (idx = 1; idx < GFAR_EM_NUM + 1; idx++)
		gfar_set_mac_for_addr(dev, idx, zero_arr);
}

/* Set the appropriate hash bit for the given addr */
/* The algorithm works like so:
 * 1) Take the Destination Address (ie the multicast address), and
 * do a CRC on it (little endian), and reverse the bits of the
 * result.
 * 2) Use the 8 most significant bits as a hash into a 256-entry
 * table.  The table is controlled through 8 32-bit registers:
 * gaddr0-7.  gaddr0's MSB is entry 0, and gaddr7's LSB is
 * gaddr7.  This means that the 3 most significant bits in the
 * hash index which gaddr register to use, and the 5 other bits
 * indicate which bit (assuming an IBM numbering scheme, which
 * for PowerPC (tm) is usually the case) in the register holds
 * the entry.
 */
static void gfar_set_hash_for_addr(struct net_device *dev, u8 *addr)
{
	u32 tempval;
	struct gfar_private *priv = netdev_priv(dev);
	u32 result = ether_crc(ETH_ALEN, addr);
	int width = priv->hash_width;
	u8 whichbit = (result >> (32 - width)) & 0x1f;
	u8 whichreg = result >> (32 - width + 5);
	u32 value = (1 << (31-whichbit));

	tempval = gfar_read(priv->hash_regs[whichreg]);
	tempval |= value;
	gfar_write(priv->hash_regs[whichreg], tempval);
}


/* There are multiple MAC Address register pairs on some controllers
 * This function sets the numth pair to a given address
 */
static void gfar_set_mac_for_addr(struct net_device *dev, int num,
				  const u8 *addr)
{
	struct gfar_private *priv = netdev_priv(dev);
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	u32 tempval;
	u32 __iomem *macptr = &regs->macstnaddr1;

	macptr += num*2;

	/* For a station address of 0x12345678ABCD in transmission
	 * order (BE), MACnADDR1 is set to 0xCDAB7856 and
	 * MACnADDR2 is set to 0x34120000.
	 */
	tempval = (addr[5] << 24) | (addr[4] << 16) |
		  (addr[3] << 8)  |  addr[2];

	gfar_write(macptr, tempval);

	tempval = (addr[1] << 24) | (addr[0] << 16);

	gfar_write(macptr+1, tempval);
}

/* GFAR error interrupt handler */
static irqreturn_t gfar_error(int irq, void *grp_id)
{
	struct gfar_priv_grp *gfargrp = grp_id;
	struct gfar __iomem *regs = gfargrp->regs;
	struct gfar_private *priv= gfargrp->priv;
	struct net_device *dev = priv->ndev;

	/* Save ievent for future reference */
	u32 events = gfar_read(&regs->ievent);

	/* Clear IEVENT */
	gfar_write(&regs->ievent, events & IEVENT_ERR_MASK);

	/* Magic Packet is not an error. */
	if ((priv->device_flags & FSL_GIANFAR_DEV_HAS_MAGIC_PACKET) &&
	    (events & IEVENT_MAG))
		events &= ~IEVENT_MAG;

	/* Hmm... */
	if (netif_msg_rx_err(priv) || netif_msg_tx_err(priv))
		netdev_dbg(dev,
			   "error interrupt (ievent=0x%08x imask=0x%08x)\n",
			   events, gfar_read(&regs->imask));

	/* Update the error counters */
	if (events & IEVENT_TXE) {
		dev->stats.tx_errors++;

		if (events & IEVENT_LC)
			dev->stats.tx_window_errors++;
		if (events & IEVENT_CRL)
			dev->stats.tx_aborted_errors++;
		if (events & IEVENT_XFUN) {
			unsigned long flags;

			netif_dbg(priv, tx_err, dev,
				  "TX FIFO underrun, packet dropped\n");
			dev->stats.tx_dropped++;
			atomic64_inc(&priv->extra_stats.tx_underrun);

			local_irq_save(flags);
			lock_tx_qs(priv);

			/* Reactivate the Tx Queues */
			gfar_write(&regs->tstat, gfargrp->tstat);

			unlock_tx_qs(priv);
			local_irq_restore(flags);
		}
		netif_dbg(priv, tx_err, dev, "Transmit Error\n");
	}
	if (events & IEVENT_BSY) {
		dev->stats.rx_errors++;
		atomic64_inc(&priv->extra_stats.rx_bsy);

		gfar_receive(irq, grp_id);

		netif_dbg(priv, rx_err, dev, "busy error (rstat: %x)\n",
			  gfar_read(&regs->rstat));
	}
	if (events & IEVENT_BABR) {
		dev->stats.rx_errors++;
		atomic64_inc(&priv->extra_stats.rx_babr);

		netif_dbg(priv, rx_err, dev, "babbling RX error\n");
	}
	if (events & IEVENT_EBERR) {
		atomic64_inc(&priv->extra_stats.eberr);
		netif_dbg(priv, rx_err, dev, "bus error\n");
	}
	if (events & IEVENT_RXC)
		netif_dbg(priv, rx_status, dev, "control frame\n");

	if (events & IEVENT_BABT) {
		atomic64_inc(&priv->extra_stats.tx_babt);
		netif_dbg(priv, tx_err, dev, "babbling TX error\n");
	}
	return IRQ_HANDLED;
}

static u32 gfar_get_flowctrl_cfg(struct gfar_private *priv)
{
	struct phy_device *phydev = priv->phydev;
	u32 val = 0;

	if (!phydev->duplex)
		return val;

	if (!priv->pause_aneg_en) {
		if (priv->tx_pause_en)
			val |= MACCFG1_TX_FLOW;
		if (priv->rx_pause_en)
			val |= MACCFG1_RX_FLOW;
	} else {
		u16 lcl_adv, rmt_adv;
		u8 flowctrl;
		/* get link partner capabilities */
		rmt_adv = 0;
		if (phydev->pause)
			rmt_adv = LPA_PAUSE_CAP;
		if (phydev->asym_pause)
			rmt_adv |= LPA_PAUSE_ASYM;

		lcl_adv = 0;
		if (phydev->advertising & ADVERTISED_Pause)
			lcl_adv |= ADVERTISE_PAUSE_CAP;
		if (phydev->advertising & ADVERTISED_Asym_Pause)
			lcl_adv |= ADVERTISE_PAUSE_ASYM;

		flowctrl = mii_resolve_flowctrl_fdx(lcl_adv, rmt_adv);
		if (flowctrl & FLOW_CTRL_TX)
			val |= MACCFG1_TX_FLOW;
		if (flowctrl & FLOW_CTRL_RX)
			val |= MACCFG1_RX_FLOW;
	}

	return val;
}

static noinline void gfar_update_link_state(struct gfar_private *priv)
{
	struct gfar __iomem *regs = priv->gfargrp[0].regs;
	struct phy_device *phydev = priv->phydev;
	struct gfar_priv_rx_q *rx_queue = NULL;
	int i;
	struct rxbd8 *bdp;

	if (unlikely(test_bit(GFAR_RESETTING, &priv->state)))
		return;

	if (phydev->link) {
		u32 tempval1 = gfar_read(&regs->maccfg1);
		u32 tempval = gfar_read(&regs->maccfg2);
		u32 ecntrl = gfar_read(&regs->ecntrl);
		u32 tx_flow_oldval = (tempval & MACCFG1_TX_FLOW);

		if (phydev->duplex != priv->oldduplex) {
			if (!(phydev->duplex))
				tempval &= ~(MACCFG2_FULL_DUPLEX);
			else
				tempval |= MACCFG2_FULL_DUPLEX;

			priv->oldduplex = phydev->duplex;
		}

		if (phydev->speed != priv->oldspeed) {
			switch (phydev->speed) {
			case 1000:
				tempval =
				    ((tempval & ~(MACCFG2_IF)) | MACCFG2_GMII);

				ecntrl &= ~(ECNTRL_R100);
				break;
			case 100:
			case 10:
				tempval =
				    ((tempval & ~(MACCFG2_IF)) | MACCFG2_MII);

				/* Reduced mode distinguishes
				 * between 10 and 100
				 */
				if (phydev->speed == SPEED_100)
					ecntrl |= ECNTRL_R100;
				else
					ecntrl &= ~(ECNTRL_R100);
				break;
			default:
				netif_warn(priv, link, priv->ndev,
					   "Ack!  Speed (%d) is not 10/100/1000!\n",
					   phydev->speed);
				break;
			}

			priv->oldspeed = phydev->speed;
		}

		tempval1 &= ~(MACCFG1_TX_FLOW | MACCFG1_RX_FLOW);
		tempval1 |= gfar_get_flowctrl_cfg(priv);

		/* Turn last free buffer recording on */
		if ((tempval1 & MACCFG1_TX_FLOW) && !tx_flow_oldval) {
			for (i = 0; i < priv->num_rx_queues; i++) {
				rx_queue = priv->rx_queue[i];
				bdp = rx_queue->cur_rx;
				/* skip to previous bd */
				bdp = skip_bd(bdp, rx_queue->rx_ring_size - 1,
					      rx_queue->rx_bd_base,
					      rx_queue->rx_ring_size);

				if (rx_queue->rfbptr)
					gfar_write(rx_queue->rfbptr, (u32)bdp);
			}

			priv->tx_actual_en = 1;
		}

		if (unlikely(!(tempval1 & MACCFG1_TX_FLOW) && tx_flow_oldval))
			priv->tx_actual_en = 0;

		gfar_write(&regs->maccfg1, tempval1);
		gfar_write(&regs->maccfg2, tempval);
		gfar_write(&regs->ecntrl, ecntrl);

		if (!priv->oldlink)
			priv->oldlink = 1;

	} else if (priv->oldlink) {
		priv->oldlink = 0;
		priv->oldspeed = 0;
		priv->oldduplex = -1;
	}

	if (netif_msg_link(priv))
		phy_print_status(phydev);
}

static struct of_device_id gfar_match[] =
{
	{
		.type = "network",
		.compatible = "gianfar",
	},
	{
		.compatible = "fsl,etsec2",
	},
	{},
};
MODULE_DEVICE_TABLE(of, gfar_match);

/* Structure for a device driver */
static struct platform_driver gfar_driver = {
	.driver = {
		.name = "fsl-gianfar",
		.pm = GFAR_PM_OPS,
		.of_match_table = gfar_match,
	},
	.probe = gfar_probe,
	.remove = gfar_remove,
};

module_platform_driver(gfar_driver);
