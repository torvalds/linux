/* SPDX-License-Identifier: GPL-2.0-or-later */
#
# Accelerated poly1305 implementation for ppc64le.
#
# Copyright 2023- IBM Corp. All rights reserved
#
#===================================================================================
# Written by Danny Tsen <dtsen@us.ibm.com>
#
# Poly1305 - this version mainly using vector/VSX/Scalar
#  - 26 bits limbs
#  - Handle multiple 64 byte blcok.
#
# Block size 16 bytes
# key = (r, s)
# clamp r &= 0x0FFFFFFC0FFFFFFC 0x0FFFFFFC0FFFFFFF
# p = 2^130 - 5
# a += m
# a = (r + a) % p
# a += s
#
# Improve performance by breaking down polynominal to the sum of products with
#     h4 = m1 * r⁴ + m2 * r³ + m3 * r² + m4 * r
#
#  07/22/21 - this revison based on the above sum of products.  Setup r^4, r^3, r^2, r and s3, s2, s1, s0
#             to 9 vectors for multiplications.
#
# setup r^4, r^3, r^2, r vectors
#    vs    [r^1, r^3, r^2, r^4]
#    vs0 = [r0,.....]
#    vs1 = [r1,.....]
#    vs2 = [r2,.....]
#    vs3 = [r3,.....]
#    vs4 = [r4,.....]
#    vs5 = [r1*5,...]
#    vs6 = [r2*5,...]
#    vs7 = [r2*5,...]
#    vs8 = [r4*5,...]
#
#  Each word in a vector consists a member of a "r/s" in [a * r/s].
#
# r0, r4*5, r3*5, r2*5, r1*5;
# r1, r0,   r4*5, r3*5, r2*5;
# r2, r1,   r0,   r4*5, r3*5;
# r3, r2,   r1,   r0,   r4*5;
# r4, r3,   r2,   r1,   r0  ;
#
#
# poly1305_p10le_4blocks( uint8_t *k, uint32_t mlen, uint8_t *m)
#  k = 32 bytes key
#  r3 = k (r, s)
#  r4 = mlen
#  r5 = m
#
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/asm-compat.h>
#include <linux/linkage.h>

.machine "any"

.text

.macro	SAVE_GPR GPR OFFSET FRAME
	std	\GPR,\OFFSET(\FRAME)
.endm

.macro	SAVE_VRS VRS OFFSET FRAME
	li	16, \OFFSET
	stvx	\VRS, 16, \FRAME
.endm

.macro	SAVE_VSX VSX OFFSET FRAME
	li	16, \OFFSET
	stxvx	\VSX, 16, \FRAME
.endm

.macro	RESTORE_GPR GPR OFFSET FRAME
	ld	\GPR,\OFFSET(\FRAME)
.endm

.macro	RESTORE_VRS VRS OFFSET FRAME
	li	16, \OFFSET
	lvx	\VRS, 16, \FRAME
.endm

.macro	RESTORE_VSX VSX OFFSET FRAME
	li	16, \OFFSET
	lxvx	\VSX, 16, \FRAME
.endm

.macro SAVE_REGS
	mflr 0
	std 0, 16(1)
	stdu 1,-752(1)

	SAVE_GPR 14, 112, 1
	SAVE_GPR 15, 120, 1
	SAVE_GPR 16, 128, 1
	SAVE_GPR 17, 136, 1
	SAVE_GPR 18, 144, 1
	SAVE_GPR 19, 152, 1
	SAVE_GPR 20, 160, 1
	SAVE_GPR 21, 168, 1
	SAVE_GPR 22, 176, 1
	SAVE_GPR 23, 184, 1
	SAVE_GPR 24, 192, 1
	SAVE_GPR 25, 200, 1
	SAVE_GPR 26, 208, 1
	SAVE_GPR 27, 216, 1
	SAVE_GPR 28, 224, 1
	SAVE_GPR 29, 232, 1
	SAVE_GPR 30, 240, 1
	SAVE_GPR 31, 248, 1

	addi	9, 1, 256
	SAVE_VRS 20, 0, 9
	SAVE_VRS 21, 16, 9
	SAVE_VRS 22, 32, 9
	SAVE_VRS 23, 48, 9
	SAVE_VRS 24, 64, 9
	SAVE_VRS 25, 80, 9
	SAVE_VRS 26, 96, 9
	SAVE_VRS 27, 112, 9
	SAVE_VRS 28, 128, 9
	SAVE_VRS 29, 144, 9
	SAVE_VRS 30, 160, 9
	SAVE_VRS 31, 176, 9

	SAVE_VSX 14, 192, 9
	SAVE_VSX 15, 208, 9
	SAVE_VSX 16, 224, 9
	SAVE_VSX 17, 240, 9
	SAVE_VSX 18, 256, 9
	SAVE_VSX 19, 272, 9
	SAVE_VSX 20, 288, 9
	SAVE_VSX 21, 304, 9
	SAVE_VSX 22, 320, 9
	SAVE_VSX 23, 336, 9
	SAVE_VSX 24, 352, 9
	SAVE_VSX 25, 368, 9
	SAVE_VSX 26, 384, 9
	SAVE_VSX 27, 400, 9
	SAVE_VSX 28, 416, 9
	SAVE_VSX 29, 432, 9
	SAVE_VSX 30, 448, 9
	SAVE_VSX 31, 464, 9
