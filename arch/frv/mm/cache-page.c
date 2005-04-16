/* cache-page.c: whole-page cache wrangling functions for MMU linux
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <asm/pgalloc.h>

/*****************************************************************************/
/*
 * DCF takes a virtual address and the page may not currently have one
 * - temporarily hijack a kmap_atomic() slot and attach the page to it
 */
void flush_dcache_page(struct page *page)
{
	unsigned long dampr2;
	void *vaddr;

	dampr2 = __get_DAMPR(2);

	vaddr = kmap_atomic(page, __KM_CACHE);

	frv_dcache_writeback((unsigned long) vaddr, (unsigned long) vaddr + PAGE_SIZE);

	kunmap_atomic(vaddr, __KM_CACHE);

	if (dampr2) {
		__set_DAMPR(2, dampr2);
		__set_IAMPR(2, dampr2);
	}

} /* end flush_dcache_page() */

/*****************************************************************************/
/*
 * ICI takes a virtual address and the page may not currently have one
 * - so we temporarily attach the page to a bit of virtual space so that is can be flushed
 */
void flush_icache_user_range(struct vm_area_struct *vma, struct page *page,
			     unsigned long start, unsigned long len)
{
	unsigned long dampr2;
	void *vaddr;

	dampr2 = __get_DAMPR(2);

	vaddr = kmap_atomic(page, __KM_CACHE);

	start = (start & ~PAGE_MASK) | (unsigned long) vaddr;
	frv_cache_wback_inv(start, start + len);

	kunmap_atomic(vaddr, __KM_CACHE);

	if (dampr2) {
		__set_DAMPR(2, dampr2);
		__set_IAMPR(2, dampr2);
	}

} /* end flush_icache_user_range() */
