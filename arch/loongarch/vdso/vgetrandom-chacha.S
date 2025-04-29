// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Xi Ruoyao <xry111@xry111.site>. All Rights Reserved.
 */

#include <asm/asm.h>
#include <asm/regdef.h>
#include <linux/linkage.h>

.text

.macro	OP_4REG	op d0 d1 d2 d3 s0 s1 s2 s3
	\op	\d0, \d0, \s0
	\op	\d1, \d1, \s1
	\op	\d2, \d2, \s2
	\op	\d3, \d3, \s3
.endm

/*
 * Very basic LoongArch implementation of ChaCha20. Produces a given positive
 * number of blocks of output with a nonce of 0, taking an input key and
 * 8-byte counter. Importantly does not spill to the stack. Its arguments
 * are:
 *
 *	a0: output bytes
 *	a1: 32-byte key input
 *	a2: 8-byte counter input/output
 *	a3: number of 64-byte blocks to write to output
 */
SYM_FUNC_START(__arch_chacha20_blocks_nostack)

/* We don't need a frame pointer */
#define s9		fp

#define output		a0
#define key		a1
#define counter		a2
#define nblocks		a3
#define i		a4
#define state0		s0
#define state1		s1
#define state2		s2
#define state3		s3
#define state4		s4
#define state5		s5
#define state6		s6
#define state7		s7
#define state8		s8
#define state9		s9
#define state10		a5
#define state11		a6
#define state12		a7
#define state13		t0
#define state14		t1
#define state15		t2
#define cnt_lo		t3
#define cnt_hi		t4
#define copy0		t5
#define copy1		t6
#define copy2		t7
#define copy3		t8

/* Packs to be used with OP_4REG */
#define line0		state0, state1, state2, state3
#define line1		state4, state5, state6, state7
#define line2		state8, state9, state10, state11
#define line3		state12, state13, state14, state15

#define line1_perm	state5, state6, state7, state4
#define line2_perm	state10, state11, state8, state9
#define line3_perm	state15, state12, state13, state14

#define copy		copy0, copy1, copy2, copy3

#define _16		16, 16, 16, 16
#define _20		20, 20, 20, 20
#define _24		24, 24, 24, 24
#define _25		25, 25, 25, 25

	/*
	 * The ABI requires s0-s9 saved, and sp aligned to 16-byte.
	 * This does not violate the stack-less requirement: no sensitive data
	 * is spilled onto the stack.
	 */
	PTR_ADDI	sp, sp, (-SZREG * 10) & STACK_ALIGN
	REG_S		s0, sp, 0
	REG_S		s1, sp, SZREG
	REG_S		s2, sp, SZREG * 2
	REG_S		s3, sp, SZREG * 3
	REG_S		s4, sp, SZREG * 4
	REG_S		s5, sp, SZREG * 5
	REG_S		s6, sp, SZREG * 6
	REG_S		s7, sp, SZREG * 7
	REG_S		s8, sp, SZREG * 8
	REG_S		s9, sp, SZREG * 9

	li.w		copy0, 0x61707865
	li.w		copy1, 0x3320646e
	li.w		copy2, 0x79622d32
	li.w		copy3, 0x6b206574

	ld.w		cnt_lo, counter, 0
	ld.w		cnt_hi, counter, 4

.Lblock:
	/* state[0,1,2,3] = "expand 32-byte k" */
	move		state0, copy0
	move		state1, copy1
	move		state2, copy2
	move		state3, copy3

	/* state[4,5,..,11] = key */
	ld.w		state4, key, 0
	ld.w		state5, key, 4
	ld.w		state6, key, 8
	ld.w		state7, key, 12
	ld.w		state8, key, 16
	ld.w		state9, key, 20
	ld.w		state10, key, 24
	ld.w		state11, key, 28

	/* state[12,13] = counter */
	move		state12, cnt_lo
	move		state13, cnt_hi

	/* state[14,15] = 0 */
	move		state14, zero
	move		state15, zero

	li.w		i, 10