.endm # SAVE_REGS

.macro RESTORE_REGS
	addi	9, 1, 256
	RESTORE_VRS 20, 0, 9
	RESTORE_VRS 21, 16, 9
	RESTORE_VRS 22, 32, 9
	RESTORE_VRS 23, 48, 9
	RESTORE_VRS 24, 64, 9
	RESTORE_VRS 25, 80, 9
	RESTORE_VRS 26, 96, 9
	RESTORE_VRS 27, 112, 9
	RESTORE_VRS 28, 128, 9
	RESTORE_VRS 29, 144, 9
	RESTORE_VRS 30, 160, 9
	RESTORE_VRS 31, 176, 9

	RESTORE_VSX 14, 192, 9
	RESTORE_VSX 15, 208, 9
	RESTORE_VSX 16, 224, 9
	RESTORE_VSX 17, 240, 9
	RESTORE_VSX 18, 256, 9
	RESTORE_VSX 19, 272, 9
	RESTORE_VSX 20, 288, 9
	RESTORE_VSX 21, 304, 9
	RESTORE_VSX 22, 320, 9
	RESTORE_VSX 23, 336, 9
	RESTORE_VSX 24, 352, 9
	RESTORE_VSX 25, 368, 9
	RESTORE_VSX 26, 384, 9
	RESTORE_VSX 27, 400, 9
	RESTORE_VSX 28, 416, 9
	RESTORE_VSX 29, 432, 9
	RESTORE_VSX 30, 448, 9
	RESTORE_VSX 31, 464, 9

	RESTORE_GPR 14, 112, 1
	RESTORE_GPR 15, 120, 1
	RESTORE_GPR 16, 128, 1
	RESTORE_GPR 17, 136, 1
	RESTORE_GPR 18, 144, 1
	RESTORE_GPR 19, 152, 1
	RESTORE_GPR 20, 160, 1
	RESTORE_GPR 21, 168, 1
	RESTORE_GPR 22, 176, 1
	RESTORE_GPR 23, 184, 1
	RESTORE_GPR 24, 192, 1
	RESTORE_GPR 25, 200, 1
	RESTORE_GPR 26, 208, 1
	RESTORE_GPR 27, 216, 1
	RESTORE_GPR 28, 224, 1
	RESTORE_GPR 29, 232, 1
	RESTORE_GPR 30, 240, 1
	RESTORE_GPR 31, 248, 1

	addi    1, 1, 752
	ld 0, 16(1)
	mtlr 0
.endm # RESTORE_REGS

#
# p[0] = a0*r0 + a1*r4*5 + a2*r3*5 + a3*r2*5 + a4*r1*5;
# p[1] = a0*r1 + a1*r0   + a2*r4*5 + a3*r3*5 + a4*r2*5;
# p[2] = a0*r2 + a1*r1   + a2*r0   + a3*r4*5 + a4*r3*5;
# p[3] = a0*r3 + a1*r2   + a2*r1   + a3*r0   + a4*r4*5;
# p[4] = a0*r4 + a1*r3   + a2*r2   + a3*r1   + a4*r0  ;
#
#    [r^2, r^3, r^1, r^4]
#    [m3,  m2,  m4,  m1]
#
# multiply odd and even words
.macro mul_odd
	vmulouw	14, 4, 26
	vmulouw	10, 5, 3
	vmulouw	11, 6, 2
	vmulouw	12, 7, 1
	vmulouw	13, 8, 0
	vmulouw	15, 4, 27
	vaddudm	14, 14, 10
	vaddudm	14, 14, 11
	vmulouw	10, 5, 26
	vmulouw	11, 6, 3
	vaddudm	14, 14, 12
	vaddudm	14, 14, 13	# x0
	vaddudm	15, 15, 10
	vaddudm	15, 15, 11
	vmulouw	12, 7, 2
	vmulouw	13, 8, 1
	vaddudm	15, 15, 12
	vaddudm	15, 15, 13	# x1
	vmulouw	16, 4, 28
	vmulouw	10, 5, 27
	vmulouw	11, 6, 26
	vaddudm	16, 16, 10
	vaddudm	16, 16, 11
	vmulouw	12, 7, 3
	vmulouw	13, 8, 2
	vaddudm	16, 16, 12
	vaddudm	16, 16, 13	# x2
	vmulouw	17, 4, 29
	vmulouw	10, 5, 28
	vmulouw	11, 6, 27
	vaddudm	17, 17, 10
	vaddudm	17, 17, 11
	vmulouw	12, 7, 26
	vmulouw	13, 8, 3
	vaddudm	17, 17, 12
	vaddudm	17, 17, 13	# x3
	vmulouw	18, 4, 30
	vmulouw	10, 5, 29
	vmulouw	11, 6, 28
	vaddudm	18, 18, 10
	vaddudm	18, 18, 11
	vmulouw	12, 7, 27
	vmulouw	13, 8, 26
	vaddudm	18, 18, 12
	vaddudm	18, 18, 13	# x4
