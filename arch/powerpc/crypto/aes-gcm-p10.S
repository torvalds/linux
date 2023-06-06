/* SPDX-License-Identifier: GPL-2.0-or-later */
 #
 # Accelerated AES-GCM stitched implementation for ppc64le.
 #
 # Copyright 2022- IBM Inc. All rights reserved
 #
 #===================================================================================
 # Written by Danny Tsen <dtsen@linux.ibm.com>
 #
 # GHASH is based on the Karatsuba multiplication method.
 #
 #    Xi xor X1
 #
 #    X1 * H^4 + X2 * H^3 + x3 * H^2 + X4 * H =
 #      (X1.h * H4.h + xX.l * H4.l + X1 * H4) +
 #      (X2.h * H3.h + X2.l * H3.l + X2 * H3) +
 #      (X3.h * H2.h + X3.l * H2.l + X3 * H2) +
 #      (X4.h * H.h + X4.l * H.l + X4 * H)
 #
 # Xi = v0
 # H Poly = v2
 # Hash keys = v3 - v14
 #     ( H.l, H, H.h)
 #     ( H^2.l, H^2, H^2.h)
 #     ( H^3.l, H^3, H^3.h)
 #     ( H^4.l, H^4, H^4.h)
 #
 # v30 is IV
 # v31 - counter 1
 #
 # AES used,
 #     vs0 - vs14 for round keys
 #     v15, v16, v17, v18, v19, v20, v21, v22 for 8 blocks (encrypted)
 #
 # This implementation uses stitched AES-GCM approach to improve overall performance.
 # AES is implemented with 8x blocks and GHASH is using 2 4x blocks.
 #
 # ===================================================================================
 #

#include <asm/ppc_asm.h>
#include <linux/linkage.h>

.machine        "any"
.text

 # 4x loops
 # v15 - v18 - input states
 # vs1 - vs9 - round keys
 #
.macro Loop_aes_middle4x
	xxlor	19+32, 1, 1
	xxlor	20+32, 2, 2
	xxlor	21+32, 3, 3
	xxlor	22+32, 4, 4

	vcipher	15, 15, 19
	vcipher	16, 16, 19
	vcipher	17, 17, 19
	vcipher	18, 18, 19

	vcipher	15, 15, 20
	vcipher	16, 16, 20
	vcipher	17, 17, 20
	vcipher	18, 18, 20

	vcipher	15, 15, 21
	vcipher	16, 16, 21
	vcipher	17, 17, 21
	vcipher	18, 18, 21

	vcipher	15, 15, 22
	vcipher	16, 16, 22
	vcipher	17, 17, 22
	vcipher	18, 18, 22

	xxlor	19+32, 5, 5
	xxlor	20+32, 6, 6
	xxlor	21+32, 7, 7
	xxlor	22+32, 8, 8

	vcipher	15, 15, 19
	vcipher	16, 16, 19
	vcipher	17, 17, 19
	vcipher	18, 18, 19

	vcipher	15, 15, 20
	vcipher	16, 16, 20
	vcipher	17, 17, 20
	vcipher	18, 18, 20

	vcipher	15, 15, 21
	vcipher	16, 16, 21
	vcipher	17, 17, 21
	vcipher	18, 18, 21

	vcipher	15, 15, 22
	vcipher	16, 16, 22
	vcipher	17, 17, 22
	vcipher	18, 18, 22

	xxlor	23+32, 9, 9
	vcipher	15, 15, 23
	vcipher	16, 16, 23
	vcipher	17, 17, 23
	vcipher	18, 18, 23
.endm

 # 8x loops
 # v15 - v22 - input states
 # vs1 - vs9 - round keys
 #
.macro Loop_aes_middle8x
	xxlor	23+32, 1, 1
	xxlor	24+32, 2, 2
	xxlor	25+32, 3, 3
	xxlor	26+32, 4, 4

	vcipher	15, 15, 23
	vcipher	16, 16, 23
	vcipher	17, 17, 23
	vcipher	18, 18, 23
	vcipher	19, 19, 23
	vcipher	20, 20, 23
	vcipher	21, 21, 23
	vcipher	22, 22, 23

	vcipher	15, 15, 24
	vcipher	16, 16, 24
	vcipher	17, 17, 24
	vcipher	18, 18, 24
	vcipher	19, 19, 24
	vcipher	20, 20, 24
	vcipher	21, 21, 24
	vcipher	22, 22, 24

	vcipher	15, 15, 25
	vcipher	16, 16, 25
	vcipher	17, 17, 25
	vcipher	18, 18, 25
	vcipher	19, 19, 25
	vcipher	20, 20, 25
	vcipher	21, 21, 25
	vcipher	22, 22, 25

	vcipher	15, 15, 26
	vcipher	16, 16, 26
	vcipher	17, 17, 26
	vcipher	18, 18, 26
	vcipher	19, 19, 26
	vcipher	20, 20, 26
	vcipher	21, 21, 26
	vcipher	22, 22, 26

	xxlor	23+32, 5, 5
	xxlor	24+32, 6, 6
	xxlor	25+32, 7, 7
	xxlor	26+32, 8, 8

	vcipher	15, 15, 23
	vcipher	16, 16, 23
	vcipher	17, 17, 23
	vcipher	18, 18, 23
	vcipher	19, 19, 23
	vcipher	20, 20, 23
	vcipher	21, 21, 23
	vcipher	22, 22, 23

	vcipher	15, 15, 24
	vcipher	16, 16, 24
	vcipher	17, 17, 24
	vcipher	18, 18, 24
	vcipher	19, 19, 24
	vcipher	20, 20, 24
	vcipher	21, 21, 24
	vcipher	22, 22, 24

	vcipher	15, 15, 25
	vcipher	16, 16, 25
	vcipher	17, 17, 25
	vcipher	18, 18, 25
	vcipher	19, 19, 25
	vcipher	20, 20, 25
	vcipher	21, 21, 25
	vcipher	22, 22, 25

	vcipher	15, 15, 26
	vcipher	16, 16, 26
	vcipher	17, 17, 26
	vcipher	18, 18, 26
	vcipher	19, 19, 26
	vcipher	20, 20, 26
	vcipher	21, 21, 26
	vcipher	22, 22, 26

	xxlor	23+32, 9, 9
	vcipher	15, 15, 23
	vcipher	16, 16, 23
	vcipher	17, 17, 23
	vcipher	18, 18, 23
	vcipher	19, 19, 23
	vcipher	20, 20, 23
	vcipher	21, 21, 23
	vcipher	22, 22, 23
