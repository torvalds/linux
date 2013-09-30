/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_CTL_REG_H
#define __ASM_CTL_REG_H

#ifdef CONFIG_64BIT
# define __CTL_LOAD	"lctlg"
# define __CTL_STORE	"stctg"
#else
# define __CTL_LOAD	"lctl"
# define __CTL_STORE	"stctl"
#endif

#define __ctl_load(array, low, high) {					\
	typedef struct { char _[sizeof(array)]; } addrtype;		\
									\
	BUILD_BUG_ON(sizeof(addrtype) != (high - low + 1) * sizeof(long));\
	asm volatile(							\
		__CTL_LOAD " %1,%2,%0\n"				\
		: : "Q" (*(addrtype *)(&array)), "i" (low), "i" (high));\
}

#define __ctl_store(array, low, high) {					\
	typedef struct { char _[sizeof(array)]; } addrtype;		\
									\
	BUILD_BUG_ON(sizeof(addrtype) != (high - low + 1) * sizeof(long));\
	asm volatile(							\
		__CTL_STORE " %1,%2,%0\n"				\
		: "=Q" (*(addrtype *)(&array))				\
		: "i" (low), "i" (high));				\
}

static inline void __ctl_set_bit(unsigned int cr, unsigned int bit)
{
	unsigned long reg;

	__ctl_store(reg, cr, cr);
	reg |= 1UL << bit;
	__ctl_load(reg, cr, cr);
}

static inline void __ctl_clear_bit(unsigned int cr, unsigned int bit)
{
	unsigned long reg;

	__ctl_store(reg, cr, cr);
	reg &= ~(1UL << bit);
	__ctl_load(reg, cr, cr);
}

void smp_ctl_set_bit(int cr, int bit);
void smp_ctl_clear_bit(int cr, int bit);

#ifdef CONFIG_SMP
# define ctl_set_bit(cr, bit) smp_ctl_set_bit(cr, bit)
# define ctl_clear_bit(cr, bit) smp_ctl_clear_bit(cr, bit)
#else
# define ctl_set_bit(cr, bit) __ctl_set_bit(cr, bit)
# define ctl_clear_bit(cr, bit) __ctl_clear_bit(cr, bit)
#endif

#endif /* __ASM_CTL_REG_H */
