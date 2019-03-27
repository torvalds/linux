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
# SHA512 for C64x+.
#
# January 2012
#
# Performance is 19 cycles per processed byte. Compared to block
# transform function from sha512.c compiled with cl6x with -mv6400+
# -o2 -DOPENSSL_SMALL_FOOTPRINT it's almost 7x faster and 2x smaller.
# Loop unroll won't make it, this implementation, any faster, because
# it's effectively dominated by SHRU||SHL pairs and you can't schedule
# more of them.
#
# !!! Note that this module uses AMR, which means that all interrupt
# service routines are expected to preserve it and for own well-being
# zero it upon entry.

while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {}
open STDOUT,">$output";

($CTXA,$INP,$NUM) = ("A4","B4","A6");            # arguments
 $K512="A3";

($Ahi,$Actxhi,$Bhi,$Bctxhi,$Chi,$Cctxhi,$Dhi,$Dctxhi,
 $Ehi,$Ectxhi,$Fhi,$Fctxhi,$Ghi,$Gctxhi,$Hhi,$Hctxhi)=map("A$_",(16..31));
($Alo,$Actxlo,$Blo,$Bctxlo,$Clo,$Cctxlo,$Dlo,$Dctxlo,
 $Elo,$Ectxlo,$Flo,$Fctxlo,$Glo,$Gctxlo,$Hlo,$Hctxlo)=map("B$_",(16..31));

($S1hi,$CHhi,$S0hi,$t0hi)=map("A$_",(10..13));
($S1lo,$CHlo,$S0lo,$t0lo)=map("B$_",(10..13));
($T1hi,         $T2hi)=         ("A6","A7");
($T1lo,$T1carry,$T2lo,$T2carry)=("B6","B7","B8","B9");
($Khi,$Klo)=("A9","A8");
($MAJhi,$MAJlo)=($T2hi,$T2lo);
($t1hi,$t1lo)=($Khi,"B2");
 $CTXB=$t1lo;

($Xihi,$Xilo)=("A5","B5");			# circular/ring buffer

$code.=<<___;
	.text

	.if	.ASSEMBLER_VERSION<7000000
	.asg	0,__TI_EABI__
	.endif
	.if	__TI_EABI__
	.nocmp
	.asg	sha512_block_data_order,_sha512_block_data_order
	.endif

	.asg	B3,RA
	.asg	A15,FP
	.asg	B15,SP

	.if	.BIG_ENDIAN
	.asg	$Khi,KHI
	.asg	$Klo,KLO
	.else
	.asg	$Khi,KLO
	.asg	$Klo,KHI
	.endif

	.global	_sha512_block_data_order
_sha512_block_data_order:
__sha512_block:
	.asmfunc stack_usage(40+128)
	MV	$NUM,A0				; reassign $NUM
||	MVK	-128,B0
  [!A0]	BNOP	RA				; if ($NUM==0) return;
|| [A0]	STW	FP,*SP--(40)			; save frame pointer
|| [A0]	MV	SP,FP
   [A0]	STDW	B13:B12,*SP[4]
|| [A0]	MVK	0x00404,B1
   [A0]	STDW	B11:B10,*SP[3]
|| [A0]	STDW	A13:A12,*FP[-3]
|| [A0]	MVKH	0x60000,B1
   [A0]	STDW	A11:A10,*SP[1]
|| [A0]	MVC	B1,AMR				; setup circular addressing
|| [A0]	ADD	B0,SP,SP			; alloca(128)
	.if	__TI_EABI__
   [A0]	AND	B0,SP,SP			; align stack at 128 bytes
|| [A0]	ADDKPC	__sha512_block,B1
|| [A0]	MVKL	\$PCR_OFFSET(K512,__sha512_block),$K512
   [A0]	MVKH	\$PCR_OFFSET(K512,__sha512_block),$K512
|| [A0]	SUBAW	SP,2,SP				; reserve two words above buffer
	.else
   [A0]	AND	B0,SP,SP			; align stack at 128 bytes
|| [A0]	ADDKPC	__sha512_block,B1
|| [A0]	MVKL	(K512-__sha512_block),$K512
   [A0]	MVKH	(K512-__sha512_block),$K512
|| [A0]	SUBAW	SP,2,SP				; reserve two words above buffer
	.endif
	ADDAW	SP,3,$Xilo
	ADDAW	SP,2,$Xihi

