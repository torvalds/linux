#! /usr/bin/env perl
# Copyright 2009-2019 The OpenSSL Project Authors. All Rights Reserved.
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
# This module implements support for Intel AES-NI extension. In
# OpenSSL context it's used with Intel engine, but can also be used as
# drop-in replacement for crypto/aes/asm/aes-x86_64.pl [see below for
# details].
#
# Performance.
#
# Given aes(enc|dec) instructions' latency asymptotic performance for
# non-parallelizable modes such as CBC encrypt is 3.75 cycles per byte
# processed with 128-bit key. And given their throughput asymptotic
# performance for parallelizable modes is 1.25 cycles per byte. Being
# asymptotic limit it's not something you commonly achieve in reality,
# but how close does one get? Below are results collected for
# different modes and block sized. Pairs of numbers are for en-/
# decryption.
#
#	16-byte     64-byte     256-byte    1-KB        8-KB
# ECB	4.25/4.25   1.38/1.38   1.28/1.28   1.26/1.26	1.26/1.26
# CTR	5.42/5.42   1.92/1.92   1.44/1.44   1.28/1.28   1.26/1.26
# CBC	4.38/4.43   4.15/1.43   4.07/1.32   4.07/1.29   4.06/1.28
# CCM	5.66/9.42   4.42/5.41   4.16/4.40   4.09/4.15   4.06/4.07
# OFB	5.42/5.42   4.64/4.64   4.44/4.44   4.39/4.39   4.38/4.38
# CFB	5.73/5.85   5.56/5.62   5.48/5.56   5.47/5.55   5.47/5.55
#
# ECB, CTR, CBC and CCM results are free from EVP overhead. This means
# that otherwise used 'openssl speed -evp aes-128-??? -engine aesni
# [-decrypt]' will exhibit 10-15% worse results for smaller blocks.
# The results were collected with specially crafted speed.c benchmark
# in order to compare them with results reported in "Intel Advanced
# Encryption Standard (AES) New Instruction Set" White Paper Revision
# 3.0 dated May 2010. All above results are consistently better. This
# module also provides better performance for block sizes smaller than
# 128 bytes in points *not* represented in the above table.
#
# Looking at the results for 8-KB buffer.
#
# CFB and OFB results are far from the limit, because implementation
# uses "generic" CRYPTO_[c|o]fb128_encrypt interfaces relying on
# single-block aesni_encrypt, which is not the most optimal way to go.
# CBC encrypt result is unexpectedly high and there is no documented
# explanation for it. Seemingly there is a small penalty for feeding
# the result back to AES unit the way it's done in CBC mode. There is
# nothing one can do and the result appears optimal. CCM result is
# identical to CBC, because CBC-MAC is essentially CBC encrypt without
# saving output. CCM CTR "stays invisible," because it's neatly
# interleaved wih CBC-MAC. This provides ~30% improvement over
# "straightforward" CCM implementation with CTR and CBC-MAC performed
# disjointly. Parallelizable modes practically achieve the theoretical
# limit.
#
# Looking at how results vary with buffer size.
#
# Curves are practically saturated at 1-KB buffer size. In most cases
# "256-byte" performance is >95%, and "64-byte" is ~90% of "8-KB" one.
# CTR curve doesn't follow this pattern and is "slowest" changing one
# with "256-byte" result being 87% of "8-KB." This is because overhead
# in CTR mode is most computationally intensive. Small-block CCM
# decrypt is slower than encrypt, because first CTR and last CBC-MAC
# iterations can't be interleaved.
#
# Results for 192- and 256-bit keys.
#
# EVP-free results were observed to scale perfectly with number of
# rounds for larger block sizes, i.e. 192-bit result being 10/12 times
# lower and 256-bit one - 10/14. Well, in CBC encrypt case differences
# are a tad smaller, because the above mentioned penalty biases all
# results by same constant value. In similar way function call
# overhead affects small-block performance, as well as OFB and CFB
# results. Differences are not large, most common coefficients are
# 10/11.7 and 10/13.4 (as opposite to 10/12.0 and 10/14.0), but one
# observe even 10/11.2 and 10/12.4 (CTR, OFB, CFB)...

# January 2011
#
# While Westmere processor features 6 cycles latency for aes[enc|dec]
# instructions, which can be scheduled every second cycle, Sandy
# Bridge spends 8 cycles per instruction, but it can schedule them
# every cycle. This means that code targeting Westmere would perform
# suboptimally on Sandy Bridge. Therefore this update.
#
# In addition, non-parallelizable CBC encrypt (as well as CCM) is
# optimized. Relative improvement might appear modest, 8% on Westmere,
# but in absolute terms it's 3.77 cycles per byte encrypted with
# 128-bit key on Westmere, and 5.07 - on Sandy Bridge. These numbers
# should be compared to asymptotic limits of 3.75 for Westmere and
# 5.00 for Sandy Bridge. Actually, the fact that they get this close
# to asymptotic limits is quite amazing. Indeed, the limit is
# calculated as latency times number of rounds, 10 for 128-bit key,
# and divided by 16, the number of bytes in block, or in other words
# it accounts *solely* for aesenc instructions. But there are extra
# instructions, and numbers so close to the asymptotic limits mean
# that it's as if it takes as little as *one* additional cycle to
# execute all of them. How is it possible? It is possible thanks to
# out-of-order execution logic, which manages to overlap post-
# processing of previous block, things like saving the output, with
# actual encryption of current block, as well as pre-processing of
# current block, things like fetching input and xor-ing it with
# 0-round element of the key schedule, with actual encryption of
# previous block. Keep this in mind...
#
# For parallelizable modes, such as ECB, CBC decrypt, CTR, higher
# performance is achieved by interleaving instructions working on
# independent blocks. In which case asymptotic limit for such modes
# can be obtained by dividing above mentioned numbers by AES
# instructions' interleave factor. Westmere can execute at most 3
# instructions at a time, meaning that optimal interleave factor is 3,
# and that's where the "magic" number of 1.25 come from. "Optimal
# interleave factor" means that increase of interleave factor does
# not improve performance. The formula has proven to reflect reality
# pretty well on Westmere... Sandy Bridge on the other hand can
# execute up to 8 AES instructions at a time, so how does varying
# interleave factor affect the performance? Here is table for ECB
# (numbers are cycles per byte processed with 128-bit key):
#
# instruction interleave factor		3x	6x	8x
# theoretical asymptotic limit		1.67	0.83	0.625
# measured performance for 8KB block	1.05	0.86	0.84
#
# "as if" interleave factor		4.7x	5.8x	6.0x
#
# Further data for other parallelizable modes:
#
# CBC decrypt				1.16	0.93	0.74
# CTR					1.14	0.91	0.74
#
# Well, given 3x column it's probably inappropriate to call the limit
# asymptotic, if it can be surpassed, isn't it? What happens there?
# Rewind to CBC paragraph for the answer. Yes, out-of-order execution
# magic is responsible for this. Processor overlaps not only the
# additional instructions with AES ones, but even AES instructions
# processing adjacent triplets of independent blocks. In the 6x case
# additional instructions  still claim disproportionally small amount
# of additional cycles, but in 8x case number of instructions must be
# a tad too high for out-of-order logic to cope with, and AES unit
# remains underutilized... As you can see 8x interleave is hardly
# justifiable, so there no need to feel bad that 32-bit aesni-x86.pl
# utilizes 6x interleave because of limited register bank capacity.
#
# Higher interleave factors do have negative impact on Westmere
# performance. While for ECB mode it's negligible ~1.5%, other
# parallelizables perform ~5% worse, which is outweighed by ~25%
# improvement on Sandy Bridge. To balance regression on Westmere
# CTR mode was implemented with 6x aesenc interleave factor.

# April 2011
#
# Add aesni_xts_[en|de]crypt. Westmere spends 1.25 cycles processing
# one byte out of 8KB with 128-bit key, Sandy Bridge - 0.90. Just like
# in CTR mode AES instruction interleave factor was chosen to be 6x.

# November 2015
#
# Add aesni_ocb_[en|de]crypt. AES instruction interleave factor was
# chosen to be 6x.

######################################################################
# Current large-block performance in cycles per byte processed with
# 128-bit key (less is better).
#
#		CBC en-/decrypt	CTR	XTS	ECB	OCB
# Westmere	3.77/1.25	1.25	1.25	1.26
# * Bridge	5.07/0.74	0.75	0.90	0.85	0.98
# Haswell	4.44/0.63	0.63	0.73	0.63	0.70
# Skylake	2.62/0.63	0.63	0.63	0.63
# Silvermont	5.75/3.54	3.56	4.12	3.87(*)	4.11
# Knights L	2.54/0.77	0.78	0.85	-	1.50
# Goldmont	3.82/1.26	1.26	1.29	1.29	1.50
# Bulldozer	5.77/0.70	0.72	0.90	0.70	0.95
# Ryzen		2.71/0.35	0.35	0.44	0.38	0.49
#
# (*)	Atom Silvermont ECB result is suboptimal because of penalties
#	incurred by operations on %xmm8-15. As ECB is not considered
#	critical, nothing was done to mitigate the problem.

$PREFIX="aesni";	# if $PREFIX is set to "AES", the script
			# generates drop-in replacement for
			# crypto/aes/asm/aes-x86_64.pl:-)

$flavour = shift;
$output  = shift;
if ($flavour =~ /\./) { $output = $flavour; undef $flavour; }

$win64=0; $win64=1 if ($flavour =~ /[nm]asm|mingw64/ || $output =~ /\.asm$/);

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
( $xlate="${dir}x86_64-xlate.pl" and -f $xlate ) or
( $xlate="${dir}../../perlasm/x86_64-xlate.pl" and -f $xlate) or
die "can't locate x86_64-xlate.pl";

open OUT,"| \"$^X\" \"$xlate\" $flavour \"$output\"";
*STDOUT=*OUT;

$movkey = $PREFIX eq "aesni" ? "movups" : "movups";
@_4args=$win64?	("%rcx","%rdx","%r8", "%r9") :	# Win64 order
		("%rdi","%rsi","%rdx","%rcx");	# Unix order

$code=".text\n";
$code.=".extern	OPENSSL_ia32cap_P\n";

$rounds="%eax";	# input to and changed by aesni_[en|de]cryptN !!!
# this is natural Unix argument order for public $PREFIX_[ecb|cbc]_encrypt ...
$inp="%rdi";
$out="%rsi";
$len="%rdx";
$key="%rcx";	# input to and changed by aesni_[en|de]cryptN !!!
$ivp="%r8";	# cbc, ctr, ...

$rnds_="%r10d";	# backup copy for $rounds
$key_="%r11";	# backup copy for $key

# %xmm register layout
$rndkey0="%xmm0";	$rndkey1="%xmm1";
$inout0="%xmm2";	$inout1="%xmm3";
$inout2="%xmm4";	$inout3="%xmm5";
$inout4="%xmm6";	$inout5="%xmm7";
$inout6="%xmm8";	$inout7="%xmm9";

$in2="%xmm6";		$in1="%xmm7";	# used in CBC decrypt, CTR, ...
$in0="%xmm8";		$iv="%xmm9";

# Inline version of internal aesni_[en|de]crypt1.
#
# Why folded loop? Because aes[enc|dec] is slow enough to accommodate
# cycles which take care of loop variables...
{ my $sn;
sub aesni_generate1 {
my ($p,$key,$rounds,$inout,$ivec)=@_;	$inout=$inout0 if (!defined($inout));
++$sn;
$code.=<<___;
	$movkey	($key),$rndkey0
	$movkey	16($key),$rndkey1
___
$code.=<<___ if (defined($ivec));
	xorps	$rndkey0,$ivec
	lea	32($key),$key
	xorps	$ivec,$inout
___
$code.=<<___ if (!defined($ivec));
	lea	32($key),$key
	xorps	$rndkey0,$inout
___
$code.=<<___;
.Loop_${p}1_$sn:
	aes${p}	$rndkey1,$inout
	dec	$rounds
	$movkey	($key),$rndkey1
	lea	16($key),$key
	jnz	.Loop_${p}1_$sn	# loop body is 16 bytes
	aes${p}last	$rndkey1,$inout
___
}}
# void $PREFIX_[en|de]crypt (const void *inp,void *out,const AES_KEY *key);
#
{ my ($inp,$out,$key) = @_4args;

$code.=<<___;
.globl	${PREFIX}_encrypt
.type	${PREFIX}_encrypt,\@abi-omnipotent
.align	16
${PREFIX}_encrypt:
.cfi_startproc
	movups	($inp),$inout0		# load input
	mov	240($key),$rounds	# key->rounds
___
	&aesni_generate1("enc",$key,$rounds);
$code.=<<___;
	 pxor	$rndkey0,$rndkey0	# clear register bank
	 pxor	$rndkey1,$rndkey1
	movups	$inout0,($out)		# output
	 pxor	$inout0,$inout0
	ret
.cfi_endproc
.size	${PREFIX}_encrypt,.-${PREFIX}_encrypt

.globl	${PREFIX}_decrypt
.type	${PREFIX}_decrypt,\@abi-omnipotent
.align	16
${PREFIX}_decrypt:
.cfi_startproc
	movups	($inp),$inout0		# load input
	mov	240($key),$rounds	# key->rounds
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	 pxor	$rndkey0,$rndkey0	# clear register bank
	 pxor	$rndkey1,$rndkey1
	movups	$inout0,($out)		# output
	 pxor	$inout0,$inout0
	ret
.cfi_endproc
.size	${PREFIX}_decrypt, .-${PREFIX}_decrypt
___
}

# _aesni_[en|de]cryptN are private interfaces, N denotes interleave
# factor. Why 3x subroutine were originally used in loops? Even though
# aes[enc|dec] latency was originally 6, it could be scheduled only
# every *2nd* cycle. Thus 3x interleave was the one providing optimal
# utilization, i.e. when subroutine's throughput is virtually same as
# of non-interleaved subroutine [for number of input blocks up to 3].
# This is why it originally made no sense to implement 2x subroutine.
# But times change and it became appropriate to spend extra 192 bytes
# on 2x subroutine on Atom Silvermont account. For processors that
# can schedule aes[enc|dec] every cycle optimal interleave factor
# equals to corresponding instructions latency. 8x is optimal for
# * Bridge and "super-optimal" for other Intel CPUs...

sub aesni_generate2 {
my $dir=shift;
# As already mentioned it takes in $key and $rounds, which are *not*
# preserved. $inout[0-1] is cipher/clear text...
$code.=<<___;
.type	_aesni_${dir}rypt2,\@abi-omnipotent
.align	16
_aesni_${dir}rypt2:
.cfi_startproc
	$movkey	($key),$rndkey0
	shl	\$4,$rounds
	$movkey	16($key),$rndkey1
	xorps	$rndkey0,$inout0
	xorps	$rndkey0,$inout1
	$movkey	32($key),$rndkey0
	lea	32($key,$rounds),$key
	neg	%rax				# $rounds
	add	\$16,%rax

.L${dir}_loop2:
	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	$movkey		($key,%rax),$rndkey1
	add		\$32,%rax
	aes${dir}	$rndkey0,$inout0
	aes${dir}	$rndkey0,$inout1
	$movkey		-16($key,%rax),$rndkey0
	jnz		.L${dir}_loop2

	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}last	$rndkey0,$inout0
	aes${dir}last	$rndkey0,$inout1
	ret
.cfi_endproc
.size	_aesni_${dir}rypt2,.-_aesni_${dir}rypt2
___
}
sub aesni_generate3 {
my $dir=shift;
# As already mentioned it takes in $key and $rounds, which are *not*
# preserved. $inout[0-2] is cipher/clear text...
$code.=<<___;
.type	_aesni_${dir}rypt3,\@abi-omnipotent
.align	16
_aesni_${dir}rypt3:
.cfi_startproc
	$movkey	($key),$rndkey0
	shl	\$4,$rounds
	$movkey	16($key),$rndkey1
	xorps	$rndkey0,$inout0
	xorps	$rndkey0,$inout1
	xorps	$rndkey0,$inout2
	$movkey	32($key),$rndkey0
	lea	32($key,$rounds),$key
	neg	%rax				# $rounds
	add	\$16,%rax

.L${dir}_loop3:
	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}	$rndkey1,$inout2
	$movkey		($key,%rax),$rndkey1
	add		\$32,%rax
	aes${dir}	$rndkey0,$inout0
	aes${dir}	$rndkey0,$inout1
	aes${dir}	$rndkey0,$inout2
	$movkey		-16($key,%rax),$rndkey0
	jnz		.L${dir}_loop3

	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}	$rndkey1,$inout2
	aes${dir}last	$rndkey0,$inout0
	aes${dir}last	$rndkey0,$inout1
	aes${dir}last	$rndkey0,$inout2
	ret
.cfi_endproc
.size	_aesni_${dir}rypt3,.-_aesni_${dir}rypt3
___
}
# 4x interleave is implemented to improve small block performance,
# most notably [and naturally] 4 block by ~30%. One can argue that one
# should have implemented 5x as well, but improvement would be <20%,
# so it's not worth it...
sub aesni_generate4 {
my $dir=shift;
# As already mentioned it takes in $key and $rounds, which are *not*
# preserved. $inout[0-3] is cipher/clear text...
$code.=<<___;
.type	_aesni_${dir}rypt4,\@abi-omnipotent
.align	16
_aesni_${dir}rypt4:
.cfi_startproc
	$movkey	($key),$rndkey0
	shl	\$4,$rounds
	$movkey	16($key),$rndkey1
	xorps	$rndkey0,$inout0
	xorps	$rndkey0,$inout1
	xorps	$rndkey0,$inout2
	xorps	$rndkey0,$inout3
	$movkey	32($key),$rndkey0
	lea	32($key,$rounds),$key
	neg	%rax				# $rounds
	.byte	0x0f,0x1f,0x00
	add	\$16,%rax

.L${dir}_loop4:
	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}	$rndkey1,$inout2
	aes${dir}	$rndkey1,$inout3
	$movkey		($key,%rax),$rndkey1
	add		\$32,%rax
	aes${dir}	$rndkey0,$inout0
	aes${dir}	$rndkey0,$inout1
	aes${dir}	$rndkey0,$inout2
	aes${dir}	$rndkey0,$inout3
	$movkey		-16($key,%rax),$rndkey0
	jnz		.L${dir}_loop4

	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}	$rndkey1,$inout2
	aes${dir}	$rndkey1,$inout3
	aes${dir}last	$rndkey0,$inout0
	aes${dir}last	$rndkey0,$inout1
	aes${dir}last	$rndkey0,$inout2
	aes${dir}last	$rndkey0,$inout3
	ret
.cfi_endproc
.size	_aesni_${dir}rypt4,.-_aesni_${dir}rypt4
___
}
sub aesni_generate6 {
my $dir=shift;
# As already mentioned it takes in $key and $rounds, which are *not*
# preserved. $inout[0-5] is cipher/clear text...
$code.=<<___;
.type	_aesni_${dir}rypt6,\@abi-omnipotent
.align	16
_aesni_${dir}rypt6:
.cfi_startproc
	$movkey		($key),$rndkey0
	shl		\$4,$rounds
	$movkey		16($key),$rndkey1
	xorps		$rndkey0,$inout0
	pxor		$rndkey0,$inout1
	pxor		$rndkey0,$inout2
	aes${dir}	$rndkey1,$inout0
	lea		32($key,$rounds),$key
	neg		%rax			# $rounds
	aes${dir}	$rndkey1,$inout1
	pxor		$rndkey0,$inout3
	pxor		$rndkey0,$inout4
	aes${dir}	$rndkey1,$inout2
	pxor		$rndkey0,$inout5
	$movkey		($key,%rax),$rndkey0
	add		\$16,%rax
	jmp		.L${dir}_loop6_enter
.align	16
.L${dir}_loop6:
	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}	$rndkey1,$inout2
.L${dir}_loop6_enter:
	aes${dir}	$rndkey1,$inout3
	aes${dir}	$rndkey1,$inout4
	aes${dir}	$rndkey1,$inout5
	$movkey		($key,%rax),$rndkey1
	add		\$32,%rax
	aes${dir}	$rndkey0,$inout0
	aes${dir}	$rndkey0,$inout1
	aes${dir}	$rndkey0,$inout2
	aes${dir}	$rndkey0,$inout3
	aes${dir}	$rndkey0,$inout4
	aes${dir}	$rndkey0,$inout5
	$movkey		-16($key,%rax),$rndkey0
	jnz		.L${dir}_loop6

	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}	$rndkey1,$inout2
	aes${dir}	$rndkey1,$inout3
	aes${dir}	$rndkey1,$inout4
	aes${dir}	$rndkey1,$inout5
	aes${dir}last	$rndkey0,$inout0
	aes${dir}last	$rndkey0,$inout1
	aes${dir}last	$rndkey0,$inout2
	aes${dir}last	$rndkey0,$inout3
	aes${dir}last	$rndkey0,$inout4
	aes${dir}last	$rndkey0,$inout5
	ret
.cfi_endproc
.size	_aesni_${dir}rypt6,.-_aesni_${dir}rypt6
___
}
sub aesni_generate8 {
my $dir=shift;
# As already mentioned it takes in $key and $rounds, which are *not*
# preserved. $inout[0-7] is cipher/clear text...
$code.=<<___;
.type	_aesni_${dir}rypt8,\@abi-omnipotent
.align	16
_aesni_${dir}rypt8:
.cfi_startproc
	$movkey		($key),$rndkey0
	shl		\$4,$rounds
	$movkey		16($key),$rndkey1
	xorps		$rndkey0,$inout0
	xorps		$rndkey0,$inout1
	pxor		$rndkey0,$inout2
	pxor		$rndkey0,$inout3
	pxor		$rndkey0,$inout4
	lea		32($key,$rounds),$key
	neg		%rax			# $rounds
	aes${dir}	$rndkey1,$inout0
	pxor		$rndkey0,$inout5
	pxor		$rndkey0,$inout6
	aes${dir}	$rndkey1,$inout1
	pxor		$rndkey0,$inout7
	$movkey		($key,%rax),$rndkey0
	add		\$16,%rax
	jmp		.L${dir}_loop8_inner
.align	16
.L${dir}_loop8:
	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
.L${dir}_loop8_inner:
	aes${dir}	$rndkey1,$inout2
	aes${dir}	$rndkey1,$inout3
	aes${dir}	$rndkey1,$inout4
	aes${dir}	$rndkey1,$inout5
	aes${dir}	$rndkey1,$inout6
	aes${dir}	$rndkey1,$inout7
.L${dir}_loop8_enter:
	$movkey		($key,%rax),$rndkey1
	add		\$32,%rax
	aes${dir}	$rndkey0,$inout0
	aes${dir}	$rndkey0,$inout1
	aes${dir}	$rndkey0,$inout2
	aes${dir}	$rndkey0,$inout3
	aes${dir}	$rndkey0,$inout4
	aes${dir}	$rndkey0,$inout5
	aes${dir}	$rndkey0,$inout6
	aes${dir}	$rndkey0,$inout7
	$movkey		-16($key,%rax),$rndkey0
	jnz		.L${dir}_loop8

	aes${dir}	$rndkey1,$inout0
	aes${dir}	$rndkey1,$inout1
	aes${dir}	$rndkey1,$inout2
	aes${dir}	$rndkey1,$inout3
	aes${dir}	$rndkey1,$inout4
	aes${dir}	$rndkey1,$inout5
	aes${dir}	$rndkey1,$inout6
	aes${dir}	$rndkey1,$inout7
	aes${dir}last	$rndkey0,$inout0
	aes${dir}last	$rndkey0,$inout1
	aes${dir}last	$rndkey0,$inout2
	aes${dir}last	$rndkey0,$inout3
	aes${dir}last	$rndkey0,$inout4
	aes${dir}last	$rndkey0,$inout5
	aes${dir}last	$rndkey0,$inout6
	aes${dir}last	$rndkey0,$inout7
	ret
.cfi_endproc
.size	_aesni_${dir}rypt8,.-_aesni_${dir}rypt8
___
}
&aesni_generate2("enc") if ($PREFIX eq "aesni");
&aesni_generate2("dec");
&aesni_generate3("enc") if ($PREFIX eq "aesni");
&aesni_generate3("dec");
&aesni_generate4("enc") if ($PREFIX eq "aesni");
&aesni_generate4("dec");
&aesni_generate6("enc") if ($PREFIX eq "aesni");
&aesni_generate6("dec");
&aesni_generate8("enc") if ($PREFIX eq "aesni");
&aesni_generate8("dec");

