// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Hangzhou C-SKY Microsystems co.,ltd. */

#include <linux/perf_event.h>
#include <linux/uaccess.h>

#include <asm/stacktrace.h>

static bool fill_callchain(void *entry, unsigned long pc)
{
	return perf_callchain_store(entry, pc) == 0;
}

/*
 * This will be called when the target is in user mode
 * This function will only be called when we use
 * "PERF_SAMPLE_CALLCHAIN" in
 * kernel/events/core.c:perf_prepare_sample()
 *
 * How to trigger perf_callchain_[user/kernel] :
 * $ perf record -e cpu-clock --call-graph fp ./program
 * $ perf report --call-graph
 *
 * On RISC-V platform, the program being sampled and the C library
 * need to be compiled with -fno-omit-frame-pointer, otherwise
 * the user stack will not contain function frame.
 */
void perf_callchain_user(struct perf_callchain_entry_ctx *entry,
			 struct pt_regs *regs)
{
	arch_stack_walk_user(fill_callchain, entry, regs);
}

void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
			   struct pt_regs *regs)
{
	walk_stackframe(NULL, regs, fill_callchain, entry);
}
