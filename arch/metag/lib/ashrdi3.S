! Copyright (C) 2012 by Imagination Technologies Ltd.
!
! 64-bit arithmetic shift right routine.
!

	.text
	.global ___ashrdi3
	.type   ___ashrdi3,function

___ashrdi3:
	MOV     D0Re0,D0Ar2
	MOV     D1Re0,D1Ar1
	CMP     D1Ar3,#0                ! COUNT == 0
	MOVEQ   PC,D1RtP                ! Yes, return

	MOV     D0Ar4,D1Ar3
	SUBS    D1Ar3,D1Ar3,#32         ! N = COUNT - 32
	BGE     $L20

!! Shift < 32
	NEG     D1Ar3,D1Ar3             ! N = - N
	LSR     D0Re0,D0Re0,D0Ar4       ! LO = LO >> COUNT
	LSL     D0Ar6,D1Re0,D1Ar3       ! TMP= HI << -(COUNT - 32)
	OR      D0Re0,D0Re0,D0Ar6       ! LO = LO | TMP
	SWAP    D1Ar3,D0Ar4
	ASR     D1Re0,D1Re0,D1Ar3       ! HI = HI >> COUNT
	MOV     PC,D1RtP
$L20:
!! Shift >= 32
	ASR     D0Re0,D1Re0,D1Ar3       ! LO = HI >> N
	ASR     D1Re0,D1Re0,#31         ! HI = HI >> 31
	MOV     PC,D1RtP
	.size ___ashrdi3,.-___ashrdi3
