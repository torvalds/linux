// SPDX-License-Identifier: GPL-2.0
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
#include <linux/kbuild.h>
#include <asm/irq.h>
#include <asm/ptrace.h>

int main(void)
{
	/* offsets into the task struct */
	OFFSET(TASK_STATE, task_struct, state);
	OFFSET(TASK_FLAGS, task_struct, flags);
	OFFSET(TASK_PTRACE, task_struct, ptrace);
	OFFSET(TASK_BLOCKED, task_struct, blocked);
	OFFSET(TASK_THREAD, task_struct, thread);
	OFFSET(TASK_THREAD_INFO, task_struct, stack);
	OFFSET(TASK_MM, task_struct, mm);
	OFFSET(TASK_ACTIVE_MM, task_struct, active_mm);

	/* offsets into the irq_cpustat_t struct */
	DEFINE(CPUSTAT_SOFTIRQ_PENDING, offsetof(irq_cpustat_t,
						 __softirq_pending));

	/* offsets into the thread struct */
	OFFSET(THREAD_KSP, thread_struct, ksp);
	OFFSET(THREAD_USP, thread_struct, usp);
	OFFSET(THREAD_CCR, thread_struct, ccr);

	/* offsets into the pt_regs struct */
	DEFINE(LER0,  offsetof(struct pt_regs, er0)      - sizeof(long));
	DEFINE(LER1,  offsetof(struct pt_regs, er1)      - sizeof(long));
	DEFINE(LER2,  offsetof(struct pt_regs, er2)      - sizeof(long));
	DEFINE(LER3,  offsetof(struct pt_regs, er3)      - sizeof(long));
	DEFINE(LER4,  offsetof(struct pt_regs, er4)      - sizeof(long));
	DEFINE(LER5,  offsetof(struct pt_regs, er5)      - sizeof(long));
	DEFINE(LER6,  offsetof(struct pt_regs, er6)      - sizeof(long));
	DEFINE(LORIG, offsetof(struct pt_regs, orig_er0) - sizeof(long));
	DEFINE(LSP,   offsetof(struct pt_regs, sp)       - sizeof(long));
	DEFINE(LCCR,  offsetof(struct pt_regs, ccr)      - sizeof(long));
	DEFINE(LVEC,  offsetof(struct pt_regs, vector)   - sizeof(long));
#if defined(CONFIG_CPU_H8S)
	DEFINE(LEXR,  offsetof(struct pt_regs, exr)      - sizeof(long));
#endif
	DEFINE(LRET,  offsetof(struct pt_regs, pc)       - sizeof(long));

	DEFINE(PT_PTRACED, PT_PTRACED);

	/* offsets in thread_info structure */
	OFFSET(TI_TASK, thread_info, task);
	OFFSET(TI_FLAGS, thread_info, flags);
	OFFSET(TI_CPU, thread_info, cpu);
	OFFSET(TI_PRE, thread_info, preempt_count);
#ifdef CONFIG_PREEMPTION
	DEFINE(TI_PRE_COUNT, offsetof(struct thread_info, preempt_count));
#endif

	return 0;
}
