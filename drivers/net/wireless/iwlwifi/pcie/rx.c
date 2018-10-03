/******************************************************************************
 *
 * Copyright(c) 2003 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/gfp.h>

#include "iwl-prph.h"
#include "iwl-io.h"
#include "internal.h"
#include "iwl-op-mode.h"

/******************************************************************************
 *
 * RX path functions
 *
 ******************************************************************************/

/*
 * Rx theory of operation
 *
 * Driver allocates a circular buffer of Receive Buffer Descriptors (RBDs),
 * each of which point to Receive Buffers to be filled by the NIC.  These get
 * used not only for Rx frames, but for any command response or notification
 * from the NIC.  The driver and NIC manage the Rx buffers by means
 * of indexes into the circular buffer.
 *
 * Rx Queue Indexes
 * The host/firmware share two index registers for managing the Rx buffers.
 *
 * The READ index maps to the first position that the firmware may be writing
 * to -- the driver can read up to (but not including) this position and get
 * good data.
 * The READ index is managed by the firmware once the card is enabled.
 *
 * The WRITE index maps to the last position the driver has read from -- the
 * position preceding WRITE is the last slot the firmware can place a packet.
 *
 * The queue is empty (no good data) if WRITE = READ - 1, and is full if
 * WRITE = READ.
 *
 * During initialization, the host sets up the READ queue position to the first
 * INDEX position, and WRITE to the last (READ - 1 wrapped)
 *
 * When the firmware places a packet in a buffer, it will advance the READ index
 * and fire the RX interrupt.  The driver can then query the READ index and
 * process as many packets as possible, moving the WRITE index forward as it
 * resets the Rx queue buffers with new memory.
 *
 * The management in the driver is as follows:
 * + A list of pre-allocated RBDs is stored in iwl->rxq->rx_free.
 *   When the interrupt handler is called, the request is processed.
 *   The page is either stolen - transferred to the upper layer
 *   or reused - added immediately to the iwl->rxq->rx_free list.
 * + When the page is stolen - the driver updates the matching queue's used
 *   count, detaches the RBD and transfers it to the queue used list.
 *   When there are two used RBDs - they are transferred to the allocator empty
 *   list. Work is then scheduled for the allocator to start allocating
 *   eight buffers.
 *   When there are another 6 used RBDs - they are transferred to the allocator
 *   empty list and the driver tries to claim the pre-allocated buffers and
 *   add them to iwl->rxq->rx_free. If it fails - it continues to claim them
 *   until ready.
 *   When there are 8+ buffers in the free list - either from allocation or from
 *   8 reused unstolen pages - restock is called to update the FW and indexes.
 * + In order to make sure the allocator always has RBDs to use for allocation
 *   the allocator has initial pool in the size of num_queues*(8-2) - the
 *   maximum missing RBDs per allocation request (request posted with 2
 *    empty RBDs, there is no guarantee when the other 6 RBDs are supplied).
 *   The queues supplies the recycle of the rest of the RBDs.
 * + A received packet is processed and handed to the kernel network stack,
 *   detached from the iwl->rxq.  The driver 'processed' index is updated.
 * + If there are no allocated buffers in iwl->rxq->rx_free,
 *   the READ INDEX is not incremented and iwl->status(RX_STALLED) is set.
 *   If there were enough free buffers and RX_STALLED is set it is cleared.
 *
 *
 * Driver sequence:
 *
 * iwl_rxq_alloc()            Allocates rx_free
 * iwl_pcie_rx_replenish()    Replenishes rx_free list from rx_used, and calls
 *                            iwl_pcie_rxq_restock.
 *                            Used only during initialization.
 * iwl_pcie_rxq_restock()     Moves available buffers from rx_free into Rx
 *                            queue, updates firmware pointers, and updates
 *                            the WRITE index.
 * iwl_pcie_rx_allocator()     Background work for allocating pages.
 *
 * -- enable interrupts --
 * ISR - iwl_rx()             Detach iwl_rx_mem_buffers from pool up to the
 *                            READ INDEX, detaching the SKB from the pool.
 *                            Moves the packet buffer from queue to rx_used.
 *                            Posts and claims requests to the allocator.
 *                            Calls iwl_pcie_rxq_restock to refill any empty
 *                            slots.
 *
 * RBD life-cycle:
 *
 * Init:
 * rxq.pool -> rxq.rx_used -> rxq.rx_free -> rxq.queue
 *
 * Regular Receive interrupt:
 * Page Stolen:
 * rxq.queue -> rxq.rx_used -> allocator.rbd_empty ->
 * allocator.rbd_allocated -> rxq.rx_free -> rxq.queue
 * Page not Stolen:
 * rxq.queue -> rxq.rx_free -> rxq.queue
 * ...
 *
 */

/*
 * iwl_rxq_space - Return number of free slots available in queue.
 */
static int iwl_rxq_space(const struct iwl_rxq *rxq)
{
	/* Make sure RX_QUEUE_SIZE is a power of 2 */
	BUILD_BUG_ON(RX_QUEUE_SIZE & (RX_QUEUE_SIZE - 1));

	/*
	 * There can be up to (RX_QUEUE_SIZE - 1) free slots, to avoid ambiguity
	 * between empty and completely full queues.
	 * The following is equivalent to modulo by RX_QUEUE_SIZE and is well
	 * defined for negative dividends.
	 */
	return (rxq->read - rxq->write - 1) & (RX_QUEUE_SIZE - 1);
}

/*
 * iwl_dma_addr2rbd_ptr - convert a DMA address to a uCode read buffer ptr
 */
static inline __le32 iwl_pcie_dma_addr2rbd_ptr(dma_addr_t dma_addr)
{
	return cpu_to_le32((u32)(dma_addr >> 8));
}

/*
 * iwl_pcie_rx_stop - stops the Rx DMA
 */
