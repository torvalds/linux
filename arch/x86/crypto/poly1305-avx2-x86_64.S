/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Poly1305 authenticator algorithm, RFC7539, x64 AVX2 functions
 *
 * Copyright (C) 2015 Martin Willi
 */

#include <linux/linkage.h>

.section	.rodata.cst32.ANMASK, "aM", @progbits, 32
.align 32
ANMASK:	.octa 0x0000000003ffffff0000000003ffffff
	.octa 0x0000000003ffffff0000000003ffffff

.section	.rodata.cst32.ORMASK, "aM", @progbits, 32
.align 32
ORMASK:	.octa 0x00000000010000000000000001000000
	.octa 0x00000000010000000000000001000000

.text

#define h0 0x00(%rdi)
#define h1 0x04(%rdi)
#define h2 0x08(%rdi)
#define h3 0x0c(%rdi)
#define h4 0x10(%rdi)
#define r0 0x00(%rdx)
#define r1 0x04(%rdx)
#define r2 0x08(%rdx)
#define r3 0x0c(%rdx)
#define r4 0x10(%rdx)
#define u0 0x00(%r8)
#define u1 0x04(%r8)
#define u2 0x08(%r8)
#define u3 0x0c(%r8)
#define u4 0x10(%r8)
#define w0 0x14(%r8)
#define w1 0x18(%r8)
#define w2 0x1c(%r8)
#define w3 0x20(%r8)
#define w4 0x24(%r8)
#define y0 0x28(%r8)
#define y1 0x2c(%r8)
#define y2 0x30(%r8)
#define y3 0x34(%r8)
#define y4 0x38(%r8)
#define m %rsi
#define hc0 %ymm0
#define hc1 %ymm1
#define hc2 %ymm2
#define hc3 %ymm3
#define hc4 %ymm4
#define hc0x %xmm0
#define hc1x %xmm1
#define hc2x %xmm2
#define hc3x %xmm3
#define hc4x %xmm4
#define t1 %ymm5
#define t2 %ymm6
#define t1x %xmm5
#define t2x %xmm6
#define ruwy0 %ymm7
#define ruwy1 %ymm8
#define ruwy2 %ymm9
#define ruwy3 %ymm10
#define ruwy4 %ymm11
#define ruwy0x %xmm7
#define ruwy1x %xmm8
#define ruwy2x %xmm9
#define ruwy3x %xmm10
#define ruwy4x %xmm11
#define svxz1 %ymm12
#define svxz2 %ymm13
#define svxz3 %ymm14
#define svxz4 %ymm15
#define d0 %r9
#define d1 %r10
#define d2 %r11
#define d3 %r12
#define d4 %r13

SYM_FUNC_START(poly1305_4block_avx2)
	# %rdi: Accumulator h[5]
	# %rsi: 64 byte input block m
	# %rdx: Poly1305 key r[5]
	# %rcx: Quadblock count
	# %r8:  Poly1305 derived key r^2 u[5], r^3 w[5], r^4 y[5],

	# This four-block variant uses loop unrolled block processing. It
	# requires 4 Poly1305 keys: r, r^2, r^3 and r^4:
	# h = (h + m) * r  =>  h = (h + m1) * r^4 + m2 * r^3 + m3 * r^2 + m4 * r

	vzeroupper
	push		%rbx
	push		%r12
	push		%r13

	# combine r0,u0,w0,y0
	vmovd		y0,ruwy0x
	vmovd		w0,t1x
	vpunpcklqdq	t1,ruwy0,ruwy0
	vmovd		u0,t1x
	vmovd		r0,t2x
	vpunpcklqdq	t2,t1,t1
	vperm2i128	$0x20,t1,ruwy0,ruwy0

	# combine r1,u1,w1,y1 and s1=r1*5,v1=u1*5,x1=w1*5,z1=y1*5
	vmovd		y1,ruwy1x
	vmovd		w1,t1x
	vpunpcklqdq	t1,ruwy1,ruwy1
	vmovd		u1,t1x
	vmovd		r1,t2x
	vpunpcklqdq	t2,t1,t1
	vperm2i128	$0x20,t1,ruwy1,ruwy1
	vpslld		$2,ruwy1,svxz1
	vpaddd		ruwy1,svxz1,svxz1

	# combine r2,u2,w2,y2 and s2=r2*5,v2=u2*5,x2=w2*5,z2=y2*5
	vmovd		y2,ruwy2x
	vmovd		w2,t1x
	vpunpcklqdq	t1,ruwy2,ruwy2
	vmovd		u2,t1x
	vmovd		r2,t2x
	vpunpcklqdq	t2,t1,t1
	vperm2i128	$0x20,t1,ruwy2,ruwy2
	vpslld		$2,ruwy2,svxz2
	vpaddd		ruwy2,svxz2,svxz2

	# combine r3,u3,w3,y3 and s3=r3*5,v3=u3*5,x3=w3*5,z3=y3*5
	vmovd		y3,ruwy3x
	vmovd		w3,t1x
	vpunpcklqdq	t1,ruwy3,ruwy3
	vmovd		u3,t1x
	vmovd		r3,t2x
	vpunpcklqdq	t2,t1,t1
	vperm2i128	$0x20,t1,ruwy3,ruwy3
	vpslld		$2,ruwy3,svxz3
	vpaddd		ruwy3,svxz3,svxz3

	# combine r4,u4,w4,y4 and s4=r4*5,v4=u4*5,x4=w4*5,z4=y4*5
	vmovd		y4,ruwy4x
	vmovd		w4,t1x
	vpunpcklqdq	t1,ruwy4,ruwy4
	vmovd		u4,t1x
	vmovd		r4,t2x
	vpunpcklqdq	t2,t1,t1
	vperm2i128	$0x20,t1,ruwy4,ruwy4
	vpslld		$2,ruwy4,svxz4
	vpaddd		ruwy4,svxz4,svxz4

