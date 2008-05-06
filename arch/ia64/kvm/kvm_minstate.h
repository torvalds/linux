/*
 *  kvm_minstate.h: min save macros
 *  Copyright (c) 2007, Intel Corporation.
 *
 *  Xuefei Xu (Anthony Xu) (Anthony.xu@intel.com)
 *  Xiantao Zhang (xiantao.zhang@intel.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */


#include <asm/asmmacro.h>
#include <asm/types.h>
#include <asm/kregs.h>
#include "asm-offsets.h"

#define KVM_MINSTATE_START_SAVE_MIN	     					\
	mov ar.rsc = 0;/* set enforced lazy mode, pl 0, little-endian, loadrs=0 */\
	;;									\
	mov.m r28 = ar.rnat;                                  			\
	addl r22 = VMM_RBS_OFFSET,r1;            /* compute base of RBS */	\
	;;									\
	lfetch.fault.excl.nt1 [r22];						\
	addl r1 = IA64_STK_OFFSET-VMM_PT_REGS_SIZE,r1;  /* compute base of memory stack */  \
	mov r23 = ar.bspstore;			/* save ar.bspstore */          \
	;;									\
	mov ar.bspstore = r22;				/* switch to kernel RBS */\
	;;									\
	mov r18 = ar.bsp;							\
	mov ar.rsc = 0x3;     /* set eager mode, pl 0, little-endian, loadrs=0 */



#define KVM_MINSTATE_END_SAVE_MIN						\
	bsw.1;          /* switch back to bank 1 (must be last in insn group) */\
	;;


#define PAL_VSA_SYNC_READ						\
	/* begin to call pal vps sync_read */				\
	add r25 = VMM_VPD_BASE_OFFSET, r21;				\
	adds r20 = VMM_VCPU_VSA_BASE_OFFSET, r21;  /* entry point */	\
	;;								\
	ld8 r25 = [r25];      /* read vpd base */			\
	ld8 r20 = [r20];						\
	;;								\
	add r20 = PAL_VPS_SYNC_READ,r20;				\
	;;								\
{ .mii;									\
	nop 0x0;							\
	mov r24 = ip;							\
	mov b0 = r20;							\
	;;								\
};									\
{ .mmb;									\
	add r24 = 0x20, r24;						\
	nop 0x0;							\
	br.cond.sptk b0;        /*  call the service */			\
	;;								\
};



#define KVM_MINSTATE_GET_CURRENT(reg)   mov reg=r21

/*
 * KVM_DO_SAVE_MIN switches to the kernel stacks (if necessary) and saves
 * the minimum state necessary that allows us to turn psr.ic back
 * on.
 *
 * Assumed state upon entry:
 *  psr.ic: off
 *  r31:	contains saved predicates (pr)
 *
 * Upon exit, the state is as follows:
 *  psr.ic: off
 *   r2 = points to &pt_regs.r16
 *   r8 = contents of ar.ccv
 *   r9 = contents of ar.csd
 *  r10 = contents of ar.ssd
 *  r11 = FPSR_DEFAULT
 *  r12 = kernel sp (kernel virtual address)
 *  r13 = points to current task_struct (kernel virtual address)
 *  p15 = TRUE if psr.i is set in cr.ipsr
 *  predicate registers (other than p2, p3, and p15), b6, r3, r14, r15:
 *	  preserved
 *
 * Note that psr.ic is NOT turned on by this macro.  This is so that
 * we can pass interruption state as arguments to a handler.
 */


#define PT(f) (VMM_PT_REGS_##f##_OFFSET)

#define KVM_DO_SAVE_MIN(COVER,SAVE_IFS,EXTRA)			\
	KVM_MINSTATE_GET_CURRENT(r16);  /* M (or M;;I) */	\
	mov r27 = ar.rsc;         /* M */			\
	mov r20 = r1;         /* A */				\
	mov r25 = ar.unat;        /* M */			\
	mov r29 = cr.ipsr;        /* M */			\
	mov r26 = ar.pfs;         /* I */			\
	mov r18 = cr.isr;         				\
	COVER;              /* B;; (or nothing) */		\
	;;							\
	tbit.z p0,p15 = r29,IA64_PSR_I_BIT;			\
	mov r1 = r16;						\
