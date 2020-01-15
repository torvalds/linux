/*
 *	Implement fast SHA-1 with AVX2 instructions. (x86_64)
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 Intel Corporation.
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
 * Ilya Albrekht <ilya.albrekht@intel.com>
 * Maxim Locktyukhin <maxim.locktyukhin@intel.com>
 * Ronen Zohar <ronen.zohar@intel.com>
 * Chandramouli Narayanan <mouli@linux.intel.com>
 *
 * BSD LICENSE
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the
 * distribution.
 * Neither the name of Intel Corporation nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
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
 *
 */

/*
 * SHA-1 implementation with Intel(R) AVX2 instruction set extensions.
 *
 *This implementation is based on the previous SSSE3 release:
 *Visit http://software.intel.com/en-us/articles/
 *and refer to improving-the-performance-of-the-secure-hash-algorithm-1/
 *
 *Updates 20-byte SHA-1 record at start of 'state', from 'input', for
 *even number of 'blocks' consecutive 64-byte blocks.
 *
 *extern "C" void sha1_transform_avx2(
 *	struct sha1_state *state, const u8* input, int blocks );
 */

#include <linux/linkage.h>

#define	CTX	%rdi	/* arg1 */
#define BUF	%rsi	/* arg2 */
#define CNT	%rdx	/* arg3 */

#define	REG_A	%ecx
#define	REG_B	%esi
#define	REG_C	%edi
#define	REG_D	%eax
#define	REG_E	%edx
#define	REG_TB	%ebx
#define	REG_TA	%r12d
#define	REG_RA	%rcx
#define	REG_RB	%rsi
#define	REG_RC	%rdi
#define	REG_RD	%rax
#define	REG_RE	%rdx
#define	REG_RTA	%r12
#define	REG_RTB	%rbx
#define	REG_T1	%r11d
#define	xmm_mov	vmovups
#define	avx2_zeroupper	vzeroupper
#define	RND_F1	1
#define	RND_F2	2
#define	RND_F3	3

.macro REGALLOC
	.set A, REG_A
	.set B, REG_B
	.set C, REG_C
	.set D, REG_D
	.set E, REG_E
	.set TB, REG_TB
	.set TA, REG_TA

	.set RA, REG_RA
	.set RB, REG_RB
	.set RC, REG_RC
	.set RD, REG_RD
	.set RE, REG_RE

	.set RTA, REG_RTA
	.set RTB, REG_RTB

	.set T1, REG_T1
.endm

#define HASH_PTR	%r9
#define BLOCKS_CTR	%r8
#define BUFFER_PTR	%r10
#define BUFFER_PTR2	%r13

#define PRECALC_BUF	%r14
#define WK_BUF		%r15

#define W_TMP		%xmm0
#define WY_TMP		%ymm0
#define WY_TMP2		%ymm9

# AVX2 variables
#define WY0		%ymm3
#define WY4		%ymm5
#define WY08		%ymm7
#define WY12		%ymm8
#define WY16		%ymm12
#define WY20		%ymm13
#define WY24		%ymm14
#define WY28		%ymm15

#define YMM_SHUFB_BSWAP	%ymm10

/*
 * Keep 2 iterations precalculated at a time:
 *    - 80 DWORDs per iteration * 2
 */
#define W_SIZE		(80*2*2 +16)

#define WK(t)	((((t) % 80) / 4)*32 + ( (t) % 4)*4 + ((t)/80)*16 )(WK_BUF)
#define PRECALC_WK(t)	((t)*2*2)(PRECALC_BUF)


.macro UPDATE_HASH  hash, val
	add	\hash, \val
	mov	\val, \hash
.endm

.macro PRECALC_RESET_WY
	.set WY_00, WY0
	.set WY_04, WY4
	.set WY_08, WY08
	.set WY_12, WY12
	.set WY_16, WY16
	.set WY_20, WY20
	.set WY_24, WY24
	.set WY_28, WY28
	.set WY_32, WY_00
.endm

