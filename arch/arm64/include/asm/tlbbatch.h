/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_ARM64_TLBBATCH_H
#define _ARCH_ARM64_TLBBATCH_H

struct arch_tlbflush_unmap_batch {
	/*
	 * For arm64, HW can do TLB shootdown, so we don't need to record a
	 * cpumask for sending IPIs.
	 */
};

#endif /* _ARCH_ARM64_TLBBATCH_H */
