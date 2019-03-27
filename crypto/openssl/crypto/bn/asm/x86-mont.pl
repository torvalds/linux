#! /usr/bin/env perl
# Copyright 2005-2018 The OpenSSL Project Authors. All Rights Reserved.
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

# October 2005
#
# This is a "teaser" code, as it can be improved in several ways...
# First of all non-SSE2 path should be implemented (yes, for now it
# performs Montgomery multiplication/convolution only on SSE2-capable
# CPUs such as P4, others fall down to original code). Then inner loop
# can be unrolled and modulo-scheduled to improve ILP and possibly
# moved to 128-bit XMM register bank (though it would require input
# rearrangement and/or increase bus bandwidth utilization). Dedicated
# squaring procedure should give further performance improvement...
# Yet, for being draft, the code improves rsa512 *sign* benchmark by
# 110%(!), rsa1024 one - by 70% and rsa4096 - by 20%:-)

# December 2006
#
# Modulo-scheduling SSE2 loops results in further 15-20% improvement.
# Integer-only code [being equipped with dedicated squaring procedure]
# gives ~40% on rsa512 sign benchmark...

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

$output = pop;
open STDOUT,">$output";

&asm_init($ARGV[0]);

$sse2=0;
for (@ARGV) { $sse2=1 if (/-DOPENSSL_IA32_SSE2/); }

&external_label("OPENSSL_ia32cap_P") if ($sse2);

&function_begin("bn_mul_mont");

$i="edx";
$j="ecx";
$ap="esi";	$tp="esi";		# overlapping variables!!!
$rp="edi";	$bp="edi";		# overlapping variables!!!
$np="ebp";
$num="ebx";

$_num=&DWP(4*0,"esp");			# stack top layout
$_rp=&DWP(4*1,"esp");
$_ap=&DWP(4*2,"esp");
$_bp=&DWP(4*3,"esp");
$_np=&DWP(4*4,"esp");
$_n0=&DWP(4*5,"esp");	$_n0q=&QWP(4*5,"esp");
$_sp=&DWP(4*6,"esp");
$_bpend=&DWP(4*7,"esp");
$frame=32;				# size of above frame rounded up to 16n

	&xor	("eax","eax");
	&mov	("edi",&wparam(5));	# int num
	&cmp	("edi",4);
	&jl	(&label("just_leave"));

	&lea	("esi",&wparam(0));	# put aside pointer to argument block
	&lea	("edx",&wparam(1));	# load ap
	&add	("edi",2);		# extra two words on top of tp
	&neg	("edi");
	&lea	("ebp",&DWP(-$frame,"esp","edi",4));	# future alloca($frame+4*(num+2))
	&neg	("edi");

	# minimize cache contention by arranging 2K window between stack
	# pointer and ap argument [np is also position sensitive vector,
	# but it's assumed to be near ap, as it's allocated at ~same
	# time].
	&mov	("eax","ebp");
	&sub	("eax","edx");
	&and	("eax",2047);
	&sub	("ebp","eax");		# this aligns sp and ap modulo 2048

	&xor	("edx","ebp");
	&and	("edx",2048);
	&xor	("edx",2048);
	&sub	("ebp","edx");		# this splits them apart modulo 4096

	&and	("ebp",-64);		# align to cache line

	# An OS-agnostic version of __chkstk.
	#
	# Some OSes (Windows) insist on stack being "wired" to
	# physical memory in strictly sequential manner, i.e. if stack
	# allocation spans two pages, then reference to farmost one can
	# be punishable by SEGV. But page walking can do good even on
	# other OSes, because it guarantees that villain thread hits
	# the guard page before it can make damage to innocent one...
	&mov	("eax","esp");
	&sub	("eax","ebp");
	&and	("eax",-4096);
	&mov	("edx","esp");		# saved stack pointer!
	&lea	("esp",&DWP(0,"ebp","eax"));
	&mov	("eax",&DWP(0,"esp"));
	&cmp	("esp","ebp");
	&ja	(&label("page_walk"));
	&jmp	(&label("page_walk_done"));