if ($PREFIX eq "aesni") {
########################################################################
# void aesni_ecb_encrypt (const void *in, void *out,
#			  size_t length, const AES_KEY *key,
#			  int enc);
$code.=<<___;
.globl	aesni_ecb_encrypt
.type	aesni_ecb_encrypt,\@function,5
.align	16
aesni_ecb_encrypt:
.cfi_startproc
___
$code.=<<___ if ($win64);
	lea	-0x58(%rsp),%rsp
	movaps	%xmm6,(%rsp)		# offload $inout4..7
	movaps	%xmm7,0x10(%rsp)
	movaps	%xmm8,0x20(%rsp)
	movaps	%xmm9,0x30(%rsp)
.Lecb_enc_body:
___
$code.=<<___;
	and	\$-16,$len		# if ($len<16)
	jz	.Lecb_ret		# return

	mov	240($key),$rounds	# key->rounds
	$movkey	($key),$rndkey0
	mov	$key,$key_		# backup $key
	mov	$rounds,$rnds_		# backup $rounds
	test	%r8d,%r8d		# 5th argument
	jz	.Lecb_decrypt
#--------------------------- ECB ENCRYPT ------------------------------#
	cmp	\$0x80,$len		# if ($len<8*16)
	jb	.Lecb_enc_tail		# short input

	movdqu	($inp),$inout0		# load 8 input blocks
	movdqu	0x10($inp),$inout1
	movdqu	0x20($inp),$inout2
	movdqu	0x30($inp),$inout3
	movdqu	0x40($inp),$inout4
	movdqu	0x50($inp),$inout5
	movdqu	0x60($inp),$inout6
	movdqu	0x70($inp),$inout7
	lea	0x80($inp),$inp		# $inp+=8*16
	sub	\$0x80,$len		# $len-=8*16 (can be zero)
	jmp	.Lecb_enc_loop8_enter
.align 16
.Lecb_enc_loop8:
	movups	$inout0,($out)		# store 8 output blocks
	mov	$key_,$key		# restore $key
	movdqu	($inp),$inout0		# load 8 input blocks
	mov	$rnds_,$rounds		# restore $rounds
	movups	$inout1,0x10($out)
	movdqu	0x10($inp),$inout1
	movups	$inout2,0x20($out)
	movdqu	0x20($inp),$inout2
	movups	$inout3,0x30($out)
	movdqu	0x30($inp),$inout3
	movups	$inout4,0x40($out)
	movdqu	0x40($inp),$inout4
	movups	$inout5,0x50($out)
	movdqu	0x50($inp),$inout5
	movups	$inout6,0x60($out)
	movdqu	0x60($inp),$inout6
	movups	$inout7,0x70($out)
	lea	0x80($out),$out		# $out+=8*16
	movdqu	0x70($inp),$inout7
	lea	0x80($inp),$inp		# $inp+=8*16
.Lecb_enc_loop8_enter:

	call	_aesni_encrypt8

	sub	\$0x80,$len
	jnc	.Lecb_enc_loop8		# loop if $len-=8*16 didn't borrow

	movups	$inout0,($out)		# store 8 output blocks
	mov	$key_,$key		# restore $key
	movups	$inout1,0x10($out)
	mov	$rnds_,$rounds		# restore $rounds
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	movups	$inout5,0x50($out)
	movups	$inout6,0x60($out)
	movups	$inout7,0x70($out)
	lea	0x80($out),$out		# $out+=8*16
	add	\$0x80,$len		# restore real remaining $len
	jz	.Lecb_ret		# done if ($len==0)

.Lecb_enc_tail:				# $len is less than 8*16
	movups	($inp),$inout0
	cmp	\$0x20,$len
	jb	.Lecb_enc_one
	movups	0x10($inp),$inout1
	je	.Lecb_enc_two
	movups	0x20($inp),$inout2
	cmp	\$0x40,$len
	jb	.Lecb_enc_three
	movups	0x30($inp),$inout3
	je	.Lecb_enc_four
	movups	0x40($inp),$inout4
	cmp	\$0x60,$len
	jb	.Lecb_enc_five
	movups	0x50($inp),$inout5
	je	.Lecb_enc_six
	movdqu	0x60($inp),$inout6
	xorps	$inout7,$inout7
	call	_aesni_encrypt8
	movups	$inout0,($out)		# store 7 output blocks
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	movups	$inout5,0x50($out)
	movups	$inout6,0x60($out)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_one:
___
	&aesni_generate1("enc",$key,$rounds);
$code.=<<___;
	movups	$inout0,($out)		# store one output block
	jmp	.Lecb_ret
.align	16
.Lecb_enc_two:
	call	_aesni_encrypt2
	movups	$inout0,($out)		# store 2 output blocks
	movups	$inout1,0x10($out)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_three:
	call	_aesni_encrypt3
	movups	$inout0,($out)		# store 3 output blocks
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_four:
	call	_aesni_encrypt4
	movups	$inout0,($out)		# store 4 output blocks
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_five:
	xorps	$inout5,$inout5
	call	_aesni_encrypt6
	movups	$inout0,($out)		# store 5 output blocks
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	jmp	.Lecb_ret
.align	16
.Lecb_enc_six:
	call	_aesni_encrypt6
	movups	$inout0,($out)		# store 6 output blocks
	movups	$inout1,0x10($out)
	movups	$inout2,0x20($out)
	movups	$inout3,0x30($out)
	movups	$inout4,0x40($out)
	movups	$inout5,0x50($out)
	jmp	.Lecb_ret
#--------------------------- ECB DECRYPT ------------------------------#
.align	16
.Lecb_decrypt:
	cmp	\$0x80,$len		# if ($len<8*16)
	jb	.Lecb_dec_tail		# short input

	movdqu	($inp),$inout0		# load 8 input blocks
	movdqu	0x10($inp),$inout1
	movdqu	0x20($inp),$inout2
	movdqu	0x30($inp),$inout3
	movdqu	0x40($inp),$inout4
	movdqu	0x50($inp),$inout5
	movdqu	0x60($inp),$inout6
	movdqu	0x70($inp),$inout7
	lea	0x80($inp),$inp		# $inp+=8*16
	sub	\$0x80,$len		# $len-=8*16 (can be zero)
	jmp	.Lecb_dec_loop8_enter
.align 16
.Lecb_dec_loop8:
	movups	$inout0,($out)		# store 8 output blocks
	mov	$key_,$key		# restore $key
	movdqu	($inp),$inout0		# load 8 input blocks
	mov	$rnds_,$rounds		# restore $rounds
	movups	$inout1,0x10($out)
	movdqu	0x10($inp),$inout1
	movups	$inout2,0x20($out)
	movdqu	0x20($inp),$inout2
	movups	$inout3,0x30($out)
	movdqu	0x30($inp),$inout3
	movups	$inout4,0x40($out)
	movdqu	0x40($inp),$inout4
	movups	$inout5,0x50($out)
	movdqu	0x50($inp),$inout5
	movups	$inout6,0x60($out)
	movdqu	0x60($inp),$inout6
	movups	$inout7,0x70($out)
	lea	0x80($out),$out		# $out+=8*16
	movdqu	0x70($inp),$inout7
	lea	0x80($inp),$inp		# $inp+=8*16
.Lecb_dec_loop8_enter:

	call	_aesni_decrypt8

	$movkey	($key_),$rndkey0
	sub	\$0x80,$len
	jnc	.Lecb_dec_loop8		# loop if $len-=8*16 didn't borrow

	movups	$inout0,($out)		# store 8 output blocks
	 pxor	$inout0,$inout0		# clear register bank
	mov	$key_,$key		# restore $key
	movups	$inout1,0x10($out)
	 pxor	$inout1,$inout1
	mov	$rnds_,$rounds		# restore $rounds
	movups	$inout2,0x20($out)
	 pxor	$inout2,$inout2
	movups	$inout3,0x30($out)
	 pxor	$inout3,$inout3
	movups	$inout4,0x40($out)
	 pxor	$inout4,$inout4
	movups	$inout5,0x50($out)
	 pxor	$inout5,$inout5
	movups	$inout6,0x60($out)
	 pxor	$inout6,$inout6
	movups	$inout7,0x70($out)
	 pxor	$inout7,$inout7
	lea	0x80($out),$out		# $out+=8*16
	add	\$0x80,$len		# restore real remaining $len
	jz	.Lecb_ret		# done if ($len==0)

.Lecb_dec_tail:
	movups	($inp),$inout0
	cmp	\$0x20,$len
	jb	.Lecb_dec_one
	movups	0x10($inp),$inout1
	je	.Lecb_dec_two
	movups	0x20($inp),$inout2
	cmp	\$0x40,$len
	jb	.Lecb_dec_three
	movups	0x30($inp),$inout3
	je	.Lecb_dec_four
	movups	0x40($inp),$inout4
	cmp	\$0x60,$len
	jb	.Lecb_dec_five
	movups	0x50($inp),$inout5
	je	.Lecb_dec_six
	movups	0x60($inp),$inout6
	$movkey	($key),$rndkey0
	xorps	$inout7,$inout7
	call	_aesni_decrypt8
	movups	$inout0,($out)		# store 7 output blocks
	 pxor	$inout0,$inout0		# clear register bank
	movups	$inout1,0x10($out)
	 pxor	$inout1,$inout1
	movups	$inout2,0x20($out)
	 pxor	$inout2,$inout2
	movups	$inout3,0x30($out)
	 pxor	$inout3,$inout3
	movups	$inout4,0x40($out)
	 pxor	$inout4,$inout4
	movups	$inout5,0x50($out)
	 pxor	$inout5,$inout5
	movups	$inout6,0x60($out)
	 pxor	$inout6,$inout6
	 pxor	$inout7,$inout7
	jmp	.Lecb_ret
.align	16
.Lecb_dec_one:
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	movups	$inout0,($out)		# store one output block
	 pxor	$inout0,$inout0		# clear register bank
	jmp	.Lecb_ret
.align	16
.Lecb_dec_two:
	call	_aesni_decrypt2
	movups	$inout0,($out)		# store 2 output blocks
	 pxor	$inout0,$inout0		# clear register bank
	movups	$inout1,0x10($out)
	 pxor	$inout1,$inout1
	jmp	.Lecb_ret
.align	16
.Lecb_dec_three:
	call	_aesni_decrypt3
	movups	$inout0,($out)		# store 3 output blocks
	 pxor	$inout0,$inout0		# clear register bank
	movups	$inout1,0x10($out)
	 pxor	$inout1,$inout1
	movups	$inout2,0x20($out)
	 pxor	$inout2,$inout2
	jmp	.Lecb_ret
.align	16
.Lecb_dec_four:
	call	_aesni_decrypt4
	movups	$inout0,($out)		# store 4 output blocks
	 pxor	$inout0,$inout0		# clear register bank
	movups	$inout1,0x10($out)
	 pxor	$inout1,$inout1
	movups	$inout2,0x20($out)
	 pxor	$inout2,$inout2
	movups	$inout3,0x30($out)
	 pxor	$inout3,$inout3
	jmp	.Lecb_ret
.align	16
.Lecb_dec_five:
	xorps	$inout5,$inout5
	call	_aesni_decrypt6
	movups	$inout0,($out)		# store 5 output blocks
	 pxor	$inout0,$inout0		# clear register bank
	movups	$inout1,0x10($out)
	 pxor	$inout1,$inout1
	movups	$inout2,0x20($out)
	 pxor	$inout2,$inout2
	movups	$inout3,0x30($out)
	 pxor	$inout3,$inout3
	movups	$inout4,0x40($out)
	 pxor	$inout4,$inout4
	 pxor	$inout5,$inout5
	jmp	.Lecb_ret
.align	16
.Lecb_dec_six:
	call	_aesni_decrypt6
	movups	$inout0,($out)		# store 6 output blocks
	 pxor	$inout0,$inout0		# clear register bank
	movups	$inout1,0x10($out)
	 pxor	$inout1,$inout1
	movups	$inout2,0x20($out)
	 pxor	$inout2,$inout2
	movups	$inout3,0x30($out)
	 pxor	$inout3,$inout3
	movups	$inout4,0x40($out)
	 pxor	$inout4,$inout4
	movups	$inout5,0x50($out)
	 pxor	$inout5,$inout5

.Lecb_ret:
	xorps	$rndkey0,$rndkey0	# %xmm0
	pxor	$rndkey1,$rndkey1
___
$code.=<<___ if ($win64);
	movaps	(%rsp),%xmm6
	movaps	%xmm0,(%rsp)		# clear stack
	movaps	0x10(%rsp),%xmm7
	movaps	%xmm0,0x10(%rsp)
	movaps	0x20(%rsp),%xmm8
	movaps	%xmm0,0x20(%rsp)
	movaps	0x30(%rsp),%xmm9
	movaps	%xmm0,0x30(%rsp)
	lea	0x58(%rsp),%rsp
.Lecb_enc_ret:
___
$code.=<<___;
	ret
.cfi_endproc
.size	aesni_ecb_encrypt,.-aesni_ecb_encrypt
___

{
######################################################################
# void aesni_ccm64_[en|de]crypt_blocks (const void *in, void *out,
#                         size_t blocks, const AES_KEY *key,
#                         const char *ivec,char *cmac);
#
# Handles only complete blocks, operates on 64-bit counter and
# does not update *ivec! Nor does it finalize CMAC value
# (see engine/eng_aesni.c for details)
#
{
my $cmac="%r9";	# 6th argument

my $increment="%xmm9";
my $iv="%xmm6";
my $bswap_mask="%xmm7";

$code.=<<___;
.globl	aesni_ccm64_encrypt_blocks
.type	aesni_ccm64_encrypt_blocks,\@function,6
.align	16
aesni_ccm64_encrypt_blocks:
___
$code.=<<___ if ($win64);
	lea	-0x58(%rsp),%rsp
	movaps	%xmm6,(%rsp)		# $iv
	movaps	%xmm7,0x10(%rsp)	# $bswap_mask
	movaps	%xmm8,0x20(%rsp)	# $in0
	movaps	%xmm9,0x30(%rsp)	# $increment
.Lccm64_enc_body:
___
$code.=<<___;
	mov	240($key),$rounds		# key->rounds
	movdqu	($ivp),$iv
	movdqa	.Lincrement64(%rip),$increment
	movdqa	.Lbswap_mask(%rip),$bswap_mask

	shl	\$4,$rounds
	mov	\$16,$rnds_
	lea	0($key),$key_
	movdqu	($cmac),$inout1
	movdqa	$iv,$inout0
	lea	32($key,$rounds),$key		# end of key schedule
	pshufb	$bswap_mask,$iv
	sub	%rax,%r10			# twisted $rounds
	jmp	.Lccm64_enc_outer
.align	16
.Lccm64_enc_outer:
	$movkey	($key_),$rndkey0
	mov	%r10,%rax
	movups	($inp),$in0			# load inp

	xorps	$rndkey0,$inout0		# counter
	$movkey	16($key_),$rndkey1
	xorps	$in0,$rndkey0
	xorps	$rndkey0,$inout1		# cmac^=inp
	$movkey	32($key_),$rndkey0

.Lccm64_enc2_loop:
	aesenc	$rndkey1,$inout0
	aesenc	$rndkey1,$inout1
	$movkey	($key,%rax),$rndkey1
	add	\$32,%rax
	aesenc	$rndkey0,$inout0
	aesenc	$rndkey0,$inout1
	$movkey	-16($key,%rax),$rndkey0
	jnz	.Lccm64_enc2_loop
	aesenc	$rndkey1,$inout0
	aesenc	$rndkey1,$inout1
	paddq	$increment,$iv
	dec	$len				# $len-- ($len is in blocks)
	aesenclast	$rndkey0,$inout0
	aesenclast	$rndkey0,$inout1

	lea	16($inp),$inp
	xorps	$inout0,$in0			# inp ^= E(iv)
	movdqa	$iv,$inout0
	movups	$in0,($out)			# save output
	pshufb	$bswap_mask,$inout0
	lea	16($out),$out			# $out+=16
	jnz	.Lccm64_enc_outer		# loop if ($len!=0)

	 pxor	$rndkey0,$rndkey0		# clear register bank
	 pxor	$rndkey1,$rndkey1
	 pxor	$inout0,$inout0
	movups	$inout1,($cmac)			# store resulting mac
	 pxor	$inout1,$inout1
	 pxor	$in0,$in0
	 pxor	$iv,$iv
___
$code.=<<___ if ($win64);
	movaps	(%rsp),%xmm6
	movaps	%xmm0,(%rsp)			# clear stack
	movaps	0x10(%rsp),%xmm7
	movaps	%xmm0,0x10(%rsp)
	movaps	0x20(%rsp),%xmm8
	movaps	%xmm0,0x20(%rsp)
	movaps	0x30(%rsp),%xmm9
	movaps	%xmm0,0x30(%rsp)
	lea	0x58(%rsp),%rsp
.Lccm64_enc_ret:
___
$code.=<<___;
	ret
.size	aesni_ccm64_encrypt_blocks,.-aesni_ccm64_encrypt_blocks
___
######################################################################
$code.=<<___;
.globl	aesni_ccm64_decrypt_blocks
.type	aesni_ccm64_decrypt_blocks,\@function,6
.align	16
aesni_ccm64_decrypt_blocks:
___
$code.=<<___ if ($win64);
	lea	-0x58(%rsp),%rsp
	movaps	%xmm6,(%rsp)		# $iv
	movaps	%xmm7,0x10(%rsp)	# $bswap_mask
	movaps	%xmm8,0x20(%rsp)	# $in8
	movaps	%xmm9,0x30(%rsp)	# $increment
.Lccm64_dec_body:
___
$code.=<<___;
	mov	240($key),$rounds		# key->rounds
	movups	($ivp),$iv
	movdqu	($cmac),$inout1
	movdqa	.Lincrement64(%rip),$increment
	movdqa	.Lbswap_mask(%rip),$bswap_mask

	movaps	$iv,$inout0
	mov	$rounds,$rnds_
	mov	$key,$key_
	pshufb	$bswap_mask,$iv
___
	&aesni_generate1("enc",$key,$rounds);
$code.=<<___;
	shl	\$4,$rnds_
	mov	\$16,$rounds
	movups	($inp),$in0			# load inp
	paddq	$increment,$iv
	lea	16($inp),$inp			# $inp+=16
	sub	%r10,%rax			# twisted $rounds
	lea	32($key_,$rnds_),$key		# end of key schedule
	mov	%rax,%r10
	jmp	.Lccm64_dec_outer
.align	16
.Lccm64_dec_outer:
	xorps	$inout0,$in0			# inp ^= E(iv)
	movdqa	$iv,$inout0
	movups	$in0,($out)			# save output
	lea	16($out),$out			# $out+=16
	pshufb	$bswap_mask,$inout0

	sub	\$1,$len			# $len-- ($len is in blocks)
	jz	.Lccm64_dec_break		# if ($len==0) break

	$movkey	($key_),$rndkey0
	mov	%r10,%rax
	$movkey	16($key_),$rndkey1
	xorps	$rndkey0,$in0
	xorps	$rndkey0,$inout0
	xorps	$in0,$inout1			# cmac^=out
	$movkey	32($key_),$rndkey0
	jmp	.Lccm64_dec2_loop
.align	16
.Lccm64_dec2_loop:
	aesenc	$rndkey1,$inout0
	aesenc	$rndkey1,$inout1
	$movkey	($key,%rax),$rndkey1
	add	\$32,%rax
	aesenc	$rndkey0,$inout0
	aesenc	$rndkey0,$inout1
	$movkey	-16($key,%rax),$rndkey0
	jnz	.Lccm64_dec2_loop
	movups	($inp),$in0			# load input
	paddq	$increment,$iv
	aesenc	$rndkey1,$inout0
	aesenc	$rndkey1,$inout1
	aesenclast	$rndkey0,$inout0
	aesenclast	$rndkey0,$inout1
	lea	16($inp),$inp			# $inp+=16
	jmp	.Lccm64_dec_outer

.align	16
.Lccm64_dec_break:
	#xorps	$in0,$inout1			# cmac^=out
	mov	240($key_),$rounds
___
	&aesni_generate1("enc",$key_,$rounds,$inout1,$in0);
$code.=<<___;
	 pxor	$rndkey0,$rndkey0		# clear register bank
	 pxor	$rndkey1,$rndkey1
	 pxor	$inout0,$inout0
	movups	$inout1,($cmac)			# store resulting mac
	 pxor	$inout1,$inout1
	 pxor	$in0,$in0
	 pxor	$iv,$iv
___
$code.=<<___ if ($win64);
	movaps	(%rsp),%xmm6
	movaps	%xmm0,(%rsp)			# clear stack
	movaps	0x10(%rsp),%xmm7
	movaps	%xmm0,0x10(%rsp)
	movaps	0x20(%rsp),%xmm8
	movaps	%xmm0,0x20(%rsp)
	movaps	0x30(%rsp),%xmm9
	movaps	%xmm0,0x30(%rsp)
	lea	0x58(%rsp),%rsp
.Lccm64_dec_ret:
___
$code.=<<___;
	ret
.size	aesni_ccm64_decrypt_blocks,.-aesni_ccm64_decrypt_blocks
___
}
######################################################################
# void aesni_ctr32_encrypt_blocks (const void *in, void *out,
#                         size_t blocks, const AES_KEY *key,
#                         const char *ivec);
#
# Handles only complete blocks, operates on 32-bit counter and
# does not update *ivec! (see crypto/modes/ctr128.c for details)
#
# Overhaul based on suggestions from Shay Gueron and Vlad Krasnov,
# http://rt.openssl.org/Ticket/Display.html?id=3021&user=guest&pass=guest.
# Keywords are full unroll and modulo-schedule counter calculations
# with zero-round key xor.
{
my ($in0,$in1,$in2,$in3,$in4,$in5)=map("%xmm$_",(10..15));
my ($key0,$ctr)=("%ebp","${ivp}d");
my $frame_size = 0x80 + ($win64?160:0);

$code.=<<___;
.globl	aesni_ctr32_encrypt_blocks
.type	aesni_ctr32_encrypt_blocks,\@function,5
.align	16
aesni_ctr32_encrypt_blocks:
.cfi_startproc
	cmp	\$1,$len
	jne	.Lctr32_bulk

	# handle single block without allocating stack frame,
	# useful when handling edges
	movups	($ivp),$inout0
	movups	($inp),$inout1
	mov	240($key),%edx			# key->rounds
___
	&aesni_generate1("enc",$key,"%edx");
$code.=<<___;
	 pxor	$rndkey0,$rndkey0		# clear register bank
	 pxor	$rndkey1,$rndkey1
	xorps	$inout1,$inout0
	 pxor	$inout1,$inout1
	movups	$inout0,($out)
	 xorps	$inout0,$inout0
	jmp	.Lctr32_epilogue

.align	16
.Lctr32_bulk:
	lea	(%rsp),$key_			# use $key_ as frame pointer
.cfi_def_cfa_register	$key_
	push	%rbp
.cfi_push	%rbp
	sub	\$$frame_size,%rsp
	and	\$-16,%rsp	# Linux kernel stack can be incorrectly seeded
___
$code.=<<___ if ($win64);
	movaps	%xmm6,-0xa8($key_)		# offload everything
	movaps	%xmm7,-0x98($key_)
	movaps	%xmm8,-0x88($key_)
	movaps	%xmm9,-0x78($key_)
	movaps	%xmm10,-0x68($key_)
	movaps	%xmm11,-0x58($key_)
	movaps	%xmm12,-0x48($key_)
	movaps	%xmm13,-0x38($key_)
	movaps	%xmm14,-0x28($key_)
	movaps	%xmm15,-0x18($key_)
.Lctr32_body:
___
$code.=<<___;

	# 8 16-byte words on top of stack are counter values
	# xor-ed with zero-round key

	movdqu	($ivp),$inout0
	movdqu	($key),$rndkey0
	mov	12($ivp),$ctr			# counter LSB
	pxor	$rndkey0,$inout0
	mov	12($key),$key0			# 0-round key LSB
	movdqa	$inout0,0x00(%rsp)		# populate counter block
	bswap	$ctr
	movdqa	$inout0,$inout1
	movdqa	$inout0,$inout2
	movdqa	$inout0,$inout3
	movdqa	$inout0,0x40(%rsp)
	movdqa	$inout0,0x50(%rsp)
	movdqa	$inout0,0x60(%rsp)
	mov	%rdx,%r10			# about to borrow %rdx
	movdqa	$inout0,0x70(%rsp)

	lea	1($ctr),%rax
	 lea	2($ctr),%rdx
	bswap	%eax
	 bswap	%edx
	xor	$key0,%eax
	 xor	$key0,%edx
	pinsrd	\$3,%eax,$inout1
	lea	3($ctr),%rax
	movdqa	$inout1,0x10(%rsp)
	 pinsrd	\$3,%edx,$inout2
	bswap	%eax
	 mov	%r10,%rdx			# restore %rdx
	 lea	4($ctr),%r10
	 movdqa	$inout2,0x20(%rsp)
	xor	$key0,%eax
	 bswap	%r10d
	pinsrd	\$3,%eax,$inout3
	 xor	$key0,%r10d
	movdqa	$inout3,0x30(%rsp)
	lea	5($ctr),%r9
	 mov	%r10d,0x40+12(%rsp)
	bswap	%r9d
	 lea	6($ctr),%r10
	mov	240($key),$rounds		# key->rounds
	xor	$key0,%r9d
	 bswap	%r10d
	mov	%r9d,0x50+12(%rsp)
	 xor	$key0,%r10d
	lea	7($ctr),%r9
	 mov	%r10d,0x60+12(%rsp)
	bswap	%r9d
	 mov	OPENSSL_ia32cap_P+4(%rip),%r10d
	xor	$key0,%r9d
	 and	\$`1<<26|1<<22`,%r10d		# isolate XSAVE+MOVBE
	mov	%r9d,0x70+12(%rsp)

	$movkey	0x10($key),$rndkey1

	movdqa	0x40(%rsp),$inout4
	movdqa	0x50(%rsp),$inout5

	cmp	\$8,$len		# $len is in blocks
	jb	.Lctr32_tail		# short input if ($len<8)

	sub	\$6,$len		# $len is biased by -6
	cmp	\$`1<<22`,%r10d		# check for MOVBE without XSAVE
	je	.Lctr32_6x		# [which denotes Atom Silvermont]

	lea	0x80($key),$key		# size optimization
	sub	\$2,$len		# $len is biased by -8
	jmp	.Lctr32_loop8

.align	16
.Lctr32_6x:
	shl	\$4,$rounds
	mov	\$48,$rnds_
	bswap	$key0
	lea	32($key,$rounds),$key	# end of key schedule
	sub	%rax,%r10		# twisted $rounds
	jmp	.Lctr32_loop6

.align	16
.Lctr32_loop6:
	 add	\$6,$ctr		# next counter value
	$movkey	-48($key,$rnds_),$rndkey0
	aesenc	$rndkey1,$inout0
	 mov	$ctr,%eax
	 xor	$key0,%eax
	aesenc	$rndkey1,$inout1
	 movbe	%eax,`0x00+12`(%rsp)	# store next counter value
	 lea	1($ctr),%eax
	aesenc	$rndkey1,$inout2
	 xor	$key0,%eax
	 movbe	%eax,`0x10+12`(%rsp)
	aesenc	$rndkey1,$inout3
	 lea	2($ctr),%eax
	 xor	$key0,%eax
	aesenc	$rndkey1,$inout4
	 movbe	%eax,`0x20+12`(%rsp)
	 lea	3($ctr),%eax
	aesenc	$rndkey1,$inout5
	$movkey	-32($key,$rnds_),$rndkey1
	 xor	$key0,%eax

	aesenc	$rndkey0,$inout0
	 movbe	%eax,`0x30+12`(%rsp)
	 lea	4($ctr),%eax
	aesenc	$rndkey0,$inout1
	 xor	$key0,%eax
	 movbe	%eax,`0x40+12`(%rsp)
	aesenc	$rndkey0,$inout2
	 lea	5($ctr),%eax
	 xor	$key0,%eax
	aesenc	$rndkey0,$inout3
	 movbe	%eax,`0x50+12`(%rsp)
	 mov	%r10,%rax		# mov	$rnds_,$rounds
	aesenc	$rndkey0,$inout4
	aesenc	$rndkey0,$inout5
	$movkey	-16($key,$rnds_),$rndkey0

	call	.Lenc_loop6

	movdqu	($inp),$inout6		# load 6 input blocks
	movdqu	0x10($inp),$inout7
	movdqu	0x20($inp),$in0
	movdqu	0x30($inp),$in1
	movdqu	0x40($inp),$in2
	movdqu	0x50($inp),$in3
	lea	0x60($inp),$inp		# $inp+=6*16
	$movkey	-64($key,$rnds_),$rndkey1
	pxor	$inout0,$inout6		# inp^=E(ctr)
	movaps	0x00(%rsp),$inout0	# load next counter [xor-ed with 0 round]
	pxor	$inout1,$inout7
	movaps	0x10(%rsp),$inout1
	pxor	$inout2,$in0
	movaps	0x20(%rsp),$inout2
	pxor	$inout3,$in1
	movaps	0x30(%rsp),$inout3
	pxor	$inout4,$in2
	movaps	0x40(%rsp),$inout4
	pxor	$inout5,$in3
	movaps	0x50(%rsp),$inout5
	movdqu	$inout6,($out)		# store 6 output blocks
	movdqu	$inout7,0x10($out)
	movdqu	$in0,0x20($out)
	movdqu	$in1,0x30($out)
	movdqu	$in2,0x40($out)
	movdqu	$in3,0x50($out)
	lea	0x60($out),$out		# $out+=6*16

	sub	\$6,$len
	jnc	.Lctr32_loop6		# loop if $len-=6 didn't borrow

	add	\$6,$len		# restore real remaining $len
	jz	.Lctr32_done		# done if ($len==0)

	lea	-48($rnds_),$rounds
	lea	-80($key,$rnds_),$key	# restore $key
	neg	$rounds
	shr	\$4,$rounds		# restore $rounds
	jmp	.Lctr32_tail

.align	32
.Lctr32_loop8:
	 add		\$8,$ctr		# next counter value
	movdqa		0x60(%rsp),$inout6
	aesenc		$rndkey1,$inout0
	 mov		$ctr,%r9d
	movdqa		0x70(%rsp),$inout7
	aesenc		$rndkey1,$inout1
	 bswap		%r9d
	$movkey		0x20-0x80($key),$rndkey0
	aesenc		$rndkey1,$inout2
	 xor		$key0,%r9d
	 nop
	aesenc		$rndkey1,$inout3
	 mov		%r9d,0x00+12(%rsp)	# store next counter value
	 lea		1($ctr),%r9
	aesenc		$rndkey1,$inout4
	aesenc		$rndkey1,$inout5
	aesenc		$rndkey1,$inout6
	aesenc		$rndkey1,$inout7
	$movkey		0x30-0x80($key),$rndkey1
___
for($i=2;$i<8;$i++) {
my $rndkeyx = ($i&1)?$rndkey1:$rndkey0;
$code.=<<___;
	 bswap		%r9d
	aesenc		$rndkeyx,$inout0
	aesenc		$rndkeyx,$inout1
	 xor		$key0,%r9d
	 .byte		0x66,0x90
	aesenc		$rndkeyx,$inout2
	aesenc		$rndkeyx,$inout3
	 mov		%r9d,`0x10*($i-1)`+12(%rsp)
	 lea		$i($ctr),%r9
	aesenc		$rndkeyx,$inout4
	aesenc		$rndkeyx,$inout5
	aesenc		$rndkeyx,$inout6
	aesenc		$rndkeyx,$inout7
	$movkey		`0x20+0x10*$i`-0x80($key),$rndkeyx
___
}
$code.=<<___;
	 bswap		%r9d
	aesenc		$rndkey0,$inout0
	aesenc		$rndkey0,$inout1
	aesenc		$rndkey0,$inout2
	 xor		$key0,%r9d
	 movdqu		0x00($inp),$in0		# start loading input
	aesenc		$rndkey0,$inout3
	 mov		%r9d,0x70+12(%rsp)
	 cmp		\$11,$rounds
	aesenc		$rndkey0,$inout4
	aesenc		$rndkey0,$inout5
	aesenc		$rndkey0,$inout6
	aesenc		$rndkey0,$inout7
	$movkey		0xa0-0x80($key),$rndkey0

	jb		.Lctr32_enc_done

	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	aesenc		$rndkey1,$inout4
	aesenc		$rndkey1,$inout5
	aesenc		$rndkey1,$inout6
	aesenc		$rndkey1,$inout7
	$movkey		0xb0-0x80($key),$rndkey1

	aesenc		$rndkey0,$inout0
	aesenc		$rndkey0,$inout1
	aesenc		$rndkey0,$inout2
	aesenc		$rndkey0,$inout3
	aesenc		$rndkey0,$inout4
	aesenc		$rndkey0,$inout5
	aesenc		$rndkey0,$inout6
	aesenc		$rndkey0,$inout7
	$movkey		0xc0-0x80($key),$rndkey0
	je		.Lctr32_enc_done

	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	aesenc		$rndkey1,$inout4
	aesenc		$rndkey1,$inout5
	aesenc		$rndkey1,$inout6
	aesenc		$rndkey1,$inout7
	$movkey		0xd0-0x80($key),$rndkey1

	aesenc		$rndkey0,$inout0
	aesenc		$rndkey0,$inout1
	aesenc		$rndkey0,$inout2
	aesenc		$rndkey0,$inout3
	aesenc		$rndkey0,$inout4
	aesenc		$rndkey0,$inout5
	aesenc		$rndkey0,$inout6
	aesenc		$rndkey0,$inout7
	$movkey		0xe0-0x80($key),$rndkey0
	jmp		.Lctr32_enc_done

.align	16
.Lctr32_enc_done:
	movdqu		0x10($inp),$in1
	pxor		$rndkey0,$in0		# input^=round[last]
	movdqu		0x20($inp),$in2
	pxor		$rndkey0,$in1
	movdqu		0x30($inp),$in3
	pxor		$rndkey0,$in2
	movdqu		0x40($inp),$in4
	pxor		$rndkey0,$in3
	movdqu		0x50($inp),$in5
	pxor		$rndkey0,$in4
	pxor		$rndkey0,$in5
	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	aesenc		$rndkey1,$inout4
	aesenc		$rndkey1,$inout5
	aesenc		$rndkey1,$inout6
	aesenc		$rndkey1,$inout7
	movdqu		0x60($inp),$rndkey1	# borrow $rndkey1 for inp[6]
	lea		0x80($inp),$inp		# $inp+=8*16

	aesenclast	$in0,$inout0		# $inN is inp[N]^round[last]
	pxor		$rndkey0,$rndkey1	# borrowed $rndkey
	movdqu		0x70-0x80($inp),$in0
	aesenclast	$in1,$inout1
	pxor		$rndkey0,$in0
	movdqa		0x00(%rsp),$in1		# load next counter block
	aesenclast	$in2,$inout2
	aesenclast	$in3,$inout3
	movdqa		0x10(%rsp),$in2
	movdqa		0x20(%rsp),$in3
	aesenclast	$in4,$inout4
	aesenclast	$in5,$inout5
	movdqa		0x30(%rsp),$in4
	movdqa		0x40(%rsp),$in5
	aesenclast	$rndkey1,$inout6
	movdqa		0x50(%rsp),$rndkey0
	$movkey		0x10-0x80($key),$rndkey1#real 1st-round key
	aesenclast	$in0,$inout7

	movups		$inout0,($out)		# store 8 output blocks
	movdqa		$in1,$inout0
	movups		$inout1,0x10($out)
	movdqa		$in2,$inout1
	movups		$inout2,0x20($out)
	movdqa		$in3,$inout2
	movups		$inout3,0x30($out)
	movdqa		$in4,$inout3
	movups		$inout4,0x40($out)
	movdqa		$in5,$inout4
	movups		$inout5,0x50($out)
	movdqa		$rndkey0,$inout5
	movups		$inout6,0x60($out)
	movups		$inout7,0x70($out)
	lea		0x80($out),$out		# $out+=8*16

	sub	\$8,$len
	jnc	.Lctr32_loop8			# loop if $len-=8 didn't borrow

	add	\$8,$len			# restore real remaining $len
	jz	.Lctr32_done			# done if ($len==0)
	lea	-0x80($key),$key

.Lctr32_tail:
	# note that at this point $inout0..5 are populated with
	# counter values xor-ed with 0-round key
	lea	16($key),$key
	cmp	\$4,$len
	jb	.Lctr32_loop3
	je	.Lctr32_loop4

	# if ($len>4) compute 7 E(counter)
	shl		\$4,$rounds
	movdqa		0x60(%rsp),$inout6
	pxor		$inout7,$inout7

	$movkey		16($key),$rndkey0
	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	lea		32-16($key,$rounds),$key# prepare for .Lenc_loop8_enter
	neg		%rax
	aesenc		$rndkey1,$inout2
	add		\$16,%rax		# prepare for .Lenc_loop8_enter
	 movups		($inp),$in0
	aesenc		$rndkey1,$inout3
	aesenc		$rndkey1,$inout4
	 movups		0x10($inp),$in1		# pre-load input
	 movups		0x20($inp),$in2
	aesenc		$rndkey1,$inout5
	aesenc		$rndkey1,$inout6

	call            .Lenc_loop8_enter

	movdqu	0x30($inp),$in3
	pxor	$in0,$inout0
	movdqu	0x40($inp),$in0
	pxor	$in1,$inout1
	movdqu	$inout0,($out)			# store output
	pxor	$in2,$inout2
	movdqu	$inout1,0x10($out)
	pxor	$in3,$inout3
	movdqu	$inout2,0x20($out)
	pxor	$in0,$inout4
	movdqu	$inout3,0x30($out)
	movdqu	$inout4,0x40($out)
	cmp	\$6,$len
	jb	.Lctr32_done			# $len was 5, stop store

	movups	0x50($inp),$in1
	xorps	$in1,$inout5
	movups	$inout5,0x50($out)
	je	.Lctr32_done			# $len was 6, stop store

	movups	0x60($inp),$in2
	xorps	$in2,$inout6
	movups	$inout6,0x60($out)
	jmp	.Lctr32_done			# $len was 7, stop store

.align	32
.Lctr32_loop4:
	aesenc		$rndkey1,$inout0
	lea		16($key),$key
	dec		$rounds
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	$movkey		($key),$rndkey1
	jnz		.Lctr32_loop4
	aesenclast	$rndkey1,$inout0
	aesenclast	$rndkey1,$inout1
	 movups		($inp),$in0		# load input
	 movups		0x10($inp),$in1
	aesenclast	$rndkey1,$inout2
	aesenclast	$rndkey1,$inout3
	 movups		0x20($inp),$in2
	 movups		0x30($inp),$in3

	xorps	$in0,$inout0
	movups	$inout0,($out)			# store output
	xorps	$in1,$inout1
	movups	$inout1,0x10($out)
	pxor	$in2,$inout2
	movdqu	$inout2,0x20($out)
	pxor	$in3,$inout3
	movdqu	$inout3,0x30($out)
	jmp	.Lctr32_done			# $len was 4, stop store

.align	32
.Lctr32_loop3:
	aesenc		$rndkey1,$inout0
	lea		16($key),$key
	dec		$rounds
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	$movkey		($key),$rndkey1
	jnz		.Lctr32_loop3
	aesenclast	$rndkey1,$inout0
	aesenclast	$rndkey1,$inout1
	aesenclast	$rndkey1,$inout2

	movups	($inp),$in0			# load input
	xorps	$in0,$inout0
	movups	$inout0,($out)			# store output
	cmp	\$2,$len
	jb	.Lctr32_done			# $len was 1, stop store

	movups	0x10($inp),$in1
	xorps	$in1,$inout1
	movups	$inout1,0x10($out)
	je	.Lctr32_done			# $len was 2, stop store

	movups	0x20($inp),$in2
	xorps	$in2,$inout2
	movups	$inout2,0x20($out)		# $len was 3, stop store

.Lctr32_done:
	xorps	%xmm0,%xmm0			# clear register bank
	xor	$key0,$key0
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
___
$code.=<<___ if (!$win64);
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	movaps	%xmm0,0x00(%rsp)		# clear stack
	pxor	%xmm8,%xmm8
	movaps	%xmm0,0x10(%rsp)
	pxor	%xmm9,%xmm9
	movaps	%xmm0,0x20(%rsp)
	pxor	%xmm10,%xmm10
	movaps	%xmm0,0x30(%rsp)
	pxor	%xmm11,%xmm11
	movaps	%xmm0,0x40(%rsp)
	pxor	%xmm12,%xmm12
	movaps	%xmm0,0x50(%rsp)
	pxor	%xmm13,%xmm13
	movaps	%xmm0,0x60(%rsp)
	pxor	%xmm14,%xmm14
	movaps	%xmm0,0x70(%rsp)
	pxor	%xmm15,%xmm15
___
$code.=<<___ if ($win64);
	movaps	-0xa8($key_),%xmm6
	movaps	%xmm0,-0xa8($key_)		# clear stack
	movaps	-0x98($key_),%xmm7
	movaps	%xmm0,-0x98($key_)
	movaps	-0x88($key_),%xmm8
	movaps	%xmm0,-0x88($key_)
	movaps	-0x78($key_),%xmm9
	movaps	%xmm0,-0x78($key_)
	movaps	-0x68($key_),%xmm10
	movaps	%xmm0,-0x68($key_)
	movaps	-0x58($key_),%xmm11
	movaps	%xmm0,-0x58($key_)
	movaps	-0x48($key_),%xmm12
	movaps	%xmm0,-0x48($key_)
	movaps	-0x38($key_),%xmm13
	movaps	%xmm0,-0x38($key_)
	movaps	-0x28($key_),%xmm14
	movaps	%xmm0,-0x28($key_)
	movaps	-0x18($key_),%xmm15
	movaps	%xmm0,-0x18($key_)
	movaps	%xmm0,0x00(%rsp)
	movaps	%xmm0,0x10(%rsp)
	movaps	%xmm0,0x20(%rsp)
	movaps	%xmm0,0x30(%rsp)
	movaps	%xmm0,0x40(%rsp)
	movaps	%xmm0,0x50(%rsp)
	movaps	%xmm0,0x60(%rsp)
	movaps	%xmm0,0x70(%rsp)
___
$code.=<<___;
	mov	-8($key_),%rbp
.cfi_restore	%rbp
	lea	($key_),%rsp
.cfi_def_cfa_register	%rsp
.Lctr32_epilogue:
	ret
.cfi_endproc
.size	aesni_ctr32_encrypt_blocks,.-aesni_ctr32_encrypt_blocks
___
}

######################################################################
# void aesni_xts_[en|de]crypt(const char *inp,char *out,size_t len,
#	const AES_KEY *key1, const AES_KEY *key2
#	const unsigned char iv[16]);
#
{
my @tweak=map("%xmm$_",(10..15));
my ($twmask,$twres,$twtmp)=("%xmm8","%xmm9",@tweak[4]);
my ($key2,$ivp,$len_)=("%r8","%r9","%r9");
my $frame_size = 0x70 + ($win64?160:0);
my $key_ = "%rbp";	# override so that we can use %r11 as FP

$code.=<<___;
.globl	aesni_xts_encrypt
.type	aesni_xts_encrypt,\@function,6
.align	16
aesni_xts_encrypt:
.cfi_startproc
	lea	(%rsp),%r11			# frame pointer
.cfi_def_cfa_register	%r11
	push	%rbp
.cfi_push	%rbp
	sub	\$$frame_size,%rsp
	and	\$-16,%rsp	# Linux kernel stack can be incorrectly seeded
___
$code.=<<___ if ($win64);
	movaps	%xmm6,-0xa8(%r11)		# offload everything
	movaps	%xmm7,-0x98(%r11)
	movaps	%xmm8,-0x88(%r11)
	movaps	%xmm9,-0x78(%r11)
	movaps	%xmm10,-0x68(%r11)
	movaps	%xmm11,-0x58(%r11)
	movaps	%xmm12,-0x48(%r11)
	movaps	%xmm13,-0x38(%r11)
	movaps	%xmm14,-0x28(%r11)
	movaps	%xmm15,-0x18(%r11)
.Lxts_enc_body:
___
$code.=<<___;
	movups	($ivp),$inout0			# load clear-text tweak
	mov	240(%r8),$rounds		# key2->rounds
	mov	240($key),$rnds_		# key1->rounds
___
	# generate the tweak
	&aesni_generate1("enc",$key2,$rounds,$inout0);
$code.=<<___;
	$movkey	($key),$rndkey0			# zero round key
	mov	$key,$key_			# backup $key
	mov	$rnds_,$rounds			# backup $rounds
	shl	\$4,$rnds_
	mov	$len,$len_			# backup $len
	and	\$-16,$len

	$movkey	16($key,$rnds_),$rndkey1	# last round key

	movdqa	.Lxts_magic(%rip),$twmask
	movdqa	$inout0,@tweak[5]
	pshufd	\$0x5f,$inout0,$twres
	pxor	$rndkey0,$rndkey1
___
    # alternative tweak calculation algorithm is based on suggestions
    # by Shay Gueron. psrad doesn't conflict with AES-NI instructions
    # and should help in the future...
    for ($i=0;$i<4;$i++) {
    $code.=<<___;
	movdqa	$twres,$twtmp
	paddd	$twres,$twres
	movdqa	@tweak[5],@tweak[$i]
	psrad	\$31,$twtmp			# broadcast upper bits
	paddq	@tweak[5],@tweak[5]
	pand	$twmask,$twtmp
	pxor	$rndkey0,@tweak[$i]
	pxor	$twtmp,@tweak[5]
___
    }
$code.=<<___;
	movdqa	@tweak[5],@tweak[4]
	psrad	\$31,$twres
	paddq	@tweak[5],@tweak[5]
	pand	$twmask,$twres
	pxor	$rndkey0,@tweak[4]
	pxor	$twres,@tweak[5]
	movaps	$rndkey1,0x60(%rsp)		# save round[0]^round[last]

	sub	\$16*6,$len
	jc	.Lxts_enc_short			# if $len-=6*16 borrowed

	mov	\$16+96,$rounds
	lea	32($key_,$rnds_),$key		# end of key schedule
	sub	%r10,%rax			# twisted $rounds
	$movkey	16($key_),$rndkey1
	mov	%rax,%r10			# backup twisted $rounds
	lea	.Lxts_magic(%rip),%r8
	jmp	.Lxts_enc_grandloop

.align	32
.Lxts_enc_grandloop:
	movdqu	`16*0`($inp),$inout0		# load input
	movdqa	$rndkey0,$twmask
	movdqu	`16*1`($inp),$inout1
	pxor	@tweak[0],$inout0		# input^=tweak^round[0]
	movdqu	`16*2`($inp),$inout2
	pxor	@tweak[1],$inout1
	 aesenc		$rndkey1,$inout0
	movdqu	`16*3`($inp),$inout3
	pxor	@tweak[2],$inout2
	 aesenc		$rndkey1,$inout1
	movdqu	`16*4`($inp),$inout4
	pxor	@tweak[3],$inout3
	 aesenc		$rndkey1,$inout2
	movdqu	`16*5`($inp),$inout5
	pxor	@tweak[5],$twmask		# round[0]^=tweak[5]
	 movdqa	0x60(%rsp),$twres		# load round[0]^round[last]
	pxor	@tweak[4],$inout4
	 aesenc		$rndkey1,$inout3
	$movkey	32($key_),$rndkey0
	lea	`16*6`($inp),$inp
	pxor	$twmask,$inout5

	 pxor	$twres,@tweak[0]		# calculate tweaks^round[last]
	aesenc		$rndkey1,$inout4
	 pxor	$twres,@tweak[1]
	 movdqa	@tweak[0],`16*0`(%rsp)		# put aside tweaks^round[last]
	aesenc		$rndkey1,$inout5
	$movkey		48($key_),$rndkey1
	 pxor	$twres,@tweak[2]

	aesenc		$rndkey0,$inout0
	 pxor	$twres,@tweak[3]
	 movdqa	@tweak[1],`16*1`(%rsp)
	aesenc		$rndkey0,$inout1
	 pxor	$twres,@tweak[4]
	 movdqa	@tweak[2],`16*2`(%rsp)
	aesenc		$rndkey0,$inout2
	aesenc		$rndkey0,$inout3
	 pxor	$twres,$twmask
	 movdqa	@tweak[4],`16*4`(%rsp)
	aesenc		$rndkey0,$inout4
	aesenc		$rndkey0,$inout5
	$movkey		64($key_),$rndkey0
	 movdqa	$twmask,`16*5`(%rsp)
	pshufd	\$0x5f,@tweak[5],$twres
	jmp	.Lxts_enc_loop6
.align	32
.Lxts_enc_loop6:
	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	aesenc		$rndkey1,$inout4
	aesenc		$rndkey1,$inout5
	$movkey		-64($key,%rax),$rndkey1
	add		\$32,%rax

	aesenc		$rndkey0,$inout0
	aesenc		$rndkey0,$inout1
	aesenc		$rndkey0,$inout2
	aesenc		$rndkey0,$inout3
	aesenc		$rndkey0,$inout4
	aesenc		$rndkey0,$inout5
	$movkey		-80($key,%rax),$rndkey0
	jnz		.Lxts_enc_loop6

	movdqa	(%r8),$twmask			# start calculating next tweak
	movdqa	$twres,$twtmp
	paddd	$twres,$twres
	 aesenc		$rndkey1,$inout0
	paddq	@tweak[5],@tweak[5]
	psrad	\$31,$twtmp
	 aesenc		$rndkey1,$inout1
	pand	$twmask,$twtmp
	$movkey	($key_),@tweak[0]		# load round[0]
	 aesenc		$rndkey1,$inout2
	 aesenc		$rndkey1,$inout3
	 aesenc		$rndkey1,$inout4
	pxor	$twtmp,@tweak[5]
	movaps	@tweak[0],@tweak[1]		# copy round[0]
	 aesenc		$rndkey1,$inout5
	 $movkey	-64($key),$rndkey1

	movdqa	$twres,$twtmp
	 aesenc		$rndkey0,$inout0
	paddd	$twres,$twres
	pxor	@tweak[5],@tweak[0]
	 aesenc		$rndkey0,$inout1
	psrad	\$31,$twtmp
	paddq	@tweak[5],@tweak[5]
	 aesenc		$rndkey0,$inout2
	 aesenc		$rndkey0,$inout3
	pand	$twmask,$twtmp
	movaps	@tweak[1],@tweak[2]
	 aesenc		$rndkey0,$inout4
	pxor	$twtmp,@tweak[5]
	movdqa	$twres,$twtmp
	 aesenc		$rndkey0,$inout5
	 $movkey	-48($key),$rndkey0

	paddd	$twres,$twres
	 aesenc		$rndkey1,$inout0
	pxor	@tweak[5],@tweak[1]
	psrad	\$31,$twtmp
	 aesenc		$rndkey1,$inout1
	paddq	@tweak[5],@tweak[5]
	pand	$twmask,$twtmp
	 aesenc		$rndkey1,$inout2
	 aesenc		$rndkey1,$inout3
	 movdqa	@tweak[3],`16*3`(%rsp)
	pxor	$twtmp,@tweak[5]
	 aesenc		$rndkey1,$inout4
	movaps	@tweak[2],@tweak[3]
	movdqa	$twres,$twtmp
	 aesenc		$rndkey1,$inout5
	 $movkey	-32($key),$rndkey1

	paddd	$twres,$twres
	 aesenc		$rndkey0,$inout0
	pxor	@tweak[5],@tweak[2]
	psrad	\$31,$twtmp
	 aesenc		$rndkey0,$inout1
	paddq	@tweak[5],@tweak[5]
	pand	$twmask,$twtmp
	 aesenc		$rndkey0,$inout2
	 aesenc		$rndkey0,$inout3
	 aesenc		$rndkey0,$inout4
	pxor	$twtmp,@tweak[5]
	movaps	@tweak[3],@tweak[4]
	 aesenc		$rndkey0,$inout5

	movdqa	$twres,$rndkey0
	paddd	$twres,$twres
	 aesenc		$rndkey1,$inout0
	pxor	@tweak[5],@tweak[3]
	psrad	\$31,$rndkey0
	 aesenc		$rndkey1,$inout1
	paddq	@tweak[5],@tweak[5]
	pand	$twmask,$rndkey0
	 aesenc		$rndkey1,$inout2
	 aesenc		$rndkey1,$inout3
	pxor	$rndkey0,@tweak[5]
	$movkey		($key_),$rndkey0
	 aesenc		$rndkey1,$inout4
	 aesenc		$rndkey1,$inout5
	$movkey		16($key_),$rndkey1

	pxor	@tweak[5],@tweak[4]
	 aesenclast	`16*0`(%rsp),$inout0
	psrad	\$31,$twres
	paddq	@tweak[5],@tweak[5]
	 aesenclast	`16*1`(%rsp),$inout1
	 aesenclast	`16*2`(%rsp),$inout2
	pand	$twmask,$twres
	mov	%r10,%rax			# restore $rounds
	 aesenclast	`16*3`(%rsp),$inout3
	 aesenclast	`16*4`(%rsp),$inout4
	 aesenclast	`16*5`(%rsp),$inout5
	pxor	$twres,@tweak[5]

	lea	`16*6`($out),$out		# $out+=6*16
	movups	$inout0,`-16*6`($out)		# store 6 output blocks
	movups	$inout1,`-16*5`($out)
	movups	$inout2,`-16*4`($out)
	movups	$inout3,`-16*3`($out)
	movups	$inout4,`-16*2`($out)
	movups	$inout5,`-16*1`($out)
	sub	\$16*6,$len
	jnc	.Lxts_enc_grandloop		# loop if $len-=6*16 didn't borrow

	mov	\$16+96,$rounds
	sub	$rnds_,$rounds
	mov	$key_,$key			# restore $key
	shr	\$4,$rounds			# restore original value

.Lxts_enc_short:
	# at the point @tweak[0..5] are populated with tweak values
	mov	$rounds,$rnds_			# backup $rounds
	pxor	$rndkey0,@tweak[0]
	add	\$16*6,$len			# restore real remaining $len
	jz	.Lxts_enc_done			# done if ($len==0)

	pxor	$rndkey0,@tweak[1]
	cmp	\$0x20,$len
	jb	.Lxts_enc_one			# $len is 1*16
	pxor	$rndkey0,@tweak[2]
	je	.Lxts_enc_two			# $len is 2*16

	pxor	$rndkey0,@tweak[3]
	cmp	\$0x40,$len
	jb	.Lxts_enc_three			# $len is 3*16
	pxor	$rndkey0,@tweak[4]
	je	.Lxts_enc_four			# $len is 4*16

	movdqu	($inp),$inout0			# $len is 5*16
	movdqu	16*1($inp),$inout1
	movdqu	16*2($inp),$inout2
	pxor	@tweak[0],$inout0
	movdqu	16*3($inp),$inout3
	pxor	@tweak[1],$inout1
	movdqu	16*4($inp),$inout4
	lea	16*5($inp),$inp			# $inp+=5*16
	pxor	@tweak[2],$inout2
	pxor	@tweak[3],$inout3
	pxor	@tweak[4],$inout4
	pxor	$inout5,$inout5

	call	_aesni_encrypt6

	xorps	@tweak[0],$inout0
	movdqa	@tweak[5],@tweak[0]
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2
	movdqu	$inout0,($out)			# store 5 output blocks
	xorps	@tweak[3],$inout3
	movdqu	$inout1,16*1($out)
	xorps	@tweak[4],$inout4
	movdqu	$inout2,16*2($out)
	movdqu	$inout3,16*3($out)
	movdqu	$inout4,16*4($out)
	lea	16*5($out),$out			# $out+=5*16
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_one:
	movups	($inp),$inout0
	lea	16*1($inp),$inp			# inp+=1*16
	xorps	@tweak[0],$inout0
___
	&aesni_generate1("enc",$key,$rounds);
$code.=<<___;
	xorps	@tweak[0],$inout0
	movdqa	@tweak[1],@tweak[0]
	movups	$inout0,($out)			# store one output block
	lea	16*1($out),$out			# $out+=1*16
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_two:
	movups	($inp),$inout0
	movups	16($inp),$inout1
	lea	32($inp),$inp			# $inp+=2*16
	xorps	@tweak[0],$inout0
	xorps	@tweak[1],$inout1

	call	_aesni_encrypt2

	xorps	@tweak[0],$inout0
	movdqa	@tweak[2],@tweak[0]
	xorps	@tweak[1],$inout1
	movups	$inout0,($out)			# store 2 output blocks
	movups	$inout1,16*1($out)
	lea	16*2($out),$out			# $out+=2*16
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_three:
	movups	($inp),$inout0
	movups	16*1($inp),$inout1
	movups	16*2($inp),$inout2
	lea	16*3($inp),$inp			# $inp+=3*16
	xorps	@tweak[0],$inout0
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2

	call	_aesni_encrypt3

	xorps	@tweak[0],$inout0
	movdqa	@tweak[3],@tweak[0]
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2
	movups	$inout0,($out)			# store 3 output blocks
	movups	$inout1,16*1($out)
	movups	$inout2,16*2($out)
	lea	16*3($out),$out			# $out+=3*16
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_four:
	movups	($inp),$inout0
	movups	16*1($inp),$inout1
	movups	16*2($inp),$inout2
	xorps	@tweak[0],$inout0
	movups	16*3($inp),$inout3
	lea	16*4($inp),$inp			# $inp+=4*16
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2
	xorps	@tweak[3],$inout3

	call	_aesni_encrypt4

	pxor	@tweak[0],$inout0
	movdqa	@tweak[4],@tweak[0]
	pxor	@tweak[1],$inout1
	pxor	@tweak[2],$inout2
	movdqu	$inout0,($out)			# store 4 output blocks
	pxor	@tweak[3],$inout3
	movdqu	$inout1,16*1($out)
	movdqu	$inout2,16*2($out)
	movdqu	$inout3,16*3($out)
	lea	16*4($out),$out			# $out+=4*16
	jmp	.Lxts_enc_done

.align	16
.Lxts_enc_done:
	and	\$15,$len_			# see if $len%16 is 0
	jz	.Lxts_enc_ret
	mov	$len_,$len

.Lxts_enc_steal:
	movzb	($inp),%eax			# borrow $rounds ...
	movzb	-16($out),%ecx			# ... and $key
	lea	1($inp),$inp
	mov	%al,-16($out)
	mov	%cl,0($out)
	lea	1($out),$out
	sub	\$1,$len
	jnz	.Lxts_enc_steal

	sub	$len_,$out			# rewind $out
	mov	$key_,$key			# restore $key
	mov	$rnds_,$rounds			# restore $rounds

	movups	-16($out),$inout0
	xorps	@tweak[0],$inout0
___
	&aesni_generate1("enc",$key,$rounds);
$code.=<<___;
	xorps	@tweak[0],$inout0
	movups	$inout0,-16($out)

.Lxts_enc_ret:
	xorps	%xmm0,%xmm0			# clear register bank
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
___
$code.=<<___ if (!$win64);
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	movaps	%xmm0,0x00(%rsp)		# clear stack
	pxor	%xmm8,%xmm8
	movaps	%xmm0,0x10(%rsp)
	pxor	%xmm9,%xmm9
	movaps	%xmm0,0x20(%rsp)
	pxor	%xmm10,%xmm10
	movaps	%xmm0,0x30(%rsp)
	pxor	%xmm11,%xmm11
	movaps	%xmm0,0x40(%rsp)
	pxor	%xmm12,%xmm12
	movaps	%xmm0,0x50(%rsp)
	pxor	%xmm13,%xmm13
	movaps	%xmm0,0x60(%rsp)
	pxor	%xmm14,%xmm14
	pxor	%xmm15,%xmm15
___
$code.=<<___ if ($win64);
	movaps	-0xa8(%r11),%xmm6
	movaps	%xmm0,-0xa8(%r11)		# clear stack
	movaps	-0x98(%r11),%xmm7
	movaps	%xmm0,-0x98(%r11)
	movaps	-0x88(%r11),%xmm8
	movaps	%xmm0,-0x88(%r11)
	movaps	-0x78(%r11),%xmm9
	movaps	%xmm0,-0x78(%r11)
	movaps	-0x68(%r11),%xmm10
	movaps	%xmm0,-0x68(%r11)
	movaps	-0x58(%r11),%xmm11
	movaps	%xmm0,-0x58(%r11)
	movaps	-0x48(%r11),%xmm12
	movaps	%xmm0,-0x48(%r11)
	movaps	-0x38(%r11),%xmm13
	movaps	%xmm0,-0x38(%r11)
	movaps	-0x28(%r11),%xmm14
	movaps	%xmm0,-0x28(%r11)
	movaps	-0x18(%r11),%xmm15
	movaps	%xmm0,-0x18(%r11)
	movaps	%xmm0,0x00(%rsp)
	movaps	%xmm0,0x10(%rsp)
	movaps	%xmm0,0x20(%rsp)
	movaps	%xmm0,0x30(%rsp)
	movaps	%xmm0,0x40(%rsp)
	movaps	%xmm0,0x50(%rsp)
	movaps	%xmm0,0x60(%rsp)
___
$code.=<<___;
	mov	-8(%r11),%rbp
.cfi_restore	%rbp
	lea	(%r11),%rsp
.cfi_def_cfa_register	%rsp
.Lxts_enc_epilogue:
	ret
.cfi_endproc
.size	aesni_xts_encrypt,.-aesni_xts_encrypt
___

$code.=<<___;
.globl	aesni_xts_decrypt
.type	aesni_xts_decrypt,\@function,6
.align	16
aesni_xts_decrypt:
.cfi_startproc
	lea	(%rsp),%r11			# frame pointer
.cfi_def_cfa_register	%r11
	push	%rbp
.cfi_push	%rbp
	sub	\$$frame_size,%rsp
	and	\$-16,%rsp	# Linux kernel stack can be incorrectly seeded
___
$code.=<<___ if ($win64);
	movaps	%xmm6,-0xa8(%r11)		# offload everything
	movaps	%xmm7,-0x98(%r11)
	movaps	%xmm8,-0x88(%r11)
	movaps	%xmm9,-0x78(%r11)
	movaps	%xmm10,-0x68(%r11)
	movaps	%xmm11,-0x58(%r11)
	movaps	%xmm12,-0x48(%r11)
	movaps	%xmm13,-0x38(%r11)
	movaps	%xmm14,-0x28(%r11)
	movaps	%xmm15,-0x18(%r11)
.Lxts_dec_body:
___
$code.=<<___;
	movups	($ivp),$inout0			# load clear-text tweak
	mov	240($key2),$rounds		# key2->rounds
	mov	240($key),$rnds_		# key1->rounds
___
	# generate the tweak
	&aesni_generate1("enc",$key2,$rounds,$inout0);
$code.=<<___;
	xor	%eax,%eax			# if ($len%16) len-=16;
	test	\$15,$len
	setnz	%al
	shl	\$4,%rax
	sub	%rax,$len

	$movkey	($key),$rndkey0			# zero round key
	mov	$key,$key_			# backup $key
	mov	$rnds_,$rounds			# backup $rounds
	shl	\$4,$rnds_
	mov	$len,$len_			# backup $len
	and	\$-16,$len

	$movkey	16($key,$rnds_),$rndkey1	# last round key

	movdqa	.Lxts_magic(%rip),$twmask
	movdqa	$inout0,@tweak[5]
	pshufd	\$0x5f,$inout0,$twres
	pxor	$rndkey0,$rndkey1
___
    for ($i=0;$i<4;$i++) {
    $code.=<<___;
	movdqa	$twres,$twtmp
	paddd	$twres,$twres
	movdqa	@tweak[5],@tweak[$i]
	psrad	\$31,$twtmp			# broadcast upper bits
	paddq	@tweak[5],@tweak[5]
	pand	$twmask,$twtmp
	pxor	$rndkey0,@tweak[$i]
	pxor	$twtmp,@tweak[5]
___
    }
$code.=<<___;
	movdqa	@tweak[5],@tweak[4]
	psrad	\$31,$twres
	paddq	@tweak[5],@tweak[5]
	pand	$twmask,$twres
	pxor	$rndkey0,@tweak[4]
	pxor	$twres,@tweak[5]
	movaps	$rndkey1,0x60(%rsp)		# save round[0]^round[last]

	sub	\$16*6,$len
	jc	.Lxts_dec_short			# if $len-=6*16 borrowed

	mov	\$16+96,$rounds
	lea	32($key_,$rnds_),$key		# end of key schedule
	sub	%r10,%rax			# twisted $rounds
	$movkey	16($key_),$rndkey1
	mov	%rax,%r10			# backup twisted $rounds
	lea	.Lxts_magic(%rip),%r8
	jmp	.Lxts_dec_grandloop

.align	32
.Lxts_dec_grandloop:
	movdqu	`16*0`($inp),$inout0		# load input
	movdqa	$rndkey0,$twmask
	movdqu	`16*1`($inp),$inout1
	pxor	@tweak[0],$inout0		# intput^=tweak^round[0]
	movdqu	`16*2`($inp),$inout2
	pxor	@tweak[1],$inout1
	 aesdec		$rndkey1,$inout0
	movdqu	`16*3`($inp),$inout3
	pxor	@tweak[2],$inout2
	 aesdec		$rndkey1,$inout1
	movdqu	`16*4`($inp),$inout4
	pxor	@tweak[3],$inout3
	 aesdec		$rndkey1,$inout2
	movdqu	`16*5`($inp),$inout5
	pxor	@tweak[5],$twmask		# round[0]^=tweak[5]
	 movdqa	0x60(%rsp),$twres		# load round[0]^round[last]
	pxor	@tweak[4],$inout4
	 aesdec		$rndkey1,$inout3
	$movkey	32($key_),$rndkey0
	lea	`16*6`($inp),$inp
	pxor	$twmask,$inout5

	 pxor	$twres,@tweak[0]		# calculate tweaks^round[last]
	aesdec		$rndkey1,$inout4
	 pxor	$twres,@tweak[1]
	 movdqa	@tweak[0],`16*0`(%rsp)		# put aside tweaks^last round key
	aesdec		$rndkey1,$inout5
	$movkey		48($key_),$rndkey1
	 pxor	$twres,@tweak[2]

	aesdec		$rndkey0,$inout0
	 pxor	$twres,@tweak[3]
	 movdqa	@tweak[1],`16*1`(%rsp)
	aesdec		$rndkey0,$inout1
	 pxor	$twres,@tweak[4]
	 movdqa	@tweak[2],`16*2`(%rsp)
	aesdec		$rndkey0,$inout2
	aesdec		$rndkey0,$inout3
	 pxor	$twres,$twmask
	 movdqa	@tweak[4],`16*4`(%rsp)
	aesdec		$rndkey0,$inout4
	aesdec		$rndkey0,$inout5
	$movkey		64($key_),$rndkey0
	 movdqa	$twmask,`16*5`(%rsp)
	pshufd	\$0x5f,@tweak[5],$twres
	jmp	.Lxts_dec_loop6
.align	32
.Lxts_dec_loop6:
	aesdec		$rndkey1,$inout0
	aesdec		$rndkey1,$inout1
	aesdec		$rndkey1,$inout2
	aesdec		$rndkey1,$inout3
	aesdec		$rndkey1,$inout4
	aesdec		$rndkey1,$inout5
	$movkey		-64($key,%rax),$rndkey1
	add		\$32,%rax

	aesdec		$rndkey0,$inout0
	aesdec		$rndkey0,$inout1
	aesdec		$rndkey0,$inout2
	aesdec		$rndkey0,$inout3
	aesdec		$rndkey0,$inout4
	aesdec		$rndkey0,$inout5
	$movkey		-80($key,%rax),$rndkey0
	jnz		.Lxts_dec_loop6

	movdqa	(%r8),$twmask			# start calculating next tweak
	movdqa	$twres,$twtmp
	paddd	$twres,$twres
	 aesdec		$rndkey1,$inout0
	paddq	@tweak[5],@tweak[5]
	psrad	\$31,$twtmp
	 aesdec		$rndkey1,$inout1
	pand	$twmask,$twtmp
	$movkey	($key_),@tweak[0]		# load round[0]
	 aesdec		$rndkey1,$inout2
	 aesdec		$rndkey1,$inout3
	 aesdec		$rndkey1,$inout4
	pxor	$twtmp,@tweak[5]
	movaps	@tweak[0],@tweak[1]		# copy round[0]
	 aesdec		$rndkey1,$inout5
	 $movkey	-64($key),$rndkey1

	movdqa	$twres,$twtmp
	 aesdec		$rndkey0,$inout0
	paddd	$twres,$twres
	pxor	@tweak[5],@tweak[0]
	 aesdec		$rndkey0,$inout1
	psrad	\$31,$twtmp
	paddq	@tweak[5],@tweak[5]
	 aesdec		$rndkey0,$inout2
	 aesdec		$rndkey0,$inout3
	pand	$twmask,$twtmp
	movaps	@tweak[1],@tweak[2]
	 aesdec		$rndkey0,$inout4
	pxor	$twtmp,@tweak[5]
	movdqa	$twres,$twtmp
	 aesdec		$rndkey0,$inout5
	 $movkey	-48($key),$rndkey0

	paddd	$twres,$twres
	 aesdec		$rndkey1,$inout0
	pxor	@tweak[5],@tweak[1]
	psrad	\$31,$twtmp
	 aesdec		$rndkey1,$inout1
	paddq	@tweak[5],@tweak[5]
	pand	$twmask,$twtmp
	 aesdec		$rndkey1,$inout2
	 aesdec		$rndkey1,$inout3
	 movdqa	@tweak[3],`16*3`(%rsp)
	pxor	$twtmp,@tweak[5]
	 aesdec		$rndkey1,$inout4
	movaps	@tweak[2],@tweak[3]
	movdqa	$twres,$twtmp
	 aesdec		$rndkey1,$inout5
	 $movkey	-32($key),$rndkey1

	paddd	$twres,$twres
	 aesdec		$rndkey0,$inout0
	pxor	@tweak[5],@tweak[2]
	psrad	\$31,$twtmp
	 aesdec		$rndkey0,$inout1
	paddq	@tweak[5],@tweak[5]
	pand	$twmask,$twtmp
	 aesdec		$rndkey0,$inout2
	 aesdec		$rndkey0,$inout3
	 aesdec		$rndkey0,$inout4
	pxor	$twtmp,@tweak[5]
	movaps	@tweak[3],@tweak[4]
	 aesdec		$rndkey0,$inout5

	movdqa	$twres,$rndkey0
	paddd	$twres,$twres
	 aesdec		$rndkey1,$inout0
	pxor	@tweak[5],@tweak[3]
	psrad	\$31,$rndkey0
	 aesdec		$rndkey1,$inout1
	paddq	@tweak[5],@tweak[5]
	pand	$twmask,$rndkey0
	 aesdec		$rndkey1,$inout2
	 aesdec		$rndkey1,$inout3
	pxor	$rndkey0,@tweak[5]
	$movkey		($key_),$rndkey0
	 aesdec		$rndkey1,$inout4
	 aesdec		$rndkey1,$inout5
	$movkey		16($key_),$rndkey1

	pxor	@tweak[5],@tweak[4]
	 aesdeclast	`16*0`(%rsp),$inout0
	psrad	\$31,$twres
	paddq	@tweak[5],@tweak[5]
	 aesdeclast	`16*1`(%rsp),$inout1
	 aesdeclast	`16*2`(%rsp),$inout2
	pand	$twmask,$twres
	mov	%r10,%rax			# restore $rounds
	 aesdeclast	`16*3`(%rsp),$inout3
	 aesdeclast	`16*4`(%rsp),$inout4
	 aesdeclast	`16*5`(%rsp),$inout5
	pxor	$twres,@tweak[5]

	lea	`16*6`($out),$out		# $out+=6*16
	movups	$inout0,`-16*6`($out)		# store 6 output blocks
	movups	$inout1,`-16*5`($out)
	movups	$inout2,`-16*4`($out)
	movups	$inout3,`-16*3`($out)
	movups	$inout4,`-16*2`($out)
	movups	$inout5,`-16*1`($out)
	sub	\$16*6,$len
	jnc	.Lxts_dec_grandloop		# loop if $len-=6*16 didn't borrow

	mov	\$16+96,$rounds
	sub	$rnds_,$rounds
	mov	$key_,$key			# restore $key
	shr	\$4,$rounds			# restore original value

.Lxts_dec_short:
	# at the point @tweak[0..5] are populated with tweak values
	mov	$rounds,$rnds_			# backup $rounds
	pxor	$rndkey0,@tweak[0]
	pxor	$rndkey0,@tweak[1]
	add	\$16*6,$len			# restore real remaining $len
	jz	.Lxts_dec_done			# done if ($len==0)

	pxor	$rndkey0,@tweak[2]
	cmp	\$0x20,$len
	jb	.Lxts_dec_one			# $len is 1*16
	pxor	$rndkey0,@tweak[3]
	je	.Lxts_dec_two			# $len is 2*16

	pxor	$rndkey0,@tweak[4]
	cmp	\$0x40,$len
	jb	.Lxts_dec_three			# $len is 3*16
	je	.Lxts_dec_four			# $len is 4*16

	movdqu	($inp),$inout0			# $len is 5*16
	movdqu	16*1($inp),$inout1
	movdqu	16*2($inp),$inout2
	pxor	@tweak[0],$inout0
	movdqu	16*3($inp),$inout3
	pxor	@tweak[1],$inout1
	movdqu	16*4($inp),$inout4
	lea	16*5($inp),$inp			# $inp+=5*16
	pxor	@tweak[2],$inout2
	pxor	@tweak[3],$inout3
	pxor	@tweak[4],$inout4

	call	_aesni_decrypt6

	xorps	@tweak[0],$inout0
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2
	movdqu	$inout0,($out)			# store 5 output blocks
	xorps	@tweak[3],$inout3
	movdqu	$inout1,16*1($out)
	xorps	@tweak[4],$inout4
	movdqu	$inout2,16*2($out)
	 pxor		$twtmp,$twtmp
	movdqu	$inout3,16*3($out)
	 pcmpgtd	@tweak[5],$twtmp
	movdqu	$inout4,16*4($out)
	lea	16*5($out),$out			# $out+=5*16
	 pshufd		\$0x13,$twtmp,@tweak[1]	# $twres
	and	\$15,$len_
	jz	.Lxts_dec_ret

	movdqa	@tweak[5],@tweak[0]
	paddq	@tweak[5],@tweak[5]		# psllq 1,$tweak
	pand	$twmask,@tweak[1]		# isolate carry and residue
	pxor	@tweak[5],@tweak[1]
	jmp	.Lxts_dec_done2

.align	16
.Lxts_dec_one:
	movups	($inp),$inout0
	lea	16*1($inp),$inp			# $inp+=1*16
	xorps	@tweak[0],$inout0
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	xorps	@tweak[0],$inout0
	movdqa	@tweak[1],@tweak[0]
	movups	$inout0,($out)			# store one output block
	movdqa	@tweak[2],@tweak[1]
	lea	16*1($out),$out			# $out+=1*16
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_two:
	movups	($inp),$inout0
	movups	16($inp),$inout1
	lea	32($inp),$inp			# $inp+=2*16
	xorps	@tweak[0],$inout0
	xorps	@tweak[1],$inout1

	call	_aesni_decrypt2

	xorps	@tweak[0],$inout0
	movdqa	@tweak[2],@tweak[0]
	xorps	@tweak[1],$inout1
	movdqa	@tweak[3],@tweak[1]
	movups	$inout0,($out)			# store 2 output blocks
	movups	$inout1,16*1($out)
	lea	16*2($out),$out			# $out+=2*16
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_three:
	movups	($inp),$inout0
	movups	16*1($inp),$inout1
	movups	16*2($inp),$inout2
	lea	16*3($inp),$inp			# $inp+=3*16
	xorps	@tweak[0],$inout0
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2

	call	_aesni_decrypt3

	xorps	@tweak[0],$inout0
	movdqa	@tweak[3],@tweak[0]
	xorps	@tweak[1],$inout1
	movdqa	@tweak[4],@tweak[1]
	xorps	@tweak[2],$inout2
	movups	$inout0,($out)			# store 3 output blocks
	movups	$inout1,16*1($out)
	movups	$inout2,16*2($out)
	lea	16*3($out),$out			# $out+=3*16
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_four:
	movups	($inp),$inout0
	movups	16*1($inp),$inout1
	movups	16*2($inp),$inout2
	xorps	@tweak[0],$inout0
	movups	16*3($inp),$inout3
	lea	16*4($inp),$inp			# $inp+=4*16
	xorps	@tweak[1],$inout1
	xorps	@tweak[2],$inout2
	xorps	@tweak[3],$inout3

	call	_aesni_decrypt4

	pxor	@tweak[0],$inout0
	movdqa	@tweak[4],@tweak[0]
	pxor	@tweak[1],$inout1
	movdqa	@tweak[5],@tweak[1]
	pxor	@tweak[2],$inout2
	movdqu	$inout0,($out)			# store 4 output blocks
	pxor	@tweak[3],$inout3
	movdqu	$inout1,16*1($out)
	movdqu	$inout2,16*2($out)
	movdqu	$inout3,16*3($out)
	lea	16*4($out),$out			# $out+=4*16
	jmp	.Lxts_dec_done

.align	16
.Lxts_dec_done:
	and	\$15,$len_			# see if $len%16 is 0
	jz	.Lxts_dec_ret
.Lxts_dec_done2:
	mov	$len_,$len
	mov	$key_,$key			# restore $key
	mov	$rnds_,$rounds			# restore $rounds

	movups	($inp),$inout0
	xorps	@tweak[1],$inout0
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	xorps	@tweak[1],$inout0
	movups	$inout0,($out)

.Lxts_dec_steal:
	movzb	16($inp),%eax			# borrow $rounds ...
	movzb	($out),%ecx			# ... and $key
	lea	1($inp),$inp
	mov	%al,($out)
	mov	%cl,16($out)
	lea	1($out),$out
	sub	\$1,$len
	jnz	.Lxts_dec_steal

	sub	$len_,$out			# rewind $out
	mov	$key_,$key			# restore $key
	mov	$rnds_,$rounds			# restore $rounds

	movups	($out),$inout0
	xorps	@tweak[0],$inout0
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	xorps	@tweak[0],$inout0
	movups	$inout0,($out)

.Lxts_dec_ret:
	xorps	%xmm0,%xmm0			# clear register bank
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
___
$code.=<<___ if (!$win64);
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	movaps	%xmm0,0x00(%rsp)		# clear stack
	pxor	%xmm8,%xmm8
	movaps	%xmm0,0x10(%rsp)
	pxor	%xmm9,%xmm9
	movaps	%xmm0,0x20(%rsp)
	pxor	%xmm10,%xmm10
	movaps	%xmm0,0x30(%rsp)
	pxor	%xmm11,%xmm11
	movaps	%xmm0,0x40(%rsp)
	pxor	%xmm12,%xmm12
	movaps	%xmm0,0x50(%rsp)
	pxor	%xmm13,%xmm13
	movaps	%xmm0,0x60(%rsp)
	pxor	%xmm14,%xmm14
	pxor	%xmm15,%xmm15
___
$code.=<<___ if ($win64);
	movaps	-0xa8(%r11),%xmm6
	movaps	%xmm0,-0xa8(%r11)		# clear stack
	movaps	-0x98(%r11),%xmm7
	movaps	%xmm0,-0x98(%r11)
	movaps	-0x88(%r11),%xmm8
	movaps	%xmm0,-0x88(%r11)
	movaps	-0x78(%r11),%xmm9
	movaps	%xmm0,-0x78(%r11)
	movaps	-0x68(%r11),%xmm10
	movaps	%xmm0,-0x68(%r11)
	movaps	-0x58(%r11),%xmm11
	movaps	%xmm0,-0x58(%r11)
	movaps	-0x48(%r11),%xmm12
	movaps	%xmm0,-0x48(%r11)
	movaps	-0x38(%r11),%xmm13
	movaps	%xmm0,-0x38(%r11)
	movaps	-0x28(%r11),%xmm14
	movaps	%xmm0,-0x28(%r11)
	movaps	-0x18(%r11),%xmm15
	movaps	%xmm0,-0x18(%r11)
	movaps	%xmm0,0x00(%rsp)
	movaps	%xmm0,0x10(%rsp)
	movaps	%xmm0,0x20(%rsp)
	movaps	%xmm0,0x30(%rsp)
	movaps	%xmm0,0x40(%rsp)
	movaps	%xmm0,0x50(%rsp)
	movaps	%xmm0,0x60(%rsp)
___
$code.=<<___;
	mov	-8(%r11),%rbp
.cfi_restore	%rbp
	lea	(%r11),%rsp
.cfi_def_cfa_register	%rsp
.Lxts_dec_epilogue:
	ret
.cfi_endproc
.size	aesni_xts_decrypt,.-aesni_xts_decrypt
___
}

######################################################################
# void aesni_ocb_[en|de]crypt(const char *inp, char *out, size_t blocks,
#	const AES_KEY *key, unsigned int start_block_num,
#	unsigned char offset_i[16], const unsigned char L_[][16],
#	unsigned char checksum[16]);
#
{
my @offset=map("%xmm$_",(10..15));
my ($checksum,$rndkey0l)=("%xmm8","%xmm9");
my ($block_num,$offset_p)=("%r8","%r9");		# 5th and 6th arguments
my ($L_p,$checksum_p) = ("%rbx","%rbp");
my ($i1,$i3,$i5) = ("%r12","%r13","%r14");
my $seventh_arg = $win64 ? 56 : 8;
my $blocks = $len;

$code.=<<___;
.globl	aesni_ocb_encrypt
.type	aesni_ocb_encrypt,\@function,6
.align	32
aesni_ocb_encrypt:
.cfi_startproc
	lea	(%rsp),%rax
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
___
$code.=<<___ if ($win64);
	lea	-0xa0(%rsp),%rsp
	movaps	%xmm6,0x00(%rsp)		# offload everything
	movaps	%xmm7,0x10(%rsp)
	movaps	%xmm8,0x20(%rsp)
	movaps	%xmm9,0x30(%rsp)
	movaps	%xmm10,0x40(%rsp)
	movaps	%xmm11,0x50(%rsp)
	movaps	%xmm12,0x60(%rsp)
	movaps	%xmm13,0x70(%rsp)
	movaps	%xmm14,0x80(%rsp)
	movaps	%xmm15,0x90(%rsp)
.Locb_enc_body:
___
$code.=<<___;
	mov	$seventh_arg(%rax),$L_p		# 7th argument
	mov	$seventh_arg+8(%rax),$checksum_p# 8th argument

	mov	240($key),$rnds_
	mov	$key,$key_
	shl	\$4,$rnds_
	$movkey	($key),$rndkey0l		# round[0]
	$movkey	16($key,$rnds_),$rndkey1	# round[last]

	movdqu	($offset_p),@offset[5]		# load last offset_i
	pxor	$rndkey1,$rndkey0l		# round[0] ^ round[last]
	pxor	$rndkey1,@offset[5]		# offset_i ^ round[last]

	mov	\$16+32,$rounds
	lea	32($key_,$rnds_),$key
	$movkey	16($key_),$rndkey1		# round[1]
	sub	%r10,%rax			# twisted $rounds
	mov	%rax,%r10			# backup twisted $rounds

	movdqu	($L_p),@offset[0]		# L_0 for all odd-numbered blocks
	movdqu	($checksum_p),$checksum		# load checksum

	test	\$1,$block_num			# is first block number odd?
	jnz	.Locb_enc_odd

	bsf	$block_num,$i1
	add	\$1,$block_num
	shl	\$4,$i1
	movdqu	($L_p,$i1),$inout5		# borrow
	movdqu	($inp),$inout0
	lea	16($inp),$inp

	call	__ocb_encrypt1

	movdqa	$inout5,@offset[5]
	movups	$inout0,($out)
	lea	16($out),$out
	sub	\$1,$blocks
	jz	.Locb_enc_done

.Locb_enc_odd:
	lea	1($block_num),$i1		# even-numbered blocks
	lea	3($block_num),$i3
	lea	5($block_num),$i5
	lea	6($block_num),$block_num
	bsf	$i1,$i1				# ntz(block)
	bsf	$i3,$i3
	bsf	$i5,$i5
	shl	\$4,$i1				# ntz(block) -> table offset
	shl	\$4,$i3
	shl	\$4,$i5

	sub	\$6,$blocks
	jc	.Locb_enc_short
	jmp	.Locb_enc_grandloop

.align	32
.Locb_enc_grandloop:
	movdqu	`16*0`($inp),$inout0		# load input
	movdqu	`16*1`($inp),$inout1
	movdqu	`16*2`($inp),$inout2
	movdqu	`16*3`($inp),$inout3
	movdqu	`16*4`($inp),$inout4
	movdqu	`16*5`($inp),$inout5
	lea	`16*6`($inp),$inp

	call	__ocb_encrypt6

	movups	$inout0,`16*0`($out)		# store output
	movups	$inout1,`16*1`($out)
	movups	$inout2,`16*2`($out)
	movups	$inout3,`16*3`($out)
	movups	$inout4,`16*4`($out)
	movups	$inout5,`16*5`($out)
	lea	`16*6`($out),$out
	sub	\$6,$blocks
	jnc	.Locb_enc_grandloop

.Locb_enc_short:
	add	\$6,$blocks
	jz	.Locb_enc_done

	movdqu	`16*0`($inp),$inout0
	cmp	\$2,$blocks
	jb	.Locb_enc_one
	movdqu	`16*1`($inp),$inout1
	je	.Locb_enc_two

	movdqu	`16*2`($inp),$inout2
	cmp	\$4,$blocks
	jb	.Locb_enc_three
	movdqu	`16*3`($inp),$inout3
	je	.Locb_enc_four

	movdqu	`16*4`($inp),$inout4
	pxor	$inout5,$inout5

	call	__ocb_encrypt6

	movdqa	@offset[4],@offset[5]
	movups	$inout0,`16*0`($out)
	movups	$inout1,`16*1`($out)
	movups	$inout2,`16*2`($out)
	movups	$inout3,`16*3`($out)
	movups	$inout4,`16*4`($out)

	jmp	.Locb_enc_done

.align	16
.Locb_enc_one:
	movdqa	@offset[0],$inout5		# borrow

	call	__ocb_encrypt1

	movdqa	$inout5,@offset[5]
	movups	$inout0,`16*0`($out)
	jmp	.Locb_enc_done

.align	16
.Locb_enc_two:
	pxor	$inout2,$inout2
	pxor	$inout3,$inout3

	call	__ocb_encrypt4

	movdqa	@offset[1],@offset[5]
	movups	$inout0,`16*0`($out)
	movups	$inout1,`16*1`($out)

	jmp	.Locb_enc_done

.align	16
.Locb_enc_three:
	pxor	$inout3,$inout3

	call	__ocb_encrypt4

	movdqa	@offset[2],@offset[5]
	movups	$inout0,`16*0`($out)
	movups	$inout1,`16*1`($out)
	movups	$inout2,`16*2`($out)

	jmp	.Locb_enc_done

.align	16
.Locb_enc_four:
	call	__ocb_encrypt4

	movdqa	@offset[3],@offset[5]
	movups	$inout0,`16*0`($out)
	movups	$inout1,`16*1`($out)
	movups	$inout2,`16*2`($out)
	movups	$inout3,`16*3`($out)

.Locb_enc_done:
	pxor	$rndkey0,@offset[5]		# "remove" round[last]
	movdqu	$checksum,($checksum_p)		# store checksum
	movdqu	@offset[5],($offset_p)		# store last offset_i

	xorps	%xmm0,%xmm0			# clear register bank
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
___
$code.=<<___ if (!$win64);
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	pxor	%xmm8,%xmm8
	pxor	%xmm9,%xmm9
	pxor	%xmm10,%xmm10
	pxor	%xmm11,%xmm11
	pxor	%xmm12,%xmm12
	pxor	%xmm13,%xmm13
	pxor	%xmm14,%xmm14
	pxor	%xmm15,%xmm15
	lea	0x28(%rsp),%rax
.cfi_def_cfa	%rax,8
___
$code.=<<___ if ($win64);
	movaps	0x00(%rsp),%xmm6
	movaps	%xmm0,0x00(%rsp)		# clear stack
	movaps	0x10(%rsp),%xmm7
	movaps	%xmm0,0x10(%rsp)
	movaps	0x20(%rsp),%xmm8
	movaps	%xmm0,0x20(%rsp)
	movaps	0x30(%rsp),%xmm9
	movaps	%xmm0,0x30(%rsp)
	movaps	0x40(%rsp),%xmm10
	movaps	%xmm0,0x40(%rsp)
	movaps	0x50(%rsp),%xmm11
	movaps	%xmm0,0x50(%rsp)
	movaps	0x60(%rsp),%xmm12
	movaps	%xmm0,0x60(%rsp)
	movaps	0x70(%rsp),%xmm13
	movaps	%xmm0,0x70(%rsp)
	movaps	0x80(%rsp),%xmm14
	movaps	%xmm0,0x80(%rsp)
	movaps	0x90(%rsp),%xmm15
	movaps	%xmm0,0x90(%rsp)
	lea	0xa0+0x28(%rsp),%rax
.Locb_enc_pop:
___
$code.=<<___;
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
.Locb_enc_epilogue:
	ret
.cfi_endproc
.size	aesni_ocb_encrypt,.-aesni_ocb_encrypt

.type	__ocb_encrypt6,\@abi-omnipotent
.align	32
__ocb_encrypt6:
	 pxor		$rndkey0l,@offset[5]	# offset_i ^ round[0]
	 movdqu		($L_p,$i1),@offset[1]
	 movdqa		@offset[0],@offset[2]
	 movdqu		($L_p,$i3),@offset[3]
	 movdqa		@offset[0],@offset[4]
	 pxor		@offset[5],@offset[0]
	 movdqu		($L_p,$i5),@offset[5]
	 pxor		@offset[0],@offset[1]
	pxor		$inout0,$checksum	# accumulate checksum
	pxor		@offset[0],$inout0	# input ^ round[0] ^ offset_i
	 pxor		@offset[1],@offset[2]
	pxor		$inout1,$checksum
	pxor		@offset[1],$inout1
	 pxor		@offset[2],@offset[3]
	pxor		$inout2,$checksum
	pxor		@offset[2],$inout2
	 pxor		@offset[3],@offset[4]
	pxor		$inout3,$checksum
	pxor		@offset[3],$inout3
	 pxor		@offset[4],@offset[5]
	pxor		$inout4,$checksum
	pxor		@offset[4],$inout4
	pxor		$inout5,$checksum
	pxor		@offset[5],$inout5
	$movkey		32($key_),$rndkey0

	lea		1($block_num),$i1	# even-numbered blocks
	lea		3($block_num),$i3
	lea		5($block_num),$i5
	add		\$6,$block_num
	 pxor		$rndkey0l,@offset[0]	# offset_i ^ round[last]
	bsf		$i1,$i1			# ntz(block)
	bsf		$i3,$i3
	bsf		$i5,$i5

	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	 pxor		$rndkey0l,@offset[1]
	 pxor		$rndkey0l,@offset[2]
	aesenc		$rndkey1,$inout4
	 pxor		$rndkey0l,@offset[3]
	 pxor		$rndkey0l,@offset[4]
	aesenc		$rndkey1,$inout5
	$movkey		48($key_),$rndkey1
	 pxor		$rndkey0l,@offset[5]

	aesenc		$rndkey0,$inout0
	aesenc		$rndkey0,$inout1
	aesenc		$rndkey0,$inout2
	aesenc		$rndkey0,$inout3
	aesenc		$rndkey0,$inout4
	aesenc		$rndkey0,$inout5
	$movkey		64($key_),$rndkey0
	shl		\$4,$i1			# ntz(block) -> table offset
	shl		\$4,$i3
	jmp		.Locb_enc_loop6

.align	32
.Locb_enc_loop6:
	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	aesenc		$rndkey1,$inout4
	aesenc		$rndkey1,$inout5
	$movkey		($key,%rax),$rndkey1
	add		\$32,%rax

	aesenc		$rndkey0,$inout0
	aesenc		$rndkey0,$inout1
	aesenc		$rndkey0,$inout2
	aesenc		$rndkey0,$inout3
	aesenc		$rndkey0,$inout4
	aesenc		$rndkey0,$inout5
	$movkey		-16($key,%rax),$rndkey0
	jnz		.Locb_enc_loop6

	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	aesenc		$rndkey1,$inout4
	aesenc		$rndkey1,$inout5
	$movkey		16($key_),$rndkey1
	shl		\$4,$i5

	aesenclast	@offset[0],$inout0
	movdqu		($L_p),@offset[0]	# L_0 for all odd-numbered blocks
	mov		%r10,%rax		# restore twisted rounds
	aesenclast	@offset[1],$inout1
	aesenclast	@offset[2],$inout2
	aesenclast	@offset[3],$inout3
	aesenclast	@offset[4],$inout4
	aesenclast	@offset[5],$inout5
	ret
.size	__ocb_encrypt6,.-__ocb_encrypt6

.type	__ocb_encrypt4,\@abi-omnipotent
.align	32
__ocb_encrypt4:
	 pxor		$rndkey0l,@offset[5]	# offset_i ^ round[0]
	 movdqu		($L_p,$i1),@offset[1]
	 movdqa		@offset[0],@offset[2]
	 movdqu		($L_p,$i3),@offset[3]
	 pxor		@offset[5],@offset[0]
	 pxor		@offset[0],@offset[1]
	pxor		$inout0,$checksum	# accumulate checksum
	pxor		@offset[0],$inout0	# input ^ round[0] ^ offset_i
	 pxor		@offset[1],@offset[2]
	pxor		$inout1,$checksum
	pxor		@offset[1],$inout1
	 pxor		@offset[2],@offset[3]
	pxor		$inout2,$checksum
	pxor		@offset[2],$inout2
	pxor		$inout3,$checksum
	pxor		@offset[3],$inout3
	$movkey		32($key_),$rndkey0

	 pxor		$rndkey0l,@offset[0]	# offset_i ^ round[last]
	 pxor		$rndkey0l,@offset[1]
	 pxor		$rndkey0l,@offset[2]
	 pxor		$rndkey0l,@offset[3]

	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	$movkey		48($key_),$rndkey1

	aesenc		$rndkey0,$inout0
	aesenc		$rndkey0,$inout1
	aesenc		$rndkey0,$inout2
	aesenc		$rndkey0,$inout3
	$movkey		64($key_),$rndkey0
	jmp		.Locb_enc_loop4

.align	32
.Locb_enc_loop4:
	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	$movkey		($key,%rax),$rndkey1
	add		\$32,%rax

	aesenc		$rndkey0,$inout0
	aesenc		$rndkey0,$inout1
	aesenc		$rndkey0,$inout2
	aesenc		$rndkey0,$inout3
	$movkey		-16($key,%rax),$rndkey0
	jnz		.Locb_enc_loop4

	aesenc		$rndkey1,$inout0
	aesenc		$rndkey1,$inout1
	aesenc		$rndkey1,$inout2
	aesenc		$rndkey1,$inout3
	$movkey		16($key_),$rndkey1
	mov		%r10,%rax		# restore twisted rounds

	aesenclast	@offset[0],$inout0
	aesenclast	@offset[1],$inout1
	aesenclast	@offset[2],$inout2
	aesenclast	@offset[3],$inout3
	ret
.size	__ocb_encrypt4,.-__ocb_encrypt4

.type	__ocb_encrypt1,\@abi-omnipotent
.align	32
__ocb_encrypt1:
	 pxor		@offset[5],$inout5	# offset_i
	 pxor		$rndkey0l,$inout5	# offset_i ^ round[0]
	pxor		$inout0,$checksum	# accumulate checksum
	pxor		$inout5,$inout0		# input ^ round[0] ^ offset_i
	$movkey		32($key_),$rndkey0

	aesenc		$rndkey1,$inout0
	$movkey		48($key_),$rndkey1
	pxor		$rndkey0l,$inout5	# offset_i ^ round[last]

	aesenc		$rndkey0,$inout0
	$movkey		64($key_),$rndkey0
	jmp		.Locb_enc_loop1

.align	32
.Locb_enc_loop1:
	aesenc		$rndkey1,$inout0
	$movkey		($key,%rax),$rndkey1
	add		\$32,%rax

	aesenc		$rndkey0,$inout0
	$movkey		-16($key,%rax),$rndkey0
	jnz		.Locb_enc_loop1

	aesenc		$rndkey1,$inout0
	$movkey		16($key_),$rndkey1	# redundant in tail
	mov		%r10,%rax		# restore twisted rounds

	aesenclast	$inout5,$inout0
	ret
.size	__ocb_encrypt1,.-__ocb_encrypt1

.globl	aesni_ocb_decrypt
.type	aesni_ocb_decrypt,\@function,6
.align	32
aesni_ocb_decrypt:
.cfi_startproc
	lea	(%rsp),%rax
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
___
$code.=<<___ if ($win64);
	lea	-0xa0(%rsp),%rsp
	movaps	%xmm6,0x00(%rsp)		# offload everything
	movaps	%xmm7,0x10(%rsp)
	movaps	%xmm8,0x20(%rsp)
	movaps	%xmm9,0x30(%rsp)
	movaps	%xmm10,0x40(%rsp)
	movaps	%xmm11,0x50(%rsp)
	movaps	%xmm12,0x60(%rsp)
	movaps	%xmm13,0x70(%rsp)
	movaps	%xmm14,0x80(%rsp)
	movaps	%xmm15,0x90(%rsp)
.Locb_dec_body:
___
$code.=<<___;
	mov	$seventh_arg(%rax),$L_p		# 7th argument
	mov	$seventh_arg+8(%rax),$checksum_p# 8th argument

	mov	240($key),$rnds_
	mov	$key,$key_
	shl	\$4,$rnds_
	$movkey	($key),$rndkey0l		# round[0]
	$movkey	16($key,$rnds_),$rndkey1	# round[last]

	movdqu	($offset_p),@offset[5]		# load last offset_i
	pxor	$rndkey1,$rndkey0l		# round[0] ^ round[last]
	pxor	$rndkey1,@offset[5]		# offset_i ^ round[last]

	mov	\$16+32,$rounds
	lea	32($key_,$rnds_),$key
	$movkey	16($key_),$rndkey1		# round[1]
	sub	%r10,%rax			# twisted $rounds
	mov	%rax,%r10			# backup twisted $rounds

	movdqu	($L_p),@offset[0]		# L_0 for all odd-numbered blocks
	movdqu	($checksum_p),$checksum		# load checksum

	test	\$1,$block_num			# is first block number odd?
	jnz	.Locb_dec_odd

	bsf	$block_num,$i1
	add	\$1,$block_num
	shl	\$4,$i1
	movdqu	($L_p,$i1),$inout5		# borrow
	movdqu	($inp),$inout0
	lea	16($inp),$inp

	call	__ocb_decrypt1

	movdqa	$inout5,@offset[5]
	movups	$inout0,($out)
	xorps	$inout0,$checksum		# accumulate checksum
	lea	16($out),$out
	sub	\$1,$blocks
	jz	.Locb_dec_done

.Locb_dec_odd:
	lea	1($block_num),$i1		# even-numbered blocks
	lea	3($block_num),$i3
	lea	5($block_num),$i5
	lea	6($block_num),$block_num
	bsf	$i1,$i1				# ntz(block)
	bsf	$i3,$i3
	bsf	$i5,$i5
	shl	\$4,$i1				# ntz(block) -> table offset
	shl	\$4,$i3
	shl	\$4,$i5

	sub	\$6,$blocks
	jc	.Locb_dec_short
	jmp	.Locb_dec_grandloop

.align	32
.Locb_dec_grandloop:
	movdqu	`16*0`($inp),$inout0		# load input
	movdqu	`16*1`($inp),$inout1
	movdqu	`16*2`($inp),$inout2
	movdqu	`16*3`($inp),$inout3
	movdqu	`16*4`($inp),$inout4
	movdqu	`16*5`($inp),$inout5
	lea	`16*6`($inp),$inp

	call	__ocb_decrypt6

	movups	$inout0,`16*0`($out)		# store output
	pxor	$inout0,$checksum		# accumulate checksum
	movups	$inout1,`16*1`($out)
	pxor	$inout1,$checksum
	movups	$inout2,`16*2`($out)
	pxor	$inout2,$checksum
	movups	$inout3,`16*3`($out)
	pxor	$inout3,$checksum
	movups	$inout4,`16*4`($out)
	pxor	$inout4,$checksum
	movups	$inout5,`16*5`($out)
	pxor	$inout5,$checksum
	lea	`16*6`($out),$out
	sub	\$6,$blocks
	jnc	.Locb_dec_grandloop

.Locb_dec_short:
	add	\$6,$blocks
	jz	.Locb_dec_done

	movdqu	`16*0`($inp),$inout0
	cmp	\$2,$blocks
	jb	.Locb_dec_one
	movdqu	`16*1`($inp),$inout1
	je	.Locb_dec_two

	movdqu	`16*2`($inp),$inout2
	cmp	\$4,$blocks
	jb	.Locb_dec_three
	movdqu	`16*3`($inp),$inout3
	je	.Locb_dec_four

	movdqu	`16*4`($inp),$inout4
	pxor	$inout5,$inout5

	call	__ocb_decrypt6

	movdqa	@offset[4],@offset[5]
	movups	$inout0,`16*0`($out)		# store output
	pxor	$inout0,$checksum		# accumulate checksum
	movups	$inout1,`16*1`($out)
	pxor	$inout1,$checksum
	movups	$inout2,`16*2`($out)
	pxor	$inout2,$checksum
	movups	$inout3,`16*3`($out)
	pxor	$inout3,$checksum
	movups	$inout4,`16*4`($out)
	pxor	$inout4,$checksum

	jmp	.Locb_dec_done

.align	16
.Locb_dec_one:
	movdqa	@offset[0],$inout5		# borrow

	call	__ocb_decrypt1

	movdqa	$inout5,@offset[5]
	movups	$inout0,`16*0`($out)		# store output
	xorps	$inout0,$checksum		# accumulate checksum
	jmp	.Locb_dec_done

.align	16
.Locb_dec_two:
	pxor	$inout2,$inout2
	pxor	$inout3,$inout3

	call	__ocb_decrypt4

	movdqa	@offset[1],@offset[5]
	movups	$inout0,`16*0`($out)		# store output
	xorps	$inout0,$checksum		# accumulate checksum
	movups	$inout1,`16*1`($out)
	xorps	$inout1,$checksum

	jmp	.Locb_dec_done

.align	16
.Locb_dec_three:
	pxor	$inout3,$inout3

	call	__ocb_decrypt4

	movdqa	@offset[2],@offset[5]
	movups	$inout0,`16*0`($out)		# store output
	xorps	$inout0,$checksum		# accumulate checksum
	movups	$inout1,`16*1`($out)
	xorps	$inout1,$checksum
	movups	$inout2,`16*2`($out)
	xorps	$inout2,$checksum

	jmp	.Locb_dec_done

.align	16
.Locb_dec_four:
	call	__ocb_decrypt4

	movdqa	@offset[3],@offset[5]
	movups	$inout0,`16*0`($out)		# store output
	pxor	$inout0,$checksum		# accumulate checksum
	movups	$inout1,`16*1`($out)
	pxor	$inout1,$checksum
	movups	$inout2,`16*2`($out)
	pxor	$inout2,$checksum
	movups	$inout3,`16*3`($out)
	pxor	$inout3,$checksum

.Locb_dec_done:
	pxor	$rndkey0,@offset[5]		# "remove" round[last]
	movdqu	$checksum,($checksum_p)		# store checksum
	movdqu	@offset[5],($offset_p)		# store last offset_i

	xorps	%xmm0,%xmm0			# clear register bank
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
___
$code.=<<___ if (!$win64);
	pxor	%xmm6,%xmm6
	pxor	%xmm7,%xmm7
	pxor	%xmm8,%xmm8
	pxor	%xmm9,%xmm9
	pxor	%xmm10,%xmm10
	pxor	%xmm11,%xmm11
	pxor	%xmm12,%xmm12
	pxor	%xmm13,%xmm13
	pxor	%xmm14,%xmm14
	pxor	%xmm15,%xmm15
	lea	0x28(%rsp),%rax
.cfi_def_cfa	%rax,8
___
$code.=<<___ if ($win64);
	movaps	0x00(%rsp),%xmm6
	movaps	%xmm0,0x00(%rsp)		# clear stack
	movaps	0x10(%rsp),%xmm7
	movaps	%xmm0,0x10(%rsp)
	movaps	0x20(%rsp),%xmm8
	movaps	%xmm0,0x20(%rsp)
	movaps	0x30(%rsp),%xmm9
	movaps	%xmm0,0x30(%rsp)
	movaps	0x40(%rsp),%xmm10
	movaps	%xmm0,0x40(%rsp)
	movaps	0x50(%rsp),%xmm11
	movaps	%xmm0,0x50(%rsp)
	movaps	0x60(%rsp),%xmm12
	movaps	%xmm0,0x60(%rsp)
	movaps	0x70(%rsp),%xmm13
	movaps	%xmm0,0x70(%rsp)
	movaps	0x80(%rsp),%xmm14
	movaps	%xmm0,0x80(%rsp)
	movaps	0x90(%rsp),%xmm15
	movaps	%xmm0,0x90(%rsp)
	lea	0xa0+0x28(%rsp),%rax
.Locb_dec_pop:
___
$code.=<<___;
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
.Locb_dec_epilogue:
	ret
.cfi_endproc
.size	aesni_ocb_decrypt,.-aesni_ocb_decrypt

.type	__ocb_decrypt6,\@abi-omnipotent
.align	32
__ocb_decrypt6:
	 pxor		$rndkey0l,@offset[5]	# offset_i ^ round[0]
	 movdqu		($L_p,$i1),@offset[1]
	 movdqa		@offset[0],@offset[2]
	 movdqu		($L_p,$i3),@offset[3]
	 movdqa		@offset[0],@offset[4]
	 pxor		@offset[5],@offset[0]
	 movdqu		($L_p,$i5),@offset[5]
	 pxor		@offset[0],@offset[1]
	pxor		@offset[0],$inout0	# input ^ round[0] ^ offset_i
	 pxor		@offset[1],@offset[2]
	pxor		@offset[1],$inout1
	 pxor		@offset[2],@offset[3]
	pxor		@offset[2],$inout2
	 pxor		@offset[3],@offset[4]
	pxor		@offset[3],$inout3
	 pxor		@offset[4],@offset[5]
	pxor		@offset[4],$inout4
	pxor		@offset[5],$inout5
	$movkey		32($key_),$rndkey0

	lea		1($block_num),$i1	# even-numbered blocks
	lea		3($block_num),$i3
	lea		5($block_num),$i5
	add		\$6,$block_num
	 pxor		$rndkey0l,@offset[0]	# offset_i ^ round[last]
	bsf		$i1,$i1			# ntz(block)
	bsf		$i3,$i3
	bsf		$i5,$i5

	aesdec		$rndkey1,$inout0
	aesdec		$rndkey1,$inout1
	aesdec		$rndkey1,$inout2
	aesdec		$rndkey1,$inout3
	 pxor		$rndkey0l,@offset[1]
	 pxor		$rndkey0l,@offset[2]
	aesdec		$rndkey1,$inout4
	 pxor		$rndkey0l,@offset[3]
	 pxor		$rndkey0l,@offset[4]
	aesdec		$rndkey1,$inout5
	$movkey		48($key_),$rndkey1
	 pxor		$rndkey0l,@offset[5]

	aesdec		$rndkey0,$inout0
	aesdec		$rndkey0,$inout1
	aesdec		$rndkey0,$inout2
	aesdec		$rndkey0,$inout3
	aesdec		$rndkey0,$inout4
	aesdec		$rndkey0,$inout5
	$movkey		64($key_),$rndkey0
	shl		\$4,$i1			# ntz(block) -> table offset
	shl		\$4,$i3
	jmp		.Locb_dec_loop6

.align	32
.Locb_dec_loop6:
	aesdec		$rndkey1,$inout0
	aesdec		$rndkey1,$inout1
	aesdec		$rndkey1,$inout2
	aesdec		$rndkey1,$inout3
	aesdec		$rndkey1,$inout4
	aesdec		$rndkey1,$inout5
	$movkey		($key,%rax),$rndkey1
	add		\$32,%rax

	aesdec		$rndkey0,$inout0
	aesdec		$rndkey0,$inout1
	aesdec		$rndkey0,$inout2
	aesdec		$rndkey0,$inout3
	aesdec		$rndkey0,$inout4
	aesdec		$rndkey0,$inout5
	$movkey		-16($key,%rax),$rndkey0
	jnz		.Locb_dec_loop6

	aesdec		$rndkey1,$inout0
	aesdec		$rndkey1,$inout1
	aesdec		$rndkey1,$inout2
	aesdec		$rndkey1,$inout3
	aesdec		$rndkey1,$inout4
	aesdec		$rndkey1,$inout5
	$movkey		16($key_),$rndkey1
	shl		\$4,$i5

	aesdeclast	@offset[0],$inout0
	movdqu		($L_p),@offset[0]	# L_0 for all odd-numbered blocks
	mov		%r10,%rax		# restore twisted rounds
	aesdeclast	@offset[1],$inout1
	aesdeclast	@offset[2],$inout2
	aesdeclast	@offset[3],$inout3
	aesdeclast	@offset[4],$inout4
	aesdeclast	@offset[5],$inout5
	ret
.size	__ocb_decrypt6,.-__ocb_decrypt6

.type	__ocb_decrypt4,\@abi-omnipotent
.align	32
__ocb_decrypt4:
	 pxor		$rndkey0l,@offset[5]	# offset_i ^ round[0]
	 movdqu		($L_p,$i1),@offset[1]
	 movdqa		@offset[0],@offset[2]
	 movdqu		($L_p,$i3),@offset[3]
	 pxor		@offset[5],@offset[0]
	 pxor		@offset[0],@offset[1]
	pxor		@offset[0],$inout0	# input ^ round[0] ^ offset_i
	 pxor		@offset[1],@offset[2]
	pxor		@offset[1],$inout1
	 pxor		@offset[2],@offset[3]
	pxor		@offset[2],$inout2
	pxor		@offset[3],$inout3
	$movkey		32($key_),$rndkey0

	 pxor		$rndkey0l,@offset[0]	# offset_i ^ round[last]
	 pxor		$rndkey0l,@offset[1]
	 pxor		$rndkey0l,@offset[2]
	 pxor		$rndkey0l,@offset[3]

	aesdec		$rndkey1,$inout0
	aesdec		$rndkey1,$inout1
	aesdec		$rndkey1,$inout2
	aesdec		$rndkey1,$inout3
	$movkey		48($key_),$rndkey1

	aesdec		$rndkey0,$inout0
	aesdec		$rndkey0,$inout1
	aesdec		$rndkey0,$inout2
	aesdec		$rndkey0,$inout3
	$movkey		64($key_),$rndkey0
	jmp		.Locb_dec_loop4

.align	32
.Locb_dec_loop4:
	aesdec		$rndkey1,$inout0
	aesdec		$rndkey1,$inout1
	aesdec		$rndkey1,$inout2
	aesdec		$rndkey1,$inout3
	$movkey		($key,%rax),$rndkey1
	add		\$32,%rax

	aesdec		$rndkey0,$inout0
	aesdec		$rndkey0,$inout1
	aesdec		$rndkey0,$inout2
	aesdec		$rndkey0,$inout3
	$movkey		-16($key,%rax),$rndkey0
	jnz		.Locb_dec_loop4

	aesdec		$rndkey1,$inout0
	aesdec		$rndkey1,$inout1
	aesdec		$rndkey1,$inout2
	aesdec		$rndkey1,$inout3
	$movkey		16($key_),$rndkey1
	mov		%r10,%rax		# restore twisted rounds

	aesdeclast	@offset[0],$inout0
	aesdeclast	@offset[1],$inout1
	aesdeclast	@offset[2],$inout2
	aesdeclast	@offset[3],$inout3
	ret
.size	__ocb_decrypt4,.-__ocb_decrypt4

.type	__ocb_decrypt1,\@abi-omnipotent
.align	32
__ocb_decrypt1:
	 pxor		@offset[5],$inout5	# offset_i
	 pxor		$rndkey0l,$inout5	# offset_i ^ round[0]
	pxor		$inout5,$inout0		# input ^ round[0] ^ offset_i
	$movkey		32($key_),$rndkey0

	aesdec		$rndkey1,$inout0
	$movkey		48($key_),$rndkey1
	pxor		$rndkey0l,$inout5	# offset_i ^ round[last]

	aesdec		$rndkey0,$inout0
	$movkey		64($key_),$rndkey0
	jmp		.Locb_dec_loop1

.align	32
.Locb_dec_loop1:
	aesdec		$rndkey1,$inout0
	$movkey		($key,%rax),$rndkey1
	add		\$32,%rax

	aesdec		$rndkey0,$inout0
	$movkey		-16($key,%rax),$rndkey0
	jnz		.Locb_dec_loop1

	aesdec		$rndkey1,$inout0
	$movkey		16($key_),$rndkey1	# redundant in tail
	mov		%r10,%rax		# restore twisted rounds

	aesdeclast	$inout5,$inout0
	ret
.size	__ocb_decrypt1,.-__ocb_decrypt1
___
} }}

