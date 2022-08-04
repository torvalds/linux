/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 1994-2002 Russell King
 *  Copyright (c) 2003, 2020 ARM Limited
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
 *                    2 MiB aligned.
 *
 * Called from head.S, which expects the following registers to be preserved:
 *   r1 = machine no, r2 = atags or dtb,
 *   r8 = phys_offset, r9 = cpuid, r10 = procinfo
 */
	__HEAD
ENTRY(__fixup_pv_table)
	mov	r0, r8, lsr #PAGE_SHIFT	@ convert to PFN
	str_l	r0, __pv_phys_pfn_offset, r3

	adr_l	r0, __pv_offset
	subs	r3, r8, #PAGE_OFFSET	@ PHYS_OFFSET - PAGE_OFFSET
	mvn	ip, #0
	strcc	ip, [r0, #HIGH_OFFSET]	@ save to __pv_offset high bits
	str	r3, [r0, #LOW_OFFSET]	@ save to __pv_offset low bits

	mov	r0, r3, lsr #21		@ constant for add/sub instructions
	teq	r3, r0, lsl #21 	@ must be 2 MiB aligned
	bne	0f

	adr_l	r4, __pv_table_begin
	adr_l	r5, __pv_table_end
	b	__fixup_a_pv_table

0:	mov	r0, r0			@ deadloop on error
	b	0b
ENDPROC(__fixup_pv_table)

	.text
__fixup_a_pv_table:
	adr_l	r6, __pv_offset
	ldr	r0, [r6, #HIGH_OFFSET]	@ pv_offset high word
	ldr	r6, [r6, #LOW_OFFSET]	@ pv_offset low word
	cmn	r0, #1
#ifdef CONFIG_THUMB2_KERNEL
	@
	@ The Thumb-2 versions of the patchable sequences are
	@
	@ phys-to-virt:			movw	<reg>, #offset<31:21>
	@				lsl	<reg>, #21
	@				sub	<VA>, <PA>, <reg>
	@
	@ virt-to-phys (non-LPAE):	movw	<reg>, #offset<31:21>
	@				lsl	<reg>, #21
	@				add	<PA>, <VA>, <reg>
	@
	@ virt-to-phys (LPAE):		movw	<reg>, #offset<31:21>
	@				lsl	<reg>, #21
	@				adds	<PAlo>, <VA>, <reg>
	@				mov	<PAhi>, #offset<39:32>
	@				adc	<PAhi>, <PAhi>, #0
	@
	@ In the non-LPAE case, all patchable instructions are MOVW
	@ instructions, where we need to patch in the offset into the
	@ second halfword of the opcode (the 16-bit immediate is encoded
	@ as imm4:i:imm3:imm8)
	@
	@       15       11 10  9           4 3    0  15  14  12 11 8 7    0
	@      +-----------+---+-------------+------++---+------+----+------+
	@ MOVW | 1 1 1 1 0 | i | 1 0 0 1 0 0 | imm4 || 0 | imm3 | Rd | imm8 |
	@      +-----------+---+-------------+------++---+------+----+------+
	@
	@ In the LPAE case, we also need to patch in the high word of the
	@ offset into the immediate field of the MOV instruction, or patch it
	@ to a MVN instruction if the offset is negative. In this case, we
	@ need to inspect the first halfword of the opcode, to check whether
	@ it is MOVW or MOV/MVN, and to perform the MOV to MVN patching if
	@ needed. The encoding of the immediate is rather complex for values
	@ of i:imm3 != 0b0000, but fortunately, we never need more than 8 lower
	@ order bits, which can be patched into imm8 directly (and i:imm3
	@ cleared)
	@
	@      15       11 10  9        5         0  15  14  12 11 8 7    0
	@     +-----------+---+---------------------++---+------+----+------+
	@ MOV | 1 1 1 1 0 | i | 0 0 0 1 0 0 1 1 1 1 || 0 | imm3 | Rd | imm8 |
	@ MVN | 1 1 1 1 0 | i | 0 0 0 1 1 0 1 1 1 1 || 0 | imm3 | Rd | imm8 |
	@     +-----------+---+---------------------++---+------+----+------+
	@
	moveq	r0, #0x200000		@ set bit 21, mov to mvn instruction
	lsrs	r3, r6, #29		@ isolate top 3 bits of displacement
	ubfx	r6, r6, #21, #8		@ put bits 28:21 into the MOVW imm8 field
	bfi	r6, r3, #12, #3		@ put bits 31:29 into the MOVW imm3 field
	b	.Lnext
.Lloop:	add	r7, r4
	adds	r4, #4			@ clears Z flag
#ifdef CONFIG_ARM_LPAE
	ldrh	ip, [r7]
ARM_BE8(rev16	ip, ip)
	tst	ip, #0x200		@ MOVW has bit 9 set, MVN has it clear
	bne	0f			@ skip to MOVW handling (Z flag is clear)
	bic	ip, #0x20		@ clear bit 5 (MVN -> MOV)
	orr	ip, ip, r0, lsr #16	@ MOV -> MVN if offset < 0
ARM_BE8(rev16	ip, ip)
	strh	ip, [r7]
	@ Z flag is set
0:
#endif
	ldrh	ip, [r7, #2]
ARM_BE8(rev16	ip, ip)
	and	ip, #0xf00		@ clear everything except Rd field
	orreq	ip, r0			@ Z flag set -> MOV/MVN -> patch in high bits
	orrne	ip, r6			@ Z flag clear -> MOVW -> patch in low bits
ARM_BE8(rev16	ip, ip)
	strh	ip, [r7, #2]
#else
#ifdef CONFIG_CPU_ENDIAN_BE8
@ in BE8, we load data in BE, but instructions still in LE
#define PV_BIT24	0x00000001
#define PV_IMM8_MASK	0xff000000
#define PV_IMMR_MSB	0x00080000
#else
#define PV_BIT24	0x01000000
#define PV_IMM8_MASK	0x000000ff
#define PV_IMMR_MSB	0x00000800
#endif

	@
	@ The ARM versions of the patchable sequences are
	@
	@ phys-to-virt:			sub	<VA>, <PA>, #offset<31:24>, lsl #24
	@				sub	<VA>, <PA>, #offset<23:16>, lsl #16
	@
	@ virt-to-phys (non-LPAE):	add	<PA>, <VA>, #offset<31:24>, lsl #24
	@				add	<PA>, <VA>, #offset<23:16>, lsl #16
	@
	@ virt-to-phys (LPAE):		movw	<reg>, #offset<31:20>
	@				adds	<PAlo>, <VA>, <reg>, lsl #20
	@				mov	<PAhi>, #offset<39:32>
	@				adc	<PAhi>, <PAhi>, #0
	@
	@ In the non-LPAE case, all patchable instructions are ADD or SUB
	@ instructions, where we need to patch in the offset into the
	@ immediate field of the opcode, which is emitted with the correct
	@ rotation value. (The effective value of the immediate is imm12<7:0>
	@ rotated right by [2 * imm12<11:8>] bits)
	@
	@      31   28 27      23 22  20 19  16 15  12 11    0
	@      +------+-----------------+------+------+-------+
	@  ADD | cond | 0 0 1 0 1 0 0 0 |  Rn  |  Rd  | imm12 |
	@  SUB | cond | 0 0 1 0 0 1 0 0 |  Rn  |  Rd  | imm12 |
	@  MOV | cond | 0 0 1 1 1 0 1 0 |  Rn  |  Rd  | imm12 |
	@  MVN | cond | 0 0 1 1 1 1 1 0 |  Rn  |  Rd  | imm12 |
	@      +------+-----------------+------+------+-------+
	@
	@ In the LPAE case, we use a MOVW instruction to carry the low offset
	@ word, and patch in the high word of the offset into the immediate
	@ field of the subsequent MOV instruction, or patch it to a MVN
	@ instruction if the offset is negative. We can distinguish MOVW
	@ instructions based on bits 23:22 of the opcode, and ADD/SUB can be
	@ distinguished from MOV/MVN (all using the encodings above) using
	@ bit 24.
	@
	@      31   28 27      23 22  20 19  16 15  12 11    0
	@      +------+-----------------+------+------+-------+
	@ MOVW | cond | 0 0 1 1 0 0 0 0 | imm4 |  Rd  | imm12 |
	@      +------+-----------------+------+------+-------+
	@
	moveq	r0, #0x400000		@ set bit 22, mov to mvn instruction
	mov	r3, r6, lsr #16		@ put offset bits 31-16 into r3
	mov	r6, r6, lsr #24		@ put offset bits 31-24 into r6
	and	r3, r3, #0xf0		@ only keep offset bits 23-20 in r3
	b	.Lnext
.Lloop:	ldr	ip, [r7, r4]
#ifdef CONFIG_ARM_LPAE
	tst	ip, #PV_BIT24		@ ADD/SUB have bit 24 clear
	beq	1f
ARM_BE8(rev	ip, ip)
	tst	ip, #0xc00000		@ MOVW has bits 23:22 clear
	bic	ip, ip, #0x400000	@ clear bit 22
	bfc	ip, #0, #12		@ clear imm12 field of MOV[W] instruction
	orreq	ip, ip, r6, lsl #4	@ MOVW -> mask in offset bits 31-24
	orreq	ip, ip, r3, lsr #4	@ MOVW -> mask in offset bits 23-20
	orrne	ip, ip, r0		@ MOV  -> mask in offset bits 7-0 (or bit 22)
ARM_BE8(rev	ip, ip)
	b	2f
1:
#endif
	tst	ip, #PV_IMMR_MSB		@ rotation value >= 16 ?
	bic	ip, ip, #PV_IMM8_MASK
	orreq	ip, ip, r6 ARM_BE8(, lsl #24)	@ mask in offset bits 31-24
	orrne	ip, ip, r3 ARM_BE8(, lsl #24)	@ mask in offset bits 23-20
2:
	str	ip, [r7, r4]
	add	r4, r4, #4
#endif

.Lnext:
	cmp	r4, r5
	ldrcc	r7, [r4]		@ use branch for delay slot
	bcc	.Lloop
	ret	lr
ENDPROC(__fixup_a_pv_table)

ENTRY(fixup_pv_table)
	stmfd	sp!, {r4 - r7, lr}
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
