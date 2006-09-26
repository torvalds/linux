/*
 * arch/mips/kernel/stacktrace.c
 *
 * Stack trace management functions
 *
 *  Copyright (C) 2006 Atsushi Nemoto <anemo@mba.ocn.ne.jp>
 */
#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <asm/stacktrace.h>

/*
 * Save stack-backtrace addresses into a stack_trace buffer:
 */
static void save_raw_context_stack(struct stack_trace *trace,
	unsigned int skip, unsigned long reg29)
{
	unsigned long *sp = (unsigned long *)reg29;
	unsigned long addr;

	while (!kstack_end(sp)) {
		addr = *sp++;
		if (__kernel_text_address(addr)) {
			if (!skip)
				trace->entries[trace->nr_entries++] = addr;
			else
				skip--;
			if (trace->nr_entries >= trace->max_entries)
				break;
		}
	}
}

static struct pt_regs * save_context_stack(struct stack_trace *trace,
	unsigned int skip, struct task_struct *task, struct pt_regs *regs)
{
	unsigned long sp = regs->regs[29];
#ifdef CONFIG_KALLSYMS
	unsigned long ra = regs->regs[31];
	unsigned long pc = regs->cp0_epc;
	extern void ret_from_irq(void);

	if (raw_show_trace || !__kernel_text_address(pc)) {
		save_raw_context_stack(trace, skip, sp);
		return NULL;
	}
	do {
		if (!skip)
			trace->entries[trace->nr_entries++] = pc;
		else
			skip--;
		if (trace->nr_entries >= trace->max_entries)
			break;
		/*
		 * If we reached the bottom of interrupt context,
		 * return saved pt_regs.
		 */
		if (pc == (unsigned long)ret_from_irq) {
			unsigned long stack_page =
				(unsigned long)task_stack_page(task);
			if (!stack_page ||
			    sp < stack_page ||
			    sp > stack_page + THREAD_SIZE - 32)
				break;
			return (struct pt_regs *)sp;
		}
		pc = unwind_stack(task, &sp, pc, ra);
		ra = 0;
	} while (pc);
#else
	save_raw_context_stack(sp);
#endif

	return NULL;
}

/*
 * Save stack-backtrace addresses into a stack_trace buffer.
 * If all_contexts is set, all contexts (hardirq, softirq and process)
 * are saved. If not set then only the current context is saved.
 */
void save_stack_trace(struct stack_trace *trace,
		      struct task_struct *task, int all_contexts,
		      unsigned int skip)
{
	struct pt_regs dummyregs;
	struct pt_regs *regs = &dummyregs;

	WARN_ON(trace->nr_entries || !trace->max_entries);

	if (task && task != current) {
		regs->regs[29] = task->thread.reg29;
		regs->regs[31] = 0;
		regs->cp0_epc = task->thread.reg31;
	} else {
		if (!task)
			task = current;
		prepare_frametrace(regs);
	}

	while (1) {
		regs = save_context_stack(trace, skip, task, regs);
		if (!all_contexts || !regs ||
		    trace->nr_entries >= trace->max_entries)
			break;
		trace->entries[trace->nr_entries++] = ULONG_MAX;
		if (trace->nr_entries >= trace->max_entries)
			break;
		skip = 0;
	}
}