########################################################################
# void $PREFIX_cbc_encrypt (const void *inp, void *out,
#			    size_t length, const AES_KEY *key,
#			    unsigned char *ivp,const int enc);
{
my $frame_size = 0x10 + ($win64?0xa0:0);	# used in decrypt
my ($iv,$in0,$in1,$in2,$in3,$in4)=map("%xmm$_",(10..15));

$code.=<<___;
.globl	${PREFIX}_cbc_encrypt
.type	${PREFIX}_cbc_encrypt,\@function,6
.align	16
${PREFIX}_cbc_encrypt:
.cfi_startproc
	test	$len,$len		# check length
	jz	.Lcbc_ret

	mov	240($key),$rnds_	# key->rounds
	mov	$key,$key_		# backup $key
	test	%r9d,%r9d		# 6th argument
	jz	.Lcbc_decrypt
#--------------------------- CBC ENCRYPT ------------------------------#
	movups	($ivp),$inout0		# load iv as initial state
	mov	$rnds_,$rounds
	cmp	\$16,$len
	jb	.Lcbc_enc_tail
	sub	\$16,$len
	jmp	.Lcbc_enc_loop
.align	16
.Lcbc_enc_loop:
	movups	($inp),$inout1		# load input
	lea	16($inp),$inp
	#xorps	$inout1,$inout0
___
	&aesni_generate1("enc",$key,$rounds,$inout0,$inout1);
$code.=<<___;
	mov	$rnds_,$rounds		# restore $rounds
	mov	$key_,$key		# restore $key
	movups	$inout0,0($out)		# store output
	lea	16($out),$out
	sub	\$16,$len
	jnc	.Lcbc_enc_loop
	add	\$16,$len
	jnz	.Lcbc_enc_tail
	 pxor	$rndkey0,$rndkey0	# clear register bank
	 pxor	$rndkey1,$rndkey1
	movups	$inout0,($ivp)
	 pxor	$inout0,$inout0
	 pxor	$inout1,$inout1
	jmp	.Lcbc_ret

.Lcbc_enc_tail:
	mov	$len,%rcx	# zaps $key
	xchg	$inp,$out	# $inp is %rsi and $out is %rdi now
	.long	0x9066A4F3	# rep movsb
	mov	\$16,%ecx	# zero tail
	sub	$len,%rcx
	xor	%eax,%eax
	.long	0x9066AAF3	# rep stosb
	lea	-16(%rdi),%rdi	# rewind $out by 1 block
	mov	$rnds_,$rounds	# restore $rounds
	mov	%rdi,%rsi	# $inp and $out are the same
	mov	$key_,$key	# restore $key
	xor	$len,$len	# len=16
	jmp	.Lcbc_enc_loop	# one more spin
#--------------------------- CBC DECRYPT ------------------------------#
.align	16
.Lcbc_decrypt:
	cmp	\$16,$len
	jne	.Lcbc_decrypt_bulk

	# handle single block without allocating stack frame,
	# useful in ciphertext stealing mode
	movdqu	($inp),$inout0		# load input
	movdqu	($ivp),$inout1		# load iv
	movdqa	$inout0,$inout2		# future iv
___
	&aesni_generate1("dec",$key,$rnds_);
$code.=<<___;
	 pxor	$rndkey0,$rndkey0	# clear register bank
	 pxor	$rndkey1,$rndkey1
	movdqu	$inout2,($ivp)		# store iv
	xorps	$inout1,$inout0		# ^=iv
	 pxor	$inout1,$inout1
	movups	$inout0,($out)		# store output
	 pxor	$inout0,$inout0
	jmp	.Lcbc_ret
.align	16
.Lcbc_decrypt_bulk:
	lea	(%rsp),%r11		# frame pointer
.cfi_def_cfa_register	%r11
	push	%rbp
.cfi_push	%rbp
	sub	\$$frame_size,%rsp
	and	\$-16,%rsp	# Linux kernel stack can be incorrectly seeded
___
$code.=<<___ if ($win64);
	movaps	%xmm6,0x10(%rsp)
	movaps	%xmm7,0x20(%rsp)
	movaps	%xmm8,0x30(%rsp)
	movaps	%xmm9,0x40(%rsp)
	movaps	%xmm10,0x50(%rsp)
	movaps	%xmm11,0x60(%rsp)
	movaps	%xmm12,0x70(%rsp)
	movaps	%xmm13,0x80(%rsp)
	movaps	%xmm14,0x90(%rsp)
	movaps	%xmm15,0xa0(%rsp)
.Lcbc_decrypt_body:
___

my $inp_=$key_="%rbp";			# reassign $key_

$code.=<<___;
	mov	$key,$key_		# [re-]backup $key [after reassignment]
	movups	($ivp),$iv
	mov	$rnds_,$rounds
	cmp	\$0x50,$len
	jbe	.Lcbc_dec_tail

	$movkey	($key),$rndkey0
	movdqu	0x00($inp),$inout0	# load input
	movdqu	0x10($inp),$inout1
	movdqa	$inout0,$in0
	movdqu	0x20($inp),$inout2
	movdqa	$inout1,$in1
	movdqu	0x30($inp),$inout3
	movdqa	$inout2,$in2
	movdqu	0x40($inp),$inout4
	movdqa	$inout3,$in3
	movdqu	0x50($inp),$inout5
	movdqa	$inout4,$in4
	mov	OPENSSL_ia32cap_P+4(%rip),%r9d
	cmp	\$0x70,$len
	jbe	.Lcbc_dec_six_or_seven

	and	\$`1<<26|1<<22`,%r9d	# isolate XSAVE+MOVBE
	sub	\$0x50,$len		# $len is biased by -5*16
	cmp	\$`1<<22`,%r9d		# check for MOVBE without XSAVE
	je	.Lcbc_dec_loop6_enter	# [which denotes Atom Silvermont]
	sub	\$0x20,$len		# $len is biased by -7*16
	lea	0x70($key),$key		# size optimization
	jmp	.Lcbc_dec_loop8_enter
.align	16
.Lcbc_dec_loop8:
	movups	$inout7,($out)
	lea	0x10($out),$out
.Lcbc_dec_loop8_enter:
	movdqu		0x60($inp),$inout6
	pxor		$rndkey0,$inout0
	movdqu		0x70($inp),$inout7
	pxor		$rndkey0,$inout1
	$movkey		0x10-0x70($key),$rndkey1
	pxor		$rndkey0,$inout2
	mov		\$-1,$inp_
	cmp		\$0x70,$len	# is there at least 0x60 bytes ahead?
	pxor		$rndkey0,$inout3
	pxor		$rndkey0,$inout4
	pxor		$rndkey0,$inout5
	pxor		$rndkey0,$inout6

	aesdec		$rndkey1,$inout0
	pxor		$rndkey0,$inout7
	$movkey		0x20-0x70($key),$rndkey0
	aesdec		$rndkey1,$inout1
	aesdec		$rndkey1,$inout2
	aesdec		$rndkey1,$inout3
	aesdec		$rndkey1,$inout4
	aesdec		$rndkey1,$inout5
	aesdec		$rndkey1,$inout6
	adc		\$0,$inp_
	and		\$128,$inp_
	aesdec		$rndkey1,$inout7
	add		$inp,$inp_
	$movkey		0x30-0x70($key),$rndkey1
___
for($i=1;$i<12;$i++) {
my $rndkeyx = ($i&1)?$rndkey0:$rndkey1;
$code.=<<___	if ($i==7);
	cmp		\$11,$rounds
___
$code.=<<___;
	aesdec		$rndkeyx,$inout0
	aesdec		$rndkeyx,$inout1
	aesdec		$rndkeyx,$inout2
	aesdec		$rndkeyx,$inout3
	aesdec		$rndkeyx,$inout4
	aesdec		$rndkeyx,$inout5
	aesdec		$rndkeyx,$inout6
	aesdec		$rndkeyx,$inout7
	$movkey		`0x30+0x10*$i`-0x70($key),$rndkeyx
___
$code.=<<___	if ($i<6 || (!($i&1) && $i>7));
	nop
___
$code.=<<___	if ($i==7);
	jb		.Lcbc_dec_done
___
$code.=<<___	if ($i==9);
	je		.Lcbc_dec_done
___
$code.=<<___	if ($i==11);
	jmp		.Lcbc_dec_done
___
}
$code.=<<___;
.align	16
.Lcbc_dec_done:
	aesdec		$rndkey1,$inout0
	aesdec		$rndkey1,$inout1
	pxor		$rndkey0,$iv
	pxor		$rndkey0,$in0
	aesdec		$rndkey1,$inout2
	aesdec		$rndkey1,$inout3
	pxor		$rndkey0,$in1
	pxor		$rndkey0,$in2
	aesdec		$rndkey1,$inout4
	aesdec		$rndkey1,$inout5
	pxor		$rndkey0,$in3
	pxor		$rndkey0,$in4
	aesdec		$rndkey1,$inout6
	aesdec		$rndkey1,$inout7
	movdqu		0x50($inp),$rndkey1

	aesdeclast	$iv,$inout0
	movdqu		0x60($inp),$iv		# borrow $iv
	pxor		$rndkey0,$rndkey1
	aesdeclast	$in0,$inout1
	pxor		$rndkey0,$iv
	movdqu		0x70($inp),$rndkey0	# next IV
	aesdeclast	$in1,$inout2
	lea		0x80($inp),$inp
	movdqu		0x00($inp_),$in0
	aesdeclast	$in2,$inout3
	aesdeclast	$in3,$inout4
	movdqu		0x10($inp_),$in1
	movdqu		0x20($inp_),$in2
	aesdeclast	$in4,$inout5
	aesdeclast	$rndkey1,$inout6
	movdqu		0x30($inp_),$in3
	movdqu		0x40($inp_),$in4
	aesdeclast	$iv,$inout7
	movdqa		$rndkey0,$iv		# return $iv
	movdqu		0x50($inp_),$rndkey1
	$movkey		-0x70($key),$rndkey0

	movups		$inout0,($out)		# store output
	movdqa		$in0,$inout0
	movups		$inout1,0x10($out)
	movdqa		$in1,$inout1
	movups		$inout2,0x20($out)
	movdqa		$in2,$inout2
	movups		$inout3,0x30($out)
	movdqa		$in3,$inout3
	movups		$inout4,0x40($out)
	movdqa		$in4,$inout4
	movups		$inout5,0x50($out)
	movdqa		$rndkey1,$inout5
	movups		$inout6,0x60($out)
	lea		0x70($out),$out

	sub	\$0x80,$len
	ja	.Lcbc_dec_loop8

	movaps	$inout7,$inout0
	lea	-0x70($key),$key
	add	\$0x70,$len
	jle	.Lcbc_dec_clear_tail_collected
	movups	$inout7,($out)
	lea	0x10($out),$out
	cmp	\$0x50,$len
	jbe	.Lcbc_dec_tail

	movaps	$in0,$inout0
.Lcbc_dec_six_or_seven:
	cmp	\$0x60,$len
	ja	.Lcbc_dec_seven

	movaps	$inout5,$inout6
	call	_aesni_decrypt6
	pxor	$iv,$inout0		# ^= IV
	movaps	$inout6,$iv
	pxor	$in0,$inout1
	movdqu	$inout0,($out)
	pxor	$in1,$inout2
	movdqu	$inout1,0x10($out)
	 pxor	$inout1,$inout1		# clear register bank
	pxor	$in2,$inout3
	movdqu	$inout2,0x20($out)
	 pxor	$inout2,$inout2
	pxor	$in3,$inout4
	movdqu	$inout3,0x30($out)
	 pxor	$inout3,$inout3
	pxor	$in4,$inout5
	movdqu	$inout4,0x40($out)
	 pxor	$inout4,$inout4
	lea	0x50($out),$out
	movdqa	$inout5,$inout0
	 pxor	$inout5,$inout5
	jmp	.Lcbc_dec_tail_collected

.align	16
.Lcbc_dec_seven:
	movups	0x60($inp),$inout6
	xorps	$inout7,$inout7
	call	_aesni_decrypt8
	movups	0x50($inp),$inout7
	pxor	$iv,$inout0		# ^= IV
	movups	0x60($inp),$iv
	pxor	$in0,$inout1
	movdqu	$inout0,($out)
	pxor	$in1,$inout2
	movdqu	$inout1,0x10($out)
	 pxor	$inout1,$inout1		# clear register bank
	pxor	$in2,$inout3
	movdqu	$inout2,0x20($out)
	 pxor	$inout2,$inout2
	pxor	$in3,$inout4
	movdqu	$inout3,0x30($out)
	 pxor	$inout3,$inout3
	pxor	$in4,$inout5
	movdqu	$inout4,0x40($out)
	 pxor	$inout4,$inout4
	pxor	$inout7,$inout6
	movdqu	$inout5,0x50($out)
	 pxor	$inout5,$inout5
	lea	0x60($out),$out
	movdqa	$inout6,$inout0
	 pxor	$inout6,$inout6
	 pxor	$inout7,$inout7
	jmp	.Lcbc_dec_tail_collected

.align	16
.Lcbc_dec_loop6:
	movups	$inout5,($out)
	lea	0x10($out),$out
	movdqu	0x00($inp),$inout0	# load input
	movdqu	0x10($inp),$inout1
	movdqa	$inout0,$in0
	movdqu	0x20($inp),$inout2
	movdqa	$inout1,$in1
	movdqu	0x30($inp),$inout3
	movdqa	$inout2,$in2
	movdqu	0x40($inp),$inout4
	movdqa	$inout3,$in3
	movdqu	0x50($inp),$inout5
	movdqa	$inout4,$in4
.Lcbc_dec_loop6_enter:
	lea	0x60($inp),$inp
	movdqa	$inout5,$inout6

	call	_aesni_decrypt6

	pxor	$iv,$inout0		# ^= IV
	movdqa	$inout6,$iv
	pxor	$in0,$inout1
	movdqu	$inout0,($out)
	pxor	$in1,$inout2
	movdqu	$inout1,0x10($out)
	pxor	$in2,$inout3
	movdqu	$inout2,0x20($out)
	pxor	$in3,$inout4
	mov	$key_,$key
	movdqu	$inout3,0x30($out)
	pxor	$in4,$inout5
	mov	$rnds_,$rounds
	movdqu	$inout4,0x40($out)
	lea	0x50($out),$out
	sub	\$0x60,$len
	ja	.Lcbc_dec_loop6

	movdqa	$inout5,$inout0
	add	\$0x50,$len
	jle	.Lcbc_dec_clear_tail_collected
	movups	$inout5,($out)
	lea	0x10($out),$out

.Lcbc_dec_tail:
	movups	($inp),$inout0
	sub	\$0x10,$len
	jbe	.Lcbc_dec_one		# $len is 1*16 or less

	movups	0x10($inp),$inout1
	movaps	$inout0,$in0
	sub	\$0x10,$len
	jbe	.Lcbc_dec_two		# $len is 2*16 or less

	movups	0x20($inp),$inout2
	movaps	$inout1,$in1
	sub	\$0x10,$len
	jbe	.Lcbc_dec_three		# $len is 3*16 or less

	movups	0x30($inp),$inout3
	movaps	$inout2,$in2
	sub	\$0x10,$len
	jbe	.Lcbc_dec_four		# $len is 4*16 or less

	movups	0x40($inp),$inout4	# $len is 5*16 or less
	movaps	$inout3,$in3
	movaps	$inout4,$in4
	xorps	$inout5,$inout5
	call	_aesni_decrypt6
	pxor	$iv,$inout0
	movaps	$in4,$iv
	pxor	$in0,$inout1
	movdqu	$inout0,($out)
	pxor	$in1,$inout2
	movdqu	$inout1,0x10($out)
	 pxor	$inout1,$inout1		# clear register bank
	pxor	$in2,$inout3
	movdqu	$inout2,0x20($out)
	 pxor	$inout2,$inout2
	pxor	$in3,$inout4
	movdqu	$inout3,0x30($out)
	 pxor	$inout3,$inout3
	lea	0x40($out),$out
	movdqa	$inout4,$inout0
	 pxor	$inout4,$inout4
	 pxor	$inout5,$inout5
	sub	\$0x10,$len
	jmp	.Lcbc_dec_tail_collected

.align	16
.Lcbc_dec_one:
	movaps	$inout0,$in0
___
	&aesni_generate1("dec",$key,$rounds);
$code.=<<___;
	xorps	$iv,$inout0
	movaps	$in0,$iv
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_two:
	movaps	$inout1,$in1
	call	_aesni_decrypt2
	pxor	$iv,$inout0
	movaps	$in1,$iv
	pxor	$in0,$inout1
	movdqu	$inout0,($out)
	movdqa	$inout1,$inout0
	 pxor	$inout1,$inout1		# clear register bank
	lea	0x10($out),$out
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_three:
	movaps	$inout2,$in2
	call	_aesni_decrypt3
	pxor	$iv,$inout0
	movaps	$in2,$iv
	pxor	$in0,$inout1
	movdqu	$inout0,($out)
	pxor	$in1,$inout2
	movdqu	$inout1,0x10($out)
	 pxor	$inout1,$inout1		# clear register bank
	movdqa	$inout2,$inout0
	 pxor	$inout2,$inout2
	lea	0x20($out),$out
	jmp	.Lcbc_dec_tail_collected
.align	16
.Lcbc_dec_four:
	movaps	$inout3,$in3
	call	_aesni_decrypt4
	pxor	$iv,$inout0
	movaps	$in3,$iv
	pxor	$in0,$inout1
	movdqu	$inout0,($out)
	pxor	$in1,$inout2
	movdqu	$inout1,0x10($out)
	 pxor	$inout1,$inout1		# clear register bank
	pxor	$in2,$inout3
	movdqu	$inout2,0x20($out)
	 pxor	$inout2,$inout2
	movdqa	$inout3,$inout0
	 pxor	$inout3,$inout3
	lea	0x30($out),$out
	jmp	.Lcbc_dec_tail_collected

.align	16
.Lcbc_dec_clear_tail_collected:
	pxor	$inout1,$inout1		# clear register bank
	pxor	$inout2,$inout2
	pxor	$inout3,$inout3
___
$code.=<<___ if (!$win64);
	pxor	$inout4,$inout4		# %xmm6..9
	pxor	$inout5,$inout5
	pxor	$inout6,$inout6
	pxor	$inout7,$inout7
___
$code.=<<___;
.Lcbc_dec_tail_collected:
	movups	$iv,($ivp)
	and	\$15,$len
	jnz	.Lcbc_dec_tail_partial
	movups	$inout0,($out)
	pxor	$inout0,$inout0
	jmp	.Lcbc_dec_ret
.align	16
.Lcbc_dec_tail_partial:
	movaps	$inout0,(%rsp)
	pxor	$inout0,$inout0
	mov	\$16,%rcx
	mov	$out,%rdi
	sub	$len,%rcx
	lea	(%rsp),%rsi
	.long	0x9066A4F3		# rep movsb
	movdqa	$inout0,(%rsp)

.Lcbc_dec_ret:
	xorps	$rndkey0,$rndkey0	# %xmm0
	pxor	$rndkey1,$rndkey1
___
$code.=<<___ if ($win64);
	movaps	0x10(%rsp),%xmm6
	movaps	%xmm0,0x10(%rsp)	# clear stack
	movaps	0x20(%rsp),%xmm7
	movaps	%xmm0,0x20(%rsp)
	movaps	0x30(%rsp),%xmm8
	movaps	%xmm0,0x30(%rsp)
	movaps	0x40(%rsp),%xmm9
	movaps	%xmm0,0x40(%rsp)
	movaps	0x50(%rsp),%xmm10
	movaps	%xmm0,0x50(%rsp)
	movaps	0x60(%rsp),%xmm11
	movaps	%xmm0,0x60(%rsp)
	movaps	0x70(%rsp),%xmm12
	movaps	%xmm0,0x70(%rsp)
	movaps	0x80(%rsp),%xmm13
	movaps	%xmm0,0x80(%rsp)
	movaps	0x90(%rsp),%xmm14
	movaps	%xmm0,0x90(%rsp)
	movaps	0xa0(%rsp),%xmm15
	movaps	%xmm0,0xa0(%rsp)
___
$code.=<<___;
	mov	-8(%r11),%rbp
.cfi_restore	%rbp
	lea	(%r11),%rsp
.cfi_def_cfa_register	%rsp
.Lcbc_ret:
	ret
.cfi_endproc
.size	${PREFIX}_cbc_encrypt,.-${PREFIX}_cbc_encrypt
___
} 
# int ${PREFIX}_set_decrypt_key(const unsigned char *inp,
#				int bits, AES_KEY *key)
#
# input:	$inp	user-supplied key
#		$bits	$inp length in bits
#		$key	pointer to key schedule
# output:	%eax	0 denoting success, -1 or -2 - failure (see C)
#		*$key	key schedule
#
{ my ($inp,$bits,$key) = @_4args;
  $bits =~ s/%r/%e/;

$code.=<<___;
.globl	${PREFIX}_set_decrypt_key
.type	${PREFIX}_set_decrypt_key,\@abi-omnipotent
.align	16
${PREFIX}_set_decrypt_key:
.cfi_startproc
	.byte	0x48,0x83,0xEC,0x08	# sub rsp,8
.cfi_adjust_cfa_offset	8
	call	__aesni_set_encrypt_key
	shl	\$4,$bits		# rounds-1 after _aesni_set_encrypt_key
	test	%eax,%eax
	jnz	.Ldec_key_ret
	lea	16($key,$bits),$inp	# points at the end of key schedule

	$movkey	($key),%xmm0		# just swap
	$movkey	($inp),%xmm1
	$movkey	%xmm0,($inp)
	$movkey	%xmm1,($key)
	lea	16($key),$key
	lea	-16($inp),$inp

.Ldec_key_inverse:
	$movkey	($key),%xmm0		# swap and inverse
	$movkey	($inp),%xmm1
	aesimc	%xmm0,%xmm0
	aesimc	%xmm1,%xmm1
	lea	16($key),$key
	lea	-16($inp),$inp
	$movkey	%xmm0,16($inp)
	$movkey	%xmm1,-16($key)
	cmp	$key,$inp
	ja	.Ldec_key_inverse

	$movkey	($key),%xmm0		# inverse middle
	aesimc	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	$movkey	%xmm0,($inp)
	pxor	%xmm0,%xmm0
.Ldec_key_ret:
	add	\$8,%rsp
.cfi_adjust_cfa_offset	-8
	ret
.cfi_endproc
.LSEH_end_set_decrypt_key:
.size	${PREFIX}_set_decrypt_key,.-${PREFIX}_set_decrypt_key
___

# This is based on submission from Intel by
#	Huang Ying
#	Vinodh Gopal
#	Kahraman Akdemir
#
# Aggressively optimized in respect to aeskeygenassist's critical path
# and is contained in %xmm0-5 to meet Win64 ABI requirement.
#
# int ${PREFIX}_set_encrypt_key(const unsigned char *inp,
#				int bits, AES_KEY * const key);
#
# input:	$inp	user-supplied key
#		$bits	$inp length in bits
#		$key	pointer to key schedule
# output:	%eax	0 denoting success, -1 or -2 - failure (see C)
#		$bits	rounds-1 (used in aesni_set_decrypt_key)
#		*$key	key schedule
#		$key	pointer to key schedule (used in
#			aesni_set_decrypt_key)
#
# Subroutine is frame-less, which means that only volatile registers
# are used. Note that it's declared "abi-omnipotent", which means that
# amount of volatile registers is smaller on Windows.
#
$code.=<<___;
.globl	${PREFIX}_set_encrypt_key
.type	${PREFIX}_set_encrypt_key,\@abi-omnipotent
.align	16
${PREFIX}_set_encrypt_key:
__aesni_set_encrypt_key:
.cfi_startproc
	.byte	0x48,0x83,0xEC,0x08	# sub rsp,8
.cfi_adjust_cfa_offset	8
	mov	\$-1,%rax
	test	$inp,$inp
	jz	.Lenc_key_ret
	test	$key,$key
	jz	.Lenc_key_ret

	mov	\$`1<<28|1<<11`,%r10d	# AVX and XOP bits
	movups	($inp),%xmm0		# pull first 128 bits of *userKey
	xorps	%xmm4,%xmm4		# low dword of xmm4 is assumed 0
	and	OPENSSL_ia32cap_P+4(%rip),%r10d
	lea	16($key),%rax		# %rax is used as modifiable copy of $key
	cmp	\$256,$bits
	je	.L14rounds
	cmp	\$192,$bits
	je	.L12rounds
	cmp	\$128,$bits
	jne	.Lbad_keybits

.L10rounds:
	mov	\$9,$bits			# 10 rounds for 128-bit key
	cmp	\$`1<<28`,%r10d			# AVX, bit no XOP
	je	.L10rounds_alt

	$movkey	%xmm0,($key)			# round 0
	aeskeygenassist	\$0x1,%xmm0,%xmm1	# round 1
	call		.Lkey_expansion_128_cold
	aeskeygenassist	\$0x2,%xmm0,%xmm1	# round 2
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x4,%xmm0,%xmm1	# round 3
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x8,%xmm0,%xmm1	# round 4
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x10,%xmm0,%xmm1	# round 5
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x20,%xmm0,%xmm1	# round 6
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x40,%xmm0,%xmm1	# round 7
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x80,%xmm0,%xmm1	# round 8
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x1b,%xmm0,%xmm1	# round 9
	call		.Lkey_expansion_128
	aeskeygenassist	\$0x36,%xmm0,%xmm1	# round 10
	call		.Lkey_expansion_128
	$movkey	%xmm0,(%rax)
	mov	$bits,80(%rax)	# 240(%rdx)
	xor	%eax,%eax
	jmp	.Lenc_key_ret

.align	16
.L10rounds_alt:
	movdqa	.Lkey_rotate(%rip),%xmm5
	mov	\$8,%r10d
	movdqa	.Lkey_rcon1(%rip),%xmm4
	movdqa	%xmm0,%xmm2
	movdqu	%xmm0,($key)
	jmp	.Loop_key128

.align	16
.Loop_key128:
	pshufb		%xmm5,%xmm0
	aesenclast	%xmm4,%xmm0
	pslld		\$1,%xmm4
	lea		16(%rax),%rax

	movdqa		%xmm2,%xmm3
	pslldq		\$4,%xmm2
	pxor		%xmm2,%xmm3
	pslldq		\$4,%xmm2
	pxor		%xmm2,%xmm3
	pslldq		\$4,%xmm2
	pxor		%xmm3,%xmm2

	pxor		%xmm2,%xmm0
	movdqu		%xmm0,-16(%rax)
	movdqa		%xmm0,%xmm2

	dec	%r10d
	jnz	.Loop_key128

	movdqa		.Lkey_rcon1b(%rip),%xmm4

	pshufb		%xmm5,%xmm0
	aesenclast	%xmm4,%xmm0
	pslld		\$1,%xmm4

	movdqa		%xmm2,%xmm3
	pslldq		\$4,%xmm2
	pxor		%xmm2,%xmm3
	pslldq		\$4,%xmm2
	pxor		%xmm2,%xmm3
	pslldq		\$4,%xmm2
	pxor		%xmm3,%xmm2

	pxor		%xmm2,%xmm0
	movdqu		%xmm0,(%rax)

	movdqa		%xmm0,%xmm2
	pshufb		%xmm5,%xmm0
	aesenclast	%xmm4,%xmm0

	movdqa		%xmm2,%xmm3
	pslldq		\$4,%xmm2
	pxor		%xmm2,%xmm3
	pslldq		\$4,%xmm2
	pxor		%xmm2,%xmm3
	pslldq		\$4,%xmm2
	pxor		%xmm3,%xmm2

	pxor		%xmm2,%xmm0
	movdqu		%xmm0,16(%rax)

	mov	$bits,96(%rax)	# 240($key)
	xor	%eax,%eax
	jmp	.Lenc_key_ret

.align	16
.L12rounds:
	movq	16($inp),%xmm2			# remaining 1/3 of *userKey
	mov	\$11,$bits			# 12 rounds for 192
	cmp	\$`1<<28`,%r10d			# AVX, but no XOP
	je	.L12rounds_alt

	$movkey	%xmm0,($key)			# round 0
	aeskeygenassist	\$0x1,%xmm2,%xmm1	# round 1,2
	call		.Lkey_expansion_192a_cold
	aeskeygenassist	\$0x2,%xmm2,%xmm1	# round 2,3
	call		.Lkey_expansion_192b
	aeskeygenassist	\$0x4,%xmm2,%xmm1	# round 4,5
	call		.Lkey_expansion_192a
	aeskeygenassist	\$0x8,%xmm2,%xmm1	# round 5,6
	call		.Lkey_expansion_192b
	aeskeygenassist	\$0x10,%xmm2,%xmm1	# round 7,8
	call		.Lkey_expansion_192a
	aeskeygenassist	\$0x20,%xmm2,%xmm1	# round 8,9
	call		.Lkey_expansion_192b
	aeskeygenassist	\$0x40,%xmm2,%xmm1	# round 10,11
	call		.Lkey_expansion_192a
	aeskeygenassist	\$0x80,%xmm2,%xmm1	# round 11,12
	call		.Lkey_expansion_192b
	$movkey	%xmm0,(%rax)
	mov	$bits,48(%rax)	# 240(%rdx)
	xor	%rax, %rax
	jmp	.Lenc_key_ret

.align	16
.L12rounds_alt:
	movdqa	.Lkey_rotate192(%rip),%xmm5
	movdqa	.Lkey_rcon1(%rip),%xmm4
	mov	\$8,%r10d
	movdqu	%xmm0,($key)
	jmp	.Loop_key192

.align	16
.Loop_key192:
	movq		%xmm2,0(%rax)
	movdqa		%xmm2,%xmm1
	pshufb		%xmm5,%xmm2
	aesenclast	%xmm4,%xmm2
	pslld		\$1, %xmm4
	lea		24(%rax),%rax

	movdqa		%xmm0,%xmm3
	pslldq		\$4,%xmm0
	pxor		%xmm0,%xmm3
	pslldq		\$4,%xmm0
	pxor		%xmm0,%xmm3
	pslldq		\$4,%xmm0
	pxor		%xmm3,%xmm0

	pshufd		\$0xff,%xmm0,%xmm3
	pxor		%xmm1,%xmm3
	pslldq		\$4,%xmm1
	pxor		%xmm1,%xmm3

	pxor		%xmm2,%xmm0
	pxor		%xmm3,%xmm2
	movdqu		%xmm0,-16(%rax)

	dec	%r10d
	jnz	.Loop_key192

	mov	$bits,32(%rax)	# 240($key)
	xor	%eax,%eax
	jmp	.Lenc_key_ret

.align	16
.L14rounds:
	movups	16($inp),%xmm2			# remaining half of *userKey
	mov	\$13,$bits			# 14 rounds for 256
	lea	16(%rax),%rax
	cmp	\$`1<<28`,%r10d			# AVX, but no XOP
	je	.L14rounds_alt

	$movkey	%xmm0,($key)			# round 0
	$movkey	%xmm2,16($key)			# round 1
	aeskeygenassist	\$0x1,%xmm2,%xmm1	# round 2
	call		.Lkey_expansion_256a_cold
	aeskeygenassist	\$0x1,%xmm0,%xmm1	# round 3
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x2,%xmm2,%xmm1	# round 4
	call		.Lkey_expansion_256a
	aeskeygenassist	\$0x2,%xmm0,%xmm1	# round 5
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x4,%xmm2,%xmm1	# round 6
	call		.Lkey_expansion_256a
	aeskeygenassist	\$0x4,%xmm0,%xmm1	# round 7
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x8,%xmm2,%xmm1	# round 8
	call		.Lkey_expansion_256a
	aeskeygenassist	\$0x8,%xmm0,%xmm1	# round 9
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x10,%xmm2,%xmm1	# round 10
	call		.Lkey_expansion_256a
	aeskeygenassist	\$0x10,%xmm0,%xmm1	# round 11
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x20,%xmm2,%xmm1	# round 12
	call		.Lkey_expansion_256a
	aeskeygenassist	\$0x20,%xmm0,%xmm1	# round 13
	call		.Lkey_expansion_256b
	aeskeygenassist	\$0x40,%xmm2,%xmm1	# round 14
	call		.Lkey_expansion_256a
	$movkey	%xmm0,(%rax)
	mov	$bits,16(%rax)	# 240(%rdx)
	xor	%rax,%rax
	jmp	.Lenc_key_ret

.align	16
.L14rounds_alt:
	movdqa	.Lkey_rotate(%rip),%xmm5
	movdqa	.Lkey_rcon1(%rip),%xmm4
	mov	\$7,%r10d
	movdqu	%xmm0,0($key)
	movdqa	%xmm2,%xmm1
	movdqu	%xmm2,16($key)
	jmp	.Loop_key256

.align	16
.Loop_key256:
	pshufb		%xmm5,%xmm2
	aesenclast	%xmm4,%xmm2

	movdqa		%xmm0,%xmm3
	pslldq		\$4,%xmm0
	pxor		%xmm0,%xmm3
	pslldq		\$4,%xmm0
	pxor		%xmm0,%xmm3
	pslldq		\$4,%xmm0
	pxor		%xmm3,%xmm0
	pslld		\$1,%xmm4

	pxor		%xmm2,%xmm0
	movdqu		%xmm0,(%rax)

	dec	%r10d
	jz	.Ldone_key256

	pshufd		\$0xff,%xmm0,%xmm2
	pxor		%xmm3,%xmm3
	aesenclast	%xmm3,%xmm2

	movdqa		%xmm1,%xmm3
	pslldq		\$4,%xmm1
	pxor		%xmm1,%xmm3
	pslldq		\$4,%xmm1
	pxor		%xmm1,%xmm3
	pslldq		\$4,%xmm1
	pxor		%xmm3,%xmm1

	pxor		%xmm1,%xmm2
	movdqu		%xmm2,16(%rax)
	lea		32(%rax),%rax
	movdqa		%xmm2,%xmm1

	jmp	.Loop_key256

.Ldone_key256:
	mov	$bits,16(%rax)	# 240($key)
	xor	%eax,%eax
	jmp	.Lenc_key_ret

.align	16
.Lbad_keybits:
	mov	\$-2,%rax
.Lenc_key_ret:
	pxor	%xmm0,%xmm0
	pxor	%xmm1,%xmm1
	pxor	%xmm2,%xmm2
	pxor	%xmm3,%xmm3
	pxor	%xmm4,%xmm4
	pxor	%xmm5,%xmm5
	add	\$8,%rsp
.cfi_adjust_cfa_offset	-8
	ret
.cfi_endproc
.LSEH_end_set_encrypt_key:

.align	16
.Lkey_expansion_128:
	$movkey	%xmm0,(%rax)
	lea	16(%rax),%rax
.Lkey_expansion_128_cold:
	shufps	\$0b00010000,%xmm0,%xmm4
	xorps	%xmm4, %xmm0
	shufps	\$0b10001100,%xmm0,%xmm4
	xorps	%xmm4, %xmm0
	shufps	\$0b11111111,%xmm1,%xmm1	# critical path
	xorps	%xmm1,%xmm0
	ret

.align 16
.Lkey_expansion_192a:
	$movkey	%xmm0,(%rax)
	lea	16(%rax),%rax
.Lkey_expansion_192a_cold:
	movaps	%xmm2, %xmm5
.Lkey_expansion_192b_warm:
	shufps	\$0b00010000,%xmm0,%xmm4
	movdqa	%xmm2,%xmm3
	xorps	%xmm4,%xmm0
	shufps	\$0b10001100,%xmm0,%xmm4
	pslldq	\$4,%xmm3
	xorps	%xmm4,%xmm0
	pshufd	\$0b01010101,%xmm1,%xmm1	# critical path
	pxor	%xmm3,%xmm2
	pxor	%xmm1,%xmm0
	pshufd	\$0b11111111,%xmm0,%xmm3
	pxor	%xmm3,%xmm2
	ret

.align 16
.Lkey_expansion_192b:
	movaps	%xmm0,%xmm3
	shufps	\$0b01000100,%xmm0,%xmm5
	$movkey	%xmm5,(%rax)
	shufps	\$0b01001110,%xmm2,%xmm3
	$movkey	%xmm3,16(%rax)
	lea	32(%rax),%rax
	jmp	.Lkey_expansion_192b_warm

.align	16
.Lkey_expansion_256a:
	$movkey	%xmm2,(%rax)
	lea	16(%rax),%rax
.Lkey_expansion_256a_cold:
	shufps	\$0b00010000,%xmm0,%xmm4
	xorps	%xmm4,%xmm0
	shufps	\$0b10001100,%xmm0,%xmm4
	xorps	%xmm4,%xmm0
	shufps	\$0b11111111,%xmm1,%xmm1	# critical path
	xorps	%xmm1,%xmm0
	ret

.align 16
.Lkey_expansion_256b:
	$movkey	%xmm0,(%rax)
	lea	16(%rax),%rax

	shufps	\$0b00010000,%xmm2,%xmm4
	xorps	%xmm4,%xmm2
	shufps	\$0b10001100,%xmm2,%xmm4
	xorps	%xmm4,%xmm2
	shufps	\$0b10101010,%xmm1,%xmm1	# critical path
	xorps	%xmm1,%xmm2
	ret
.size	${PREFIX}_set_encrypt_key,.-${PREFIX}_set_encrypt_key
.size	__aesni_set_encrypt_key,.-__aesni_set_encrypt_key
___
}

