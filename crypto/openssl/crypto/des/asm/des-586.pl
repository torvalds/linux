#! /usr/bin/env perl
# Copyright 1995-2016 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

# The inner loop instruction sequence and the IP/FP modifications are from
# Svend Olaf Mikkelsen.

$0 =~ m/(.*[\/\\])[^\/\\]+$/; $dir=$1;
push(@INC,"${dir}","${dir}../../perlasm");
require "x86asm.pl";
require "cbc.pl";
require "desboth.pl";

# base code is in Microsoft
# op dest, source
# format.
#

$output=pop;
open STDOUT,">$output";

&asm_init($ARGV[0]);

$L="edi";
$R="esi";
$trans="ebp";
$small_footprint=1 if (grep(/\-DOPENSSL_SMALL_FOOTPRINT/,@ARGV));
# one can discuss setting this variable to 1 unconditionally, as
# the folded loop is only 3% slower than unrolled, but >7 times smaller

&public_label("DES_SPtrans");
&static_label("des_sptrans");

&DES_encrypt_internal();
&DES_decrypt_internal();
&DES_encrypt("DES_encrypt1",1);
&DES_encrypt("DES_encrypt2",0);
&DES_encrypt3("DES_encrypt3",1);
&DES_encrypt3("DES_decrypt3",0);
&cbc("DES_ncbc_encrypt","DES_encrypt1","DES_encrypt1",0,4,5,3,5,-1);
&cbc("DES_ede3_cbc_encrypt","DES_encrypt3","DES_decrypt3",0,6,7,3,4,5);
&DES_SPtrans();

&asm_finish();

close STDOUT;

sub DES_encrypt_internal()
	{
	&function_begin_B("_x86_DES_encrypt");

	if ($small_footprint)
	    {
	    &lea("edx",&DWP(128,"ecx"));
	    &push("edx");
	    &push("ecx");
	    &set_label("eloop");
		&D_ENCRYPT(0,$L,$R,0,$trans,"eax","ebx","ecx","edx",&swtmp(0));
		&comment("");
		&D_ENCRYPT(1,$R,$L,2,$trans,"eax","ebx","ecx","edx",&swtmp(0));
		&comment("");
		&add("ecx",16);
		&cmp("ecx",&swtmp(1));
		&mov(&swtmp(0),"ecx");
		&jb(&label("eloop"));
	    &add("esp",8);
	    }
	else
	    {
	    &push("ecx");
	    for ($i=0; $i<16; $i+=2)
		{
		&comment("Round $i");
		&D_ENCRYPT($i,$L,$R,$i*2,$trans,"eax","ebx","ecx","edx",&swtmp(0));
		&comment("Round ".sprintf("%d",$i+1));
		&D_ENCRYPT($i+1,$R,$L,($i+1)*2,$trans,"eax","ebx","ecx","edx",&swtmp(0));
		}
	    &add("esp",4);
	}
	&ret();

	&function_end_B("_x86_DES_encrypt");
	}

sub DES_decrypt_internal()
	{
	&function_begin_B("_x86_DES_decrypt");

	if ($small_footprint)
	    {
	    &push("ecx");
	    &lea("ecx",&DWP(128,"ecx"));
	    &push("ecx");
	    &set_label("dloop");
		&D_ENCRYPT(0,$L,$R,-2,$trans,"eax","ebx","ecx","edx",&swtmp(0));
		&comment("");
		&D_ENCRYPT(1,$R,$L,-4,$trans,"eax","ebx","ecx","edx",&swtmp(0));
		&comment("");
		&sub("ecx",16);
		&cmp("ecx",&swtmp(1));
		&mov(&swtmp(0),"ecx");
		&ja(&label("dloop"));
	    &add("esp",8);
	    }
	else
	    {
	    &push("ecx");
	    for ($i=15; $i>0; $i-=2)
		{
		&comment("Round $i");
		&D_ENCRYPT(15-$i,$L,$R,$i*2,$trans,"eax","ebx","ecx","edx",&swtmp(0));
		&comment("Round ".sprintf("%d",$i-1));
		&D_ENCRYPT(15-$i+1,$R,$L,($i-1)*2,$trans,"eax","ebx","ecx","edx",&swtmp(0));
		}
	    &add("esp",4);
	    }
	&ret();

	&function_end_B("_x86_DES_decrypt");
	}

