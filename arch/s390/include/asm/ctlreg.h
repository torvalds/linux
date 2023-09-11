/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright IBM Corp. 1999, 2009
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#ifndef __ASM_S390_CTLREG_H
#define __ASM_S390_CTLREG_H

#include <linux/bits.h>

#define CR0_TRANSACTIONAL_EXECUTION_BIT		(63 - 8)
#define CR0_CLOCK_COMPARATOR_SIGN_BIT		(63 - 10)
#define CR0_CRYPTOGRAPHY_COUNTER_BIT		(63 - 13)
#define CR0_PAI_EXTENSION_BIT			(63 - 14)
#define CR0_CPUMF_EXTRACTION_AUTH_BIT		(63 - 15)
#define CR0_WARNING_TRACK_BIT			(63 - 30)
#define CR0_LOW_ADDRESS_PROTECTION_BIT		(63 - 35)
#define CR0_FETCH_PROTECTION_OVERRIDE_BIT	(63 - 38)
#define CR0_STORAGE_PROTECTION_OVERRIDE_BIT	(63 - 39)
#define CR0_EDAT_BIT				(63 - 40)
#define CR0_INSTRUCTION_EXEC_PROTECTION_BIT	(63 - 43)
#define CR0_VECTOR_BIT				(63 - 46)
#define CR0_MALFUNCTION_ALERT_SUBMASK_BIT	(63 - 48)
#define CR0_EMERGENCY_SIGNAL_SUBMASK_BIT	(63 - 49)
#define CR0_EXTERNAL_CALL_SUBMASK_BIT		(63 - 50)
#define CR0_CLOCK_COMPARATOR_SUBMASK_BIT	(63 - 52)
#define CR0_CPU_TIMER_SUBMASK_BIT		(63 - 53)
#define CR0_SERVICE_SIGNAL_SUBMASK_BIT		(63 - 54)
#define CR0_UNUSED_56_BIT			(63 - 56)
#define CR0_INTERRUPT_KEY_SUBMASK_BIT		(63 - 57)
#define CR0_MEASUREMENT_ALERT_SUBMASK_BIT	(63 - 58)
#define CR0_ETR_SUBMASK_BIT			(63 - 59)
#define CR0_IUCV_BIT				(63 - 62)

#define CR0_TRANSACTIONAL_EXECUTION		BIT(CR0_TRANSACTIONAL_EXECUTION_BIT)
#define CR0_CLOCK_COMPARATOR_SIGN		BIT(CR0_CLOCK_COMPARATOR_SIGN_BIT)
#define CR0_CRYPTOGRAPHY_COUNTER		BIT(CR0_CRYPTOGRAPHY_COUNTER_BIT)
#define CR0_PAI_EXTENSION			BIT(CR0_PAI_EXTENSION_BIT)
#define CR0_CPUMF_EXTRACTION_AUTH		BIT(CR0_CPUMF_EXTRACTION_AUTH_BIT)
#define CR0_WARNING_TRACK			BIT(CR0_WARNING_TRACK_BIT)
#define CR0_LOW_ADDRESS_PROTECTION		BIT(CR0_LOW_ADDRESS_PROTECTION_BIT)
#define CR0_FETCH_PROTECTION_OVERRIDE		BIT(CR0_FETCH_PROTECTION_OVERRIDE_BIT)
#define CR0_STORAGE_PROTECTION_OVERRIDE		BIT(CR0_STORAGE_PROTECTION_OVERRIDE_BIT)
#define CR0_EDAT				BIT(CR0_EDAT_BIT)
#define CR0_INSTRUCTION_EXEC_PROTECTION		BIT(CR0_INSTRUCTION_EXEC_PROTECTION_BIT)
#define CR0_VECTOR				BIT(CR0_VECTOR_BIT)
#define CR0_MALFUNCTION_ALERT_SUBMASK		BIT(CR0_MALFUNCTION_ALERT_SUBMASK_BIT)
#define CR0_EMERGENCY_SIGNAL_SUBMASK		BIT(CR0_EMERGENCY_SIGNAL_SUBMASK_BIT)
#define CR0_EXTERNAL_CALL_SUBMASK		BIT(CR0_EXTERNAL_CALL_SUBMASK_BIT)
#define CR0_CLOCK_COMPARATOR_SUBMASK		BIT(CR0_CLOCK_COMPARATOR_SUBMASK_BIT)
#define CR0_CPU_TIMER_SUBMASK			BIT(CR0_CPU_TIMER_SUBMASK_BIT)
#define CR0_SERVICE_SIGNAL_SUBMASK		BIT(CR0_SERVICE_SIGNAL_SUBMASK_BIT)
#define CR0_UNUSED_56				BIT(CR0_UNUSED_56_BIT)
#define CR0_INTERRUPT_KEY_SUBMASK		BIT(CR0_INTERRUPT_KEY_SUBMASK_BIT)
#define CR0_MEASUREMENT_ALERT_SUBMASK		BIT(CR0_MEASUREMENT_ALERT_SUBMASK_BIT)
#define CR0_ETR_SUBMASK				BIT(CR0_ETR_SUBMASK_BIT)
#define CR0_IUCV				BIT(CR0_IUCV_BIT)

#define CR2_MIO_ADDRESSING_BIT			(63 - 58)
#define CR2_GUARDED_STORAGE_BIT			(63 - 59)

#define CR2_MIO_ADDRESSING			BIT(CR2_MIO_ADDRESSING_BIT)
#define CR2_GUARDED_STORAGE			BIT(CR2_GUARDED_STORAGE_BIT)

