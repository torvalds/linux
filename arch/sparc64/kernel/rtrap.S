/* $Id: rtrap.S,v 1.61 2002/02/09 19:49:31 davem Exp $
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

#define		RTRAP_PSTATE		(PSTATE_RMO|PSTATE_PEF|PSTATE_PRIV|PSTATE_IE)
#define		RTRAP_PSTATE_IRQOFF	(PSTATE_RMO|PSTATE_PEF|PSTATE_PRIV)
#define		RTRAP_PSTATE_AG_IRQOFF	(PSTATE_RMO|PSTATE_PEF|PSTATE_PRIV|PSTATE_AG)

		.text
		.align			32
__handle_softirq:
		call			do_softirq
		 nop
		ba,a,pt			%xcc, __handle_softirq_continue
		 nop
__handle_preemption:
		call			schedule
		 wrpr			%g0, RTRAP_PSTATE, %pstate
		ba,pt			%xcc, __handle_preemption_continue
		 wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate

__handle_user_windows:
		call			fault_in_user_windows
		 wrpr			%g0, RTRAP_PSTATE, %pstate
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate
		/* Redo sched+sig checks */
		ldx			[%g6 + TI_FLAGS], %l0
		andcc			%l0, _TIF_NEED_RESCHED, %g0

		be,pt			%xcc, 1f
		 nop
		call			schedule
		 wrpr			%g0, RTRAP_PSTATE, %pstate
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate
		ldx			[%g6 + TI_FLAGS], %l0

1:		andcc			%l0, _TIF_SIGPENDING, %g0
		be,pt			%xcc, __handle_user_windows_continue
		 nop
		mov			%l5, %o1
		add			%sp, PTREGS_OFF, %o0
		mov			%l0, %o2

		call			do_notify_resume
		 wrpr			%g0, RTRAP_PSTATE, %pstate
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate
		/* Signal delivery can modify pt_regs tstate, so we must
		 * reload it.
		 */
		ldx			[%sp + PTREGS_OFF + PT_V9_TSTATE], %l1
		sethi			%hi(0xf << 20), %l4
		and			%l1, %l4, %l4
		ba,pt			%xcc, __handle_user_windows_continue

		 andn			%l1, %l4, %l1
__handle_perfctrs:
		call			update_perfctrs
		 wrpr			%g0, RTRAP_PSTATE, %pstate
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate
		ldub			[%g6 + TI_WSAVED], %o2
		brz,pt			%o2, 1f
		 nop
		/* Redo userwin+sched+sig checks */
		call			fault_in_user_windows

		 wrpr			%g0, RTRAP_PSTATE, %pstate
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate
		ldx			[%g6 + TI_FLAGS], %l0
		andcc			%l0, _TIF_NEED_RESCHED, %g0
		be,pt			%xcc, 1f

		 nop
		call			schedule
		 wrpr			%g0, RTRAP_PSTATE, %pstate
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate
		ldx			[%g6 + TI_FLAGS], %l0
1:		andcc			%l0, _TIF_SIGPENDING, %g0

		be,pt			%xcc, __handle_perfctrs_continue
		 sethi			%hi(TSTATE_PEF), %o0
		mov			%l5, %o1
		add			%sp, PTREGS_OFF, %o0
		mov			%l0, %o2
		call			do_notify_resume

		 wrpr			%g0, RTRAP_PSTATE, %pstate
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate
		/* Signal delivery can modify pt_regs tstate, so we must
		 * reload it.
		 */
		ldx			[%sp + PTREGS_OFF + PT_V9_TSTATE], %l1
		sethi			%hi(0xf << 20), %l4
		and			%l1, %l4, %l4
		andn			%l1, %l4, %l1
		ba,pt			%xcc, __handle_perfctrs_continue

		 sethi			%hi(TSTATE_PEF), %o0
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
		 wrpr			%g0, RTRAP_PSTATE, %pstate
		wrpr			%g0, RTRAP_PSTATE_IRQOFF, %pstate

		/* Signal delivery can modify pt_regs tstate, so we must
		 * reload it.
		 */
		ldx			[%sp + PTREGS_OFF + PT_V9_TSTATE], %l1
		sethi			%hi(0xf << 20), %l4
		and			%l1, %l4, %l4
		ba,pt			%xcc, __handle_signal_continue
		 andn			%l1, %l4, %l1

		.align			64
		.globl			rtrap_irq, rtrap, irqsz_patchme, rtrap_xcall
