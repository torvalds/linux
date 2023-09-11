/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_S390_CTLREG_H
#define __ASM_S390_CTLREG_H

#include <linux/bits.h>

#define CR0_CLOCK_COMPARATOR_SIGN	BIT(63 - 10)
#define CR0_LOW_ADDRESS_PROTECTION	BIT(63 - 35)
#define CR0_FETCH_PROTECTION_OVERRIDE	BIT(63 - 38)
#define CR0_STORAGE_PROTECTION_OVERRIDE	BIT(63 - 39)
#define CR0_EMERGENCY_SIGNAL_SUBMASK	BIT(63 - 49)
#define CR0_EXTERNAL_CALL_SUBMASK	BIT(63 - 50)
#define CR0_CLOCK_COMPARATOR_SUBMASK	BIT(63 - 52)
#define CR0_CPU_TIMER_SUBMASK		BIT(63 - 53)
#define CR0_SERVICE_SIGNAL_SUBMASK	BIT(63 - 54)
#define CR0_UNUSED_56			BIT(63 - 56)
#define CR0_INTERRUPT_KEY_SUBMASK	BIT(63 - 57)
#define CR0_MEASUREMENT_ALERT_SUBMASK	BIT(63 - 58)

#define CR14_UNUSED_32			BIT(63 - 32)
#define CR14_UNUSED_33			BIT(63 - 33)
#define CR14_CHANNEL_REPORT_SUBMASK	BIT(63 - 35)
#define CR14_RECOVERY_SUBMASK		BIT(63 - 36)
#define CR14_DEGRADATION_SUBMASK	BIT(63 - 37)
#define CR14_EXTERNAL_DAMAGE_SUBMASK	BIT(63 - 38)
#define CR14_WARNING_SUBMASK		BIT(63 - 39)

#ifndef __ASSEMBLY__

#include <linux/bug.h>

#define __local_ctl_load(array, low, high) do {				\
	struct addrtype {						\
		char _[sizeof(array)];					\
	};								\
	int _high = high;						\
	int _low = low;							\
	int _esize;							\
									\
	_esize = (_high - _low + 1) * sizeof(unsigned long);		\
	BUILD_BUG_ON(sizeof(struct addrtype) != _esize);		\
	asm volatile(							\
		"	lctlg	%[_low],%[_high],%[_arr]\n"		\
		:							\
		: [_arr] "Q" (*(struct addrtype *)(&array)),		\
		  [_low] "i" (low), [_high] "i" (high)			\
		: "memory");						\
} while (0)

#define __local_ctl_store(array, low, high) do {			\
	struct addrtype {						\
		char _[sizeof(array)];					\
	};								\
	int _high = high;						\
	int _low = low;							\
	int _esize;							\
									\
	_esize = (_high - _low + 1) * sizeof(unsigned long);		\
	BUILD_BUG_ON(sizeof(struct addrtype) != _esize);		\
	asm volatile(							\
		"	stctg	%[_low],%[_high],%[_arr]\n"		\
		: [_arr] "=Q" (*(struct addrtype *)(&array))		\
		: [_low] "i" (low), [_high] "i" (high));		\
} while (0)

static __always_inline void local_ctl_load(unsigned int cr, unsigned long *reg)
{
	asm volatile(
		"	lctlg	%[cr],%[cr],%[reg]\n"
		:
		: [reg] "Q" (*reg), [cr] "i" (cr)
		: "memory");
}

static __always_inline void local_ctl_store(unsigned int cr, unsigned long *reg)
{
	asm volatile(
		"	stctg	%[cr],%[cr],%[reg]\n"
		: [reg] "=Q" (*reg)
		: [cr] "i" (cr));
}

static __always_inline void local_ctl_set_bit(unsigned int cr, unsigned int bit)
{
	unsigned long reg;

	local_ctl_store(cr, &reg);
	reg |= 1UL << bit;
	local_ctl_load(cr, &reg);
}

static __always_inline void local_ctl_clear_bit(unsigned int cr, unsigned int bit)
{
	unsigned long reg;

	local_ctl_store(cr, &reg);
	reg &= ~(1UL << bit);
	local_ctl_load(cr, &reg);
}

void system_ctlreg_lock(void);
void system_ctlreg_unlock(void);
void system_ctl_set_clear_bit(unsigned int cr, unsigned int bit, bool set);

static inline void system_ctl_set_bit(unsigned int cr, unsigned int bit)
{
	system_ctl_set_clear_bit(cr, bit, true);
}

static inline void system_ctl_clear_bit(unsigned int cr, unsigned int bit)
{
	system_ctl_set_clear_bit(cr, bit, false);
}

union ctlreg0 {
	unsigned long val;
	struct {
		unsigned long	   : 8;
		unsigned long tcx  : 1;	/* Transactional-Execution control */
		unsigned long pifo : 1;	/* Transactional-Execution Program-
					   Interruption-Filtering Override */
		unsigned long	   : 3;
		unsigned long ccc  : 1; /* Cryptography counter control */
		unsigned long pec  : 1; /* PAI extension control */
		unsigned long	   : 17;
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

union ctlreg5 {
	unsigned long val;
	struct {
		unsigned long	    : 33;
		unsigned long pasteo: 25;
		unsigned long	    : 6;
	};
};

union ctlreg15 {
	unsigned long val;
	struct {
		unsigned long lsea  : 61;
		unsigned long	    : 3;
	};
};

#endif /* __ASSEMBLY__ */
#endif /* __ASM_S390_CTLREG_H */
