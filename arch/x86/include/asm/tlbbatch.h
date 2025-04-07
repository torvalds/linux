/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_X86_TLBBATCH_H
#define _ARCH_X86_TLBBATCH_H

#include <linux/cpumask.h>

struct arch_tlbflush_unmap_batch {
	/*
	 * Each bit set is a CPU that potentially has a TLB entry for one of
	 * the PFNs being flushed..
	 */
	struct cpumask cpumask;
	/*
	 * Set if pages were unmapped from any MM, even one that does not
	 * have active CPUs in its cpumask.
	 */
	bool unmapped_pages;
};

#endif /* _ARCH_X86_TLBBATCH_H */
