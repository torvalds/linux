#! /usr/bin/env perl
# Copyright 2007-2018 The OpenSSL Project Authors. All Rights Reserved.
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
# SHA256 block transform for x86. September 2007.
#
# Performance improvement over compiler generated code varies from
# 10% to 40% [see below]. Not very impressive on some µ-archs, but
# it's 5 times smaller and optimizes amount of writes.
#
# May 2012.
#
# Optimization including two of Pavel Semjanov's ideas, alternative
# Maj and full unroll, resulted in ~20-25% improvement on most CPUs,
# ~7% on Pentium, ~40% on Atom. As fully unrolled loop body is almost
# 15x larger, 8KB vs. 560B, it's fired only for longer inputs. But not
# on P4, where it kills performance, nor Sandy Bridge, where folded
# loop is approximately as fast...
#
# June 2012.
#
# Add AMD XOP-specific code path, >30% improvement on Bulldozer over
# May version, >60% over original. Add AVX+shrd code path, >25%
# improvement on Sandy Bridge over May version, 60% over original.
#
# May 2013.
#
# Replace AMD XOP code path with SSSE3 to cover more processors.
# (Biggest improvement coefficient is on upcoming Atom Silvermont,
# not shown.) Add AVX+BMI code path.
#
# March 2014.
#
# Add support for Intel SHA Extensions.
#
# Performance in clock cycles per processed byte (less is better):
#
#		gcc	icc	x86 asm(*)	SIMD	x86_64 asm(**)
# Pentium	46	57	40/38		-	-
# PIII		36	33	27/24		-	-
# P4		41	38	28		-	17.3
# AMD K8	27	25	19/15.5		-	14.9
# Core2		26	23	18/15.6		14.3	13.8
# Westmere	27	-	19/15.7		13.4	12.3
# Sandy Bridge	25	-	15.9		12.4	11.6
# Ivy Bridge	24	-	15.0		11.4	10.3
# Haswell	22	-	13.9		9.46	7.80
# Skylake	20	-	14.9		9.50	7.70
# Bulldozer	36	-	27/22		17.0	13.6
# VIA Nano	36	-	25/22		16.8	16.5
# Atom		50	-	30/25		21.9	18.9
# Silvermont	40	-	34/31		22.9	20.6
# Goldmont	29	-	20		16.3(***)
#
# (*)	numbers after slash are for unrolled loop, where applicable;
# (**)	x86_64 assembly performance is presented for reference
#	purposes, results are best-available;
# (***)	SHAEXT result is 4.1, strangely enough better than 64-bit one;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

$output=pop;
open STDOUT,">$output";

