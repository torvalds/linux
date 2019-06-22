// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mm/copypage-v6.c
 *
 *  Copyright (C) 2002 Deep Blue Solutions Ltd, All Rights Reserved.
 */
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/highmem.h>

#include <asm/pgtable.h>
#include <asm/shmparam.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/cachetype.h>

#include "mm.h"

#if SHMLBA > 16384
#error FIX ME
#endif

static DEFINE_RAW_SPINLOCK(v6_lock);

/*
 * Copy the user page.  No aliasing to deal with so we can just
 * attack the kernel's existing mapping of these pages.
 */
static void v6_copy_user_highpage_nonaliasing(struct page *to,
	struct page *from, unsigned long vaddr, struct vm_area_struct *vma)
{
	void *kto, *kfrom;

	kfrom = kmap_atomic(from);
	kto = kmap_atomic(to);
	copy_page(kto, kfrom);
	kunmap_atomic(kto);
	kunmap_atomic(kfrom);
}

/*
 * Clear the user page.  No aliasing to deal with so we can just
 * attack the kernel's existing mapping of this page.
 */
static void v6_clear_user_highpage_nonaliasing(struct page *page, unsigned long vaddr)
{
	void *kaddr = kmap_atomic(page);
	clear_page(kaddr);
	kunmap_atomic(kaddr);
}

/*
 * Discard data in the kernel mapping for the new page.
 * FIXME: needs this MCRR to be supported.
 */
static void discard_old_kernel_data(void *kto)
{
	__asm__("mcrr	p15, 0, %1, %0, c6	@ 0xec401f06"
	   :
	   : "r" (kto),
	     "r" ((unsigned long)kto + PAGE_SIZE - 1)
	   : "cc");
}

/*
 * Copy the page, taking account of the cache colour.
 */
static void v6_copy_user_highpage_aliasing(struct page *to,
	struct page *from, unsigned long vaddr, struct vm_area_struct *vma)
{
	unsigned int offset = CACHE_COLOUR(vaddr);
	unsigned long kfrom, kto;

	if (!test_and_set_bit(PG_dcache_clean, &from->flags))
		__flush_dcache_page(page_mapping_file(from), from);

	/* FIXME: not highmem safe */
	discard_old_kernel_data(page_address(to));

	/*
	 * Now copy the page using the same cache colour as the
	 * pages ultimate destination.
	 */
	raw_spin_lock(&v6_lock);

	kfrom = COPYPAGE_V6_FROM + (offset << PAGE_SHIFT);
	kto   = COPYPAGE_V6_TO + (offset << PAGE_SHIFT);

	set_top_pte(kfrom, mk_pte(from, PAGE_KERNEL));
	set_top_pte(kto, mk_pte(to, PAGE_KERNEL));

	copy_page((void *)kto, (void *)kfrom);

	raw_spin_unlock(&v6_lock);
}

/*
 * Clear the user page.  We need to deal with the aliasing issues,
 * so remap the kernel page into the same cache colour as the user
 * page.
 */
static void v6_clear_user_highpage_aliasing(struct page *page, unsigned long vaddr)
{
	unsigned long to = COPYPAGE_V6_TO + (CACHE_COLOUR(vaddr) << PAGE_SHIFT);

	/* FIXME: not highmem safe */
	discard_old_kernel_data(page_address(page));

	/*
	 * Now clear the page using the same cache colour as
	 * the pages ultimate destination.
	 */
	raw_spin_lock(&v6_lock);

	set_top_pte(to, mk_pte(page, PAGE_KERNEL));
	clear_page((void *)to);

	raw_spin_unlock(&v6_lock);
}

struct cpu_user_fns v6_user_fns __initdata = {
	.cpu_clear_user_highpage = v6_clear_user_highpage_nonaliasing,
	.cpu_copy_user_highpage	= v6_copy_user_highpage_nonaliasing,
};

static int __init v6_userpage_init(void)
{
	if (cache_is_vipt_aliasing()) {
		cpu_user.cpu_clear_user_highpage = v6_clear_user_highpage_aliasing;
		cpu_user.cpu_copy_user_highpage = v6_copy_user_highpage_aliasing;
	}

	return 0;
}

core_initcall(v6_userpage_init);
