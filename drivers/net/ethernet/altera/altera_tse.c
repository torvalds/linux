/*
 * Altera Triple-Speed Ethernet MAC driver
 *
 * Copyright (C) 2008-2013 Altera Corporation
 *
 * Contributors:
 *   Dalon Westergreen
 *   Thomas Chou
 *   Ian Abbott
 *   Yuriy Kozlov
 *   Tobias Klauser
 *
 * Original driver contributed by SLS.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>
#include <linux/phy.h>
#include <linux/io.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <asm/cacheflush.h>
#include <linux/delay.h>

#include "altera_tse.h"

#define DRV_NAME "altera_tse"

/* Module parameters */
#define MSGDMA_RX_SIZE 512
static int dma_rx_num = MSGDMA_RX_SIZE;
module_param(dma_rx_num, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dma_rx_num, "Number of descriptors in the RX list");

#define MSGDMA_TX_SIZE 512
static int dma_tx_num = MSGDMA_TX_SIZE;
module_param(dma_tx_num, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(dma_tx_num, "Number of descriptors in the TX list");

/* 1 -> print contents of all tx packets on printk */
#define TX_DEEP_DEBUG 0

static inline void tse_set_bit(void __iomem *ioaddr, u32 bit_mask)
{
	u32 value = ioread32(ioaddr);
	value |= bit_mask;
	iowrite32(value, ioaddr);
}

static inline void tse_clear_bit(void __iomem *ioaddr, u32 bit_mask)
{
	u32 value = ioread32(ioaddr);
	value &= ~bit_mask;
	iowrite32(value, ioaddr);
}

static inline int tse_bit_is_set(void __iomem *ioaddr, u32 bit_mask)
{
	u32 value = ioread32(ioaddr);
	return (value & bit_mask) ? 1 : 0;
}

static inline int tse_bit_is_clear(void __iomem *ioaddr, u32 bit_mask)
{
	u32 value = ioread32(ioaddr);
	return (value & bit_mask) ? 0 : 1;
}

/* Allow network stack to resume queueing packets after we've
 * finished transmitting at least 1/4 of the packets in the queue.
 */
#define TSE_TX_THRESH(x)	(x->dma_tx_size / 4)

static inline u32 tse_tx_avail(struct alt_tse_private *priv)
{
	return priv->dirty_tx + priv->dma_tx_size - priv->cur_tx - 1;
}

/* MDIO specific functions
 */
static int altera_tse_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	struct tse_regs *regs = (struct tse_regs *) bus->priv;
	unsigned int *mdio_regs = (unsigned int *) &regs->mac.mdio_phy0;
	u32 data;

	/* set MDIO address */
	iowrite32((mii_id & 0x1f), &regs->mac.mdio_phy0_addr);

	/* get the data */
	data = ioread32(&mdio_regs[regnum]) & 0xffff;
	return data;
}

static int altera_tse_mdio_write(struct mii_bus *bus, int mii_id, int regnum,
				 u16 value)
{
	struct tse_regs *regs = (struct tse_regs *) bus->priv;
	unsigned int *mdio_regs = (unsigned int *) &regs->mac.mdio_phy0;

	/* set MDIO address */
	iowrite32((mii_id & 0x1f), &regs->mac.mdio_phy0_addr);

	/* write the data */
	iowrite32((u32) value, &mdio_regs[regnum]);
	return 0;
}

/*******************************************************************************
*	mSGDMA Control Stuff
*
*******************************************************************************/
static inline void disable_msgdma_irq(struct msgdma_csr *csr)
{
	tse_clear_bit(&csr->control, MSGDMA_CSR_CTL_GLOBAL_INTR);
}

static inline void enable_msgdma_irq(struct msgdma_csr *csr)
{
	tse_set_bit(&csr->control, MSGDMA_CSR_CTL_GLOBAL_INTR);
}

static inline void clear_msgdma_irq(struct msgdma_csr *csr)
{
	iowrite32(MSGDMA_CSR_STAT_IRQ, &csr->status);
}

/* Resets mSGDMA Rx/Tx dispatcher
 */
static void reset_msgdma(struct alt_tse_private *priv)
{
	struct tse_regs *regs = priv->regs;
	int counter;

	/* Reset Rx mSGDMA */
	iowrite32(MSGDMA_CSR_STAT_MASK, &regs->rx_csr.status);
	iowrite32(MSGDMA_CSR_CTL_RESET, &regs->rx_csr.control);

	counter = 0;
	while (counter++ < ALT_TSE_SW_RESET_WATCHDOG_CNTR) {
		if (tse_bit_is_clear(&regs->rx_csr.status,
				MSGDMA_CSR_STAT_RESETTING))
			break;
		udelay(1);
	}

	if ((counter >= ALT_TSE_SW_RESET_WATCHDOG_CNTR)
			/*&& (netif_msg_drv(priv))*/)
		pr_warn("%s: TSE Rx mSGDMA resetting bit never cleared!\n",
				priv->dev->name);

	/* clear all status bits */
	iowrite32(MSGDMA_CSR_STAT_MASK, &regs->rx_csr.status);

	/* Reset Tx mSGDMA */
	iowrite32(MSGDMA_CSR_STAT_MASK, &regs->tx_csr.status);
	iowrite32(MSGDMA_CSR_CTL_RESET, &regs->tx_csr.control);

	counter = 0;
	while (counter++ < ALT_TSE_SW_RESET_WATCHDOG_CNTR) {
		if (tse_bit_is_clear(&regs->tx_csr.status,
				MSGDMA_CSR_STAT_RESETTING))
			break;
		udelay(1);
	}

	if ((counter >= ALT_TSE_SW_RESET_WATCHDOG_CNTR)
			/*&& (netif_msg_drv(priv))*/)
		pr_warn("%s: TSE Tx mSGDMA resetting bit never cleared!\n",
				priv->dev->name);

	/* clear all status bits */
	iowrite32(MSGDMA_CSR_STAT_MASK, &regs->tx_csr.status);
}

