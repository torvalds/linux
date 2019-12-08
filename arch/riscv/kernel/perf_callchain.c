// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Hangzhou C-SKY Microsystems co.,ltd. */

#include <linux/perf_event.h>
#include <linux/uaccess.h>

/* Kernel callchain */
struct stackframe {
	unsigned long fp;
	unsigned long ra;
};

/*
 * Get the return address for a single stackframe and return a pointer to the
 * next frame tail.
 */
static unsigned long user_backtrace(struct perf_callchain_entry_ctx *entry,
				    unsigned long fp, unsigned long reg_ra)
{
	struct stackframe buftail;
	unsigned long ra = 0;
	unsigned long *user_frame_tail =
			(unsigned long *)(fp - sizeof(struct stackframe));

	/* Check accessibility of one struct frame_tail beyond */
	if (!access_ok(user_frame_tail, sizeof(buftail)))
		return 0;
	if (__copy_from_user_inatomic(&buftail, user_frame_tail,
				      sizeof(buftail)))
		return 0;

	if (reg_ra != 0)
		ra = reg_ra;
	else
		ra = buftail.ra;

	fp = buftail.fp;
	if (ra != 0)
		perf_callchain_store(entry, ra);
	else
		return 0;

	return fp;
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
	unsigned long fp = 0;

	/* RISC-V does not support perf in guest mode. */
	if (perf_guest_cbs && perf_guest_cbs->is_in_guest())
		return;

	fp = regs->s0;
	perf_callchain_store(entry, regs->epc);

	fp = user_backtrace(entry, fp, regs->ra);
	while (fp && !(fp & 0x3) && entry->nr < entry->max_stack)
		fp = user_backtrace(entry, fp, 0);
}

bool fill_callchain(unsigned long pc, void *entry)
{
	return perf_callchain_store(entry, pc);
}

void notrace walk_stackframe(struct task_struct *task,
	struct pt_regs *regs, bool (*fn)(unsigned long, void *), void *arg);
void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
			   struct pt_regs *regs)
{
	/* RISC-V does not support perf in guest mode. */
	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		pr_warn("RISC-V does not support perf in guest mode!");
		return;
	}

	walk_stackframe(NULL, regs, fill_callchain, entry);
}
