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
########################################################################
#       Function API:
#       UINT16 crc_t10dif_pcl(
#               UINT16 init_crc, //initial CRC value, 16 bits
#               const unsigned char *buf, //buffer pointer to calculate CRC on
#               UINT64 len //buffer length in bytes (64-bit data)
#       );
#
#       Reference paper titled "Fast CRC Computation for Generic
#	Polynomials Using PCLMULQDQ Instruction"
#       URL: http://www.intel.com/content/dam/www/public/us/en/documents
#  /white-papers/fast-crc-computation-generic-polynomials-pclmulqdq-paper.pdf
#
#

#include <linux/linkage.h>

.text

#define        arg1 %rdi
#define        arg2 %rsi
#define        arg3 %rdx

#define        arg1_low32 %edi

ENTRY(crc_t10dif_pcl)
.align 16

	# adjust the 16-bit initial_crc value, scale it to 32 bits
	shl	$16, arg1_low32

	# Allocate Stack Space
	mov     %rsp, %rcx
	sub	$16*2, %rsp
	# align stack to 16 byte boundary
	and     $~(0x10 - 1), %rsp

	# check if smaller than 256
	cmp	$256, arg3

	# for sizes less than 128, we can't fold 64B at a time...
	jl	_less_than_128


	# load the initial crc value
	movd	arg1_low32, %xmm10	# initial crc

	# crc value does not need to be byte-reflected, but it needs
	# to be moved to the high part of the register.
	# because data will be byte-reflected and will align with
	# initial crc at correct place.
	pslldq	$12, %xmm10

	movdqa  SHUF_MASK(%rip), %xmm11
	# receive the initial 64B data, xor the initial crc value
	movdqu	16*0(arg2), %xmm0
	movdqu	16*1(arg2), %xmm1
	movdqu	16*2(arg2), %xmm2
	movdqu	16*3(arg2), %xmm3
	movdqu	16*4(arg2), %xmm4
	movdqu	16*5(arg2), %xmm5
	movdqu	16*6(arg2), %xmm6
	movdqu	16*7(arg2), %xmm7

	pshufb	%xmm11, %xmm0
	# XOR the initial_crc value
	pxor	%xmm10, %xmm0
	pshufb	%xmm11, %xmm1
	pshufb	%xmm11, %xmm2
	pshufb	%xmm11, %xmm3
	pshufb	%xmm11, %xmm4
	pshufb	%xmm11, %xmm5
	pshufb	%xmm11, %xmm6
	pshufb	%xmm11, %xmm7

	movdqa	rk3(%rip), %xmm10	#xmm10 has rk3 and rk4
					#imm value of pclmulqdq instruction
					#will determine which constant to use

	#################################################################
	# we subtract 256 instead of 128 to save one instruction from the loop
	sub	$256, arg3

	# at this section of the code, there is 64*x+y (0<=y<64) bytes of
	# buffer. The _fold_64_B_loop will fold 64B at a time
	# until we have 64+y Bytes of buffer


	# fold 64B at a time. This section of the code folds 4 xmm
	# registers in parallel
