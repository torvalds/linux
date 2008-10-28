/******************************************************************************
 * arch/ia64/include/asm/xen/inst.h
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

#include <asm/xen/privop.h>

#define ia64_ivt				xen_ivt
#define DO_SAVE_MIN				XEN_DO_SAVE_MIN

#define __paravirt_switch_to			xen_switch_to
#define __paravirt_leave_syscall		xen_leave_syscall
#define __paravirt_work_processed_syscall	xen_work_processed_syscall
#define __paravirt_leave_kernel			xen_leave_kernel
#define __paravirt_pending_syscall_end		xen_work_pending_syscall_end
#define __paravirt_work_processed_syscall_target \
						xen_work_processed_syscall

#define MOV_FROM_IFA(reg)	\
	movl reg = XSI_IFA;	\
	;;			\
	ld8 reg = [reg]

#define MOV_FROM_ITIR(reg)	\
	movl reg = XSI_ITIR;	\
	;;			\
	ld8 reg = [reg]

#define MOV_FROM_ISR(reg)	\
	movl reg = XSI_ISR;	\
	;;			\
	ld8 reg = [reg]

#define MOV_FROM_IHA(reg)	\
	movl reg = XSI_IHA;	\
	;;			\
	ld8 reg = [reg]

#define MOV_FROM_IPSR(pred, reg)	\
(pred)	movl reg = XSI_IPSR;		\
	;;				\
(pred)	ld8 reg = [reg]

#define MOV_FROM_IIM(reg)	\
	movl reg = XSI_IIM;	\
	;;			\
	ld8 reg = [reg]

#define MOV_FROM_IIP(reg)	\
	movl reg = XSI_IIP;	\
	;;			\
	ld8 reg = [reg]

.macro __MOV_FROM_IVR reg, clob
	.ifc "\reg", "r8"
		XEN_HYPER_GET_IVR
		.exitm
	.endif
	.ifc "\clob", "r8"
		XEN_HYPER_GET_IVR
		;;
		mov \reg = r8
		.exitm
	.endif

	mov \clob = r8
	;;
	XEN_HYPER_GET_IVR
	;;
	mov \reg = r8
	;;
	mov r8 = \clob
.endm
#define MOV_FROM_IVR(reg, clob)	__MOV_FROM_IVR reg, clob

.macro __MOV_FROM_PSR pred, reg, clob
	.ifc "\reg", "r8"
		(\pred)	XEN_HYPER_GET_PSR;
		.exitm
	.endif
	.ifc "\clob", "r8"
		(\pred)	XEN_HYPER_GET_PSR
		;;
		(\pred)	mov \reg = r8
		.exitm
	.endif

	(\pred)	mov \clob = r8
	(\pred)	XEN_HYPER_GET_PSR
	;;
	(\pred)	mov \reg = r8
	(\pred)	mov r8 = \clob
.endm
#define MOV_FROM_PSR(pred, reg, clob)	__MOV_FROM_PSR pred, reg, clob


#define MOV_TO_IFA(reg, clob)	\
	movl clob = XSI_IFA;	\
	;;			\
	st8 [clob] = reg	\

#define MOV_TO_ITIR(pred, reg, clob)	\
(pred)	movl clob = XSI_ITIR;		\
	;;				\
(pred)	st8 [clob] = reg

#define MOV_TO_IHA(pred, reg, clob)	\
(pred)	movl clob = XSI_IHA;		\
	;;				\
(pred)	st8 [clob] = reg

#define MOV_TO_IPSR(pred, reg, clob)	\
(pred)	movl clob = XSI_IPSR;		\
	;;				\
(pred)	st8 [clob] = reg;		\
	;;

#define MOV_TO_IFS(pred, reg, clob)	\
(pred)	movl clob = XSI_IFS;		\
	;;				\
(pred)	st8 [clob] = reg;		\
	;;

#define MOV_TO_IIP(reg, clob)	\
	movl clob = XSI_IIP;	\
	;;			\
	st8 [clob] = reg

.macro ____MOV_TO_KR kr, reg, clob0, clob1
	.ifc "\clob0", "r9"
		.error "clob0 \clob0 must not be r9"
	.endif
	.ifc "\clob1", "r8"
		.error "clob1 \clob1 must not be r8"
	.endif

	.ifnc "\reg", "r9"
		.ifnc "\clob1", "r9"
			mov \clob1 = r9
		.endif
		mov r9 = \reg
	.endif
	.ifnc "\clob0", "r8"
		mov \clob0 = r8
	.endif
	mov r8 = \kr
	;;
	XEN_HYPER_SET_KR

	.ifnc "\reg", "r9"
		.ifnc "\clob1", "r9"
			mov r9 = \clob1
		.endif
	.endif
	.ifnc "\clob0", "r8"
		mov r8 = \clob0
	.endif
.endm

.macro __MOV_TO_KR kr, reg, clob0, clob1
	.ifc "\clob0", "r9"
		____MOV_TO_KR \kr, \reg, \clob1, \clob0
		.exitm
	.endif
	.ifc "\clob1", "r8"
		____MOV_TO_KR \kr, \reg, \clob1, \clob0
		.exitm
	.endif

	____MOV_TO_KR \kr, \reg, \clob0, \clob1
.endm

#define MOV_TO_KR(kr, reg, clob0, clob1) \
	__MOV_TO_KR IA64_KR_ ## kr, reg, clob0, clob1


.macro __ITC_I pred, reg, clob
	.ifc "\reg", "r8"
		(\pred)	XEN_HYPER_ITC_I
		.exitm
	.endif
	.ifc "\clob", "r8"
		(\pred)	mov r8 = \reg
		;;
		(\pred)	XEN_HYPER_ITC_I
		.exitm
	.endif

	(\pred)	mov \clob = r8
	(\pred)	mov r8 = \reg
	;;
	(\pred)	XEN_HYPER_ITC_I
	;;
	(\pred)	mov r8 = \clob
	;;
.endm
#define ITC_I(pred, reg, clob)	__ITC_I pred, reg, clob

.macro __ITC_D pred, reg, clob
	.ifc "\reg", "r8"
		(\pred)	XEN_HYPER_ITC_D
		;;
		.exitm
	.endif
	.ifc "\clob", "r8"
		(\pred)	mov r8 = \reg
		;;
		(\pred)	XEN_HYPER_ITC_D
		;;
		.exitm
	.endif

	(\pred)	mov \clob = r8
	(\pred)	mov r8 = \reg
	;;
	(\pred)	XEN_HYPER_ITC_D
	;;
	(\pred)	mov r8 = \clob
	;;
.endm
#define ITC_D(pred, reg, clob)	__ITC_D pred, reg, clob

.macro __ITC_I_AND_D pred_i, pred_d, reg, clob
	.ifc "\reg", "r8"
		(\pred_i)XEN_HYPER_ITC_I
		;;
		(\pred_d)XEN_HYPER_ITC_D
		;;
		.exitm
	.endif
	.ifc "\clob", "r8"
		mov r8 = \reg
		;;
		(\pred_i)XEN_HYPER_ITC_I
		;;
		(\pred_d)XEN_HYPER_ITC_D
		;;
		.exitm
	.endif

	mov \clob = r8
	mov r8 = \reg
	;;
	(\pred_i)XEN_HYPER_ITC_I
	;;
	(\pred_d)XEN_HYPER_ITC_D
	;;
	mov r8 = \clob
	;;
.endm
#define ITC_I_AND_D(pred_i, pred_d, reg, clob) \
	__ITC_I_AND_D pred_i, pred_d, reg, clob

.macro __THASH pred, reg0, reg1, clob
	.ifc "\reg0", "r8"
		(\pred)	mov r8 = \reg1
		(\pred)	XEN_HYPER_THASH
		.exitm
	.endc
	.ifc "\reg1", "r8"
		(\pred)	XEN_HYPER_THASH
		;;
		(\pred)	mov \reg0 = r8
		;;
		.exitm
	.endif
	.ifc "\clob", "r8"
		(\pred)	mov r8 = \reg1
		(\pred)	XEN_HYPER_THASH
		;;
		(\pred)	mov \reg0 = r8
		;;
		.exitm
	.endif

	(\pred)	mov \clob = r8
	(\pred)	mov r8 = \reg1
	(\pred)	XEN_HYPER_THASH
	;;
	(\pred)	mov \reg0 = r8
	(\pred)	mov r8 = \clob
	;;
.endm
#define THASH(pred, reg0, reg1, clob) __THASH pred, reg0, reg1, clob

#define SSM_PSR_IC_AND_DEFAULT_BITS_AND_SRLZ_I(clob0, clob1)	\
	mov clob0 = 1;						\
	movl clob1 = XSI_PSR_IC;				\
	;;							\
	st4 [clob1] = clob0					\
	;;

#define SSM_PSR_IC_AND_SRLZ_D(clob0, clob1)	\
	;;					\
	srlz.d;					\
	mov clob1 = 1;				\
	movl clob0 = XSI_PSR_IC;		\
	;;					\
	st4 [clob0] = clob1

#define RSM_PSR_IC(clob)	\
	movl clob = XSI_PSR_IC;	\
	;;			\
	st4 [clob] = r0;	\
	;;

/* pred will be clobbered */
#define MASK_TO_PEND_OFS    (-1)
#define SSM_PSR_I(pred, pred_clob, clob)				\
(pred)	movl clob = XSI_PSR_I_ADDR					\
	;;								\
