/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) many different people.
 * If you wrote this, please acknowledge your work.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

void FAST_FUNC chomp(char *s)
{
	char *lc = last_char_is(s, '\n');

	if (lc)
		*lc = '\0';
}
