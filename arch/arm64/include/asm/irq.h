#ifndef __ASM_IRQ_H
#define __ASM_IRQ_H

#define IRQ_STACK_SIZE			THREAD_SIZE

#ifndef __ASSEMBLER__

#include <linux/percpu.h>
#include <linux/sched/task_stack.h>

#include <asm-generic/irq.h>
#include <asm/thread_info.h>

struct pt_regs;

DECLARE_PER_CPU(unsigned long [IRQ_STACK_SIZE/sizeof(long)], irq_stack);

extern void set_handle_irq(void (*handle_irq)(struct pt_regs *));

static inline int nr_legacy_irqs(void)
{
	return 0;
}

static inline bool on_irq_stack(unsigned long sp)
{
	unsigned long low = (unsigned long)raw_cpu_ptr(irq_stack);
	unsigned long high = low + IRQ_STACK_SIZE;

	return (low <= sp && sp < high);
}

static inline bool on_task_stack(struct task_struct *tsk, unsigned long sp)
{
	unsigned long low = (unsigned long)task_stack_page(tsk);
	unsigned long high = low + THREAD_SIZE;

	return (low <= sp && sp < high);
}

#endif /* !__ASSEMBLER__ */
#endif
