// SPDX-License-Identifier: GPL-2.0
/*
 * Intel(R) Trace Hub Memory Storage Unit
 *
 * Copyright (C) 2014-2015 Intel Corporation.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/sizes.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>

#ifdef CONFIG_X86
#include <asm/set_memory.h>
#endif

#include "intel_th.h"
#include "msu.h"

#define msc_dev(x) (&(x)->thdev->dev)

/**
 * struct msc_block - multiblock mode block descriptor
 * @bdesc:	pointer to hardware descriptor (beginning of the block)
 * @addr:	physical address of the block
 */
struct msc_block {
	struct msc_block_desc	*bdesc;
	dma_addr_t		addr;
};

/**
 * struct msc_window - multiblock mode window descriptor
 * @entry:	window list linkage (msc::win_list)
 * @pgoff:	page offset into the buffer that this window starts at
 * @nr_blocks:	number of blocks (pages) in this window
 * @block:	array of block descriptors
 */
struct msc_window {
	struct list_head	entry;
	unsigned long		pgoff;
	unsigned int		nr_blocks;
	struct msc		*msc;
	struct msc_block	block[0];
};

/**
 * struct msc_iter - iterator for msc buffer
 * @entry:		msc::iter_list linkage
 * @msc:		pointer to the MSC device
 * @start_win:		oldest window
 * @win:		current window
 * @offset:		current logical offset into the buffer
 * @start_block:	oldest block in the window
 * @block:		block number in the window
 * @block_off:		offset into current block
 * @wrap_count:		block wrapping handling
 * @eof:		end of buffer reached
 */
struct msc_iter {
	struct list_head	entry;
	struct msc		*msc;
	struct msc_window	*start_win;
	struct msc_window	*win;
	unsigned long		offset;
	int			start_block;
	int			block;
	unsigned int		block_off;
	unsigned int		wrap_count;
	unsigned int		eof;
};

/**
 * struct msc - MSC device representation
 * @reg_base:		register window base address
 * @thdev:		intel_th_device pointer
 * @win_list:		list of windows in multiblock mode
 * @single_sgt:		single mode buffer
 * @nr_pages:		total number of pages allocated for this buffer
 * @single_sz:		amount of data in single mode
 * @single_wrap:	single mode wrap occurred
 * @base:		buffer's base pointer
 * @base_addr:		buffer's base address
 * @user_count:		number of users of the buffer
 * @mmap_count:		number of mappings
 * @buf_mutex:		mutex to serialize access to buffer-related bits

 * @enabled:		MSC is enabled
 * @wrap:		wrapping is enabled
 * @mode:		MSC operating mode
 * @burst_len:		write burst length
 * @index:		number of this MSC in the MSU
 */
struct msc {
	void __iomem		*reg_base;
	struct intel_th_device	*thdev;

	struct list_head	win_list;
	struct sg_table		single_sgt;
	unsigned long		nr_pages;
	unsigned long		single_sz;
	unsigned int		single_wrap : 1;
	void			*base;
	dma_addr_t		base_addr;

	/* <0: no buffer, 0: no users, >0: active users */
	atomic_t		user_count;

	atomic_t		mmap_count;
	struct mutex		buf_mutex;

	struct list_head	iter_list;

	/* config */
	unsigned int		enabled : 1,
				wrap	: 1;
	unsigned int		mode;
	unsigned int		burst_len;
	unsigned int		index;
};

static inline bool msc_block_is_empty(struct msc_block_desc *bdesc)
{
	/* header hasn't been written */
	if (!bdesc->valid_dw)
		return true;

	/* valid_dw includes the header */
	if (!msc_data_sz(bdesc))
		return true;

	return false;
}

/**
 * msc_oldest_window() - locate the window with oldest data
 * @msc:	MSC device
 *
 * This should only be used in multiblock mode. Caller should hold the
 * msc::user_count reference.
 *
 * Return:	the oldest window with valid data
 */
static struct msc_window *msc_oldest_window(struct msc *msc)
{
	struct msc_window *win;
	u32 reg = ioread32(msc->reg_base + REG_MSU_MSC0NWSA);
	unsigned long win_addr = (unsigned long)reg << PAGE_SHIFT;
	unsigned int found = 0;

	if (list_empty(&msc->win_list))
		return NULL;

	/*
	 * we might need a radix tree for this, depending on how
	 * many windows a typical user would allocate; ideally it's
	 * something like 2, in which case we're good
	 */
	list_for_each_entry(win, &msc->win_list, entry) {
		if (win->block[0].addr == win_addr)
			found++;

		/* skip the empty ones */
		if (msc_block_is_empty(win->block[0].bdesc))
			continue;

		if (found)
			return win;
	}

	return list_entry(msc->win_list.next, struct msc_window, entry);
}

/**
 * msc_win_oldest_block() - locate the oldest block in a given window
 * @win:	window to look at
 *
 * Return:	index of the block with the oldest data
 */
static unsigned int msc_win_oldest_block(struct msc_window *win)
{
	unsigned int blk;
	struct msc_block_desc *bdesc = win->block[0].bdesc;

	/* without wrapping, first block is the oldest */
	if (!msc_block_wrapped(bdesc))
		return 0;

	/*
	 * with wrapping, last written block contains both the newest and the
	 * oldest data for this window.
	 */
	for (blk = 0; blk < win->nr_blocks; blk++) {
		bdesc = win->block[blk].bdesc;

		if (msc_block_last_written(bdesc))
			return blk;
	}

	return 0;
}

