/*
 * This header file contains assembly-language definitions (assembly
 * macros, etc.) for this specific Xtensa processor's TIE extensions
 * and options.  It is customized to this Xtensa processor configuration.
 *
 * This file is subject to the terms and conditions of version 2.1 of the GNU
 * Lesser General Public License as published by the Free Software Foundation.
 *
 * Copyright (C) 1999-2009 Tensilica Inc.
 */

#ifndef _XTENSA_CORE_TIE_ASM_H
#define _XTENSA_CORE_TIE_ASM_H

/*  Selection parameter values for save-area save/restore macros:  */
/*  Option vs. TIE:  */
#define XTHAL_SAS_TIE	0x0001	/* custom extension or coprocessor */
#define XTHAL_SAS_OPT	0x0002	/* optional (and not a coprocessor) */
/*  Whether used automatically by compiler:  */
#define XTHAL_SAS_NOCC	0x0004	/* not used by compiler w/o special opts/code */
#define XTHAL_SAS_CC	0x0008	/* used by compiler without special opts/code */
/*  ABI handling across function calls:  */
#define XTHAL_SAS_CALR	0x0010	/* caller-saved */
#define XTHAL_SAS_CALE	0x0020	/* callee-saved */
#define XTHAL_SAS_GLOB	0x0040	/* global across function calls (in thread) */
/*  Misc  */
#define XTHAL_SAS_ALL	0xFFFF	/* include all default NCP contents */



/* Macro to save all non-coprocessor (extra) custom TIE and optional state
 * (not including zero-overhead loop registers).
 * Save area ptr (clobbered):  ptr  (8 byte aligned)
 * Scratch regs  (clobbered):  at1..at4  (only first XCHAL_NCP_NUM_ATMPS needed)
 */
	.macro xchal_ncp_store  ptr at1 at2 at3 at4  continue=0 ofs=-1 select=XTHAL_SAS_ALL
	xchal_sa_start	\continue, \ofs
	.ifeq (XTHAL_SAS_OPT | XTHAL_SAS_NOCC | XTHAL_SAS_CALR) & ~\select
	xchal_sa_align	\ptr, 0, 1024-4, 4, 4
	rsr	\at1, BR		// boolean option
	s32i	\at1, \ptr, .Lxchal_ofs_ + 0
	.set	.Lxchal_ofs_, .Lxchal_ofs_ + 4
	.endif
	.ifeq (XTHAL_SAS_OPT | XTHAL_SAS_NOCC | XTHAL_SAS_CALR) & ~\select
	xchal_sa_align	\ptr, 0, 1024-4, 4, 4
	rsr	\at1, SCOMPARE1		// conditional store option
	s32i	\at1, \ptr, .Lxchal_ofs_ + 0
	.set	.Lxchal_ofs_, .Lxchal_ofs_ + 4
	.endif
	.ifeq (XTHAL_SAS_OPT | XTHAL_SAS_CC | XTHAL_SAS_GLOB) & ~\select
	xchal_sa_align	\ptr, 0, 1024-4, 4, 4
	rur	\at1, THREADPTR		// threadptr option
	s32i	\at1, \ptr, .Lxchal_ofs_ + 0
	.set	.Lxchal_ofs_, .Lxchal_ofs_ + 4
	.endif
	.endm	// xchal_ncp_store

/* Macro to save all non-coprocessor (extra) custom TIE and optional state
 * (not including zero-overhead loop registers).
 * Save area ptr (clobbered):  ptr  (8 byte aligned)
 * Scratch regs  (clobbered):  at1..at4  (only first XCHAL_NCP_NUM_ATMPS needed)
 */
	.macro xchal_ncp_load  ptr at1 at2 at3 at4  continue=0 ofs=-1 select=XTHAL_SAS_ALL
	xchal_sa_start	\continue, \ofs
	.ifeq (XTHAL_SAS_OPT | XTHAL_SAS_NOCC | XTHAL_SAS_CALR) & ~\select
	xchal_sa_align	\ptr, 0, 1024-4, 4, 4
	l32i	\at1, \ptr, .Lxchal_ofs_ + 0
	wsr	\at1, BR		// boolean option
	.set	.Lxchal_ofs_, .Lxchal_ofs_ + 4
	.endif
	.ifeq (XTHAL_SAS_OPT | XTHAL_SAS_NOCC | XTHAL_SAS_CALR) & ~\select
	xchal_sa_align	\ptr, 0, 1024-4, 4, 4
	l32i	\at1, \ptr, .Lxchal_ofs_ + 0
	wsr	\at1, SCOMPARE1		// conditional store option
	.set	.Lxchal_ofs_, .Lxchal_ofs_ + 4
	.endif
	.ifeq (XTHAL_SAS_OPT | XTHAL_SAS_CC | XTHAL_SAS_GLOB) & ~\select
	xchal_sa_align	\ptr, 0, 1024-4, 4, 4
	l32i	\at1, \ptr, .Lxchal_ofs_ + 0
	wur	\at1, THREADPTR		// threadptr option
	.set	.Lxchal_ofs_, .Lxchal_ofs_ + 4
	.endif
	.endm	// xchal_ncp_load