$code.=<<___;
.align	64
.Lbswap_mask:
	.byte	15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0
.Lincrement32:
	.long	6,6,6,0
.Lincrement64:
	.long	1,0,0,0
.Lxts_magic:
	.long	0x87,0,1,0
.Lincrement1:
	.byte	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1
.Lkey_rotate:
	.long	0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d
.Lkey_rotate192:
	.long	0x04070605,0x04070605,0x04070605,0x04070605
.Lkey_rcon1:
	.long	1,1,1,1
.Lkey_rcon1b:
	.long	0x1b,0x1b,0x1b,0x1b

.asciz  "AES for Intel AES-NI, CRYPTOGAMS by <appro\@openssl.org>"
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
___
$code.=<<___ if ($PREFIX eq "aesni");
.type	ecb_ccm64_se_handler,\@abi-omnipotent
.align	16
ecb_ccm64_se_handler:
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

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail

	lea	0(%rax),%rsi		# %xmm save area
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$8,%ecx		# 4*sizeof(%xmm0)/sizeof(%rax)
	.long	0xa548f3fc		# cld; rep movsq
	lea	0x58(%rax),%rax		# adjust stack pointer

	jmp	.Lcommon_seh_tail
.size	ecb_ccm64_se_handler,.-ecb_ccm64_se_handler

