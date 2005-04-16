/*
 *  linux/arch/arm26/lib/memcpy.S
 *
 *  Copyright (C) 1995-1999 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  ASM optimised string functions
 */
#include <linux/linkage.h>
#include <asm/assembler.h>

		.text

#define ENTER	\
		mov	ip,sp	;\
		stmfd	sp!,{r4-r9,fp,ip,lr,pc}	;\
		sub	fp,ip,#4

#define EXIT	\
		LOADREGS(ea, fp, {r4 - r9, fp, sp, pc})

#define EXITEQ	\
		LOADREGS(eqea, fp, {r4 - r9, fp, sp, pc})

/*
 * Prototype: void memcpy(void *to,const void *from,unsigned long n);
 * ARM3: cant use memcopy here!!!
 */
ENTRY(memcpy)
ENTRY(memmove)
		ENTER
		cmp	r1, r0
		bcc	19f
		subs	r2, r2, #4
		blt	6f
		ands	ip, r0, #3
		bne	7f
		ands	ip, r1, #3
		bne	8f

1:		subs	r2, r2, #8
		blt	5f
		subs	r2, r2, #0x14
		blt	3f
2:		ldmia	r1!,{r3 - r9, ip}
		stmia	r0!,{r3 - r9, ip}
		subs	r2, r2, #32
		bge	2b
		cmn	r2, #16
		ldmgeia	r1!, {r3 - r6}
		stmgeia	r0!, {r3 - r6}
		subge	r2, r2, #0x10
3:		adds	r2, r2, #0x14
4:		ldmgeia	r1!, {r3 - r5}
		stmgeia	r0!, {r3 - r5}
		subges	r2, r2, #12
		bge	4b
5:		adds	r2, r2, #8
		blt	6f
		subs	r2, r2, #4
		ldrlt	r3, [r1], #4
		ldmgeia	r1!, {r4, r5}
		strlt	r3, [r0], #4
		stmgeia	r0!, {r4, r5}
		subge	r2, r2, #4

6:		adds	r2, r2, #4
		EXITEQ
		cmp	r2, #2
		ldrb	r3, [r1], #1
		ldrgeb	r4, [r1], #1
		ldrgtb	r5, [r1], #1
		strb	r3, [r0], #1
		strgeb	r4, [r0], #1
		strgtb	r5, [r0], #1
		EXIT

7:		rsb	ip, ip, #4
		cmp	ip, #2
		ldrb	r3, [r1], #1
		ldrgeb	r4, [r1], #1
		ldrgtb	r5, [r1], #1
		strb	r3, [r0], #1
		strgeb	r4, [r0], #1
		strgtb	r5, [r0], #1
		subs	r2, r2, ip
		blt	6b
		ands	ip, r1, #3
		beq	1b

8:		bic	r1, r1, #3
		ldr	r7, [r1], #4
		cmp	ip, #2
		bgt	15f
		beq	11f
		cmp	r2, #12
		blt	10f
		sub	r2, r2, #12
9:		mov	r3, r7, pull #8
		ldmia	r1!, {r4 - r7}
		orr	r3, r3, r4, push #24
		mov	r4, r4, pull #8
		orr	r4, r4, r5, push #24
		mov	r5, r5, pull #8
		orr	r5, r5, r6, push #24
		mov	r6, r6, pull #8
		orr	r6, r6, r7, push #24
		stmia	r0!, {r3 - r6}
		subs	r2, r2, #16
		bge	9b
		adds	r2, r2, #12
		blt	100f
10:		mov	r3, r7, pull #8
		ldr	r7, [r1], #4
		subs	r2, r2, #4
		orr	r3, r3, r7, push #24
		str	r3, [r0], #4
		bge	10b
100:		sub	r1, r1, #3
		b	6b

11:		cmp	r2, #12
		blt	13f		/* */
		sub	r2, r2, #12
12:		mov	r3, r7, pull #16
		ldmia	r1!, {r4 - r7}
		orr	r3, r3, r4, push #16
		mov	r4, r4, pull #16
		orr	r4, r4, r5, push #16
		mov	r5, r5, pull #16
		orr	r5, r5, r6, push #16
		mov	r6, r6, pull #16
		orr	r6, r6, r7, push #16
		stmia	r0!, {r3 - r6}
		subs	r2, r2, #16
		bge	12b
		adds	r2, r2, #12
		blt	14f
13:		mov	r3, r7, pull #16
		ldr	r7, [r1], #4
		subs	r2, r2, #4
		orr	r3, r3, r7, push #16
		str	r3, [r0], #4
		bge	13b
14:		sub	r1, r1, #2
		b	6b

15:		cmp	r2, #12
		blt	17f
		sub	r2, r2, #12
16:		mov	r3, r7, pull #24
		ldmia	r1!, {r4 - r7}
		orr	r3, r3, r4, push #8
		mov	r4, r4, pull #24
		orr	r4, r4, r5, push #8
		mov	r5, r5, pull #24
		orr	r5, r5, r6, push #8
		mov	r6, r6, pull #24
		orr	r6, r6, r7, push #8
		stmia	r0!, {r3 - r6}
		subs	r2, r2, #16
		bge	16b
		adds	r2, r2, #12
		blt	18f
