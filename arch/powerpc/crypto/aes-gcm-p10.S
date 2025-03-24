/* SPDX-License-Identifier: GPL-2.0-or-later */
#
# Accelerated AES-GCM stitched implementation for ppc64le.
#
# Copyright 2024- IBM Inc.
#
#===================================================================================
# Written by Danny Tsen <dtsen@us.ibm.com>
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
#     vs0 - round key 0
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

.macro	SAVE_GPR GPR OFFSET FRAME
	std	\GPR,\OFFSET(\FRAME)
.endm

.macro	SAVE_VRS VRS OFFSET FRAME
	stxv	\VRS+32, \OFFSET(\FRAME)
.endm

.macro	RESTORE_GPR GPR OFFSET FRAME
	ld	\GPR,\OFFSET(\FRAME)
.endm

.macro	RESTORE_VRS VRS OFFSET FRAME
	lxv	\VRS+32, \OFFSET(\FRAME)
.endm

.macro SAVE_REGS
	mflr 0
	std 0, 16(1)
	stdu 1,-512(1)

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

	addi    1, 1, 512
	ld 0, 16(1)
	mtlr 0
.endm # RESTORE_REGS

# 4x loops
.macro AES_CIPHER_4x _VCIPHER ST r
	\_VCIPHER	\ST, \ST, \r
	\_VCIPHER	\ST+1, \ST+1, \r
	\_VCIPHER	\ST+2, \ST+2, \r
	\_VCIPHER	\ST+3, \ST+3, \r
.endm

# 8x loops
.macro AES_CIPHER_8x _VCIPHER ST r
	\_VCIPHER	\ST, \ST, \r
	\_VCIPHER	\ST+1, \ST+1, \r
	\_VCIPHER	\ST+2, \ST+2, \r
	\_VCIPHER	\ST+3, \ST+3, \r
	\_VCIPHER	\ST+4, \ST+4, \r
	\_VCIPHER	\ST+5, \ST+5, \r
	\_VCIPHER	\ST+6, \ST+6, \r
	\_VCIPHER	\ST+7, \ST+7, \r
.endm

.macro LOOP_8AES_STATE
	xxlor	32+23, 1, 1
	xxlor	32+24, 2, 2
	xxlor	32+25, 3, 3
	xxlor	32+26, 4, 4
	AES_CIPHER_8x vcipher, 15, 23
	AES_CIPHER_8x vcipher, 15, 24
	AES_CIPHER_8x vcipher, 15, 25
	AES_CIPHER_8x vcipher, 15, 26
	xxlor	32+23, 5, 5
	xxlor	32+24, 6, 6
	xxlor	32+25, 7, 7
	xxlor	32+26, 8, 8
	AES_CIPHER_8x vcipher, 15, 23
	AES_CIPHER_8x vcipher, 15, 24
	AES_CIPHER_8x vcipher, 15, 25
	AES_CIPHER_8x vcipher, 15, 26
.endm

#
# PPC_GHASH4x(H, S1, S2, S3, S4): Compute 4x hash values based on Karatsuba method.
# H: returning digest
# S#: states
#
# S1 should xor with the previous digest
#
# Xi = v0
# H Poly = v2
# Hash keys = v3 - v14
# Scratch: v23 - v29
#
.macro PPC_GHASH4x H S1 S2 S3 S4

	vpmsumd	23, 12, \S1		# H4.L * X.L
	vpmsumd	24, 9, \S2
	vpmsumd	25, 6, \S3
	vpmsumd	26, 3, \S4

	vpmsumd	27, 13, \S1		# H4.L * X.H + H4.H * X.L
	vpmsumd	28, 10, \S2		# H3.L * X1.H + H3.H * X1.L

	vxor	23, 23, 24
	vxor	23, 23, 25
	vxor	23, 23, 26		# L

	vxor	24, 27, 28
	vpmsumd	25, 7, \S3
	vpmsumd	26, 4, \S4

	vxor	24, 24, 25
	vxor	24, 24, 26		# M

	# sum hash and reduction with H Poly
	vpmsumd	28, 23, 2		# reduction

	vxor	1, 1, 1
	vsldoi	25, 24, 1, 8		# mL
	vsldoi	1, 1, 24, 8		# mH
	vxor	23, 23, 25		# mL + L

	# This performs swap and xor like,
	#   vsldoi	23, 23, 23, 8		# swap
	#   vxor	23, 23, 28
	xxlor	32+25, 10, 10
	vpermxor 23, 23, 28, 25

	vpmsumd	26, 14, \S1		# H4.H * X.H
	vpmsumd	27, 11, \S2
	vpmsumd	28, 8, \S3
	vpmsumd	29, 5, \S4

	vxor	24, 26, 27
	vxor	24, 24, 28
	vxor	24, 24, 29

	vxor	24, 24, 1

	# sum hash and reduction with H Poly
	vsldoi	25, 23, 23, 8		# swap
	vpmsumd	23, 23, 2
	vxor	27, 25, 24
	vxor	\H, 23, 27
