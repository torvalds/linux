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
#include "et131x_adapter.h"
#include "et1310_rx.h"
#include "et131x.h"

void nic_return_rfd(struct et131x_adapter *etdev, struct rfd *rfd);

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
	u32 i, j;
	u32 bufsize;
	u32 pktStatRingSize, FBRChunkSize;
	struct rx_ring *rx_ring;

	/* Setup some convenience pointers */
	rx_ring = &adapter->rx_ring;

	/* Alloc memory for the lookup table */
#ifdef USE_FBR0
	rx_ring->fbr[0] = kmalloc(sizeof(struct fbr_lookup), GFP_KERNEL);
#endif
	rx_ring->fbr[1] = kmalloc(sizeof(struct fbr_lookup), GFP_KERNEL);

	/* The first thing we will do is configure the sizes of the buffer
	 * rings. These will change based on jumbo packet support.  Larger
	 * jumbo packets increases the size of each entry in FBR0, and the
	 * number of entries in FBR0, while at the same time decreasing the
	 * number of entries in FBR1.
	 *
	 * FBR1 holds "large" frames, FBR0 holds "small" frames.  If FBR1
	 * entries are huge in order to accommodate a "jumbo" frame, then it
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
	adapter->rx_ring.PsrNumEntries = adapter->rx_ring.Fbr0NumEntries +
	    adapter->rx_ring.Fbr1NumEntries;
#else
	adapter->rx_ring.PsrNumEntries = adapter->rx_ring.Fbr1NumEntries;
#endif

	/* Allocate an area of memory for Free Buffer Ring 1 */
	bufsize = (sizeof(struct fbr_desc) * rx_ring->Fbr1NumEntries) + 0xfff;
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

	rx_ring->pFbr1RingVa = (void *)((u8 *) rx_ring->pFbr1RingVa +
					rx_ring->Fbr1offset);

#ifdef USE_FBR0
	/* Allocate an area of memory for Free Buffer Ring 0 */
	bufsize = (sizeof(struct fbr_desc) * rx_ring->Fbr0NumEntries) + 0xfff;
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

	rx_ring->pFbr0RingVa = (void *)((u8 *) rx_ring->pFbr0RingVa +
					rx_ring->Fbr0offset);
#endif

	for (i = 0; i < (rx_ring->Fbr1NumEntries / FBR_CHUNKS);
	     i++) {
		u64 Fbr1Offset;
		u64 Fbr1TempPa;
		u32 Fbr1Align;

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
		rx_ring->Fbr1MemVa[i] =
		    pci_alloc_consistent(adapter->pdev, FBRChunkSize,
					 &rx_ring->Fbr1MemPa[i]);

		if (!rx_ring->Fbr1MemVa[i]) {
		dev_err(&adapter->pdev->dev,
				"Could not alloc memory\n");
			return -ENOMEM;
		}

		/* See NOTE in "Save Physical Address" comment above */
		Fbr1TempPa = rx_ring->Fbr1MemPa[i];

		et131x_align_allocated_memory(adapter,
					      &Fbr1TempPa,
					      &Fbr1Offset, (Fbr1Align - 1));

		for (j = 0; j < FBR_CHUNKS; j++) {
			u32 index = (i * FBR_CHUNKS) + j;

			/* Save the Virtual address of this index for quick
			 * access later
			 */
			rx_ring->fbr[1]->virt[index] =
			    (u8 *) rx_ring->Fbr1MemVa[i] +
			    (j * rx_ring->Fbr1BufferSize) + Fbr1Offset;

			/* now store the physical address in the descriptor
			 * so the device can access it
			 */
			rx_ring->fbr[1]->bus_high[index] =
			    (u32) (Fbr1TempPa >> 32);
			rx_ring->fbr[1]->bus_low[index] = (u32) Fbr1TempPa;

			Fbr1TempPa += rx_ring->Fbr1BufferSize;

			rx_ring->fbr[1]->buffer1[index] =
			    rx_ring->fbr[1]->virt[index];
			rx_ring->fbr[1]->buffer2[index] =
			    rx_ring->fbr[1]->virt[index] - 4;
		}
	}

