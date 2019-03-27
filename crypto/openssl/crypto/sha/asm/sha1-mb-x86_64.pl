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

# Multi-buffer SHA1 procedure processes n buffers in parallel by
# placing buffer data to designated lane of SIMD register. n is
# naturally limited to 4 on pre-AVX2 processors and to 8 on
# AVX2-capable processors such as Haswell.
#
#		this	+aesni(i)	sha1	aesni-sha1	gain(iv)
# -------------------------------------------------------------------
# Westmere(ii)	10.7/n	+1.28=3.96(n=4)	5.30	6.66		+68%
# Atom(ii)	18.1/n	+3.93=8.46(n=4)	9.37	12.8		+51%
# Sandy Bridge	(8.16	+5.15=13.3)/n	4.99	5.98		+80%
# Ivy Bridge	(8.08	+5.14=13.2)/n	4.60	5.54		+68%
# Haswell(iii)	(8.96	+5.00=14.0)/n	3.57	4.55		+160%
# Skylake	(8.70	+5.00=13.7)/n	3.64	4.20		+145%
# Bulldozer	(9.76	+5.76=15.5)/n	5.95	6.37		+64%
#
# (i)	multi-block CBC encrypt with 128-bit key;
# (ii)	(HASH+AES)/n does not apply to Westmere for n>3 and Atom,
#	because of lower AES-NI instruction throughput;
# (iii)	"this" is for n=8, when we gather twice as much data, result
#	for n=4 is 8.00+4.44=12.4;
# (iv)	presented improvement coefficients are asymptotic limits and
#	in real-life application are somewhat lower, e.g. for 2KB
#	fragments they range from 30% to 100% (on Haswell);

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

# void sha1_multi_block (
#     struct {	unsigned int A[8];
#		unsigned int B[8];
#		unsigned int C[8];
#		unsigned int D[8];
#		unsigned int E[8];	} *ctx,
#     struct {	void *ptr; int blocks;	} inp[8],
#     int num);		/* 1 or 2 */
#
$ctx="%rdi";	# 1st arg
$inp="%rsi";	# 2nd arg
$num="%edx";
@ptr=map("%r$_",(8..11));
$Tbl="%rbp";

@V=($A,$B,$C,$D,$E)=map("%xmm$_",(0..4));
($t0,$t1,$t2,$t3,$tx)=map("%xmm$_",(5..9));
@Xi=map("%xmm$_",(10..14));
$K="%xmm15";

if (1) {
    # Atom-specific optimization aiming to eliminate pshufb with high
    # registers [and thus get rid of 48 cycles accumulated penalty]
    @Xi=map("%xmm$_",(0..4));
    ($tx,$t0,$t1,$t2,$t3)=map("%xmm$_",(5..9));
    @V=($A,$B,$C,$D,$E)=map("%xmm$_",(10..14));
}

$REG_SZ=16;

sub Xi_off {
my $off = shift;

    $off %= 16; $off *= $REG_SZ;
    $off<256 ? "$off-128(%rax)" : "$off-256-128(%rbx)";
}

sub BODY_00_19 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;
my $k=$i+2;

# Loads are performed 2+3/4 iterations in advance. 3/4 means that out
# of 4 words you would expect to be loaded per given iteration one is
# spilled to next iteration. In other words indices in four input
# streams are distributed as following:
#
# $i==0:	0,0,0,0,1,1,1,1,2,2,2,
# $i==1:	2,3,3,3,
# $i==2:	3,4,4,4,
# ...
# $i==13:	14,15,15,15,
# $i==14:	15
#
# Then at $i==15 Xupdate is applied one iteration in advance...
$code.=<<___ if ($i==0);
	movd		(@ptr[0]),@Xi[0]
	 lea		`16*4`(@ptr[0]),@ptr[0]
	movd		(@ptr[1]),@Xi[2]	# borrow @Xi[2]
	 lea		`16*4`(@ptr[1]),@ptr[1]
	movd		(@ptr[2]),@Xi[3]	# borrow @Xi[3]
	 lea		`16*4`(@ptr[2]),@ptr[2]
	movd		(@ptr[3]),@Xi[4]	# borrow @Xi[4]
	 lea		`16*4`(@ptr[3]),@ptr[3]
	punpckldq	@Xi[3],@Xi[0]
	 movd		`4*$j-16*4`(@ptr[0]),@Xi[1]
	punpckldq	@Xi[4],@Xi[2]
	 movd		`4*$j-16*4`(@ptr[1]),$t3
	punpckldq	@Xi[2],@Xi[0]
	 movd		`4*$j-16*4`(@ptr[2]),$t2
	pshufb		$tx,@Xi[0]
___
$code.=<<___ if ($i<14);			# just load input
	 movd		`4*$j-16*4`(@ptr[3]),$t1
	 punpckldq	$t2,@Xi[1]
	movdqa	$a,$t2
	paddd	$K,$e				# e+=K_00_19
	 punpckldq	$t1,$t3
	movdqa	$b,$t1
	movdqa	$b,$t0
	pslld	\$5,$t2
	pandn	$d,$t1
	pand	$c,$t0
	 punpckldq	$t3,@Xi[1]
	movdqa	$a,$t3

	movdqa	@Xi[0],`&Xi_off($i)`
	paddd	@Xi[0],$e			# e+=X[i]
	 movd		`4*$k-16*4`(@ptr[0]),@Xi[2]
	psrld	\$27,$t3
	pxor	$t1,$t0				# Ch(b,c,d)
	movdqa	$b,$t1

	por	$t3,$t2				# rol(a,5)
	 movd		`4*$k-16*4`(@ptr[1]),$t3
	pslld	\$30,$t1
	paddd	$t0,$e				# e+=Ch(b,c,d)

	psrld	\$2,$b
	paddd	$t2,$e				# e+=rol(a,5)
	 pshufb	$tx,@Xi[1]
	 movd		`4*$k-16*4`(@ptr[2]),$t2
	por	$t1,$b				# b=rol(b,30)
