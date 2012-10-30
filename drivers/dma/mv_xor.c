/*
 * offload engine driver for the Marvell XOR engine
 * Copyright (C) 2007, 2008, Marvell International Ltd.
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
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/memory.h>
#include <linux/clk.h>
#include <linux/platform_data/dma-mv_xor.h>

#include "dmaengine.h"
#include "mv_xor.h"

static void mv_xor_issue_pending(struct dma_chan *chan);

#define to_mv_xor_chan(chan)		\
	container_of(chan, struct mv_xor_chan, common)

#define to_mv_xor_device(dev)		\
	container_of(dev, struct mv_xor_device, common)

#define to_mv_xor_slot(tx)		\
	container_of(tx, struct mv_xor_desc_slot, async_tx)

static void mv_desc_init(struct mv_xor_desc_slot *desc, unsigned long flags)
{
	struct mv_xor_desc *hw_desc = desc->hw_desc;

	hw_desc->status = (1 << 31);
	hw_desc->phy_next_desc = 0;
	hw_desc->desc_command = (1 << 31);
}

static u32 mv_desc_get_dest_addr(struct mv_xor_desc_slot *desc)
{
	struct mv_xor_desc *hw_desc = desc->hw_desc;
	return hw_desc->phy_dest_addr;
}

static u32 mv_desc_get_src_addr(struct mv_xor_desc_slot *desc,
				int src_idx)
{
	struct mv_xor_desc *hw_desc = desc->hw_desc;
	return hw_desc->phy_src_addr[src_idx];
}


static void mv_desc_set_byte_count(struct mv_xor_desc_slot *desc,
				   u32 byte_count)
{
	struct mv_xor_desc *hw_desc = desc->hw_desc;
	hw_desc->byte_count = byte_count;
}

static void mv_desc_set_next_desc(struct mv_xor_desc_slot *desc,
				  u32 next_desc_addr)
{
	struct mv_xor_desc *hw_desc = desc->hw_desc;
	BUG_ON(hw_desc->phy_next_desc);
	hw_desc->phy_next_desc = next_desc_addr;
}

static void mv_desc_clear_next_desc(struct mv_xor_desc_slot *desc)
{
	struct mv_xor_desc *hw_desc = desc->hw_desc;
	hw_desc->phy_next_desc = 0;
}

static void mv_desc_set_block_fill_val(struct mv_xor_desc_slot *desc, u32 val)
{
	desc->value = val;
}

static void mv_desc_set_dest_addr(struct mv_xor_desc_slot *desc,
				  dma_addr_t addr)
{
	struct mv_xor_desc *hw_desc = desc->hw_desc;
	hw_desc->phy_dest_addr = addr;
}

static int mv_chan_memset_slot_count(size_t len)
{
	return 1;
}

#define mv_chan_memcpy_slot_count(c) mv_chan_memset_slot_count(c)

static void mv_desc_set_src_addr(struct mv_xor_desc_slot *desc,
				 int index, dma_addr_t addr)
{
	struct mv_xor_desc *hw_desc = desc->hw_desc;
	hw_desc->phy_src_addr[index] = addr;
	if (desc->type == DMA_XOR)
		hw_desc->desc_command |= (1 << index);
}

static u32 mv_chan_get_current_desc(struct mv_xor_chan *chan)
{
	return __raw_readl(XOR_CURR_DESC(chan));
}

static void mv_chan_set_next_descriptor(struct mv_xor_chan *chan,
					u32 next_desc_addr)
{
	__raw_writel(next_desc_addr, XOR_NEXT_DESC(chan));
}

static void mv_chan_set_dest_pointer(struct mv_xor_chan *chan, u32 desc_addr)
{
	__raw_writel(desc_addr, XOR_DEST_POINTER(chan));
}

static void mv_chan_set_block_size(struct mv_xor_chan *chan, u32 block_size)
{
	__raw_writel(block_size, XOR_BLOCK_SIZE(chan));
}

static void mv_chan_set_value(struct mv_xor_chan *chan, u32 value)
{
	__raw_writel(value, XOR_INIT_VALUE_LOW(chan));
	__raw_writel(value, XOR_INIT_VALUE_HIGH(chan));
}

static void mv_chan_unmask_interrupts(struct mv_xor_chan *chan)
{
	u32 val = __raw_readl(XOR_INTR_MASK(chan));
	val |= XOR_INTR_MASK_VALUE << (chan->idx * 16);
	__raw_writel(val, XOR_INTR_MASK(chan));
}

static u32 mv_chan_get_intr_cause(struct mv_xor_chan *chan)
{
	u32 intr_cause = __raw_readl(XOR_INTR_CAUSE(chan));
	intr_cause = (intr_cause >> (chan->idx * 16)) & 0xFFFF;
	return intr_cause;
}

static int mv_is_err_intr(u32 intr_cause)
{
	if (intr_cause & ((1<<4)|(1<<5)|(1<<6)|(1<<7)|(1<<8)|(1<<9)))
		return 1;

	return 0;
}

static void mv_xor_device_clear_eoc_cause(struct mv_xor_chan *chan)
{
	u32 val = ~(1 << (chan->idx * 16));
	dev_dbg(chan->device->common.dev, "%s, val 0x%08x\n", __func__, val);
	__raw_writel(val, XOR_INTR_CAUSE(chan));
}

static void mv_xor_device_clear_err_status(struct mv_xor_chan *chan)
{
	u32 val = 0xFFFF0000 >> (chan->idx * 16);
	__raw_writel(val, XOR_INTR_CAUSE(chan));
}

static int mv_can_chain(struct mv_xor_desc_slot *desc)
{
	struct mv_xor_desc_slot *chain_old_tail = list_entry(
		desc->chain_node.prev, struct mv_xor_desc_slot, chain_node);

	if (chain_old_tail->type != desc->type)
		return 0;
	if (desc->type == DMA_MEMSET)
		return 0;

	return 1;
}

static void mv_set_mode(struct mv_xor_chan *chan,
			       enum dma_transaction_type type)
{
	u32 op_mode;
	u32 config = __raw_readl(XOR_CONFIG(chan));

	switch (type) {
	case DMA_XOR:
		op_mode = XOR_OPERATION_MODE_XOR;
		break;
	case DMA_MEMCPY:
		op_mode = XOR_OPERATION_MODE_MEMCPY;
		break;
	case DMA_MEMSET:
		op_mode = XOR_OPERATION_MODE_MEMSET;
		break;
	default:
		dev_err(chan->device->common.dev,
			"error: unsupported operation %d.\n",
			type);
		BUG();
		return;
	}

	config &= ~0x7;
	config |= op_mode;
	__raw_writel(config, XOR_CONFIG(chan));
	chan->current_type = type;
}

static void mv_chan_activate(struct mv_xor_chan *chan)
{
	u32 activation;

	dev_dbg(chan->device->common.dev, " activate chan.\n");
	activation = __raw_readl(XOR_ACTIVATION(chan));
	activation |= 0x1;
	__raw_writel(activation, XOR_ACTIVATION(chan));
}

static char mv_chan_is_busy(struct mv_xor_chan *chan)
{
	u32 state = __raw_readl(XOR_ACTIVATION(chan));

	state = (state >> 4) & 0x3;

	return (state == 1) ? 1 : 0;
}

static int mv_chan_xor_slot_count(size_t len, int src_cnt)
{
	return 1;
}

/**
 * mv_xor_free_slots - flags descriptor slots for reuse
 * @slot: Slot to free
 * Caller must hold &mv_chan->lock while calling this function
 */
