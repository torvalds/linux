/*
 * Copyright (c) 2005 Ammasso, Inc. All rights reserved.
 * Copyright (c) 2005 Open Grid Computing, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/if_vlan.h>
#include <linux/crc32.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>

#include <rdma/ib_smi.h>
#include "c2.h"
#include "c2_provider.h"

MODULE_AUTHOR("Tom Tucker <tom@opengridcomputing.com>");
MODULE_DESCRIPTION("Ammasso AMSO1100 Low-level iWARP Driver");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRV_VERSION);

static const u32 default_msg = NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK
    | NETIF_MSG_IFUP | NETIF_MSG_IFDOWN;

static int debug = -1;		/* defaults above */
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

static int c2_up(struct net_device *netdev);
static int c2_down(struct net_device *netdev);
static int c2_xmit_frame(struct sk_buff *skb, struct net_device *netdev);
static void c2_tx_interrupt(struct net_device *netdev);
static void c2_rx_interrupt(struct net_device *netdev);
static irqreturn_t c2_interrupt(int irq, void *dev_id);
static void c2_tx_timeout(struct net_device *netdev);
static int c2_change_mtu(struct net_device *netdev, int new_mtu);
static void c2_reset(struct c2_port *c2_port);
static struct net_device_stats *c2_get_stats(struct net_device *netdev);

static struct pci_device_id c2_pci_table[] = {
	{ PCI_DEVICE(0x18b8, 0xb001) },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, c2_pci_table);

static void c2_print_macaddr(struct net_device *netdev)
{
	pr_debug("%s: MAC %02X:%02X:%02X:%02X:%02X:%02X, "
		"IRQ %u\n", netdev->name,
		netdev->dev_addr[0], netdev->dev_addr[1], netdev->dev_addr[2],
		netdev->dev_addr[3], netdev->dev_addr[4], netdev->dev_addr[5],
		netdev->irq);
}

static void c2_set_rxbufsize(struct c2_port *c2_port)
{
	struct net_device *netdev = c2_port->netdev;

	if (netdev->mtu > RX_BUF_SIZE)
		c2_port->rx_buf_size =
		    netdev->mtu + ETH_HLEN + sizeof(struct c2_rxp_hdr) +
		    NET_IP_ALIGN;
	else
		c2_port->rx_buf_size = sizeof(struct c2_rxp_hdr) + RX_BUF_SIZE;
}

/*
 * Allocate TX ring elements and chain them together.
 * One-to-one association of adapter descriptors with ring elements.
 */
static int c2_tx_ring_alloc(struct c2_ring *tx_ring, void *vaddr,
			    dma_addr_t base, void __iomem * mmio_txp_ring)
{
	struct c2_tx_desc *tx_desc;
	struct c2_txp_desc __iomem *txp_desc;
	struct c2_element *elem;
	int i;

	tx_ring->start = kmalloc(sizeof(*elem) * tx_ring->count, GFP_KERNEL);
	if (!tx_ring->start)
		return -ENOMEM;

	elem = tx_ring->start;
	tx_desc = vaddr;
	txp_desc = mmio_txp_ring;
	for (i = 0; i < tx_ring->count; i++, elem++, tx_desc++, txp_desc++) {
		tx_desc->len = 0;
		tx_desc->status = 0;

		/* Set TXP_HTXD_UNINIT */
		__raw_writeq(cpu_to_be64(0x1122334455667788ULL),
			     (void __iomem *) txp_desc + C2_TXP_ADDR);
		__raw_writew(0, (void __iomem *) txp_desc + C2_TXP_LEN);
		__raw_writew(cpu_to_be16(TXP_HTXD_UNINIT),
			     (void __iomem *) txp_desc + C2_TXP_FLAGS);

		elem->skb = NULL;
		elem->ht_desc = tx_desc;
		elem->hw_desc = txp_desc;

		if (i == tx_ring->count - 1) {
			elem->next = tx_ring->start;
			tx_desc->next_offset = base;
		} else {
			elem->next = elem + 1;
			tx_desc->next_offset =
			    base + (i + 1) * sizeof(*tx_desc);
		}
	}

	tx_ring->to_use = tx_ring->to_clean = tx_ring->start;

	return 0;
}

/*
 * Allocate RX ring elements and chain them together.
 * One-to-one association of adapter descriptors with ring elements.
 */
static int c2_rx_ring_alloc(struct c2_ring *rx_ring, void *vaddr,
			    dma_addr_t base, void __iomem * mmio_rxp_ring)
{
	struct c2_rx_desc *rx_desc;
	struct c2_rxp_desc __iomem *rxp_desc;
	struct c2_element *elem;
	int i;

	rx_ring->start = kmalloc(sizeof(*elem) * rx_ring->count, GFP_KERNEL);
	if (!rx_ring->start)
		return -ENOMEM;

	elem = rx_ring->start;
	rx_desc = vaddr;
	rxp_desc = mmio_rxp_ring;
	for (i = 0; i < rx_ring->count; i++, elem++, rx_desc++, rxp_desc++) {
		rx_desc->len = 0;
		rx_desc->status = 0;

		/* Set RXP_HRXD_UNINIT */
		__raw_writew(cpu_to_be16(RXP_HRXD_OK),
		       (void __iomem *) rxp_desc + C2_RXP_STATUS);
		__raw_writew(0, (void __iomem *) rxp_desc + C2_RXP_COUNT);
		__raw_writew(0, (void __iomem *) rxp_desc + C2_RXP_LEN);
		__raw_writeq(cpu_to_be64(0x99aabbccddeeffULL),
			     (void __iomem *) rxp_desc + C2_RXP_ADDR);
		__raw_writew(cpu_to_be16(RXP_HRXD_UNINIT),
			     (void __iomem *) rxp_desc + C2_RXP_FLAGS);

		elem->skb = NULL;
		elem->ht_desc = rx_desc;
		elem->hw_desc = rxp_desc;

		if (i == rx_ring->count - 1) {
			elem->next = rx_ring->start;
			rx_desc->next_offset = base;
		} else {
			elem->next = elem + 1;
			rx_desc->next_offset =
			    base + (i + 1) * sizeof(*rx_desc);
		}
	}

	rx_ring->to_use = rx_ring->to_clean = rx_ring->start;

	return 0;
}

