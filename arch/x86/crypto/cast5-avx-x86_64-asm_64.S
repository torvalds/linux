/*
 * Cast5 Cipher 16-way parallel algorithm (AVX/x86_64)
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
 *
 * Copyright © 2012 Jussi Kivilinna <jussi.kivilinna@mbnet.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307
 * USA
 *
 */

.file "cast5-avx-x86_64-asm_64.S"

.extern cast5_s1
.extern cast5_s2
.extern cast5_s3
.extern cast5_s4

/* structure of crypto context */
#define km	0
#define kr	(16*4)
#define rr	((16*4)+16)

/* s-boxes */
#define s1	cast5_s1
#define s2	cast5_s2
#define s3	cast5_s3
#define s4	cast5_s4

/**********************************************************************
  16-way AVX cast5
 **********************************************************************/
#define CTX %rdi

#define RL1 %xmm0
#define RR1 %xmm1
#define RL2 %xmm2
#define RR2 %xmm3
#define RL3 %xmm4
#define RR3 %xmm5
#define RL4 %xmm6
#define RR4 %xmm7

#define RX %xmm8

#define RKM  %xmm9
#define RKR  %xmm10
#define RKRF %xmm11
#define RKRR %xmm12

#define R32  %xmm13
#define R1ST %xmm14

#define RTMP %xmm15

#define RID1  %rbp
#define RID1d %ebp
#define RID2  %rsi
#define RID2d %esi

#define RGI1   %rdx
#define RGI1bl %dl
#define RGI1bh %dh
#define RGI2   %rcx
#define RGI2bl %cl
#define RGI2bh %ch

#define RGI3   %rax
#define RGI3bl %al
#define RGI3bh %ah
#define RGI4   %rbx
#define RGI4bl %bl
#define RGI4bh %bh

#define RFS1  %r8
#define RFS1d %r8d
#define RFS2  %r9
#define RFS2d %r9d
#define RFS3  %r10
#define RFS3d %r10d


#define lookup_32bit(src, dst, op1, op2, op3, interleave_op, il_reg) \
	movzbl		src ## bh,     RID1d;    \
	movzbl		src ## bl,     RID2d;    \
	shrq $16,	src;                     \
	movl		s1(, RID1, 4), dst ## d; \
	op1		s2(, RID2, 4), dst ## d; \
	movzbl		src ## bh,     RID1d;    \
	movzbl		src ## bl,     RID2d;    \
	interleave_op(il_reg);			 \
	op2		s3(, RID1, 4), dst ## d; \
	op3		s4(, RID2, 4), dst ## d;

#define dummy(d) /* do nothing */

#define shr_next(reg) \
	shrq $16,	reg;

#define F_head(a, x, gi1, gi2, op0) \
	op0	a,	RKM,  x;                 \
	vpslld	RKRF,	x,    RTMP;              \
	vpsrld	RKRR,	x,    x;                 \
	vpor	RTMP,	x,    x;                 \
	\
	vmovq		x,    gi1;               \
	vpextrq $1,	x,    gi2;