static void mv_xor_free_slots(struct mv_xor_chan *mv_chan,
			      struct mv_xor_desc_slot *slot)
{
	dev_dbg(mv_chan->device->common.dev, "%s %d slot %p\n",
		__func__, __LINE__, slot);

	slot->slots_per_op = 0;

}

/*
 * mv_xor_start_new_chain - program the engine to operate on new chain headed by
 * sw_desc
 * Caller must hold &mv_chan->lock while calling this function
 */
static void mv_xor_start_new_chain(struct mv_xor_chan *mv_chan,
				   struct mv_xor_desc_slot *sw_desc)
{
	dev_dbg(mv_chan->device->common.dev, "%s %d: sw_desc %p\n",
		__func__, __LINE__, sw_desc);
	if (sw_desc->type != mv_chan->current_type)
		mv_set_mode(mv_chan, sw_desc->type);

	if (sw_desc->type == DMA_MEMSET) {
		/* for memset requests we need to program the engine, no
		 * descriptors used.
		 */
		struct mv_xor_desc *hw_desc = sw_desc->hw_desc;
		mv_chan_set_dest_pointer(mv_chan, hw_desc->phy_dest_addr);
		mv_chan_set_block_size(mv_chan, sw_desc->unmap_len);
		mv_chan_set_value(mv_chan, sw_desc->value);
	} else {
		/* set the hardware chain */
		mv_chan_set_next_descriptor(mv_chan, sw_desc->async_tx.phys);
	}
	mv_chan->pending += sw_desc->slot_cnt;
	mv_xor_issue_pending(&mv_chan->common);
}

static dma_cookie_t
mv_xor_run_tx_complete_actions(struct mv_xor_desc_slot *desc,
	struct mv_xor_chan *mv_chan, dma_cookie_t cookie)
{
	BUG_ON(desc->async_tx.cookie < 0);

	if (desc->async_tx.cookie > 0) {
		cookie = desc->async_tx.cookie;

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
			struct mv_xor_desc_slot *unmap = desc->group_head;
			struct device *dev =
				&mv_chan->device->pdev->dev;
			u32 len = unmap->unmap_len;
			enum dma_ctrl_flags flags = desc->async_tx.flags;
			u32 src_cnt;
			dma_addr_t addr;
			dma_addr_t dest;

			src_cnt = unmap->unmap_src_cnt;
			dest = mv_desc_get_dest_addr(unmap);
			if (!(flags & DMA_COMPL_SKIP_DEST_UNMAP)) {
				enum dma_data_direction dir;

				if (src_cnt > 1) /* is xor ? */
					dir = DMA_BIDIRECTIONAL;
				else
					dir = DMA_FROM_DEVICE;
				dma_unmap_page(dev, dest, len, dir);
			}

			if (!(flags & DMA_COMPL_SKIP_SRC_UNMAP)) {
				while (src_cnt--) {
					addr = mv_desc_get_src_addr(unmap,
								    src_cnt);
					if (addr == dest)
						continue;
					dma_unmap_page(dev, addr, len,
						       DMA_TO_DEVICE);
				}
			}
			desc->group_head = NULL;
		}
	}

	/* run dependent operations */
	dma_run_dependencies(&desc->async_tx);

	return cookie;
}

static int
mv_xor_clean_completed_slots(struct mv_xor_chan *mv_chan)
{
	struct mv_xor_desc_slot *iter, *_iter;

	dev_dbg(mv_chan->device->common.dev, "%s %d\n", __func__, __LINE__);
	list_for_each_entry_safe(iter, _iter, &mv_chan->completed_slots,
				 completed_node) {

		if (async_tx_test_ack(&iter->async_tx)) {
			list_del(&iter->completed_node);
			mv_xor_free_slots(mv_chan, iter);
		}
	}
	return 0;
}

static int
mv_xor_clean_slot(struct mv_xor_desc_slot *desc,
	struct mv_xor_chan *mv_chan)
{
	dev_dbg(mv_chan->device->common.dev, "%s %d: desc %p flags %d\n",
		__func__, __LINE__, desc, desc->async_tx.flags);
	list_del(&desc->chain_node);
	/* the client is allowed to attach dependent operations
	 * until 'ack' is set
	 */
	if (!async_tx_test_ack(&desc->async_tx)) {
		/* move this slot to the completed_slots */
		list_add_tail(&desc->completed_node, &mv_chan->completed_slots);
		return 0;
	}

