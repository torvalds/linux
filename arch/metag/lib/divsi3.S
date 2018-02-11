! SPDX-License-Identifier: GPL-2.0
! Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007
!               Imagination Technologies Ltd
!
! Integer divide routines.
!

	.text
	.global ___udivsi3
	.type   ___udivsi3,function
	.align  2
___udivsi3:
!!
!! Since core is signed divide case, just set control variable
!!
	MOV     D1Re0,D0Ar2             ! Au already in A1Ar1, Bu -> D1Re0
	MOV     D0Re0,#0                ! Result is 0
	MOV     D0Ar4,#0                ! Return positive result
	B       $LIDMCUStart
	.size   ___udivsi3,.-___udivsi3

!!
!! 32-bit division signed i/p - passed signed 32-bit numbers
!!
	.global ___divsi3
	.type   ___divsi3,function
	.align  2
___divsi3:
!!
!! A already in D1Ar1, B already in D0Ar2 -> make B abs(B)
!!
	MOV     D1Re0,D0Ar2             ! A already in A1Ar1, B -> D1Re0
	MOV     D0Re0,#0                ! Result is 0
	XOR     D0Ar4,D1Ar1,D1Re0       ! D0Ar4 -ive if result is -ive
	ABS     D1Ar1,D1Ar1             ! abs(A) -> Au
	ABS     D1Re0,D1Re0             ! abs(B) -> Bu
$LIDMCUStart:
	CMP     D1Ar1,D1Re0             ! Is ( Au > Bu )?
	LSR     D1Ar3,D1Ar1,#2          ! Calculate (Au & (~3)) >> 2
	CMPHI   D1Re0,D1Ar3             ! OR ( (Au & (~3)) <= (Bu << 2) )?
	LSLSHI  D1Ar3,D1Re0,#1          ! Buq = Bu << 1
	BLS     $LIDMCUSetup            ! Yes: Do normal divide
!!
!! Quick divide setup can assume that CurBit only needs to start at 2
!!
$LIDMCQuick:
	CMP     D1Ar1,D1Ar3             ! ( A >= Buq )?
	ADDCC   D0Re0,D0Re0,#2          ! If yes result += 2
	SUBCC   D1Ar1,D1Ar1,D1Ar3       !        and A -= Buq
	CMP     D1Ar1,D1Re0             ! ( A >= Bu )?
	ADDCC   D0Re0,D0Re0,#1          ! If yes result += 1
	SUBCC   D1Ar1,D1Ar1,D1Re0       !        and A -= Bu
	ORS     D0Ar4,D0Ar4,D0Ar4       ! Return neg result?
	NEG     D0Ar2,D0Re0             ! Calculate neg result
	MOVMI   D0Re0,D0Ar2             ! Yes: Take neg result
$LIDMCRet:
	MOV     PC,D1RtP
!!
!!  Setup for general unsigned divide code
!!
!!      D0Re0 is used to form the result, already set to Zero
!!      D1Re0 is the input Bu value, this gets trashed
!!      D0Ar6 is curbit which is set to 1 at the start and shifted up
!!      D0Ar4 is negative if we should return a negative result
!!      D1Ar1 is the input Au value, eventually this holds the remainder
!!
$LIDMCUSetup:
	CMP     D1Ar1,D1Re0             ! Is ( Au < Bu )?
	MOV     D0Ar6,#1                ! Set curbit to 1
	BCS     $LIDMCRet               ! Yes: Return 0 remainder Au
!!
!! Calculate alignment using FFB instruction
!!
	FFB     D1Ar5,D1Ar1             ! Find first bit of Au
	ANDN    D1Ar5,D1Ar5,#31         ! Handle exceptional case.
	ORN     D1Ar5,D1Ar5,#31         ! if N bit set, set to 31
	FFB     D1Ar3,D1Re0             ! Find first bit of Bu
	ANDN    D1Ar3,D1Ar3,#31         ! Handle exceptional case.
	ORN     D1Ar3,D1Ar3,#31         ! if N bit set, set to 31
	SUBS    D1Ar3,D1Ar5,D1Ar3       ! calculate diff, ffbA - ffbB
	MOV     D0Ar2,D1Ar3             ! copy into bank 0
	LSLGT   D1Re0,D1Re0,D1Ar3       ! ( > 0) ? left shift B
	LSLGT   D0Ar6,D0Ar6,D0Ar2       ! ( > 0) ? left shift curbit
!!
!! Now we start the divide proper, logic is
!!
!!       if ( A >= B ) add curbit to result and subtract B from A
!!       shift curbit and B down by 1 in either case
!!
$LIDMCLoop:
	CMP     D1Ar1, D1Re0            ! ( A >= B )?
	ADDCC   D0Re0, D0Re0, D0Ar6     ! If yes result += curbit
	SUBCC   D1Ar1, D1Ar1, D1Re0     ! and A -= B
	LSRS    D0Ar6, D0Ar6, #1        ! Shift down curbit, is it zero?
	LSR     D1Re0, D1Re0, #1        ! Shift down B
	BNZ     $LIDMCLoop               ! Was single bit in curbit lost?
	ORS     D0Ar4,D0Ar4,D0Ar4       ! Return neg result?
	NEG     D0Ar2,D0Re0             ! Calculate neg result
	MOVMI   D0Re0,D0Ar2             ! Yes: Take neg result
	MOV     PC,D1RtP
	.size   ___divsi3,.-___divsi3