.endm

#
# Compute update single ghash
# scratch: v1, v22..v27
#
.macro PPC_GHASH1x H S1

	vxor	1, 1, 1

	vpmsumd	22, 3, \S1		# L
	vpmsumd	23, 4, \S1		# M
	vpmsumd	24, 5, \S1		# H

	vpmsumd	27, 22, 2		# reduction

	vsldoi	25, 23, 1, 8		# mL
	vsldoi	26, 1, 23, 8		# mH
	vxor	22, 22, 25		# LL + LL
	vxor	24, 24, 26		# HH + HH

	xxlor	32+25, 10, 10
	vpermxor 22, 22, 27, 25

	vsldoi	23, 22, 22, 8		# swap
	vpmsumd	22, 22, 2		# reduction
	vxor	23, 23, 24
	vxor	\H, 22, 23
.endm

#
# LOAD_HASH_TABLE
# Xi = v0
# H Poly = v2
# Hash keys = v3 - v14
#
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

################################################################################
# Compute AES and ghash one block at a time.
# r23: AES rounds
# v30: current IV
# vs0: roundkey 0
#
################################################################################
SYM_FUNC_START_LOCAL(aes_gcm_crypt_1x)

	cmpdi	5, 16
	bge	__More_1x
	blr
__More_1x:
	li      10, 16
	divdu   12, 5, 10

	xxlxor	32+15, 32+30, 0

	# Pre-load 8 AES rounds to scratch vectors.
	xxlor	32+16, 1, 1
	xxlor	32+17, 2, 2
	xxlor	32+18, 3, 3
	xxlor	32+19, 4, 4
	xxlor	32+20, 5, 5
	xxlor	32+21, 6, 6
	xxlor	32+28, 7, 7
	xxlor	32+29, 8, 8
	lwz	23, 240(6)	# n rounds
	addi	22, 23, -9	# remaing AES rounds

	cmpdi	12, 0
	bgt	__Loop_1x
	blr

__Loop_1x:
	mtctr	22
	addi	10, 6, 144
	vcipher	15, 15, 16
	vcipher	15, 15, 17
	vcipher	15, 15, 18
	vcipher	15, 15, 19
	vcipher	15, 15, 20
	vcipher	15, 15, 21
	vcipher	15, 15, 28
	vcipher	15, 15, 29

__Loop_aes_1state:
	lxv	32+1, 0(10)
	vcipher	15, 15, 1
	addi	10, 10, 16
	bdnz	__Loop_aes_1state
	lxv	32+1, 0(10)		# last round key
	lxvb16x 11, 0, 14		# load input block
	vcipherlast 15, 15, 1

	xxlxor	32+15, 32+15, 11
	stxvb16x 32+15, 0, 9	# store output
	addi	14, 14, 16
	addi	9, 9, 16

	cmpdi	24, 0	# decrypt?
	bne	__Encrypt_1x
	xxlor	15+32, 11, 11
__Encrypt_1x:
	vxor	15, 15, 0
	PPC_GHASH1x 0, 15

	addi	5, 5, -16
	addi	11, 11, 16

	vadduwm 30, 30, 31		# IV + counter
	xxlxor	32+15, 32+30, 0
	addi	12, 12, -1
	cmpdi	12, 0
	bgt	__Loop_1x

	stxvb16x 32+30, 0, 7		# update IV
	stxvb16x 32+0, 0, 8		# update Xi
	blr
SYM_FUNC_END(aes_gcm_crypt_1x)

################################################################################
# Process a normal partial block when we come here.
#  Compute partial mask, Load and store partial block to stack.
#  Update partial_len and pblock.
#  pblock is (encrypted ^ AES state) for encrypt
#        and (input ^ AES state) for decrypt.
#
################################################################################
SYM_FUNC_START_LOCAL(__Process_partial)

	# create partial mask
	vspltisb 16, -1
	li	12, 16
	sub	12, 12, 5
	sldi	12, 12, 3
	mtvsrdd	32+17, 0, 12
	vslo	16, 16, 17		# partial block mask

	lxvb16x 11, 0, 14		# load partial block
	xxland	11, 11, 32+16

	# AES crypt partial
	xxlxor	32+15, 32+30, 0
	lwz	23, 240(6)		# n rounds
	addi	22, 23, -1		# loop - 1
	mtctr	22
	addi	10, 6, 16