	mv_xor_free_slots(mv_chan, desc);
	return 0;
}

static void __mv_xor_slot_cleanup(struct mv_xor_chan *mv_chan)
{
	struct mv_xor_desc_slot *iter, *_iter;
	dma_cookie_t cookie = 0;
	int busy = mv_chan_is_busy(mv_chan);
	u32 current_desc = mv_chan_get_current_desc(mv_chan);
	int seen_current = 0;

	dev_dbg(mv_chan->device->common.dev, "%s %d\n", __func__, __LINE__);
	dev_dbg(mv_chan->device->common.dev, "current_desc %x\n", current_desc);
	mv_xor_clean_completed_slots(mv_chan);

	/* free completed slots from the chain starting with
	 * the oldest descriptor
	 */

	list_for_each_entry_safe(iter, _iter, &mv_chan->chain,
					chain_node) {
		prefetch(_iter);
		prefetch(&_iter->async_tx);

		/* do not advance past the current descriptor loaded into the
		 * hardware channel, subsequent descriptors are either in
		 * process or have not been submitted
		 */
		if (seen_current)
			break;

		/* stop the search if we reach the current descriptor and the
		 * channel is busy
		 */
		if (iter->async_tx.phys == current_desc) {
			seen_current = 1;
			if (busy)
				break;
		}

		cookie = mv_xor_run_tx_complete_actions(iter, mv_chan, cookie);

		if (mv_xor_clean_slot(iter, mv_chan))
			break;
	}

	if ((busy == 0) && !list_empty(&mv_chan->chain)) {
		struct mv_xor_desc_slot *chain_head;
		chain_head = list_entry(mv_chan->chain.next,
					struct mv_xor_desc_slot,
					chain_node);

		mv_xor_start_new_chain(mv_chan, chain_head);
	}

	if (cookie > 0)
		mv_chan->common.completed_cookie = cookie;
}

static void
mv_xor_slot_cleanup(struct mv_xor_chan *mv_chan)
{
	spin_lock_bh(&mv_chan->lock);
	__mv_xor_slot_cleanup(mv_chan);
	spin_unlock_bh(&mv_chan->lock);
}

static void mv_xor_tasklet(unsigned long data)
{
	struct mv_xor_chan *chan = (struct mv_xor_chan *) data;
	mv_xor_slot_cleanup(chan);
}

static struct mv_xor_desc_slot *
mv_xor_alloc_slots(struct mv_xor_chan *mv_chan, int num_slots,
		    int slots_per_op)
{
	struct mv_xor_desc_slot *iter, *_iter, *alloc_start = NULL;
	LIST_HEAD(chain);
	int slots_found, retry = 0;

	/* start search from the last allocated descrtiptor
	 * if a contiguous allocation can not be found start searching
	 * from the beginning of the list
	 */
retry:
	slots_found = 0;
	if (retry == 0)
		iter = mv_chan->last_used;
	else
		iter = list_entry(&mv_chan->all_slots,
			struct mv_xor_desc_slot,
			slot_node);

	list_for_each_entry_safe_continue(
		iter, _iter, &mv_chan->all_slots, slot_node) {
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
		if (!slots_found++)
			alloc_start = iter;

		if (slots_found == num_slots) {
			struct mv_xor_desc_slot *alloc_tail = NULL;
			struct mv_xor_desc_slot *last_used = NULL;
			iter = alloc_start;
			while (num_slots) {
				int i;

				/* pre-ack all but the last descriptor */
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
						struct mv_xor_desc_slot,
						slot_node);
				}
				num_slots -= slots_per_op;
			}
			alloc_tail->group_head = alloc_start;
			alloc_tail->async_tx.cookie = -EBUSY;
			list_splice(&chain, &alloc_tail->tx_list);
			mv_chan->last_used = last_used;
			mv_desc_clear_next_desc(alloc_start);
			mv_desc_clear_next_desc(alloc_tail);
			return alloc_tail;
		}
	}
	if (!retry++)
		goto retry;

	/* try to free some slots if the allocation fails */
	tasklet_schedule(&mv_chan->irq_tasklet);

	return NULL;
}

/************************ DMA engine API functions ****************************/
static dma_cookie_t
mv_xor_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct mv_xor_desc_slot *sw_desc = to_mv_xor_slot(tx);
	struct mv_xor_chan *mv_chan = to_mv_xor_chan(tx->chan);
	struct mv_xor_desc_slot *grp_start, *old_chain_tail;
	dma_cookie_t cookie;
	int new_hw_chain = 1;

	dev_dbg(mv_chan->device->common.dev,
		"%s sw_desc %p: async_tx %p\n",
		__func__, sw_desc, &sw_desc->async_tx);

	grp_start = sw_desc->group_head;

	spin_lock_bh(&mv_chan->lock);
	cookie = dma_cookie_assign(tx);

	if (list_empty(&mv_chan->chain))
		list_splice_init(&sw_desc->tx_list, &mv_chan->chain);
	else {
		new_hw_chain = 0;

		old_chain_tail = list_entry(mv_chan->chain.prev,
					    struct mv_xor_desc_slot,
					    chain_node);
		list_splice_init(&grp_start->tx_list,
				 &old_chain_tail->chain_node);

		if (!mv_can_chain(grp_start))
			goto submit_done;

		dev_dbg(mv_chan->device->common.dev, "Append to last desc %x\n",
			old_chain_tail->async_tx.phys);

		/* fix up the hardware chain */
		mv_desc_set_next_desc(old_chain_tail, grp_start->async_tx.phys);

		/* if the channel is not busy */
		if (!mv_chan_is_busy(mv_chan)) {
			u32 current_desc = mv_chan_get_current_desc(mv_chan);
			/*
			 * and the curren desc is the end of the chain before
			 * the append, then we need to start the channel
			 */
			if (current_desc == old_chain_tail->async_tx.phys)
				new_hw_chain = 1;
		}
	}

	if (new_hw_chain)
		mv_xor_start_new_chain(mv_chan, grp_start);

