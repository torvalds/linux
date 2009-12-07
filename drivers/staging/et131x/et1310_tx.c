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


static void et131x_update_tcb_list(struct et131x_adapter *etdev);
static void et131x_check_send_wait_list(struct et131x_adapter *etdev);
static inline void et131x_free_send_packet(struct et131x_adapter *etdev,
					   PMP_TCB pMpTcb);
static int et131x_send_packet(struct sk_buff *skb,
			      struct et131x_adapter *etdev);
static int nic_send_packet(struct et131x_adapter *etdev, PMP_TCB pMpTcb);

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
	TX_RING_t *tx_ring = &adapter->TxRing;

	/* Allocate memory for the TCB's (Transmit Control Block) */
	adapter->TxRing.MpTcbMem = (MP_TCB *)kcalloc(NUM_TCB, sizeof(MP_TCB),
						      GFP_ATOMIC | GFP_DMA);
	if (!adapter->TxRing.MpTcbMem) {
		dev_err(&adapter->pdev->dev, "Cannot alloc memory for TCBs\n");
		return -ENOMEM;
	}

	/* Allocate enough memory for the Tx descriptor ring, and allocate
	 * some extra so that the ring can be aligned on a 4k boundary.
	 */
	desc_size = (sizeof(TX_DESC_ENTRY_t) * NUM_DESC_PER_RING_TX) + 4096 - 1;
	tx_ring->pTxDescRingVa =
	    (PTX_DESC_ENTRY_t) pci_alloc_consistent(adapter->pdev, desc_size,
						    &tx_ring->pTxDescRingPa);
	if (!adapter->TxRing.pTxDescRingVa) {
		dev_err(&adapter->pdev->dev, "Cannot alloc memory for Tx Ring\n");
		return -ENOMEM;
	}

	/* Save physical address
	 *
	 * NOTE: pci_alloc_consistent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here before
	 * storing the adjusted address.
	 */
	tx_ring->pTxDescRingAdjustedPa = tx_ring->pTxDescRingPa;

	/* Align Tx Descriptor Ring on a 4k (0x1000) byte boundary */
	et131x_align_allocated_memory(adapter,
				      &tx_ring->pTxDescRingAdjustedPa,
				      &tx_ring->TxDescOffset, 0x0FFF);

	tx_ring->pTxDescRingVa += tx_ring->TxDescOffset;

	/* Allocate memory for the Tx status block */
	tx_ring->pTxStatusVa = pci_alloc_consistent(adapter->pdev,
						    sizeof(TX_STATUS_BLOCK_t),
						    &tx_ring->pTxStatusPa);
	if (!adapter->TxRing.pTxStatusPa) {
		dev_err(&adapter->pdev->dev,
				  "Cannot alloc memory for Tx status block\n");
		return -ENOMEM;
	}

	/* Allocate memory for a dummy buffer */
	tx_ring->pTxDummyBlkVa = pci_alloc_consistent(adapter->pdev,
						      NIC_MIN_PACKET_SIZE,
						      &tx_ring->pTxDummyBlkPa);
	if (!adapter->TxRing.pTxDummyBlkPa) {
		dev_err(&adapter->pdev->dev,
			"Cannot alloc memory for Tx dummy buffer\n");
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

	if (adapter->TxRing.pTxDescRingVa) {
		/* Free memory relating to Tx rings here */
		adapter->TxRing.pTxDescRingVa -= adapter->TxRing.TxDescOffset;

		desc_size =
		    (sizeof(TX_DESC_ENTRY_t) * NUM_DESC_PER_RING_TX) + 4096 - 1;

		pci_free_consistent(adapter->pdev,
				    desc_size,
				    adapter->TxRing.pTxDescRingVa,
				    adapter->TxRing.pTxDescRingPa);

		adapter->TxRing.pTxDescRingVa = NULL;
	}

	/* Free memory for the Tx status block */
	if (adapter->TxRing.pTxStatusVa) {
		pci_free_consistent(adapter->pdev,
				    sizeof(TX_STATUS_BLOCK_t),
				    adapter->TxRing.pTxStatusVa,
				    adapter->TxRing.pTxStatusPa);

		adapter->TxRing.pTxStatusVa = NULL;
	}

	/* Free memory for the dummy buffer */
	if (adapter->TxRing.pTxDummyBlkVa) {
		pci_free_consistent(adapter->pdev,
				    NIC_MIN_PACKET_SIZE,
				    adapter->TxRing.pTxDummyBlkVa,
				    adapter->TxRing.pTxDummyBlkPa);

		adapter->TxRing.pTxDummyBlkVa = NULL;
	}

	/* Free the memory for MP_TCB structures */
	kfree(adapter->TxRing.MpTcbMem);
}