int iwl_pcie_rx_stop(struct iwl_trans *trans)
{
	iwl_write_direct32(trans, FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
	return iwl_poll_direct_bit(trans, FH_MEM_RSSR_RX_STATUS_REG,
				   FH_RSSR_CHNL0_RX_STATUS_CHNL_IDLE, 1000);
}

/*
 * iwl_pcie_rxq_inc_wr_ptr - Update the write pointer for the RX queue
 */
static void iwl_pcie_rxq_inc_wr_ptr(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	u32 reg;

	lockdep_assert_held(&rxq->lock);

	/*
	 * explicitly wake up the NIC if:
	 * 1. shadow registers aren't enabled
	 * 2. there is a chance that the NIC is asleep
	 */
	if (!trans->cfg->base_params->shadow_reg_enable &&
	    test_bit(STATUS_TPOWER_PMI, &trans->status)) {
		reg = iwl_read32(trans, CSR_UCODE_DRV_GP1);

		if (reg & CSR_UCODE_DRV_GP1_BIT_MAC_SLEEP) {
			IWL_DEBUG_INFO(trans, "Rx queue requesting wakeup, GP1 = 0x%x\n",
				       reg);
			iwl_set_bit(trans, CSR_GP_CNTRL,
				    CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
			rxq->need_update = true;
			return;
		}
	}

	rxq->write_actual = round_down(rxq->write, 8);
	iwl_write32(trans, FH_RSCSR_CHNL0_WPTR, rxq->write_actual);
}

static void iwl_pcie_rxq_check_wrptr(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;

	spin_lock(&rxq->lock);

	if (!rxq->need_update)
		goto exit_unlock;

	iwl_pcie_rxq_inc_wr_ptr(trans);
	rxq->need_update = false;

 exit_unlock:
	spin_unlock(&rxq->lock);
}

/*
 * iwl_pcie_rxq_restock - refill RX queue from pre-allocated pool
 *
 * If there are slots in the RX queue that need to be restocked,
 * and we have free pre-allocated buffers, fill the ranks as much
 * as we can, pulling from rx_free.
 *
 * This moves the 'write' index forward to catch up with 'processed', and
 * also updates the memory address in the firmware to reference the new
 * target buffer.
 */
static void iwl_pcie_rxq_restock(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	struct iwl_rx_mem_buffer *rxb;

	/*
	 * If the device isn't enabled - not need to try to add buffers...
	 * This can happen when we stop the device and still have an interrupt
	 * pending. We stop the APM before we sync the interrupts because we
	 * have to (see comment there). On the other hand, since the APM is
	 * stopped, we cannot access the HW (in particular not prph).
	 * So don't try to restock if the APM has been already stopped.
	 */
	if (!test_bit(STATUS_DEVICE_ENABLED, &trans->status))
		return;

	spin_lock(&rxq->lock);
	while ((iwl_rxq_space(rxq) > 0) && (rxq->free_count)) {
		/* The overwritten rxb must be a used one */
		rxb = rxq->queue[rxq->write];
		BUG_ON(rxb && rxb->page);

		/* Get next free Rx buffer, remove from free list */
		rxb = list_first_entry(&rxq->rx_free, struct iwl_rx_mem_buffer,
				       list);
		list_del(&rxb->list);

		/* Point to Rx buffer via next RBD in circular buffer */
		rxq->bd[rxq->write] = iwl_pcie_dma_addr2rbd_ptr(rxb->page_dma);
		rxq->queue[rxq->write] = rxb;
		rxq->write = (rxq->write + 1) & RX_QUEUE_MASK;
		rxq->free_count--;
	}
	spin_unlock(&rxq->lock);

	/* If we've added more space for the firmware to place data, tell it.
	 * Increment device's write pointer in multiples of 8. */
	if (rxq->write_actual != (rxq->write & ~0x7)) {
		spin_lock(&rxq->lock);
		iwl_pcie_rxq_inc_wr_ptr(trans);
		spin_unlock(&rxq->lock);
	}
}

/*
 * iwl_pcie_rx_alloc_page - allocates and returns a page.
 *
 */
static struct page *iwl_pcie_rx_alloc_page(struct iwl_trans *trans,
					   gfp_t priority)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	struct page *page;
	gfp_t gfp_mask = priority;

	if (rxq->free_count > RX_LOW_WATERMARK)
		gfp_mask |= __GFP_NOWARN;

	if (trans_pcie->rx_page_order > 0)
		gfp_mask |= __GFP_COMP;

	/* Alloc a new receive buffer */
	page = alloc_pages(gfp_mask, trans_pcie->rx_page_order);
	if (!page) {
		if (net_ratelimit())
			IWL_DEBUG_INFO(trans, "alloc_pages failed, order: %d\n",
				       trans_pcie->rx_page_order);
		/* Issue an error if the hardware has consumed more than half
		 * of its free buffer list and we don't have enough
		 * pre-allocated buffers.
`		 */
		if (rxq->free_count <= RX_LOW_WATERMARK &&
		    iwl_rxq_space(rxq) > (RX_QUEUE_SIZE / 2) &&
		    net_ratelimit())
			IWL_CRIT(trans,
				 "Failed to alloc_pages with GFP_KERNEL. Only %u free buffers remaining.\n",
				 rxq->free_count);
		return NULL;
	}
	return page;
}

/*
 * iwl_pcie_rxq_alloc_rbs - allocate a page for each used RBD
 *
 * A used RBD is an Rx buffer that has been given to the stack. To use it again
 * a page must be allocated and the RBD must point to the page. This function
 * doesn't change the HW pointer but handles the list of pages that is used by
 * iwl_pcie_rxq_restock. The latter function will update the HW to use the newly
 * allocated buffers.
 */
static void iwl_pcie_rxq_alloc_rbs(struct iwl_trans *trans, gfp_t priority)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	struct iwl_rx_mem_buffer *rxb;
	struct page *page;

	while (1) {
		spin_lock(&rxq->lock);
		if (list_empty(&rxq->rx_used)) {
			spin_unlock(&rxq->lock);
			return;
		}
		spin_unlock(&rxq->lock);

		/* Alloc a new receive buffer */
		page = iwl_pcie_rx_alloc_page(trans, priority);
		if (!page)
			return;

		spin_lock(&rxq->lock);

		if (list_empty(&rxq->rx_used)) {
			spin_unlock(&rxq->lock);
			__free_pages(page, trans_pcie->rx_page_order);
			return;
		}
		rxb = list_first_entry(&rxq->rx_used, struct iwl_rx_mem_buffer,
				       list);
		list_del(&rxb->list);
		spin_unlock(&rxq->lock);

		BUG_ON(rxb->page);
		rxb->page = page;
		/* Get physical address of the RB */
		rxb->page_dma =
			dma_map_page(trans->dev, page, 0,
				     PAGE_SIZE << trans_pcie->rx_page_order,
				     DMA_FROM_DEVICE);
		if (dma_mapping_error(trans->dev, rxb->page_dma)) {
			rxb->page = NULL;
			spin_lock(&rxq->lock);
			list_add(&rxb->list, &rxq->rx_used);
			spin_unlock(&rxq->lock);
			__free_pages(page, trans_pcie->rx_page_order);
			return;
		}
		/* dma address must be no more than 36 bits */
		BUG_ON(rxb->page_dma & ~DMA_BIT_MASK(36));
		/* and also 256 byte aligned! */
		BUG_ON(rxb->page_dma & DMA_BIT_MASK(8));

		spin_lock(&rxq->lock);

		list_add_tail(&rxb->list, &rxq->rx_free);
		rxq->free_count++;

		spin_unlock(&rxq->lock);
	}
}

static void iwl_pcie_rxq_free_rbs(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	int i;

	lockdep_assert_held(&rxq->lock);

	for (i = 0; i < RX_QUEUE_SIZE; i++) {
		if (!rxq->pool[i].page)
			continue;
		dma_unmap_page(trans->dev, rxq->pool[i].page_dma,
			       PAGE_SIZE << trans_pcie->rx_page_order,
			       DMA_FROM_DEVICE);
		__free_pages(rxq->pool[i].page, trans_pcie->rx_page_order);
		rxq->pool[i].page = NULL;
	}
}

