;; Copyright 2012-2016 The OpenSSL Project Authors. All Rights Reserved.
;;
;; Licensed under the OpenSSL license (the "License").  You may not use
;; this file except in compliance with the License.  You can obtain a copy
;; in the file LICENSE in the source distribution or at
;; https://www.openssl.org/source/license.html
;;
;;====================================================================
;; Written by Andy Polyakov <appro@openssl.org> for the OpenSSL
;; project.
;;
;; Rights for redistribution and usage in source and binary forms are
;; granted according to the OpenSSL license. Warranty of any kind is
;; disclaimed.
;;====================================================================
;; Compiler-generated multiply-n-add SPLOOP runs at 12*n cycles, n
;; being the number of 32-bit words, addition - 8*n. Corresponding 4x
;; unrolled SPLOOP-free loops - at ~8*n and ~5*n. Below assembler
;; SPLOOPs spin at ... 2*n cycles [plus epilogue].
;;====================================================================
	.text

	.if	.ASSEMBLER_VERSION<7000000
	.asg	0,__TI_EABI__
	.endif
	.if	__TI_EABI__
	.asg	bn_mul_add_words,_bn_mul_add_words
	.asg	bn_mul_words,_bn_mul_words
	.asg	bn_sqr_words,_bn_sqr_words
	.asg	bn_add_words,_bn_add_words
	.asg	bn_sub_words,_bn_sub_words
	.asg	bn_div_words,_bn_div_words
	.asg	bn_sqr_comba8,_bn_sqr_comba8
	.asg	bn_mul_comba8,_bn_mul_comba8
	.asg	bn_sqr_comba4,_bn_sqr_comba4
	.asg	bn_mul_comba4,_bn_mul_comba4
	.endif

	.asg	B3,RA
	.asg	A4,ARG0
	.asg	B4,ARG1
	.asg	A6,ARG2
	.asg	B6,ARG3
	.asg	A8,ARG4
	.asg	B8,ARG5
	.asg	A4,RET
	.asg	A15,FP
	.asg	B14,DP
	.asg	B15,SP

	.global	_bn_mul_add_words
_bn_mul_add_words:
	.asmfunc
	MV	ARG2,B0
  [!B0]	BNOP	RA
||[!B0]	MVK	0,RET
   [B0]	MVC	B0,ILC
   [B0]	ZERO	A19		; high part of accumulator
|| [B0]	MV	ARG0,A2
|| [B0]	MV	ARG3,A3
	NOP	3

	SPLOOP	2		; 2*n+10
;;====================================================================
	LDW	*ARG1++,B7	; ap[i]
	NOP	3
	LDW	*ARG0++,A7	; rp[i]
	MPY32U	B7,A3,A17:A16
	NOP	3		; [2,0] in epilogue
	ADDU	A16,A7,A21:A20
	ADDU	A19,A21:A20,A19:A18
||	MV.S	A17,A23
	SPKERNEL 2,1		; leave slot for "return value"
||	STW	A18,*A2++	; rp[i]
||	ADD	A19,A23,A19
;;====================================================================
	BNOP	RA,4
	MV	A19,RET		; return value
	.endasmfunc

	.global	_bn_mul_words
_bn_mul_words:
	.asmfunc
	MV	ARG2,B0
  [!B0]	BNOP	RA
||[!B0]	MVK	0,RET
   [B0]	MVC	B0,ILC
   [B0]	ZERO	A19		; high part of accumulator
	NOP	3

	SPLOOP	2		; 2*n+10
;;====================================================================
	LDW	*ARG1++,A7	; ap[i]
	NOP	4
	MPY32U	A7,ARG3,A17:A16
	NOP	4		; [2,0] in epiloque
	ADDU	A19,A16,A19:A18
||	MV.S	A17,A21
	SPKERNEL 2,1		; leave slot for "return value"
||	STW	A18,*ARG0++	; rp[i]
||	ADD.L	A19,A21,A19
;;====================================================================
	BNOP	RA,4
	MV	A19,RET		; return value
	.endasmfunc

	.global	_bn_sqr_words
