/*
 * tbisoft.S
 *
 * Copyright (C) 2001, 2002, 2007, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Support for soft threads and soft context switches
 */

	.file	"tbisoft.S"

#include <asm/tbx.h>

#ifdef METAC_1_0
/* Ax.4 is saved in TBICTX */
#define A0_4  ,A0.4
#define D0_5  ,D0.5
#else
/* Ax.4 is NOT saved in TBICTX */
#define A0_4
#define D0_5
#endif

/* Size of the TBICTX structure */
#define TBICTX_BYTES ((TBICTX_AX_REGS*8)+TBICTX_AX)

	.text
	.balign	4
	.global	___TBISwitchTail
	.type	___TBISwitchTail,function
___TBISwitchTail:
	B	$LSwitchTail
	.size	___TBISwitchTail,.-___TBISwitchTail

/* 
 * TBIRES __TBIJumpX( TBIX64 ArgsA, PTBICTX *rpSaveCtx, int TrigsMask,
 *                                    void (*fnMain)(), void *pStack );
 *
 * This is a combination of __TBISwitch and __TBIJump with the context of
 * the calling thread being saved in the rpSaveCtx location with a drop-thru
 *  effect into the __TBIJump logic. ArgsB passes via __TBIJump to the
 *  routine eventually invoked will reflect the rpSaveCtx value specified.
 */
	.text
	.balign	4
	.global	___TBIJumpX
	.type	___TBIJumpX,function
___TBIJumpX:
	CMP	D1RtP,#-1
	B	$LSwitchStart
	.size	___TBIJumpX,.-___TBIJumpX

/*
 * TBIRES __TBISwitch( TBIRES Switch, PTBICTX *rpSaveCtx )
 *
 * Software syncronous context switch between soft threads, save only the
 * registers which are actually valid on call entry.
 *
 *	A0FrP, D0RtP, D0.5, D0.6, D0.7      - Saved on stack
 *	A1GbP is global to all soft threads so not virtualised
 *	A0StP is then saved as the base of the TBICTX of the thread
 *	
 */
	.text
	.balign	4
	.global	___TBISwitch
	.type	___TBISwitch,function
___TBISwitch:
	XORS	D0Re0,D0Re0,D0Re0		/* Set ZERO flag */