.endm

.macro mul_even
	vmuleuw	9, 4, 26
	vmuleuw	10, 5, 3
	vmuleuw	11, 6, 2
	vmuleuw	12, 7, 1
	vmuleuw	13, 8, 0
	vaddudm	14, 14, 9
	vaddudm	14, 14, 10
	vaddudm	14, 14, 11
	vaddudm	14, 14, 12
	vaddudm	14, 14, 13	# x0

	vmuleuw	9, 4, 27
	vmuleuw	10, 5, 26
	vmuleuw	11, 6, 3
	vmuleuw	12, 7, 2
	vmuleuw	13, 8, 1
	vaddudm	15, 15, 9
	vaddudm	15, 15, 10
	vaddudm	15, 15, 11
	vaddudm	15, 15, 12
	vaddudm	15, 15, 13	# x1

	vmuleuw	9, 4, 28
	vmuleuw	10, 5, 27
	vmuleuw	11, 6, 26
	vmuleuw	12, 7, 3
	vmuleuw	13, 8, 2
	vaddudm	16, 16, 9
	vaddudm	16, 16, 10
	vaddudm	16, 16, 11
	vaddudm	16, 16, 12
	vaddudm	16, 16, 13	# x2

	vmuleuw	9, 4, 29
	vmuleuw	10, 5, 28
	vmuleuw	11, 6, 27
	vmuleuw	12, 7, 26
	vmuleuw	13, 8, 3
	vaddudm	17, 17, 9
	vaddudm	17, 17, 10
	vaddudm	17, 17, 11
	vaddudm	17, 17, 12
	vaddudm	17, 17, 13	# x3

	vmuleuw	9, 4, 30
	vmuleuw	10, 5, 29
	vmuleuw	11, 6, 28
	vmuleuw	12, 7, 27
	vmuleuw	13, 8, 26
	vaddudm	18, 18, 9
	vaddudm	18, 18, 10
	vaddudm	18, 18, 11
	vaddudm	18, 18, 12
	vaddudm	18, 18, 13	# x4
.endm

#
# poly1305_setup_r
#
# setup r^4, r^3, r^2, r vectors
#    [r, r^3, r^2, r^4]
#    vs0 = [r0,...]
#    vs1 = [r1,...]
#    vs2 = [r2,...]
#    vs3 = [r3,...]
#    vs4 = [r4,...]
#    vs5 = [r4*5,...]
#    vs6 = [r3*5,...]
#    vs7 = [r2*5,...]
#    vs8 = [r1*5,...]
#
# r0, r4*5, r3*5, r2*5, r1*5;
# r1, r0,   r4*5, r3*5, r2*5;
# r2, r1,   r0,   r4*5, r3*5;
# r3, r2,   r1,   r0,   r4*5;
# r4, r3,   r2,   r1,   r0  ;
#
.macro poly1305_setup_r

	# save r
	xxlor	26, 58, 58
	xxlor	27, 59, 59
	xxlor	28, 60, 60
	xxlor	29, 61, 61
	xxlor	30, 62, 62

	xxlxor	31, 31, 31

