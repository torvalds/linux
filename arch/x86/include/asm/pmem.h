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
 * a subsequent pmem driver flush operation will drain posted write queues.
 */
static inline void arch_memcpy_to_pmem(void *dst, const void *src, size_t n)
{
	int rem;

	/*
	 * We are copying between two kernel buffers, if
	 * __copy_from_user_inatomic_nocache() returns an error (page
	 * fault) we would have already reported a general protection fault
	 * before the WARN+BUG.
	 */
	rem = __copy_from_user_inatomic_nocache(dst, (void __user *) src, n);
	if (WARN(rem, "%s: fault copying %p <- %p unwritten: %d\n",
				__func__, dst, src, rem))
		BUG();
}

static inline void arch_invalidate_pmem(void *addr, size_t size)
{
	clflush_cache_range(addr, size);
}
#endif /* CONFIG_ARCH_HAS_PMEM_API */
#endif /* __ASM_X86_PMEM_H__ */
