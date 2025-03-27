/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Camellia Cipher Algorithm (x86_64)
 *
 * Copyright (C) 2012 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 */

#include <linux/linkage.h>
#include <linux/cfi_types.h>

.file "camellia-x86_64-asm_64.S"
.text

.extern camellia_sp10011110;
.extern camellia_sp22000222;
.extern camellia_sp03303033;
.extern camellia_sp00444404;
.extern camellia_sp02220222;
.extern camellia_sp30333033;
.extern camellia_sp44044404;
.extern camellia_sp11101110;

#define sp10011110 camellia_sp10011110
#define sp22000222 camellia_sp22000222
#define sp03303033 camellia_sp03303033
#define sp00444404 camellia_sp00444404
#define sp02220222 camellia_sp02220222
#define sp30333033 camellia_sp30333033
#define sp44044404 camellia_sp44044404
#define sp11101110 camellia_sp11101110

#define CAMELLIA_TABLE_BYTE_LEN 272

/* struct camellia_ctx: */
#define key_table 0
#define key_length CAMELLIA_TABLE_BYTE_LEN

/* register macros */
#define CTX %rdi
#define RIO %rsi
#define RIOd %esi

#define RAB0 %rax
#define RCD0 %rcx
#define RAB1 %rbx
#define RCD1 %rdx

#define RAB0d %eax
#define RCD0d %ecx
#define RAB1d %ebx
#define RCD1d %edx

#define RAB0bl %al
#define RCD0bl %cl
#define RAB1bl %bl
#define RCD1bl %dl

#define RAB0bh %ah
#define RCD0bh %ch
#define RAB1bh %bh
#define RCD1bh %dh

#define RT0 %rsi
#define RT1 %r12
#define RT2 %r8

#define RT0d %esi
#define RT1d %r12d
#define RT2d %r8d

#define RT2bl %r8b

#define RXOR %r9
#define RR12 %r10
#define RDST %r11

#define RXORd %r9d
#define RXORbl %r9b

#define xor2ror16(T0, T1, tmp1, tmp2, ab, dst) \
	leaq T0(%rip), 			tmp1; \
	movzbl ab ## bl,		tmp2 ## d; \
	xorq (tmp1, tmp2, 8),		dst; \
	leaq T1(%rip), 			tmp2; \
	movzbl ab ## bh,		tmp1 ## d; \
	rorq $16,			ab; \
	xorq (tmp2, tmp1, 8),		dst;

/**********************************************************************
  1-way camellia
 **********************************************************************/
