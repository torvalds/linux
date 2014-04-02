/*
 *  linux/lib/eabi.c
 *
 *  Copyright (C) 2012  Mark Charlebois
 */

/*
 * EABI routines
 */

#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/export.h>

void __aeabi_memcpy(void *dest, const void *src, size_t n)
{
	(void)memcpy(dest, src, n);
}
EXPORT_SYMBOL(__aeabi_memcpy);

void __aeabi_memmove(void *dest, const void *src, size_t n)
{
	(void)memmove(dest, src, n);
}
EXPORT_SYMBOL(__aeabi_memmove);

void __aeabi_memset(void *s, size_t n, int c)
{
	(void)memset(s, c, n);
}
EXPORT_SYMBOL(__aeabi_memset);