__Loop_aes_pstate:
	lxv	32+1, 0(10)
	vcipher	15, 15, 1
	addi	10, 10, 16
	bdnz	__Loop_aes_pstate
	lxv	32+1, 0(10)		# last round key
	vcipherlast 15, 15, 1

	xxlxor	32+15, 32+15, 11
	vand	15, 15, 16

	# AES crypt output v15
	# Write partial
	li	10, 224
	stxvb16x 15+32, 10, 1		# write v15 to stack
	addi	10, 1, 223
	addi	12, 9, -1
        mtctr	5			# partial block len
__Write_partial:
        lbzu	22, 1(10)
	stbu	22, 1(12)
        bdnz	__Write_partial

	cmpdi	24, 0			# decrypt?
	bne	__Encrypt_partial
	xxlor	32+15, 11, 11		# decrypt using the input block
__Encrypt_partial:
	#vxor	15, 15, 0		# ^ previous hash
	#PPC_GHASH1x 0, 15

	add	14, 14, 5
	add	9, 9, 5
	std	5, 56(7)		# update partial
	sub	11, 11, 5
	li	5, 0			# done last byte

	#
	# Don't increase IV since this is the last partial.
	# It should get updated in gcm_update if no more data blocks.
	#vadduwm	30, 30, 31		# increase IV
	stxvb16x 32+30, 0, 7		# update IV
	li	10, 64
	stxvb16x 32+0, 0, 8		# Update X1
	stxvb16x 32+15, 10, 7		# Update pblock
	blr
SYM_FUNC_END(__Process_partial)

################################################################################
# Combine partial blocks and ghash when we come here.
#
# The partial block has to be shifted to the right location to encrypt/decrypt
# and compute ghash if combing the previous partial block is needed.
# - Compute ghash for a full block. Clear Partial_len and pblock. Update IV.
#   Write Xi.
# - Don't compute ghash if not full block.  gcm_update will take care of it
#   is the last block. Update Partial_len and pblock.
#
################################################################################
SYM_FUNC_START_LOCAL(__Combine_partial)

	ld	12, 56(7)
	mr	21, 5			# these bytes to be processed

	li	17, 0
	li	16, 16
	sub	22, 16, 12		# bytes to complete a block
	sub	17, 22, 5		# remaining bytes in a block
	cmpdi	5, 16
	ble	__Inp_msg_less16
	li	17, 0
	mr	21, 22
	b	__Combine_continue
__Inp_msg_less16:
	cmpd	22, 5
	bgt	__Combine_continue
	li	17, 0
	mr	21, 22			# these bytes to be processed

__Combine_continue:
	# load msg and shift to the proper location and mask
	vspltisb 16, -1
	sldi	15, 12, 3
	mtvsrdd	32+17, 0, 15
	vslo	16, 16, 17
	vsro	16, 16, 17
	sldi	15, 17, 3
	mtvsrdd	32+17, 0, 15
	vsro	16, 16, 17
	vslo	16, 16, 17		# mask

	lxvb16x 32+19, 0, 14		# load partial block
	sldi	15, 12, 3
	mtvsrdd	32+17, 0, 15
	vsro	19, 19, 17		# 0x00..xxxx??..??
	sldi	15, 17, 3
	mtvsrdd	32+17, 0, 15
	vsro	19, 19, 17		# 0x00..xxxx
	vslo	19, 19, 17		# shift back to form 0x00..xxxx00..00

	# AES crypt partial
	xxlxor	32+15, 32+30, 0
	lwz	23, 240(6)	# n rounds
	addi	22, 23, -1	# loop - 1
	mtctr	22
	addi	10, 6, 16

__Loop_aes_cpstate:
	lxv	32+1, 0(10)
	vcipher	15, 15, 1
	addi	10, 10, 16
	bdnz	__Loop_aes_cpstate
	lxv	32+1, 0(10)		# last round key
	vcipherlast 15, 15, 1

	vxor	15, 15, 19
	vand	15, 15, 16

	# AES crypt output v15
	# Write partial
	li	10, 224
	stxvb16x 15+32, 10, 1		# write v15 to stack
	addi	10, 1, 223
	add	10, 10, 12		# add offset
	addi	15, 9, -1
        mtctr	21			# partial block len
