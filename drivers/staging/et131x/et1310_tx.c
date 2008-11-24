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
 * THIS SOFTWARE IS PROVIDED “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES,
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
#include "et131x_debug.h"
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
#include <asm/io.h>
#include <asm/system.h>
#include <asm/bitops.h>

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

/* Data for debugging facilities */
#ifdef CONFIG_ET131X_DEBUG
extern dbg_info_t *et131x_dbginfo;
#endif /* CONFIG_ET131X_DEBUG */

static void et131x_update_tcb_list(struct et131x_adapter *pAdapter);
static void et131x_check_send_wait_list(struct et131x_adapter *pAdapter);
static inline void et131x_free_send_packet(struct et131x_adapter *pAdapter,
					   PMP_TCB pMpTcb);
static int et131x_send_packet(struct sk_buff *skb,
			      struct et131x_adapter *pAdapter);
static int nic_send_packet(struct et131x_adapter *pAdapter, PMP_TCB pMpTcb);

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

	DBG_ENTER(et131x_dbginfo);

	/* Allocate memory for the TCB's (Transmit Control Block) */
	adapter->TxRing.MpTcbMem = (MP_TCB *) kcalloc(NUM_TCB, sizeof(MP_TCB),
						      GFP_ATOMIC | GFP_DMA);
	if (!adapter->TxRing.MpTcbMem) {
		DBG_ERROR(et131x_dbginfo, "Cannot alloc memory for TCBs\n");
		DBG_LEAVE(et131x_dbginfo);
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
		DBG_ERROR(et131x_dbginfo, "Cannot alloc memory for Tx Ring\n");
		DBG_LEAVE(et131x_dbginfo);
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
		DBG_ERROR(et131x_dbginfo,
			  "Cannot alloc memory for Tx status block\n");
		DBG_LEAVE(et131x_dbginfo);
		return -ENOMEM;
	}

	/* Allocate memory for a dummy buffer */
	tx_ring->pTxDummyBlkVa = pci_alloc_consistent(adapter->pdev,
						      NIC_MIN_PACKET_SIZE,
						      &tx_ring->pTxDummyBlkPa);
	if (!adapter->TxRing.pTxDummyBlkPa) {
		DBG_ERROR(et131x_dbginfo,
			  "Cannot alloc memory for Tx dummy buffer\n");
		DBG_LEAVE(et131x_dbginfo);
		return -ENOMEM;
	}

	DBG_LEAVE(et131x_dbginfo);
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

	DBG_ENTER(et131x_dbginfo);

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
	if (adapter->TxRing.MpTcbMem) {
		kfree(adapter->TxRing.MpTcbMem);
		adapter->TxRing.MpTcbMem = NULL;
	}

	DBG_LEAVE(et131x_dbginfo);
}

/**
 * ConfigTxDmaRegs - Set up the tx dma section of the JAGCore.
 * @adapter: pointer to our private adapter structure
 */
void ConfigTxDmaRegs(struct et131x_adapter *pAdapter)
{
	struct _TXDMA_t __iomem *pTxDma = &pAdapter->CSRAddress->txdma;

	DBG_ENTER(et131x_dbginfo);

	/* Load the hardware with the start of the transmit descriptor ring. */
	writel((uint32_t) (pAdapter->TxRing.pTxDescRingAdjustedPa >> 32),
	       &pTxDma->pr_base_hi);
	writel((uint32_t) pAdapter->TxRing.pTxDescRingAdjustedPa,
	       &pTxDma->pr_base_lo);

	/* Initialise the transmit DMA engine */
	writel(NUM_DESC_PER_RING_TX - 1, &pTxDma->pr_num_des.value);

	/* Load the completion writeback physical address
	 *
	 * NOTE: pci_alloc_consistent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here before
	 * storing the adjusted address.
	 */
	writel(0, &pTxDma->dma_wb_base_hi);
	writel(pAdapter->TxRing.pTxStatusPa, &pTxDma->dma_wb_base_lo);

	memset(pAdapter->TxRing.pTxStatusVa, 0, sizeof(TX_STATUS_BLOCK_t));

	writel(0, &pTxDma->service_request.value);
	pAdapter->TxRing.txDmaReadyToSend.value = 0;

	DBG_LEAVE(et131x_dbginfo);
}

/**
 * et131x_tx_dma_disable - Stop of Tx_DMA on the ET1310
 * @pAdapter: pointer to our adapter structure
 */
void et131x_tx_dma_disable(struct et131x_adapter *pAdapter)
{
	DBG_ENTER(et131x_dbginfo);

	/* Setup the tramsmit dma configuration register */
	writel(0x101, &pAdapter->CSRAddress->txdma.csr.value);

	DBG_LEAVE(et131x_dbginfo);
}

/**
 * et131x_tx_dma_enable - re-start of Tx_DMA on the ET1310.
 * @pAdapter: pointer to our adapter structure
 *
 * Mainly used after a return to the D0 (full-power) state from a lower state.
 */
