########################################################################
# Implement fast CRC-T10DIF computation with SSE and PCLMULQDQ instructions
#
# Copyright (c) 2013, Intel Corporation
#
# Authors:
#     Erdinc Ozturk <erdinc.ozturk@intel.com>
#     Vinodh Gopal <vinodh.gopal@intel.com>
#     James Guilford <james.guilford@intel.com>
#     Tim Chen <tim.c.chen@linux.intel.com>
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# COPYING in the main directory of this source tree, or the
# OpenIB.org BSD license below:
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
# * Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in the
#   documentation and/or other materials provided with the
#   distribution.
#
# * Neither the name of the Intel Corporation nor the names of its
#   contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
#
# THIS SOFTWARE IS PROVIDED BY INTEL CORPORATION ""AS IS"" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL INTEL CORPORATION OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#       Reference paper titled "Fast CRC Computation for Generic
#	Polynomials Using PCLMULQDQ Instruction"
#       URL: http://www.intel.com/content/dam/www/public/us/en/documents
#  /white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf
#

#include <linux/linkage.h>

.text

#define		init_crc	%edi
#define		buf		%rsi
#define		len		%rdx

#define		FOLD_CONSTS	%xmm10
#define		BSWAP_MASK	%xmm11

# Fold reg1, reg2 into the next 32 data bytes, storing the result back into
# reg1, reg2.
.macro	fold_32_bytes	offset, reg1, reg2
	movdqu	\offset(buf), %xmm9
	movdqu	\offset+16(buf), %xmm12
	pshufb	BSWAP_MASK, %xmm9
	pshufb	BSWAP_MASK, %xmm12
	movdqa	\reg1, %xmm8
	movdqa	\reg2, %xmm13
	pclmulqdq	$0x00, FOLD_CONSTS, \reg1
	pclmulqdq	$0x11, FOLD_CONSTS, %xmm8
	pclmulqdq	$0x00, FOLD_CONSTS, \reg2
	pclmulqdq	$0x11, FOLD_CONSTS, %xmm13
	pxor	%xmm9 , \reg1
	xorps	%xmm8 , \reg1
	pxor	%xmm12, \reg2
	xorps	%xmm13, \reg2
.endm

# Fold src_reg into dst_reg.
.macro	fold_16_bytes	src_reg, dst_reg
	movdqa	\src_reg, %xmm8
	pclmulqdq	$0x11, FOLD_CONSTS, \src_reg
	pclmulqdq	$0x00, FOLD_CONSTS, %xmm8
	pxor	%xmm8, \dst_reg
	xorps	\src_reg, \dst_reg
.endm

#
# u16 crc_t10dif_pcl(u16 init_crc, const *u8 buf, size_t len);
#
# Assumes len >= 16.
#
SYM_FUNC_START(crc_t10dif_pcl)

	movdqa	.Lbswap_mask(%rip), BSWAP_MASK

	# For sizes less than 256 bytes, we can't fold 128 bytes at a time.
	cmp	$256, len
	jl	.Lless_than_256_bytes

	# Load the first 128 data bytes.  Byte swapping is necessary to make the
	# bit order match the polynomial coefficient order.
	movdqu	16*0(buf), %xmm0
	movdqu	16*1(buf), %xmm1
	movdqu	16*2(buf), %xmm2
	movdqu	16*3(buf), %xmm3
	movdqu	16*4(buf), %xmm4
	movdqu	16*5(buf), %xmm5
	movdqu	16*6(buf), %xmm6
	movdqu	16*7(buf), %xmm7
	add	$128, buf
	pshufb	BSWAP_MASK, %xmm0
	pshufb	BSWAP_MASK, %xmm1
	pshufb	BSWAP_MASK, %xmm2
	pshufb	BSWAP_MASK, %xmm3
	pshufb	BSWAP_MASK, %xmm4
	pshufb	BSWAP_MASK, %xmm5
	pshufb	BSWAP_MASK, %xmm6
	pshufb	BSWAP_MASK, %xmm7

	# XOR the first 16 data *bits* with the initial CRC value.
	pxor	%xmm8, %xmm8
	pinsrw	$7, init_crc, %xmm8
	pxor	%xmm8, %xmm0

	movdqa	.Lfold_across_128_bytes_consts(%rip), FOLD_CONSTS

	# Subtract 128 for the 128 data bytes just consumed.  Subtract another
	# 128 to simplify the termination condition of the following loop.
	sub	$256, len

	# While >= 128 data bytes remain (not counting xmm0-7), fold the 128
	# bytes xmm0-7 into them, storing the result back into xmm0-7.
