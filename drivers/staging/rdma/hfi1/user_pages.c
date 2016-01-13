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
#include <linux/sched.h>
#include <linux/device.h>

#include "hfi.h"

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

int hfi1_acquire_user_pages(unsigned long vaddr, size_t npages, bool writable,
			    struct page **pages)
{
	unsigned long pinned, lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	bool can_lock = capable(CAP_IPC_LOCK);
	int ret;

	down_read(&current->mm->mmap_sem);
	pinned = current->mm->pinned_vm;
	up_read(&current->mm->mmap_sem);

	if (pinned + npages > lock_limit && !can_lock)
		return -ENOMEM;

	ret = get_user_pages_fast(vaddr, npages, writable, pages);
	if (ret < 0)
		return ret;

	down_write(&current->mm->mmap_sem);
	current->mm->pinned_vm += ret;
	up_write(&current->mm->mmap_sem);

	return ret;
}

void hfi1_release_user_pages(struct page **p, size_t npages, bool dirty)
{
	size_t i;

	for (i = 0; i < npages; i++) {
		if (dirty)
			set_page_dirty_lock(p[i]);
		put_page(p[i]);
	}

	if (current->mm) { /* during close after signal, mm can be NULL */
		down_write(&current->mm->mmap_sem);
		current->mm->pinned_vm -= npages;
		up_write(&current->mm->mmap_sem);
	}
}
