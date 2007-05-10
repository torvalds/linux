/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 */

#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/ptrace.h>
#include <linux/hardirq.h>
#include <asm/irq.h>
#include <asm/errno.h>

#define DEFINE(sym, val) \
	asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main (void)
{
	/* offsets into the task struct */
	DEFINE (TASK_STATE, offsetof (struct task_struct, state));
	DEFINE (TASK_FLAGS, offsetof (struct task_struct, flags));
	DEFINE (TASK_PTRACE, offsetof (struct task_struct, ptrace));
	DEFINE (TASK_BLOCKED, offsetof (struct task_struct, blocked));
	DEFINE (TASK_THREAD, offsetof (struct task_struct, thread));
	DEFINE (TASK_THREAD_INFO, offsetof (struct task_struct, stack));
	DEFINE (TASK_MM, offsetof (struct task_struct, mm));
	DEFINE (TASK_ACTIVE_MM, offsetof (struct task_struct, active_mm));
	DEFINE (TASK_PID, offsetof (struct task_struct, pid));

	/* offsets into the kernel_stat struct */
	DEFINE (STAT_IRQ, offsetof (struct kernel_stat, irqs));


	/* signal defines */
	DEFINE (SIGSEGV, SIGSEGV);
	DEFINE (SEGV_MAPERR, SEGV_MAPERR);
	DEFINE (SIGTRAP, SIGTRAP);
	DEFINE (SIGCHLD, SIGCHLD);
	DEFINE (SIGILL, SIGILL);
	DEFINE (TRAP_TRACE, TRAP_TRACE);

	/* ptrace flag bits */
	DEFINE (PT_PTRACED, PT_PTRACED);
	DEFINE (PT_DTRACE, PT_DTRACE);

	/* error values */
	DEFINE (ENOSYS, ENOSYS);

	/* clone flag bits */
	DEFINE (CLONE_VFORK, CLONE_VFORK);
	DEFINE (CLONE_VM, CLONE_VM);

	return 0;
}
