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
# SHA256 for C64x+.
#
# January 2012
#
# Performance is just below 10 cycles per processed byte, which is
# almost 40% faster than compiler-generated code. Unroll is unlikely
# to give more than ~8% improvement...
#
# !!! Note that this module uses AMR, which means that all interrupt
# service routines are expected to preserve it and for own well-being
# zero it upon entry.

while (($output=shift) && ($output!~/\w[\w\-]*\.\w+$/)) {}
open STDOUT,">$output";

($CTXA,$INP,$NUM) = ("A4","B4","A6");            # arguments
 $K256="A3";

($A,$Actx,$B,$Bctx,$C,$Cctx,$D,$Dctx,$T2,$S0,$s1,$t0a,$t1a,$t2a,$X9,$X14)
	=map("A$_",(16..31));
($E,$Ectx,$F,$Fctx,$G,$Gctx,$H,$Hctx,$T1,$S1,$s0,$t0e,$t1e,$t2e,$X1,$X15)
	=map("B$_",(16..31));

($Xia,$Xib)=("A5","B5");			# circular/ring buffer
 $CTXB=$t2e;

($Xn,$X0,$K)=("B7","B8","B9");
($Maj,$Ch)=($T2,"B6");

$code.=<<___;
	.text

	.if	.ASSEMBLER_VERSION<7000000
	.asg	0,__TI_EABI__
	.endif
	.if	__TI_EABI__
	.nocmp
	.asg	sha256_block_data_order,_sha256_block_data_order
	.endif

	.asg	B3,RA
	.asg	A15,FP
	.asg	B15,SP

	.if	.BIG_ENDIAN
	.asg	SWAP2,MV
	.asg	SWAP4,MV
	.endif

	.global	_sha256_block_data_order
_sha256_block_data_order:
__sha256_block:
	.asmfunc stack_usage(64)
	MV	$NUM,A0				; reassign $NUM
||	MVK	-64,B0
  [!A0]	BNOP	RA				; if ($NUM==0) return;
|| [A0]	STW	FP,*SP--[16]			; save frame pointer and alloca(64)
|| [A0]	MV	SP,FP
   [A0]	ADDKPC	__sha256_block,B2
|| [A0]	AND	B0,SP,SP			; align stack at 64 bytes
	.if	__TI_EABI__
   [A0]	MVK	0x00404,B1
|| [A0]	MVKL	\$PCR_OFFSET(K256,__sha256_block),$K256
   [A0]	MVKH	0x50000,B1
|| [A0]	MVKH	\$PCR_OFFSET(K256,__sha256_block),$K256
	.else
   [A0]	MVK	0x00404,B1
|| [A0]	MVKL	(K256-__sha256_block),$K256
   [A0]	MVKH	0x50000,B1
|| [A0]	MVKH	(K256-__sha256_block),$K256
	.endif
   [A0]	MVC	B1,AMR				; setup circular addressing
|| [A0]	MV	SP,$Xia
   [A0]	MV	SP,$Xib
|| [A0]	ADD	B2,$K256,$K256
|| [A0]	MV	$CTXA,$CTXB
|| [A0]	SUBAW	SP,2,SP				; reserve two words above buffer
	LDW	*${CTXA}[0],$A			; load ctx
||	LDW	*${CTXB}[4],$E
	LDW	*${CTXA}[1],$B
||	LDW	*${CTXB}[5],$F
	LDW	*${CTXA}[2],$C
||	LDW	*${CTXB}[6],$G
	LDW	*${CTXA}[3],$D
||	LDW	*${CTXB}[7],$H

	LDNW	*$INP++,$Xn			; pre-fetch input
	LDW	*$K256++,$K			; pre-fetch K256[0]
	MVK	14,B0				; loop counters
	MVK	47,B1
||	ADDAW	$Xia,9,$Xia
outerloop?:
	SUB	A0,1,A0
||	MV	$A,$Actx
||	MV	$E,$Ectx
||	MVD	$B,$Bctx
||	MVD	$F,$Fctx
	MV	$C,$Cctx
||	MV	$G,$Gctx
||	MVD	$D,$Dctx
||	MVD	$H,$Hctx
||	SWAP4	$Xn,$X0

	SPLOOPD	8				; BODY_00_14
||	MVC	B0,ILC
||	SWAP2	$X0,$X0

	LDNW	*$INP++,$Xn
||	ROTL	$A,30,$S0
||	OR	$A,$B,$Maj
||	AND	$A,$B,$t2a
||	ROTL	$E,26,$S1
||	AND	$F,$E,$Ch
||	ANDN	$G,$E,$t2e
	ROTL	$A,19,$t0a
