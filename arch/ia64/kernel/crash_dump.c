// SPDX-License-Identifier: GPL-2.0
/*
 *	kernel/crash_dump.c - Memory preserving reboot related code.
 *
 *	Created by: Simon Horman <horms@verge.net.au>
 *	Original code moved from kernel/crash.c
 *	Original code comment copied from the i386 version of this file
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/crash_dump.h>
#include <linux/uio.h>
#include <asm/page.h>

ssize_t copy_oldmem_page(struct iov_iter *iter, unsigned long pfn,
		size_t csize, unsigned long offset)
{
	void  *vaddr;

	if (!csize)
		return 0;
	vaddr = __va(pfn<<PAGE_SHIFT);
	csize = copy_to_iter(vaddr + offset, csize, iter);
	return csize;
}