_bn_sqr_words:
	.asmfunc
	MV	ARG2,B0
  [!B0]	BNOP	RA
||[!B0]	MVK	0,RET
   [B0]	MVC	B0,ILC
   [B0]	MV	ARG0,B2
|| [B0]	ADD	4,ARG0,ARG0
	NOP	3

	SPLOOP	2		; 2*n+10
;;====================================================================
	LDW	*ARG1++,B7	; ap[i]
	NOP	4
	MPY32U	B7,B7,B1:B0
	NOP	3		; [2,0] in epilogue
	STW	B0,*B2++(8)	; rp[2*i]
	MV	B1,A1
	SPKERNEL 2,0		; fully overlap BNOP RA,5
||	STW	A1,*ARG0++(8)	; rp[2*i+1]
;;====================================================================
	BNOP	RA,5
	.endasmfunc

	.global	_bn_add_words
_bn_add_words:
	.asmfunc
	MV	ARG3,B0
  [!B0]	BNOP	RA
||[!B0]	MVK	0,RET
   [B0]	MVC	B0,ILC
   [B0]	ZERO	A1		; carry flag
|| [B0]	MV	ARG0,A3
	NOP	3

	SPLOOP	2		; 2*n+6
;;====================================================================
	LDW	*ARG2++,A7	; bp[i]
||	LDW	*ARG1++,B7	; ap[i]
	NOP	4
	ADDU	A7,B7,A9:A8
	ADDU	A1,A9:A8,A1:A0
	SPKERNEL 0,0		; fully overlap BNOP RA,5
||	STW	A0,*A3++	; write result
||	MV	A1,RET		; keep carry flag in RET
;;====================================================================
	BNOP	RA,5
	.endasmfunc

	.global	_bn_sub_words
_bn_sub_words:
	.asmfunc
	MV	ARG3,B0
  [!B0]	BNOP	RA
||[!B0]	MVK	0,RET
   [B0]	MVC	B0,ILC
   [B0]	ZERO	A2		; borrow flag
|| [B0]	MV	ARG0,A3
	NOP	3

	SPLOOP	2		; 2*n+6
;;====================================================================
	LDW	*ARG2++,A7	; bp[i]
||	LDW	*ARG1++,B7	; ap[i]
	NOP	4
	SUBU	B7,A7,A1:A0
  [A2]	SUB	A1:A0,1,A1:A0
	SPKERNEL 0,1		; leave slot for "return borrow flag"
||	STW	A0,*A3++	; write result
||	AND	1,A1,A2		; pass on borrow flag
;;====================================================================
	BNOP	RA,4
	AND	1,A1,RET	; return borrow flag
	.endasmfunc

	.global	_bn_div_words
_bn_div_words:
	.asmfunc
	LMBD	1,A6,A0		; leading zero bits in dv
	LMBD	1,A4,A1		; leading zero bits in hi
||	MVK	32,B0
	CMPLTU	A1,A0,A2
||	ADD	A0,B0,B0
  [ A2]	BNOP	RA
||[ A2]	MVK	-1,A4		; return overflow
||[!A2]	MV	A4,A3		; reassign hi
  [!A2]	MV	B4,A4		; reassign lo, will be quotient
||[!A2]	MVC	B0,ILC
  [!A2]	SHL	A6,A0,A6	; normalize dv
||	MVK	1,A1

  [!A2]	CMPLTU	A3,A6,A1	; hi<dv?
||[!A2]	SHL	A4,1,A5:A4	; lo<<1
  [!A1]	SUB	A3,A6,A3	; hi-=dv
||[!A1]	OR	1,A4,A4
  [!A2]	SHRU	A3,31,A1	; upper bit
||[!A2]	ADDAH	A5,A3,A3	; hi<<1|lo>>31

	SPLOOP	3
  [!A1]	CMPLTU	A3,A6,A1	; hi<dv?
||[ A1]	ZERO	A1
||	SHL	A4,1,A5:A4	; lo<<1
  [!A1]	SUB	A3,A6,A3	; hi-=dv
||[!A1]	OR	1,A4,A4		; quotient
	SHRU	A3,31,A1	; upper bit
