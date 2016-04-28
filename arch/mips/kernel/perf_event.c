/*
 * Linux performance counter support for MIPS.
 *
 * Copyright (C) 2010 MIPS Technologies, Inc.
 * Author: Deng-Cheng Zhu
 *
 * This code is based on the implementation for ARM, which is in turn
 * based on the sparc64 perf event code and the x86 code. Performance
 * counter access is based on the MIPS Oprofile code. And the callchain
 * support references the code of MIPS stacktrace.c.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/perf_event.h>

#include <asm/stacktrace.h>

/* Callchain handling code. */

/*
 * Leave userspace callchain empty for now. When we find a way to trace
 * the user stack callchains, we will add it here.
 */

static void save_raw_perf_callchain(struct perf_callchain_entry_ctx *entry,
				    unsigned long reg29)
{
	unsigned long *sp = (unsigned long *)reg29;
	unsigned long addr;

	while (!kstack_end(sp)) {
		addr = *sp++;
		if (__kernel_text_address(addr)) {
			perf_callchain_store(entry, addr);
			if (entry->entry->nr >= entry->max_stack)
				break;
		}
	}
}

void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
			   struct pt_regs *regs)
{
	unsigned long sp = regs->regs[29];
#ifdef CONFIG_KALLSYMS
	unsigned long ra = regs->regs[31];
	unsigned long pc = regs->cp0_epc;

	if (raw_show_trace || !__kernel_text_address(pc)) {
		unsigned long stack_page =
			(unsigned long)task_stack_page(current);
		if (stack_page && sp >= stack_page &&
		    sp <= stack_page + THREAD_SIZE - 32)
			save_raw_perf_callchain(entry, sp);
		return;
	}
	do {
		perf_callchain_store(entry, pc);
		if (entry->entry->nr >= entry->max_stack)
			break;
		pc = unwind_stack(current, &sp, pc, &ra);
	} while (pc);
#else
	save_raw_perf_callchain(entry, sp);
#endif
}