#    [r, r^3, r^2, r^4]
	# compute r^2
	vmr	4, 26
	vmr	5, 27
	vmr	6, 28
	vmr	7, 29
	vmr	8, 30
	bl	do_mul		# r^2 r^1
	xxpermdi 58, 58, 36, 0x3		# r0
	xxpermdi 59, 59, 37, 0x3		# r1
	xxpermdi 60, 60, 38, 0x3		# r2
	xxpermdi 61, 61, 39, 0x3		# r3
	xxpermdi 62, 62, 40, 0x3		# r4
	xxpermdi 36, 36, 36, 0x3
	xxpermdi 37, 37, 37, 0x3
	xxpermdi 38, 38, 38, 0x3
	xxpermdi 39, 39, 39, 0x3
	xxpermdi 40, 40, 40, 0x3
	vspltisb 13, 2
	vsld	9, 27, 13
	vsld	10, 28, 13
	vsld	11, 29, 13
	vsld	12, 30, 13
	vaddudm	0, 9, 27
	vaddudm	1, 10, 28
	vaddudm	2, 11, 29
	vaddudm	3, 12, 30

	bl	do_mul		# r^4 r^3
	vmrgow	26, 26, 4
	vmrgow	27, 27, 5
	vmrgow	28, 28, 6
	vmrgow	29, 29, 7
	vmrgow	30, 30, 8
	vspltisb 13, 2
	vsld	9, 27, 13
	vsld	10, 28, 13
	vsld	11, 29, 13
	vsld	12, 30, 13
	vaddudm	0, 9, 27
	vaddudm	1, 10, 28
	vaddudm	2, 11, 29
	vaddudm	3, 12, 30

	# r^2 r^4
	xxlor	0, 58, 58
	xxlor	1, 59, 59
	xxlor	2, 60, 60
	xxlor	3, 61, 61
	xxlor	4, 62, 62
	xxlor	5, 32, 32
	xxlor	6, 33, 33
	xxlor	7, 34, 34
	xxlor	8, 35, 35

	vspltw	9, 26, 3
	vspltw	10, 26, 2
	vmrgow	26, 10, 9
	vspltw	9, 27, 3
	vspltw	10, 27, 2
	vmrgow	27, 10, 9
	vspltw	9, 28, 3
	vspltw	10, 28, 2
	vmrgow	28, 10, 9
	vspltw	9, 29, 3
	vspltw	10, 29, 2
	vmrgow	29, 10, 9
	vspltw	9, 30, 3
	vspltw	10, 30, 2
	vmrgow	30, 10, 9

	vsld	9, 27, 13
	vsld	10, 28, 13
	vsld	11, 29, 13
	vsld	12, 30, 13
	vaddudm	0, 9, 27
	vaddudm	1, 10, 28
	vaddudm	2, 11, 29
	vaddudm	3, 12, 30
.endm

SYM_FUNC_START_LOCAL(do_mul)
	mul_odd

	# do reduction ( h %= p )
	# carry reduction
	vspltisb 9, 2
	vsrd	10, 14, 31
	vsrd	11, 17, 31
	vand	7, 17, 25
	vand	4, 14, 25
	vaddudm	18, 18, 11
	vsrd	12, 18, 31
	vaddudm	15, 15, 10

	vsrd	11, 15, 31
	vand	8, 18, 25
	vand	5, 15, 25
	vaddudm	4, 4, 12
	vsld	10, 12, 9
	vaddudm	6, 16, 11

	vsrd	13, 6, 31
	vand	6, 6, 25
	vaddudm	4, 4, 10
	vsrd	10, 4, 31
	vaddudm	7, 7, 13

	vsrd	11, 7, 31
	vand	7, 7, 25
	vand	4, 4, 25
	vaddudm	5, 5, 10
	vaddudm	8, 8, 11
	blr
SYM_FUNC_END(do_mul)

#
# init key
#
.macro do_poly1305_init
	addis	10, 2, rmask@toc@ha
	addi	10, 10, rmask@toc@l

	ld	11, 0(10)
	ld	12, 8(10)

	li	14, 16
	li	15, 32
	addis	10, 2, cnum@toc@ha
	addi	10, 10, cnum@toc@l
	lvx	25, 0, 10	# v25 - mask
	lvx	31, 14, 10	# v31 = 1a
	lvx	19, 15, 10	# v19 = 1 << 24
	lxv	24, 48(10)	# vs24
	lxv	25, 64(10)	# vs25

	# initialize
	# load key from r3 to vectors
	ld	9, 24(3)
	ld	10, 32(3)
	and.	9, 9, 11
	and.	10, 10, 12

	# break 26 bits
	extrdi	14, 9, 26, 38
	extrdi	15, 9, 26, 12
	extrdi	16, 9, 12, 0
	mtvsrdd	58, 0, 14
	insrdi	16, 10, 14, 38
	mtvsrdd	59, 0, 15
	extrdi	17, 10, 26, 24
	mtvsrdd	60, 0, 16
	extrdi	18, 10, 24, 0
	mtvsrdd	61, 0, 17
	mtvsrdd	62, 0, 18

	# r1 = r1 * 5, r2 = r2 * 5, r3 = r3 * 5, r4 = r4 * 5
	li	9, 5
	mtvsrdd	36, 0, 9
	vmulouw	0, 27, 4		# v0 = rr0
	vmulouw	1, 28, 4		# v1 = rr1
	vmulouw	2, 29, 4		# v2 = rr2
	vmulouw	3, 30, 4		# v3 = rr3
