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

#include "altera_tse.h"

#define DRV_NAME "altera_tse"

/* 1 -> print contents of all tx packets on printk */
#define TX_DEEP_DEBUG 0

/* MDIO specific functions */

static int altera_tse_mdio_read(struct mii_bus *bus, int mii_id, int regnum)
{
	alt_tse_mac *mac_dev;
	unsigned int *mdio_regs;
	unsigned int data;

	mac_dev = (alt_tse_mac *) bus->priv;

	/* set MDIO address */
	writel(mii_id, &mac_dev->mdio_phy1_addr);
	mdio_regs = (unsigned int *) &mac_dev->mdio_phy1;

	/* get the data */
	data = readl(&mdio_regs[regnum]);

	return data & 0xffff;
}

static int altera_tse_mdio_write(struct mii_bus *bus, int mii_id, int regnum,\
				u16 value)
{
	alt_tse_mac *mac_dev;
	unsigned int *mdio_regs;
	unsigned int data;

	mac_dev = (alt_tse_mac *) bus->priv;

	/* set MDIO address */
	writel(mii_id, &mac_dev->mdio_phy1_addr);
	mdio_regs = (unsigned int *) &mac_dev->mdio_phy1;

	/* get the data */
	data = (unsigned int) value;

	writel(data, &mdio_regs[regnum]);

	return 0;
}

/*******************************************************************************
*	SGDMA Control Stuff
*
*******************************************************************************/

/* Clear descriptor memory ,initialize SGDMA descriptor chain and reset SGDMA.
* arg1     :TSE private data structure
* @return  :1 on success
*           less than zero on error
*/
static void sgdma_config(struct alt_tse_private *tse_priv)
{
	unsigned int mem_off, *mem_ptr =
		(unsigned int *)tse_priv->desc_mem_base,
	mem_size = ALT_TSE_TOTAL_SGDMA_DESC_SIZE;

	/* Clearing SGDMA desc Memory */
	for (mem_off = 0; mem_off < mem_size; mem_off += 4)
		*mem_ptr++ = 0x00000000;

	/* reset rx_sgdma */
	tse_priv->rx_sgdma_dev->control = ALT_SGDMA_CONTROL_SOFTWARERESET_MSK;
	tse_priv->rx_sgdma_dev->control = 0x0;

	/* reset tx_sgdma */
	tse_priv->tx_sgdma_dev->control = ALT_SGDMA_CONTROL_SOFTWARERESET_MSK;
	tse_priv->tx_sgdma_dev->control = 0x0;
}

/* This is a generic routine that the SGDMA mode-specific routines
* call to populate a descriptor.
* arg1     :pointer to first SGDMA descriptor.
* arg2     :pointer to next  SGDMA descriptor.
* arg3     :Address to where data to be written.
* arg4     :Address from where data to be read.
* arg5     :no of byte to transaction.
* arg6     :variable indicating to generate start of packet or not
* arg7     :read fixed
* arg8     :write fixed
* arg9     :read burst
* arg10    :write burst
* arg11    :atlantic_channel number
*/
static void alt_sgdma_construct_descriptor_burst(
				volatile struct alt_sgdma_descriptor *desc,
				volatile struct alt_sgdma_descriptor *next,
				unsigned int *read_addr,
				unsigned int *write_addr,
				unsigned short length_or_eop,
				int generate_eop,
				int read_fixed,
				int write_fixed_or_sop,
				int read_burst,
				int write_burst,
				unsigned char atlantic_channel)
{
	/*
	* Mark the "next" descriptor as "not" owned by hardware. This prevents
	* The SGDMA controller from continuing to process the chain. This is
	* done as a single IO write to bypass cache, without flushing
	* the entire descriptor, since only the 8-bit descriptor status must
	* be flushed.
	*/
	next->descriptor_control = (next->descriptor_control
			& ~ALT_SGDMA_DESCRIPTOR_CONTROL_OWNED_BY_HW_MSK);
#if TX_DEEP_DEBUG
	if (read_addr != 0) {
		unsigned char *buf = ((unsigned char *)read_addr) + 2;
		int i;
		int len  = length_or_eop;

		/* first 2 bytes of the mac-address of my laptop...
		*/
		if (buf[0] == 0 && buf[1] == 0x12) {
			pr_debug("incoming tx descriptor read_addr:%p len:%d",
				read_addr, len);
			BUG_ON(write_addr != 0);
			for (i = 0; i < len-2; i++) {
				if ((i % 16) == 0)
					pr_debug("\n%04x: ", i);

				if ((i % 16) == 8)/* emulate wireshark output*/
					pr_debug(" ");

				pr_debug("%02x ", buf[i]); /* assume tx_shift */
			}
			pr_debug("\n -- end of packet data\n");
		}
	}
#endif
	desc->source = read_addr;
	desc->destination = write_addr;
	desc->next = (unsigned int *)next;
	desc->source_pad = 0x0;
	desc->destination_pad = 0x0;
	desc->next_pad = 0x0;
	desc->bytes_to_transfer = length_or_eop;
	desc->actual_bytes_transferred = 0;
	desc->descriptor_status = 0x0;

	/* SGDMA burst not currently supported */
	desc->read_burst = 0;	/* read_burst; TBD */
	desc->write_burst = 0;	/* write_burst; TBD */

	/* Set the descriptor control block as follows:
	* - Set "owned by hardware" bit
	* - Optionally set "generate EOP" bit
	* - Optionally set the "read from fixed address" bit
	* - Optionally set the "write to fixed address bit (which serves
	*   serves as a "generate SOP" control bit in memory-to-stream mode).
	* - Set the 4-bit atlantic channel, if specified
	*
	* Note that this step is performed after all other descriptor
	* information has been filled out so that, if the controller already
	* happens to be pointing at this descriptor, it will not run (via the
	* "owned by hardware" bit) until all other descriptor information has
	* been set up.
	*/

	desc->descriptor_control = (
		(ALT_SGDMA_DESCRIPTOR_CONTROL_OWNED_BY_HW_MSK) |
		(generate_eop ?
		ALT_SGDMA_DESCRIPTOR_CONTROL_GENERATE_EOP_MSK : 0x0) |
		(read_fixed ?
		ALT_SGDMA_DESCRIPTOR_CONTROL_READ_FIXED_ADDRESS_MSK : 0x0) |
		(write_fixed_or_sop ?
		ALT_SGDMA_DESCRIPTOR_CONTROL_WRITE_FIXED_ADDRESS_MSK : 0x0) |
		(atlantic_channel ? ((atlantic_channel & 0x0F) << 3) : 0)
	);
}