.Lpermute:
	/* odd round */
	OP_4REG	add.w	line0, line1
	OP_4REG	xor	line3, line0
	OP_4REG	rotri.w	line3, _16

	OP_4REG	add.w	line2, line3
	OP_4REG	xor	line1, line2
	OP_4REG	rotri.w	line1, _20

	OP_4REG	add.w	line0, line1
	OP_4REG	xor	line3, line0
	OP_4REG	rotri.w	line3, _24

	OP_4REG	add.w	line2, line3
	OP_4REG	xor	line1, line2
	OP_4REG	rotri.w	line1, _25

	/* even round */
	OP_4REG	add.w	line0, line1_perm
	OP_4REG	xor	line3_perm, line0
	OP_4REG	rotri.w	line3_perm, _16

	OP_4REG	add.w	line2_perm, line3_perm
	OP_4REG	xor	line1_perm, line2_perm
	OP_4REG	rotri.w	line1_perm, _20

	OP_4REG	add.w	line0, line1_perm
	OP_4REG	xor	line3_perm, line0
	OP_4REG	rotri.w	line3_perm, _24

	OP_4REG	add.w	line2_perm, line3_perm
	OP_4REG	xor	line1_perm, line2_perm
	OP_4REG	rotri.w	line1_perm, _25

	addi.w		i, i, -1
	bnez		i, .Lpermute

	/* output[0,1,2,3] = copy[0,1,2,3] + state[0,1,2,3] */
	OP_4REG	add.w	line0, copy
	st.w		state0, output, 0
	st.w		state1, output, 4
	st.w		state2, output, 8
	st.w		state3, output, 12

	/* from now on state[0,1,2,3] are scratch registers  */

	/* state[0,1,2,3] = lo32(key) */
	ld.w		state0, key, 0
	ld.w		state1, key, 4
	ld.w		state2, key, 8
	ld.w		state3, key, 12

	/* output[4,5,6,7] = state[0,1,2,3] + state[4,5,6,7] */
	OP_4REG	add.w	line1, line0
	st.w		state4, output, 16
	st.w		state5, output, 20
	st.w		state6, output, 24
	st.w		state7, output, 28

	/* state[0,1,2,3] = hi32(key) */
	ld.w		state0, key, 16
	ld.w		state1, key, 20
	ld.w		state2, key, 24
	ld.w		state3, key, 28

	/* output[8,9,10,11] = state[0,1,2,3] + state[8,9,10,11] */
	OP_4REG	add.w	line2, line0
	st.w		state8, output, 32
	st.w		state9, output, 36
	st.w		state10, output, 40
	st.w		state11, output, 44

	/* output[12,13,14,15] = state[12,13,14,15] + [cnt_lo, cnt_hi, 0, 0] */
	add.w		state12, state12, cnt_lo
	add.w		state13, state13, cnt_hi
	st.w		state12, output, 48
	st.w		state13, output, 52
	st.w		state14, output, 56
	st.w		state15, output, 60

	/* ++counter  */
	addi.w		cnt_lo, cnt_lo, 1
	sltui		state0, cnt_lo, 1
	add.w		cnt_hi, cnt_hi, state0

	/* output += 64 */
	PTR_ADDI	output, output, 64
	/* --nblocks */
	PTR_ADDI	nblocks, nblocks, -1
	bnez		nblocks, .Lblock

	/* counter = [cnt_lo, cnt_hi] */
	st.w		cnt_lo, counter, 0
	st.w		cnt_hi, counter, 4

	/*
	 * Zero out the potentially sensitive regs, in case nothing uses these
	 * again. As at now copy[0,1,2,3] just contains "expand 32-byte k" and
	 * state[0,...,9] are s0-s9 those we'll restore in the epilogue, so we
	 * only need to zero state[11,...,15].
	 */
	move		state10, zero
	move		state11, zero
	move		state12, zero
	move		state13, zero
	move		state14, zero
	move		state15, zero

	REG_L		s0, sp, 0
	REG_L		s1, sp, SZREG
	REG_L		s2, sp, SZREG * 2
	REG_L		s3, sp, SZREG * 3
	REG_L		s4, sp, SZREG * 4
	REG_L		s5, sp, SZREG * 5
	REG_L		s6, sp, SZREG * 6
	REG_L		s7, sp, SZREG * 7
	REG_L		s8, sp, SZREG * 8
	REG_L		s9, sp, SZREG * 9
	PTR_ADDI	sp, sp, -((-SZREG * 10) & STACK_ALIGN)

	jr		ra
SYM_FUNC_END(__arch_chacha20_blocks_nostack)