.endm

#
# poly1305_p10le_4blocks( uint8_t *k, uint32_t mlen, uint8_t *m)
#  k = 32 bytes key
#  r3 = k (r, s)
#  r4 = mlen
#  r5 = m
#
SYM_FUNC_START(poly1305_p10le_4blocks)
.align 5
	cmpdi	5, 64
	blt	Out_no_poly1305

	SAVE_REGS

	do_poly1305_init

	li	21, 0	# counter to message

	poly1305_setup_r

	# load previous H state
	# break/convert r6 to 26 bits
	ld	9, 0(3)
	ld	10, 8(3)
	ld	19, 16(3)
	sldi	19, 19, 24
	mtvsrdd	41, 0, 19
	extrdi	14, 9, 26, 38
	extrdi	15, 9, 26, 12
	extrdi	16, 9, 12, 0
	mtvsrdd	36, 0, 14
	insrdi	16, 10, 14, 38
	mtvsrdd	37, 0, 15
	extrdi	17, 10, 26, 24
	mtvsrdd	38, 0, 16
	extrdi	18, 10, 24, 0
	mtvsrdd	39, 0, 17
	mtvsrdd	40, 0, 18
	vor	8, 8, 9

	# input m1 m2
	add	20, 4, 21
	xxlor	49, 24, 24
	xxlor	50, 25, 25
	lxvw4x	43, 0, 20
	addi	17, 20, 16
	lxvw4x	44, 0, 17
	vperm	14, 11, 12, 17
	vperm	15, 11, 12, 18
	vand	9, 14, 25	# a0
	vsrd	10, 14, 31	# >> 26
	vsrd	11, 10, 31	# 12 bits left
	vand	10, 10, 25	# a1
	vspltisb 13, 12
	vand	16, 15, 25
	vsld	12, 16, 13
	vor	11, 11, 12
	vand	11, 11, 25	# a2
	vspltisb 13, 14
	vsrd	12, 15, 13	# >> 14
	vsrd	13, 12, 31	# >> 26, a4
	vand	12, 12, 25	# a3

	vaddudm	20, 4, 9
	vaddudm	21, 5, 10
	vaddudm	22, 6, 11
	vaddudm	23, 7, 12
	vaddudm	24, 8, 13

	# m3 m4
	addi	17, 17, 16
	lxvw4x	43, 0, 17
	addi	17, 17, 16
	lxvw4x	44, 0, 17
	vperm	14, 11, 12, 17
	vperm	15, 11, 12, 18
	vand	9, 14, 25	# a0
	vsrd	10, 14, 31	# >> 26
	vsrd	11, 10, 31	# 12 bits left
	vand	10, 10, 25	# a1
	vspltisb 13, 12
	vand	16, 15, 25
	vsld	12, 16, 13
	vspltisb 13, 14
	vor	11, 11, 12
	vand	11, 11, 25	# a2
	vsrd	12, 15, 13	# >> 14
	vsrd	13, 12, 31	# >> 26, a4
	vand	12, 12, 25	# a3

	# Smash 4 message blocks into 5 vectors of [m4,  m2,  m3,  m1]
	vmrgow	4, 9, 20
	vmrgow	5, 10, 21
	vmrgow	6, 11, 22
	vmrgow	7, 12, 23
	vmrgow	8, 13, 24
	vaddudm	8, 8, 19

	addi	5, 5, -64	# len -= 64
	addi	21, 21, 64	# offset += 64

	li      9, 64
	divdu   31, 5, 9

	cmpdi	31, 0
	ble	Skip_block_loop

	mtctr	31