/* Start to copy from rxFIFO into given buffer memory area with Asynchronous
* .so the function does not return the actual bytes transferred for current
* descriptor
* arg1     :TSE private data structure
* arg2     :Pointer to first descriptor structure of RX SGDMA chain
* return   0 on success
*          less than 0 on errors
*/
static int sgdma_async_read(struct alt_tse_private *tse_priv,
			    volatile struct alt_sgdma_descriptor *rx_desc)
{
	unsigned int timeout;
	unsigned int retval = 0;
	struct net_device *dev = tse_priv->dev;

	/* Make sure SGDMA controller is not busy from a former command */
	timeout = 0;

	pr_debug("%s: Waiting while RX SGDMA is busy\n", dev->name);

	tse_priv->rx_sgdma_dev->control = 0;
	tse_priv->rx_sgdma_dev->status = 0x1f;	/* clear status */

	/* Wait for the descriptor (chain) to complete */
	while (tse_priv->rx_sgdma_dev->status & ALT_SGDMA_STATUS_BUSY_MSK) {
		ndelay(100);
		if (timeout++ == ALT_TSE_SGDMA_BUSY_WATCHDOG_CNTR) {
			pr_debug("%s: RX SGDMA timeout\n", dev->name);
			return -EBUSY;
		}
	}

	tse_priv->rx_sgdma_dev->next_descriptor_pointer =
		(int)(volatile struct alt_sgdma_descriptor *)rx_desc;

	/* Don't just enable IRQs adhoc */
	tse_priv->rx_sgdma_dev->control = tse_priv->rx_sgdma_imask |
		ALT_SGDMA_CONTROL_RUN_MSK;

	return retval;
}

static int sgdma_async_write(struct alt_tse_private *tse_priv,
			    volatile struct alt_sgdma_descriptor *tx_desc)
{
	unsigned int timeout;
	unsigned int retval = 0;
	struct net_device *dev = tse_priv->dev;

	/* Make sure SGDMA controller is not busy from a former command */
	timeout = 0;

	tse_priv->tx_sgdma_dev->control = 0;
	tse_priv->tx_sgdma_dev->status = 0x1f;	/* clear status */

	/* Wait for the descriptor (chain) to complete */
	while (tse_priv->tx_sgdma_dev->status & ALT_SGDMA_STATUS_BUSY_MSK) {
		ndelay(100);
		if (timeout++ == ALT_TSE_SGDMA_BUSY_WATCHDOG_CNTR) {
			pr_debug("%s: TX SGDMA timeout\n", dev->name);
			return -EBUSY;
		}
	}

	tse_priv->tx_sgdma_dev->next_descriptor_pointer =
		(int)(volatile struct alt_sgdma_descriptor *)tx_desc;

	/* Don't just enable IRQs adhoc */
	tse_priv->tx_sgdma_dev->control = tse_priv->tx_sgdma_imask |
		ALT_SGDMA_CONTROL_RUN_MSK;

	return retval;
}

/* Create TSE descriptor for next buffer
* or error if no buffer available
*/
static int tse_sgdma_add_buffer(struct net_device *dev)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	int next_head;
	struct sk_buff *skb;

	next_head = (tse_priv->rx_sgdma_descriptor_head + 1) &
			ALT_RX_RING_MOD_MASK;

	if (next_head == tse_priv->rx_sgdma_descriptor_tail)
		return -EBUSY;

	/* current MTU + 4 b/c input packet is aligned by 2; */
	skb = dev_alloc_skb(tse_priv->current_mtu + 4);
	if (skb == NULL) {
		if (netif_msg_rx_err(tse_priv))
			pr_warn("%s :ENOMEM:::skb_size=%d\n",
				dev->name, tse_priv->current_mtu + 4);
		return -ENOMEM;
	}
	flush_dcache_range((unsigned long)skb->head,
			   (unsigned long)skb->end);
	skb->dev = dev;

	tse_priv->rx_skb[tse_priv->rx_sgdma_descriptor_head] = skb;

	alt_sgdma_construct_descriptor_burst(
		(volatile struct alt_sgdma_descriptor *)
		&tse_priv->sgdma_rx_desc[tse_priv->rx_sgdma_descriptor_head],
		(volatile struct alt_sgdma_descriptor *)
		&tse_priv->sgdma_rx_desc[next_head],
		NULL, /* read addr */
		(unsigned int *)tse_priv->
			rx_skb[tse_priv->rx_sgdma_descriptor_head]->data,
		0x0, /* length or EOP */
		0x0, /* gen eop */
		0x0, /* read fixed */
		0x0, /* write fixed or sop */
		0x0, /* read burst */
		0x0, /* write burst */
		0x0 /* channel */
	);

	tse_priv->rx_sgdma_descriptor_head = next_head;

	return 0;
}

/* Init and setup SGDMA descriptor chain */
static int sgdma_read_init(struct net_device *dev)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	int rx_loop;
	int ret;

	for (rx_loop = 0; rx_loop < ALT_TSE_RX_SGDMA_DESC_COUNT; rx_loop++)
		ret = tse_sgdma_add_buffer(dev);

	sgdma_async_read(tse_priv,
			 &tse_priv->sgdma_rx_desc[tse_priv->
						  rx_sgdma_descriptor_tail]);
	pr_debug("%s: SGDMA read init completed\n", dev->name);

	return 0;
}