17:		mov	r3, r7, pull #24
		ldr	r7, [r1], #4
		subs	r2, r2, #4
		orr	r3, r3, r7, push #8
		str	r3, [r0], #4
		bge	17b
18:		sub	r1, r1, #1
		b	6b


19:		add	r1, r1, r2
		add	r0, r0, r2
		subs	r2, r2, #4
		blt	24f
		ands	ip, r0, #3
		bne	25f
		ands	ip, r1, #3
		bne	26f

20:		subs	r2, r2, #8
		blt	23f
		subs	r2, r2, #0x14
		blt	22f
21:		ldmdb	r1!, {r3 - r9, ip}
		stmdb	r0!, {r3 - r9, ip}
		subs	r2, r2, #32
		bge	21b
22:		cmn	r2, #16
		ldmgedb	r1!, {r3 - r6}
		stmgedb	r0!, {r3 - r6}
		subge	r2, r2, #16
		adds	r2, r2, #20
		ldmgedb	r1!, {r3 - r5}
		stmgedb	r0!, {r3 - r5}
		subge	r2, r2, #12
23:		adds	r2, r2, #8
		blt	24f
		subs	r2, r2, #4
		ldrlt	r3, [r1, #-4]!
		ldmgedb	r1!, {r4, r5}
		strlt	r3, [r0, #-4]!
		stmgedb	r0!, {r4, r5}
		subge	r2, r2, #4

24:		adds	r2, r2, #4
		EXITEQ
		cmp	r2, #2
		ldrb	r3, [r1, #-1]!
		ldrgeb	r4, [r1, #-1]!
		ldrgtb	r5, [r1, #-1]!
		strb	r3, [r0, #-1]!
		strgeb	r4, [r0, #-1]!
		strgtb	r5, [r0, #-1]!
		EXIT

25:		cmp	ip, #2
		ldrb	r3, [r1, #-1]!
		ldrgeb	r4, [r1, #-1]!
		ldrgtb	r5, [r1, #-1]!
		strb	r3, [r0, #-1]!
		strgeb	r4, [r0, #-1]!
		strgtb	r5, [r0, #-1]!
		subs	r2, r2, ip
		blt	24b
		ands	ip, r1, #3
		beq	20b

26:		bic	r1, r1, #3
		ldr	r3, [r1], #0
		cmp	ip, #2
		blt	34f
		beq	30f
		cmp	r2, #12
		blt	28f
		sub	r2, r2, #12
27:		mov	r7, r3, push #8
		ldmdb	r1!, {r3, r4, r5, r6}
		orr	r7, r7, r6, pull #24
		mov	r6, r6, push #8
		orr	r6, r6, r5, pull #24
		mov	r5, r5, push #8
		orr	r5, r5, r4, pull #24
		mov	r4, r4, push #8
		orr	r4, r4, r3, pull #24
		stmdb	r0!, {r4, r5, r6, r7}
		subs	r2, r2, #16
		bge	27b
		adds	r2, r2, #12
		blt	29f
28:		mov	ip, r3, push #8
		ldr	r3, [r1, #-4]!
		subs	r2, r2, #4
		orr	ip, ip, r3, pull #24
		str	ip, [r0, #-4]!
		bge	28b
29:		add	r1, r1, #3
		b	24b

30:		cmp	r2, #12
		blt	32f
		sub	r2, r2, #12
31:		mov	r7, r3, push #16
		ldmdb	r1!, {r3, r4, r5, r6}
		orr	r7, r7, r6, pull #16
		mov	r6, r6, push #16
		orr	r6, r6, r5, pull #16
		mov	r5, r5, push #16
		orr	r5, r5, r4, pull #16
		mov	r4, r4, push #16
		orr	r4, r4, r3, pull #16
		stmdb	r0!, {r4, r5, r6, r7}
		subs	r2, r2, #16
		bge	31b
		adds	r2, r2, #12
		blt	33f
32:		mov	ip, r3, push #16
		ldr	r3, [r1, #-4]!
		subs	r2, r2, #4
		orr	ip, ip, r3, pull #16
		str	ip, [r0, #-4]!
		bge	32b
33:		add	r1, r1, #2
		b	24b

34:		cmp	r2, #12
		blt	36f
		sub	r2, r2, #12
35:		mov	r7, r3, push #24
		ldmdb	r1!, {r3, r4, r5, r6}
		orr	r7, r7, r6, pull #8
		mov	r6, r6, push #24
		orr	r6, r6, r5, pull #8
		mov	r5, r5, push #24
		orr	r5, r5, r4, pull #8
		mov	r4, r4, push #24
		orr	r4, r4, r3, pull #8
		stmdb	r0!, {r4, r5, r6, r7}
		subs	r2, r2, #16
		bge	35b
		adds	r2, r2, #12
		blt	37f
36:		mov	ip, r3, push #24
		ldr	r3, [r1, #-4]!
		subs	r2, r2, #4
		orr	ip, ip, r3, pull #8
		str	ip, [r0, #-4]!
		bge	36b
37:		add	r1, r1, #1
		b	24b

		.align
