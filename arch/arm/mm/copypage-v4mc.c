/*
 *  linux/arch/arm/lib/copypage-armv4mc.S
 *
 *  Copyright (C) 1995-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This handles the mini data cache, as found on SA11x0 and XScale
 * processors.  When we copy a user page page, we map it in such a way
 * that accesses to this page will not touch the main data cache, but
 * will be cached in the mini data cache.  This prevents us thrashing
 * the main data cache on page faults.
 */
#include <linux/init.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>

#include "mm.h"

/*
 * 0xffff8000 to 0xffffffff is reserved for any ARM architecture
 * specific hacks for copying pages efficiently.
 */
#define minicache_pgprot __pgprot(L_PTE_PRESENT | L_PTE_YOUNG | \
				  L_PTE_CACHEABLE)

static DEFINE_SPINLOCK(minicache_lock);

/*
 * ARMv4 mini-dcache optimised copy_user_page
 *
 * We flush the destination cache lines just before we write the data into the
 * corresponding address.  Since the Dcache is read-allocate, this removes the
 * Dcache aliasing issue.  The writes will be forwarded to the write buffer,
 * and merged as appropriate.
 *
 * Note: We rely on all ARMv4 processors implementing the "invalidate D line"
 * instruction.  If your processor does not supply this, you have to write your
 * own copy_user_page that does the right thing.
 */
static void __attribute__((naked))
mc_copy_user_page(void *from, void *to)
{
	asm volatile(
	"stmfd	sp!, {r4, lr}			@ 2\n\
	mov	r4, %2				@ 1\n\
	ldmia	%0!, {r2, r3, ip, lr}		@ 4\n\
1:	mcr	p15, 0, %1, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	%1!, {r2, r3, ip, lr}		@ 4\n\
	ldmia	%0!, {r2, r3, ip, lr}		@ 4+1\n\
	stmia	%1!, {r2, r3, ip, lr}		@ 4\n\
	ldmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	mcr	p15, 0, %1, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	%1!, {r2, r3, ip, lr}		@ 4\n\
	ldmia	%0!, {r2, r3, ip, lr}		@ 4\n\
	subs	r4, r4, #1			@ 1\n\
	stmia	%1!, {r2, r3, ip, lr}		@ 4\n\
	ldmneia	%0!, {r2, r3, ip, lr}		@ 4\n\
	bne	1b				@ 1\n\
	ldmfd	sp!, {r4, pc}			@ 3"
	:
	: "r" (from), "r" (to), "I" (PAGE_SIZE / 64));
}

void v4_mc_copy_user_page(void *kto, const void *kfrom, unsigned long vaddr)
{
	spin_lock(&minicache_lock);

	set_pte(TOP_PTE(0xffff8000), pfn_pte(__pa(kfrom) >> PAGE_SHIFT, minicache_pgprot));
	flush_tlb_kernel_page(0xffff8000);

	mc_copy_user_page((void *)0xffff8000, kto);

	spin_unlock(&minicache_lock);
}

/*
 * ARMv4 optimised clear_user_page
 */
void __attribute__((naked))
v4_mc_clear_user_page(void *kaddr, unsigned long vaddr)
{
	asm volatile(
	"str	lr, [sp, #-4]!\n\
	mov	r1, %0				@ 1\n\
	mov	r2, #0				@ 1\n\
	mov	r3, #0				@ 1\n\
	mov	ip, #0				@ 1\n\
	mov	lr, #0				@ 1\n\
1:	mcr	p15, 0, r0, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	r0!, {r2, r3, ip, lr}		@ 4\n\
	stmia	r0!, {r2, r3, ip, lr}		@ 4\n\
	mcr	p15, 0, r0, c7, c6, 1		@ 1   invalidate D line\n\
	stmia	r0!, {r2, r3, ip, lr}		@ 4\n\
	stmia	r0!, {r2, r3, ip, lr}		@ 4\n\
	subs	r1, r1, #1			@ 1\n\
	bne	1b				@ 1\n\
	ldr	pc, [sp], #4"
	:
	: "I" (PAGE_SIZE / 64));
}

struct cpu_user_fns v4_mc_user_fns __initdata = {
	.cpu_clear_user_page	= v4_mc_clear_user_page, 
	.cpu_copy_user_page	= v4_mc_copy_user_page,
};
