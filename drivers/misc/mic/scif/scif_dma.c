/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 *
 */
#include "scif_main.h"
#include "scif_map.h"

/*
 * struct scif_dma_comp_cb - SCIF DMA completion callback
 *
 * @dma_completion_func: DMA completion callback
 * @cb_cookie: DMA completion callback cookie
 * @temp_buf: Temporary buffer
 * @temp_buf_to_free: Temporary buffer to be freed
 * @is_cache: Is a kmem_cache allocated buffer
 * @dst_offset: Destination registration offset
 * @dst_window: Destination registration window
 * @len: Length of the temp buffer
 * @temp_phys: DMA address of the temp buffer
 * @sdev: The SCIF device
 * @header_padding: padding for cache line alignment
 */
struct scif_dma_comp_cb {
	void (*dma_completion_func)(void *cookie);
	void *cb_cookie;
	u8 *temp_buf;
	u8 *temp_buf_to_free;
	bool is_cache;
	s64 dst_offset;
	struct scif_window *dst_window;
	size_t len;
	dma_addr_t temp_phys;
	struct scif_dev *sdev;
	int header_padding;
};

/**
 * struct scif_copy_work - Work for DMA copy
 *
 * @src_offset: Starting source offset
 * @dst_offset: Starting destination offset
 * @src_window: Starting src registered window
 * @dst_window: Starting dst registered window
 * @loopback: true if this is a loopback DMA transfer
 * @len: Length of the transfer
 * @comp_cb: DMA copy completion callback
 * @remote_dev: The remote SCIF peer device
 * @fence_type: polling or interrupt based
 * @ordered: is this a tail byte ordered DMA transfer
 */
struct scif_copy_work {
	s64 src_offset;
	s64 dst_offset;
	struct scif_window *src_window;
	struct scif_window *dst_window;
	int loopback;
	size_t len;
	struct scif_dma_comp_cb   *comp_cb;
	struct scif_dev	*remote_dev;
	int fence_type;
	bool ordered;
};

#ifndef list_entry_next
#define list_entry_next(pos, member) \
	list_entry(pos->member.next, typeof(*pos), member)
#endif

/**
 * scif_reserve_dma_chan:
 * @ep: Endpoint Descriptor.
 *
 * This routine reserves a DMA channel for a particular
 * endpoint. All DMA transfers for an endpoint are always
 * programmed on the same DMA channel.
 */
int scif_reserve_dma_chan(struct scif_endpt *ep)
{
	int err = 0;
	struct scif_dev *scifdev;
	struct scif_hw_dev *sdev;
	struct dma_chan *chan;

	/* Loopback DMAs are not supported on the management node */
	if (!scif_info.nodeid && scifdev_self(ep->remote_dev))
		return 0;
	if (scif_info.nodeid)
		scifdev = &scif_dev[0];
	else
		scifdev = ep->remote_dev;
	sdev = scifdev->sdev;
	if (!sdev->num_dma_ch)
		return -ENODEV;
	chan = sdev->dma_ch[scifdev->dma_ch_idx];
	scifdev->dma_ch_idx = (scifdev->dma_ch_idx + 1) % sdev->num_dma_ch;
	mutex_lock(&ep->rma_info.rma_lock);
	ep->rma_info.dma_chan = chan;
	mutex_unlock(&ep->rma_info.rma_lock);
	return err;
}

#ifdef CONFIG_MMU_NOTIFIER
/**
 * scif_rma_destroy_tcw:
 *
 * This routine destroys temporary cached windows
 */
static
void __scif_rma_destroy_tcw(struct scif_mmu_notif *mmn,
			    struct scif_endpt *ep,
			    u64 start, u64 len)
{
	struct list_head *item, *tmp;
	struct scif_window *window;
	u64 start_va, end_va;
	u64 end = start + len;

	if (end <= start)
		return;

	list_for_each_safe(item, tmp, &mmn->tc_reg_list) {
		window = list_entry(item, struct scif_window, list);
		ep = (struct scif_endpt *)window->ep;
		if (!len)
			break;
		start_va = window->va_for_temp;
		end_va = start_va + (window->nr_pages << PAGE_SHIFT);
		if (start < start_va && end <= start_va)
			break;
		if (start >= end_va)
			continue;
		__scif_rma_destroy_tcw_helper(window);
	}
}

static void scif_rma_destroy_tcw(struct scif_mmu_notif *mmn, u64 start, u64 len)
{
	struct scif_endpt *ep = mmn->ep;

	spin_lock(&ep->rma_info.tc_lock);
	__scif_rma_destroy_tcw(mmn, ep, start, len);
	spin_unlock(&ep->rma_info.tc_lock);
}

static void scif_rma_destroy_tcw_ep(struct scif_endpt *ep)
{
	struct list_head *item, *tmp;
	struct scif_mmu_notif *mmn;

	list_for_each_safe(item, tmp, &ep->rma_info.mmn_list) {
		mmn = list_entry(item, struct scif_mmu_notif, list);
		scif_rma_destroy_tcw(mmn, 0, ULONG_MAX);
	}
}

static void __scif_rma_destroy_tcw_ep(struct scif_endpt *ep)
{
	struct list_head *item, *tmp;
	struct scif_mmu_notif *mmn;

	spin_lock(&ep->rma_info.tc_lock);
	list_for_each_safe(item, tmp, &ep->rma_info.mmn_list) {
		mmn = list_entry(item, struct scif_mmu_notif, list);
		__scif_rma_destroy_tcw(mmn, ep, 0, ULONG_MAX);
	}
	spin_unlock(&ep->rma_info.tc_lock);
}

static bool scif_rma_tc_can_cache(struct scif_endpt *ep, size_t cur_bytes)
{
	if ((cur_bytes >> PAGE_SHIFT) > scif_info.rma_tc_limit)
		return false;
	if ((atomic_read(&ep->rma_info.tcw_total_pages)
			+ (cur_bytes >> PAGE_SHIFT)) >
			scif_info.rma_tc_limit) {
		dev_info(scif_info.mdev.this_device,
			 "%s %d total=%d, current=%zu reached max\n",
			 __func__, __LINE__,
			 atomic_read(&ep->rma_info.tcw_total_pages),
			 (1 + (cur_bytes >> PAGE_SHIFT)));
		scif_rma_destroy_tcw_invalid();
		__scif_rma_destroy_tcw_ep(ep);
	}
	return true;
}

static void scif_mmu_notifier_release(struct mmu_notifier *mn,
				      struct mm_struct *mm)
{
	struct scif_mmu_notif	*mmn;

	mmn = container_of(mn, struct scif_mmu_notif, ep_mmu_notifier);
	scif_rma_destroy_tcw(mmn, 0, ULONG_MAX);
	schedule_work(&scif_info.misc_work);
}

static void scif_mmu_notifier_invalidate_page(struct mmu_notifier *mn,
					      struct mm_struct *mm,
					      unsigned long address)
{
	struct scif_mmu_notif	*mmn;

	mmn = container_of(mn, struct scif_mmu_notif, ep_mmu_notifier);
	scif_rma_destroy_tcw(mmn, address, PAGE_SIZE);
}

static void scif_mmu_notifier_invalidate_range_start(struct mmu_notifier *mn,
						     struct mm_struct *mm,
						     unsigned long start,
						     unsigned long end)
{
	struct scif_mmu_notif	*mmn;

	mmn = container_of(mn, struct scif_mmu_notif, ep_mmu_notifier);
	scif_rma_destroy_tcw(mmn, start, end - start);
}

static void scif_mmu_notifier_invalidate_range_end(struct mmu_notifier *mn,
						   struct mm_struct *mm,
						   unsigned long start,
						   unsigned long end)
{
	/*
	 * Nothing to do here, everything needed was done in
	 * invalidate_range_start.
	 */
}

static const struct mmu_notifier_ops scif_mmu_notifier_ops = {
	.release = scif_mmu_notifier_release,
	.clear_flush_young = NULL,
	.invalidate_page = scif_mmu_notifier_invalidate_page,
	.invalidate_range_start = scif_mmu_notifier_invalidate_range_start,
	.invalidate_range_end = scif_mmu_notifier_invalidate_range_end};

static void scif_ep_unregister_mmu_notifier(struct scif_endpt *ep)
{
	struct scif_endpt_rma_info *rma = &ep->rma_info;
	struct scif_mmu_notif *mmn = NULL;
	struct list_head *item, *tmp;

	mutex_lock(&ep->rma_info.mmn_lock);
	list_for_each_safe(item, tmp, &rma->mmn_list) {
		mmn = list_entry(item, struct scif_mmu_notif, list);
		mmu_notifier_unregister(&mmn->ep_mmu_notifier, mmn->mm);
		list_del(item);
		kfree(mmn);
	}
	mutex_unlock(&ep->rma_info.mmn_lock);
}

