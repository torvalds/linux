#! /usr/bin/env perl
# Copyright 2010-2019 The OpenSSL Project Authors. All Rights Reserved.
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
# March, June 2010
#
# The module implements "4-bit" GCM GHASH function and underlying
# single multiplication operation in GF(2^128). "4-bit" means that
# it uses 256 bytes per-key table [+128 bytes shared table]. GHASH
# function features so called "528B" variant utilizing additional
# 256+16 bytes of per-key storage [+512 bytes shared table].
# Performance results are for this streamed GHASH subroutine and are
# expressed in cycles per processed byte, less is better:
#
#		gcc 3.4.x(*)	assembler
#
# P4		28.6		14.0		+100%
# Opteron	19.3		7.7		+150%
# Core2		17.8		8.1(**)		+120%
# Atom		31.6		16.8		+88%
# VIA Nano	21.8		10.1		+115%
#
# (*)	comparison is not completely fair, because C results are
#	for vanilla "256B" implementation, while assembler results
#	are for "528B";-)
# (**)	it's mystery [to me] why Core2 result is not same as for
#	Opteron;

# May 2010
#
# Add PCLMULQDQ version performing at 2.02 cycles per processed byte.
# See ghash-x86.pl for background information and details about coding
# techniques.
#
# Special thanks to David Woodhouse for providing access to a
# Westmere-based system on behalf of Intel Open Source Technology Centre.

# December 2012
#
# Overhaul: aggregate Karatsuba post-processing, improve ILP in
# reduction_alg9, increase reduction aggregate factor to 4x. As for
# the latter. ghash-x86.pl discusses that it makes lesser sense to
# increase aggregate factor. Then why increase here? Critical path
# consists of 3 independent pclmulqdq instructions, Karatsuba post-
# processing and reduction. "On top" of this we lay down aggregated
# multiplication operations, triplets of independent pclmulqdq's. As
# issue rate for pclmulqdq is limited, it makes lesser sense to
# aggregate more multiplications than it takes to perform remaining
# non-multiplication operations. 2x is near-optimal coefficient for
# contemporary Intel CPUs (therefore modest improvement coefficient),
# but not for Bulldozer. Latter is because logical SIMD operations
# are twice as slow in comparison to Intel, so that critical path is
# longer. A CPU with higher pclmulqdq issue rate would also benefit
# from higher aggregate factor...
#
# Westmere	1.78(+13%)
# Sandy Bridge	1.80(+8%)
# Ivy Bridge	1.80(+7%)
# Haswell	0.55(+93%) (if system doesn't support AVX)
# Broadwell	0.45(+110%)(if system doesn't support AVX)
# Skylake	0.44(+110%)(if system doesn't support AVX)
# Bulldozer	1.49(+27%)
# Silvermont	2.88(+13%)
# Knights L	2.12(-)    (if system doesn't support AVX)
# Goldmont	1.08(+24%)

# March 2013
#
# ... 8x aggregate factor AVX code path is using reduction algorithm
# suggested by Shay Gueron[1]. Even though contemporary AVX-capable
# CPUs such as Sandy and Ivy Bridge can execute it, the code performs
# sub-optimally in comparison to above mentioned version. But thanks
# to Ilya Albrekht and Max Locktyukhin of Intel Corp. we knew that
# it performs in 0.41 cycles per byte on Haswell processor, in
# 0.29 on Broadwell, and in 0.36 on Skylake.
#
# Knights Landing achieves 1.09 cpb.
#
# [1] http://rt.openssl.org/Ticket/Display.html?id=2900&user=guest&pass=guest

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
	$avx = ($1>=2.20) + ($1>=2.22);
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

$do4xaggr=1;

# common register layout
$nlo="%rax";
$nhi="%rbx";
$Zlo="%r8";
$Zhi="%r9";
$tmp="%r10";
$rem_4bit = "%r11";

$Xi="%rdi";
$Htbl="%rsi";

# per-function register layout
$cnt="%rcx";
$rem="%rdx";

sub LB() { my $r=shift; $r =~ s/%[er]([a-d])x/%\1l/	or
			$r =~ s/%[er]([sd]i)/%\1l/	or
			$r =~ s/%[er](bp)/%\1l/		or
			$r =~ s/%(r[0-9]+)[d]?/%\1b/;   $r; }

sub AUTOLOAD()		# thunk [simplified] 32-bit style perlasm
{ my $opcode = $AUTOLOAD; $opcode =~ s/.*:://;
  my $arg = pop;
    $arg = "\$$arg" if ($arg*1 eq $arg);
    $code .= "\t$opcode\t".join(',',$arg,reverse @_)."\n";
}

{ my $N;
  sub loop() {
  my $inp = shift;

	$N++;
$code.=<<___;
	xor	$nlo,$nlo
	xor	$nhi,$nhi
	mov	`&LB("$Zlo")`,`&LB("$nlo")`
	mov	`&LB("$Zlo")`,`&LB("$nhi")`
	shl	\$4,`&LB("$nlo")`
	mov	\$14,$cnt
	mov	8($Htbl,$nlo),$Zlo
	mov	($Htbl,$nlo),$Zhi
	and	\$0xf0,`&LB("$nhi")`
	mov	$Zlo,$rem
	jmp	.Loop$N

.align	16
.Loop$N:
	shr	\$4,$Zlo
	and	\$0xf,$rem
	mov	$Zhi,$tmp
	mov	($inp,$cnt),`&LB("$nlo")`
	shr	\$4,$Zhi
	xor	8($Htbl,$nhi),$Zlo
	shl	\$60,$tmp
	xor	($Htbl,$nhi),$Zhi
	mov	`&LB("$nlo")`,`&LB("$nhi")`
	xor	($rem_4bit,$rem,8),$Zhi
	mov	$Zlo,$rem
	shl	\$4,`&LB("$nlo")`
	xor	$tmp,$Zlo
	dec	$cnt
	js	.Lbreak$N

	shr	\$4,$Zlo
	and	\$0xf,$rem
	mov	$Zhi,$tmp
	shr	\$4,$Zhi
	xor	8($Htbl,$nlo),$Zlo
	shl	\$60,$tmp
	xor	($Htbl,$nlo),$Zhi
	and	\$0xf0,`&LB("$nhi")`
	xor	($rem_4bit,$rem,8),$Zhi
	mov	$Zlo,$rem
	xor	$tmp,$Zlo
	jmp	.Loop$N

.align	16
.Lbreak$N:
	shr	\$4,$Zlo
	and	\$0xf,$rem
	mov	$Zhi,$tmp
	shr	\$4,$Zhi
	xor	8($Htbl,$nlo),$Zlo
	shl	\$60,$tmp
	xor	($Htbl,$nlo),$Zhi
	and	\$0xf0,`&LB("$nhi")`
	xor	($rem_4bit,$rem,8),$Zhi
	mov	$Zlo,$rem
	xor	$tmp,$Zlo

	shr	\$4,$Zlo
	and	\$0xf,$rem
	mov	$Zhi,$tmp
	shr	\$4,$Zhi
	xor	8($Htbl,$nhi),$Zlo
	shl	\$60,$tmp
	xor	($Htbl,$nhi),$Zhi
	xor	$tmp,$Zlo
	xor	($rem_4bit,$rem,8),$Zhi

	bswap	$Zlo
	bswap	$Zhi
___
}}

$code=<<___;
.text
.extern	OPENSSL_ia32cap_P

.globl	gcm_gmult_4bit
.type	gcm_gmult_4bit,\@function,2
.align	16
gcm_gmult_4bit:
.cfi_startproc
	push	%rbx
.cfi_push	%rbx
	push	%rbp		# %rbp and others are pushed exclusively in
.cfi_push	%rbp
	push	%r12		# order to reuse Win64 exception handler...
.cfi_push	%r12
	push	%r13
.cfi_push	%r13
	push	%r14
.cfi_push	%r14
	push	%r15
.cfi_push	%r15
	sub	\$280,%rsp
.cfi_adjust_cfa_offset	280
.Lgmult_prologue:

	movzb	15($Xi),$Zlo
	lea	.Lrem_4bit(%rip),$rem_4bit
___
	&loop	($Xi);
$code.=<<___;
	mov	$Zlo,8($Xi)
	mov	$Zhi,($Xi)

	lea	280+48(%rsp),%rsi
.cfi_def_cfa	%rsi,8
	mov	-8(%rsi),%rbx
.cfi_restore	%rbx
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lgmult_epilogue:
	ret
.cfi_endproc
.size	gcm_gmult_4bit,.-gcm_gmult_4bit
___

# per-function register layout
$inp="%rdx";
$len="%rcx";
$rem_8bit=$rem_4bit;

$code.=<<___;
.globl	gcm_ghash_4bit
.type	gcm_ghash_4bit,\@function,4
.align	16
gcm_ghash_4bit:
.cfi_startproc
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
	sub	\$280,%rsp
.cfi_adjust_cfa_offset	280
.Lghash_prologue:
	mov	$inp,%r14		# reassign couple of args
	mov	$len,%r15
