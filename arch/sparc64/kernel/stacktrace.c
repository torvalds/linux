#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/thread_info.h>
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
		struct reg_window *rw;
		struct pt_regs *regs;
		unsigned long pc;

		/* Bogus frame pointer? */
		if (fp < (thread_base + sizeof(struct thread_info)) ||
		    fp >= (thread_base + THREAD_SIZE))
			break;

		rw = (struct reg_window *) fp;
		regs = (struct pt_regs *) (rw + 1);

		if ((regs->magic & ~0x1ff) == PT_REGS_MAGIC) {
			pc = regs->tpc;
			fp = regs->u_regs[UREG_I6] + STACK_BIAS;
		} else {
			pc = rw->ins[7];
			fp = rw->ins[6] + STACK_BIAS;
		}

		if (trace->skip > 0)
			trace->skip--;
		else
			trace->entries[trace->nr_entries++] = pc;
	} while (trace->nr_entries < trace->max_entries);
}