static int tse_init_rx_buffer(struct alt_tse_private *priv, int i)
{
	struct sk_buff *skb;
	unsigned int length = priv->max_data_size;

	skb = __netdev_alloc_skb(priv->dev, length, GFP_KERNEL);
	if (!skb) {
		pr_err("%s: Rx init fails; skb is NULL\n", __func__);
		return -ENOMEM;
	}
	priv->rx_skbuff_dma[i] = dma_map_single(priv->device, skb->data, length,
						DMA_FROM_DEVICE);
	if (dma_mapping_error(priv->device, priv->rx_skbuff_dma[i])) {
		pr_err("%s: DMA mapping error\n", __func__);
		dev_kfree_skb_any(skb);
		return -EINVAL;
	}
	priv->rx_skbuff[i] = skb;
	return 0;
}

static void tse_free_rx_buffer(struct alt_tse_private *priv, int i)
{
	struct sk_buff *skb = priv->rx_skbuff[i];
	dma_addr_t dma_addr = priv->rx_skbuff_dma[i];
	unsigned int length = priv->max_data_size;

	if (skb != NULL) {
		if (dma_addr)
			dma_unmap_single(priv->device, dma_addr, length,
					DMA_FROM_DEVICE);
		dev_kfree_skb_any(skb);
		priv->rx_skbuff[i] = NULL;
		priv->rx_skbuff_dma[i] = 0;
	}
}

static void tse_free_tx_buffer(struct alt_tse_private *priv, int i)
{
	struct sk_buff *skb = priv->tx_skbuff[i];
	dma_addr_t dma_addr = priv->tx_skbuff_dma[i];

	if (skb != NULL) {
		if (dma_addr)
			dma_unmap_single(priv->device, dma_addr, skb->len,
					DMA_TO_DEVICE);
		dev_kfree_skb_any(skb);
		priv->tx_skbuff[i] = NULL;
		priv->tx_skbuff_dma[i] = 0;
	}
}

static int init_msgdma_skbufs(struct alt_tse_private *priv)
{
	unsigned int rx_descs = priv->rx_desc_num;
	unsigned int tx_descs = priv->dma_tx_size;
	int ret = -ENOMEM;
	int i;

	/* Make Rx queue */
	priv->rx_skbuff_dma = kmalloc_array(rx_descs, sizeof(dma_addr_t),
								GFP_KERNEL);
	if (!priv->rx_skbuff_dma)
		goto err_rx_skbuff_dma;

	priv->rx_skbuff = kmalloc_array(rx_descs, sizeof(struct sk_buff *),
								GFP_KERNEL);
	if (!priv->rx_skbuff)
		goto err_rx_skbuff;

	/* Make Tx queue */
	priv->tx_skbuff_dma = kmalloc_array(tx_descs, sizeof(dma_addr_t),
								GFP_KERNEL);
	if (!priv->tx_skbuff_dma)
		goto err_tx_skbuff_dma;

	priv->tx_skbuff = kmalloc_array(tx_descs, sizeof(struct sk_buff *),
								GFP_KERNEL);
	if (!priv->tx_skbuff)
		goto err_tx_skbuff;

	/* Init Rx queue */
	for (i = 0; i < rx_descs; i++) {
		ret = tse_init_rx_buffer(priv, i);
		if (ret)
			goto err_init_rx_buffers;
	}

	priv->cur_rx_desc = 0;
	priv->dirty_rx_desc = 0;

	/* Init Tx queue */
	for (i = 0; i < tx_descs; i++) {
		priv->tx_skbuff_dma[i] = 0;
		priv->tx_skbuff[i] = NULL;
	}

	priv->dirty_tx = 0;
	priv->cur_tx = 0;

	return 0;
err_init_rx_buffers:
	while (--i >= 0)
		tse_free_rx_buffer(priv, i);
	kfree(priv->tx_skbuff);
err_tx_skbuff:
	kfree(priv->tx_skbuff_dma);
err_tx_skbuff_dma:
	kfree(priv->rx_skbuff);
err_rx_skbuff:
	kfree(priv->rx_skbuff_dma);
err_rx_skbuff_dma:
	return ret;
}

static void free_msgdma_skbufs(struct net_device *dev)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	unsigned int rx_descs = priv->rx_desc_num;
	unsigned int tx_descs = priv->dma_tx_size;
	int i;

	/* Release the DMA TX/RX socket buffers */
	for (i = 0; i < rx_descs; i++)
		tse_free_rx_buffer(priv, i);
	for (i = 0; i < tx_descs; i++)
		tse_free_tx_buffer(priv, i);

	kfree(priv->tx_skbuff);
	kfree(priv->tx_skbuff_dma);
	kfree(priv->rx_skbuff);
	kfree(priv->rx_skbuff_dma);
}

/* Put buffer to the mSGDMA RX FIFO
 */
static void sgdma_put_rx_desc(struct alt_tse_private *priv, int entry)
{
	struct msgdma_extended_desc *desc = &priv->regs->rx_desc;
	u32 len = priv->max_data_size;
	dma_addr_t dma_addr = priv->rx_skbuff_dma[entry];
	u32 control = (MSGDMA_DESC_CTL_END_ON_EOP
			| MSGDMA_DESC_CTL_END_ON_LEN
			| MSGDMA_DESC_CTL_TR_COMP_IRQ
			| MSGDMA_DESC_CTL_EARLY_IRQ
			| MSGDMA_DESC_CTL_TR_ERR_IRQ_MSK
			| MSGDMA_DESC_CTL_GO);

	iowrite32(0, &desc->read_addr_lo);
	iowrite32(0, &desc->read_addr_hi);
	iowrite32(dma_addr, &desc->write_addr_lo);
	iowrite32(0, &desc->write_addr_hi);
	iowrite32(len, &desc->len);
	iowrite32(0, &desc->burst_seq_num);
	iowrite32(0x00010001, &desc->stride);
	iowrite32(control, &desc->control);
}

/* Put buffer to the mSGDMA TX FIFO
 */