submit_done:
	spin_unlock_bh(&mv_chan->lock);

	return cookie;
}

/* returns the number of allocated descriptors */
static int mv_xor_alloc_chan_resources(struct dma_chan *chan)
{
	char *hw_desc;
	int idx;
	struct mv_xor_chan *mv_chan = to_mv_xor_chan(chan);
	struct mv_xor_desc_slot *slot = NULL;
	int num_descs_in_pool = mv_chan->device->pool_size/MV_XOR_SLOT_SIZE;

	/* Allocate descriptor slots */
	idx = mv_chan->slots_allocated;
	while (idx < num_descs_in_pool) {
		slot = kzalloc(sizeof(*slot), GFP_KERNEL);
		if (!slot) {
			printk(KERN_INFO "MV XOR Channel only initialized"
				" %d descriptor slots", idx);
			break;
		}
		hw_desc = (char *) mv_chan->device->dma_desc_pool_virt;
		slot->hw_desc = (void *) &hw_desc[idx * MV_XOR_SLOT_SIZE];

		dma_async_tx_descriptor_init(&slot->async_tx, chan);
		slot->async_tx.tx_submit = mv_xor_tx_submit;
		INIT_LIST_HEAD(&slot->chain_node);
		INIT_LIST_HEAD(&slot->slot_node);
		INIT_LIST_HEAD(&slot->tx_list);
		hw_desc = (char *) mv_chan->device->dma_desc_pool;
		slot->async_tx.phys =
			(dma_addr_t) &hw_desc[idx * MV_XOR_SLOT_SIZE];
		slot->idx = idx++;

		spin_lock_bh(&mv_chan->lock);
		mv_chan->slots_allocated = idx;
		list_add_tail(&slot->slot_node, &mv_chan->all_slots);
		spin_unlock_bh(&mv_chan->lock);
	}

	if (mv_chan->slots_allocated && !mv_chan->last_used)
		mv_chan->last_used = list_entry(mv_chan->all_slots.next,
					struct mv_xor_desc_slot,
					slot_node);

	dev_dbg(mv_chan->device->common.dev,
		"allocated %d descriptor slots last_used: %p\n",
		mv_chan->slots_allocated, mv_chan->last_used);

	return mv_chan->slots_allocated ? : -ENOMEM;
}

static struct dma_async_tx_descriptor *
mv_xor_prep_dma_memcpy(struct dma_chan *chan, dma_addr_t dest, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct mv_xor_chan *mv_chan = to_mv_xor_chan(chan);
	struct mv_xor_desc_slot *sw_desc, *grp_start;
	int slot_cnt;

	dev_dbg(mv_chan->device->common.dev,
		"%s dest: %x src %x len: %u flags: %ld\n",
		__func__, dest, src, len, flags);
	if (unlikely(len < MV_XOR_MIN_BYTE_COUNT))
		return NULL;

	BUG_ON(len > MV_XOR_MAX_BYTE_COUNT);

	spin_lock_bh(&mv_chan->lock);
	slot_cnt = mv_chan_memcpy_slot_count(len);
	sw_desc = mv_xor_alloc_slots(mv_chan, slot_cnt, 1);
	if (sw_desc) {
		sw_desc->type = DMA_MEMCPY;
		sw_desc->async_tx.flags = flags;
		grp_start = sw_desc->group_head;
		mv_desc_init(grp_start, flags);
		mv_desc_set_byte_count(grp_start, len);
		mv_desc_set_dest_addr(sw_desc->group_head, dest);
		mv_desc_set_src_addr(grp_start, 0, src);
		sw_desc->unmap_src_cnt = 1;
		sw_desc->unmap_len = len;
	}
	spin_unlock_bh(&mv_chan->lock);

	dev_dbg(mv_chan->device->common.dev,
		"%s sw_desc %p async_tx %p\n",
		__func__, sw_desc, sw_desc ? &sw_desc->async_tx : 0);

	return sw_desc ? &sw_desc->async_tx : NULL;
}

static struct dma_async_tx_descriptor *
mv_xor_prep_dma_memset(struct dma_chan *chan, dma_addr_t dest, int value,
		       size_t len, unsigned long flags)
{
	struct mv_xor_chan *mv_chan = to_mv_xor_chan(chan);
	struct mv_xor_desc_slot *sw_desc, *grp_start;
	int slot_cnt;

	dev_dbg(mv_chan->device->common.dev,
		"%s dest: %x len: %u flags: %ld\n",
		__func__, dest, len, flags);
	if (unlikely(len < MV_XOR_MIN_BYTE_COUNT))
		return NULL;

	BUG_ON(len > MV_XOR_MAX_BYTE_COUNT);

	spin_lock_bh(&mv_chan->lock);
	slot_cnt = mv_chan_memset_slot_count(len);
	sw_desc = mv_xor_alloc_slots(mv_chan, slot_cnt, 1);
	if (sw_desc) {
		sw_desc->type = DMA_MEMSET;
		sw_desc->async_tx.flags = flags;
		grp_start = sw_desc->group_head;
		mv_desc_init(grp_start, flags);
		mv_desc_set_byte_count(grp_start, len);
		mv_desc_set_dest_addr(sw_desc->group_head, dest);
		mv_desc_set_block_fill_val(grp_start, value);
		sw_desc->unmap_src_cnt = 1;
		sw_desc->unmap_len = len;
	}
	spin_unlock_bh(&mv_chan->lock);
	dev_dbg(mv_chan->device->common.dev,
		"%s sw_desc %p async_tx %p \n",
		__func__, sw_desc, &sw_desc->async_tx);
	return sw_desc ? &sw_desc->async_tx : NULL;
}