(pred)	ld8 clob = [clob]						\
	;;								\
	/* if (pred) vpsr.i = 1 */					\
	/* if (pred) (vcpu->vcpu_info->evtchn_upcall_mask)=0 */		\
(pred)	st1 [clob] = r0, MASK_TO_PEND_OFS				\
	;;								\
	/* if (vcpu->vcpu_info->evtchn_upcall_pending) */		\
(pred)	ld1 clob = [clob]						\
	;;								\
(pred)	cmp.ne.unc pred_clob, p0 = clob, r0				\
	;;								\
(pred_clob)XEN_HYPER_SSM_I	/* do areal ssm psr.i */

#define RSM_PSR_I(pred, clob0, clob1)	\
	movl clob0 = XSI_PSR_I_ADDR;	\
	mov clob1 = 1;			\
	;;				\
	ld8 clob0 = [clob0];		\
	;;				\
(pred)	st1 [clob0] = clob1

#define RSM_PSR_I_IC(clob0, clob1, clob2)		\
	movl clob0 = XSI_PSR_I_ADDR;			\
	movl clob1 = XSI_PSR_IC;			\
	;;						\
	ld8 clob0 = [clob0];				\
	mov clob2 = 1;					\
	;;						\
	/* note: clears both vpsr.i and vpsr.ic! */	\
	st1 [clob0] = clob2;				\
	st4 [clob1] = r0;				\
	;;

