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
	call _do_irq;
	SP += 12;
	call _return_from_int;
.Lcommon_restore_context:
	RESTORE_CONTEXT
	rti;

/* interrupt routine for ivhw - 5 */
ENTRY(_evt_ivhw)
	SAVE_ALL_SYS
#ifdef CONFIG_FRAME_POINTER
	fp = 0;
#endif

#if ANOMALY_05000283
	cc = r7 == r7;
	p5.h = HI(CHIPID);
	p5.l = LO(CHIPID);
	if cc jump 1f;
	r7.l = W[p5];
1:
#endif

#ifdef CONFIG_HARDWARE_PM
	r7 = [sp + PT_SEQSTAT];
	r7 = r7 >>> 0xe;
	r6 = 0x1F;
	r7 = r7 & r6;
	r5 = 0x12;
	cc = r7 == r5;
	if cc jump .Lcall_do_ovf; /* deal with performance counter overflow */
#endif
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

	call _ret_from_exception;
.Lcommon_restore_all_sys:
	RESTORE_ALL_SYS
	rti;

#ifdef CONFIG_HARDWARE_PM
.Lcall_do_ovf:

	SP += -12;
	call _pm_overflow;
	SP += 12;

	jump .Lcommon_restore_all_sys;
#endif

ENDPROC(_evt_ivhw)

/* Interrupt routine for evt2 (NMI).
 * We don't actually use this, so just return.
 * For inner circle type details, please see:
 * http://docs.blackfin.uclinux.org/doku.php?id=linux:nmi
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
