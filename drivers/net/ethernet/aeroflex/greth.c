// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Aeroflex Gaisler GRETH 10/100/1G Ethernet MAC.
 *
 * 2005-2010 (c) Aeroflex Gaisler AB
 *
 * This driver supports GRETH 10/100 and GRETH 10/100/1G Ethernet MACs
 * available in the GRLIB VHDL IP core library.
 *
 * Full documentation of both cores can be found here:
 * https://www.gaisler.com/products/grlib/grip.pdf
 *
 * The Gigabit version supports scatter/gather DMA, any alignment of
 * buffers and checksum offloading.
 *
 * Contributors: Kristoffer Glembo
 *               Daniel Hellstrom
 *               Marko Isomaki
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/skbuff.h>
#include <linux/io.h>
#include <linux/crc32.h>
#include <linux/mii.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/byteorder.h>

#ifdef CONFIG_SPARC
#include <asm/idprom.h>
#endif

#include "greth.h"

#define GRETH_DEF_MSG_ENABLE	  \
	(NETIF_MSG_DRV		| \
	 NETIF_MSG_PROBE	| \
	 NETIF_MSG_LINK		| \
	 NETIF_MSG_IFDOWN	| \
	 NETIF_MSG_IFUP		| \
	 NETIF_MSG_RX_ERR	| \
	 NETIF_MSG_TX_ERR)

static int greth_debug = -1;	/* -1 == use GRETH_DEF_MSG_ENABLE as value */
module_param(greth_debug, int, 0);
MODULE_PARM_DESC(greth_debug, "GRETH bitmapped debugging message enable value");

/* Accept MAC address of the form macaddr=0x08,0x00,0x20,0x30,0x40,0x50 */
static int macaddr[6];
module_param_array(macaddr, int, NULL, 0);
MODULE_PARM_DESC(macaddr, "GRETH Ethernet MAC address");

static int greth_edcl = 1;
module_param(greth_edcl, int, 0);
MODULE_PARM_DESC(greth_edcl, "GRETH EDCL usage indicator. Set to 1 if EDCL is used.");

static int greth_open(struct net_device *dev);
static netdev_tx_t greth_start_xmit(struct sk_buff *skb,
	   struct net_device *dev);
static netdev_tx_t greth_start_xmit_gbit(struct sk_buff *skb,
	   struct net_device *dev);
static int greth_rx(struct net_device *dev, int limit);
static int greth_rx_gbit(struct net_device *dev, int limit);
static void greth_clean_tx(struct net_device *dev);
static void greth_clean_tx_gbit(struct net_device *dev);
static irqreturn_t greth_interrupt(int irq, void *dev_id);
static int greth_close(struct net_device *dev);
static int greth_set_mac_add(struct net_device *dev, void *p);
static void greth_set_multicast_list(struct net_device *dev);

#define GRETH_REGLOAD(a)	    (be32_to_cpu(__raw_readl(&(a))))
#define GRETH_REGSAVE(a, v)         (__raw_writel(cpu_to_be32(v), &(a)))
#define GRETH_REGORIN(a, v)         (GRETH_REGSAVE(a, (GRETH_REGLOAD(a) | (v))))
#define GRETH_REGANDIN(a, v)        (GRETH_REGSAVE(a, (GRETH_REGLOAD(a) & (v))))

#define NEXT_TX(N)      (((N) + 1) & GRETH_TXBD_NUM_MASK)
#define SKIP_TX(N, C)   (((N) + C) & GRETH_TXBD_NUM_MASK)
#define NEXT_RX(N)      (((N) + 1) & GRETH_RXBD_NUM_MASK)

static void greth_print_rx_packet(void *addr, int len)
{
	print_hex_dump(KERN_DEBUG, "RX: ", DUMP_PREFIX_OFFSET, 16, 1,
			addr, len, true);
}

static void greth_print_tx_packet(struct sk_buff *skb)
{
	int i;
	int length;

	if (skb_shinfo(skb)->nr_frags == 0)
		length = skb->len;
	else
		length = skb_headlen(skb);

	print_hex_dump(KERN_DEBUG, "TX: ", DUMP_PREFIX_OFFSET, 16, 1,
			skb->data, length, true);

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {

		print_hex_dump(KERN_DEBUG, "TX: ", DUMP_PREFIX_OFFSET, 16, 1,
			       skb_frag_address(&skb_shinfo(skb)->frags[i]),
			       skb_frag_size(&skb_shinfo(skb)->frags[i]), true);
	}
}

static inline void greth_enable_tx(struct greth_private *greth)
{
	wmb();
	GRETH_REGORIN(greth->regs->control, GRETH_TXEN);
}

static inline void greth_enable_tx_and_irq(struct greth_private *greth)
{
	wmb(); /* BDs must been written to memory before enabling TX */
	GRETH_REGORIN(greth->regs->control, GRETH_TXEN | GRETH_TXI);
}

static inline void greth_disable_tx(struct greth_private *greth)
{
	GRETH_REGANDIN(greth->regs->control, ~GRETH_TXEN);
}

static inline void greth_enable_rx(struct greth_private *greth)
{
	wmb();
	GRETH_REGORIN(greth->regs->control, GRETH_RXEN);
}

static inline void greth_disable_rx(struct greth_private *greth)
{
	GRETH_REGANDIN(greth->regs->control, ~GRETH_RXEN);
}

static inline void greth_enable_irqs(struct greth_private *greth)
{
	GRETH_REGORIN(greth->regs->control, GRETH_RXI | GRETH_TXI);
}

static inline void greth_disable_irqs(struct greth_private *greth)
{
	GRETH_REGANDIN(greth->regs->control, ~(GRETH_RXI|GRETH_TXI));
}

static inline void greth_write_bd(u32 *bd, u32 val)
{
	__raw_writel(cpu_to_be32(val), bd);
}

static inline u32 greth_read_bd(u32 *bd)
{
	return be32_to_cpu(__raw_readl(bd));
}