static struct dma_async_tx_descriptor *
mv_xor_prep_dma_xor(struct dma_chan *chan, dma_addr_t dest, dma_addr_t *src,
		    unsigned int src_cnt, size_t len, unsigned long flags)
{
	struct mv_xor_chan *mv_chan = to_mv_xor_chan(chan);
	struct mv_xor_desc_slot *sw_desc, *grp_start;
	int slot_cnt;

	if (unlikely(len < MV_XOR_MIN_BYTE_COUNT))
		return NULL;

	BUG_ON(len > MV_XOR_MAX_BYTE_COUNT);

	dev_dbg(mv_chan->device->common.dev,
		"%s src_cnt: %d len: dest %x %u flags: %ld\n",
		__func__, src_cnt, len, dest, flags);

	spin_lock_bh(&mv_chan->lock);
	slot_cnt = mv_chan_xor_slot_count(len, src_cnt);
	sw_desc = mv_xor_alloc_slots(mv_chan, slot_cnt, 1);
	if (sw_desc) {
		sw_desc->type = DMA_XOR;
		sw_desc->async_tx.flags = flags;
		grp_start = sw_desc->group_head;
		mv_desc_init(grp_start, flags);
		/* the byte count field is the same as in memcpy desc*/
		mv_desc_set_byte_count(grp_start, len);
		mv_desc_set_dest_addr(sw_desc->group_head, dest);
		sw_desc->unmap_src_cnt = src_cnt;
		sw_desc->unmap_len = len;
		while (src_cnt--)
			mv_desc_set_src_addr(grp_start, src_cnt, src[src_cnt]);
	}
	spin_unlock_bh(&mv_chan->lock);
	dev_dbg(mv_chan->device->common.dev,
		"%s sw_desc %p async_tx %p \n",
		__func__, sw_desc, &sw_desc->async_tx);
	return sw_desc ? &sw_desc->async_tx : NULL;
}

static void mv_xor_free_chan_resources(struct dma_chan *chan)
{
	struct mv_xor_chan *mv_chan = to_mv_xor_chan(chan);
	struct mv_xor_desc_slot *iter, *_iter;
	int in_use_descs = 0;

	mv_xor_slot_cleanup(mv_chan);

	spin_lock_bh(&mv_chan->lock);
	list_for_each_entry_safe(iter, _iter, &mv_chan->chain,
					chain_node) {
		in_use_descs++;
		list_del(&iter->chain_node);
	}
	list_for_each_entry_safe(iter, _iter, &mv_chan->completed_slots,
				 completed_node) {
		in_use_descs++;
		list_del(&iter->completed_node);
	}
	list_for_each_entry_safe_reverse(
		iter, _iter, &mv_chan->all_slots, slot_node) {
		list_del(&iter->slot_node);
		kfree(iter);
		mv_chan->slots_allocated--;
	}
	mv_chan->last_used = NULL;

	dev_dbg(mv_chan->device->common.dev, "%s slots_allocated %d\n",
		__func__, mv_chan->slots_allocated);
	spin_unlock_bh(&mv_chan->lock);

	if (in_use_descs)
		dev_err(mv_chan->device->common.dev,
			"freeing %d in use descriptors!\n", in_use_descs);
}

/**
 * mv_xor_status - poll the status of an XOR transaction
 * @chan: XOR channel handle
 * @cookie: XOR transaction identifier
 * @txstate: XOR transactions state holder (or NULL)
 */
static enum dma_status mv_xor_status(struct dma_chan *chan,
					  dma_cookie_t cookie,
					  struct dma_tx_state *txstate)
{
	struct mv_xor_chan *mv_chan = to_mv_xor_chan(chan);
	enum dma_status ret;

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret == DMA_SUCCESS) {
		mv_xor_clean_completed_slots(mv_chan);
		return ret;
	}
	mv_xor_slot_cleanup(mv_chan);

	return dma_cookie_status(chan, cookie, txstate);
}

static void mv_dump_xor_regs(struct mv_xor_chan *chan)
{
	u32 val;

	val = __raw_readl(XOR_CONFIG(chan));
	dev_err(chan->device->common.dev,
		"config       0x%08x.\n", val);

	val = __raw_readl(XOR_ACTIVATION(chan));
	dev_err(chan->device->common.dev,
		"activation   0x%08x.\n", val);

	val = __raw_readl(XOR_INTR_CAUSE(chan));
	dev_err(chan->device->common.dev,
		"intr cause   0x%08x.\n", val);

	val = __raw_readl(XOR_INTR_MASK(chan));
	dev_err(chan->device->common.dev,
		"intr mask    0x%08x.\n", val);

	val = __raw_readl(XOR_ERROR_CAUSE(chan));
	dev_err(chan->device->common.dev,
		"error cause  0x%08x.\n", val);

	val = __raw_readl(XOR_ERROR_ADDR(chan));
	dev_err(chan->device->common.dev,
		"error addr   0x%08x.\n", val);
}

static void mv_xor_err_interrupt_handler(struct mv_xor_chan *chan,
					 u32 intr_cause)
{
	if (intr_cause & (1 << 4)) {
	     dev_dbg(chan->device->common.dev,
		     "ignore this error\n");
	     return;
	}

	dev_err(chan->device->common.dev,
		"error on chan %d. intr cause 0x%08x.\n",
		chan->idx, intr_cause);

	mv_dump_xor_regs(chan);
	BUG();
}