.type	ctr_xts_se_handler,\@abi-omnipotent
.align	16
ctr_xts_se_handler:
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
	lea	(%rsi,%r10),%r10	# prologue lable
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lcommon_seh_tail

	mov	152($context),%rax	# pull context->Rsp

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail

	mov	208($context),%rax	# pull context->R11

	lea	-0xa8(%rax),%rsi	# %xmm save area
	lea	512($context),%rdi	# & context.Xmm6
	mov	\$20,%ecx		# 10*sizeof(%xmm0)/sizeof(%rax)
	.long	0xa548f3fc		# cld; rep movsq

	mov	-8(%rax),%rbp		# restore saved %rbp
	mov	%rbp,160($context)	# restore context->Rbp
	jmp	.Lcommon_seh_tail
.size	ctr_xts_se_handler,.-ctr_xts_se_handler

.type	ocb_se_handler,\@abi-omnipotent
.align	16
ocb_se_handler:
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
	lea	(%rsi,%r10),%r10	# prologue lable
	cmp	%r10,%rbx		# context->Rip<prologue label
	jb	.Lcommon_seh_tail

	mov	4(%r11),%r10d		# HandlerData[1]
	lea	(%rsi,%r10),%r10	# epilogue label
	cmp	%r10,%rbx		# context->Rip>=epilogue label
	jae	.Lcommon_seh_tail

	mov	8(%r11),%r10d		# HandlerData[2]
	lea	(%rsi,%r10),%r10
	cmp	%r10,%rbx		# context->Rip>=pop label
	jae	.Locb_no_xmm

	mov	152($context),%rax	# pull context->Rsp

	lea	(%rax),%rsi		# %xmm save area
	lea	512($context),%rdi	# & context.Xmm6
	mov	\$20,%ecx		# 10*sizeof(%xmm0)/sizeof(%rax)
	.long	0xa548f3fc		# cld; rep movsq
	lea	0xa0+0x28(%rax),%rax

