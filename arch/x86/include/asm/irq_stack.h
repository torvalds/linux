/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IRQ_STACK_H
#define _ASM_X86_IRQ_STACK_H

#include <linux/ptrace.h>

#include <asm/processor.h>

#ifdef CONFIG_X86_64
static __always_inline bool irqstack_active(void)
{
	return __this_cpu_read(irq_count) != -1;
}

void asm_call_on_stack(void *sp, void (*func)(void), void *arg);
void asm_call_sysvec_on_stack(void *sp, void (*func)(struct pt_regs *regs),
			      struct pt_regs *regs);
void asm_call_irq_on_stack(void *sp, void (*func)(struct irq_desc *desc),
			   struct irq_desc *desc);

static __always_inline void __run_on_irqstack(void (*func)(void))
{
	void *tos = __this_cpu_read(hardirq_stack_ptr);

	__this_cpu_add(irq_count, 1);
	asm_call_on_stack(tos - 8, func, NULL);
	__this_cpu_sub(irq_count, 1);
}

static __always_inline void
__run_sysvec_on_irqstack(void (*func)(struct pt_regs *regs),
			 struct pt_regs *regs)
{
	void *tos = __this_cpu_read(hardirq_stack_ptr);

	__this_cpu_add(irq_count, 1);
	asm_call_sysvec_on_stack(tos - 8, func, regs);
	__this_cpu_sub(irq_count, 1);
}

static __always_inline void
__run_irq_on_irqstack(void (*func)(struct irq_desc *desc),
		      struct irq_desc *desc)
{
	void *tos = __this_cpu_read(hardirq_stack_ptr);

	__this_cpu_add(irq_count, 1);
	asm_call_irq_on_stack(tos - 8, func, desc);
	__this_cpu_sub(irq_count, 1);
}

#else /* CONFIG_X86_64 */
static inline bool irqstack_active(void) { return false; }
static inline void __run_on_irqstack(void (*func)(void)) { }
static inline void __run_sysvec_on_irqstack(void (*func)(struct pt_regs *regs),
					    struct pt_regs *regs) { }
static inline void __run_irq_on_irqstack(void (*func)(struct irq_desc *desc),
					 struct irq_desc *desc) { }
#endif /* !CONFIG_X86_64 */

static __always_inline bool irq_needs_irq_stack(struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_X86_32))
		return false;
	if (!regs)
		return !irqstack_active();
	return !user_mode(regs) && !irqstack_active();
}


static __always_inline void run_on_irqstack_cond(void (*func)(void),
						 struct pt_regs *regs)
{
	lockdep_assert_irqs_disabled();

	if (irq_needs_irq_stack(regs))
		__run_on_irqstack(func);
	else
		func();
}

static __always_inline void
run_sysvec_on_irqstack_cond(void (*func)(struct pt_regs *regs),
			    struct pt_regs *regs)
{
	lockdep_assert_irqs_disabled();

	if (irq_needs_irq_stack(regs))
		__run_sysvec_on_irqstack(func, regs);
	else
		func(regs);
}

static __always_inline void
run_irq_on_irqstack_cond(void (*func)(struct irq_desc *desc), struct irq_desc *desc,
			 struct pt_regs *regs)
{
	lockdep_assert_irqs_disabled();

	if (irq_needs_irq_stack(regs))
		__run_irq_on_irqstack(func, desc);
	else
		func(desc);
}

#endif