static void greth_clean_rings(struct greth_private *greth)
{
	int i;
	struct greth_bd *rx_bdp = greth->rx_bd_base;
	struct greth_bd *tx_bdp = greth->tx_bd_base;

	if (greth->gbit_mac) {

		/* Free and unmap RX buffers */
		for (i = 0; i < GRETH_RXBD_NUM; i++, rx_bdp++) {
			if (greth->rx_skbuff[i] != NULL) {
				dev_kfree_skb(greth->rx_skbuff[i]);
				dma_unmap_single(greth->dev,
						 greth_read_bd(&rx_bdp->addr),
						 MAX_FRAME_SIZE+NET_IP_ALIGN,
						 DMA_FROM_DEVICE);
			}
		}

		/* TX buffers */
		while (greth->tx_free < GRETH_TXBD_NUM) {

			struct sk_buff *skb = greth->tx_skbuff[greth->tx_last];
			int nr_frags = skb_shinfo(skb)->nr_frags;
			tx_bdp = greth->tx_bd_base + greth->tx_last;
			greth->tx_last = NEXT_TX(greth->tx_last);

			dma_unmap_single(greth->dev,
					 greth_read_bd(&tx_bdp->addr),
					 skb_headlen(skb),
					 DMA_TO_DEVICE);

			for (i = 0; i < nr_frags; i++) {
				skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
				tx_bdp = greth->tx_bd_base + greth->tx_last;

				dma_unmap_page(greth->dev,
					       greth_read_bd(&tx_bdp->addr),
					       skb_frag_size(frag),
					       DMA_TO_DEVICE);

				greth->tx_last = NEXT_TX(greth->tx_last);
			}
			greth->tx_free += nr_frags+1;
			dev_kfree_skb(skb);
		}


	} else { /* 10/100 Mbps MAC */

		for (i = 0; i < GRETH_RXBD_NUM; i++, rx_bdp++) {
			kfree(greth->rx_bufs[i]);
			dma_unmap_single(greth->dev,
					 greth_read_bd(&rx_bdp->addr),
					 MAX_FRAME_SIZE,
					 DMA_FROM_DEVICE);
		}
		for (i = 0; i < GRETH_TXBD_NUM; i++, tx_bdp++) {
			kfree(greth->tx_bufs[i]);
			dma_unmap_single(greth->dev,
					 greth_read_bd(&tx_bdp->addr),
					 MAX_FRAME_SIZE,
					 DMA_TO_DEVICE);
		}
	}
}

static int greth_init_rings(struct greth_private *greth)
{
	struct sk_buff *skb;
	struct greth_bd *rx_bd, *tx_bd;
	u32 dma_addr;
	int i;

	rx_bd = greth->rx_bd_base;
	tx_bd = greth->tx_bd_base;

	/* Initialize descriptor rings and buffers */
	if (greth->gbit_mac) {

		for (i = 0; i < GRETH_RXBD_NUM; i++) {
			skb = netdev_alloc_skb(greth->netdev, MAX_FRAME_SIZE+NET_IP_ALIGN);
			if (skb == NULL) {
				if (netif_msg_ifup(greth))
					dev_err(greth->dev, "Error allocating DMA ring.\n");
				goto cleanup;
			}
			skb_reserve(skb, NET_IP_ALIGN);
			dma_addr = dma_map_single(greth->dev,
						  skb->data,
						  MAX_FRAME_SIZE+NET_IP_ALIGN,
						  DMA_FROM_DEVICE);

			if (dma_mapping_error(greth->dev, dma_addr)) {
				if (netif_msg_ifup(greth))
					dev_err(greth->dev, "Could not create initial DMA mapping\n");
				dev_kfree_skb(skb);
				goto cleanup;
			}
			greth->rx_skbuff[i] = skb;
			greth_write_bd(&rx_bd[i].addr, dma_addr);
			greth_write_bd(&rx_bd[i].stat, GRETH_BD_EN | GRETH_BD_IE);
		}

	} else {

		/* 10/100 MAC uses a fixed set of buffers and copy to/from SKBs */
		for (i = 0; i < GRETH_RXBD_NUM; i++) {

			greth->rx_bufs[i] = kmalloc(MAX_FRAME_SIZE, GFP_KERNEL);

			if (greth->rx_bufs[i] == NULL) {
				if (netif_msg_ifup(greth))
					dev_err(greth->dev, "Error allocating DMA ring.\n");
				goto cleanup;
			}

			dma_addr = dma_map_single(greth->dev,
						  greth->rx_bufs[i],
						  MAX_FRAME_SIZE,
						  DMA_FROM_DEVICE);

			if (dma_mapping_error(greth->dev, dma_addr)) {
				if (netif_msg_ifup(greth))
					dev_err(greth->dev, "Could not create initial DMA mapping\n");
				goto cleanup;
			}
			greth_write_bd(&rx_bd[i].addr, dma_addr);
			greth_write_bd(&rx_bd[i].stat, GRETH_BD_EN | GRETH_BD_IE);
		}
		for (i = 0; i < GRETH_TXBD_NUM; i++) {

			greth->tx_bufs[i] = kmalloc(MAX_FRAME_SIZE, GFP_KERNEL);

			if (greth->tx_bufs[i] == NULL) {
				if (netif_msg_ifup(greth))
					dev_err(greth->dev, "Error allocating DMA ring.\n");
				goto cleanup;
			}

			dma_addr = dma_map_single(greth->dev,
						  greth->tx_bufs[i],
						  MAX_FRAME_SIZE,
						  DMA_TO_DEVICE);

			if (dma_mapping_error(greth->dev, dma_addr)) {
				if (netif_msg_ifup(greth))
					dev_err(greth->dev, "Could not create initial DMA mapping\n");
				goto cleanup;
			}
			greth_write_bd(&tx_bd[i].addr, dma_addr);
			greth_write_bd(&tx_bd[i].stat, 0);
		}
	}
	greth_write_bd(&rx_bd[GRETH_RXBD_NUM - 1].stat,
		       greth_read_bd(&rx_bd[GRETH_RXBD_NUM - 1].stat) | GRETH_BD_WR);

	/* Initialize pointers. */
	greth->rx_cur = 0;
	greth->tx_next = 0;
	greth->tx_last = 0;
	greth->tx_free = GRETH_TXBD_NUM;

	/* Initialize descriptor base address */
	GRETH_REGSAVE(greth->regs->tx_desc_p, greth->tx_bd_base_phys);
	GRETH_REGSAVE(greth->regs->rx_desc_p, greth->rx_bd_base_phys);

	return 0;

cleanup:
	greth_clean_rings(greth);
	return -ENOMEM;
}

static int greth_open(struct net_device *dev)
{
	struct greth_private *greth = netdev_priv(dev);
	int err;

	err = greth_init_rings(greth);
	if (err) {
		if (netif_msg_ifup(greth))
			dev_err(&dev->dev, "Could not allocate memory for DMA rings\n");
		return err;
	}

	err = request_irq(greth->irq, greth_interrupt, 0, "eth", (void *) dev);
	if (err) {
		if (netif_msg_ifup(greth))
			dev_err(&dev->dev, "Could not allocate interrupt %d\n", dev->irq);
		greth_clean_rings(greth);
		return err;
	}

	if (netif_msg_ifup(greth))
		dev_dbg(&dev->dev, " starting queue\n");
	netif_start_queue(dev);

	GRETH_REGSAVE(greth->regs->status, 0xFF);

	napi_enable(&greth->napi);

	greth_enable_irqs(greth);
	greth_enable_tx(greth);
	greth_enable_rx(greth);
	return 0;

}

