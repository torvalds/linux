/*
 * Implement fast CRC32C with PCLMULQDQ instructions. (x86_64)
 *
 * The white papers on CRC32C calculations with PCLMULQDQ instruction can be
 * downloaded from:
 * http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/crc-iscsi-polynomial-crc32-instruction-paper.pdf
 * http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/fast-crc-computation-paper.pdf
 *
 * Copyright (C) 2012 Intel Corporation.
 * Copyright 2024 Google LLC
 *
 * Authors:
 *	Wajdi Feghali <wajdi.k.feghali@intel.com>
 *	James Guilford <james.guilford@intel.com>
 *	David Cote <david.m.cote@intel.com>
 *	Tim Chen <tim.c.chen@linux.intel.com>
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/linkage.h>

## ISCSI CRC 32 Implementation with crc32 and pclmulqdq Instruction

# Define threshold below which buffers are considered "small" and routed to
# regular CRC code that does not interleave the CRC instructions.
#define SMALL_SIZE 200

# unsigned int crc_pcl(const u8 *buffer, unsigned int len, unsigned int crc_init);

.text
SYM_FUNC_START(crc_pcl)
#define    bufp		  %rdi
#define    bufp_d	  %edi
#define    len		  %esi
#define    crc_init	  %edx
#define    crc_init_q	  %rdx
#define    n_misaligned	  %ecx /* overlaps chunk_bytes! */
#define    n_misaligned_q %rcx
#define    chunk_bytes	  %ecx /* overlaps n_misaligned! */
#define    chunk_bytes_q  %rcx
#define    crc1		  %r8
#define    crc2		  %r9

	cmp	$SMALL_SIZE, len
	jb	.Lsmall

	################################################################
	## 1) ALIGN:
	################################################################
	mov	bufp_d, n_misaligned
	neg	n_misaligned
	and	$7, n_misaligned	# calculate the misalignment amount of
					# the address
	je	.Laligned		# Skip if aligned

	# Process 1 <= n_misaligned <= 7 bytes individually in order to align
	# the remaining data to an 8-byte boundary.
.Ldo_align:
	movq	(bufp), %rax
	add	n_misaligned_q, bufp
	sub	n_misaligned, len
.Lalign_loop:
	crc32b	%al, crc_init		# compute crc32 of 1-byte
	shr	$8, %rax		# get next byte
	dec	n_misaligned
	jne     .Lalign_loop
.Laligned:

	################################################################
	## 2) PROCESS BLOCK:
	################################################################

	cmp	$128*24, len
	jae     .Lfull_block

.Lpartial_block:
	# Compute floor(len / 24) to get num qwords to process from each lane.
	imul	$2731, len, %eax	# 2731 = ceil(2^16 / 24)
	shr	$16, %eax
	jmp	.Lcrc_3lanes

.Lfull_block:
	# Processing 128 qwords from each lane.
	mov	$128, %eax

	################################################################
	## 3) CRC each of three lanes:
	################################################################

.Lcrc_3lanes:
	xor	crc1,crc1
	xor     crc2,crc2
	mov	%eax, chunk_bytes
	shl	$3, chunk_bytes		# num bytes to process from each lane
	sub	$5, %eax		# 4 for 4x_loop, 1 for special last iter
	jl	.Lcrc_3lanes_4x_done

	# Unroll the loop by a factor of 4 to reduce the overhead of the loop
	# bookkeeping instructions, which can compete with crc32q for the ALUs.
.Lcrc_3lanes_4x_loop:
	crc32q	(bufp), crc_init_q
	crc32q	(bufp,chunk_bytes_q), crc1
	crc32q	(bufp,chunk_bytes_q,2), crc2
	crc32q	8(bufp), crc_init_q
	crc32q	8(bufp,chunk_bytes_q), crc1
	crc32q	8(bufp,chunk_bytes_q,2), crc2
	crc32q	16(bufp), crc_init_q
	crc32q	16(bufp,chunk_bytes_q), crc1
	crc32q	16(bufp,chunk_bytes_q,2), crc2
	crc32q	24(bufp), crc_init_q
	crc32q	24(bufp,chunk_bytes_q), crc1
	crc32q	24(bufp,chunk_bytes_q,2), crc2
	add	$32, bufp
	sub	$4, %eax
	jge	.Lcrc_3lanes_4x_loop

.Lcrc_3lanes_4x_done:
	add	$4, %eax
	jz	.Lcrc_3lanes_last_qword

.Lcrc_3lanes_1x_loop:
	crc32q	(bufp), crc_init_q
	crc32q	(bufp,chunk_bytes_q), crc1
	crc32q	(bufp,chunk_bytes_q,2), crc2
	add	$8, bufp
	dec	%eax
	jnz	.Lcrc_3lanes_1x_loop

