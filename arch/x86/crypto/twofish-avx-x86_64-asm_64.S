/*
 * Twofish Cipher 8-way parallel algorithm (AVX/x86_64)
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

.file "twofish-avx-x86_64-asm_64.S"
.text

/* structure of crypto context */
#define s0	0
#define s1	1024
#define s2	2048
#define s3	3072
#define w	4096
#define k	4128

/**********************************************************************
  8-way AVX twofish
 **********************************************************************/
#define CTX %rdi

#define RA1 %xmm0
#define RB1 %xmm1
#define RC1 %xmm2
#define RD1 %xmm3

#define RA2 %xmm4
#define RB2 %xmm5
#define RC2 %xmm6
#define RD2 %xmm7

#define RX0 %xmm8
#define RY0 %xmm9

#define RX1 %xmm10
#define RY1 %xmm11

#define RK1 %xmm12
#define RK2 %xmm13

#define RT %xmm14
#define RR %xmm15

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

#define RGS1  %r8
#define RGS1d %r8d
#define RGS2  %r9
#define RGS2d %r9d
#define RGS3  %r10
#define RGS3d %r10d


#define lookup_32bit(t0, t1, t2, t3, src, dst, interleave_op, il_reg) \
	movzbl		src ## bl,        RID1d;     \
	movzbl		src ## bh,        RID2d;     \
	shrq $16,	src;                         \
	movl		t0(CTX, RID1, 4), dst ## d;  \
	movl		t1(CTX, RID2, 4), RID2d;     \
	movzbl		src ## bl,        RID1d;     \
	xorl		RID2d,            dst ## d;  \
	movzbl		src ## bh,        RID2d;     \
	interleave_op(il_reg);			     \
	xorl		t2(CTX, RID1, 4), dst ## d;  \
	xorl		t3(CTX, RID2, 4), dst ## d;

#define dummy(d) /* do nothing */

#define shr_next(reg) \
	shrq $16,	reg;