/*
 * iwl_pcie_rx_replenish - Move all used buffers from rx_used to rx_free
 *
 * When moving to rx_free an page is allocated for the slot.
 *
 * Also restock the Rx queue via iwl_pcie_rxq_restock.
 * This is called only during initialization
 */
static void iwl_pcie_rx_replenish(struct iwl_trans *trans)
{
	iwl_pcie_rxq_alloc_rbs(trans, GFP_KERNEL);

	iwl_pcie_rxq_restock(trans);
}

/*
 * iwl_pcie_rx_allocator - Allocates pages in the background for RX queues
 *
 * Allocates for each received request 8 pages
 * Called as a scheduled work item.
 */
static void iwl_pcie_rx_allocator(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rb_allocator *rba = &trans_pcie->rba;
	struct list_head local_empty;
	int pending = atomic_xchg(&rba->req_pending, 0);

	IWL_DEBUG_RX(trans, "Pending allocation requests = %d\n", pending);

	/* If we were scheduled - there is at least one request */
	spin_lock(&rba->lock);
	/* swap out the rba->rbd_empty to a local list */
	list_replace_init(&rba->rbd_empty, &local_empty);
	spin_unlock(&rba->lock);

	while (pending) {
		int i;
		struct list_head local_allocated;

		INIT_LIST_HEAD(&local_allocated);

		for (i = 0; i < RX_CLAIM_REQ_ALLOC;) {
			struct iwl_rx_mem_buffer *rxb;
			struct page *page;

			/* List should never be empty - each reused RBD is
			 * returned to the list, and initial pool covers any
			 * possible gap between the time the page is allocated
			 * to the time the RBD is added.
			 */
			BUG_ON(list_empty(&local_empty));
			/* Get the first rxb from the rbd list */
			rxb = list_first_entry(&local_empty,
					       struct iwl_rx_mem_buffer, list);
			BUG_ON(rxb->page);

			/* Alloc a new receive buffer */
			page = iwl_pcie_rx_alloc_page(trans, GFP_KERNEL);
			if (!page)
				continue;
			rxb->page = page;

			/* Get physical address of the RB */
			rxb->page_dma = dma_map_page(trans->dev, page, 0,
					PAGE_SIZE << trans_pcie->rx_page_order,
					DMA_FROM_DEVICE);
			if (dma_mapping_error(trans->dev, rxb->page_dma)) {
				rxb->page = NULL;
				__free_pages(page, trans_pcie->rx_page_order);
				continue;
			}
			/* dma address must be no more than 36 bits */
			BUG_ON(rxb->page_dma & ~DMA_BIT_MASK(36));
			/* and also 256 byte aligned! */
			BUG_ON(rxb->page_dma & DMA_BIT_MASK(8));

			/* move the allocated entry to the out list */
			list_move(&rxb->list, &local_allocated);
			i++;
		}

		pending--;
		if (!pending) {
			pending = atomic_xchg(&rba->req_pending, 0);
			IWL_DEBUG_RX(trans,
				     "Pending allocation requests = %d\n",
				     pending);
		}

		spin_lock(&rba->lock);
		/* add the allocated rbds to the allocator allocated list */
		list_splice_tail(&local_allocated, &rba->rbd_allocated);
		/* get more empty RBDs for current pending requests */
		list_splice_tail_init(&rba->rbd_empty, &local_empty);
		spin_unlock(&rba->lock);

		atomic_inc(&rba->req_ready);
	}

	spin_lock(&rba->lock);
	/* return unused rbds to the allocator empty list */
	list_splice_tail(&local_empty, &rba->rbd_empty);
	spin_unlock(&rba->lock);
}

/*
 * iwl_pcie_rx_allocator_get - Returns the pre-allocated pages
.*
.* Called by queue when the queue posted allocation request and
 * has freed 8 RBDs in order to restock itself.
 */
static int iwl_pcie_rx_allocator_get(struct iwl_trans *trans,
				     struct iwl_rx_mem_buffer
				     *out[RX_CLAIM_REQ_ALLOC])
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rb_allocator *rba = &trans_pcie->rba;
	int i;

	/*
	 * atomic_dec_if_positive returns req_ready - 1 for any scenario.
	 * If req_ready is 0 atomic_dec_if_positive will return -1 and this
	 * function will return -ENOMEM, as there are no ready requests.
	 * atomic_dec_if_positive will perofrm the *actual* decrement only if
	 * req_ready > 0, i.e. - there are ready requests and the function
	 * hands one request to the caller.
	 */
	if (atomic_dec_if_positive(&rba->req_ready) < 0)
		return -ENOMEM;

	spin_lock(&rba->lock);
	for (i = 0; i < RX_CLAIM_REQ_ALLOC; i++) {
		/* Get next free Rx buffer, remove it from free list */
		out[i] = list_first_entry(&rba->rbd_allocated,
			       struct iwl_rx_mem_buffer, list);
		list_del(&out[i]->list);
	}
	spin_unlock(&rba->lock);

	return 0;
}

static void iwl_pcie_rx_allocator_work(struct work_struct *data)
{
	struct iwl_rb_allocator *rba_p =
		container_of(data, struct iwl_rb_allocator, rx_alloc);
	struct iwl_trans_pcie *trans_pcie =
		container_of(rba_p, struct iwl_trans_pcie, rba);

	iwl_pcie_rx_allocator(trans_pcie->trans);
}

static int iwl_pcie_rx_alloc(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	struct iwl_rb_allocator *rba = &trans_pcie->rba;
	struct device *dev = trans->dev;

	memset(&trans_pcie->rxq, 0, sizeof(trans_pcie->rxq));

	spin_lock_init(&rxq->lock);
	spin_lock_init(&rba->lock);

	if (WARN_ON(rxq->bd || rxq->rb_stts))
		return -EINVAL;

	/* Allocate the circular buffer of Read Buffer Descriptors (RBDs) */
	rxq->bd = dma_zalloc_coherent(dev, sizeof(__le32) * RX_QUEUE_SIZE,
				      &rxq->bd_dma, GFP_KERNEL);
	if (!rxq->bd)
		goto err_bd;

	/*Allocate the driver's pointer to receive buffer status */
	rxq->rb_stts = dma_zalloc_coherent(dev, sizeof(*rxq->rb_stts),
					   &rxq->rb_stts_dma, GFP_KERNEL);
	if (!rxq->rb_stts)
		goto err_rb_stts;

	return 0;

err_rb_stts:
	dma_free_coherent(dev, sizeof(__le32) * RX_QUEUE_SIZE,
			  rxq->bd, rxq->bd_dma);
	rxq->bd_dma = 0;
	rxq->bd = NULL;
err_bd:
	return -ENOMEM;
}