.endm

.macro Loop_aes_middle_1x
	xxlor	19+32, 1, 1
	xxlor	20+32, 2, 2
	xxlor	21+32, 3, 3
	xxlor	22+32, 4, 4

	vcipher 15, 15, 19
	vcipher 15, 15, 20
	vcipher 15, 15, 21
	vcipher 15, 15, 22

	xxlor	19+32, 5, 5
	xxlor	20+32, 6, 6
	xxlor	21+32, 7, 7
	xxlor	22+32, 8, 8

	vcipher 15, 15, 19
	vcipher 15, 15, 20
	vcipher 15, 15, 21
	vcipher 15, 15, 22

	xxlor	19+32, 9, 9
	vcipher 15, 15, 19
.endm

 #
 # Compute 4x hash values based on Karatsuba method.
 #
.macro ppc_aes_gcm_ghash
	vxor		15, 15, 0

	vpmsumd		23, 12, 15		# H4.L * X.L
	vpmsumd		24, 9, 16
	vpmsumd		25, 6, 17
	vpmsumd		26, 3, 18

	vxor		23, 23, 24
	vxor		23, 23, 25
	vxor		23, 23, 26		# L

	vpmsumd		24, 13, 15		# H4.L * X.H + H4.H * X.L
	vpmsumd		25, 10, 16		# H3.L * X1.H + H3.H * X1.L
	vpmsumd		26, 7, 17
	vpmsumd		27, 4, 18

	vxor		24, 24, 25
	vxor		24, 24, 26
	vxor		24, 24, 27		# M

	# sum hash and reduction with H Poly
	vpmsumd		28, 23, 2		# reduction

	vxor		29, 29, 29
	vsldoi		26, 24, 29, 8		# mL
	vsldoi		29, 29, 24, 8		# mH
	vxor		23, 23, 26		# mL + L

	vsldoi		23, 23, 23, 8		# swap
	vxor		23, 23, 28

	vpmsumd		24, 14, 15		# H4.H * X.H
	vpmsumd		25, 11, 16
	vpmsumd		26, 8, 17
	vpmsumd		27, 5, 18

	vxor		24, 24, 25
	vxor		24, 24, 26
	vxor		24, 24, 27

	vxor		24, 24, 29

	# sum hash and reduction with H Poly
	vsldoi		27, 23, 23, 8		# swap
	vpmsumd		23, 23, 2
	vxor		27, 27, 24
	vxor		23, 23, 27

	xxlor		32, 23+32, 23+32		# update hash

.endm

 #
 # Combine two 4x ghash
 # v15 - v22 - input blocks
 #