/**
 * msc_is_last_win() - check if a window is the last one for a given MSC
 * @win:	window
 * Return:	true if @win is the last window in MSC's multiblock buffer
 */
static inline bool msc_is_last_win(struct msc_window *win)
{
	return win->entry.next == &win->msc->win_list;
}

/**
 * msc_next_window() - return next window in the multiblock buffer
 * @win:	current window
 *
 * Return:	window following the current one
 */
static struct msc_window *msc_next_window(struct msc_window *win)
{
	if (msc_is_last_win(win))
		return list_entry(win->msc->win_list.next, struct msc_window,
				  entry);

	return list_entry(win->entry.next, struct msc_window, entry);
}

static struct msc_block_desc *msc_iter_bdesc(struct msc_iter *iter)
{
	return iter->win->block[iter->block].bdesc;
}

static void msc_iter_init(struct msc_iter *iter)
{
	memset(iter, 0, sizeof(*iter));
	iter->start_block = -1;
	iter->block = -1;
}

static struct msc_iter *msc_iter_install(struct msc *msc)
{
	struct msc_iter *iter;

	iter = kzalloc(sizeof(*iter), GFP_KERNEL);
	if (!iter)
		return ERR_PTR(-ENOMEM);

	mutex_lock(&msc->buf_mutex);

	/*
	 * Reading and tracing are mutually exclusive; if msc is
	 * enabled, open() will fail; otherwise existing readers
	 * will prevent enabling the msc and the rest of fops don't
	 * need to worry about it.
	 */
	if (msc->enabled) {
		kfree(iter);
		iter = ERR_PTR(-EBUSY);
		goto unlock;
	}

	msc_iter_init(iter);
	iter->msc = msc;

	list_add_tail(&iter->entry, &msc->iter_list);
unlock:
	mutex_unlock(&msc->buf_mutex);

	return iter;
}

static void msc_iter_remove(struct msc_iter *iter, struct msc *msc)
{
	mutex_lock(&msc->buf_mutex);
	list_del(&iter->entry);
	mutex_unlock(&msc->buf_mutex);

	kfree(iter);
}

static void msc_iter_block_start(struct msc_iter *iter)
{
	if (iter->start_block != -1)
		return;

	iter->start_block = msc_win_oldest_block(iter->win);
	iter->block = iter->start_block;
	iter->wrap_count = 0;

	/*
	 * start with the block with oldest data; if data has wrapped
	 * in this window, it should be in this block
	 */
	if (msc_block_wrapped(msc_iter_bdesc(iter)))
		iter->wrap_count = 2;

}

static int msc_iter_win_start(struct msc_iter *iter, struct msc *msc)
{
	/* already started, nothing to do */
	if (iter->start_win)
		return 0;

	iter->start_win = msc_oldest_window(msc);
	if (!iter->start_win)
		return -EINVAL;

	iter->win = iter->start_win;
	iter->start_block = -1;

	msc_iter_block_start(iter);

	return 0;
}

static int msc_iter_win_advance(struct msc_iter *iter)
{
	iter->win = msc_next_window(iter->win);
	iter->start_block = -1;

	if (iter->win == iter->start_win) {
		iter->eof++;
		return 1;
	}

	msc_iter_block_start(iter);

	return 0;
}

static int msc_iter_block_advance(struct msc_iter *iter)
{
	iter->block_off = 0;

	/* wrapping */
	if (iter->wrap_count && iter->block == iter->start_block) {
		iter->wrap_count--;
		if (!iter->wrap_count)
			/* copied newest data from the wrapped block */
			return msc_iter_win_advance(iter);
	}

	/* no wrapping, check for last written block */
	if (!iter->wrap_count && msc_block_last_written(msc_iter_bdesc(iter)))
		/* copied newest data for the window */
		return msc_iter_win_advance(iter);

	/* block advance */
	if (++iter->block == iter->win->nr_blocks)
		iter->block = 0;

	/* no wrapping, sanity check in case there is no last written block */
	if (!iter->wrap_count && iter->block == iter->start_block)
		return msc_iter_win_advance(iter);

	return 0;
}

/**
 * msc_buffer_iterate() - go through multiblock buffer's data
 * @iter:	iterator structure
 * @size:	amount of data to scan
 * @data:	callback's private data
 * @fn:		iterator callback
 *
 * This will start at the window which will be written to next (containing
 * the oldest data) and work its way to the current window, calling @fn
 * for each chunk of data as it goes.
 *
 * Caller should have msc::user_count reference to make sure the buffer
 * doesn't disappear from under us.
 *
 * Return:	amount of data actually scanned.
 */
