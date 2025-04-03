// SPDX-License-Identifier: GPL-2.0

#include <linux/string.h>
#include <linux/export.h>

#if !defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_CC_HAS_KASAN_MEMINTRINSIC_PREFIX)
/*
 * If CONFIG_KASAN_GENERIC is on but CONFIG_CC_HAS_KASAN_MEMINTRINSIC_PREFIX is
 * off, mm/kasan/shadow.c will define the kasan version memcpy and its friends
 * for us. We should not do anything, otherwise the symbols will conflict.
 */

#undef memcpy
#undef memset
#undef memmove

__visible void *memcpy(void *dest, const void *src, size_t count)
{
	return __memcpy(dest, src, count);
}
EXPORT_SYMBOL(memcpy);

__visible void *memset(void *s, int c, size_t count)
{
	return __memset(s, c, count);
}
EXPORT_SYMBOL(memset);

__visible void *memmove(void *dest, const void *src, size_t count)
{
	return __memmove(dest, src, count);
}
EXPORT_SYMBOL(memmove);

#endif
