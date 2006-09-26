/*
 * arch/i386/kernel/stacktrace.c
 *
 * Stack trace management functions
 *
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#include <linux/sched.h>
#include <linux/stacktrace.h>

static inline int valid_stack_ptr(struct thread_info *tinfo, void *p)
{
	return	p > (void *)tinfo &&
		p < (void *)tinfo + THREAD_SIZE - 3;
}

/*
 * Save stack-backtrace addresses into a stack_trace buffer:
 */
static inline unsigned long
save_context_stack(struct stack_trace *trace, unsigned int skip,
		   struct thread_info *tinfo, unsigned long *stack,
		   unsigned long ebp)
{
	unsigned long addr;

#ifdef CONFIG_FRAME_POINTER
	while (valid_stack_ptr(tinfo, (void *)ebp)) {
		addr = *(unsigned long *)(ebp + 4);
		if (!skip)
			trace->entries[trace->nr_entries++] = addr;
		else
			skip--;
		if (trace->nr_entries >= trace->max_entries)
			break;
		/*
		 * break out of recursive entries (such as
		 * end_of_stack_stop_unwind_function):
	 	 */
		if (ebp == *(unsigned long *)ebp)
			break;

		ebp = *(unsigned long *)ebp;
	}
#else
	while (valid_stack_ptr(tinfo, stack)) {
		addr = *stack++;
		if (__kernel_text_address(addr)) {
			if (!skip)
				trace->entries[trace->nr_entries++] = addr;
			else
				skip--;
			if (trace->nr_entries >= trace->max_entries)
				break;
		}
	}
#endif

	return ebp;
}

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace(struct stack_trace *trace, struct task_struct *task)
{
	unsigned long ebp;
	unsigned long *stack = &ebp;

	WARN_ON(trace->nr_entries || !trace->max_entries);

	if (!task || task == current) {
		/* Grab ebp right from our regs: */
		asm ("movl %%ebp, %0" : "=r" (ebp));
	} else {
		/* ebp is the last reg pushed by switch_to(): */
		ebp = *(unsigned long *) task->thread.esp;
	}

	while (1) {
		struct thread_info *context = (struct thread_info *)
				((unsigned long)stack & (~(THREAD_SIZE - 1)));

		ebp = save_context_stack(trace, trace->skip, context, stack, ebp);
		stack = (unsigned long *)context->previous_esp;
		if (!stack || trace->nr_entries >= trace->max_entries)
			break;
		trace->entries[trace->nr_entries++] = ULONG_MAX;
		if (trace->nr_entries >= trace->max_entries)
			break;
	}
}