||	ADDAH	A5,A3,A3	; hi<<1|lo>>31
	SPKERNEL

	BNOP	RA,5
	.endasmfunc

;;====================================================================
;; Not really Comba algorithm, just straightforward NxM... Dedicated
;; fully unrolled real Comba implementations are asymptotically 2x
;; faster, but naturally larger undertaking. Purpose of this exercise
;; was rather to learn to master nested SPLOOPs...
;;====================================================================
	.global	_bn_sqr_comba8
	.global	_bn_mul_comba8
_bn_sqr_comba8:
	MV	ARG1,ARG2
_bn_mul_comba8:
	.asmfunc
	MVK	8,B0		; N, RILC
||	MVK	8,A0		; M, outer loop counter
||	MV	ARG1,A5		; copy ap
||	MV	ARG0,B4		; copy rp
||	ZERO	B19		; high part of accumulator
	MVC	B0,RILC
||	SUB	B0,2,B1		; N-2, initial ILC
||	SUB	B0,1,B2		; const B2=N-1
||	LDW	*A5++,B6	; ap[0]
||	MV	A0,A3		; const A3=M
sploopNxM?:			; for best performance arrange M<=N
   [A0]	SPLOOPD	2		; 2*n+10
||	MVC	B1,ILC
||	ADDAW	B4,B0,B5
||	ZERO	B7
||	LDW	*A5++,A9	; pre-fetch ap[1]
||	ZERO	A1
||	SUB	A0,1,A0
;;====================================================================
;; SPLOOP from bn_mul_add_words, but with flipped A<>B register files.
;; This is because of Advisory 15 from TI publication SPRZ247I.
	LDW	*ARG2++,A7	; bp[i]
	NOP	3
   [A1]	LDW	*B5++,B7	; rp[i]
	MPY32U	A7,B6,B17:B16
	NOP	3
	ADDU	B16,B7,B21:B20
	ADDU	B19,B21:B20,B19:B18
||	MV.S	B17,B23
	SPKERNEL
||	STW	B18,*B4++	; rp[i]
||	ADD.S	B19,B23,B19
;;====================================================================
outer?:				; m*2*(n+1)+10
	SUBAW	ARG2,A3,ARG2	; rewind bp to bp[0]
	SPMASKR
||	CMPGT	A0,1,A2		; done pre-fetching ap[i+1]?
	MVD	A9,B6		; move through .M unit(*)
   [A2]	LDW	*A5++,A9	; pre-fetch ap[i+1]
	SUBAW	B5,B2,B5	; rewind rp to rp[1]
	MVK	1,A1
   [A0]	BNOP.S1	outer?,4
|| [A0]	SUB.L	A0,1,A0
	STW	B19,*B4--[B2]	; rewind rp tp rp[1]
||	ZERO.S	B19		; high part of accumulator
;; end of outer?
	BNOP	RA,5		; return
	.endasmfunc
;; (*)	It should be noted that B6 is used as input to MPY32U in
;;	chronologically next cycle in *preceding* SPLOOP iteration.
;;	Normally such arrangement would require DINT, but at this
;;	point SPLOOP is draining and interrupts are disabled
;;	implicitly.

	.global	_bn_sqr_comba4
	.global	_bn_mul_comba4
_bn_sqr_comba4:
	MV	ARG1,ARG2
_bn_mul_comba4:
	.asmfunc
	.if	0
	BNOP	sploopNxM?,3
	;; Above mentioned m*2*(n+1)+10 does not apply in n=m=4 case,
	;; because of low-counter effect, when prologue phase finishes
	;; before SPKERNEL instruction is reached. As result it's 25%
	;; slower than expected...
	MVK	4,B0		; N, RILC
||	MVK	4,A0		; M, outer loop counter
||	MV	ARG1,A5		; copy ap
||	MV	ARG0,B4		; copy rp
||	ZERO	B19		; high part of accumulator
	MVC	B0,RILC
