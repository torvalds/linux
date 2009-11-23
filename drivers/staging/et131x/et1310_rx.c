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
 * et1310_rx.c - Routines used to perform data reception
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

#include "et1310_rx.h"


void nic_return_rfd(struct et131x_adapter *etdev, PMP_RFD pMpRfd);

/**
 * et131x_rx_dma_memory_alloc
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success and errno on failure (as defined in errno.h)
 *
 * Allocates Free buffer ring 1 for sure, free buffer ring 0 if required,
 * and the Packet Status Ring.
 */
int et131x_rx_dma_memory_alloc(struct et131x_adapter *adapter)
{
	uint32_t OuterLoop, InnerLoop;
	uint32_t bufsize;
	uint32_t pktStatRingSize, FBRChunkSize;
	RX_RING_t *rx_ring;

	/* Setup some convenience pointers */
	rx_ring = (RX_RING_t *) &adapter->RxRing;

	/* Alloc memory for the lookup table */
#ifdef USE_FBR0
	rx_ring->Fbr[0] = kmalloc(sizeof(FBRLOOKUPTABLE), GFP_KERNEL);
#endif

	rx_ring->Fbr[1] = kmalloc(sizeof(FBRLOOKUPTABLE), GFP_KERNEL);

	/* The first thing we will do is configure the sizes of the buffer
	 * rings. These will change based on jumbo packet support.  Larger
	 * jumbo packets increases the size of each entry in FBR0, and the
	 * number of entries in FBR0, while at the same time decreasing the
	 * number of entries in FBR1.
	 *
	 * FBR1 holds "large" frames, FBR0 holds "small" frames.  If FBR1
	 * entries are huge in order to accomodate a "jumbo" frame, then it
	 * will have less entries.  Conversely, FBR1 will now be relied upon
	 * to carry more "normal" frames, thus it's entry size also increases
	 * and the number of entries goes up too (since it now carries
	 * "small" + "regular" packets.
	 *
	 * In this scheme, we try to maintain 512 entries between the two
	 * rings. Also, FBR1 remains a constant size - when it's size doubles
	 * the number of entries halves.  FBR0 increases in size, however.
	 */

	if (adapter->RegistryJumboPacket < 2048) {
#ifdef USE_FBR0
		rx_ring->Fbr0BufferSize = 256;
		rx_ring->Fbr0NumEntries = 512;
#endif
		rx_ring->Fbr1BufferSize = 2048;
		rx_ring->Fbr1NumEntries = 512;
	} else if (adapter->RegistryJumboPacket < 4096) {
#ifdef USE_FBR0
		rx_ring->Fbr0BufferSize = 512;
		rx_ring->Fbr0NumEntries = 1024;
#endif
		rx_ring->Fbr1BufferSize = 4096;
		rx_ring->Fbr1NumEntries = 512;
	} else {
#ifdef USE_FBR0
		rx_ring->Fbr0BufferSize = 1024;
		rx_ring->Fbr0NumEntries = 768;
#endif
		rx_ring->Fbr1BufferSize = 16384;
		rx_ring->Fbr1NumEntries = 128;
	}

#ifdef USE_FBR0
	adapter->RxRing.PsrNumEntries = adapter->RxRing.Fbr0NumEntries +
	    adapter->RxRing.Fbr1NumEntries;
#else
	adapter->RxRing.PsrNumEntries = adapter->RxRing.Fbr1NumEntries;
#endif

	/* Allocate an area of memory for Free Buffer Ring 1 */
	bufsize = (sizeof(FBR_DESC_t) * rx_ring->Fbr1NumEntries) + 0xfff;
	rx_ring->pFbr1RingVa = pci_alloc_consistent(adapter->pdev,
						    bufsize,
						    &rx_ring->pFbr1RingPa);
	if (!rx_ring->pFbr1RingVa) {
		dev_err(&adapter->pdev->dev,
			  "Cannot alloc memory for Free Buffer Ring 1\n");
		return -ENOMEM;
	}

	/* Save physical address
	 *
	 * NOTE: pci_alloc_consistent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here
	 * before storing the adjusted address.
	 */
	rx_ring->Fbr1Realpa = rx_ring->pFbr1RingPa;

	/* Align Free Buffer Ring 1 on a 4K boundary */
	et131x_align_allocated_memory(adapter,
				      &rx_ring->Fbr1Realpa,
				      &rx_ring->Fbr1offset, 0x0FFF);

	rx_ring->pFbr1RingVa = (void *)((uint8_t *) rx_ring->pFbr1RingVa +
					rx_ring->Fbr1offset);

#ifdef USE_FBR0
	/* Allocate an area of memory for Free Buffer Ring 0 */
	bufsize = (sizeof(FBR_DESC_t) * rx_ring->Fbr0NumEntries) + 0xfff;
	rx_ring->pFbr0RingVa = pci_alloc_consistent(adapter->pdev,
						    bufsize,
						    &rx_ring->pFbr0RingPa);
	if (!rx_ring->pFbr0RingVa) {
		dev_err(&adapter->pdev->dev,
			  "Cannot alloc memory for Free Buffer Ring 0\n");
		return -ENOMEM;
	}

	/* Save physical address
	 *
	 * NOTE: pci_alloc_consistent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here before
	 * storing the adjusted address.
	 */
	rx_ring->Fbr0Realpa = rx_ring->pFbr0RingPa;

	/* Align Free Buffer Ring 0 on a 4K boundary */
	et131x_align_allocated_memory(adapter,
				      &rx_ring->Fbr0Realpa,
				      &rx_ring->Fbr0offset, 0x0FFF);

	rx_ring->pFbr0RingVa = (void *)((uint8_t *) rx_ring->pFbr0RingVa +
					rx_ring->Fbr0offset);
#endif

	for (OuterLoop = 0; OuterLoop < (rx_ring->Fbr1NumEntries / FBR_CHUNKS);
	     OuterLoop++) {
		uint64_t Fbr1Offset;
		uint64_t Fbr1TempPa;
		uint32_t Fbr1Align;

		/* This code allocates an area of memory big enough for N
		 * free buffers + (buffer_size - 1) so that the buffers can
		 * be aligned on 4k boundaries.  If each buffer were aligned
		 * to a buffer_size boundary, the effect would be to double
		 * the size of FBR0.  By allocating N buffers at once, we
		 * reduce this overhead.
		 */
		if (rx_ring->Fbr1BufferSize > 4096)
			Fbr1Align = 4096;
		else
			Fbr1Align = rx_ring->Fbr1BufferSize;

		FBRChunkSize =
		    (FBR_CHUNKS * rx_ring->Fbr1BufferSize) + Fbr1Align - 1;
		rx_ring->Fbr1MemVa[OuterLoop] =
		    pci_alloc_consistent(adapter->pdev, FBRChunkSize,
					 &rx_ring->Fbr1MemPa[OuterLoop]);

		if (!rx_ring->Fbr1MemVa[OuterLoop]) {
		dev_err(&adapter->pdev->dev,
				"Could not alloc memory\n");
			return -ENOMEM;
		}

		/* See NOTE in "Save Physical Address" comment above */
		Fbr1TempPa = rx_ring->Fbr1MemPa[OuterLoop];

		et131x_align_allocated_memory(adapter,
					      &Fbr1TempPa,
					      &Fbr1Offset, (Fbr1Align - 1));

		for (InnerLoop = 0; InnerLoop < FBR_CHUNKS; InnerLoop++) {
			uint32_t index = (OuterLoop * FBR_CHUNKS) + InnerLoop;

			/* Save the Virtual address of this index for quick
			 * access later
			 */
			rx_ring->Fbr[1]->Va[index] =
			    (uint8_t *) rx_ring->Fbr1MemVa[OuterLoop] +
			    (InnerLoop * rx_ring->Fbr1BufferSize) + Fbr1Offset;

			/* now store the physical address in the descriptor
			 * so the device can access it
			 */
			rx_ring->Fbr[1]->PAHigh[index] =
			    (uint32_t) (Fbr1TempPa >> 32);
			rx_ring->Fbr[1]->PALow[index] = (uint32_t) Fbr1TempPa;

			Fbr1TempPa += rx_ring->Fbr1BufferSize;

			rx_ring->Fbr[1]->Buffer1[index] =
			    rx_ring->Fbr[1]->Va[index];
			rx_ring->Fbr[1]->Buffer2[index] =
			    rx_ring->Fbr[1]->Va[index] - 4;
		}
	}

#ifdef USE_FBR0
	/* Same for FBR0 (if in use) */
	for (OuterLoop = 0; OuterLoop < (rx_ring->Fbr0NumEntries / FBR_CHUNKS);
	     OuterLoop++) {
		uint64_t Fbr0Offset;
		uint64_t Fbr0TempPa;

		FBRChunkSize = ((FBR_CHUNKS + 1) * rx_ring->Fbr0BufferSize) - 1;
		rx_ring->Fbr0MemVa[OuterLoop] =
		    pci_alloc_consistent(adapter->pdev, FBRChunkSize,
					 &rx_ring->Fbr0MemPa[OuterLoop]);

		if (!rx_ring->Fbr0MemVa[OuterLoop]) {
			dev_err(&adapter->pdev->dev,
				"Could not alloc memory\n");
			return -ENOMEM;
		}

		/* See NOTE in "Save Physical Address" comment above */
		Fbr0TempPa = rx_ring->Fbr0MemPa[OuterLoop];

		et131x_align_allocated_memory(adapter,
					      &Fbr0TempPa,
					      &Fbr0Offset,
					      rx_ring->Fbr0BufferSize - 1);

		for (InnerLoop = 0; InnerLoop < FBR_CHUNKS; InnerLoop++) {
			uint32_t index = (OuterLoop * FBR_CHUNKS) + InnerLoop;

			rx_ring->Fbr[0]->Va[index] =
			    (uint8_t *) rx_ring->Fbr0MemVa[OuterLoop] +
			    (InnerLoop * rx_ring->Fbr0BufferSize) + Fbr0Offset;

			rx_ring->Fbr[0]->PAHigh[index] =
			    (uint32_t) (Fbr0TempPa >> 32);
			rx_ring->Fbr[0]->PALow[index] = (uint32_t) Fbr0TempPa;

			Fbr0TempPa += rx_ring->Fbr0BufferSize;

			rx_ring->Fbr[0]->Buffer1[index] =
			    rx_ring->Fbr[0]->Va[index];
			rx_ring->Fbr[0]->Buffer2[index] =
			    rx_ring->Fbr[0]->Va[index] - 4;
		}
	}
#endif

	/* Allocate an area of memory for FIFO of Packet Status ring entries */
	pktStatRingSize =
	    sizeof(PKT_STAT_DESC_t) * adapter->RxRing.PsrNumEntries;

	rx_ring->pPSRingVa = pci_alloc_consistent(adapter->pdev,
						  pktStatRingSize + 0x0fff,
						  &rx_ring->pPSRingPa);

	if (!rx_ring->pPSRingVa) {
		dev_err(&adapter->pdev->dev,
			  "Cannot alloc memory for Packet Status Ring\n");
		return -ENOMEM;
	}

	/* Save physical address
	 *
	 * NOTE : pci_alloc_consistent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here before
	 * storing the adjusted address.
	 */
	rx_ring->pPSRingRealPa = rx_ring->pPSRingPa;

	/* Align Packet Status Ring on a 4K boundary */
	et131x_align_allocated_memory(adapter,
				      &rx_ring->pPSRingRealPa,
				      &rx_ring->pPSRingOffset, 0x0FFF);

	rx_ring->pPSRingVa = (void *)((uint8_t *) rx_ring->pPSRingVa +
				      rx_ring->pPSRingOffset);

	/* Allocate an area of memory for writeback of status information */
	rx_ring->pRxStatusVa = pci_alloc_consistent(adapter->pdev,
						    sizeof(RX_STATUS_BLOCK_t) +
						    0x7, &rx_ring->pRxStatusPa);
	if (!rx_ring->pRxStatusVa) {
		dev_err(&adapter->pdev->dev,
			  "Cannot alloc memory for Status Block\n");
		return -ENOMEM;
	}

	/* Save physical address */
	rx_ring->RxStatusRealPA = rx_ring->pRxStatusPa;

	/* Align write back on an 8 byte boundary */
	et131x_align_allocated_memory(adapter,
				      &rx_ring->RxStatusRealPA,
				      &rx_ring->RxStatusOffset, 0x07);

	rx_ring->pRxStatusVa = (void *)((uint8_t *) rx_ring->pRxStatusVa +
					rx_ring->RxStatusOffset);
	rx_ring->NumRfd = NIC_DEFAULT_NUM_RFD;

	/* Recv
	 * pci_pool_create initializes a lookaside list. After successful
	 * creation, nonpaged fixed-size blocks can be allocated from and
	 * freed to the lookaside list.
	 * RFDs will be allocated from this pool.
	 */
	rx_ring->RecvLookaside = kmem_cache_create(adapter->netdev->name,
						   sizeof(MP_RFD),
						   0,
						   SLAB_CACHE_DMA |
						   SLAB_HWCACHE_ALIGN,
						   NULL);

	adapter->Flags |= fMP_ADAPTER_RECV_LOOKASIDE;

	/* The RFDs are going to be put on lists later on, so initialize the
	 * lists now.
	 */
	INIT_LIST_HEAD(&rx_ring->RecvList);
	INIT_LIST_HEAD(&rx_ring->RecvPendingList);
	return 0;
}

