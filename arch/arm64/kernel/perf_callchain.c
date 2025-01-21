// SPDX-License-Identifier: GPL-2.0-only
/*
 * arm64 callchain support
 *
 * Copyright (C) 2015 ARM Limited
 */
#include <linux/perf_event.h>
#include <linux/stacktrace.h>
#include <linux/uaccess.h>

#include <asm/pointer_auth.h>

static bool callchain_trace(void *data, unsigned long pc)
{
	struct perf_callchain_entry_ctx *entry = data;

	return perf_callchain_store(entry, pc) == 0;
}

void perf_callchain_user(struct perf_callchain_entry_ctx *entry,
			 struct pt_regs *regs)
{
	if (perf_guest_state()) {
		/* We don't support guest os callchain now */
		return;
	}

	arch_stack_walk_user(callchain_trace, entry, regs);
}

void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
			   struct pt_regs *regs)
{
	if (perf_guest_state()) {
		/* We don't support guest os callchain now */
		return;
	}

	arch_stack_walk(callchain_trace, entry, current, regs);
}
