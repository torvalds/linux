/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (C) 2016-2018 René van Dorst <opensource@vdorst.com>. All Rights Reserved.
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#define MASK_U32		0x3c
#define CHACHA20_BLOCK_SIZE	64
#define STACK_SIZE		32

#define X0	$t0
#define X1	$t1
#define X2	$t2
#define X3	$t3
#define X4	$t4
#define X5	$t5
#define X6	$t6
#define X7	$t7
#define X8	$t8
#define X9	$t9
#define X10	$v1
#define X11	$s6
#define X12	$s5
#define X13	$s4
#define X14	$s3
#define X15	$s2
/* Use regs which are overwritten on exit for Tx so we don't leak clear data. */
#define T0	$s1
#define T1	$s0
#define T(n)	T ## n
#define X(n)	X ## n

/* Input arguments */
#define STATE		$a0
#define OUT		$a1
#define IN		$a2
#define BYTES		$a3

/* Output argument */
/* NONCE[0] is kept in a register and not in memory.
 * We don't want to touch original value in memory.
 * Must be incremented every loop iteration.
 */
#define NONCE_0		$v0

/* SAVED_X and SAVED_CA are set in the jump table.
 * Use regs which are overwritten on exit else we don't leak clear data.
 * They are used to handling the last bytes which are not multiple of 4.
 */
#define SAVED_X		X15
#define SAVED_CA	$s7

#define IS_UNALIGNED	$s7

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define MSB 0
#define LSB 3
#define ROTx rotl
#define ROTR(n) rotr n, 24
#define	CPU_TO_LE32(n) \
	wsbh	n; \
	rotr	n, 16;
#else
#define MSB 3
#define LSB 0
#define ROTx rotr
#define CPU_TO_LE32(n)
#define ROTR(n)
#endif

#define FOR_EACH_WORD(x) \
	x( 0); \
	x( 1); \
	x( 2); \
	x( 3); \
	x( 4); \
	x( 5); \
	x( 6); \
	x( 7); \
	x( 8); \
	x( 9); \
	x(10); \
	x(11); \
	x(12); \
	x(13); \
	x(14); \
	x(15);

#define FOR_EACH_WORD_REV(x) \
	x(15); \
	x(14); \
	x(13); \
	x(12); \
	x(11); \
	x(10); \
	x( 9); \
	x( 8); \
	x( 7); \
	x( 6); \
	x( 5); \
	x( 4); \
	x( 3); \
	x( 2); \
	x( 1); \
	x( 0);

#define PLUS_ONE_0	 1
#define PLUS_ONE_1	 2
#define PLUS_ONE_2	 3
#define PLUS_ONE_3	 4
#define PLUS_ONE_4	 5
#define PLUS_ONE_5	 6
#define PLUS_ONE_6	 7
#define PLUS_ONE_7	 8
#define PLUS_ONE_8	 9
#define PLUS_ONE_9	10
#define PLUS_ONE_10	11
#define PLUS_ONE_11	12
#define PLUS_ONE_12	13
#define PLUS_ONE_13	14
#define PLUS_ONE_14	15
#define PLUS_ONE_15	16
#define PLUS_ONE(x)	PLUS_ONE_ ## x
#define _CONCAT3(a,b,c)	a ## b ## c
#define CONCAT3(a,b,c)	_CONCAT3(a,b,c)