/**
 * et131x_rx_dma_memory_free - Free all memory allocated within this module.
 * @adapter: pointer to our private adapter structure
 */
void et131x_rx_dma_memory_free(struct et131x_adapter *adapter)
{
	uint32_t index;
	uint32_t bufsize;
	uint32_t pktStatRingSize;
	PMP_RFD pMpRfd;
	RX_RING_t *rx_ring;

	/* Setup some convenience pointers */
	rx_ring = (RX_RING_t *) &adapter->RxRing;

	/* Free RFDs and associated packet descriptors */
	WARN_ON(rx_ring->nReadyRecv != rx_ring->NumRfd);

	while (!list_empty(&rx_ring->RecvList)) {
		pMpRfd = (MP_RFD *) list_entry(rx_ring->RecvList.next,
					       MP_RFD, list_node);

		list_del(&pMpRfd->list_node);
		et131x_rfd_resources_free(adapter, pMpRfd);
	}

	while (!list_empty(&rx_ring->RecvPendingList)) {
		pMpRfd = (MP_RFD *) list_entry(rx_ring->RecvPendingList.next,
					       MP_RFD, list_node);
		list_del(&pMpRfd->list_node);
		et131x_rfd_resources_free(adapter, pMpRfd);
	}

	/* Free Free Buffer Ring 1 */
	if (rx_ring->pFbr1RingVa) {
		/* First the packet memory */
		for (index = 0; index <
		     (rx_ring->Fbr1NumEntries / FBR_CHUNKS); index++) {
			if (rx_ring->Fbr1MemVa[index]) {
				uint32_t Fbr1Align;

				if (rx_ring->Fbr1BufferSize > 4096)
					Fbr1Align = 4096;
				else
					Fbr1Align = rx_ring->Fbr1BufferSize;

				bufsize =
				    (rx_ring->Fbr1BufferSize * FBR_CHUNKS) +
				    Fbr1Align - 1;

				pci_free_consistent(adapter->pdev,
						    bufsize,
						    rx_ring->Fbr1MemVa[index],
						    rx_ring->Fbr1MemPa[index]);

				rx_ring->Fbr1MemVa[index] = NULL;
			}
		}

		/* Now the FIFO itself */
		rx_ring->pFbr1RingVa = (void *)((uint8_t *)
				rx_ring->pFbr1RingVa - rx_ring->Fbr1offset);

		bufsize =
		    (sizeof(FBR_DESC_t) * rx_ring->Fbr1NumEntries) + 0xfff;

		pci_free_consistent(adapter->pdev,
				    bufsize,
				    rx_ring->pFbr1RingVa, rx_ring->pFbr1RingPa);

		rx_ring->pFbr1RingVa = NULL;
	}

#ifdef USE_FBR0
	/* Now the same for Free Buffer Ring 0 */
	if (rx_ring->pFbr0RingVa) {
		/* First the packet memory */
		for (index = 0; index <
		     (rx_ring->Fbr0NumEntries / FBR_CHUNKS); index++) {
			if (rx_ring->Fbr0MemVa[index]) {
				bufsize =
				    (rx_ring->Fbr0BufferSize *
				     (FBR_CHUNKS + 1)) - 1;

				pci_free_consistent(adapter->pdev,
						    bufsize,
						    rx_ring->Fbr0MemVa[index],
						    rx_ring->Fbr0MemPa[index]);

				rx_ring->Fbr0MemVa[index] = NULL;
			}
		}

		/* Now the FIFO itself */
		rx_ring->pFbr0RingVa = (void *)((uint8_t *)
				rx_ring->pFbr0RingVa - rx_ring->Fbr0offset);

		bufsize =
		    (sizeof(FBR_DESC_t) * rx_ring->Fbr0NumEntries) + 0xfff;

		pci_free_consistent(adapter->pdev,
				    bufsize,
				    rx_ring->pFbr0RingVa, rx_ring->pFbr0RingPa);

		rx_ring->pFbr0RingVa = NULL;
	}
#endif

	/* Free Packet Status Ring */
	if (rx_ring->pPSRingVa) {
		rx_ring->pPSRingVa = (void *)((uint8_t *) rx_ring->pPSRingVa -
					      rx_ring->pPSRingOffset);

		pktStatRingSize =
		    sizeof(PKT_STAT_DESC_t) * adapter->RxRing.PsrNumEntries;

		pci_free_consistent(adapter->pdev,
				    pktStatRingSize + 0x0fff,
				    rx_ring->pPSRingVa, rx_ring->pPSRingPa);

		rx_ring->pPSRingVa = NULL;
	}

	/* Free area of memory for the writeback of status information */
	if (rx_ring->pRxStatusVa) {
		rx_ring->pRxStatusVa = (void *)((uint8_t *)
				rx_ring->pRxStatusVa - rx_ring->RxStatusOffset);

		pci_free_consistent(adapter->pdev,
				sizeof(RX_STATUS_BLOCK_t) + 0x7,
				rx_ring->pRxStatusVa, rx_ring->pRxStatusPa);

		rx_ring->pRxStatusVa = NULL;
	}

	/* Free receive buffer pool */

	/* Free receive packet pool */

	/* Destroy the lookaside (RFD) pool */
	if (adapter->Flags & fMP_ADAPTER_RECV_LOOKASIDE) {
		kmem_cache_destroy(rx_ring->RecvLookaside);
		adapter->Flags &= ~fMP_ADAPTER_RECV_LOOKASIDE;
	}

	/* Free the FBR Lookup Table */
#ifdef USE_FBR0
	kfree(rx_ring->Fbr[0]);
#endif

	kfree(rx_ring->Fbr[1]);

	/* Reset Counters */
	rx_ring->nReadyRecv = 0;
}