sub DES_encrypt
	{
	local($name,$do_ip)=@_;

	&function_begin_B($name);

	&push("esi");
	&push("edi");

	&comment("");
	&comment("Load the 2 words");

	if ($do_ip)
		{
		&mov($R,&wparam(0));
		 &xor(	"ecx",		"ecx"		);

		&push("ebx");
		&push("ebp");

		&mov("eax",&DWP(0,$R,"",0));
		 &mov("ebx",&wparam(2));	# get encrypt flag
		&mov($L,&DWP(4,$R,"",0));
		&comment("");
		&comment("IP");
		&IP_new("eax",$L,$R,3);
		}
	else
		{
		&mov("eax",&wparam(0));
		 &xor(	"ecx",		"ecx"		);

		&push("ebx");
		&push("ebp");

		&mov($R,&DWP(0,"eax","",0));
		 &mov("ebx",&wparam(2));	# get encrypt flag
		&rotl($R,3);
		&mov($L,&DWP(4,"eax","",0));
		&rotl($L,3);
		}

	# PIC-ification:-)
	&call	(&label("pic_point"));
	&set_label("pic_point");
	&blindpop($trans);
	&lea	($trans,&DWP(&label("des_sptrans")."-".&label("pic_point"),$trans));

	&mov(	"ecx",	&wparam(1)	);

	&cmp("ebx","0");
	&je(&label("decrypt"));
	&call("_x86_DES_encrypt");
	&jmp(&label("done"));
	&set_label("decrypt");
	&call("_x86_DES_decrypt");
	&set_label("done");

	if ($do_ip)
		{
		&comment("");
		&comment("FP");
		&mov("edx",&wparam(0));
		&FP_new($L,$R,"eax",3);

		&mov(&DWP(0,"edx","",0),"eax");
		&mov(&DWP(4,"edx","",0),$R);
		}
	else
		{
		&comment("");
		&comment("Fixup");
		&rotr($L,3);		# r
		 &mov("eax",&wparam(0));
		&rotr($R,3);		# l
		 &mov(&DWP(0,"eax","",0),$L);
		 &mov(&DWP(4,"eax","",0),$R);
		}

	&pop("ebp");
	&pop("ebx");
	&pop("edi");
	&pop("esi");
	&ret();

	&function_end_B($name);
	}

sub D_ENCRYPT
	{
	local($r,$L,$R,$S,$trans,$u,$tmp1,$tmp2,$t,$wp1)=@_;

	 &mov(	$u,		&DWP(&n2a($S*4),$tmp2,"",0));
	&xor(	$tmp1,		$tmp1);
	 &mov(	$t,		&DWP(&n2a(($S+1)*4),$tmp2,"",0));
	&xor(	$u,		$R);
	&xor(	$tmp2,		$tmp2);
	 &xor(	$t,		$R);
	&and(	$u,		"0xfcfcfcfc"	);
	 &and(	$t,		"0xcfcfcfcf"	);
	&movb(	&LB($tmp1),	&LB($u)	);
	 &movb(	&LB($tmp2),	&HB($u)	);
	&rotr(	$t,		4		);
	&xor(	$L,		&DWP("     ",$trans,$tmp1,0));
	 &movb(	&LB($tmp1),	&LB($t)	);
	 &xor(	$L,		&DWP("0x200",$trans,$tmp2,0));
	 &movb(	&LB($tmp2),	&HB($t)	);
	&shr(	$u,		16);
	 &xor(	$L,		&DWP("0x100",$trans,$tmp1,0));
	 &movb(	&LB($tmp1),	&HB($u)	);
	&shr(	$t,		16);
	 &xor(	$L,		&DWP("0x300",$trans,$tmp2,0));
	&movb(	&LB($tmp2),	&HB($t)	);
	 &and(	$u,		"0xff"	);
	&and(	$t,		"0xff"	);
	 &xor(	$L,		&DWP("0x600",$trans,$tmp1,0));
	 &xor(	$L,		&DWP("0x700",$trans,$tmp2,0));
	&mov(	$tmp2,		$wp1	);
	 &xor(	$L,		&DWP("0x400",$trans,$u,0));
	 &xor(	$L,		&DWP("0x500",$trans,$t,0));
	}

sub n2a
	{
	sprintf("%d",$_[0]);
	}

