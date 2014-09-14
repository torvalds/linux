#ifndef _ASM_X86_IRQ_H
#define _ASM_X86_IRQ_H
/*
 *	(C) 1992, 1993 Linus Torvalds, (C) 1997 Ingo Molnar
 *
 *	IRQ/IPI changes taken from work by Thomas Radke
 *	<tomsoft@informatik.tu-chemnitz.de>
 */

#include <asm/apicdef.h>
#include <asm/irq_vectors.h>

static inline int irq_canonicalize(int irq)
{
	return ((irq == 2) ? 9 : irq);
}

#ifdef CONFIG_X86_32
extern void irq_ctx_init(int cpu);
#else
# define irq_ctx_init(cpu) do { } while (0)
#endif

#define __ARCH_HAS_DO_SOFTIRQ

#ifdef CONFIG_HOTPLUG_CPU
#include <linux/cpumask.h>
extern int check_irq_vectors_for_cpu_disable(void);
extern void fixup_irqs(void);
extern void irq_force_complete_move(int);
#endif

extern void (*x86_platform_ipi_callback)(void);
extern void native_init_IRQ(void);
extern bool handle_irq(unsigned irq, struct pt_regs *regs);

extern __visible unsigned int do_IRQ(struct pt_regs *regs);

/* Interrupt vector management */
extern DECLARE_BITMAP(used_vectors, NR_VECTORS);
extern int vector_used_by_percpu_irq(unsigned int vector);

extern void init_ISA_irqs(void);

#ifdef CONFIG_X86_LOCAL_APIC
void arch_trigger_all_cpu_backtrace(bool);
#define arch_trigger_all_cpu_backtrace arch_trigger_all_cpu_backtrace
#endif

#endif /* _ASM_X86_IRQ_H */
