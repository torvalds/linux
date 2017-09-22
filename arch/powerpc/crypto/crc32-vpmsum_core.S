/*
 * Core of the accelerated CRC algorithm.
 * In your file, define the constants and CRC_FUNCTION_NAME
 * Then include this file.
 *
 * Calculate the checksum of data that is 16 byte aligned and a multiple of
 * 16 bytes.
 *
 * The first step is to reduce it to 1024 bits. We do this in 8 parallel
 * chunks in order to mask the latency of the vpmsum instructions. If we
 * have more than 32 kB of data to checksum we repeat this step multiple
 * times, passing in the previous 1024 bits.
 *
 * The next step is to reduce the 1024 bits to 64 bits. This step adds
 * 32 bits of 0s to the end - this matches what a CRC does. We just
 * calculate constants that land the data in this 32 bits.
 *
 * We then use fixed point Barrett reduction to compute a mod n over GF(2)
 * for n = CRC using POWER8 instructions. We use x = 32.
 *
 * http://en.wikipedia.org/wiki/Barrett_reduction
 *
 * Copyright (C) 2015 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
*/

#include <asm/ppc_asm.h>
#include <asm/ppc-opcode.h>

#define MAX_SIZE	32768

	.text

#if defined(__BIG_ENDIAN__) && defined(REFLECT)
#define BYTESWAP_DATA
#elif defined(__LITTLE_ENDIAN__) && !defined(REFLECT)
#define BYTESWAP_DATA
#else
#undef BYTESWAP_DATA
#endif

#define off16		r25
#define off32		r26
#define off48		r27
#define off64		r28
#define off80		r29
#define off96		r30
#define off112		r31

#define const1		v24
#define const2		v25

#define byteswap	v26
#define	mask_32bit	v27
#define	mask_64bit	v28
#define zeroes		v29

#ifdef BYTESWAP_DATA
#define VPERM(A, B, C, D) vperm	A, B, C, D
#else
#define VPERM(A, B, C, D)
#endif

/* unsigned int CRC_FUNCTION_NAME(unsigned int crc, void *p, unsigned long len) */
FUNC_START(CRC_FUNCTION_NAME)
	std	r31,-8(r1)
	std	r30,-16(r1)
	std	r29,-24(r1)
	std	r28,-32(r1)
	std	r27,-40(r1)
	std	r26,-48(r1)
	std	r25,-56(r1)

	li	off16,16
	li	off32,32
	li	off48,48
	li	off64,64
	li	off80,80
	li	off96,96
	li	off112,112
	li	r0,0

	/* Enough room for saving 10 non volatile VMX registers */
	subi	r6,r1,56+10*16
	subi	r7,r1,56+2*16

	stvx	v20,0,r6
	stvx	v21,off16,r6
	stvx	v22,off32,r6
	stvx	v23,off48,r6
	stvx	v24,off64,r6
	stvx	v25,off80,r6
	stvx	v26,off96,r6
	stvx	v27,off112,r6
	stvx	v28,0,r7
	stvx	v29,off16,r7

	mr	r10,r3

	vxor	zeroes,zeroes,zeroes
	vspltisw v0,-1

	vsldoi	mask_32bit,zeroes,v0,4
	vsldoi	mask_64bit,zeroes,v0,8

	/* Get the initial value into v8 */
	vxor	v8,v8,v8
	MTVRD(v8, R3)
#ifdef REFLECT
	vsldoi	v8,zeroes,v8,8	/* shift into bottom 32 bits */
#else
	vsldoi	v8,v8,zeroes,4	/* shift into top 32 bits */
#endif

#ifdef BYTESWAP_DATA
	addis	r3,r2,.byteswap_constant@toc@ha
	addi	r3,r3,.byteswap_constant@toc@l

	lvx	byteswap,0,r3
	addi	r3,r3,16
#endif

	cmpdi	r5,256
	blt	.Lshort

	rldicr	r6,r5,0,56

	/* Checksum in blocks of MAX_SIZE */
1:	lis	r7,MAX_SIZE@h
	ori	r7,r7,MAX_SIZE@l
	mr	r9,r7
	cmpd	r6,r7
	bgt	2f
	mr	r7,r6
