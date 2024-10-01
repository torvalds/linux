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
	unsigned long ulimit_pages;
	unsigned long cache_limit_pages;
	unsigned int usr_ctxts;

	/*
	 * Perform RLIMIT_MEMLOCK based checks unless CAP_IPC_LOCK is present.
	 */
	if (!capable(CAP_IPC_LOCK)) {
		ulimit_pages =
			DIV_ROUND_DOWN_ULL(rlimit(RLIMIT_MEMLOCK), PAGE_SIZE);

		/*
		 * Pinning these pages would exceed this process's locked memory
		 * limit.
		 */
		if (atomic64_read(&mm->pinned_vm) + npages > ulimit_pages)
			return false;

		/*
		 * Only allow 1/4 of the user's RLIMIT_MEMLOCK to be used for HFI
		 * caches.  This fraction is then equally distributed among all
		 * existing user contexts.  Note that if RLIMIT_MEMLOCK is
		 * 'unlimited' (-1), the value of this limit will be > 2^42 pages
		 * (2^64 / 2^12 / 2^8 / 2^2).
		 *
		 * The effectiveness of this check may be reduced if I/O occurs on
		 * some user contexts before all user contexts are created.  This
		 * check assumes that this process is the only one using this
		 * context (e.g., the corresponding fd was not passed to another
		 * process for concurrent access) as there is no per-context,
		 * per-process tracking of pinned pages.  It also assumes that each
		 * user context has only one cache to limit.
		 */
		usr_ctxts = dd->num_rcv_contexts - dd->first_dyn_alloc_ctxt;
		if (nlocked + npages > (ulimit_pages / usr_ctxts / 4))
			return false;
	}

	/*
	 * Pinning these pages would exceed the size limit for this cache.
	 */
	cache_limit_pages = cache_size * (1024 * 1024) / PAGE_SIZE;
	if (nlocked + npages > cache_limit_pages)
		return false;

	return true;
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