||	MV	$CTXA,$CTXB
	LDW	*${CTXA}[0^.LITTLE_ENDIAN],$Ahi	; load ctx
||	LDW	*${CTXB}[1^.LITTLE_ENDIAN],$Alo
||	ADD	B1,$K512,$K512
	LDW	*${CTXA}[2^.LITTLE_ENDIAN],$Bhi
||	LDW	*${CTXB}[3^.LITTLE_ENDIAN],$Blo
	LDW	*${CTXA}[4^.LITTLE_ENDIAN],$Chi
||	LDW	*${CTXB}[5^.LITTLE_ENDIAN],$Clo
	LDW	*${CTXA}[6^.LITTLE_ENDIAN],$Dhi
||	LDW	*${CTXB}[7^.LITTLE_ENDIAN],$Dlo
	LDW	*${CTXA}[8^.LITTLE_ENDIAN],$Ehi
||	LDW	*${CTXB}[9^.LITTLE_ENDIAN],$Elo
	LDW	*${CTXA}[10^.LITTLE_ENDIAN],$Fhi
||	LDW	*${CTXB}[11^.LITTLE_ENDIAN],$Flo
	LDW	*${CTXA}[12^.LITTLE_ENDIAN],$Ghi
||	LDW	*${CTXB}[13^.LITTLE_ENDIAN],$Glo
	LDW	*${CTXA}[14^.LITTLE_ENDIAN],$Hhi
||	LDW	*${CTXB}[15^.LITTLE_ENDIAN],$Hlo

	LDNDW	*$INP++,B11:B10			; pre-fetch input
	LDDW	*$K512++,$Khi:$Klo		; pre-fetch K512[0]
outerloop?:
	MVK	15,B0				; loop counters
||	MVK	64,B1
||	SUB	A0,1,A0
	MV	$Ahi,$Actxhi
||	MV	$Alo,$Actxlo
||	MV	$Bhi,$Bctxhi
||	MV	$Blo,$Bctxlo
||	MV	$Chi,$Cctxhi
||	MV	$Clo,$Cctxlo
||	MVD	$Dhi,$Dctxhi
||	MVD	$Dlo,$Dctxlo
	MV	$Ehi,$Ectxhi
||	MV	$Elo,$Ectxlo
||	MV	$Fhi,$Fctxhi
||	MV	$Flo,$Fctxlo
||	MV	$Ghi,$Gctxhi
||	MV	$Glo,$Gctxlo
||	MVD	$Hhi,$Hctxhi
||	MVD	$Hlo,$Hctxlo
loop0_15?:
	.if	.BIG_ENDIAN
	MV	B11,$T1hi
||	MV	B10,$T1lo
	.else
	SWAP4	B10,$T1hi
||	SWAP4	B11,$T1lo
	SWAP2	$T1hi,$T1hi
||	SWAP2	$T1lo,$T1lo
	.endif
loop16_79?:
	STW	$T1hi,*$Xihi++[2]
||	STW	$T1lo,*$Xilo++[2]			; X[i] = T1
||	ADD	$Hhi,$T1hi,$T1hi
||	ADDU	$Hlo,$T1lo,$T1carry:$T1lo		; T1 += h
||	SHRU	$Ehi,14,$S1hi
||	SHL	$Ehi,32-14,$S1lo
	XOR	$Fhi,$Ghi,$CHhi
||	XOR	$Flo,$Glo,$CHlo
||	ADD	KHI,$T1hi,$T1hi
||	ADDU	KLO,$T1carry:$T1lo,$T1carry:$T1lo	; T1 += K512[i]
||	SHRU	$Elo,14,$t0lo
||	SHL	$Elo,32-14,$t0hi
	XOR	$t0hi,$S1hi,$S1hi
||	XOR	$t0lo,$S1lo,$S1lo
||	AND	$Ehi,$CHhi,$CHhi
||	AND	$Elo,$CHlo,$CHlo
||	ROTL	$Ghi,0,$Hhi
||	ROTL	$Glo,0,$Hlo				; h = g
||	SHRU	$Ehi,18,$t0hi
||	SHL	$Ehi,32-18,$t0lo
	XOR	$t0hi,$S1hi,$S1hi
