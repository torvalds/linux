/*
 * File:         arch/blackfin/mach-common/interrupt.S
 * Based on:
 * Author:       D. Jeff Dionne <jeff@ryeham.ee.ryerson.ca>
 *               Kenneth Albanowski <kjahds@kjahds.com>
 *
 * Created:      ?
 * Description:  Interrupt Entries
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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

#if ANOMALY_05000283 || ANOMALY_05000315
	cc = r7 == r7;
	p5.h = HI(CHIPID);
	p5.l = LO(CHIPID);
	if cc jump 1f;
	r7.l = W[p5];
1:
#endif
	r1 =  sp;
	SP += -12;
#ifdef CONFIG_IPIPE
	call ___ipipe_grab_irq
	SP += 12;
	cc = r0 == 0;
	if cc jump .Lcommon_restore_context;
#else /* CONFIG_IPIPE */
	call _do_irq;
	SP += 12;
#endif /* CONFIG_IPIPE */
	call _return_from_int;
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

#if ANOMALY_05000283 || ANOMALY_05000315
	cc = r7 == r7;
	p5.h = HI(CHIPID);
	p5.l = LO(CHIPID);
	if cc jump 1f;
	r7.l = W[p5];
1:
#endif

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
	call _trap_c;
	SP += 12;

#ifdef EBIU_ERRMST
	/* make sure EBIU_ERRMST is clear */
	p0.l = LO(EBIU_ERRMST);
	p0.h = HI(EBIU_ERRMST);
	r0.l = (CORE_ERROR | CORE_MERROR);
	w[p0] = r0.l;
#endif

	call _ret_from_exception;

.Lcommon_restore_all_sys:
	RESTORE_ALL_SYS
	rti;
ENDPROC(_evt_ivhw)

/* Interrupt routine for evt2 (NMI).
 * We don't actually use this, so just return.
 * For inner circle type details, please see:
 * http://docs.blackfin.uclinux.org/doku.php?id=linux-kernel:nmi
 */
ENTRY(_evt_nmi)
.weak _evt_nmi
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
	call _system_call;
	jump .Lcommon_restore_context;
ENDPROC(_evt_system_call)

#ifdef CONFIG_IPIPE
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

	[--sp] = reti;
	reti = [sp++];          /* IRQs are off. */
	r0.h = 3f;
	r0.l = 3f;
	p0.l = lo(EVT14);
	p0.h = hi(EVT14);
	[p0] = r0;
	csync;
	r0 = 0x401f (z);
	sti r0;
	raise 14;
	[--sp] = reti;          /* IRQs on. */
2:
	jump 2b;                /* Likely paranoid. */
3:
	sp += 4;                /* Discard saved RETI */
	r0.h = _evt14_softirq;
	r0.l = _evt14_softirq;
	p0.l = lo(EVT14);
	p0.h = hi(EVT14);
	[p0] = r0;
	csync;
	p0.l = _bfin_irq_flags;
	p0.h = _bfin_irq_flags;
	r0 = [p0];
	sti r0;
	rts;
ENDPROC(___ipipe_call_irqtail)

#endif /* CONFIG_IPIPE */