static ssize_t
msc_buffer_iterate(struct msc_iter *iter, size_t size, void *data,
		   unsigned long (*fn)(void *, void *, size_t))
{
	struct msc *msc = iter->msc;
	size_t len = size;
	unsigned int advance;

	if (iter->eof)
		return 0;

	/* start with the oldest window */
	if (msc_iter_win_start(iter, msc))
		return 0;

	do {
		unsigned long data_bytes = msc_data_sz(msc_iter_bdesc(iter));
		void *src = (void *)msc_iter_bdesc(iter) + MSC_BDESC;
		size_t tocopy = data_bytes, copied = 0;
		size_t remaining = 0;

		advance = 1;

		/*
		 * If block wrapping happened, we need to visit the last block
		 * twice, because it contains both the oldest and the newest
		 * data in this window.
		 *
		 * First time (wrap_count==2), in the very beginning, to collect
		 * the oldest data, which is in the range
		 * (data_bytes..DATA_IN_PAGE).
		 *
		 * Second time (wrap_count==1), it's just like any other block,
		 * containing data in the range of [MSC_BDESC..data_bytes].
		 */
		if (iter->block == iter->start_block && iter->wrap_count == 2) {
			tocopy = DATA_IN_PAGE - data_bytes;
			src += data_bytes;
		}

		if (!tocopy)
			goto next_block;

		tocopy -= iter->block_off;
		src += iter->block_off;

		if (len < tocopy) {
			tocopy = len;
			advance = 0;
		}

		remaining = fn(data, src, tocopy);

		if (remaining)
			advance = 0;

		copied = tocopy - remaining;
		len -= copied;
		iter->block_off += copied;
		iter->offset += copied;

		if (!advance)
			break;

next_block:
		if (msc_iter_block_advance(iter))
			break;

	} while (len);

	return size - len;
}

/**
 * msc_buffer_clear_hw_header() - clear hw header for multiblock
 * @msc:	MSC device
 */
static void msc_buffer_clear_hw_header(struct msc *msc)
{
	struct msc_window *win;

	list_for_each_entry(win, &msc->win_list, entry) {
		unsigned int blk;
		size_t hw_sz = sizeof(struct msc_block_desc) -
			offsetof(struct msc_block_desc, hw_tag);

		for (blk = 0; blk < win->nr_blocks; blk++) {
			struct msc_block_desc *bdesc = win->block[blk].bdesc;

			memset(&bdesc->hw_tag, 0, hw_sz);
		}
	}
}

/**
 * msc_configure() - set up MSC hardware
 * @msc:	the MSC device to configure
 *
 * Program storage mode, wrapping, burst length and trace buffer address
 * into a given MSC. Then, enable tracing and set msc::enabled.
 * The latter is serialized on msc::buf_mutex, so make sure to hold it.
 */
static int msc_configure(struct msc *msc)
{
	u32 reg;

	lockdep_assert_held(&msc->buf_mutex);

	if (msc->mode > MSC_MODE_MULTI)
		return -EINVAL;

	if (msc->mode == MSC_MODE_MULTI)
		msc_buffer_clear_hw_header(msc);

	reg = msc->base_addr >> PAGE_SHIFT;
	iowrite32(reg, msc->reg_base + REG_MSU_MSC0BAR);

	if (msc->mode == MSC_MODE_SINGLE) {
		reg = msc->nr_pages;
		iowrite32(reg, msc->reg_base + REG_MSU_MSC0SIZE);
	}

	reg = ioread32(msc->reg_base + REG_MSU_MSC0CTL);
	reg &= ~(MSC_MODE | MSC_WRAPEN | MSC_EN | MSC_RD_HDR_OVRD);

	reg |= MSC_EN;
	reg |= msc->mode << __ffs(MSC_MODE);
	reg |= msc->burst_len << __ffs(MSC_LEN);

	if (msc->wrap)
		reg |= MSC_WRAPEN;

	iowrite32(reg, msc->reg_base + REG_MSU_MSC0CTL);

	msc->thdev->output.multiblock = msc->mode == MSC_MODE_MULTI;
	intel_th_trace_enable(msc->thdev);
	msc->enabled = 1;


	return 0;
}

/**
 * msc_disable() - disable MSC hardware
 * @msc:	MSC device to disable
 *
 * If @msc is enabled, disable tracing on the switch and then disable MSC
 * storage. Caller must hold msc::buf_mutex.
 */
static void msc_disable(struct msc *msc)
{
	unsigned long count;
	u32 reg;

	lockdep_assert_held(&msc->buf_mutex);

	intel_th_trace_disable(msc->thdev);

	for (reg = 0, count = MSC_PLE_WAITLOOP_DEPTH;
	     count && !(reg & MSCSTS_PLE); count--) {
		reg = ioread32(msc->reg_base + REG_MSU_MSC0STS);
		cpu_relax();
	}

	if (!count)
		dev_dbg(msc_dev(msc), "timeout waiting for MSC0 PLE\n");

	if (msc->mode == MSC_MODE_SINGLE) {
		msc->single_wrap = !!(reg & MSCSTS_WRAPSTAT);

		reg = ioread32(msc->reg_base + REG_MSU_MSC0MWP);
		msc->single_sz = reg & ((msc->nr_pages << PAGE_SHIFT) - 1);
		dev_dbg(msc_dev(msc), "MSCnMWP: %08x/%08lx, wrap: %d\n",
			reg, msc->single_sz, msc->single_wrap);
	}

	reg = ioread32(msc->reg_base + REG_MSU_MSC0CTL);
	reg &= ~MSC_EN;
	iowrite32(reg, msc->reg_base + REG_MSU_MSC0CTL);
	msc->enabled = 0;

	iowrite32(0, msc->reg_base + REG_MSU_MSC0BAR);
	iowrite32(0, msc->reg_base + REG_MSU_MSC0SIZE);

	dev_dbg(msc_dev(msc), "MSCnNWSA: %08x\n",
		ioread32(msc->reg_base + REG_MSU_MSC0NWSA));

	reg = ioread32(msc->reg_base + REG_MSU_MSC0STS);
	dev_dbg(msc_dev(msc), "MSCnSTS: %08x\n", reg);
}