2:	subf	r6,r7,r6

	/* our main loop does 128 bytes at a time */
	srdi	r7,r7,7

	/*
	 * Work out the offset into the constants table to start at. Each
	 * constant is 16 bytes, and it is used against 128 bytes of input
	 * data - 128 / 16 = 8
	 */
	sldi	r8,r7,4
	srdi	r9,r9,3
	subf	r8,r8,r9

	/* We reduce our final 128 bytes in a separate step */
	addi	r7,r7,-1
	mtctr	r7

	addis	r3,r2,.constants@toc@ha
	addi	r3,r3,.constants@toc@l

	/* Find the start of our constants */
	add	r3,r3,r8

	/* zero v0-v7 which will contain our checksums */
	vxor	v0,v0,v0
	vxor	v1,v1,v1
	vxor	v2,v2,v2
	vxor	v3,v3,v3
	vxor	v4,v4,v4
	vxor	v5,v5,v5
	vxor	v6,v6,v6
	vxor	v7,v7,v7

	lvx	const1,0,r3

	/*
	 * If we are looping back to consume more data we use the values
	 * already in v16-v23.
	 */
	cmpdi	r0,1
	beq	2f

	/* First warm up pass */
	lvx	v16,0,r4
	lvx	v17,off16,r4
	VPERM(v16,v16,v16,byteswap)
	VPERM(v17,v17,v17,byteswap)
	lvx	v18,off32,r4
	lvx	v19,off48,r4
	VPERM(v18,v18,v18,byteswap)
	VPERM(v19,v19,v19,byteswap)
	lvx	v20,off64,r4
	lvx	v21,off80,r4
	VPERM(v20,v20,v20,byteswap)
	VPERM(v21,v21,v21,byteswap)
	lvx	v22,off96,r4
	lvx	v23,off112,r4
	VPERM(v22,v22,v22,byteswap)
	VPERM(v23,v23,v23,byteswap)
	addi	r4,r4,8*16

	/* xor in initial value */
	vxor	v16,v16,v8

2:	bdz	.Lfirst_warm_up_done

	addi	r3,r3,16
	lvx	const2,0,r3

	/* Second warm up pass */
	VPMSUMD(v8,v16,const1)
	lvx	v16,0,r4
	VPERM(v16,v16,v16,byteswap)
	ori	r2,r2,0

	VPMSUMD(v9,v17,const1)
	lvx	v17,off16,r4
	VPERM(v17,v17,v17,byteswap)
	ori	r2,r2,0

	VPMSUMD(v10,v18,const1)
	lvx	v18,off32,r4
	VPERM(v18,v18,v18,byteswap)
	ori	r2,r2,0

	VPMSUMD(v11,v19,const1)
	lvx	v19,off48,r4
	VPERM(v19,v19,v19,byteswap)
	ori	r2,r2,0

	VPMSUMD(v12,v20,const1)
	lvx	v20,off64,r4
	VPERM(v20,v20,v20,byteswap)
	ori	r2,r2,0

	VPMSUMD(v13,v21,const1)
	lvx	v21,off80,r4
	VPERM(v21,v21,v21,byteswap)
	ori	r2,r2,0

	VPMSUMD(v14,v22,const1)
	lvx	v22,off96,r4
	VPERM(v22,v22,v22,byteswap)
	ori	r2,r2,0

	VPMSUMD(v15,v23,const1)
	lvx	v23,off112,r4
	VPERM(v23,v23,v23,byteswap)

	addi	r4,r4,8*16

	bdz	.Lfirst_cool_down

	/*
	 * main loop. We modulo schedule it such that it takes three iterations
	 * to complete - first iteration load, second iteration vpmsum, third
	 * iteration xor.
	 */
	.balign	16
