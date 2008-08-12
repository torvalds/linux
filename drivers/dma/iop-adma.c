/*
 * offload engine driver for the Intel Xscale series of i/o processors
 * Copyright © 2006, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*
 * This driver supports the asynchrounous DMA copy and RAID engines available
 * on the Intel Xscale(R) family of I/O Processors (IOP 32x, 33x, 134x)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/async_tx.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/memory.h>
#include <linux/ioport.h>

#include <mach/adma.h>

#define to_iop_adma_chan(chan) container_of(chan, struct iop_adma_chan, common)
#define to_iop_adma_device(dev) \
	container_of(dev, struct iop_adma_device, common)
#define tx_to_iop_adma_slot(tx) \
	container_of(tx, struct iop_adma_desc_slot, async_tx)

/**
 * iop_adma_free_slots - flags descriptor slots for reuse
 * @slot: Slot to free
 * Caller must hold &iop_chan->lock while calling this function
 */
static void iop_adma_free_slots(struct iop_adma_desc_slot *slot)
{
	int stride = slot->slots_per_op;

	while (stride--) {
		slot->slots_per_op = 0;
		slot = list_entry(slot->slot_node.next,
				struct iop_adma_desc_slot,
				slot_node);
	}
}

static dma_cookie_t
iop_adma_run_tx_complete_actions(struct iop_adma_desc_slot *desc,
	struct iop_adma_chan *iop_chan, dma_cookie_t cookie)
{
	BUG_ON(desc->async_tx.cookie < 0);
	if (desc->async_tx.cookie > 0) {
		cookie = desc->async_tx.cookie;
		desc->async_tx.cookie = 0;

		/* call the callback (must not sleep or submit new
		 * operations to this channel)
		 */
		if (desc->async_tx.callback)
			desc->async_tx.callback(
				desc->async_tx.callback_param);

		/* unmap dma addresses
		 * (unmap_single vs unmap_page?)
		 */
		if (desc->group_head && desc->unmap_len) {
			struct iop_adma_desc_slot *unmap = desc->group_head;
			struct device *dev =
				&iop_chan->device->pdev->dev;
			u32 len = unmap->unmap_len;
			enum dma_ctrl_flags flags = desc->async_tx.flags;
			u32 src_cnt;
			dma_addr_t addr;

			if (!(flags & DMA_COMPL_SKIP_DEST_UNMAP)) {
				addr = iop_desc_get_dest_addr(unmap, iop_chan);
				dma_unmap_page(dev, addr, len, DMA_FROM_DEVICE);
			}

			if (!(flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
				src_cnt = unmap->unmap_src_cnt;
				while (src_cnt--) {
					addr = iop_desc_get_src_addr(unmap,
								     iop_chan,
								     src_cnt);
					dma_unmap_page(dev, addr, len,
						       DMA_TO_DEVICE);
				}
			}
			desc->group_head = NULL;
		}
	}

	/* run dependent operations */
	async_tx_run_dependencies(&desc->async_tx);

	return cookie;
}

static int
iop_adma_clean_slot(struct iop_adma_desc_slot *desc,
	struct iop_adma_chan *iop_chan)
{
	/* the client is allowed to attach dependent operations
	 * until 'ack' is set
	 */
	if (!async_tx_test_ack(&desc->async_tx))
		return 0;

	/* leave the last descriptor in the chain
	 * so we can append to it
	 */
	if (desc->chain_node.next == &iop_chan->chain)
		return 1;

	dev_dbg(iop_chan->device->common.dev,
		"\tfree slot: %d slots_per_op: %d\n",
		desc->idx, desc->slots_per_op);

	list_del(&desc->chain_node);
	iop_adma_free_slots(desc);