static int intel_th_msc_activate(struct intel_th_device *thdev)
{
	struct msc *msc = dev_get_drvdata(&thdev->dev);
	int ret = -EBUSY;

	if (!atomic_inc_unless_negative(&msc->user_count))
		return -ENODEV;

	mutex_lock(&msc->buf_mutex);

	/* if there are readers, refuse */
	if (list_empty(&msc->iter_list))
		ret = msc_configure(msc);

	mutex_unlock(&msc->buf_mutex);

	if (ret)
		atomic_dec(&msc->user_count);

	return ret;
}

static void intel_th_msc_deactivate(struct intel_th_device *thdev)
{
	struct msc *msc = dev_get_drvdata(&thdev->dev);

	mutex_lock(&msc->buf_mutex);
	if (msc->enabled) {
		msc_disable(msc);
		atomic_dec(&msc->user_count);
	}
	mutex_unlock(&msc->buf_mutex);
}

/**
 * msc_buffer_contig_alloc() - allocate a contiguous buffer for SINGLE mode
 * @msc:	MSC device
 * @size:	allocation size in bytes
 *
 * This modifies msc::base, which requires msc::buf_mutex to serialize, so the
 * caller is expected to hold it.
 *
 * Return:	0 on success, -errno otherwise.
 */
static int msc_buffer_contig_alloc(struct msc *msc, unsigned long size)
{
	unsigned long nr_pages = size >> PAGE_SHIFT;
	unsigned int order = get_order(size);
	struct page *page;
	int ret;

	if (!size)
		return 0;

	ret = sg_alloc_table(&msc->single_sgt, 1, GFP_KERNEL);
	if (ret)
		goto err_out;

	ret = -ENOMEM;
	page = alloc_pages(GFP_KERNEL | __GFP_ZERO | GFP_DMA32, order);
	if (!page)
		goto err_free_sgt;

	split_page(page, order);
	sg_set_buf(msc->single_sgt.sgl, page_address(page), size);

	ret = dma_map_sg(msc_dev(msc)->parent->parent, msc->single_sgt.sgl, 1,
			 DMA_FROM_DEVICE);
	if (ret < 0)
		goto err_free_pages;

	msc->nr_pages = nr_pages;
	msc->base = page_address(page);
	msc->base_addr = sg_dma_address(msc->single_sgt.sgl);

	return 0;

err_free_pages:
	__free_pages(page, order);

err_free_sgt:
	sg_free_table(&msc->single_sgt);

err_out:
	return ret;
}

/**
 * msc_buffer_contig_free() - free a contiguous buffer
 * @msc:	MSC configured in SINGLE mode
 */
static void msc_buffer_contig_free(struct msc *msc)
{
	unsigned long off;

	dma_unmap_sg(msc_dev(msc)->parent->parent, msc->single_sgt.sgl,
		     1, DMA_FROM_DEVICE);
	sg_free_table(&msc->single_sgt);

	for (off = 0; off < msc->nr_pages << PAGE_SHIFT; off += PAGE_SIZE) {
		struct page *page = virt_to_page(msc->base + off);

		page->mapping = NULL;
		__free_page(page);
	}

	msc->nr_pages = 0;
}

/**
 * msc_buffer_contig_get_page() - find a page at a given offset
 * @msc:	MSC configured in SINGLE mode
 * @pgoff:	page offset
 *
 * Return:	page, if @pgoff is within the range, NULL otherwise.
 */
static struct page *msc_buffer_contig_get_page(struct msc *msc,
					       unsigned long pgoff)
{
	if (pgoff >= msc->nr_pages)
		return NULL;

	return virt_to_page(msc->base + (pgoff << PAGE_SHIFT));
}

/**
 * msc_buffer_win_alloc() - alloc a window for a multiblock mode
 * @msc:	MSC device
 * @nr_blocks:	number of pages in this window
 *
 * This modifies msc::win_list and msc::base, which requires msc::buf_mutex
 * to serialize, so the caller is expected to hold it.
 *
 * Return:	0 on success, -errno otherwise.
 */
static int msc_buffer_win_alloc(struct msc *msc, unsigned int nr_blocks)
{
	struct msc_window *win;
	unsigned long size = PAGE_SIZE;
	int i, ret = -ENOMEM;

	if (!nr_blocks)
		return 0;

	win = kzalloc(offsetof(struct msc_window, block[nr_blocks]),
		      GFP_KERNEL);
	if (!win)
		return -ENOMEM;

	if (!list_empty(&msc->win_list)) {
		struct msc_window *prev = list_entry(msc->win_list.prev,
						     struct msc_window, entry);

		win->pgoff = prev->pgoff + prev->nr_blocks;
	}

	for (i = 0; i < nr_blocks; i++) {
		win->block[i].bdesc =
			dma_alloc_coherent(msc_dev(msc)->parent->parent, size,
					   &win->block[i].addr, GFP_KERNEL);

		if (!win->block[i].bdesc)
			goto err_nomem;

#ifdef CONFIG_X86
		/* Set the page as uncached */
		set_memory_uc((unsigned long)win->block[i].bdesc, 1);
#endif
	}

	win->msc = msc;
	win->nr_blocks = nr_blocks;

	if (list_empty(&msc->win_list)) {
		msc->base = win->block[0].bdesc;
		msc->base_addr = win->block[0].addr;
	}

	list_add_tail(&win->entry, &msc->win_list);
	msc->nr_pages += nr_blocks;

	return 0;

err_nomem:
	for (i--; i >= 0; i--) {
#ifdef CONFIG_X86
		/* Reset the page to write-back before releasing */
		set_memory_wb((unsigned long)win->block[i].bdesc, 1);
#endif
		dma_free_coherent(msc_dev(msc)->parent->parent, size,
				  win->block[i].bdesc, win->block[i].addr);
	}
	kfree(win);

	return ret;
}

