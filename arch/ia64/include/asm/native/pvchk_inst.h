#ifndef _ASM_NATIVE_PVCHK_INST_H
#define _ASM_NATIVE_PVCHK_INST_H

/******************************************************************************
 * arch/ia64/include/asm/native/pvchk_inst.h
 * Checker for paravirtualizations of privileged operations.
 *
 * Copyright (C) 2005 Hewlett-Packard Co
 *      Dan Magenheimer <dan.magenheimer@hp.com>
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

/**********************************************
 * Instructions paravirtualized for correctness
 **********************************************/

/* "fc" and "thash" are privilege-sensitive instructions, meaning they
 *  may have different semantics depending on whether they are executed
 *  at PL0 vs PL!=0.  When paravirtualized, these instructions mustn't
 *  be allowed to execute directly, lest incorrect semantics result.
 */

#define fc	.error "fc should not be used directly."
#define thash	.error "thash should not be used directly."

/* Note that "ttag" and "cover" are also privilege-sensitive; "ttag"
 * is not currently used (though it may be in a long-format VHPT system!)
 * and the semantics of cover only change if psr.ic is off which is very
 * rare (and currently non-existent outside of assembly code
 */
#define ttag	.error "ttag should not be used directly."
#define cover	.error "cover should not be used directly."

/* There are also privilege-sensitive registers.  These registers are
 * readable at any privilege level but only writable at PL0.
 */
#define cpuid	.error "cpuid should not be used directly."
#define pmd	.error "pmd should not be used directly."

/*
 * mov ar.eflag =
 * mov = ar.eflag
 */

/**********************************************
 * Instructions paravirtualized for performance
 **********************************************/
/*
 * Those instructions include '.' which can't be handled by cpp.
 * or can't be handled by cpp easily.
 * They are handled by sed instead of cpp.
 */

/* for .S
 * itc.i
 * itc.d
 *
 * bsw.0
 * bsw.1
 *
 * ssm psr.ic | PSR_DEFAULT_BITS
 * ssm psr.ic
 * rsm psr.ic
 * ssm psr.i
 * rsm psr.i
 * rsm psr.i | psr.ic
 * rsm psr.dt
 * ssm psr.dt
 *
 * mov = cr.ifa
 * mov = cr.itir
 * mov = cr.isr
 * mov = cr.iha
 * mov = cr.ipsr
 * mov = cr.iim
 * mov = cr.iip
 * mov = cr.ivr
 * mov = psr
 *
 * mov cr.ifa =
 * mov cr.itir =
 * mov cr.iha =
 * mov cr.ipsr =
 * mov cr.ifs =
 * mov cr.iip =
 * mov cr.kr =
 */

/* for intrinsics
 * ssm psr.i
 * rsm psr.i
 * mov = psr
 * mov = ivr
 * mov = tpr
 * mov cr.itm =
 * mov eoi =
 * mov rr[] =
 * mov = rr[]
 * mov = kr
 * mov kr =
 * ptc.ga
 */

/*************************************************************
 * define paravirtualized instrcution macros as nop to ingore.
 * and check whether arguments are appropriate.
 *************************************************************/

/* check whether reg is a regular register */
.macro is_rreg_in reg
	.ifc "\reg", "r0"
		nop 0
		.exitm
	.endif
	;;
	mov \reg = r0
	;;
.endm
#define IS_RREG_IN(reg)	is_rreg_in reg ;

#define IS_RREG_OUT(reg)			\
	;;					\
	mov reg = r0				\
	;;

#define IS_RREG_CLOB(reg)	IS_RREG_OUT(reg)

/* check whether pred is a predicate register */
#define IS_PRED_IN(pred)			\
	;;					\
	(pred)	nop 0				\
	;;

#define IS_PRED_OUT(pred)			\
	;;					\
	cmp.eq pred, p0 = r0, r0		\
	;;

#define IS_PRED_CLOB(pred)	IS_PRED_OUT(pred)


#define DO_SAVE_MIN(__COVER, SAVE_IFS, EXTRA, WORKAROUND)	\
	nop 0
#define MOV_FROM_IFA(reg)			\
	IS_RREG_OUT(reg)
#define MOV_FROM_ITIR(reg)			\
	IS_RREG_OUT(reg)
#define MOV_FROM_ISR(reg)			\
	IS_RREG_OUT(reg)
#define MOV_FROM_IHA(reg)			\
	IS_RREG_OUT(reg)
#define MOV_FROM_IPSR(pred, reg)		\
	IS_PRED_IN(pred)			\
	IS_RREG_OUT(reg)
#define MOV_FROM_IIM(reg)			\
	IS_RREG_OUT(reg)
#define MOV_FROM_IIP(reg)			\
	IS_RREG_OUT(reg)
