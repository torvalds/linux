#! /usr/bin/env perl
# Copyright 2008-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# ====================================================================
# Copyright (c) 2008 Andy Polyakov <appro@openssl.org>
#
# This module may be used under the terms of either the GNU General
# Public License version 2 or later, the GNU Lesser General Public
# License version 2.1 or later, the Mozilla Public License version
# 1.1 or the BSD License. The exact terms of either license are
# distributed along with this module. For further details see
# http://www.openssl.org/~appro/camellia/.
# ====================================================================

# Performance in cycles per processed byte (less is better) in
# 'openssl speed ...' benchmark:
#
#			AMD K8	Core2	PIII	P4
# -evp camellia-128-ecb	21.5	22.8	27.0	28.9
# + over gcc 3.4.6	+90/11% +70/10%	+53/4%	+160/64%
# + over icc 8.0	+48/19% +21/15%	+21/17%	+55/37%
#
# camellia-128-cbc	17.3	21.1	23.9	25.9
#
# 128-bit key setup	196	280	256	240	cycles/key
# + over gcc 3.4.6	+30/0%	+17/11%	+11/0%	+63/40%
# + over icc 8.0	+18/3%	+10/0%	+10/3%	+21/10%
#
# Pairs of numbers in "+" rows represent performance improvement over
# compiler generated position-independent code, PIC, and non-PIC
# respectively. PIC results are of greater relevance, as this module
# is position-independent, i.e. suitable for a shared library or PIE.
# Position independence "costs" one register, which is why compilers
# are so close with non-PIC results, they have an extra register to
# spare. CBC results are better than ECB ones thanks to "zero-copy"
# private _x86_* interface, and are ~30-40% better than with compiler
# generated cmll_cbc.o, and reach ~80-90% of x86_64 performance on
# same CPU (where applicable).

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

$OPENSSL=1;

$output = pop;
open STDOUT,">$output";

