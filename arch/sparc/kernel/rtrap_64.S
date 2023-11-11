/* SPDX-License-Identifier: GPL-2.0 */
/*
 * rtrap.S: Preparing for return from trap on Sparc V9.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */


#include <asm/asi.h>
#include <asm/pstate.h>
#include <asm/ptrace.h>
#include <asm/spitfire.h>
#include <asm/head.h>
#include <asm/visasm.h>
#include <asm/processor.h>

#ifdef CONFIG_CONTEXT_TRACKING_USER
# define SCHEDULE_USER schedule_user
#else
# define SCHEDULE_USER schedule
#endif

		.text
		.align			32
__handle_preemption:
		call			SCHEDULE_USER
661:		 wrpr			%g0, RTRAP_PSTATE, %pstate
		/* If userspace is using ADI, it could potentially pass
		 * a pointer with version tag embedded in it. To maintain
		 * the ADI security, we must re-enable PSTATE.mcde before
		 * we continue execution in the kernel for another thread.
		 */
		.section .sun_m7_1insn_patch, "ax"
		.word	661b
		 wrpr			%g0, RTRAP_PSTATE|PSTATE_MCDE, %pstate
		.previous
		ba,pt			%xcc, __handle_preemption_continue
		 wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate

__handle_user_windows:
		add			%sp, PTREGS_OFF, %o0
		call			fault_in_user_windows
661:		 wrpr			%g0, RTRAP_PSTATE, %pstate
		/* If userspace is using ADI, it could potentially pass
		 * a pointer with version tag embedded in it. To maintain
		 * the ADI security, we must re-enable PSTATE.mcde before
		 * we continue execution in the kernel for another thread.
		 */
		.section .sun_m7_1insn_patch, "ax"
		.word	661b
		 wrpr			%g0, RTRAP_PSTATE|PSTATE_MCDE, %pstate
		.previous
		ba,pt			%xcc, __handle_preemption_continue
		 wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate

__handle_userfpu:
		rd			%fprs, %l5
		andcc			%l5, FPRS_FEF, %g0
		sethi			%hi(TSTATE_PEF), %o0
		be,a,pn			%icc, __handle_userfpu_continue
		 andn			%l1, %o0, %l1
		ba,a,pt			%xcc, __handle_userfpu_continue

__handle_signal:
		mov			%l5, %o1
		add			%sp, PTREGS_OFF, %o0
		mov			%l0, %o2
		call			do_notify_resume
661:		 wrpr			%g0, RTRAP_PSTATE, %pstate
		/* If userspace is using ADI, it could potentially pass
		 * a pointer with version tag embedded in it. To maintain
		 * the ADI security, we must re-enable PSTATE.mcde before
		 * we continue execution in the kernel for another thread.
		 */
		.section .sun_m7_1insn_patch, "ax"
		.word	661b
		 wrpr			%g0, RTRAP_PSTATE|PSTATE_MCDE, %pstate
		.previous
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate

		/* Signal delivery can modify pt_regs tstate, so we must
		 * reload it.
		 */
		ldx			[%sp + PTREGS_OFF + PT_V9_TSTATE], %l1
		sethi			%hi(0xf << 20), %l4
		and			%l1, %l4, %l4
		andn			%l1, %l4, %l1
		ba,pt			%xcc, __handle_preemption_continue
		 srl			%l4, 20, %l4

		/* When returning from a NMI (%pil==15) interrupt we want to
		 * avoid running softirqs, doing IRQ tracing, preempting, etc.
		 */
		.globl			rtrap_nmi
rtrap_nmi:	ldx			[%sp + PTREGS_OFF + PT_V9_TSTATE], %l1
		sethi			%hi(0xf << 20), %l4
		and			%l1, %l4, %l4
		andn			%l1, %l4, %l1
		srl			%l4, 20, %l4
		ba,pt			%xcc, rtrap_no_irq_enable
		nop
		/* Do not actually set the %pil here.  We will do that
		 * below after we clear PSTATE_IE in the %pstate register.
		 * If we re-enable interrupts here, we can recurse down
		 * the hardirq stack potentially endlessly, causing a
		 * stack overflow.
		 */

		.align			64
		.globl			rtrap_irq, rtrap, irqsz_patchme, rtrap_xcall
rtrap_irq:
rtrap:
		/* mm/ultra.S:xcall_report_regs KNOWS about this load. */
		ldx			[%sp + PTREGS_OFF + PT_V9_TSTATE], %l1
rtrap_xcall:
		sethi			%hi(0xf << 20), %l4
		and			%l1, %l4, %l4
		andn			%l1, %l4, %l1
		srl			%l4, 20, %l4
#ifdef CONFIG_TRACE_IRQFLAGS
		brnz,pn			%l4, rtrap_no_irq_enable
		 nop
		call			trace_hardirqs_on
		 nop
		/* Do not actually set the %pil here.  We will do that
		 * below after we clear PSTATE_IE in the %pstate register.
		 * If we re-enable interrupts here, we can recurse down
		 * the hardirq stack potentially endlessly, causing a
		 * stack overflow.
		 *
		 * It is tempting to put this test and trace_hardirqs_on
		 * call at the 'rt_continue' label, but that will not work
		 * as that path hits unconditionally and we do not want to
		 * execute this in NMI return paths, for example.
		 */