||	AND	$C,$Maj,$Maj
||	ROTL	$E,21,$t0e
||	XOR	$t2e,$Ch,$Ch			; Ch(e,f,g) = (e&f)^(~e&g)
	ROTL	$A,10,$t1a
||	OR	$t2a,$Maj,$Maj			; Maj(a,b,c) = ((a|b)&c)|(a&b)
||	ROTL	$E,7,$t1e
||	ADD	$K,$H,$T1			; T1 = h + K256[i]
	ADD	$X0,$T1,$T1			; T1 += X[i];
||	STW	$X0,*$Xib++
||	XOR	$t0a,$S0,$S0
||	XOR	$t0e,$S1,$S1
	XOR	$t1a,$S0,$S0			; Sigma0(a)
||	XOR	$t1e,$S1,$S1			; Sigma1(e)
||	LDW	*$K256++,$K			; pre-fetch K256[i+1]
||	ADD	$Ch,$T1,$T1			; T1 += Ch(e,f,g)
	ADD	$S1,$T1,$T1			; T1 += Sigma1(e)
||	ADD	$S0,$Maj,$T2			; T2 = Sigma0(a) + Maj(a,b,c)
||	ROTL	$G,0,$H				; h = g
||	MV	$F,$G				; g = f
||	MV	$X0,$X14
||	SWAP4	$Xn,$X0
	SWAP2	$X0,$X0
||	MV	$E,$F				; f = e
||	ADD	$D,$T1,$E			; e = d + T1
||	MV	$C,$D				; d = c
	MV	$B,$C				; c = b
||	MV	$A,$B				; b = a
||	ADD	$T1,$T2,$A			; a = T1 + T2
	SPKERNEL

	ROTL	$A,30,$S0			; BODY_15
||	OR	$A,$B,$Maj
||	AND	$A,$B,$t2a
||	ROTL	$E,26,$S1
||	AND	$F,$E,$Ch
||	ANDN	$G,$E,$t2e
||	LDW	*${Xib}[1],$Xn			; modulo-scheduled
	ROTL	$A,19,$t0a
||	AND	$C,$Maj,$Maj
||	ROTL	$E,21,$t0e
||	XOR	$t2e,$Ch,$Ch			; Ch(e,f,g) = (e&f)^(~e&g)
||	LDW	*${Xib}[2],$X1			; modulo-scheduled
	ROTL	$A,10,$t1a
||	OR	$t2a,$Maj,$Maj			; Maj(a,b,c) = ((a|b)&c)|(a&b)
||	ROTL	$E,7,$t1e
||	ADD	$K,$H,$T1			; T1 = h + K256[i]
	ADD	$X0,$T1,$T1			; T1 += X[i];
||	STW	$X0,*$Xib++
||	XOR	$t0a,$S0,$S0
||	XOR	$t0e,$S1,$S1
	XOR	$t1a,$S0,$S0			; Sigma0(a)
||	XOR	$t1e,$S1,$S1			; Sigma1(e)
||	LDW	*$K256++,$K			; pre-fetch K256[i+1]
||	ADD	$Ch,$T1,$T1			; T1 += Ch(e,f,g)
	ADD	$S1,$T1,$T1			; T1 += Sigma1(e)
||	ADD	$S0,$Maj,$T2			; T2 = Sigma0(a) + Maj(a,b,c)
||	ROTL	$G,0,$H				; h = g
||	MV	$F,$G				; g = f
||	MV	$X0,$X15
	MV	$E,$F				; f = e
||	ADD	$D,$T1,$E			; e = d + T1
||	MV	$C,$D				; d = c
||	MV	$Xn,$X0				; modulo-scheduled
||	LDW	*$Xia,$X9			; modulo-scheduled
||	ROTL	$X1,25,$t0e			; modulo-scheduled
||	ROTL	$X14,15,$t0a			; modulo-scheduled
	SHRU	$X1,3,$s0			; modulo-scheduled
||	SHRU	$X14,10,$s1			; modulo-scheduled
||	ROTL	$B,0,$C				; c = b
||	MV	$A,$B				; b = a
||	ADD	$T1,$T2,$A			; a = T1 + T2

	SPLOOPD	10				; BODY_16_63
||	MVC	B1,ILC
||	ROTL	$X1,14,$t1e			; modulo-scheduled
||	ROTL	$X14,13,$t1a			; modulo-scheduled

	XOR	$t0e,$s0,$s0
||	XOR	$t0a,$s1,$s1
||	MV	$X15,$X14
||	MV	$X1,$Xn
	XOR	$t1e,$s0,$s0			; sigma0(X[i+1])
||	XOR	$t1a,$s1,$s1			; sigma1(X[i+14])
||	LDW	*${Xib}[2],$X1			; module-scheduled
	ROTL	$A,30,$S0
