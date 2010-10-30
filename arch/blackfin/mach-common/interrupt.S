/*
 * Interrupt Entries
 *
 * Copyright 2005-2009 Analog Devices Inc.
 *               D. Jeff Dionne <jeff@ryeham.ee.ryerson.ca>
 *               Kenneth Albanowski <kjahds@kjahds.com>
 *
 * Licensed under the GPL-2 or later.
 */

#include <asm/blackfin.h>
#include <mach/irq.h>
#include <linux/linkage.h>
#include <asm/entry.h>
#include <asm/asm-offsets.h>
#include <asm/trace.h>
#include <asm/traps.h>
#include <asm/thread_info.h>

#include <asm/context.S>

.extern _ret_from_exception

#ifdef CONFIG_I_ENTRY_L1
.section .l1.text
#else
.text
#endif

.align 4 	/* just in case */

/* Common interrupt entry code.	 First we do CLI, then push
 * RETI, to keep interrupts disabled, but to allow this state to be changed
 * by local_bh_enable.
 * R0 contains the interrupt number, while R1 may contain the value of IPEND,
 * or garbage if IPEND won't be needed by the ISR.  */
__common_int_entry:
	[--sp] = fp;
	[--sp] = usp;

	[--sp] = i0;
	[--sp] = i1;
	[--sp] = i2;
	[--sp] = i3;

	[--sp] = m0;
	[--sp] = m1;
	[--sp] = m2;
	[--sp] = m3;

	[--sp] = l0;
	[--sp] = l1;
	[--sp] = l2;
	[--sp] = l3;

	[--sp] = b0;
	[--sp] = b1;
	[--sp] = b2;
	[--sp] = b3;
	[--sp] = a0.x;
	[--sp] = a0.w;
	[--sp] = a1.x;
	[--sp] = a1.w;

	[--sp] = LC0;
	[--sp] = LC1;
	[--sp] = LT0;
	[--sp] = LT1;
	[--sp] = LB0;
	[--sp] = LB1;

	[--sp] = ASTAT;

	[--sp] = r0;	/* Skip reserved */
	[--sp] = RETS;
	r2 = RETI;
	[--sp] = r2;
	[--sp] = RETX;
	[--sp] = RETN;
	[--sp] = RETE;
	[--sp] = SEQSTAT;
	[--sp] = r1;	/* IPEND - R1 may or may not be set up before jumping here. */

	/* Switch to other method of keeping interrupts disabled.  */
#ifdef CONFIG_DEBUG_HWERR
	r1 = 0x3f;
	sti r1;
#else
	cli r1;
#endif
#ifdef CONFIG_TRACE_IRQFLAGS
	[--sp] = r0;
	sp += -12;
	call _trace_hardirqs_off;
	sp += 12;
	r0 = [sp++];
#endif
	[--sp] = RETI;  /* orig_pc */
	/* Clear all L registers.  */
	r1 = 0 (x);
	l0 = r1;
	l1 = r1;
	l2 = r1;
	l3 = r1;
#ifdef CONFIG_FRAME_POINTER
	fp = 0;
#endif

	ANOMALY_283_315_WORKAROUND(p5, r7)

	r1 =  sp;
	SP += -12;
#ifdef CONFIG_IPIPE
	call ___ipipe_grab_irq
	SP += 12;
	cc = r0 == 0;
	if cc jump .Lcommon_restore_context;
#else /* CONFIG_IPIPE */

#ifdef CONFIG_PREEMPT
	r7 = sp;
	r4.l = lo(ALIGN_PAGE_MASK);
	r4.h = hi(ALIGN_PAGE_MASK);
	r7 = r7 & r4;
	p5 = r7;
	r7 = [p5 + TI_PREEMPT]; /* get preempt count */
	r7 += 1;                /* increment it */
	[p5 + TI_PREEMPT] = r7;
#endif
	pseudo_long_call _do_irq, p2;

#ifdef CONFIG_PREEMPT
	r7 += -1;
	[p5 + TI_PREEMPT] = r7; /* restore preempt count */
#endif

	SP += 12;
#endif /* CONFIG_IPIPE */
	pseudo_long_call _return_from_int, p2;
.Lcommon_restore_context:
	RESTORE_CONTEXT
	rti;

/* interrupt routine for ivhw - 5 */
ENTRY(_evt_ivhw)
	/* In case a single action kicks off multiple memory transactions, (like
	 * a cache line fetch, - this can cause multiple hardware errors, let's
	 * catch them all. First - make sure all the actions are complete, and
	 * the core sees the hardware errors.
	 */
	SSYNC;
	SSYNC;

	SAVE_ALL_SYS
#ifdef CONFIG_FRAME_POINTER
	fp = 0;
#endif

	ANOMALY_283_315_WORKAROUND(p5, r7)

	/* Handle all stacked hardware errors
	 * To make sure we don't hang forever, only do it 10 times
	 */
	R0 = 0;
	R2 = 10;
1:
	P0.L = LO(ILAT);
	P0.H = HI(ILAT);
	R1 = [P0];
	CC = BITTST(R1, EVT_IVHW_P);
	IF ! CC JUMP 2f;
	/* OK a hardware error is pending - clear it */
	R1 = EVT_IVHW_P;
	[P0] = R1;
	R0 += 1;
	CC = R1 == R2;
	if CC JUMP 2f;
	JUMP 1b;