	return 0;
}

static void __iop_adma_slot_cleanup(struct iop_adma_chan *iop_chan)
{
	struct iop_adma_desc_slot *iter, *_iter, *grp_start = NULL;
	dma_cookie_t cookie = 0;
	u32 current_desc = iop_chan_get_current_descriptor(iop_chan);
	int busy = iop_chan_is_busy(iop_chan);
	int seen_current = 0, slot_cnt = 0, slots_per_op = 0;

	dev_dbg(iop_chan->device->common.dev, "%s\n", __func__);
	/* free completed slots from the chain starting with
	 * the oldest descriptor
	 */
	list_for_each_entry_safe(iter, _iter, &iop_chan->chain,
					chain_node) {
		pr_debug("\tcookie: %d slot: %d busy: %d "
			"this_desc: %#x next_desc: %#x ack: %d\n",
			iter->async_tx.cookie, iter->idx, busy,
			iter->async_tx.phys, iop_desc_get_next_desc(iter),
			async_tx_test_ack(&iter->async_tx));
		prefetch(_iter);
		prefetch(&_iter->async_tx);

		/* do not advance past the current descriptor loaded into the
		 * hardware channel, subsequent descriptors are either in
		 * process or have not been submitted
		 */
		if (seen_current)
			break;

		/* stop the search if we reach the current descriptor and the
		 * channel is busy, or if it appears that the current descriptor
		 * needs to be re-read (i.e. has been appended to)
		 */
		if (iter->async_tx.phys == current_desc) {
			BUG_ON(seen_current++);
			if (busy || iop_desc_get_next_desc(iter))
				break;
		}

		/* detect the start of a group transaction */
		if (!slot_cnt && !slots_per_op) {
			slot_cnt = iter->slot_cnt;
			slots_per_op = iter->slots_per_op;
			if (slot_cnt <= slots_per_op) {
				slot_cnt = 0;
				slots_per_op = 0;
			}
		}

		if (slot_cnt) {
			pr_debug("\tgroup++\n");
			if (!grp_start)
				grp_start = iter;
			slot_cnt -= slots_per_op;
		}

		/* all the members of a group are complete */
		if (slots_per_op != 0 && slot_cnt == 0) {
			struct iop_adma_desc_slot *grp_iter, *_grp_iter;
			int end_of_chain = 0;
			pr_debug("\tgroup end\n");

			/* collect the total results */
			if (grp_start->xor_check_result) {
				u32 zero_sum_result = 0;
				slot_cnt = grp_start->slot_cnt;
				grp_iter = grp_start;

				list_for_each_entry_from(grp_iter,
					&iop_chan->chain, chain_node) {
					zero_sum_result |=
					    iop_desc_get_zero_result(grp_iter);
					    pr_debug("\titer%d result: %d\n",
					    grp_iter->idx, zero_sum_result);
					slot_cnt -= slots_per_op;
					if (slot_cnt == 0)
						break;
				}
				pr_debug("\tgrp_start->xor_check_result: %p\n",
					grp_start->xor_check_result);
				*grp_start->xor_check_result = zero_sum_result;
			}

			/* clean up the group */
			slot_cnt = grp_start->slot_cnt;
			grp_iter = grp_start;
			list_for_each_entry_safe_from(grp_iter, _grp_iter,
				&iop_chan->chain, chain_node) {
				cookie = iop_adma_run_tx_complete_actions(
					grp_iter, iop_chan, cookie);

				slot_cnt -= slots_per_op;
				end_of_chain = iop_adma_clean_slot(grp_iter,
					iop_chan);

				if (slot_cnt == 0 || end_of_chain)
					break;
			}

			/* the group should be complete at this point */
			BUG_ON(slot_cnt);

			slots_per_op = 0;
			grp_start = NULL;
			if (end_of_chain)
				break;
			else
				continue;
		} else if (slots_per_op) /* wait for group completion */
			continue;

		/* write back zero sum results (single descriptor case) */
		if (iter->xor_check_result && iter->async_tx.cookie)
			*iter->xor_check_result =
				iop_desc_get_zero_result(iter);

		cookie = iop_adma_run_tx_complete_actions(
					iter, iop_chan, cookie);

		if (iop_adma_clean_slot(iter, iop_chan))
			break;
	}

	BUG_ON(!seen_current);

	if (cookie > 0) {
		iop_chan->completed_cookie = cookie;
		pr_debug("\tcompleted cookie %d\n", cookie);
	}
}

static void
iop_adma_slot_cleanup(struct iop_adma_chan *iop_chan)
{
	spin_lock_bh(&iop_chan->lock);
	__iop_adma_slot_cleanup(iop_chan);
	spin_unlock_bh(&iop_chan->lock);
}

static void iop_adma_tasklet(unsigned long data)
{
	struct iop_adma_chan *iop_chan = (struct iop_adma_chan *) data;

	spin_lock(&iop_chan->lock);
	__iop_adma_slot_cleanup(iop_chan);
	spin_unlock(&iop_chan->lock);
}

static struct iop_adma_desc_slot *
iop_adma_alloc_slots(struct iop_adma_chan *iop_chan, int num_slots,
			int slots_per_op)
{
	struct iop_adma_desc_slot *iter, *_iter, *alloc_start = NULL;
	LIST_HEAD(chain);
	int slots_found, retry = 0;

	/* start search from the last allocated descrtiptor
	 * if a contiguous allocation can not be found start searching
	 * from the beginning of the list
	 */
retry:
	slots_found = 0;
	if (retry == 0)
		iter = iop_chan->last_used;
	else
		iter = list_entry(&iop_chan->all_slots,
			struct iop_adma_desc_slot,
			slot_node);

	list_for_each_entry_safe_continue(
		iter, _iter, &iop_chan->all_slots, slot_node) {
		prefetch(_iter);
		prefetch(&_iter->async_tx);
		if (iter->slots_per_op) {
			/* give up after finding the first busy slot
			 * on the second pass through the list
			 */
			if (retry)
				break;

			slots_found = 0;
			continue;
		}

		/* start the allocation if the slot is correctly aligned */
		if (!slots_found++) {
			if (iop_desc_is_aligned(iter, slots_per_op))
				alloc_start = iter;
			else {
				slots_found = 0;
				continue;
			}
		}

		if (slots_found == num_slots) {
			struct iop_adma_desc_slot *alloc_tail = NULL;
			struct iop_adma_desc_slot *last_used = NULL;
			iter = alloc_start;
			while (num_slots) {
				int i;
				dev_dbg(iop_chan->device->common.dev,
					"allocated slot: %d "
					"(desc %p phys: %#x) slots_per_op %d\n",
					iter->idx, iter->hw_desc,
					iter->async_tx.phys, slots_per_op);

				/* pre-ack all but the last descriptor */
				if (num_slots != slots_per_op)
					async_tx_ack(&iter->async_tx);

				list_add_tail(&iter->chain_node, &chain);
				alloc_tail = iter;
				iter->async_tx.cookie = 0;
				iter->slot_cnt = num_slots;
				iter->xor_check_result = NULL;
				for (i = 0; i < slots_per_op; i++) {
					iter->slots_per_op = slots_per_op - i;
					last_used = iter;
					iter = list_entry(iter->slot_node.next,
						struct iop_adma_desc_slot,
						slot_node);
				}
				num_slots -= slots_per_op;
			}
			alloc_tail->group_head = alloc_start;
			alloc_tail->async_tx.cookie = -EBUSY;
			list_splice(&chain, &alloc_tail->async_tx.tx_list);
			iop_chan->last_used = last_used;
			iop_desc_clear_next_desc(alloc_start);
			iop_desc_clear_next_desc(alloc_tail);
			return alloc_tail;
		}
	}
	if (!retry++)
		goto retry;

	/* perform direct reclaim if the allocation fails */
	__iop_adma_slot_cleanup(iop_chan);

	return NULL;
}

static dma_cookie_t
iop_desc_assign_cookie(struct iop_adma_chan *iop_chan,
	struct iop_adma_desc_slot *desc)
{
	dma_cookie_t cookie = iop_chan->common.cookie;
	cookie++;
	if (cookie < 0)
		cookie = 1;
	iop_chan->common.cookie = desc->async_tx.cookie = cookie;
	return cookie;
}

static void iop_adma_check_threshold(struct iop_adma_chan *iop_chan)
{
	dev_dbg(iop_chan->device->common.dev, "pending: %d\n",
		iop_chan->pending);

	if (iop_chan->pending >= IOP_ADMA_THRESHOLD) {
		iop_chan->pending = 0;
		iop_chan_append(iop_chan);
	}
}

static dma_cookie_t
iop_adma_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct iop_adma_desc_slot *sw_desc = tx_to_iop_adma_slot(tx);
	struct iop_adma_chan *iop_chan = to_iop_adma_chan(tx->chan);
	struct iop_adma_desc_slot *grp_start, *old_chain_tail;
	int slot_cnt;
	int slots_per_op;
	dma_cookie_t cookie;

	grp_start = sw_desc->group_head;
	slot_cnt = grp_start->slot_cnt;
	slots_per_op = grp_start->slots_per_op;

	spin_lock_bh(&iop_chan->lock);
	cookie = iop_desc_assign_cookie(iop_chan, sw_desc);

