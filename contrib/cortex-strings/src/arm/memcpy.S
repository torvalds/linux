/* Copyright (c) 2013, Linaro Limited
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

      * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

      * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

      * Neither the name of Linaro Limited nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
   This memcpy routine is optimised for Cortex-A15 cores and takes advantage
   of VFP or NEON when built with the appropriate flags.

   Assumptions:

    ARMv6 (ARMv7-a if using Neon)
    ARM state
    Unaligned accesses

 */

	.syntax unified
	/* This implementation requires ARM state.  */
	.arm

#ifdef __ARM_NEON__

	.fpu	neon
	.arch	armv7-a
# define FRAME_SIZE	4
# define USE_VFP
# define USE_NEON

#elif !defined (__SOFTFP__)

	.arch	armv6
	.fpu	vfpv2
# define FRAME_SIZE	32
# define USE_VFP

#else
	.arch	armv6
# define FRAME_SIZE    32

#endif

/* Old versions of GAS incorrectly implement the NEON align semantics.  */
#ifdef BROKEN_ASM_NEON_ALIGN
#define ALIGN(addr, align) addr,:align
#else
#define ALIGN(addr, align) addr:align
#endif

#define PC_OFFSET	8	/* PC pipeline compensation.  */
#define INSN_SIZE	4

/* Call parameters.  */
#define dstin	r0
#define src	r1
#define count	r2

/* Locals.  */
#define tmp1	r3
#define dst	ip
#define tmp2	r10

#ifndef USE_NEON
/* For bulk copies using GP registers.  */
#define	A_l	r2		/* Call-clobbered.  */
#define	A_h	r3		/* Call-clobbered.  */
#define	B_l	r4
#define	B_h	r5
#define	C_l	r6
#define	C_h	r7
#define	D_l	r8
#define	D_h	r9
#endif

/* Number of lines ahead to pre-fetch data.  If you change this the code
   below will need adjustment to compensate.  */

#define prefetch_lines	5