/**
 * et131x_init_recv - Initialize receive data structures.
 * @adapter: pointer to our private adapter structure
 *
 * Returns 0 on success and errno on failure (as defined in errno.h)
 */
int et131x_init_recv(struct et131x_adapter *adapter)
{
	int status = -ENOMEM;
	PMP_RFD pMpRfd = NULL;
	uint32_t RfdCount;
	uint32_t TotalNumRfd = 0;
	RX_RING_t *rx_ring = NULL;

	/* Setup some convenience pointers */
	rx_ring = (RX_RING_t *) &adapter->RxRing;

	/* Setup each RFD */
	for (RfdCount = 0; RfdCount < rx_ring->NumRfd; RfdCount++) {
		pMpRfd = (MP_RFD *) kmem_cache_alloc(rx_ring->RecvLookaside,
						     GFP_ATOMIC | GFP_DMA);

		if (!pMpRfd) {
			dev_err(&adapter->pdev->dev,
				  "Couldn't alloc RFD out of kmem_cache\n");
			status = -ENOMEM;
			continue;
		}

		status = et131x_rfd_resources_alloc(adapter, pMpRfd);
		if (status != 0) {
			dev_err(&adapter->pdev->dev,
				  "Couldn't alloc packet for RFD\n");
			kmem_cache_free(rx_ring->RecvLookaside, pMpRfd);
			continue;
		}

		/* Add this RFD to the RecvList */
		list_add_tail(&pMpRfd->list_node, &rx_ring->RecvList);

		/* Increment both the available RFD's, and the total RFD's. */
		rx_ring->nReadyRecv++;
		TotalNumRfd++;
	}

	if (TotalNumRfd > NIC_MIN_NUM_RFD)
		status = 0;

	rx_ring->NumRfd = TotalNumRfd;

	if (status != 0) {
		kmem_cache_free(rx_ring->RecvLookaside, pMpRfd);
		dev_err(&adapter->pdev->dev,
			  "Allocation problems in et131x_init_recv\n");
	}
	return status;
}