.macro PRECALC_ROTATE_WY
	/* Rotate macros */
	.set WY_32, WY_28
	.set WY_28, WY_24
	.set WY_24, WY_20
	.set WY_20, WY_16
	.set WY_16, WY_12
	.set WY_12, WY_08
	.set WY_08, WY_04
	.set WY_04, WY_00
	.set WY_00, WY_32

	/* Define register aliases */
	.set WY, WY_00
	.set WY_minus_04, WY_04
	.set WY_minus_08, WY_08
	.set WY_minus_12, WY_12
	.set WY_minus_16, WY_16
	.set WY_minus_20, WY_20
	.set WY_minus_24, WY_24
	.set WY_minus_28, WY_28
	.set WY_minus_32, WY
.endm

.macro PRECALC_00_15
	.if (i == 0) # Initialize and rotate registers
		PRECALC_RESET_WY
		PRECALC_ROTATE_WY
	.endif

	/* message scheduling pre-compute for rounds 0-15 */
	.if   ((i & 7) == 0)
		/*
		 * blended AVX2 and ALU instruction scheduling
		 * 1 vector iteration per 8 rounds
		 */
		vmovdqu (i * 2)(BUFFER_PTR), W_TMP
	.elseif ((i & 7) == 1)
		vinsertf128 $1, ((i-1) * 2)(BUFFER_PTR2),\
			 WY_TMP, WY_TMP
	.elseif ((i & 7) == 2)
		vpshufb YMM_SHUFB_BSWAP, WY_TMP, WY
	.elseif ((i & 7) == 4)
		vpaddd  K_XMM + K_XMM_AR(%rip), WY, WY_TMP
	.elseif ((i & 7) == 7)
		vmovdqu  WY_TMP, PRECALC_WK(i&~7)

		PRECALC_ROTATE_WY
	.endif
.endm

.macro PRECALC_16_31
	/*
	 * message scheduling pre-compute for rounds 16-31
	 * calculating last 32 w[i] values in 8 XMM registers
	 * pre-calculate K+w[i] values and store to mem
	 * for later load by ALU add instruction
	 *
	 * "brute force" vectorization for rounds 16-31 only
	 * due to w[i]->w[i-3] dependency
	 */
	.if   ((i & 7) == 0)
		/*
		 * blended AVX2 and ALU instruction scheduling
		 * 1 vector iteration per 8 rounds
		 */
		/* w[i-14] */
		vpalignr	$8, WY_minus_16, WY_minus_12, WY
		vpsrldq	$4, WY_minus_04, WY_TMP               /* w[i-3] */
	.elseif ((i & 7) == 1)
		vpxor	WY_minus_08, WY, WY
		vpxor	WY_minus_16, WY_TMP, WY_TMP
	.elseif ((i & 7) == 2)
		vpxor	WY_TMP, WY, WY
		vpslldq	$12, WY, WY_TMP2
	.elseif ((i & 7) == 3)
		vpslld	$1, WY, WY_TMP
		vpsrld	$31, WY, WY
	.elseif ((i & 7) == 4)
		vpor	WY, WY_TMP, WY_TMP
		vpslld	$2, WY_TMP2, WY
	.elseif ((i & 7) == 5)
		vpsrld	$30, WY_TMP2, WY_TMP2
		vpxor	WY, WY_TMP, WY_TMP
	.elseif ((i & 7) == 7)
		vpxor	WY_TMP2, WY_TMP, WY
		vpaddd  K_XMM + K_XMM_AR(%rip), WY, WY_TMP
		vmovdqu	WY_TMP, PRECALC_WK(i&~7)

		PRECALC_ROTATE_WY
	.endif
.endm

