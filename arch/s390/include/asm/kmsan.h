/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_KMSAN_H
#define _ASM_S390_KMSAN_H

#include <asm/lowcore.h>
#include <asm/page.h>
#include <linux/kmsan.h>
#include <linux/mmzone.h>
#include <linux/stddef.h>

#ifndef MODULE

static inline bool is_lowcore_addr(void *addr)
{
	return addr >= (void *)get_lowcore() &&
	       addr < (void *)(get_lowcore() + 1);
}

static inline void *arch_kmsan_get_meta_or_null(void *addr, bool is_origin)
{
	if (is_lowcore_addr(addr)) {
		/*
		 * Different lowcores accessed via S390_lowcore are described
		 * by the same struct page. Resolve the prefix manually in
		 * order to get a distinct struct page.
		 */
		addr += (void *)lowcore_ptr[raw_smp_processor_id()] -
			(void *)get_lowcore();
		if (KMSAN_WARN_ON(is_lowcore_addr(addr)))
			return NULL;
		return kmsan_get_metadata(addr, is_origin);
	}
	return NULL;
}

static inline bool kmsan_virt_addr_valid(void *addr)
{
	bool ret;

	/*
	 * pfn_valid() relies on RCU, and may call into the scheduler on exiting
	 * the critical section. However, this would result in recursion with
	 * KMSAN. Therefore, disable preemption here, and re-enable preemption
	 * below while suppressing reschedules to avoid recursion.
	 *
	 * Note, this sacrifices occasionally breaking scheduling guarantees.
	 * Although, a kernel compiled with KMSAN has already given up on any
	 * performance guarantees due to being heavily instrumented.
	 */
	preempt_disable();
	ret = virt_addr_valid(addr);
	preempt_enable_no_resched();

	return ret;
}

#endif /* !MODULE */

#endif /* _ASM_S390_KMSAN_H */