||	XOR	$t0lo,$S1lo,$S1lo
||	XOR	$Ghi,$CHhi,$CHhi
||	XOR	$Glo,$CHlo,$CHlo			; Ch(e,f,g) = ((f^g)&e)^g
||	ROTL	$Fhi,0,$Ghi
||	ROTL	$Flo,0,$Glo				; g = f
||	SHRU	$Elo,18,$t0lo
||	SHL	$Elo,32-18,$t0hi
	XOR	$t0hi,$S1hi,$S1hi
||	XOR	$t0lo,$S1lo,$S1lo
||	OR	$Ahi,$Bhi,$MAJhi
||	OR	$Alo,$Blo,$MAJlo
||	ROTL	$Ehi,0,$Fhi
||	ROTL	$Elo,0,$Flo				; f = e
||	SHRU	$Ehi,41-32,$t0lo
||	SHL	$Ehi,64-41,$t0hi
	XOR	$t0hi,$S1hi,$S1hi
||	XOR	$t0lo,$S1lo,$S1lo
||	AND	$Chi,$MAJhi,$MAJhi
||	AND	$Clo,$MAJlo,$MAJlo
||	ROTL	$Dhi,0,$Ehi
||	ROTL	$Dlo,0,$Elo				; e = d
||	SHRU	$Elo,41-32,$t0hi
||	SHL	$Elo,64-41,$t0lo
	XOR	$t0hi,$S1hi,$S1hi
||	XOR	$t0lo,$S1lo,$S1lo			; Sigma1(e)
||	AND	$Ahi,$Bhi,$t1hi
||	AND	$Alo,$Blo,$t1lo
||	ROTL	$Chi,0,$Dhi
||	ROTL	$Clo,0,$Dlo				; d = c
||	SHRU	$Ahi,28,$S0hi
||	SHL	$Ahi,32-28,$S0lo
	OR	$t1hi,$MAJhi,$MAJhi
||	OR	$t1lo,$MAJlo,$MAJlo			; Maj(a,b,c) = ((a|b)&c)|(a&b)
||	ADD	$CHhi,$T1hi,$T1hi
||	ADDU	$CHlo,$T1carry:$T1lo,$T1carry:$T1lo	; T1 += Ch(e,f,g)
||	ROTL	$Bhi,0,$Chi
||	ROTL	$Blo,0,$Clo				; c = b
||	SHRU	$Alo,28,$t0lo
||	SHL	$Alo,32-28,$t0hi
	XOR	$t0hi,$S0hi,$S0hi
||	XOR	$t0lo,$S0lo,$S0lo
||	ADD	$S1hi,$T1hi,$T1hi
||	ADDU	$S1lo,$T1carry:$T1lo,$T1carry:$T1lo	; T1 += Sigma1(e)
||	ROTL	$Ahi,0,$Bhi
||	ROTL	$Alo,0,$Blo				; b = a
||	SHRU	$Ahi,34-32,$t0lo
||	SHL	$Ahi,64-34,$t0hi
	XOR	$t0hi,$S0hi,$S0hi
||	XOR	$t0lo,$S0lo,$S0lo
||	ADD	$MAJhi,$T1hi,$T2hi
||	ADDU	$MAJlo,$T1carry:$T1lo,$T2carry:$T2lo	; T2 = T1+Maj(a,b,c)
||	SHRU	$Alo,34-32,$t0hi
||	SHL	$Alo,64-34,$t0lo
	XOR	$t0hi,$S0hi,$S0hi
||	XOR	$t0lo,$S0lo,$S0lo
||	ADD	$Ehi,$T1hi,$T1hi
||	ADDU	$Elo,$T1carry:$T1lo,$T1carry:$T1lo	; T1 += e
|| [B0]	BNOP	loop0_15?
||	SHRU	$Ahi,39-32,$t0lo
||	SHL	$Ahi,64-39,$t0hi
	XOR	$t0hi,$S0hi,$S0hi
||	XOR	$t0lo,$S0lo,$S0lo
|| [B0]	LDNDW	*$INP++,B11:B10				; pre-fetch input
||[!B1]	BNOP	break?
||	SHRU	$Alo,39-32,$t0hi
||	SHL	$Alo,64-39,$t0lo
	XOR	$t0hi,$S0hi,$S0hi
