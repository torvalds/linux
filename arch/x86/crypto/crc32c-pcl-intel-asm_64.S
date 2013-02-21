/*
 * Implement fast CRC32C with PCLMULQDQ instructions. (x86_64)
 *
 * The white papers on CRC32C calculations with PCLMULQDQ instruction can be
 * downloaded from:
 * http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/crc-iscsi-polynomial-crc32-instruction-paper.pdf
 * http://www.intel.com/content/dam/www/public/us/en/documents/white-papers/fast-crc-computation-paper.pdf
 *
 * Copyright (C) 2012 Intel Corporation.
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

.macro LABEL prefix n
\prefix\n\():
.endm

.macro JMPTBL_ENTRY i
.word crc_\i - crc_array
.endm

.macro JNC_LESS_THAN j
	jnc less_than_\j
.endm

# Define threshold where buffers are considered "small" and routed to more
# efficient "by-1" code. This "by-1" code only handles up to 255 bytes, so
# SMALL_SIZE can be no larger than 255.

#define SMALL_SIZE 200

.if (SMALL_SIZE > 255)
.error "SMALL_ SIZE must be < 256"
.endif

# unsigned int crc_pcl(u8 *buffer, int len, unsigned int crc_init);

ENTRY(crc_pcl)
#define    bufp		%rdi
#define    bufp_dw	%edi
#define    bufp_w	%di
#define    bufp_b	%dil
#define    bufptmp	%rcx
#define    block_0	%rcx
#define    block_1	%rdx
#define    block_2	%r11
#define    len		%rsi
#define    len_dw	%esi
#define    len_w	%si
#define    len_b	%sil
#define    crc_init_arg %rdx
#define    tmp		%rbx
#define    crc_init	%r8
#define    crc_init_dw	%r8d
#define    crc1		%r9
#define    crc2		%r10

	pushq   %rbx
	pushq   %rdi
	pushq   %rsi

	## Move crc_init for Linux to a different
	mov     crc_init_arg, crc_init

	################################################################
	## 1) ALIGN:
	################################################################

	mov     bufp, bufptmp		# rdi = *buf
	neg     bufp
	and     $7, bufp		# calculate the unalignment amount of
					# the address
	je      proc_block		# Skip if aligned

	## If len is less than 8 and we're unaligned, we need to jump
	## to special code to avoid reading beyond the end of the buffer
	cmp     $8, len
	jae     do_align
	# less_than_8 expects length in upper 3 bits of len_dw
	# less_than_8_post_shl1 expects length = carryflag * 8 + len_dw[31:30]
	shl     $32-3+1, len_dw
	jmp     less_than_8_post_shl1

do_align:
	#### Calculate CRC of unaligned bytes of the buffer (if any)
	movq    (bufptmp), tmp		# load a quadward from the buffer
	add     bufp, bufptmp		# align buffer pointer for quadword
					# processing
	sub     bufp, len		# update buffer length
align_loop:
	crc32b  %bl, crc_init_dw 	# compute crc32 of 1-byte
	shr     $8, tmp			# get next byte
	dec     bufp
	jne     align_loop

proc_block:

	################################################################
	## 2) PROCESS  BLOCKS:
	################################################################

	## compute num of bytes to be processed
	movq    len, tmp		# save num bytes in tmp

	cmpq    $128*24, len
	jae     full_block

continue_block:
	cmpq    $SMALL_SIZE, len
	jb      small

	## len < 128*24
	movq    $2731, %rax		# 2731 = ceil(2^16 / 24)
	mul     len_dw
	shrq    $16, %rax

	## eax contains floor(bytes / 24) = num 24-byte chunks to do

	## process rax 24-byte chunks (128 >= rax >= 0)

	## compute end address of each block
	## block 0 (base addr + RAX * 8)
	## block 1 (base addr + RAX * 16)
	## block 2 (base addr + RAX * 24)
	lea     (bufptmp, %rax, 8), block_0
	lea     (block_0, %rax, 8), block_1
	lea     (block_1, %rax, 8), block_2

	xor     crc1, crc1
	xor     crc2, crc2

	## branch into array
	lea	jump_table(%rip), bufp
	movzxw  (bufp, %rax, 2), len
	offset=crc_array-jump_table
	lea     offset(bufp, len, 1), bufp
	jmp     *bufp

	################################################################
	## 2a) PROCESS FULL BLOCKS:
	################################################################
full_block:
	movq    $128,%rax
	lea     128*8*2(block_0), block_1
	lea     128*8*3(block_0), block_2
	add     $128*8*1, block_0

	xor     crc1,crc1
	xor     crc2,crc2

	# Fall thruogh into top of crc array (crc_128)

	################################################################
	## 3) CRC Array:
	################################################################

crc_array:
	i=128
.rept 128-1
.altmacro
LABEL crc_ %i
.noaltmacro
	crc32q   -i*8(block_0), crc_init
	crc32q   -i*8(block_1), crc1
	crc32q   -i*8(block_2), crc2
	i=(i-1)
.endr

.altmacro
LABEL crc_ %i
.noaltmacro
	crc32q   -i*8(block_0), crc_init
	crc32q   -i*8(block_1), crc1
# SKIP  crc32  -i*8(block_2), crc2 ; Don't do this one yet

	mov     block_2, block_0

	################################################################
	## 4) Combine three results:
	################################################################

	lea	(K_table-16)(%rip), bufp	# first entry is for idx 1
	shlq    $3, %rax			# rax *= 8
	subq    %rax, tmp			# tmp -= rax*8
	shlq    $1, %rax
	subq    %rax, tmp			# tmp -= rax*16
						# (total tmp -= rax*24)
	addq    %rax, bufp

	movdqa  (bufp), %xmm0			# 2 consts: K1:K2

	movq    crc_init, %xmm1			# CRC for block 1
	pclmulqdq $0x00,%xmm0,%xmm1		# Multiply by K2

	movq    crc1, %xmm2			# CRC for block 2
	pclmulqdq $0x10, %xmm0, %xmm2		# Multiply by K1

	pxor    %xmm2,%xmm1
	movq    %xmm1, %rax
	xor     -i*8(block_2), %rax
	mov     crc2, crc_init
	crc32   %rax, crc_init

################################################################
## 5) Check for end:
################################################################

LABEL crc_ 0
	mov     tmp, len
	cmp     $128*24, tmp
	jae     full_block
	cmp     $24, tmp
	jae     continue_block

less_than_24:
	shl     $32-4, len_dw			# less_than_16 expects length
						# in upper 4 bits of len_dw
	jnc     less_than_16
	crc32q  (bufptmp), crc_init
	crc32q  8(bufptmp), crc_init
	jz      do_return
	add     $16, bufptmp
	# len is less than 8 if we got here
	# less_than_8 expects length in upper 3 bits of len_dw
	# less_than_8_post_shl1 expects length = carryflag * 8 + len_dw[31:30]
	shl     $2, len_dw
	jmp     less_than_8_post_shl1

	#######################################################################
	## 6) LESS THAN 256-bytes REMAIN AT THIS POINT (8-bits of len are full)
	#######################################################################
small:
	shl $32-8, len_dw		# Prepare len_dw for less_than_256
	j=256
.rept 5					# j = {256, 128, 64, 32, 16}
.altmacro
LABEL less_than_ %j			# less_than_j: Length should be in
					# upper lg(j) bits of len_dw
	j=(j/2)
	shl     $1, len_dw		# Get next MSB
	JNC_LESS_THAN %j
.noaltmacro
	i=0
.rept (j/8)
	crc32q  i(bufptmp), crc_init	# Compute crc32 of 8-byte data
	i=i+8
.endr
	jz      do_return		# Return if remaining length is zero
	add     $j, bufptmp		# Advance buf
.endr

less_than_8:				# Length should be stored in
					# upper 3 bits of len_dw
	shl     $1, len_dw
less_than_8_post_shl1:
	jnc     less_than_4
	crc32l  (bufptmp), crc_init_dw	# CRC of 4 bytes
	jz      do_return		# return if remaining data is zero
	add     $4, bufptmp
less_than_4:				# Length should be stored in
					# upper 2 bits of len_dw
	shl     $1, len_dw
	jnc     less_than_2
	crc32w  (bufptmp), crc_init_dw	# CRC of 2 bytes
	jz      do_return		# return if remaining data is zero
	add     $2, bufptmp
less_than_2:				# Length should be stored in the MSB
					# of len_dw
	shl     $1, len_dw
	jnc     less_than_1
	crc32b  (bufptmp), crc_init_dw	# CRC of 1 byte
less_than_1:				# Length should be zero
do_return:
	movq    crc_init, %rax
	popq    %rsi
	popq    %rdi
	popq    %rbx
        ret

        ################################################################
        ## jump table        Table is 129 entries x 2 bytes each
        ################################################################
.align 4
jump_table:
	i=0
.rept 129
.altmacro
JMPTBL_ENTRY %i
.noaltmacro
	i=i+1
.endr

ENDPROC(crc_pcl)

	################################################################
	## PCLMULQDQ tables
	## Table is 128 entries x 2 quad words each
	################################################################
.data
.align 64
K_table:
        .quad 0x14cd00bd6,0x105ec76f0
        .quad 0x0ba4fc28e,0x14cd00bd6
        .quad 0x1d82c63da,0x0f20c0dfe
        .quad 0x09e4addf8,0x0ba4fc28e
        .quad 0x039d3b296,0x1384aa63a
        .quad 0x102f9b8a2,0x1d82c63da
        .quad 0x14237f5e6,0x01c291d04
        .quad 0x00d3b6092,0x09e4addf8
        .quad 0x0c96cfdc0,0x0740eef02
        .quad 0x18266e456,0x039d3b296
        .quad 0x0daece73e,0x0083a6eec
        .quad 0x0ab7aff2a,0x102f9b8a2
        .quad 0x1248ea574,0x1c1733996
        .quad 0x083348832,0x14237f5e6
        .quad 0x12c743124,0x02ad91c30
        .quad 0x0b9e02b86,0x00d3b6092
        .quad 0x018b33a4e,0x06992cea2
        .quad 0x1b331e26a,0x0c96cfdc0
        .quad 0x17d35ba46,0x07e908048
        .quad 0x1bf2e8b8a,0x18266e456
        .quad 0x1a3e0968a,0x11ed1f9d8
        .quad 0x0ce7f39f4,0x0daece73e
        .quad 0x061d82e56,0x0f1d0f55e
        .quad 0x0d270f1a2,0x0ab7aff2a
        .quad 0x1c3f5f66c,0x0a87ab8a8
        .quad 0x12ed0daac,0x1248ea574
        .quad 0x065863b64,0x08462d800
        .quad 0x11eef4f8e,0x083348832
        .quad 0x1ee54f54c,0x071d111a8
        .quad 0x0b3e32c28,0x12c743124
        .quad 0x0064f7f26,0x0ffd852c6
        .quad 0x0dd7e3b0c,0x0b9e02b86
        .quad 0x0f285651c,0x0dcb17aa4
        .quad 0x010746f3c,0x018b33a4e
        .quad 0x1c24afea4,0x0f37c5aee
        .quad 0x0271d9844,0x1b331e26a
        .quad 0x08e766a0c,0x06051d5a2
        .quad 0x093a5f730,0x17d35ba46
        .quad 0x06cb08e5c,0x11d5ca20e
        .quad 0x06b749fb2,0x1bf2e8b8a
        .quad 0x1167f94f2,0x021f3d99c
        .quad 0x0cec3662e,0x1a3e0968a
        .quad 0x19329634a,0x08f158014
        .quad 0x0e6fc4e6a,0x0ce7f39f4
        .quad 0x08227bb8a,0x1a5e82106
        .quad 0x0b0cd4768,0x061d82e56
        .quad 0x13c2b89c4,0x188815ab2
        .quad 0x0d7a4825c,0x0d270f1a2
        .quad 0x10f5ff2ba,0x105405f3e
        .quad 0x00167d312,0x1c3f5f66c
        .quad 0x0f6076544,0x0e9adf796
        .quad 0x026f6a60a,0x12ed0daac
        .quad 0x1a2adb74e,0x096638b34
        .quad 0x19d34af3a,0x065863b64
        .quad 0x049c3cc9c,0x1e50585a0
        .quad 0x068bce87a,0x11eef4f8e
        .quad 0x1524fa6c6,0x19f1c69dc
        .quad 0x16cba8aca,0x1ee54f54c
        .quad 0x042d98888,0x12913343e
        .quad 0x1329d9f7e,0x0b3e32c28
        .quad 0x1b1c69528,0x088f25a3a
        .quad 0x02178513a,0x0064f7f26
        .quad 0x0e0ac139e,0x04e36f0b0
        .quad 0x0170076fa,0x0dd7e3b0c
        .quad 0x141a1a2e2,0x0bd6f81f8
        .quad 0x16ad828b4,0x0f285651c
        .quad 0x041d17b64,0x19425cbba
        .quad 0x1fae1cc66,0x010746f3c
        .quad 0x1a75b4b00,0x18db37e8a
        .quad 0x0f872e54c,0x1c24afea4
        .quad 0x01e41e9fc,0x04c144932
        .quad 0x086d8e4d2,0x0271d9844
        .quad 0x160f7af7a,0x052148f02
        .quad 0x05bb8f1bc,0x08e766a0c
        .quad 0x0a90fd27a,0x0a3c6f37a
        .quad 0x0b3af077a,0x093a5f730
        .quad 0x04984d782,0x1d22c238e
        .quad 0x0ca6ef3ac,0x06cb08e5c
        .quad 0x0234e0b26,0x063ded06a
        .quad 0x1d88abd4a,0x06b749fb2
        .quad 0x04597456a,0x04d56973c
        .quad 0x0e9e28eb4,0x1167f94f2
        .quad 0x07b3ff57a,0x19385bf2e
        .quad 0x0c9c8b782,0x0cec3662e
        .quad 0x13a9cba9e,0x0e417f38a
        .quad 0x093e106a4,0x19329634a
        .quad 0x167001a9c,0x14e727980
        .quad 0x1ddffc5d4,0x0e6fc4e6a
        .quad 0x00df04680,0x0d104b8fc
        .quad 0x02342001e,0x08227bb8a
        .quad 0x00a2a8d7e,0x05b397730
        .quad 0x168763fa6,0x0b0cd4768
        .quad 0x1ed5a407a,0x0e78eb416
        .quad 0x0d2c3ed1a,0x13c2b89c4
        .quad 0x0995a5724,0x1641378f0
        .quad 0x19b1afbc4,0x0d7a4825c
        .quad 0x109ffedc0,0x08d96551c
        .quad 0x0f2271e60,0x10f5ff2ba
        .quad 0x00b0bf8ca,0x00bf80dd2
        .quad 0x123888b7a,0x00167d312
        .quad 0x1e888f7dc,0x18dcddd1c
        .quad 0x002ee03b2,0x0f6076544
        .quad 0x183e8d8fe,0x06a45d2b2
        .quad 0x133d7a042,0x026f6a60a
        .quad 0x116b0f50c,0x1dd3e10e8
        .quad 0x05fabe670,0x1a2adb74e
        .quad 0x130004488,0x0de87806c
        .quad 0x000bcf5f6,0x19d34af3a
        .quad 0x18f0c7078,0x014338754
        .quad 0x017f27698,0x049c3cc9c
        .quad 0x058ca5f00,0x15e3e77ee
        .quad 0x1af900c24,0x068bce87a
        .quad 0x0b5cfca28,0x0dd07448e
        .quad 0x0ded288f8,0x1524fa6c6
        .quad 0x059f229bc,0x1d8048348
        .quad 0x06d390dec,0x16cba8aca
        .quad 0x037170390,0x0a3e3e02c
        .quad 0x06353c1cc,0x042d98888
        .quad 0x0c4584f5c,0x0d73c7bea
        .quad 0x1f16a3418,0x1329d9f7e
        .quad 0x0531377e2,0x185137662
        .quad 0x1d8d9ca7c,0x1b1c69528
        .quad 0x0b25b29f2,0x18a08b5bc
        .quad 0x19fb2a8b0,0x02178513a
        .quad 0x1a08fe6ac,0x1da758ae0
        .quad 0x045cddf4e,0x0e0ac139e
        .quad 0x1a91647f2,0x169cf9eb0
        .quad 0x1a0f717c4,0x0170076fa