__Write_combine_partial:
        lbzu	22, 1(10)
	stbu	22, 1(15)
        bdnz	__Write_combine_partial

	add	14, 14, 21
	add	11, 11, 21
	add	9, 9, 21
	sub	5, 5, 21

	# Encrypt/Decrypt?
	cmpdi	24, 0			# decrypt?
	bne	__Encrypt_combine_partial
	vmr	15, 19		# decrypt using the input block

__Encrypt_combine_partial:
	#
	# Update partial flag and combine ghash.
__Update_partial_ghash:
	li	10, 64
	lxvb16x 32+17, 10, 7		# load previous pblock
	add	12, 12, 21		# combined pprocessed
	vxor	15, 15, 17		# combined pblock

	cmpdi	12, 16
	beq	__Clear_partial_flag
	std	12, 56(7)		# update partial len
	stxvb16x 32+15, 10, 7		# Update current pblock
	blr

__Clear_partial_flag:
	li	12, 0
	std	12, 56(7)
	# Update IV and ghash here
	vadduwm	30, 30, 31		# increase IV
	stxvb16x 32+30, 0, 7		# update IV

	# v15 either is either (input blockor encrypted)^(AES state)
	vxor	15, 15, 0
	PPC_GHASH1x 0, 15
	stxvb16x 32+0, 10, 7		# update pblock for debug?
	stxvb16x 32+0, 0, 8		# update Xi
	blr
SYM_FUNC_END(__Combine_partial)

################################################################################
# gcm_update(iv, Xi) - compute last hash
#
################################################################################
SYM_FUNC_START(gcm_update)

	ld	10, 56(3)
	cmpdi	10, 0
	beq	__no_update

	lxvb16x	32, 0, 4	# load Xi
	# load Hash - h^4, h^3, h^2, h
	li	10, 32
	lxvd2x	2+32, 10, 4	# H Poli
	li	10, 48
	lxvd2x	3+32, 10, 4	# Hl
	li	10, 64
	lxvd2x	4+32, 10, 4	# H
	li	10, 80
	lxvd2x	5+32, 10, 4	# Hh

	addis	11, 2, permx@toc@ha
	addi	11, 11, permx@toc@l
	lxv	10, 0(11)	# vs10: vpermxor vector

	li	9, 64
	lxvb16x 32+6, 9, 3		# load pblock
	vxor	6, 6, 0

	vxor	1, 1, 1
	vpmsumd	12, 3, 6		# L
	vpmsumd	13, 4, 6		# M
	vpmsumd	14, 5, 6		# H
	vpmsumd	17, 12, 2		# reduction
	vsldoi	15, 13, 1, 8		# mL
	vsldoi	16, 1, 13, 8		# mH
	vxor	12, 12, 15		# LL + LL
	vxor	14, 14, 16		# HH + HH
	xxlor	32+15, 10, 10
	vpermxor 12, 12, 17, 15
	vsldoi	13, 12, 12, 8		# swap
	vpmsumd	12, 12, 2		# reduction
	vxor	13, 13, 14
	vxor	7, 12, 13

	#vxor	0, 0, 0
	#stxvb16x 32+0, 9, 3
	li	10, 0
	std	10, 56(3)
	stxvb16x 32+7, 0, 4

__no_update:
	blr
SYM_FUNC_END(gcm_update)

################################################################################
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
################################################################################
SYM_FUNC_START(aes_p10_gcm_encrypt)

	cmpdi	5, 0
	ble	__Invalid_msg_len

	SAVE_REGS
	LOAD_HASH_TABLE

	# initialize ICB: GHASH( IV ), IV - r7
	lxvb16x	30+32, 0, 7	# load IV  - v30

	mr	14, 3
	mr	9, 4

	# counter 1
	vxor	31, 31, 31
	vspltisb 22, 1
	vsldoi	31, 31, 22,1	# counter 1

	addis	11, 2, permx@toc@ha
	addi	11, 11, permx@toc@l
	lxv	10, 0(11)	# vs10: vpermxor vector
	li	11, 0

	# load 9 round keys to VSR
	lxv	0, 0(6)			# round key 0
	lxv	1, 16(6)		# round key 1
	lxv	2, 32(6)		# round key 2
	lxv	3, 48(6)		# round key 3
	lxv	4, 64(6)		# round key 4
	lxv	5, 80(6)		# round key 5
	lxv	6, 96(6)		# round key 6
	lxv	7, 112(6)		# round key 7
	lxv	8, 128(6)		# round key 8

	# load rounds - 10 (128), 12 (192), 14 (256)
	lwz	23, 240(6)		# n rounds
	li	24, 1			# encrypt

