/*
 * tbipcx.S
 *
 * Copyright (C) 2001, 2002, 2007, 2009, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Asyncronous trigger handling including exceptions
 */

	.file	"tbipcx.S"
#include <asm/metag_regs.h>
#include <asm/tbx.h>

/* BEGIN HACK */
/* define these for now while doing inital conversion to GAS 
   will fix properly later */

/* Signal identifiers always have the TBID_SIGNAL_BIT set and contain the
   following related bit-fields */
#define TBID_SIGNUM_S       2

/* END HACK */

#ifdef METAC_1_0
/* Ax.4 is saved in TBICTX */
#define A0_4  ,A0.4
#else
/* Ax.4 is NOT saved in TBICTX */
#define A0_4
#endif

/* Size of the TBICTX structure */
#define TBICTX_BYTES ((TBICTX_AX_REGS*8)+TBICTX_AX)

#ifdef METAC_1_1
#ifndef BOOTROM
#ifndef SPECIAL_BUILD
/* Jump straight into the boot ROM version of this code */
#define CODE_USES_BOOTROM
#endif
#endif
#endif

/* Define space needed for CATCH buffer state in traditional units */
#define CATCH_ENTRIES      5
#define CATCH_ENTRY_BYTES 16

#ifndef CODE_USES_BOOTROM
#define A0GblIStP	A0.15  /* PTBICTX for current thread in PRIV system */
#define A1GblIGbP 	A1.15  /* Interrupt A1GbP value in PRIV system */
#endif

/*
 * TBIRES __TBIASyncTrigger( TBIRES State )
 */
	.text
	.balign	4
	.global	___TBIASyncTrigger
	.type	___TBIASyncTrigger,function
___TBIASyncTrigger:
#ifdef CODE_USES_BOOTROM
	MOVT	D0Re0,#HI(LINCORE_BASE)
	JUMP	D0Re0,#0xA0
#else
	MOV	D0FrT,A0FrP			/* Boing entry sequence */
	ADD	A0FrP,A0StP,#0
	SETL	[A0StP++],D0FrT,D1RtP
	MOV	D0Re0,PCX			/* Check for repeat call */
	MOVT	D0FrT,#HI(___TBIBoingRTI+4)
	ADD	D0FrT,D0FrT,#LO(___TBIBoingRTI+4)
	CMP	D0Re0,D0FrT
	BEQ	___TBIBoingExit			/* Already set up - come out */
	ADD	D1Ar1,D1Ar1,#7			/* PRIV system stack here */
	MOV	A0.2,A0StP			/*  else push context here */
	MOVS	D0Re0,D0Ar2			/* Return in user mode? */
	ANDMB	D1Ar1,D1Ar1,#0xfff8		/*  align priv stack to 64-bit */
	MOV	D1Re0,D1Ar1			/*   and set result to arg */
	MOVMI	A0.2,D1Ar1			/*  use priv stack if PRIV set			 */
/*
 * Generate an initial TBICTX to return to our own current call context
 */
	MOVT	D1Ar5,#HI(___TBIBoingExit)	/* Go here to return */
	ADD	D1Ar5,D1Ar5,#LO(___TBIBoingExit)
	ADD	A0.3,A0.2,#TBICTX_DX		/* DX Save area */
	ANDT	D0Ar2,D0Ar2,#TBICTX_PRIV_BIT	/* Extract PRIV bit */
	MOVT	D0Ar6,#TBICTX_SOFT_BIT		/* Only soft thread state */
	ADD	D0Ar6,D0Ar6,D0Ar2		/* Add in PRIV bit if requested */
	SETL	[A0.2],D0Ar6,D1Ar5		/* Push header fields */
	ADD	D0FrT,A0.2,#TBICTX_AX		/* Address AX save area */
	MSETL	[A0.3],D0Re0,D0Ar6,D0Ar4,D0Ar2,D0FrT,D0.5,D0.6,D0.7
	MOV	D0Ar6,#0
	MOV	D1Ar5,#0
	SETL	[A0.3++],D0Ar6,D1Ar5		/* Zero CT register states */
	SETL	[A0.3++],D0Ar6,D1Ar5
	MSETL	[D0FrT],A0StP,A0FrP,A0.2,A0.3 A0_4 /* Save AX regs */
	MOV	A0FrP,A0.2			/* Restore me! */
	B	___TBIResume
	.size	___TBIASyncTrigger,.-___TBIASyncTrigger

