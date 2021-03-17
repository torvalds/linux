/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * ChaCha 256-bit cipher algorithm, x64 AVX2 functions
 *
 * Copyright (C) 2015 Martin Willi
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

.section	.rodata.cst32.CTR2BL, "aM", @progbits, 32
.align 32
CTR2BL:	.octa 0x00000000000000000000000000000000
	.octa 0x00000000000000000000000000000001

.section	.rodata.cst32.CTR4BL, "aM", @progbits, 32
.align 32
CTR4BL:	.octa 0x00000000000000000000000000000002
	.octa 0x00000000000000000000000000000003

.text

SYM_FUNC_START(chacha_2block_xor_avx2)
	# %rdi: Input state matrix, s
	# %rsi: up to 2 data blocks output, o
	# %rdx: up to 2 data blocks input, i
	# %rcx: input/output length in bytes
	# %r8d: nrounds

	# This function encrypts two ChaCha blocks by loading the state
	# matrix twice across four AVX registers. It performs matrix operations
	# on four words in each matrix in parallel, but requires shuffling to
	# rearrange the words after each round.

	vzeroupper

	# x0..3[0-2] = s0..3
	vbroadcasti128	0x00(%rdi),%ymm0
	vbroadcasti128	0x10(%rdi),%ymm1
	vbroadcasti128	0x20(%rdi),%ymm2
	vbroadcasti128	0x30(%rdi),%ymm3

	vpaddd		CTR2BL(%rip),%ymm3,%ymm3

	vmovdqa		%ymm0,%ymm8
	vmovdqa		%ymm1,%ymm9
	vmovdqa		%ymm2,%ymm10
	vmovdqa		%ymm3,%ymm11

	vmovdqa		ROT8(%rip),%ymm4
	vmovdqa		ROT16(%rip),%ymm5

	mov		%rcx,%rax

