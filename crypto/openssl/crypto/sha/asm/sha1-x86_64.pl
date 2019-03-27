#! /usr/bin/env perl
# Copyright 2006-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. The module is, however, dual licensed under OpenSSL and
# CRYPTOGAMS licenses depending on where you obtain it. For further
# details see http://www.openssl.org/~appro/cryptogams/.
# ====================================================================
#
# sha1_block procedure for x86_64.
#
# It was brought to my attention that on EM64T compiler-generated code
# was far behind 32-bit assembler implementation. This is unlike on
# Opteron where compiler-generated code was only 15% behind 32-bit
# assembler, which originally made it hard to motivate the effort.
# There was suggestion to mechanically translate 32-bit code, but I
# dismissed it, reasoning that x86_64 offers enough register bank
# capacity to fully utilize SHA-1 parallelism. Therefore this fresh
# implementation:-) However! While 64-bit code does perform better
# on Opteron, I failed to beat 32-bit assembler on EM64T core. Well,
# x86_64 does offer larger *addressable* bank, but out-of-order core
# reaches for even more registers through dynamic aliasing, and EM64T
# core must have managed to run-time optimize even 32-bit code just as
# good as 64-bit one. Performance improvement is summarized in the
# following table:
#
#		gcc 3.4		32-bit asm	cycles/byte
# Opteron	+45%		+20%		6.8
# Xeon P4	+65%		+0%		9.9
# Core2		+60%		+10%		7.0

# August 2009.
#
# The code was revised to minimize code size and to maximize
# "distance" between instructions producing input to 'lea'
# instruction and the 'lea' instruction itself, which is essential
# for Intel Atom core.

# October 2010.
#
# Add SSSE3, Supplemental[!] SSE3, implementation. The idea behind it
# is to offload message schedule denoted by Wt in NIST specification,
# or Xupdate in OpenSSL source, to SIMD unit. See sha1-586.pl module
# for background and implementation details. The only difference from
# 32-bit code is that 64-bit code doesn't have to spill @X[] elements
# to free temporary registers.

# April 2011.
#
# Add AVX code path. See sha1-586.pl for further information.

# May 2013.
#
# Add AVX2+BMI code path. Initial attempt (utilizing BMI instructions
# and loading pair of consecutive blocks to 256-bit %ymm registers)
# did not provide impressive performance improvement till a crucial
# hint regarding the number of Xupdate iterations to pre-compute in
# advance was provided by Ilya Albrekht of Intel Corp.

# March 2014.
#
# Add support for Intel SHA Extensions.

######################################################################
# Current performance is summarized in following table. Numbers are
# CPU clock cycles spent to process single byte (less is better).
#
#		x86_64		SSSE3		AVX[2]
# P4		9.05		-
# Opteron	6.26		-
# Core2		6.55		6.05/+8%	-
# Westmere	6.73		5.30/+27%	-
# Sandy Bridge	7.70		6.10/+26%	4.99/+54%
# Ivy Bridge	6.06		4.67/+30%	4.60/+32%
# Haswell	5.45		4.15/+31%	3.57/+53%
# Skylake	5.18		4.06/+28%	3.54/+46%
# Bulldozer	9.11		5.95/+53%
# Ryzen		4.75		3.80/+24%	1.93/+150%(**)
# VIA Nano	9.32		7.15/+30%
# Atom		10.3		9.17/+12%
# Silvermont	13.1(*)		9.37/+40%
# Knights L	13.2(*)		9.68/+36%	8.30/+59%
# Goldmont	8.13		6.42/+27%	1.70/+380%(**)
#
# (*)	obviously suboptimal result, nothing was done about it,
#	because SSSE3 code is compiled unconditionally;
# (**)	SHAEXT result

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

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