static void sgdma_put_tx_desc(struct alt_tse_private *priv, int entry)
{
	struct msgdma_extended_desc *desc = &priv->regs->tx_desc;
	u32 len = priv->tx_skbuff[entry]->len;
	dma_addr_t dma_addr = priv->tx_skbuff_dma[entry];
	u32 control = (MSGDMA_DESC_CTL_GEN_SOP
			| MSGDMA_DESC_CTL_GEN_EOP
#ifndef NO_TX_IRQ
			| MSGDMA_DESC_CTL_TR_COMP_IRQ
			| MSGDMA_DESC_CTL_EARLY_IRQ
			| MSGDMA_DESC_CTL_TR_ERR_IRQ_MSK
#endif
			| MSGDMA_DESC_CTL_EARLY_DONE
			| MSGDMA_DESC_CTL_GO);

	iowrite32(dma_addr, &desc->read_addr_lo);
	iowrite32(0, &desc->read_addr_hi);
	iowrite32(0, &desc->write_addr_lo);
	iowrite32(0, &desc->write_addr_hi);
	iowrite32(len, &desc->len);
	iowrite32(0, &desc->burst_seq_num);
	iowrite32(0x00010001, &desc->stride);
	iowrite32(control, &desc->control);
}

/*******************************************************************************
* actual ethernet stuff
*
*******************************************************************************/
static inline void tse_msgdma_rx_refill(struct alt_tse_private *priv)
{
	unsigned int rxsize = priv->rx_desc_num;
	unsigned int entry;
	int ret;

	for (; priv->cur_rx_desc - priv->dirty_rx_desc > 0;
			priv->dirty_rx_desc++) {
		entry = priv->dirty_rx_desc % rxsize;
		if (likely(priv->rx_skbuff[entry] == NULL)) {
			ret = tse_init_rx_buffer(priv, entry);
			if (unlikely(ret != 0)) {
				pr_err("%s: Cannot allocate skb %d\n",
						priv->dev->name, entry);
				break;
			}
			sgdma_put_rx_desc(priv, entry);
			if (netif_msg_rx_status(priv))
				pr_debug("\trefill entry #%d\n", entry);
		}
	}
}

/* This the function called by the napi poll method.
 * It gets all the frames inside the Rx ring.
 */
static int tse_rx(struct alt_tse_private *priv, int limit)
{
	unsigned int count = 0;
	u32 csr_lenght;
	u32 csr_status;
	unsigned int next_entry;
	struct sk_buff *skb;
	unsigned int entry = priv->cur_rx_desc % priv->rx_desc_num;

	while ((ioread32(&priv->regs->rx_csr.resp_fill_level) & 0xffff)
			&& (count < limit)) {
		csr_lenght = ioread32(&priv->regs->rx_resp.bytes_transferred);
		csr_status = ioread32(&priv->regs->rx_resp.status);

		if ((csr_status & 0xFF) || (csr_lenght == 0))
			netdev_err(priv->dev,
					"RCV csr_status %08X csr_lenght %08X\n",
					csr_status, csr_lenght);

		count++;
		next_entry = (++priv->cur_rx_desc) % priv->rx_desc_num;

		skb = priv->rx_skbuff[entry];
		if (unlikely(!skb)) {
			pr_err("%s: Inconsistent Rx descriptor chain\n",
					priv->dev->name);
			priv->dev->stats.rx_dropped++;
			break;
		}
		priv->rx_skbuff[entry] = NULL;

		skb_put(skb, csr_lenght);
		dma_unmap_single(priv->device, priv->rx_skbuff_dma[entry],
				priv->max_data_size, DMA_FROM_DEVICE);

		if (netif_msg_pktdata(priv)) {
			pr_info("frame received (%dbytes)\n", csr_lenght);
			print_hex_dump(KERN_ERR, "data: ", DUMP_PREFIX_OFFSET,
					16, 1, skb->data, csr_lenght, true);
		}

		skb->protocol = eth_type_trans(skb, priv->dev);
		skb_checksum_none_assert(skb);

		napi_gro_receive(&priv->napi, skb);

		priv->dev->stats.rx_packets++;
		priv->dev->stats.rx_bytes += csr_lenght;

		entry = next_entry;
	}

	tse_msgdma_rx_refill(priv);
	return count;
}

/* This the function called by the napi poll method.
 * It reclaims resources after transmission completes.
 */
static void tse_tx_complete(struct alt_tse_private *priv)
{
	unsigned int txsize = priv->dma_tx_size;
	u32 inuse;
	u32 ready;
	u32 status;
	unsigned int entry;
	struct sk_buff *skb;
	dma_addr_t dma_addr;

	spin_lock(&priv->tx_lock);

	/* Get number of sent descriptors */
	inuse = ioread32(&priv->regs->tx_csr.rw_fill_level) & 0xffff;
	if (inuse) { /* Tx FIFO is not empty */
		ready = priv->cur_tx - priv->dirty_tx - inuse - 1;
	} else {
		/* Check for buffered last packet */
		status = ioread32(&priv->regs->tx_csr.status);
		if (status & MSGDMA_CSR_STAT_BUSY)
			ready = priv->cur_tx - priv->dirty_tx - 1;
		else
			ready = priv->cur_tx - priv->dirty_tx;
	}

	/* Free sent buffers */
	while (ready && (priv->dirty_tx != priv->cur_tx)) {
		entry = priv->dirty_tx % txsize;

		skb = priv->tx_skbuff[entry];
		dma_addr = priv->tx_skbuff_dma[entry];

		if (netif_msg_tx_done(priv))
			pr_debug("%s: curr %d, dirty %d\n", __func__,
					priv->cur_tx, priv->dirty_tx);

		if (likely(dma_addr)) {
			dma_unmap_single(priv->device, dma_addr, skb->len,
					DMA_TO_DEVICE);
			priv->tx_skbuff_dma[entry] = 0;
		}
		if (likely(skb != NULL)) {
			dev_kfree_skb(skb);
			priv->tx_skbuff[entry] = NULL;
		}
		priv->dev->stats.tx_packets++;
		priv->dirty_tx++;
		ready--;
	}

	if (unlikely(netif_queue_stopped(priv->dev) &&
			tse_tx_avail(priv) > TSE_TX_THRESH(priv))) {
		if (netif_msg_tx_done(priv))
			pr_debug("%s: restart transmit\n", __func__);
		netif_wake_queue(priv->dev);
	}

	spin_unlock(&priv->tx_lock);
}

