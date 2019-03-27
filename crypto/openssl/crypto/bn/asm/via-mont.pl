#! /usr/bin/env perl
# Copyright 2006-2018 The OpenSSL Project Authors. All Rights Reserved.
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
# Wrapper around 'rep montmul', VIA-specific instruction accessing
# PadLock Montgomery Multiplier. The wrapper is designed as drop-in
# replacement for OpenSSL bn_mul_mont [first implemented in 0.9.9].
#
# Below are interleaved outputs from 'openssl speed rsa dsa' for 4
# different software configurations on 1.5GHz VIA Esther processor.
# Lines marked with "software integer" denote performance of hand-
# coded integer-only assembler found in OpenSSL 0.9.7. "Software SSE2"
# refers to hand-coded SSE2 Montgomery multiplication procedure found
# OpenSSL 0.9.9. "Hardware VIA SDK" refers to padlock_pmm routine from
# Padlock SDK 2.0.1 available for download from VIA, which naturally
# utilizes the magic 'repz montmul' instruction. And finally "hardware
# this" refers to *this* implementation which also uses 'repz montmul'
#
#                   sign    verify    sign/s verify/s
# rsa  512 bits 0.001720s 0.000140s    581.4   7149.7	software integer
# rsa  512 bits 0.000690s 0.000086s   1450.3  11606.0	software SSE2
# rsa  512 bits 0.006136s 0.000201s    163.0   4974.5	hardware VIA SDK
# rsa  512 bits 0.000712s 0.000050s   1404.9  19858.5	hardware this
#
# rsa 1024 bits 0.008518s 0.000413s    117.4   2420.8	software integer
# rsa 1024 bits 0.004275s 0.000277s    233.9   3609.7	software SSE2
# rsa 1024 bits 0.012136s 0.000260s     82.4   3844.5	hardware VIA SDK
# rsa 1024 bits 0.002522s 0.000116s    396.5   8650.9	hardware this
#
# rsa 2048 bits 0.050101s 0.001371s     20.0    729.6	software integer
# rsa 2048 bits 0.030273s 0.001008s     33.0    991.9	software SSE2
# rsa 2048 bits 0.030833s 0.000976s     32.4   1025.1	hardware VIA SDK
# rsa 2048 bits 0.011879s 0.000342s     84.2   2921.7	hardware this
#
# rsa 4096 bits 0.327097s 0.004859s      3.1    205.8	software integer
# rsa 4096 bits 0.229318s 0.003859s      4.4    259.2	software SSE2
# rsa 4096 bits 0.233953s 0.003274s      4.3    305.4	hardware VIA SDK
# rsa 4096 bits 0.070493s 0.001166s     14.2    857.6	hardware this
#
# dsa  512 bits 0.001342s 0.001651s    745.2    605.7	software integer
# dsa  512 bits 0.000844s 0.000987s   1185.3   1013.1	software SSE2
# dsa  512 bits 0.001902s 0.002247s    525.6    444.9	hardware VIA SDK
# dsa  512 bits 0.000458s 0.000524s   2182.2   1909.1	hardware this
#
# dsa 1024 bits 0.003964s 0.004926s    252.3    203.0	software integer
# dsa 1024 bits 0.002686s 0.003166s    372.3    315.8	software SSE2
# dsa 1024 bits 0.002397s 0.002823s    417.1    354.3	hardware VIA SDK
# dsa 1024 bits 0.000978s 0.001170s   1022.2    855.0	hardware this
#
# dsa 2048 bits 0.013280s 0.016518s     75.3     60.5	software integer
# dsa 2048 bits 0.009911s 0.011522s    100.9     86.8	software SSE2
# dsa 2048 bits 0.009542s 0.011763s    104.8     85.0	hardware VIA SDK
# dsa 2048 bits 0.002884s 0.003352s    346.8    298.3	hardware this
#
# To give you some other reference point here is output for 2.4GHz P4
# running hand-coded SSE2 bn_mul_mont found in 0.9.9, i.e. "software
# SSE2" in above terms.
#
# rsa  512 bits 0.000407s 0.000047s   2454.2  21137.0
# rsa 1024 bits 0.002426s 0.000141s    412.1   7100.0
# rsa 2048 bits 0.015046s 0.000491s     66.5   2034.9
# rsa 4096 bits 0.109770s 0.002379s      9.1    420.3
# dsa  512 bits 0.000438s 0.000525s   2281.1   1904.1
# dsa 1024 bits 0.001346s 0.001595s    742.7    627.0
# dsa 2048 bits 0.004745s 0.005582s    210.7    179.1
#
# Conclusions:
# - VIA SDK leaves a *lot* of room for improvement (which this
#   implementation successfully fills:-);
# - 'rep montmul' gives up to >3x performance improvement depending on
#   key length;
# - in terms of absolute performance it delivers approximately as much
#   as modern out-of-order 32-bit cores [again, for longer keys].

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

