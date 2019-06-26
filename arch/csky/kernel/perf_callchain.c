// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/perf_event.h>
#include <linux/uaccess.h>

/* Kernel callchain */
struct stackframe {
	unsigned long fp;
	unsigned long lr;
};

static int unwind_frame_kernel(struct stackframe *frame)
{
	if (kstack_end((void *)frame->fp))
		return -EPERM;
	if (frame->fp & 0x3 || frame->fp < TASK_SIZE)
		return -EPERM;

	*frame = *(struct stackframe *)frame->fp;
	if (__kernel_text_address(frame->lr)) {
		int graph = 0;

		frame->lr = ftrace_graph_ret_addr(NULL, &graph, frame->lr,
				NULL);
	}
	return 0;
}

static void notrace walk_stackframe(struct stackframe *fr,
			struct perf_callchain_entry_ctx *entry)
{
	do {
		perf_callchain_store(entry, fr->lr);
	} while (unwind_frame_kernel(fr) >= 0);
}

/*
 * Get the return address for a single stackframe and return a pointer to the
 * next frame tail.
 */
static unsigned long user_backtrace(struct perf_callchain_entry_ctx *entry,
			unsigned long fp, unsigned long reg_lr)
{
	struct stackframe buftail;
	unsigned long lr = 0;
	unsigned long *user_frame_tail = (unsigned long *)fp;

	/* Check accessibility of one struct frame_tail beyond */
	if (!access_ok(user_frame_tail, sizeof(buftail)))
		return 0;
	if (__copy_from_user_inatomic(&buftail, user_frame_tail,
				      sizeof(buftail)))
		return 0;

	if (reg_lr != 0)
		lr = reg_lr;
	else
		lr = buftail.lr;

	fp = buftail.fp;
	perf_callchain_store(entry, lr);

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
 * On C-SKY platform, the program being sampled and the C library
 * need to be compiled with * -mbacktrace, otherwise the user
 * stack will not contain function frame.
 */
void perf_callchain_user(struct perf_callchain_entry_ctx *entry,
			 struct pt_regs *regs)
{
	unsigned long fp = 0;

	/* C-SKY does not support virtualization. */
	if (perf_guest_cbs && perf_guest_cbs->is_in_guest())
		return;

	fp = regs->regs[4];
	perf_callchain_store(entry, regs->pc);

	/*
	 * While backtrace from leaf function, lr is normally
	 * not saved inside frame on C-SKY, so get lr from pt_regs
	 * at the sample point. However, lr value can be incorrect if
	 * lr is used as temp register
	 */
	fp = user_backtrace(entry, fp, regs->lr);

	while (fp && !(fp & 0x3) && entry->nr < entry->max_stack)
		fp = user_backtrace(entry, fp, 0);
}

void perf_callchain_kernel(struct perf_callchain_entry_ctx *entry,
			   struct pt_regs *regs)
{
	struct stackframe fr;

	/* C-SKY does not support virtualization. */
	if (perf_guest_cbs && perf_guest_cbs->is_in_guest()) {
		pr_warn("C-SKY does not support perf in guest mode!");
		return;
	}

	fr.fp = regs->regs[4];
	fr.lr = regs->lr;
	walk_stackframe(&fr, entry);
}