#define XCHAL_NCP_NUM_ATMPS	1



/* Macro to save the state of TIE coprocessor AudioEngineLX.
 * Save area ptr (clobbered):  ptr  (8 byte aligned)
 * Scratch regs  (clobbered):  at1..at4  (only first XCHAL_CP1_NUM_ATMPS needed)
 */
#define xchal_cp_AudioEngineLX_store	xchal_cp1_store
/* #define xchal_cp_AudioEngineLX_store_a2	xchal_cp1_store a2 a3 a4 a5 a6 */
	.macro	xchal_cp1_store  ptr at1 at2 at3 at4  continue=0 ofs=-1 select=XTHAL_SAS_ALL
	xchal_sa_start \continue, \ofs
	.ifeq (XTHAL_SAS_TIE | XTHAL_SAS_NOCC | XTHAL_SAS_CALR) & ~\select
	xchal_sa_align	\ptr, 0, 0, 1, 8
	rur240	\at1		// AE_OVF_SAR
	s32i	\at1, \ptr, 0
	rur241	\at1		// AE_BITHEAD
	s32i	\at1, \ptr, 4
	rur242	\at1		// AE_TS_FTS_BU_BP
	s32i	\at1, \ptr, 8
	rur243	\at1		// AE_SD_NO
	s32i	\at1, \ptr, 12
	AE_SP24X2S.I aep0, \ptr,  16
	AE_SP24X2S.I aep1, \ptr,  24
	AE_SP24X2S.I aep2, \ptr,  32
	AE_SP24X2S.I aep3, \ptr,  40
	AE_SP24X2S.I aep4, \ptr,  48
	AE_SP24X2S.I aep5, \ptr,  56
	addi	\ptr, \ptr, 64
	AE_SP24X2S.I aep6, \ptr,  0
	AE_SP24X2S.I aep7, \ptr,  8
	AE_SQ56S.I aeq0, \ptr,  16
	AE_SQ56S.I aeq1, \ptr,  24
	AE_SQ56S.I aeq2, \ptr,  32
	AE_SQ56S.I aeq3, \ptr,  40
	.set	.Lxchal_pofs_, .Lxchal_pofs_ + 64
	.set	.Lxchal_ofs_, .Lxchal_ofs_ + 112
	.endif
	.endm	// xchal_cp1_store

/* Macro to restore the state of TIE coprocessor AudioEngineLX.
 * Save area ptr (clobbered):  ptr  (8 byte aligned)
 * Scratch regs  (clobbered):  at1..at4  (only first XCHAL_CP1_NUM_ATMPS needed)
 */
#define xchal_cp_AudioEngineLX_load	xchal_cp1_load
/* #define xchal_cp_AudioEngineLX_load_a2	xchal_cp1_load a2 a3 a4 a5 a6 */
	.macro	xchal_cp1_load  ptr at1 at2 at3 at4  continue=0 ofs=-1 select=XTHAL_SAS_ALL
	xchal_sa_start \continue, \ofs
	.ifeq (XTHAL_SAS_TIE | XTHAL_SAS_NOCC | XTHAL_SAS_CALR) & ~\select
	xchal_sa_align	\ptr, 0, 0, 1, 8
	l32i	\at1, \ptr, 0
	wur240	\at1		// AE_OVF_SAR
	l32i	\at1, \ptr, 4
	wur241	\at1		// AE_BITHEAD
	l32i	\at1, \ptr, 8
	wur242	\at1		// AE_TS_FTS_BU_BP
	l32i	\at1, \ptr, 12
	wur243	\at1		// AE_SD_NO
	addi	\ptr, \ptr, 80
	AE_LQ56.I aeq0, \ptr,  0
	AE_LQ56.I aeq1, \ptr,  8
	AE_LQ56.I aeq2, \ptr,  16
	AE_LQ56.I aeq3, \ptr,  24
	AE_LP24X2.I aep0, \ptr,  -64
	AE_LP24X2.I aep1, \ptr,  -56
	AE_LP24X2.I aep2, \ptr,  -48
	AE_LP24X2.I aep3, \ptr,  -40
	AE_LP24X2.I aep4, \ptr,  -32
	AE_LP24X2.I aep5, \ptr,  -24
	AE_LP24X2.I aep6, \ptr,  -16
	AE_LP24X2.I aep7, \ptr,  -8
	.set	.Lxchal_pofs_, .Lxchal_pofs_ + 80
	.set	.Lxchal_ofs_, .Lxchal_ofs_ + 112
	.endif
	.endm	// xchal_cp1_load

#define XCHAL_CP1_NUM_ATMPS	1
#define XCHAL_SA_NUM_ATMPS	1

	/*  Empty macros for unconfigured coprocessors:  */
	.macro xchal_cp0_store	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp0_load	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp2_store	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp2_load	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp3_store	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp3_load	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp4_store	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp4_load	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp5_store	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp5_load	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp6_store	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp6_load	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp7_store	p a b c d continue=0 ofs=-1 select=-1 ; .endm
	.macro xchal_cp7_load	p a b c d continue=0 ofs=-1 select=-1 ; .endm

#endif /*_XTENSA_CORE_TIE_ASM_H*/
