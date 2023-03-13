/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Twofish Cipher 3-way parallel algorithm (x86_64)
 *
 * Copyright (C) 2011 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 */

#include <linux/linkage.h>

.file "twofish-x86_64-asm-3way.S"
.text

/* structure of crypto context */
#define s0	0
#define s1	1024
#define s2	2048
#define s3	3072
#define w	4096
#define k	4128

/**********************************************************************
  3-way twofish
 **********************************************************************/
#define CTX %rdi
#define RIO %rdx

#define RAB0 %rax
#define RAB1 %rbx
#define RAB2 %rcx

#define RAB0d %eax
#define RAB1d %ebx
#define RAB2d %ecx

#define RAB0bh %ah
#define RAB1bh %bh
#define RAB2bh %ch

#define RAB0bl %al
#define RAB1bl %bl
#define RAB2bl %cl

#define CD0 0x0(%rsp)
#define CD1 0x8(%rsp)
#define CD2 0x10(%rsp)

# used only before/after all rounds
#define RCD0 %r8
#define RCD1 %r9
#define RCD2 %r10

# used only during rounds
#define RX0 %r8
#define RX1 %r9
#define RX2 %r10

#define RX0d %r8d
#define RX1d %r9d
#define RX2d %r10d

#define RY0 %r11
#define RY1 %r12
#define RY2 %r13

#define RY0d %r11d
#define RY1d %r12d
#define RY2d %r13d

#define RT0 %rdx
#define RT1 %rsi

#define RT0d %edx
#define RT1d %esi

#define RT1bl %sil

#define do16bit_ror(rot, op1, op2, T0, T1, tmp1, tmp2, ab, dst) \
	movzbl ab ## bl,		tmp2 ## d; \
	movzbl ab ## bh,		tmp1 ## d; \
	rorq $(rot),			ab; \
	op1##l T0(CTX, tmp2, 4),	dst ## d; \
	op2##l T1(CTX, tmp1, 4),	dst ## d;

#define swap_ab_with_cd(ab, cd, tmp)	\
	movq cd, tmp;			\
	movq ab, cd;			\
	movq tmp, ab;

/*
 * Combined G1 & G2 function. Reordered with help of rotates to have moves
 * at begining.
 */