# h4 =   m1 * r⁴ + m2 * r³ + m3 * r² + m4 * r
# Rewrite the polynominal sum of product as follows,
# h1 = (h0 + m1) * r^2,	h2 = (h0 + m2) * r^2
# h3 = (h1 + m3) * r^2,	h4 = (h2 + m4) * r^2  --> (h0 + m1) r*4 + (h3 + m3) r^2, (h0 + m2) r^4 + (h0 + m4) r^2
#  .... Repeat
# h5 = (h3 + m5) * r^2,	h6 = (h4 + m6) * r^2  -->
# h7 = (h5 + m7) * r^2,	h8 = (h6 + m8) * r^1  --> m5 * r^4 + m6 * r^3 + m7 * r^2 + m8 * r
#
loop_4blocks:

	# Multiply odd words and even words
	mul_odd
	mul_even
	# carry reduction
	vspltisb 9, 2
	vsrd	10, 14, 31
	vsrd	11, 17, 31
	vand	7, 17, 25
	vand	4, 14, 25
	vaddudm	18, 18, 11
	vsrd	12, 18, 31
	vaddudm	15, 15, 10

	vsrd	11, 15, 31
	vand	8, 18, 25
	vand	5, 15, 25
	vaddudm	4, 4, 12
	vsld	10, 12, 9
	vaddudm	6, 16, 11

	vsrd	13, 6, 31
	vand	6, 6, 25
	vaddudm	4, 4, 10
	vsrd	10, 4, 31
	vaddudm	7, 7, 13

	vsrd	11, 7, 31
	vand	7, 7, 25
	vand	4, 4, 25
	vaddudm	5, 5, 10
	vaddudm	8, 8, 11

	# input m1  m2  m3  m4
	add	20, 4, 21
	xxlor	49, 24, 24
	xxlor	50, 25, 25
	lxvw4x	43, 0, 20
	addi	17, 20, 16
	lxvw4x	44, 0, 17
	vperm	14, 11, 12, 17
	vperm	15, 11, 12, 18
	addi	17, 17, 16
	lxvw4x	43, 0, 17
	addi	17, 17, 16
	lxvw4x	44, 0, 17
	vperm	17, 11, 12, 17
	vperm	18, 11, 12, 18

	vand	20, 14, 25	# a0
	vand	9, 17, 25	# a0
	vsrd	21, 14, 31	# >> 26
	vsrd	22, 21, 31	# 12 bits left
	vsrd	10, 17, 31	# >> 26
	vsrd	11, 10, 31	# 12 bits left

	vand	21, 21, 25	# a1
	vand	10, 10, 25	# a1

	vspltisb 13, 12
	vand	16, 15, 25
	vsld	23, 16, 13
	vor	22, 22, 23
	vand	22, 22, 25	# a2
	vand	16, 18, 25
	vsld	12, 16, 13
	vor	11, 11, 12
	vand	11, 11, 25	# a2
	vspltisb 13, 14
	vsrd	23, 15, 13	# >> 14
	vsrd	24, 23, 31	# >> 26, a4
	vand	23, 23, 25	# a3
	vsrd	12, 18, 13	# >> 14
	vsrd	13, 12, 31	# >> 26, a4
	vand	12, 12, 25	# a3

	vaddudm	4, 4, 20
	vaddudm	5, 5, 21
	vaddudm	6, 6, 22
	vaddudm	7, 7, 23
	vaddudm	8, 8, 24

	# Smash 4 message blocks into 5 vectors of [m4,  m2,  m3,  m1]
	vmrgow	4, 9, 4
	vmrgow	5, 10, 5
	vmrgow	6, 11, 6
	vmrgow	7, 12, 7
	vmrgow	8, 13, 8
	vaddudm	8, 8, 19

	addi	5, 5, -64	# len -= 64
	addi	21, 21, 64	# offset += 64

	bdnz	loop_4blocks

Skip_block_loop:
	xxlor	58, 0, 0
	xxlor	59, 1, 1
	xxlor	60, 2, 2
	xxlor	61, 3, 3
	xxlor	62, 4, 4
	xxlor	32, 5, 5
	xxlor	33, 6, 6
	xxlor	34, 7, 7
	xxlor	35, 8, 8

	# Multiply odd words and even words
	mul_odd
	mul_even

	# Sum the products.
	xxpermdi 41, 31, 46, 0
	xxpermdi 42, 31, 47, 0
	vaddudm	4, 14, 9
	xxpermdi 36, 31, 36, 3
	vaddudm	5, 15, 10
	xxpermdi 37, 31, 37, 3
	xxpermdi 43, 31, 48, 0
	vaddudm	6, 16, 11
	xxpermdi 38, 31, 38, 3
	xxpermdi 44, 31, 49, 0
	vaddudm	7, 17, 12
	xxpermdi 39, 31, 39, 3
	xxpermdi 45, 31, 50, 0
	vaddudm	8, 18, 13
	xxpermdi 40, 31, 40, 3

	# carry reduction
	vspltisb 9, 2
	vsrd	10, 4, 31
	vsrd	11, 7, 31
	vand	7, 7, 25
	vand	4, 4, 25
	vaddudm	8, 8, 11
	vsrd	12, 8, 31
	vaddudm	5, 5, 10

	vsrd	11, 5, 31
	vand	8, 8, 25
	vand	5, 5, 25
	vaddudm	4, 4, 12
	vsld	10, 12, 9
	vaddudm	6, 6, 11

	vsrd	13, 6, 31
	vand	6, 6, 25
	vaddudm	4, 4, 10
	vsrd	10, 4, 31
	vaddudm	7, 7, 13

	vsrd	11, 7, 31
	vand	7, 7, 25
	vand	4, 4, 25
	vaddudm	5, 5, 10
	vsrd	10, 5, 31
	vand	5, 5, 25
	vaddudm	6, 6, 10
	vaddudm	8, 8, 11

	b	do_final_update

