#! /usr/bin/env perl
# Copyright 2005-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

#
# ====================================================================
# Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
# project. Rights for redistribution and usage in source and binary
# forms are granted according to the OpenSSL license.
# ====================================================================
#
# sha256/512_block procedure for x86_64.
#
# 40% improvement over compiler-generated code on Opteron. On EM64T
# sha256 was observed to run >80% faster and sha512 - >40%. No magical
# tricks, just straight implementation... I really wonder why gcc
# [being armed with inline assembler] fails to generate as fast code.
# The only thing which is cool about this module is that it's very
# same instruction sequence used for both SHA-256 and SHA-512. In
# former case the instructions operate on 32-bit operands, while in
# latter - on 64-bit ones. All I had to do is to get one flavor right,
# the other one passed the test right away:-)
#
# sha256_block runs in ~1005 cycles on Opteron, which gives you
# asymptotic performance of 64*1000/1005=63.7MBps times CPU clock
# frequency in GHz. sha512_block runs in ~1275 cycles, which results
# in 128*1000/1275=100MBps per GHz. Is there room for improvement?
# Well, if you compare it to IA-64 implementation, which maintains
# X[16] in register bank[!], tends to 4 instructions per CPU clock
# cycle and runs in 1003 cycles, 1275 is very good result for 3-way
# issue Opteron pipeline and X[16] maintained in memory. So that *if*
# there is a way to improve it, *then* the only way would be to try to
# offload X[16] updates to SSE unit, but that would require "deeper"
# loop unroll, which in turn would naturally cause size blow-up, not
# to mention increased complexity! And once again, only *if* it's
# actually possible to noticeably improve overall ILP, instruction
# level parallelism, on a given CPU implementation in this case.
#
# Special note on Intel EM64T. While Opteron CPU exhibits perfect
# performance ratio of 1.5 between 64- and 32-bit flavors [see above],
# [currently available] EM64T CPUs apparently are far from it. On the
# contrary, 64-bit version, sha512_block, is ~30% *slower* than 32-bit
# sha256_block:-( This is presumably because 64-bit shifts/rotates
# apparently are not atomic instructions, but implemented in microcode.
#
# May 2012.
#
# Optimization including one of Pavel Semjanov's ideas, alternative
# Maj, resulted in >=5% improvement on most CPUs, +20% SHA256 and
# unfortunately -2% SHA512 on P4 [which nobody should care about
# that much].
#
# June 2012.
#
# Add SIMD code paths, see below for improvement coefficients. SSSE3
# code path was not attempted for SHA512, because improvement is not
# estimated to be high enough, noticeably less than 9%, to justify
# the effort, not on pre-AVX processors. [Obviously with exclusion
# for VIA Nano, but it has SHA512 instruction that is faster and
# should be used instead.] For reference, corresponding estimated
# upper limit for improvement for SSSE3 SHA256 is 28%. The fact that
# higher coefficients are observed on VIA Nano and Bulldozer has more
# to do with specifics of their architecture [which is topic for
# separate discussion].
#
# November 2012.
#
# Add AVX2 code path. Two consecutive input blocks are loaded to
# 256-bit %ymm registers, with data from first block to least
# significant 128-bit halves and data from second to most significant.
# The data is then processed with same SIMD instruction sequence as
# for AVX, but with %ymm as operands. Side effect is increased stack
# frame, 448 additional bytes in SHA256 and 1152 in SHA512, and 1.2KB
# code size increase.
#
# March 2014.
#
# Add support for Intel SHA Extensions.

######################################################################
# Current performance in cycles per processed byte (less is better):
#
#		SHA256	SSSE3       AVX/XOP(*)	    SHA512  AVX/XOP(*)
#
# AMD K8	14.9	-	    -		    9.57    -
# P4		17.3	-	    -		    30.8    -
# Core 2	15.6	13.8(+13%)  -		    9.97    -
# Westmere	14.8	12.3(+19%)  -		    9.58    -
# Sandy Bridge	17.4	14.2(+23%)  11.6(+50%(**))  11.2    8.10(+38%(**))
# Ivy Bridge	12.6	10.5(+20%)  10.3(+22%)	    8.17    7.22(+13%)
# Haswell	12.2	9.28(+31%)  7.80(+56%)	    7.66    5.40(+42%)
# Skylake	11.4	9.03(+26%)  7.70(+48%)      7.25    5.20(+40%)
# Bulldozer	21.1	13.6(+54%)  13.6(+54%(***)) 13.5    8.58(+57%)
# Ryzen		11.0	9.02(+22%)  2.05(+440%)     7.05    5.67(+20%)
# VIA Nano	23.0	16.5(+39%)  -		    14.7    -
# Atom		23.0	18.9(+22%)  -		    14.7    -
# Silvermont	27.4	20.6(+33%)  -               17.5    -
# Knights L	27.4	21.0(+30%)  19.6(+40%)	    17.5    12.8(+37%)
# Goldmont	18.9	14.3(+32%)  4.16(+350%)     12.0    -
#
# (*)	whichever best applicable, including SHAEXT;
# (**)	switch from ror to shrd stands for fair share of improvement;
# (***)	execution time is fully determined by remaining integer-only
#	part, body_00_15; reducing the amount of SIMD instructions
#	below certain limit makes no difference/sense; to conserve
#	space SHA256 XOP code path is therefore omitted;

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

