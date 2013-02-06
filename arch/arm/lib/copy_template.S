/*
 *  linux/arch/arm/lib/copy_template.s
 *
 *  Code template for optimized memory copy functions
 *
 *  Author:	Nicolas Pitre
 *  Created:	Sep 28, 2005
 *  Copyright:	MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

/*
 * Theory of operation
 * -------------------
 *
 * This file provides the core code for a forward memory copy used in
 * the implementation of memcopy(), copy_to_user() and copy_from_user().
 *
 * The including file must define the following accessor macros
 * according to the need of the given function:
 *
 * ldr1w ptr reg abort
 *
 *	This loads one word from 'ptr', stores it in 'reg' and increments
 *	'ptr' to the next word. The 'abort' argument is used for fixup tables.
 *
 * ldr4w ptr reg1 reg2 reg3 reg4 abort
 * ldr8w ptr, reg1 reg2 reg3 reg4 reg5 reg6 reg7 reg8 abort
 *
 *	This loads four or eight words starting from 'ptr', stores them
 *	in provided registers and increments 'ptr' past those words.
 *	The'abort' argument is used for fixup tables.
 *
 * ldr1b ptr reg cond abort
 *
 *	Similar to ldr1w, but it loads a byte and increments 'ptr' one byte.
 *	It also must apply the condition code if provided, otherwise the
 *	"al" condition is assumed by default.
 *
 * str1w ptr reg abort
 * str8w ptr reg1 reg2 reg3 reg4 reg5 reg6 reg7 reg8 abort
 * str1b ptr reg cond abort
 *
 *	Same as their ldr* counterparts, but data is stored to 'ptr' location
 *	rather than being loaded.
 *
 * enter reg1 reg2
 *
 *	Preserve the provided registers on the stack plus any additional
 *	data as needed by the implementation including this code. Called
 *	upon code entry.
 *
 * exit reg1 reg2
 *
 *	Restore registers with the values previously saved with the
 *	'preserv' macro. Called upon code termination.
 *
 * LDR1W_SHIFT
 * STR1W_SHIFT
 *
 *	Correction to be applied to the "ip" register when branching into
 *	the ldr1w or str1w instructions (some of these macros may expand to
 *	than one 32bit instruction in Thumb-2)
 */