/* NAPI Polling function
*	processes packets received, until end of received packets
*	or budget is reached
*	Clear TX buffers
*	also restarts SGDMAs for TX, RX as needed
*/
static int tse_poll(struct napi_struct *napi, int budget)
{
	struct alt_tse_private *priv =
			container_of(napi, struct alt_tse_private, napi);
	struct net_device *dev = priv->dev;
	int work_done = 0;

	tse_tx_complete(priv);

	work_done = tse_rx(priv, budget);
	if (work_done < budget) {
		napi_complete(napi);
		enable_msgdma_irq(&priv->regs->rx_csr);
#ifndef NO_TX_IRQ
		enable_msgdma_irq(&priv->regs->tx_csr);
#endif
		pr_debug("%s: NAPI Complete, did %d packets with budget %d\n",
				dev->name, work_done, budget);
	}
	return work_done;
}

/* SG-DMA TX & RX FIFO interrupt routing
* arg1     :irq number
* arg2     :user data passed to isr
*/
static irqreturn_t alt_sgdma_isr(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct alt_tse_private *priv;

	if (unlikely(!dev)) {
		pr_err("%s: invalid dev pointer\n", __func__);
		return IRQ_NONE;
	}
	priv = netdev_priv(dev);

	/* turn off desc irqs and enable napi rx */
	if (likely(napi_schedule_prep(&priv->napi))) {
		pr_debug("%s: NAPI starting\n", dev->name);
		disable_msgdma_irq(&priv->regs->rx_csr);
#ifndef NO_TX_IRQ
		disable_msgdma_irq(&priv->regs->tx_csr);
#endif
		__napi_schedule(&priv->napi);
	} else {
		/* if we get here, we received another IRQ while processing
		 * NAPI
		 */
		if (netif_msg_intr(priv))
			pr_warn("%s: TSE IRQ received while IRQs disabled\n",
								dev->name);
	}

	/* reset IRQ */
	if (irq == priv->rx_irq)
		clear_msgdma_irq(&priv->regs->rx_csr);
	else if (irq == priv->tx_irq)
		clear_msgdma_irq(&priv->regs->tx_csr);

	return IRQ_HANDLED;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/*
* Polling receive - used by netconsole and other diagnostic tools
* to allow network i/o with interrupts disabled.
*/
static void tse_net_poll_controller(struct net_device *dev)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	disable_irq(tse_priv->rx_irq);
	disable_irq(tse_priv->tx_irq);
	alt_sgdma_isr(tse_priv->rx_irq, dev);
	enable_irq(tse_priv->rx_irq);
	enable_irq(tse_priv->tx_irq);
}
#endif

/*******************************************************************************
* TX and RX functions
*	Send Function
*	Receive function, clears RX Ring - Called from NAPI softirq
*	Clear Transmit buffers - Called from NAPI softirq
*
*******************************************************************************/
/* Send Packet Function
* arg1     :skb to send
* arg2     :netdev device
*/
static int tse_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	unsigned int txsize = priv->dma_tx_size;
	unsigned int entry;
	dma_addr_t dma_addr;
	enum netdev_tx ret = NETDEV_TX_OK;

	spin_lock(&priv->tx_lock);

	if (unlikely(tse_tx_avail(priv) < 1)) {
		if (!netif_queue_stopped(dev)) {
			netif_stop_queue(dev);
			/* This is a hard error, log it. */
			pr_err("%s: Tx list full when queue awake\n", __func__);
		}
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	/* load up descriptor */
	entry = priv->cur_tx % txsize;
	dma_addr = dma_map_single(priv->device, skb->data, skb->len,
							DMA_TO_DEVICE);
	if (dma_mapping_error(priv->device, dma_addr)) {
		pr_err("%s: DMA mapping error\n", __func__);
		ret = NETDEV_TX_BUSY;
		goto out;
	}

	priv->tx_skbuff[entry] = skb;
	priv->tx_skbuff_dma[entry] = dma_addr;
	sgdma_put_tx_desc(priv, entry);

	priv->cur_tx++;

	if (netif_msg_pktdata(priv)) {
		pr_debug("%s: curr=%d dirty=%d entry=%d\n", __func__,
				(priv->cur_tx % txsize),
				(priv->dirty_tx % txsize), entry);

		pr_debug(">>> frame to be transmitted:\n");
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 16, 1,
				skb->data, skb->len, 0);
	}
	if (unlikely(tse_tx_avail(priv) < 1)) {
		if (netif_msg_hw(priv))
			pr_debug("%s: stop transmitted packets\n", __func__);
		netif_stop_queue(dev);
	}

	dev->stats.tx_bytes += skb->len;

out:
	spin_unlock(&priv->tx_lock);
	return ret;
}

/* Called every time the controller might need to be made
 * aware of new link state.  The PHY code conveys this
 * information through variables in the phydev structure, and this
 * function converts those variables into the appropriate
 * register values, and can bring down the device if needed.
 */
static void altera_tse_adjust_link(struct net_device *dev)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	struct phy_device *phydev = priv->phydev;
	int new_state = 0;

	/* only change config if there is a link */
	spin_lock(&priv->mac_cfg_lock);
	if (phydev->link) {
		/* Read old config */
		u32 cfg_reg = ioread32(&priv->regs->mac.command_config);

		/* Check duplex */
		if (phydev->duplex != priv->oldduplex) {
			new_state = 1;
			if (!(phydev->duplex))
				cfg_reg |= MAC_CMDCFG_HD_ENA;
			else
				cfg_reg &= ~MAC_CMDCFG_HD_ENA;

			pr_debug("%s: Link duplex = 0x%x\n", dev->name,
					phydev->duplex);

			priv->oldduplex = phydev->duplex;
		}

		/* Check speed */
		if (phydev->speed != priv->oldspeed) {
			new_state = 1;
			switch (phydev->speed) {
			case 1000:
				cfg_reg |= MAC_CMDCFG_ETH_SPEED;
				cfg_reg &= ~MAC_CMDCFG_ENA_10;
				break;
			case 100:
				cfg_reg &= ~MAC_CMDCFG_ETH_SPEED;
				cfg_reg &= ~MAC_CMDCFG_ENA_10;
				break;
			case 10:
				cfg_reg &= ~MAC_CMDCFG_ETH_SPEED;
				cfg_reg |= MAC_CMDCFG_ENA_10;
				break;
			default:
				if (netif_msg_link(priv))
					netdev_warn(dev, "Speed (%d) is not 10/100/1000!\n",
							phydev->speed);
				break;
			}
			priv->oldspeed = phydev->speed;
		}
		iowrite32(cfg_reg, &priv->regs->mac.command_config);
		netif_carrier_on(dev);

		if (!priv->oldlink) {
			new_state = 1;
			priv->oldlink = 1;
		}
	} else if (priv->oldlink) {
		new_state = 1;
		priv->oldlink = 0;
		priv->oldspeed = 0;
		priv->oldduplex = -1;
		netif_carrier_off(dev);
	}

	if (new_state/* && netif_msg_link(tse_priv)*/)
		phy_print_status(phydev);

	spin_unlock(&priv->mac_cfg_lock);
}