4:	lvx	const1,0,r3
	addi	r3,r3,16
	ori	r2,r2,0

	vxor	v0,v0,v8
	VPMSUMD(v8,v16,const2)
	lvx	v16,0,r4
	VPERM(v16,v16,v16,byteswap)
	ori	r2,r2,0

	vxor	v1,v1,v9
	VPMSUMD(v9,v17,const2)
	lvx	v17,off16,r4
	VPERM(v17,v17,v17,byteswap)
	ori	r2,r2,0

	vxor	v2,v2,v10
	VPMSUMD(v10,v18,const2)
	lvx	v18,off32,r4
	VPERM(v18,v18,v18,byteswap)
	ori	r2,r2,0

	vxor	v3,v3,v11
	VPMSUMD(v11,v19,const2)
	lvx	v19,off48,r4
	VPERM(v19,v19,v19,byteswap)
	lvx	const2,0,r3
	ori	r2,r2,0

	vxor	v4,v4,v12
	VPMSUMD(v12,v20,const1)
	lvx	v20,off64,r4
	VPERM(v20,v20,v20,byteswap)
	ori	r2,r2,0

	vxor	v5,v5,v13
	VPMSUMD(v13,v21,const1)
	lvx	v21,off80,r4
	VPERM(v21,v21,v21,byteswap)
	ori	r2,r2,0

	vxor	v6,v6,v14
	VPMSUMD(v14,v22,const1)
	lvx	v22,off96,r4
	VPERM(v22,v22,v22,byteswap)
	ori	r2,r2,0

	vxor	v7,v7,v15
	VPMSUMD(v15,v23,const1)
	lvx	v23,off112,r4
	VPERM(v23,v23,v23,byteswap)

	addi	r4,r4,8*16

	bdnz	4b

.Lfirst_cool_down:
	/* First cool down pass */
	lvx	const1,0,r3
	addi	r3,r3,16

	vxor	v0,v0,v8
	VPMSUMD(v8,v16,const1)
	ori	r2,r2,0

	vxor	v1,v1,v9
	VPMSUMD(v9,v17,const1)
	ori	r2,r2,0

	vxor	v2,v2,v10
	VPMSUMD(v10,v18,const1)
	ori	r2,r2,0

	vxor	v3,v3,v11
	VPMSUMD(v11,v19,const1)
	ori	r2,r2,0

	vxor	v4,v4,v12
	VPMSUMD(v12,v20,const1)
	ori	r2,r2,0

	vxor	v5,v5,v13
	VPMSUMD(v13,v21,const1)
	ori	r2,r2,0

	vxor	v6,v6,v14
	VPMSUMD(v14,v22,const1)
	ori	r2,r2,0

	vxor	v7,v7,v15
	VPMSUMD(v15,v23,const1)
	ori	r2,r2,0

.Lsecond_cool_down:
	/* Second cool down pass */
	vxor	v0,v0,v8
	vxor	v1,v1,v9
	vxor	v2,v2,v10
	vxor	v3,v3,v11
	vxor	v4,v4,v12
	vxor	v5,v5,v13
	vxor	v6,v6,v14
	vxor	v7,v7,v15

#ifdef REFLECT
	/*
	 * vpmsumd produces a 96 bit result in the least significant bits
	 * of the register. Since we are bit reflected we have to shift it
	 * left 32 bits so it occupies the least significant bits in the
	 * bit reflected domain.
	 */
	vsldoi	v0,v0,zeroes,4
	vsldoi	v1,v1,zeroes,4
	vsldoi	v2,v2,zeroes,4
	vsldoi	v3,v3,zeroes,4
	vsldoi	v4,v4,zeroes,4
	vsldoi	v5,v5,zeroes,4
	vsldoi	v6,v6,zeroes,4
	vsldoi	v7,v7,zeroes,4