__Process_encrypt:
	#
	# Process different blocks
	#
	ld	12, 56(7)
	cmpdi	12, 0
	bgt	__Do_combine_enc
	cmpdi	5, 128
	blt	__Process_more_enc

#
# Process 8x AES/GCM blocks
#
__Process_8x_enc:
	# 8x blcoks
	li	10, 128
	divdu	12, 5, 10	# n 128 bytes-blocks

	addi	12, 12, -1	# loop - 1

	vmr	15, 30		# first state: IV
	vadduwm	16, 15, 31	# state + counter
	vadduwm	17, 16, 31
	vadduwm	18, 17, 31
	vadduwm	19, 18, 31
	vadduwm	20, 19, 31
	vadduwm	21, 20, 31
	vadduwm	22, 21, 31
	xxlor	9, 32+22, 32+22	# save last state

	# vxor  state, state, w # addroundkey
	xxlor	32+29, 0, 0
        vxor    15, 15, 29      # IV + round key - add round key 0
	vxor	16, 16, 29
	vxor	17, 17, 29
	vxor	18, 18, 29
	vxor	19, 19, 29
	vxor	20, 20, 29
	vxor	21, 21, 29
	vxor	22, 22, 29

	li	15, 16
	li	16, 32
	li	17, 48
	li	18, 64
	li	19, 80
	li	20, 96
	li	21, 112

	#
	# Pre-compute first 8 AES state and leave 1/3/5 more rounds
	# for the loop.
	#
	addi	22, 23, -9		# process 8 keys
	mtctr	22			# AES key loop
	addi	10, 6, 144

	LOOP_8AES_STATE			# process 8 AES keys

__PreLoop_aes_state:
	lxv	32+1, 0(10)		# round key
	AES_CIPHER_8x vcipher 15 1
	addi	10, 10, 16
	bdnz	__PreLoop_aes_state
	lxv	32+1, 0(10)		# last round key (v1)

	cmpdi	12, 0			# Only one loop (8 block)
	beq	__Finish_ghash

#
# Loop 8x blocks and compute ghash
#
__Loop_8x_block_enc:
	vcipherlast     15, 15, 1
	vcipherlast     16, 16, 1
	vcipherlast     17, 17, 1
	vcipherlast     18, 18, 1
	vcipherlast     19, 19, 1
	vcipherlast     20, 20, 1
	vcipherlast     21, 21, 1
	vcipherlast     22, 22, 1

	lxvb16x	32+23, 0, 14	# load block
	lxvb16x	32+24, 15, 14	# load block
	lxvb16x	32+25, 16, 14	# load block
	lxvb16x	32+26, 17, 14	# load block
	lxvb16x	32+27, 18, 14	# load block
	lxvb16x	32+28, 19, 14	# load block
	lxvb16x	32+29, 20, 14	# load block
	lxvb16x	32+30, 21, 14	# load block
	addi	14, 14, 128

	vxor	15, 15, 23
	vxor	16, 16, 24
	vxor	17, 17, 25
	vxor	18, 18, 26
	vxor	19, 19, 27
	vxor	20, 20, 28
	vxor	21, 21, 29
	vxor	22, 22, 30

	stxvb16x 47, 0, 9	# store output
	stxvb16x 48, 15, 9	# store output
	stxvb16x 49, 16, 9	# store output
	stxvb16x 50, 17, 9	# store output
	stxvb16x 51, 18, 9	# store output
	stxvb16x 52, 19, 9	# store output
	stxvb16x 53, 20, 9	# store output
	stxvb16x 54, 21, 9	# store output
	addi	9, 9, 128

	# ghash here
	vxor	15, 15, 0
	PPC_GHASH4x 0, 15, 16, 17, 18

	vxor	19, 19, 0
	PPC_GHASH4x 0, 19, 20, 21, 22

	xxlor	32+15, 9, 9		# last state
	vadduwm 15, 15, 31		# state + counter
	vadduwm 16, 15, 31
	vadduwm 17, 16, 31
	vadduwm 18, 17, 31
	vadduwm 19, 18, 31
	vadduwm 20, 19, 31
	vadduwm 21, 20, 31
	vadduwm 22, 21, 31
	xxlor	9, 32+22, 32+22		# save last state

	xxlor	32+27, 0, 0		# restore roundkey 0
        vxor    15, 15, 27		# IV + round key - add round key 0
	vxor	16, 16, 27
	vxor	17, 17, 27
	vxor	18, 18, 27
	vxor	19, 19, 27
	vxor	20, 20, 27
	vxor	21, 21, 27
	vxor	22, 22, 27

	addi    5, 5, -128
	addi    11, 11, 128

	LOOP_8AES_STATE			# process 8 AES keys
	mtctr	22			# AES key loop
	addi	10, 6, 144
