#! /usr/bin/env perl
# Copyright 2013-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================

# Multi-buffer SHA256 procedure processes n buffers in parallel by
# placing buffer data to designated lane of SIMD register. n is
# naturally limited to 4 on pre-AVX2 processors and to 8 on
# AVX2-capable processors such as Haswell.
#
#		this	+aesni(i)	sha256	aesni-sha256	gain(iv)
# -------------------------------------------------------------------
# Westmere(ii)	23.3/n	+1.28=7.11(n=4)	12.3	+3.75=16.1	+126%
# Atom(ii)	38.7/n	+3.93=13.6(n=4)	20.8	+5.69=26.5	+95%
# Sandy Bridge	(20.5	+5.15=25.7)/n	11.6	13.0		+103%
# Ivy Bridge	(20.4	+5.14=25.5)/n	10.3	11.6		+82%
# Haswell(iii)	(21.0	+5.00=26.0)/n	7.80	8.79		+170%
# Skylake	(18.9	+5.00=23.9)/n	7.70	8.17		+170%
# Bulldozer	(21.6	+5.76=27.4)/n	13.6	13.7		+100%
#
# (i)	multi-block CBC encrypt with 128-bit key;
# (ii)	(HASH+AES)/n does not apply to Westmere for n>3 and Atom,
#	because of lower AES-NI instruction throughput, nor is there
#	AES-NI-SHA256 stitch for these processors;
# (iii)	"this" is for n=8, when we gather twice as much data, result
#	for n=4 is 20.3+4.44=24.7;
# (iv)	presented improvement coefficients are asymptotic limits and
#	in real-life application are somewhat lower, e.g. for 2KB
#	fragments they range from 75% to 130% (on Haswell);

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

$avx=0;

if (`$ENV{CC} -Wa,-v -c -o /dev/null -x assembler /dev/null 2>&1`
		=~ /GNU assembler version ([2-9]\.[0-9]+)/) {
	$avx = ($1>=2.19) + ($1>=2.22);
}

if (!$avx && $win64 && ($flavour =~ /nasm/ || $ENV{ASM} =~ /nasm/) &&
	   `nasm -v 2>&1` =~ /NASM version ([2-9]\.[0-9]+)/) {
	$avx = ($1>=2.09) + ($1>=2.10);
}

if (!$avx && $win64 && ($flavour =~ /masm/ || $ENV{ASM} =~ /ml64/) &&
	   `ml64 2>&1` =~ /Version ([0-9]+)\./) {
	$avx = ($1>=10) + ($1>=11);
}