||	XOR	$t0lo,$S0lo,$S0lo			; Sigma0(a)
||	ADD	$T1carry,$T1hi,$Ehi
||	MV	$T1lo,$Elo				; e = T1
||[!B0]	LDW	*${Xihi}[28],$T1hi
||[!B0]	LDW	*${Xilo}[28],$T1lo			; X[i+14]
	ADD	$S0hi,$T2hi,$T2hi
||	ADDU	$S0lo,$T2carry:$T2lo,$T2carry:$T2lo	; T2 += Sigma0(a)
|| [B1]	LDDW	*$K512++,$Khi:$Klo			; pre-fetch K512[i]
	NOP						; avoid cross-path stall
	ADD	$T2carry,$T2hi,$Ahi
||	MV	$T2lo,$Alo				; a = T2
|| [B0]	SUB	B0,1,B0
;;===== branch to loop00_15? is taken here
	NOP
;;===== branch to break? is taken here
	LDW	*${Xihi}[2],$T2hi
||	LDW	*${Xilo}[2],$T2lo			; X[i+1]
||	SHRU	$T1hi,19,$S1hi
||	SHL	$T1hi,32-19,$S1lo
	SHRU	$T1lo,19,$t0lo
||	SHL	$T1lo,32-19,$t0hi
	XOR	$t0hi,$S1hi,$S1hi
||	XOR	$t0lo,$S1lo,$S1lo
||	SHRU	$T1hi,61-32,$t0lo
||	SHL	$T1hi,64-61,$t0hi
	XOR	$t0hi,$S1hi,$S1hi
||	XOR	$t0lo,$S1lo,$S1lo
||	SHRU	$T1lo,61-32,$t0hi
||	SHL	$T1lo,64-61,$t0lo
	XOR	$t0hi,$S1hi,$S1hi
||	XOR	$t0lo,$S1lo,$S1lo
||	SHRU	$T1hi,6,$t0hi
||	SHL	$T1hi,32-6,$t0lo
	XOR	$t0hi,$S1hi,$S1hi
||	XOR	$t0lo,$S1lo,$S1lo
||	SHRU	$T1lo,6,$t0lo
||	LDW	*${Xihi}[18],$T1hi
||	LDW	*${Xilo}[18],$T1lo			; X[i+9]
	XOR	$t0lo,$S1lo,$S1lo			; sigma1(Xi[i+14])

||	LDW	*${Xihi}[0],$CHhi
||	LDW	*${Xilo}[0],$CHlo			; X[i]
||	SHRU	$T2hi,1,$S0hi
||	SHL	$T2hi,32-1,$S0lo
	SHRU	$T2lo,1,$t0lo
||	SHL	$T2lo,32-1,$t0hi
	XOR	$t0hi,$S0hi,$S0hi
||	XOR	$t0lo,$S0lo,$S0lo
||	SHRU	$T2hi,8,$t0hi
||	SHL	$T2hi,32-8,$t0lo
	XOR	$t0hi,$S0hi,$S0hi
||	XOR	$t0lo,$S0lo,$S0lo
||	SHRU	$T2lo,8,$t0lo
||	SHL	$T2lo,32-8,$t0hi
	XOR	$t0hi,$S0hi,$S0hi
||	XOR	$t0lo,$S0lo,$S0lo
||	ADD	$S1hi,$T1hi,$T1hi
||	ADDU	$S1lo,$T1lo,$T1carry:$T1lo		; T1 = X[i+9]+sigma1()
|| [B1]	BNOP	loop16_79?
||	SHRU	$T2hi,7,$t0hi
||	SHL	$T2hi,32-7,$t0lo
	XOR	$t0hi,$S0hi,$S0hi
