/*
 * Agere Systems Inc.
 * 10/100/1000 Base-T Ethernet Driver for the ET1301 and ET131x series MACs
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *   http://www.agere.com
 *
 *------------------------------------------------------------------------------
 *
 * et1310_tx.c - Routines used to perform data transmission.
 *
 *------------------------------------------------------------------------------
 *
 * SOFTWARE LICENSE
 *
 * This software is provided subject to the following terms and conditions,
 * which you should read carefully before using the software.  Using this
 * software indicates your acceptance of these terms and conditions.  If you do
 * not agree with these terms and conditions, do not use the software.
 *
 * Copyright © 2005 Agere Systems Inc.
 * All rights reserved.
 *
 * Redistribution and use in source or binary forms, with or without
 * modifications, are permitted provided that the following conditions are met:
 *
 * . Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following Disclaimer as comments in the code as
 *    well as in the documentation and/or other materials provided with the
 *    distribution.
 *
 * . Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following Disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * . Neither the name of Agere Systems Inc. nor the names of the contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * Disclaimer
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, INFRINGEMENT AND THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  ANY
 * USE, MODIFICATION OR DISTRIBUTION OF THIS SOFTWARE IS SOLELY AT THE USERS OWN
 * RISK. IN NO EVENT SHALL AGERE SYSTEMS INC. OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, INCLUDING, BUT NOT LIMITED TO, CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 */

#include "et131x_version.h"
#include "et131x_defs.h"

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/bitops.h>
#include <asm/system.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>
#include <linux/ioport.h>

#include "et1310_phy.h"
#include "et1310_pm.h"
#include "et1310_jagcore.h"

#include "et131x_adapter.h"
#include "et131x_initpci.h"
#include "et131x_isr.h"

#include "et1310_tx.h"


static inline void et131x_free_send_packet(struct et131x_adapter *etdev,
					   struct tcb *tcb);
static int et131x_send_packet(struct sk_buff *skb,
			      struct et131x_adapter *etdev);
static int nic_send_packet(struct et131x_adapter *etdev, struct tcb *tcb);

/**
 * et131x_tx_dma_memory_alloc
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success and errno on failure (as defined in errno.h).
 *
 * Allocates memory that will be visible both to the device and to the CPU.
 * The OS will pass us packets, pointers to which we will insert in the Tx
 * Descriptor queue. The device will read this queue to find the packets in
 * memory. The device will update the "status" in memory each time it xmits a
 * packet.
 */
int et131x_tx_dma_memory_alloc(struct et131x_adapter *adapter)
{
	int desc_size = 0;
	struct tx_ring *tx_ring = &adapter->tx_ring;

	/* Allocate memory for the TCB's (Transmit Control Block) */
	adapter->tx_ring.tcb_ring = (struct tcb *)
		kcalloc(NUM_TCB, sizeof(struct tcb), GFP_ATOMIC | GFP_DMA);
	if (!adapter->tx_ring.tcb_ring) {
		dev_err(&adapter->pdev->dev, "Cannot alloc memory for TCBs\n");
		return -ENOMEM;
	}

	/* Allocate enough memory for the Tx descriptor ring, and allocate
	 * some extra so that the ring can be aligned on a 4k boundary.
	 */
	desc_size = (sizeof(struct tx_desc) * NUM_DESC_PER_RING_TX) + 4096 - 1;
	tx_ring->tx_desc_ring =
	    (struct tx_desc *) pci_alloc_consistent(adapter->pdev, desc_size,
						    &tx_ring->tx_desc_ring_pa);
	if (!adapter->tx_ring.tx_desc_ring) {
		dev_err(&adapter->pdev->dev,
					"Cannot alloc memory for Tx Ring\n");
		return -ENOMEM;
	}

	/* Save physical address
	 *
	 * NOTE: pci_alloc_consistent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here before
	 * storing the adjusted address.
	 */
	/* Allocate memory for the Tx status block */
	tx_ring->tx_status = pci_alloc_consistent(adapter->pdev,
						    sizeof(u32),
						    &tx_ring->tx_status_pa);
	if (!adapter->tx_ring.tx_status_pa) {
		dev_err(&adapter->pdev->dev,
				  "Cannot alloc memory for Tx status block\n");
		return -ENOMEM;
	}
	return 0;
}

/**
 * et131x_tx_dma_memory_free - Free all memory allocated within this module
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success and errno on failure (as defined in errno.h).
 */