/*	mov r21=r16;	*/					\
	/* switch from user to kernel RBS: */			\
	;;							\
	invala;             /* M */				\
	SAVE_IFS;						\
	;;							\
	KVM_MINSTATE_START_SAVE_MIN				\
	adds r17 = 2*L1_CACHE_BYTES,r1;/* cache-line size */	\
	adds r16 = PT(CR_IPSR),r1;				\
	;;							\
	lfetch.fault.excl.nt1 [r17],L1_CACHE_BYTES;		\
	st8 [r16] = r29;      /* save cr.ipsr */		\
	;;							\
	lfetch.fault.excl.nt1 [r17];				\
	tbit.nz p15,p0 = r29,IA64_PSR_I_BIT;			\
	mov r29 = b0						\
	;;							\
	adds r16 = PT(R8),r1; /* initialize first base pointer */\
	adds r17 = PT(R9),r1; /* initialize second base pointer */\
	;;							\
.mem.offset 0,0; st8.spill [r16] = r8,16;			\
.mem.offset 8,0; st8.spill [r17] = r9,16;			\
	;;							\
.mem.offset 0,0; st8.spill [r16] = r10,24;			\
.mem.offset 8,0; st8.spill [r17] = r11,24;			\
	;;							\
	mov r9 = cr.iip;         /* M */			\
	mov r10 = ar.fpsr;        /* M */			\
	;;							\
	st8 [r16] = r9,16;    /* save cr.iip */			\
	st8 [r17] = r30,16;   /* save cr.ifs */			\
	sub r18 = r18,r22;    /* r18=RSE.ndirty*8 */		\
	;;							\
	st8 [r16] = r25,16;   /* save ar.unat */		\
	st8 [r17] = r26,16;    /* save ar.pfs */		\
	shl r18 = r18,16;     /* calu ar.rsc used for "loadrs" */\
	;;							\
	st8 [r16] = r27,16;   /* save ar.rsc */			\
	st8 [r17] = r28,16;   /* save ar.rnat */		\
	;;          /* avoid RAW on r16 & r17 */		\
	st8 [r16] = r23,16;   /* save ar.bspstore */		\
	st8 [r17] = r31,16;   /* save predicates */		\
	;;							\
	st8 [r16] = r29,16;   /* save b0 */			\
	st8 [r17] = r18,16;   /* save ar.rsc value for "loadrs" */\
	;;							\
.mem.offset 0,0; st8.spill [r16] = r20,16;/* save original r1 */  \
.mem.offset 8,0; st8.spill [r17] = r12,16;			\
	adds r12 = -16,r1;    /* switch to kernel memory stack */  \
	;;							\
.mem.offset 0,0; st8.spill [r16] = r13,16;			\
.mem.offset 8,0; st8.spill [r17] = r10,16;	/* save ar.fpsr */\
	mov r13 = r21;   /* establish `current' */		\
	;;							\
.mem.offset 0,0; st8.spill [r16] = r15,16;			\
.mem.offset 8,0; st8.spill [r17] = r14,16;			\
	;;							\
.mem.offset 0,0; st8.spill [r16] = r2,16;			\
.mem.offset 8,0; st8.spill [r17] = r3,16;			\
	adds r2 = VMM_PT_REGS_R16_OFFSET,r1;			\
	 ;;							\
	adds r16 = VMM_VCPU_IIPA_OFFSET,r13;			\
	adds r17 = VMM_VCPU_ISR_OFFSET,r13;			\
	mov r26 = cr.iipa;					\
	mov r27 = cr.isr;					\
	;;							\
	st8 [r16] = r26;					\
	st8 [r17] = r27;					\
	;;							\
	EXTRA;							\
	mov r8 = ar.ccv;					\
	mov r9 = ar.csd;					\
	mov r10 = ar.ssd;					\
	movl r11 = FPSR_DEFAULT;   /* L-unit */			\
	adds r17 = VMM_VCPU_GP_OFFSET,r13;			\
	;;							\
	ld8 r1 = [r17];/* establish kernel global pointer */	\
	;;							\
	PAL_VSA_SYNC_READ					\
	KVM_MINSTATE_END_SAVE_MIN

