// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/lib/copypage-xscale.S
 *
 *  Copyright (C) 1995-2005 Russell King
 *
 * This handles the mini data cache, as found on SA11x0 and XScale
 * processors.  When we copy a user page page, we map it in such a way
 * that accesses to this page will not touch the main data cache, but
 * will be cached in the mini data cache.  This prevents us thrashing
 * the main data cache on page faults.
 */
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/highmem.h>

#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

#include "mm.h"

#define minicache_pgprot __pgprot(L_PTE_PRESENT | L_PTE_YOUNG | \
				  L_PTE_MT_MINICACHE)

static DEFINE_RAW_SPINLOCK(minicache_lock);

/*
 * XScale mini-dcache optimised copy_user_highpage
 *
 * We flush the destination cache lines just before we write the data into the
 * corresponding address.  Since the Dcache is read-allocate, this removes the
 * Dcache aliasing issue.  The writes will be forwarded to the write buffer,
 * and merged as appropriate.
 */
static void mc_copy_user_page(void *from, void *to)
{
	int tmp;

	/*
	 * Strangely enough, best performance is achieved
	 * when prefetching destination as well.  (NP)
	 */
	asm volatile ("\
	pld	[%0, #0]			\n\
	pld	[%0, #32]			\n\
	pld	[%1, #0]			\n\
	pld	[%1, #32]			\n\
1:	pld	[%0, #64]			\n\
	pld	[%0, #96]			\n\
	pld	[%1, #64]			\n\
	pld	[%1, #96]			\n\
2:	ldrd	r2, r3, [%0], #8		\n\
	ldrd	r4, r5, [%0], #8		\n\
	mov	ip, %1				\n\
	strd	r2, r3, [%1], #8		\n\
	ldrd	r2, r3, [%0], #8		\n\
	strd	r4, r5, [%1], #8		\n\
	ldrd	r4, r5, [%0], #8		\n\
	strd	r2, r3, [%1], #8		\n\
	strd	r4, r5, [%1], #8		\n\
	mcr	p15, 0, ip, c7, c10, 1		@ clean D line\n\
	ldrd	r2, r3, [%0], #8		\n\
	mcr	p15, 0, ip, c7, c6, 1		@ invalidate D line\n\
	ldrd	r4, r5, [%0], #8		\n\
	mov	ip, %1				\n\
	strd	r2, r3, [%1], #8		\n\
	ldrd	r2, r3, [%0], #8		\n\
	strd	r4, r5, [%1], #8		\n\
	ldrd	r4, r5, [%0], #8		\n\
	strd	r2, r3, [%1], #8		\n\
	strd	r4, r5, [%1], #8		\n\
	mcr	p15, 0, ip, c7, c10, 1		@ clean D line\n\
	subs	%2, %2, #1			\n\
	mcr	p15, 0, ip, c7, c6, 1		@ invalidate D line\n\
	bgt	1b				\n\
	beq	2b				"
	: "+&r" (from), "+&r" (to), "=&r" (tmp)
	: "2" (PAGE_SIZE / 64 - 1)
	: "r2", "r3", "r4", "r5", "ip");
}

void xscale_mc_copy_user_highpage(struct page *to, struct page *from,
	unsigned long vaddr, struct vm_area_struct *vma)
{
	void *kto = kmap_atomic(to);

	if (!test_and_set_bit(PG_dcache_clean, &from->flags))
		__flush_dcache_page(page_mapping_file(from), from);

	raw_spin_lock(&minicache_lock);

	set_top_pte(COPYPAGE_MINICACHE, mk_pte(from, minicache_pgprot));

	mc_copy_user_page((void *)COPYPAGE_MINICACHE, kto);

	raw_spin_unlock(&minicache_lock);

	kunmap_atomic(kto);
}

/*
 * XScale optimised clear_user_page
 */
void
xscale_mc_clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *ptr, *kaddr = kmap_atomic(page);
	asm volatile(
	"mov	r1, %2				\n\
	mov	r2, #0				\n\
	mov	r3, #0				\n\
1:	mov	ip, %0				\n\
	strd	r2, r3, [%0], #8		\n\
	strd	r2, r3, [%0], #8		\n\
	strd	r2, r3, [%0], #8		\n\
	strd	r2, r3, [%0], #8		\n\
	mcr	p15, 0, ip, c7, c10, 1		@ clean D line\n\
	subs	r1, r1, #1			\n\
	mcr	p15, 0, ip, c7, c6, 1		@ invalidate D line\n\
	bne	1b"
	: "=r" (ptr)
	: "0" (kaddr), "I" (PAGE_SIZE / 32)
	: "r1", "r2", "r3", "ip");
	kunmap_atomic(kaddr);
}

struct cpu_user_fns xscale_mc_user_fns __initdata = {
	.cpu_clear_user_highpage = xscale_mc_clear_user_highpage,
	.cpu_copy_user_highpage	= xscale_mc_copy_user_highpage,
};