# now has a side affect of rotating $a by $shift
sub R_PERM_OP
	{
	local($a,$b,$tt,$shift,$mask,$last)=@_;

	&rotl(	$a,		$shift		) if ($shift != 0);
	&mov(	$tt,		$a		);
	&xor(	$a,		$b		);
	&and(	$a,		$mask		);
	# This can never succeed, and besides it is difficult to see what the
	# idea was - Ben 13 Feb 99
	if (!$last eq $b)
		{
		&xor(	$b,		$a		);
		&xor(	$tt,		$a		);
		}
	else
		{
		&xor(	$tt,		$a		);
		&xor(	$b,		$a		);
		}
	&comment("");
	}

sub IP_new
	{
	local($l,$r,$tt,$lr)=@_;

	&R_PERM_OP($l,$r,$tt, 4,"0xf0f0f0f0",$l);
	&R_PERM_OP($r,$tt,$l,20,"0xfff0000f",$l);
	&R_PERM_OP($l,$tt,$r,14,"0x33333333",$r);
	&R_PERM_OP($tt,$r,$l,22,"0x03fc03fc",$r);
	&R_PERM_OP($l,$r,$tt, 9,"0xaaaaaaaa",$r);

	if ($lr != 3)
		{
		if (($lr-3) < 0)
			{ &rotr($tt,	3-$lr); }
		else	{ &rotl($tt,	$lr-3); }
		}
	if ($lr != 2)
		{
		if (($lr-2) < 0)
			{ &rotr($r,	2-$lr); }
		else	{ &rotl($r,	$lr-2); }
		}
	}

sub FP_new
	{
	local($l,$r,$tt,$lr)=@_;

	if ($lr != 2)
		{
		if (($lr-2) < 0)
			{ &rotl($r,	2-$lr); }
		else	{ &rotr($r,	$lr-2); }
		}
	if ($lr != 3)
		{
		if (($lr-3) < 0)
			{ &rotl($l,	3-$lr); }
		else	{ &rotr($l,	$lr-3); }
		}

	&R_PERM_OP($l,$r,$tt, 0,"0xaaaaaaaa",$r);
	&R_PERM_OP($tt,$r,$l,23,"0x03fc03fc",$r);
	&R_PERM_OP($l,$r,$tt,10,"0x33333333",$l);
	&R_PERM_OP($r,$tt,$l,18,"0xfff0000f",$l);
	&R_PERM_OP($l,$tt,$r,12,"0xf0f0f0f0",$r);
	&rotr($tt	, 4);
	}