.Lcrc_3lanes_last_qword:
	crc32q	(bufp), crc_init_q
	crc32q	(bufp,chunk_bytes_q), crc1
# SKIP  crc32q	(bufp,chunk_bytes_q,2), crc2	; Don't do this one yet

	################################################################
	## 4) Combine three results:
	################################################################

	lea	(K_table-8)(%rip), %rax		# first entry is for idx 1
	pmovzxdq (%rax,chunk_bytes_q), %xmm0	# 2 consts: K1:K2
	lea	(chunk_bytes,chunk_bytes,2), %eax # chunk_bytes * 3
	sub	%eax, len			# len -= chunk_bytes * 3

	movq	crc_init_q, %xmm1		# CRC for block 1
	pclmulqdq $0x00, %xmm0, %xmm1		# Multiply by K2

	movq    crc1, %xmm2			# CRC for block 2
	pclmulqdq $0x10, %xmm0, %xmm2		# Multiply by K1

	pxor    %xmm2,%xmm1
	movq    %xmm1, %rax
	xor	(bufp,chunk_bytes_q,2), %rax
	mov	crc2, crc_init_q
	crc32	%rax, crc_init_q
	lea	8(bufp,chunk_bytes_q,2), bufp

	################################################################
	## 5) If more blocks remain, goto (2):
	################################################################

	cmp	$128*24, len
	jae	.Lfull_block
	cmp	$SMALL_SIZE, len
	jae	.Lpartial_block

	#######################################################################
	## 6) Process any remainder without interleaving:
	#######################################################################
.Lsmall:
	test	len, len
	jz	.Ldone
	mov	len, %eax
	shr	$3, %eax
	jz	.Ldo_dword
.Ldo_qwords:
	crc32q	(bufp), crc_init_q
	add	$8, bufp
	dec	%eax
	jnz	.Ldo_qwords
.Ldo_dword:
	test	$4, len
	jz	.Ldo_word
	crc32l	(bufp), crc_init
	add	$4, bufp
.Ldo_word:
	test	$2, len
	jz	.Ldo_byte
	crc32w	(bufp), crc_init
	add	$2, bufp
.Ldo_byte:
	test	$1, len
	jz	.Ldone
	crc32b	(bufp), crc_init
.Ldone:
	mov	crc_init, %eax
        RET
SYM_FUNC_END(crc_pcl)

.section	.rodata, "a", @progbits
	################################################################
	## PCLMULQDQ tables
	## Table is 128 entries x 2 words (8 bytes) each
	################################################################