#endif
rtrap_no_irq_enable:
		andcc			%l1, TSTATE_PRIV, %l3
		bne,pn			%icc, to_kernel
		 nop

		/* We must hold IRQs off and atomically test schedule+signal
		 * state, then hold them off all the way back to userspace.
		 * If we are returning to kernel, none of this matters.  Note
		 * that we are disabling interrupts via PSTATE_IE, not using
		 * %pil.
		 *
		 * If we do not do this, there is a window where we would do
		 * the tests, later the signal/resched event arrives but we do
		 * not process it since we are still in kernel mode.  It would
		 * take until the next local IRQ before the signal/resched
		 * event would be handled.
		 *
		 * This also means that if we have to deal with user
		 * windows, we have to redo all of these sched+signal checks
		 * with IRQs disabled.
		 */
to_user:	wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate
		wrpr			0, %pil
__handle_preemption_continue:
		ldx			[%g6 + TI_FLAGS], %l0
		sethi			%hi(_TIF_USER_WORK_MASK), %o0
		or			%o0, %lo(_TIF_USER_WORK_MASK), %o0
		andcc			%l0, %o0, %g0
		sethi			%hi(TSTATE_PEF), %o0
		be,pt			%xcc, user_nowork
		 andcc			%l1, %o0, %g0
		andcc			%l0, _TIF_NEED_RESCHED, %g0
		bne,pn			%xcc, __handle_preemption
		 andcc			%l0, _TIF_DO_NOTIFY_RESUME_MASK, %g0
		bne,pn			%xcc, __handle_signal
		 ldub			[%g6 + TI_WSAVED], %o2
		brnz,pn			%o2, __handle_user_windows
		 nop
		sethi			%hi(TSTATE_PEF), %o0
		andcc			%l1, %o0, %g0

		/* This fpdepth clear is necessary for non-syscall rtraps only */
user_nowork:
		bne,pn			%xcc, __handle_userfpu
		 stb			%g0, [%g6 + TI_FPDEPTH]
__handle_userfpu_continue:

rt_continue:	ldx			[%sp + PTREGS_OFF + PT_V9_G1], %g1
		ldx			[%sp + PTREGS_OFF + PT_V9_G2], %g2

		ldx			[%sp + PTREGS_OFF + PT_V9_G3], %g3
		ldx			[%sp + PTREGS_OFF + PT_V9_G4], %g4
		ldx			[%sp + PTREGS_OFF + PT_V9_G5], %g5
		brz,pt			%l3, 1f
		mov			%g6, %l2

		/* Must do this before thread reg is clobbered below.  */
		LOAD_PER_CPU_BASE(%g5, %g6, %i0, %i1, %i2)
1:
		ldx			[%sp + PTREGS_OFF + PT_V9_G6], %g6
		ldx			[%sp + PTREGS_OFF + PT_V9_G7], %g7

		/* Normal globals are restored, go to trap globals.  */
661:		wrpr			%g0, RTRAP_PSTATE_AG_IRQOFF, %pstate
		nop
		.section		.sun4v_2insn_patch, "ax"
		.word			661b
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate
		SET_GL(1)
		.previous

		mov			%l2, %g6

		ldx			[%sp + PTREGS_OFF + PT_V9_I0], %i0
		ldx			[%sp + PTREGS_OFF + PT_V9_I1], %i1

		ldx			[%sp + PTREGS_OFF + PT_V9_I2], %i2
		ldx			[%sp + PTREGS_OFF + PT_V9_I3], %i3
		ldx			[%sp + PTREGS_OFF + PT_V9_I4], %i4
		ldx			[%sp + PTREGS_OFF + PT_V9_I5], %i5
		ldx			[%sp + PTREGS_OFF + PT_V9_I6], %i6
		ldx			[%sp + PTREGS_OFF + PT_V9_I7], %i7
		ldx			[%sp + PTREGS_OFF + PT_V9_TPC], %l2
		ldx			[%sp + PTREGS_OFF + PT_V9_TNPC], %o2

		ld			[%sp + PTREGS_OFF + PT_V9_Y], %o3
		wr			%o3, %g0, %y
		wrpr			%l4, 0x0, %pil
		wrpr			%g0, 0x1, %tl
		andn			%l1, TSTATE_SYSCALL, %l1
		wrpr			%l1, %g0, %tstate
		wrpr			%l2, %g0, %tpc
		wrpr			%o2, %g0, %tnpc

		brnz,pn			%l3, kern_rtt
		 mov			PRIMARY_CONTEXT, %l7

661:		ldxa			[%l7 + %l7] ASI_DMMU, %l0
		.section		.sun4v_1insn_patch, "ax"
		.word			661b
		ldxa			[%l7 + %l7] ASI_MMU, %l0
		.previous

		sethi			%hi(sparc64_kern_pri_nuc_bits), %l1
		ldx			[%l1 + %lo(sparc64_kern_pri_nuc_bits)], %l1
		or			%l0, %l1, %l0

