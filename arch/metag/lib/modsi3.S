! SPDX-License-Identifier: GPL-2.0
! Copyright (C) 2001, 2002, 2003, 2004, 2005, 2006, 2007
!               Imagination Technologies Ltd
!
! Integer modulus routines.
!
!!
!! 32-bit modulus unsigned i/p - passed unsigned 32-bit numbers
!!
	.text
	.global ___umodsi3
	.type   ___umodsi3,function
	.align  2
___umodsi3:
	MOV     D0FrT,D1RtP             ! Save original return address
	CALLR   D1RtP,___udivsi3
	MOV     D1RtP,D0FrT             ! Recover return address
	MOV     D0Re0,D1Ar1             ! Return remainder
	MOV     PC,D1RtP
	.size   ___umodsi3,.-___umodsi3

!!
!! 32-bit modulus signed i/p - passed signed 32-bit numbers
!!
	.global ___modsi3
	.type   ___modsi3,function
	.align  2
___modsi3:
	MOV     D0FrT,D1RtP             ! Save original return address
	MOV     A0.2,D1Ar1              ! Save A in A0.2
	CALLR   D1RtP,___divsi3
	MOV     D1RtP,D0FrT             ! Recover return address
	MOV     D1Re0,A0.2              ! Recover A
	MOV     D0Re0,D1Ar1             ! Return remainder
	ORS     D1Re0,D1Re0,D1Re0       ! Was A negative?
	NEG     D1Ar1,D1Ar1             ! Negate remainder
	MOVMI   D0Re0,D1Ar1             ! Return neg remainder
	MOV     PC, D1RtP
	.size   ___modsi3,.-___modsi3
