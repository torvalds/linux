/*
 * Stack trace management functions
 *
 *  Copyright IBM Corp. 2006
 *  Author(s): Heiko Carstens <heiko.carstens@de.ibm.com>
 */

#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/kallsyms.h>
#include <linux/module.h>

static unsigned long save_context_stack(struct stack_trace *trace,
					unsigned long sp,
					unsigned long low,
					unsigned long high,
					int savesched)
{
	struct stack_frame *sf;
	struct pt_regs *regs;
	unsigned long addr;

	while(1) {
		if (sp < low || sp > high)
			return sp;
		sf = (struct stack_frame *)sp;
		while(1) {
			addr = sf->gprs[8];
			if (!trace->skip)
				trace->entries[trace->nr_entries++] = addr;
			else
				trace->skip--;
			if (trace->nr_entries >= trace->max_entries)
				return sp;
			low = sp;
			sp = sf->back_chain;
			if (!sp)
				break;
			if (sp <= low || sp > high - sizeof(*sf))
				return sp;
			sf = (struct stack_frame *)sp;
		}
		/* Zero backchain detected, check for interrupt frame. */
		sp = (unsigned long)(sf + 1);
		if (sp <= low || sp > high - sizeof(*regs))
			return sp;
		regs = (struct pt_regs *)sp;
		addr = regs->psw.addr;
		if (savesched || !in_sched_functions(addr)) {
			if (!trace->skip)
				trace->entries[trace->nr_entries++] = addr;
			else
				trace->skip--;
		}
		if (trace->nr_entries >= trace->max_entries)
			return sp;
		low = sp;
		sp = regs->gprs[15];
	}
}

static void __save_stack_trace(struct stack_trace *trace, unsigned long sp)
{
	unsigned long new_sp, frame_size;

	frame_size = STACK_FRAME_OVERHEAD + sizeof(struct pt_regs);
	new_sp = save_context_stack(trace, sp,
			S390_lowcore.panic_stack + frame_size - PAGE_SIZE,
			S390_lowcore.panic_stack + frame_size, 1);
	new_sp = save_context_stack(trace, new_sp,
			S390_lowcore.async_stack + frame_size - ASYNC_SIZE,
			S390_lowcore.async_stack + frame_size, 1);
	save_context_stack(trace, new_sp,
			   S390_lowcore.thread_info,
			   S390_lowcore.thread_info + THREAD_SIZE, 1);
}

void save_stack_trace(struct stack_trace *trace)
{
	register unsigned long r15 asm ("15");
	unsigned long sp;

	sp = r15;
	__save_stack_trace(trace, sp);
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = ULONG_MAX;
}
EXPORT_SYMBOL_GPL(save_stack_trace);

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	unsigned long sp, low, high;

	sp = tsk->thread.ksp;
	if (tsk == current) {
		/* Get current stack pointer. */
		asm volatile("la %0,0(15)" : "=a" (sp));
	}
	low = (unsigned long) task_stack_page(tsk);
	high = (unsigned long) task_pt_regs(tsk);
	save_context_stack(trace, sp, low, high, 0);
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = ULONG_MAX;
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);

void save_stack_trace_regs(struct pt_regs *regs, struct stack_trace *trace)
{
	unsigned long sp;

	sp = kernel_stack_pointer(regs);
	__save_stack_trace(trace, sp);
	if (trace->nr_entries < trace->max_entries)
		trace->entries[trace->nr_entries++] = ULONG_MAX;
}
EXPORT_SYMBOL_GPL(save_stack_trace_regs);
