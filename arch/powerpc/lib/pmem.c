// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2017 IBM Corporation. All rights reserved.
 */

#include <linux/string.h>
#include <linux/export.h>
#include <linux/uaccess.h>
#include <linux/libnvdimm.h>

#include <asm/cacheflush.h>

static inline void __clean_pmem_range(unsigned long start, unsigned long stop)
{
	unsigned long shift = l1_dcache_shift();
	unsigned long bytes = l1_dcache_bytes();
	void *addr = (void *)(start & ~(bytes - 1));
	unsigned long size = stop - (unsigned long)addr + (bytes - 1);
	unsigned long i;

	for (i = 0; i < size >> shift; i++, addr += bytes)
		asm volatile(PPC_DCBSTPS(%0, %1): :"i"(0), "r"(addr): "memory");
}

static inline void __flush_pmem_range(unsigned long start, unsigned long stop)
{
	unsigned long shift = l1_dcache_shift();
	unsigned long bytes = l1_dcache_bytes();
	void *addr = (void *)(start & ~(bytes - 1));
	unsigned long size = stop - (unsigned long)addr + (bytes - 1);
	unsigned long i;

	for (i = 0; i < size >> shift; i++, addr += bytes)
		asm volatile(PPC_DCBFPS(%0, %1): :"i"(0), "r"(addr): "memory");
}

static inline void clean_pmem_range(unsigned long start, unsigned long stop)
{
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		return __clean_pmem_range(start, stop);
}

static inline void flush_pmem_range(unsigned long start, unsigned long stop)
{
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		return __flush_pmem_range(start, stop);
}

/*
 * CONFIG_ARCH_HAS_PMEM_API symbols
 */
void arch_wb_cache_pmem(void *addr, size_t size)
{
	unsigned long start = (unsigned long) addr;
	clean_pmem_range(start, start + size);
}
EXPORT_SYMBOL_GPL(arch_wb_cache_pmem);

void arch_invalidate_pmem(void *addr, size_t size)
{
	unsigned long start = (unsigned long) addr;
	flush_pmem_range(start, start + size);
}
EXPORT_SYMBOL_GPL(arch_invalidate_pmem);

/*
 * CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE symbols
 */
long __copy_from_user_flushcache(void *dest, const void __user *src,
		unsigned size)
{
	unsigned long copied, start = (unsigned long) dest;

	copied = __copy_from_user(dest, src, size);
	clean_pmem_range(start, start + size);

	return copied;
}

void memcpy_flushcache(void *dest, const void *src, size_t size)
{
	unsigned long start = (unsigned long) dest;

	memcpy(dest, src, size);
	clean_pmem_range(start, start + size);
}
EXPORT_SYMBOL(memcpy_flushcache);

void memcpy_page_flushcache(char *to, struct page *page, size_t offset,
	size_t len)
{
	memcpy_flushcache(to, page_to_virt(page) + offset, len);
}
EXPORT_SYMBOL(memcpy_page_flushcache);