#define CR14_UNUSED_32_BIT			(63 - 32)
#define CR14_UNUSED_33_BIT			(63 - 33)
#define CR14_CHANNEL_REPORT_SUBMASK_BIT		(63 - 35)
#define CR14_RECOVERY_SUBMASK_BIT		(63 - 36)
#define CR14_DEGRADATION_SUBMASK_BIT		(63 - 37)
#define CR14_EXTERNAL_DAMAGE_SUBMASK_BIT	(63 - 38)
#define CR14_WARNING_SUBMASK_BIT		(63 - 39)

#define CR14_UNUSED_32				BIT(CR14_UNUSED_32_BIT)
#define CR14_UNUSED_33				BIT(CR14_UNUSED_33_BIT)
#define CR14_CHANNEL_REPORT_SUBMASK		BIT(CR14_CHANNEL_REPORT_SUBMASK_BIT)
#define CR14_RECOVERY_SUBMASK			BIT(CR14_RECOVERY_SUBMASK_BIT)
#define CR14_DEGRADATION_SUBMASK		BIT(CR14_DEGRADATION_SUBMASK_BIT)
#define CR14_EXTERNAL_DAMAGE_SUBMASK		BIT(CR14_EXTERNAL_DAMAGE_SUBMASK_BIT)
#define CR14_WARNING_SUBMASK			BIT(CR14_WARNING_SUBMASK_BIT)

#ifndef __ASSEMBLY__

#include <linux/bug.h>

struct ctlreg {
	unsigned long val;
};

#define __local_ctl_load(low, high, array) do {				\
	struct addrtype {						\
		char _[sizeof(array)];					\
	};								\
	int _high = high;						\
	int _low = low;							\
	int _esize;							\
									\
	_esize = (_high - _low + 1) * sizeof(struct ctlreg);		\
	BUILD_BUG_ON(sizeof(struct addrtype) != _esize);		\
	typecheck(struct ctlreg, array[0]);				\
	asm volatile(							\
		"	lctlg	%[_low],%[_high],%[_arr]\n"		\
		:							\
		: [_arr] "Q" (*(struct addrtype *)(&array)),		\
		  [_low] "i" (low), [_high] "i" (high)			\
		: "memory");						\
} while (0)

#define __local_ctl_store(low, high, array) do {			\
	struct addrtype {						\
		char _[sizeof(array)];					\
	};								\
	int _high = high;						\
	int _low = low;							\
	int _esize;							\
									\
	_esize = (_high - _low + 1) * sizeof(struct ctlreg);		\
	BUILD_BUG_ON(sizeof(struct addrtype) != _esize);		\
	typecheck(struct ctlreg, array[0]);				\
	asm volatile(							\
		"	stctg	%[_low],%[_high],%[_arr]\n"		\
		: [_arr] "=Q" (*(struct addrtype *)(&array))		\
		: [_low] "i" (low), [_high] "i" (high));		\
} while (0)

static __always_inline void local_ctl_load(unsigned int cr, struct ctlreg *reg)
{
	asm volatile(
		"	lctlg	%[cr],%[cr],%[reg]\n"
		:
		: [reg] "Q" (*reg), [cr] "i" (cr)
		: "memory");
}

static __always_inline void local_ctl_store(unsigned int cr, struct ctlreg *reg)
{
	asm volatile(
		"	stctg	%[cr],%[cr],%[reg]\n"
		: [reg] "=Q" (*reg)
		: [cr] "i" (cr));
}

static __always_inline void local_ctl_set_bit(unsigned int cr, unsigned int bit)
{
	struct ctlreg reg;

	local_ctl_store(cr, &reg);
	reg.val |= 1UL << bit;
	local_ctl_load(cr, &reg);
}

static __always_inline void local_ctl_clear_bit(unsigned int cr, unsigned int bit)
{
	struct ctlreg reg;

	local_ctl_store(cr, &reg);
	reg.val &= ~(1UL << bit);
	local_ctl_load(cr, &reg);
}

struct lowcore;

void system_ctlreg_lock(void);
void system_ctlreg_unlock(void);
void system_ctlreg_init_save_area(struct lowcore *lc);
void system_ctlreg_modify(unsigned int cr, unsigned long data, int request);

enum {
	CTLREG_SET_BIT,
	CTLREG_CLEAR_BIT,
	CTLREG_LOAD,
};

static inline void system_ctl_set_bit(unsigned int cr, unsigned int bit)
{
	system_ctlreg_modify(cr, bit, CTLREG_SET_BIT);
}

static inline void system_ctl_clear_bit(unsigned int cr, unsigned int bit)
{
	system_ctlreg_modify(cr, bit, CTLREG_CLEAR_BIT);
}

static inline void system_ctl_load(unsigned int cr, struct ctlreg *reg)
{
	system_ctlreg_modify(cr, reg->val, CTLREG_LOAD);
}

union ctlreg0 {
	unsigned long val;
	struct ctlreg reg;
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
	struct ctlreg reg;
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
	struct ctlreg reg;
	struct {
		unsigned long	    : 33;
		unsigned long pasteo: 25;
		unsigned long	    : 6;
	};
};

union ctlreg15 {
	unsigned long val;
	struct ctlreg reg;
	struct {
		unsigned long lsea  : 61;
		unsigned long	    : 3;
	};
};

#endif /* __ASSEMBLY__ */
#endif /* __ASM_S390_CTLREG_H */