&asm_init($ARGV[0],$ARGV[$#ARGV] eq "386");

@T=("eax","ebx","ecx","edx");
$idx="esi";
$key="edi";
$Tbl="ebp";

# stack frame layout in _x86_Camellia_* routines, frame is allocated
# by caller
$__ra=&DWP(0,"esp");	# return address
$__s0=&DWP(4,"esp");	# s0 backing store
$__s1=&DWP(8,"esp");	# s1 backing store
$__s2=&DWP(12,"esp");	# s2 backing store
$__s3=&DWP(16,"esp");	# s3 backing store
$__end=&DWP(20,"esp");	# pointer to end/start of key schedule

# stack frame layout in Camellia_[en|crypt] routines, which differs from
# above by 4 and overlaps by pointer to end/start of key schedule
$_end=&DWP(16,"esp");
$_esp=&DWP(20,"esp");

# const unsigned int Camellia_SBOX[4][256];
# Well, sort of... Camellia_SBOX[0][] is interleaved with [1][],
# and [2][] - with [3][]. This is done to optimize code size.
$SBOX1_1110=0;		# Camellia_SBOX[0]
$SBOX4_4404=4;		# Camellia_SBOX[1]
$SBOX2_0222=2048;	# Camellia_SBOX[2]
$SBOX3_3033=2052;	# Camellia_SBOX[3]
&static_label("Camellia_SIGMA");
&static_label("Camellia_SBOX");

sub Camellia_Feistel {
my $i=@_[0];
my $seed=defined(@_[1])?@_[1]:0;
my $scale=$seed<0?-8:8;
my $frame=defined(@_[2])?@_[2]:0;
my $j=($i&1)*2;
my $t0=@T[($j)%4],$t1=@T[($j+1)%4],$t2=@T[($j+2)%4],$t3=@T[($j+3)%4];

	&xor	($t0,$idx);				# t0^=key[0]
	&xor	($t1,&DWP($seed+$i*$scale+4,$key));	# t1^=key[1]
	&movz	($idx,&HB($t0));			# (t0>>8)&0xff
	&mov	($t3,&DWP($SBOX3_3033,$Tbl,$idx,8));	# t3=SBOX3_3033[0]
	&movz	($idx,&LB($t0));			# (t0>>0)&0xff
	&xor	($t3,&DWP($SBOX4_4404,$Tbl,$idx,8));	# t3^=SBOX4_4404[0]
	&shr	($t0,16);
	&movz	($idx,&LB($t1));			# (t1>>0)&0xff
	&mov	($t2,&DWP($SBOX1_1110,$Tbl,$idx,8));	# t2=SBOX1_1110[1]
	&movz	($idx,&HB($t0));			# (t0>>24)&0xff
	&xor	($t3,&DWP($SBOX1_1110,$Tbl,$idx,8));	# t3^=SBOX1_1110[0]
	&movz	($idx,&HB($t1));			# (t1>>8)&0xff
	&xor	($t2,&DWP($SBOX4_4404,$Tbl,$idx,8));	# t2^=SBOX4_4404[1]
	&shr	($t1,16);
	&movz	($t0,&LB($t0));				# (t0>>16)&0xff
	&xor	($t3,&DWP($SBOX2_0222,$Tbl,$t0,8));	# t3^=SBOX2_0222[0]
	&movz	($idx,&HB($t1));			# (t1>>24)&0xff
	&mov	($t0,&DWP($frame+4*(($j+3)%4),"esp"));	# prefetch "s3"
	&xor	($t2,$t3);				# t2^=t3
	&rotr	($t3,8);				# t3=RightRotate(t3,8)
	&xor	($t2,&DWP($SBOX2_0222,$Tbl,$idx,8));	# t2^=SBOX2_0222[1]
	&movz	($idx,&LB($t1));			# (t1>>16)&0xff
	&mov	($t1,&DWP($frame+4*(($j+2)%4),"esp"));	# prefetch "s2"
	&xor	($t3,$t0);				# t3^=s3
	&xor	($t2,&DWP($SBOX3_3033,$Tbl,$idx,8));	# t2^=SBOX3_3033[1]
	&mov	($idx,&DWP($seed+($i+1)*$scale,$key));	# prefetch key[i+1]
	&xor	($t3,$t2);				# t3^=t2
	&mov	(&DWP($frame+4*(($j+3)%4),"esp"),$t3);	# s3=t3
	&xor	($t2,$t1);				# t2^=s2
	&mov	(&DWP($frame+4*(($j+2)%4),"esp"),$t2);	# s2=t2
}

# void Camellia_EncryptBlock_Rounds(
#		int grandRounds,
#		const Byte plaintext[],
#		const KEY_TABLE_TYPE keyTable,
#		Byte ciphertext[])
&function_begin("Camellia_EncryptBlock_Rounds");
	&mov	("eax",&wparam(0));	# load grandRounds
	&mov	($idx,&wparam(1));	# load plaintext pointer
	&mov	($key,&wparam(2));	# load key schedule pointer

	&mov	("ebx","esp");
	&sub	("esp",7*4);		# place for s[0-3],keyEnd,esp and ra
	&and	("esp",-64);

	# place stack frame just "above mod 1024" the key schedule
	# this ensures that cache associativity of 2 suffices
	&lea	("ecx",&DWP(-64-63,$key));
	&sub	("ecx","esp");
	&neg	("ecx");
	&and	("ecx",0x3C0);	# modulo 1024, but aligned to cache-line
	&sub	("esp","ecx");
	&add	("esp",4);	# 4 is reserved for callee's return address

	&shl	("eax",6);
	&lea	("eax",&DWP(0,$key,"eax"));
	&mov	($_esp,"ebx");	# save %esp
	&mov	($_end,"eax");	# save keyEnd

	&call	(&label("pic_point"));
	&set_label("pic_point");
	&blindpop($Tbl);
	&lea	($Tbl,&DWP(&label("Camellia_SBOX")."-".&label("pic_point"),$Tbl));

	&mov	(@T[0],&DWP(0,$idx));	# load plaintext
	&mov	(@T[1],&DWP(4,$idx));
	&mov	(@T[2],&DWP(8,$idx));
	&bswap	(@T[0]);
	&mov	(@T[3],&DWP(12,$idx));
	&bswap	(@T[1]);
	&bswap	(@T[2]);
	&bswap	(@T[3]);

	&call	("_x86_Camellia_encrypt");

	&mov	("esp",$_esp);
	&bswap	(@T[0]);
	&mov	($idx,&wparam(3));	# load ciphertext pointer
	&bswap	(@T[1]);
	&bswap	(@T[2]);
	&bswap	(@T[3]);
	&mov	(&DWP(0,$idx),@T[0]);	# write ciphertext
	&mov	(&DWP(4,$idx),@T[1]);
	&mov	(&DWP(8,$idx),@T[2]);
	&mov	(&DWP(12,$idx),@T[3]);
&function_end("Camellia_EncryptBlock_Rounds");
# V1.x API
&function_begin_B("Camellia_EncryptBlock");
	&mov	("eax",128);
	&sub	("eax",&wparam(0));	# load keyBitLength
	&mov	("eax",3);
	&adc	("eax",0);		# keyBitLength==128?3:4
	&mov	(&wparam(0),"eax");
	&jmp	(&label("Camellia_EncryptBlock_Rounds"));
&function_end_B("Camellia_EncryptBlock");

if ($OPENSSL) {
# void Camellia_encrypt(
#		const unsigned char *in,
#		unsigned char *out,
#		const CAMELLIA_KEY *key)
&function_begin("Camellia_encrypt");
	&mov	($idx,&wparam(0));	# load plaintext pointer
	&mov	($key,&wparam(2));	# load key schedule pointer

	&mov	("ebx","esp");
	&sub	("esp",7*4);		# place for s[0-3],keyEnd,esp and ra
	&and	("esp",-64);
	&mov	("eax",&DWP(272,$key));	# load grandRounds counter

	# place stack frame just "above mod 1024" the key schedule
	# this ensures that cache associativity of 2 suffices
	&lea	("ecx",&DWP(-64-63,$key));
	&sub	("ecx","esp");
	&neg	("ecx");
	&and	("ecx",0x3C0);	# modulo 1024, but aligned to cache-line
	&sub	("esp","ecx");
	&add	("esp",4);	# 4 is reserved for callee's return address

	&shl	("eax",6);
	&lea	("eax",&DWP(0,$key,"eax"));
	&mov	($_esp,"ebx");	# save %esp
	&mov	($_end,"eax");	# save keyEnd

	&call	(&label("pic_point"));
	&set_label("pic_point");
	&blindpop($Tbl);
	&lea	($Tbl,&DWP(&label("Camellia_SBOX")."-".&label("pic_point"),$Tbl));

	&mov	(@T[0],&DWP(0,$idx));	# load plaintext
	&mov	(@T[1],&DWP(4,$idx));
	&mov	(@T[2],&DWP(8,$idx));
	&bswap	(@T[0]);
	&mov	(@T[3],&DWP(12,$idx));
	&bswap	(@T[1]);
	&bswap	(@T[2]);
	&bswap	(@T[3]);

	&call	("_x86_Camellia_encrypt");

	&mov	("esp",$_esp);
	&bswap	(@T[0]);
	&mov	($idx,&wparam(1));	# load ciphertext pointer
	&bswap	(@T[1]);
	&bswap	(@T[2]);
	&bswap	(@T[3]);
	&mov	(&DWP(0,$idx),@T[0]);	# write ciphertext
	&mov	(&DWP(4,$idx),@T[1]);
	&mov	(&DWP(8,$idx),@T[2]);
	&mov	(&DWP(12,$idx),@T[3]);
&function_end("Camellia_encrypt");
}

&function_begin_B("_x86_Camellia_encrypt");
	&xor	(@T[0],&DWP(0,$key));	# ^=key[0-3]
	&xor	(@T[1],&DWP(4,$key));
	&xor	(@T[2],&DWP(8,$key));
	&xor	(@T[3],&DWP(12,$key));
	&mov	($idx,&DWP(16,$key));	# prefetch key[4]

	&mov	($__s0,@T[0]);		# save s[0-3]
	&mov	($__s1,@T[1]);
	&mov	($__s2,@T[2]);
	&mov	($__s3,@T[3]);

&set_label("loop",16);
	for ($i=0;$i<6;$i++) { Camellia_Feistel($i,16,4); }

	&add	($key,16*4);
	&cmp	($key,$__end);
	&je	(&label("done"));

	# @T[0-1] are preloaded, $idx is preloaded with key[0]
	&and	($idx,@T[0]);
	 &mov	 (@T[3],$__s3);
	&rotl	($idx,1);
	 &mov	 (@T[2],@T[3]);
	&xor	(@T[1],$idx);
	 &or	 (@T[2],&DWP(12,$key));
	&mov	($__s1,@T[1]);		# s1^=LeftRotate(s0&key[0],1);
	 &xor	 (@T[2],$__s2);

	&mov	($idx,&DWP(4,$key));
	 &mov	 ($__s2,@T[2]);		# s2^=s3|key[3];
	&or	($idx,@T[1]);
	 &and	 (@T[2],&DWP(8,$key));
	&xor	(@T[0],$idx);
	 &rotl	 (@T[2],1);
	&mov	($__s0,@T[0]);		# s0^=s1|key[1];
	 &xor	 (@T[3],@T[2]);
	&mov	($idx,&DWP(16,$key));		# prefetch key[4]
	 &mov	 ($__s3,@T[3]);		# s3^=LeftRotate(s2&key[2],1);
	&jmp	(&label("loop"));

&set_label("done",8);
	&mov	(@T[2],@T[0]);		# SwapHalf
	&mov	(@T[3],@T[1]);
	&mov	(@T[0],$__s2);
	&mov	(@T[1],$__s3);
	&xor	(@T[0],$idx);		# $idx is preloaded with key[0]
	&xor	(@T[1],&DWP(4,$key));
	&xor	(@T[2],&DWP(8,$key));
	&xor	(@T[3],&DWP(12,$key));
	&ret	();
&function_end_B("_x86_Camellia_encrypt");

# void Camellia_DecryptBlock_Rounds(
#		int grandRounds,
#		const Byte ciphertext[],
#		const KEY_TABLE_TYPE keyTable,
#		Byte plaintext[])
&function_begin("Camellia_DecryptBlock_Rounds");
	&mov	("eax",&wparam(0));	# load grandRounds
	&mov	($idx,&wparam(1));	# load ciphertext pointer
	&mov	($key,&wparam(2));	# load key schedule pointer

	&mov	("ebx","esp");
	&sub	("esp",7*4);		# place for s[0-3],keyEnd,esp and ra
	&and	("esp",-64);

	# place stack frame just "above mod 1024" the key schedule
	# this ensures that cache associativity of 2 suffices
	&lea	("ecx",&DWP(-64-63,$key));
	&sub	("ecx","esp");
	&neg	("ecx");
	&and	("ecx",0x3C0);	# modulo 1024, but aligned to cache-line
	&sub	("esp","ecx");
	&add	("esp",4);	# 4 is reserved for callee's return address

	&shl	("eax",6);
	&mov	(&DWP(4*4,"esp"),$key);	# save keyStart
	&lea	($key,&DWP(0,$key,"eax"));
	&mov	(&DWP(5*4,"esp"),"ebx");# save %esp

	&call	(&label("pic_point"));
	&set_label("pic_point");
	&blindpop($Tbl);
	&lea	($Tbl,&DWP(&label("Camellia_SBOX")."-".&label("pic_point"),$Tbl));

	&mov	(@T[0],&DWP(0,$idx));	# load ciphertext
	&mov	(@T[1],&DWP(4,$idx));
	&mov	(@T[2],&DWP(8,$idx));
	&bswap	(@T[0]);
	&mov	(@T[3],&DWP(12,$idx));
	&bswap	(@T[1]);
	&bswap	(@T[2]);
	&bswap	(@T[3]);

	&call	("_x86_Camellia_decrypt");

	&mov	("esp",&DWP(5*4,"esp"));
	&bswap	(@T[0]);
	&mov	($idx,&wparam(3));	# load plaintext pointer
	&bswap	(@T[1]);
	&bswap	(@T[2]);
	&bswap	(@T[3]);
	&mov	(&DWP(0,$idx),@T[0]);	# write plaintext
	&mov	(&DWP(4,$idx),@T[1]);
	&mov	(&DWP(8,$idx),@T[2]);
	&mov	(&DWP(12,$idx),@T[3]);
&function_end("Camellia_DecryptBlock_Rounds");
# V1.x API
&function_begin_B("Camellia_DecryptBlock");
	&mov	("eax",128);
	&sub	("eax",&wparam(0));	# load keyBitLength
	&mov	("eax",3);
	&adc	("eax",0);		# keyBitLength==128?3:4
	&mov	(&wparam(0),"eax");
	&jmp	(&label("Camellia_DecryptBlock_Rounds"));
&function_end_B("Camellia_DecryptBlock");

if ($OPENSSL) {
# void Camellia_decrypt(
#		const unsigned char *in,
#		unsigned char *out,
#		const CAMELLIA_KEY *key)
&function_begin("Camellia_decrypt");
	&mov	($idx,&wparam(0));	# load ciphertext pointer
	&mov	($key,&wparam(2));	# load key schedule pointer

	&mov	("ebx","esp");
	&sub	("esp",7*4);		# place for s[0-3],keyEnd,esp and ra
	&and	("esp",-64);
	&mov	("eax",&DWP(272,$key));	# load grandRounds counter

	# place stack frame just "above mod 1024" the key schedule
	# this ensures that cache associativity of 2 suffices
	&lea	("ecx",&DWP(-64-63,$key));
	&sub	("ecx","esp");
	&neg	("ecx");
	&and	("ecx",0x3C0);	# modulo 1024, but aligned to cache-line
	&sub	("esp","ecx");
	&add	("esp",4);	# 4 is reserved for callee's return address

	&shl	("eax",6);
	&mov	(&DWP(4*4,"esp"),$key);	# save keyStart
	&lea	($key,&DWP(0,$key,"eax"));
	&mov	(&DWP(5*4,"esp"),"ebx");# save %esp

	&call	(&label("pic_point"));
	&set_label("pic_point");
	&blindpop($Tbl);
	&lea	($Tbl,&DWP(&label("Camellia_SBOX")."-".&label("pic_point"),$Tbl));

	&mov	(@T[0],&DWP(0,$idx));	# load ciphertext
	&mov	(@T[1],&DWP(4,$idx));
	&mov	(@T[2],&DWP(8,$idx));
	&bswap	(@T[0]);
	&mov	(@T[3],&DWP(12,$idx));
	&bswap	(@T[1]);
	&bswap	(@T[2]);
	&bswap	(@T[3]);

	&call	("_x86_Camellia_decrypt");

	&mov	("esp",&DWP(5*4,"esp"));
	&bswap	(@T[0]);
	&mov	($idx,&wparam(1));	# load plaintext pointer
	&bswap	(@T[1]);
	&bswap	(@T[2]);
	&bswap	(@T[3]);
	&mov	(&DWP(0,$idx),@T[0]);	# write plaintext
	&mov	(&DWP(4,$idx),@T[1]);
	&mov	(&DWP(8,$idx),@T[2]);
	&mov	(&DWP(12,$idx),@T[3]);
&function_end("Camellia_decrypt");
}

&function_begin_B("_x86_Camellia_decrypt");
	&xor	(@T[0],&DWP(0,$key));	# ^=key[0-3]
	&xor	(@T[1],&DWP(4,$key));
	&xor	(@T[2],&DWP(8,$key));
	&xor	(@T[3],&DWP(12,$key));
	&mov	($idx,&DWP(-8,$key));	# prefetch key[-2]

	&mov	($__s0,@T[0]);		# save s[0-3]
	&mov	($__s1,@T[1]);
	&mov	($__s2,@T[2]);
	&mov	($__s3,@T[3]);

&set_label("loop",16);
	for ($i=0;$i<6;$i++) { Camellia_Feistel($i,-8,4); }

	&sub	($key,16*4);
	&cmp	($key,$__end);
	&je	(&label("done"));

	# @T[0-1] are preloaded, $idx is preloaded with key[2]
	&and	($idx,@T[0]);
	 &mov	 (@T[3],$__s3);
	&rotl	($idx,1);
	 &mov	 (@T[2],@T[3]);
	&xor	(@T[1],$idx);
	 &or	 (@T[2],&DWP(4,$key));
	&mov	($__s1,@T[1]);		# s1^=LeftRotate(s0&key[0],1);
	 &xor	 (@T[2],$__s2);

	&mov	($idx,&DWP(12,$key));
	 &mov	 ($__s2,@T[2]);		# s2^=s3|key[3];
	&or	($idx,@T[1]);
	 &and	 (@T[2],&DWP(0,$key));
	&xor	(@T[0],$idx);
	 &rotl	 (@T[2],1);
	&mov	($__s0,@T[0]);		# s0^=s1|key[1];
	 &xor	 (@T[3],@T[2]);
	&mov	($idx,&DWP(-8,$key));	# prefetch key[4]
	 &mov	 ($__s3,@T[3]);		# s3^=LeftRotate(s2&key[2],1);
	&jmp	(&label("loop"));

&set_label("done",8);
	&mov	(@T[2],@T[0]);		# SwapHalf
	&mov	(@T[3],@T[1]);
	&mov	(@T[0],$__s2);
	&mov	(@T[1],$__s3);
	&xor	(@T[2],$idx);		# $idx is preloaded with key[2]
	&xor	(@T[3],&DWP(12,$key));
	&xor	(@T[0],&DWP(0,$key));
	&xor	(@T[1],&DWP(4,$key));
	&ret	();
&function_end_B("_x86_Camellia_decrypt");

# shld is very slow on Intel P4 family. Even on AMD it limits
# instruction decode rate [because it's VectorPath] and consequently
# performance. PIII, PM and Core[2] seem to be the only ones which
# execute this code ~7% faster...
sub __rotl128 {
  my ($i0,$i1,$i2,$i3,$rot,$rnd,@T)=@_;

    $rnd *= 2;
    if ($rot) {
	&mov	($idx,$i0);
	&shld	($i0,$i1,$rot);
	&shld	($i1,$i2,$rot);
	&shld	($i2,$i3,$rot);
	&shld	($i3,$idx,$rot);
    }
    &mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i0 eq @T[0]);
    &mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i1 eq @T[0]);
    &mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i2 eq @T[0]);
    &mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i3 eq @T[0]);
}