static void iwl_pcie_rx_hw_init(struct iwl_trans *trans, struct iwl_rxq *rxq)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 rb_size;
	const u32 rfdnlog = RX_QUEUE_SIZE_LOG; /* 256 RBDs */

	if (trans_pcie->rx_buf_size_8k)
		rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_8K;
	else
		rb_size = FH_RCSR_RX_CONFIG_REG_VAL_RB_SIZE_4K;

	/* Stop Rx DMA */
	iwl_write_direct32(trans, FH_MEM_RCSR_CHNL0_CONFIG_REG, 0);
	/* reset and flush pointers */
	iwl_write_direct32(trans, FH_MEM_RCSR_CHNL0_RBDCB_WPTR, 0);
	iwl_write_direct32(trans, FH_MEM_RCSR_CHNL0_FLUSH_RB_REQ, 0);
	iwl_write_direct32(trans, FH_RSCSR_CHNL0_RDPTR, 0);

	/* Reset driver's Rx queue write index */
	iwl_write_direct32(trans, FH_RSCSR_CHNL0_RBDCB_WPTR_REG, 0);

	/* Tell device where to find RBD circular buffer in DRAM */
	iwl_write_direct32(trans, FH_RSCSR_CHNL0_RBDCB_BASE_REG,
			   (u32)(rxq->bd_dma >> 8));

	/* Tell device where in DRAM to update its Rx status */
	iwl_write_direct32(trans, FH_RSCSR_CHNL0_STTS_WPTR_REG,
			   rxq->rb_stts_dma >> 4);

	/* Enable Rx DMA
	 * FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY is set because of HW bug in
	 *      the credit mechanism in 5000 HW RX FIFO
	 * Direct rx interrupts to hosts
	 * Rx buffer size 4 or 8k
	 * RB timeout 0x10
	 * 256 RBDs
	 */
	iwl_write_direct32(trans, FH_MEM_RCSR_CHNL0_CONFIG_REG,
			   FH_RCSR_RX_CONFIG_CHNL_EN_ENABLE_VAL |
			   FH_RCSR_CHNL0_RX_IGNORE_RXF_EMPTY |
			   FH_RCSR_CHNL0_RX_CONFIG_IRQ_DEST_INT_HOST_VAL |
			   rb_size|
			   (RX_RB_TIMEOUT << FH_RCSR_RX_CONFIG_REG_IRQ_RBTH_POS)|
			   (rfdnlog << FH_RCSR_RX_CONFIG_RBDCB_SIZE_POS));

	/* Set interrupt coalescing timer to default (2048 usecs) */
	iwl_write8(trans, CSR_INT_COALESCING, IWL_HOST_INT_TIMEOUT_DEF);

	/* W/A for interrupt coalescing bug in 7260 and 3160 */
	if (trans->cfg->host_interrupt_operation_mode)
		iwl_set_bit(trans, CSR_INT_COALESCING, IWL_HOST_INT_OPER_MODE);
}

static void iwl_pcie_rx_init_rxb_lists(struct iwl_rxq *rxq)
{
	int i;

	lockdep_assert_held(&rxq->lock);

	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);
	rxq->free_count = 0;
	rxq->used_count = 0;

	for (i = 0; i < RX_QUEUE_SIZE; i++)
		list_add(&rxq->pool[i].list, &rxq->rx_used);
}

static void iwl_pcie_rx_init_rba(struct iwl_rb_allocator *rba)
{
	int i;

	lockdep_assert_held(&rba->lock);

	INIT_LIST_HEAD(&rba->rbd_allocated);
	INIT_LIST_HEAD(&rba->rbd_empty);

	for (i = 0; i < RX_POOL_SIZE; i++)
		list_add(&rba->pool[i].list, &rba->rbd_empty);
}

static void iwl_pcie_rx_free_rba(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rb_allocator *rba = &trans_pcie->rba;
	int i;

	lockdep_assert_held(&rba->lock);

	for (i = 0; i < RX_POOL_SIZE; i++) {
		if (!rba->pool[i].page)
			continue;
		dma_unmap_page(trans->dev, rba->pool[i].page_dma,
			       PAGE_SIZE << trans_pcie->rx_page_order,
			       DMA_FROM_DEVICE);
		__free_pages(rba->pool[i].page, trans_pcie->rx_page_order);
		rba->pool[i].page = NULL;
	}
}

int iwl_pcie_rx_init(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	struct iwl_rb_allocator *rba = &trans_pcie->rba;
	int i, err;

	if (!rxq->bd) {
		err = iwl_pcie_rx_alloc(trans);
		if (err)
			return err;
	}
	if (!rba->alloc_wq)
		rba->alloc_wq = alloc_workqueue("rb_allocator",
						WQ_HIGHPRI | WQ_UNBOUND, 1);
	INIT_WORK(&rba->rx_alloc, iwl_pcie_rx_allocator_work);

	cancel_work_sync(&rba->rx_alloc);

	spin_lock(&rba->lock);
	atomic_set(&rba->req_pending, 0);
	atomic_set(&rba->req_ready, 0);
	/* free all first - we might be reconfigured for a different size */
	iwl_pcie_rx_free_rba(trans);
	iwl_pcie_rx_init_rba(rba);
	spin_unlock(&rba->lock);

	spin_lock(&rxq->lock);

	/* free all first - we might be reconfigured for a different size */
	iwl_pcie_rxq_free_rbs(trans);
	iwl_pcie_rx_init_rxb_lists(rxq);

	for (i = 0; i < RX_QUEUE_SIZE; i++)
		rxq->queue[i] = NULL;

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->write_actual = 0;
	memset(rxq->rb_stts, 0, sizeof(*rxq->rb_stts));
	spin_unlock(&rxq->lock);

	iwl_pcie_rx_replenish(trans);

	iwl_pcie_rx_hw_init(trans, rxq);

	spin_lock(&rxq->lock);
	iwl_pcie_rxq_inc_wr_ptr(trans);
	spin_unlock(&rxq->lock);

	return 0;
}

void iwl_pcie_rx_free(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	struct iwl_rb_allocator *rba = &trans_pcie->rba;

	/*if rxq->bd is NULL, it means that nothing has been allocated,
	 * exit now */
	if (!rxq->bd) {
		IWL_DEBUG_INFO(trans, "Free NULL rx context\n");
		return;
	}

	cancel_work_sync(&rba->rx_alloc);
	if (rba->alloc_wq) {
		destroy_workqueue(rba->alloc_wq);
		rba->alloc_wq = NULL;
	}

	spin_lock(&rba->lock);
	iwl_pcie_rx_free_rba(trans);
	spin_unlock(&rba->lock);

	spin_lock(&rxq->lock);
	iwl_pcie_rxq_free_rbs(trans);
	spin_unlock(&rxq->lock);

	dma_free_coherent(trans->dev, sizeof(__le32) * RX_QUEUE_SIZE,
			  rxq->bd, rxq->bd_dma);
	rxq->bd_dma = 0;
	rxq->bd = NULL;

	if (rxq->rb_stts)
		dma_free_coherent(trans->dev,
				  sizeof(struct iwl_rb_status),
				  rxq->rb_stts, rxq->rb_stts_dma);
	else
		IWL_DEBUG_INFO(trans, "Free rxq->rb_stts which is NULL\n");
	rxq->rb_stts_dma = 0;
	rxq->rb_stts = NULL;
}

/*
 * iwl_pcie_rx_reuse_rbd - Recycle used RBDs
 *
 * Called when a RBD can be reused. The RBD is transferred to the allocator.
 * When there are 2 empty RBDs - a request for allocation is posted
 */