static int greth_close(struct net_device *dev)
{
	struct greth_private *greth = netdev_priv(dev);

	napi_disable(&greth->napi);

	greth_disable_irqs(greth);
	greth_disable_tx(greth);
	greth_disable_rx(greth);

	netif_stop_queue(dev);

	free_irq(greth->irq, (void *) dev);

	greth_clean_rings(greth);

	return 0;
}

static netdev_tx_t
greth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct greth_private *greth = netdev_priv(dev);
	struct greth_bd *bdp;
	int err = NETDEV_TX_OK;
	u32 status, dma_addr, ctrl;
	unsigned long flags;

	/* Clean TX Ring */
	greth_clean_tx(greth->netdev);

	if (unlikely(greth->tx_free <= 0)) {
		spin_lock_irqsave(&greth->devlock, flags);/*save from poll/irq*/
		ctrl = GRETH_REGLOAD(greth->regs->control);
		/* Enable TX IRQ only if not already in poll() routine */
		if (ctrl & GRETH_RXI)
			GRETH_REGSAVE(greth->regs->control, ctrl | GRETH_TXI);
		netif_stop_queue(dev);
		spin_unlock_irqrestore(&greth->devlock, flags);
		return NETDEV_TX_BUSY;
	}

	if (netif_msg_pktdata(greth))
		greth_print_tx_packet(skb);


	if (unlikely(skb->len > MAX_FRAME_SIZE)) {
		dev->stats.tx_errors++;
		goto out;
	}

	bdp = greth->tx_bd_base + greth->tx_next;
	dma_addr = greth_read_bd(&bdp->addr);

	memcpy((unsigned char *) phys_to_virt(dma_addr), skb->data, skb->len);

	dma_sync_single_for_device(greth->dev, dma_addr, skb->len, DMA_TO_DEVICE);

	status = GRETH_BD_EN | GRETH_BD_IE | (skb->len & GRETH_BD_LEN);
	greth->tx_bufs_length[greth->tx_next] = skb->len & GRETH_BD_LEN;

	/* Wrap around descriptor ring */
	if (greth->tx_next == GRETH_TXBD_NUM_MASK) {
		status |= GRETH_BD_WR;
	}

	greth->tx_next = NEXT_TX(greth->tx_next);
	greth->tx_free--;

	/* Write descriptor control word and enable transmission */
	greth_write_bd(&bdp->stat, status);
	spin_lock_irqsave(&greth->devlock, flags); /*save from poll/irq*/
	greth_enable_tx(greth);
	spin_unlock_irqrestore(&greth->devlock, flags);

out:
	dev_kfree_skb(skb);
	return err;
}

static inline u16 greth_num_free_bds(u16 tx_last, u16 tx_next)
{
	if (tx_next < tx_last)
		return (tx_last - tx_next) - 1;
	else
		return GRETH_TXBD_NUM - (tx_next - tx_last) - 1;
}

static netdev_tx_t
greth_start_xmit_gbit(struct sk_buff *skb, struct net_device *dev)
{
	struct greth_private *greth = netdev_priv(dev);
	struct greth_bd *bdp;
	u32 status, dma_addr;
	int curr_tx, nr_frags, i, err = NETDEV_TX_OK;
	unsigned long flags;
	u16 tx_last;

	nr_frags = skb_shinfo(skb)->nr_frags;
	tx_last = greth->tx_last;
	rmb(); /* tx_last is updated by the poll task */

	if (greth_num_free_bds(tx_last, greth->tx_next) < nr_frags + 1) {
		netif_stop_queue(dev);
		err = NETDEV_TX_BUSY;
		goto out;
	}

	if (netif_msg_pktdata(greth))
		greth_print_tx_packet(skb);

	if (unlikely(skb->len > MAX_FRAME_SIZE)) {
		dev->stats.tx_errors++;
		goto out;
	}

	/* Save skb pointer. */
	greth->tx_skbuff[greth->tx_next] = skb;

	/* Linear buf */
	if (nr_frags != 0)
		status = GRETH_TXBD_MORE;
	else
		status = GRETH_BD_IE;

	if (skb->ip_summed == CHECKSUM_PARTIAL)
		status |= GRETH_TXBD_CSALL;
	status |= skb_headlen(skb) & GRETH_BD_LEN;
	if (greth->tx_next == GRETH_TXBD_NUM_MASK)
		status |= GRETH_BD_WR;


	bdp = greth->tx_bd_base + greth->tx_next;
	greth_write_bd(&bdp->stat, status);
	dma_addr = dma_map_single(greth->dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE);

	if (unlikely(dma_mapping_error(greth->dev, dma_addr)))
		goto map_error;

	greth_write_bd(&bdp->addr, dma_addr);

	curr_tx = NEXT_TX(greth->tx_next);

	/* Frags */
	for (i = 0; i < nr_frags; i++) {
		skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
		greth->tx_skbuff[curr_tx] = NULL;
		bdp = greth->tx_bd_base + curr_tx;

		status = GRETH_BD_EN;
		if (skb->ip_summed == CHECKSUM_PARTIAL)
			status |= GRETH_TXBD_CSALL;
		status |= skb_frag_size(frag) & GRETH_BD_LEN;

		/* Wrap around descriptor ring */
		if (curr_tx == GRETH_TXBD_NUM_MASK)
			status |= GRETH_BD_WR;

		/* More fragments left */
		if (i < nr_frags - 1)
			status |= GRETH_TXBD_MORE;
		else
			status |= GRETH_BD_IE; /* enable IRQ on last fragment */

		greth_write_bd(&bdp->stat, status);

		dma_addr = skb_frag_dma_map(greth->dev, frag, 0, skb_frag_size(frag),
					    DMA_TO_DEVICE);

		if (unlikely(dma_mapping_error(greth->dev, dma_addr)))
			goto frag_map_error;

		greth_write_bd(&bdp->addr, dma_addr);

		curr_tx = NEXT_TX(curr_tx);
	}

	wmb();

	/* Enable the descriptor chain by enabling the first descriptor */
	bdp = greth->tx_bd_base + greth->tx_next;
	greth_write_bd(&bdp->stat,
		       greth_read_bd(&bdp->stat) | GRETH_BD_EN);

	spin_lock_irqsave(&greth->devlock, flags); /*save from poll/irq*/
	greth->tx_next = curr_tx;
	greth_enable_tx_and_irq(greth);
	spin_unlock_irqrestore(&greth->devlock, flags);

	return NETDEV_TX_OK;

frag_map_error:
	/* Unmap SKB mappings that succeeded and disable descriptor */
	for (i = 0; greth->tx_next + i != curr_tx; i++) {
		bdp = greth->tx_bd_base + greth->tx_next + i;
		dma_unmap_single(greth->dev,
				 greth_read_bd(&bdp->addr),
				 greth_read_bd(&bdp->stat) & GRETH_BD_LEN,
				 DMA_TO_DEVICE);
		greth_write_bd(&bdp->stat, 0);
	}
map_error:
	if (net_ratelimit())
		dev_warn(greth->dev, "Could not create TX DMA mapping\n");
	dev_kfree_skb(skb);
out:
	return err;
}

