// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2015-2017 Intel Corporation.
 */

#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/device.h>
#include <linux/module.h>

#include "hfi.h"

static unsigned long cache_size = 256;
module_param(cache_size, ulong, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(cache_size, "Send and receive side cache size limit (in MB)");

/*
 * Determine whether the caller can pin pages.
 *
 * This function should be used in the implementation of buffer caches.
 * The cache implementation should call this function prior to attempting
 * to pin buffer pages in order to determine whether they should do so.
 * The function computes cache limits based on the configured ulimit and
 * cache size. Use of this function is especially important for caches
 * which are not limited in any other way (e.g. by HW resources) and, thus,
 * could keeping caching buffers.
 *
 */
bool hfi1_can_pin_pages(struct hfi1_devdata *dd, struct mm_struct *mm,
			u32 nlocked, u32 npages)
{
	unsigned long ulimit = rlimit(RLIMIT_MEMLOCK), pinned, cache_limit,
		size = (cache_size * (1UL << 20)); /* convert to bytes */
	unsigned int usr_ctxts =
			dd->num_rcv_contexts - dd->first_dyn_alloc_ctxt;
	bool can_lock = capable(CAP_IPC_LOCK);

	/*
	 * Calculate per-cache size. The calculation below uses only a quarter
	 * of the available per-context limit. This leaves space for other
	 * pinning. Should we worry about shared ctxts?
	 */
	cache_limit = (ulimit / usr_ctxts) / 4;

	/* If ulimit isn't set to "unlimited" and is smaller than cache_size. */
	if (ulimit != (-1UL) && size > cache_limit)
		size = cache_limit;

	/* Convert to number of pages */
	size = DIV_ROUND_UP(size, PAGE_SIZE);

	pinned = atomic64_read(&mm->pinned_vm);

	/* First, check the absolute limit against all pinned pages. */
	if (pinned + npages >= ulimit && !can_lock)
		return false;

	return ((nlocked + npages) <= size) || can_lock;
}

int hfi1_acquire_user_pages(struct mm_struct *mm, unsigned long vaddr, size_t npages,
			    bool writable, struct page **pages)
{
	int ret;
	unsigned int gup_flags = FOLL_LONGTERM | (writable ? FOLL_WRITE : 0);

	ret = pin_user_pages_fast(vaddr, npages, gup_flags, pages);
	if (ret < 0)
		return ret;

	atomic64_add(ret, &mm->pinned_vm);

	return ret;
}

void hfi1_release_user_pages(struct mm_struct *mm, struct page **p,
			     size_t npages, bool dirty)
{
	unpin_user_pages_dirty_lock(p, npages, dirty);

	if (mm) { /* during close after signal, mm can be NULL */
		atomic64_sub(npages, &mm->pinned_vm);
	}
}
