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

static inline u32 bump_free_buff_ring(u32 *free_buff_ring, u32 limit)
{
	u32 tmp_free_buff_ring = *free_buff_ring;
	tmp_free_buff_ring++;
	/* This works for all cases where limit < 1024. The 1023 case
	   works because 1023++ is 1024 which means the if condition is not
	   taken but the carry of the bit into the wrap bit toggles the wrap
	   value correctly */
	if ((tmp_free_buff_ring & ET_DMA10_MASK) > limit) {
		tmp_free_buff_ring &= ~ET_DMA10_MASK;
		tmp_free_buff_ring ^= ET_DMA10_WRAP;
	}
	/* For the 1023 case */
	tmp_free_buff_ring &= (ET_DMA10_MASK|ET_DMA10_WRAP);
	*free_buff_ring = tmp_free_buff_ring;
	return tmp_free_buff_ring;
}

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
	u32 pktstat_ringsize, fbr_chunksize;
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

	if (adapter->registry_jumbo_packet < 2048) {
#ifdef USE_FBR0
		rx_ring->fbr0_buffsize = 256;
		rx_ring->fbr0_num_entries = 512;
#endif
		rx_ring->fbr1_buffsize = 2048;
		rx_ring->fbr1_num_entries = 512;
	} else if (adapter->registry_jumbo_packet < 4096) {
#ifdef USE_FBR0
		rx_ring->fbr0_buffsize = 512;
		rx_ring->fbr0_num_entries = 1024;
#endif
		rx_ring->fbr1_buffsize = 4096;
		rx_ring->fbr1_num_entries = 512;
	} else {
#ifdef USE_FBR0
		rx_ring->fbr0_buffsize = 1024;
		rx_ring->fbr0_num_entries = 768;
#endif
		rx_ring->fbr1_buffsize = 16384;
		rx_ring->fbr1_num_entries = 128;
	}

#ifdef USE_FBR0
	adapter->rx_ring.psr_num_entries = adapter->rx_ring.fbr0_num_entries +
	    adapter->rx_ring.fbr1_num_entries;
#else
	adapter->rx_ring.psr_num_entries = adapter->rx_ring.fbr1_num_entries;