.macro ppc_aes_gcm_ghash2_4x
	# first 4x hash
	vxor		15, 15, 0		# Xi + X

	vpmsumd		23, 12, 15		# H4.L * X.L
	vpmsumd		24, 9, 16
	vpmsumd		25, 6, 17
	vpmsumd		26, 3, 18

	vxor		23, 23, 24
	vxor		23, 23, 25
	vxor		23, 23, 26		# L

	vpmsumd		24, 13, 15		# H4.L * X.H + H4.H * X.L
	vpmsumd		25, 10, 16		# H3.L * X1.H + H3.H * X1.L
	vpmsumd		26, 7, 17
	vpmsumd		27, 4, 18

	vxor		24, 24, 25
	vxor		24, 24, 26

	# sum hash and reduction with H Poly
	vpmsumd		28, 23, 2		# reduction

	vxor		29, 29, 29

	vxor		24, 24, 27		# M
	vsldoi		26, 24, 29, 8		# mL
	vsldoi		29, 29, 24, 8		# mH
	vxor		23, 23, 26		# mL + L

	vsldoi		23, 23, 23, 8		# swap
	vxor		23, 23, 28

	vpmsumd		24, 14, 15		# H4.H * X.H
	vpmsumd		25, 11, 16
	vpmsumd		26, 8, 17
	vpmsumd		27, 5, 18

	vxor		24, 24, 25
	vxor		24, 24, 26
	vxor		24, 24, 27		# H

	vxor		24, 24, 29		# H + mH

	# sum hash and reduction with H Poly
	vsldoi		27, 23, 23, 8		# swap
	vpmsumd		23, 23, 2
	vxor		27, 27, 24
	vxor		27, 23, 27		# 1st Xi

	# 2nd 4x hash
	vpmsumd		24, 9, 20
	vpmsumd		25, 6, 21
	vpmsumd		26, 3, 22
	vxor		19, 19, 27		# Xi + X
	vpmsumd		23, 12, 19		# H4.L * X.L

	vxor		23, 23, 24
	vxor		23, 23, 25
	vxor		23, 23, 26		# L

	vpmsumd		24, 13, 19		# H4.L * X.H + H4.H * X.L
	vpmsumd		25, 10, 20		# H3.L * X1.H + H3.H * X1.L
	vpmsumd		26, 7, 21
	vpmsumd		27, 4, 22

	vxor		24, 24, 25
	vxor		24, 24, 26

	# sum hash and reduction with H Poly
	vpmsumd		28, 23, 2		# reduction

	vxor		29, 29, 29

	vxor		24, 24, 27		# M
	vsldoi		26, 24, 29, 8		# mL
	vsldoi		29, 29, 24, 8		# mH
	vxor		23, 23, 26		# mL + L

	vsldoi		23, 23, 23, 8		# swap
	vxor		23, 23, 28

	vpmsumd		24, 14, 19		# H4.H * X.H
	vpmsumd		25, 11, 20
	vpmsumd		26, 8, 21
	vpmsumd		27, 5, 22

	vxor		24, 24, 25
	vxor		24, 24, 26
	vxor		24, 24, 27		# H

	vxor		24, 24, 29		# H + mH

	# sum hash and reduction with H Poly
	vsldoi		27, 23, 23, 8		# swap
	vpmsumd		23, 23, 2
	vxor		27, 27, 24
	vxor		23, 23, 27

	xxlor		32, 23+32, 23+32		# update hash

.endm

 #
 # Compute update single hash
 #
.macro ppc_update_hash_1x
	vxor		28, 28, 0

	vxor		19, 19, 19

	vpmsumd		22, 3, 28		# L
	vpmsumd		23, 4, 28		# M
	vpmsumd		24, 5, 28		# H

	vpmsumd		27, 22, 2		# reduction

	vsldoi		25, 23, 19, 8		# mL
	vsldoi		26, 19, 23, 8		# mH
	vxor		22, 22, 25		# LL + LL
	vxor		24, 24, 26		# HH + HH

	vsldoi		22, 22, 22, 8		# swap
	vxor		22, 22, 27

	vsldoi		20, 22, 22, 8		# swap
	vpmsumd		22, 22, 2		# reduction
	vxor		20, 20, 24
	vxor		22, 22, 20

	vmr		0, 22			# update hash

.endm

.macro SAVE_REGS
	stdu 1,-640(1)
	mflr 0

	std	14,112(1)
	std	15,120(1)
	std	16,128(1)
	std	17,136(1)
	std	18,144(1)
	std	19,152(1)
	std	20,160(1)
	std	21,168(1)
	li	9, 256
	stvx	20, 9, 1
	addi	9, 9, 16
	stvx	21, 9, 1
	addi	9, 9, 16
	stvx	22, 9, 1
	addi	9, 9, 16
	stvx	23, 9, 1
	addi	9, 9, 16
	stvx	24, 9, 1
	addi	9, 9, 16
	stvx	25, 9, 1
	addi	9, 9, 16
	stvx	26, 9, 1
	addi	9, 9, 16
	stvx	27, 9, 1
	addi	9, 9, 16
	stvx	28, 9, 1
	addi	9, 9, 16
	stvx	29, 9, 1
	addi	9, 9, 16
	stvx	30, 9, 1
	addi	9, 9, 16
	stvx	31, 9, 1
	stxv	14, 464(1)
	stxv	15, 480(1)
	stxv	16, 496(1)
	stxv	17, 512(1)
	stxv	18, 528(1)
	stxv	19, 544(1)
	stxv	20, 560(1)
	stxv	21, 576(1)
	stxv	22, 592(1)
	std	0, 656(1)
.endm

.macro RESTORE_REGS
	lxv	14, 464(1)
	lxv	15, 480(1)
	lxv	16, 496(1)
	lxv	17, 512(1)
	lxv	18, 528(1)
	lxv	19, 544(1)
	lxv	20, 560(1)
	lxv	21, 576(1)
	lxv	22, 592(1)
	li	9, 256
	lvx	20, 9, 1
	addi	9, 9, 16
	lvx	21, 9, 1
	addi	9, 9, 16
	lvx	22, 9, 1
	addi	9, 9, 16
	lvx	23, 9, 1
	addi	9, 9, 16
	lvx	24, 9, 1
	addi	9, 9, 16
	lvx	25, 9, 1
	addi	9, 9, 16
	lvx	26, 9, 1
	addi	9, 9, 16
	lvx	27, 9, 1
	addi	9, 9, 16
	lvx	28, 9, 1
	addi	9, 9, 16
	lvx	29, 9, 1
	addi	9, 9, 16
	lvx	30, 9, 1
	addi	9, 9, 16
	lvx	31, 9, 1

	ld	0, 656(1)
	ld      14,112(1)
	ld      15,120(1)
	ld      16,128(1)
	ld      17,136(1)
	ld      18,144(1)
	ld      19,152(1)
	ld      20,160(1)
	ld	21,168(1)

	mtlr	0
	addi	1, 1, 640
.endm