#define PLDSIZE	(CONFIG_ARM_PLD_SIZE)

		enter	r4, lr

		subs	r2, r2, #4
		blt	8f
		ands	ip, r0, #3
	PLD(	pld	[r1, #0]		)
		bne	9f
		ands	ip, r1, #3
		bne	10f

1:		subs	r2, r2, #(28)
		stmfd	sp!, {r5 - r8}
		blt	5f

	CALGN(	ands	ip, r0, #31		)
	CALGN(	rsb	r3, ip, #32		)
	CALGN(	sbcnes	r4, r3, r2		)  @ C is always set here
	CALGN(	bcs	2f			)
	CALGN(	adr	r4, 6f			)
	CALGN(	subs	r2, r2, r3		)  @ C gets set
	CALGN(	add	pc, r4, ip		)

	PLD(	pld	[r1, #0]		)

#if (PLDSIZE == 64)
2:	PLD(	cmp	r2, #32)
	PLD(	blt	.32copy)

.64copy:
	PLD(	subs	r2, r2, #(PLDSIZE*3+32)		)
	PLD(	pld	[r1, #PLDSIZE-4]		)
	PLD(	blt	4f			)
	PLD(	pld	[r1, #PLDSIZE*2-4]		)
	PLD(	pld	[r1, #PLDSIZE*3-4]		)
3:	PLD(	pld	[r1, #PLDSIZE*4-4]		)
4:		cpy8w	r0, r1, r4, r5, abort=20f
		cpy8w	r0, r1, r4, r5, abort=20f
		subs	r2, r2, #PLDSIZE
		bge	3b
	PLD(	cmn	r2, #(PLDSIZE*3)		)
	PLD(	bge	4b			)

	PLD(	cmn r2, #(PLDSIZE*4-32)	)
	PLD(	blt	5f)

.32copy:
		cpy8w	r0, r1, r4, r5, abort=20f
#else
2:	PLD(	subs	r2, r2, #(PLDSIZE*3)		)
	PLD(	pld	[r1, #(PLDSIZE-4)]		)
	PLD(	blt	4f			)
	PLD(	pld	[r1, #(PLDSIZE*2-4)]		)
	PLD(	pld	[r1, #(PLDSIZE*3-4)]		)

3:	PLD(	pld	[r1, #(PLDSIZE*4-4)]		)
4:		ldr8w   r1, r3, r4, r5, r6, r7, r8, ip, lr, abort=20f
		subs	r2, r2, #PLDSIZE
		str8w   r0, r3, r4, r5, r6, r7, r8, ip, lr, abort=20f

		bge	3b
	PLD(	cmn	r2, #(PLDSIZE*3)			)
	PLD(	bge	4b			)
#endif

5:		ands	ip, r2, #28
		rsb	ip, ip, #32
#if LDR1W_SHIFT > 0
		lsl	ip, ip, #LDR1W_SHIFT
#endif
		addne	pc, pc, ip		@ C is always clear here
		b	7f
6:
		.rept	(1 << LDR1W_SHIFT)
		W(nop)
		.endr
		ldr1w	r1, r3, abort=20f
		ldr1w	r1, r4, abort=20f
		ldr1w	r1, r5, abort=20f
		ldr1w	r1, r6, abort=20f
		ldr1w	r1, r7, abort=20f
		ldr1w	r1, r8, abort=20f
		ldr1w	r1, lr, abort=20f

#if LDR1W_SHIFT < STR1W_SHIFT
		lsl	ip, ip, #STR1W_SHIFT - LDR1W_SHIFT
#elif LDR1W_SHIFT > STR1W_SHIFT
		lsr	ip, ip, #LDR1W_SHIFT - STR1W_SHIFT
#endif
		add	pc, pc, ip
		nop
		.rept	(1 << STR1W_SHIFT)
		W(nop)
		.endr
		str1w	r0, r3, abort=20f
		str1w	r0, r4, abort=20f
		str1w	r0, r5, abort=20f
		str1w	r0, r6, abort=20f
		str1w	r0, r7, abort=20f
		str1w	r0, r8, abort=20f
		str1w	r0, lr, abort=20f

	CALGN(	bcs	2b			)

7:		ldmfd	sp!, {r5 - r8}

8:		movs	r2, r2, lsl #31
		ldr1b	r1, r3, ne, abort=21f
		ldr1b	r1, r4, cs, abort=21f
		ldr1b	r1, ip, cs, abort=21f
		str1b	r0, r3, ne, abort=21f
		str1b	r0, r4, cs, abort=21f
		str1b	r0, ip, cs, abort=21f

		exit	r4, pc

9:		rsb	ip, ip, #4
		cmp	ip, #2
		ldr1b	r1, r3, gt, abort=21f
		ldr1b	r1, r4, ge, abort=21f
		ldr1b	r1, lr, abort=21f
		str1b	r0, r3, gt, abort=21f
		str1b	r0, r4, ge, abort=21f
		subs	r2, r2, ip
		str1b	r0, lr, abort=21f
		blt	8b
		ands	ip, r1, #3
		beq	1b

10:		bic	r1, r1, #3
		cmp	ip, #2
		ldr1w	r1, lr, abort=21f
		beq	17f
		bgt	18f


		.macro	forward_copy_shift pull push

		subs	r2, r2, #28
		blt	14f

	CALGN(	ands	ip, r0, #31		)
	CALGN(	rsb	ip, ip, #32		)
	CALGN(	sbcnes	r4, ip, r2		)  @ C is always set here
	CALGN(	subcc	r2, r2, ip		)
	CALGN(	bcc	15f			)

11:		stmfd	sp!, {r5 - r9}

	PLD(	pld	[r1, #0]		)
	PLD(	subs	r2, r2, #96		)
	PLD(	pld	[r1, #28]		)
	PLD(	blt	13f			)
	PLD(	pld	[r1, #60]		)
	PLD(	pld	[r1, #92]		)

12:	PLD(	pld	[r1, #124]		)
13:		ldr4w	r1, r4, r5, r6, r7, abort=19f
		mov	r3, lr, pull #\pull
		subs	r2, r2, #32
		ldr4w	r1, r8, r9, ip, lr, abort=19f
		orr	r3, r3, r4, push #\push
		mov	r4, r4, pull #\pull
		orr	r4, r4, r5, push #\push
		mov	r5, r5, pull #\pull
		orr	r5, r5, r6, push #\push
		mov	r6, r6, pull #\pull
		orr	r6, r6, r7, push #\push
		mov	r7, r7, pull #\pull
		orr	r7, r7, r8, push #\push
		mov	r8, r8, pull #\pull
		orr	r8, r8, r9, push #\push
		mov	r9, r9, pull #\pull
		orr	r9, r9, ip, push #\push
		mov	ip, ip, pull #\pull
		orr	ip, ip, lr, push #\push
		str8w	r0, r3, r4, r5, r6, r7, r8, r9, ip, , abort=19f
		bge	12b
	PLD(	cmn	r2, #96			)
	PLD(	bge	13b			)

		ldmfd	sp!, {r5 - r9}

14:		ands	ip, r2, #28
		beq	16f

15:		mov	r3, lr, pull #\pull
		ldr1w	r1, lr, abort=21f
		subs	ip, ip, #4
		orr	r3, r3, lr, push #\push
		str1w	r0, r3, abort=21f
		bgt	15b
	CALGN(	cmp	r2, #0			)
	CALGN(	bge	11b			)

16:		sub	r1, r1, #(\push / 8)
		b	8b

		.endm


		forward_copy_shift	pull=8	push=24

17:		forward_copy_shift	pull=16	push=16

18:		forward_copy_shift	pull=24	push=8


/*
 * Abort preamble and completion macros.
 * If a fixup handler is required then those macros must surround it.
 * It is assumed that the fixup code will handle the private part of
 * the exit macro.
 */

	.macro	copy_abort_preamble
19:	ldmfd	sp!, {r5 - r9}
	b	21f
20:	ldmfd	sp!, {r5 - r8}
21:
	.endm

	.macro	copy_abort_end
	ldmfd	sp!, {r4, pc}
	.endm

