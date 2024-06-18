// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2017 ARM Ltd.
 */

#include <linux/uaccess.h>
#include <asm/barrier.h>
#include <asm/cacheflush.h>

void memcpy_flushcache(void *dst, const void *src, size_t cnt)
{
	/*
	 * We assume this should not be called with @dst pointing to
	 * non-cacheable memory, such that we don't need an explicit
	 * barrier to order the cache maintenance against the memcpy.
	 */
	memcpy(dst, src, cnt);
	dcache_clean_pop((unsigned long)dst, (unsigned long)dst + cnt);
}
EXPORT_SYMBOL_GPL(memcpy_flushcache);

unsigned long __copy_user_flushcache(void *to, const void __user *from,
				     unsigned long n)
{
	unsigned long rc;

	rc = raw_copy_from_user(to, from, n);

	/* See above */
	dcache_clean_pop((unsigned long)to, (unsigned long)to + n - rc);
	return rc;
}
