#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/thread_info.h>
#include <linux/module.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>

#include "kstack.h"

static void __save_stack_trace(struct thread_info *tp,
			       struct stack_trace *trace,
			       bool skip_sched)
{
	unsigned long ksp, fp;

	if (tp == current_thread_info()) {
		stack_trace_flush();
		__asm__ __volatile__("mov %%fp, %0" : "=r" (ksp));
	} else {
		ksp = tp->ksp;
	}

	fp = ksp + STACK_BIAS;
	do {
		struct sparc_stackf *sf;
		struct pt_regs *regs;
		unsigned long pc;

		if (!kstack_valid(tp, fp))
			break;

		sf = (struct sparc_stackf *) fp;
		regs = (struct pt_regs *) (sf + 1);

		if (kstack_is_trap_frame(tp, regs)) {
			if (!(regs->tstate & TSTATE_PRIV))
				break;
			pc = regs->tpc;
			fp = regs->u_regs[UREG_I6] + STACK_BIAS;
		} else {
			pc = sf->callers_pc;
			fp = (unsigned long)sf->fp + STACK_BIAS;
		}

		if (trace->skip > 0)
			trace->skip--;
		else if (!skip_sched || !in_sched_functions(pc))
			trace->entries[trace->nr_entries++] = pc;
	} while (trace->nr_entries < trace->max_entries);
}

void save_stack_trace(struct stack_trace *trace)
{
	__save_stack_trace(current_thread_info(), trace, false);
}
EXPORT_SYMBOL_GPL(save_stack_trace);

void save_stack_trace_tsk(struct task_struct *tsk, struct stack_trace *trace)
{
	struct thread_info *tp = task_thread_info(tsk);

	__save_stack_trace(tp, trace, true);
}
EXPORT_SYMBOL_GPL(save_stack_trace_tsk);
