/*
 * tbicore.S
 *
 * Copyright (C) 2001, 2002, 2007, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Core functions needed to support use of the thread binary interface for META
 * processors
 */

	.file	"tbicore.S"
/* Get data structures and defines from the TBI C header */
#include <asm/metag_mem.h>
#include <asm/metag_regs.h>
#include <asm/tbx.h>

	.data
	.balign	8
	.global	___pTBISegs
	.type	___pTBISegs,object
___pTBISegs:
	.quad	0		/* Segment list pointer with it's */
	.size	___pTBISegs,.-___pTBISegs
					/* own id or spin-lock location */
/*
 * Return ___pTBISegs value specific to privilege level - not very complicated
 * at the moment
 *
 * Register Usage: D0Re0 is the result, D1Re0 is used as a scratch
 */
	.text
	.balign	4
	.global	___TBISegList
	.type	___TBISegList,function
___TBISegList:
	MOVT	A1LbP,#HI(___pTBISegs)
	ADD	A1LbP,A1LbP,#LO(___pTBISegs)
	GETL	D0Re0,D1Re0,[A1LbP]
	MOV	PC,D1RtP
	.size	___TBISegList,.-___TBISegList

/*
 * Search the segment list for a match given Id, pStart can be NULL
 *
 * Register Usage: D1Ar1 is pSeg, D0Ar2 is Id, D0Re0 is the result
 *                 D0Ar4, D1Ar3 are used as a scratch
 *                 NB: The PSTAT bit if Id in D0Ar2 may be toggled
 */
	.text
	.balign	4
	.global	___TBIFindSeg
	.type	___TBIFindSeg,function
___TBIFindSeg:
	MOVT	A1LbP,#HI(___pTBISegs)
	ADD	A1LbP,A1LbP,#LO(___pTBISegs)
	GETL	D1Ar3,D0Ar4,[A1LbP]	/* Read segment list head */
	MOV	D0Re0,TXSTATUS		/* What priv level are we at? */
	CMP	D1Ar1,#0		/* Is pStart provided? */
/* Disable privilege adaption for now */
	ANDT	D0Re0,D0Re0,#0	/*HI(TXSTATUS_PSTAT_BIT)  ; Is PSTAT set? Zero if not */
	LSL	D0Re0,D0Re0,#(TBID_PSTAT_S-TXSTATUS_PSTAT_S)
	XOR	D0Ar2,D0Ar2,D0Re0	/* Toggle Id PSTAT if privileged */
	MOVNZ	D1Ar3,D1Ar1		/* Use pStart if provided */
$LFindSegLoop:			
	ADDS	D0Re0,D1Ar3,#0		/* End of list? Load result into D0Re0 */
	MOVZ	PC,D1RtP		/* If result is NULL we leave */
	GETL	D1Ar3,D0Ar4,[D1Ar3]	/* Read pLink and Id */
	CMP	D0Ar4,D0Ar2		/* Does it match? */
	BNZ	$LFindSegLoop		/* Loop if there is no match */
	TST	D0Re0,D0Re0		/* Clear zero flag - we found it! */
	MOV	PC,D1RtP		/* Return */
	.size	___TBIFindSeg,.-___TBIFindSeg

/* Useful offsets to encode the lower bits of the lock/unlock addresses */
#define UON  (LINSYSEVENT_WR_ATOMIC_LOCK   & 0xFFF8)
#define UOFF (LINSYSEVENT_WR_ATOMIC_UNLOCK & 0xFFF8)

/*
 * Perform a whole spin-lock sequence as used by the TBISignal routine
 *
 * Register Usage: D1Ar1 is pLock, D0Ar2 is Mask, D0Re0 is the result
 *                 (All other usage due to ___TBIPoll - D0Ar6, D1Re0)
 */
	.text
	.balign	4
	.global	___TBISpin
	.type	___TBISpin,function
___TBISpin:
	SETL	[A0StP++],D0FrT,D1RtP	/* Save our return address */
	ORS	D0Re0,D0Re0,#1		/* Clear zero flag */
	MOV	D1RtP,PC		/* Setup return address to form loop */
$LSpinLoop:
	BNZ	___TBIPoll		/* Keep repeating if fail to set */
	GETL	D0FrT,D1RtP,[--A0StP]	/* Restore return address */
	MOV	PC,D1RtP		/* Return */
	.size	___TBISpin,.-___TBISpin

/*
 * Perform an attempt to gain access to a spin-lock and set some bits
 * 
 * Register Usage: D1Ar1 is pLock, D0Ar2 is Mask, D0Re0 is the result
 *                 !!On return Zero flag is SET if we are sucessfull!!
 *                 A0.3 is used to hold base address of system event region
 *                 D1Re0 use to hold TXMASKI while interrupts are off
 */
	.text
	.balign	4
	.global	___TBIPoll
	.type	___TBIPoll,function
___TBIPoll:
	MOV	D1Re0,#0		/* Prepare to disable ints */
	MOVT	A0.3,#HI(LINSYSEVENT_WR_ATOMIC_LOCK)
	SWAP	D1Re0,TXMASKI		/* Really stop ints */
	LOCK2				/* Gain all locks */
	SET	[A0.3+#UON],D1RtP	/* Stop shared memory access too */
	DCACHE	[D1Ar1],A0.3		/* Flush Cache line */
	GETD	D0Re0,[D1Ar1]		/* Get new state from memory or hit */
	DCACHE	[D1Ar1],A0.3		/* Flush Cache line */
	GETD	D0Re0,[D1Ar1]		/* Get current state */
	TST	D0Re0,D0Ar2		/* Are we clear to send? */
	ORZ	D0Re0,D0Re0,D0Ar2	/* Yes: So set bits and */
	SETDZ	[D1Ar1],D0Re0		/*      transmit new state */
	SET	[A0.3+#UOFF],D1RtP	/* Allow shared memory access */
	LOCK0				/* Release all locks */
	MOV	TXMASKI,D1Re0		/* Allow ints */
$LPollEnd:
	XORNZ	D0Re0,D0Re0,D0Re0	/* No: Generate zero result */
	MOV	PC,D1RtP		/* Return (NZ indicates failure) */
	.size	___TBIPoll,.-___TBIPoll

/*
 * End of tbicore.S
 */