/*******************************************************************************
* Phy init
*	Using shared PHY control interface
*
*******************************************************************************/
/* Initializes driver's PHY state, and attaches to the PHY.
 * Returns 0 on success.
 */
static int init_phy(struct net_device *dev)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	struct phy_device *phydev;
	char phy_id_fmt[MII_BUS_ID_SIZE + 3];
	int ret;

	priv->oldlink = 0;
	priv->oldspeed = 0;
	priv->oldduplex = -1;

	if (priv->phy_addr != -1) {
		snprintf(phy_id_fmt, MII_BUS_ID_SIZE + 3, PHY_ID_FMT,
				priv->mdio->id, priv->phy_addr);

		pr_debug("%s: trying to attach to %s\n", dev->name, phy_id_fmt);

		phydev = phy_connect(dev, phy_id_fmt, &altera_tse_adjust_link,
				priv->phy_iface);
		if (IS_ERR(phydev)) {
			netdev_err(dev, "Could not attach to PHY\n");
			return PTR_ERR(phydev);
		}
	} else {
		phydev = phy_find_first(priv->mdio);
		if (phydev == NULL) {
			netdev_err(dev, "No PHY found\n");
			return -ENXIO;
		}

		ret = phy_connect_direct(dev, phydev, &altera_tse_adjust_link,
				priv->phy_iface);
		if (ret != 0) {
			netdev_err(dev, "Could not attach to PHY\n");
			return ret;
		}
	}

	/* Stop Advertising 1000BASE Capability if interface is not GMII */
	if ((priv->phy_iface == PHY_INTERFACE_MODE_MII)
			|| (priv->phy_iface == PHY_INTERFACE_MODE_RMII))
		phydev->advertising &= ~(SUPPORTED_1000baseT_Half
					| SUPPORTED_1000baseT_Full);

	/*
	 * Broken HW is sometimes missing the pull-up resistor on the
	 * MDIO line, which results in reads to non-existent devices returning
	 * 0 rather than 0xffff. Catch this here and treat 0 as a non-existent
	 * device as well.
	 * Note: phydev->phy_id is the result of reading the UID PHY registers.
	 */
	if (phydev->phy_id == 0) {
		netdev_err(dev, "Bad PHY UID 0x%08x\n", phydev->phy_id);
		phy_disconnect(phydev);
		return -ENODEV;
	}

	pr_debug("%s: attached to PHY %d (UID 0x%08x) Link = %d\n",
			dev->name, phydev->addr, phydev->phy_id, phydev->link);

	priv->phydev = phydev;
	return 0;
}

/*******************************************************************************
* MAC setup and control
*	MAC init, and various setting functions
*
*******************************************************************************/
static void tse_update_mac_addr(struct alt_tse_private *priv, u8 *addr)
{
	struct alt_tse_mac *mac = &priv->regs->mac;
	u32 msb;
	u32 lsb;

	msb = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	lsb = ((addr[5] << 8) | addr[4]) & 0xffff;

	/* Set primary MAC address */
	iowrite32(msb, &mac->mac_addr_0);
	iowrite32(lsb, &mac->mac_addr_1);

	if (priv->enable_sup_addr) {
		/* Set supplemental the MAC addresses */
		iowrite32(msb, &mac->supp_mac_addr_0_0);
		iowrite32(lsb, &mac->supp_mac_addr_0_1);

		iowrite32(msb, &mac->supp_mac_addr_1_0);
		iowrite32(lsb, &mac->supp_mac_addr_1_1);

		iowrite32(msb, &mac->supp_mac_addr_2_0);
		iowrite32(lsb, &mac->supp_mac_addr_2_1);

		iowrite32(msb, &mac->supp_mac_addr_3_0);
		iowrite32(lsb, &mac->supp_mac_addr_3_1);
	}
}

static int reset_mac(struct alt_tse_mac *mac)
{
	void __iomem *cmd_cfg_reg = &mac->command_config;
	int counter;
	u32 dat;

	dat = ioread32(cmd_cfg_reg);
	dat &= ~(MAC_CMDCFG_TX_ENA | MAC_CMDCFG_RX_ENA);
	dat |= MAC_CMDCFG_SW_RESET | MAC_CMDCFG_CNT_RESET;
	iowrite32(dat, cmd_cfg_reg);

	counter = 0;
	while (counter++ < ALT_TSE_SW_RESET_WATCHDOG_CNTR) {
		dat = ioread32(cmd_cfg_reg);
		if (!(dat & MAC_CMDCFG_SW_RESET))
			break;
		udelay(1);
	}

	if (counter >= ALT_TSE_SW_RESET_WATCHDOG_CNTR) {
		/* XXX a workaround */
		dat = ioread32(cmd_cfg_reg);
		dat &= ~MAC_CMDCFG_SW_RESET;
		iowrite32(dat, cmd_cfg_reg);
		return -1;
	}
	return 0;
}

