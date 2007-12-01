/*
 * Provide symbol in case str func is not inlined.
 *
 * Copyright (c) 2006-2007 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#define strcmp __inline_strcmp
#include <asm/string.h>
#undef strcmp

#include <linux/module.h>

int strcmp(const char *dest, const char *src)
{
	return __inline_strcmp(dest, src);
}
EXPORT_SYMBOL(strcmp);
