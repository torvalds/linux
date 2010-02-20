/*
 * This file contains the code that gets mapped at the upper end of each task's text
 * region.  For now, it contains the signal trampoline code only.
 *
 * Copyright (C) 1999-2003 Hewlett-Packard Co
 * 	David Mosberger-Tang <davidm@hpl.hp.com>
 */


#include <asm/asmmacro.h>
#include <asm/errno.h>
#include <asm/asm-offsets.h>
#include <asm/sigcontext.h>
#include <asm/system.h>
#include <asm/unistd.h>
#include "paravirt_inst.h"

/*
 * We can't easily refer to symbols inside the kernel.  To avoid full runtime relocation,
 * complications with the linker (which likes to create PLT stubs for branches
 * to targets outside the shared object) and to avoid multi-phase kernel builds, we
 * simply create minimalistic "patch lists" in special ELF sections.
 */
	.section ".data..patch.fsyscall_table", "a"
	.previous
#define LOAD_FSYSCALL_TABLE(reg)			\
[1:]	movl reg=0;					\
	.xdata4 ".data..patch.fsyscall_table", 1b-.

	.section ".data..patch.brl_fsys_bubble_down", "a"
	.previous
#define BRL_COND_FSYS_BUBBLE_DOWN(pr)			\
[1:](pr)brl.cond.sptk 0;				\
	;;						\
	.xdata4 ".data..patch.brl_fsys_bubble_down", 1b-.

GLOBAL_ENTRY(__kernel_syscall_via_break)
	.prologue
	.altrp b6
	.body
	/*
	 * Note: for (fast) syscall restart to work, the break instruction must be
	 *	 the first one in the bundle addressed by syscall_via_break.
	 */
{ .mib
	break 0x100000
	nop.i 0
	br.ret.sptk.many b6
}
END(__kernel_syscall_via_break)

#	define ARG0_OFF		(16 + IA64_SIGFRAME_ARG0_OFFSET)
#	define ARG1_OFF		(16 + IA64_SIGFRAME_ARG1_OFFSET)
#	define ARG2_OFF		(16 + IA64_SIGFRAME_ARG2_OFFSET)
#	define SIGHANDLER_OFF	(16 + IA64_SIGFRAME_HANDLER_OFFSET)
#	define SIGCONTEXT_OFF	(16 + IA64_SIGFRAME_SIGCONTEXT_OFFSET)

#	define FLAGS_OFF	IA64_SIGCONTEXT_FLAGS_OFFSET
#	define CFM_OFF		IA64_SIGCONTEXT_CFM_OFFSET
#	define FR6_OFF		IA64_SIGCONTEXT_FR6_OFFSET
#	define BSP_OFF		IA64_SIGCONTEXT_AR_BSP_OFFSET
#	define RNAT_OFF		IA64_SIGCONTEXT_AR_RNAT_OFFSET
#	define UNAT_OFF		IA64_SIGCONTEXT_AR_UNAT_OFFSET
#	define FPSR_OFF		IA64_SIGCONTEXT_AR_FPSR_OFFSET
#	define PR_OFF		IA64_SIGCONTEXT_PR_OFFSET
#	define RP_OFF		IA64_SIGCONTEXT_IP_OFFSET
#	define SP_OFF		IA64_SIGCONTEXT_R12_OFFSET
#	define RBS_BASE_OFF	IA64_SIGCONTEXT_RBS_BASE_OFFSET
#	define LOADRS_OFF	IA64_SIGCONTEXT_LOADRS_OFFSET
#	define base0		r2
#	define base1		r3
	/*
	 * When we get here, the memory stack looks like this:
	 *
	 *   +===============================+
       	 *   |				     |
       	 *   //	    struct sigframe          //
       	 *   |				     |
	 *   +-------------------------------+ <-- sp+16
	 *   |      16 byte of scratch       |
	 *   |            space              |
	 *   +-------------------------------+ <-- sp
	 *
	 * The register stack looks _exactly_ the way it looked at the time the signal
	 * occurred.  In other words, we're treading on a potential mine-field: each
	 * incoming general register may be a NaT value (including sp, in which case the
	 * process ends up dying with a SIGSEGV).
	 *
	 * The first thing need to do is a cover to get the registers onto the backing
	 * store.  Once that is done, we invoke the signal handler which may modify some
	 * of the machine state.  After returning from the signal handler, we return
	 * control to the previous context by executing a sigreturn system call.  A signal
	 * handler may call the rt_sigreturn() function to directly return to a given
	 * sigcontext.  However, the user-level sigreturn() needs to do much more than
	 * calling the rt_sigreturn() system call as it needs to unwind the stack to
	 * restore preserved registers that may have been saved on the signal handler's
	 * call stack.
	 */