&asm_init($ARGV[0],$ARGV[$#ARGV] eq "386");

$xmm=$avx=0;
for (@ARGV) { $xmm=1 if (/-DOPENSSL_IA32_SSE2/); }

if ($xmm &&	`$ENV{CC} -Wa,-v -c -o /dev/null -x assembler /dev/null 2>&1`
			=~ /GNU assembler version ([2-9]\.[0-9]+)/) {
	$avx = ($1>=2.19) + ($1>=2.22);
}

if ($xmm && !$avx && $ARGV[0] eq "win32n" &&
		`nasm -v 2>&1` =~ /NASM version ([2-9]\.[0-9]+)/) {
	$avx = ($1>=2.03) + ($1>=2.10);
}

if ($xmm && !$avx && $ARGV[0] eq "win32" &&
		`ml 2>&1` =~ /Version ([0-9]+)\./) {
	$avx = ($1>=10) + ($1>=11);
}

if ($xmm && !$avx && `$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|based on LLVM) ([3-9]\.[0-9]+)/) {
	$avx = ($2>=3.0) + ($2>3.0);
}

$shaext=$xmm;	### set to zero if compiling for 1.0.1

$unroll_after = 64*4;	# If pre-evicted from L1P cache first spin of
			# fully unrolled loop was measured to run about
			# 3-4x slower. If slowdown coefficient is N and
			# unrolled loop is m times faster, then you break
			# even at (N-1)/(m-1) blocks. Then it needs to be
			# adjusted for probability of code being evicted,
			# code size/cache size=1/4. Typical m is 1.15...

$A="eax";
$E="edx";
$T="ebx";
$Aoff=&DWP(4,"esp");
$Boff=&DWP(8,"esp");
$Coff=&DWP(12,"esp");
$Doff=&DWP(16,"esp");
$Eoff=&DWP(20,"esp");
$Foff=&DWP(24,"esp");
$Goff=&DWP(28,"esp");
$Hoff=&DWP(32,"esp");
$Xoff=&DWP(36,"esp");
$K256="ebp";

sub BODY_16_63() {
	&mov	($T,"ecx");			# "ecx" is preloaded
	 &mov	("esi",&DWP(4*(9+15+16-14),"esp"));
	&ror	("ecx",18-7);
	 &mov	("edi","esi");
	&ror	("esi",19-17);
	 &xor	("ecx",$T);
	 &shr	($T,3);
	&ror	("ecx",7);
	 &xor	("esi","edi");
	 &xor	($T,"ecx");			# T = sigma0(X[-15])
	&ror	("esi",17);
	 &add	($T,&DWP(4*(9+15+16),"esp"));	# T += X[-16]
	&shr	("edi",10);
	 &add	($T,&DWP(4*(9+15+16-9),"esp"));	# T += X[-7]
	#&xor	("edi","esi")			# sigma1(X[-2])
	# &add	($T,"edi");			# T += sigma1(X[-2])
	# &mov	(&DWP(4*(9+15),"esp"),$T);	# save X[0]

	&BODY_00_15(1);
}
sub BODY_00_15() {
    my $in_16_63=shift;

	&mov	("ecx",$E);
	 &xor	("edi","esi")			if ($in_16_63);	# sigma1(X[-2])
	 &mov	("esi",$Foff);
	&ror	("ecx",25-11);
	 &add	($T,"edi")			if ($in_16_63);	# T += sigma1(X[-2])
	 &mov	("edi",$Goff);
	&xor	("ecx",$E);
	 &xor	("esi","edi");
	 &mov	($T,&DWP(4*(9+15),"esp"))	if (!$in_16_63);
	 &mov	(&DWP(4*(9+15),"esp"),$T)	if ($in_16_63);	# save X[0]
	&ror	("ecx",11-6);
	 &and	("esi",$E);
	 &mov	($Eoff,$E);		# modulo-scheduled
	&xor	($E,"ecx");
	 &add	($T,$Hoff);		# T += h
	 &xor	("esi","edi");		# Ch(e,f,g)
	&ror	($E,6);			# Sigma1(e)
	 &mov	("ecx",$A);
	 &add	($T,"esi");		# T += Ch(e,f,g)

	&ror	("ecx",22-13);
	 &add	($T,$E);		# T += Sigma1(e)
	 &mov	("edi",$Boff);
	&xor	("ecx",$A);
	 &mov	($Aoff,$A);		# modulo-scheduled
	 &lea	("esp",&DWP(-4,"esp"));
	&ror	("ecx",13-2);
	 &mov	("esi",&DWP(0,$K256));
	&xor	("ecx",$A);
	 &mov	($E,$Eoff);		# e in next iteration, d in this one
	 &xor	($A,"edi");		# a ^= b
	&ror	("ecx",2);		# Sigma0(a)

	 &add	($T,"esi");		# T+= K[i]
	 &mov	(&DWP(0,"esp"),$A);	# (b^c) in next round
	&add	($E,$T);		# d += T
	 &and	($A,&DWP(4,"esp"));	# a &= (b^c)
	&add	($T,"ecx");		# T += Sigma0(a)
	 &xor	($A,"edi");		# h = Maj(a,b,c) = Ch(a^b,c,b)
	 &mov	("ecx",&DWP(4*(9+15+16-1),"esp"))	if ($in_16_63);	# preload T
	&add	($K256,4);
	 &add	($A,$T);		# h += T
}

&external_label("OPENSSL_ia32cap_P")		if (!$i386);

&function_begin("sha256_block_data_order");
	&mov	("esi",wparam(0));	# ctx
	&mov	("edi",wparam(1));	# inp
	&mov	("eax",wparam(2));	# num
	&mov	("ebx","esp");		# saved sp

	&call	(&label("pic_point"));	# make it PIC!
&set_label("pic_point");
	&blindpop($K256);
	&lea	($K256,&DWP(&label("K256")."-".&label("pic_point"),$K256));

	&sub	("esp",16);
	&and	("esp",-64);

	&shl	("eax",6);
	&add	("eax","edi");
	&mov	(&DWP(0,"esp"),"esi");	# ctx
	&mov	(&DWP(4,"esp"),"edi");	# inp
	&mov	(&DWP(8,"esp"),"eax");	# inp+num*128
	&mov	(&DWP(12,"esp"),"ebx");	# saved sp
						if (!$i386 && $xmm) {
	&picmeup("edx","OPENSSL_ia32cap_P",$K256,&label("K256"));
	&mov	("ecx",&DWP(0,"edx"));
	&mov	("ebx",&DWP(4,"edx"));
	&test	("ecx",1<<20);		# check for P4
	&jnz	(&label("loop"));
	&mov	("edx",&DWP(8,"edx"))	if ($xmm);
	&test	("ecx",1<<24);		# check for FXSR
	&jz	($unroll_after?&label("no_xmm"):&label("loop"));
	&and	("ecx",1<<30);		# mask "Intel CPU" bit
	&and	("ebx",1<<28|1<<9);	# mask AVX and SSSE3 bits
	&test	("edx",1<<29)		if ($shaext);	# check for SHA
	&jnz	(&label("shaext"))	if ($shaext);
	&or	("ecx","ebx");
	&and	("ecx",1<<28|1<<30);
	&cmp	("ecx",1<<28|1<<30);
					if ($xmm) {
	&je	(&label("AVX"))		if ($avx);
	&test	("ebx",1<<9);		# check for SSSE3
	&jnz	(&label("SSSE3"));
					} else {
	&je	(&label("loop_shrd"));
					}
						if ($unroll_after) {
&set_label("no_xmm");
	&sub	("eax","edi");
	&cmp	("eax",$unroll_after);
	&jae	(&label("unrolled"));
						} }
	&jmp	(&label("loop"));

sub COMPACT_LOOP() {
my $suffix=shift;

&set_label("loop$suffix",$suffix?32:16);
    # copy input block to stack reversing byte and dword order
    for($i=0;$i<4;$i++) {
	&mov	("eax",&DWP($i*16+0,"edi"));
	&mov	("ebx",&DWP($i*16+4,"edi"));
	&mov	("ecx",&DWP($i*16+8,"edi"));
	&bswap	("eax");
	&mov	("edx",&DWP($i*16+12,"edi"));
	&bswap	("ebx");
	&push	("eax");
	&bswap	("ecx");
	&push	("ebx");
	&bswap	("edx");
	&push	("ecx");
	&push	("edx");
    }
	&add	("edi",64);
	&lea	("esp",&DWP(-4*9,"esp"));# place for A,B,C,D,E,F,G,H
	&mov	(&DWP(4*(9+16)+4,"esp"),"edi");

	# copy ctx->h[0-7] to A,B,C,D,E,F,G,H on stack
	&mov	($A,&DWP(0,"esi"));
	&mov	("ebx",&DWP(4,"esi"));
	&mov	("ecx",&DWP(8,"esi"));
	&mov	("edi",&DWP(12,"esi"));
	# &mov	($Aoff,$A);
	&mov	($Boff,"ebx");
	&xor	("ebx","ecx");
	&mov	($Coff,"ecx");
	&mov	($Doff,"edi");
	&mov	(&DWP(0,"esp"),"ebx");	# magic
	&mov	($E,&DWP(16,"esi"));
	&mov	("ebx",&DWP(20,"esi"));
	&mov	("ecx",&DWP(24,"esi"));
	&mov	("edi",&DWP(28,"esi"));
	# &mov	($Eoff,$E);
	&mov	($Foff,"ebx");
	&mov	($Goff,"ecx");
	&mov	($Hoff,"edi");

&set_label("00_15$suffix",16);

	&BODY_00_15();

	&cmp	("esi",0xc19bf174);
	&jne	(&label("00_15$suffix"));

	&mov	("ecx",&DWP(4*(9+15+16-1),"esp"));	# preloaded in BODY_00_15(1)
	&jmp	(&label("16_63$suffix"));

&set_label("16_63$suffix",16);

	&BODY_16_63();

	&cmp	("esi",0xc67178f2);
	&jne	(&label("16_63$suffix"));

	&mov	("esi",&DWP(4*(9+16+64)+0,"esp"));#ctx
	# &mov	($A,$Aoff);
	&mov	("ebx",$Boff);
	# &mov	("edi",$Coff);
	&mov	("ecx",$Doff);
	&add	($A,&DWP(0,"esi"));
	&add	("ebx",&DWP(4,"esi"));
	&add	("edi",&DWP(8,"esi"));
	&add	("ecx",&DWP(12,"esi"));
	&mov	(&DWP(0,"esi"),$A);
	&mov	(&DWP(4,"esi"),"ebx");
	&mov	(&DWP(8,"esi"),"edi");
	&mov	(&DWP(12,"esi"),"ecx");
	# &mov	($E,$Eoff);
	&mov	("eax",$Foff);
	&mov	("ebx",$Goff);
	&mov	("ecx",$Hoff);
	&mov	("edi",&DWP(4*(9+16+64)+4,"esp"));#inp
	&add	($E,&DWP(16,"esi"));
	&add	("eax",&DWP(20,"esi"));
	&add	("ebx",&DWP(24,"esi"));
	&add	("ecx",&DWP(28,"esi"));
	&mov	(&DWP(16,"esi"),$E);
	&mov	(&DWP(20,"esi"),"eax");
	&mov	(&DWP(24,"esi"),"ebx");
	&mov	(&DWP(28,"esi"),"ecx");

	&lea	("esp",&DWP(4*(9+16+64),"esp"));# destroy frame
	&sub	($K256,4*64);			# rewind K

	&cmp	("edi",&DWP(8,"esp"));		# are we done yet?
	&jb	(&label("loop$suffix"));
}
	&COMPACT_LOOP();
	&mov	("esp",&DWP(12,"esp"));		# restore sp
&function_end_A();
						if (!$i386 && !$xmm) {
	# ~20% improvement on Sandy Bridge
	local *ror = sub { &shrd(@_[0],@_) };
	&COMPACT_LOOP("_shrd");
	&mov	("esp",&DWP(12,"esp"));		# restore sp
&function_end_A();
						}

&set_label("K256",64);	# Yes! I keep it in the code segment!
@K256=(	0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,
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
	0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2	);
&data_word(@K256);
&data_word(0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f);	# byte swap mask
&asciz("SHA256 block transform for x86, CRYPTOGAMS by <appro\@openssl.org>");

($a,$b,$c,$d,$e,$f,$g,$h)=(0..7);	# offsets
sub off { &DWP(4*(((shift)-$i)&7),"esp"); }

if (!$i386 && $unroll_after) {
my @AH=($A,$K256);

&set_label("unrolled",16);
	&lea	("esp",&DWP(-96,"esp"));
	# copy ctx->h[0-7] to A,B,C,D,E,F,G,H on stack
	&mov	($AH[0],&DWP(0,"esi"));
	&mov	($AH[1],&DWP(4,"esi"));
	&mov	("ecx",&DWP(8,"esi"));
	&mov	("ebx",&DWP(12,"esi"));
	#&mov	(&DWP(0,"esp"),$AH[0]);
	&mov	(&DWP(4,"esp"),$AH[1]);
	&xor	($AH[1],"ecx");		# magic
	&mov	(&DWP(8,"esp"),"ecx");
	&mov	(&DWP(12,"esp"),"ebx");
	&mov	($E,&DWP(16,"esi"));
	&mov	("ebx",&DWP(20,"esi"));
	&mov	("ecx",&DWP(24,"esi"));
	&mov	("esi",&DWP(28,"esi"));
	#&mov	(&DWP(16,"esp"),$E);
	&mov	(&DWP(20,"esp"),"ebx");
	&mov	(&DWP(24,"esp"),"ecx");
	&mov	(&DWP(28,"esp"),"esi");
	&jmp	(&label("grand_loop"));

&set_label("grand_loop",16);
    # copy input block to stack reversing byte order
    for($i=0;$i<5;$i++) {
	&mov	("ebx",&DWP(12*$i+0,"edi"));
	&mov	("ecx",&DWP(12*$i+4,"edi"));
	&bswap	("ebx");
	&mov	("esi",&DWP(12*$i+8,"edi"));
	&bswap	("ecx");
	&mov	(&DWP(32+12*$i+0,"esp"),"ebx");
	&bswap	("esi");
	&mov	(&DWP(32+12*$i+4,"esp"),"ecx");
	&mov	(&DWP(32+12*$i+8,"esp"),"esi");
    }
	&mov	("ebx",&DWP($i*12,"edi"));
	&add	("edi",64);
	&bswap	("ebx");
	&mov	(&DWP(96+4,"esp"),"edi");
	&mov	(&DWP(32+12*$i,"esp"),"ebx");

    my ($t1,$t2) = ("ecx","esi");

    for ($i=0;$i<64;$i++) {

      if ($i>=16) {
	&mov	($T,$t1);			# $t1 is preloaded
	# &mov	($t2,&DWP(32+4*(($i+14)&15),"esp"));
	&ror	($t1,18-7);
	 &mov	("edi",$t2);
	&ror	($t2,19-17);
	 &xor	($t1,$T);
	 &shr	($T,3);
	&ror	($t1,7);
	 &xor	($t2,"edi");
	 &xor	($T,$t1);			# T = sigma0(X[-15])
	&ror	($t2,17);
	 &add	($T,&DWP(32+4*($i&15),"esp"));	# T += X[-16]
	&shr	("edi",10);
	 &add	($T,&DWP(32+4*(($i+9)&15),"esp"));	# T += X[-7]
	#&xor	("edi",$t2)			# sigma1(X[-2])
	# &add	($T,"edi");			# T += sigma1(X[-2])
	# &mov	(&DWP(4*(9+15),"esp"),$T);	# save X[0]
      }
	&mov	($t1,$E);
	 &xor	("edi",$t2)			if ($i>=16);	# sigma1(X[-2])
	 &mov	($t2,&off($f));
	&ror	($E,25-11);
	 &add	($T,"edi")			if ($i>=16);	# T += sigma1(X[-2])
	 &mov	("edi",&off($g));
	&xor	($E,$t1);
	 &mov	($T,&DWP(32+4*($i&15),"esp"))	if ($i<16);	# X[i]
	 &mov	(&DWP(32+4*($i&15),"esp"),$T)	if ($i>=16 && $i<62);	# save X[0]
	 &xor	($t2,"edi");
	&ror	($E,11-6);
	 &and	($t2,$t1);
	 &mov	(&off($e),$t1);		# save $E, modulo-scheduled
	&xor	($E,$t1);
	 &add	($T,&off($h));		# T += h
	 &xor	("edi",$t2);		# Ch(e,f,g)
	&ror	($E,6);			# Sigma1(e)
	 &mov	($t1,$AH[0]);
	 &add	($T,"edi");		# T += Ch(e,f,g)

	&ror	($t1,22-13);
	 &mov	($t2,$AH[0]);
	 &mov	("edi",&off($b));
	&xor	($t1,$AH[0]);
	 &mov	(&off($a),$AH[0]);	# save $A, modulo-scheduled
	 &xor	($AH[0],"edi");		# a ^= b, (b^c) in next round
	&ror	($t1,13-2);
	 &and	($AH[1],$AH[0]);	# (b^c) &= (a^b)
	 &lea	($E,&DWP(@K256[$i],$T,$E));	# T += Sigma1(1)+K[i]
	&xor	($t1,$t2);
	 &xor	($AH[1],"edi");		# h = Maj(a,b,c) = Ch(a^b,c,b)
	 &mov	($t2,&DWP(32+4*(($i+2)&15),"esp"))	if ($i>=15 && $i<63);
	&ror	($t1,2);		# Sigma0(a)

	 &add	($AH[1],$E);		# h += T
	 &add	($E,&off($d));		# d += T
	&add	($AH[1],$t1);		# h += Sigma0(a)
	 &mov	($t1,&DWP(32+4*(($i+15)&15),"esp"))	if ($i>=15 && $i<63);

	@AH = reverse(@AH);		# rotate(a,h)
	($t1,$t2) = ($t2,$t1);		# rotate(t1,t2)
    }
	&mov	("esi",&DWP(96,"esp"));	#ctx
					#&mov	($AH[0],&DWP(0,"esp"));
	&xor	($AH[1],"edi");		#&mov	($AH[1],&DWP(4,"esp"));
					#&mov	("edi", &DWP(8,"esp"));
	&mov	("ecx",&DWP(12,"esp"));
	&add	($AH[0],&DWP(0,"esi"));
	&add	($AH[1],&DWP(4,"esi"));
	&add	("edi",&DWP(8,"esi"));
	&add	("ecx",&DWP(12,"esi"));
	&mov	(&DWP(0,"esi"),$AH[0]);
	&mov	(&DWP(4,"esi"),$AH[1]);
	&mov	(&DWP(8,"esi"),"edi");
	&mov	(&DWP(12,"esi"),"ecx");
	 #&mov	(&DWP(0,"esp"),$AH[0]);
	 &mov	(&DWP(4,"esp"),$AH[1]);
	 &xor	($AH[1],"edi");		# magic
	 &mov	(&DWP(8,"esp"),"edi");
	 &mov	(&DWP(12,"esp"),"ecx");
	#&mov	($E,&DWP(16,"esp"));
	&mov	("edi",&DWP(20,"esp"));
	&mov	("ebx",&DWP(24,"esp"));
	&mov	("ecx",&DWP(28,"esp"));
	&add	($E,&DWP(16,"esi"));
	&add	("edi",&DWP(20,"esi"));
	&add	("ebx",&DWP(24,"esi"));
	&add	("ecx",&DWP(28,"esi"));
	&mov	(&DWP(16,"esi"),$E);
	&mov	(&DWP(20,"esi"),"edi");
	&mov	(&DWP(24,"esi"),"ebx");
	&mov	(&DWP(28,"esi"),"ecx");
	 #&mov	(&DWP(16,"esp"),$E);
	 &mov	(&DWP(20,"esp"),"edi");
	&mov	("edi",&DWP(96+4,"esp"));	# inp
	 &mov	(&DWP(24,"esp"),"ebx");
	 &mov	(&DWP(28,"esp"),"ecx");

	&cmp	("edi",&DWP(96+8,"esp"));	# are we done yet?
	&jb	(&label("grand_loop"));

	&mov	("esp",&DWP(96+12,"esp"));	# restore sp
&function_end_A();
}
						if (!$i386 && $xmm) {{{
if ($shaext) {
######################################################################
# Intel SHA Extensions implementation of SHA256 update function.
#
my ($ctx,$inp,$end)=("esi","edi","eax");
my ($Wi,$ABEF,$CDGH,$TMP)=map("xmm$_",(0..2,7));
my @MSG=map("xmm$_",(3..6));

sub sha256op38 {
 my ($opcodelet,$dst,$src)=@_;
    if ("$dst:$src" =~ /xmm([0-7]):xmm([0-7])/)
    {	&data_byte(0x0f,0x38,$opcodelet,0xc0|($1<<3)|$2);	}
}
sub sha256rnds2	{ sha256op38(0xcb,@_); }
sub sha256msg1	{ sha256op38(0xcc,@_); }
sub sha256msg2	{ sha256op38(0xcd,@_); }

&set_label("shaext",32);
	&sub		("esp",32);

	&movdqu		($ABEF,&QWP(0,$ctx));		# DCBA
	&lea		($K256,&DWP(0x80,$K256));
	&movdqu		($CDGH,&QWP(16,$ctx));		# HGFE
	&movdqa		($TMP,&QWP(0x100-0x80,$K256));	# byte swap mask

	&pshufd		($Wi,$ABEF,0x1b);		# ABCD
	&pshufd		($ABEF,$ABEF,0xb1);		# CDAB
	&pshufd		($CDGH,$CDGH,0x1b);		# EFGH
	&palignr	($ABEF,$CDGH,8);		# ABEF
	&punpcklqdq	($CDGH,$Wi);			# CDGH
	&jmp		(&label("loop_shaext"));

&set_label("loop_shaext",16);
	&movdqu		(@MSG[0],&QWP(0,$inp));
	&movdqu		(@MSG[1],&QWP(0x10,$inp));
	&movdqu		(@MSG[2],&QWP(0x20,$inp));
	&pshufb		(@MSG[0],$TMP);
	&movdqu		(@MSG[3],&QWP(0x30,$inp));
	&movdqa		(&QWP(16,"esp"),$CDGH);		# offload

	&movdqa		($Wi,&QWP(0*16-0x80,$K256));
	&paddd		($Wi,@MSG[0]);
	&pshufb		(@MSG[1],$TMP);
	&sha256rnds2	($CDGH,$ABEF);			# 0-3
	&pshufd		($Wi,$Wi,0x0e);
	&nop		();
	&movdqa		(&QWP(0,"esp"),$ABEF);		# offload
	&sha256rnds2	($ABEF,$CDGH);

	&movdqa		($Wi,&QWP(1*16-0x80,$K256));
	&paddd		($Wi,@MSG[1]);
	&pshufb		(@MSG[2],$TMP);
	&sha256rnds2	($CDGH,$ABEF);			# 4-7
	&pshufd		($Wi,$Wi,0x0e);
	&lea		($inp,&DWP(0x40,$inp));
	&sha256msg1	(@MSG[0],@MSG[1]);
	&sha256rnds2	($ABEF,$CDGH);

	&movdqa		($Wi,&QWP(2*16-0x80,$K256));
	&paddd		($Wi,@MSG[2]);
	&pshufb		(@MSG[3],$TMP);
	&sha256rnds2	($CDGH,$ABEF);			# 8-11
	&pshufd		($Wi,$Wi,0x0e);
	&movdqa		($TMP,@MSG[3]);
	&palignr	($TMP,@MSG[2],4);
	&nop		();
	&paddd		(@MSG[0],$TMP);
	&sha256msg1	(@MSG[1],@MSG[2]);
	&sha256rnds2	($ABEF,$CDGH);

	&movdqa		($Wi,&QWP(3*16-0x80,$K256));
	&paddd		($Wi,@MSG[3]);
	&sha256msg2	(@MSG[0],@MSG[3]);
	&sha256rnds2	($CDGH,$ABEF);			# 12-15
	&pshufd		($Wi,$Wi,0x0e);
	&movdqa		($TMP,@MSG[0]);
	&palignr	($TMP,@MSG[3],4);
	&nop		();
	&paddd		(@MSG[1],$TMP);
	&sha256msg1	(@MSG[2],@MSG[3]);
	&sha256rnds2	($ABEF,$CDGH);

for($i=4;$i<16-3;$i++) {
	&movdqa		($Wi,&QWP($i*16-0x80,$K256));
	&paddd		($Wi,@MSG[0]);
	&sha256msg2	(@MSG[1],@MSG[0]);
	&sha256rnds2	($CDGH,$ABEF);			# 16-19...
	&pshufd		($Wi,$Wi,0x0e);
	&movdqa		($TMP,@MSG[1]);
	&palignr	($TMP,@MSG[0],4);
	&nop		();
	&paddd		(@MSG[2],$TMP);
	&sha256msg1	(@MSG[3],@MSG[0]);
	&sha256rnds2	($ABEF,$CDGH);

	push(@MSG,shift(@MSG));
}
	&movdqa		($Wi,&QWP(13*16-0x80,$K256));
	&paddd		($Wi,@MSG[0]);
	&sha256msg2	(@MSG[1],@MSG[0]);
	&sha256rnds2	($CDGH,$ABEF);			# 52-55
	&pshufd		($Wi,$Wi,0x0e);
	&movdqa		($TMP,@MSG[1])
	&palignr	($TMP,@MSG[0],4);
	&sha256rnds2	($ABEF,$CDGH);
	&paddd		(@MSG[2],$TMP);

	&movdqa		($Wi,&QWP(14*16-0x80,$K256));
	&paddd		($Wi,@MSG[1]);
	&sha256rnds2	($CDGH,$ABEF);			# 56-59
	&pshufd		($Wi,$Wi,0x0e);
	&sha256msg2	(@MSG[2],@MSG[1]);
	&movdqa		($TMP,&QWP(0x100-0x80,$K256));	# byte swap mask
	&sha256rnds2	($ABEF,$CDGH);

	&movdqa		($Wi,&QWP(15*16-0x80,$K256));
	&paddd		($Wi,@MSG[2]);
	&nop		();
	&sha256rnds2	($CDGH,$ABEF);			# 60-63
	&pshufd		($Wi,$Wi,0x0e);
	&cmp		($end,$inp);
	&nop		();
	&sha256rnds2	($ABEF,$CDGH);

	&paddd		($CDGH,&QWP(16,"esp"));
	&paddd		($ABEF,&QWP(0,"esp"));
	&jnz		(&label("loop_shaext"));

	&pshufd		($CDGH,$CDGH,0xb1);		# DCHG
	&pshufd		($TMP,$ABEF,0x1b);		# FEBA
	&pshufd		($ABEF,$ABEF,0xb1);		# BAFE
	&punpckhqdq	($ABEF,$CDGH);			# DCBA
	&palignr	($CDGH,$TMP,8);			# HGFE

	&mov		("esp",&DWP(32+12,"esp"));
	&movdqu		(&QWP(0,$ctx),$ABEF);
	&movdqu		(&QWP(16,$ctx),$CDGH);
&function_end_A();
}

my @X = map("xmm$_",(0..3));
my ($t0,$t1,$t2,$t3) = map("xmm$_",(4..7));
my @AH = ($A,$T);

&set_label("SSSE3",32);
	&lea	("esp",&DWP(-96,"esp"));
	# copy ctx->h[0-7] to A,B,C,D,E,F,G,H on stack
	&mov	($AH[0],&DWP(0,"esi"));
	&mov	($AH[1],&DWP(4,"esi"));
	&mov	("ecx",&DWP(8,"esi"));
	&mov	("edi",&DWP(12,"esi"));
	#&mov	(&DWP(0,"esp"),$AH[0]);
	&mov	(&DWP(4,"esp"),$AH[1]);
	&xor	($AH[1],"ecx");			# magic
	&mov	(&DWP(8,"esp"),"ecx");
	&mov	(&DWP(12,"esp"),"edi");
	&mov	($E,&DWP(16,"esi"));
	&mov	("edi",&DWP(20,"esi"));
	&mov	("ecx",&DWP(24,"esi"));
	&mov	("esi",&DWP(28,"esi"));
	#&mov	(&DWP(16,"esp"),$E);
	&mov	(&DWP(20,"esp"),"edi");
	&mov	("edi",&DWP(96+4,"esp"));	# inp
	&mov	(&DWP(24,"esp"),"ecx");
	&mov	(&DWP(28,"esp"),"esi");
	&movdqa	($t3,&QWP(256,$K256));
	&jmp	(&label("grand_ssse3"));

&set_label("grand_ssse3",16);
	# load input, reverse byte order, add K256[0..15], save to stack
	&movdqu	(@X[0],&QWP(0,"edi"));
	&movdqu	(@X[1],&QWP(16,"edi"));
	&movdqu	(@X[2],&QWP(32,"edi"));
	&movdqu	(@X[3],&QWP(48,"edi"));
	&add	("edi",64);
	&pshufb	(@X[0],$t3);
	&mov	(&DWP(96+4,"esp"),"edi");
	&pshufb	(@X[1],$t3);
	&movdqa	($t0,&QWP(0,$K256));
	&pshufb	(@X[2],$t3);
	&movdqa	($t1,&QWP(16,$K256));
	&paddd	($t0,@X[0]);
	&pshufb	(@X[3],$t3);
	&movdqa	($t2,&QWP(32,$K256));
	&paddd	($t1,@X[1]);
	&movdqa	($t3,&QWP(48,$K256));
	&movdqa	(&QWP(32+0,"esp"),$t0);
	&paddd	($t2,@X[2]);
	&movdqa	(&QWP(32+16,"esp"),$t1);
	&paddd	($t3,@X[3]);
	&movdqa	(&QWP(32+32,"esp"),$t2);
	&movdqa	(&QWP(32+48,"esp"),$t3);
	&jmp	(&label("ssse3_00_47"));

&set_label("ssse3_00_47",16);
	&add		($K256,64);

sub SSSE3_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body,&$body,&$body);	# 120 instructions

	  eval(shift(@insns));
	&movdqa		($t0,@X[1]);
	  eval(shift(@insns));			# @
	  eval(shift(@insns));
	&movdqa		($t3,@X[3]);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&palignr	($t0,@X[0],4);		# X[1..4]
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	  eval(shift(@insns));
	 &palignr	($t3,@X[2],4);		# X[9..12]
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&movdqa		($t1,$t0);
	  eval(shift(@insns));			# @
	  eval(shift(@insns));
	&movdqa		($t2,$t0);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&psrld		($t0,3);
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	 &paddd		(@X[0],$t3);		# X[0..3] += X[9..12]
	  eval(shift(@insns));
	  eval(shift(@insns));
	&psrld		($t2,7);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	  eval(shift(@insns));
	 &pshufd	($t3,@X[3],0b11111010);	# X[14..15]
	  eval(shift(@insns));
	  eval(shift(@insns));
	&pslld		($t1,32-18);
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	&pxor		($t0,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&psrld		($t2,18-7);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	&pxor		($t0,$t1);
	  eval(shift(@insns));
	  eval(shift(@insns));
	&pslld		($t1,18-7);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	&pxor		($t0,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &movdqa	($t2,$t3);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	&pxor		($t0,$t1);		# sigma0(X[1..4])
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &psrld		($t3,10);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	&paddd		(@X[0],$t0);		# X[0..3] += sigma0(X[1..4])
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &psrlq		($t2,17);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	 &pxor		($t3,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &psrlq		($t2,19-17);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	 &pxor		($t3,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &pshufd	($t3,$t3,0b10000000);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	  eval(shift(@insns));
	 &psrldq	($t3,8);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&paddd		(@X[0],$t3);		# X[0..1] += sigma1(X[14..15])
	  eval(shift(@insns));			# @
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	  eval(shift(@insns));
	 &pshufd	($t3,@X[0],0b01010000);	# X[16..17]
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &movdqa	($t2,$t3);
	  eval(shift(@insns));			# @
	 &psrld		($t3,10);
	  eval(shift(@insns));
	 &psrlq		($t2,17);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	 &pxor		($t3,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &psrlq		($t2,19-17);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	 &pxor		($t3,$t2);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &pshufd	($t3,$t3,0b00001000);
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	&movdqa		($t2,&QWP(16*$j,$K256));
	  eval(shift(@insns));
	  eval(shift(@insns));
	 &pslldq	($t3,8);
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));			# @
	&paddd		(@X[0],$t3);		# X[2..3] += sigma1(X[16..17])
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	  eval(shift(@insns));
	&paddd		($t2,@X[0]);
	  eval(shift(@insns));			# @

	foreach (@insns) { eval; }		# remaining instructions

	&movdqa		(&QWP(32+16*$j,"esp"),$t2);
}

sub body_00_15 () {
	(
	'&mov	("ecx",$E);',
	'&ror	($E,25-11);',
	 '&mov	("esi",&off($f));',
	'&xor	($E,"ecx");',
	 '&mov	("edi",&off($g));',
	 '&xor	("esi","edi");',
	'&ror	($E,11-6);',
	 '&and	("esi","ecx");',
	 '&mov	(&off($e),"ecx");',	# save $E, modulo-scheduled
	'&xor	($E,"ecx");',
	 '&xor	("edi","esi");',	# Ch(e,f,g)
	'&ror	($E,6);',		# T = Sigma1(e)
	 '&mov	("ecx",$AH[0]);',
	 '&add	($E,"edi");',		# T += Ch(e,f,g)
	 '&mov	("edi",&off($b));',
	'&mov	("esi",$AH[0]);',

	'&ror	("ecx",22-13);',
	 '&mov	(&off($a),$AH[0]);',	# save $A, modulo-scheduled
	'&xor	("ecx",$AH[0]);',
	 '&xor	($AH[0],"edi");',	# a ^= b, (b^c) in next round
	 '&add	($E,&off($h));',	# T += h
	'&ror	("ecx",13-2);',
	 '&and	($AH[1],$AH[0]);',	# (b^c) &= (a^b)
	'&xor	("ecx","esi");',
	 '&add	($E,&DWP(32+4*($i&15),"esp"));',	# T += K[i]+X[i]
	 '&xor	($AH[1],"edi");',	# h = Maj(a,b,c) = Ch(a^b,c,b)
	'&ror	("ecx",2);',		# Sigma0(a)

	 '&add	($AH[1],$E);',		# h += T
	 '&add	($E,&off($d));',	# d += T
	'&add	($AH[1],"ecx");'.	# h += Sigma0(a)

	'@AH = reverse(@AH); $i++;'	# rotate(a,h)
	);
}

    for ($i=0,$j=0; $j<4; $j++) {
	&SSSE3_00_47($j,\&body_00_15,@X);
	push(@X,shift(@X));		# rotate(@X)
    }
	&cmp	(&DWP(16*$j,$K256),0x00010203);
	&jne	(&label("ssse3_00_47"));

    for ($i=0; $i<16; ) {
	foreach(body_00_15()) { eval; }
    }

	&mov	("esi",&DWP(96,"esp"));	#ctx
					#&mov	($AH[0],&DWP(0,"esp"));
	&xor	($AH[1],"edi");		#&mov	($AH[1],&DWP(4,"esp"));
					#&mov	("edi", &DWP(8,"esp"));
	&mov	("ecx",&DWP(12,"esp"));
	&add	($AH[0],&DWP(0,"esi"));
	&add	($AH[1],&DWP(4,"esi"));
	&add	("edi",&DWP(8,"esi"));
	&add	("ecx",&DWP(12,"esi"));
	&mov	(&DWP(0,"esi"),$AH[0]);
	&mov	(&DWP(4,"esi"),$AH[1]);
	&mov	(&DWP(8,"esi"),"edi");
	&mov	(&DWP(12,"esi"),"ecx");
	 #&mov	(&DWP(0,"esp"),$AH[0]);
	 &mov	(&DWP(4,"esp"),$AH[1]);
	 &xor	($AH[1],"edi");			# magic
	 &mov	(&DWP(8,"esp"),"edi");
	 &mov	(&DWP(12,"esp"),"ecx");
	#&mov	($E,&DWP(16,"esp"));
	&mov	("edi",&DWP(20,"esp"));
	&mov	("ecx",&DWP(24,"esp"));
	&add	($E,&DWP(16,"esi"));
	&add	("edi",&DWP(20,"esi"));
	&add	("ecx",&DWP(24,"esi"));
	&mov	(&DWP(16,"esi"),$E);
	&mov	(&DWP(20,"esi"),"edi");
	 &mov	(&DWP(20,"esp"),"edi");
	&mov	("edi",&DWP(28,"esp"));
	&mov	(&DWP(24,"esi"),"ecx");
	 #&mov	(&DWP(16,"esp"),$E);
	&add	("edi",&DWP(28,"esi"));
	 &mov	(&DWP(24,"esp"),"ecx");
	&mov	(&DWP(28,"esi"),"edi");
	 &mov	(&DWP(28,"esp"),"edi");
	&mov	("edi",&DWP(96+4,"esp"));	# inp

	&movdqa	($t3,&QWP(64,$K256));
	&sub	($K256,3*64);			# rewind K
	&cmp	("edi",&DWP(96+8,"esp"));	# are we done yet?
	&jb	(&label("grand_ssse3"));

	&mov	("esp",&DWP(96+12,"esp"));	# restore sp
&function_end_A();
						if ($avx) {
&set_label("AVX",32);
						if ($avx>1) {
	&and	("edx",1<<8|1<<3);		# check for BMI2+BMI1
	&cmp	("edx",1<<8|1<<3);
	&je	(&label("AVX_BMI"));
						}
	&lea	("esp",&DWP(-96,"esp"));
	&vzeroall	();
	# copy ctx->h[0-7] to A,B,C,D,E,F,G,H on stack
	&mov	($AH[0],&DWP(0,"esi"));
	&mov	($AH[1],&DWP(4,"esi"));
	&mov	("ecx",&DWP(8,"esi"));
	&mov	("edi",&DWP(12,"esi"));
	#&mov	(&DWP(0,"esp"),$AH[0]);
	&mov	(&DWP(4,"esp"),$AH[1]);
	&xor	($AH[1],"ecx");			# magic
	&mov	(&DWP(8,"esp"),"ecx");
	&mov	(&DWP(12,"esp"),"edi");
	&mov	($E,&DWP(16,"esi"));
	&mov	("edi",&DWP(20,"esi"));
	&mov	("ecx",&DWP(24,"esi"));
	&mov	("esi",&DWP(28,"esi"));
	#&mov	(&DWP(16,"esp"),$E);
	&mov	(&DWP(20,"esp"),"edi");
	&mov	("edi",&DWP(96+4,"esp"));	# inp
	&mov	(&DWP(24,"esp"),"ecx");
	&mov	(&DWP(28,"esp"),"esi");
	&vmovdqa	($t3,&QWP(256,$K256));
	&jmp	(&label("grand_avx"));

&set_label("grand_avx",32);
	# load input, reverse byte order, add K256[0..15], save to stack
	&vmovdqu	(@X[0],&QWP(0,"edi"));
	&vmovdqu	(@X[1],&QWP(16,"edi"));
	&vmovdqu	(@X[2],&QWP(32,"edi"));
	&vmovdqu	(@X[3],&QWP(48,"edi"));
	&add		("edi",64);
	&vpshufb	(@X[0],@X[0],$t3);
	&mov		(&DWP(96+4,"esp"),"edi");
	&vpshufb	(@X[1],@X[1],$t3);
	&vpshufb	(@X[2],@X[2],$t3);
	&vpaddd		($t0,@X[0],&QWP(0,$K256));
	&vpshufb	(@X[3],@X[3],$t3);
	&vpaddd		($t1,@X[1],&QWP(16,$K256));
	&vpaddd		($t2,@X[2],&QWP(32,$K256));
	&vpaddd		($t3,@X[3],&QWP(48,$K256));
	&vmovdqa	(&QWP(32+0,"esp"),$t0);
	&vmovdqa	(&QWP(32+16,"esp"),$t1);
	&vmovdqa	(&QWP(32+32,"esp"),$t2);
	&vmovdqa	(&QWP(32+48,"esp"),$t3);
	&jmp		(&label("avx_00_47"));

&set_label("avx_00_47",16);
	&add		($K256,64);

sub Xupdate_AVX () {
	(
	'&vpalignr	($t0,@X[1],@X[0],4);',	# X[1..4]
	 '&vpalignr	($t3,@X[3],@X[2],4);',	# X[9..12]
	'&vpsrld	($t2,$t0,7);',
	 '&vpaddd	(@X[0],@X[0],$t3);',	# X[0..3] += X[9..16]
	'&vpsrld	($t3,$t0,3);',
	'&vpslld	($t1,$t0,14);',
	'&vpxor		($t0,$t3,$t2);',
	 '&vpshufd	($t3,@X[3],0b11111010)',# X[14..15]
	'&vpsrld	($t2,$t2,18-7);',
	'&vpxor		($t0,$t0,$t1);',
	'&vpslld	($t1,$t1,25-14);',
	'&vpxor		($t0,$t0,$t2);',
	 '&vpsrld	($t2,$t3,10);',
	'&vpxor		($t0,$t0,$t1);',	# sigma0(X[1..4])
	 '&vpsrlq	($t1,$t3,17);',
	'&vpaddd	(@X[0],@X[0],$t0);',	# X[0..3] += sigma0(X[1..4])
	 '&vpxor	($t2,$t2,$t1);',
	 '&vpsrlq	($t3,$t3,19);',
	 '&vpxor	($t2,$t2,$t3);',	# sigma1(X[14..15]
	 '&vpshufd	($t3,$t2,0b10000100);',
	'&vpsrldq	($t3,$t3,8);',
	'&vpaddd	(@X[0],@X[0],$t3);',	# X[0..1] += sigma1(X[14..15])
	 '&vpshufd	($t3,@X[0],0b01010000)',# X[16..17]
	 '&vpsrld	($t2,$t3,10);',
	 '&vpsrlq	($t1,$t3,17);',
	 '&vpxor	($t2,$t2,$t1);',
	 '&vpsrlq	($t3,$t3,19);',
	 '&vpxor	($t2,$t2,$t3);',	# sigma1(X[16..17]
	 '&vpshufd	($t3,$t2,0b11101000);',
	'&vpslldq	($t3,$t3,8);',
	'&vpaddd	(@X[0],@X[0],$t3);'	# X[2..3] += sigma1(X[16..17])
	);
}

local *ror = sub { &shrd(@_[0],@_) };
sub AVX_00_47 () {
my $j = shift;
my $body = shift;
my @X = @_;
my @insns = (&$body,&$body,&$body,&$body);	# 120 instructions
my $insn;

	foreach (Xupdate_AVX()) {		# 31 instructions
	    eval;
	    eval(shift(@insns));
	    eval(shift(@insns));
	    eval($insn = shift(@insns));
	    eval(shift(@insns)) if ($insn =~ /rorx/ && @insns[0] =~ /rorx/);
	}
	&vpaddd		($t2,@X[0],&QWP(16*$j,$K256));
	foreach (@insns) { eval; }		# remaining instructions
	&vmovdqa	(&QWP(32+16*$j,"esp"),$t2);
}

    for ($i=0,$j=0; $j<4; $j++) {
	&AVX_00_47($j,\&body_00_15,@X);
	push(@X,shift(@X));		# rotate(@X)
    }
	&cmp	(&DWP(16*$j,$K256),0x00010203);
	&jne	(&label("avx_00_47"));

    for ($i=0; $i<16; ) {
	foreach(body_00_15()) { eval; }
    }

	&mov	("esi",&DWP(96,"esp"));	#ctx
					#&mov	($AH[0],&DWP(0,"esp"));
	&xor	($AH[1],"edi");		#&mov	($AH[1],&DWP(4,"esp"));
					#&mov	("edi", &DWP(8,"esp"));
	&mov	("ecx",&DWP(12,"esp"));
	&add	($AH[0],&DWP(0,"esi"));
	&add	($AH[1],&DWP(4,"esi"));
	&add	("edi",&DWP(8,"esi"));
	&add	("ecx",&DWP(12,"esi"));
	&mov	(&DWP(0,"esi"),$AH[0]);
	&mov	(&DWP(4,"esi"),$AH[1]);
	&mov	(&DWP(8,"esi"),"edi");
	&mov	(&DWP(12,"esi"),"ecx");
	 #&mov	(&DWP(0,"esp"),$AH[0]);
	 &mov	(&DWP(4,"esp"),$AH[1]);
	 &xor	($AH[1],"edi");			# magic
	 &mov	(&DWP(8,"esp"),"edi");
	 &mov	(&DWP(12,"esp"),"ecx");
	#&mov	($E,&DWP(16,"esp"));
	&mov	("edi",&DWP(20,"esp"));
	&mov	("ecx",&DWP(24,"esp"));
	&add	($E,&DWP(16,"esi"));
	&add	("edi",&DWP(20,"esi"));
	&add	("ecx",&DWP(24,"esi"));
	&mov	(&DWP(16,"esi"),$E);
	&mov	(&DWP(20,"esi"),"edi");
	 &mov	(&DWP(20,"esp"),"edi");
	&mov	("edi",&DWP(28,"esp"));
	&mov	(&DWP(24,"esi"),"ecx");
	 #&mov	(&DWP(16,"esp"),$E);
	&add	("edi",&DWP(28,"esi"));
	 &mov	(&DWP(24,"esp"),"ecx");
	&mov	(&DWP(28,"esi"),"edi");
	 &mov	(&DWP(28,"esp"),"edi");
	&mov	("edi",&DWP(96+4,"esp"));	# inp

	&vmovdqa	($t3,&QWP(64,$K256));
	&sub	($K256,3*64);			# rewind K
	&cmp	("edi",&DWP(96+8,"esp"));	# are we done yet?
	&jb	(&label("grand_avx"));

	&mov	("esp",&DWP(96+12,"esp"));	# restore sp
	&vzeroall	();
&function_end_A();
						if ($avx>1) {
sub bodyx_00_15 () {			# +10%
	(
	'&rorx	("ecx",$E,6)',
	'&rorx	("esi",$E,11)',
	 '&mov	(&off($e),$E)',		# save $E, modulo-scheduled
	'&rorx	("edi",$E,25)',
	'&xor	("ecx","esi")',
	 '&andn	("esi",$E,&off($g))',
	'&xor	("ecx","edi")',		# Sigma1(e)
	 '&and	($E,&off($f))',
	 '&mov	(&off($a),$AH[0]);',	# save $A, modulo-scheduled
	 '&or	($E,"esi")',		# T = Ch(e,f,g)

	'&rorx	("edi",$AH[0],2)',
	'&rorx	("esi",$AH[0],13)',
	 '&lea	($E,&DWP(0,$E,"ecx"))',	# T += Sigma1(e)
	'&rorx	("ecx",$AH[0],22)',
	'&xor	("esi","edi")',
	 '&mov	("edi",&off($b))',
	'&xor	("ecx","esi")',		# Sigma0(a)

	 '&xor	($AH[0],"edi")',	# a ^= b, (b^c) in next round
	 '&add	($E,&off($h))',		# T += h
	 '&and	($AH[1],$AH[0])',	# (b^c) &= (a^b)
	 '&add	($E,&DWP(32+4*($i&15),"esp"))',	# T += K[i]+X[i]
	 '&xor	($AH[1],"edi")',	# h = Maj(a,b,c) = Ch(a^b,c,b)

	 '&add	("ecx",$E)',		# h += T
	 '&add	($E,&off($d))',		# d += T
	'&lea	($AH[1],&DWP(0,$AH[1],"ecx"));'.	# h += Sigma0(a)

	'@AH = reverse(@AH); $i++;'	# rotate(a,h)
	);
}

&set_label("AVX_BMI",32);
	&lea	("esp",&DWP(-96,"esp"));
	&vzeroall	();
	# copy ctx->h[0-7] to A,B,C,D,E,F,G,H on stack
	&mov	($AH[0],&DWP(0,"esi"));
	&mov	($AH[1],&DWP(4,"esi"));
	&mov	("ecx",&DWP(8,"esi"));
	&mov	("edi",&DWP(12,"esi"));
	#&mov	(&DWP(0,"esp"),$AH[0]);
	&mov	(&DWP(4,"esp"),$AH[1]);
	&xor	($AH[1],"ecx");			# magic
	&mov	(&DWP(8,"esp"),"ecx");
	&mov	(&DWP(12,"esp"),"edi");
	&mov	($E,&DWP(16,"esi"));
	&mov	("edi",&DWP(20,"esi"));
	&mov	("ecx",&DWP(24,"esi"));
	&mov	("esi",&DWP(28,"esi"));
	#&mov	(&DWP(16,"esp"),$E);
	&mov	(&DWP(20,"esp"),"edi");
	&mov	("edi",&DWP(96+4,"esp"));	# inp
	&mov	(&DWP(24,"esp"),"ecx");
	&mov	(&DWP(28,"esp"),"esi");
	&vmovdqa	($t3,&QWP(256,$K256));
	&jmp	(&label("grand_avx_bmi"));

&set_label("grand_avx_bmi",32);
	# load input, reverse byte order, add K256[0..15], save to stack
	&vmovdqu	(@X[0],&QWP(0,"edi"));
	&vmovdqu	(@X[1],&QWP(16,"edi"));
	&vmovdqu	(@X[2],&QWP(32,"edi"));
	&vmovdqu	(@X[3],&QWP(48,"edi"));
	&add		("edi",64);
	&vpshufb	(@X[0],@X[0],$t3);
	&mov		(&DWP(96+4,"esp"),"edi");
	&vpshufb	(@X[1],@X[1],$t3);
	&vpshufb	(@X[2],@X[2],$t3);
	&vpaddd		($t0,@X[0],&QWP(0,$K256));
	&vpshufb	(@X[3],@X[3],$t3);
	&vpaddd		($t1,@X[1],&QWP(16,$K256));
	&vpaddd		($t2,@X[2],&QWP(32,$K256));
	&vpaddd		($t3,@X[3],&QWP(48,$K256));
	&vmovdqa	(&QWP(32+0,"esp"),$t0);
	&vmovdqa	(&QWP(32+16,"esp"),$t1);
	&vmovdqa	(&QWP(32+32,"esp"),$t2);
	&vmovdqa	(&QWP(32+48,"esp"),$t3);
	&jmp		(&label("avx_bmi_00_47"));

&set_label("avx_bmi_00_47",16);
	&add		($K256,64);

    for ($i=0,$j=0; $j<4; $j++) {
	&AVX_00_47($j,\&bodyx_00_15,@X);
	push(@X,shift(@X));		# rotate(@X)
    }
	&cmp	(&DWP(16*$j,$K256),0x00010203);
	&jne	(&label("avx_bmi_00_47"));

    for ($i=0; $i<16; ) {
	foreach(bodyx_00_15()) { eval; }
    }

	&mov	("esi",&DWP(96,"esp"));	#ctx
					#&mov	($AH[0],&DWP(0,"esp"));
	&xor	($AH[1],"edi");		#&mov	($AH[1],&DWP(4,"esp"));
					#&mov	("edi", &DWP(8,"esp"));
	&mov	("ecx",&DWP(12,"esp"));
	&add	($AH[0],&DWP(0,"esi"));
	&add	($AH[1],&DWP(4,"esi"));
	&add	("edi",&DWP(8,"esi"));
	&add	("ecx",&DWP(12,"esi"));
	&mov	(&DWP(0,"esi"),$AH[0]);
	&mov	(&DWP(4,"esi"),$AH[1]);
	&mov	(&DWP(8,"esi"),"edi");
	&mov	(&DWP(12,"esi"),"ecx");
	 #&mov	(&DWP(0,"esp"),$AH[0]);
	 &mov	(&DWP(4,"esp"),$AH[1]);
	 &xor	($AH[1],"edi");			# magic
	 &mov	(&DWP(8,"esp"),"edi");
	 &mov	(&DWP(12,"esp"),"ecx");
	#&mov	($E,&DWP(16,"esp"));
	&mov	("edi",&DWP(20,"esp"));
	&mov	("ecx",&DWP(24,"esp"));
	&add	($E,&DWP(16,"esi"));
	&add	("edi",&DWP(20,"esi"));
	&add	("ecx",&DWP(24,"esi"));
	&mov	(&DWP(16,"esi"),$E);
	&mov	(&DWP(20,"esi"),"edi");
	 &mov	(&DWP(20,"esp"),"edi");
	&mov	("edi",&DWP(28,"esp"));
	&mov	(&DWP(24,"esi"),"ecx");
	 #&mov	(&DWP(16,"esp"),$E);
	&add	("edi",&DWP(28,"esi"));
	 &mov	(&DWP(24,"esp"),"ecx");
	&mov	(&DWP(28,"esi"),"edi");
	 &mov	(&DWP(28,"esp"),"edi");
	&mov	("edi",&DWP(96+4,"esp"));	# inp

	&vmovdqa	($t3,&QWP(64,$K256));
	&sub	($K256,3*64);			# rewind K
	&cmp	("edi",&DWP(96+8,"esp"));	# are we done yet?
	&jb	(&label("grand_avx_bmi"));

	&mov	("esp",&DWP(96+12,"esp"));	# restore sp
	&vzeroall	();
&function_end_A();
						}
						}
						}}}
&function_end_B("sha256_block_data_order");

&asm_finish();

close STDOUT;