#ifdef USE_VFP
	.macro	cpy_line_vfp vreg, base
	vstr	\vreg, [dst, #\base]
	vldr	\vreg, [src, #\base]
	vstr	d0, [dst, #\base + 8]
	vldr	d0, [src, #\base + 8]
	vstr	d1, [dst, #\base + 16]
	vldr	d1, [src, #\base + 16]
	vstr	d2, [dst, #\base + 24]
	vldr	d2, [src, #\base + 24]
	vstr	\vreg, [dst, #\base + 32]
	vldr	\vreg, [src, #\base + prefetch_lines * 64 - 32]
	vstr	d0, [dst, #\base + 40]
	vldr	d0, [src, #\base + 40]
	vstr	d1, [dst, #\base + 48]
	vldr	d1, [src, #\base + 48]
	vstr	d2, [dst, #\base + 56]
	vldr	d2, [src, #\base + 56]
	.endm

	.macro	cpy_tail_vfp vreg, base
	vstr	\vreg, [dst, #\base]
	vldr	\vreg, [src, #\base]
	vstr	d0, [dst, #\base + 8]
	vldr	d0, [src, #\base + 8]
	vstr	d1, [dst, #\base + 16]
	vldr	d1, [src, #\base + 16]
	vstr	d2, [dst, #\base + 24]
	vldr	d2, [src, #\base + 24]
	vstr	\vreg, [dst, #\base + 32]
	vstr	d0, [dst, #\base + 40]
	vldr	d0, [src, #\base + 40]
	vstr	d1, [dst, #\base + 48]
	vldr	d1, [src, #\base + 48]
	vstr	d2, [dst, #\base + 56]
	vldr	d2, [src, #\base + 56]
	.endm
#endif

	.macro def_fn f p2align=0
	.text
	.p2align \p2align
	.global \f
	.type \f, %function
\f:
	.endm

def_fn memcpy p2align=6

	mov	dst, dstin	/* Preserve dstin, we need to return it.  */
	cmp	count, #64
	bge	.Lcpy_not_short
	/* Deal with small copies quickly by dropping straight into the
	   exit block.  */

.Ltail63unaligned:
#ifdef USE_NEON
	and	tmp1, count, #0x38
	rsb	tmp1, tmp1, #(56 - PC_OFFSET + INSN_SIZE)
	add	pc, pc, tmp1
	vld1.8	{d0}, [src]!	/* 14 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 12 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 10 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 8 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 6 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 4 words to go.  */
	vst1.8	{d0}, [dst]!
	vld1.8	{d0}, [src]!	/* 2 words to go.  */
	vst1.8	{d0}, [dst]!

	tst	count, #4
	ldrne	tmp1, [src], #4
	strne	tmp1, [dst], #4
#else
	/* Copy up to 15 full words of data.  May not be aligned.  */
	/* Cannot use VFP for unaligned data.  */
	and	tmp1, count, #0x3c
	add	dst, dst, tmp1
	add	src, src, tmp1
	rsb	tmp1, tmp1, #(60 - PC_OFFSET/2 + INSN_SIZE/2)
	/* Jump directly into the sequence below at the correct offset.  */
	add	pc, pc, tmp1, lsl #1

	ldr	tmp1, [src, #-60]	/* 15 words to go.  */
	str	tmp1, [dst, #-60]

	ldr	tmp1, [src, #-56]	/* 14 words to go.  */
	str	tmp1, [dst, #-56]
	ldr	tmp1, [src, #-52]
	str	tmp1, [dst, #-52]

	ldr	tmp1, [src, #-48]	/* 12 words to go.  */
	str	tmp1, [dst, #-48]
	ldr	tmp1, [src, #-44]
	str	tmp1, [dst, #-44]

	ldr	tmp1, [src, #-40]	/* 10 words to go.  */
	str	tmp1, [dst, #-40]
	ldr	tmp1, [src, #-36]
	str	tmp1, [dst, #-36]

	ldr	tmp1, [src, #-32]	/* 8 words to go.  */
	str	tmp1, [dst, #-32]
	ldr	tmp1, [src, #-28]
	str	tmp1, [dst, #-28]

	ldr	tmp1, [src, #-24]	/* 6 words to go.  */
	str	tmp1, [dst, #-24]
	ldr	tmp1, [src, #-20]
	str	tmp1, [dst, #-20]

	ldr	tmp1, [src, #-16]	/* 4 words to go.  */
	str	tmp1, [dst, #-16]
	ldr	tmp1, [src, #-12]
	str	tmp1, [dst, #-12]

	ldr	tmp1, [src, #-8]	/* 2 words to go.  */
	str	tmp1, [dst, #-8]
	ldr	tmp1, [src, #-4]
	str	tmp1, [dst, #-4]
#endif

	lsls	count, count, #31
	ldrhcs	tmp1, [src], #2
	ldrbne	src, [src]		/* Src is dead, use as a scratch.  */
	strhcs	tmp1, [dst], #2
	strbne	src, [dst]
	bx	lr

.Lcpy_not_short:
	/* At least 64 bytes to copy, but don't know the alignment yet.  */
	str	tmp2, [sp, #-FRAME_SIZE]!
	and	tmp2, src, #7
	and	tmp1, dst, #7
	cmp	tmp1, tmp2
	bne	.Lcpy_notaligned

#ifdef USE_VFP
	/* Magic dust alert!  Force VFP on Cortex-A9.  Experiments show
	   that the FP pipeline is much better at streaming loads and
	   stores.  This is outside the critical loop.  */
	vmov.f32	s0, s0
#endif

	/* SRC and DST have the same mutual 64-bit alignment, but we may
	   still need to pre-copy some bytes to get to natural alignment.
	   We bring SRC and DST into full 64-bit alignment.  */
	lsls	tmp2, dst, #29
	beq	1f
	rsbs	tmp2, tmp2, #0
	sub	count, count, tmp2, lsr #29
	ldrmi	tmp1, [src], #4
	strmi	tmp1, [dst], #4
	lsls	tmp2, tmp2, #2
	ldrhcs	tmp1, [src], #2
	ldrbne	tmp2, [src], #1
	strhcs	tmp1, [dst], #2
	strbne	tmp2, [dst], #1

1:
	subs	tmp2, count, #64	/* Use tmp2 for count.  */
	blt	.Ltail63aligned

	cmp	tmp2, #512
	bge	.Lcpy_body_long

.Lcpy_body_medium:			/* Count in tmp2.  */
#ifdef USE_VFP
1:
	vldr	d0, [src, #0]
	subs	tmp2, tmp2, #64
	vldr	d1, [src, #8]
	vstr	d0, [dst, #0]
	vldr	d0, [src, #16]
	vstr	d1, [dst, #8]
	vldr	d1, [src, #24]
	vstr	d0, [dst, #16]
	vldr	d0, [src, #32]
	vstr	d1, [dst, #24]
	vldr	d1, [src, #40]
	vstr	d0, [dst, #32]
	vldr	d0, [src, #48]
	vstr	d1, [dst, #40]
	vldr	d1, [src, #56]
	vstr	d0, [dst, #48]
	add	src, src, #64
	vstr	d1, [dst, #56]
	add	dst, dst, #64
	bge	1b
	tst	tmp2, #0x3f
	beq	.Ldone

.Ltail63aligned:			/* Count in tmp2.  */
	and	tmp1, tmp2, #0x38
	add	dst, dst, tmp1
	add	src, src, tmp1
	rsb	tmp1, tmp1, #(56 - PC_OFFSET + INSN_SIZE)
	add	pc, pc, tmp1

	vldr	d0, [src, #-56]	/* 14 words to go.  */
	vstr	d0, [dst, #-56]
	vldr	d0, [src, #-48]	/* 12 words to go.  */
	vstr	d0, [dst, #-48]
	vldr	d0, [src, #-40]	/* 10 words to go.  */
	vstr	d0, [dst, #-40]
	vldr	d0, [src, #-32]	/* 8 words to go.  */
	vstr	d0, [dst, #-32]
	vldr	d0, [src, #-24]	/* 6 words to go.  */
	vstr	d0, [dst, #-24]
	vldr	d0, [src, #-16]	/* 4 words to go.  */
	vstr	d0, [dst, #-16]
	vldr	d0, [src, #-8]	/* 2 words to go.  */
	vstr	d0, [dst, #-8]
#else
	sub	src, src, #8
	sub	dst, dst, #8
1:
	ldrd	A_l, A_h, [src, #8]
	strd	A_l, A_h, [dst, #8]
	ldrd	A_l, A_h, [src, #16]
	strd	A_l, A_h, [dst, #16]
	ldrd	A_l, A_h, [src, #24]
	strd	A_l, A_h, [dst, #24]
	ldrd	A_l, A_h, [src, #32]
	strd	A_l, A_h, [dst, #32]
	ldrd	A_l, A_h, [src, #40]
	strd	A_l, A_h, [dst, #40]
	ldrd	A_l, A_h, [src, #48]
	strd	A_l, A_h, [dst, #48]
	ldrd	A_l, A_h, [src, #56]
	strd	A_l, A_h, [dst, #56]
	ldrd	A_l, A_h, [src, #64]!
	strd	A_l, A_h, [dst, #64]!
	subs	tmp2, tmp2, #64
	bge	1b
	tst	tmp2, #0x3f
	bne	1f
	ldr	tmp2,[sp], #FRAME_SIZE
	bx	lr
1:
	add	src, src, #8
	add	dst, dst, #8

.Ltail63aligned:			/* Count in tmp2.  */
	/* Copy up to 7 d-words of data.  Similar to Ltail63unaligned, but
	   we know that the src and dest are 64-bit aligned so we can use
	   LDRD/STRD to improve efficiency.  */
	/* TMP2 is now negative, but we don't care about that.  The bottom
	   six bits still tell us how many bytes are left to copy.  */

	and	tmp1, tmp2, #0x38
	add	dst, dst, tmp1
	add	src, src, tmp1
	rsb	tmp1, tmp1, #(56 - PC_OFFSET + INSN_SIZE)
	add	pc, pc, tmp1
	ldrd	A_l, A_h, [src, #-56]	/* 14 words to go.  */
	strd	A_l, A_h, [dst, #-56]
	ldrd	A_l, A_h, [src, #-48]	/* 12 words to go.  */
	strd	A_l, A_h, [dst, #-48]
	ldrd	A_l, A_h, [src, #-40]	/* 10 words to go.  */
	strd	A_l, A_h, [dst, #-40]
	ldrd	A_l, A_h, [src, #-32]	/* 8 words to go.  */
	strd	A_l, A_h, [dst, #-32]
	ldrd	A_l, A_h, [src, #-24]	/* 6 words to go.  */
	strd	A_l, A_h, [dst, #-24]
	ldrd	A_l, A_h, [src, #-16]	/* 4 words to go.  */
	strd	A_l, A_h, [dst, #-16]
	ldrd	A_l, A_h, [src, #-8]	/* 2 words to go.  */
	strd	A_l, A_h, [dst, #-8]

#endif
	tst	tmp2, #4
	ldrne	tmp1, [src], #4
	strne	tmp1, [dst], #4
	lsls	tmp2, tmp2, #31		/* Count (tmp2) now dead. */
	ldrhcs	tmp1, [src], #2
	ldrbne	tmp2, [src]
	strhcs	tmp1, [dst], #2
	strbne	tmp2, [dst]

.Ldone:
	ldr	tmp2, [sp], #FRAME_SIZE
	bx	lr

.Lcpy_body_long:			/* Count in tmp2.  */

	/* Long copy.  We know that there's at least (prefetch_lines * 64)
	   bytes to go.  */
#ifdef USE_VFP
	/* Don't use PLD.  Instead, read some data in advance of the current
	   copy position into a register.  This should act like a PLD
	   operation but we won't have to repeat the transfer.  */

	vldr	d3, [src, #0]
	vldr	d4, [src, #64]
	vldr	d5, [src, #128]
	vldr	d6, [src, #192]
	vldr	d7, [src, #256]

	vldr	d0, [src, #8]
	vldr	d1, [src, #16]
	vldr	d2, [src, #24]
	add	src, src, #32

	subs	tmp2, tmp2, #prefetch_lines * 64 * 2
	blt	2f
1:
	cpy_line_vfp	d3, 0
	cpy_line_vfp	d4, 64
	cpy_line_vfp	d5, 128
	add	dst, dst, #3 * 64
	add	src, src, #3 * 64
	cpy_line_vfp	d6, 0
	cpy_line_vfp	d7, 64
	add	dst, dst, #2 * 64
	add	src, src, #2 * 64
	subs	tmp2, tmp2, #prefetch_lines * 64
	bge	1b

2:
	cpy_tail_vfp	d3, 0
	cpy_tail_vfp	d4, 64
	cpy_tail_vfp	d5, 128
	add	src, src, #3 * 64
	add	dst, dst, #3 * 64
	cpy_tail_vfp	d6, 0
	vstr	d7, [dst, #64]
	vldr	d7, [src, #64]
	vstr	d0, [dst, #64 + 8]
	vldr	d0, [src, #64 + 8]
	vstr	d1, [dst, #64 + 16]
	vldr	d1, [src, #64 + 16]
	vstr	d2, [dst, #64 + 24]
	vldr	d2, [src, #64 + 24]
	vstr	d7, [dst, #64 + 32]
	add	src, src, #96
	vstr	d0, [dst, #64 + 40]
	vstr	d1, [dst, #64 + 48]
	vstr	d2, [dst, #64 + 56]
	add	dst, dst, #128
	add	tmp2, tmp2, #prefetch_lines * 64
	b	.Lcpy_body_medium
#else
	/* Long copy.  Use an SMS style loop to maximize the I/O
	   bandwidth of the core.  We don't have enough spare registers
	   to synthesise prefetching, so use PLD operations.  */
	/* Pre-bias src and dst.  */
	sub	src, src, #8
	sub	dst, dst, #8
	pld	[src, #8]
	pld	[src, #72]
	subs	tmp2, tmp2, #64
	pld	[src, #136]
	ldrd	A_l, A_h, [src, #8]
	strd	B_l, B_h, [sp, #8]
	ldrd	B_l, B_h, [src, #16]
	strd	C_l, C_h, [sp, #16]
	ldrd	C_l, C_h, [src, #24]
	strd	D_l, D_h, [sp, #24]
	pld	[src, #200]
	ldrd	D_l, D_h, [src, #32]!
	b	1f
	.p2align	6
2:
	pld	[src, #232]
	strd	A_l, A_h, [dst, #40]
	ldrd	A_l, A_h, [src, #40]
	strd	B_l, B_h, [dst, #48]
	ldrd	B_l, B_h, [src, #48]
	strd	C_l, C_h, [dst, #56]
	ldrd	C_l, C_h, [src, #56]
	strd	D_l, D_h, [dst, #64]!
	ldrd	D_l, D_h, [src, #64]!
	subs	tmp2, tmp2, #64
1:
	strd	A_l, A_h, [dst, #8]
	ldrd	A_l, A_h, [src, #8]
	strd	B_l, B_h, [dst, #16]
	ldrd	B_l, B_h, [src, #16]
	strd	C_l, C_h, [dst, #24]
	ldrd	C_l, C_h, [src, #24]
	strd	D_l, D_h, [dst, #32]
	ldrd	D_l, D_h, [src, #32]
	bcs	2b
	/* Save the remaining bytes and restore the callee-saved regs.  */
	strd	A_l, A_h, [dst, #40]
	add	src, src, #40
	strd	B_l, B_h, [dst, #48]
	ldrd	B_l, B_h, [sp, #8]
	strd	C_l, C_h, [dst, #56]
	ldrd	C_l, C_h, [sp, #16]
	strd	D_l, D_h, [dst, #64]
	ldrd	D_l, D_h, [sp, #24]
	add	dst, dst, #72
	tst	tmp2, #0x3f
	bne	.Ltail63aligned
	ldr	tmp2, [sp], #FRAME_SIZE
	bx	lr
#endif

.Lcpy_notaligned:
	pld	[src]
	pld	[src, #64]
	/* There's at least 64 bytes to copy, but there is no mutual
	   alignment.  */
	/* Bring DST to 64-bit alignment.  */
	lsls	tmp2, dst, #29
	pld	[src, #(2 * 64)]
	beq	1f
	rsbs	tmp2, tmp2, #0
	sub	count, count, tmp2, lsr #29
	ldrmi	tmp1, [src], #4
	strmi	tmp1, [dst], #4
	lsls	tmp2, tmp2, #2
	ldrbne	tmp1, [src], #1
	ldrhcs	tmp2, [src], #2
	strbne	tmp1, [dst], #1
	strhcs	tmp2, [dst], #2
1:
	pld	[src, #(3 * 64)]
	subs	count, count, #64
	ldrmi	tmp2, [sp], #FRAME_SIZE
	bmi	.Ltail63unaligned
	pld	[src, #(4 * 64)]

#ifdef USE_NEON
	vld1.8	{d0-d3}, [src]!
	vld1.8	{d4-d7}, [src]!
	subs	count, count, #64
	bmi	2f
1:
	pld	[src, #(4 * 64)]
	vst1.8	{d0-d3}, [ALIGN (dst, 64)]!
	vld1.8	{d0-d3}, [src]!
	vst1.8	{d4-d7}, [ALIGN (dst, 64)]!
	vld1.8	{d4-d7}, [src]!
	subs	count, count, #64
	bpl	1b
2:
	vst1.8	{d0-d3}, [ALIGN (dst, 64)]!
	vst1.8	{d4-d7}, [ALIGN (dst, 64)]!
	ands	count, count, #0x3f
#else
	/* Use an SMS style loop to maximize the I/O bandwidth.  */
	sub	src, src, #4
	sub	dst, dst, #8
	subs	tmp2, count, #64	/* Use tmp2 for count.  */
	ldr	A_l, [src, #4]
	ldr	A_h, [src, #8]
	strd	B_l, B_h, [sp, #8]
	ldr	B_l, [src, #12]
	ldr	B_h, [src, #16]
	strd	C_l, C_h, [sp, #16]
	ldr	C_l, [src, #20]
	ldr	C_h, [src, #24]
	strd	D_l, D_h, [sp, #24]
	ldr	D_l, [src, #28]
	ldr	D_h, [src, #32]!
	b	1f
	.p2align	6
2:
	pld	[src, #(5 * 64) - (32 - 4)]
	strd	A_l, A_h, [dst, #40]
	ldr	A_l, [src, #36]
	ldr	A_h, [src, #40]
	strd	B_l, B_h, [dst, #48]
	ldr	B_l, [src, #44]
	ldr	B_h, [src, #48]
	strd	C_l, C_h, [dst, #56]
	ldr	C_l, [src, #52]
	ldr	C_h, [src, #56]
	strd	D_l, D_h, [dst, #64]!
	ldr	D_l, [src, #60]
	ldr	D_h, [src, #64]!
	subs	tmp2, tmp2, #64
1:
	strd	A_l, A_h, [dst, #8]
	ldr	A_l, [src, #4]
	ldr	A_h, [src, #8]
	strd	B_l, B_h, [dst, #16]
	ldr	B_l, [src, #12]
	ldr	B_h, [src, #16]
	strd	C_l, C_h, [dst, #24]
	ldr	C_l, [src, #20]
	ldr	C_h, [src, #24]
	strd	D_l, D_h, [dst, #32]
	ldr	D_l, [src, #28]
	ldr	D_h, [src, #32]
	bcs	2b

	/* Save the remaining bytes and restore the callee-saved regs.  */
	strd	A_l, A_h, [dst, #40]
	add	src, src, #36
	strd	B_l, B_h, [dst, #48]
	ldrd	B_l, B_h, [sp, #8]
	strd	C_l, C_h, [dst, #56]
	ldrd	C_l, C_h, [sp, #16]
	strd	D_l, D_h, [dst, #64]
	ldrd	D_l, D_h, [sp, #24]
	add	dst, dst, #72
	ands	count, tmp2, #0x3f
#endif
	ldr	tmp2, [sp], #FRAME_SIZE
	bne	.Ltail63unaligned
	bx	lr

	.size	memcpy, . - memcpy
