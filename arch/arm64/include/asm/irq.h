#ifndef __ASM_IRQ_H
#define __ASM_IRQ_H

#define IRQ_STACK_SIZE			THREAD_SIZE
#define IRQ_STACK_START_SP		THREAD_START_SP

#ifndef __ASSEMBLER__

#include <linux/percpu.h>

#include <asm-generic/irq.h>
#include <asm/thread_info.h>

#define __ARCH_HAS_DO_SOFTIRQ

struct pt_regs;

DECLARE_PER_CPU(unsigned long [IRQ_STACK_SIZE/sizeof(long)], irq_stack);

/*
 * The highest address on the stack, and the first to be used. Used to
 * find the dummy-stack frame put down by el?_irq() in entry.S.
 */
#define IRQ_STACK_PTR(cpu) ((unsigned long)per_cpu(irq_stack, cpu) + IRQ_STACK_START_SP)

/*
 * The offset from irq_stack_ptr where entry.S will store the original
 * stack pointer. Used by unwind_frame() and dump_backtrace().
 */
#define IRQ_STACK_TO_TASK_STACK(ptr) *((unsigned long *)(ptr - 0x10));

extern void set_handle_irq(void (*handle_irq)(struct pt_regs *));

static inline int nr_legacy_irqs(void)
{
	return 0;
}

static inline bool on_irq_stack(unsigned long sp, int cpu)
{
	/* variable names the same as kernel/stacktrace.c */
	unsigned long low = (unsigned long)per_cpu(irq_stack, cpu);
	unsigned long high = low + IRQ_STACK_START_SP;

	return (low <= sp && sp <= high);
}

#endif /* !__ASSEMBLER__ */
#endif