/**
 * ConfigTxDmaRegs - Set up the tx dma section of the JAGCore.
 * @etdev: pointer to our private adapter structure
 */
void ConfigTxDmaRegs(struct et131x_adapter *etdev)
{
	struct _TXDMA_t __iomem *txdma = &etdev->regs->txdma;

	/* Load the hardware with the start of the transmit descriptor ring. */
	writel((uint32_t) (etdev->TxRing.pTxDescRingAdjustedPa >> 32),
	       &txdma->pr_base_hi);
	writel((uint32_t) etdev->TxRing.pTxDescRingAdjustedPa,
	       &txdma->pr_base_lo);

	/* Initialise the transmit DMA engine */
	writel(NUM_DESC_PER_RING_TX - 1, &txdma->pr_num_des.value);

	/* Load the completion writeback physical address
	 *
	 * NOTE: pci_alloc_consistent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here before
	 * storing the adjusted address.
	 */
	writel(0, &txdma->dma_wb_base_hi);
	writel(etdev->TxRing.pTxStatusPa, &txdma->dma_wb_base_lo);

	memset(etdev->TxRing.pTxStatusVa, 0, sizeof(TX_STATUS_BLOCK_t));

	writel(0, &txdma->service_request);
	etdev->TxRing.txDmaReadyToSend = 0;
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
	u32 csr = ET_TXDMA_SNGL_EPKT;
	if (etdev->RegistryPhyLoopbk)
		/* TxDMA is disabled for loopback operation. */
		csr |= ET_TXDMA_CSR_HALT;
	else
		/* Setup the transmit dma configuration register for normal
		 * operation
		 */
		csr |= PARM_DMA_CACHE_DEF << ET_TXDMA_CACHE_SHIFT;
	writel(csr, &etdev->regs->txdma.csr);
}

/**
 * et131x_init_send - Initialize send data structures
 * @adapter: pointer to our private adapter structure
 */