/*******************************************************************************
* actual ethernet stuff
*
*******************************************************************************/
/* NAPI Polling function
*	processes packets received, until end of received packets
*	or budget is reached
*	Clear TX buffers
*	also restarts SGDMAs for TX, RX as needed
*/
static int tse_poll(struct napi_struct *napi, int budget)
{
	struct alt_tse_private *tse_priv =
		container_of(napi, struct alt_tse_private, napi);
	struct net_device *dev = tse_priv->dev;
	volatile struct alt_sgdma_descriptor *temp_desc_pointer;
	unsigned int desc_status, desc_control;
	int howmany = 0;
	unsigned int rx_bytes;
	struct sk_buff *skb;
	unsigned long flags;
	unsigned int tx_tail;
	unsigned int tx_loop;

	pr_debug("%s: Entering tse_poll with budget %d\n", dev->name, budget);

	temp_desc_pointer =
		&tse_priv->sgdma_rx_desc[tse_priv->rx_sgdma_descriptor_tail];
	desc_status = temp_desc_pointer->descriptor_status;

	/* loop over descriptors until one is not complete */
	while ((desc_status &
		ALT_SGDMA_DESCRIPTOR_STATUS_TERMINATED_BY_EOP_MSK) &&
		(howmany < budget)) {
		pr_debug("%s: NAPI RX loop\n", dev->name);

		if ((desc_status & ALT_SGDMA_DESCRIPTOR_STATUS_ERROR_MSK) &&
			(netif_msg_rx_err(tse_priv)))
				pr_warn("%s :TSE RX Err: Status = 0x%x\n",
					dev->name, desc_status);

		/* get desc SKB */
		skb = tse_priv->rx_skb[tse_priv->rx_sgdma_descriptor_tail];
		tse_priv->rx_skb[tse_priv->rx_sgdma_descriptor_tail] = NULL;

		rx_bytes = temp_desc_pointer->actual_bytes_transferred;

		/* process packet */
		/* Align IP header to 32 bits */
		rx_bytes -= NET_IP_ALIGN;
		skb_reserve(skb, NET_IP_ALIGN);
		skb_put(skb, rx_bytes);
		skb->protocol = eth_type_trans(skb, dev);

		if (netif_receive_skb(skb) == NET_RX_DROP) {
			pr_debug("%s: packet dropped\n", dev->name);
			dev->stats.rx_dropped++;
		}

		/* next descriptor */
		tse_priv->rx_sgdma_descriptor_tail =
			(tse_priv->rx_sgdma_descriptor_tail + 1) &
			ALT_RX_RING_MOD_MASK;

		/* add new desc */
		if ((tse_sgdma_add_buffer(dev)) && (netif_msg_rx_err(tse_priv)))
			pr_warn("%s :ah, something happened, and no desc was added to rx",
				dev->name);

		/* update temp_desc to next desc */
		temp_desc_pointer =
			&tse_priv->sgdma_rx_desc[
			tse_priv->rx_sgdma_descriptor_tail];
		desc_status = temp_desc_pointer->descriptor_status;

		howmany++;
	}

	pr_debug("%s: RX SGDMA STATUS=0x%x, tail=0x%x, head=0x%x\n", dev->name,
		tse_priv->rx_sgdma_dev->status,
		tse_priv->rx_sgdma_descriptor_tail,
		tse_priv->rx_sgdma_descriptor_head);

	/* check sgdma status, and restart as needed */
	if ((tse_priv->rx_sgdma_dev->status &
		ALT_SGDMA_STATUS_CHAIN_COMPLETED_MSK) ||
		!(tse_priv->rx_sgdma_dev->status & ALT_SGDMA_STATUS_BUSY_MSK)) {
		pr_debug("%s: starting with rx_tail = %d and rx_head = %d\n",
			dev->name, tse_priv->rx_sgdma_descriptor_tail,
			tse_priv->rx_sgdma_descriptor_head);

		sgdma_async_read(tse_priv,
			&tse_priv->sgdma_rx_desc[
			tse_priv->rx_sgdma_descriptor_tail]);
	}

	/* now do TX stuff */
	if (tse_priv->tx_sgdma_descriptor_tail !=
		tse_priv->tx_sgdma_descriptor_head) {
		if (spin_trylock_irqsave(&tse_priv->tx_lock, flags)) {
			pr_debug("%s: NAPI TX Section\n", dev->name);

			tx_tail = tse_priv->tx_sgdma_descriptor_tail;
			temp_desc_pointer = &tse_priv->sgdma_tx_desc[tx_tail];
			desc_control = temp_desc_pointer->descriptor_control;

			/* loop over tx desc from tail till head, check for
			 * !hw owned
			 */
			tx_loop = 0;
			while (!(desc_control &
				ALT_SGDMA_DESCRIPTOR_CONTROL_OWNED_BY_HW_MSK) &&
				(tx_tail !=
					tse_priv->tx_sgdma_descriptor_head)){
				dev_kfree_skb(tse_priv->tx_skb[tx_tail]);
				tse_priv->tx_skb[tx_tail] = NULL;

				tx_loop++;
				tx_tail = (tx_tail + 1) & ALT_TX_RING_MOD_MASK;
				temp_desc_pointer =
					&tse_priv->sgdma_tx_desc[tx_tail];
				desc_control =
					temp_desc_pointer->descriptor_control;
			}
			tse_priv->tx_sgdma_descriptor_tail = tx_tail;
			temp_desc_pointer = &tse_priv->sgdma_tx_desc[tx_tail];

			/* check is tx sgdma is running, and if it should be */
			if (!(tse_priv->tx_sgdma_dev->status &
				ALT_SGDMA_STATUS_BUSY_MSK) &&
				(temp_desc_pointer->descriptor_control &
				ALT_SGDMA_DESCRIPTOR_CONTROL_OWNED_BY_HW_MSK)) {
				pr_debug("%s: NAPI starting TX SGDMA with Desc %d\n",
					dev->name, tx_tail);
				/* restart sgdma */
				sgdma_async_write(tse_priv,
					&tse_priv->sgdma_tx_desc[tx_tail]);

			}

			/* restart netif queue if it was stopped */
			if (netif_queue_stopped(dev)) {
				pr_debug("%s: Cleared %d descriptors,tail = %d, head = %d, waking queue\n",
					dev->name, tx_loop, tx_tail,
					tse_priv->tx_sgdma_descriptor_head);
				netif_wake_queue(dev);
			}

			spin_unlock_irqrestore(&tse_priv->tx_lock, flags);
		}
	}

	/* if all packets processed, complete rx, and turn on normal IRQs */
	if (howmany < budget) {
		napi_complete(napi);

		/* turn on desc irqs again */
		tse_priv->rx_sgdma_imask |= ALT_SGDMA_CONTROL_IE_GLOBAL_MSK;
		tse_priv->rx_sgdma_dev->control |=
			ALT_SGDMA_CONTROL_IE_GLOBAL_MSK;
#ifndef NO_TX_IRQ
		tse_priv->tx_sgdma_imask |= ALT_SGDMA_CONTROL_IE_GLOBAL_MSK;
		tse_priv->tx_sgdma_dev->control |=
			ALT_SGDMA_CONTROL_IE_GLOBAL_MSK;
#endif

		pr_debug("%s: NAPI Complete, did %d packets with budget %d\n",
			dev->name, howmany, budget);

		temp_desc_pointer =
			&tse_priv->sgdma_rx_desc[
			tse_priv->rx_sgdma_descriptor_tail];
		desc_status = temp_desc_pointer->descriptor_status;

		/* Check to see if the data at the RX_SGDMA tail is valid */
		if (desc_status &
			ALT_SGDMA_DESCRIPTOR_STATUS_TERMINATED_BY_EOP_MSK)
			napi_reschedule(napi);
	}

	return howmany;
}