static void iwl_pcie_rx_reuse_rbd(struct iwl_trans *trans,
				  struct iwl_rx_mem_buffer *rxb,
				  struct iwl_rxq *rxq, bool emergency)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rb_allocator *rba = &trans_pcie->rba;

	/* Move the RBD to the used list, will be moved to allocator in batches
	 * before claiming or posting a request*/
	list_add_tail(&rxb->list, &rxq->rx_used);

	if (unlikely(emergency))
		return;

	/* Count the allocator owned RBDs */
	rxq->used_count++;

	/* If we have RX_POST_REQ_ALLOC new released rx buffers -
	 * issue a request for allocator. Modulo RX_CLAIM_REQ_ALLOC is
	 * used for the case we failed to claim RX_CLAIM_REQ_ALLOC,
	 * after but we still need to post another request.
	 */
	if ((rxq->used_count % RX_CLAIM_REQ_ALLOC) == RX_POST_REQ_ALLOC) {
		/* Move the 2 RBDs to the allocator ownership.
		 Allocator has another 6 from pool for the request completion*/
		spin_lock(&rba->lock);
		list_splice_tail_init(&rxq->rx_used, &rba->rbd_empty);
		spin_unlock(&rba->lock);

		atomic_inc(&rba->req_pending);
		queue_work(rba->alloc_wq, &rba->rx_alloc);
	}
}

static void iwl_pcie_rx_handle_rb(struct iwl_trans *trans,
				struct iwl_rx_mem_buffer *rxb,
				bool emergency)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	struct iwl_txq *txq = &trans_pcie->txq[trans_pcie->cmd_queue];
	bool page_stolen = false;
	int max_len = PAGE_SIZE << trans_pcie->rx_page_order;
	u32 offset = 0;

	if (WARN_ON(!rxb))
		return;

	dma_unmap_page(trans->dev, rxb->page_dma, max_len, DMA_FROM_DEVICE);

	while (offset + sizeof(u32) + sizeof(struct iwl_cmd_header) < max_len) {
		struct iwl_rx_packet *pkt;
		u16 sequence;
		bool reclaim;
		int index, cmd_index, len;
		struct iwl_rx_cmd_buffer rxcb = {
			._offset = offset,
			._rx_page_order = trans_pcie->rx_page_order,
			._page = rxb->page,
			._page_stolen = false,
			.truesize = max_len,
		};

		pkt = rxb_addr(&rxcb);

		if (pkt->len_n_flags == cpu_to_le32(FH_RSCSR_FRAME_INVALID))
			break;

		IWL_DEBUG_RX(trans,
			     "cmd at offset %d: %s (0x%.2x, seq 0x%x)\n",
			     rxcb._offset,
			     get_cmd_string(trans_pcie, pkt->hdr.cmd),
			     pkt->hdr.cmd, le16_to_cpu(pkt->hdr.sequence));

		len = iwl_rx_packet_len(pkt);
		len += sizeof(u32); /* account for status word */
		trace_iwlwifi_dev_rx(trans->dev, trans, pkt, len);
		trace_iwlwifi_dev_rx_data(trans->dev, trans, pkt, len);

		/* Reclaim a command buffer only if this packet is a response
		 *   to a (driver-originated) command.
		 * If the packet (e.g. Rx frame) originated from uCode,
		 *   there is no command buffer to reclaim.
		 * Ucode should set SEQ_RX_FRAME bit if ucode-originated,
		 *   but apparently a few don't get set; catch them here. */
		reclaim = !(pkt->hdr.sequence & SEQ_RX_FRAME);
		if (reclaim) {
			int i;

			for (i = 0; i < trans_pcie->n_no_reclaim_cmds; i++) {
				if (trans_pcie->no_reclaim_cmds[i] ==
							pkt->hdr.cmd) {
					reclaim = false;
					break;
				}
			}
		}

		sequence = le16_to_cpu(pkt->hdr.sequence);
		index = SEQ_TO_INDEX(sequence);
		cmd_index = get_cmd_index(&txq->q, index);

		iwl_op_mode_rx(trans->op_mode, &trans_pcie->napi, &rxcb);

		if (reclaim) {
			kzfree(txq->entries[cmd_index].free_buf);
			txq->entries[cmd_index].free_buf = NULL;
		}

		/*
		 * After here, we should always check rxcb._page_stolen,
		 * if it is true then one of the handlers took the page.
		 */

		if (reclaim) {
			/* Invoke any callbacks, transfer the buffer to caller,
			 * and fire off the (possibly) blocking
			 * iwl_trans_send_cmd()
			 * as we reclaim the driver command queue */
			if (!rxcb._page_stolen)
				iwl_pcie_hcmd_complete(trans, &rxcb);
			else
				IWL_WARN(trans, "Claim null rxb?\n");
		}

		page_stolen |= rxcb._page_stolen;
		offset += ALIGN(len, FH_RSCSR_FRAME_ALIGN);
	}

	/* page was stolen from us -- free our reference */
	if (page_stolen) {
		__free_pages(rxb->page, trans_pcie->rx_page_order);
		rxb->page = NULL;
	}

	/* Reuse the page if possible. For notification packets and
	 * SKBs that fail to Rx correctly, add them back into the
	 * rx_free list for reuse later. */
	if (rxb->page != NULL) {
		rxb->page_dma =
			dma_map_page(trans->dev, rxb->page, 0,
				     PAGE_SIZE << trans_pcie->rx_page_order,
				     DMA_FROM_DEVICE);
		if (dma_mapping_error(trans->dev, rxb->page_dma)) {
			/*
			 * free the page(s) as well to not break
			 * the invariant that the items on the used
			 * list have no page(s)
			 */
			__free_pages(rxb->page, trans_pcie->rx_page_order);
			rxb->page = NULL;
			iwl_pcie_rx_reuse_rbd(trans, rxb, rxq, emergency);
		} else {
			list_add_tail(&rxb->list, &rxq->rx_free);
			rxq->free_count++;
		}
	} else
		iwl_pcie_rx_reuse_rbd(trans, rxb, rxq, emergency);
}

/*
 * iwl_pcie_rx_handle - Main entry function for receiving responses from fw
 */
