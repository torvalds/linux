/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Christophe Leroy <christophe.leroy@csgroup.eu>, CS GROUP France
 */

#include <linux/linkage.h>

#include <asm/ppc_asm.h>

#define	dst_bytes	r3
#define	key		r4
#define	counter		r5
#define	nblocks		r6

#define	idx_r0		r0
#define	val4		r4

#define	const0		0x61707865
#define	const1		0x3320646e
#define	const2		0x79622d32
#define	const3		0x6b206574

#define	key0		r5
#define	key1		r6
#define	key2		r7
#define	key3		r8
#define	key4		r9
#define	key5		r10
#define	key6		r11
#define	key7		r12

#define	counter0	r14
#define	counter1	r15

#define	state0		r16
#define	state1		r17
#define	state2		r18
#define	state3		r19
#define	state4		r20
#define	state5		r21
#define	state6		r22
#define	state7		r23
#define	state8		r24
#define	state9		r25
#define	state10		r26
#define	state11		r27
#define	state12		r28
#define	state13		r29
#define	state14		r30
#define	state15		r31

.macro quarterround4 a1 b1 c1 d1 a2 b2 c2 d2 a3 b3 c3 d3 a4 b4 c4 d4
	add	\a1, \a1, \b1
	add	\a2, \a2, \b2
	add	\a3, \a3, \b3
	add	\a4, \a4, \b4
	xor	\d1, \d1, \a1
	xor	\d2, \d2, \a2
	xor	\d3, \d3, \a3
	xor	\d4, \d4, \a4
	rotlwi	\d1, \d1, 16
	rotlwi	\d2, \d2, 16
	rotlwi	\d3, \d3, 16
	rotlwi	\d4, \d4, 16
	add	\c1, \c1, \d1
	add	\c2, \c2, \d2
	add	\c3, \c3, \d3
	add	\c4, \c4, \d4
	xor	\b1, \b1, \c1
	xor	\b2, \b2, \c2
	xor	\b3, \b3, \c3
	xor	\b4, \b4, \c4
	rotlwi	\b1, \b1, 12
	rotlwi	\b2, \b2, 12
	rotlwi	\b3, \b3, 12
	rotlwi	\b4, \b4, 12
	add	\a1, \a1, \b1
	add	\a2, \a2, \b2
	add	\a3, \a3, \b3
	add	\a4, \a4, \b4
	xor	\d1, \d1, \a1
	xor	\d2, \d2, \a2
	xor	\d3, \d3, \a3
	xor	\d4, \d4, \a4
	rotlwi	\d1, \d1, 8
	rotlwi	\d2, \d2, 8
	rotlwi	\d3, \d3, 8
	rotlwi	\d4, \d4, 8
	add	\c1, \c1, \d1
	add	\c2, \c2, \d2
	add	\c3, \c3, \d3
	add	\c4, \c4, \d4
	xor	\b1, \b1, \c1
	xor	\b2, \b2, \c2
	xor	\b3, \b3, \c3
	xor	\b4, \b4, \c4
	rotlwi	\b1, \b1, 7
	rotlwi	\b2, \b2, 7
	rotlwi	\b3, \b3, 7
	rotlwi	\b4, \b4, 7
.endm

#define QUARTERROUND4(a1,b1,c1,d1,a2,b2,c2,d2,a3,b3,c3,d3,a4,b4,c4,d4) \
	quarterround4 state##a1 state##b1 state##c1 state##d1 \
		      state##a2 state##b2 state##c2 state##d2 \
		      state##a3 state##b3 state##c3 state##d3 \
		      state##a4 state##b4 state##c4 state##d4

/*
 * Very basic 32 bits implementation of ChaCha20. Produces a given positive number
 * of blocks of output with a nonce of 0, taking an input key and 8-byte
 * counter. Importantly does not spill to the stack. Its arguments are:
 *
 *	r3: output bytes
 *	r4: 32-byte key input
 *	r5: 8-byte counter input/output (saved on stack)
 *	r6: number of 64-byte blocks to write to output
 *
 *	r0: counter of blocks (initialised with r6)
 *	r4: Value '4' after key has been read.
 *	r5-r12: key
 *	r14-r15: counter
 *	r16-r31: state
 */