/* SG-DMA TX & RX FIFO interrupt routing
* arg1     :irq number
* arg2     :user data passed to isr
*/
static irqreturn_t alt_sgdma_isr(int irq, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct alt_tse_private *tse_priv = netdev_priv(dev);

	pr_debug("%s: TSE IRQ TX head = %d, tail = %d\n", dev->name,
		tse_priv->tx_sgdma_descriptor_head,
		tse_priv->tx_sgdma_descriptor_tail);

	/* turn off desc irqs and enable napi rx */
	if (napi_schedule_prep(&tse_priv->napi)) {
		pr_debug("%s: NAPI starting\n", dev->name);
		tse_priv->rx_sgdma_imask &= ~ALT_SGDMA_CONTROL_IE_GLOBAL_MSK;
		tse_priv->rx_sgdma_dev->control &=
			~ALT_SGDMA_CONTROL_IE_GLOBAL_MSK;
#ifndef NO_TX_IRQ
		tse_priv->tx_sgdma_imask &= ~ALT_SGDMA_CONTROL_IE_GLOBAL_MSK;
		tse_priv->tx_sgdma_dev->control &=
			~ALT_SGDMA_CONTROL_IE_GLOBAL_MSK;
#endif
		__napi_schedule(&tse_priv->napi);
	} else {
		/* if we get here, we received another IRQ while processing
		 * NAPI
		 */
		if (netif_msg_intr(tse_priv))
			pr_warn("%s: TSE IRQ received while IRQs disabled\n",
				dev->name);
	}

	/* reset IRQ */
	if (irq == tse_priv->rx_irq)
		tse_priv->rx_sgdma_dev->control |=
			ALT_SGDMA_CONTROL_CLEAR_INTERRUPT_MSK;
	else if (irq == tse_priv->tx_irq)
		tse_priv->tx_sgdma_dev->control |=
			ALT_SGDMA_CONTROL_CLEAR_INTERRUPT_MSK;

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
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	unsigned int len;
	unsigned int next_head;
	unsigned int next_head_check;
	unsigned int head;
	unsigned int tail;
	unsigned int aligned_tx_buffer;
	unsigned long flags;
	char req_tx_shift_16;

	/* Align frame data to 32bit boundaries */
	if (((unsigned int) skb->data) & 0x2) {
		req_tx_shift_16 = 0x1;
		skb_push(skb, 2);
	} else {
		req_tx_shift_16 = 0x0;
	}

	aligned_tx_buffer = (unsigned int) skb->data;
	len = skb->len;

	flush_dcache_range(aligned_tx_buffer, aligned_tx_buffer + len);

	spin_lock_irqsave(&tse_priv->tx_lock, flags);
	/* get the heads */
	head = tse_priv->tx_sgdma_descriptor_head;
	tail = tse_priv->tx_sgdma_descriptor_tail;
	next_head = (head + 1) & ALT_TX_RING_MOD_MASK;
	next_head_check = (head + 2) & ALT_TX_RING_MOD_MASK;

	pr_debug("%s: head = %d, next_head = %d, tail = %d\n", dev->name, head,
		next_head, tse_priv->tx_sgdma_descriptor_tail);

	/* if next next head is == tail, stop the queue */
	/* next_head = (next_head + 1) & (ALT_TX_RING_MOD_MASK); */
	if (next_head_check == tse_priv->tx_sgdma_descriptor_tail) {
		/* no space in ring, we stop the queue */
		pr_debug("%s: TX next_head not clear, stopping queue, tail = %d, head = %d\n",
			 dev->name, tse_priv->tx_sgdma_descriptor_tail,
			 tse_priv->tx_sgdma_descriptor_head);
		if (!netif_queue_stopped(dev))
			netif_stop_queue(dev);
		napi_schedule(&tse_priv->napi);
	}

	tse_priv->tx_skb[head] = skb;

	/* wait till tx is done, change shift 16 */
	if (req_tx_shift_16 != tse_priv->last_tx_shift_16) {
		pr_debug("%s: tx_shift does not match\n", dev->name);

		while (tse_priv->tx_sgdma_dev->status &
			ALT_SGDMA_STATUS_BUSY_MSK)
			;

		tse_priv->mac_dev->tx_cmd_stat.bits.tx_shift16 =
			req_tx_shift_16 & 0x1;
		tse_priv->last_tx_shift_16 = req_tx_shift_16;
	}

	alt_sgdma_construct_descriptor_burst(
		(volatile struct alt_sgdma_descriptor *)
			&tse_priv->sgdma_tx_desc[head],
		(volatile struct alt_sgdma_descriptor *)
			&tse_priv->sgdma_tx_desc[next_head],
		(unsigned int *)aligned_tx_buffer, /* read addr */
		(unsigned int *)0,
		(len), /* length or EOP */
		0x1, /* gen eop */
		0x0, /* read fixed */
		0x1, /* write fixed or sop */
		0x0, /* read burst */
		0x0, /* write burst */
		0x0  /* channel */
	);

	/* now check is the sgdma is running, if it is then do nothing.
	 *if it is not, start it up with irq's enabled.
	 */

	if (!(tse_priv->tx_sgdma_dev->status & ALT_SGDMA_STATUS_BUSY_MSK)) {
		unsigned int i;

		pr_debug("%s: TX SGDMA not running\n", dev->name);

		/* Walk the descriptors and find the first one OBHW
		 * (Owned By Hardware)
		 */
		for (i = tse_priv->tx_sgdma_descriptor_tail; i != next_head;
			i = (i + 1) & (ALT_TX_RING_MOD_MASK)) {
			if (((&tse_priv->sgdma_tx_desc[i])->descriptor_control &
				ALT_SGDMA_DESCRIPTOR_CONTROL_OWNED_BY_HW_MSK)) {
				sgdma_async_write(tse_priv,
					&tse_priv->sgdma_tx_desc[i]);
				break;
			}
		}
	}

	tse_priv->tx_sgdma_descriptor_head = next_head;

	spin_unlock_irqrestore(&tse_priv->tx_lock, flags);

	return NETDEV_TX_OK;
}

/* Called every time the controller might need to be made
 * aware of new link state.  The PHY code conveys this
 * information through variables in the phydev structure, and this
 * function converts those variables into the appropriate
 * register values, and can bring down the device if needed.
 */
static void altera_tse_adjust_link(struct net_device *dev)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	unsigned long flags;
	struct phy_device *phydev = tse_priv->phydev;
	int new_state = 0;
	unsigned int refvar;

	/* only change config if there is a link */
	spin_lock_irqsave(&tse_priv->tx_lock, flags);
	if (phydev->link) {
		/* read old config */
		refvar = tse_priv->mac_dev->command_config.image;

		/* check duplex */
		if (phydev->duplex != tse_priv->oldduplex) {
			new_state = 1;
			/* not duplex */
			if (!(phydev->duplex))
				refvar |= ALTERA_TSE_CMD_HD_ENA_MSK;
			else
				refvar &= ~ALTERA_TSE_CMD_HD_ENA_MSK;

			pr_debug("%s: Link duplex = 0x%x\n", dev->name,
				phydev->duplex);

			tse_priv->oldduplex = phydev->duplex;
		}

		if (phydev->speed != tse_priv->oldspeed) {
			new_state = 1;
			switch (phydev->speed) {
			case 1000:
				refvar |= ALTERA_TSE_CMD_ETH_SPEED_MSK;
				refvar &= ~ALTERA_TSE_CMD_ENA_10_MSK;
				break;
			case 100:
				refvar &= ~ALTERA_TSE_CMD_ETH_SPEED_MSK;
				refvar &= ~ALTERA_TSE_CMD_ENA_10_MSK;
				break;
			case 10:
				refvar &= ~ALTERA_TSE_CMD_ETH_SPEED_MSK;
				refvar |= ALTERA_TSE_CMD_ENA_10_MSK;
				break;
			default:
				if (netif_msg_link(tse_priv))
					pr_warn("%s: Ack! Speed (%d) is not 10/100/1000!\n",
						dev->name, phydev->speed);
				break;
			}

			tse_priv->oldspeed = phydev->speed;
		}

		tse_priv->mac_dev->command_config.image = refvar;

		netif_carrier_on(tse_priv->dev);

	} else if (tse_priv->oldlink) {
		new_state = 1;
		tse_priv->oldlink = 0;
		tse_priv->oldspeed = 0;
		tse_priv->oldduplex = -1;
		netif_carrier_off(tse_priv->dev);
	}

	if (new_state && netif_msg_link(tse_priv))
		phy_print_status(phydev);

	spin_unlock_irqrestore(&tse_priv->tx_lock, flags);
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
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	struct phy_device *phydev;
	phy_interface_t iface;

	iface = tse_priv->phy_iface;

	tse_priv->oldlink = 0;
	tse_priv->oldspeed = 0;
	tse_priv->oldduplex = -1;

	if (tse_priv->phy_addr != -1) {
		char phy_id[MII_BUS_ID_SIZE];
		char mii_id[MII_BUS_ID_SIZE];

		snprintf(mii_id, MII_BUS_ID_SIZE, "%x", tse_priv->mii_id);
		snprintf(phy_id, MII_BUS_ID_SIZE, PHY_ID_FMT, mii_id,
			tse_priv->phy_addr);

		phydev = phy_connect(dev, phy_id, &altera_tse_adjust_link,
			iface);
		if (IS_ERR(phydev)) {
			dev_err(&dev->dev, "could not attach to PHY\n");
			return PTR_ERR(phydev);
		}
	} else {
		int ret;

		phydev = phy_find_first(tse_priv->mdio);
		if (!phydev) {
			dev_err(&dev->dev, "No PHY found\n");
			return -ENXIO;
		}

		ret = phy_connect_direct(dev, phydev, &altera_tse_adjust_link,
			iface);
		if (ret) {
			dev_err(&dev->dev, "could not attach to PHY\n");
			return ret;
		}
	}

	/* XXX: hard code for now */
	phydev->supported &= PHY_GBIT_FEATURES;
	phydev->advertising &= phydev->supported;

	tse_priv->phydev = phydev;

	return 0;
}