void et131x_tx_dma_enable(struct et131x_adapter *pAdapter)
{
	DBG_ENTER(et131x_dbginfo);

	if (pAdapter->RegistryPhyLoopbk) {
	/* TxDMA is disabled for loopback operation. */
		writel(0x101, &pAdapter->CSRAddress->txdma.csr.value);
	} else {
		TXDMA_CSR_t csr = { 0 };

		/* Setup the transmit dma configuration register for normal
		 * operation
		 */
		csr.bits.sngl_epkt_mode = 1;
		csr.bits.halt = 0;
		csr.bits.cache_thrshld = pAdapter->RegistryDMACache;
		writel(csr.value, &pAdapter->CSRAddress->txdma.csr.value);
	}

	DBG_LEAVE(et131x_dbginfo);
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

	DBG_ENTER(et131x_dbginfo);

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

	DBG_LEAVE(et131x_dbginfo);
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
	struct et131x_adapter *pAdapter = NULL;

	DBG_TX_ENTER(et131x_dbginfo);

	pAdapter = netdev_priv(netdev);

	/* Send these packets
	 *
	 * NOTE: The Linux Tx entry point is only given one packet at a time
	 * to Tx, so the PacketCount and it's array used makes no sense here
	 */

	/* Queue is not empty or TCB is not available */
	if (!list_empty(&pAdapter->TxRing.SendWaitQueue) ||
	    MP_TCB_RESOURCES_NOT_AVAILABLE(pAdapter)) {
		/* NOTE: If there's an error on send, no need to queue the
		 * packet under Linux; if we just send an error up to the
		 * netif layer, it will resend the skb to us.
		 */
		DBG_VERBOSE(et131x_dbginfo, "TCB Resources Not Available\n");
		status = -ENOMEM;
	} else {
		/* We need to see if the link is up; if it's not, make the
		 * netif layer think we're good and drop the packet
		 */
		//if( MP_SHOULD_FAIL_SEND( pAdapter ) || pAdapter->DriverNoPhyAccess )
		if (MP_SHOULD_FAIL_SEND(pAdapter) || pAdapter->DriverNoPhyAccess
		    || !netif_carrier_ok(netdev)) {
			DBG_VERBOSE(et131x_dbginfo,
				    "Can't Tx, Link is DOWN; drop the packet\n");

			dev_kfree_skb_any(skb);
			skb = NULL;

			pAdapter->net_stats.tx_dropped++;
		} else {
			status = et131x_send_packet(skb, pAdapter);

			if (status == -ENOMEM) {

				/* NOTE: If there's an error on send, no need
				 * to queue the packet under Linux; if we just
				 * send an error up to the netif layer, it
				 * will resend the skb to us.
				 */
				DBG_WARNING(et131x_dbginfo,
					    "Resources problem, Queue tx packet\n");
			} else if (status != 0) {
				/* On any other error, make netif think we're
				 * OK and drop the packet
				 */
				DBG_WARNING(et131x_dbginfo,
					    "General error, drop packet\n");

				dev_kfree_skb_any(skb);
				skb = NULL;

				pAdapter->net_stats.tx_dropped++;
			}
		}
	}

	DBG_TX_LEAVE(et131x_dbginfo);
	return status;
}

/**
 * et131x_send_packet - Do the work to send a packet
 * @skb: the packet(s) to send
 * @pAdapter: a pointer to the device's private adapter structure
 *
 * Return 0 in almost all cases; non-zero value in extreme hard failure only.
 *
 * Assumption: Send spinlock has been acquired
 */
static int et131x_send_packet(struct sk_buff *skb,
			      struct et131x_adapter *pAdapter)
{
	int status = 0;
	PMP_TCB pMpTcb = NULL;
	uint16_t *pShBufVa;
	unsigned long lockflags;

	DBG_TX_ENTER(et131x_dbginfo);

	/* Is our buffer scattered, or continuous? */
	if (skb_shinfo(skb)->nr_frags == 0) {
		DBG_TX(et131x_dbginfo, "Scattered buffer: NO\n");
	} else {
		DBG_TX(et131x_dbginfo, "Scattered buffer: YES, Num Frags: %d\n",
		       skb_shinfo(skb)->nr_frags);
	}

	/* All packets must have at least a MAC address and a protocol type */
	if (skb->len < ETH_HLEN) {
		DBG_ERROR(et131x_dbginfo,
			  "Packet size < ETH_HLEN (14 bytes)\n");
		DBG_LEAVE(et131x_dbginfo);
		return -EIO;
	}

	/* Get a TCB for this packet */
	spin_lock_irqsave(&pAdapter->TCBReadyQLock, lockflags);

	pMpTcb = pAdapter->TxRing.TCBReadyQueueHead;

	if (pMpTcb == NULL) {
		spin_unlock_irqrestore(&pAdapter->TCBReadyQLock, lockflags);

		DBG_WARNING(et131x_dbginfo, "Can't obtain a TCB\n");
		DBG_TX_LEAVE(et131x_dbginfo);
		return -ENOMEM;
	}

	pAdapter->TxRing.TCBReadyQueueHead = pMpTcb->Next;

	if (pAdapter->TxRing.TCBReadyQueueHead == NULL) {
		pAdapter->TxRing.TCBReadyQueueTail = NULL;
	}

	spin_unlock_irqrestore(&pAdapter->TCBReadyQLock, lockflags);

	pMpTcb->PacketLength = skb->len;
	pMpTcb->Packet = skb;

	if ((skb->data != NULL) && ((skb->len - skb->data_len) >= 6)) {
		pShBufVa = (uint16_t *) skb->data;

		if ((pShBufVa[0] == 0xffff) &&
		    (pShBufVa[1] == 0xffff) && (pShBufVa[2] == 0xffff)) {
			MP_SET_FLAG(pMpTcb, fMP_DEST_BROAD);
		} else if ((pShBufVa[0] & 0x3) == 0x0001) {
			MP_SET_FLAG(pMpTcb, fMP_DEST_MULTI);
		}
	}

	pMpTcb->Next = NULL;

	/* Call the NIC specific send handler. */
	if (status == 0) {
		status = nic_send_packet(pAdapter, pMpTcb);
	}

	if (status != 0) {
		spin_lock_irqsave(&pAdapter->TCBReadyQLock, lockflags);

		if (pAdapter->TxRing.TCBReadyQueueTail) {
			pAdapter->TxRing.TCBReadyQueueTail->Next = pMpTcb;
		} else {
			/* Apparently ready Q is empty. */
			pAdapter->TxRing.TCBReadyQueueHead = pMpTcb;
		}

		pAdapter->TxRing.TCBReadyQueueTail = pMpTcb;

		spin_unlock_irqrestore(&pAdapter->TCBReadyQLock, lockflags);

		DBG_TX_LEAVE(et131x_dbginfo);
		return status;
	}

	DBG_ASSERT(pAdapter->TxRing.nBusySend <= NUM_TCB);