.Locb_no_xmm:
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
.size	ocb_se_handler,.-ocb_se_handler
___
$code.=<<___;
.type	cbc_se_handler,\@abi-omnipotent
.align	16
cbc_se_handler:
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

	mov	152($context),%rax	# pull context->Rsp
	mov	248($context),%rbx	# pull context->Rip

	lea	.Lcbc_decrypt_bulk(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<"prologue" label
	jb	.Lcommon_seh_tail

	mov	120($context),%rax	# pull context->Rax

	lea	.Lcbc_decrypt_body(%rip),%r10
	cmp	%r10,%rbx		# context->Rip<cbc_decrypt_body
	jb	.Lcommon_seh_tail

	mov	152($context),%rax	# pull context->Rsp

	lea	.Lcbc_ret(%rip),%r10
	cmp	%r10,%rbx		# context->Rip>="epilogue" label
	jae	.Lcommon_seh_tail

	lea	16(%rax),%rsi		# %xmm save area
	lea	512($context),%rdi	# &context.Xmm6
	mov	\$20,%ecx		# 10*sizeof(%xmm0)/sizeof(%rax)
	.long	0xa548f3fc		# cld; rep movsq

	mov	208($context),%rax	# pull context->R11

	mov	-8(%rax),%rbp		# restore saved %rbp
	mov	%rbp,160($context)	# restore context->Rbp

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
.size	cbc_se_handler,.-cbc_se_handler

.section	.pdata
.align	4
___
$code.=<<___ if ($PREFIX eq "aesni");
	.rva	.LSEH_begin_aesni_ecb_encrypt
	.rva	.LSEH_end_aesni_ecb_encrypt
	.rva	.LSEH_info_ecb

	.rva	.LSEH_begin_aesni_ccm64_encrypt_blocks
	.rva	.LSEH_end_aesni_ccm64_encrypt_blocks
	.rva	.LSEH_info_ccm64_enc

	.rva	.LSEH_begin_aesni_ccm64_decrypt_blocks
	.rva	.LSEH_end_aesni_ccm64_decrypt_blocks
	.rva	.LSEH_info_ccm64_dec

	.rva	.LSEH_begin_aesni_ctr32_encrypt_blocks
	.rva	.LSEH_end_aesni_ctr32_encrypt_blocks
	.rva	.LSEH_info_ctr32

	.rva	.LSEH_begin_aesni_xts_encrypt
	.rva	.LSEH_end_aesni_xts_encrypt
	.rva	.LSEH_info_xts_enc

	.rva	.LSEH_begin_aesni_xts_decrypt
	.rva	.LSEH_end_aesni_xts_decrypt
	.rva	.LSEH_info_xts_dec

	.rva	.LSEH_begin_aesni_ocb_encrypt
	.rva	.LSEH_end_aesni_ocb_encrypt
	.rva	.LSEH_info_ocb_enc

	.rva	.LSEH_begin_aesni_ocb_decrypt
	.rva	.LSEH_end_aesni_ocb_decrypt
	.rva	.LSEH_info_ocb_dec
___
$code.=<<___;
	.rva	.LSEH_begin_${PREFIX}_cbc_encrypt
	.rva	.LSEH_end_${PREFIX}_cbc_encrypt
	.rva	.LSEH_info_cbc

	.rva	${PREFIX}_set_decrypt_key
	.rva	.LSEH_end_set_decrypt_key
	.rva	.LSEH_info_key

	.rva	${PREFIX}_set_encrypt_key
	.rva	.LSEH_end_set_encrypt_key
	.rva	.LSEH_info_key
.section	.xdata
.align	8
___
$code.=<<___ if ($PREFIX eq "aesni");
.LSEH_info_ecb:
	.byte	9,0,0,0
	.rva	ecb_ccm64_se_handler
	.rva	.Lecb_enc_body,.Lecb_enc_ret		# HandlerData[]
.LSEH_info_ccm64_enc:
	.byte	9,0,0,0
	.rva	ecb_ccm64_se_handler
	.rva	.Lccm64_enc_body,.Lccm64_enc_ret	# HandlerData[]
.LSEH_info_ccm64_dec:
	.byte	9,0,0,0
	.rva	ecb_ccm64_se_handler
	.rva	.Lccm64_dec_body,.Lccm64_dec_ret	# HandlerData[]
.LSEH_info_ctr32:
	.byte	9,0,0,0
	.rva	ctr_xts_se_handler
	.rva	.Lctr32_body,.Lctr32_epilogue		# HandlerData[]
.LSEH_info_xts_enc:
	.byte	9,0,0,0
	.rva	ctr_xts_se_handler
	.rva	.Lxts_enc_body,.Lxts_enc_epilogue	# HandlerData[]
.LSEH_info_xts_dec:
	.byte	9,0,0,0
	.rva	ctr_xts_se_handler
	.rva	.Lxts_dec_body,.Lxts_dec_epilogue	# HandlerData[]
.LSEH_info_ocb_enc:
	.byte	9,0,0,0
	.rva	ocb_se_handler
	.rva	.Locb_enc_body,.Locb_enc_epilogue	# HandlerData[]
	.rva	.Locb_enc_pop
	.long	0
.LSEH_info_ocb_dec:
	.byte	9,0,0,0
	.rva	ocb_se_handler
	.rva	.Locb_dec_body,.Locb_dec_epilogue	# HandlerData[]
	.rva	.Locb_dec_pop
	.long	0
___
$code.=<<___;
.LSEH_info_cbc:
	.byte	9,0,0,0
	.rva	cbc_se_handler
.LSEH_info_key:
	.byte	0x01,0x04,0x01,0x00
	.byte	0x04,0x02,0x00,0x00	# sub rsp,8
___
}

