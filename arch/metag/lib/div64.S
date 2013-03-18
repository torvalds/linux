! Copyright (C) 2012 Imagination Technologies Ltd.
!
! Signed/unsigned 64-bit division routines.
!

	.text
	.global _div_u64
	.type   _div_u64,function

_div_u64:
$L1:
	ORS     A0.3,D1Ar3,D0Ar4
	BNE     $L3
$L2:
	MOV     D0Re0,D0Ar2
	MOV     D1Re0,D1Ar1
	MOV     PC,D1RtP
$L3:
	CMP     D1Ar3,D1Ar1
	CMPEQ   D0Ar4,D0Ar2
	MOV     D0Re0,#1
	MOV     D1Re0,#0
	BHS     $L6
$L4:
	ADDS    D0Ar6,D0Ar4,D0Ar4
	ADD     D1Ar5,D1Ar3,D1Ar3
	ADDCS   D1Ar5,D1Ar5,#1
	CMP     D1Ar5,D1Ar3
	CMPEQ   D0Ar6,D0Ar4
	BLO     $L6
$L5:
	MOV     D0Ar4,D0Ar6
	MOV     D1Ar3,D1Ar5
	ADDS    D0Re0,D0Re0,D0Re0
	ADD     D1Re0,D1Re0,D1Re0
	ADDCS   D1Re0,D1Re0,#1
	CMP     D1Ar3,D1Ar1
	CMPEQ   D0Ar4,D0Ar2
	BLO     $L4
$L6:
	ORS     A0.3,D1Re0,D0Re0
	MOV     D0Ar6,#0
	MOV     D1Ar5,D0Ar6
	BEQ     $L10
$L7:
	CMP     D1Ar1,D1Ar3
	CMPEQ   D0Ar2,D0Ar4
	BLO     $L9
$L8:
	ADDS    D0Ar6,D0Ar6,D0Re0
	ADD     D1Ar5,D1Ar5,D1Re0
	ADDCS   D1Ar5,D1Ar5,#1

	SUBS    D0Ar2,D0Ar2,D0Ar4
	SUB     D1Ar1,D1Ar1,D1Ar3
	SUBCS   D1Ar1,D1Ar1,#1
$L9:
	LSL     A0.3,D1Re0,#31
	LSR     D0Re0,D0Re0,#1
	LSR     D1Re0,D1Re0,#1
	OR      D0Re0,D0Re0,A0.3
	LSL     A0.3,D1Ar3,#31
	LSR     D0Ar4,D0Ar4,#1
	LSR     D1Ar3,D1Ar3,#1
	OR      D0Ar4,D0Ar4,A0.3
	ORS     A0.3,D1Re0,D0Re0
	BNE     $L7
$L10:
	MOV     D0Re0,D0Ar6
	MOV     D1Re0,D1Ar5
	MOV     PC,D1RtP
	.size _div_u64,.-_div_u64

	.text
	.global _div_s64
	.type   _div_s64,function
_div_s64:
	MSETL   [A0StP],D0FrT,D0.5
	XOR     D0.5,D0Ar2,D0Ar4
	XOR     D1.5,D1Ar1,D1Ar3
	TSTT    D1Ar1,#HI(0x80000000)
	BZ      $L25

	NEGS    D0Ar2,D0Ar2
	NEG     D1Ar1,D1Ar1
	SUBCS   D1Ar1,D1Ar1,#1
$L25:
	TSTT    D1Ar3,#HI(0x80000000)
	BZ      $L27

	NEGS    D0Ar4,D0Ar4
	NEG     D1Ar3,D1Ar3
	SUBCS   D1Ar3,D1Ar3,#1
$L27:
	CALLR   D1RtP,_div_u64
	TSTT    D1.5,#HI(0x80000000)
	BZ      $L29

	NEGS    D0Re0,D0Re0
	NEG     D1Re0,D1Re0
	SUBCS   D1Re0,D1Re0,#1
$L29:

	GETL    D0FrT,D1RtP,[A0StP+#(-16)]
	GETL    D0.5,D1.5,[A0StP+#(-8)]
	SUB     A0StP,A0StP,#16
	MOV     PC,D1RtP
	.size _div_s64,.-_div_s64