/**
 * et131x_rfd_resources_alloc
 * @adapter: pointer to our private adapter structure
 * @pMpRfd: pointer to a RFD
 *
 * Returns 0 on success and errno on failure (as defined in errno.h)
 */
int et131x_rfd_resources_alloc(struct et131x_adapter *adapter, MP_RFD *pMpRfd)
{
	pMpRfd->Packet = NULL;

	return 0;
}

/**
 * et131x_rfd_resources_free - Free the packet allocated for the given RFD
 * @adapter: pointer to our private adapter structure
 * @pMpRfd: pointer to a RFD
 */
void et131x_rfd_resources_free(struct et131x_adapter *adapter, MP_RFD *pMpRfd)
{
	pMpRfd->Packet = NULL;
	kmem_cache_free(adapter->RxRing.RecvLookaside, pMpRfd);
}

/**
 * ConfigRxDmaRegs - Start of Rx_DMA init sequence
 * @etdev: pointer to our adapter structure
 */
void ConfigRxDmaRegs(struct et131x_adapter *etdev)
{
	struct _RXDMA_t __iomem *rx_dma = &etdev->regs->rxdma;
	struct _rx_ring_t *pRxLocal = &etdev->RxRing;
	PFBR_DESC_t fbr_entry;
	uint32_t entry;
	RXDMA_PSR_NUM_DES_t psr_num_des;
	unsigned long flags;

	/* Halt RXDMA to perform the reconfigure.  */
	et131x_rx_dma_disable(etdev);

	/* Load the completion writeback physical address
	 *
	 * NOTE : pci_alloc_consistent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here
	 * before storing the adjusted address.
	 */
	writel((uint32_t) (pRxLocal->RxStatusRealPA >> 32),
	       &rx_dma->dma_wb_base_hi);
	writel((uint32_t) pRxLocal->RxStatusRealPA, &rx_dma->dma_wb_base_lo);

	memset(pRxLocal->pRxStatusVa, 0, sizeof(RX_STATUS_BLOCK_t));

	/* Set the address and parameters of the packet status ring into the
	 * 1310's registers
	 */
	writel((uint32_t) (pRxLocal->pPSRingRealPa >> 32),
	       &rx_dma->psr_base_hi);
	writel((uint32_t) pRxLocal->pPSRingRealPa, &rx_dma->psr_base_lo);
	writel(pRxLocal->PsrNumEntries - 1, &rx_dma->psr_num_des.value);
	writel(0, &rx_dma->psr_full_offset.value);

	psr_num_des.value = readl(&rx_dma->psr_num_des.value);
	writel((psr_num_des.bits.psr_ndes * LO_MARK_PERCENT_FOR_PSR) / 100,
	       &rx_dma->psr_min_des.value);

	spin_lock_irqsave(&etdev->RcvLock, flags);

	/* These local variables track the PSR in the adapter structure */
	pRxLocal->local_psr_full.bits.psr_full = 0;
	pRxLocal->local_psr_full.bits.psr_full_wrap = 0;

	/* Now's the best time to initialize FBR1 contents */
	fbr_entry = (PFBR_DESC_t) pRxLocal->pFbr1RingVa;
	for (entry = 0; entry < pRxLocal->Fbr1NumEntries; entry++) {
		fbr_entry->addr_hi = pRxLocal->Fbr[1]->PAHigh[entry];
		fbr_entry->addr_lo = pRxLocal->Fbr[1]->PALow[entry];
		fbr_entry->word2.bits.bi = entry;
		fbr_entry++;
	}

	/* Set the address and parameters of Free buffer ring 1 (and 0 if
	 * required) into the 1310's registers
	 */
	writel((uint32_t) (pRxLocal->Fbr1Realpa >> 32), &rx_dma->fbr1_base_hi);
	writel((uint32_t) pRxLocal->Fbr1Realpa, &rx_dma->fbr1_base_lo);
	writel(pRxLocal->Fbr1NumEntries - 1, &rx_dma->fbr1_num_des.value);
	writel(ET_DMA10_WRAP, &rx_dma->fbr1_full_offset);

	/* This variable tracks the free buffer ring 1 full position, so it
	 * has to match the above.
	 */
	pRxLocal->local_Fbr1_full = ET_DMA10_WRAP;
	writel(((pRxLocal->Fbr1NumEntries * LO_MARK_PERCENT_FOR_RX) / 100) - 1,
	       &rx_dma->fbr1_min_des.value);

#ifdef USE_FBR0
	/* Now's the best time to initialize FBR0 contents */
	fbr_entry = (PFBR_DESC_t) pRxLocal->pFbr0RingVa;
	for (entry = 0; entry < pRxLocal->Fbr0NumEntries; entry++) {
		fbr_entry->addr_hi = pRxLocal->Fbr[0]->PAHigh[entry];
		fbr_entry->addr_lo = pRxLocal->Fbr[0]->PALow[entry];
		fbr_entry->word2.bits.bi = entry;
		fbr_entry++;
	}

	writel((uint32_t) (pRxLocal->Fbr0Realpa >> 32), &rx_dma->fbr0_base_hi);
	writel((uint32_t) pRxLocal->Fbr0Realpa, &rx_dma->fbr0_base_lo);
	writel(pRxLocal->Fbr0NumEntries - 1, &rx_dma->fbr0_num_des.value);
	writel(ET_DMA10_WRAP, &rx_dma->fbr0_full_offset);

	/* This variable tracks the free buffer ring 0 full position, so it
	 * has to match the above.
	 */
	pRxLocal->local_Fbr0_full = ET_DMA10_WRAP;
	writel(((pRxLocal->Fbr0NumEntries * LO_MARK_PERCENT_FOR_RX) / 100) - 1,
	       &rx_dma->fbr0_min_des.value);
#endif

	/* Program the number of packets we will receive before generating an
	 * interrupt.
	 * For version B silicon, this value gets updated once autoneg is
	 *complete.
	 */
	writel(PARM_RX_NUM_BUFS_DEF, &rx_dma->num_pkt_done.value);

	/* The "time_done" is not working correctly to coalesce interrupts
	 * after a given time period, but rather is giving us an interrupt
	 * regardless of whether we have received packets.
	 * This value gets updated once autoneg is complete.
	 */
	writel(PARM_RX_TIME_INT_DEF, &rx_dma->max_pkt_time.value);

	spin_unlock_irqrestore(&etdev->RcvLock, flags);
}