/* Initialize MAC core registers
*/
static int init_mac(struct alt_tse_private *priv)
{
	struct alt_tse_mac *mac = &priv->regs->mac;
	unsigned int cmd = 0;

	/* Setup Rx FIFO */
	iowrite32(priv->rx_fifo_depth - 16, &mac->rx_section_empty);
	iowrite32(0, &mac->rx_section_full);	/* store and forward */
	iowrite32(8, &mac->rx_almost_empty);
	iowrite32(8, &mac->rx_almost_full);

	/* Setup Tx FIFO */
	iowrite32(priv->tx_fifo_depth - 16, &mac->tx_section_empty);
	iowrite32(0, &mac->tx_section_full);	/* store and forward */
	iowrite32(8, &mac->tx_almost_empty);
	iowrite32(3, &mac->tx_almost_full);

	/* MAC Address Configuration */
	tse_update_mac_addr(priv, priv->dev->dev_addr);

	/* MAC Function Configuration */
	iowrite32(priv->max_frame_size, &mac->frm_length);
	iowrite32(12, &mac->tx_ipg_length);
	/*iowrite32(0xffff, &mac->pause_quanta);*/

	/* Disable RX/TX shift 16 for alignment of all received frames on 16-bit
	 * start address
	 */
	tse_clear_bit(&mac->rx_cmd_stat, ALT_TSE_RX_CMD_STAT_RX_SHIFT16);
	tse_clear_bit(&mac->tx_cmd_stat, ALT_TSE_TX_CMD_STAT_TX_SHIFT16);

	/* Set the MAC options */
	cmd = ioread32(&mac->command_config);
	cmd |= MAC_CMDCFG_PAD_EN;	/* Padding Removal on Receive */
	cmd &= ~MAC_CMDCFG_CRC_FWD;	/* CRC Removal */
	cmd |= MAC_CMDCFG_RX_ERR_DISC;	/* Automatically discard frames
					   with CRC errors
					 */
	cmd |= MAC_CMDCFG_CNTL_FRM_ENA;
	iowrite32(cmd, &mac->command_config);

	pr_debug("%s: MAC post-initialization: CMD_CONFIG=0x%08x\n",
			priv->dev->name, cmd);

	return 0;
}

static void tse_set_mac(struct alt_tse_private *priv, bool enable)
{
	struct alt_tse_mac *mac = &priv->regs->mac;
	u32 value = ioread32(&mac->command_config);

	if (enable)
		value |= MAC_CMDCFG_TX_ENA | MAC_CMDCFG_RX_ENA;
	else
		value &= ~(MAC_CMDCFG_TX_ENA | MAC_CMDCFG_RX_ENA);

	iowrite32(value, &mac->command_config);
}

/* Change the MTU
 * The interface is opened whenever 'ifconfig' activates it
 *  arg1   : 'net_device' structure pointer
 *  arg2   : new mtu value
 *  return : 0
 */
/* TODO
 */
static int tse_change_mtu(struct net_device *dev, int new_mtu)
{
	if (netif_running(dev)) {
		pr_err("%s: must be stopped to change its MTU\n", dev->name);
		return -EBUSY;
	}

	return -EINVAL;
}

/* Program multicasts mac addresses into hash look-up table
 * arg1    : net device for which multicasts filter is adjusted
 * arg2    : multicasts address count
 * arg3    : list of multicasts addresses
*/
static void tse_set_hash_table(struct net_device *dev)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	struct alt_tse_mac *mac = &priv->regs->mac;
	struct netdev_hw_addr *ha;

	netdev_for_each_mc_addr(ha, dev) {
		unsigned int hash = 0;
		int mac_octet;

		for (mac_octet = 5; mac_octet >= 0; mac_octet--) {
			unsigned char xor_bit = 0;
			unsigned char octet = ha->addr[mac_octet];
			unsigned int bitshift;

			for (bitshift = 0; bitshift < 8; bitshift++)
				xor_bit ^= ((octet >> bitshift) & 0x01);
			hash = (hash << 1) | xor_bit;
		}

		iowrite32(1, &mac->hash_table[hash]);
	}
}

/* Set/Clear multicasts filter
 * arg1    : net device for which multicasts filter is adjusted
 *           multicasts table from the linked list of addresses
 *           associated with this dev structure.
 */
static void tse_set_multicast_list(struct net_device *dev)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	struct alt_tse_mac *mac = &priv->regs->mac;
	int hash_loop;

	spin_lock(&priv->mac_cfg_lock);
	if (dev->flags & IFF_PROMISC) {
		/* Log any net taps */
		tse_set_bit(&mac->command_config, MAC_CMDCFG_PROMIS_EN);
	} else {
		tse_clear_bit(&mac->command_config, MAC_CMDCFG_PROMIS_EN);
	}

	if (priv->ena_hash) {
		if (dev->flags & IFF_ALLMULTI) {
			for (hash_loop = 0; hash_loop < 64; hash_loop++)
				iowrite32(1, &mac->hash_table[hash_loop]);
		} else {
			for (hash_loop = 0; hash_loop < 64; hash_loop++)
				/* Clear any existing hash entries */
				iowrite32(0, &mac->hash_table[hash_loop]);

			tse_set_hash_table(dev);
		}
	}
	spin_unlock(&priv->mac_cfg_lock);
}

/*******************************************************************************
* Driver Open, shutdown, probe functions
*
*******************************************************************************/

/* Open and Initialize the interface
 * The interface is opened whenever 'ifconfig' activates it
 *  arg1   : 'net_device' structure pointer
 *  return : 0
 */
static int tse_open(struct net_device *dev)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	int ret = 0;
	int i;

	/* Reset and configure TSE MAC and probe associated PHY */
	ret = init_phy(dev);
	if (ret != 0) {
		netdev_err(dev, "Cannot attach to PHY (error: %d)\n", ret);
		goto phy_error;
	}

	spin_lock(&priv->mac_cfg_lock);
	ret = reset_mac(&priv->regs->mac);
	if (ret)
		netdev_err(dev, "Cannot reset MAC core (error: %d)\n", ret);

	ret = init_mac(priv);
	spin_unlock(&priv->mac_cfg_lock);
	if (ret) {
		netdev_err(dev, "Cannot init MAC core (error: %d)\n", ret);
		goto msgdma_skbuf_error;
	}

	reset_msgdma(priv);

	/* Create and initialize the TX/RX descriptors chains. */
	priv->rx_desc_num = dma_rx_num;/*L1_CACHE_ALIGN(msgdma_rxsize);*/
	priv->dma_tx_size = dma_tx_num;
	priv->max_data_size = 2048;
	ret = init_msgdma_skbufs(priv);
	if (ret) {
		netdev_err(dev, "DMA descriptors initialization failed\n");
		goto msgdma_skbuf_error;
	}

	/* Setup mSGDMA RX descriptor chain */
	for (i = 0; i < priv->rx_desc_num; i++)
		sgdma_put_rx_desc(priv, i);

	/* Register RX SGDMA interrupt */
	ret = request_irq(priv->rx_irq, alt_sgdma_isr, IRQF_SHARED,
			dev->name, dev);
	if (ret) {
		netdev_err(dev, "Unable to register RX mSGDMA interrupt %d\n",
				priv->rx_irq);
		goto init_error;
	}
	enable_msgdma_irq(&priv->regs->rx_csr);