/*******************************************************************************
* MAC setup and control
*	MAC init, and various setting functions
*
*******************************************************************************/
/* Initialize MAC core registers
*  arg1   : 'net_device' structure pointer
*
*/
static int init_mac(struct net_device *dev)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	int counter;
	int dat;

	/* reset the mac */
	tse_priv->mac_dev->command_config.bits.transmit_enable = 0;
	tse_priv->mac_dev->command_config.bits.receive_enable = 0;
	tse_priv->mac_dev->command_config.bits.software_reset = 1;

	counter = 0;
	while (tse_priv->mac_dev->command_config.bits.software_reset) {
		ndelay(100);
		if (counter++ > ALT_TSE_SW_RESET_WATCHDOG_CNTR)
			break;
	}

	if ((counter >= ALT_TSE_SW_RESET_WATCHDOG_CNTR) &&
		(netif_msg_drv(tse_priv)))
		pr_warn("%s: TSE SW reset bit never cleared!\n", dev->name);

	/* default config is enabled HERE including TX and rx */
	dat = tse_priv->mac_dev->command_config.image;

	if ((dat & 0x03) && (netif_msg_drv(tse_priv))) {
		pr_warn("%s: RX/TX not disabled after reset. CMD_CONFIG=0x%08x\n",
			dev->name, dat);
	}

	/* Initialize MAC registers */
	tse_priv->mac_dev->max_frame_length = tse_priv->current_mtu;
	tse_priv->mac_dev->rx_almost_empty_threshold = 8;
	tse_priv->mac_dev->rx_almost_full_threshold = 8;
	tse_priv->mac_dev->tx_almost_empty_threshold = 8;
	tse_priv->mac_dev->tx_almost_full_threshold = 3;
	tse_priv->mac_dev->tx_sel_empty_threshold = tse_priv->tse_tx_depth - 16;
	tse_priv->mac_dev->tx_sel_full_threshold = 0;
	tse_priv->mac_dev->rx_sel_empty_threshold = tse_priv->tse_rx_depth - 16;
	tse_priv->mac_dev->rx_sel_full_threshold = 0;

	/* Enable RX shift 16 for alignment of all received frames on 16-bit
	   start address
	 */
	tse_priv->mac_dev->rx_cmd_stat.bits.rx_shift16 = 1;
	tse_priv->last_rx_shift_16 = 1;
	/* check if the MAC supports the 16-bit shift option at the RX CMD
	   STATUS Register
	 */
	if (tse_priv->mac_dev->rx_cmd_stat.bits.rx_shift16) {
		tse_priv->rx_shift_16_ok = 1;
	} else if (netif_msg_drv(tse_priv)) {
		tse_priv->rx_shift_16_ok = 0;
		pr_warn("%s: Incompatible with RX_CMD_STAT register return RxShift16 value.\n",
			dev->name);
		return -1;
	}

	/* Enable TX shift 16 for alignment of all transmitting frames on 16-bit
	   start address
	 */
	tse_priv->mac_dev->tx_cmd_stat.bits.tx_shift16 = 1;
	tse_priv->mac_dev->tx_cmd_stat.bits.omit_crc = 0;
	tse_priv->last_tx_shift_16 = 1;
	/*
	* check if the MAC supports the 16-bit shift option allowing us
	* to send frames without copying. Used by the send function later.
	*/
	if (tse_priv->mac_dev->tx_cmd_stat.bits.tx_shift16) {
		tse_priv->tx_shift_16_ok = 1;
	} else {
		tse_priv->tx_shift_16_ok = 0;
		pr_warn("%s: Incompatible value with TX_CMD_STAT register return TxShift16 value.\n",
			dev->name);
		return -1;
	}

	/* enable MAC */
	dat = ALTERA_TSE_CMD_TX_ENA_MSK | ALTERA_TSE_CMD_RX_ENA_MSK |

	/* enable pause frame generation */
#if ENABLE_PHY_LOOPBACK
		ALTERA_TSE_CMD_PROMIS_EN_MSK |	/* promiscuous mode */
		ALTERA_TSE_CMD_LOOPBACK_MSK |	/* loopback mode */
#endif
	ALTERA_TSE_CMD_RX_ERR_DISC_MSK; /* automatically discard frames with
					   CRC errors
					 */

	tse_priv->mac_dev->command_config.image = dat;

	pr_debug("%s: MAC post-initialization: CMD_CONFIG=0x%08x\n", dev->name,
		tse_priv->mac_dev->command_config.image);

	/* Set the MAC address */
	tse_priv->mac_dev->mac_addr_0 = ((tse_priv->dev->dev_addr[3]) << 24 |
					(tse_priv->dev->dev_addr[2]) << 16 |
					(tse_priv->dev->dev_addr[1]) << 8 |
					(tse_priv->dev->dev_addr[0]));

	tse_priv->mac_dev->mac_addr_1 = ((tse_priv->dev->dev_addr[5] << 8 |
					(tse_priv->dev->dev_addr[4])) & 0xFFFF);

	/* Set the MAC address */
	tse_priv->mac_dev->supp_mac_addr_0_0 = tse_priv->mac_dev->mac_addr_0;
	tse_priv->mac_dev->supp_mac_addr_0_1 = tse_priv->mac_dev->mac_addr_1;

	/* Set the MAC address */
	tse_priv->mac_dev->supp_mac_addr_1_0 = tse_priv->mac_dev->mac_addr_0;
	tse_priv->mac_dev->supp_mac_addr_1_1 = tse_priv->mac_dev->mac_addr_1;

	/* Set the MAC address */
	tse_priv->mac_dev->supp_mac_addr_2_0 = tse_priv->mac_dev->mac_addr_0;
	tse_priv->mac_dev->supp_mac_addr_2_1 = tse_priv->mac_dev->mac_addr_1;

	/* Set the MAC address */
	tse_priv->mac_dev->supp_mac_addr_3_0 = tse_priv->mac_dev->mac_addr_0;
	tse_priv->mac_dev->supp_mac_addr_3_1 = tse_priv->mac_dev->mac_addr_1;

	/* tse_priv->mac_dev->command_config.bits.src_mac_addr_sel_on_tx=0; */
	return 0;
}