/**
 * msc_buffer_win_free() - free a window from MSC's window list
 * @msc:	MSC device
 * @win:	window to free
 *
 * This modifies msc::win_list and msc::base, which requires msc::buf_mutex
 * to serialize, so the caller is expected to hold it.
 */
static void msc_buffer_win_free(struct msc *msc, struct msc_window *win)
{
	int i;

	msc->nr_pages -= win->nr_blocks;

	list_del(&win->entry);
	if (list_empty(&msc->win_list)) {
		msc->base = NULL;
		msc->base_addr = 0;
	}

	for (i = 0; i < win->nr_blocks; i++) {
		struct page *page = virt_to_page(win->block[i].bdesc);

		page->mapping = NULL;
#ifdef CONFIG_X86
		/* Reset the page to write-back before releasing */
		set_memory_wb((unsigned long)win->block[i].bdesc, 1);
#endif
		dma_free_coherent(msc_dev(win->msc)->parent->parent, PAGE_SIZE,
				  win->block[i].bdesc, win->block[i].addr);
	}

	kfree(win);
}

/**
 * msc_buffer_relink() - set up block descriptors for multiblock mode
 * @msc:	MSC device
 *
 * This traverses msc::win_list, which requires msc::buf_mutex to serialize,
 * so the caller is expected to hold it.
 */
static void msc_buffer_relink(struct msc *msc)
{
	struct msc_window *win, *next_win;

	/* call with msc::mutex locked */
	list_for_each_entry(win, &msc->win_list, entry) {
		unsigned int blk;
		u32 sw_tag = 0;

		/*
		 * Last window's next_win should point to the first window
		 * and MSC_SW_TAG_LASTWIN should be set.
		 */
		if (msc_is_last_win(win)) {
			sw_tag |= MSC_SW_TAG_LASTWIN;
			next_win = list_entry(msc->win_list.next,
					      struct msc_window, entry);
		} else {
			next_win = list_entry(win->entry.next,
					      struct msc_window, entry);
		}

		for (blk = 0; blk < win->nr_blocks; blk++) {
			struct msc_block_desc *bdesc = win->block[blk].bdesc;

			memset(bdesc, 0, sizeof(*bdesc));

			bdesc->next_win = next_win->block[0].addr >> PAGE_SHIFT;

			/*
			 * Similarly to last window, last block should point
			 * to the first one.
			 */
			if (blk == win->nr_blocks - 1) {
				sw_tag |= MSC_SW_TAG_LASTBLK;
				bdesc->next_blk =
					win->block[0].addr >> PAGE_SHIFT;
			} else {
				bdesc->next_blk =
					win->block[blk + 1].addr >> PAGE_SHIFT;
			}

			bdesc->sw_tag = sw_tag;
			bdesc->block_sz = PAGE_SIZE / 64;
		}
	}

	/*
	 * Make the above writes globally visible before tracing is
	 * enabled to make sure hardware sees them coherently.
	 */
	wmb();
}

static void msc_buffer_multi_free(struct msc *msc)
{
	struct msc_window *win, *iter;

	list_for_each_entry_safe(win, iter, &msc->win_list, entry)
		msc_buffer_win_free(msc, win);
}

static int msc_buffer_multi_alloc(struct msc *msc, unsigned long *nr_pages,
				  unsigned int nr_wins)
{
	int ret, i;

	for (i = 0; i < nr_wins; i++) {
		ret = msc_buffer_win_alloc(msc, nr_pages[i]);
		if (ret) {
			msc_buffer_multi_free(msc);
			return ret;
		}
	}

	msc_buffer_relink(msc);

	return 0;
}

/**
 * msc_buffer_free() - free buffers for MSC
 * @msc:	MSC device
 *
 * Free MSC's storage buffers.
 *
 * This modifies msc::win_list and msc::base, which requires msc::buf_mutex to
 * serialize, so the caller is expected to hold it.
 */
static void msc_buffer_free(struct msc *msc)
{
	if (msc->mode == MSC_MODE_SINGLE)
		msc_buffer_contig_free(msc);
	else if (msc->mode == MSC_MODE_MULTI)
		msc_buffer_multi_free(msc);
}

/**
 * msc_buffer_alloc() - allocate a buffer for MSC
 * @msc:	MSC device
 * @size:	allocation size in bytes
 *
 * Allocate a storage buffer for MSC, depending on the msc::mode, it will be
 * either done via msc_buffer_contig_alloc() for SINGLE operation mode or
 * msc_buffer_win_alloc() for multiblock operation. The latter allocates one
 * window per invocation, so in multiblock mode this can be called multiple
 * times for the same MSC to allocate multiple windows.
 *
 * This modifies msc::win_list and msc::base, which requires msc::buf_mutex
 * to serialize, so the caller is expected to hold it.
 *
 * Return:	0 on success, -errno otherwise.
 */