.macro PRECALC_32_79
	/*
	 * in SHA-1 specification:
	 * w[i] = (w[i-3] ^ w[i-8]  ^ w[i-14] ^ w[i-16]) rol 1
	 * instead we do equal:
	 * w[i] = (w[i-6] ^ w[i-16] ^ w[i-28] ^ w[i-32]) rol 2
	 * allows more efficient vectorization
	 * since w[i]=>w[i-3] dependency is broken
	 */

	.if   ((i & 7) == 0)
	/*
	 * blended AVX2 and ALU instruction scheduling
	 * 1 vector iteration per 8 rounds
	 */
		vpalignr	$8, WY_minus_08, WY_minus_04, WY_TMP
	.elseif ((i & 7) == 1)
		/* W is W_minus_32 before xor */
		vpxor	WY_minus_28, WY, WY
	.elseif ((i & 7) == 2)
		vpxor	WY_minus_16, WY_TMP, WY_TMP
	.elseif ((i & 7) == 3)
		vpxor	WY_TMP, WY, WY
	.elseif ((i & 7) == 4)
		vpslld	$2, WY, WY_TMP
	.elseif ((i & 7) == 5)
		vpsrld	$30, WY, WY
		vpor	WY, WY_TMP, WY
	.elseif ((i & 7) == 7)
		vpaddd  K_XMM + K_XMM_AR(%rip), WY, WY_TMP
		vmovdqu	WY_TMP, PRECALC_WK(i&~7)

		PRECALC_ROTATE_WY
	.endif
.endm

.macro PRECALC r, s
	.set i, \r

	.if (i < 40)
		.set K_XMM, 32*0
	.elseif (i < 80)
		.set K_XMM, 32*1
	.elseif (i < 120)
		.set K_XMM, 32*2
	.else
		.set K_XMM, 32*3
	.endif

	.if (i<32)
		PRECALC_00_15	\s
	.elseif (i<64)
		PRECALC_16_31	\s
	.elseif (i < 160)
		PRECALC_32_79	\s
	.endif
.endm

.macro ROTATE_STATE
	.set T_REG, E
	.set E, D
	.set D, C
	.set C, B
	.set B, TB
	.set TB, A
	.set A, T_REG

	.set T_REG, RE
	.set RE, RD
	.set RD, RC
	.set RC, RB
	.set RB, RTB
	.set RTB, RA
	.set RA, T_REG
.endm

/* Macro relies on saved ROUND_Fx */

.macro RND_FUN f, r
	.if (\f == RND_F1)
		ROUND_F1	\r
	.elseif (\f == RND_F2)
		ROUND_F2	\r
	.elseif (\f == RND_F3)
		ROUND_F3	\r
	.endif
.endm

.macro RR r
	.set round_id, (\r % 80)

	.if (round_id == 0)        /* Precalculate F for first round */
		.set ROUND_FUNC, RND_F1
		mov	B, TB

		rorx	$(32-30), B, B    /* b>>>2 */
		andn	D, TB, T1
		and	C, TB
		xor	T1, TB
	.endif

	RND_FUN ROUND_FUNC, \r
	ROTATE_STATE

	.if   (round_id == 18)
		.set ROUND_FUNC, RND_F2
	.elseif (round_id == 38)
		.set ROUND_FUNC, RND_F3
	.elseif (round_id == 58)
		.set ROUND_FUNC, RND_F2
	.endif

	.set round_id, ( (\r+1) % 80)

	RND_FUN ROUND_FUNC, (\r+1)
	ROTATE_STATE
.endm

.macro ROUND_F1 r
	add	WK(\r), E

	andn	C, A, T1			/* ~b&d */
	lea	(RE,RTB), E		/* Add F from the previous round */

	rorx	$(32-5), A, TA		/* T2 = A >>> 5 */
	rorx	$(32-30),A, TB		/* b>>>2 for next round */

	PRECALC	(\r)			/* msg scheduling for next 2 blocks */

	/*
	 * Calculate F for the next round
	 * (b & c) ^ andn[b, d]
	 */
	and	B, A			/* b&c */
	xor	T1, A			/* F1 = (b&c) ^ (~b&d) */

	lea	(RE,RTA), E		/* E += A >>> 5 */
.endm