||	XOR	$t0lo,$S0lo,$S0lo
||	ADD	$CHhi,$T1hi,$T1hi
||	ADDU	$CHlo,$T1carry:$T1lo,$T1carry:$T1lo	; T1 += X[i]
||	SHRU	$T2lo,7,$t0lo
	XOR	$t0lo,$S0lo,$S0lo			; sigma0(Xi[i+1]

	ADD	$S0hi,$T1hi,$T1hi
||	ADDU	$S0lo,$T1carry:$T1lo,$T1carry:$T1lo	; T1 += sigma0()
|| [B1]	SUB	B1,1,B1
	NOP						; avoid cross-path stall
	ADD	$T1carry,$T1hi,$T1hi
;;===== branch to loop16_79? is taken here

break?:
	ADD	$Ahi,$Actxhi,$Ahi		; accumulate ctx
||	ADDU	$Alo,$Actxlo,$Actxlo:$Alo
|| [A0]	LDNDW	*$INP++,B11:B10			; pre-fetch input
|| [A0]	ADDK	-640,$K512			; rewind pointer to K512
	ADD	$Bhi,$Bctxhi,$Bhi
||	ADDU	$Blo,$Bctxlo,$Bctxlo:$Blo
|| [A0]	LDDW	*$K512++,$Khi:$Klo		; pre-fetch K512[0]
	ADD	$Chi,$Cctxhi,$Chi
||	ADDU	$Clo,$Cctxlo,$Cctxlo:$Clo
||	ADD	$Actxlo,$Ahi,$Ahi
||[!A0]	MV	$CTXA,$CTXB
	ADD	$Dhi,$Dctxhi,$Dhi
||	ADDU	$Dlo,$Dctxlo,$Dctxlo:$Dlo
||	ADD	$Bctxlo,$Bhi,$Bhi
||[!A0]	STW	$Ahi,*${CTXA}[0^.LITTLE_ENDIAN]	; save ctx
||[!A0]	STW	$Alo,*${CTXB}[1^.LITTLE_ENDIAN]
	ADD	$Ehi,$Ectxhi,$Ehi
||	ADDU	$Elo,$Ectxlo,$Ectxlo:$Elo
||	ADD	$Cctxlo,$Chi,$Chi
|| [A0]	BNOP	outerloop?
||[!A0]	STW	$Bhi,*${CTXA}[2^.LITTLE_ENDIAN]
||[!A0]	STW	$Blo,*${CTXB}[3^.LITTLE_ENDIAN]
	ADD	$Fhi,$Fctxhi,$Fhi
||	ADDU	$Flo,$Fctxlo,$Fctxlo:$Flo
||	ADD	$Dctxlo,$Dhi,$Dhi
||[!A0]	STW	$Chi,*${CTXA}[4^.LITTLE_ENDIAN]
||[!A0]	STW	$Clo,*${CTXB}[5^.LITTLE_ENDIAN]
	ADD	$Ghi,$Gctxhi,$Ghi
||	ADDU	$Glo,$Gctxlo,$Gctxlo:$Glo
||	ADD	$Ectxlo,$Ehi,$Ehi
||[!A0]	STW	$Dhi,*${CTXA}[6^.LITTLE_ENDIAN]
||[!A0]	STW	$Dlo,*${CTXB}[7^.LITTLE_ENDIAN]
	ADD	$Hhi,$Hctxhi,$Hhi
||	ADDU	$Hlo,$Hctxlo,$Hctxlo:$Hlo
||	ADD	$Fctxlo,$Fhi,$Fhi
||[!A0]	STW	$Ehi,*${CTXA}[8^.LITTLE_ENDIAN]
||[!A0]	STW	$Elo,*${CTXB}[9^.LITTLE_ENDIAN]
	ADD	$Gctxlo,$Ghi,$Ghi
||[!A0]	STW	$Fhi,*${CTXA}[10^.LITTLE_ENDIAN]
||[!A0]	STW	$Flo,*${CTXB}[11^.LITTLE_ENDIAN]
	ADD	$Hctxlo,$Hhi,$Hhi
||[!A0]	STW	$Ghi,*${CTXA}[12^.LITTLE_ENDIAN]
||[!A0]	STW	$Glo,*${CTXB}[13^.LITTLE_ENDIAN]
;;===== branch to outerloop? is taken here

	STW	$Hhi,*${CTXA}[14^.LITTLE_ENDIAN]
||	STW	$Hlo,*${CTXB}[15^.LITTLE_ENDIAN]
||	MVK	-40,B0
	ADD	FP,B0,SP			; destroy circular buffer
||	LDDW	*FP[-4],A11:A10
	LDDW	*SP[2],A13:A12
||	LDDW	*FP[-2],B11:B10
	LDDW	*SP[4],B13:B12
||	BNOP	RA
	LDW	*++SP(40),FP			; restore frame pointer
	MVK	0,B0
	MVC	B0,AMR				; clear AMR
	NOP	2				; wait till FP is committed
	.endasmfunc

	.if	__TI_EABI__
	.sect	".text:sha_asm.const"
	.else
	.sect	".const:sha_asm"
	.endif
	.align	128
K512:
	.uword	0x428a2f98,0xd728ae22, 0x71374491,0x23ef65cd
	.uword	0xb5c0fbcf,0xec4d3b2f, 0xe9b5dba5,0x8189dbbc
	.uword	0x3956c25b,0xf348b538, 0x59f111f1,0xb605d019
	.uword	0x923f82a4,0xaf194f9b, 0xab1c5ed5,0xda6d8118
	.uword	0xd807aa98,0xa3030242, 0x12835b01,0x45706fbe
	.uword	0x243185be,0x4ee4b28c, 0x550c7dc3,0xd5ffb4e2
	.uword	0x72be5d74,0xf27b896f, 0x80deb1fe,0x3b1696b1
	.uword	0x9bdc06a7,0x25c71235, 0xc19bf174,0xcf692694
	.uword	0xe49b69c1,0x9ef14ad2, 0xefbe4786,0x384f25e3
	.uword	0x0fc19dc6,0x8b8cd5b5, 0x240ca1cc,0x77ac9c65
	.uword	0x2de92c6f,0x592b0275, 0x4a7484aa,0x6ea6e483
	.uword	0x5cb0a9dc,0xbd41fbd4, 0x76f988da,0x831153b5
	.uword	0x983e5152,0xee66dfab, 0xa831c66d,0x2db43210
	.uword	0xb00327c8,0x98fb213f, 0xbf597fc7,0xbeef0ee4
	.uword	0xc6e00bf3,0x3da88fc2, 0xd5a79147,0x930aa725
	.uword	0x06ca6351,0xe003826f, 0x14292967,0x0a0e6e70
	.uword	0x27b70a85,0x46d22ffc, 0x2e1b2138,0x5c26c926
	.uword	0x4d2c6dfc,0x5ac42aed, 0x53380d13,0x9d95b3df
	.uword	0x650a7354,0x8baf63de, 0x766a0abb,0x3c77b2a8
	.uword	0x81c2c92e,0x47edaee6, 0x92722c85,0x1482353b
	.uword	0xa2bfe8a1,0x4cf10364, 0xa81a664b,0xbc423001
	.uword	0xc24b8b70,0xd0f89791, 0xc76c51a3,0x0654be30
	.uword	0xd192e819,0xd6ef5218, 0xd6990624,0x5565a910
	.uword	0xf40e3585,0x5771202a, 0x106aa070,0x32bbd1b8
	.uword	0x19a4c116,0xb8d2d0c8, 0x1e376c08,0x5141ab53
	.uword	0x2748774c,0xdf8eeb99, 0x34b0bcb5,0xe19b48a8
	.uword	0x391c0cb3,0xc5c95a63, 0x4ed8aa4a,0xe3418acb
	.uword	0x5b9cca4f,0x7763e373, 0x682e6ff3,0xd6b2b8a3
	.uword	0x748f82ee,0x5defb2fc, 0x78a5636f,0x43172f60
	.uword	0x84c87814,0xa1f0ab72, 0x8cc70208,0x1a6439ec
	.uword	0x90befffa,0x23631e28, 0xa4506ceb,0xde82bde9
	.uword	0xbef9a3f7,0xb2c67915, 0xc67178f2,0xe372532b
	.uword	0xca273ece,0xea26619c, 0xd186b8c7,0x21c0c207
	.uword	0xeada7dd6,0xcde0eb1e, 0xf57d4f7f,0xee6ed178
	.uword	0x06f067aa,0x72176fba, 0x0a637dc5,0xa2c898a6
	.uword	0x113f9804,0xbef90dae, 0x1b710b35,0x131c471b
	.uword	0x28db77f5,0x23047d84, 0x32caab7b,0x40c72493
	.uword	0x3c9ebe0a,0x15c9bebc, 0x431d67c4,0x9c100d4c
	.uword	0x4cc5d4be,0xcb3e42b6, 0x597f299c,0xfc657e2a
	.uword	0x5fcb6fab,0x3ad6faec, 0x6c44198c,0x4a475817
	.cstring "SHA512 block transform for C64x+, CRYPTOGAMS by <appro\@openssl.org>"
	.align	4
___

print $code;
close STDOUT;