void et131x_tx_dma_memory_free(struct et131x_adapter *adapter)
{
	int desc_size = 0;

	if (adapter->tx_ring.tx_desc_ring) {
		/* Free memory relating to Tx rings here */
		desc_size = (sizeof(struct tx_desc) * NUM_DESC_PER_RING_TX)
								+ 4096 - 1;
		pci_free_consistent(adapter->pdev,
				    desc_size,
				    adapter->tx_ring.tx_desc_ring,
				    adapter->tx_ring.tx_desc_ring_pa);
		adapter->tx_ring.tx_desc_ring = NULL;
	}

	/* Free memory for the Tx status block */
	if (adapter->tx_ring.tx_status) {
		pci_free_consistent(adapter->pdev,
				    sizeof(u32),
				    adapter->tx_ring.tx_status,
				    adapter->tx_ring.tx_status_pa);

		adapter->tx_ring.tx_status = NULL;
	}
	/* Free the memory for the tcb structures */
	kfree(adapter->tx_ring.tcb_ring);
}

/**
 * ConfigTxDmaRegs - Set up the tx dma section of the JAGCore.
 * @etdev: pointer to our private adapter structure
 *
 * Configure the transmit engine with the ring buffers we have created
 * and prepare it for use.
 */
void ConfigTxDmaRegs(struct et131x_adapter *etdev)
{
	struct txdma_regs __iomem *txdma = &etdev->regs->txdma;

	/* Load the hardware with the start of the transmit descriptor ring. */
	writel((u32) ((u64)etdev->tx_ring.tx_desc_ring_pa >> 32),
	       &txdma->pr_base_hi);
	writel((u32) etdev->tx_ring.tx_desc_ring_pa,
	       &txdma->pr_base_lo);

	/* Initialise the transmit DMA engine */
	writel(NUM_DESC_PER_RING_TX - 1, &txdma->pr_num_des);

	/* Load the completion writeback physical address */
	writel((u32)((u64)etdev->tx_ring.tx_status_pa >> 32),
						&txdma->dma_wb_base_hi);
	writel((u32)etdev->tx_ring.tx_status_pa, &txdma->dma_wb_base_lo);

	*etdev->tx_ring.tx_status = 0;

	writel(0, &txdma->service_request);
	etdev->tx_ring.send_idx = 0;
}

/**
 * et131x_tx_dma_disable - Stop of Tx_DMA on the ET1310
 * @etdev: pointer to our adapter structure
 */
void et131x_tx_dma_disable(struct et131x_adapter *etdev)
{
	/* Setup the tramsmit dma configuration register */
	writel(ET_TXDMA_CSR_HALT|ET_TXDMA_SNGL_EPKT,
					&etdev->regs->txdma.csr);
}

/**
 * et131x_tx_dma_enable - re-start of Tx_DMA on the ET1310.
 * @etdev: pointer to our adapter structure
 *
 * Mainly used after a return to the D0 (full-power) state from a lower state.
 */
void et131x_tx_dma_enable(struct et131x_adapter *etdev)
{
	/* Setup the transmit dma configuration register for normal
	 * operation
	 */
	writel(ET_TXDMA_SNGL_EPKT|(PARM_DMA_CACHE_DEF << ET_TXDMA_CACHE_SHIFT),
					&etdev->regs->txdma.csr);
}

/**
 * et131x_init_send - Initialize send data structures
 * @adapter: pointer to our private adapter structure
 */
void et131x_init_send(struct et131x_adapter *adapter)
{
	struct tcb *tcb;
	u32 ct;
	struct tx_ring *tx_ring;

	/* Setup some convenience pointers */
	tx_ring = &adapter->tx_ring;
	tcb = adapter->tx_ring.tcb_ring;

	tx_ring->tcb_qhead = tcb;

	memset(tcb, 0, sizeof(struct tcb) * NUM_TCB);

	/* Go through and set up each TCB */
	for (ct = 0; ct++ < NUM_TCB; tcb++)
		/* Set the link pointer in HW TCB to the next TCB in the
		 * chain
		 */
		tcb->next = tcb + 1;

	/* Set the  tail pointer */
	tcb--;
	tx_ring->tcb_qtail = tcb;
	tcb->next = NULL;
	/* Curr send queue should now be empty */
	tx_ring->send_head = NULL;
	tx_ring->send_tail = NULL;
}