void et131x_init_send(struct et131x_adapter *adapter)
{
	PMP_TCB pMpTcb;
	uint32_t TcbCount;
	TX_RING_t *tx_ring;

	/* Setup some convenience pointers */
	tx_ring = &adapter->TxRing;
	pMpTcb = adapter->TxRing.MpTcbMem;

	tx_ring->TCBReadyQueueHead = pMpTcb;

	/* Go through and set up each TCB */
	for (TcbCount = 0; TcbCount < NUM_TCB; TcbCount++) {
		memset(pMpTcb, 0, sizeof(MP_TCB));

		/* Set the link pointer in HW TCB to the next TCB in the
		 * chain.  If this is the last TCB in the chain, also set the
		 * tail pointer.
		 */
		if (TcbCount < NUM_TCB - 1) {
			pMpTcb->Next = pMpTcb + 1;
		} else {
			tx_ring->TCBReadyQueueTail = pMpTcb;
			pMpTcb->Next = (PMP_TCB) NULL;
		}

		pMpTcb++;
	}

	/* Curr send queue should now be empty */
	tx_ring->CurrSendHead = (PMP_TCB) NULL;
	tx_ring->CurrSendTail = (PMP_TCB) NULL;

	INIT_LIST_HEAD(&adapter->TxRing.SendWaitQueue);
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

	/* Queue is not empty or TCB is not available */
	if (!list_empty(&etdev->TxRing.SendWaitQueue) ||
	    MP_TCB_RESOURCES_NOT_AVAILABLE(etdev)) {
		/* NOTE: If there's an error on send, no need to queue the
		 * packet under Linux; if we just send an error up to the
		 * netif layer, it will resend the skb to us.
		 */
		status = -ENOMEM;
	} else {
		/* We need to see if the link is up; if it's not, make the
		 * netif layer think we're good and drop the packet
		 */
		/*
		 * if( MP_SHOULD_FAIL_SEND( etdev ) ||
		 *  etdev->DriverNoPhyAccess )
		 */
		if (MP_SHOULD_FAIL_SEND(etdev) || etdev->DriverNoPhyAccess
		    || !netif_carrier_ok(netdev)) {
			dev_kfree_skb_any(skb);
			skb = NULL;

			etdev->net_stats.tx_dropped++;
		} else {
			status = et131x_send_packet(skb, etdev);

			if (status == -ENOMEM) {

				/* NOTE: If there's an error on send, no need
				 * to queue the packet under Linux; if we just
				 * send an error up to the netif layer, it
				 * will resend the skb to us.
				 */
			} else if (status != 0) {
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
	int status = 0;
	PMP_TCB pMpTcb = NULL;
	uint16_t *shbufva;
	unsigned long flags;

	/* All packets must have at least a MAC address and a protocol type */
	if (skb->len < ETH_HLEN) {
		return -EIO;
	}

	/* Get a TCB for this packet */
	spin_lock_irqsave(&etdev->TCBReadyQLock, flags);

	pMpTcb = etdev->TxRing.TCBReadyQueueHead;

	if (pMpTcb == NULL) {
		spin_unlock_irqrestore(&etdev->TCBReadyQLock, flags);
		return -ENOMEM;
	}

	etdev->TxRing.TCBReadyQueueHead = pMpTcb->Next;

	if (etdev->TxRing.TCBReadyQueueHead == NULL)
		etdev->TxRing.TCBReadyQueueTail = NULL;

	spin_unlock_irqrestore(&etdev->TCBReadyQLock, flags);

	pMpTcb->PacketLength = skb->len;
	pMpTcb->Packet = skb;

	if ((skb->data != NULL) && ((skb->len - skb->data_len) >= 6)) {
		shbufva = (uint16_t *) skb->data;

		if ((shbufva[0] == 0xffff) &&
		    (shbufva[1] == 0xffff) && (shbufva[2] == 0xffff)) {
			pMpTcb->Flags |= fMP_DEST_BROAD;
		} else if ((shbufva[0] & 0x3) == 0x0001) {
			pMpTcb->Flags |=  fMP_DEST_MULTI;
		}
	}

	pMpTcb->Next = NULL;

	/* Call the NIC specific send handler. */
	if (status == 0)
		status = nic_send_packet(etdev, pMpTcb);

	if (status != 0) {
		spin_lock_irqsave(&etdev->TCBReadyQLock, flags);

		if (etdev->TxRing.TCBReadyQueueTail) {
			etdev->TxRing.TCBReadyQueueTail->Next = pMpTcb;
		} else {
			/* Apparently ready Q is empty. */
			etdev->TxRing.TCBReadyQueueHead = pMpTcb;
		}

		etdev->TxRing.TCBReadyQueueTail = pMpTcb;
		spin_unlock_irqrestore(&etdev->TCBReadyQLock, flags);
		return status;
	}
	WARN_ON(etdev->TxRing.nBusySend > NUM_TCB);
	return 0;
}

/**
 * nic_send_packet - NIC specific send handler for version B silicon.
 * @etdev: pointer to our adapter
 * @pMpTcb: pointer to MP_TCB
 *
 * Returns 0 or errno.
 */
static int nic_send_packet(struct et131x_adapter *etdev, PMP_TCB pMpTcb)
{
	uint32_t loopIndex;
	TX_DESC_ENTRY_t CurDesc[24];
	uint32_t FragmentNumber = 0;
	uint32_t thiscopy, remainder;
	struct sk_buff *pPacket = pMpTcb->Packet;
	uint32_t FragListCount = skb_shinfo(pPacket)->nr_frags + 1;
	struct skb_frag_struct *pFragList = &skb_shinfo(pPacket)->frags[0];
	unsigned long flags;

	/* Part of the optimizations of this send routine restrict us to
	 * sending 24 fragments at a pass.  In practice we should never see
	 * more than 5 fragments.
	 *
	 * NOTE: The older version of this function (below) can handle any
	 * number of fragments. If needed, we can call this function,
	 * although it is less efficient.
	 */
	if (FragListCount > 23) {
		return -EIO;
	}

	memset(CurDesc, 0, sizeof(TX_DESC_ENTRY_t) * (FragListCount + 1));

	for (loopIndex = 0; loopIndex < FragListCount; loopIndex++) {
		/* If there is something in this element, lets get a
		 * descriptor from the ring and get the necessary data
		 */
		if (loopIndex == 0) {
			/* If the fragments are smaller than a standard MTU,
			 * then map them to a single descriptor in the Tx
			 * Desc ring. However, if they're larger, as is
			 * possible with support for jumbo packets, then
			 * split them each across 2 descriptors.
			 *
			 * This will work until we determine why the hardware
			 * doesn't seem to like large fragments.
			 */
			if ((pPacket->len - pPacket->data_len) <= 1514) {
				CurDesc[FragmentNumber].DataBufferPtrHigh = 0;
				CurDesc[FragmentNumber].word2.bits.
				    length_in_bytes =
				    pPacket->len - pPacket->data_len;

				/* NOTE: Here, the dma_addr_t returned from
				 * pci_map_single() is implicitly cast as a
				 * uint32_t. Although dma_addr_t can be
				 * 64-bit, the address returned by
				 * pci_map_single() is always 32-bit
				 * addressable (as defined by the pci/dma
				 * subsystem)
				 */
				CurDesc[FragmentNumber++].DataBufferPtrLow =
				    pci_map_single(etdev->pdev,
						   pPacket->data,
						   pPacket->len -
						   pPacket->data_len,
						   PCI_DMA_TODEVICE);
			} else {
				CurDesc[FragmentNumber].DataBufferPtrHigh = 0;
				CurDesc[FragmentNumber].word2.bits.
				    length_in_bytes =
				    ((pPacket->len - pPacket->data_len) / 2);

				/* NOTE: Here, the dma_addr_t returned from
				 * pci_map_single() is implicitly cast as a
				 * uint32_t. Although dma_addr_t can be
				 * 64-bit, the address returned by
				 * pci_map_single() is always 32-bit
				 * addressable (as defined by the pci/dma
				 * subsystem)
				 */
				CurDesc[FragmentNumber++].DataBufferPtrLow =
				    pci_map_single(etdev->pdev,
						   pPacket->data,
						   ((pPacket->len -
						     pPacket->data_len) / 2),
						   PCI_DMA_TODEVICE);
				CurDesc[FragmentNumber].DataBufferPtrHigh = 0;

				CurDesc[FragmentNumber].word2.bits.
				    length_in_bytes =
				    ((pPacket->len - pPacket->data_len) / 2);

				/* NOTE: Here, the dma_addr_t returned from
				 * pci_map_single() is implicitly cast as a
				 * uint32_t. Although dma_addr_t can be
				 * 64-bit, the address returned by
				 * pci_map_single() is always 32-bit
				 * addressable (as defined by the pci/dma
				 * subsystem)
				 */
				CurDesc[FragmentNumber++].DataBufferPtrLow =
				    pci_map_single(etdev->pdev,
						   pPacket->data +
						   ((pPacket->len -
						     pPacket->data_len) / 2),
						   ((pPacket->len -
						     pPacket->data_len) / 2),
						   PCI_DMA_TODEVICE);
			}
		} else {
			CurDesc[FragmentNumber].DataBufferPtrHigh = 0;
			CurDesc[FragmentNumber].word2.bits.length_in_bytes =
			    pFragList[loopIndex - 1].size;

			/* NOTE: Here, the dma_addr_t returned from
			 * pci_map_page() is implicitly cast as a uint32_t.
			 * Although dma_addr_t can be 64-bit, the address
			 * returned by pci_map_page() is always 32-bit
			 * addressable (as defined by the pci/dma subsystem)
			 */
			CurDesc[FragmentNumber++].DataBufferPtrLow =
			    pci_map_page(etdev->pdev,
					 pFragList[loopIndex - 1].page,
					 pFragList[loopIndex - 1].page_offset,
					 pFragList[loopIndex - 1].size,
					 PCI_DMA_TODEVICE);
		}
	}

	if (FragmentNumber == 0)
		return -EIO;

	if (etdev->linkspeed == TRUEPHY_SPEED_1000MBPS) {
		if (++etdev->TxRing.TxPacketsSinceLastinterrupt ==
		    PARM_TX_NUM_BUFS_DEF) {
			CurDesc[FragmentNumber - 1].word3.value = 0x5;
			etdev->TxRing.TxPacketsSinceLastinterrupt = 0;
		} else {
			CurDesc[FragmentNumber - 1].word3.value = 0x1;
		}
	} else {
		CurDesc[FragmentNumber - 1].word3.value = 0x5;
	}

	CurDesc[0].word3.bits.f = 1;

	pMpTcb->WrIndexStart = etdev->TxRing.txDmaReadyToSend;
	pMpTcb->PacketStaleCount = 0;

	spin_lock_irqsave(&etdev->SendHWLock, flags);

	thiscopy = NUM_DESC_PER_RING_TX -
				INDEX10(etdev->TxRing.txDmaReadyToSend);

	if (thiscopy >= FragmentNumber) {
		remainder = 0;
		thiscopy = FragmentNumber;
	} else {
		remainder = FragmentNumber - thiscopy;
	}

	memcpy(etdev->TxRing.pTxDescRingVa +
	       INDEX10(etdev->TxRing.txDmaReadyToSend), CurDesc,
	       sizeof(TX_DESC_ENTRY_t) * thiscopy);

	add_10bit(&etdev->TxRing.txDmaReadyToSend, thiscopy);

	if (INDEX10(etdev->TxRing.txDmaReadyToSend)== 0 ||
	    INDEX10(etdev->TxRing.txDmaReadyToSend) == NUM_DESC_PER_RING_TX) {
	     	etdev->TxRing.txDmaReadyToSend &= ~ET_DMA10_MASK;
	     	etdev->TxRing.txDmaReadyToSend ^= ET_DMA10_WRAP;
	}

	if (remainder) {
		memcpy(etdev->TxRing.pTxDescRingVa,
		       CurDesc + thiscopy,
		       sizeof(TX_DESC_ENTRY_t) * remainder);

		add_10bit(&etdev->TxRing.txDmaReadyToSend, remainder);
	}

	if (INDEX10(etdev->TxRing.txDmaReadyToSend) == 0) {
		if (etdev->TxRing.txDmaReadyToSend)
			pMpTcb->WrIndex = NUM_DESC_PER_RING_TX - 1;
		else
			pMpTcb->WrIndex= ET_DMA10_WRAP | (NUM_DESC_PER_RING_TX - 1);
	} else
		pMpTcb->WrIndex = etdev->TxRing.txDmaReadyToSend - 1;

	spin_lock(&etdev->TCBSendQLock);

	if (etdev->TxRing.CurrSendTail)
		etdev->TxRing.CurrSendTail->Next = pMpTcb;
	else
		etdev->TxRing.CurrSendHead = pMpTcb;

	etdev->TxRing.CurrSendTail = pMpTcb;

	WARN_ON(pMpTcb->Next != NULL);

	etdev->TxRing.nBusySend++;

	spin_unlock(&etdev->TCBSendQLock);

	/* Write the new write pointer back to the device. */
	writel(etdev->TxRing.txDmaReadyToSend,
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
 * et131x_free_send_packet - Recycle a MP_TCB, complete the packet if necessary
 * @etdev: pointer to our adapter
 * @pMpTcb: pointer to MP_TCB
 *
 * Assumption - Send spinlock has been acquired
 */
inline void et131x_free_send_packet(struct et131x_adapter *etdev,
							PMP_TCB pMpTcb)
{
	unsigned long flags;
	TX_DESC_ENTRY_t *desc = NULL;
	struct net_device_stats *stats = &etdev->net_stats;

	if (pMpTcb->Flags & fMP_DEST_BROAD)
		atomic_inc(&etdev->Stats.brdcstxmt);
	else if (pMpTcb->Flags & fMP_DEST_MULTI)
		atomic_inc(&etdev->Stats.multixmt);
	else
		atomic_inc(&etdev->Stats.unixmt);

	if (pMpTcb->Packet) {
		stats->tx_bytes += pMpTcb->Packet->len;

		/* Iterate through the TX descriptors on the ring
		 * corresponding to this packet and umap the fragments
		 * they point to
		 */
		do {
			desc =
			    (TX_DESC_ENTRY_t *) (etdev->TxRing.pTxDescRingVa +
			    	INDEX10(pMpTcb->WrIndexStart));

			pci_unmap_single(etdev->pdev,
					 desc->DataBufferPtrLow,
					 desc->word2.value, PCI_DMA_TODEVICE);

			add_10bit(&pMpTcb->WrIndexStart, 1);
			if (INDEX10(pMpTcb->WrIndexStart) >=
			    NUM_DESC_PER_RING_TX) {
			    	pMpTcb->WrIndexStart &= ~ET_DMA10_MASK;
			    	pMpTcb->WrIndexStart ^= ET_DMA10_WRAP;
			}
		} while (desc != (etdev->TxRing.pTxDescRingVa +
				INDEX10(pMpTcb->WrIndex)));

		dev_kfree_skb_any(pMpTcb->Packet);
	}

	memset(pMpTcb, 0, sizeof(MP_TCB));

	/* Add the TCB to the Ready Q */
	spin_lock_irqsave(&etdev->TCBReadyQLock, flags);

	etdev->Stats.opackets++;

	if (etdev->TxRing.TCBReadyQueueTail) {
		etdev->TxRing.TCBReadyQueueTail->Next = pMpTcb;
	} else {
		/* Apparently ready Q is empty. */
		etdev->TxRing.TCBReadyQueueHead = pMpTcb;
	}

	etdev->TxRing.TCBReadyQueueTail = pMpTcb;

	spin_unlock_irqrestore(&etdev->TCBReadyQLock, flags);
	WARN_ON(etdev->TxRing.nBusySend < 0);
}

/**
 * et131x_free_busy_send_packets - Free and complete the stopped active sends
 * @etdev: pointer to our adapter
 *
 * Assumption - Send spinlock has been acquired
 */
void et131x_free_busy_send_packets(struct et131x_adapter *etdev)
{
	PMP_TCB pMpTcb;
	struct list_head *entry;
	unsigned long flags;
	uint32_t FreeCounter = 0;

	while (!list_empty(&etdev->TxRing.SendWaitQueue)) {
		spin_lock_irqsave(&etdev->SendWaitLock, flags);

		etdev->TxRing.nWaitSend--;
		spin_unlock_irqrestore(&etdev->SendWaitLock, flags);

		entry = etdev->TxRing.SendWaitQueue.next;
	}

	etdev->TxRing.nWaitSend = 0;

	/* Any packets being sent? Check the first TCB on the send list */
	spin_lock_irqsave(&etdev->TCBSendQLock, flags);

	pMpTcb = etdev->TxRing.CurrSendHead;

	while ((pMpTcb != NULL) && (FreeCounter < NUM_TCB)) {
		PMP_TCB pNext = pMpTcb->Next;

		etdev->TxRing.CurrSendHead = pNext;

		if (pNext == NULL)
			etdev->TxRing.CurrSendTail = NULL;

		etdev->TxRing.nBusySend--;

		spin_unlock_irqrestore(&etdev->TCBSendQLock, flags);

		FreeCounter++;
		et131x_free_send_packet(etdev, pMpTcb);

		spin_lock_irqsave(&etdev->TCBSendQLock, flags);

		pMpTcb = etdev->TxRing.CurrSendHead;
	}

	WARN_ON(FreeCounter == NUM_TCB);

	spin_unlock_irqrestore(&etdev->TCBSendQLock, flags);

	etdev->TxRing.nBusySend = 0;
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
	/* Mark as completed any packets which have been sent by the device. */
	et131x_update_tcb_list(etdev);

	/* If we queued any transmits because we didn't have any TCBs earlier,
	 * dequeue and send those packets now, as long as we have free TCBs.
	 */
	et131x_check_send_wait_list(etdev);
}

/**
 * et131x_update_tcb_list - Helper routine for Send Interrupt handler
 * @etdev: pointer to our adapter
 *
 * Re-claims the send resources and completes sends.  Can also be called as
 * part of the NIC send routine when the "ServiceComplete" indication has
 * wrapped.
 */
static void et131x_update_tcb_list(struct et131x_adapter *etdev)
{
	unsigned long flags;
	u32 ServiceComplete;
	PMP_TCB pMpTcb;
	u32 index;

	ServiceComplete = readl(&etdev->regs->txdma.NewServiceComplete);
	index = INDEX10(ServiceComplete);

	/* Has the ring wrapped?  Process any descriptors that do not have
	 * the same "wrap" indicator as the current completion indicator
	 */
	spin_lock_irqsave(&etdev->TCBSendQLock, flags);

	pMpTcb = etdev->TxRing.CurrSendHead;

	while (pMpTcb &&
	       ((ServiceComplete ^ pMpTcb->WrIndex) & ET_DMA10_WRAP) &&
	       index < INDEX10(pMpTcb->WrIndex)) {
		etdev->TxRing.nBusySend--;
		etdev->TxRing.CurrSendHead = pMpTcb->Next;
		if (pMpTcb->Next == NULL)
			etdev->TxRing.CurrSendTail = NULL;

		spin_unlock_irqrestore(&etdev->TCBSendQLock, flags);
		et131x_free_send_packet(etdev, pMpTcb);
		spin_lock_irqsave(&etdev->TCBSendQLock, flags);

		/* Goto the next packet */
		pMpTcb = etdev->TxRing.CurrSendHead;
	}
	while (pMpTcb &&
	       !((ServiceComplete ^ pMpTcb->WrIndex) & ET_DMA10_WRAP)
	       && index > (pMpTcb->WrIndex & ET_DMA10_MASK)) {
		etdev->TxRing.nBusySend--;
		etdev->TxRing.CurrSendHead = pMpTcb->Next;
		if (pMpTcb->Next == NULL)
			etdev->TxRing.CurrSendTail = NULL;

		spin_unlock_irqrestore(&etdev->TCBSendQLock, flags);
		et131x_free_send_packet(etdev, pMpTcb);
		spin_lock_irqsave(&etdev->TCBSendQLock, flags);

		/* Goto the next packet */
		pMpTcb = etdev->TxRing.CurrSendHead;
	}

	/* Wake up the queue when we hit a low-water mark */
	if (etdev->TxRing.nBusySend <= (NUM_TCB / 3))
		netif_wake_queue(etdev->netdev);

	spin_unlock_irqrestore(&etdev->TCBSendQLock, flags);
}

/**
 * et131x_check_send_wait_list - Helper routine for the interrupt handler
 * @etdev: pointer to our adapter
 *
 * Takes packets from the send wait queue and posts them to the device (if
 * room available).
 */
static void et131x_check_send_wait_list(struct et131x_adapter *etdev)
{
	unsigned long flags;

	spin_lock_irqsave(&etdev->SendWaitLock, flags);

	while (!list_empty(&etdev->TxRing.SendWaitQueue) &&
				MP_TCB_RESOURCES_AVAILABLE(etdev)) {
		struct list_head *entry;

		entry = etdev->TxRing.SendWaitQueue.next;

		etdev->TxRing.nWaitSend--;
	}

	spin_unlock_irqrestore(&etdev->SendWaitLock, flags);
}