.Ldoubleround:

	# x0 += x1, x3 = rotl32(x3 ^ x0, 16)
	vpaddd		%ymm1,%ymm0,%ymm0
	vpxor		%ymm0,%ymm3,%ymm3
	vpshufb		%ymm5,%ymm3,%ymm3

	# x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	vpaddd		%ymm3,%ymm2,%ymm2
	vpxor		%ymm2,%ymm1,%ymm1
	vmovdqa		%ymm1,%ymm6
	vpslld		$12,%ymm6,%ymm6
	vpsrld		$20,%ymm1,%ymm1
	vpor		%ymm6,%ymm1,%ymm1

	# x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	vpaddd		%ymm1,%ymm0,%ymm0
	vpxor		%ymm0,%ymm3,%ymm3
	vpshufb		%ymm4,%ymm3,%ymm3

	# x2 += x3, x1 = rotl32(x1 ^ x2, 7)
	vpaddd		%ymm3,%ymm2,%ymm2
	vpxor		%ymm2,%ymm1,%ymm1
	vmovdqa		%ymm1,%ymm7
	vpslld		$7,%ymm7,%ymm7
	vpsrld		$25,%ymm1,%ymm1
	vpor		%ymm7,%ymm1,%ymm1

	# x1 = shuffle32(x1, MASK(0, 3, 2, 1))
	vpshufd		$0x39,%ymm1,%ymm1
	# x2 = shuffle32(x2, MASK(1, 0, 3, 2))
	vpshufd		$0x4e,%ymm2,%ymm2
	# x3 = shuffle32(x3, MASK(2, 1, 0, 3))
	vpshufd		$0x93,%ymm3,%ymm3

	# x0 += x1, x3 = rotl32(x3 ^ x0, 16)
	vpaddd		%ymm1,%ymm0,%ymm0
	vpxor		%ymm0,%ymm3,%ymm3
	vpshufb		%ymm5,%ymm3,%ymm3

	# x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	vpaddd		%ymm3,%ymm2,%ymm2
	vpxor		%ymm2,%ymm1,%ymm1
	vmovdqa		%ymm1,%ymm6
	vpslld		$12,%ymm6,%ymm6
	vpsrld		$20,%ymm1,%ymm1
	vpor		%ymm6,%ymm1,%ymm1

	# x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	vpaddd		%ymm1,%ymm0,%ymm0
	vpxor		%ymm0,%ymm3,%ymm3
	vpshufb		%ymm4,%ymm3,%ymm3

	# x2 += x3, x1 = rotl32(x1 ^ x2, 7)
	vpaddd		%ymm3,%ymm2,%ymm2
	vpxor		%ymm2,%ymm1,%ymm1
	vmovdqa		%ymm1,%ymm7
	vpslld		$7,%ymm7,%ymm7
	vpsrld		$25,%ymm1,%ymm1
	vpor		%ymm7,%ymm1,%ymm1

	# x1 = shuffle32(x1, MASK(2, 1, 0, 3))
	vpshufd		$0x93,%ymm1,%ymm1
	# x2 = shuffle32(x2, MASK(1, 0, 3, 2))
	vpshufd		$0x4e,%ymm2,%ymm2
	# x3 = shuffle32(x3, MASK(0, 3, 2, 1))
	vpshufd		$0x39,%ymm3,%ymm3

	sub		$2,%r8d
	jnz		.Ldoubleround

	# o0 = i0 ^ (x0 + s0)
	vpaddd		%ymm8,%ymm0,%ymm7
	cmp		$0x10,%rax
	jl		.Lxorpart2
	vpxor		0x00(%rdx),%xmm7,%xmm6
	vmovdqu		%xmm6,0x00(%rsi)
	vextracti128	$1,%ymm7,%xmm0
	# o1 = i1 ^ (x1 + s1)
	vpaddd		%ymm9,%ymm1,%ymm7
	cmp		$0x20,%rax
	jl		.Lxorpart2
	vpxor		0x10(%rdx),%xmm7,%xmm6
	vmovdqu		%xmm6,0x10(%rsi)
	vextracti128	$1,%ymm7,%xmm1
	# o2 = i2 ^ (x2 + s2)
	vpaddd		%ymm10,%ymm2,%ymm7
	cmp		$0x30,%rax
	jl		.Lxorpart2
	vpxor		0x20(%rdx),%xmm7,%xmm6
	vmovdqu		%xmm6,0x20(%rsi)
	vextracti128	$1,%ymm7,%xmm2
	# o3 = i3 ^ (x3 + s3)
	vpaddd		%ymm11,%ymm3,%ymm7
	cmp		$0x40,%rax
	jl		.Lxorpart2
	vpxor		0x30(%rdx),%xmm7,%xmm6
	vmovdqu		%xmm6,0x30(%rsi)
	vextracti128	$1,%ymm7,%xmm3

	# xor and write second block
	vmovdqa		%xmm0,%xmm7
	cmp		$0x50,%rax
	jl		.Lxorpart2
	vpxor		0x40(%rdx),%xmm7,%xmm6
	vmovdqu		%xmm6,0x40(%rsi)

	vmovdqa		%xmm1,%xmm7
	cmp		$0x60,%rax
	jl		.Lxorpart2
	vpxor		0x50(%rdx),%xmm7,%xmm6
	vmovdqu		%xmm6,0x50(%rsi)

	vmovdqa		%xmm2,%xmm7
	cmp		$0x70,%rax
	jl		.Lxorpart2
	vpxor		0x60(%rdx),%xmm7,%xmm6
	vmovdqu		%xmm6,0x60(%rsi)

	vmovdqa		%xmm3,%xmm7
	cmp		$0x80,%rax
	jl		.Lxorpart2
	vpxor		0x70(%rdx),%xmm7,%xmm6
	vmovdqu		%xmm6,0x70(%rsi)

.Ldone2:
	vzeroupper
	ret

.Lxorpart2:
	# xor remaining bytes from partial register into output
	mov		%rax,%r9
	and		$0x0f,%r9
	jz		.Ldone2
	and		$~0x0f,%rax

	mov		%rsi,%r11

	lea		8(%rsp),%r10
	sub		$0x10,%rsp
	and		$~31,%rsp

	lea		(%rdx,%rax),%rsi
	mov		%rsp,%rdi
	mov		%r9,%rcx
	rep movsb

	vpxor		0x00(%rsp),%xmm7,%xmm7
	vmovdqa		%xmm7,0x00(%rsp)

	mov		%rsp,%rsi
	lea		(%r11,%rax),%rdi
	mov		%r9,%rcx
	rep movsb

	lea		-8(%r10),%rsp
	jmp		.Ldone2

