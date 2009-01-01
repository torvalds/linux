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

#ifdef CONFIG_X86_LOCAL_APIC
# define ARCH_HAS_NMI_WATCHDOG
#endif

#ifdef CONFIG_4KSTACKS
  extern void irq_ctx_init(int cpu);
  extern void irq_ctx_exit(int cpu);
# define __ARCH_HAS_DO_SOFTIRQ
#else
# define irq_ctx_init(cpu) do { } while (0)
# define irq_ctx_exit(cpu) do { } while (0)
# ifdef CONFIG_X86_64
#  define __ARCH_HAS_DO_SOFTIRQ
# endif
#endif

#ifdef CONFIG_HOTPLUG_CPU
#include <linux/cpumask.h>
extern void fixup_irqs(void);
#endif

extern unsigned int do_IRQ(struct pt_regs *regs);
extern void init_IRQ(void);
extern void native_init_IRQ(void);

/* Interrupt vector management */
extern DECLARE_BITMAP(used_vectors, NR_VECTORS);
extern int vector_used_by_percpu_irq(unsigned int vector);

#endif /* _ASM_X86_IRQ_H */