$LSwitchStart:
	MOV	D0FrT,A0FrP			/* Boing entry sequence */
	ADD	A0FrP,A0StP,#0			
	SETL	[A0StP+#8++],D0FrT,D1RtP
/*
 * Save current frame state - we save all regs because we don't want
 * uninitialised crap in the TBICTX structure that the asyncronous resumption
 * of a thread will restore.
 */
	MOVT	D1Re0,#HI($LSwitchExit)		/* ASync resume point here */
	ADD	D1Re0,D1Re0,#LO($LSwitchExit)
	SETD	[D1Ar3],A0StP			/* Record pCtx of this thread */
	MOVT	D0Re0,#TBICTX_SOFT_BIT		/* Only soft thread state */
	SETL	[A0StP++],D0Re0,D1Re0		/* Push header fields */
	ADD	D0FrT,A0StP,#TBICTX_AX-TBICTX_DX /* Address AX save area */
	MOV	D0Re0,#0			/* Setup 0:0 result for ASync */
	MOV	D1Re0,#0			/* resume of the thread */
	MSETL	[A0StP],D0Re0,D0Ar6,D0Ar4,D0Ar2,D0FrT,D0.5,D0.6,D0.7
	SETL	[A0StP++],D0Re0,D1Re0		/* Zero CurrRPT, CurrBPOBITS, */
	SETL	[A0StP++],D0Re0,D1Re0		/* Zero CurrMODE, CurrDIVTIME */
	ADD	A0StP,A0StP,#(TBICTX_AX_REGS*8)	/* Reserve AX save space */
	MSETL	[D0FrT],A0StP,A0FrP,A0.2,A0.3 A0_4 /* Save AX regs */
	BNZ	___TBIJump
/*
 * NextThread MUST be in TBICTX_SOFT_BIT state!
 */
$LSwitchTail:
	MOV	D0Re0,D0Ar2			/* Result from args */
	MOV	D1Re0,D1Ar1
	ADD	D1RtP,D1Ar1,#TBICTX_AX
	MGETL	A0StP,A0FrP,[D1RtP]		/* Get frame values */
$LSwitchCmn:
	ADD	A0.2,D1Ar1,#TBICTX_DX+(8*5)
	MGETL	D0.5,D0.6,D0.7,[A0.2]		/* Get caller-saved DX regs */
$LSwitchExit:
	GETL	D0FrT,D1RtP,[A0FrP++] 		/* Restore state from frame */
	SUB	A0StP,A0FrP,#8			/* Unwind stack */
	MOV	A0FrP,D0FrT			/* Last memory read completes */
	MOV	PC,D1RtP			/* Return to caller */
	.size	___TBISwitch,.-___TBISwitch

/*
 * void __TBISyncResume( TBIRES State, int TrigMask );
 *
 * This routine causes the TBICTX structure specified in State.Sig.pCtx to
 * be restored. This implies that execution will not return to the caller.
 * The State.Sig.TrigMask field will be ored into TXMASKI during the
 * context switch such that any immediately occuring interrupts occur in
 * the context of the newly specified task. The State.Sig.SaveMask parameter
 * is ignored.
 */
	.text
	.balign	4
	.global	___TBISyncResume
	.type	___TBISyncResume,function
___TBISyncResume:
	MOV	D0Re0,D0Ar2			/* Result from args */
	MOV	D1Re0,D1Ar1
	XOR	D1Ar5,D1Ar5,D1Ar5		/* D1Ar5 = 0 */
	ADD	D1RtP,D1Ar1,#TBICTX_AX
	SWAP	D1Ar5,TXMASKI			/* D1Ar5 <-> TXMASKI */
	MGETL	A0StP,A0FrP,[D1RtP]		/* Get frame values */
	OR	TXMASKI,D1Ar5,D1Ar3		/* New TXMASKI */
	B 	$LSwitchCmn
	.size	___TBISyncResume,.-___TBISyncResume

/*
 * void __TBIJump( TBIX64 ArgsA, TBIX32 ArgsB, int TrigsMask,
 *                               void (*fnMain)(), void *pStack );
 *
 * Jump directly to a new routine on an arbitrary stack with arbitrary args
 * oring bits back into TXMASKI on route.
 */
	.text
	.balign	4
	.global	___TBIJump
	.type	___TBIJump,function
___TBIJump:
	XOR	D0Re0,D0Re0,D0Re0		/* D0Re0 = 0 */
	MOV	A0StP,D0Ar6			/* Stack = Frame */
	SWAP	D0Re0,TXMASKI			/* D0Re0 <-> TXMASKI */
	MOV	A0FrP,D0Ar6			
	MOVT	A1LbP,#HI(__exit)
	ADD	A1LbP,A1LbP,#LO(__exit)
	MOV	D1RtP,A1LbP			/* D1RtP = __exit */
	OR	TXMASKI,D0Re0,D0Ar4		/* New TXMASKI */
	MOV	PC,D1Ar5			/* Jump to fnMain */
	.size	___TBIJump,.-___TBIJump

/*
 *	PTBICTX __TBISwitchInit( void *pStack, int (*fnMain)(),
 *                             .... 4 extra 32-bit args .... );
 *                             
 * Generate a new soft thread context ready for it's first outing.
 *
 *	D1Ar1 - Region of memory to be used as the new soft thread stack
 *	D0Ar2 - Main line routine for new soft thread
 *	D1Ar3, D0Ar4, D1Ar5, D0Ar6 - arguments to be passed on stack
 *	The routine returns the initial PTBICTX value for the new thread
 */
	.text
	.balign	4
	.global	___TBISwitchInit
	.type	___TBISwitchInit,function
___TBISwitchInit:
	MOV	D0FrT,A0FrP			/* Need save return point */
	ADD	A0FrP,A0StP,#0
	SETL	[A0StP++],D0FrT,D1RtP		/* Save return to caller */
	MOVT	A1LbP,#HI(__exit)
	ADD	A1LbP,A1LbP,#LO(__exit)
	MOV	D1RtP,A1LbP			/* Get address of __exit */
	ADD	D1Ar1,D1Ar1,#7			/* Align stack to 64-bits */
	ANDMB	D1Ar1,D1Ar1,#0xfff8		/*   by rounding base up */
	MOV	A0.2,D1Ar1			/* A0.2 is new stack */
	MOV	D0FrT,D1Ar1			/* Initial puesdo-frame pointer */
	SETL	[A0.2++],D0FrT,D1RtP		/* Save return to __exit */
	MOV	D1RtP,D0Ar2
	SETL	[A0.2++],D0FrT,D1RtP		/* Save return to fnMain */
	ADD	D0FrT,D0FrT,#8			/* Advance puesdo-frame pointer */
	MSETL	[A0.2],D0Ar6,D0Ar4		/* Save extra initial args */
	MOVT	D1RtP,#HI(___TBIStart)		/* Start up code for new stack */
	ADD	D1RtP,D1RtP,#LO(___TBIStart)
	SETL	[A0.2++],D0FrT,D1RtP		/* Save return to ___TBIStart */
	ADD	D0FrT,D0FrT,#(8*3)		/* Advance puesdo-frame pointer */
	MOV	D0Re0,A0.2			/* Return pCtx for new thread */
	MOV	D1Re0,#0			/* pCtx:0 is default Arg1:Arg2 */
/*
 * Generate initial TBICTX state
 */
	MOVT	D1Ar1,#HI($LSwitchExit)		/* Async restore code */
	ADD	D1Ar1,D1Ar1,#LO($LSwitchExit)
	MOVT	D0Ar2,#TBICTX_SOFT_BIT		/* Only soft thread state */
	ADD	D0Ar6,A0.2,#TBICTX_BYTES	/* New A0StP */
	MOV	D1Ar5,A1GbP			/* Same A1GbP */
	MOV	D0Ar4,D0FrT			/* Initial A0FrP */
	MOV	D1Ar3,A1LbP			/* Same A1LbP */
	SETL	[A0.2++],D0Ar2,D1Ar1		/* Set header fields */
	MSETL	[A0.2],D0Re0,D0Ar6,D0Ar4,D0Ar2,D0FrT,D0.5,D0.6,D0.7
	MOV	D0Ar2,#0			/* Zero values */
	MOV	D1Ar1,#0
	SETL	[A0.2++],D0Ar2,D1Ar1		/* Zero CurrRPT, CurrBPOBITS, */
	SETL	[A0.2++],D0Ar2,D1Ar1		/*      CurrMODE, and pCurrCBuf */
	MSETL	[A0.2],D0Ar6,D0Ar4,D0Ar2,D0FrT D0_5 /* Set DX and then AX regs */
	B	$LSwitchExit			/* All done! */
	.size	___TBISwitchInit,.-___TBISwitchInit

	.text
	.balign	4
	.global	___TBIStart
	.type	___TBIStart,function
___TBIStart:
	MOV	D1Ar1,D1Re0			/* Pass TBIRES args to call */
	MOV	D0Ar2,D0Re0
	MGETL	D0Re0,D0Ar6,D0Ar4,[A0FrP]	/* Get hidden args */
	SUB	A0StP,A0FrP,#(8*3)		/* Entry stack pointer */
	MOV	A0FrP,D0Re0			/* Entry frame pointer */
	MOVT	A1LbP,#HI(__exit)
	ADD	A1LbP,A1LbP,#LO(__exit)
	MOV	D1RtP,A1LbP			/* D1RtP = __exit */
	MOV	PC,D1Re0			/* Jump into fnMain */
	.size	___TBIStart,.-___TBIStart

/*
 * End of tbisoft.S
 */