||	OR	$A,$B,$Maj
||	AND	$A,$B,$t2a
||	ROTL	$E,26,$S1
||	AND	$F,$E,$Ch
||	ANDN	$G,$E,$t2e
||	ADD	$X9,$X0,$X0			; X[i] += X[i+9]
	ROTL	$A,19,$t0a
||	AND	$C,$Maj,$Maj
||	ROTL	$E,21,$t0e
||	XOR	$t2e,$Ch,$Ch			; Ch(e,f,g) = (e&f)^(~e&g)
||	ADD	$s0,$X0,$X0			; X[i] += sigma1(X[i+1])
	ROTL	$A,10,$t1a
||	OR	$t2a,$Maj,$Maj			; Maj(a,b,c) = ((a|b)&c)|(a&b)
||	ROTL	$E,7,$t1e
||	ADD	$H,$K,$T1			; T1 = h + K256[i]
||	ADD	$s1,$X0,$X0			; X[i] += sigma1(X[i+14])
	XOR	$t0a,$S0,$S0
||	XOR	$t0e,$S1,$S1
||	ADD	$X0,$T1,$T1			; T1 += X[i]
||	STW	$X0,*$Xib++
	XOR	$t1a,$S0,$S0			; Sigma0(a)
||	XOR	$t1e,$S1,$S1			; Sigma1(e)
||	ADD	$Ch,$T1,$T1			; T1 += Ch(e,f,g)
||	MV	$X0,$X15
||	ROTL	$G,0,$H				; h = g
||	LDW	*$K256++,$K			; pre-fetch K256[i+1]
	ADD	$S1,$T1,$T1			; T1 += Sigma1(e)
||	ADD	$S0,$Maj,$T2			; T2 = Sigma0(a) + Maj(a,b,c)
||	MV	$F,$G				; g = f
||	MV	$Xn,$X0				; modulo-scheduled
||	LDW	*++$Xia,$X9			; modulo-scheduled
||	ROTL	$X1,25,$t0e			; module-scheduled
||	ROTL	$X14,15,$t0a			; modulo-scheduled
	ROTL	$X1,14,$t1e			; modulo-scheduled
||	ROTL	$X14,13,$t1a			; modulo-scheduled
||	MV	$E,$F				; f = e
||	ADD	$D,$T1,$E			; e = d + T1
||	MV	$C,$D				; d = c
||	MV	$B,$C				; c = b
	MV	$A,$B				; b = a
||	ADD	$T1,$T2,$A			; a = T1 + T2
||	SHRU	$X1,3,$s0			; modulo-scheduled
||	SHRU	$X14,10,$s1			; modulo-scheduled
	SPKERNEL

   [A0]	B	outerloop?
|| [A0]	LDNW	*$INP++,$Xn			; pre-fetch input
|| [A0]	ADDK	-260,$K256			; rewind K256
||	ADD	$Actx,$A,$A			; accumulate ctx
||	ADD	$Ectx,$E,$E
||	ADD	$Bctx,$B,$B
	ADD	$Fctx,$F,$F
||	ADD	$Cctx,$C,$C
||	ADD	$Gctx,$G,$G
||	ADD	$Dctx,$D,$D
||	ADD	$Hctx,$H,$H
|| [A0]	LDW	*$K256++,$K			; pre-fetch K256[0]

  [!A0]	BNOP	RA
||[!A0]	MV	$CTXA,$CTXB
  [!A0]	MV	FP,SP				; restore stack pointer
||[!A0]	LDW	*FP[0],FP			; restore frame pointer
  [!A0]	STW	$A,*${CTXA}[0]  		; save ctx
||[!A0]	STW	$E,*${CTXB}[4]
||[!A0]	MVK	0,B0
  [!A0]	STW	$B,*${CTXA}[1]
||[!A0]	STW	$F,*${CTXB}[5]
||[!A0]	MVC	B0,AMR				; clear AMR
	STW	$C,*${CTXA}[2]
||	STW	$G,*${CTXB}[6]
	STW	$D,*${CTXA}[3]
||	STW	$H,*${CTXB}[7]
	.endasmfunc

	.if	__TI_EABI__
	.sect	".text:sha_asm.const"
	.else
	.sect	".const:sha_asm"
	.endif
	.align	128
K256:
	.uword	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
	.uword	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
	.uword	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
	.uword	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
	.uword	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
	.uword	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
	.uword	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
	.uword	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
	.uword	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
	.uword	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
	.uword	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
	.uword	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
	.uword	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
	.uword	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
	.uword	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
	.uword	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
	.cstring "SHA256 block transform for C64x+, CRYPTOGAMS by <appro\@openssl.org>"
	.align	4

___

print $code;
close STDOUT;
