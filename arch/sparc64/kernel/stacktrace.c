#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/thread_info.h>
#include <linux/module.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>

#include "kstack.h"

void save_stack_trace(struct stack_trace *trace)
{
	struct thread_info *tp = task_thread_info(current);
	unsigned long ksp, fp;

	stack_trace_flush();

	__asm__ __volatile__(
		"mov	%%fp, %0"
		: "=r" (ksp)
	);

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
		else
			trace->entries[trace->nr_entries++] = pc;
	} while (trace->nr_entries < trace->max_entries);
}
EXPORT_SYMBOL_GPL(save_stack_trace);