_fold_64_B_loop:

	# update the buffer pointer
	add	$128, arg2		#    buf += 64#

	movdqu	16*0(arg2), %xmm9
	movdqu	16*1(arg2), %xmm12
	pshufb	%xmm11, %xmm9
	pshufb	%xmm11, %xmm12
	movdqa	%xmm0, %xmm8
	movdqa	%xmm1, %xmm13
	pclmulqdq	$0x0 , %xmm10, %xmm0
	pclmulqdq	$0x11, %xmm10, %xmm8
	pclmulqdq	$0x0 , %xmm10, %xmm1
	pclmulqdq	$0x11, %xmm10, %xmm13
	pxor	%xmm9 , %xmm0
	xorps	%xmm8 , %xmm0
	pxor	%xmm12, %xmm1
	xorps	%xmm13, %xmm1

	movdqu	16*2(arg2), %xmm9
	movdqu	16*3(arg2), %xmm12
	pshufb	%xmm11, %xmm9
	pshufb	%xmm11, %xmm12
	movdqa	%xmm2, %xmm8
	movdqa	%xmm3, %xmm13
	pclmulqdq	$0x0, %xmm10, %xmm2
	pclmulqdq	$0x11, %xmm10, %xmm8
	pclmulqdq	$0x0, %xmm10, %xmm3
	pclmulqdq	$0x11, %xmm10, %xmm13
	pxor	%xmm9 , %xmm2
	xorps	%xmm8 , %xmm2
	pxor	%xmm12, %xmm3
	xorps	%xmm13, %xmm3

	movdqu	16*4(arg2), %xmm9
	movdqu	16*5(arg2), %xmm12
	pshufb	%xmm11, %xmm9
	pshufb	%xmm11, %xmm12
	movdqa	%xmm4, %xmm8
	movdqa	%xmm5, %xmm13
	pclmulqdq	$0x0,  %xmm10, %xmm4
	pclmulqdq	$0x11, %xmm10, %xmm8
	pclmulqdq	$0x0,  %xmm10, %xmm5
	pclmulqdq	$0x11, %xmm10, %xmm13
	pxor	%xmm9 ,  %xmm4
	xorps	%xmm8 ,  %xmm4
	pxor	%xmm12,  %xmm5
	xorps	%xmm13,  %xmm5

	movdqu	16*6(arg2), %xmm9
	movdqu	16*7(arg2), %xmm12
	pshufb	%xmm11, %xmm9
	pshufb	%xmm11, %xmm12
	movdqa	%xmm6 , %xmm8
	movdqa	%xmm7 , %xmm13
	pclmulqdq	$0x0 , %xmm10, %xmm6
	pclmulqdq	$0x11, %xmm10, %xmm8
	pclmulqdq	$0x0 , %xmm10, %xmm7
	pclmulqdq	$0x11, %xmm10, %xmm13
	pxor	%xmm9 , %xmm6
	xorps	%xmm8 , %xmm6
	pxor	%xmm12, %xmm7
	xorps	%xmm13, %xmm7

	sub	$128, arg3

	# check if there is another 64B in the buffer to be able to fold
	jge	_fold_64_B_loop
	##################################################################


	add	$128, arg2
	# at this point, the buffer pointer is pointing at the last y Bytes
	# of the buffer the 64B of folded data is in 4 of the xmm
	# registers: xmm0, xmm1, xmm2, xmm3


	# fold the 8 xmm registers to 1 xmm register with different constants

	movdqa	rk9(%rip), %xmm10
	movdqa	%xmm0, %xmm8
	pclmulqdq	$0x11, %xmm10, %xmm0
	pclmulqdq	$0x0 , %xmm10, %xmm8
	pxor	%xmm8, %xmm7
	xorps	%xmm0, %xmm7

	movdqa	rk11(%rip), %xmm10
	movdqa	%xmm1, %xmm8
	pclmulqdq	 $0x11, %xmm10, %xmm1
	pclmulqdq	 $0x0 , %xmm10, %xmm8
	pxor	%xmm8, %xmm7
	xorps	%xmm1, %xmm7

	movdqa	rk13(%rip), %xmm10
	movdqa	%xmm2, %xmm8
	pclmulqdq	 $0x11, %xmm10, %xmm2
	pclmulqdq	 $0x0 , %xmm10, %xmm8
	pxor	%xmm8, %xmm7
	pxor	%xmm2, %xmm7

	movdqa	rk15(%rip), %xmm10
	movdqa	%xmm3, %xmm8
	pclmulqdq	$0x11, %xmm10, %xmm3
	pclmulqdq	$0x0 , %xmm10, %xmm8
	pxor	%xmm8, %xmm7
	xorps	%xmm3, %xmm7

	movdqa	rk17(%rip), %xmm10
	movdqa	%xmm4, %xmm8
	pclmulqdq	$0x11, %xmm10, %xmm4
	pclmulqdq	$0x0 , %xmm10, %xmm8
	pxor	%xmm8, %xmm7
	pxor	%xmm4, %xmm7

	movdqa	rk19(%rip), %xmm10
	movdqa	%xmm5, %xmm8
	pclmulqdq	$0x11, %xmm10, %xmm5
	pclmulqdq	$0x0 , %xmm10, %xmm8
	pxor	%xmm8, %xmm7
	xorps	%xmm5, %xmm7

	movdqa	rk1(%rip), %xmm10	#xmm10 has rk1 and rk2
					#imm value of pclmulqdq instruction
					#will determine which constant to use
	movdqa	%xmm6, %xmm8
	pclmulqdq	$0x11, %xmm10, %xmm6
	pclmulqdq	$0x0 , %xmm10, %xmm8
	pxor	%xmm8, %xmm7
	pxor	%xmm6, %xmm7


	# instead of 64, we add 48 to the loop counter to save 1 instruction
	# from the loop instead of a cmp instruction, we use the negative
	# flag with the jl instruction
	add	$128-16, arg3
	jl	_final_reduction_for_128

	# now we have 16+y bytes left to reduce. 16 Bytes is in register xmm7
	# and the rest is in memory. We can fold 16 bytes at a time if y>=16
	# continue folding 16B at a time