$output = pop;
open STDOUT,">$output";

&asm_init($ARGV[0]);

# int bn_mul_mont(BN_ULONG *rp, const BN_ULONG *ap, const BN_ULONG *bp, const BN_ULONG *np,const BN_ULONG *n0, int num);
$func="bn_mul_mont_padlock";

$pad=16*1;	# amount of reserved bytes on top of every vector

# stack layout
$mZeroPrime=&DWP(0,"esp");		# these are specified by VIA
$A=&DWP(4,"esp");
$B=&DWP(8,"esp");
$T=&DWP(12,"esp");
$M=&DWP(16,"esp");
$scratch=&DWP(20,"esp");
$rp=&DWP(24,"esp");			# these are mine
$sp=&DWP(28,"esp");
# &DWP(32,"esp")			# 32 byte scratch area
# &DWP(64+(4*$num+$pad)*0,"esp")	# padded tp[num]
# &DWP(64+(4*$num+$pad)*1,"esp")	# padded copy of ap[num]
# &DWP(64+(4*$num+$pad)*2,"esp")	# padded copy of bp[num]
# &DWP(64+(4*$num+$pad)*3,"esp")	# padded copy of np[num]
# Note that SDK suggests to unconditionally allocate 2K per vector. This
# has quite an impact on performance. It naturally depends on key length,
# but to give an example 1024 bit private RSA key operations suffer >30%
# penalty. I allocate only as much as actually required...

