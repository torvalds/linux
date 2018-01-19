/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __UM_IRQFLAGS_H
#define __UM_IRQFLAGS_H

extern int get_signals(void);
extern int set_signals(int enable);
extern void block_signals(void);
extern void unblock_signals(void);

#define arch_local_save_flags arch_local_save_flags
static inline unsigned long arch_local_save_flags(void)
{
	return get_signals();
}

#define arch_local_irq_restore arch_local_irq_restore
static inline void arch_local_irq_restore(unsigned long flags)
{
	set_signals(flags);
}

#define arch_local_irq_enable arch_local_irq_enable
static inline void arch_local_irq_enable(void)
{
	unblock_signals();
}

#define arch_local_irq_disable arch_local_irq_disable
static inline void arch_local_irq_disable(void)
{
	block_signals();
}

#define ARCH_IRQ_DISABLED	0
#define ARCh_IRQ_ENABLED	(SIGIO|SIGVTALRM)

#include <asm-generic/irqflags.h>

#endif
