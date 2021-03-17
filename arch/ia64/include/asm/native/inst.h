/* SPDX-License-Identifier: GPL-2.0-or-later */
/******************************************************************************
 * arch/ia64/include/asm/native/inst.h
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 */

#define DO_SAVE_MIN		IA64_NATIVE_DO_SAVE_MIN

#define MOV_FROM_IFA(reg)	\
	mov reg = cr.ifa

#define MOV_FROM_ITIR(reg)	\
	mov reg = cr.itir

#define MOV_FROM_ISR(reg)	\
	mov reg = cr.isr

#define MOV_FROM_IHA(reg)	\
	mov reg = cr.iha

#define MOV_FROM_IPSR(pred, reg)	\
(pred)	mov reg = cr.ipsr

#define MOV_FROM_IIM(reg)	\
	mov reg = cr.iim

#define MOV_FROM_IIP(reg)	\
	mov reg = cr.iip

#define MOV_FROM_IVR(reg, clob)	\
	mov reg = cr.ivr

#define MOV_FROM_PSR(pred, reg, clob)	\
(pred)	mov reg = psr

#define MOV_FROM_ITC(pred, pred_clob, reg, clob)	\
(pred)	mov reg = ar.itc

#define MOV_TO_IFA(reg, clob)	\
	mov cr.ifa = reg

#define MOV_TO_ITIR(pred, reg, clob)	\
(pred)	mov cr.itir = reg

#define MOV_TO_IHA(pred, reg, clob)	\
(pred)	mov cr.iha = reg

#define MOV_TO_IPSR(pred, reg, clob)		\
(pred)	mov cr.ipsr = reg

#define MOV_TO_IFS(pred, reg, clob)	\
(pred)	mov cr.ifs = reg

#define MOV_TO_IIP(reg, clob)	\
	mov cr.iip = reg

#define MOV_TO_KR(kr, reg, clob0, clob1)	\
	mov IA64_KR(kr) = reg

#define ITC_I(pred, reg, clob)	\
(pred)	itc.i reg

#define ITC_D(pred, reg, clob)	\
(pred)	itc.d reg

#define ITC_I_AND_D(pred_i, pred_d, reg, clob)	\
(pred_i) itc.i reg;				\
(pred_d) itc.d reg

#define THASH(pred, reg0, reg1, clob)		\
(pred)	thash reg0 = reg1

#define SSM_PSR_IC_AND_DEFAULT_BITS_AND_SRLZ_I(clob0, clob1)		\
	ssm psr.ic | PSR_DEFAULT_BITS					\
	;;								\
	srlz.i /* guarantee that interruption collectin is on */	\
	;;

#define SSM_PSR_IC_AND_SRLZ_D(clob0, clob1)	\
	ssm psr.ic				\
	;;					\
	srlz.d

#define RSM_PSR_IC(clob)	\
	rsm psr.ic

#define SSM_PSR_I(pred, pred_clob, clob)	\
(pred)	ssm psr.i

#define RSM_PSR_I(pred, clob0, clob1)	\
(pred)	rsm psr.i

#define RSM_PSR_I_IC(clob0, clob1, clob2)	\
	rsm psr.i | psr.ic

#define RSM_PSR_DT		\
	rsm psr.dt

#define RSM_PSR_BE_I(clob0, clob1)	\
	rsm psr.be | psr.i

#define SSM_PSR_DT_AND_SRLZ_I	\
	ssm psr.dt		\
	;;			\
	srlz.i

#define BSW_0(clob0, clob1, clob2)	\
	bsw.0

#define BSW_1(clob0, clob1)	\
	bsw.1

#define COVER	\
	cover

#define RFI	\
	rfi