.Lfold_128_bytes_loop:
	fold_32_bytes	0, %xmm0, %xmm1
	fold_32_bytes	32, %xmm2, %xmm3
	fold_32_bytes	64, %xmm4, %xmm5
	fold_32_bytes	96, %xmm6, %xmm7
	add	$128, buf
	sub	$128, len
	jge	.Lfold_128_bytes_loop

	# Now fold the 112 bytes in xmm0-xmm6 into the 16 bytes in xmm7.

	# Fold across 64 bytes.
	movdqa	.Lfold_across_64_bytes_consts(%rip), FOLD_CONSTS
	fold_16_bytes	%xmm0, %xmm4
	fold_16_bytes	%xmm1, %xmm5
	fold_16_bytes	%xmm2, %xmm6
	fold_16_bytes	%xmm3, %xmm7
	# Fold across 32 bytes.
	movdqa	.Lfold_across_32_bytes_consts(%rip), FOLD_CONSTS
	fold_16_bytes	%xmm4, %xmm6
	fold_16_bytes	%xmm5, %xmm7
	# Fold across 16 bytes.
	movdqa	.Lfold_across_16_bytes_consts(%rip), FOLD_CONSTS
	fold_16_bytes	%xmm6, %xmm7

	# Add 128 to get the correct number of data bytes remaining in 0...127
	# (not counting xmm7), following the previous extra subtraction by 128.
	# Then subtract 16 to simplify the termination condition of the
	# following loop.
	add	$128-16, len

	# While >= 16 data bytes remain (not counting xmm7), fold the 16 bytes
	# xmm7 into them, storing the result back into xmm7.
	jl	.Lfold_16_bytes_loop_done
.Lfold_16_bytes_loop:
	movdqa	%xmm7, %xmm8
	pclmulqdq	$0x11, FOLD_CONSTS, %xmm7
	pclmulqdq	$0x00, FOLD_CONSTS, %xmm8
	pxor	%xmm8, %xmm7
	movdqu	(buf), %xmm0
	pshufb	BSWAP_MASK, %xmm0
	pxor	%xmm0 , %xmm7
	add	$16, buf
	sub	$16, len
	jge	.Lfold_16_bytes_loop

.Lfold_16_bytes_loop_done:
	# Add 16 to get the correct number of data bytes remaining in 0...15
	# (not counting xmm7), following the previous extra subtraction by 16.
	add	$16, len
	je	.Lreduce_final_16_bytes

.Lhandle_partial_segment:
	# Reduce the last '16 + len' bytes where 1 <= len <= 15 and the first 16
	# bytes are in xmm7 and the rest are the remaining data in 'buf'.  To do
	# this without needing a fold constant for each possible 'len', redivide
	# the bytes into a first chunk of 'len' bytes and a second chunk of 16
	# bytes, then fold the first chunk into the second.

	movdqa	%xmm7, %xmm2

	# xmm1 = last 16 original data bytes
	movdqu	-16(buf, len), %xmm1
	pshufb	BSWAP_MASK, %xmm1

	# xmm2 = high order part of second chunk: xmm7 left-shifted by 'len' bytes.
	lea	.Lbyteshift_table+16(%rip), %rax
	sub	len, %rax
	movdqu	(%rax), %xmm0
	pshufb	%xmm0, %xmm2

	# xmm7 = first chunk: xmm7 right-shifted by '16-len' bytes.
	pxor	.Lmask1(%rip), %xmm0
	pshufb	%xmm0, %xmm7

	# xmm1 = second chunk: 'len' bytes from xmm1 (low-order bytes),
	# then '16-len' bytes from xmm2 (high-order bytes).
	pblendvb	%xmm2, %xmm1	#xmm0 is implicit

	# Fold the first chunk into the second chunk, storing the result in xmm7.
	movdqa	%xmm7, %xmm8
	pclmulqdq	$0x11, FOLD_CONSTS, %xmm7
	pclmulqdq	$0x00, FOLD_CONSTS, %xmm8
	pxor	%xmm8, %xmm7
	pxor	%xmm1, %xmm7