#endif

	/* xor with last 1024 bits */
	lvx	v8,0,r4
	lvx	v9,off16,r4
	VPERM(v8,v8,v8,byteswap)
	VPERM(v9,v9,v9,byteswap)
	lvx	v10,off32,r4
	lvx	v11,off48,r4
	VPERM(v10,v10,v10,byteswap)
	VPERM(v11,v11,v11,byteswap)
	lvx	v12,off64,r4
	lvx	v13,off80,r4
	VPERM(v12,v12,v12,byteswap)
	VPERM(v13,v13,v13,byteswap)
	lvx	v14,off96,r4
	lvx	v15,off112,r4
	VPERM(v14,v14,v14,byteswap)
	VPERM(v15,v15,v15,byteswap)

	addi	r4,r4,8*16

	vxor	v16,v0,v8
	vxor	v17,v1,v9
	vxor	v18,v2,v10
	vxor	v19,v3,v11
	vxor	v20,v4,v12
	vxor	v21,v5,v13
	vxor	v22,v6,v14
	vxor	v23,v7,v15

	li	r0,1
	cmpdi	r6,0
	addi	r6,r6,128
	bne	1b

	/* Work out how many bytes we have left */
	andi.	r5,r5,127

	/* Calculate where in the constant table we need to start */
	subfic	r6,r5,128
	add	r3,r3,r6

	/* How many 16 byte chunks are in the tail */
	srdi	r7,r5,4
	mtctr	r7

	/*
	 * Reduce the previously calculated 1024 bits to 64 bits, shifting
	 * 32 bits to include the trailing 32 bits of zeros
	 */
	lvx	v0,0,r3
	lvx	v1,off16,r3
	lvx	v2,off32,r3
	lvx	v3,off48,r3
	lvx	v4,off64,r3
	lvx	v5,off80,r3
	lvx	v6,off96,r3
	lvx	v7,off112,r3
	addi	r3,r3,8*16

	VPMSUMW(v0,v16,v0)
	VPMSUMW(v1,v17,v1)
	VPMSUMW(v2,v18,v2)
	VPMSUMW(v3,v19,v3)
	VPMSUMW(v4,v20,v4)
	VPMSUMW(v5,v21,v5)
	VPMSUMW(v6,v22,v6)
	VPMSUMW(v7,v23,v7)

	/* Now reduce the tail (0 - 112 bytes) */
	cmpdi	r7,0
	beq	1f

	lvx	v16,0,r4
	lvx	v17,0,r3
	VPERM(v16,v16,v16,byteswap)
	VPMSUMW(v16,v16,v17)
	vxor	v0,v0,v16
	bdz	1f

	lvx	v16,off16,r4
	lvx	v17,off16,r3
	VPERM(v16,v16,v16,byteswap)
	VPMSUMW(v16,v16,v17)
	vxor	v0,v0,v16
	bdz	1f

	lvx	v16,off32,r4
	lvx	v17,off32,r3
	VPERM(v16,v16,v16,byteswap)
	VPMSUMW(v16,v16,v17)
	vxor	v0,v0,v16
	bdz	1f

	lvx	v16,off48,r4
	lvx	v17,off48,r3
	VPERM(v16,v16,v16,byteswap)
	VPMSUMW(v16,v16,v17)
	vxor	v0,v0,v16
	bdz	1f

	lvx	v16,off64,r4
	lvx	v17,off64,r3
	VPERM(v16,v16,v16,byteswap)
	VPMSUMW(v16,v16,v17)
	vxor	v0,v0,v16
	bdz	1f

	lvx	v16,off80,r4
	lvx	v17,off80,r3
	VPERM(v16,v16,v16,byteswap)
	VPMSUMW(v16,v16,v17)
	vxor	v0,v0,v16
	bdz	1f

	lvx	v16,off96,r4
	lvx	v17,off96,r3
	VPERM(v16,v16,v16,byteswap)
	VPMSUMW(v16,v16,v17)
	vxor	v0,v0,v16

	/* Now xor all the parallel chunks together */
1:	vxor	v0,v0,v1
	vxor	v2,v2,v3
	vxor	v4,v4,v5
	vxor	v6,v6,v7

	vxor	v0,v0,v2
	vxor	v4,v4,v6

	vxor	v0,v0,v4

.Lbarrett_reduction:
	/* Barrett constants */
	addis	r3,r2,.barrett_constants@toc@ha
	addi	r3,r3,.barrett_constants@toc@l

	lvx	const1,0,r3
	lvx	const2,off16,r3

	vsldoi	v1,v0,v0,8
	vxor	v0,v0,v1		/* xor two 64 bit results together */