# ... Implementing 128-bit rotate without shld gives >3x performance
# improvement on P4, only ~7% degradation on other Intel CPUs and
# not worse performance on AMD. This is therefore preferred.
sub _rotl128 {
  my ($i0,$i1,$i2,$i3,$rot,$rnd,@T)=@_;

    $rnd *= 2;
    if ($rot) {
	&mov	($Tbl,$i0);
	&shl	($i0,$rot);
	&mov	($idx,$i1);
	&shr	($idx,32-$rot);
	&shl	($i1,$rot);
	&or	($i0,$idx);
	&mov	($idx,$i2);
	&shl	($i2,$rot);
	&mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i0 eq @T[0]);
	&shr	($idx,32-$rot);
	&or	($i1,$idx);
	&shr	($Tbl,32-$rot);
	&mov	($idx,$i3);
	&shr	($idx,32-$rot);
	&mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i1 eq @T[0]);
	&shl	($i3,$rot);
	&or	($i2,$idx);
	&or	($i3,$Tbl);
	&mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i2 eq @T[0]);
	&mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i3 eq @T[0]);
    } else {
	&mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i0 eq @T[0]);
	&mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i1 eq @T[0]);
	&mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i2 eq @T[0]);
	&mov	(&DWP(-128+4*$rnd++,$key),shift(@T))	if ($i3 eq @T[0]);
    }
}

