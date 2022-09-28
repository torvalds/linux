/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * des3_ede-asm_64.S  -  x86-64 assembly implementation of 3DES cipher
 *
 * Copyright © 2014 Jussi Kivilinna <jussi.kivilinna@iki.fi>
 */

#include <linux/linkage.h>

.file "des3_ede-asm_64.S"
.text

#define s1 .L_s1
#define s2 ((s1) + (64*8))
#define s3 ((s2) + (64*8))
#define s4 ((s3) + (64*8))
#define s5 ((s4) + (64*8))
#define s6 ((s5) + (64*8))
#define s7 ((s6) + (64*8))
#define s8 ((s7) + (64*8))

/* register macros */
#define CTX %rdi

#define RL0 %r8
#define RL1 %r9
#define RL2 %r10

#define RL0d %r8d
#define RL1d %r9d
#define RL2d %r10d

#define RR0 %r11
#define RR1 %r12
#define RR2 %r13

#define RR0d %r11d
#define RR1d %r12d
#define RR2d %r13d

#define RW0 %rax
#define RW1 %rbx
#define RW2 %rcx

#define RW0d %eax
#define RW1d %ebx
#define RW2d %ecx

#define RW0bl %al
#define RW1bl %bl
#define RW2bl %cl

#define RW0bh %ah
#define RW1bh %bh
#define RW2bh %ch

#define RT0 %r15
#define RT1 %rsi
#define RT2 %r14
#define RT3 %rdx

#define RT0d %r15d
#define RT1d %esi
#define RT2d %r14d
#define RT3d %edx

/***********************************************************************
 * 1-way 3DES
 ***********************************************************************/
#define do_permutation(a, b, offset, mask) \
	movl a, RT0d; \
	shrl $(offset), RT0d; \
	xorl b, RT0d; \
	andl $(mask), RT0d; \
	xorl RT0d, b; \
	shll $(offset), RT0d; \
	xorl RT0d, a;

#define expand_to_64bits(val, mask) \
	movl val##d, RT0d; \
	rorl $4, RT0d; \
	shlq $32, RT0; \
	orq RT0, val; \
	andq mask, val;

#define compress_to_64bits(val) \
	movq val, RT0; \
	shrq $32, RT0; \
	roll $4, RT0d; \
	orl RT0d, val##d;