/**
 * et131x_send_packets - This function is called by the OS to send packets
 * @skb: the packet(s) to send
 * @netdev:device on which to TX the above packet(s)
 *
 * Return 0 in almost all cases; non-zero value in extreme hard failure only
 */
int et131x_send_packets(struct sk_buff *skb, struct net_device *netdev)
{
	int status = 0;
	struct et131x_adapter *etdev = NULL;

	etdev = netdev_priv(netdev);

	/* Send these packets
	 *
	 * NOTE: The Linux Tx entry point is only given one packet at a time
	 * to Tx, so the PacketCount and it's array used makes no sense here
	 */

	/* TCB is not available */
	if (etdev->tx_ring.used >= NUM_TCB) {
		/* NOTE: If there's an error on send, no need to queue the
		 * packet under Linux; if we just send an error up to the
		 * netif layer, it will resend the skb to us.
		 */
		status = -ENOMEM;
	} else {
		/* We need to see if the link is up; if it's not, make the
		 * netif layer think we're good and drop the packet
		 */
		if ((etdev->Flags & fMP_ADAPTER_FAIL_SEND_MASK) ||
					!netif_carrier_ok(netdev)) {
			dev_kfree_skb_any(skb);
			skb = NULL;

			etdev->net_stats.tx_dropped++;
		} else {
			status = et131x_send_packet(skb, etdev);
			if (status != 0 && status != -ENOMEM) {
				/* On any other error, make netif think we're
				 * OK and drop the packet
				 */
				dev_kfree_skb_any(skb);
				skb = NULL;
				etdev->net_stats.tx_dropped++;
			}
		}
	}
	return status;
}

/**
 * et131x_send_packet - Do the work to send a packet
 * @skb: the packet(s) to send
 * @etdev: a pointer to the device's private adapter structure
 *
 * Return 0 in almost all cases; non-zero value in extreme hard failure only.
 *
 * Assumption: Send spinlock has been acquired
 */
static int et131x_send_packet(struct sk_buff *skb,
			      struct et131x_adapter *etdev)
{
	int status;
	struct tcb *tcb = NULL;
	u16 *shbufva;
	unsigned long flags;

	/* All packets must have at least a MAC address and a protocol type */
	if (skb->len < ETH_HLEN)
		return -EIO;

	/* Get a TCB for this packet */
	spin_lock_irqsave(&etdev->TCBReadyQLock, flags);

	tcb = etdev->tx_ring.tcb_qhead;

	if (tcb == NULL) {
		spin_unlock_irqrestore(&etdev->TCBReadyQLock, flags);
		return -ENOMEM;
	}

	etdev->tx_ring.tcb_qhead = tcb->next;

	if (etdev->tx_ring.tcb_qhead == NULL)
		etdev->tx_ring.tcb_qtail = NULL;

	spin_unlock_irqrestore(&etdev->TCBReadyQLock, flags);

	tcb->skb = skb;

	if (skb->data != NULL && skb->len - skb->data_len >= 6) {
		shbufva = (u16 *) skb->data;

		if ((shbufva[0] == 0xffff) &&
		    (shbufva[1] == 0xffff) && (shbufva[2] == 0xffff)) {
			tcb->flags |= fMP_DEST_BROAD;
		} else if ((shbufva[0] & 0x3) == 0x0001) {
			tcb->flags |=  fMP_DEST_MULTI;
		}
	}

	tcb->next = NULL;

	/* Call the NIC specific send handler. */
	status = nic_send_packet(etdev, tcb);

	if (status != 0) {
		spin_lock_irqsave(&etdev->TCBReadyQLock, flags);

		if (etdev->tx_ring.tcb_qtail)
			etdev->tx_ring.tcb_qtail->next = tcb;
		else
			/* Apparently ready Q is empty. */
			etdev->tx_ring.tcb_qhead = tcb;

		etdev->tx_ring.tcb_qtail = tcb;
		spin_unlock_irqrestore(&etdev->TCBReadyQLock, flags);
		return status;
	}
	WARN_ON(etdev->tx_ring.used > NUM_TCB);
	return 0;
}

/**
 * nic_send_packet - NIC specific send handler for version B silicon.
 * @etdev: pointer to our adapter
 * @tcb: pointer to struct tcb
 *
 * Returns 0 or errno.
 */
