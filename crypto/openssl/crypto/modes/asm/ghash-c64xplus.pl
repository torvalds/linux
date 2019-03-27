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
# December 2011
#
# The module implements GCM GHASH function and underlying single
# multiplication operation in GF(2^128). Even though subroutines
# have _4bit suffix, they are not using any tables, but rely on
# hardware Galois Field Multiply support. Streamed GHASH processes
# byte in ~7 cycles, which is >6x faster than "4-bit" table-driven
# code compiled with TI's cl6x 6.0 with -mv6400+ -o2 flags. We are
# comparing apples vs. oranges, but compiler surely could have done
# better, because theoretical [though not necessarily achievable]
# estimate for "4-bit" table-driven implementation is ~12 cycles.

while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {}
open STDOUT,">$output";

($Xip,$Htable,$inp,$len)=("A4","B4","A6","B6");	# arguments

($Z0,$Z1,$Z2,$Z3,	$H0, $H1, $H2, $H3,
			$H0x,$H1x,$H2x,$H3x)=map("A$_",(16..27));
($H01u,$H01y,$H2u,$H3u,	$H0y,$H1y,$H2y,$H3y,
			$H0z,$H1z,$H2z,$H3z)=map("B$_",(16..27));
($FF000000,$E10000)=("B30","B31");
($xip,$x0,$x1,$xib)=map("B$_",(6..9));	# $xip zaps $len
 $xia="A9";
($rem,$res)=("B4","B5");		# $rem zaps $Htable

$code.=<<___;
	.text

	.if	.ASSEMBLER_VERSION<7000000
	.asg	0,__TI_EABI__
	.endif
	.if	__TI_EABI__
	.asg	gcm_gmult_1bit,_gcm_gmult_1bit
	.asg	gcm_gmult_4bit,_gcm_gmult_4bit
	.asg	gcm_ghash_4bit,_gcm_ghash_4bit
	.endif

	.asg	B3,RA

	.if	0
	.global	_gcm_gmult_1bit
_gcm_gmult_1bit:
	ADDAD	$Htable,2,$Htable
	.endif
	.global	_gcm_gmult_4bit
_gcm_gmult_4bit:
	.asmfunc
	LDDW	*${Htable}[-1],$H1:$H0	; H.lo
	LDDW	*${Htable}[-2],$H3:$H2	; H.hi
||	MV	$Xip,${xip}		; reassign Xi
||	MVK	15,B1			; SPLOOPD constant

	MVK	0xE1,$E10000
||	LDBU	*++${xip}[15],$x1	; Xi[15]
	MVK	0xFF,$FF000000
||	LDBU	*--${xip},$x0		; Xi[14]
	SHL	$E10000,16,$E10000	; [pre-shifted] reduction polynomial
	SHL	$FF000000,24,$FF000000	; upper byte mask
||	BNOP	ghash_loop?
||	MVK	1,B0			; take a single spin

	PACKH2	$H0,$H1,$xia		; pack H0' and H1's upper bytes
	AND	$H2,$FF000000,$H2u	; H2's upper byte
	AND	$H3,$FF000000,$H3u	; H3's upper byte
||	SHRU	$H2u,8,$H2u
	SHRU	$H3u,8,$H3u
||	ZERO	$Z1:$Z0
	SHRU2	$xia,8,$H01u
||	ZERO	$Z3:$Z2
	.endasmfunc

	.global	_gcm_ghash_4bit
_gcm_ghash_4bit:
	.asmfunc
	LDDW	*${Htable}[-1],$H1:$H0	; H.lo
||	SHRU	$len,4,B0		; reassign len
	LDDW	*${Htable}[-2],$H3:$H2	; H.hi
||	MV	$Xip,${xip}		; reassign Xi
||	MVK	15,B1			; SPLOOPD constant

	MVK	0xE1,$E10000
|| [B0]	LDNDW	*${inp}[1],$H1x:$H0x
	MVK	0xFF,$FF000000