__LastLoop_aes_state:
	lxv	32+1, 0(10)		# round key
	AES_CIPHER_8x vcipher 15 1
	addi	10, 10, 16
	bdnz	__LastLoop_aes_state
	lxv	32+1, 0(10)		# last round key (v1)

	addi	12, 12, -1
	cmpdi	12, 0
	bne	__Loop_8x_block_enc

__Finish_ghash:
	vcipherlast     15, 15, 1
	vcipherlast     16, 16, 1
	vcipherlast     17, 17, 1
	vcipherlast     18, 18, 1
	vcipherlast     19, 19, 1
	vcipherlast     20, 20, 1
	vcipherlast     21, 21, 1
	vcipherlast     22, 22, 1

	lxvb16x	32+23, 0, 14	# load block
	lxvb16x	32+24, 15, 14	# load block
	lxvb16x	32+25, 16, 14	# load block
	lxvb16x	32+26, 17, 14	# load block
	lxvb16x	32+27, 18, 14	# load block
	lxvb16x	32+28, 19, 14	# load block
	lxvb16x	32+29, 20, 14	# load block
	lxvb16x	32+30, 21, 14	# load block
	addi	14, 14, 128

	vxor	15, 15, 23
	vxor	16, 16, 24
	vxor	17, 17, 25
	vxor	18, 18, 26
	vxor	19, 19, 27
	vxor	20, 20, 28
	vxor	21, 21, 29
	vxor	22, 22, 30

	stxvb16x 47, 0, 9	# store output
	stxvb16x 48, 15, 9	# store output
	stxvb16x 49, 16, 9	# store output
	stxvb16x 50, 17, 9	# store output
	stxvb16x 51, 18, 9	# store output
	stxvb16x 52, 19, 9	# store output
	stxvb16x 53, 20, 9	# store output
	stxvb16x 54, 21, 9	# store output
	addi	9, 9, 128

	vxor	15, 15, 0
	PPC_GHASH4x 0, 15, 16, 17, 18

	vxor	19, 19, 0
	PPC_GHASH4x 0, 19, 20, 21, 22

	xxlor	30+32, 9, 9		# last ctr
	vadduwm	30, 30, 31		# increase ctr
	stxvb16x 32+30, 0, 7		# update IV
	stxvb16x 32+0, 0, 8		# update Xi

	addi    5, 5, -128
	addi    11, 11, 128

	#
	# Done 8x blocks
	#

	cmpdi   5, 0
	beq     aes_gcm_out

__Process_more_enc:
	li	24, 1			# encrypt
	bl	aes_gcm_crypt_1x
	cmpdi   5, 0
	beq     aes_gcm_out

	bl	__Process_partial
	cmpdi   5, 0
	beq     aes_gcm_out
__Do_combine_enc:
	bl	__Combine_partial
	cmpdi	5, 0
	bgt	__Process_encrypt
	b	aes_gcm_out

SYM_FUNC_END(aes_p10_gcm_encrypt)

################################################################################
# aes_p10_gcm_decrypt (const void *inp, void *out, size_t len,
#               const char *rk, unsigned char iv[16], void *Xip);
# 8x Decrypt
#
################################################################################
SYM_FUNC_START(aes_p10_gcm_decrypt)

	cmpdi	5, 0
	ble	__Invalid_msg_len

	SAVE_REGS
	LOAD_HASH_TABLE

	# initialize ICB: GHASH( IV ), IV - r7
	lxvb16x	30+32, 0, 7	# load IV  - v30

	mr	14, 3
	mr	9, 4

	# counter 1
	vxor	31, 31, 31
	vspltisb 22, 1
	vsldoi	31, 31, 22,1	# counter 1

	addis	11, 2, permx@toc@ha
	addi	11, 11, permx@toc@l
	lxv	10, 0(11)	# vs10: vpermxor vector
	li	11, 0

	# load 9 round keys to VSR
	lxv	0, 0(6)			# round key 0
	lxv	1, 16(6)		# round key 1
	lxv	2, 32(6)		# round key 2
	lxv	3, 48(6)		# round key 3
	lxv	4, 64(6)		# round key 4
	lxv	5, 80(6)		# round key 5
	lxv	6, 96(6)		# round key 6
	lxv	7, 112(6)		# round key 7
	lxv	8, 128(6)		# round key 8

	# load rounds - 10 (128), 12 (192), 14 (256)
	lwz	23, 240(6)		# n rounds
	li	24, 0			# decrypt