static int msc_buffer_alloc(struct msc *msc, unsigned long *nr_pages,
			    unsigned int nr_wins)
{
	int ret;

	/* -1: buffer not allocated */
	if (atomic_read(&msc->user_count) != -1)
		return -EBUSY;

	if (msc->mode == MSC_MODE_SINGLE) {
		if (nr_wins != 1)
			return -EINVAL;

		ret = msc_buffer_contig_alloc(msc, nr_pages[0] << PAGE_SHIFT);
	} else if (msc->mode == MSC_MODE_MULTI) {
		ret = msc_buffer_multi_alloc(msc, nr_pages, nr_wins);
	} else {
		ret = -EINVAL;
	}

	if (!ret) {
		/* allocation should be visible before the counter goes to 0 */
		smp_mb__before_atomic();

		if (WARN_ON_ONCE(atomic_cmpxchg(&msc->user_count, -1, 0) != -1))
			return -EINVAL;
	}

	return ret;
}

/**
 * msc_buffer_unlocked_free_unless_used() - free a buffer unless it's in use
 * @msc:	MSC device
 *
 * This will free MSC buffer unless it is in use or there is no allocated
 * buffer.
 * Caller needs to hold msc::buf_mutex.
 *
 * Return:	0 on successful deallocation or if there was no buffer to
 *		deallocate, -EBUSY if there are active users.
 */
static int msc_buffer_unlocked_free_unless_used(struct msc *msc)
{
	int count, ret = 0;

	count = atomic_cmpxchg(&msc->user_count, 0, -1);

	/* > 0: buffer is allocated and has users */
	if (count > 0)
		ret = -EBUSY;
	/* 0: buffer is allocated, no users */
	else if (!count)
		msc_buffer_free(msc);
	/* < 0: no buffer, nothing to do */

	return ret;
}

/**
 * msc_buffer_free_unless_used() - free a buffer unless it's in use
 * @msc:	MSC device
 *
 * This is a locked version of msc_buffer_unlocked_free_unless_used().
 */
static int msc_buffer_free_unless_used(struct msc *msc)
{
	int ret;

	mutex_lock(&msc->buf_mutex);
	ret = msc_buffer_unlocked_free_unless_used(msc);
	mutex_unlock(&msc->buf_mutex);

	return ret;
}

/**
 * msc_buffer_get_page() - get MSC buffer page at a given offset
 * @msc:	MSC device
 * @pgoff:	page offset into the storage buffer
 *
 * This traverses msc::win_list, so holding msc::buf_mutex is expected from
 * the caller.
 *
 * Return:	page if @pgoff corresponds to a valid buffer page or NULL.
 */
static struct page *msc_buffer_get_page(struct msc *msc, unsigned long pgoff)
{
	struct msc_window *win;

	if (msc->mode == MSC_MODE_SINGLE)
		return msc_buffer_contig_get_page(msc, pgoff);

	list_for_each_entry(win, &msc->win_list, entry)
		if (pgoff >= win->pgoff && pgoff < win->pgoff + win->nr_blocks)
			goto found;

	return NULL;

found:
	pgoff -= win->pgoff;
	return virt_to_page(win->block[pgoff].bdesc);
}

/**
 * struct msc_win_to_user_struct - data for copy_to_user() callback
 * @buf:	userspace buffer to copy data to
 * @offset:	running offset
 */
struct msc_win_to_user_struct {
	char __user	*buf;
	unsigned long	offset;
};

/**
 * msc_win_to_user() - iterator for msc_buffer_iterate() to copy data to user
 * @data:	callback's private data
 * @src:	source buffer
 * @len:	amount of data to copy from the source buffer
 */
static unsigned long msc_win_to_user(void *data, void *src, size_t len)
{
	struct msc_win_to_user_struct *u = data;
	unsigned long ret;

	ret = copy_to_user(u->buf + u->offset, src, len);
	u->offset += len - ret;

	return ret;
}


/*
 * file operations' callbacks
 */

static int intel_th_msc_open(struct inode *inode, struct file *file)
{
	struct intel_th_device *thdev = file->private_data;
	struct msc *msc = dev_get_drvdata(&thdev->dev);
	struct msc_iter *iter;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	iter = msc_iter_install(msc);
	if (IS_ERR(iter))
		return PTR_ERR(iter);

	file->private_data = iter;

	return nonseekable_open(inode, file);
}

static int intel_th_msc_release(struct inode *inode, struct file *file)
{
	struct msc_iter *iter = file->private_data;
	struct msc *msc = iter->msc;

	msc_iter_remove(iter, msc);

	return 0;
}

static ssize_t
msc_single_to_user(struct msc *msc, char __user *buf, loff_t off, size_t len)
{
	unsigned long size = msc->nr_pages << PAGE_SHIFT, rem = len;
	unsigned long start = off, tocopy = 0;

	if (msc->single_wrap) {
		start += msc->single_sz;
		if (start < size) {
			tocopy = min(rem, size - start);
			if (copy_to_user(buf, msc->base + start, tocopy))
				return -EFAULT;

			buf += tocopy;
			rem -= tocopy;
			start += tocopy;
		}

		start &= size - 1;
		if (rem) {
			tocopy = min(rem, msc->single_sz - start);
			if (copy_to_user(buf, msc->base + start, tocopy))
				return -EFAULT;

			rem -= tocopy;
		}

		return len - rem;
	}

	if (copy_to_user(buf, msc->base + start, rem))
		return -EFAULT;

	return len;
}

