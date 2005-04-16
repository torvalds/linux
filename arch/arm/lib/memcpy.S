/*
 *  linux/arch/arm/lib/memcpy.S
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
		stmfd	sp!,{r0,r4-r9,fp,ip,lr,pc}	;\
		sub	fp,ip,#4

#define EXIT	\
		LOADREGS(ea, fp, {r0, r4 - r9, fp, sp, pc})

#define EXITEQ	\
		LOADREGS(eqea, fp, {r0, r4 - r9, fp, sp, pc})

/*
 * Prototype: void memcpy(void *to,const void *from,unsigned long n);
 */
ENTRY(memcpy)
ENTRY(memmove)
		ENTER
		cmp	r1, r0
		bcc	23f
		subs	r2, r2, #4
		blt	6f
	PLD(	pld	[r1, #0]		)
		ands	ip, r0, #3
		bne	7f
		ands	ip, r1, #3
		bne	8f

1:		subs	r2, r2, #8
		blt	5f
		subs	r2, r2, #20
		blt	4f
	PLD(	pld	[r1, #28]		)
	PLD(	subs	r2, r2, #64		)
	PLD(	blt	3f			)
2:	PLD(	pld	[r1, #60]		)
	PLD(	pld	[r1, #92]		)
		ldmia	r1!, {r3 - r9, ip}
		subs	r2, r2, #32
		stmgeia	r0!, {r3 - r9, ip}
		ldmgeia	r1!, {r3 - r9, ip}
		subges	r2, r2, #32
		stmia	r0!, {r3 - r9, ip}
		bge	2b
3:	PLD(	ldmia	r1!, {r3 - r9, ip}	)
	PLD(	adds	r2, r2, #32		)
	PLD(	stmgeia	r0!, {r3 - r9, ip}	)
	PLD(	ldmgeia	r1!, {r3 - r9, ip}	)
	PLD(	subges	r2, r2, #32		)
	PLD(	stmia	r0!, {r3 - r9, ip}	)
4:		cmn	r2, #16
		ldmgeia	r1!, {r3 - r6}
		subge	r2, r2, #16
		stmgeia	r0!, {r3 - r6}
		adds	r2, r2, #20
		ldmgeia	r1!, {r3 - r5}
		subge	r2, r2, #12
		stmgeia	r0!, {r3 - r5}
5:		adds	r2, r2, #8
		blt	6f
		subs	r2, r2, #4
		ldrlt	r3, [r1], #4
		ldmgeia	r1!, {r4, r5}
		subge	r2, r2, #4
		strlt	r3, [r0], #4
		stmgeia	r0!, {r4, r5}

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
		bgt	18f
		beq	13f
		cmp	r2, #12
		blt	11f
	PLD(	pld	[r1, #12]		)
		sub	r2, r2, #12
	PLD(	subs	r2, r2, #32		)
	PLD(	blt	10f			)
	PLD(	pld	[r1, #28]		)
9:	PLD(	pld	[r1, #44]		)
10:		mov	r3, r7, pull #8
		ldmia	r1!, {r4 - r7}
		subs	r2, r2, #16
		orr	r3, r3, r4, push #24
		mov	r4, r4, pull #8
		orr	r4, r4, r5, push #24
		mov	r5, r5, pull #8
		orr	r5, r5, r6, push #24
		mov	r6, r6, pull #8
		orr	r6, r6, r7, push #24
		stmia	r0!, {r3 - r6}
		bge	9b
	PLD(	cmn	r2, #32			)
	PLD(	bge	10b			)
	PLD(	add	r2, r2, #32		)
		adds	r2, r2, #12
		blt	12f
11:		mov	r3, r7, pull #8
		ldr	r7, [r1], #4
		subs	r2, r2, #4
		orr	r3, r3, r7, push #24
		str	r3, [r0], #4
		bge	11b
12:		sub	r1, r1, #3
		b	6b

13:		cmp	r2, #12
		blt	16f
	PLD(	pld	[r1, #12]		)
		sub	r2, r2, #12
	PLD(	subs	r2, r2, #32		)
	PLD(	blt	15f			)
	PLD(	pld	[r1, #28]		)
14:	PLD(	pld	[r1, #44]		)
15:		mov	r3, r7, pull #16
		ldmia	r1!, {r4 - r7}
		subs	r2, r2, #16
		orr	r3, r3, r4, push #16
		mov	r4, r4, pull #16
		orr	r4, r4, r5, push #16
		mov	r5, r5, pull #16
		orr	r5, r5, r6, push #16
		mov	r6, r6, pull #16
		orr	r6, r6, r7, push #16
		stmia	r0!, {r3 - r6}
		bge	14b
	PLD(	cmn	r2, #32			)
	PLD(	bge	15b			)
	PLD(	add	r2, r2, #32		)
		adds	r2, r2, #12
		blt	17f
16:		mov	r3, r7, pull #16
		ldr	r7, [r1], #4
		subs	r2, r2, #4
		orr	r3, r3, r7, push #16
		str	r3, [r0], #4
		bge	16b
17:		sub	r1, r1, #2
		b	6b

18:		cmp	r2, #12
		blt	21f
	PLD(	pld	[r1, #12]		)
		sub	r2, r2, #12
	PLD(	subs	r2, r2, #32		)
	PLD(	blt	20f			)
	PLD(	pld	[r1, #28]		)
19:	PLD(	pld	[r1, #44]		)
20:		mov	r3, r7, pull #24
		ldmia	r1!, {r4 - r7}
		subs	r2, r2, #16
		orr	r3, r3, r4, push #8
		mov	r4, r4, pull #24
		orr	r4, r4, r5, push #8
		mov	r5, r5, pull #24
		orr	r5, r5, r6, push #8
		mov	r6, r6, pull #24
		orr	r6, r6, r7, push #8
		stmia	r0!, {r3 - r6}
		bge	19b
	PLD(	cmn	r2, #32			)
	PLD(	bge	20b			)
	PLD(	add	r2, r2, #32		)
		adds	r2, r2, #12
		blt	22f
21:		mov	r3, r7, pull #24
		ldr	r7, [r1], #4
		subs	r2, r2, #4
		orr	r3, r3, r7, push #8
		str	r3, [r0], #4
		bge	21b
22:		sub	r1, r1, #1
		b	6b


23:		add	r1, r1, r2
		add	r0, r0, r2
		subs	r2, r2, #4
		blt	29f
	PLD(	pld	[r1, #-4]		)
		ands	ip, r0, #3
		bne	30f
		ands	ip, r1, #3
		bne	31f

24:		subs	r2, r2, #8
		blt	28f
		subs	r2, r2, #20
		blt	27f
	PLD(	pld	[r1, #-32]		)
	PLD(	subs	r2, r2, #64		)
	PLD(	blt	26f			)
25:	PLD(	pld	[r1, #-64]		)
	PLD(	pld	[r1, #-96]		)
		ldmdb	r1!, {r3 - r9, ip}
		subs	r2, r2, #32
		stmgedb	r0!, {r3 - r9, ip}
		ldmgedb	r1!, {r3 - r9, ip}
		subges	r2, r2, #32
		stmdb	r0!, {r3 - r9, ip}
		bge	25b
26:	PLD(	ldmdb	r1!, {r3 - r9, ip}	)
	PLD(	adds	r2, r2, #32		)
	PLD(	stmgedb	r0!, {r3 - r9, ip}	)
	PLD(	ldmgedb	r1!, {r3 - r9, ip}	)
	PLD(	subges	r2, r2, #32		)
	PLD(	stmdb	r0!, {r3 - r9, ip}	)
27:		cmn	r2, #16
		ldmgedb	r1!, {r3 - r6}
		subge	r2, r2, #16
		stmgedb	r0!, {r3 - r6}
		adds	r2, r2, #20
		ldmgedb	r1!, {r3 - r5}
		subge	r2, r2, #12
		stmgedb	r0!, {r3 - r5}
28:		adds	r2, r2, #8
		blt	29f
		subs	r2, r2, #4
		ldrlt	r3, [r1, #-4]!
		ldmgedb	r1!, {r4, r5}
		subge	r2, r2, #4
		strlt	r3, [r0, #-4]!
		stmgedb	r0!, {r4, r5}

29:		adds	r2, r2, #4
		EXITEQ
		cmp	r2, #2
		ldrb	r3, [r1, #-1]!
		ldrgeb	r4, [r1, #-1]!
		ldrgtb	r5, [r1, #-1]!
		strb	r3, [r0, #-1]!
		strgeb	r4, [r0, #-1]!
		strgtb	r5, [r0, #-1]!
		EXIT

30:		cmp	ip, #2
		ldrb	r3, [r1, #-1]!
		ldrgeb	r4, [r1, #-1]!
		ldrgtb	r5, [r1, #-1]!
		strb	r3, [r0, #-1]!
		strgeb	r4, [r0, #-1]!
		strgtb	r5, [r0, #-1]!
		subs	r2, r2, ip
		blt	29b
		ands	ip, r1, #3
		beq	24b

31:		bic	r1, r1, #3
		ldr	r3, [r1], #0
		cmp	ip, #2
		blt	41f
		beq	36f
		cmp	r2, #12
		blt	34f
	PLD(	pld	[r1, #-16]		)
		sub	r2, r2, #12
	PLD(	subs	r2, r2, #32		)
	PLD(	blt	33f			)
	PLD(	pld	[r1, #-32]		)
32:	PLD(	pld	[r1, #-48]		)
33:		mov	r7, r3, push #8
		ldmdb	r1!, {r3, r4, r5, r6}
		subs	r2, r2, #16
		orr	r7, r7, r6, pull #24
		mov	r6, r6, push #8
		orr	r6, r6, r5, pull #24
		mov	r5, r5, push #8
		orr	r5, r5, r4, pull #24
		mov	r4, r4, push #8
		orr	r4, r4, r3, pull #24
		stmdb	r0!, {r4, r5, r6, r7}
		bge	32b
	PLD(	cmn	r2, #32			)
	PLD(	bge	33b			)
	PLD(	add	r2, r2, #32		)
		adds	r2, r2, #12
		blt	35f
34:		mov	ip, r3, push #8
		ldr	r3, [r1, #-4]!
		subs	r2, r2, #4
		orr	ip, ip, r3, pull #24
		str	ip, [r0, #-4]!
		bge	34b
35:		add	r1, r1, #3
		b	29b

36:		cmp	r2, #12
		blt	39f
	PLD(	pld	[r1, #-16]		)
		sub	r2, r2, #12
	PLD(	subs	r2, r2, #32		)
	PLD(	blt	38f			)
	PLD(	pld	[r1, #-32]		)
37:	PLD(	pld	[r1, #-48]		)
38:		mov	r7, r3, push #16
		ldmdb	r1!, {r3, r4, r5, r6}
		subs	r2, r2, #16
		orr	r7, r7, r6, pull #16
		mov	r6, r6, push #16
		orr	r6, r6, r5, pull #16
		mov	r5, r5, push #16
		orr	r5, r5, r4, pull #16
		mov	r4, r4, push #16
		orr	r4, r4, r3, pull #16
		stmdb	r0!, {r4, r5, r6, r7}
		bge	37b
	PLD(	cmn	r2, #32			)
	PLD(	bge	38b			)
	PLD(	add	r2, r2, #32		)
		adds	r2, r2, #12
		blt	40f
39:		mov	ip, r3, push #16
		ldr	r3, [r1, #-4]!
		subs	r2, r2, #4
		orr	ip, ip, r3, pull #16
		str	ip, [r0, #-4]!
		bge	39b
40:		add	r1, r1, #2
		b	29b

41:		cmp	r2, #12
		blt	44f
	PLD(	pld	[r1, #-16]		)
		sub	r2, r2, #12
	PLD(	subs	r2, r2, #32		)
	PLD(	blt	43f			)
	PLD(	pld	[r1, #-32]		)
42:	PLD(	pld	[r1, #-48]		)
43:		mov	r7, r3, push #24
		ldmdb	r1!, {r3, r4, r5, r6}
		subs	r2, r2, #16
		orr	r7, r7, r6, pull #8
		mov	r6, r6, push #24
		orr	r6, r6, r5, pull #8
		mov	r5, r5, push #24
		orr	r5, r5, r4, pull #8
		mov	r4, r4, push #24
		orr	r4, r4, r3, pull #8
		stmdb	r0!, {r4, r5, r6, r7}
		bge	42b
	PLD(	cmn	r2, #32			)
	PLD(	bge	43b			)
	PLD(	add	r2, r2, #32		)
		adds	r2, r2, #12
		blt	45f
44:		mov	ip, r3, push #24
		ldr	r3, [r1, #-4]!
		subs	r2, r2, #4
		orr	ip, ip, r3, pull #8
		str	ip, [r0, #-4]!
		bge	44b
45:		add	r1, r1, #1
		b	29b