___
{ my $inp="%r14";
  my $dat="%edx";
  my $len="%r15";
  my @nhi=("%ebx","%ecx");
  my @rem=("%r12","%r13");
  my $Hshr4="%rbp";

	&sub	($Htbl,-128);		# size optimization
	&lea	($Hshr4,"16+128(%rsp)");
	{ my @lo =($nlo,$nhi);
          my @hi =($Zlo,$Zhi);

	  &xor	($dat,$dat);
	  for ($i=0,$j=-2;$i<18;$i++,$j++) {
	    &mov	("$j(%rsp)",&LB($dat))		if ($i>1);
	    &or		($lo[0],$tmp)			if ($i>1);
	    &mov	(&LB($dat),&LB($lo[1]))		if ($i>0 && $i<17);
	    &shr	($lo[1],4)			if ($i>0 && $i<17);
	    &mov	($tmp,$hi[1])			if ($i>0 && $i<17);
	    &shr	($hi[1],4)			if ($i>0 && $i<17);
	    &mov	("8*$j($Hshr4)",$hi[0])		if ($i>1);
	    &mov	($hi[0],"16*$i+0-128($Htbl)")	if ($i<16);
	    &shl	(&LB($dat),4)			if ($i>0 && $i<17);
	    &mov	("8*$j-128($Hshr4)",$lo[0])	if ($i>1);
	    &mov	($lo[0],"16*$i+8-128($Htbl)")	if ($i<16);
	    &shl	($tmp,60)			if ($i>0 && $i<17);

	    push	(@lo,shift(@lo));
	    push	(@hi,shift(@hi));
	  }
	}
	&add	($Htbl,-128);
	&mov	($Zlo,"8($Xi)");
	&mov	($Zhi,"0($Xi)");
	&add	($len,$inp);		# pointer to the end of data
	&lea	($rem_8bit,".Lrem_8bit(%rip)");
	&jmp	(".Louter_loop");

$code.=".align	16\n.Louter_loop:\n";
	&xor	($Zhi,"($inp)");
	&mov	("%rdx","8($inp)");
	&lea	($inp,"16($inp)");
	&xor	("%rdx",$Zlo);
	&mov	("($Xi)",$Zhi);
	&mov	("8($Xi)","%rdx");
	&shr	("%rdx",32);

	&xor	($nlo,$nlo);
	&rol	($dat,8);
	&mov	(&LB($nlo),&LB($dat));
	&movz	($nhi[0],&LB($dat));
	&shl	(&LB($nlo),4);
	&shr	($nhi[0],4);

	for ($j=11,$i=0;$i<15;$i++) {
	    &rol	($dat,8);
	    &xor	($Zlo,"8($Htbl,$nlo)")			if ($i>0);
	    &xor	($Zhi,"($Htbl,$nlo)")			if ($i>0);
	    &mov	($Zlo,"8($Htbl,$nlo)")			if ($i==0);
	    &mov	($Zhi,"($Htbl,$nlo)")			if ($i==0);

	    &mov	(&LB($nlo),&LB($dat));
	    &xor	($Zlo,$tmp)				if ($i>0);
	    &movzw	($rem[1],"($rem_8bit,$rem[1],2)")	if ($i>0);

	    &movz	($nhi[1],&LB($dat));
	    &shl	(&LB($nlo),4);
	    &movzb	($rem[0],"(%rsp,$nhi[0])");

	    &shr	($nhi[1],4)				if ($i<14);
	    &and	($nhi[1],0xf0)				if ($i==14);
	    &shl	($rem[1],48)				if ($i>0);
	    &xor	($rem[0],$Zlo);

	    &mov	($tmp,$Zhi);
	    &xor	($Zhi,$rem[1])				if ($i>0);
	    &shr	($Zlo,8);

	    &movz	($rem[0],&LB($rem[0]));
	    &mov	($dat,"$j($Xi)")			if (--$j%4==0);
	    &shr	($Zhi,8);

	    &xor	($Zlo,"-128($Hshr4,$nhi[0],8)");
	    &shl	($tmp,56);
	    &xor	($Zhi,"($Hshr4,$nhi[0],8)");

	    unshift	(@nhi,pop(@nhi));		# "rotate" registers
	    unshift	(@rem,pop(@rem));
	}
	&movzw	($rem[1],"($rem_8bit,$rem[1],2)");
	&xor	($Zlo,"8($Htbl,$nlo)");
	&xor	($Zhi,"($Htbl,$nlo)");

	&shl	($rem[1],48);
	&xor	($Zlo,$tmp);

	&xor	($Zhi,$rem[1]);
	&movz	($rem[0],&LB($Zlo));
	&shr	($Zlo,4);

	&mov	($tmp,$Zhi);
	&shl	(&LB($rem[0]),4);
	&shr	($Zhi,4);

	&xor	($Zlo,"8($Htbl,$nhi[0])");
	&movzw	($rem[0],"($rem_8bit,$rem[0],2)");
	&shl	($tmp,60);

	&xor	($Zhi,"($Htbl,$nhi[0])");
	&xor	($Zlo,$tmp);
	&shl	($rem[0],48);

	&bswap	($Zlo);
	&xor	($Zhi,$rem[0]);

	&bswap	($Zhi);
	&cmp	($inp,$len);
	&jb	(".Louter_loop");
}
$code.=<<___;
	mov	$Zlo,8($Xi)
	mov	$Zhi,($Xi)

	lea	280+48(%rsp),%rsi
.cfi_def_cfa	%rsi,8
	mov	-48(%rsi),%r15
