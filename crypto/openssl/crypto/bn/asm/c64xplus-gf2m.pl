#! /usr/bin/env perl
# Copyright 2012-2016 The OpenSSL Project Authors. All Rights Reserved.
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
# February 2012
#
# The module implements bn_GF2m_mul_2x2 polynomial multiplication
# used in bn_gf2m.c. It's kind of low-hanging mechanical port from
# C for the time being... The subroutine runs in 37 cycles, which is
# 4.5x faster than compiler-generated code. Though comparison is
# totally unfair, because this module utilizes Galois Field Multiply
# instruction.

while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {}
open STDOUT,">$output";

($rp,$a1,$a0,$b1,$b0)=("A4","B4","A6","B6","A8");   # argument vector

($Alo,$Alox0,$Alox1,$Alox2,$Alox3)=map("A$_",(16..20));
($Ahi,$Ahix0,$Ahix1,$Ahix2,$Ahix3)=map("B$_",(16..20));
($B_0,$B_1,$B_2,$B_3)=("B5","A5","A7","B7");
($A,$B)=($Alo,$B_1);
$xFF="B1";

sub mul_1x1_upper {
my ($A,$B)=@_;
$code.=<<___;
	EXTU	$B,8,24,$B_2		; smash $B to 4 bytes
||	AND	$B,$xFF,$B_0
||	SHRU	$B,24,$B_3
	SHRU	$A,16,   $Ahi		; smash $A to two halfwords
||	EXTU	$A,16,16,$Alo

	XORMPY	$Alo,$B_2,$Alox2	; 16x8 bits multiplication
||	XORMPY	$Ahi,$B_2,$Ahix2
||	EXTU	$B,16,24,$B_1
	XORMPY	$Alo,$B_0,$Alox0
||	XORMPY	$Ahi,$B_0,$Ahix0
	XORMPY	$Alo,$B_3,$Alox3
||	XORMPY	$Ahi,$B_3,$Ahix3
	XORMPY	$Alo,$B_1,$Alox1
||	XORMPY	$Ahi,$B_1,$Ahix1
___
}
sub mul_1x1_merged {
my ($OUTlo,$OUThi,$A,$B)=@_;
$code.=<<___;
	 EXTU	$B,8,24,$B_2		; smash $B to 4 bytes
||	 AND	$B,$xFF,$B_0
||	 SHRU	$B,24,$B_3
	 SHRU	$A,16,   $Ahi		; smash $A to two halfwords
||	 EXTU	$A,16,16,$Alo

	XOR	$Ahix0,$Alox2,$Ahix0
||	MV	$Ahix2,$OUThi
||	 XORMPY	$Alo,$B_2,$Alox2
	 XORMPY	$Ahi,$B_2,$Ahix2
||	 EXTU	$B,16,24,$B_1
||	 XORMPY	$Alo,$B_0,A1		; $Alox0
	XOR	$Ahix1,$Alox3,$Ahix1
||	SHL	$Ahix0,16,$OUTlo
||	SHRU	$Ahix0,16,$Ahix0
	XOR	$Alox0,$OUTlo,$OUTlo
||	XOR	$Ahix0,$OUThi,$OUThi
||	 XORMPY	$Ahi,$B_0,$Ahix0
||	 XORMPY	$Alo,$B_3,$Alox3
||	SHL	$Alox1,8,$Alox1
||	SHL	$Ahix3,8,$Ahix3
	XOR	$Alox1,$OUTlo,$OUTlo
||	XOR	$Ahix3,$OUThi,$OUThi
||	 XORMPY	$Ahi,$B_3,$Ahix3
||	SHL	$Ahix1,24,$Alox1
||	SHRU	$Ahix1,8, $Ahix1
	XOR	$Alox1,$OUTlo,$OUTlo
||	XOR	$Ahix1,$OUThi,$OUThi
||	 XORMPY	$Alo,$B_1,$Alox1
||	 XORMPY	$Ahi,$B_1,$Ahix1
||	 MV	A1,$Alox0
___
}
sub mul_1x1_lower {
my ($OUTlo,$OUThi)=@_;
$code.=<<___;
	;NOP
	XOR	$Ahix0,$Alox2,$Ahix0
||	MV	$Ahix2,$OUThi
	NOP
	XOR	$Ahix1,$Alox3,$Ahix1
||	SHL	$Ahix0,16,$OUTlo
||	SHRU	$Ahix0,16,$Ahix0
	XOR	$Alox0,$OUTlo,$OUTlo
||	XOR	$Ahix0,$OUThi,$OUThi
||	SHL	$Alox1,8,$Alox1
||	SHL	$Ahix3,8,$Ahix3
	XOR	$Alox1,$OUTlo,$OUTlo
||	XOR	$Ahix3,$OUThi,$OUThi
||	SHL	$Ahix1,24,$Alox1
||	SHRU	$Ahix1,8, $Ahix1
	XOR	$Alox1,$OUTlo,$OUTlo
||	XOR	$Ahix1,$OUThi,$OUThi
___
}
$code.=<<___;
	.text

	.if	.ASSEMBLER_VERSION<7000000
	.asg	0,__TI_EABI__
	.endif
	.if	__TI_EABI__
	.asg	bn_GF2m_mul_2x2,_bn_GF2m_mul_2x2
	.endif

	.global	_bn_GF2m_mul_2x2
_bn_GF2m_mul_2x2:
	.asmfunc
	MVK	0xFF,$xFF
___
	&mul_1x1_upper($a0,$b0);		# a0·b0
$code.=<<___;
||	MV	$b1,$B
	MV	$a1,$A
___
	&mul_1x1_merged("A28","B28",$A,$B);	# a0·b0/a1·b1
$code.=<<___;
||	XOR	$b0,$b1,$B
	XOR	$a0,$a1,$A
___
	&mul_1x1_merged("A31","B31",$A,$B);	# a1·b1/(a0+a1)·(b0+b1)
$code.=<<___;
	XOR	A28,A31,A29
||	XOR	B28,B31,B29			; a0·b0+a1·b1
___
	&mul_1x1_lower("A30","B30");		# (a0+a1)·(b0+b1)
$code.=<<___;
||	BNOP	B3
	XOR	A29,A30,A30
||	XOR	B29,B30,B30			; (a0+a1)·(b0+b1)-a0·b0-a1·b1
	XOR	B28,A30,A30
||	STW	A28,*${rp}[0]
	XOR	B30,A31,A31
||	STW	A30,*${rp}[1]
	STW	A31,*${rp}[2]
	STW	B31,*${rp}[3]
	.endasmfunc
___

print $code;
close STDOUT;