static irqreturn_t greth_interrupt(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct greth_private *greth;
	u32 status, ctrl;
	irqreturn_t retval = IRQ_NONE;

	greth = netdev_priv(dev);

	spin_lock(&greth->devlock);

	/* Get the interrupt events that caused us to be here. */
	status = GRETH_REGLOAD(greth->regs->status);

	/* Must see if interrupts are enabled also, INT_TX|INT_RX flags may be
	 * set regardless of whether IRQ is enabled or not. Especially
	 * important when shared IRQ.
	 */
	ctrl = GRETH_REGLOAD(greth->regs->control);

	/* Handle rx and tx interrupts through poll */
	if (((status & (GRETH_INT_RE | GRETH_INT_RX)) && (ctrl & GRETH_RXI)) ||
	    ((status & (GRETH_INT_TE | GRETH_INT_TX)) && (ctrl & GRETH_TXI))) {
		retval = IRQ_HANDLED;

		/* Disable interrupts and schedule poll() */
		greth_disable_irqs(greth);
		napi_schedule(&greth->napi);
	}

	spin_unlock(&greth->devlock);

	return retval;
}

static void greth_clean_tx(struct net_device *dev)
{
	struct greth_private *greth;
	struct greth_bd *bdp;
	u32 stat;

	greth = netdev_priv(dev);

	while (1) {
		bdp = greth->tx_bd_base + greth->tx_last;
		GRETH_REGSAVE(greth->regs->status, GRETH_INT_TE | GRETH_INT_TX);
		mb();
		stat = greth_read_bd(&bdp->stat);

		if (unlikely(stat & GRETH_BD_EN))
			break;

		if (greth->tx_free == GRETH_TXBD_NUM)
			break;

		/* Check status for errors */
		if (unlikely(stat & GRETH_TXBD_STATUS)) {
			dev->stats.tx_errors++;
			if (stat & GRETH_TXBD_ERR_AL)
				dev->stats.tx_aborted_errors++;
			if (stat & GRETH_TXBD_ERR_UE)
				dev->stats.tx_fifo_errors++;
		}
		dev->stats.tx_packets++;
		dev->stats.tx_bytes += greth->tx_bufs_length[greth->tx_last];
		greth->tx_last = NEXT_TX(greth->tx_last);
		greth->tx_free++;
	}

	if (greth->tx_free > 0) {
		netif_wake_queue(dev);
	}
}

static inline void greth_update_tx_stats(struct net_device *dev, u32 stat)
{
	/* Check status for errors */
	if (unlikely(stat & GRETH_TXBD_STATUS)) {
		dev->stats.tx_errors++;
		if (stat & GRETH_TXBD_ERR_AL)
			dev->stats.tx_aborted_errors++;
		if (stat & GRETH_TXBD_ERR_UE)
			dev->stats.tx_fifo_errors++;
		if (stat & GRETH_TXBD_ERR_LC)
			dev->stats.tx_aborted_errors++;
	}
	dev->stats.tx_packets++;
}

static void greth_clean_tx_gbit(struct net_device *dev)
{
	struct greth_private *greth;
	struct greth_bd *bdp, *bdp_last_frag;
	struct sk_buff *skb = NULL;
	u32 stat;
	int nr_frags, i;
	u16 tx_last;

	greth = netdev_priv(dev);
	tx_last = greth->tx_last;

	while (tx_last != greth->tx_next) {

		skb = greth->tx_skbuff[tx_last];

		nr_frags = skb_shinfo(skb)->nr_frags;

		/* We only clean fully completed SKBs */
		bdp_last_frag = greth->tx_bd_base + SKIP_TX(tx_last, nr_frags);

		GRETH_REGSAVE(greth->regs->status, GRETH_INT_TE | GRETH_INT_TX);
		mb();
		stat = greth_read_bd(&bdp_last_frag->stat);

		if (stat & GRETH_BD_EN)
			break;

		greth->tx_skbuff[tx_last] = NULL;

		greth_update_tx_stats(dev, stat);
		dev->stats.tx_bytes += skb->len;

		bdp = greth->tx_bd_base + tx_last;

		tx_last = NEXT_TX(tx_last);

		dma_unmap_single(greth->dev,
				 greth_read_bd(&bdp->addr),
				 skb_headlen(skb),
				 DMA_TO_DEVICE);

		for (i = 0; i < nr_frags; i++) {
			skb_frag_t *frag = &skb_shinfo(skb)->frags[i];
			bdp = greth->tx_bd_base + tx_last;

			dma_unmap_page(greth->dev,
				       greth_read_bd(&bdp->addr),
				       skb_frag_size(frag),
				       DMA_TO_DEVICE);

			tx_last = NEXT_TX(tx_last);
		}
		dev_kfree_skb(skb);
	}
	if (skb) { /* skb is set only if the above while loop was entered */
		wmb();
		greth->tx_last = tx_last;

		if (netif_queue_stopped(dev) &&
		    (greth_num_free_bds(tx_last, greth->tx_next) >
		    (MAX_SKB_FRAGS+1)))
			netif_wake_queue(dev);
	}
}

