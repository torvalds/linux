/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/linkage.h>
#include <linux/unistd.h>
#include <asm/irqflags.h>
#include <asm/processor.h>
#include <arch/abi.h>
#include <arch/spr_def.h>

#ifdef __tilegx__
#define bnzt bnezt
#endif

STD_ENTRY(current_text_addr)
	{ move r0, lr; jrp lr }
	STD_ENDPROC(current_text_addr)

STD_ENTRY(KBacktraceIterator_init_current)
	{ move r2, lr; lnk r1 }
	{ move r4, r52; addli r1, r1, KBacktraceIterator_init_current - . }
	{ move r3, sp; j _KBacktraceIterator_init_current }
	jrp lr   /* keep backtracer happy */
	STD_ENDPROC(KBacktraceIterator_init_current)

/* Loop forever on a nap during SMP boot. */
STD_ENTRY(smp_nap)
	nap
	nop       /* avoid provoking the icache prefetch with a jump */
	j smp_nap /* we are not architecturally guaranteed not to exit nap */
	jrp lr    /* clue in the backtracer */
	STD_ENDPROC(smp_nap)

/*
 * Enable interrupts racelessly and then nap until interrupted.
 * Architecturally, we are guaranteed that enabling interrupts via
 * mtspr to INTERRUPT_CRITICAL_SECTION only interrupts at the next PC.
 * This function's _cpu_idle_nap address is special; see intvec.S.
 * When interrupted at _cpu_idle_nap, we bump the PC forward 8, and
 * as a result return to the function that called _cpu_idle().
 */
STD_ENTRY_SECTION(_cpu_idle, .cpuidle.text)
	movei r1, 1
	IRQ_ENABLE_LOAD(r2, r3)
	mtspr INTERRUPT_CRITICAL_SECTION, r1
	IRQ_ENABLE_APPLY(r2, r3)       /* unmask, but still with ICS set */
	mtspr INTERRUPT_CRITICAL_SECTION, zero
	.global _cpu_idle_nap
_cpu_idle_nap:
	nap
	nop       /* avoid provoking the icache prefetch with a jump */
	jrp lr
	STD_ENDPROC(_cpu_idle)
