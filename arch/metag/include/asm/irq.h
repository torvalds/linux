#ifndef __ASM_METAG_IRQ_H
#define __ASM_METAG_IRQ_H

#ifdef CONFIG_4KSTACKS
extern void irq_ctx_init(int cpu);
extern void irq_ctx_exit(int cpu);
# define __ARCH_HAS_DO_SOFTIRQ
#else
static inline void irq_ctx_init(int cpu)
{
}
static inline void irq_ctx_exit(int cpu)
{
}
#endif

void tbi_startup_interrupt(int);
void tbi_shutdown_interrupt(int);

struct pt_regs;

int tbisig_map(unsigned int hw);
extern void do_IRQ(int irq, struct pt_regs *regs);
extern void init_IRQ(void);

#ifdef CONFIG_METAG_SUSPEND_MEM
int traps_save_context(void);
int traps_restore_context(void);
#endif

#include <asm-generic/irq.h>

#ifdef CONFIG_HOTPLUG_CPU
extern void migrate_irqs(void);
#endif

#endif /* __ASM_METAG_IRQ_H */