#define SIGTRAMP_SAVES										\
	.unwabi 3, 's';		/* mark this as a sigtramp handler (saves scratch regs) */	\
	.unwabi @svr4, 's'; /* backwards compatibility with old unwinders (remove in v2.7) */	\
	.savesp ar.unat, UNAT_OFF+SIGCONTEXT_OFF;						\
	.savesp ar.fpsr, FPSR_OFF+SIGCONTEXT_OFF;						\
	.savesp pr, PR_OFF+SIGCONTEXT_OFF;     							\
	.savesp rp, RP_OFF+SIGCONTEXT_OFF;							\
	.savesp ar.pfs, CFM_OFF+SIGCONTEXT_OFF;							\
	.vframesp SP_OFF+SIGCONTEXT_OFF

GLOBAL_ENTRY(__kernel_sigtramp)
	// describe the state that is active when we get here:
	.prologue
	SIGTRAMP_SAVES
	.body

	.label_state 1

	adds base0=SIGHANDLER_OFF,sp
	adds base1=RBS_BASE_OFF+SIGCONTEXT_OFF,sp
	br.call.sptk.many rp=1f
1:
	ld8 r17=[base0],(ARG0_OFF-SIGHANDLER_OFF)	// get pointer to signal handler's plabel
	ld8 r15=[base1]					// get address of new RBS base (or NULL)
	cover				// push args in interrupted frame onto backing store
	;;
	cmp.ne p1,p0=r15,r0		// do we need to switch rbs? (note: pr is saved by kernel)
	mov.m r9=ar.bsp			// fetch ar.bsp
	.spillsp.p p1, ar.rnat, RNAT_OFF+SIGCONTEXT_OFF
(p1)	br.cond.spnt setup_rbs		// yup -> (clobbers p8, r14-r16, and r18-r20)
back_from_setup_rbs:
	alloc r8=ar.pfs,0,0,3,0
	ld8 out0=[base0],16		// load arg0 (signum)
	adds base1=(ARG1_OFF-(RBS_BASE_OFF+SIGCONTEXT_OFF)),base1
	;;
	ld8 out1=[base1]		// load arg1 (siginfop)
	ld8 r10=[r17],8			// get signal handler entry point
	;;
	ld8 out2=[base0]		// load arg2 (sigcontextp)
	ld8 gp=[r17]			// get signal handler's global pointer
	adds base0=(BSP_OFF+SIGCONTEXT_OFF),sp
	;;
	.spillsp ar.bsp, BSP_OFF+SIGCONTEXT_OFF
	st8 [base0]=r9			// save sc_ar_bsp
	adds base0=(FR6_OFF+SIGCONTEXT_OFF),sp
	adds base1=(FR6_OFF+16+SIGCONTEXT_OFF),sp
	;;
	stf.spill [base0]=f6,32
	stf.spill [base1]=f7,32
	;;
	stf.spill [base0]=f8,32
	stf.spill [base1]=f9,32
	mov b6=r10
	;;
	stf.spill [base0]=f10,32
	stf.spill [base1]=f11,32
	;;
	stf.spill [base0]=f12,32
	stf.spill [base1]=f13,32
	;;
	stf.spill [base0]=f14,32
	stf.spill [base1]=f15,32
	br.call.sptk.many rp=b6			// call the signal handler
.ret0:	adds base0=(BSP_OFF+SIGCONTEXT_OFF),sp
	;;
	ld8 r15=[base0]				// fetch sc_ar_bsp
	mov r14=ar.bsp
	;;
	cmp.ne p1,p0=r14,r15			// do we need to restore the rbs?
(p1)	br.cond.spnt restore_rbs		// yup -> (clobbers r14-r18, f6 & f7)
	;;
