/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2012 Calxeda, Inc.
 */
#ifndef _ASM_ARM_PERCPU_H_
#define _ASM_ARM_PERCPU_H_

#include <asm/insn.h>

register unsigned long current_stack_pointer asm ("sp");

/*
 * Same as asm-generic/percpu.h, except that we store the per cpu offset
 * in the TPIDRPRW. TPIDRPRW only exists on V6K and V7
 */
#ifdef CONFIG_SMP
static inline void set_my_cpu_offset(unsigned long off)
{
	extern unsigned int smp_on_up;

	if (IS_ENABLED(CONFIG_CPU_V6) && !smp_on_up)
		return;

	/* Set TPIDRPRW */
	asm volatile("mcr p15, 0, %0, c13, c0, 4" : : "r" (off) : "memory");
}

static __always_inline unsigned long __my_cpu_offset(void)
{
	unsigned long off;

	/*
	 * Read TPIDRPRW.
	 * We want to allow caching the value, so avoid using volatile and
	 * instead use a fake stack read to hazard against barrier().
	 */
	asm("0:	mrc p15, 0, %0, c13, c0, 4			\n\t"
#ifdef CONFIG_CPU_V6
	    "1:							\n\t"
	    "	.subsection 1					\n\t"
#if defined(CONFIG_ARM_HAS_GROUP_RELOCS) && \
    !(defined(MODULE) && defined(CONFIG_ARM_MODULE_PLTS))
	    "2: " LOAD_SYM_ARMV6(%0, __per_cpu_offset) "	\n\t"
	    "	b	1b					\n\t"
#else
	    "2: ldr	%0, 3f					\n\t"
	    "	ldr	%0, [%0]				\n\t"
	    "	b	1b					\n\t"
	    "3:	.long	__per_cpu_offset			\n\t"
#endif
	    "	.previous					\n\t"
	    "	.pushsection \".alt.smp.init\", \"a\"		\n\t"
	    "	.long	0b - .					\n\t"
	    "	b	. + (2b - 0b)				\n\t"
	    "	.popsection					\n\t"
#endif
	     : "=r" (off)
	     : "Q" (*(const unsigned long *)current_stack_pointer));

	return off;
}
#define __my_cpu_offset __my_cpu_offset()
#else
#define set_my_cpu_offset(x)	do {} while(0)

#endif /* CONFIG_SMP */

#include <asm-generic/percpu.h>

#endif /* _ASM_ARM_PERCPU_H_ */
