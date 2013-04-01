/*
 * tbilogf.S
 *
 * Copyright (C) 2001, 2002, 2007, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Defines __TBILogF trap code for debugging messages and __TBICont for debug
 * assert to be implemented on.
 */

	.file	"tbilogf.S"

/*
 * Perform console printf using external debugger or host support
 */
	.text
	.balign	4
	.global	___TBILogF
	.type	___TBILogF,function
___TBILogF:
	MSETL 	[A0StP],D0Ar6,D0Ar4,D0Ar2
	SWITCH 	#0xC10020
	MOV	D0Re0,#0
	SUB 	A0StP,A0StP,#24
	MOV	PC,D1RtP
	.size	___TBILogF,.-___TBILogF

/*
 * Perform wait for continue under control of the debugger
 */
	.text
	.balign	4
	.global	___TBICont
	.type	___TBICont,function
___TBICont:
	MOV 	D0Ar6,#1
	MSETL 	[A0StP],D0Ar6,D0Ar4,D0Ar2
	SWITCH 	#0xC30006 	/* Returns if we are to continue */
	SUB	A0StP,A0StP,#(8*3)
	MOV	PC,D1RtP	/* Return */
	.size	___TBICont,.-___TBICont

/*
 * End of tbilogf.S
 */