/**
 * SetRxDmaTimer - Set the heartbeat timer according to line rate.
 * @etdev: pointer to our adapter structure
 */
void SetRxDmaTimer(struct et131x_adapter *etdev)
{
	/* For version B silicon, we do not use the RxDMA timer for 10 and 100
	 * Mbits/s line rates. We do not enable and RxDMA interrupt coalescing.
	 */
	if ((etdev->linkspeed == TRUEPHY_SPEED_100MBPS) ||
	    (etdev->linkspeed == TRUEPHY_SPEED_10MBPS)) {
		writel(0, &etdev->regs->rxdma.max_pkt_time.value);
		writel(1, &etdev->regs->rxdma.num_pkt_done.value);
	}
}

/**
 * et131x_rx_dma_disable - Stop of Rx_DMA on the ET1310
 * @etdev: pointer to our adapter structure
 */
void et131x_rx_dma_disable(struct et131x_adapter *etdev)
{
	RXDMA_CSR_t csr;

	/* Setup the receive dma configuration register */
	writel(0x00002001, &etdev->regs->rxdma.csr.value);
	csr.value = readl(&etdev->regs->rxdma.csr.value);
	if (csr.bits.halt_status != 1) {
		udelay(5);
		csr.value = readl(&etdev->regs->rxdma.csr.value);
		if (csr.bits.halt_status != 1)
			dev_err(&etdev->pdev->dev,
				"RX Dma failed to enter halt state. CSR 0x%08x\n",
				csr.value);
	}
}