	old_chain_tail = list_entry(iop_chan->chain.prev,
		struct iop_adma_desc_slot, chain_node);
	list_splice_init(&sw_desc->async_tx.tx_list,
			 &old_chain_tail->chain_node);

	/* fix up the hardware chain */
	iop_desc_set_next_desc(old_chain_tail, grp_start->async_tx.phys);

	/* 1/ don't add pre-chained descriptors
	 * 2/ dummy read to flush next_desc write
	 */
	BUG_ON(iop_desc_get_next_desc(sw_desc));

	/* increment the pending count by the number of slots
	 * memcpy operations have a 1:1 (slot:operation) relation
	 * other operations are heavier and will pop the threshold
	 * more often.
	 */
	iop_chan->pending += slot_cnt;
	iop_adma_check_threshold(iop_chan);
	spin_unlock_bh(&iop_chan->lock);

	dev_dbg(iop_chan->device->common.dev, "%s cookie: %d slot: %d\n",
		__func__, sw_desc->async_tx.cookie, sw_desc->idx);

	return cookie;
}

static void iop_chan_start_null_memcpy(struct iop_adma_chan *iop_chan);
static void iop_chan_start_null_xor(struct iop_adma_chan *iop_chan);

/**
 * iop_adma_alloc_chan_resources -  returns the number of allocated descriptors
 * @chan - allocate descriptor resources for this channel
 * @client - current client requesting the channel be ready for requests
 *
 * Note: We keep the slots for 1 operation on iop_chan->chain at all times.  To
 * avoid deadlock, via async_xor, num_descs_in_pool must at a minimum be
 * greater than 2x the number slots needed to satisfy a device->max_xor
 * request.
 * */
static int iop_adma_alloc_chan_resources(struct dma_chan *chan,
					 struct dma_client *client)
{
	char *hw_desc;
	int idx;
	struct iop_adma_chan *iop_chan = to_iop_adma_chan(chan);
	struct iop_adma_desc_slot *slot = NULL;
	int init = iop_chan->slots_allocated ? 0 : 1;
	struct iop_adma_platform_data *plat_data =
		iop_chan->device->pdev->dev.platform_data;
	int num_descs_in_pool = plat_data->pool_size/IOP_ADMA_SLOT_SIZE;

	/* Allocate descriptor slots */
	do {
		idx = iop_chan->slots_allocated;
		if (idx == num_descs_in_pool)
			break;

		slot = kzalloc(sizeof(*slot), GFP_KERNEL);
		if (!slot) {
			printk(KERN_INFO "IOP ADMA Channel only initialized"
				" %d descriptor slots", idx);
			break;
		}
		hw_desc = (char *) iop_chan->device->dma_desc_pool_virt;
		slot->hw_desc = (void *) &hw_desc[idx * IOP_ADMA_SLOT_SIZE];

		dma_async_tx_descriptor_init(&slot->async_tx, chan);
		slot->async_tx.tx_submit = iop_adma_tx_submit;
		INIT_LIST_HEAD(&slot->chain_node);
		INIT_LIST_HEAD(&slot->slot_node);
		INIT_LIST_HEAD(&slot->async_tx.tx_list);
		hw_desc = (char *) iop_chan->device->dma_desc_pool;
		slot->async_tx.phys =
			(dma_addr_t) &hw_desc[idx * IOP_ADMA_SLOT_SIZE];
		slot->idx = idx;

		spin_lock_bh(&iop_chan->lock);
		iop_chan->slots_allocated++;
		list_add_tail(&slot->slot_node, &iop_chan->all_slots);
		spin_unlock_bh(&iop_chan->lock);
	} while (iop_chan->slots_allocated < num_descs_in_pool);

	if (idx && !iop_chan->last_used)
		iop_chan->last_used = list_entry(iop_chan->all_slots.next,
					struct iop_adma_desc_slot,
					slot_node);

	dev_dbg(iop_chan->device->common.dev,
		"allocated %d descriptor slots last_used: %p\n",
		iop_chan->slots_allocated, iop_chan->last_used);

	/* initialize the channel and the chain with a null operation */
	if (init) {
		if (dma_has_cap(DMA_MEMCPY,
			iop_chan->device->common.cap_mask))
			iop_chan_start_null_memcpy(iop_chan);
		else if (dma_has_cap(DMA_XOR,
			iop_chan->device->common.cap_mask))
			iop_chan_start_null_xor(iop_chan);
		else
			BUG();
	}

	return (idx > 0) ? idx : -ENOMEM;
}