&set_label("page_walk",16);
	&lea	("esp",&DWP(-4096,"esp"));
	&mov	("eax",&DWP(0,"esp"));
	&cmp	("esp","ebp");
	&ja	(&label("page_walk"));
&set_label("page_walk_done");

	################################# load argument block...
	&mov	("eax",&DWP(0*4,"esi"));# BN_ULONG *rp
	&mov	("ebx",&DWP(1*4,"esi"));# const BN_ULONG *ap
	&mov	("ecx",&DWP(2*4,"esi"));# const BN_ULONG *bp
	&mov	("ebp",&DWP(3*4,"esi"));# const BN_ULONG *np
	&mov	("esi",&DWP(4*4,"esi"));# const BN_ULONG *n0
	#&mov	("edi",&DWP(5*4,"esi"));# int num

	&mov	("esi",&DWP(0,"esi"));	# pull n0[0]
	&mov	($_rp,"eax");		# ... save a copy of argument block
	&mov	($_ap,"ebx");
	&mov	($_bp,"ecx");
	&mov	($_np,"ebp");
	&mov	($_n0,"esi");
	&lea	($num,&DWP(-3,"edi"));	# num=num-1 to assist modulo-scheduling
	#&mov	($_num,$num);		# redundant as $num is not reused
	&mov	($_sp,"edx");		# saved stack pointer!

