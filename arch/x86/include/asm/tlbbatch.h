#ifndef _ARCH_X86_TLBBATCH_H
#define _ARCH_X86_TLBBATCH_H

#include <linux/cpumask.h>

#ifdef CONFIG_SMP
struct arch_tlbflush_unmap_batch {
	/*
	 * Each bit set is a CPU that potentially has a TLB entry for one of
	 * the PFNs being flushed..
	 */
	struct cpumask cpumask;
};
#endif

#endif /* _ARCH_X86_TLBBATCH_H */
