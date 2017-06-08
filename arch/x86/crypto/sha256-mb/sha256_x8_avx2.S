/*
 * Multi-buffer SHA256 algorithm hash compute routine
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 *  Copyright(c) 2016 Intel Corporation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of version 2 of the GNU General Public License as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  Contact Information:
 *	Megha Dey <megha.dey@linux.intel.com>
 *
 *  BSD LICENSE
 *
 *  Copyright(c) 2016 Intel Corporation.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in
 *      the documentation and/or other materials provided with the
 *      distribution.
 *    * Neither the name of Intel Corporation nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/linkage.h>
#include "sha256_mb_mgr_datastruct.S"

## code to compute oct SHA256 using SSE-256
## outer calling routine takes care of save and restore of XMM registers
## Logic designed/laid out by JDG

## Function clobbers: rax, rcx, rdx,   rbx, rsi, rdi, r9-r15; %ymm0-15
## Linux clobbers:    rax rbx rcx rdx rsi            r9 r10 r11 r12 r13 r14 r15
## Linux preserves:                       rdi rbp r8
##
## clobbers %ymm0-15

arg1 = %rdi
arg2 = %rsi
reg3 = %rcx
reg4 = %rdx

# Common definitions
STATE = arg1
INP_SIZE = arg2

IDX = %rax
ROUND = %rbx
TBL = reg3

inp0 = %r9
inp1 = %r10
inp2 = %r11
inp3 = %r12
inp4 = %r13
inp5 = %r14
inp6 = %r15
inp7 = reg4

a = %ymm0
b = %ymm1
c = %ymm2
d = %ymm3
e = %ymm4
f = %ymm5
g = %ymm6
h = %ymm7

T1 = %ymm8

a0 = %ymm12
a1 = %ymm13
a2 = %ymm14
TMP = %ymm15
TMP0 = %ymm6
TMP1 = %ymm7

TT0 = %ymm8
TT1 = %ymm9
TT2 = %ymm10
TT3 = %ymm11
TT4 = %ymm12
TT5 = %ymm13
TT6 = %ymm14
TT7 = %ymm15

# Define stack usage

# Assume stack aligned to 32 bytes before call
# Therefore FRAMESZ mod 32 must be 32-8 = 24

#define FRAMESZ	0x388

#define VMOVPS	vmovups

# TRANSPOSE8 r0, r1, r2, r3, r4, r5, r6, r7, t0, t1
# "transpose" data in {r0...r7} using temps {t0...t1}
# Input looks like: {r0 r1 r2 r3 r4 r5 r6 r7}
# r0 = {a7 a6 a5 a4   a3 a2 a1 a0}
# r1 = {b7 b6 b5 b4   b3 b2 b1 b0}
# r2 = {c7 c6 c5 c4   c3 c2 c1 c0}
# r3 = {d7 d6 d5 d4   d3 d2 d1 d0}
# r4 = {e7 e6 e5 e4   e3 e2 e1 e0}
# r5 = {f7 f6 f5 f4   f3 f2 f1 f0}
# r6 = {g7 g6 g5 g4   g3 g2 g1 g0}
# r7 = {h7 h6 h5 h4   h3 h2 h1 h0}
#
# Output looks like: {r0 r1 r2 r3 r4 r5 r6 r7}
# r0 = {h0 g0 f0 e0   d0 c0 b0 a0}
# r1 = {h1 g1 f1 e1   d1 c1 b1 a1}
# r2 = {h2 g2 f2 e2   d2 c2 b2 a2}
# r3 = {h3 g3 f3 e3   d3 c3 b3 a3}
# r4 = {h4 g4 f4 e4   d4 c4 b4 a4}
# r5 = {h5 g5 f5 e5   d5 c5 b5 a5}
# r6 = {h6 g6 f6 e6   d6 c6 b6 a6}
# r7 = {h7 g7 f7 e7   d7 c7 b7 a7}
#

.macro TRANSPOSE8 r0 r1 r2 r3 r4 r5 r6 r7 t0 t1
	# process top half (r0..r3) {a...d}
	vshufps	$0x44, \r1, \r0, \t0 # t0 = {b5 b4 a5 a4   b1 b0 a1 a0}
	vshufps	$0xEE, \r1, \r0, \r0 # r0 = {b7 b6 a7 a6   b3 b2 a3 a2}
	vshufps	$0x44, \r3, \r2, \t1 # t1 = {d5 d4 c5 c4   d1 d0 c1 c0}
	vshufps	$0xEE, \r3, \r2, \r2 # r2 = {d7 d6 c7 c6   d3 d2 c3 c2}
	vshufps	$0xDD, \t1, \t0, \r3 # r3 = {d5 c5 b5 a5   d1 c1 b1 a1}
	vshufps	$0x88, \r2, \r0, \r1 # r1 = {d6 c6 b6 a6   d2 c2 b2 a2}
	vshufps	$0xDD, \r2, \r0, \r0 # r0 = {d7 c7 b7 a7   d3 c3 b3 a3}
	vshufps	$0x88, \t1, \t0, \t0 # t0 = {d4 c4 b4 a4   d0 c0 b0 a0}

	# use r2 in place of t0
	# process bottom half (r4..r7) {e...h}
	vshufps	$0x44, \r5, \r4, \r2 # r2 = {f5 f4 e5 e4   f1 f0 e1 e0}
	vshufps	$0xEE, \r5, \r4, \r4 # r4 = {f7 f6 e7 e6   f3 f2 e3 e2}
	vshufps	$0x44, \r7, \r6, \t1 # t1 = {h5 h4 g5 g4   h1 h0 g1 g0}
	vshufps	$0xEE, \r7, \r6, \r6 # r6 = {h7 h6 g7 g6   h3 h2 g3 g2}
	vshufps	$0xDD, \t1, \r2, \r7 # r7 = {h5 g5 f5 e5   h1 g1 f1 e1}
	vshufps	$0x88, \r6, \r4, \r5 # r5 = {h6 g6 f6 e6   h2 g2 f2 e2}
	vshufps	$0xDD, \r6, \r4, \r4 # r4 = {h7 g7 f7 e7   h3 g3 f3 e3}
	vshufps	$0x88, \t1, \r2, \t1 # t1 = {h4 g4 f4 e4   h0 g0 f0 e0}

	vperm2f128	$0x13, \r1, \r5, \r6  # h6...a6
	vperm2f128	$0x02, \r1, \r5, \r2  # h2...a2
	vperm2f128	$0x13, \r3, \r7, \r5  # h5...a5
	vperm2f128	$0x02, \r3, \r7, \r1  # h1...a1
	vperm2f128	$0x13, \r0, \r4, \r7  # h7...a7
	vperm2f128	$0x02, \r0, \r4, \r3  # h3...a3
	vperm2f128	$0x13, \t0, \t1, \r4  # h4...a4
	vperm2f128	$0x02, \t0, \t1, \r0  # h0...a0

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

.macro _PRORD reg imm tmp
	vpslld	$(32-\imm),\reg,\tmp
	vpsrld	$\imm,\reg, \reg
	vpor	\tmp,\reg, \reg
.endm

# PRORD_nd reg, imm, tmp, src
.macro _PRORD_nd reg imm tmp src
	vpslld	$(32-\imm), \src, \tmp
	vpsrld	$\imm, \src, \reg
	vpor	\tmp, \reg, \reg
.endm

# PRORD dst/src, amt
.macro PRORD reg imm
	_PRORD	\reg,\imm,TMP
.endm

# PRORD_nd dst, src, amt
.macro PRORD_nd reg tmp imm
	_PRORD_nd	\reg, \imm, TMP, \tmp
.endm

# arguments passed implicitly in preprocessor symbols i, a...h
.macro ROUND_00_15 _T1 i
	PRORD_nd	a0,e,5	# sig1: a0 = (e >> 5)

	vpxor	g, f, a2	# ch: a2 = f^g
	vpand	e,a2, a2	# ch: a2 = (f^g)&e
	vpxor	g, a2, a2	# a2 = ch

	PRORD_nd	a1,e,25	# sig1: a1 = (e >> 25)

	vmovdqu	\_T1,(SZ8*(\i & 0xf))(%rsp)
	vpaddd	(TBL,ROUND,1), \_T1, \_T1	# T1 = W + K
	vpxor	e,a0, a0	# sig1: a0 = e ^ (e >> 5)
	PRORD	a0, 6		# sig1: a0 = (e >> 6) ^ (e >> 11)
	vpaddd	a2, h, h	# h = h + ch
	PRORD_nd	a2,a,11	# sig0: a2 = (a >> 11)
	vpaddd	\_T1,h, h 	# h = h + ch + W + K
	vpxor	a1, a0, a0	# a0 = sigma1
	PRORD_nd	a1,a,22	# sig0: a1 = (a >> 22)
	vpxor	c, a, \_T1	# maj: T1 = a^c
	add	$SZ8, ROUND	# ROUND++
	vpand	b, \_T1, \_T1	# maj: T1 = (a^c)&b
	vpaddd	a0, h, h
	vpaddd	h, d, d
	vpxor	a, a2, a2	# sig0: a2 = a ^ (a >> 11)
	PRORD	a2,2		# sig0: a2 = (a >> 2) ^ (a >> 13)
	vpxor	a1, a2, a2	# a2 = sig0
	vpand	c, a, a1	# maj: a1 = a&c
	vpor	\_T1, a1, a1 	# a1 = maj
	vpaddd	a1, h, h	# h = h + ch + W + K + maj
	vpaddd	a2, h, h	# h = h + ch + W + K + maj + sigma0
	ROTATE_ARGS
.endm

# arguments passed implicitly in preprocessor symbols i, a...h
.macro ROUND_16_XX _T1 i
	vmovdqu	(SZ8*((\i-15)&0xf))(%rsp), \_T1
	vmovdqu	(SZ8*((\i-2)&0xf))(%rsp), a1
	vmovdqu	\_T1, a0
	PRORD	\_T1,11
	vmovdqu	a1, a2
	PRORD	a1,2
	vpxor	a0, \_T1, \_T1
	PRORD	\_T1, 7
	vpxor	a2, a1, a1
	PRORD	a1, 17
	vpsrld	$3, a0, a0
	vpxor	a0, \_T1, \_T1
	vpsrld	$10, a2, a2
	vpxor	a2, a1, a1
	vpaddd	(SZ8*((\i-16)&0xf))(%rsp), \_T1, \_T1
	vpaddd	(SZ8*((\i-7)&0xf))(%rsp), a1, a1
	vpaddd	a1, \_T1, \_T1

	ROUND_00_15 \_T1,\i
.endm

# SHA256_ARGS:
#   UINT128 digest[8];  // transposed digests
#   UINT8  *data_ptr[4];

# void sha256_x8_avx2(SHA256_ARGS *args, UINT64 bytes);
# arg 1 : STATE : pointer to array of pointers to input data
# arg 2 : INP_SIZE  : size of input in blocks
	# general registers preserved in outer calling routine
	# outer calling routine saves all the XMM registers
	# save rsp, allocate 32-byte aligned for local variables
ENTRY(sha256_x8_avx2)

	# save callee-saved clobbered registers to comply with C function ABI
	push    %r12
	push    %r13
	push    %r14
	push    %r15

	mov	%rsp, IDX
	sub	$FRAMESZ, %rsp
	and	$~0x1F, %rsp
	mov	IDX, _rsp(%rsp)

	# Load the pre-transposed incoming digest.
	vmovdqu	0*SHA256_DIGEST_ROW_SIZE(STATE),a
	vmovdqu	1*SHA256_DIGEST_ROW_SIZE(STATE),b
	vmovdqu	2*SHA256_DIGEST_ROW_SIZE(STATE),c
	vmovdqu	3*SHA256_DIGEST_ROW_SIZE(STATE),d
	vmovdqu	4*SHA256_DIGEST_ROW_SIZE(STATE),e
	vmovdqu	5*SHA256_DIGEST_ROW_SIZE(STATE),f
	vmovdqu	6*SHA256_DIGEST_ROW_SIZE(STATE),g
	vmovdqu	7*SHA256_DIGEST_ROW_SIZE(STATE),h

	lea	K256_8(%rip),TBL

	# load the address of each of the 4 message lanes
	# getting ready to transpose input onto stack
	mov	_args_data_ptr+0*PTR_SZ(STATE),inp0
	mov	_args_data_ptr+1*PTR_SZ(STATE),inp1
	mov	_args_data_ptr+2*PTR_SZ(STATE),inp2
	mov	_args_data_ptr+3*PTR_SZ(STATE),inp3
	mov	_args_data_ptr+4*PTR_SZ(STATE),inp4
	mov	_args_data_ptr+5*PTR_SZ(STATE),inp5
	mov	_args_data_ptr+6*PTR_SZ(STATE),inp6
	mov	_args_data_ptr+7*PTR_SZ(STATE),inp7

	xor	IDX, IDX
lloop:
	xor	ROUND, ROUND

	# save old digest
	vmovdqu	a, _digest(%rsp)
	vmovdqu	b, _digest+1*SZ8(%rsp)
	vmovdqu	c, _digest+2*SZ8(%rsp)
	vmovdqu	d, _digest+3*SZ8(%rsp)
	vmovdqu	e, _digest+4*SZ8(%rsp)
	vmovdqu	f, _digest+5*SZ8(%rsp)
	vmovdqu	g, _digest+6*SZ8(%rsp)
	vmovdqu	h, _digest+7*SZ8(%rsp)
	i = 0
.rep 2
	VMOVPS	i*32(inp0, IDX), TT0
	VMOVPS	i*32(inp1, IDX), TT1
	VMOVPS	i*32(inp2, IDX), TT2
	VMOVPS	i*32(inp3, IDX), TT3
	VMOVPS	i*32(inp4, IDX), TT4
	VMOVPS	i*32(inp5, IDX), TT5
	VMOVPS	i*32(inp6, IDX), TT6
	VMOVPS	i*32(inp7, IDX), TT7
	vmovdqu	g, _ytmp(%rsp)
	vmovdqu	h, _ytmp+1*SZ8(%rsp)
	TRANSPOSE8	TT0, TT1, TT2, TT3, TT4, TT5, TT6, TT7,   TMP0, TMP1
	vmovdqu	PSHUFFLE_BYTE_FLIP_MASK(%rip), TMP1
	vmovdqu	_ytmp(%rsp), g
	vpshufb	TMP1, TT0, TT0
	vpshufb	TMP1, TT1, TT1
	vpshufb	TMP1, TT2, TT2
	vpshufb	TMP1, TT3, TT3
	vpshufb	TMP1, TT4, TT4
	vpshufb	TMP1, TT5, TT5
	vpshufb	TMP1, TT6, TT6
	vpshufb	TMP1, TT7, TT7
	vmovdqu	_ytmp+1*SZ8(%rsp), h
	vmovdqu	TT4, _ytmp(%rsp)
	vmovdqu	TT5, _ytmp+1*SZ8(%rsp)
	vmovdqu	TT6, _ytmp+2*SZ8(%rsp)
	vmovdqu	TT7, _ytmp+3*SZ8(%rsp)
	ROUND_00_15	TT0,(i*8+0)
	vmovdqu	_ytmp(%rsp), TT0
	ROUND_00_15	TT1,(i*8+1)
	vmovdqu	_ytmp+1*SZ8(%rsp), TT1
	ROUND_00_15	TT2,(i*8+2)
	vmovdqu	_ytmp+2*SZ8(%rsp), TT2
	ROUND_00_15	TT3,(i*8+3)
	vmovdqu	_ytmp+3*SZ8(%rsp), TT3
	ROUND_00_15	TT0,(i*8+4)
	ROUND_00_15	TT1,(i*8+5)
	ROUND_00_15	TT2,(i*8+6)
	ROUND_00_15	TT3,(i*8+7)
	i = (i+1)
.endr
	add	$64, IDX
	i = (i*8)

	jmp	Lrounds_16_xx
.align 16
Lrounds_16_xx:
.rep 16
	ROUND_16_XX	T1, i
	i = (i+1)
.endr

	cmp	$ROUNDS,ROUND
	jb	Lrounds_16_xx

	# add old digest
	vpaddd	_digest+0*SZ8(%rsp), a, a
	vpaddd	_digest+1*SZ8(%rsp), b, b
	vpaddd	_digest+2*SZ8(%rsp), c, c
	vpaddd	_digest+3*SZ8(%rsp), d, d
	vpaddd	_digest+4*SZ8(%rsp), e, e
	vpaddd	_digest+5*SZ8(%rsp), f, f
	vpaddd	_digest+6*SZ8(%rsp), g, g
	vpaddd	_digest+7*SZ8(%rsp), h, h

	sub	$1, INP_SIZE  # unit is blocks
	jne	lloop

	# write back to memory (state object) the transposed digest
	vmovdqu	a, 0*SHA256_DIGEST_ROW_SIZE(STATE)
	vmovdqu	b, 1*SHA256_DIGEST_ROW_SIZE(STATE)
	vmovdqu	c, 2*SHA256_DIGEST_ROW_SIZE(STATE)
	vmovdqu	d, 3*SHA256_DIGEST_ROW_SIZE(STATE)
	vmovdqu	e, 4*SHA256_DIGEST_ROW_SIZE(STATE)
	vmovdqu	f, 5*SHA256_DIGEST_ROW_SIZE(STATE)
	vmovdqu	g, 6*SHA256_DIGEST_ROW_SIZE(STATE)
	vmovdqu	h, 7*SHA256_DIGEST_ROW_SIZE(STATE)

	# update input pointers
	add	IDX, inp0
	mov	inp0, _args_data_ptr+0*8(STATE)
	add	IDX, inp1
	mov	inp1, _args_data_ptr+1*8(STATE)
	add	IDX, inp2
	mov	inp2, _args_data_ptr+2*8(STATE)
	add	IDX, inp3
	mov	inp3, _args_data_ptr+3*8(STATE)
	add	IDX, inp4
	mov	inp4, _args_data_ptr+4*8(STATE)
	add	IDX, inp5
	mov	inp5, _args_data_ptr+5*8(STATE)
	add	IDX, inp6
	mov	inp6, _args_data_ptr+6*8(STATE)
	add	IDX, inp7
	mov	inp7, _args_data_ptr+7*8(STATE)

	# Postamble
	mov	_rsp(%rsp), %rsp

	# restore callee-saved clobbered registers
	pop     %r15
	pop     %r14
	pop     %r13
	pop     %r12

	ret
ENDPROC(sha256_x8_avx2)

.section	.rodata.K256_8, "a", @progbits
.align 64
K256_8:
	.octa	0x428a2f98428a2f98428a2f98428a2f98
	.octa	0x428a2f98428a2f98428a2f98428a2f98
	.octa	0x71374491713744917137449171374491
	.octa	0x71374491713744917137449171374491
	.octa	0xb5c0fbcfb5c0fbcfb5c0fbcfb5c0fbcf
	.octa	0xb5c0fbcfb5c0fbcfb5c0fbcfb5c0fbcf
	.octa	0xe9b5dba5e9b5dba5e9b5dba5e9b5dba5
	.octa	0xe9b5dba5e9b5dba5e9b5dba5e9b5dba5
	.octa	0x3956c25b3956c25b3956c25b3956c25b
	.octa	0x3956c25b3956c25b3956c25b3956c25b
	.octa	0x59f111f159f111f159f111f159f111f1
	.octa	0x59f111f159f111f159f111f159f111f1
	.octa	0x923f82a4923f82a4923f82a4923f82a4
	.octa	0x923f82a4923f82a4923f82a4923f82a4
	.octa	0xab1c5ed5ab1c5ed5ab1c5ed5ab1c5ed5
	.octa	0xab1c5ed5ab1c5ed5ab1c5ed5ab1c5ed5
	.octa	0xd807aa98d807aa98d807aa98d807aa98
	.octa	0xd807aa98d807aa98d807aa98d807aa98
	.octa	0x12835b0112835b0112835b0112835b01
	.octa	0x12835b0112835b0112835b0112835b01
	.octa	0x243185be243185be243185be243185be
	.octa	0x243185be243185be243185be243185be
	.octa	0x550c7dc3550c7dc3550c7dc3550c7dc3
	.octa	0x550c7dc3550c7dc3550c7dc3550c7dc3
	.octa	0x72be5d7472be5d7472be5d7472be5d74
	.octa	0x72be5d7472be5d7472be5d7472be5d74
	.octa	0x80deb1fe80deb1fe80deb1fe80deb1fe
	.octa	0x80deb1fe80deb1fe80deb1fe80deb1fe
	.octa	0x9bdc06a79bdc06a79bdc06a79bdc06a7
	.octa	0x9bdc06a79bdc06a79bdc06a79bdc06a7
	.octa	0xc19bf174c19bf174c19bf174c19bf174
	.octa	0xc19bf174c19bf174c19bf174c19bf174
	.octa	0xe49b69c1e49b69c1e49b69c1e49b69c1
	.octa	0xe49b69c1e49b69c1e49b69c1e49b69c1
	.octa	0xefbe4786efbe4786efbe4786efbe4786
	.octa	0xefbe4786efbe4786efbe4786efbe4786
	.octa	0x0fc19dc60fc19dc60fc19dc60fc19dc6
	.octa	0x0fc19dc60fc19dc60fc19dc60fc19dc6
	.octa	0x240ca1cc240ca1cc240ca1cc240ca1cc
	.octa	0x240ca1cc240ca1cc240ca1cc240ca1cc
	.octa	0x2de92c6f2de92c6f2de92c6f2de92c6f
	.octa	0x2de92c6f2de92c6f2de92c6f2de92c6f
	.octa	0x4a7484aa4a7484aa4a7484aa4a7484aa
	.octa	0x4a7484aa4a7484aa4a7484aa4a7484aa
	.octa	0x5cb0a9dc5cb0a9dc5cb0a9dc5cb0a9dc
	.octa	0x5cb0a9dc5cb0a9dc5cb0a9dc5cb0a9dc
	.octa	0x76f988da76f988da76f988da76f988da
	.octa	0x76f988da76f988da76f988da76f988da
	.octa	0x983e5152983e5152983e5152983e5152
	.octa	0x983e5152983e5152983e5152983e5152
	.octa	0xa831c66da831c66da831c66da831c66d
	.octa	0xa831c66da831c66da831c66da831c66d
	.octa	0xb00327c8b00327c8b00327c8b00327c8
	.octa	0xb00327c8b00327c8b00327c8b00327c8
	.octa	0xbf597fc7bf597fc7bf597fc7bf597fc7
	.octa	0xbf597fc7bf597fc7bf597fc7bf597fc7
	.octa	0xc6e00bf3c6e00bf3c6e00bf3c6e00bf3
	.octa	0xc6e00bf3c6e00bf3c6e00bf3c6e00bf3
	.octa	0xd5a79147d5a79147d5a79147d5a79147
	.octa	0xd5a79147d5a79147d5a79147d5a79147
	.octa	0x06ca635106ca635106ca635106ca6351
	.octa	0x06ca635106ca635106ca635106ca6351
	.octa	0x14292967142929671429296714292967
	.octa	0x14292967142929671429296714292967
	.octa	0x27b70a8527b70a8527b70a8527b70a85
	.octa	0x27b70a8527b70a8527b70a8527b70a85
	.octa	0x2e1b21382e1b21382e1b21382e1b2138
	.octa	0x2e1b21382e1b21382e1b21382e1b2138
	.octa	0x4d2c6dfc4d2c6dfc4d2c6dfc4d2c6dfc
	.octa	0x4d2c6dfc4d2c6dfc4d2c6dfc4d2c6dfc
	.octa	0x53380d1353380d1353380d1353380d13
	.octa	0x53380d1353380d1353380d1353380d13
	.octa	0x650a7354650a7354650a7354650a7354
	.octa	0x650a7354650a7354650a7354650a7354
	.octa	0x766a0abb766a0abb766a0abb766a0abb
	.octa	0x766a0abb766a0abb766a0abb766a0abb
	.octa	0x81c2c92e81c2c92e81c2c92e81c2c92e
	.octa	0x81c2c92e81c2c92e81c2c92e81c2c92e
	.octa	0x92722c8592722c8592722c8592722c85
	.octa	0x92722c8592722c8592722c8592722c85
	.octa	0xa2bfe8a1a2bfe8a1a2bfe8a1a2bfe8a1
	.octa	0xa2bfe8a1a2bfe8a1a2bfe8a1a2bfe8a1
	.octa	0xa81a664ba81a664ba81a664ba81a664b
	.octa	0xa81a664ba81a664ba81a664ba81a664b
	.octa	0xc24b8b70c24b8b70c24b8b70c24b8b70
	.octa	0xc24b8b70c24b8b70c24b8b70c24b8b70
	.octa	0xc76c51a3c76c51a3c76c51a3c76c51a3
	.octa	0xc76c51a3c76c51a3c76c51a3c76c51a3
	.octa	0xd192e819d192e819d192e819d192e819
	.octa	0xd192e819d192e819d192e819d192e819
	.octa	0xd6990624d6990624d6990624d6990624
	.octa	0xd6990624d6990624d6990624d6990624
	.octa	0xf40e3585f40e3585f40e3585f40e3585
	.octa	0xf40e3585f40e3585f40e3585f40e3585
	.octa	0x106aa070106aa070106aa070106aa070
	.octa	0x106aa070106aa070106aa070106aa070
	.octa	0x19a4c11619a4c11619a4c11619a4c116
	.octa	0x19a4c11619a4c11619a4c11619a4c116
	.octa	0x1e376c081e376c081e376c081e376c08
	.octa	0x1e376c081e376c081e376c081e376c08
	.octa	0x2748774c2748774c2748774c2748774c
	.octa	0x2748774c2748774c2748774c2748774c
	.octa	0x34b0bcb534b0bcb534b0bcb534b0bcb5
	.octa	0x34b0bcb534b0bcb534b0bcb534b0bcb5
	.octa	0x391c0cb3391c0cb3391c0cb3391c0cb3
	.octa	0x391c0cb3391c0cb3391c0cb3391c0cb3
	.octa	0x4ed8aa4a4ed8aa4a4ed8aa4a4ed8aa4a
	.octa	0x4ed8aa4a4ed8aa4a4ed8aa4a4ed8aa4a
	.octa	0x5b9cca4f5b9cca4f5b9cca4f5b9cca4f
	.octa	0x5b9cca4f5b9cca4f5b9cca4f5b9cca4f
	.octa	0x682e6ff3682e6ff3682e6ff3682e6ff3
	.octa	0x682e6ff3682e6ff3682e6ff3682e6ff3
	.octa	0x748f82ee748f82ee748f82ee748f82ee
	.octa	0x748f82ee748f82ee748f82ee748f82ee
	.octa	0x78a5636f78a5636f78a5636f78a5636f
	.octa	0x78a5636f78a5636f78a5636f78a5636f
	.octa	0x84c8781484c8781484c8781484c87814
	.octa	0x84c8781484c8781484c8781484c87814
	.octa	0x8cc702088cc702088cc702088cc70208
	.octa	0x8cc702088cc702088cc702088cc70208
	.octa	0x90befffa90befffa90befffa90befffa
	.octa	0x90befffa90befffa90befffa90befffa
	.octa	0xa4506ceba4506ceba4506ceba4506ceb
	.octa	0xa4506ceba4506ceba4506ceba4506ceb
	.octa	0xbef9a3f7bef9a3f7bef9a3f7bef9a3f7
	.octa	0xbef9a3f7bef9a3f7bef9a3f7bef9a3f7
	.octa	0xc67178f2c67178f2c67178f2c67178f2
	.octa	0xc67178f2c67178f2c67178f2c67178f2

.section	.rodata.cst32.PSHUFFLE_BYTE_FLIP_MASK, "aM", @progbits, 32
.align 32
PSHUFFLE_BYTE_FLIP_MASK:
.octa 0x0c0d0e0f08090a0b0405060700010203
.octa 0x0c0d0e0f08090a0b0405060700010203

.section	.rodata.cst256.K256, "aM", @progbits, 256
.align 64
.global K256
K256:
	.int	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	.int	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	.int	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	.int	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	.int	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	.int	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	.int	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	.int	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	.int	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	.int	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	.int	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	.int	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	.int	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	.int	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	.int	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	.int	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