#ifndef NO_TX_IRQ
	/* Register TX SGDMA interrupt */
	ret = request_irq(priv->tx_irq, alt_sgdma_isr, IRQF_SHARED,
			dev->name, dev);
	if (ret) {
		netdev_err(dev, "Unable to register TX mSGDMA interrupt %d\n",
				priv->tx_irq);
		goto tx_request_irq_error;
	}
	enable_msgdma_irq(&priv->regs->tx_csr);
#endif

	/* Start MAC Rx/Tx */
	spin_lock(&priv->mac_cfg_lock);
	tse_set_mac(priv, true);
	spin_unlock(&priv->mac_cfg_lock);

	if (priv->phydev)
		phy_start(priv->phydev);

	napi_enable(&priv->napi);
	netif_start_queue(dev);

	return 0;

#ifndef NO_TX_IRQ
tx_request_irq_error:
	free_irq(priv->rx_irq, dev);
	disable_msgdma_irq(&priv->regs->rx_csr);
#endif
init_error:
	free_msgdma_skbufs(dev);
msgdma_skbuf_error:
	if (priv->phydev) {
		phy_disconnect(priv->phydev);
		priv->phydev = NULL;
	}
phy_error:
	return ret;
}

/* Stop TSE MAC interface - this puts the device in an inactive state
 * arg1   : 'net_device' structure pointer
 * return : 0
 */
static int tse_shutdown(struct net_device *dev)
{
	struct alt_tse_private *priv = netdev_priv(dev);
	int ret;

	/* Stop and disconnect the PHY */
	if (priv->phydev) {
		phy_stop(priv->phydev);
		phy_disconnect(priv->phydev);
		priv->phydev = NULL;
	}

	netif_stop_queue(dev);
	napi_disable(&priv->napi);

	/* Free the IRQ lines */
	free_irq(priv->rx_irq, dev);
#ifndef NO_TX_IRQ
	free_irq(priv->tx_irq, dev);
#endif

	/* disable and reset the MAC, empties fifo */
	spin_lock(&priv->mac_cfg_lock);
	spin_lock(&priv->tx_lock);

	ret = reset_mac(&priv->regs->mac);
	if (ret)
		netdev_err(dev, "Cannot reset MAC core (error: %d)\n", ret);
	reset_msgdma(priv);
	free_msgdma_skbufs(dev);

	spin_unlock(&priv->tx_lock);
	spin_unlock(&priv->mac_cfg_lock);

	netif_carrier_off(dev);

	return 0;
}

static const struct net_device_ops altera_tse_netdev_ops = {
	.ndo_open		= tse_open,
	.ndo_stop		= tse_shutdown,
	.ndo_start_xmit		= tse_start_xmit,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_set_rx_mode	= tse_set_multicast_list,
	.ndo_change_mtu		= tse_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= tse_net_poll_controller
#endif
};

static int altera_tse_get_of_prop(struct platform_device *pdev,
				  const char *name, unsigned int *val)
{
	const __be32 *tmp;
	int len;

	tmp = of_get_property(pdev->dev.of_node, name, &len);
	if (!tmp || len < sizeof(__be32))
		return -ENODEV;

	*val = be32_to_cpup(tmp);
	return 0;
}

static int altera_tse_get_phy_iface_prop(struct platform_device *pdev,
					 phy_interface_t *iface)
{
	const void *prop;
	int len;

	prop = of_get_property(pdev->dev.of_node, "phy-mode", &len);
	if (!prop)
		return -ENOENT;
	if (len < 4)
		return -EINVAL;

	if (!strncmp((char *)prop, "mii", 3)) {
		*iface = PHY_INTERFACE_MODE_MII;
		return 0;
	} else if (!strncmp((char *)prop, "gmii", 4)) {
		*iface = PHY_INTERFACE_MODE_GMII;
		return 0;
	} else if (!strncmp((char *)prop, "rgmii-id", 8)) {
		*iface = PHY_INTERFACE_MODE_RGMII_ID;
		return 0;
	} else if (!strncmp((char *)prop, "rgmii", 5)) {
		*iface = PHY_INTERFACE_MODE_RGMII;
		return 0;
	} else if (!strncmp((char *)prop, "sgmii", 5)) {
		*iface = PHY_INTERFACE_MODE_SGMII;
		return 0;
	}

	return -EINVAL;
}

/**
 * altera_tse_probe() - probe Altera TSE MAC device
 * pdev:	platform device
 */
