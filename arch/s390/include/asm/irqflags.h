/*
 *    Copyright IBM Corp. 2006,2010
 *    Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_IRQFLAGS_H
#define __ASM_IRQFLAGS_H

#include <linux/types.h>

/* store then or system mask. */
#define __raw_local_irq_stosm(__or)					\
({									\
	unsigned long __mask;						\
	asm volatile(							\
		"	stosm	%0,%1"					\
		: "=Q" (__mask) : "i" (__or) : "memory");		\
	__mask;								\
})

/* store then and system mask. */
#define __raw_local_irq_stnsm(__and)					\
({									\
	unsigned long __mask;						\
	asm volatile(							\
		"	stnsm	%0,%1"					\
		: "=Q" (__mask) : "i" (__and) : "memory");		\
	__mask;								\
})

/* set system mask. */
#define __raw_local_irq_ssm(__mask)					\
({									\
	asm volatile("ssm   %0" : : "Q" (__mask) : "memory");		\
})

/* interrupt control.. */
static inline unsigned long raw_local_irq_enable(void)
{
	return __raw_local_irq_stosm(0x03);
}

static inline unsigned long raw_local_irq_disable(void)
{
	return __raw_local_irq_stnsm(0xfc);
}

#define raw_local_save_flags(x)						\
do {									\
	typecheck(unsigned long, x);					\
	(x) = __raw_local_irq_stosm(0x00);				\
} while (0)

static inline void raw_local_irq_restore(unsigned long flags)
{
	__raw_local_irq_ssm(flags);
}

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & (3UL << (BITS_PER_LONG - 8)));
}

/* For spinlocks etc */
#define raw_local_irq_save(x)	((x) = raw_local_irq_disable())

#endif /* __ASM_IRQFLAGS_H */