#define g1g2_3(ab, cd, Tx0, Tx1, Tx2, Tx3, Ty0, Ty1, Ty2, Ty3, x, y) \
	/* G1,1 && G2,1 */ \
	do16bit_ror(32, mov, xor, Tx0, Tx1, RT0, x ## 0, ab ## 0, x ## 0); \
	do16bit_ror(48, mov, xor, Ty1, Ty2, RT0, y ## 0, ab ## 0, y ## 0); \
	\
	do16bit_ror(32, mov, xor, Tx0, Tx1, RT0, x ## 1, ab ## 1, x ## 1); \
	do16bit_ror(48, mov, xor, Ty1, Ty2, RT0, y ## 1, ab ## 1, y ## 1); \
	\
	do16bit_ror(32, mov, xor, Tx0, Tx1, RT0, x ## 2, ab ## 2, x ## 2); \
	do16bit_ror(48, mov, xor, Ty1, Ty2, RT0, y ## 2, ab ## 2, y ## 2); \
	\
	/* G1,2 && G2,2 */ \
	do16bit_ror(32, xor, xor, Tx2, Tx3, RT0, RT1, ab ## 0, x ## 0); \
	do16bit_ror(16, xor, xor, Ty3, Ty0, RT0, RT1, ab ## 0, y ## 0); \
	swap_ab_with_cd(ab ## 0, cd ## 0, RT0); \
	\
	do16bit_ror(32, xor, xor, Tx2, Tx3, RT0, RT1, ab ## 1, x ## 1); \
	do16bit_ror(16, xor, xor, Ty3, Ty0, RT0, RT1, ab ## 1, y ## 1); \
	swap_ab_with_cd(ab ## 1, cd ## 1, RT0); \
	\
	do16bit_ror(32, xor, xor, Tx2, Tx3, RT0, RT1, ab ## 2, x ## 2); \
	do16bit_ror(16, xor, xor, Ty3, Ty0, RT0, RT1, ab ## 2, y ## 2); \
	swap_ab_with_cd(ab ## 2, cd ## 2, RT0);

#define enc_round_end(ab, x, y, n) \
	addl y ## d,			x ## d; \
	addl x ## d,			y ## d; \
	addl k+4*(2*(n))(CTX),		x ## d; \
	xorl ab ## d,			x ## d; \
	addl k+4*(2*(n)+1)(CTX),	y ## d; \
	shrq $32,			ab; \
	roll $1,			ab ## d; \
	xorl y ## d,			ab ## d; \
	shlq $32,			ab; \
	rorl $1,			x ## d; \
	orq x,				ab;

#define dec_round_end(ba, x, y, n) \
	addl y ## d,			x ## d; \
	addl x ## d,			y ## d; \
	addl k+4*(2*(n))(CTX),		x ## d; \
	addl k+4*(2*(n)+1)(CTX),	y ## d; \
	xorl ba ## d,			y ## d; \
	shrq $32,			ba; \
	roll $1,			ba ## d; \
	xorl x ## d,			ba ## d; \
	shlq $32,			ba; \
	rorl $1,			y ## d; \
	orq y,				ba;

#define encrypt_round3(ab, cd, n) \
	g1g2_3(ab, cd, s0, s1, s2, s3, s0, s1, s2, s3, RX, RY); \
	\
	enc_round_end(ab ## 0, RX0, RY0, n); \
	enc_round_end(ab ## 1, RX1, RY1, n); \
	enc_round_end(ab ## 2, RX2, RY2, n);

#define decrypt_round3(ba, dc, n) \
	g1g2_3(ba, dc, s1, s2, s3, s0, s3, s0, s1, s2, RY, RX); \
	\
	dec_round_end(ba ## 0, RX0, RY0, n); \
	dec_round_end(ba ## 1, RX1, RY1, n); \
	dec_round_end(ba ## 2, RX2, RY2, n);

#define encrypt_cycle3(ab, cd, n) \
	encrypt_round3(ab, cd, n*2); \
	encrypt_round3(ab, cd, (n*2)+1);

#define decrypt_cycle3(ba, dc, n) \
	decrypt_round3(ba, dc, (n*2)+1); \
	decrypt_round3(ba, dc, (n*2));

#define push_cd()	\
	pushq RCD2;	\
	pushq RCD1;	\
	pushq RCD0;

#define pop_cd()	\
	popq RCD0;	\
	popq RCD1;	\
	popq RCD2;

#define inpack3(in, n, xy, m) \
	movq 4*(n)(in),			xy ## 0; \
	xorq w+4*m(CTX),		xy ## 0; \
	\
	movq 4*(4+(n))(in),		xy ## 1; \
	xorq w+4*m(CTX),		xy ## 1; \
	\
	movq 4*(8+(n))(in),		xy ## 2; \
	xorq w+4*m(CTX),		xy ## 2;

#define outunpack3(op, out, n, xy, m) \
	xorq w+4*m(CTX),		xy ## 0; \
	op ## q xy ## 0,		4*(n)(out); \
	\
	xorq w+4*m(CTX),		xy ## 1; \
	op ## q xy ## 1,		4*(4+(n))(out); \
	\
	xorq w+4*m(CTX),		xy ## 2; \
	op ## q xy ## 2,		4*(8+(n))(out);

#define inpack_enc3() \
	inpack3(RIO, 0, RAB, 0); \
	inpack3(RIO, 2, RCD, 2);

#define outunpack_enc3(op) \
	outunpack3(op, RIO, 2, RAB, 6); \
	outunpack3(op, RIO, 0, RCD, 4);

#define inpack_dec3() \
	inpack3(RIO, 0, RAB, 4); \
	rorq $32,			RAB0; \
	rorq $32,			RAB1; \
	rorq $32,			RAB2; \
	inpack3(RIO, 2, RCD, 6); \
	rorq $32,			RCD0; \
	rorq $32,			RCD1; \
	rorq $32,			RCD2;

#define outunpack_dec3() \
	rorq $32,			RCD0; \
	rorq $32,			RCD1; \
	rorq $32,			RCD2; \
	outunpack3(mov, RIO, 0, RCD, 0); \
	rorq $32,			RAB0; \
	rorq $32,			RAB1; \
	rorq $32,			RAB2; \
	outunpack3(mov, RIO, 2, RAB, 2);

SYM_FUNC_START(__twofish_enc_blk_3way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src, RIO
	 *	%rcx: bool, if true: xor output
	 */
	pushq %r13;
	pushq %r12;
	pushq %rbx;

	pushq %rcx; /* bool xor */
	pushq %rsi; /* dst */

	inpack_enc3();

	push_cd();
	encrypt_cycle3(RAB, CD, 0);
	encrypt_cycle3(RAB, CD, 1);
	encrypt_cycle3(RAB, CD, 2);
	encrypt_cycle3(RAB, CD, 3);
	encrypt_cycle3(RAB, CD, 4);
	encrypt_cycle3(RAB, CD, 5);
	encrypt_cycle3(RAB, CD, 6);
	encrypt_cycle3(RAB, CD, 7);
	pop_cd();

	popq RIO; /* dst */
	popq RT1; /* bool xor */

	testb RT1bl, RT1bl;
	jnz .L__enc_xor3;

	outunpack_enc3(mov);

	popq %rbx;
	popq %r12;
	popq %r13;
	RET;

.L__enc_xor3:
	outunpack_enc3(xor);

	popq %rbx;
	popq %r12;
	popq %r13;
	RET;
SYM_FUNC_END(__twofish_enc_blk_3way)

SYM_FUNC_START(twofish_dec_blk_3way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src, RIO
	 */
	pushq %r13;
	pushq %r12;
	pushq %rbx;

	pushq %rsi; /* dst */

	inpack_dec3();

	push_cd();
	decrypt_cycle3(RAB, CD, 7);
	decrypt_cycle3(RAB, CD, 6);
	decrypt_cycle3(RAB, CD, 5);
	decrypt_cycle3(RAB, CD, 4);
	decrypt_cycle3(RAB, CD, 3);
	decrypt_cycle3(RAB, CD, 2);
	decrypt_cycle3(RAB, CD, 1);
	decrypt_cycle3(RAB, CD, 0);
	pop_cd();

	popq RIO; /* dst */

	outunpack_dec3();

	popq %rbx;
	popq %r12;
	popq %r13;
	RET;
SYM_FUNC_END(twofish_dec_blk_3way)
