/*
 * include/asm/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() functions from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#include <asm/pil.h>

#ifndef __ASSEMBLY__

static inline unsigned long __raw_local_save_flags(void)
{
	unsigned long flags;

	__asm__ __volatile__(
		"rdpr	%%pil, %0"
		: "=r" (flags)
	);

	return flags;
}

#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

static inline void raw_local_irq_restore(unsigned long flags)
{
	__asm__ __volatile__(
		"wrpr	%0, %%pil"
		: /* no output */
		: "r" (flags)
		: "memory"
	);
}

static inline void raw_local_irq_disable(void)
{
	__asm__ __volatile__(
		"wrpr	%0, %%pil"
		: /* no outputs */
		: "i" (PIL_NORMAL_MAX)
		: "memory"
	);
}

static inline void raw_local_irq_enable(void)
{
	__asm__ __volatile__(
		"wrpr	0, %%pil"
		: /* no outputs */
		: /* no inputs */
		: "memory"
	);
}

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return (flags > 0);
}

static inline int raw_irqs_disabled(void)
{
	unsigned long flags = __raw_local_save_flags();

	return raw_irqs_disabled_flags(flags);
}

/*
 * For spinlocks, etc:
 */
static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long flags, tmp;

	/* Disable interrupts to PIL_NORMAL_MAX unless we already
	 * are using PIL_NMI, in which case PIL_NMI is retained.
	 *
	 * The only values we ever program into the %pil are 0,
	 * PIL_NORMAL_MAX and PIL_NMI.
	 *
	 * Since PIL_NMI is the largest %pil value and all bits are
	 * set in it (0xf), it doesn't matter what PIL_NORMAL_MAX
	 * actually is.
	 */
	__asm__ __volatile__(
		"rdpr	%%pil, %0\n\t"
		"or	%0, %2, %1\n\t"
		"wrpr	%1, 0x0, %%pil"
		: "=r" (flags), "=r" (tmp)
		: "i" (PIL_NORMAL_MAX)
		: "memory"
	);

	return flags;
}

#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

#endif /* (__ASSEMBLY__) */

#endif /* !(_ASM_IRQFLAGS_H) */
