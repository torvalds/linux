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

static inline int arch_memcpy_from_pmem(void *dst, const void *src, size_t n)
{
	return memcpy_mcsafe(dst, src, n);
}

/**
 * arch_wb_cache_pmem - write back a cache range with CLWB
 * @vaddr:	virtual start address
 * @size:	number of bytes to write back
 *
 * Write back a cache range using the CLWB (cache line write back)
 * instruction. Note that @size is internally rounded up to be cache
 * line size aligned.
 */
static inline void arch_wb_cache_pmem(void *addr, size_t size)
{
	u16 x86_clflush_size = boot_cpu_data.x86_clflush_size;
	unsigned long clflush_mask = x86_clflush_size - 1;
	void *vend = addr + size;
	void *p;

	for (p = (void *)((unsigned long)addr & ~clflush_mask);
	     p < vend; p += x86_clflush_size)
		clwb(p);
}

/**
 * arch_copy_from_iter_pmem - copy data from an iterator to PMEM
 * @addr:	PMEM destination address
 * @bytes:	number of bytes to copy
 * @i:		iterator with source data
 *
 * Copy data from the iterator 'i' to the PMEM buffer starting at 'addr'.
 */
static inline size_t arch_copy_from_iter_pmem(void *addr, size_t bytes,
		struct iov_iter *i)
{
	size_t len;

	/* TODO: skip the write-back by always using non-temporal stores */
	len = copy_from_iter_nocache(addr, bytes, i);

	/*
	 * In the iovec case on x86_64 copy_from_iter_nocache() uses
	 * non-temporal stores for the bulk of the transfer, but we need
	 * to manually flush if the transfer is unaligned. A cached
	 * memory copy is used when destination or size is not naturally
	 * aligned. That is:
	 *   - Require 8-byte alignment when size is 8 bytes or larger.
	 *   - Require 4-byte alignment when size is 4 bytes.
	 *
	 * In the non-iovec case the entire destination needs to be
	 * flushed.
	 */
	if (iter_is_iovec(i)) {
		unsigned long flushed, dest = (unsigned long) addr;

		if (bytes < 8) {
			if (!IS_ALIGNED(dest, 4) || (bytes != 4))
				arch_wb_cache_pmem(addr, bytes);
		} else {
			if (!IS_ALIGNED(dest, 8)) {
				dest = ALIGN(dest, boot_cpu_data.x86_clflush_size);
				arch_wb_cache_pmem(addr, 1);
			}

			flushed = dest - (unsigned long) addr;
			if (bytes > flushed && !IS_ALIGNED(bytes - flushed, 8))
				arch_wb_cache_pmem(addr + bytes - 1, 1);
		}
	} else
		arch_wb_cache_pmem(addr, bytes);

	return len;
}

/**
 * arch_clear_pmem - zero a PMEM memory range
 * @addr:	virtual start address
 * @size:	number of bytes to zero
 *
 * Write zeros into the memory range starting at 'addr' for 'size' bytes.
 */
static inline void arch_clear_pmem(void *addr, size_t size)
{
	memset(addr, 0, size);
	arch_wb_cache_pmem(addr, size);
}

static inline void arch_invalidate_pmem(void *addr, size_t size)
{
	clflush_cache_range(addr, size);
}
#endif /* CONFIG_ARCH_HAS_PMEM_API */
#endif /* __ASM_X86_PMEM_H__ */