/**
 * et131x_rx_dma_enable - re-start of Rx_DMA on the ET1310.
 * @etdev: pointer to our adapter structure
 */
void et131x_rx_dma_enable(struct et131x_adapter *etdev)
{
	if (etdev->RegistryPhyLoopbk)
		/* RxDMA is disabled for loopback operation. */
		writel(0x1, &etdev->regs->rxdma.csr.value);
	else {
	/* Setup the receive dma configuration register for normal operation */
		RXDMA_CSR_t csr = { 0 };

		csr.bits.fbr1_enable = 1;
		if (etdev->RxRing.Fbr1BufferSize == 4096)
			csr.bits.fbr1_size = 1;
		else if (etdev->RxRing.Fbr1BufferSize == 8192)
			csr.bits.fbr1_size = 2;
		else if (etdev->RxRing.Fbr1BufferSize == 16384)
			csr.bits.fbr1_size = 3;
#ifdef USE_FBR0
		csr.bits.fbr0_enable = 1;
		if (etdev->RxRing.Fbr0BufferSize == 256)
			csr.bits.fbr0_size = 1;
		else if (etdev->RxRing.Fbr0BufferSize == 512)
			csr.bits.fbr0_size = 2;
		else if (etdev->RxRing.Fbr0BufferSize == 1024)
			csr.bits.fbr0_size = 3;
#endif
		writel(csr.value, &etdev->regs->rxdma.csr.value);

		csr.value = readl(&etdev->regs->rxdma.csr.value);
		if (csr.bits.halt_status != 0) {
			udelay(5);
			csr.value = readl(&etdev->regs->rxdma.csr.value);
			if (csr.bits.halt_status != 0) {
				dev_err(&etdev->pdev->dev,
					"RX Dma failed to exit halt state.  CSR 0x%08x\n",
					csr.value);
			}
		}
	}
}

/**
 * nic_rx_pkts - Checks the hardware for available packets
 * @etdev: pointer to our adapter
 *
 * Returns pMpRfd, a pointer to our MPRFD.
 *
 * Checks the hardware for available packets, using completion ring
 * If packets are available, it gets an RFD from the RecvList, attaches
 * the packet to it, puts the RFD in the RecvPendList, and also returns
 * the pointer to the RFD.
 */