back_from_restore_rbs:
	adds base0=(FR6_OFF+SIGCONTEXT_OFF),sp
	adds base1=(FR6_OFF+16+SIGCONTEXT_OFF),sp
	;;
	ldf.fill f6=[base0],32
	ldf.fill f7=[base1],32
	;;
	ldf.fill f8=[base0],32
	ldf.fill f9=[base1],32
	;;
	ldf.fill f10=[base0],32
	ldf.fill f11=[base1],32
	;;
	ldf.fill f12=[base0],32
	ldf.fill f13=[base1],32
	;;
	ldf.fill f14=[base0],32
	ldf.fill f15=[base1],32
	mov r15=__NR_rt_sigreturn
	.restore sp				// pop .prologue
	break __BREAK_SYSCALL

	.prologue
	SIGTRAMP_SAVES
setup_rbs:
	mov ar.rsc=0				// put RSE into enforced lazy mode
	;;
	.save ar.rnat, r19
	mov r19=ar.rnat				// save RNaT before switching backing store area
	adds r14=(RNAT_OFF+SIGCONTEXT_OFF),sp

	mov r18=ar.bspstore
	mov ar.bspstore=r15			// switch over to new register backing store area
	;;

	.spillsp ar.rnat, RNAT_OFF+SIGCONTEXT_OFF
	st8 [r14]=r19				// save sc_ar_rnat
	.body
	mov.m r16=ar.bsp			// sc_loadrs <- (new bsp - new bspstore) << 16
	adds r14=(LOADRS_OFF+SIGCONTEXT_OFF),sp
	;;
	invala
	sub r15=r16,r15
	extr.u r20=r18,3,6
	;;
	mov ar.rsc=0xf				// set RSE into eager mode, pl 3
	cmp.eq p8,p0=63,r20
	shl r15=r15,16
	;;
	st8 [r14]=r15				// save sc_loadrs
(p8)	st8 [r18]=r19		// if bspstore points at RNaT slot, store RNaT there now
	.restore sp				// pop .prologue
	br.cond.sptk back_from_setup_rbs

	.prologue
	SIGTRAMP_SAVES
	.spillsp ar.rnat, RNAT_OFF+SIGCONTEXT_OFF
	.body
restore_rbs:
	// On input:
	//	r14 = bsp1 (bsp at the time of return from signal handler)
	//	r15 = bsp0 (bsp at the time the signal occurred)
	//
	// Here, we need to calculate bspstore0, the value that ar.bspstore needs
	// to be set to, based on bsp0 and the size of the dirty partition on
	// the alternate stack (sc_loadrs >> 16).  This can be done with the
	// following algorithm:
	//
	//  bspstore0 = rse_skip_regs(bsp0, -rse_num_regs(bsp1 - (loadrs >> 19), bsp1));
	//
	// This is what the code below does.
	//
	alloc r2=ar.pfs,0,0,0,0			// alloc null frame
	adds r16=(LOADRS_OFF+SIGCONTEXT_OFF),sp
	adds r18=(RNAT_OFF+SIGCONTEXT_OFF),sp
	;;
	ld8 r17=[r16]
	ld8 r16=[r18]			// get new rnat
	extr.u r18=r15,3,6	// r18 <- rse_slot_num(bsp0)
	;;
	mov ar.rsc=r17			// put RSE into enforced lazy mode
	shr.u r17=r17,16
	;;
	sub r14=r14,r17		// r14 (bspstore1) <- bsp1 - (sc_loadrs >> 16)
	shr.u r17=r17,3		// r17 <- (sc_loadrs >> 19)
	;;
	loadrs			// restore dirty partition
	extr.u r14=r14,3,6	// r14 <- rse_slot_num(bspstore1)
	;;
	add r14=r14,r17		// r14 <- rse_slot_num(bspstore1) + (sc_loadrs >> 19)
	;;
	shr.u r14=r14,6		// r14 <- (rse_slot_num(bspstore1) + (sc_loadrs >> 19))/0x40
	;;
	sub r14=r14,r17		// r14 <- -rse_num_regs(bspstore1, bsp1)
	movl r17=0x8208208208208209
	;;
	add r18=r18,r14		// r18 (delta) <- rse_slot_num(bsp0) - rse_num_regs(bspstore1,bsp1)
	setf.sig f7=r17
	cmp.lt p7,p0=r14,r0	// p7 <- (r14 < 0)?
	;;