static int greth_rx(struct net_device *dev, int limit)
{
	struct greth_private *greth;
	struct greth_bd *bdp;
	struct sk_buff *skb;
	int pkt_len;
	int bad, count;
	u32 status, dma_addr;
	unsigned long flags;

	greth = netdev_priv(dev);

	for (count = 0; count < limit; ++count) {

		bdp = greth->rx_bd_base + greth->rx_cur;
		GRETH_REGSAVE(greth->regs->status, GRETH_INT_RE | GRETH_INT_RX);
		mb();
		status = greth_read_bd(&bdp->stat);

		if (unlikely(status & GRETH_BD_EN)) {
			break;
		}

		dma_addr = greth_read_bd(&bdp->addr);
		bad = 0;

		/* Check status for errors. */
		if (unlikely(status & GRETH_RXBD_STATUS)) {
			if (status & GRETH_RXBD_ERR_FT) {
				dev->stats.rx_length_errors++;
				bad = 1;
			}
			if (status & (GRETH_RXBD_ERR_AE | GRETH_RXBD_ERR_OE)) {
				dev->stats.rx_frame_errors++;
				bad = 1;
			}
			if (status & GRETH_RXBD_ERR_CRC) {
				dev->stats.rx_crc_errors++;
				bad = 1;
			}
		}
		if (unlikely(bad)) {
			dev->stats.rx_errors++;

		} else {

			pkt_len = status & GRETH_BD_LEN;

			skb = netdev_alloc_skb(dev, pkt_len + NET_IP_ALIGN);

			if (unlikely(skb == NULL)) {

				if (net_ratelimit())
					dev_warn(&dev->dev, "low on memory - " "packet dropped\n");

				dev->stats.rx_dropped++;

			} else {
				skb_reserve(skb, NET_IP_ALIGN);

				dma_sync_single_for_cpu(greth->dev,
							dma_addr,
							pkt_len,
							DMA_FROM_DEVICE);

				if (netif_msg_pktdata(greth))
					greth_print_rx_packet(phys_to_virt(dma_addr), pkt_len);

				skb_put_data(skb, phys_to_virt(dma_addr),
					     pkt_len);

				skb->protocol = eth_type_trans(skb, dev);
				dev->stats.rx_bytes += pkt_len;
				dev->stats.rx_packets++;
				netif_receive_skb(skb);
			}
		}

		status = GRETH_BD_EN | GRETH_BD_IE;
		if (greth->rx_cur == GRETH_RXBD_NUM_MASK) {
			status |= GRETH_BD_WR;
		}

		wmb();
		greth_write_bd(&bdp->stat, status);

		dma_sync_single_for_device(greth->dev, dma_addr, MAX_FRAME_SIZE, DMA_FROM_DEVICE);

		spin_lock_irqsave(&greth->devlock, flags); /* save from XMIT */
		greth_enable_rx(greth);
		spin_unlock_irqrestore(&greth->devlock, flags);

		greth->rx_cur = NEXT_RX(greth->rx_cur);
	}

	return count;
}

static inline int hw_checksummed(u32 status)
{

	if (status & GRETH_RXBD_IP_FRAG)
		return 0;

	if (status & GRETH_RXBD_IP && status & GRETH_RXBD_IP_CSERR)
		return 0;

	if (status & GRETH_RXBD_UDP && status & GRETH_RXBD_UDP_CSERR)
		return 0;

	if (status & GRETH_RXBD_TCP && status & GRETH_RXBD_TCP_CSERR)
		return 0;

	return 1;
}

static int greth_rx_gbit(struct net_device *dev, int limit)
{
	struct greth_private *greth;
	struct greth_bd *bdp;
	struct sk_buff *skb, *newskb;
	int pkt_len;
	int bad, count = 0;
	u32 status, dma_addr;
	unsigned long flags;

	greth = netdev_priv(dev);

	for (count = 0; count < limit; ++count) {

		bdp = greth->rx_bd_base + greth->rx_cur;
		skb = greth->rx_skbuff[greth->rx_cur];
		GRETH_REGSAVE(greth->regs->status, GRETH_INT_RE | GRETH_INT_RX);
		mb();
		status = greth_read_bd(&bdp->stat);
		bad = 0;

		if (status & GRETH_BD_EN)
			break;

		/* Check status for errors. */
		if (unlikely(status & GRETH_RXBD_STATUS)) {

			if (status & GRETH_RXBD_ERR_FT) {
				dev->stats.rx_length_errors++;
				bad = 1;
			} else if (status &
				   (GRETH_RXBD_ERR_AE | GRETH_RXBD_ERR_OE | GRETH_RXBD_ERR_LE)) {
				dev->stats.rx_frame_errors++;
				bad = 1;
			} else if (status & GRETH_RXBD_ERR_CRC) {
				dev->stats.rx_crc_errors++;
				bad = 1;
			}
		}

		/* Allocate new skb to replace current, not needed if the
		 * current skb can be reused */
		if (!bad && (newskb=netdev_alloc_skb(dev, MAX_FRAME_SIZE + NET_IP_ALIGN))) {
			skb_reserve(newskb, NET_IP_ALIGN);

			dma_addr = dma_map_single(greth->dev,
						      newskb->data,
						      MAX_FRAME_SIZE + NET_IP_ALIGN,
						      DMA_FROM_DEVICE);

			if (!dma_mapping_error(greth->dev, dma_addr)) {
				/* Process the incoming frame. */
				pkt_len = status & GRETH_BD_LEN;

				dma_unmap_single(greth->dev,
						 greth_read_bd(&bdp->addr),
						 MAX_FRAME_SIZE + NET_IP_ALIGN,
						 DMA_FROM_DEVICE);

				if (netif_msg_pktdata(greth))
					greth_print_rx_packet(phys_to_virt(greth_read_bd(&bdp->addr)), pkt_len);

				skb_put(skb, pkt_len);

				if (dev->features & NETIF_F_RXCSUM && hw_checksummed(status))
					skb->ip_summed = CHECKSUM_UNNECESSARY;
				else
					skb_checksum_none_assert(skb);

				skb->protocol = eth_type_trans(skb, dev);
				dev->stats.rx_packets++;
				dev->stats.rx_bytes += pkt_len;
				netif_receive_skb(skb);

				greth->rx_skbuff[greth->rx_cur] = newskb;
				greth_write_bd(&bdp->addr, dma_addr);
			} else {
				if (net_ratelimit())
					dev_warn(greth->dev, "Could not create DMA mapping, dropping packet\n");
				dev_kfree_skb(newskb);
				/* reusing current skb, so it is a drop */
				dev->stats.rx_dropped++;
			}
		} else if (bad) {
			/* Bad Frame transfer, the skb is reused */
			dev->stats.rx_dropped++;
		} else {
			/* Failed Allocating a new skb. This is rather stupid
			 * but the current "filled" skb is reused, as if
			 * transfer failure. One could argue that RX descriptor
			 * table handling should be divided into cleaning and
			 * filling as the TX part of the driver
			 */
			if (net_ratelimit())
				dev_warn(greth->dev, "Could not allocate SKB, dropping packet\n");
			/* reusing current skb, so it is a drop */
			dev->stats.rx_dropped++;
		}

		status = GRETH_BD_EN | GRETH_BD_IE;
		if (greth->rx_cur == GRETH_RXBD_NUM_MASK) {
			status |= GRETH_BD_WR;
		}

		wmb();
		greth_write_bd(&bdp->stat, status);
		spin_lock_irqsave(&greth->devlock, flags);
		greth_enable_rx(greth);
		spin_unlock_irqrestore(&greth->devlock, flags);
		greth->rx_cur = NEXT_RX(greth->rx_cur);
	}

	return count;

}