static int nic_send_packet(struct et131x_adapter *etdev, struct tcb *tcb)
{
	u32 i;
	struct tx_desc desc[24];	/* 24 x 16 byte */
	u32 frag = 0;
	u32 thiscopy, remainder;
	struct sk_buff *skb = tcb->skb;
	u32 nr_frags = skb_shinfo(skb)->nr_frags + 1;
	struct skb_frag_struct *frags = &skb_shinfo(skb)->frags[0];
	unsigned long flags;

	/* Part of the optimizations of this send routine restrict us to
	 * sending 24 fragments at a pass.  In practice we should never see
	 * more than 5 fragments.
	 *
	 * NOTE: The older version of this function (below) can handle any
	 * number of fragments. If needed, we can call this function,
	 * although it is less efficient.
	 */
	if (nr_frags > 23)
		return -EIO;

	memset(desc, 0, sizeof(struct tx_desc) * (nr_frags + 1));

	for (i = 0; i < nr_frags; i++) {
		/* If there is something in this element, lets get a
		 * descriptor from the ring and get the necessary data
		 */
		if (i == 0) {
			/* If the fragments are smaller than a standard MTU,
			 * then map them to a single descriptor in the Tx
			 * Desc ring. However, if they're larger, as is
			 * possible with support for jumbo packets, then
			 * split them each across 2 descriptors.
			 *
			 * This will work until we determine why the hardware
			 * doesn't seem to like large fragments.
			 */
			if ((skb->len - skb->data_len) <= 1514) {
				desc[frag].addr_hi = 0;
				/* Low 16bits are length, high is vlan and
				   unused currently so zero */
				desc[frag].len_vlan =
					skb->len - skb->data_len;

				/* NOTE: Here, the dma_addr_t returned from
				 * pci_map_single() is implicitly cast as a
				 * u32. Although dma_addr_t can be
				 * 64-bit, the address returned by
				 * pci_map_single() is always 32-bit
				 * addressable (as defined by the pci/dma
				 * subsystem)
				 */
				desc[frag++].addr_lo =
				    pci_map_single(etdev->pdev,
						   skb->data,
						   skb->len -
						   skb->data_len,
						   PCI_DMA_TODEVICE);
			} else {
				desc[frag].addr_hi = 0;
				desc[frag].len_vlan =
				    (skb->len - skb->data_len) / 2;

				/* NOTE: Here, the dma_addr_t returned from
				 * pci_map_single() is implicitly cast as a
				 * u32. Although dma_addr_t can be
				 * 64-bit, the address returned by
				 * pci_map_single() is always 32-bit
				 * addressable (as defined by the pci/dma
				 * subsystem)
				 */
				desc[frag++].addr_lo =
				    pci_map_single(etdev->pdev,
						   skb->data,
						   ((skb->len -
						     skb->data_len) / 2),
						   PCI_DMA_TODEVICE);
				desc[frag].addr_hi = 0;

				desc[frag].len_vlan =
				    (skb->len - skb->data_len) / 2;

				/* NOTE: Here, the dma_addr_t returned from
				 * pci_map_single() is implicitly cast as a
				 * u32. Although dma_addr_t can be
				 * 64-bit, the address returned by
				 * pci_map_single() is always 32-bit
				 * addressable (as defined by the pci/dma
				 * subsystem)
				 */
				desc[frag++].addr_lo =
				    pci_map_single(etdev->pdev,
						   skb->data +
						   ((skb->len -
						     skb->data_len) / 2),
						   ((skb->len -
						     skb->data_len) / 2),
						   PCI_DMA_TODEVICE);
			}
		} else {
			desc[frag].addr_hi = 0;
			desc[frag].len_vlan =
					frags[i - 1].size;

			/* NOTE: Here, the dma_addr_t returned from
			 * pci_map_page() is implicitly cast as a u32.
			 * Although dma_addr_t can be 64-bit, the address
			 * returned by pci_map_page() is always 32-bit
			 * addressable (as defined by the pci/dma subsystem)
			 */
			desc[frag++].addr_lo =
			    pci_map_page(etdev->pdev,
					 frags[i - 1].page,
					 frags[i - 1].page_offset,
					 frags[i - 1].size,
					 PCI_DMA_TODEVICE);
		}
	}

	if (frag == 0)
		return -EIO;

	if (etdev->linkspeed == TRUEPHY_SPEED_1000MBPS) {
		if (++etdev->tx_ring.since_irq == PARM_TX_NUM_BUFS_DEF) {
			/* Last element & Interrupt flag */
			desc[frag - 1].flags = 0x5;
			etdev->tx_ring.since_irq = 0;
		} else { /* Last element */
			desc[frag - 1].flags = 0x1;
		}
	} else
		desc[frag - 1].flags = 0x5;

	desc[0].flags |= 2;	/* First element flag */

	tcb->index_start = etdev->tx_ring.send_idx;
	tcb->stale = 0;

	spin_lock_irqsave(&etdev->SendHWLock, flags);

	thiscopy = NUM_DESC_PER_RING_TX -
				INDEX10(etdev->tx_ring.send_idx);

	if (thiscopy >= frag) {
		remainder = 0;
		thiscopy = frag;
	} else {
		remainder = frag - thiscopy;
	}

	memcpy(etdev->tx_ring.tx_desc_ring +
	       INDEX10(etdev->tx_ring.send_idx), desc,
	       sizeof(struct tx_desc) * thiscopy);

	add_10bit(&etdev->tx_ring.send_idx, thiscopy);

	if (INDEX10(etdev->tx_ring.send_idx) == 0 ||
		    INDEX10(etdev->tx_ring.send_idx) == NUM_DESC_PER_RING_TX) {
		etdev->tx_ring.send_idx &= ~ET_DMA10_MASK;
		etdev->tx_ring.send_idx ^= ET_DMA10_WRAP;
	}

	if (remainder) {
		memcpy(etdev->tx_ring.tx_desc_ring,
		       desc + thiscopy,
		       sizeof(struct tx_desc) * remainder);

		add_10bit(&etdev->tx_ring.send_idx, remainder);
	}

	if (INDEX10(etdev->tx_ring.send_idx) == 0) {
		if (etdev->tx_ring.send_idx)
			tcb->index = NUM_DESC_PER_RING_TX - 1;
		else
			tcb->index = ET_DMA10_WRAP|(NUM_DESC_PER_RING_TX - 1);
	} else
		tcb->index = etdev->tx_ring.send_idx - 1;

	spin_lock(&etdev->TCBSendQLock);

	if (etdev->tx_ring.send_tail)
		etdev->tx_ring.send_tail->next = tcb;
	else
		etdev->tx_ring.send_head = tcb;

	etdev->tx_ring.send_tail = tcb;

	WARN_ON(tcb->next != NULL);

	etdev->tx_ring.used++;

	spin_unlock(&etdev->TCBSendQLock);

	/* Write the new write pointer back to the device. */
	writel(etdev->tx_ring.send_idx,
	       &etdev->regs->txdma.service_request);

	/* For Gig only, we use Tx Interrupt coalescing.  Enable the software
	 * timer to wake us up if this packet isn't followed by N more.
	 */
	if (etdev->linkspeed == TRUEPHY_SPEED_1000MBPS) {
		writel(PARM_TX_TIME_INT_DEF * NANO_IN_A_MICRO,
		       &etdev->regs->global.watchdog_timer);
	}
	spin_unlock_irqrestore(&etdev->SendHWLock, flags);

	return 0;
}