if($sse2) {
$acc0="mm0";	# mmx register bank layout
$acc1="mm1";
$car0="mm2";
$car1="mm3";
$mul0="mm4";
$mul1="mm5";
$temp="mm6";
$mask="mm7";

	&picmeup("eax","OPENSSL_ia32cap_P");
	&bt	(&DWP(0,"eax"),26);
	&jnc	(&label("non_sse2"));

	&mov	("eax",-1);
	&movd	($mask,"eax");		# mask 32 lower bits

	&mov	($ap,$_ap);		# load input pointers
	&mov	($bp,$_bp);
	&mov	($np,$_np);

	&xor	($i,$i);		# i=0
	&xor	($j,$j);		# j=0

	&movd	($mul0,&DWP(0,$bp));		# bp[0]
	&movd	($mul1,&DWP(0,$ap));		# ap[0]
	&movd	($car1,&DWP(0,$np));		# np[0]

	&pmuludq($mul1,$mul0);			# ap[0]*bp[0]
	&movq	($car0,$mul1);
	&movq	($acc0,$mul1);			# I wish movd worked for
	&pand	($acc0,$mask);			# inter-register transfers

	&pmuludq($mul1,$_n0q);			# *=n0

	&pmuludq($car1,$mul1);			# "t[0]"*np[0]*n0
	&paddq	($car1,$acc0);

	&movd	($acc1,&DWP(4,$np));		# np[1]
	&movd	($acc0,&DWP(4,$ap));		# ap[1]

	&psrlq	($car0,32);
	&psrlq	($car1,32);

	&inc	($j);				# j++
&set_label("1st",16);
	&pmuludq($acc0,$mul0);			# ap[j]*bp[0]
	&pmuludq($acc1,$mul1);			# np[j]*m1
	&paddq	($car0,$acc0);			# +=c0
	&paddq	($car1,$acc1);			# +=c1

	&movq	($acc0,$car0);
	&pand	($acc0,$mask);
	&movd	($acc1,&DWP(4,$np,$j,4));	# np[j+1]
	&paddq	($car1,$acc0);			# +=ap[j]*bp[0];
	&movd	($acc0,&DWP(4,$ap,$j,4));	# ap[j+1]
	&psrlq	($car0,32);
	&movd	(&DWP($frame-4,"esp",$j,4),$car1);	# tp[j-1]=
	&psrlq	($car1,32);

	&lea	($j,&DWP(1,$j));
	&cmp	($j,$num);
	&jl	(&label("1st"));

	&pmuludq($acc0,$mul0);			# ap[num-1]*bp[0]
	&pmuludq($acc1,$mul1);			# np[num-1]*m1
	&paddq	($car0,$acc0);			# +=c0
	&paddq	($car1,$acc1);			# +=c1

	&movq	($acc0,$car0);
	&pand	($acc0,$mask);
	&paddq	($car1,$acc0);			# +=ap[num-1]*bp[0];
	&movd	(&DWP($frame-4,"esp",$j,4),$car1);	# tp[num-2]=

	&psrlq	($car0,32);
	&psrlq	($car1,32);

	&paddq	($car1,$car0);
	&movq	(&QWP($frame,"esp",$num,4),$car1);	# tp[num].tp[num-1]

	&inc	($i);				# i++
&set_label("outer");
	&xor	($j,$j);			# j=0

	&movd	($mul0,&DWP(0,$bp,$i,4));	# bp[i]
	&movd	($mul1,&DWP(0,$ap));		# ap[0]
	&movd	($temp,&DWP($frame,"esp"));	# tp[0]
	&movd	($car1,&DWP(0,$np));		# np[0]
	&pmuludq($mul1,$mul0);			# ap[0]*bp[i]

	&paddq	($mul1,$temp);			# +=tp[0]
	&movq	($acc0,$mul1);
	&movq	($car0,$mul1);
	&pand	($acc0,$mask);

	&pmuludq($mul1,$_n0q);			# *=n0

	&pmuludq($car1,$mul1);
	&paddq	($car1,$acc0);

	&movd	($temp,&DWP($frame+4,"esp"));	# tp[1]
	&movd	($acc1,&DWP(4,$np));		# np[1]
	&movd	($acc0,&DWP(4,$ap));		# ap[1]

	&psrlq	($car0,32);
	&psrlq	($car1,32);
	&paddq	($car0,$temp);			# +=tp[1]

	&inc	($j);				# j++
	&dec	($num);
&set_label("inner");
	&pmuludq($acc0,$mul0);			# ap[j]*bp[i]
	&pmuludq($acc1,$mul1);			# np[j]*m1
	&paddq	($car0,$acc0);			# +=c0
	&paddq	($car1,$acc1);			# +=c1

	&movq	($acc0,$car0);
	&movd	($temp,&DWP($frame+4,"esp",$j,4));# tp[j+1]
	&pand	($acc0,$mask);
	&movd	($acc1,&DWP(4,$np,$j,4));	# np[j+1]
	&paddq	($car1,$acc0);			# +=ap[j]*bp[i]+tp[j]
	&movd	($acc0,&DWP(4,$ap,$j,4));	# ap[j+1]
	&psrlq	($car0,32);
	&movd	(&DWP($frame-4,"esp",$j,4),$car1);# tp[j-1]=
	&psrlq	($car1,32);
	&paddq	($car0,$temp);			# +=tp[j+1]

	&dec	($num);
	&lea	($j,&DWP(1,$j));		# j++
	&jnz	(&label("inner"));

	&mov	($num,$j);
	&pmuludq($acc0,$mul0);			# ap[num-1]*bp[i]
	&pmuludq($acc1,$mul1);			# np[num-1]*m1
	&paddq	($car0,$acc0);			# +=c0
	&paddq	($car1,$acc1);			# +=c1

	&movq	($acc0,$car0);
	&pand	($acc0,$mask);
	&paddq	($car1,$acc0);			# +=ap[num-1]*bp[i]+tp[num-1]
	&movd	(&DWP($frame-4,"esp",$j,4),$car1);	# tp[num-2]=
	&psrlq	($car0,32);
	&psrlq	($car1,32);

	&movd	($temp,&DWP($frame+4,"esp",$num,4));	# += tp[num]
	&paddq	($car1,$car0);
	&paddq	($car1,$temp);
	&movq	(&QWP($frame,"esp",$num,4),$car1);	# tp[num].tp[num-1]

	&lea	($i,&DWP(1,$i));		# i++
	&cmp	($i,$num);
	&jle	(&label("outer"));

	&emms	();				# done with mmx bank
	&jmp	(&label("common_tail"));

&set_label("non_sse2",16);
}

