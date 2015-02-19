/*
 * tbidefr.S
 *
 * Copyright (C) 2009, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Routing deferred exceptions
 */

#include <asm/metag_regs.h>
#include <asm/tbx.h>

	.text
	.balign	4
	.global	___TBIHandleDFR
	.type	___TBIHandleDFR,function
/* D1Ar1:D0Ar2 -- State
 * D0Ar3       -- SigNum
 * D0Ar4       -- Triggers
 * D1Ar5       -- Inst
 * D0Ar6       -- pTBI (volatile)
 */
___TBIHandleDFR:
#ifdef META_BUG_MBN100212
	MSETL	[A0StP++], D0FrT, D0.5

	/* D1Ar1,D0Ar2,D1Ar5,D0Ar6 -- Arguments to handler, must be preserved
	 * D0Ar4       -- The deferred exceptions
	 * D1Ar3       -- As per D0Ar4 but just the trigger bits
	 * D0.5        -- The bgnd deferred exceptions
	 * D1.5        -- TXDEFR with bgnd re-added
	 */

	/* - Collect the pending deferred exceptions using TXSTAT,
	 *   (ack's the bgnd exceptions as a side-effect)
	 * - Manually collect remaining (interrupt) deferred exceptions
	 *   using TXDEFR
	 * - Replace the triggers (from TXSTATI) with the int deferred
	 *   exceptions DEFR ..., TXSTATI would have returned if it was valid
	 *   from bgnd code
	 * - Reconstruct TXDEFR by or'ing bgnd deferred exceptions (except
	 *   the DEFER bit) and the int deferred exceptions. This will be
	 *   restored later
	 */
	DEFR	D0.5,  TXSTAT
	MOV	D1.5,  TXDEFR
	ANDT	D0.5,  D0.5, #HI(0xFFFF0000)
	MOV	D1Ar3, D1.5
	ANDT	D1Ar3, D1Ar3, #HI(0xFFFF0000)
	OR	D0Ar4, D1Ar3, #TXSTAT_DEFER_BIT
	OR	D1.5, D1.5, D0.5

	/* Mask off anything unrelated to the deferred exception triggers */
	ANDT	D1Ar3, D1Ar3, #HI(TXSTAT_BUSERR_BIT | TXSTAT_FPE_BITS)

	/* Can assume that at least one exception happened since this
	 * handler wouldnt have been called otherwise.
	 * 
	 * Replace the signal number and at the same time, prepare
	 * the mask to acknowledge the exception
	 *
	 * D1Re0 -- The bits to acknowledge
	 * D1Ar3 -- The signal number
	 * D1RtP -- Scratch to deal with non-conditional insns
	 */
	MOVT	D1Re0, #HI(TXSTAT_FPE_BITS & ~TXSTAT_FPE_DENORMAL_BIT)
	MOV	D1RtP, #TXSTAT_FPE_INVALID_S
	FFB	D1Ar3, D1Ar3
	CMP	D1Ar3, #TXSTAT_FPE_INVALID_S
	MOVLE	D1Ar3, D1RtP /* Collapse FPE triggers to a single signal */
	MOV	D1RtP, #1
	LSLGT	D1Re0, D1RtP, D1Ar3

	/* Get the handler using the signal number
	 *
	 * D1Ar3 -- The signal number
	 * D0Re0 -- Offset into TBI struct containing handler address
	 * D1Re0 -- Mask of triggers to keep
	 * D1RtP -- Address of handler
	 */
	SUB	D1Ar3, D1Ar3, #(TXSTAT_FPE_INVALID_S - TBID_SIGNUM_FPE)
	LSL	D0Re0, D1Ar3, #2
	XOR	D1Re0, D1Re0, #-1   /* Prepare mask for acknowledge (avoids stall) */
	ADD	D0Re0,D0Re0,#TBI_fnSigs
	GETD	D1RtP, [D0Ar6+D0Re0]

	/* Acknowledge triggers */
	AND	D1.5, D1.5, D1Re0

	/* Restore remaining exceptions
	 * Do this here in case the handler enables nested interrupts
	 *
	 * D1.5 -- TXDEFR with this exception ack'd
	 */
	MOV	TXDEFR, D1.5

	/* Call the handler */
	SWAP	D1RtP, PC

	GETL	D0.5,  D1.5,  [--A0StP]
	GETL	D0FrT, D1RtP, [--A0StP]
	MOV	PC,D1RtP
#else  /* META_BUG_MBN100212 */

	/* D1Ar1,D0Ar2,D1Ar5,D0Ar6 -- Arguments to handler, must be preserved
	 * D0Ar4       -- The deferred exceptions
	 * D1Ar3       -- As per D0Ar4 but just the trigger bits
	 */

	/* - Collect the pending deferred exceptions using TXSTAT,
	 *   (ack's the interrupt exceptions as a side-effect)
	 */
	DEFR	D0Ar4, TXSTATI

	/* Mask off anything unrelated to the deferred exception triggers */
	MOV	D1Ar3, D0Ar4
	ANDT	D1Ar3, D1Ar3, #HI(TXSTAT_BUSERR_BIT | TXSTAT_FPE_BITS)

	/* Can assume that at least one exception happened since this
	 * handler wouldnt have been called otherwise.
	 * 
	 * Replace the signal number and at the same time, prepare
	 * the mask to acknowledge the exception
	 *
	 * The unusual code for 1<<D1Ar3 may need explanation.
	 * Normally this would be done using 'MOV rs,#1' and 'LSL rd,rs,D1Ar3'
	 * but only D1Re0 is available in D1 and no crossunit insns are available
	 * Even worse, there is no conditional 'MOV r,#uimm8'.
	 * Since the CMP proves that D1Ar3 >= 20, we can reuse the bottom 12-bits
	 * of D1Re0 (using 'ORGT r,#1') in the knowledge that the top 20-bits will
	 * be discarded without affecting the result.
	 *
	 * D1Re0 -- The bits to acknowledge
	 * D1Ar3 -- The signal number
	 */
	MOVT	D1Re0, #HI(TXSTAT_FPE_BITS & ~TXSTAT_FPE_DENORMAL_BIT)
	MOV	D0Re0, #TXSTAT_FPE_INVALID_S
	FFB	D1Ar3, D1Ar3
	CMP	D1Ar3, #TXSTAT_FPE_INVALID_S
	MOVLE	D1Ar3, D0Re0 /* Collapse FPE triggers to a single signal */
	ORGT	D1Re0, D1Re0, #1
	LSLGT	D1Re0, D1Re0, D1Ar3

	SUB	D1Ar3, D1Ar3, #(TXSTAT_FPE_INVALID_S - TBID_SIGNUM_FPE)

	/* Acknowledge triggers and restore remaining exceptions
	 * Do this here in case the handler enables nested interrupts
	 *
	 * (x | y) ^ y == x & ~y. It avoids the restrictive XOR ...,#-1 insn
	 * and is the same length
	 */
	MOV	D0Re0, TXDEFR
	OR	D0Re0, D0Re0, D1Re0
	XOR	TXDEFR, D0Re0, D1Re0

	/* Get the handler using the signal number
	 *
	 * D1Ar3 -- The signal number
	 * D0Re0 -- Address of handler
	 */
	LSL	D0Re0, D1Ar3, #2
	ADD	D0Re0,D0Re0,#TBI_fnSigs
	GETD	D0Re0, [D0Ar6+D0Re0]

	/* Tailcall the handler */
	MOV	PC,D0Re0

#endif /* META_BUG_MBN100212 */
	.size	___TBIHandleDFR,.-___TBIHandleDFR
/*
 * End of tbidefr.S
 */