if (!$avx && `$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|.*based on LLVM) ([3-9]\.[0-9]+)/) {
	$avx = ($2>=3.0) + ($2>3.0);
}

$shaext=1;	### set to zero if compiling for 1.0.1
$avx=1		if (!$shaext && $avx);

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

if ($output =~ /512/) {
	$func="sha512_block_data_order";
	$TABLE="K512";
	$SZ=8;
	@ROT=($A,$B,$C,$D,$E,$F,$G,$H)=("%rax","%rbx","%rcx","%rdx",
					"%r8", "%r9", "%r10","%r11");
	($T1,$a0,$a1,$a2,$a3)=("%r12","%r13","%r14","%r15","%rdi");
	@Sigma0=(28,34,39);
	@Sigma1=(14,18,41);
	@sigma0=(1,  8, 7);
	@sigma1=(19,61, 6);
	$rounds=80;
} else {
	$func="sha256_block_data_order";
	$TABLE="K256";
	$SZ=4;
	@ROT=($A,$B,$C,$D,$E,$F,$G,$H)=("%eax","%ebx","%ecx","%edx",
					"%r8d","%r9d","%r10d","%r11d");
	($T1,$a0,$a1,$a2,$a3)=("%r12d","%r13d","%r14d","%r15d","%edi");
	@Sigma0=( 2,13,22);
	@Sigma1=( 6,11,25);
	@sigma0=( 7,18, 3);
	@sigma1=(17,19,10);
	$rounds=64;
}

$ctx="%rdi";	# 1st arg, zapped by $a3
$inp="%rsi";	# 2nd arg
$Tbl="%rbp";

$_ctx="16*$SZ+0*8(%rsp)";
$_inp="16*$SZ+1*8(%rsp)";
$_end="16*$SZ+2*8(%rsp)";
$_rsp="`16*$SZ+3*8`(%rsp)";
$framesz="16*$SZ+4*8";


sub ROUND_00_15()
{ my ($i,$a,$b,$c,$d,$e,$f,$g,$h) = @_;
  my $STRIDE=$SZ;
     $STRIDE += 16 if ($i%(16/$SZ)==(16/$SZ-1));

$code.=<<___;
	ror	\$`$Sigma1[2]-$Sigma1[1]`,$a0
	mov	$f,$a2

	xor	$e,$a0
	ror	\$`$Sigma0[2]-$Sigma0[1]`,$a1
	xor	$g,$a2			# f^g

	mov	$T1,`$SZ*($i&0xf)`(%rsp)
	xor	$a,$a1
	and	$e,$a2			# (f^g)&e

	ror	\$`$Sigma1[1]-$Sigma1[0]`,$a0
	add	$h,$T1			# T1+=h
	xor	$g,$a2			# Ch(e,f,g)=((f^g)&e)^g

	ror	\$`$Sigma0[1]-$Sigma0[0]`,$a1
	xor	$e,$a0
	add	$a2,$T1			# T1+=Ch(e,f,g)

	mov	$a,$a2
	add	($Tbl),$T1		# T1+=K[round]
	xor	$a,$a1

	xor	$b,$a2			# a^b, b^c in next round
	ror	\$$Sigma1[0],$a0	# Sigma1(e)
	mov	$b,$h

	and	$a2,$a3
	ror	\$$Sigma0[0],$a1	# Sigma0(a)
	add	$a0,$T1			# T1+=Sigma1(e)

	xor	$a3,$h			# h=Maj(a,b,c)=Ch(a^b,c,b)
	add	$T1,$d			# d+=T1
	add	$T1,$h			# h+=T1

	lea	$STRIDE($Tbl),$Tbl	# round++
___
$code.=<<___ if ($i<15);
	add	$a1,$h			# h+=Sigma0(a)
___
	($a2,$a3) = ($a3,$a2);
}

sub ROUND_16_XX()
{ my ($i,$a,$b,$c,$d,$e,$f,$g,$h) = @_;

$code.=<<___;
	mov	`$SZ*(($i+1)&0xf)`(%rsp),$a0
	mov	`$SZ*(($i+14)&0xf)`(%rsp),$a2

	mov	$a0,$T1
	ror	\$`$sigma0[1]-$sigma0[0]`,$a0
	add	$a1,$a			# modulo-scheduled h+=Sigma0(a)
	mov	$a2,$a1
	ror	\$`$sigma1[1]-$sigma1[0]`,$a2

	xor	$T1,$a0
	shr	\$$sigma0[2],$T1
	ror	\$$sigma0[0],$a0
	xor	$a1,$a2
	shr	\$$sigma1[2],$a1

	ror	\$$sigma1[0],$a2
	xor	$a0,$T1			# sigma0(X[(i+1)&0xf])
	xor	$a1,$a2			# sigma1(X[(i+14)&0xf])
	add	`$SZ*(($i+9)&0xf)`(%rsp),$T1

	add	`$SZ*($i&0xf)`(%rsp),$T1
	mov	$e,$a0
	add	$a2,$T1
	mov	$a,$a1
___
	&ROUND_00_15(@_);
}

$code=<<___;
.text

.extern	OPENSSL_ia32cap_P
.globl	$func
.type	$func,\@function,3
.align	16
$func:
.cfi_startproc
___
$code.=<<___ if ($SZ==4 || $avx);
	lea	OPENSSL_ia32cap_P(%rip),%r11
	mov	0(%r11),%r9d
	mov	4(%r11),%r10d
	mov	8(%r11),%r11d
___
$code.=<<___ if ($SZ==4 && $shaext);
	test	\$`1<<29`,%r11d		# check for SHA
	jnz	_shaext_shortcut
___
$code.=<<___ if ($avx && $SZ==8);
	test	\$`1<<11`,%r10d		# check for XOP
	jnz	.Lxop_shortcut
___
$code.=<<___ if ($avx>1);
	and	\$`1<<8|1<<5|1<<3`,%r11d	# check for BMI2+AVX2+BMI1
	cmp	\$`1<<8|1<<5|1<<3`,%r11d
	je	.Lavx2_shortcut
___
$code.=<<___ if ($avx);
	and	\$`1<<30`,%r9d		# mask "Intel CPU" bit
	and	\$`1<<28|1<<9`,%r10d	# mask AVX and SSSE3 bits
	or	%r9d,%r10d
	cmp	\$`1<<28|1<<9|1<<30`,%r10d
	je	.Lavx_shortcut
___
$code.=<<___ if ($SZ==4);
	test	\$`1<<9`,%r10d
	jnz	.Lssse3_shortcut
___
$code.=<<___;
	mov	%rsp,%rax		# copy %rsp
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
	shl	\$4,%rdx		# num*16
	sub	\$$framesz,%rsp
	lea	($inp,%rdx,$SZ),%rdx	# inp+num*16*$SZ
	and	\$-64,%rsp		# align stack frame
	mov	$ctx,$_ctx		# save ctx, 1st arg
	mov	$inp,$_inp		# save inp, 2nd arh
	mov	%rdx,$_end		# save end pointer, "3rd" arg
	mov	%rax,$_rsp		# save copy of %rsp
.cfi_cfa_expression	$_rsp,deref,+8
.Lprologue:

	mov	$SZ*0($ctx),$A
	mov	$SZ*1($ctx),$B
	mov	$SZ*2($ctx),$C
	mov	$SZ*3($ctx),$D
	mov	$SZ*4($ctx),$E
	mov	$SZ*5($ctx),$F
	mov	$SZ*6($ctx),$G
	mov	$SZ*7($ctx),$H
	jmp	.Lloop

.align	16
.Lloop:
	mov	$B,$a3
	lea	$TABLE(%rip),$Tbl
	xor	$C,$a3			# magic
___
	for($i=0;$i<16;$i++) {
		$code.="	mov	$SZ*$i($inp),$T1\n";
		$code.="	mov	@ROT[4],$a0\n";
		$code.="	mov	@ROT[0],$a1\n";
		$code.="	bswap	$T1\n";
		&ROUND_00_15($i,@ROT);
		unshift(@ROT,pop(@ROT));
	}
$code.=<<___;
	jmp	.Lrounds_16_xx
.align	16
.Lrounds_16_xx:
___
	for(;$i<32;$i++) {
		&ROUND_16_XX($i,@ROT);
		unshift(@ROT,pop(@ROT));
	}

$code.=<<___;
	cmpb	\$0,`$SZ-1`($Tbl)
	jnz	.Lrounds_16_xx

	mov	$_ctx,$ctx
	add	$a1,$A			# modulo-scheduled h+=Sigma0(a)
	lea	16*$SZ($inp),$inp

	add	$SZ*0($ctx),$A
	add	$SZ*1($ctx),$B
	add	$SZ*2($ctx),$C
	add	$SZ*3($ctx),$D
	add	$SZ*4($ctx),$E
	add	$SZ*5($ctx),$F
	add	$SZ*6($ctx),$G
	add	$SZ*7($ctx),$H

	cmp	$_end,$inp

	mov	$A,$SZ*0($ctx)
	mov	$B,$SZ*1($ctx)
	mov	$C,$SZ*2($ctx)
	mov	$D,$SZ*3($ctx)
	mov	$E,$SZ*4($ctx)
	mov	$F,$SZ*5($ctx)
	mov	$G,$SZ*6($ctx)
	mov	$H,$SZ*7($ctx)
	jb	.Lloop

	mov	$_rsp,%rsi
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
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue:
	ret
.cfi_endproc
.size	$func,.-$func
___

if ($SZ==4) {
$code.=<<___;
.align	64
.type	$TABLE,\@object
$TABLE:
	.long	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	.long	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5
	.long	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	.long	0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5
	.long	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	.long	0xd807aa98,0x12835b01,0x243185be,0x550c7dc3
	.long	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	.long	0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174
	.long	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	.long	0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc
	.long	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	.long	0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da
	.long	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	.long	0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7
	.long	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	.long	0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967
	.long	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	.long	0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13
	.long	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	.long	0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85
	.long	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	.long	0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3
	.long	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	.long	0xd192e819,0xd6990624,0xf40e3585,0x106aa070
	.long	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	.long	0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5
	.long	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	.long	0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3
	.long	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	.long	0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208
	.long	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
	.long	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2

	.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f
	.long	0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f
	.long	0x03020100,0x0b0a0908,0xffffffff,0xffffffff
	.long	0x03020100,0x0b0a0908,0xffffffff,0xffffffff
	.long	0xffffffff,0xffffffff,0x03020100,0x0b0a0908
	.long	0xffffffff,0xffffffff,0x03020100,0x0b0a0908
	.asciz	"SHA256 block transform for x86_64, CRYPTOGAMS by <appro\@openssl.org>"
___
} else {
$code.=<<___;
.align	64
.type	$TABLE,\@object
$TABLE:
	.quad	0x428a2f98d728ae22,0x7137449123ef65cd
	.quad	0x428a2f98d728ae22,0x7137449123ef65cd
	.quad	0xb5c0fbcfec4d3b2f,0xe9b5dba58189dbbc
	.quad	0xb5c0fbcfec4d3b2f,0xe9b5dba58189dbbc
	.quad	0x3956c25bf348b538,0x59f111f1b605d019
	.quad	0x3956c25bf348b538,0x59f111f1b605d019
	.quad	0x923f82a4af194f9b,0xab1c5ed5da6d8118
	.quad	0x923f82a4af194f9b,0xab1c5ed5da6d8118
	.quad	0xd807aa98a3030242,0x12835b0145706fbe
	.quad	0xd807aa98a3030242,0x12835b0145706fbe
	.quad	0x243185be4ee4b28c,0x550c7dc3d5ffb4e2
	.quad	0x243185be4ee4b28c,0x550c7dc3d5ffb4e2
	.quad	0x72be5d74f27b896f,0x80deb1fe3b1696b1
	.quad	0x72be5d74f27b896f,0x80deb1fe3b1696b1
	.quad	0x9bdc06a725c71235,0xc19bf174cf692694
	.quad	0x9bdc06a725c71235,0xc19bf174cf692694
	.quad	0xe49b69c19ef14ad2,0xefbe4786384f25e3
	.quad	0xe49b69c19ef14ad2,0xefbe4786384f25e3
	.quad	0x0fc19dc68b8cd5b5,0x240ca1cc77ac9c65
	.quad	0x0fc19dc68b8cd5b5,0x240ca1cc77ac9c65
	.quad	0x2de92c6f592b0275,0x4a7484aa6ea6e483
	.quad	0x2de92c6f592b0275,0x4a7484aa6ea6e483
	.quad	0x5cb0a9dcbd41fbd4,0x76f988da831153b5
	.quad	0x5cb0a9dcbd41fbd4,0x76f988da831153b5
	.quad	0x983e5152ee66dfab,0xa831c66d2db43210
	.quad	0x983e5152ee66dfab,0xa831c66d2db43210
	.quad	0xb00327c898fb213f,0xbf597fc7beef0ee4
	.quad	0xb00327c898fb213f,0xbf597fc7beef0ee4
	.quad	0xc6e00bf33da88fc2,0xd5a79147930aa725
	.quad	0xc6e00bf33da88fc2,0xd5a79147930aa725
	.quad	0x06ca6351e003826f,0x142929670a0e6e70
	.quad	0x06ca6351e003826f,0x142929670a0e6e70
	.quad	0x27b70a8546d22ffc,0x2e1b21385c26c926
	.quad	0x27b70a8546d22ffc,0x2e1b21385c26c926
	.quad	0x4d2c6dfc5ac42aed,0x53380d139d95b3df
	.quad	0x4d2c6dfc5ac42aed,0x53380d139d95b3df
	.quad	0x650a73548baf63de,0x766a0abb3c77b2a8
	.quad	0x650a73548baf63de,0x766a0abb3c77b2a8
	.quad	0x81c2c92e47edaee6,0x92722c851482353b
	.quad	0x81c2c92e47edaee6,0x92722c851482353b
	.quad	0xa2bfe8a14cf10364,0xa81a664bbc423001
	.quad	0xa2bfe8a14cf10364,0xa81a664bbc423001
	.quad	0xc24b8b70d0f89791,0xc76c51a30654be30
	.quad	0xc24b8b70d0f89791,0xc76c51a30654be30
	.quad	0xd192e819d6ef5218,0xd69906245565a910
	.quad	0xd192e819d6ef5218,0xd69906245565a910
	.quad	0xf40e35855771202a,0x106aa07032bbd1b8
	.quad	0xf40e35855771202a,0x106aa07032bbd1b8
	.quad	0x19a4c116b8d2d0c8,0x1e376c085141ab53
	.quad	0x19a4c116b8d2d0c8,0x1e376c085141ab53
	.quad	0x2748774cdf8eeb99,0x34b0bcb5e19b48a8
	.quad	0x2748774cdf8eeb99,0x34b0bcb5e19b48a8
	.quad	0x391c0cb3c5c95a63,0x4ed8aa4ae3418acb
	.quad	0x391c0cb3c5c95a63,0x4ed8aa4ae3418acb
	.quad	0x5b9cca4f7763e373,0x682e6ff3d6b2b8a3
	.quad	0x5b9cca4f7763e373,0x682e6ff3d6b2b8a3
	.quad	0x748f82ee5defb2fc,0x78a5636f43172f60
	.quad	0x748f82ee5defb2fc,0x78a5636f43172f60
	.quad	0x84c87814a1f0ab72,0x8cc702081a6439ec
	.quad	0x84c87814a1f0ab72,0x8cc702081a6439ec
	.quad	0x90befffa23631e28,0xa4506cebde82bde9
	.quad	0x90befffa23631e28,0xa4506cebde82bde9
	.quad	0xbef9a3f7b2c67915,0xc67178f2e372532b
	.quad	0xbef9a3f7b2c67915,0xc67178f2e372532b
	.quad	0xca273eceea26619c,0xd186b8c721c0c207
	.quad	0xca273eceea26619c,0xd186b8c721c0c207
	.quad	0xeada7dd6cde0eb1e,0xf57d4f7fee6ed178
	.quad	0xeada7dd6cde0eb1e,0xf57d4f7fee6ed178
	.quad	0x06f067aa72176fba,0x0a637dc5a2c898a6
	.quad	0x06f067aa72176fba,0x0a637dc5a2c898a6
	.quad	0x113f9804bef90dae,0x1b710b35131c471b
	.quad	0x113f9804bef90dae,0x1b710b35131c471b
	.quad	0x28db77f523047d84,0x32caab7b40c72493
	.quad	0x28db77f523047d84,0x32caab7b40c72493
	.quad	0x3c9ebe0a15c9bebc,0x431d67c49c100d4c
	.quad	0x3c9ebe0a15c9bebc,0x431d67c49c100d4c
	.quad	0x4cc5d4becb3e42b6,0x597f299cfc657e2a
	.quad	0x4cc5d4becb3e42b6,0x597f299cfc657e2a
	.quad	0x5fcb6fab3ad6faec,0x6c44198c4a475817
	.quad	0x5fcb6fab3ad6faec,0x6c44198c4a475817

	.quad	0x0001020304050607,0x08090a0b0c0d0e0f
	.quad	0x0001020304050607,0x08090a0b0c0d0e0f
	.asciz	"SHA512 block transform for x86_64, CRYPTOGAMS by <appro\@openssl.org>"
___
}

######################################################################
# SIMD code paths
#
if ($SZ==4 && $shaext) {{{
######################################################################
# Intel SHA Extensions implementation of SHA256 update function.
#
my ($ctx,$inp,$num,$Tbl)=("%rdi","%rsi","%rdx","%rcx");

my ($Wi,$ABEF,$CDGH,$TMP,$BSWAP,$ABEF_SAVE,$CDGH_SAVE)=map("%xmm$_",(0..2,7..10));
my @MSG=map("%xmm$_",(3..6));

$code.=<<___;
.type	sha256_block_data_order_shaext,\@function,3
.align	64
sha256_block_data_order_shaext:
_shaext_shortcut:
___
$code.=<<___ if ($win64);
	lea	`-8-5*16`(%rsp),%rsp
	movaps	%xmm6,-8-5*16(%rax)
	movaps	%xmm7,-8-4*16(%rax)
	movaps	%xmm8,-8-3*16(%rax)
	movaps	%xmm9,-8-2*16(%rax)
	movaps	%xmm10,-8-1*16(%rax)
.Lprologue_shaext:
___
$code.=<<___;
	lea		K256+0x80(%rip),$Tbl
	movdqu		($ctx),$ABEF		# DCBA
	movdqu		16($ctx),$CDGH		# HGFE
	movdqa		0x200-0x80($Tbl),$TMP	# byte swap mask

	pshufd		\$0x1b,$ABEF,$Wi	# ABCD
	pshufd		\$0xb1,$ABEF,$ABEF	# CDAB
	pshufd		\$0x1b,$CDGH,$CDGH	# EFGH
	movdqa		$TMP,$BSWAP		# offload
	palignr		\$8,$CDGH,$ABEF		# ABEF
	punpcklqdq	$Wi,$CDGH		# CDGH
	jmp		.Loop_shaext

.align	16
.Loop_shaext:
	movdqu		($inp),@MSG[0]
	movdqu		0x10($inp),@MSG[1]
	movdqu		0x20($inp),@MSG[2]
	pshufb		$TMP,@MSG[0]
	movdqu		0x30($inp),@MSG[3]

	movdqa		0*32-0x80($Tbl),$Wi
	paddd		@MSG[0],$Wi
	pshufb		$TMP,@MSG[1]
	movdqa		$CDGH,$CDGH_SAVE	# offload
	sha256rnds2	$ABEF,$CDGH		# 0-3
	pshufd		\$0x0e,$Wi,$Wi
	nop
	movdqa		$ABEF,$ABEF_SAVE	# offload
	sha256rnds2	$CDGH,$ABEF

	movdqa		1*32-0x80($Tbl),$Wi
	paddd		@MSG[1],$Wi
	pshufb		$TMP,@MSG[2]
	sha256rnds2	$ABEF,$CDGH		# 4-7
	pshufd		\$0x0e,$Wi,$Wi
	lea		0x40($inp),$inp
	sha256msg1	@MSG[1],@MSG[0]
	sha256rnds2	$CDGH,$ABEF

	movdqa		2*32-0x80($Tbl),$Wi
	paddd		@MSG[2],$Wi
	pshufb		$TMP,@MSG[3]
	sha256rnds2	$ABEF,$CDGH		# 8-11
	pshufd		\$0x0e,$Wi,$Wi
	movdqa		@MSG[3],$TMP
	palignr		\$4,@MSG[2],$TMP
	nop
	paddd		$TMP,@MSG[0]
	sha256msg1	@MSG[2],@MSG[1]
	sha256rnds2	$CDGH,$ABEF

	movdqa		3*32-0x80($Tbl),$Wi
	paddd		@MSG[3],$Wi
	sha256msg2	@MSG[3],@MSG[0]
	sha256rnds2	$ABEF,$CDGH		# 12-15
	pshufd		\$0x0e,$Wi,$Wi
	movdqa		@MSG[0],$TMP
	palignr		\$4,@MSG[3],$TMP
	nop
	paddd		$TMP,@MSG[1]
	sha256msg1	@MSG[3],@MSG[2]
	sha256rnds2	$CDGH,$ABEF
___
for($i=4;$i<16-3;$i++) {
$code.=<<___;
	movdqa		$i*32-0x80($Tbl),$Wi
	paddd		@MSG[0],$Wi
	sha256msg2	@MSG[0],@MSG[1]
	sha256rnds2	$ABEF,$CDGH		# 16-19...
	pshufd		\$0x0e,$Wi,$Wi
	movdqa		@MSG[1],$TMP
	palignr		\$4,@MSG[0],$TMP
	nop
	paddd		$TMP,@MSG[2]
	sha256msg1	@MSG[0],@MSG[3]
	sha256rnds2	$CDGH,$ABEF
___
	push(@MSG,shift(@MSG));
}
$code.=<<___;
	movdqa		13*32-0x80($Tbl),$Wi
	paddd		@MSG[0],$Wi
	sha256msg2	@MSG[0],@MSG[1]
	sha256rnds2	$ABEF,$CDGH		# 52-55
	pshufd		\$0x0e,$Wi,$Wi
	movdqa		@MSG[1],$TMP
	palignr		\$4,@MSG[0],$TMP
	sha256rnds2	$CDGH,$ABEF
	paddd		$TMP,@MSG[2]

	movdqa		14*32-0x80($Tbl),$Wi
	paddd		@MSG[1],$Wi
	sha256rnds2	$ABEF,$CDGH		# 56-59
	pshufd		\$0x0e,$Wi,$Wi
	sha256msg2	@MSG[1],@MSG[2]
	movdqa		$BSWAP,$TMP
	sha256rnds2	$CDGH,$ABEF

	movdqa		15*32-0x80($Tbl),$Wi
	paddd		@MSG[2],$Wi
	nop
	sha256rnds2	$ABEF,$CDGH		# 60-63
	pshufd		\$0x0e,$Wi,$Wi
	dec		$num
	nop
	sha256rnds2	$CDGH,$ABEF

	paddd		$CDGH_SAVE,$CDGH
	paddd		$ABEF_SAVE,$ABEF
	jnz		.Loop_shaext

	pshufd		\$0xb1,$CDGH,$CDGH	# DCHG
	pshufd		\$0x1b,$ABEF,$TMP	# FEBA
	pshufd		\$0xb1,$ABEF,$ABEF	# BAFE
	punpckhqdq	$CDGH,$ABEF		# DCBA
	palignr		\$8,$TMP,$CDGH		# HGFE

	movdqu	$ABEF,($ctx)
	movdqu	$CDGH,16($ctx)
___
$code.=<<___ if ($win64);
	movaps	-8-5*16(%rax),%xmm6
	movaps	-8-4*16(%rax),%xmm7
	movaps	-8-3*16(%rax),%xmm8
	movaps	-8-2*16(%rax),%xmm9
	movaps	-8-1*16(%rax),%xmm10
	mov	%rax,%rsp
.Lepilogue_shaext:
___
$code.=<<___;
	ret
.size	sha256_block_data_order_shaext,.-sha256_block_data_order_shaext
___
}}}
{{{

my $a4=$T1;
my ($a,$b,$c,$d,$e,$f,$g,$h);

sub AUTOLOAD()		# thunk [simplified] 32-bit style perlasm
{ my $opcode = $AUTOLOAD; $opcode =~ s/.*:://;
  my $arg = pop;
    $arg = "\$$arg" if ($arg*1 eq $arg);
    $code .= "\t$opcode\t".join(',',$arg,reverse @_)."\n";
}

sub body_00_15 () {
	(
	'($a,$b,$c,$d,$e,$f,$g,$h)=@ROT;'.

	'&ror	($a0,$Sigma1[2]-$Sigma1[1])',
	'&mov	($a,$a1)',
	'&mov	($a4,$f)',

	'&ror	($a1,$Sigma0[2]-$Sigma0[1])',
	'&xor	($a0,$e)',
	'&xor	($a4,$g)',			# f^g

	'&ror	($a0,$Sigma1[1]-$Sigma1[0])',
	'&xor	($a1,$a)',
	'&and	($a4,$e)',			# (f^g)&e

	'&xor	($a0,$e)',
	'&add	($h,$SZ*($i&15)."(%rsp)")',	# h+=X[i]+K[i]
	'&mov	($a2,$a)',

	'&xor	($a4,$g)',			# Ch(e,f,g)=((f^g)&e)^g
	'&ror	($a1,$Sigma0[1]-$Sigma0[0])',
	'&xor	($a2,$b)',			# a^b, b^c in next round

	'&add	($h,$a4)',			# h+=Ch(e,f,g)
	'&ror	($a0,$Sigma1[0])',		# Sigma1(e)
	'&and	($a3,$a2)',			# (b^c)&(a^b)

	'&xor	($a1,$a)',
	'&add	($h,$a0)',			# h+=Sigma1(e)
	'&xor	($a3,$b)',			# Maj(a,b,c)=Ch(a^b,c,b)

	'&ror	($a1,$Sigma0[0])',		# Sigma0(a)
	'&add	($d,$h)',			# d+=h
	'&add	($h,$a3)',			# h+=Maj(a,b,c)

	'&mov	($a0,$d)',
	'&add	($a1,$h);'.			# h+=Sigma0(a)
	'($a2,$a3) = ($a3,$a2); unshift(@ROT,pop(@ROT)); $i++;'
	);
}

######################################################################
# SSSE3 code path
#
if ($SZ==4) {	# SHA256 only
my @X = map("%xmm$_",(0..3));
my ($t0,$t1,$t2,$t3, $t4,$t5) = map("%xmm$_",(4..9));

$code.=<<___;
.type	${func}_ssse3,\@function,3
.align	64
${func}_ssse3:
.cfi_startproc
.Lssse3_shortcut:
	mov	%rsp,%rax		# copy %rsp
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
	shl	\$4,%rdx		# num*16
	sub	\$`$framesz+$win64*16*4`,%rsp
	lea	($inp,%rdx,$SZ),%rdx	# inp+num*16*$SZ
	and	\$-64,%rsp		# align stack frame
	mov	$ctx,$_ctx		# save ctx, 1st arg
	mov	$inp,$_inp		# save inp, 2nd arh
	mov	%rdx,$_end		# save end pointer, "3rd" arg
	mov	%rax,$_rsp		# save copy of %rsp
.cfi_cfa_expression	$_rsp,deref,+8
___
$code.=<<___ if ($win64);
	movaps	%xmm6,16*$SZ+32(%rsp)
	movaps	%xmm7,16*$SZ+48(%rsp)
	movaps	%xmm8,16*$SZ+64(%rsp)
	movaps	%xmm9,16*$SZ+80(%rsp)
___
$code.=<<___;
.Lprologue_ssse3:

	mov	$SZ*0($ctx),$A
	mov	$SZ*1($ctx),$B
	mov	$SZ*2($ctx),$C
	mov	$SZ*3($ctx),$D
	mov	$SZ*4($ctx),$E
	mov	$SZ*5($ctx),$F
	mov	$SZ*6($ctx),$G
	mov	$SZ*7($ctx),$H
___

$code.=<<___;
	#movdqa	$TABLE+`$SZ*2*$rounds`+32(%rip),$t4
	#movdqa	$TABLE+`$SZ*2*$rounds`+64(%rip),$t5
	jmp	.Lloop_ssse3
.align	16
.Lloop_ssse3:
	movdqa	$TABLE+`$SZ*2*$rounds`(%rip),$t3
	movdqu	0x00($inp),@X[0]
	movdqu	0x10($inp),@X[1]
	movdqu	0x20($inp),@X[2]
	pshufb	$t3,@X[0]
	movdqu	0x30($inp),@X[3]
	lea	$TABLE(%rip),$Tbl
	pshufb	$t3,@X[1]
	movdqa	0x00($Tbl),$t0
	movdqa	0x20($Tbl),$t1
	pshufb	$t3,@X[2]
	paddd	@X[0],$t0
	movdqa	0x40($Tbl),$t2
	pshufb	$t3,@X[3]
	movdqa	0x60($Tbl),$t3
	paddd	@X[1],$t1
	paddd	@X[2],$t2
	paddd	@X[3],$t3
	movdqa	$t0,0x00(%rsp)
	mov	$A,$a1
	movdqa	$t1,0x10(%rsp)
	mov	$B,$a3
	movdqa	$t2,0x20(%rsp)
	xor	$C,$a3			# magic
	movdqa	$t3,0x30(%rsp)
	mov	$E,$a0
	jmp	.Lssse3_00_47

.align	16
.Lssse3_00_47:
	sub	\$`-16*2*$SZ`,$Tbl	# size optimization
___
sub Xupdate_256_SSSE3 () {
	(
	'&movdqa	($t0,@X[1]);',
	'&movdqa	($t3,@X[3])',
	'&palignr	($t0,@X[0],$SZ)',	# X[1..4]
	 '&palignr	($t3,@X[2],$SZ);',	# X[9..12]
	'&movdqa	($t1,$t0)',
	'&movdqa	($t2,$t0);',
	'&psrld		($t0,$sigma0[2])',
	 '&paddd	(@X[0],$t3);',		# X[0..3] += X[9..12]
	'&psrld		($t2,$sigma0[0])',
	 '&pshufd	($t3,@X[3],0b11111010)',# X[14..15]
	'&pslld		($t1,8*$SZ-$sigma0[1]);'.
	'&pxor		($t0,$t2)',
	'&psrld		($t2,$sigma0[1]-$sigma0[0]);'.
	'&pxor		($t0,$t1)',
	'&pslld		($t1,$sigma0[1]-$sigma0[0]);'.
	'&pxor		($t0,$t2);',
	 '&movdqa	($t2,$t3)',
	'&pxor		($t0,$t1);',		# sigma0(X[1..4])
	 '&psrld	($t3,$sigma1[2])',
	'&paddd		(@X[0],$t0);',		# X[0..3] += sigma0(X[1..4])
	 '&psrlq	($t2,$sigma1[0])',
	 '&pxor		($t3,$t2);',
	 '&psrlq	($t2,$sigma1[1]-$sigma1[0])',
	 '&pxor		($t3,$t2)',
	 '&pshufb	($t3,$t4)',		# sigma1(X[14..15])
	'&paddd		(@X[0],$t3)',		# X[0..1] += sigma1(X[14..15])
	 '&pshufd	($t3,@X[0],0b01010000)',# X[16..17]
	 '&movdqa	($t2,$t3);',
	 '&psrld	($t3,$sigma1[2])',
	 '&psrlq	($t2,$sigma1[0])',
	 '&pxor		($t3,$t2);',
	 '&psrlq	($t2,$sigma1[1]-$sigma1[0])',
	 '&pxor		($t3,$t2);',
	'&movdqa	($t2,16*2*$j."($Tbl)")',
	 '&pshufb	($t3,$t5)',
	'&paddd		(@X[0],$t3)'		# X[2..3] += sigma1(X[16..17])
	);
}

sub SSSE3_256_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body,&$body,&$body);	# 104 instructions

    if (0) {
	foreach (Xupdate_256_SSSE3()) {		# 36 instructions
	    eval;
	    eval(shift(@insns));
	    eval(shift(@insns));
	    eval(shift(@insns));
	}
    } else {			# squeeze extra 4% on Westmere and 19% on Atom
	  eval(shift(@insns));	#@
	&movdqa		($t0,@X[1]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&movdqa		($t3,@X[3]);
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	&palignr	($t0,@X[0],$SZ);	# X[1..4]
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &palignr	($t3,@X[2],$SZ);	# X[9..12]
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	&movdqa		($t1,$t0);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&movdqa		($t2,$t0);
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	&psrld		($t0,$sigma0[2]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &paddd		(@X[0],$t3);		# X[0..3] += X[9..12]
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	&psrld		($t2,$sigma0[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &pshufd	($t3,@X[3],0b11111010);	# X[4..15]
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	&pslld		($t1,8*$SZ-$sigma0[1]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&pxor		($t0,$t2);
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	&psrld		($t2,$sigma0[1]-$sigma0[0]);
	  eval(shift(@insns));
	&pxor		($t0,$t1);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&pslld		($t1,$sigma0[1]-$sigma0[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&pxor		($t0,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	 &movdqa	($t2,$t3);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&pxor		($t0,$t1);		# sigma0(X[1..4])
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &psrld		($t3,$sigma1[2]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&paddd		(@X[0],$t0);		# X[0..3] += sigma0(X[1..4])
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	 &psrlq		($t2,$sigma1[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &pxor		($t3,$t2);
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	 &psrlq		($t2,$sigma1[1]-$sigma1[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &pxor		($t3,$t2);
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	  eval(shift(@insns));
	 #&pshufb	($t3,$t4);		# sigma1(X[14..15])
	 &pshufd	($t3,$t3,0b10000000);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &psrldq	($t3,8);
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	&paddd		(@X[0],$t3);		# X[0..1] += sigma1(X[14..15])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &pshufd	($t3,@X[0],0b01010000);	# X[16..17]
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	 &movdqa	($t2,$t3);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &psrld		($t3,$sigma1[2]);
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	 &psrlq		($t2,$sigma1[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &pxor		($t3,$t2);
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	 &psrlq		($t2,$sigma1[1]-$sigma1[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &pxor		($t3,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));	#@
	 #&pshufb	($t3,$t5);
	 &pshufd	($t3,$t3,0b00001000);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&movdqa		($t2,16*2*$j."($Tbl)");
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	 &pslldq	($t3,8);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&paddd		(@X[0],$t3);		# X[2..3] += sigma1(X[16..17])
	  eval(shift(@insns));	#@
	  eval(shift(@insns));
	  eval(shift(@insns));
    }
	&paddd		($t2,@X[0]);
	  foreach (@insns) { eval; }		# remaining instructions
	&movdqa		(16*$j."(%rsp)",$t2);
}

    for ($i=0,$j=0; $j<4; $j++) {
	&SSSE3_256_00_47($j,\&body_00_15,@X);
	push(@X,shift(@X));			# rotate(@X)
    }
	&cmpb	($SZ-1+16*2*$SZ."($Tbl)",0);
	&jne	(".Lssse3_00_47");

    for ($i=0; $i<16; ) {
	foreach(body_00_15()) { eval; }
    }
$code.=<<___;
	mov	$_ctx,$ctx
	mov	$a1,$A

	add	$SZ*0($ctx),$A
	lea	16*$SZ($inp),$inp
	add	$SZ*1($ctx),$B
	add	$SZ*2($ctx),$C
	add	$SZ*3($ctx),$D
	add	$SZ*4($ctx),$E
	add	$SZ*5($ctx),$F
	add	$SZ*6($ctx),$G
	add	$SZ*7($ctx),$H

	cmp	$_end,$inp

	mov	$A,$SZ*0($ctx)
	mov	$B,$SZ*1($ctx)
	mov	$C,$SZ*2($ctx)
	mov	$D,$SZ*3($ctx)
	mov	$E,$SZ*4($ctx)
	mov	$F,$SZ*5($ctx)
	mov	$G,$SZ*6($ctx)
	mov	$H,$SZ*7($ctx)
	jb	.Lloop_ssse3

	mov	$_rsp,%rsi
.cfi_def_cfa	%rsi,8
___
$code.=<<___ if ($win64);
	movaps	16*$SZ+32(%rsp),%xmm6
	movaps	16*$SZ+48(%rsp),%xmm7
	movaps	16*$SZ+64(%rsp),%xmm8
	movaps	16*$SZ+80(%rsp),%xmm9
___
$code.=<<___;
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
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_ssse3:
	ret
.cfi_endproc
.size	${func}_ssse3,.-${func}_ssse3
___
}

if ($avx) {{
######################################################################
# XOP code path
#
if ($SZ==8) {	# SHA512 only
$code.=<<___;
.type	${func}_xop,\@function,3
.align	64
${func}_xop:
.cfi_startproc
.Lxop_shortcut:
	mov	%rsp,%rax		# copy %rsp
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
	shl	\$4,%rdx		# num*16
	sub	\$`$framesz+$win64*16*($SZ==4?4:6)`,%rsp
	lea	($inp,%rdx,$SZ),%rdx	# inp+num*16*$SZ
	and	\$-64,%rsp		# align stack frame
	mov	$ctx,$_ctx		# save ctx, 1st arg
	mov	$inp,$_inp		# save inp, 2nd arh
	mov	%rdx,$_end		# save end pointer, "3rd" arg
	mov	%rax,$_rsp		# save copy of %rsp
.cfi_cfa_expression	$_rsp,deref,+8
___
$code.=<<___ if ($win64);
	movaps	%xmm6,16*$SZ+32(%rsp)
	movaps	%xmm7,16*$SZ+48(%rsp)
	movaps	%xmm8,16*$SZ+64(%rsp)
	movaps	%xmm9,16*$SZ+80(%rsp)
___
$code.=<<___ if ($win64 && $SZ>4);
	movaps	%xmm10,16*$SZ+96(%rsp)
	movaps	%xmm11,16*$SZ+112(%rsp)
___
$code.=<<___;
.Lprologue_xop:

	vzeroupper
	mov	$SZ*0($ctx),$A
	mov	$SZ*1($ctx),$B
	mov	$SZ*2($ctx),$C
	mov	$SZ*3($ctx),$D
	mov	$SZ*4($ctx),$E
	mov	$SZ*5($ctx),$F
	mov	$SZ*6($ctx),$G
	mov	$SZ*7($ctx),$H
	jmp	.Lloop_xop
___
					if ($SZ==4) {	# SHA256
    my @X = map("%xmm$_",(0..3));
    my ($t0,$t1,$t2,$t3) = map("%xmm$_",(4..7));

$code.=<<___;
.align	16
.Lloop_xop:
	vmovdqa	$TABLE+`$SZ*2*$rounds`(%rip),$t3
	vmovdqu	0x00($inp),@X[0]
	vmovdqu	0x10($inp),@X[1]
	vmovdqu	0x20($inp),@X[2]
	vmovdqu	0x30($inp),@X[3]
	vpshufb	$t3,@X[0],@X[0]
	lea	$TABLE(%rip),$Tbl
	vpshufb	$t3,@X[1],@X[1]
	vpshufb	$t3,@X[2],@X[2]
	vpaddd	0x00($Tbl),@X[0],$t0
	vpshufb	$t3,@X[3],@X[3]
	vpaddd	0x20($Tbl),@X[1],$t1
	vpaddd	0x40($Tbl),@X[2],$t2
	vpaddd	0x60($Tbl),@X[3],$t3
	vmovdqa	$t0,0x00(%rsp)
	mov	$A,$a1
	vmovdqa	$t1,0x10(%rsp)
	mov	$B,$a3
	vmovdqa	$t2,0x20(%rsp)
	xor	$C,$a3			# magic
	vmovdqa	$t3,0x30(%rsp)
	mov	$E,$a0
	jmp	.Lxop_00_47

.align	16
.Lxop_00_47:
	sub	\$`-16*2*$SZ`,$Tbl	# size optimization
___
sub XOP_256_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body,&$body,&$body);	# 104 instructions

	&vpalignr	($t0,@X[1],@X[0],$SZ);	# X[1..4]
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpalignr	($t3,@X[3],@X[2],$SZ);	# X[9..12]
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vprotd		($t1,$t0,8*$SZ-$sigma0[1]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpsrld		($t0,$t0,$sigma0[2]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpaddd	(@X[0],@X[0],$t3);	# X[0..3] += X[9..12]
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vprotd		($t2,$t1,$sigma0[1]-$sigma0[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpxor		($t0,$t0,$t1);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vprotd	($t3,@X[3],8*$SZ-$sigma1[1]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpxor		($t0,$t0,$t2);		# sigma0(X[1..4])
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpsrld	($t2,@X[3],$sigma1[2]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpaddd		(@X[0],@X[0],$t0);	# X[0..3] += sigma0(X[1..4])
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vprotd	($t1,$t3,$sigma1[1]-$sigma1[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpxor		($t3,$t3,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpxor		($t3,$t3,$t1);		# sigma1(X[14..15])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpsrldq	($t3,$t3,8);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpaddd		(@X[0],@X[0],$t3);	# X[0..1] += sigma1(X[14..15])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vprotd	($t3,@X[0],8*$SZ-$sigma1[1]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpsrld	($t2,@X[0],$sigma1[2]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vprotd	($t1,$t3,$sigma1[1]-$sigma1[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpxor		($t3,$t3,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpxor		($t3,$t3,$t1);		# sigma1(X[16..17])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpslldq	($t3,$t3,8);		# 22 instructions
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpaddd		(@X[0],@X[0],$t3);	# X[2..3] += sigma1(X[16..17])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpaddd		($t2,@X[0],16*2*$j."($Tbl)");
	  foreach (@insns) { eval; }		# remaining instructions
	&vmovdqa	(16*$j."(%rsp)",$t2);
}

    for ($i=0,$j=0; $j<4; $j++) {
	&XOP_256_00_47($j,\&body_00_15,@X);
	push(@X,shift(@X));			# rotate(@X)
    }
	&cmpb	($SZ-1+16*2*$SZ."($Tbl)",0);
	&jne	(".Lxop_00_47");

    for ($i=0; $i<16; ) {
	foreach(body_00_15()) { eval; }
    }

					} else {	# SHA512
    my @X = map("%xmm$_",(0..7));
    my ($t0,$t1,$t2,$t3) = map("%xmm$_",(8..11));

$code.=<<___;
.align	16
.Lloop_xop:
	vmovdqa	$TABLE+`$SZ*2*$rounds`(%rip),$t3
	vmovdqu	0x00($inp),@X[0]
	lea	$TABLE+0x80(%rip),$Tbl	# size optimization
	vmovdqu	0x10($inp),@X[1]
	vmovdqu	0x20($inp),@X[2]
	vpshufb	$t3,@X[0],@X[0]
	vmovdqu	0x30($inp),@X[3]
	vpshufb	$t3,@X[1],@X[1]
	vmovdqu	0x40($inp),@X[4]
	vpshufb	$t3,@X[2],@X[2]
	vmovdqu	0x50($inp),@X[5]
	vpshufb	$t3,@X[3],@X[3]
	vmovdqu	0x60($inp),@X[6]
	vpshufb	$t3,@X[4],@X[4]
	vmovdqu	0x70($inp),@X[7]
	vpshufb	$t3,@X[5],@X[5]
	vpaddq	-0x80($Tbl),@X[0],$t0
	vpshufb	$t3,@X[6],@X[6]
	vpaddq	-0x60($Tbl),@X[1],$t1
	vpshufb	$t3,@X[7],@X[7]
	vpaddq	-0x40($Tbl),@X[2],$t2
	vpaddq	-0x20($Tbl),@X[3],$t3
	vmovdqa	$t0,0x00(%rsp)
	vpaddq	0x00($Tbl),@X[4],$t0
	vmovdqa	$t1,0x10(%rsp)
	vpaddq	0x20($Tbl),@X[5],$t1
	vmovdqa	$t2,0x20(%rsp)
	vpaddq	0x40($Tbl),@X[6],$t2
	vmovdqa	$t3,0x30(%rsp)
	vpaddq	0x60($Tbl),@X[7],$t3
	vmovdqa	$t0,0x40(%rsp)
	mov	$A,$a1
	vmovdqa	$t1,0x50(%rsp)
	mov	$B,$a3
	vmovdqa	$t2,0x60(%rsp)
	xor	$C,$a3			# magic
	vmovdqa	$t3,0x70(%rsp)
	mov	$E,$a0
	jmp	.Lxop_00_47

.align	16
.Lxop_00_47:
	add	\$`16*2*$SZ`,$Tbl
___
sub XOP_512_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body);			# 52 instructions

	&vpalignr	($t0,@X[1],@X[0],$SZ);	# X[1..2]
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpalignr	($t3,@X[5],@X[4],$SZ);	# X[9..10]
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vprotq		($t1,$t0,8*$SZ-$sigma0[1]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpsrlq		($t0,$t0,$sigma0[2]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpaddq	(@X[0],@X[0],$t3);	# X[0..1] += X[9..10]
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vprotq		($t2,$t1,$sigma0[1]-$sigma0[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpxor		($t0,$t0,$t1);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vprotq	($t3,@X[7],8*$SZ-$sigma1[1]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpxor		($t0,$t0,$t2);		# sigma0(X[1..2])
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpsrlq	($t2,@X[7],$sigma1[2]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpaddq		(@X[0],@X[0],$t0);	# X[0..1] += sigma0(X[1..2])
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vprotq	($t1,$t3,$sigma1[1]-$sigma1[0]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpxor		($t3,$t3,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &vpxor		($t3,$t3,$t1);		# sigma1(X[14..15])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpaddq		(@X[0],@X[0],$t3);	# X[0..1] += sigma1(X[14..15])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&vpaddq		($t2,@X[0],16*2*$j-0x80."($Tbl)");
	  foreach (@insns) { eval; }		# remaining instructions
	&vmovdqa	(16*$j."(%rsp)",$t2);
}

    for ($i=0,$j=0; $j<8; $j++) {
	&XOP_512_00_47($j,\&body_00_15,@X);
	push(@X,shift(@X));			# rotate(@X)
    }
	&cmpb	($SZ-1+16*2*$SZ-0x80."($Tbl)",0);
	&jne	(".Lxop_00_47");

    for ($i=0; $i<16; ) {
	foreach(body_00_15()) { eval; }
    }
}
$code.=<<___;
	mov	$_ctx,$ctx
	mov	$a1,$A

	add	$SZ*0($ctx),$A
	lea	16*$SZ($inp),$inp
	add	$SZ*1($ctx),$B
	add	$SZ*2($ctx),$C
	add	$SZ*3($ctx),$D
	add	$SZ*4($ctx),$E
	add	$SZ*5($ctx),$F
	add	$SZ*6($ctx),$G
	add	$SZ*7($ctx),$H

	cmp	$_end,$inp

	mov	$A,$SZ*0($ctx)
	mov	$B,$SZ*1($ctx)
	mov	$C,$SZ*2($ctx)
	mov	$D,$SZ*3($ctx)
	mov	$E,$SZ*4($ctx)
	mov	$F,$SZ*5($ctx)
	mov	$G,$SZ*6($ctx)
	mov	$H,$SZ*7($ctx)
	jb	.Lloop_xop

	mov	$_rsp,%rsi
.cfi_def_cfa	%rsi,8
	vzeroupper
___
$code.=<<___ if ($win64);
	movaps	16*$SZ+32(%rsp),%xmm6
	movaps	16*$SZ+48(%rsp),%xmm7
	movaps	16*$SZ+64(%rsp),%xmm8
	movaps	16*$SZ+80(%rsp),%xmm9
___
$code.=<<___ if ($win64 && $SZ>4);
	movaps	16*$SZ+96(%rsp),%xmm10
	movaps	16*$SZ+112(%rsp),%xmm11
___
$code.=<<___;
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
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_xop:
	ret
.cfi_endproc
.size	${func}_xop,.-${func}_xop
___
}
######################################################################
# AVX+shrd code path
#
local *ror = sub { &shrd(@_[0],@_) };

$code.=<<___;
.type	${func}_avx,\@function,3
.align	64
${func}_avx:
.cfi_startproc
.Lavx_shortcut:
	mov	%rsp,%rax		# copy %rsp
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
	shl	\$4,%rdx		# num*16
	sub	\$`$framesz+$win64*16*($SZ==4?4:6)`,%rsp
	lea	($inp,%rdx,$SZ),%rdx	# inp+num*16*$SZ
	and	\$-64,%rsp		# align stack frame
	mov	$ctx,$_ctx		# save ctx, 1st arg
	mov	$inp,$_inp		# save inp, 2nd arh
	mov	%rdx,$_end		# save end pointer, "3rd" arg
	mov	%rax,$_rsp		# save copy of %rsp
.cfi_cfa_expression	$_rsp,deref,+8
___
$code.=<<___ if ($win64);
	movaps	%xmm6,16*$SZ+32(%rsp)
	movaps	%xmm7,16*$SZ+48(%rsp)
	movaps	%xmm8,16*$SZ+64(%rsp)
	movaps	%xmm9,16*$SZ+80(%rsp)
___
$code.=<<___ if ($win64 && $SZ>4);
	movaps	%xmm10,16*$SZ+96(%rsp)
	movaps	%xmm11,16*$SZ+112(%rsp)
___
$code.=<<___;
.Lprologue_avx:

	vzeroupper
	mov	$SZ*0($ctx),$A
	mov	$SZ*1($ctx),$B
	mov	$SZ*2($ctx),$C
	mov	$SZ*3($ctx),$D
	mov	$SZ*4($ctx),$E
	mov	$SZ*5($ctx),$F
	mov	$SZ*6($ctx),$G
	mov	$SZ*7($ctx),$H
___
					if ($SZ==4) {	# SHA256
    my @X = map("%xmm$_",(0..3));
    my ($t0,$t1,$t2,$t3, $t4,$t5) = map("%xmm$_",(4..9));

$code.=<<___;
	vmovdqa	$TABLE+`$SZ*2*$rounds`+32(%rip),$t4
	vmovdqa	$TABLE+`$SZ*2*$rounds`+64(%rip),$t5
	jmp	.Lloop_avx
.align	16
.Lloop_avx:
	vmovdqa	$TABLE+`$SZ*2*$rounds`(%rip),$t3
	vmovdqu	0x00($inp),@X[0]
	vmovdqu	0x10($inp),@X[1]
	vmovdqu	0x20($inp),@X[2]
	vmovdqu	0x30($inp),@X[3]
	vpshufb	$t3,@X[0],@X[0]
	lea	$TABLE(%rip),$Tbl
	vpshufb	$t3,@X[1],@X[1]
	vpshufb	$t3,@X[2],@X[2]
	vpaddd	0x00($Tbl),@X[0],$t0
	vpshufb	$t3,@X[3],@X[3]
	vpaddd	0x20($Tbl),@X[1],$t1
	vpaddd	0x40($Tbl),@X[2],$t2
	vpaddd	0x60($Tbl),@X[3],$t3
	vmovdqa	$t0,0x00(%rsp)
	mov	$A,$a1
	vmovdqa	$t1,0x10(%rsp)
	mov	$B,$a3
	vmovdqa	$t2,0x20(%rsp)
	xor	$C,$a3			# magic
	vmovdqa	$t3,0x30(%rsp)
	mov	$E,$a0
	jmp	.Lavx_00_47

.align	16
.Lavx_00_47:
	sub	\$`-16*2*$SZ`,$Tbl	# size optimization
___
sub Xupdate_256_AVX () {
	(
	'&vpalignr	($t0,@X[1],@X[0],$SZ)',	# X[1..4]
	 '&vpalignr	($t3,@X[3],@X[2],$SZ)',	# X[9..12]
	'&vpsrld	($t2,$t0,$sigma0[0]);',
	 '&vpaddd	(@X[0],@X[0],$t3)',	# X[0..3] += X[9..12]
	'&vpsrld	($t3,$t0,$sigma0[2])',
	'&vpslld	($t1,$t0,8*$SZ-$sigma0[1]);',
	'&vpxor		($t0,$t3,$t2)',
	 '&vpshufd	($t3,@X[3],0b11111010)',# X[14..15]
	'&vpsrld	($t2,$t2,$sigma0[1]-$sigma0[0]);',
	'&vpxor		($t0,$t0,$t1)',
	'&vpslld	($t1,$t1,$sigma0[1]-$sigma0[0]);',
	'&vpxor		($t0,$t0,$t2)',
	 '&vpsrld	($t2,$t3,$sigma1[2]);',
	'&vpxor		($t0,$t0,$t1)',		# sigma0(X[1..4])
	 '&vpsrlq	($t3,$t3,$sigma1[0]);',
	'&vpaddd	(@X[0],@X[0],$t0)',	# X[0..3] += sigma0(X[1..4])
	 '&vpxor	($t2,$t2,$t3);',
	 '&vpsrlq	($t3,$t3,$sigma1[1]-$sigma1[0])',
	 '&vpxor	($t2,$t2,$t3)',
	 '&vpshufb	($t2,$t2,$t4)',		# sigma1(X[14..15])
	'&vpaddd	(@X[0],@X[0],$t2)',	# X[0..1] += sigma1(X[14..15])
	 '&vpshufd	($t3,@X[0],0b01010000)',# X[16..17]
	 '&vpsrld	($t2,$t3,$sigma1[2])',
	 '&vpsrlq	($t3,$t3,$sigma1[0])',
	 '&vpxor	($t2,$t2,$t3);',
	 '&vpsrlq	($t3,$t3,$sigma1[1]-$sigma1[0])',
	 '&vpxor	($t2,$t2,$t3)',
	 '&vpshufb	($t2,$t2,$t5)',
	'&vpaddd	(@X[0],@X[0],$t2)'	# X[2..3] += sigma1(X[16..17])
	);
}

sub AVX_256_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body,&$body,&$body);	# 104 instructions

	foreach (Xupdate_256_AVX()) {		# 29 instructions
	    eval;
	    eval(shift(@insns));
	    eval(shift(@insns));
	    eval(shift(@insns));
	}
	&vpaddd		($t2,@X[0],16*2*$j."($Tbl)");
	  foreach (@insns) { eval; }		# remaining instructions
	&vmovdqa	(16*$j."(%rsp)",$t2);
}

    for ($i=0,$j=0; $j<4; $j++) {
	&AVX_256_00_47($j,\&body_00_15,@X);
	push(@X,shift(@X));			# rotate(@X)
    }
	&cmpb	($SZ-1+16*2*$SZ."($Tbl)",0);
	&jne	(".Lavx_00_47");

    for ($i=0; $i<16; ) {
	foreach(body_00_15()) { eval; }
    }

					} else {	# SHA512
    my @X = map("%xmm$_",(0..7));
    my ($t0,$t1,$t2,$t3) = map("%xmm$_",(8..11));

$code.=<<___;
	jmp	.Lloop_avx
.align	16
.Lloop_avx:
	vmovdqa	$TABLE+`$SZ*2*$rounds`(%rip),$t3
	vmovdqu	0x00($inp),@X[0]
	lea	$TABLE+0x80(%rip),$Tbl	# size optimization
	vmovdqu	0x10($inp),@X[1]
	vmovdqu	0x20($inp),@X[2]
	vpshufb	$t3,@X[0],@X[0]
	vmovdqu	0x30($inp),@X[3]
	vpshufb	$t3,@X[1],@X[1]
	vmovdqu	0x40($inp),@X[4]
	vpshufb	$t3,@X[2],@X[2]
	vmovdqu	0x50($inp),@X[5]
	vpshufb	$t3,@X[3],@X[3]
	vmovdqu	0x60($inp),@X[6]
	vpshufb	$t3,@X[4],@X[4]
	vmovdqu	0x70($inp),@X[7]
	vpshufb	$t3,@X[5],@X[5]
	vpaddq	-0x80($Tbl),@X[0],$t0
	vpshufb	$t3,@X[6],@X[6]
	vpaddq	-0x60($Tbl),@X[1],$t1
	vpshufb	$t3,@X[7],@X[7]
	vpaddq	-0x40($Tbl),@X[2],$t2
	vpaddq	-0x20($Tbl),@X[3],$t3
	vmovdqa	$t0,0x00(%rsp)
	vpaddq	0x00($Tbl),@X[4],$t0
	vmovdqa	$t1,0x10(%rsp)
	vpaddq	0x20($Tbl),@X[5],$t1
	vmovdqa	$t2,0x20(%rsp)
	vpaddq	0x40($Tbl),@X[6],$t2
	vmovdqa	$t3,0x30(%rsp)
	vpaddq	0x60($Tbl),@X[7],$t3
	vmovdqa	$t0,0x40(%rsp)
	mov	$A,$a1
	vmovdqa	$t1,0x50(%rsp)
	mov	$B,$a3
	vmovdqa	$t2,0x60(%rsp)
	xor	$C,$a3			# magic
	vmovdqa	$t3,0x70(%rsp)
	mov	$E,$a0
	jmp	.Lavx_00_47

.align	16
.Lavx_00_47:
	add	\$`16*2*$SZ`,$Tbl
___
sub Xupdate_512_AVX () {
	(
	'&vpalignr	($t0,@X[1],@X[0],$SZ)',	# X[1..2]
	 '&vpalignr	($t3,@X[5],@X[4],$SZ)',	# X[9..10]
	'&vpsrlq	($t2,$t0,$sigma0[0])',
	 '&vpaddq	(@X[0],@X[0],$t3);',	# X[0..1] += X[9..10]
	'&vpsrlq	($t3,$t0,$sigma0[2])',
	'&vpsllq	($t1,$t0,8*$SZ-$sigma0[1]);',
	 '&vpxor	($t0,$t3,$t2)',
	'&vpsrlq	($t2,$t2,$sigma0[1]-$sigma0[0]);',
	 '&vpxor	($t0,$t0,$t1)',
	'&vpsllq	($t1,$t1,$sigma0[1]-$sigma0[0]);',
	 '&vpxor	($t0,$t0,$t2)',
	 '&vpsrlq	($t3,@X[7],$sigma1[2]);',
	'&vpxor		($t0,$t0,$t1)',		# sigma0(X[1..2])
	 '&vpsllq	($t2,@X[7],8*$SZ-$sigma1[1]);',
	'&vpaddq	(@X[0],@X[0],$t0)',	# X[0..1] += sigma0(X[1..2])
	 '&vpsrlq	($t1,@X[7],$sigma1[0]);',
	 '&vpxor	($t3,$t3,$t2)',
	 '&vpsllq	($t2,$t2,$sigma1[1]-$sigma1[0]);',
	 '&vpxor	($t3,$t3,$t1)',
	 '&vpsrlq	($t1,$t1,$sigma1[1]-$sigma1[0]);',
	 '&vpxor	($t3,$t3,$t2)',
	 '&vpxor	($t3,$t3,$t1)',		# sigma1(X[14..15])
	'&vpaddq	(@X[0],@X[0],$t3)',	# X[0..1] += sigma1(X[14..15])
	);
}

sub AVX_512_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body);			# 52 instructions

	foreach (Xupdate_512_AVX()) {		# 23 instructions
	    eval;
	    eval(shift(@insns));
	    eval(shift(@insns));
	}
	&vpaddq		($t2,@X[0],16*2*$j-0x80."($Tbl)");
	  foreach (@insns) { eval; }		# remaining instructions
	&vmovdqa	(16*$j."(%rsp)",$t2);
}

    for ($i=0,$j=0; $j<8; $j++) {
	&AVX_512_00_47($j,\&body_00_15,@X);
	push(@X,shift(@X));			# rotate(@X)
    }
	&cmpb	($SZ-1+16*2*$SZ-0x80."($Tbl)",0);
	&jne	(".Lavx_00_47");

    for ($i=0; $i<16; ) {
	foreach(body_00_15()) { eval; }
    }
}
$code.=<<___;
	mov	$_ctx,$ctx
	mov	$a1,$A

	add	$SZ*0($ctx),$A
	lea	16*$SZ($inp),$inp
	add	$SZ*1($ctx),$B
	add	$SZ*2($ctx),$C
	add	$SZ*3($ctx),$D
	add	$SZ*4($ctx),$E
	add	$SZ*5($ctx),$F
	add	$SZ*6($ctx),$G
	add	$SZ*7($ctx),$H

	cmp	$_end,$inp

	mov	$A,$SZ*0($ctx)
	mov	$B,$SZ*1($ctx)
	mov	$C,$SZ*2($ctx)
	mov	$D,$SZ*3($ctx)
	mov	$E,$SZ*4($ctx)
	mov	$F,$SZ*5($ctx)
	mov	$G,$SZ*6($ctx)
	mov	$H,$SZ*7($ctx)
	jb	.Lloop_avx

	mov	$_rsp,%rsi
.cfi_def_cfa	%rsi,8
	vzeroupper
___
$code.=<<___ if ($win64);
	movaps	16*$SZ+32(%rsp),%xmm6
	movaps	16*$SZ+48(%rsp),%xmm7
	movaps	16*$SZ+64(%rsp),%xmm8
	movaps	16*$SZ+80(%rsp),%xmm9
___
$code.=<<___ if ($win64 && $SZ>4);
	movaps	16*$SZ+96(%rsp),%xmm10
	movaps	16*$SZ+112(%rsp),%xmm11
___
$code.=<<___;
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
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_avx:
	ret
.cfi_endproc
.size	${func}_avx,.-${func}_avx
___

if ($avx>1) {{
######################################################################
# AVX2+BMI code path
#
my $a5=$SZ==4?"%esi":"%rsi";	# zap $inp
my $PUSH8=8*2*$SZ;
use integer;

sub bodyx_00_15 () {
	# at start $a1 should be zero, $a3 - $b^$c and $a4 copy of $f
	(
	'($a,$b,$c,$d,$e,$f,$g,$h)=@ROT;'.

	'&add	($h,(32*($i/(16/$SZ))+$SZ*($i%(16/$SZ)))%$PUSH8.$base)',    # h+=X[i]+K[i]
	'&and	($a4,$e)',		# f&e
	'&rorx	($a0,$e,$Sigma1[2])',
	'&rorx	($a2,$e,$Sigma1[1])',

	'&lea	($a,"($a,$a1)")',	# h+=Sigma0(a) from the past
	'&lea	($h,"($h,$a4)")',
	'&andn	($a4,$e,$g)',		# ~e&g
	'&xor	($a0,$a2)',

	'&rorx	($a1,$e,$Sigma1[0])',
	'&lea	($h,"($h,$a4)")',	# h+=Ch(e,f,g)=(e&f)+(~e&g)
	'&xor	($a0,$a1)',		# Sigma1(e)
	'&mov	($a2,$a)',

	'&rorx	($a4,$a,$Sigma0[2])',
	'&lea	($h,"($h,$a0)")',	# h+=Sigma1(e)
	'&xor	($a2,$b)',		# a^b, b^c in next round
	'&rorx	($a1,$a,$Sigma0[1])',

	'&rorx	($a0,$a,$Sigma0[0])',
	'&lea	($d,"($d,$h)")',	# d+=h
	'&and	($a3,$a2)',		# (b^c)&(a^b)
	'&xor	($a1,$a4)',

	'&xor	($a3,$b)',		# Maj(a,b,c)=Ch(a^b,c,b)
	'&xor	($a1,$a0)',		# Sigma0(a)
	'&lea	($h,"($h,$a3)");'.	# h+=Maj(a,b,c)
	'&mov	($a4,$e)',		# copy of f in future

	'($a2,$a3) = ($a3,$a2); unshift(@ROT,pop(@ROT)); $i++;'
	);
	# and at the finish one has to $a+=$a1
}

$code.=<<___;
.type	${func}_avx2,\@function,3
.align	64
${func}_avx2:
.cfi_startproc
.Lavx2_shortcut:
	mov	%rsp,%rax		# copy %rsp
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
	sub	\$`2*$SZ*$rounds+4*8+$win64*16*($SZ==4?4:6)`,%rsp
	shl	\$4,%rdx		# num*16
	and	\$-256*$SZ,%rsp		# align stack frame
	lea	($inp,%rdx,$SZ),%rdx	# inp+num*16*$SZ
	add	\$`2*$SZ*($rounds-8)`,%rsp
	mov	$ctx,$_ctx		# save ctx, 1st arg
	mov	$inp,$_inp		# save inp, 2nd arh
	mov	%rdx,$_end		# save end pointer, "3rd" arg
	mov	%rax,$_rsp		# save copy of %rsp
.cfi_cfa_expression	$_rsp,deref,+8
___
$code.=<<___ if ($win64);
	movaps	%xmm6,16*$SZ+32(%rsp)
	movaps	%xmm7,16*$SZ+48(%rsp)
	movaps	%xmm8,16*$SZ+64(%rsp)
	movaps	%xmm9,16*$SZ+80(%rsp)
___
$code.=<<___ if ($win64 && $SZ>4);
	movaps	%xmm10,16*$SZ+96(%rsp)
	movaps	%xmm11,16*$SZ+112(%rsp)
___
$code.=<<___;
.Lprologue_avx2:

	vzeroupper
	sub	\$-16*$SZ,$inp		# inp++, size optimization
	mov	$SZ*0($ctx),$A
	mov	$inp,%r12		# borrow $T1
	mov	$SZ*1($ctx),$B
	cmp	%rdx,$inp		# $_end
	mov	$SZ*2($ctx),$C
	cmove	%rsp,%r12		# next block or random data
	mov	$SZ*3($ctx),$D
	mov	$SZ*4($ctx),$E
	mov	$SZ*5($ctx),$F
	mov	$SZ*6($ctx),$G
	mov	$SZ*7($ctx),$H
___
					if ($SZ==4) {	# SHA256
    my @X = map("%ymm$_",(0..3));
    my ($t0,$t1,$t2,$t3, $t4,$t5) = map("%ymm$_",(4..9));

$code.=<<___;
	vmovdqa	$TABLE+`$SZ*2*$rounds`+32(%rip),$t4
	vmovdqa	$TABLE+`$SZ*2*$rounds`+64(%rip),$t5
	jmp	.Loop_avx2
.align	16
.Loop_avx2:
	vmovdqa	$TABLE+`$SZ*2*$rounds`(%rip),$t3
	vmovdqu	-16*$SZ+0($inp),%xmm0
	vmovdqu	-16*$SZ+16($inp),%xmm1
	vmovdqu	-16*$SZ+32($inp),%xmm2
	vmovdqu	-16*$SZ+48($inp),%xmm3
	#mov		$inp,$_inp	# offload $inp
	vinserti128	\$1,(%r12),@X[0],@X[0]
	vinserti128	\$1,16(%r12),@X[1],@X[1]
	vpshufb		$t3,@X[0],@X[0]
	vinserti128	\$1,32(%r12),@X[2],@X[2]
	vpshufb		$t3,@X[1],@X[1]
	vinserti128	\$1,48(%r12),@X[3],@X[3]

	lea	$TABLE(%rip),$Tbl
	vpshufb	$t3,@X[2],@X[2]
	vpaddd	0x00($Tbl),@X[0],$t0
	vpshufb	$t3,@X[3],@X[3]
	vpaddd	0x20($Tbl),@X[1],$t1
	vpaddd	0x40($Tbl),@X[2],$t2
	vpaddd	0x60($Tbl),@X[3],$t3
	vmovdqa	$t0,0x00(%rsp)
	xor	$a1,$a1
	vmovdqa	$t1,0x20(%rsp)
	lea	-$PUSH8(%rsp),%rsp
	mov	$B,$a3
	vmovdqa	$t2,0x00(%rsp)
	xor	$C,$a3			# magic
	vmovdqa	$t3,0x20(%rsp)
	mov	$F,$a4
	sub	\$-16*2*$SZ,$Tbl	# size optimization
	jmp	.Lavx2_00_47

.align	16
.Lavx2_00_47:
___

sub AVX2_256_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body,&$body,&$body);	# 96 instructions
my $base = "+2*$PUSH8(%rsp)";

	&lea	("%rsp","-$PUSH8(%rsp)")	if (($j%2)==0);
	foreach (Xupdate_256_AVX()) {		# 29 instructions
	    eval;
	    eval(shift(@insns));
	    eval(shift(@insns));
	    eval(shift(@insns));
	}
	&vpaddd		($t2,@X[0],16*2*$j."($Tbl)");
	  foreach (@insns) { eval; }		# remaining instructions
	&vmovdqa	((32*$j)%$PUSH8."(%rsp)",$t2);
}

    for ($i=0,$j=0; $j<4; $j++) {
	&AVX2_256_00_47($j,\&bodyx_00_15,@X);
	push(@X,shift(@X));			# rotate(@X)
    }
	&lea	($Tbl,16*2*$SZ."($Tbl)");
	&cmpb	(($SZ-1)."($Tbl)",0);
	&jne	(".Lavx2_00_47");

    for ($i=0; $i<16; ) {
	my $base=$i<8?"+$PUSH8(%rsp)":"(%rsp)";
	foreach(bodyx_00_15()) { eval; }
    }
					} else {	# SHA512
    my @X = map("%ymm$_",(0..7));
    my ($t0,$t1,$t2,$t3) = map("%ymm$_",(8..11));

$code.=<<___;
	jmp	.Loop_avx2
.align	16
.Loop_avx2:
	vmovdqu	-16*$SZ($inp),%xmm0
	vmovdqu	-16*$SZ+16($inp),%xmm1
	vmovdqu	-16*$SZ+32($inp),%xmm2
	lea	$TABLE+0x80(%rip),$Tbl	# size optimization
	vmovdqu	-16*$SZ+48($inp),%xmm3
	vmovdqu	-16*$SZ+64($inp),%xmm4
	vmovdqu	-16*$SZ+80($inp),%xmm5
	vmovdqu	-16*$SZ+96($inp),%xmm6
	vmovdqu	-16*$SZ+112($inp),%xmm7
	#mov	$inp,$_inp	# offload $inp
	vmovdqa	`$SZ*2*$rounds-0x80`($Tbl),$t2
	vinserti128	\$1,(%r12),@X[0],@X[0]
	vinserti128	\$1,16(%r12),@X[1],@X[1]
	 vpshufb	$t2,@X[0],@X[0]
	vinserti128	\$1,32(%r12),@X[2],@X[2]
	 vpshufb	$t2,@X[1],@X[1]
	vinserti128	\$1,48(%r12),@X[3],@X[3]
	 vpshufb	$t2,@X[2],@X[2]
	vinserti128	\$1,64(%r12),@X[4],@X[4]
	 vpshufb	$t2,@X[3],@X[3]
	vinserti128	\$1,80(%r12),@X[5],@X[5]
	 vpshufb	$t2,@X[4],@X[4]
	vinserti128	\$1,96(%r12),@X[6],@X[6]
	 vpshufb	$t2,@X[5],@X[5]
	vinserti128	\$1,112(%r12),@X[7],@X[7]

	vpaddq	-0x80($Tbl),@X[0],$t0
	vpshufb	$t2,@X[6],@X[6]
	vpaddq	-0x60($Tbl),@X[1],$t1
	vpshufb	$t2,@X[7],@X[7]
	vpaddq	-0x40($Tbl),@X[2],$t2
	vpaddq	-0x20($Tbl),@X[3],$t3
	vmovdqa	$t0,0x00(%rsp)
	vpaddq	0x00($Tbl),@X[4],$t0
	vmovdqa	$t1,0x20(%rsp)
	vpaddq	0x20($Tbl),@X[5],$t1
	vmovdqa	$t2,0x40(%rsp)
	vpaddq	0x40($Tbl),@X[6],$t2
	vmovdqa	$t3,0x60(%rsp)
	lea	-$PUSH8(%rsp),%rsp
	vpaddq	0x60($Tbl),@X[7],$t3
	vmovdqa	$t0,0x00(%rsp)
	xor	$a1,$a1
	vmovdqa	$t1,0x20(%rsp)
	mov	$B,$a3
	vmovdqa	$t2,0x40(%rsp)
	xor	$C,$a3			# magic
	vmovdqa	$t3,0x60(%rsp)
	mov	$F,$a4
	add	\$16*2*$SZ,$Tbl
	jmp	.Lavx2_00_47

.align	16
.Lavx2_00_47:
___

sub AVX2_512_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body);			# 48 instructions
my $base = "+2*$PUSH8(%rsp)";

	&lea	("%rsp","-$PUSH8(%rsp)")	if (($j%4)==0);
	foreach (Xupdate_512_AVX()) {		# 23 instructions
	    eval;
	    if ($_ !~ /\;$/) {
		eval(shift(@insns));
		eval(shift(@insns));
		eval(shift(@insns));
	    }
	}
	&vpaddq		($t2,@X[0],16*2*$j-0x80."($Tbl)");
	  foreach (@insns) { eval; }		# remaining instructions
	&vmovdqa	((32*$j)%$PUSH8."(%rsp)",$t2);
}

    for ($i=0,$j=0; $j<8; $j++) {
	&AVX2_512_00_47($j,\&bodyx_00_15,@X);
	push(@X,shift(@X));			# rotate(@X)
    }
	&lea	($Tbl,16*2*$SZ."($Tbl)");
	&cmpb	(($SZ-1-0x80)."($Tbl)",0);
	&jne	(".Lavx2_00_47");

    for ($i=0; $i<16; ) {
	my $base=$i<8?"+$PUSH8(%rsp)":"(%rsp)";
	foreach(bodyx_00_15()) { eval; }
    }
}
$code.=<<___;
	mov	`2*$SZ*$rounds`(%rsp),$ctx	# $_ctx
	add	$a1,$A
	#mov	`2*$SZ*$rounds+8`(%rsp),$inp	# $_inp
	lea	`2*$SZ*($rounds-8)`(%rsp),$Tbl

	add	$SZ*0($ctx),$A
	add	$SZ*1($ctx),$B
	add	$SZ*2($ctx),$C
	add	$SZ*3($ctx),$D
	add	$SZ*4($ctx),$E
	add	$SZ*5($ctx),$F
	add	$SZ*6($ctx),$G
	add	$SZ*7($ctx),$H

	mov	$A,$SZ*0($ctx)
	mov	$B,$SZ*1($ctx)
	mov	$C,$SZ*2($ctx)
	mov	$D,$SZ*3($ctx)
	mov	$E,$SZ*4($ctx)
	mov	$F,$SZ*5($ctx)
	mov	$G,$SZ*6($ctx)
	mov	$H,$SZ*7($ctx)

	cmp	`$PUSH8+2*8`($Tbl),$inp	# $_end
	je	.Ldone_avx2

	xor	$a1,$a1
	mov	$B,$a3
	xor	$C,$a3			# magic
	mov	$F,$a4
	jmp	.Lower_avx2
.align	16
.Lower_avx2:
___
    for ($i=0; $i<8; ) {
	my $base="+16($Tbl)";
	foreach(bodyx_00_15()) { eval; }
    }
$code.=<<___;
	lea	-$PUSH8($Tbl),$Tbl
	cmp	%rsp,$Tbl
	jae	.Lower_avx2

	mov	`2*$SZ*$rounds`(%rsp),$ctx	# $_ctx
	add	$a1,$A
	#mov	`2*$SZ*$rounds+8`(%rsp),$inp	# $_inp
	lea	`2*$SZ*($rounds-8)`(%rsp),%rsp

	add	$SZ*0($ctx),$A
	add	$SZ*1($ctx),$B
	add	$SZ*2($ctx),$C
	add	$SZ*3($ctx),$D
	add	$SZ*4($ctx),$E
	add	$SZ*5($ctx),$F
	lea	`2*16*$SZ`($inp),$inp	# inp+=2
	add	$SZ*6($ctx),$G
	mov	$inp,%r12
	add	$SZ*7($ctx),$H
	cmp	$_end,$inp

	mov	$A,$SZ*0($ctx)
	cmove	%rsp,%r12		# next block or stale data
	mov	$B,$SZ*1($ctx)
	mov	$C,$SZ*2($ctx)
	mov	$D,$SZ*3($ctx)
	mov	$E,$SZ*4($ctx)
	mov	$F,$SZ*5($ctx)
	mov	$G,$SZ*6($ctx)
	mov	$H,$SZ*7($ctx)

	jbe	.Loop_avx2
	lea	(%rsp),$Tbl

.Ldone_avx2:
	lea	($Tbl),%rsp
	mov	$_rsp,%rsi
.cfi_def_cfa	%rsi,8
	vzeroupper
___
$code.=<<___ if ($win64);
	movaps	16*$SZ+32(%rsp),%xmm6
	movaps	16*$SZ+48(%rsp),%xmm7
	movaps	16*$SZ+64(%rsp),%xmm8
	movaps	16*$SZ+80(%rsp),%xmm9
___
$code.=<<___ if ($win64 && $SZ>4);
	movaps	16*$SZ+96(%rsp),%xmm10
	movaps	16*$SZ+112(%rsp),%xmm11
___
$code.=<<___;
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
	lea	(%rsi),%rsp
.cfi_def_cfa_register	%rsp
.Lepilogue_avx2:
	ret
.cfi_endproc
.size	${func}_avx2,.-${func}_avx2
___
}}
}}}}}

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
	mov	56($disp),%r11		# disp->HanderlData

	mov	0(%r11),%r10d		# HandlerData[0]
	lea	(%rsi,%r10),%r10	# prologue label
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lin_prologue

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lin_prologue
___
$code.=<<___ if ($avx>1);
	lea	.Lavx2_shortcut(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<avx2_shortcut
	jb	.Lnot_in_avx2

	and	\$-256*$SZ,%rax
	add	\$`2*$SZ*($rounds-8)`,%rax
.Lnot_in_avx2:
___
$code.=<<___;
	mov	%rax,%rsi		# put aside Rsp
	mov	16*$SZ+3*8(%rax),%rax	# pull $_rsp

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

	lea	.Lepilogue(%rip),%r10
	cmp	%r10,%rbx
	jb	.Lin_prologue		# non-AVX code

	lea	16*$SZ+4*8(%rsi),%rsi	# Xmm6- save area
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$`$SZ==4?8:12`,%ecx
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

$code.=<<___ if ($SZ==4 && $shaext);
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
	jb	.Lin_prologue

	lea	.Lepilogue_shaext(%rip),%r10
	cmp	%r10,%rbx		# context->Rip>=.Lepilogue
	jae	.Lin_prologue

	lea	-8-5*16(%rax),%rsi
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$10,%ecx
	.long	0xa548f3fc		# cld; rep movsq

	jmp	.Lin_prologue
.size	shaext_handler,.-shaext_handler
___

$code.=<<___;
.section	.pdata
.align	4
	.rva	.LSEH_begin_$func
	.rva	.LSEH_end_$func
	.rva	.LSEH_info_$func
___
$code.=<<___ if ($SZ==4 && $shaext);
	.rva	.LSEH_begin_${func}_shaext
	.rva	.LSEH_end_${func}_shaext
	.rva	.LSEH_info_${func}_shaext
___
$code.=<<___ if ($SZ==4);
	.rva	.LSEH_begin_${func}_ssse3
	.rva	.LSEH_end_${func}_ssse3
	.rva	.LSEH_info_${func}_ssse3
___
$code.=<<___ if ($avx && $SZ==8);
	.rva	.LSEH_begin_${func}_xop
	.rva	.LSEH_end_${func}_xop
	.rva	.LSEH_info_${func}_xop
___
$code.=<<___ if ($avx);
	.rva	.LSEH_begin_${func}_avx
	.rva	.LSEH_end_${func}_avx
	.rva	.LSEH_info_${func}_avx
___
$code.=<<___ if ($avx>1);
	.rva	.LSEH_begin_${func}_avx2
	.rva	.LSEH_end_${func}_avx2
	.rva	.LSEH_info_${func}_avx2
___
$code.=<<___;
.section	.xdata
.align	8
.LSEH_info_$func:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lprologue,.Lepilogue			# HandlerData[]
___
$code.=<<___ if ($SZ==4 && $shaext);
.LSEH_info_${func}_shaext:
	.byte	9,0,0,0
	.rva	shaext_handler
___
$code.=<<___ if ($SZ==4);
.LSEH_info_${func}_ssse3:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lprologue_ssse3,.Lepilogue_ssse3	# HandlerData[]
___
$code.=<<___ if ($avx && $SZ==8);
.LSEH_info_${func}_xop:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lprologue_xop,.Lepilogue_xop		# HandlerData[]
___
$code.=<<___ if ($avx);
.LSEH_info_${func}_avx:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lprologue_avx,.Lepilogue_avx		# HandlerData[]
___
$code.=<<___ if ($avx>1);
.LSEH_info_${func}_avx2:
	.byte	9,0,0,0
	.rva	se_handler
	.rva	.Lprologue_avx2,.Lepilogue_avx2		# HandlerData[]
___
}

sub sha256op38 {
    my $instr = shift;
    my %opcodelet = (
		"sha256rnds2" => 0xcb,
  		"sha256msg1"  => 0xcc,
		"sha256msg2"  => 0xcd	);

    if (defined($opcodelet{$instr}) && @_[0] =~ /%xmm([0-7]),\s*%xmm([0-7])/) {
      my @opcode=(0x0f,0x38);
	push @opcode,$opcodelet{$instr};
	push @opcode,0xc0|($1&7)|(($2&7)<<3);		# ModR/M
	return ".byte\t".join(',',@opcode);
    } else {
	return $instr."\t".@_[0];
    }
}

foreach (split("\n",$code)) {
	s/\`([^\`]*)\`/eval $1/geo;

	s/\b(sha256[^\s]*)\s+(.*)/sha256op38($1,$2)/geo;

	print $_,"\n";
}
close STDOUT;
