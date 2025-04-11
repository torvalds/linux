/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2025 Xi Ruoyao <xry111@xry111.site>. All Rights Reserved.
 *
 * Based on arch/loongarch/vdso/vgetrandom-chacha.S.
 */

#include <asm/asm.h>
#include <linux/linkage.h>

.text

.macro	ROTRI	rd rs imm
	slliw	t0, \rs, 32 - \imm
	srliw	\rd, \rs, \imm
	or	\rd, \rd, t0
.endm

.macro	OP_4REG	op d0 d1 d2 d3 s0 s1 s2 s3
	\op	\d0, \d0, \s0
	\op	\d1, \d1, \s1
	\op	\d2, \d2, \s2
	\op	\d3, \d3, \s3
.endm

/*
 *	a0: output bytes
 * 	a1: 32-byte key input
 *	a2: 8-byte counter input/output
 *	a3: number of 64-byte blocks to write to output
 */
SYM_FUNC_START(__arch_chacha20_blocks_nostack)

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
#define state10		s10
#define state11		s11
#define state12		a5
#define state13		a6
#define state14		a7
#define state15		t1
#define cnt		t2
#define copy0		t3
#define copy1		t4
#define copy2		t5
#define copy3		t6

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

	addi		sp, sp, -12*SZREG
	REG_S		s0,         (sp)
	REG_S		s1,    SZREG(sp)
	REG_S		s2,  2*SZREG(sp)
	REG_S		s3,  3*SZREG(sp)
	REG_S		s4,  4*SZREG(sp)
	REG_S		s5,  5*SZREG(sp)
	REG_S		s6,  6*SZREG(sp)
	REG_S		s7,  7*SZREG(sp)
	REG_S		s8,  8*SZREG(sp)
	REG_S		s9,  9*SZREG(sp)
	REG_S		s10, 10*SZREG(sp)
	REG_S		s11, 11*SZREG(sp)

	ld		cnt, (counter)

	li		copy0, 0x61707865
	li		copy1, 0x3320646e
	li		copy2, 0x79622d32
	li		copy3, 0x6b206574

.Lblock:
	/* state[0,1,2,3] = "expand 32-byte k" */
	mv		state0, copy0
	mv		state1, copy1
	mv		state2, copy2
	mv		state3, copy3

	/* state[4,5,..,11] = key */
	lw		state4,   (key)
	lw		state5,  4(key)
	lw		state6,  8(key)
	lw		state7,  12(key)
	lw		state8,  16(key)
	lw		state9,  20(key)
	lw		state10, 24(key)
	lw		state11, 28(key)

	/* state[12,13] = counter */
	mv		state12, cnt
	srli		state13, cnt, 32

	/* state[14,15] = 0 */
	mv		state14, zero
	mv		state15, zero

	li		i, 10
.Lpermute:
	/* odd round */
	OP_4REG	addw	line0, line1
	OP_4REG	xor	line3, line0
	OP_4REG	ROTRI	line3, _16

	OP_4REG	addw	line2, line3
	OP_4REG	xor	line1, line2
	OP_4REG	ROTRI	line1, _20

	OP_4REG	addw	line0, line1
	OP_4REG	xor	line3, line0
	OP_4REG	ROTRI	line3, _24

	OP_4REG	addw	line2, line3
	OP_4REG	xor	line1, line2
	OP_4REG	ROTRI	line1, _25

	/* even round */
	OP_4REG	addw	line0, line1_perm
	OP_4REG	xor	line3_perm, line0
	OP_4REG	ROTRI	line3_perm, _16

	OP_4REG	addw	line2_perm, line3_perm
	OP_4REG	xor	line1_perm, line2_perm
	OP_4REG	ROTRI	line1_perm, _20

	OP_4REG	addw	line0, line1_perm
	OP_4REG	xor	line3_perm, line0
	OP_4REG	ROTRI	line3_perm, _24

	OP_4REG	addw	line2_perm, line3_perm
	OP_4REG	xor	line1_perm, line2_perm
	OP_4REG	ROTRI	line1_perm, _25

	addi		i, i, -1
	bnez		i, .Lpermute

	/* output[0,1,2,3] = copy[0,1,2,3] + state[0,1,2,3] */
	OP_4REG	addw	line0, copy
	sw		state0,   (output)
	sw		state1,  4(output)
	sw		state2,  8(output)
	sw		state3, 12(output)

	/* from now on state[0,1,2,3] are scratch registers  */

	/* state[0,1,2,3] = lo(key) */
	lw		state0,   (key)
	lw		state1,  4(key)
	lw		state2,  8(key)
	lw		state3, 12(key)

	/* output[4,5,6,7] = state[0,1,2,3] + state[4,5,6,7] */
	OP_4REG	addw	line1, line0
	sw		state4, 16(output)
	sw		state5, 20(output)
	sw		state6, 24(output)
	sw		state7, 28(output)

	/* state[0,1,2,3] = hi(key) */
	lw		state0, 16(key)
	lw		state1, 20(key)
	lw		state2, 24(key)
	lw		state3, 28(key)

	/* output[8,9,10,11] = tmp[0,1,2,3] + state[8,9,10,11] */
	OP_4REG	addw	line2, line0
	sw		state8,  32(output)
	sw		state9,  36(output)
	sw		state10, 40(output)
	sw		state11, 44(output)

	/* output[12,13,14,15] = state[12,13,14,15] + [cnt_lo, cnt_hi, 0, 0] */
	addw		state12, state12, cnt
	srli		state0, cnt, 32
	addw		state13, state13, state0
	sw		state12, 48(output)
	sw		state13, 52(output)
	sw		state14, 56(output)
	sw		state15, 60(output)

	/* ++counter */
	addi		cnt, cnt, 1

	/* output += 64 */
	addi		output, output, 64
	/* --nblocks */
	addi		nblocks, nblocks, -1
	bnez		nblocks, .Lblock

	/* counter = [cnt_lo, cnt_hi] */
	sd		cnt, (counter)

	/* Zero out the potentially sensitive regs, in case nothing uses these
	 * again.  As at now copy[0,1,2,3] just contains "expand 32-byte k" and
	 * state[0,...,11] are s0-s11 those we'll restore in the epilogue, we
	 * only need to zero state[12,...,15].
	 */
	mv		state12, zero
	mv		state13, zero
	mv		state14, zero
	mv		state15, zero

	REG_L		s0,         (sp)
	REG_L		s1,    SZREG(sp)
	REG_L		s2,  2*SZREG(sp)
	REG_L		s3,  3*SZREG(sp)
	REG_L		s4,  4*SZREG(sp)
	REG_L		s5,  5*SZREG(sp)
	REG_L		s6,  6*SZREG(sp)
	REG_L		s7,  7*SZREG(sp)
	REG_L		s8,  8*SZREG(sp)
	REG_L		s9,  9*SZREG(sp)
	REG_L		s10, 10*SZREG(sp)
	REG_L		s11, 11*SZREG(sp)
	addi		sp, sp, 12*SZREG

	ret
SYM_FUNC_END(__arch_chacha20_blocks_nostack)