_16B_reduction_loop:
	movdqa	%xmm7, %xmm8
	pclmulqdq	$0x11, %xmm10, %xmm7
	pclmulqdq	$0x0 , %xmm10, %xmm8
	pxor	%xmm8, %xmm7
	movdqu	(arg2), %xmm0
	pshufb	%xmm11, %xmm0
	pxor	%xmm0 , %xmm7
	add	$16, arg2
	sub	$16, arg3
	# instead of a cmp instruction, we utilize the flags with the
	# jge instruction equivalent of: cmp arg3, 16-16
	# check if there is any more 16B in the buffer to be able to fold
	jge	_16B_reduction_loop

	#now we have 16+z bytes left to reduce, where 0<= z < 16.
	#first, we reduce the data in the xmm7 register


_final_reduction_for_128:
	# check if any more data to fold. If not, compute the CRC of
	# the final 128 bits
	add	$16, arg3
	je	_128_done

	# here we are getting data that is less than 16 bytes.
	# since we know that there was data before the pointer, we can
	# offset the input pointer before the actual point, to receive
	# exactly 16 bytes. after that the registers need to be adjusted.
_get_last_two_xmms:
	movdqa	%xmm7, %xmm2

	movdqu	-16(arg2, arg3), %xmm1
	pshufb	%xmm11, %xmm1

	# get rid of the extra data that was loaded before
	# load the shift constant
	lea	pshufb_shf_table+16(%rip), %rax
	sub	arg3, %rax
	movdqu	(%rax), %xmm0

	# shift xmm2 to the left by arg3 bytes
	pshufb	%xmm0, %xmm2

	# shift xmm7 to the right by 16-arg3 bytes
	pxor	mask1(%rip), %xmm0
	pshufb	%xmm0, %xmm7
	pblendvb	%xmm2, %xmm1	#xmm0 is implicit

	# fold 16 Bytes
	movdqa	%xmm1, %xmm2
	movdqa	%xmm7, %xmm8
	pclmulqdq	$0x11, %xmm10, %xmm7
	pclmulqdq	$0x0 , %xmm10, %xmm8
	pxor	%xmm8, %xmm7
	pxor	%xmm2, %xmm7

_128_done:
	# compute crc of a 128-bit value
	movdqa	rk5(%rip), %xmm10	# rk5 and rk6 in xmm10
	movdqa	%xmm7, %xmm0

	#64b fold
	pclmulqdq	$0x1, %xmm10, %xmm7
	pslldq	$8   ,  %xmm0
	pxor	%xmm0,  %xmm7

	#32b fold
	movdqa	%xmm7, %xmm0

	pand	mask2(%rip), %xmm0

	psrldq	$12, %xmm7
	pclmulqdq	$0x10, %xmm10, %xmm7
	pxor	%xmm0, %xmm7

	#barrett reduction
_barrett:
	movdqa	rk7(%rip), %xmm10	# rk7 and rk8 in xmm10
	movdqa	%xmm7, %xmm0
	pclmulqdq	$0x01, %xmm10, %xmm7
	pslldq	$4, %xmm7
	pclmulqdq	$0x11, %xmm10, %xmm7

	pslldq	$4, %xmm7
	pxor	%xmm0, %xmm7
	pextrd	$1, %xmm7, %eax

_cleanup:
	# scale the result back to 16 bits
	shr	$16, %eax
	mov     %rcx, %rsp
	ret

########################################################################