#endif

	/* Allocate an area of memory for Free Buffer Ring 1 */
	bufsize = (sizeof(struct fbr_desc) * rx_ring->fbr1_num_entries) + 0xfff;
	rx_ring->fbr1_ring_virtaddr = pci_alloc_consistent(adapter->pdev,
						bufsize,
						&rx_ring->fbr1_ring_physaddr);
	if (!rx_ring->fbr1_ring_virtaddr) {
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
	rx_ring->fbr1_real_physaddr = rx_ring->fbr1_ring_physaddr;

	/* Align Free Buffer Ring 1 on a 4K boundary */
	et131x_align_allocated_memory(adapter,
				      &rx_ring->fbr1_real_physaddr,
				      &rx_ring->fbr1_offset, 0x0FFF);

	rx_ring->fbr1_ring_virtaddr =
			(void *)((u8 *) rx_ring->fbr1_ring_virtaddr +
			rx_ring->fbr1_offset);

#ifdef USE_FBR0
	/* Allocate an area of memory for Free Buffer Ring 0 */
	bufsize = (sizeof(struct fbr_desc) * rx_ring->fbr0_num_entries) + 0xfff;
	rx_ring->fbr0_ring_virtaddr = pci_alloc_consistent(adapter->pdev,
						bufsize,
						&rx_ring->fbr0_ring_physaddr);
	if (!rx_ring->fbr0_ring_virtaddr) {
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
	rx_ring->fbr0_real_physaddr = rx_ring->fbr0_ring_physaddr;

	/* Align Free Buffer Ring 0 on a 4K boundary */
	et131x_align_allocated_memory(adapter,
				      &rx_ring->fbr0_real_physaddr,
				      &rx_ring->fbr0_offset, 0x0FFF);

	rx_ring->fbr0_ring_virtaddr =
				(void *)((u8 *) rx_ring->fbr0_ring_virtaddr +
				rx_ring->fbr0_offset);
#endif
	for (i = 0; i < (rx_ring->fbr1_num_entries / FBR_CHUNKS); i++) {
		u64 fbr1_offset;
		u64 fbr1_tmp_physaddr;
		u32 fbr1_align;

		/* This code allocates an area of memory big enough for N
		 * free buffers + (buffer_size - 1) so that the buffers can
		 * be aligned on 4k boundaries.  If each buffer were aligned
		 * to a buffer_size boundary, the effect would be to double
		 * the size of FBR0.  By allocating N buffers at once, we
		 * reduce this overhead.
		 */
		if (rx_ring->fbr1_buffsize > 4096)
			fbr1_align = 4096;
		else
			fbr1_align = rx_ring->fbr1_buffsize;

		fbr_chunksize =
		    (FBR_CHUNKS * rx_ring->fbr1_buffsize) + fbr1_align - 1;
		rx_ring->fbr1_mem_virtaddrs[i] =
		    pci_alloc_consistent(adapter->pdev, fbr_chunksize,
					 &rx_ring->fbr1_mem_physaddrs[i]);

		if (!rx_ring->fbr1_mem_virtaddrs[i]) {
			dev_err(&adapter->pdev->dev,
				"Could not alloc memory\n");
			return -ENOMEM;
		}

		/* See NOTE in "Save Physical Address" comment above */
		fbr1_tmp_physaddr = rx_ring->fbr1_mem_physaddrs[i];

		et131x_align_allocated_memory(adapter,
					      &fbr1_tmp_physaddr,
					      &fbr1_offset, (fbr1_align - 1));

		for (j = 0; j < FBR_CHUNKS; j++) {
			u32 index = (i * FBR_CHUNKS) + j;

			/* Save the Virtual address of this index for quick
			 * access later
			 */
			rx_ring->fbr[1]->virt[index] =
			    (u8 *) rx_ring->fbr1_mem_virtaddrs[i] +
			    (j * rx_ring->fbr1_buffsize) + fbr1_offset;

			/* now store the physical address in the descriptor
			 * so the device can access it
			 */
			rx_ring->fbr[1]->bus_high[index] =
			    (u32) (fbr1_tmp_physaddr >> 32);
			rx_ring->fbr[1]->bus_low[index] =
			    (u32) fbr1_tmp_physaddr;

			fbr1_tmp_physaddr += rx_ring->fbr1_buffsize;

			rx_ring->fbr[1]->buffer1[index] =
			    rx_ring->fbr[1]->virt[index];
			rx_ring->fbr[1]->buffer2[index] =
			    rx_ring->fbr[1]->virt[index] - 4;
		}
	}

#ifdef USE_FBR0
	/* Same for FBR0 (if in use) */
	for (i = 0; i < (rx_ring->fbr0_num_entries / FBR_CHUNKS); i++) {
		u64 fbr0_offset;
		u64 fbr0_tmp_physaddr;

		fbr_chunksize =
		    ((FBR_CHUNKS + 1) * rx_ring->fbr0_buffsize) - 1;
		rx_ring->fbr0_mem_virtaddrs[i] =
		    pci_alloc_consistent(adapter->pdev, fbr_chunksize,
					 &rx_ring->fbr0_mem_physaddrs[i]);

		if (!rx_ring->fbr0_mem_virtaddrs[i]) {
			dev_err(&adapter->pdev->dev,
				"Could not alloc memory\n");
			return -ENOMEM;
		}

		/* See NOTE in "Save Physical Address" comment above */
		fbr0_tmp_physaddr = rx_ring->fbr0_mem_physaddrs[i];

		et131x_align_allocated_memory(adapter,
					      &fbr0_tmp_physaddr,
					      &fbr0_offset,
					      rx_ring->fbr0_buffsize - 1);

		for (j = 0; j < FBR_CHUNKS; j++) {
			u32 index = (i * FBR_CHUNKS) + j;

			rx_ring->fbr[0]->virt[index] =
			    (u8 *) rx_ring->fbr0_mem_virtaddrs[i] +
			    (j * rx_ring->fbr0_buffsize) + fbr0_offset;

			rx_ring->fbr[0]->bus_high[index] =
			    (u32) (fbr0_tmp_physaddr >> 32);
			rx_ring->fbr[0]->bus_low[index] =
			    (u32) fbr0_tmp_physaddr;

			fbr0_tmp_physaddr += rx_ring->fbr0_buffsize;

			rx_ring->fbr[0]->buffer1[index] =
			    rx_ring->fbr[0]->virt[index];
			rx_ring->fbr[0]->buffer2[index] =
			    rx_ring->fbr[0]->virt[index] - 4;
		}
	}
#endif

	/* Allocate an area of memory for FIFO of Packet Status ring entries */
	pktstat_ringsize =
	    sizeof(struct pkt_stat_desc) * adapter->rx_ring.psr_num_entries;

	rx_ring->ps_ring_virtaddr = pci_alloc_consistent(adapter->pdev,
						  pktstat_ringsize,
						  &rx_ring->ps_ring_physaddr);

	if (!rx_ring->ps_ring_virtaddr) {
		dev_err(&adapter->pdev->dev,
			  "Cannot alloc memory for Packet Status Ring\n");
		return -ENOMEM;
	}
	printk(KERN_INFO "Packet Status Ring %lx\n",
	    (unsigned long) rx_ring->ps_ring_physaddr);

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
	rx_ring->num_rfd = NIC_DEFAULT_NUM_RFD;
	printk(KERN_INFO "PRS %lx\n", (unsigned long)rx_ring->rx_status_bus);

	/* Recv
	 * pci_pool_create initializes a lookaside list. After successful
	 * creation, nonpaged fixed-size blocks can be allocated from and
	 * freed to the lookaside list.
	 * RFDs will be allocated from this pool.
	 */
	rx_ring->recv_lookaside = kmem_cache_create(adapter->netdev->name,
						   sizeof(struct rfd),
						   0,
						   SLAB_CACHE_DMA |
						   SLAB_HWCACHE_ALIGN,
						   NULL);

	adapter->flags |= fMP_ADAPTER_RECV_LOOKASIDE;

	/* The RFDs are going to be put on lists later on, so initialize the
	 * lists now.
	 */
	INIT_LIST_HEAD(&rx_ring->recv_list);
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
	u32 pktstat_ringsize;
	struct rfd *rfd;
	struct rx_ring *rx_ring;

	/* Setup some convenience pointers */
	rx_ring = &adapter->rx_ring;

	/* Free RFDs and associated packet descriptors */
	WARN_ON(rx_ring->num_ready_recv != rx_ring->num_rfd);

	while (!list_empty(&rx_ring->recv_list)) {
		rfd = (struct rfd *) list_entry(rx_ring->recv_list.next,
				struct rfd, list_node);

		list_del(&rfd->list_node);
		rfd->skb = NULL;
		kmem_cache_free(adapter->rx_ring.recv_lookaside, rfd);
	}

	/* Free Free Buffer Ring 1 */
	if (rx_ring->fbr1_ring_virtaddr) {
		/* First the packet memory */
		for (index = 0; index <
		     (rx_ring->fbr1_num_entries / FBR_CHUNKS); index++) {
			if (rx_ring->fbr1_mem_virtaddrs[index]) {
				u32 fbr1_align;

				if (rx_ring->fbr1_buffsize > 4096)
					fbr1_align = 4096;
				else
					fbr1_align = rx_ring->fbr1_buffsize;

				bufsize =
				    (rx_ring->fbr1_buffsize * FBR_CHUNKS) +
				    fbr1_align - 1;

				pci_free_consistent(adapter->pdev,
					bufsize,
					rx_ring->fbr1_mem_virtaddrs[index],
					rx_ring->fbr1_mem_physaddrs[index]);

				rx_ring->fbr1_mem_virtaddrs[index] = NULL;
			}
		}

		/* Now the FIFO itself */
		rx_ring->fbr1_ring_virtaddr = (void *)((u8 *)
			rx_ring->fbr1_ring_virtaddr - rx_ring->fbr1_offset);

		bufsize = (sizeof(struct fbr_desc) * rx_ring->fbr1_num_entries)
							    + 0xfff;

		pci_free_consistent(adapter->pdev, bufsize,
				    rx_ring->fbr1_ring_virtaddr,
				    rx_ring->fbr1_ring_physaddr);

		rx_ring->fbr1_ring_virtaddr = NULL;
	}

#ifdef USE_FBR0
	/* Now the same for Free Buffer Ring 0 */
	if (rx_ring->fbr0_ring_virtaddr) {
		/* First the packet memory */
		for (index = 0; index <
		     (rx_ring->fbr0_num_entries / FBR_CHUNKS); index++) {
			if (rx_ring->fbr0_mem_virtaddrs[index]) {
				bufsize =
				    (rx_ring->fbr0_buffsize *
				     (FBR_CHUNKS + 1)) - 1;

				pci_free_consistent(adapter->pdev,
					bufsize,
					rx_ring->fbr0_mem_virtaddrs[index],
					rx_ring->fbr0_mem_physaddrs[index]);

				rx_ring->fbr0_mem_virtaddrs[index] = NULL;
			}
		}

		/* Now the FIFO itself */
		rx_ring->fbr0_ring_virtaddr = (void *)((u8 *)
			rx_ring->fbr0_ring_virtaddr - rx_ring->fbr0_offset);

		bufsize = (sizeof(struct fbr_desc) * rx_ring->fbr0_num_entries)
							    + 0xfff;

		pci_free_consistent(adapter->pdev,
				    bufsize,
				    rx_ring->fbr0_ring_virtaddr,
				    rx_ring->fbr0_ring_physaddr);

		rx_ring->fbr0_ring_virtaddr = NULL;
	}
#endif

	/* Free Packet Status Ring */
	if (rx_ring->ps_ring_virtaddr) {
		pktstat_ringsize =
		    sizeof(struct pkt_stat_desc) *
		    adapter->rx_ring.psr_num_entries;

		pci_free_consistent(adapter->pdev, pktstat_ringsize,
				    rx_ring->ps_ring_virtaddr,
				    rx_ring->ps_ring_physaddr);

		rx_ring->ps_ring_virtaddr = NULL;
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
	if (adapter->flags & fMP_ADAPTER_RECV_LOOKASIDE) {
		kmem_cache_destroy(rx_ring->recv_lookaside);
		adapter->flags &= ~fMP_ADAPTER_RECV_LOOKASIDE;
	}

	/* Free the FBR Lookup Table */
#ifdef USE_FBR0
	kfree(rx_ring->fbr[0]);
#endif

	kfree(rx_ring->fbr[1]);

	/* Reset Counters */
	rx_ring->num_ready_recv = 0;
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
	for (rfdct = 0; rfdct < rx_ring->num_rfd; rfdct++) {
		rfd = kmem_cache_alloc(rx_ring->recv_lookaside,
						     GFP_ATOMIC | GFP_DMA);

		if (!rfd) {
			dev_err(&adapter->pdev->dev,
				  "Couldn't alloc RFD out of kmem_cache\n");
			status = -ENOMEM;
			continue;
		}

		rfd->skb = NULL;

		/* Add this RFD to the recv_list */
		list_add_tail(&rfd->list_node, &rx_ring->recv_list);

		/* Increment both the available RFD's, and the total RFD's. */
		rx_ring->num_ready_recv++;
		numrfd++;
	}

	if (numrfd > NIC_MIN_NUM_RFD)
		status = 0;

	rx_ring->num_rfd = numrfd;

	if (status != 0) {
		kmem_cache_free(rx_ring->recv_lookaside, rfd);
		dev_err(&adapter->pdev->dev,
			  "Allocation problems in et131x_init_recv\n");
	}
	return status;
}

/**
 * et131x_config_rx_dma_regs - Start of Rx_DMA init sequence
 * @adapter: pointer to our adapter structure
 */
void et131x_config_rx_dma_regs(struct et131x_adapter *adapter)
{
	struct rxdma_regs __iomem *rx_dma = &adapter->regs->rxdma;
	struct rx_ring *rx_local = &adapter->rx_ring;
	struct fbr_desc *fbr_entry;
	u32 entry;
	u32 psr_num_des;
	unsigned long flags;

	/* Halt RXDMA to perform the reconfigure.  */
	et131x_rx_dma_disable(adapter);

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
	writel((u32) ((u64)rx_local->ps_ring_physaddr >> 32),
	       &rx_dma->psr_base_hi);
	writel((u32) rx_local->ps_ring_physaddr, &rx_dma->psr_base_lo);
	writel(rx_local->psr_num_entries - 1, &rx_dma->psr_num_des);
	writel(0, &rx_dma->psr_full_offset);

	psr_num_des = readl(&rx_dma->psr_num_des) & 0xFFF;
	writel((psr_num_des * LO_MARK_PERCENT_FOR_PSR) / 100,
	       &rx_dma->psr_min_des);

	spin_lock_irqsave(&adapter->rcv_lock, flags);

	/* These local variables track the PSR in the adapter structure */
	rx_local->local_psr_full = 0;

	/* Now's the best time to initialize FBR1 contents */
	fbr_entry = (struct fbr_desc *) rx_local->fbr1_ring_virtaddr;
	for (entry = 0; entry < rx_local->fbr1_num_entries; entry++) {
		fbr_entry->addr_hi = rx_local->fbr[1]->bus_high[entry];
		fbr_entry->addr_lo = rx_local->fbr[1]->bus_low[entry];
		fbr_entry->word2 = entry;
		fbr_entry++;
	}

	/* Set the address and parameters of Free buffer ring 1 (and 0 if
	 * required) into the 1310's registers
	 */
	writel((u32) (rx_local->fbr1_real_physaddr >> 32),
	       &rx_dma->fbr1_base_hi);
	writel((u32) rx_local->fbr1_real_physaddr, &rx_dma->fbr1_base_lo);
	writel(rx_local->fbr1_num_entries - 1, &rx_dma->fbr1_num_des);
	writel(ET_DMA10_WRAP, &rx_dma->fbr1_full_offset);

	/* This variable tracks the free buffer ring 1 full position, so it
	 * has to match the above.
	 */
	rx_local->local_fbr1_full = ET_DMA10_WRAP;
	writel(
	    ((rx_local->fbr1_num_entries * LO_MARK_PERCENT_FOR_RX) / 100) - 1,
	    &rx_dma->fbr1_min_des);

#ifdef USE_FBR0
	/* Now's the best time to initialize FBR0 contents */
	fbr_entry = (struct fbr_desc *) rx_local->fbr0_ring_virtaddr;
	for (entry = 0; entry < rx_local->fbr0_num_entries; entry++) {
		fbr_entry->addr_hi = rx_local->fbr[0]->bus_high[entry];
		fbr_entry->addr_lo = rx_local->fbr[0]->bus_low[entry];
		fbr_entry->word2 = entry;
		fbr_entry++;
	}

	writel((u32) (rx_local->fbr0_real_physaddr >> 32),
	       &rx_dma->fbr0_base_hi);
	writel((u32) rx_local->fbr0_real_physaddr, &rx_dma->fbr0_base_lo);
	writel(rx_local->fbr0_num_entries - 1, &rx_dma->fbr0_num_des);
	writel(ET_DMA10_WRAP, &rx_dma->fbr0_full_offset);

	/* This variable tracks the free buffer ring 0 full position, so it
	 * has to match the above.
	 */
	rx_local->local_fbr0_full = ET_DMA10_WRAP;
	writel(
	    ((rx_local->fbr0_num_entries * LO_MARK_PERCENT_FOR_RX) / 100) - 1,
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

	spin_unlock_irqrestore(&adapter->rcv_lock, flags);
}

/**
 * et131x_set_rx_dma_timer - Set the heartbeat timer according to line rate.
 * @adapter: pointer to our adapter structure
 */
void et131x_set_rx_dma_timer(struct et131x_adapter *adapter)
{
	/* For version B silicon, we do not use the RxDMA timer for 10 and 100
	 * Mbits/s line rates. We do not enable and RxDMA interrupt coalescing.
	 */
	if ((adapter->linkspeed == TRUEPHY_SPEED_100MBPS) ||
	    (adapter->linkspeed == TRUEPHY_SPEED_10MBPS)) {
		writel(0, &adapter->regs->rxdma.max_pkt_time);
		writel(1, &adapter->regs->rxdma.num_pkt_done);
	}
}

/**
 * NICReturnRFD - Recycle a RFD and put it back onto the receive list
 * @adapter: pointer to our adapter
 * @rfd: pointer to the RFD
 */
static void nic_return_rfd(struct et131x_adapter *adapter, struct rfd *rfd)
{
	struct rx_ring *rx_local = &adapter->rx_ring;
	struct rxdma_regs __iomem *rx_dma = &adapter->regs->rxdma;
	u16 buff_index = rfd->bufferindex;
	u8 ring_index = rfd->ringindex;
	unsigned long flags;

	/* We don't use any of the OOB data besides status. Otherwise, we
	 * need to clean up OOB data
	 */
	if (
#ifdef USE_FBR0
	    (ring_index == 0 && buff_index < rx_local->fbr0_num_entries) ||
#endif
	    (ring_index == 1 && buff_index < rx_local->fbr1_num_entries)) {
		spin_lock_irqsave(&adapter->fbr_lock, flags);

		if (ring_index == 1) {
			struct fbr_desc *next =
			    (struct fbr_desc *) (rx_local->fbr1_ring_virtaddr) +
					 INDEX10(rx_local->local_fbr1_full);

			/* Handle the Free Buffer Ring advancement here. Write
			 * the PA / Buffer Index for the returned buffer into
			 * the oldest (next to be freed)FBR entry
			 */
			next->addr_hi = rx_local->fbr[1]->bus_high[buff_index];
			next->addr_lo = rx_local->fbr[1]->bus_low[buff_index];
			next->word2 = buff_index;

			writel(bump_free_buff_ring(&rx_local->local_fbr1_full,
				rx_local->fbr1_num_entries - 1),
				&rx_dma->fbr1_full_offset);
		}
#ifdef USE_FBR0
		else {
			struct fbr_desc *next = (struct fbr_desc *)
				rx_local->fbr0_ring_virtaddr +
					INDEX10(rx_local->local_fbr0_full);

			/* Handle the Free Buffer Ring advancement here. Write
			 * the PA / Buffer Index for the returned buffer into
			 * the oldest (next to be freed) FBR entry
			 */
			next->addr_hi = rx_local->fbr[0]->bus_high[buff_index];
			next->addr_lo = rx_local->fbr[0]->bus_low[buff_index];
			next->word2 = buff_index;

			writel(bump_free_buff_ring(&rx_local->local_fbr0_full,
					rx_local->fbr0_num_entries - 1),
			       &rx_dma->fbr0_full_offset);
		}
#endif
		spin_unlock_irqrestore(&adapter->fbr_lock, flags);
	} else {
		dev_err(&adapter->pdev->dev,
			  "%s illegal Buffer Index returned\n", __func__);
	}

	/* The processing on this RFD is done, so put it back on the tail of
	 * our list
	 */
	spin_lock_irqsave(&adapter->rcv_lock, flags);
	list_add_tail(&rfd->list_node, &rx_local->recv_list);
	rx_local->num_ready_recv++;
	spin_unlock_irqrestore(&adapter->rcv_lock, flags);

	WARN_ON(rx_local->num_ready_recv > rx_local->num_rfd);
}

/**
 * et131x_rx_dma_disable - Stop of Rx_DMA on the ET1310
 * @adapter: pointer to our adapter structure
 */
void et131x_rx_dma_disable(struct et131x_adapter *adapter)
{
	u32 csr;
	/* Setup the receive dma configuration register */
	writel(0x00002001, &adapter->regs->rxdma.csr);
	csr = readl(&adapter->regs->rxdma.csr);
	if ((csr & 0x00020000) == 0) {	/* Check halt status (bit 17) */
		udelay(5);
		csr = readl(&adapter->regs->rxdma.csr);
		if ((csr & 0x00020000) == 0)
			dev_err(&adapter->pdev->dev,
			"RX Dma failed to enter halt state. CSR 0x%08x\n",
				csr);
	}
}

/**
 * et131x_rx_dma_enable - re-start of Rx_DMA on the ET1310.
 * @adapter: pointer to our adapter structure
 */
void et131x_rx_dma_enable(struct et131x_adapter *adapter)
{
	/* Setup the receive dma configuration register for normal operation */
	u32 csr =  0x2000;	/* FBR1 enable */

	if (adapter->rx_ring.fbr1_buffsize == 4096)
		csr |= 0x0800;
	else if (adapter->rx_ring.fbr1_buffsize == 8192)
		csr |= 0x1000;
	else if (adapter->rx_ring.fbr1_buffsize == 16384)
		csr |= 0x1800;
#ifdef USE_FBR0
	csr |= 0x0400;		/* FBR0 enable */
	if (adapter->rx_ring.fbr0_buffsize == 256)
		csr |= 0x0100;
	else if (adapter->rx_ring.fbr0_buffsize == 512)
		csr |= 0x0200;
	else if (adapter->rx_ring.fbr0_buffsize == 1024)
		csr |= 0x0300;
#endif
	writel(csr, &adapter->regs->rxdma.csr);

	csr = readl(&adapter->regs->rxdma.csr);
	if ((csr & 0x00020000) != 0) {
		udelay(5);
		csr = readl(&adapter->regs->rxdma.csr);
		if ((csr & 0x00020000) != 0) {
			dev_err(&adapter->pdev->dev,
			    "RX Dma failed to exit halt state.  CSR 0x%08x\n",
				csr);
		}
	}
}

/**
 * nic_rx_pkts - Checks the hardware for available packets
 * @adapter: pointer to our adapter
 *
 * Returns rfd, a pointer to our MPRFD.
 *
 * Checks the hardware for available packets, using completion ring
 * If packets are available, it gets an RFD from the recv_list, attaches
 * the packet to it, puts the RFD in the RecvPendList, and also returns
 * the pointer to the RFD.
 */
static struct rfd *nic_rx_pkts(struct et131x_adapter *adapter)
{
	struct rx_ring *rx_local = &adapter->rx_ring;
	struct rx_status_block *status;
	struct pkt_stat_desc *psr;
	struct rfd *rfd;
	u32 i;
	u8 *buf;
	unsigned long flags;
	struct list_head *element;
	u8 ring_index;
	u16 buff_index;
	u32 len;
	u32 word0;
	u32 word1;

	/* RX Status block is written by the DMA engine prior to every
	 * interrupt. It contains the next to be used entry in the Packet
	 * Status Ring, and also the two Free Buffer rings.
	 */
	status = rx_local->rx_status_block;
	word1 = status->word1 >> 16;	/* Get the useful bits */

	/* Check the PSR and wrap bits do not match */
	if ((word1 & 0x1FFF) == (rx_local->local_psr_full & 0x1FFF))
		/* Looks like this ring is not updated yet */
		return NULL;

	/* The packet status ring indicates that data is available. */
	psr = (struct pkt_stat_desc *) (rx_local->ps_ring_virtaddr) +
			(rx_local->local_psr_full & 0xFFF);

	/* Grab any information that is required once the PSR is
	 * advanced, since we can no longer rely on the memory being
	 * accurate
	 */
	len = psr->word1 & 0xFFFF;
	ring_index = (psr->word1 >> 26) & 0x03;
	buff_index = (psr->word1 >> 16) & 0x3FF;
	word0 = psr->word0;

	/* Indicate that we have used this PSR entry. */
	/* FIXME wrap 12 */
	add_12bit(&rx_local->local_psr_full, 1);
	if (
	  (rx_local->local_psr_full & 0xFFF) > rx_local->psr_num_entries - 1) {
		/* Clear psr full and toggle the wrap bit */
		rx_local->local_psr_full &=  ~0xFFF;
		rx_local->local_psr_full ^= 0x1000;
	}

	writel(rx_local->local_psr_full,
	       &adapter->regs->rxdma.psr_full_offset);

#ifndef USE_FBR0
	if (ring_index != 1)
		return NULL;
#endif

#ifdef USE_FBR0
	if (ring_index > 1 ||
		(ring_index == 0 &&
		buff_index > rx_local->fbr0_num_entries - 1) ||
		(ring_index == 1 &&
		buff_index > rx_local->fbr1_num_entries - 1))
#else
	if (ring_index != 1 || buff_index > rx_local->fbr1_num_entries - 1)
#endif
	{
		/* Illegal buffer or ring index cannot be used by S/W*/
		dev_err(&adapter->pdev->dev,
			  "NICRxPkts PSR Entry %d indicates "
			  "length of %d and/or bad bi(%d)\n",
			  rx_local->local_psr_full & 0xFFF,
			  len, buff_index);
		return NULL;
	}

	/* Get and fill the RFD. */
	spin_lock_irqsave(&adapter->rcv_lock, flags);

	rfd = NULL;
	element = rx_local->recv_list.next;
	rfd = (struct rfd *) list_entry(element, struct rfd, list_node);

	if (rfd == NULL) {
		spin_unlock_irqrestore(&adapter->rcv_lock, flags);
		return NULL;
	}

	list_del(&rfd->list_node);
	rx_local->num_ready_recv--;

	spin_unlock_irqrestore(&adapter->rcv_lock, flags);

	rfd->bufferindex = buff_index;
	rfd->ringindex = ring_index;

	/* In V1 silicon, there is a bug which screws up filtering of
	 * runt packets.  Therefore runt packet filtering is disabled
	 * in the MAC and the packets are dropped here.  They are
	 * also counted here.
	 */
	if (len < (NIC_MIN_PACKET_SIZE + 4)) {
		adapter->stats.rx_other_errs++;
		len = 0;
	}

	if (len) {
		if (adapter->replica_phy_loopbk == 1) {
			buf = rx_local->fbr[ring_index]->virt[buff_index];

			if (memcmp(&buf[6], adapter->addr, ETH_ALEN) == 0) {
				if (memcmp(&buf[42], "Replica packet",
					   ETH_HLEN)) {
					adapter->replica_phy_loopbk_passfail = 1;
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
			if ((adapter->packet_filter &
					ET131X_PACKET_TYPE_MULTICAST)
			    && !(adapter->packet_filter &
					ET131X_PACKET_TYPE_PROMISCUOUS)
			    && !(adapter->packet_filter &
					ET131X_PACKET_TYPE_ALL_MULTICAST)) {
				buf = rx_local->fbr[ring_index]->
						virt[buff_index];

				/* Loop through our list to see if the
				 * destination address of this packet
				 * matches one in our list.
				 */
				for (i = 0; i < adapter->multicast_addr_count;
				     i++) {
					if (buf[0] ==
						adapter->multicast_list[i][0]
					    && buf[1] ==
						adapter->multicast_list[i][1]
					    && buf[2] ==
						adapter->multicast_list[i][2]
					    && buf[3] ==
						adapter->multicast_list[i][3]
					    && buf[4] ==
						adapter->multicast_list[i][4]
					    && buf[5] ==
						adapter->multicast_list[i][5]) {
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
				if (i == adapter->multicast_addr_count)
					len = 0;
			}

			if (len > 0)
				adapter->stats.multicast_pkts_rcvd++;
		} else if (word0 & ALCATEL_BROADCAST_PKT)
			adapter->stats.broadcast_pkts_rcvd++;
		else
			/* Not sure what this counter measures in
			 * promiscuous mode. Perhaps we should check
			 * the MAC address to see if it is directed
			 * to us in promiscuous mode.
			 */
			adapter->stats.unicast_pkts_rcvd++;
	}

	if (len > 0) {
		struct sk_buff *skb = NULL;

		/*rfd->len = len - 4; */
		rfd->len = len;

		skb = dev_alloc_skb(rfd->len + 2);
		if (!skb) {
			dev_err(&adapter->pdev->dev,
				  "Couldn't alloc an SKB for Rx\n");
			return NULL;
		}

		adapter->net_stats.rx_bytes += rfd->len;

		memcpy(skb_put(skb, rfd->len),
		       rx_local->fbr[ring_index]->virt[buff_index],
		       rfd->len);

		skb->dev = adapter->netdev;
		skb->protocol = eth_type_trans(skb, adapter->netdev);
		skb->ip_summed = CHECKSUM_NONE;

		netif_rx(skb);
	} else {
		rfd->len = 0;
	}

	nic_return_rfd(adapter, rfd);
	return rfd;
}

/**
 * et131x_reset_recv - Reset the receive list
 * @adapter: pointer to our adapter
 *
 * Assumption, Rcv spinlock has been acquired.
 */
void et131x_reset_recv(struct et131x_adapter *adapter)
{
	WARN_ON(list_empty(&adapter->rx_ring.recv_list));
}

/**
 * et131x_handle_recv_interrupt - Interrupt handler for receive processing
 * @adapter: pointer to our adapter
 *
 * Assumption, Rcv spinlock has been acquired.
 */
void et131x_handle_recv_interrupt(struct et131x_adapter *adapter)
{
	struct rfd *rfd = NULL;
	u32 count = 0;
	bool done = true;

	/* Process up to available RFD's */
	while (count < NUM_PACKETS_HANDLED) {
		if (list_empty(&adapter->rx_ring.recv_list)) {
			WARN_ON(adapter->rx_ring.num_ready_recv != 0);
			done = false;
			break;
		}

		rfd = nic_rx_pkts(adapter);

		if (rfd == NULL)
			break;

		/* Do not receive any packets until a filter has been set.
		 * Do not receive any packets until we have link.
		 * If length is zero, return the RFD in order to advance the
		 * Free buffer ring.
		 */
		if (!adapter->packet_filter ||
		    !netif_carrier_ok(adapter->netdev) ||
		    rfd->len == 0)
			continue;

		/* Increment the number of packets we received */
		adapter->net_stats.rx_packets++;

		/* Set the status on the packet, either resources or success */
		if (adapter->rx_ring.num_ready_recv < RFD_LOW_WATER_MARK) {
			dev_warn(&adapter->pdev->dev,
				    "RFD's are running out\n");
		}
		count++;
	}

	if (count == NUM_PACKETS_HANDLED || !done) {
		adapter->rx_ring.unfinished_receives = true;
		writel(PARM_TX_TIME_INT_DEF * NANO_IN_A_MICRO,
		       &adapter->regs->global.watchdog_timer);
	} else
		/* Watchdog timer will disable itself if appropriate. */
		adapter->rx_ring.unfinished_receives = false;
}