sub rex {
  local *opcode=shift;
  my ($dst,$src)=@_;
  my $rex=0;

    $rex|=0x04			if($dst>=8);
    $rex|=0x01			if($src>=8);
    push @opcode,$rex|0x40	if($rex);
}

sub aesni {
  my $line=shift;
  my @opcode=(0x66);

    if ($line=~/(aeskeygenassist)\s+\$([x0-9a-f]+),\s*%xmm([0-9]+),\s*%xmm([0-9]+)/) {
	rex(\@opcode,$4,$3);
	push @opcode,0x0f,0x3a,0xdf;
	push @opcode,0xc0|($3&7)|(($4&7)<<3);	# ModR/M
	my $c=$2;
	push @opcode,$c=~/^0/?oct($c):$c;
	return ".byte\t".join(',',@opcode);
    }
    elsif ($line=~/(aes[a-z]+)\s+%xmm([0-9]+),\s*%xmm([0-9]+)/) {
	my %opcodelet = (
		"aesimc" => 0xdb,
		"aesenc" => 0xdc,	"aesenclast" => 0xdd,
		"aesdec" => 0xde,	"aesdeclast" => 0xdf
	);
	return undef if (!defined($opcodelet{$1}));
	rex(\@opcode,$3,$2);
	push @opcode,0x0f,0x38,$opcodelet{$1};
	push @opcode,0xc0|($2&7)|(($3&7)<<3);	# ModR/M
	return ".byte\t".join(',',@opcode);
    }
    elsif ($line=~/(aes[a-z]+)\s+([0x1-9a-fA-F]*)\(%rsp\),\s*%xmm([0-9]+)/) {
	my %opcodelet = (
		"aesenc" => 0xdc,	"aesenclast" => 0xdd,
		"aesdec" => 0xde,	"aesdeclast" => 0xdf
	);
	return undef if (!defined($opcodelet{$1}));
	my $off = $2;
	push @opcode,0x44 if ($3>=8);
	push @opcode,0x0f,0x38,$opcodelet{$1};
	push @opcode,0x44|(($3&7)<<3),0x24;	# ModR/M
	push @opcode,($off=~/^0/?oct($off):$off)&0xff;
	return ".byte\t".join(',',@opcode);
    }
    return $line;
}

sub movbe {
	".byte	0x0f,0x38,0xf1,0x44,0x24,".shift;
}

$code =~ s/\`([^\`]*)\`/eval($1)/gem;
$code =~ s/\b(aes.*%xmm[0-9]+).*$/aesni($1)/gem;
#$code =~ s/\bmovbe\s+%eax/bswap %eax; mov %eax/gm;	# debugging artefact
$code =~ s/\bmovbe\s+%eax,\s*([0-9]+)\(%rsp\)/movbe($1)/gem;

print $code;

close STDOUT;