#ifdef USE_FBR0
	/* Same for FBR0 (if in use) */
	for (i = 0; i < (rx_ring->Fbr0NumEntries / FBR_CHUNKS);
	     i++) {
		u64 Fbr0Offset;
		u64 Fbr0TempPa;

		FBRChunkSize = ((FBR_CHUNKS + 1) * rx_ring->Fbr0BufferSize) - 1;
		rx_ring->Fbr0MemVa[i] =
		    pci_alloc_consistent(adapter->pdev, FBRChunkSize,
					 &rx_ring->Fbr0MemPa[i]);

		if (!rx_ring->Fbr0MemVa[i]) {
			dev_err(&adapter->pdev->dev,
				"Could not alloc memory\n");
			return -ENOMEM;
		}

		/* See NOTE in "Save Physical Address" comment above */
		Fbr0TempPa = rx_ring->Fbr0MemPa[i];

		et131x_align_allocated_memory(adapter,
					      &Fbr0TempPa,
					      &Fbr0Offset,
					      rx_ring->Fbr0BufferSize - 1);

		for (j = 0; j < FBR_CHUNKS; j++) {
			u32 index = (i * FBR_CHUNKS) + j;

			rx_ring->fbr[0]->virt[index] =
			    (u8 *) rx_ring->Fbr0MemVa[i] +
			    (j * rx_ring->Fbr0BufferSize) + Fbr0Offset;

			rx_ring->fbr[0]->bus_high[index] =
			    (u32) (Fbr0TempPa >> 32);
			rx_ring->fbr[0]->bus_low[index] = (u32) Fbr0TempPa;

			Fbr0TempPa += rx_ring->Fbr0BufferSize;

			rx_ring->fbr[0]->buffer1[index] =
			    rx_ring->fbr[0]->virt[index];
			rx_ring->fbr[0]->buffer2[index] =
			    rx_ring->fbr[0]->virt[index] - 4;
		}
	}
#endif

	/* Allocate an area of memory for FIFO of Packet Status ring entries */
	pktStatRingSize =
	    sizeof(struct pkt_stat_desc) * adapter->rx_ring.PsrNumEntries;

	rx_ring->pPSRingVa = pci_alloc_consistent(adapter->pdev,
						  pktStatRingSize,
						  &rx_ring->pPSRingPa);

	if (!rx_ring->pPSRingVa) {
		dev_err(&adapter->pdev->dev,
			  "Cannot alloc memory for Packet Status Ring\n");
		return -ENOMEM;
	}
	printk(KERN_INFO "PSR %lx\n", (unsigned long) rx_ring->pPSRingPa);

	/*
	 * NOTE : pci_alloc_consistent(), used above to alloc DMA regions,
	 * ALWAYS returns SAC (32-bit) addresses. If DAC (64-bit) addresses
	 * are ever returned, make sure the high part is retrieved here before
	 * storing the adjusted address.
	 */

	/* Allocate an area of memory for writeback of status information */
	rx_ring->rx_status_block = pci_alloc_consistent(adapter->pdev,
					    sizeof(struct rx_status_block),
					    &rx_ring->rx_status_bus);
	if (!rx_ring->rx_status_block) {
		dev_err(&adapter->pdev->dev,
			  "Cannot alloc memory for Status Block\n");
		return -ENOMEM;
	}
	rx_ring->NumRfd = NIC_DEFAULT_NUM_RFD;
	printk(KERN_INFO "PRS %lx\n", (unsigned long)rx_ring->rx_status_bus);

	/* Recv
	 * pci_pool_create initializes a lookaside list. After successful
	 * creation, nonpaged fixed-size blocks can be allocated from and
	 * freed to the lookaside list.
	 * RFDs will be allocated from this pool.
	 */
	rx_ring->RecvLookaside = kmem_cache_create(adapter->netdev->name,
						   sizeof(struct rfd),
						   0,
						   SLAB_CACHE_DMA |
						   SLAB_HWCACHE_ALIGN,
						   NULL);

	adapter->Flags |= fMP_ADAPTER_RECV_LOOKASIDE;

	/* The RFDs are going to be put on lists later on, so initialize the
	 * lists now.
	 */
	INIT_LIST_HEAD(&rx_ring->RecvList);
	return 0;
}