#define initial_permutation(left, right) \
	do_permutation(left##d, right##d,  4, 0x0f0f0f0f); \
	do_permutation(left##d, right##d, 16, 0x0000ffff); \
	do_permutation(right##d, left##d,  2, 0x33333333); \
	do_permutation(right##d, left##d,  8, 0x00ff00ff); \
	movabs $0x3f3f3f3f3f3f3f3f, RT3; \
	movl left##d, RW0d; \
	roll $1, right##d; \
	xorl right##d, RW0d; \
	andl $0xaaaaaaaa, RW0d; \
	xorl RW0d, left##d; \
	xorl RW0d, right##d; \
	roll $1, left##d; \
	expand_to_64bits(right, RT3); \
	expand_to_64bits(left, RT3);

#define final_permutation(left, right) \
	compress_to_64bits(right); \
	compress_to_64bits(left); \
	movl right##d, RW0d; \
	rorl $1, left##d; \
	xorl left##d, RW0d; \
	andl $0xaaaaaaaa, RW0d; \
	xorl RW0d, right##d; \
	xorl RW0d, left##d; \
	rorl $1, right##d; \
	do_permutation(right##d, left##d,  8, 0x00ff00ff); \
	do_permutation(right##d, left##d,  2, 0x33333333); \
	do_permutation(left##d, right##d, 16, 0x0000ffff); \
	do_permutation(left##d, right##d,  4, 0x0f0f0f0f);

#define round1(n, from, to, load_next_key) \
	xorq from, RW0; \
	\
	movzbl RW0bl, RT0d; \
	movzbl RW0bh, RT1d; \
	shrq $16, RW0; \
	movzbl RW0bl, RT2d; \
	movzbl RW0bh, RT3d; \
	shrq $16, RW0; \
	movq s8(, RT0, 8), RT0; \
	xorq s6(, RT1, 8), to; \
	movzbl RW0bl, RL1d; \
	movzbl RW0bh, RT1d; \
	shrl $16, RW0d; \
	xorq s4(, RT2, 8), RT0; \
	xorq s2(, RT3, 8), to; \
	movzbl RW0bl, RT2d; \
	movzbl RW0bh, RT3d; \
	xorq s7(, RL1, 8), RT0; \
	xorq s5(, RT1, 8), to; \
	xorq s3(, RT2, 8), RT0; \
	load_next_key(n, RW0); \
	xorq RT0, to; \
	xorq s1(, RT3, 8), to; \

#define load_next_key(n, RWx) \
	movq (((n) + 1) * 8)(CTX), RWx;

#define dummy2(a, b) /*_*/

#define read_block(io, left, right) \
	movl    (io), left##d; \
	movl   4(io), right##d; \
	bswapl left##d; \
	bswapl right##d;

#define write_block(io, left, right) \
	bswapl left##d; \
	bswapl right##d; \
	movl   left##d,   (io); \
	movl   right##d, 4(io);

SYM_FUNC_START(des3_ede_x86_64_crypt_blk)
	/* input:
	 *	%rdi: round keys, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */
	pushq %rbx;
	pushq %r12;
	pushq %r13;
	pushq %r14;
	pushq %r15;

	pushq %rsi; /* dst */

	read_block(%rdx, RL0, RR0);
	initial_permutation(RL0, RR0);

	movq (CTX), RW0;

	round1(0, RR0, RL0, load_next_key);
	round1(1, RL0, RR0, load_next_key);
	round1(2, RR0, RL0, load_next_key);
	round1(3, RL0, RR0, load_next_key);
	round1(4, RR0, RL0, load_next_key);
	round1(5, RL0, RR0, load_next_key);
	round1(6, RR0, RL0, load_next_key);
	round1(7, RL0, RR0, load_next_key);
	round1(8, RR0, RL0, load_next_key);
	round1(9, RL0, RR0, load_next_key);
	round1(10, RR0, RL0, load_next_key);
	round1(11, RL0, RR0, load_next_key);
	round1(12, RR0, RL0, load_next_key);
	round1(13, RL0, RR0, load_next_key);
	round1(14, RR0, RL0, load_next_key);
	round1(15, RL0, RR0, load_next_key);

	round1(16+0, RL0, RR0, load_next_key);
	round1(16+1, RR0, RL0, load_next_key);
	round1(16+2, RL0, RR0, load_next_key);
	round1(16+3, RR0, RL0, load_next_key);
	round1(16+4, RL0, RR0, load_next_key);
	round1(16+5, RR0, RL0, load_next_key);
	round1(16+6, RL0, RR0, load_next_key);
	round1(16+7, RR0, RL0, load_next_key);
	round1(16+8, RL0, RR0, load_next_key);
	round1(16+9, RR0, RL0, load_next_key);
	round1(16+10, RL0, RR0, load_next_key);
	round1(16+11, RR0, RL0, load_next_key);
	round1(16+12, RL0, RR0, load_next_key);
	round1(16+13, RR0, RL0, load_next_key);
	round1(16+14, RL0, RR0, load_next_key);
	round1(16+15, RR0, RL0, load_next_key);

	round1(32+0, RR0, RL0, load_next_key);
	round1(32+1, RL0, RR0, load_next_key);
	round1(32+2, RR0, RL0, load_next_key);
	round1(32+3, RL0, RR0, load_next_key);
	round1(32+4, RR0, RL0, load_next_key);
	round1(32+5, RL0, RR0, load_next_key);
	round1(32+6, RR0, RL0, load_next_key);
	round1(32+7, RL0, RR0, load_next_key);
	round1(32+8, RR0, RL0, load_next_key);
	round1(32+9, RL0, RR0, load_next_key);
	round1(32+10, RR0, RL0, load_next_key);
	round1(32+11, RL0, RR0, load_next_key);
	round1(32+12, RR0, RL0, load_next_key);
	round1(32+13, RL0, RR0, load_next_key);
	round1(32+14, RR0, RL0, load_next_key);
	round1(32+15, RL0, RR0, dummy2);

	final_permutation(RR0, RL0);

	popq %rsi /* dst */
	write_block(%rsi, RR0, RL0);

	popq %r15;
	popq %r14;
	popq %r13;
	popq %r12;
	popq %rbx;

	RET;
SYM_FUNC_END(des3_ede_x86_64_crypt_blk)

/***********************************************************************
 * 3-way 3DES
 ***********************************************************************/
#define expand_to_64bits(val, mask) \
	movl val##d, RT0d; \
	rorl $4, RT0d; \
	shlq $32, RT0; \
	orq RT0, val; \
	andq mask, val;

#define compress_to_64bits(val) \
	movq val, RT0; \
	shrq $32, RT0; \
	roll $4, RT0d; \
	orl RT0d, val##d;

#define initial_permutation3(left, right) \
	do_permutation(left##0d, right##0d,  4, 0x0f0f0f0f); \
	do_permutation(left##0d, right##0d, 16, 0x0000ffff); \
	  do_permutation(left##1d, right##1d,  4, 0x0f0f0f0f); \
	  do_permutation(left##1d, right##1d, 16, 0x0000ffff); \
	    do_permutation(left##2d, right##2d,  4, 0x0f0f0f0f); \
	    do_permutation(left##2d, right##2d, 16, 0x0000ffff); \
	    \
	do_permutation(right##0d, left##0d,  2, 0x33333333); \
	do_permutation(right##0d, left##0d,  8, 0x00ff00ff); \
	  do_permutation(right##1d, left##1d,  2, 0x33333333); \
	  do_permutation(right##1d, left##1d,  8, 0x00ff00ff); \
	    do_permutation(right##2d, left##2d,  2, 0x33333333); \
	    do_permutation(right##2d, left##2d,  8, 0x00ff00ff); \
	    \
	movabs $0x3f3f3f3f3f3f3f3f, RT3; \
	    \
	movl left##0d, RW0d; \
	roll $1, right##0d; \
	xorl right##0d, RW0d; \
	andl $0xaaaaaaaa, RW0d; \
	xorl RW0d, left##0d; \
	xorl RW0d, right##0d; \
	roll $1, left##0d; \
	expand_to_64bits(right##0, RT3); \
	expand_to_64bits(left##0, RT3); \
	  movl left##1d, RW1d; \
	  roll $1, right##1d; \
	  xorl right##1d, RW1d; \
	  andl $0xaaaaaaaa, RW1d; \
	  xorl RW1d, left##1d; \
	  xorl RW1d, right##1d; \
	  roll $1, left##1d; \
	  expand_to_64bits(right##1, RT3); \
	  expand_to_64bits(left##1, RT3); \
	    movl left##2d, RW2d; \
	    roll $1, right##2d; \
	    xorl right##2d, RW2d; \
	    andl $0xaaaaaaaa, RW2d; \
	    xorl RW2d, left##2d; \
	    xorl RW2d, right##2d; \
	    roll $1, left##2d; \
	    expand_to_64bits(right##2, RT3); \
	    expand_to_64bits(left##2, RT3);

#define final_permutation3(left, right) \
	compress_to_64bits(right##0); \
	compress_to_64bits(left##0); \
	movl right##0d, RW0d; \
	rorl $1, left##0d; \
	xorl left##0d, RW0d; \
	andl $0xaaaaaaaa, RW0d; \
	xorl RW0d, right##0d; \
	xorl RW0d, left##0d; \
	rorl $1, right##0d; \
	  compress_to_64bits(right##1); \
	  compress_to_64bits(left##1); \
	  movl right##1d, RW1d; \
	  rorl $1, left##1d; \
	  xorl left##1d, RW1d; \
	  andl $0xaaaaaaaa, RW1d; \
	  xorl RW1d, right##1d; \
	  xorl RW1d, left##1d; \
	  rorl $1, right##1d; \
	    compress_to_64bits(right##2); \
	    compress_to_64bits(left##2); \
	    movl right##2d, RW2d; \
	    rorl $1, left##2d; \
	    xorl left##2d, RW2d; \
	    andl $0xaaaaaaaa, RW2d; \
	    xorl RW2d, right##2d; \
	    xorl RW2d, left##2d; \
	    rorl $1, right##2d; \
	    \
	do_permutation(right##0d, left##0d,  8, 0x00ff00ff); \
	do_permutation(right##0d, left##0d,  2, 0x33333333); \
	  do_permutation(right##1d, left##1d,  8, 0x00ff00ff); \
	  do_permutation(right##1d, left##1d,  2, 0x33333333); \
	    do_permutation(right##2d, left##2d,  8, 0x00ff00ff); \
	    do_permutation(right##2d, left##2d,  2, 0x33333333); \
	    \
	do_permutation(left##0d, right##0d, 16, 0x0000ffff); \
	do_permutation(left##0d, right##0d,  4, 0x0f0f0f0f); \
	  do_permutation(left##1d, right##1d, 16, 0x0000ffff); \
	  do_permutation(left##1d, right##1d,  4, 0x0f0f0f0f); \
	    do_permutation(left##2d, right##2d, 16, 0x0000ffff); \
	    do_permutation(left##2d, right##2d,  4, 0x0f0f0f0f);

#define round3(n, from, to, load_next_key, do_movq) \
	xorq from##0, RW0; \
	movzbl RW0bl, RT3d; \
	movzbl RW0bh, RT1d; \
	shrq $16, RW0; \
	xorq s8(, RT3, 8), to##0; \
	xorq s6(, RT1, 8), to##0; \
	movzbl RW0bl, RT3d; \
	movzbl RW0bh, RT1d; \
	shrq $16, RW0; \
	xorq s4(, RT3, 8), to##0; \
	xorq s2(, RT1, 8), to##0; \
	movzbl RW0bl, RT3d; \
	movzbl RW0bh, RT1d; \
	shrl $16, RW0d; \
	xorq s7(, RT3, 8), to##0; \
	xorq s5(, RT1, 8), to##0; \
	movzbl RW0bl, RT3d; \
	movzbl RW0bh, RT1d; \
	load_next_key(n, RW0); \
	xorq s3(, RT3, 8), to##0; \
	xorq s1(, RT1, 8), to##0; \
		xorq from##1, RW1; \
		movzbl RW1bl, RT3d; \
		movzbl RW1bh, RT1d; \
		shrq $16, RW1; \
		xorq s8(, RT3, 8), to##1; \
		xorq s6(, RT1, 8), to##1; \
		movzbl RW1bl, RT3d; \
		movzbl RW1bh, RT1d; \
		shrq $16, RW1; \
		xorq s4(, RT3, 8), to##1; \
		xorq s2(, RT1, 8), to##1; \
		movzbl RW1bl, RT3d; \
		movzbl RW1bh, RT1d; \
		shrl $16, RW1d; \
		xorq s7(, RT3, 8), to##1; \
		xorq s5(, RT1, 8), to##1; \
		movzbl RW1bl, RT3d; \
		movzbl RW1bh, RT1d; \
		do_movq(RW0, RW1); \
		xorq s3(, RT3, 8), to##1; \
		xorq s1(, RT1, 8), to##1; \
			xorq from##2, RW2; \
			movzbl RW2bl, RT3d; \
			movzbl RW2bh, RT1d; \
			shrq $16, RW2; \
			xorq s8(, RT3, 8), to##2; \
			xorq s6(, RT1, 8), to##2; \
			movzbl RW2bl, RT3d; \
			movzbl RW2bh, RT1d; \
			shrq $16, RW2; \
			xorq s4(, RT3, 8), to##2; \
			xorq s2(, RT1, 8), to##2; \
			movzbl RW2bl, RT3d; \
			movzbl RW2bh, RT1d; \
			shrl $16, RW2d; \
			xorq s7(, RT3, 8), to##2; \
			xorq s5(, RT1, 8), to##2; \
			movzbl RW2bl, RT3d; \
			movzbl RW2bh, RT1d; \
			do_movq(RW0, RW2); \
			xorq s3(, RT3, 8), to##2; \
			xorq s1(, RT1, 8), to##2;

#define __movq(src, dst) \
	movq src, dst;

SYM_FUNC_START(des3_ede_x86_64_crypt_blk_3way)
	/* input:
	 *	%rdi: ctx, round keys
	 *	%rsi: dst (3 blocks)
	 *	%rdx: src (3 blocks)
	 */

	pushq %rbx;
	pushq %r12;
	pushq %r13;
	pushq %r14;
	pushq %r15;

	pushq %rsi /* dst */

	/* load input */
	movl 0 * 4(%rdx), RL0d;
	movl 1 * 4(%rdx), RR0d;
	movl 2 * 4(%rdx), RL1d;
	movl 3 * 4(%rdx), RR1d;
	movl 4 * 4(%rdx), RL2d;
	movl 5 * 4(%rdx), RR2d;

	bswapl RL0d;
	bswapl RR0d;
	bswapl RL1d;
	bswapl RR1d;
	bswapl RL2d;
	bswapl RR2d;

	initial_permutation3(RL, RR);

	movq 0(CTX), RW0;
	movq RW0, RW1;
	movq RW0, RW2;

	round3(0, RR, RL, load_next_key, __movq);
	round3(1, RL, RR, load_next_key, __movq);
	round3(2, RR, RL, load_next_key, __movq);
	round3(3, RL, RR, load_next_key, __movq);
	round3(4, RR, RL, load_next_key, __movq);
	round3(5, RL, RR, load_next_key, __movq);
	round3(6, RR, RL, load_next_key, __movq);
	round3(7, RL, RR, load_next_key, __movq);
	round3(8, RR, RL, load_next_key, __movq);
	round3(9, RL, RR, load_next_key, __movq);
	round3(10, RR, RL, load_next_key, __movq);
	round3(11, RL, RR, load_next_key, __movq);
	round3(12, RR, RL, load_next_key, __movq);
	round3(13, RL, RR, load_next_key, __movq);
	round3(14, RR, RL, load_next_key, __movq);
	round3(15, RL, RR, load_next_key, __movq);

	round3(16+0, RL, RR, load_next_key, __movq);
	round3(16+1, RR, RL, load_next_key, __movq);
	round3(16+2, RL, RR, load_next_key, __movq);
	round3(16+3, RR, RL, load_next_key, __movq);
	round3(16+4, RL, RR, load_next_key, __movq);
	round3(16+5, RR, RL, load_next_key, __movq);
	round3(16+6, RL, RR, load_next_key, __movq);
	round3(16+7, RR, RL, load_next_key, __movq);
	round3(16+8, RL, RR, load_next_key, __movq);
	round3(16+9, RR, RL, load_next_key, __movq);
	round3(16+10, RL, RR, load_next_key, __movq);
	round3(16+11, RR, RL, load_next_key, __movq);
	round3(16+12, RL, RR, load_next_key, __movq);
	round3(16+13, RR, RL, load_next_key, __movq);
	round3(16+14, RL, RR, load_next_key, __movq);
	round3(16+15, RR, RL, load_next_key, __movq);

	round3(32+0, RR, RL, load_next_key, __movq);
	round3(32+1, RL, RR, load_next_key, __movq);
	round3(32+2, RR, RL, load_next_key, __movq);
	round3(32+3, RL, RR, load_next_key, __movq);
	round3(32+4, RR, RL, load_next_key, __movq);
	round3(32+5, RL, RR, load_next_key, __movq);
	round3(32+6, RR, RL, load_next_key, __movq);
	round3(32+7, RL, RR, load_next_key, __movq);
	round3(32+8, RR, RL, load_next_key, __movq);
	round3(32+9, RL, RR, load_next_key, __movq);
	round3(32+10, RR, RL, load_next_key, __movq);
	round3(32+11, RL, RR, load_next_key, __movq);
	round3(32+12, RR, RL, load_next_key, __movq);
	round3(32+13, RL, RR, load_next_key, __movq);
	round3(32+14, RR, RL, load_next_key, __movq);
	round3(32+15, RL, RR, dummy2, dummy2);

	final_permutation3(RR, RL);

	bswapl RR0d;
	bswapl RL0d;
	bswapl RR1d;
	bswapl RL1d;
	bswapl RR2d;
	bswapl RL2d;

	popq %rsi /* dst */
	movl RR0d, 0 * 4(%rsi);
	movl RL0d, 1 * 4(%rsi);
	movl RR1d, 2 * 4(%rsi);
	movl RL1d, 3 * 4(%rsi);
	movl RR2d, 4 * 4(%rsi);
	movl RL2d, 5 * 4(%rsi);

	popq %r15;
	popq %r14;
	popq %r13;
	popq %r12;
	popq %rbx;

	RET;
SYM_FUNC_END(des3_ede_x86_64_crypt_blk_3way)

.section	.rodata, "a", @progbits
.align 16
.L_s1:
	.quad 0x0010100001010400, 0x0000000000000000
	.quad 0x0000100000010000, 0x0010100001010404
	.quad 0x0010100001010004, 0x0000100000010404
	.quad 0x0000000000000004, 0x0000100000010000
	.quad 0x0000000000000400, 0x0010100001010400
	.quad 0x0010100001010404, 0x0000000000000400
	.quad 0x0010000001000404, 0x0010100001010004
	.quad 0x0010000001000000, 0x0000000000000004
	.quad 0x0000000000000404, 0x0010000001000400
	.quad 0x0010000001000400, 0x0000100000010400
	.quad 0x0000100000010400, 0x0010100001010000
	.quad 0x0010100001010000, 0x0010000001000404
	.quad 0x0000100000010004, 0x0010000001000004
	.quad 0x0010000001000004, 0x0000100000010004
	.quad 0x0000000000000000, 0x0000000000000404
	.quad 0x0000100000010404, 0x0010000001000000
	.quad 0x0000100000010000, 0x0010100001010404
	.quad 0x0000000000000004, 0x0010100001010000
	.quad 0x0010100001010400, 0x0010000001000000
	.quad 0x0010000001000000, 0x0000000000000400
	.quad 0x0010100001010004, 0x0000100000010000
	.quad 0x0000100000010400, 0x0010000001000004
	.quad 0x0000000000000400, 0x0000000000000004
	.quad 0x0010000001000404, 0x0000100000010404
	.quad 0x0010100001010404, 0x0000100000010004
	.quad 0x0010100001010000, 0x0010000001000404
	.quad 0x0010000001000004, 0x0000000000000404
	.quad 0x0000100000010404, 0x0010100001010400
	.quad 0x0000000000000404, 0x0010000001000400
	.quad 0x0010000001000400, 0x0000000000000000
	.quad 0x0000100000010004, 0x0000100000010400
	.quad 0x0000000000000000, 0x0010100001010004
.L_s2:
	.quad 0x0801080200100020, 0x0800080000000000
	.quad 0x0000080000000000, 0x0001080200100020
	.quad 0x0001000000100000, 0x0000000200000020
	.quad 0x0801000200100020, 0x0800080200000020
	.quad 0x0800000200000020, 0x0801080200100020
	.quad 0x0801080000100000, 0x0800000000000000
	.quad 0x0800080000000000, 0x0001000000100000
	.quad 0x0000000200000020, 0x0801000200100020
	.quad 0x0001080000100000, 0x0001000200100020
	.quad 0x0800080200000020, 0x0000000000000000
	.quad 0x0800000000000000, 0x0000080000000000
	.quad 0x0001080200100020, 0x0801000000100000
	.quad 0x0001000200100020, 0x0800000200000020
	.quad 0x0000000000000000, 0x0001080000100000
	.quad 0x0000080200000020, 0x0801080000100000
	.quad 0x0801000000100000, 0x0000080200000020
	.quad 0x0000000000000000, 0x0001080200100020
	.quad 0x0801000200100020, 0x0001000000100000
	.quad 0x0800080200000020, 0x0801000000100000
	.quad 0x0801080000100000, 0x0000080000000000
	.quad 0x0801000000100000, 0x0800080000000000
	.quad 0x0000000200000020, 0x0801080200100020
	.quad 0x0001080200100020, 0x0000000200000020
	.quad 0x0000080000000000, 0x0800000000000000
	.quad 0x0000080200000020, 0x0801080000100000
	.quad 0x0001000000100000, 0x0800000200000020
	.quad 0x0001000200100020, 0x0800080200000020
	.quad 0x0800000200000020, 0x0001000200100020
	.quad 0x0001080000100000, 0x0000000000000000
	.quad 0x0800080000000000, 0x0000080200000020
	.quad 0x0800000000000000, 0x0801000200100020
	.quad 0x0801080200100020, 0x0001080000100000
.L_s3:
	.quad 0x0000002000000208, 0x0000202008020200
	.quad 0x0000000000000000, 0x0000200008020008
	.quad 0x0000002008000200, 0x0000000000000000
	.quad 0x0000202000020208, 0x0000002008000200
	.quad 0x0000200000020008, 0x0000000008000008
	.quad 0x0000000008000008, 0x0000200000020000
	.quad 0x0000202008020208, 0x0000200000020008
	.quad 0x0000200008020000, 0x0000002000000208
	.quad 0x0000000008000000, 0x0000000000000008
	.quad 0x0000202008020200, 0x0000002000000200
	.quad 0x0000202000020200, 0x0000200008020000
	.quad 0x0000200008020008, 0x0000202000020208
	.quad 0x0000002008000208, 0x0000202000020200
	.quad 0x0000200000020000, 0x0000002008000208
	.quad 0x0000000000000008, 0x0000202008020208
	.quad 0x0000002000000200, 0x0000000008000000
	.quad 0x0000202008020200, 0x0000000008000000
	.quad 0x0000200000020008, 0x0000002000000208
	.quad 0x0000200000020000, 0x0000202008020200
	.quad 0x0000002008000200, 0x0000000000000000
	.quad 0x0000002000000200, 0x0000200000020008
	.quad 0x0000202008020208, 0x0000002008000200
	.quad 0x0000000008000008, 0x0000002000000200
	.quad 0x0000000000000000, 0x0000200008020008
	.quad 0x0000002008000208, 0x0000200000020000
	.quad 0x0000000008000000, 0x0000202008020208
	.quad 0x0000000000000008, 0x0000202000020208
	.quad 0x0000202000020200, 0x0000000008000008
	.quad 0x0000200008020000, 0x0000002008000208
	.quad 0x0000002000000208, 0x0000200008020000
	.quad 0x0000202000020208, 0x0000000000000008
	.quad 0x0000200008020008, 0x0000202000020200
.L_s4:
	.quad 0x1008020000002001, 0x1000020800002001
	.quad 0x1000020800002001, 0x0000000800000000
	.quad 0x0008020800002000, 0x1008000800000001
	.quad 0x1008000000000001, 0x1000020000002001
	.quad 0x0000000000000000, 0x0008020000002000
	.quad 0x0008020000002000, 0x1008020800002001
	.quad 0x1000000800000001, 0x0000000000000000
	.quad 0x0008000800000000, 0x1008000000000001
	.quad 0x1000000000000001, 0x0000020000002000
	.quad 0x0008000000000000, 0x1008020000002001
	.quad 0x0000000800000000, 0x0008000000000000
	.quad 0x1000020000002001, 0x0000020800002000
	.quad 0x1008000800000001, 0x1000000000000001
	.quad 0x0000020800002000, 0x0008000800000000
	.quad 0x0000020000002000, 0x0008020800002000
	.quad 0x1008020800002001, 0x1000000800000001
	.quad 0x0008000800000000, 0x1008000000000001
	.quad 0x0008020000002000, 0x1008020800002001
	.quad 0x1000000800000001, 0x0000000000000000
	.quad 0x0000000000000000, 0x0008020000002000
	.quad 0x0000020800002000, 0x0008000800000000
	.quad 0x1008000800000001, 0x1000000000000001
	.quad 0x1008020000002001, 0x1000020800002001
	.quad 0x1000020800002001, 0x0000000800000000
	.quad 0x1008020800002001, 0x1000000800000001
	.quad 0x1000000000000001, 0x0000020000002000
	.quad 0x1008000000000001, 0x1000020000002001
	.quad 0x0008020800002000, 0x1008000800000001
	.quad 0x1000020000002001, 0x0000020800002000
	.quad 0x0008000000000000, 0x1008020000002001
	.quad 0x0000000800000000, 0x0008000000000000
	.quad 0x0000020000002000, 0x0008020800002000
.L_s5:
	.quad 0x0000001000000100, 0x0020001002080100
	.quad 0x0020000002080000, 0x0420001002000100
	.quad 0x0000000000080000, 0x0000001000000100
	.quad 0x0400000000000000, 0x0020000002080000
	.quad 0x0400001000080100, 0x0000000000080000
	.quad 0x0020001002000100, 0x0400001000080100
	.quad 0x0420001002000100, 0x0420000002080000
	.quad 0x0000001000080100, 0x0400000000000000
	.quad 0x0020000002000000, 0x0400000000080000
	.quad 0x0400000000080000, 0x0000000000000000
	.quad 0x0400001000000100, 0x0420001002080100
	.quad 0x0420001002080100, 0x0020001002000100
	.quad 0x0420000002080000, 0x0400001000000100
	.quad 0x0000000000000000, 0x0420000002000000
	.quad 0x0020001002080100, 0x0020000002000000
	.quad 0x0420000002000000, 0x0000001000080100
	.quad 0x0000000000080000, 0x0420001002000100
	.quad 0x0000001000000100, 0x0020000002000000
	.quad 0x0400000000000000, 0x0020000002080000
	.quad 0x0420001002000100, 0x0400001000080100
	.quad 0x0020001002000100, 0x0400000000000000
	.quad 0x0420000002080000, 0x0020001002080100
	.quad 0x0400001000080100, 0x0000001000000100
	.quad 0x0020000002000000, 0x0420000002080000
	.quad 0x0420001002080100, 0x0000001000080100
	.quad 0x0420000002000000, 0x0420001002080100
	.quad 0x0020000002080000, 0x0000000000000000
	.quad 0x0400000000080000, 0x0420000002000000
	.quad 0x0000001000080100, 0x0020001002000100
	.quad 0x0400001000000100, 0x0000000000080000
	.quad 0x0000000000000000, 0x0400000000080000
	.quad 0x0020001002080100, 0x0400001000000100
.L_s6:
	.quad 0x0200000120000010, 0x0204000020000000
	.quad 0x0000040000000000, 0x0204040120000010
	.quad 0x0204000020000000, 0x0000000100000010
	.quad 0x0204040120000010, 0x0004000000000000
	.quad 0x0200040020000000, 0x0004040100000010
	.quad 0x0004000000000000, 0x0200000120000010
	.quad 0x0004000100000010, 0x0200040020000000
	.quad 0x0200000020000000, 0x0000040100000010
	.quad 0x0000000000000000, 0x0004000100000010
	.quad 0x0200040120000010, 0x0000040000000000
	.quad 0x0004040000000000, 0x0200040120000010
	.quad 0x0000000100000010, 0x0204000120000010
	.quad 0x0204000120000010, 0x0000000000000000
	.quad 0x0004040100000010, 0x0204040020000000
	.quad 0x0000040100000010, 0x0004040000000000
	.quad 0x0204040020000000, 0x0200000020000000
	.quad 0x0200040020000000, 0x0000000100000010
	.quad 0x0204000120000010, 0x0004040000000000
	.quad 0x0204040120000010, 0x0004000000000000
	.quad 0x0000040100000010, 0x0200000120000010
	.quad 0x0004000000000000, 0x0200040020000000
	.quad 0x0200000020000000, 0x0000040100000010
	.quad 0x0200000120000010, 0x0204040120000010
	.quad 0x0004040000000000, 0x0204000020000000
	.quad 0x0004040100000010, 0x0204040020000000
	.quad 0x0000000000000000, 0x0204000120000010
	.quad 0x0000000100000010, 0x0000040000000000
	.quad 0x0204000020000000, 0x0004040100000010
	.quad 0x0000040000000000, 0x0004000100000010
	.quad 0x0200040120000010, 0x0000000000000000
	.quad 0x0204040020000000, 0x0200000020000000
	.quad 0x0004000100000010, 0x0200040120000010
.L_s7:
	.quad 0x0002000000200000, 0x2002000004200002
	.quad 0x2000000004000802, 0x0000000000000000
	.quad 0x0000000000000800, 0x2000000004000802
	.quad 0x2002000000200802, 0x0002000004200800
	.quad 0x2002000004200802, 0x0002000000200000
	.quad 0x0000000000000000, 0x2000000004000002
	.quad 0x2000000000000002, 0x0000000004000000
	.quad 0x2002000004200002, 0x2000000000000802
	.quad 0x0000000004000800, 0x2002000000200802
	.quad 0x2002000000200002, 0x0000000004000800
	.quad 0x2000000004000002, 0x0002000004200000
	.quad 0x0002000004200800, 0x2002000000200002
	.quad 0x0002000004200000, 0x0000000000000800
	.quad 0x2000000000000802, 0x2002000004200802
	.quad 0x0002000000200800, 0x2000000000000002
	.quad 0x0000000004000000, 0x0002000000200800
	.quad 0x0000000004000000, 0x0002000000200800
	.quad 0x0002000000200000, 0x2000000004000802
	.quad 0x2000000004000802, 0x2002000004200002
	.quad 0x2002000004200002, 0x2000000000000002
	.quad 0x2002000000200002, 0x0000000004000000
	.quad 0x0000000004000800, 0x0002000000200000
	.quad 0x0002000004200800, 0x2000000000000802
	.quad 0x2002000000200802, 0x0002000004200800
	.quad 0x2000000000000802, 0x2000000004000002
	.quad 0x2002000004200802, 0x0002000004200000
	.quad 0x0002000000200800, 0x0000000000000000
	.quad 0x2000000000000002, 0x2002000004200802
	.quad 0x0000000000000000, 0x2002000000200802
	.quad 0x0002000004200000, 0x0000000000000800
	.quad 0x2000000004000002, 0x0000000004000800
	.quad 0x0000000000000800, 0x2002000000200002
.L_s8:
	.quad 0x0100010410001000, 0x0000010000001000
	.quad 0x0000000000040000, 0x0100010410041000
	.quad 0x0100000010000000, 0x0100010410001000
	.quad 0x0000000400000000, 0x0100000010000000
	.quad 0x0000000400040000, 0x0100000010040000
	.quad 0x0100010410041000, 0x0000010000041000
	.quad 0x0100010010041000, 0x0000010400041000
	.quad 0x0000010000001000, 0x0000000400000000
	.quad 0x0100000010040000, 0x0100000410000000
	.quad 0x0100010010001000, 0x0000010400001000
	.quad 0x0000010000041000, 0x0000000400040000
	.quad 0x0100000410040000, 0x0100010010041000
	.quad 0x0000010400001000, 0x0000000000000000
	.quad 0x0000000000000000, 0x0100000410040000
	.quad 0x0100000410000000, 0x0100010010001000
	.quad 0x0000010400041000, 0x0000000000040000
	.quad 0x0000010400041000, 0x0000000000040000
	.quad 0x0100010010041000, 0x0000010000001000
	.quad 0x0000000400000000, 0x0100000410040000
	.quad 0x0000010000001000, 0x0000010400041000
	.quad 0x0100010010001000, 0x0000000400000000
	.quad 0x0100000410000000, 0x0100000010040000
	.quad 0x0100000410040000, 0x0100000010000000
	.quad 0x0000000000040000, 0x0100010410001000
	.quad 0x0000000000000000, 0x0100010410041000
	.quad 0x0000000400040000, 0x0100000410000000
	.quad 0x0100000010040000, 0x0100010010001000
	.quad 0x0100010410001000, 0x0000000000000000
	.quad 0x0100010410041000, 0x0000010000041000
	.quad 0x0000010000041000, 0x0000010400001000
	.quad 0x0000010400001000, 0x0000000400040000
	.quad 0x0100000010000000, 0x0100010010041000
