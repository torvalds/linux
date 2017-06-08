#ifndef __ASM_IRQ_H
#define __ASM_IRQ_H

#define IRQ_STACK_SIZE			THREAD_SIZE
#define IRQ_STACK_START_SP		THREAD_START_SP

#ifndef __ASSEMBLER__

#include <linux/percpu.h>

#include <asm-generic/irq.h>
#include <asm/thread_info.h>

struct pt_regs;

DECLARE_PER_CPU(unsigned long [IRQ_STACK_SIZE/sizeof(long)], irq_stack);

/*
 * The highest address on the stack, and the first to be used. Used to
 * find the dummy-stack frame put down by el?_irq() in entry.S, which
 * is structured as follows:
 *
 *       ------------
 *       |          |  <- irq_stack_ptr
 *   top ------------
 *       |   x19    | <- irq_stack_ptr - 0x08
 *       ------------
 *       |   x29    | <- irq_stack_ptr - 0x10
 *       ------------
 *
 * where x19 holds a copy of the task stack pointer where the struct pt_regs
 * from kernel_entry can be found.
 *
 */
#define IRQ_STACK_PTR(cpu) ((unsigned long)per_cpu(irq_stack, cpu) + IRQ_STACK_START_SP)

/*
 * The offset from irq_stack_ptr where entry.S will store the original
 * stack pointer. Used by unwind_frame() and dump_backtrace().
 */
#define IRQ_STACK_TO_TASK_STACK(ptr) (*((unsigned long *)((ptr) - 0x08)))

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