.macro LOAD_HASH_TABLE
	# Load Xi
	lxvb16x	32, 0, 8	# load Xi

	# load Hash - h^4, h^3, h^2, h
	li	10, 32
	lxvd2x	2+32, 10, 8	# H Poli
	li	10, 48
	lxvd2x	3+32, 10, 8	# Hl
	li	10, 64
	lxvd2x	4+32, 10, 8	# H
	li	10, 80
	lxvd2x	5+32, 10, 8	# Hh

	li	10, 96
	lxvd2x	6+32, 10, 8	# H^2l
	li	10, 112
	lxvd2x	7+32, 10, 8	# H^2
	li	10, 128
	lxvd2x	8+32, 10, 8	# H^2h

	li	10, 144
	lxvd2x	9+32, 10, 8	# H^3l
	li	10, 160
	lxvd2x	10+32, 10, 8	# H^3
	li	10, 176
	lxvd2x	11+32, 10, 8	# H^3h

	li	10, 192
	lxvd2x	12+32, 10, 8	# H^4l
	li	10, 208
	lxvd2x	13+32, 10, 8	# H^4
	li	10, 224
	lxvd2x	14+32, 10, 8	# H^4h
.endm

 #
 # aes_p10_gcm_encrypt (const void *inp, void *out, size_t len,
 #               const char *rk, unsigned char iv[16], void *Xip);
 #
 #    r3 - inp
 #    r4 - out
 #    r5 - len
 #    r6 - AES round keys
 #    r7 - iv and other data
 #    r8 - Xi, HPoli, hash keys
 #
 #    rounds is at offset 240 in rk
 #    Xi is at 0 in gcm_table (Xip).
 #
_GLOBAL(aes_p10_gcm_encrypt)
.align 5

	SAVE_REGS

	LOAD_HASH_TABLE

	# initialize ICB: GHASH( IV ), IV - r7
	lxvb16x	30+32, 0, 7	# load IV  - v30

	mr	12, 5		# length
	li	11, 0		# block index

	# counter 1
	vxor	31, 31, 31
	vspltisb 22, 1
	vsldoi	31, 31, 22,1	# counter 1

	# load round key to VSR
	lxv	0, 0(6)
	lxv	1, 0x10(6)
	lxv	2, 0x20(6)
	lxv	3, 0x30(6)
	lxv	4, 0x40(6)
	lxv	5, 0x50(6)
	lxv	6, 0x60(6)
	lxv	7, 0x70(6)
	lxv	8, 0x80(6)
	lxv	9, 0x90(6)
	lxv	10, 0xa0(6)

	# load rounds - 10 (128), 12 (192), 14 (256)
	lwz	9,240(6)

	#
	# vxor	state, state, w # addroundkey
	xxlor	32+29, 0, 0
	vxor	15, 30, 29	# IV + round key - add round key 0

	cmpdi	9, 10
	beq	Loop_aes_gcm_8x

	# load 2 more round keys (v11, v12)
	lxv	11, 0xb0(6)
	lxv	12, 0xc0(6)

	cmpdi	9, 12
	beq	Loop_aes_gcm_8x

	# load 2 more round keys (v11, v12, v13, v14)
	lxv	13, 0xd0(6)
	lxv	14, 0xe0(6)
	cmpdi	9, 14
	beq	Loop_aes_gcm_8x

	b	aes_gcm_out

.align 5
Loop_aes_gcm_8x:
	mr	14, 3
	mr	9, 4

	#
	# check partial block
	#
Continue_partial_check:
	ld	15, 56(7)
	cmpdi	15, 0
	beq	Continue
	bgt	Final_block
	cmpdi	15, 16
	blt	Final_block

Continue:
	# n blcoks
	li	10, 128
	divdu	10, 12, 10	# n 128 bytes-blocks
	cmpdi	10, 0
	beq	Loop_last_block

	vaddudm	30, 30, 31	# IV + counter
	vxor	16, 30, 29
	vaddudm	30, 30, 31
	vxor	17, 30, 29
	vaddudm	30, 30, 31
	vxor	18, 30, 29
	vaddudm	30, 30, 31
	vxor	19, 30, 29
	vaddudm	30, 30, 31
	vxor	20, 30, 29
	vaddudm	30, 30, 31
	vxor	21, 30, 29
	vaddudm	30, 30, 31
	vxor	22, 30, 29

	mtctr	10

	li	15, 16
	li	16, 32
	li	17, 48
	li	18, 64
	li	19, 80
	li	20, 96
	li	21, 112

	lwz	10, 240(6)