static int greth_poll(struct napi_struct *napi, int budget)
{
	struct greth_private *greth;
	int work_done = 0;
	unsigned long flags;
	u32 mask, ctrl;
	greth = container_of(napi, struct greth_private, napi);

restart_txrx_poll:
	if (greth->gbit_mac) {
		greth_clean_tx_gbit(greth->netdev);
		work_done += greth_rx_gbit(greth->netdev, budget - work_done);
	} else {
		if (netif_queue_stopped(greth->netdev))
			greth_clean_tx(greth->netdev);
		work_done += greth_rx(greth->netdev, budget - work_done);
	}

	if (work_done < budget) {

		spin_lock_irqsave(&greth->devlock, flags);

		ctrl = GRETH_REGLOAD(greth->regs->control);
		if ((greth->gbit_mac && (greth->tx_last != greth->tx_next)) ||
		    (!greth->gbit_mac && netif_queue_stopped(greth->netdev))) {
			GRETH_REGSAVE(greth->regs->control,
					ctrl | GRETH_TXI | GRETH_RXI);
			mask = GRETH_INT_RX | GRETH_INT_RE |
			       GRETH_INT_TX | GRETH_INT_TE;
		} else {
			GRETH_REGSAVE(greth->regs->control, ctrl | GRETH_RXI);
			mask = GRETH_INT_RX | GRETH_INT_RE;
		}

		if (GRETH_REGLOAD(greth->regs->status) & mask) {
			GRETH_REGSAVE(greth->regs->control, ctrl);
			spin_unlock_irqrestore(&greth->devlock, flags);
			goto restart_txrx_poll;
		} else {
			napi_complete_done(napi, work_done);
			spin_unlock_irqrestore(&greth->devlock, flags);
		}
	}

	return work_done;
}

static int greth_set_mac_add(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct greth_private *greth;
	struct greth_regs *regs;

	greth = netdev_priv(dev);
	regs = greth->regs;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	eth_hw_addr_set(dev, addr->sa_data);
	GRETH_REGSAVE(regs->esa_msb, dev->dev_addr[0] << 8 | dev->dev_addr[1]);
	GRETH_REGSAVE(regs->esa_lsb, dev->dev_addr[2] << 24 | dev->dev_addr[3] << 16 |
		      dev->dev_addr[4] << 8 | dev->dev_addr[5]);

	return 0;
}

static u32 greth_hash_get_index(__u8 *addr)
{
	return (ether_crc(6, addr)) & 0x3F;
}

static void greth_set_hash_filter(struct net_device *dev)
{
	struct netdev_hw_addr *ha;
	struct greth_private *greth = netdev_priv(dev);
	struct greth_regs *regs = greth->regs;
	u32 mc_filter[2];
	unsigned int bitnr;

	mc_filter[0] = mc_filter[1] = 0;

	netdev_for_each_mc_addr(ha, dev) {
		bitnr = greth_hash_get_index(ha->addr);
		mc_filter[bitnr >> 5] |= 1 << (bitnr & 31);
	}

	GRETH_REGSAVE(regs->hash_msb, mc_filter[1]);
	GRETH_REGSAVE(regs->hash_lsb, mc_filter[0]);
}

static void greth_set_multicast_list(struct net_device *dev)
{
	int cfg;
	struct greth_private *greth = netdev_priv(dev);
	struct greth_regs *regs = greth->regs;

	cfg = GRETH_REGLOAD(regs->control);
	if (dev->flags & IFF_PROMISC)
		cfg |= GRETH_CTRL_PR;
	else
		cfg &= ~GRETH_CTRL_PR;

	if (greth->multicast) {
		if (dev->flags & IFF_ALLMULTI) {
			GRETH_REGSAVE(regs->hash_msb, -1);
			GRETH_REGSAVE(regs->hash_lsb, -1);
			cfg |= GRETH_CTRL_MCEN;
			GRETH_REGSAVE(regs->control, cfg);
			return;
		}

		if (netdev_mc_empty(dev)) {
			cfg &= ~GRETH_CTRL_MCEN;
			GRETH_REGSAVE(regs->control, cfg);
			return;
		}

		/* Setup multicast filter */
		greth_set_hash_filter(dev);
		cfg |= GRETH_CTRL_MCEN;
	}
	GRETH_REGSAVE(regs->control, cfg);
}

static u32 greth_get_msglevel(struct net_device *dev)
{
	struct greth_private *greth = netdev_priv(dev);
	return greth->msg_enable;
}

static void greth_set_msglevel(struct net_device *dev, u32 value)
{
	struct greth_private *greth = netdev_priv(dev);
	greth->msg_enable = value;
}

static int greth_get_regs_len(struct net_device *dev)
{
	return sizeof(struct greth_regs);
}

static void greth_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	struct greth_private *greth = netdev_priv(dev);

	strscpy(info->driver, dev_driver_string(greth->dev),
		sizeof(info->driver));
	strscpy(info->bus_info, greth->dev->bus->name, sizeof(info->bus_info));
}

static void greth_get_regs(struct net_device *dev, struct ethtool_regs *regs, void *p)
{
	int i;
	struct greth_private *greth = netdev_priv(dev);
	u32 __iomem *greth_regs = (u32 __iomem *) greth->regs;
	u32 *buff = p;

	for (i = 0; i < sizeof(struct greth_regs) / sizeof(u32); i++)
		buff[i] = greth_read_bd(&greth_regs[i]);
}

static const struct ethtool_ops greth_ethtool_ops = {
	.get_msglevel		= greth_get_msglevel,
	.set_msglevel		= greth_set_msglevel,
	.get_drvinfo		= greth_get_drvinfo,
	.get_regs_len           = greth_get_regs_len,
	.get_regs               = greth_get_regs,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.set_link_ksettings	= phy_ethtool_set_link_ksettings,
};

static struct net_device_ops greth_netdev_ops = {
	.ndo_open		= greth_open,
	.ndo_stop		= greth_close,
	.ndo_start_xmit		= greth_start_xmit,
	.ndo_set_mac_address	= greth_set_mac_add,
	.ndo_validate_addr	= eth_validate_addr,
};

static inline int wait_for_mdio(struct greth_private *greth)
{
	unsigned long timeout = jiffies + 4*HZ/100;
	while (GRETH_REGLOAD(greth->regs->mdio) & GRETH_MII_BUSY) {
		if (time_after(jiffies, timeout))
			return 0;
	}
	return 1;
}

