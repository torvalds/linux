#! /usr/bin/env perl
# Copyright 2011-2016 The OpenSSL Project Authors. All Rights Reserved.
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
# May 2011
#
# The module implements bn_GF2m_mul_2x2 polynomial multiplication used
# in bn_gf2m.c. It's kind of low-hanging mechanical port from C for
# the time being... Except that it has three code paths: pure integer
# code suitable for any x86 CPU, MMX code suitable for PIII and later
# and PCLMULQDQ suitable for Westmere and later. Improvement varies
# from one benchmark and µ-arch to another. Below are interval values
# for 163- and 571-bit ECDH benchmarks relative to compiler-generated
# code:
#
# PIII		16%-30%
# P4		12%-12%
# Opteron	18%-40%
# Core2		19%-44%
# Atom		38%-64%
# Westmere	53%-121%(PCLMULQDQ)/20%-32%(MMX)
# Sandy Bridge	72%-127%(PCLMULQDQ)/27%-23%(MMX)
#
# Note that above improvement coefficients are not coefficients for
# bn_GF2m_mul_2x2 itself. For example 120% ECDH improvement is result
# of bn_GF2m_mul_2x2 being >4x faster. As it gets faster, benchmark
# is more and more dominated by other subroutines, most notably by
# BN_GF2m_mod[_mul]_arr...

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

$output = pop;
open STDOUT,">$output";