do_final_update:
	# combine 26 bit limbs
	# v4, v5, v6, v7 and v8 are 26 bit vectors
	vsld	5, 5, 31
	vor	20, 4, 5
	vspltisb 11, 12
	vsrd	12, 6, 11
	vsld	6, 6, 31
	vsld	6, 6, 31
	vor	20, 20, 6
	vspltisb 11, 14
	vsld	7, 7, 11
	vor	21, 7, 12
	mfvsrld	16, 40		# save last 2 bytes
	vsld	8, 8, 11
	vsld	8, 8, 31
	vor	21, 21, 8
	mfvsrld	17, 52
	mfvsrld	19, 53
	srdi	16, 16, 24

	std	17, 0(3)
	std	19, 8(3)
	stw	16, 16(3)

Out_loop:
	li	3, 0

	RESTORE_REGS

	blr

Out_no_poly1305:
	li	3, 0
	blr
SYM_FUNC_END(poly1305_p10le_4blocks)

#
# =======================================================================
# The following functions implement 64 x 64 bits multiplication poly1305.
#
SYM_FUNC_START_LOCAL(Poly1305_init_64)
	#  mask 0x0FFFFFFC0FFFFFFC
	#  mask 0x0FFFFFFC0FFFFFFF
	addis	10, 2, rmask@toc@ha
	addi	10, 10, rmask@toc@l
	ld	11, 0(10)
	ld	12, 8(10)

	# initialize
	# load key from r3
	ld	9, 24(3)
	ld	10, 32(3)
	and.	9, 9, 11	# cramp mask r0
	and.	10, 10, 12	# cramp mask r1

        srdi    21, 10, 2
        add     19, 21, 10      # s1: r19 - (r1 >> 2) *5

        # setup r and s
        li      25, 0
	mtvsrdd 32+0, 9, 19	# r0, s1
	mtvsrdd 32+1, 10, 9	# r1, r0
	mtvsrdd 32+2, 19, 25	# s1
	mtvsrdd 32+3, 9, 25	# r0

	blr
SYM_FUNC_END(Poly1305_init_64)

# Poly1305_mult
# v6 = (h0, h1), v8 = h2
# v0 = (r0, s1), v1 = (r1, r0), v2 = s1, v3 = r0
#
# Output: v7, v10, v11
#
SYM_FUNC_START_LOCAL(Poly1305_mult)
	#
	#	d0 = h0 * r0 + h1 * s1
	vmsumudm	7, 6, 0, 9		# h0 * r0, h1 * s1

	#	d1 = h0 * r1 + h1 * r0 + h2 * s1
	vmsumudm	11, 6, 1, 9		# h0 * r1, h1 * r0
	vmsumudm	10, 8, 2, 11		# d1 += h2 * s1

	#       d2 = r0
	vmsumudm	11, 8, 3, 9		# d2 = h2 * r0
	blr
SYM_FUNC_END(Poly1305_mult)

#
# carry reduction
# h %=p
#
# Input: v7, v10, v11
# Output: r27, r28, r29
#
SYM_FUNC_START_LOCAL(Carry_reduction)
	mfvsrld	27, 32+7
	mfvsrld	28, 32+10
	mfvsrld	29, 32+11
	mfvsrd	20, 32+7	# h0.h
	mfvsrd	21, 32+10	# h1.h

	addc	28, 28, 20
	adde	29, 29, 21
	srdi	22, 29, 0x2
	sldi	23, 22, 0x2
	add	23, 23, 22	# (h2 & 3) * 5
	addc	27, 27, 23	# h0
	addze	28, 28		# h1
	andi.	29, 29, 0x3	# h2
	blr
SYM_FUNC_END(Carry_reduction)

