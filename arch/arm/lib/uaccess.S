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

.Lc2u_dest_not_aligned:
		rsb	ip, ip, #4
		cmp	ip, #2
		ldrb	r3, [r1], #1
USER(		strbt	r3, [r0], #1)			@ May fault
		ldrgeb	r3, [r1], #1
USER(		strgebt	r3, [r0], #1)			@ May fault
		ldrgtb	r3, [r1], #1
USER(		strgtbt	r3, [r0], #1)			@ May fault
		sub	r2, r2, ip
		b	.Lc2u_dest_aligned

ENTRY(__arch_copy_to_user)
		stmfd	sp!, {r2, r4 - r7, lr}
		cmp	r2, #4
		blt	.Lc2u_not_enough
		ands	ip, r0, #3
		bne	.Lc2u_dest_not_aligned
.Lc2u_dest_aligned:

		ands	ip, r1, #3
		bne	.Lc2u_src_not_aligned
/*
 * Seeing as there has to be at least 8 bytes to copy, we can
 * copy one word, and force a user-mode page fault...
 */

.Lc2u_0fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.Lc2u_0nowords
		ldr	r3, [r1], #4
USER(		strt	r3, [r0], #4)			@ May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT	@ On each page, use a ld/st??t instruction
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.Lc2u_0fupi
/*
 * ip = max no. of bytes to copy before needing another "strt" insn
 */
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #32
		blt	.Lc2u_0rem8lp

.Lc2u_0cpy8lp:	ldmia	r1!, {r3 - r6}
		stmia	r0!, {r3 - r6}			@ Shouldnt fault
		ldmia	r1!, {r3 - r6}
		subs	ip, ip, #32
		stmia	r0!, {r3 - r6}			@ Shouldnt fault
		bpl	.Lc2u_0cpy8lp

.Lc2u_0rem8lp:	cmn	ip, #16
		ldmgeia	r1!, {r3 - r6}
		stmgeia	r0!, {r3 - r6}			@ Shouldnt fault
		tst	ip, #8
		ldmneia	r1!, {r3 - r4}
		stmneia	r0!, {r3 - r4}			@ Shouldnt fault
		tst	ip, #4
		ldrne	r3, [r1], #4
		strnet	r3, [r0], #4			@ Shouldnt fault
		ands	ip, ip, #3
		beq	.Lc2u_0fupi
.Lc2u_0nowords:	teq	ip, #0
		beq	.Lc2u_finished
.Lc2u_nowords:	cmp	ip, #2
		ldrb	r3, [r1], #1
USER(		strbt	r3, [r0], #1)			@ May fault
		ldrgeb	r3, [r1], #1
USER(		strgebt	r3, [r0], #1)			@ May fault
		ldrgtb	r3, [r1], #1
USER(		strgtbt	r3, [r0], #1)			@ May fault
		b	.Lc2u_finished

.Lc2u_not_enough:
		movs	ip, r2
		bne	.Lc2u_nowords
.Lc2u_finished:	mov	r0, #0
		LOADREGS(fd,sp!,{r2, r4 - r7, pc})

.Lc2u_src_not_aligned:
		bic	r1, r1, #3
		ldr	r7, [r1], #4
		cmp	ip, #2
		bgt	.Lc2u_3fupi
		beq	.Lc2u_2fupi
.Lc2u_1fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.Lc2u_1nowords
		mov	r3, r7, pull #8
		ldr	r7, [r1], #4
		orr	r3, r3, r7, push #24
USER(		strt	r3, [r0], #4)			@ May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.Lc2u_1fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.Lc2u_1rem8lp

.Lc2u_1cpy8lp:	mov	r3, r7, pull #8
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
		bpl	.Lc2u_1cpy8lp

.Lc2u_1rem8lp:	tst	ip, #8
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
		beq	.Lc2u_1fupi
.Lc2u_1nowords:	mov	r3, r7, get_byte_1
		teq	ip, #0
		beq	.Lc2u_finished
		cmp	ip, #2
USER(		strbt	r3, [r0], #1)			@ May fault
		movge	r3, r7, get_byte_2
USER(		strgebt	r3, [r0], #1)			@ May fault
		movgt	r3, r7, get_byte_3
USER(		strgtbt	r3, [r0], #1)			@ May fault
		b	.Lc2u_finished

.Lc2u_2fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.Lc2u_2nowords
		mov	r3, r7, pull #16
		ldr	r7, [r1], #4
		orr	r3, r3, r7, push #16
USER(		strt	r3, [r0], #4)			@ May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.Lc2u_2fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.Lc2u_2rem8lp

.Lc2u_2cpy8lp:	mov	r3, r7, pull #16
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
		bpl	.Lc2u_2cpy8lp

.Lc2u_2rem8lp:	tst	ip, #8
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
		beq	.Lc2u_2fupi
.Lc2u_2nowords:	mov	r3, r7, get_byte_2
		teq	ip, #0
		beq	.Lc2u_finished
		cmp	ip, #2
USER(		strbt	r3, [r0], #1)			@ May fault
		movge	r3, r7, get_byte_3
USER(		strgebt	r3, [r0], #1)			@ May fault
		ldrgtb	r3, [r1], #0
USER(		strgtbt	r3, [r0], #1)			@ May fault
		b	.Lc2u_finished

.Lc2u_3fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.Lc2u_3nowords
		mov	r3, r7, pull #24
		ldr	r7, [r1], #4
		orr	r3, r3, r7, push #8
USER(		strt	r3, [r0], #4)			@ May fault
		mov	ip, r0, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.Lc2u_3fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.Lc2u_3rem8lp

.Lc2u_3cpy8lp:	mov	r3, r7, pull #24
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
		bpl	.Lc2u_3cpy8lp

.Lc2u_3rem8lp:	tst	ip, #8
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
		beq	.Lc2u_3fupi
.Lc2u_3nowords:	mov	r3, r7, get_byte_3
		teq	ip, #0
		beq	.Lc2u_finished
		cmp	ip, #2
USER(		strbt	r3, [r0], #1)			@ May fault
		ldrgeb	r3, [r1], #1
USER(		strgebt	r3, [r0], #1)			@ May fault
		ldrgtb	r3, [r1], #0
USER(		strgtbt	r3, [r0], #1)			@ May fault
		b	.Lc2u_finished

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
.Lcfu_dest_not_aligned:
		rsb	ip, ip, #4
		cmp	ip, #2
USER(		ldrbt	r3, [r1], #1)			@ May fault
		strb	r3, [r0], #1
USER(		ldrgebt	r3, [r1], #1)			@ May fault
		strgeb	r3, [r0], #1
USER(		ldrgtbt	r3, [r1], #1)			@ May fault
		strgtb	r3, [r0], #1
		sub	r2, r2, ip
		b	.Lcfu_dest_aligned

ENTRY(__arch_copy_from_user)
		stmfd	sp!, {r0, r2, r4 - r7, lr}
		cmp	r2, #4
		blt	.Lcfu_not_enough
		ands	ip, r0, #3
		bne	.Lcfu_dest_not_aligned
.Lcfu_dest_aligned:
		ands	ip, r1, #3
		bne	.Lcfu_src_not_aligned

/*
 * Seeing as there has to be at least 8 bytes to copy, we can
 * copy one word, and force a user-mode page fault...
 */

.Lcfu_0fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.Lcfu_0nowords
USER(		ldrt	r3, [r1], #4)
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT	@ On each page, use a ld/st??t instruction
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.Lcfu_0fupi
/*
 * ip = max no. of bytes to copy before needing another "strt" insn
 */
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #32
		blt	.Lcfu_0rem8lp

.Lcfu_0cpy8lp:	ldmia	r1!, {r3 - r6}			@ Shouldnt fault
		stmia	r0!, {r3 - r6}
		ldmia	r1!, {r3 - r6}			@ Shouldnt fault
		subs	ip, ip, #32
		stmia	r0!, {r3 - r6}
		bpl	.Lcfu_0cpy8lp

.Lcfu_0rem8lp:	cmn	ip, #16
		ldmgeia	r1!, {r3 - r6}			@ Shouldnt fault
		stmgeia	r0!, {r3 - r6}
		tst	ip, #8
		ldmneia	r1!, {r3 - r4}			@ Shouldnt fault
		stmneia	r0!, {r3 - r4}
		tst	ip, #4
		ldrnet	r3, [r1], #4			@ Shouldnt fault
		strne	r3, [r0], #4
		ands	ip, ip, #3
		beq	.Lcfu_0fupi
.Lcfu_0nowords:	teq	ip, #0
		beq	.Lcfu_finished
.Lcfu_nowords:	cmp	ip, #2
USER(		ldrbt	r3, [r1], #1)			@ May fault
		strb	r3, [r0], #1
USER(		ldrgebt	r3, [r1], #1)			@ May fault
		strgeb	r3, [r0], #1
USER(		ldrgtbt	r3, [r1], #1)			@ May fault
		strgtb	r3, [r0], #1
		b	.Lcfu_finished

.Lcfu_not_enough:
		movs	ip, r2
		bne	.Lcfu_nowords
.Lcfu_finished:	mov	r0, #0
		add	sp, sp, #8
		LOADREGS(fd,sp!,{r4 - r7, pc})

.Lcfu_src_not_aligned:
		bic	r1, r1, #3
USER(		ldrt	r7, [r1], #4)			@ May fault
		cmp	ip, #2
		bgt	.Lcfu_3fupi
		beq	.Lcfu_2fupi
.Lcfu_1fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.Lcfu_1nowords
		mov	r3, r7, pull #8
USER(		ldrt	r7, [r1], #4)			@ May fault
		orr	r3, r3, r7, push #24
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.Lcfu_1fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.Lcfu_1rem8lp

.Lcfu_1cpy8lp:	mov	r3, r7, pull #8
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
		bpl	.Lcfu_1cpy8lp

.Lcfu_1rem8lp:	tst	ip, #8
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
		beq	.Lcfu_1fupi
.Lcfu_1nowords:	mov	r3, r7, get_byte_1
		teq	ip, #0
		beq	.Lcfu_finished
		cmp	ip, #2
		strb	r3, [r0], #1
		movge	r3, r7, get_byte_2
		strgeb	r3, [r0], #1
		movgt	r3, r7, get_byte_3
		strgtb	r3, [r0], #1
		b	.Lcfu_finished

.Lcfu_2fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.Lcfu_2nowords
		mov	r3, r7, pull #16
USER(		ldrt	r7, [r1], #4)			@ May fault
		orr	r3, r3, r7, push #16
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.Lcfu_2fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.Lcfu_2rem8lp


.Lcfu_2cpy8lp:	mov	r3, r7, pull #16
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
		bpl	.Lcfu_2cpy8lp

.Lcfu_2rem8lp:	tst	ip, #8
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
		beq	.Lcfu_2fupi
.Lcfu_2nowords:	mov	r3, r7, get_byte_2
		teq	ip, #0
		beq	.Lcfu_finished
		cmp	ip, #2
		strb	r3, [r0], #1
		movge	r3, r7, get_byte_3
		strgeb	r3, [r0], #1
USER(		ldrgtbt	r3, [r1], #0)			@ May fault
		strgtb	r3, [r0], #1
		b	.Lcfu_finished

.Lcfu_3fupi:	subs	r2, r2, #4
		addmi	ip, r2, #4
		bmi	.Lcfu_3nowords
		mov	r3, r7, pull #24
USER(		ldrt	r7, [r1], #4)			@ May fault
		orr	r3, r3, r7, push #8
		str	r3, [r0], #4
		mov	ip, r1, lsl #32 - PAGE_SHIFT
		rsb	ip, ip, #0
		movs	ip, ip, lsr #32 - PAGE_SHIFT
		beq	.Lcfu_3fupi
		cmp	r2, ip
		movlt	ip, r2
		sub	r2, r2, ip
		subs	ip, ip, #16
		blt	.Lcfu_3rem8lp

.Lcfu_3cpy8lp:	mov	r3, r7, pull #24
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
		bpl	.Lcfu_3cpy8lp

.Lcfu_3rem8lp:	tst	ip, #8
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
		beq	.Lcfu_3fupi
.Lcfu_3nowords:	mov	r3, r7, get_byte_3
		teq	ip, #0
		beq	.Lcfu_finished
		cmp	ip, #2
		strb	r3, [r0], #1
USER(		ldrgebt	r3, [r1], #1)			@ May fault
		strgeb	r3, [r0], #1
USER(		ldrgtbt	r3, [r1], #1)			@ May fault
		strgtb	r3, [r0], #1
		b	.Lcfu_finished

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

