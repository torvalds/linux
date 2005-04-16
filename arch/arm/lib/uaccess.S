/*
 *  linux/arch/arm/lib/uaccess.S
 *
 *  Copyright (C) 1995, 1996,1997,1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Routines to block copy data to/from user memory
 *   These are highly optimised both for the 4k page size
 *   and for various alignments.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
#include <asm/errno.h>

		.text

#define PAGE_SHIFT 12

/* Prototype: int __arch_copy_to_user(void *to, const char *from, size_t n)
 * Purpose  : copy a block to user memory from kernel memory
 * Params   : to   - user memory
 *          : from - kernel memory
 *          : n    - number of bytes to copy
 * Returns  : Number of bytes NOT copied.
 */

.c2u_dest_not_aligned:
		rsb	ip, ip, #4
		cmp	ip, #2
		ldrb	r3, [r1], #1
USER(		strbt	r3, [r0], #1)			@ May fault
		ldrgeb	r3, [r1], #1
USER(		strgebt	r3, [r0], #1)			@ May fault
		ldrgtb	r3, [r1], #1
USER(		strgtbt	r3, [r0], #1)			@ May fault
		sub	r2, r2, ip
		b	.c2u_dest_aligned

ENTRY(__arch_copy_to_user)
		stmfd	sp!, {r2, r4 - r7, lr}
		cmp	r2, #4
		blt	.c2u_not_enough
	PLD(	pld	[r1, #0]		)
	PLD(	pld	[r0, #0]		)
		ands	ip, r0, #3
		bne	.c2u_dest_not_aligned
.c2u_dest_aligned:

		ands	ip, r1, #3
		bne	.c2u_src_not_aligned
/*
 * Seeing as there has to be at least 8 bytes to copy, we can
 * copy one word, and force a user-mode page fault...
 */

.c2u_0fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.c2u_0nowords
		ldr	r3, [r1], #4
USER(		strt	r3, [r0], #4)			@ May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT	@ On each page, use a ld/st??t instruction
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.c2u_0fupi
/*
 * ip = max no. of bytes to copy before needing another "strt" insn
 */
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #32
		blt	.c2u_0rem8lp
	PLD(	pld	[r1, #28]		)
	PLD(	pld	[r0, #28]		)
	PLD(	subs	ip, ip, #64			)
	PLD(	blt	.c2u_0cpynopld		)
	PLD(	pld	[r1, #60]		)
	PLD(	pld	[r0, #60]		)

.c2u_0cpy8lp:
	PLD(	pld	[r1, #92]		)
	PLD(	pld	[r0, #92]		)
.c2u_0cpynopld:	ldmia	r1!, {r3 - r6}
		stmia	r0!, {r3 - r6}			@ Shouldnt fault
		ldmia	r1!, {r3 - r6}
		subs	ip, ip, #32
		stmia	r0!, {r3 - r6}			@ Shouldnt fault
		bpl	.c2u_0cpy8lp
	PLD(	cmn	ip, #64			)
	PLD(	bge	.c2u_0cpynopld		)
	PLD(	add	ip, ip, #64		)

.c2u_0rem8lp:	cmn	ip, #16
		ldmgeia	r1!, {r3 - r6}
		stmgeia	r0!, {r3 - r6}			@ Shouldnt fault
		tst	ip, #8
		ldmneia	r1!, {r3 - r4}
		stmneia	r0!, {r3 - r4}			@ Shouldnt fault
		tst	ip, #4
		ldrne	r3, [r1], #4
		strnet	r3, [r0], #4			@ Shouldnt fault
		ands	ip, ip, #3
		beq	.c2u_0fupi
.c2u_0nowords:	teq	ip, #0
		beq	.c2u_finished
.c2u_nowords:	cmp	ip, #2
		ldrb	r3, [r1], #1
USER(		strbt	r3, [r0], #1)			@ May fault
		ldrgeb	r3, [r1], #1
USER(		strgebt	r3, [r0], #1)			@ May fault
		ldrgtb	r3, [r1], #1
USER(		strgtbt	r3, [r0], #1)			@ May fault
		b	.c2u_finished

.c2u_not_enough:
		movs	ip, r2
		bne	.c2u_nowords
.c2u_finished:	mov	r0, #0
		LOADREGS(fd,sp!,{r2, r4 - r7, pc})

.c2u_src_not_aligned:
		bic	r1, r1, #3
		ldr	r7, [r1], #4
		cmp	ip, #2
		bgt	.c2u_3fupi
		beq	.c2u_2fupi
.c2u_1fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.c2u_1nowords
		mov	r3, r7, pull #8
		ldr	r7, [r1], #4
		orr	r3, r3, r7, push #24
USER(		strt	r3, [r0], #4)			@ May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.c2u_1fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.c2u_1rem8lp
	PLD(	pld	[r1, #12]		)
	PLD(	pld	[r0, #12]		)
	PLD(	subs	ip, ip, #32		)
	PLD(	blt	.c2u_1cpynopld		)
	PLD(	pld	[r1, #28]		)
	PLD(	pld	[r0, #28]		)

.c2u_1cpy8lp:
	PLD(	pld	[r1, #44]		)
	PLD(	pld	[r0, #44]		)
.c2u_1cpynopld:	mov	r3, r7, pull #8
		ldmia	r1!, {r4 - r7}
		subs	ip, ip, #16
		orr	r3, r3, r4, push #24
		mov	r4, r4, pull #8
		orr	r4, r4, r5, push #24
		mov	r5, r5, pull #8
		orr	r5, r5, r6, push #24
		mov	r6, r6, pull #8
		orr	r6, r6, r7, push #24
		stmia	r0!, {r3 - r6}			@ Shouldnt fault
		bpl	.c2u_1cpy8lp
	PLD(	cmn	ip, #32			)
	PLD(	bge	.c2u_1cpynopld		)
	PLD(	add	ip, ip, #32		)

.c2u_1rem8lp:	tst	ip, #8
		movne	r3, r7, pull #8
		ldmneia	r1!, {r4, r7}
		orrne	r3, r3, r4, push #24
		movne	r4, r4, pull #8
		orrne	r4, r4, r7, push #24
		stmneia	r0!, {r3 - r4}			@ Shouldnt fault
		tst	ip, #4
		movne	r3, r7, pull #8
		ldrne	r7, [r1], #4
		orrne	r3, r3, r7, push #24
		strnet	r3, [r0], #4			@ Shouldnt fault
		ands	ip, ip, #3
		beq	.c2u_1fupi
.c2u_1nowords:	mov	r3, r7, get_byte_1
		teq	ip, #0
		beq	.c2u_finished
		cmp	ip, #2
USER(		strbt	r3, [r0], #1)			@ May fault
		movge	r3, r7, get_byte_2
USER(		strgebt	r3, [r0], #1)			@ May fault
		movgt	r3, r7, get_byte_3
USER(		strgtbt	r3, [r0], #1)			@ May fault
		b	.c2u_finished

.c2u_2fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.c2u_2nowords
		mov	r3, r7, pull #16
		ldr	r7, [r1], #4
		orr	r3, r3, r7, push #16
USER(		strt	r3, [r0], #4)			@ May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.c2u_2fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.c2u_2rem8lp
	PLD(	pld	[r1, #12]		)
	PLD(	pld	[r0, #12]		)
	PLD(	subs	ip, ip, #32		)
	PLD(	blt	.c2u_2cpynopld		)
	PLD(	pld	[r1, #28]		)
	PLD(	pld	[r0, #28]		)

.c2u_2cpy8lp:
	PLD(	pld	[r1, #44]		)
	PLD(	pld	[r0, #44]		)
.c2u_2cpynopld:	mov	r3, r7, pull #16
		ldmia	r1!, {r4 - r7}
		subs	ip, ip, #16
		orr	r3, r3, r4, push #16
		mov	r4, r4, pull #16
		orr	r4, r4, r5, push #16
		mov	r5, r5, pull #16
		orr	r5, r5, r6, push #16
		mov	r6, r6, pull #16
		orr	r6, r6, r7, push #16
		stmia	r0!, {r3 - r6}			@ Shouldnt fault
		bpl	.c2u_2cpy8lp
	PLD(	cmn	ip, #32			)
	PLD(	bge	.c2u_2cpynopld		)
	PLD(	add	ip, ip, #32		)

.c2u_2rem8lp:	tst	ip, #8
		movne	r3, r7, pull #16
		ldmneia	r1!, {r4, r7}
		orrne	r3, r3, r4, push #16
		movne	r4, r4, pull #16
		orrne	r4, r4, r7, push #16
		stmneia	r0!, {r3 - r4}			@ Shouldnt fault
		tst	ip, #4
		movne	r3, r7, pull #16
		ldrne	r7, [r1], #4
		orrne	r3, r3, r7, push #16
		strnet	r3, [r0], #4			@ Shouldnt fault
		ands	ip, ip, #3
		beq	.c2u_2fupi
.c2u_2nowords:	mov	r3, r7, get_byte_2
		teq	ip, #0
		beq	.c2u_finished
		cmp	ip, #2
USER(		strbt	r3, [r0], #1)			@ May fault
		movge	r3, r7, get_byte_3
USER(		strgebt	r3, [r0], #1)			@ May fault
		ldrgtb	r3, [r1], #0
USER(		strgtbt	r3, [r0], #1)			@ May fault
		b	.c2u_finished

.c2u_3fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.c2u_3nowords
		mov	r3, r7, pull #24
		ldr	r7, [r1], #4
		orr	r3, r3, r7, push #8
USER(		strt	r3, [r0], #4)			@ May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.c2u_3fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.c2u_3rem8lp
	PLD(	pld	[r1, #12]		)
	PLD(	pld	[r0, #12]		)
	PLD(	subs	ip, ip, #32		)
	PLD(	blt	.c2u_3cpynopld		)
	PLD(	pld	[r1, #28]		)
	PLD(	pld	[r0, #28]		)

.c2u_3cpy8lp:
	PLD(	pld	[r1, #44]		)
	PLD(	pld	[r0, #44]		)
.c2u_3cpynopld:	mov	r3, r7, pull #24
		ldmia	r1!, {r4 - r7}
		subs	ip, ip, #16
		orr	r3, r3, r4, push #8
		mov	r4, r4, pull #24
		orr	r4, r4, r5, push #8
		mov	r5, r5, pull #24
		orr	r5, r5, r6, push #8
		mov	r6, r6, pull #24
		orr	r6, r6, r7, push #8
		stmia	r0!, {r3 - r6}			@ Shouldnt fault
		bpl	.c2u_3cpy8lp
	PLD(	cmn	ip, #32			)
	PLD(	bge	.c2u_3cpynopld		)
	PLD(	add	ip, ip, #32		)

.c2u_3rem8lp:	tst	ip, #8
		movne	r3, r7, pull #24
		ldmneia	r1!, {r4, r7}
		orrne	r3, r3, r4, push #8
		movne	r4, r4, pull #24
		orrne	r4, r4, r7, push #8
		stmneia	r0!, {r3 - r4}			@ Shouldnt fault
		tst	ip, #4
		movne	r3, r7, pull #24
		ldrne	r7, [r1], #4
		orrne	r3, r3, r7, push #8
		strnet	r3, [r0], #4			@ Shouldnt fault
		ands	ip, ip, #3
		beq	.c2u_3fupi
.c2u_3nowords:	mov	r3, r7, get_byte_3
		teq	ip, #0
		beq	.c2u_finished
		cmp	ip, #2
USER(		strbt	r3, [r0], #1)			@ May fault
		ldrgeb	r3, [r1], #1
USER(		strgebt	r3, [r0], #1)			@ May fault
		ldrgtb	r3, [r1], #0
USER(		strgtbt	r3, [r0], #1)			@ May fault
		b	.c2u_finished

		.section .fixup,"ax"
		.align	0
9001:		LOADREGS(fd,sp!, {r0, r4 - r7, pc})
		.previous

/* Prototype: unsigned long __arch_copy_from_user(void *to,const void *from,unsigned long n);
 * Purpose  : copy a block from user memory to kernel memory
 * Params   : to   - kernel memory
 *          : from - user memory
 *          : n    - number of bytes to copy
 * Returns  : Number of bytes NOT copied.
 */
.cfu_dest_not_aligned:
		rsb	ip, ip, #4
		cmp	ip, #2
USER(		ldrbt	r3, [r1], #1)			@ May fault
		strb	r3, [r0], #1
USER(		ldrgebt	r3, [r1], #1)			@ May fault
		strgeb	r3, [r0], #1
USER(		ldrgtbt	r3, [r1], #1)			@ May fault
		strgtb	r3, [r0], #1
		sub	r2, r2, ip
		b	.cfu_dest_aligned

ENTRY(__arch_copy_from_user)
		stmfd	sp!, {r0, r2, r4 - r7, lr}
		cmp	r2, #4
		blt	.cfu_not_enough
	PLD(	pld	[r1, #0]		)
	PLD(	pld	[r0, #0]		)
		ands	ip, r0, #3
		bne	.cfu_dest_not_aligned
.cfu_dest_aligned:
		ands	ip, r1, #3
		bne	.cfu_src_not_aligned
/*
 * Seeing as there has to be at least 8 bytes to copy, we can
 * copy one word, and force a user-mode page fault...
 */

.cfu_0fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.cfu_0nowords
USER(		ldrt	r3, [r1], #4)
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT	@ On each page, use a ld/st??t instruction
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.cfu_0fupi
/*
 * ip = max no. of bytes to copy before needing another "strt" insn
 */
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #32
		blt	.cfu_0rem8lp
	PLD(	pld	[r1, #28]		)
	PLD(	pld	[r0, #28]		)
	PLD(	subs	ip, ip, #64			)
	PLD(	blt	.cfu_0cpynopld		)
	PLD(	pld	[r1, #60]		)
	PLD(	pld	[r0, #60]		)

.cfu_0cpy8lp:
	PLD(	pld	[r1, #92]		)
	PLD(	pld	[r0, #92]		)
.cfu_0cpynopld:	ldmia	r1!, {r3 - r6}			@ Shouldnt fault
		stmia	r0!, {r3 - r6}
		ldmia	r1!, {r3 - r6}			@ Shouldnt fault
		subs	ip, ip, #32
		stmia	r0!, {r3 - r6}
		bpl	.cfu_0cpy8lp
	PLD(	cmn	ip, #64			)
	PLD(	bge	.cfu_0cpynopld		)
	PLD(	add	ip, ip, #64		)

.cfu_0rem8lp:	cmn	ip, #16
		ldmgeia	r1!, {r3 - r6}			@ Shouldnt fault
		stmgeia	r0!, {r3 - r6}
		tst	ip, #8
		ldmneia	r1!, {r3 - r4}			@ Shouldnt fault
		stmneia	r0!, {r3 - r4}
		tst	ip, #4
		ldrnet	r3, [r1], #4			@ Shouldnt fault
		strne	r3, [r0], #4
		ands	ip, ip, #3
		beq	.cfu_0fupi
.cfu_0nowords:	teq	ip, #0
		beq	.cfu_finished
.cfu_nowords:	cmp	ip, #2
USER(		ldrbt	r3, [r1], #1)			@ May fault
		strb	r3, [r0], #1
USER(		ldrgebt	r3, [r1], #1)			@ May fault
		strgeb	r3, [r0], #1
USER(		ldrgtbt	r3, [r1], #1)			@ May fault
		strgtb	r3, [r0], #1
		b	.cfu_finished

.cfu_not_enough:
		movs	ip, r2
		bne	.cfu_nowords
.cfu_finished:	mov	r0, #0
		add	sp, sp, #8
		LOADREGS(fd,sp!,{r4 - r7, pc})

.cfu_src_not_aligned:
		bic	r1, r1, #3
USER(		ldrt	r7, [r1], #4)			@ May fault
		cmp	ip, #2
		bgt	.cfu_3fupi
		beq	.cfu_2fupi
.cfu_1fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.cfu_1nowords
		mov	r3, r7, pull #8
USER(		ldrt	r7, [r1], #4)			@ May fault
		orr	r3, r3, r7, push #24
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.cfu_1fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.cfu_1rem8lp
	PLD(	pld	[r1, #12]		)
	PLD(	pld	[r0, #12]		)
	PLD(	subs	ip, ip, #32		)
	PLD(	blt	.cfu_1cpynopld		)
	PLD(	pld	[r1, #28]		)
	PLD(	pld	[r0, #28]		)

.cfu_1cpy8lp:
	PLD(	pld	[r1, #44]		)
	PLD(	pld	[r0, #44]		)
.cfu_1cpynopld:	mov	r3, r7, pull #8
		ldmia	r1!, {r4 - r7}			@ Shouldnt fault
		subs	ip, ip, #16
		orr	r3, r3, r4, push #24
		mov	r4, r4, pull #8
		orr	r4, r4, r5, push #24
		mov	r5, r5, pull #8
		orr	r5, r5, r6, push #24
		mov	r6, r6, pull #8
		orr	r6, r6, r7, push #24
		stmia	r0!, {r3 - r6}
		bpl	.cfu_1cpy8lp
	PLD(	cmn	ip, #32			)
	PLD(	bge	.cfu_1cpynopld		)
	PLD(	add	ip, ip, #32		)

.cfu_1rem8lp:	tst	ip, #8
		movne	r3, r7, pull #8
		ldmneia	r1!, {r4, r7}			@ Shouldnt fault
		orrne	r3, r3, r4, push #24
		movne	r4, r4, pull #8
		orrne	r4, r4, r7, push #24
		stmneia	r0!, {r3 - r4}
		tst	ip, #4
		movne	r3, r7, pull #8
USER(		ldrnet	r7, [r1], #4)			@ May fault
		orrne	r3, r3, r7, push #24
		strne	r3, [r0], #4
		ands	ip, ip, #3
		beq	.cfu_1fupi
.cfu_1nowords:	mov	r3, r7, get_byte_1
		teq	ip, #0
		beq	.cfu_finished
		cmp	ip, #2
		strb	r3, [r0], #1
		movge	r3, r7, get_byte_2
		strgeb	r3, [r0], #1
		movgt	r3, r7, get_byte_3
		strgtb	r3, [r0], #1
		b	.cfu_finished

.cfu_2fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.cfu_2nowords
		mov	r3, r7, pull #16
USER(		ldrt	r7, [r1], #4)			@ May fault
		orr	r3, r3, r7, push #16
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.cfu_2fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.cfu_2rem8lp
	PLD(	pld	[r1, #12]		)
	PLD(	pld	[r0, #12]		)
	PLD(	subs	ip, ip, #32		)
	PLD(	blt	.cfu_2cpynopld		)
	PLD(	pld	[r1, #28]		)
	PLD(	pld	[r0, #28]		)

.cfu_2cpy8lp:
	PLD(	pld	[r1, #44]		)
	PLD(	pld	[r0, #44]		)
.cfu_2cpynopld:	mov	r3, r7, pull #16
		ldmia	r1!, {r4 - r7}			@ Shouldnt fault
		subs	ip, ip, #16
		orr	r3, r3, r4, push #16
		mov	r4, r4, pull #16
		orr	r4, r4, r5, push #16
		mov	r5, r5, pull #16
		orr	r5, r5, r6, push #16
		mov	r6, r6, pull #16
		orr	r6, r6, r7, push #16
		stmia	r0!, {r3 - r6}
		bpl	.cfu_2cpy8lp
	PLD(	cmn	ip, #32			)
	PLD(	bge	.cfu_2cpynopld		)
	PLD(	add	ip, ip, #32		)

.cfu_2rem8lp:	tst	ip, #8
		movne	r3, r7, pull #16
		ldmneia	r1!, {r4, r7}			@ Shouldnt fault
		orrne	r3, r3, r4, push #16
		movne	r4, r4, pull #16
		orrne	r4, r4, r7, push #16
		stmneia	r0!, {r3 - r4}
		tst	ip, #4
		movne	r3, r7, pull #16
USER(		ldrnet	r7, [r1], #4)			@ May fault
		orrne	r3, r3, r7, push #16
		strne	r3, [r0], #4
		ands	ip, ip, #3
		beq	.cfu_2fupi
.cfu_2nowords:	mov	r3, r7, get_byte_2
		teq	ip, #0
		beq	.cfu_finished
		cmp	ip, #2
		strb	r3, [r0], #1
		movge	r3, r7, get_byte_3
		strgeb	r3, [r0], #1
USER(		ldrgtbt	r3, [r1], #0)			@ May fault
		strgtb	r3, [r0], #1
		b	.cfu_finished

.cfu_3fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.cfu_3nowords
		mov	r3, r7, pull #24
USER(		ldrt	r7, [r1], #4)			@ May fault
		orr	r3, r3, r7, push #8
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.cfu_3fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.cfu_3rem8lp
	PLD(	pld	[r1, #12]		)
	PLD(	pld	[r0, #12]		)
	PLD(	subs	ip, ip, #32		)
	PLD(	blt	.cfu_3cpynopld		)
	PLD(	pld	[r1, #28]		)
	PLD(	pld	[r0, #28]		)

.cfu_3cpy8lp:
	PLD(	pld	[r1, #44]		)
	PLD(	pld	[r0, #44]		)
.cfu_3cpynopld:	mov	r3, r7, pull #24
		ldmia	r1!, {r4 - r7}			@ Shouldnt fault
		orr	r3, r3, r4, push #8
		mov	r4, r4, pull #24
		orr	r4, r4, r5, push #8
		mov	r5, r5, pull #24
		orr	r5, r5, r6, push #8
		mov	r6, r6, pull #24
		orr	r6, r6, r7, push #8
		stmia	r0!, {r3 - r6}
		subs	ip, ip, #16
		bpl	.cfu_3cpy8lp
	PLD(	cmn	ip, #32			)
	PLD(	bge	.cfu_3cpynopld		)
	PLD(	add	ip, ip, #32		)

.cfu_3rem8lp:	tst	ip, #8
		movne	r3, r7, pull #24
		ldmneia	r1!, {r4, r7}			@ Shouldnt fault
		orrne	r3, r3, r4, push #8
		movne	r4, r4, pull #24
		orrne	r4, r4, r7, push #8
		stmneia	r0!, {r3 - r4}
		tst	ip, #4
		movne	r3, r7, pull #24
USER(		ldrnet	r7, [r1], #4)			@ May fault
		orrne	r3, r3, r7, push #8
		strne	r3, [r0], #4
		ands	ip, ip, #3
		beq	.cfu_3fupi
.cfu_3nowords:	mov	r3, r7, get_byte_3
		teq	ip, #0
		beq	.cfu_finished
		cmp	ip, #2
		strb	r3, [r0], #1
USER(		ldrgebt	r3, [r1], #1)			@ May fault
		strgeb	r3, [r0], #1
USER(		ldrgtbt	r3, [r1], #1)			@ May fault
		strgtb	r3, [r0], #1
		b	.cfu_finished

		.section .fixup,"ax"
		.align	0
		/*
		 * We took an exception.  r0 contains a pointer to
		 * the byte not copied.
		 */
9001:		ldr	r2, [sp], #4			@ void *to
		sub	r2, r0, r2			@ bytes copied
		ldr	r1, [sp], #4			@ unsigned long count
		subs	r4, r1, r2			@ bytes left to copy
		movne	r1, r4
		blne	__memzero
		mov	r0, r4
		LOADREGS(fd,sp!, {r4 - r7, pc})
		.previous

/* Prototype: int __arch_clear_user(void *addr, size_t sz)
 * Purpose  : clear some user memory
 * Params   : addr - user memory address to clear
 *          : sz   - number of bytes to clear
 * Returns  : number of bytes NOT cleared
 */
ENTRY(__arch_clear_user)
		stmfd	sp!, {r1, lr}
		mov	r2, #0
		cmp	r1, #4
		blt	2f
		ands	ip, r0, #3
		beq	1f
		cmp	ip, #2
USER(		strbt	r2, [r0], #1)
USER(		strlebt	r2, [r0], #1)
USER(		strltbt	r2, [r0], #1)
		rsb	ip, ip, #4
		sub	r1, r1, ip		@  7  6  5  4  3  2  1
1:		subs	r1, r1, #8		@ -1 -2 -3 -4 -5 -6 -7
USER(		strplt	r2, [r0], #4)
USER(		strplt	r2, [r0], #4)
		bpl	1b
		adds	r1, r1, #4		@  3  2  1  0 -1 -2 -3
USER(		strplt	r2, [r0], #4)
2:		tst	r1, #2			@ 1x 1x 0x 0x 1x 1x 0x
USER(		strnebt	r2, [r0], #1)
USER(		strnebt	r2, [r0], #1)
		tst	r1, #1			@ x1 x0 x1 x0 x1 x0 x1
USER(		strnebt	r2, [r0], #1)
		mov	r0, #0
		LOADREGS(fd,sp!, {r1, pc})

		.section .fixup,"ax"
		.align	0
9001:		LOADREGS(fd,sp!, {r0, pc})
		.previous

