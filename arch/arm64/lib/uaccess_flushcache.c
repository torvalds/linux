/*
 * Copyright (C) 2017 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
	__clean_dcache_area_pop(dst, cnt);
}
EXPORT_SYMBOL_GPL(memcpy_flushcache);

void memcpy_page_flushcache(char *to, struct page *page, size_t offset,
			    size_t len)
{
	memcpy_flushcache(to, page_address(page) + offset, len);
}

unsigned long __copy_user_flushcache(void *to, const void __user *from,
				     unsigned long n)
{
	unsigned long rc = __arch_copy_from_user(to, from, n);

	/* See above */
	__clean_dcache_area_pop(to, n - rc);
	return rc;
}