(p7)	adds r18=-62,r18	// delta -= 62
	;;
	setf.sig f6=r18
	;;
	xmpy.h f6=f6,f7
	;;
	getf.sig r17=f6
	;;
	add r17=r17,r18
	shr r18=r18,63
	;;
	shr r17=r17,5
	;;
	sub r17=r17,r18		// r17 = delta/63
	;;
	add r17=r14,r17		// r17 <- delta/63 - rse_num_regs(bspstore1, bsp1)
	;;
	shladd r15=r17,3,r15	// r15 <- bsp0 + 8*(delta/63 - rse_num_regs(bspstore1, bsp1))
	;;
	mov ar.bspstore=r15			// switch back to old register backing store area
	;;
	mov ar.rnat=r16				// restore RNaT
	mov ar.rsc=0xf				// (will be restored later on from sc_ar_rsc)
	// invala not necessary as that will happen when returning to user-mode
	br.cond.sptk back_from_restore_rbs
END(__kernel_sigtramp)

/*
 * On entry:
 *	r11 = saved ar.pfs
 *	r15 = system call #
 *	b0  = saved return address
 *	b6  = return address
 * On exit:
 *	r11 = saved ar.pfs
 *	r15 = system call #
 *	b0  = saved return address
 *	all other "scratch" registers:	undefined
 *	all "preserved" registers:	same as on entry
 */

GLOBAL_ENTRY(__kernel_syscall_via_epc)
	.prologue
	.altrp b6
	.body
{
	/*
	 * Note: the kernel cannot assume that the first two instructions in this
	 * bundle get executed.  The remaining code must be safe even if
	 * they do not get executed.
	 */
	adds r17=-1024,r15			// A
	mov r10=0				// A    default to successful syscall execution
	epc					// B	causes split-issue
}
	;;
	RSM_PSR_BE_I(r20, r22)			// M2 (5 cyc to srlz.d)
	LOAD_FSYSCALL_TABLE(r14)		// X
	;;
	mov r16=IA64_KR(CURRENT)		// M2 (12 cyc)
	shladd r18=r17,3,r14			// A
	mov r19=NR_syscalls-1			// A
	;;
	lfetch [r18]				// M0|1
	MOV_FROM_PSR(p0, r29, r8)		// M2 (12 cyc)
	// If r17 is a NaT, p6 will be zero
	cmp.geu p6,p7=r19,r17			// A    (sysnr > 0 && sysnr < 1024+NR_syscalls)?
	;;
	mov r21=ar.fpsr				// M2 (12 cyc)
	tnat.nz p10,p9=r15			// I0
	mov.i r26=ar.pfs			// I0 (would stall anyhow due to srlz.d...)
	;;
	srlz.d					// M0 (forces split-issue) ensure PSR.BE==0
(p6)	ld8 r18=[r18]				// M0|1
	nop.i 0
	;;
	nop.m 0
(p6)	tbit.z.unc p8,p0=r18,0			// I0 (dual-issues with "mov b7=r18"!)
	nop.i 0
	;;
	SSM_PSR_I(p8, p14, r25)
(p6)	mov b7=r18				// I0
(p8)	br.dptk.many b7				// B

	mov r27=ar.rsc				// M2 (12 cyc)
/*
 * brl.cond doesn't work as intended because the linker would convert this branch
 * into a branch to a PLT.  Perhaps there will be a way to avoid this with some
 * future version of the linker.  In the meantime, we just use an indirect branch
 * instead.
 */
#ifdef CONFIG_ITANIUM
(p6)	add r14=-8,r14				// r14 <- addr of fsys_bubble_down entry
	;;
(p6)	ld8 r14=[r14]				// r14 <- fsys_bubble_down
	;;
(p6)	mov b7=r14
(p6)	br.sptk.many b7
#else
	BRL_COND_FSYS_BUBBLE_DOWN(p6)
#endif
	SSM_PSR_I(p0, p14, r10)
	mov r10=-1
(p10)	mov r8=EINVAL
(p9)	mov r8=ENOSYS
	FSYS_RETURN

#ifdef CONFIG_PARAVIRT
	/*
	 * padd to make the size of this symbol constant
	 * independent of paravirtualization.
	 */
	.align PAGE_SIZE / 8
#endif
END(__kernel_syscall_via_epc)
