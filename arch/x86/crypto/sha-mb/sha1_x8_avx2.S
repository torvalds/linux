/*
 * Multi-buffer SHA1 algorithm hash compute routine
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 *  Copyright(c) 2014 Intel Corporation.
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
 *      James Guilford <james.guilford@intel.com>
 *	Tim Chen <tim.c.chen@linux.intel.com>
 *
 *  BSD LICENSE
 *
 *  Copyright(c) 2014 Intel Corporation.
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
#include "sha1_mb_mgr_datastruct.S"

## code to compute oct SHA1 using SSE-256
## outer calling routine takes care of save and restore of XMM registers

## Function clobbers: rax, rcx, rdx,   rbx, rsi, rdi, r9-r15# ymm0-15
##
## Linux clobbers:    rax rbx rcx rdx rsi            r9 r10 r11 r12 r13 r14 r15
## Linux preserves:                       rdi rbp r8
##
## clobbers ymm0-15


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
	vshufps  $0x44, \r1, \r0, \t0 # t0 = {b5 b4 a5 a4   b1 b0 a1 a0}
	vshufps  $0xEE, \r1, \r0, \r0 # r0 = {b7 b6 a7 a6   b3 b2 a3 a2}
	vshufps  $0x44, \r3, \r2, \t1 # t1 = {d5 d4 c5 c4   d1 d0 c1 c0}
	vshufps  $0xEE, \r3, \r2, \r2 # r2 = {d7 d6 c7 c6   d3 d2 c3 c2}
	vshufps  $0xDD, \t1, \t0, \r3 # r3 = {d5 c5 b5 a5   d1 c1 b1 a1}
	vshufps  $0x88, \r2, \r0, \r1 # r1 = {d6 c6 b6 a6   d2 c2 b2 a2}
	vshufps  $0xDD, \r2, \r0, \r0 # r0 = {d7 c7 b7 a7   d3 c3 b3 a3}
	vshufps  $0x88, \t1, \t0, \t0 # t0 = {d4 c4 b4 a4   d0 c0 b0 a0}

	# use r2 in place of t0
	# process bottom half (r4..r7) {e...h}
	vshufps  $0x44, \r5, \r4, \r2 # r2 = {f5 f4 e5 e4   f1 f0 e1 e0}
	vshufps  $0xEE, \r5, \r4, \r4 # r4 = {f7 f6 e7 e6   f3 f2 e3 e2}
	vshufps  $0x44, \r7, \r6, \t1 # t1 = {h5 h4 g5 g4   h1 h0 g1 g0}
	vshufps  $0xEE, \r7, \r6, \r6 # r6 = {h7 h6 g7 g6   h3 h2 g3 g2}
	vshufps  $0xDD, \t1, \r2, \r7 # r7 = {h5 g5 f5 e5   h1 g1 f1 e1}
	vshufps  $0x88, \r6, \r4, \r5 # r5 = {h6 g6 f6 e6   h2 g2 f2 e2}
	vshufps  $0xDD, \r6, \r4, \r4 # r4 = {h7 g7 f7 e7   h3 g3 f3 e3}
	vshufps  $0x88, \t1, \r2, \t1 # t1 = {h4 g4 f4 e4   h0 g0 f0 e0}

	vperm2f128      $0x13, \r1, \r5, \r6  # h6...a6
	vperm2f128      $0x02, \r1, \r5, \r2  # h2...a2
	vperm2f128      $0x13, \r3, \r7, \r5  # h5...a5
	vperm2f128      $0x02, \r3, \r7, \r1  # h1...a1
	vperm2f128      $0x13, \r0, \r4, \r7  # h7...a7
	vperm2f128      $0x02, \r0, \r4, \r3  # h3...a3
	vperm2f128      $0x13, \t0, \t1, \r4  # h4...a4
	vperm2f128      $0x02, \t0, \t1, \r0  # h0...a0

.endm
##
## Magic functions defined in FIPS 180-1
##
# macro MAGIC_F0 F,B,C,D,T   ## F = (D ^ (B & (C ^ D)))
.macro MAGIC_F0 regF regB regC regD regT
    vpxor \regD, \regC, \regF
    vpand \regB, \regF, \regF
    vpxor \regD, \regF, \regF
.endm

# macro MAGIC_F1 F,B,C,D,T   ## F = (B ^ C ^ D)
.macro MAGIC_F1 regF regB regC regD regT
    vpxor  \regC, \regD, \regF
    vpxor  \regB, \regF, \regF
.endm

# macro MAGIC_F2 F,B,C,D,T   ## F = ((B & C) | (B & D) | (C & D))
.macro MAGIC_F2 regF regB regC regD regT
    vpor  \regC, \regB, \regF
    vpand \regC, \regB, \regT
    vpand \regD, \regF, \regF
    vpor  \regT, \regF, \regF
.endm

# macro MAGIC_F3 F,B,C,D,T   ## F = (B ^ C ^ D)
.macro MAGIC_F3 regF regB regC regD regT
    MAGIC_F1 \regF,\regB,\regC,\regD,\regT
.endm

# PROLD reg, imm, tmp
.macro PROLD reg imm tmp
	vpsrld  $(32-\imm), \reg, \tmp
	vpslld  $\imm, \reg, \reg
	vpor    \tmp, \reg, \reg
.endm

.macro PROLD_nd reg imm tmp src
	vpsrld  $(32-\imm), \src, \tmp
	vpslld  $\imm, \src, \reg
	vpor	\tmp, \reg, \reg
.endm

.macro SHA1_STEP_00_15 regA regB regC regD regE regT regF memW immCNT MAGIC
	vpaddd	\immCNT, \regE, \regE
	vpaddd	\memW*32(%rsp), \regE, \regE
	PROLD_nd \regT, 5, \regF, \regA
	vpaddd	\regT, \regE, \regE
	\MAGIC  \regF, \regB, \regC, \regD, \regT
        PROLD   \regB, 30, \regT
        vpaddd  \regF, \regE, \regE
.endm

.macro SHA1_STEP_16_79 regA regB regC regD regE regT regF memW immCNT MAGIC
	vpaddd	\immCNT, \regE, \regE
	offset = ((\memW - 14) & 15) * 32
	vmovdqu offset(%rsp), W14
	vpxor	W14, W16, W16
	offset = ((\memW -  8) & 15) * 32
	vpxor	offset(%rsp), W16, W16
	offset = ((\memW -  3) & 15) * 32
	vpxor	offset(%rsp), W16, W16
	vpsrld	$(32-1), W16, \regF
	vpslld	$1, W16, W16
	vpor	W16, \regF, \regF

	ROTATE_W

	offset = ((\memW - 0) & 15) * 32
	vmovdqu	\regF, offset(%rsp)
	vpaddd	\regF, \regE, \regE
	PROLD_nd \regT, 5, \regF, \regA
	vpaddd	\regT, \regE, \regE
	\MAGIC \regF,\regB,\regC,\regD,\regT      ## FUN  = MAGIC_Fi(B,C,D)
	PROLD   \regB,30, \regT
	vpaddd  \regF, \regE, \regE
.endm

########################################################################
########################################################################
########################################################################

## FRAMESZ plus pushes must be an odd multiple of 8
YMM_SAVE = (15-15)*32
FRAMESZ = 32*16 + YMM_SAVE
_YMM  =   FRAMESZ - YMM_SAVE

#define VMOVPS   vmovups

IDX  = %rax
inp0 = %r9
inp1 = %r10
inp2 = %r11
inp3 = %r12
inp4 = %r13
inp5 = %r14
inp6 = %r15
inp7 = %rcx
arg1 = %rdi
arg2 = %rsi
RSP_SAVE = %rdx

# ymm0 A
# ymm1 B
# ymm2 C
# ymm3 D
# ymm4 E
# ymm5         F       AA
# ymm6         T0      BB
# ymm7         T1      CC
# ymm8         T2      DD
# ymm9         T3      EE
# ymm10                T4      TMP
# ymm11                T5      FUN
# ymm12                T6      K
# ymm13                T7      W14
# ymm14                T8      W15
# ymm15                T9      W16


A  =     %ymm0
B  =     %ymm1
C  =     %ymm2
D  =     %ymm3
E  =     %ymm4
F  =     %ymm5
T0 =	 %ymm6
T1 =     %ymm7
T2 =     %ymm8
T3 =     %ymm9
T4 =     %ymm10
T5 =     %ymm11
T6 =     %ymm12
T7 =     %ymm13
T8  =     %ymm14
T9  =     %ymm15

AA  =     %ymm5
BB  =     %ymm6
CC  =     %ymm7
DD  =     %ymm8
EE  =     %ymm9
TMP =     %ymm10
FUN =     %ymm11
K   =     %ymm12
W14 =     %ymm13
W15 =     %ymm14
W16 =     %ymm15

.macro ROTATE_ARGS
 TMP_ = E
 E = D
 D = C
 C = B
 B = A
 A = TMP_
.endm

.macro ROTATE_W
TMP_  = W16
W16  = W15
W15  = W14
W14  = TMP_
.endm

# 8 streams x 5 32bit words per digest x 4 bytes per word
#define DIGEST_SIZE (8*5*4)

.align 32

# void sha1_x8_avx2(void **input_data, UINT128 *digest, UINT32 size)
# arg 1 : pointer to array[4] of pointer to input data
# arg 2 : size (in blocks) ;; assumed to be >= 1
#
ENTRY(sha1_x8_avx2)

	# save callee-saved clobbered registers to comply with C function ABI
	push	%r12
	push	%r13
	push	%r14
	push	%r15

	#save rsp
	mov	%rsp, RSP_SAVE
	sub     $FRAMESZ, %rsp

	#align rsp to 32 Bytes
	and	$~0x1F, %rsp

	## Initialize digests
	vmovdqu  0*32(arg1), A
	vmovdqu  1*32(arg1), B
	vmovdqu  2*32(arg1), C
	vmovdqu  3*32(arg1), D
	vmovdqu  4*32(arg1), E

	## transpose input onto stack
	mov     _data_ptr+0*8(arg1),inp0
	mov     _data_ptr+1*8(arg1),inp1
	mov     _data_ptr+2*8(arg1),inp2
	mov     _data_ptr+3*8(arg1),inp3
	mov     _data_ptr+4*8(arg1),inp4
	mov     _data_ptr+5*8(arg1),inp5
	mov     _data_ptr+6*8(arg1),inp6
	mov     _data_ptr+7*8(arg1),inp7

	xor     IDX, IDX
lloop:
	vmovdqu  PSHUFFLE_BYTE_FLIP_MASK(%rip), F
	I=0
.rep 2
	VMOVPS   (inp0, IDX), T0
	VMOVPS   (inp1, IDX), T1
	VMOVPS   (inp2, IDX), T2
	VMOVPS   (inp3, IDX), T3
	VMOVPS   (inp4, IDX), T4
	VMOVPS   (inp5, IDX), T5
	VMOVPS   (inp6, IDX), T6
	VMOVPS   (inp7, IDX), T7

	TRANSPOSE8       T0, T1, T2, T3, T4, T5, T6, T7, T8, T9
	vpshufb  F, T0, T0
	vmovdqu  T0, (I*8)*32(%rsp)
	vpshufb  F, T1, T1
	vmovdqu  T1, (I*8+1)*32(%rsp)
	vpshufb  F, T2, T2
	vmovdqu  T2, (I*8+2)*32(%rsp)
	vpshufb  F, T3, T3
	vmovdqu  T3, (I*8+3)*32(%rsp)
	vpshufb  F, T4, T4
	vmovdqu  T4, (I*8+4)*32(%rsp)
	vpshufb  F, T5, T5
	vmovdqu  T5, (I*8+5)*32(%rsp)
	vpshufb  F, T6, T6
	vmovdqu  T6, (I*8+6)*32(%rsp)
	vpshufb  F, T7, T7
	vmovdqu  T7, (I*8+7)*32(%rsp)
	add     $32, IDX
	I = (I+1)
.endr
	# save old digests
	vmovdqu  A,AA
	vmovdqu  B,BB
	vmovdqu  C,CC
	vmovdqu  D,DD
	vmovdqu  E,EE

##
## perform 0-79 steps
##
	vmovdqu  K00_19(%rip), K
## do rounds 0...15
	I = 0
.rep 16
	SHA1_STEP_00_15 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F0
	ROTATE_ARGS
	I = (I+1)
.endr

## do rounds 16...19
	vmovdqu  ((16 - 16) & 15) * 32 (%rsp), W16
	vmovdqu  ((16 - 15) & 15) * 32 (%rsp), W15
.rep 4
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F0
	ROTATE_ARGS
	I = (I+1)
.endr

## do rounds 20...39
	vmovdqu  K20_39(%rip), K
.rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F1
	ROTATE_ARGS
	I = (I+1)
.endr

## do rounds 40...59
	vmovdqu  K40_59(%rip), K
.rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F2
	ROTATE_ARGS
	I = (I+1)
.endr

## do rounds 60...79
	vmovdqu  K60_79(%rip), K
.rep 20
	SHA1_STEP_16_79 A,B,C,D,E, TMP,FUN, I, K, MAGIC_F3
	ROTATE_ARGS
	I = (I+1)
.endr

	vpaddd   AA,A,A
	vpaddd   BB,B,B
	vpaddd   CC,C,C
	vpaddd   DD,D,D
	vpaddd   EE,E,E

	sub     $1, arg2
	jne     lloop

	# write out digests
	vmovdqu  A, 0*32(arg1)
	vmovdqu  B, 1*32(arg1)
	vmovdqu  C, 2*32(arg1)
	vmovdqu  D, 3*32(arg1)
	vmovdqu  E, 4*32(arg1)

	# update input pointers
	add     IDX, inp0
	add     IDX, inp1
	add     IDX, inp2
	add     IDX, inp3
	add     IDX, inp4
	add     IDX, inp5
	add     IDX, inp6
	add     IDX, inp7
	mov     inp0, _data_ptr (arg1)
	mov     inp1, _data_ptr + 1*8(arg1)
	mov     inp2, _data_ptr + 2*8(arg1)
	mov     inp3, _data_ptr + 3*8(arg1)
	mov     inp4, _data_ptr + 4*8(arg1)
	mov     inp5, _data_ptr + 5*8(arg1)
	mov     inp6, _data_ptr + 6*8(arg1)
	mov     inp7, _data_ptr + 7*8(arg1)

	################
	## Postamble

	mov     RSP_SAVE, %rsp

	# restore callee-saved clobbered registers
	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12

	ret
ENDPROC(sha1_x8_avx2)


.data

.align 32
K00_19:
.octa 0x5A8279995A8279995A8279995A827999
.octa 0x5A8279995A8279995A8279995A827999
K20_39:
.octa 0x6ED9EBA16ED9EBA16ED9EBA16ED9EBA1
.octa 0x6ED9EBA16ED9EBA16ED9EBA16ED9EBA1
K40_59:
.octa 0x8F1BBCDC8F1BBCDC8F1BBCDC8F1BBCDC
.octa 0x8F1BBCDC8F1BBCDC8F1BBCDC8F1BBCDC
K60_79:
.octa 0xCA62C1D6CA62C1D6CA62C1D6CA62C1D6
.octa 0xCA62C1D6CA62C1D6CA62C1D6CA62C1D6
PSHUFFLE_BYTE_FLIP_MASK:
.octa 0x0c0d0e0f08090a0b0405060700010203
.octa 0x0c0d0e0f08090a0b0405060700010203
