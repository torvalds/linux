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

char* FAST_FUNC trim(char *s)
{
	size_t len = strlen(s);
	size_t old = len;

	/* trim trailing whitespace */
	while (len && isspace(s[len-1]))
		--len;

	/* trim leading whitespace */
	if (len) {
		char *nws = skip_whitespace(s);
		if ((nws - s) != 0) {
			len -= (nws - s);
			memmove(s, nws, len);
		}
	}

	s += len;
	/* If it was a "const char*" which does not need trimming,
	 * avoid superfluous store */
	if (old != len)
		*s = '\0';

	return s;
}
