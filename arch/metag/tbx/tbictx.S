/*
 * tbictx.S
 *
 * Copyright (C) 2001, 2002, 2007, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Explicit state save and restore routines forming part of the thread binary
 * interface for META processors
 */

	.file	"tbictx.S"
#include <asm/metag_regs.h>
#include <asm/tbx.h>

#ifdef METAC_1_0
/* Ax.4 is NOT saved in XAX3 */
#define A0_4
#else
/* Ax.4 is saved in XAX4 */
#define A0_4 A0.4,
#endif


/* Size of the TBICTX structure */
#define TBICTX_BYTES ((TBICTX_AX_REGS*8)+TBICTX_AX)

/*
 * TBIRES __TBINestInts( TBIRES State, void *pExt, int NoNestMask )
 */
	.text
	.balign	4
	.global	___TBINestInts
	.type	___TBINestInts,function
___TBINestInts:
	XOR	D0Ar4,D0Ar4,#-1			/* D0Ar4 = ~TrigBit */
	AND	D0Ar4,D0Ar4,#0xFFFF		/* D0Ar4 &= 0xFFFF */
	MOV	D0Ar6,TXMASKI			/* BGNDHALT currently enabled? */
	TSTT	D0Ar2,#TBICTX_XDX8_BIT+TBICTX_XAXX_BIT+TBICTX_XHL2_BIT+TBICTX_XTDP_BIT+TBICTX_XCBF_BIT
	AND	D0Ar4,D0Ar2,D0Ar4		/* D0Ar4 = Ints to allow */
	XOR	D0Ar2,D0Ar2,D0Ar4		/* Less Ints in TrigMask */
	BNZ	___TBINestInts2			/* Jump if ctx save required! */
	TSTT	D0Ar2,#TBICTX_CBUF_BIT+TBICTX_CBRP_BIT	/* Is catch state dirty? */
	OR	D0Ar4,D0Ar4,D0Ar6		/* Or in TXMASKI BGNDHALT if set */
	TSTNZ	D0Ar4,D0Ar4			/* Yes: AND triggers enabled */
	MOV	D0Re0,D0Ar2			/* Update State argument */
	MOV	D1Re0,D1Ar1			/*  with less Ints in TrigMask */
	MOVZ	TXMASKI,D0Ar4			/* Early return: Enable Ints */
	MOVZ	PC,D1RtP			/* Early return */
	.size	___TBINestInts,.-___TBINestInts
/*
 * Drop thru into sub-function-
 */
	.global	___TBINestInts2
	.type	___TBINestInts2,function
___TBINestInts2:
	MOV	D0FrT,A0FrP			/* Full entry sequence so we */
	ADD	A0FrP,A0StP,#0			/*     can make sub-calls */
	MSETL	[A0StP],D0FrT,D0.5,D0.6		/*     and preserve our result */
	ORT	D0Ar2,D0Ar2,#TBICTX_XCBF_BIT	/* Add in XCBF save request */
	MOV	D0.5,D0Ar2			/* Save State in DX.5 */
	MOV	D1.5,D1Ar1
	OR	D0.6,D0Ar4,D0Ar6		/* Save TrigMask in D0.6 */
	MOVT	D1RtP,#HI(___TBICtxSave)	/* Save catch buffer */
	CALL	D1RtP,#LO(___TBICtxSave)
	MOV	TXMASKI,D0.6			/* Allow Ints */
	MOV	D0Re0,D0.5			/* Return State */
	MOV	D1Re0,D1.5
	MGETL	D0FrT,D0.5,D0.6,[A0FrP]		/* Full exit sequence */
	SUB	A0StP,A0FrP,#(8*3)
	MOV	A0FrP,D0FrT
	MOV	PC,D1RtP
	.size	___TBINestInts2,.-___TBINestInts2

/*
 * void *__TBICtxSave( TBIRES State, void *pExt )
 *
 *       D0Ar2 contains TBICTX_*_BIT values that control what
 *          extended data is to be saved beyond the end of D1Ar1.
 *       These bits must be ored into the SaveMask of this structure.
 *
 *       Virtually all possible scratch registers are used.
 *
 *       The D1Ar1 parameter is only used as the basis for saving
 *       CBUF state.
 */
