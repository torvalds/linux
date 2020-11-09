/**
 * @file backtrace.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon
 * @author David Smith
 */

#include <linux/oprofile.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/compat.h>
#include <linux/uaccess.h>

#include <asm/ptrace.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>

#ifdef CONFIG_COMPAT
static struct stack_frame_ia32 *
dump_user_backtrace_32(struct stack_frame_ia32 *head)
{
	/* Also check accessibility of one struct frame_head beyond: */
	struct stack_frame_ia32 bufhead[2];
	struct stack_frame_ia32 *fp;
	unsigned long bytes;

	bytes = copy_from_user_nmi(bufhead, head, sizeof(bufhead));
	if (bytes != 0)
		return NULL;

	fp = (struct stack_frame_ia32 *) compat_ptr(bufhead[0].next_frame);

	oprofile_add_trace(bufhead[0].return_address);

	/* frame pointers should strictly progress back up the stack
	* (towards higher addresses) */
	if (head >= fp)
		return NULL;

	return fp;
}

static inline int
x86_backtrace_32(struct pt_regs * const regs, unsigned int depth)
{
	struct stack_frame_ia32 *head;

	/* User process is IA32 */
	if (!current || user_64bit_mode(regs))
		return 0;

	head = (struct stack_frame_ia32 *) regs->bp;
	while (depth-- && head)
		head = dump_user_backtrace_32(head);

	return 1;
}

#else
static inline int
x86_backtrace_32(struct pt_regs * const regs, unsigned int depth)
{
	return 0;
}
#endif /* CONFIG_COMPAT */

static struct stack_frame *dump_user_backtrace(struct stack_frame *head)
{
	/* Also check accessibility of one struct frame_head beyond: */
	struct stack_frame bufhead[2];
	unsigned long bytes;

	bytes = copy_from_user_nmi(bufhead, head, sizeof(bufhead));
	if (bytes != 0)
		return NULL;

	oprofile_add_trace(bufhead[0].return_address);

	/* frame pointers should strictly progress back up the stack
	 * (towards higher addresses) */
	if (head >= bufhead[0].next_frame)
		return NULL;

	return bufhead[0].next_frame;
}

void
x86_backtrace(struct pt_regs * const regs, unsigned int depth)
{
	struct stack_frame *head = (struct stack_frame *)frame_pointer(regs);

	if (!user_mode(regs)) {
		struct unwind_state state;
		unsigned long addr;

		if (!depth)
			return;

		oprofile_add_trace(regs->ip);

		if (!--depth)
			return;

		for (unwind_start(&state, current, regs, NULL);
		     !unwind_done(&state); unwind_next_frame(&state)) {
			addr = unwind_get_return_address(&state);
			if (!addr)
				break;

			oprofile_add_trace(addr);

			if (!--depth)
				break;
		}

		return;
	}

	if (x86_backtrace_32(regs, depth))
		return;

	while (depth-- && head)
		head = dump_user_backtrace(head);
}