.macro ROUND_F2 r
	add	WK(\r), E
	lea	(RE,RTB), E		/* Add F from the previous round */

	/* Calculate F for the next round */
	rorx	$(32-5), A, TA		/* T2 = A >>> 5 */
	.if ((round_id) < 79)
		rorx	$(32-30), A, TB	/* b>>>2 for next round */
	.endif
	PRECALC	(\r)			/* msg scheduling for next 2 blocks */

	.if ((round_id) < 79)
		xor	B, A
	.endif

	add	TA, E			/* E += A >>> 5 */

	.if ((round_id) < 79)
		xor	C, A
	.endif
.endm

.macro ROUND_F3 r
	add	WK(\r), E
	PRECALC	(\r)			/* msg scheduling for next 2 blocks */

	lea	(RE,RTB), E		/* Add F from the previous round */

	mov	B, T1
	or	A, T1

	rorx	$(32-5), A, TA		/* T2 = A >>> 5 */
	rorx	$(32-30), A, TB		/* b>>>2 for next round */

	/* Calculate F for the next round
	 * (b and c) or (d and (b or c))
	 */
	and	C, T1
	and	B, A
	or	T1, A

	add	TA, E			/* E += A >>> 5 */

.endm

/* Add constant only if (%2 > %3) condition met (uses RTA as temp)
 * %1 + %2 >= %3 ? %4 : 0
 */
.macro ADD_IF_GE a, b, c, d
	mov     \a, RTA
	add     $\d, RTA
	cmp     $\c, \b
	cmovge  RTA, \a
.endm

/*
 * macro implements 80 rounds of SHA-1, for multiple blocks with s/w pipelining
 */
.macro SHA1_PIPELINED_MAIN_BODY

	REGALLOC

	mov	(HASH_PTR), A
	mov	4(HASH_PTR), B
	mov	8(HASH_PTR), C
	mov	12(HASH_PTR), D
	mov	16(HASH_PTR), E

	mov	%rsp, PRECALC_BUF
	lea	(2*4*80+32)(%rsp), WK_BUF

	# Precalc WK for first 2 blocks
	ADD_IF_GE BUFFER_PTR2, BLOCKS_CTR, 2, 64
	.set i, 0
	.rept    160
		PRECALC i
		.set i, i + 1
	.endr

	/* Go to next block if needed */
	ADD_IF_GE BUFFER_PTR, BLOCKS_CTR, 3, 128
	ADD_IF_GE BUFFER_PTR2, BLOCKS_CTR, 4, 128
	xchg	WK_BUF, PRECALC_BUF

	.align 32
_loop:
	/*
	 * code loops through more than one block
	 * we use K_BASE value as a signal of a last block,
	 * it is set below by: cmovae BUFFER_PTR, K_BASE
	 */
	test BLOCKS_CTR, BLOCKS_CTR
	jnz _begin
	.align 32
	jmp	_end
	.align 32
_begin:

	/*
	 * Do first block
	 * rounds: 0,2,4,6,8
	 */
	.set j, 0
	.rept 5
		RR	j
		.set j, j+2
	.endr

	jmp _loop0
_loop0:

	/*
	 * rounds:
	 * 10,12,14,16,18
	 * 20,22,24,26,28
	 * 30,32,34,36,38
	 * 40,42,44,46,48
	 * 50,52,54,56,58
	 */
	.rept 25
		RR	j
		.set j, j+2
	.endr

	/* Update Counter */
	sub $1, BLOCKS_CTR
	/* Move to the next block only if needed*/
	ADD_IF_GE BUFFER_PTR, BLOCKS_CTR, 4, 128
	/*
	 * rounds
	 * 60,62,64,66,68
	 * 70,72,74,76,78
	 */
	.rept 10
		RR	j
		.set j, j+2
	.endr

	UPDATE_HASH	(HASH_PTR), A
	UPDATE_HASH	4(HASH_PTR), TB
	UPDATE_HASH	8(HASH_PTR), C
	UPDATE_HASH	12(HASH_PTR), D
	UPDATE_HASH	16(HASH_PTR), E

	test	BLOCKS_CTR, BLOCKS_CTR
	jz	_loop

	mov	TB, B

	/* Process second block */
	/*
	 * rounds
	 *  0+80, 2+80, 4+80, 6+80, 8+80
	 * 10+80,12+80,14+80,16+80,18+80
	 */

	.set j, 0
	.rept 10
		RR	j+80
		.set j, j+2
	.endr

	jmp	_loop1