/*
 *       If TBICTX_XEXT_BIT is specified in State. then State.pCtx->Ext is
 *       utilised to save the base address of the context save area and
 *       the extended states saved. The XEXT flag then indicates that the
 *       original state of the A0.2 and A1.2 registers from TBICTX.Ext.AX2
 *       are stored as the first part of the extended state structure.
 */
	.balign	4
	.global	___TBICtxSave
	.type	___TBICtxSave,function
___TBICtxSave:
	GETD	D0Re0,[D1Ar1+#TBICTX_SaveMask-2]	/* Get SaveMask */
	TSTT	D0Ar2,#TBICTX_XDX8_BIT+TBICTX_XAXX_BIT+TBICTX_XHL2_BIT+TBICTX_XTDP_BIT+TBICTX_XEXT_BIT
						/* Just XCBF to save? */
	MOV	A0.2,D1Ar3			/* Save pointer into A0.2 */
	MOV	A1.2,D1RtP			/* Free off D0FrT:D1RtP pair */
	BZ	$LCtxSaveCBUF			/* Yes: Only XCBF may be saved */
	TSTT	D0Ar2,#TBICTX_XEXT_BIT		/* Extended base-state model? */
	BZ	$LCtxSaveXDX8
	GETL	D0Ar6,D1Ar5,[D1Ar1+#TBICTX_Ext_AX2]	/* Get A0.2, A1.2 state */
	MOV	D0Ar4,D0Ar2			/* Extract Ctx.SaveFlags value */
	ANDMT	D0Ar4,D0Ar4,#TBICTX_XDX8_BIT+TBICTX_XAXX_BIT+TBICTX_XHL2_BIT+TBICTX_XTDP_BIT+TBICTX_XEXT_BIT
	SETD	[D1Ar1+#TBICTX_Ext_Ctx_pExt],A0.2
	SETD	[D1Ar1+#TBICTX_Ext_Ctx_SaveMask-2],D0Ar4
	SETL	[A0.2++],D0Ar6,D1Ar5		/* Save A0.2, A1.2 state */
$LCtxSaveXDX8:
	TSTT	D0Ar2,#TBICTX_XDX8_BIT		/* Save extended DX regs? */
	BZ	$LCtxSaveXAXX
/*
 * Save 8 extra DX registers
 */
	MSETL	[A0.2],D0.8,D0.9,D0.10,D0.11,D0.12,D0.13,D0.14,D0.15
$LCtxSaveXAXX:
	TSTT	D0Ar2,#TBICTX_XAXX_BIT		/* Save extended AX regs? */
	SWAP	D0Re0,A0.2			/* pDst into D0Re0 */
	BZ	$LCtxSaveXHL2
/*
 * Save 4 extra AX registers
 */
	MSETL	[D0Re0], A0_4 A0.5,A0.6,A0.7	/* Save 8*3 bytes */
$LCtxSaveXHL2:
	TSTT	D0Ar2,#TBICTX_XHL2_BIT		/* Save hardware-loop regs? */
	SWAP	D0Re0,A0.2			/* pDst back into A0.2 */
	MOV	D0Ar6,TXL1START
	MOV	D1Ar5,TXL2START
	BZ	$LCtxSaveXTDP
/*
 * Save hardware loop registers
 */
	SETL	[A0.2++],D0Ar6,D1Ar5		/* Save 8*1 bytes */
	MOV	D0Ar6,TXL1END
	MOV	D1Ar5,TXL2END
	MOV	D0FrT,TXL1COUNT
	MOV	D1RtP,TXL2COUNT
	MSETL	[A0.2],D0Ar6,D0FrT		/* Save 8*2 bytes */
/*
 * Clear loop counters to disable any current loops
 */
	XOR	TXL1COUNT,D0FrT,D0FrT
	XOR	TXL2COUNT,D1RtP,D1RtP
$LCtxSaveXTDP:
	TSTT	D0Ar2,#TBICTX_XTDP_BIT		/* Save per-thread DSP regs? */
	BZ	$LCtxSaveCBUF
/*
 * Save per-thread DSP registers; ACC.0, PR.0, PI.1-3 (PI.0 is zero)
 */
#ifndef CTX_NO_DSP
D	SETL	[A0.2++],AC0.0,AC1.0		/* Save ACx.0 lower 32-bits */
DH	SETL	[A0.2++],AC0.0,AC1.0		/* Save ACx.0 upper 32-bits */
D	SETL	[A0.2++],D0AR.0,D1AR.0		/* Save DSP RAM registers */
D	SETL	[A0.2++],D0AR.1,D1AR.1
D	SETL	[A0.2++],D0AW.0,D1AW.0
D	SETL	[A0.2++],D0AW.1,D1AW.1
D	SETL	[A0.2++],D0BR.0,D1BR.0
D	SETL	[A0.2++],D0BR.1,D1BR.1
D	SETL	[A0.2++],D0BW.0,D1BW.0
D	SETL	[A0.2++],D0BW.1,D1BW.1
D	SETL	[A0.2++],D0ARI.0,D1ARI.0
D	SETL	[A0.2++],D0ARI.1,D1ARI.1
D	SETL	[A0.2++],D0AWI.0,D1AWI.0
D	SETL	[A0.2++],D0AWI.1,D1AWI.1
D	SETL	[A0.2++],D0BRI.0,D1BRI.0
D	SETL	[A0.2++],D0BRI.1,D1BRI.1
D	SETL	[A0.2++],D0BWI.0,D1BWI.0
D	SETL	[A0.2++],D0BWI.1,D1BWI.1
D	SETD	[A0.2++],T0
D	SETD	[A0.2++],T1
D	SETD	[A0.2++],T2
D	SETD	[A0.2++],T3
D	SETD	[A0.2++],T4
D	SETD	[A0.2++],T5
D	SETD	[A0.2++],T6
D	SETD	[A0.2++],T7
D	SETD	[A0.2++],T8
D	SETD	[A0.2++],T9
D	SETD	[A0.2++],TA
D	SETD	[A0.2++],TB
D	SETD	[A0.2++],TC
D	SETD	[A0.2++],TD
D	SETD	[A0.2++],TE
D	SETD	[A0.2++],TF
#else
	ADD	A0.2,A0.2,#(8*18+4*16)
#endif
	MOV	D0Ar6,TXMRSIZE
	MOV	D1Ar5,TXDRSIZE
	SETL	[A0.2++],D0Ar6,D1Ar5		/* Save 8*1 bytes */
	
$LCtxSaveCBUF:
#ifdef TBI_1_3
	MOV	D0Ar4,D0Re0			/* Copy Ctx Flags */
	ANDT	D0Ar4,D0Ar4,#TBICTX_XCBF_BIT	/*   mask XCBF if already set */
	XOR	D0Ar4,D0Ar4,#-1
	AND	D0Ar2,D0Ar2,D0Ar4		/*   remove XCBF if already set */
#endif
	TSTT	D0Ar2,#TBICTX_XCBF_BIT		/* Want to save CBUF? */
	ANDT	D0Ar2,D0Ar2,#TBICTX_XDX8_BIT+TBICTX_XAXX_BIT+TBICTX_XHL2_BIT+TBICTX_XTDP_BIT+TBICTX_XEXT_BIT
	OR	D0Ar2,D0Ar2,D0Re0		/* Generate new SaveMask */
	SETD	[D1Ar1+#TBICTX_SaveMask-2],D0Ar2/* Add in bits saved to TBICTX */
	MOV	D0Re0,A0.2			/* Return end of save area */
	MOV	D0Ar4,TXDIVTIME			/* Get TXDIVTIME */
	MOVZ	PC,A1.2				/* No: Early return */
	TSTT	D0Ar2,#TBICTX_CBUF_BIT+TBICTX_CBRP_BIT	/* Need to save CBUF? */
	MOVZ	PC,A1.2				/* No: Early return */
	ORT	D0Ar2,D0Ar2,#TBICTX_XCBF_BIT
	SETD	[D1Ar1+#TBICTX_SaveMask-2],D0Ar2/* Add in XCBF bit to TBICTX */
	ADD	A0.2,D1Ar1,#TBICTX_BYTES	/* Dump CBUF state after TBICTX */
/*
 * Save CBUF
 */
	SETD	[A0.2+# 0],TXCATCH0		/* Restore TXCATCHn */
	SETD	[A0.2+# 4],TXCATCH1
	TSTT	D0Ar2,#TBICTX_CBRP_BIT		/* ... RDDIRTY was/is set */
	SETD	[A0.2+# 8],TXCATCH2
	SETD	[A0.2+#12],TXCATCH3
	BZ	$LCtxSaveComplete
	SETL	[A0.2+#(2*8)],RD		/* Save read pipeline */
	SETL	[A0.2+#(3*8)],RD		/* Save read pipeline */
	SETL	[A0.2+#(4*8)],RD		/* Save read pipeline */
	SETL	[A0.2+#(5*8)],RD		/* Save read pipeline */
	SETL	[A0.2+#(6*8)],RD		/* Save read pipeline */
	SETL	[A0.2+#(7*8)],RD		/* Save read pipeline */
	AND	TXDIVTIME,D0Ar4,#TXDIVTIME_DIV_BITS /* Clear RPDIRTY */
$LCtxSaveComplete:
	MOV	PC,A1.2				/* Return */
	.size	___TBICtxSave,.-___TBICtxSave

/*
 * void *__TBICtxRestore( TBIRES State, void *pExt )
 *
 *                 D0Ar2 contains TBICTX_*_BIT values that control what
 *                    extended data is to be recovered from D1Ar3 (pExt).
 *
 *                 Virtually all possible scratch registers are used.
 */
/*
 *	If TBICTX_XEXT_BIT is specified in State. Then the saved state of
 *       the orginal A0.2 and A1.2 is restored from pExt and the XEXT
 *       related flags are removed from State.pCtx->SaveMask.
 *
 */
	.balign	4
	.global	___TBICtxRestore
	.type	___TBICtxRestore,function
___TBICtxRestore:
	GETD	D0Ar6,[D1Ar1+#TBICTX_CurrMODE]	/* Get TXMODE Value */
	ANDST	D0Ar2,D0Ar2,#TBICTX_XDX8_BIT+TBICTX_XAXX_BIT+TBICTX_XHL2_BIT+TBICTX_XTDP_BIT+TBICTX_XEXT_BIT
	MOV	D1Re0,D0Ar2			/* Keep flags in D1Re0 */
	MOV	D0Re0,D1Ar3			/* D1Ar3 is default result */
	MOVZ	PC,D1RtP			/* Early return, nothing to do */
	ANDT	D0Ar6,D0Ar6,#0xE000		/* Top bits of TXMODE required */
	MOV	A0.3,D0Ar6			/* Save TXMODE for later */
	TSTT	D1Re0,#TBICTX_XEXT_BIT		/* Check for XEXT bit */
	BZ	$LCtxRestXDX8
	GETD	D0Ar4,[D1Ar1+#TBICTX_SaveMask-2]/* Get current SaveMask */
	GETL	D0Ar6,D1Ar5,[D0Re0++]		/* Restore A0.2, A1.2 state */
	ANDMT	D0Ar4,D0Ar4,#(0xFFFF-(TBICTX_XDX8_BIT+TBICTX_XAXX_BIT+TBICTX_XHL2_BIT+TBICTX_XTDP_BIT+TBICTX_XEXT_BIT))
	SETD	[D1Ar1+#TBICTX_SaveMask-2],D0Ar4/* New SaveMask */
#ifdef METAC_1_0
	SETD	[D1Ar1+#TBICTX_Ext_AX2_U0],D0Ar6
	MOV	D0Ar6,D1Ar1
	SETD	[D0Ar6+#TBICTX_Ext_AX2_U1],D1Ar5
#else
	SETL	[D1Ar1+#TBICTX_Ext_AX2],D0Ar6,D1Ar5
#endif
$LCtxRestXDX8:
	TSTT	D1Re0,#TBICTX_XDX8_BIT		/* Get extended DX regs? */
	MOV	A1.2,D1RtP			/* Free off D1RtP register */
	BZ	$LCtxRestXAXX
/*
 * Restore 8 extra DX registers
 */
	MGETL	D0.8,D0.9,D0.10,D0.11,D0.12,D0.13,D0.14,D0.15,[D0Re0]
$LCtxRestXAXX:
	TSTT	D1Re0,#TBICTX_XAXX_BIT		/* Get extended AX regs? */
	BZ	$LCtxRestXHL2
/*
 * Restore 3 extra AX registers
 */
	MGETL	A0_4 A0.5,A0.6,A0.7,[D0Re0]	/* Get 8*3 bytes */
$LCtxRestXHL2:
	TSTT	D1Re0,#TBICTX_XHL2_BIT		/* Get hardware-loop regs? */
	BZ	$LCtxRestXTDP
/*
 * Get hardware loop registers
 */
	MGETL	D0Ar6,D0Ar4,D0Ar2,[D0Re0]	/* Get 8*3 bytes */
	MOV	TXL1START,D0Ar6
	MOV	TXL2START,D1Ar5
	MOV	TXL1END,D0Ar4
	MOV	TXL2END,D1Ar3
	MOV	TXL1COUNT,D0Ar2
	MOV	TXL2COUNT,D1Ar1
$LCtxRestXTDP:
	TSTT	D1Re0,#TBICTX_XTDP_BIT		/* Get per-thread DSP regs? */
	MOVZ	PC,A1.2				/* No: Early return */
/*
 * Get per-thread DSP registers; ACC.0, PR.0, PI.1-3 (PI.0 is zero)
 */
	MOV	A0.2,D0Re0
	GETL	D0Ar6,D1Ar5,[D0Re0++#((16*4)+(18*8))]
#ifndef CTX_NO_DSP
D	GETL	AC0.0,AC1.0,[A0.2++]		/* Restore ACx.0 lower 32-bits */
DH	GETL	AC0.0,AC1.0,[A0.2++]		/* Restore ACx.0 upper 32-bits */
#else
	ADD	A0.2,A0.2,#(2*8)
#endif
	ADD	D0Re0,D0Re0,#(2*4)
	MOV	TXMODE,A0.3			/* Some TXMODE bits needed */
	MOV	TXMRSIZE,D0Ar6
	MOV	TXDRSIZE,D1Ar5
#ifndef CTX_NO_DSP
D	GETL	D0AR.0,D1AR.0,[A0.2++]		/* Restore DSP RAM registers */
D	GETL	D0AR.1,D1AR.1,[A0.2++]
D	GETL	D0AW.0,D1AW.0,[A0.2++]
D	GETL	D0AW.1,D1AW.1,[A0.2++]
D	GETL	D0BR.0,D1BR.0,[A0.2++]
D	GETL	D0BR.1,D1BR.1,[A0.2++]
D	GETL	D0BW.0,D1BW.0,[A0.2++]
D	GETL	D0BW.1,D1BW.1,[A0.2++]
#else
	ADD	A0.2,A0.2,#(8*8)
#endif
	MOV	TXMODE,#0			/* Restore TXMODE */
#ifndef CTX_NO_DSP
D	GETL	D0ARI.0,D1ARI.0,[A0.2++]
D	GETL	D0ARI.1,D1ARI.1,[A0.2++]
D	GETL	D0AWI.0,D1AWI.0,[A0.2++]
D	GETL	D0AWI.1,D1AWI.1,[A0.2++]
D	GETL	D0BRI.0,D1BRI.0,[A0.2++]
D	GETL	D0BRI.1,D1BRI.1,[A0.2++]
D	GETL	D0BWI.0,D1BWI.0,[A0.2++]
D	GETL	D0BWI.1,D1BWI.1,[A0.2++]
D	GETD	T0,[A0.2++]
D	GETD	T1,[A0.2++]
D	GETD	T2,[A0.2++]
D	GETD	T3,[A0.2++]
D	GETD	T4,[A0.2++]
D	GETD	T5,[A0.2++]
D	GETD	T6,[A0.2++]
D	GETD	T7,[A0.2++]
D	GETD	T8,[A0.2++]
D	GETD	T9,[A0.2++]
D	GETD	TA,[A0.2++]
D	GETD	TB,[A0.2++]
D	GETD	TC,[A0.2++]
D	GETD	TD,[A0.2++]
D	GETD	TE,[A0.2++]
D	GETD	TF,[A0.2++]
#else
	ADD	A0.2,A0.2,#(8*8+4*16)
#endif
	MOV	PC,A1.2				/* Return */
	.size	___TBICtxRestore,.-___TBICtxRestore

/*
 * End of tbictx.S
 */