#ifdef REFLECT
	/* shift left one bit */
	vspltisb v1,1
	vsl	v0,v0,v1
#endif

	vand	v0,v0,mask_64bit
#ifndef REFLECT
	/*
	 * Now for the Barrett reduction algorithm. The idea is to calculate q,
	 * the multiple of our polynomial that we need to subtract. By
	 * doing the computation 2x bits higher (ie 64 bits) and shifting the
	 * result back down 2x bits, we round down to the nearest multiple.
	 */
	VPMSUMD(v1,v0,const1)	/* ma */
	vsldoi	v1,zeroes,v1,8	/* q = floor(ma/(2^64)) */
	VPMSUMD(v1,v1,const2)	/* qn */
	vxor	v0,v0,v1	/* a - qn, subtraction is xor in GF(2) */

	/*
	 * Get the result into r3. We need to shift it left 8 bytes:
	 * V0 [ 0 1 2 X ]
	 * V0 [ 0 X 2 3 ]
	 */
	vsldoi	v0,v0,zeroes,8	/* shift result into top 64 bits */
#else
	/*
	 * The reflected version of Barrett reduction. Instead of bit
	 * reflecting our data (which is expensive to do), we bit reflect our
	 * constants and our algorithm, which means the intermediate data in
	 * our vector registers goes from 0-63 instead of 63-0. We can reflect
	 * the algorithm because we don't carry in mod 2 arithmetic.
	 */
	vand	v1,v0,mask_32bit	/* bottom 32 bits of a */
	VPMSUMD(v1,v1,const1)		/* ma */
	vand	v1,v1,mask_32bit	/* bottom 32bits of ma */
	VPMSUMD(v1,v1,const2)		/* qn */
	vxor	v0,v0,v1		/* a - qn, subtraction is xor in GF(2) */

	/*
	 * Since we are bit reflected, the result (ie the low 32 bits) is in
	 * the high 32 bits. We just need to shift it left 4 bytes
	 * V0 [ 0 1 X 3 ]
	 * V0 [ 0 X 2 3 ]
	 */
	vsldoi	v0,v0,zeroes,4		/* shift result into top 64 bits of */
#endif

	/* Get it into r3 */
	MFVRD(R3, v0)

.Lout:
	subi	r6,r1,56+10*16
	subi	r7,r1,56+2*16

	lvx	v20,0,r6
	lvx	v21,off16,r6
	lvx	v22,off32,r6
	lvx	v23,off48,r6
	lvx	v24,off64,r6
	lvx	v25,off80,r6
	lvx	v26,off96,r6
	lvx	v27,off112,r6
	lvx	v28,0,r7
	lvx	v29,off16,r7

	ld	r31,-8(r1)
	ld	r30,-16(r1)
	ld	r29,-24(r1)
	ld	r28,-32(r1)
	ld	r27,-40(r1)
	ld	r26,-48(r1)
	ld	r25,-56(r1)

	blr

.Lfirst_warm_up_done:
	lvx	const1,0,r3
	addi	r3,r3,16

	VPMSUMD(v8,v16,const1)
	VPMSUMD(v9,v17,const1)
	VPMSUMD(v10,v18,const1)
	VPMSUMD(v11,v19,const1)
	VPMSUMD(v12,v20,const1)
	VPMSUMD(v13,v21,const1)
	VPMSUMD(v14,v22,const1)
	VPMSUMD(v15,v23,const1)

	b	.Lsecond_cool_down

