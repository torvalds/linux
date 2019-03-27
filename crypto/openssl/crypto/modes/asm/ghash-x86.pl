#! /usr/bin/env perl
# Copyright 2010-2016 The OpenSSL Project Authors. All Rights Reserved.
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
# March, May, June 2010
#
# The module implements "4-bit" GCM GHASH function and underlying
# single multiplication operation in GF(2^128). "4-bit" means that it
# uses 256 bytes per-key table [+64/128 bytes fixed table]. It has two
# code paths: vanilla x86 and vanilla SSE. Former will be executed on
# 486 and Pentium, latter on all others. SSE GHASH features so called
# "528B" variant of "4-bit" method utilizing additional 256+16 bytes
# of per-key storage [+512 bytes shared table]. Performance results
# are for streamed GHASH subroutine and are expressed in cycles per
# processed byte, less is better:
#
#		gcc 2.95.3(*)	SSE assembler	x86 assembler
#
# Pentium	105/111(**)	-		50
# PIII		68 /75		12.2		24
# P4		125/125		17.8		84(***)
# Opteron	66 /70		10.1		30
# Core2		54 /67		8.4		18
# Atom		105/105		16.8		53
# VIA Nano	69 /71		13.0		27
#
# (*)	gcc 3.4.x was observed to generate few percent slower code,
#	which is one of reasons why 2.95.3 results were chosen,
#	another reason is lack of 3.4.x results for older CPUs;
#	comparison with SSE results is not completely fair, because C
#	results are for vanilla "256B" implementation, while
#	assembler results are for "528B";-)
# (**)	second number is result for code compiled with -fPIC flag,
#	which is actually more relevant, because assembler code is
#	position-independent;
# (***)	see comment in non-MMX routine for further details;
#
# To summarize, it's >2-5 times faster than gcc-generated code. To
# anchor it to something else SHA1 assembler processes one byte in
# ~7 cycles on contemporary x86 cores. As for choice of MMX/SSE
# in particular, see comment at the end of the file...

# May 2010
#
# Add PCLMULQDQ version performing at 2.10 cycles per processed byte.
# The question is how close is it to theoretical limit? The pclmulqdq
# instruction latency appears to be 14 cycles and there can't be more
# than 2 of them executing at any given time. This means that single
# Karatsuba multiplication would take 28 cycles *plus* few cycles for
# pre- and post-processing. Then multiplication has to be followed by
# modulo-reduction. Given that aggregated reduction method [see
# "Carry-less Multiplication and Its Usage for Computing the GCM Mode"
# white paper by Intel] allows you to perform reduction only once in
# a while we can assume that asymptotic performance can be estimated
# as (28+Tmod/Naggr)/16, where Tmod is time to perform reduction
# and Naggr is the aggregation factor.
#
# Before we proceed to this implementation let's have closer look at
# the best-performing code suggested by Intel in their white paper.
# By tracing inter-register dependencies Tmod is estimated as ~19
# cycles and Naggr chosen by Intel is 4, resulting in 2.05 cycles per
# processed byte. As implied, this is quite optimistic estimate,
# because it does not account for Karatsuba pre- and post-processing,
# which for a single multiplication is ~5 cycles. Unfortunately Intel
# does not provide performance data for GHASH alone. But benchmarking
# AES_GCM_encrypt ripped out of Fig. 15 of the white paper with aadt
# alone resulted in 2.46 cycles per byte of out 16KB buffer. Note that
# the result accounts even for pre-computing of degrees of the hash
# key H, but its portion is negligible at 16KB buffer size.
#
# Moving on to the implementation in question. Tmod is estimated as
# ~13 cycles and Naggr is 2, giving asymptotic performance of ...
# 2.16. How is it possible that measured performance is better than
# optimistic theoretical estimate? There is one thing Intel failed
# to recognize. By serializing GHASH with CTR in same subroutine
# former's performance is really limited to above (Tmul + Tmod/Naggr)
# equation. But if GHASH procedure is detached, the modulo-reduction
# can be interleaved with Naggr-1 multiplications at instruction level
# and under ideal conditions even disappear from the equation. So that
# optimistic theoretical estimate for this implementation is ...
# 28/16=1.75, and not 2.16. Well, it's probably way too optimistic,
# at least for such small Naggr. I'd argue that (28+Tproc/Naggr),
# where Tproc is time required for Karatsuba pre- and post-processing,
# is more realistic estimate. In this case it gives ... 1.91 cycles.
# Or in other words, depending on how well we can interleave reduction
# and one of the two multiplications the performance should be between
# 1.91 and 2.16. As already mentioned, this implementation processes
# one byte out of 8KB buffer in 2.10 cycles, while x86_64 counterpart
# - in 2.02. x86_64 performance is better, because larger register
# bank allows to interleave reduction and multiplication better.
#
# Does it make sense to increase Naggr? To start with it's virtually
# impossible in 32-bit mode, because of limited register bank
# capacity. Otherwise improvement has to be weighed against slower
# setup, as well as code size and complexity increase. As even
# optimistic estimate doesn't promise 30% performance improvement,
# there are currently no plans to increase Naggr.
#
# Special thanks to David Woodhouse for providing access to a
# Westmere-based system on behalf of Intel Open Source Technology Centre.

# January 2010
#
# Tweaked to optimize transitions between integer and FP operations
# on same XMM register, PCLMULQDQ subroutine was measured to process
# one byte in 2.07 cycles on Sandy Bridge, and in 2.12 - on Westmere.
# The minor regression on Westmere is outweighed by ~15% improvement
# on Sandy Bridge. Strangely enough attempt to modify 64-bit code in
# similar manner resulted in almost 20% degradation on Sandy Bridge,
# where original 64-bit code processes one byte in 1.95 cycles.

#####################################################################
# For reference, AMD Bulldozer processes one byte in 1.98 cycles in
# 32-bit mode and 1.89 in 64-bit.

# February 2013
#
# Overhaul: aggregate Karatsuba post-processing, improve ILP in
# reduction_alg9. Resulting performance is 1.96 cycles per byte on
# Westmere, 1.95 - on Sandy/Ivy Bridge, 1.76 - on Bulldozer.

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

$output=pop;
open STDOUT,">$output";