static void iwl_pcie_rx_handle(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct iwl_rxq *rxq = &trans_pcie->rxq;
	u32 r, i, j, count = 0;
	bool emergency = false;

restart:
	spin_lock(&rxq->lock);
	/* uCode's read index (stored in shared DRAM) indicates the last Rx
	 * buffer that the driver may process (last buffer filled by ucode). */
	r = le16_to_cpu(ACCESS_ONCE(rxq->rb_stts->closed_rb_num)) & 0x0FFF;
	i = rxq->read;

	/* Rx interrupt, but nothing sent from uCode */
	if (i == r)
		IWL_DEBUG_RX(trans, "HW = SW = %d\n", r);

	while (i != r) {
		struct iwl_rx_mem_buffer *rxb;

		if (unlikely(rxq->used_count == RX_QUEUE_SIZE / 2))
			emergency = true;

		rxb = rxq->queue[i];
		rxq->queue[i] = NULL;

		IWL_DEBUG_RX(trans, "rxbuf: HW = %d, SW = %d (%p)\n",
			     r, i, rxb);
		iwl_pcie_rx_handle_rb(trans, rxb, emergency);

		i = (i + 1) & RX_QUEUE_MASK;

		/* If we have RX_CLAIM_REQ_ALLOC released rx buffers -
		 * try to claim the pre-allocated buffers from the allocator */
		if (rxq->used_count >= RX_CLAIM_REQ_ALLOC) {
			struct iwl_rb_allocator *rba = &trans_pcie->rba;
			struct iwl_rx_mem_buffer *out[RX_CLAIM_REQ_ALLOC];

			if (rxq->used_count % RX_CLAIM_REQ_ALLOC == 0 &&
			    !emergency) {
				/* Add the remaining 6 empty RBDs
				* for allocator use
				 */
				spin_lock(&rba->lock);
				list_splice_tail_init(&rxq->rx_used,
						      &rba->rbd_empty);
				spin_unlock(&rba->lock);
			}

			/* If not ready - continue, will try to reclaim later.
			* No need to reschedule work - allocator exits only on
			* success */
			if (!iwl_pcie_rx_allocator_get(trans, out)) {
				/* If success - then RX_CLAIM_REQ_ALLOC
				 * buffers were retrieved and should be added
				 * to free list */
				rxq->used_count -= RX_CLAIM_REQ_ALLOC;
				for (j = 0; j < RX_CLAIM_REQ_ALLOC; j++) {
					list_add_tail(&out[j]->list,
						      &rxq->rx_free);
					rxq->free_count++;
				}
			}
		}
		if (emergency) {
			count++;
			if (count == 8) {
				count = 0;
				if (rxq->used_count < RX_QUEUE_SIZE / 3)
					emergency = false;
				spin_unlock(&rxq->lock);
				iwl_pcie_rxq_alloc_rbs(trans, GFP_ATOMIC);
				spin_lock(&rxq->lock);
			}
		}
		/* handle restock for three cases, can be all of them at once:
		* - we just pulled buffers from the allocator
		* - we have 8+ unstolen pages accumulated
		* - we are in emergency and allocated buffers
		 */
		if (rxq->free_count >=  RX_CLAIM_REQ_ALLOC) {
			rxq->read = i;
			spin_unlock(&rxq->lock);
			iwl_pcie_rxq_restock(trans);
			goto restart;
		}
	}

	/* Backtrack one entry */
	rxq->read = i;
	spin_unlock(&rxq->lock);

	/*
	 * handle a case where in emergency there are some unallocated RBDs.
	 * those RBDs are in the used list, but are not tracked by the queue's
	 * used_count which counts allocator owned RBDs.
	 * unallocated emergency RBDs must be allocated on exit, otherwise
	 * when called again the function may not be in emergency mode and
	 * they will be handed to the allocator with no tracking in the RBD
	 * allocator counters, which will lead to them never being claimed back
	 * by the queue.
	 * by allocating them here, they are now in the queue free list, and
	 * will be restocked by the next call of iwl_pcie_rxq_restock.
	 */
	if (unlikely(emergency && count))
		iwl_pcie_rxq_alloc_rbs(trans, GFP_ATOMIC);

	if (trans_pcie->napi.poll)
		napi_gro_flush(&trans_pcie->napi, false);
}

/*
 * iwl_pcie_irq_handle_error - called for HW or SW error interrupt from card
 */
static void iwl_pcie_irq_handle_error(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int i;

	/* W/A for WiFi/WiMAX coex and WiMAX own the RF */
	if (trans->cfg->internal_wimax_coex &&
	    !trans->cfg->apmg_not_supported &&
	    (!(iwl_read_prph(trans, APMG_CLK_CTRL_REG) &
			     APMS_CLK_VAL_MRB_FUNC_MODE) ||
	     (iwl_read_prph(trans, APMG_PS_CTRL_REG) &
			    APMG_PS_CTRL_VAL_RESET_REQ))) {
		clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
		iwl_op_mode_wimax_active(trans->op_mode);
		wake_up(&trans_pcie->wait_command_queue);
		return;
	}

	iwl_pcie_dump_csr(trans);
	iwl_dump_fh(trans, NULL);

	local_bh_disable();
	/* The STATUS_FW_ERROR bit is set in this function. This must happen
	 * before we wake up the command caller, to ensure a proper cleanup. */
	iwl_trans_fw_error(trans);
	local_bh_enable();

	for (i = 0; i < trans->cfg->base_params->num_of_queues; i++)
		del_timer(&trans_pcie->txq[i].stuck_timer);

	clear_bit(STATUS_SYNC_HCMD_ACTIVE, &trans->status);
	wake_up(&trans_pcie->wait_command_queue);
}

static u32 iwl_pcie_int_cause_non_ict(struct iwl_trans *trans)
{
	u32 inta;

	lockdep_assert_held(&IWL_TRANS_GET_PCIE_TRANS(trans)->irq_lock);

	trace_iwlwifi_dev_irq(trans->dev);

	/* Discover which interrupts are active/pending */
	inta = iwl_read32(trans, CSR_INT);

	/* the thread will service interrupts and re-enable them */
	return inta;
}

/* a device (PCI-E) page is 4096 bytes long */
#define ICT_SHIFT	12
#define ICT_SIZE	(1 << ICT_SHIFT)
#define ICT_COUNT	(ICT_SIZE / sizeof(u32))

/* interrupt handler using ict table, with this interrupt driver will
 * stop using INTA register to get device's interrupt, reading this register
 * is expensive, device will write interrupts in ICT dram table, increment
 * index then will fire interrupt to driver, driver will OR all ICT table
 * entries from current index up to table entry with 0 value. the result is
 * the interrupt we need to service, driver will set the entries back to 0 and
 * set index.
 */
static u32 iwl_pcie_int_cause_ict(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 inta;
	u32 val = 0;
	u32 read;

	trace_iwlwifi_dev_irq(trans->dev);

	/* Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC. */
	read = le32_to_cpu(trans_pcie->ict_tbl[trans_pcie->ict_index]);
	trace_iwlwifi_dev_ict_read(trans->dev, trans_pcie->ict_index, read);
	if (!read)
		return 0;

	/*
	 * Collect all entries up to the first 0, starting from ict_index;
	 * note we already read at ict_index.
	 */
	do {
		val |= read;
		IWL_DEBUG_ISR(trans, "ICT index %d value 0x%08X\n",
				trans_pcie->ict_index, read);
		trans_pcie->ict_tbl[trans_pcie->ict_index] = 0;
		trans_pcie->ict_index =
			((trans_pcie->ict_index + 1) & (ICT_COUNT - 1));

		read = le32_to_cpu(trans_pcie->ict_tbl[trans_pcie->ict_index]);
		trace_iwlwifi_dev_ict_read(trans->dev, trans_pcie->ict_index,
					   read);
	} while (read);

	/* We should not get this value, just ignore it. */
	if (val == 0xffffffff)
		val = 0;

	/*
	 * this is a w/a for a h/w bug. the h/w bug may cause the Rx bit
	 * (bit 15 before shifting it to 31) to clear when using interrupt
	 * coalescing. fortunately, bits 18 and 19 stay set when this happens
	 * so we use them to decide on the real state of the Rx bit.
	 * In order words, bit 15 is set if bit 18 or bit 19 are set.
	 */
	if (val & 0xC0000)
		val |= 0x8000;

	inta = (0xff & val) | ((0xff00 & val) << 16);
	return inta;
}