static void scif_init_mmu_notifier(struct scif_mmu_notif *mmn,
				   struct mm_struct *mm, struct scif_endpt *ep)
{
	mmn->ep = ep;
	mmn->mm = mm;
	mmn->ep_mmu_notifier.ops = &scif_mmu_notifier_ops;
	INIT_LIST_HEAD(&mmn->list);
	INIT_LIST_HEAD(&mmn->tc_reg_list);
}

static struct scif_mmu_notif *
scif_find_mmu_notifier(struct mm_struct *mm, struct scif_endpt_rma_info *rma)
{
	struct scif_mmu_notif *mmn;
	struct list_head *item;

	list_for_each(item, &rma->mmn_list) {
		mmn = list_entry(item, struct scif_mmu_notif, list);
		if (mmn->mm == mm)
			return mmn;
	}
	return NULL;
}

static struct scif_mmu_notif *
scif_add_mmu_notifier(struct mm_struct *mm, struct scif_endpt *ep)
{
	struct scif_mmu_notif *mmn
		 = kzalloc(sizeof(*mmn), GFP_KERNEL);

	if (!mmn)
		return ERR_PTR(ENOMEM);

	scif_init_mmu_notifier(mmn, current->mm, ep);
	if (mmu_notifier_register(&mmn->ep_mmu_notifier,
				  current->mm)) {
		kfree(mmn);
		return ERR_PTR(EBUSY);
	}
	list_add(&mmn->list, &ep->rma_info.mmn_list);
	return mmn;
}

/*
 * Called from the misc thread to destroy temporary cached windows and
 * unregister the MMU notifier for the SCIF endpoint.
 */
void scif_mmu_notif_handler(struct work_struct *work)
{
	struct list_head *pos, *tmpq;
	struct scif_endpt *ep;
restart:
	scif_rma_destroy_tcw_invalid();
	spin_lock(&scif_info.rmalock);
	list_for_each_safe(pos, tmpq, &scif_info.mmu_notif_cleanup) {
		ep = list_entry(pos, struct scif_endpt, mmu_list);
		list_del(&ep->mmu_list);
		spin_unlock(&scif_info.rmalock);
		scif_rma_destroy_tcw_ep(ep);
		scif_ep_unregister_mmu_notifier(ep);
		goto restart;
	}
	spin_unlock(&scif_info.rmalock);
}

static bool scif_is_set_reg_cache(int flags)
{
	return !!(flags & SCIF_RMA_USECACHE);
}
#else
static struct scif_mmu_notif *
scif_find_mmu_notifier(struct mm_struct *mm,
		       struct scif_endpt_rma_info *rma)
{
	return NULL;
}

static struct scif_mmu_notif *
scif_add_mmu_notifier(struct mm_struct *mm, struct scif_endpt *ep)
{
	return NULL;
}

void scif_mmu_notif_handler(struct work_struct *work)
{
}

static bool scif_is_set_reg_cache(int flags)
{
	return false;
}

static bool scif_rma_tc_can_cache(struct scif_endpt *ep, size_t cur_bytes)
{
	return false;
}
#endif

/**
 * scif_register_temp:
 * @epd: End Point Descriptor.
 * @addr: virtual address to/from which to copy
 * @len: length of range to copy
 * @out_offset: computed offset returned by reference.
 * @out_window: allocated registered window returned by reference.
 *
 * Create a temporary registered window. The peer will not know about this
 * window. This API is used for scif_vreadfrom()/scif_vwriteto() API's.
 */
static int
scif_register_temp(scif_epd_t epd, unsigned long addr, size_t len, int prot,
		   off_t *out_offset, struct scif_window **out_window)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	int err;
	scif_pinned_pages_t pinned_pages;
	size_t aligned_len;

	aligned_len = ALIGN(len, PAGE_SIZE);

	err = __scif_pin_pages((void *)(addr & PAGE_MASK),
			       aligned_len, &prot, 0, &pinned_pages);
	if (err)
		return err;

	pinned_pages->prot = prot;

	/* Compute the offset for this registration */
	err = scif_get_window_offset(ep, 0, 0,
				     aligned_len >> PAGE_SHIFT,
				     (s64 *)out_offset);
	if (err)
		goto error_unpin;

	/* Allocate and prepare self registration window */
	*out_window = scif_create_window(ep, aligned_len >> PAGE_SHIFT,
					*out_offset, true);
	if (!*out_window) {
		scif_free_window_offset(ep, NULL, *out_offset);
		err = -ENOMEM;
		goto error_unpin;
	}

	(*out_window)->pinned_pages = pinned_pages;
	(*out_window)->nr_pages = pinned_pages->nr_pages;
	(*out_window)->prot = pinned_pages->prot;

	(*out_window)->va_for_temp = addr & PAGE_MASK;
	err = scif_map_window(ep->remote_dev, *out_window);
	if (err) {
		/* Something went wrong! Rollback */
		scif_destroy_window(ep, *out_window);
		*out_window = NULL;
	} else {
		*out_offset |= (addr - (*out_window)->va_for_temp);
	}
	return err;
error_unpin:
	if (err)
		dev_err(&ep->remote_dev->sdev->dev,
			"%s %d err %d\n", __func__, __LINE__, err);
	scif_unpin_pages(pinned_pages);
	return err;
}

#define SCIF_DMA_TO (3 * HZ)

/*
 * scif_sync_dma - Program a DMA without an interrupt descriptor
 *
 * @dev - The address of the pointer to the device instance used
 * for DMA registration.
 * @chan - DMA channel to be used.
 * @sync_wait: Wait for DMA to complete?
 *
 * Return 0 on success and -errno on error.
 */