/*
 * Optimised return to handler for META Core
 */
___TBIBoingRTH:
	RTH					/* Go to background level */
	MOVT	A0.2,     #HI($Lpcx_target)
	ADD	A0.2,A0.2,#LO($Lpcx_target)
	MOV	PCX,A0.2                        /* Setup PCX for interrupts */
	MOV	PC,D1Re0			/* Jump to handler */
/*
 * This is where the code below needs to jump to wait for outermost interrupt
 * event in a non-privilege mode system (single shared interrupt stack).
 */
___TBIBoingPCX:
	MGETL	A0StP,A0FrP,A0.2,A0.3 A0_4,[D1Re0] /* Restore AX regs */
	MOV	TXSTATUS,D0Re0			/* Restore flags */
	GETL	D0Re0,D1Re0,[D1Re0+#TBICTX_DX-TBICTX_BYTES]
___TBIBoingRTI:
	RTI					/* Wait for interrupt */
$Lpcx_target:
/*
 * Save initial interrupt state on current stack
 */
	SETL	[A0StP+#TBICTX_DX],D0Re0,D1Re0	/* Save key registers */
	ADD	D1Re0,A0StP,#TBICTX_AX		/* Address AX save area */
	MOV	D0Re0,TXSTATUS			/* Read TXSTATUS into D0Re0 */
	MOV	TXSTATUS,#0			/* Clear TXSTATUS */
	MSETL	[D1Re0],A0StP,A0FrP,A0.2,A0.3 A0_4 /* Save AX critical regs */
/*
 * Register state at this point is-
 *
 *	D0Re0 - Old TXSTATUS with PRIV and CBUF bits set if appropriate
 *	A0StP - Is call stack frame and base of TBICTX being generated
 *	A1GbP - Is valid static access link
 */
___TBIBoing:
	LOCK0					/* Make sure we have no locks! */
	ADD	A1.2,A0StP,#TBICTX_DX+(8*1)	/* Address DX.1 save area */
	MOV	A0FrP,A0StP			/* Setup frame pointer */
	MSETL	[A1.2],D0Ar6,D0Ar4,D0Ar2,D0FrT,D0.5,D0.6,D0.7
	MOV	D0Ar4,TXRPT			/* Save critical CT regs */
	MOV	D1Ar3,TXBPOBITS
	MOV	D1Ar1,TXDIVTIME			/* Calc catch buffer pSrc */
	MOV	D0Ar2,TXMODE
	MOV	TXMODE,#0			/* Clear TXMODE */
#ifdef TXDIVTIME_RPDIRTY_BIT
	TSTT	D1Ar1,#HI(TXDIVTIME_RPDIRTY_BIT)/* NZ = RPDIRTY */
	MOVT	D0Ar6,#TBICTX_CBRP_BIT
	ORNZ	D0Re0,D0Re0,D0Ar6		/* Set CBRP if RPDIRTY set */
#endif
	MSETL	[A1.2],D0Ar4,D0Ar2		/* Save CT regs state */
	MOV	D0Ar2,D0Re0			/* Copy TXSTATUS */
	ANDMT	D0Ar2,D0Ar2,#TBICTX_CBUF_BIT+TBICTX_CBRP_BIT
#ifdef TBI_1_4
	MOVT	D1Ar1,#TBICTX_FPAC_BIT		/* Copy FPActive into FPAC */
	TSTT	D0Re0,#HI(TXSTATUS_FPACTIVE_BIT)
	ORNZ	D0Ar2,D0Ar2,D1Ar1
#endif
	MOV	D1Ar1,PCX			/* Read CurrPC */
	ORT	D0Ar2,D0Ar2,#TBICTX_CRIT_BIT	/* SaveMask + CRIT bit */
	SETL	[A0FrP+#TBICTX_Flags],D0Ar2,D1Ar1 /* Set pCtx header fields */
/*
 * Completed context save, now we need to make a call to an interrupt handler
 *
 *	D0Re0 - holds PRIV, WAIT, CBUF flags, HALT reason if appropriate
 *	A0FrP - interrupt stack frame and base of TBICTX being generated
 *	A0StP - same as A0FrP
 */
___TBIBoingWait:
				/* Reserve space for TBICTX and CBUF */
	ADD	A0StP,A0StP,#TBICTX_BYTES+(CATCH_ENTRY_BYTES*CATCH_ENTRIES)
	MOV	D0Ar4,TXSTATI			/* Read the Triggers data */
	MOV	D1Ar3,TXDIVTIME			/* Read IRQEnc bits */
	MOV	D0Ar2,D0Re0			/* Copy PRIV and WAIT flags */
	ANDT	D0Ar2,D0Ar2,#TBICTX_PRIV_BIT+TBICTX_WAIT_BIT+TBICTX_CBUF_BIT
#ifdef TBI_1_4
	MOVT	D1Ar5,#TBICTX_FPAC_BIT		/* Copy FPActive into FPAC */
	TSTT	D0Re0,#HI(TXSTATUS_FPACTIVE_BIT)
	ORNZ	D0Ar2,D0Ar2,D1Ar5
#endif
	ANDT	D1Ar3,D1Ar3,#HI(TXDIVTIME_IRQENC_BITS)
	LSR	D1Ar3,D1Ar3,#TXDIVTIME_IRQENC_S
	AND	TXSTATI,D0Ar4,#TXSTATI_BGNDHALT_BIT/* Ack any HALT seen */
	ANDS	D0Ar4,D0Ar4,#0xFFFF-TXSTATI_BGNDHALT_BIT /* Only seen HALT? */
	ORT	D0Ar2,D0Ar2,#TBICTX_CRIT_BIT	/* Set CRIT */
#ifndef BOOTROM
	MOVT	A1LbP,#HI(___pTBIs)
	ADD	A1LbP,A1LbP,#LO(___pTBIs)
	GETL	D1Ar5,D0Ar6,[A1LbP]		/* D0Ar6 = ___pTBIs[1] */
#else
/*
 * For BOOTROM support ___pTBIs must be allocated at offset 0 vs A1GbP
 */
	GETL	D1Ar5,D0Ar6,[A1GbP]			/* D0Ar6 = ___pTBIs[1] */
#endif
	BZ	___TBIBoingHalt			/* Yes: Service HALT */
/*
 * Encode interrupt as signal vector, strip away same/lower TXMASKI bits
 */
	MOV	D1Ar1,#1			/* Generate mask for this bit */
	MOV	D0Re0,TXMASKI			/* Get interrupt mask */
	LSL	TXSTATI,D1Ar1,D1Ar3		/* Acknowledge trigger */
	AND	TXMASKI,D0Re0,#TXSTATI_BGNDHALT_BIT	/* Only allow HALTs */
	OR	D0Ar2,D0Ar2,D0Re0		/* Set TBIRES.Sig.TrigMask */
	ADD	D1Ar3,D1Ar3,#TBID_SIGNUM_TRT	/* Offset into interrupt sigs */
	LSL	D0Re0,D1Ar3,#TBID_SIGNUM_S	/* Generate offset from SigNum */
/*
 * This is a key moment we are about to call the handler, register state is
 * as follows-
 *
 *	D0Re0 - Handler vector (SigNum<<TBID_SIGNUM_S)
 *	D0Ar2 - TXMASKI:TBICTX_CRIT_BIT with optional CBUF and PRIV bits
 *	D1Ar3 - SigNum
 *	D0Ar4 - State read from TXSTATI
 *	D1Ar5 - Inst for SWITCH trigger case only, otherwise undefined
 *	D0Ar6 - pTBI
 */
___TBIBoingVec:
	ADD	D0Re0,D0Re0,#TBI_fnSigs		/* Offset into signal table */
	GETD	D1Re0,[D0Ar6+D0Re0]		/* Get address for Handler */
/*
 * Call handler at interrupt level, when it returns simply resume execution
 * of state indicated by D1Re0.
 */
	MOV	D1Ar1,A0FrP			/* Pass in pCtx */
	CALLR	D1RtP,___TBIBoingRTH		/* Use RTH to invoke handler */
	
/*
 * Perform critical state restore and execute background thread.
 *
 *	A0FrP - is pointer to TBICTX structure to resume
 *	D0Re0 - contains additional TXMASKI triggers
 */
	.text
	.balign	4
#ifdef BOOTROM
	.global	___TBIResume
#endif
___TBIResume:
/*
 * New META IP method
 */
	RTH					/* Go to interrupt level */
	MOV	D0Ar4,TXMASKI			/* Read TXMASKI */
	OR	TXMASKI,D0Ar4,D0Re0		/* -Write-Modify TXMASKI */
	GETL	D0Re0,D1Re0,[A0FrP+#TBICTX_Flags]/* Get Flags:SaveMask, CurrPC */
	MOV	A0StP,A0FrP			/* Position stack pointer */
	MOV	D0Ar2,TXPOLLI			/* Read pending triggers */
	MOV	PCX,D1Re0			/* Set resumption PC */
	TST	D0Ar2,#0xFFFF			/* Any pending triggers? */
	BNZ	___TBIBoingWait			/* Yes: Go for triggers */
	TSTT	D0Re0,#TBICTX_WAIT_BIT		/* Do we WAIT anyway? */
	BNZ	___TBIBoingWait			/* Yes: Go for triggers */
	LSLS	D1Ar5,D0Re0,#1			/* Test XCBF (MI) & PRIV (CS)? */
	ADD	D1Re0,A0FrP,#TBICTX_CurrRPT	/* Address CT save area */
	ADD	A0StP,A0FrP,#TBICTX_DX+(8*1)	/* Address DX.1 save area */
	MGETL	A0.2,A0.3,[D1Re0]		/* Get CT reg states */
	MOV	D1Ar3,A1.3			/* Copy old TXDIVTIME */
	BPL	___TBIResCrit			/* No: Skip logic */
	ADD	D0Ar4,A0FrP,#TBICTX_BYTES	/* Source is after TBICTX */
	ANDST	D1Ar3,D1Ar3,#HI(TXDIVTIME_RPMASK_BITS)/* !Z if RPDIRTY */
	MGETL	D0.5,D0.6,[D0Ar4]		/* Read Catch state */
	MOV	TXCATCH0,D0.5			/* Restore TXCATCHn */
	MOV	TXCATCH1,D1.5
	MOV	TXCATCH2,D0.6
	MOV	TXCATCH3,D1.6
	BZ	___TBIResCrit
	MOV	D0Ar2,#(1*8)
	LSRS	D1Ar3,D1Ar3,#TXDIVTIME_RPMASK_S+1 /* 2nd RPMASK bit -> bit 0 */
	ADD	RA,D0Ar4,#(0*8)			/* Re-read read pipeline */
	ADDNZ	RA,D0Ar4,D0Ar2			/* If Bit 0 set issue RA */
	LSRS	D1Ar3,D1Ar3,#2			/* Bit 1 -> C, Bit 2 -> Bit 0 */
	ADD	D0Ar2,D0Ar2,#8
	ADDCS	RA,D0Ar4,D0Ar2			/* If C issue RA */
	ADD	D0Ar2,D0Ar2,#8
	ADDNZ	RA,D0Ar4,D0Ar2			/* If Bit 0 set issue RA */
	LSRS	D1Ar3,D1Ar3,#2			/* Bit 1 -> C, Bit 2 -> Bit 0 */
	ADD	D0Ar2,D0Ar2,#8
	ADDCS	RA,D0Ar4,D0Ar2			/* If C issue RA */
	ADD	D0Ar2,D0Ar2,#8
	ADDNZ	RA,D0Ar4,D0Ar2			/* If Bit 0 set issue RA */
	MOV	TXDIVTIME,A1.3			/* Set RPDIRTY again */
___TBIResCrit:
	LSLS	D1Ar5,D0Re0,#1			/* Test XCBF (MI) & PRIV (CS)? */
#ifdef TBI_1_4
	ANDT	D1Ar5,D1Ar5,#(TBICTX_FPAC_BIT*2)
	LSL	D0Ar6,D1Ar5,#3			/* Convert FPAC into FPACTIVE */
#endif
 	ANDMT	D0Re0,D0Re0,#TBICTX_CBUF_BIT	/* Keep CBUF bit from SaveMask */
#ifdef TBI_1_4
	OR	D0Re0,D0Re0,D0Ar6		/* Combine FPACTIVE with others */
#endif
	MGETL	D0Ar6,D0Ar4,D0Ar2,D0FrT,D0.5,D0.6,D0.7,[A0StP] /* Restore DX */
	MOV	TXRPT,A0.2			/* Restore CT regs */
	MOV	TXBPOBITS,A1.2
	MOV	TXMODE,A0.3
	BCC	___TBIBoingPCX			/* Do non-PRIV wait! */
	MOV	A1GblIGbP,A1GbP			/* Save A1GbP too */
	MGETL	A0StP,A0FrP,A0.2,A0.3 A0_4,[D1Re0] /* Restore AX regs */
/*
 * Wait for the first interrupt/exception trigger in a privilege mode system
 * (interrupt stack area for current TASK to be pointed to by A0GblIStP
 * or per_cpu__stack_save[hwthread_id]).
 */
	MOV	TXSTATUS,D0Re0			/* Restore flags */
	MOV	D0Re0,TXPRIVEXT			/* Set TXPRIVEXT_TXTOGGLEI_BIT */
	SUB	D1Re0,D1Re0,#TBICTX_BYTES	/* TBICTX is top of int stack */
#ifdef TBX_PERCPU_SP_SAVE
	SWAP	D1Ar3,A1GbP
	MOV	D1Ar3,TXENABLE			/* Which thread are we? */
	AND	D1Ar3,D1Ar3,#TXENABLE_THREAD_BITS
	LSR	D1Ar3,D1Ar3,#TXENABLE_THREAD_S-2
	ADDT	D1Ar3,D1Ar3,#HI(_per_cpu__stack_save)
	ADD	D1Ar3,D1Ar3,#LO(_per_cpu__stack_save)
	SETD	[D1Ar3],D1Re0
	SWAP	D1Ar3,A1GbP
#else
	MOV	A0GblIStP, D1Re0
#endif
	OR	D0Re0,D0Re0,#TXPRIVEXT_TXTOGGLEI_BIT
	MOV	TXPRIVEXT,D0Re0			/* Cannot set TXPRIVEXT if !priv */
	GETL	D0Re0,D1Re0,[D1Re0+#TBICTX_DX]
	RTI					/* Wait for interrupt */
/*
 * Save initial interrupt state on A0GblIStP, switch to A0GblIStP if
 * BOOTROM code, save and switch to [A1GbP] otherwise.
 */
___TBIBoingPCXP:
#ifdef TBX_PERCPU_SP_SAVE
	SWAP	D1Ar3,A1GbP			/* Get PRIV stack base */
	MOV	D1Ar3,TXENABLE			/* Which thread are we? */
	AND	D1Ar3,D1Ar3,#TXENABLE_THREAD_BITS
	LSR	D1Ar3,D1Ar3,#TXENABLE_THREAD_S-2
	ADDT	D1Ar3,D1Ar3,#HI(_per_cpu__stack_save)
	ADD	D1Ar3,D1Ar3,#LO(_per_cpu__stack_save)
	GETD	D1Ar3,[D1Ar3]
#else
	SWAP	D1Ar3,A0GblIStP			/* Get PRIV stack base */
#endif
	SETL	[D1Ar3+#TBICTX_DX],D0Re0,D1Re0 /* Save key registers */
	MOV	D0Re0,TXPRIVEXT			/* Clear TXPRIVEXT_TXTOGGLEI_BIT */
	ADD	D1Re0,D1Ar3,#TBICTX_AX	/* Address AX save area */
	ANDMB	D0Re0,D0Re0,#0xFFFF-TXPRIVEXT_TXTOGGLEI_BIT
	MOV	TXPRIVEXT,D0Re0			/* Cannot set TXPRIVEXT if !priv */
	MOV	D0Re0,TXSTATUS			/* Read TXSTATUS into D0Re0 */
	MOV	TXSTATUS,#0			/* Clear TXSTATUS */
	MSETL	[D1Re0],A0StP,A0FrP,A0.2,A0.3 A0_4 /* Save AX critical regs */
	MOV	A0StP,D1Ar3			/* Switch stacks */
#ifdef TBX_PERCPU_SP_SAVE
	MOV	D1Ar3,A1GbP			/* Get D1Ar2 back */
#else
	MOV	D1Ar3,A0GblIStP			/* Get D1Ar2 back */
#endif
	ORT	D0Re0,D0Re0,#TBICTX_PRIV_BIT	/* Add PRIV to TXSTATUS */
	MOV	A1GbP,A1GblIGbP			/* Restore A1GbP */
	B	___TBIBoing			/* Enter common handler code */
/*
 * At this point we know it's a background HALT case we are handling.
 * The restored TXSTATUS always needs to have zero in the reason bits.
 */
___TBIBoingHalt:
	MOV	D0Ar4,TXMASKI			/* Get interrupt mask */
	ANDST	D0Re0,D0Re0,#HI(TXSTATUS_MAJOR_HALT_BITS+TXSTATUS_MEM_FAULT_BITS)
	AND	TXMASKI,D0Ar4,#TXSTATI_BGNDHALT_BIT /* Only allow HALTs */
	AND	D0Ar4,D0Ar4,#0xFFFF-TXSTATI_BGNDHALT_BIT /* What ints are off? */
	OR	D0Ar2,D0Ar2,D0Ar4		/* Set TBIRES.Sig.TrigMask */
	MOV	D0Ar4,#TXSTATI_BGNDHALT_BIT	/* This was the trigger state */
	LSR	D1Ar3,D0Re0,#TXSTATUS_MAJOR_HALT_S
	MOV	D0Re0,#TBID_SIGNUM_XXF<<TBID_SIGNUM_S
	BNZ	___TBIBoingVec			/* Jump to XXF exception handler */
/*
 * Only the SWITCH cases are left, PCX must be valid
 */
#ifdef TBI_1_4
	MOV	D1Ar5,TXPRIVEXT
	TST	D1Ar5,#TXPRIVEXT_MINIMON_BIT
	LSR	D1Ar3,D1Ar1,#1                  /* Shift needed for MINIM paths (fill stall) */
	BZ	$Lmeta                          /* If META only, skip */
	TSTT	D1Ar1,#HI(0x00800000)
	ANDMT	D1Ar3,D1Ar3,#HI(0x007FFFFF >> 1)/* Shifted mask for large MINIM */
	ANDT	D1Ar1,D1Ar1,#HI(0xFFE00000)     /* Static mask for small MINIM */
	BZ	$Llarge_minim                   /* If large MINIM */
$Lsmall_minim:
	TSTT	D1Ar3,#HI(0x00100000 >> 1)
	ANDMT	D1Ar3,D1Ar3,#HI(0x001FFFFF >> 1)/* Correct shifted mask for large MINIM */
	ADDZ	D1Ar1,D1Ar1,D1Ar3               /* If META rgn, add twice to undo LSR #1 */
	B	$Lrecombine
$Llarge_minim:
	ANDST	D1Ar1,D1Ar1,#HI(0xFF800000)     /* Correct static mask for small MINIM */
	                                        /* Z=0 (Cannot place code at NULL) */
$Lrecombine:
	ADD	D1Ar1,D1Ar1,D1Ar3               /* Combine static and shifted parts */
$Lmeta:
	GETW	D1Ar5,[D1Ar1++]			/* META: lo-16, MINIM: lo-16 (all-16 if short) */
	GETW	D1Ar3,[D1Ar1]			/* META: hi-16, MINIM: hi-16 (only if long) */
	MOV	D1Re0,D1Ar5
	XOR	D1Re0,D1Re0,#0x4000
	LSLSNZ	D1Re0,D1Re0,#(32-14)		/* MINIM: If long C=0, if short C=1 */
	LSLCC	D1Ar3,D1Ar3,#16			/* META/MINIM long: Move hi-16 up */
	LSLCS	D1Ar3,D1Ar5,#16			/* MINIM short: Dup all-16 */
	ADD	D1Ar5,D1Ar5,D1Ar3		/* ALL: Combine both 16-bit parts */
#else
	GETD	D1Ar5,[D1Ar1]			/* Read instruction for switch */
#endif
	LSR	D1Ar3,D1Ar5,#22			/* Convert into signal number */
	AND	D1Ar3,D1Ar3,#TBID_SIGNUM_SW3-TBID_SIGNUM_SW0
	LSL	D0Re0,D1Ar3,#TBID_SIGNUM_S	/* Generate offset from SigNum */
	B	___TBIBoingVec			/* Jump to switch handler */
/*
 * Exit from TBIASyncTrigger call
 */
___TBIBoingExit:
	GETL	D0FrT,D1RtP,[A0FrP++] 		/* Restore state from frame */
	SUB	A0StP,A0FrP,#8			/* Unwind stack */
	MOV	A0FrP,D0FrT			/* Last memory read completes */
	MOV	PC,D1RtP			/* Return to caller */
#endif /* ifdef CODE_USES_BOOTROM */
	.size	___TBIResume,.-___TBIResume

#ifndef BOOTROM
/*
 * void __TBIASyncResume( TBIRES State )
 */
	.text
	.balign	4
	.global	___TBIASyncResume
	.type	___TBIASyncResume,function
___TBIASyncResume:
/*
 * Perform CRIT|SOFT state restore and execute background thread.
 */
	MOV	D1Ar3,D1Ar1			/* Restore this context */
	MOV	D0Re0,D0Ar2			/* Carry in additional triggers */
						/* Reserve space for TBICTX */
	ADD	D1Ar3,D1Ar3,#TBICTX_BYTES+(CATCH_ENTRY_BYTES*CATCH_ENTRIES)
	MOV	A0StP,D1Ar3			/* Enter with protection of */
	MOV	A0FrP,D1Ar1			/*   TBICTX on our stack */
#ifdef CODE_USES_BOOTROM
	MOVT	D1Ar1,#HI(LINCORE_BASE)
	JUMP	D1Ar1,#0xA4
#else
	B	___TBIResume
#endif
	.size	___TBIASyncResume,.-___TBIASyncResume
#endif /* ifndef BOOTROM */

/*
 * End of tbipcx.S
 */