SYM_FUNC_END(chacha_2block_xor_avx2)

SYM_FUNC_START(chacha_4block_xor_avx2)
	# %rdi: Input state matrix, s
	# %rsi: up to 4 data blocks output, o
	# %rdx: up to 4 data blocks input, i
	# %rcx: input/output length in bytes
	# %r8d: nrounds

	# This function encrypts four ChaCha blocks by loading the state
	# matrix four times across eight AVX registers. It performs matrix
	# operations on four words in two matrices in parallel, sequentially
	# to the operations on the four words of the other two matrices. The
	# required word shuffling has a rather high latency, we can do the
	# arithmetic on two matrix-pairs without much slowdown.

	vzeroupper

	# x0..3[0-4] = s0..3
	vbroadcasti128	0x00(%rdi),%ymm0
	vbroadcasti128	0x10(%rdi),%ymm1
	vbroadcasti128	0x20(%rdi),%ymm2
	vbroadcasti128	0x30(%rdi),%ymm3

	vmovdqa		%ymm0,%ymm4
	vmovdqa		%ymm1,%ymm5
	vmovdqa		%ymm2,%ymm6
	vmovdqa		%ymm3,%ymm7

	vpaddd		CTR2BL(%rip),%ymm3,%ymm3
	vpaddd		CTR4BL(%rip),%ymm7,%ymm7

	vmovdqa		%ymm0,%ymm11
	vmovdqa		%ymm1,%ymm12
	vmovdqa		%ymm2,%ymm13
	vmovdqa		%ymm3,%ymm14
	vmovdqa		%ymm7,%ymm15

	vmovdqa		ROT8(%rip),%ymm8
	vmovdqa		ROT16(%rip),%ymm9

	mov		%rcx,%rax