static int scif_sync_dma(struct scif_hw_dev *sdev, struct dma_chan *chan,
			 bool sync_wait)
{
	int err = 0;
	struct dma_async_tx_descriptor *tx = NULL;
	enum dma_ctrl_flags flags = DMA_PREP_FENCE;
	dma_cookie_t cookie;
	struct dma_device *ddev;

	if (!chan) {
		err = -EIO;
		dev_err(&sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		return err;
	}
	ddev = chan->device;

	tx = ddev->device_prep_dma_memcpy(chan, 0, 0, 0, flags);
	if (!tx) {
		err = -ENOMEM;
		dev_err(&sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		goto release;
	}
	cookie = tx->tx_submit(tx);

	if (dma_submit_error(cookie)) {
		err = -ENOMEM;
		dev_err(&sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		goto release;
	}
	if (!sync_wait) {
		dma_async_issue_pending(chan);
	} else {
		if (dma_sync_wait(chan, cookie) == DMA_COMPLETE) {
			err = 0;
		} else {
			err = -EIO;
			dev_err(&sdev->dev, "%s %d err %d\n",
				__func__, __LINE__, err);
		}
	}
release:
	return err;
}

static void scif_dma_callback(void *arg)
{
	struct completion *done = (struct completion *)arg;

	complete(done);
}

#define SCIF_DMA_SYNC_WAIT true
#define SCIF_DMA_POLL BIT(0)
#define SCIF_DMA_INTR BIT(1)

/*
 * scif_async_dma - Program a DMA with an interrupt descriptor
 *
 * @dev - The address of the pointer to the device instance used
 * for DMA registration.
 * @chan - DMA channel to be used.
 * Return 0 on success and -errno on error.
 */
static int scif_async_dma(struct scif_hw_dev *sdev, struct dma_chan *chan)
{
	int err = 0;
	struct dma_device *ddev;
	struct dma_async_tx_descriptor *tx = NULL;
	enum dma_ctrl_flags flags = DMA_PREP_INTERRUPT | DMA_PREP_FENCE;
	DECLARE_COMPLETION_ONSTACK(done_wait);
	dma_cookie_t cookie;
	enum dma_status status;

	if (!chan) {
		err = -EIO;
		dev_err(&sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		return err;
	}
	ddev = chan->device;

	tx = ddev->device_prep_dma_memcpy(chan, 0, 0, 0, flags);
	if (!tx) {
		err = -ENOMEM;
		dev_err(&sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		goto release;
	}
	reinit_completion(&done_wait);
	tx->callback = scif_dma_callback;
	tx->callback_param = &done_wait;
	cookie = tx->tx_submit(tx);

	if (dma_submit_error(cookie)) {
		err = -ENOMEM;
		dev_err(&sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		goto release;
	}
	dma_async_issue_pending(chan);

	err = wait_for_completion_timeout(&done_wait, SCIF_DMA_TO);
	if (!err) {
		err = -EIO;
		dev_err(&sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		goto release;
	}
	err = 0;
	status = dma_async_is_tx_complete(chan, cookie, NULL, NULL);
	if (status != DMA_COMPLETE) {
		err = -EIO;
		dev_err(&sdev->dev, "%s %d err %d\n",
			__func__, __LINE__, err);
		goto release;
	}
release:
	return err;
}

/*
 * scif_drain_dma_poll - Drain all outstanding DMA operations for a particular
 * DMA channel via polling.
 *
 * @sdev - The SCIF device
 * @chan - DMA channel
 * Return 0 on success and -errno on error.
 */
static int scif_drain_dma_poll(struct scif_hw_dev *sdev, struct dma_chan *chan)
{
	if (!chan)
		return -EINVAL;
	return scif_sync_dma(sdev, chan, SCIF_DMA_SYNC_WAIT);
}

/*
 * scif_drain_dma_intr - Drain all outstanding DMA operations for a particular
 * DMA channel via interrupt based blocking wait.
 *
 * @sdev - The SCIF device
 * @chan - DMA channel
 * Return 0 on success and -errno on error.
 */
int scif_drain_dma_intr(struct scif_hw_dev *sdev, struct dma_chan *chan)
{
	if (!chan)
		return -EINVAL;
	return scif_async_dma(sdev, chan);
}

/**
 * scif_rma_destroy_windows:
 *
 * This routine destroys all windows queued for cleanup
 */
void scif_rma_destroy_windows(void)
{
	struct list_head *item, *tmp;
	struct scif_window *window;
	struct scif_endpt *ep;
	struct dma_chan *chan;

	might_sleep();
restart:
	spin_lock(&scif_info.rmalock);
	list_for_each_safe(item, tmp, &scif_info.rma) {
		window = list_entry(item, struct scif_window,
				    list);
		ep = (struct scif_endpt *)window->ep;
		chan = ep->rma_info.dma_chan;

		list_del_init(&window->list);
		spin_unlock(&scif_info.rmalock);
		if (!chan || !scifdev_alive(ep) ||
		    !scif_drain_dma_intr(ep->remote_dev->sdev,
					 ep->rma_info.dma_chan))
			/* Remove window from global list */
			window->unreg_state = OP_COMPLETED;
		else
			dev_warn(&ep->remote_dev->sdev->dev,
				 "DMA engine hung?\n");
		if (window->unreg_state == OP_COMPLETED) {
			if (window->type == SCIF_WINDOW_SELF)
				scif_destroy_window(ep, window);
			else
				scif_destroy_remote_window(window);
			atomic_dec(&ep->rma_info.tw_refcount);
		}
		goto restart;
	}
	spin_unlock(&scif_info.rmalock);
}

/**
 * scif_rma_destroy_tcw:
 *
 * This routine destroys temporary cached registered windows
 * which have been queued for cleanup.
 */
void scif_rma_destroy_tcw_invalid(void)
{
	struct list_head *item, *tmp;
	struct scif_window *window;
	struct scif_endpt *ep;
	struct dma_chan *chan;

	might_sleep();
restart:
	spin_lock(&scif_info.rmalock);
	list_for_each_safe(item, tmp, &scif_info.rma_tc) {
		window = list_entry(item, struct scif_window, list);
		ep = (struct scif_endpt *)window->ep;
		chan = ep->rma_info.dma_chan;
		list_del_init(&window->list);
		spin_unlock(&scif_info.rmalock);
		mutex_lock(&ep->rma_info.rma_lock);
		if (!chan || !scifdev_alive(ep) ||
		    !scif_drain_dma_intr(ep->remote_dev->sdev,
					 ep->rma_info.dma_chan)) {
			atomic_sub(window->nr_pages,
				   &ep->rma_info.tcw_total_pages);
			scif_destroy_window(ep, window);
			atomic_dec(&ep->rma_info.tcw_refcount);
		} else {
			dev_warn(&ep->remote_dev->sdev->dev,
				 "DMA engine hung?\n");
		}
		mutex_unlock(&ep->rma_info.rma_lock);
		goto restart;
	}
	spin_unlock(&scif_info.rmalock);
}

static inline
void *_get_local_va(off_t off, struct scif_window *window, size_t len)
{
	int page_nr = (off - window->offset) >> PAGE_SHIFT;
	off_t page_off = off & ~PAGE_MASK;
	void *va = NULL;

	if (window->type == SCIF_WINDOW_SELF) {
		struct page **pages = window->pinned_pages->pages;

		va = page_address(pages[page_nr]) + page_off;
	}
	return va;
}

static inline
void *ioremap_remote(off_t off, struct scif_window *window,
		     size_t len, struct scif_dev *dev,
		     struct scif_window_iter *iter)
{
	dma_addr_t phys = scif_off_to_dma_addr(window, off, NULL, iter);

	/*
	 * If the DMA address is not card relative then we need the DMA
	 * addresses to be an offset into the bar. The aperture base was already
	 * added so subtract it here since scif_ioremap is going to add it again
	 */
	if (!scifdev_self(dev) && window->type == SCIF_WINDOW_PEER &&
	    dev->sdev->aper && !dev->sdev->card_rel_da)
		phys = phys - dev->sdev->aper->pa;
	return scif_ioremap(phys, len, dev);
}

static inline void
iounmap_remote(void *virt, size_t size, struct scif_copy_work *work)
{
	scif_iounmap(virt, size, work->remote_dev);
}

/*
 * Takes care of ordering issue caused by
 * 1. Hardware:  Only in the case of cpu copy from mgmt node to card
 * because of WC memory.
 * 2. Software: If memcpy reorders copy instructions for optimization.
 * This could happen at both mgmt node and card.
 */
static inline void
scif_ordered_memcpy_toio(char *dst, const char *src, size_t count)
{
	if (!count)
		return;

	memcpy_toio((void __iomem __force *)dst, src, --count);
	/* Order the last byte with the previous stores */
	wmb();
	*(dst + count) = *(src + count);
}

static inline void scif_unaligned_cpy_toio(char *dst, const char *src,
					   size_t count, bool ordered)
{
	if (ordered)
		scif_ordered_memcpy_toio(dst, src, count);
	else
		memcpy_toio((void __iomem __force *)dst, src, count);
}

static inline
void scif_ordered_memcpy_fromio(char *dst, const char *src, size_t count)
{
	if (!count)
		return;

	memcpy_fromio(dst, (void __iomem __force *)src, --count);
	/* Order the last byte with the previous loads */
	rmb();
	*(dst + count) = *(src + count);
}

static inline void scif_unaligned_cpy_fromio(char *dst, const char *src,
					     size_t count, bool ordered)
{
	if (ordered)
		scif_ordered_memcpy_fromio(dst, src, count);
	else
		memcpy_fromio(dst, (void __iomem __force *)src, count);
}

#define SCIF_RMA_ERROR_CODE (~(dma_addr_t)0x0)

/*
 * scif_off_to_dma_addr:
 * Obtain the dma_addr given the window and the offset.
 * @window: Registered window.
 * @off: Window offset.
 * @nr_bytes: Return the number of contiguous bytes till next DMA addr index.
 * @index: Return the index of the dma_addr array found.
 * @start_off: start offset of index of the dma addr array found.
 * The nr_bytes provides the callee an estimate of the maximum possible
 * DMA xfer possible while the index/start_off provide faster lookups
 * for the next iteration.
 */
dma_addr_t scif_off_to_dma_addr(struct scif_window *window, s64 off,
				size_t *nr_bytes, struct scif_window_iter *iter)
{
	int i, page_nr;
	s64 start, end;
	off_t page_off;

	if (window->nr_pages == window->nr_contig_chunks) {
		page_nr = (off - window->offset) >> PAGE_SHIFT;
		page_off = off & ~PAGE_MASK;

		if (nr_bytes)
			*nr_bytes = PAGE_SIZE - page_off;
		return window->dma_addr[page_nr] | page_off;
	}
	if (iter) {
		i = iter->index;
		start = iter->offset;
	} else {
		i =  0;
		start =  window->offset;
	}
	for (; i < window->nr_contig_chunks; i++) {
		end = start + (window->num_pages[i] << PAGE_SHIFT);
		if (off >= start && off < end) {
			if (iter) {
				iter->index = i;
				iter->offset = start;
			}
			if (nr_bytes)
				*nr_bytes = end - off;
			return (window->dma_addr[i] + (off - start));
		}
		start += (window->num_pages[i] << PAGE_SHIFT);
	}
	dev_err(scif_info.mdev.this_device,
		"%s %d BUG. Addr not found? window %p off 0x%llx\n",
		__func__, __LINE__, window, off);
	return SCIF_RMA_ERROR_CODE;
}

/*
 * Copy between rma window and temporary buffer
 */
static void scif_rma_local_cpu_copy(s64 offset, struct scif_window *window,
				    u8 *temp, size_t rem_len, bool to_temp)
{
	void *window_virt;
	size_t loop_len;
	int offset_in_page;
	s64 end_offset;

	offset_in_page = offset & ~PAGE_MASK;
	loop_len = PAGE_SIZE - offset_in_page;

	if (rem_len < loop_len)
		loop_len = rem_len;

	window_virt = _get_local_va(offset, window, loop_len);
	if (!window_virt)
		return;
	if (to_temp)
		memcpy(temp, window_virt, loop_len);
	else
		memcpy(window_virt, temp, loop_len);

	offset += loop_len;
	temp += loop_len;
	rem_len -= loop_len;

	end_offset = window->offset +
		(window->nr_pages << PAGE_SHIFT);
	while (rem_len) {
		if (offset == end_offset) {
			window = list_entry_next(window, list);
			end_offset = window->offset +
				(window->nr_pages << PAGE_SHIFT);
		}
		loop_len = min(PAGE_SIZE, rem_len);
		window_virt = _get_local_va(offset, window, loop_len);
		if (!window_virt)
			return;
		if (to_temp)
			memcpy(temp, window_virt, loop_len);
		else
			memcpy(window_virt, temp, loop_len);
		offset	+= loop_len;
		temp	+= loop_len;
		rem_len	-= loop_len;
	}
}

/**
 * scif_rma_completion_cb:
 * @data: RMA cookie
 *
 * RMA interrupt completion callback.
 */
static void scif_rma_completion_cb(void *data)
{
	struct scif_dma_comp_cb *comp_cb = data;

	/* Free DMA Completion CB. */
	if (comp_cb->dst_window)
		scif_rma_local_cpu_copy(comp_cb->dst_offset,
					comp_cb->dst_window,
					comp_cb->temp_buf +
					comp_cb->header_padding,
					comp_cb->len, false);
	scif_unmap_single(comp_cb->temp_phys, comp_cb->sdev,
			  SCIF_KMEM_UNALIGNED_BUF_SIZE);
	if (comp_cb->is_cache)
		kmem_cache_free(unaligned_cache,
				comp_cb->temp_buf_to_free);
	else
		kfree(comp_cb->temp_buf_to_free);
}

/* Copies between temporary buffer and offsets provided in work */
static int
scif_rma_list_dma_copy_unaligned(struct scif_copy_work *work,
				 u8 *temp, struct dma_chan *chan,
				 bool src_local)
{
	struct scif_dma_comp_cb *comp_cb = work->comp_cb;
	dma_addr_t window_dma_addr, temp_dma_addr;
	dma_addr_t temp_phys = comp_cb->temp_phys;
	size_t loop_len, nr_contig_bytes = 0, remaining_len = work->len;
	int offset_in_ca, ret = 0;
	s64 end_offset, offset;
	struct scif_window *window;
	void *window_virt_addr;
	size_t tail_len;
	struct dma_async_tx_descriptor *tx;
	struct dma_device *dev = chan->device;
	dma_cookie_t cookie;

	if (src_local) {
		offset = work->dst_offset;
		window = work->dst_window;
	} else {
		offset = work->src_offset;
		window = work->src_window;
	}

	offset_in_ca = offset & (L1_CACHE_BYTES - 1);
	if (offset_in_ca) {
		loop_len = L1_CACHE_BYTES - offset_in_ca;
		loop_len = min(loop_len, remaining_len);
		window_virt_addr = ioremap_remote(offset, window,
						  loop_len,
						  work->remote_dev,
						  NULL);
		if (!window_virt_addr)
			return -ENOMEM;
		if (src_local)
			scif_unaligned_cpy_toio(window_virt_addr, temp,
						loop_len,
						work->ordered &&
						!(remaining_len - loop_len));
		else
			scif_unaligned_cpy_fromio(temp, window_virt_addr,
						  loop_len, work->ordered &&
						  !(remaining_len - loop_len));
		iounmap_remote(window_virt_addr, loop_len, work);

		offset += loop_len;
		temp += loop_len;
		temp_phys += loop_len;
		remaining_len -= loop_len;
	}

	offset_in_ca = offset & ~PAGE_MASK;
	end_offset = window->offset +
		(window->nr_pages << PAGE_SHIFT);

	tail_len = remaining_len & (L1_CACHE_BYTES - 1);
	remaining_len -= tail_len;
	while (remaining_len) {
		if (offset == end_offset) {
			window = list_entry_next(window, list);
			end_offset = window->offset +
				(window->nr_pages << PAGE_SHIFT);
		}
		if (scif_is_mgmt_node())
			temp_dma_addr = temp_phys;
		else
			/* Fix if we ever enable IOMMU on the card */
			temp_dma_addr = (dma_addr_t)virt_to_phys(temp);
		window_dma_addr = scif_off_to_dma_addr(window, offset,
						       &nr_contig_bytes,
						       NULL);
		loop_len = min(nr_contig_bytes, remaining_len);
		if (src_local) {
			if (work->ordered && !tail_len &&
			    !(remaining_len - loop_len) &&
			    loop_len != L1_CACHE_BYTES) {
				/*
				 * Break up the last chunk of the transfer into
				 * two steps. if there is no tail to guarantee
				 * DMA ordering. SCIF_DMA_POLLING inserts
				 * a status update descriptor in step 1 which
				 * acts as a double sided synchronization fence
				 * for the DMA engine to ensure that the last
				 * cache line in step 2 is updated last.
				 */
				/* Step 1) DMA: Body Length - L1_CACHE_BYTES. */
				tx =
				dev->device_prep_dma_memcpy(chan,
							    window_dma_addr,
							    temp_dma_addr,
							    loop_len -
							    L1_CACHE_BYTES,
							    DMA_PREP_FENCE);
				if (!tx) {
					ret = -ENOMEM;
					goto err;
				}
				cookie = tx->tx_submit(tx);
				if (dma_submit_error(cookie)) {
					ret = -ENOMEM;
					goto err;
				}
				dma_async_issue_pending(chan);
				offset += (loop_len - L1_CACHE_BYTES);
				temp_dma_addr += (loop_len - L1_CACHE_BYTES);
				window_dma_addr += (loop_len - L1_CACHE_BYTES);
				remaining_len -= (loop_len - L1_CACHE_BYTES);
				loop_len = remaining_len;

				/* Step 2) DMA: L1_CACHE_BYTES */
				tx =
				dev->device_prep_dma_memcpy(chan,
							    window_dma_addr,
							    temp_dma_addr,
							    loop_len, 0);
				if (!tx) {
					ret = -ENOMEM;
					goto err;
				}
				cookie = tx->tx_submit(tx);
				if (dma_submit_error(cookie)) {
					ret = -ENOMEM;
					goto err;
				}
				dma_async_issue_pending(chan);
			} else {
				tx =
				dev->device_prep_dma_memcpy(chan,
							    window_dma_addr,
							    temp_dma_addr,
							    loop_len, 0);
				if (!tx) {
					ret = -ENOMEM;
					goto err;
				}
				cookie = tx->tx_submit(tx);
				if (dma_submit_error(cookie)) {
					ret = -ENOMEM;
					goto err;
				}
				dma_async_issue_pending(chan);
			}
		} else {
			tx = dev->device_prep_dma_memcpy(chan, temp_dma_addr,
					window_dma_addr, loop_len, 0);
			if (!tx) {
				ret = -ENOMEM;
				goto err;
			}
			cookie = tx->tx_submit(tx);
			if (dma_submit_error(cookie)) {
				ret = -ENOMEM;
				goto err;
			}
			dma_async_issue_pending(chan);
		}
		if (ret < 0)
			goto err;
		offset += loop_len;
		temp += loop_len;
		temp_phys += loop_len;
		remaining_len -= loop_len;
		offset_in_ca = 0;
	}
	if (tail_len) {
		if (offset == end_offset) {
			window = list_entry_next(window, list);
			end_offset = window->offset +
				(window->nr_pages << PAGE_SHIFT);
		}
		window_virt_addr = ioremap_remote(offset, window, tail_len,
						  work->remote_dev,
						  NULL);
		if (!window_virt_addr)
			return -ENOMEM;
		/*
		 * The CPU copy for the tail bytes must be initiated only once
		 * previous DMA transfers for this endpoint have completed
		 * to guarantee ordering.
		 */
		if (work->ordered) {
			struct scif_dev *rdev = work->remote_dev;

			ret = scif_drain_dma_intr(rdev->sdev, chan);
			if (ret)
				return ret;
		}
		if (src_local)
			scif_unaligned_cpy_toio(window_virt_addr, temp,
						tail_len, work->ordered);
		else
			scif_unaligned_cpy_fromio(temp, window_virt_addr,
						  tail_len, work->ordered);
		iounmap_remote(window_virt_addr, tail_len, work);
	}
	tx = dev->device_prep_dma_memcpy(chan, 0, 0, 0, DMA_PREP_INTERRUPT);
	if (!tx) {
		ret = -ENOMEM;
		return ret;
	}
	tx->callback = &scif_rma_completion_cb;
	tx->callback_param = comp_cb;
	cookie = tx->tx_submit(tx);

	if (dma_submit_error(cookie)) {
		ret = -ENOMEM;
		return ret;
	}
	dma_async_issue_pending(chan);
	return 0;
err:
	dev_err(scif_info.mdev.this_device,
		"%s %d Desc Prog Failed ret %d\n",
		__func__, __LINE__, ret);
	return ret;
}

/*
 * _scif_rma_list_dma_copy_aligned:
 *
 * Traverse all the windows and perform DMA copy.
 */
static int _scif_rma_list_dma_copy_aligned(struct scif_copy_work *work,
					   struct dma_chan *chan)
{
	dma_addr_t src_dma_addr, dst_dma_addr;
	size_t loop_len, remaining_len, src_contig_bytes = 0;
	size_t dst_contig_bytes = 0;
	struct scif_window_iter src_win_iter;
	struct scif_window_iter dst_win_iter;
	s64 end_src_offset, end_dst_offset;
	struct scif_window *src_window = work->src_window;
	struct scif_window *dst_window = work->dst_window;
	s64 src_offset = work->src_offset, dst_offset = work->dst_offset;
	int ret = 0;
	struct dma_async_tx_descriptor *tx;
	struct dma_device *dev = chan->device;
	dma_cookie_t cookie;

	remaining_len = work->len;

	scif_init_window_iter(src_window, &src_win_iter);
	scif_init_window_iter(dst_window, &dst_win_iter);
	end_src_offset = src_window->offset +
		(src_window->nr_pages << PAGE_SHIFT);
	end_dst_offset = dst_window->offset +
		(dst_window->nr_pages << PAGE_SHIFT);
	while (remaining_len) {
		if (src_offset == end_src_offset) {
			src_window = list_entry_next(src_window, list);
			end_src_offset = src_window->offset +
				(src_window->nr_pages << PAGE_SHIFT);
			scif_init_window_iter(src_window, &src_win_iter);
		}
		if (dst_offset == end_dst_offset) {
			dst_window = list_entry_next(dst_window, list);
			end_dst_offset = dst_window->offset +
				(dst_window->nr_pages << PAGE_SHIFT);
			scif_init_window_iter(dst_window, &dst_win_iter);
		}

		/* compute dma addresses for transfer */
		src_dma_addr = scif_off_to_dma_addr(src_window, src_offset,
						    &src_contig_bytes,
						    &src_win_iter);
		dst_dma_addr = scif_off_to_dma_addr(dst_window, dst_offset,
						    &dst_contig_bytes,
						    &dst_win_iter);
		loop_len = min(src_contig_bytes, dst_contig_bytes);
		loop_len = min(loop_len, remaining_len);
		if (work->ordered && !(remaining_len - loop_len)) {
			/*
			 * Break up the last chunk of the transfer into two
			 * steps to ensure that the last byte in step 2 is
			 * updated last.
			 */
			/* Step 1) DMA: Body Length - 1 */
			tx = dev->device_prep_dma_memcpy(chan, dst_dma_addr,
							 src_dma_addr,
							 loop_len - 1,
							 DMA_PREP_FENCE);
			if (!tx) {
				ret = -ENOMEM;
				goto err;
			}
			cookie = tx->tx_submit(tx);
			if (dma_submit_error(cookie)) {
				ret = -ENOMEM;
				goto err;
			}
			src_offset += (loop_len - 1);
			dst_offset += (loop_len - 1);
			src_dma_addr += (loop_len - 1);
			dst_dma_addr += (loop_len - 1);
			remaining_len -= (loop_len - 1);
			loop_len = remaining_len;

			/* Step 2) DMA: 1 BYTES */
			tx = dev->device_prep_dma_memcpy(chan, dst_dma_addr,
					src_dma_addr, loop_len, 0);
			if (!tx) {
				ret = -ENOMEM;
				goto err;
			}
			cookie = tx->tx_submit(tx);
			if (dma_submit_error(cookie)) {
				ret = -ENOMEM;
				goto err;
			}
			dma_async_issue_pending(chan);
		} else {
			tx = dev->device_prep_dma_memcpy(chan, dst_dma_addr,
					src_dma_addr, loop_len, 0);
			if (!tx) {
				ret = -ENOMEM;
				goto err;
			}
			cookie = tx->tx_submit(tx);
			if (dma_submit_error(cookie)) {
				ret = -ENOMEM;
				goto err;
			}
		}
		src_offset += loop_len;
		dst_offset += loop_len;
		remaining_len -= loop_len;
	}
	return ret;
err:
	dev_err(scif_info.mdev.this_device,
		"%s %d Desc Prog Failed ret %d\n",
		__func__, __LINE__, ret);
	return ret;
}

/*
 * scif_rma_list_dma_copy_aligned:
 *
 * Traverse all the windows and perform DMA copy.
 */
static int scif_rma_list_dma_copy_aligned(struct scif_copy_work *work,
					  struct dma_chan *chan)
{
	dma_addr_t src_dma_addr, dst_dma_addr;
	size_t loop_len, remaining_len, tail_len, src_contig_bytes = 0;
	size_t dst_contig_bytes = 0;
	int src_cache_off;
	s64 end_src_offset, end_dst_offset;
	struct scif_window_iter src_win_iter;
	struct scif_window_iter dst_win_iter;
	void *src_virt, *dst_virt;
	struct scif_window *src_window = work->src_window;
	struct scif_window *dst_window = work->dst_window;
	s64 src_offset = work->src_offset, dst_offset = work->dst_offset;
	int ret = 0;
	struct dma_async_tx_descriptor *tx;
	struct dma_device *dev = chan->device;
	dma_cookie_t cookie;

	remaining_len = work->len;
	scif_init_window_iter(src_window, &src_win_iter);
	scif_init_window_iter(dst_window, &dst_win_iter);

	src_cache_off = src_offset & (L1_CACHE_BYTES - 1);
	if (src_cache_off != 0) {
		/* Head */
		loop_len = L1_CACHE_BYTES - src_cache_off;
		loop_len = min(loop_len, remaining_len);
		src_dma_addr = __scif_off_to_dma_addr(src_window, src_offset);
		dst_dma_addr = __scif_off_to_dma_addr(dst_window, dst_offset);
		if (src_window->type == SCIF_WINDOW_SELF)
			src_virt = _get_local_va(src_offset, src_window,
						 loop_len);
		else
			src_virt = ioremap_remote(src_offset, src_window,
						  loop_len,
						  work->remote_dev, NULL);
		if (!src_virt)
			return -ENOMEM;
		if (dst_window->type == SCIF_WINDOW_SELF)
			dst_virt = _get_local_va(dst_offset, dst_window,
						 loop_len);
		else
			dst_virt = ioremap_remote(dst_offset, dst_window,
						  loop_len,
						  work->remote_dev, NULL);
		if (!dst_virt) {
			if (src_window->type != SCIF_WINDOW_SELF)
				iounmap_remote(src_virt, loop_len, work);
			return -ENOMEM;
		}
		if (src_window->type == SCIF_WINDOW_SELF)
			scif_unaligned_cpy_toio(dst_virt, src_virt, loop_len,
						remaining_len == loop_len ?
						work->ordered : false);
		else
			scif_unaligned_cpy_fromio(dst_virt, src_virt, loop_len,
						  remaining_len == loop_len ?
						  work->ordered : false);
		if (src_window->type != SCIF_WINDOW_SELF)
			iounmap_remote(src_virt, loop_len, work);
		if (dst_window->type != SCIF_WINDOW_SELF)
			iounmap_remote(dst_virt, loop_len, work);
		src_offset += loop_len;
		dst_offset += loop_len;
		remaining_len -= loop_len;
	}

	end_src_offset = src_window->offset +
		(src_window->nr_pages << PAGE_SHIFT);
	end_dst_offset = dst_window->offset +
		(dst_window->nr_pages << PAGE_SHIFT);
	tail_len = remaining_len & (L1_CACHE_BYTES - 1);
	remaining_len -= tail_len;
	while (remaining_len) {
		if (src_offset == end_src_offset) {
			src_window = list_entry_next(src_window, list);
			end_src_offset = src_window->offset +
				(src_window->nr_pages << PAGE_SHIFT);
			scif_init_window_iter(src_window, &src_win_iter);
		}
		if (dst_offset == end_dst_offset) {
			dst_window = list_entry_next(dst_window, list);
			end_dst_offset = dst_window->offset +
				(dst_window->nr_pages << PAGE_SHIFT);
			scif_init_window_iter(dst_window, &dst_win_iter);
		}

		/* compute dma addresses for transfer */
		src_dma_addr = scif_off_to_dma_addr(src_window, src_offset,
						    &src_contig_bytes,
						    &src_win_iter);
		dst_dma_addr = scif_off_to_dma_addr(dst_window, dst_offset,
						    &dst_contig_bytes,
						    &dst_win_iter);
		loop_len = min(src_contig_bytes, dst_contig_bytes);
		loop_len = min(loop_len, remaining_len);
		if (work->ordered && !tail_len &&
		    !(remaining_len - loop_len)) {
			/*
			 * Break up the last chunk of the transfer into two
			 * steps. if there is no tail to gurantee DMA ordering.
			 * Passing SCIF_DMA_POLLING inserts a status update
			 * descriptor in step 1 which acts as a double sided
			 * synchronization fence for the DMA engine to ensure
			 * that the last cache line in step 2 is updated last.
			 */
			/* Step 1) DMA: Body Length - L1_CACHE_BYTES. */
			tx = dev->device_prep_dma_memcpy(chan, dst_dma_addr,
							 src_dma_addr,
							 loop_len -
							 L1_CACHE_BYTES,
							 DMA_PREP_FENCE);
			if (!tx) {
				ret = -ENOMEM;
				goto err;
			}
			cookie = tx->tx_submit(tx);
			if (dma_submit_error(cookie)) {
				ret = -ENOMEM;
				goto err;
			}
			dma_async_issue_pending(chan);
			src_offset += (loop_len - L1_CACHE_BYTES);
			dst_offset += (loop_len - L1_CACHE_BYTES);
			src_dma_addr += (loop_len - L1_CACHE_BYTES);
			dst_dma_addr += (loop_len - L1_CACHE_BYTES);
			remaining_len -= (loop_len - L1_CACHE_BYTES);
			loop_len = remaining_len;

			/* Step 2) DMA: L1_CACHE_BYTES */
			tx = dev->device_prep_dma_memcpy(chan, dst_dma_addr,
							 src_dma_addr,
							 loop_len, 0);
			if (!tx) {
				ret = -ENOMEM;
				goto err;
			}
			cookie = tx->tx_submit(tx);
			if (dma_submit_error(cookie)) {
				ret = -ENOMEM;
				goto err;
			}
			dma_async_issue_pending(chan);
		} else {
			tx = dev->device_prep_dma_memcpy(chan, dst_dma_addr,
							 src_dma_addr,
							 loop_len, 0);
			if (!tx) {
				ret = -ENOMEM;
				goto err;
			}
			cookie = tx->tx_submit(tx);
			if (dma_submit_error(cookie)) {
				ret = -ENOMEM;
				goto err;
			}
			dma_async_issue_pending(chan);
		}
		src_offset += loop_len;
		dst_offset += loop_len;
		remaining_len -= loop_len;
	}
	remaining_len = tail_len;
	if (remaining_len) {
		loop_len = remaining_len;
		if (src_offset == end_src_offset)
			src_window = list_entry_next(src_window, list);
		if (dst_offset == end_dst_offset)
			dst_window = list_entry_next(dst_window, list);

		src_dma_addr = __scif_off_to_dma_addr(src_window, src_offset);
		dst_dma_addr = __scif_off_to_dma_addr(dst_window, dst_offset);
		/*
		 * The CPU copy for the tail bytes must be initiated only once
		 * previous DMA transfers for this endpoint have completed to
		 * guarantee ordering.
		 */
		if (work->ordered) {
			struct scif_dev *rdev = work->remote_dev;

			ret = scif_drain_dma_poll(rdev->sdev, chan);
			if (ret)
				return ret;
		}
		if (src_window->type == SCIF_WINDOW_SELF)
			src_virt = _get_local_va(src_offset, src_window,
						 loop_len);
		else
			src_virt = ioremap_remote(src_offset, src_window,
						  loop_len,
						  work->remote_dev, NULL);
		if (!src_virt)
			return -ENOMEM;

		if (dst_window->type == SCIF_WINDOW_SELF)
			dst_virt = _get_local_va(dst_offset, dst_window,
						 loop_len);
		else
			dst_virt = ioremap_remote(dst_offset, dst_window,
						  loop_len,
						  work->remote_dev, NULL);
		if (!dst_virt) {
			if (src_window->type != SCIF_WINDOW_SELF)
				iounmap_remote(src_virt, loop_len, work);
			return -ENOMEM;
		}

		if (src_window->type == SCIF_WINDOW_SELF)
			scif_unaligned_cpy_toio(dst_virt, src_virt, loop_len,
						work->ordered);
		else
			scif_unaligned_cpy_fromio(dst_virt, src_virt,
						  loop_len, work->ordered);
		if (src_window->type != SCIF_WINDOW_SELF)
			iounmap_remote(src_virt, loop_len, work);

		if (dst_window->type != SCIF_WINDOW_SELF)
			iounmap_remote(dst_virt, loop_len, work);
		remaining_len -= loop_len;
	}
	return ret;
err:
	dev_err(scif_info.mdev.this_device,
		"%s %d Desc Prog Failed ret %d\n",
		__func__, __LINE__, ret);
	return ret;
}

/*
 * scif_rma_list_cpu_copy:
 *
 * Traverse all the windows and perform CPU copy.
 */
static int scif_rma_list_cpu_copy(struct scif_copy_work *work)
{
	void *src_virt, *dst_virt;
	size_t loop_len, remaining_len;
	int src_page_off, dst_page_off;
	s64 src_offset = work->src_offset, dst_offset = work->dst_offset;
	struct scif_window *src_window = work->src_window;
	struct scif_window *dst_window = work->dst_window;
	s64 end_src_offset, end_dst_offset;
	int ret = 0;
	struct scif_window_iter src_win_iter;
	struct scif_window_iter dst_win_iter;

	remaining_len = work->len;

	scif_init_window_iter(src_window, &src_win_iter);
	scif_init_window_iter(dst_window, &dst_win_iter);
	while (remaining_len) {
		src_page_off = src_offset & ~PAGE_MASK;
		dst_page_off = dst_offset & ~PAGE_MASK;
		loop_len = min(PAGE_SIZE -
			       max(src_page_off, dst_page_off),
			       remaining_len);

		if (src_window->type == SCIF_WINDOW_SELF)
			src_virt = _get_local_va(src_offset, src_window,
						 loop_len);
		else
			src_virt = ioremap_remote(src_offset, src_window,
						  loop_len,
						  work->remote_dev,
						  &src_win_iter);
		if (!src_virt) {
			ret = -ENOMEM;
			goto error;
		}

		if (dst_window->type == SCIF_WINDOW_SELF)
			dst_virt = _get_local_va(dst_offset, dst_window,
						 loop_len);
		else
			dst_virt = ioremap_remote(dst_offset, dst_window,
						  loop_len,
						  work->remote_dev,
						  &dst_win_iter);
		if (!dst_virt) {
			if (src_window->type == SCIF_WINDOW_PEER)
				iounmap_remote(src_virt, loop_len, work);
			ret = -ENOMEM;
			goto error;
		}

		if (work->loopback) {
			memcpy(dst_virt, src_virt, loop_len);
		} else {
			if (src_window->type == SCIF_WINDOW_SELF)
				memcpy_toio((void __iomem __force *)dst_virt,
					    src_virt, loop_len);
			else
				memcpy_fromio(dst_virt,
					      (void __iomem __force *)src_virt,
					      loop_len);
		}
		if (src_window->type == SCIF_WINDOW_PEER)
			iounmap_remote(src_virt, loop_len, work);

		if (dst_window->type == SCIF_WINDOW_PEER)
			iounmap_remote(dst_virt, loop_len, work);

		src_offset += loop_len;
		dst_offset += loop_len;
		remaining_len -= loop_len;
		if (remaining_len) {
			end_src_offset = src_window->offset +
				(src_window->nr_pages << PAGE_SHIFT);
			end_dst_offset = dst_window->offset +
				(dst_window->nr_pages << PAGE_SHIFT);
			if (src_offset == end_src_offset) {
				src_window = list_entry_next(src_window, list);
				scif_init_window_iter(src_window,
						      &src_win_iter);
			}
			if (dst_offset == end_dst_offset) {
				dst_window = list_entry_next(dst_window, list);
				scif_init_window_iter(dst_window,
						      &dst_win_iter);
			}
		}
	}
error:
	return ret;
}

static int scif_rma_list_dma_copy_wrapper(struct scif_endpt *epd,
					  struct scif_copy_work *work,
					  struct dma_chan *chan, off_t loffset)
{
	int src_cache_off, dst_cache_off;
	s64 src_offset = work->src_offset, dst_offset = work->dst_offset;
	u8 *temp = NULL;
	bool src_local = true, dst_local = false;
	struct scif_dma_comp_cb *comp_cb;
	dma_addr_t src_dma_addr, dst_dma_addr;
	int err;

	if (is_dma_copy_aligned(chan->device, 1, 1, 1))
		return _scif_rma_list_dma_copy_aligned(work, chan);

	src_cache_off = src_offset & (L1_CACHE_BYTES - 1);
	dst_cache_off = dst_offset & (L1_CACHE_BYTES - 1);

	if (dst_cache_off == src_cache_off)
		return scif_rma_list_dma_copy_aligned(work, chan);

	if (work->loopback)
		return scif_rma_list_cpu_copy(work);
	src_dma_addr = __scif_off_to_dma_addr(work->src_window, src_offset);
	dst_dma_addr = __scif_off_to_dma_addr(work->dst_window, dst_offset);
	src_local = work->src_window->type == SCIF_WINDOW_SELF;
	dst_local = work->dst_window->type == SCIF_WINDOW_SELF;

	dst_local = dst_local;
	/* Allocate dma_completion cb */
	comp_cb = kzalloc(sizeof(*comp_cb), GFP_KERNEL);
	if (!comp_cb)
		goto error;

	work->comp_cb = comp_cb;
	comp_cb->cb_cookie = comp_cb;
	comp_cb->dma_completion_func = &scif_rma_completion_cb;

	if (work->len + (L1_CACHE_BYTES << 1) < SCIF_KMEM_UNALIGNED_BUF_SIZE) {
		comp_cb->is_cache = false;
		/* Allocate padding bytes to align to a cache line */
		temp = kmalloc(work->len + (L1_CACHE_BYTES << 1),
			       GFP_KERNEL);
		if (!temp)
			goto free_comp_cb;
		comp_cb->temp_buf_to_free = temp;
		/* kmalloc(..) does not guarantee cache line alignment */
		if (!IS_ALIGNED((u64)temp, L1_CACHE_BYTES))
			temp = PTR_ALIGN(temp, L1_CACHE_BYTES);
	} else {
		comp_cb->is_cache = true;
		temp = kmem_cache_alloc(unaligned_cache, GFP_KERNEL);
		if (!temp)
			goto free_comp_cb;
		comp_cb->temp_buf_to_free = temp;
	}

	if (src_local) {
		temp += dst_cache_off;
		scif_rma_local_cpu_copy(work->src_offset, work->src_window,
					temp, work->len, true);
	} else {
		comp_cb->dst_window = work->dst_window;
		comp_cb->dst_offset = work->dst_offset;
		work->src_offset = work->src_offset - src_cache_off;
		comp_cb->len = work->len;
		work->len = ALIGN(work->len + src_cache_off, L1_CACHE_BYTES);
		comp_cb->header_padding = src_cache_off;
	}
	comp_cb->temp_buf = temp;

	err = scif_map_single(&comp_cb->temp_phys, temp,
			      work->remote_dev, SCIF_KMEM_UNALIGNED_BUF_SIZE);
	if (err)
		goto free_temp_buf;
	comp_cb->sdev = work->remote_dev;
	if (scif_rma_list_dma_copy_unaligned(work, temp, chan, src_local) < 0)
		goto free_temp_buf;
	if (!src_local)
		work->fence_type = SCIF_DMA_INTR;
	return 0;
free_temp_buf:
	if (comp_cb->is_cache)
		kmem_cache_free(unaligned_cache, comp_cb->temp_buf_to_free);
	else
		kfree(comp_cb->temp_buf_to_free);
free_comp_cb:
	kfree(comp_cb);
error:
	return -ENOMEM;
}

/**
 * scif_rma_copy:
 * @epd: end point descriptor.
 * @loffset: offset in local registered address space to/from which to copy
 * @addr: user virtual address to/from which to copy
 * @len: length of range to copy
 * @roffset: offset in remote registered address space to/from which to copy
 * @flags: flags
 * @dir: LOCAL->REMOTE or vice versa.
 * @last_chunk: true if this is the last chunk of a larger transfer
 *
 * Validate parameters, check if src/dst registered ranges requested for copy
 * are valid and initiate either CPU or DMA copy.
 */
static int scif_rma_copy(scif_epd_t epd, off_t loffset, unsigned long addr,
			 size_t len, off_t roffset, int flags,
			 enum scif_rma_dir dir, bool last_chunk)
{
	struct scif_endpt *ep = (struct scif_endpt *)epd;
	struct scif_rma_req remote_req;
	struct scif_rma_req req;
	struct scif_window *local_window = NULL;
	struct scif_window *remote_window = NULL;
	struct scif_copy_work copy_work;
	bool loopback;
	int err = 0;
	struct dma_chan *chan;
	struct scif_mmu_notif *mmn = NULL;
	bool cache = false;
	struct device *spdev;

	err = scif_verify_epd(ep);
	if (err)
		return err;

	if (flags && !(flags & (SCIF_RMA_USECPU | SCIF_RMA_USECACHE |
				SCIF_RMA_SYNC | SCIF_RMA_ORDERED)))
		return -EINVAL;

	loopback = scifdev_self(ep->remote_dev) ? true : false;
	copy_work.fence_type = ((flags & SCIF_RMA_SYNC) && last_chunk) ?
				SCIF_DMA_POLL : 0;
	copy_work.ordered = !!((flags & SCIF_RMA_ORDERED) && last_chunk);

	/* Use CPU for Mgmt node <-> Mgmt node copies */
	if (loopback && scif_is_mgmt_node()) {
		flags |= SCIF_RMA_USECPU;
		copy_work.fence_type = 0x0;
	}

	cache = scif_is_set_reg_cache(flags);

	remote_req.out_window = &remote_window;
	remote_req.offset = roffset;
	remote_req.nr_bytes = len;
	/*
	 * If transfer is from local to remote then the remote window
	 * must be writeable and vice versa.
	 */
	remote_req.prot = dir == SCIF_LOCAL_TO_REMOTE ? VM_WRITE : VM_READ;
	remote_req.type = SCIF_WINDOW_PARTIAL;
	remote_req.head = &ep->rma_info.remote_reg_list;

	spdev = scif_get_peer_dev(ep->remote_dev);
	if (IS_ERR(spdev)) {
		err = PTR_ERR(spdev);
		return err;
	}

	if (addr && cache) {
		mutex_lock(&ep->rma_info.mmn_lock);
		mmn = scif_find_mmu_notifier(current->mm, &ep->rma_info);
		if (!mmn)
			scif_add_mmu_notifier(current->mm, ep);
		mutex_unlock(&ep->rma_info.mmn_lock);
		if (IS_ERR(mmn)) {
			scif_put_peer_dev(spdev);
			return PTR_ERR(mmn);
		}
		cache = cache && !scif_rma_tc_can_cache(ep, len);
	}
	mutex_lock(&ep->rma_info.rma_lock);
	if (addr) {
		req.out_window = &local_window;
		req.nr_bytes = ALIGN(len + (addr & ~PAGE_MASK),
				     PAGE_SIZE);
		req.va_for_temp = addr & PAGE_MASK;
		req.prot = (dir == SCIF_LOCAL_TO_REMOTE ?
			    VM_READ : VM_WRITE | VM_READ);
		/* Does a valid local window exist? */
		if (mmn) {
			spin_lock(&ep->rma_info.tc_lock);
			req.head = &mmn->tc_reg_list;
			err = scif_query_tcw(ep, &req);
			spin_unlock(&ep->rma_info.tc_lock);
		}
		if (!mmn || err) {
			err = scif_register_temp(epd, req.va_for_temp,
						 req.nr_bytes, req.prot,
						 &loffset, &local_window);
			if (err) {
				mutex_unlock(&ep->rma_info.rma_lock);
				goto error;
			}
			if (!cache)
				goto skip_cache;
			atomic_inc(&ep->rma_info.tcw_refcount);
			atomic_add_return(local_window->nr_pages,
					  &ep->rma_info.tcw_total_pages);
			if (mmn) {
				spin_lock(&ep->rma_info.tc_lock);
				scif_insert_tcw(local_window,
						&mmn->tc_reg_list);
				spin_unlock(&ep->rma_info.tc_lock);
			}
		}
skip_cache:
		loffset = local_window->offset +
				(addr - local_window->va_for_temp);
	} else {
		req.out_window = &local_window;
		req.offset = loffset;
		/*
		 * If transfer is from local to remote then the self window
		 * must be readable and vice versa.
		 */
		req.prot = dir == SCIF_LOCAL_TO_REMOTE ? VM_READ : VM_WRITE;
		req.nr_bytes = len;
		req.type = SCIF_WINDOW_PARTIAL;
		req.head = &ep->rma_info.reg_list;
		/* Does a valid local window exist? */
		err = scif_query_window(&req);
		if (err) {
			mutex_unlock(&ep->rma_info.rma_lock);
			goto error;
		}
	}

	/* Does a valid remote window exist? */
	err = scif_query_window(&remote_req);
	if (err) {
		mutex_unlock(&ep->rma_info.rma_lock);
		goto error;
	}

	/*
	 * Prepare copy_work for submitting work to the DMA kernel thread
	 * or CPU copy routine.
	 */
	copy_work.len = len;
	copy_work.loopback = loopback;
	copy_work.remote_dev = ep->remote_dev;
	if (dir == SCIF_LOCAL_TO_REMOTE) {
		copy_work.src_offset = loffset;
		copy_work.src_window = local_window;
		copy_work.dst_offset = roffset;
		copy_work.dst_window = remote_window;
	} else {
		copy_work.src_offset = roffset;
		copy_work.src_window = remote_window;
		copy_work.dst_offset = loffset;
		copy_work.dst_window = local_window;
	}

	if (flags & SCIF_RMA_USECPU) {
		scif_rma_list_cpu_copy(&copy_work);
	} else {
		chan = ep->rma_info.dma_chan;
		err = scif_rma_list_dma_copy_wrapper(epd, &copy_work,
						     chan, loffset);
	}
	if (addr && !cache)
		atomic_inc(&ep->rma_info.tw_refcount);

	mutex_unlock(&ep->rma_info.rma_lock);

	if (last_chunk) {
		struct scif_dev *rdev = ep->remote_dev;

		if (copy_work.fence_type == SCIF_DMA_POLL)
			err = scif_drain_dma_poll(rdev->sdev,
						  ep->rma_info.dma_chan);
		else if (copy_work.fence_type == SCIF_DMA_INTR)
			err = scif_drain_dma_intr(rdev->sdev,
						  ep->rma_info.dma_chan);
	}

	if (addr && !cache)
		scif_queue_for_cleanup(local_window, &scif_info.rma);
	scif_put_peer_dev(spdev);
	return err;
error:
	if (err) {
		if (addr && local_window && !cache)
			scif_destroy_window(ep, local_window);
		dev_err(scif_info.mdev.this_device,
			"%s %d err %d len 0x%lx\n",
			__func__, __LINE__, err, len);
	}
	scif_put_peer_dev(spdev);
	return err;
}

int scif_readfrom(scif_epd_t epd, off_t loffset, size_t len,
		  off_t roffset, int flags)
{
	int err;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI readfrom: ep %p loffset 0x%lx len 0x%lx offset 0x%lx flags 0x%x\n",
		epd, loffset, len, roffset, flags);
	if (scif_unaligned(loffset, roffset)) {
		while (len > SCIF_MAX_UNALIGNED_BUF_SIZE) {
			err = scif_rma_copy(epd, loffset, 0x0,
					    SCIF_MAX_UNALIGNED_BUF_SIZE,
					    roffset, flags,
					    SCIF_REMOTE_TO_LOCAL, false);
			if (err)
				goto readfrom_err;
			loffset += SCIF_MAX_UNALIGNED_BUF_SIZE;
			roffset += SCIF_MAX_UNALIGNED_BUF_SIZE;
			len -= SCIF_MAX_UNALIGNED_BUF_SIZE;
		}
	}
	err = scif_rma_copy(epd, loffset, 0x0, len,
			    roffset, flags, SCIF_REMOTE_TO_LOCAL, true);
readfrom_err:
	return err;
}
EXPORT_SYMBOL_GPL(scif_readfrom);

int scif_writeto(scif_epd_t epd, off_t loffset, size_t len,
		 off_t roffset, int flags)
{
	int err;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI writeto: ep %p loffset 0x%lx len 0x%lx roffset 0x%lx flags 0x%x\n",
		epd, loffset, len, roffset, flags);
	if (scif_unaligned(loffset, roffset)) {
		while (len > SCIF_MAX_UNALIGNED_BUF_SIZE) {
			err = scif_rma_copy(epd, loffset, 0x0,
					    SCIF_MAX_UNALIGNED_BUF_SIZE,
					    roffset, flags,
					    SCIF_LOCAL_TO_REMOTE, false);
			if (err)
				goto writeto_err;
			loffset += SCIF_MAX_UNALIGNED_BUF_SIZE;
			roffset += SCIF_MAX_UNALIGNED_BUF_SIZE;
			len -= SCIF_MAX_UNALIGNED_BUF_SIZE;
		}
	}
	err = scif_rma_copy(epd, loffset, 0x0, len,
			    roffset, flags, SCIF_LOCAL_TO_REMOTE, true);
writeto_err:
	return err;
}
EXPORT_SYMBOL_GPL(scif_writeto);

int scif_vreadfrom(scif_epd_t epd, void *addr, size_t len,
		   off_t roffset, int flags)
{
	int err;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI vreadfrom: ep %p addr %p len 0x%lx roffset 0x%lx flags 0x%x\n",
		epd, addr, len, roffset, flags);
	if (scif_unaligned((off_t __force)addr, roffset)) {
		if (len > SCIF_MAX_UNALIGNED_BUF_SIZE)
			flags &= ~SCIF_RMA_USECACHE;

		while (len > SCIF_MAX_UNALIGNED_BUF_SIZE) {
			err = scif_rma_copy(epd, 0, (u64)addr,
					    SCIF_MAX_UNALIGNED_BUF_SIZE,
					    roffset, flags,
					    SCIF_REMOTE_TO_LOCAL, false);
			if (err)
				goto vreadfrom_err;
			addr += SCIF_MAX_UNALIGNED_BUF_SIZE;
			roffset += SCIF_MAX_UNALIGNED_BUF_SIZE;
			len -= SCIF_MAX_UNALIGNED_BUF_SIZE;
		}
	}
	err = scif_rma_copy(epd, 0, (u64)addr, len,
			    roffset, flags, SCIF_REMOTE_TO_LOCAL, true);
vreadfrom_err:
	return err;
}
EXPORT_SYMBOL_GPL(scif_vreadfrom);

int scif_vwriteto(scif_epd_t epd, void *addr, size_t len,
		  off_t roffset, int flags)
{
	int err;

	dev_dbg(scif_info.mdev.this_device,
		"SCIFAPI vwriteto: ep %p addr %p len 0x%lx roffset 0x%lx flags 0x%x\n",
		epd, addr, len, roffset, flags);
	if (scif_unaligned((off_t __force)addr, roffset)) {
		if (len > SCIF_MAX_UNALIGNED_BUF_SIZE)
			flags &= ~SCIF_RMA_USECACHE;

		while (len > SCIF_MAX_UNALIGNED_BUF_SIZE) {
			err = scif_rma_copy(epd, 0, (u64)addr,
					    SCIF_MAX_UNALIGNED_BUF_SIZE,
					    roffset, flags,
					    SCIF_LOCAL_TO_REMOTE, false);
			if (err)
				goto vwriteto_err;
			addr += SCIF_MAX_UNALIGNED_BUF_SIZE;
			roffset += SCIF_MAX_UNALIGNED_BUF_SIZE;
			len -= SCIF_MAX_UNALIGNED_BUF_SIZE;
		}
	}
	err = scif_rma_copy(epd, 0, (u64)addr, len,
			    roffset, flags, SCIF_LOCAL_TO_REMOTE, true);
vwriteto_err:
	return err;
}
EXPORT_SYMBOL_GPL(scif_vwriteto);
