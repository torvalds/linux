! SPDX-License-Identifier: GPL-2.0
!   Copyright (C) 2008-2012 Imagination Technologies Ltd.

	.text
	.global	_memset
	.type	_memset,function
! D1Ar1 dst
! D0Ar2 c
! D1Ar3 cnt
! D0Re0 dst
_memset:
	AND	D0Ar2,D0Ar2,#0xFF	! Ensure a byte input value
	MULW 	D0Ar2,D0Ar2,#0x0101	! Duplicate byte value into  0-15
	ANDS	D0Ar4,D1Ar1,#7		! Extract bottom LSBs of dst
	LSL 	D0Re0,D0Ar2,#16		! Duplicate byte value into 16-31
	ADD	A0.2,D0Ar2,D0Re0	! Duplicate byte value into 4 (A0.2)
	MOV	D0Re0,D1Ar1		! Return dst
	BZ	$LLongStub		! if start address is aligned
	! start address is not aligned on an 8 byte boundary, so we
	! need the number of bytes up to the next 8 byte address
	! boundary, or the length of the string if less than 8, in D1Ar5
	MOV	D0Ar2,#8		! Need 8 - N in D1Ar5 ...
	SUB	D1Ar5,D0Ar2,D0Ar4	!            ... subtract N
	CMP	D1Ar3,D1Ar5
	MOVMI	D1Ar5,D1Ar3
	B	$LByteStub		! dst is mis-aligned, do $LByteStub

!
! Preamble to LongLoop which generates 4*8 bytes per interation (5 cycles)
!
$LLongStub:
	LSRS	D0Ar2,D1Ar3,#5
	AND	D1Ar3,D1Ar3,#0x1F
	MOV	A1.2,A0.2
	BEQ	$LLongishStub
	SUB	TXRPT,D0Ar2,#1
	CMP	D1Ar3,#0
$LLongLoop:
	SETL 	[D1Ar1++],A0.2,A1.2
	SETL 	[D1Ar1++],A0.2,A1.2
	SETL 	[D1Ar1++],A0.2,A1.2
	SETL 	[D1Ar1++],A0.2,A1.2
	BR	$LLongLoop
	BZ	$Lexit
!
! Preamble to LongishLoop which generates 1*8 bytes per interation (2 cycles)
!
$LLongishStub:
	LSRS	D0Ar2,D1Ar3,#3
	AND	D1Ar3,D1Ar3,#0x7
	MOV	D1Ar5,D1Ar3
	BEQ	$LByteStub
	SUB	TXRPT,D0Ar2,#1
	CMP	D1Ar3,#0
$LLongishLoop:
	SETL 	[D1Ar1++],A0.2,A1.2
	BR	$LLongishLoop
	BZ	$Lexit
!
! This does a byte structured burst of up to 7 bytes
!
!	D1Ar1 should point to the location required
!	D1Ar3 should be the remaining total byte count
!	D1Ar5 should be burst size (<= D1Ar3)
!
$LByteStub:
	SUBS	D1Ar3,D1Ar3,D1Ar5	! Reduce count
	ADD	D1Ar1,D1Ar1,D1Ar5	! Advance pointer to end of area
	MULW	D1Ar5,D1Ar5,#4		! Scale to (1*4), (2*4), (3*4)
	SUB	D1Ar5,D1Ar5,#(8*4)	! Rebase to -(7*4), -(6*4), -(5*4), ...
	MOV	A1.2,D1Ar5
	SUB	PC,CPC1,A1.2		! Jump into table below
	SETB 	[D1Ar1+#(-7)],A0.2
	SETB 	[D1Ar1+#(-6)],A0.2
	SETB 	[D1Ar1+#(-5)],A0.2
	SETB 	[D1Ar1+#(-4)],A0.2
	SETB 	[D1Ar1+#(-3)],A0.2
	SETB 	[D1Ar1+#(-2)],A0.2
	SETB 	[D1Ar1+#(-1)],A0.2
!
! Return if all data has been output, otherwise do $LLongStub
!
	BNZ	$LLongStub
$Lexit:
	MOV	PC,D1RtP
        .size _memset,.-_memset

