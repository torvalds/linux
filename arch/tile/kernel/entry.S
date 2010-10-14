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

/*
 * Implement execve().  The i386 code has a note that forking from kernel
 * space results in no copy on write until the execve, so we should be
 * careful not to write to the stack here.
 */
STD_ENTRY(kernel_execve)
	moveli TREG_SYSCALL_NR_NAME, __NR_execve
	swint1
	jrp lr
	STD_ENDPROC(kernel_execve)

/* Delay a fixed number of cycles. */
STD_ENTRY(__delay)
	{ addi r0, r0, -1; bnzt r0, . }
	jrp lr
	STD_ENDPROC(__delay)

/*
 * We don't run this function directly, but instead copy it to a page
 * we map into every user process.  See vdso_setup().
 *
 * Note that libc has a copy of this function that it uses to compare
 * against the PC when a stack backtrace ends, so if this code is
 * changed, the libc implementation(s) should also be updated.
 */
	.pushsection .data
ENTRY(__rt_sigreturn)
	moveli TREG_SYSCALL_NR_NAME,__NR_rt_sigreturn
	swint1
	ENDPROC(__rt_sigreturn)
	ENTRY(__rt_sigreturn_end)
	.popsection

STD_ENTRY(dump_stack)
	{ move r2, lr; lnk r1 }
	{ move r4, r52; addli r1, r1, dump_stack - . }
	{ move r3, sp; j _dump_stack }
	jrp lr   /* keep backtracer happy */
	STD_ENDPROC(dump_stack)

STD_ENTRY(KBacktraceIterator_init_current)
	{ move r2, lr; lnk r1 }
	{ move r4, r52; addli r1, r1, KBacktraceIterator_init_current - . }
	{ move r3, sp; j _KBacktraceIterator_init_current }
	jrp lr   /* keep backtracer happy */
	STD_ENDPROC(KBacktraceIterator_init_current)

/*
 * Reset our stack to r1/r2 (sp and ksp0+cpu respectively), then
 * free the old stack (passed in r0) and re-invoke cpu_idle().
 * We update sp and ksp0 simultaneously to avoid backtracer warnings.
 */
STD_ENTRY(cpu_idle_on_new_stack)
	{
	 move sp, r1
	 mtspr SPR_SYSTEM_SAVE_K_0, r2
	}
	jal free_thread_info
	j cpu_idle
	STD_ENDPROC(cpu_idle_on_new_stack)

/* Loop forever on a nap during SMP boot. */
STD_ENTRY(smp_nap)
	nap
	j smp_nap /* we are not architecturally guaranteed not to exit nap */
	jrp lr    /* clue in the backtracer */
	STD_ENDPROC(smp_nap)

/*
 * Enable interrupts racelessly and then nap until interrupted.
 * This function's _cpu_idle_nap address is special; see intvec.S.
 * When interrupted at _cpu_idle_nap, we bump the PC forward 8, and
 * as a result return to the function that called _cpu_idle().
 */
STD_ENTRY(_cpu_idle)
	{
	 lnk r0
	 movei r1, KERNEL_PL
	}
	{
	 addli r0, r0, _cpu_idle_nap - .
	 mtspr INTERRUPT_CRITICAL_SECTION, r1
	}
	IRQ_ENABLE(r2, r3)             /* unmask, but still with ICS set */
	mtspr SPR_EX_CONTEXT_K_1, r1   /* Kernel PL, ICS clear */
	mtspr SPR_EX_CONTEXT_K_0, r0
	iret
	.global _cpu_idle_nap
_cpu_idle_nap:
	nap
	jrp lr
	STD_ENDPROC(_cpu_idle)