if (0) {
	&mov	("esp",$_sp);
	&xor	("eax","eax");	# signal "not fast enough [yet]"
	&jmp	(&label("just_leave"));
	# While the below code provides competitive performance for
	# all key lengths on modern Intel cores, it's still more
	# than 10% slower for 4096-bit key elsewhere:-( "Competitive"
	# means compared to the original integer-only assembler.
	# 512-bit RSA sign is better by ~40%, but that's about all
	# one can say about all CPUs...
} else {
$inp="esi";	# integer path uses these registers differently
$word="edi";
$carry="ebp";

	&mov	($inp,$_ap);
	&lea	($carry,&DWP(1,$num));
	&mov	($word,$_bp);
	&xor	($j,$j);				# j=0
	&mov	("edx",$inp);
	&and	($carry,1);				# see if num is even
	&sub	("edx",$word);				# see if ap==bp
	&lea	("eax",&DWP(4,$word,$num,4));		# &bp[num]
	&or	($carry,"edx");
	&mov	($word,&DWP(0,$word));			# bp[0]
	&jz	(&label("bn_sqr_mont"));
	&mov	($_bpend,"eax");
	&mov	("eax",&DWP(0,$inp));
	&xor	("edx","edx");

&set_label("mull",16);
	&mov	($carry,"edx");
	&mul	($word);				# ap[j]*bp[0]
	&add	($carry,"eax");
	&lea	($j,&DWP(1,$j));
	&adc	("edx",0);
	&mov	("eax",&DWP(0,$inp,$j,4));		# ap[j+1]
	&cmp	($j,$num);
	&mov	(&DWP($frame-4,"esp",$j,4),$carry);	# tp[j]=
	&jl	(&label("mull"));

	&mov	($carry,"edx");
	&mul	($word);				# ap[num-1]*bp[0]
	 &mov	($word,$_n0);
	&add	("eax",$carry);
	 &mov	($inp,$_np);
	&adc	("edx",0);
	 &imul	($word,&DWP($frame,"esp"));		# n0*tp[0]

	&mov	(&DWP($frame,"esp",$num,4),"eax");	# tp[num-1]=
	&xor	($j,$j);
	&mov	(&DWP($frame+4,"esp",$num,4),"edx");	# tp[num]=
	&mov	(&DWP($frame+8,"esp",$num,4),$j);	# tp[num+1]=

	&mov	("eax",&DWP(0,$inp));			# np[0]
	&mul	($word);				# np[0]*m
	&add	("eax",&DWP($frame,"esp"));		# +=tp[0]
	&mov	("eax",&DWP(4,$inp));			# np[1]
	&adc	("edx",0);
	&inc	($j);

	&jmp	(&label("2ndmadd"));

&set_label("1stmadd",16);
	&mov	($carry,"edx");
	&mul	($word);				# ap[j]*bp[i]
	&add	($carry,&DWP($frame,"esp",$j,4));	# +=tp[j]
	&lea	($j,&DWP(1,$j));
	&adc	("edx",0);
	&add	($carry,"eax");
	&mov	("eax",&DWP(0,$inp,$j,4));		# ap[j+1]
	&adc	("edx",0);
	&cmp	($j,$num);
	&mov	(&DWP($frame-4,"esp",$j,4),$carry);	# tp[j]=
	&jl	(&label("1stmadd"));

	&mov	($carry,"edx");
	&mul	($word);				# ap[num-1]*bp[i]
	&add	("eax",&DWP($frame,"esp",$num,4));	# +=tp[num-1]
	 &mov	($word,$_n0);
	&adc	("edx",0);
	 &mov	($inp,$_np);
	&add	($carry,"eax");
	&adc	("edx",0);
	 &imul	($word,&DWP($frame,"esp"));		# n0*tp[0]

	&xor	($j,$j);
	&add	("edx",&DWP($frame+4,"esp",$num,4));	# carry+=tp[num]
	&mov	(&DWP($frame,"esp",$num,4),$carry);	# tp[num-1]=
	&adc	($j,0);
	 &mov	("eax",&DWP(0,$inp));			# np[0]
	&mov	(&DWP($frame+4,"esp",$num,4),"edx");	# tp[num]=
	&mov	(&DWP($frame+8,"esp",$num,4),$j);	# tp[num+1]=

	&mul	($word);				# np[0]*m
	&add	("eax",&DWP($frame,"esp"));		# +=tp[0]
	&mov	("eax",&DWP(4,$inp));			# np[1]
	&adc	("edx",0);
	&mov	($j,1);

&set_label("2ndmadd",16);
	&mov	($carry,"edx");
	&mul	($word);				# np[j]*m
	&add	($carry,&DWP($frame,"esp",$j,4));	# +=tp[j]
	&lea	($j,&DWP(1,$j));
	&adc	("edx",0);
	&add	($carry,"eax");
	&mov	("eax",&DWP(0,$inp,$j,4));		# np[j+1]
	&adc	("edx",0);
	&cmp	($j,$num);
	&mov	(&DWP($frame-8,"esp",$j,4),$carry);	# tp[j-1]=
	&jl	(&label("2ndmadd"));

	&mov	($carry,"edx");
	&mul	($word);				# np[j]*m
	&add	($carry,&DWP($frame,"esp",$num,4));	# +=tp[num-1]
	&adc	("edx",0);
	&add	($carry,"eax");
	&adc	("edx",0);
	&mov	(&DWP($frame-4,"esp",$num,4),$carry);	# tp[num-2]=

	&xor	("eax","eax");
	 &mov	($j,$_bp);				# &bp[i]
	&add	("edx",&DWP($frame+4,"esp",$num,4));	# carry+=tp[num]
	&adc	("eax",&DWP($frame+8,"esp",$num,4));	# +=tp[num+1]
	 &lea	($j,&DWP(4,$j));
	&mov	(&DWP($frame,"esp",$num,4),"edx");	# tp[num-1]=
	 &cmp	($j,$_bpend);
	&mov	(&DWP($frame+4,"esp",$num,4),"eax");	# tp[num]=
	&je	(&label("common_tail"));

	&mov	($word,&DWP(0,$j));			# bp[i+1]
	&mov	($inp,$_ap);
	&mov	($_bp,$j);				# &bp[++i]
	&xor	($j,$j);
	&xor	("edx","edx");
	&mov	("eax",&DWP(0,$inp));
	&jmp	(&label("1stmadd"));

&set_label("bn_sqr_mont",16);
$sbit=$num;
	&mov	($_num,$num);
	&mov	($_bp,$j);				# i=0

	&mov	("eax",$word);				# ap[0]
	&mul	($word);				# ap[0]*ap[0]
	&mov	(&DWP($frame,"esp"),"eax");		# tp[0]=
	&mov	($sbit,"edx");
	&shr	("edx",1);
	&and	($sbit,1);
	&inc	($j);
&set_label("sqr",16);
	&mov	("eax",&DWP(0,$inp,$j,4));		# ap[j]
	&mov	($carry,"edx");
	&mul	($word);				# ap[j]*ap[0]
	&add	("eax",$carry);
	&lea	($j,&DWP(1,$j));
	&adc	("edx",0);
	&lea	($carry,&DWP(0,$sbit,"eax",2));
	&shr	("eax",31);
	&cmp	($j,$_num);
	&mov	($sbit,"eax");
	&mov	(&DWP($frame-4,"esp",$j,4),$carry);	# tp[j]=
	&jl	(&label("sqr"));

	&mov	("eax",&DWP(0,$inp,$j,4));		# ap[num-1]
	&mov	($carry,"edx");
	&mul	($word);				# ap[num-1]*ap[0]
	&add	("eax",$carry);
	 &mov	($word,$_n0);
	&adc	("edx",0);
	 &mov	($inp,$_np);
	&lea	($carry,&DWP(0,$sbit,"eax",2));
	 &imul	($word,&DWP($frame,"esp"));		# n0*tp[0]
	&shr	("eax",31);
	&mov	(&DWP($frame,"esp",$j,4),$carry);	# tp[num-1]=

	&lea	($carry,&DWP(0,"eax","edx",2));
	 &mov	("eax",&DWP(0,$inp));			# np[0]
	&shr	("edx",31);
	&mov	(&DWP($frame+4,"esp",$j,4),$carry);	# tp[num]=
	&mov	(&DWP($frame+8,"esp",$j,4),"edx");	# tp[num+1]=

	&mul	($word);				# np[0]*m
	&add	("eax",&DWP($frame,"esp"));		# +=tp[0]
	&mov	($num,$j);
	&adc	("edx",0);
	&mov	("eax",&DWP(4,$inp));			# np[1]
	&mov	($j,1);

&set_label("3rdmadd",16);
	&mov	($carry,"edx");
	&mul	($word);				# np[j]*m
	&add	($carry,&DWP($frame,"esp",$j,4));	# +=tp[j]
	&adc	("edx",0);
	&add	($carry,"eax");
	&mov	("eax",&DWP(4,$inp,$j,4));		# np[j+1]
	&adc	("edx",0);
	&mov	(&DWP($frame-4,"esp",$j,4),$carry);	# tp[j-1]=

	&mov	($carry,"edx");
	&mul	($word);				# np[j+1]*m
	&add	($carry,&DWP($frame+4,"esp",$j,4));	# +=tp[j+1]
	&lea	($j,&DWP(2,$j));
	&adc	("edx",0);
	&add	($carry,"eax");
	&mov	("eax",&DWP(0,$inp,$j,4));		# np[j+2]
	&adc	("edx",0);
	&cmp	($j,$num);
	&mov	(&DWP($frame-8,"esp",$j,4),$carry);	# tp[j]=
	&jl	(&label("3rdmadd"));

	&mov	($carry,"edx");
	&mul	($word);				# np[j]*m
	&add	($carry,&DWP($frame,"esp",$num,4));	# +=tp[num-1]
	&adc	("edx",0);
	&add	($carry,"eax");
	&adc	("edx",0);
	&mov	(&DWP($frame-4,"esp",$num,4),$carry);	# tp[num-2]=

	&mov	($j,$_bp);				# i
	&xor	("eax","eax");
	&mov	($inp,$_ap);
	&add	("edx",&DWP($frame+4,"esp",$num,4));	# carry+=tp[num]
	&adc	("eax",&DWP($frame+8,"esp",$num,4));	# +=tp[num+1]
	&mov	(&DWP($frame,"esp",$num,4),"edx");	# tp[num-1]=
	&cmp	($j,$num);
	&mov	(&DWP($frame+4,"esp",$num,4),"eax");	# tp[num]=
	&je	(&label("common_tail"));

	&mov	($word,&DWP(4,$inp,$j,4));		# ap[i]
	&lea	($j,&DWP(1,$j));
	&mov	("eax",$word);
	&mov	($_bp,$j);				# ++i
	&mul	($word);				# ap[i]*ap[i]
	&add	("eax",&DWP($frame,"esp",$j,4));	# +=tp[i]
	&adc	("edx",0);
	&mov	(&DWP($frame,"esp",$j,4),"eax");	# tp[i]=
	&xor	($carry,$carry);
	&cmp	($j,$num);
	&lea	($j,&DWP(1,$j));
	&je	(&label("sqrlast"));

	&mov	($sbit,"edx");				# zaps $num
	&shr	("edx",1);
	&and	($sbit,1);
&set_label("sqradd",16);
	&mov	("eax",&DWP(0,$inp,$j,4));		# ap[j]
	&mov	($carry,"edx");
	&mul	($word);				# ap[j]*ap[i]
	&add	("eax",$carry);
	&lea	($carry,&DWP(0,"eax","eax"));
	&adc	("edx",0);
	&shr	("eax",31);
	&add	($carry,&DWP($frame,"esp",$j,4));	# +=tp[j]
	&lea	($j,&DWP(1,$j));
	&adc	("eax",0);
	&add	($carry,$sbit);
	&adc	("eax",0);
	&cmp	($j,$_num);
	&mov	(&DWP($frame-4,"esp",$j,4),$carry);	# tp[j]=
	&mov	($sbit,"eax");
	&jle	(&label("sqradd"));

	&mov	($carry,"edx");
	&add	("edx","edx");
	&shr	($carry,31);
	&add	("edx",$sbit);
	&adc	($carry,0);
&set_label("sqrlast");
	&mov	($word,$_n0);
	&mov	($inp,$_np);
	&imul	($word,&DWP($frame,"esp"));		# n0*tp[0]

	&add	("edx",&DWP($frame,"esp",$j,4));	# +=tp[num]
	&mov	("eax",&DWP(0,$inp));			# np[0]
	&adc	($carry,0);
	&mov	(&DWP($frame,"esp",$j,4),"edx");	# tp[num]=
	&mov	(&DWP($frame+4,"esp",$j,4),$carry);	# tp[num+1]=

	&mul	($word);				# np[0]*m
	&add	("eax",&DWP($frame,"esp"));		# +=tp[0]
	&lea	($num,&DWP(-1,$j));
	&adc	("edx",0);
	&mov	($j,1);
	&mov	("eax",&DWP(4,$inp));			# np[1]

	&jmp	(&label("3rdmadd"));
}