sub _saveround {
my ($rnd,$key,@T)=@_;
my $bias=int(@T[0])?shift(@T):0;

	&mov	(&DWP($bias+$rnd*8+0,$key),@T[0]);
	&mov	(&DWP($bias+$rnd*8+4,$key),@T[1])	if ($#T>=1);
	&mov	(&DWP($bias+$rnd*8+8,$key),@T[2])	if ($#T>=2);
	&mov	(&DWP($bias+$rnd*8+12,$key),@T[3])	if ($#T>=3);
}

sub _loadround {
my ($rnd,$key,@T)=@_;
my $bias=int(@T[0])?shift(@T):0;

	&mov	(@T[0],&DWP($bias+$rnd*8+0,$key));
	&mov	(@T[1],&DWP($bias+$rnd*8+4,$key))	if ($#T>=1);
	&mov	(@T[2],&DWP($bias+$rnd*8+8,$key))	if ($#T>=2);
	&mov	(@T[3],&DWP($bias+$rnd*8+12,$key))	if ($#T>=3);
}

# void Camellia_Ekeygen(
#		const int keyBitLength,
#		const Byte *rawKey,
#		KEY_TABLE_TYPE keyTable)
&function_begin("Camellia_Ekeygen");
{ my $step=0;

	&stack_push(4);				# place for s[0-3]

	&mov	($Tbl,&wparam(0));		# load arguments
	&mov	($idx,&wparam(1));
	&mov	($key,&wparam(2));

	&mov	(@T[0],&DWP(0,$idx));		# load 0-127 bits
	&mov	(@T[1],&DWP(4,$idx));
	&mov	(@T[2],&DWP(8,$idx));
	&mov	(@T[3],&DWP(12,$idx));

	&bswap	(@T[0]);
	&bswap	(@T[1]);
	&bswap	(@T[2]);
	&bswap	(@T[3]);

	&_saveround	(0,$key,@T);		# KL<<<0

	&cmp	($Tbl,128);
	&je	(&label("1st128"));

	&mov	(@T[0],&DWP(16,$idx));		# load 128-191 bits
	&mov	(@T[1],&DWP(20,$idx));
	&cmp	($Tbl,192);
	&je	(&label("1st192"));
	&mov	(@T[2],&DWP(24,$idx));		# load 192-255 bits
	&mov	(@T[3],&DWP(28,$idx));
	&jmp	(&label("1st256"));
&set_label("1st192",4);
	&mov	(@T[2],@T[0]);
	&mov	(@T[3],@T[1]);
	&not	(@T[2]);
	&not	(@T[3]);
&set_label("1st256",4);
	&bswap	(@T[0]);
	&bswap	(@T[1]);
	&bswap	(@T[2]);
	&bswap	(@T[3]);

	&_saveround	(4,$key,@T);		# temporary storage for KR!

	&xor	(@T[0],&DWP(0*8+0,$key));	# KR^KL
	&xor	(@T[1],&DWP(0*8+4,$key));
	&xor	(@T[2],&DWP(1*8+0,$key));
	&xor	(@T[3],&DWP(1*8+4,$key));

&set_label("1st128",4);
	&call	(&label("pic_point"));
	&set_label("pic_point");
	&blindpop($Tbl);
	&lea	($Tbl,&DWP(&label("Camellia_SBOX")."-".&label("pic_point"),$Tbl));
	&lea	($key,&DWP(&label("Camellia_SIGMA")."-".&label("Camellia_SBOX"),$Tbl));

	&mov	($idx,&DWP($step*8,$key));	# prefetch SIGMA[0]
	&mov	(&swtmp(0),@T[0]);		# save s[0-3]
	&mov	(&swtmp(1),@T[1]);
	&mov	(&swtmp(2),@T[2]);
	&mov	(&swtmp(3),@T[3]);
	&Camellia_Feistel($step++);
	&Camellia_Feistel($step++);
	&mov	(@T[2],&swtmp(2));
	&mov	(@T[3],&swtmp(3));

	&mov	($idx,&wparam(2));
	&xor	(@T[0],&DWP(0*8+0,$idx));	# ^KL
	&xor	(@T[1],&DWP(0*8+4,$idx));
	&xor	(@T[2],&DWP(1*8+0,$idx));
	&xor	(@T[3],&DWP(1*8+4,$idx));

	&mov	($idx,&DWP($step*8,$key));	# prefetch SIGMA[4]
	&mov	(&swtmp(0),@T[0]);		# save s[0-3]
	&mov	(&swtmp(1),@T[1]);
	&mov	(&swtmp(2),@T[2]);
	&mov	(&swtmp(3),@T[3]);
	&Camellia_Feistel($step++);
	&Camellia_Feistel($step++);
	&mov	(@T[2],&swtmp(2));
	&mov	(@T[3],&swtmp(3));

	&mov	($idx,&wparam(0));
	&cmp	($idx,128);
	&jne	(&label("2nd256"));

	&mov	($key,&wparam(2));
	&lea	($key,&DWP(128,$key));		# size optimization

	####### process KA
	&_saveround	(2,$key,-128,@T);	# KA<<<0
	&_rotl128	(@T,15,6,@T);		# KA<<<15
	&_rotl128	(@T,15,8,@T);		# KA<<<(15+15=30)
	&_rotl128	(@T,15,12,@T[0],@T[1]);	# KA<<<(30+15=45)
	&_rotl128	(@T,15,14,@T);		# KA<<<(45+15=60)
	push		(@T,shift(@T));		# rotl128(@T,32);
	&_rotl128	(@T,2,20,@T);		# KA<<<(60+32+2=94)
	&_rotl128	(@T,17,24,@T);		# KA<<<(94+17=111)

	####### process KL
	&_loadround	(0,$key,-128,@T);	# load KL
	&_rotl128	(@T,15,4,@T);		# KL<<<15
	&_rotl128	(@T,30,10,@T);		# KL<<<(15+30=45)
	&_rotl128	(@T,15,13,@T[2],@T[3]);	# KL<<<(45+15=60)
	&_rotl128	(@T,17,16,@T);		# KL<<<(60+17=77)
	&_rotl128	(@T,17,18,@T);		# KL<<<(77+17=94)
	&_rotl128	(@T,17,22,@T);		# KL<<<(94+17=111)

	while (@T[0] ne "eax")			# restore order
	{   unshift	(@T,pop(@T));   }

	&mov	("eax",3);			# 3 grandRounds
	&jmp	(&label("done"));

&set_label("2nd256",16);
	&mov	($idx,&wparam(2));
	&_saveround	(6,$idx,@T);		# temporary storage for KA!

	&xor	(@T[0],&DWP(4*8+0,$idx));	# KA^KR
	&xor	(@T[1],&DWP(4*8+4,$idx));
	&xor	(@T[2],&DWP(5*8+0,$idx));
	&xor	(@T[3],&DWP(5*8+4,$idx));

	&mov	($idx,&DWP($step*8,$key));	# prefetch SIGMA[8]
	&mov	(&swtmp(0),@T[0]);		# save s[0-3]
	&mov	(&swtmp(1),@T[1]);
	&mov	(&swtmp(2),@T[2]);
	&mov	(&swtmp(3),@T[3]);
	&Camellia_Feistel($step++);
	&Camellia_Feistel($step++);
	&mov	(@T[2],&swtmp(2));
	&mov	(@T[3],&swtmp(3));

	&mov	($key,&wparam(2));
	&lea	($key,&DWP(128,$key));		# size optimization

	####### process KB
	&_saveround	(2,$key,-128,@T);	# KB<<<0
	&_rotl128	(@T,30,10,@T);		# KB<<<30
	&_rotl128	(@T,30,20,@T);		# KB<<<(30+30=60)
	push		(@T,shift(@T));		# rotl128(@T,32);
	&_rotl128	(@T,19,32,@T);		# KB<<<(60+32+19=111)

	####### process KR
	&_loadround	(4,$key,-128,@T);	# load KR
	&_rotl128	(@T,15,4,@T);		# KR<<<15
	&_rotl128	(@T,15,8,@T);		# KR<<<(15+15=30)
	&_rotl128	(@T,30,18,@T);		# KR<<<(30+30=60)
	push		(@T,shift(@T));		# rotl128(@T,32);
	&_rotl128	(@T,2,26,@T);		# KR<<<(60+32+2=94)

	####### process KA
	&_loadround	(6,$key,-128,@T);	# load KA
	&_rotl128	(@T,15,6,@T);		# KA<<<15
	&_rotl128	(@T,30,14,@T);		# KA<<<(15+30=45)
	push		(@T,shift(@T));		# rotl128(@T,32);
	&_rotl128	(@T,0,24,@T);		# KA<<<(45+32+0=77)
	&_rotl128	(@T,17,28,@T);		# KA<<<(77+17=94)

	####### process KL
	&_loadround	(0,$key,-128,@T);	# load KL
	push		(@T,shift(@T));		# rotl128(@T,32);
	&_rotl128	(@T,13,12,@T);		# KL<<<(32+13=45)
	&_rotl128	(@T,15,16,@T);		# KL<<<(45+15=60)
	&_rotl128	(@T,17,22,@T);		# KL<<<(60+17=77)
	push		(@T,shift(@T));		# rotl128(@T,32);
	&_rotl128	(@T,2,30,@T);		# KL<<<(77+32+2=111)

	while (@T[0] ne "eax")			# restore order
	{   unshift	(@T,pop(@T));   }

	&mov	("eax",4);			# 4 grandRounds
&set_label("done");
	&lea	("edx",&DWP(272-128,$key));	# end of key schedule
	&stack_pop(4);
}
&function_end("Camellia_Ekeygen");

if ($OPENSSL) {
# int Camellia_set_key (
#		const unsigned char *userKey,
#		int bits,
#		CAMELLIA_KEY *key)
&function_begin_B("Camellia_set_key");
	&push	("ebx");
	&mov	("ecx",&wparam(0));	# pull arguments
	&mov	("ebx",&wparam(1));
	&mov	("edx",&wparam(2));

	&mov	("eax",-1);
	&test	("ecx","ecx");
	&jz	(&label("done"));	# userKey==NULL?
	&test	("edx","edx");
	&jz	(&label("done"));	# key==NULL?

	&mov	("eax",-2);
	&cmp	("ebx",256);
	&je	(&label("arg_ok"));	# bits==256?
	&cmp	("ebx",192);
	&je	(&label("arg_ok"));	# bits==192?
	&cmp	("ebx",128);
	&jne	(&label("done"));	# bits!=128?
&set_label("arg_ok",4);

	&push	("edx");		# push arguments
	&push	("ecx");
	&push	("ebx");
	&call	("Camellia_Ekeygen");
	&stack_pop(3);

	# eax holds grandRounds and edx points at where to put it
	&mov	(&DWP(0,"edx"),"eax");
	&xor	("eax","eax");
&set_label("done",4);
	&pop	("ebx");
	&ret	();
&function_end_B("Camellia_set_key");
}

@SBOX=(
112,130, 44,236,179, 39,192,229,228,133, 87, 53,234, 12,174, 65,
 35,239,107,147, 69, 25,165, 33,237, 14, 79, 78, 29,101,146,189,
134,184,175,143,124,235, 31,206, 62, 48,220, 95, 94,197, 11, 26,
166,225, 57,202,213, 71, 93, 61,217,  1, 90,214, 81, 86,108, 77,
139, 13,154,102,251,204,176, 45,116, 18, 43, 32,240,177,132,153,
223, 76,203,194, 52,126,118,  5,109,183,169, 49,209, 23,  4,215,
 20, 88, 58, 97,222, 27, 17, 28, 50, 15,156, 22, 83, 24,242, 34,
254, 68,207,178,195,181,122,145, 36,  8,232,168, 96,252,105, 80,
170,208,160,125,161,137, 98,151, 84, 91, 30,149,224,255,100,210,
 16,196,  0, 72,163,247,117,219,138,  3,230,218,  9, 63,221,148,
135, 92,131,  2,205, 74,144, 51,115,103,246,243,157,127,191,226,
 82,155,216, 38,200, 55,198, 59,129,150,111, 75, 19,190, 99, 46,
233,121,167,140,159,110,188,142, 41,245,249,182, 47,253,180, 89,
120,152,  6,106,231, 70,113,186,212, 37,171, 66,136,162,141,250,
114,  7,185, 85,248,238,172, 10, 54, 73, 42,104, 60, 56,241,164,
 64, 40,211,123,187,201, 67,193, 21,227,173,244,119,199,128,158);

sub S1110 { my $i=shift; $i=@SBOX[$i]; return $i<<24|$i<<16|$i<<8; }
sub S4404 { my $i=shift; $i=($i<<1|$i>>7)&0xff; $i=@SBOX[$i]; return $i<<24|$i<<16|$i; }
sub S0222 { my $i=shift; $i=@SBOX[$i]; $i=($i<<1|$i>>7)&0xff; return $i<<16|$i<<8|$i; }
sub S3033 { my $i=shift; $i=@SBOX[$i]; $i=($i>>1|$i<<7)&0xff; return $i<<24|$i<<8|$i; }

&set_label("Camellia_SIGMA",64);
&data_word(
    0xa09e667f, 0x3bcc908b, 0xb67ae858, 0x4caa73b2,
    0xc6ef372f, 0xe94f82be, 0x54ff53a5, 0xf1d36f1c,
    0x10e527fa, 0xde682d1d, 0xb05688c2, 0xb3e6c1fd,
    0,          0,          0,          0);
&set_label("Camellia_SBOX",64);
# tables are interleaved, remember?
for ($i=0;$i<256;$i++) { &data_word(&S1110($i),&S4404($i)); }
for ($i=0;$i<256;$i++) { &data_word(&S0222($i),&S3033($i)); }

# void Camellia_cbc_encrypt (const void char *inp, unsigned char *out,
#			size_t length, const CAMELLIA_KEY *key,
#			unsigned char *ivp,const int enc);
{
# stack frame layout
#             -4(%esp)		# return address	 0(%esp)
#              0(%esp)		# s0			 4(%esp)
#              4(%esp)		# s1			 8(%esp)
#              8(%esp)		# s2			12(%esp)
#             12(%esp)		# s3			16(%esp)
#             16(%esp)		# end of key schedule	20(%esp)
#             20(%esp)		# %esp backup
my $_inp=&DWP(24,"esp");	#copy of wparam(0)
my $_out=&DWP(28,"esp");	#copy of wparam(1)
my $_len=&DWP(32,"esp");	#copy of wparam(2)
my $_key=&DWP(36,"esp");	#copy of wparam(3)
my $_ivp=&DWP(40,"esp");	#copy of wparam(4)
my $ivec=&DWP(44,"esp");	#ivec[16]
my $_tmp=&DWP(44,"esp");	#volatile variable [yes, aliases with ivec]
my ($s0,$s1,$s2,$s3) = @T;

&function_begin("Camellia_cbc_encrypt");
	&mov	($s2 eq "ecx"? $s2 : "",&wparam(2));	# load len
	&cmp	($s2,0);
	&je	(&label("enc_out"));

	&pushf	();
	&cld	();

	&mov	($s0,&wparam(0));	# load inp
	&mov	($s1,&wparam(1));	# load out
	#&mov	($s2,&wparam(2));	# load len
	&mov	($s3,&wparam(3));	# load key
	&mov	($Tbl,&wparam(4));	# load ivp

	# allocate aligned stack frame...
	&lea	($idx,&DWP(-64,"esp"));
	&and	($idx,-64);

	# place stack frame just "above mod 1024" the key schedule
	# this ensures that cache associativity of 2 suffices
	&lea	($key,&DWP(-64-63,$s3));
	&sub	($key,$idx);
	&neg	($key);
	&and	($key,0x3C0);	# modulo 1024, but aligned to cache-line
	&sub	($idx,$key);

	&mov	($key,&wparam(5));	# load enc

	&exch	("esp",$idx);
	&add	("esp",4);		# reserve for return address!
	&mov	($_esp,$idx);		# save %esp

	&mov	($_inp,$s0);		# save copy of inp
	&mov	($_out,$s1);		# save copy of out
	&mov	($_len,$s2);		# save copy of len
	&mov	($_key,$s3);		# save copy of key
	&mov	($_ivp,$Tbl);		# save copy of ivp

	&call   (&label("pic_point"));	# make it PIC!
	&set_label("pic_point");
	&blindpop($Tbl);
	&lea    ($Tbl,&DWP(&label("Camellia_SBOX")."-".&label("pic_point"),$Tbl));

	&mov	($idx,32);
	&set_label("prefetch_sbox",4);
		&mov	($s0,&DWP(0,$Tbl));
		&mov	($s1,&DWP(32,$Tbl));
		&mov	($s2,&DWP(64,$Tbl));
		&mov	($s3,&DWP(96,$Tbl));
		&lea	($Tbl,&DWP(128,$Tbl));
		&dec	($idx);
	&jnz	(&label("prefetch_sbox"));
	&mov	($s0,$_key);
	&sub	($Tbl,4096);
	&mov	($idx,$_inp);
	&mov	($s3,&DWP(272,$s0));		# load grandRounds

	&cmp	($key,0);
	&je	(&label("DECRYPT"));

	&mov	($s2,$_len);
	&mov	($key,$_ivp);
	&shl	($s3,6);
	&lea	($s3,&DWP(0,$s0,$s3));
	&mov	($_end,$s3);

	&test	($s2,0xFFFFFFF0);
	&jz	(&label("enc_tail"));		# short input...

	&mov	($s0,&DWP(0,$key));		# load iv
	&mov	($s1,&DWP(4,$key));

	&set_label("enc_loop",4);
		&mov	($s2,&DWP(8,$key));
		&mov	($s3,&DWP(12,$key));

		&xor	($s0,&DWP(0,$idx));	# xor input data
		&xor	($s1,&DWP(4,$idx));
		&xor	($s2,&DWP(8,$idx));
		&bswap	($s0);
		&xor	($s3,&DWP(12,$idx));
		&bswap	($s1);
		&mov	($key,$_key);		# load key
		&bswap	($s2);
		&bswap	($s3);

		&call	("_x86_Camellia_encrypt");

		&mov	($idx,$_inp);		# load inp
		&mov	($key,$_out);		# load out

		&bswap	($s0);
		&bswap	($s1);
		&bswap	($s2);
		&mov	(&DWP(0,$key),$s0);	# save output data
		&bswap	($s3);
		&mov	(&DWP(4,$key),$s1);
		&mov	(&DWP(8,$key),$s2);
		&mov	(&DWP(12,$key),$s3);

		&mov	($s2,$_len);		# load len

		&lea	($idx,&DWP(16,$idx));
		&mov	($_inp,$idx);		# save inp

		&lea	($s3,&DWP(16,$key));
		&mov	($_out,$s3);		# save out

		&sub	($s2,16);
		&test	($s2,0xFFFFFFF0);
		&mov	($_len,$s2);		# save len
	&jnz	(&label("enc_loop"));
	&test	($s2,15);
	&jnz	(&label("enc_tail"));
	&mov	($idx,$_ivp);		# load ivp
	&mov	($s2,&DWP(8,$key));	# restore last dwords
	&mov	($s3,&DWP(12,$key));
	&mov	(&DWP(0,$idx),$s0);	# save ivec
	&mov	(&DWP(4,$idx),$s1);
	&mov	(&DWP(8,$idx),$s2);
	&mov	(&DWP(12,$idx),$s3);

	&mov	("esp",$_esp);
	&popf	();
    &set_label("enc_out");
	&function_end_A();
	&pushf	();			# kludge, never executed

    &set_label("enc_tail",4);
	&mov	($s0,$key eq "edi" ? $key : "");
	&mov	($key,$_out);			# load out
	&push	($s0);				# push ivp
	&mov	($s1,16);
	&sub	($s1,$s2);
	&cmp	($key,$idx);			# compare with inp
	&je	(&label("enc_in_place"));
	&align	(4);
	&data_word(0xA4F3F689);	# rep movsb	# copy input
	&jmp	(&label("enc_skip_in_place"));
    &set_label("enc_in_place");
	&lea	($key,&DWP(0,$key,$s2));
    &set_label("enc_skip_in_place");
	&mov	($s2,$s1);
	&xor	($s0,$s0);
	&align	(4);
	&data_word(0xAAF3F689);	# rep stosb	# zero tail
	&pop	($key);				# pop ivp

	&mov	($idx,$_out);			# output as input
	&mov	($s0,&DWP(0,$key));
	&mov	($s1,&DWP(4,$key));
	&mov	($_len,16);			# len=16
	&jmp	(&label("enc_loop"));		# one more spin...

#----------------------------- DECRYPT -----------------------------#
&set_label("DECRYPT",16);
	&shl	($s3,6);
	&lea	($s3,&DWP(0,$s0,$s3));
	&mov	($_end,$s0);
	&mov	($_key,$s3);

	&cmp	($idx,$_out);
	&je	(&label("dec_in_place"));	# in-place processing...

	&mov	($key,$_ivp);			# load ivp
	&mov	($_tmp,$key);

	&set_label("dec_loop",4);
		&mov	($s0,&DWP(0,$idx));	# read input
		&mov	($s1,&DWP(4,$idx));
		&mov	($s2,&DWP(8,$idx));
		&bswap	($s0);
		&mov	($s3,&DWP(12,$idx));
		&bswap	($s1);
		&mov	($key,$_key);		# load key
		&bswap	($s2);
		&bswap	($s3);

		&call	("_x86_Camellia_decrypt");

		&mov	($key,$_tmp);		# load ivp
		&mov	($idx,$_len);		# load len

		&bswap	($s0);
		&bswap	($s1);
		&bswap	($s2);
		&xor	($s0,&DWP(0,$key));	# xor iv
		&bswap	($s3);
		&xor	($s1,&DWP(4,$key));
		&xor	($s2,&DWP(8,$key));
		&xor	($s3,&DWP(12,$key));

		&sub	($idx,16);
		&jc	(&label("dec_partial"));
		&mov	($_len,$idx);		# save len
		&mov	($idx,$_inp);		# load inp
		&mov	($key,$_out);		# load out

		&mov	(&DWP(0,$key),$s0);	# write output
		&mov	(&DWP(4,$key),$s1);
		&mov	(&DWP(8,$key),$s2);
		&mov	(&DWP(12,$key),$s3);

		&mov	($_tmp,$idx);		# save ivp
		&lea	($idx,&DWP(16,$idx));
		&mov	($_inp,$idx);		# save inp

		&lea	($key,&DWP(16,$key));
		&mov	($_out,$key);		# save out

	&jnz	(&label("dec_loop"));
	&mov	($key,$_tmp);		# load temp ivp
    &set_label("dec_end");
	&mov	($idx,$_ivp);		# load user ivp
	&mov	($s0,&DWP(0,$key));	# load iv
	&mov	($s1,&DWP(4,$key));
	&mov	($s2,&DWP(8,$key));
	&mov	($s3,&DWP(12,$key));
	&mov	(&DWP(0,$idx),$s0);	# copy back to user
	&mov	(&DWP(4,$idx),$s1);
	&mov	(&DWP(8,$idx),$s2);
	&mov	(&DWP(12,$idx),$s3);
	&jmp	(&label("dec_out"));

    &set_label("dec_partial",4);
	&lea	($key,$ivec);
	&mov	(&DWP(0,$key),$s0);	# dump output to stack
	&mov	(&DWP(4,$key),$s1);
	&mov	(&DWP(8,$key),$s2);
	&mov	(&DWP(12,$key),$s3);
	&lea	($s2 eq "ecx" ? $s2 : "",&DWP(16,$idx));
	&mov	($idx eq "esi" ? $idx : "",$key);
	&mov	($key eq "edi" ? $key : "",$_out);	# load out
	&data_word(0xA4F3F689);	# rep movsb		# copy output
	&mov	($key,$_inp);				# use inp as temp ivp
	&jmp	(&label("dec_end"));

    &set_label("dec_in_place",4);
	&set_label("dec_in_place_loop");
		&lea	($key,$ivec);
		&mov	($s0,&DWP(0,$idx));	# read input
		&mov	($s1,&DWP(4,$idx));
		&mov	($s2,&DWP(8,$idx));
		&mov	($s3,&DWP(12,$idx));

		&mov	(&DWP(0,$key),$s0);	# copy to temp
		&mov	(&DWP(4,$key),$s1);
		&mov	(&DWP(8,$key),$s2);
		&bswap	($s0);
		&mov	(&DWP(12,$key),$s3);
		&bswap	($s1);
		&mov	($key,$_key);		# load key
		&bswap	($s2);
		&bswap	($s3);

		&call	("_x86_Camellia_decrypt");

		&mov	($key,$_ivp);		# load ivp
		&mov	($idx,$_out);		# load out

		&bswap	($s0);
		&bswap	($s1);
		&bswap	($s2);
		&xor	($s0,&DWP(0,$key));	# xor iv
		&bswap	($s3);
		&xor	($s1,&DWP(4,$key));
		&xor	($s2,&DWP(8,$key));
		&xor	($s3,&DWP(12,$key));

		&mov	(&DWP(0,$idx),$s0);	# write output
		&mov	(&DWP(4,$idx),$s1);
		&mov	(&DWP(8,$idx),$s2);
		&mov	(&DWP(12,$idx),$s3);

		&lea	($idx,&DWP(16,$idx));
		&mov	($_out,$idx);		# save out

		&lea	($idx,$ivec);
		&mov	($s0,&DWP(0,$idx));	# read temp
		&mov	($s1,&DWP(4,$idx));
		&mov	($s2,&DWP(8,$idx));
		&mov	($s3,&DWP(12,$idx));

		&mov	(&DWP(0,$key),$s0);	# copy iv
		&mov	(&DWP(4,$key),$s1);
		&mov	(&DWP(8,$key),$s2);
		&mov	(&DWP(12,$key),$s3);

		&mov	($idx,$_inp);		# load inp

		&lea	($idx,&DWP(16,$idx));
		&mov	($_inp,$idx);		# save inp

		&mov	($s2,$_len);		# load len
		&sub	($s2,16);
		&jc	(&label("dec_in_place_partial"));
		&mov	($_len,$s2);		# save len
	&jnz	(&label("dec_in_place_loop"));
	&jmp	(&label("dec_out"));

    &set_label("dec_in_place_partial",4);
	# one can argue if this is actually required...
	&mov	($key eq "edi" ? $key : "",$_out);
	&lea	($idx eq "esi" ? $idx : "",$ivec);
	&lea	($key,&DWP(0,$key,$s2));
	&lea	($idx,&DWP(16,$idx,$s2));
	&neg	($s2 eq "ecx" ? $s2 : "");
	&data_word(0xA4F3F689);	# rep movsb	# restore tail

    &set_label("dec_out",4);
    &mov	("esp",$_esp);
    &popf	();
&function_end("Camellia_cbc_encrypt");
}

&asciz("Camellia for x86 by <appro\@openssl.org>");

&asm_finish();

close STDOUT;
