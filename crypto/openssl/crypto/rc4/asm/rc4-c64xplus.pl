#! /usr/bin/env perl
# Copyright 2014-2018 The OpenSSL Project Authors. All Rights Reserved.
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
# RC4 for C64x+.
#
# April 2014
#
# RC4 subroutine processes one byte in 7.0 cycles, which is 3x faster
# than TI CGT-generated code. Loop is scheduled in such way that
# there is only one reference to memory in each cycle. This is done
# to avoid L1D memory banking conflicts, see SPRU871 TI publication
# for further details. Otherwise it should be possible to schedule
# the loop for iteration interval of 6...

($KEY,$LEN,$INP,$OUT)=("A4","B4","A6","B6");

($KEYA,$XX,$TY,$xx,$ONE,$ret)=map("A$_",(5,7,8,9,1,2));
($KEYB,$YY,$TX,$tx,$SUM,$dat)=map("B$_",(5,7,8,9,1,2));

$code.=<<___;
	.text

	.if	.ASSEMBLER_VERSION<7000000
	.asg	0,__TI_EABI__
	.endif
	.if	__TI_EABI__
	.nocmp
	.asg	RC4,_RC4
	.asg	RC4_set_key,_RC4_set_key
	.asg	RC4_options,_RC4_options
	.endif

	.global	_RC4
	.align	16
_RC4:
	.asmfunc
	MV	$LEN,B0
  [!B0]	BNOP	B3			; if (len==0) return;
||[B0]	ADD	$KEY,2,$KEYA
||[B0]	ADD	$KEY,2,$KEYB
  [B0]	MVK	1,$ONE
||[B0]	LDBU	*${KEYA}[-2],$XX	; key->x
  [B0]	LDBU	*${KEYB}[-1],$YY	; key->y
||	NOP	4

	ADD4	$ONE,$XX,$XX
	LDBU	*${KEYA}[$XX],$TX
||	MVC	$LEN,ILC
	NOP	4
;;==================================================
	SPLOOP	7
||	ADD4	$TX,$YY,$YY

	LDBU	*${KEYB}[$YY],$TY
||	MVD	$XX,$xx
||	ADD4	$ONE,$XX,$XX
	LDBU	*${KEYA}[$XX],$tx
	CMPEQ	$YY,$XX,B0
||	NOP	3
	STB	$TX,*${KEYB}[$YY]
||[B0]	ADD4	$TX,$YY,$YY
	STB	$TY,*${KEYA}[$xx]
||[!B0]	ADD4	$tx,$YY,$YY
||[!B0]	MVD	$tx,$TX
	ADD4	$TY,$TX,$SUM		; [0,0] $TX is not replaced by $tx yet!
||	NOP	2
	LDBU	*$INP++,$dat
||	NOP	2
	LDBU	*${KEYB}[$SUM],$ret
||	NOP	5
	XOR.L	$dat,$ret,$ret
	SPKERNEL
||	STB	$ret,*$OUT++
;;==================================================
	SUB4	$XX,$ONE,$XX
||	NOP	5
	STB	$XX,*${KEYA}[-2]	; key->x
||	SUB4	$YY,$TX,$YY
||	BNOP	B3
	STB	$YY,*${KEYB}[-1]	; key->y
||	NOP	5
	.endasmfunc

	.global	_RC4_set_key
	.align	16
_RC4_set_key:
	.asmfunc
	.if	.BIG_ENDIAN
	MVK	0x00000404,$ONE
||	MVK	0x00000203,B0
	MVKH	0x04040000,$ONE
||	MVKH	0x00010000,B0
	.else
	MVK	0x00000404,$ONE
||	MVK	0x00000100,B0
	MVKH	0x04040000,$ONE
||	MVKH	0x03020000,B0
	.endif
	ADD	$KEY,2,$KEYA
||	ADD	$KEY,2,$KEYB
||	ADD	$INP,$LEN,$ret		; end of input
	LDBU	*${INP}++,$dat
||	MVK	0,$TX
	STH	$TX,*${KEY}++		; key->x=key->y=0
||	MV	B0,A0
||	MVK	64-4,B0

;;==================================================
	SPLOOPD	1
||	MVC	B0,ILC

	STNW	A0,*${KEY}++
||	ADD4	$ONE,A0,A0
	SPKERNEL
;;==================================================

	MVK	0,$YY
||	MVK	0,$XX
	MVK	1,$ONE
||	MVK	256-1,B0

;;==================================================
	SPLOOPD	8
||	MVC	B0,ILC

	ADD4	$dat,$YY,$YY
||	CMPEQ	$INP,$ret,A0		; end of input?
	LDBU	*${KEYB}[$YY],$TY
||	MVD	$XX,$xx
||	ADD4	$ONE,$XX,$XX
	LDBU	*${KEYA}[$XX],$tx
||[A0]	SUB	$INP,$LEN,$INP		; rewind
	LDBU	*${INP}++,$dat
||	CMPEQ	$YY,$XX,B0
||	NOP	3
	STB	$TX,*${KEYB}[$YY]
||[B0]	ADD4	$TX,$YY,$YY
	STB	$TY,*${KEYA}[$xx]
||[!B0]	ADD4	$tx,$YY,$YY
||[!B0]	MV	$tx,$TX
	SPKERNEL
;;==================================================

	BNOP	B3,5
	.endasmfunc

	.global	_RC4_options
	.align	16
_RC4_options:
_rc4_options:
	.asmfunc
	BNOP	B3,1
	ADDKPC	_rc4_options,B4
	.if	__TI_EABI__
	MVKL	\$PCR_OFFSET(rc4_options,_rc4_options),A4
	MVKH	\$PCR_OFFSET(rc4_options,_rc4_options),A4
	.else
	MVKL	(rc4_options-_rc4_options),A4
	MVKH	(rc4_options-_rc4_options),A4
	.endif
	ADD	B4,A4,A4
	.endasmfunc

	.if	__TI_EABI__
	.sect	".text:rc4_options.const"
	.else
	.sect	".const:rc4_options"
	.endif
	.align	4
rc4_options:
	.cstring "rc4(sploop,char)"
	.cstring "RC4 for C64+, CRYPTOGAMS by <appro\@openssl.org>"
	.align	4
___

$output=pop;
open STDOUT,">$output";
print $code;
close STDOUT;
