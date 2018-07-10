/*
 * Utility routines.
 *
 * Copyright (C) 2013 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

//kbuild:lib-y += endofname.o

#include "libbb.h"

#define is_name(c)      ((c) == '_' || isalpha((unsigned char)(c)))
#define is_in_name(c)   ((c) == '_' || isalnum((unsigned char)(c)))

const char* FAST_FUNC
endofname(const char *name)
{
	if (!is_name(*name))
		return name;
	while (*++name) {
		if (!is_in_name(*name))
			break;
	}
	return name;
}
