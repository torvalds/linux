#! /usr/bin/env perl
# Copyright 2015-2018 The OpenSSL Project Authors. All Rights Reserved.
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
#
# ECP_NISTZ256 module for x86/SSE2.
#
# October 2014.
#
# Original ECP_NISTZ256 submission targeting x86_64 is detailed in
# http://eprint.iacr.org/2013/816. In the process of adaptation
# original .c module was made 32-bit savvy in order to make this
# implementation possible.
#
#		with/without -DECP_NISTZ256_ASM
# Pentium	+66-163%
# PIII		+72-172%
# P4		+65-132%
# Core2		+90-215%
# Sandy Bridge	+105-265% (contemporary i[57]-* are all close to this)
# Atom		+65-155%
# Opteron	+54-110%
# Bulldozer	+99-240%
# VIA Nano	+93-290%
#
# Ranges denote minimum and maximum improvement coefficients depending
# on benchmark. Lower coefficients are for ECDSA sign, server-side
# operation. Keep in mind that +200% means 3x improvement.

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

$output=pop;
open STDOUT,">$output";

&asm_init($ARGV[0],$ARGV[$#ARGV] eq "386");

$sse2=0;
for (@ARGV) { $sse2=1 if (/-DOPENSSL_IA32_SSE2/); }

&external_label("OPENSSL_ia32cap_P") if ($sse2);


########################################################################
# Convert ecp_nistz256_table.c to layout expected by ecp_nistz_gather_w7
#
open TABLE,"<ecp_nistz256_table.c"		or
open TABLE,"<${dir}../ecp_nistz256_table.c"	or
die "failed to open ecp_nistz256_table.c:",$!;

use integer;

foreach(<TABLE>) {
	s/TOBN\(\s*(0x[0-9a-f]+),\s*(0x[0-9a-f]+)\s*\)/push @arr,hex($2),hex($1)/geo;
}
close TABLE;

# See ecp_nistz256_table.c for explanation for why it's 64*16*37.
# 64*16*37-1 is because $#arr returns last valid index or @arr, not
# amount of elements.
die "insane number of elements" if ($#arr != 64*16*37-1);

&public_label("ecp_nistz256_precomputed");
&align(4096);
&set_label("ecp_nistz256_precomputed");

########################################################################
# this conversion smashes P256_POINT_AFFINE by individual bytes with
# 64 byte interval, similar to
#	1111222233334444
#	1234123412341234
for(1..37) {
	@tbl = splice(@arr,0,64*16);
	for($i=0;$i<64;$i++) {
		undef @line;
		for($j=0;$j<64;$j++) {
			push @line,(@tbl[$j*16+$i/4]>>(($i%4)*8))&0xff;
		}
		&data_byte(join(',',map { sprintf "0x%02x",$_} @line));
	}
}

########################################################################
# Keep in mind that constants are stored least to most significant word
&static_label("RR");
&set_label("RR",64);
&data_word(3,0,-1,-5,-2,-1,-3,4);	# 2^512 mod P-256

&static_label("ONE_mont");
&set_label("ONE_mont");
&data_word(1,0,0,-1,-1,-1,-2,0);

&static_label("ONE");
&set_label("ONE");
&data_word(1,0,0,0,0,0,0,0);
&asciz("ECP_NISZ256 for x86/SSE2, CRYPTOGAMS by <appro\@openssl.org>");
&align(64);

########################################################################
# void ecp_nistz256_mul_by_2(BN_ULONG edi[8],const BN_ULONG esi[8]);
&function_begin("ecp_nistz256_mul_by_2");
	&mov	("esi",&wparam(1));
	&mov	("edi",&wparam(0));
	&mov	("ebp","esi");
########################################################################
# common pattern for internal functions is that %edi is result pointer,
# %esi and %ebp are input ones, %ebp being optional. %edi is preserved.
	&call	("_ecp_nistz256_add");
&function_end("ecp_nistz256_mul_by_2");

########################################################################
# void ecp_nistz256_mul_by_3(BN_ULONG edi[8],const BN_ULONG esi[8]);
&function_begin("ecp_nistz256_mul_by_3");
	&mov	("esi",&wparam(1));
					# multiplication by 3 is performed
					# as 2*n+n, but we can't use output
					# to store 2*n, because if output
					# pointer equals to input, then
					# we'll get 2*n+2*n.
	&stack_push(8);			# therefore we need to allocate
					# 256-bit intermediate buffer.
	&mov	("edi","esp");
	&mov	("ebp","esi");
	&call	("_ecp_nistz256_add");
	&lea	("esi",&DWP(0,"edi"));
	&mov	("ebp",&wparam(1));
	&mov	("edi",&wparam(0));
	&call	("_ecp_nistz256_add");
	&stack_pop(8);
&function_end("ecp_nistz256_mul_by_3");

########################################################################
# void ecp_nistz256_div_by_2(BN_ULONG edi[8],const BN_ULONG esi[8]);
&function_begin("ecp_nistz256_div_by_2");
	&mov	("esi",&wparam(1));
	&mov	("edi",&wparam(0));
	&call	("_ecp_nistz256_div_by_2");
&function_end("ecp_nistz256_div_by_2");

&function_begin_B("_ecp_nistz256_div_by_2");
	# tmp = a is odd ? a+mod : a
	#
	# note that because mod has special form, i.e. consists of
	# 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	# assigning least significant bit of input to one register,
	# %ebp, and its negative to another, %edx.

	&mov	("ebp",&DWP(0,"esi"));
	&xor	("edx","edx");
	&mov	("ebx",&DWP(4,"esi"));
	&mov	("eax","ebp");
	&and	("ebp",1);
	&mov	("ecx",&DWP(8,"esi"));
	&sub	("edx","ebp");

	&add	("eax","edx");
	&adc	("ebx","edx");
	&mov	(&DWP(0,"edi"),"eax");
	&adc	("ecx","edx");
	&mov	(&DWP(4,"edi"),"ebx");
	&mov	(&DWP(8,"edi"),"ecx");

	&mov	("eax",&DWP(12,"esi"));
	&mov	("ebx",&DWP(16,"esi"));
	&adc	("eax",0);
	&mov	("ecx",&DWP(20,"esi"));
	&adc	("ebx",0);
	&mov	(&DWP(12,"edi"),"eax");
	&adc	("ecx",0);
	&mov	(&DWP(16,"edi"),"ebx");
	&mov	(&DWP(20,"edi"),"ecx");

	&mov	("eax",&DWP(24,"esi"));
	&mov	("ebx",&DWP(28,"esi"));
	&adc	("eax","ebp");
	&adc	("ebx","edx");
	&mov	(&DWP(24,"edi"),"eax");
	&sbb	("esi","esi");			# broadcast carry bit
	&mov	(&DWP(28,"edi"),"ebx");

	# ret = tmp >> 1

	&mov	("eax",&DWP(0,"edi"));
	&mov	("ebx",&DWP(4,"edi"));
	&mov	("ecx",&DWP(8,"edi"));
	&mov	("edx",&DWP(12,"edi"));

	&shr	("eax",1);
	&mov	("ebp","ebx");
	&shl	("ebx",31);
	&or	("eax","ebx");

	&shr	("ebp",1);
	&mov	("ebx","ecx");
	&shl	("ecx",31);
	&mov	(&DWP(0,"edi"),"eax");
	&or	("ebp","ecx");
	&mov	("eax",&DWP(16,"edi"));

	&shr	("ebx",1);
	&mov	("ecx","edx");
	&shl	("edx",31);
	&mov	(&DWP(4,"edi"),"ebp");
	&or	("ebx","edx");
	&mov	("ebp",&DWP(20,"edi"));

	&shr	("ecx",1);
	&mov	("edx","eax");
	&shl	("eax",31);
	&mov	(&DWP(8,"edi"),"ebx");
	&or	("ecx","eax");
	&mov	("ebx",&DWP(24,"edi"));

	&shr	("edx",1);
	&mov	("eax","ebp");
	&shl	("ebp",31);
	&mov	(&DWP(12,"edi"),"ecx");
	&or	("edx","ebp");
	&mov	("ecx",&DWP(28,"edi"));

	&shr	("eax",1);
	&mov	("ebp","ebx");
	&shl	("ebx",31);
	&mov	(&DWP(16,"edi"),"edx");
	&or	("eax","ebx");

	&shr	("ebp",1);
	&mov	("ebx","ecx");
	&shl	("ecx",31);
	&mov	(&DWP(20,"edi"),"eax");
	&or	("ebp","ecx");

	&shr	("ebx",1);
	&shl	("esi",31);
	&mov	(&DWP(24,"edi"),"ebp");
	&or	("ebx","esi");			# handle top-most carry bit
	&mov	(&DWP(28,"edi"),"ebx");

	&ret	();
&function_end_B("_ecp_nistz256_div_by_2");

########################################################################
# void ecp_nistz256_add(BN_ULONG edi[8],const BN_ULONG esi[8],
#					const BN_ULONG ebp[8]);
&function_begin("ecp_nistz256_add");
	&mov	("esi",&wparam(1));
	&mov	("ebp",&wparam(2));
	&mov	("edi",&wparam(0));
	&call	("_ecp_nistz256_add");
&function_end("ecp_nistz256_add");

&function_begin_B("_ecp_nistz256_add");
	&mov	("eax",&DWP(0,"esi"));
	&mov	("ebx",&DWP(4,"esi"));
	&mov	("ecx",&DWP(8,"esi"));
	&add	("eax",&DWP(0,"ebp"));
	&mov	("edx",&DWP(12,"esi"));
	&adc	("ebx",&DWP(4,"ebp"));
	&mov	(&DWP(0,"edi"),"eax");
	&adc	("ecx",&DWP(8,"ebp"));
	&mov	(&DWP(4,"edi"),"ebx");
	&adc	("edx",&DWP(12,"ebp"));
	&mov	(&DWP(8,"edi"),"ecx");
	&mov	(&DWP(12,"edi"),"edx");

	&mov	("eax",&DWP(16,"esi"));
	&mov	("ebx",&DWP(20,"esi"));
	&mov	("ecx",&DWP(24,"esi"));
	&adc	("eax",&DWP(16,"ebp"));
	&mov	("edx",&DWP(28,"esi"));
	&adc	("ebx",&DWP(20,"ebp"));
	&mov	(&DWP(16,"edi"),"eax");
	&adc	("ecx",&DWP(24,"ebp"));
	&mov	(&DWP(20,"edi"),"ebx");
	&mov	("esi",0);
	&adc	("edx",&DWP(28,"ebp"));
	&mov	(&DWP(24,"edi"),"ecx");
	&adc	("esi",0);
	&mov	(&DWP(28,"edi"),"edx");

	# if a+b >= modulus, subtract modulus.
	#
	# But since comparison implies subtraction, we subtract modulus
	# to see if it borrows, and then subtract it for real if
	# subtraction didn't borrow.

	&mov	("eax",&DWP(0,"edi"));
	&mov	("ebx",&DWP(4,"edi"));
	&mov	("ecx",&DWP(8,"edi"));
	&sub	("eax",-1);
	&mov	("edx",&DWP(12,"edi"));
	&sbb	("ebx",-1);
	&mov	("eax",&DWP(16,"edi"));
	&sbb	("ecx",-1);
	&mov	("ebx",&DWP(20,"edi"));
	&sbb	("edx",0);
	&mov	("ecx",&DWP(24,"edi"));
	&sbb	("eax",0);
	&mov	("edx",&DWP(28,"edi"));
	&sbb	("ebx",0);
	&sbb	("ecx",1);
	&sbb	("edx",-1);
	&sbb	("esi",0);

	# Note that because mod has special form, i.e. consists of
	# 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	# by using borrow.

	&not	("esi");
	&mov	("eax",&DWP(0,"edi"));
	&mov	("ebp","esi");
	&mov	("ebx",&DWP(4,"edi"));
	&shr	("ebp",31);
	&mov	("ecx",&DWP(8,"edi"));
	&sub	("eax","esi");
	&mov	("edx",&DWP(12,"edi"));
	&sbb	("ebx","esi");
	&mov	(&DWP(0,"edi"),"eax");
	&sbb	("ecx","esi");
	&mov	(&DWP(4,"edi"),"ebx");
	&sbb	("edx",0);
	&mov	(&DWP(8,"edi"),"ecx");
	&mov	(&DWP(12,"edi"),"edx");

	&mov	("eax",&DWP(16,"edi"));
	&mov	("ebx",&DWP(20,"edi"));
	&mov	("ecx",&DWP(24,"edi"));
	&sbb	("eax",0);
	&mov	("edx",&DWP(28,"edi"));
	&sbb	("ebx",0);
	&mov	(&DWP(16,"edi"),"eax");
	&sbb	("ecx","ebp");
	&mov	(&DWP(20,"edi"),"ebx");
	&sbb	("edx","esi");
	&mov	(&DWP(24,"edi"),"ecx");
	&mov	(&DWP(28,"edi"),"edx");

	&ret	();
&function_end_B("_ecp_nistz256_add");

########################################################################
# void ecp_nistz256_sub(BN_ULONG edi[8],const BN_ULONG esi[8],
#					const BN_ULONG ebp[8]);
&function_begin("ecp_nistz256_sub");
	&mov	("esi",&wparam(1));
	&mov	("ebp",&wparam(2));
	&mov	("edi",&wparam(0));
	&call	("_ecp_nistz256_sub");
&function_end("ecp_nistz256_sub");

&function_begin_B("_ecp_nistz256_sub");
	&mov	("eax",&DWP(0,"esi"));
	&mov	("ebx",&DWP(4,"esi"));
	&mov	("ecx",&DWP(8,"esi"));
	&sub	("eax",&DWP(0,"ebp"));
	&mov	("edx",&DWP(12,"esi"));
	&sbb	("ebx",&DWP(4,"ebp"));
	&mov	(&DWP(0,"edi"),"eax");
	&sbb	("ecx",&DWP(8,"ebp"));
	&mov	(&DWP(4,"edi"),"ebx");
	&sbb	("edx",&DWP(12,"ebp"));
	&mov	(&DWP(8,"edi"),"ecx");
	&mov	(&DWP(12,"edi"),"edx");

	&mov	("eax",&DWP(16,"esi"));
	&mov	("ebx",&DWP(20,"esi"));
	&mov	("ecx",&DWP(24,"esi"));
	&sbb	("eax",&DWP(16,"ebp"));
	&mov	("edx",&DWP(28,"esi"));
	&sbb	("ebx",&DWP(20,"ebp"));
	&sbb	("ecx",&DWP(24,"ebp"));
	&mov	(&DWP(16,"edi"),"eax");
	&sbb	("edx",&DWP(28,"ebp"));
	&mov	(&DWP(20,"edi"),"ebx");
	&sbb	("esi","esi");			# broadcast borrow bit
	&mov	(&DWP(24,"edi"),"ecx");
	&mov	(&DWP(28,"edi"),"edx");

	# if a-b borrows, add modulus.
	#
	# Note that because mod has special form, i.e. consists of
	# 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	# assigning borrow bit to one register, %ebp, and its negative
	# to another, %esi. But we started by calculating %esi...

	&mov	("eax",&DWP(0,"edi"));
	&mov	("ebp","esi");
	&mov	("ebx",&DWP(4,"edi"));
	&shr	("ebp",31);
	&mov	("ecx",&DWP(8,"edi"));
	&add	("eax","esi");
	&mov	("edx",&DWP(12,"edi"));
	&adc	("ebx","esi");
	&mov	(&DWP(0,"edi"),"eax");
	&adc	("ecx","esi");
	&mov	(&DWP(4,"edi"),"ebx");
	&adc	("edx",0);
	&mov	(&DWP(8,"edi"),"ecx");
	&mov	(&DWP(12,"edi"),"edx");

	&mov	("eax",&DWP(16,"edi"));
	&mov	("ebx",&DWP(20,"edi"));
	&mov	("ecx",&DWP(24,"edi"));
	&adc	("eax",0);
	&mov	("edx",&DWP(28,"edi"));
	&adc	("ebx",0);
	&mov	(&DWP(16,"edi"),"eax");
	&adc	("ecx","ebp");
	&mov	(&DWP(20,"edi"),"ebx");
	&adc	("edx","esi");
	&mov	(&DWP(24,"edi"),"ecx");
	&mov	(&DWP(28,"edi"),"edx");

	&ret	();
&function_end_B("_ecp_nistz256_sub");

########################################################################
# void ecp_nistz256_neg(BN_ULONG edi[8],const BN_ULONG esi[8]);
&function_begin("ecp_nistz256_neg");
	&mov	("ebp",&wparam(1));
	&mov	("edi",&wparam(0));

	&xor	("eax","eax");
	&stack_push(8);
	&mov	(&DWP(0,"esp"),"eax");
	&mov	("esi","esp");
	&mov	(&DWP(4,"esp"),"eax");
	&mov	(&DWP(8,"esp"),"eax");
	&mov	(&DWP(12,"esp"),"eax");
	&mov	(&DWP(16,"esp"),"eax");
	&mov	(&DWP(20,"esp"),"eax");
	&mov	(&DWP(24,"esp"),"eax");
	&mov	(&DWP(28,"esp"),"eax");

	&call	("_ecp_nistz256_sub");

	&stack_pop(8);
&function_end("ecp_nistz256_neg");

&function_begin_B("_picup_eax");
	&mov	("eax",&DWP(0,"esp"));
	&ret	();
&function_end_B("_picup_eax");

########################################################################
# void ecp_nistz256_to_mont(BN_ULONG edi[8],const BN_ULONG esi[8]);
&function_begin("ecp_nistz256_to_mont");
	&mov	("esi",&wparam(1));
	&call	("_picup_eax");
    &set_label("pic");
	&lea	("ebp",&DWP(&label("RR")."-".&label("pic"),"eax"));
						if ($sse2) {
	&picmeup("eax","OPENSSL_ia32cap_P","eax",&label("pic"));
	&mov	("eax",&DWP(0,"eax"));		}
	&mov	("edi",&wparam(0));
	&call	("_ecp_nistz256_mul_mont");
&function_end("ecp_nistz256_to_mont");

########################################################################
# void ecp_nistz256_from_mont(BN_ULONG edi[8],const BN_ULONG esi[8]);
&function_begin("ecp_nistz256_from_mont");
	&mov	("esi",&wparam(1));
	&call	("_picup_eax");
    &set_label("pic");
	&lea	("ebp",&DWP(&label("ONE")."-".&label("pic"),"eax"));
						if ($sse2) {
	&picmeup("eax","OPENSSL_ia32cap_P","eax",&label("pic"));
	&mov	("eax",&DWP(0,"eax"));		}
	&mov	("edi",&wparam(0));
	&call	("_ecp_nistz256_mul_mont");
&function_end("ecp_nistz256_from_mont");

########################################################################
# void ecp_nistz256_mul_mont(BN_ULONG edi[8],const BN_ULONG esi[8],
#					     const BN_ULONG ebp[8]);
&function_begin("ecp_nistz256_mul_mont");
	&mov	("esi",&wparam(1));
	&mov	("ebp",&wparam(2));
						if ($sse2) {
	&call	("_picup_eax");
    &set_label("pic");
	&picmeup("eax","OPENSSL_ia32cap_P","eax",&label("pic"));
	&mov	("eax",&DWP(0,"eax"));		}
	&mov	("edi",&wparam(0));
	&call	("_ecp_nistz256_mul_mont");
&function_end("ecp_nistz256_mul_mont");

########################################################################
# void ecp_nistz256_sqr_mont(BN_ULONG edi[8],const BN_ULONG esi[8]);
&function_begin("ecp_nistz256_sqr_mont");
	&mov	("esi",&wparam(1));
						if ($sse2) {
	&call	("_picup_eax");
    &set_label("pic");
	&picmeup("eax","OPENSSL_ia32cap_P","eax",&label("pic"));
	&mov	("eax",&DWP(0,"eax"));		}
	&mov	("edi",&wparam(0));
	&mov	("ebp","esi");
	&call	("_ecp_nistz256_mul_mont");
&function_end("ecp_nistz256_sqr_mont");

&function_begin_B("_ecp_nistz256_mul_mont");
						if ($sse2) {
	&and	("eax",1<<24|1<<26);
	&cmp	("eax",1<<24|1<<26);		# see if XMM+SSE2 is on
	&jne	(&label("mul_mont_ialu"));

	########################################
	# SSE2 code path featuring 32x16-bit
	# multiplications is ~2x faster than
	# IALU counterpart (except on Atom)...
	########################################
	# stack layout:
	# +------------------------------------+< %esp
	# | 7 16-byte temporary XMM words,     |
	# | "sliding" toward lower address     |
	# .                                    .
	# +------------------------------------+
	# | unused XMM word                    |
	# +------------------------------------+< +128,%ebx
	# | 8 16-byte XMM words holding copies |
	# | of a[i]<<64|a[i]                   |
	# .                                    .
	# .                                    .
	# +------------------------------------+< +256
	&mov	("edx","esp");
	&sub	("esp",0x100);

	&movd	("xmm7",&DWP(0,"ebp"));		# b[0] -> 0000.00xy
	&lea	("ebp",&DWP(4,"ebp"));
	&pcmpeqd("xmm6","xmm6");
	&psrlq	("xmm6",48);			# compose 0xffff<<64|0xffff

	&pshuflw("xmm7","xmm7",0b11011100);	# 0000.00xy -> 0000.0x0y
	&and	("esp",-64);
	&pshufd	("xmm7","xmm7",0b11011100);	# 0000.0x0y -> 000x.000y
	&lea	("ebx",&DWP(0x80,"esp"));

	&movd	("xmm0",&DWP(4*0,"esi"));	# a[0] -> 0000.00xy
	&pshufd	("xmm0","xmm0",0b11001100);	# 0000.00xy -> 00xy.00xy
	&movd	("xmm1",&DWP(4*1,"esi"));	# a[1] -> ...
	&movdqa	(&QWP(0x00,"ebx"),"xmm0");	# offload converted a[0]
	&pmuludq("xmm0","xmm7");		# a[0]*b[0]

	&movd	("xmm2",&DWP(4*2,"esi"));
	&pshufd	("xmm1","xmm1",0b11001100);
	&movdqa	(&QWP(0x10,"ebx"),"xmm1");
	&pmuludq("xmm1","xmm7");		# a[1]*b[0]

	 &movq	("xmm4","xmm0");		# clear upper 64 bits
	 &pslldq("xmm4",6);
	 &paddq	("xmm4","xmm0");
	 &movdqa("xmm5","xmm4");
	 &psrldq("xmm4",10);			# upper 32 bits of a[0]*b[0]
	 &pand	("xmm5","xmm6");		# lower 32 bits of a[0]*b[0]

	# Upper half of a[0]*b[i] is carried into next multiplication
	# iteration, while lower one "participates" in actual reduction.
	# Normally latter is done by accumulating result of multiplication
	# of modulus by "magic" digit, but thanks to special form of modulus
	# and "magic" digit it can be performed only with additions and
	# subtractions (see note in IALU section below). Note that we are
	# not bothered with carry bits, they are accumulated in "flatten"
	# phase after all multiplications and reductions.

	&movd	("xmm3",&DWP(4*3,"esi"));
	&pshufd	("xmm2","xmm2",0b11001100);
	&movdqa	(&QWP(0x20,"ebx"),"xmm2");
	&pmuludq("xmm2","xmm7");		# a[2]*b[0]
	 &paddq	("xmm1","xmm4");		# a[1]*b[0]+hw(a[0]*b[0]), carry
	&movdqa	(&QWP(0x00,"esp"),"xmm1");	# t[0]

	&movd	("xmm0",&DWP(4*4,"esi"));
	&pshufd	("xmm3","xmm3",0b11001100);
	&movdqa	(&QWP(0x30,"ebx"),"xmm3");
	&pmuludq("xmm3","xmm7");		# a[3]*b[0]
	&movdqa	(&QWP(0x10,"esp"),"xmm2");

	&movd	("xmm1",&DWP(4*5,"esi"));
	&pshufd	("xmm0","xmm0",0b11001100);
	&movdqa	(&QWP(0x40,"ebx"),"xmm0");
	&pmuludq("xmm0","xmm7");		# a[4]*b[0]
	 &paddq	("xmm3","xmm5");		# a[3]*b[0]+lw(a[0]*b[0]), reduction step
	&movdqa	(&QWP(0x20,"esp"),"xmm3");

	&movd	("xmm2",&DWP(4*6,"esi"));
	&pshufd	("xmm1","xmm1",0b11001100);
	&movdqa	(&QWP(0x50,"ebx"),"xmm1");
	&pmuludq("xmm1","xmm7");		# a[5]*b[0]
	&movdqa	(&QWP(0x30,"esp"),"xmm0");
	 &pshufd("xmm4","xmm5",0b10110001);	# xmm4 = xmm5<<32, reduction step

	&movd	("xmm3",&DWP(4*7,"esi"));
	&pshufd	("xmm2","xmm2",0b11001100);
	&movdqa	(&QWP(0x60,"ebx"),"xmm2");
	&pmuludq("xmm2","xmm7");		# a[6]*b[0]
	&movdqa	(&QWP(0x40,"esp"),"xmm1");
	 &psubq	("xmm4","xmm5");		# xmm4 = xmm5*0xffffffff, reduction step

	&movd	("xmm0",&DWP(0,"ebp"));		# b[1] -> 0000.00xy
	&pshufd	("xmm3","xmm3",0b11001100);
	&movdqa	(&QWP(0x70,"ebx"),"xmm3");
	&pmuludq("xmm3","xmm7");		# a[7]*b[0]

	&pshuflw("xmm7","xmm0",0b11011100);	# 0000.00xy -> 0000.0x0y
	&movdqa	("xmm0",&QWP(0x00,"ebx"));	# pre-load converted a[0]
	&pshufd	("xmm7","xmm7",0b11011100);	# 0000.0x0y -> 000x.000y

	&mov	("ecx",6);
	&lea	("ebp",&DWP(4,"ebp"));
	&jmp	(&label("madd_sse2"));

&set_label("madd_sse2",16);
	 &paddq	("xmm2","xmm5");		# a[6]*b[i-1]+lw(a[0]*b[i-1]), reduction step [modulo-scheduled]
	 &paddq	("xmm3","xmm4");		# a[7]*b[i-1]+lw(a[0]*b[i-1])*0xffffffff, reduction step [modulo-scheduled]
	&movdqa	("xmm1",&QWP(0x10,"ebx"));
	&pmuludq("xmm0","xmm7");		# a[0]*b[i]
	 &movdqa(&QWP(0x50,"esp"),"xmm2");

	&movdqa	("xmm2",&QWP(0x20,"ebx"));
	&pmuludq("xmm1","xmm7");		# a[1]*b[i]
	 &movdqa(&QWP(0x60,"esp"),"xmm3");
	&paddq	("xmm0",&QWP(0x00,"esp"));

	&movdqa	("xmm3",&QWP(0x30,"ebx"));
	&pmuludq("xmm2","xmm7");		# a[2]*b[i]
	 &movq	("xmm4","xmm0");		# clear upper 64 bits
	 &pslldq("xmm4",6);
	&paddq	("xmm1",&QWP(0x10,"esp"));
	 &paddq	("xmm4","xmm0");
	 &movdqa("xmm5","xmm4");
	 &psrldq("xmm4",10);			# upper 33 bits of a[0]*b[i]+t[0]

	&movdqa	("xmm0",&QWP(0x40,"ebx"));
	&pmuludq("xmm3","xmm7");		# a[3]*b[i]
	 &paddq	("xmm1","xmm4");		# a[1]*b[i]+hw(a[0]*b[i]), carry
	&paddq	("xmm2",&QWP(0x20,"esp"));
	&movdqa	(&QWP(0x00,"esp"),"xmm1");

	&movdqa	("xmm1",&QWP(0x50,"ebx"));
	&pmuludq("xmm0","xmm7");		# a[4]*b[i]
	&paddq	("xmm3",&QWP(0x30,"esp"));
	&movdqa	(&QWP(0x10,"esp"),"xmm2");
	 &pand	("xmm5","xmm6");		# lower 32 bits of a[0]*b[i]

	&movdqa	("xmm2",&QWP(0x60,"ebx"));
	&pmuludq("xmm1","xmm7");		# a[5]*b[i]
	 &paddq	("xmm3","xmm5");		# a[3]*b[i]+lw(a[0]*b[i]), reduction step
	&paddq	("xmm0",&QWP(0x40,"esp"));
	&movdqa	(&QWP(0x20,"esp"),"xmm3");
	 &pshufd("xmm4","xmm5",0b10110001);	# xmm4 = xmm5<<32, reduction step

	&movdqa	("xmm3","xmm7");
	&pmuludq("xmm2","xmm7");		# a[6]*b[i]
	 &movd	("xmm7",&DWP(0,"ebp"));		# b[i++] -> 0000.00xy
	 &lea	("ebp",&DWP(4,"ebp"));
	&paddq	("xmm1",&QWP(0x50,"esp"));
	 &psubq	("xmm4","xmm5");		# xmm4 = xmm5*0xffffffff, reduction step
	&movdqa	(&QWP(0x30,"esp"),"xmm0");
	 &pshuflw("xmm7","xmm7",0b11011100);	# 0000.00xy -> 0000.0x0y

	&pmuludq("xmm3",&QWP(0x70,"ebx"));	# a[7]*b[i]
	 &pshufd("xmm7","xmm7",0b11011100);	# 0000.0x0y -> 000x.000y
	 &movdqa("xmm0",&QWP(0x00,"ebx"));	# pre-load converted a[0]
	&movdqa	(&QWP(0x40,"esp"),"xmm1");
	&paddq	("xmm2",&QWP(0x60,"esp"));

	&dec	("ecx");
	&jnz	(&label("madd_sse2"));

	 &paddq	("xmm2","xmm5");		# a[6]*b[6]+lw(a[0]*b[6]), reduction step [modulo-scheduled]
	 &paddq	("xmm3","xmm4");		# a[7]*b[6]+lw(a[0]*b[6])*0xffffffff, reduction step [modulo-scheduled]
	&movdqa	("xmm1",&QWP(0x10,"ebx"));
	&pmuludq("xmm0","xmm7");		# a[0]*b[7]
	 &movdqa(&QWP(0x50,"esp"),"xmm2");

	&movdqa	("xmm2",&QWP(0x20,"ebx"));
	&pmuludq("xmm1","xmm7");		# a[1]*b[7]
	 &movdqa(&QWP(0x60,"esp"),"xmm3");
	&paddq	("xmm0",&QWP(0x00,"esp"));

	&movdqa	("xmm3",&QWP(0x30,"ebx"));
	&pmuludq("xmm2","xmm7");		# a[2]*b[7]
	 &movq	("xmm4","xmm0");		# clear upper 64 bits
	 &pslldq("xmm4",6);
	&paddq	("xmm1",&QWP(0x10,"esp"));
	 &paddq	("xmm4","xmm0");
	 &movdqa("xmm5","xmm4");
	 &psrldq("xmm4",10);			# upper 33 bits of a[0]*b[i]+t[0]

	&movdqa	("xmm0",&QWP(0x40,"ebx"));
	&pmuludq("xmm3","xmm7");		# a[3]*b[7]
	 &paddq	("xmm1","xmm4");		# a[1]*b[7]+hw(a[0]*b[7]), carry
	&paddq	("xmm2",&QWP(0x20,"esp"));
	&movdqa	(&QWP(0x00,"esp"),"xmm1");

	&movdqa	("xmm1",&QWP(0x50,"ebx"));
	&pmuludq("xmm0","xmm7");		# a[4]*b[7]
	&paddq	("xmm3",&QWP(0x30,"esp"));
	&movdqa	(&QWP(0x10,"esp"),"xmm2");
	 &pand	("xmm5","xmm6");		# lower 32 bits of a[0]*b[i]

	&movdqa	("xmm2",&QWP(0x60,"ebx"));
	&pmuludq("xmm1","xmm7");		# a[5]*b[7]
	 &paddq	("xmm3","xmm5");		# reduction step
	&paddq	("xmm0",&QWP(0x40,"esp"));
	&movdqa	(&QWP(0x20,"esp"),"xmm3");
	 &pshufd("xmm4","xmm5",0b10110001);	# xmm4 = xmm5<<32, reduction step

	&movdqa	("xmm3",&QWP(0x70,"ebx"));
	&pmuludq("xmm2","xmm7");		# a[6]*b[7]
	&paddq	("xmm1",&QWP(0x50,"esp"));
	 &psubq	("xmm4","xmm5");		# xmm4 = xmm5*0xffffffff, reduction step
	&movdqa	(&QWP(0x30,"esp"),"xmm0");

	&pmuludq("xmm3","xmm7");		# a[7]*b[7]
	&pcmpeqd("xmm7","xmm7");
	&movdqa	("xmm0",&QWP(0x00,"esp"));
	&pslldq	("xmm7",8);
	&movdqa	(&QWP(0x40,"esp"),"xmm1");
	&paddq	("xmm2",&QWP(0x60,"esp"));

	 &paddq	("xmm2","xmm5");		# a[6]*b[7]+lw(a[0]*b[7]), reduction step
	 &paddq	("xmm3","xmm4");		# a[6]*b[7]+lw(a[0]*b[7])*0xffffffff, reduction step
	 &movdqa(&QWP(0x50,"esp"),"xmm2");
	 &movdqa(&QWP(0x60,"esp"),"xmm3");

	&movdqa	("xmm1",&QWP(0x10,"esp"));
	&movdqa	("xmm2",&QWP(0x20,"esp"));
	&movdqa	("xmm3",&QWP(0x30,"esp"));

	&movq	("xmm4","xmm0");		# "flatten"
	&pand	("xmm0","xmm7");
	&xor	("ebp","ebp");
	&pslldq	("xmm4",6);
	 &movq	("xmm5","xmm1");
	&paddq	("xmm0","xmm4");
	 &pand	("xmm1","xmm7");
	&psrldq	("xmm0",6);
	&movd	("eax","xmm0");
	&psrldq	("xmm0",4);

	&paddq	("xmm5","xmm0");
	&movdqa	("xmm0",&QWP(0x40,"esp"));
	&sub	("eax",-1);			# start subtracting modulus,
						# this is used to determine
						# if result is larger/smaller
						# than modulus (see below)
	&pslldq	("xmm5",6);
	 &movq	("xmm4","xmm2");
	&paddq	("xmm1","xmm5");
	 &pand	("xmm2","xmm7");
	&psrldq	("xmm1",6);
	&mov	(&DWP(4*0,"edi"),"eax");
	&movd	("eax","xmm1");
	&psrldq	("xmm1",4);

	&paddq	("xmm4","xmm1");
	&movdqa	("xmm1",&QWP(0x50,"esp"));
	&sbb	("eax",-1);
	&pslldq	("xmm4",6);
	 &movq	("xmm5","xmm3");
	&paddq	("xmm2","xmm4");
	 &pand	("xmm3","xmm7");
	&psrldq	("xmm2",6);
	&mov	(&DWP(4*1,"edi"),"eax");
	&movd	("eax","xmm2");
	&psrldq	("xmm2",4);

	&paddq	("xmm5","xmm2");
	&movdqa	("xmm2",&QWP(0x60,"esp"));
	&sbb	("eax",-1);
	&pslldq	("xmm5",6);
	 &movq	("xmm4","xmm0");
	&paddq	("xmm3","xmm5");
	 &pand	("xmm0","xmm7");
	&psrldq	("xmm3",6);
	&mov	(&DWP(4*2,"edi"),"eax");
	&movd	("eax","xmm3");
	&psrldq	("xmm3",4);

	&paddq	("xmm4","xmm3");
	&sbb	("eax",0);
	&pslldq	("xmm4",6);
	 &movq	("xmm5","xmm1");
	&paddq	("xmm0","xmm4");
	 &pand	("xmm1","xmm7");
	&psrldq	("xmm0",6);
	&mov	(&DWP(4*3,"edi"),"eax");
	&movd	("eax","xmm0");
	&psrldq	("xmm0",4);

	&paddq	("xmm5","xmm0");
	&sbb	("eax",0);
	&pslldq	("xmm5",6);
	 &movq	("xmm4","xmm2");
	&paddq	("xmm1","xmm5");
	 &pand	("xmm2","xmm7");
	&psrldq	("xmm1",6);
	&movd	("ebx","xmm1");
	&psrldq	("xmm1",4);
	&mov	("esp","edx");

	&paddq	("xmm4","xmm1");
	&pslldq	("xmm4",6);
	&paddq	("xmm2","xmm4");
	&psrldq	("xmm2",6);
	&movd	("ecx","xmm2");
	&psrldq	("xmm2",4);
	&sbb	("ebx",0);
	&movd	("edx","xmm2");
	&pextrw	("esi","xmm2",2);		# top-most overflow bit
	&sbb	("ecx",1);
	&sbb	("edx",-1);
	&sbb	("esi",0);			# borrow from subtraction

	# Final step is "if result > mod, subtract mod", and at this point
	# we have result - mod written to output buffer, as well as borrow
	# bit from this subtraction, and if borrow bit is set, we add
	# modulus back.
	#
	# Note that because mod has special form, i.e. consists of
	# 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	# assigning borrow bit to one register, %ebp, and its negative
	# to another, %esi. But we started by calculating %esi...

	&sub	("ebp","esi");
	&add	(&DWP(4*0,"edi"),"esi");	# add modulus or zero
	&adc	(&DWP(4*1,"edi"),"esi");
	&adc	(&DWP(4*2,"edi"),"esi");
	&adc	(&DWP(4*3,"edi"),0);
	&adc	("eax",0);
	&adc	("ebx",0);
	&mov	(&DWP(4*4,"edi"),"eax");
	&adc	("ecx","ebp");
	&mov	(&DWP(4*5,"edi"),"ebx");
	&adc	("edx","esi");
	&mov	(&DWP(4*6,"edi"),"ecx");
	&mov	(&DWP(4*7,"edi"),"edx");

	&ret	();

&set_label("mul_mont_ialu",16);			}

	########################################
	# IALU code path suitable for all CPUs.
	########################################
	# stack layout:
	# +------------------------------------+< %esp
	# | 8 32-bit temporary words, accessed |
	# | as circular buffer                 |
	# .                                    .
	# .                                    .
	# +------------------------------------+< +32
	# | offloaded destination pointer      |
	# +------------------------------------+
	# | unused                             |
	# +------------------------------------+< +40
	&sub	("esp",10*4);

	&mov	("eax",&DWP(0*4,"esi"));		# a[0]
	&mov	("ebx",&DWP(0*4,"ebp"));		# b[0]
	&mov	(&DWP(8*4,"esp"),"edi");		# off-load dst ptr

	&mul	("ebx");				# a[0]*b[0]
	&mov	(&DWP(0*4,"esp"),"eax");		# t[0]
	&mov	("eax",&DWP(1*4,"esi"));
	&mov	("ecx","edx")

	&mul	("ebx");				# a[1]*b[0]
	&add	("ecx","eax");
	&mov	("eax",&DWP(2*4,"esi"));
	&adc	("edx",0);
	&mov	(&DWP(1*4,"esp"),"ecx");		# t[1]
	&mov	("ecx","edx");

	&mul	("ebx");				# a[2]*b[0]
	&add	("ecx","eax");
	&mov	("eax",&DWP(3*4,"esi"));
	&adc	("edx",0);
	&mov	(&DWP(2*4,"esp"),"ecx");		# t[2]
	&mov	("ecx","edx");

	&mul	("ebx");				# a[3]*b[0]
	&add	("ecx","eax");
	&mov	("eax",&DWP(4*4,"esi"));
	&adc	("edx",0);
	&mov	(&DWP(3*4,"esp"),"ecx");		# t[3]
	&mov	("ecx","edx");

	&mul	("ebx");				# a[4]*b[0]
	&add	("ecx","eax");
	&mov	("eax",&DWP(5*4,"esi"));
	&adc	("edx",0);
	&mov	(&DWP(4*4,"esp"),"ecx");		# t[4]
	&mov	("ecx","edx");

	&mul	("ebx");				# a[5]*b[0]
	&add	("ecx","eax");
	&mov	("eax",&DWP(6*4,"esi"));
	&adc	("edx",0);
	&mov	(&DWP(5*4,"esp"),"ecx");		# t[5]
	&mov	("ecx","edx");

	&mul	("ebx");				# a[6]*b[0]
	&add	("ecx","eax");
	&mov	("eax",&DWP(7*4,"esi"));
	&adc	("edx",0);
	&mov	(&DWP(6*4,"esp"),"ecx");		# t[6]
	&mov	("ecx","edx");

	&xor	("edi","edi");				# initial top-most carry
	&mul	("ebx");				# a[7]*b[0]
	&add	("ecx","eax");				# t[7]
	&mov	("eax",&DWP(0*4,"esp"));		# t[0]
	&adc	("edx",0);				# t[8]

for ($i=0;$i<7;$i++) {
	my $j=$i+1;

	# Reduction iteration is normally performed by accumulating
	# result of multiplication of modulus by "magic" digit [and
	# omitting least significant word, which is guaranteed to
	# be 0], but thanks to special form of modulus and "magic"
	# digit being equal to least significant word, it can be
	# performed with additions and subtractions alone. Indeed:
	#
	#        ffff.0001.0000.0000.0000.ffff.ffff.ffff
	# *                                         abcd
	# + xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.abcd
	#
	# Now observing that ff..ff*x = (2^n-1)*x = 2^n*x-x, we
	# rewrite above as:
	#
	#   xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.abcd
	# + abcd.0000.abcd.0000.0000.abcd.0000.0000.0000
	# -      abcd.0000.0000.0000.0000.0000.0000.abcd
	#
	# or marking redundant operations:
	#
	#   xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.xxxx.----
	# + abcd.0000.abcd.0000.0000.abcd.----.----.----
	# -      abcd.----.----.----.----.----.----.----

	&add	(&DWP((($i+3)%8)*4,"esp"),"eax");	# t[3]+=t[0]
	&adc	(&DWP((($i+4)%8)*4,"esp"),0);		# t[4]+=0
	&adc	(&DWP((($i+5)%8)*4,"esp"),0);		# t[5]+=0
	&adc	(&DWP((($i+6)%8)*4,"esp"),"eax");	# t[6]+=t[0]
	&adc	("ecx",0);				# t[7]+=0
	&adc	("edx","eax");				# t[8]+=t[0]
	&adc	("edi",0);				# top-most carry
	 &mov	("ebx",&DWP($j*4,"ebp"));		# b[i]
	&sub	("ecx","eax");				# t[7]-=t[0]
	 &mov	("eax",&DWP(0*4,"esi"));		# a[0]
	&sbb	("edx",0);				# t[8]-=0
	&mov	(&DWP((($i+7)%8)*4,"esp"),"ecx");
	&sbb	("edi",0);				# top-most carry,
							# keep in mind that
							# netto result is
							# *addition* of value
							# with (abcd<<32)-abcd
							# on top, so that
							# underflow is
							# impossible, because
							# (abcd<<32)-abcd
							# doesn't underflow
	&mov	(&DWP((($i+8)%8)*4,"esp"),"edx");

	&mul	("ebx");				# a[0]*b[i]
	&add	("eax",&DWP((($j+0)%8)*4,"esp"));
	&adc	("edx",0);
	&mov	(&DWP((($j+0)%8)*4,"esp"),"eax");
	&mov	("eax",&DWP(1*4,"esi"));
	&mov	("ecx","edx")

	&mul	("ebx");				# a[1]*b[i]
	&add	("ecx",&DWP((($j+1)%8)*4,"esp"));
	&adc	("edx",0);
	&add	("ecx","eax");
	&adc	("edx",0);
	&mov	("eax",&DWP(2*4,"esi"));
	&mov	(&DWP((($j+1)%8)*4,"esp"),"ecx");
	&mov	("ecx","edx");

	&mul	("ebx");				# a[2]*b[i]
	&add	("ecx",&DWP((($j+2)%8)*4,"esp"));
	&adc	("edx",0);
	&add	("ecx","eax");
	&adc	("edx",0);
	&mov	("eax",&DWP(3*4,"esi"));
	&mov	(&DWP((($j+2)%8)*4,"esp"),"ecx");
	&mov	("ecx","edx");

	&mul	("ebx");				# a[3]*b[i]
	&add	("ecx",&DWP((($j+3)%8)*4,"esp"));
	&adc	("edx",0);
	&add	("ecx","eax");
	&adc	("edx",0);
	&mov	("eax",&DWP(4*4,"esi"));
	&mov	(&DWP((($j+3)%8)*4,"esp"),"ecx");
	&mov	("ecx","edx");

	&mul	("ebx");				# a[4]*b[i]
	&add	("ecx",&DWP((($j+4)%8)*4,"esp"));
	&adc	("edx",0);
	&add	("ecx","eax");
	&adc	("edx",0);
	&mov	("eax",&DWP(5*4,"esi"));
	&mov	(&DWP((($j+4)%8)*4,"esp"),"ecx");
	&mov	("ecx","edx");

	&mul	("ebx");				# a[5]*b[i]
	&add	("ecx",&DWP((($j+5)%8)*4,"esp"));
	&adc	("edx",0);
	&add	("ecx","eax");
	&adc	("edx",0);
	&mov	("eax",&DWP(6*4,"esi"));
	&mov	(&DWP((($j+5)%8)*4,"esp"),"ecx");
	&mov	("ecx","edx");

	&mul	("ebx");				# a[6]*b[i]
	&add	("ecx",&DWP((($j+6)%8)*4,"esp"));
	&adc	("edx",0);
	&add	("ecx","eax");
	&adc	("edx",0);
	&mov	("eax",&DWP(7*4,"esi"));
	&mov	(&DWP((($j+6)%8)*4,"esp"),"ecx");
	&mov	("ecx","edx");

	&mul	("ebx");				# a[7]*b[i]
	&add	("ecx",&DWP((($j+7)%8)*4,"esp"));
	&adc	("edx",0);
	&add	("ecx","eax");				# t[7]
	&mov	("eax",&DWP((($j+0)%8)*4,"esp"));	# t[0]
	&adc	("edx","edi");				# t[8]
	&mov	("edi",0);
	&adc	("edi",0);				# top-most carry
}
	&mov	("ebp",&DWP(8*4,"esp"));		# restore dst ptr
	&xor	("esi","esi");
	my $j=$i+1;

	# last multiplication-less reduction
	&add	(&DWP((($i+3)%8)*4,"esp"),"eax");	# t[3]+=t[0]
	&adc	(&DWP((($i+4)%8)*4,"esp"),0);		# t[4]+=0
	&adc	(&DWP((($i+5)%8)*4,"esp"),0);		# t[5]+=0
	&adc	(&DWP((($i+6)%8)*4,"esp"),"eax");	# t[6]+=t[0]
	&adc	("ecx",0);				# t[7]+=0
	&adc	("edx","eax");				# t[8]+=t[0]
	&adc	("edi",0);				# top-most carry
	 &mov	("ebx",&DWP((($j+1)%8)*4,"esp"));
	&sub	("ecx","eax");				# t[7]-=t[0]
	 &mov	("eax",&DWP((($j+0)%8)*4,"esp"));
	&sbb	("edx",0);				# t[8]-=0
	&mov	(&DWP((($i+7)%8)*4,"esp"),"ecx");
	&sbb	("edi",0);				# top-most carry
	&mov	(&DWP((($i+8)%8)*4,"esp"),"edx");

	# Final step is "if result > mod, subtract mod", but we do it
	# "other way around", namely write result - mod to output buffer
	# and if subtraction borrowed, add modulus back.

	&mov	("ecx",&DWP((($j+2)%8)*4,"esp"));
	&sub	("eax",-1);
	&mov	("edx",&DWP((($j+3)%8)*4,"esp"));
	&sbb	("ebx",-1);
	&mov	(&DWP(0*4,"ebp"),"eax");
	&sbb	("ecx",-1);
	&mov	(&DWP(1*4,"ebp"),"ebx");
	&sbb	("edx",0);
	&mov	(&DWP(2*4,"ebp"),"ecx");
	&mov	(&DWP(3*4,"ebp"),"edx");

	&mov	("eax",&DWP((($j+4)%8)*4,"esp"));
	&mov	("ebx",&DWP((($j+5)%8)*4,"esp"));
	&mov	("ecx",&DWP((($j+6)%8)*4,"esp"));
	&sbb	("eax",0);
	&mov	("edx",&DWP((($j+7)%8)*4,"esp"));
	&sbb	("ebx",0);
	&sbb	("ecx",1);
	&sbb	("edx",-1);
	&sbb	("edi",0);

	# Note that because mod has special form, i.e. consists of
	# 0xffffffff, 1 and 0s, we can conditionally synthesize it by
	# assigning borrow bit to one register, %ebp, and its negative
	# to another, %esi. But we started by calculating %esi...

	&sub	("esi","edi");
	&add	(&DWP(0*4,"ebp"),"edi");		# add modulus or zero
	&adc	(&DWP(1*4,"ebp"),"edi");
	&adc	(&DWP(2*4,"ebp"),"edi");
	&adc	(&DWP(3*4,"ebp"),0);
	&adc	("eax",0);
	&adc	("ebx",0);
	&mov	(&DWP(4*4,"ebp"),"eax");
	&adc	("ecx","esi");
	&mov	(&DWP(5*4,"ebp"),"ebx");
	&adc	("edx","edi");
	&mov	(&DWP(6*4,"ebp"),"ecx");
	&mov	("edi","ebp");				# fulfill contract
	&mov	(&DWP(7*4,"ebp"),"edx");

	&add	("esp",10*4);
	&ret	();
&function_end_B("_ecp_nistz256_mul_mont");

########################################################################
# void ecp_nistz256_scatter_w5(void *edi,const P256_POINT *esi,
#					 int ebp);
&function_begin("ecp_nistz256_scatter_w5");
	&mov	("edi",&wparam(0));
	&mov	("esi",&wparam(1));
	&mov	("ebp",&wparam(2));

	&lea	("edi",&DWP(128-4,"edi","ebp",4));
	&mov	("ebp",96/16);
&set_label("scatter_w5_loop");
	&mov	("eax",&DWP(0,"esi"));
	&mov	("ebx",&DWP(4,"esi"));
	&mov	("ecx",&DWP(8,"esi"));
	&mov	("edx",&DWP(12,"esi"));
	&lea	("esi",&DWP(16,"esi"));
	&mov	(&DWP(64*0-128,"edi"),"eax");
	&mov	(&DWP(64*1-128,"edi"),"ebx");
	&mov	(&DWP(64*2-128,"edi"),"ecx");
	&mov	(&DWP(64*3-128,"edi"),"edx");
	&lea	("edi",&DWP(64*4,"edi"));
	&dec	("ebp");
	&jnz	(&label("scatter_w5_loop"));
&function_end("ecp_nistz256_scatter_w5");

########################################################################
# void ecp_nistz256_gather_w5(P256_POINT *edi,const void *esi,
#					      int ebp);
&function_begin("ecp_nistz256_gather_w5");
	&mov	("esi",&wparam(1));
	&mov	("ebp",&wparam(2));

	&lea	("esi",&DWP(0,"esi","ebp",4));
	&neg	("ebp");
	&sar	("ebp",31);
	&mov	("edi",&wparam(0));
	&lea	("esi",&DWP(0,"esi","ebp",4));

    for($i=0;$i<24;$i+=4) {
	&mov	("eax",&DWP(64*($i+0),"esi"));
	&mov	("ebx",&DWP(64*($i+1),"esi"));
	&mov	("ecx",&DWP(64*($i+2),"esi"));
	&mov	("edx",&DWP(64*($i+3),"esi"));
	&and	("eax","ebp");
	&and	("ebx","ebp");
	&and	("ecx","ebp");
	&and	("edx","ebp");
	&mov	(&DWP(4*($i+0),"edi"),"eax");
	&mov	(&DWP(4*($i+1),"edi"),"ebx");
	&mov	(&DWP(4*($i+2),"edi"),"ecx");
	&mov	(&DWP(4*($i+3),"edi"),"edx");
    }
&function_end("ecp_nistz256_gather_w5");

########################################################################
# void ecp_nistz256_scatter_w7(void *edi,const P256_POINT_AFFINE *esi,
#					 int ebp);
&function_begin("ecp_nistz256_scatter_w7");
	&mov	("edi",&wparam(0));
	&mov	("esi",&wparam(1));
	&mov	("ebp",&wparam(2));

	&lea	("edi",&DWP(0,"edi","ebp"));
	&mov	("ebp",64/4);
&set_label("scatter_w7_loop");
	&mov	("eax",&DWP(0,"esi"));
	&lea	("esi",&DWP(4,"esi"));
	&mov	(&BP(64*0,"edi"),"al");
	&mov	(&BP(64*1,"edi"),"ah");
	&shr	("eax",16);
	&mov	(&BP(64*2,"edi"),"al");
	&mov	(&BP(64*3,"edi"),"ah");
	&lea	("edi",&DWP(64*4,"edi"));
	&dec	("ebp");
	&jnz	(&label("scatter_w7_loop"));
&function_end("ecp_nistz256_scatter_w7");

########################################################################
# void ecp_nistz256_gather_w7(P256_POINT_AFFINE *edi,const void *esi,
#						     int ebp);
&function_begin("ecp_nistz256_gather_w7");
	&mov	("esi",&wparam(1));
	&mov	("ebp",&wparam(2));

	&add	("esi","ebp");
	&neg	("ebp"),
	&sar	("ebp",31);
	&mov	("edi",&wparam(0));
	&lea	("esi",&DWP(0,"esi","ebp"));

    for($i=0;$i<64;$i+=4) {
	&movz	("eax",&BP(64*($i+0),"esi"));
	&movz	("ebx",&BP(64*($i+1),"esi"));
	&movz	("ecx",&BP(64*($i+2),"esi"));
	&and	("eax","ebp");
	&movz	("edx",&BP(64*($i+3),"esi"));
	&and	("ebx","ebp");
	&mov	(&BP($i+0,"edi"),"al");
	&and	("ecx","ebp");
	&mov	(&BP($i+1,"edi"),"bl");
	&and	("edx","ebp");
	&mov	(&BP($i+2,"edi"),"cl");
	&mov	(&BP($i+3,"edi"),"dl");
    }
&function_end("ecp_nistz256_gather_w7");

########################################################################
# following subroutines are "literal" implementation of those found in
# ecp_nistz256.c
#
########################################################################
# void ecp_nistz256_point_double(P256_POINT *out,const P256_POINT *inp);
#
&static_label("point_double_shortcut");
&function_begin("ecp_nistz256_point_double");
{   my ($S,$M,$Zsqr,$in_x,$tmp0)=map(32*$_,(0..4));

	&mov	("esi",&wparam(1));

	# above map() describes stack layout with 5 temporary
	# 256-bit vectors on top, then we take extra word for
	# OPENSSL_ia32cap_P copy.
	&stack_push(8*5+1);
						if ($sse2) {
	&call	("_picup_eax");
    &set_label("pic");
	&picmeup("edx","OPENSSL_ia32cap_P","eax",&label("pic"));
	&mov	("ebp",&DWP(0,"edx"));		}

&set_label("point_double_shortcut");
	&mov	("eax",&DWP(0,"esi"));		# copy in_x
	&mov	("ebx",&DWP(4,"esi"));
	&mov	("ecx",&DWP(8,"esi"));
	&mov	("edx",&DWP(12,"esi"));
	&mov	(&DWP($in_x+0,"esp"),"eax");
	&mov	(&DWP($in_x+4,"esp"),"ebx");
	&mov	(&DWP($in_x+8,"esp"),"ecx");
	&mov	(&DWP($in_x+12,"esp"),"edx");
	&mov	("eax",&DWP(16,"esi"));
	&mov	("ebx",&DWP(20,"esi"));
	&mov	("ecx",&DWP(24,"esi"));
	&mov	("edx",&DWP(28,"esi"));
	&mov	(&DWP($in_x+16,"esp"),"eax");
	&mov	(&DWP($in_x+20,"esp"),"ebx");
	&mov	(&DWP($in_x+24,"esp"),"ecx");
	&mov	(&DWP($in_x+28,"esp"),"edx");
	&mov	(&DWP(32*5,"esp"),"ebp");	# OPENSSL_ia32cap_P copy

	&lea	("ebp",&DWP(32,"esi"));
	&lea	("esi",&DWP(32,"esi"));
	&lea	("edi",&DWP($S,"esp"));
	&call	("_ecp_nistz256_add");		# p256_mul_by_2(S, in_y);

	&mov	("eax",&DWP(32*5,"esp"));	# OPENSSL_ia32cap_P copy
	&mov	("esi",64);
	&add	("esi",&wparam(1));
	&lea	("edi",&DWP($Zsqr,"esp"));
	&mov	("ebp","esi");
	&call	("_ecp_nistz256_mul_mont");	# p256_sqr_mont(Zsqr, in_z);

	&mov	("eax",&DWP(32*5,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($S,"esp"));
	&lea	("ebp",&DWP($S,"esp"));
	&lea	("edi",&DWP($S,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_sqr_mont(S, S);

	&mov	("eax",&DWP(32*5,"esp"));	# OPENSSL_ia32cap_P copy
	&mov	("ebp",&wparam(1));
	&lea	("esi",&DWP(32,"ebp"));
	&lea	("ebp",&DWP(64,"ebp"));
	&lea	("edi",&DWP($tmp0,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(tmp0, in_z, in_y);

	&lea	("esi",&DWP($in_x,"esp"));
	&lea	("ebp",&DWP($Zsqr,"esp"));
	&lea	("edi",&DWP($M,"esp"));
	&call	("_ecp_nistz256_add");		# p256_add(M, in_x, Zsqr);

	&mov	("edi",64);
	&lea	("esi",&DWP($tmp0,"esp"));
	&lea	("ebp",&DWP($tmp0,"esp"));
	&add	("edi",&wparam(0));
	&call	("_ecp_nistz256_add");		# p256_mul_by_2(res_z, tmp0);

	&lea	("esi",&DWP($in_x,"esp"));
	&lea	("ebp",&DWP($Zsqr,"esp"));
	&lea	("edi",&DWP($Zsqr,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(Zsqr, in_x, Zsqr);

	&mov	("eax",&DWP(32*5,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($S,"esp"));
	&lea	("ebp",&DWP($S,"esp"));
	&lea	("edi",&DWP($tmp0,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_sqr_mont(tmp0, S);

	&mov	("eax",&DWP(32*5,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($M,"esp"));
	&lea	("ebp",&DWP($Zsqr,"esp"));
	&lea	("edi",&DWP($M,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(M, M, Zsqr);

	&mov	("edi",32);
	&lea	("esi",&DWP($tmp0,"esp"));
	&add	("edi",&wparam(0));
	&call	("_ecp_nistz256_div_by_2");	# p256_div_by_2(res_y, tmp0);

	&lea	("esi",&DWP($M,"esp"));
	&lea	("ebp",&DWP($M,"esp"));
	&lea	("edi",&DWP($tmp0,"esp"));
	&call	("_ecp_nistz256_add");		# 1/2 p256_mul_by_3(M, M);

	&mov	("eax",&DWP(32*5,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in_x,"esp"));
	&lea	("ebp",&DWP($S,"esp"));
	&lea	("edi",&DWP($S,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(S, S, in_x);

	&lea	("esi",&DWP($tmp0,"esp"));
	&lea	("ebp",&DWP($M,"esp"));
	&lea	("edi",&DWP($M,"esp"));
	&call	("_ecp_nistz256_add");		# 2/2 p256_mul_by_3(M, M);

	&lea	("esi",&DWP($S,"esp"));
	&lea	("ebp",&DWP($S,"esp"));
	&lea	("edi",&DWP($tmp0,"esp"));
	&call	("_ecp_nistz256_add");		# p256_mul_by_2(tmp0, S);

	&mov	("eax",&DWP(32*5,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($M,"esp"));
	&lea	("ebp",&DWP($M,"esp"));
	&mov	("edi",&wparam(0));
	&call	("_ecp_nistz256_mul_mont");	# p256_sqr_mont(res_x, M);

	&mov	("esi","edi");			# %edi is still res_x here
	&lea	("ebp",&DWP($tmp0,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(res_x, res_x, tmp0);

	&lea	("esi",&DWP($S,"esp"));
	&mov	("ebp","edi");			# %edi is still res_x
	&lea	("edi",&DWP($S,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(S, S, res_x);

	&mov	("eax",&DWP(32*5,"esp"));	# OPENSSL_ia32cap_P copy
	&mov	("esi","edi");			# %edi is still &S
	&lea	("ebp",&DWP($M,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(S, S, M);

	&mov	("ebp",32);
	&lea	("esi",&DWP($S,"esp"));
	&add	("ebp",&wparam(0));
	&mov	("edi","ebp");
	&call	("_ecp_nistz256_sub");		# p256_sub(res_y, S, res_y);

	&stack_pop(8*5+1);
} &function_end("ecp_nistz256_point_double");

########################################################################
# void ecp_nistz256_point_add(P256_POINT *out,const P256_POINT *in1,
#					      const P256_POINT *in2);
&function_begin("ecp_nistz256_point_add");
{   my ($res_x,$res_y,$res_z,
	$in1_x,$in1_y,$in1_z,
	$in2_x,$in2_y,$in2_z,
	$H,$Hsqr,$R,$Rsqr,$Hcub,
	$U1,$U2,$S1,$S2)=map(32*$_,(0..17));
    my ($Z1sqr, $Z2sqr) = ($Hsqr, $Rsqr);

	&mov	("esi",&wparam(2));

	# above map() describes stack layout with 18 temporary
	# 256-bit vectors on top, then we take extra words for
	# !in1infty, !in2infty, result of check for zero and
	# OPENSSL_ia32cap_P copy. [one unused word for padding]
	&stack_push(8*18+5);
						if ($sse2) {
	&call	("_picup_eax");
    &set_label("pic");
	&picmeup("edx","OPENSSL_ia32cap_P","eax",&label("pic"));
	&mov	("ebp",&DWP(0,"edx"));		}

	&lea	("edi",&DWP($in2_x,"esp"));
    for($i=0;$i<96;$i+=16) {
	&mov	("eax",&DWP($i+0,"esi"));	# copy in2
	&mov	("ebx",&DWP($i+4,"esi"));
	&mov	("ecx",&DWP($i+8,"esi"));
	&mov	("edx",&DWP($i+12,"esi"));
	&mov	(&DWP($i+0,"edi"),"eax");
	&mov	(&DWP(32*18+12,"esp"),"ebp")	if ($i==0);
	&mov	("ebp","eax")			if ($i==64);
	&or	("ebp","eax")			if ($i>64);
	&mov	(&DWP($i+4,"edi"),"ebx");
	&or	("ebp","ebx")			if ($i>=64);
	&mov	(&DWP($i+8,"edi"),"ecx");
	&or	("ebp","ecx")			if ($i>=64);
	&mov	(&DWP($i+12,"edi"),"edx");
	&or	("ebp","edx")			if ($i>=64);
    }
	&xor	("eax","eax");
	&mov	("esi",&wparam(1));
	&sub	("eax","ebp");
	&or	("ebp","eax");
	&sar	("ebp",31);
	&mov	(&DWP(32*18+4,"esp"),"ebp");	# !in2infty

	&lea	("edi",&DWP($in1_x,"esp"));
    for($i=0;$i<96;$i+=16) {
	&mov	("eax",&DWP($i+0,"esi"));	# copy in1
	&mov	("ebx",&DWP($i+4,"esi"));
	&mov	("ecx",&DWP($i+8,"esi"));
	&mov	("edx",&DWP($i+12,"esi"));
	&mov	(&DWP($i+0,"edi"),"eax");
	&mov	("ebp","eax")			if ($i==64);
	&or	("ebp","eax")			if ($i>64);
	&mov	(&DWP($i+4,"edi"),"ebx");
	&or	("ebp","ebx")			if ($i>=64);
	&mov	(&DWP($i+8,"edi"),"ecx");
	&or	("ebp","ecx")			if ($i>=64);
	&mov	(&DWP($i+12,"edi"),"edx");
	&or	("ebp","edx")			if ($i>=64);
    }
	&xor	("eax","eax");
	&sub	("eax","ebp");
	&or	("ebp","eax");
	&sar	("ebp",31);
	&mov	(&DWP(32*18+0,"esp"),"ebp");	# !in1infty

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in2_z,"esp"));
	&lea	("ebp",&DWP($in2_z,"esp"));
	&lea	("edi",&DWP($Z2sqr,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_sqr_mont(Z2sqr, in2_z);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in1_z,"esp"));
	&lea	("ebp",&DWP($in1_z,"esp"));
	&lea	("edi",&DWP($Z1sqr,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_sqr_mont(Z1sqr, in1_z);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($Z2sqr,"esp"));
	&lea	("ebp",&DWP($in2_z,"esp"));
	&lea	("edi",&DWP($S1,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(S1, Z2sqr, in2_z);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($Z1sqr,"esp"));
	&lea	("ebp",&DWP($in1_z,"esp"));
	&lea	("edi",&DWP($S2,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(S2, Z1sqr, in1_z);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in1_y,"esp"));
	&lea	("ebp",&DWP($S1,"esp"));
	&lea	("edi",&DWP($S1,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(S1, S1, in1_y);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in2_y,"esp"));
	&lea	("ebp",&DWP($S2,"esp"));
	&lea	("edi",&DWP($S2,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(S2, S2, in2_y);

	&lea	("esi",&DWP($S2,"esp"));
	&lea	("ebp",&DWP($S1,"esp"));
	&lea	("edi",&DWP($R,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(R, S2, S1);

	&or	("ebx","eax");			# see if result is zero
	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&or	("ebx","ecx");
	&or	("ebx","edx");
	&or	("ebx",&DWP(0,"edi"));
	&or	("ebx",&DWP(4,"edi"));
	 &lea	("esi",&DWP($in1_x,"esp"));
	&or	("ebx",&DWP(8,"edi"));
	 &lea	("ebp",&DWP($Z2sqr,"esp"));
	&or	("ebx",&DWP(12,"edi"));
	 &lea	("edi",&DWP($U1,"esp"));
	&mov	(&DWP(32*18+8,"esp"),"ebx");

	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(U1, in1_x, Z2sqr);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in2_x,"esp"));
	&lea	("ebp",&DWP($Z1sqr,"esp"));
	&lea	("edi",&DWP($U2,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(U2, in2_x, Z1sqr);

	&lea	("esi",&DWP($U2,"esp"));
	&lea	("ebp",&DWP($U1,"esp"));
	&lea	("edi",&DWP($H,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(H, U2, U1);

	&or	("eax","ebx");			# see if result is zero
	&or	("eax","ecx");
	&or	("eax","edx");
	&or	("eax",&DWP(0,"edi"));
	&or	("eax",&DWP(4,"edi"));
	&or	("eax",&DWP(8,"edi"));
	&or	("eax",&DWP(12,"edi"));

	&data_byte(0x3e);			# predict taken
	&jnz	(&label("add_proceed"));	# is_equal(U1,U2)?

	&mov	("eax",&DWP(32*18+0,"esp"));
	&and	("eax",&DWP(32*18+4,"esp"));
	&mov	("ebx",&DWP(32*18+8,"esp"));
	&jz	(&label("add_proceed"));	# (in1infty || in2infty)?
	&test	("ebx","ebx");
	&jz	(&label("add_double"));		# is_equal(S1,S2)?

	&mov	("edi",&wparam(0));
	&xor	("eax","eax");
	&mov	("ecx",96/4);
	&data_byte(0xfc,0xf3,0xab);		# cld; stosd
	&jmp	(&label("add_done"));

&set_label("add_double",16);
	&mov	("esi",&wparam(1));
	&mov	("ebp",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&add	("esp",4*((8*18+5)-(8*5+1)));	# difference in frame sizes
	&jmp	(&label("point_double_shortcut"));

&set_label("add_proceed",16);
	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($R,"esp"));
	&lea	("ebp",&DWP($R,"esp"));
	&lea	("edi",&DWP($Rsqr,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_sqr_mont(Rsqr, R);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($H,"esp"));
	&lea	("ebp",&DWP($in1_z,"esp"));
	&lea	("edi",&DWP($res_z,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(res_z, H, in1_z);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($H,"esp"));
	&lea	("ebp",&DWP($H,"esp"));
	&lea	("edi",&DWP($Hsqr,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_sqr_mont(Hsqr, H);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in2_z,"esp"));
	&lea	("ebp",&DWP($res_z,"esp"));
	&lea	("edi",&DWP($res_z,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(res_z, res_z, in2_z);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($Hsqr,"esp"));
	&lea	("ebp",&DWP($U1,"esp"));
	&lea	("edi",&DWP($U2,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(U2, U1, Hsqr);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($H,"esp"));
	&lea	("ebp",&DWP($Hsqr,"esp"));
	&lea	("edi",&DWP($Hcub,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(Hcub, Hsqr, H);

	&lea	("esi",&DWP($U2,"esp"));
	&lea	("ebp",&DWP($U2,"esp"));
	&lea	("edi",&DWP($Hsqr,"esp"));
	&call	("_ecp_nistz256_add");		# p256_mul_by_2(Hsqr, U2);

	&lea	("esi",&DWP($Rsqr,"esp"));
	&lea	("ebp",&DWP($Hsqr,"esp"));
	&lea	("edi",&DWP($res_x,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(res_x, Rsqr, Hsqr);

	&lea	("esi",&DWP($res_x,"esp"));
	&lea	("ebp",&DWP($Hcub,"esp"));
	&lea	("edi",&DWP($res_x,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(res_x, res_x, Hcub);

	&lea	("esi",&DWP($U2,"esp"));
	&lea	("ebp",&DWP($res_x,"esp"));
	&lea	("edi",&DWP($res_y,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(res_y, U2, res_x);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($Hcub,"esp"));
	&lea	("ebp",&DWP($S1,"esp"));
	&lea	("edi",&DWP($S2,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(S2, S1, Hcub);

	&mov	("eax",&DWP(32*18+12,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($R,"esp"));
	&lea	("ebp",&DWP($res_y,"esp"));
	&lea	("edi",&DWP($res_y,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(res_y, R, res_y);

	&lea	("esi",&DWP($res_y,"esp"));
	&lea	("ebp",&DWP($S2,"esp"));
	&lea	("edi",&DWP($res_y,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(res_y, res_y, S2);

	&mov	("ebp",&DWP(32*18+0,"esp"));	# !in1infty
	&mov	("esi",&DWP(32*18+4,"esp"));	# !in2infty
	&mov	("edi",&wparam(0));
	&mov	("edx","ebp");
	&not	("ebp");
	&and	("edx","esi");
	&and	("ebp","esi");
	&not	("esi");

	########################################
	# conditional moves
    for($i=64;$i<96;$i+=4) {
	&mov	("eax","edx");
	&and	("eax",&DWP($res_x+$i,"esp"));
	&mov	("ebx","ebp");
	&and	("ebx",&DWP($in2_x+$i,"esp"));
	&mov	("ecx","esi");
	&and	("ecx",&DWP($in1_x+$i,"esp"));
	&or	("eax","ebx");
	&or	("eax","ecx");
	&mov	(&DWP($i,"edi"),"eax");
    }
    for($i=0;$i<64;$i+=4) {
	&mov	("eax","edx");
	&and	("eax",&DWP($res_x+$i,"esp"));
	&mov	("ebx","ebp");
	&and	("ebx",&DWP($in2_x+$i,"esp"));
	&mov	("ecx","esi");
	&and	("ecx",&DWP($in1_x+$i,"esp"));
	&or	("eax","ebx");
	&or	("eax","ecx");
	&mov	(&DWP($i,"edi"),"eax");
    }
    &set_label("add_done");
	&stack_pop(8*18+5);
} &function_end("ecp_nistz256_point_add");

########################################################################
# void ecp_nistz256_point_add_affine(P256_POINT *out,
#				     const P256_POINT *in1,
#				     const P256_POINT_AFFINE *in2);
&function_begin("ecp_nistz256_point_add_affine");
{
    my ($res_x,$res_y,$res_z,
	$in1_x,$in1_y,$in1_z,
	$in2_x,$in2_y,
	$U2,$S2,$H,$R,$Hsqr,$Hcub,$Rsqr)=map(32*$_,(0..14));
    my $Z1sqr = $S2;
    my @ONE_mont=(1,0,0,-1,-1,-1,-2,0);

	&mov	("esi",&wparam(1));

	# above map() describes stack layout with 15 temporary
	# 256-bit vectors on top, then we take extra words for
	# !in1infty, !in2infty, and OPENSSL_ia32cap_P copy.
	&stack_push(8*15+3);
						if ($sse2) {
	&call	("_picup_eax");
    &set_label("pic");
	&picmeup("edx","OPENSSL_ia32cap_P","eax",&label("pic"));
	&mov	("ebp",&DWP(0,"edx"));		}

	&lea	("edi",&DWP($in1_x,"esp"));
    for($i=0;$i<96;$i+=16) {
	&mov	("eax",&DWP($i+0,"esi"));	# copy in1
	&mov	("ebx",&DWP($i+4,"esi"));
	&mov	("ecx",&DWP($i+8,"esi"));
	&mov	("edx",&DWP($i+12,"esi"));
	&mov	(&DWP($i+0,"edi"),"eax");
	&mov	(&DWP(32*15+8,"esp"),"ebp")	if ($i==0);
	&mov	("ebp","eax")			if ($i==64);
	&or	("ebp","eax")			if ($i>64);
	&mov	(&DWP($i+4,"edi"),"ebx");
	&or	("ebp","ebx")			if ($i>=64);
	&mov	(&DWP($i+8,"edi"),"ecx");
	&or	("ebp","ecx")			if ($i>=64);
	&mov	(&DWP($i+12,"edi"),"edx");
	&or	("ebp","edx")			if ($i>=64);
    }
	&xor	("eax","eax");
	&mov	("esi",&wparam(2));
	&sub	("eax","ebp");
	&or	("ebp","eax");
	&sar	("ebp",31);
	&mov	(&DWP(32*15+0,"esp"),"ebp");	# !in1infty

	&lea	("edi",&DWP($in2_x,"esp"));
    for($i=0;$i<64;$i+=16) {
	&mov	("eax",&DWP($i+0,"esi"));	# copy in2
	&mov	("ebx",&DWP($i+4,"esi"));
	&mov	("ecx",&DWP($i+8,"esi"));
	&mov	("edx",&DWP($i+12,"esi"));
	&mov	(&DWP($i+0,"edi"),"eax");
	&mov	("ebp","eax")			if ($i==0);
	&or	("ebp","eax")			if ($i!=0);
	&mov	(&DWP($i+4,"edi"),"ebx");
	&or	("ebp","ebx");
	&mov	(&DWP($i+8,"edi"),"ecx");
	&or	("ebp","ecx");
	&mov	(&DWP($i+12,"edi"),"edx");
	&or	("ebp","edx");
    }
	&xor	("ebx","ebx");
	&mov	("eax",&DWP(32*15+8,"esp"));	# OPENSSL_ia32cap_P copy
	&sub	("ebx","ebp");
	 &lea	("esi",&DWP($in1_z,"esp"));
	&or	("ebx","ebp");
	 &lea	("ebp",&DWP($in1_z,"esp"));
	&sar	("ebx",31);
	 &lea	("edi",&DWP($Z1sqr,"esp"));
	&mov	(&DWP(32*15+4,"esp"),"ebx");	# !in2infty

	&call	("_ecp_nistz256_mul_mont");	# p256_sqr_mont(Z1sqr, in1_z);

	&mov	("eax",&DWP(32*15+8,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in2_x,"esp"));
	&mov	("ebp","edi");			# %esi is stull &Z1sqr
	&lea	("edi",&DWP($U2,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(U2, Z1sqr, in2_x);

	&mov	("eax",&DWP(32*15+8,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in1_z,"esp"));
	&lea	("ebp",&DWP($Z1sqr,"esp"));
	&lea	("edi",&DWP($S2,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(S2, Z1sqr, in1_z);

	&lea	("esi",&DWP($U2,"esp"));
	&lea	("ebp",&DWP($in1_x,"esp"));
	&lea	("edi",&DWP($H,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(H, U2, in1_x);

	&mov	("eax",&DWP(32*15+8,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in2_y,"esp"));
	&lea	("ebp",&DWP($S2,"esp"));
	&lea	("edi",&DWP($S2,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(S2, S2, in2_y);

	&mov	("eax",&DWP(32*15+8,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in1_z,"esp"));
	&lea	("ebp",&DWP($H,"esp"));
	&lea	("edi",&DWP($res_z,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(res_z, H, in1_z);

	&lea	("esi",&DWP($S2,"esp"));
	&lea	("ebp",&DWP($in1_y,"esp"));
	&lea	("edi",&DWP($R,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(R, S2, in1_y);

	&mov	("eax",&DWP(32*15+8,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($H,"esp"));
	&lea	("ebp",&DWP($H,"esp"));
	&lea	("edi",&DWP($Hsqr,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_sqr_mont(Hsqr, H);

	&mov	("eax",&DWP(32*15+8,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($R,"esp"));
	&lea	("ebp",&DWP($R,"esp"));
	&lea	("edi",&DWP($Rsqr,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_sqr_mont(Rsqr, R);

	&mov	("eax",&DWP(32*15+8,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($in1_x,"esp"));
	&lea	("ebp",&DWP($Hsqr,"esp"));
	&lea	("edi",&DWP($U2,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(U2, in1_x, Hsqr);

	&mov	("eax",&DWP(32*15+8,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($H,"esp"));
	&lea	("ebp",&DWP($Hsqr,"esp"));
	&lea	("edi",&DWP($Hcub,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(Hcub, Hsqr, H);

	&lea	("esi",&DWP($U2,"esp"));
	&lea	("ebp",&DWP($U2,"esp"));
	&lea	("edi",&DWP($Hsqr,"esp"));
	&call	("_ecp_nistz256_add");		# p256_mul_by_2(Hsqr, U2);

	&lea	("esi",&DWP($Rsqr,"esp"));
	&lea	("ebp",&DWP($Hsqr,"esp"));
	&lea	("edi",&DWP($res_x,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(res_x, Rsqr, Hsqr);

	&lea	("esi",&DWP($res_x,"esp"));
	&lea	("ebp",&DWP($Hcub,"esp"));
	&lea	("edi",&DWP($res_x,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(res_x, res_x, Hcub);

	&lea	("esi",&DWP($U2,"esp"));
	&lea	("ebp",&DWP($res_x,"esp"));
	&lea	("edi",&DWP($res_y,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(res_y, U2, res_x);

	&mov	("eax",&DWP(32*15+8,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($Hcub,"esp"));
	&lea	("ebp",&DWP($in1_y,"esp"));
	&lea	("edi",&DWP($S2,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(S2, Hcub, in1_y);

	&mov	("eax",&DWP(32*15+8,"esp"));	# OPENSSL_ia32cap_P copy
	&lea	("esi",&DWP($R,"esp"));
	&lea	("ebp",&DWP($res_y,"esp"));
	&lea	("edi",&DWP($res_y,"esp"));
	&call	("_ecp_nistz256_mul_mont");	# p256_mul_mont(res_y, res_y, R);

	&lea	("esi",&DWP($res_y,"esp"));
	&lea	("ebp",&DWP($S2,"esp"));
	&lea	("edi",&DWP($res_y,"esp"));
	&call	("_ecp_nistz256_sub");		# p256_sub(res_y, res_y, S2);

	&mov	("ebp",&DWP(32*15+0,"esp"));	# !in1infty
	&mov	("esi",&DWP(32*15+4,"esp"));	# !in2infty
	&mov	("edi",&wparam(0));
	&mov	("edx","ebp");
	&not	("ebp");
	&and	("edx","esi");
	&and	("ebp","esi");
	&not	("esi");

	########################################
	# conditional moves
    for($i=64;$i<96;$i+=4) {
	my $one=@ONE_mont[($i-64)/4];

	&mov	("eax","edx");
	&and	("eax",&DWP($res_x+$i,"esp"));
	&mov	("ebx","ebp")			if ($one && $one!=-1);
	&and	("ebx",$one)			if ($one && $one!=-1);
	&mov	("ecx","esi");
	&and	("ecx",&DWP($in1_x+$i,"esp"));
	&or	("eax",$one==-1?"ebp":"ebx")	if ($one);
	&or	("eax","ecx");
	&mov	(&DWP($i,"edi"),"eax");
    }
    for($i=0;$i<64;$i+=4) {
	&mov	("eax","edx");
	&and	("eax",&DWP($res_x+$i,"esp"));
	&mov	("ebx","ebp");
	&and	("ebx",&DWP($in2_x+$i,"esp"));
	&mov	("ecx","esi");
	&and	("ecx",&DWP($in1_x+$i,"esp"));
	&or	("eax","ebx");
	&or	("eax","ecx");
	&mov	(&DWP($i,"edi"),"eax");
    }
	&stack_pop(8*15+3);
} &function_end("ecp_nistz256_point_add_affine");

&asm_finish();

close STDOUT;
