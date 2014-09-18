/*
 * tbitimer.S
 *
 * Copyright (C) 2001, 2002, 2007, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * TBI timer support routines and data values
 */

	.file	"tbitimer.S"
/*
 * Get data structures and defines from the main C header
 */
#include <asm/tbx.h>

	.data
	.balign	8
	.global	___TBITimeB
	.type	___TBITimeB,object
___TBITimeB:
	.quad	0		/* Background 'lost' ticks */
	.size	___TBITimeB,.-___TBITimeB

	.data
	.balign	8
	.global	___TBITimeI
	.type	___TBITimeI,object
___TBITimeI:
	.quad	0		/* Interrupt 'lost' ticks */
	.size	___TBITimeI,.-___TBITimeI

	.data
	.balign	8
	.global	___TBITimes
	.type	___TBITimes,object
___TBITimes:
	.long	___TBITimeB	/* Table of 'lost' tick values */
	.long	___TBITimeI
	.size	___TBITimes,.-___TBITimes

/*
 * Flag bits for control of ___TBITimeCore
 */
#define TIMER_SET_BIT  1
#define TIMER_ADD_BIT  2

/*
 * Initialise or stop timer support
 *
 * Register Usage: D1Ar1 holds Id, D1Ar2 is initial delay or 0
 *                 D0FrT is used to call ___TBITimeCore
 *                 D0Re0 is used for the result which is TXSTAT_TIMER_BIT
 *                 D0Ar4, D1Ar5, D0Ar6 are all used as scratch
 *		  Other registers are those set by ___TBITimeCore
 *			A0.3 is assumed to point at ___TBITime(I/B)
 */
	.text
	.balign	4
	.global	___TBITimerCtrl
	.type	___TBITimerCtrl,function
___TBITimerCtrl:
	MOV	D1Ar5,#TIMER_SET_BIT		/* Timer SET request */
	MOVT	D0FrT,#HI(___TBITimeCore)	/* Get timer core reg values */
	CALL	D0FrT,#LO(___TBITimeCore)	/* and perform register update */
	NEGS	D0Ar6,D0Ar2			/* Set flags from time-stamp */
	ASR	D1Ar5,D0Ar6,#31			/* Sign extend D0Ar6 into D1Ar5 */
	SETLNZ	[A0.3],D0Ar6,D1Ar5		/* ___TBITime(B/I)=-Start if enable */
	MOV	PC,D1RtP			/* Return */
	.size	___TBITimerCtrl,.-___TBITimerCtrl
	
/*
 * Return ___TBITimeStamp value
 *
 * Register Usage: D1Ar1 holds Id
 *                 D0FrT is used to call ___TBITimeCore
 *                 D0Re0, D1Re0 is used for the result
 *                 D1Ar3, D0Ar4, D1Ar5
 *		  Other registers are those set by ___TBITimeCore
 *			D0Ar6 is assumed to be the timer value read
 *			A0.3 is assumed to point at ___TBITime(I/B)
 */
	.text
	.balign	4
	.global	___TBITimeStamp
	.type	___TBITimeStamp,function
___TBITimeStamp:
	MOV	D1Ar5,#0			/* Timer GET request */
	MOVT	D0FrT,#HI(___TBITimeCore)	/* Get timer core reg values */
	CALL	D0FrT,#LO(___TBITimeCore)	/* with no register update */
	ADDS	D0Re0,D0Ar4,D0Ar6		/* Add current time value */
	ADD	D1Re0,D1Ar3,D1Ar5		/*  to 64-bit signed extend time */
	ADDCS	D1Re0,D1Re0,#1			/* Support borrow too */
	MOV	PC,D1RtP			/* Return */
	.size	___TBITimeStamp,.-___TBITimeStamp

/*
 * Perform ___TBITimerAdd logic
 *
 * Register Usage: D1Ar1 holds Id, D0Ar2 holds value to be added to the timer
 *                 D0Re0 is used for the result - new TIMER value
 *                 D1Ar5, D0Ar6 are used as scratch
 *		  Other registers are those set by ___TBITimeCore
 *			D0Ar6 is assumed to be the timer value read
 *			D0Ar4, D1Ar3 is the current value of ___TBITime(B/I)
 */
	.text
	.balign	4
	.global	___TBITimerAdd
	.type	___TBITimerAdd,function
___TBITimerAdd:
	MOV	D1Ar5,#TIMER_ADD_BIT		/* Timer ADD request */
	MOVT	D0FrT,#HI(___TBITimeCore)	/* Get timer core reg values */
	CALL	D0FrT,#LO(___TBITimeCore)	/* with no register update */
	ADD	D0Re0,D0Ar2,D0Ar6		/* Regenerate new value = result */
	NEG	D0Ar2,D0Ar2			/* Negate delta */
	ASR	D1Re0,D0Ar2,#31			/* Sign extend negated delta */
	ADDS	D0Ar4,D0Ar4,D0Ar2		/* Add time added to ... */
	ADD	D1Ar3,D1Ar3,D1Re0		/* ... real timer ... */
	ADDCS	D1Ar3,D1Ar3,#1			/* ... with carry */
	SETL	[A0.3],D0Ar4,D1Ar3		/* Update ___TBITime(B/I) */
	MOV	PC,D1RtP			/* Return */
	.size	___TBITimerAdd,.-___TBITimerAdd

