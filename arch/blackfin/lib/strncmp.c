/*
 * Provide symbol in case str func is not inlined.
 *
 * Copyright (c) 2006-2007 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#define strncmp __inline_strncmp
#include <asm/string.h>
#include <linux/module.h>
#undef strncmp

int strncmp(const char *cs, const char *ct, size_t count)
{
	return __inline_strncmp(cs, ct, count);
}
EXPORT_SYMBOL(strncmp);
