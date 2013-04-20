! Copyright (C) 2012 by Imagination Technologies Ltd.
!
! 64-bit signed compare routine.
!

	.text
	.global ___cmpdi2
	.type   ___cmpdi2,function

!         low    high
! s64 a  (D0Ar2, D1Ar1)
! s64 b  (D0Ar4, D1Ar3)
___cmpdi2:
	! start at 1 (equal) and conditionally increment or decrement
	MOV	D0Re0,#1

	! high words differ?
	CMP	D1Ar1,D1Ar3
	BNE	$Lhigh_differ

	! unsigned compare low words
	CMP	D0Ar2,D0Ar4
	SUBLO	D0Re0,D0Re0,#1
	ADDHI	D0Re0,D0Re0,#1
	MOV	PC,D1RtP

$Lhigh_differ:
	! signed compare high words
	SUBLT	D0Re0,D0Re0,#1
	ADDGT	D0Re0,D0Re0,#1
	MOV	PC,D1RtP
	.size ___cmpdi2,.-___cmpdi2