/* Change the MTU
 * The interface is opened whenever 'ifconfig' activates it
 *  arg1   : 'net_device' structure pointer
 *  arg2   : new mtu value
 *  return : 0
 */
/* Jumbo-grams seem to work :-(
 */
#define TSE_MIN_MTU 64
#define TSE_MAX_MTU 16384

static int tse_change_mtu(struct net_device *dev, int new_mtu)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	unsigned int free_loop;
	int ret;

	if (netif_running(dev))
		return -EBUSY;

	if ((new_mtu > (tse_priv->tse_tx_depth * ALT_TSE_MAC_FIFO_WIDTH)) ||
		(new_mtu > (tse_priv->tse_rx_depth * ALT_TSE_MAC_FIFO_WIDTH))) {
		pr_err("Your system doesn't support new MTU size as TX/RX FIFO size is small\n");
		return -EINVAL;
	}

	if (new_mtu < TSE_MIN_MTU || new_mtu > TSE_MAX_MTU)
		return -EINVAL;

	spin_lock(&tse_priv->rx_lock);

	tse_priv->rx_sgdma_dev->control = ALT_SGDMA_CONTROL_SOFTWARERESET_MSK;
	tse_priv->rx_sgdma_dev->control = 0x0;

	tse_priv->current_mtu = new_mtu;
	dev->mtu = new_mtu;
	tse_priv->mac_dev->max_frame_length = tse_priv->current_mtu;

	/* Disable receiver and transmitter  descriptor(SGDMA) */
	for (free_loop = 0; free_loop < ALT_TSE_RX_SGDMA_DESC_COUNT;
		free_loop++) {
		/* Free the original skb */
		if (tse_priv->rx_skb[free_loop] != NULL)
			dev_kfree_skb(tse_priv->rx_skb[free_loop]);
	}

	/* Prepare RX SGDMA to receive packets */
	ret = sgdma_read_init(dev);
	if (ret)
		goto out;

out:
	spin_unlock(&tse_priv->rx_lock);

	return ret;
}

/* Get the current Ethernet statistics.This may be called with the device open
 * or closed.
 * arg1   : net device for which multicasts filter is adjusted
 * return : network statistics structure
 */
static struct net_device_stats *tse_get_statistics(struct net_device *dev)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	struct net_device_stats *net_status = &dev->stats;

	/* total packets received without error*/
	net_status->rx_packets =
	    tse_priv->mac_dev->aFramesReceivedOK +
	    tse_priv->mac_dev->ifInErrors;

	/* total packets received without error*/
	net_status->tx_packets =
	    tse_priv->mac_dev->aFramesTransmittedOK +
	    tse_priv->mac_dev->ifOutErrors;

	/* total bytes received without error  */
	net_status->rx_bytes = tse_priv->mac_dev->aOctetsReceivedOK;

	/* total bytes transmitted without error   */
	net_status->tx_bytes = tse_priv->mac_dev->aOctetsTransmittedOK;

	/* bad received packets  */
	net_status->rx_errors = tse_priv->mac_dev->ifInErrors;

	/* bad Transmitted packets */
	net_status->tx_errors = tse_priv->mac_dev->ifOutErrors;

	/* multicasts packets received */
	net_status->multicast = tse_priv->mac_dev->ifInMulticastPkts;

	return net_status;
}

/* Program multicasts mac addresses into hash look-up table
 * arg1    : net device for which multicasts filter is adjusted
 * arg2    : multicasts address count
 * arg3    : list of multicasts addresses
*/
static void tse_set_hash_table(struct net_device *dev)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	alt_tse_mac *p_mac_base = tse_priv->mac_dev;
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

		p_mac_base->hash_table[hash] = 1;
	}
}

/* Set/Clear multicasts filter
 * arg1    : net device for which multicasts filter is adjusted
 *           multicasts table from the linked list of addresses
 *           associated with this dev structure.
 */
static void tse_set_multicast_list(struct net_device *dev)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	int hash_loop;

	if (dev->flags & IFF_PROMISC) {
		/* Log any net taps */
		tse_priv->mac_dev->command_config.image |=
		    ALTERA_TSE_CMD_PROMIS_EN_MSK;
	} else {
		tse_priv->mac_dev->command_config.image &=
		    ~ALTERA_TSE_CMD_PROMIS_EN_MSK;
	}

	if (dev->flags & IFF_ALLMULTI) {
		for (hash_loop = 0; hash_loop < 64; hash_loop++)
			tse_priv->mac_dev->hash_table[hash_loop] = 1;
	} else {
		for (hash_loop = 0; hash_loop < 64; hash_loop++)
			/* Clear any existing hash entries */
			tse_priv->mac_dev->hash_table[hash_loop] = 0;

		tse_set_hash_table(dev);
	}
}

static void tse_update_mac_addr(struct alt_tse_private *tse_priv, u8 *addr)
{
	/* Set primary MAC address */
	tse_priv->mac_dev->mac_addr_0 = ((addr[3]) << 24
					 | (addr[2]) << 16
					 | (addr[1]) << 8
					 | (addr[0]));

	tse_priv->mac_dev->mac_addr_1 = ((addr[5] << 8
					  | (addr[4])) & 0xFFFF);

	/* Set supplemental the MAC addresses */
	tse_priv->mac_dev->supp_mac_addr_0_0 = tse_priv->mac_dev->mac_addr_0;
	tse_priv->mac_dev->supp_mac_addr_0_1 = tse_priv->mac_dev->mac_addr_1;

	tse_priv->mac_dev->supp_mac_addr_1_0 = tse_priv->mac_dev->mac_addr_0;
	tse_priv->mac_dev->supp_mac_addr_1_1 = tse_priv->mac_dev->mac_addr_1;

	tse_priv->mac_dev->supp_mac_addr_2_0 = tse_priv->mac_dev->mac_addr_0;
	tse_priv->mac_dev->supp_mac_addr_2_1 = tse_priv->mac_dev->mac_addr_1;

	tse_priv->mac_dev->supp_mac_addr_3_0 = tse_priv->mac_dev->mac_addr_0;
	tse_priv->mac_dev->supp_mac_addr_3_1 = tse_priv->mac_dev->mac_addr_1;
}

