/*
 * Provide symbol in case str func is not inlined.
 *
 * Copyright (c) 2006-2007 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#define strncpy __inline_strncpy
#include <asm/string.h>
#undef strncpy

#include <linux/module.h>

char *strncpy(char *dest, const char *src, size_t n)
{
	return __inline_strncpy(dest, src, n);
}
EXPORT_SYMBOL(strncpy);
