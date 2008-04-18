
#include <asm/cache.h>

#include "entry.h"

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
/* read ar.itc in advance, and use it before leaving bank 0 */
#define ACCOUNT_GET_STAMP				\
(pUStk) mov.m r20=ar.itc;
#define ACCOUNT_SYS_ENTER				\
(pUStk) br.call.spnt rp=account_sys_enter		\
	;;
#else
#define ACCOUNT_GET_STAMP
#define ACCOUNT_SYS_ENTER
#endif

/*
 * DO_SAVE_MIN switches to the kernel stacks (if necessary) and saves
 * the minimum state necessary that allows us to turn psr.ic back
 * on.
 *
 * Assumed state upon entry:
 *	psr.ic: off
 *	r31:	contains saved predicates (pr)
 *
 * Upon exit, the state is as follows:
 *	psr.ic: off
 *	 r2 = points to &pt_regs.r16
 *	 r8 = contents of ar.ccv
 *	 r9 = contents of ar.csd
 *	r10 = contents of ar.ssd
 *	r11 = FPSR_DEFAULT
 *	r12 = kernel sp (kernel virtual address)
 *	r13 = points to current task_struct (kernel virtual address)
 *	p15 = TRUE if psr.i is set in cr.ipsr
 *	predicate registers (other than p2, p3, and p15), b6, r3, r14, r15:
 *		preserved
 *
 * Note that psr.ic is NOT turned on by this macro.  This is so that
 * we can pass interruption state as arguments to a handler.
 */
#define DO_SAVE_MIN(COVER,SAVE_IFS,EXTRA)							\
	mov r16=IA64_KR(CURRENT);	/* M */							\
	mov r27=ar.rsc;			/* M */							\
	mov r20=r1;			/* A */							\
	mov r25=ar.unat;		/* M */							\
	mov r29=cr.ipsr;		/* M */							\
	mov r26=ar.pfs;			/* I */							\
	mov r28=cr.iip;			/* M */							\
	mov r21=ar.fpsr;		/* M */							\
	COVER;				/* B;; (or nothing) */					\
	;;											\
	adds r16=IA64_TASK_THREAD_ON_USTACK_OFFSET,r16;						\
	;;											\
	ld1 r17=[r16];				/* load current->thread.on_ustack flag */	\
	st1 [r16]=r0;				/* clear current->thread.on_ustack flag */	\
	adds r1=-IA64_TASK_THREAD_ON_USTACK_OFFSET,r16						\
	/* switch from user to kernel RBS: */							\
	;;											\
	invala;				/* M */							\
	SAVE_IFS;										\
	cmp.eq pKStk,pUStk=r0,r17;		/* are we in kernel mode already? */		\
	;;											\
(pUStk)	mov ar.rsc=0;		/* set enforced lazy mode, pl 0, little-endian, loadrs=0 */	\
	;;											\
(pUStk)	mov.m r24=ar.rnat;									\
(pUStk)	addl r22=IA64_RBS_OFFSET,r1;			/* compute base of RBS */		\
(pKStk) mov r1=sp;					/* get sp  */				\
	;;											\
(pUStk) lfetch.fault.excl.nt1 [r22];								\
(pUStk)	addl r1=IA64_STK_OFFSET-IA64_PT_REGS_SIZE,r1;	/* compute base of memory stack */	\
(pUStk)	mov r23=ar.bspstore;				/* save ar.bspstore */			\
	;;											\
(pUStk)	mov ar.bspstore=r22;				/* switch to kernel RBS */		\
(pKStk) addl r1=-IA64_PT_REGS_SIZE,r1;			/* if in kernel mode, use sp (r12) */	\
	;;											\
(pUStk)	mov r18=ar.bsp;										\
(pUStk)	mov ar.rsc=0x3;		/* set eager mode, pl 0, little-endian, loadrs=0 */		\
	adds r17=2*L1_CACHE_BYTES,r1;		/* really: biggest cache-line size */		\
	adds r16=PT(CR_IPSR),r1;								\
	;;											\
	lfetch.fault.excl.nt1 [r17],L1_CACHE_BYTES;						\
	st8 [r16]=r29;		/* save cr.ipsr */						\
	;;											\
	lfetch.fault.excl.nt1 [r17];								\
	tbit.nz p15,p0=r29,IA64_PSR_I_BIT;							\
	mov r29=b0										\
	;;											\
	adds r16=PT(R8),r1;	/* initialize first base pointer */				\
	adds r17=PT(R9),r1;	/* initialize second base pointer */				\
(pKStk)	mov r18=r0;		/* make sure r18 isn't NaT */					\
	;;											\
.mem.offset 0,0; st8.spill [r16]=r8,16;								\
.mem.offset 8,0; st8.spill [r17]=r9,16;								\
        ;;											\
.mem.offset 0,0; st8.spill [r16]=r10,24;							\
.mem.offset 8,0; st8.spill [r17]=r11,24;							\
        ;;											\
	st8 [r16]=r28,16;	/* save cr.iip */						\
	st8 [r17]=r30,16;	/* save cr.ifs */						\
(pUStk)	sub r18=r18,r22;	/* r18=RSE.ndirty*8 */						\
	mov r8=ar.ccv;										\
	mov r9=ar.csd;										\
	mov r10=ar.ssd;										\
	movl r11=FPSR_DEFAULT;   /* L-unit */							\
	;;											\
	st8 [r16]=r25,16;	/* save ar.unat */						\
	st8 [r17]=r26,16;	/* save ar.pfs */						\
	shl r18=r18,16;		/* compute ar.rsc to be used for "loadrs" */			\
	;;											\
	st8 [r16]=r27,16;	/* save ar.rsc */						\
