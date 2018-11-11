/*
 * ChaCha20 256-bit cipher algorithm, RFC7539, x64 AVX2 functions
 *
 * Copyright (C) 2015 Martin Willi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/linkage.h>

.section	.rodata.cst32.ROT8, "aM", @progbits, 32
.align 32
ROT8:	.octa 0x0e0d0c0f0a09080b0605040702010003
	.octa 0x0e0d0c0f0a09080b0605040702010003

.section	.rodata.cst32.ROT16, "aM", @progbits, 32
.align 32
ROT16:	.octa 0x0d0c0f0e09080b0a0504070601000302
	.octa 0x0d0c0f0e09080b0a0504070601000302

.section	.rodata.cst32.CTRINC, "aM", @progbits, 32
.align 32
CTRINC:	.octa 0x00000003000000020000000100000000
	.octa 0x00000007000000060000000500000004

.text

ENTRY(chacha20_8block_xor_avx2)
	# %rdi: Input state matrix, s
	# %rsi: up to 8 data blocks output, o
	# %rdx: up to 8 data blocks input, i
	# %rcx: input/output length in bytes

	# This function encrypts eight consecutive ChaCha20 blocks by loading
	# the state matrix in AVX registers eight times. As we need some
	# scratch registers, we save the first four registers on the stack. The
	# algorithm performs each operation on the corresponding word of each
	# state matrix, hence requires no word shuffling. For final XORing step
	# we transpose the matrix by interleaving 32-, 64- and then 128-bit
	# words, which allows us to do XOR in AVX registers. 8/16-bit word
	# rotation is done with the slightly better performing byte shuffling,
	# 7/12-bit word rotation uses traditional shift+OR.

	vzeroupper
	# 4 * 32 byte stack, 32-byte aligned
	lea		8(%rsp),%r10
	and		$~31, %rsp
	sub		$0x80, %rsp
	mov		%rcx,%rax

	# x0..15[0-7] = s[0..15]
	vpbroadcastd	0x00(%rdi),%ymm0
	vpbroadcastd	0x04(%rdi),%ymm1
	vpbroadcastd	0x08(%rdi),%ymm2
	vpbroadcastd	0x0c(%rdi),%ymm3
	vpbroadcastd	0x10(%rdi),%ymm4
	vpbroadcastd	0x14(%rdi),%ymm5
	vpbroadcastd	0x18(%rdi),%ymm6
	vpbroadcastd	0x1c(%rdi),%ymm7
	vpbroadcastd	0x20(%rdi),%ymm8
	vpbroadcastd	0x24(%rdi),%ymm9
	vpbroadcastd	0x28(%rdi),%ymm10
	vpbroadcastd	0x2c(%rdi),%ymm11
	vpbroadcastd	0x30(%rdi),%ymm12
	vpbroadcastd	0x34(%rdi),%ymm13
	vpbroadcastd	0x38(%rdi),%ymm14
	vpbroadcastd	0x3c(%rdi),%ymm15
	# x0..3 on stack
	vmovdqa		%ymm0,0x00(%rsp)
	vmovdqa		%ymm1,0x20(%rsp)
	vmovdqa		%ymm2,0x40(%rsp)
	vmovdqa		%ymm3,0x60(%rsp)

	vmovdqa		CTRINC(%rip),%ymm1
	vmovdqa		ROT8(%rip),%ymm2
	vmovdqa		ROT16(%rip),%ymm3

	# x12 += counter values 0-3
	vpaddd		%ymm1,%ymm12,%ymm12

	mov		$10,%ecx

.Ldoubleround8:
	# x0 += x4, x12 = rotl32(x12 ^ x0, 16)
	vpaddd		0x00(%rsp),%ymm4,%ymm0
	vmovdqa		%ymm0,0x00(%rsp)
	vpxor		%ymm0,%ymm12,%ymm12
	vpshufb		%ymm3,%ymm12,%ymm12
	# x1 += x5, x13 = rotl32(x13 ^ x1, 16)
	vpaddd		0x20(%rsp),%ymm5,%ymm0
	vmovdqa		%ymm0,0x20(%rsp)
	vpxor		%ymm0,%ymm13,%ymm13
	vpshufb		%ymm3,%ymm13,%ymm13
	# x2 += x6, x14 = rotl32(x14 ^ x2, 16)
	vpaddd		0x40(%rsp),%ymm6,%ymm0
	vmovdqa		%ymm0,0x40(%rsp)
	vpxor		%ymm0,%ymm14,%ymm14
	vpshufb		%ymm3,%ymm14,%ymm14
	# x3 += x7, x15 = rotl32(x15 ^ x3, 16)
	vpaddd		0x60(%rsp),%ymm7,%ymm0
	vmovdqa		%ymm0,0x60(%rsp)
	vpxor		%ymm0,%ymm15,%ymm15
	vpshufb		%ymm3,%ymm15,%ymm15

	# x8 += x12, x4 = rotl32(x4 ^ x8, 12)
	vpaddd		%ymm12,%ymm8,%ymm8
	vpxor		%ymm8,%ymm4,%ymm4
	vpslld		$12,%ymm4,%ymm0
	vpsrld		$20,%ymm4,%ymm4
	vpor		%ymm0,%ymm4,%ymm4
	# x9 += x13, x5 = rotl32(x5 ^ x9, 12)
	vpaddd		%ymm13,%ymm9,%ymm9
	vpxor		%ymm9,%ymm5,%ymm5
	vpslld		$12,%ymm5,%ymm0
	vpsrld		$20,%ymm5,%ymm5
	vpor		%ymm0,%ymm5,%ymm5
	# x10 += x14, x6 = rotl32(x6 ^ x10, 12)
	vpaddd		%ymm14,%ymm10,%ymm10
	vpxor		%ymm10,%ymm6,%ymm6
	vpslld		$12,%ymm6,%ymm0
	vpsrld		$20,%ymm6,%ymm6
	vpor		%ymm0,%ymm6,%ymm6
	# x11 += x15, x7 = rotl32(x7 ^ x11, 12)
	vpaddd		%ymm15,%ymm11,%ymm11
	vpxor		%ymm11,%ymm7,%ymm7
	vpslld		$12,%ymm7,%ymm0
	vpsrld		$20,%ymm7,%ymm7
	vpor		%ymm0,%ymm7,%ymm7

	# x0 += x4, x12 = rotl32(x12 ^ x0, 8)
	vpaddd		0x00(%rsp),%ymm4,%ymm0
	vmovdqa		%ymm0,0x00(%rsp)
	vpxor		%ymm0,%ymm12,%ymm12
	vpshufb		%ymm2,%ymm12,%ymm12
	# x1 += x5, x13 = rotl32(x13 ^ x1, 8)
	vpaddd		0x20(%rsp),%ymm5,%ymm0
	vmovdqa		%ymm0,0x20(%rsp)
	vpxor		%ymm0,%ymm13,%ymm13
	vpshufb		%ymm2,%ymm13,%ymm13
	# x2 += x6, x14 = rotl32(x14 ^ x2, 8)
	vpaddd		0x40(%rsp),%ymm6,%ymm0
	vmovdqa		%ymm0,0x40(%rsp)
	vpxor		%ymm0,%ymm14,%ymm14
	vpshufb		%ymm2,%ymm14,%ymm14
	# x3 += x7, x15 = rotl32(x15 ^ x3, 8)
	vpaddd		0x60(%rsp),%ymm7,%ymm0
	vmovdqa		%ymm0,0x60(%rsp)
	vpxor		%ymm0,%ymm15,%ymm15
	vpshufb		%ymm2,%ymm15,%ymm15

	# x8 += x12, x4 = rotl32(x4 ^ x8, 7)
	vpaddd		%ymm12,%ymm8,%ymm8
	vpxor		%ymm8,%ymm4,%ymm4
	vpslld		$7,%ymm4,%ymm0
	vpsrld		$25,%ymm4,%ymm4
	vpor		%ymm0,%ymm4,%ymm4
	# x9 += x13, x5 = rotl32(x5 ^ x9, 7)
	vpaddd		%ymm13,%ymm9,%ymm9
	vpxor		%ymm9,%ymm5,%ymm5
	vpslld		$7,%ymm5,%ymm0
	vpsrld		$25,%ymm5,%ymm5
	vpor		%ymm0,%ymm5,%ymm5
	# x10 += x14, x6 = rotl32(x6 ^ x10, 7)
	vpaddd		%ymm14,%ymm10,%ymm10
	vpxor		%ymm10,%ymm6,%ymm6
	vpslld		$7,%ymm6,%ymm0
	vpsrld		$25,%ymm6,%ymm6
	vpor		%ymm0,%ymm6,%ymm6
	# x11 += x15, x7 = rotl32(x7 ^ x11, 7)
	vpaddd		%ymm15,%ymm11,%ymm11
	vpxor		%ymm11,%ymm7,%ymm7
	vpslld		$7,%ymm7,%ymm0
	vpsrld		$25,%ymm7,%ymm7
	vpor		%ymm0,%ymm7,%ymm7

	# x0 += x5, x15 = rotl32(x15 ^ x0, 16)
	vpaddd		0x00(%rsp),%ymm5,%ymm0
	vmovdqa		%ymm0,0x00(%rsp)
	vpxor		%ymm0,%ymm15,%ymm15
	vpshufb		%ymm3,%ymm15,%ymm15
	# x1 += x6, x12 = rotl32(x12 ^ x1, 16)%ymm0
	vpaddd		0x20(%rsp),%ymm6,%ymm0
	vmovdqa		%ymm0,0x20(%rsp)
	vpxor		%ymm0,%ymm12,%ymm12
	vpshufb		%ymm3,%ymm12,%ymm12
	# x2 += x7, x13 = rotl32(x13 ^ x2, 16)
	vpaddd		0x40(%rsp),%ymm7,%ymm0
	vmovdqa		%ymm0,0x40(%rsp)
	vpxor		%ymm0,%ymm13,%ymm13
	vpshufb		%ymm3,%ymm13,%ymm13
	# x3 += x4, x14 = rotl32(x14 ^ x3, 16)
	vpaddd		0x60(%rsp),%ymm4,%ymm0
	vmovdqa		%ymm0,0x60(%rsp)
	vpxor		%ymm0,%ymm14,%ymm14
	vpshufb		%ymm3,%ymm14,%ymm14

	# x10 += x15, x5 = rotl32(x5 ^ x10, 12)
	vpaddd		%ymm15,%ymm10,%ymm10
	vpxor		%ymm10,%ymm5,%ymm5
	vpslld		$12,%ymm5,%ymm0
	vpsrld		$20,%ymm5,%ymm5
	vpor		%ymm0,%ymm5,%ymm5
	# x11 += x12, x6 = rotl32(x6 ^ x11, 12)
	vpaddd		%ymm12,%ymm11,%ymm11
	vpxor		%ymm11,%ymm6,%ymm6
	vpslld		$12,%ymm6,%ymm0
	vpsrld		$20,%ymm6,%ymm6
	vpor		%ymm0,%ymm6,%ymm6
	# x8 += x13, x7 = rotl32(x7 ^ x8, 12)
	vpaddd		%ymm13,%ymm8,%ymm8
	vpxor		%ymm8,%ymm7,%ymm7
	vpslld		$12,%ymm7,%ymm0
	vpsrld		$20,%ymm7,%ymm7
	vpor		%ymm0,%ymm7,%ymm7
	# x9 += x14, x4 = rotl32(x4 ^ x9, 12)
	vpaddd		%ymm14,%ymm9,%ymm9
	vpxor		%ymm9,%ymm4,%ymm4
	vpslld		$12,%ymm4,%ymm0
	vpsrld		$20,%ymm4,%ymm4
	vpor		%ymm0,%ymm4,%ymm4

	# x0 += x5, x15 = rotl32(x15 ^ x0, 8)
	vpaddd		0x00(%rsp),%ymm5,%ymm0
	vmovdqa		%ymm0,0x00(%rsp)
	vpxor		%ymm0,%ymm15,%ymm15
	vpshufb		%ymm2,%ymm15,%ymm15
	# x1 += x6, x12 = rotl32(x12 ^ x1, 8)
	vpaddd		0x20(%rsp),%ymm6,%ymm0
	vmovdqa		%ymm0,0x20(%rsp)
	vpxor		%ymm0,%ymm12,%ymm12
	vpshufb		%ymm2,%ymm12,%ymm12
	# x2 += x7, x13 = rotl32(x13 ^ x2, 8)
	vpaddd		0x40(%rsp),%ymm7,%ymm0
	vmovdqa		%ymm0,0x40(%rsp)
	vpxor		%ymm0,%ymm13,%ymm13
	vpshufb		%ymm2,%ymm13,%ymm13
	# x3 += x4, x14 = rotl32(x14 ^ x3, 8)
	vpaddd		0x60(%rsp),%ymm4,%ymm0
	vmovdqa		%ymm0,0x60(%rsp)
	vpxor		%ymm0,%ymm14,%ymm14
	vpshufb		%ymm2,%ymm14,%ymm14

	# x10 += x15, x5 = rotl32(x5 ^ x10, 7)
	vpaddd		%ymm15,%ymm10,%ymm10
	vpxor		%ymm10,%ymm5,%ymm5
	vpslld		$7,%ymm5,%ymm0
	vpsrld		$25,%ymm5,%ymm5
	vpor		%ymm0,%ymm5,%ymm5
	# x11 += x12, x6 = rotl32(x6 ^ x11, 7)
	vpaddd		%ymm12,%ymm11,%ymm11
	vpxor		%ymm11,%ymm6,%ymm6
	vpslld		$7,%ymm6,%ymm0
	vpsrld		$25,%ymm6,%ymm6
	vpor		%ymm0,%ymm6,%ymm6
	# x8 += x13, x7 = rotl32(x7 ^ x8, 7)
	vpaddd		%ymm13,%ymm8,%ymm8
	vpxor		%ymm8,%ymm7,%ymm7
	vpslld		$7,%ymm7,%ymm0
	vpsrld		$25,%ymm7,%ymm7
	vpor		%ymm0,%ymm7,%ymm7
	# x9 += x14, x4 = rotl32(x4 ^ x9, 7)
	vpaddd		%ymm14,%ymm9,%ymm9
	vpxor		%ymm9,%ymm4,%ymm4
	vpslld		$7,%ymm4,%ymm0
	vpsrld		$25,%ymm4,%ymm4
	vpor		%ymm0,%ymm4,%ymm4

	dec		%ecx
	jnz		.Ldoubleround8

	# x0..15[0-3] += s[0..15]
	vpbroadcastd	0x00(%rdi),%ymm0
	vpaddd		0x00(%rsp),%ymm0,%ymm0
	vmovdqa		%ymm0,0x00(%rsp)
	vpbroadcastd	0x04(%rdi),%ymm0
	vpaddd		0x20(%rsp),%ymm0,%ymm0
	vmovdqa		%ymm0,0x20(%rsp)
	vpbroadcastd	0x08(%rdi),%ymm0
	vpaddd		0x40(%rsp),%ymm0,%ymm0
	vmovdqa		%ymm0,0x40(%rsp)
	vpbroadcastd	0x0c(%rdi),%ymm0
	vpaddd		0x60(%rsp),%ymm0,%ymm0
	vmovdqa		%ymm0,0x60(%rsp)
	vpbroadcastd	0x10(%rdi),%ymm0
	vpaddd		%ymm0,%ymm4,%ymm4
	vpbroadcastd	0x14(%rdi),%ymm0
	vpaddd		%ymm0,%ymm5,%ymm5
	vpbroadcastd	0x18(%rdi),%ymm0
	vpaddd		%ymm0,%ymm6,%ymm6
	vpbroadcastd	0x1c(%rdi),%ymm0
	vpaddd		%ymm0,%ymm7,%ymm7
	vpbroadcastd	0x20(%rdi),%ymm0
	vpaddd		%ymm0,%ymm8,%ymm8
	vpbroadcastd	0x24(%rdi),%ymm0
	vpaddd		%ymm0,%ymm9,%ymm9
	vpbroadcastd	0x28(%rdi),%ymm0
	vpaddd		%ymm0,%ymm10,%ymm10
	vpbroadcastd	0x2c(%rdi),%ymm0
	vpaddd		%ymm0,%ymm11,%ymm11
	vpbroadcastd	0x30(%rdi),%ymm0
	vpaddd		%ymm0,%ymm12,%ymm12
	vpbroadcastd	0x34(%rdi),%ymm0
	vpaddd		%ymm0,%ymm13,%ymm13
	vpbroadcastd	0x38(%rdi),%ymm0
	vpaddd		%ymm0,%ymm14,%ymm14
	vpbroadcastd	0x3c(%rdi),%ymm0
	vpaddd		%ymm0,%ymm15,%ymm15

	# x12 += counter values 0-3
	vpaddd		%ymm1,%ymm12,%ymm12

	# interleave 32-bit words in state n, n+1
	vmovdqa		0x00(%rsp),%ymm0
	vmovdqa		0x20(%rsp),%ymm1
	vpunpckldq	%ymm1,%ymm0,%ymm2
	vpunpckhdq	%ymm1,%ymm0,%ymm1
	vmovdqa		%ymm2,0x00(%rsp)
	vmovdqa		%ymm1,0x20(%rsp)
	vmovdqa		0x40(%rsp),%ymm0
	vmovdqa		0x60(%rsp),%ymm1
	vpunpckldq	%ymm1,%ymm0,%ymm2
	vpunpckhdq	%ymm1,%ymm0,%ymm1
	vmovdqa		%ymm2,0x40(%rsp)
	vmovdqa		%ymm1,0x60(%rsp)
	vmovdqa		%ymm4,%ymm0
	vpunpckldq	%ymm5,%ymm0,%ymm4
	vpunpckhdq	%ymm5,%ymm0,%ymm5
	vmovdqa		%ymm6,%ymm0
	vpunpckldq	%ymm7,%ymm0,%ymm6
	vpunpckhdq	%ymm7,%ymm0,%ymm7
	vmovdqa		%ymm8,%ymm0
	vpunpckldq	%ymm9,%ymm0,%ymm8
	vpunpckhdq	%ymm9,%ymm0,%ymm9
	vmovdqa		%ymm10,%ymm0
	vpunpckldq	%ymm11,%ymm0,%ymm10
	vpunpckhdq	%ymm11,%ymm0,%ymm11
	vmovdqa		%ymm12,%ymm0
	vpunpckldq	%ymm13,%ymm0,%ymm12
	vpunpckhdq	%ymm13,%ymm0,%ymm13
	vmovdqa		%ymm14,%ymm0
	vpunpckldq	%ymm15,%ymm0,%ymm14
	vpunpckhdq	%ymm15,%ymm0,%ymm15

	# interleave 64-bit words in state n, n+2
	vmovdqa		0x00(%rsp),%ymm0
	vmovdqa		0x40(%rsp),%ymm2
	vpunpcklqdq	%ymm2,%ymm0,%ymm1
	vpunpckhqdq	%ymm2,%ymm0,%ymm2
	vmovdqa		%ymm1,0x00(%rsp)
	vmovdqa		%ymm2,0x40(%rsp)
	vmovdqa		0x20(%rsp),%ymm0
	vmovdqa		0x60(%rsp),%ymm2
	vpunpcklqdq	%ymm2,%ymm0,%ymm1
	vpunpckhqdq	%ymm2,%ymm0,%ymm2
	vmovdqa		%ymm1,0x20(%rsp)
	vmovdqa		%ymm2,0x60(%rsp)
	vmovdqa		%ymm4,%ymm0
	vpunpcklqdq	%ymm6,%ymm0,%ymm4
	vpunpckhqdq	%ymm6,%ymm0,%ymm6
	vmovdqa		%ymm5,%ymm0
	vpunpcklqdq	%ymm7,%ymm0,%ymm5
	vpunpckhqdq	%ymm7,%ymm0,%ymm7
	vmovdqa		%ymm8,%ymm0
	vpunpcklqdq	%ymm10,%ymm0,%ymm8
	vpunpckhqdq	%ymm10,%ymm0,%ymm10
	vmovdqa		%ymm9,%ymm0
	vpunpcklqdq	%ymm11,%ymm0,%ymm9
	vpunpckhqdq	%ymm11,%ymm0,%ymm11
	vmovdqa		%ymm12,%ymm0
	vpunpcklqdq	%ymm14,%ymm0,%ymm12
	vpunpckhqdq	%ymm14,%ymm0,%ymm14
	vmovdqa		%ymm13,%ymm0
	vpunpcklqdq	%ymm15,%ymm0,%ymm13
	vpunpckhqdq	%ymm15,%ymm0,%ymm15

	# interleave 128-bit words in state n, n+4
	# xor/write first four blocks
	vmovdqa		0x00(%rsp),%ymm1
	vperm2i128	$0x20,%ymm4,%ymm1,%ymm0
	cmp		$0x0020,%rax
	jl		.Lxorpart8
	vpxor		0x0000(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x0000(%rsi)
	vperm2i128	$0x31,%ymm4,%ymm1,%ymm4

	vperm2i128	$0x20,%ymm12,%ymm8,%ymm0
	cmp		$0x0040,%rax
	jl		.Lxorpart8
	vpxor		0x0020(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x0020(%rsi)
	vperm2i128	$0x31,%ymm12,%ymm8,%ymm12

	vmovdqa		0x40(%rsp),%ymm1
	vperm2i128	$0x20,%ymm6,%ymm1,%ymm0
	cmp		$0x0060,%rax
	jl		.Lxorpart8
	vpxor		0x0040(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x0040(%rsi)
	vperm2i128	$0x31,%ymm6,%ymm1,%ymm6

	vperm2i128	$0x20,%ymm14,%ymm10,%ymm0
	cmp		$0x0080,%rax
	jl		.Lxorpart8
	vpxor		0x0060(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x0060(%rsi)
	vperm2i128	$0x31,%ymm14,%ymm10,%ymm14

	vmovdqa		0x20(%rsp),%ymm1
	vperm2i128	$0x20,%ymm5,%ymm1,%ymm0
	cmp		$0x00a0,%rax
	jl		.Lxorpart8
	vpxor		0x0080(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x0080(%rsi)
	vperm2i128	$0x31,%ymm5,%ymm1,%ymm5

	vperm2i128	$0x20,%ymm13,%ymm9,%ymm0
	cmp		$0x00c0,%rax
	jl		.Lxorpart8
	vpxor		0x00a0(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x00a0(%rsi)
	vperm2i128	$0x31,%ymm13,%ymm9,%ymm13

	vmovdqa		0x60(%rsp),%ymm1
	vperm2i128	$0x20,%ymm7,%ymm1,%ymm0
	cmp		$0x00e0,%rax
	jl		.Lxorpart8
	vpxor		0x00c0(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x00c0(%rsi)
	vperm2i128	$0x31,%ymm7,%ymm1,%ymm7

	vperm2i128	$0x20,%ymm15,%ymm11,%ymm0
	cmp		$0x0100,%rax
	jl		.Lxorpart8
	vpxor		0x00e0(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x00e0(%rsi)
	vperm2i128	$0x31,%ymm15,%ymm11,%ymm15

	# xor remaining blocks, write to output
	vmovdqa		%ymm4,%ymm0
	cmp		$0x0120,%rax
	jl		.Lxorpart8
	vpxor		0x0100(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x0100(%rsi)

	vmovdqa		%ymm12,%ymm0
	cmp		$0x0140,%rax
	jl		.Lxorpart8
	vpxor		0x0120(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x0120(%rsi)

	vmovdqa		%ymm6,%ymm0
	cmp		$0x0160,%rax
	jl		.Lxorpart8
	vpxor		0x0140(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x0140(%rsi)

	vmovdqa		%ymm14,%ymm0
	cmp		$0x0180,%rax
	jl		.Lxorpart8
	vpxor		0x0160(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x0160(%rsi)

	vmovdqa		%ymm5,%ymm0
	cmp		$0x01a0,%rax
	jl		.Lxorpart8
	vpxor		0x0180(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x0180(%rsi)

	vmovdqa		%ymm13,%ymm0
	cmp		$0x01c0,%rax
	jl		.Lxorpart8
	vpxor		0x01a0(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x01a0(%rsi)

	vmovdqa		%ymm7,%ymm0
	cmp		$0x01e0,%rax
	jl		.Lxorpart8
	vpxor		0x01c0(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x01c0(%rsi)

	vmovdqa		%ymm15,%ymm0
	cmp		$0x0200,%rax
	jl		.Lxorpart8
	vpxor		0x01e0(%rdx),%ymm0,%ymm0
	vmovdqu		%ymm0,0x01e0(%rsi)

.Ldone8:
	vzeroupper
	lea		-8(%r10),%rsp
	ret

.Lxorpart8:
	# xor remaining bytes from partial register into output
	mov		%rax,%r9
	and		$0x1f,%r9
	jz		.Ldone8
	and		$~0x1f,%rax

	mov		%rsi,%r11

	lea		(%rdx,%rax),%rsi
	mov		%rsp,%rdi
	mov		%r9,%rcx
	rep movsb

	vpxor		0x00(%rsp),%ymm0,%ymm0
	vmovdqa		%ymm0,0x00(%rsp)

	mov		%rsp,%rsi
	lea		(%r11,%rax),%rdi
	mov		%r9,%rcx
	rep movsb

	jmp		.Ldone8

ENDPROC(chacha20_8block_xor_avx2)
