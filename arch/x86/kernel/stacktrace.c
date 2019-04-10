/*
 * Stack trace management functions
 *
 *  Copyright (C) 2006-2009 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/stacktrace.h>
#include <linux/export.h>
#include <linux/uaccess.h>
#include <asm/stacktrace.h>
#include <asm/unwind.h>

static int save_stack_address(struct stack_trace *trace, unsigned long addr,
			      bool nosched)
{
	if (nosched && in_sched_functions(addr))
		return 0;

	if (trace->skip > 0) {
		trace->skip--;
		return 0;
	}

	if (trace->nr_entries >= trace->max_entries)
		return -1;

	trace->entries[trace->nr_entries++] = addr;
	return 0;
}

static void noinline __save_stack_trace(struct stack_trace *trace,
			       struct task_struct *task, struct pt_regs *regs,
			       bool nosched)
{
	struct unwind_state state;
	unsigned long addr;

	if (regs)
		save_stack_address(trace, regs->ip, nosched);

	for (unwind_start(&state, task, regs, NULL); !unwind_done(&state);
	     unwind_next_frame(&state)) {
		addr = unwind_get_return_address(&state);
		if (!addr || save_stack_address(trace, addr, nosched))
			break;
	}
}

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace(struct stack_trace *trace)
{
	trace->skip++;
	__save_stack_trace(trace, current, NULL, false);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

void save_stack_trace_regs(struct pt_regs *regs, struct stack_trace *trace)
{
	__save_stack_trace(trace, current, regs, false);
}

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	if (!try_get_task_stack(tsk))
		return;

	if (tsk == current)
		trace->skip++;
	__save_stack_trace(trace, tsk, NULL, true);

	put_task_stack(tsk);
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);

#ifdef CONFIG_HAVE_RELIABLE_STACKTRACE

static int __always_inline
__save_stack_trace_reliable(struct stack_trace *trace,
			    struct task_struct *task)
{
	struct unwind_state state;
	struct pt_regs *regs;
	unsigned long addr;

	for (unwind_start(&state, task, NULL, NULL);
	     !unwind_done(&state) && !unwind_error(&state);
	     unwind_next_frame(&state)) {

		regs = unwind_get_entry_regs(&state, NULL);
		if (regs) {
			/* Success path for user tasks */
			if (user_mode(regs))
				return 0;

			/*
			 * Kernel mode registers on the stack indicate an
			 * in-kernel interrupt or exception (e.g., preemption
			 * or a page fault), which can make frame pointers
			 * unreliable.
			 */

			if (IS_ENABLED(CONFIG_FRAME_POINTER))
				return -EINVAL;
		}

		addr = unwind_get_return_address(&state);

		/*
		 * A NULL or invalid return address probably means there's some
		 * generated code which __kernel_text_address() doesn't know
		 * about.
		 */
		if (!addr)
			return -EINVAL;

		if (save_stack_address(trace, addr, false))
			return -EINVAL;
	}

	/* Check for stack corruption */
	if (unwind_error(&state))
		return -EINVAL;

	/* Success path for non-user tasks, i.e. kthreads and idle tasks */
	if (!(task->flags & (PF_KTHREAD | PF_IDLE)))
		return -EINVAL;

	return 0;
}

/*
 * This function returns an error if it detects any unreliable features of the
 * stack.  Otherwise it guarantees that the stack trace is reliable.
 *
 * If the task is not 'current', the caller *must* ensure the task is inactive.
 */
int save_stack_trace_tsk_reliable(struct task_struct *tsk,
				  struct stack_trace *trace)
{
	int ret;

	/*
	 * If the task doesn't have a stack (e.g., a zombie), the stack is
	 * "reliably" empty.
	 */
	if (!try_get_task_stack(tsk))
		return 0;

	ret = __save_stack_trace_reliable(trace, tsk);

	put_task_stack(tsk);

	return ret;
}
#endif /* CONFIG_HAVE_RELIABLE_STACKTRACE */

/* Userspace stacktrace - based on kernel/trace/trace_sysprof.c */

struct stack_frame_user {
	const void __user	*next_fp;
	unsigned long		ret_addr;
};

static int
copy_stack_frame(const void __user *fp, struct stack_frame_user *frame)
{
	int ret;

	if (!access_ok(fp, sizeof(*frame)))
		return 0;

	ret = 1;
	pagefault_disable();
	if (__copy_from_user_inatomic(frame, fp, sizeof(*frame)))
		ret = 0;
	pagefault_enable();

	return ret;
}

static inline void __save_stack_trace_user(struct stack_trace *trace)
{
	const struct pt_regs *regs = task_pt_regs(current);
	const void __user *fp = (const void __user *)regs->bp;

	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = regs->ip;

	while (trace->nr_entries < trace->max_entries) {
		struct stack_frame_user frame;

		frame.next_fp = NULL;
		frame.ret_addr = 0;
		if (!copy_stack_frame(fp, &frame))
			break;
		if ((unsigned long)fp < regs->sp)
			break;
		if (frame.ret_addr) {
			trace->entries[trace->nr_entries++] =
				frame.ret_addr;
		}
		if (fp == frame.next_fp)
			break;
		fp = frame.next_fp;
	}
}

void save_stack_trace_user(struct stack_trace *trace)
{
	/*
	 * Trace user stack if we are not a kernel thread
	 */
	if (current->mm)
		__save_stack_trace_user(trace);
}