#define STORE_UNALIGNED(x) \
CONCAT3(.Lchacha_mips_xor_unaligned_, PLUS_ONE(x), _b: ;) \
	.if (x != 12); \
		lw	T0, (x*4)(STATE); \
	.endif; \
	lwl	T1, (x*4)+MSB ## (IN); \
	lwr	T1, (x*4)+LSB ## (IN); \
	.if (x == 12); \
		addu	X ## x, NONCE_0; \
	.else; \
		addu	X ## x, T0; \
	.endif; \
	CPU_TO_LE32(X ## x); \
	xor	X ## x, T1; \
	swl	X ## x, (x*4)+MSB ## (OUT); \
	swr	X ## x, (x*4)+LSB ## (OUT);

#define STORE_ALIGNED(x) \
CONCAT3(.Lchacha_mips_xor_aligned_, PLUS_ONE(x), _b: ;) \
	.if (x != 12); \
		lw	T0, (x*4)(STATE); \
	.endif; \
	lw	T1, (x*4) ## (IN); \
	.if (x == 12); \
		addu	X ## x, NONCE_0; \
	.else; \
		addu	X ## x, T0; \
	.endif; \
	CPU_TO_LE32(X ## x); \
	xor	X ## x, T1; \
	sw	X ## x, (x*4) ## (OUT);

/* Jump table macro.
 * Used for setup and handling the last bytes, which are not multiple of 4.
 * X15 is free to store Xn
 * Every jumptable entry must be equal in size.
 */
#define JMPTBL_ALIGNED(x) \
.Lchacha_mips_jmptbl_aligned_ ## x: ; \
	.set	noreorder; \
	b	.Lchacha_mips_xor_aligned_ ## x ## _b; \
	.if (x == 12); \
		addu	SAVED_X, X ## x, NONCE_0; \
	.else; \
		addu	SAVED_X, X ## x, SAVED_CA; \
	.endif; \
	.set	reorder

#define JMPTBL_UNALIGNED(x) \
.Lchacha_mips_jmptbl_unaligned_ ## x: ; \
	.set	noreorder; \
	b	.Lchacha_mips_xor_unaligned_ ## x ## _b; \
	.if (x == 12); \
		addu	SAVED_X, X ## x, NONCE_0; \
	.else; \
		addu	SAVED_X, X ## x, SAVED_CA; \
	.endif; \
	.set	reorder

#define AXR(A, B, C, D,  K, L, M, N,  V, W, Y, Z,  S) \
	addu	X(A), X(K); \
	addu	X(B), X(L); \
	addu	X(C), X(M); \
	addu	X(D), X(N); \
	xor	X(V), X(A); \
	xor	X(W), X(B); \
	xor	X(Y), X(C); \
	xor	X(Z), X(D); \
	rotl	X(V), S;    \
	rotl	X(W), S;    \
	rotl	X(Y), S;    \
	rotl	X(Z), S;

.text
.set	reorder
.set	noat
.globl	chacha_crypt_arch
.ent	chacha_crypt_arch
chacha_crypt_arch:
	.frame	$sp, STACK_SIZE, $ra

	/* Load number of rounds */
	lw	$at, 16($sp)

	addiu	$sp, -STACK_SIZE

	/* Return bytes = 0. */
	beqz	BYTES, .Lchacha_mips_end

	lw	NONCE_0, 48(STATE)

	/* Save s0-s7 */
	sw	$s0,  0($sp)
	sw	$s1,  4($sp)
	sw	$s2,  8($sp)
	sw	$s3, 12($sp)
	sw	$s4, 16($sp)
	sw	$s5, 20($sp)
	sw	$s6, 24($sp)
	sw	$s7, 28($sp)

	/* Test IN or OUT is unaligned.
	 * IS_UNALIGNED = ( IN | OUT ) & 0x00000003
	 */
	or	IS_UNALIGNED, IN, OUT
	andi	IS_UNALIGNED, 0x3

	b	.Lchacha_rounds_start

.align 4
.Loop_chacha_rounds:
	addiu	IN,  CHACHA20_BLOCK_SIZE
	addiu	OUT, CHACHA20_BLOCK_SIZE
	addiu	NONCE_0, 1

.Lchacha_rounds_start:
	lw	X0,  0(STATE)
	lw	X1,  4(STATE)
	lw	X2,  8(STATE)
	lw	X3,  12(STATE)

	lw	X4,  16(STATE)
	lw	X5,  20(STATE)
	lw	X6,  24(STATE)
	lw	X7,  28(STATE)
	lw	X8,  32(STATE)
	lw	X9,  36(STATE)
	lw	X10, 40(STATE)
	lw	X11, 44(STATE)

	move	X12, NONCE_0
	lw	X13, 52(STATE)
	lw	X14, 56(STATE)
	lw	X15, 60(STATE)

.Loop_chacha_xor_rounds:
	addiu	$at, -2
	AXR( 0, 1, 2, 3,  4, 5, 6, 7, 12,13,14,15, 16);
	AXR( 8, 9,10,11, 12,13,14,15,  4, 5, 6, 7, 12);
	AXR( 0, 1, 2, 3,  4, 5, 6, 7, 12,13,14,15,  8);
	AXR( 8, 9,10,11, 12,13,14,15,  4, 5, 6, 7,  7);
	AXR( 0, 1, 2, 3,  5, 6, 7, 4, 15,12,13,14, 16);
	AXR(10,11, 8, 9, 15,12,13,14,  5, 6, 7, 4, 12);
	AXR( 0, 1, 2, 3,  5, 6, 7, 4, 15,12,13,14,  8);
	AXR(10,11, 8, 9, 15,12,13,14,  5, 6, 7, 4,  7);
	bnez	$at, .Loop_chacha_xor_rounds

	addiu	BYTES, -(CHACHA20_BLOCK_SIZE)

	/* Is data src/dst unaligned? Jump */
	bnez	IS_UNALIGNED, .Loop_chacha_unaligned

	/* Set number rounds here to fill delayslot. */
	lw	$at, (STACK_SIZE+16)($sp)

	/* BYTES < 0, it has no full block. */
	bltz	BYTES, .Lchacha_mips_no_full_block_aligned

	FOR_EACH_WORD_REV(STORE_ALIGNED)

	/* BYTES > 0? Loop again. */
	bgtz	BYTES, .Loop_chacha_rounds

	/* Place this here to fill delay slot */
	addiu	NONCE_0, 1

	/* BYTES < 0? Handle last bytes */
	bltz	BYTES, .Lchacha_mips_xor_bytes

.Lchacha_mips_xor_done:
	/* Restore used registers */
	lw	$s0,  0($sp)
	lw	$s1,  4($sp)
	lw	$s2,  8($sp)
	lw	$s3, 12($sp)
	lw	$s4, 16($sp)
	lw	$s5, 20($sp)
	lw	$s6, 24($sp)
	lw	$s7, 28($sp)

	/* Write NONCE_0 back to right location in state */
	sw	NONCE_0, 48(STATE)

.Lchacha_mips_end:
	addiu	$sp, STACK_SIZE
	jr	$ra

.Lchacha_mips_no_full_block_aligned:
	/* Restore the offset on BYTES */
	addiu	BYTES, CHACHA20_BLOCK_SIZE

	/* Get number of full WORDS */
	andi	$at, BYTES, MASK_U32

	/* Load upper half of jump table addr */
	lui	T0, %hi(.Lchacha_mips_jmptbl_aligned_0)

	/* Calculate lower half jump table offset */
	ins	T0, $at, 1, 6

	/* Add offset to STATE */
	addu	T1, STATE, $at

	/* Add lower half jump table addr */
	addiu	T0, %lo(.Lchacha_mips_jmptbl_aligned_0)

	/* Read value from STATE */
	lw	SAVED_CA, 0(T1)

	/* Store remaining bytecounter as negative value */
	subu	BYTES, $at, BYTES

	jr	T0

	/* Jump table */
	FOR_EACH_WORD(JMPTBL_ALIGNED)


.Loop_chacha_unaligned:
	/* Set number rounds here to fill delayslot. */
	lw	$at, (STACK_SIZE+16)($sp)

	/* BYTES > 0, it has no full block. */
	bltz	BYTES, .Lchacha_mips_no_full_block_unaligned

	FOR_EACH_WORD_REV(STORE_UNALIGNED)

	/* BYTES > 0? Loop again. */
	bgtz	BYTES, .Loop_chacha_rounds

	/* Write NONCE_0 back to right location in state */
	sw	NONCE_0, 48(STATE)

	.set noreorder
	/* Fall through to byte handling */
	bgez	BYTES, .Lchacha_mips_xor_done
.Lchacha_mips_xor_unaligned_0_b:
.Lchacha_mips_xor_aligned_0_b:
	/* Place this here to fill delay slot */
	addiu	NONCE_0, 1
	.set reorder

.Lchacha_mips_xor_bytes:
	addu	IN, $at
	addu	OUT, $at
	/* First byte */
	lbu	T1, 0(IN)
	addiu	$at, BYTES, 1
	CPU_TO_LE32(SAVED_X)
	ROTR(SAVED_X)
	xor	T1, SAVED_X
	sb	T1, 0(OUT)
	beqz	$at, .Lchacha_mips_xor_done
	/* Second byte */
	lbu	T1, 1(IN)
	addiu	$at, BYTES, 2
	ROTx	SAVED_X, 8
	xor	T1, SAVED_X
	sb	T1, 1(OUT)
	beqz	$at, .Lchacha_mips_xor_done
	/* Third byte */
	lbu	T1, 2(IN)
	ROTx	SAVED_X, 8
	xor	T1, SAVED_X
	sb	T1, 2(OUT)
	b	.Lchacha_mips_xor_done

.Lchacha_mips_no_full_block_unaligned:
	/* Restore the offset on BYTES */
	addiu	BYTES, CHACHA20_BLOCK_SIZE

	/* Get number of full WORDS */
	andi	$at, BYTES, MASK_U32

	/* Load upper half of jump table addr */
	lui	T0, %hi(.Lchacha_mips_jmptbl_unaligned_0)

	/* Calculate lower half jump table offset */
	ins	T0, $at, 1, 6

	/* Add offset to STATE */
	addu	T1, STATE, $at

	/* Add lower half jump table addr */
	addiu	T0, %lo(.Lchacha_mips_jmptbl_unaligned_0)

	/* Read value from STATE */
	lw	SAVED_CA, 0(T1)

	/* Store remaining bytecounter as negative value */
	subu	BYTES, $at, BYTES

	jr	T0

	/* Jump table */
	FOR_EACH_WORD(JMPTBL_UNALIGNED)
.end chacha_crypt_arch
.set at

/* Input arguments
 * STATE	$a0
 * OUT		$a1
 * NROUND	$a2
 */

#undef X12
#undef X13
#undef X14
#undef X15

#define X12	$a3
#define X13	$at
#define X14	$v0
#define X15	STATE

.set noat
.globl	hchacha_block_arch
.ent	hchacha_block_arch
hchacha_block_arch:
	.frame	$sp, STACK_SIZE, $ra

	addiu	$sp, -STACK_SIZE

	/* Save X11(s6) */
	sw	X11, 0($sp)

	lw	X0,  0(STATE)
	lw	X1,  4(STATE)
	lw	X2,  8(STATE)
	lw	X3,  12(STATE)
	lw	X4,  16(STATE)
	lw	X5,  20(STATE)
	lw	X6,  24(STATE)
	lw	X7,  28(STATE)
	lw	X8,  32(STATE)
	lw	X9,  36(STATE)
	lw	X10, 40(STATE)
	lw	X11, 44(STATE)
	lw	X12, 48(STATE)
	lw	X13, 52(STATE)
	lw	X14, 56(STATE)
	lw	X15, 60(STATE)

.Loop_hchacha_xor_rounds:
	addiu	$a2, -2
	AXR( 0, 1, 2, 3,  4, 5, 6, 7, 12,13,14,15, 16);
	AXR( 8, 9,10,11, 12,13,14,15,  4, 5, 6, 7, 12);
	AXR( 0, 1, 2, 3,  4, 5, 6, 7, 12,13,14,15,  8);
	AXR( 8, 9,10,11, 12,13,14,15,  4, 5, 6, 7,  7);
	AXR( 0, 1, 2, 3,  5, 6, 7, 4, 15,12,13,14, 16);
	AXR(10,11, 8, 9, 15,12,13,14,  5, 6, 7, 4, 12);
	AXR( 0, 1, 2, 3,  5, 6, 7, 4, 15,12,13,14,  8);
	AXR(10,11, 8, 9, 15,12,13,14,  5, 6, 7, 4,  7);
	bnez	$a2, .Loop_hchacha_xor_rounds

	/* Restore used register */
	lw	X11, 0($sp)

	sw	X0,  0(OUT)
	sw	X1,  4(OUT)
	sw	X2,  8(OUT)
	sw	X3,  12(OUT)
	sw	X12, 16(OUT)
	sw	X13, 20(OUT)
	sw	X14, 24(OUT)
	sw	X15, 28(OUT)

	addiu	$sp, STACK_SIZE
	jr	$ra
.end hchacha_block_arch
.set at