___
$code.=<<___ if ($i==14);			# just load input
	 movd		`4*$j-16*4`(@ptr[3]),$t1
	 punpckldq	$t2,@Xi[1]
	movdqa	$a,$t2
	paddd	$K,$e				# e+=K_00_19
	 punpckldq	$t1,$t3
	movdqa	$b,$t1
	movdqa	$b,$t0
	pslld	\$5,$t2
	 prefetcht0	63(@ptr[0])
	pandn	$d,$t1
	pand	$c,$t0
	 punpckldq	$t3,@Xi[1]
	movdqa	$a,$t3

	movdqa	@Xi[0],`&Xi_off($i)`
	paddd	@Xi[0],$e			# e+=X[i]
	psrld	\$27,$t3
	pxor	$t1,$t0				# Ch(b,c,d)
	movdqa	$b,$t1
	 prefetcht0	63(@ptr[1])

	por	$t3,$t2				# rol(a,5)
	pslld	\$30,$t1
	paddd	$t0,$e				# e+=Ch(b,c,d)
	 prefetcht0	63(@ptr[2])

	psrld	\$2,$b
	paddd	$t2,$e				# e+=rol(a,5)
	 pshufb	$tx,@Xi[1]
	 prefetcht0	63(@ptr[3])
	por	$t1,$b				# b=rol(b,30)
___
$code.=<<___ if ($i>=13 && $i<15);
	movdqa	`&Xi_off($j+2)`,@Xi[3]		# preload "X[2]"
___
$code.=<<___ if ($i>=15);			# apply Xupdate
	pxor	@Xi[-2],@Xi[1]			# "X[13]"
	movdqa	`&Xi_off($j+2)`,@Xi[3]		# "X[2]"

	movdqa	$a,$t2
	 pxor	`&Xi_off($j+8)`,@Xi[1]
	paddd	$K,$e				# e+=K_00_19
	movdqa	$b,$t1
	pslld	\$5,$t2
	 pxor	@Xi[3],@Xi[1]
	movdqa	$b,$t0
	pandn	$d,$t1
	 movdqa	@Xi[1],$tx
	pand	$c,$t0
	movdqa	$a,$t3
	 psrld	\$31,$tx
	 paddd	@Xi[1],@Xi[1]

	movdqa	@Xi[0],`&Xi_off($i)`
	paddd	@Xi[0],$e			# e+=X[i]
	psrld	\$27,$t3
	pxor	$t1,$t0				# Ch(b,c,d)

	movdqa	$b,$t1
	por	$t3,$t2				# rol(a,5)
	pslld	\$30,$t1
	paddd	$t0,$e				# e+=Ch(b,c,d)

	psrld	\$2,$b
	paddd	$t2,$e				# e+=rol(a,5)
	 por	$tx,@Xi[1]			# rol	\$1,@Xi[1]
	por	$t1,$b				# b=rol(b,30)
___
push(@Xi,shift(@Xi));
}

sub BODY_20_39 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;

$code.=<<___ if ($i<79);
	pxor	@Xi[-2],@Xi[1]			# "X[13]"
	movdqa	`&Xi_off($j+2)`,@Xi[3]		# "X[2]"

	movdqa	$a,$t2
	movdqa	$d,$t0
	 pxor	`&Xi_off($j+8)`,@Xi[1]
	paddd	$K,$e				# e+=K_20_39
	pslld	\$5,$t2
	pxor	$b,$t0

	movdqa	$a,$t3
___
$code.=<<___ if ($i<72);
	movdqa	@Xi[0],`&Xi_off($i)`
___
$code.=<<___ if ($i<79);
	paddd	@Xi[0],$e			# e+=X[i]
	 pxor	@Xi[3],@Xi[1]
	psrld	\$27,$t3
	pxor	$c,$t0				# Parity(b,c,d)
	movdqa	$b,$t1

	pslld	\$30,$t1
	 movdqa	@Xi[1],$tx
	por	$t3,$t2				# rol(a,5)
	 psrld	\$31,$tx
	paddd	$t0,$e				# e+=Parity(b,c,d)
	 paddd	@Xi[1],@Xi[1]

	psrld	\$2,$b
	paddd	$t2,$e				# e+=rol(a,5)
	 por	$tx,@Xi[1]			# rol(@Xi[1],1)
	por	$t1,$b				# b=rol(b,30)
___
$code.=<<___ if ($i==79);
	movdqa	$a,$t2
	paddd	$K,$e				# e+=K_20_39
	movdqa	$d,$t0
	pslld	\$5,$t2
	pxor	$b,$t0

	movdqa	$a,$t3
	paddd	@Xi[0],$e			# e+=X[i]
	psrld	\$27,$t3
	movdqa	$b,$t1
	pxor	$c,$t0				# Parity(b,c,d)

	pslld	\$30,$t1
	por	$t3,$t2				# rol(a,5)
	paddd	$t0,$e				# e+=Parity(b,c,d)

	psrld	\$2,$b
	paddd	$t2,$e				# e+=rol(a,5)
	por	$t1,$b				# b=rol(b,30)
___
push(@Xi,shift(@Xi));
}

sub BODY_40_59 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;

$code.=<<___;
	pxor	@Xi[-2],@Xi[1]			# "X[13]"
	movdqa	`&Xi_off($j+2)`,@Xi[3]		# "X[2]"

	movdqa	$a,$t2
	movdqa	$d,$t1
	 pxor	`&Xi_off($j+8)`,@Xi[1]
	pxor	@Xi[3],@Xi[1]
	paddd	$K,$e				# e+=K_40_59
	pslld	\$5,$t2
	movdqa	$a,$t3
	pand	$c,$t1

	movdqa	$d,$t0
	 movdqa	@Xi[1],$tx
	psrld	\$27,$t3
	paddd	$t1,$e
	pxor	$c,$t0

	movdqa	@Xi[0],`&Xi_off($i)`
	paddd	@Xi[0],$e			# e+=X[i]
	por	$t3,$t2				# rol(a,5)
	 psrld	\$31,$tx
	pand	$b,$t0
	movdqa	$b,$t1

	pslld	\$30,$t1
	 paddd	@Xi[1],@Xi[1]
	paddd	$t0,$e				# e+=Maj(b,d,c)

	psrld	\$2,$b
	paddd	$t2,$e				# e+=rol(a,5)
	 por	$tx,@Xi[1]			# rol(@X[1],1)
	por	$t1,$b				# b=rol(b,30)
