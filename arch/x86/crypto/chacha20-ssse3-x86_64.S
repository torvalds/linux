/*
 * ChaCha20 256-bit cipher algorithm, RFC7539, x64 SSSE3 functions
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

ROT8:	.octa 0x0e0d0c0f0a09080b0605040702010003
ROT16:	.octa 0x0d0c0f0e09080b0a0504070601000302

.text

ENTRY(chacha20_block_xor_ssse3)
	# %rdi: Input state matrix, s
	# %rsi: 1 data block output, o
	# %rdx: 1 data block input, i

	# This function encrypts one ChaCha20 block by loading the state matrix
	# in four SSE registers. It performs matrix operation on four words in
	# parallel, but requireds shuffling to rearrange the words after each
	# round. 8/16-bit word rotation is done with the slightly better
	# performing SSSE3 byte shuffling, 7/12-bit word rotation uses
	# traditional shift+OR.

	# x0..3 = s0..3
	movdqa		0x00(%rdi),%xmm0
	movdqa		0x10(%rdi),%xmm1
	movdqa		0x20(%rdi),%xmm2
	movdqa		0x30(%rdi),%xmm3
	movdqa		%xmm0,%xmm8
	movdqa		%xmm1,%xmm9
	movdqa		%xmm2,%xmm10
	movdqa		%xmm3,%xmm11

	movdqa		ROT8(%rip),%xmm4
	movdqa		ROT16(%rip),%xmm5

	mov	$10,%ecx

.Ldoubleround:

	# x0 += x1, x3 = rotl32(x3 ^ x0, 16)
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm5,%xmm3

	# x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm6
	pslld		$12,%xmm6
	psrld		$20,%xmm1
	por		%xmm6,%xmm1

	# x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm4,%xmm3

	# x2 += x3, x1 = rotl32(x1 ^ x2, 7)
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm7
	pslld		$7,%xmm7
	psrld		$25,%xmm1
	por		%xmm7,%xmm1

	# x1 = shuffle32(x1, MASK(0, 3, 2, 1))
	pshufd		$0x39,%xmm1,%xmm1
	# x2 = shuffle32(x2, MASK(1, 0, 3, 2))
	pshufd		$0x4e,%xmm2,%xmm2
	# x3 = shuffle32(x3, MASK(2, 1, 0, 3))
	pshufd		$0x93,%xmm3,%xmm3

	# x0 += x1, x3 = rotl32(x3 ^ x0, 16)
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm5,%xmm3

	# x2 += x3, x1 = rotl32(x1 ^ x2, 12)
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm6
	pslld		$12,%xmm6
	psrld		$20,%xmm1
	por		%xmm6,%xmm1

	# x0 += x1, x3 = rotl32(x3 ^ x0, 8)
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm4,%xmm3

	# x2 += x3, x1 = rotl32(x1 ^ x2, 7)
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm7
	pslld		$7,%xmm7
	psrld		$25,%xmm1
	por		%xmm7,%xmm1

	# x1 = shuffle32(x1, MASK(2, 1, 0, 3))
	pshufd		$0x93,%xmm1,%xmm1
	# x2 = shuffle32(x2, MASK(1, 0, 3, 2))
	pshufd		$0x4e,%xmm2,%xmm2
	# x3 = shuffle32(x3, MASK(0, 3, 2, 1))
	pshufd		$0x39,%xmm3,%xmm3

	dec		%ecx
	jnz		.Ldoubleround

	# o0 = i0 ^ (x0 + s0)
	movdqu		0x00(%rdx),%xmm4
	paddd		%xmm8,%xmm0
	pxor		%xmm4,%xmm0
	movdqu		%xmm0,0x00(%rsi)
	# o1 = i1 ^ (x1 + s1)
	movdqu		0x10(%rdx),%xmm5
	paddd		%xmm9,%xmm1
	pxor		%xmm5,%xmm1
	movdqu		%xmm1,0x10(%rsi)
	# o2 = i2 ^ (x2 + s2)
	movdqu		0x20(%rdx),%xmm6
	paddd		%xmm10,%xmm2
	pxor		%xmm6,%xmm2
	movdqu		%xmm2,0x20(%rsi)
	# o3 = i3 ^ (x3 + s3)
	movdqu		0x30(%rdx),%xmm7
	paddd		%xmm11,%xmm3
	pxor		%xmm7,%xmm3
	movdqu		%xmm3,0x30(%rsi)

	ret
ENDPROC(chacha20_block_xor_ssse3)