661:		stxa			%l0, [%l7] ASI_DMMU
		.section		.sun4v_1insn_patch, "ax"
		.word			661b
		stxa			%l0, [%l7] ASI_MMU
		.previous

		sethi			%hi(KERNBASE), %l7
		flush			%l7
		rdpr			%wstate, %l1
		rdpr			%otherwin, %l2
		srl			%l1, 3, %l1

661:		wrpr			%l2, %g0, %canrestore
		.section		.fast_win_ctrl_1insn_patch, "ax"
		.word			661b
		.word			0x89880000	! normalw
		.previous

		wrpr			%l1, %g0, %wstate
		brnz,pt			%l2, user_rtt_restore
661:		 wrpr			%g0, %g0, %otherwin
		.section		.fast_win_ctrl_1insn_patch, "ax"
		.word			661b
		 nop
		.previous

		ldx			[%g6 + TI_FLAGS], %g3
		wr			%g0, ASI_AIUP, %asi
		rdpr			%cwp, %g1
		andcc			%g3, _TIF_32BIT, %g0
		sub			%g1, 1, %g1
		bne,pt			%xcc, user_rtt_fill_32bit
		 wrpr			%g1, %cwp
		ba,a,pt			%xcc, user_rtt_fill_64bit
		 nop

user_rtt_fill_fixup_dax:
		ba,pt	%xcc, user_rtt_fill_fixup_common
		 mov	1, %g3

user_rtt_fill_fixup_mna:
		ba,pt	%xcc, user_rtt_fill_fixup_common
		 mov	2, %g3

user_rtt_fill_fixup:
		ba,pt	%xcc, user_rtt_fill_fixup_common
		 clr	%g3

user_rtt_pre_restore:
		add			%g1, 1, %g1
		wrpr			%g1, 0x0, %cwp

user_rtt_restore:
		restore
		rdpr			%canrestore, %g1
		wrpr			%g1, 0x0, %cleanwin
		retry
		nop

kern_rtt:	rdpr			%canrestore, %g1
		brz,pn			%g1, kern_rtt_fill
		 nop
kern_rtt_restore:
		stw			%g0, [%sp + PTREGS_OFF + PT_V9_MAGIC]
		restore
		retry

to_kernel:
#ifdef CONFIG_PREEMPTION
		ldsw			[%g6 + TI_PRE_COUNT], %l5
		brnz			%l5, kern_fpucheck
		 ldx			[%g6 + TI_FLAGS], %l5
		andcc			%l5, _TIF_NEED_RESCHED, %g0
		be,pt			%xcc, kern_fpucheck
		 nop
		cmp			%l4, 0
		bne,pn			%xcc, kern_fpucheck
		 nop
		call			preempt_schedule_irq
		 nop
		ba,pt			%xcc, rtrap
#endif
kern_fpucheck:	ldub			[%g6 + TI_FPDEPTH], %l5
		brz,pt			%l5, rt_continue
		 srl			%l5, 1, %o0
		add			%g6, TI_FPSAVED, %l6
		ldub			[%l6 + %o0], %l2
		sub			%l5, 2, %l5

		add			%g6, TI_GSR, %o1
		andcc			%l2, (FPRS_FEF|FPRS_DU), %g0
		be,pt			%icc, 2f
		 and			%l2, FPRS_DL, %l6
		andcc			%l2, FPRS_FEF, %g0
		be,pn			%icc, 5f
		 sll			%o0, 3, %o5
		rd			%fprs, %g1

		wr			%g1, FPRS_FEF, %fprs
		ldx			[%o1 + %o5], %g1
		add			%g6, TI_XFSR, %o1
		sll			%o0, 8, %o2
		add			%g6, TI_FPREGS, %o3
		brz,pn			%l6, 1f
		 add			%g6, TI_FPREGS+0x40, %o4

		membar			#Sync
		ldda			[%o3 + %o2] ASI_BLK_P, %f0
		ldda			[%o4 + %o2] ASI_BLK_P, %f16
		membar			#Sync
1:		andcc			%l2, FPRS_DU, %g0
		be,pn			%icc, 1f
		 wr			%g1, 0, %gsr
		add			%o2, 0x80, %o2
		membar			#Sync
		ldda			[%o3 + %o2] ASI_BLK_P, %f32
		ldda			[%o4 + %o2] ASI_BLK_P, %f48
1:		membar			#Sync
		ldx			[%o1 + %o5], %fsr
2:		stb			%l5, [%g6 + TI_FPDEPTH]
		ba,pt			%xcc, rt_continue
		 nop
5:		wr			%g0, FPRS_FEF, %fprs
		sll			%o0, 8, %o2

		add			%g6, TI_FPREGS+0x80, %o3
		add			%g6, TI_FPREGS+0xc0, %o4
		membar			#Sync
		ldda			[%o3 + %o2] ASI_BLK_P, %f32
		ldda			[%o4 + %o2] ASI_BLK_P, %f48
		membar			#Sync
		wr			%g0, FPRS_DU, %fprs
		ba,pt			%xcc, rt_continue
		 stb			%l5, [%g6 + TI_FPDEPTH]
