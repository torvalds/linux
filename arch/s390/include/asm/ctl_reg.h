/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_CTL_REG_H
#define __ASM_CTL_REG_H

#include <linux/const.h>

#define CR2_GUARDED_STORAGE		_BITUL(63 - 59)

#define CR14_CHANNEL_REPORT_SUBMASK	_BITUL(63 - 35)
#define CR14_RECOVERY_SUBMASK		_BITUL(63 - 36)
#define CR14_DEGRADATION_SUBMASK	_BITUL(63 - 37)
#define CR14_EXTERNAL_DAMAGE_SUBMASK	_BITUL(63 - 38)
#define CR14_WARNING_SUBMASK		_BITUL(63 - 39)

#ifndef __ASSEMBLY__

#include <linux/bug.h>

#define __ctl_load(array, low, high) do {				\
	typedef struct { char _[sizeof(array)]; } addrtype;		\
									\
	BUILD_BUG_ON(sizeof(addrtype) != (high - low + 1) * sizeof(long));\
	asm volatile(							\
		"	lctlg	%1,%2,%0\n"				\
		:							\
		: "Q" (*(addrtype *)(&array)), "i" (low), "i" (high)	\
		: "memory");						\
} while (0)

#define __ctl_store(array, low, high) do {				\
	typedef struct { char _[sizeof(array)]; } addrtype;		\
									\
	BUILD_BUG_ON(sizeof(addrtype) != (high - low + 1) * sizeof(long));\
	asm volatile(							\
		"	stctg	%1,%2,%0\n"				\
		: "=Q" (*(addrtype *)(&array))				\
		: "i" (low), "i" (high));				\
} while (0)

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

union ctlreg0 {
	unsigned long val;
	struct {
		unsigned long	   : 8;
		unsigned long tcx  : 1;	/* Transactional-Execution control */
		unsigned long pifo : 1;	/* Transactional-Execution Program-
					   Interruption-Filtering Override */
		unsigned long	   : 22;
		unsigned long	   : 3;
		unsigned long lap  : 1; /* Low-address-protection control */
		unsigned long	   : 4;
		unsigned long edat : 1; /* Enhanced-DAT-enablement control */
		unsigned long	   : 2;
		unsigned long iep  : 1; /* Instruction-Execution-Protection */
		unsigned long	   : 1;
		unsigned long afp  : 1; /* AFP-register control */
		unsigned long vx   : 1; /* Vector enablement control */
		unsigned long	   : 7;
		unsigned long sssm : 1; /* Service signal subclass mask */
		unsigned long	   : 9;
	};
};

union ctlreg2 {
	unsigned long val;
	struct {
		unsigned long	    : 33;
		unsigned long ducto : 25;
		unsigned long	    : 1;
		unsigned long gse   : 1;
		unsigned long	    : 1;
		unsigned long tds   : 1;
		unsigned long tdc   : 2;
	};
};

#ifdef CONFIG_SMP
# define ctl_set_bit(cr, bit) smp_ctl_set_bit(cr, bit)
# define ctl_clear_bit(cr, bit) smp_ctl_clear_bit(cr, bit)
#else
# define ctl_set_bit(cr, bit) __ctl_set_bit(cr, bit)
# define ctl_clear_bit(cr, bit) __ctl_clear_bit(cr, bit)
#endif

#endif /* __ASSEMBLY__ */
#endif /* __ASM_CTL_REG_H */