/* Setup buffer for receiving */
static inline int c2_rx_alloc(struct c2_port *c2_port, struct c2_element *elem)
{
	struct c2_dev *c2dev = c2_port->c2dev;
	struct c2_rx_desc *rx_desc = elem->ht_desc;
	struct sk_buff *skb;
	dma_addr_t mapaddr;
	u32 maplen;
	struct c2_rxp_hdr *rxp_hdr;

	skb = dev_alloc_skb(c2_port->rx_buf_size);
	if (unlikely(!skb)) {
		pr_debug("%s: out of memory for receive\n",
			c2_port->netdev->name);
		return -ENOMEM;
	}

	/* Zero out the rxp hdr in the sk_buff */
	memset(skb->data, 0, sizeof(*rxp_hdr));

	skb->dev = c2_port->netdev;

	maplen = c2_port->rx_buf_size;
	mapaddr =
	    pci_map_single(c2dev->pcidev, skb->data, maplen,
			   PCI_DMA_FROMDEVICE);

	/* Set the sk_buff RXP_header to RXP_HRXD_READY */
	rxp_hdr = (struct c2_rxp_hdr *) skb->data;
	rxp_hdr->flags = RXP_HRXD_READY;

	__raw_writew(0, elem->hw_desc + C2_RXP_STATUS);
	__raw_writew(cpu_to_be16((u16) maplen - sizeof(*rxp_hdr)),
		     elem->hw_desc + C2_RXP_LEN);
	__raw_writeq(cpu_to_be64(mapaddr), elem->hw_desc + C2_RXP_ADDR);
	__raw_writew(cpu_to_be16(RXP_HRXD_READY), elem->hw_desc + C2_RXP_FLAGS);

	elem->skb = skb;
	elem->mapaddr = mapaddr;
	elem->maplen = maplen;
	rx_desc->len = maplen;

	return 0;
}

/*
 * Allocate buffers for the Rx ring
 * For receive:  rx_ring.to_clean is next received frame
 */
static int c2_rx_fill(struct c2_port *c2_port)
{
	struct c2_ring *rx_ring = &c2_port->rx_ring;
	struct c2_element *elem;
	int ret = 0;

	elem = rx_ring->start;
	do {
		if (c2_rx_alloc(c2_port, elem)) {
			ret = 1;
			break;
		}
	} while ((elem = elem->next) != rx_ring->start);

	rx_ring->to_clean = rx_ring->start;
	return ret;
}

