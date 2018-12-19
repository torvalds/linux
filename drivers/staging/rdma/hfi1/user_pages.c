/*
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2015 Intel Corporation.
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
 * Copyright(c) 2015 Intel Corporation.
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
#include <linux/device.h>

#include "hfi.h"

static void __hfi1_release_user_pages(struct page **p, size_t num_pages,
				      int dirty)
{
	size_t i;

	for (i = 0; i < num_pages; i++) {
		if (dirty)
			set_page_dirty_lock(p[i]);
		put_page(p[i]);
	}
}

/*
 * Call with current->mm->mmap_sem held.
 */
static int __hfi1_get_user_pages(unsigned long start_page, size_t num_pages,
				 struct page **p)
{
	unsigned long lock_limit;
	size_t got;
	int ret;

	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;

	if (num_pages > lock_limit && !capable(CAP_IPC_LOCK)) {
		ret = -ENOMEM;
		goto bail;
	}

	for (got = 0; got < num_pages; got += ret) {
		ret = get_user_pages(current, current->mm,
				     start_page + got * PAGE_SIZE,
				     num_pages - got, FOLL_WRITE | FOLL_FORCE,
				     p + got, NULL);
		if (ret < 0)
			goto bail_release;
	}

	current->mm->pinned_vm += num_pages;

	ret = 0;
	goto bail;

bail_release:
	__hfi1_release_user_pages(p, got, 0);
bail:
	return ret;
}

/**
 * hfi1_map_page - a safety wrapper around pci_map_page()
 *
 */
dma_addr_t hfi1_map_page(struct pci_dev *hwdev, struct page *page,
			 unsigned long offset, size_t size, int direction)
{
	dma_addr_t phys;

	phys = pci_map_page(hwdev, page, offset, size, direction);

	return phys;
}

/**
 * hfi1_get_user_pages - lock user pages into memory
 * @start_page: the start page
 * @num_pages: the number of pages
 * @p: the output page structures
 *
 * This function takes a given start page (page aligned user virtual
 * address) and pins it and the following specified number of pages.  For
 * now, num_pages is always 1, but that will probably change at some point
 * (because caller is doing expected sends on a single virtually contiguous
 * buffer, so we can do all pages at once).
 */
int hfi1_get_user_pages(unsigned long start_page, size_t num_pages,
			struct page **p)
{
	int ret;

	down_write(&current->mm->mmap_sem);

	ret = __hfi1_get_user_pages(start_page, num_pages, p);

	up_write(&current->mm->mmap_sem);

	return ret;
}

void hfi1_release_user_pages(struct page **p, size_t num_pages)
{
	if (current->mm) /* during close after signal, mm can be NULL */
		down_write(&current->mm->mmap_sem);

	__hfi1_release_user_pages(p, num_pages, 1);

	if (current->mm) {
		current->mm->pinned_vm -= num_pages;
		up_write(&current->mm->mmap_sem);
	}
}