/*
 * SAVE_REST saves the remainder of pt_regs (with psr.ic on).
 *
 * Assumed state upon entry:
 *  psr.ic: on
 *  r2: points to &pt_regs.f6
 *  r3: points to &pt_regs.f7
 *  r8: contents of ar.ccv
 *  r9: contents of ar.csd
 *  r10:	contents of ar.ssd
 *  r11:	FPSR_DEFAULT
 *
 * Registers r14 and r15 are guaranteed not to be touched by SAVE_REST.
 */
#define KVM_SAVE_REST				\
.mem.offset 0,0; st8.spill [r2] = r16,16;	\
.mem.offset 8,0; st8.spill [r3] = r17,16;	\
	;;				\
.mem.offset 0,0; st8.spill [r2] = r18,16;	\
.mem.offset 8,0; st8.spill [r3] = r19,16;	\
	;;				\
.mem.offset 0,0; st8.spill [r2] = r20,16;	\
.mem.offset 8,0; st8.spill [r3] = r21,16;	\
	mov r18=b6;			\
	;;				\
.mem.offset 0,0; st8.spill [r2] = r22,16;	\
.mem.offset 8,0; st8.spill [r3] = r23,16;	\
	mov r19 = b7;				\
	;;					\
.mem.offset 0,0; st8.spill [r2] = r24,16;	\
.mem.offset 8,0; st8.spill [r3] = r25,16;	\
	;;					\
.mem.offset 0,0; st8.spill [r2] = r26,16;	\
.mem.offset 8,0; st8.spill [r3] = r27,16;	\
	;;					\
.mem.offset 0,0; st8.spill [r2] = r28,16;	\
.mem.offset 8,0; st8.spill [r3] = r29,16;	\
	;;					\
.mem.offset 0,0; st8.spill [r2] = r30,16;	\
.mem.offset 8,0; st8.spill [r3] = r31,32;	\
	;;					\
	mov ar.fpsr = r11;			\
	st8 [r2] = r8,8;			\
	adds r24 = PT(B6)-PT(F7),r3;		\
	adds r25 = PT(B7)-PT(F7),r3;		\
	;;					\
	st8 [r24] = r18,16;       /* b6 */	\
	st8 [r25] = r19,16;       /* b7 */	\
	adds r2 = PT(R4)-PT(F6),r2;		\
	adds r3 = PT(R5)-PT(F7),r3;		\
	;;					\
	st8 [r24] = r9;	/* ar.csd */		\
	st8 [r25] = r10;	/* ar.ssd */	\
	;;					\
	mov r18 = ar.unat;			\
	adds r19 = PT(EML_UNAT)-PT(R4),r2;	\
	;;					\
	st8 [r19] = r18; /* eml_unat */ 	\


#define KVM_SAVE_EXTRA				\
.mem.offset 0,0; st8.spill [r2] = r4,16;	\
.mem.offset 8,0; st8.spill [r3] = r5,16;	\
	;;					\
.mem.offset 0,0; st8.spill [r2] = r6,16;	\
.mem.offset 8,0; st8.spill [r3] = r7;		\
	;;					\
	mov r26 = ar.unat;			\
	;;					\
	st8 [r2] = r26;/* eml_unat */ 		\

#define KVM_SAVE_MIN_WITH_COVER		KVM_DO_SAVE_MIN(cover, mov r30 = cr.ifs,)
#define KVM_SAVE_MIN_WITH_COVER_R19	KVM_DO_SAVE_MIN(cover, mov r30 = cr.ifs, mov r15 = r19)
#define KVM_SAVE_MIN			KVM_DO_SAVE_MIN(     , mov r30 = r0, )