&asm_init($ARGV[0],$x86only = $ARGV[$#ARGV] eq "386");

$sse2=0;
for (@ARGV) { $sse2=1 if (/-DOPENSSL_IA32_SSE2/); }

($Zhh,$Zhl,$Zlh,$Zll) = ("ebp","edx","ecx","ebx");
$inp  = "edi";
$Htbl = "esi";

$unroll = 0;	# Affects x86 loop. Folded loop performs ~7% worse
		# than unrolled, which has to be weighted against
		# 2.5x x86-specific code size reduction.

sub x86_loop {
    my $off = shift;
    my $rem = "eax";

	&mov	($Zhh,&DWP(4,$Htbl,$Zll));
	&mov	($Zhl,&DWP(0,$Htbl,$Zll));
	&mov	($Zlh,&DWP(12,$Htbl,$Zll));
	&mov	($Zll,&DWP(8,$Htbl,$Zll));
	&xor	($rem,$rem);	# avoid partial register stalls on PIII

	# shrd practically kills P4, 2.5x deterioration, but P4 has
	# MMX code-path to execute. shrd runs tad faster [than twice
	# the shifts, move's and or's] on pre-MMX Pentium (as well as
	# PIII and Core2), *but* minimizes code size, spares register
	# and thus allows to fold the loop...
	if (!$unroll) {
	my $cnt = $inp;
	&mov	($cnt,15);
	&jmp	(&label("x86_loop"));
	&set_label("x86_loop",16);
	    for($i=1;$i<=2;$i++) {
		&mov	(&LB($rem),&LB($Zll));
		&shrd	($Zll,$Zlh,4);
		&and	(&LB($rem),0xf);
		&shrd	($Zlh,$Zhl,4);
		&shrd	($Zhl,$Zhh,4);
		&shr	($Zhh,4);
		&xor	($Zhh,&DWP($off+16,"esp",$rem,4));

		&mov	(&LB($rem),&BP($off,"esp",$cnt));
		if ($i&1) {
			&and	(&LB($rem),0xf0);
		} else {
			&shl	(&LB($rem),4);
		}

		&xor	($Zll,&DWP(8,$Htbl,$rem));
		&xor	($Zlh,&DWP(12,$Htbl,$rem));
		&xor	($Zhl,&DWP(0,$Htbl,$rem));
		&xor	($Zhh,&DWP(4,$Htbl,$rem));

		if ($i&1) {
			&dec	($cnt);
			&js	(&label("x86_break"));
		} else {
			&jmp	(&label("x86_loop"));
		}
	    }
	&set_label("x86_break",16);
	} else {
	    for($i=1;$i<32;$i++) {
		&comment($i);
		&mov	(&LB($rem),&LB($Zll));
		&shrd	($Zll,$Zlh,4);
		&and	(&LB($rem),0xf);
		&shrd	($Zlh,$Zhl,4);
		&shrd	($Zhl,$Zhh,4);
		&shr	($Zhh,4);
		&xor	($Zhh,&DWP($off+16,"esp",$rem,4));

		if ($i&1) {
			&mov	(&LB($rem),&BP($off+15-($i>>1),"esp"));
			&and	(&LB($rem),0xf0);
		} else {
			&mov	(&LB($rem),&BP($off+15-($i>>1),"esp"));
			&shl	(&LB($rem),4);
		}

		&xor	($Zll,&DWP(8,$Htbl,$rem));
		&xor	($Zlh,&DWP(12,$Htbl,$rem));
		&xor	($Zhl,&DWP(0,$Htbl,$rem));
		&xor	($Zhh,&DWP(4,$Htbl,$rem));
	    }
	}
	&bswap	($Zll);
	&bswap	($Zlh);
	&bswap	($Zhl);
	if (!$x86only) {
		&bswap	($Zhh);
	} else {
		&mov	("eax",$Zhh);
		&bswap	("eax");
		&mov	($Zhh,"eax");
	}
}

if ($unroll) {
    &function_begin_B("_x86_gmult_4bit_inner");
	&x86_loop(4);
	&ret	();
    &function_end_B("_x86_gmult_4bit_inner");
}

sub deposit_rem_4bit {
    my $bias = shift;

	&mov	(&DWP($bias+0, "esp"),0x0000<<16);
	&mov	(&DWP($bias+4, "esp"),0x1C20<<16);
	&mov	(&DWP($bias+8, "esp"),0x3840<<16);
	&mov	(&DWP($bias+12,"esp"),0x2460<<16);
	&mov	(&DWP($bias+16,"esp"),0x7080<<16);
	&mov	(&DWP($bias+20,"esp"),0x6CA0<<16);
	&mov	(&DWP($bias+24,"esp"),0x48C0<<16);
	&mov	(&DWP($bias+28,"esp"),0x54E0<<16);
	&mov	(&DWP($bias+32,"esp"),0xE100<<16);
	&mov	(&DWP($bias+36,"esp"),0xFD20<<16);
	&mov	(&DWP($bias+40,"esp"),0xD940<<16);
	&mov	(&DWP($bias+44,"esp"),0xC560<<16);
	&mov	(&DWP($bias+48,"esp"),0x9180<<16);
	&mov	(&DWP($bias+52,"esp"),0x8DA0<<16);
	&mov	(&DWP($bias+56,"esp"),0xA9C0<<16);
	&mov	(&DWP($bias+60,"esp"),0xB5E0<<16);
}

$suffix = $x86only ? "" : "_x86";

&function_begin("gcm_gmult_4bit".$suffix);
	&stack_push(16+4+1);			# +1 for stack alignment
	&mov	($inp,&wparam(0));		# load Xi
	&mov	($Htbl,&wparam(1));		# load Htable

	&mov	($Zhh,&DWP(0,$inp));		# load Xi[16]
	&mov	($Zhl,&DWP(4,$inp));
	&mov	($Zlh,&DWP(8,$inp));
	&mov	($Zll,&DWP(12,$inp));

	&deposit_rem_4bit(16);

	&mov	(&DWP(0,"esp"),$Zhh);		# copy Xi[16] on stack
	&mov	(&DWP(4,"esp"),$Zhl);
	&mov	(&DWP(8,"esp"),$Zlh);
	&mov	(&DWP(12,"esp"),$Zll);
	&shr	($Zll,20);
	&and	($Zll,0xf0);

	if ($unroll) {
		&call	("_x86_gmult_4bit_inner");
	} else {
		&x86_loop(0);
		&mov	($inp,&wparam(0));
	}

	&mov	(&DWP(12,$inp),$Zll);
	&mov	(&DWP(8,$inp),$Zlh);
	&mov	(&DWP(4,$inp),$Zhl);
	&mov	(&DWP(0,$inp),$Zhh);
	&stack_pop(16+4+1);
&function_end("gcm_gmult_4bit".$suffix);

&function_begin("gcm_ghash_4bit".$suffix);
	&stack_push(16+4+1);			# +1 for 64-bit alignment
	&mov	($Zll,&wparam(0));		# load Xi
	&mov	($Htbl,&wparam(1));		# load Htable
	&mov	($inp,&wparam(2));		# load in
	&mov	("ecx",&wparam(3));		# load len
	&add	("ecx",$inp);
	&mov	(&wparam(3),"ecx");

	&mov	($Zhh,&DWP(0,$Zll));		# load Xi[16]
	&mov	($Zhl,&DWP(4,$Zll));
	&mov	($Zlh,&DWP(8,$Zll));
	&mov	($Zll,&DWP(12,$Zll));

	&deposit_rem_4bit(16);

    &set_label("x86_outer_loop",16);
	&xor	($Zll,&DWP(12,$inp));		# xor with input
	&xor	($Zlh,&DWP(8,$inp));
	&xor	($Zhl,&DWP(4,$inp));
	&xor	($Zhh,&DWP(0,$inp));
	&mov	(&DWP(12,"esp"),$Zll);		# dump it on stack
	&mov	(&DWP(8,"esp"),$Zlh);
	&mov	(&DWP(4,"esp"),$Zhl);
	&mov	(&DWP(0,"esp"),$Zhh);

	&shr	($Zll,20);
	&and	($Zll,0xf0);

	if ($unroll) {
		&call	("_x86_gmult_4bit_inner");
	} else {
		&x86_loop(0);
		&mov	($inp,&wparam(2));
	}
	&lea	($inp,&DWP(16,$inp));
	&cmp	($inp,&wparam(3));
	&mov	(&wparam(2),$inp)	if (!$unroll);
	&jb	(&label("x86_outer_loop"));

	&mov	($inp,&wparam(0));	# load Xi
	&mov	(&DWP(12,$inp),$Zll);
	&mov	(&DWP(8,$inp),$Zlh);
	&mov	(&DWP(4,$inp),$Zhl);
	&mov	(&DWP(0,$inp),$Zhh);
	&stack_pop(16+4+1);
&function_end("gcm_ghash_4bit".$suffix);

if (!$x86only) {{{

&static_label("rem_4bit");

if (!$sse2) {{	# pure-MMX "May" version...

$S=12;		# shift factor for rem_4bit

&function_begin_B("_mmx_gmult_4bit_inner");
# MMX version performs 3.5 times better on P4 (see comment in non-MMX
# routine for further details), 100% better on Opteron, ~70% better
# on Core2 and PIII... In other words effort is considered to be well
# spent... Since initial release the loop was unrolled in order to
# "liberate" register previously used as loop counter. Instead it's
# used to optimize critical path in 'Z.hi ^= rem_4bit[Z.lo&0xf]'.
# The path involves move of Z.lo from MMX to integer register,
# effective address calculation and finally merge of value to Z.hi.
# Reference to rem_4bit is scheduled so late that I had to >>4
# rem_4bit elements. This resulted in 20-45% procent improvement
# on contemporary µ-archs.
{
    my $cnt;
    my $rem_4bit = "eax";
    my @rem = ($Zhh,$Zll);
    my $nhi = $Zhl;
    my $nlo = $Zlh;

    my ($Zlo,$Zhi) = ("mm0","mm1");
    my $tmp = "mm2";

	&xor	($nlo,$nlo);	# avoid partial register stalls on PIII
	&mov	($nhi,$Zll);
	&mov	(&LB($nlo),&LB($nhi));
	&shl	(&LB($nlo),4);
	&and	($nhi,0xf0);
	&movq	($Zlo,&QWP(8,$Htbl,$nlo));
	&movq	($Zhi,&QWP(0,$Htbl,$nlo));
	&movd	($rem[0],$Zlo);

	for ($cnt=28;$cnt>=-2;$cnt--) {
	    my $odd = $cnt&1;
	    my $nix = $odd ? $nlo : $nhi;

		&shl	(&LB($nlo),4)			if ($odd);
		&psrlq	($Zlo,4);
		&movq	($tmp,$Zhi);
		&psrlq	($Zhi,4);
		&pxor	($Zlo,&QWP(8,$Htbl,$nix));
		&mov	(&LB($nlo),&BP($cnt/2,$inp))	if (!$odd && $cnt>=0);
		&psllq	($tmp,60);
		&and	($nhi,0xf0)			if ($odd);
		&pxor	($Zhi,&QWP(0,$rem_4bit,$rem[1],8)) if ($cnt<28);
		&and	($rem[0],0xf);
		&pxor	($Zhi,&QWP(0,$Htbl,$nix));
		&mov	($nhi,$nlo)			if (!$odd && $cnt>=0);
		&movd	($rem[1],$Zlo);
		&pxor	($Zlo,$tmp);

		push	(@rem,shift(@rem));		# "rotate" registers
	}

	&mov	($inp,&DWP(4,$rem_4bit,$rem[1],8));	# last rem_4bit[rem]

	&psrlq	($Zlo,32);	# lower part of Zlo is already there
	&movd	($Zhl,$Zhi);
	&psrlq	($Zhi,32);
	&movd	($Zlh,$Zlo);
	&movd	($Zhh,$Zhi);
	&shl	($inp,4);	# compensate for rem_4bit[i] being >>4

	&bswap	($Zll);
	&bswap	($Zhl);
	&bswap	($Zlh);
	&xor	($Zhh,$inp);
	&bswap	($Zhh);

	&ret	();
}
&function_end_B("_mmx_gmult_4bit_inner");

&function_begin("gcm_gmult_4bit_mmx");
	&mov	($inp,&wparam(0));	# load Xi
	&mov	($Htbl,&wparam(1));	# load Htable

	&call	(&label("pic_point"));
	&set_label("pic_point");
	&blindpop("eax");
	&lea	("eax",&DWP(&label("rem_4bit")."-".&label("pic_point"),"eax"));

	&movz	($Zll,&BP(15,$inp));

	&call	("_mmx_gmult_4bit_inner");

	&mov	($inp,&wparam(0));	# load Xi
	&emms	();
	&mov	(&DWP(12,$inp),$Zll);
	&mov	(&DWP(4,$inp),$Zhl);
	&mov	(&DWP(8,$inp),$Zlh);
	&mov	(&DWP(0,$inp),$Zhh);
&function_end("gcm_gmult_4bit_mmx");

# Streamed version performs 20% better on P4, 7% on Opteron,
# 10% on Core2 and PIII...
&function_begin("gcm_ghash_4bit_mmx");
	&mov	($Zhh,&wparam(0));	# load Xi
	&mov	($Htbl,&wparam(1));	# load Htable
	&mov	($inp,&wparam(2));	# load in
	&mov	($Zlh,&wparam(3));	# load len

	&call	(&label("pic_point"));
	&set_label("pic_point");
	&blindpop("eax");
	&lea	("eax",&DWP(&label("rem_4bit")."-".&label("pic_point"),"eax"));

	&add	($Zlh,$inp);
	&mov	(&wparam(3),$Zlh);	# len to point at the end of input
	&stack_push(4+1);		# +1 for stack alignment

	&mov	($Zll,&DWP(12,$Zhh));	# load Xi[16]
	&mov	($Zhl,&DWP(4,$Zhh));
	&mov	($Zlh,&DWP(8,$Zhh));
	&mov	($Zhh,&DWP(0,$Zhh));
	&jmp	(&label("mmx_outer_loop"));

    &set_label("mmx_outer_loop",16);
	&xor	($Zll,&DWP(12,$inp));
	&xor	($Zhl,&DWP(4,$inp));
	&xor	($Zlh,&DWP(8,$inp));
	&xor	($Zhh,&DWP(0,$inp));
	&mov	(&wparam(2),$inp);
	&mov	(&DWP(12,"esp"),$Zll);
	&mov	(&DWP(4,"esp"),$Zhl);
	&mov	(&DWP(8,"esp"),$Zlh);
	&mov	(&DWP(0,"esp"),$Zhh);

	&mov	($inp,"esp");
	&shr	($Zll,24);

	&call	("_mmx_gmult_4bit_inner");

	&mov	($inp,&wparam(2));
	&lea	($inp,&DWP(16,$inp));
	&cmp	($inp,&wparam(3));
	&jb	(&label("mmx_outer_loop"));

	&mov	($inp,&wparam(0));	# load Xi
	&emms	();
	&mov	(&DWP(12,$inp),$Zll);
	&mov	(&DWP(4,$inp),$Zhl);
	&mov	(&DWP(8,$inp),$Zlh);
	&mov	(&DWP(0,$inp),$Zhh);

	&stack_pop(4+1);
&function_end("gcm_ghash_4bit_mmx");

}} else {{	# "June" MMX version...
		# ... has slower "April" gcm_gmult_4bit_mmx with folded
		# loop. This is done to conserve code size...
$S=16;		# shift factor for rem_4bit

sub mmx_loop() {
# MMX version performs 2.8 times better on P4 (see comment in non-MMX
# routine for further details), 40% better on Opteron and Core2, 50%
# better on PIII... In other words effort is considered to be well
# spent...
    my $inp = shift;
    my $rem_4bit = shift;
    my $cnt = $Zhh;
    my $nhi = $Zhl;
    my $nlo = $Zlh;
    my $rem = $Zll;

    my ($Zlo,$Zhi) = ("mm0","mm1");
    my $tmp = "mm2";

	&xor	($nlo,$nlo);	# avoid partial register stalls on PIII
	&mov	($nhi,$Zll);
	&mov	(&LB($nlo),&LB($nhi));
	&mov	($cnt,14);
	&shl	(&LB($nlo),4);
	&and	($nhi,0xf0);
	&movq	($Zlo,&QWP(8,$Htbl,$nlo));
	&movq	($Zhi,&QWP(0,$Htbl,$nlo));
	&movd	($rem,$Zlo);
	&jmp	(&label("mmx_loop"));

    &set_label("mmx_loop",16);
	&psrlq	($Zlo,4);
	&and	($rem,0xf);
	&movq	($tmp,$Zhi);
	&psrlq	($Zhi,4);
	&pxor	($Zlo,&QWP(8,$Htbl,$nhi));
	&mov	(&LB($nlo),&BP(0,$inp,$cnt));
	&psllq	($tmp,60);
	&pxor	($Zhi,&QWP(0,$rem_4bit,$rem,8));
	&dec	($cnt);
	&movd	($rem,$Zlo);
	&pxor	($Zhi,&QWP(0,$Htbl,$nhi));
	&mov	($nhi,$nlo);
	&pxor	($Zlo,$tmp);
	&js	(&label("mmx_break"));

	&shl	(&LB($nlo),4);
	&and	($rem,0xf);
	&psrlq	($Zlo,4);
	&and	($nhi,0xf0);
	&movq	($tmp,$Zhi);
	&psrlq	($Zhi,4);
	&pxor	($Zlo,&QWP(8,$Htbl,$nlo));
	&psllq	($tmp,60);
	&pxor	($Zhi,&QWP(0,$rem_4bit,$rem,8));
	&movd	($rem,$Zlo);
	&pxor	($Zhi,&QWP(0,$Htbl,$nlo));
	&pxor	($Zlo,$tmp);
	&jmp	(&label("mmx_loop"));

    &set_label("mmx_break",16);
	&shl	(&LB($nlo),4);
	&and	($rem,0xf);
	&psrlq	($Zlo,4);
	&and	($nhi,0xf0);
	&movq	($tmp,$Zhi);
	&psrlq	($Zhi,4);
	&pxor	($Zlo,&QWP(8,$Htbl,$nlo));
	&psllq	($tmp,60);
	&pxor	($Zhi,&QWP(0,$rem_4bit,$rem,8));
	&movd	($rem,$Zlo);
	&pxor	($Zhi,&QWP(0,$Htbl,$nlo));
	&pxor	($Zlo,$tmp);

	&psrlq	($Zlo,4);
	&and	($rem,0xf);
	&movq	($tmp,$Zhi);
	&psrlq	($Zhi,4);
	&pxor	($Zlo,&QWP(8,$Htbl,$nhi));
	&psllq	($tmp,60);
	&pxor	($Zhi,&QWP(0,$rem_4bit,$rem,8));
	&movd	($rem,$Zlo);
	&pxor	($Zhi,&QWP(0,$Htbl,$nhi));
	&pxor	($Zlo,$tmp);

	&psrlq	($Zlo,32);	# lower part of Zlo is already there
	&movd	($Zhl,$Zhi);
	&psrlq	($Zhi,32);
	&movd	($Zlh,$Zlo);
	&movd	($Zhh,$Zhi);

	&bswap	($Zll);
	&bswap	($Zhl);
	&bswap	($Zlh);
	&bswap	($Zhh);
}

&function_begin("gcm_gmult_4bit_mmx");
	&mov	($inp,&wparam(0));	# load Xi
	&mov	($Htbl,&wparam(1));	# load Htable

	&call	(&label("pic_point"));
	&set_label("pic_point");
	&blindpop("eax");
	&lea	("eax",&DWP(&label("rem_4bit")."-".&label("pic_point"),"eax"));

	&movz	($Zll,&BP(15,$inp));

	&mmx_loop($inp,"eax");

	&emms	();
	&mov	(&DWP(12,$inp),$Zll);
	&mov	(&DWP(4,$inp),$Zhl);
	&mov	(&DWP(8,$inp),$Zlh);
	&mov	(&DWP(0,$inp),$Zhh);
&function_end("gcm_gmult_4bit_mmx");

######################################################################
# Below subroutine is "528B" variant of "4-bit" GCM GHASH function
# (see gcm128.c for details). It provides further 20-40% performance
# improvement over above mentioned "May" version.

&static_label("rem_8bit");

&function_begin("gcm_ghash_4bit_mmx");
{ my ($Zlo,$Zhi) = ("mm7","mm6");
  my $rem_8bit = "esi";
  my $Htbl = "ebx";

    # parameter block
    &mov	("eax",&wparam(0));		# Xi
    &mov	("ebx",&wparam(1));		# Htable
    &mov	("ecx",&wparam(2));		# inp
    &mov	("edx",&wparam(3));		# len
    &mov	("ebp","esp");			# original %esp
    &call	(&label("pic_point"));
    &set_label	("pic_point");
    &blindpop	($rem_8bit);
    &lea	($rem_8bit,&DWP(&label("rem_8bit")."-".&label("pic_point"),$rem_8bit));

    &sub	("esp",512+16+16);		# allocate stack frame...
    &and	("esp",-64);			# ...and align it
    &sub	("esp",16);			# place for (u8)(H[]<<4)

    &add	("edx","ecx");			# pointer to the end of input
    &mov	(&DWP(528+16+0,"esp"),"eax");	# save Xi
    &mov	(&DWP(528+16+8,"esp"),"edx");	# save inp+len
    &mov	(&DWP(528+16+12,"esp"),"ebp");	# save original %esp

    { my @lo  = ("mm0","mm1","mm2");
      my @hi  = ("mm3","mm4","mm5");
      my @tmp = ("mm6","mm7");
      my ($off1,$off2,$i) = (0,0,);

      &add	($Htbl,128);			# optimize for size
      &lea	("edi",&DWP(16+128,"esp"));
      &lea	("ebp",&DWP(16+256+128,"esp"));

      # decompose Htable (low and high parts are kept separately),
      # generate Htable[]>>4, (u8)(Htable[]<<4), save to stack...
      for ($i=0;$i<18;$i++) {

	&mov	("edx",&DWP(16*$i+8-128,$Htbl))		if ($i<16);
	&movq	($lo[0],&QWP(16*$i+8-128,$Htbl))	if ($i<16);
	&psllq	($tmp[1],60)				if ($i>1);
	&movq	($hi[0],&QWP(16*$i+0-128,$Htbl))	if ($i<16);
	&por	($lo[2],$tmp[1])			if ($i>1);
	&movq	(&QWP($off1-128,"edi"),$lo[1])		if ($i>0 && $i<17);
	&psrlq	($lo[1],4)				if ($i>0 && $i<17);
	&movq	(&QWP($off1,"edi"),$hi[1])		if ($i>0 && $i<17);
	&movq	($tmp[0],$hi[1])			if ($i>0 && $i<17);
	&movq	(&QWP($off2-128,"ebp"),$lo[2])		if ($i>1);
	&psrlq	($hi[1],4)				if ($i>0 && $i<17);
	&movq	(&QWP($off2,"ebp"),$hi[2])		if ($i>1);
	&shl	("edx",4)				if ($i<16);
	&mov	(&BP($i,"esp"),&LB("edx"))		if ($i<16);

	unshift	(@lo,pop(@lo));			# "rotate" registers
	unshift	(@hi,pop(@hi));
	unshift	(@tmp,pop(@tmp));
	$off1 += 8	if ($i>0);
	$off2 += 8	if ($i>1);
      }
    }

    &movq	($Zhi,&QWP(0,"eax"));
    &mov	("ebx",&DWP(8,"eax"));
    &mov	("edx",&DWP(12,"eax"));		# load Xi

&set_label("outer",16);
  { my $nlo = "eax";
    my $dat = "edx";
    my @nhi = ("edi","ebp");
    my @rem = ("ebx","ecx");
    my @red = ("mm0","mm1","mm2");
    my $tmp = "mm3";

    &xor	($dat,&DWP(12,"ecx"));		# merge input data
    &xor	("ebx",&DWP(8,"ecx"));
    &pxor	($Zhi,&QWP(0,"ecx"));
    &lea	("ecx",&DWP(16,"ecx"));		# inp+=16
    #&mov	(&DWP(528+12,"esp"),$dat);	# save inp^Xi
    &mov	(&DWP(528+8,"esp"),"ebx");
    &movq	(&QWP(528+0,"esp"),$Zhi);
    &mov	(&DWP(528+16+4,"esp"),"ecx");	# save inp

    &xor	($nlo,$nlo);
    &rol	($dat,8);
    &mov	(&LB($nlo),&LB($dat));
    &mov	($nhi[1],$nlo);
    &and	(&LB($nlo),0x0f);
    &shr	($nhi[1],4);
    &pxor	($red[0],$red[0]);
    &rol	($dat,8);			# next byte
    &pxor	($red[1],$red[1]);
    &pxor	($red[2],$red[2]);

    # Just like in "May" version modulo-schedule for critical path in
    # 'Z.hi ^= rem_8bit[Z.lo&0xff^((u8)H[nhi]<<4)]<<48'. Final 'pxor'
    # is scheduled so late that rem_8bit[] has to be shifted *right*
    # by 16, which is why last argument to pinsrw is 2, which
    # corresponds to <<32=<<48>>16...
    for ($j=11,$i=0;$i<15;$i++) {

      if ($i>0) {
	&pxor	($Zlo,&QWP(16,"esp",$nlo,8));		# Z^=H[nlo]
	&rol	($dat,8);				# next byte
	&pxor	($Zhi,&QWP(16+128,"esp",$nlo,8));

	&pxor	($Zlo,$tmp);
	&pxor	($Zhi,&QWP(16+256+128,"esp",$nhi[0],8));
	&xor	(&LB($rem[1]),&BP(0,"esp",$nhi[0]));	# rem^(H[nhi]<<4)
      } else {
	&movq	($Zlo,&QWP(16,"esp",$nlo,8));
	&movq	($Zhi,&QWP(16+128,"esp",$nlo,8));
      }

	&mov	(&LB($nlo),&LB($dat));
	&mov	($dat,&DWP(528+$j,"esp"))		if (--$j%4==0);

	&movd	($rem[0],$Zlo);
	&movz	($rem[1],&LB($rem[1]))			if ($i>0);
	&psrlq	($Zlo,8);				# Z>>=8

	&movq	($tmp,$Zhi);
	&mov	($nhi[0],$nlo);
	&psrlq	($Zhi,8);

	&pxor	($Zlo,&QWP(16+256+0,"esp",$nhi[1],8));	# Z^=H[nhi]>>4
	&and	(&LB($nlo),0x0f);
	&psllq	($tmp,56);

	&pxor	($Zhi,$red[1])				if ($i>1);
	&shr	($nhi[0],4);
	&pinsrw	($red[0],&WP(0,$rem_8bit,$rem[1],2),2)	if ($i>0);

	unshift	(@red,pop(@red));			# "rotate" registers
	unshift	(@rem,pop(@rem));
	unshift	(@nhi,pop(@nhi));
    }

    &pxor	($Zlo,&QWP(16,"esp",$nlo,8));		# Z^=H[nlo]
    &pxor	($Zhi,&QWP(16+128,"esp",$nlo,8));
    &xor	(&LB($rem[1]),&BP(0,"esp",$nhi[0]));	# rem^(H[nhi]<<4)

    &pxor	($Zlo,$tmp);
    &pxor	($Zhi,&QWP(16+256+128,"esp",$nhi[0],8));
    &movz	($rem[1],&LB($rem[1]));

    &pxor	($red[2],$red[2]);			# clear 2nd word
    &psllq	($red[1],4);

    &movd	($rem[0],$Zlo);
    &psrlq	($Zlo,4);				# Z>>=4

    &movq	($tmp,$Zhi);
    &psrlq	($Zhi,4);
    &shl	($rem[0],4);				# rem<<4

    &pxor	($Zlo,&QWP(16,"esp",$nhi[1],8));	# Z^=H[nhi]
    &psllq	($tmp,60);
    &movz	($rem[0],&LB($rem[0]));

    &pxor	($Zlo,$tmp);
    &pxor	($Zhi,&QWP(16+128,"esp",$nhi[1],8));

    &pinsrw	($red[0],&WP(0,$rem_8bit,$rem[1],2),2);
    &pxor	($Zhi,$red[1]);

    &movd	($dat,$Zlo);
    &pinsrw	($red[2],&WP(0,$rem_8bit,$rem[0],2),3);	# last is <<48

    &psllq	($red[0],12);				# correct by <<16>>4
    &pxor	($Zhi,$red[0]);
    &psrlq	($Zlo,32);
    &pxor	($Zhi,$red[2]);

    &mov	("ecx",&DWP(528+16+4,"esp"));	# restore inp
    &movd	("ebx",$Zlo);
    &movq	($tmp,$Zhi);			# 01234567
    &psllw	($Zhi,8);			# 1.3.5.7.
    &psrlw	($tmp,8);			# .0.2.4.6
    &por	($Zhi,$tmp);			# 10325476
    &bswap	($dat);
    &pshufw	($Zhi,$Zhi,0b00011011);		# 76543210
    &bswap	("ebx");

    &cmp	("ecx",&DWP(528+16+8,"esp"));	# are we done?
    &jne	(&label("outer"));
  }

    &mov	("eax",&DWP(528+16+0,"esp"));	# restore Xi
    &mov	(&DWP(12,"eax"),"edx");
    &mov	(&DWP(8,"eax"),"ebx");
    &movq	(&QWP(0,"eax"),$Zhi);

    &mov	("esp",&DWP(528+16+12,"esp"));	# restore original %esp
    &emms	();
}
&function_end("gcm_ghash_4bit_mmx");
}}

if ($sse2) {{
######################################################################
# PCLMULQDQ version.

$Xip="eax";
$Htbl="edx";
$const="ecx";
$inp="esi";
$len="ebx";

($Xi,$Xhi)=("xmm0","xmm1");	$Hkey="xmm2";
($T1,$T2,$T3)=("xmm3","xmm4","xmm5");
($Xn,$Xhn)=("xmm6","xmm7");

&static_label("bswap");

sub clmul64x64_T2 {	# minimal "register" pressure
my ($Xhi,$Xi,$Hkey,$HK)=@_;

	&movdqa		($Xhi,$Xi);		#
	&pshufd		($T1,$Xi,0b01001110);
	&pshufd		($T2,$Hkey,0b01001110)	if (!defined($HK));
	&pxor		($T1,$Xi);		#
	&pxor		($T2,$Hkey)		if (!defined($HK));
			$HK=$T2			if (!defined($HK));

	&pclmulqdq	($Xi,$Hkey,0x00);	#######
	&pclmulqdq	($Xhi,$Hkey,0x11);	#######
	&pclmulqdq	($T1,$HK,0x00);		#######
	&xorps		($T1,$Xi);		#
	&xorps		($T1,$Xhi);		#

	&movdqa		($T2,$T1);		#
	&psrldq		($T1,8);
	&pslldq		($T2,8);		#
	&pxor		($Xhi,$T1);
	&pxor		($Xi,$T2);		#
}

sub clmul64x64_T3 {
# Even though this subroutine offers visually better ILP, it
# was empirically found to be a tad slower than above version.
# At least in gcm_ghash_clmul context. But it's just as well,
# because loop modulo-scheduling is possible only thanks to
# minimized "register" pressure...
my ($Xhi,$Xi,$Hkey)=@_;

	&movdqa		($T1,$Xi);		#
	&movdqa		($Xhi,$Xi);
	&pclmulqdq	($Xi,$Hkey,0x00);	#######
	&pclmulqdq	($Xhi,$Hkey,0x11);	#######
	&pshufd		($T2,$T1,0b01001110);	#
	&pshufd		($T3,$Hkey,0b01001110);
	&pxor		($T2,$T1);		#
	&pxor		($T3,$Hkey);
	&pclmulqdq	($T2,$T3,0x00);		#######
	&pxor		($T2,$Xi);		#
	&pxor		($T2,$Xhi);		#

	&movdqa		($T3,$T2);		#
	&psrldq		($T2,8);
	&pslldq		($T3,8);		#
	&pxor		($Xhi,$T2);
	&pxor		($Xi,$T3);		#
}

if (1) {		# Algorithm 9 with <<1 twist.
			# Reduction is shorter and uses only two
			# temporary registers, which makes it better
			# candidate for interleaving with 64x64
			# multiplication. Pre-modulo-scheduled loop
			# was found to be ~20% faster than Algorithm 5
			# below. Algorithm 9 was therefore chosen for
			# further optimization...

sub reduction_alg9 {	# 17/11 times faster than Intel version
my ($Xhi,$Xi) = @_;

	# 1st phase
	&movdqa		($T2,$Xi);		#
	&movdqa		($T1,$Xi);
	&psllq		($Xi,5);
	&pxor		($T1,$Xi);		#
	&psllq		($Xi,1);
	&pxor		($Xi,$T1);		#
	&psllq		($Xi,57);		#
	&movdqa		($T1,$Xi);		#
	&pslldq		($Xi,8);
	&psrldq		($T1,8);		#
	&pxor		($Xi,$T2);
	&pxor		($Xhi,$T1);		#

	# 2nd phase
	&movdqa		($T2,$Xi);
	&psrlq		($Xi,1);
	&pxor		($Xhi,$T2);		#
	&pxor		($T2,$Xi);
	&psrlq		($Xi,5);
	&pxor		($Xi,$T2);		#
	&psrlq		($Xi,1);		#
	&pxor		($Xi,$Xhi)		#
}

&function_begin_B("gcm_init_clmul");
	&mov		($Htbl,&wparam(0));
	&mov		($Xip,&wparam(1));

	&call		(&label("pic"));
&set_label("pic");
	&blindpop	($const);
	&lea		($const,&DWP(&label("bswap")."-".&label("pic"),$const));

	&movdqu		($Hkey,&QWP(0,$Xip));
	&pshufd		($Hkey,$Hkey,0b01001110);# dword swap

	# <<1 twist
	&pshufd		($T2,$Hkey,0b11111111);	# broadcast uppermost dword
	&movdqa		($T1,$Hkey);
	&psllq		($Hkey,1);
	&pxor		($T3,$T3);		#
	&psrlq		($T1,63);
	&pcmpgtd	($T3,$T2);		# broadcast carry bit
	&pslldq		($T1,8);
	&por		($Hkey,$T1);		# H<<=1

	# magic reduction
	&pand		($T3,&QWP(16,$const));	# 0x1c2_polynomial
	&pxor		($Hkey,$T3);		# if(carry) H^=0x1c2_polynomial

	# calculate H^2
	&movdqa		($Xi,$Hkey);
	&clmul64x64_T2	($Xhi,$Xi,$Hkey);
	&reduction_alg9	($Xhi,$Xi);

	&pshufd		($T1,$Hkey,0b01001110);
	&pshufd		($T2,$Xi,0b01001110);
	&pxor		($T1,$Hkey);		# Karatsuba pre-processing
	&movdqu		(&QWP(0,$Htbl),$Hkey);	# save H
	&pxor		($T2,$Xi);		# Karatsuba pre-processing
	&movdqu		(&QWP(16,$Htbl),$Xi);	# save H^2
	&palignr	($T2,$T1,8);		# low part is H.lo^H.hi
	&movdqu		(&QWP(32,$Htbl),$T2);	# save Karatsuba "salt"

	&ret		();
&function_end_B("gcm_init_clmul");

&function_begin_B("gcm_gmult_clmul");
	&mov		($Xip,&wparam(0));
	&mov		($Htbl,&wparam(1));

	&call		(&label("pic"));
&set_label("pic");
	&blindpop	($const);
	&lea		($const,&DWP(&label("bswap")."-".&label("pic"),$const));

	&movdqu		($Xi,&QWP(0,$Xip));
	&movdqa		($T3,&QWP(0,$const));
	&movups		($Hkey,&QWP(0,$Htbl));
	&pshufb		($Xi,$T3);
	&movups		($T2,&QWP(32,$Htbl));

	&clmul64x64_T2	($Xhi,$Xi,$Hkey,$T2);
	&reduction_alg9	($Xhi,$Xi);

	&pshufb		($Xi,$T3);
	&movdqu		(&QWP(0,$Xip),$Xi);

	&ret	();
&function_end_B("gcm_gmult_clmul");

&function_begin("gcm_ghash_clmul");
	&mov		($Xip,&wparam(0));
	&mov		($Htbl,&wparam(1));
	&mov		($inp,&wparam(2));
	&mov		($len,&wparam(3));

	&call		(&label("pic"));
&set_label("pic");
	&blindpop	($const);
	&lea		($const,&DWP(&label("bswap")."-".&label("pic"),$const));

	&movdqu		($Xi,&QWP(0,$Xip));
	&movdqa		($T3,&QWP(0,$const));
	&movdqu		($Hkey,&QWP(0,$Htbl));
	&pshufb		($Xi,$T3);

	&sub		($len,0x10);
	&jz		(&label("odd_tail"));

	#######
	# Xi+2 =[H*(Ii+1 + Xi+1)] mod P =
	#	[(H*Ii+1) + (H*Xi+1)] mod P =
	#	[(H*Ii+1) + H^2*(Ii+Xi)] mod P
	#
	&movdqu		($T1,&QWP(0,$inp));	# Ii
	&movdqu		($Xn,&QWP(16,$inp));	# Ii+1
	&pshufb		($T1,$T3);
	&pshufb		($Xn,$T3);
	&movdqu		($T3,&QWP(32,$Htbl));
	&pxor		($Xi,$T1);		# Ii+Xi

	&pshufd		($T1,$Xn,0b01001110);	# H*Ii+1
	&movdqa		($Xhn,$Xn);
	&pxor		($T1,$Xn);		#
	&lea		($inp,&DWP(32,$inp));	# i+=2

	&pclmulqdq	($Xn,$Hkey,0x00);	#######
	&pclmulqdq	($Xhn,$Hkey,0x11);	#######
	&pclmulqdq	($T1,$T3,0x00);		#######
	&movups		($Hkey,&QWP(16,$Htbl));	# load H^2
	&nop		();

	&sub		($len,0x20);
	&jbe		(&label("even_tail"));
	&jmp		(&label("mod_loop"));

&set_label("mod_loop",32);
	&pshufd		($T2,$Xi,0b01001110);	# H^2*(Ii+Xi)
	&movdqa		($Xhi,$Xi);
	&pxor		($T2,$Xi);		#
	&nop		();

	&pclmulqdq	($Xi,$Hkey,0x00);	#######
	&pclmulqdq	($Xhi,$Hkey,0x11);	#######
	&pclmulqdq	($T2,$T3,0x10);		#######
	&movups		($Hkey,&QWP(0,$Htbl));	# load H

	&xorps		($Xi,$Xn);		# (H*Ii+1) + H^2*(Ii+Xi)
	&movdqa		($T3,&QWP(0,$const));
	&xorps		($Xhi,$Xhn);
	 &movdqu	($Xhn,&QWP(0,$inp));	# Ii
	&pxor		($T1,$Xi);		# aggregated Karatsuba post-processing
	 &movdqu	($Xn,&QWP(16,$inp));	# Ii+1
	&pxor		($T1,$Xhi);		#

	 &pshufb	($Xhn,$T3);
	&pxor		($T2,$T1);		#

	&movdqa		($T1,$T2);		#
	&psrldq		($T2,8);
	&pslldq		($T1,8);		#
	&pxor		($Xhi,$T2);
	&pxor		($Xi,$T1);		#
	 &pshufb	($Xn,$T3);
	 &pxor		($Xhi,$Xhn);		# "Ii+Xi", consume early

	&movdqa		($Xhn,$Xn);		#&clmul64x64_TX	($Xhn,$Xn,$Hkey); H*Ii+1
	  &movdqa	($T2,$Xi);		#&reduction_alg9($Xhi,$Xi); 1st phase
	  &movdqa	($T1,$Xi);
	  &psllq	($Xi,5);
	  &pxor		($T1,$Xi);		#
	  &psllq	($Xi,1);
	  &pxor		($Xi,$T1);		#
	&pclmulqdq	($Xn,$Hkey,0x00);	#######
	&movups		($T3,&QWP(32,$Htbl));
	  &psllq	($Xi,57);		#
	  &movdqa	($T1,$Xi);		#
	  &pslldq	($Xi,8);
	  &psrldq	($T1,8);		#
	  &pxor		($Xi,$T2);
	  &pxor		($Xhi,$T1);		#
	&pshufd		($T1,$Xhn,0b01001110);
	  &movdqa	($T2,$Xi);		# 2nd phase
	  &psrlq	($Xi,1);
	&pxor		($T1,$Xhn);
	  &pxor		($Xhi,$T2);		#
	&pclmulqdq	($Xhn,$Hkey,0x11);	#######
	&movups		($Hkey,&QWP(16,$Htbl));	# load H^2
	  &pxor		($T2,$Xi);
	  &psrlq	($Xi,5);
	  &pxor		($Xi,$T2);		#
	  &psrlq	($Xi,1);		#
	  &pxor		($Xi,$Xhi)		#
	&pclmulqdq	($T1,$T3,0x00);		#######

	&lea		($inp,&DWP(32,$inp));
	&sub		($len,0x20);
	&ja		(&label("mod_loop"));

&set_label("even_tail");
	&pshufd		($T2,$Xi,0b01001110);	# H^2*(Ii+Xi)
	&movdqa		($Xhi,$Xi);
	&pxor		($T2,$Xi);		#

	&pclmulqdq	($Xi,$Hkey,0x00);	#######
	&pclmulqdq	($Xhi,$Hkey,0x11);	#######
	&pclmulqdq	($T2,$T3,0x10);		#######
	&movdqa		($T3,&QWP(0,$const));

	&xorps		($Xi,$Xn);		# (H*Ii+1) + H^2*(Ii+Xi)
	&xorps		($Xhi,$Xhn);
	&pxor		($T1,$Xi);		# aggregated Karatsuba post-processing
	&pxor		($T1,$Xhi);		#

	&pxor		($T2,$T1);		#

	&movdqa		($T1,$T2);		#
	&psrldq		($T2,8);
	&pslldq		($T1,8);		#
	&pxor		($Xhi,$T2);
	&pxor		($Xi,$T1);		#

	&reduction_alg9	($Xhi,$Xi);

	&test		($len,$len);
	&jnz		(&label("done"));

	&movups		($Hkey,&QWP(0,$Htbl));	# load H
&set_label("odd_tail");
	&movdqu		($T1,&QWP(0,$inp));	# Ii
	&pshufb		($T1,$T3);
	&pxor		($Xi,$T1);		# Ii+Xi

	&clmul64x64_T2	($Xhi,$Xi,$Hkey);	# H*(Ii+Xi)
	&reduction_alg9	($Xhi,$Xi);

&set_label("done");
	&pshufb		($Xi,$T3);
	&movdqu		(&QWP(0,$Xip),$Xi);
&function_end("gcm_ghash_clmul");

} else {		# Algorithm 5. Kept for reference purposes.

sub reduction_alg5 {	# 19/16 times faster than Intel version
my ($Xhi,$Xi)=@_;

	# <<1
	&movdqa		($T1,$Xi);		#
	&movdqa		($T2,$Xhi);
	&pslld		($Xi,1);
	&pslld		($Xhi,1);		#
	&psrld		($T1,31);
	&psrld		($T2,31);		#
	&movdqa		($T3,$T1);
	&pslldq		($T1,4);
	&psrldq		($T3,12);		#
	&pslldq		($T2,4);
	&por		($Xhi,$T3);		#
	&por		($Xi,$T1);
	&por		($Xhi,$T2);		#

	# 1st phase
	&movdqa		($T1,$Xi);
	&movdqa		($T2,$Xi);
	&movdqa		($T3,$Xi);		#
	&pslld		($T1,31);
	&pslld		($T2,30);
	&pslld		($Xi,25);		#
	&pxor		($T1,$T2);
	&pxor		($T1,$Xi);		#
	&movdqa		($T2,$T1);		#
	&pslldq		($T1,12);
	&psrldq		($T2,4);		#
	&pxor		($T3,$T1);

	# 2nd phase
	&pxor		($Xhi,$T3);		#
	&movdqa		($Xi,$T3);
	&movdqa		($T1,$T3);
	&psrld		($Xi,1);		#
	&psrld		($T1,2);
	&psrld		($T3,7);		#
	&pxor		($Xi,$T1);
	&pxor		($Xhi,$T2);
	&pxor		($Xi,$T3);		#
	&pxor		($Xi,$Xhi);		#
}

&function_begin_B("gcm_init_clmul");
	&mov		($Htbl,&wparam(0));
	&mov		($Xip,&wparam(1));

	&call		(&label("pic"));
&set_label("pic");
	&blindpop	($const);
	&lea		($const,&DWP(&label("bswap")."-".&label("pic"),$const));

	&movdqu		($Hkey,&QWP(0,$Xip));
	&pshufd		($Hkey,$Hkey,0b01001110);# dword swap

	# calculate H^2
	&movdqa		($Xi,$Hkey);
	&clmul64x64_T3	($Xhi,$Xi,$Hkey);
	&reduction_alg5	($Xhi,$Xi);

	&movdqu		(&QWP(0,$Htbl),$Hkey);	# save H
	&movdqu		(&QWP(16,$Htbl),$Xi);	# save H^2

	&ret		();
&function_end_B("gcm_init_clmul");

&function_begin_B("gcm_gmult_clmul");
	&mov		($Xip,&wparam(0));
	&mov		($Htbl,&wparam(1));

	&call		(&label("pic"));
&set_label("pic");
	&blindpop	($const);
	&lea		($const,&DWP(&label("bswap")."-".&label("pic"),$const));

	&movdqu		($Xi,&QWP(0,$Xip));
	&movdqa		($Xn,&QWP(0,$const));
	&movdqu		($Hkey,&QWP(0,$Htbl));
	&pshufb		($Xi,$Xn);

	&clmul64x64_T3	($Xhi,$Xi,$Hkey);
	&reduction_alg5	($Xhi,$Xi);

	&pshufb		($Xi,$Xn);
	&movdqu		(&QWP(0,$Xip),$Xi);

	&ret	();
&function_end_B("gcm_gmult_clmul");

&function_begin("gcm_ghash_clmul");
	&mov		($Xip,&wparam(0));
	&mov		($Htbl,&wparam(1));
	&mov		($inp,&wparam(2));
	&mov		($len,&wparam(3));

	&call		(&label("pic"));
&set_label("pic");
	&blindpop	($const);
	&lea		($const,&DWP(&label("bswap")."-".&label("pic"),$const));

	&movdqu		($Xi,&QWP(0,$Xip));
	&movdqa		($T3,&QWP(0,$const));
	&movdqu		($Hkey,&QWP(0,$Htbl));
	&pshufb		($Xi,$T3);

	&sub		($len,0x10);
	&jz		(&label("odd_tail"));

	#######
	# Xi+2 =[H*(Ii+1 + Xi+1)] mod P =
	#	[(H*Ii+1) + (H*Xi+1)] mod P =
	#	[(H*Ii+1) + H^2*(Ii+Xi)] mod P
	#
	&movdqu		($T1,&QWP(0,$inp));	# Ii
	&movdqu		($Xn,&QWP(16,$inp));	# Ii+1
	&pshufb		($T1,$T3);
	&pshufb		($Xn,$T3);
	&pxor		($Xi,$T1);		# Ii+Xi

	&clmul64x64_T3	($Xhn,$Xn,$Hkey);	# H*Ii+1
	&movdqu		($Hkey,&QWP(16,$Htbl));	# load H^2

	&sub		($len,0x20);
	&lea		($inp,&DWP(32,$inp));	# i+=2
	&jbe		(&label("even_tail"));

&set_label("mod_loop");
	&clmul64x64_T3	($Xhi,$Xi,$Hkey);	# H^2*(Ii+Xi)
	&movdqu		($Hkey,&QWP(0,$Htbl));	# load H

	&pxor		($Xi,$Xn);		# (H*Ii+1) + H^2*(Ii+Xi)
	&pxor		($Xhi,$Xhn);

	&reduction_alg5	($Xhi,$Xi);

	#######
	&movdqa		($T3,&QWP(0,$const));
	&movdqu		($T1,&QWP(0,$inp));	# Ii
	&movdqu		($Xn,&QWP(16,$inp));	# Ii+1
	&pshufb		($T1,$T3);
	&pshufb		($Xn,$T3);
	&pxor		($Xi,$T1);		# Ii+Xi

	&clmul64x64_T3	($Xhn,$Xn,$Hkey);	# H*Ii+1
	&movdqu		($Hkey,&QWP(16,$Htbl));	# load H^2

	&sub		($len,0x20);
	&lea		($inp,&DWP(32,$inp));
	&ja		(&label("mod_loop"));

&set_label("even_tail");
	&clmul64x64_T3	($Xhi,$Xi,$Hkey);	# H^2*(Ii+Xi)

	&pxor		($Xi,$Xn);		# (H*Ii+1) + H^2*(Ii+Xi)
	&pxor		($Xhi,$Xhn);

	&reduction_alg5	($Xhi,$Xi);

	&movdqa		($T3,&QWP(0,$const));
	&test		($len,$len);
	&jnz		(&label("done"));

	&movdqu		($Hkey,&QWP(0,$Htbl));	# load H
&set_label("odd_tail");
	&movdqu		($T1,&QWP(0,$inp));	# Ii
	&pshufb		($T1,$T3);
	&pxor		($Xi,$T1);		# Ii+Xi

	&clmul64x64_T3	($Xhi,$Xi,$Hkey);	# H*(Ii+Xi)
	&reduction_alg5	($Xhi,$Xi);

	&movdqa		($T3,&QWP(0,$const));
&set_label("done");
	&pshufb		($Xi,$T3);
	&movdqu		(&QWP(0,$Xip),$Xi);
&function_end("gcm_ghash_clmul");

}

&set_label("bswap",64);
	&data_byte(15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0);
	&data_byte(1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0xc2);	# 0x1c2_polynomial
&set_label("rem_8bit",64);
	&data_short(0x0000,0x01C2,0x0384,0x0246,0x0708,0x06CA,0x048C,0x054E);
	&data_short(0x0E10,0x0FD2,0x0D94,0x0C56,0x0918,0x08DA,0x0A9C,0x0B5E);
	&data_short(0x1C20,0x1DE2,0x1FA4,0x1E66,0x1B28,0x1AEA,0x18AC,0x196E);
	&data_short(0x1230,0x13F2,0x11B4,0x1076,0x1538,0x14FA,0x16BC,0x177E);
	&data_short(0x3840,0x3982,0x3BC4,0x3A06,0x3F48,0x3E8A,0x3CCC,0x3D0E);
	&data_short(0x3650,0x3792,0x35D4,0x3416,0x3158,0x309A,0x32DC,0x331E);
	&data_short(0x2460,0x25A2,0x27E4,0x2626,0x2368,0x22AA,0x20EC,0x212E);
	&data_short(0x2A70,0x2BB2,0x29F4,0x2836,0x2D78,0x2CBA,0x2EFC,0x2F3E);
	&data_short(0x7080,0x7142,0x7304,0x72C6,0x7788,0x764A,0x740C,0x75CE);
	&data_short(0x7E90,0x7F52,0x7D14,0x7CD6,0x7998,0x785A,0x7A1C,0x7BDE);
	&data_short(0x6CA0,0x6D62,0x6F24,0x6EE6,0x6BA8,0x6A6A,0x682C,0x69EE);
	&data_short(0x62B0,0x6372,0x6134,0x60F6,0x65B8,0x647A,0x663C,0x67FE);
	&data_short(0x48C0,0x4902,0x4B44,0x4A86,0x4FC8,0x4E0A,0x4C4C,0x4D8E);
	&data_short(0x46D0,0x4712,0x4554,0x4496,0x41D8,0x401A,0x425C,0x439E);
	&data_short(0x54E0,0x5522,0x5764,0x56A6,0x53E8,0x522A,0x506C,0x51AE);
	&data_short(0x5AF0,0x5B32,0x5974,0x58B6,0x5DF8,0x5C3A,0x5E7C,0x5FBE);
	&data_short(0xE100,0xE0C2,0xE284,0xE346,0xE608,0xE7CA,0xE58C,0xE44E);
	&data_short(0xEF10,0xEED2,0xEC94,0xED56,0xE818,0xE9DA,0xEB9C,0xEA5E);
	&data_short(0xFD20,0xFCE2,0xFEA4,0xFF66,0xFA28,0xFBEA,0xF9AC,0xF86E);
	&data_short(0xF330,0xF2F2,0xF0B4,0xF176,0xF438,0xF5FA,0xF7BC,0xF67E);
	&data_short(0xD940,0xD882,0xDAC4,0xDB06,0xDE48,0xDF8A,0xDDCC,0xDC0E);
	&data_short(0xD750,0xD692,0xD4D4,0xD516,0xD058,0xD19A,0xD3DC,0xD21E);
	&data_short(0xC560,0xC4A2,0xC6E4,0xC726,0xC268,0xC3AA,0xC1EC,0xC02E);
	&data_short(0xCB70,0xCAB2,0xC8F4,0xC936,0xCC78,0xCDBA,0xCFFC,0xCE3E);
	&data_short(0x9180,0x9042,0x9204,0x93C6,0x9688,0x974A,0x950C,0x94CE);
	&data_short(0x9F90,0x9E52,0x9C14,0x9DD6,0x9898,0x995A,0x9B1C,0x9ADE);
	&data_short(0x8DA0,0x8C62,0x8E24,0x8FE6,0x8AA8,0x8B6A,0x892C,0x88EE);
	&data_short(0x83B0,0x8272,0x8034,0x81F6,0x84B8,0x857A,0x873C,0x86FE);
	&data_short(0xA9C0,0xA802,0xAA44,0xAB86,0xAEC8,0xAF0A,0xAD4C,0xAC8E);
	&data_short(0xA7D0,0xA612,0xA454,0xA596,0xA0D8,0xA11A,0xA35C,0xA29E);
	&data_short(0xB5E0,0xB422,0xB664,0xB7A6,0xB2E8,0xB32A,0xB16C,0xB0AE);
	&data_short(0xBBF0,0xBA32,0xB874,0xB9B6,0xBCF8,0xBD3A,0xBF7C,0xBEBE);
}}	# $sse2

&set_label("rem_4bit",64);
	&data_word(0,0x0000<<$S,0,0x1C20<<$S,0,0x3840<<$S,0,0x2460<<$S);
	&data_word(0,0x7080<<$S,0,0x6CA0<<$S,0,0x48C0<<$S,0,0x54E0<<$S);
	&data_word(0,0xE100<<$S,0,0xFD20<<$S,0,0xD940<<$S,0,0xC560<<$S);
	&data_word(0,0x9180<<$S,0,0x8DA0<<$S,0,0xA9C0<<$S,0,0xB5E0<<$S);
}}}	# !$x86only

&asciz("GHASH for x86, CRYPTOGAMS by <appro\@openssl.org>");
&asm_finish();

close STDOUT;

# A question was risen about choice of vanilla MMX. Or rather why wasn't
# SSE2 chosen instead? In addition to the fact that MMX runs on legacy
# CPUs such as PIII, "4-bit" MMX version was observed to provide better
# performance than *corresponding* SSE2 one even on contemporary CPUs.
# SSE2 results were provided by Peter-Michael Hager. He maintains SSE2
# implementation featuring full range of lookup-table sizes, but with
# per-invocation lookup table setup. Latter means that table size is
# chosen depending on how much data is to be hashed in every given call,
# more data - larger table. Best reported result for Core2 is ~4 cycles
# per processed byte out of 64KB block. This number accounts even for
# 64KB table setup overhead. As discussed in gcm128.c we choose to be
# more conservative in respect to lookup table sizes, but how do the
# results compare? Minimalistic "256B" MMX version delivers ~11 cycles
# on same platform. As also discussed in gcm128.c, next in line "8-bit
# Shoup's" or "4KB" method should deliver twice the performance of
# "256B" one, in other words not worse than ~6 cycles per byte. It
# should be also be noted that in SSE2 case improvement can be "super-
# linear," i.e. more than twice, mostly because >>8 maps to single
# instruction on SSE2 register. This is unlike "4-bit" case when >>4
# maps to same amount of instructions in both MMX and SSE2 cases.
# Bottom line is that switch to SSE2 is considered to be justifiable
# only in case we choose to implement "8-bit" method...