#define roundsm(ab, subkey, cd) \
	movq (key_table + ((subkey) * 2) * 4)(CTX),	RT2; \
	\
	xor2ror16(sp00444404, sp03303033, RT0, RT1, ab ## 0, cd ## 0); \
	xor2ror16(sp22000222, sp10011110, RT0, RT1, ab ## 0, RT2); \
	xor2ror16(sp11101110, sp44044404, RT0, RT1, ab ## 0, cd ## 0); \
	xor2ror16(sp30333033, sp02220222, RT0, RT1, ab ## 0, RT2); \
	\
	xorq RT2,					cd ## 0;

#define fls(l, r, kl, kr) \
	movl (key_table + ((kl) * 2) * 4)(CTX),		RT0d; \
	andl l ## 0d,					RT0d; \
	roll $1,					RT0d; \
	shlq $32,					RT0; \
	xorq RT0,					l ## 0; \
	movq (key_table + ((kr) * 2) * 4)(CTX),		RT1; \
	orq r ## 0,					RT1; \
	shrq $32,					RT1; \
	xorq RT1,					r ## 0; \
	\
	movq (key_table + ((kl) * 2) * 4)(CTX),		RT2; \
	orq l ## 0,					RT2; \
	shrq $32,					RT2; \
	xorq RT2,					l ## 0; \
	movl (key_table + ((kr) * 2) * 4)(CTX),		RT0d; \
	andl r ## 0d,					RT0d; \
	roll $1,					RT0d; \
	shlq $32,					RT0; \
	xorq RT0,					r ## 0;

#define enc_rounds(i) \
	roundsm(RAB, i + 2, RCD); \
	roundsm(RCD, i + 3, RAB); \
	roundsm(RAB, i + 4, RCD); \
	roundsm(RCD, i + 5, RAB); \
	roundsm(RAB, i + 6, RCD); \
	roundsm(RCD, i + 7, RAB);

#define enc_fls(i) \
	fls(RAB, RCD, i + 0, i + 1);

#define enc_inpack() \
	movq (RIO),			RAB0; \
	bswapq				RAB0; \
	rolq $32,			RAB0; \
	movq 4*2(RIO),			RCD0; \
	bswapq				RCD0; \
	rorq $32,			RCD0; \
	xorq key_table(CTX),		RAB0;

#define enc_outunpack(op, max) \
	xorq key_table(CTX, max, 8),	RCD0; \
	rorq $32,			RCD0; \
	bswapq				RCD0; \
	op ## q RCD0,			(RIO); \
	rolq $32,			RAB0; \
	bswapq				RAB0; \
	op ## q RAB0,			4*2(RIO);

#define dec_rounds(i) \
	roundsm(RAB, i + 7, RCD); \
	roundsm(RCD, i + 6, RAB); \
	roundsm(RAB, i + 5, RCD); \
	roundsm(RCD, i + 4, RAB); \
	roundsm(RAB, i + 3, RCD); \
	roundsm(RCD, i + 2, RAB);

#define dec_fls(i) \
	fls(RAB, RCD, i + 1, i + 0);

#define dec_inpack(max) \
	movq (RIO),			RAB0; \
	bswapq				RAB0; \
	rolq $32,			RAB0; \
	movq 4*2(RIO),			RCD0; \
	bswapq				RCD0; \
	rorq $32,			RCD0; \
	xorq key_table(CTX, max, 8),	RAB0;

#define dec_outunpack() \
	xorq key_table(CTX),		RCD0; \
	rorq $32,			RCD0; \
	bswapq				RCD0; \
	movq RCD0,			(RIO); \
	rolq $32,			RAB0; \
	bswapq				RAB0; \
	movq RAB0,			4*2(RIO);

SYM_TYPED_FUNC_START(__camellia_enc_blk)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: bool xor
	 */
	movq %r12, RR12;

	movq %rcx, RXOR;
	movq %rsi, RDST;
	movq %rdx, RIO;

	enc_inpack();

	enc_rounds(0);
	enc_fls(8);
	enc_rounds(8);
	enc_fls(16);
	enc_rounds(16);
	movl $24, RT1d; /* max */

	cmpb $16, key_length(CTX);
	je .L__enc_done;

	enc_fls(24);
	enc_rounds(24);
	movl $32, RT1d; /* max */

.L__enc_done:
	testb RXORbl, RXORbl;
	movq RDST, RIO;

	jnz .L__enc_xor;

	enc_outunpack(mov, RT1);

	movq RR12, %r12;
	RET;

.L__enc_xor:
	enc_outunpack(xor, RT1);

	movq RR12, %r12;
	RET;
SYM_FUNC_END(__camellia_enc_blk)

SYM_TYPED_FUNC_START(camellia_dec_blk)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */
	cmpl $16, key_length(CTX);
	movl $32, RT2d;
	movl $24, RXORd;
	cmovel RXORd, RT2d; /* max */

	movq %r12, RR12;
	movq %rsi, RDST;
	movq %rdx, RIO;

	dec_inpack(RT2);

	cmpb $24, RT2bl;
	je .L__dec_rounds16;

	dec_rounds(24);
	dec_fls(24);

.L__dec_rounds16:
	dec_rounds(16);
	dec_fls(16);
	dec_rounds(8);
	dec_fls(8);
	dec_rounds(0);

	movq RDST, RIO;

	dec_outunpack();

	movq RR12, %r12;
	RET;
SYM_FUNC_END(camellia_dec_blk)

/**********************************************************************
  2-way camellia
 **********************************************************************/
#define roundsm2(ab, subkey, cd) \
	movq (key_table + ((subkey) * 2) * 4)(CTX),	RT2; \
	xorq RT2,					cd ## 1; \
	\
	xor2ror16(sp00444404, sp03303033, RT0, RT1, ab ## 0, cd ## 0); \
	xor2ror16(sp22000222, sp10011110, RT0, RT1, ab ## 0, RT2); \
	xor2ror16(sp11101110, sp44044404, RT0, RT1, ab ## 0, cd ## 0); \
	xor2ror16(sp30333033, sp02220222, RT0, RT1, ab ## 0, RT2); \
	\
		xor2ror16(sp00444404, sp03303033, RT0, RT1, ab ## 1, cd ## 1); \
		xorq RT2,					cd ## 0; \
		xor2ror16(sp22000222, sp10011110, RT0, RT1, ab ## 1, cd ## 1); \
		xor2ror16(sp11101110, sp44044404, RT0, RT1, ab ## 1, cd ## 1); \
		xor2ror16(sp30333033, sp02220222, RT0, RT1, ab ## 1, cd ## 1);

#define fls2(l, r, kl, kr) \
	movl (key_table + ((kl) * 2) * 4)(CTX),		RT0d; \
	andl l ## 0d,					RT0d; \
	roll $1,					RT0d; \
	shlq $32,					RT0; \
	xorq RT0,					l ## 0; \
	movq (key_table + ((kr) * 2) * 4)(CTX),		RT1; \
	orq r ## 0,					RT1; \
	shrq $32,					RT1; \
	xorq RT1,					r ## 0; \
	\
		movl (key_table + ((kl) * 2) * 4)(CTX),		RT2d; \
		andl l ## 1d,					RT2d; \
		roll $1,					RT2d; \
		shlq $32,					RT2; \
		xorq RT2,					l ## 1; \
		movq (key_table + ((kr) * 2) * 4)(CTX),		RT0; \
		orq r ## 1,					RT0; \
		shrq $32,					RT0; \
		xorq RT0,					r ## 1; \
	\
	movq (key_table + ((kl) * 2) * 4)(CTX),		RT1; \
	orq l ## 0,					RT1; \
	shrq $32,					RT1; \
	xorq RT1,					l ## 0; \
	movl (key_table + ((kr) * 2) * 4)(CTX),		RT2d; \
	andl r ## 0d,					RT2d; \
	roll $1,					RT2d; \
	shlq $32,					RT2; \
	xorq RT2,					r ## 0; \
	\
		movq (key_table + ((kl) * 2) * 4)(CTX),		RT0; \
		orq l ## 1,					RT0; \
		shrq $32,					RT0; \
		xorq RT0,					l ## 1; \
		movl (key_table + ((kr) * 2) * 4)(CTX),		RT1d; \
		andl r ## 1d,					RT1d; \
		roll $1,					RT1d; \
		shlq $32,					RT1; \
		xorq RT1,					r ## 1;

#define enc_rounds2(i) \
	roundsm2(RAB, i + 2, RCD); \
	roundsm2(RCD, i + 3, RAB); \
	roundsm2(RAB, i + 4, RCD); \
	roundsm2(RCD, i + 5, RAB); \
	roundsm2(RAB, i + 6, RCD); \
	roundsm2(RCD, i + 7, RAB);

#define enc_fls2(i) \
	fls2(RAB, RCD, i + 0, i + 1);

#define enc_inpack2() \
	movq (RIO),			RAB0; \
	bswapq				RAB0; \
	rorq $32,			RAB0; \
	movq 4*2(RIO),			RCD0; \
	bswapq				RCD0; \
	rolq $32,			RCD0; \
	xorq key_table(CTX),		RAB0; \
	\
		movq 8*2(RIO),			RAB1; \
		bswapq				RAB1; \
		rorq $32,			RAB1; \
		movq 12*2(RIO),			RCD1; \
		bswapq				RCD1; \
		rolq $32,			RCD1; \
		xorq key_table(CTX),		RAB1;

#define enc_outunpack2(op, max) \
	xorq key_table(CTX, max, 8),	RCD0; \
	rolq $32,			RCD0; \
	bswapq				RCD0; \
	op ## q RCD0,			(RIO); \
	rorq $32,			RAB0; \
	bswapq				RAB0; \
	op ## q RAB0,			4*2(RIO); \
	\
		xorq key_table(CTX, max, 8),	RCD1; \
		rolq $32,			RCD1; \
		bswapq				RCD1; \
		op ## q RCD1,			8*2(RIO); \
		rorq $32,			RAB1; \
		bswapq				RAB1; \
		op ## q RAB1,			12*2(RIO);

#define dec_rounds2(i) \
	roundsm2(RAB, i + 7, RCD); \
	roundsm2(RCD, i + 6, RAB); \
	roundsm2(RAB, i + 5, RCD); \
	roundsm2(RCD, i + 4, RAB); \
	roundsm2(RAB, i + 3, RCD); \
	roundsm2(RCD, i + 2, RAB);

#define dec_fls2(i) \
	fls2(RAB, RCD, i + 1, i + 0);

#define dec_inpack2(max) \
	movq (RIO),			RAB0; \
	bswapq				RAB0; \
	rorq $32,			RAB0; \
	movq 4*2(RIO),			RCD0; \
	bswapq				RCD0; \
	rolq $32,			RCD0; \
	xorq key_table(CTX, max, 8),	RAB0; \
	\
		movq 8*2(RIO),			RAB1; \
		bswapq				RAB1; \
		rorq $32,			RAB1; \
		movq 12*2(RIO),			RCD1; \
		bswapq				RCD1; \
		rolq $32,			RCD1; \
		xorq key_table(CTX, max, 8),	RAB1;

#define dec_outunpack2() \
	xorq key_table(CTX),		RCD0; \
	rolq $32,			RCD0; \
	bswapq				RCD0; \
	movq RCD0,			(RIO); \
	rorq $32,			RAB0; \
	bswapq				RAB0; \
	movq RAB0,			4*2(RIO); \
	\
		xorq key_table(CTX),		RCD1; \
		rolq $32,			RCD1; \
		bswapq				RCD1; \
		movq RCD1,			8*2(RIO); \
		rorq $32,			RAB1; \
		bswapq				RAB1; \
		movq RAB1,			12*2(RIO);

SYM_TYPED_FUNC_START(__camellia_enc_blk_2way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: bool xor
	 */
	pushq %rbx;

	movq %r12, RR12;
	movq %rcx, RXOR;
	movq %rsi, RDST;
	movq %rdx, RIO;

	enc_inpack2();

	enc_rounds2(0);
	enc_fls2(8);
	enc_rounds2(8);
	enc_fls2(16);
	enc_rounds2(16);
	movl $24, RT2d; /* max */

	cmpb $16, key_length(CTX);
	je .L__enc2_done;

	enc_fls2(24);
	enc_rounds2(24);
	movl $32, RT2d; /* max */

.L__enc2_done:
	test RXORbl, RXORbl;
	movq RDST, RIO;
	jnz .L__enc2_xor;

	enc_outunpack2(mov, RT2);

	movq RR12, %r12;
	popq %rbx;
	RET;

.L__enc2_xor:
	enc_outunpack2(xor, RT2);

	movq RR12, %r12;
	popq %rbx;
	RET;
SYM_FUNC_END(__camellia_enc_blk_2way)

SYM_TYPED_FUNC_START(camellia_dec_blk_2way)
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */
	cmpl $16, key_length(CTX);
	movl $32, RT2d;
	movl $24, RXORd;
	cmovel RXORd, RT2d; /* max */

	movq %rbx, RXOR;
	movq %r12, RR12;
	movq %rsi, RDST;
	movq %rdx, RIO;

	dec_inpack2(RT2);

	cmpb $24, RT2bl;
	je .L__dec2_rounds16;

	dec_rounds2(24);
	dec_fls2(24);

.L__dec2_rounds16:
	dec_rounds2(16);
	dec_fls2(16);
	dec_rounds2(8);
	dec_fls2(8);
	dec_rounds2(0);

	movq RDST, RIO;

	dec_outunpack2();

	movq RR12, %r12;
	movq RXOR, %rbx;
	RET;
SYM_FUNC_END(camellia_dec_blk_2way)