.Ldoubleround4:

	# x0 += x1, x3 = rotl32(x3 ^ x0, 16)
	vpaddd		%ymm1,%ymm0,%ymm0
	vpxor		%ymm0,%ymm3,%ymm3
	vpshufb		%ymm9,%ymm3,%ymm3

	vpaddd		%ymm5,%ymm4,%ymm4
	vpxor		%ymm4,%ymm7,%ymm7
	vpshufb		%ymm9,%ymm7,%ymm7

	# x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	vpaddd		%ymm3,%ymm2,%ymm2
	vpxor		%ymm2,%ymm1,%ymm1
	vmovdqa		%ymm1,%ymm10
	vpslld		$12,%ymm10,%ymm10
	vpsrld		$20,%ymm1,%ymm1
	vpor		%ymm10,%ymm1,%ymm1

	vpaddd		%ymm7,%ymm6,%ymm6
	vpxor		%ymm6,%ymm5,%ymm5
	vmovdqa		%ymm5,%ymm10
	vpslld		$12,%ymm10,%ymm10
	vpsrld		$20,%ymm5,%ymm5
	vpor		%ymm10,%ymm5,%ymm5

	# x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	vpaddd		%ymm1,%ymm0,%ymm0
	vpxor		%ymm0,%ymm3,%ymm3
	vpshufb		%ymm8,%ymm3,%ymm3

	vpaddd		%ymm5,%ymm4,%ymm4
	vpxor		%ymm4,%ymm7,%ymm7
	vpshufb		%ymm8,%ymm7,%ymm7

	# x2 += x3, x1 = rotl32(x1 ^ x2, 7)
	vpaddd		%ymm3,%ymm2,%ymm2
	vpxor		%ymm2,%ymm1,%ymm1
	vmovdqa		%ymm1,%ymm10
	vpslld		$7,%ymm10,%ymm10
	vpsrld		$25,%ymm1,%ymm1
	vpor		%ymm10,%ymm1,%ymm1

	vpaddd		%ymm7,%ymm6,%ymm6
	vpxor		%ymm6,%ymm5,%ymm5
	vmovdqa		%ymm5,%ymm10
	vpslld		$7,%ymm10,%ymm10
	vpsrld		$25,%ymm5,%ymm5
	vpor		%ymm10,%ymm5,%ymm5

	# x1 = shuffle32(x1, MASK(0, 3, 2, 1))
	vpshufd		$0x39,%ymm1,%ymm1
	vpshufd		$0x39,%ymm5,%ymm5
	# x2 = shuffle32(x2, MASK(1, 0, 3, 2))
	vpshufd		$0x4e,%ymm2,%ymm2
	vpshufd		$0x4e,%ymm6,%ymm6
	# x3 = shuffle32(x3, MASK(2, 1, 0, 3))
	vpshufd		$0x93,%ymm3,%ymm3
	vpshufd		$0x93,%ymm7,%ymm7

	# x0 += x1, x3 = rotl32(x3 ^ x0, 16)
	vpaddd		%ymm1,%ymm0,%ymm0
	vpxor		%ymm0,%ymm3,%ymm3
	vpshufb		%ymm9,%ymm3,%ymm3

	vpaddd		%ymm5,%ymm4,%ymm4
	vpxor		%ymm4,%ymm7,%ymm7
	vpshufb		%ymm9,%ymm7,%ymm7

	# x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	vpaddd		%ymm3,%ymm2,%ymm2
	vpxor		%ymm2,%ymm1,%ymm1
	vmovdqa		%ymm1,%ymm10
	vpslld		$12,%ymm10,%ymm10
	vpsrld		$20,%ymm1,%ymm1
	vpor		%ymm10,%ymm1,%ymm1

	vpaddd		%ymm7,%ymm6,%ymm6
	vpxor		%ymm6,%ymm5,%ymm5
	vmovdqa		%ymm5,%ymm10
	vpslld		$12,%ymm10,%ymm10
	vpsrld		$20,%ymm5,%ymm5
	vpor		%ymm10,%ymm5,%ymm5

	# x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	vpaddd		%ymm1,%ymm0,%ymm0
	vpxor		%ymm0,%ymm3,%ymm3
	vpshufb		%ymm8,%ymm3,%ymm3

	vpaddd		%ymm5,%ymm4,%ymm4
	vpxor		%ymm4,%ymm7,%ymm7
	vpshufb		%ymm8,%ymm7,%ymm7

	# x2 += x3, x1 = rotl32(x1 ^ x2, 7)
	vpaddd		%ymm3,%ymm2,%ymm2
	vpxor		%ymm2,%ymm1,%ymm1
	vmovdqa		%ymm1,%ymm10
	vpslld		$7,%ymm10,%ymm10
	vpsrld		$25,%ymm1,%ymm1
	vpor		%ymm10,%ymm1,%ymm1

	vpaddd		%ymm7,%ymm6,%ymm6
	vpxor		%ymm6,%ymm5,%ymm5
	vmovdqa		%ymm5,%ymm10
	vpslld		$7,%ymm10,%ymm10
	vpsrld		$25,%ymm5,%ymm5
	vpor		%ymm10,%ymm5,%ymm5

	# x1 = shuffle32(x1, MASK(2, 1, 0, 3))
	vpshufd		$0x93,%ymm1,%ymm1
	vpshufd		$0x93,%ymm5,%ymm5
	# x2 = shuffle32(x2, MASK(1, 0, 3, 2))
	vpshufd		$0x4e,%ymm2,%ymm2
	vpshufd		$0x4e,%ymm6,%ymm6
	# x3 = shuffle32(x3, MASK(0, 3, 2, 1))
	vpshufd		$0x39,%ymm3,%ymm3
	vpshufd		$0x39,%ymm7,%ymm7

	sub		$2,%r8d
	jnz		.Ldoubleround4

	# o0 = i0 ^ (x0 + s0), first block
	vpaddd		%ymm11,%ymm0,%ymm10
	cmp		$0x10,%rax
	jl		.Lxorpart4
	vpxor		0x00(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0x00(%rsi)
	vextracti128	$1,%ymm10,%xmm0
	# o1 = i1 ^ (x1 + s1), first block
	vpaddd		%ymm12,%ymm1,%ymm10
	cmp		$0x20,%rax
	jl		.Lxorpart4
	vpxor		0x10(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0x10(%rsi)
	vextracti128	$1,%ymm10,%xmm1
	# o2 = i2 ^ (x2 + s2), first block
	vpaddd		%ymm13,%ymm2,%ymm10
	cmp		$0x30,%rax
	jl		.Lxorpart4
	vpxor		0x20(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0x20(%rsi)
	vextracti128	$1,%ymm10,%xmm2
	# o3 = i3 ^ (x3 + s3), first block
	vpaddd		%ymm14,%ymm3,%ymm10
	cmp		$0x40,%rax
	jl		.Lxorpart4
	vpxor		0x30(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0x30(%rsi)
	vextracti128	$1,%ymm10,%xmm3

	# xor and write second block
	vmovdqa		%xmm0,%xmm10
	cmp		$0x50,%rax
	jl		.Lxorpart4
	vpxor		0x40(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0x40(%rsi)

	vmovdqa		%xmm1,%xmm10
	cmp		$0x60,%rax
	jl		.Lxorpart4
	vpxor		0x50(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0x50(%rsi)

	vmovdqa		%xmm2,%xmm10
	cmp		$0x70,%rax
	jl		.Lxorpart4
	vpxor		0x60(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0x60(%rsi)

	vmovdqa		%xmm3,%xmm10
	cmp		$0x80,%rax
	jl		.Lxorpart4
	vpxor		0x70(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0x70(%rsi)

	# o0 = i0 ^ (x0 + s0), third block
	vpaddd		%ymm11,%ymm4,%ymm10
	cmp		$0x90,%rax
	jl		.Lxorpart4
	vpxor		0x80(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0x80(%rsi)
	vextracti128	$1,%ymm10,%xmm4
	# o1 = i1 ^ (x1 + s1), third block
	vpaddd		%ymm12,%ymm5,%ymm10
	cmp		$0xa0,%rax
	jl		.Lxorpart4
	vpxor		0x90(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0x90(%rsi)
	vextracti128	$1,%ymm10,%xmm5
	# o2 = i2 ^ (x2 + s2), third block
	vpaddd		%ymm13,%ymm6,%ymm10
	cmp		$0xb0,%rax
	jl		.Lxorpart4
	vpxor		0xa0(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0xa0(%rsi)
	vextracti128	$1,%ymm10,%xmm6
	# o3 = i3 ^ (x3 + s3), third block
	vpaddd		%ymm15,%ymm7,%ymm10
	cmp		$0xc0,%rax
	jl		.Lxorpart4
	vpxor		0xb0(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0xb0(%rsi)
	vextracti128	$1,%ymm10,%xmm7

	# xor and write fourth block
	vmovdqa		%xmm4,%xmm10
	cmp		$0xd0,%rax
	jl		.Lxorpart4
	vpxor		0xc0(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0xc0(%rsi)

	vmovdqa		%xmm5,%xmm10
	cmp		$0xe0,%rax
	jl		.Lxorpart4
	vpxor		0xd0(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0xd0(%rsi)

	vmovdqa		%xmm6,%xmm10
	cmp		$0xf0,%rax
	jl		.Lxorpart4
	vpxor		0xe0(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0xe0(%rsi)

	vmovdqa		%xmm7,%xmm10
	cmp		$0x100,%rax
	jl		.Lxorpart4
	vpxor		0xf0(%rdx),%xmm10,%xmm9
	vmovdqu		%xmm9,0xf0(%rsi)

.Ldone4:
	vzeroupper
	ret

.Lxorpart4:
	# xor remaining bytes from partial register into output
	mov		%rax,%r9
	and		$0x0f,%r9
	jz		.Ldone4
	and		$~0x0f,%rax

	mov		%rsi,%r11

	lea		8(%rsp),%r10
	sub		$0x10,%rsp
	and		$~31,%rsp

	lea		(%rdx,%rax),%rsi
	mov		%rsp,%rdi
	mov		%r9,%rcx
	rep movsb

	vpxor		0x00(%rsp),%xmm10,%xmm10
	vmovdqa		%xmm10,0x00(%rsp)

	mov		%rsp,%rsi
	lea		(%r11,%rax),%rdi
	mov		%r9,%rcx
	rep movsb

	lea		-8(%r10),%rsp
	jmp		.Ldone4

SYM_FUNC_END(chacha_4block_xor_avx2)

SYM_FUNC_START(chacha_8block_xor_avx2)
	# %rdi: Input state matrix, s
	# %rsi: up to 8 data blocks output, o
	# %rdx: up to 8 data blocks input, i
	# %rcx: input/output length in bytes
	# %r8d: nrounds

	# This function encrypts eight consecutive ChaCha blocks by loading
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

	sub		$2,%r8d
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

SYM_FUNC_END(chacha_8block_xor_avx2)