.Lreduce_final_16_bytes:
	# Reduce the 128-bit value M(x), stored in xmm7, to the final 16-bit CRC

	# Load 'x^48 * (x^48 mod G(x))' and 'x^48 * (x^80 mod G(x))'.
	movdqa	.Lfinal_fold_consts(%rip), FOLD_CONSTS

	# Fold the high 64 bits into the low 64 bits, while also multiplying by
	# x^64.  This produces a 128-bit value congruent to x^64 * M(x) and
	# whose low 48 bits are 0.
	movdqa	%xmm7, %xmm0
	pclmulqdq	$0x11, FOLD_CONSTS, %xmm7 # high bits * x^48 * (x^80 mod G(x))
	pslldq	$8, %xmm0
	pxor	%xmm0, %xmm7			  # + low bits * x^64

	# Fold the high 32 bits into the low 96 bits.  This produces a 96-bit
	# value congruent to x^64 * M(x) and whose low 48 bits are 0.
	movdqa	%xmm7, %xmm0
	pand	.Lmask2(%rip), %xmm0		  # zero high 32 bits
	psrldq	$12, %xmm7			  # extract high 32 bits
	pclmulqdq	$0x00, FOLD_CONSTS, %xmm7 # high 32 bits * x^48 * (x^48 mod G(x))
	pxor	%xmm0, %xmm7			  # + low bits

	# Load G(x) and floor(x^48 / G(x)).
	movdqa	.Lbarrett_reduction_consts(%rip), FOLD_CONSTS

	# Use Barrett reduction to compute the final CRC value.
	movdqa	%xmm7, %xmm0
	pclmulqdq	$0x11, FOLD_CONSTS, %xmm7 # high 32 bits * floor(x^48 / G(x))
	psrlq	$32, %xmm7			  # /= x^32
	pclmulqdq	$0x00, FOLD_CONSTS, %xmm7 # *= G(x)
	psrlq	$48, %xmm0
	pxor	%xmm7, %xmm0		     # + low 16 nonzero bits
	# Final CRC value (x^16 * M(x)) mod G(x) is in low 16 bits of xmm0.

	pextrw	$0, %xmm0, %eax
	RET

.align 16
.Lless_than_256_bytes:
	# Checksumming a buffer of length 16...255 bytes

	# Load the first 16 data bytes.
	movdqu	(buf), %xmm7
	pshufb	BSWAP_MASK, %xmm7
	add	$16, buf

	# XOR the first 16 data *bits* with the initial CRC value.
	pxor	%xmm0, %xmm0
	pinsrw	$7, init_crc, %xmm0
	pxor	%xmm0, %xmm7

	movdqa	.Lfold_across_16_bytes_consts(%rip), FOLD_CONSTS
	cmp	$16, len
	je	.Lreduce_final_16_bytes		# len == 16
	sub	$32, len
	jge	.Lfold_16_bytes_loop		# 32 <= len <= 255
	add	$16, len
	jmp	.Lhandle_partial_segment	# 17 <= len <= 31
SYM_FUNC_END(crc_t10dif_pcl)

.section	.rodata, "a", @progbits
.align 16

# Fold constants precomputed from the polynomial 0x18bb7
# G(x) = x^16 + x^15 + x^11 + x^9 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + x^0
.Lfold_across_128_bytes_consts:
	.quad		0x0000000000006123	# x^(8*128)	mod G(x)
	.quad		0x0000000000002295	# x^(8*128+64)	mod G(x)
.Lfold_across_64_bytes_consts:
	.quad		0x0000000000001069	# x^(4*128)	mod G(x)
	.quad		0x000000000000dd31	# x^(4*128+64)	mod G(x)
.Lfold_across_32_bytes_consts:
	.quad		0x000000000000857d	# x^(2*128)	mod G(x)
	.quad		0x0000000000007acc	# x^(2*128+64)	mod G(x)
.Lfold_across_16_bytes_consts:
	.quad		0x000000000000a010	# x^(1*128)	mod G(x)
	.quad		0x0000000000001faa	# x^(1*128+64)	mod G(x)
.Lfinal_fold_consts:
	.quad		0x1368000000000000	# x^48 * (x^48 mod G(x))
	.quad		0x2d56000000000000	# x^48 * (x^80 mod G(x))
.Lbarrett_reduction_consts:
	.quad		0x0000000000018bb7	# G(x)
	.quad		0x00000001f65a57f8	# floor(x^48 / G(x))

.section	.rodata.cst16.mask1, "aM", @progbits, 16
.align 16
.Lmask1:
	.octa	0x80808080808080808080808080808080

.section	.rodata.cst16.mask2, "aM", @progbits, 16
.align 16
.Lmask2:
	.octa	0x00000000FFFFFFFFFFFFFFFFFFFFFFFF

.section	.rodata.cst16.bswap_mask, "aM", @progbits, 16
.align 16
.Lbswap_mask:
	.octa	0x000102030405060708090A0B0C0D0E0F

.section	.rodata.cst32.byteshift_table, "aM", @progbits, 32
.align 16
# For 1 <= len <= 15, the 16-byte vector beginning at &byteshift_table[16 - len]
# is the index vector to shift left by 'len' bytes, and is also {0x80, ...,
# 0x80} XOR the index vector to shift right by '16 - len' bytes.
.Lbyteshift_table:
	.byte		 0x0, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87
	.byte		0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f
	.byte		 0x0,  0x1,  0x2,  0x3,  0x4,  0x5,  0x6,  0x7
	.byte		 0x8,  0x9,  0xa,  0xb,  0xc,  0xd,  0xe , 0x0