__Process_decrypt:
	#
	# Process different blocks
	#
	ld	12, 56(7)
	cmpdi	12, 0
	bgt	__Do_combine_dec
	cmpdi	5, 128
	blt	__Process_more_dec

#
# Process 8x AES/GCM blocks
#
__Process_8x_dec:
	# 8x blcoks
	li	10, 128
	divdu	12, 5, 10	# n 128 bytes-blocks

	addi	12, 12, -1	# loop - 1

	vmr	15, 30		# first state: IV
	vadduwm	16, 15, 31	# state + counter
	vadduwm	17, 16, 31
	vadduwm	18, 17, 31
	vadduwm	19, 18, 31
	vadduwm	20, 19, 31
	vadduwm	21, 20, 31
	vadduwm	22, 21, 31
	xxlor	9, 32+22, 32+22	# save last state

	# vxor  state, state, w # addroundkey
	xxlor	32+29, 0, 0
        vxor    15, 15, 29      # IV + round key - add round key 0
	vxor	16, 16, 29
	vxor	17, 17, 29
	vxor	18, 18, 29
	vxor	19, 19, 29
	vxor	20, 20, 29
	vxor	21, 21, 29
	vxor	22, 22, 29

	li	15, 16
	li	16, 32
	li	17, 48
	li	18, 64
	li	19, 80
	li	20, 96
	li	21, 112

	#
	# Pre-compute first 8 AES state and leave 1/3/5 more rounds
	# for the loop.
	#
	addi	22, 23, -9		# process 8 keys
	mtctr	22			# AES key loop
	addi	10, 6, 144

	LOOP_8AES_STATE			# process 8 AES keys

__PreLoop_aes_state_dec:
	lxv	32+1, 0(10)		# round key
	AES_CIPHER_8x vcipher 15 1
	addi	10, 10, 16
	bdnz	__PreLoop_aes_state_dec
	lxv	32+1, 0(10)		# last round key (v1)

	cmpdi	12, 0			# Only one loop (8 block)
	beq	__Finish_ghash_dec

#
# Loop 8x blocks and compute ghash
#
__Loop_8x_block_dec:
	vcipherlast     15, 15, 1
	vcipherlast     16, 16, 1
	vcipherlast     17, 17, 1
	vcipherlast     18, 18, 1
	vcipherlast     19, 19, 1
	vcipherlast     20, 20, 1
	vcipherlast     21, 21, 1
	vcipherlast     22, 22, 1

	lxvb16x	32+23, 0, 14	# load block
	lxvb16x	32+24, 15, 14	# load block
	lxvb16x	32+25, 16, 14	# load block
	lxvb16x	32+26, 17, 14	# load block
	lxvb16x	32+27, 18, 14	# load block
	lxvb16x	32+28, 19, 14	# load block
	lxvb16x	32+29, 20, 14	# load block
	lxvb16x	32+30, 21, 14	# load block
	addi	14, 14, 128

	vxor	15, 15, 23
	vxor	16, 16, 24
	vxor	17, 17, 25
	vxor	18, 18, 26
	vxor	19, 19, 27
	vxor	20, 20, 28
	vxor	21, 21, 29
	vxor	22, 22, 30

	stxvb16x 47, 0, 9	# store output
	stxvb16x 48, 15, 9	# store output
	stxvb16x 49, 16, 9	# store output
	stxvb16x 50, 17, 9	# store output
	stxvb16x 51, 18, 9	# store output
	stxvb16x 52, 19, 9	# store output
	stxvb16x 53, 20, 9	# store output
	stxvb16x 54, 21, 9	# store output

	addi	9, 9, 128

	vmr	15, 23
	vmr	16, 24
	vmr	17, 25
	vmr	18, 26
	vmr	19, 27
	vmr	20, 28
	vmr	21, 29
	vmr	22, 30

	# ghash here
	vxor	15, 15, 0
	PPC_GHASH4x 0, 15, 16, 17, 18

	vxor	19, 19, 0
	PPC_GHASH4x 0, 19, 20, 21, 22

	xxlor	32+15, 9, 9		# last state
	vadduwm 15, 15, 31		# state + counter
	vadduwm 16, 15, 31
	vadduwm 17, 16, 31
	vadduwm 18, 17, 31
	vadduwm 19, 18, 31
	vadduwm 20, 19, 31
	vadduwm 21, 20, 31
	vadduwm 22, 21, 31
	xxlor	9, 32+22, 32+22		# save last state

	xxlor	32+27, 0, 0		# restore roundkey 0
        vxor    15, 15, 27		# IV + round key - add round key 0
	vxor	16, 16, 27
	vxor	17, 17, 27
	vxor	18, 18, 27
	vxor	19, 19, 27
	vxor	20, 20, 27
	vxor	21, 21, 27
	vxor	22, 22, 27

	addi    5, 5, -128
	addi    11, 11, 128

	LOOP_8AES_STATE			# process 8 AES keys
	mtctr	22			# AES key loop
	addi	10, 6, 144