.align 16
_less_than_128:

	# check if there is enough buffer to be able to fold 16B at a time
	cmp	$32, arg3
	jl	_less_than_32
	movdqa  SHUF_MASK(%rip), %xmm11

	# now if there is, load the constants
	movdqa	rk1(%rip), %xmm10	# rk1 and rk2 in xmm10

	movd	arg1_low32, %xmm0	# get the initial crc value
	pslldq	$12, %xmm0	# align it to its correct place
	movdqu	(arg2), %xmm7	# load the plaintext
	pshufb	%xmm11, %xmm7	# byte-reflect the plaintext
	pxor	%xmm0, %xmm7


	# update the buffer pointer
	add	$16, arg2

	# update the counter. subtract 32 instead of 16 to save one
	# instruction from the loop
	sub	$32, arg3

	jmp	_16B_reduction_loop


.align 16
_less_than_32:
	# mov initial crc to the return value. this is necessary for
	# zero-length buffers.
	mov	arg1_low32, %eax
	test	arg3, arg3
	je	_cleanup

	movdqa  SHUF_MASK(%rip), %xmm11

	movd	arg1_low32, %xmm0	# get the initial crc value
	pslldq	$12, %xmm0	# align it to its correct place

	cmp	$16, arg3
	je	_exact_16_left
	jl	_less_than_16_left

	movdqu	(arg2), %xmm7	# load the plaintext
	pshufb	%xmm11, %xmm7	# byte-reflect the plaintext
	pxor	%xmm0 , %xmm7	# xor the initial crc value
	add	$16, arg2
	sub	$16, arg3
	movdqa	rk1(%rip), %xmm10	# rk1 and rk2 in xmm10
	jmp	_get_last_two_xmms


.align 16
_less_than_16_left:
	# use stack space to load data less than 16 bytes, zero-out
	# the 16B in memory first.

	pxor	%xmm1, %xmm1
	mov	%rsp, %r11
	movdqa	%xmm1, (%r11)

	cmp	$4, arg3
	jl	_only_less_than_4

	# backup the counter value
	mov	arg3, %r9
	cmp	$8, arg3
	jl	_less_than_8_left

	# load 8 Bytes
	mov	(arg2), %rax
	mov	%rax, (%r11)
	add	$8, %r11
	sub	$8, arg3
	add	$8, arg2
_less_than_8_left:

	cmp	$4, arg3
	jl	_less_than_4_left

	# load 4 Bytes
	mov	(arg2), %eax
	mov	%eax, (%r11)
	add	$4, %r11
	sub	$4, arg3
	add	$4, arg2
_less_than_4_left:

	cmp	$2, arg3
	jl	_less_than_2_left

	# load 2 Bytes
	mov	(arg2), %ax
	mov	%ax, (%r11)
	add	$2, %r11
	sub	$2, arg3
	add	$2, arg2
_less_than_2_left:
	cmp     $1, arg3
        jl      _zero_left

	# load 1 Byte
	mov	(arg2), %al
	mov	%al, (%r11)
_zero_left:
	movdqa	(%rsp), %xmm7
	pshufb	%xmm11, %xmm7
	pxor	%xmm0 , %xmm7	# xor the initial crc value

	# shl r9, 4
	lea	pshufb_shf_table+16(%rip), %rax
	sub	%r9, %rax
	movdqu	(%rax), %xmm0
	pxor	mask1(%rip), %xmm0

	pshufb	%xmm0, %xmm7
	jmp	_128_done

.align 16
_exact_16_left:
	movdqu	(arg2), %xmm7
	pshufb	%xmm11, %xmm7
	pxor	%xmm0 , %xmm7   # xor the initial crc value

	jmp	_128_done

_only_less_than_4:
	cmp	$3, arg3
	jl	_only_less_than_3

	# load 3 Bytes
	mov	(arg2), %al
	mov	%al, (%r11)

	mov	1(arg2), %al
	mov	%al, 1(%r11)

	mov	2(arg2), %al
	mov	%al, 2(%r11)

	movdqa	 (%rsp), %xmm7
	pshufb	 %xmm11, %xmm7
	pxor	 %xmm0 , %xmm7  # xor the initial crc value

	psrldq	$5, %xmm7

	jmp	_barrett
_only_less_than_3:
	cmp	$2, arg3
	jl	_only_less_than_2

	# load 2 Bytes
	mov	(arg2), %al
	mov	%al, (%r11)

	mov	1(arg2), %al
	mov	%al, 1(%r11)

	movdqa	(%rsp), %xmm7
	pshufb	%xmm11, %xmm7
	pxor	%xmm0 , %xmm7   # xor the initial crc value

	psrldq	$6, %xmm7

	jmp	_barrett