.align 8
K_table:
	.long 0x493c7d27, 0x00000001
	.long 0xba4fc28e, 0x493c7d27
	.long 0xddc0152b, 0xf20c0dfe
	.long 0x9e4addf8, 0xba4fc28e
	.long 0x39d3b296, 0x3da6d0cb
	.long 0x0715ce53, 0xddc0152b
	.long 0x47db8317, 0x1c291d04
	.long 0x0d3b6092, 0x9e4addf8
	.long 0xc96cfdc0, 0x740eef02
	.long 0x878a92a7, 0x39d3b296
	.long 0xdaece73e, 0x083a6eec
	.long 0xab7aff2a, 0x0715ce53
	.long 0x2162d385, 0xc49f4f67
	.long 0x83348832, 0x47db8317
	.long 0x299847d5, 0x2ad91c30
	.long 0xb9e02b86, 0x0d3b6092
	.long 0x18b33a4e, 0x6992cea2
	.long 0xb6dd949b, 0xc96cfdc0
	.long 0x78d9ccb7, 0x7e908048
	.long 0xbac2fd7b, 0x878a92a7
	.long 0xa60ce07b, 0x1b3d8f29
	.long 0xce7f39f4, 0xdaece73e
	.long 0x61d82e56, 0xf1d0f55e
	.long 0xd270f1a2, 0xab7aff2a
	.long 0xc619809d, 0xa87ab8a8
	.long 0x2b3cac5d, 0x2162d385
	.long 0x65863b64, 0x8462d800
	.long 0x1b03397f, 0x83348832
	.long 0xebb883bd, 0x71d111a8
	.long 0xb3e32c28, 0x299847d5
	.long 0x064f7f26, 0xffd852c6
	.long 0xdd7e3b0c, 0xb9e02b86
	.long 0xf285651c, 0xdcb17aa4
	.long 0x10746f3c, 0x18b33a4e
	.long 0xc7a68855, 0xf37c5aee
	.long 0x271d9844, 0xb6dd949b
	.long 0x8e766a0c, 0x6051d5a2
	.long 0x93a5f730, 0x78d9ccb7
	.long 0x6cb08e5c, 0x18b0d4ff
	.long 0x6b749fb2, 0xbac2fd7b
	.long 0x1393e203, 0x21f3d99c
	.long 0xcec3662e, 0xa60ce07b
	.long 0x96c515bb, 0x8f158014
	.long 0xe6fc4e6a, 0xce7f39f4
	.long 0x8227bb8a, 0xa00457f7
	.long 0xb0cd4768, 0x61d82e56
	.long 0x39c7ff35, 0x8d6d2c43
	.long 0xd7a4825c, 0xd270f1a2
	.long 0x0ab3844b, 0x00ac29cf
	.long 0x0167d312, 0xc619809d
	.long 0xf6076544, 0xe9adf796
	.long 0x26f6a60a, 0x2b3cac5d
	.long 0xa741c1bf, 0x96638b34
	.long 0x98d8d9cb, 0x65863b64
	.long 0x49c3cc9c, 0xe0e9f351
	.long 0x68bce87a, 0x1b03397f
	.long 0x57a3d037, 0x9af01f2d
	.long 0x6956fc3b, 0xebb883bd
	.long 0x42d98888, 0x2cff42cf
	.long 0x3771e98f, 0xb3e32c28
	.long 0xb42ae3d9, 0x88f25a3a
	.long 0x2178513a, 0x064f7f26
	.long 0xe0ac139e, 0x4e36f0b0
	.long 0x170076fa, 0xdd7e3b0c
	.long 0x444dd413, 0xbd6f81f8
	.long 0x6f345e45, 0xf285651c
	.long 0x41d17b64, 0x91c9bd4b
	.long 0xff0dba97, 0x10746f3c
	.long 0xa2b73df1, 0x885f087b
	.long 0xf872e54c, 0xc7a68855
	.long 0x1e41e9fc, 0x4c144932
	.long 0x86d8e4d2, 0x271d9844
	.long 0x651bd98b, 0x52148f02
	.long 0x5bb8f1bc, 0x8e766a0c
	.long 0xa90fd27a, 0xa3c6f37a
	.long 0xb3af077a, 0x93a5f730
	.long 0x4984d782, 0xd7c0557f
	.long 0xca6ef3ac, 0x6cb08e5c
	.long 0x234e0b26, 0x63ded06a
	.long 0xdd66cbbb, 0x6b749fb2
	.long 0x4597456a, 0x4d56973c
	.long 0xe9e28eb4, 0x1393e203
	.long 0x7b3ff57a, 0x9669c9df
	.long 0xc9c8b782, 0xcec3662e
	.long 0x3f70cc6f, 0xe417f38a
	.long 0x93e106a4, 0x96c515bb
	.long 0x62ec6c6d, 0x4b9e0f71
	.long 0xd813b325, 0xe6fc4e6a
	.long 0x0df04680, 0xd104b8fc
	.long 0x2342001e, 0x8227bb8a
	.long 0x0a2a8d7e, 0x5b397730
	.long 0x6d9a4957, 0xb0cd4768
	.long 0xe8b6368b, 0xe78eb416
	.long 0xd2c3ed1a, 0x39c7ff35
	.long 0x995a5724, 0x61ff0e01
	.long 0x9ef68d35, 0xd7a4825c
	.long 0x0c139b31, 0x8d96551c
	.long 0xf2271e60, 0x0ab3844b
	.long 0x0b0bf8ca, 0x0bf80dd2
	.long 0x2664fd8b, 0x0167d312
	.long 0xed64812d, 0x8821abed
	.long 0x02ee03b2, 0xf6076544
	.long 0x8604ae0f, 0x6a45d2b2
	.long 0x363bd6b3, 0x26f6a60a
	.long 0x135c83fd, 0xd8d26619
	.long 0x5fabe670, 0xa741c1bf
	.long 0x35ec3279, 0xde87806c
	.long 0x00bcf5f6, 0x98d8d9cb
	.long 0x8ae00689, 0x14338754
	.long 0x17f27698, 0x49c3cc9c
	.long 0x58ca5f00, 0x5bd2011f
	.long 0xaa7c7ad5, 0x68bce87a
	.long 0xb5cfca28, 0xdd07448e
	.long 0xded288f8, 0x57a3d037
	.long 0x59f229bc, 0xdde8f5b9
	.long 0x6d390dec, 0x6956fc3b
	.long 0x37170390, 0xa3e3e02c
	.long 0x6353c1cc, 0x42d98888
	.long 0xc4584f5c, 0xd73c7bea
	.long 0xf48642e9, 0x3771e98f
	.long 0x531377e2, 0x80ff0093
	.long 0xdd35bc8d, 0xb42ae3d9
	.long 0xb25b29f2, 0x8fe4c34d
	.long 0x9a5ede41, 0x2178513a
	.long 0xa563905d, 0xdf99fc11
	.long 0x45cddf4e, 0xe0ac139e
	.long 0xacfa3103, 0x6c23e841
	.long 0xa51b6135, 0x170076fa