#define F_tail(a, x, gi1, gi2, op1, op2, op3) \
	lookup_32bit(##gi1, RFS1, op1, op2, op3, shr_next, ##gi1); \
	lookup_32bit(##gi2, RFS3, op1, op2, op3, shr_next, ##gi2); \
	\
	lookup_32bit(##gi1, RFS2, op1, op2, op3, dummy, none);     \
	shlq $32,	RFS2;                                      \
	orq		RFS1, RFS2;                                \
	lookup_32bit(##gi2, RFS1, op1, op2, op3, dummy, none);     \
	shlq $32,	RFS1;                                      \
	orq		RFS1, RFS3;                                \
	\
	vmovq		RFS2, x;                                   \
	vpinsrq $1,	RFS3, x, x;

#define F_2(a1, b1, a2, b2, op0, op1, op2, op3) \
	F_head(b1, RX, RGI1, RGI2, op0);              \
	F_head(b2, RX, RGI3, RGI4, op0);              \
	\
	F_tail(b1, RX, RGI1, RGI2, op1, op2, op3);    \
	F_tail(b2, RTMP, RGI3, RGI4, op1, op2, op3);  \
	\
	vpxor		a1, RX,   a1;                 \
	vpxor		a2, RTMP, a2;

#define F1_2(a1, b1, a2, b2) \
	F_2(a1, b1, a2, b2, vpaddd, xorl, subl, addl)
#define F2_2(a1, b1, a2, b2) \
	F_2(a1, b1, a2, b2, vpxor, subl, addl, xorl)
#define F3_2(a1, b1, a2, b2) \
	F_2(a1, b1, a2, b2, vpsubd, addl, xorl, subl)

#define subround(a1, b1, a2, b2, f) \
	F ## f ## _2(a1, b1, a2, b2);

#define round(l, r, n, f) \
	vbroadcastss 	(km+(4*n))(CTX), RKM;        \
	vpand		R1ST,            RKR,  RKRF; \
	vpsubq		RKRF,            R32,  RKRR; \
	vpsrldq $1,	RKR,             RKR;        \
	subround(l ## 1, r ## 1, l ## 2, r ## 2, f); \
	subround(l ## 3, r ## 3, l ## 4, r ## 4, f);

#define enc_preload_rkr() \
	vbroadcastss	.L16_mask,                RKR;      \
	/* add 16-bit rotation to key rotations (mod 32) */ \
	vpxor		kr(CTX),                  RKR, RKR;

#define dec_preload_rkr() \
	vbroadcastss	.L16_mask,                RKR;      \
	/* add 16-bit rotation to key rotations (mod 32) */ \
	vpxor		kr(CTX),                  RKR, RKR; \
	vpshufb		.Lbswap128_mask,          RKR, RKR;

#define transpose_2x4(x0, x1, t0, t1) \
	vpunpckldq		x1, x0, t0; \
	vpunpckhdq		x1, x0, t1; \
	\
	vpunpcklqdq		t1, t0, x0; \
	vpunpckhqdq		t1, t0, x1;

#define inpack_blocks(in, x0, x1, t0, t1, rmask) \
	vmovdqu (0*4*4)(in),	x0; \
	vmovdqu (1*4*4)(in),	x1; \
	vpshufb rmask, 	x0,	x0; \
	vpshufb rmask, 	x1,	x1; \
	\
	transpose_2x4(x0, x1, t0, t1)

#define outunpack_blocks(out, x0, x1, t0, t1, rmask) \
	transpose_2x4(x0, x1, t0, t1) \
	\
	vpshufb rmask,	x0, x0;           \
	vpshufb rmask,	x1, x1;           \
	vmovdqu 	x0, (0*4*4)(out); \
	vmovdqu		x1, (1*4*4)(out);

#define outunpack_xor_blocks(out, x0, x1, t0, t1, rmask) \
	transpose_2x4(x0, x1, t0, t1) \
	\
	vpshufb rmask,	x0, x0;               \
	vpshufb rmask,	x1, x1;               \
	vpxor		(0*4*4)(out), x0, x0; \
	vmovdqu 	x0, (0*4*4)(out);     \
	vpxor		(1*4*4)(out), x1, x1; \
	vmovdqu	        x1, (1*4*4)(out);

.data

.align 16
.Lbswap_mask:
	.byte 3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12
.Lbswap128_mask:
	.byte 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
.L16_mask:
	.byte 16, 16, 16, 16
.L32_mask:
	.byte 32, 0, 0, 0
.Lfirst_mask:
	.byte 0x1f, 0, 0, 0

.text

.align 16
.global __cast5_enc_blk_16way
.type   __cast5_enc_blk_16way,@function;

__cast5_enc_blk_16way:
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: bool, if true: xor output
	 */

	pushq %rbp;
	pushq %rbx;
	pushq %rcx;

	vmovdqa .Lbswap_mask, RKM;
	vmovd .Lfirst_mask, R1ST;
	vmovd .L32_mask, R32;
	enc_preload_rkr();

	leaq 1*(2*4*4)(%rdx), %rax;
	inpack_blocks(%rdx, RL1, RR1, RTMP, RX, RKM);
	inpack_blocks(%rax, RL2, RR2, RTMP, RX, RKM);
	leaq 2*(2*4*4)(%rdx), %rax;
	inpack_blocks(%rax, RL3, RR3, RTMP, RX, RKM);
	leaq 3*(2*4*4)(%rdx), %rax;
	inpack_blocks(%rax, RL4, RR4, RTMP, RX, RKM);

	movq %rsi, %r11;

	round(RL, RR, 0, 1);
	round(RR, RL, 1, 2);
	round(RL, RR, 2, 3);
	round(RR, RL, 3, 1);
	round(RL, RR, 4, 2);
	round(RR, RL, 5, 3);
	round(RL, RR, 6, 1);
	round(RR, RL, 7, 2);
	round(RL, RR, 8, 3);
	round(RR, RL, 9, 1);
	round(RL, RR, 10, 2);
	round(RR, RL, 11, 3);

	movzbl rr(CTX), %eax;
	testl %eax, %eax;
	jnz __skip_enc;

	round(RL, RR, 12, 1);
	round(RR, RL, 13, 2);
	round(RL, RR, 14, 3);
	round(RR, RL, 15, 1);

__skip_enc:
	popq %rcx;
	popq %rbx;
	popq %rbp;

	vmovdqa .Lbswap_mask, RKM;
	leaq 1*(2*4*4)(%r11), %rax;

	testb %cl, %cl;
	jnz __enc_xor16;

	outunpack_blocks(%r11, RR1, RL1, RTMP, RX, RKM);
	outunpack_blocks(%rax, RR2, RL2, RTMP, RX, RKM);
	leaq 2*(2*4*4)(%r11), %rax;
	outunpack_blocks(%rax, RR3, RL3, RTMP, RX, RKM);
	leaq 3*(2*4*4)(%r11), %rax;
	outunpack_blocks(%rax, RR4, RL4, RTMP, RX, RKM);

	ret;

__enc_xor16:
	outunpack_xor_blocks(%r11, RR1, RL1, RTMP, RX, RKM);
	outunpack_xor_blocks(%rax, RR2, RL2, RTMP, RX, RKM);
	leaq 2*(2*4*4)(%r11), %rax;
	outunpack_xor_blocks(%rax, RR3, RL3, RTMP, RX, RKM);
	leaq 3*(2*4*4)(%r11), %rax;
	outunpack_xor_blocks(%rax, RR4, RL4, RTMP, RX, RKM);

	ret;

.align 16
.global cast5_dec_blk_16way
.type   cast5_dec_blk_16way,@function;

cast5_dec_blk_16way:
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */

	pushq %rbp;
	pushq %rbx;

	vmovdqa .Lbswap_mask, RKM;
	vmovd .Lfirst_mask, R1ST;
	vmovd .L32_mask, R32;
	dec_preload_rkr();

	leaq 1*(2*4*4)(%rdx), %rax;
	inpack_blocks(%rdx, RL1, RR1, RTMP, RX, RKM);
	inpack_blocks(%rax, RL2, RR2, RTMP, RX, RKM);
	leaq 2*(2*4*4)(%rdx), %rax;
	inpack_blocks(%rax, RL3, RR3, RTMP, RX, RKM);
	leaq 3*(2*4*4)(%rdx), %rax;
	inpack_blocks(%rax, RL4, RR4, RTMP, RX, RKM);

	movq %rsi, %r11;

	movzbl rr(CTX), %eax;
	testl %eax, %eax;
	jnz __skip_dec;

	round(RL, RR, 15, 1);
	round(RR, RL, 14, 3);
	round(RL, RR, 13, 2);
	round(RR, RL, 12, 1);

__dec_tail:
	round(RL, RR, 11, 3);
	round(RR, RL, 10, 2);
	round(RL, RR, 9, 1);
	round(RR, RL, 8, 3);
	round(RL, RR, 7, 2);
	round(RR, RL, 6, 1);
	round(RL, RR, 5, 3);
	round(RR, RL, 4, 2);
	round(RL, RR, 3, 1);
	round(RR, RL, 2, 3);
	round(RL, RR, 1, 2);
	round(RR, RL, 0, 1);

	vmovdqa .Lbswap_mask, RKM;
	popq %rbx;
	popq %rbp;

	leaq 1*(2*4*4)(%r11), %rax;
	outunpack_blocks(%r11, RR1, RL1, RTMP, RX, RKM);
	outunpack_blocks(%rax, RR2, RL2, RTMP, RX, RKM);
	leaq 2*(2*4*4)(%r11), %rax;
	outunpack_blocks(%rax, RR3, RL3, RTMP, RX, RKM);
	leaq 3*(2*4*4)(%r11), %rax;
	outunpack_blocks(%rax, RR4, RL4, RTMP, RX, RKM);

	ret;

__skip_dec:
	vpsrldq $4, RKR, RKR;
	jmp __dec_tail;