PMP_RFD nic_rx_pkts(struct et131x_adapter *etdev)
{
	struct _rx_ring_t *pRxLocal = &etdev->RxRing;
	PRX_STATUS_BLOCK_t pRxStatusBlock;
	PPKT_STAT_DESC_t pPSREntry;
	PMP_RFD pMpRfd;
	uint32_t nIndex;
	uint8_t *pBufVa;
	unsigned long flags;
	struct list_head *element;
	uint8_t ringIndex;
	uint16_t bufferIndex;
	uint32_t localLen;
	PKT_STAT_DESC_WORD0_t Word0;

	/* RX Status block is written by the DMA engine prior to every
	 * interrupt. It contains the next to be used entry in the Packet
	 * Status Ring, and also the two Free Buffer rings.
	 */
	pRxStatusBlock = (PRX_STATUS_BLOCK_t) pRxLocal->pRxStatusVa;

	if (pRxStatusBlock->Word1.bits.PSRoffset ==
			pRxLocal->local_psr_full.bits.psr_full &&
			pRxStatusBlock->Word1.bits.PSRwrap ==
			pRxLocal->local_psr_full.bits.psr_full_wrap) {
		/* Looks like this ring is not updated yet */
		return NULL;
	}

	/* The packet status ring indicates that data is available. */
	pPSREntry = (PPKT_STAT_DESC_t) (pRxLocal->pPSRingVa) +
			pRxLocal->local_psr_full.bits.psr_full;

	/* Grab any information that is required once the PSR is
	 * advanced, since we can no longer rely on the memory being
	 * accurate
	 */
	localLen = pPSREntry->word1.bits.length;
	ringIndex = (uint8_t) pPSREntry->word1.bits.ri;
	bufferIndex = (uint16_t) pPSREntry->word1.bits.bi;
	Word0 = pPSREntry->word0;

	/* Indicate that we have used this PSR entry. */
	if (++pRxLocal->local_psr_full.bits.psr_full >
	    pRxLocal->PsrNumEntries - 1) {
		pRxLocal->local_psr_full.bits.psr_full = 0;
		pRxLocal->local_psr_full.bits.psr_full_wrap ^= 1;
	}

	writel(pRxLocal->local_psr_full.value,
	       &etdev->regs->rxdma.psr_full_offset.value);

#ifndef USE_FBR0
	if (ringIndex != 1) {
		return NULL;
	}
#endif

#ifdef USE_FBR0
	if (ringIndex > 1 ||
		(ringIndex == 0 &&
		bufferIndex > pRxLocal->Fbr0NumEntries - 1) ||
		(ringIndex == 1 &&
		bufferIndex > pRxLocal->Fbr1NumEntries - 1))
#else
	if (ringIndex != 1 ||
		bufferIndex > pRxLocal->Fbr1NumEntries - 1)
#endif
	{
		/* Illegal buffer or ring index cannot be used by S/W*/
		dev_err(&etdev->pdev->dev,
			  "NICRxPkts PSR Entry %d indicates "
			  "length of %d and/or bad bi(%d)\n",
			  pRxLocal->local_psr_full.bits.psr_full,
			  localLen, bufferIndex);
		return NULL;
	}

	/* Get and fill the RFD. */
	spin_lock_irqsave(&etdev->RcvLock, flags);

	pMpRfd = NULL;
	element = pRxLocal->RecvList.next;
	pMpRfd = (PMP_RFD) list_entry(element, MP_RFD, list_node);

	if (pMpRfd == NULL) {
		spin_unlock_irqrestore(&etdev->RcvLock, flags);
		return NULL;
	}

	list_del(&pMpRfd->list_node);
	pRxLocal->nReadyRecv--;

	spin_unlock_irqrestore(&etdev->RcvLock, flags);

	pMpRfd->bufferindex = bufferIndex;
	pMpRfd->ringindex = ringIndex;

	/* In V1 silicon, there is a bug which screws up filtering of
	 * runt packets.  Therefore runt packet filtering is disabled
	 * in the MAC and the packets are dropped here.  They are
	 * also counted here.
	 */
	if (localLen < (NIC_MIN_PACKET_SIZE + 4)) {
		etdev->Stats.other_errors++;
		localLen = 0;
	}

	if (localLen) {
		if (etdev->ReplicaPhyLoopbk == 1) {
			pBufVa = pRxLocal->Fbr[ringIndex]->Va[bufferIndex];

			if (memcmp(&pBufVa[6], &etdev->CurrentAddress[0],
				   ETH_ALEN) == 0) {
				if (memcmp(&pBufVa[42], "Replica packet",
					   ETH_HLEN)) {
					etdev->ReplicaPhyLoopbkPF = 1;
				}
			}
		}

		/* Determine if this is a multicast packet coming in */
		if ((Word0.value & ALCATEL_MULTICAST_PKT) &&
		    !(Word0.value & ALCATEL_BROADCAST_PKT)) {
			/* Promiscuous mode and Multicast mode are
			 * not mutually exclusive as was first
			 * thought.  I guess Promiscuous is just
			 * considered a super-set of the other
			 * filters. Generally filter is 0x2b when in
			 * promiscuous mode.
			 */
			if ((etdev->PacketFilter & ET131X_PACKET_TYPE_MULTICAST)
			    && !(etdev->PacketFilter & ET131X_PACKET_TYPE_PROMISCUOUS)
			    && !(etdev->PacketFilter & ET131X_PACKET_TYPE_ALL_MULTICAST)) {
				pBufVa = pRxLocal->Fbr[ringIndex]->
						Va[bufferIndex];

				/* Loop through our list to see if the
				 * destination address of this packet
				 * matches one in our list.
				 */
				for (nIndex = 0;
				     nIndex < etdev->MCAddressCount;
				     nIndex++) {
					if (pBufVa[0] ==
					    etdev->MCList[nIndex][0]
					    && pBufVa[1] ==
					    etdev->MCList[nIndex][1]
					    && pBufVa[2] ==
					    etdev->MCList[nIndex][2]
					    && pBufVa[3] ==
					    etdev->MCList[nIndex][3]
					    && pBufVa[4] ==
					    etdev->MCList[nIndex][4]
					    && pBufVa[5] ==
					    etdev->MCList[nIndex][5]) {
						break;
					}
				}

				/* If our index is equal to the number
				 * of Multicast address we have, then
				 * this means we did not find this
				 * packet's matching address in our
				 * list.  Set the PacketSize to zero,
				 * so we free our RFD when we return
				 * from this function.
				 */
				if (nIndex == etdev->MCAddressCount)
					localLen = 0;
			}

			if (localLen > 0)
				etdev->Stats.multircv++;
		} else if (Word0.value & ALCATEL_BROADCAST_PKT)
			etdev->Stats.brdcstrcv++;
		else
			/* Not sure what this counter measures in
			 * promiscuous mode. Perhaps we should check
			 * the MAC address to see if it is directed
			 * to us in promiscuous mode.
			 */
			etdev->Stats.unircv++;
	}

	if (localLen > 0) {
		struct sk_buff *skb = NULL;

		/* pMpRfd->PacketSize = localLen - 4; */
		pMpRfd->PacketSize = localLen;

		skb = dev_alloc_skb(pMpRfd->PacketSize + 2);
		if (!skb) {
			dev_err(&etdev->pdev->dev,
				  "Couldn't alloc an SKB for Rx\n");
			return NULL;
		}

		etdev->net_stats.rx_bytes += pMpRfd->PacketSize;

		memcpy(skb_put(skb, pMpRfd->PacketSize),
		       pRxLocal->Fbr[ringIndex]->Va[bufferIndex],
		       pMpRfd->PacketSize);

		skb->dev = etdev->netdev;
		skb->protocol = eth_type_trans(skb, etdev->netdev);
		skb->ip_summed = CHECKSUM_NONE;

		netif_rx(skb);
	} else {
		pMpRfd->PacketSize = 0;
	}

	nic_return_rfd(etdev, pMpRfd);
	return pMpRfd;
}

/**
 * et131x_reset_recv - Reset the receive list
 * @etdev: pointer to our adapter
 *
 * Assumption, Rcv spinlock has been acquired.
 */
void et131x_reset_recv(struct et131x_adapter *etdev)
{
	PMP_RFD pMpRfd;
	struct list_head *element;

	WARN_ON(list_empty(&etdev->RxRing.RecvList));

	/* Take all the RFD's from the pending list, and stick them on the
	 * RecvList.
	 */
	while (!list_empty(&etdev->RxRing.RecvPendingList)) {
		element = etdev->RxRing.RecvPendingList.next;

		pMpRfd = (PMP_RFD) list_entry(element, MP_RFD, list_node);

		list_move_tail(&pMpRfd->list_node, &etdev->RxRing.RecvList);
	}
}

/**
 * et131x_handle_recv_interrupt - Interrupt handler for receive processing
 * @etdev: pointer to our adapter
 *
 * Assumption, Rcv spinlock has been acquired.
 */
