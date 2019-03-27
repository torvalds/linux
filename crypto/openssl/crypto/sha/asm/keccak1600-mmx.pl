#!/usr/bin/env perl
# Copyright 2017 The OpenSSL Project Authors. All Rights Reserved.
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
# Keccak-1600 for x86 MMX.
#
# June 2017.
#
# Below code is KECCAK_2X implementation (see sha/keccak1600.c) with
# C[5] held in register bank and D[5] offloaded to memory. Though
# instead of actually unrolling the loop pair-wise I simply flip
# pointers to T[][] and A[][] and the end of round. Since number of
# rounds is even, last round writes to A[][] and everything works out.
# It's argued that MMX is the only code path meaningful to implement
# for x86. This is because non-MMX-capable processors is an extinct
# breed, and they as well can lurk executing compiler-generated code.
# For reference gcc-5.x-generated KECCAK_2X code takes 89 cycles per
# processed byte on Pentium. Which is fair result. But older compilers
# produce worse code. On the other hand one can wonder why not 128-bit
# SSE2? Well, SSE2 won't provide double improvement, rather far from
# that, if any at all on some processors, because it will take extra
# permutations and inter-bank data trasfers. Besides, contemporary
# CPUs are better off executing 64-bit code, and it makes lesser sense
# to invest into fancy 32-bit code. And the decision doesn't seem to
# be inadequate, if one compares below results to "64-bit platforms in
# 32-bit mode" SIMD data points available at
# http://keccak.noekeon.org/sw_performance.html.
#
########################################################################
# Numbers are cycles per processed byte out of large message.
#
#			r=1088(i)
#
# PIII			30/+150%
# Pentium M		27/+150%
# P4			40/+85%
# Core 2		19/+170%
# Sandy Bridge(ii)	18/+140%
# Atom			33/+180%
# Silvermont(ii)	30/+180%
# VIA Nano(ii)		43/+60%
# Sledgehammer(ii)(iii)	24/+130%
#
# (i)	Corresponds to SHA3-256. Numbers after slash are improvement
#	coefficients over KECCAK_2X [with bit interleave and lane
#	complementing] position-independent *scalar* code generated
#	by gcc-5.x. It's not exactly fair comparison, but it's a
#	datapoint...
# (ii)	64-bit processor executing 32-bit code.
# (iii)	Result is considered to be representative even for older AMD
#	processors.

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

$output=pop;
open STDOUT,">$output";