.Lshort:
	cmpdi	r5,0
	beq	.Lzero

	addis	r3,r2,.short_constants@toc@ha
	addi	r3,r3,.short_constants@toc@l

	/* Calculate where in the constant table we need to start */
	subfic	r6,r5,256
	add	r3,r3,r6

	/* How many 16 byte chunks? */
	srdi	r7,r5,4
	mtctr	r7

	vxor	v19,v19,v19
	vxor	v20,v20,v20

	lvx	v0,0,r4
	lvx	v16,0,r3
	VPERM(v0,v0,v16,byteswap)
	vxor	v0,v0,v8	/* xor in initial value */
	VPMSUMW(v0,v0,v16)
	bdz	.Lv0

	lvx	v1,off16,r4
	lvx	v17,off16,r3
	VPERM(v1,v1,v17,byteswap)
	VPMSUMW(v1,v1,v17)
	bdz	.Lv1

	lvx	v2,off32,r4
	lvx	v16,off32,r3
	VPERM(v2,v2,v16,byteswap)
	VPMSUMW(v2,v2,v16)
	bdz	.Lv2

	lvx	v3,off48,r4
	lvx	v17,off48,r3
	VPERM(v3,v3,v17,byteswap)
	VPMSUMW(v3,v3,v17)
	bdz	.Lv3

	lvx	v4,off64,r4
	lvx	v16,off64,r3
	VPERM(v4,v4,v16,byteswap)
	VPMSUMW(v4,v4,v16)
	bdz	.Lv4

	lvx	v5,off80,r4
	lvx	v17,off80,r3
	VPERM(v5,v5,v17,byteswap)
	VPMSUMW(v5,v5,v17)
	bdz	.Lv5

	lvx	v6,off96,r4
	lvx	v16,off96,r3
	VPERM(v6,v6,v16,byteswap)
	VPMSUMW(v6,v6,v16)
	bdz	.Lv6

	lvx	v7,off112,r4
	lvx	v17,off112,r3
	VPERM(v7,v7,v17,byteswap)
	VPMSUMW(v7,v7,v17)
	bdz	.Lv7

	addi	r3,r3,128
	addi	r4,r4,128

	lvx	v8,0,r4
	lvx	v16,0,r3
	VPERM(v8,v8,v16,byteswap)
	VPMSUMW(v8,v8,v16)
	bdz	.Lv8

	lvx	v9,off16,r4
	lvx	v17,off16,r3
	VPERM(v9,v9,v17,byteswap)
	VPMSUMW(v9,v9,v17)
	bdz	.Lv9

	lvx	v10,off32,r4
	lvx	v16,off32,r3
	VPERM(v10,v10,v16,byteswap)
	VPMSUMW(v10,v10,v16)
	bdz	.Lv10

	lvx	v11,off48,r4
	lvx	v17,off48,r3
	VPERM(v11,v11,v17,byteswap)
	VPMSUMW(v11,v11,v17)
	bdz	.Lv11

	lvx	v12,off64,r4
	lvx	v16,off64,r3
	VPERM(v12,v12,v16,byteswap)
	VPMSUMW(v12,v12,v16)
	bdz	.Lv12

	lvx	v13,off80,r4
	lvx	v17,off80,r3
	VPERM(v13,v13,v17,byteswap)
	VPMSUMW(v13,v13,v17)
	bdz	.Lv13

	lvx	v14,off96,r4
	lvx	v16,off96,r3
	VPERM(v14,v14,v16,byteswap)
	VPMSUMW(v14,v14,v16)
	bdz	.Lv14

	lvx	v15,off112,r4
	lvx	v17,off112,r3
	VPERM(v15,v15,v17,byteswap)
	VPMSUMW(v15,v15,v17)

.Lv15:	vxor	v19,v19,v15
.Lv14:	vxor	v20,v20,v14
.Lv13:	vxor	v19,v19,v13
.Lv12:	vxor	v20,v20,v12
.Lv11:	vxor	v19,v19,v11
.Lv10:	vxor	v20,v20,v10
.Lv9:	vxor	v19,v19,v9
.Lv8:	vxor	v20,v20,v8
.Lv7:	vxor	v19,v19,v7
.Lv6:	vxor	v20,v20,v6
.Lv5:	vxor	v19,v19,v5
.Lv4:	vxor	v20,v20,v4
.Lv3:	vxor	v19,v19,v3
.Lv2:	vxor	v20,v20,v2
.Lv1:	vxor	v19,v19,v1
.Lv0:	vxor	v20,v20,v0

	vxor	v0,v19,v20

	b	.Lbarrett_reduction

.Lzero:
	mr	r3,r10
	b	.Lout

FUNC_END(CRC_FUNCTION_NAME)