___
push(@Xi,shift(@Xi));
}

$code.=<<___;
.text

.extern	OPENSSL_ia32cap_P

.globl	sha1_multi_block
.type	sha1_multi_block,\@function,3
.align	32
sha1_multi_block:
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
.cfi_push	%rbx
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
	and	\$-256,%rsp
	mov	%rax,`$REG_SZ*17`(%rsp)		# original %rsp
.cfi_cfa_expression	%rsp+`$REG_SZ*17`,deref,+8
.Lbody:
	lea	K_XX_XX(%rip),$Tbl
	lea	`$REG_SZ*16`(%rsp),%rbx

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

	movdqu	0x00($ctx),$A			# load context
	 lea	128(%rsp),%rax
	movdqu	0x20($ctx),$B
	movdqu	0x40($ctx),$C
	movdqu	0x60($ctx),$D
	movdqu	0x80($ctx),$E
	movdqa	0x60($Tbl),$tx			# pbswap_mask
	movdqa	-0x20($Tbl),$K			# K_00_19
	jmp	.Loop

.align	32
.Loop:
___
for($i=0;$i<20;$i++)	{ &BODY_00_19($i,@V); unshift(@V,pop(@V)); }
$code.="	movdqa	0x00($Tbl),$K\n";	# K_20_39
for(;$i<40;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
$code.="	movdqa	0x20($Tbl),$K\n";	# K_40_59
for(;$i<60;$i++)	{ &BODY_40_59($i,@V); unshift(@V,pop(@V)); }
$code.="	movdqa	0x40($Tbl),$K\n";	# K_60_79
for(;$i<80;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	movdqa	(%rbx),@Xi[0]			# pull counters
	mov	\$1,%ecx
	cmp	4*0(%rbx),%ecx			# examine counters
	pxor	$t2,$t2
	cmovge	$Tbl,@ptr[0]			# cancel input
	cmp	4*1(%rbx),%ecx
	movdqa	@Xi[0],@Xi[1]
	cmovge	$Tbl,@ptr[1]
	cmp	4*2(%rbx),%ecx
	pcmpgtd	$t2,@Xi[1]			# mask value
	cmovge	$Tbl,@ptr[2]
	cmp	4*3(%rbx),%ecx
	paddd	@Xi[1],@Xi[0]			# counters--
	cmovge	$Tbl,@ptr[3]

	movdqu	0x00($ctx),$t0
	pand	@Xi[1],$A
	movdqu	0x20($ctx),$t1
	pand	@Xi[1],$B
	paddd	$t0,$A
	movdqu	0x40($ctx),$t2
	pand	@Xi[1],$C
	paddd	$t1,$B
	movdqu	0x60($ctx),$t3
	pand	@Xi[1],$D
	paddd	$t2,$C
	movdqu	0x80($ctx),$tx
	pand	@Xi[1],$E
	movdqu	$A,0x00($ctx)
	paddd	$t3,$D
	movdqu	$B,0x20($ctx)
	paddd	$tx,$E
	movdqu	$C,0x40($ctx)
	movdqu	$D,0x60($ctx)
	movdqu	$E,0x80($ctx)

	movdqa	@Xi[0],(%rbx)			# save counters
	movdqa	0x60($Tbl),$tx			# pbswap_mask
	movdqa	-0x20($Tbl),$K			# K_00_19
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
.size	sha1_multi_block,.-sha1_multi_block
___
						{{{
my ($ABCD0,$E0,$E0_,$BSWAP,$ABCD1,$E1,$E1_)=map("%xmm$_",(0..3,8..10));
my @MSG0=map("%xmm$_",(4..7));
my @MSG1=map("%xmm$_",(11..14));

$code.=<<___;
.type	sha1_multi_block_shaext,\@function,3
.align	32
sha1_multi_block_shaext:
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
	lea	0x40($ctx),$ctx			# size optimization
	mov	%rax,`$REG_SZ*17`(%rsp)		# original %rsp
.Lbody_shaext:
	lea	`$REG_SZ*16`(%rsp),%rbx
	movdqa	K_XX_XX+0x80(%rip),$BSWAP	# byte-n-word swap

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

	movq		0x00-0x40($ctx),$ABCD0	# a1.a0
	movq		0x20-0x40($ctx),@MSG0[0]# b1.b0
	movq		0x40-0x40($ctx),@MSG0[1]# c1.c0
	movq		0x60-0x40($ctx),@MSG0[2]# d1.d0
	movq		0x80-0x40($ctx),@MSG0[3]# e1.e0

	punpckldq	@MSG0[0],$ABCD0		# b1.a1.b0.a0
	punpckldq	@MSG0[2],@MSG0[1]	# d1.c1.d0.c0

	movdqa		$ABCD0,$ABCD1
	punpcklqdq	@MSG0[1],$ABCD0		# d0.c0.b0.a0
	punpckhqdq	@MSG0[1],$ABCD1		# d1.c1.b1.a1

	pshufd		\$0b00111111,@MSG0[3],$E0
	pshufd		\$0b01111111,@MSG0[3],$E1
	pshufd		\$0b00011011,$ABCD0,$ABCD0
	pshufd		\$0b00011011,$ABCD1,$ABCD1
	jmp		.Loop_shaext

.align	32
.Loop_shaext:
	movdqu		0x00(@ptr[0]),@MSG0[0]
	 movdqu		0x00(@ptr[1]),@MSG1[0]
	movdqu		0x10(@ptr[0]),@MSG0[1]
	 movdqu		0x10(@ptr[1]),@MSG1[1]
	movdqu		0x20(@ptr[0]),@MSG0[2]
	pshufb		$BSWAP,@MSG0[0]
	 movdqu		0x20(@ptr[1]),@MSG1[2]
	 pshufb		$BSWAP,@MSG1[0]
	movdqu		0x30(@ptr[0]),@MSG0[3]
	lea		0x40(@ptr[0]),@ptr[0]
	pshufb		$BSWAP,@MSG0[1]
	 movdqu		0x30(@ptr[1]),@MSG1[3]
	 lea		0x40(@ptr[1]),@ptr[1]
	 pshufb		$BSWAP,@MSG1[1]

	movdqa		$E0,0x50(%rsp)		# offload
	paddd		@MSG0[0],$E0
	 movdqa		$E1,0x70(%rsp)
	 paddd		@MSG1[0],$E1
	movdqa		$ABCD0,0x40(%rsp)	# offload
	movdqa		$ABCD0,$E0_
	 movdqa		$ABCD1,0x60(%rsp)
	 movdqa		$ABCD1,$E1_
	sha1rnds4	\$0,$E0,$ABCD0		# 0-3
	sha1nexte	@MSG0[1],$E0_
	 sha1rnds4	\$0,$E1,$ABCD1		# 0-3
	 sha1nexte	@MSG1[1],$E1_
	pshufb		$BSWAP,@MSG0[2]
	prefetcht0	127(@ptr[0])
	sha1msg1	@MSG0[1],@MSG0[0]
	 pshufb		$BSWAP,@MSG1[2]
	 prefetcht0	127(@ptr[1])
	 sha1msg1	@MSG1[1],@MSG1[0]

	pshufb		$BSWAP,@MSG0[3]
	movdqa		$ABCD0,$E0
	 pshufb		$BSWAP,@MSG1[3]
	 movdqa		$ABCD1,$E1
	sha1rnds4	\$0,$E0_,$ABCD0		# 4-7
	sha1nexte	@MSG0[2],$E0
	 sha1rnds4	\$0,$E1_,$ABCD1		# 4-7
	 sha1nexte	@MSG1[2],$E1
	pxor		@MSG0[2],@MSG0[0]
	sha1msg1	@MSG0[2],@MSG0[1]
	 pxor		@MSG1[2],@MSG1[0]
	 sha1msg1	@MSG1[2],@MSG1[1]
___
for($i=2;$i<20-4;$i++) {
$code.=<<___;
	movdqa		$ABCD0,$E0_
	 movdqa		$ABCD1,$E1_
	sha1rnds4	\$`int($i/5)`,$E0,$ABCD0	# 8-11
	sha1nexte	@MSG0[3],$E0_
	 sha1rnds4	\$`int($i/5)`,$E1,$ABCD1	# 8-11
	 sha1nexte	@MSG1[3],$E1_
	sha1msg2	@MSG0[3],@MSG0[0]
	 sha1msg2	@MSG1[3],@MSG1[0]
	pxor		@MSG0[3],@MSG0[1]
	sha1msg1	@MSG0[3],@MSG0[2]
	 pxor		@MSG1[3],@MSG1[1]
	 sha1msg1	@MSG1[3],@MSG1[2]
___
	($E0,$E0_)=($E0_,$E0);		($E1,$E1_)=($E1_,$E1);
	push(@MSG0,shift(@MSG0));	push(@MSG1,shift(@MSG1));
}
$code.=<<___;
	movdqa		$ABCD0,$E0_
	 movdqa		$ABCD1,$E1_
	sha1rnds4	\$3,$E0,$ABCD0		# 64-67
	sha1nexte	@MSG0[3],$E0_
	 sha1rnds4	\$3,$E1,$ABCD1		# 64-67
	 sha1nexte	@MSG1[3],$E1_
	sha1msg2	@MSG0[3],@MSG0[0]
	 sha1msg2	@MSG1[3],@MSG1[0]
	pxor		@MSG0[3],@MSG0[1]
	 pxor		@MSG1[3],@MSG1[1]

	mov		\$1,%ecx
	pxor		@MSG0[2],@MSG0[2]	# zero
	cmp		4*0(%rbx),%ecx		# examine counters
	cmovge		%rsp,@ptr[0]		# cancel input

	movdqa		$ABCD0,$E0
	 movdqa		$ABCD1,$E1
	sha1rnds4	\$3,$E0_,$ABCD0		# 68-71
	sha1nexte	@MSG0[0],$E0
	 sha1rnds4	\$3,$E1_,$ABCD1		# 68-71
	 sha1nexte	@MSG1[0],$E1
	sha1msg2	@MSG0[0],@MSG0[1]
	 sha1msg2	@MSG1[0],@MSG1[1]

	cmp		4*1(%rbx),%ecx
	cmovge		%rsp,@ptr[1]
	movq		(%rbx),@MSG0[0]		# pull counters

	movdqa		$ABCD0,$E0_
	 movdqa		$ABCD1,$E1_
	sha1rnds4	\$3,$E0,$ABCD0		# 72-75
	sha1nexte	@MSG0[1],$E0_
	 sha1rnds4	\$3,$E1,$ABCD1		# 72-75
	 sha1nexte	@MSG1[1],$E1_

	pshufd		\$0x00,@MSG0[0],@MSG1[2]
	pshufd		\$0x55,@MSG0[0],@MSG1[3]
	movdqa		@MSG0[0],@MSG0[1]
	pcmpgtd		@MSG0[2],@MSG1[2]
	pcmpgtd		@MSG0[2],@MSG1[3]

	movdqa		$ABCD0,$E0
	 movdqa		$ABCD1,$E1
	sha1rnds4	\$3,$E0_,$ABCD0		# 76-79
	sha1nexte	$MSG0[2],$E0
	 sha1rnds4	\$3,$E1_,$ABCD1		# 76-79
	 sha1nexte	$MSG0[2],$E1

	pcmpgtd		@MSG0[2],@MSG0[1]	# counter mask
	pand		@MSG1[2],$ABCD0
	pand		@MSG1[2],$E0
	 pand		@MSG1[3],$ABCD1
	 pand		@MSG1[3],$E1
	paddd		@MSG0[1],@MSG0[0]	# counters--

	paddd		0x40(%rsp),$ABCD0
	paddd		0x50(%rsp),$E0
	 paddd		0x60(%rsp),$ABCD1
	 paddd		0x70(%rsp),$E1

	movq		@MSG0[0],(%rbx)		# save counters
	dec		$num
	jnz		.Loop_shaext

	mov		`$REG_SZ*17+8`(%rsp),$num

	pshufd		\$0b00011011,$ABCD0,$ABCD0
	pshufd		\$0b00011011,$ABCD1,$ABCD1

	movdqa		$ABCD0,@MSG0[0]
	punpckldq	$ABCD1,$ABCD0		# b1.b0.a1.a0
	punpckhdq	$ABCD1,@MSG0[0]		# d1.d0.c1.c0
	punpckhdq	$E1,$E0			# e1.e0.xx.xx
	movq		$ABCD0,0x00-0x40($ctx)	# a1.a0
	psrldq		\$8,$ABCD0
	movq		@MSG0[0],0x40-0x40($ctx)# c1.c0
	psrldq		\$8,@MSG0[0]
	movq		$ABCD0,0x20-0x40($ctx)	# b1.b0
	psrldq		\$8,$E0
	movq		@MSG0[0],0x60-0x40($ctx)# d1.d0
	movq		$E0,0x80-0x40($ctx)	# e1.e0

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
.size	sha1_multi_block_shaext,.-sha1_multi_block_shaext
___
						}}}

						if ($avx) {{{
sub BODY_00_19_avx {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;
my $k=$i+2;
my $vpack = $REG_SZ==16 ? "vpunpckldq" : "vinserti128";
my $ptr_n = $REG_SZ==16 ? @ptr[1] : @ptr[4];

$code.=<<___ if ($i==0 && $REG_SZ==16);
	vmovd		(@ptr[0]),@Xi[0]
	 lea		`16*4`(@ptr[0]),@ptr[0]
	vmovd		(@ptr[1]),@Xi[2]	# borrow Xi[2]
	 lea		`16*4`(@ptr[1]),@ptr[1]
	vpinsrd		\$1,(@ptr[2]),@Xi[0],@Xi[0]
	 lea		`16*4`(@ptr[2]),@ptr[2]
	vpinsrd		\$1,(@ptr[3]),@Xi[2],@Xi[2]
	 lea		`16*4`(@ptr[3]),@ptr[3]
	 vmovd		`4*$j-16*4`(@ptr[0]),@Xi[1]
	vpunpckldq	@Xi[2],@Xi[0],@Xi[0]
	 vmovd		`4*$j-16*4`($ptr_n),$t3
	vpshufb		$tx,@Xi[0],@Xi[0]
___
$code.=<<___ if ($i<15 && $REG_SZ==16);		# just load input
	 vpinsrd	\$1,`4*$j-16*4`(@ptr[2]),@Xi[1],@Xi[1]
	 vpinsrd	\$1,`4*$j-16*4`(@ptr[3]),$t3,$t3
___
$code.=<<___ if ($i==0 && $REG_SZ==32);
	vmovd		(@ptr[0]),@Xi[0]
	 lea		`16*4`(@ptr[0]),@ptr[0]
	vmovd		(@ptr[4]),@Xi[2]	# borrow Xi[2]
	 lea		`16*4`(@ptr[4]),@ptr[4]
	vmovd		(@ptr[1]),$t2
	 lea		`16*4`(@ptr[1]),@ptr[1]
	vmovd		(@ptr[5]),$t1
	 lea		`16*4`(@ptr[5]),@ptr[5]
	vpinsrd		\$1,(@ptr[2]),@Xi[0],@Xi[0]
	 lea		`16*4`(@ptr[2]),@ptr[2]
	vpinsrd		\$1,(@ptr[6]),@Xi[2],@Xi[2]
	 lea		`16*4`(@ptr[6]),@ptr[6]
	vpinsrd		\$1,(@ptr[3]),$t2,$t2
	 lea		`16*4`(@ptr[3]),@ptr[3]
	vpunpckldq	$t2,@Xi[0],@Xi[0]
	vpinsrd		\$1,(@ptr[7]),$t1,$t1
	 lea		`16*4`(@ptr[7]),@ptr[7]
	vpunpckldq	$t1,@Xi[2],@Xi[2]
	 vmovd		`4*$j-16*4`(@ptr[0]),@Xi[1]
	vinserti128	@Xi[2],@Xi[0],@Xi[0]
	 vmovd		`4*$j-16*4`($ptr_n),$t3
	vpshufb		$tx,@Xi[0],@Xi[0]
___
$code.=<<___ if ($i<15 && $REG_SZ==32);		# just load input
	 vmovd		`4*$j-16*4`(@ptr[1]),$t2
	 vmovd		`4*$j-16*4`(@ptr[5]),$t1
	 vpinsrd	\$1,`4*$j-16*4`(@ptr[2]),@Xi[1],@Xi[1]
	 vpinsrd	\$1,`4*$j-16*4`(@ptr[6]),$t3,$t3
	 vpinsrd	\$1,`4*$j-16*4`(@ptr[3]),$t2,$t2
	 vpunpckldq	$t2,@Xi[1],@Xi[1]
	 vpinsrd	\$1,`4*$j-16*4`(@ptr[7]),$t1,$t1
	 vpunpckldq	$t1,$t3,$t3
___
$code.=<<___ if ($i<14);
	vpaddd	$K,$e,$e			# e+=K_00_19
	vpslld	\$5,$a,$t2
	vpandn	$d,$b,$t1
	vpand	$c,$b,$t0

	vmovdqa	@Xi[0],`&Xi_off($i)`
	vpaddd	@Xi[0],$e,$e			# e+=X[i]
	 $vpack		$t3,@Xi[1],@Xi[1]
	vpsrld	\$27,$a,$t3
	vpxor	$t1,$t0,$t0			# Ch(b,c,d)
	 vmovd		`4*$k-16*4`(@ptr[0]),@Xi[2]

	vpslld	\$30,$b,$t1
	vpor	$t3,$t2,$t2			# rol(a,5)
	 vmovd		`4*$k-16*4`($ptr_n),$t3
	vpaddd	$t0,$e,$e			# e+=Ch(b,c,d)

	vpsrld	\$2,$b,$b
	vpaddd	$t2,$e,$e			# e+=rol(a,5)
	 vpshufb	$tx,@Xi[1],@Xi[1]
	vpor	$t1,$b,$b			# b=rol(b,30)
___
$code.=<<___ if ($i==14);
	vpaddd	$K,$e,$e			# e+=K_00_19
	 prefetcht0	63(@ptr[0])
	vpslld	\$5,$a,$t2
	vpandn	$d,$b,$t1
	vpand	$c,$b,$t0

	vmovdqa	@Xi[0],`&Xi_off($i)`
	vpaddd	@Xi[0],$e,$e			# e+=X[i]
	 $vpack		$t3,@Xi[1],@Xi[1]
	vpsrld	\$27,$a,$t3
	 prefetcht0	63(@ptr[1])
	vpxor	$t1,$t0,$t0			# Ch(b,c,d)

	vpslld	\$30,$b,$t1
	vpor	$t3,$t2,$t2			# rol(a,5)
	 prefetcht0	63(@ptr[2])
	vpaddd	$t0,$e,$e			# e+=Ch(b,c,d)

	vpsrld	\$2,$b,$b
	vpaddd	$t2,$e,$e			# e+=rol(a,5)
	 prefetcht0	63(@ptr[3])
	 vpshufb	$tx,@Xi[1],@Xi[1]
	vpor	$t1,$b,$b			# b=rol(b,30)
___
$code.=<<___ if ($i>=13 && $i<15);
	vmovdqa	`&Xi_off($j+2)`,@Xi[3]		# preload "X[2]"
___
$code.=<<___ if ($i>=15);			# apply Xupdate
	vpxor	@Xi[-2],@Xi[1],@Xi[1]		# "X[13]"
	vmovdqa	`&Xi_off($j+2)`,@Xi[3]		# "X[2]"

	vpaddd	$K,$e,$e			# e+=K_00_19
	vpslld	\$5,$a,$t2
	vpandn	$d,$b,$t1
	 `"prefetcht0	63(@ptr[4])"		if ($i==15 && $REG_SZ==32)`
	vpand	$c,$b,$t0

	vmovdqa	@Xi[0],`&Xi_off($i)`
	vpaddd	@Xi[0],$e,$e			# e+=X[i]
	 vpxor	`&Xi_off($j+8)`,@Xi[1],@Xi[1]
	vpsrld	\$27,$a,$t3
	vpxor	$t1,$t0,$t0			# Ch(b,c,d)
	 vpxor	@Xi[3],@Xi[1],@Xi[1]
	 `"prefetcht0	63(@ptr[5])"		if ($i==15 && $REG_SZ==32)`

	vpslld	\$30,$b,$t1
	vpor	$t3,$t2,$t2			# rol(a,5)
	vpaddd	$t0,$e,$e			# e+=Ch(b,c,d)
	 `"prefetcht0	63(@ptr[6])"		if ($i==15 && $REG_SZ==32)`
	 vpsrld	\$31,@Xi[1],$tx
	 vpaddd	@Xi[1],@Xi[1],@Xi[1]

	vpsrld	\$2,$b,$b
	 `"prefetcht0	63(@ptr[7])"		if ($i==15 && $REG_SZ==32)`
	vpaddd	$t2,$e,$e			# e+=rol(a,5)
	 vpor	$tx,@Xi[1],@Xi[1]		# rol	\$1,@Xi[1]
	vpor	$t1,$b,$b			# b=rol(b,30)
___
push(@Xi,shift(@Xi));
}

sub BODY_20_39_avx {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;

$code.=<<___ if ($i<79);
	vpxor	@Xi[-2],@Xi[1],@Xi[1]		# "X[13]"
	vmovdqa	`&Xi_off($j+2)`,@Xi[3]		# "X[2]"

	vpslld	\$5,$a,$t2
	vpaddd	$K,$e,$e			# e+=K_20_39
	vpxor	$b,$d,$t0
___
$code.=<<___ if ($i<72);
	vmovdqa	@Xi[0],`&Xi_off($i)`
___
$code.=<<___ if ($i<79);
	vpaddd	@Xi[0],$e,$e			# e+=X[i]
	 vpxor	`&Xi_off($j+8)`,@Xi[1],@Xi[1]
	vpsrld	\$27,$a,$t3
	vpxor	$c,$t0,$t0			# Parity(b,c,d)
	 vpxor	@Xi[3],@Xi[1],@Xi[1]

	vpslld	\$30,$b,$t1
	vpor	$t3,$t2,$t2			# rol(a,5)
	vpaddd	$t0,$e,$e			# e+=Parity(b,c,d)
	 vpsrld	\$31,@Xi[1],$tx
	 vpaddd	@Xi[1],@Xi[1],@Xi[1]

	vpsrld	\$2,$b,$b
	vpaddd	$t2,$e,$e			# e+=rol(a,5)
	 vpor	$tx,@Xi[1],@Xi[1]		# rol(@Xi[1],1)
	vpor	$t1,$b,$b			# b=rol(b,30)
___
$code.=<<___ if ($i==79);
	vpslld	\$5,$a,$t2
	vpaddd	$K,$e,$e			# e+=K_20_39
	vpxor	$b,$d,$t0

	vpsrld	\$27,$a,$t3
	vpaddd	@Xi[0],$e,$e			# e+=X[i]
	vpxor	$c,$t0,$t0			# Parity(b,c,d)

	vpslld	\$30,$b,$t1
	vpor	$t3,$t2,$t2			# rol(a,5)
	vpaddd	$t0,$e,$e			# e+=Parity(b,c,d)

	vpsrld	\$2,$b,$b
	vpaddd	$t2,$e,$e			# e+=rol(a,5)
	vpor	$t1,$b,$b			# b=rol(b,30)
___
push(@Xi,shift(@Xi));
}

sub BODY_40_59_avx {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;

$code.=<<___;
	vpxor	@Xi[-2],@Xi[1],@Xi[1]		# "X[13]"
	vmovdqa	`&Xi_off($j+2)`,@Xi[3]		# "X[2]"

	vpaddd	$K,$e,$e			# e+=K_40_59
	vpslld	\$5,$a,$t2
	vpand	$c,$d,$t1
	 vpxor	`&Xi_off($j+8)`,@Xi[1],@Xi[1]

	vpaddd	$t1,$e,$e
	vpsrld	\$27,$a,$t3
	vpxor	$c,$d,$t0
	 vpxor	@Xi[3],@Xi[1],@Xi[1]

	vmovdqu	@Xi[0],`&Xi_off($i)`
	vpaddd	@Xi[0],$e,$e			# e+=X[i]
	vpor	$t3,$t2,$t2			# rol(a,5)
	 vpsrld	\$31,@Xi[1],$tx
	vpand	$b,$t0,$t0
	 vpaddd	@Xi[1],@Xi[1],@Xi[1]

	vpslld	\$30,$b,$t1
	vpaddd	$t0,$e,$e			# e+=Maj(b,d,c)

	vpsrld	\$2,$b,$b
	vpaddd	$t2,$e,$e			# e+=rol(a,5)
	 vpor	$tx,@Xi[1],@Xi[1]		# rol(@X[1],1)
	vpor	$t1,$b,$b			# b=rol(b,30)
___
push(@Xi,shift(@Xi));
}

$code.=<<___;
.type	sha1_multi_block_avx,\@function,3
.align	32
sha1_multi_block_avx:
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
	lea	K_XX_XX(%rip),$Tbl
	lea	`$REG_SZ*16`(%rsp),%rbx

	vzeroupper
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

	vmovdqu	0x00($ctx),$A			# load context
	 lea	128(%rsp),%rax
	vmovdqu	0x20($ctx),$B
	vmovdqu	0x40($ctx),$C
	vmovdqu	0x60($ctx),$D
	vmovdqu	0x80($ctx),$E
	vmovdqu	0x60($Tbl),$tx			# pbswap_mask
	jmp	.Loop_avx

.align	32
.Loop_avx:
___
$code.="	vmovdqa	-0x20($Tbl),$K\n";	# K_00_19
for($i=0;$i<20;$i++)	{ &BODY_00_19_avx($i,@V); unshift(@V,pop(@V)); }
$code.="	vmovdqa	0x00($Tbl),$K\n";	# K_20_39
for(;$i<40;$i++)	{ &BODY_20_39_avx($i,@V); unshift(@V,pop(@V)); }
$code.="	vmovdqa	0x20($Tbl),$K\n";	# K_40_59
for(;$i<60;$i++)	{ &BODY_40_59_avx($i,@V); unshift(@V,pop(@V)); }
$code.="	vmovdqa	0x40($Tbl),$K\n";	# K_60_79
for(;$i<80;$i++)	{ &BODY_20_39_avx($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	mov	\$1,%ecx
___
for($i=0;$i<4;$i++) {
    $code.=<<___;
	cmp	`4*$i`(%rbx),%ecx		# examine counters
	cmovge	$Tbl,@ptr[$i]			# cancel input
___
}
$code.=<<___;
	vmovdqu	(%rbx),$t0			# pull counters
	vpxor	$t2,$t2,$t2
	vmovdqa	$t0,$t1
	vpcmpgtd $t2,$t1,$t1			# mask value
	vpaddd	$t1,$t0,$t0			# counters--

	vpand	$t1,$A,$A
	vpand	$t1,$B,$B
	vpaddd	0x00($ctx),$A,$A
	vpand	$t1,$C,$C
	vpaddd	0x20($ctx),$B,$B
	vpand	$t1,$D,$D
	vpaddd	0x40($ctx),$C,$C
	vpand	$t1,$E,$E
	vpaddd	0x60($ctx),$D,$D
	vpaddd	0x80($ctx),$E,$E
	vmovdqu	$A,0x00($ctx)
	vmovdqu	$B,0x20($ctx)
	vmovdqu	$C,0x40($ctx)
	vmovdqu	$D,0x60($ctx)
	vmovdqu	$E,0x80($ctx)

	vmovdqu	$t0,(%rbx)			# save counters
	vmovdqu	0x60($Tbl),$tx			# pbswap_mask
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
.size	sha1_multi_block_avx,.-sha1_multi_block_avx
___

						if ($avx>1) {
$code =~ s/\`([^\`]*)\`/eval $1/gem;

$REG_SZ=32;

@ptr=map("%r$_",(12..15,8..11));

@V=($A,$B,$C,$D,$E)=map("%ymm$_",(0..4));
($t0,$t1,$t2,$t3,$tx)=map("%ymm$_",(5..9));
@Xi=map("%ymm$_",(10..14));
$K="%ymm15";

$code.=<<___;
.type	sha1_multi_block_avx2,\@function,3
.align	32
sha1_multi_block_avx2:
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
	lea	K_XX_XX(%rip),$Tbl
	shr	\$1,$num

	vzeroupper
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
	vmovdqu	0x00($ctx),$A			# load context
	 lea	128(%rsp),%rax
	vmovdqu	0x20($ctx),$B
	 lea	256+128(%rsp),%rbx
	vmovdqu	0x40($ctx),$C
	vmovdqu	0x60($ctx),$D
	vmovdqu	0x80($ctx),$E
	vmovdqu	0x60($Tbl),$tx			# pbswap_mask
	jmp	.Loop_avx2

.align	32
.Loop_avx2:
___
$code.="	vmovdqa	-0x20($Tbl),$K\n";	# K_00_19
for($i=0;$i<20;$i++)	{ &BODY_00_19_avx($i,@V); unshift(@V,pop(@V)); }
$code.="	vmovdqa	0x00($Tbl),$K\n";	# K_20_39
for(;$i<40;$i++)	{ &BODY_20_39_avx($i,@V); unshift(@V,pop(@V)); }
$code.="	vmovdqa	0x20($Tbl),$K\n";	# K_40_59
for(;$i<60;$i++)	{ &BODY_40_59_avx($i,@V); unshift(@V,pop(@V)); }
$code.="	vmovdqa	0x40($Tbl),$K\n";	# K_60_79
for(;$i<80;$i++)	{ &BODY_20_39_avx($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	mov	\$1,%ecx
	lea	`$REG_SZ*16`(%rsp),%rbx
___
for($i=0;$i<8;$i++) {
    $code.=<<___;
	cmp	`4*$i`(%rbx),%ecx		# examine counters
	cmovge	$Tbl,@ptr[$i]			# cancel input
___
}
$code.=<<___;
	vmovdqu	(%rbx),$t0		# pull counters
	vpxor	$t2,$t2,$t2
	vmovdqa	$t0,$t1
	vpcmpgtd $t2,$t1,$t1			# mask value
	vpaddd	$t1,$t0,$t0			# counters--

	vpand	$t1,$A,$A
	vpand	$t1,$B,$B
	vpaddd	0x00($ctx),$A,$A
	vpand	$t1,$C,$C
	vpaddd	0x20($ctx),$B,$B
	vpand	$t1,$D,$D
	vpaddd	0x40($ctx),$C,$C
	vpand	$t1,$E,$E
	vpaddd	0x60($ctx),$D,$D
	vpaddd	0x80($ctx),$E,$E
	vmovdqu	$A,0x00($ctx)
	vmovdqu	$B,0x20($ctx)
	vmovdqu	$C,0x40($ctx)
	vmovdqu	$D,0x60($ctx)
	vmovdqu	$E,0x80($ctx)

	vmovdqu	$t0,(%rbx)			# save counters
	lea	256+128(%rsp),%rbx
	vmovdqu	0x60($Tbl),$tx			# pbswap_mask
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
.size	sha1_multi_block_avx2,.-sha1_multi_block_avx2
___
						}	}}}
$code.=<<___;

.align	256
	.long	0x5a827999,0x5a827999,0x5a827999,0x5a827999	# K_00_19
	.long	0x5a827999,0x5a827999,0x5a827999,0x5a827999	# K_00_19
K_XX_XX:
	.long	0x6ed9eba1,0x6ed9eba1,0x6ed9eba1,0x6ed9eba1	# K_20_39
	.long	0x6ed9eba1,0x6ed9eba1,0x6ed9eba1,0x6ed9eba1	# K_20_39
	.long	0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc	# K_40_59
	.long	0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc	# K_40_59
	.long	0xca62c1d6,0xca62c1d6,0xca62c1d6,0xca62c1d6	# K_60_79
	.long	0xca62c1d6,0xca62c1d6,0xca62c1d6,0xca62c1d6	# K_60_79
	.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f	# pbswap
	.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f	# pbswap
	.byte	0xf,0xe,0xd,0xc,0xb,0xa,0x9,0x8,0x7,0x6,0x5,0x4,0x3,0x2,0x1,0x0
	.asciz	"SHA1 multi-block transform for x86_64, CRYPTOGAMS by <appro\@openssl.org>"
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
	.rva	.LSEH_begin_sha1_multi_block
	.rva	.LSEH_end_sha1_multi_block
	.rva	.LSEH_info_sha1_multi_block
	.rva	.LSEH_begin_sha1_multi_block_shaext
	.rva	.LSEH_end_sha1_multi_block_shaext
	.rva	.LSEH_info_sha1_multi_block_shaext
___
$code.=<<___ if ($avx);
	.rva	.LSEH_begin_sha1_multi_block_avx
	.rva	.LSEH_end_sha1_multi_block_avx
	.rva	.LSEH_info_sha1_multi_block_avx
___
$code.=<<___ if ($avx>1);
	.rva	.LSEH_begin_sha1_multi_block_avx2
	.rva	.LSEH_end_sha1_multi_block_avx2
	.rva	.LSEH_info_sha1_multi_block_avx2
___
$code.=<<___;
.section	.xdata
.align	8
.LSEH_info_sha1_multi_block:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lbody,.Lepilogue			# HandlerData[]
.LSEH_info_sha1_multi_block_shaext:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lbody_shaext,.Lepilogue_shaext	# HandlerData[]
___
$code.=<<___ if ($avx);
.LSEH_info_sha1_multi_block_avx:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lbody_avx,.Lepilogue_avx		# HandlerData[]
___
$code.=<<___ if ($avx>1);
.LSEH_info_sha1_multi_block_avx2:
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

sub sha1rnds4 {
    if (@_[0] =~ /\$([x0-9a-f]+),\s*%xmm([0-9]+),\s*%xmm([0-9]+)/) {
      my @opcode=(0x0f,0x3a,0xcc);
	rex(\@opcode,$3,$2);
	push @opcode,0xc0|($2&7)|(($3&7)<<3);		# ModR/M
	my $c=$1;
	push @opcode,$c=~/^0/?oct($c):$c;
	return ".byte\t".join(',',@opcode);
    } else {
	return "sha1rnds4\t".@_[0];
    }
}

sub sha1op38 {
    my $instr = shift;
    my %opcodelet = (
		"sha1nexte" => 0xc8,
  		"sha1msg1"  => 0xc9,
		"sha1msg2"  => 0xca	);

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

	s/\b(sha1rnds4)\s+(.*)/sha1rnds4($2)/geo		or
	s/\b(sha1[^\s]*)\s+(.*)/sha1op38($1,$2)/geo		or

	s/\b(vmov[dq])\b(.+)%ymm([0-9]+)/$1$2%xmm$3/go		or
	s/\b(vmovdqu)\b(.+)%x%ymm([0-9]+)/$1$2%xmm$3/go		or
	s/\b(vpinsr[qd])\b(.+)%ymm([0-9]+),%ymm([0-9]+)/$1$2%xmm$3,%xmm$4/go	or
	s/\b(vpextr[qd])\b(.+)%ymm([0-9]+)/$1$2%xmm$3/go	or
	s/\b(vinserti128)\b(\s+)%ymm/$1$2\$1,%xmm/go		or
	s/\b(vpbroadcast[qd]\s+)%ymm([0-9]+)/$1%xmm$2/go;

	print $_,"\n";
}

close STDOUT;