static struct dma_async_tx_descriptor *
iop_adma_prep_dma_interrupt(struct dma_chan *chan, unsigned long flags)
{
	struct iop_adma_chan *iop_chan = to_iop_adma_chan(chan);
	struct iop_adma_desc_slot *sw_desc, *grp_start;
	int slot_cnt, slots_per_op;

	dev_dbg(iop_chan->device->common.dev, "%s\n", __func__);

	spin_lock_bh(&iop_chan->lock);
	slot_cnt = iop_chan_interrupt_slot_count(&slots_per_op, iop_chan);
	sw_desc = iop_adma_alloc_slots(iop_chan, slot_cnt, slots_per_op);
	if (sw_desc) {
		grp_start = sw_desc->group_head;
		iop_desc_init_interrupt(grp_start, iop_chan);
		grp_start->unmap_len = 0;
		sw_desc->async_tx.flags = flags;
	}
	spin_unlock_bh(&iop_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

static struct dma_async_tx_descriptor *
iop_adma_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dma_dest,
			 dma_addr_t dma_src, size_t len, unsigned long flags)
{
	struct iop_adma_chan *iop_chan = to_iop_adma_chan(chan);
	struct iop_adma_desc_slot *sw_desc, *grp_start;
	int slot_cnt, slots_per_op;

	if (unlikely(!len))
		return NULL;
	BUG_ON(unlikely(len > IOP_ADMA_MAX_BYTE_COUNT));

	dev_dbg(iop_chan->device->common.dev, "%s len: %u\n",
		__func__, len);

	spin_lock_bh(&iop_chan->lock);
	slot_cnt = iop_chan_memcpy_slot_count(len, &slots_per_op);
	sw_desc = iop_adma_alloc_slots(iop_chan, slot_cnt, slots_per_op);
	if (sw_desc) {
		grp_start = sw_desc->group_head;
		iop_desc_init_memcpy(grp_start, flags);
		iop_desc_set_byte_count(grp_start, iop_chan, len);
		iop_desc_set_dest_addr(grp_start, iop_chan, dma_dest);
		iop_desc_set_memcpy_src_addr(grp_start, dma_src);
		sw_desc->unmap_src_cnt = 1;
		sw_desc->unmap_len = len;
		sw_desc->async_tx.flags = flags;
	}
	spin_unlock_bh(&iop_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

static struct dma_async_tx_descriptor *
iop_adma_prep_dma_memset(struct dma_chan *chan, dma_addr_t dma_dest,
			 int value, size_t len, unsigned long flags)
{
	struct iop_adma_chan *iop_chan = to_iop_adma_chan(chan);
	struct iop_adma_desc_slot *sw_desc, *grp_start;
	int slot_cnt, slots_per_op;

	if (unlikely(!len))
		return NULL;
	BUG_ON(unlikely(len > IOP_ADMA_MAX_BYTE_COUNT));

	dev_dbg(iop_chan->device->common.dev, "%s len: %u\n",
		__func__, len);

	spin_lock_bh(&iop_chan->lock);
	slot_cnt = iop_chan_memset_slot_count(len, &slots_per_op);
	sw_desc = iop_adma_alloc_slots(iop_chan, slot_cnt, slots_per_op);
	if (sw_desc) {
		grp_start = sw_desc->group_head;
		iop_desc_init_memset(grp_start, flags);
		iop_desc_set_byte_count(grp_start, iop_chan, len);
		iop_desc_set_block_fill_val(grp_start, value);
		iop_desc_set_dest_addr(grp_start, iop_chan, dma_dest);
		sw_desc->unmap_src_cnt = 1;
		sw_desc->unmap_len = len;
		sw_desc->async_tx.flags = flags;
	}
	spin_unlock_bh(&iop_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

static struct dma_async_tx_descriptor *
iop_adma_prep_dma_xor(struct dma_chan *chan, dma_addr_t dma_dest,
		      dma_addr_t *dma_src, unsigned int src_cnt, size_t len,
		      unsigned long flags)
{
	struct iop_adma_chan *iop_chan = to_iop_adma_chan(chan);
	struct iop_adma_desc_slot *sw_desc, *grp_start;
	int slot_cnt, slots_per_op;

	if (unlikely(!len))
		return NULL;
	BUG_ON(unlikely(len > IOP_ADMA_XOR_MAX_BYTE_COUNT));

	dev_dbg(iop_chan->device->common.dev,
		"%s src_cnt: %d len: %u flags: %lx\n",
		__func__, src_cnt, len, flags);

	spin_lock_bh(&iop_chan->lock);
	slot_cnt = iop_chan_xor_slot_count(len, src_cnt, &slots_per_op);
	sw_desc = iop_adma_alloc_slots(iop_chan, slot_cnt, slots_per_op);
	if (sw_desc) {
		grp_start = sw_desc->group_head;
		iop_desc_init_xor(grp_start, src_cnt, flags);
		iop_desc_set_byte_count(grp_start, iop_chan, len);
		iop_desc_set_dest_addr(grp_start, iop_chan, dma_dest);
		sw_desc->unmap_src_cnt = src_cnt;
		sw_desc->unmap_len = len;
		sw_desc->async_tx.flags = flags;
		while (src_cnt--)
			iop_desc_set_xor_src_addr(grp_start, src_cnt,
						  dma_src[src_cnt]);
	}
	spin_unlock_bh(&iop_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

static struct dma_async_tx_descriptor *
iop_adma_prep_dma_zero_sum(struct dma_chan *chan, dma_addr_t *dma_src,
			   unsigned int src_cnt, size_t len, u32 *result,
			   unsigned long flags)
{
	struct iop_adma_chan *iop_chan = to_iop_adma_chan(chan);
	struct iop_adma_desc_slot *sw_desc, *grp_start;
	int slot_cnt, slots_per_op;

	if (unlikely(!len))
		return NULL;

	dev_dbg(iop_chan->device->common.dev, "%s src_cnt: %d len: %u\n",
		__func__, src_cnt, len);

	spin_lock_bh(&iop_chan->lock);
	slot_cnt = iop_chan_zero_sum_slot_count(len, src_cnt, &slots_per_op);
	sw_desc = iop_adma_alloc_slots(iop_chan, slot_cnt, slots_per_op);
	if (sw_desc) {
		grp_start = sw_desc->group_head;
		iop_desc_init_zero_sum(grp_start, src_cnt, flags);
		iop_desc_set_zero_sum_byte_count(grp_start, len);
		grp_start->xor_check_result = result;
		pr_debug("\t%s: grp_start->xor_check_result: %p\n",
			__func__, grp_start->xor_check_result);
		sw_desc->unmap_src_cnt = src_cnt;
		sw_desc->unmap_len = len;
		sw_desc->async_tx.flags = flags;
		while (src_cnt--)
			iop_desc_set_zero_sum_src_addr(grp_start, src_cnt,
						       dma_src[src_cnt]);
	}
	spin_unlock_bh(&iop_chan->lock);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

static void iop_adma_free_chan_resources(struct dma_chan *chan)
{
	struct iop_adma_chan *iop_chan = to_iop_adma_chan(chan);
	struct iop_adma_desc_slot *iter, *_iter;
	int in_use_descs = 0;

	iop_adma_slot_cleanup(iop_chan);

	spin_lock_bh(&iop_chan->lock);
	list_for_each_entry_safe(iter, _iter, &iop_chan->chain,
					chain_node) {
		in_use_descs++;
		list_del(&iter->chain_node);
	}
	list_for_each_entry_safe_reverse(
		iter, _iter, &iop_chan->all_slots, slot_node) {
		list_del(&iter->slot_node);
		kfree(iter);
		iop_chan->slots_allocated--;
	}
	iop_chan->last_used = NULL;

	dev_dbg(iop_chan->device->common.dev, "%s slots_allocated %d\n",
		__func__, iop_chan->slots_allocated);
	spin_unlock_bh(&iop_chan->lock);

	/* one is ok since we left it on there on purpose */
	if (in_use_descs > 1)
		printk(KERN_ERR "IOP: Freeing %d in use descriptors!\n",
			in_use_descs - 1);
}

/**
 * iop_adma_is_complete - poll the status of an ADMA transaction
 * @chan: ADMA channel handle
 * @cookie: ADMA transaction identifier
 */
static enum dma_status iop_adma_is_complete(struct dma_chan *chan,
					dma_cookie_t cookie,
					dma_cookie_t *done,
					dma_cookie_t *used)
{
	struct iop_adma_chan *iop_chan = to_iop_adma_chan(chan);
	dma_cookie_t last_used;
	dma_cookie_t last_complete;
	enum dma_status ret;

	last_used = chan->cookie;
	last_complete = iop_chan->completed_cookie;

	if (done)
		*done = last_complete;
	if (used)
		*used = last_used;

	ret = dma_async_is_complete(cookie, last_complete, last_used);
	if (ret == DMA_SUCCESS)
		return ret;

	iop_adma_slot_cleanup(iop_chan);

	last_used = chan->cookie;
	last_complete = iop_chan->completed_cookie;

	if (done)
		*done = last_complete;
	if (used)
		*used = last_used;

	return dma_async_is_complete(cookie, last_complete, last_used);
}

static irqreturn_t iop_adma_eot_handler(int irq, void *data)
{
	struct iop_adma_chan *chan = data;

	dev_dbg(chan->device->common.dev, "%s\n", __func__);

	tasklet_schedule(&chan->irq_tasklet);

	iop_adma_device_clear_eot_status(chan);

	return IRQ_HANDLED;
}

static irqreturn_t iop_adma_eoc_handler(int irq, void *data)
{
	struct iop_adma_chan *chan = data;

	dev_dbg(chan->device->common.dev, "%s\n", __func__);

	tasklet_schedule(&chan->irq_tasklet);

	iop_adma_device_clear_eoc_status(chan);

	return IRQ_HANDLED;
}

static irqreturn_t iop_adma_err_handler(int irq, void *data)
{
	struct iop_adma_chan *chan = data;
	unsigned long status = iop_chan_get_status(chan);

	dev_printk(KERN_ERR, chan->device->common.dev,
		"error ( %s%s%s%s%s%s%s)\n",
		iop_is_err_int_parity(status, chan) ? "int_parity " : "",
		iop_is_err_mcu_abort(status, chan) ? "mcu_abort " : "",
		iop_is_err_int_tabort(status, chan) ? "int_tabort " : "",
		iop_is_err_int_mabort(status, chan) ? "int_mabort " : "",
		iop_is_err_pci_tabort(status, chan) ? "pci_tabort " : "",
		iop_is_err_pci_mabort(status, chan) ? "pci_mabort " : "",
		iop_is_err_split_tx(status, chan) ? "split_tx " : "");

	iop_adma_device_clear_err_status(chan);

	BUG();

	return IRQ_HANDLED;
}

static void iop_adma_issue_pending(struct dma_chan *chan)
{
	struct iop_adma_chan *iop_chan = to_iop_adma_chan(chan);

	if (iop_chan->pending) {
		iop_chan->pending = 0;
		iop_chan_append(iop_chan);
	}
}

/*
 * Perform a transaction to verify the HW works.
 */
#define IOP_ADMA_TEST_SIZE 2000

static int __devinit iop_adma_memcpy_self_test(struct iop_adma_device *device)
{
	int i;
	void *src, *dest;
	dma_addr_t src_dma, dest_dma;
	struct dma_chan *dma_chan;
	dma_cookie_t cookie;
	struct dma_async_tx_descriptor *tx;
	int err = 0;
	struct iop_adma_chan *iop_chan;

	dev_dbg(device->common.dev, "%s\n", __func__);

	src = kmalloc(IOP_ADMA_TEST_SIZE, GFP_KERNEL);
	if (!src)
		return -ENOMEM;
	dest = kzalloc(IOP_ADMA_TEST_SIZE, GFP_KERNEL);
	if (!dest) {
		kfree(src);
		return -ENOMEM;
	}

	/* Fill in src buffer */
	for (i = 0; i < IOP_ADMA_TEST_SIZE; i++)
		((u8 *) src)[i] = (u8)i;

	/* Start copy, using first DMA channel */
	dma_chan = container_of(device->common.channels.next,
				struct dma_chan,
				device_node);
	if (iop_adma_alloc_chan_resources(dma_chan, NULL) < 1) {
		err = -ENODEV;
		goto out;
	}

	dest_dma = dma_map_single(dma_chan->device->dev, dest,
				IOP_ADMA_TEST_SIZE, DMA_FROM_DEVICE);
	src_dma = dma_map_single(dma_chan->device->dev, src,
				IOP_ADMA_TEST_SIZE, DMA_TO_DEVICE);
	tx = iop_adma_prep_dma_memcpy(dma_chan, dest_dma, src_dma,
				      IOP_ADMA_TEST_SIZE,
				      DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	cookie = iop_adma_tx_submit(tx);
	iop_adma_issue_pending(dma_chan);
	msleep(1);

	if (iop_adma_is_complete(dma_chan, cookie, NULL, NULL) !=
			DMA_SUCCESS) {
		dev_printk(KERN_ERR, dma_chan->device->dev,
			"Self-test copy timed out, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

	iop_chan = to_iop_adma_chan(dma_chan);
	dma_sync_single_for_cpu(&iop_chan->device->pdev->dev, dest_dma,
		IOP_ADMA_TEST_SIZE, DMA_FROM_DEVICE);
	if (memcmp(src, dest, IOP_ADMA_TEST_SIZE)) {
		dev_printk(KERN_ERR, dma_chan->device->dev,
			"Self-test copy failed compare, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

free_resources:
	iop_adma_free_chan_resources(dma_chan);
out:
	kfree(src);
	kfree(dest);
	return err;
}

#define IOP_ADMA_NUM_SRC_TEST 4 /* must be <= 15 */
static int __devinit
iop_adma_xor_zero_sum_self_test(struct iop_adma_device *device)
{
	int i, src_idx;
	struct page *dest;
	struct page *xor_srcs[IOP_ADMA_NUM_SRC_TEST];
	struct page *zero_sum_srcs[IOP_ADMA_NUM_SRC_TEST + 1];
	dma_addr_t dma_srcs[IOP_ADMA_NUM_SRC_TEST + 1];
	dma_addr_t dma_addr, dest_dma;
	struct dma_async_tx_descriptor *tx;
	struct dma_chan *dma_chan;
	dma_cookie_t cookie;
	u8 cmp_byte = 0;
	u32 cmp_word;
	u32 zero_sum_result;
	int err = 0;
	struct iop_adma_chan *iop_chan;

	dev_dbg(device->common.dev, "%s\n", __func__);

	for (src_idx = 0; src_idx < IOP_ADMA_NUM_SRC_TEST; src_idx++) {
		xor_srcs[src_idx] = alloc_page(GFP_KERNEL);
		if (!xor_srcs[src_idx])
			while (src_idx--) {
				__free_page(xor_srcs[src_idx]);
				return -ENOMEM;
			}
	}

	dest = alloc_page(GFP_KERNEL);
	if (!dest)
		while (src_idx--) {
			__free_page(xor_srcs[src_idx]);
			return -ENOMEM;
		}

	/* Fill in src buffers */
	for (src_idx = 0; src_idx < IOP_ADMA_NUM_SRC_TEST; src_idx++) {
		u8 *ptr = page_address(xor_srcs[src_idx]);
		for (i = 0; i < PAGE_SIZE; i++)
			ptr[i] = (1 << src_idx);
	}

	for (src_idx = 0; src_idx < IOP_ADMA_NUM_SRC_TEST; src_idx++)
		cmp_byte ^= (u8) (1 << src_idx);

	cmp_word = (cmp_byte << 24) | (cmp_byte << 16) |
			(cmp_byte << 8) | cmp_byte;

	memset(page_address(dest), 0, PAGE_SIZE);

	dma_chan = container_of(device->common.channels.next,
				struct dma_chan,
				device_node);
	if (iop_adma_alloc_chan_resources(dma_chan, NULL) < 1) {
		err = -ENODEV;
		goto out;
	}

	/* test xor */
	dest_dma = dma_map_page(dma_chan->device->dev, dest, 0,
				PAGE_SIZE, DMA_FROM_DEVICE);
	for (i = 0; i < IOP_ADMA_NUM_SRC_TEST; i++)
		dma_srcs[i] = dma_map_page(dma_chan->device->dev, xor_srcs[i],
					   0, PAGE_SIZE, DMA_TO_DEVICE);
	tx = iop_adma_prep_dma_xor(dma_chan, dest_dma, dma_srcs,
				   IOP_ADMA_NUM_SRC_TEST, PAGE_SIZE,
				   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	cookie = iop_adma_tx_submit(tx);
	iop_adma_issue_pending(dma_chan);
	msleep(8);

	if (iop_adma_is_complete(dma_chan, cookie, NULL, NULL) !=
		DMA_SUCCESS) {
		dev_printk(KERN_ERR, dma_chan->device->dev,
			"Self-test xor timed out, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

	iop_chan = to_iop_adma_chan(dma_chan);
	dma_sync_single_for_cpu(&iop_chan->device->pdev->dev, dest_dma,
		PAGE_SIZE, DMA_FROM_DEVICE);
	for (i = 0; i < (PAGE_SIZE / sizeof(u32)); i++) {
		u32 *ptr = page_address(dest);
		if (ptr[i] != cmp_word) {
			dev_printk(KERN_ERR, dma_chan->device->dev,
				"Self-test xor failed compare, disabling\n");
			err = -ENODEV;
			goto free_resources;
		}
	}
	dma_sync_single_for_device(&iop_chan->device->pdev->dev, dest_dma,
		PAGE_SIZE, DMA_TO_DEVICE);

	/* skip zero sum if the capability is not present */
	if (!dma_has_cap(DMA_ZERO_SUM, dma_chan->device->cap_mask))
		goto free_resources;

	/* zero sum the sources with the destintation page */
	for (i = 0; i < IOP_ADMA_NUM_SRC_TEST; i++)
		zero_sum_srcs[i] = xor_srcs[i];
	zero_sum_srcs[i] = dest;

	zero_sum_result = 1;

	for (i = 0; i < IOP_ADMA_NUM_SRC_TEST + 1; i++)
		dma_srcs[i] = dma_map_page(dma_chan->device->dev,
					   zero_sum_srcs[i], 0, PAGE_SIZE,
					   DMA_TO_DEVICE);
	tx = iop_adma_prep_dma_zero_sum(dma_chan, dma_srcs,
					IOP_ADMA_NUM_SRC_TEST + 1, PAGE_SIZE,
					&zero_sum_result,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	cookie = iop_adma_tx_submit(tx);
	iop_adma_issue_pending(dma_chan);
	msleep(8);

	if (iop_adma_is_complete(dma_chan, cookie, NULL, NULL) != DMA_SUCCESS) {
		dev_printk(KERN_ERR, dma_chan->device->dev,
			"Self-test zero sum timed out, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

	if (zero_sum_result != 0) {
		dev_printk(KERN_ERR, dma_chan->device->dev,
			"Self-test zero sum failed compare, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

	/* test memset */
	dma_addr = dma_map_page(dma_chan->device->dev, dest, 0,
			PAGE_SIZE, DMA_FROM_DEVICE);
	tx = iop_adma_prep_dma_memset(dma_chan, dma_addr, 0, PAGE_SIZE,
				      DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	cookie = iop_adma_tx_submit(tx);
	iop_adma_issue_pending(dma_chan);
	msleep(8);

	if (iop_adma_is_complete(dma_chan, cookie, NULL, NULL) != DMA_SUCCESS) {
		dev_printk(KERN_ERR, dma_chan->device->dev,
			"Self-test memset timed out, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

	for (i = 0; i < PAGE_SIZE/sizeof(u32); i++) {
		u32 *ptr = page_address(dest);
		if (ptr[i]) {
			dev_printk(KERN_ERR, dma_chan->device->dev,
				"Self-test memset failed compare, disabling\n");
			err = -ENODEV;
			goto free_resources;
		}
	}

	/* test for non-zero parity sum */
	zero_sum_result = 0;
	for (i = 0; i < IOP_ADMA_NUM_SRC_TEST + 1; i++)
		dma_srcs[i] = dma_map_page(dma_chan->device->dev,
					   zero_sum_srcs[i], 0, PAGE_SIZE,
					   DMA_TO_DEVICE);
	tx = iop_adma_prep_dma_zero_sum(dma_chan, dma_srcs,
					IOP_ADMA_NUM_SRC_TEST + 1, PAGE_SIZE,
					&zero_sum_result,
					DMA_PREP_INTERRUPT | DMA_CTRL_ACK);

	cookie = iop_adma_tx_submit(tx);
	iop_adma_issue_pending(dma_chan);
	msleep(8);

	if (iop_adma_is_complete(dma_chan, cookie, NULL, NULL) != DMA_SUCCESS) {
		dev_printk(KERN_ERR, dma_chan->device->dev,
			"Self-test non-zero sum timed out, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

	if (zero_sum_result != 1) {
		dev_printk(KERN_ERR, dma_chan->device->dev,
			"Self-test non-zero sum failed compare, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

free_resources:
	iop_adma_free_chan_resources(dma_chan);
out:
	src_idx = IOP_ADMA_NUM_SRC_TEST;
	while (src_idx--)
		__free_page(xor_srcs[src_idx]);
	__free_page(dest);
	return err;
}

static int __devexit iop_adma_remove(struct platform_device *dev)
{
	struct iop_adma_device *device = platform_get_drvdata(dev);
	struct dma_chan *chan, *_chan;
	struct iop_adma_chan *iop_chan;
	int i;
	struct iop_adma_platform_data *plat_data = dev->dev.platform_data;

	dma_async_device_unregister(&device->common);

	for (i = 0; i < 3; i++) {
		unsigned int irq;
		irq = platform_get_irq(dev, i);
		free_irq(irq, device);
	}

	dma_free_coherent(&dev->dev, plat_data->pool_size,
			device->dma_desc_pool_virt, device->dma_desc_pool);

	do {
		struct resource *res;
		res = platform_get_resource(dev, IORESOURCE_MEM, 0);
		release_mem_region(res->start, res->end - res->start);
	} while (0);

	list_for_each_entry_safe(chan, _chan, &device->common.channels,
				device_node) {
		iop_chan = to_iop_adma_chan(chan);
		list_del(&chan->device_node);
		kfree(iop_chan);
	}
	kfree(device);

	return 0;
}

static int __devinit iop_adma_probe(struct platform_device *pdev)
{
	struct resource *res;
	int ret = 0, i;
	struct iop_adma_device *adev;
	struct iop_adma_chan *iop_chan;
	struct dma_device *dma_dev;
	struct iop_adma_platform_data *plat_data = pdev->dev.platform_data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	if (!devm_request_mem_region(&pdev->dev, res->start,
				res->end - res->start, pdev->name))
		return -EBUSY;

	adev = kzalloc(sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return -ENOMEM;
	dma_dev = &adev->common;

	/* allocate coherent memory for hardware descriptors
	 * note: writecombine gives slightly better performance, but
	 * requires that we explicitly flush the writes
	 */
	if ((adev->dma_desc_pool_virt = dma_alloc_writecombine(&pdev->dev,
					plat_data->pool_size,
					&adev->dma_desc_pool,
					GFP_KERNEL)) == NULL) {
		ret = -ENOMEM;
		goto err_free_adev;
	}

	dev_dbg(&pdev->dev, "%s: allocted descriptor pool virt %p phys %p\n",
		__func__, adev->dma_desc_pool_virt,
		(void *) adev->dma_desc_pool);

	adev->id = plat_data->hw_id;

	/* discover transaction capabilites from the platform data */
	dma_dev->cap_mask = plat_data->cap_mask;

	adev->pdev = pdev;
	platform_set_drvdata(pdev, adev);

	INIT_LIST_HEAD(&dma_dev->channels);

	/* set base routines */
	dma_dev->device_alloc_chan_resources = iop_adma_alloc_chan_resources;
	dma_dev->device_free_chan_resources = iop_adma_free_chan_resources;
	dma_dev->device_is_tx_complete = iop_adma_is_complete;
	dma_dev->device_issue_pending = iop_adma_issue_pending;
	dma_dev->dev = &pdev->dev;

	/* set prep routines based on capability */
	if (dma_has_cap(DMA_MEMCPY, dma_dev->cap_mask))
		dma_dev->device_prep_dma_memcpy = iop_adma_prep_dma_memcpy;
	if (dma_has_cap(DMA_MEMSET, dma_dev->cap_mask))
		dma_dev->device_prep_dma_memset = iop_adma_prep_dma_memset;
	if (dma_has_cap(DMA_XOR, dma_dev->cap_mask)) {
		dma_dev->max_xor = iop_adma_get_max_xor();
		dma_dev->device_prep_dma_xor = iop_adma_prep_dma_xor;
	}
	if (dma_has_cap(DMA_ZERO_SUM, dma_dev->cap_mask))
		dma_dev->device_prep_dma_zero_sum =
			iop_adma_prep_dma_zero_sum;
	if (dma_has_cap(DMA_INTERRUPT, dma_dev->cap_mask))
		dma_dev->device_prep_dma_interrupt =
			iop_adma_prep_dma_interrupt;

	iop_chan = kzalloc(sizeof(*iop_chan), GFP_KERNEL);
	if (!iop_chan) {
		ret = -ENOMEM;
		goto err_free_dma;
	}
	iop_chan->device = adev;

	iop_chan->mmr_base = devm_ioremap(&pdev->dev, res->start,
					res->end - res->start);
	if (!iop_chan->mmr_base) {
		ret = -ENOMEM;
		goto err_free_iop_chan;
	}
	tasklet_init(&iop_chan->irq_tasklet, iop_adma_tasklet, (unsigned long)
		iop_chan);

	/* clear errors before enabling interrupts */
	iop_adma_device_clear_err_status(iop_chan);

	for (i = 0; i < 3; i++) {
		irq_handler_t handler[] = { iop_adma_eot_handler,
					iop_adma_eoc_handler,
					iop_adma_err_handler };
		int irq = platform_get_irq(pdev, i);
		if (irq < 0) {
			ret = -ENXIO;
			goto err_free_iop_chan;
		} else {
			ret = devm_request_irq(&pdev->dev, irq,
					handler[i], 0, pdev->name, iop_chan);
			if (ret)
				goto err_free_iop_chan;
		}
	}

	spin_lock_init(&iop_chan->lock);
	INIT_LIST_HEAD(&iop_chan->chain);
	INIT_LIST_HEAD(&iop_chan->all_slots);
	INIT_RCU_HEAD(&iop_chan->common.rcu);
	iop_chan->common.device = dma_dev;
	list_add_tail(&iop_chan->common.device_node, &dma_dev->channels);

	if (dma_has_cap(DMA_MEMCPY, dma_dev->cap_mask)) {
		ret = iop_adma_memcpy_self_test(adev);
		dev_dbg(&pdev->dev, "memcpy self test returned %d\n", ret);
		if (ret)
			goto err_free_iop_chan;
	}

	if (dma_has_cap(DMA_XOR, dma_dev->cap_mask) ||
		dma_has_cap(DMA_MEMSET, dma_dev->cap_mask)) {
		ret = iop_adma_xor_zero_sum_self_test(adev);
		dev_dbg(&pdev->dev, "xor self test returned %d\n", ret);
		if (ret)
			goto err_free_iop_chan;
	}

	dev_printk(KERN_INFO, &pdev->dev, "Intel(R) IOP: "
	  "( %s%s%s%s%s%s%s%s%s%s)\n",
	  dma_has_cap(DMA_PQ_XOR, dma_dev->cap_mask) ? "pq_xor " : "",
	  dma_has_cap(DMA_PQ_UPDATE, dma_dev->cap_mask) ? "pq_update " : "",
	  dma_has_cap(DMA_PQ_ZERO_SUM, dma_dev->cap_mask) ? "pq_zero_sum " : "",
	  dma_has_cap(DMA_XOR, dma_dev->cap_mask) ? "xor " : "",
	  dma_has_cap(DMA_DUAL_XOR, dma_dev->cap_mask) ? "dual_xor " : "",
	  dma_has_cap(DMA_ZERO_SUM, dma_dev->cap_mask) ? "xor_zero_sum " : "",
	  dma_has_cap(DMA_MEMSET, dma_dev->cap_mask)  ? "fill " : "",
	  dma_has_cap(DMA_MEMCPY_CRC32C, dma_dev->cap_mask) ? "cpy+crc " : "",
	  dma_has_cap(DMA_MEMCPY, dma_dev->cap_mask) ? "cpy " : "",
	  dma_has_cap(DMA_INTERRUPT, dma_dev->cap_mask) ? "intr " : "");

	dma_async_device_register(dma_dev);
	goto out;

 err_free_iop_chan:
	kfree(iop_chan);
 err_free_dma:
	dma_free_coherent(&adev->pdev->dev, plat_data->pool_size,
			adev->dma_desc_pool_virt, adev->dma_desc_pool);
 err_free_adev:
	kfree(adev);
 out:
	return ret;
}

static void iop_chan_start_null_memcpy(struct iop_adma_chan *iop_chan)
{
	struct iop_adma_desc_slot *sw_desc, *grp_start;
	dma_cookie_t cookie;
	int slot_cnt, slots_per_op;

	dev_dbg(iop_chan->device->common.dev, "%s\n", __func__);

	spin_lock_bh(&iop_chan->lock);
	slot_cnt = iop_chan_memcpy_slot_count(0, &slots_per_op);
	sw_desc = iop_adma_alloc_slots(iop_chan, slot_cnt, slots_per_op);
	if (sw_desc) {
		grp_start = sw_desc->group_head;

		list_splice_init(&sw_desc->async_tx.tx_list, &iop_chan->chain);
		async_tx_ack(&sw_desc->async_tx);
		iop_desc_init_memcpy(grp_start, 0);
		iop_desc_set_byte_count(grp_start, iop_chan, 0);
		iop_desc_set_dest_addr(grp_start, iop_chan, 0);
		iop_desc_set_memcpy_src_addr(grp_start, 0);

		cookie = iop_chan->common.cookie;
		cookie++;
		if (cookie <= 1)
			cookie = 2;

		/* initialize the completed cookie to be less than
		 * the most recently used cookie
		 */
		iop_chan->completed_cookie = cookie - 1;
		iop_chan->common.cookie = sw_desc->async_tx.cookie = cookie;

		/* channel should not be busy */
		BUG_ON(iop_chan_is_busy(iop_chan));

		/* clear any prior error-status bits */
		iop_adma_device_clear_err_status(iop_chan);

		/* disable operation */
		iop_chan_disable(iop_chan);

		/* set the descriptor address */
		iop_chan_set_next_descriptor(iop_chan, sw_desc->async_tx.phys);

		/* 1/ don't add pre-chained descriptors
		 * 2/ dummy read to flush next_desc write
		 */
		BUG_ON(iop_desc_get_next_desc(sw_desc));

		/* run the descriptor */
		iop_chan_enable(iop_chan);
	} else
		dev_printk(KERN_ERR, iop_chan->device->common.dev,
			 "failed to allocate null descriptor\n");
	spin_unlock_bh(&iop_chan->lock);
}

static void iop_chan_start_null_xor(struct iop_adma_chan *iop_chan)
{
	struct iop_adma_desc_slot *sw_desc, *grp_start;
	dma_cookie_t cookie;
	int slot_cnt, slots_per_op;

	dev_dbg(iop_chan->device->common.dev, "%s\n", __func__);

	spin_lock_bh(&iop_chan->lock);
	slot_cnt = iop_chan_xor_slot_count(0, 2, &slots_per_op);
	sw_desc = iop_adma_alloc_slots(iop_chan, slot_cnt, slots_per_op);
	if (sw_desc) {
		grp_start = sw_desc->group_head;
		list_splice_init(&sw_desc->async_tx.tx_list, &iop_chan->chain);
		async_tx_ack(&sw_desc->async_tx);
		iop_desc_init_null_xor(grp_start, 2, 0);
		iop_desc_set_byte_count(grp_start, iop_chan, 0);
		iop_desc_set_dest_addr(grp_start, iop_chan, 0);
		iop_desc_set_xor_src_addr(grp_start, 0, 0);
		iop_desc_set_xor_src_addr(grp_start, 1, 0);

		cookie = iop_chan->common.cookie;
		cookie++;
		if (cookie <= 1)
			cookie = 2;

		/* initialize the completed cookie to be less than
		 * the most recently used cookie
		 */
		iop_chan->completed_cookie = cookie - 1;
		iop_chan->common.cookie = sw_desc->async_tx.cookie = cookie;

		/* channel should not be busy */
		BUG_ON(iop_chan_is_busy(iop_chan));

		/* clear any prior error-status bits */
		iop_adma_device_clear_err_status(iop_chan);

		/* disable operation */
		iop_chan_disable(iop_chan);

		/* set the descriptor address */
		iop_chan_set_next_descriptor(iop_chan, sw_desc->async_tx.phys);

		/* 1/ don't add pre-chained descriptors
		 * 2/ dummy read to flush next_desc write
		 */
		BUG_ON(iop_desc_get_next_desc(sw_desc));

		/* run the descriptor */
		iop_chan_enable(iop_chan);
	} else
		dev_printk(KERN_ERR, iop_chan->device->common.dev,
			"failed to allocate null descriptor\n");
	spin_unlock_bh(&iop_chan->lock);
}

MODULE_ALIAS("platform:iop-adma");

static struct platform_driver iop_adma_driver = {
	.probe		= iop_adma_probe,
	.remove		= iop_adma_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "iop-adma",
	},
};

static int __init iop_adma_init (void)
{
	return platform_driver_register(&iop_adma_driver);
}

/* it's currently unsafe to unload this module */
#if 0
static void __exit iop_adma_exit (void)
{
	platform_driver_unregister(&iop_adma_driver);
	return;
}
module_exit(iop_adma_exit);
#endif

module_init(iop_adma_init);

MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("IOP ADMA Engine Driver");
MODULE_LICENSE("GPL");
