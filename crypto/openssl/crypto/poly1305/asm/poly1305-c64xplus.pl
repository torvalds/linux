#! /usr/bin/env perl
# Copyright 2016 The OpenSSL Project Authors. All Rights Reserved.
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
# Poly1305 hash for C64x+.
#
# October 2015
#
# Performance is [incredible for a 32-bit processor] 1.82 cycles per
# processed byte. Comparison to compiler-generated code is problematic,
# because results were observed to vary from 2.1 to 7.6 cpb depending
# on compiler's ability to inline small functions. Compiler also
# disables interrupts for some reason, thus making interrupt response
# time dependent on input length. This module on the other hand is free
# from such limitation.

$output=pop;
open STDOUT,">$output";

($CTXA,$INPB,$LEN,$PADBIT)=("A4","B4","A6","B6");
($H0,$H1,$H2,$H3,$H4,$H4a)=("A8","B8","A10","B10","B2",$LEN);
($D0,$D1,$D2,$D3)=         ("A9","B9","A11","B11");
($R0,$R1,$R2,$R3,$S1,$S2,$S3,$S3b)=("A0","B0","A1","B1","A12","B12","A13","B13");
($THREE,$R0b,$S2a)=("B7","B5","A5");

$code.=<<___;
	.text

	.if	.ASSEMBLER_VERSION<7000000
	.asg	0,__TI_EABI__
	.endif
	.if	__TI_EABI__
	.asg	poly1305_init,_poly1305_init
	.asg	poly1305_blocks,_poly1305_blocks
	.asg	poly1305_emit,_poly1305_emit
	.endif

	.asg	B3,RA
	.asg	A15,FP
	.asg	B15,SP

	.if	.LITTLE_ENDIAN
	.asg	MV,SWAP2
	.asg	MV.L,SWAP4
	.endif

	.global	_poly1305_init
_poly1305_init:
	.asmfunc
	LDNDW	*${INPB}[0],B17:B16	; load key material
	LDNDW	*${INPB}[1],A17:A16

||	ZERO	B9:B8
||	MVK	-1,B0
	STDW	B9:B8,*${CTXA}[0]	; initialize h1:h0
||	SHRU	B0,4,B0			; 0x0fffffff
||	MVK	-4,B1
	STDW	B9:B8,*${CTXA}[1]	; initialize h3:h2
||	AND	B0,B1,B1		; 0x0ffffffc
	STW	B8,*${CTXA}[4]		; initialize h4

	.if	.BIG_ENDIAN
	SWAP2	B16,B17
||	SWAP2	B17,B16
	SWAP2	A16,A17
||	SWAP2	A17,A16
	SWAP4	B16,B16
||	SWAP4	A16,A16
	SWAP4	B17,B17
||	SWAP4	A17,A17
	.endif

	AND	B16,B0,B20		; r0 = key[0] & 0x0fffffff
||	AND	B17,B1,B22		; r1 = key[1] & 0x0ffffffc
||	EXTU	B17,4,6,B16		; r1>>2
	AND	A16,B1,B21		; r2 = key[2] & 0x0ffffffc
||	AND	A17,B1,A23		; r3 = key[3] & 0x0ffffffc
||	BNOP	RA
	SHRU	B21,2,B18
||	ADD	B22,B16,B16		; s1 = r1 + r1>>2

	STDW	B21:B20,*${CTXA}[3]	; save r2:r0
||	ADD	B21,B18,B18		; s2 = r2 + r2>>2
||	SHRU	A23,2,B17
||	MV	A23,B23
	STDW	B23:B22,*${CTXA}[4]	; save r3:r1
||	ADD	B23,B17,B19		; s3 = r3 + r3>>2
||	ADD	B23,B17,B17		; s3 = r3 + r3>>2
	STDW	B17:B16,*${CTXA}[5]	; save s3:s1
	STDW	B19:B18,*${CTXA}[6]	; save s3:s2
||	ZERO	A4			; return 0
	.endasmfunc

	.global	_poly1305_blocks
	.align	32
_poly1305_blocks:
	.asmfunc	stack_usage(40)
	SHRU	$LEN,4,A2		; A2 is loop counter, number of blocks
  [!A2]	BNOP	RA			; no data
|| [A2]	STW	FP,*SP--(40)		; save frame pointer and alloca(40)
|| [A2]	MV	SP,FP
   [A2]	STDW	B13:B12,*SP[4]		; ABI says so
|| [A2]	MV	$CTXA,$S3b		; borrow $S3b
   [A2]	STDW	B11:B10,*SP[3]
|| [A2]	STDW	A13:A12,*FP[-3]
   [A2]	STDW	A11:A10,*FP[-4]

|| [A2]	LDDW	*${S3b}[0],B25:B24	; load h1:h0
   [A2]	LDNW	*${INPB}++[4],$D0	; load inp[0]
   [A2]	LDNW	*${INPB}[-3],$D1	; load inp[1]

	LDDW	*${CTXA}[1],B29:B28	; load h3:h2, B28 is h2
	LDNW	*${INPB}[-2],$D2	; load inp[2]
	LDNW	*${INPB}[-1],$D3	; load inp[3]

	LDDW	*${CTXA}[3],$R2:$R0	; load r2:r0
