/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ARCH_ARM64_TLBBATCH_H
#define _ARCH_ARM64_TLBBATCH_H

struct arch_tlbflush_unmap_batch {
	/*
	 * For arm64, HW can do tlb shootdown, so we don't
	 * need to record cpumask for sending IPI
	 */
};

#endif /* _ARCH_ARM64_TLBBATCH_H */