|| [B0]	LDNDW	*${inp}++[2],$H3x:$H2x
	SHL	$E10000,16,$E10000	; [pre-shifted] reduction polynomial
||	LDDW	*${xip}[1],$Z1:$Z0
	SHL	$FF000000,24,$FF000000	; upper byte mask
||	LDDW	*${xip}[0],$Z3:$Z2

	PACKH2	$H0,$H1,$xia		; pack H0' and H1's upper bytes
	AND	$H2,$FF000000,$H2u	; H2's upper byte
	AND	$H3,$FF000000,$H3u	; H3's upper byte
||	SHRU	$H2u,8,$H2u
	SHRU	$H3u,8,$H3u
	SHRU2	$xia,8,$H01u

|| [B0]	XOR	$H0x,$Z0,$Z0		; Xi^=inp
|| [B0]	XOR	$H1x,$Z1,$Z1
	.if	.LITTLE_ENDIAN
   [B0]	XOR	$H2x,$Z2,$Z2
|| [B0]	XOR	$H3x,$Z3,$Z3
|| [B0]	SHRU	$Z1,24,$xia		; Xi[15], avoid cross-path stall
	STDW	$Z1:$Z0,*${xip}[1]
|| [B0]	SHRU	$Z1,16,$x0		; Xi[14]
|| [B0]	ZERO	$Z1:$Z0
	.else
   [B0]	XOR	$H2x,$Z2,$Z2
|| [B0]	XOR	$H3x,$Z3,$Z3
|| [B0]	MV	$Z0,$xia		; Xi[15], avoid cross-path stall
	STDW	$Z1:$Z0,*${xip}[1]
|| [B0] SHRU	$Z0,8,$x0		; Xi[14]
|| [B0]	ZERO	$Z1:$Z0
	.endif
	STDW	$Z3:$Z2,*${xip}[0]
|| [B0]	ZERO	$Z3:$Z2
|| [B0]	MV	$xia,$x1
   [B0]	ADDK	14,${xip}

ghash_loop?:
	SPLOOPD	6			; 6*16+7
||	MVC	B1,ILC
|| [B0]	SUB	B0,1,B0
||	ZERO	A0
||	ADD	$x1,$x1,$xib		; SHL	$x1,1,$xib
||	SHL	$x1,1,$xia
___

########____________________________
#  0    D2.     M1          M2      |
#  1            M1                  |
#  2            M1          M2      |
#  3        D1. M1          M2      |
#  4        S1. L1                  |
#  5    S2  S1x L1          D2  L2  |____________________________
#  6/0          L1  S1      L2  S2x |D2.     M1          M2      |
#  7/1          L1  S1  D1x S2  M2  |        M1                  |
#  8/2              S1  L1x S2      |        M1          M2      |
#  9/3              S1  L1x         |    D1. M1          M2      |
# 10/4                  D1x         |    S1. L1                  |
# 11/5                              |S2  S1x L1          D2  L2  |____________
# 12/6/0                D1x       __|        L1  S1      L2  S2x |D2.     ....
#    7/1                                     L1  S1  D1x S2  M2  |        ....
#    8/2                                         S1  L1x S2      |        ....
#####...                                         ................|............
$code.=<<___;
	XORMPY	$H0,$xia,$H0x		; 0	; H·(Xi[i]<<1)
||	XORMPY	$H01u,$xib,$H01y
|| [A0]	LDBU	*--${xip},$x0
	XORMPY	$H1,$xia,$H1x		; 1
	XORMPY	$H2,$xia,$H2x		; 2
||	XORMPY	$H2u,$xib,$H2y
	XORMPY	$H3,$xia,$H3x		; 3
||	XORMPY	$H3u,$xib,$H3y
||[!A0]	MVK.D	15,A0				; *--${xip} counter
	XOR.L	$H0x,$Z0,$Z0		; 4	; Z^=H·(Xi[i]<<1)
|| [A0]	SUB.S	A0,1,A0
	XOR.L	$H1x,$Z1,$Z1		; 5
