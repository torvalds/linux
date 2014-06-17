/*
 * tbiroot.S
 *
 * Copyright (C) 2001, 2002, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Module that creates and via ___TBI function returns a TBI Root Block for
 * interrupt and background processing on the current thread.
 */

	.file	"tbiroot.S"
#include <asm/metag_regs.h>

/*
 * Get data structures and defines from the TBI C header
 */
#include <asm/tbx.h>


/* If signals need to be exchanged we must create a TBI Root Block */

	.data
	.balign	8
	.global	___pTBIs
	.type	___pTBIs,object
___pTBIs:
	.long	0 /* Bgnd+Int root block ptrs */
	.long	0
	.size	___pTBIs,.-___pTBIs


/*
 * Return ___pTBIs value specific to execution level with promotion/demotion
 *
 * Register Usage: D1Ar1 is Id, D0Re0 is the primary result
 *                 D1Re0 is secondary result (___pTBIs for other exec level)
 */
	.text
	.balign	4
	.global	___TBI
	.type	___TBI,function
___TBI:
	TSTT	D1Ar1,#HI(TBID_ISTAT_BIT)	/* Bgnd or Int level? */
	MOVT	A1LbP,#HI(___pTBIs)
	ADD	A1LbP,A1LbP,#LO(___pTBIs)
	GETL	D0Re0,D1Re0,[A1LbP] /* Base of root block table */
	SWAPNZ	D0Re0,D1Re0			/* Swap if asked */
	MOV	PC,D1RtP
	.size	___TBI,.-___TBI


/*
 * Return identifier of the current thread in TBI segment or signal format with
 * secondary mask to indicate privilege and interrupt level of thread
 */
	.text
	.balign	4
	.global	___TBIThrdPrivId
	.type	___TBIThrdPrivId,function
___TBIThrdPrivId:
	.global	___TBIThreadId
	.type	___TBIThreadId,function
___TBIThreadId:
#ifndef METAC_0_1
	MOV	D1Re0,TXSTATUS			/* Are we privileged or int? */
	MOV	D0Re0,TXENABLE			/* Which thread are we? */
/* Disable privilege adaption for now */
	ANDT	D1Re0,D1Re0,#HI(TXSTATUS_ISTAT_BIT) /* +TXSTATUS_PSTAT_BIT) */
	LSL	D1Re0,D1Re0,#TBID_ISTAT_S-TXSTATUS_ISTAT_S
	AND	D0Re0,D0Re0,#TXENABLE_THREAD_BITS
	LSL	D0Re0,D0Re0,#TBID_THREAD_S-TXENABLE_THREAD_S
#else
/* Thread 0 only */
	XOR	D0Re0,D0Re0,D0Re0
	XOR	D1Re0,D1Re0,D1Re0
#endif
	MOV 	PC,D1RtP			/* Return */
	.size	___TBIThrdPrivId,.-___TBIThrdPrivId
	.size	___TBIThreadId,.-___TBIThreadId 


/*
 * End of tbiroot.S
 */
