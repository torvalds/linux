// SPDX-License-Identifier: GPL-2.0
/*
 * Stack trace management functions
 *
 * Copyright (C) 2022 Loongson Technology Corporation Limited
 */
#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/uaccess.h>

#include <asm/stacktrace.h>
#include <asm/unwind.h>

void arch_stack_walk(stack_trace_consume_fn consume_entry, void *cookie,
		     struct task_struct *task, struct pt_regs *regs)
{
	unsigned long addr;
	struct pt_regs dummyregs;
	struct unwind_state state;

	regs = &dummyregs;

	if (task == current) {
		regs->regs[3] = (unsigned long)__builtin_frame_address(0);
		regs->csr_era = (unsigned long)__builtin_return_address(0);
	} else {
		regs->regs[3] = thread_saved_fp(task);
		regs->csr_era = thread_saved_ra(task);
	}

	regs->regs[1] = 0;
	for (unwind_start(&state, task, regs);
	      !unwind_done(&state); unwind_next_frame(&state)) {
		addr = unwind_get_return_address(&state);
		if (!addr || !consume_entry(cookie, addr))
			break;
	}
}

static int
copy_stack_frame(unsigned long fp, struct stack_frame *frame)
{
	int ret = 1;
	unsigned long err;
	unsigned long __user *user_frame_tail;

	user_frame_tail = (unsigned long *)(fp - sizeof(struct stack_frame));
	if (!access_ok(user_frame_tail, sizeof(*frame)))
		return 0;

	pagefault_disable();
	err = (__copy_from_user_inatomic(frame, user_frame_tail, sizeof(*frame)));
	if (err || (unsigned long)user_frame_tail >= frame->fp)
		ret = 0;
	pagefault_enable();

	return ret;
}

void arch_stack_walk_user(stack_trace_consume_fn consume_entry, void *cookie,
			  const struct pt_regs *regs)
{
	unsigned long fp = regs->regs[22];

	while (fp && !((unsigned long)fp & 0xf)) {
		struct stack_frame frame;

		frame.fp = 0;
		frame.ra = 0;
		if (!copy_stack_frame(fp, &frame))
			break;
		if (!frame.ra)
			break;
		if (!consume_entry(cookie, frame.ra))
			break;
		fp = frame.fp;
	}
}
