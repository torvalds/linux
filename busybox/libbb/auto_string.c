/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2015 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//kbuild:lib-y += auto_string.o

#include "libbb.h"

char* FAST_FUNC auto_string(char *str)
{
	static char *saved[4];
	static uint8_t cur_saved; /* = 0 */

	free(saved[cur_saved]);
	saved[cur_saved] = str;
	cur_saved = (cur_saved + 1) & (ARRAY_SIZE(saved)-1);

	return str;
}