static ssize_t intel_th_msc_read(struct file *file, char __user *buf,
				 size_t len, loff_t *ppos)
{
	struct msc_iter *iter = file->private_data;
	struct msc *msc = iter->msc;
	size_t size;
	loff_t off = *ppos;
	ssize_t ret = 0;

	if (!atomic_inc_unless_negative(&msc->user_count))
		return 0;

	if (msc->mode == MSC_MODE_SINGLE && !msc->single_wrap)
		size = msc->single_sz;
	else
		size = msc->nr_pages << PAGE_SHIFT;

	if (!size)
		goto put_count;

	if (off >= size)
		goto put_count;

	if (off + len >= size)
		len = size - off;

	if (msc->mode == MSC_MODE_SINGLE) {
		ret = msc_single_to_user(msc, buf, off, len);
		if (ret >= 0)
			*ppos += ret;
	} else if (msc->mode == MSC_MODE_MULTI) {
		struct msc_win_to_user_struct u = {
			.buf	= buf,
			.offset	= 0,
		};

		ret = msc_buffer_iterate(iter, len, &u, msc_win_to_user);
		if (ret >= 0)
			*ppos = iter->offset;
	} else {
		ret = -EINVAL;
	}

put_count:
	atomic_dec(&msc->user_count);

	return ret;
}

/*
 * vm operations callbacks (vm_ops)
 */

static void msc_mmap_open(struct vm_area_struct *vma)
{
	struct msc_iter *iter = vma->vm_file->private_data;
	struct msc *msc = iter->msc;

	atomic_inc(&msc->mmap_count);
}

static void msc_mmap_close(struct vm_area_struct *vma)
{
	struct msc_iter *iter = vma->vm_file->private_data;
	struct msc *msc = iter->msc;
	unsigned long pg;

	if (!atomic_dec_and_mutex_lock(&msc->mmap_count, &msc->buf_mutex))
		return;

	/* drop page _refcounts */
	for (pg = 0; pg < msc->nr_pages; pg++) {
		struct page *page = msc_buffer_get_page(msc, pg);

		if (WARN_ON_ONCE(!page))
			continue;

		if (page->mapping)
			page->mapping = NULL;
	}

	/* last mapping -- drop user_count */
	atomic_dec(&msc->user_count);
	mutex_unlock(&msc->buf_mutex);
}

static vm_fault_t msc_mmap_fault(struct vm_fault *vmf)
{
	struct msc_iter *iter = vmf->vma->vm_file->private_data;
	struct msc *msc = iter->msc;

	vmf->page = msc_buffer_get_page(msc, vmf->pgoff);
	if (!vmf->page)
		return VM_FAULT_SIGBUS;

	get_page(vmf->page);
	vmf->page->mapping = vmf->vma->vm_file->f_mapping;
	vmf->page->index = vmf->pgoff;

	return 0;
}

static const struct vm_operations_struct msc_mmap_ops = {
	.open	= msc_mmap_open,
	.close	= msc_mmap_close,
	.fault	= msc_mmap_fault,
};

static int intel_th_msc_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	struct msc_iter *iter = vma->vm_file->private_data;
	struct msc *msc = iter->msc;
	int ret = -EINVAL;

	if (!size || offset_in_page(size))
		return -EINVAL;

	if (vma->vm_pgoff)
		return -EINVAL;

	/* grab user_count once per mmap; drop in msc_mmap_close() */
	if (!atomic_inc_unless_negative(&msc->user_count))
		return -EINVAL;

	if (msc->mode != MSC_MODE_SINGLE &&
	    msc->mode != MSC_MODE_MULTI)
		goto out;

	if (size >> PAGE_SHIFT != msc->nr_pages)
		goto out;

	atomic_set(&msc->mmap_count, 1);
	ret = 0;

out:
	if (ret)
		atomic_dec(&msc->user_count);

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_flags |= VM_DONTEXPAND | VM_DONTCOPY;
	vma->vm_ops = &msc_mmap_ops;
	return ret;
}

static const struct file_operations intel_th_msc_fops = {
	.open		= intel_th_msc_open,
	.release	= intel_th_msc_release,
	.read		= intel_th_msc_read,
	.mmap		= intel_th_msc_mmap,
	.llseek		= no_llseek,
	.owner		= THIS_MODULE,
};

static int intel_th_msc_init(struct msc *msc)
{
	atomic_set(&msc->user_count, -1);

	msc->mode = MSC_MODE_MULTI;
	mutex_init(&msc->buf_mutex);
	INIT_LIST_HEAD(&msc->win_list);
	INIT_LIST_HEAD(&msc->iter_list);

	msc->burst_len =
		(ioread32(msc->reg_base + REG_MSU_MSC0CTL) & MSC_LEN) >>
		__ffs(MSC_LEN);

	return 0;
}

static const char * const msc_mode[] = {
	[MSC_MODE_SINGLE]	= "single",
	[MSC_MODE_MULTI]	= "multi",
	[MSC_MODE_EXI]		= "ExI",
	[MSC_MODE_DEBUG]	= "debug",
};

static ssize_t
wrap_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct msc *msc = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%d\n", msc->wrap);
}

static ssize_t
wrap_store(struct device *dev, struct device_attribute *attr, const char *buf,
	   size_t size)
{
	struct msc *msc = dev_get_drvdata(dev);
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	msc->wrap = !!val;

	return size;
}

static DEVICE_ATTR_RW(wrap);

static ssize_t
mode_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct msc *msc = dev_get_drvdata(dev);

	return scnprintf(buf, PAGE_SIZE, "%s\n", msc_mode[msc->mode]);
}