#ifdef TBI_1_4
/*
 * Perform ___TBITimerDeadline logic
 *    NB: Delays are positive compared to the Wait values which are -ive
 *
 * Register Usage: D1Ar1 holds Id
 *                 D0Ar2 holds Delay requested
 *                 D0Re0 is used for the result - old TIMER Delay value
 *                 D1Ar5, D0Ar6 are used as scratch
 *                 Other registers are those set by ___TBITimeCore
 *                 D0Ar6 is assumed to be the timer value read
 *                 D0Ar4, D1Ar3 is the current value of ___TBITime(B/I)
 *
 */
        .text
        .type   ___TBITimerDeadline,function
        .global ___TBITimerDeadline
        .align  2
___TBITimerDeadline:
	MOV	D1Ar5,#TIMER_SET_BIT		/* Timer SET request */
	MOVT	D0FrT,#HI(___TBITimeCore)	/* Get timer core reg values */
	CALL	D0FrT,#LO(___TBITimeCore)	/* with no register update */
	MOV	D0Re0,D0Ar6			/* Old value read = result */
	SUB	D0Ar2,D0Ar6,D0Ar2		/* Delta from (old - new) */
	ASR	D1Re0,D0Ar2,#31			/* Sign extend delta */
	ADDS	D0Ar4,D0Ar4,D0Ar2		/* Add time added to ... */
	ADD	D1Ar3,D1Ar3,D1Re0		/* ... real timer ... */
	ADDCS	D1Ar3,D1Ar3,#1			/* ... with carry */
	SETL	[A0.3],D0Ar4,D1Ar3		/* Update ___TBITime(B/I) */
	MOV	PC,D1RtP			/* Return */
        .size   ___TBITimerDeadline,.-___TBITimerDeadline
#endif /* TBI_1_4 */

/*
 * Perform core timer access logic
 *
 * Register Usage: D1Ar1 holds Id, D0Ar2 holds input value for SET and
 *                                             input value for ADD
 *                 D1Ar5 controls op as SET or ADD as bit values
 *                 On return D0Ar6, D1Ar5 holds the old 64-bit timer value
 *                 A0.3 is setup to point at ___TBITime(I/B)
 *                 A1.3 is setup to point at ___TBITimes
 *                 D0Ar4, D1Ar3 is setup to value of ___TBITime(I/B)
 */
	.text
	.balign	4
	.global	___TBITimeCore
	.type	___TBITimeCore,function
___TBITimeCore:
#ifndef METAC_0_1
	TSTT	D1Ar1,#HI(TBID_ISTAT_BIT)	/* Interrupt level timer? */
#endif
	MOVT	A1LbP,#HI(___TBITimes)
	ADD	A1LbP,A1LbP,#LO(___TBITimes)
	MOV	A1.3,A1LbP			/* Get ___TBITimes address */
#ifndef METAC_0_1
	BNZ	$LTimeCoreI			/* Yes: Service TXTIMERI! */
#endif
	LSRS	D1Ar5,D1Ar5,#1			/* Carry = SET, Zero = !ADD */
	GETD	A0.3,[A1.3+#0]			/* A0.3 == &___TBITimeB */
	MOV	D0Ar6,TXTIMER			/* Always GET old value */
	MOVCS	TXTIMER,D0Ar2			/* Conditional SET operation */
	ADDNZ	TXTIMER,D0Ar2,D0Ar6		/* Conditional ADD operation */
#ifndef METAC_0_1
	B	$LTimeCoreEnd
$LTimeCoreI:
	LSRS	D1Ar5,D1Ar5,#1			/* Carry = SET, Zero = !ADD */
	GETD	A0.3,[A1.3+#4]			/* A0.3 == &___TBITimeI */
	MOV	D0Ar6,TXTIMERI			/* Always GET old value */
	MOVCS	TXTIMERI,D0Ar2			/* Conditional SET operation */
	ADDNZ	TXTIMERI,D0Ar2,D0Ar6		/* Conditional ADD operation */
$LTimeCoreEnd:
#endif
	ASR	D1Ar5,D0Ar6,#31			/* Sign extend D0Ar6 into D1Ar5 */
	GETL	D0Ar4,D1Ar3,[A0.3]		/* Read ___TBITime(B/I) */
	MOV	PC,D0FrT			/* Return quickly */
	.size	___TBITimeCore,.-___TBITimeCore

/*
 * End of tbitimer.S
 */