static int altera_tse_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	int ret = -ENODEV;
	struct resource *res, *regs;
	struct alt_tse_private *priv;
	struct mii_bus *mdio;
	int i, len;
	const unsigned char *macaddr;

	ndev = alloc_etherdev(sizeof(struct alt_tse_private));
	if (!ndev) {
		dev_err(&pdev->dev, "Could not allocate network device\n");
		return -ENODEV;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);

	priv = netdev_priv(ndev);
	priv->device = &pdev->dev;
	priv->dev = ndev;

	/* TSE MAC register area */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot obtain MAC register area\n");
		ret = -ENODEV;
		goto out_free;
	}

	regs = devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), DRV_NAME);
	if (!regs) {
		dev_err(&pdev->dev, "cannot request MAC register area\n");
		ret = -EBUSY;
		goto out_free;
	}

	priv->regs = devm_ioremap_nocache(&pdev->dev, regs->start,
			resource_size(regs));
	if (!priv->regs) {
		dev_err(&pdev->dev, "cannot remap MAC register area\n");
		ret = -ENOMEM;
		goto out_free;
	}

	/* RX SGDMA IRQ */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot obtain mSGDMA RX IRQ\n");
		ret = -ENODEV;
		goto out_free;
	}

	priv->rx_irq = res->start;

	/* TX SGDMA IRQ  */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (!res) {
		dev_err(&pdev->dev, "cannot obtain mSGDMA TX IRQ\n");
		ret = -ENODEV;
		goto out_free;
	}

	priv->tx_irq = res->start;

	/* get RX FIFO depth from device tree (assuming FIFO width = 4) */
	ret = altera_tse_get_of_prop(pdev, "ALTR,rx-fifo-depth",
					&priv->rx_fifo_depth);
	if (ret) {
		dev_err(&pdev->dev, "cannot obtain rx-fifo-depth\n");
		goto out_free;
	}

	/* get TX FIFO depth from device tree (assuming FIFO width = 4) */
	ret = altera_tse_get_of_prop(pdev, "ALTR,tx-fifo-depth",
					&priv->tx_fifo_depth);
	if (ret) {
		dev_err(&pdev->dev, "cannot obtain tx-fifo-depth\n");
		goto out_free;
	}

	/* get max frame size from device tree */
	ret = altera_tse_get_of_prop(pdev, "max-frame-size",
					&priv->max_frame_size);
	if (ret) {
		dev_err(&pdev->dev, "cannot obtain max-frame-size\n");
		goto out_free;
	}

	/* get supplementary HW address supporting from device tree */
	ret = altera_tse_get_of_prop(pdev, "ALTR,enable-sup-addr",
					&priv->enable_sup_addr);
	if (ret) {
		dev_err(&pdev->dev, "cannot obtain enable-sup-addr\n");
		goto out_free;
	}

	/* get multicast hash table supporting from device tree */
	ret = altera_tse_get_of_prop(pdev, "ALTR,ena-hash",
					&priv->ena_hash);
	if (ret) {
		dev_err(&pdev->dev, "cannot obtain ena-hash\n");
		goto out_free;
	}

	/* get default MAC address from device tree */
	macaddr = of_get_property(pdev->dev.of_node, "local-mac-address", &len);
	if (macaddr && len == ETH_ALEN)
		memcpy(ndev->dev_addr, macaddr, ETH_ALEN);

	/* If we didn't get a valid address, generate a random one */
	if (!is_valid_ether_addr(ndev->dev_addr))
		eth_hw_addr_random(ndev);

	/* Write it to the MAC address register */
	/*tse_update_mac_addr(priv, ndev->dev_addr);*/

	/* get MII ID from device tree */
	ret = altera_tse_get_of_prop(pdev, "ALTR,mii-id", &priv->mii_id);
	if (ret) {
		dev_err(&pdev->dev, "cannot obtain mii-id\n");
		goto out_free;
	}

	ret = altera_tse_get_phy_iface_prop(pdev, &priv->phy_iface);
	if (ret == -ENOENT) {
		/* backward compatability, assume RGMII */
		dev_warn(&pdev->dev,
			 "cannot obtain PHY interface mode, assuming RGMII\n");
		priv->phy_iface = PHY_INTERFACE_MODE_RGMII;
	} else if (ret) {
		dev_err(&pdev->dev, "unknown PHY interface mode\n");
		goto out_free;
	}

	/* try to get PHY address from device tree, use PHY autodetection if
	 * no valid address is given
	 */
	ret = altera_tse_get_of_prop(pdev, "ALTR,phy-addr", &priv->phy_addr);
	if (ret)
		priv->phy_addr = -1;

	mdio = mdiobus_alloc();
	if (mdio == NULL) {
		netdev_err(ndev, "error allocating MDIO bus\n");
		ret = -ENOMEM;
		goto out_free;
	}

	mdio->name = DRV_NAME;
	mdio->read = &altera_tse_mdio_read;
	mdio->write = &altera_tse_mdio_write;
	snprintf(mdio->id, MII_BUS_ID_SIZE, "%s-%u", mdio->name, priv->mii_id);

	mdio->irq = kcalloc(PHY_MAX_ADDR, sizeof(int), GFP_KERNEL);
	if (mdio->irq == NULL) {
		ret = -ENOMEM;
		goto out_free_mdio;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		mdio->irq[i] = PHY_POLL;

	mdio->priv = (void *) priv->regs;

	ret = mdiobus_register(mdio);
	if (ret != 0) {
		dev_err(&pdev->dev, "cannot register MDIO bus %s\n", mdio->id);
		goto out_free_mdio_irq;
	}

	priv->mdio = mdio;

	/* initialize netdev */
	ether_setup(ndev);
	ndev->base_addr = (unsigned long) priv->regs;
	ndev->netdev_ops = &altera_tse_netdev_ops;
	tse_set_ethtool_ops(ndev);

	ndev->hw_features &= ~NETIF_F_SG;
	ndev->features &= ~NETIF_F_SG;

	/* setup NAPI interface */
	netif_napi_add(ndev, &priv->napi, tse_poll, NAPI_POLL_WEIGHT);

	spin_lock_init(&priv->mac_cfg_lock);
	spin_lock_init(&priv->tx_lock);

	ret = register_netdev(ndev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register TSE net device\n");
		goto out_free_mdio_irq;
	}

	platform_set_drvdata(pdev, ndev);

	pr_info("%s: Altera TSE MAC at 0x%08lx irq %d/%d\n", ndev->name,
			(unsigned long) priv->regs, priv->rx_irq, priv->tx_irq);

	return 0;

out_free_mdio_irq:
	kfree(mdio->irq);
out_free_mdio:
	mdiobus_free(mdio);
out_free:
	free_netdev(ndev);
	return ret;
}

/**
 * altera_tse_remove() - remove Altera TSE MAC device
 * pdev:	platform device
 */
static int altera_tse_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct alt_tse_private *priv = netdev_priv(ndev);

	platform_set_drvdata(pdev, NULL);

	if (priv->mdio) {
		mdiobus_unregister(priv->mdio);
		kfree(priv->mdio->irq);
		mdiobus_free(priv->mdio);
	}

	netif_carrier_off(ndev);

	if (ndev) {
		unregister_netdev(ndev);
		free_netdev(ndev);
	}

	return 0;
}

static struct of_device_id altera_tse_of_match[] = {
	{ .compatible = "ALTR,tse-1.0", },
	{},
};
MODULE_DEVICE_TABLE(of, altera_tse_of_match);

static struct platform_driver altera_tse_driver = {
	.probe		= altera_tse_probe,
	.remove		= altera_tse_remove,
	.suspend	= NULL,
	.resume		= NULL,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = altera_tse_of_match,
	},
};

module_platform_driver(altera_tse_driver);

MODULE_AUTHOR("Altera Corporation");
MODULE_DESCRIPTION("Altera Triple Speed Ethernet MAC driver");
MODULE_LICENSE("GPL");
