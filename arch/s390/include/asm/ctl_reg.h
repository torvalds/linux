/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_CTL_REG_H
#define __ASM_CTL_REG_H

#include <linux/bits.h>

#define CR0_CLOCK_COMPARATOR_SIGN	BIT(63 - 10)
#define CR0_LOW_ADDRESS_PROTECTION	BIT(63 - 35)
#define CR0_EMERGENCY_SIGNAL_SUBMASK	BIT(63 - 49)
#define CR0_EXTERNAL_CALL_SUBMASK	BIT(63 - 50)
#define CR0_CLOCK_COMPARATOR_SUBMASK	BIT(63 - 52)
#define CR0_CPU_TIMER_SUBMASK		BIT(63 - 53)
#define CR0_SERVICE_SIGNAL_SUBMASK	BIT(63 - 54)
#define CR0_UNUSED_56			BIT(63 - 56)
#define CR0_INTERRUPT_KEY_SUBMASK	BIT(63 - 57)
#define CR0_MEASUREMENT_ALERT_SUBMASK	BIT(63 - 58)

#define CR2_GUARDED_STORAGE		BIT(63 - 59)

#define CR14_UNUSED_32			BIT(63 - 32)
#define CR14_UNUSED_33			BIT(63 - 33)
#define CR14_CHANNEL_REPORT_SUBMASK	BIT(63 - 35)
#define CR14_RECOVERY_SUBMASK		BIT(63 - 36)
#define CR14_DEGRADATION_SUBMASK	BIT(63 - 37)
#define CR14_EXTERNAL_DAMAGE_SUBMASK	BIT(63 - 38)
#define CR14_WARNING_SUBMASK		BIT(63 - 39)

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

static __always_inline void __ctl_set_bit(unsigned int cr, unsigned int bit)
{
	unsigned long reg;

	__ctl_store(reg, cr, cr);
	reg |= 1UL << bit;
	__ctl_load(reg, cr, cr);
}

static __always_inline void __ctl_clear_bit(unsigned int cr, unsigned int bit)
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

#define ctl_set_bit(cr, bit) smp_ctl_set_bit(cr, bit)
#define ctl_clear_bit(cr, bit) smp_ctl_clear_bit(cr, bit)

#endif /* __ASSEMBLY__ */
#endif /* __ASM_CTL_REG_H */