Loop_8x_block:

	lxvb16x		15, 0, 14	# load block
	lxvb16x		16, 15, 14	# load block
	lxvb16x		17, 16, 14	# load block
	lxvb16x		18, 17, 14	# load block
	lxvb16x		19, 18, 14	# load block
	lxvb16x		20, 19, 14	# load block
	lxvb16x		21, 20, 14	# load block
	lxvb16x		22, 21, 14	# load block
	addi		14, 14, 128

	Loop_aes_middle8x

	xxlor	23+32, 10, 10

	cmpdi	10, 10
	beq	Do_next_ghash

	# 192 bits
	xxlor	24+32, 11, 11

	vcipher	15, 15, 23
	vcipher	16, 16, 23
	vcipher	17, 17, 23
	vcipher	18, 18, 23
	vcipher	19, 19, 23
	vcipher	20, 20, 23
	vcipher	21, 21, 23
	vcipher	22, 22, 23

	vcipher	15, 15, 24
	vcipher	16, 16, 24
	vcipher	17, 17, 24
	vcipher	18, 18, 24
	vcipher	19, 19, 24
	vcipher	20, 20, 24
	vcipher	21, 21, 24
	vcipher	22, 22, 24

	xxlor	23+32, 12, 12

	cmpdi	10, 12
	beq	Do_next_ghash

	# 256 bits
	xxlor	24+32, 13, 13

	vcipher	15, 15, 23
	vcipher	16, 16, 23
	vcipher	17, 17, 23
	vcipher	18, 18, 23
	vcipher	19, 19, 23
	vcipher	20, 20, 23
	vcipher	21, 21, 23
	vcipher	22, 22, 23

	vcipher	15, 15, 24
	vcipher	16, 16, 24
	vcipher	17, 17, 24
	vcipher	18, 18, 24
	vcipher	19, 19, 24
	vcipher	20, 20, 24
	vcipher	21, 21, 24
	vcipher	22, 22, 24

	xxlor	23+32, 14, 14

	cmpdi	10, 14
	beq	Do_next_ghash
	b	aes_gcm_out

Do_next_ghash:

	#
	# last round
	vcipherlast     15, 15, 23
	vcipherlast     16, 16, 23

	xxlxor		47, 47, 15
	stxvb16x        47, 0, 9	# store output
	xxlxor		48, 48, 16
	stxvb16x        48, 15, 9	# store output

	vcipherlast     17, 17, 23
	vcipherlast     18, 18, 23

	xxlxor		49, 49, 17
	stxvb16x        49, 16, 9	# store output
	xxlxor		50, 50, 18
	stxvb16x        50, 17, 9	# store output

	vcipherlast     19, 19, 23
	vcipherlast     20, 20, 23

	xxlxor		51, 51, 19
	stxvb16x        51, 18, 9	# store output
	xxlxor		52, 52, 20
	stxvb16x        52, 19, 9	# store output

	vcipherlast     21, 21, 23
	vcipherlast     22, 22, 23

	xxlxor		53, 53, 21
	stxvb16x        53, 20, 9	# store output
	xxlxor		54, 54, 22
	stxvb16x        54, 21, 9	# store output

	addi		9, 9, 128

	# ghash here
	ppc_aes_gcm_ghash2_4x

	xxlor	27+32, 0, 0
	vaddudm 30, 30, 31		# IV + counter
	vmr	29, 30
	vxor    15, 30, 27		# add round key
	vaddudm 30, 30, 31
	vxor    16, 30, 27
	vaddudm 30, 30, 31
	vxor    17, 30, 27
	vaddudm 30, 30, 31
	vxor    18, 30, 27
	vaddudm 30, 30, 31
	vxor    19, 30, 27
	vaddudm 30, 30, 31
	vxor    20, 30, 27
	vaddudm 30, 30, 31
	vxor    21, 30, 27
	vaddudm 30, 30, 31
	vxor    22, 30, 27

	addi    12, 12, -128
	addi    11, 11, 128

	bdnz	Loop_8x_block

	vmr	30, 29
	stxvb16x 30+32, 0, 7		# update IV

Loop_last_block:
	cmpdi   12, 0
	beq     aes_gcm_out

	# loop last few blocks
	li      10, 16
	divdu   10, 12, 10

	mtctr   10

	lwz	10, 240(6)

	cmpdi   12, 16
	blt     Final_block

Next_rem_block:
	lxvb16x 15, 0, 14		# load block

	Loop_aes_middle_1x

	xxlor	23+32, 10, 10

	cmpdi	10, 10
	beq	Do_next_1x

	# 192 bits
	xxlor	24+32, 11, 11

	vcipher	15, 15, 23
	vcipher	15, 15, 24

	xxlor	23+32, 12, 12

	cmpdi	10, 12
	beq	Do_next_1x

	# 256 bits
	xxlor	24+32, 13, 13

	vcipher	15, 15, 23
	vcipher	15, 15, 24

	xxlor	23+32, 14, 14

	cmpdi	10, 14
	beq	Do_next_1x

Do_next_1x:
	vcipherlast     15, 15, 23

	xxlxor		47, 47, 15
	stxvb16x	47, 0, 9	# store output
	addi		14, 14, 16
	addi		9, 9, 16

	vmr		28, 15
	ppc_update_hash_1x

	addi		12, 12, -16
	addi		11, 11, 16
	xxlor		19+32, 0, 0
	vaddudm		30, 30, 31		# IV + counter
	vxor		15, 30, 19		# add round key

	bdnz	Next_rem_block

	li	15, 0
	std	15, 56(7)		# clear partial?
	stxvb16x 30+32, 0, 7		# update IV
	cmpdi	12, 0
	beq	aes_gcm_out

Final_block:
	lwz	10, 240(6)
	Loop_aes_middle_1x

	xxlor	23+32, 10, 10

	cmpdi	10, 10
	beq	Do_final_1x

	# 192 bits
	xxlor	24+32, 11, 11

	vcipher	15, 15, 23
	vcipher	15, 15, 24

	xxlor	23+32, 12, 12

	cmpdi	10, 12
	beq	Do_final_1x

	# 256 bits
	xxlor	24+32, 13, 13

	vcipher	15, 15, 23
	vcipher	15, 15, 24

	xxlor	23+32, 14, 14

	cmpdi	10, 14
	beq	Do_final_1x

