/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2015 Russell King
 *
 * This assembly is required to safely remap the physical address space
 * for Keystone 2
 */
#include <linux/linkage.h>
#include <linux/pgtable.h>
#include <asm/asm-offsets.h>
#include <asm/cp15.h>
#include <asm/page.h>

	.section ".idmap.text", "ax"

#define L1_ORDER 3
#define L2_ORDER 3

ENTRY(lpae_pgtables_remap_asm)
	stmfd	sp!, {r4-r8, lr}

	mrc	p15, 0, r8, c1, c0, 0		@ read control reg
	bic	ip, r8, #CR_M			@ disable caches and MMU
	mcr	p15, 0, ip, c1, c0, 0
	dsb
	isb

	/* Update level 2 entries covering the kernel */
	ldr	r6, =(_end - 1)
	add	r7, r2, #0x1000
	add	r6, r7, r6, lsr #SECTION_SHIFT - L2_ORDER
	add	r7, r7, #KERNEL_OFFSET >> (SECTION_SHIFT - L2_ORDER)
1:	ldrd	r4, r5, [r7]
	adds	r4, r4, r0
	adc	r5, r5, r1
	strd	r4, r5, [r7], #1 << L2_ORDER
	cmp	r7, r6
	bls	1b

	/* Update level 2 entries for the boot data */
	add	r7, r2, #0x1000
	movw	r3, #FDT_FIXED_BASE >> (SECTION_SHIFT - L2_ORDER)
	add	r7, r7, r3
	ldrd	r4, r5, [r7]
	adds	r4, r4, r0
	adc	r5, r5, r1
	strd	r4, r5, [r7], #1 << L2_ORDER
	ldrd	r4, r5, [r7]
	adds	r4, r4, r0
	adc	r5, r5, r1
	strd	r4, r5, [r7]

	/* Update level 1 entries */
	mov	r6, #4
	mov	r7, r2
2:	ldrd	r4, r5, [r7]
	adds	r4, r4, r0
	adc	r5, r5, r1
	strd	r4, r5, [r7], #1 << L1_ORDER
	subs	r6, r6, #1
	bne	2b

	mrrc	p15, 0, r4, r5, c2		@ read TTBR0
	adds	r4, r4, r0			@ update physical address
	adc	r5, r5, r1
	mcrr	p15, 0, r4, r5, c2		@ write back TTBR0
	mrrc	p15, 1, r4, r5, c2		@ read TTBR1
	adds	r4, r4, r0			@ update physical address
	adc	r5, r5, r1
	mcrr	p15, 1, r4, r5, c2		@ write back TTBR1

	dsb

	mov	ip, #0
	mcr	p15, 0, ip, c7, c5, 0		@ I+BTB cache invalidate
	mcr	p15, 0, ip, c8, c7, 0		@ local_flush_tlb_all()
	dsb
	isb

	mcr	p15, 0, r8, c1, c0, 0		@ re-enable MMU
	dsb
	isb

	ldmfd	sp!, {r4-r8, pc}
ENDPROC(lpae_pgtables_remap_asm)