irqreturn_t iwl_pcie_irq_handler(int irq, void *dev_id)
{
	struct iwl_trans *trans = dev_id;
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	struct isr_statistics *isr_stats = &trans_pcie->isr_stats;
	u32 inta = 0;
	u32 handled = 0;

	lock_map_acquire(&trans->sync_cmd_lockdep_map);

	spin_lock(&trans_pcie->irq_lock);

	/* dram interrupt table not set yet,
	 * use legacy interrupt.
	 */
	if (likely(trans_pcie->use_ict))
		inta = iwl_pcie_int_cause_ict(trans);
	else
		inta = iwl_pcie_int_cause_non_ict(trans);

	if (iwl_have_debug_level(IWL_DL_ISR)) {
		IWL_DEBUG_ISR(trans,
			      "ISR inta 0x%08x, enabled 0x%08x(sw), enabled(hw) 0x%08x, fh 0x%08x\n",
			      inta, trans_pcie->inta_mask,
			      iwl_read32(trans, CSR_INT_MASK),
			      iwl_read32(trans, CSR_FH_INT_STATUS));
		if (inta & (~trans_pcie->inta_mask))
			IWL_DEBUG_ISR(trans,
				      "We got a masked interrupt (0x%08x)\n",
				      inta & (~trans_pcie->inta_mask));
	}

	inta &= trans_pcie->inta_mask;

	/*
	 * Ignore interrupt if there's nothing in NIC to service.
	 * This may be due to IRQ shared with another device,
	 * or due to sporadic interrupts thrown from our NIC.
	 */
	if (unlikely(!inta)) {
		IWL_DEBUG_ISR(trans, "Ignore interrupt, inta == 0\n");
		/*
		 * Re-enable interrupts here since we don't
		 * have anything to service
		 */
		if (test_bit(STATUS_INT_ENABLED, &trans->status))
			iwl_enable_interrupts(trans);
		spin_unlock(&trans_pcie->irq_lock);
		lock_map_release(&trans->sync_cmd_lockdep_map);
		return IRQ_NONE;
	}

	if (unlikely(inta == 0xFFFFFFFF || (inta & 0xFFFFFFF0) == 0xa5a5a5a0)) {
		/*
		 * Hardware disappeared. It might have
		 * already raised an interrupt.
		 */
		IWL_WARN(trans, "HARDWARE GONE?? INTA == 0x%08x\n", inta);
		spin_unlock(&trans_pcie->irq_lock);
		goto out;
	}

	/* Ack/clear/reset pending uCode interrupts.
	 * Note:  Some bits in CSR_INT are "OR" of bits in CSR_FH_INT_STATUS,
	 */
	/* There is a hardware bug in the interrupt mask function that some
	 * interrupts (i.e. CSR_INT_BIT_SCD) can still be generated even if
	 * they are disabled in the CSR_INT_MASK register. Furthermore the
	 * ICT interrupt handling mechanism has another bug that might cause
	 * these unmasked interrupts fail to be detected. We workaround the
	 * hardware bugs here by ACKing all the possible interrupts so that
	 * interrupt coalescing can still be achieved.
	 */
	iwl_write32(trans, CSR_INT, inta | ~trans_pcie->inta_mask);

	if (iwl_have_debug_level(IWL_DL_ISR))
		IWL_DEBUG_ISR(trans, "inta 0x%08x, enabled 0x%08x\n",
			      inta, iwl_read32(trans, CSR_INT_MASK));

	spin_unlock(&trans_pcie->irq_lock);

	/* Now service all interrupt bits discovered above. */
	if (inta & CSR_INT_BIT_HW_ERR) {
		IWL_ERR(trans, "Hardware error detected.  Restarting.\n");

		/* Tell the device to stop sending interrupts */
		iwl_disable_interrupts(trans);

		isr_stats->hw++;
		iwl_pcie_irq_handle_error(trans);

		handled |= CSR_INT_BIT_HW_ERR;

		goto out;
	}

	if (iwl_have_debug_level(IWL_DL_ISR)) {
		/* NIC fires this, but we don't use it, redundant with WAKEUP */
		if (inta & CSR_INT_BIT_SCD) {
			IWL_DEBUG_ISR(trans,
				      "Scheduler finished to transmit the frame/frames.\n");
			isr_stats->sch++;
		}

		/* Alive notification via Rx interrupt will do the real work */
		if (inta & CSR_INT_BIT_ALIVE) {
			IWL_DEBUG_ISR(trans, "Alive interrupt\n");
			isr_stats->alive++;
		}
	}

	/* Safely ignore these bits for debug checks below */
	inta &= ~(CSR_INT_BIT_SCD | CSR_INT_BIT_ALIVE);

	/* HW RF KILL switch toggled */
	if (inta & CSR_INT_BIT_RF_KILL) {
		bool hw_rfkill;

		hw_rfkill = iwl_is_rfkill_set(trans);
		IWL_WARN(trans, "RF_KILL bit toggled to %s.\n",
			 hw_rfkill ? "disable radio" : "enable radio");

		isr_stats->rfkill++;

		mutex_lock(&trans_pcie->mutex);
		iwl_trans_pcie_rf_kill(trans, hw_rfkill);
		mutex_unlock(&trans_pcie->mutex);
		if (hw_rfkill) {
			set_bit(STATUS_RFKILL, &trans->status);
			if (test_and_clear_bit(STATUS_SYNC_HCMD_ACTIVE,
					       &trans->status))
				IWL_DEBUG_RF_KILL(trans,
						  "Rfkill while SYNC HCMD in flight\n");
			wake_up(&trans_pcie->wait_command_queue);
		} else {
			clear_bit(STATUS_RFKILL, &trans->status);
		}

		handled |= CSR_INT_BIT_RF_KILL;
	}

	/* Chip got too hot and stopped itself */
	if (inta & CSR_INT_BIT_CT_KILL) {
		IWL_ERR(trans, "Microcode CT kill error detected.\n");
		isr_stats->ctkill++;
		handled |= CSR_INT_BIT_CT_KILL;
	}

	/* Error detected by uCode */
	if (inta & CSR_INT_BIT_SW_ERR) {
		IWL_ERR(trans, "Microcode SW error detected. "
			" Restarting 0x%X.\n", inta);
		isr_stats->sw++;
		iwl_pcie_irq_handle_error(trans);
		handled |= CSR_INT_BIT_SW_ERR;
	}

	/* uCode wakes up after power-down sleep */
	if (inta & CSR_INT_BIT_WAKEUP) {
		IWL_DEBUG_ISR(trans, "Wakeup interrupt\n");
		iwl_pcie_rxq_check_wrptr(trans);
		iwl_pcie_txq_check_wrptrs(trans);

		isr_stats->wakeup++;

		handled |= CSR_INT_BIT_WAKEUP;
	}

	/* All uCode command responses, including Tx command responses,
	 * Rx "responses" (frame-received notification), and other
	 * notifications from uCode come through here*/
	if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX |
		    CSR_INT_BIT_RX_PERIODIC)) {
		IWL_DEBUG_ISR(trans, "Rx interrupt\n");
		if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX)) {
			handled |= (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX);
			iwl_write32(trans, CSR_FH_INT_STATUS,
					CSR_FH_INT_RX_MASK);
		}
		if (inta & CSR_INT_BIT_RX_PERIODIC) {
			handled |= CSR_INT_BIT_RX_PERIODIC;
			iwl_write32(trans,
				CSR_INT, CSR_INT_BIT_RX_PERIODIC);
		}
		/* Sending RX interrupt require many steps to be done in the
		 * the device:
		 * 1- write interrupt to current index in ICT table.
		 * 2- dma RX frame.
		 * 3- update RX shared data to indicate last write index.
		 * 4- send interrupt.
		 * This could lead to RX race, driver could receive RX interrupt
		 * but the shared data changes does not reflect this;
		 * periodic interrupt will detect any dangling Rx activity.
		 */

		/* Disable periodic interrupt; we use it as just a one-shot. */
		iwl_write8(trans, CSR_INT_PERIODIC_REG,
			    CSR_INT_PERIODIC_DIS);

		/*
		 * Enable periodic interrupt in 8 msec only if we received
		 * real RX interrupt (instead of just periodic int), to catch
		 * any dangling Rx interrupt.  If it was just the periodic
		 * interrupt, there was no dangling Rx activity, and no need
		 * to extend the periodic interrupt; one-shot is enough.
		 */
		if (inta & (CSR_INT_BIT_FH_RX | CSR_INT_BIT_SW_RX))
			iwl_write8(trans, CSR_INT_PERIODIC_REG,
				   CSR_INT_PERIODIC_ENA);

		isr_stats->rx++;

		local_bh_disable();
		iwl_pcie_rx_handle(trans);
		local_bh_enable();
	}

	/* This "Tx" DMA channel is used only for loading uCode */
	if (inta & CSR_INT_BIT_FH_TX) {
		iwl_write32(trans, CSR_FH_INT_STATUS, CSR_FH_INT_TX_MASK);
		IWL_DEBUG_ISR(trans, "uCode load interrupt\n");
		isr_stats->tx++;
		handled |= CSR_INT_BIT_FH_TX;
		/* Wake up uCode load routine, now that load is complete */
		trans_pcie->ucode_write_complete = true;
		wake_up(&trans_pcie->ucode_write_waitq);
	}

	if (inta & ~handled) {
		IWL_ERR(trans, "Unhandled INTA bits 0x%08x\n", inta & ~handled);
		isr_stats->unhandled++;
	}

	if (inta & ~(trans_pcie->inta_mask)) {
		IWL_WARN(trans, "Disabled INTA bits 0x%08x were pending\n",
			 inta & ~trans_pcie->inta_mask);
	}

	/* Re-enable all interrupts */
	/* only Re-enable if disabled by irq */
	if (test_bit(STATUS_INT_ENABLED, &trans->status))
		iwl_enable_interrupts(trans);
	/* Re-enable RF_KILL if it occurred */
	else if (handled & CSR_INT_BIT_RF_KILL)
		iwl_enable_rfkill_int(trans);