_only_less_than_2:

	# load 1 Byte
	mov	(arg2), %al
	mov	%al, (%r11)

	movdqa	(%rsp), %xmm7
	pshufb	%xmm11, %xmm7
	pxor	%xmm0 , %xmm7   # xor the initial crc value

	psrldq	$7, %xmm7

	jmp	_barrett

ENDPROC(crc_t10dif_pcl)

.data

# precomputed constants
# these constants are precomputed from the poly:
# 0x8bb70000 (0x8bb7 scaled to 32 bits)
.align 16
# Q = 0x18BB70000
# rk1 = 2^(32*3) mod Q << 32
# rk2 = 2^(32*5) mod Q << 32
# rk3 = 2^(32*15) mod Q << 32
# rk4 = 2^(32*17) mod Q << 32
# rk5 = 2^(32*3) mod Q << 32
# rk6 = 2^(32*2) mod Q << 32
# rk7 = floor(2^64/Q)
# rk8 = Q
rk1:
.quad 0x2d56000000000000
rk2:
.quad 0x06df000000000000
rk3:
.quad 0x9d9d000000000000
rk4:
.quad 0x7cf5000000000000
rk5:
.quad 0x2d56000000000000
rk6:
.quad 0x1368000000000000
rk7:
.quad 0x00000001f65a57f8
rk8:
.quad 0x000000018bb70000

rk9:
.quad 0xceae000000000000
rk10:
.quad 0xbfd6000000000000
rk11:
.quad 0x1e16000000000000
rk12:
.quad 0x713c000000000000
rk13:
.quad 0xf7f9000000000000
rk14:
.quad 0x80a6000000000000
rk15:
.quad 0x044c000000000000
rk16:
.quad 0xe658000000000000
rk17:
.quad 0xad18000000000000
rk18:
.quad 0xa497000000000000
rk19:
.quad 0x6ee3000000000000
rk20:
.quad 0xe7b5000000000000



mask1:
.octa 0x80808080808080808080808080808080
mask2:
.octa 0x00000000FFFFFFFFFFFFFFFFFFFFFFFF

SHUF_MASK:
.octa 0x000102030405060708090A0B0C0D0E0F

pshufb_shf_table:
# use these values for shift constants for the pshufb instruction
# different alignments result in values as shown:
#	DDQ 0x008f8e8d8c8b8a898887868584838281 # shl 15 (16-1) / shr1
#	DDQ 0x01008f8e8d8c8b8a8988878685848382 # shl 14 (16-3) / shr2
#	DDQ 0x0201008f8e8d8c8b8a89888786858483 # shl 13 (16-4) / shr3
#	DDQ 0x030201008f8e8d8c8b8a898887868584 # shl 12 (16-4) / shr4
#	DDQ 0x04030201008f8e8d8c8b8a8988878685 # shl 11 (16-5) / shr5
#	DDQ 0x0504030201008f8e8d8c8b8a89888786 # shl 10 (16-6) / shr6
#	DDQ 0x060504030201008f8e8d8c8b8a898887 # shl 9  (16-7) / shr7
#	DDQ 0x07060504030201008f8e8d8c8b8a8988 # shl 8  (16-8) / shr8
#	DDQ 0x0807060504030201008f8e8d8c8b8a89 # shl 7  (16-9) / shr9
#	DDQ 0x090807060504030201008f8e8d8c8b8a # shl 6  (16-10) / shr10
#	DDQ 0x0a090807060504030201008f8e8d8c8b # shl 5  (16-11) / shr11
#	DDQ 0x0b0a090807060504030201008f8e8d8c # shl 4  (16-12) / shr12
#	DDQ 0x0c0b0a090807060504030201008f8e8d # shl 3  (16-13) / shr13
#	DDQ 0x0d0c0b0a090807060504030201008f8e # shl 2  (16-14) / shr14
#	DDQ 0x0e0d0c0b0a090807060504030201008f # shl 1  (16-15) / shr15
.octa 0x8f8e8d8c8b8a89888786858483828100
.octa 0x000e0d0c0b0a09080706050403020100
