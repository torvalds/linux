/*
 * tbictxfpu.S
 *
 * Copyright (C) 2009, 2012 Imagination Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation.
 *
 * Explicit state save and restore routines forming part of the thread binary
 * interface for META processors
 */

	.file	"tbifpuctx.S"

#include <asm/metag_regs.h>
#include <asm/tbx.h>

#ifdef TBI_1_4
/*
 * void *__TBICtxFPUSave( TBIRES State, void *pExt )
 *
 *                 D0Ar2 contains TBICTX_*_BIT values that control what
 *                    extended data is to be saved.
 *                 These bits must be ored into the SaveMask of this structure.
 *
 *                 Virtually all possible scratch registers are used.
 */
	.text
	.balign	4
	.global	___TBICtxFPUSave
	.type	___TBICtxFPUSave,function
___TBICtxFPUSave:

	/* D1Ar1:D0Ar2 - State
	 * D1Ar3       - pExt
	 * D0Ar4       - Value of METAC_CORE_ID
	 * D1Ar5       - Scratch
	 * D0Ar6       - Scratch
	 */
	
	/* If the FPAC bit isnt set then there is nothing to do */
	TSTT	D0Ar2,#TBICTX_FPAC_BIT
	MOVZ	PC, D1RtP

	/* Obtain the Core config */
	MOVT	D0Ar4,        #HI(METAC_CORE_ID)
	ADD	D0Ar4, D0Ar4, #LO(METAC_CORE_ID)
	GETD	D0Ar4, [D0Ar4]

	/* Detect FX.8 - FX.15 and add to core config */
	MOV	D0Ar6, TXENABLE
	AND	D0Ar6, D0Ar6, #(TXENABLE_CLASSALT_FPUR8 << TXENABLE_CLASS_S)
	AND	D0Ar4, D0Ar4, #LO(0x0000FFFF)
	ORT	D0Ar4, D0Ar4, #HI(TBICTX_CFGFPU_FX16_BIT)
	XOR	D0Ar4, D0Ar4, D0Ar6

	/* Save the relevant bits to the buffer */
	SETD	[D1Ar3++], D0Ar4

	/* Save the relevant bits of TXDEFR (Assumes TXDEFR is coherent) ... */
	MOV	D0Ar6, TXDEFR
	LSR	D0Re0, D0Ar6, #8
	AND	D0Re0, D0Re0, #LO(TXDEFR_FPE_FE_BITS>>8)
	AND	D0Ar6, D0Ar6, #LO(TXDEFR_FPE_ICTRL_BITS)
	OR	D0Re0, D0Re0, D0Ar6

	/* ... along with relevant bits of TXMODE to buffer */
	MOV	D0Ar6, TXMODE
	ANDT	D0Ar6, D0Ar6, #HI(TXMODE_FPURMODE_BITS)
	ORT	D0Ar6, D0Ar6, #HI(TXMODE_FPURMODEWRITE_BIT)
	OR	D0Ar6, D0Ar6, D0Re0
	SETD	[D1Ar3++], D0Ar6

	GETD	D0Ar6,[D1Ar1+#TBICTX_SaveMask-2] /* Get the current SaveMask */
	/* D0Ar6       - pCtx->SaveMask */

	TSTT	D0Ar4, #HI(TBICTX_CFGFPU_FX16_BIT) /* Perform test here for extended FPU registers
	    	                                    * to avoid stalls
	    	                                    */
	/* Save the standard FPU registers */
F	MSETL	[D1Ar3++], FX.0, FX.2, FX.4, FX.6

	/* Save the extended FPU registers if they are present */
	BZ	$Lskip_save_fx8_fx16
F	MSETL	[D1Ar3++], FX.8, FX.10, FX.12, FX.14
$Lskip_save_fx8_fx16:

	/* Save the FPU Accumulator if it is present */
	TST	D0Ar4, #METAC_COREID_NOFPACC_BIT
	BNZ	$Lskip_save_fpacc
F	SETL	[D1Ar3++], ACF.0
F	SETL	[D1Ar3++], ACF.1
F	SETL	[D1Ar3++], ACF.2
$Lskip_save_fpacc:

	/* Update pCtx->SaveMask */
	ANDT	D0Ar2, D0Ar2, #TBICTX_FPAC_BIT
	OR	D0Ar6, D0Ar6, D0Ar2
	SETD	[D1Ar1+#TBICTX_SaveMask-2],D0Ar6/* Add in XCBF bit to TBICTX */

	MOV	D0Re0, D1Ar3 /* Return end of save area */
	MOV	PC, D1RtP

	.size	___TBICtxFPUSave,.-___TBICtxFPUSave

/*
 * void *__TBICtxFPURestore( TBIRES State, void *pExt )
 *
 *                 D0Ar2 contains TBICTX_*_BIT values that control what
 *                    extended data is to be recovered from D1Ar3 (pExt).
 *
 *                 Virtually all possible scratch registers are used.
 */
/*
 * If TBICTX_XEXT_BIT is specified in State. Then the saved state of
 *       the orginal A0.2 and A1.2 is restored from pExt and the XEXT
 *       related flags are removed from State.pCtx->SaveMask.
 *
 */
	.balign	4
	.global	___TBICtxFPURestore
	.type	___TBICtxFPURestore,function
___TBICtxFPURestore:

	/* D1Ar1:D0Ar2 - State
	 * D1Ar3       - pExt
	 * D0Ar4       - Value of METAC_CORE_ID
	 * D1Ar5       - Scratch
	 * D0Ar6       - Scratch
	 * D1Re0       - Scratch
	 */

	/* If the FPAC bit isnt set then there is nothing to do */
	TSTT	D0Ar2,#TBICTX_FPAC_BIT
	MOVZ	PC, D1RtP

	/* Obtain the relevant bits of the Core config */
	GETD	D0Ar4, [D1Ar3++]

	/* Restore FPU related parts of TXDEFR. Assumes TXDEFR is coherent */
	GETD	D1Ar5, [D1Ar3++]
	MOV	D0Ar6, D1Ar5
	LSL	D1Re0, D1Ar5, #8
	ANDT	D1Re0, D1Re0, #HI(TXDEFR_FPE_FE_BITS|TXDEFR_FPE_ICTRL_BITS)
	AND	D1Ar5, D1Ar5, #LO(TXDEFR_FPE_FE_BITS|TXDEFR_FPE_ICTRL_BITS)
	OR	D1Re0, D1Re0, D1Ar5

	MOV	D1Ar5, TXDEFR
	ANDMT	D1Ar5, D1Ar5, #HI(~(TXDEFR_FPE_FE_BITS|TXDEFR_FPE_ICTRL_BITS))
	ANDMB	D1Ar5, D1Ar5, #LO(~(TXDEFR_FPE_FE_BITS|TXDEFR_FPE_ICTRL_BITS))
	OR	D1Re0, D1Re0, D1Ar5
	MOV	TXDEFR, D1Re0

	/* Restore relevant bits of TXMODE */
	MOV	D1Ar5, TXMODE
	ANDMT	D1Ar5, D1Ar5, #HI(~TXMODE_FPURMODE_BITS)
	ANDT	D0Ar6, D0Ar6, #HI(TXMODE_FPURMODE_BITS|TXMODE_FPURMODEWRITE_BIT)
	OR	D0Ar6, D0Ar6, D1Ar5
	MOV	TXMODE, D0Ar6

	TSTT	D0Ar4, #HI(TBICTX_CFGFPU_FX16_BIT) /* Perform test here for extended FPU registers
	    	                                    * to avoid stalls
	    	                                    */
	/* Save the standard FPU registers */
F	MGETL	FX.0, FX.2, FX.4, FX.6, [D1Ar3++]

	/* Save the extended FPU registers if they are present */
	BZ	$Lskip_restore_fx8_fx16
F	MGETL	FX.8, FX.10, FX.12, FX.14, [D1Ar3++]
$Lskip_restore_fx8_fx16:

	/* Save the FPU Accumulator if it is present */
	TST	D0Ar4, #METAC_COREID_NOFPACC_BIT
	BNZ	$Lskip_restore_fpacc
F	GETL	ACF.0, [D1Ar3++]
F	GETL	ACF.1, [D1Ar3++]
F	GETL	ACF.2, [D1Ar3++]
$Lskip_restore_fpacc:

	MOV	D0Re0, D1Ar3 /* Return end of save area */
	MOV	PC, D1RtP

	.size	___TBICtxFPURestore,.-___TBICtxFPURestore

#endif /* TBI_1_4 */

/*
 * End of tbictx.S
 */