static int greth_mdio_read(struct mii_bus *bus, int phy, int reg)
{
	struct greth_private *greth = bus->priv;
	int data;

	if (!wait_for_mdio(greth))
		return -EBUSY;

	GRETH_REGSAVE(greth->regs->mdio, ((phy & 0x1F) << 11) | ((reg & 0x1F) << 6) | 2);

	if (!wait_for_mdio(greth))
		return -EBUSY;

	if (!(GRETH_REGLOAD(greth->regs->mdio) & GRETH_MII_NVALID)) {
		data = (GRETH_REGLOAD(greth->regs->mdio) >> 16) & 0xFFFF;
		return data;

	} else {
		return -1;
	}
}

static int greth_mdio_write(struct mii_bus *bus, int phy, int reg, u16 val)
{
	struct greth_private *greth = bus->priv;

	if (!wait_for_mdio(greth))
		return -EBUSY;

	GRETH_REGSAVE(greth->regs->mdio,
		      ((val & 0xFFFF) << 16) | ((phy & 0x1F) << 11) | ((reg & 0x1F) << 6) | 1);

	if (!wait_for_mdio(greth))
		return -EBUSY;

	return 0;
}

static void greth_link_change(struct net_device *dev)
{
	struct greth_private *greth = netdev_priv(dev);
	struct phy_device *phydev = dev->phydev;
	unsigned long flags;
	int status_change = 0;
	u32 ctrl;

	spin_lock_irqsave(&greth->devlock, flags);

	if (phydev->link) {

		if ((greth->speed != phydev->speed) || (greth->duplex != phydev->duplex)) {
			ctrl = GRETH_REGLOAD(greth->regs->control) &
			       ~(GRETH_CTRL_FD | GRETH_CTRL_SP | GRETH_CTRL_GB);

			if (phydev->duplex)
				ctrl |= GRETH_CTRL_FD;

			if (phydev->speed == SPEED_100)
				ctrl |= GRETH_CTRL_SP;
			else if (phydev->speed == SPEED_1000)
				ctrl |= GRETH_CTRL_GB;

			GRETH_REGSAVE(greth->regs->control, ctrl);
			greth->speed = phydev->speed;
			greth->duplex = phydev->duplex;
			status_change = 1;
		}
	}

	if (phydev->link != greth->link) {
		if (!phydev->link) {
			greth->speed = 0;
			greth->duplex = -1;
		}
		greth->link = phydev->link;

		status_change = 1;
	}

	spin_unlock_irqrestore(&greth->devlock, flags);

	if (status_change) {
		if (phydev->link)
			pr_debug("%s: link up (%d/%s)\n",
				dev->name, phydev->speed,
				DUPLEX_FULL == phydev->duplex ? "Full" : "Half");
		else
			pr_debug("%s: link down\n", dev->name);
	}
}

static int greth_mdio_probe(struct net_device *dev)
{
	struct greth_private *greth = netdev_priv(dev);
	struct phy_device *phy = NULL;
	int ret;

	/* Find the first PHY */
	phy = phy_find_first(greth->mdio);

	if (!phy) {
		if (netif_msg_probe(greth))
			dev_err(&dev->dev, "no PHY found\n");
		return -ENXIO;
	}

	ret = phy_connect_direct(dev, phy, &greth_link_change,
				 greth->gbit_mac ? PHY_INTERFACE_MODE_GMII : PHY_INTERFACE_MODE_MII);
	if (ret) {
		if (netif_msg_ifup(greth))
			dev_err(&dev->dev, "could not attach to PHY\n");
		return ret;
	}

	if (greth->gbit_mac)
		phy_set_max_speed(phy, SPEED_1000);
	else
		phy_set_max_speed(phy, SPEED_100);

	linkmode_copy(phy->advertising, phy->supported);

	greth->link = 0;
	greth->speed = 0;
	greth->duplex = -1;

	return 0;
}

static int greth_mdio_init(struct greth_private *greth)
{
	int ret;
	unsigned long timeout;
	struct net_device *ndev = greth->netdev;

	greth->mdio = mdiobus_alloc();
	if (!greth->mdio) {
		return -ENOMEM;
	}

	greth->mdio->name = "greth-mdio";
	snprintf(greth->mdio->id, MII_BUS_ID_SIZE, "%s-%d", greth->mdio->name, greth->irq);
	greth->mdio->read = greth_mdio_read;
	greth->mdio->write = greth_mdio_write;
	greth->mdio->priv = greth;

	ret = mdiobus_register(greth->mdio);
	if (ret) {
		goto error;
	}

	ret = greth_mdio_probe(greth->netdev);
	if (ret) {
		if (netif_msg_probe(greth))
			dev_err(&greth->netdev->dev, "failed to probe MDIO bus\n");
		goto unreg_mdio;
	}

	phy_start(ndev->phydev);

	/* If Ethernet debug link is used make autoneg happen right away */
	if (greth->edcl && greth_edcl == 1) {
		phy_start_aneg(ndev->phydev);
		timeout = jiffies + 6*HZ;
		while (!phy_aneg_done(ndev->phydev) &&
		       time_before(jiffies, timeout)) {
		}
		phy_read_status(ndev->phydev);
		greth_link_change(greth->netdev);
	}

	return 0;

unreg_mdio:
	mdiobus_unregister(greth->mdio);
error:
	mdiobus_free(greth->mdio);
	return ret;
}

