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
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/ptrace.h>

#define DEFINE(sym, val) \
        asm volatile("\n->" #sym " %0 " #val : : "i" (val))

#define BLANK() asm volatile("\n->" : : )

int main(void)
{
	/* offsets into the task struct */
	DEFINE(TASK_STATE, offsetof(struct task_struct, state));
	DEFINE(TASK_FLAGS, offsetof(struct task_struct, flags));
	DEFINE(TASK_PTRACE, offsetof(struct task_struct, ptrace));
	DEFINE(TASK_BLOCKED, offsetof(struct task_struct, blocked));
	DEFINE(TASK_THREAD, offsetof(struct task_struct, thread));
	DEFINE(TASK_THREAD_INFO, offsetof(struct task_struct, stack));
	DEFINE(TASK_MM, offsetof(struct task_struct, mm));
	DEFINE(TASK_ACTIVE_MM, offsetof(struct task_struct, active_mm));

	/* offsets into the irq_cpustat_t struct */
	DEFINE(CPUSTAT_SOFTIRQ_PENDING, offsetof(irq_cpustat_t, __softirq_pending));

	/* offsets into the thread struct */
	DEFINE(THREAD_KSP, offsetof(struct thread_struct, ksp));
	DEFINE(THREAD_USP, offsetof(struct thread_struct, usp));
	DEFINE(THREAD_CCR, offsetof(struct thread_struct, ccr));

	/* offsets into the pt_regs struct */
	DEFINE(LER0,  offsetof(struct pt_regs, er0)      - sizeof(long));
	DEFINE(LER1,  offsetof(struct pt_regs, er1)      - sizeof(long));
	DEFINE(LER2,  offsetof(struct pt_regs, er2)      - sizeof(long));
	DEFINE(LER3,  offsetof(struct pt_regs, er3)      - sizeof(long));
	DEFINE(LER4,  offsetof(struct pt_regs, er4)      - sizeof(long));
	DEFINE(LER5,  offsetof(struct pt_regs, er5)      - sizeof(long));
	DEFINE(LER6,  offsetof(struct pt_regs, er6)      - sizeof(long));
	DEFINE(LORIG, offsetof(struct pt_regs, orig_er0) - sizeof(long));
	DEFINE(LCCR,  offsetof(struct pt_regs, ccr)      - sizeof(long));
	DEFINE(LVEC,  offsetof(struct pt_regs, vector)   - sizeof(long));
#if defined(__H8300S__)
	DEFINE(LEXR,  offsetof(struct pt_regs, exr)      - sizeof(long));
#endif
	DEFINE(LRET,  offsetof(struct pt_regs, pc)       - sizeof(long));

	DEFINE(PT_PTRACED, PT_PTRACED);
	DEFINE(PT_DTRACE, PT_DTRACE);

	return 0;
}