2:
	# We are going to dump something out, so make sure we print IPEND properly
	p2.l = lo(IPEND);
	p2.h = hi(IPEND);
	r0 = [p2];
	[sp + PT_IPEND] = r0;

	/* set the EXCAUSE to HWERR for trap_c */
	r0 = [sp + PT_SEQSTAT];
	R1.L = LO(VEC_HWERR);
	R1.H = HI(VEC_HWERR);
	R0 = R0 | R1;
	[sp + PT_SEQSTAT] = R0;

	r0 = sp;        /* stack frame pt_regs pointer argument ==> r0 */
	SP += -12;
	pseudo_long_call _trap_c, p5;
	SP += 12;

#ifdef EBIU_ERRMST
	/* make sure EBIU_ERRMST is clear */
	p0.l = LO(EBIU_ERRMST);
	p0.h = HI(EBIU_ERRMST);
	r0.l = (CORE_ERROR | CORE_MERROR);
	w[p0] = r0.l;
#endif

	pseudo_long_call _ret_from_exception, p2;

.Lcommon_restore_all_sys:
	RESTORE_ALL_SYS
	rti;
ENDPROC(_evt_ivhw)

/* Interrupt routine for evt2 (NMI).
 * For inner circle type details, please see:
 * http://docs.blackfin.uclinux.org/doku.php?id=linux-kernel:nmi
 */
ENTRY(_evt_nmi)
#ifndef CONFIG_NMI_WATCHDOG
.weak _evt_nmi
#else
	/* Not take account of CPLBs, this handler will not return */
	SAVE_ALL_SYS
	r0 = sp;
	r1 = retn;
	[sp + PT_PC] = r1;
	trace_buffer_save(p4,r5);

	ANOMALY_283_315_WORKAROUND(p4, r5)

	SP += -12;
	call _do_nmi;
	SP += 12;
1:
	jump 1b;
#endif
	rtn;
ENDPROC(_evt_nmi)

/* interrupt routine for core timer - 6 */
ENTRY(_evt_timer)
	TIMER_INTERRUPT_ENTRY(EVT_IVTMR_P)

/* interrupt routine for evt7 - 7 */
ENTRY(_evt_evt7)
	INTERRUPT_ENTRY(EVT_IVG7_P)
ENTRY(_evt_evt8)
	INTERRUPT_ENTRY(EVT_IVG8_P)
ENTRY(_evt_evt9)
	INTERRUPT_ENTRY(EVT_IVG9_P)
ENTRY(_evt_evt10)
	INTERRUPT_ENTRY(EVT_IVG10_P)
ENTRY(_evt_evt11)
	INTERRUPT_ENTRY(EVT_IVG11_P)
ENTRY(_evt_evt12)
	INTERRUPT_ENTRY(EVT_IVG12_P)
ENTRY(_evt_evt13)
	INTERRUPT_ENTRY(EVT_IVG13_P)


 /* interrupt routine for system_call - 15 */
ENTRY(_evt_system_call)
	SAVE_CONTEXT_SYSCALL
#ifdef CONFIG_FRAME_POINTER
	fp = 0;
#endif
	pseudo_long_call _system_call, p2;
	jump .Lcommon_restore_context;
ENDPROC(_evt_system_call)

#ifdef CONFIG_IPIPE
/*
 * __ipipe_call_irqtail: lowers the current priority level to EVT15
 * before running a user-defined routine, then raises the priority
 * level to EVT14 to prepare the caller for a normal interrupt
 * return through RTI.
 *
 * We currently use this facility in two occasions:
 *
 * - to branch to __ipipe_irq_tail_hook as requested by a high
 *   priority domain after the pipeline delivered an interrupt,
 *   e.g. such as Xenomai, in order to start its rescheduling
 *   procedure, since we may not switch tasks when IRQ levels are
 *   nested on the Blackfin, so we have to fake an interrupt return
 *   so that we may reschedule immediately.
 *
 * - to branch to sync_root_irqs, in order to play any interrupt
 *   pending for the root domain (i.e. the Linux kernel). This lowers
 *   the core priority level enough so that Linux IRQ handlers may
 *   never delay interrupts handled by high priority domains; we defer
 *   those handlers until this point instead. This is a substitute
 *   to using a threaded interrupt model for the Linux kernel.
 *
 * r0: address of user-defined routine
 * context: caller must have preempted EVT15, hw interrupts must be off.
 */
ENTRY(___ipipe_call_irqtail)
	p0 = r0;
	r0.l = 1f;
	r0.h = 1f;
	reti = r0;
	rti;
1:
	[--sp] = rets;
	[--sp] = ( r7:4, p5:3 );
	sp += -12;
	call (p0);
	sp += 12;
	( r7:4, p5:3 ) = [sp++];
	rets = [sp++];

#ifdef CONFIG_DEBUG_HWERR
	/* enable irq14 & hwerr interrupt, until we transition to _evt_evt14 */
	r0 = (EVT_IVG14 | EVT_IVHW | \
		EVT_IRPTEN | EVT_EVX | EVT_NMI | EVT_RST | EVT_EMU);
#else
	/* Only enable irq14 interrupt, until we transition to _evt_evt14 */
	r0 = (EVT_IVG14 | \
		EVT_IRPTEN | EVT_EVX | EVT_NMI | EVT_RST | EVT_EMU);
#endif
	sti r0;
	raise 14;		/* Branches to _evt_evt14 */
2:
	jump 2b;                /* Likely paranoid. */
ENDPROC(___ipipe_call_irqtail)

#endif /* CONFIG_IPIPE */