static ssize_t
mode_store(struct device *dev, struct device_attribute *attr, const char *buf,
	   size_t size)
{
	struct msc *msc = dev_get_drvdata(dev);
	size_t len = size;
	char *cp;
	int i, ret;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	cp = memchr(buf, '\n', len);
	if (cp)
		len = cp - buf;

	for (i = 0; i < ARRAY_SIZE(msc_mode); i++)
		if (!strncmp(msc_mode[i], buf, len))
			goto found;

	return -EINVAL;

found:
	mutex_lock(&msc->buf_mutex);
	ret = msc_buffer_unlocked_free_unless_used(msc);
	if (!ret)
		msc->mode = i;
	mutex_unlock(&msc->buf_mutex);

	return ret ? ret : size;
}

static DEVICE_ATTR_RW(mode);

static ssize_t
nr_pages_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct msc *msc = dev_get_drvdata(dev);
	struct msc_window *win;
	size_t count = 0;

	mutex_lock(&msc->buf_mutex);

	if (msc->mode == MSC_MODE_SINGLE)
		count = scnprintf(buf, PAGE_SIZE, "%ld\n", msc->nr_pages);
	else if (msc->mode == MSC_MODE_MULTI) {
		list_for_each_entry(win, &msc->win_list, entry) {
			count += scnprintf(buf + count, PAGE_SIZE - count,
					   "%d%c", win->nr_blocks,
					   msc_is_last_win(win) ? '\n' : ',');
		}
	} else {
		count = scnprintf(buf, PAGE_SIZE, "unsupported\n");
	}

	mutex_unlock(&msc->buf_mutex);

	return count;
}

static ssize_t
nr_pages_store(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t size)
{
	struct msc *msc = dev_get_drvdata(dev);
	unsigned long val, *win = NULL, *rewin;
	size_t len = size;
	const char *p = buf;
	char *end, *s;
	int ret, nr_wins = 0;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	ret = msc_buffer_free_unless_used(msc);
	if (ret)
		return ret;

	/* scan the comma-separated list of allocation sizes */
	end = memchr(buf, '\n', len);
	if (end)
		len = end - buf;

	do {
		end = memchr(p, ',', len);
		s = kstrndup(p, end ? end - p : len, GFP_KERNEL);
		if (!s) {
			ret = -ENOMEM;
			goto free_win;
		}

		ret = kstrtoul(s, 10, &val);
		kfree(s);

		if (ret || !val)
			goto free_win;

		if (nr_wins && msc->mode == MSC_MODE_SINGLE) {
			ret = -EINVAL;
			goto free_win;
		}

		nr_wins++;
		rewin = krealloc(win, sizeof(*win) * nr_wins, GFP_KERNEL);
		if (!rewin) {
			kfree(win);
			return -ENOMEM;
		}

		win = rewin;
		win[nr_wins - 1] = val;

		if (!end)
			break;

		/* consume the number and the following comma, hence +1 */
		len -= end - p + 1;
		p = end + 1;
	} while (len);

	mutex_lock(&msc->buf_mutex);
	ret = msc_buffer_alloc(msc, win, nr_wins);
	mutex_unlock(&msc->buf_mutex);

free_win:
	kfree(win);

	return ret ? ret : size;
}

static DEVICE_ATTR_RW(nr_pages);

static struct attribute *msc_output_attrs[] = {
	&dev_attr_wrap.attr,
	&dev_attr_mode.attr,
	&dev_attr_nr_pages.attr,
	NULL,
};

static struct attribute_group msc_output_group = {
	.attrs	= msc_output_attrs,
};

static int intel_th_msc_probe(struct intel_th_device *thdev)
{
	struct device *dev = &thdev->dev;
	struct resource *res;
	struct msc *msc;
	void __iomem *base;
	int err;

	res = intel_th_device_get_resource(thdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	base = devm_ioremap(dev, res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	msc = devm_kzalloc(dev, sizeof(*msc), GFP_KERNEL);
	if (!msc)
		return -ENOMEM;

	msc->index = thdev->id;

	msc->thdev = thdev;
	msc->reg_base = base + msc->index * 0x100;

	err = intel_th_msc_init(msc);
	if (err)
		return err;

	dev_set_drvdata(dev, msc);

	return 0;
}

static void intel_th_msc_remove(struct intel_th_device *thdev)
{
	struct msc *msc = dev_get_drvdata(&thdev->dev);
	int ret;

	intel_th_msc_deactivate(thdev);

	/*
	 * Buffers should not be used at this point except if the
	 * output character device is still open and the parent
	 * device gets detached from its bus, which is a FIXME.
	 */
	ret = msc_buffer_free_unless_used(msc);
	WARN_ON_ONCE(ret);
}

static struct intel_th_driver intel_th_msc_driver = {
	.probe	= intel_th_msc_probe,
	.remove	= intel_th_msc_remove,
	.activate	= intel_th_msc_activate,
	.deactivate	= intel_th_msc_deactivate,
	.fops	= &intel_th_msc_fops,
	.attr_group	= &msc_output_group,
	.driver	= {
		.name	= "msc",
		.owner	= THIS_MODULE,
	},
};

module_driver(intel_th_msc_driver,
	      intel_th_driver_register,
	      intel_th_driver_unregister);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Intel(R) Trace Hub Memory Storage Unit driver");
MODULE_AUTHOR("Alexander Shishkin <alexander.shishkin@linux.intel.com>");