static int tse_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct alt_tse_private *tse_priv = netdev_priv(dev);

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	dev->addr_assign_type &= ~NET_ADDR_RANDOM;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	tse_update_mac_addr(tse_priv, dev->dev_addr);

	return 0;
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
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	int ret = 0;

	/* start NAPI */
	napi_enable(&tse_priv->napi);

	/* Reset and configure TSE MAC and probe associated PHY */
	if (init_phy(dev)) {
		napi_disable(&tse_priv->napi);
		return -EAGAIN;
	}

	if (init_mac(dev)) {
		napi_disable(&tse_priv->napi);
		return -EAGAIN;
	}

	/* Initialize SGDMA */
	tse_priv->rx_sgdma_descriptor_tail = 0;
	tse_priv->rx_sgdma_descriptor_head = 0;
	tse_priv->tx_sgdma_descriptor_tail = 0;
	tse_priv->tx_sgdma_descriptor_head = 0;

	sgdma_config(tse_priv);

	/* Prepare RX SGDMA to receive packets */
	ret = sgdma_read_init(dev);
	if (ret) {
		napi_disable(&tse_priv->napi);
		return ret;
	}

	/* Register RX SGDMA interrupt */
	ret = request_irq(tse_priv->rx_irq, alt_sgdma_isr, 0, "SGDMA_RX", dev);
	if (ret) {
		dev_err(&dev->dev, "Unable to register RX SGDMA interrupt %d\n",
			tse_priv->rx_irq);
		napi_disable(&tse_priv->napi);
		return ret;
	}

	/* Register TX SGDMA interrupt */
	ret = request_irq(tse_priv->tx_irq, alt_sgdma_isr, 0, "SGDMA_TX", dev);
	if (ret) {
		dev_err(&dev->dev, "Unable to register TX SGDMA interrupt %d\n",
			tse_priv->tx_irq);
		free_irq(tse_priv->rx_irq, dev);
		napi_disable(&tse_priv->napi);
		return ret;
	}

	phy_start(tse_priv->phydev);

	/* Start network queue */
	netif_start_queue(dev);

	return 0;
}

/* Stop TSE MAC interface - this puts the device in an inactive state
 * arg1   : 'net_device' structure pointer
 * return : 0
 */
static int tse_shutdown(struct net_device *dev)
{
	struct alt_tse_private *tse_priv = netdev_priv(dev);
	unsigned int free_loop;
	int counter;

	napi_disable(&tse_priv->napi);

	/* Free interrupt handler */
	free_irq(tse_priv->rx_irq, dev);
	free_irq(tse_priv->tx_irq, dev);

	/* disable and reset the MAC, empties fifo */
	tse_priv->mac_dev->command_config.bits.software_reset = 1;

	counter = 0;
	while (tse_priv->mac_dev->command_config.bits.software_reset) {
		ndelay(100);
		if (counter++ > ALT_TSE_SW_RESET_WATCHDOG_CNTR)
			break;
	}

	if ((counter >= ALT_TSE_SW_RESET_WATCHDOG_CNTR) &&
		netif_msg_ifdown(tse_priv)) {
		pr_warn("%s: SHUTDOWN: TSE SW reset bit never cleared!\n",
			dev->name);
	}

	spin_lock(&tse_priv->rx_lock);
	spin_lock(&tse_priv->tx_lock);

	/* Need to reset/turn off sgdmas */
	sgdma_config(tse_priv);

	/* Disable receiver and transmitter  descriptor(SGDAM) */
	for (free_loop = 0; free_loop < ALT_TSE_TX_SGDMA_DESC_COUNT;
		free_loop++) {
		/* Free the original skb */
		if (tse_priv->tx_skb[free_loop] != NULL) {
			dev_kfree_skb(tse_priv->tx_skb[free_loop]);
			tse_priv->tx_skb[free_loop] = NULL;
		}
	}

	/* Disable receiver and transmitter  descriptor(SGDMA) */
	for (free_loop = 0; free_loop < ALT_TSE_RX_SGDMA_DESC_COUNT;
		free_loop++) {
		/* Free the original skb */
		if (tse_priv->rx_skb[free_loop] != NULL) {
			dev_kfree_skb(tse_priv->rx_skb[free_loop]);
			tse_priv->rx_skb[free_loop] = NULL;
		}
	}

	spin_unlock(&tse_priv->rx_lock);
	spin_unlock(&tse_priv->tx_lock);

	phy_disconnect(tse_priv->phydev);
	tse_priv->phydev = NULL;

	netif_stop_queue(dev);

	return 0;
}

static const struct net_device_ops altera_tse_netdev_ops = {
	.ndo_open		= tse_open,
	.ndo_stop		= tse_shutdown,
	.ndo_start_xmit		= tse_start_xmit,
	.ndo_get_stats		= tse_get_statistics,
	.ndo_set_mac_address	= tse_set_mac_address,
	.ndo_set_rx_mode	= tse_set_multicast_list,
	.ndo_change_mtu		= tse_change_mtu,
	.ndo_validate_addr	= eth_validate_addr,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= tse_net_poll_controller
#endif
};