&function_begin($func);
	&xor	("eax","eax");
	&mov	("ecx",&wparam(5));	# num
	# meet VIA's limitations for num [note that the specification
	# expresses them in bits, while we work with amount of 32-bit words]
	&test	("ecx",3);
	&jnz	(&label("leave"));	# num % 4 != 0
	&cmp	("ecx",8);
	&jb	(&label("leave"));	# num < 8
	&cmp	("ecx",1024);
	&ja	(&label("leave"));	# num > 1024

	&pushf	();
	&cld	();

	&mov	("edi",&wparam(0));	# rp
	&mov	("eax",&wparam(1));	# ap
	&mov	("ebx",&wparam(2));	# bp
	&mov	("edx",&wparam(3));	# np
	&mov	("esi",&wparam(4));	# n0
	&mov	("esi",&DWP(0,"esi"));	# *n0

	&lea	("ecx",&DWP($pad,"","ecx",4));	# ecx becomes vector size in bytes
	&lea	("ebp",&DWP(64,"","ecx",4));	# allocate 4 vectors + 64 bytes
	&neg	("ebp");
	&add	("ebp","esp");
	&and	("ebp",-64);		# align to cache-line
	&xchg	("ebp","esp");		# alloca

	&mov	($rp,"edi");		# save rp
	&mov	($sp,"ebp");		# save esp

	&mov	($mZeroPrime,"esi");
	&lea	("esi",&DWP(64,"esp"));	# tp
	&mov	($T,"esi");
	&lea	("edi",&DWP(32,"esp"));	# scratch area
	&mov	($scratch,"edi");
	&mov	("esi","eax");

	&lea	("ebp",&DWP(-$pad,"ecx"));
	&shr	("ebp",2);		# restore original num value in ebp

	&xor	("eax","eax");

	&mov	("ecx","ebp");
	&lea	("ecx",&DWP((32+$pad)/4,"ecx"));# padded tp + scratch
	&data_byte(0xf3,0xab);		# rep stosl, bzero

	&mov	("ecx","ebp");
	&lea	("edi",&DWP(64+$pad,"esp","ecx",4));# pointer to ap copy
	&mov	($A,"edi");
	&data_byte(0xf3,0xa5);		# rep movsl, memcpy
	&mov	("ecx",$pad/4);
	&data_byte(0xf3,0xab);		# rep stosl, bzero pad
	# edi points at the end of padded ap copy...

	&mov	("ecx","ebp");
	&mov	("esi","ebx");
	&mov	($B,"edi");
	&data_byte(0xf3,0xa5);		# rep movsl, memcpy
	&mov	("ecx",$pad/4);
	&data_byte(0xf3,0xab);		# rep stosl, bzero pad
	# edi points at the end of padded bp copy...

	&mov	("ecx","ebp");
	&mov	("esi","edx");
	&mov	($M,"edi");
	&data_byte(0xf3,0xa5);		# rep movsl, memcpy
	&mov	("ecx",$pad/4);
	&data_byte(0xf3,0xab);		# rep stosl, bzero pad
	# edi points at the end of padded np copy...

	# let magic happen...
	&mov	("ecx","ebp");
	&mov	("esi","esp");
	&shl	("ecx",5);		# convert word counter to bit counter
	&align	(4);
	&data_byte(0xf3,0x0f,0xa6,0xc0);# rep montmul

	&mov	("ecx","ebp");
	&lea	("esi",&DWP(64,"esp"));		# tp
	# edi still points at the end of padded np copy...
	&neg	("ebp");
	&lea	("ebp",&DWP(-$pad,"edi","ebp",4));	# so just "rewind"
	&mov	("edi",$rp);			# restore rp
	&xor	("edx","edx");			# i=0 and clear CF

&set_label("sub",8);
	&mov	("eax",&DWP(0,"esi","edx",4));
	&sbb	("eax",&DWP(0,"ebp","edx",4));
	&mov	(&DWP(0,"edi","edx",4),"eax");	# rp[i]=tp[i]-np[i]
	&lea	("edx",&DWP(1,"edx"));		# i++
	&loop	(&label("sub"));		# doesn't affect CF!

	&mov	("eax",&DWP(0,"esi","edx",4));	# upmost overflow bit
	&sbb	("eax",0);

	&mov	("ecx","edx");			# num
	&mov	("edx",0);			# i=0

&set_label("copy",8);
	&mov	("ebx",&DWP(0,"esi","edx",4));
	&mov	("eax",&DWP(0,"edi","edx",4));
	&mov	(&DWP(0,"esi","edx",4),"ecx");	# zap tp
	&cmovc	("eax","ebx");
	&mov	(&DWP(0,"edi","edx",4),"eax");
	&lea	("edx",&DWP(1,"edx"));		# i++
	&loop	(&label("copy"));

	&mov	("ebp",$sp);
	&xor	("eax","eax");

	&mov	("ecx",64/4);
	&mov	("edi","esp");		# zap frame including scratch area
	&data_byte(0xf3,0xab);		# rep stosl, bzero

	# zap copies of ap, bp and np
	&lea	("edi",&DWP(64+$pad,"esp","edx",4));# pointer to ap
	&lea	("ecx",&DWP(3*$pad/4,"edx","edx",2));
	&data_byte(0xf3,0xab);		# rep stosl, bzero

	&mov	("esp","ebp");
	&inc	("eax");		# signal "done"
	&popf	();
&set_label("leave");
&function_end($func);

&asciz("Padlock Montgomery Multiplication, CRYPTOGAMS by <appro\@openssl.org>");

&asm_finish();

close STDOUT;
