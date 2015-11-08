/*
 * Poly1305 authenticator algorithm, RFC7539, x64 SSE2 functions
 *
 * Copyright (C) 2015 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/linkage.h>

.data
.align 16

ANMASK:	.octa 0x0000000003ffffff0000000003ffffff
ORMASK:	.octa 0x00000000010000000000000001000000

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
#define s1 0x00(%rsp)
#define s2 0x04(%rsp)
#define s3 0x08(%rsp)
#define s4 0x0c(%rsp)
#define m %rsi
#define h01 %xmm0
#define h23 %xmm1
#define h44 %xmm2
#define t1 %xmm3
#define t2 %xmm4
#define t3 %xmm5
#define t4 %xmm6
#define mask %xmm7
#define d0 %r8
#define d1 %r9
#define d2 %r10
#define d3 %r11
#define d4 %r12

ENTRY(poly1305_block_sse2)
	# %rdi: Accumulator h[5]
	# %rsi: 16 byte input block m
	# %rdx: Poly1305 key r[5]
	# %rcx: Block count

	# This single block variant tries to improve performance by doing two
	# multiplications in parallel using SSE instructions. There is quite
	# some quardword packing involved, hence the speedup is marginal.

	push		%rbx
	push		%r12
	sub		$0x10,%rsp

	# s1..s4 = r1..r4 * 5
	mov		r1,%eax
	lea		(%eax,%eax,4),%eax
	mov		%eax,s1
	mov		r2,%eax
	lea		(%eax,%eax,4),%eax
	mov		%eax,s2
	mov		r3,%eax
	lea		(%eax,%eax,4),%eax
	mov		%eax,s3
	mov		r4,%eax
	lea		(%eax,%eax,4),%eax
	mov		%eax,s4

	movdqa		ANMASK(%rip),mask

.Ldoblock:
	# h01 = [0, h1, 0, h0]
	# h23 = [0, h3, 0, h2]
	# h44 = [0, h4, 0, h4]
	movd		h0,h01
	movd		h1,t1
	movd		h2,h23
	movd		h3,t2
	movd		h4,h44
	punpcklqdq	t1,h01
	punpcklqdq	t2,h23
	punpcklqdq	h44,h44

	# h01 += [ (m[3-6] >> 2) & 0x3ffffff, m[0-3] & 0x3ffffff ]
	movd		0x00(m),t1
	movd		0x03(m),t2
	psrld		$2,t2
	punpcklqdq	t2,t1
	pand		mask,t1
	paddd		t1,h01
	# h23 += [ (m[9-12] >> 6) & 0x3ffffff, (m[6-9] >> 4) & 0x3ffffff ]
	movd		0x06(m),t1
	movd		0x09(m),t2
	psrld		$4,t1
	psrld		$6,t2
	punpcklqdq	t2,t1
	pand		mask,t1
	paddd		t1,h23
	# h44 += [ (m[12-15] >> 8) | (1 << 24), (m[12-15] >> 8) | (1 << 24) ]
	mov		0x0c(m),%eax
	shr		$8,%eax
	or		$0x01000000,%eax
	movd		%eax,t1
	pshufd		$0xc4,t1,t1
	paddd		t1,h44

	# t1[0] = h0 * r0 + h2 * s3
	# t1[1] = h1 * s4 + h3 * s2
	movd		r0,t1
	movd		s4,t2
	punpcklqdq	t2,t1
	pmuludq		h01,t1
	movd		s3,t2
	movd		s2,t3
	punpcklqdq	t3,t2
	pmuludq		h23,t2
	paddq		t2,t1
	# t2[0] = h0 * r1 + h2 * s4
	# t2[1] = h1 * r0 + h3 * s3
	movd		r1,t2
	movd		r0,t3
	punpcklqdq	t3,t2
	pmuludq		h01,t2
	movd		s4,t3
	movd		s3,t4
	punpcklqdq	t4,t3
	pmuludq		h23,t3
	paddq		t3,t2
	# t3[0] = h4 * s1
	# t3[1] = h4 * s2
	movd		s1,t3
	movd		s2,t4
	punpcklqdq	t4,t3
	pmuludq		h44,t3
	# d0 = t1[0] + t1[1] + t3[0]
	# d1 = t2[0] + t2[1] + t3[1]
	movdqa		t1,t4
	punpcklqdq	t2,t4
	punpckhqdq	t2,t1
	paddq		t4,t1
	paddq		t3,t1
	movq		t1,d0
	psrldq		$8,t1
	movq		t1,d1

	# t1[0] = h0 * r2 + h2 * r0
	# t1[1] = h1 * r1 + h3 * s4
	movd		r2,t1
	movd		r1,t2
	punpcklqdq 	t2,t1
	pmuludq		h01,t1
	movd		r0,t2
	movd		s4,t3
	punpcklqdq	t3,t2
	pmuludq		h23,t2
	paddq		t2,t1
	# t2[0] = h0 * r3 + h2 * r1
	# t2[1] = h1 * r2 + h3 * r0
	movd		r3,t2
	movd		r2,t3
	punpcklqdq	t3,t2
	pmuludq		h01,t2
	movd		r1,t3
	movd		r0,t4
	punpcklqdq	t4,t3
	pmuludq		h23,t3
	paddq		t3,t2
	# t3[0] = h4 * s3
	# t3[1] = h4 * s4
	movd		s3,t3
	movd		s4,t4
	punpcklqdq	t4,t3
	pmuludq		h44,t3
	# d2 = t1[0] + t1[1] + t3[0]
	# d3 = t2[0] + t2[1] + t3[1]
	movdqa		t1,t4
	punpcklqdq	t2,t4
	punpckhqdq	t2,t1
	paddq		t4,t1
	paddq		t3,t1
	movq		t1,d2
	psrldq		$8,t1
	movq		t1,d3

	# t1[0] = h0 * r4 + h2 * r2
	# t1[1] = h1 * r3 + h3 * r1
	movd		r4,t1
	movd		r3,t2
	punpcklqdq	t2,t1
	pmuludq		h01,t1
	movd		r2,t2
	movd		r1,t3
	punpcklqdq	t3,t2
	pmuludq		h23,t2
	paddq		t2,t1
	# t3[0] = h4 * r0
	movd		r0,t3
	pmuludq		h44,t3
	# d4 = t1[0] + t1[1] + t3[0]
	movdqa		t1,t4
	psrldq		$8,t4
	paddq		t4,t1
	paddq		t3,t1
	movq		t1,d4

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
	lea		(%eax,%eax,4),%eax
	add		%eax,%ebx
	# h4 = d4 & 0x3ffffff
	mov		d4,%rax
	and		$0x3ffffff,%eax
	mov		%eax,h4

	# h1 += h0 >> 26
	mov		%ebx,%eax
	shr		$26,%eax
	add		%eax,h1
	# h0 = h0 & 0x3ffffff
	andl		$0x3ffffff,%ebx
	mov		%ebx,h0

	add		$0x10,m
	dec		%rcx
	jnz		.Ldoblock

	add		$0x10,%rsp
	pop		%r12
	pop		%rbx
	ret
ENDPROC(poly1305_block_sse2)


#define u0 0x00(%r8)
#define u1 0x04(%r8)
#define u2 0x08(%r8)
#define u3 0x0c(%r8)
#define u4 0x10(%r8)
#define hc0 %xmm0
#define hc1 %xmm1
#define hc2 %xmm2
#define hc3 %xmm5
#define hc4 %xmm6
#define ru0 %xmm7
#define ru1 %xmm8
#define ru2 %xmm9
#define ru3 %xmm10
#define ru4 %xmm11
#define sv1 %xmm12
#define sv2 %xmm13
#define sv3 %xmm14
#define sv4 %xmm15
#undef d0
#define d0 %r13

ENTRY(poly1305_2block_sse2)
	# %rdi: Accumulator h[5]
	# %rsi: 16 byte input block m
	# %rdx: Poly1305 key r[5]
	# %rcx: Doubleblock count
	# %r8:  Poly1305 derived key r^2 u[5]

	# This two-block variant further improves performance by using loop
	# unrolled block processing. This is more straight forward and does
	# less byte shuffling, but requires a second Poly1305 key r^2:
	# h = (h + m) * r    =>    h = (h + m1) * r^2 + m2 * r

	push		%rbx
	push		%r12
	push		%r13

	# combine r0,u0
	movd		u0,ru0
	movd		r0,t1
	punpcklqdq	t1,ru0

	# combine r1,u1 and s1=r1*5,v1=u1*5
	movd		u1,ru1
	movd		r1,t1
	punpcklqdq	t1,ru1
	movdqa		ru1,sv1
	pslld		$2,sv1
	paddd		ru1,sv1

	# combine r2,u2 and s2=r2*5,v2=u2*5
	movd		u2,ru2
	movd		r2,t1
	punpcklqdq	t1,ru2
	movdqa		ru2,sv2
	pslld		$2,sv2
	paddd		ru2,sv2

	# combine r3,u3 and s3=r3*5,v3=u3*5
	movd		u3,ru3
	movd		r3,t1
	punpcklqdq	t1,ru3
	movdqa		ru3,sv3
	pslld		$2,sv3
	paddd		ru3,sv3

	# combine r4,u4 and s4=r4*5,v4=u4*5
	movd		u4,ru4
	movd		r4,t1
	punpcklqdq	t1,ru4
	movdqa		ru4,sv4
	pslld		$2,sv4
	paddd		ru4,sv4

.Ldoblock2:
	# hc0 = [ m[16-19] & 0x3ffffff, h0 + m[0-3] & 0x3ffffff ]
	movd		0x00(m),hc0
	movd		0x10(m),t1
	punpcklqdq	t1,hc0
	pand		ANMASK(%rip),hc0
	movd		h0,t1
	paddd		t1,hc0
	# hc1 = [ (m[19-22] >> 2) & 0x3ffffff, h1 + (m[3-6] >> 2) & 0x3ffffff ]
	movd		0x03(m),hc1
	movd		0x13(m),t1
	punpcklqdq	t1,hc1
	psrld		$2,hc1
	pand		ANMASK(%rip),hc1
	movd		h1,t1
	paddd		t1,hc1
	# hc2 = [ (m[22-25] >> 4) & 0x3ffffff, h2 + (m[6-9] >> 4) & 0x3ffffff ]
	movd		0x06(m),hc2
	movd		0x16(m),t1
	punpcklqdq	t1,hc2
	psrld		$4,hc2
	pand		ANMASK(%rip),hc2
	movd		h2,t1
	paddd		t1,hc2
	# hc3 = [ (m[25-28] >> 6) & 0x3ffffff, h3 + (m[9-12] >> 6) & 0x3ffffff ]
	movd		0x09(m),hc3
	movd		0x19(m),t1
	punpcklqdq	t1,hc3
	psrld		$6,hc3
	pand		ANMASK(%rip),hc3
	movd		h3,t1
	paddd		t1,hc3
	# hc4 = [ (m[28-31] >> 8) | (1<<24), h4 + (m[12-15] >> 8) | (1<<24) ]
	movd		0x0c(m),hc4
	movd		0x1c(m),t1
	punpcklqdq	t1,hc4
	psrld		$8,hc4
	por		ORMASK(%rip),hc4
	movd		h4,t1
	paddd		t1,hc4

	# t1 = [ hc0[1] * r0, hc0[0] * u0 ]
	movdqa		ru0,t1
	pmuludq		hc0,t1
	# t1 += [ hc1[1] * s4, hc1[0] * v4 ]
	movdqa		sv4,t2
	pmuludq		hc1,t2
	paddq		t2,t1
	# t1 += [ hc2[1] * s3, hc2[0] * v3 ]
	movdqa		sv3,t2
	pmuludq		hc2,t2
	paddq		t2,t1
	# t1 += [ hc3[1] * s2, hc3[0] * v2 ]
	movdqa		sv2,t2
	pmuludq		hc3,t2
	paddq		t2,t1
	# t1 += [ hc4[1] * s1, hc4[0] * v1 ]
	movdqa		sv1,t2
	pmuludq		hc4,t2
	paddq		t2,t1
	# d0 = t1[0] + t1[1]
	movdqa		t1,t2
	psrldq		$8,t2
	paddq		t2,t1
	movq		t1,d0

	# t1 = [ hc0[1] * r1, hc0[0] * u1 ]
	movdqa		ru1,t1
	pmuludq		hc0,t1
	# t1 += [ hc1[1] * r0, hc1[0] * u0 ]
	movdqa		ru0,t2
	pmuludq		hc1,t2
	paddq		t2,t1
	# t1 += [ hc2[1] * s4, hc2[0] * v4 ]
	movdqa		sv4,t2
	pmuludq		hc2,t2
	paddq		t2,t1
	# t1 += [ hc3[1] * s3, hc3[0] * v3 ]
	movdqa		sv3,t2
	pmuludq		hc3,t2
	paddq		t2,t1
	# t1 += [ hc4[1] * s2, hc4[0] * v2 ]
	movdqa		sv2,t2
	pmuludq		hc4,t2
	paddq		t2,t1
	# d1 = t1[0] + t1[1]
	movdqa		t1,t2
	psrldq		$8,t2
	paddq		t2,t1
	movq		t1,d1

	# t1 = [ hc0[1] * r2, hc0[0] * u2 ]
	movdqa		ru2,t1
	pmuludq		hc0,t1
	# t1 += [ hc1[1] * r1, hc1[0] * u1 ]
	movdqa		ru1,t2
	pmuludq		hc1,t2
	paddq		t2,t1
	# t1 += [ hc2[1] * r0, hc2[0] * u0 ]
	movdqa		ru0,t2
	pmuludq		hc2,t2
	paddq		t2,t1
	# t1 += [ hc3[1] * s4, hc3[0] * v4 ]
	movdqa		sv4,t2
	pmuludq		hc3,t2
	paddq		t2,t1
	# t1 += [ hc4[1] * s3, hc4[0] * v3 ]
	movdqa		sv3,t2
	pmuludq		hc4,t2
	paddq		t2,t1
	# d2 = t1[0] + t1[1]
	movdqa		t1,t2
	psrldq		$8,t2
	paddq		t2,t1
	movq		t1,d2

	# t1 = [ hc0[1] * r3, hc0[0] * u3 ]
	movdqa		ru3,t1
	pmuludq		hc0,t1
	# t1 += [ hc1[1] * r2, hc1[0] * u2 ]
	movdqa		ru2,t2
	pmuludq		hc1,t2
	paddq		t2,t1
	# t1 += [ hc2[1] * r1, hc2[0] * u1 ]
	movdqa		ru1,t2
	pmuludq		hc2,t2
	paddq		t2,t1
	# t1 += [ hc3[1] * r0, hc3[0] * u0 ]
	movdqa		ru0,t2
	pmuludq		hc3,t2
	paddq		t2,t1
	# t1 += [ hc4[1] * s4, hc4[0] * v4 ]
	movdqa		sv4,t2
	pmuludq		hc4,t2
	paddq		t2,t1
	# d3 = t1[0] + t1[1]
	movdqa		t1,t2
	psrldq		$8,t2
	paddq		t2,t1
	movq		t1,d3

	# t1 = [ hc0[1] * r4, hc0[0] * u4 ]
	movdqa		ru4,t1
	pmuludq		hc0,t1
	# t1 += [ hc1[1] * r3, hc1[0] * u3 ]
	movdqa		ru3,t2
	pmuludq		hc1,t2
	paddq		t2,t1
	# t1 += [ hc2[1] * r2, hc2[0] * u2 ]
	movdqa		ru2,t2
	pmuludq		hc2,t2
	paddq		t2,t1
	# t1 += [ hc3[1] * r1, hc3[0] * u1 ]
	movdqa		ru1,t2
	pmuludq		hc3,t2
	paddq		t2,t1
	# t1 += [ hc4[1] * r0, hc4[0] * u0 ]
	movdqa		ru0,t2
	pmuludq		hc4,t2
	paddq		t2,t1
	# d4 = t1[0] + t1[1]
	movdqa		t1,t2
	psrldq		$8,t2
	paddq		t2,t1
	movq		t1,d4

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
	lea		(%eax,%eax,4),%eax
	add		%eax,%ebx
	# h4 = d4 & 0x3ffffff
	mov		d4,%rax
	and		$0x3ffffff,%eax
	mov		%eax,h4

	# h1 += h0 >> 26
	mov		%ebx,%eax
	shr		$26,%eax
	add		%eax,h1
	# h0 = h0 & 0x3ffffff
	andl		$0x3ffffff,%ebx
	mov		%ebx,h0

	add		$0x20,m
	dec		%rcx
	jnz		.Ldoblock2

	pop		%r13
	pop		%r12
	pop		%rbx
	ret
ENDPROC(poly1305_2block_sse2)