&set_label("common_tail",16);
	&mov	($np,$_np);			# load modulus pointer
	&mov	($rp,$_rp);			# load result pointer
	&lea	($tp,&DWP($frame,"esp"));	# [$ap and $bp are zapped]

	&mov	("eax",&DWP(0,$tp));		# tp[0]
	&mov	($j,$num);			# j=num-1
	&xor	($i,$i);			# i=0 and clear CF!

&set_label("sub",16);
	&sbb	("eax",&DWP(0,$np,$i,4));
	&mov	(&DWP(0,$rp,$i,4),"eax");	# rp[i]=tp[i]-np[i]
	&dec	($j);				# doesn't affect CF!
	&mov	("eax",&DWP(4,$tp,$i,4));	# tp[i+1]
	&lea	($i,&DWP(1,$i));		# i++
	&jge	(&label("sub"));

	&sbb	("eax",0);			# handle upmost overflow bit
	&mov	("edx",-1);
	&xor	("edx","eax");
	&jmp	(&label("copy"));

&set_label("copy",16);				# conditional copy
	&mov	($tp,&DWP($frame,"esp",$num,4));
	&mov	($np,&DWP(0,$rp,$num,4));
	&mov	(&DWP($frame,"esp",$num,4),$j);	# zap temporary vector
	&and	($tp,"eax");
	&and	($np,"edx");
	&or	($np,$tp);
	&mov	(&DWP(0,$rp,$num,4),$np);
	&dec	($num);
	&jge	(&label("copy"));

	&mov	("esp",$_sp);		# pull saved stack pointer
	&mov	("eax",1);
&set_label("just_leave");
&function_end("bn_mul_mont");

&asciz("Montgomery Multiplication for x86, CRYPTOGAMS by <appro\@openssl.org>");

&asm_finish();

close STDOUT;