/**
 * et131x_free_send_packet - Recycle a struct tcb
 * @etdev: pointer to our adapter
 * @tcb: pointer to struct tcb
 *
 * Complete the packet if necessary
 * Assumption - Send spinlock has been acquired
 */
inline void et131x_free_send_packet(struct et131x_adapter *etdev,
						struct tcb *tcb)
{
	unsigned long flags;
	struct tx_desc *desc = NULL;
	struct net_device_stats *stats = &etdev->net_stats;

	if (tcb->flags & fMP_DEST_BROAD)
		atomic_inc(&etdev->Stats.brdcstxmt);
	else if (tcb->flags & fMP_DEST_MULTI)
		atomic_inc(&etdev->Stats.multixmt);
	else
		atomic_inc(&etdev->Stats.unixmt);

	if (tcb->skb) {
		stats->tx_bytes += tcb->skb->len;

		/* Iterate through the TX descriptors on the ring
		 * corresponding to this packet and umap the fragments
		 * they point to
		 */
		do {
			desc = (struct tx_desc *)(etdev->tx_ring.tx_desc_ring +
						INDEX10(tcb->index_start));

			pci_unmap_single(etdev->pdev,
					 desc->addr_lo,
					 desc->len_vlan, PCI_DMA_TODEVICE);

			add_10bit(&tcb->index_start, 1);
			if (INDEX10(tcb->index_start) >=
							NUM_DESC_PER_RING_TX) {
				tcb->index_start &= ~ET_DMA10_MASK;
				tcb->index_start ^= ET_DMA10_WRAP;
			}
		} while (desc != (etdev->tx_ring.tx_desc_ring +
				INDEX10(tcb->index)));

		dev_kfree_skb_any(tcb->skb);
	}