static irqreturn_t mv_xor_interrupt_handler(int irq, void *data)
{
	struct mv_xor_chan *chan = data;
	u32 intr_cause = mv_chan_get_intr_cause(chan);

	dev_dbg(chan->device->common.dev, "intr cause %x\n", intr_cause);

	if (mv_is_err_intr(intr_cause))
		mv_xor_err_interrupt_handler(chan, intr_cause);

	tasklet_schedule(&chan->irq_tasklet);

	mv_xor_device_clear_eoc_cause(chan);

	return IRQ_HANDLED;
}

static void mv_xor_issue_pending(struct dma_chan *chan)
{
	struct mv_xor_chan *mv_chan = to_mv_xor_chan(chan);

	if (mv_chan->pending >= MV_XOR_THRESHOLD) {
		mv_chan->pending = 0;
		mv_chan_activate(mv_chan);
	}
}

/*
 * Perform a transaction to verify the HW works.
 */
#define MV_XOR_TEST_SIZE 2000

static int __devinit mv_xor_memcpy_self_test(struct mv_xor_device *device)
{
	int i;
	void *src, *dest;
	dma_addr_t src_dma, dest_dma;
	struct dma_chan *dma_chan;
	dma_cookie_t cookie;
	struct dma_async_tx_descriptor *tx;
	int err = 0;
	struct mv_xor_chan *mv_chan;

	src = kmalloc(sizeof(u8) * MV_XOR_TEST_SIZE, GFP_KERNEL);
	if (!src)
		return -ENOMEM;

	dest = kzalloc(sizeof(u8) * MV_XOR_TEST_SIZE, GFP_KERNEL);
	if (!dest) {
		kfree(src);
		return -ENOMEM;
	}

	/* Fill in src buffer */
	for (i = 0; i < MV_XOR_TEST_SIZE; i++)
		((u8 *) src)[i] = (u8)i;

	/* Start copy, using first DMA channel */
	dma_chan = container_of(device->common.channels.next,
				struct dma_chan,
				device_node);
	if (mv_xor_alloc_chan_resources(dma_chan) < 1) {
		err = -ENODEV;
		goto out;
	}

	dest_dma = dma_map_single(dma_chan->device->dev, dest,
				  MV_XOR_TEST_SIZE, DMA_FROM_DEVICE);

	src_dma = dma_map_single(dma_chan->device->dev, src,
				 MV_XOR_TEST_SIZE, DMA_TO_DEVICE);

	tx = mv_xor_prep_dma_memcpy(dma_chan, dest_dma, src_dma,
				    MV_XOR_TEST_SIZE, 0);
	cookie = mv_xor_tx_submit(tx);
	mv_xor_issue_pending(dma_chan);
	async_tx_ack(tx);
	msleep(1);

	if (mv_xor_status(dma_chan, cookie, NULL) !=
	    DMA_SUCCESS) {
		dev_err(dma_chan->device->dev,
			"Self-test copy timed out, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

	mv_chan = to_mv_xor_chan(dma_chan);
	dma_sync_single_for_cpu(&mv_chan->device->pdev->dev, dest_dma,
				MV_XOR_TEST_SIZE, DMA_FROM_DEVICE);
	if (memcmp(src, dest, MV_XOR_TEST_SIZE)) {
		dev_err(dma_chan->device->dev,
			"Self-test copy failed compare, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

free_resources:
	mv_xor_free_chan_resources(dma_chan);
out:
	kfree(src);
	kfree(dest);
	return err;
}

#define MV_XOR_NUM_SRC_TEST 4 /* must be <= 15 */
static int __devinit
mv_xor_xor_self_test(struct mv_xor_device *device)
{
	int i, src_idx;
	struct page *dest;
	struct page *xor_srcs[MV_XOR_NUM_SRC_TEST];
	dma_addr_t dma_srcs[MV_XOR_NUM_SRC_TEST];
	dma_addr_t dest_dma;
	struct dma_async_tx_descriptor *tx;
	struct dma_chan *dma_chan;
	dma_cookie_t cookie;
	u8 cmp_byte = 0;
	u32 cmp_word;
	int err = 0;
	struct mv_xor_chan *mv_chan;

	for (src_idx = 0; src_idx < MV_XOR_NUM_SRC_TEST; src_idx++) {
		xor_srcs[src_idx] = alloc_page(GFP_KERNEL);
		if (!xor_srcs[src_idx]) {
			while (src_idx--)
				__free_page(xor_srcs[src_idx]);
			return -ENOMEM;
		}
	}

	dest = alloc_page(GFP_KERNEL);
	if (!dest) {
		while (src_idx--)
			__free_page(xor_srcs[src_idx]);
		return -ENOMEM;
	}

	/* Fill in src buffers */
	for (src_idx = 0; src_idx < MV_XOR_NUM_SRC_TEST; src_idx++) {
		u8 *ptr = page_address(xor_srcs[src_idx]);
		for (i = 0; i < PAGE_SIZE; i++)
			ptr[i] = (1 << src_idx);
	}

	for (src_idx = 0; src_idx < MV_XOR_NUM_SRC_TEST; src_idx++)
		cmp_byte ^= (u8) (1 << src_idx);

	cmp_word = (cmp_byte << 24) | (cmp_byte << 16) |
		(cmp_byte << 8) | cmp_byte;

	memset(page_address(dest), 0, PAGE_SIZE);

	dma_chan = container_of(device->common.channels.next,
				struct dma_chan,
				device_node);
	if (mv_xor_alloc_chan_resources(dma_chan) < 1) {
		err = -ENODEV;
		goto out;
	}

	/* test xor */
	dest_dma = dma_map_page(dma_chan->device->dev, dest, 0, PAGE_SIZE,
				DMA_FROM_DEVICE);

	for (i = 0; i < MV_XOR_NUM_SRC_TEST; i++)
		dma_srcs[i] = dma_map_page(dma_chan->device->dev, xor_srcs[i],
					   0, PAGE_SIZE, DMA_TO_DEVICE);

	tx = mv_xor_prep_dma_xor(dma_chan, dest_dma, dma_srcs,
				 MV_XOR_NUM_SRC_TEST, PAGE_SIZE, 0);

	cookie = mv_xor_tx_submit(tx);
	mv_xor_issue_pending(dma_chan);
	async_tx_ack(tx);
	msleep(8);

	if (mv_xor_status(dma_chan, cookie, NULL) !=
	    DMA_SUCCESS) {
		dev_err(dma_chan->device->dev,
			"Self-test xor timed out, disabling\n");
		err = -ENODEV;
		goto free_resources;
	}

	mv_chan = to_mv_xor_chan(dma_chan);
	dma_sync_single_for_cpu(&mv_chan->device->pdev->dev, dest_dma,
				PAGE_SIZE, DMA_FROM_DEVICE);
	for (i = 0; i < (PAGE_SIZE / sizeof(u32)); i++) {
		u32 *ptr = page_address(dest);
		if (ptr[i] != cmp_word) {
			dev_err(dma_chan->device->dev,
				"Self-test xor failed compare, disabling."
				" index %d, data %x, expected %x\n", i,
				ptr[i], cmp_word);
			err = -ENODEV;
			goto free_resources;
		}
	}

free_resources:
	mv_xor_free_chan_resources(dma_chan);
out:
	src_idx = MV_XOR_NUM_SRC_TEST;
	while (src_idx--)
		__free_page(xor_srcs[src_idx]);
	__free_page(dest);
	return err;
}

static int mv_xor_channel_remove(struct mv_xor_device *device)
{
	struct dma_chan *chan, *_chan;
	struct mv_xor_chan *mv_chan;

	dma_async_device_unregister(&device->common);

	dma_free_coherent(&device->pdev->dev, device->pool_size,
			  device->dma_desc_pool_virt, device->dma_desc_pool);

	list_for_each_entry_safe(chan, _chan, &device->common.channels,
				 device_node) {
		mv_chan = to_mv_xor_chan(chan);
		list_del(&chan->device_node);
	}

	return 0;
}

static struct mv_xor_device *
mv_xor_channel_add(struct mv_xor_private *msp,
		   struct platform_device *pdev,
		   int hw_id, dma_cap_mask_t cap_mask,
		   size_t pool_size, int irq)
{
	int ret = 0;
	struct mv_xor_device *adev;
	struct mv_xor_chan *mv_chan;
	struct dma_device *dma_dev;

	adev = devm_kzalloc(&pdev->dev, sizeof(*adev), GFP_KERNEL);
	if (!adev)
		return ERR_PTR(-ENOMEM);

	dma_dev = &adev->common;

	/* allocate coherent memory for hardware descriptors
	 * note: writecombine gives slightly better performance, but
	 * requires that we explicitly flush the writes
	 */
	adev->pool_size = pool_size;
	adev->dma_desc_pool_virt = dma_alloc_writecombine(&pdev->dev,
							  adev->pool_size,
							  &adev->dma_desc_pool,
							  GFP_KERNEL);
	if (!adev->dma_desc_pool_virt)
		return ERR_PTR(-ENOMEM);

	adev->id = hw_id;

	/* discover transaction capabilites from the platform data */
	dma_dev->cap_mask = cap_mask;
	adev->pdev = pdev;
	adev->shared = msp;

	INIT_LIST_HEAD(&dma_dev->channels);

	/* set base routines */
	dma_dev->device_alloc_chan_resources = mv_xor_alloc_chan_resources;
	dma_dev->device_free_chan_resources = mv_xor_free_chan_resources;
	dma_dev->device_tx_status = mv_xor_status;
	dma_dev->device_issue_pending = mv_xor_issue_pending;
	dma_dev->dev = &pdev->dev;

	/* set prep routines based on capability */
	if (dma_has_cap(DMA_MEMCPY, dma_dev->cap_mask))
		dma_dev->device_prep_dma_memcpy = mv_xor_prep_dma_memcpy;
	if (dma_has_cap(DMA_MEMSET, dma_dev->cap_mask))
		dma_dev->device_prep_dma_memset = mv_xor_prep_dma_memset;
	if (dma_has_cap(DMA_XOR, dma_dev->cap_mask)) {
		dma_dev->max_xor = 8;
		dma_dev->device_prep_dma_xor = mv_xor_prep_dma_xor;
	}

	mv_chan = devm_kzalloc(&pdev->dev, sizeof(*mv_chan), GFP_KERNEL);
	if (!mv_chan) {
		ret = -ENOMEM;
		goto err_free_dma;
	}
	mv_chan->device = adev;
	mv_chan->idx = hw_id;
	mv_chan->mmr_base = adev->shared->xor_base;

	if (!mv_chan->mmr_base) {
		ret = -ENOMEM;
		goto err_free_dma;
	}
	tasklet_init(&mv_chan->irq_tasklet, mv_xor_tasklet, (unsigned long)
		     mv_chan);

	/* clear errors before enabling interrupts */
	mv_xor_device_clear_err_status(mv_chan);

	ret = devm_request_irq(&pdev->dev, irq,
			       mv_xor_interrupt_handler,
			       0, dev_name(&pdev->dev), mv_chan);
	if (ret)
		goto err_free_dma;

	mv_chan_unmask_interrupts(mv_chan);

	mv_set_mode(mv_chan, DMA_MEMCPY);

	spin_lock_init(&mv_chan->lock);
	INIT_LIST_HEAD(&mv_chan->chain);
	INIT_LIST_HEAD(&mv_chan->completed_slots);
	INIT_LIST_HEAD(&mv_chan->all_slots);
	mv_chan->common.device = dma_dev;
	dma_cookie_init(&mv_chan->common);

	list_add_tail(&mv_chan->common.device_node, &dma_dev->channels);

	if (dma_has_cap(DMA_MEMCPY, dma_dev->cap_mask)) {
		ret = mv_xor_memcpy_self_test(adev);
		dev_dbg(&pdev->dev, "memcpy self test returned %d\n", ret);
		if (ret)
			goto err_free_dma;
	}

	if (dma_has_cap(DMA_XOR, dma_dev->cap_mask)) {
		ret = mv_xor_xor_self_test(adev);
		dev_dbg(&pdev->dev, "xor self test returned %d\n", ret);
		if (ret)
			goto err_free_dma;
	}

	dev_info(&pdev->dev, "Marvell XOR: "
	  "( %s%s%s%s)\n",
	  dma_has_cap(DMA_XOR, dma_dev->cap_mask) ? "xor " : "",
	  dma_has_cap(DMA_MEMSET, dma_dev->cap_mask)  ? "fill " : "",
	  dma_has_cap(DMA_MEMCPY, dma_dev->cap_mask) ? "cpy " : "",
	  dma_has_cap(DMA_INTERRUPT, dma_dev->cap_mask) ? "intr " : "");

	dma_async_device_register(dma_dev);
	return adev;

 err_free_dma:
	dma_free_coherent(&adev->pdev->dev, pool_size,
			adev->dma_desc_pool_virt, adev->dma_desc_pool);
	return ERR_PTR(ret);
}

static void
mv_xor_conf_mbus_windows(struct mv_xor_private *msp,
			 const struct mbus_dram_target_info *dram)
{
	void __iomem *base = msp->xor_base;
	u32 win_enable = 0;
	int i;

	for (i = 0; i < 8; i++) {
		writel(0, base + WINDOW_BASE(i));
		writel(0, base + WINDOW_SIZE(i));
		if (i < 4)
			writel(0, base + WINDOW_REMAP_HIGH(i));
	}

	for (i = 0; i < dram->num_cs; i++) {
		const struct mbus_dram_window *cs = dram->cs + i;

		writel((cs->base & 0xffff0000) |
		       (cs->mbus_attr << 8) |
		       dram->mbus_dram_target_id, base + WINDOW_BASE(i));
		writel((cs->size - 1) & 0xffff0000, base + WINDOW_SIZE(i));

		win_enable |= (1 << i);
		win_enable |= 3 << (16 + (2 * i));
	}

	writel(win_enable, base + WINDOW_BAR_ENABLE(0));
	writel(win_enable, base + WINDOW_BAR_ENABLE(1));
}

static int mv_xor_probe(struct platform_device *pdev)
{
	const struct mbus_dram_target_info *dram;
	struct mv_xor_private *msp;
	struct mv_xor_platform_data *pdata = pdev->dev.platform_data;
	struct resource *res;
	int i, ret;

	dev_notice(&pdev->dev, "Marvell XOR driver\n");

	msp = devm_kzalloc(&pdev->dev, sizeof(*msp), GFP_KERNEL);
	if (!msp)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	msp->xor_base = devm_ioremap(&pdev->dev, res->start,
				     resource_size(res));
	if (!msp->xor_base)
		return -EBUSY;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return -ENODEV;

	msp->xor_high_base = devm_ioremap(&pdev->dev, res->start,
					  resource_size(res));
	if (!msp->xor_high_base)
		return -EBUSY;

	platform_set_drvdata(pdev, msp);

	/*
	 * (Re-)program MBUS remapping windows if we are asked to.
	 */
	dram = mv_mbus_dram_info();
	if (dram)
		mv_xor_conf_mbus_windows(msp, dram);

	/* Not all platforms can gate the clock, so it is not
	 * an error if the clock does not exists.
	 */
	msp->clk = clk_get(&pdev->dev, NULL);
	if (!IS_ERR(msp->clk))
		clk_prepare_enable(msp->clk);

	if (pdata && pdata->channels) {
		for (i = 0; i < MV_XOR_MAX_CHANNELS; i++) {
			struct mv_xor_channel_data *cd;
			int irq;

			cd = &pdata->channels[i];
			if (!cd) {
				ret = -ENODEV;
				goto err_channel_add;
			}

			irq = platform_get_irq(pdev, i);
			if (irq < 0) {
				ret = irq;
				goto err_channel_add;
			}

			msp->channels[i] =
				mv_xor_channel_add(msp, pdev, cd->hw_id,
						   cd->cap_mask,
						   cd->pool_size, irq);
			if (IS_ERR(msp->channels[i])) {
				ret = PTR_ERR(msp->channels[i]);
				goto err_channel_add;
			}
		}
	}

	return 0;

err_channel_add:
	for (i = 0; i < MV_XOR_MAX_CHANNELS; i++)
		if (msp->channels[i])
			mv_xor_channel_remove(msp->channels[i]);

	clk_disable_unprepare(msp->clk);
	clk_put(msp->clk);
	return ret;
}

static int mv_xor_remove(struct platform_device *pdev)
{
	struct mv_xor_private *msp = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < MV_XOR_MAX_CHANNELS; i++) {
		if (msp->channels[i])
			mv_xor_channel_remove(msp->channels[i]);
	}

	if (!IS_ERR(msp->clk)) {
		clk_disable_unprepare(msp->clk);
		clk_put(msp->clk);
	}

	return 0;
}

static struct platform_driver mv_xor_driver = {
	.probe		= mv_xor_probe,
	.remove		= mv_xor_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= MV_XOR_NAME,
	},
};


static int __init mv_xor_init(void)
{
	return platform_driver_register(&mv_xor_driver);
}
module_init(mv_xor_init);

/* it's currently unsafe to unload this module */
#if 0
static void __exit mv_xor_exit(void)
{
	platform_driver_unregister(&mv_xor_driver);
	return;
}

module_exit(mv_xor_exit);
#endif

MODULE_AUTHOR("Saeed Bishara <saeed@marvell.com>");
MODULE_DESCRIPTION("DMA engine driver for Marvell's XOR engine");
MODULE_LICENSE("GPL");
