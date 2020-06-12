// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/lib/copypage-armv4mc.S
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

#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

#include "mm.h"

#define minicache_pgprot __pgprot(L_PTE_PRESENT | L_PTE_YOUNG | \
				  L_PTE_MT_MINICACHE)

static DEFINE_RAW_SPINLOCK(minicache_lock);

/*
 * ARMv4 mini-dcache optimised copy_user_highpage
 *
 * We flush the destination cache lines just before we write the data into the
 * corresponding address.  Since the Dcache is read-allocate, this removes the
 * Dcache aliasing issue.  The writes will be forwarded to the write buffer,
 * and merged as appropriate.
 *
 * Note: We rely on all ARMv4 processors implementing the "invalidate D line"
 * instruction.  If your processor does not supply this, you have to write your
 * own copy_user_highpage that does the right thing.
 */
static void mc_copy_user_page(void *from, void *to)
{
	int tmp;

	asm volatile ("\
	.syntax unified\n\
	ldmia	%0!, {r2, r3, ip, lr}		@ 4\n\
1:	mcr	p15, 0, %1, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	%1!, {r2, r3, ip, lr}		@ 4\n\
	ldmia	%0!, {r2, r3, ip, lr}		@ 4+1\n\
	stmia	%1!, {r2, r3, ip, lr}		@ 4\n\
	ldmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	mcr	p15, 0, %1, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	%1!, {r2, r3, ip, lr}		@ 4\n\
	ldmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	subs	%2, %2, #1			@ 1\n\
	stmia	%1!, {r2, r3, ip, lr}		@ 4\n\
	ldmiane	%0!, {r2, r3, ip, lr}		@ 4\n\
	bne	1b				@ "
	: "+&r" (from), "+&r" (to), "=&r" (tmp)
	: "2" (PAGE_SIZE / 64)
	: "r2", "r3", "ip", "lr");
}

void v4_mc_copy_user_highpage(struct page *to, struct page *from,
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
 * ARMv4 optimised clear_user_page
 */
void v4_mc_clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *ptr, *kaddr = kmap_atomic(page);
	asm volatile("\
	mov	r1, %2				@ 1\n\
	mov	r2, #0				@ 1\n\
	mov	r3, #0				@ 1\n\
	mov	ip, #0				@ 1\n\
	mov	lr, #0				@ 1\n\
1:	mcr	p15, 0, %0, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	stmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	mcr	p15, 0, %0, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	stmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	subs	r1, r1, #1			@ 1\n\
	bne	1b				@ 1"
	: "=r" (ptr)
	: "0" (kaddr), "I" (PAGE_SIZE / 64)
	: "r1", "r2", "r3", "ip", "lr");
	kunmap_atomic(kaddr);
}

struct cpu_user_fns v4_mc_user_fns __initdata = {
	.cpu_clear_user_highpage = v4_mc_clear_user_highpage,
	.cpu_copy_user_highpage	= v4_mc_copy_user_highpage,
};