(pUStk)	st8 [r17]=r24,16;	/* save ar.rnat */						\
(pKStk)	adds r17=16,r17;	/* skip over ar_rnat field */					\
	;;			/* avoid RAW on r16 & r17 */					\
(pUStk)	st8 [r16]=r23,16;	/* save ar.bspstore */						\
	st8 [r17]=r31,16;	/* save predicates */						\
(pKStk)	adds r16=16,r16;	/* skip over ar_bspstore field */				\
	;;											\
	st8 [r16]=r29,16;	/* save b0 */							\
	st8 [r17]=r18,16;	/* save ar.rsc value for "loadrs" */				\
	cmp.eq pNonSys,pSys=r0,r0	/* initialize pSys=0, pNonSys=1 */			\
	;;											\
.mem.offset 0,0; st8.spill [r16]=r20,16;	/* save original r1 */				\
.mem.offset 8,0; st8.spill [r17]=r12,16;							\
	adds r12=-16,r1;	/* switch to kernel memory stack (with 16 bytes of scratch) */	\
	;;											\
.mem.offset 0,0; st8.spill [r16]=r13,16;							\
.mem.offset 8,0; st8.spill [r17]=r21,16;	/* save ar.fpsr */				\
	mov r13=IA64_KR(CURRENT);	/* establish `current' */				\
	;;											\
.mem.offset 0,0; st8.spill [r16]=r15,16;							\
.mem.offset 8,0; st8.spill [r17]=r14,16;							\
	;;											\
.mem.offset 0,0; st8.spill [r16]=r2,16;								\
.mem.offset 8,0; st8.spill [r17]=r3,16;								\
	ACCOUNT_GET_STAMP									\
	adds r2=IA64_PT_REGS_R16_OFFSET,r1;							\
	;;											\
	EXTRA;											\
	movl r1=__gp;		/* establish kernel global pointer */				\
	;;											\
	ACCOUNT_SYS_ENTER									\
	bsw.1;			/* switch back to bank 1 (must be last in insn group) */	\
	;;

/*
 * SAVE_REST saves the remainder of pt_regs (with psr.ic on).
 *
 * Assumed state upon entry:
 *	psr.ic: on
 *	r2:	points to &pt_regs.r16
 *	r3:	points to &pt_regs.r17
 *	r8:	contents of ar.ccv
 *	r9:	contents of ar.csd
 *	r10:	contents of ar.ssd
 *	r11:	FPSR_DEFAULT
 *
 * Registers r14 and r15 are guaranteed not to be touched by SAVE_REST.
 */
#define SAVE_REST				\
.mem.offset 0,0; st8.spill [r2]=r16,16;		\
.mem.offset 8,0; st8.spill [r3]=r17,16;		\
	;;					\
.mem.offset 0,0; st8.spill [r2]=r18,16;		\
.mem.offset 8,0; st8.spill [r3]=r19,16;		\
	;;					\
.mem.offset 0,0; st8.spill [r2]=r20,16;		\
.mem.offset 8,0; st8.spill [r3]=r21,16;		\
	mov r18=b6;				\
	;;					\
.mem.offset 0,0; st8.spill [r2]=r22,16;		\
.mem.offset 8,0; st8.spill [r3]=r23,16;		\
	mov r19=b7;				\
	;;					\
.mem.offset 0,0; st8.spill [r2]=r24,16;		\
.mem.offset 8,0; st8.spill [r3]=r25,16;		\
	;;					\
.mem.offset 0,0; st8.spill [r2]=r26,16;		\
.mem.offset 8,0; st8.spill [r3]=r27,16;		\
	;;					\
.mem.offset 0,0; st8.spill [r2]=r28,16;		\
.mem.offset 8,0; st8.spill [r3]=r29,16;		\
	;;					\
.mem.offset 0,0; st8.spill [r2]=r30,16;		\
.mem.offset 8,0; st8.spill [r3]=r31,32;		\
	;;					\
	mov ar.fpsr=r11;	/* M-unit */	\
	st8 [r2]=r8,8;		/* ar.ccv */	\
	adds r24=PT(B6)-PT(F7),r3;		\
	;;					\
	stf.spill [r2]=f6,32;			\
	stf.spill [r3]=f7,32;			\
	;;					\
	stf.spill [r2]=f8,32;			\
	stf.spill [r3]=f9,32;			\
	;;					\
	stf.spill [r2]=f10;			\
	stf.spill [r3]=f11;			\
	adds r25=PT(B7)-PT(F11),r3;		\
	;;					\
	st8 [r24]=r18,16;       /* b6 */	\
	st8 [r25]=r19,16;       /* b7 */	\
	;;					\
	st8 [r24]=r9;        	/* ar.csd */	\
	st8 [r25]=r10;      	/* ar.ssd */	\
	;;

#define SAVE_MIN_WITH_COVER	DO_SAVE_MIN(cover, mov r30=cr.ifs,)
#define SAVE_MIN_WITH_COVER_R19	DO_SAVE_MIN(cover, mov r30=cr.ifs, mov r15=r19)
#define SAVE_MIN		DO_SAVE_MIN(     , mov r30=r0, )
