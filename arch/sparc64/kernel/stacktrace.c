#include <linux/sched.h>
#include <linux/stacktrace.h>
#include <linux/thread_info.h>
#include <asm/ptrace.h>

void save_stack_trace(struct stack_trace *trace, struct task_struct *task)
{
	unsigned long ksp, fp, thread_base;
	struct thread_info *tp;

	if (!task)
		task = current;
	tp = task_thread_info(task);
	if (task == current) {
		flushw_all();
		__asm__ __volatile__(
			"mov	%%fp, %0"
			: "=r" (ksp)
		);
	} else
		ksp = tp->ksp;

	fp = ksp + STACK_BIAS;
	thread_base = (unsigned long) tp;
	do {
		struct reg_window *rw;

		/* Bogus frame pointer? */
		if (fp < (thread_base + sizeof(struct thread_info)) ||
		    fp >= (thread_base + THREAD_SIZE))
			break;

		rw = (struct reg_window *) fp;
		if (trace->skip > 0)
			trace->skip--;
		else
			trace->entries[trace->nr_entries++] = rw->ins[7];

		fp = rw->ins[6] + STACK_BIAS;
	} while (trace->nr_entries < trace->max_entries);
}