||	AND.D	$H01y,$FF000000,$H0z
||	SWAP2.L	$H01y,$H1y		;	; SHL	$H01y,16,$H1y
||	SHL	$x0,1,$xib
||	SHL	$x0,1,$xia

	XOR.L	$H2x,$Z2,$Z2		; 6/0	; [0,0] in epilogue
||	SHL	$Z0,1,$rem		;	; rem=Z<<1
||	SHRMB.S	$Z1,$Z0,$Z0		;	; Z>>=8
||	AND.L	$H1y,$FF000000,$H1z
	XOR.L	$H3x,$Z3,$Z3		; 7/1
||	SHRMB.S	$Z2,$Z1,$Z1
||	XOR.D	$H0z,$Z0,$Z0			; merge upper byte products
||	AND.S	$H2y,$FF000000,$H2z
||	XORMPY	$E10000,$rem,$res	;	; implicit rem&0x1FE
	XOR.L	$H1z,$Z1,$Z1		; 8/2
||	SHRMB.S	$Z3,$Z2,$Z2
||	AND.S	$H3y,$FF000000,$H3z
	XOR.L	$H2z,$Z2,$Z2		; 9/3
||	SHRU	$Z3,8,$Z3
	XOR.D	$H3z,$Z3,$Z3		; 10/4
	NOP				; 11/5

	SPKERNEL 0,2
||	XOR.D	$res,$Z3,$Z3		; 12/6/0; Z^=res

	; input pre-fetch is possible where D1 slot is available...
   [B0]	LDNDW	*${inp}[1],$H1x:$H0x	; 8/-
   [B0]	LDNDW	*${inp}++[2],$H3x:$H2x	; 9/-
	NOP				; 10/-
	.if	.LITTLE_ENDIAN
	SWAP2	$Z0,$Z1			; 11/-
||	SWAP4	$Z1,$Z0
	SWAP4	$Z1,$Z1			; 12/-
||	SWAP2	$Z0,$Z0
	SWAP2	$Z2,$Z3
||	SWAP4	$Z3,$Z2
||[!B0]	BNOP	RA
	SWAP4	$Z3,$Z3
||	SWAP2	$Z2,$Z2
|| [B0]	BNOP	ghash_loop?
   [B0]	XOR	$H0x,$Z0,$Z0		; Xi^=inp
|| [B0]	XOR	$H1x,$Z1,$Z1
   [B0]	XOR	$H2x,$Z2,$Z2
|| [B0]	XOR	$H3x,$Z3,$Z3
|| [B0]	SHRU	$Z1,24,$xia		; Xi[15], avoid cross-path stall
	STDW	$Z1:$Z0,*${xip}[1]
|| [B0]	SHRU	$Z1,16,$x0		; Xi[14]
|| [B0]	ZERO	$Z1:$Z0
	.else
  [!B0]	BNOP	RA			; 11/-
   [B0]	BNOP	ghash_loop?		; 12/-
   [B0]	XOR	$H0x,$Z0,$Z0		; Xi^=inp
|| [B0]	XOR	$H1x,$Z1,$Z1
   [B0]	XOR	$H2x,$Z2,$Z2
|| [B0]	XOR	$H3x,$Z3,$Z3
|| [B0]	MV	$Z0,$xia		; Xi[15], avoid cross-path stall
	STDW	$Z1:$Z0,*${xip}[1]
|| [B0] SHRU	$Z0,8,$x0		; Xi[14]
|| [B0]	ZERO	$Z1:$Z0
	.endif
	STDW	$Z3:$Z2,*${xip}[0]
|| [B0]	ZERO	$Z3:$Z2
|| [B0]	MV	$xia,$x1
   [B0]	ADDK	14,${xip}
	.endasmfunc

	.sect	.const
	.cstring "GHASH for C64x+, CRYPTOGAMS by <appro\@openssl.org>"
	.align	4
___

print $code;
close STDOUT;
