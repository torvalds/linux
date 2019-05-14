/*
 * Copyright(c) 2017 IBM Corporation. All rights reserved.
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

#include <linux/string.h>
#include <linux/export.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>

/*
 * CONFIG_ARCH_HAS_PMEM_API symbols
 */
void arch_wb_cache_pmem(void *addr, size_t size)
{
	unsigned long start = (unsigned long) addr;
	flush_dcache_range(start, start + size);
}
EXPORT_SYMBOL(arch_wb_cache_pmem);

void arch_invalidate_pmem(void *addr, size_t size)
{
	unsigned long start = (unsigned long) addr;
	flush_dcache_range(start, start + size);
}
EXPORT_SYMBOL(arch_invalidate_pmem);

/*
 * CONFIG_ARCH_HAS_UACCESS_FLUSHCACHE symbols
 */
long __copy_from_user_flushcache(void *dest, const void __user *src,
		unsigned size)
{
	unsigned long copied, start = (unsigned long) dest;

	copied = __copy_from_user(dest, src, size);
	flush_dcache_range(start, start + size);

	return copied;
}

void *memcpy_flushcache(void *dest, const void *src, size_t size)
{
	unsigned long start = (unsigned long) dest;

	memcpy(dest, src, size);
	flush_dcache_range(start, start + size);

	return dest;
}
EXPORT_SYMBOL(memcpy_flushcache);

void memcpy_page_flushcache(char *to, struct page *page, size_t offset,
	size_t len)
{
	memcpy_flushcache(to, page_to_virt(page) + offset, len);
}
EXPORT_SYMBOL(memcpy_page_flushcache);
