! Copyright (C) 2012 by Imagination Technologies Ltd.
!
! 64-bit arithmetic shift left routine.
!

	.text
	.global ___ashldi3
	.type   ___ashldi3,function

___ashldi3:
	MOV     D0Re0,D0Ar2
	MOV     D1Re0,D1Ar1
	CMP     D1Ar3,#0                ! COUNT == 0
	MOVEQ   PC,D1RtP                ! Yes, return

	SUBS    D0Ar4,D1Ar3,#32         ! N = COUNT - 32
	BGE     $L10

!! Shift < 32
	NEG     D0Ar4,D0Ar4             ! N = - N
	LSL     D1Re0,D1Re0,D1Ar3       ! HI = HI << COUNT
	LSR     D0Ar6,D0Re0,D0Ar4       ! TMP= LO >> -(COUNT - 32)
	OR      D1Re0,D1Re0,D0Ar6       ! HI = HI | TMP
	SWAP    D0Ar4,D1Ar3
	LSL     D0Re0,D0Re0,D0Ar4       ! LO = LO << COUNT
	MOV     PC,D1RtP

$L10:
!! Shift >= 32
	LSL     D1Re0,D0Re0,D0Ar4       ! HI = LO << N
	MOV     D0Re0,#0                ! LO = 0
	MOV     PC,D1RtP
	.size ___ashldi3,.-___ashldi3
