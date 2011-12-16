#ifndef __UM_IRQFLAGS_H
#define __UM_IRQFLAGS_H

extern int get_signals(void);
extern int set_signals(int enable);
extern void block_signals(void);
extern void unblock_signals(void);

static inline unsigned long arch_local_save_flags(void)
{
	return get_signals();
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	set_signals(flags);
}

static inline void arch_local_irq_enable(void)
{
	unblock_signals();
}

static inline void arch_local_irq_disable(void)
{
	block_signals();
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;
	flags = arch_local_save_flags();
	arch_local_irq_disable();
	return flags;
}

static inline bool arch_irqs_disabled(void)
{
	return arch_local_save_flags() == 0;
}

#endif
