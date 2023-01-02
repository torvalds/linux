// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <linux/export.h>

#undef memcpy
#undef memset
#undef memmove

__visible void *memcpy(void *to, const void *from, size_t n)
{
	return __memcpy(to, from, n);
}
EXPORT_SYMBOL(memcpy);

__visible void *memset(void *s, int c, size_t count)
{
	return __memset(s, c, count);
}
EXPORT_SYMBOL(memset);