#define MOV_FROM_IVR(reg, clob)			\
	IS_RREG_OUT(reg)			\
	IS_RREG_CLOB(clob)
#define MOV_FROM_PSR(pred, reg, clob)		\
	IS_PRED_IN(pred)			\
	IS_RREG_OUT(reg)			\
	IS_RREG_CLOB(clob)
#define MOV_FROM_ITC(pred, pred_clob, reg, clob)	\
	IS_PRED_IN(pred)				\
	IS_PRED_CLOB(pred_clob)				\
	IS_RREG_OUT(reg)				\
	IS_RREG_CLOB(clob)
#define MOV_TO_IFA(reg, clob)			\
	IS_RREG_IN(reg)				\
	IS_RREG_CLOB(clob)
#define MOV_TO_ITIR(pred, reg, clob)		\
	IS_PRED_IN(pred)			\
	IS_RREG_IN(reg)				\
	IS_RREG_CLOB(clob)
#define MOV_TO_IHA(pred, reg, clob)		\
	IS_PRED_IN(pred)			\
	IS_RREG_IN(reg)				\
	IS_RREG_CLOB(clob)
#define MOV_TO_IPSR(pred, reg, clob)		\
	IS_PRED_IN(pred)			\
	IS_RREG_IN(reg)				\
	IS_RREG_CLOB(clob)
#define MOV_TO_IFS(pred, reg, clob)		\
	IS_PRED_IN(pred)			\
	IS_RREG_IN(reg)				\
	IS_RREG_CLOB(clob)
#define MOV_TO_IIP(reg, clob)			\
	IS_RREG_IN(reg)				\
	IS_RREG_CLOB(clob)
#define MOV_TO_KR(kr, reg, clob0, clob1)	\
	IS_RREG_IN(reg)				\
	IS_RREG_CLOB(clob0)			\
	IS_RREG_CLOB(clob1)
#define ITC_I(pred, reg, clob)			\
	IS_PRED_IN(pred)			\
	IS_RREG_IN(reg)				\
	IS_RREG_CLOB(clob)
#define ITC_D(pred, reg, clob)			\
	IS_PRED_IN(pred)			\
	IS_RREG_IN(reg)				\
	IS_RREG_CLOB(clob)
#define ITC_I_AND_D(pred_i, pred_d, reg, clob)	\
	IS_PRED_IN(pred_i)			\
	IS_PRED_IN(pred_d)			\
	IS_RREG_IN(reg)				\
	IS_RREG_CLOB(clob)
#define THASH(pred, reg0, reg1, clob)		\
	IS_PRED_IN(pred)			\
	IS_RREG_OUT(reg0)			\
	IS_RREG_IN(reg1)			\
	IS_RREG_CLOB(clob)
#define SSM_PSR_IC_AND_DEFAULT_BITS_AND_SRLZ_I(clob0, clob1)	\
	IS_RREG_CLOB(clob0)					\
	IS_RREG_CLOB(clob1)
#define SSM_PSR_IC_AND_SRLZ_D(clob0, clob1)	\
	IS_RREG_CLOB(clob0)			\
	IS_RREG_CLOB(clob1)
#define RSM_PSR_IC(clob)			\
	IS_RREG_CLOB(clob)
#define SSM_PSR_I(pred, pred_clob, clob)	\
	IS_PRED_IN(pred)			\
	IS_PRED_CLOB(pred_clob)			\
	IS_RREG_CLOB(clob)
#define RSM_PSR_I(pred, clob0, clob1)		\
	IS_PRED_IN(pred)			\
	IS_RREG_CLOB(clob0)			\
	IS_RREG_CLOB(clob1)
#define RSM_PSR_I_IC(clob0, clob1, clob2)	\
	IS_RREG_CLOB(clob0)			\
	IS_RREG_CLOB(clob1)			\
	IS_RREG_CLOB(clob2)
#define RSM_PSR_DT				\
	nop 0
#define RSM_PSR_BE_I(clob0, clob1)		\
	IS_RREG_CLOB(clob0)			\
	IS_RREG_CLOB(clob1)
#define SSM_PSR_DT_AND_SRLZ_I			\
	nop 0
#define BSW_0(clob0, clob1, clob2)		\
	IS_RREG_CLOB(clob0)			\
	IS_RREG_CLOB(clob1)			\
	IS_RREG_CLOB(clob2)
#define BSW_1(clob0, clob1)			\
	IS_RREG_CLOB(clob0)			\
	IS_RREG_CLOB(clob1)
#define COVER					\
	nop 0
#define RFI					\
	br.ret.sptk.many rp /* defining nop causes dependency error */

#endif /* _ASM_NATIVE_PVCHK_INST_H */