||	LDDW	*${S3b}[4],$R3:$R1	; load r3:r1
||	SWAP2	$D0,$D0

	LDDW	*${CTXA}[5],$S3:$S1	; load s3:s1
||	LDDW	*${S3b}[6],$S3b:$S2	; load s3:s2
||	SWAP4	$D0,$D0
||	SWAP2	$D1,$D1

	ADDU	$D0,B24,$D0:$H0		; h0+=inp[0]
||	ADD	$D0,B24,B27		; B-copy of h0+inp[0]
||	SWAP4	$D1,$D1
	ADDU	$D1,B25,$D1:$H1		; h1+=inp[1]
||	MVK	3,$THREE
||	SWAP2	$D2,$D2
	LDW	*${CTXA}[4],$H4		; load h4
||	SWAP4	$D2,$D2
||	MV	B29,B30			; B30 is h3
	MV	$R0,$R0b

loop?:
	MPY32U	$H0,$R0,A17:A16
||	MPY32U	B27,$R1,B17:B16		; MPY32U	$H0,$R1,B17:B16
||	ADDU	$D0,$D1:$H1,B25:B24	; ADDU		$D0,$D1:$H1,$D1:$H1
||	ADDU	$D2,B28,$D2:$H2		; h2+=inp[2]
||	SWAP2	$D3,$D3
	MPY32U	$H0,$R2,A19:A18
||	MPY32U	B27,$R3,B19:B18		; MPY32U	$H0,$R3,B19:B18
||	ADD	$D0,$H1,A24		; A-copy of B24
||	SWAP4	$D3,$D3
|| [A2]	SUB	A2,1,A2			; decrement loop counter

	MPY32U	A24,$S3,A21:A20		; MPY32U	$H1,$S3,A21:A20
||	MPY32U	B24,$R0b,B21:B20	; MPY32U	$H1,$R0,B21:B20
||	ADDU	B25,$D2:$H2,$D2:$H2	; ADDU		$D1,$D2:$H2,$D2:$H2
||	ADDU	$D3,B30,$D3:$H3		; h3+=inp[3]
||	ADD	B25,$H2,B25		; B-copy of $H2
	MPY32U	A24,$R1,A23:A22		; MPY32U	$H1,$R1,A23:A22
||	MPY32U	B24,$R2,B23:B22		; MPY32U	$H1,$R2,B23:B22

	MPY32U	$H2,$S2,A25:A24
||	MPY32U	B25,$S3b,B25:B24	; MPY32U	$H2,$S3,B25:B24
||	ADDU	$D2,$D3:$H3,$D3:$H3
||	ADD	$PADBIT,$H4,$H4		; h4+=padbit
	MPY32U	$H2,$R0,A27:A26
||	MPY32U	$H2,$R1,B27:B26
||	ADD	$D3,$H4,$H4
||	MV	$S2,$S2a

	MPY32U	$H3,$S1,A29:A28
||	MPY32U	$H3,$S2,B29:B28
||	ADD	A21,A17,A21		; start accumulating "d3:d0"
||	ADD	B21,B17,B21
||	ADDU	A20,A16,A17:A16
||	ADDU	B20,B16,B17:B16
|| [A2]	LDNW	*${INPB}++[4],$D0	; load inp[0]
	MPY32U	$H3,$S3,A31:A30
||	MPY32U	$H3,$R0b,B31:B30
||	ADD	A23,A19,A23
||	ADD	B23,B19,B23
||	ADDU	A22,A18,A19:A18
||	ADDU	B22,B18,B19:B18
|| [A2]	LDNW	*${INPB}[-3],$D1	; load inp[1]

	MPY32	$H4,$S1,B20
||	MPY32	$H4,$S2a,A20
||	ADD	A25,A21,A21
||	ADD	B25,B21,B21
||	ADDU	A24,A17:A16,A17:A16
||	ADDU	B24,B17:B16,B17:B16
|| [A2]	LDNW	*${INPB}[-2],$D2	; load inp[2]
	MPY32	$H4,$S3b,B22
||	ADD	A27,A23,A23
||	ADD	B27,B23,B23
||	ADDU	A26,A19:A18,A19:A18
||	ADDU	B26,B19:B18,B19:B18
|| [A2]	LDNW	*${INPB}[-1],$D3	; load inp[3]

	MPY32	$H4,$R0b,$H4
||	ADD	A29,A21,A21		; final hi("d0")
||	ADD	B29,B21,B21		; final hi("d1")
||	ADDU	A28,A17:A16,A17:A16	; final lo("d0")
||	ADDU	B28,B17:B16,B17:B16
	ADD	A31,A23,A23		; final hi("d2")