rtrap_irq:
rtrap:
#ifndef CONFIG_SMP
		sethi			%hi(per_cpu____cpu_data), %l0
		lduw			[%l0 + %lo(per_cpu____cpu_data)], %l1
#else
		sethi			%hi(per_cpu____cpu_data), %l0
		or			%l0, %lo(per_cpu____cpu_data), %l0
		lduw			[%l0 + %g5], %l1
#endif
		cmp			%l1, 0

		/* mm/ultra.S:xcall_report_regs KNOWS about this load. */
		bne,pn			%icc, __handle_softirq
		 ldx			[%sp + PTREGS_OFF + PT_V9_TSTATE], %l1
__handle_softirq_continue:
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
		wrpr			%l4, %pil
rtrap_no_irq_enable:
#endif
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
		 * This also means that if we have to deal with performance
		 * counters or user windows, we have to redo all of these
		 * sched+signal checks with IRQs disabled.
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
		 andcc			%l0, _TIF_SIGPENDING, %g0
		bne,pn			%xcc, __handle_signal
__handle_signal_continue:
		 ldub			[%g6 + TI_WSAVED], %o2
		brnz,pn			%o2, __handle_user_windows
		 nop
__handle_user_windows_continue:
		ldx			[%g6 + TI_FLAGS], %l5
		andcc			%l5, _TIF_PERFCTR, %g0
		sethi			%hi(TSTATE_PEF), %o0
		bne,pn			%xcc, __handle_perfctrs
__handle_perfctrs_continue:
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

		wrpr			%l2, %g0, %canrestore
		wrpr			%l1, %g0, %wstate
		brnz,pt			%l2, user_rtt_restore
		 wrpr			%g0, %g0, %otherwin

		ldx			[%g6 + TI_FLAGS], %g3
		wr			%g0, ASI_AIUP, %asi
		rdpr			%cwp, %g1
		andcc			%g3, _TIF_32BIT, %g0
		sub			%g1, 1, %g1
		bne,pt			%xcc, user_rtt_fill_32bit
		 wrpr			%g1, %cwp
		ba,a,pt			%xcc, user_rtt_fill_64bit

user_rtt_fill_fixup:
		rdpr	%cwp, %g1
		add	%g1, 1, %g1
		wrpr	%g1, 0x0, %cwp

		rdpr	%wstate, %g2
		sll	%g2, 3, %g2
		wrpr	%g2, 0x0, %wstate

		/* We know %canrestore and %otherwin are both zero.  */

		sethi	%hi(sparc64_kern_pri_context), %g2
		ldx	[%g2 + %lo(sparc64_kern_pri_context)], %g2
		mov	PRIMARY_CONTEXT, %g1

661:		stxa	%g2, [%g1] ASI_DMMU
		.section .sun4v_1insn_patch, "ax"
		.word	661b
		stxa	%g2, [%g1] ASI_MMU
		.previous

		sethi	%hi(KERNBASE), %g1
		flush	%g1

		or	%g4, FAULT_CODE_WINFIXUP, %g4
		stb	%g4, [%g6 + TI_FAULT_CODE]
		stx	%g5, [%g6 + TI_FAULT_ADDR]

		mov	%g6, %l1
		wrpr	%g0, 0x0, %tl

661:		nop
		.section		.sun4v_1insn_patch, "ax"
		.word			661b
		SET_GL(0)
		.previous

		wrpr	%g0, RTRAP_PSTATE, %pstate

		mov	%l1, %g6
		ldx	[%g6 + TI_TASK], %g4
		LOAD_PER_CPU_BASE(%g5, %g6, %g1, %g2, %g3)
		call	do_sparc64_fault
		 add	%sp, PTREGS_OFF, %o0
		ba,pt	%xcc, rtrap
		 nop

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
		restore
		retry

to_kernel:
#ifdef CONFIG_PREEMPT
		ldsw			[%g6 + TI_PRE_COUNT], %l5
		brnz			%l5, kern_fpucheck
		 ldx			[%g6 + TI_FLAGS], %l5
		andcc			%l5, _TIF_NEED_RESCHED, %g0
		be,pt			%xcc, kern_fpucheck
		 nop
		cmp			%l4, 0
		bne,pn			%xcc, kern_fpucheck
		 sethi			%hi(PREEMPT_ACTIVE), %l6
		stw			%l6, [%g6 + TI_PRE_COUNT]
		call			schedule
		 nop
		ba,pt			%xcc, rtrap
		 stw			%g0, [%g6 + TI_PRE_COUNT]
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