Do_final_1x:
	vcipherlast     15, 15, 23

	# check partial block
	li	21, 0			# encrypt
	ld	15, 56(7)		# partial?
	cmpdi	15, 0
	beq	Normal_block
	bl	Do_partial_block

	cmpdi	12, 0
	ble aes_gcm_out

	b Continue_partial_check

Normal_block:
	lxvb16x	15, 0, 14		# load last block
	xxlxor	47, 47, 15

	# create partial block mask
	li	15, 16
	sub	15, 15, 12		# index to the mask

	vspltisb	16, -1		# first 16 bytes - 0xffff...ff
	vspltisb	17, 0		# second 16 bytes - 0x0000...00
	li	10, 192
	stvx	16, 10, 1
	addi	10, 10, 16
	stvx	17, 10, 1

	addi	10, 1, 192
	lxvb16x	16, 15, 10		# load partial block mask
	xxland	47, 47, 16

	vmr	28, 15
	ppc_update_hash_1x

	# * should store only the remaining bytes.
	bl	Write_partial_block

	stxvb16x 30+32, 0, 7		# update IV
	std	12, 56(7)		# update partial?
	li	16, 16

	stxvb16x	32, 0, 8		# write out Xi
	stxvb16x	32, 16, 8		# write out Xi
	b aes_gcm_out

 #
 # Compute data mask
 #
.macro GEN_MASK _mask _start _end
	vspltisb	16, -1		# first 16 bytes - 0xffff...ff
	vspltisb	17, 0		# second 16 bytes - 0x0000...00
	li	10, 192
	stxvb16x	17+32, 10, 1
	add	10, 10, \_start
	stxvb16x	16+32, 10, 1
	add	10, 10, \_end
	stxvb16x	17+32, 10, 1

	addi	10, 1, 192
	lxvb16x	\_mask, 0, 10		# load partial block mask
.endm

 #
 # Handle multiple partial blocks for encrypt and decrypt
 #   operations.
 #
SYM_FUNC_START_LOCAL(Do_partial_block)
	add	17, 15, 5
	cmpdi	17, 16
	bgt	Big_block
	GEN_MASK 18, 15, 5
	b	_Partial
SYM_FUNC_END(Do_partial_block)
Big_block:
	li	16, 16
	GEN_MASK 18, 15, 16

_Partial:
	lxvb16x	17+32, 0, 14		# load last block
	sldi	16, 15, 3
	mtvsrdd	32+16, 0, 16
	vsro	17, 17, 16
	xxlxor	47, 47, 17+32
	xxland	47, 47, 18

	vxor	0, 0, 0			# clear Xi
	vmr	28, 15

	cmpdi	21, 0			# encrypt/decrypt ops?
	beq	Skip_decrypt
	xxland	32+28, 32+17, 18

Skip_decrypt:

	ppc_update_hash_1x

	li	16, 16
	lxvb16x 32+29, 16, 8
	vxor	0, 0, 29
	stxvb16x 32, 0, 8		# save Xi
	stxvb16x 32, 16, 8		# save Xi

	# store partial block
	# loop the rest of the stream if any
	sldi	16, 15, 3
	mtvsrdd	32+16, 0, 16
	vslo	15, 15, 16
	#stxvb16x 15+32, 0, 9		# last block

	li	16, 16
	sub	17, 16, 15		# 16 - partial

	add	16, 15, 5
	cmpdi	16, 16
	bgt	Larger_16
	mr	17, 5
Larger_16:

	# write partial
	li		10, 192
	stxvb16x	15+32, 10, 1	# save current block

	addi		10, 9, -1
	addi		16, 1, 191
	mtctr		17		# move partial byte count

Write_last_partial:
        lbzu		18, 1(16)
	stbu		18, 1(10)
        bdnz		Write_last_partial
	# Complete loop partial

	add	14, 14, 17
	add	9, 9, 17
	sub	12, 12, 17
	add	11, 11, 17

	add	15, 15, 5
	cmpdi	15, 16
	blt	Save_partial

	vaddudm	30, 30, 31
	stxvb16x 30+32, 0, 7		# update IV
	xxlor	32+29, 0, 0
	vxor	15, 30, 29		# IV + round key - add round key 0
	li	15, 0
	std	15, 56(7)		# partial done - clear
	b	Partial_done
Save_partial:
	std	15, 56(7)		# partial

Partial_done:
	blr

 #
 # Write partial block
 # r9 - output
 # r12 - remaining bytes
 # v15 - partial input data
 #
SYM_FUNC_START_LOCAL(Write_partial_block)
	li		10, 192
	stxvb16x	15+32, 10, 1		# last block

	addi		10, 9, -1
	addi		16, 1, 191

        mtctr		12			# remaining bytes
	li		15, 0

Write_last_byte:
        lbzu		14, 1(16)
	stbu		14, 1(10)
        bdnz		Write_last_byte
	blr
SYM_FUNC_END(Write_partial_block)

aes_gcm_out:
	# out = state
	stxvb16x	32, 0, 8		# write out Xi
	add	3, 11, 12		# return count

	RESTORE_REGS
	blr

 #
 # 8x Decrypt
 #
