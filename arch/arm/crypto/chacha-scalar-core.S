/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Google, Inc.
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

/*
 * Design notes:
 *
 * 16 registers would be needed to hold the state matrix, but only 14 are
 * available because 'sp' and 'pc' cannot be used.  So we spill the elements
 * (x8, x9) to the stack and swap them out with (x10, x11).  This adds one
 * 'ldrd' and one 'strd' instruction per round.
 *
 * All rotates are performed using the implicit rotate operand accepted by the
 * 'add' and 'eor' instructions.  This is faster than using explicit rotate
 * instructions.  To make this work, we allow the values in the second and last
 * rows of the ChaCha state matrix (rows 'b' and 'd') to temporarily have the
 * wrong rotation amount.  The rotation amount is then fixed up just in time
 * when the values are used.  'brot' is the number of bits the values in row 'b'
 * need to be rotated right to arrive at the correct values, and 'drot'
 * similarly for row 'd'.  (brot, drot) start out as (0, 0) but we make it such
 * that they end up as (25, 24) after every round.
 */

	// ChaCha state registers
	X0	.req	r0
	X1	.req	r1
	X2	.req	r2
	X3	.req	r3
	X4	.req	r4
	X5	.req	r5
	X6	.req	r6
	X7	.req	r7
	X8_X10	.req	r8	// shared by x8 and x10
	X9_X11	.req	r9	// shared by x9 and x11
	X12	.req	r10
	X13	.req	r11
	X14	.req	r12
	X15	.req	r14

.macro __rev		out, in,  t0, t1, t2
.if __LINUX_ARM_ARCH__ >= 6
	rev		\out, \in
.else
	lsl		\t0, \in, #24
	and		\t1, \in, #0xff00
	and		\t2, \in, #0xff0000
	orr		\out, \t0, \in, lsr #24
	orr		\out, \out, \t1, lsl #8
	orr		\out, \out, \t2, lsr #8
.endif
.endm

.macro _le32_bswap	x,  t0, t1, t2
#ifdef __ARMEB__
	__rev		\x, \x,  \t0, \t1, \t2
#endif
.endm

.macro _le32_bswap_4x	a, b, c, d,  t0, t1, t2
	_le32_bswap	\a,  \t0, \t1, \t2
	_le32_bswap	\b,  \t0, \t1, \t2
	_le32_bswap	\c,  \t0, \t1, \t2
	_le32_bswap	\d,  \t0, \t1, \t2
.endm