#define RSM_PSR_DT		\
	XEN_HYPER_RSM_PSR_DT

#define SSM_PSR_DT_AND_SRLZ_I	\
	XEN_HYPER_SSM_PSR_DT

#define BSW_0(clob0, clob1, clob2)			\
	;;						\
	/* r16-r31 all now hold bank1 values */		\
	mov clob2 = ar.unat;				\
	movl clob0 = XSI_BANK1_R16;			\
	movl clob1 = XSI_BANK1_R16 + 8;			\
	;;						\
.mem.offset 0, 0; st8.spill [clob0] = r16, 16;		\
.mem.offset 8, 0; st8.spill [clob1] = r17, 16;		\
	;;						\
.mem.offset 0, 0; st8.spill [clob0] = r18, 16;		\
.mem.offset 8, 0; st8.spill [clob1] = r19, 16;		\
	;;						\
.mem.offset 0, 0; st8.spill [clob0] = r20, 16;		\
.mem.offset 8, 0; st8.spill [clob1] = r21, 16;		\
	;;						\
.mem.offset 0, 0; st8.spill [clob0] = r22, 16;		\
.mem.offset 8, 0; st8.spill [clob1] = r23, 16;		\
	;;						\
.mem.offset 0, 0; st8.spill [clob0] = r24, 16;		\
.mem.offset 8, 0; st8.spill [clob1] = r25, 16;		\
	;;						\
.mem.offset 0, 0; st8.spill [clob0] = r26, 16;		\
.mem.offset 8, 0; st8.spill [clob1] = r27, 16;		\
	;;						\
.mem.offset 0, 0; st8.spill [clob0] = r28, 16;		\
.mem.offset 8, 0; st8.spill [clob1] = r29, 16;		\
	;;						\
.mem.offset 0, 0; st8.spill [clob0] = r30, 16;		\
.mem.offset 8, 0; st8.spill [clob1] = r31, 16;		\
	;;						\
	mov clob1 = ar.unat;				\
	movl clob0 = XSI_B1NAT;				\
	;;						\
	st8 [clob0] = clob1;				\
	mov ar.unat = clob2;				\
	movl clob0 = XSI_BANKNUM;			\
	;;						\
	st4 [clob0] = r0


	/* FIXME: THIS CODE IS NOT NaT SAFE! */
#define XEN_BSW_1(clob)			\
	mov clob = ar.unat;		\
	movl r30 = XSI_B1NAT;		\
	;;				\
	ld8 r30 = [r30];		\
	mov r31 = 1;			\
	;;				\
	mov ar.unat = r30;		\
	movl r30 = XSI_BANKNUM;		\
	;;				\
	st4 [r30] = r31;		\
	movl r30 = XSI_BANK1_R16;	\
	movl r31 = XSI_BANK1_R16+8;	\
	;;				\
	ld8.fill r16 = [r30], 16;	\
	ld8.fill r17 = [r31], 16;	\
	;;				\
	ld8.fill r18 = [r30], 16;	\
	ld8.fill r19 = [r31], 16;	\
	;;				\
	ld8.fill r20 = [r30], 16;	\
	ld8.fill r21 = [r31], 16;	\
	;;				\
	ld8.fill r22 = [r30], 16;	\
	ld8.fill r23 = [r31], 16;	\
	;;				\
	ld8.fill r24 = [r30], 16;	\
	ld8.fill r25 = [r31], 16;	\
	;;				\
	ld8.fill r26 = [r30], 16;	\
	ld8.fill r27 = [r31], 16;	\
	;;				\
	ld8.fill r28 = [r30], 16;	\
	ld8.fill r29 = [r31], 16;	\
	;;				\
	ld8.fill r30 = [r30];		\
	ld8.fill r31 = [r31];		\
	;;				\
	mov ar.unat = clob

#define BSW_1(clob0, clob1)	XEN_BSW_1(clob1)


#define COVER	\
	XEN_HYPER_COVER

#define RFI			\
	XEN_HYPER_RFI;		\
	dv_serialize_data