if (!$avx && `$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|.*based on LLVM) ([2-9]\.[0-9]+)/) {
	$avx = ($2>=3.0) + ($2>3.0);
}

$shaext=1;	### set to zero if compiling for 1.0.1
$avx=1		if (!$shaext && $avx);

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

$ctx="%rdi";	# 1st arg
$inp="%rsi";	# 2nd arg
$num="%rdx";	# 3rd arg

# reassign arguments in order to produce more compact code
$ctx="%r8";
$inp="%r9";
$num="%r10";

$t0="%eax";
$t1="%ebx";
$t2="%ecx";
@xi=("%edx","%ebp","%r14d");
$A="%esi";
$B="%edi";
$C="%r11d";
$D="%r12d";
$E="%r13d";

@V=($A,$B,$C,$D,$E);

sub BODY_00_19 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;
$code.=<<___ if ($i==0);
	mov	`4*$i`($inp),$xi[0]
	bswap	$xi[0]
___
$code.=<<___ if ($i<15);
	mov	`4*$j`($inp),$xi[1]
	mov	$d,$t0
	mov	$xi[0],`4*$i`(%rsp)
	mov	$a,$t2
	bswap	$xi[1]
	xor	$c,$t0
	rol	\$5,$t2
	and	$b,$t0
	lea	0x5a827999($xi[0],$e),$e
	add	$t2,$e
	xor	$d,$t0
	rol	\$30,$b
	add	$t0,$e
___
$code.=<<___ if ($i>=15);
	xor	`4*($j%16)`(%rsp),$xi[1]
	mov	$d,$t0
	mov	$xi[0],`4*($i%16)`(%rsp)
	mov	$a,$t2
	xor	`4*(($j+2)%16)`(%rsp),$xi[1]
	xor	$c,$t0
	rol	\$5,$t2
	xor	`4*(($j+8)%16)`(%rsp),$xi[1]
	and	$b,$t0
	lea	0x5a827999($xi[0],$e),$e
	rol	\$30,$b
	xor	$d,$t0
	add	$t2,$e
	rol	\$1,$xi[1]
	add	$t0,$e
___
push(@xi,shift(@xi));
}

sub BODY_20_39 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;
my $K=($i<40)?0x6ed9eba1:0xca62c1d6;
$code.=<<___ if ($i<79);
	xor	`4*($j%16)`(%rsp),$xi[1]
	mov	$b,$t0
	`"mov	$xi[0],".4*($i%16)."(%rsp)"	if ($i<72)`
	mov	$a,$t2
	xor	`4*(($j+2)%16)`(%rsp),$xi[1]
	xor	$d,$t0
	rol	\$5,$t2
	xor	`4*(($j+8)%16)`(%rsp),$xi[1]
	lea	$K($xi[0],$e),$e
	xor	$c,$t0
	add	$t2,$e
	rol	\$30,$b
	add	$t0,$e
	rol	\$1,$xi[1]
___
$code.=<<___ if ($i==79);
	mov	$b,$t0
	mov	$a,$t2
	xor	$d,$t0
	lea	$K($xi[0],$e),$e
	rol	\$5,$t2
	xor	$c,$t0
	add	$t2,$e
	rol	\$30,$b
	add	$t0,$e
___
push(@xi,shift(@xi));
}

sub BODY_40_59 {
my ($i,$a,$b,$c,$d,$e)=@_;
my $j=$i+1;
$code.=<<___;
	xor	`4*($j%16)`(%rsp),$xi[1]
	mov	$d,$t0
	mov	$xi[0],`4*($i%16)`(%rsp)
	mov	$d,$t1
	xor	`4*(($j+2)%16)`(%rsp),$xi[1]
	and	$c,$t0
	mov	$a,$t2
	xor	`4*(($j+8)%16)`(%rsp),$xi[1]
	lea	0x8f1bbcdc($xi[0],$e),$e
	xor	$c,$t1
	rol	\$5,$t2
	add	$t0,$e
	rol	\$1,$xi[1]
	and	$b,$t1
	add	$t2,$e
	rol	\$30,$b
	add	$t1,$e
___
push(@xi,shift(@xi));
}

$code.=<<___;
.text
.extern	OPENSSL_ia32cap_P

.globl	sha1_block_data_order
.type	sha1_block_data_order,\@function,3
.align	16
sha1_block_data_order:
.cfi_startproc
	mov	OPENSSL_ia32cap_P+0(%rip),%r9d
	mov	OPENSSL_ia32cap_P+4(%rip),%r8d
	mov	OPENSSL_ia32cap_P+8(%rip),%r10d
	test	\$`1<<9`,%r8d		# check SSSE3 bit
	jz	.Lialu
___
$code.=<<___ if ($shaext);
	test	\$`1<<29`,%r10d		# check SHA bit
	jnz	_shaext_shortcut
___
$code.=<<___ if ($avx>1);
	and	\$`1<<3|1<<5|1<<8`,%r10d	# check AVX2+BMI1+BMI2
	cmp	\$`1<<3|1<<5|1<<8`,%r10d
	je	_avx2_shortcut
___
$code.=<<___ if ($avx);
	and	\$`1<<28`,%r8d		# mask AVX bit
	and	\$`1<<30`,%r9d		# mask "Intel CPU" bit
	or	%r9d,%r8d
	cmp	\$`1<<28|1<<30`,%r8d
	je	_avx_shortcut
___
$code.=<<___;
	jmp	_ssse3_shortcut

.align	16
.Lialu:
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
	mov	%rdi,$ctx	# reassigned argument
	sub	\$`8+16*4`,%rsp
	mov	%rsi,$inp	# reassigned argument
	and	\$-64,%rsp
	mov	%rdx,$num	# reassigned argument
	mov	%rax,`16*4`(%rsp)
.cfi_cfa_expression	%rsp+64,deref,+8
.Lprologue:

	mov	0($ctx),$A
	mov	4($ctx),$B
	mov	8($ctx),$C
	mov	12($ctx),$D
	mov	16($ctx),$E
	jmp	.Lloop

.align	16
.Lloop:
___
for($i=0;$i<20;$i++)	{ &BODY_00_19($i,@V); unshift(@V,pop(@V)); }
for(;$i<40;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
for(;$i<60;$i++)	{ &BODY_40_59($i,@V); unshift(@V,pop(@V)); }
for(;$i<80;$i++)	{ &BODY_20_39($i,@V); unshift(@V,pop(@V)); }
$code.=<<___;
	add	0($ctx),$A
	add	4($ctx),$B
	add	8($ctx),$C
	add	12($ctx),$D
	add	16($ctx),$E
	mov	$A,0($ctx)
	mov	$B,4($ctx)
	mov	$C,8($ctx)
	mov	$D,12($ctx)
	mov	$E,16($ctx)

	sub	\$1,$num
	lea	`16*4`($inp),$inp
	jnz	.Lloop

	mov	`16*4`(%rsp),%rsi
.cfi_def_cfa	%rsi,8
	mov	-40(%rsi),%r14
.cfi_restore	%r14
	mov	-32(%rsi),%r13
.cfi_restore	%r13
	mov	-24(%rsi),%r12
.cfi_restore	%r12
	mov	-16(%rsi),%rbp
.cfi_restore	%rbp
	mov	-8(%rsi),%rbx
.cfi_restore	%rbx
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue:
	ret
.cfi_endproc
.size	sha1_block_data_order,.-sha1_block_data_order
___
if ($shaext) {{{
######################################################################
# Intel SHA Extensions implementation of SHA1 update function.
#
my ($ctx,$inp,$num)=("%rdi","%rsi","%rdx");
my ($ABCD,$E,$E_,$BSWAP,$ABCD_SAVE,$E_SAVE)=map("%xmm$_",(0..3,8,9));
my @MSG=map("%xmm$_",(4..7));

$code.=<<___;
.type	sha1_block_data_order_shaext,\@function,3
.align	32
sha1_block_data_order_shaext:
_shaext_shortcut:
.cfi_startproc
___
$code.=<<___ if ($win64);
	lea	`-8-4*16`(%rsp),%rsp
	movaps	%xmm6,-8-4*16(%rax)
	movaps	%xmm7,-8-3*16(%rax)
	movaps	%xmm8,-8-2*16(%rax)
	movaps	%xmm9,-8-1*16(%rax)
.Lprologue_shaext:
___
$code.=<<___;
	movdqu	($ctx),$ABCD
	movd	16($ctx),$E
	movdqa	K_XX_XX+0xa0(%rip),$BSWAP	# byte-n-word swap

	movdqu	($inp),@MSG[0]
	pshufd	\$0b00011011,$ABCD,$ABCD	# flip word order
	movdqu	0x10($inp),@MSG[1]
	pshufd	\$0b00011011,$E,$E		# flip word order
	movdqu	0x20($inp),@MSG[2]
	pshufb	$BSWAP,@MSG[0]
	movdqu	0x30($inp),@MSG[3]
	pshufb	$BSWAP,@MSG[1]
	pshufb	$BSWAP,@MSG[2]
	movdqa	$E,$E_SAVE			# offload $E
	pshufb	$BSWAP,@MSG[3]
	jmp	.Loop_shaext

.align	16
.Loop_shaext:
	dec		$num
	lea		0x40($inp),%r8		# next input block
	paddd		@MSG[0],$E
	cmovne		%r8,$inp
	movdqa		$ABCD,$ABCD_SAVE	# offload $ABCD
___
for($i=0;$i<20-4;$i+=2) {
$code.=<<___;
	sha1msg1	@MSG[1],@MSG[0]
	movdqa		$ABCD,$E_
	sha1rnds4	\$`int($i/5)`,$E,$ABCD	# 0-3...
	sha1nexte	@MSG[1],$E_
	pxor		@MSG[2],@MSG[0]
	sha1msg1	@MSG[2],@MSG[1]
	sha1msg2	@MSG[3],@MSG[0]

	movdqa		$ABCD,$E
	sha1rnds4	\$`int(($i+1)/5)`,$E_,$ABCD
	sha1nexte	@MSG[2],$E
	pxor		@MSG[3],@MSG[1]
	sha1msg2	@MSG[0],@MSG[1]
___
	push(@MSG,shift(@MSG));	push(@MSG,shift(@MSG));
}
$code.=<<___;
	movdqu		($inp),@MSG[0]
	movdqa		$ABCD,$E_
	sha1rnds4	\$3,$E,$ABCD		# 64-67
	sha1nexte	@MSG[1],$E_
	movdqu		0x10($inp),@MSG[1]
	pshufb		$BSWAP,@MSG[0]

	movdqa		$ABCD,$E
	sha1rnds4	\$3,$E_,$ABCD		# 68-71
	sha1nexte	@MSG[2],$E
	movdqu		0x20($inp),@MSG[2]
	pshufb		$BSWAP,@MSG[1]

	movdqa		$ABCD,$E_
	sha1rnds4	\$3,$E,$ABCD		# 72-75
	sha1nexte	@MSG[3],$E_
	movdqu		0x30($inp),@MSG[3]
	pshufb		$BSWAP,@MSG[2]

	movdqa		$ABCD,$E
	sha1rnds4	\$3,$E_,$ABCD		# 76-79
	sha1nexte	$E_SAVE,$E
	pshufb		$BSWAP,@MSG[3]

	paddd		$ABCD_SAVE,$ABCD
	movdqa		$E,$E_SAVE		# offload $E

	jnz		.Loop_shaext

	pshufd	\$0b00011011,$ABCD,$ABCD
	pshufd	\$0b00011011,$E,$E
	movdqu	$ABCD,($ctx)
	movd	$E,16($ctx)
___
$code.=<<___ if ($win64);
	movaps	-8-4*16(%rax),%xmm6
	movaps	-8-3*16(%rax),%xmm7
	movaps	-8-2*16(%rax),%xmm8
	movaps	-8-1*16(%rax),%xmm9
	mov	%rax,%rsp
.Lepilogue_shaext:
___
$code.=<<___;
.cfi_endproc
	ret
.size	sha1_block_data_order_shaext,.-sha1_block_data_order_shaext
___
}}}
{{{
my $Xi=4;
my @X=map("%xmm$_",(4..7,0..3));
my @Tx=map("%xmm$_",(8..10));
my $Kx="%xmm11";
my @V=($A,$B,$C,$D,$E)=("%eax","%ebx","%ecx","%edx","%ebp");	# size optimization
my @T=("%esi","%edi");
my $j=0;
my $rx=0;
my $K_XX_XX="%r14";
my $fp="%r11";

my $_rol=sub { &rol(@_) };
my $_ror=sub { &ror(@_) };

{ my $sn;
sub align32() {
  ++$sn;
$code.=<<___;
	jmp	.Lalign32_$sn	# see "Decoded ICache" in manual
.align	32
.Lalign32_$sn:
___
}
}

$code.=<<___;
.type	sha1_block_data_order_ssse3,\@function,3
.align	16
sha1_block_data_order_ssse3:
_ssse3_shortcut:
.cfi_startproc
	mov	%rsp,$fp	# frame pointer
.cfi_def_cfa_register	$fp
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r12
.cfi_push	%r12
	push	%r13		# redundant, done to share Win64 SE handler
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	lea	`-64-($win64?6*16:0)`(%rsp),%rsp
___
$code.=<<___ if ($win64);
	movaps	%xmm6,-40-6*16($fp)
	movaps	%xmm7,-40-5*16($fp)
	movaps	%xmm8,-40-4*16($fp)
	movaps	%xmm9,-40-3*16($fp)
	movaps	%xmm10,-40-2*16($fp)
	movaps	%xmm11,-40-1*16($fp)
.Lprologue_ssse3:
___
$code.=<<___;
	and	\$-64,%rsp
	mov	%rdi,$ctx	# reassigned argument
	mov	%rsi,$inp	# reassigned argument
	mov	%rdx,$num	# reassigned argument

	shl	\$6,$num
	add	$inp,$num
	lea	K_XX_XX+64(%rip),$K_XX_XX

	mov	0($ctx),$A		# load context
	mov	4($ctx),$B
	mov	8($ctx),$C
	mov	12($ctx),$D
	mov	$B,@T[0]		# magic seed
	mov	16($ctx),$E
	mov	$C,@T[1]
	xor	$D,@T[1]
	and	@T[1],@T[0]

	movdqa	64($K_XX_XX),@X[2]	# pbswap mask
	movdqa	-64($K_XX_XX),@Tx[1]	# K_00_19
	movdqu	0($inp),@X[-4&7]	# load input to %xmm[0-3]
	movdqu	16($inp),@X[-3&7]
	movdqu	32($inp),@X[-2&7]
	movdqu	48($inp),@X[-1&7]
	pshufb	@X[2],@X[-4&7]		# byte swap
	pshufb	@X[2],@X[-3&7]
	pshufb	@X[2],@X[-2&7]
	add	\$64,$inp
	paddd	@Tx[1],@X[-4&7]		# add K_00_19
	pshufb	@X[2],@X[-1&7]
	paddd	@Tx[1],@X[-3&7]
	paddd	@Tx[1],@X[-2&7]
	movdqa	@X[-4&7],0(%rsp)	# X[]+K xfer to IALU
	psubd	@Tx[1],@X[-4&7]		# restore X[]
	movdqa	@X[-3&7],16(%rsp)
	psubd	@Tx[1],@X[-3&7]
	movdqa	@X[-2&7],32(%rsp)
	psubd	@Tx[1],@X[-2&7]
	jmp	.Loop_ssse3
___

sub AUTOLOAD()		# thunk [simplified] 32-bit style perlasm
{ my $opcode = $AUTOLOAD; $opcode =~ s/.*:://;
  my $arg = pop;
    $arg = "\$$arg" if ($arg*1 eq $arg);
    $code .= "\t$opcode\t".join(',',$arg,reverse @_)."\n";
}

sub Xupdate_ssse3_16_31()		# recall that $Xi starts with 4
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 40 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));		# ror
	&pshufd	(@X[0],@X[-4&7],0xee);	# was &movdqa	(@X[0],@X[-3&7]);
	 eval(shift(@insns));
	&movdqa	(@Tx[0],@X[-1&7]);
	  &paddd	(@Tx[1],@X[-1&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	&punpcklqdq(@X[0],@X[-3&7]);	# compose "X[-14]" in "X[0]", was &palignr(@X[0],@X[-4&7],8);
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	&psrldq	(@Tx[0],4);		# "X[-3]", 3 dwords
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pxor	(@X[0],@X[-4&7]);	# "X[0]"^="X[-16]"
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	&pxor	(@Tx[0],@X[-2&7]);	# "X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pxor	(@X[0],@Tx[0]);		# "X[0]"^="X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	  &movdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));

	&movdqa	(@Tx[2],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	&movdqa	(@Tx[0],@X[0]);
	 eval(shift(@insns));

	&pslldq	(@Tx[2],12);		# "X[0]"<<96, extract one dword
	&paddd	(@X[0],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	&psrld	(@Tx[0],31);
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	&movdqa	(@Tx[1],@Tx[2]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	&psrld	(@Tx[2],30);
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	&por	(@X[0],@Tx[0]);		# "X[0]"<<<=1
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pslld	(@Tx[1],2);
	&pxor	(@X[0],@Tx[2]);
	 eval(shift(@insns));
	  &movdqa	(@Tx[2],eval(2*16*(($Xi)/5)-64)."($K_XX_XX)");	# K_XX_XX
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));

	&pxor	(@X[0],@Tx[1]);		# "X[0]"^=("X[0]">>96)<<<2
	&pshufd (@Tx[1],@X[-1&7],0xee)	if ($Xi==7);	# was &movdqa	(@Tx[0],@X[-1&7]) in Xupdate_ssse3_32_79

	 foreach (@insns) { eval; }	# remaining instructions [if any]

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
		push(@Tx,shift(@Tx));
}

sub Xupdate_ssse3_32_79()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 to 44 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns))		if ($Xi==8);
	&pxor	(@X[0],@X[-4&7]);	# "X[0]"="X[-32]"^"X[-16]"
	 eval(shift(@insns))		if ($Xi==8);
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns))		if (@insns[1] =~ /_ror/);
	 eval(shift(@insns))		if (@insns[0] =~ /_ror/);
	&punpcklqdq(@Tx[0],@X[-1&7]);	# compose "X[-6]", was &palignr(@Tx[0],@X[-2&7],8);
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol

	&pxor	(@X[0],@X[-7&7]);	# "X[0]"^="X[-28]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	if ($Xi%5) {
	  &movdqa	(@Tx[2],@Tx[1]);# "perpetuate" K_XX_XX...
	} else {			# ... or load next one
	  &movdqa	(@Tx[2],eval(2*16*($Xi/5)-64)."($K_XX_XX)");
	}
	 eval(shift(@insns));		# ror
	  &paddd	(@Tx[1],@X[-1&7]);
	 eval(shift(@insns));

	&pxor	(@X[0],@Tx[0]);		# "X[0]"^="X[-6]"
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns))		if (@insns[0] =~ /_ror/);

	&movdqa	(@Tx[0],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &movdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer to IALU
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));
	 eval(shift(@insns));		# body_20_39

	&pslld	(@X[0],2);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&psrld	(@Tx[0],30);
	 eval(shift(@insns))		if (@insns[0] =~ /_rol/);# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror

	&por	(@X[0],@Tx[0]);		# "X[0]"<<<=2
	 eval(shift(@insns));
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns))		if (@insns[1] =~ /_rol/);
	 eval(shift(@insns))		if (@insns[0] =~ /_rol/);
	  &pshufd(@Tx[1],@X[-1&7],0xee)	if ($Xi<19);	# was &movdqa	(@Tx[1],@X[0])
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));

	 foreach (@insns) { eval; }	# remaining instructions

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
		push(@Tx,shift(@Tx));
}

sub Xuplast_ssse3_80()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &paddd	(@Tx[1],@X[-1&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	  &movdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer IALU

	 foreach (@insns) { eval; }		# remaining instructions

	&cmp	($inp,$num);
	&je	(".Ldone_ssse3");

	unshift(@Tx,pop(@Tx));

	&movdqa	(@X[2],"64($K_XX_XX)");		# pbswap mask
	&movdqa	(@Tx[1],"-64($K_XX_XX)");	# K_00_19
	&movdqu	(@X[-4&7],"0($inp)");		# load input
	&movdqu	(@X[-3&7],"16($inp)");
	&movdqu	(@X[-2&7],"32($inp)");
	&movdqu	(@X[-1&7],"48($inp)");
	&pshufb	(@X[-4&7],@X[2]);		# byte swap
	&add	($inp,64);

  $Xi=0;
}

sub Xloop_ssse3()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&pshufb	(@X[($Xi-3)&7],@X[2]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&paddd	(@X[($Xi-4)&7],@Tx[1]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&movdqa	(eval(16*$Xi)."(%rsp)",@X[($Xi-4)&7]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&psubd	(@X[($Xi-4)&7],@Tx[1]);

	foreach (@insns) { eval; }
  $Xi++;
}

sub Xtail_ssse3()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	foreach (@insns) { eval; }
}

sub body_00_19 () {	# ((c^d)&b)^d
	# on start @T[0]=(c^d)&b
	return &body_20_39() if ($rx==19); $rx++;
	(
	'($a,$b,$c,$d,$e)=@V;'.
	'&$_ror	($b,$j?7:2)',	# $b>>>2
	'&xor	(@T[0],$d)',
	'&mov	(@T[1],$a)',	# $b for next round

	'&add	($e,eval(4*($j&15))."(%rsp)")',	# X[]+K xfer
	'&xor	($b,$c)',	# $c^$d for next round

	'&$_rol	($a,5)',
	'&add	($e,@T[0])',
	'&and	(@T[1],$b)',	# ($b&($c^$d)) for next round

	'&xor	($b,$c)',	# restore $b
	'&add	($e,$a);'	.'$j++; unshift(@V,pop(@V)); unshift(@T,pop(@T));'
	);
}

sub body_20_39 () {	# b^d^c
	# on entry @T[0]=b^d
	return &body_40_59() if ($rx==39); $rx++;
	(
	'($a,$b,$c,$d,$e)=@V;'.
	'&add	($e,eval(4*($j&15))."(%rsp)")',	# X[]+K xfer
	'&xor	(@T[0],$d)	if($j==19);'.
	'&xor	(@T[0],$c)	if($j> 19)',	# ($b^$d^$c)
	'&mov	(@T[1],$a)',	# $b for next round

	'&$_rol	($a,5)',
	'&add	($e,@T[0])',
	'&xor	(@T[1],$c)	if ($j< 79)',	# $b^$d for next round

	'&$_ror	($b,7)',	# $b>>>2
	'&add	($e,$a);'	.'$j++; unshift(@V,pop(@V)); unshift(@T,pop(@T));'
	);
}

sub body_40_59 () {	# ((b^c)&(c^d))^c
	# on entry @T[0]=(b^c), (c^=d)
	$rx++;
	(
	'($a,$b,$c,$d,$e)=@V;'.
	'&add	($e,eval(4*($j&15))."(%rsp)")',	# X[]+K xfer
	'&and	(@T[0],$c)	if ($j>=40)',	# (b^c)&(c^d)
	'&xor	($c,$d)		if ($j>=40)',	# restore $c

	'&$_ror	($b,7)',	# $b>>>2
	'&mov	(@T[1],$a)',	# $b for next round
	'&xor	(@T[0],$c)',

	'&$_rol	($a,5)',
	'&add	($e,@T[0])',
	'&xor	(@T[1],$c)	if ($j==59);'.
	'&xor	(@T[1],$b)	if ($j< 59)',	# b^c for next round

	'&xor	($b,$c)		if ($j< 59)',	# c^d for next round
	'&add	($e,$a);'	.'$j++; unshift(@V,pop(@V)); unshift(@T,pop(@T));'
	);
}
$code.=<<___;
.align	16
.Loop_ssse3:
___
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_16_31(\&body_00_19);
	&Xupdate_ssse3_32_79(\&body_00_19);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_40_59);
	&Xupdate_ssse3_32_79(\&body_20_39);
	&Xuplast_ssse3_80(\&body_20_39);	# can jump to "done"

				$saved_j=$j; @saved_V=@V;

	&Xloop_ssse3(\&body_20_39);
	&Xloop_ssse3(\&body_20_39);
	&Xloop_ssse3(\&body_20_39);

$code.=<<___;
	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	add	12($ctx),$D
	mov	$A,0($ctx)
	add	16($ctx),$E
	mov	@T[0],4($ctx)
	mov	@T[0],$B			# magic seed
	mov	$C,8($ctx)
	mov	$C,@T[1]
	mov	$D,12($ctx)
	xor	$D,@T[1]
	mov	$E,16($ctx)
	and	@T[1],@T[0]
	jmp	.Loop_ssse3

.align	16
.Ldone_ssse3:
___
				$j=$saved_j; @V=@saved_V;

	&Xtail_ssse3(\&body_20_39);
	&Xtail_ssse3(\&body_20_39);
	&Xtail_ssse3(\&body_20_39);

$code.=<<___;
	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	mov	$A,0($ctx)
	add	12($ctx),$D
	mov	@T[0],4($ctx)
	add	16($ctx),$E
	mov	$C,8($ctx)
	mov	$D,12($ctx)
	mov	$E,16($ctx)
___
$code.=<<___ if ($win64);
	movaps	-40-6*16($fp),%xmm6
	movaps	-40-5*16($fp),%xmm7
	movaps	-40-4*16($fp),%xmm8
	movaps	-40-3*16($fp),%xmm9
	movaps	-40-2*16($fp),%xmm10
	movaps	-40-1*16($fp),%xmm11
___
$code.=<<___;
	mov	-40($fp),%r14
.cfi_restore	%r14
	mov	-32($fp),%r13
.cfi_restore	%r13
	mov	-24($fp),%r12
.cfi_restore	%r12
	mov	-16($fp),%rbp
.cfi_restore	%rbp
	mov	-8($fp),%rbx
.cfi_restore	%rbx
	lea	($fp),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_ssse3:
	ret
.cfi_endproc
.size	sha1_block_data_order_ssse3,.-sha1_block_data_order_ssse3
___

if ($avx) {
$Xi=4;				# reset variables
@X=map("%xmm$_",(4..7,0..3));
@Tx=map("%xmm$_",(8..10));
$j=0;
$rx=0;

my $done_avx_label=".Ldone_avx";

my $_rol=sub { &shld(@_[0],@_) };
my $_ror=sub { &shrd(@_[0],@_) };

$code.=<<___;
.type	sha1_block_data_order_avx,\@function,3
.align	16
sha1_block_data_order_avx:
_avx_shortcut:
.cfi_startproc
	mov	%rsp,$fp
.cfi_def_cfa_register	$fp
	push	%rbx
.cfi_push	%rbx
	push	%rbp
.cfi_push	%rbp
	push	%r12
.cfi_push	%r12
	push	%r13		# redundant, done to share Win64 SE handler
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	lea	`-64-($win64?6*16:0)`(%rsp),%rsp
	vzeroupper
___
$code.=<<___ if ($win64);
	vmovaps	%xmm6,-40-6*16($fp)
	vmovaps	%xmm7,-40-5*16($fp)
	vmovaps	%xmm8,-40-4*16($fp)
	vmovaps	%xmm9,-40-3*16($fp)
	vmovaps	%xmm10,-40-2*16($fp)
	vmovaps	%xmm11,-40-1*16($fp)
.Lprologue_avx:
___
$code.=<<___;
	and	\$-64,%rsp
	mov	%rdi,$ctx	# reassigned argument
	mov	%rsi,$inp	# reassigned argument
	mov	%rdx,$num	# reassigned argument

	shl	\$6,$num
	add	$inp,$num
	lea	K_XX_XX+64(%rip),$K_XX_XX

	mov	0($ctx),$A		# load context
	mov	4($ctx),$B
	mov	8($ctx),$C
	mov	12($ctx),$D
	mov	$B,@T[0]		# magic seed
	mov	16($ctx),$E
	mov	$C,@T[1]
	xor	$D,@T[1]
	and	@T[1],@T[0]

	vmovdqa	64($K_XX_XX),@X[2]	# pbswap mask
	vmovdqa	-64($K_XX_XX),$Kx	# K_00_19
	vmovdqu	0($inp),@X[-4&7]	# load input to %xmm[0-3]
	vmovdqu	16($inp),@X[-3&7]
	vmovdqu	32($inp),@X[-2&7]
	vmovdqu	48($inp),@X[-1&7]
	vpshufb	@X[2],@X[-4&7],@X[-4&7]	# byte swap
	add	\$64,$inp
	vpshufb	@X[2],@X[-3&7],@X[-3&7]
	vpshufb	@X[2],@X[-2&7],@X[-2&7]
	vpshufb	@X[2],@X[-1&7],@X[-1&7]
	vpaddd	$Kx,@X[-4&7],@X[0]	# add K_00_19
	vpaddd	$Kx,@X[-3&7],@X[1]
	vpaddd	$Kx,@X[-2&7],@X[2]
	vmovdqa	@X[0],0(%rsp)		# X[]+K xfer to IALU
	vmovdqa	@X[1],16(%rsp)
	vmovdqa	@X[2],32(%rsp)
	jmp	.Loop_avx
___

sub Xupdate_avx_16_31()		# recall that $Xi starts with 4
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 40 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpalignr(@X[0],@X[-3&7],@X[-4&7],8);	# compose "X[-14]" in "X[0]"
	 eval(shift(@insns));
	 eval(shift(@insns));

	  &vpaddd	(@Tx[1],$Kx,@X[-1&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpsrldq(@Tx[0],@X[-1&7],4);		# "X[-3]", 3 dwords
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpxor	(@X[0],@X[0],@X[-4&7]);		# "X[0]"^="X[-16]"
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@Tx[0],@Tx[0],@X[-2&7]);	# "X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@Tx[0]);		# "X[0]"^="X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &vmovdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpsrld	(@Tx[0],@X[0],31);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpslldq(@Tx[2],@X[0],12);		# "X[0]"<<96, extract one dword
	&vpaddd	(@X[0],@X[0],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpsrld	(@Tx[1],@Tx[2],30);
	&vpor	(@X[0],@X[0],@Tx[0]);		# "X[0]"<<<=1
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpslld	(@Tx[2],@Tx[2],2);
	&vpxor	(@X[0],@X[0],@Tx[1]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@Tx[2]);		# "X[0]"^=("X[0]">>96)<<<2
	 eval(shift(@insns));
	 eval(shift(@insns));
	  &vmovdqa	($Kx,eval(2*16*(($Xi)/5)-64)."($K_XX_XX)")	if ($Xi%5==0);	# K_XX_XX
	 eval(shift(@insns));
	 eval(shift(@insns));


	 foreach (@insns) { eval; }	# remaining instructions [if any]

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
}

sub Xupdate_avx_32_79()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 to 44 instructions
  my ($a,$b,$c,$d,$e);

	&vpalignr(@Tx[0],@X[-1&7],@X[-2&7],8);	# compose "X[-6]"
	&vpxor	(@X[0],@X[0],@X[-4&7]);		# "X[0]"="X[-32]"^"X[-16]"
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol

	&vpxor	(@X[0],@X[0],@X[-7&7]);		# "X[0]"^="X[-28]"
	 eval(shift(@insns));
	 eval(shift(@insns))	if (@insns[0] !~ /&ro[rl]/);
	  &vpaddd	(@Tx[1],$Kx,@X[-1&7]);
	  &vmovdqa	($Kx,eval(2*16*($Xi/5)-64)."($K_XX_XX)")	if ($Xi%5==0);
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@Tx[0]);		# "X[0]"^="X[-6]"
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol

	&vpsrld	(@Tx[0],@X[0],30);
	  &vmovdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&vpslld	(@X[0],@X[0],2);
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# ror
	 eval(shift(@insns));

	&vpor	(@X[0],@X[0],@Tx[0]);		# "X[0]"<<<=2
	 eval(shift(@insns));		# body_20_39
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));		# rol
	 eval(shift(@insns));

	 foreach (@insns) { eval; }	# remaining instructions

  $Xi++;	push(@X,shift(@X));	# "rotate" X[]
}

sub Xuplast_avx_80()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	  &vpaddd	(@Tx[1],$Kx,@X[-1&7]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	  &vmovdqa	(eval(16*(($Xi-1)&3))."(%rsp)",@Tx[1]);	# X[]+K xfer IALU

	 foreach (@insns) { eval; }		# remaining instructions

	&cmp	($inp,$num);
	&je	($done_avx_label);

	&vmovdqa(@X[2],"64($K_XX_XX)");		# pbswap mask
	&vmovdqa($Kx,"-64($K_XX_XX)");		# K_00_19
	&vmovdqu(@X[-4&7],"0($inp)");		# load input
	&vmovdqu(@X[-3&7],"16($inp)");
	&vmovdqu(@X[-2&7],"32($inp)");
	&vmovdqu(@X[-1&7],"48($inp)");
	&vpshufb(@X[-4&7],@X[-4&7],@X[2]);	# byte swap
	&add	($inp,64);

  $Xi=0;
}

sub Xloop_avx()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpshufb(@X[($Xi-3)&7],@X[($Xi-3)&7],@X[2]);
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vpaddd	(@X[$Xi&7],@X[($Xi-4)&7],$Kx);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vmovdqa(eval(16*$Xi)."(%rsp)",@X[$Xi&7]);	# X[]+K xfer to IALU
	 eval(shift(@insns));
	 eval(shift(@insns));

	foreach (@insns) { eval; }
  $Xi++;
}

sub Xtail_avx()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	foreach (@insns) { eval; }
}

$code.=<<___;
.align	16
.Loop_avx:
___
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_16_31(\&body_00_19);
	&Xupdate_avx_32_79(\&body_00_19);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_40_59);
	&Xupdate_avx_32_79(\&body_20_39);
	&Xuplast_avx_80(\&body_20_39);	# can jump to "done"

				$saved_j=$j; @saved_V=@V;

	&Xloop_avx(\&body_20_39);
	&Xloop_avx(\&body_20_39);
	&Xloop_avx(\&body_20_39);

$code.=<<___;
	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	add	12($ctx),$D
	mov	$A,0($ctx)
	add	16($ctx),$E
	mov	@T[0],4($ctx)
	mov	@T[0],$B			# magic seed
	mov	$C,8($ctx)
	mov	$C,@T[1]
	mov	$D,12($ctx)
	xor	$D,@T[1]
	mov	$E,16($ctx)
	and	@T[1],@T[0]
	jmp	.Loop_avx

.align	16
$done_avx_label:
___
				$j=$saved_j; @V=@saved_V;

	&Xtail_avx(\&body_20_39);
	&Xtail_avx(\&body_20_39);
	&Xtail_avx(\&body_20_39);

$code.=<<___;
	vzeroupper

	add	0($ctx),$A			# update context
	add	4($ctx),@T[0]
	add	8($ctx),$C
	mov	$A,0($ctx)
	add	12($ctx),$D
	mov	@T[0],4($ctx)
	add	16($ctx),$E
	mov	$C,8($ctx)
	mov	$D,12($ctx)
	mov	$E,16($ctx)
___
$code.=<<___ if ($win64);
	movaps	-40-6*16($fp),%xmm6
	movaps	-40-5*16($fp),%xmm7
	movaps	-40-4*16($fp),%xmm8
	movaps	-40-3*16($fp),%xmm9
	movaps	-40-2*16($fp),%xmm10
	movaps	-40-1*16($fp),%xmm11
___
$code.=<<___;
	mov	-40($fp),%r14
.cfi_restore	%r14
	mov	-32($fp),%r13
.cfi_restore	%r13
	mov	-24($fp),%r12
.cfi_restore	%r12
	mov	-16($fp),%rbp
.cfi_restore	%rbp
	mov	-8($fp),%rbx
.cfi_restore	%rbx
	lea	($fp),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_avx:
	ret
.cfi_endproc
.size	sha1_block_data_order_avx,.-sha1_block_data_order_avx
___

if ($avx>1) {
use integer;
$Xi=4;					# reset variables
@X=map("%ymm$_",(4..7,0..3));
@Tx=map("%ymm$_",(8..10));
$Kx="%ymm11";
$j=0;

my @ROTX=("%eax","%ebp","%ebx","%ecx","%edx","%esi");
my ($a5,$t0)=("%r12d","%edi");

my ($A,$F,$B,$C,$D,$E)=@ROTX;
my $rx=0;
my $frame="%r13";

$code.=<<___;
.type	sha1_block_data_order_avx2,\@function,3
.align	16
sha1_block_data_order_avx2:
_avx2_shortcut:
.cfi_startproc
	mov	%rsp,$fp
.cfi_def_cfa_register	$fp
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
	vzeroupper
___
$code.=<<___ if ($win64);
	lea	-6*16(%rsp),%rsp
	vmovaps	%xmm6,-40-6*16($fp)
	vmovaps	%xmm7,-40-5*16($fp)
	vmovaps	%xmm8,-40-4*16($fp)
	vmovaps	%xmm9,-40-3*16($fp)
	vmovaps	%xmm10,-40-2*16($fp)
	vmovaps	%xmm11,-40-1*16($fp)
.Lprologue_avx2:
___
$code.=<<___;
	mov	%rdi,$ctx		# reassigned argument
	mov	%rsi,$inp		# reassigned argument
	mov	%rdx,$num		# reassigned argument

	lea	-640(%rsp),%rsp
	shl	\$6,$num
	 lea	64($inp),$frame
	and	\$-128,%rsp
	add	$inp,$num
	lea	K_XX_XX+64(%rip),$K_XX_XX

	mov	0($ctx),$A		# load context
	 cmp	$num,$frame
	 cmovae	$inp,$frame		# next or same block
	mov	4($ctx),$F
	mov	8($ctx),$C
	mov	12($ctx),$D
	mov	16($ctx),$E
	vmovdqu	64($K_XX_XX),@X[2]	# pbswap mask

	vmovdqu		($inp),%xmm0
	vmovdqu		16($inp),%xmm1
	vmovdqu		32($inp),%xmm2
	vmovdqu		48($inp),%xmm3
	lea		64($inp),$inp
	vinserti128	\$1,($frame),@X[-4&7],@X[-4&7]
	vinserti128	\$1,16($frame),@X[-3&7],@X[-3&7]
	vpshufb		@X[2],@X[-4&7],@X[-4&7]
	vinserti128	\$1,32($frame),@X[-2&7],@X[-2&7]
	vpshufb		@X[2],@X[-3&7],@X[-3&7]
	vinserti128	\$1,48($frame),@X[-1&7],@X[-1&7]
	vpshufb		@X[2],@X[-2&7],@X[-2&7]
	vmovdqu		-64($K_XX_XX),$Kx	# K_00_19
	vpshufb		@X[2],@X[-1&7],@X[-1&7]

	vpaddd	$Kx,@X[-4&7],@X[0]	# add K_00_19
	vpaddd	$Kx,@X[-3&7],@X[1]
	vmovdqu	@X[0],0(%rsp)		# X[]+K xfer to IALU
	vpaddd	$Kx,@X[-2&7],@X[2]
	vmovdqu	@X[1],32(%rsp)
	vpaddd	$Kx,@X[-1&7],@X[3]
	vmovdqu	@X[2],64(%rsp)
	vmovdqu	@X[3],96(%rsp)
___
for (;$Xi<8;$Xi++) {	# Xupdate_avx2_16_31
    use integer;

	&vpalignr(@X[0],@X[-3&7],@X[-4&7],8);	# compose "X[-14]" in "X[0]"
	&vpsrldq(@Tx[0],@X[-1&7],4);		# "X[-3]", 3 dwords
	&vpxor	(@X[0],@X[0],@X[-4&7]);		# "X[0]"^="X[-16]"
	&vpxor	(@Tx[0],@Tx[0],@X[-2&7]);	# "X[-3]"^"X[-8]"
	&vpxor	(@X[0],@X[0],@Tx[0]);		# "X[0]"^="X[-3]"^"X[-8]"
	&vpsrld	(@Tx[0],@X[0],31);
	&vmovdqu($Kx,eval(2*16*(($Xi)/5)-64)."($K_XX_XX)")	if ($Xi%5==0);	# K_XX_XX
	&vpslldq(@Tx[2],@X[0],12);		# "X[0]"<<96, extract one dword
	&vpaddd	(@X[0],@X[0],@X[0]);
	&vpsrld	(@Tx[1],@Tx[2],30);
	&vpor	(@X[0],@X[0],@Tx[0]);		# "X[0]"<<<=1
	&vpslld	(@Tx[2],@Tx[2],2);
	&vpxor	(@X[0],@X[0],@Tx[1]);
	&vpxor	(@X[0],@X[0],@Tx[2]);		# "X[0]"^=("X[0]">>96)<<<2
	&vpaddd	(@Tx[1],@X[0],$Kx);
	&vmovdqu("32*$Xi(%rsp)",@Tx[1]);	# X[]+K xfer to IALU

	push(@X,shift(@X));	# "rotate" X[]
}
$code.=<<___;
	lea	128(%rsp),$frame
	jmp	.Loop_avx2
.align	32
.Loop_avx2:
	rorx	\$2,$F,$B
	andn	$D,$F,$t0
	and	$C,$F
	xor	$t0,$F
___
sub bodyx_00_19 () {	# 8 instructions, 3 cycles critical path
	# at start $f=(b&c)^(~b&d), $b>>>=2
	return &bodyx_20_39() if ($rx==19); $rx++;
	(
	'($a,$f,$b,$c,$d,$e)=@ROTX;'.

	'&add	($e,((32*($j/4)+4*($j%4))%256-128)."($frame)");'.	# e+=X[i]+K
	 '&lea	($frame,"256($frame)")	if ($j%32==31);',
	'&andn	($t0,$a,$c)',			# ~b&d for next round

	'&add	($e,$f)',			# e+=(b&c)^(~b&d)
	'&rorx	($a5,$a,27)',			# a<<<5
	'&rorx	($f,$a,2)',			# b>>>2 for next round
	'&and	($a,$b)',			# b&c for next round

	'&add	($e,$a5)',			# e+=a<<<5
	'&xor	($a,$t0);'.			# f=(b&c)^(~b&d) for next round

	'unshift(@ROTX,pop(@ROTX)); $j++;'
	)
}

sub bodyx_20_39 () {	# 7 instructions, 2 cycles critical path
	# on entry $f=b^c^d, $b>>>=2
	return &bodyx_40_59() if ($rx==39); $rx++;
	(
	'($a,$f,$b,$c,$d,$e)=@ROTX;'.

	'&add	($e,((32*($j/4)+4*($j%4))%256-128)."($frame)");'.	# e+=X[i]+K
	 '&lea	($frame,"256($frame)")	if ($j%32==31);',

	'&lea	($e,"($e,$f)")',		# e+=b^c^d
	'&rorx	($a5,$a,27)',			# a<<<5
	'&rorx	($f,$a,2)	if ($j<79)',	# b>>>2 in next round
	'&xor	($a,$b)		if ($j<79)',	# b^c for next round

	'&add	($e,$a5)',			# e+=a<<<5
	'&xor	($a,$c)		if ($j<79);'.	# f=b^c^d for next round

	'unshift(@ROTX,pop(@ROTX)); $j++;'
	)
}

sub bodyx_40_59 () {	# 10 instructions, 3 cycles critical path
	# on entry $f=((b^c)&(c^d)), $b>>>=2
	$rx++;
	(
	'($a,$f,$b,$c,$d,$e)=@ROTX;'.

	'&add	($e,((32*($j/4)+4*($j%4))%256-128)."($frame)");'.	# e+=X[i]+K
	 '&lea	($frame,"256($frame)")	if ($j%32==31);',
	'&xor	($f,$c)		if ($j>39)',	# (b^c)&(c^d)^c
	'&mov	($t0,$b)	if ($j<59)',	# count on zero latency
	'&xor	($t0,$c)	if ($j<59)',	# c^d for next round

	'&lea	($e,"($e,$f)")',		# e+=(b^c)&(c^d)^c
	'&rorx	($a5,$a,27)',			# a<<<5
	'&rorx	($f,$a,2)',			# b>>>2 in next round
	'&xor	($a,$b)',			# b^c for next round

	'&add	($e,$a5)',			# e+=a<<<5
	'&and	($a,$t0)	if ($j< 59);'.	# f=(b^c)&(c^d) for next round
	'&xor	($a,$c)		if ($j==59);'.	# f=b^c^d for next round

	'unshift(@ROTX,pop(@ROTX)); $j++;'
	)
}

sub Xupdate_avx2_16_31()		# recall that $Xi starts with 4
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body,&$body);	# 35 instructions
  my ($a,$b,$c,$d,$e);

	&vpalignr(@X[0],@X[-3&7],@X[-4&7],8);	# compose "X[-14]" in "X[0]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpsrldq(@Tx[0],@X[-1&7],4);		# "X[-3]", 3 dwords
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@X[-4&7]);		# "X[0]"^="X[-16]"
	&vpxor	(@Tx[0],@Tx[0],@X[-2&7]);	# "X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@Tx[0]);		# "X[0]"^="X[-3]"^"X[-8]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpsrld	(@Tx[0],@X[0],31);
	&vmovdqu($Kx,eval(2*16*(($Xi)/5)-64)."($K_XX_XX)")	if ($Xi%5==0);	# K_XX_XX
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpslldq(@Tx[2],@X[0],12);		# "X[0]"<<96, extract one dword
	&vpaddd	(@X[0],@X[0],@X[0]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpsrld	(@Tx[1],@Tx[2],30);
	&vpor	(@X[0],@X[0],@Tx[0]);		# "X[0]"<<<=1
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpslld	(@Tx[2],@Tx[2],2);
	&vpxor	(@X[0],@X[0],@Tx[1]);
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@Tx[2]);		# "X[0]"^=("X[0]">>96)<<<2
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpaddd	(@Tx[1],@X[0],$Kx);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	&vmovdqu(eval(32*($Xi))."(%rsp)",@Tx[1]);	# X[]+K xfer to IALU

	 foreach (@insns) { eval; }	# remaining instructions [if any]

	$Xi++;
	push(@X,shift(@X));	# "rotate" X[]
}

sub Xupdate_avx2_32_79()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body,&$body);	# 35 to 50 instructions
  my ($a,$b,$c,$d,$e);

	&vpalignr(@Tx[0],@X[-1&7],@X[-2&7],8);	# compose "X[-6]"
	&vpxor	(@X[0],@X[0],@X[-4&7]);		# "X[0]"="X[-32]"^"X[-16]"
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@X[-7&7]);		# "X[0]"^="X[-28]"
	&vmovdqu($Kx,eval(2*16*($Xi/5)-64)."($K_XX_XX)")	if ($Xi%5==0);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpxor	(@X[0],@X[0],@Tx[0]);		# "X[0]"^="X[-6]"
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpsrld	(@Tx[0],@X[0],30);
	&vpslld	(@X[0],@X[0],2);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	#&vpslld	(@X[0],@X[0],2);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpor	(@X[0],@X[0],@Tx[0]);		# "X[0]"<<<=2
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vpaddd	(@Tx[1],@X[0],$Kx);
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));
	 eval(shift(@insns));

	&vmovdqu("32*$Xi(%rsp)",@Tx[1]);	# X[]+K xfer to IALU

	 foreach (@insns) { eval; }	# remaining instructions

	$Xi++;
	push(@X,shift(@X));	# "rotate" X[]
}

sub Xloop_avx2()
{ use integer;
  my $body = shift;
  my @insns = (&$body,&$body,&$body,&$body,&$body);	# 32 instructions
  my ($a,$b,$c,$d,$e);

	 foreach (@insns) { eval; }
}

	&align32();
	&Xupdate_avx2_32_79(\&bodyx_00_19);
	&Xupdate_avx2_32_79(\&bodyx_00_19);
	&Xupdate_avx2_32_79(\&bodyx_00_19);
	&Xupdate_avx2_32_79(\&bodyx_00_19);

	&Xupdate_avx2_32_79(\&bodyx_20_39);
	&Xupdate_avx2_32_79(\&bodyx_20_39);
	&Xupdate_avx2_32_79(\&bodyx_20_39);
	&Xupdate_avx2_32_79(\&bodyx_20_39);

	&align32();
	&Xupdate_avx2_32_79(\&bodyx_40_59);
	&Xupdate_avx2_32_79(\&bodyx_40_59);
	&Xupdate_avx2_32_79(\&bodyx_40_59);
	&Xupdate_avx2_32_79(\&bodyx_40_59);

	&Xloop_avx2(\&bodyx_20_39);
	&Xloop_avx2(\&bodyx_20_39);
	&Xloop_avx2(\&bodyx_20_39);
	&Xloop_avx2(\&bodyx_20_39);

$code.=<<___;
	lea	128($inp),$frame
	lea	128($inp),%rdi			# borrow $t0
	cmp	$num,$frame
	cmovae	$inp,$frame			# next or previous block

	# output is d-e-[a]-f-b-c => A=d,F=e,C=f,D=b,E=c
	add	0($ctx),@ROTX[0]		# update context
	add	4($ctx),@ROTX[1]
	add	8($ctx),@ROTX[3]
	mov	@ROTX[0],0($ctx)
	add	12($ctx),@ROTX[4]
	mov	@ROTX[1],4($ctx)
	 mov	@ROTX[0],$A			# A=d
	add	16($ctx),@ROTX[5]
	 mov	@ROTX[3],$a5
	mov	@ROTX[3],8($ctx)
	 mov	@ROTX[4],$D			# D=b
	 #xchg	@ROTX[5],$F			# F=c, C=f
	mov	@ROTX[4],12($ctx)
	 mov	@ROTX[1],$F			# F=e
	mov	@ROTX[5],16($ctx)
	#mov	$F,16($ctx)
	 mov	@ROTX[5],$E			# E=c
	 mov	$a5,$C				# C=f
	 #xchg	$F,$E				# E=c, F=e

	cmp	$num,$inp
	je	.Ldone_avx2
___

$Xi=4;				# reset variables
@X=map("%ymm$_",(4..7,0..3));

$code.=<<___;
	vmovdqu	64($K_XX_XX),@X[2]		# pbswap mask
	cmp	$num,%rdi			# borrowed $t0
	ja	.Last_avx2

	vmovdqu		-64(%rdi),%xmm0		# low part of @X[-4&7]
	vmovdqu		-48(%rdi),%xmm1
	vmovdqu		-32(%rdi),%xmm2
	vmovdqu		-16(%rdi),%xmm3
	vinserti128	\$1,0($frame),@X[-4&7],@X[-4&7]
	vinserti128	\$1,16($frame),@X[-3&7],@X[-3&7]
	vinserti128	\$1,32($frame),@X[-2&7],@X[-2&7]
	vinserti128	\$1,48($frame),@X[-1&7],@X[-1&7]
	jmp	.Last_avx2

.align	32
.Last_avx2:
	lea	128+16(%rsp),$frame
	rorx	\$2,$F,$B
	andn	$D,$F,$t0
	and	$C,$F
	xor	$t0,$F
	sub	\$-128,$inp
___
	$rx=$j=0;	@ROTX=($A,$F,$B,$C,$D,$E);

	&Xloop_avx2	(\&bodyx_00_19);
	&Xloop_avx2	(\&bodyx_00_19);
	&Xloop_avx2	(\&bodyx_00_19);
	&Xloop_avx2	(\&bodyx_00_19);

	&Xloop_avx2	(\&bodyx_20_39);
	  &vmovdqu	($Kx,"-64($K_XX_XX)");		# K_00_19
	  &vpshufb	(@X[-4&7],@X[-4&7],@X[2]);	# byte swap
	&Xloop_avx2	(\&bodyx_20_39);
	  &vpshufb	(@X[-3&7],@X[-3&7],@X[2]);
	  &vpaddd	(@Tx[0],@X[-4&7],$Kx);		# add K_00_19
	&Xloop_avx2	(\&bodyx_20_39);
	  &vmovdqu	("0(%rsp)",@Tx[0]);
	  &vpshufb	(@X[-2&7],@X[-2&7],@X[2]);
	  &vpaddd	(@Tx[1],@X[-3&7],$Kx);
	&Xloop_avx2	(\&bodyx_20_39);
	  &vmovdqu	("32(%rsp)",@Tx[1]);
	  &vpshufb	(@X[-1&7],@X[-1&7],@X[2]);
	  &vpaddd	(@X[2],@X[-2&7],$Kx);

	&Xloop_avx2	(\&bodyx_40_59);
	&align32	();
	  &vmovdqu	("64(%rsp)",@X[2]);
	  &vpaddd	(@X[3],@X[-1&7],$Kx);
	&Xloop_avx2	(\&bodyx_40_59);
	  &vmovdqu	("96(%rsp)",@X[3]);
	&Xloop_avx2	(\&bodyx_40_59);
	&Xupdate_avx2_16_31(\&bodyx_40_59);

	&Xupdate_avx2_16_31(\&bodyx_20_39);
	&Xupdate_avx2_16_31(\&bodyx_20_39);
	&Xupdate_avx2_16_31(\&bodyx_20_39);
	&Xloop_avx2	(\&bodyx_20_39);

$code.=<<___;
	lea	128(%rsp),$frame

	# output is d-e-[a]-f-b-c => A=d,F=e,C=f,D=b,E=c
	add	0($ctx),@ROTX[0]		# update context
	add	4($ctx),@ROTX[1]
	add	8($ctx),@ROTX[3]
	mov	@ROTX[0],0($ctx)
	add	12($ctx),@ROTX[4]
	mov	@ROTX[1],4($ctx)
	 mov	@ROTX[0],$A			# A=d
	add	16($ctx),@ROTX[5]
	 mov	@ROTX[3],$a5
	mov	@ROTX[3],8($ctx)
	 mov	@ROTX[4],$D			# D=b
	 #xchg	@ROTX[5],$F			# F=c, C=f
	mov	@ROTX[4],12($ctx)
	 mov	@ROTX[1],$F			# F=e
	mov	@ROTX[5],16($ctx)
	#mov	$F,16($ctx)
	 mov	@ROTX[5],$E			# E=c
	 mov	$a5,$C				# C=f
	 #xchg	$F,$E				# E=c, F=e

	cmp	$num,$inp
	jbe	.Loop_avx2

.Ldone_avx2:
	vzeroupper
___
$code.=<<___ if ($win64);
	movaps	-40-6*16($fp),%xmm6
	movaps	-40-5*16($fp),%xmm7
	movaps	-40-4*16($fp),%xmm8
	movaps	-40-3*16($fp),%xmm9
	movaps	-40-2*16($fp),%xmm10
	movaps	-40-1*16($fp),%xmm11
___
$code.=<<___;
	mov	-40($fp),%r14
.cfi_restore	%r14
	mov	-32($fp),%r13
.cfi_restore	%r13
	mov	-24($fp),%r12
.cfi_restore	%r12
	mov	-16($fp),%rbp
.cfi_restore	%rbp
	mov	-8($fp),%rbx
.cfi_restore	%rbx
	lea	($fp),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_avx2:
	ret
.cfi_endproc
.size	sha1_block_data_order_avx2,.-sha1_block_data_order_avx2
___
}
}
$code.=<<___;
.align	64
K_XX_XX:
.long	0x5a827999,0x5a827999,0x5a827999,0x5a827999	# K_00_19
.long	0x5a827999,0x5a827999,0x5a827999,0x5a827999	# K_00_19
.long	0x6ed9eba1,0x6ed9eba1,0x6ed9eba1,0x6ed9eba1	# K_20_39
.long	0x6ed9eba1,0x6ed9eba1,0x6ed9eba1,0x6ed9eba1	# K_20_39
.long	0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc	# K_40_59
.long	0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc,0x8f1bbcdc	# K_40_59
.long	0xca62c1d6,0xca62c1d6,0xca62c1d6,0xca62c1d6	# K_60_79
.long	0xca62c1d6,0xca62c1d6,0xca62c1d6,0xca62c1d6	# K_60_79
.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f	# pbswap mask
.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f	# pbswap mask
.byte	0xf,0xe,0xd,0xc,0xb,0xa,0x9,0x8,0x7,0x6,0x5,0x4,0x3,0x2,0x1,0x0
___
}}}
$code.=<<___;
.asciz	"SHA1 block transform for x86_64, CRYPTOGAMS by <appro\@openssl.org>"
.align	64
___

# EXCEPTION_DISPOSITION handler (EXCEPTION_RECORD *rec,ULONG64 frame,
#		CONTEXT *context,DISPATCHER_CONTEXT *disp)
if ($win64) {
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

	lea	.Lprologue(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<.Lprologue
	jb	.Lcommon_seh_tail

	mov	152($context),%rax	# pull context->Rsp

	lea	.Lepilogue(%rip),%r10
	cmp	%r10,%rbx		# context->Rip>=.Lepilogue
	jae	.Lcommon_seh_tail

	mov	`16*4`(%rax),%rax	# pull saved stack pointer

	mov	-8(%rax),%rbx
	mov	-16(%rax),%rbp
	mov	-24(%rax),%r12
	mov	-32(%rax),%r13
	mov	-40(%rax),%r14
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r12,216($context)	# restore context->R12
	mov	%r13,224($context)	# restore context->R13
	mov	%r14,232($context)	# restore context->R14

	jmp	.Lcommon_seh_tail
.size	se_handler,.-se_handler
___

$code.=<<___ if ($shaext);
.type	shaext_handler,\@abi-omnipotent
.align	16
shaext_handler:
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

	lea	.Lprologue_shaext(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<.Lprologue
	jb	.Lcommon_seh_tail

	lea	.Lepilogue_shaext(%rip),%r10
	cmp	%r10,%rbx		# context->Rip>=.Lepilogue
	jae	.Lcommon_seh_tail

	lea	-8-4*16(%rax),%rsi
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$8,%ecx
	.long	0xa548f3fc		# cld; rep movsq

	jmp	.Lcommon_seh_tail
.size	shaext_handler,.-shaext_handler
___

$code.=<<___;
.type	ssse3_handler,\@abi-omnipotent
.align	16
ssse3_handler:
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
	lea	(%rsi,%r10),%r10	# prologue label
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lcommon_seh_tail

	mov	208($context),%rax	# pull context->R11

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail

	lea	-40-6*16(%rax),%rsi
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$12,%ecx
	.long	0xa548f3fc		# cld; rep movsq

	mov	-8(%rax),%rbx
	mov	-16(%rax),%rbp
	mov	-24(%rax),%r12
	mov	-32(%rax),%r13
	mov	-40(%rax),%r14
	mov	%rbx,144($context)	# restore context->Rbx
	mov	%rbp,160($context)	# restore context->Rbp
	mov	%r12,216($context)	# restore context->R12
	mov	%r13,224($context)	# restore context->R13
	mov	%r14,232($context)	# restore context->R14

.Lcommon_seh_tail:
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
.size	ssse3_handler,.-ssse3_handler

.section	.pdata
.align	4
	.rva	.LSEH_begin_sha1_block_data_order
	.rva	.LSEH_end_sha1_block_data_order
	.rva	.LSEH_info_sha1_block_data_order
___
$code.=<<___ if ($shaext);
	.rva	.LSEH_begin_sha1_block_data_order_shaext
	.rva	.LSEH_end_sha1_block_data_order_shaext
	.rva	.LSEH_info_sha1_block_data_order_shaext
___
$code.=<<___;
	.rva	.LSEH_begin_sha1_block_data_order_ssse3
	.rva	.LSEH_end_sha1_block_data_order_ssse3
	.rva	.LSEH_info_sha1_block_data_order_ssse3
___
$code.=<<___ if ($avx);
	.rva	.LSEH_begin_sha1_block_data_order_avx
	.rva	.LSEH_end_sha1_block_data_order_avx
	.rva	.LSEH_info_sha1_block_data_order_avx
___
$code.=<<___ if ($avx>1);
	.rva	.LSEH_begin_sha1_block_data_order_avx2
	.rva	.LSEH_end_sha1_block_data_order_avx2
	.rva	.LSEH_info_sha1_block_data_order_avx2
___
$code.=<<___;
.section	.xdata
.align	8
.LSEH_info_sha1_block_data_order:
	.byte	9,0,0,0
	.rva	se_handler
___
$code.=<<___ if ($shaext);
.LSEH_info_sha1_block_data_order_shaext:
	.byte	9,0,0,0
	.rva	shaext_handler
___
$code.=<<___;
.LSEH_info_sha1_block_data_order_ssse3:
	.byte	9,0,0,0
	.rva	ssse3_handler
	.rva	.Lprologue_ssse3,.Lepilogue_ssse3	# HandlerData[]
___
$code.=<<___ if ($avx);
.LSEH_info_sha1_block_data_order_avx:
	.byte	9,0,0,0
	.rva	ssse3_handler
	.rva	.Lprologue_avx,.Lepilogue_avx		# HandlerData[]
___
$code.=<<___ if ($avx>1);
.LSEH_info_sha1_block_data_order_avx2:
	.byte	9,0,0,0
	.rva	ssse3_handler
	.rva	.Lprologue_avx2,.Lepilogue_avx2		# HandlerData[]
___
}

####################################################################

sub sha1rnds4 {
    if (@_[0] =~ /\$([x0-9a-f]+),\s*%xmm([0-7]),\s*%xmm([0-7])/) {
      my @opcode=(0x0f,0x3a,0xcc);
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
      my $rex=0;
	$rex|=0x04			if ($2>=8);
	$rex|=0x01			if ($1>=8);
	unshift @opcode,0x40|$rex	if ($rex);
	push @opcode,$opcodelet{$instr};
	push @opcode,0xc0|($1&7)|(($2&7)<<3);		# ModR/M
	return ".byte\t".join(',',@opcode);
    } else {
	return $instr."\t".@_[0];
    }
}

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/geo;

	s/\b(sha1rnds4)\s+(.*)/sha1rnds4($2)/geo	or
	s/\b(sha1[^\s]*)\s+(.*)/sha1op38($1,$2)/geo;

	print $_,"\n";
}
close STDOUT;