/**
 * et131x_rx_dma_memory_free - Free all memory allocated within this module.
 * @adapter: pointer to our private adapter structure
 */
void et131x_rx_dma_memory_free(struct et131x_adapter *adapter)
{
	u32 index;
	u32 bufsize;
	u32 pktStatRingSize;
	struct rfd *rfd;
	struct rx_ring *rx_ring;

	/* Setup some convenience pointers */
	rx_ring = &adapter->rx_ring;

	/* Free RFDs and associated packet descriptors */
	WARN_ON(rx_ring->nReadyRecv != rx_ring->NumRfd);

	while (!list_empty(&rx_ring->RecvList)) {
		rfd = (struct rfd *) list_entry(rx_ring->RecvList.next,
				struct rfd, list_node);

		list_del(&rfd->list_node);
		rfd->skb = NULL;
		kmem_cache_free(adapter->rx_ring.RecvLookaside, rfd);
	}

	/* Free Free Buffer Ring 1 */
	if (rx_ring->pFbr1RingVa) {
		/* First the packet memory */
		for (index = 0; index <
		     (rx_ring->Fbr1NumEntries / FBR_CHUNKS); index++) {
			if (rx_ring->Fbr1MemVa[index]) {
				u32 Fbr1Align;

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
		rx_ring->pFbr1RingVa = (void *)((u8 *)
				rx_ring->pFbr1RingVa - rx_ring->Fbr1offset);

		bufsize = (sizeof(struct fbr_desc) * rx_ring->Fbr1NumEntries)
							    + 0xfff;

		pci_free_consistent(adapter->pdev, bufsize,
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
		rx_ring->pFbr0RingVa = (void *)((u8 *)
				rx_ring->pFbr0RingVa - rx_ring->Fbr0offset);

		bufsize = (sizeof(struct fbr_desc) * rx_ring->Fbr0NumEntries)
							    + 0xfff;

		pci_free_consistent(adapter->pdev,
				    bufsize,
				    rx_ring->pFbr0RingVa, rx_ring->pFbr0RingPa);

		rx_ring->pFbr0RingVa = NULL;
	}
#endif

	/* Free Packet Status Ring */
	if (rx_ring->pPSRingVa) {
		pktStatRingSize =
		    sizeof(struct pkt_stat_desc) * adapter->rx_ring.PsrNumEntries;

		pci_free_consistent(adapter->pdev, pktStatRingSize,
				    rx_ring->pPSRingVa, rx_ring->pPSRingPa);

		rx_ring->pPSRingVa = NULL;
	}

	/* Free area of memory for the writeback of status information */
	if (rx_ring->rx_status_block) {
		pci_free_consistent(adapter->pdev,
			sizeof(struct rx_status_block),
			rx_ring->rx_status_block, rx_ring->rx_status_bus);
		rx_ring->rx_status_block = NULL;
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
	kfree(rx_ring->fbr[0]);
#endif

	kfree(rx_ring->fbr[1]);

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
	struct rfd *rfd = NULL;
	u32 rfdct;
	u32 numrfd = 0;
	struct rx_ring *rx_ring;

	/* Setup some convenience pointers */
	rx_ring = &adapter->rx_ring;

	/* Setup each RFD */
	for (rfdct = 0; rfdct < rx_ring->NumRfd; rfdct++) {
		rfd = kmem_cache_alloc(rx_ring->RecvLookaside,
						     GFP_ATOMIC | GFP_DMA);

		if (!rfd) {
			dev_err(&adapter->pdev->dev,
				  "Couldn't alloc RFD out of kmem_cache\n");
			status = -ENOMEM;
			continue;
		}

		rfd->skb = NULL;

		/* Add this RFD to the RecvList */
		list_add_tail(&rfd->list_node, &rx_ring->RecvList);

		/* Increment both the available RFD's, and the total RFD's. */
		rx_ring->nReadyRecv++;
		numrfd++;
	}

	if (numrfd > NIC_MIN_NUM_RFD)
		status = 0;

	rx_ring->NumRfd = numrfd;

	if (status != 0) {
		kmem_cache_free(rx_ring->RecvLookaside, rfd);
		dev_err(&adapter->pdev->dev,
			  "Allocation problems in et131x_init_recv\n");
	}
	return status;
}

/**
 * ConfigRxDmaRegs - Start of Rx_DMA init sequence
 * @etdev: pointer to our adapter structure
 */
void ConfigRxDmaRegs(struct et131x_adapter *etdev)
{
	struct rxdma_regs __iomem *rx_dma = &etdev->regs->rxdma;
	struct rx_ring *rx_local = &etdev->rx_ring;
	struct fbr_desc *fbr_entry;
	u32 entry;
	u32 psr_num_des;
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
	writel((u32) ((u64)rx_local->rx_status_bus >> 32),
	       &rx_dma->dma_wb_base_hi);
	writel((u32) rx_local->rx_status_bus, &rx_dma->dma_wb_base_lo);

	memset(rx_local->rx_status_block, 0, sizeof(struct rx_status_block));

	/* Set the address and parameters of the packet status ring into the
	 * 1310's registers
	 */
	writel((u32) ((u64)rx_local->pPSRingPa >> 32),
	       &rx_dma->psr_base_hi);
	writel((u32) rx_local->pPSRingPa, &rx_dma->psr_base_lo);
	writel(rx_local->PsrNumEntries - 1, &rx_dma->psr_num_des);
	writel(0, &rx_dma->psr_full_offset);

	psr_num_des = readl(&rx_dma->psr_num_des) & 0xFFF;
	writel((psr_num_des * LO_MARK_PERCENT_FOR_PSR) / 100,
	       &rx_dma->psr_min_des);

	spin_lock_irqsave(&etdev->rcv_lock, flags);

	/* These local variables track the PSR in the adapter structure */
	rx_local->local_psr_full = 0;

	/* Now's the best time to initialize FBR1 contents */
	fbr_entry = (struct fbr_desc *) rx_local->pFbr1RingVa;
	for (entry = 0; entry < rx_local->Fbr1NumEntries; entry++) {
		fbr_entry->addr_hi = rx_local->fbr[1]->bus_high[entry];
		fbr_entry->addr_lo = rx_local->fbr[1]->bus_low[entry];
		fbr_entry->word2 = entry;
		fbr_entry++;
	}

	/* Set the address and parameters of Free buffer ring 1 (and 0 if
	 * required) into the 1310's registers
	 */
	writel((u32) (rx_local->Fbr1Realpa >> 32), &rx_dma->fbr1_base_hi);
	writel((u32) rx_local->Fbr1Realpa, &rx_dma->fbr1_base_lo);
	writel(rx_local->Fbr1NumEntries - 1, &rx_dma->fbr1_num_des);
	writel(ET_DMA10_WRAP, &rx_dma->fbr1_full_offset);

	/* This variable tracks the free buffer ring 1 full position, so it
	 * has to match the above.
	 */
	rx_local->local_Fbr1_full = ET_DMA10_WRAP;
	writel(((rx_local->Fbr1NumEntries * LO_MARK_PERCENT_FOR_RX) / 100) - 1,
	       &rx_dma->fbr1_min_des);

#ifdef USE_FBR0
	/* Now's the best time to initialize FBR0 contents */
	fbr_entry = (struct fbr_desc *) rx_local->pFbr0RingVa;
	for (entry = 0; entry < rx_local->Fbr0NumEntries; entry++) {
		fbr_entry->addr_hi = rx_local->fbr[0]->bus_high[entry];
		fbr_entry->addr_lo = rx_local->fbr[0]->bus_low[entry];
		fbr_entry->word2 = entry;
		fbr_entry++;
	}

	writel((u32) (rx_local->Fbr0Realpa >> 32), &rx_dma->fbr0_base_hi);
	writel((u32) rx_local->Fbr0Realpa, &rx_dma->fbr0_base_lo);
	writel(rx_local->Fbr0NumEntries - 1, &rx_dma->fbr0_num_des);
	writel(ET_DMA10_WRAP, &rx_dma->fbr0_full_offset);

	/* This variable tracks the free buffer ring 0 full position, so it
	 * has to match the above.
	 */
	rx_local->local_Fbr0_full = ET_DMA10_WRAP;
	writel(((rx_local->Fbr0NumEntries * LO_MARK_PERCENT_FOR_RX) / 100) - 1,
	       &rx_dma->fbr0_min_des);
#endif

	/* Program the number of packets we will receive before generating an
	 * interrupt.
	 * For version B silicon, this value gets updated once autoneg is
	 *complete.
	 */
	writel(PARM_RX_NUM_BUFS_DEF, &rx_dma->num_pkt_done);

	/* The "time_done" is not working correctly to coalesce interrupts
	 * after a given time period, but rather is giving us an interrupt
	 * regardless of whether we have received packets.
	 * This value gets updated once autoneg is complete.
	 */
	writel(PARM_RX_TIME_INT_DEF, &rx_dma->max_pkt_time);

	spin_unlock_irqrestore(&etdev->rcv_lock, flags);
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
		writel(0, &etdev->regs->rxdma.max_pkt_time);
		writel(1, &etdev->regs->rxdma.num_pkt_done);
	}
}

/**
 * et131x_rx_dma_disable - Stop of Rx_DMA on the ET1310
 * @etdev: pointer to our adapter structure
 */
void et131x_rx_dma_disable(struct et131x_adapter *etdev)
{
	u32 csr;
	/* Setup the receive dma configuration register */
	writel(0x00002001, &etdev->regs->rxdma.csr);
	csr = readl(&etdev->regs->rxdma.csr);
	if ((csr & 0x00020000) == 0) {	/* Check halt status (bit 17) */
		udelay(5);
		csr = readl(&etdev->regs->rxdma.csr);
		if ((csr & 0x00020000) == 0)
			dev_err(&etdev->pdev->dev,
			"RX Dma failed to enter halt state. CSR 0x%08x\n",
				csr);
	}
}

/**
 * et131x_rx_dma_enable - re-start of Rx_DMA on the ET1310.
 * @etdev: pointer to our adapter structure
 */
void et131x_rx_dma_enable(struct et131x_adapter *etdev)
{
	/* Setup the receive dma configuration register for normal operation */
	u32 csr =  0x2000;	/* FBR1 enable */

	if (etdev->rx_ring.Fbr1BufferSize == 4096)
		csr |= 0x0800;
	else if (etdev->rx_ring.Fbr1BufferSize == 8192)
		csr |= 0x1000;
	else if (etdev->rx_ring.Fbr1BufferSize == 16384)
		csr |= 0x1800;
#ifdef USE_FBR0
	csr |= 0x0400;		/* FBR0 enable */
	if (etdev->rx_ring.Fbr0BufferSize == 256)
		csr |= 0x0100;
	else if (etdev->rx_ring.Fbr0BufferSize == 512)
		csr |= 0x0200;
	else if (etdev->rx_ring.Fbr0BufferSize == 1024)
		csr |= 0x0300;
#endif
	writel(csr, &etdev->regs->rxdma.csr);

	csr = readl(&etdev->regs->rxdma.csr);
	if ((csr & 0x00020000) != 0) {
		udelay(5);
		csr = readl(&etdev->regs->rxdma.csr);
		if ((csr & 0x00020000) != 0) {
			dev_err(&etdev->pdev->dev,
			    "RX Dma failed to exit halt state.  CSR 0x%08x\n",
				csr);
		}
	}
}

/**
 * nic_rx_pkts - Checks the hardware for available packets
 * @etdev: pointer to our adapter
 *
 * Returns rfd, a pointer to our MPRFD.
 *
 * Checks the hardware for available packets, using completion ring
 * If packets are available, it gets an RFD from the RecvList, attaches
 * the packet to it, puts the RFD in the RecvPendList, and also returns
 * the pointer to the RFD.
 */
struct rfd * nic_rx_pkts(struct et131x_adapter *etdev)
{
	struct rx_ring *rx_local = &etdev->rx_ring;
	struct rx_status_block *status;
	struct pkt_stat_desc *psr;
	struct rfd *rfd;
	u32 i;
	u8 *buf;
	unsigned long flags;
	struct list_head *element;
	u8 rindex;
	u16 bindex;
	u32 len;
	u32 word0;
	u32 word1;

	/* RX Status block is written by the DMA engine prior to every
	 * interrupt. It contains the next to be used entry in the Packet
	 * Status Ring, and also the two Free Buffer rings.
	 */
	status = rx_local->rx_status_block;
	word1 = status->Word1 >> 16;	/* Get the useful bits */

	/* Check the PSR and wrap bits do not match */
	if ((word1 & 0x1FFF) == (rx_local->local_psr_full & 0x1FFF))
		/* Looks like this ring is not updated yet */
		return NULL;

	/* The packet status ring indicates that data is available. */
	psr = (struct pkt_stat_desc *) (rx_local->pPSRingVa) +
			(rx_local->local_psr_full & 0xFFF);

	/* Grab any information that is required once the PSR is
	 * advanced, since we can no longer rely on the memory being
	 * accurate
	 */
	len = psr->word1 & 0xFFFF;
	rindex = (psr->word1 >> 26) & 0x03;
	bindex = (psr->word1 >> 16) & 0x3FF;
	word0 = psr->word0;

	/* Indicate that we have used this PSR entry. */
	/* FIXME wrap 12 */
	add_12bit(&rx_local->local_psr_full, 1);
	if ((rx_local->local_psr_full & 0xFFF)  > rx_local->PsrNumEntries - 1) {
		/* Clear psr full and toggle the wrap bit */
		rx_local->local_psr_full &=  ~0xFFF;
		rx_local->local_psr_full ^= 0x1000;
	}

	writel(rx_local->local_psr_full,
	       &etdev->regs->rxdma.psr_full_offset);

#ifndef USE_FBR0
	if (rindex != 1)
		return NULL;
#endif

#ifdef USE_FBR0
	if (rindex > 1 ||
		(rindex == 0 &&
		bindex > rx_local->Fbr0NumEntries - 1) ||
		(rindex == 1 &&
		bindex > rx_local->Fbr1NumEntries - 1))
#else
	if (rindex != 1 || bindex > rx_local->Fbr1NumEntries - 1)
#endif
	{
		/* Illegal buffer or ring index cannot be used by S/W*/
		dev_err(&etdev->pdev->dev,
			  "NICRxPkts PSR Entry %d indicates "
			  "length of %d and/or bad bi(%d)\n",
			  rx_local->local_psr_full & 0xFFF,
			  len, bindex);
		return NULL;
	}

	/* Get and fill the RFD. */
	spin_lock_irqsave(&etdev->rcv_lock, flags);

	rfd = NULL;
	element = rx_local->RecvList.next;
	rfd = (struct rfd *) list_entry(element, struct rfd, list_node);

	if (rfd == NULL) {
		spin_unlock_irqrestore(&etdev->rcv_lock, flags);
		return NULL;
	}

	list_del(&rfd->list_node);
	rx_local->nReadyRecv--;

	spin_unlock_irqrestore(&etdev->rcv_lock, flags);

	rfd->bufferindex = bindex;
	rfd->ringindex = rindex;

	/* In V1 silicon, there is a bug which screws up filtering of
	 * runt packets.  Therefore runt packet filtering is disabled
	 * in the MAC and the packets are dropped here.  They are
	 * also counted here.
	 */
	if (len < (NIC_MIN_PACKET_SIZE + 4)) {
		etdev->Stats.other_errors++;
		len = 0;
	}

	if (len) {
		if (etdev->ReplicaPhyLoopbk == 1) {
			buf = rx_local->fbr[rindex]->virt[bindex];

			if (memcmp(&buf[6], etdev->addr, ETH_ALEN) == 0) {
				if (memcmp(&buf[42], "Replica packet",
					   ETH_HLEN)) {
					etdev->ReplicaPhyLoopbkPF = 1;
				}
			}
		}

		/* Determine if this is a multicast packet coming in */
		if ((word0 & ALCATEL_MULTICAST_PKT) &&
		    !(word0 & ALCATEL_BROADCAST_PKT)) {
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
				buf = rx_local->fbr[rindex]->
						virt[bindex];

				/* Loop through our list to see if the
				 * destination address of this packet
				 * matches one in our list.
				 */
				for (i = 0;
				     i < etdev->MCAddressCount;
				     i++) {
					if (buf[0] ==
					    etdev->MCList[i][0]
					    && buf[1] ==
					    etdev->MCList[i][1]
					    && buf[2] ==
					    etdev->MCList[i][2]
					    && buf[3] ==
					    etdev->MCList[i][3]
					    && buf[4] ==
					    etdev->MCList[i][4]
					    && buf[5] ==
					    etdev->MCList[i][5]) {
						break;
					}
				}

				/* If our index is equal to the number
				 * of Multicast address we have, then
				 * this means we did not find this
				 * packet's matching address in our
				 * list.  Set the len to zero,
				 * so we free our RFD when we return
				 * from this function.
				 */
				if (i == etdev->MCAddressCount)
					len = 0;
			}

			if (len > 0)
				etdev->Stats.multircv++;
		} else if (word0 & ALCATEL_BROADCAST_PKT)
			etdev->Stats.brdcstrcv++;
		else
			/* Not sure what this counter measures in
			 * promiscuous mode. Perhaps we should check
			 * the MAC address to see if it is directed
			 * to us in promiscuous mode.
			 */
			etdev->Stats.unircv++;
	}

	if (len > 0) {
		struct sk_buff *skb = NULL;

		/*rfd->len = len - 4; */
		rfd->len = len;

		skb = dev_alloc_skb(rfd->len + 2);
		if (!skb) {
			dev_err(&etdev->pdev->dev,
				  "Couldn't alloc an SKB for Rx\n");
			return NULL;
		}

		etdev->net_stats.rx_bytes += rfd->len;

		memcpy(skb_put(skb, rfd->len),
		       rx_local->fbr[rindex]->virt[bindex],
		       rfd->len);

		skb->dev = etdev->netdev;
		skb->protocol = eth_type_trans(skb, etdev->netdev);
		skb->ip_summed = CHECKSUM_NONE;

		netif_rx(skb);
	} else {
		rfd->len = 0;
	}

	nic_return_rfd(etdev, rfd);
	return rfd;
}

/**
 * et131x_reset_recv - Reset the receive list
 * @etdev: pointer to our adapter
 *
 * Assumption, Rcv spinlock has been acquired.
 */
void et131x_reset_recv(struct et131x_adapter *etdev)
{
	WARN_ON(list_empty(&etdev->rx_ring.RecvList));

}

/**
 * et131x_handle_recv_interrupt - Interrupt handler for receive processing
 * @etdev: pointer to our adapter
 *
 * Assumption, Rcv spinlock has been acquired.
 */
void et131x_handle_recv_interrupt(struct et131x_adapter *etdev)
{
	struct rfd *rfd = NULL;
	u32 count = 0;
	bool done = true;

	/* Process up to available RFD's */
	while (count < NUM_PACKETS_HANDLED) {
		if (list_empty(&etdev->rx_ring.RecvList)) {
			WARN_ON(etdev->rx_ring.nReadyRecv != 0);
			done = false;
			break;
		}

		rfd = nic_rx_pkts(etdev);

		if (rfd == NULL)
			break;

		/* Do not receive any packets until a filter has been set.
		 * Do not receive any packets until we have link.
		 * If length is zero, return the RFD in order to advance the
		 * Free buffer ring.
		 */
		if (!etdev->PacketFilter ||
		    !(etdev->Flags & fMP_ADAPTER_LINK_DETECTION) ||
		    rfd->len == 0) {
			continue;
		}

		/* Increment the number of packets we received */
		etdev->Stats.ipackets++;

		/* Set the status on the packet, either resources or success */
		if (etdev->rx_ring.nReadyRecv < RFD_LOW_WATER_MARK) {
			dev_warn(&etdev->pdev->dev,
				    "RFD's are running out\n");
		}
		count++;
	}

	if (count == NUM_PACKETS_HANDLED || !done) {
		etdev->rx_ring.UnfinishedReceives = true;
		writel(PARM_TX_TIME_INT_DEF * NANO_IN_A_MICRO,
		       &etdev->regs->global.watchdog_timer);
	} else
		/* Watchdog timer will disable itself if appropriate. */
		etdev->rx_ring.UnfinishedReceives = false;
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
 * @rfd: pointer to the RFD
 */
void nic_return_rfd(struct et131x_adapter *etdev, struct rfd *rfd)
{
	struct rx_ring *rx_local = &etdev->rx_ring;
	struct rxdma_regs __iomem *rx_dma = &etdev->regs->rxdma;
	u16 bi = rfd->bufferindex;
	u8 ri = rfd->ringindex;
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
			struct fbr_desc *next =
			    (struct fbr_desc *) (rx_local->pFbr1RingVa) +
					 INDEX10(rx_local->local_Fbr1_full);

			/* Handle the Free Buffer Ring advancement here. Write
			 * the PA / Buffer Index for the returned buffer into
			 * the oldest (next to be freed)FBR entry
			 */
			next->addr_hi = rx_local->fbr[1]->bus_high[bi];
			next->addr_lo = rx_local->fbr[1]->bus_low[bi];
			next->word2 = bi;

			writel(bump_fbr(&rx_local->local_Fbr1_full,
				rx_local->Fbr1NumEntries - 1),
				&rx_dma->fbr1_full_offset);
		}
#ifdef USE_FBR0
		else {
			struct fbr_desc *next = (struct fbr_desc *)
				rx_local->pFbr0RingVa +
					INDEX10(rx_local->local_Fbr0_full);

			/* Handle the Free Buffer Ring advancement here. Write
			 * the PA / Buffer Index for the returned buffer into
			 * the oldest (next to be freed) FBR entry
			 */
			next->addr_hi = rx_local->fbr[0]->bus_high[bi];
			next->addr_lo = rx_local->fbr[0]->bus_low[bi];
			next->word2 = bi;

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
	spin_lock_irqsave(&etdev->rcv_lock, flags);
	list_add_tail(&rfd->list_node, &rx_local->RecvList);
	rx_local->nReadyRecv++;
	spin_unlock_irqrestore(&etdev->rcv_lock, flags);

	WARN_ON(rx_local->nReadyRecv > rx_local->NumRfd);
}
