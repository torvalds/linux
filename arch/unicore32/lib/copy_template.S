/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/lib/copy_template.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
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
 * enter
 *
 *	Preserve the provided registers on the stack plus any additional
 *	data as needed by the implementation including this code. Called
 *	upon code entry.
 *
 * exit
 *
 *	Restore registers with the values previously saved with the
 *	'preserv' macro. Called upon code termination.
 */


		enter

		sub.a	r2, r2, #4
		bsl	8f
		and.a	ip, r0, #3
		bne	9f
		and.a	ip, r1, #3
		bne	10f

1:		sub.a	r2, r2, #(28)
		stm.w	(r5 - r8), [sp-]
		bsl	5f

3:
4:		ldr8w	r1, r3, r4, r5, r6, r7, r8, r10, r11, abort=20f
		sub.a	r2, r2, #32
		str8w	r0, r3, r4, r5, r6, r7, r8, r10, r11, abort=20f
		beg	3b

5:		and.a	ip, r2, #28
		rsub	ip, ip, #32
		beq	7f
		add	pc, pc, ip		@ C is always clear here
		nop

		ldr1w	r1, r3, abort=20f
		ldr1w	r1, r4, abort=20f
		ldr1w	r1, r5, abort=20f
		ldr1w	r1, r6, abort=20f
		ldr1w	r1, r7, abort=20f
		ldr1w	r1, r8, abort=20f
		ldr1w	r1, r11, abort=20f

		add	pc, pc, ip
		nop

		str1w	r0, r3, abort=20f
		str1w	r0, r4, abort=20f
		str1w	r0, r5, abort=20f
		str1w	r0, r6, abort=20f
		str1w	r0, r7, abort=20f
		str1w	r0, r8, abort=20f
		str1w	r0, r11, abort=20f

7:		ldm.w	(r5 - r8), [sp]+

8:		mov.a	r2, r2 << #31
		ldr1b	r1, r3, ne, abort=21f
		ldr1b	r1, r4, ea, abort=21f
		ldr1b	r1, r10, ea, abort=21f
		str1b	r0, r3, ne, abort=21f
		str1b	r0, r4, ea, abort=21f
		str1b	r0, r10, ea, abort=21f

		exit

9:		rsub	ip, ip, #4
		csub.a	ip, #2
		ldr1b	r1, r3, sg, abort=21f
		ldr1b	r1, r4, eg, abort=21f
		ldr1b	r1, r11, abort=21f
		str1b	r0, r3, sg, abort=21f
		str1b	r0, r4, eg, abort=21f
		sub.a	r2, r2, ip
		str1b	r0, r11, abort=21f
		bsl	8b
		and.a	ip, r1, #3
		beq	1b

10:		andn	r1, r1, #3
		csub.a	ip, #2
		ldr1w	r1, r11, abort=21f
		beq	17f
		bsg	18f


		.macro	forward_copy_shift a b

		sub.a	r2, r2, #28
		bsl	14f

11:		stm.w	(r5 - r9), [sp-]

12:
		ldr4w	r1, r4, r5, r6, r7, abort=19f
		mov	r3, r11 pull #\a
		sub.a	r2, r2, #32
		ldr4w	r1, r8, r9, r10, r11, abort=19f
		or	r3, r3, r4 push #\b
		mov	r4, r4 pull #\a
		or	r4, r4, r5 push #\b
		mov	r5, r5 pull #\a
		or	r5, r5, r6 push #\b
		mov	r6, r6 pull #\a
		or	r6, r6, r7 push #\b
		mov	r7, r7 pull #\a
		or	r7, r7, r8 push #\b
		mov	r8, r8 pull #\a
		or	r8, r8, r9 push #\b
		mov	r9, r9 pull #\a
		or	r9, r9, r10 push #\b
		mov	r10, r10 pull #\a
		or	r10, r10, r11 push #\b
		str8w	r0, r3, r4, r5, r6, r7, r8, r9, r10, , abort=19f
		beg	12b

		ldm.w	(r5 - r9), [sp]+

14:		and.a	ip, r2, #28
		beq	16f

15:		mov	r3, r11 pull #\a
		ldr1w	r1, r11, abort=21f
		sub.a	ip, ip, #4
		or	r3, r3, r11 push #\b
		str1w	r0, r3, abort=21f
		bsg	15b

16:		sub	r1, r1, #(\b / 8)
		b	8b

		.endm


		forward_copy_shift	a=8	b=24

17:		forward_copy_shift	a=16	b=16

18:		forward_copy_shift	a=24	b=8


/*
 * Abort preamble and completion macros.
 * If a fixup handler is required then those macros must surround it.
 * It is assumed that the fixup code will handle the private part of
 * the exit macro.
 */

	.macro	copy_abort_preamble
19:	ldm.w	(r5 - r9), [sp]+
	b	21f
299:	.word	0			@ store lr
					@ to avoid function call in fixup
20:	ldm.w	(r5 - r8), [sp]+
21:
	adr	r1, 299b
	stw	lr, [r1]
	.endm

	.macro	copy_abort_end
	adr	lr, 299b
	ldw	pc, [lr]
	.endm