||	ADD	B31,B23,B23		; final hi("d3")
||	ADDU	A30,A19:A18,A19:A18
||	ADDU	B30,B19:B18,B19:B18
	ADDU	B20,B17:B16,B17:B16	; final lo("d1")
||	ADDU	A20,A19:A18,A19:A18	; final lo("d2")
	ADDU	B22,B19:B18,B19:B18	; final lo("d3")

||	ADD	A17,A21,A21		; "flatten" "d3:d0"
	MV	A19,B29			; move to avoid cross-path stalls
	ADDU	A21,B17:B16,B27:B26	; B26 is h1
	ADD	B21,B27,B27
||	DMV	B29,A18,B29:B28		; move to avoid cross-path stalls
	ADDU	B27,B29:B28,B29:B28	; B28 is h2
|| [A2]	SWAP2	$D0,$D0
	ADD	A23,B29,B29
|| [A2]	SWAP4	$D0,$D0
	ADDU	B29,B19:B18,B31:B30	; B30 is h3
	ADD	B23,B31,B31
||	MV	A16,B24			; B24 is h0
|| [A2]	SWAP2	$D1,$D1
	ADD	B31,$H4,$H4
|| [A2]	SWAP4	$D1,$D1

	SHRU	$H4,2,B16		; last reduction step
||	AND	$H4,$THREE,$H4
	ADDAW	B16,B16,B16		; 5*(h4>>2)
|| [A2]	BNOP	loop?

	ADDU	B24,B16,B25:B24		; B24 is h0
|| [A2]	SWAP2	$D2,$D2
	ADDU	B26,B25,B27:B26		; B26 is h1
|| [A2]	SWAP4	$D2,$D2
	ADDU	B28,B27,B29:B28		; B28 is h2
|| [A2]	ADDU	$D0,B24,$D0:$H0		; h0+=inp[0]
|| [A2]	ADD	$D0,B24,B27		; B-copy of h0+inp[0]
	ADDU	B30,B29,B31:B30		; B30 is h3
	ADD	B31,$H4,$H4
|| [A2]	ADDU	$D1,B26,$D1:$H1		; h1+=inp[1]
;;===== branch to loop? is taken here

	LDDW	*FP[-4],A11:A10		; ABI says so
	LDDW	*FP[-3],A13:A12
||	LDDW	*SP[3],B11:B10
	LDDW	*SP[4],B13:B12
||	MV	B26,B25
||	BNOP	RA
	LDW	*++SP(40),FP		; restore frame pointer
||	MV	B30,B29
	STDW	B25:B24,*${CTXA}[0]	; save h1:h0
	STDW	B29:B28,*${CTXA}[1]	; save h3:h2
	STW	$H4,*${CTXA}[4]		; save h4
	NOP	1
	.endasmfunc
___
{
my ($MAC,$NONCEA,$NONCEB)=($INPB,$LEN,$PADBIT);

$code.=<<___;
	.global	_poly1305_emit
	.align	32
_poly1305_emit:
	.asmfunc
	LDDW	*${CTXA}[0],A17:A16	; load h1:h0
	LDDW	*${CTXA}[1],A19:A18	; load h3:h2
	LDW	*${CTXA}[4],A20		; load h4
	MV	$NONCEA,$NONCEB

	MVK	5,A22			; compare to modulus
	ADDU	A16,A22,A23:A22
||	LDW	*${NONCEA}[0],A8
||	LDW	*${NONCEB}[1],B8
	ADDU	A17,A23,A25:A24
||	LDW	*${NONCEA}[2],A9
||	LDW	*${NONCEB}[3],B9
	ADDU	A19,A25,A27:A26
	ADDU	A19,A27,A29:A28
	ADD	A20,A29,A29

	SHRU	A29,2,A2		; check for overflow in 130-th bit

   [A2]	MV	A22,A16			; select
|| [A2]	MV	A24,A17
   [A2]	MV	A26,A18
|| [A2]	MV	A28,A19

||	ADDU	A8,A16,A23:A22		; accumulate nonce
	ADDU	B8,A17,A25:A24
||	SWAP2	A22,A22
	ADDU	A23,A25:A24,A25:A24
	ADDU	A9,A18,A27:A26
||	SWAP2	A24,A24
	ADDU	A25,A27:A26,A27:A26
||	ADD	B9,A19,A28
	ADD	A27,A28,A28
||	SWAP2	A26,A26

	.if	.BIG_ENDIAN
	SWAP2	A28,A28
||	SWAP4	A22,A22
||	SWAP4	A24,B24
	SWAP4	A26,A26
	SWAP4	A28,A28
||	MV	B24,A24
	.endif

	BNOP	RA,1
	STNW	A22,*${MAC}[0]		; write the result
	STNW	A24,*${MAC}[1]
	STNW	A26,*${MAC}[2]
	STNW	A28,*${MAC}[3]
	.endasmfunc
___
}
$code.=<<___;
	.sect	.const
	.cstring "Poly1305 for C64x+, CRYPTOGAMS by <appro\@openssl.org>"
	.align	4
___

print $code;