/* Initialize the GRETH MAC */
static int greth_of_probe(struct platform_device *ofdev)
{
	struct net_device *dev;
	struct greth_private *greth;
	struct greth_regs *regs;

	int i;
	int err;
	int tmp;
	u8 addr[ETH_ALEN];
	unsigned long timeout;

	dev = alloc_etherdev(sizeof(struct greth_private));

	if (dev == NULL)
		return -ENOMEM;

	greth = netdev_priv(dev);
	greth->netdev = dev;
	greth->dev = &ofdev->dev;

	if (greth_debug > 0)
		greth->msg_enable = greth_debug;
	else
		greth->msg_enable = GRETH_DEF_MSG_ENABLE;

	spin_lock_init(&greth->devlock);

	greth->regs = of_ioremap(&ofdev->resource[0], 0,
				 resource_size(&ofdev->resource[0]),
				 "grlib-greth regs");

	if (greth->regs == NULL) {
		if (netif_msg_probe(greth))
			dev_err(greth->dev, "ioremap failure.\n");
		err = -EIO;
		goto error1;
	}

	regs = greth->regs;
	greth->irq = ofdev->archdata.irqs[0];

	dev_set_drvdata(greth->dev, dev);
	SET_NETDEV_DEV(dev, greth->dev);

	if (netif_msg_probe(greth))
		dev_dbg(greth->dev, "resetting controller.\n");

	/* Reset the controller. */
	GRETH_REGSAVE(regs->control, GRETH_RESET);

	/* Wait for MAC to reset itself */
	timeout = jiffies + HZ/100;
	while (GRETH_REGLOAD(regs->control) & GRETH_RESET) {
		if (time_after(jiffies, timeout)) {
			err = -EIO;
			if (netif_msg_probe(greth))
				dev_err(greth->dev, "timeout when waiting for reset.\n");
			goto error2;
		}
	}

	/* Get default PHY address  */
	greth->phyaddr = (GRETH_REGLOAD(regs->mdio) >> 11) & 0x1F;

	/* Check if we have GBIT capable MAC */
	tmp = GRETH_REGLOAD(regs->control);
	greth->gbit_mac = (tmp >> 27) & 1;

	/* Check for multicast capability */
	greth->multicast = (tmp >> 25) & 1;

	greth->edcl = (tmp >> 31) & 1;

	/* If we have EDCL we disable the EDCL speed-duplex FSM so
	 * it doesn't interfere with the software */
	if (greth->edcl != 0)
		GRETH_REGORIN(regs->control, GRETH_CTRL_DISDUPLEX);

	/* Check if MAC can handle MDIO interrupts */
	greth->mdio_int_en = (tmp >> 26) & 1;

	err = greth_mdio_init(greth);
	if (err) {
		if (netif_msg_probe(greth))
			dev_err(greth->dev, "failed to register MDIO bus\n");
		goto error2;
	}

	/* Allocate TX descriptor ring in coherent memory */
	greth->tx_bd_base = dma_alloc_coherent(greth->dev, 1024,
					       &greth->tx_bd_base_phys,
					       GFP_KERNEL);
	if (!greth->tx_bd_base) {
		err = -ENOMEM;
		goto error3;
	}

	/* Allocate RX descriptor ring in coherent memory */
	greth->rx_bd_base = dma_alloc_coherent(greth->dev, 1024,
					       &greth->rx_bd_base_phys,
					       GFP_KERNEL);
	if (!greth->rx_bd_base) {
		err = -ENOMEM;
		goto error4;
	}

	/* Get MAC address from: module param, OF property or ID prom */
	for (i = 0; i < 6; i++) {
		if (macaddr[i] != 0)
			break;
	}
	if (i == 6) {
		err = of_get_mac_address(ofdev->dev.of_node, addr);
		if (!err) {
			for (i = 0; i < 6; i++)
				macaddr[i] = (unsigned int) addr[i];
		} else {
#ifdef CONFIG_SPARC
			for (i = 0; i < 6; i++)
				macaddr[i] = (unsigned int) idprom->id_ethaddr[i];
#endif
		}
	}

	for (i = 0; i < 6; i++)
		addr[i] = macaddr[i];
	eth_hw_addr_set(dev, addr);

	macaddr[5]++;

	if (!is_valid_ether_addr(&dev->dev_addr[0])) {
		if (netif_msg_probe(greth))
			dev_err(greth->dev, "no valid ethernet address, aborting.\n");
		err = -EINVAL;
		goto error5;
	}

	GRETH_REGSAVE(regs->esa_msb, dev->dev_addr[0] << 8 | dev->dev_addr[1]);
	GRETH_REGSAVE(regs->esa_lsb, dev->dev_addr[2] << 24 | dev->dev_addr[3] << 16 |
		      dev->dev_addr[4] << 8 | dev->dev_addr[5]);

	/* Clear all pending interrupts except PHY irq */
	GRETH_REGSAVE(regs->status, 0xFF);

	if (greth->gbit_mac) {
		dev->hw_features = NETIF_F_SG | NETIF_F_IP_CSUM |
			NETIF_F_RXCSUM;
		dev->features = dev->hw_features | NETIF_F_HIGHDMA;
		greth_netdev_ops.ndo_start_xmit = greth_start_xmit_gbit;
	}

	if (greth->multicast) {
		greth_netdev_ops.ndo_set_rx_mode = greth_set_multicast_list;
		dev->flags |= IFF_MULTICAST;
	} else {
		dev->flags &= ~IFF_MULTICAST;
	}

	dev->netdev_ops = &greth_netdev_ops;
	dev->ethtool_ops = &greth_ethtool_ops;

	err = register_netdev(dev);
	if (err) {
		if (netif_msg_probe(greth))
			dev_err(greth->dev, "netdevice registration failed.\n");
		goto error5;
	}

	/* setup NAPI */
	netif_napi_add(dev, &greth->napi, greth_poll);

	return 0;

error5:
	dma_free_coherent(greth->dev, 1024, greth->rx_bd_base, greth->rx_bd_base_phys);
error4:
	dma_free_coherent(greth->dev, 1024, greth->tx_bd_base, greth->tx_bd_base_phys);
error3:
	mdiobus_unregister(greth->mdio);
error2:
	of_iounmap(&ofdev->resource[0], greth->regs, resource_size(&ofdev->resource[0]));
error1:
	free_netdev(dev);
	return err;
}

static void greth_of_remove(struct platform_device *of_dev)
{
	struct net_device *ndev = platform_get_drvdata(of_dev);
	struct greth_private *greth = netdev_priv(ndev);

	/* Free descriptor areas */
	dma_free_coherent(&of_dev->dev, 1024, greth->rx_bd_base, greth->rx_bd_base_phys);

	dma_free_coherent(&of_dev->dev, 1024, greth->tx_bd_base, greth->tx_bd_base_phys);

	if (ndev->phydev)
		phy_stop(ndev->phydev);
	mdiobus_unregister(greth->mdio);

	unregister_netdev(ndev);

	of_iounmap(&of_dev->resource[0], greth->regs, resource_size(&of_dev->resource[0]));

	free_netdev(ndev);
}

static const struct of_device_id greth_of_match[] = {
	{
	 .name = "GAISLER_ETHMAC",
	 },
	{
	 .name = "01_01d",
	 },
	{},
};

MODULE_DEVICE_TABLE(of, greth_of_match);

static struct platform_driver greth_of_driver = {
	.driver = {
		.name = "grlib-greth",
		.of_match_table = greth_of_match,
	},
	.probe = greth_of_probe,
	.remove_new = greth_of_remove,
};

module_platform_driver(greth_of_driver);

MODULE_AUTHOR("Aeroflex Gaisler AB.");
MODULE_DESCRIPTION("Aeroflex Gaisler Ethernet MAC driver");
MODULE_LICENSE("GPL");
