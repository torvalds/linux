! Copyright (C) 2012 by Imagination Technologies Ltd.
!
! 64-bit multiply routine.
!

!
! 64-bit signed/unsigned multiply
!
! A = D1Ar1:D0Ar2 = a 2^48 + b 2^32 +  c 2^16 + d 2^0
!
! B = D1Ar3:D0Ar4 = w 2^48 + x 2^32 +  y 2^16 + z 2^0
!
	.text
	.global ___muldi3
	.type   ___muldi3,function

___muldi3:
	MULD    D1Re0,D1Ar1,D0Ar4       ! (a 2^48 + b 2^32)(y 2^16 + z 2^0)
	MULD    D0Re0,D0Ar2,D1Ar3       ! (w 2^48 + x 2^32)(c 2^16 + d 2^0)
	ADD     D1Re0,D1Re0,D0Re0

	MULW    D0Re0,D0Ar2,D0Ar4       ! (d 2^0)  * (z 2^0)

	RTDW    D0Ar2,D0Ar2
	MULW    D0Ar6,D0Ar2,D0Ar4       ! (c 2^16)(z 2^0)
	LSR     D1Ar5,D0Ar6,#16
	LSL     D0Ar6,D0Ar6,#16
	ADDS    D0Re0,D0Re0,D0Ar6
	ADDCS   D1Re0,D1Re0,#1
	RTDW    D0Ar4,D0Ar4
	ADD     D1Re0,D1Re0,D1Ar5

	MULW    D0Ar6,D0Ar2,D0Ar4       ! (c 2^16)(y 2^16)
	ADD     D1Re0,D1Re0,D0Ar6

	RTDW    D0Ar2,D0Ar2
	MULW    D0Ar6,D0Ar2,D0Ar4       ! (d 2^0)(y 2^16)
	LSR     D1Ar5,D0Ar6,#16
	LSL     D0Ar6,D0Ar6,#16
	ADDS    D0Re0,D0Re0,D0Ar6
	ADD     D1Re0,D1Re0,D1Ar5
	ADDCS   D1Re0,D1Re0,#1
	MOV     PC, D1RtP
	.size ___muldi3,.-___muldi3
