/*
 * Multi-buffer SHA512 algorithm hash compute routine
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Contact Information:
 *     Megha Dey <megha.dey@linux.intel.com>
 *
 * BSD LICENSE
 *
 * Copyright(c) 2016 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

# code to compute quad SHA512 using AVX2
# use YMMs to tackle the larger digest size
# outer calling routine takes care of save and restore of XMM registers
# Logic designed/laid out by JDG

# Function clobbers: rax, rcx, rdx, rbx, rsi, rdi, r9-r15; ymm0-15
# Stack must be aligned to 32 bytes before call
# Linux clobbers: rax rbx rcx rsi r8 r9 r10 r11 r12
# Linux preserves: rcx rdx rdi rbp r13 r14 r15
# clobbers ymm0-15

#include <linux/linkage.h>
#include "sha512_mb_mgr_datastruct.S"

arg1 = %rdi
arg2 = %rsi

# Common definitions
STATE = arg1
INP_SIZE = arg2

IDX = %rax
ROUND = %rbx
TBL = %r8

inp0 = %r9
inp1 = %r10
inp2 = %r11
inp3 = %r12

a = %ymm0
b = %ymm1
c = %ymm2
d = %ymm3
e = %ymm4
f = %ymm5
g = %ymm6
h = %ymm7

a0 = %ymm8
a1 = %ymm9
a2 = %ymm10

TT0 = %ymm14
TT1 = %ymm13
TT2 = %ymm12
TT3 = %ymm11
TT4 = %ymm10
TT5 = %ymm9

T1 = %ymm14
TMP = %ymm15

# Define stack usage
STACK_SPACE1 = SZ4*16 + NUM_SHA512_DIGEST_WORDS*SZ4 + 24

#define VMOVPD	vmovupd
_digest = SZ4*16

# transpose r0, r1, r2, r3, t0, t1
# "transpose" data in {r0..r3} using temps {t0..t3}
# Input looks like: {r0 r1 r2 r3}
# r0 = {a7 a6 a5 a4 a3 a2 a1 a0}
# r1 = {b7 b6 b5 b4 b3 b2 b1 b0}
# r2 = {c7 c6 c5 c4 c3 c2 c1 c0}
# r3 = {d7 d6 d5 d4 d3 d2 d1 d0}
#
# output looks like: {t0 r1 r0 r3}
# t0 = {d1 d0 c1 c0 b1 b0 a1 a0}
# r1 = {d3 d2 c3 c2 b3 b2 a3 a2}
# r0 = {d5 d4 c5 c4 b5 b4 a5 a4}
# r3 = {d7 d6 c7 c6 b7 b6 a7 a6}

.macro TRANSPOSE r0 r1 r2 r3 t0 t1
	vshufps  $0x44, \r1, \r0, \t0 # t0 = {b5 b4 a5 a4   b1 b0 a1 a0}
        vshufps  $0xEE, \r1, \r0, \r0 # r0 = {b7 b6 a7 a6   b3 b2 a3 a2}
        vshufps  $0x44, \r3, \r2, \t1 # t1 = {d5 d4 c5 c4   d1 d0 c1 c0}
        vshufps  $0xEE, \r3, \r2, \r2 # r2 = {d7 d6 c7 c6   d3 d2 c3 c2}

	vperm2f128      $0x20, \r2, \r0, \r1  # h6...a6
        vperm2f128      $0x31, \r2, \r0, \r3  # h2...a2
        vperm2f128      $0x31, \t1, \t0, \r0  # h5...a5
        vperm2f128      $0x20, \t1, \t0, \t0  # h1...a1
.endm

.macro ROTATE_ARGS
TMP_ = h
h = g
g = f
f = e
e = d
d = c
c = b
b = a
a = TMP_
.endm

# PRORQ reg, imm, tmp
# packed-rotate-right-double
# does a rotate by doing two shifts and an or
.macro _PRORQ reg imm tmp
	vpsllq	$(64-\imm),\reg,\tmp
	vpsrlq	$\imm,\reg, \reg
	vpor	\tmp,\reg, \reg
.endm

# non-destructive
# PRORQ_nd reg, imm, tmp, src
.macro _PRORQ_nd reg imm tmp src
	vpsllq	$(64-\imm), \src, \tmp
	vpsrlq	$\imm, \src, \reg
	vpor	\tmp, \reg, \reg
.endm

# PRORQ dst/src, amt
.macro PRORQ reg imm
	_PRORQ	\reg, \imm, TMP
.endm

# PRORQ_nd dst, src, amt
.macro PRORQ_nd reg tmp imm
	_PRORQ_nd	\reg, \imm, TMP, \tmp
.endm

#; arguments passed implicitly in preprocessor symbols i, a...h
.macro ROUND_00_15 _T1 i
	PRORQ_nd a0, e, (18-14)	# sig1: a0 = (e >> 4)

	vpxor   g, f, a2        # ch: a2 = f^g
        vpand   e,a2, a2                # ch: a2 = (f^g)&e
        vpxor   g, a2, a2               # a2 = ch

        PRORQ_nd        a1,e,41         # sig1: a1 = (e >> 25)

        offset = SZ4*(\i & 0xf)
        vmovdqu \_T1,offset(%rsp)
        vpaddq  (TBL,ROUND,1), \_T1, \_T1       # T1 = W + K
        vpxor   e,a0, a0        # sig1: a0 = e ^ (e >> 5)
        PRORQ   a0, 14           # sig1: a0 = (e >> 6) ^ (e >> 11)
        vpaddq  a2, h, h        # h = h + ch
        PRORQ_nd        a2,a,6  # sig0: a2 = (a >> 11)
        vpaddq  \_T1,h, h       # h = h + ch + W + K
        vpxor   a1, a0, a0      # a0 = sigma1
	vmovdqu a,\_T1
        PRORQ_nd        a1,a,39 # sig0: a1 = (a >> 22)
        vpxor   c, \_T1, \_T1      # maj: T1 = a^c
        add     $SZ4, ROUND     # ROUND++
        vpand   b, \_T1, \_T1   # maj: T1 = (a^c)&b
        vpaddq  a0, h, h
        vpaddq  h, d, d
        vpxor   a, a2, a2       # sig0: a2 = a ^ (a >> 11)
        PRORQ   a2,28            # sig0: a2 = (a >> 2) ^ (a >> 13)
        vpxor   a1, a2, a2      # a2 = sig0
        vpand   c, a, a1        # maj: a1 = a&c
        vpor    \_T1, a1, a1    # a1 = maj
        vpaddq  a1, h, h        # h = h + ch + W + K + maj
        vpaddq  a2, h, h        # h = h + ch + W + K + maj + sigma0
        ROTATE_ARGS
.endm


#; arguments passed implicitly in preprocessor symbols i, a...h
.macro ROUND_16_XX _T1 i
	vmovdqu SZ4*((\i-15)&0xf)(%rsp), \_T1
        vmovdqu SZ4*((\i-2)&0xf)(%rsp), a1
        vmovdqu \_T1, a0
        PRORQ   \_T1,7
        vmovdqu a1, a2
        PRORQ   a1,42
        vpxor   a0, \_T1, \_T1
        PRORQ   \_T1, 1
        vpxor   a2, a1, a1
        PRORQ   a1, 19
        vpsrlq  $7, a0, a0
        vpxor   a0, \_T1, \_T1
        vpsrlq  $6, a2, a2
        vpxor   a2, a1, a1
        vpaddq  SZ4*((\i-16)&0xf)(%rsp), \_T1, \_T1
        vpaddq  SZ4*((\i-7)&0xf)(%rsp), a1, a1
        vpaddq  a1, \_T1, \_T1

        ROUND_00_15 \_T1,\i
.endm


# void sha512_x4_avx2(void *STATE, const int INP_SIZE)
# arg 1 : STATE    : pointer to input data
# arg 2 : INP_SIZE : size of data in blocks (assumed >= 1)
ENTRY(sha512_x4_avx2)
	# general registers preserved in outer calling routine
	# outer calling routine saves all the XMM registers
	# save callee-saved clobbered registers to comply with C function ABI
	push    %r12
	push    %r13
	push    %r14
	push    %r15

	sub     $STACK_SPACE1, %rsp

        # Load the pre-transposed incoming digest.
        vmovdqu 0*SHA512_DIGEST_ROW_SIZE(STATE),a
        vmovdqu 1*SHA512_DIGEST_ROW_SIZE(STATE),b
        vmovdqu 2*SHA512_DIGEST_ROW_SIZE(STATE),c
        vmovdqu 3*SHA512_DIGEST_ROW_SIZE(STATE),d
        vmovdqu 4*SHA512_DIGEST_ROW_SIZE(STATE),e
        vmovdqu 5*SHA512_DIGEST_ROW_SIZE(STATE),f
        vmovdqu 6*SHA512_DIGEST_ROW_SIZE(STATE),g
        vmovdqu 7*SHA512_DIGEST_ROW_SIZE(STATE),h

        lea     K512_4(%rip),TBL

        # load the address of each of the 4 message lanes
        # getting ready to transpose input onto stack
        mov     _data_ptr+0*PTR_SZ(STATE),inp0
        mov     _data_ptr+1*PTR_SZ(STATE),inp1
        mov     _data_ptr+2*PTR_SZ(STATE),inp2
        mov     _data_ptr+3*PTR_SZ(STATE),inp3

        xor     IDX, IDX
lloop:
        xor     ROUND, ROUND

	# save old digest
        vmovdqu a, _digest(%rsp)
        vmovdqu b, _digest+1*SZ4(%rsp)
        vmovdqu c, _digest+2*SZ4(%rsp)
        vmovdqu d, _digest+3*SZ4(%rsp)
        vmovdqu e, _digest+4*SZ4(%rsp)
        vmovdqu f, _digest+5*SZ4(%rsp)
        vmovdqu g, _digest+6*SZ4(%rsp)
        vmovdqu h, _digest+7*SZ4(%rsp)
        i = 0
.rep 4
	vmovdqu PSHUFFLE_BYTE_FLIP_MASK(%rip), TMP
        VMOVPD  i*32(inp0, IDX), TT2
        VMOVPD  i*32(inp1, IDX), TT1
        VMOVPD  i*32(inp2, IDX), TT4
        VMOVPD  i*32(inp3, IDX), TT3
	TRANSPOSE	TT2, TT1, TT4, TT3, TT0, TT5
	vpshufb	TMP, TT0, TT0
	vpshufb	TMP, TT1, TT1
	vpshufb	TMP, TT2, TT2
	vpshufb	TMP, TT3, TT3
	ROUND_00_15	TT0,(i*4+0)
	ROUND_00_15	TT1,(i*4+1)
	ROUND_00_15	TT2,(i*4+2)
	ROUND_00_15	TT3,(i*4+3)
	i = (i+1)
.endr
        add     $128, IDX

        i = (i*4)

        jmp     Lrounds_16_xx
.align 16
Lrounds_16_xx:
.rep 16
        ROUND_16_XX     T1, i
        i = (i+1)
.endr
        cmp     $0xa00,ROUND
        jb      Lrounds_16_xx

	# add old digest
        vpaddq  _digest(%rsp), a, a
        vpaddq  _digest+1*SZ4(%rsp), b, b
        vpaddq  _digest+2*SZ4(%rsp), c, c
        vpaddq  _digest+3*SZ4(%rsp), d, d
        vpaddq  _digest+4*SZ4(%rsp), e, e
        vpaddq  _digest+5*SZ4(%rsp), f, f
        vpaddq  _digest+6*SZ4(%rsp), g, g
        vpaddq  _digest+7*SZ4(%rsp), h, h

        sub     $1, INP_SIZE  # unit is blocks
        jne     lloop

        # write back to memory (state object) the transposed digest
        vmovdqu a, 0*SHA512_DIGEST_ROW_SIZE(STATE)
        vmovdqu b, 1*SHA512_DIGEST_ROW_SIZE(STATE)
        vmovdqu c, 2*SHA512_DIGEST_ROW_SIZE(STATE)
        vmovdqu d, 3*SHA512_DIGEST_ROW_SIZE(STATE)
        vmovdqu e, 4*SHA512_DIGEST_ROW_SIZE(STATE)
        vmovdqu f, 5*SHA512_DIGEST_ROW_SIZE(STATE)
        vmovdqu g, 6*SHA512_DIGEST_ROW_SIZE(STATE)
        vmovdqu h, 7*SHA512_DIGEST_ROW_SIZE(STATE)

	# update input data pointers
	add     IDX, inp0
        mov     inp0, _data_ptr+0*PTR_SZ(STATE)
        add     IDX, inp1
        mov     inp1, _data_ptr+1*PTR_SZ(STATE)
        add     IDX, inp2
        mov     inp2, _data_ptr+2*PTR_SZ(STATE)
        add     IDX, inp3
        mov     inp3, _data_ptr+3*PTR_SZ(STATE)

	#;;;;;;;;;;;;;;;
	#; Postamble
	add $STACK_SPACE1, %rsp
	# restore callee-saved clobbered registers

	pop     %r15
	pop     %r14
	pop     %r13
	pop     %r12

	# outer calling routine restores XMM and other GP registers
	ret
ENDPROC(sha512_x4_avx2)

.data
.align 64
K512_4:
	.octa 0x428a2f98d728ae22428a2f98d728ae22,\
		0x428a2f98d728ae22428a2f98d728ae22
	.octa 0x7137449123ef65cd7137449123ef65cd,\
		0x7137449123ef65cd7137449123ef65cd
	.octa 0xb5c0fbcfec4d3b2fb5c0fbcfec4d3b2f,\
		0xb5c0fbcfec4d3b2fb5c0fbcfec4d3b2f
	.octa 0xe9b5dba58189dbbce9b5dba58189dbbc,\
		0xe9b5dba58189dbbce9b5dba58189dbbc
	.octa 0x3956c25bf348b5383956c25bf348b538,\
		0x3956c25bf348b5383956c25bf348b538
	.octa 0x59f111f1b605d01959f111f1b605d019,\
		0x59f111f1b605d01959f111f1b605d019
	.octa 0x923f82a4af194f9b923f82a4af194f9b,\
		0x923f82a4af194f9b923f82a4af194f9b
	.octa 0xab1c5ed5da6d8118ab1c5ed5da6d8118,\
		0xab1c5ed5da6d8118ab1c5ed5da6d8118
	.octa 0xd807aa98a3030242d807aa98a3030242,\
		0xd807aa98a3030242d807aa98a3030242
	.octa 0x12835b0145706fbe12835b0145706fbe,\
		0x12835b0145706fbe12835b0145706fbe
	.octa 0x243185be4ee4b28c243185be4ee4b28c,\
		0x243185be4ee4b28c243185be4ee4b28c
	.octa 0x550c7dc3d5ffb4e2550c7dc3d5ffb4e2,\
		0x550c7dc3d5ffb4e2550c7dc3d5ffb4e2
	.octa 0x72be5d74f27b896f72be5d74f27b896f,\
		0x72be5d74f27b896f72be5d74f27b896f
	.octa 0x80deb1fe3b1696b180deb1fe3b1696b1,\
		0x80deb1fe3b1696b180deb1fe3b1696b1
	.octa 0x9bdc06a725c712359bdc06a725c71235,\
		0x9bdc06a725c712359bdc06a725c71235
	.octa 0xc19bf174cf692694c19bf174cf692694,\
		0xc19bf174cf692694c19bf174cf692694
	.octa 0xe49b69c19ef14ad2e49b69c19ef14ad2,\
		0xe49b69c19ef14ad2e49b69c19ef14ad2
	.octa 0xefbe4786384f25e3efbe4786384f25e3,\
		0xefbe4786384f25e3efbe4786384f25e3
	.octa 0x0fc19dc68b8cd5b50fc19dc68b8cd5b5,\
		0x0fc19dc68b8cd5b50fc19dc68b8cd5b5
	.octa 0x240ca1cc77ac9c65240ca1cc77ac9c65,\
		0x240ca1cc77ac9c65240ca1cc77ac9c65
	.octa 0x2de92c6f592b02752de92c6f592b0275,\
		0x2de92c6f592b02752de92c6f592b0275
	.octa 0x4a7484aa6ea6e4834a7484aa6ea6e483,\
		0x4a7484aa6ea6e4834a7484aa6ea6e483
	.octa 0x5cb0a9dcbd41fbd45cb0a9dcbd41fbd4,\
		0x5cb0a9dcbd41fbd45cb0a9dcbd41fbd4
	.octa 0x76f988da831153b576f988da831153b5,\
		0x76f988da831153b576f988da831153b5
	.octa 0x983e5152ee66dfab983e5152ee66dfab,\
		0x983e5152ee66dfab983e5152ee66dfab
	.octa 0xa831c66d2db43210a831c66d2db43210,\
		0xa831c66d2db43210a831c66d2db43210
	.octa 0xb00327c898fb213fb00327c898fb213f,\
		0xb00327c898fb213fb00327c898fb213f
	.octa 0xbf597fc7beef0ee4bf597fc7beef0ee4,\
		0xbf597fc7beef0ee4bf597fc7beef0ee4
	.octa 0xc6e00bf33da88fc2c6e00bf33da88fc2,\
		0xc6e00bf33da88fc2c6e00bf33da88fc2
	.octa 0xd5a79147930aa725d5a79147930aa725,\
		0xd5a79147930aa725d5a79147930aa725
	.octa 0x06ca6351e003826f06ca6351e003826f,\
		0x06ca6351e003826f06ca6351e003826f
	.octa 0x142929670a0e6e70142929670a0e6e70,\
		0x142929670a0e6e70142929670a0e6e70
	.octa 0x27b70a8546d22ffc27b70a8546d22ffc,\
		0x27b70a8546d22ffc27b70a8546d22ffc
	.octa 0x2e1b21385c26c9262e1b21385c26c926,\
		0x2e1b21385c26c9262e1b21385c26c926
	.octa 0x4d2c6dfc5ac42aed4d2c6dfc5ac42aed,\
		0x4d2c6dfc5ac42aed4d2c6dfc5ac42aed
	.octa 0x53380d139d95b3df53380d139d95b3df,\
		0x53380d139d95b3df53380d139d95b3df
	.octa 0x650a73548baf63de650a73548baf63de,\
		0x650a73548baf63de650a73548baf63de
	.octa 0x766a0abb3c77b2a8766a0abb3c77b2a8,\
		0x766a0abb3c77b2a8766a0abb3c77b2a8
	.octa 0x81c2c92e47edaee681c2c92e47edaee6,\
		0x81c2c92e47edaee681c2c92e47edaee6
	.octa 0x92722c851482353b92722c851482353b,\
		0x92722c851482353b92722c851482353b
	.octa 0xa2bfe8a14cf10364a2bfe8a14cf10364,\
		0xa2bfe8a14cf10364a2bfe8a14cf10364
	.octa 0xa81a664bbc423001a81a664bbc423001,\
		0xa81a664bbc423001a81a664bbc423001
	.octa 0xc24b8b70d0f89791c24b8b70d0f89791,\
		0xc24b8b70d0f89791c24b8b70d0f89791
	.octa 0xc76c51a30654be30c76c51a30654be30,\
		0xc76c51a30654be30c76c51a30654be30
	.octa 0xd192e819d6ef5218d192e819d6ef5218,\
		0xd192e819d6ef5218d192e819d6ef5218
	.octa 0xd69906245565a910d69906245565a910,\
		0xd69906245565a910d69906245565a910
	.octa 0xf40e35855771202af40e35855771202a,\
		0xf40e35855771202af40e35855771202a
	.octa 0x106aa07032bbd1b8106aa07032bbd1b8,\
		0x106aa07032bbd1b8106aa07032bbd1b8
	.octa 0x19a4c116b8d2d0c819a4c116b8d2d0c8,\
		0x19a4c116b8d2d0c819a4c116b8d2d0c8
	.octa 0x1e376c085141ab531e376c085141ab53,\
		0x1e376c085141ab531e376c085141ab53
	.octa 0x2748774cdf8eeb992748774cdf8eeb99,\
		0x2748774cdf8eeb992748774cdf8eeb99
	.octa 0x34b0bcb5e19b48a834b0bcb5e19b48a8,\
		0x34b0bcb5e19b48a834b0bcb5e19b48a8
	.octa 0x391c0cb3c5c95a63391c0cb3c5c95a63,\
		0x391c0cb3c5c95a63391c0cb3c5c95a63
	.octa 0x4ed8aa4ae3418acb4ed8aa4ae3418acb,\
		0x4ed8aa4ae3418acb4ed8aa4ae3418acb
	.octa 0x5b9cca4f7763e3735b9cca4f7763e373,\
		0x5b9cca4f7763e3735b9cca4f7763e373
	.octa 0x682e6ff3d6b2b8a3682e6ff3d6b2b8a3,\
		0x682e6ff3d6b2b8a3682e6ff3d6b2b8a3
	.octa 0x748f82ee5defb2fc748f82ee5defb2fc,\
		0x748f82ee5defb2fc748f82ee5defb2fc
	.octa 0x78a5636f43172f6078a5636f43172f60,\
		0x78a5636f43172f6078a5636f43172f60
	.octa 0x84c87814a1f0ab7284c87814a1f0ab72,\
		0x84c87814a1f0ab7284c87814a1f0ab72
	.octa 0x8cc702081a6439ec8cc702081a6439ec,\
		0x8cc702081a6439ec8cc702081a6439ec
	.octa 0x90befffa23631e2890befffa23631e28,\
		0x90befffa23631e2890befffa23631e28
	.octa 0xa4506cebde82bde9a4506cebde82bde9,\
		0xa4506cebde82bde9a4506cebde82bde9
	.octa 0xbef9a3f7b2c67915bef9a3f7b2c67915,\
		0xbef9a3f7b2c67915bef9a3f7b2c67915
	.octa 0xc67178f2e372532bc67178f2e372532b,\
		0xc67178f2e372532bc67178f2e372532b
	.octa 0xca273eceea26619cca273eceea26619c,\
		0xca273eceea26619cca273eceea26619c
	.octa 0xd186b8c721c0c207d186b8c721c0c207,\
		0xd186b8c721c0c207d186b8c721c0c207
	.octa 0xeada7dd6cde0eb1eeada7dd6cde0eb1e,\
		0xeada7dd6cde0eb1eeada7dd6cde0eb1e
	.octa 0xf57d4f7fee6ed178f57d4f7fee6ed178,\
		0xf57d4f7fee6ed178f57d4f7fee6ed178
	.octa 0x06f067aa72176fba06f067aa72176fba,\
		0x06f067aa72176fba06f067aa72176fba
	.octa 0x0a637dc5a2c898a60a637dc5a2c898a6,\
		0x0a637dc5a2c898a60a637dc5a2c898a6
	.octa 0x113f9804bef90dae113f9804bef90dae,\
		0x113f9804bef90dae113f9804bef90dae
	.octa 0x1b710b35131c471b1b710b35131c471b,\
		0x1b710b35131c471b1b710b35131c471b
	.octa 0x28db77f523047d8428db77f523047d84,\
		0x28db77f523047d8428db77f523047d84
	.octa 0x32caab7b40c7249332caab7b40c72493,\
		0x32caab7b40c7249332caab7b40c72493
	.octa 0x3c9ebe0a15c9bebc3c9ebe0a15c9bebc,\
		0x3c9ebe0a15c9bebc3c9ebe0a15c9bebc
	.octa 0x431d67c49c100d4c431d67c49c100d4c,\
		0x431d67c49c100d4c431d67c49c100d4c
	.octa 0x4cc5d4becb3e42b64cc5d4becb3e42b6,\
		0x4cc5d4becb3e42b64cc5d4becb3e42b6
	.octa 0x597f299cfc657e2a597f299cfc657e2a,\
		0x597f299cfc657e2a597f299cfc657e2a
	.octa 0x5fcb6fab3ad6faec5fcb6fab3ad6faec,\
		0x5fcb6fab3ad6faec5fcb6fab3ad6faec
	.octa 0x6c44198c4a4758176c44198c4a475817,\
		0x6c44198c4a4758176c44198c4a475817

PSHUFFLE_BYTE_FLIP_MASK: .octa 0x08090a0b0c0d0e0f0001020304050607
                         .octa 0x18191a1b1c1d1e1f1011121314151617