&asm_init($ARGV[0],$x86only = $ARGV[$#ARGV] eq "386");

$sse2=0;
for (@ARGV) { $sse2=1 if (/-DOPENSSL_IA32_SSE2/); }

&external_label("OPENSSL_ia32cap_P") if ($sse2);

$a="eax";
$b="ebx";
($a1,$a2,$a4)=("ecx","edx","ebp");

$R="mm0";
@T=("mm1","mm2");
($A,$B,$B30,$B31)=("mm2","mm3","mm4","mm5");
@i=("esi","edi");

					if (!$x86only) {
&function_begin_B("_mul_1x1_mmx");
	&sub	("esp",32+4);
	 &mov	($a1,$a);
	 &lea	($a2,&DWP(0,$a,$a));
	 &and	($a1,0x3fffffff);
	 &lea	($a4,&DWP(0,$a2,$a2));
	 &mov	(&DWP(0*4,"esp"),0);
	 &and	($a2,0x7fffffff);
	&movd	($A,$a);
	&movd	($B,$b);
	 &mov	(&DWP(1*4,"esp"),$a1);	# a1
	 &xor	($a1,$a2);		# a1^a2
	&pxor	($B31,$B31);
	&pxor	($B30,$B30);
	 &mov	(&DWP(2*4,"esp"),$a2);	# a2
	 &xor	($a2,$a4);		# a2^a4
	 &mov	(&DWP(3*4,"esp"),$a1);	# a1^a2
	&pcmpgtd($B31,$A);		# broadcast 31st bit
	&paddd	($A,$A);		# $A<<=1
	 &xor	($a1,$a2);		# a1^a4=a1^a2^a2^a4
	 &mov	(&DWP(4*4,"esp"),$a4);	# a4
	 &xor	($a4,$a2);		# a2=a4^a2^a4
	&pand	($B31,$B);
	&pcmpgtd($B30,$A);		# broadcast 30th bit
	 &mov	(&DWP(5*4,"esp"),$a1);	# a1^a4
	 &xor	($a4,$a1);		# a1^a2^a4
	&psllq	($B31,31);
	&pand	($B30,$B);
	 &mov	(&DWP(6*4,"esp"),$a2);	# a2^a4
	&mov	(@i[0],0x7);
	 &mov	(&DWP(7*4,"esp"),$a4);	# a1^a2^a4
	 &mov	($a4,@i[0]);
	&and	(@i[0],$b);
	&shr	($b,3);
	&mov	(@i[1],$a4);
	&psllq	($B30,30);
	&and	(@i[1],$b);
	&shr	($b,3);
	&movd	($R,&DWP(0,"esp",@i[0],4));
	&mov	(@i[0],$a4);
	&and	(@i[0],$b);
	&shr	($b,3);
	for($n=1;$n<9;$n++) {
		&movd	(@T[1],&DWP(0,"esp",@i[1],4));
		&mov	(@i[1],$a4);
		&psllq	(@T[1],3*$n);
		&and	(@i[1],$b);
		&shr	($b,3);
		&pxor	($R,@T[1]);

		push(@i,shift(@i)); push(@T,shift(@T));
	}
	&movd	(@T[1],&DWP(0,"esp",@i[1],4));
	&pxor	($R,$B30);
	&psllq	(@T[1],3*$n++);
	&pxor	($R,@T[1]);

	&movd	(@T[0],&DWP(0,"esp",@i[0],4));
	&pxor	($R,$B31);
	&psllq	(@T[0],3*$n);
	&add	("esp",32+4);
	&pxor	($R,@T[0]);
	&ret	();
&function_end_B("_mul_1x1_mmx");
					}

($lo,$hi)=("eax","edx");
@T=("ecx","ebp");

&function_begin_B("_mul_1x1_ialu");
	&sub	("esp",32+4);
	 &mov	($a1,$a);
	 &lea	($a2,&DWP(0,$a,$a));
	 &lea	($a4,&DWP(0,"",$a,4));
	 &and	($a1,0x3fffffff);
	&lea	(@i[1],&DWP(0,$lo,$lo));
	&sar	($lo,31);		# broadcast 31st bit
	 &mov	(&DWP(0*4,"esp"),0);
	 &and	($a2,0x7fffffff);
	 &mov	(&DWP(1*4,"esp"),$a1);	# a1
	 &xor	($a1,$a2);		# a1^a2
	 &mov	(&DWP(2*4,"esp"),$a2);	# a2
	 &xor	($a2,$a4);		# a2^a4
	 &mov	(&DWP(3*4,"esp"),$a1);	# a1^a2
	 &xor	($a1,$a2);		# a1^a4=a1^a2^a2^a4
	 &mov	(&DWP(4*4,"esp"),$a4);	# a4
	 &xor	($a4,$a2);		# a2=a4^a2^a4
	 &mov	(&DWP(5*4,"esp"),$a1);	# a1^a4
	 &xor	($a4,$a1);		# a1^a2^a4
	&sar	(@i[1],31);		# broadcast 30th bit
	&and	($lo,$b);
	 &mov	(&DWP(6*4,"esp"),$a2);	# a2^a4
	&and	(@i[1],$b);
	 &mov	(&DWP(7*4,"esp"),$a4);	# a1^a2^a4
	&mov	($hi,$lo);
	&shl	($lo,31);
	&mov	(@T[0],@i[1]);
	&shr	($hi,1);

	 &mov	(@i[0],0x7);
	&shl	(@i[1],30);
	 &and	(@i[0],$b);
	&shr	(@T[0],2);
	&xor	($lo,@i[1]);

	&shr	($b,3);
	&mov	(@i[1],0x7);		# 5-byte instruction!?
	&and	(@i[1],$b);
	&shr	($b,3);
	 &xor	($hi,@T[0]);
	&xor	($lo,&DWP(0,"esp",@i[0],4));
	&mov	(@i[0],0x7);
	&and	(@i[0],$b);
	&shr	($b,3);
	for($n=1;$n<9;$n++) {
		&mov	(@T[1],&DWP(0,"esp",@i[1],4));
		&mov	(@i[1],0x7);
		&mov	(@T[0],@T[1]);
		&shl	(@T[1],3*$n);
		&and	(@i[1],$b);
		&shr	(@T[0],32-3*$n);
		&xor	($lo,@T[1]);
		&shr	($b,3);
		&xor	($hi,@T[0]);

		push(@i,shift(@i)); push(@T,shift(@T));
	}
	&mov	(@T[1],&DWP(0,"esp",@i[1],4));
	&mov	(@T[0],@T[1]);
	&shl	(@T[1],3*$n);
	&mov	(@i[1],&DWP(0,"esp",@i[0],4));
	&shr	(@T[0],32-3*$n);	$n++;
	&mov	(@i[0],@i[1]);
	&xor	($lo,@T[1]);
	&shl	(@i[1],3*$n);
	&xor	($hi,@T[0]);
	&shr	(@i[0],32-3*$n);
	&xor	($lo,@i[1]);
	&xor	($hi,@i[0]);

	&add	("esp",32+4);
	&ret	();
&function_end_B("_mul_1x1_ialu");

# void bn_GF2m_mul_2x2(BN_ULONG *r, BN_ULONG a1, BN_ULONG a0, BN_ULONG b1, BN_ULONG b0);
&function_begin_B("bn_GF2m_mul_2x2");
if (!$x86only) {
	&picmeup("edx","OPENSSL_ia32cap_P");
	&mov	("eax",&DWP(0,"edx"));
	&mov	("edx",&DWP(4,"edx"));
	&test	("eax",1<<23);		# check MMX bit
	&jz	(&label("ialu"));
if ($sse2) {
	&test	("eax",1<<24);		# check FXSR bit
	&jz	(&label("mmx"));
	&test	("edx",1<<1);		# check PCLMULQDQ bit
	&jz	(&label("mmx"));

	&movups		("xmm0",&QWP(8,"esp"));
	&shufps		("xmm0","xmm0",0b10110001);
	&pclmulqdq	("xmm0","xmm0",1);
	&mov		("eax",&DWP(4,"esp"));
	&movups		(&QWP(0,"eax"),"xmm0");
	&ret	();

&set_label("mmx",16);
}
	&push	("ebp");
	&push	("ebx");
	&push	("esi");
	&push	("edi");
	&mov	($a,&wparam(1));
	&mov	($b,&wparam(3));
	&call	("_mul_1x1_mmx");	# a1·b1
	&movq	("mm7",$R);

	&mov	($a,&wparam(2));
	&mov	($b,&wparam(4));
	&call	("_mul_1x1_mmx");	# a0·b0
	&movq	("mm6",$R);

	&mov	($a,&wparam(1));
	&mov	($b,&wparam(3));
	&xor	($a,&wparam(2));
	&xor	($b,&wparam(4));
	&call	("_mul_1x1_mmx");	# (a0+a1)·(b0+b1)
	&pxor	($R,"mm7");
	&mov	($a,&wparam(0));
	&pxor	($R,"mm6");		# (a0+a1)·(b0+b1)-a1·b1-a0·b0

	&movq	($A,$R);
	&psllq	($R,32);
	&pop	("edi");
	&psrlq	($A,32);
	&pop	("esi");
	&pxor	($R,"mm6");
	&pop	("ebx");
	&pxor	($A,"mm7");
	&movq	(&QWP(0,$a),$R);
	&pop	("ebp");
	&movq	(&QWP(8,$a),$A);
	&emms	();
	&ret	();
&set_label("ialu",16);
}
	&push	("ebp");
	&push	("ebx");
	&push	("esi");
	&push	("edi");
	&stack_push(4+1);

	&mov	($a,&wparam(1));
	&mov	($b,&wparam(3));
	&call	("_mul_1x1_ialu");	# a1·b1
	&mov	(&DWP(8,"esp"),$lo);
	&mov	(&DWP(12,"esp"),$hi);

	&mov	($a,&wparam(2));
	&mov	($b,&wparam(4));
	&call	("_mul_1x1_ialu");	# a0·b0
	&mov	(&DWP(0,"esp"),$lo);
	&mov	(&DWP(4,"esp"),$hi);

	&mov	($a,&wparam(1));
	&mov	($b,&wparam(3));
	&xor	($a,&wparam(2));
	&xor	($b,&wparam(4));
	&call	("_mul_1x1_ialu");	# (a0+a1)·(b0+b1)

	&mov	("ebp",&wparam(0));
		 @r=("ebx","ecx","edi","esi");
	&mov	(@r[0],&DWP(0,"esp"));
	&mov	(@r[1],&DWP(4,"esp"));
	&mov	(@r[2],&DWP(8,"esp"));
	&mov	(@r[3],&DWP(12,"esp"));

	&xor	($lo,$hi);
	&xor	($hi,@r[1]);
	&xor	($lo,@r[0]);
	&mov	(&DWP(0,"ebp"),@r[0]);
	&xor	($hi,@r[2]);
	&mov	(&DWP(12,"ebp"),@r[3]);
	&xor	($lo,@r[3]);
	&stack_pop(4+1);
	&xor	($hi,@r[3]);
	&pop	("edi");
	&xor	($lo,$hi);
	&pop	("esi");
	&mov	(&DWP(8,"ebp"),$hi);
	&pop	("ebx");
	&mov	(&DWP(4,"ebp"),$lo);
	&pop	("ebp");
	&ret	();
&function_end_B("bn_GF2m_mul_2x2");

&asciz	("GF(2^m) Multiplication for x86, CRYPTOGAMS by <appro\@openssl.org>");

&asm_finish();

close STDOUT;
