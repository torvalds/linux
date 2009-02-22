#ifndef __SPARC_KERNEL_H
#define __SPARC_KERNEL_H

#include <linux/interrupt.h>

/* cpu.c */
extern const char *sparc_cpu_type;
extern const char *sparc_pmu_type;
extern const char *sparc_fpu_type;

extern unsigned int fsr_storage;

#ifdef CONFIG_SPARC32
/* cpu.c */
extern void cpu_probe(void);

/* traps_32.c */
extern void handle_hw_divzero(struct pt_regs *regs, unsigned long pc,
                              unsigned long npc, unsigned long psr);
/* muldiv.c */
extern int do_user_muldiv (struct pt_regs *, unsigned long);

/* irq_32.c */
extern struct irqaction static_irqaction[];
extern int static_irq_count;
extern spinlock_t irq_action_lock;

extern void unexpected_irq(int irq, void *dev_id, struct pt_regs * regs);

#else /* CONFIG_SPARC32 */
#endif /* CONFIG_SPARC32 */
#endif /* !(__SPARC_KERNEL_H) */
