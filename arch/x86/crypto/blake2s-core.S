/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 * Copyright (C) 2017-2019 Samuel Neves <sneves@dei.uc.pt>. All Rights Reserved.
 */

#include <linux/linkage.h>

.section .rodata.cst32.BLAKE2S_IV, "aM", @progbits, 32
.align 32
IV:	.octa 0xA54FF53A3C6EF372BB67AE856A09E667
	.octa 0x5BE0CD191F83D9AB9B05688C510E527F
.section .rodata.cst16.ROT16, "aM", @progbits, 16
.align 16
ROT16:	.octa 0x0D0C0F0E09080B0A0504070601000302
.section .rodata.cst16.ROR328, "aM", @progbits, 16
.align 16
ROR328:	.octa 0x0C0F0E0D080B0A090407060500030201
.section .rodata.cst64.BLAKE2S_SIGMA, "aM", @progbits, 160
.align 64
SIGMA:
.byte  0,  2,  4,  6,  1,  3,  5,  7, 14,  8, 10, 12, 15,  9, 11, 13
.byte 14,  4,  9, 13, 10,  8, 15,  6,  5,  1,  0, 11,  3, 12,  2,  7
.byte 11, 12,  5, 15,  8,  0,  2, 13,  9, 10,  3,  7,  4, 14,  6,  1
.byte  7,  3, 13, 11,  9,  1, 12, 14, 15,  2,  5,  4,  8,  6, 10,  0
.byte  9,  5,  2, 10,  0,  7,  4, 15,  3, 14, 11,  6, 13,  1, 12,  8
.byte  2,  6,  0,  8, 12, 10, 11,  3,  1,  4,  7, 15,  9, 13,  5, 14
.byte 12,  1, 14,  4,  5, 15, 13, 10,  8,  0,  6,  9, 11,  7,  3,  2
.byte 13,  7, 12,  3, 11, 14,  1,  9,  2,  5, 15,  8, 10,  0,  4,  6
.byte  6, 14, 11,  0, 15,  9,  3,  8, 10, 12, 13,  1,  5,  2,  7,  4
.byte 10,  8,  7,  1,  2,  4,  6,  5, 13, 15,  9,  3,  0, 11, 14, 12
#ifdef CONFIG_AS_AVX512
.section .rodata.cst64.BLAKE2S_SIGMA2, "aM", @progbits, 640
.align 64
SIGMA2:
.long  0,  2,  4,  6,  1,  3,  5,  7, 14,  8, 10, 12, 15,  9, 11, 13
.long  8,  2, 13, 15, 10,  9, 12,  3,  6,  4,  0, 14,  5, 11,  1,  7
.long 11, 13,  8,  6,  5, 10, 14,  3,  2,  4, 12, 15,  1,  0,  7,  9
.long 11, 10,  7,  0,  8, 15,  1, 13,  3,  6,  2, 12,  4, 14,  9,  5
.long  4, 10,  9, 14, 15,  0, 11,  8,  1,  7,  3, 13,  2,  5,  6, 12
.long  2, 11,  4, 15, 14,  3, 10,  8, 13,  6,  5,  7,  0, 12,  1,  9
.long  4,  8, 15,  9, 14, 11, 13,  5,  3,  2,  1, 12,  6, 10,  7,  0
.long  6, 13,  0, 14, 12,  2,  1, 11, 15,  4,  5,  8,  7,  9,  3, 10
.long 15,  5,  4, 13, 10,  7,  3, 11, 12,  2,  0,  6,  9,  8,  1, 14
.long  8,  7, 14, 11, 13, 15,  0, 12, 10,  4,  5,  6,  3,  2,  1,  9
#endif /* CONFIG_AS_AVX512 */

.text
SYM_FUNC_START(blake2s_compress_ssse3)
	testq		%rdx,%rdx
	je		.Lendofloop
	movdqu		(%rdi),%xmm0
	movdqu		0x10(%rdi),%xmm1
	movdqa		ROT16(%rip),%xmm12
	movdqa		ROR328(%rip),%xmm13
	movdqu		0x20(%rdi),%xmm14
	movq		%rcx,%xmm15
	leaq		SIGMA+0xa0(%rip),%r8
	jmp		.Lbeginofloop
	.align		32