.macro __ldrd		a, b, src, offset
#if __LINUX_ARM_ARCH__ >= 6
	ldrd		\a, \b, [\src, #\offset]
#else
	ldr		\a, [\src, #\offset]
	ldr		\b, [\src, #\offset + 4]
#endif
.endm

.macro __strd		a, b, dst, offset
#if __LINUX_ARM_ARCH__ >= 6
	strd		\a, \b, [\dst, #\offset]
#else
	str		\a, [\dst, #\offset]
	str		\b, [\dst, #\offset + 4]
#endif
.endm

.macro _halfround	a1, b1, c1, d1,  a2, b2, c2, d2

	// a += b; d ^= a; d = rol(d, 16);
	add		\a1, \a1, \b1, ror #brot
	add		\a2, \a2, \b2, ror #brot
	eor		\d1, \a1, \d1, ror #drot
	eor		\d2, \a2, \d2, ror #drot
	// drot == 32 - 16 == 16

	// c += d; b ^= c; b = rol(b, 12);
	add		\c1, \c1, \d1, ror #16
	add		\c2, \c2, \d2, ror #16
	eor		\b1, \c1, \b1, ror #brot
	eor		\b2, \c2, \b2, ror #brot
	// brot == 32 - 12 == 20

	// a += b; d ^= a; d = rol(d, 8);
	add		\a1, \a1, \b1, ror #20
	add		\a2, \a2, \b2, ror #20
	eor		\d1, \a1, \d1, ror #16
	eor		\d2, \a2, \d2, ror #16
	// drot == 32 - 8 == 24

	// c += d; b ^= c; b = rol(b, 7);
	add		\c1, \c1, \d1, ror #24
	add		\c2, \c2, \d2, ror #24
	eor		\b1, \c1, \b1, ror #20
	eor		\b2, \c2, \b2, ror #20
	// brot == 32 - 7 == 25
.endm

.macro _doubleround

	// column round

	// quarterrounds: (x0, x4, x8, x12) and (x1, x5, x9, x13)
	_halfround	X0, X4, X8_X10, X12,  X1, X5, X9_X11, X13

	// save (x8, x9); restore (x10, x11)
	__strd		X8_X10, X9_X11, sp, 0
	__ldrd		X8_X10, X9_X11, sp, 8

	// quarterrounds: (x2, x6, x10, x14) and (x3, x7, x11, x15)
	_halfround	X2, X6, X8_X10, X14,  X3, X7, X9_X11, X15

	.set brot, 25
	.set drot, 24

	// diagonal round

	// quarterrounds: (x0, x5, x10, x15) and (x1, x6, x11, x12)
	_halfround	X0, X5, X8_X10, X15,  X1, X6, X9_X11, X12

	// save (x10, x11); restore (x8, x9)
	__strd		X8_X10, X9_X11, sp, 8
	__ldrd		X8_X10, X9_X11, sp, 0

	// quarterrounds: (x2, x7, x8, x13) and (x3, x4, x9, x14)
	_halfround	X2, X7, X8_X10, X13,  X3, X4, X9_X11, X14
.endm

.macro _chacha_permute	nrounds
	.set brot, 0
	.set drot, 0
	.rept \nrounds / 2
	 _doubleround
	.endr
.endm

.macro _chacha		nrounds

.Lnext_block\@:
	// Stack: unused0-unused1 x10-x11 x0-x15 OUT IN LEN
	// Registers contain x0-x9,x12-x15.

	// Do the core ChaCha permutation to update x0-x15.
	_chacha_permute	\nrounds

	add		sp, #8
	// Stack: x10-x11 orig_x0-orig_x15 OUT IN LEN
	// Registers contain x0-x9,x12-x15.
	// x4-x7 are rotated by 'brot'; x12-x15 are rotated by 'drot'.

	// Free up some registers (r8-r12,r14) by pushing (x8-x9,x12-x15).
	push		{X8_X10, X9_X11, X12, X13, X14, X15}

	// Load (OUT, IN, LEN).
	ldr		r14, [sp, #96]
	ldr		r12, [sp, #100]
	ldr		r11, [sp, #104]

	orr		r10, r14, r12

	// Use slow path if fewer than 64 bytes remain.
	cmp		r11, #64
	blt		.Lxor_slowpath\@

	// Use slow path if IN and/or OUT isn't 4-byte aligned.  Needed even on
	// ARMv6+, since ldmia and stmia (used below) still require alignment.
	tst		r10, #3
	bne		.Lxor_slowpath\@

	// Fast path: XOR 64 bytes of aligned data.

	// Stack: x8-x9 x12-x15 x10-x11 orig_x0-orig_x15 OUT IN LEN
	// Registers: r0-r7 are x0-x7; r8-r11 are free; r12 is IN; r14 is OUT.
	// x4-x7 are rotated by 'brot'; x12-x15 are rotated by 'drot'.

	// x0-x3
	__ldrd		r8, r9, sp, 32
	__ldrd		r10, r11, sp, 40
	add		X0, X0, r8
	add		X1, X1, r9
	add		X2, X2, r10
	add		X3, X3, r11
	_le32_bswap_4x	X0, X1, X2, X3,  r8, r9, r10
	ldmia		r12!, {r8-r11}
	eor		X0, X0, r8
	eor		X1, X1, r9
	eor		X2, X2, r10
	eor		X3, X3, r11
	stmia		r14!, {X0-X3}

	// x4-x7
	__ldrd		r8, r9, sp, 48
	__ldrd		r10, r11, sp, 56
	add		X4, r8, X4, ror #brot
	add		X5, r9, X5, ror #brot
	ldmia		r12!, {X0-X3}
	add		X6, r10, X6, ror #brot
	add		X7, r11, X7, ror #brot
	_le32_bswap_4x	X4, X5, X6, X7,  r8, r9, r10
	eor		X4, X4, X0
	eor		X5, X5, X1
	eor		X6, X6, X2
	eor		X7, X7, X3
	stmia		r14!, {X4-X7}

	// x8-x15
	pop		{r0-r7}			// (x8-x9,x12-x15,x10-x11)
	__ldrd		r8, r9, sp, 32
	__ldrd		r10, r11, sp, 40
	add		r0, r0, r8		// x8
	add		r1, r1, r9		// x9
	add		r6, r6, r10		// x10
	add		r7, r7, r11		// x11
	_le32_bswap_4x	r0, r1, r6, r7,  r8, r9, r10
	ldmia		r12!, {r8-r11}
	eor		r0, r0, r8		// x8
	eor		r1, r1, r9		// x9
	eor		r6, r6, r10		// x10
	eor		r7, r7, r11		// x11
	stmia		r14!, {r0,r1,r6,r7}
	ldmia		r12!, {r0,r1,r6,r7}
	__ldrd		r8, r9, sp, 48
	__ldrd		r10, r11, sp, 56
	add		r2, r8, r2, ror #drot	// x12
	add		r3, r9, r3, ror #drot	// x13
	add		r4, r10, r4, ror #drot	// x14
	add		r5, r11, r5, ror #drot	// x15
	_le32_bswap_4x	r2, r3, r4, r5,  r9, r10, r11
	  ldr		r9, [sp, #72]		// load LEN
	eor		r2, r2, r0		// x12
	eor		r3, r3, r1		// x13
	eor		r4, r4, r6		// x14
	eor		r5, r5, r7		// x15
	  subs		r9, #64			// decrement and check LEN
	stmia		r14!, {r2-r5}

	beq		.Ldone\@

.Lprepare_for_next_block\@:

	// Stack: x0-x15 OUT IN LEN

	// Increment block counter (x12)
	add		r8, #1

	// Store updated (OUT, IN, LEN)
	str		r14, [sp, #64]
	str		r12, [sp, #68]
	str		r9, [sp, #72]

	  mov		r14, sp

	// Store updated block counter (x12)
	str		r8, [sp, #48]

	  sub		sp, #16

	// Reload state and do next block
	ldmia		r14!, {r0-r11}		// load x0-x11
	__strd		r10, r11, sp, 8		// store x10-x11 before state
	ldmia		r14, {r10-r12,r14}	// load x12-x15
	b		.Lnext_block\@

.Lxor_slowpath\@:
	// Slow path: < 64 bytes remaining, or unaligned input or output buffer.
	// We handle it by storing the 64 bytes of keystream to the stack, then
	// XOR-ing the needed portion with the data.

	// Allocate keystream buffer
	sub		sp, #64
	mov		r14, sp

	// Stack: ks0-ks15 x8-x9 x12-x15 x10-x11 orig_x0-orig_x15 OUT IN LEN
	// Registers: r0-r7 are x0-x7; r8-r11 are free; r12 is IN; r14 is &ks0.
	// x4-x7 are rotated by 'brot'; x12-x15 are rotated by 'drot'.

	// Save keystream for x0-x3
	__ldrd		r8, r9, sp, 96
	__ldrd		r10, r11, sp, 104
	add		X0, X0, r8
	add		X1, X1, r9
	add		X2, X2, r10
	add		X3, X3, r11
	_le32_bswap_4x	X0, X1, X2, X3,  r8, r9, r10
	stmia		r14!, {X0-X3}

	// Save keystream for x4-x7
	__ldrd		r8, r9, sp, 112
	__ldrd		r10, r11, sp, 120
	add		X4, r8, X4, ror #brot
	add		X5, r9, X5, ror #brot
	add		X6, r10, X6, ror #brot
	add		X7, r11, X7, ror #brot
	_le32_bswap_4x	X4, X5, X6, X7,  r8, r9, r10
	  add		r8, sp, #64
	stmia		r14!, {X4-X7}

	// Save keystream for x8-x15
	ldm		r8, {r0-r7}		// (x8-x9,x12-x15,x10-x11)
	__ldrd		r8, r9, sp, 128
	__ldrd		r10, r11, sp, 136
	add		r0, r0, r8		// x8
	add		r1, r1, r9		// x9
	add		r6, r6, r10		// x10
	add		r7, r7, r11		// x11
	_le32_bswap_4x	r0, r1, r6, r7,  r8, r9, r10
	stmia		r14!, {r0,r1,r6,r7}
	__ldrd		r8, r9, sp, 144
	__ldrd		r10, r11, sp, 152
	add		r2, r8, r2, ror #drot	// x12
	add		r3, r9, r3, ror #drot	// x13
	add		r4, r10, r4, ror #drot	// x14
	add		r5, r11, r5, ror #drot	// x15
	_le32_bswap_4x	r2, r3, r4, r5,  r9, r10, r11
	stmia		r14, {r2-r5}

	// Stack: ks0-ks15 unused0-unused7 x0-x15 OUT IN LEN
	// Registers: r8 is block counter, r12 is IN.

	ldr		r9, [sp, #168]		// LEN
	ldr		r14, [sp, #160]		// OUT
	cmp		r9, #64
	  mov		r0, sp
	movle		r1, r9
	movgt		r1, #64
	// r1 is number of bytes to XOR, in range [1, 64]

.if __LINUX_ARM_ARCH__ < 6
	orr		r2, r12, r14
	tst		r2, #3			// IN or OUT misaligned?
	bne		.Lxor_next_byte\@
.endif

	// XOR a word at a time
.rept 16
	subs		r1, #4
	blt		.Lxor_words_done\@
	ldr		r2, [r12], #4
	ldr		r3, [r0], #4
	eor		r2, r2, r3
	str		r2, [r14], #4
.endr
	b		.Lxor_slowpath_done\@
.Lxor_words_done\@:
	ands		r1, r1, #3
	beq		.Lxor_slowpath_done\@

	// XOR a byte at a time
.Lxor_next_byte\@:
	ldrb		r2, [r12], #1
	ldrb		r3, [r0], #1
	eor		r2, r2, r3
	strb		r2, [r14], #1
	subs		r1, #1
	bne		.Lxor_next_byte\@

.Lxor_slowpath_done\@:
	subs		r9, #64
	add		sp, #96
	bgt		.Lprepare_for_next_block\@

.Ldone\@:
.endm	// _chacha

/*
 * void chacha_doarm(u8 *dst, const u8 *src, unsigned int bytes,
 *		     const u32 *state, int nrounds);
 */
ENTRY(chacha_doarm)
	cmp		r2, #0			// len == 0?
	reteq		lr

	ldr		ip, [sp]
	cmp		ip, #12

	push		{r0-r2,r4-r11,lr}

	// Push state x0-x15 onto stack.
	// Also store an extra copy of x10-x11 just before the state.

	add		X12, r3, #48
	ldm		X12, {X12,X13,X14,X15}
	push		{X12,X13,X14,X15}
	sub		sp, sp, #64

	__ldrd		X8_X10, X9_X11, r3, 40
	__strd		X8_X10, X9_X11, sp, 8
	__strd		X8_X10, X9_X11, sp, 56
	ldm		r3, {X0-X9_X11}
	__strd		X0, X1, sp, 16
	__strd		X2, X3, sp, 24
	__strd		X4, X5, sp, 32
	__strd		X6, X7, sp, 40
	__strd		X8_X10, X9_X11, sp, 48

	beq		1f
	_chacha		20

0:	add		sp, #76
	pop		{r4-r11, pc}

1:	_chacha		12
	b		0b
ENDPROC(chacha_doarm)

/*
 * void hchacha_block_arm(const u32 state[16], u32 out[8], int nrounds);
 */
ENTRY(hchacha_block_arm)
	push		{r1,r4-r11,lr}

	cmp		r2, #12			// ChaCha12 ?

	mov		r14, r0
	ldmia		r14!, {r0-r11}		// load x0-x11
	push		{r10-r11}		// store x10-x11 to stack
	ldm		r14, {r10-r12,r14}	// load x12-x15
	sub		sp, #8

	beq		1f
	_chacha_permute	20

	// Skip over (unused0-unused1, x10-x11)
0:	add		sp, #16

	// Fix up rotations of x12-x15
	ror		X12, X12, #drot
	ror		X13, X13, #drot
	  pop		{r4}			// load 'out'
	ror		X14, X14, #drot
	ror		X15, X15, #drot

	// Store (x0-x3,x12-x15) to 'out'
	stm		r4, {X0,X1,X2,X3,X12,X13,X14,X15}

	pop		{r4-r11,pc}

1:	_chacha_permute	12
	b		0b
ENDPROC(hchacha_block_arm)