.cfi_restore	%r15
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
	lea	0(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lghash_epilogue:
	ret
.cfi_endproc
.size	gcm_ghash_4bit,.-gcm_ghash_4bit
___

######################################################################
# PCLMULQDQ version.

@_4args=$win64?	("%rcx","%rdx","%r8", "%r9") :	# Win64 order
		("%rdi","%rsi","%rdx","%rcx");	# Unix order

($Xi,$Xhi)=("%xmm0","%xmm1");	$Hkey="%xmm2";
($T1,$T2,$T3)=("%xmm3","%xmm4","%xmm5");

sub clmul64x64_T2 {	# minimal register pressure
my ($Xhi,$Xi,$Hkey,$HK)=@_;

if (!defined($HK)) {	$HK = $T2;
$code.=<<___;
	movdqa		$Xi,$Xhi		#
	pshufd		\$0b01001110,$Xi,$T1
	pshufd		\$0b01001110,$Hkey,$T2
	pxor		$Xi,$T1			#
	pxor		$Hkey,$T2
___
} else {
$code.=<<___;
	movdqa		$Xi,$Xhi		#
	pshufd		\$0b01001110,$Xi,$T1
	pxor		$Xi,$T1			#
___
}
$code.=<<___;
	pclmulqdq	\$0x00,$Hkey,$Xi	#######
	pclmulqdq	\$0x11,$Hkey,$Xhi	#######
	pclmulqdq	\$0x00,$HK,$T1		#######
	pxor		$Xi,$T1			#
	pxor		$Xhi,$T1		#

	movdqa		$T1,$T2			#
	psrldq		\$8,$T1
	pslldq		\$8,$T2			#
	pxor		$T1,$Xhi
	pxor		$T2,$Xi			#
___
}

sub reduction_alg9 {	# 17/11 times faster than Intel version
my ($Xhi,$Xi) = @_;

$code.=<<___;
	# 1st phase
	movdqa		$Xi,$T2			#
	movdqa		$Xi,$T1
	psllq		\$5,$Xi
	pxor		$Xi,$T1			#
	psllq		\$1,$Xi
	pxor		$T1,$Xi			#
	psllq		\$57,$Xi		#
	movdqa		$Xi,$T1			#
	pslldq		\$8,$Xi
	psrldq		\$8,$T1			#
	pxor		$T2,$Xi
	pxor		$T1,$Xhi		#

	# 2nd phase
	movdqa		$Xi,$T2
	psrlq		\$1,$Xi
	pxor		$T2,$Xhi		#
	pxor		$Xi,$T2
	psrlq		\$5,$Xi
	pxor		$T2,$Xi			#
	psrlq		\$1,$Xi			#
	pxor		$Xhi,$Xi		#
___
}

{ my ($Htbl,$Xip)=@_4args;
  my $HK="%xmm6";

$code.=<<___;
.globl	gcm_init_clmul
.type	gcm_init_clmul,\@abi-omnipotent
.align	16
gcm_init_clmul:
.cfi_startproc
.L_init_clmul:
___
$code.=<<___ if ($win64);
.LSEH_begin_gcm_init_clmul:
	# I can't trust assembler to use specific encoding:-(
	.byte	0x48,0x83,0xec,0x18		#sub	$0x18,%rsp
	.byte	0x0f,0x29,0x34,0x24		#movaps	%xmm6,(%rsp)
___
$code.=<<___;
	movdqu		($Xip),$Hkey
	pshufd		\$0b01001110,$Hkey,$Hkey	# dword swap

	# <<1 twist
	pshufd		\$0b11111111,$Hkey,$T2	# broadcast uppermost dword
	movdqa		$Hkey,$T1
	psllq		\$1,$Hkey
	pxor		$T3,$T3			#
	psrlq		\$63,$T1
	pcmpgtd		$T2,$T3			# broadcast carry bit
	pslldq		\$8,$T1
	por		$T1,$Hkey		# H<<=1

	# magic reduction
	pand		.L0x1c2_polynomial(%rip),$T3
	pxor		$T3,$Hkey		# if(carry) H^=0x1c2_polynomial

	# calculate H^2
	pshufd		\$0b01001110,$Hkey,$HK
	movdqa		$Hkey,$Xi
	pxor		$Hkey,$HK
___
	&clmul64x64_T2	($Xhi,$Xi,$Hkey,$HK);
	&reduction_alg9	($Xhi,$Xi);
$code.=<<___;
	pshufd		\$0b01001110,$Hkey,$T1
	pshufd		\$0b01001110,$Xi,$T2
	pxor		$Hkey,$T1		# Karatsuba pre-processing
	movdqu		$Hkey,0x00($Htbl)	# save H
	pxor		$Xi,$T2			# Karatsuba pre-processing
	movdqu		$Xi,0x10($Htbl)		# save H^2
	palignr		\$8,$T1,$T2		# low part is H.lo^H.hi...
	movdqu		$T2,0x20($Htbl)		# save Karatsuba "salt"
___
if ($do4xaggr) {
	&clmul64x64_T2	($Xhi,$Xi,$Hkey,$HK);	# H^3
	&reduction_alg9	($Xhi,$Xi);
$code.=<<___;
	movdqa		$Xi,$T3
___
	&clmul64x64_T2	($Xhi,$Xi,$Hkey,$HK);	# H^4
	&reduction_alg9	($Xhi,$Xi);
$code.=<<___;
	pshufd		\$0b01001110,$T3,$T1
	pshufd		\$0b01001110,$Xi,$T2
	pxor		$T3,$T1			# Karatsuba pre-processing
	movdqu		$T3,0x30($Htbl)		# save H^3
	pxor		$Xi,$T2			# Karatsuba pre-processing
	movdqu		$Xi,0x40($Htbl)		# save H^4
	palignr		\$8,$T1,$T2		# low part is H^3.lo^H^3.hi...
	movdqu		$T2,0x50($Htbl)		# save Karatsuba "salt"
___
}
$code.=<<___ if ($win64);
	movaps	(%rsp),%xmm6
	lea	0x18(%rsp),%rsp
.LSEH_end_gcm_init_clmul:
___
$code.=<<___;
	ret
.cfi_endproc
.size	gcm_init_clmul,.-gcm_init_clmul
___
}

{ my ($Xip,$Htbl)=@_4args;

$code.=<<___;
.globl	gcm_gmult_clmul
.type	gcm_gmult_clmul,\@abi-omnipotent
.align	16
gcm_gmult_clmul:
.cfi_startproc
.L_gmult_clmul:
	movdqu		($Xip),$Xi
	movdqa		.Lbswap_mask(%rip),$T3
	movdqu		($Htbl),$Hkey
	movdqu		0x20($Htbl),$T2
	pshufb		$T3,$Xi
___
	&clmul64x64_T2	($Xhi,$Xi,$Hkey,$T2);
$code.=<<___ if (0 || (&reduction_alg9($Xhi,$Xi)&&0));
	# experimental alternative. special thing about is that there
	# no dependency between the two multiplications...
	mov		\$`0xE1<<1`,%eax
	mov		\$0xA040608020C0E000,%r10	# ((7..0)·0xE0)&0xff
	mov		\$0x07,%r11d
	movq		%rax,$T1
	movq		%r10,$T2
	movq		%r11,$T3		# borrow $T3
	pand		$Xi,$T3
	pshufb		$T3,$T2			# ($Xi&7)·0xE0
	movq		%rax,$T3
	pclmulqdq	\$0x00,$Xi,$T1		# ·(0xE1<<1)
	pxor		$Xi,$T2
	pslldq		\$15,$T2
	paddd		$T2,$T2			# <<(64+56+1)
	pxor		$T2,$Xi
	pclmulqdq	\$0x01,$T3,$Xi
	movdqa		.Lbswap_mask(%rip),$T3	# reload $T3
	psrldq		\$1,$T1
	pxor		$T1,$Xhi
	pslldq		\$7,$Xi
	pxor		$Xhi,$Xi
___
$code.=<<___;
	pshufb		$T3,$Xi
	movdqu		$Xi,($Xip)
	ret
.cfi_endproc
.size	gcm_gmult_clmul,.-gcm_gmult_clmul
___
}

{ my ($Xip,$Htbl,$inp,$len)=@_4args;
  my ($Xln,$Xmn,$Xhn,$Hkey2,$HK) = map("%xmm$_",(3..7));
  my ($T1,$T2,$T3)=map("%xmm$_",(8..10));

$code.=<<___;
.globl	gcm_ghash_clmul
.type	gcm_ghash_clmul,\@abi-omnipotent
.align	32
gcm_ghash_clmul:
.cfi_startproc
.L_ghash_clmul:
___
$code.=<<___ if ($win64);
	lea	-0x88(%rsp),%rax
.LSEH_begin_gcm_ghash_clmul:
	# I can't trust assembler to use specific encoding:-(
	.byte	0x48,0x8d,0x60,0xe0		#lea	-0x20(%rax),%rsp
	.byte	0x0f,0x29,0x70,0xe0		#movaps	%xmm6,-0x20(%rax)
	.byte	0x0f,0x29,0x78,0xf0		#movaps	%xmm7,-0x10(%rax)
	.byte	0x44,0x0f,0x29,0x00		#movaps	%xmm8,0(%rax)
	.byte	0x44,0x0f,0x29,0x48,0x10	#movaps	%xmm9,0x10(%rax)
	.byte	0x44,0x0f,0x29,0x50,0x20	#movaps	%xmm10,0x20(%rax)
	.byte	0x44,0x0f,0x29,0x58,0x30	#movaps	%xmm11,0x30(%rax)
	.byte	0x44,0x0f,0x29,0x60,0x40	#movaps	%xmm12,0x40(%rax)
	.byte	0x44,0x0f,0x29,0x68,0x50	#movaps	%xmm13,0x50(%rax)
	.byte	0x44,0x0f,0x29,0x70,0x60	#movaps	%xmm14,0x60(%rax)
	.byte	0x44,0x0f,0x29,0x78,0x70	#movaps	%xmm15,0x70(%rax)
___
$code.=<<___;
	movdqa		.Lbswap_mask(%rip),$T3

	movdqu		($Xip),$Xi
	movdqu		($Htbl),$Hkey
	movdqu		0x20($Htbl),$HK
	pshufb		$T3,$Xi

	sub		\$0x10,$len
	jz		.Lodd_tail

	movdqu		0x10($Htbl),$Hkey2
___
if ($do4xaggr) {
my ($Xl,$Xm,$Xh,$Hkey3,$Hkey4)=map("%xmm$_",(11..15));

$code.=<<___;
	mov		OPENSSL_ia32cap_P+4(%rip),%eax
	cmp		\$0x30,$len
	jb		.Lskip4x

	and		\$`1<<26|1<<22`,%eax	# isolate MOVBE+XSAVE
	cmp		\$`1<<22`,%eax		# check for MOVBE without XSAVE
	je		.Lskip4x

	sub		\$0x30,$len
	mov		\$0xA040608020C0E000,%rax	# ((7..0)·0xE0)&0xff
	movdqu		0x30($Htbl),$Hkey3
	movdqu		0x40($Htbl),$Hkey4

	#######
	# Xi+4 =[(H*Ii+3) + (H^2*Ii+2) + (H^3*Ii+1) + H^4*(Ii+Xi)] mod P
	#
	movdqu		0x30($inp),$Xln
	 movdqu		0x20($inp),$Xl
	pshufb		$T3,$Xln
	 pshufb		$T3,$Xl
	movdqa		$Xln,$Xhn
	pshufd		\$0b01001110,$Xln,$Xmn
	pxor		$Xln,$Xmn
	pclmulqdq	\$0x00,$Hkey,$Xln
	pclmulqdq	\$0x11,$Hkey,$Xhn
	pclmulqdq	\$0x00,$HK,$Xmn

	movdqa		$Xl,$Xh
	pshufd		\$0b01001110,$Xl,$Xm
	pxor		$Xl,$Xm
	pclmulqdq	\$0x00,$Hkey2,$Xl
	pclmulqdq	\$0x11,$Hkey2,$Xh
	pclmulqdq	\$0x10,$HK,$Xm
	xorps		$Xl,$Xln
	xorps		$Xh,$Xhn
	movups		0x50($Htbl),$HK
	xorps		$Xm,$Xmn

	movdqu		0x10($inp),$Xl
	 movdqu		0($inp),$T1
	pshufb		$T3,$Xl
	 pshufb		$T3,$T1
	movdqa		$Xl,$Xh
	pshufd		\$0b01001110,$Xl,$Xm
	 pxor		$T1,$Xi
	pxor		$Xl,$Xm
	pclmulqdq	\$0x00,$Hkey3,$Xl
	 movdqa		$Xi,$Xhi
	 pshufd		\$0b01001110,$Xi,$T1
	 pxor		$Xi,$T1
	pclmulqdq	\$0x11,$Hkey3,$Xh
	pclmulqdq	\$0x00,$HK,$Xm
	xorps		$Xl,$Xln
	xorps		$Xh,$Xhn

	lea	0x40($inp),$inp
	sub	\$0x40,$len
	jc	.Ltail4x

	jmp	.Lmod4_loop
.align	32
.Lmod4_loop:
	pclmulqdq	\$0x00,$Hkey4,$Xi
	xorps		$Xm,$Xmn
	 movdqu		0x30($inp),$Xl
	 pshufb		$T3,$Xl
	pclmulqdq	\$0x11,$Hkey4,$Xhi
	xorps		$Xln,$Xi
	 movdqu		0x20($inp),$Xln
	 movdqa		$Xl,$Xh
	pclmulqdq	\$0x10,$HK,$T1
	 pshufd		\$0b01001110,$Xl,$Xm
	xorps		$Xhn,$Xhi
	 pxor		$Xl,$Xm
	 pshufb		$T3,$Xln
	movups		0x20($Htbl),$HK
	xorps		$Xmn,$T1
	 pclmulqdq	\$0x00,$Hkey,$Xl
	 pshufd		\$0b01001110,$Xln,$Xmn

	pxor		$Xi,$T1			# aggregated Karatsuba post-processing
	 movdqa		$Xln,$Xhn
	pxor		$Xhi,$T1		#
	 pxor		$Xln,$Xmn
	movdqa		$T1,$T2			#
	 pclmulqdq	\$0x11,$Hkey,$Xh
	pslldq		\$8,$T1
	psrldq		\$8,$T2			#
	pxor		$T1,$Xi
	movdqa		.L7_mask(%rip),$T1
	pxor		$T2,$Xhi		#
	movq		%rax,$T2

	pand		$Xi,$T1			# 1st phase
	pshufb		$T1,$T2			#
	pxor		$Xi,$T2			#
	 pclmulqdq	\$0x00,$HK,$Xm
	psllq		\$57,$T2		#
	movdqa		$T2,$T1			#
	pslldq		\$8,$T2
	 pclmulqdq	\$0x00,$Hkey2,$Xln
	psrldq		\$8,$T1			#
	pxor		$T2,$Xi
	pxor		$T1,$Xhi		#
	movdqu		0($inp),$T1

	movdqa		$Xi,$T2			# 2nd phase
	psrlq		\$1,$Xi
	 pclmulqdq	\$0x11,$Hkey2,$Xhn
	 xorps		$Xl,$Xln
	 movdqu		0x10($inp),$Xl
	 pshufb		$T3,$Xl
	 pclmulqdq	\$0x10,$HK,$Xmn
	 xorps		$Xh,$Xhn
	 movups		0x50($Htbl),$HK
	pshufb		$T3,$T1
	pxor		$T2,$Xhi		#
	pxor		$Xi,$T2
	psrlq		\$5,$Xi

	 movdqa		$Xl,$Xh
	 pxor		$Xm,$Xmn
	 pshufd		\$0b01001110,$Xl,$Xm
	pxor		$T2,$Xi			#
	pxor		$T1,$Xhi
	 pxor		$Xl,$Xm
	 pclmulqdq	\$0x00,$Hkey3,$Xl
	psrlq		\$1,$Xi			#
	pxor		$Xhi,$Xi		#
	movdqa		$Xi,$Xhi
	 pclmulqdq	\$0x11,$Hkey3,$Xh
	 xorps		$Xl,$Xln
	pshufd		\$0b01001110,$Xi,$T1
	pxor		$Xi,$T1

	 pclmulqdq	\$0x00,$HK,$Xm
	 xorps		$Xh,$Xhn

	lea	0x40($inp),$inp
	sub	\$0x40,$len
	jnc	.Lmod4_loop

.Ltail4x:
	pclmulqdq	\$0x00,$Hkey4,$Xi
	pclmulqdq	\$0x11,$Hkey4,$Xhi
	pclmulqdq	\$0x10,$HK,$T1
	xorps		$Xm,$Xmn
	xorps		$Xln,$Xi
	xorps		$Xhn,$Xhi
	pxor		$Xi,$Xhi		# aggregated Karatsuba post-processing
	pxor		$Xmn,$T1

	pxor		$Xhi,$T1		#
	pxor		$Xi,$Xhi

	movdqa		$T1,$T2			#
	psrldq		\$8,$T1
	pslldq		\$8,$T2			#
	pxor		$T1,$Xhi
	pxor		$T2,$Xi			#
___
	&reduction_alg9($Xhi,$Xi);
$code.=<<___;
	add	\$0x40,$len
	jz	.Ldone
	movdqu	0x20($Htbl),$HK
	sub	\$0x10,$len
	jz	.Lodd_tail
.Lskip4x:
___
}
$code.=<<___;
	#######
	# Xi+2 =[H*(Ii+1 + Xi+1)] mod P =
	#	[(H*Ii+1) + (H*Xi+1)] mod P =
	#	[(H*Ii+1) + H^2*(Ii+Xi)] mod P
	#
	movdqu		($inp),$T1		# Ii
	movdqu		16($inp),$Xln		# Ii+1
	pshufb		$T3,$T1
	pshufb		$T3,$Xln
	pxor		$T1,$Xi			# Ii+Xi

	movdqa		$Xln,$Xhn
	pshufd		\$0b01001110,$Xln,$Xmn
	pxor		$Xln,$Xmn
	pclmulqdq	\$0x00,$Hkey,$Xln
	pclmulqdq	\$0x11,$Hkey,$Xhn
	pclmulqdq	\$0x00,$HK,$Xmn

	lea		32($inp),$inp		# i+=2
	nop
	sub		\$0x20,$len
	jbe		.Leven_tail
	nop
	jmp		.Lmod_loop

.align	32
.Lmod_loop:
	movdqa		$Xi,$Xhi
	movdqa		$Xmn,$T1
	pshufd		\$0b01001110,$Xi,$Xmn	#
	pxor		$Xi,$Xmn		#

	pclmulqdq	\$0x00,$Hkey2,$Xi
	pclmulqdq	\$0x11,$Hkey2,$Xhi
	pclmulqdq	\$0x10,$HK,$Xmn

	pxor		$Xln,$Xi		# (H*Ii+1) + H^2*(Ii+Xi)
	pxor		$Xhn,$Xhi
	  movdqu	($inp),$T2		# Ii
	pxor		$Xi,$T1			# aggregated Karatsuba post-processing
	  pshufb	$T3,$T2
	  movdqu	16($inp),$Xln		# Ii+1

	pxor		$Xhi,$T1
	  pxor		$T2,$Xhi		# "Ii+Xi", consume early
	pxor		$T1,$Xmn
	 pshufb		$T3,$Xln
	movdqa		$Xmn,$T1		#
	psrldq		\$8,$T1
	pslldq		\$8,$Xmn		#
	pxor		$T1,$Xhi
	pxor		$Xmn,$Xi		#

	movdqa		$Xln,$Xhn		#

	  movdqa	$Xi,$T2			# 1st phase
	  movdqa	$Xi,$T1
	  psllq		\$5,$Xi
	  pxor		$Xi,$T1			#
	pclmulqdq	\$0x00,$Hkey,$Xln	#######
	  psllq		\$1,$Xi
	  pxor		$T1,$Xi			#
	  psllq		\$57,$Xi		#
	  movdqa	$Xi,$T1			#
	  pslldq	\$8,$Xi
	  psrldq	\$8,$T1			#
	  pxor		$T2,$Xi
	pshufd		\$0b01001110,$Xhn,$Xmn
	  pxor		$T1,$Xhi		#
	pxor		$Xhn,$Xmn		#

	  movdqa	$Xi,$T2			# 2nd phase
	  psrlq		\$1,$Xi
	pclmulqdq	\$0x11,$Hkey,$Xhn	#######
	  pxor		$T2,$Xhi		#
	  pxor		$Xi,$T2
	  psrlq		\$5,$Xi
	  pxor		$T2,$Xi			#
	lea		32($inp),$inp
	  psrlq		\$1,$Xi			#
	pclmulqdq	\$0x00,$HK,$Xmn		#######
	  pxor		$Xhi,$Xi		#

	sub		\$0x20,$len
	ja		.Lmod_loop

.Leven_tail:
	 movdqa		$Xi,$Xhi
	 movdqa		$Xmn,$T1
	 pshufd		\$0b01001110,$Xi,$Xmn	#
	 pxor		$Xi,$Xmn		#

	pclmulqdq	\$0x00,$Hkey2,$Xi
	pclmulqdq	\$0x11,$Hkey2,$Xhi
	pclmulqdq	\$0x10,$HK,$Xmn

	pxor		$Xln,$Xi		# (H*Ii+1) + H^2*(Ii+Xi)
	pxor		$Xhn,$Xhi
	pxor		$Xi,$T1
	pxor		$Xhi,$T1
	pxor		$T1,$Xmn
	movdqa		$Xmn,$T1		#
	psrldq		\$8,$T1
	pslldq		\$8,$Xmn		#
	pxor		$T1,$Xhi
	pxor		$Xmn,$Xi		#
___
	&reduction_alg9	($Xhi,$Xi);
$code.=<<___;
	test		$len,$len
	jnz		.Ldone

.Lodd_tail:
	movdqu		($inp),$T1		# Ii
	pshufb		$T3,$T1
	pxor		$T1,$Xi			# Ii+Xi
___
	&clmul64x64_T2	($Xhi,$Xi,$Hkey,$HK);	# H*(Ii+Xi)
	&reduction_alg9	($Xhi,$Xi);
$code.=<<___;
.Ldone:
	pshufb		$T3,$Xi
	movdqu		$Xi,($Xip)
___
$code.=<<___ if ($win64);
	movaps	(%rsp),%xmm6
	movaps	0x10(%rsp),%xmm7
	movaps	0x20(%rsp),%xmm8
	movaps	0x30(%rsp),%xmm9
	movaps	0x40(%rsp),%xmm10
	movaps	0x50(%rsp),%xmm11
	movaps	0x60(%rsp),%xmm12
	movaps	0x70(%rsp),%xmm13
	movaps	0x80(%rsp),%xmm14
	movaps	0x90(%rsp),%xmm15
	lea	0xa8(%rsp),%rsp
.LSEH_end_gcm_ghash_clmul:
___
$code.=<<___;
	ret
.cfi_endproc
.size	gcm_ghash_clmul,.-gcm_ghash_clmul
___
}

$code.=<<___;
.globl	gcm_init_avx
.type	gcm_init_avx,\@abi-omnipotent
.align	32
gcm_init_avx:
.cfi_startproc
___
if ($avx) {
my ($Htbl,$Xip)=@_4args;
my $HK="%xmm6";

$code.=<<___ if ($win64);
.LSEH_begin_gcm_init_avx:
	# I can't trust assembler to use specific encoding:-(
	.byte	0x48,0x83,0xec,0x18		#sub	$0x18,%rsp
	.byte	0x0f,0x29,0x34,0x24		#movaps	%xmm6,(%rsp)
___
$code.=<<___;
	vzeroupper

	vmovdqu		($Xip),$Hkey
	vpshufd		\$0b01001110,$Hkey,$Hkey	# dword swap

	# <<1 twist
	vpshufd		\$0b11111111,$Hkey,$T2	# broadcast uppermost dword
	vpsrlq		\$63,$Hkey,$T1
	vpsllq		\$1,$Hkey,$Hkey
	vpxor		$T3,$T3,$T3		#
	vpcmpgtd	$T2,$T3,$T3		# broadcast carry bit
	vpslldq		\$8,$T1,$T1
	vpor		$T1,$Hkey,$Hkey		# H<<=1

	# magic reduction
	vpand		.L0x1c2_polynomial(%rip),$T3,$T3
	vpxor		$T3,$Hkey,$Hkey		# if(carry) H^=0x1c2_polynomial

	vpunpckhqdq	$Hkey,$Hkey,$HK
	vmovdqa		$Hkey,$Xi
	vpxor		$Hkey,$HK,$HK
	mov		\$4,%r10		# up to H^8
	jmp		.Linit_start_avx
___

sub clmul64x64_avx {
my ($Xhi,$Xi,$Hkey,$HK)=@_;

if (!defined($HK)) {	$HK = $T2;
$code.=<<___;
	vpunpckhqdq	$Xi,$Xi,$T1
	vpunpckhqdq	$Hkey,$Hkey,$T2
	vpxor		$Xi,$T1,$T1		#
	vpxor		$Hkey,$T2,$T2
___
} else {
$code.=<<___;
	vpunpckhqdq	$Xi,$Xi,$T1
	vpxor		$Xi,$T1,$T1		#
___
}
$code.=<<___;
	vpclmulqdq	\$0x11,$Hkey,$Xi,$Xhi	#######
	vpclmulqdq	\$0x00,$Hkey,$Xi,$Xi	#######
	vpclmulqdq	\$0x00,$HK,$T1,$T1	#######
	vpxor		$Xi,$Xhi,$T2		#
	vpxor		$T2,$T1,$T1		#

	vpslldq		\$8,$T1,$T2		#
	vpsrldq		\$8,$T1,$T1
	vpxor		$T2,$Xi,$Xi		#
	vpxor		$T1,$Xhi,$Xhi
___
}

sub reduction_avx {
my ($Xhi,$Xi) = @_;

$code.=<<___;
	vpsllq		\$57,$Xi,$T1		# 1st phase
	vpsllq		\$62,$Xi,$T2
	vpxor		$T1,$T2,$T2		#
	vpsllq		\$63,$Xi,$T1
	vpxor		$T1,$T2,$T2		#
	vpslldq		\$8,$T2,$T1		#
	vpsrldq		\$8,$T2,$T2
	vpxor		$T1,$Xi,$Xi		#
	vpxor		$T2,$Xhi,$Xhi

	vpsrlq		\$1,$Xi,$T2		# 2nd phase
	vpxor		$Xi,$Xhi,$Xhi
	vpxor		$T2,$Xi,$Xi		#
	vpsrlq		\$5,$T2,$T2
	vpxor		$T2,$Xi,$Xi		#
	vpsrlq		\$1,$Xi,$Xi		#
	vpxor		$Xhi,$Xi,$Xi		#
___
}

$code.=<<___;
.align	32
.Linit_loop_avx:
	vpalignr	\$8,$T1,$T2,$T3		# low part is H.lo^H.hi...
	vmovdqu		$T3,-0x10($Htbl)	# save Karatsuba "salt"
___
	&clmul64x64_avx	($Xhi,$Xi,$Hkey,$HK);	# calculate H^3,5,7
	&reduction_avx	($Xhi,$Xi);
$code.=<<___;
.Linit_start_avx:
	vmovdqa		$Xi,$T3
___
	&clmul64x64_avx	($Xhi,$Xi,$Hkey,$HK);	# calculate H^2,4,6,8
	&reduction_avx	($Xhi,$Xi);
$code.=<<___;
	vpshufd		\$0b01001110,$T3,$T1
	vpshufd		\$0b01001110,$Xi,$T2
	vpxor		$T3,$T1,$T1		# Karatsuba pre-processing
	vmovdqu		$T3,0x00($Htbl)		# save H^1,3,5,7
	vpxor		$Xi,$T2,$T2		# Karatsuba pre-processing
	vmovdqu		$Xi,0x10($Htbl)		# save H^2,4,6,8
	lea		0x30($Htbl),$Htbl
	sub		\$1,%r10
	jnz		.Linit_loop_avx

	vpalignr	\$8,$T2,$T1,$T3		# last "salt" is flipped
	vmovdqu		$T3,-0x10($Htbl)

	vzeroupper
___
$code.=<<___ if ($win64);
	movaps	(%rsp),%xmm6
	lea	0x18(%rsp),%rsp
.LSEH_end_gcm_init_avx:
___
$code.=<<___;
	ret
.cfi_endproc
.size	gcm_init_avx,.-gcm_init_avx
___
} else {
$code.=<<___;
	jmp	.L_init_clmul
.size	gcm_init_avx,.-gcm_init_avx
___
}

$code.=<<___;
.globl	gcm_gmult_avx
.type	gcm_gmult_avx,\@abi-omnipotent
.align	32
gcm_gmult_avx:
.cfi_startproc
	jmp	.L_gmult_clmul
.cfi_endproc
.size	gcm_gmult_avx,.-gcm_gmult_avx
___

$code.=<<___;
.globl	gcm_ghash_avx
.type	gcm_ghash_avx,\@abi-omnipotent
.align	32
gcm_ghash_avx:
.cfi_startproc
___
if ($avx) {
my ($Xip,$Htbl,$inp,$len)=@_4args;
my ($Xlo,$Xhi,$Xmi,
    $Zlo,$Zhi,$Zmi,
    $Hkey,$HK,$T1,$T2,
    $Xi,$Xo,$Tred,$bswap,$Ii,$Ij) = map("%xmm$_",(0..15));

$code.=<<___ if ($win64);
	lea	-0x88(%rsp),%rax
.LSEH_begin_gcm_ghash_avx:
	# I can't trust assembler to use specific encoding:-(
	.byte	0x48,0x8d,0x60,0xe0		#lea	-0x20(%rax),%rsp
	.byte	0x0f,0x29,0x70,0xe0		#movaps	%xmm6,-0x20(%rax)
	.byte	0x0f,0x29,0x78,0xf0		#movaps	%xmm7,-0x10(%rax)
	.byte	0x44,0x0f,0x29,0x00		#movaps	%xmm8,0(%rax)
	.byte	0x44,0x0f,0x29,0x48,0x10	#movaps	%xmm9,0x10(%rax)
	.byte	0x44,0x0f,0x29,0x50,0x20	#movaps	%xmm10,0x20(%rax)
	.byte	0x44,0x0f,0x29,0x58,0x30	#movaps	%xmm11,0x30(%rax)
	.byte	0x44,0x0f,0x29,0x60,0x40	#movaps	%xmm12,0x40(%rax)
	.byte	0x44,0x0f,0x29,0x68,0x50	#movaps	%xmm13,0x50(%rax)
	.byte	0x44,0x0f,0x29,0x70,0x60	#movaps	%xmm14,0x60(%rax)
	.byte	0x44,0x0f,0x29,0x78,0x70	#movaps	%xmm15,0x70(%rax)
___
$code.=<<___;
	vzeroupper

	vmovdqu		($Xip),$Xi		# load $Xi
	lea		.L0x1c2_polynomial(%rip),%r10
	lea		0x40($Htbl),$Htbl	# size optimization
	vmovdqu		.Lbswap_mask(%rip),$bswap
	vpshufb		$bswap,$Xi,$Xi
	cmp		\$0x80,$len
	jb		.Lshort_avx
	sub		\$0x80,$len

	vmovdqu		0x70($inp),$Ii		# I[7]
	vmovdqu		0x00-0x40($Htbl),$Hkey	# $Hkey^1
	vpshufb		$bswap,$Ii,$Ii
	vmovdqu		0x20-0x40($Htbl),$HK

	vpunpckhqdq	$Ii,$Ii,$T2
	 vmovdqu	0x60($inp),$Ij		# I[6]
	vpclmulqdq	\$0x00,$Hkey,$Ii,$Xlo
	vpxor		$Ii,$T2,$T2
	 vpshufb	$bswap,$Ij,$Ij
	vpclmulqdq	\$0x11,$Hkey,$Ii,$Xhi
	 vmovdqu	0x10-0x40($Htbl),$Hkey	# $Hkey^2
	 vpunpckhqdq	$Ij,$Ij,$T1
	 vmovdqu	0x50($inp),$Ii		# I[5]
	vpclmulqdq	\$0x00,$HK,$T2,$Xmi
	 vpxor		$Ij,$T1,$T1

	 vpshufb	$bswap,$Ii,$Ii
	vpclmulqdq	\$0x00,$Hkey,$Ij,$Zlo
	 vpunpckhqdq	$Ii,$Ii,$T2
	vpclmulqdq	\$0x11,$Hkey,$Ij,$Zhi
	 vmovdqu	0x30-0x40($Htbl),$Hkey	# $Hkey^3
	 vpxor		$Ii,$T2,$T2
	 vmovdqu	0x40($inp),$Ij		# I[4]
	vpclmulqdq	\$0x10,$HK,$T1,$Zmi
	 vmovdqu	0x50-0x40($Htbl),$HK

	 vpshufb	$bswap,$Ij,$Ij
	vpxor		$Xlo,$Zlo,$Zlo
	vpclmulqdq	\$0x00,$Hkey,$Ii,$Xlo
	vpxor		$Xhi,$Zhi,$Zhi
	 vpunpckhqdq	$Ij,$Ij,$T1
	vpclmulqdq	\$0x11,$Hkey,$Ii,$Xhi
	 vmovdqu	0x40-0x40($Htbl),$Hkey	# $Hkey^4
	vpxor		$Xmi,$Zmi,$Zmi
	vpclmulqdq	\$0x00,$HK,$T2,$Xmi
	 vpxor		$Ij,$T1,$T1

	 vmovdqu	0x30($inp),$Ii		# I[3]
	vpxor		$Zlo,$Xlo,$Xlo
	vpclmulqdq	\$0x00,$Hkey,$Ij,$Zlo
	vpxor		$Zhi,$Xhi,$Xhi
	 vpshufb	$bswap,$Ii,$Ii
	vpclmulqdq	\$0x11,$Hkey,$Ij,$Zhi
	 vmovdqu	0x60-0x40($Htbl),$Hkey	# $Hkey^5
	vpxor		$Zmi,$Xmi,$Xmi
	 vpunpckhqdq	$Ii,$Ii,$T2
	vpclmulqdq	\$0x10,$HK,$T1,$Zmi
	 vmovdqu	0x80-0x40($Htbl),$HK
	 vpxor		$Ii,$T2,$T2

	 vmovdqu	0x20($inp),$Ij		# I[2]
	vpxor		$Xlo,$Zlo,$Zlo
	vpclmulqdq	\$0x00,$Hkey,$Ii,$Xlo
	vpxor		$Xhi,$Zhi,$Zhi
	 vpshufb	$bswap,$Ij,$Ij
	vpclmulqdq	\$0x11,$Hkey,$Ii,$Xhi
	 vmovdqu	0x70-0x40($Htbl),$Hkey	# $Hkey^6
	vpxor		$Xmi,$Zmi,$Zmi
	 vpunpckhqdq	$Ij,$Ij,$T1
	vpclmulqdq	\$0x00,$HK,$T2,$Xmi
	 vpxor		$Ij,$T1,$T1

	 vmovdqu	0x10($inp),$Ii		# I[1]
	vpxor		$Zlo,$Xlo,$Xlo
	vpclmulqdq	\$0x00,$Hkey,$Ij,$Zlo
	vpxor		$Zhi,$Xhi,$Xhi
	 vpshufb	$bswap,$Ii,$Ii
	vpclmulqdq	\$0x11,$Hkey,$Ij,$Zhi
	 vmovdqu	0x90-0x40($Htbl),$Hkey	# $Hkey^7
	vpxor		$Zmi,$Xmi,$Xmi
	 vpunpckhqdq	$Ii,$Ii,$T2
	vpclmulqdq	\$0x10,$HK,$T1,$Zmi
	 vmovdqu	0xb0-0x40($Htbl),$HK
	 vpxor		$Ii,$T2,$T2

	 vmovdqu	($inp),$Ij		# I[0]
	vpxor		$Xlo,$Zlo,$Zlo
	vpclmulqdq	\$0x00,$Hkey,$Ii,$Xlo
	vpxor		$Xhi,$Zhi,$Zhi
	 vpshufb	$bswap,$Ij,$Ij
	vpclmulqdq	\$0x11,$Hkey,$Ii,$Xhi
	 vmovdqu	0xa0-0x40($Htbl),$Hkey	# $Hkey^8
	vpxor		$Xmi,$Zmi,$Zmi
	vpclmulqdq	\$0x10,$HK,$T2,$Xmi

	lea		0x80($inp),$inp
	cmp		\$0x80,$len
	jb		.Ltail_avx

	vpxor		$Xi,$Ij,$Ij		# accumulate $Xi
	sub		\$0x80,$len
	jmp		.Loop8x_avx

.align	32
.Loop8x_avx:
	vpunpckhqdq	$Ij,$Ij,$T1
	 vmovdqu	0x70($inp),$Ii		# I[7]
	vpxor		$Xlo,$Zlo,$Zlo
	vpxor		$Ij,$T1,$T1
	vpclmulqdq	\$0x00,$Hkey,$Ij,$Xi
	 vpshufb	$bswap,$Ii,$Ii
	vpxor		$Xhi,$Zhi,$Zhi
	vpclmulqdq	\$0x11,$Hkey,$Ij,$Xo
	 vmovdqu	0x00-0x40($Htbl),$Hkey	# $Hkey^1
	 vpunpckhqdq	$Ii,$Ii,$T2
	vpxor		$Xmi,$Zmi,$Zmi
	vpclmulqdq	\$0x00,$HK,$T1,$Tred
	 vmovdqu	0x20-0x40($Htbl),$HK
	 vpxor		$Ii,$T2,$T2

	  vmovdqu	0x60($inp),$Ij		# I[6]
	 vpclmulqdq	\$0x00,$Hkey,$Ii,$Xlo
	vpxor		$Zlo,$Xi,$Xi		# collect result
	  vpshufb	$bswap,$Ij,$Ij
	 vpclmulqdq	\$0x11,$Hkey,$Ii,$Xhi
	vxorps		$Zhi,$Xo,$Xo
	  vmovdqu	0x10-0x40($Htbl),$Hkey	# $Hkey^2
	 vpunpckhqdq	$Ij,$Ij,$T1
	 vpclmulqdq	\$0x00,$HK,  $T2,$Xmi
	vpxor		$Zmi,$Tred,$Tred
	 vxorps		$Ij,$T1,$T1

	  vmovdqu	0x50($inp),$Ii		# I[5]
	vpxor		$Xi,$Tred,$Tred		# aggregated Karatsuba post-processing
	 vpclmulqdq	\$0x00,$Hkey,$Ij,$Zlo
	vpxor		$Xo,$Tred,$Tred
	vpslldq		\$8,$Tred,$T2
	 vpxor		$Xlo,$Zlo,$Zlo
	 vpclmulqdq	\$0x11,$Hkey,$Ij,$Zhi
	vpsrldq		\$8,$Tred,$Tred
	vpxor		$T2, $Xi, $Xi
	  vmovdqu	0x30-0x40($Htbl),$Hkey	# $Hkey^3
	  vpshufb	$bswap,$Ii,$Ii
	vxorps		$Tred,$Xo, $Xo
	 vpxor		$Xhi,$Zhi,$Zhi
	 vpunpckhqdq	$Ii,$Ii,$T2
	 vpclmulqdq	\$0x10,$HK,  $T1,$Zmi
	  vmovdqu	0x50-0x40($Htbl),$HK
	 vpxor		$Ii,$T2,$T2
	 vpxor		$Xmi,$Zmi,$Zmi

	  vmovdqu	0x40($inp),$Ij		# I[4]
	vpalignr	\$8,$Xi,$Xi,$Tred	# 1st phase
	 vpclmulqdq	\$0x00,$Hkey,$Ii,$Xlo
	  vpshufb	$bswap,$Ij,$Ij
	 vpxor		$Zlo,$Xlo,$Xlo
	 vpclmulqdq	\$0x11,$Hkey,$Ii,$Xhi
	  vmovdqu	0x40-0x40($Htbl),$Hkey	# $Hkey^4
	 vpunpckhqdq	$Ij,$Ij,$T1
	 vpxor		$Zhi,$Xhi,$Xhi
	 vpclmulqdq	\$0x00,$HK,  $T2,$Xmi
	 vxorps		$Ij,$T1,$T1
	 vpxor		$Zmi,$Xmi,$Xmi

	  vmovdqu	0x30($inp),$Ii		# I[3]
	vpclmulqdq	\$0x10,(%r10),$Xi,$Xi
	 vpclmulqdq	\$0x00,$Hkey,$Ij,$Zlo
	  vpshufb	$bswap,$Ii,$Ii
	 vpxor		$Xlo,$Zlo,$Zlo
	 vpclmulqdq	\$0x11,$Hkey,$Ij,$Zhi
	  vmovdqu	0x60-0x40($Htbl),$Hkey	# $Hkey^5
	 vpunpckhqdq	$Ii,$Ii,$T2
	 vpxor		$Xhi,$Zhi,$Zhi
	 vpclmulqdq	\$0x10,$HK,  $T1,$Zmi
	  vmovdqu	0x80-0x40($Htbl),$HK
	 vpxor		$Ii,$T2,$T2
	 vpxor		$Xmi,$Zmi,$Zmi

	  vmovdqu	0x20($inp),$Ij		# I[2]
	 vpclmulqdq	\$0x00,$Hkey,$Ii,$Xlo
	  vpshufb	$bswap,$Ij,$Ij
	 vpxor		$Zlo,$Xlo,$Xlo
	 vpclmulqdq	\$0x11,$Hkey,$Ii,$Xhi
	  vmovdqu	0x70-0x40($Htbl),$Hkey	# $Hkey^6
	 vpunpckhqdq	$Ij,$Ij,$T1
	 vpxor		$Zhi,$Xhi,$Xhi
	 vpclmulqdq	\$0x00,$HK,  $T2,$Xmi
	 vpxor		$Ij,$T1,$T1
	 vpxor		$Zmi,$Xmi,$Xmi
	vxorps		$Tred,$Xi,$Xi

	  vmovdqu	0x10($inp),$Ii		# I[1]
	vpalignr	\$8,$Xi,$Xi,$Tred	# 2nd phase
	 vpclmulqdq	\$0x00,$Hkey,$Ij,$Zlo
	  vpshufb	$bswap,$Ii,$Ii
	 vpxor		$Xlo,$Zlo,$Zlo
	 vpclmulqdq	\$0x11,$Hkey,$Ij,$Zhi
	  vmovdqu	0x90-0x40($Htbl),$Hkey	# $Hkey^7
	vpclmulqdq	\$0x10,(%r10),$Xi,$Xi
	vxorps		$Xo,$Tred,$Tred
	 vpunpckhqdq	$Ii,$Ii,$T2
	 vpxor		$Xhi,$Zhi,$Zhi
	 vpclmulqdq	\$0x10,$HK,  $T1,$Zmi
	  vmovdqu	0xb0-0x40($Htbl),$HK
	 vpxor		$Ii,$T2,$T2
	 vpxor		$Xmi,$Zmi,$Zmi

	  vmovdqu	($inp),$Ij		# I[0]
	 vpclmulqdq	\$0x00,$Hkey,$Ii,$Xlo
	  vpshufb	$bswap,$Ij,$Ij
	 vpclmulqdq	\$0x11,$Hkey,$Ii,$Xhi
	  vmovdqu	0xa0-0x40($Htbl),$Hkey	# $Hkey^8
	vpxor		$Tred,$Ij,$Ij
	 vpclmulqdq	\$0x10,$HK,  $T2,$Xmi
	vpxor		$Xi,$Ij,$Ij		# accumulate $Xi

	lea		0x80($inp),$inp
	sub		\$0x80,$len
	jnc		.Loop8x_avx

	add		\$0x80,$len
	jmp		.Ltail_no_xor_avx

.align	32
.Lshort_avx:
	vmovdqu		-0x10($inp,$len),$Ii	# very last word
	lea		($inp,$len),$inp
	vmovdqu		0x00-0x40($Htbl),$Hkey	# $Hkey^1
	vmovdqu		0x20-0x40($Htbl),$HK
	vpshufb		$bswap,$Ii,$Ij

	vmovdqa		$Xlo,$Zlo		# subtle way to zero $Zlo,
	vmovdqa		$Xhi,$Zhi		# $Zhi and
	vmovdqa		$Xmi,$Zmi		# $Zmi
	sub		\$0x10,$len
	jz		.Ltail_avx

	vpunpckhqdq	$Ij,$Ij,$T1
	vpxor		$Xlo,$Zlo,$Zlo
	vpclmulqdq	\$0x00,$Hkey,$Ij,$Xlo
	vpxor		$Ij,$T1,$T1
	 vmovdqu	-0x20($inp),$Ii
	vpxor		$Xhi,$Zhi,$Zhi
	vpclmulqdq	\$0x11,$Hkey,$Ij,$Xhi
	vmovdqu		0x10-0x40($Htbl),$Hkey	# $Hkey^2
	 vpshufb	$bswap,$Ii,$Ij
	vpxor		$Xmi,$Zmi,$Zmi
	vpclmulqdq	\$0x00,$HK,$T1,$Xmi
	vpsrldq		\$8,$HK,$HK
	sub		\$0x10,$len
	jz		.Ltail_avx

	vpunpckhqdq	$Ij,$Ij,$T1
	vpxor		$Xlo,$Zlo,$Zlo
	vpclmulqdq	\$0x00,$Hkey,$Ij,$Xlo
	vpxor		$Ij,$T1,$T1
	 vmovdqu	-0x30($inp),$Ii
	vpxor		$Xhi,$Zhi,$Zhi
	vpclmulqdq	\$0x11,$Hkey,$Ij,$Xhi
	vmovdqu		0x30-0x40($Htbl),$Hkey	# $Hkey^3
	 vpshufb	$bswap,$Ii,$Ij
	vpxor		$Xmi,$Zmi,$Zmi
	vpclmulqdq	\$0x00,$HK,$T1,$Xmi
	vmovdqu		0x50-0x40($Htbl),$HK
	sub		\$0x10,$len
	jz		.Ltail_avx

	vpunpckhqdq	$Ij,$Ij,$T1
	vpxor		$Xlo,$Zlo,$Zlo
	vpclmulqdq	\$0x00,$Hkey,$Ij,$Xlo
	vpxor		$Ij,$T1,$T1
	 vmovdqu	-0x40($inp),$Ii
	vpxor		$Xhi,$Zhi,$Zhi
	vpclmulqdq	\$0x11,$Hkey,$Ij,$Xhi
	vmovdqu		0x40-0x40($Htbl),$Hkey	# $Hkey^4
	 vpshufb	$bswap,$Ii,$Ij
	vpxor		$Xmi,$Zmi,$Zmi
	vpclmulqdq	\$0x00,$HK,$T1,$Xmi
	vpsrldq		\$8,$HK,$HK
	sub		\$0x10,$len
	jz		.Ltail_avx

	vpunpckhqdq	$Ij,$Ij,$T1
	vpxor		$Xlo,$Zlo,$Zlo
	vpclmulqdq	\$0x00,$Hkey,$Ij,$Xlo
	vpxor		$Ij,$T1,$T1
	 vmovdqu	-0x50($inp),$Ii
	vpxor		$Xhi,$Zhi,$Zhi
	vpclmulqdq	\$0x11,$Hkey,$Ij,$Xhi
	vmovdqu		0x60-0x40($Htbl),$Hkey	# $Hkey^5
	 vpshufb	$bswap,$Ii,$Ij
	vpxor		$Xmi,$Zmi,$Zmi
	vpclmulqdq	\$0x00,$HK,$T1,$Xmi
	vmovdqu		0x80-0x40($Htbl),$HK
	sub		\$0x10,$len
	jz		.Ltail_avx

	vpunpckhqdq	$Ij,$Ij,$T1
	vpxor		$Xlo,$Zlo,$Zlo
	vpclmulqdq	\$0x00,$Hkey,$Ij,$Xlo
	vpxor		$Ij,$T1,$T1
	 vmovdqu	-0x60($inp),$Ii
	vpxor		$Xhi,$Zhi,$Zhi
	vpclmulqdq	\$0x11,$Hkey,$Ij,$Xhi
	vmovdqu		0x70-0x40($Htbl),$Hkey	# $Hkey^6
	 vpshufb	$bswap,$Ii,$Ij
	vpxor		$Xmi,$Zmi,$Zmi
	vpclmulqdq	\$0x00,$HK,$T1,$Xmi
	vpsrldq		\$8,$HK,$HK
	sub		\$0x10,$len
	jz		.Ltail_avx

	vpunpckhqdq	$Ij,$Ij,$T1
	vpxor		$Xlo,$Zlo,$Zlo
	vpclmulqdq	\$0x00,$Hkey,$Ij,$Xlo
	vpxor		$Ij,$T1,$T1
	 vmovdqu	-0x70($inp),$Ii
	vpxor		$Xhi,$Zhi,$Zhi
	vpclmulqdq	\$0x11,$Hkey,$Ij,$Xhi
	vmovdqu		0x90-0x40($Htbl),$Hkey	# $Hkey^7
	 vpshufb	$bswap,$Ii,$Ij
	vpxor		$Xmi,$Zmi,$Zmi
	vpclmulqdq	\$0x00,$HK,$T1,$Xmi
	vmovq		0xb8-0x40($Htbl),$HK
	sub		\$0x10,$len
	jmp		.Ltail_avx

.align	32
.Ltail_avx:
	vpxor		$Xi,$Ij,$Ij		# accumulate $Xi
.Ltail_no_xor_avx:
	vpunpckhqdq	$Ij,$Ij,$T1
	vpxor		$Xlo,$Zlo,$Zlo
	vpclmulqdq	\$0x00,$Hkey,$Ij,$Xlo
	vpxor		$Ij,$T1,$T1
	vpxor		$Xhi,$Zhi,$Zhi
	vpclmulqdq	\$0x11,$Hkey,$Ij,$Xhi
	vpxor		$Xmi,$Zmi,$Zmi
	vpclmulqdq	\$0x00,$HK,$T1,$Xmi

	vmovdqu		(%r10),$Tred

	vpxor		$Xlo,$Zlo,$Xi
	vpxor		$Xhi,$Zhi,$Xo
	vpxor		$Xmi,$Zmi,$Zmi

	vpxor		$Xi, $Zmi,$Zmi		# aggregated Karatsuba post-processing
	vpxor		$Xo, $Zmi,$Zmi
	vpslldq		\$8, $Zmi,$T2
	vpsrldq		\$8, $Zmi,$Zmi
	vpxor		$T2, $Xi, $Xi
	vpxor		$Zmi,$Xo, $Xo

	vpclmulqdq	\$0x10,$Tred,$Xi,$T2	# 1st phase
	vpalignr	\$8,$Xi,$Xi,$Xi
	vpxor		$T2,$Xi,$Xi

	vpclmulqdq	\$0x10,$Tred,$Xi,$T2	# 2nd phase
	vpalignr	\$8,$Xi,$Xi,$Xi
	vpxor		$Xo,$Xi,$Xi
	vpxor		$T2,$Xi,$Xi

	cmp		\$0,$len
	jne		.Lshort_avx

	vpshufb		$bswap,$Xi,$Xi
	vmovdqu		$Xi,($Xip)
	vzeroupper
___
$code.=<<___ if ($win64);
	movaps	(%rsp),%xmm6
	movaps	0x10(%rsp),%xmm7
	movaps	0x20(%rsp),%xmm8
	movaps	0x30(%rsp),%xmm9
	movaps	0x40(%rsp),%xmm10
	movaps	0x50(%rsp),%xmm11
	movaps	0x60(%rsp),%xmm12
	movaps	0x70(%rsp),%xmm13
	movaps	0x80(%rsp),%xmm14
	movaps	0x90(%rsp),%xmm15
	lea	0xa8(%rsp),%rsp
.LSEH_end_gcm_ghash_avx:
___
$code.=<<___;
	ret
.cfi_endproc
.size	gcm_ghash_avx,.-gcm_ghash_avx
___
} else {
$code.=<<___;
	jmp	.L_ghash_clmul
.size	gcm_ghash_avx,.-gcm_ghash_avx
___
}

$code.=<<___;
.align	64
.Lbswap_mask:
	.byte	15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
.L0x1c2_polynomial:
	.byte	1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xc2
.L7_mask:
	.long	7,0,7,0
.L7_mask_poly:
	.long	7,0,`0xE1<<1`,0
.align	64
.type	.Lrem_4bit,\@object
.Lrem_4bit:
	.long	0,`0x0000<<16`,0,`0x1C20<<16`,0,`0x3840<<16`,0,`0x2460<<16`
	.long	0,`0x7080<<16`,0,`0x6CA0<<16`,0,`0x48C0<<16`,0,`0x54E0<<16`
	.long	0,`0xE100<<16`,0,`0xFD20<<16`,0,`0xD940<<16`,0,`0xC560<<16`
	.long	0,`0x9180<<16`,0,`0x8DA0<<16`,0,`0xA9C0<<16`,0,`0xB5E0<<16`
.type	.Lrem_8bit,\@object
.Lrem_8bit:
	.value	0x0000,0x01C2,0x0384,0x0246,0x0708,0x06CA,0x048C,0x054E
	.value	0x0E10,0x0FD2,0x0D94,0x0C56,0x0918,0x08DA,0x0A9C,0x0B5E
	.value	0x1C20,0x1DE2,0x1FA4,0x1E66,0x1B28,0x1AEA,0x18AC,0x196E
	.value	0x1230,0x13F2,0x11B4,0x1076,0x1538,0x14FA,0x16BC,0x177E
	.value	0x3840,0x3982,0x3BC4,0x3A06,0x3F48,0x3E8A,0x3CCC,0x3D0E
	.value	0x3650,0x3792,0x35D4,0x3416,0x3158,0x309A,0x32DC,0x331E
	.value	0x2460,0x25A2,0x27E4,0x2626,0x2368,0x22AA,0x20EC,0x212E
	.value	0x2A70,0x2BB2,0x29F4,0x2836,0x2D78,0x2CBA,0x2EFC,0x2F3E
	.value	0x7080,0x7142,0x7304,0x72C6,0x7788,0x764A,0x740C,0x75CE
	.value	0x7E90,0x7F52,0x7D14,0x7CD6,0x7998,0x785A,0x7A1C,0x7BDE
	.value	0x6CA0,0x6D62,0x6F24,0x6EE6,0x6BA8,0x6A6A,0x682C,0x69EE
	.value	0x62B0,0x6372,0x6134,0x60F6,0x65B8,0x647A,0x663C,0x67FE
	.value	0x48C0,0x4902,0x4B44,0x4A86,0x4FC8,0x4E0A,0x4C4C,0x4D8E
	.value	0x46D0,0x4712,0x4554,0x4496,0x41D8,0x401A,0x425C,0x439E
	.value	0x54E0,0x5522,0x5764,0x56A6,0x53E8,0x522A,0x506C,0x51AE
	.value	0x5AF0,0x5B32,0x5974,0x58B6,0x5DF8,0x5C3A,0x5E7C,0x5FBE
	.value	0xE100,0xE0C2,0xE284,0xE346,0xE608,0xE7CA,0xE58C,0xE44E
	.value	0xEF10,0xEED2,0xEC94,0xED56,0xE818,0xE9DA,0xEB9C,0xEA5E
	.value	0xFD20,0xFCE2,0xFEA4,0xFF66,0xFA28,0xFBEA,0xF9AC,0xF86E
	.value	0xF330,0xF2F2,0xF0B4,0xF176,0xF438,0xF5FA,0xF7BC,0xF67E
	.value	0xD940,0xD882,0xDAC4,0xDB06,0xDE48,0xDF8A,0xDDCC,0xDC0E
	.value	0xD750,0xD692,0xD4D4,0xD516,0xD058,0xD19A,0xD3DC,0xD21E
	.value	0xC560,0xC4A2,0xC6E4,0xC726,0xC268,0xC3AA,0xC1EC,0xC02E
	.value	0xCB70,0xCAB2,0xC8F4,0xC936,0xCC78,0xCDBA,0xCFFC,0xCE3E
	.value	0x9180,0x9042,0x9204,0x93C6,0x9688,0x974A,0x950C,0x94CE
	.value	0x9F90,0x9E52,0x9C14,0x9DD6,0x9898,0x995A,0x9B1C,0x9ADE
	.value	0x8DA0,0x8C62,0x8E24,0x8FE6,0x8AA8,0x8B6A,0x892C,0x88EE
	.value	0x83B0,0x8272,0x8034,0x81F6,0x84B8,0x857A,0x873C,0x86FE
	.value	0xA9C0,0xA802,0xAA44,0xAB86,0xAEC8,0xAF0A,0xAD4C,0xAC8E
	.value	0xA7D0,0xA612,0xA454,0xA596,0xA0D8,0xA11A,0xA35C,0xA29E
	.value	0xB5E0,0xB422,0xB664,0xB7A6,0xB2E8,0xB32A,0xB16C,0xB0AE
	.value	0xBBF0,0xBA32,0xB874,0xB9B6,0xBCF8,0xBD3A,0xBF7C,0xBEBE

.asciz	"GHASH for x86_64, CRYPTOGAMS by <appro\@openssl.org>"
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

	mov	8($disp),%rsi		# disp->ImageBase
	mov	56($disp),%r11		# disp->HandlerData

	mov	0(%r11),%r10d		# HandlerData[0]
	lea	(%rsi,%r10),%r10	# prologue label
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lin_prologue

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lin_prologue

	lea	48+280(%rax),%rax	# adjust "rsp"

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

.Lin_prologue:
	mov	8(%rax),%rdi
	mov	16(%rax),%rsi
	mov	%rax,152($context)	# restore context->Rsp
	mov	%rsi,168($context)	# restore context->Rsi
	mov	%rdi,176($context)	# restore context->Rdi

	mov	40($disp),%rdi		# disp->ContextRecord
	mov	$context,%rsi		# context
	mov	\$`1232/8`,%ecx		# sizeof(CONTEXT)
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

.section	.pdata
.align	4
	.rva	.LSEH_begin_gcm_gmult_4bit
	.rva	.LSEH_end_gcm_gmult_4bit
	.rva	.LSEH_info_gcm_gmult_4bit

	.rva	.LSEH_begin_gcm_ghash_4bit
	.rva	.LSEH_end_gcm_ghash_4bit
	.rva	.LSEH_info_gcm_ghash_4bit

	.rva	.LSEH_begin_gcm_init_clmul
	.rva	.LSEH_end_gcm_init_clmul
	.rva	.LSEH_info_gcm_init_clmul

	.rva	.LSEH_begin_gcm_ghash_clmul
	.rva	.LSEH_end_gcm_ghash_clmul
	.rva	.LSEH_info_gcm_ghash_clmul
___
$code.=<<___	if ($avx);
	.rva	.LSEH_begin_gcm_init_avx
	.rva	.LSEH_end_gcm_init_avx
	.rva	.LSEH_info_gcm_init_clmul

	.rva	.LSEH_begin_gcm_ghash_avx
	.rva	.LSEH_end_gcm_ghash_avx
	.rva	.LSEH_info_gcm_ghash_clmul
___
$code.=<<___;
.section	.xdata
.align	8
.LSEH_info_gcm_gmult_4bit:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lgmult_prologue,.Lgmult_epilogue	# HandlerData
.LSEH_info_gcm_ghash_4bit:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lghash_prologue,.Lghash_epilogue	# HandlerData
.LSEH_info_gcm_init_clmul:
	.byte	0x01,0x08,0x03,0x00
	.byte	0x08,0x68,0x00,0x00	#movaps	0x00(rsp),xmm6
	.byte	0x04,0x22,0x00,0x00	#sub	rsp,0x18
.LSEH_info_gcm_ghash_clmul:
	.byte	0x01,0x33,0x16,0x00
	.byte	0x33,0xf8,0x09,0x00	#movaps 0x90(rsp),xmm15
	.byte	0x2e,0xe8,0x08,0x00	#movaps 0x80(rsp),xmm14
	.byte	0x29,0xd8,0x07,0x00	#movaps 0x70(rsp),xmm13
	.byte	0x24,0xc8,0x06,0x00	#movaps 0x60(rsp),xmm12
	.byte	0x1f,0xb8,0x05,0x00	#movaps 0x50(rsp),xmm11
	.byte	0x1a,0xa8,0x04,0x00	#movaps 0x40(rsp),xmm10
	.byte	0x15,0x98,0x03,0x00	#movaps 0x30(rsp),xmm9
	.byte	0x10,0x88,0x02,0x00	#movaps 0x20(rsp),xmm8
	.byte	0x0c,0x78,0x01,0x00	#movaps 0x10(rsp),xmm7
	.byte	0x08,0x68,0x00,0x00	#movaps 0x00(rsp),xmm6
	.byte	0x04,0x01,0x15,0x00	#sub	rsp,0xa8
___
}

$code =~ s/\`([^\`]*)\`/eval($1)/gem;

print $code;

close STDOUT;