void et131x_handle_recv_interrupt(struct et131x_adapter *etdev)
{
	PMP_RFD pMpRfd = NULL;
	struct sk_buff *PacketArray[NUM_PACKETS_HANDLED];
	PMP_RFD RFDFreeArray[NUM_PACKETS_HANDLED];
	uint32_t PacketArrayCount = 0;
	uint32_t PacketsToHandle;
	uint32_t PacketFreeCount = 0;
	bool TempUnfinishedRec = false;

	PacketsToHandle = NUM_PACKETS_HANDLED;

	/* Process up to available RFD's */
	while (PacketArrayCount < PacketsToHandle) {
		if (list_empty(&etdev->RxRing.RecvList)) {
			WARN_ON(etdev->RxRing.nReadyRecv != 0);
			TempUnfinishedRec = true;
			break;
		}

		pMpRfd = nic_rx_pkts(etdev);

		if (pMpRfd == NULL)
			break;

		/* Do not receive any packets until a filter has been set.
		 * Do not receive any packets until we have link.
		 * If length is zero, return the RFD in order to advance the
		 * Free buffer ring.
		 */
		if (!etdev->PacketFilter ||
		    !(etdev->Flags & fMP_ADAPTER_LINK_DETECTION) ||
		    pMpRfd->PacketSize == 0) {
			continue;
		}

		/* Increment the number of packets we received */
		etdev->Stats.ipackets++;

		/* Set the status on the packet, either resources or success */
		if (etdev->RxRing.nReadyRecv >= RFD_LOW_WATER_MARK) {
			/* Put this RFD on the pending list
			 *
			 * NOTE: nic_rx_pkts() above is already returning the
			 * RFD to the RecvList, so don't additionally do that
			 * here.
			 * Besides, we don't really need (at this point) the
			 * pending list anyway.
			 */
		} else {
			RFDFreeArray[PacketFreeCount] = pMpRfd;
			PacketFreeCount++;

			dev_warn(&etdev->pdev->dev,
				    "RFD's are running out\n");
		}

		PacketArray[PacketArrayCount] = pMpRfd->Packet;
		PacketArrayCount++;
	}

	if ((PacketArrayCount == NUM_PACKETS_HANDLED) || TempUnfinishedRec) {
		etdev->RxRing.UnfinishedReceives = true;
		writel(PARM_TX_TIME_INT_DEF * NANO_IN_A_MICRO,
		       &etdev->regs->global.watchdog_timer);
	} else {
		/* Watchdog timer will disable itself if appropriate. */
		etdev->RxRing.UnfinishedReceives = false;
	}
}

static inline u32 bump_fbr(u32 *fbr, u32 limit)
{
        u32 v = *fbr;
        v++;
        /* This works for all cases where limit < 1024. The 1023 case
           works because 1023++ is 1024 which means the if condition is not
           taken but the carry of the bit into the wrap bit toggles the wrap
           value correctly */
        if ((v & ET_DMA10_MASK) > limit) {
                v &= ~ET_DMA10_MASK;
                v ^= ET_DMA10_WRAP;
        }
        /* For the 1023 case */
        v &= (ET_DMA10_MASK|ET_DMA10_WRAP);
        *fbr = v;
        return v;
}

/**
 * NICReturnRFD - Recycle a RFD and put it back onto the receive list
 * @etdev: pointer to our adapter
 * @pMpRfd: pointer to the RFD
 */
void nic_return_rfd(struct et131x_adapter *etdev, PMP_RFD pMpRfd)
{
	struct _rx_ring_t *rx_local = &etdev->RxRing;
	struct _RXDMA_t __iomem *rx_dma = &etdev->regs->rxdma;
	uint16_t bi = pMpRfd->bufferindex;
	uint8_t ri = pMpRfd->ringindex;
	unsigned long flags;

	/* We don't use any of the OOB data besides status. Otherwise, we
	 * need to clean up OOB data
	 */
	if (
#ifdef USE_FBR0
	    (ri == 0 && bi < rx_local->Fbr0NumEntries) ||
#endif
	    (ri == 1 && bi < rx_local->Fbr1NumEntries)) {
		spin_lock_irqsave(&etdev->FbrLock, flags);

		if (ri == 1) {
			PFBR_DESC_t pNextDesc =
			    (PFBR_DESC_t) (rx_local->pFbr1RingVa) +
			    INDEX10(rx_local->local_Fbr1_full);

			/* Handle the Free Buffer Ring advancement here. Write
			 * the PA / Buffer Index for the returned buffer into
			 * the oldest (next to be freed)FBR entry
			 */
			pNextDesc->addr_hi = rx_local->Fbr[1]->PAHigh[bi];
			pNextDesc->addr_lo = rx_local->Fbr[1]->PALow[bi];
			pNextDesc->word2.value = bi;

			writel(bump_fbr(&rx_local->local_Fbr1_full,
				rx_local->Fbr1NumEntries - 1),
				&rx_dma->fbr1_full_offset);
		}
#ifdef USE_FBR0
		else {
			PFBR_DESC_t pNextDesc =
			    (PFBR_DESC_t) rx_local->pFbr0RingVa +
			    INDEX10(rx_local->local_Fbr0_full);

			/* Handle the Free Buffer Ring advancement here. Write
			 * the PA / Buffer Index for the returned buffer into
			 * the oldest (next to be freed) FBR entry
			 */
			pNextDesc->addr_hi = rx_local->Fbr[0]->PAHigh[bi];
			pNextDesc->addr_lo = rx_local->Fbr[0]->PALow[bi];
			pNextDesc->word2.value = bi;

			writel(bump_fbr(&rx_local->local_Fbr0_full,
					rx_local->Fbr0NumEntries - 1),
			       &rx_dma->fbr0_full_offset);
		}
#endif
		spin_unlock_irqrestore(&etdev->FbrLock, flags);
	} else {
		dev_err(&etdev->pdev->dev,
			  "NICReturnRFD illegal Buffer Index returned\n");
	}

	/* The processing on this RFD is done, so put it back on the tail of
	 * our list
	 */
	spin_lock_irqsave(&etdev->RcvLock, flags);
	list_add_tail(&pMpRfd->list_node, &rx_local->RecvList);
	rx_local->nReadyRecv++;
	spin_unlock_irqrestore(&etdev->RcvLock, flags);

	WARN_ON(rx_local->nReadyRecv > rx_local->NumRfd);
}
