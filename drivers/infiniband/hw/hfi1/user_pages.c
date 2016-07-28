/*
 * Copyright(c) 2015, 2016 Intel Corporation.
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * BSD LICENSE
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/mm.h>
#include <linux/sched.h>
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
	unsigned usr_ctxts = dd->num_rcv_contexts - dd->first_user_ctxt;
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

	down_read(&mm->mmap_sem);
	pinned = mm->pinned_vm;
	up_read(&mm->mmap_sem);

	/* First, check the absolute limit against all pinned pages. */
	if (pinned + npages >= ulimit && !can_lock)
		return false;

	return ((nlocked + npages) <= size) || can_lock;
}

int hfi1_acquire_user_pages(struct mm_struct *mm, unsigned long vaddr, size_t npages,
			    bool writable, struct page **pages)
{
	int ret;

	ret = get_user_pages_fast(vaddr, npages, writable, pages);
	if (ret < 0)
		return ret;

	down_write(&mm->mmap_sem);
	mm->pinned_vm += ret;
	up_write(&mm->mmap_sem);

	return ret;
}

void hfi1_release_user_pages(struct mm_struct *mm, struct page **p,
			     size_t npages, bool dirty)
{
	size_t i;

	for (i = 0; i < npages; i++) {
		if (dirty)
			set_page_dirty_lock(p[i]);
		put_page(p[i]);
	}

	if (mm) { /* during close after signal, mm can be NULL */
		down_write(&mm->mmap_sem);
		mm->pinned_vm -= npages;
		up_write(&mm->mmap_sem);
	}
}