out:
	lock_map_release(&trans->sync_cmd_lockdep_map);
	return IRQ_HANDLED;
}

/******************************************************************************
 *
 * ICT functions
 *
 ******************************************************************************/

/* Free dram table */
void iwl_pcie_free_ict(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (trans_pcie->ict_tbl) {
		dma_free_coherent(trans->dev, ICT_SIZE,
				  trans_pcie->ict_tbl,
				  trans_pcie->ict_tbl_dma);
		trans_pcie->ict_tbl = NULL;
		trans_pcie->ict_tbl_dma = 0;
	}
}

/*
 * allocate dram shared table, it is an aligned memory
 * block of ICT_SIZE.
 * also reset all data related to ICT table interrupt.
 */
int iwl_pcie_alloc_ict(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	trans_pcie->ict_tbl =
		dma_zalloc_coherent(trans->dev, ICT_SIZE,
				   &trans_pcie->ict_tbl_dma,
				   GFP_KERNEL);
	if (!trans_pcie->ict_tbl)
		return -ENOMEM;

	/* just an API sanity check ... it is guaranteed to be aligned */
	if (WARN_ON(trans_pcie->ict_tbl_dma & (ICT_SIZE - 1))) {
		iwl_pcie_free_ict(trans);
		return -EINVAL;
	}

	IWL_DEBUG_ISR(trans, "ict dma addr %Lx ict vir addr %p\n",
		      (unsigned long long)trans_pcie->ict_tbl_dma,
		      trans_pcie->ict_tbl);

	return 0;
}

/* Device is going up inform it about using ICT interrupt table,
 * also we need to tell the driver to start using ICT interrupt.
 */
void iwl_pcie_reset_ict(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 val;

	if (!trans_pcie->ict_tbl)
		return;

	spin_lock(&trans_pcie->irq_lock);
	iwl_disable_interrupts(trans);

	memset(trans_pcie->ict_tbl, 0, ICT_SIZE);

	val = trans_pcie->ict_tbl_dma >> ICT_SHIFT;

	val |= CSR_DRAM_INT_TBL_ENABLE |
	       CSR_DRAM_INIT_TBL_WRAP_CHECK |
	       CSR_DRAM_INIT_TBL_WRITE_POINTER;

	IWL_DEBUG_ISR(trans, "CSR_DRAM_INT_TBL_REG =0x%x\n", val);

	iwl_write32(trans, CSR_DRAM_INT_TBL_REG, val);
	trans_pcie->use_ict = true;
	trans_pcie->ict_index = 0;
	iwl_write32(trans, CSR_INT, trans_pcie->inta_mask);
	iwl_enable_interrupts(trans);
	spin_unlock(&trans_pcie->irq_lock);
}

/* Device is going down disable ict interrupt usage */
void iwl_pcie_disable_ict(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	spin_lock(&trans_pcie->irq_lock);
	trans_pcie->use_ict = false;
	spin_unlock(&trans_pcie->irq_lock);
}

irqreturn_t iwl_pcie_isr(int irq, void *data)
{
	struct iwl_trans *trans = data;

	if (!trans)
		return IRQ_NONE;

	/* Disable (but don't clear!) interrupts here to avoid
	 * back-to-back ISRs and sporadic interrupts from our NIC.
	 * If we have something to service, the tasklet will re-enable ints.
	 * If we *don't* have something, we'll re-enable before leaving here.
	 */
	iwl_write32(trans, CSR_INT_MASK, 0x00000000);

	return IRQ_WAKE_THREAD;
}
