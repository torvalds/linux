/*
 * Copyright(c) 2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
#ifndef __ASM_X86_PMEM_H__
#define __ASM_X86_PMEM_H__

#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/cpufeature.h>
#include <asm/special_insns.h>

#ifdef CONFIG_ARCH_HAS_PMEM_API
/**
 * arch_memcpy_to_pmem - copy data to persistent memory
 * @dst: destination buffer for the copy
 * @src: source buffer for the copy
 * @n: length of the copy in bytes
 *
 * Copy data to persistent memory media via non-temporal stores so that
 * a subsequent arch_wmb_pmem() can flush cpu and memory controller
 * write buffers to guarantee durability.
 */
static inline void arch_memcpy_to_pmem(void __pmem *dst, const void *src,
		size_t n)
{
	int unwritten;

	/*
	 * We are copying between two kernel buffers, if
	 * __copy_from_user_inatomic_nocache() returns an error (page
	 * fault) we would have already reported a general protection fault
	 * before the WARN+BUG.
	 */
	unwritten = __copy_from_user_inatomic_nocache((void __force *) dst,
			(void __user *) src, n);
	if (WARN(unwritten, "%s: fault copying %p <- %p unwritten: %d\n",
				__func__, dst, src, unwritten))
		BUG();
}

/**
 * arch_wmb_pmem - synchronize writes to persistent memory
 *
 * After a series of arch_memcpy_to_pmem() operations this drains data
 * from cpu write buffers and any platform (memory controller) buffers
 * to ensure that written data is durable on persistent memory media.
 */
static inline void arch_wmb_pmem(void)
{
	/*
	 * wmb() to 'sfence' all previous writes such that they are
	 * architecturally visible to 'pcommit'.  Note, that we've
	 * already arranged for pmem writes to avoid the cache via
	 * arch_memcpy_to_pmem().
	 */
	wmb();
	pcommit_sfence();
}

static inline bool arch_has_wmb_pmem(void)
{
#ifdef CONFIG_X86_64
	/*
	 * We require that wmb() be an 'sfence', that is only guaranteed on
	 * 64-bit builds
	 */
	return static_cpu_has(X86_FEATURE_PCOMMIT);
#else
	return false;
#endif
}
#endif /* CONFIG_ARCH_HAS_PMEM_API */

#endif /* __ASM_X86_PMEM_H__ */