_GLOBAL(aes_p10_gcm_decrypt)
.align 5

	SAVE_REGS

	LOAD_HASH_TABLE

	# initialize ICB: GHASH( IV ), IV - r7
	lxvb16x	30+32, 0, 7	# load IV  - v30

	mr	12, 5		# length
	li	11, 0		# block index

	# counter 1
	vxor	31, 31, 31
	vspltisb 22, 1
	vsldoi	31, 31, 22,1	# counter 1

	# load round key to VSR
	lxv	0, 0(6)
	lxv	1, 0x10(6)
	lxv	2, 0x20(6)
	lxv	3, 0x30(6)
	lxv	4, 0x40(6)
	lxv	5, 0x50(6)
	lxv	6, 0x60(6)
	lxv	7, 0x70(6)
	lxv	8, 0x80(6)
	lxv	9, 0x90(6)
	lxv	10, 0xa0(6)

	# load rounds - 10 (128), 12 (192), 14 (256)
	lwz	9,240(6)

	#
	# vxor	state, state, w # addroundkey
	xxlor	32+29, 0, 0
	vxor	15, 30, 29	# IV + round key - add round key 0

	cmpdi	9, 10
	beq	Loop_aes_gcm_8x_dec

	# load 2 more round keys (v11, v12)
	lxv	11, 0xb0(6)
	lxv	12, 0xc0(6)

	cmpdi	9, 12
	beq	Loop_aes_gcm_8x_dec

	# load 2 more round keys (v11, v12, v13, v14)
	lxv	13, 0xd0(6)
	lxv	14, 0xe0(6)
	cmpdi	9, 14
	beq	Loop_aes_gcm_8x_dec

	b	aes_gcm_out

.align 5
Loop_aes_gcm_8x_dec:
	mr	14, 3
	mr	9, 4

	#
	# check partial block
	#
Continue_partial_check_dec:
	ld	15, 56(7)
	cmpdi	15, 0
	beq	Continue_dec
	bgt	Final_block_dec
	cmpdi	15, 16
	blt	Final_block_dec

Continue_dec:
	# n blcoks
	li	10, 128
	divdu	10, 12, 10	# n 128 bytes-blocks
	cmpdi	10, 0
	beq	Loop_last_block_dec

	vaddudm	30, 30, 31	# IV + counter
	vxor	16, 30, 29
	vaddudm	30, 30, 31
	vxor	17, 30, 29
	vaddudm	30, 30, 31
	vxor	18, 30, 29
	vaddudm	30, 30, 31
	vxor	19, 30, 29
	vaddudm	30, 30, 31
	vxor	20, 30, 29
	vaddudm	30, 30, 31
	vxor	21, 30, 29
	vaddudm	30, 30, 31
	vxor	22, 30, 29

	mtctr	10

	li	15, 16
	li	16, 32
	li	17, 48
	li	18, 64
	li	19, 80
	li	20, 96
	li	21, 112

	lwz	10, 240(6)

Loop_8x_block_dec:

	lxvb16x		15, 0, 14	# load block
	lxvb16x		16, 15, 14	# load block
	lxvb16x		17, 16, 14	# load block
	lxvb16x		18, 17, 14	# load block
	lxvb16x		19, 18, 14	# load block
	lxvb16x		20, 19, 14	# load block
	lxvb16x		21, 20, 14	# load block
	lxvb16x		22, 21, 14	# load block
	addi		14, 14, 128

	Loop_aes_middle8x

	xxlor	23+32, 10, 10

	cmpdi	10, 10
	beq	Do_next_ghash_dec

	# 192 bits
	xxlor	24+32, 11, 11

	vcipher	15, 15, 23
	vcipher	16, 16, 23
	vcipher	17, 17, 23
	vcipher	18, 18, 23
	vcipher	19, 19, 23
	vcipher	20, 20, 23
	vcipher	21, 21, 23
	vcipher	22, 22, 23

	vcipher	15, 15, 24
	vcipher	16, 16, 24
	vcipher	17, 17, 24
	vcipher	18, 18, 24
	vcipher	19, 19, 24
	vcipher	20, 20, 24
	vcipher	21, 21, 24
	vcipher	22, 22, 24

	xxlor	23+32, 12, 12

	cmpdi	10, 12
	beq	Do_next_ghash_dec

	# 256 bits
	xxlor	24+32, 13, 13

	vcipher	15, 15, 23
	vcipher	16, 16, 23
	vcipher	17, 17, 23
	vcipher	18, 18, 23
	vcipher	19, 19, 23
	vcipher	20, 20, 23
	vcipher	21, 21, 23
	vcipher	22, 22, 23

	vcipher	15, 15, 24
	vcipher	16, 16, 24
	vcipher	17, 17, 24
	vcipher	18, 18, 24
	vcipher	19, 19, 24
	vcipher	20, 20, 24
	vcipher	21, 21, 24
	vcipher	22, 22, 24

	xxlor	23+32, 14, 14

	cmpdi	10, 14
	beq	Do_next_ghash_dec
	b	aes_gcm_out

