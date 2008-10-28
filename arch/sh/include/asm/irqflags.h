#ifndef __ASM_SH_IRQFLAGS_H
#define __ASM_SH_IRQFLAGS_H

#ifdef CONFIG_SUPERH32
#include "irqflags_32.h"
#else
#include "irqflags_64.h"
#endif

#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return (flags != 0);
}

static inline int raw_irqs_disabled(void)
{
	unsigned long flags = __raw_local_save_flags();

	return raw_irqs_disabled_flags(flags);
}

#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

static inline void raw_local_irq_restore(unsigned long flags)
{
	if ((flags & 0xf0) != 0xf0)
		raw_local_irq_enable();
}

#endif /* __ASM_SH_IRQFLAGS_H */