static void altera_tse_sgdma_setup(struct alt_tse_private *tse_priv)
{
	/* Set initial SGDMA descriptor address */
	tse_priv->desc =
	    (volatile struct alt_sgdma_descriptor *)tse_priv->desc_mem_base;
	tse_priv->sgdma_tx_desc =
	    (volatile struct alt_sgdma_descriptor *)tse_priv->desc;
	tse_priv->sgdma_rx_desc =
	    (volatile struct alt_sgdma_descriptor *)&tse_priv->
	    desc[ALT_TSE_TX_SGDMA_DESC_COUNT];

	tse_priv->rx_sgdma_imask = ALT_SGDMA_CONTROL_IE_CHAIN_COMPLETED_MSK
				| ALT_SGDMA_STATUS_DESC_COMPLETED_MSK
				| ALT_SGDMA_CONTROL_IE_GLOBAL_MSK;
#ifndef NO_TX_IRQ
	tse_priv->tx_sgdma_imask = ALT_SGDMA_CONTROL_IE_CHAIN_COMPLETED_MSK
				| ALT_SGDMA_CONTROL_IE_GLOBAL_MSK;
#else
	tse_priv->tx_sgdma_imask = 0;
#endif
}

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
	struct net_device *dev;
	int ret = -ENODEV;
	struct resource *res, *regs, *sgdma_rx, *sgdma_tx, *desc_mem;
	struct alt_tse_private *tse_priv;
	struct mii_bus *mdio;
	int i, len;
	const unsigned char *macaddr;

	dev = alloc_etherdev(sizeof(struct alt_tse_private));
	if (!dev) {
		dev_err(&pdev->dev, "Could not allocate network device\n");
		return -ENODEV;
	}

	netdev_boot_setup_check(dev);
	tse_priv = netdev_priv(dev);
	tse_priv->dev = dev;

	SET_NETDEV_DEV(dev, &pdev->dev);
	platform_set_drvdata(pdev, dev);

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

	tse_priv->mac_dev = devm_ioremap_nocache(&pdev->dev, regs->start,
					resource_size(regs));
	if (!tse_priv->mac_dev) {
		dev_err(&pdev->dev, "cannot remap MAC register area\n");
		ret = -ENOMEM;
		goto out_free;
	}

	/* RX SGDMA register area */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&pdev->dev, "cannot obtain SGDMA RX register area\n");
		ret = -ENODEV;
		goto out_free;
	}

	sgdma_rx = devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), DRV_NAME);
	if (!sgdma_rx) {
		dev_err(&pdev->dev, "cannot request SGDMA RX register area\n");
		ret = -EBUSY;
		goto out_free;
	}

	tse_priv->rx_sgdma_dev = devm_ioremap_nocache(&pdev->dev,
					sgdma_rx->start,
					resource_size(sgdma_rx));
	if (!tse_priv->rx_sgdma_dev) {
		dev_err(&pdev->dev, "cannot remap SGDMA RX register area\n");
		ret = -ENOMEM;
		goto out_free;
	}

	/* TX SGDMA register area */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (!res) {
		dev_err(&pdev->dev, "cannot obtain SGDMA TX register area\n");
		ret = -ENODEV;
		goto out_free;
	}

	sgdma_tx = devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), DRV_NAME);
	if (!sgdma_tx) {
		dev_err(&pdev->dev, "cannot request SGDMA TX register area\n");
		ret = -EBUSY;
		goto out_free;
	}

	tse_priv->tx_sgdma_dev = devm_ioremap_nocache(&pdev->dev,
					sgdma_tx->start,
					resource_size(sgdma_tx));
	if (!tse_priv->tx_sgdma_dev) {
		dev_err(&pdev->dev, "cannot remap SGDMA TX register area\n");
		ret = -ENOMEM;
		goto out_free;
	}

	/* descriptor memory area */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 3);
	if (!res) {
		dev_err(&pdev->dev, "cannot obtain descriptor memory area\n");
		ret = -ENODEV;
		goto out_free;
	}

	desc_mem = devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), DRV_NAME);
	if (!desc_mem) {
		dev_err(&pdev->dev, "cannot request descriptor memory area\n");
		ret = -EBUSY;
		goto out_free;
	}

	tse_priv->desc_mem_base = devm_ioremap_nocache(&pdev->dev,
					desc_mem->start,
					resource_size(desc_mem));
	if (!tse_priv->desc_mem_base) {
		dev_err(&pdev->dev, "cannot remap descriptor memory area\n");
		ret = -ENOMEM;
		goto out_free;
	}

	/* RX SGDMA IRQ */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot obtain SGDMA RX IRQ\n");
		ret = -ENODEV;
		goto out_free;
	}

	tse_priv->rx_irq = res->start;

	/* TX SGDMA IRQ  */
	res = platform_get_resource(pdev, IORESOURCE_IRQ, 1);
	if (!res) {
		dev_err(&pdev->dev, "cannot obtain SGDMA TX IRQ\n");
		ret = -ENODEV;
		goto out_free;
	}

	tse_priv->tx_irq = res->start;

	/* get RX FIFO depth from device tree (assuming FIFO width = 4) */
	ret = altera_tse_get_of_prop(pdev, "ALTR,rx-fifo-depth",
		&tse_priv->tse_rx_depth);
	if (ret)
		goto out_free;

	/* get TX FIFO depth from device tree (assuming FIFO width = 4) */
	ret = altera_tse_get_of_prop(pdev, "ALTR,tx-fifo-depth",
		&tse_priv->tse_tx_depth);
	if (ret)
		goto out_free;

	/* get max frame size from device tree */
	ret = altera_tse_get_of_prop(pdev, "max-frame-size",
		&tse_priv->current_mtu);
	if (ret)
		goto out_free;

	/* get default MAC address from device tree */
	macaddr = of_get_property(pdev->dev.of_node, "local-mac-address", &len);
	if (macaddr && len == ETH_ALEN)
		memcpy(dev->dev_addr, macaddr, ETH_ALEN);

	/* If we didn't get a valid address, generate a random one */
	if (!is_valid_ether_addr(dev->dev_addr))
		eth_hw_addr_random(dev);

	/* Write it to the MAC address register */
	tse_update_mac_addr(tse_priv, dev->dev_addr);

	/* get MII ID from device tree */
	ret = altera_tse_get_of_prop(pdev, "ALTR,mii-id", &tse_priv->mii_id);
	if (ret)
		goto out_free;

	ret = altera_tse_get_phy_iface_prop(pdev, &tse_priv->phy_iface);
	if (ret == -ENOENT) {
		/* backward compatability, assume RGMII */
		dev_warn(&pdev->dev,
			 "cannot obtain PHY interface mode, assuming RGMII\n");
		tse_priv->phy_iface = PHY_INTERFACE_MODE_RGMII;
	} else if (ret) {
		dev_err(&pdev->dev, "unknown PHY interface mode\n");
		goto out_free;
	}

	/* try to get PHY address from device tree, use PHY autodetection if
	 * no valid address is given
	 */
	ret = altera_tse_get_of_prop(pdev, "ALTR,phy-addr",
			&tse_priv->phy_addr);
	if (ret)
		tse_priv->phy_addr = -1;

	mdio = mdiobus_alloc();
	if (!mdio) {
		ret = -ENOMEM;
		goto out_free;
	}

	mdio->name = "altera_tse-mdio";
	mdio->read = &altera_tse_mdio_read;
	mdio->write = &altera_tse_mdio_write;
	snprintf(mdio->id, MII_BUS_ID_SIZE, "%u", tse_priv->mii_id);

	mdio->irq = kcalloc(PHY_MAX_ADDR, sizeof(int), GFP_KERNEL);
	if (!mdio->irq) {
		ret = -ENOMEM;
		goto out_free_mdio;
	}

	for (i = 0; i < PHY_MAX_ADDR; i++)
		mdio->irq[i] = PHY_POLL;

	mdio->priv = (void *) tse_priv->mac_dev;

	ret = mdiobus_register(mdio);
	if (ret) {
		dev_err(&pdev->dev, "cannot register MDIO bus\n");
		goto out_free_mdio_irq;
	}

	tse_priv->mdio = mdio;

	/* initialize SGDMAs */
	altera_tse_sgdma_setup(tse_priv);

	/* initialize netdev */
	ether_setup(dev);
	dev->base_addr = (unsigned long) tse_priv->mac_dev;
	dev->netdev_ops = &altera_tse_netdev_ops;
	tse_set_ethtool_ops(dev);

	/* setup NAPI interface */
	netif_napi_add(dev, &tse_priv->napi, tse_poll,
		ALT_TSE_RX_SGDMA_DESC_COUNT);

	spin_lock_init(&tse_priv->rx_lock);
	spin_lock_init(&tse_priv->tx_lock);

	ret = register_netdev(dev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register TSE net device\n");
		goto out_free_mdio_irq;
	}

	pr_info("%s: Altera TSE MAC at 0x%08lx irq %d/%d\n", dev->name,
			dev->base_addr, tse_priv->rx_irq, tse_priv->tx_irq);

	return 0;

out_free_mdio_irq:
	kfree(mdio->irq);
out_free_mdio:
	mdiobus_free(mdio);
out_free:
	free_netdev(dev);
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

	if (ndev) {
		unregister_netdev(ndev);
		free_netdev(ndev);
	}

	if (priv->mdio) {
		mdiobus_unregister(priv->mdio);
		kfree(priv->mdio->irq);
		mdiobus_free(priv->mdio);
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