SYM_FUNC_START(__arch_chacha20_blocks_nostack)
#ifdef __powerpc64__
	std	counter, -216(r1)

	std	r14, -144(r1)
	std	r15, -136(r1)
	std	r16, -128(r1)
	std	r17, -120(r1)
	std	r18, -112(r1)
	std	r19, -104(r1)
	std	r20, -96(r1)
	std	r21, -88(r1)
	std	r22, -80(r1)
	std	r23, -72(r1)
	std	r24, -64(r1)
	std	r25, -56(r1)
	std	r26, -48(r1)
	std	r27, -40(r1)
	std	r28, -32(r1)
	std	r29, -24(r1)
	std	r30, -16(r1)
	std	r31, -8(r1)
#else
	stwu	r1, -96(r1)
	stw	counter, 20(r1)
#ifdef __BIG_ENDIAN__
	stmw	r14, 24(r1)
#else
	stw	r14, 24(r1)
	stw	r15, 28(r1)
	stw	r16, 32(r1)
	stw	r17, 36(r1)
	stw	r18, 40(r1)
	stw	r19, 44(r1)
	stw	r20, 48(r1)
	stw	r21, 52(r1)
	stw	r22, 56(r1)
	stw	r23, 60(r1)
	stw	r24, 64(r1)
	stw	r25, 68(r1)
	stw	r26, 72(r1)
	stw	r27, 76(r1)
	stw	r28, 80(r1)
	stw	r29, 84(r1)
	stw	r30, 88(r1)
	stw	r31, 92(r1)
#endif
#endif	/* __powerpc64__ */

	lwz	counter0, 0(counter)
	lwz	counter1, 4(counter)
#ifdef __powerpc64__
	rldimi	counter0, counter1, 32, 0
#endif
	mr	idx_r0, nblocks
	subi	dst_bytes, dst_bytes, 4

	lwz	key0, 0(key)
	lwz	key1, 4(key)
	lwz	key2, 8(key)
	lwz	key3, 12(key)
	lwz	key4, 16(key)
	lwz	key5, 20(key)
	lwz	key6, 24(key)
	lwz	key7, 28(key)

	li	val4, 4
.Lblock:
	li	r31, 10

	lis	state0, const0@ha
	lis	state1, const1@ha
	lis	state2, const2@ha
	lis	state3, const3@ha
	addi	state0, state0, const0@l
	addi	state1, state1, const1@l
	addi	state2, state2, const2@l
	addi	state3, state3, const3@l

	mtctr	r31

	mr	state4, key0
	mr	state5, key1
	mr	state6, key2
	mr	state7, key3
	mr	state8, key4
	mr	state9, key5
	mr	state10, key6
	mr	state11, key7

	mr	state12, counter0
	mr	state13, counter1

	li	state14, 0
	li	state15, 0

.Lpermute:
	QUARTERROUND4( 0, 4, 8,12, 1, 5, 9,13, 2, 6,10,14, 3, 7,11,15)
	QUARTERROUND4( 0, 5,10,15, 1, 6,11,12, 2, 7, 8,13, 3, 4, 9,14)

	bdnz	.Lpermute

	addis	state0, state0, const0@ha
	addis	state1, state1, const1@ha
	addis	state2, state2, const2@ha
	addis	state3, state3, const3@ha
	addi	state0, state0, const0@l
	addi	state1, state1, const1@l
	addi	state2, state2, const2@l
	addi	state3, state3, const3@l

	add	state4, state4, key0
	add	state5, state5, key1
	add	state6, state6, key2
	add	state7, state7, key3
	add	state8, state8, key4
	add	state9, state9, key5
	add	state10, state10, key6
	add	state11, state11, key7

	add	state12, state12, counter0
	add	state13, state13, counter1