	DBG_TX_LEAVE(et131x_dbginfo);
	return 0;
}

/**
 * nic_send_packet - NIC specific send handler for version B silicon.
 * @pAdapter: pointer to our adapter
 * @pMpTcb: pointer to MP_TCB
 *
 * Returns 0 or errno.
 */
static int nic_send_packet(struct et131x_adapter *pAdapter, PMP_TCB pMpTcb)
{
	uint32_t loopIndex;
	TX_DESC_ENTRY_t CurDesc[24];
	uint32_t FragmentNumber = 0;
	uint32_t iThisCopy, iRemainder;
	struct sk_buff *pPacket = pMpTcb->Packet;
	uint32_t FragListCount = skb_shinfo(pPacket)->nr_frags + 1;
	struct skb_frag_struct *pFragList = &skb_shinfo(pPacket)->frags[0];
	unsigned long lockflags1, lockflags2;

	DBG_TX_ENTER(et131x_dbginfo);

	/* Part of the optimizations of this send routine restrict us to
	 * sending 24 fragments at a pass.  In practice we should never see
	 * more than 5 fragments.
	 *
	 * NOTE: The older version of this function (below) can handle any
	 * number of fragments. If needed, we can call this function,
	 * although it is less efficient.
	 */
	if (FragListCount > 23) {
		DBG_TX_LEAVE(et131x_dbginfo);
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
				DBG_TX(et131x_dbginfo,
				       "Got packet of length %d, "
				       "filling desc entry %d, "
				       "TCB: 0x%p\n",
				       (pPacket->len - pPacket->data_len),
				       pAdapter->TxRing.txDmaReadyToSend.bits.
				       val, pMpTcb);

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
				    pci_map_single(pAdapter->pdev,
						   pPacket->data,
						   pPacket->len -
						   pPacket->data_len,
						   PCI_DMA_TODEVICE);
			} else {
				DBG_TX(et131x_dbginfo,
				       "Got packet of length %d, "
				       "filling desc entry %d, "
				       "TCB: 0x%p\n",
				       (pPacket->len - pPacket->data_len),
				       pAdapter->TxRing.txDmaReadyToSend.bits.
				       val, pMpTcb);

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
				    pci_map_single(pAdapter->pdev,
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
				    pci_map_single(pAdapter->pdev,
						   pPacket->data +
						   ((pPacket->len -
						     pPacket->data_len) / 2),
						   ((pPacket->len -
						     pPacket->data_len) / 2),
						   PCI_DMA_TODEVICE);
			}
		} else {
			DBG_TX(et131x_dbginfo,
			       "Got packet of length %d,"
			       "filling desc entry %d\n"
			       "TCB: 0x%p\n",
			       pFragList[loopIndex].size,
			       pAdapter->TxRing.txDmaReadyToSend.bits.val,
			       pMpTcb);

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
			    pci_map_page(pAdapter->pdev,
					 pFragList[loopIndex - 1].page,
					 pFragList[loopIndex - 1].page_offset,
					 pFragList[loopIndex - 1].size,
					 PCI_DMA_TODEVICE);
		}
	}

	if (FragmentNumber == 0) {
		DBG_WARNING(et131x_dbginfo, "No. frags is 0\n");
		return -EIO;
	}

	if (pAdapter->uiLinkSpeed == TRUEPHY_SPEED_1000MBPS) {
		if (++pAdapter->TxRing.TxPacketsSinceLastinterrupt ==
		    pAdapter->RegistryTxNumBuffers) {
			CurDesc[FragmentNumber - 1].word3.value = 0x5;
			pAdapter->TxRing.TxPacketsSinceLastinterrupt = 0;
		} else {
			CurDesc[FragmentNumber - 1].word3.value = 0x1;
		}
	} else {
		CurDesc[FragmentNumber - 1].word3.value = 0x5;
	}

	CurDesc[0].word3.bits.f = 1;

	pMpTcb->WrIndexStart = pAdapter->TxRing.txDmaReadyToSend;
	pMpTcb->PacketStaleCount = 0;

	spin_lock_irqsave(&pAdapter->SendHWLock, lockflags1);

	iThisCopy =
	    NUM_DESC_PER_RING_TX - pAdapter->TxRing.txDmaReadyToSend.bits.val;

	if (iThisCopy >= FragmentNumber) {
		iRemainder = 0;
		iThisCopy = FragmentNumber;
	} else {
		iRemainder = FragmentNumber - iThisCopy;
	}

	memcpy(pAdapter->TxRing.pTxDescRingVa +
	       pAdapter->TxRing.txDmaReadyToSend.bits.val, CurDesc,
	       sizeof(TX_DESC_ENTRY_t) * iThisCopy);

	pAdapter->TxRing.txDmaReadyToSend.bits.val += iThisCopy;

	if ((pAdapter->TxRing.txDmaReadyToSend.bits.val == 0) ||
	    (pAdapter->TxRing.txDmaReadyToSend.bits.val ==
	     NUM_DESC_PER_RING_TX)) {
		if (pAdapter->TxRing.txDmaReadyToSend.bits.wrap) {
			pAdapter->TxRing.txDmaReadyToSend.value = 0;
		} else {
			pAdapter->TxRing.txDmaReadyToSend.value = 0x400;
		}
	}

	if (iRemainder) {
		memcpy(pAdapter->TxRing.pTxDescRingVa,
		       CurDesc + iThisCopy,
		       sizeof(TX_DESC_ENTRY_t) * iRemainder);

		pAdapter->TxRing.txDmaReadyToSend.bits.val += iRemainder;
	}

	if (pAdapter->TxRing.txDmaReadyToSend.bits.val == 0) {
		if (pAdapter->TxRing.txDmaReadyToSend.value) {
			pMpTcb->WrIndex.value = NUM_DESC_PER_RING_TX - 1;
		} else {
			pMpTcb->WrIndex.value =
			    0x400 | (NUM_DESC_PER_RING_TX - 1);
		}
	} else {
		pMpTcb->WrIndex.value =
		    pAdapter->TxRing.txDmaReadyToSend.value - 1;
	}

	spin_lock_irqsave(&pAdapter->TCBSendQLock, lockflags2);

	if (pAdapter->TxRing.CurrSendTail) {
		pAdapter->TxRing.CurrSendTail->Next = pMpTcb;
	} else {
		pAdapter->TxRing.CurrSendHead = pMpTcb;
	}

	pAdapter->TxRing.CurrSendTail = pMpTcb;

	DBG_ASSERT(pMpTcb->Next == NULL);

	pAdapter->TxRing.nBusySend++;

	spin_unlock_irqrestore(&pAdapter->TCBSendQLock, lockflags2);

	/* Write the new write pointer back to the device. */
	writel(pAdapter->TxRing.txDmaReadyToSend.value,
	       &pAdapter->CSRAddress->txdma.service_request.value);

	/* For Gig only, we use Tx Interrupt coalescing.  Enable the software
	 * timer to wake us up if this packet isn't followed by N more.
	 */
	if (pAdapter->uiLinkSpeed == TRUEPHY_SPEED_1000MBPS) {
		writel(pAdapter->RegistryTxTimeInterval * NANO_IN_A_MICRO,
		       &pAdapter->CSRAddress->global.watchdog_timer);
	}

	spin_unlock_irqrestore(&pAdapter->SendHWLock, lockflags1);

	DBG_TX_LEAVE(et131x_dbginfo);
	return 0;
}

