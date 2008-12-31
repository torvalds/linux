/******************************************************************************
 * arch/ia64/include/asm/native/inst.h
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

#define DO_SAVE_MIN		IA64_NATIVE_DO_SAVE_MIN

#define __paravirt_switch_to			ia64_native_switch_to
#define __paravirt_leave_syscall		ia64_native_leave_syscall
#define __paravirt_work_processed_syscall	ia64_native_work_processed_syscall
#define __paravirt_leave_kernel			ia64_native_leave_kernel
#define __paravirt_pending_syscall_end		ia64_work_pending_syscall_end
#define __paravirt_work_processed_syscall_target \
						ia64_work_processed_syscall

#ifdef CONFIG_PARAVIRT_GUEST_ASM_CLOBBER_CHECK
# define PARAVIRT_POISON	0xdeadbeefbaadf00d
# define CLOBBER(clob)				\
	;;					\
	movl clob = PARAVIRT_POISON;		\
	;;
# define CLOBBER_PRED(pred_clob)		\
	;;					\
	cmp.eq pred_clob, p0 = r0, r0		\
	;;
#else
# define CLOBBER(clob)			/* nothing */
# define CLOBBER_PRED(pred_clob)	/* nothing */
#endif

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
	mov reg = cr.ivr	\
	CLOBBER(clob)

#define MOV_FROM_PSR(pred, reg, clob)	\
(pred)	mov reg = psr			\
	CLOBBER(clob)

#define MOV_TO_IFA(reg, clob)	\
	mov cr.ifa = reg	\
	CLOBBER(clob)

#define MOV_TO_ITIR(pred, reg, clob)	\
(pred)	mov cr.itir = reg		\
	CLOBBER(clob)

#define MOV_TO_IHA(pred, reg, clob)	\
(pred)	mov cr.iha = reg		\
	CLOBBER(clob)

#define MOV_TO_IPSR(pred, reg, clob)		\
(pred)	mov cr.ipsr = reg			\
	CLOBBER(clob)

#define MOV_TO_IFS(pred, reg, clob)	\
(pred)	mov cr.ifs = reg		\
	CLOBBER(clob)

#define MOV_TO_IIP(reg, clob)	\
	mov cr.iip = reg	\
	CLOBBER(clob)

#define MOV_TO_KR(kr, reg, clob0, clob1)	\
	mov IA64_KR(kr) = reg			\
	CLOBBER(clob0)				\
	CLOBBER(clob1)

#define ITC_I(pred, reg, clob)	\
(pred)	itc.i reg		\
	CLOBBER(clob)

#define ITC_D(pred, reg, clob)	\
(pred)	itc.d reg		\
	CLOBBER(clob)

#define ITC_I_AND_D(pred_i, pred_d, reg, clob)	\
(pred_i) itc.i reg;				\
(pred_d) itc.d reg				\
	CLOBBER(clob)

#define THASH(pred, reg0, reg1, clob)		\
(pred)	thash reg0 = reg1			\
	CLOBBER(clob)

#define SSM_PSR_IC_AND_DEFAULT_BITS_AND_SRLZ_I(clob0, clob1)		\
	ssm psr.ic | PSR_DEFAULT_BITS					\
	CLOBBER(clob0)							\
	CLOBBER(clob1)							\
	;;								\
	srlz.i /* guarantee that interruption collectin is on */	\
	;;

#define SSM_PSR_IC_AND_SRLZ_D(clob0, clob1)	\
	ssm psr.ic				\
	CLOBBER(clob0)				\
	CLOBBER(clob1)				\
	;;					\
	srlz.d

#define RSM_PSR_IC(clob)	\
	rsm psr.ic		\
	CLOBBER(clob)

#define SSM_PSR_I(pred, pred_clob, clob)	\
(pred)	ssm psr.i				\
	CLOBBER(clob)				\
	CLOBBER_PRED(pred_clob)

#define RSM_PSR_I(pred, clob0, clob1)	\
(pred)	rsm psr.i			\
	CLOBBER(clob0)			\
	CLOBBER(clob1)

#define RSM_PSR_I_IC(clob0, clob1, clob2)	\
	rsm psr.i | psr.ic			\
	CLOBBER(clob0)				\
	CLOBBER(clob1)				\
	CLOBBER(clob2)

#define RSM_PSR_DT		\
	rsm psr.dt

#define SSM_PSR_DT_AND_SRLZ_I	\
	ssm psr.dt		\
	;;			\
	srlz.i

#define BSW_0(clob0, clob1, clob2)	\
	bsw.0				\
	CLOBBER(clob0)			\
	CLOBBER(clob1)			\
	CLOBBER(clob2)

#define BSW_1(clob0, clob1)	\
	bsw.1			\
	CLOBBER(clob0)		\
	CLOBBER(clob1)

#define COVER	\
	cover

#define RFI	\
	rfi