#
# poly1305 multiplication
# h *= r, h %= p
#	d0 = h0 * r0 + h1 * s1
#	d1 = h0 * r1 + h1 * r0 + h2 * s1
#       d2 = h0 * r0
#
#
# unsigned int poly1305_test_64s(unisgned char *state, const byte *src, size_t len, highbit)
#   - no highbit if final leftover block (highbit = 0)
#
SYM_FUNC_START(poly1305_64s)
	cmpdi	5, 0
	ble	Out_no_poly1305_64

	mflr 0
	std 0, 16(1)
	stdu 1,-400(1)

	SAVE_GPR 14, 112, 1
	SAVE_GPR 15, 120, 1
	SAVE_GPR 16, 128, 1
	SAVE_GPR 17, 136, 1
	SAVE_GPR 18, 144, 1
	SAVE_GPR 19, 152, 1
	SAVE_GPR 20, 160, 1
	SAVE_GPR 21, 168, 1
	SAVE_GPR 22, 176, 1
	SAVE_GPR 23, 184, 1
	SAVE_GPR 24, 192, 1
	SAVE_GPR 25, 200, 1
	SAVE_GPR 26, 208, 1
	SAVE_GPR 27, 216, 1
	SAVE_GPR 28, 224, 1
	SAVE_GPR 29, 232, 1
	SAVE_GPR 30, 240, 1
	SAVE_GPR 31, 248, 1

	# Init poly1305
	bl Poly1305_init_64

	li 25, 0			# offset to inp and outp

	add 11, 25, 4

	# load h
	# h0, h1, h2?
        ld	27, 0(3)
        ld	28, 8(3)
        lwz	29, 16(3)

        li      30, 16
        divdu   31, 5, 30

        mtctr   31

        mr      24, 6		# highbit

Loop_block_64:
	vxor	9, 9, 9

	ld	20, 0(11)
	ld	21, 8(11)
	addi	11, 11, 16

	addc	27, 27, 20
	adde	28, 28, 21
	adde	29, 29, 24

	li	22, 0
	mtvsrdd	32+6, 27, 28	# h0, h1
	mtvsrdd	32+8, 29, 22	# h2

	bl	Poly1305_mult

	bl	Carry_reduction

	bdnz	Loop_block_64

	std	27, 0(3)
	std	28, 8(3)
	stw	29, 16(3)

	li	3, 0

	RESTORE_GPR 14, 112, 1
	RESTORE_GPR 15, 120, 1
	RESTORE_GPR 16, 128, 1
	RESTORE_GPR 17, 136, 1
	RESTORE_GPR 18, 144, 1
	RESTORE_GPR 19, 152, 1
	RESTORE_GPR 20, 160, 1
	RESTORE_GPR 21, 168, 1
	RESTORE_GPR 22, 176, 1
	RESTORE_GPR 23, 184, 1
	RESTORE_GPR 24, 192, 1
	RESTORE_GPR 25, 200, 1
	RESTORE_GPR 26, 208, 1
	RESTORE_GPR 27, 216, 1
	RESTORE_GPR 28, 224, 1
	RESTORE_GPR 29, 232, 1
	RESTORE_GPR 30, 240, 1
	RESTORE_GPR 31, 248, 1

	addi    1, 1, 400
	ld 0, 16(1)
	mtlr 0

	blr

Out_no_poly1305_64:
	li	3, 0
	blr
SYM_FUNC_END(poly1305_64s)

#
# Input: r3 = h, r4 = s, r5 = mac
# mac = h + s
#
SYM_FUNC_START(poly1305_emit_64)
	ld	10, 0(3)
	ld	11, 8(3)
	ld	12, 16(3)

	# compare modulus
	# h + 5 + (-p)
	mr	6, 10
	mr	7, 11
	mr	8, 12
	addic.	6, 6, 5
	addze	7, 7
	addze	8, 8
	srdi	9, 8, 2		# overflow?
	cmpdi	9, 0
	beq	Skip_h64
	mr	10, 6
	mr	11, 7
	mr	12, 8

Skip_h64:
	ld	6, 0(4)
	ld	7, 8(4)
	addc	10, 10, 6
	adde	11, 11, 7
	addze	12, 12

	std	10, 0(5)
	std	11, 8(5)
	blr
SYM_FUNC_END(poly1305_emit_64)

SYM_DATA_START_LOCAL(RMASK)
.align 5
rmask:
.byte	0xff, 0xff, 0xff, 0x0f, 0xfc, 0xff, 0xff, 0x0f, 0xfc, 0xff, 0xff, 0x0f, 0xfc, 0xff, 0xff, 0x0f
cnum:
.long	0x03ffffff, 0x00000000, 0x03ffffff, 0x00000000
.long	0x1a, 0x00, 0x1a, 0x00
.long	0x01000000, 0x01000000, 0x01000000, 0x01000000
.long	0x00010203, 0x04050607, 0x10111213, 0x14151617
.long	0x08090a0b, 0x0c0d0e0f, 0x18191a1b, 0x1c1d1e1f
SYM_DATA_END(RMASK)
