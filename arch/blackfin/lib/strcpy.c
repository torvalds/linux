/*
 * Provide symbol in case str func is not inlined.
 *
 * Copyright (c) 2006-2007 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#define strcpy __inline_strcpy
#include <asm/string.h>
#undef strcpy

#include <linux/module.h>

char *strcpy(char *dest, const char *src)
{
	return __inline_strcpy(dest, src);
}
EXPORT_SYMBOL(strcpy);