if (!$avx && `$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|.*based on LLVM) ([3-9]\.[0-9]+)/) {
	$avx = ($2>=3.0) + ($2>3.0);
}

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

# void sha256_multi_block (
#     struct {	unsigned int A[8];
#		unsigned int B[8];
#		unsigned int C[8];
#		unsigned int D[8];
#		unsigned int E[8];
#		unsigned int F[8];
#		unsigned int G[8];
#		unsigned int H[8];	} *ctx,
#     struct {	void *ptr; int blocks;	} inp[8],
#     int num);		/* 1 or 2 */
#
$ctx="%rdi";	# 1st arg
$inp="%rsi";	# 2nd arg
$num="%edx";	# 3rd arg
@ptr=map("%r$_",(8..11));
$Tbl="%rbp";

@V=($A,$B,$C,$D,$E,$F,$G,$H)=map("%xmm$_",(8..15));
($t1,$t2,$t3,$axb,$bxc,$Xi,$Xn,$sigma)=map("%xmm$_",(0..7));

$REG_SZ=16;

sub Xi_off {
my $off = shift;

    $off %= 16; $off *= $REG_SZ;
    $off<256 ? "$off-128(%rax)" : "$off-256-128(%rbx)";
}

sub ROUND_00_15 {
my ($i,$a,$b,$c,$d,$e,$f,$g,$h)=@_;

$code.=<<___ if ($i<15);
	movd		`4*$i`(@ptr[0]),$Xi
	movd		`4*$i`(@ptr[1]),$t1
	movd		`4*$i`(@ptr[2]),$t2
	movd		`4*$i`(@ptr[3]),$t3
	punpckldq	$t2,$Xi
	punpckldq	$t3,$t1
	punpckldq	$t1,$Xi
___
$code.=<<___ if ($i==15);
	movd		`4*$i`(@ptr[0]),$Xi
	 lea		`16*4`(@ptr[0]),@ptr[0]
	movd		`4*$i`(@ptr[1]),$t1
	 lea		`16*4`(@ptr[1]),@ptr[1]
	movd		`4*$i`(@ptr[2]),$t2
	 lea		`16*4`(@ptr[2]),@ptr[2]
	movd		`4*$i`(@ptr[3]),$t3
	 lea		`16*4`(@ptr[3]),@ptr[3]
	punpckldq	$t2,$Xi
	punpckldq	$t3,$t1
	punpckldq	$t1,$Xi
___
$code.=<<___;
	movdqa	$e,$sigma
	`"pshufb	$Xn,$Xi"		if ($i<=15 && ($i&1)==0)`
	movdqa	$e,$t3
	`"pshufb	$Xn,$Xi"		if ($i<=15 && ($i&1)==1)`
	psrld	\$6,$sigma
	movdqa	$e,$t2
	pslld	\$7,$t3
	movdqa	$Xi,`&Xi_off($i)`
	 paddd	$h,$Xi				# Xi+=h

	psrld	\$11,$t2
	pxor	$t3,$sigma
	pslld	\$21-7,$t3
	 paddd	`32*($i%8)-128`($Tbl),$Xi	# Xi+=K[round]
	pxor	$t2,$sigma

	psrld	\$25-11,$t2
	 movdqa	$e,$t1
	 `"prefetcht0	63(@ptr[0])"		if ($i==15)`
	pxor	$t3,$sigma
	 movdqa	$e,$axb				# borrow $axb
	pslld	\$26-21,$t3
	 pandn	$g,$t1
	 pand	$f,$axb
	pxor	$t2,$sigma

	 `"prefetcht0	63(@ptr[1])"		if ($i==15)`
	movdqa	$a,$t2
	pxor	$t3,$sigma			# Sigma1(e)
	movdqa	$a,$t3
	psrld	\$2,$t2
	paddd	$sigma,$Xi			# Xi+=Sigma1(e)
	 pxor	$axb,$t1			# Ch(e,f,g)
	 movdqa	$b,$axb
	movdqa	$a,$sigma
	pslld	\$10,$t3
	 pxor	$a,$axb				# a^b, b^c in next round

	 `"prefetcht0	63(@ptr[2])"		if ($i==15)`
	psrld	\$13,$sigma
	pxor	$t3,$t2
	 paddd	$t1,$Xi				# Xi+=Ch(e,f,g)
	pslld	\$19-10,$t3
	 pand	$axb,$bxc
	pxor	$sigma,$t2

	 `"prefetcht0	63(@ptr[3])"		if ($i==15)`
	psrld	\$22-13,$sigma
	pxor	$t3,$t2
	 movdqa	$b,$h
	pslld	\$30-19,$t3
	pxor	$t2,$sigma
	 pxor	$bxc,$h				# h=Maj(a,b,c)=Ch(a^b,c,b)
	 paddd	$Xi,$d				# d+=Xi
	pxor	$t3,$sigma			# Sigma0(a)

	paddd	$Xi,$h				# h+=Xi
	paddd	$sigma,$h			# h+=Sigma0(a)
___
$code.=<<___ if (($i%8)==7);
	lea	`32*8`($Tbl),$Tbl
___
	($axb,$bxc)=($bxc,$axb);
}

sub ROUND_16_XX {
my $i=shift;

$code.=<<___;
	movdqa	`&Xi_off($i+1)`,$Xn
	paddd	`&Xi_off($i+9)`,$Xi		# Xi+=X[i+9]

	movdqa	$Xn,$sigma
	movdqa	$Xn,$t2
	psrld	\$3,$sigma
	movdqa	$Xn,$t3

	psrld	\$7,$t2
	movdqa	`&Xi_off($i+14)`,$t1
	pslld	\$14,$t3
	pxor	$t2,$sigma
	psrld	\$18-7,$t2
	movdqa	$t1,$axb			# borrow $axb
	pxor	$t3,$sigma
	pslld	\$25-14,$t3
	pxor	$t2,$sigma
	psrld	\$10,$t1
	movdqa	$axb,$t2

	psrld	\$17,$axb
	pxor	$t3,$sigma			# sigma0(X[i+1])
	pslld	\$13,$t2
	 paddd	$sigma,$Xi			# Xi+=sigma0(e)
	pxor	$axb,$t1
	psrld	\$19-17,$axb
	pxor	$t2,$t1
	pslld	\$15-13,$t2
	pxor	$axb,$t1
	pxor	$t2,$t1				# sigma0(X[i+14])
	paddd	$t1,$Xi				# Xi+=sigma1(X[i+14])
___
	&ROUND_00_15($i,@_);
	($Xi,$Xn)=($Xn,$Xi);
}

$code.=<<___;
.text

.extern	OPENSSL_ia32cap_P

.globl	sha256_multi_block
.type	sha256_multi_block,\@function,3
.align	32
sha256_multi_block:
.cfi_startproc
	mov	OPENSSL_ia32cap_P+4(%rip),%rcx
	bt	\$61,%rcx			# check SHA bit
	jc	_shaext_shortcut
___
$code.=<<___ if ($avx);
	test	\$`1<<28`,%ecx
	jnz	_avx_shortcut
___
$code.=<<___;
	mov	%rsp,%rax
.cfi_def_cfa_register	%rax
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
___
$code.=<<___ if ($win64);
	lea	-0xa8(%rsp),%rsp
	movaps	%xmm6,(%rsp)
	movaps	%xmm7,0x10(%rsp)
	movaps	%xmm8,0x20(%rsp)
	movaps	%xmm9,0x30(%rsp)
	movaps	%xmm10,-0x78(%rax)
	movaps	%xmm11,-0x68(%rax)
	movaps	%xmm12,-0x58(%rax)
	movaps	%xmm13,-0x48(%rax)
	movaps	%xmm14,-0x38(%rax)
	movaps	%xmm15,-0x28(%rax)
___
$code.=<<___;
	sub	\$`$REG_SZ*18`, %rsp
	and	\$-256,%rsp
	mov	%rax,`$REG_SZ*17`(%rsp)		# original %rsp
.cfi_cfa_expression	%rsp+`$REG_SZ*17`,deref,+8
.Lbody:
	lea	K256+128(%rip),$Tbl
	lea	`$REG_SZ*16`(%rsp),%rbx
	lea	0x80($ctx),$ctx			# size optimization

.Loop_grande:
	mov	$num,`$REG_SZ*17+8`(%rsp)	# original $num
	xor	$num,$num
___
for($i=0;$i<4;$i++) {
    $code.=<<___;
	mov	`16*$i+0`($inp),@ptr[$i]	# input pointer
	mov	`16*$i+8`($inp),%ecx		# number of blocks
	cmp	$num,%ecx
	cmovg	%ecx,$num			# find maximum
	test	%ecx,%ecx
	mov	%ecx,`4*$i`(%rbx)		# initialize counters
	cmovle	$Tbl,@ptr[$i]			# cancel input
___
}
$code.=<<___;
	test	$num,$num
	jz	.Ldone

	movdqu	0x00-0x80($ctx),$A		# load context
	 lea	128(%rsp),%rax
	movdqu	0x20-0x80($ctx),$B
	movdqu	0x40-0x80($ctx),$C
	movdqu	0x60-0x80($ctx),$D
	movdqu	0x80-0x80($ctx),$E
	movdqu	0xa0-0x80($ctx),$F
	movdqu	0xc0-0x80($ctx),$G
	movdqu	0xe0-0x80($ctx),$H
	movdqu	.Lpbswap(%rip),$Xn
	jmp	.Loop

.align	32
.Loop:
	movdqa	$C,$bxc
	pxor	$B,$bxc				# magic seed
___
for($i=0;$i<16;$i++)	{ &ROUND_00_15($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	movdqu	`&Xi_off($i)`,$Xi
	mov	\$3,%ecx
	jmp	.Loop_16_xx
.align	32
.Loop_16_xx:
___
for(;$i<32;$i++)	{ &ROUND_16_XX($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	dec	%ecx
	jnz	.Loop_16_xx

	mov	\$1,%ecx
	lea	K256+128(%rip),$Tbl

	movdqa	(%rbx),$sigma			# pull counters
	cmp	4*0(%rbx),%ecx			# examine counters
	pxor	$t1,$t1
	cmovge	$Tbl,@ptr[0]			# cancel input
	cmp	4*1(%rbx),%ecx
	movdqa	$sigma,$Xn
	cmovge	$Tbl,@ptr[1]
	cmp	4*2(%rbx),%ecx
	pcmpgtd	$t1,$Xn				# mask value
	cmovge	$Tbl,@ptr[2]
	cmp	4*3(%rbx),%ecx
	paddd	$Xn,$sigma			# counters--
	cmovge	$Tbl,@ptr[3]

	movdqu	0x00-0x80($ctx),$t1
	pand	$Xn,$A
	movdqu	0x20-0x80($ctx),$t2
	pand	$Xn,$B
	movdqu	0x40-0x80($ctx),$t3
	pand	$Xn,$C
	movdqu	0x60-0x80($ctx),$Xi
	pand	$Xn,$D
	paddd	$t1,$A
	movdqu	0x80-0x80($ctx),$t1
	pand	$Xn,$E
	paddd	$t2,$B
	movdqu	0xa0-0x80($ctx),$t2
	pand	$Xn,$F
	paddd	$t3,$C
	movdqu	0xc0-0x80($ctx),$t3
	pand	$Xn,$G
	paddd	$Xi,$D
	movdqu	0xe0-0x80($ctx),$Xi
	pand	$Xn,$H
	paddd	$t1,$E
	paddd	$t2,$F
	movdqu	$A,0x00-0x80($ctx)
	paddd	$t3,$G
	movdqu	$B,0x20-0x80($ctx)
	paddd	$Xi,$H
	movdqu	$C,0x40-0x80($ctx)
	movdqu	$D,0x60-0x80($ctx)
	movdqu	$E,0x80-0x80($ctx)
	movdqu	$F,0xa0-0x80($ctx)
	movdqu	$G,0xc0-0x80($ctx)
	movdqu	$H,0xe0-0x80($ctx)

	movdqa	$sigma,(%rbx)			# save counters
	movdqa	.Lpbswap(%rip),$Xn
	dec	$num
	jnz	.Loop

	mov	`$REG_SZ*17+8`(%rsp),$num
	lea	$REG_SZ($ctx),$ctx
	lea	`16*$REG_SZ/4`($inp),$inp
	dec	$num
	jnz	.Loop_grande

.Ldone:
	mov	`$REG_SZ*17`(%rsp),%rax		# original %rsp
.cfi_def_cfa	%rax,8
___
$code.=<<___ if ($win64);
	movaps	-0xb8(%rax),%xmm6
	movaps	-0xa8(%rax),%xmm7
	movaps	-0x98(%rax),%xmm8
	movaps	-0x88(%rax),%xmm9
	movaps	-0x78(%rax),%xmm10
	movaps	-0x68(%rax),%xmm11
	movaps	-0x58(%rax),%xmm12
	movaps	-0x48(%rax),%xmm13
	movaps	-0x38(%rax),%xmm14
	movaps	-0x28(%rax),%xmm15
___
$code.=<<___;
	mov	-16(%rax),%rbp
.cfi_restore	%rbp
	mov	-8(%rax),%rbx
.cfi_restore	%rbx
	lea	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue:
	ret
.cfi_endproc
.size	sha256_multi_block,.-sha256_multi_block
___
						{{{
my ($Wi,$TMP0,$TMP1,$TMPx,$ABEF0,$CDGH0,$ABEF1,$CDGH1)=map("%xmm$_",(0..3,12..15));
my @MSG0=map("%xmm$_",(4..7));
my @MSG1=map("%xmm$_",(8..11));

$code.=<<___;
.type	sha256_multi_block_shaext,\@function,3
.align	32
sha256_multi_block_shaext:
.cfi_startproc
_shaext_shortcut:
	mov	%rsp,%rax
.cfi_def_cfa_register	%rax
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
___
$code.=<<___ if ($win64);
	lea	-0xa8(%rsp),%rsp
	movaps	%xmm6,(%rsp)
	movaps	%xmm7,0x10(%rsp)
	movaps	%xmm8,0x20(%rsp)
	movaps	%xmm9,0x30(%rsp)
	movaps	%xmm10,-0x78(%rax)
	movaps	%xmm11,-0x68(%rax)
	movaps	%xmm12,-0x58(%rax)
	movaps	%xmm13,-0x48(%rax)
	movaps	%xmm14,-0x38(%rax)
	movaps	%xmm15,-0x28(%rax)
___
$code.=<<___;
	sub	\$`$REG_SZ*18`,%rsp
	shl	\$1,$num			# we process pair at a time
	and	\$-256,%rsp
	lea	0x80($ctx),$ctx			# size optimization
	mov	%rax,`$REG_SZ*17`(%rsp)		# original %rsp
.Lbody_shaext:
	lea	`$REG_SZ*16`(%rsp),%rbx
	lea	K256_shaext+0x80(%rip),$Tbl

.Loop_grande_shaext:
	mov	$num,`$REG_SZ*17+8`(%rsp)	# original $num
	xor	$num,$num
___
for($i=0;$i<2;$i++) {
    $code.=<<___;
	mov	`16*$i+0`($inp),@ptr[$i]	# input pointer
	mov	`16*$i+8`($inp),%ecx		# number of blocks
	cmp	$num,%ecx
	cmovg	%ecx,$num			# find maximum
	test	%ecx,%ecx
	mov	%ecx,`4*$i`(%rbx)		# initialize counters
	cmovle	%rsp,@ptr[$i]			# cancel input
___
}
$code.=<<___;
	test	$num,$num
	jz	.Ldone_shaext

	movq		0x00-0x80($ctx),$ABEF0		# A1.A0
	movq		0x20-0x80($ctx),@MSG0[0]	# B1.B0
	movq		0x40-0x80($ctx),$CDGH0		# C1.C0
	movq		0x60-0x80($ctx),@MSG0[1]	# D1.D0
	movq		0x80-0x80($ctx),@MSG1[0]	# E1.E0
	movq		0xa0-0x80($ctx),@MSG1[1]	# F1.F0
	movq		0xc0-0x80($ctx),@MSG1[2]	# G1.G0
	movq		0xe0-0x80($ctx),@MSG1[3]	# H1.H0

	punpckldq	@MSG0[0],$ABEF0			# B1.A1.B0.A0
	punpckldq	@MSG0[1],$CDGH0			# D1.C1.D0.C0
	punpckldq	@MSG1[1],@MSG1[0]		# F1.E1.F0.E0
	punpckldq	@MSG1[3],@MSG1[2]		# H1.G1.H0.G0
	movdqa		K256_shaext-0x10(%rip),$TMPx	# byte swap

	movdqa		$ABEF0,$ABEF1
	movdqa		$CDGH0,$CDGH1
	punpcklqdq	@MSG1[0],$ABEF0			# F0.E0.B0.A0
	punpcklqdq	@MSG1[2],$CDGH0			# H0.G0.D0.C0
	punpckhqdq	@MSG1[0],$ABEF1			# F1.E1.B1.A1
	punpckhqdq	@MSG1[2],$CDGH1			# H1.G1.D1.C1

	pshufd		\$0b00011011,$ABEF0,$ABEF0
	pshufd		\$0b00011011,$CDGH0,$CDGH0
	pshufd		\$0b00011011,$ABEF1,$ABEF1
	pshufd		\$0b00011011,$CDGH1,$CDGH1
	jmp		.Loop_shaext

.align	32
.Loop_shaext:
	movdqu		0x00(@ptr[0]),@MSG0[0]
	 movdqu		0x00(@ptr[1]),@MSG1[0]
	movdqu		0x10(@ptr[0]),@MSG0[1]
	 movdqu		0x10(@ptr[1]),@MSG1[1]
	movdqu		0x20(@ptr[0]),@MSG0[2]
	pshufb		$TMPx,@MSG0[0]
	 movdqu		0x20(@ptr[1]),@MSG1[2]
	 pshufb		$TMPx,@MSG1[0]
	movdqu		0x30(@ptr[0]),@MSG0[3]
	lea		0x40(@ptr[0]),@ptr[0]
	 movdqu		0x30(@ptr[1]),@MSG1[3]
	 lea		0x40(@ptr[1]),@ptr[1]

	movdqa		0*16-0x80($Tbl),$Wi
	pshufb		$TMPx,@MSG0[1]
	paddd		@MSG0[0],$Wi
	pxor		$ABEF0,@MSG0[0]		# black magic
	movdqa		$Wi,$TMP0
	 movdqa		0*16-0x80($Tbl),$TMP1
	 pshufb		$TMPx,@MSG1[1]
	 paddd		@MSG1[0],$TMP1
	movdqa		$CDGH0,0x50(%rsp)	# offload
	sha256rnds2	$ABEF0,$CDGH0		# 0-3
	 pxor		$ABEF1,@MSG1[0]		# black magic
	 movdqa		$TMP1,$Wi
	 movdqa		$CDGH1,0x70(%rsp)
	 sha256rnds2	$ABEF1,$CDGH1		# 0-3
	pshufd		\$0x0e,$TMP0,$Wi
	pxor		$ABEF0,@MSG0[0]		# black magic
	movdqa		$ABEF0,0x40(%rsp)	# offload
	sha256rnds2	$CDGH0,$ABEF0
	 pshufd		\$0x0e,$TMP1,$Wi
	 pxor		$ABEF1,@MSG1[0]		# black magic
	 movdqa		$ABEF1,0x60(%rsp)
	movdqa		1*16-0x80($Tbl),$TMP0
	paddd		@MSG0[1],$TMP0
	pshufb		$TMPx,@MSG0[2]
	 sha256rnds2	$CDGH1,$ABEF1

	movdqa		$TMP0,$Wi
	 movdqa		1*16-0x80($Tbl),$TMP1
	 paddd		@MSG1[1],$TMP1
	sha256rnds2	$ABEF0,$CDGH0		# 4-7
	 movdqa		$TMP1,$Wi
	prefetcht0	127(@ptr[0])
	pshufb		$TMPx,@MSG0[3]
	 pshufb		$TMPx,@MSG1[2]
	 prefetcht0	127(@ptr[1])
	 sha256rnds2	$ABEF1,$CDGH1		# 4-7
	pshufd		\$0x0e,$TMP0,$Wi
	 pshufb		$TMPx,@MSG1[3]
	sha256msg1	@MSG0[1],@MSG0[0]
	sha256rnds2	$CDGH0,$ABEF0
	 pshufd		\$0x0e,$TMP1,$Wi
	movdqa		2*16-0x80($Tbl),$TMP0
	paddd		@MSG0[2],$TMP0
	 sha256rnds2	$CDGH1,$ABEF1

	movdqa		$TMP0,$Wi
	 movdqa		2*16-0x80($Tbl),$TMP1
	 paddd		@MSG1[2],$TMP1
	sha256rnds2	$ABEF0,$CDGH0		# 8-11
	 sha256msg1	@MSG1[1],@MSG1[0]
	 movdqa		$TMP1,$Wi
	movdqa		@MSG0[3],$TMPx
	 sha256rnds2	$ABEF1,$CDGH1		# 8-11
	pshufd		\$0x0e,$TMP0,$Wi
	palignr		\$4,@MSG0[2],$TMPx
	paddd		$TMPx,@MSG0[0]
	 movdqa		@MSG1[3],$TMPx
	 palignr	\$4,@MSG1[2],$TMPx
	sha256msg1	@MSG0[2],@MSG0[1]
	sha256rnds2	$CDGH0,$ABEF0
	 pshufd		\$0x0e,$TMP1,$Wi
	movdqa		3*16-0x80($Tbl),$TMP0
	paddd		@MSG0[3],$TMP0
	 sha256rnds2	$CDGH1,$ABEF1
	 sha256msg1	@MSG1[2],@MSG1[1]

	movdqa		$TMP0,$Wi
	 movdqa		3*16-0x80($Tbl),$TMP1
	 paddd		$TMPx,@MSG1[0]
	 paddd		@MSG1[3],$TMP1
	sha256msg2	@MSG0[3],@MSG0[0]
	sha256rnds2	$ABEF0,$CDGH0		# 12-15
	 movdqa		$TMP1,$Wi
	movdqa		@MSG0[0],$TMPx
	palignr		\$4,@MSG0[3],$TMPx
	 sha256rnds2	$ABEF1,$CDGH1		# 12-15
	 sha256msg2	@MSG1[3],@MSG1[0]
	pshufd		\$0x0e,$TMP0,$Wi
	paddd		$TMPx,@MSG0[1]
	 movdqa		@MSG1[0],$TMPx
	 palignr	\$4,@MSG1[3],$TMPx
	sha256msg1	@MSG0[3],@MSG0[2]
	sha256rnds2	$CDGH0,$ABEF0
	 pshufd		\$0x0e,$TMP1,$Wi
	movdqa		4*16-0x80($Tbl),$TMP0
	paddd		@MSG0[0],$TMP0
	 sha256rnds2	$CDGH1,$ABEF1
	 sha256msg1	@MSG1[3],@MSG1[2]
___
for($i=4;$i<16-3;$i++) {
$code.=<<___;
	movdqa		$TMP0,$Wi
	 movdqa		$i*16-0x80($Tbl),$TMP1
	 paddd		$TMPx,@MSG1[1]
	 paddd		@MSG1[0],$TMP1
	sha256msg2	@MSG0[0],@MSG0[1]
	sha256rnds2	$ABEF0,$CDGH0		# 16-19...
	 movdqa		$TMP1,$Wi
	movdqa		@MSG0[1],$TMPx
	palignr		\$4,@MSG0[0],$TMPx
	 sha256rnds2	$ABEF1,$CDGH1		# 16-19...
	 sha256msg2	@MSG1[0],@MSG1[1]
	pshufd		\$0x0e,$TMP0,$Wi
	paddd		$TMPx,@MSG0[2]
	 movdqa		@MSG1[1],$TMPx
	 palignr	\$4,@MSG1[0],$TMPx
	sha256msg1	@MSG0[0],@MSG0[3]
	sha256rnds2	$CDGH0,$ABEF0
	 pshufd		\$0x0e,$TMP1,$Wi
	movdqa		`($i+1)*16`-0x80($Tbl),$TMP0
	paddd		@MSG0[1],$TMP0
	 sha256rnds2	$CDGH1,$ABEF1
	 sha256msg1	@MSG1[0],@MSG1[3]
___
	push(@MSG0,shift(@MSG0));	push(@MSG1,shift(@MSG1));
}
$code.=<<___;
	movdqa		$TMP0,$Wi
	 movdqa		13*16-0x80($Tbl),$TMP1
	 paddd		$TMPx,@MSG1[1]
	 paddd		@MSG1[0],$TMP1
	sha256msg2	@MSG0[0],@MSG0[1]
	sha256rnds2	$ABEF0,$CDGH0		# 52-55
	 movdqa		$TMP1,$Wi
	movdqa		@MSG0[1],$TMPx
	palignr		\$4,@MSG0[0],$TMPx
	 sha256rnds2	$ABEF1,$CDGH1		# 52-55
	 sha256msg2	@MSG1[0],@MSG1[1]
	pshufd		\$0x0e,$TMP0,$Wi
	paddd		$TMPx,@MSG0[2]
	 movdqa		@MSG1[1],$TMPx
	 palignr	\$4,@MSG1[0],$TMPx
	nop
	sha256rnds2	$CDGH0,$ABEF0
	 pshufd		\$0x0e,$TMP1,$Wi
	movdqa		14*16-0x80($Tbl),$TMP0
	paddd		@MSG0[1],$TMP0
	 sha256rnds2	$CDGH1,$ABEF1

	movdqa		$TMP0,$Wi
	 movdqa		14*16-0x80($Tbl),$TMP1
	 paddd		$TMPx,@MSG1[2]
	 paddd		@MSG1[1],$TMP1
	sha256msg2	@MSG0[1],@MSG0[2]
	nop
	sha256rnds2	$ABEF0,$CDGH0		# 56-59
	 movdqa		$TMP1,$Wi
	  mov		\$1,%ecx
	  pxor		@MSG0[1],@MSG0[1]	# zero
	 sha256rnds2	$ABEF1,$CDGH1		# 56-59
	 sha256msg2	@MSG1[1],@MSG1[2]
	pshufd		\$0x0e,$TMP0,$Wi
	movdqa		15*16-0x80($Tbl),$TMP0
	paddd		@MSG0[2],$TMP0
	  movq		(%rbx),@MSG0[2]		# pull counters
	  nop
	sha256rnds2	$CDGH0,$ABEF0
	 pshufd		\$0x0e,$TMP1,$Wi
	 movdqa		15*16-0x80($Tbl),$TMP1
	 paddd		@MSG1[2],$TMP1
	 sha256rnds2	$CDGH1,$ABEF1

	movdqa		$TMP0,$Wi
	  cmp		4*0(%rbx),%ecx		# examine counters
	  cmovge	%rsp,@ptr[0]		# cancel input
	  cmp		4*1(%rbx),%ecx
	  cmovge	%rsp,@ptr[1]
	  pshufd	\$0x00,@MSG0[2],@MSG1[0]
	sha256rnds2	$ABEF0,$CDGH0		# 60-63
	 movdqa		$TMP1,$Wi
	  pshufd	\$0x55,@MSG0[2],@MSG1[1]
	  movdqa	@MSG0[2],@MSG1[2]
	 sha256rnds2	$ABEF1,$CDGH1		# 60-63
	pshufd		\$0x0e,$TMP0,$Wi
	  pcmpgtd	@MSG0[1],@MSG1[0]
	  pcmpgtd	@MSG0[1],@MSG1[1]
	sha256rnds2	$CDGH0,$ABEF0
	 pshufd		\$0x0e,$TMP1,$Wi
	  pcmpgtd	@MSG0[1],@MSG1[2]	# counter mask
	  movdqa	K256_shaext-0x10(%rip),$TMPx
	 sha256rnds2	$CDGH1,$ABEF1

	pand		@MSG1[0],$CDGH0
	 pand		@MSG1[1],$CDGH1
	pand		@MSG1[0],$ABEF0
	 pand		@MSG1[1],$ABEF1
	paddd		@MSG0[2],@MSG1[2]	# counters--

	paddd		0x50(%rsp),$CDGH0
	 paddd		0x70(%rsp),$CDGH1
	paddd		0x40(%rsp),$ABEF0
	 paddd		0x60(%rsp),$ABEF1

	movq		@MSG1[2],(%rbx)		# save counters
	dec		$num
	jnz		.Loop_shaext

	mov		`$REG_SZ*17+8`(%rsp),$num

	pshufd		\$0b00011011,$ABEF0,$ABEF0
	pshufd		\$0b00011011,$CDGH0,$CDGH0
	pshufd		\$0b00011011,$ABEF1,$ABEF1
	pshufd		\$0b00011011,$CDGH1,$CDGH1

	movdqa		$ABEF0,@MSG0[0]
	movdqa		$CDGH0,@MSG0[1]
	punpckldq	$ABEF1,$ABEF0			# B1.B0.A1.A0
	punpckhdq	$ABEF1,@MSG0[0]			# F1.F0.E1.E0
	punpckldq	$CDGH1,$CDGH0			# D1.D0.C1.C0
	punpckhdq	$CDGH1,@MSG0[1]			# H1.H0.G1.G0

	movq		$ABEF0,0x00-0x80($ctx)		# A1.A0
	psrldq		\$8,$ABEF0
	movq		@MSG0[0],0x80-0x80($ctx)	# E1.E0
	psrldq		\$8,@MSG0[0]
	movq		$ABEF0,0x20-0x80($ctx)		# B1.B0
	movq		@MSG0[0],0xa0-0x80($ctx)	# F1.F0

	movq		$CDGH0,0x40-0x80($ctx)		# C1.C0
	psrldq		\$8,$CDGH0
	movq		@MSG0[1],0xc0-0x80($ctx)	# G1.G0
	psrldq		\$8,@MSG0[1]
	movq		$CDGH0,0x60-0x80($ctx)		# D1.D0
	movq		@MSG0[1],0xe0-0x80($ctx)	# H1.H0

	lea	`$REG_SZ/2`($ctx),$ctx
	lea	`16*2`($inp),$inp
	dec	$num
	jnz	.Loop_grande_shaext

.Ldone_shaext:
	#mov	`$REG_SZ*17`(%rsp),%rax		# original %rsp
___
$code.=<<___ if ($win64);
	movaps	-0xb8(%rax),%xmm6
	movaps	-0xa8(%rax),%xmm7
	movaps	-0x98(%rax),%xmm8
	movaps	-0x88(%rax),%xmm9
	movaps	-0x78(%rax),%xmm10
	movaps	-0x68(%rax),%xmm11
	movaps	-0x58(%rax),%xmm12
	movaps	-0x48(%rax),%xmm13
	movaps	-0x38(%rax),%xmm14
	movaps	-0x28(%rax),%xmm15
___
$code.=<<___;
	mov	-16(%rax),%rbp
.cfi_restore	%rbp
	mov	-8(%rax),%rbx
.cfi_restore	%rbx
	lea	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_shaext:
	ret
.cfi_endproc
.size	sha256_multi_block_shaext,.-sha256_multi_block_shaext
___
						}}}
						if ($avx) {{{
sub ROUND_00_15_avx {
my ($i,$a,$b,$c,$d,$e,$f,$g,$h)=@_;

$code.=<<___ if ($i<15 && $REG_SZ==16);
	vmovd		`4*$i`(@ptr[0]),$Xi
	vmovd		`4*$i`(@ptr[1]),$t1
	vpinsrd		\$1,`4*$i`(@ptr[2]),$Xi,$Xi
	vpinsrd		\$1,`4*$i`(@ptr[3]),$t1,$t1
	vpunpckldq	$t1,$Xi,$Xi
	vpshufb		$Xn,$Xi,$Xi
___
$code.=<<___ if ($i==15 && $REG_SZ==16);
	vmovd		`4*$i`(@ptr[0]),$Xi
	 lea		`16*4`(@ptr[0]),@ptr[0]
	vmovd		`4*$i`(@ptr[1]),$t1
	 lea		`16*4`(@ptr[1]),@ptr[1]
	vpinsrd		\$1,`4*$i`(@ptr[2]),$Xi,$Xi
	 lea		`16*4`(@ptr[2]),@ptr[2]
	vpinsrd		\$1,`4*$i`(@ptr[3]),$t1,$t1
	 lea		`16*4`(@ptr[3]),@ptr[3]
	vpunpckldq	$t1,$Xi,$Xi
	vpshufb		$Xn,$Xi,$Xi
___
$code.=<<___ if ($i<15 && $REG_SZ==32);
	vmovd		`4*$i`(@ptr[0]),$Xi
	vmovd		`4*$i`(@ptr[4]),$t1
	vmovd		`4*$i`(@ptr[1]),$t2
	vmovd		`4*$i`(@ptr[5]),$t3
	vpinsrd		\$1,`4*$i`(@ptr[2]),$Xi,$Xi
	vpinsrd		\$1,`4*$i`(@ptr[6]),$t1,$t1
	vpinsrd		\$1,`4*$i`(@ptr[3]),$t2,$t2
	vpunpckldq	$t2,$Xi,$Xi
	vpinsrd		\$1,`4*$i`(@ptr[7]),$t3,$t3
	vpunpckldq	$t3,$t1,$t1
	vinserti128	$t1,$Xi,$Xi
	vpshufb		$Xn,$Xi,$Xi
___
$code.=<<___ if ($i==15 && $REG_SZ==32);
	vmovd		`4*$i`(@ptr[0]),$Xi
	 lea		`16*4`(@ptr[0]),@ptr[0]
	vmovd		`4*$i`(@ptr[4]),$t1
	 lea		`16*4`(@ptr[4]),@ptr[4]
	vmovd		`4*$i`(@ptr[1]),$t2
	 lea		`16*4`(@ptr[1]),@ptr[1]
	vmovd		`4*$i`(@ptr[5]),$t3
	 lea		`16*4`(@ptr[5]),@ptr[5]
	vpinsrd		\$1,`4*$i`(@ptr[2]),$Xi,$Xi
	 lea		`16*4`(@ptr[2]),@ptr[2]
	vpinsrd		\$1,`4*$i`(@ptr[6]),$t1,$t1
	 lea		`16*4`(@ptr[6]),@ptr[6]
	vpinsrd		\$1,`4*$i`(@ptr[3]),$t2,$t2
	 lea		`16*4`(@ptr[3]),@ptr[3]
	vpunpckldq	$t2,$Xi,$Xi
	vpinsrd		\$1,`4*$i`(@ptr[7]),$t3,$t3
	 lea		`16*4`(@ptr[7]),@ptr[7]
	vpunpckldq	$t3,$t1,$t1
	vinserti128	$t1,$Xi,$Xi
	vpshufb		$Xn,$Xi,$Xi
___
$code.=<<___;
	vpsrld	\$6,$e,$sigma
	vpslld	\$26,$e,$t3
	vmovdqu	$Xi,`&Xi_off($i)`
	 vpaddd	$h,$Xi,$Xi			# Xi+=h

	vpsrld	\$11,$e,$t2
	vpxor	$t3,$sigma,$sigma
	vpslld	\$21,$e,$t3
	 vpaddd	`32*($i%8)-128`($Tbl),$Xi,$Xi	# Xi+=K[round]
	vpxor	$t2,$sigma,$sigma

	vpsrld	\$25,$e,$t2
	vpxor	$t3,$sigma,$sigma
	 `"prefetcht0	63(@ptr[0])"		if ($i==15)`
	vpslld	\$7,$e,$t3
	 vpandn	$g,$e,$t1
	 vpand	$f,$e,$axb			# borrow $axb
	 `"prefetcht0	63(@ptr[1])"		if ($i==15)`
	vpxor	$t2,$sigma,$sigma

	vpsrld	\$2,$a,$h			# borrow $h
	vpxor	$t3,$sigma,$sigma		# Sigma1(e)
	 `"prefetcht0	63(@ptr[2])"		if ($i==15)`
	vpslld	\$30,$a,$t2
	 vpxor	$axb,$t1,$t1			# Ch(e,f,g)
	 vpxor	$a,$b,$axb			# a^b, b^c in next round
	 `"prefetcht0	63(@ptr[3])"		if ($i==15)`
	vpxor	$t2,$h,$h
	vpaddd	$sigma,$Xi,$Xi			# Xi+=Sigma1(e)

	vpsrld	\$13,$a,$t2
	 `"prefetcht0	63(@ptr[4])"		if ($i==15 && $REG_SZ==32)`
	vpslld	\$19,$a,$t3
	 vpaddd	$t1,$Xi,$Xi			# Xi+=Ch(e,f,g)
	 vpand	$axb,$bxc,$bxc
	 `"prefetcht0	63(@ptr[5])"		if ($i==15 && $REG_SZ==32)`
	vpxor	$t2,$h,$sigma

	vpsrld	\$22,$a,$t2
	vpxor	$t3,$sigma,$sigma
	 `"prefetcht0	63(@ptr[6])"		if ($i==15 && $REG_SZ==32)`
	vpslld	\$10,$a,$t3
	 vpxor	$bxc,$b,$h			# h=Maj(a,b,c)=Ch(a^b,c,b)
	 vpaddd	$Xi,$d,$d			# d+=Xi
	 `"prefetcht0	63(@ptr[7])"		if ($i==15 && $REG_SZ==32)`
	vpxor	$t2,$sigma,$sigma
	vpxor	$t3,$sigma,$sigma		# Sigma0(a)

	vpaddd	$Xi,$h,$h			# h+=Xi
	vpaddd	$sigma,$h,$h			# h+=Sigma0(a)
___
$code.=<<___ if (($i%8)==7);
	add	\$`32*8`,$Tbl
___
	($axb,$bxc)=($bxc,$axb);
}

sub ROUND_16_XX_avx {
my $i=shift;

$code.=<<___;
	vmovdqu	`&Xi_off($i+1)`,$Xn
	vpaddd	`&Xi_off($i+9)`,$Xi,$Xi		# Xi+=X[i+9]

	vpsrld	\$3,$Xn,$sigma
	vpsrld	\$7,$Xn,$t2
	vpslld	\$25,$Xn,$t3
	vpxor	$t2,$sigma,$sigma
	vpsrld	\$18,$Xn,$t2
	vpxor	$t3,$sigma,$sigma
	vpslld	\$14,$Xn,$t3
	vmovdqu	`&Xi_off($i+14)`,$t1
	vpsrld	\$10,$t1,$axb			# borrow $axb

	vpxor	$t2,$sigma,$sigma
	vpsrld	\$17,$t1,$t2
	vpxor	$t3,$sigma,$sigma		# sigma0(X[i+1])
	vpslld	\$15,$t1,$t3
	 vpaddd	$sigma,$Xi,$Xi			# Xi+=sigma0(e)
	vpxor	$t2,$axb,$sigma
	vpsrld	\$19,$t1,$t2
	vpxor	$t3,$sigma,$sigma
	vpslld	\$13,$t1,$t3
	vpxor	$t2,$sigma,$sigma
	vpxor	$t3,$sigma,$sigma		# sigma0(X[i+14])
	vpaddd	$sigma,$Xi,$Xi			# Xi+=sigma1(X[i+14])
___
	&ROUND_00_15_avx($i,@_);
	($Xi,$Xn)=($Xn,$Xi);
}

$code.=<<___;
.type	sha256_multi_block_avx,\@function,3
.align	32
sha256_multi_block_avx:
.cfi_startproc
_avx_shortcut:
___
$code.=<<___ if ($avx>1);
	shr	\$32,%rcx
	cmp	\$2,$num
	jb	.Lavx
	test	\$`1<<5`,%ecx
	jnz	_avx2_shortcut
	jmp	.Lavx
.align	32
.Lavx:
___
$code.=<<___;
	mov	%rsp,%rax
.cfi_def_cfa_register	%rax
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
___
$code.=<<___ if ($win64);
	lea	-0xa8(%rsp),%rsp
	movaps	%xmm6,(%rsp)
	movaps	%xmm7,0x10(%rsp)
	movaps	%xmm8,0x20(%rsp)
	movaps	%xmm9,0x30(%rsp)
	movaps	%xmm10,-0x78(%rax)
	movaps	%xmm11,-0x68(%rax)
	movaps	%xmm12,-0x58(%rax)
	movaps	%xmm13,-0x48(%rax)
	movaps	%xmm14,-0x38(%rax)
	movaps	%xmm15,-0x28(%rax)
___
$code.=<<___;
	sub	\$`$REG_SZ*18`, %rsp
	and	\$-256,%rsp
	mov	%rax,`$REG_SZ*17`(%rsp)		# original %rsp
.cfi_cfa_expression	%rsp+`$REG_SZ*17`,deref,+8
.Lbody_avx:
	lea	K256+128(%rip),$Tbl
	lea	`$REG_SZ*16`(%rsp),%rbx
	lea	0x80($ctx),$ctx			# size optimization

.Loop_grande_avx:
	mov	$num,`$REG_SZ*17+8`(%rsp)	# original $num
	xor	$num,$num
___
for($i=0;$i<4;$i++) {
    $code.=<<___;
	mov	`16*$i+0`($inp),@ptr[$i]	# input pointer
	mov	`16*$i+8`($inp),%ecx		# number of blocks
	cmp	$num,%ecx
	cmovg	%ecx,$num			# find maximum
	test	%ecx,%ecx
	mov	%ecx,`4*$i`(%rbx)		# initialize counters
	cmovle	$Tbl,@ptr[$i]			# cancel input
___
}
$code.=<<___;
	test	$num,$num
	jz	.Ldone_avx

	vmovdqu	0x00-0x80($ctx),$A		# load context
	 lea	128(%rsp),%rax
	vmovdqu	0x20-0x80($ctx),$B
	vmovdqu	0x40-0x80($ctx),$C
	vmovdqu	0x60-0x80($ctx),$D
	vmovdqu	0x80-0x80($ctx),$E
	vmovdqu	0xa0-0x80($ctx),$F
	vmovdqu	0xc0-0x80($ctx),$G
	vmovdqu	0xe0-0x80($ctx),$H
	vmovdqu	.Lpbswap(%rip),$Xn
	jmp	.Loop_avx

.align	32
.Loop_avx:
	vpxor	$B,$C,$bxc			# magic seed
___
for($i=0;$i<16;$i++)	{ &ROUND_00_15_avx($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	vmovdqu	`&Xi_off($i)`,$Xi
	mov	\$3,%ecx
	jmp	.Loop_16_xx_avx
.align	32
.Loop_16_xx_avx:
___
for(;$i<32;$i++)	{ &ROUND_16_XX_avx($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	dec	%ecx
	jnz	.Loop_16_xx_avx

	mov	\$1,%ecx
	lea	K256+128(%rip),$Tbl
___
for($i=0;$i<4;$i++) {
    $code.=<<___;
	cmp	`4*$i`(%rbx),%ecx		# examine counters
	cmovge	$Tbl,@ptr[$i]			# cancel input
___
}
$code.=<<___;
	vmovdqa	(%rbx),$sigma			# pull counters
	vpxor	$t1,$t1,$t1
	vmovdqa	$sigma,$Xn
	vpcmpgtd $t1,$Xn,$Xn			# mask value
	vpaddd	$Xn,$sigma,$sigma		# counters--

	vmovdqu	0x00-0x80($ctx),$t1
	vpand	$Xn,$A,$A
	vmovdqu	0x20-0x80($ctx),$t2
	vpand	$Xn,$B,$B
	vmovdqu	0x40-0x80($ctx),$t3
	vpand	$Xn,$C,$C
	vmovdqu	0x60-0x80($ctx),$Xi
	vpand	$Xn,$D,$D
	vpaddd	$t1,$A,$A
	vmovdqu	0x80-0x80($ctx),$t1
	vpand	$Xn,$E,$E
	vpaddd	$t2,$B,$B
	vmovdqu	0xa0-0x80($ctx),$t2
	vpand	$Xn,$F,$F
	vpaddd	$t3,$C,$C
	vmovdqu	0xc0-0x80($ctx),$t3
	vpand	$Xn,$G,$G
	vpaddd	$Xi,$D,$D
	vmovdqu	0xe0-0x80($ctx),$Xi
	vpand	$Xn,$H,$H
	vpaddd	$t1,$E,$E
	vpaddd	$t2,$F,$F
	vmovdqu	$A,0x00-0x80($ctx)
	vpaddd	$t3,$G,$G
	vmovdqu	$B,0x20-0x80($ctx)
	vpaddd	$Xi,$H,$H
	vmovdqu	$C,0x40-0x80($ctx)
	vmovdqu	$D,0x60-0x80($ctx)
	vmovdqu	$E,0x80-0x80($ctx)
	vmovdqu	$F,0xa0-0x80($ctx)
	vmovdqu	$G,0xc0-0x80($ctx)
	vmovdqu	$H,0xe0-0x80($ctx)

	vmovdqu	$sigma,(%rbx)			# save counters
	vmovdqu	.Lpbswap(%rip),$Xn
	dec	$num
	jnz	.Loop_avx

	mov	`$REG_SZ*17+8`(%rsp),$num
	lea	$REG_SZ($ctx),$ctx
	lea	`16*$REG_SZ/4`($inp),$inp
	dec	$num
	jnz	.Loop_grande_avx

.Ldone_avx:
	mov	`$REG_SZ*17`(%rsp),%rax		# original %rsp
.cfi_def_cfa	%rax,8
	vzeroupper
___
$code.=<<___ if ($win64);
	movaps	-0xb8(%rax),%xmm6
	movaps	-0xa8(%rax),%xmm7
	movaps	-0x98(%rax),%xmm8
	movaps	-0x88(%rax),%xmm9
	movaps	-0x78(%rax),%xmm10
	movaps	-0x68(%rax),%xmm11
	movaps	-0x58(%rax),%xmm12
	movaps	-0x48(%rax),%xmm13
	movaps	-0x38(%rax),%xmm14
	movaps	-0x28(%rax),%xmm15
___
$code.=<<___;
	mov	-16(%rax),%rbp
.cfi_restore	%rbp
	mov	-8(%rax),%rbx
.cfi_restore	%rbx
	lea	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_avx:
	ret
.cfi_endproc
.size	sha256_multi_block_avx,.-sha256_multi_block_avx
___
						if ($avx>1) {
$code =~ s/\`([^\`]*)\`/eval $1/gem;

$REG_SZ=32;
@ptr=map("%r$_",(12..15,8..11));

@V=($A,$B,$C,$D,$E,$F,$G,$H)=map("%ymm$_",(8..15));
($t1,$t2,$t3,$axb,$bxc,$Xi,$Xn,$sigma)=map("%ymm$_",(0..7));

$code.=<<___;
.type	sha256_multi_block_avx2,\@function,3
.align	32
sha256_multi_block_avx2:
.cfi_startproc
_avx2_shortcut:
	mov	%rsp,%rax
.cfi_def_cfa_register	%rax
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r12
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
___
$code.=<<___ if ($win64);
	lea	-0xa8(%rsp),%rsp
	movaps	%xmm6,(%rsp)
	movaps	%xmm7,0x10(%rsp)
	movaps	%xmm8,0x20(%rsp)
	movaps	%xmm9,0x30(%rsp)
	movaps	%xmm10,0x40(%rsp)
	movaps	%xmm11,0x50(%rsp)
	movaps	%xmm12,-0x78(%rax)
	movaps	%xmm13,-0x68(%rax)
	movaps	%xmm14,-0x58(%rax)
	movaps	%xmm15,-0x48(%rax)
___
$code.=<<___;
	sub	\$`$REG_SZ*18`, %rsp
	and	\$-256,%rsp
	mov	%rax,`$REG_SZ*17`(%rsp)		# original %rsp
.cfi_cfa_expression	%rsp+`$REG_SZ*17`,deref,+8
.Lbody_avx2:
	lea	K256+128(%rip),$Tbl
	lea	0x80($ctx),$ctx			# size optimization

.Loop_grande_avx2:
	mov	$num,`$REG_SZ*17+8`(%rsp)	# original $num
	xor	$num,$num
	lea	`$REG_SZ*16`(%rsp),%rbx
___
for($i=0;$i<8;$i++) {
    $code.=<<___;
	mov	`16*$i+0`($inp),@ptr[$i]	# input pointer
	mov	`16*$i+8`($inp),%ecx		# number of blocks
	cmp	$num,%ecx
	cmovg	%ecx,$num			# find maximum
	test	%ecx,%ecx
	mov	%ecx,`4*$i`(%rbx)		# initialize counters
	cmovle	$Tbl,@ptr[$i]			# cancel input
___
}
$code.=<<___;
	vmovdqu	0x00-0x80($ctx),$A		# load context
	 lea	128(%rsp),%rax
	vmovdqu	0x20-0x80($ctx),$B
	 lea	256+128(%rsp),%rbx
	vmovdqu	0x40-0x80($ctx),$C
	vmovdqu	0x60-0x80($ctx),$D
	vmovdqu	0x80-0x80($ctx),$E
	vmovdqu	0xa0-0x80($ctx),$F
	vmovdqu	0xc0-0x80($ctx),$G
	vmovdqu	0xe0-0x80($ctx),$H
	vmovdqu	.Lpbswap(%rip),$Xn
	jmp	.Loop_avx2

.align	32
.Loop_avx2:
	vpxor	$B,$C,$bxc			# magic seed
___
for($i=0;$i<16;$i++)	{ &ROUND_00_15_avx($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	vmovdqu	`&Xi_off($i)`,$Xi
	mov	\$3,%ecx
	jmp	.Loop_16_xx_avx2
.align	32
.Loop_16_xx_avx2:
___
for(;$i<32;$i++)	{ &ROUND_16_XX_avx($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	dec	%ecx
	jnz	.Loop_16_xx_avx2

	mov	\$1,%ecx
	lea	`$REG_SZ*16`(%rsp),%rbx
	lea	K256+128(%rip),$Tbl
___
for($i=0;$i<8;$i++) {
    $code.=<<___;
	cmp	`4*$i`(%rbx),%ecx		# examine counters
	cmovge	$Tbl,@ptr[$i]			# cancel input
___
}
$code.=<<___;
	vmovdqa	(%rbx),$sigma			# pull counters
	vpxor	$t1,$t1,$t1
	vmovdqa	$sigma,$Xn
	vpcmpgtd $t1,$Xn,$Xn			# mask value
	vpaddd	$Xn,$sigma,$sigma		# counters--

	vmovdqu	0x00-0x80($ctx),$t1
	vpand	$Xn,$A,$A
	vmovdqu	0x20-0x80($ctx),$t2
	vpand	$Xn,$B,$B
	vmovdqu	0x40-0x80($ctx),$t3
	vpand	$Xn,$C,$C
	vmovdqu	0x60-0x80($ctx),$Xi
	vpand	$Xn,$D,$D
	vpaddd	$t1,$A,$A
	vmovdqu	0x80-0x80($ctx),$t1
	vpand	$Xn,$E,$E
	vpaddd	$t2,$B,$B
	vmovdqu	0xa0-0x80($ctx),$t2
	vpand	$Xn,$F,$F
	vpaddd	$t3,$C,$C
	vmovdqu	0xc0-0x80($ctx),$t3
	vpand	$Xn,$G,$G
	vpaddd	$Xi,$D,$D
	vmovdqu	0xe0-0x80($ctx),$Xi
	vpand	$Xn,$H,$H
	vpaddd	$t1,$E,$E
	vpaddd	$t2,$F,$F
	vmovdqu	$A,0x00-0x80($ctx)
	vpaddd	$t3,$G,$G
	vmovdqu	$B,0x20-0x80($ctx)
	vpaddd	$Xi,$H,$H
	vmovdqu	$C,0x40-0x80($ctx)
	vmovdqu	$D,0x60-0x80($ctx)
	vmovdqu	$E,0x80-0x80($ctx)
	vmovdqu	$F,0xa0-0x80($ctx)
	vmovdqu	$G,0xc0-0x80($ctx)
	vmovdqu	$H,0xe0-0x80($ctx)

	vmovdqu	$sigma,(%rbx)			# save counters
	lea	256+128(%rsp),%rbx
	vmovdqu	.Lpbswap(%rip),$Xn
	dec	$num
	jnz	.Loop_avx2

	#mov	`$REG_SZ*17+8`(%rsp),$num
	#lea	$REG_SZ($ctx),$ctx
	#lea	`16*$REG_SZ/4`($inp),$inp
	#dec	$num
	#jnz	.Loop_grande_avx2

.Ldone_avx2:
	mov	`$REG_SZ*17`(%rsp),%rax		# original %rsp
.cfi_def_cfa	%rax,8
	vzeroupper
___
$code.=<<___ if ($win64);
	movaps	-0xd8(%rax),%xmm6
	movaps	-0xc8(%rax),%xmm7
	movaps	-0xb8(%rax),%xmm8
	movaps	-0xa8(%rax),%xmm9
	movaps	-0x98(%rax),%xmm10
	movaps	-0x88(%rax),%xmm11
	movaps	-0x78(%rax),%xmm12
	movaps	-0x68(%rax),%xmm13
	movaps	-0x58(%rax),%xmm14
	movaps	-0x48(%rax),%xmm15
___
$code.=<<___;
	mov	-48(%rax),%r15
.cfi_restore	%r15
	mov	-40(%rax),%r14
.cfi_restore	%r14
	mov	-32(%rax),%r13
.cfi_restore	%r13
	mov	-24(%rax),%r12
.cfi_restore	%r12
	mov	-16(%rax),%rbp
.cfi_restore	%rbp
	mov	-8(%rax),%rbx
.cfi_restore	%rbx
	lea	(%rax),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_avx2:
	ret
.cfi_endproc
.size	sha256_multi_block_avx2,.-sha256_multi_block_avx2
___
					}	}}}
$code.=<<___;
.align	256
K256:
___
sub TABLE {
    foreach (@_) {
	$code.=<<___;
	.long	$_,$_,$_,$_
	.long	$_,$_,$_,$_
___
    }
}
&TABLE(	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,
	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,
	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,
	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,
	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,
	0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,
	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,
	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 );
$code.=<<___;
.Lpbswap:
	.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f	# pbswap
	.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f	# pbswap
K256_shaext:
	.long	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	.long	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	.long	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	.long	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	.long	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	.long	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	.long	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	.long	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	.long	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	.long	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	.long	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	.long	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	.long	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	.long	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	.long	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	.long	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
	.asciz	"SHA256 multi-block transform for x86_64, CRYPTOGAMS by <appro\@openssl.org>"
___

if ($win64) {
# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
$rec="%rcx";
$frame="%rdx";
$context="%r8";
$disp="%r9";

$code.=<<___;
.extern	__imp_RtlVirtualUnwind
.type	se_handler,\@abi-omnipotent
.align	16
se_handler:
	push	%rsi
	push	%rdi
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15
	pushfq
	sub	\$64,%rsp

	mov	120($context),%rax	# pull context->Rax
	mov	248($context),%rbx	# pull context->Rip

	mov	8($disp),%rsi		# disp->ImageBase
	mov	56($disp),%r11		# disp->HandlerData

	mov	0(%r11),%r10d		# HandlerData[0]
	lea	(%rsi,%r10),%r10	# end of prologue label
	cmp	%r10,%rbx		# context->Rip<.Lbody
	jb	.Lin_prologue

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=.Lepilogue
	jae	.Lin_prologue

	mov	`16*17`(%rax),%rax	# pull saved stack pointer

	mov	-8(%rax),%rbx
	mov	-16(%rax),%rbp
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp

	lea	-24-10*16(%rax),%rsi
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$20,%ecx
	.long	0xa548f3fc		# cld; rep movsq

.Lin_prologue:
	mov	8(%rax),%rdi
	mov	16(%rax),%rsi
	mov	%rax,152($context)	# restore context->Rsp
	mov	%rsi,168($context)	# restore context->Rsi
	mov	%rdi,176($context)	# restore context->Rdi

	mov	40($disp),%rdi		# disp->ContextRecord
	mov	$context,%rsi		# context
	mov	\$154,%ecx		# sizeof(CONTEXT)
	.long	0xa548f3fc		# cld; rep movsq

	mov	$disp,%rsi
	xor	%rcx,%rcx		# arg1, UNW_FLAG_NHANDLER
	mov	8(%rsi),%rdx		# arg2, disp->ImageBase
	mov	0(%rsi),%r8		# arg3, disp->ControlPc
	mov	16(%rsi),%r9		# arg4, disp->FunctionEntry
	mov	40(%rsi),%r10		# disp->ContextRecord
	lea	56(%rsi),%r11		# &disp->HandlerData
	lea	24(%rsi),%r12		# &disp->EstablisherFrame
	mov	%r10,32(%rsp)		# arg5
	mov	%r11,40(%rsp)		# arg6
	mov	%r12,48(%rsp)		# arg7
	mov	%rcx,56(%rsp)		# arg8, (NULL)
	call	*__imp_RtlVirtualUnwind(%rip)

	mov	\$1,%eax		# ExceptionContinueSearch
	add	\$64,%rsp
	popfq
	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
	pop	%rbp
	pop	%rbx
	pop	%rdi
	pop	%rsi
	ret
.size	se_handler,.-se_handler
___
$code.=<<___ if ($avx>1);
.type	avx2_handler,\@abi-omnipotent
.align	16
avx2_handler:
	push	%rsi
	push	%rdi
	push	%rbx
	push	%rbp
	push	%r12
	push	%r13
	push	%r14
	push	%r15
	pushfq
	sub	\$64,%rsp

	mov	120($context),%rax	# pull context->Rax
	mov	248($context),%rbx	# pull context->Rip

	mov	8($disp),%rsi		# disp->ImageBase
	mov	56($disp),%r11		# disp->HandlerData

	mov	0(%r11),%r10d		# HandlerData[0]
	lea	(%rsi,%r10),%r10	# end of prologue label
	cmp	%r10,%rbx		# context->Rip<body label
	jb	.Lin_prologue

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lin_prologue

	mov	`32*17`($context),%rax	# pull saved stack pointer

	mov	-8(%rax),%rbx
	mov	-16(%rax),%rbp
	mov	-24(%rax),%r12
	mov	-32(%rax),%r13
	mov	-40(%rax),%r14
	mov	-48(%rax),%r15
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r12,216($context)	# restore context->R12
	mov	%r13,224($context)	# restore context->R13
	mov	%r14,232($context)	# restore context->R14
	mov	%r15,240($context)	# restore context->R15

	lea	-56-10*16(%rax),%rsi
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$20,%ecx
	.long	0xa548f3fc		# cld; rep movsq

	jmp	.Lin_prologue
.size	avx2_handler,.-avx2_handler
___
$code.=<<___;
.section	.pdata
.align	4
	.rva	.LSEH_begin_sha256_multi_block
	.rva	.LSEH_end_sha256_multi_block
	.rva	.LSEH_info_sha256_multi_block
	.rva	.LSEH_begin_sha256_multi_block_shaext
	.rva	.LSEH_end_sha256_multi_block_shaext
	.rva	.LSEH_info_sha256_multi_block_shaext
___
$code.=<<___ if ($avx);
	.rva	.LSEH_begin_sha256_multi_block_avx
	.rva	.LSEH_end_sha256_multi_block_avx
	.rva	.LSEH_info_sha256_multi_block_avx
___
$code.=<<___ if ($avx>1);
	.rva	.LSEH_begin_sha256_multi_block_avx2
	.rva	.LSEH_end_sha256_multi_block_avx2
	.rva	.LSEH_info_sha256_multi_block_avx2
___
$code.=<<___;
.section	.xdata
.align	8
.LSEH_info_sha256_multi_block:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lbody,.Lepilogue			# HandlerData[]
.LSEH_info_sha256_multi_block_shaext:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lbody_shaext,.Lepilogue_shaext		# HandlerData[]
___
$code.=<<___ if ($avx);
.LSEH_info_sha256_multi_block_avx:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lbody_avx,.Lepilogue_avx		# HandlerData[]
___
$code.=<<___ if ($avx>1);
.LSEH_info_sha256_multi_block_avx2:
	.byte	9,0,0,0
	.rva	avx2_handler
	.rva	.Lbody_avx2,.Lepilogue_avx2		# HandlerData[]
___
}
####################################################################

sub rex {
  local *opcode=shift;
  my ($dst,$src)=@_;
  my $rex=0;

    $rex|=0x04			if ($dst>=8);
    $rex|=0x01			if ($src>=8);
    unshift @opcode,$rex|0x40	if ($rex);
}

sub sha256op38 {
    my $instr = shift;
    my %opcodelet = (
		"sha256rnds2" => 0xcb,
  		"sha256msg1"  => 0xcc,
		"sha256msg2"  => 0xcd	);

    if (defined($opcodelet{$instr}) && @_[0] =~ /%xmm([0-9]+),\s*%xmm([0-9]+)/) {
      my @opcode=(0x0f,0x38);
	rex(\@opcode,$2,$1);
	push @opcode,$opcodelet{$instr};
	push @opcode,0xc0|($1&7)|(($2&7)<<3);		# ModR/M
	return ".byte\t".join(',',@opcode);
    } else {
	return $instr."\t".@_[0];
    }
}

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval($1)/ge;

	s/\b(sha256[^\s]*)\s+(.*)/sha256op38($1,$2)/geo		or

	s/\b(vmov[dq])\b(.+)%ymm([0-9]+)/$1$2%xmm$3/go		or
	s/\b(vmovdqu)\b(.+)%x%ymm([0-9]+)/$1$2%xmm$3/go		or
	s/\b(vpinsr[qd])\b(.+)%ymm([0-9]+),%ymm([0-9]+)/$1$2%xmm$3,%xmm$4/go	or
	s/\b(vpextr[qd])\b(.+)%ymm([0-9]+)/$1$2%xmm$3/go	or
	s/\b(vinserti128)\b(\s+)%ymm/$1$2\$1,%xmm/go		or
	s/\b(vpbroadcast[qd]\s+)%ymm([0-9]+)/$1%xmm$2/go;

	print $_,"\n";
}

close STDOUT;