	memset(tcb, 0, sizeof(struct tcb));

	/* Add the TCB to the Ready Q */
	spin_lock_irqsave(&etdev->TCBReadyQLock, flags);

	etdev->Stats.opackets++;

	if (etdev->tx_ring.tcb_qtail)
		etdev->tx_ring.tcb_qtail->next = tcb;
	else
		/* Apparently ready Q is empty. */
		etdev->tx_ring.tcb_qhead = tcb;

	etdev->tx_ring.tcb_qtail = tcb;

	spin_unlock_irqrestore(&etdev->TCBReadyQLock, flags);
	WARN_ON(etdev->tx_ring.used < 0);
}

/**
 * et131x_free_busy_send_packets - Free and complete the stopped active sends
 * @etdev: pointer to our adapter
 *
 * Assumption - Send spinlock has been acquired
 */
void et131x_free_busy_send_packets(struct et131x_adapter *etdev)
{
	struct tcb *tcb;
	unsigned long flags;
	u32 freed = 0;

	/* Any packets being sent? Check the first TCB on the send list */
	spin_lock_irqsave(&etdev->TCBSendQLock, flags);

	tcb = etdev->tx_ring.send_head;

	while (tcb != NULL && freed < NUM_TCB) {
		struct tcb *next = tcb->next;

		etdev->tx_ring.send_head = next;

		if (next == NULL)
			etdev->tx_ring.send_tail = NULL;

		etdev->tx_ring.used--;

		spin_unlock_irqrestore(&etdev->TCBSendQLock, flags);

		freed++;
		et131x_free_send_packet(etdev, tcb);

		spin_lock_irqsave(&etdev->TCBSendQLock, flags);

		tcb = etdev->tx_ring.send_head;
	}

	WARN_ON(freed == NUM_TCB);

	spin_unlock_irqrestore(&etdev->TCBSendQLock, flags);

	etdev->tx_ring.used = 0;
}

/**
 * et131x_handle_send_interrupt - Interrupt handler for sending processing
 * @etdev: pointer to our adapter
 *
 * Re-claim the send resources, complete sends and get more to send from
 * the send wait queue.
 *
 * Assumption - Send spinlock has been acquired
 */
void et131x_handle_send_interrupt(struct et131x_adapter *etdev)
{
	unsigned long flags;
	u32 serviced;
	struct tcb *tcb;
	u32 index;

	serviced = readl(&etdev->regs->txdma.NewServiceComplete);
	index = INDEX10(serviced);

	/* Has the ring wrapped?  Process any descriptors that do not have
	 * the same "wrap" indicator as the current completion indicator
	 */
	spin_lock_irqsave(&etdev->TCBSendQLock, flags);

	tcb = etdev->tx_ring.send_head;

	while (tcb &&
	       ((serviced ^ tcb->index) & ET_DMA10_WRAP) &&
	       index < INDEX10(tcb->index)) {
		etdev->tx_ring.used--;
		etdev->tx_ring.send_head = tcb->next;
		if (tcb->next == NULL)
			etdev->tx_ring.send_tail = NULL;

		spin_unlock_irqrestore(&etdev->TCBSendQLock, flags);
		et131x_free_send_packet(etdev, tcb);
		spin_lock_irqsave(&etdev->TCBSendQLock, flags);

		/* Goto the next packet */
		tcb = etdev->tx_ring.send_head;
	}
	while (tcb &&
	       !((serviced ^ tcb->index) & ET_DMA10_WRAP)
	       && index > (tcb->index & ET_DMA10_MASK)) {
		etdev->tx_ring.used--;
		etdev->tx_ring.send_head = tcb->next;
		if (tcb->next == NULL)
			etdev->tx_ring.send_tail = NULL;

		spin_unlock_irqrestore(&etdev->TCBSendQLock, flags);
		et131x_free_send_packet(etdev, tcb);
		spin_lock_irqsave(&etdev->TCBSendQLock, flags);

		/* Goto the next packet */
		tcb = etdev->tx_ring.send_head;
	}

	/* Wake up the queue when we hit a low-water mark */
	if (etdev->tx_ring.used <= NUM_TCB / 3)
		netif_wake_queue(etdev->netdev);

	spin_unlock_irqrestore(&etdev->TCBSendQLock, flags);
}

