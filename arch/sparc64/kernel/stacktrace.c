#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/thread_info.h>
#include <linux/module.h>
#include <asm/ptrace.h>
#include <asm/stacktrace.h>

void save_stack_trace(struct stack_trace *trace)
{
	unsigned long ksp, fp, thread_base;
	struct thread_info *tp = task_thread_info(current);

	stack_trace_flush();

	__asm__ __volatile__(
		"mov	%%fp, %0"
		: "=r" (ksp)
	);

	fp = ksp + STACK_BIAS;
	thread_base = (unsigned long) tp;
	do {
		struct sparc_stackf *sf;
		struct pt_regs *regs;
		unsigned long pc;

		/* Bogus frame pointer? */
		if (fp < (thread_base + sizeof(struct thread_info)) ||
		    fp > (thread_base + THREAD_SIZE - sizeof(struct sparc_stackf)))
			break;

		sf = (struct sparc_stackf *) fp;
		regs = (struct pt_regs *) (sf + 1);

		if (((unsigned long)regs <=
		     (thread_base + THREAD_SIZE - sizeof(*regs))) &&
		    (regs->magic & ~0x1ff) == PT_REGS_MAGIC) {
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