_loop1:
	/*
	 * rounds
	 * 20+80,22+80,24+80,26+80,28+80
	 * 30+80,32+80,34+80,36+80,38+80
	 */
	.rept 10
		RR	j+80
		.set j, j+2
	.endr

	jmp	_loop2
_loop2:

	/*
	 * rounds
	 * 40+80,42+80,44+80,46+80,48+80
	 * 50+80,52+80,54+80,56+80,58+80
	 */
	.rept 10
		RR	j+80
		.set j, j+2
	.endr

	/* update counter */
	sub     $1, BLOCKS_CTR
	/* Move to the next block only if needed*/
	ADD_IF_GE BUFFER_PTR2, BLOCKS_CTR, 4, 128

	jmp	_loop3
_loop3:

	/*
	 * rounds
	 * 60+80,62+80,64+80,66+80,68+80
	 * 70+80,72+80,74+80,76+80,78+80
	 */
	.rept 10
		RR	j+80
		.set j, j+2
	.endr

	UPDATE_HASH	(HASH_PTR), A
	UPDATE_HASH	4(HASH_PTR), TB
	UPDATE_HASH	8(HASH_PTR), C
	UPDATE_HASH	12(HASH_PTR), D
	UPDATE_HASH	16(HASH_PTR), E

	/* Reset state for AVX2 reg permutation */
	mov	A, TA
	mov	TB, A
	mov	C, TB
	mov	E, C
	mov	D, B
	mov	TA, D

	REGALLOC

	xchg	WK_BUF, PRECALC_BUF

	jmp	_loop

	.align 32
	_end:

.endm
/*
 * macro implements SHA-1 function's body for several 64-byte blocks
 * param: function's name
 */
.macro SHA1_VECTOR_ASM  name
	ENTRY(\name)

	push	%rbx
	push	%r12
	push	%r13
	push	%r14
	push	%r15

	RESERVE_STACK  = (W_SIZE*4 + 8+24)

	/* Align stack */
	mov	%rsp, %rbx
	and	$~(0x20-1), %rsp
	push	%rbx
	sub	$RESERVE_STACK, %rsp

	avx2_zeroupper

	/* Setup initial values */
	mov	CTX, HASH_PTR
	mov	BUF, BUFFER_PTR

	mov	BUF, BUFFER_PTR2
	mov	CNT, BLOCKS_CTR

	xmm_mov	BSWAP_SHUFB_CTL(%rip), YMM_SHUFB_BSWAP

	SHA1_PIPELINED_MAIN_BODY

	avx2_zeroupper

	add	$RESERVE_STACK, %rsp
	pop	%rsp

	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
	pop	%rbx

	ret

	ENDPROC(\name)
.endm

.section .rodata

#define K1 0x5a827999
#define K2 0x6ed9eba1
#define K3 0x8f1bbcdc
#define K4 0xca62c1d6

.align 128
K_XMM_AR:
	.long K1, K1, K1, K1
	.long K1, K1, K1, K1
	.long K2, K2, K2, K2
	.long K2, K2, K2, K2
	.long K3, K3, K3, K3
	.long K3, K3, K3, K3
	.long K4, K4, K4, K4
	.long K4, K4, K4, K4

BSWAP_SHUFB_CTL:
	.long 0x00010203
	.long 0x04050607
	.long 0x08090a0b
	.long 0x0c0d0e0f
	.long 0x00010203
	.long 0x04050607
	.long 0x08090a0b
	.long 0x0c0d0e0f
.text

SHA1_VECTOR_ASM     sha1_transform_avx2