/*
 * NOTE: For now, keep this older version of NICSendPacket around for
 * reference, even though it's not used
 */
#if 0

/**
 * NICSendPacket - NIC specific send handler.
 * @pAdapter: pointer to our adapter
 * @pMpTcb: pointer to MP_TCB
 *
 * Returns 0 on succes, errno on failure.
 *
 * This version of the send routine is designed for version A silicon.
 * Assumption - Send spinlock has been acquired.
 */
static int nic_send_packet(struct et131x_adapter *pAdapter, PMP_TCB pMpTcb)
{
	uint32_t loopIndex, fragIndex, loopEnd;
	uint32_t iSplitFirstElement = 0;
	uint32_t SegmentSize = 0;
	TX_DESC_ENTRY_t CurDesc;
	TX_DESC_ENTRY_t *CurDescPostCopy = NULL;
	uint32_t SlotsAvailable;
	DMA10W_t ServiceComplete;
	unsigned int lockflags1, lockflags2;
	struct sk_buff *pPacket = pMpTcb->Packet;
	uint32_t FragListCount = skb_shinfo(pPacket)->nr_frags + 1;
	struct skb_frag_struct *pFragList = &skb_shinfo(pPacket)->frags[0];

	DBG_TX_ENTER(et131x_dbginfo);

	ServiceComplete.value =
		readl(&pAdapter->CSRAddress->txdma.NewServiceComplete.value);

	/*
	 * Attempt to fix TWO hardware bugs:
	 * 1)  NEVER write an odd number of descriptors.
	 * 2)  If packet length is less than NIC_MIN_PACKET_SIZE, then pad the
	 *     packet to NIC_MIN_PACKET_SIZE bytes by adding a new last
	 *     descriptor IN HALF DUPLEX MODE ONLY
	 * NOTE that (2) interacts with (1).  If the packet is less than
	 * NIC_MIN_PACKET_SIZE bytes then we will append a descriptor.
	 * Therefore if it is even now, it will eventually end up odd, and
	 * so will need adjusting.
	 *
	 * VLAN tags get involved since VLAN tags add another one or two
	 * segments.
	 */
	DBG_TX(et131x_dbginfo,
	       "pMpTcb->PacketLength: %d\n", pMpTcb->PacketLength);

	if ((pAdapter->uiDuplexMode == 0)
	    && (pMpTcb->PacketLength < NIC_MIN_PACKET_SIZE)) {
		DBG_TX(et131x_dbginfo,
		       "HALF DUPLEX mode AND len < MIN_PKT_SIZE\n");
		if ((FragListCount & 0x1) == 0) {
			DBG_TX(et131x_dbginfo,
			       "Even number of descs, split 1st elem\n");
			iSplitFirstElement = 1;
			//SegmentSize = pFragList[0].size / 2;
			SegmentSize = (pPacket->len - pPacket->data_len) / 2;
		}
	} else if (FragListCount & 0x1) {
		DBG_TX(et131x_dbginfo, "Odd number of descs, split 1st elem\n");

		iSplitFirstElement = 1;
		//SegmentSize = pFragList[0].size / 2;
		SegmentSize = (pPacket->len - pPacket->data_len) / 2;
	}

	spin_lock_irqsave(&pAdapter->SendHWLock, lockflags1);

	if (pAdapter->TxRing.txDmaReadyToSend.bits.serv_req_wrap ==
	    ServiceComplete.bits.serv_cpl_wrap) {
		/* The ring hasn't wrapped.  Slots available should be
		 * (RING_SIZE) -  the difference between the two pointers.
		 */
		SlotsAvailable = NUM_DESC_PER_RING_TX -
		    (pAdapter->TxRing.txDmaReadyToSend.bits.serv_req -
		     ServiceComplete.bits.serv_cpl);
	} else {
		/* The ring has wrapped.  Slots available should be the
		 * difference between the two pointers.
		 */
		SlotsAvailable = ServiceComplete.bits.serv_cpl -
		    pAdapter->TxRing.txDmaReadyToSend.bits.serv_req;
	}

	if ((FragListCount + iSplitFirstElement) > SlotsAvailable) {
		DBG_WARNING(et131x_dbginfo,
			    "Not Enough Space in Tx Desc Ring\n");
		spin_unlock_irqrestore(&pAdapter->SendHWLock, lockflags1);
		return -ENOMEM;
	}

	loopEnd = (FragListCount) + iSplitFirstElement;
	fragIndex = 0;

	DBG_TX(et131x_dbginfo,
	       "TCB           : 0x%p\n"
	       "Packet (SKB)  : 0x%p\t Packet->len: %d\t Packet->data_len: %d\n"
	       "FragListCount : %d\t iSplitFirstElement: %d\t loopEnd:%d\n",
	       pMpTcb,
	       pPacket, pPacket->len, pPacket->data_len,
	       FragListCount, iSplitFirstElement, loopEnd);

	for (loopIndex = 0; loopIndex < loopEnd; loopIndex++) {
		if (loopIndex > iSplitFirstElement) {
			fragIndex++;
		}

		DBG_TX(et131x_dbginfo,
		       "In loop, loopIndex: %d\t fragIndex: %d\n", loopIndex,
		       fragIndex);

		/* If there is something in this element, let's get a
		 * descriptor from the ring and get the necessary data
		 */
		DBG_TX(et131x_dbginfo,
		       "Packet Length %d,"
		       "filling desc entry %d\n",
		       pPacket->len,
		       pAdapter->TxRing.txDmaReadyToSend.bits.serv_req);

		// NOTE - Should we do a paranoia check here to make sure the fragment
		// actually has a length? It's HIGHLY unlikely the fragment would
		// contain no data...
		if (1) {
			// NOTE - Currently always getting 32-bit addrs, and dma_addr_t is
			//        only 32-bit, so leave "high" ptr value out for now
			CurDesc.DataBufferPtrHigh = 0;

			CurDesc.word2.value = 0;
			CurDesc.word3.value = 0;

			if (fragIndex == 0) {
				if (iSplitFirstElement) {
					DBG_TX(et131x_dbginfo,
					       "Split first element: YES\n");

					if (loopIndex == 0) {
						DBG_TX(et131x_dbginfo,
						       "Got fragment of length %d, fragIndex: %d\n",
						       pPacket->len -
						       pPacket->data_len,
						       fragIndex);
						DBG_TX(et131x_dbginfo,
						       "SegmentSize: %d\n",
						       SegmentSize);

						CurDesc.word2.bits.
						    length_in_bytes =
						    SegmentSize;
						CurDesc.DataBufferPtrLow =
						    pci_map_single(pAdapter->
								   pdev,
								   pPacket->
								   data,
								   SegmentSize,
								   PCI_DMA_TODEVICE);
						DBG_TX(et131x_dbginfo,
						       "pci_map_single() returns: 0x%08x\n",
						       CurDesc.
						       DataBufferPtrLow);
					} else {
						DBG_TX(et131x_dbginfo,
						       "Got fragment of length %d, fragIndex: %d\n",
						       pPacket->len -
						       pPacket->data_len,
						       fragIndex);
						DBG_TX(et131x_dbginfo,
						       "Leftover Size: %d\n",
						       (pPacket->len -
							pPacket->data_len -
							SegmentSize));

						CurDesc.word2.bits.
						    length_in_bytes =
						    ((pPacket->len -
						      pPacket->data_len) -
						     SegmentSize);
						CurDesc.DataBufferPtrLow =
						    pci_map_single(pAdapter->
								   pdev,
								   (pPacket->
								    data +
								    SegmentSize),
								   (pPacket->
								    len -
								    pPacket->
								    data_len -
								    SegmentSize),
								   PCI_DMA_TODEVICE);
						DBG_TX(et131x_dbginfo,
						       "pci_map_single() returns: 0x%08x\n",
						       CurDesc.
						       DataBufferPtrLow);
					}
				} else {
					DBG_TX(et131x_dbginfo,
					       "Split first element: NO\n");

					CurDesc.word2.bits.length_in_bytes =
					    pPacket->len - pPacket->data_len;

					CurDesc.DataBufferPtrLow =
					    pci_map_single(pAdapter->pdev,
							   pPacket->data,
							   (pPacket->len -
							    pPacket->data_len),
							   PCI_DMA_TODEVICE);
					DBG_TX(et131x_dbginfo,
					       "pci_map_single() returns: 0x%08x\n",
					       CurDesc.DataBufferPtrLow);
				}
			} else {

				CurDesc.word2.bits.length_in_bytes =
				    pFragList[fragIndex - 1].size;
				CurDesc.DataBufferPtrLow =
				    pci_map_page(pAdapter->pdev,
						 pFragList[fragIndex - 1].page,
						 pFragList[fragIndex -
							   1].page_offset,
						 pFragList[fragIndex - 1].size,
						 PCI_DMA_TODEVICE);
				DBG_TX(et131x_dbginfo,
				       "pci_map_page() returns: 0x%08x\n",
				       CurDesc.DataBufferPtrLow);
			}

			if (loopIndex == 0) {
				/* This is the first descriptor of the packet
				 *
				 * Set the "f" bit to indicate this is the
				 * first descriptor in the packet.
				 */
				DBG_TX(et131x_dbginfo,
				       "This is our FIRST descriptor\n");
				CurDesc.word3.bits.f = 1;

				pMpTcb->WrIndexStart =
				    pAdapter->TxRing.txDmaReadyToSend;
			}

			if ((loopIndex == (loopEnd - 1)) &&
			    (pAdapter->uiDuplexMode ||
			     (pMpTcb->PacketLength >= NIC_MIN_PACKET_SIZE))) {
				/* This is the Last descriptor of the packet */
				DBG_TX(et131x_dbginfo,
				       "THIS is our LAST descriptor\n");

				if (pAdapter->uiLinkSpeed ==
				    TRUEPHY_SPEED_1000MBPS) {
					if (++pAdapter->TxRing.
					    TxPacketsSinceLastinterrupt >=
					    pAdapter->RegistryTxNumBuffers) {
						CurDesc.word3.value = 0x5;
						pAdapter->TxRing.
						    TxPacketsSinceLastinterrupt
						    = 0;
					} else {
						CurDesc.word3.value = 0x1;
					}
				} else {
					CurDesc.word3.value = 0x5;
				}

				/* Following index will be used during freeing
				 * of packet
				 */
				pMpTcb->WrIndex =
				    pAdapter->TxRing.txDmaReadyToSend;
				pMpTcb->PacketStaleCount = 0;
			}

			/* Copy the descriptor (filled above) into the
			 * descriptor ring at the next free entry.  Advance
			 * the "next free entry" variable
			 */
			memcpy(pAdapter->TxRing.pTxDescRingVa +
			       pAdapter->TxRing.txDmaReadyToSend.bits.serv_req,
			       &CurDesc, sizeof(TX_DESC_ENTRY_t));

			CurDescPostCopy =
			    pAdapter->TxRing.pTxDescRingVa +
			    pAdapter->TxRing.txDmaReadyToSend.bits.serv_req;

			DBG_TX(et131x_dbginfo,
			       "CURRENT DESCRIPTOR\n"
			       "\tAddress           : 0x%p\n"
			       "\tDataBufferPtrHigh : 0x%08x\n"
			       "\tDataBufferPtrLow  : 0x%08x\n"
			       "\tword2             : 0x%08x\n"
			       "\tword3             : 0x%08x\n",
			       CurDescPostCopy,
			       CurDescPostCopy->DataBufferPtrHigh,
			       CurDescPostCopy->DataBufferPtrLow,
			       CurDescPostCopy->word2.value,
			       CurDescPostCopy->word3.value);

			if (++pAdapter->TxRing.txDmaReadyToSend.bits.serv_req >=
			    NUM_DESC_PER_RING_TX) {
				if (pAdapter->TxRing.txDmaReadyToSend.bits.
				    serv_req_wrap) {
					pAdapter->TxRing.txDmaReadyToSend.
					    value = 0;
				} else {
					pAdapter->TxRing.txDmaReadyToSend.
					    value = 0x400;
				}
			}
		}
	}

	if (pAdapter->uiDuplexMode == 0 &&
	    pMpTcb->PacketLength < NIC_MIN_PACKET_SIZE) {
		// NOTE - Same 32/64-bit issue as above...
		CurDesc.DataBufferPtrHigh = 0x0;
		CurDesc.DataBufferPtrLow = pAdapter->TxRing.pTxDummyBlkPa;
		CurDesc.word2.value = 0;

		if (pAdapter->uiLinkSpeed == TRUEPHY_SPEED_1000MBPS) {
			if (++pAdapter->TxRing.TxPacketsSinceLastinterrupt >=
			    pAdapter->RegistryTxNumBuffers) {
				CurDesc.word3.value = 0x5;
				pAdapter->TxRing.TxPacketsSinceLastinterrupt =
				    0;
			} else {
				CurDesc.word3.value = 0x1;
			}
		} else {
			CurDesc.word3.value = 0x5;
		}

		CurDesc.word2.bits.length_in_bytes =
		    NIC_MIN_PACKET_SIZE - pMpTcb->PacketLength;

		pMpTcb->WrIndex = pAdapter->TxRing.txDmaReadyToSend;

		memcpy(pAdapter->TxRing.pTxDescRingVa +
		       pAdapter->TxRing.txDmaReadyToSend.bits.serv_req,
		       &CurDesc, sizeof(TX_DESC_ENTRY_t));

		CurDescPostCopy =
		    pAdapter->TxRing.pTxDescRingVa +
		    pAdapter->TxRing.txDmaReadyToSend.bits.serv_req;

		DBG_TX(et131x_dbginfo,
		       "CURRENT DESCRIPTOR\n"
		       "\tAddress           : 0x%p\n"
		       "\tDataBufferPtrHigh : 0x%08x\n"
		       "\tDataBufferPtrLow  : 0x%08x\n"
		       "\tword2             : 0x%08x\n"
		       "\tword3             : 0x%08x\n",
		       CurDescPostCopy,
		       CurDescPostCopy->DataBufferPtrHigh,
		       CurDescPostCopy->DataBufferPtrLow,
		       CurDescPostCopy->word2.value,
		       CurDescPostCopy->word3.value);

		if (++pAdapter->TxRing.txDmaReadyToSend.bits.serv_req >=
		    NUM_DESC_PER_RING_TX) {
			if (pAdapter->TxRing.txDmaReadyToSend.bits.
			    serv_req_wrap) {
				pAdapter->TxRing.txDmaReadyToSend.value = 0;
			} else {
				pAdapter->TxRing.txDmaReadyToSend.value = 0x400;
			}
		}

		DBG_TX(et131x_dbginfo, "Padding descriptor %d by %d bytes\n",
		       //pAdapter->TxRing.txDmaReadyToSend.value,
		       pAdapter->TxRing.txDmaReadyToSend.bits.serv_req,
		       NIC_MIN_PACKET_SIZE - pMpTcb->PacketLength);
	}

	spin_lock_irqsave(&pAdapter->TCBSendQLock, lockflags2);

	if (pAdapter->TxRing.CurrSendTail) {
		pAdapter->TxRing.CurrSendTail->Next = pMpTcb;
	} else {
		pAdapter->TxRing.CurrSendHead = pMpTcb;
	}

	pAdapter->TxRing.CurrSendTail = pMpTcb;

	DBG_ASSERT(pMpTcb->Next == NULL);

	pAdapter->TxRing.nBusySend++;

	spin_unlock_irqrestore(&pAdapter->TCBSendQLock, lockflags2);

	/* Write the new write pointer back to the device. */
	writel(pAdapter->TxRing.txDmaReadyToSend.value,
	       &pAdapter->CSRAddress->txdma.service_request.value);

#ifdef CONFIG_ET131X_DEBUG
	DumpDeviceBlock(DBG_TX_ON, pAdapter, 1);
#endif

	/* For Gig only, we use Tx Interrupt coalescing.  Enable the software
	 * timer to wake us up if this packet isn't followed by N more.
	 */
	if (pAdapter->uiLinkSpeed == TRUEPHY_SPEED_1000MBPS) {
		writel(pAdapter->RegistryTxTimeInterval * NANO_IN_A_MICRO,
		       &pAdapter->CSRAddress->global.watchdog_timer);
	}

	spin_unlock_irqrestore(&pAdapter->SendHWLock, lockflags1);

	DBG_TX_LEAVE(et131x_dbginfo);
	return 0;
}