.Lbeginofloop:
	movdqa		%xmm0,%xmm10
	movdqa		%xmm1,%xmm11
	paddq		%xmm15,%xmm14
	movdqa		IV(%rip),%xmm2
	movdqa		%xmm14,%xmm3
	pxor		IV+0x10(%rip),%xmm3
	leaq		SIGMA(%rip),%rcx
.Lroundloop:
	movzbl		(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm4
	movzbl		0x1(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm5
	movzbl		0x2(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm6
	movzbl		0x3(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm7
	punpckldq	%xmm5,%xmm4
	punpckldq	%xmm7,%xmm6
	punpcklqdq	%xmm6,%xmm4
	paddd		%xmm4,%xmm0
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm12,%xmm3
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm8
	psrld		$0xc,%xmm1
	pslld		$0x14,%xmm8
	por		%xmm8,%xmm1
	movzbl		0x4(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm5
	movzbl		0x5(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm6
	movzbl		0x6(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm7
	movzbl		0x7(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm4
	punpckldq	%xmm6,%xmm5
	punpckldq	%xmm4,%xmm7
	punpcklqdq	%xmm7,%xmm5
	paddd		%xmm5,%xmm0
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm13,%xmm3
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm8
	psrld		$0x7,%xmm1
	pslld		$0x19,%xmm8
	por		%xmm8,%xmm1
	pshufd		$0x93,%xmm0,%xmm0
	pshufd		$0x4e,%xmm3,%xmm3
	pshufd		$0x39,%xmm2,%xmm2
	movzbl		0x8(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm6
	movzbl		0x9(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm7
	movzbl		0xa(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm4
	movzbl		0xb(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm5
	punpckldq	%xmm7,%xmm6
	punpckldq	%xmm5,%xmm4
	punpcklqdq	%xmm4,%xmm6
	paddd		%xmm6,%xmm0
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm12,%xmm3
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm8
	psrld		$0xc,%xmm1
	pslld		$0x14,%xmm8
	por		%xmm8,%xmm1
	movzbl		0xc(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm7
	movzbl		0xd(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm4
	movzbl		0xe(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm5
	movzbl		0xf(%rcx),%eax
	movd		(%rsi,%rax,4),%xmm6
	punpckldq	%xmm4,%xmm7
	punpckldq	%xmm6,%xmm5
	punpcklqdq	%xmm5,%xmm7
	paddd		%xmm7,%xmm0
	paddd		%xmm1,%xmm0
	pxor		%xmm0,%xmm3
	pshufb		%xmm13,%xmm3
	paddd		%xmm3,%xmm2
	pxor		%xmm2,%xmm1
	movdqa		%xmm1,%xmm8
	psrld		$0x7,%xmm1
	pslld		$0x19,%xmm8
	por		%xmm8,%xmm1
	pshufd		$0x39,%xmm0,%xmm0
	pshufd		$0x4e,%xmm3,%xmm3
	pshufd		$0x93,%xmm2,%xmm2
	addq		$0x10,%rcx
	cmpq		%r8,%rcx
	jnz		.Lroundloop
	pxor		%xmm2,%xmm0
	pxor		%xmm3,%xmm1
	pxor		%xmm10,%xmm0
	pxor		%xmm11,%xmm1
	addq		$0x40,%rsi
	decq		%rdx
	jnz		.Lbeginofloop
	movdqu		%xmm0,(%rdi)
	movdqu		%xmm1,0x10(%rdi)
	movdqu		%xmm14,0x20(%rdi)
.Lendofloop:
	ret
SYM_FUNC_END(blake2s_compress_ssse3)

#ifdef CONFIG_AS_AVX512
SYM_FUNC_START(blake2s_compress_avx512)
	vmovdqu		(%rdi),%xmm0
	vmovdqu		0x10(%rdi),%xmm1
	vmovdqu		0x20(%rdi),%xmm4
	vmovq		%rcx,%xmm5
	vmovdqa		IV(%rip),%xmm14
	vmovdqa		IV+16(%rip),%xmm15
	jmp		.Lblake2s_compress_avx512_mainloop
.align 32
.Lblake2s_compress_avx512_mainloop:
	vmovdqa		%xmm0,%xmm10
	vmovdqa		%xmm1,%xmm11
	vpaddq		%xmm5,%xmm4,%xmm4
	vmovdqa		%xmm14,%xmm2
	vpxor		%xmm15,%xmm4,%xmm3
	vmovdqu		(%rsi),%ymm6
	vmovdqu		0x20(%rsi),%ymm7
	addq		$0x40,%rsi
	leaq		SIGMA2(%rip),%rax
	movb		$0xa,%cl
.Lblake2s_compress_avx512_roundloop:
	addq		$0x40,%rax
	vmovdqa		-0x40(%rax),%ymm8
	vmovdqa		-0x20(%rax),%ymm9
	vpermi2d	%ymm7,%ymm6,%ymm8
	vpermi2d	%ymm7,%ymm6,%ymm9
	vmovdqa		%ymm8,%ymm6
	vmovdqa		%ymm9,%ymm7
	vpaddd		%xmm8,%xmm0,%xmm0
	vpaddd		%xmm1,%xmm0,%xmm0
	vpxor		%xmm0,%xmm3,%xmm3
	vprord		$0x10,%xmm3,%xmm3
	vpaddd		%xmm3,%xmm2,%xmm2
	vpxor		%xmm2,%xmm1,%xmm1
	vprord		$0xc,%xmm1,%xmm1
	vextracti128	$0x1,%ymm8,%xmm8
	vpaddd		%xmm8,%xmm0,%xmm0
	vpaddd		%xmm1,%xmm0,%xmm0
	vpxor		%xmm0,%xmm3,%xmm3
	vprord		$0x8,%xmm3,%xmm3
	vpaddd		%xmm3,%xmm2,%xmm2
	vpxor		%xmm2,%xmm1,%xmm1
	vprord		$0x7,%xmm1,%xmm1
	vpshufd		$0x93,%xmm0,%xmm0
	vpshufd		$0x4e,%xmm3,%xmm3
	vpshufd		$0x39,%xmm2,%xmm2
	vpaddd		%xmm9,%xmm0,%xmm0
	vpaddd		%xmm1,%xmm0,%xmm0
	vpxor		%xmm0,%xmm3,%xmm3
	vprord		$0x10,%xmm3,%xmm3
	vpaddd		%xmm3,%xmm2,%xmm2
	vpxor		%xmm2,%xmm1,%xmm1
	vprord		$0xc,%xmm1,%xmm1
	vextracti128	$0x1,%ymm9,%xmm9
	vpaddd		%xmm9,%xmm0,%xmm0
	vpaddd		%xmm1,%xmm0,%xmm0
	vpxor		%xmm0,%xmm3,%xmm3
	vprord		$0x8,%xmm3,%xmm3
	vpaddd		%xmm3,%xmm2,%xmm2
	vpxor		%xmm2,%xmm1,%xmm1
	vprord		$0x7,%xmm1,%xmm1
	vpshufd		$0x39,%xmm0,%xmm0
	vpshufd		$0x4e,%xmm3,%xmm3
	vpshufd		$0x93,%xmm2,%xmm2
	decb		%cl
	jne		.Lblake2s_compress_avx512_roundloop
	vpxor		%xmm10,%xmm0,%xmm0
	vpxor		%xmm11,%xmm1,%xmm1
	vpxor		%xmm2,%xmm0,%xmm0
	vpxor		%xmm3,%xmm1,%xmm1
	decq		%rdx
	jne		.Lblake2s_compress_avx512_mainloop
	vmovdqu		%xmm0,(%rdi)
	vmovdqu		%xmm1,0x10(%rdi)
	vmovdqu		%xmm4,0x20(%rdi)
	vzeroupper
	retq
SYM_FUNC_END(blake2s_compress_avx512)
#endif /* CONFIG_AS_AVX512 */
