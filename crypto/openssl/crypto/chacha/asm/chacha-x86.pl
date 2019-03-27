#! /usr/bin/env perl
# Copyright 2016-2018 The OpenSSL Project Authors. All Rights Reserved.
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
# January 2015
#
# ChaCha20 for x86.
#
# Performance in cycles per byte out of large buffer.
#
#		1xIALU/gcc	4xSSSE3
# Pentium	17.5/+80%
# PIII		14.2/+60%
# P4		18.6/+84%
# Core2		9.56/+89%	4.83
# Westmere	9.50/+45%	3.35
# Sandy Bridge	10.5/+47%	3.20
# Haswell	8.15/+50%	2.83
# Skylake	7.53/+22%	2.75
# Silvermont	17.4/+36%	8.35
# Goldmont	13.4/+40%	4.36
# Sledgehammer	10.2/+54%
# Bulldozer	13.4/+50%	4.38(*)
#
# (*)	Bulldozer actually executes 4xXOP code path that delivers 3.55;

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";

$output=pop;
open STDOUT,">$output";

&asm_init($ARGV[0],$ARGV[$#ARGV] eq "386");

$xmm=$ymm=0;
for (@ARGV) { $xmm=1 if (/-DOPENSSL_IA32_SSE2/); }

$ymm=1 if ($xmm &&
		`$ENV{CC} -Wa,-v -c -o /dev/null -x assembler /dev/null 2>&1`
			=~ /GNU assembler version ([2-9]\.[0-9]+)/ &&
		($gasver=$1)>=2.19);	# first version supporting AVX

$ymm=1 if ($xmm && !$ymm && $ARGV[0] eq "win32n" &&
		`nasm -v 2>&1` =~ /NASM version ([2-9]\.[0-9]+)/ &&
		$1>=2.03);	# first version supporting AVX

$ymm=1 if ($xmm && !$ymm && $ARGV[0] eq "win32" &&
		`ml 2>&1` =~ /Version ([0-9]+)\./ &&
		$1>=10);	# first version supporting AVX

$ymm=1 if ($xmm && !$ymm &&
		`$ENV{CC} -v 2>&1` =~ /((?:^clang|LLVM) version|based on LLVM) ([3-9]\.[0-9]+)/ &&
		$2>=3.0);	# first version supporting AVX

$a="eax";
($b,$b_)=("ebx","ebp");
($c,$c_)=("ecx","esi");
($d,$d_)=("edx","edi");

sub QUARTERROUND {
my ($ai,$bi,$ci,$di,$i)=@_;
my ($an,$bn,$cn,$dn)=map(($_&~3)+(($_+1)&3),($ai,$bi,$ci,$di));	# next
my ($ap,$bp,$cp,$dp)=map(($_&~3)+(($_-1)&3),($ai,$bi,$ci,$di));	# previous

	#       a   b   c   d
	#
	#       0   4   8  12 < even round
	#       1   5   9  13
	#       2   6  10  14
	#       3   7  11  15
	#       0   5  10  15 < odd round
	#       1   6  11  12
	#       2   7   8  13
	#       3   4   9  14

	if ($i==0) {
            my $j=4;
	    ($ap,$bp,$cp,$dp)=map(($_&~3)+(($_-$j--)&3),($ap,$bp,$cp,$dp));
	} elsif ($i==3) {
            my $j=0;
	    ($an,$bn,$cn,$dn)=map(($_&~3)+(($_+$j++)&3),($an,$bn,$cn,$dn));
	} elsif ($i==4) {
            my $j=4;
	    ($ap,$bp,$cp,$dp)=map(($_&~3)+(($_+$j--)&3),($ap,$bp,$cp,$dp));
	} elsif ($i==7) {
            my $j=0;
	    ($an,$bn,$cn,$dn)=map(($_&~3)+(($_-$j++)&3),($an,$bn,$cn,$dn));
	}

	#&add	($a,$b);			# see elsewhere
	&xor	($d,$a);
	 &mov	(&DWP(4*$cp,"esp"),$c_)		if ($ai>0 && $ai<3);
	&rol	($d,16);
	 &mov	(&DWP(4*$bp,"esp"),$b_)		if ($i!=0);
	&add	($c,$d);
	 &mov	($c_,&DWP(4*$cn,"esp"))		if ($ai>0 && $ai<3);
	&xor	($b,$c);
	 &mov	($d_,&DWP(4*$dn,"esp"))		if ($di!=$dn);
	&rol	($b,12);
	 &mov	($b_,&DWP(4*$bn,"esp"))		if ($i<7);
	 &mov	($b_,&DWP(128,"esp"))		if ($i==7);	# loop counter
	&add	($a,$b);
	&xor	($d,$a);
	&mov	(&DWP(4*$ai,"esp"),$a);
	&rol	($d,8);
	&mov	($a,&DWP(4*$an,"esp"));
	&add	($c,$d);
	&mov	(&DWP(4*$di,"esp"),$d)		if ($di!=$dn);
	&mov	($d_,$d)			if ($di==$dn);
	&xor	($b,$c);
	 &add	($a,$b_)			if ($i<7);	# elsewhere
	&rol	($b,7);

	($b,$b_)=($b_,$b);
	($c,$c_)=($c_,$c);
	($d,$d_)=($d_,$d);
}

&static_label("ssse3_shortcut");
&static_label("xop_shortcut");
&static_label("ssse3_data");
&static_label("pic_point");

&function_begin("ChaCha20_ctr32");
	&xor	("eax","eax");
	&cmp	("eax",&wparam(2));		# len==0?
	&je	(&label("no_data"));
if ($xmm) {
	&call	(&label("pic_point"));
&set_label("pic_point");
	&blindpop("eax");
	&picmeup("ebp","OPENSSL_ia32cap_P","eax",&label("pic_point"));
	&test	(&DWP(0,"ebp"),1<<24);		# test FXSR bit
	&jz	(&label("x86"));
	&test	(&DWP(4,"ebp"),1<<9);		# test SSSE3 bit
	&jz	(&label("x86"));
	&jmp	(&label("ssse3_shortcut"));
&set_label("x86");
}
	&mov	("esi",&wparam(3));		# key
	&mov	("edi",&wparam(4));		# counter and nonce

	&stack_push(33);

	&mov	("eax",&DWP(4*0,"esi"));	# copy key
	&mov	("ebx",&DWP(4*1,"esi"));
	&mov	("ecx",&DWP(4*2,"esi"));
	&mov	("edx",&DWP(4*3,"esi"));
	&mov	(&DWP(64+4*4,"esp"),"eax");
	&mov	(&DWP(64+4*5,"esp"),"ebx");
	&mov	(&DWP(64+4*6,"esp"),"ecx");
	&mov	(&DWP(64+4*7,"esp"),"edx");
	&mov	("eax",&DWP(4*4,"esi"));
	&mov	("ebx",&DWP(4*5,"esi"));
	&mov	("ecx",&DWP(4*6,"esi"));
	&mov	("edx",&DWP(4*7,"esi"));
	&mov	(&DWP(64+4*8,"esp"),"eax");
	&mov	(&DWP(64+4*9,"esp"),"ebx");
	&mov	(&DWP(64+4*10,"esp"),"ecx");
	&mov	(&DWP(64+4*11,"esp"),"edx");
	&mov	("eax",&DWP(4*0,"edi"));	# copy counter and nonce
	&mov	("ebx",&DWP(4*1,"edi"));
	&mov	("ecx",&DWP(4*2,"edi"));
	&mov	("edx",&DWP(4*3,"edi"));
	&sub	("eax",1);
	&mov	(&DWP(64+4*12,"esp"),"eax");
	&mov	(&DWP(64+4*13,"esp"),"ebx");
	&mov	(&DWP(64+4*14,"esp"),"ecx");
	&mov	(&DWP(64+4*15,"esp"),"edx");
	&jmp	(&label("entry"));

&set_label("outer_loop",16);
	&mov	(&wparam(1),$b);		# save input
	&mov	(&wparam(0),$a);		# save output
	&mov	(&wparam(2),$c);		# save len
&set_label("entry");
	&mov	($a,0x61707865);
	&mov	(&DWP(4*1,"esp"),0x3320646e);
	&mov	(&DWP(4*2,"esp"),0x79622d32);
	&mov	(&DWP(4*3,"esp"),0x6b206574);

	&mov	($b, &DWP(64+4*5,"esp"));	# copy key material
	&mov	($b_,&DWP(64+4*6,"esp"));
	&mov	($c, &DWP(64+4*10,"esp"));
	&mov	($c_,&DWP(64+4*11,"esp"));
	&mov	($d, &DWP(64+4*13,"esp"));
	&mov	($d_,&DWP(64+4*14,"esp"));
	&mov	(&DWP(4*5,"esp"),$b);
	&mov	(&DWP(4*6,"esp"),$b_);
	&mov	(&DWP(4*10,"esp"),$c);
	&mov	(&DWP(4*11,"esp"),$c_);
	&mov	(&DWP(4*13,"esp"),$d);
	&mov	(&DWP(4*14,"esp"),$d_);

	&mov	($b, &DWP(64+4*7,"esp"));
	&mov	($d_,&DWP(64+4*15,"esp"));
	&mov	($d, &DWP(64+4*12,"esp"));
	&mov	($b_,&DWP(64+4*4,"esp"));
	&mov	($c, &DWP(64+4*8,"esp"));
	&mov	($c_,&DWP(64+4*9,"esp"));
	&add	($d,1);				# counter value
	&mov	(&DWP(4*7,"esp"),$b);
	&mov	(&DWP(4*15,"esp"),$d_);
	&mov	(&DWP(64+4*12,"esp"),$d);	# save counter value

	&mov	($b,10);			# loop counter
	&jmp	(&label("loop"));

&set_label("loop",16);
	&add	($a,$b_);			# elsewhere
	&mov	(&DWP(128,"esp"),$b);		# save loop counter
	&mov	($b,$b_);
	&QUARTERROUND(0, 4, 8, 12, 0);
	&QUARTERROUND(1, 5, 9, 13, 1);
	&QUARTERROUND(2, 6,10, 14, 2);
	&QUARTERROUND(3, 7,11, 15, 3);
	&QUARTERROUND(0, 5,10, 15, 4);
	&QUARTERROUND(1, 6,11, 12, 5);
	&QUARTERROUND(2, 7, 8, 13, 6);
	&QUARTERROUND(3, 4, 9, 14, 7);
	&dec	($b);
	&jnz	(&label("loop"));

	&mov	($b,&wparam(2));		# load len

	&add	($a,0x61707865);		# accumulate key material
	&add	($b_,&DWP(64+4*4,"esp"));
	&add	($c, &DWP(64+4*8,"esp"));
	&add	($c_,&DWP(64+4*9,"esp"));

	&cmp	($b,64);
	&jb	(&label("tail"));

	&mov	($b,&wparam(1));		# load input pointer
	&add	($d, &DWP(64+4*12,"esp"));
	&add	($d_,&DWP(64+4*14,"esp"));

	&xor	($a, &DWP(4*0,$b));		# xor with input
	&xor	($b_,&DWP(4*4,$b));
	&mov	(&DWP(4*0,"esp"),$a);
	&mov	($a,&wparam(0));		# load output pointer
	&xor	($c, &DWP(4*8,$b));
	&xor	($c_,&DWP(4*9,$b));
	&xor	($d, &DWP(4*12,$b));
	&xor	($d_,&DWP(4*14,$b));
	&mov	(&DWP(4*4,$a),$b_);		# write output
	&mov	(&DWP(4*8,$a),$c);
	&mov	(&DWP(4*9,$a),$c_);
	&mov	(&DWP(4*12,$a),$d);
	&mov	(&DWP(4*14,$a),$d_);

	&mov	($b_,&DWP(4*1,"esp"));
	&mov	($c, &DWP(4*2,"esp"));
	&mov	($c_,&DWP(4*3,"esp"));
	&mov	($d, &DWP(4*5,"esp"));
	&mov	($d_,&DWP(4*6,"esp"));
	&add	($b_,0x3320646e);		# accumulate key material
	&add	($c, 0x79622d32);
	&add	($c_,0x6b206574);
	&add	($d, &DWP(64+4*5,"esp"));
	&add	($d_,&DWP(64+4*6,"esp"));
	&xor	($b_,&DWP(4*1,$b));
	&xor	($c, &DWP(4*2,$b));
	&xor	($c_,&DWP(4*3,$b));
	&xor	($d, &DWP(4*5,$b));
	&xor	($d_,&DWP(4*6,$b));
	&mov	(&DWP(4*1,$a),$b_);
	&mov	(&DWP(4*2,$a),$c);
	&mov	(&DWP(4*3,$a),$c_);
	&mov	(&DWP(4*5,$a),$d);
	&mov	(&DWP(4*6,$a),$d_);

	&mov	($b_,&DWP(4*7,"esp"));
	&mov	($c, &DWP(4*10,"esp"));
	&mov	($c_,&DWP(4*11,"esp"));
	&mov	($d, &DWP(4*13,"esp"));
	&mov	($d_,&DWP(4*15,"esp"));
	&add	($b_,&DWP(64+4*7,"esp"));
	&add	($c, &DWP(64+4*10,"esp"));
	&add	($c_,&DWP(64+4*11,"esp"));
	&add	($d, &DWP(64+4*13,"esp"));
	&add	($d_,&DWP(64+4*15,"esp"));
	&xor	($b_,&DWP(4*7,$b));
	&xor	($c, &DWP(4*10,$b));
	&xor	($c_,&DWP(4*11,$b));
	&xor	($d, &DWP(4*13,$b));
	&xor	($d_,&DWP(4*15,$b));
	&lea	($b,&DWP(4*16,$b));
	&mov	(&DWP(4*7,$a),$b_);
	&mov	($b_,&DWP(4*0,"esp"));
	&mov	(&DWP(4*10,$a),$c);
	&mov	($c,&wparam(2));		# len
	&mov	(&DWP(4*11,$a),$c_);
	&mov	(&DWP(4*13,$a),$d);
	&mov	(&DWP(4*15,$a),$d_);
	&mov	(&DWP(4*0,$a),$b_);
	&lea	($a,&DWP(4*16,$a));
	&sub	($c,64);
	&jnz	(&label("outer_loop"));

	&jmp	(&label("done"));

&set_label("tail");
	&add	($d, &DWP(64+4*12,"esp"));
	&add	($d_,&DWP(64+4*14,"esp"));
	&mov	(&DWP(4*0,"esp"),$a);
	&mov	(&DWP(4*4,"esp"),$b_);
	&mov	(&DWP(4*8,"esp"),$c);
	&mov	(&DWP(4*9,"esp"),$c_);
	&mov	(&DWP(4*12,"esp"),$d);
	&mov	(&DWP(4*14,"esp"),$d_);

	&mov	($b_,&DWP(4*1,"esp"));
	&mov	($c, &DWP(4*2,"esp"));
	&mov	($c_,&DWP(4*3,"esp"));
	&mov	($d, &DWP(4*5,"esp"));
	&mov	($d_,&DWP(4*6,"esp"));
	&add	($b_,0x3320646e);		# accumulate key material
	&add	($c, 0x79622d32);
	&add	($c_,0x6b206574);
	&add	($d, &DWP(64+4*5,"esp"));
	&add	($d_,&DWP(64+4*6,"esp"));
	&mov	(&DWP(4*1,"esp"),$b_);
	&mov	(&DWP(4*2,"esp"),$c);
	&mov	(&DWP(4*3,"esp"),$c_);
	&mov	(&DWP(4*5,"esp"),$d);
	&mov	(&DWP(4*6,"esp"),$d_);

	&mov	($b_,&DWP(4*7,"esp"));
	&mov	($c, &DWP(4*10,"esp"));
	&mov	($c_,&DWP(4*11,"esp"));
	&mov	($d, &DWP(4*13,"esp"));
	&mov	($d_,&DWP(4*15,"esp"));
	&add	($b_,&DWP(64+4*7,"esp"));
	&add	($c, &DWP(64+4*10,"esp"));
	&add	($c_,&DWP(64+4*11,"esp"));
	&add	($d, &DWP(64+4*13,"esp"));
	&add	($d_,&DWP(64+4*15,"esp"));
	&mov	(&DWP(4*7,"esp"),$b_);
	&mov	($b_,&wparam(1));		# load input
	&mov	(&DWP(4*10,"esp"),$c);
	&mov	($c,&wparam(0));		# load output
	&mov	(&DWP(4*11,"esp"),$c_);
	&xor	($c_,$c_);
	&mov	(&DWP(4*13,"esp"),$d);
	&mov	(&DWP(4*15,"esp"),$d_);

	&xor	("eax","eax");
	&xor	("edx","edx");
&set_label("tail_loop");
	&movb	("al",&BP(0,$c_,$b_));
	&movb	("dl",&BP(0,"esp",$c_));
	&lea	($c_,&DWP(1,$c_));
	&xor	("al","dl");
	&mov	(&BP(-1,$c,$c_),"al");
	&dec	($b);
	&jnz	(&label("tail_loop"));

&set_label("done");
	&stack_pop(33);
&set_label("no_data");
&function_end("ChaCha20_ctr32");

if ($xmm) {
my ($xa,$xa_,$xb,$xb_,$xc,$xc_,$xd,$xd_)=map("xmm$_",(0..7));
my ($out,$inp,$len)=("edi","esi","ecx");

sub QUARTERROUND_SSSE3 {
my ($ai,$bi,$ci,$di,$i)=@_;
my ($an,$bn,$cn,$dn)=map(($_&~3)+(($_+1)&3),($ai,$bi,$ci,$di));	# next
my ($ap,$bp,$cp,$dp)=map(($_&~3)+(($_-1)&3),($ai,$bi,$ci,$di));	# previous

	#       a   b   c   d
	#
	#       0   4   8  12 < even round
	#       1   5   9  13
	#       2   6  10  14
	#       3   7  11  15
	#       0   5  10  15 < odd round
	#       1   6  11  12
	#       2   7   8  13
	#       3   4   9  14

	if ($i==0) {
            my $j=4;
	    ($ap,$bp,$cp,$dp)=map(($_&~3)+(($_-$j--)&3),($ap,$bp,$cp,$dp));
	} elsif ($i==3) {
            my $j=0;
	    ($an,$bn,$cn,$dn)=map(($_&~3)+(($_+$j++)&3),($an,$bn,$cn,$dn));
	} elsif ($i==4) {
            my $j=4;
	    ($ap,$bp,$cp,$dp)=map(($_&~3)+(($_+$j--)&3),($ap,$bp,$cp,$dp));
	} elsif ($i==7) {
            my $j=0;
	    ($an,$bn,$cn,$dn)=map(($_&~3)+(($_-$j++)&3),($an,$bn,$cn,$dn));
	}

	#&paddd	($xa,$xb);			# see elsewhere
	#&pxor	($xd,$xa);			# see elsewhere
	 &movdqa(&QWP(16*$cp-128,"ebx"),$xc_)	if ($ai>0 && $ai<3);
	&pshufb	($xd,&QWP(0,"eax"));		# rot16
	 &movdqa(&QWP(16*$bp-128,"ebx"),$xb_)	if ($i!=0);
	&paddd	($xc,$xd);
	 &movdqa($xc_,&QWP(16*$cn-128,"ebx"))	if ($ai>0 && $ai<3);
	&pxor	($xb,$xc);
	 &movdqa($xb_,&QWP(16*$bn-128,"ebx"))	if ($i<7);
	&movdqa	($xa_,$xb);			# borrow as temporary
	&pslld	($xb,12);
	&psrld	($xa_,20);
	&por	($xb,$xa_);
	 &movdqa($xa_,&QWP(16*$an-128,"ebx"));
	&paddd	($xa,$xb);
	 &movdqa($xd_,&QWP(16*$dn-128,"ebx"))	if ($di!=$dn);
	&pxor	($xd,$xa);
	&movdqa	(&QWP(16*$ai-128,"ebx"),$xa);
	&pshufb	($xd,&QWP(16,"eax"));		# rot8
	&paddd	($xc,$xd);
	&movdqa	(&QWP(16*$di-128,"ebx"),$xd)	if ($di!=$dn);
	&movdqa	($xd_,$xd)			if ($di==$dn);
	&pxor	($xb,$xc);
	 &paddd	($xa_,$xb_)			if ($i<7);	# elsewhere
	&movdqa	($xa,$xb);			# borrow as temporary
	&pslld	($xb,7);
	&psrld	($xa,25);
	 &pxor	($xd_,$xa_)			if ($i<7);	# elsewhere
	&por	($xb,$xa);

	($xa,$xa_)=($xa_,$xa);
	($xb,$xb_)=($xb_,$xb);
	($xc,$xc_)=($xc_,$xc);
	($xd,$xd_)=($xd_,$xd);
}

&function_begin("ChaCha20_ssse3");
&set_label("ssse3_shortcut");
if ($ymm) {
	&test		(&DWP(4,"ebp"),1<<11);		# test XOP bit
	&jnz		(&label("xop_shortcut"));
}

	&mov		($out,&wparam(0));
	&mov		($inp,&wparam(1));
	&mov		($len,&wparam(2));
	&mov		("edx",&wparam(3));		# key
	&mov		("ebx",&wparam(4));		# counter and nonce

	&mov		("ebp","esp");
	&stack_push	(131);
	&and		("esp",-64);
	&mov		(&DWP(512,"esp"),"ebp");

	&lea		("eax",&DWP(&label("ssse3_data")."-".
				    &label("pic_point"),"eax"));
	&movdqu		("xmm3",&QWP(0,"ebx"));		# counter and nonce

if (defined($gasver) && $gasver>=2.17) {		# even though we encode
							# pshufb manually, we
							# handle only register
							# operands, while this
							# segment uses memory
							# operand...
	&cmp		($len,64*4);
	&jb		(&label("1x"));

	&mov		(&DWP(512+4,"esp"),"edx");	# offload pointers
	&mov		(&DWP(512+8,"esp"),"ebx");
	&sub		($len,64*4);			# bias len
	&lea		("ebp",&DWP(256+128,"esp"));	# size optimization

	&movdqu		("xmm7",&QWP(0,"edx"));		# key
	&pshufd		("xmm0","xmm3",0x00);
	&pshufd		("xmm1","xmm3",0x55);
	&pshufd		("xmm2","xmm3",0xaa);
	&pshufd		("xmm3","xmm3",0xff);
	 &paddd		("xmm0",&QWP(16*3,"eax"));	# fix counters
	&pshufd		("xmm4","xmm7",0x00);
	&pshufd		("xmm5","xmm7",0x55);
	 &psubd		("xmm0",&QWP(16*4,"eax"));
	&pshufd		("xmm6","xmm7",0xaa);
	&pshufd		("xmm7","xmm7",0xff);
	&movdqa		(&QWP(16*12-128,"ebp"),"xmm0");
	&movdqa		(&QWP(16*13-128,"ebp"),"xmm1");
	&movdqa		(&QWP(16*14-128,"ebp"),"xmm2");
	&movdqa		(&QWP(16*15-128,"ebp"),"xmm3");
	 &movdqu	("xmm3",&QWP(16,"edx"));	# key
	&movdqa		(&QWP(16*4-128,"ebp"),"xmm4");
	&movdqa		(&QWP(16*5-128,"ebp"),"xmm5");
	&movdqa		(&QWP(16*6-128,"ebp"),"xmm6");
	&movdqa		(&QWP(16*7-128,"ebp"),"xmm7");
	 &movdqa	("xmm7",&QWP(16*2,"eax"));	# sigma
	 &lea		("ebx",&DWP(128,"esp"));	# size optimization

	&pshufd		("xmm0","xmm3",0x00);
	&pshufd		("xmm1","xmm3",0x55);
	&pshufd		("xmm2","xmm3",0xaa);
	&pshufd		("xmm3","xmm3",0xff);
	&pshufd		("xmm4","xmm7",0x00);
	&pshufd		("xmm5","xmm7",0x55);
	&pshufd		("xmm6","xmm7",0xaa);
	&pshufd		("xmm7","xmm7",0xff);
	&movdqa		(&QWP(16*8-128,"ebp"),"xmm0");
	&movdqa		(&QWP(16*9-128,"ebp"),"xmm1");
	&movdqa		(&QWP(16*10-128,"ebp"),"xmm2");
	&movdqa		(&QWP(16*11-128,"ebp"),"xmm3");
	&movdqa		(&QWP(16*0-128,"ebp"),"xmm4");
	&movdqa		(&QWP(16*1-128,"ebp"),"xmm5");
	&movdqa		(&QWP(16*2-128,"ebp"),"xmm6");
	&movdqa		(&QWP(16*3-128,"ebp"),"xmm7");

	&lea		($inp,&DWP(128,$inp));		# size optimization
	&lea		($out,&DWP(128,$out));		# size optimization
	&jmp		(&label("outer_loop"));

&set_label("outer_loop",16);
	#&movdqa	("xmm0",&QWP(16*0-128,"ebp"));	# copy key material
	&movdqa		("xmm1",&QWP(16*1-128,"ebp"));
	&movdqa		("xmm2",&QWP(16*2-128,"ebp"));
	&movdqa		("xmm3",&QWP(16*3-128,"ebp"));
	#&movdqa	("xmm4",&QWP(16*4-128,"ebp"));
	&movdqa		("xmm5",&QWP(16*5-128,"ebp"));
	&movdqa		("xmm6",&QWP(16*6-128,"ebp"));
	&movdqa		("xmm7",&QWP(16*7-128,"ebp"));
	#&movdqa	(&QWP(16*0-128,"ebx"),"xmm0");
	&movdqa		(&QWP(16*1-128,"ebx"),"xmm1");
	&movdqa		(&QWP(16*2-128,"ebx"),"xmm2");
	&movdqa		(&QWP(16*3-128,"ebx"),"xmm3");
	#&movdqa	(&QWP(16*4-128,"ebx"),"xmm4");
	&movdqa		(&QWP(16*5-128,"ebx"),"xmm5");
	&movdqa		(&QWP(16*6-128,"ebx"),"xmm6");
	&movdqa		(&QWP(16*7-128,"ebx"),"xmm7");
	#&movdqa	("xmm0",&QWP(16*8-128,"ebp"));
	#&movdqa	("xmm1",&QWP(16*9-128,"ebp"));
	&movdqa		("xmm2",&QWP(16*10-128,"ebp"));
	&movdqa		("xmm3",&QWP(16*11-128,"ebp"));
	&movdqa		("xmm4",&QWP(16*12-128,"ebp"));
	&movdqa		("xmm5",&QWP(16*13-128,"ebp"));
	&movdqa		("xmm6",&QWP(16*14-128,"ebp"));
	&movdqa		("xmm7",&QWP(16*15-128,"ebp"));
	&paddd		("xmm4",&QWP(16*4,"eax"));	# counter value
	#&movdqa	(&QWP(16*8-128,"ebx"),"xmm0");
	#&movdqa	(&QWP(16*9-128,"ebx"),"xmm1");
	&movdqa		(&QWP(16*10-128,"ebx"),"xmm2");
	&movdqa		(&QWP(16*11-128,"ebx"),"xmm3");
	&movdqa		(&QWP(16*12-128,"ebx"),"xmm4");
	&movdqa		(&QWP(16*13-128,"ebx"),"xmm5");
	&movdqa		(&QWP(16*14-128,"ebx"),"xmm6");
	&movdqa		(&QWP(16*15-128,"ebx"),"xmm7");
	&movdqa		(&QWP(16*12-128,"ebp"),"xmm4");	# save counter value

	&movdqa		($xa, &QWP(16*0-128,"ebp"));
	&movdqa		($xd, "xmm4");
	&movdqa		($xb_,&QWP(16*4-128,"ebp"));
	&movdqa		($xc, &QWP(16*8-128,"ebp"));
	&movdqa		($xc_,&QWP(16*9-128,"ebp"));

	&mov		("edx",10);			# loop counter
	&nop		();

&set_label("loop",16);
	&paddd		($xa,$xb_);			# elsewhere
	&movdqa		($xb,$xb_);
	&pxor		($xd,$xa);			# elsewhere
	&QUARTERROUND_SSSE3(0, 4, 8, 12, 0);
	&QUARTERROUND_SSSE3(1, 5, 9, 13, 1);
	&QUARTERROUND_SSSE3(2, 6,10, 14, 2);
	&QUARTERROUND_SSSE3(3, 7,11, 15, 3);
	&QUARTERROUND_SSSE3(0, 5,10, 15, 4);
	&QUARTERROUND_SSSE3(1, 6,11, 12, 5);
	&QUARTERROUND_SSSE3(2, 7, 8, 13, 6);
	&QUARTERROUND_SSSE3(3, 4, 9, 14, 7);
	&dec		("edx");
	&jnz		(&label("loop"));

	&movdqa		(&QWP(16*4-128,"ebx"),$xb_);
	&movdqa		(&QWP(16*8-128,"ebx"),$xc);
	&movdqa		(&QWP(16*9-128,"ebx"),$xc_);
	&movdqa		(&QWP(16*12-128,"ebx"),$xd);
	&movdqa		(&QWP(16*14-128,"ebx"),$xd_);

    my ($xa0,$xa1,$xa2,$xa3,$xt0,$xt1,$xt2,$xt3)=map("xmm$_",(0..7));

	#&movdqa	($xa0,&QWP(16*0-128,"ebx"));	# it's there
	&movdqa		($xa1,&QWP(16*1-128,"ebx"));
	&movdqa		($xa2,&QWP(16*2-128,"ebx"));
	&movdqa		($xa3,&QWP(16*3-128,"ebx"));

    for($i=0;$i<256;$i+=64) {
	&paddd		($xa0,&QWP($i+16*0-128,"ebp"));	# accumulate key material
	&paddd		($xa1,&QWP($i+16*1-128,"ebp"));
	&paddd		($xa2,&QWP($i+16*2-128,"ebp"));
	&paddd		($xa3,&QWP($i+16*3-128,"ebp"));

	&movdqa		($xt2,$xa0);		# "de-interlace" data
	&punpckldq	($xa0,$xa1);
	&movdqa		($xt3,$xa2);
	&punpckldq	($xa2,$xa3);
	&punpckhdq	($xt2,$xa1);
	&punpckhdq	($xt3,$xa3);
	&movdqa		($xa1,$xa0);
	&punpcklqdq	($xa0,$xa2);		# "a0"
	&movdqa		($xa3,$xt2);
	&punpcklqdq	($xt2,$xt3);		# "a2"
	&punpckhqdq	($xa1,$xa2);		# "a1"
	&punpckhqdq	($xa3,$xt3);		# "a3"

	#($xa2,$xt2)=($xt2,$xa2);

	&movdqu		($xt0,&QWP(64*0-128,$inp));	# load input
	&movdqu		($xt1,&QWP(64*1-128,$inp));
	&movdqu		($xa2,&QWP(64*2-128,$inp));
	&movdqu		($xt3,&QWP(64*3-128,$inp));
	&lea		($inp,&QWP($i<192?16:(64*4-16*3),$inp));
	&pxor		($xt0,$xa0);
	&movdqa		($xa0,&QWP($i+16*4-128,"ebx"))	if ($i<192);
	&pxor		($xt1,$xa1);
	&movdqa		($xa1,&QWP($i+16*5-128,"ebx"))	if ($i<192);
	&pxor		($xt2,$xa2);
	&movdqa		($xa2,&QWP($i+16*6-128,"ebx"))	if ($i<192);
	&pxor		($xt3,$xa3);
	&movdqa		($xa3,&QWP($i+16*7-128,"ebx"))	if ($i<192);
	&movdqu		(&QWP(64*0-128,$out),$xt0);	# store output
	&movdqu		(&QWP(64*1-128,$out),$xt1);
	&movdqu		(&QWP(64*2-128,$out),$xt2);
	&movdqu		(&QWP(64*3-128,$out),$xt3);
	&lea		($out,&QWP($i<192?16:(64*4-16*3),$out));
    }
	&sub		($len,64*4);
	&jnc		(&label("outer_loop"));

	&add		($len,64*4);
	&jz		(&label("done"));

	&mov		("ebx",&DWP(512+8,"esp"));	# restore pointers
	&lea		($inp,&DWP(-128,$inp));
	&mov		("edx",&DWP(512+4,"esp"));
	&lea		($out,&DWP(-128,$out));

	&movd		("xmm2",&DWP(16*12-128,"ebp"));	# counter value
	&movdqu		("xmm3",&QWP(0,"ebx"));
	&paddd		("xmm2",&QWP(16*6,"eax"));	# +four
	&pand		("xmm3",&QWP(16*7,"eax"));
	&por		("xmm3","xmm2");		# counter value
}
{
my ($a,$b,$c,$d,$t,$t1,$rot16,$rot24)=map("xmm$_",(0..7));

sub SSSE3ROUND {	# critical path is 20 "SIMD ticks" per round
	&paddd		($a,$b);
	&pxor		($d,$a);
	&pshufb		($d,$rot16);

	&paddd		($c,$d);
	&pxor		($b,$c);
	&movdqa		($t,$b);
	&psrld		($b,20);
	&pslld		($t,12);
	&por		($b,$t);

	&paddd		($a,$b);
	&pxor		($d,$a);
	&pshufb		($d,$rot24);

	&paddd		($c,$d);
	&pxor		($b,$c);
	&movdqa		($t,$b);
	&psrld		($b,25);
	&pslld		($t,7);
	&por		($b,$t);
}

&set_label("1x");
	&movdqa		($a,&QWP(16*2,"eax"));		# sigma
	&movdqu		($b,&QWP(0,"edx"));
	&movdqu		($c,&QWP(16,"edx"));
	#&movdqu	($d,&QWP(0,"ebx"));		# already loaded
	&movdqa		($rot16,&QWP(0,"eax"));
	&movdqa		($rot24,&QWP(16,"eax"));
	&mov		(&DWP(16*3,"esp"),"ebp");

	&movdqa		(&QWP(16*0,"esp"),$a);
	&movdqa		(&QWP(16*1,"esp"),$b);
	&movdqa		(&QWP(16*2,"esp"),$c);
	&movdqa		(&QWP(16*3,"esp"),$d);
	&mov		("edx",10);
	&jmp		(&label("loop1x"));

&set_label("outer1x",16);
	&movdqa		($d,&QWP(16*5,"eax"));		# one
	&movdqa		($a,&QWP(16*0,"esp"));
	&movdqa		($b,&QWP(16*1,"esp"));
	&movdqa		($c,&QWP(16*2,"esp"));
	&paddd		($d,&QWP(16*3,"esp"));
	&mov		("edx",10);
	&movdqa		(&QWP(16*3,"esp"),$d);
	&jmp		(&label("loop1x"));

&set_label("loop1x",16);
	&SSSE3ROUND();
	&pshufd	($c,$c,0b01001110);
	&pshufd	($b,$b,0b00111001);
	&pshufd	($d,$d,0b10010011);
	&nop	();

	&SSSE3ROUND();
	&pshufd	($c,$c,0b01001110);
	&pshufd	($b,$b,0b10010011);
	&pshufd	($d,$d,0b00111001);

	&dec		("edx");
	&jnz		(&label("loop1x"));

	&paddd		($a,&QWP(16*0,"esp"));
	&paddd		($b,&QWP(16*1,"esp"));
	&paddd		($c,&QWP(16*2,"esp"));
	&paddd		($d,&QWP(16*3,"esp"));

	&cmp		($len,64);
	&jb		(&label("tail"));

	&movdqu		($t,&QWP(16*0,$inp));
	&movdqu		($t1,&QWP(16*1,$inp));
	&pxor		($a,$t);		# xor with input
	&movdqu		($t,&QWP(16*2,$inp));
	&pxor		($b,$t1);
	&movdqu		($t1,&QWP(16*3,$inp));
	&pxor		($c,$t);
	&pxor		($d,$t1);
	&lea		($inp,&DWP(16*4,$inp));	# inp+=64

	&movdqu		(&QWP(16*0,$out),$a);	# write output
	&movdqu		(&QWP(16*1,$out),$b);
	&movdqu		(&QWP(16*2,$out),$c);
	&movdqu		(&QWP(16*3,$out),$d);
	&lea		($out,&DWP(16*4,$out));	# inp+=64

	&sub		($len,64);
	&jnz		(&label("outer1x"));

	&jmp		(&label("done"));

&set_label("tail");
	&movdqa		(&QWP(16*0,"esp"),$a);
	&movdqa		(&QWP(16*1,"esp"),$b);
	&movdqa		(&QWP(16*2,"esp"),$c);
	&movdqa		(&QWP(16*3,"esp"),$d);

	&xor		("eax","eax");
	&xor		("edx","edx");
	&xor		("ebp","ebp");

&set_label("tail_loop");
	&movb		("al",&BP(0,"esp","ebp"));
	&movb		("dl",&BP(0,$inp,"ebp"));
	&lea		("ebp",&DWP(1,"ebp"));
	&xor		("al","dl");
	&movb		(&BP(-1,$out,"ebp"),"al");
	&dec		($len);
	&jnz		(&label("tail_loop"));
}
&set_label("done");
	&mov		("esp",&DWP(512,"esp"));
&function_end("ChaCha20_ssse3");

&align	(64);
&set_label("ssse3_data");
&data_byte(0x2,0x3,0x0,0x1, 0x6,0x7,0x4,0x5, 0xa,0xb,0x8,0x9, 0xe,0xf,0xc,0xd);
&data_byte(0x3,0x0,0x1,0x2, 0x7,0x4,0x5,0x6, 0xb,0x8,0x9,0xa, 0xf,0xc,0xd,0xe);
&data_word(0x61707865,0x3320646e,0x79622d32,0x6b206574);
&data_word(0,1,2,3);
&data_word(4,4,4,4);
&data_word(1,0,0,0);
&data_word(4,0,0,0);
&data_word(0,-1,-1,-1);
&align	(64);
}
&asciz	("ChaCha20 for x86, CRYPTOGAMS by <appro\@openssl.org>");

if ($ymm) {
my ($xa,$xa_,$xb,$xb_,$xc,$xc_,$xd,$xd_)=map("xmm$_",(0..7));
my ($out,$inp,$len)=("edi","esi","ecx");

sub QUARTERROUND_XOP {
my ($ai,$bi,$ci,$di,$i)=@_;
my ($an,$bn,$cn,$dn)=map(($_&~3)+(($_+1)&3),($ai,$bi,$ci,$di));	# next
my ($ap,$bp,$cp,$dp)=map(($_&~3)+(($_-1)&3),($ai,$bi,$ci,$di));	# previous

	#       a   b   c   d
	#
	#       0   4   8  12 < even round
	#       1   5   9  13
	#       2   6  10  14
	#       3   7  11  15
	#       0   5  10  15 < odd round
	#       1   6  11  12
	#       2   7   8  13
	#       3   4   9  14

	if ($i==0) {
            my $j=4;
	    ($ap,$bp,$cp,$dp)=map(($_&~3)+(($_-$j--)&3),($ap,$bp,$cp,$dp));
	} elsif ($i==3) {
            my $j=0;
	    ($an,$bn,$cn,$dn)=map(($_&~3)+(($_+$j++)&3),($an,$bn,$cn,$dn));
	} elsif ($i==4) {
            my $j=4;
	    ($ap,$bp,$cp,$dp)=map(($_&~3)+(($_+$j--)&3),($ap,$bp,$cp,$dp));
	} elsif ($i==7) {
            my $j=0;
	    ($an,$bn,$cn,$dn)=map(($_&~3)+(($_-$j++)&3),($an,$bn,$cn,$dn));
	}

	#&vpaddd	($xa,$xa,$xb);			# see elsewhere
	#&vpxor		($xd,$xd,$xa);			# see elsewhere
	 &vmovdqa	(&QWP(16*$cp-128,"ebx"),$xc_)	if ($ai>0 && $ai<3);
	&vprotd		($xd,$xd,16);
	 &vmovdqa	(&QWP(16*$bp-128,"ebx"),$xb_)	if ($i!=0);
	&vpaddd		($xc,$xc,$xd);
	 &vmovdqa	($xc_,&QWP(16*$cn-128,"ebx"))	if ($ai>0 && $ai<3);
	&vpxor		($xb,$i!=0?$xb:$xb_,$xc);
	 &vmovdqa	($xa_,&QWP(16*$an-128,"ebx"));
	&vprotd		($xb,$xb,12);
	 &vmovdqa	($xb_,&QWP(16*$bn-128,"ebx"))	if ($i<7);
	&vpaddd		($xa,$xa,$xb);
	 &vmovdqa	($xd_,&QWP(16*$dn-128,"ebx"))	if ($di!=$dn);
	&vpxor		($xd,$xd,$xa);
	 &vpaddd	($xa_,$xa_,$xb_)		if ($i<7);	# elsewhere
	&vprotd		($xd,$xd,8);
	&vmovdqa	(&QWP(16*$ai-128,"ebx"),$xa);
	&vpaddd		($xc,$xc,$xd);
	&vmovdqa	(&QWP(16*$di-128,"ebx"),$xd)	if ($di!=$dn);
	&vpxor		($xb,$xb,$xc);
	 &vpxor		($xd_,$di==$dn?$xd:$xd_,$xa_)	if ($i<7);	# elsewhere
	&vprotd		($xb,$xb,7);

	($xa,$xa_)=($xa_,$xa);
	($xb,$xb_)=($xb_,$xb);
	($xc,$xc_)=($xc_,$xc);
	($xd,$xd_)=($xd_,$xd);
}

&function_begin("ChaCha20_xop");
&set_label("xop_shortcut");
	&mov		($out,&wparam(0));
	&mov		($inp,&wparam(1));
	&mov		($len,&wparam(2));
	&mov		("edx",&wparam(3));		# key
	&mov		("ebx",&wparam(4));		# counter and nonce
	&vzeroupper	();

	&mov		("ebp","esp");
	&stack_push	(131);
	&and		("esp",-64);
	&mov		(&DWP(512,"esp"),"ebp");

	&lea		("eax",&DWP(&label("ssse3_data")."-".
				    &label("pic_point"),"eax"));
	&vmovdqu	("xmm3",&QWP(0,"ebx"));		# counter and nonce

	&cmp		($len,64*4);
	&jb		(&label("1x"));

	&mov		(&DWP(512+4,"esp"),"edx");	# offload pointers
	&mov		(&DWP(512+8,"esp"),"ebx");
	&sub		($len,64*4);			# bias len
	&lea		("ebp",&DWP(256+128,"esp"));	# size optimization

	&vmovdqu	("xmm7",&QWP(0,"edx"));		# key
	&vpshufd	("xmm0","xmm3",0x00);
	&vpshufd	("xmm1","xmm3",0x55);
	&vpshufd	("xmm2","xmm3",0xaa);
	&vpshufd	("xmm3","xmm3",0xff);
	 &vpaddd	("xmm0","xmm0",&QWP(16*3,"eax"));	# fix counters
	&vpshufd	("xmm4","xmm7",0x00);
	&vpshufd	("xmm5","xmm7",0x55);
	 &vpsubd	("xmm0","xmm0",&QWP(16*4,"eax"));
	&vpshufd	("xmm6","xmm7",0xaa);
	&vpshufd	("xmm7","xmm7",0xff);
	&vmovdqa	(&QWP(16*12-128,"ebp"),"xmm0");
	&vmovdqa	(&QWP(16*13-128,"ebp"),"xmm1");
	&vmovdqa	(&QWP(16*14-128,"ebp"),"xmm2");
	&vmovdqa	(&QWP(16*15-128,"ebp"),"xmm3");
	 &vmovdqu	("xmm3",&QWP(16,"edx"));	# key
	&vmovdqa	(&QWP(16*4-128,"ebp"),"xmm4");
	&vmovdqa	(&QWP(16*5-128,"ebp"),"xmm5");
	&vmovdqa	(&QWP(16*6-128,"ebp"),"xmm6");
	&vmovdqa	(&QWP(16*7-128,"ebp"),"xmm7");
	 &vmovdqa	("xmm7",&QWP(16*2,"eax"));	# sigma
	 &lea		("ebx",&DWP(128,"esp"));	# size optimization

	&vpshufd	("xmm0","xmm3",0x00);
	&vpshufd	("xmm1","xmm3",0x55);
	&vpshufd	("xmm2","xmm3",0xaa);
	&vpshufd	("xmm3","xmm3",0xff);
	&vpshufd	("xmm4","xmm7",0x00);
	&vpshufd	("xmm5","xmm7",0x55);
	&vpshufd	("xmm6","xmm7",0xaa);
	&vpshufd	("xmm7","xmm7",0xff);
	&vmovdqa	(&QWP(16*8-128,"ebp"),"xmm0");
	&vmovdqa	(&QWP(16*9-128,"ebp"),"xmm1");
	&vmovdqa	(&QWP(16*10-128,"ebp"),"xmm2");
	&vmovdqa	(&QWP(16*11-128,"ebp"),"xmm3");
	&vmovdqa	(&QWP(16*0-128,"ebp"),"xmm4");
	&vmovdqa	(&QWP(16*1-128,"ebp"),"xmm5");
	&vmovdqa	(&QWP(16*2-128,"ebp"),"xmm6");
	&vmovdqa	(&QWP(16*3-128,"ebp"),"xmm7");

	&lea		($inp,&DWP(128,$inp));		# size optimization
	&lea		($out,&DWP(128,$out));		# size optimization
	&jmp		(&label("outer_loop"));

&set_label("outer_loop",32);
	#&vmovdqa	("xmm0",&QWP(16*0-128,"ebp"));	# copy key material
	&vmovdqa	("xmm1",&QWP(16*1-128,"ebp"));
	&vmovdqa	("xmm2",&QWP(16*2-128,"ebp"));
	&vmovdqa	("xmm3",&QWP(16*3-128,"ebp"));
	#&vmovdqa	("xmm4",&QWP(16*4-128,"ebp"));
	&vmovdqa	("xmm5",&QWP(16*5-128,"ebp"));
	&vmovdqa	("xmm6",&QWP(16*6-128,"ebp"));
	&vmovdqa	("xmm7",&QWP(16*7-128,"ebp"));
	#&vmovdqa	(&QWP(16*0-128,"ebx"),"xmm0");
	&vmovdqa	(&QWP(16*1-128,"ebx"),"xmm1");
	&vmovdqa	(&QWP(16*2-128,"ebx"),"xmm2");
	&vmovdqa	(&QWP(16*3-128,"ebx"),"xmm3");
	#&vmovdqa	(&QWP(16*4-128,"ebx"),"xmm4");
	&vmovdqa	(&QWP(16*5-128,"ebx"),"xmm5");
	&vmovdqa	(&QWP(16*6-128,"ebx"),"xmm6");
	&vmovdqa	(&QWP(16*7-128,"ebx"),"xmm7");
	#&vmovdqa	("xmm0",&QWP(16*8-128,"ebp"));
	#&vmovdqa	("xmm1",&QWP(16*9-128,"ebp"));
	&vmovdqa	("xmm2",&QWP(16*10-128,"ebp"));
	&vmovdqa	("xmm3",&QWP(16*11-128,"ebp"));
	&vmovdqa	("xmm4",&QWP(16*12-128,"ebp"));
	&vmovdqa	("xmm5",&QWP(16*13-128,"ebp"));
	&vmovdqa	("xmm6",&QWP(16*14-128,"ebp"));
	&vmovdqa	("xmm7",&QWP(16*15-128,"ebp"));
	&vpaddd		("xmm4","xmm4",&QWP(16*4,"eax"));	# counter value
	#&vmovdqa	(&QWP(16*8-128,"ebx"),"xmm0");
	#&vmovdqa	(&QWP(16*9-128,"ebx"),"xmm1");
	&vmovdqa	(&QWP(16*10-128,"ebx"),"xmm2");
	&vmovdqa	(&QWP(16*11-128,"ebx"),"xmm3");
	&vmovdqa	(&QWP(16*12-128,"ebx"),"xmm4");
	&vmovdqa	(&QWP(16*13-128,"ebx"),"xmm5");
	&vmovdqa	(&QWP(16*14-128,"ebx"),"xmm6");
	&vmovdqa	(&QWP(16*15-128,"ebx"),"xmm7");
	&vmovdqa	(&QWP(16*12-128,"ebp"),"xmm4");	# save counter value

	&vmovdqa	($xa, &QWP(16*0-128,"ebp"));
	&vmovdqa	($xd, "xmm4");
	&vmovdqa	($xb_,&QWP(16*4-128,"ebp"));
	&vmovdqa	($xc, &QWP(16*8-128,"ebp"));
	&vmovdqa	($xc_,&QWP(16*9-128,"ebp"));

	&mov		("edx",10);			# loop counter
	&nop		();

&set_label("loop",32);
	&vpaddd		($xa,$xa,$xb_);			# elsewhere
	&vpxor		($xd,$xd,$xa);			# elsewhere
	&QUARTERROUND_XOP(0, 4, 8, 12, 0);
	&QUARTERROUND_XOP(1, 5, 9, 13, 1);
	&QUARTERROUND_XOP(2, 6,10, 14, 2);
	&QUARTERROUND_XOP(3, 7,11, 15, 3);
	&QUARTERROUND_XOP(0, 5,10, 15, 4);
	&QUARTERROUND_XOP(1, 6,11, 12, 5);
	&QUARTERROUND_XOP(2, 7, 8, 13, 6);
	&QUARTERROUND_XOP(3, 4, 9, 14, 7);
	&dec		("edx");
	&jnz		(&label("loop"));

	&vmovdqa	(&QWP(16*4-128,"ebx"),$xb_);
	&vmovdqa	(&QWP(16*8-128,"ebx"),$xc);
	&vmovdqa	(&QWP(16*9-128,"ebx"),$xc_);
	&vmovdqa	(&QWP(16*12-128,"ebx"),$xd);
	&vmovdqa	(&QWP(16*14-128,"ebx"),$xd_);

    my ($xa0,$xa1,$xa2,$xa3,$xt0,$xt1,$xt2,$xt3)=map("xmm$_",(0..7));

	#&vmovdqa	($xa0,&QWP(16*0-128,"ebx"));	# it's there
	&vmovdqa	($xa1,&QWP(16*1-128,"ebx"));
	&vmovdqa	($xa2,&QWP(16*2-128,"ebx"));
	&vmovdqa	($xa3,&QWP(16*3-128,"ebx"));

    for($i=0;$i<256;$i+=64) {
	&vpaddd		($xa0,$xa0,&QWP($i+16*0-128,"ebp"));	# accumulate key material
	&vpaddd		($xa1,$xa1,&QWP($i+16*1-128,"ebp"));
	&vpaddd		($xa2,$xa2,&QWP($i+16*2-128,"ebp"));
	&vpaddd		($xa3,$xa3,&QWP($i+16*3-128,"ebp"));

	&vpunpckldq	($xt2,$xa0,$xa1);	# "de-interlace" data
	&vpunpckldq	($xt3,$xa2,$xa3);
	&vpunpckhdq	($xa0,$xa0,$xa1);
	&vpunpckhdq	($xa2,$xa2,$xa3);
	&vpunpcklqdq	($xa1,$xt2,$xt3);	# "a0"
	&vpunpckhqdq	($xt2,$xt2,$xt3);	# "a1"
	&vpunpcklqdq	($xt3,$xa0,$xa2);	# "a2"
	&vpunpckhqdq	($xa3,$xa0,$xa2);	# "a3"

	&vpxor		($xt0,$xa1,&QWP(64*0-128,$inp));
	&vpxor		($xt1,$xt2,&QWP(64*1-128,$inp));
	&vpxor		($xt2,$xt3,&QWP(64*2-128,$inp));
	&vpxor		($xt3,$xa3,&QWP(64*3-128,$inp));
	&lea		($inp,&QWP($i<192?16:(64*4-16*3),$inp));
	&vmovdqa	($xa0,&QWP($i+16*4-128,"ebx"))	if ($i<192);
	&vmovdqa	($xa1,&QWP($i+16*5-128,"ebx"))	if ($i<192);
	&vmovdqa	($xa2,&QWP($i+16*6-128,"ebx"))	if ($i<192);
	&vmovdqa	($xa3,&QWP($i+16*7-128,"ebx"))	if ($i<192);
	&vmovdqu	(&QWP(64*0-128,$out),$xt0);	# store output
	&vmovdqu	(&QWP(64*1-128,$out),$xt1);
	&vmovdqu	(&QWP(64*2-128,$out),$xt2);
	&vmovdqu	(&QWP(64*3-128,$out),$xt3);
	&lea		($out,&QWP($i<192?16:(64*4-16*3),$out));
    }
	&sub		($len,64*4);
	&jnc		(&label("outer_loop"));

	&add		($len,64*4);
	&jz		(&label("done"));

	&mov		("ebx",&DWP(512+8,"esp"));	# restore pointers
	&lea		($inp,&DWP(-128,$inp));
	&mov		("edx",&DWP(512+4,"esp"));
	&lea		($out,&DWP(-128,$out));

	&vmovd		("xmm2",&DWP(16*12-128,"ebp"));	# counter value
	&vmovdqu	("xmm3",&QWP(0,"ebx"));
	&vpaddd		("xmm2","xmm2",&QWP(16*6,"eax"));# +four
	&vpand		("xmm3","xmm3",&QWP(16*7,"eax"));
	&vpor		("xmm3","xmm3","xmm2");		# counter value
{
my ($a,$b,$c,$d,$t,$t1,$rot16,$rot24)=map("xmm$_",(0..7));

sub XOPROUND {
	&vpaddd		($a,$a,$b);
	&vpxor		($d,$d,$a);
	&vprotd		($d,$d,16);

	&vpaddd		($c,$c,$d);
	&vpxor		($b,$b,$c);
	&vprotd		($b,$b,12);

	&vpaddd		($a,$a,$b);
	&vpxor		($d,$d,$a);
	&vprotd		($d,$d,8);

	&vpaddd		($c,$c,$d);
	&vpxor		($b,$b,$c);
	&vprotd		($b,$b,7);
}

&set_label("1x");
	&vmovdqa	($a,&QWP(16*2,"eax"));		# sigma
	&vmovdqu	($b,&QWP(0,"edx"));
	&vmovdqu	($c,&QWP(16,"edx"));
	#&vmovdqu	($d,&QWP(0,"ebx"));		# already loaded
	&vmovdqa	($rot16,&QWP(0,"eax"));
	&vmovdqa	($rot24,&QWP(16,"eax"));
	&mov		(&DWP(16*3,"esp"),"ebp");

	&vmovdqa	(&QWP(16*0,"esp"),$a);
	&vmovdqa	(&QWP(16*1,"esp"),$b);
	&vmovdqa	(&QWP(16*2,"esp"),$c);
	&vmovdqa	(&QWP(16*3,"esp"),$d);
	&mov		("edx",10);
	&jmp		(&label("loop1x"));

&set_label("outer1x",16);
	&vmovdqa	($d,&QWP(16*5,"eax"));		# one
	&vmovdqa	($a,&QWP(16*0,"esp"));
	&vmovdqa	($b,&QWP(16*1,"esp"));
	&vmovdqa	($c,&QWP(16*2,"esp"));
	&vpaddd		($d,$d,&QWP(16*3,"esp"));
	&mov		("edx",10);
	&vmovdqa	(&QWP(16*3,"esp"),$d);
	&jmp		(&label("loop1x"));

&set_label("loop1x",16);
	&XOPROUND();
	&vpshufd	($c,$c,0b01001110);
	&vpshufd	($b,$b,0b00111001);
	&vpshufd	($d,$d,0b10010011);

	&XOPROUND();
	&vpshufd	($c,$c,0b01001110);
	&vpshufd	($b,$b,0b10010011);
	&vpshufd	($d,$d,0b00111001);

	&dec		("edx");
	&jnz		(&label("loop1x"));

	&vpaddd		($a,$a,&QWP(16*0,"esp"));
	&vpaddd		($b,$b,&QWP(16*1,"esp"));
	&vpaddd		($c,$c,&QWP(16*2,"esp"));
	&vpaddd		($d,$d,&QWP(16*3,"esp"));

	&cmp		($len,64);
	&jb		(&label("tail"));

	&vpxor		($a,$a,&QWP(16*0,$inp));	# xor with input
	&vpxor		($b,$b,&QWP(16*1,$inp));
	&vpxor		($c,$c,&QWP(16*2,$inp));
	&vpxor		($d,$d,&QWP(16*3,$inp));
	&lea		($inp,&DWP(16*4,$inp));		# inp+=64

	&vmovdqu	(&QWP(16*0,$out),$a);		# write output
	&vmovdqu	(&QWP(16*1,$out),$b);
	&vmovdqu	(&QWP(16*2,$out),$c);
	&vmovdqu	(&QWP(16*3,$out),$d);
	&lea		($out,&DWP(16*4,$out));		# inp+=64

	&sub		($len,64);
	&jnz		(&label("outer1x"));

	&jmp		(&label("done"));

&set_label("tail");
	&vmovdqa	(&QWP(16*0,"esp"),$a);
	&vmovdqa	(&QWP(16*1,"esp"),$b);
	&vmovdqa	(&QWP(16*2,"esp"),$c);
	&vmovdqa	(&QWP(16*3,"esp"),$d);

	&xor		("eax","eax");
	&xor		("edx","edx");
	&xor		("ebp","ebp");

&set_label("tail_loop");
	&movb		("al",&BP(0,"esp","ebp"));
	&movb		("dl",&BP(0,$inp,"ebp"));
	&lea		("ebp",&DWP(1,"ebp"));
	&xor		("al","dl");
	&movb		(&BP(-1,$out,"ebp"),"al");
	&dec		($len);
	&jnz		(&label("tail_loop"));
}
&set_label("done");
	&vzeroupper	();
	&mov		("esp",&DWP(512,"esp"));
&function_end("ChaCha20_xop");
}

&asm_finish();

close STDOUT;