||	SUB	B0,2,B1		; first ILC
||	SUB	B0,1,B2		; const B2=N-1
||	LDW	*A5++,B6	; ap[0]
||	MV	A0,A3		; const A3=M
	.else
	;; This alternative is an exercise in fully unrolled Comba
	;; algorithm implementation that operates at n*(n+1)+12, or
	;; as little as 32 cycles...
	LDW	*ARG1[0],B16	; a[0]
||	LDW	*ARG2[0],A16	; b[0]
	LDW	*ARG1[1],B17	; a[1]
||	LDW	*ARG2[1],A17	; b[1]
	LDW	*ARG1[2],B18	; a[2]
||	LDW	*ARG2[2],A18	; b[2]
	LDW	*ARG1[3],B19	; a[3]
||	LDW	*ARG2[3],A19	; b[3]
	NOP
	MPY32U	A16,B16,A1:A0	; a[0]*b[0]
	MPY32U	A17,B16,A23:A22	; a[0]*b[1]
	MPY32U	A16,B17,A25:A24	; a[1]*b[0]
	MPY32U	A16,B18,A27:A26	; a[2]*b[0]
	STW	A0,*ARG0[0]
||	MPY32U	A17,B17,A29:A28	; a[1]*b[1]
	MPY32U	A18,B16,A31:A30	; a[0]*b[2]
||	ADDU	A22,A1,A1:A0
	MV	A23,B0
||	MPY32U	A19,B16,A21:A20	; a[3]*b[0]
||	ADDU	A24,A1:A0,A1:A0
	ADDU	A25,B0,B1:B0
||	STW	A0,*ARG0[1]
||	MPY32U	A18,B17,A23:A22	; a[2]*b[1]
||	ADDU	A26,A1,A9:A8
	ADDU	A27,B1,B9:B8
||	MPY32U	A17,B18,A25:A24	; a[1]*b[2]
||	ADDU	A28,A9:A8,A9:A8
	ADDU	A29,B9:B8,B9:B8
||	MPY32U	A16,B19,A27:A26	; a[0]*b[3]
||	ADDU	A30,A9:A8,A9:A8
	ADDU	A31,B9:B8,B9:B8
||	ADDU	B0,A9:A8,A9:A8
	STW	A8,*ARG0[2]
||	ADDU	A20,A9,A1:A0
	ADDU	A21,B9,B1:B0
||	MPY32U	A19,B17,A21:A20	; a[3]*b[1]
||	ADDU	A22,A1:A0,A1:A0
	ADDU	A23,B1:B0,B1:B0
||	MPY32U	A18,B18,A23:A22	; a[2]*b[2]
||	ADDU	A24,A1:A0,A1:A0
	ADDU	A25,B1:B0,B1:B0
||	MPY32U	A17,B19,A25:A24	; a[1]*b[3]
||	ADDU	A26,A1:A0,A1:A0
	ADDU	A27,B1:B0,B1:B0
||	ADDU	B8,A1:A0,A1:A0
	STW	A0,*ARG0[3]
||	MPY32U	A19,B18,A27:A26	; a[3]*b[2]
||	ADDU	A20,A1,A9:A8
	ADDU	A21,B1,B9:B8
||	MPY32U	A18,B19,A29:A28	; a[2]*b[3]
||	ADDU	A22,A9:A8,A9:A8
	ADDU	A23,B9:B8,B9:B8
||	MPY32U	A19,B19,A31:A30	; a[3]*b[3]
||	ADDU	A24,A9:A8,A9:A8
	ADDU	A25,B9:B8,B9:B8
||	ADDU	B0,A9:A8,A9:A8
	STW	A8,*ARG0[4]
||	ADDU	A26,A9,A1:A0
	ADDU	A27,B9,B1:B0
||	ADDU	A28,A1:A0,A1:A0
	ADDU	A29,B1:B0,B1:B0
||	BNOP	RA
||	ADDU	B8,A1:A0,A1:A0
	STW	A0,*ARG0[5]
||	ADDU	A30,A1,A9:A8
	ADD	A31,B1,B8
	ADDU	B0,A9:A8,A9:A8	; removed || to avoid cross-path stall below
	ADD	B8,A9,A9
||	STW	A8,*ARG0[6]
	STW	A9,*ARG0[7]
	.endif
	.endasmfunc