#define G(gi1, gi2, x, t0, t1, t2, t3) \
	lookup_32bit(t0, t1, t2, t3, ##gi1, RGS1, shr_next, ##gi1);  \
	lookup_32bit(t0, t1, t2, t3, ##gi2, RGS3, shr_next, ##gi2);  \
	\
	lookup_32bit(t0, t1, t2, t3, ##gi1, RGS2, dummy, none);      \
	shlq $32,	RGS2;                                        \
	orq		RGS1, RGS2;                                  \
	lookup_32bit(t0, t1, t2, t3, ##gi2, RGS1, dummy, none);      \
	shlq $32,	RGS1;                                        \
	orq		RGS1, RGS3;

#define round_head_2(a, b, x1, y1, x2, y2) \
	vmovq		b ## 1, RGI3;           \
	vpextrq $1,	b ## 1, RGI4;           \
	\
	G(RGI1, RGI2, x1, s0, s1, s2, s3);      \
	vmovq		a ## 2, RGI1;           \
	vpextrq $1,	a ## 2, RGI2;           \
	vmovq		RGS2, x1;               \
	vpinsrq $1,	RGS3, x1, x1;           \
	\
	G(RGI3, RGI4, y1, s1, s2, s3, s0);      \
	vmovq		b ## 2, RGI3;           \
	vpextrq $1,	b ## 2, RGI4;           \
	vmovq		RGS2, y1;               \
	vpinsrq $1,	RGS3, y1, y1;           \
	\
	G(RGI1, RGI2, x2, s0, s1, s2, s3);      \
	vmovq		RGS2, x2;               \
	vpinsrq $1,	RGS3, x2, x2;           \
	\
	G(RGI3, RGI4, y2, s1, s2, s3, s0);      \
	vmovq		RGS2, y2;               \
	vpinsrq $1,	RGS3, y2, y2;

#define encround_tail(a, b, c, d, x, y, prerotate) \
	vpaddd			x, y,   x; \
	vpaddd			x, RK1, RT;\
	prerotate(b);			   \
	vpxor			RT, c,  c; \
	vpaddd			y, x,   y; \
	vpaddd			y, RK2, y; \
	vpsrld $1,		c, RT;     \
	vpslld $(32 - 1),	c, c;      \
	vpor			c, RT,  c; \
	vpxor			d, y,   d; \

#define decround_tail(a, b, c, d, x, y, prerotate) \
	vpaddd			x, y,   x; \
	vpaddd			x, RK1, RT;\
	prerotate(a);			   \
	vpxor			RT, c,  c; \
	vpaddd			y, x,   y; \
	vpaddd			y, RK2, y; \
	vpxor			d, y,   d; \
	vpsrld $1,		d, y;      \
	vpslld $(32 - 1),	d, d;      \
	vpor			d, y,   d; \

#define rotate_1l(x) \
	vpslld $1,		x, RR;     \
	vpsrld $(32 - 1),	x, x;      \
	vpor			x, RR,  x;

#define preload_rgi(c) \
	vmovq			c, RGI1; \
	vpextrq $1,		c, RGI2;

#define encrypt_round(n, a, b, c, d, preload, prerotate) \
	vbroadcastss (k+4*(2*(n)))(CTX),   RK1;                  \
	vbroadcastss (k+4*(2*(n)+1))(CTX), RK2;                  \
	round_head_2(a, b, RX0, RY0, RX1, RY1);                  \
	encround_tail(a ## 1, b ## 1, c ## 1, d ## 1, RX0, RY0, prerotate); \
	preload(c ## 1);                                         \
	encround_tail(a ## 2, b ## 2, c ## 2, d ## 2, RX1, RY1, prerotate);

#define decrypt_round(n, a, b, c, d, preload, prerotate) \
	vbroadcastss (k+4*(2*(n)))(CTX),   RK1;                  \
	vbroadcastss (k+4*(2*(n)+1))(CTX), RK2;                  \
	round_head_2(a, b, RX0, RY0, RX1, RY1);                  \
	decround_tail(a ## 1, b ## 1, c ## 1, d ## 1, RX0, RY0, prerotate); \
	preload(c ## 1);                                         \
	decround_tail(a ## 2, b ## 2, c ## 2, d ## 2, RX1, RY1, prerotate);

#define encrypt_cycle(n) \
	encrypt_round((2*n), RA, RB, RC, RD, preload_rgi, rotate_1l); \
	encrypt_round(((2*n) + 1), RC, RD, RA, RB, preload_rgi, rotate_1l);

#define encrypt_cycle_last(n) \
	encrypt_round((2*n), RA, RB, RC, RD, preload_rgi, rotate_1l); \
	encrypt_round(((2*n) + 1), RC, RD, RA, RB, dummy, dummy);

#define decrypt_cycle(n) \
	decrypt_round(((2*n) + 1), RC, RD, RA, RB, preload_rgi, rotate_1l); \
	decrypt_round((2*n), RA, RB, RC, RD, preload_rgi, rotate_1l);

#define decrypt_cycle_last(n) \
	decrypt_round(((2*n) + 1), RC, RD, RA, RB, preload_rgi, rotate_1l); \
	decrypt_round((2*n), RA, RB, RC, RD, dummy, dummy);

#define transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	vpunpckldq		x1, x0, t0; \
	vpunpckhdq		x1, x0, t2; \
	vpunpckldq		x3, x2, t1; \
	vpunpckhdq		x3, x2, x3; \
	\
	vpunpcklqdq		t1, t0, x0; \
	vpunpckhqdq		t1, t0, x1; \
	vpunpcklqdq		x3, t2, x2; \
	vpunpckhqdq		x3, t2, x3;

#define inpack_blocks(in, x0, x1, x2, x3, wkey, t0, t1, t2) \
	vpxor (0*4*4)(in),	wkey, x0; \
	vpxor (1*4*4)(in),	wkey, x1; \
	vpxor (2*4*4)(in),	wkey, x2; \
	vpxor (3*4*4)(in),	wkey, x3; \
	\
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2)

#define outunpack_blocks(out, x0, x1, x2, x3, wkey, t0, t1, t2) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	\
	vpxor		x0, wkey, x0;     \
	vmovdqu 	x0, (0*4*4)(out); \
	vpxor		x1, wkey, x1;     \
	vmovdqu		x1, (1*4*4)(out); \
	vpxor		x2, wkey, x2;     \
	vmovdqu		x2, (2*4*4)(out); \
	vpxor		x3, wkey, x3;     \
	vmovdqu		x3, (3*4*4)(out);

#define outunpack_xor_blocks(out, x0, x1, x2, x3, wkey, t0, t1, t2) \
	transpose_4x4(x0, x1, x2, x3, t0, t1, t2) \
	\
	vpxor		x0, wkey, x0;         \
	vpxor		(0*4*4)(out), x0, x0; \
	vmovdqu 	x0, (0*4*4)(out);     \
	vpxor		x1, wkey, x1;         \
	vpxor		(1*4*4)(out), x1, x1; \
	vmovdqu	        x1, (1*4*4)(out);     \
	vpxor		x2, wkey, x2;         \
	vpxor           (2*4*4)(out), x2, x2; \
	vmovdqu		x2, (2*4*4)(out);     \
	vpxor		x3, wkey, x3;         \
	vpxor           (3*4*4)(out), x3, x3; \
	vmovdqu		x3, (3*4*4)(out);

.align 8
.global __twofish_enc_blk_8way
.type   __twofish_enc_blk_8way,@function;

__twofish_enc_blk_8way:
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 *	%rcx: bool, if true: xor output
	 */

	pushq %rbp;
	pushq %rbx;
	pushq %rcx;

	vmovdqu w(CTX), RK1;

	leaq (4*4*4)(%rdx), %rax;
	inpack_blocks(%rdx, RA1, RB1, RC1, RD1, RK1, RX0, RY0, RK2);
	preload_rgi(RA1);
	rotate_1l(RD1);
	inpack_blocks(%rax, RA2, RB2, RC2, RD2, RK1, RX0, RY0, RK2);
	rotate_1l(RD2);

	movq %rsi, %r11;

	encrypt_cycle(0);
	encrypt_cycle(1);
	encrypt_cycle(2);
	encrypt_cycle(3);
	encrypt_cycle(4);
	encrypt_cycle(5);
	encrypt_cycle(6);
	encrypt_cycle_last(7);

	vmovdqu (w+4*4)(CTX), RK1;

	popq %rcx;
	popq %rbx;
	popq %rbp;

	leaq (4*4*4)(%r11), %rax;

	testb %cl, %cl;
	jnz __enc_xor8;

	outunpack_blocks(%r11, RC1, RD1, RA1, RB1, RK1, RX0, RY0, RK2);
	outunpack_blocks(%rax, RC2, RD2, RA2, RB2, RK1, RX0, RY0, RK2);

	ret;

__enc_xor8:
	outunpack_xor_blocks(%r11, RC1, RD1, RA1, RB1, RK1, RX0, RY0, RK2);
	outunpack_xor_blocks(%rax, RC2, RD2, RA2, RB2, RK1, RX0, RY0, RK2);

	ret;

.align 8
.global twofish_dec_blk_8way
.type   twofish_dec_blk_8way,@function;

twofish_dec_blk_8way:
	/* input:
	 *	%rdi: ctx, CTX
	 *	%rsi: dst
	 *	%rdx: src
	 */

	pushq %rbp;
	pushq %rbx;

	vmovdqu (w+4*4)(CTX), RK1;

	leaq (4*4*4)(%rdx), %rax;
	inpack_blocks(%rdx, RC1, RD1, RA1, RB1, RK1, RX0, RY0, RK2);
	preload_rgi(RC1);
	rotate_1l(RA1);
	inpack_blocks(%rax, RC2, RD2, RA2, RB2, RK1, RX0, RY0, RK2);
	rotate_1l(RA2);

	movq %rsi, %r11;

	decrypt_cycle(7);
	decrypt_cycle(6);
	decrypt_cycle(5);
	decrypt_cycle(4);
	decrypt_cycle(3);
	decrypt_cycle(2);
	decrypt_cycle(1);
	decrypt_cycle_last(0);

	vmovdqu (w)(CTX), RK1;

	popq %rbx;
	popq %rbp;

	leaq (4*4*4)(%r11), %rax;
	outunpack_blocks(%r11, RA1, RB1, RC1, RD1, RK1, RX0, RY0, RK2);
	outunpack_blocks(%rax, RA2, RB2, RC2, RD2, RK1, RX0, RY0, RK2);

	ret;
