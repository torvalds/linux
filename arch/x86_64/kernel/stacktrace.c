/*
 * arch/x86_64/kernel/stacktrace.c
 *
 * Stack trace management functions
 *
 *  Copyright (C) 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 */
#include <linux/sched.h>
#include <linux/stacktrace.h>

#include <asm/smp.h>

static inline int
in_range(unsigned long start, unsigned long addr, unsigned long end)
{
	return addr >= start && addr <= end;
}

static unsigned long
get_stack_end(struct task_struct *task, unsigned long stack)
{
	unsigned long stack_start, stack_end, flags;
	int i, cpu;

	/*
	 * The most common case is that we are in the task stack:
	 */
	stack_start = (unsigned long)task->thread_info;
	stack_end = stack_start + THREAD_SIZE;

	if (in_range(stack_start, stack, stack_end))
		return stack_end;

	/*
	 * We are in an interrupt if irqstackptr is set:
	 */
	raw_local_irq_save(flags);
	cpu = safe_smp_processor_id();
	stack_end = (unsigned long)cpu_pda(cpu)->irqstackptr;

	if (stack_end) {
		stack_start = stack_end & ~(IRQSTACKSIZE-1);
		if (in_range(stack_start, stack, stack_end))
			goto out_restore;
		/*
		 * We get here if we are in an IRQ context but we
		 * are also in an exception stack.
		 */
	}

	/*
	 * Iterate over all exception stacks, and figure out whether
	 * 'stack' is in one of them:
	 */
	for (i = 0; i < N_EXCEPTION_STACKS; i++) {
		/*
		 * set 'end' to the end of the exception stack.
		 */
		stack_end = per_cpu(init_tss, cpu).ist[i];
		stack_start = stack_end - EXCEPTION_STKSZ;

		/*
		 * Is 'stack' above this exception frame's end?
		 * If yes then skip to the next frame.
		 */
		if (stack >= stack_end)
			continue;
		/*
		 * Is 'stack' above this exception frame's start address?
		 * If yes then we found the right frame.
		 */
		if (stack >= stack_start)
			goto out_restore;

		/*
		 * If this is a debug stack, and if it has a larger size than
		 * the usual exception stacks, then 'stack' might still
		 * be within the lower portion of the debug stack:
		 */
#if DEBUG_STKSZ > EXCEPTION_STKSZ
		if (i == DEBUG_STACK - 1 && stack >= stack_end - DEBUG_STKSZ) {
			/*
			 * Black magic. A large debug stack is composed of
			 * multiple exception stack entries, which we
			 * iterate through now. Dont look:
			 */
			do {
				stack_end -= EXCEPTION_STKSZ;
				stack_start -= EXCEPTION_STKSZ;
			} while (stack < stack_start);

			goto out_restore;
		}
#endif
	}
	/*
	 * Ok, 'stack' is not pointing to any of the system stacks.
	 */
	stack_end = 0;

out_restore:
	raw_local_irq_restore(flags);

	return stack_end;
}


/*
 * Save stack-backtrace addresses into a stack_trace buffer:
 */
static inline unsigned long
save_context_stack(struct stack_trace *trace,
		   unsigned long stack, unsigned long stack_end)
{
	int skip = trace->skip;
	unsigned long addr;

#ifdef CONFIG_FRAME_POINTER
	unsigned long prev_stack = 0;

	while (in_range(prev_stack, stack, stack_end)) {
		pr_debug("stack:          %p\n", (void *)stack);
		addr = (unsigned long)(((unsigned long *)stack)[1]);
		pr_debug("addr:           %p\n", (void *)addr);
		if (!skip)
			trace->entries[trace->nr_entries++] = addr-1;
		else
			skip--;
		if (trace->nr_entries >= trace->max_entries)
			break;
		if (!addr)
			return 0;
		/*
		 * Stack frames must go forwards (otherwise a loop could
		 * happen if the stackframe is corrupted), so we move
		 * prev_stack forwards:
		 */
		prev_stack = stack;
		stack = (unsigned long)(((unsigned long *)stack)[0]);
	}
	pr_debug("invalid:        %p\n", (void *)stack);
#else
	while (stack < stack_end) {
		addr = ((unsigned long *)stack)[0];
		stack += sizeof(long);
		if (__kernel_text_address(addr)) {
			if (!skip)
				trace->entries[trace->nr_entries++] = addr-1;
			else
				skip--;
			if (trace->nr_entries >= trace->max_entries)
				break;
		}
	}
#endif
	return stack;
}

#define MAX_STACKS 10

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 */
void save_stack_trace(struct stack_trace *trace, struct task_struct *task)
{
	unsigned long stack = (unsigned long)&stack;
	int i, nr_stacks = 0, stacks_done[MAX_STACKS];

	WARN_ON(trace->nr_entries || !trace->max_entries);

	if (!task)
		task = current;

	pr_debug("task: %p, ti: %p\n", task, task->thread_info);

	if (!task || task == current) {
		/* Grab rbp right from our regs: */
		asm ("mov %%rbp, %0" : "=r" (stack));
		pr_debug("rbp:            %p\n", (void *)stack);
	} else {
		/* rbp is the last reg pushed by switch_to(): */
		stack = task->thread.rsp;
		pr_debug("other task rsp: %p\n", (void *)stack);
		stack = (unsigned long)(((unsigned long *)stack)[0]);
		pr_debug("other task rbp: %p\n", (void *)stack);
	}

	while (1) {
		unsigned long stack_end = get_stack_end(task, stack);

		pr_debug("stack:          %p\n", (void *)stack);
		pr_debug("stack end:      %p\n", (void *)stack_end);

		/*
		 * Invalid stack addres?
		 */
		if (!stack_end)
			return;
		/*
		 * Were we in this stack already? (recursion)
		 */
		for (i = 0; i < nr_stacks; i++)
			if (stacks_done[i] == stack_end)
				return;
		stacks_done[nr_stacks] = stack_end;

		stack = save_context_stack(trace, stack, stack_end);
		if (!stack || trace->nr_entries >= trace->max_entries)
			return;
		trace->entries[trace->nr_entries++] = ULONG_MAX;
		if (trace->nr_entries >= trace->max_entries)
			return;
		if (++nr_stacks >= MAX_STACKS)
			return;
	}
}