#endif

/**
 * et131x_free_send_packet - Recycle a MP_TCB, complete the packet if necessary
 * @pAdapter: pointer to our adapter
 * @pMpTcb: pointer to MP_TCB
 *
 * Assumption - Send spinlock has been acquired
 */
__inline void et131x_free_send_packet(struct et131x_adapter *pAdapter, PMP_TCB pMpTcb)
{
	unsigned long lockflags;
	TX_DESC_ENTRY_t *desc = NULL;
	struct net_device_stats *stats = &pAdapter->net_stats;

	if (MP_TEST_FLAG(pMpTcb, fMP_DEST_BROAD)) {
		atomic_inc(&pAdapter->Stats.brdcstxmt);
	} else if (MP_TEST_FLAG(pMpTcb, fMP_DEST_MULTI)) {
		atomic_inc(&pAdapter->Stats.multixmt);
	} else {
		atomic_inc(&pAdapter->Stats.unixmt);
	}

	if (pMpTcb->Packet) {
		stats->tx_bytes += pMpTcb->Packet->len;

		/* Iterate through the TX descriptors on the ring
		 * corresponding to this packet and umap the fragments
		 * they point to
		 */
		DBG_TX(et131x_dbginfo,
		       "Unmap descriptors Here\n"
		       "TCB                  : 0x%p\n"
		       "TCB Next             : 0x%p\n"
		       "TCB PacketLength     : %d\n"
		       "TCB WrIndex.value    : 0x%08x\n"
		       "TCB WrIndex.bits.val : %d\n"
		       "TCB WrIndex.value    : 0x%08x\n"
		       "TCB WrIndex.bits.val : %d\n",
		       pMpTcb,
		       pMpTcb->Next,
		       pMpTcb->PacketLength,
		       pMpTcb->WrIndexStart.value,
		       pMpTcb->WrIndexStart.bits.val,
		       pMpTcb->WrIndex.value,
		       pMpTcb->WrIndex.bits.val);

		do {
			desc =
			    (TX_DESC_ENTRY_t *) (pAdapter->TxRing.
						 pTxDescRingVa +
						 pMpTcb->WrIndexStart.bits.val);

			DBG_TX(et131x_dbginfo,
			       "CURRENT DESCRIPTOR\n"
			       "\tAddress           : 0x%p\n"
			       "\tDataBufferPtrHigh : 0x%08x\n"
			       "\tDataBufferPtrLow  : 0x%08x\n"
			       "\tword2             : 0x%08x\n"
			       "\tword3             : 0x%08x\n",
			       desc,
			       desc->DataBufferPtrHigh,
			       desc->DataBufferPtrLow,
			       desc->word2.value,
			       desc->word3.value);

			pci_unmap_single(pAdapter->pdev,
					 desc->DataBufferPtrLow,
					 desc->word2.value, PCI_DMA_TODEVICE);

			if (++pMpTcb->WrIndexStart.bits.val >=
			    NUM_DESC_PER_RING_TX) {
				if (pMpTcb->WrIndexStart.bits.wrap) {
					pMpTcb->WrIndexStart.value = 0;
				} else {
					pMpTcb->WrIndexStart.value = 0x400;
				}
			}
		}
		while (desc != (pAdapter->TxRing.pTxDescRingVa +
				pMpTcb->WrIndex.bits.val));

		DBG_TX(et131x_dbginfo,
		       "Free Packet (SKB)   : 0x%p\n", pMpTcb->Packet);

		dev_kfree_skb_any(pMpTcb->Packet);
	}

	memset(pMpTcb, 0, sizeof(MP_TCB));

	/* Add the TCB to the Ready Q */
	spin_lock_irqsave(&pAdapter->TCBReadyQLock, lockflags);

	pAdapter->Stats.opackets++;

	if (pAdapter->TxRing.TCBReadyQueueTail) {
		pAdapter->TxRing.TCBReadyQueueTail->Next = pMpTcb;
	} else {
		/* Apparently ready Q is empty. */
		pAdapter->TxRing.TCBReadyQueueHead = pMpTcb;
	}

	pAdapter->TxRing.TCBReadyQueueTail = pMpTcb;

	spin_unlock_irqrestore(&pAdapter->TCBReadyQLock, lockflags);

	DBG_ASSERT(pAdapter->TxRing.nBusySend >= 0);
}