.Ldoblock4:
	# hc0 = [m[48-51] & 0x3ffffff, m[32-35] & 0x3ffffff,
	#	 m[16-19] & 0x3ffffff, m[ 0- 3] & 0x3ffffff + h0]
	vmovd		0x00(m),hc0x
	vmovd		0x10(m),t1x
	vpunpcklqdq	t1,hc0,hc0
	vmovd		0x20(m),t1x
	vmovd		0x30(m),t2x
	vpunpcklqdq	t2,t1,t1
	vperm2i128	$0x20,t1,hc0,hc0
	vpand		ANMASK(%rip),hc0,hc0
	vmovd		h0,t1x
	vpaddd		t1,hc0,hc0
	# hc1 = [(m[51-54] >> 2) & 0x3ffffff, (m[35-38] >> 2) & 0x3ffffff,
	#	 (m[19-22] >> 2) & 0x3ffffff, (m[ 3- 6] >> 2) & 0x3ffffff + h1]
	vmovd		0x03(m),hc1x
	vmovd		0x13(m),t1x
	vpunpcklqdq	t1,hc1,hc1
	vmovd		0x23(m),t1x
	vmovd		0x33(m),t2x
	vpunpcklqdq	t2,t1,t1
	vperm2i128	$0x20,t1,hc1,hc1
	vpsrld		$2,hc1,hc1
	vpand		ANMASK(%rip),hc1,hc1
	vmovd		h1,t1x
	vpaddd		t1,hc1,hc1
	# hc2 = [(m[54-57] >> 4) & 0x3ffffff, (m[38-41] >> 4) & 0x3ffffff,
	#	 (m[22-25] >> 4) & 0x3ffffff, (m[ 6- 9] >> 4) & 0x3ffffff + h2]
	vmovd		0x06(m),hc2x
	vmovd		0x16(m),t1x
	vpunpcklqdq	t1,hc2,hc2
	vmovd		0x26(m),t1x
	vmovd		0x36(m),t2x
	vpunpcklqdq	t2,t1,t1
	vperm2i128	$0x20,t1,hc2,hc2
	vpsrld		$4,hc2,hc2
	vpand		ANMASK(%rip),hc2,hc2
	vmovd		h2,t1x
	vpaddd		t1,hc2,hc2
	# hc3 = [(m[57-60] >> 6) & 0x3ffffff, (m[41-44] >> 6) & 0x3ffffff,
	#	 (m[25-28] >> 6) & 0x3ffffff, (m[ 9-12] >> 6) & 0x3ffffff + h3]
	vmovd		0x09(m),hc3x
	vmovd		0x19(m),t1x
	vpunpcklqdq	t1,hc3,hc3
	vmovd		0x29(m),t1x
	vmovd		0x39(m),t2x
	vpunpcklqdq	t2,t1,t1
	vperm2i128	$0x20,t1,hc3,hc3
	vpsrld		$6,hc3,hc3
	vpand		ANMASK(%rip),hc3,hc3
	vmovd		h3,t1x
	vpaddd		t1,hc3,hc3
	# hc4 = [(m[60-63] >> 8) | (1<<24), (m[44-47] >> 8) | (1<<24),
	#	 (m[28-31] >> 8) | (1<<24), (m[12-15] >> 8) | (1<<24) + h4]
	vmovd		0x0c(m),hc4x
	vmovd		0x1c(m),t1x
	vpunpcklqdq	t1,hc4,hc4
	vmovd		0x2c(m),t1x
	vmovd		0x3c(m),t2x
	vpunpcklqdq	t2,t1,t1
	vperm2i128	$0x20,t1,hc4,hc4
	vpsrld		$8,hc4,hc4
	vpor		ORMASK(%rip),hc4,hc4
	vmovd		h4,t1x
	vpaddd		t1,hc4,hc4

	# t1 = [ hc0[3] * r0, hc0[2] * u0, hc0[1] * w0, hc0[0] * y0 ]
	vpmuludq	hc0,ruwy0,t1
	# t1 += [ hc1[3] * s4, hc1[2] * v4, hc1[1] * x4, hc1[0] * z4 ]
	vpmuludq	hc1,svxz4,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc2[3] * s3, hc2[2] * v3, hc2[1] * x3, hc2[0] * z3 ]
	vpmuludq	hc2,svxz3,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc3[3] * s2, hc3[2] * v2, hc3[1] * x2, hc3[0] * z2 ]
	vpmuludq	hc3,svxz2,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc4[3] * s1, hc4[2] * v1, hc4[1] * x1, hc4[0] * z1 ]
	vpmuludq	hc4,svxz1,t2
	vpaddq		t2,t1,t1
	# d0 = t1[0] + t1[1] + t[2] + t[3]
	vpermq		$0xee,t1,t2
	vpaddq		t2,t1,t1
	vpsrldq		$8,t1,t2
	vpaddq		t2,t1,t1
	vmovq		t1x,d0

	# t1 = [ hc0[3] * r1, hc0[2] * u1,hc0[1] * w1, hc0[0] * y1 ]
	vpmuludq	hc0,ruwy1,t1
	# t1 += [ hc1[3] * r0, hc1[2] * u0, hc1[1] * w0, hc1[0] * y0 ]
	vpmuludq	hc1,ruwy0,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc2[3] * s4, hc2[2] * v4, hc2[1] * x4, hc2[0] * z4 ]
	vpmuludq	hc2,svxz4,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc3[3] * s3, hc3[2] * v3, hc3[1] * x3, hc3[0] * z3 ]
	vpmuludq	hc3,svxz3,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc4[3] * s2, hc4[2] * v2, hc4[1] * x2, hc4[0] * z2 ]
	vpmuludq	hc4,svxz2,t2
	vpaddq		t2,t1,t1
	# d1 = t1[0] + t1[1] + t1[3] + t1[4]
	vpermq		$0xee,t1,t2
	vpaddq		t2,t1,t1
	vpsrldq		$8,t1,t2
	vpaddq		t2,t1,t1
	vmovq		t1x,d1

	# t1 = [ hc0[3] * r2, hc0[2] * u2, hc0[1] * w2, hc0[0] * y2 ]
	vpmuludq	hc0,ruwy2,t1
	# t1 += [ hc1[3] * r1, hc1[2] * u1, hc1[1] * w1, hc1[0] * y1 ]
	vpmuludq	hc1,ruwy1,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc2[3] * r0, hc2[2] * u0, hc2[1] * w0, hc2[0] * y0 ]
	vpmuludq	hc2,ruwy0,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc3[3] * s4, hc3[2] * v4, hc3[1] * x4, hc3[0] * z4 ]
	vpmuludq	hc3,svxz4,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc4[3] * s3, hc4[2] * v3, hc4[1] * x3, hc4[0] * z3 ]
	vpmuludq	hc4,svxz3,t2
	vpaddq		t2,t1,t1
	# d2 = t1[0] + t1[1] + t1[2] + t1[3]
	vpermq		$0xee,t1,t2
	vpaddq		t2,t1,t1
	vpsrldq		$8,t1,t2
	vpaddq		t2,t1,t1
	vmovq		t1x,d2

	# t1 = [ hc0[3] * r3, hc0[2] * u3, hc0[1] * w3, hc0[0] * y3 ]
	vpmuludq	hc0,ruwy3,t1
	# t1 += [ hc1[3] * r2, hc1[2] * u2, hc1[1] * w2, hc1[0] * y2 ]
	vpmuludq	hc1,ruwy2,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc2[3] * r1, hc2[2] * u1, hc2[1] * w1, hc2[0] * y1 ]
	vpmuludq	hc2,ruwy1,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc3[3] * r0, hc3[2] * u0, hc3[1] * w0, hc3[0] * y0 ]
	vpmuludq	hc3,ruwy0,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc4[3] * s4, hc4[2] * v4, hc4[1] * x4, hc4[0] * z4 ]
	vpmuludq	hc4,svxz4,t2
	vpaddq		t2,t1,t1
	# d3 = t1[0] + t1[1] + t1[2] + t1[3]
	vpermq		$0xee,t1,t2
	vpaddq		t2,t1,t1
	vpsrldq		$8,t1,t2
	vpaddq		t2,t1,t1
	vmovq		t1x,d3

	# t1 = [ hc0[3] * r4, hc0[2] * u4, hc0[1] * w4, hc0[0] * y4 ]
	vpmuludq	hc0,ruwy4,t1
	# t1 += [ hc1[3] * r3, hc1[2] * u3, hc1[1] * w3, hc1[0] * y3 ]
	vpmuludq	hc1,ruwy3,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc2[3] * r2, hc2[2] * u2, hc2[1] * w2, hc2[0] * y2 ]
	vpmuludq	hc2,ruwy2,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc3[3] * r1, hc3[2] * u1, hc3[1] * w1, hc3[0] * y1 ]
	vpmuludq	hc3,ruwy1,t2
	vpaddq		t2,t1,t1
	# t1 += [ hc4[3] * r0, hc4[2] * u0, hc4[1] * w0, hc4[0] * y0 ]
	vpmuludq	hc4,ruwy0,t2
	vpaddq		t2,t1,t1
	# d4 = t1[0] + t1[1] + t1[2] + t1[3]
	vpermq		$0xee,t1,t2
	vpaddq		t2,t1,t1
	vpsrldq		$8,t1,t2
	vpaddq		t2,t1,t1
	vmovq		t1x,d4

	# Now do a partial reduction mod (2^130)-5, carrying h0 -> h1 -> h2 ->
	# h3 -> h4 -> h0 -> h1 to get h0,h2,h3,h4 < 2^26 and h1 < 2^26 + a small
	# amount.  Careful: we must not assume the carry bits 'd0 >> 26',
	# 'd1 >> 26', 'd2 >> 26', 'd3 >> 26', and '(d4 >> 26) * 5' fit in 32-bit
	# integers.  It's true in a single-block implementation, but not here.

	# d1 += d0 >> 26
	mov		d0,%rax
	shr		$26,%rax
	add		%rax,d1
	# h0 = d0 & 0x3ffffff
	mov		d0,%rbx
	and		$0x3ffffff,%ebx

	# d2 += d1 >> 26
	mov		d1,%rax
	shr		$26,%rax
	add		%rax,d2
	# h1 = d1 & 0x3ffffff
	mov		d1,%rax
	and		$0x3ffffff,%eax
	mov		%eax,h1

	# d3 += d2 >> 26
	mov		d2,%rax
	shr		$26,%rax
	add		%rax,d3
	# h2 = d2 & 0x3ffffff
	mov		d2,%rax
	and		$0x3ffffff,%eax
	mov		%eax,h2

	# d4 += d3 >> 26
	mov		d3,%rax
	shr		$26,%rax
	add		%rax,d4
	# h3 = d3 & 0x3ffffff
	mov		d3,%rax
	and		$0x3ffffff,%eax
	mov		%eax,h3

	# h0 += (d4 >> 26) * 5
	mov		d4,%rax
	shr		$26,%rax
	lea		(%rax,%rax,4),%rax
	add		%rax,%rbx
	# h4 = d4 & 0x3ffffff
	mov		d4,%rax
	and		$0x3ffffff,%eax
	mov		%eax,h4

	# h1 += h0 >> 26
	mov		%rbx,%rax
	shr		$26,%rax
	add		%eax,h1
	# h0 = h0 & 0x3ffffff
	andl		$0x3ffffff,%ebx
	mov		%ebx,h0

	add		$0x40,m
	dec		%rcx
	jnz		.Ldoblock4

	vzeroupper
	pop		%r13
	pop		%r12
	pop		%rbx
	ret
SYM_FUNC_END(poly1305_4block_avx2)
