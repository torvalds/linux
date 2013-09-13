! Copyright (C) 2012 by Imagination Technologies Ltd.
!
! 64-bit unsigned compare routine.
!

	.text
	.global ___ucmpdi2
	.type   ___ucmpdi2,function

!         low    high
! u64 a  (D0Ar2, D1Ar1)
! u64 b  (D0Ar4, D1Ar3)
___ucmpdi2:
	! start at 1 (equal) and conditionally increment or decrement
	MOV	D0Re0,#1

	! high words
	CMP	D1Ar1,D1Ar3
	! or if equal, low words
	CMPEQ	D0Ar2,D0Ar4

	! unsigned compare
	SUBLO	D0Re0,D0Re0,#1
	ADDHI	D0Re0,D0Re0,#1

	MOV	PC,D1RtP
	.size ___ucmpdi2,.-___ucmpdi2