/* Free all buffers in RX ring, assumes receiver stopped */
static void c2_rx_clean(struct c2_port *c2_port)
{
	struct c2_dev *c2dev = c2_port->c2dev;
	struct c2_ring *rx_ring = &c2_port->rx_ring;
	struct c2_element *elem;
	struct c2_rx_desc *rx_desc;

	elem = rx_ring->start;
	do {
		rx_desc = elem->ht_desc;
		rx_desc->len = 0;

		__raw_writew(0, elem->hw_desc + C2_RXP_STATUS);
		__raw_writew(0, elem->hw_desc + C2_RXP_COUNT);
		__raw_writew(0, elem->hw_desc + C2_RXP_LEN);
		__raw_writeq(cpu_to_be64(0x99aabbccddeeffULL),
			     elem->hw_desc + C2_RXP_ADDR);
		__raw_writew(cpu_to_be16(RXP_HRXD_UNINIT),
			     elem->hw_desc + C2_RXP_FLAGS);

		if (elem->skb) {
			pci_unmap_single(c2dev->pcidev, elem->mapaddr,
					 elem->maplen, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(elem->skb);
			elem->skb = NULL;
		}
	} while ((elem = elem->next) != rx_ring->start);
}

static inline int c2_tx_free(struct c2_dev *c2dev, struct c2_element *elem)
{
	struct c2_tx_desc *tx_desc = elem->ht_desc;

	tx_desc->len = 0;

	pci_unmap_single(c2dev->pcidev, elem->mapaddr, elem->maplen,
			 PCI_DMA_TODEVICE);

	if (elem->skb) {
		dev_kfree_skb_any(elem->skb);
		elem->skb = NULL;
	}

	return 0;
}

/* Free all buffers in TX ring, assumes transmitter stopped */
static void c2_tx_clean(struct c2_port *c2_port)
{
	struct c2_ring *tx_ring = &c2_port->tx_ring;
	struct c2_element *elem;
	struct c2_txp_desc txp_htxd;
	int retry;
	unsigned long flags;

	spin_lock_irqsave(&c2_port->tx_lock, flags);

	elem = tx_ring->start;

	do {
		retry = 0;
		do {
			txp_htxd.flags =
			    readw(elem->hw_desc + C2_TXP_FLAGS);

			if (txp_htxd.flags == TXP_HTXD_READY) {
				retry = 1;
				__raw_writew(0,
					     elem->hw_desc + C2_TXP_LEN);
				__raw_writeq(0,
					     elem->hw_desc + C2_TXP_ADDR);
				__raw_writew(cpu_to_be16(TXP_HTXD_DONE),
					     elem->hw_desc + C2_TXP_FLAGS);
				c2_port->netstats.tx_dropped++;
				break;
			} else {
				__raw_writew(0,
					     elem->hw_desc + C2_TXP_LEN);
				__raw_writeq(cpu_to_be64(0x1122334455667788ULL),
					     elem->hw_desc + C2_TXP_ADDR);
				__raw_writew(cpu_to_be16(TXP_HTXD_UNINIT),
					     elem->hw_desc + C2_TXP_FLAGS);
			}

			c2_tx_free(c2_port->c2dev, elem);

		} while ((elem = elem->next) != tx_ring->start);
	} while (retry);

	c2_port->tx_avail = c2_port->tx_ring.count - 1;
	c2_port->c2dev->cur_tx = tx_ring->to_use - tx_ring->start;

	if (c2_port->tx_avail > MAX_SKB_FRAGS + 1)
		netif_wake_queue(c2_port->netdev);

	spin_unlock_irqrestore(&c2_port->tx_lock, flags);
}

/*
 * Process transmit descriptors marked 'DONE' by the firmware,
 * freeing up their unneeded sk_buffs.
 */
static void c2_tx_interrupt(struct net_device *netdev)
{
	struct c2_port *c2_port = netdev_priv(netdev);
	struct c2_dev *c2dev = c2_port->c2dev;
	struct c2_ring *tx_ring = &c2_port->tx_ring;
	struct c2_element *elem;
	struct c2_txp_desc txp_htxd;

	spin_lock(&c2_port->tx_lock);

	for (elem = tx_ring->to_clean; elem != tx_ring->to_use;
	     elem = elem->next) {
		txp_htxd.flags =
		    be16_to_cpu(readw(elem->hw_desc + C2_TXP_FLAGS));

		if (txp_htxd.flags != TXP_HTXD_DONE)
			break;

		if (netif_msg_tx_done(c2_port)) {
			/* PCI reads are expensive in fast path */
			txp_htxd.len =
			    be16_to_cpu(readw(elem->hw_desc + C2_TXP_LEN));
			pr_debug("%s: tx done slot %3Zu status 0x%x len "
				"%5u bytes\n",
				netdev->name, elem - tx_ring->start,
				txp_htxd.flags, txp_htxd.len);
		}

		c2_tx_free(c2dev, elem);
		++(c2_port->tx_avail);
	}

	tx_ring->to_clean = elem;

	if (netif_queue_stopped(netdev)
	    && c2_port->tx_avail > MAX_SKB_FRAGS + 1)
		netif_wake_queue(netdev);

	spin_unlock(&c2_port->tx_lock);
}

static void c2_rx_error(struct c2_port *c2_port, struct c2_element *elem)
{
	struct c2_rx_desc *rx_desc = elem->ht_desc;
	struct c2_rxp_hdr *rxp_hdr = (struct c2_rxp_hdr *) elem->skb->data;

	if (rxp_hdr->status != RXP_HRXD_OK ||
	    rxp_hdr->len > (rx_desc->len - sizeof(*rxp_hdr))) {
		pr_debug("BAD RXP_HRXD\n");
		pr_debug("  rx_desc : %p\n", rx_desc);
		pr_debug("    index : %Zu\n",
			elem - c2_port->rx_ring.start);
		pr_debug("    len   : %u\n", rx_desc->len);
		pr_debug("  rxp_hdr : %p [PA %p]\n", rxp_hdr,
			(void *) __pa((unsigned long) rxp_hdr));
		pr_debug("    flags : 0x%x\n", rxp_hdr->flags);
		pr_debug("    status: 0x%x\n", rxp_hdr->status);
		pr_debug("    len   : %u\n", rxp_hdr->len);
		pr_debug("    rsvd  : 0x%x\n", rxp_hdr->rsvd);
	}

	/* Setup the skb for reuse since we're dropping this pkt */
	elem->skb->tail = elem->skb->data = elem->skb->head;

	/* Zero out the rxp hdr in the sk_buff */
	memset(elem->skb->data, 0, sizeof(*rxp_hdr));

	/* Write the descriptor to the adapter's rx ring */
	__raw_writew(0, elem->hw_desc + C2_RXP_STATUS);
	__raw_writew(0, elem->hw_desc + C2_RXP_COUNT);
	__raw_writew(cpu_to_be16((u16) elem->maplen - sizeof(*rxp_hdr)),
		     elem->hw_desc + C2_RXP_LEN);
	__raw_writeq(cpu_to_be64(elem->mapaddr), elem->hw_desc + C2_RXP_ADDR);
	__raw_writew(cpu_to_be16(RXP_HRXD_READY), elem->hw_desc + C2_RXP_FLAGS);

	pr_debug("packet dropped\n");
	c2_port->netstats.rx_dropped++;
}

static void c2_rx_interrupt(struct net_device *netdev)
{
	struct c2_port *c2_port = netdev_priv(netdev);
	struct c2_dev *c2dev = c2_port->c2dev;
	struct c2_ring *rx_ring = &c2_port->rx_ring;
	struct c2_element *elem;
	struct c2_rx_desc *rx_desc;
	struct c2_rxp_hdr *rxp_hdr;
	struct sk_buff *skb;
	dma_addr_t mapaddr;
	u32 maplen, buflen;
	unsigned long flags;

	spin_lock_irqsave(&c2dev->lock, flags);

	/* Begin where we left off */
	rx_ring->to_clean = rx_ring->start + c2dev->cur_rx;

	for (elem = rx_ring->to_clean; elem->next != rx_ring->to_clean;
	     elem = elem->next) {
		rx_desc = elem->ht_desc;
		mapaddr = elem->mapaddr;
		maplen = elem->maplen;
		skb = elem->skb;
		rxp_hdr = (struct c2_rxp_hdr *) skb->data;

		if (rxp_hdr->flags != RXP_HRXD_DONE)
			break;
		buflen = rxp_hdr->len;

		/* Sanity check the RXP header */
		if (rxp_hdr->status != RXP_HRXD_OK ||
		    buflen > (rx_desc->len - sizeof(*rxp_hdr))) {
			c2_rx_error(c2_port, elem);
			continue;
		}

		/*
		 * Allocate and map a new skb for replenishing the host
		 * RX desc
		 */
		if (c2_rx_alloc(c2_port, elem)) {
			c2_rx_error(c2_port, elem);
			continue;
		}

		/* Unmap the old skb */
		pci_unmap_single(c2dev->pcidev, mapaddr, maplen,
				 PCI_DMA_FROMDEVICE);

		prefetch(skb->data);

		/*
		 * Skip past the leading 8 bytes comprising of the
		 * "struct c2_rxp_hdr", prepended by the adapter
		 * to the usual Ethernet header ("struct ethhdr"),
		 * to the start of the raw Ethernet packet.
		 *
		 * Fix up the various fields in the sk_buff before
		 * passing it up to netif_rx(). The transfer size
		 * (in bytes) specified by the adapter len field of
		 * the "struct rxp_hdr_t" does NOT include the
		 * "sizeof(struct c2_rxp_hdr)".
		 */
		skb->data += sizeof(*rxp_hdr);
		skb->tail = skb->data + buflen;
		skb->len = buflen;
		skb->dev = netdev;
		skb->protocol = eth_type_trans(skb, netdev);

		netif_rx(skb);

		netdev->last_rx = jiffies;
		c2_port->netstats.rx_packets++;
		c2_port->netstats.rx_bytes += buflen;
	}

	/* Save where we left off */
	rx_ring->to_clean = elem;
	c2dev->cur_rx = elem - rx_ring->start;
	C2_SET_CUR_RX(c2dev, c2dev->cur_rx);

	spin_unlock_irqrestore(&c2dev->lock, flags);
}

/*
 * Handle netisr0 TX & RX interrupts.
 */
static irqreturn_t c2_interrupt(int irq, void *dev_id)
{
	unsigned int netisr0, dmaisr;
	int handled = 0;
	struct c2_dev *c2dev = (struct c2_dev *) dev_id;

	/* Process CCILNET interrupts */
	netisr0 = readl(c2dev->regs + C2_NISR0);
	if (netisr0) {

		/*
		 * There is an issue with the firmware that always
		 * provides the status of RX for both TX & RX
		 * interrupts.  So process both queues here.
		 */
		c2_rx_interrupt(c2dev->netdev);
		c2_tx_interrupt(c2dev->netdev);

		/* Clear the interrupt */
		writel(netisr0, c2dev->regs + C2_NISR0);
		handled++;
	}

	/* Process RNIC interrupts */
	dmaisr = readl(c2dev->regs + C2_DISR);
	if (dmaisr) {
		writel(dmaisr, c2dev->regs + C2_DISR);
		c2_rnic_interrupt(c2dev);
		handled++;
	}

	if (handled) {
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static int c2_up(struct net_device *netdev)
{
	struct c2_port *c2_port = netdev_priv(netdev);
	struct c2_dev *c2dev = c2_port->c2dev;
	struct c2_element *elem;
	struct c2_rxp_hdr *rxp_hdr;
	struct in_device *in_dev;
	size_t rx_size, tx_size;
	int ret, i;
	unsigned int netimr0;

	if (netif_msg_ifup(c2_port))
		pr_debug("%s: enabling interface\n", netdev->name);

	/* Set the Rx buffer size based on MTU */
	c2_set_rxbufsize(c2_port);

	/* Allocate DMA'able memory for Tx/Rx host descriptor rings */
	rx_size = c2_port->rx_ring.count * sizeof(struct c2_rx_desc);
	tx_size = c2_port->tx_ring.count * sizeof(struct c2_tx_desc);

	c2_port->mem_size = tx_size + rx_size;
	c2_port->mem = pci_alloc_consistent(c2dev->pcidev, c2_port->mem_size,
					    &c2_port->dma);
	if (c2_port->mem == NULL) {
		pr_debug("Unable to allocate memory for "
			"host descriptor rings\n");
		return -ENOMEM;
	}

	memset(c2_port->mem, 0, c2_port->mem_size);

	/* Create the Rx host descriptor ring */
	if ((ret =
	     c2_rx_ring_alloc(&c2_port->rx_ring, c2_port->mem, c2_port->dma,
			      c2dev->mmio_rxp_ring))) {
		pr_debug("Unable to create RX ring\n");
		goto bail0;
	}

	/* Allocate Rx buffers for the host descriptor ring */
	if (c2_rx_fill(c2_port)) {
		pr_debug("Unable to fill RX ring\n");
		goto bail1;
	}

	/* Create the Tx host descriptor ring */
	if ((ret = c2_tx_ring_alloc(&c2_port->tx_ring, c2_port->mem + rx_size,
				    c2_port->dma + rx_size,
				    c2dev->mmio_txp_ring))) {
		pr_debug("Unable to create TX ring\n");
		goto bail1;
	}

	/* Set the TX pointer to where we left off */
	c2_port->tx_avail = c2_port->tx_ring.count - 1;
	c2_port->tx_ring.to_use = c2_port->tx_ring.to_clean =
	    c2_port->tx_ring.start + c2dev->cur_tx;

	/* missing: Initialize MAC */

	BUG_ON(c2_port->tx_ring.to_use != c2_port->tx_ring.to_clean);

	/* Reset the adapter, ensures the driver is in sync with the RXP */
	c2_reset(c2_port);

	/* Reset the READY bit in the sk_buff RXP headers & adapter HRXDQ */
	for (i = 0, elem = c2_port->rx_ring.start; i < c2_port->rx_ring.count;
	     i++, elem++) {
		rxp_hdr = (struct c2_rxp_hdr *) elem->skb->data;
		rxp_hdr->flags = 0;
		__raw_writew(cpu_to_be16(RXP_HRXD_READY),
			     elem->hw_desc + C2_RXP_FLAGS);
	}

	/* Enable network packets */
	netif_start_queue(netdev);

	/* Enable IRQ */
	writel(0, c2dev->regs + C2_IDIS);
	netimr0 = readl(c2dev->regs + C2_NIMR0);
	netimr0 &= ~(C2_PCI_HTX_INT | C2_PCI_HRX_INT);
	writel(netimr0, c2dev->regs + C2_NIMR0);

	/* Tell the stack to ignore arp requests for ipaddrs bound to
	 * other interfaces.  This is needed to prevent the host stack
	 * from responding to arp requests to the ipaddr bound on the
	 * rdma interface.
	 */
	in_dev = in_dev_get(netdev);
	in_dev->cnf.arp_ignore = 1;
	in_dev_put(in_dev);

	return 0;

      bail1:
	c2_rx_clean(c2_port);
	kfree(c2_port->rx_ring.start);

      bail0:
	pci_free_consistent(c2dev->pcidev, c2_port->mem_size, c2_port->mem,
			    c2_port->dma);

	return ret;
}

static int c2_down(struct net_device *netdev)
{
	struct c2_port *c2_port = netdev_priv(netdev);
	struct c2_dev *c2dev = c2_port->c2dev;

	if (netif_msg_ifdown(c2_port))
		pr_debug("%s: disabling interface\n",
			netdev->name);

	/* Wait for all the queued packets to get sent */
	c2_tx_interrupt(netdev);

	/* Disable network packets */
	netif_stop_queue(netdev);

	/* Disable IRQs by clearing the interrupt mask */
	writel(1, c2dev->regs + C2_IDIS);
	writel(0, c2dev->regs + C2_NIMR0);

	/* missing: Stop transmitter */

	/* missing: Stop receiver */

	/* Reset the adapter, ensures the driver is in sync with the RXP */
	c2_reset(c2_port);

	/* missing: Turn off LEDs here */

	/* Free all buffers in the host descriptor rings */
	c2_tx_clean(c2_port);
	c2_rx_clean(c2_port);

	/* Free the host descriptor rings */
	kfree(c2_port->rx_ring.start);
	kfree(c2_port->tx_ring.start);
	pci_free_consistent(c2dev->pcidev, c2_port->mem_size, c2_port->mem,
			    c2_port->dma);

	return 0;
}

static void c2_reset(struct c2_port *c2_port)
{
	struct c2_dev *c2dev = c2_port->c2dev;
	unsigned int cur_rx = c2dev->cur_rx;

	/* Tell the hardware to quiesce */
	C2_SET_CUR_RX(c2dev, cur_rx | C2_PCI_HRX_QUI);

	/*
	 * The hardware will reset the C2_PCI_HRX_QUI bit once
	 * the RXP is quiesced.  Wait 2 seconds for this.
	 */
	ssleep(2);

	cur_rx = C2_GET_CUR_RX(c2dev);

	if (cur_rx & C2_PCI_HRX_QUI)
		pr_debug("c2_reset: failed to quiesce the hardware!\n");

	cur_rx &= ~C2_PCI_HRX_QUI;

	c2dev->cur_rx = cur_rx;

	pr_debug("Current RX: %u\n", c2dev->cur_rx);
}

static int c2_xmit_frame(struct sk_buff *skb, struct net_device *netdev)
{
	struct c2_port *c2_port = netdev_priv(netdev);
	struct c2_dev *c2dev = c2_port->c2dev;
	struct c2_ring *tx_ring = &c2_port->tx_ring;
	struct c2_element *elem;
	dma_addr_t mapaddr;
	u32 maplen;
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&c2_port->tx_lock, flags);

	if (unlikely(c2_port->tx_avail < (skb_shinfo(skb)->nr_frags + 1))) {
		netif_stop_queue(netdev);
		spin_unlock_irqrestore(&c2_port->tx_lock, flags);

		pr_debug("%s: Tx ring full when queue awake!\n",
			netdev->name);
		return NETDEV_TX_BUSY;
	}

	maplen = skb_headlen(skb);
	mapaddr =
	    pci_map_single(c2dev->pcidev, skb->data, maplen, PCI_DMA_TODEVICE);

	elem = tx_ring->to_use;
	elem->skb = skb;
	elem->mapaddr = mapaddr;
	elem->maplen = maplen;

	/* Tell HW to xmit */
	__raw_writeq(cpu_to_be64(mapaddr), elem->hw_desc + C2_TXP_ADDR);
	__raw_writew(cpu_to_be16(maplen), elem->hw_desc + C2_TXP_LEN);
	__raw_writew(cpu_to_be16(TXP_HTXD_READY), elem->hw_desc + C2_TXP_FLAGS);

	c2_port->netstats.tx_packets++;
	c2_port->netstats.tx_bytes += maplen;

	/* Loop thru additional data fragments and queue them */
	if (skb_shinfo(skb)->nr_frags) {
		for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
			maplen = frag->size;
			mapaddr =
			    pci_map_page(c2dev->pcidev, frag->page,
					 frag->page_offset, maplen,
					 PCI_DMA_TODEVICE);

			elem = elem->next;
			elem->skb = NULL;
			elem->mapaddr = mapaddr;
			elem->maplen = maplen;

			/* Tell HW to xmit */
			__raw_writeq(cpu_to_be64(mapaddr),
				     elem->hw_desc + C2_TXP_ADDR);
			__raw_writew(cpu_to_be16(maplen),
				     elem->hw_desc + C2_TXP_LEN);
			__raw_writew(cpu_to_be16(TXP_HTXD_READY),
				     elem->hw_desc + C2_TXP_FLAGS);

			c2_port->netstats.tx_packets++;
			c2_port->netstats.tx_bytes += maplen;
		}
	}

	tx_ring->to_use = elem->next;
	c2_port->tx_avail -= (skb_shinfo(skb)->nr_frags + 1);

	if (c2_port->tx_avail <= MAX_SKB_FRAGS + 1) {
		netif_stop_queue(netdev);
		if (netif_msg_tx_queued(c2_port))
			pr_debug("%s: transmit queue full\n",
				netdev->name);
	}

	spin_unlock_irqrestore(&c2_port->tx_lock, flags);

	netdev->trans_start = jiffies;

	return NETDEV_TX_OK;
}

static struct net_device_stats *c2_get_stats(struct net_device *netdev)
{
	struct c2_port *c2_port = netdev_priv(netdev);

	return &c2_port->netstats;
}

static void c2_tx_timeout(struct net_device *netdev)
{
	struct c2_port *c2_port = netdev_priv(netdev);

	if (netif_msg_timer(c2_port))
		pr_debug("%s: tx timeout\n", netdev->name);

	c2_tx_clean(c2_port);
}

static int c2_change_mtu(struct net_device *netdev, int new_mtu)
{
	int ret = 0;

	if (new_mtu < ETH_ZLEN || new_mtu > ETH_JUMBO_MTU)
		return -EINVAL;

	netdev->mtu = new_mtu;

	if (netif_running(netdev)) {
		c2_down(netdev);

		c2_up(netdev);
	}

	return ret;
}

/* Initialize network device */
static struct net_device *c2_devinit(struct c2_dev *c2dev,
				     void __iomem * mmio_addr)
{
	struct c2_port *c2_port = NULL;
	struct net_device *netdev = alloc_etherdev(sizeof(*c2_port));

	if (!netdev) {
		pr_debug("c2_port etherdev alloc failed");
		return NULL;
	}

	SET_MODULE_OWNER(netdev);
	SET_NETDEV_DEV(netdev, &c2dev->pcidev->dev);

	netdev->open = c2_up;
	netdev->stop = c2_down;
	netdev->hard_start_xmit = c2_xmit_frame;
	netdev->get_stats = c2_get_stats;
	netdev->tx_timeout = c2_tx_timeout;
	netdev->change_mtu = c2_change_mtu;
	netdev->watchdog_timeo = C2_TX_TIMEOUT;
	netdev->irq = c2dev->pcidev->irq;

	c2_port = netdev_priv(netdev);
	c2_port->netdev = netdev;
	c2_port->c2dev = c2dev;
	c2_port->msg_enable = netif_msg_init(debug, default_msg);
	c2_port->tx_ring.count = C2_NUM_TX_DESC;
	c2_port->rx_ring.count = C2_NUM_RX_DESC;

	spin_lock_init(&c2_port->tx_lock);

	/* Copy our 48-bit ethernet hardware address */
	memcpy_fromio(netdev->dev_addr, mmio_addr + C2_REGS_ENADDR, 6);

	/* Validate the MAC address */
	if (!is_valid_ether_addr(netdev->dev_addr)) {
		pr_debug("Invalid MAC Address\n");
		c2_print_macaddr(netdev);
		free_netdev(netdev);
		return NULL;
	}

	c2dev->netdev = netdev;

	return netdev;
}

static int __devinit c2_probe(struct pci_dev *pcidev,
			      const struct pci_device_id *ent)
{
	int ret = 0, i;
	unsigned long reg0_start, reg0_flags, reg0_len;
	unsigned long reg2_start, reg2_flags, reg2_len;
	unsigned long reg4_start, reg4_flags, reg4_len;
	unsigned kva_map_size;
	struct net_device *netdev = NULL;
	struct c2_dev *c2dev = NULL;
	void __iomem *mmio_regs = NULL;

	printk(KERN_INFO PFX "AMSO1100 Gigabit Ethernet driver v%s loaded\n",
		DRV_VERSION);

	/* Enable PCI device */
	ret = pci_enable_device(pcidev);
	if (ret) {
		printk(KERN_ERR PFX "%s: Unable to enable PCI device\n",
			pci_name(pcidev));
		goto bail0;
	}

	reg0_start = pci_resource_start(pcidev, BAR_0);
	reg0_len = pci_resource_len(pcidev, BAR_0);
	reg0_flags = pci_resource_flags(pcidev, BAR_0);

	reg2_start = pci_resource_start(pcidev, BAR_2);
	reg2_len = pci_resource_len(pcidev, BAR_2);
	reg2_flags = pci_resource_flags(pcidev, BAR_2);

	reg4_start = pci_resource_start(pcidev, BAR_4);
	reg4_len = pci_resource_len(pcidev, BAR_4);
	reg4_flags = pci_resource_flags(pcidev, BAR_4);

	pr_debug("BAR0 size = 0x%lX bytes\n", reg0_len);
	pr_debug("BAR2 size = 0x%lX bytes\n", reg2_len);
	pr_debug("BAR4 size = 0x%lX bytes\n", reg4_len);

	/* Make sure PCI base addr are MMIO */
	if (!(reg0_flags & IORESOURCE_MEM) ||
	    !(reg2_flags & IORESOURCE_MEM) || !(reg4_flags & IORESOURCE_MEM)) {
		printk(KERN_ERR PFX "PCI regions not an MMIO resource\n");
		ret = -ENODEV;
		goto bail1;
	}

	/* Check for weird/broken PCI region reporting */
	if ((reg0_len < C2_REG0_SIZE) ||
	    (reg2_len < C2_REG2_SIZE) || (reg4_len < C2_REG4_SIZE)) {
		printk(KERN_ERR PFX "Invalid PCI region sizes\n");
		ret = -ENODEV;
		goto bail1;
	}

	/* Reserve PCI I/O and memory resources */
	ret = pci_request_regions(pcidev, DRV_NAME);
	if (ret) {
		printk(KERN_ERR PFX "%s: Unable to request regions\n",
			pci_name(pcidev));
		goto bail1;
	}

	if ((sizeof(dma_addr_t) > 4)) {
		ret = pci_set_dma_mask(pcidev, DMA_64BIT_MASK);
		if (ret < 0) {
			printk(KERN_ERR PFX "64b DMA configuration failed\n");
			goto bail2;
		}
	} else {
		ret = pci_set_dma_mask(pcidev, DMA_32BIT_MASK);
		if (ret < 0) {
			printk(KERN_ERR PFX "32b DMA configuration failed\n");
			goto bail2;
		}
	}

	/* Enables bus-mastering on the device */
	pci_set_master(pcidev);

	/* Remap the adapter PCI registers in BAR4 */
	mmio_regs = ioremap_nocache(reg4_start + C2_PCI_REGS_OFFSET,
				    sizeof(struct c2_adapter_pci_regs));
	if (mmio_regs == 0UL) {
		printk(KERN_ERR PFX
			"Unable to remap adapter PCI registers in BAR4\n");
		ret = -EIO;
		goto bail2;
	}

	/* Validate PCI regs magic */
	for (i = 0; i < sizeof(c2_magic); i++) {
		if (c2_magic[i] != readb(mmio_regs + C2_REGS_MAGIC + i)) {
			printk(KERN_ERR PFX "Downlevel Firmware boot loader "
				"[%d/%Zd: got 0x%x, exp 0x%x]. Use the cc_flash "
			       "utility to update your boot loader\n",
				i + 1, sizeof(c2_magic),
				readb(mmio_regs + C2_REGS_MAGIC + i),
				c2_magic[i]);
			printk(KERN_ERR PFX "Adapter not claimed\n");
			iounmap(mmio_regs);
			ret = -EIO;
			goto bail2;
		}
	}

	/* Validate the adapter version */
	if (be32_to_cpu(readl(mmio_regs + C2_REGS_VERS)) != C2_VERSION) {
		printk(KERN_ERR PFX "Version mismatch "
			"[fw=%u, c2=%u], Adapter not claimed\n",
			be32_to_cpu(readl(mmio_regs + C2_REGS_VERS)),
			C2_VERSION);
		ret = -EINVAL;
		iounmap(mmio_regs);
		goto bail2;
	}

	/* Validate the adapter IVN */
	if (be32_to_cpu(readl(mmio_regs + C2_REGS_IVN)) != C2_IVN) {
		printk(KERN_ERR PFX "Downlevel FIrmware level. You should be using "
		       "the OpenIB device support kit. "
		       "[fw=0x%x, c2=0x%x], Adapter not claimed\n",
			be32_to_cpu(readl(mmio_regs + C2_REGS_IVN)),
			C2_IVN);
		ret = -EINVAL;
		iounmap(mmio_regs);
		goto bail2;
	}

	/* Allocate hardware structure */
	c2dev = (struct c2_dev *) ib_alloc_device(sizeof(*c2dev));
	if (!c2dev) {
		printk(KERN_ERR PFX "%s: Unable to alloc hardware struct\n",
			pci_name(pcidev));
		ret = -ENOMEM;
		iounmap(mmio_regs);
		goto bail2;
	}

	memset(c2dev, 0, sizeof(*c2dev));
	spin_lock_init(&c2dev->lock);
	c2dev->pcidev = pcidev;
	c2dev->cur_tx = 0;

	/* Get the last RX index */
	c2dev->cur_rx =
	    (be32_to_cpu(readl(mmio_regs + C2_REGS_HRX_CUR)) -
	     0xffffc000) / sizeof(struct c2_rxp_desc);

	/* Request an interrupt line for the driver */
	ret = request_irq(pcidev->irq, c2_interrupt, IRQF_SHARED, DRV_NAME, c2dev);
	if (ret) {
		printk(KERN_ERR PFX "%s: requested IRQ %u is busy\n",
			pci_name(pcidev), pcidev->irq);
		iounmap(mmio_regs);
		goto bail3;
	}

	/* Set driver specific data */
	pci_set_drvdata(pcidev, c2dev);

	/* Initialize network device */
	if ((netdev = c2_devinit(c2dev, mmio_regs)) == NULL) {
		iounmap(mmio_regs);
		goto bail4;
	}

	/* Save off the actual size prior to unmapping mmio_regs */
	kva_map_size = be32_to_cpu(readl(mmio_regs + C2_REGS_PCI_WINSIZE));

	/* Unmap the adapter PCI registers in BAR4 */
	iounmap(mmio_regs);

	/* Register network device */
	ret = register_netdev(netdev);
	if (ret) {
		printk(KERN_ERR PFX "Unable to register netdev, ret = %d\n",
			ret);
		goto bail5;
	}

	/* Disable network packets */
	netif_stop_queue(netdev);

	/* Remap the adapter HRXDQ PA space to kernel VA space */
	c2dev->mmio_rxp_ring = ioremap_nocache(reg4_start + C2_RXP_HRXDQ_OFFSET,
					       C2_RXP_HRXDQ_SIZE);
	if (c2dev->mmio_rxp_ring == 0UL) {
		printk(KERN_ERR PFX "Unable to remap MMIO HRXDQ region\n");
		ret = -EIO;
		goto bail6;
	}

	/* Remap the adapter HTXDQ PA space to kernel VA space */
	c2dev->mmio_txp_ring = ioremap_nocache(reg4_start + C2_TXP_HTXDQ_OFFSET,
					       C2_TXP_HTXDQ_SIZE);
	if (c2dev->mmio_txp_ring == 0UL) {
		printk(KERN_ERR PFX "Unable to remap MMIO HTXDQ region\n");
		ret = -EIO;
		goto bail7;
	}

	/* Save off the current RX index in the last 4 bytes of the TXP Ring */
	C2_SET_CUR_RX(c2dev, c2dev->cur_rx);

	/* Remap the PCI registers in adapter BAR0 to kernel VA space */
	c2dev->regs = ioremap_nocache(reg0_start, reg0_len);
	if (c2dev->regs == 0UL) {
		printk(KERN_ERR PFX "Unable to remap BAR0\n");
		ret = -EIO;
		goto bail8;
	}

	/* Remap the PCI registers in adapter BAR4 to kernel VA space */
	c2dev->pa = reg4_start + C2_PCI_REGS_OFFSET;
	c2dev->kva = ioremap_nocache(reg4_start + C2_PCI_REGS_OFFSET,
				     kva_map_size);
	if (c2dev->kva == 0UL) {
		printk(KERN_ERR PFX "Unable to remap BAR4\n");
		ret = -EIO;
		goto bail9;
	}

	/* Print out the MAC address */
	c2_print_macaddr(netdev);

	ret = c2_rnic_init(c2dev);
	if (ret) {
		printk(KERN_ERR PFX "c2_rnic_init failed: %d\n", ret);
		goto bail10;
	}

	if (c2_register_device(c2dev))
		goto bail10;

	return 0;

 bail10:
	iounmap(c2dev->kva);

 bail9:
	iounmap(c2dev->regs);

 bail8:
	iounmap(c2dev->mmio_txp_ring);

 bail7:
	iounmap(c2dev->mmio_rxp_ring);

 bail6:
	unregister_netdev(netdev);

 bail5:
	free_netdev(netdev);

 bail4:
	free_irq(pcidev->irq, c2dev);

 bail3:
	ib_dealloc_device(&c2dev->ibdev);

 bail2:
	pci_release_regions(pcidev);

 bail1:
	pci_disable_device(pcidev);

 bail0:
	return ret;
}

static void __devexit c2_remove(struct pci_dev *pcidev)
{
	struct c2_dev *c2dev = pci_get_drvdata(pcidev);
	struct net_device *netdev = c2dev->netdev;

	/* Unregister with OpenIB */
	c2_unregister_device(c2dev);

	/* Clean up the RNIC resources */
	c2_rnic_term(c2dev);

	/* Remove network device from the kernel */
	unregister_netdev(netdev);

	/* Free network device */
	free_netdev(netdev);

	/* Free the interrupt line */
	free_irq(pcidev->irq, c2dev);

	/* missing: Turn LEDs off here */

	/* Unmap adapter PA space */
	iounmap(c2dev->kva);
	iounmap(c2dev->regs);
	iounmap(c2dev->mmio_txp_ring);
	iounmap(c2dev->mmio_rxp_ring);

	/* Free the hardware structure */
	ib_dealloc_device(&c2dev->ibdev);

	/* Release reserved PCI I/O and memory resources */
	pci_release_regions(pcidev);

	/* Disable PCI device */
	pci_disable_device(pcidev);

	/* Clear driver specific data */
	pci_set_drvdata(pcidev, NULL);
}

static struct pci_driver c2_pci_driver = {
	.name = DRV_NAME,
	.id_table = c2_pci_table,
	.probe = c2_probe,
	.remove = __devexit_p(c2_remove),
};

static int __init c2_init_module(void)
{
	return pci_register_driver(&c2_pci_driver);
}

static void __exit c2_exit_module(void)
{
	pci_unregister_driver(&c2_pci_driver);
}

module_init(c2_init_module);
module_exit(c2_exit_module);
