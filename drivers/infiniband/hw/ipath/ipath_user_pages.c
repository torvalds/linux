/*
 * Copyright (c) 2003, 2004, 2005, 2006 PathScale, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/mm.h>
#include <linux/device.h>

#include "ipath_kernel.h"

static void __ipath_release_user_pages(struct page **p, size_t num_pages,
				   int dirty)
{
	size_t i;

	for (i = 0; i < num_pages; i++) {
		ipath_cdbg(MM, "%lu/%lu put_page %p\n", (unsigned long) i,
			   (unsigned long) num_pages, p[i]);
		if (dirty)
			set_page_dirty_lock(p[i]);
		put_page(p[i]);
	}
}

/* call with current->mm->mmap_sem held */
static int __get_user_pages(unsigned long start_page, size_t num_pages,
			struct page **p, struct vm_area_struct **vma)
{
	unsigned long lock_limit;
	size_t got;
	int ret;

#if 0
	/*
	 * XXX - causes MPI programs to fail, haven't had time to check
	 * yet
	 */
	if (!capable(CAP_IPC_LOCK)) {
		ret = -EPERM;
		goto bail;
	}
#endif

	lock_limit = current->signal->rlim[RLIMIT_MEMLOCK].rlim_cur >>
		PAGE_SHIFT;

	if (num_pages > lock_limit) {
		ret = -ENOMEM;
		goto bail;
	}

	ipath_cdbg(VERBOSE, "pin %lx pages from vaddr %lx\n",
		   (unsigned long) num_pages, start_page);

	for (got = 0; got < num_pages; got += ret) {
		ret = get_user_pages(current, current->mm,
				     start_page + got * PAGE_SIZE,
				     num_pages - got, 1, 1,
				     p + got, vma);
		if (ret < 0)
			goto bail_release;
	}

	current->mm->locked_vm += num_pages;

	ret = 0;
	goto bail;

bail_release:
	__ipath_release_user_pages(p, got, 0);
bail:
	return ret;
}

/**
 * ipath_get_user_pages - lock user pages into memory
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
int ipath_get_user_pages(unsigned long start_page, size_t num_pages,
			 struct page **p)
{
	int ret;

	down_write(&current->mm->mmap_sem);

	ret = __get_user_pages(start_page, num_pages, p, NULL);

	up_write(&current->mm->mmap_sem);

	return ret;
}

/**
 * ipath_get_user_pages_nocopy - lock a single page for I/O and mark shared
 * @start_page: the page to lock
 * @p: the output page structure
 *
 * This is similar to ipath_get_user_pages, but it's always one page, and we
 * mark the page as locked for I/O, and shared.  This is used for the user
 * process page that contains the destination address for the rcvhdrq tail
 * update, so we need to have the vma. If we don't do this, the page can be
 * taken away from us on fork, even if the child never touches it, and then
 * the user process never sees the tail register updates.
 */
int ipath_get_user_pages_nocopy(unsigned long page, struct page **p)
{
	struct vm_area_struct *vma;
	int ret;

	down_write(&current->mm->mmap_sem);

	ret = __get_user_pages(page, 1, p, &vma);

	up_write(&current->mm->mmap_sem);

	return ret;
}

void ipath_release_user_pages(struct page **p, size_t num_pages)
{
	down_write(&current->mm->mmap_sem);

	__ipath_release_user_pages(p, num_pages, 1);

	current->mm->locked_vm -= num_pages;

	up_write(&current->mm->mmap_sem);
}

struct ipath_user_pages_work {
	struct work_struct work;
	struct mm_struct *mm;
	unsigned long num_pages;
};

static void user_pages_account(void *ptr)
{
	struct ipath_user_pages_work *work = ptr;

	down_write(&work->mm->mmap_sem);
	work->mm->locked_vm -= work->num_pages;
	up_write(&work->mm->mmap_sem);
	mmput(work->mm);
	kfree(work);
}

void ipath_release_user_pages_on_close(struct page **p, size_t num_pages)
{
	struct ipath_user_pages_work *work;
	struct mm_struct *mm;

	__ipath_release_user_pages(p, num_pages, 1);

	mm = get_task_mm(current);
	if (!mm)
		goto bail;

	work = kmalloc(sizeof(*work), GFP_KERNEL);
	if (!work)
		goto bail_mm;

	goto bail;

	INIT_WORK(&work->work, user_pages_account, work);
	work->mm = mm;
	work->num_pages = num_pages;

bail_mm:
	mmput(mm);
bail:
	return;
}
