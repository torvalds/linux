/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 1994-2002 Russell King
 *  Copyright (c) 2003 ARM Limited
 *  All Rights Reserved
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/page.h>

#ifdef __ARMEB__
#define LOW_OFFSET	0x4
#define HIGH_OFFSET	0x0
#else
#define LOW_OFFSET	0x0
#define HIGH_OFFSET	0x4
#endif

/*
 * __fixup_pv_table - patch the stub instructions with the delta between
 *                    PHYS_OFFSET and PAGE_OFFSET, which is assumed to be
 *                    16MiB aligned.
 *
 * Called from head.S, which expects the following registers to be preserved:
 *   r1 = machine no, r2 = atags or dtb,
 *   r8 = phys_offset, r9 = cpuid, r10 = procinfo
 */
	__HEAD
ENTRY(__fixup_pv_table)
	adr	r0, 1f
	ldmia	r0, {r3-r7}
	mvn	ip, #0
	subs	r3, r0, r3		@ PHYS_OFFSET - PAGE_OFFSET
	add	r4, r4, r3		@ adjust table start address
	add	r5, r5, r3		@ adjust table end address
	add	r6, r6, r3		@ adjust __pv_phys_pfn_offset address
	add	r7, r7, r3		@ adjust __pv_offset address
	mov	r0, r8, lsr #PAGE_SHIFT	@ convert to PFN
	str	r0, [r6]		@ save computed PHYS_OFFSET to __pv_phys_pfn_offset
	strcc	ip, [r7, #HIGH_OFFSET]	@ save to __pv_offset high bits
	mov	r6, r3, lsr #24		@ constant for add/sub instructions
	teq	r3, r6, lsl #24 	@ must be 16MiB aligned
	bne	0f
	str	r3, [r7, #LOW_OFFSET]	@ save to __pv_offset low bits
	b	__fixup_a_pv_table
0:	mov	r0, r0			@ deadloop on error
	b	0b
ENDPROC(__fixup_pv_table)

	.align
1:	.long	.
	.long	__pv_table_begin
	.long	__pv_table_end
2:	.long	__pv_phys_pfn_offset
	.long	__pv_offset

	.text
__fixup_a_pv_table:
	adr	r0, 3f
	ldr	r6, [r0]
	add	r6, r6, r3
	ldr	r0, [r6, #HIGH_OFFSET]	@ pv_offset high word
	ldr	r6, [r6, #LOW_OFFSET]	@ pv_offset low word
	mov	r6, r6, lsr #24
	cmn	r0, #1
#ifdef CONFIG_THUMB2_KERNEL
	moveq	r0, #0x200000		@ set bit 21, mov to mvn instruction
	lsls	r6, #24
	beq	.Lnext
	clz	r7, r6
	lsr	r6, #24
	lsl	r6, r7
	bic	r6, #0x0080
	lsrs	r7, #1
	orrcs	r6, #0x0080
	orr	r6, r6, r7, lsl #12
	orr	r6, #0x4000
	b	.Lnext
.Lloop:	add	r7, r3
	ldrh	ip, [r7, #2]
ARM_BE8(rev16	ip, ip)
	tst	ip, #0x4000
	and	ip, #0x8f00
	orrne	ip, r6			@ mask in offset bits 31-24
	orreq	ip, r0			@ mask in offset bits 7-0
ARM_BE8(rev16	ip, ip)
	strh	ip, [r7, #2]
	bne	.Lnext
	ldrh	ip, [r7]
ARM_BE8(rev16	ip, ip)
	bic	ip, #0x20
	orr	ip, ip, r0, lsr #16
ARM_BE8(rev16	ip, ip)
	strh	ip, [r7]
#else
#ifdef CONFIG_CPU_ENDIAN_BE8
@ in BE8, we load data in BE, but instructions still in LE
#define PV_BIT22	0x00004000
#define PV_IMM8_MASK	0xff000000
#define PV_ROT_MASK	0x000f0000
#else
#define PV_BIT22	0x00400000
#define PV_IMM8_MASK	0x000000ff
#define PV_ROT_MASK	0xf00
#endif

	moveq	r0, #0x400000		@ set bit 22, mov to mvn instruction
	b	.Lnext
.Lloop:	ldr	ip, [r7, r3]
	bic	ip, ip, #PV_IMM8_MASK
	tst	ip, #PV_ROT_MASK		@ check the rotation field
	orrne	ip, ip, r6 ARM_BE8(, lsl #24)	@ mask in offset bits 31-24
	biceq	ip, ip, #PV_BIT22		@ clear bit 22
	orreq	ip, ip, r0 ARM_BE8(, ror #8)	@ mask in offset bits 7-0 (or bit 22)
	str	ip, [r7, r3]
#endif

.Lnext:
	cmp	r4, r5
	ldrcc	r7, [r4], #4		@ use branch for delay slot
	bcc	.Lloop
	ret	lr
ENDPROC(__fixup_a_pv_table)

	.align
3:	.long __pv_offset

ENTRY(fixup_pv_table)
	stmfd	sp!, {r4 - r7, lr}
	mov	r3, #0			@ no offset
	mov	r4, r0			@ r0 = table start
	add	r5, r0, r1		@ r1 = table size
	bl	__fixup_a_pv_table
	ldmfd	sp!, {r4 - r7, pc}
ENDPROC(fixup_pv_table)

	.data
	.align	2
	.globl	__pv_phys_pfn_offset
	.type	__pv_phys_pfn_offset, %object
__pv_phys_pfn_offset:
	.word	0
	.size	__pv_phys_pfn_offset, . -__pv_phys_pfn_offset

	.globl	__pv_offset
	.type	__pv_offset, %object
__pv_offset:
	.quad	0
	.size	__pv_offset, . -__pv_offset