__LastLoop_aes_state_dec:
	lxv	32+1, 0(10)		# round key
	AES_CIPHER_8x vcipher 15 1
	addi	10, 10, 16
	bdnz	__LastLoop_aes_state_dec
	lxv	32+1, 0(10)		# last round key (v1)

	addi	12, 12, -1
	cmpdi	12, 0
	bne	__Loop_8x_block_dec

__Finish_ghash_dec:
	vcipherlast     15, 15, 1
	vcipherlast     16, 16, 1
	vcipherlast     17, 17, 1
	vcipherlast     18, 18, 1
	vcipherlast     19, 19, 1
	vcipherlast     20, 20, 1
	vcipherlast     21, 21, 1
	vcipherlast     22, 22, 1

	lxvb16x	32+23, 0, 14	# load block
	lxvb16x	32+24, 15, 14	# load block
	lxvb16x	32+25, 16, 14	# load block
	lxvb16x	32+26, 17, 14	# load block
	lxvb16x	32+27, 18, 14	# load block
	lxvb16x	32+28, 19, 14	# load block
	lxvb16x	32+29, 20, 14	# load block
	lxvb16x	32+30, 21, 14	# load block
	addi	14, 14, 128

	vxor	15, 15, 23
	vxor	16, 16, 24
	vxor	17, 17, 25
	vxor	18, 18, 26
	vxor	19, 19, 27
	vxor	20, 20, 28
	vxor	21, 21, 29
	vxor	22, 22, 30

	stxvb16x 47, 0, 9	# store output
	stxvb16x 48, 15, 9	# store output
	stxvb16x 49, 16, 9	# store output
	stxvb16x 50, 17, 9	# store output
	stxvb16x 51, 18, 9	# store output
	stxvb16x 52, 19, 9	# store output
	stxvb16x 53, 20, 9	# store output
	stxvb16x 54, 21, 9	# store output
	addi	9, 9, 128

	#vmr	15, 23
	vxor	15, 23, 0
	vmr	16, 24
	vmr	17, 25
	vmr	18, 26
	vmr	19, 27
	vmr	20, 28
	vmr	21, 29
	vmr	22, 30

	#vxor	15, 15, 0
	PPC_GHASH4x 0, 15, 16, 17, 18

	vxor	19, 19, 0
	PPC_GHASH4x 0, 19, 20, 21, 22

	xxlor	30+32, 9, 9		# last ctr
	vadduwm	30, 30, 31		# increase ctr
	stxvb16x 32+30, 0, 7		# update IV
	stxvb16x 32+0, 0, 8		# update Xi

	addi    5, 5, -128
	addi    11, 11, 128

	#
	# Done 8x blocks
	#

	cmpdi   5, 0
	beq     aes_gcm_out

__Process_more_dec:
	li	24, 0			# decrypt
	bl	aes_gcm_crypt_1x
	cmpdi   5, 0
	beq     aes_gcm_out

	bl	__Process_partial
	cmpdi   5, 0
	beq     aes_gcm_out
__Do_combine_dec:
	bl	__Combine_partial
	cmpdi	5, 0
	bgt	__Process_decrypt
	b	aes_gcm_out
SYM_FUNC_END(aes_p10_gcm_decrypt)

SYM_FUNC_START_LOCAL(aes_gcm_out)

	mr	3, 11			# return count

	RESTORE_REGS
	blr

__Invalid_msg_len:
	li	3, 0
	blr
SYM_FUNC_END(aes_gcm_out)

SYM_DATA_START_LOCAL(PERMX)
.align 4
# for vector permute and xor
permx:
.long 0x4c5d6e7f, 0x08192a3b, 0xc4d5e6f7, 0x8091a2b3
SYM_DATA_END(permx)