/**
 * et131x_free_busy_send_packets - Free and complete the stopped active sends
 * @pAdapter: pointer to our adapter
 *
 * Assumption - Send spinlock has been acquired
 */
void et131x_free_busy_send_packets(struct et131x_adapter *pAdapter)
{
	PMP_TCB pMpTcb;
	struct list_head *pEntry;
	struct sk_buff *pPacket = NULL;
	unsigned long lockflags;
	uint32_t FreeCounter = 0;

	DBG_ENTER(et131x_dbginfo);

	while (!list_empty(&pAdapter->TxRing.SendWaitQueue)) {
		spin_lock_irqsave(&pAdapter->SendWaitLock, lockflags);

		pAdapter->TxRing.nWaitSend--;
		spin_unlock_irqrestore(&pAdapter->SendWaitLock, lockflags);

		pEntry = pAdapter->TxRing.SendWaitQueue.next;

		pPacket = NULL;
	}

	pAdapter->TxRing.nWaitSend = 0;

	/* Any packets being sent? Check the first TCB on the send list */
	spin_lock_irqsave(&pAdapter->TCBSendQLock, lockflags);

	pMpTcb = pAdapter->TxRing.CurrSendHead;

	while ((pMpTcb != NULL) && (FreeCounter < NUM_TCB)) {
		PMP_TCB pNext = pMpTcb->Next;

		pAdapter->TxRing.CurrSendHead = pNext;

		if (pNext == NULL) {
			pAdapter->TxRing.CurrSendTail = NULL;
		}

		pAdapter->TxRing.nBusySend--;

		spin_unlock_irqrestore(&pAdapter->TCBSendQLock, lockflags);

		DBG_VERBOSE(et131x_dbginfo, "pMpTcb = 0x%p\n", pMpTcb);

		FreeCounter++;
		MP_FREE_SEND_PACKET_FUN(pAdapter, pMpTcb);

		spin_lock_irqsave(&pAdapter->TCBSendQLock, lockflags);

		pMpTcb = pAdapter->TxRing.CurrSendHead;
	}

	if (FreeCounter == NUM_TCB) {
		DBG_ERROR(et131x_dbginfo,
			  "MpFreeBusySendPackets exitted loop for a bad reason\n");
		BUG();
	}

	spin_unlock_irqrestore(&pAdapter->TCBSendQLock, lockflags);

	pAdapter->TxRing.nBusySend = 0;

	DBG_LEAVE(et131x_dbginfo);
}

