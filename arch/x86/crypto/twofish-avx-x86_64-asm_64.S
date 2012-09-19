/*
 * Twofish Cipher 8-way parallel algorithm (AVX/x86_64)
 *
 * Copyright (C) 2012 Johannes Goetzfried
 *     <Johannes.Goetzfried@informatik.stud.uni-erlangen.de>
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

#define RX %xmm8
#define RY %xmm9

#define RK1 %xmm10
#define RK2 %xmm11

#define RID1  %rax
#define RID1b %al
#define RID2  %rbx
#define RID2b %bl

#define RGI1   %rdx
#define RGI1bl %dl
#define RGI1bh %dh
#define RGI2   %rcx
#define RGI2bl %cl
#define RGI2bh %ch

#define RGS1  %r8
#define RGS1d %r8d
#define RGS2  %r9
#define RGS2d %r9d
#define RGS3  %r10
#define RGS3d %r10d


#define lookup_32bit(t0, t1, t2, t3, src, dst) \
	movb		src ## bl,        RID1b;     \
	movb		src ## bh,        RID2b;     \
	movl		t0(CTX, RID1, 4), dst ## d;  \
	xorl		t1(CTX, RID2, 4), dst ## d;  \
	shrq $16,	src;                         \
	movb		src ## bl,        RID1b;     \
	movb		src ## bh,        RID2b;     \
	xorl		t2(CTX, RID1, 4), dst ## d;  \
	xorl		t3(CTX, RID2, 4), dst ## d;

#define G(a, x, t0, t1, t2, t3) \
	vmovq		a,    RGI1;               \
	vpsrldq $8,	a,    x;                  \
	vmovq		x,    RGI2;               \
	\
	lookup_32bit(t0, t1, t2, t3, RGI1, RGS1); \
	shrq $16,	RGI1;                     \
	lookup_32bit(t0, t1, t2, t3, RGI1, RGS2); \
	shlq $32,	RGS2;                     \
	orq		RGS1, RGS2;               \
	\
	lookup_32bit(t0, t1, t2, t3, RGI2, RGS1); \
	shrq $16,	RGI2;                     \
	lookup_32bit(t0, t1, t2, t3, RGI2, RGS3); \
	shlq $32,	RGS3;                     \
	orq		RGS1, RGS3;               \
	\
	vmovq		RGS2, x;                  \
	vpinsrq $1,	RGS3, x, x;

#define encround(a, b, c, d, x, y) \
	G(a, x, s0, s1, s2, s3);           \
	G(b, y, s1, s2, s3, s0);           \
	vpaddd			x, y,   x; \
	vpaddd			y, x,   y; \
	vpaddd			x, RK1, x; \
	vpaddd			y, RK2, y; \
	vpxor			x, c,   c; \
	vpsrld $1,		c, x;      \
	vpslld $(32 - 1),	c, c;      \
	vpor			c, x,   c; \
	vpslld $1,		d, x;      \
	vpsrld $(32 - 1),	d, d;      \
	vpor			d, x,   d; \
	vpxor			d, y,   d;

#define decround(a, b, c, d, x, y) \
	G(a, x, s0, s1, s2, s3);           \
	G(b, y, s1, s2, s3, s0);           \
	vpaddd			x, y,   x; \
	vpaddd			y, x,   y; \
	vpaddd			y, RK2, y; \
	vpxor			d, y,   d; \
	vpsrld $1,		d, y;      \
	vpslld $(32 - 1),	d, d;      \
	vpor			d, y,   d; \
	vpslld $1,		c, y;      \
	vpsrld $(32 - 1),	c, c;      \
	vpor			c, y,   c; \
	vpaddd			x, RK1, x; \
	vpxor			x, c,   c;

#define encrypt_round(n, a, b, c, d) \
	vbroadcastss (k+4*(2*(n)))(CTX),   RK1;           \
	vbroadcastss (k+4*(2*(n)+1))(CTX), RK2;           \
	encround(a ## 1, b ## 1, c ## 1, d ## 1, RX, RY); \
	encround(a ## 2, b ## 2, c ## 2, d ## 2, RX, RY);

#define decrypt_round(n, a, b, c, d) \
	vbroadcastss (k+4*(2*(n)))(CTX),   RK1;           \
	vbroadcastss (k+4*(2*(n)+1))(CTX), RK2;           \
	decround(a ## 1, b ## 1, c ## 1, d ## 1, RX, RY); \
	decround(a ## 2, b ## 2, c ## 2, d ## 2, RX, RY);

#define encrypt_cycle(n) \
	encrypt_round((2*n), RA, RB, RC, RD);       \
	encrypt_round(((2*n) + 1), RC, RD, RA, RB);

#define decrypt_cycle(n) \
	decrypt_round(((2*n) + 1), RC, RD, RA, RB); \
	decrypt_round((2*n), RA, RB, RC, RD);


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

	pushq %rbx;
	pushq %rcx;

	vmovdqu w(CTX), RK1;

	leaq (4*4*4)(%rdx), %rax;
	inpack_blocks(%rdx, RA1, RB1, RC1, RD1, RK1, RX, RY, RK2);
	inpack_blocks(%rax, RA2, RB2, RC2, RD2, RK1, RX, RY, RK2);

	xorq RID1, RID1;
	xorq RID2, RID2;

	encrypt_cycle(0);
	encrypt_cycle(1);
	encrypt_cycle(2);
	encrypt_cycle(3);
	encrypt_cycle(4);
	encrypt_cycle(5);
	encrypt_cycle(6);
	encrypt_cycle(7);

	vmovdqu (w+4*4)(CTX), RK1;

	popq %rcx;
	popq %rbx;

	leaq (4*4*4)(%rsi), %rax;

	testb %cl, %cl;
	jnz __enc_xor8;

	outunpack_blocks(%rsi, RC1, RD1, RA1, RB1, RK1, RX, RY, RK2);
	outunpack_blocks(%rax, RC2, RD2, RA2, RB2, RK1, RX, RY, RK2);

	ret;

__enc_xor8:
	outunpack_xor_blocks(%rsi, RC1, RD1, RA1, RB1, RK1, RX, RY, RK2);
	outunpack_xor_blocks(%rax, RC2, RD2, RA2, RB2, RK1, RX, RY, RK2);

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

	pushq %rbx;

	vmovdqu (w+4*4)(CTX), RK1;

	leaq (4*4*4)(%rdx), %rax;
	inpack_blocks(%rdx, RC1, RD1, RA1, RB1, RK1, RX, RY, RK2);
	inpack_blocks(%rax, RC2, RD2, RA2, RB2, RK1, RX, RY, RK2);

	xorq RID1, RID1;
	xorq RID2, RID2;

	decrypt_cycle(7);
	decrypt_cycle(6);
	decrypt_cycle(5);
	decrypt_cycle(4);
	decrypt_cycle(3);
	decrypt_cycle(2);
	decrypt_cycle(1);
	decrypt_cycle(0);

	vmovdqu (w)(CTX), RK1;

	popq %rbx;

	leaq (4*4*4)(%rsi), %rax;
	outunpack_blocks(%rsi, RA1, RB1, RC1, RD1, RK1, RX, RY, RK2);
	outunpack_blocks(%rax, RA2, RB2, RC2, RD2, RK1, RX, RY, RK2);

	ret;