Do_next_ghash_dec:

	#
	# last round
	vcipherlast     15, 15, 23
	vcipherlast     16, 16, 23

	xxlxor		47, 47, 15
	stxvb16x        47, 0, 9	# store output
	xxlxor		48, 48, 16
	stxvb16x        48, 15, 9	# store output

	vcipherlast     17, 17, 23
	vcipherlast     18, 18, 23

	xxlxor		49, 49, 17
	stxvb16x        49, 16, 9	# store output
	xxlxor		50, 50, 18
	stxvb16x        50, 17, 9	# store output

	vcipherlast     19, 19, 23
	vcipherlast     20, 20, 23

	xxlxor		51, 51, 19
	stxvb16x        51, 18, 9	# store output
	xxlxor		52, 52, 20
	stxvb16x        52, 19, 9	# store output

	vcipherlast     21, 21, 23
	vcipherlast     22, 22, 23

	xxlxor		53, 53, 21
	stxvb16x        53, 20, 9	# store output
	xxlxor		54, 54, 22
	stxvb16x        54, 21, 9	# store output

	addi		9, 9, 128

	xxlor           15+32, 15, 15
	xxlor           16+32, 16, 16
	xxlor           17+32, 17, 17
	xxlor           18+32, 18, 18
	xxlor           19+32, 19, 19
	xxlor           20+32, 20, 20
	xxlor           21+32, 21, 21
	xxlor           22+32, 22, 22

	# ghash here
	ppc_aes_gcm_ghash2_4x

	xxlor	27+32, 0, 0
	vaddudm 30, 30, 31		# IV + counter
	vmr	29, 30
	vxor    15, 30, 27		# add round key
	vaddudm 30, 30, 31
	vxor    16, 30, 27
	vaddudm 30, 30, 31
	vxor    17, 30, 27
	vaddudm 30, 30, 31
	vxor    18, 30, 27
	vaddudm 30, 30, 31
	vxor    19, 30, 27
	vaddudm 30, 30, 31
	vxor    20, 30, 27
	vaddudm 30, 30, 31
	vxor    21, 30, 27
	vaddudm 30, 30, 31
	vxor    22, 30, 27

	addi    12, 12, -128
	addi    11, 11, 128

	bdnz	Loop_8x_block_dec

	vmr	30, 29
	stxvb16x 30+32, 0, 7		# update IV

Loop_last_block_dec:
	cmpdi   12, 0
	beq     aes_gcm_out

	# loop last few blocks
	li      10, 16
	divdu   10, 12, 10

	mtctr   10

	lwz	10, 240(6)

	cmpdi   12, 16
	blt     Final_block_dec

Next_rem_block_dec:
	lxvb16x 15, 0, 14		# load block

	Loop_aes_middle_1x

	xxlor	23+32, 10, 10

	cmpdi	10, 10
	beq	Do_next_1x_dec

	# 192 bits
	xxlor	24+32, 11, 11

	vcipher	15, 15, 23
	vcipher	15, 15, 24

	xxlor	23+32, 12, 12

	cmpdi	10, 12
	beq	Do_next_1x_dec

	# 256 bits
	xxlor	24+32, 13, 13

	vcipher	15, 15, 23
	vcipher	15, 15, 24

	xxlor	23+32, 14, 14

	cmpdi	10, 14
	beq	Do_next_1x_dec

Do_next_1x_dec:
	vcipherlast     15, 15, 23

	xxlxor		47, 47, 15
	stxvb16x	47, 0, 9	# store output
	addi		14, 14, 16
	addi		9, 9, 16

	xxlor           28+32, 15, 15
	#vmr		28, 15
	ppc_update_hash_1x

	addi		12, 12, -16
	addi		11, 11, 16
	xxlor		19+32, 0, 0
	vaddudm		30, 30, 31		# IV + counter
	vxor		15, 30, 19		# add round key

	bdnz	Next_rem_block_dec

	li	15, 0
	std	15, 56(7)		# clear partial?
	stxvb16x 30+32, 0, 7		# update IV
	cmpdi	12, 0
	beq	aes_gcm_out

Final_block_dec:
	lwz	10, 240(6)
	Loop_aes_middle_1x

	xxlor	23+32, 10, 10

	cmpdi	10, 10
	beq	Do_final_1x_dec

	# 192 bits
	xxlor	24+32, 11, 11

	vcipher	15, 15, 23
	vcipher	15, 15, 24

	xxlor	23+32, 12, 12

	cmpdi	10, 12
	beq	Do_final_1x_dec

	# 256 bits
	xxlor	24+32, 13, 13

	vcipher	15, 15, 23
	vcipher	15, 15, 24

	xxlor	23+32, 14, 14

	cmpdi	10, 14
	beq	Do_final_1x_dec

Do_final_1x_dec:
	vcipherlast     15, 15, 23

	# check partial block
	li	21, 1			# decrypt
	ld	15, 56(7)		# partial?
	cmpdi	15, 0
	beq	Normal_block_dec
	bl	Do_partial_block
	cmpdi	12, 0
	ble aes_gcm_out

	b Continue_partial_check_dec

Normal_block_dec:
	lxvb16x	15, 0, 14		# load last block
	xxlxor	47, 47, 15

	# create partial block mask
	li	15, 16
	sub	15, 15, 12		# index to the mask

	vspltisb	16, -1		# first 16 bytes - 0xffff...ff
	vspltisb	17, 0		# second 16 bytes - 0x0000...00
	li	10, 192
	stvx	16, 10, 1
	addi	10, 10, 16
	stvx	17, 10, 1

	addi	10, 1, 192
	lxvb16x	16, 15, 10		# load partial block mask
	xxland	47, 47, 16

	xxland	32+28, 15, 16
	#vmr	28, 15
	ppc_update_hash_1x

	# * should store only the remaining bytes.
	bl	Write_partial_block

	stxvb16x 30+32, 0, 7		# update IV
	std	12, 56(7)		# update partial?
	li	16, 16

	stxvb16x	32, 0, 8		# write out Xi
	stxvb16x	32, 16, 8		# write out Xi
	b aes_gcm_out