&asm_init($ARGV[0],$ARGV[$#ARGV] eq "386");

my @C = map("mm$_",(0..4));
my @T = map("mm$_",(5..7));
my @A = map([ 8*$_-100, 8*($_+1)-100, 8*($_+2)-100,
              8*($_+3)-100, 8*($_+4)-100 ], (0,5,10,15,20));
my @D = map(8*$_+4, (0..4));
my @rhotates = ([  0,  1, 62, 28, 27 ],
                [ 36, 44,  6, 55, 20 ],
                [  3, 10, 43, 25, 39 ],
                [ 41, 45, 15, 21,  8 ],
                [ 18,  2, 61, 56, 14 ]);

&static_label("iotas");

&function_begin_B("_KeccakF1600");
	&movq	(@C[0],&QWP($A[4][0],"esi"));
	&movq	(@C[1],&QWP($A[4][1],"esi"));
	&movq	(@C[2],&QWP($A[4][2],"esi"));
	&movq	(@C[3],&QWP($A[4][3],"esi"));
	&movq	(@C[4],&QWP($A[4][4],"esi"));

	&mov	("ecx",24);			# loop counter
	&jmp	(&label("loop"));

    &set_label("loop",16);
	######################################### Theta
	&pxor	(@C[0],&QWP($A[0][0],"esi"));
	&pxor	(@C[1],&QWP($A[0][1],"esi"));
	&pxor	(@C[2],&QWP($A[0][2],"esi"));
	&pxor	(@C[3],&QWP($A[0][3],"esi"));
	&pxor	(@C[4],&QWP($A[0][4],"esi"));

	&pxor	(@C[0],&QWP($A[1][0],"esi"));
	&pxor	(@C[1],&QWP($A[1][1],"esi"));
	&pxor	(@C[2],&QWP($A[1][2],"esi"));
	&pxor	(@C[3],&QWP($A[1][3],"esi"));
	&pxor	(@C[4],&QWP($A[1][4],"esi"));

	&pxor	(@C[0],&QWP($A[2][0],"esi"));
	&pxor	(@C[1],&QWP($A[2][1],"esi"));
	&pxor	(@C[2],&QWP($A[2][2],"esi"));
	&pxor	(@C[3],&QWP($A[2][3],"esi"));
	&pxor	(@C[4],&QWP($A[2][4],"esi"));

	&pxor	(@C[2],&QWP($A[3][2],"esi"));
	&pxor	(@C[0],&QWP($A[3][0],"esi"));
	&pxor	(@C[1],&QWP($A[3][1],"esi"));
	&pxor	(@C[3],&QWP($A[3][3],"esi"));
	 &movq	(@T[0],@C[2]);
	&pxor	(@C[4],&QWP($A[3][4],"esi"));

	 &movq	(@T[2],@C[2]);
	 &psrlq	(@T[0],63);
	&movq	(@T[1],@C[0]);
	 &psllq	(@T[2],1);
	 &pxor	(@T[0],@C[0]);
	&psrlq	(@C[0],63);
	 &pxor	(@T[0],@T[2]);
	&psllq	(@T[1],1);
	 &movq	(@T[2],@C[1]);
	 &movq	(&QWP(@D[1],"esp"),@T[0]);	# D[1] = E[0] = ROL64(C[2], 1) ^ C[0];

	&pxor	(@T[1],@C[0]);
	 &psrlq	(@T[2],63);
	&pxor	(@T[1],@C[3]);
	 &movq	(@C[0],@C[1]);
	&movq	(&QWP(@D[4],"esp"),@T[1]);	# D[4] = E[1] = ROL64(C[0], 1) ^ C[3];

	 &psllq	(@C[0],1);
	 &pxor	(@T[2],@C[4]);
	 &pxor	(@C[0],@T[2]);

	&movq	(@T[2],@C[3]);
	&psrlq	(@C[3],63);
	 &movq	(&QWP(@D[0],"esp"),@C[0]);	# D[0] = C[0] = ROL64(C[1], 1) ^ C[4];
	&psllq	(@T[2],1);
	 &movq	(@T[0],@C[4]);
	 &psrlq	(@C[4],63);
	&pxor	(@C[1],@C[3]);
	 &psllq	(@T[0],1);
	&pxor	(@C[1],@T[2]);
	 &pxor	(@C[2],@C[4]);
	&movq	(&QWP(@D[2],"esp"),@C[1]);	# D[2] = C[1] = ROL64(C[3], 1) ^ C[1];
	 &pxor	(@C[2],@T[0]);

	######################################### first Rho(0) is special
	&movq	(@C[3],&QWP($A[3][3],"esi"));
	 &movq	(&QWP(@D[3],"esp"),@C[2]);	# D[3] = C[2] = ROL64(C[4], 1) ^ C[2];
	&pxor	(@C[3],@C[2]);
	 &movq	(@C[4],&QWP($A[4][4],"esi"));
	&movq	(@T[2],@C[3]);
	&psrlq	(@C[3],64-$rhotates[3][3]);
	 &pxor	(@C[4],@T[1]);
	&psllq	(@T[2],$rhotates[3][3]);
	 &movq	(@T[1],@C[4]);
	 &psrlq	(@C[4],64-$rhotates[4][4]);
	&por	(@C[3],@T[2]);		# C[3] = ROL64(A[3][3] ^ C[2], rhotates[3][3]);   /* D[3] */
	 &psllq	(@T[1],$rhotates[4][4]);

	&movq	(@C[2],&QWP($A[2][2],"esi"));
	 &por	(@C[4],@T[1]);		# C[4] = ROL64(A[4][4] ^ E[1], rhotates[4][4]);   /* D[4] */
	&pxor	(@C[2],@C[1]);
	 &movq	(@C[1],&QWP($A[1][1],"esi"));
	&movq	(@T[1],@C[2]);
	&psrlq	(@C[2],64-$rhotates[2][2]);
	 &pxor	(@C[1],&QWP(@D[1],"esp"));
	&psllq	(@T[1],$rhotates[2][2]);

	 &movq	(@T[2],@C[1]);
	 &psrlq	(@C[1],64-$rhotates[1][1]);
	&por	(@C[2],@T[1]);		# C[2] = ROL64(A[2][2] ^ C[1], rhotates[2][2]);   /* D[2] */
	 &psllq	(@T[2],$rhotates[1][1]);
	&pxor	(@C[0],&QWP($A[0][0],"esi")); # /* rotate by 0 */  /* D[0] */
	 &por	(@C[1],@T[2]);		# C[1] = ROL64(A[1][1] ^ D[1], rhotates[1][1]);

sub Chi() {				######### regular Chi step
    my ($y,$xrho) = @_;

	&movq	(@T[0],@C[1]);
	 &movq	(@T[1],@C[2]);
	&pandn	(@T[0],@C[2]);
	 &pandn	(@C[2],@C[3]);
	&pxor	(@T[0],@C[0]);
	 &pxor	(@C[2],@C[1]);
	&pxor	(@T[0],&QWP(0,"ebx"))		if ($y == 0);
	&lea	("ebx",&DWP(8,"ebx"))		if ($y == 0);

	&movq	(@T[2],@C[3]);
	&movq	(&QWP($A[$y][0],"edi"),@T[0]);	# R[0][0] = C[0] ^ (~C[1] & C[2]) ^ iotas[i];
	 &movq	(@T[0],@C[4]);
	&pandn	(@C[3],@C[4]);
	 &pandn	(@C[4],@C[0]);
	&pxor	(@C[3],@T[1]);
	 &movq	(&QWP($A[$y][1],"edi"),@C[2]);	# R[0][1] = C[1] ^ (~C[2] & C[3]);
	 &pxor	(@C[4],@T[2]);
	  &movq	(@T[2],&QWP($A[0][$xrho],"esi"))	if (defined($xrho));

	 &movq	(&QWP($A[$y][2],"edi"),@C[3]);	# R[0][2] = C[2] ^ (~C[3] & C[4]);
	&pandn	(@C[0],@C[1]);
	 &movq	(&QWP($A[$y][3],"edi"),@C[4]);	# R[0][3] = C[3] ^ (~C[4] & C[0]);
	&pxor	(@C[0],@T[0]);
	  &pxor	(@T[2],&QWP(@D[$xrho],"esp"))		if (defined($xrho));
	&movq	(&QWP($A[$y][4],"edi"),@C[0]);	# R[0][4] = C[4] ^ (~C[0] & C[1]);
}
	&Chi	(0, 3);

sub Rho() {				######### regular Rho step
    my $x = shift;

	#&movq	(@T[2],&QWP($A[0][$x],"esi"));	# moved to Chi
	#&pxor	(@T[2],&QWP(@D[$x],"esp"));	# moved to Chi
	&movq	(@C[0],@T[2]);
	&psrlq	(@T[2],64-$rhotates[0][$x]);
	 &movq	(@C[1],&QWP($A[1][($x+1)%5],"esi"));
	&psllq	(@C[0],$rhotates[0][$x]);
	 &pxor	(@C[1],&QWP(@D[($x+1)%5],"esp"));
	&por	(@C[0],@T[2]);		# C[0] = ROL64(A[0][3] ^ D[3], rhotates[0][3]);

	 &movq	(@T[1],@C[1]);
	 &psrlq	(@C[1],64-$rhotates[1][($x+1)%5]);
	&movq	(@C[2],&QWP($A[2][($x+2)%5],"esi"));
	 &psllq	(@T[1],$rhotates[1][($x+1)%5]);
	&pxor	(@C[2],&QWP(@D[($x+2)%5],"esp"));
	 &por	(@C[1],@T[1]);		# C[1] = ROL64(A[1][4] ^ D[4], rhotates[1][4]);

	&movq	(@T[2],@C[2]);
	&psrlq	(@C[2],64-$rhotates[2][($x+2)%5]);
	 &movq	(@C[3],&QWP($A[3][($x+3)%5],"esi"));
	&psllq	(@T[2],$rhotates[2][($x+2)%5]);
	 &pxor	(@C[3],&QWP(@D[($x+3)%5],"esp"));
	&por	(@C[2],@T[2]);		# C[2] = ROL64(A[2][0] ^ D[0], rhotates[2][0]);

	 &movq	(@T[0],@C[3]);
	 &psrlq	(@C[3],64-$rhotates[3][($x+3)%5]);
	&movq	(@C[4],&QWP($A[4][($x+4)%5],"esi"));
	 &psllq	(@T[0],$rhotates[3][($x+3)%5]);
	&pxor	(@C[4],&QWP(@D[($x+4)%5],"esp"));
	 &por	(@C[3],@T[0]);		# C[3] = ROL64(A[3][1] ^ D[1], rhotates[3][1]);

	&movq	(@T[1],@C[4]);
	&psrlq	(@C[4],64-$rhotates[4][($x+4)%5]);
	&psllq	(@T[1],$rhotates[4][($x+4)%5]);
	&por	(@C[4],@T[1]);		# C[4] = ROL64(A[4][2] ^ D[2], rhotates[4][2]);
}
	&Rho	(3);	&Chi	(1, 1);
	&Rho	(1);	&Chi	(2, 4);
	&Rho	(4);	&Chi	(3, 2);
	&Rho	(2);	###&Chi	(4);

	&movq	(@T[0],@C[0]);		######### last Chi(4) is special
	 &xor	("edi","esi");		# &xchg	("esi","edi");
	&movq	(&QWP(@D[1],"esp"),@C[1]);
	 &xor	("esi","edi");
	 &xor	("edi","esi");

	&movq	(@T[1],@C[1]);
	 &movq	(@T[2],@C[2]);
	&pandn	(@T[1],@C[2]);
	 &pandn	(@T[2],@C[3]);
	&pxor	(@C[0],@T[1]);
	 &pxor	(@C[1],@T[2]);

	&movq	(@T[1],@C[3]);
	 &movq	(&QWP($A[4][0],"esi"),@C[0]);	# R[4][0] = C[0] ^= (~C[1] & C[2]);
	&pandn	(@T[1],@C[4]);
	 &movq	(&QWP($A[4][1],"esi"),@C[1]);	# R[4][1] = C[1] ^= (~C[2] & C[3]);
	&pxor	(@C[2],@T[1]);
	 &movq	(@T[2],@C[4]);
	&movq	(&QWP($A[4][2],"esi"),@C[2]);	# R[4][2] = C[2] ^= (~C[3] & C[4]);

	&pandn	(@T[2],@T[0]);
	 &pandn	(@T[0],&QWP(@D[1],"esp"));
	&pxor	(@C[3],@T[2]);
	 &pxor	(@C[4],@T[0]);
	&movq	(&QWP($A[4][3],"esi"),@C[3]);	# R[4][3] = C[3] ^= (~C[4] & D[0]);
	&sub	("ecx",1);
	 &movq	(&QWP($A[4][4],"esi"),@C[4]);	# R[4][4] = C[4] ^= (~D[0] & D[1]);
	&jnz	(&label("loop"));

	&lea	("ebx",&DWP(-192,"ebx"));	# rewind iotas
	&ret	();
&function_end_B("_KeccakF1600");

&function_begin("KeccakF1600");
	&mov	("esi",&wparam(0));
	&mov	("ebp","esp");
	&sub	("esp",240);
	&call	(&label("pic_point"));
    &set_label("pic_point");
	&blindpop("ebx");
	&lea	("ebx",&DWP(&label("iotas")."-".&label("pic_point"),"ebx"));
	&and	("esp",-8);
	&lea	("esi",&DWP(100,"esi"));	# size optimization
	&lea	("edi",&DWP(8*5+100,"esp"));	# size optimization

	&call	("_KeccakF1600");

	&mov	("esp","ebp");
	&emms	();
&function_end("KeccakF1600");

&function_begin("SHA3_absorb");
	&mov	("esi",&wparam(0));		# A[][]
	&mov	("eax",&wparam(1));		# inp
	&mov	("ecx",&wparam(2));		# len
	&mov	("edx",&wparam(3));		# bsz
	&mov	("ebp","esp");
	&sub	("esp",240+8);
	&call	(&label("pic_point"));
    &set_label("pic_point");
	&blindpop("ebx");
	&lea	("ebx",&DWP(&label("iotas")."-".&label("pic_point"),"ebx"));
	&and	("esp",-8);

	&mov	("edi","esi");
	&lea	("esi",&DWP(100,"esi"));	# size optimization
	&mov	(&DWP(-4,"ebp"),"edx");		# save bsz
	&jmp	(&label("loop"));

&set_label("loop",16);
	&cmp	("ecx","edx");			# len < bsz?
	&jc	(&label("absorbed"));

	&shr	("edx",3);			# bsz /= 8
&set_label("block");
	&movq	("mm0",&QWP(0,"eax"));
	&lea	("eax",&DWP(8,"eax"));
	&pxor	("mm0",&QWP(0,"edi"));
	&lea	("edi",&DWP(8,"edi"));
	&sub	("ecx",8);			# len -= 8
	&movq	(&QWP(-8,"edi"),"mm0");
	&dec	("edx");			# bsz--
	&jnz	(&label("block"));

	&lea	("edi",&DWP(8*5+100,"esp"));	# size optimization
	&mov	(&DWP(-8,"ebp"),"ecx");		# save len
	&call	("_KeccakF1600");
	&mov	("ecx",&DWP(-8,"ebp"));		# pull len
	&mov	("edx",&DWP(-4,"ebp"));		# pull bsz
	&lea	("edi",&DWP(-100,"esi"));
	&jmp	(&label("loop"));

&set_label("absorbed",16);
	&mov	("eax","ecx");			# return value
	&mov	("esp","ebp");
	&emms	();
&function_end("SHA3_absorb");

&function_begin("SHA3_squeeze");
	&mov	("esi",&wparam(0));		# A[][]
	&mov	("eax",&wparam(1));		# out
	&mov	("ecx",&wparam(2));		# len
	&mov	("edx",&wparam(3));		# bsz
	&mov	("ebp","esp");
	&sub	("esp",240+8);
	&call	(&label("pic_point"));
    &set_label("pic_point");
	&blindpop("ebx");
	&lea	("ebx",&DWP(&label("iotas")."-".&label("pic_point"),"ebx"));
	&and	("esp",-8);

	&shr	("edx",3);			# bsz /= 8
	&mov	("edi","esi");
	&lea	("esi",&DWP(100,"esi"));	# size optimization
	&mov	(&DWP(-4,"ebp"),"edx");		# save bsz
	&jmp	(&label("loop"));

&set_label("loop",16);
	&cmp	("ecx",8);			# len < 8?
	&jc	(&label("tail"));

	&movq	("mm0",&QWP(0,"edi"));
	&lea	("edi",&DWP(8,"edi"));
	&movq	(&QWP(0,"eax"),"mm0");
	&lea	("eax",&DWP(8,"eax"));
	&sub	("ecx",8);			# len -= 8
	&jz	(&label("done"));

	&dec	("edx");			# bsz--
	&jnz	(&label("loop"));

	&lea	("edi",&DWP(8*5+100,"esp"));	# size optimization
	&mov	(&DWP(-8,"ebp"),"ecx");		# save len
	&call	("_KeccakF1600");
	&mov	("ecx",&DWP(-8,"ebp"));		# pull len
	&mov	("edx",&DWP(-4,"ebp"));		# pull bsz
	&lea	("edi",&DWP(-100,"esi"));
	&jmp	(&label("loop"));

&set_label("tail",16);
	&mov	("esi","edi");
	&mov	("edi","eax");
	&data_word("0xA4F39066");		# rep movsb

&set_label("done");
	&mov	("esp","ebp");
	&emms	();
&function_end("SHA3_squeeze");

&set_label("iotas",32);
	&data_word(0x00000001,0x00000000);
	&data_word(0x00008082,0x00000000);
	&data_word(0x0000808a,0x80000000);
	&data_word(0x80008000,0x80000000);
	&data_word(0x0000808b,0x00000000);
	&data_word(0x80000001,0x00000000);
	&data_word(0x80008081,0x80000000);
	&data_word(0x00008009,0x80000000);
	&data_word(0x0000008a,0x00000000);
	&data_word(0x00000088,0x00000000);
	&data_word(0x80008009,0x00000000);
	&data_word(0x8000000a,0x00000000);
	&data_word(0x8000808b,0x00000000);
	&data_word(0x0000008b,0x80000000);
	&data_word(0x00008089,0x80000000);
	&data_word(0x00008003,0x80000000);
	&data_word(0x00008002,0x80000000);
	&data_word(0x00000080,0x80000000);
	&data_word(0x0000800a,0x00000000);
	&data_word(0x8000000a,0x80000000);
	&data_word(0x80008081,0x80000000);
	&data_word(0x00008080,0x80000000);
	&data_word(0x80000001,0x00000000);
	&data_word(0x80008008,0x80000000);
&asciz("Keccak-1600 absorb and squeeze for MMX, CRYPTOGAMS by <appro\@openssl.org>");

&asm_finish();

close STDOUT;
