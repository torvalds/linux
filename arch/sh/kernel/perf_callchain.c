/*
 * Performance event callchain support - SuperH architecture code
 *
 * Copyright (C) 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/perf_event.h>
#include <linux/percpu.h>
#include <asm/unwinder.h>
#include <asm/ptrace.h>

static inline void callchain_store(struct perf_callchain_entry *entry, u64 ip)
{
	if (entry->nr < PERF_MAX_STACK_DEPTH)
		entry->ip[entry->nr++] = ip;
}

static void callchain_warning(void *data, char *msg)
{
}

static void
callchain_warning_symbol(void *data, char *msg, unsigned long symbol)
{
}

static int callchain_stack(void *data, char *name)
{
	return 0;
}

static void callchain_address(void *data, unsigned long addr, int reliable)
{
	struct perf_callchain_entry *entry = data;

	if (reliable)
		callchain_store(entry, addr);
}

static const struct stacktrace_ops callchain_ops = {
	.warning	= callchain_warning,
	.warning_symbol	= callchain_warning_symbol,
	.stack		= callchain_stack,
	.address	= callchain_address,
};

static void
perf_callchain_kernel(struct pt_regs *regs, struct perf_callchain_entry *entry)
{
	callchain_store(entry, PERF_CONTEXT_KERNEL);
	callchain_store(entry, regs->pc);

	unwind_stack(NULL, regs, NULL, &callchain_ops, entry);
}

static void
perf_do_callchain(struct pt_regs *regs, struct perf_callchain_entry *entry)
{
	int is_user;

	if (!regs)
		return;

	is_user = user_mode(regs);

	if (!current || current->pid == 0)
		return;

	if (is_user && current->state != TASK_RUNNING)
		return;

	/*
	 * Only the kernel side is implemented for now.
	 */
	if (!is_user)
		perf_callchain_kernel(regs, entry);
}

/*
 * No need for separate IRQ and NMI entries.
 */
static DEFINE_PER_CPU(struct perf_callchain_entry, callchain);

struct perf_callchain_entry *perf_callchain(struct pt_regs *regs)
{
	struct perf_callchain_entry *entry = &__get_cpu_var(callchain);

	entry->nr = 0;

	perf_do_callchain(regs, entry);

	return entry;
}