#ifdef __BIG_ENDIAN__
	stwbrx	state0, val4, dst_bytes
	addi	dst_bytes, dst_bytes, 8
	stwbrx	state1, 0, dst_bytes
	stwbrx	state2, val4, dst_bytes
	addi	dst_bytes, dst_bytes, 8
	stwbrx	state3, 0, dst_bytes
	stwbrx	state4, val4, dst_bytes
	addi	dst_bytes, dst_bytes, 8
	stwbrx	state5, 0, dst_bytes
	stwbrx	state6, val4, dst_bytes
	addi	dst_bytes, dst_bytes, 8
	stwbrx	state7, 0, dst_bytes
	stwbrx	state8, val4, dst_bytes
	addi	dst_bytes, dst_bytes, 8
	stwbrx	state9, 0, dst_bytes
	stwbrx	state10, val4, dst_bytes
	addi	dst_bytes, dst_bytes, 8
	stwbrx	state11, 0, dst_bytes
	stwbrx	state12, val4, dst_bytes
	addi	dst_bytes, dst_bytes, 8
	stwbrx	state13, 0, dst_bytes
	stwbrx	state14, val4, dst_bytes
	addi	dst_bytes, dst_bytes, 8
	stwbrx	state15, 0, dst_bytes
#else
	stw	state0, 4(dst_bytes)
	stw	state1, 8(dst_bytes)
	stw	state2, 12(dst_bytes)
	stw	state3, 16(dst_bytes)
	stw	state4, 20(dst_bytes)
	stw	state5, 24(dst_bytes)
	stw	state6, 28(dst_bytes)
	stw	state7, 32(dst_bytes)
	stw	state8, 36(dst_bytes)
	stw	state9, 40(dst_bytes)
	stw	state10, 44(dst_bytes)
	stw	state11, 48(dst_bytes)
	stw	state12, 52(dst_bytes)
	stw	state13, 56(dst_bytes)
	stw	state14, 60(dst_bytes)
	stwu	state15, 64(dst_bytes)
#endif

	subic.	idx_r0, idx_r0, 1	/* subi. can't use r0 as source */

#ifdef __powerpc64__
	addi	counter0, counter0, 1
	srdi	counter1, counter0, 32
#else
	addic	counter0, counter0, 1
	addze	counter1, counter1
#endif

	bne	.Lblock

#ifdef __powerpc64__
	ld	counter, -216(r1)
#else
	lwz	counter, 20(r1)
#endif
	stw	counter0, 0(counter)
	stw	counter1, 4(counter)

	li	r6, 0
	li	r7, 0
	li	r8, 0
	li	r9, 0
	li	r10, 0
	li	r11, 0
	li	r12, 0

#ifdef __powerpc64__
	ld	r14, -144(r1)
	ld	r15, -136(r1)
	ld	r16, -128(r1)
	ld	r17, -120(r1)
	ld	r18, -112(r1)
	ld	r19, -104(r1)
	ld	r20, -96(r1)
	ld	r21, -88(r1)
	ld	r22, -80(r1)
	ld	r23, -72(r1)
	ld	r24, -64(r1)
	ld	r25, -56(r1)
	ld	r26, -48(r1)
	ld	r27, -40(r1)
	ld	r28, -32(r1)
	ld	r29, -24(r1)
	ld	r30, -16(r1)
	ld	r31, -8(r1)
#else
#ifdef __BIG_ENDIAN__
	lmw	r14, 24(r1)
#else
	lwz	r14, 24(r1)
	lwz	r15, 28(r1)
	lwz	r16, 32(r1)
	lwz	r17, 36(r1)
	lwz	r18, 40(r1)
	lwz	r19, 44(r1)
	lwz	r20, 48(r1)
	lwz	r21, 52(r1)
	lwz	r22, 56(r1)
	lwz	r23, 60(r1)
	lwz	r24, 64(r1)
	lwz	r25, 68(r1)
	lwz	r26, 72(r1)
	lwz	r27, 76(r1)
	lwz	r28, 80(r1)
	lwz	r29, 84(r1)
	lwz	r30, 88(r1)
	lwz	r31, 92(r1)
#endif
	addi	r1, r1, 96
#endif	/* __powerpc64__ */
	blr
SYM_FUNC_END(__arch_chacha20_blocks_nostack)