/**
 * et131x_handle_send_interrupt - Interrupt handler for sending processing
 * @pAdapter: pointer to our adapter
 *
 * Re-claim the send resources, complete sends and get more to send from
 * the send wait queue.
 *
 * Assumption - Send spinlock has been acquired
 */
void et131x_handle_send_interrupt(struct et131x_adapter *pAdapter)
{
	DBG_TX_ENTER(et131x_dbginfo);

	/* Mark as completed any packets which have been sent by the device. */
	et131x_update_tcb_list(pAdapter);

	/* If we queued any transmits because we didn't have any TCBs earlier,
	 * dequeue and send those packets now, as long as we have free TCBs.
	 */
	et131x_check_send_wait_list(pAdapter);

	DBG_TX_LEAVE(et131x_dbginfo);
}

/**
 * et131x_update_tcb_list - Helper routine for Send Interrupt handler
 * @pAdapter: pointer to our adapter
 *
 * Re-claims the send resources and completes sends.  Can also be called as
 * part of the NIC send routine when the "ServiceComplete" indication has
 * wrapped.
 */
static void et131x_update_tcb_list(struct et131x_adapter *pAdapter)
{
	unsigned long lockflags;
	DMA10W_t ServiceComplete;
	PMP_TCB pMpTcb;

	ServiceComplete.value =
	    readl(&pAdapter->CSRAddress->txdma.NewServiceComplete.value);

	/* Has the ring wrapped?  Process any descriptors that do not have
	 * the same "wrap" indicator as the current completion indicator
	 */
	spin_lock_irqsave(&pAdapter->TCBSendQLock, lockflags);

	pMpTcb = pAdapter->TxRing.CurrSendHead;
	while (pMpTcb &&
	       ServiceComplete.bits.wrap != pMpTcb->WrIndex.bits.wrap  &&
	       ServiceComplete.bits.val < pMpTcb->WrIndex.bits.val) {
		pAdapter->TxRing.nBusySend--;
		pAdapter->TxRing.CurrSendHead = pMpTcb->Next;
		if (pMpTcb->Next == NULL) {
			pAdapter->TxRing.CurrSendTail = NULL;
		}

		spin_unlock_irqrestore(&pAdapter->TCBSendQLock, lockflags);
		MP_FREE_SEND_PACKET_FUN(pAdapter, pMpTcb);
		spin_lock_irqsave(&pAdapter->TCBSendQLock, lockflags);

		/* Goto the next packet */
		pMpTcb = pAdapter->TxRing.CurrSendHead;
	}
	while (pMpTcb &&
	       ServiceComplete.bits.wrap == pMpTcb->WrIndex.bits.wrap &&
	       ServiceComplete.bits.val > pMpTcb->WrIndex.bits.val) {
		pAdapter->TxRing.nBusySend--;
		pAdapter->TxRing.CurrSendHead = pMpTcb->Next;
		if (pMpTcb->Next == NULL) {
			pAdapter->TxRing.CurrSendTail = NULL;
		}

		spin_unlock_irqrestore(&pAdapter->TCBSendQLock, lockflags);
		MP_FREE_SEND_PACKET_FUN(pAdapter, pMpTcb);
		spin_lock_irqsave(&pAdapter->TCBSendQLock, lockflags);

		/* Goto the next packet */
		pMpTcb = pAdapter->TxRing.CurrSendHead;
	}

	/* Wake up the queue when we hit a low-water mark */
	if (pAdapter->TxRing.nBusySend <= (NUM_TCB / 3)) {
		netif_wake_queue(pAdapter->netdev);
	}

	spin_unlock_irqrestore(&pAdapter->TCBSendQLock, lockflags);
}

/**
 * et131x_check_send_wait_list - Helper routine for the interrupt handler
 * @pAdapter: pointer to our adapter
 *
 * Takes packets from the send wait queue and posts them to the device (if
 * room available).
 */
static void et131x_check_send_wait_list(struct et131x_adapter *pAdapter)
{
	unsigned long lockflags;

	spin_lock_irqsave(&pAdapter->SendWaitLock, lockflags);

	while (!list_empty(&pAdapter->TxRing.SendWaitQueue) &&
	       MP_TCB_RESOURCES_AVAILABLE(pAdapter)) {
		struct list_head *pEntry;

		DBG_VERBOSE(et131x_dbginfo, "Tx packets on the wait queue\n");

		pEntry = pAdapter->TxRing.SendWaitQueue.next;

		pAdapter->TxRing.nWaitSend--;

		DBG_WARNING(et131x_dbginfo,
			    "MpHandleSendInterrupt - sent a queued pkt. Waiting %d\n",
			    pAdapter->TxRing.nWaitSend);
	}

	spin_unlock_irqrestore(&pAdapter->SendWaitLock, lockflags);
}