sub DES_SPtrans
	{
	&set_label("DES_SPtrans",64);
	&set_label("des_sptrans");
	&data_word(0x02080800, 0x00080000, 0x02000002, 0x02080802);
	&data_word(0x02000000, 0x00080802, 0x00080002, 0x02000002);
	&data_word(0x00080802, 0x02080800, 0x02080000, 0x00000802);
	&data_word(0x02000802, 0x02000000, 0x00000000, 0x00080002);
	&data_word(0x00080000, 0x00000002, 0x02000800, 0x00080800);
	&data_word(0x02080802, 0x02080000, 0x00000802, 0x02000800);
	&data_word(0x00000002, 0x00000800, 0x00080800, 0x02080002);
	&data_word(0x00000800, 0x02000802, 0x02080002, 0x00000000);
	&data_word(0x00000000, 0x02080802, 0x02000800, 0x00080002);
	&data_word(0x02080800, 0x00080000, 0x00000802, 0x02000800);
	&data_word(0x02080002, 0x00000800, 0x00080800, 0x02000002);
	&data_word(0x00080802, 0x00000002, 0x02000002, 0x02080000);
	&data_word(0x02080802, 0x00080800, 0x02080000, 0x02000802);
	&data_word(0x02000000, 0x00000802, 0x00080002, 0x00000000);
	&data_word(0x00080000, 0x02000000, 0x02000802, 0x02080800);
	&data_word(0x00000002, 0x02080002, 0x00000800, 0x00080802);
	# nibble 1
	&data_word(0x40108010, 0x00000000, 0x00108000, 0x40100000);
	&data_word(0x40000010, 0x00008010, 0x40008000, 0x00108000);
	&data_word(0x00008000, 0x40100010, 0x00000010, 0x40008000);
	&data_word(0x00100010, 0x40108000, 0x40100000, 0x00000010);
	&data_word(0x00100000, 0x40008010, 0x40100010, 0x00008000);
	&data_word(0x00108010, 0x40000000, 0x00000000, 0x00100010);
	&data_word(0x40008010, 0x00108010, 0x40108000, 0x40000010);
	&data_word(0x40000000, 0x00100000, 0x00008010, 0x40108010);
	&data_word(0x00100010, 0x40108000, 0x40008000, 0x00108010);
	&data_word(0x40108010, 0x00100010, 0x40000010, 0x00000000);
	&data_word(0x40000000, 0x00008010, 0x00100000, 0x40100010);
	&data_word(0x00008000, 0x40000000, 0x00108010, 0x40008010);
	&data_word(0x40108000, 0x00008000, 0x00000000, 0x40000010);
	&data_word(0x00000010, 0x40108010, 0x00108000, 0x40100000);
	&data_word(0x40100010, 0x00100000, 0x00008010, 0x40008000);
	&data_word(0x40008010, 0x00000010, 0x40100000, 0x00108000);
	# nibble 2
	&data_word(0x04000001, 0x04040100, 0x00000100, 0x04000101);
	&data_word(0x00040001, 0x04000000, 0x04000101, 0x00040100);
	&data_word(0x04000100, 0x00040000, 0x04040000, 0x00000001);
	&data_word(0x04040101, 0x00000101, 0x00000001, 0x04040001);
	&data_word(0x00000000, 0x00040001, 0x04040100, 0x00000100);
	&data_word(0x00000101, 0x04040101, 0x00040000, 0x04000001);
	&data_word(0x04040001, 0x04000100, 0x00040101, 0x04040000);
	&data_word(0x00040100, 0x00000000, 0x04000000, 0x00040101);
	&data_word(0x04040100, 0x00000100, 0x00000001, 0x00040000);
	&data_word(0x00000101, 0x00040001, 0x04040000, 0x04000101);
	&data_word(0x00000000, 0x04040100, 0x00040100, 0x04040001);
	&data_word(0x00040001, 0x04000000, 0x04040101, 0x00000001);
	&data_word(0x00040101, 0x04000001, 0x04000000, 0x04040101);
	&data_word(0x00040000, 0x04000100, 0x04000101, 0x00040100);
	&data_word(0x04000100, 0x00000000, 0x04040001, 0x00000101);
	&data_word(0x04000001, 0x00040101, 0x00000100, 0x04040000);
	# nibble 3
	&data_word(0x00401008, 0x10001000, 0x00000008, 0x10401008);
	&data_word(0x00000000, 0x10400000, 0x10001008, 0x00400008);
	&data_word(0x10401000, 0x10000008, 0x10000000, 0x00001008);
	&data_word(0x10000008, 0x00401008, 0x00400000, 0x10000000);
	&data_word(0x10400008, 0x00401000, 0x00001000, 0x00000008);
	&data_word(0x00401000, 0x10001008, 0x10400000, 0x00001000);
	&data_word(0x00001008, 0x00000000, 0x00400008, 0x10401000);
	&data_word(0x10001000, 0x10400008, 0x10401008, 0x00400000);
	&data_word(0x10400008, 0x00001008, 0x00400000, 0x10000008);
	&data_word(0x00401000, 0x10001000, 0x00000008, 0x10400000);
	&data_word(0x10001008, 0x00000000, 0x00001000, 0x00400008);
	&data_word(0x00000000, 0x10400008, 0x10401000, 0x00001000);
	&data_word(0x10000000, 0x10401008, 0x00401008, 0x00400000);
	&data_word(0x10401008, 0x00000008, 0x10001000, 0x00401008);
	&data_word(0x00400008, 0x00401000, 0x10400000, 0x10001008);
	&data_word(0x00001008, 0x10000000, 0x10000008, 0x10401000);
	# nibble 4
	&data_word(0x08000000, 0x00010000, 0x00000400, 0x08010420);
	&data_word(0x08010020, 0x08000400, 0x00010420, 0x08010000);
	&data_word(0x00010000, 0x00000020, 0x08000020, 0x00010400);
	&data_word(0x08000420, 0x08010020, 0x08010400, 0x00000000);
	&data_word(0x00010400, 0x08000000, 0x00010020, 0x00000420);
	&data_word(0x08000400, 0x00010420, 0x00000000, 0x08000020);
	&data_word(0x00000020, 0x08000420, 0x08010420, 0x00010020);
	&data_word(0x08010000, 0x00000400, 0x00000420, 0x08010400);
	&data_word(0x08010400, 0x08000420, 0x00010020, 0x08010000);
	&data_word(0x00010000, 0x00000020, 0x08000020, 0x08000400);
	&data_word(0x08000000, 0x00010400, 0x08010420, 0x00000000);
	&data_word(0x00010420, 0x08000000, 0x00000400, 0x00010020);
	&data_word(0x08000420, 0x00000400, 0x00000000, 0x08010420);
	&data_word(0x08010020, 0x08010400, 0x00000420, 0x00010000);
	&data_word(0x00010400, 0x08010020, 0x08000400, 0x00000420);
	&data_word(0x00000020, 0x00010420, 0x08010000, 0x08000020);
	# nibble 5
	&data_word(0x80000040, 0x00200040, 0x00000000, 0x80202000);
	&data_word(0x00200040, 0x00002000, 0x80002040, 0x00200000);
	&data_word(0x00002040, 0x80202040, 0x00202000, 0x80000000);
	&data_word(0x80002000, 0x80000040, 0x80200000, 0x00202040);
	&data_word(0x00200000, 0x80002040, 0x80200040, 0x00000000);
	&data_word(0x00002000, 0x00000040, 0x80202000, 0x80200040);
	&data_word(0x80202040, 0x80200000, 0x80000000, 0x00002040);
	&data_word(0x00000040, 0x00202000, 0x00202040, 0x80002000);
	&data_word(0x00002040, 0x80000000, 0x80002000, 0x00202040);
	&data_word(0x80202000, 0x00200040, 0x00000000, 0x80002000);
	&data_word(0x80000000, 0x00002000, 0x80200040, 0x00200000);
	&data_word(0x00200040, 0x80202040, 0x00202000, 0x00000040);
	&data_word(0x80202040, 0x00202000, 0x00200000, 0x80002040);
	&data_word(0x80000040, 0x80200000, 0x00202040, 0x00000000);
	&data_word(0x00002000, 0x80000040, 0x80002040, 0x80202000);
	&data_word(0x80200000, 0x00002040, 0x00000040, 0x80200040);
	# nibble 6
	&data_word(0x00004000, 0x00000200, 0x01000200, 0x01000004);
	&data_word(0x01004204, 0x00004004, 0x00004200, 0x00000000);
	&data_word(0x01000000, 0x01000204, 0x00000204, 0x01004000);
	&data_word(0x00000004, 0x01004200, 0x01004000, 0x00000204);
	&data_word(0x01000204, 0x00004000, 0x00004004, 0x01004204);
	&data_word(0x00000000, 0x01000200, 0x01000004, 0x00004200);
	&data_word(0x01004004, 0x00004204, 0x01004200, 0x00000004);
	&data_word(0x00004204, 0x01004004, 0x00000200, 0x01000000);
	&data_word(0x00004204, 0x01004000, 0x01004004, 0x00000204);
	&data_word(0x00004000, 0x00000200, 0x01000000, 0x01004004);
	&data_word(0x01000204, 0x00004204, 0x00004200, 0x00000000);
	&data_word(0x00000200, 0x01000004, 0x00000004, 0x01000200);
	&data_word(0x00000000, 0x01000204, 0x01000200, 0x00004200);
	&data_word(0x00000204, 0x00004000, 0x01004204, 0x01000000);
	&data_word(0x01004200, 0x00000004, 0x00004004, 0x01004204);
	&data_word(0x01000004, 0x01004200, 0x01004000, 0x00004004);
	# nibble 7
	&data_word(0x20800080, 0x20820000, 0x00020080, 0x00000000);
	&data_word(0x20020000, 0x00800080, 0x20800000, 0x20820080);
	&data_word(0x00000080, 0x20000000, 0x00820000, 0x00020080);
	&data_word(0x00820080, 0x20020080, 0x20000080, 0x20800000);
	&data_word(0x00020000, 0x00820080, 0x00800080, 0x20020000);
	&data_word(0x20820080, 0x20000080, 0x00000000, 0x00820000);
	&data_word(0x20000000, 0x00800000, 0x20020080, 0x20800080);
	&data_word(0x00800000, 0x00020000, 0x20820000, 0x00000080);
	&data_word(0x00800000, 0x00020000, 0x20000080, 0x20820080);
	&data_word(0x00020080, 0x20000000, 0x00000000, 0x00820000);
	&data_word(0x20800080, 0x20020080, 0x20020000, 0x00800080);
	&data_word(0x20820000, 0x00000080, 0x00800080, 0x20020000);
	&data_word(0x20820080, 0x00800000, 0x20800000, 0x20000080);
	&data_word(0x00820000, 0x00020080, 0x20020080, 0x20800000);
	&data_word(0x00000080, 0x20820000, 0x00820080, 0x00000000);
	&data_word(0x20000000, 0x20800080, 0x00020000, 0x00820080);
	}
