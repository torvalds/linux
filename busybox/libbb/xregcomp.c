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
#include "xregex.h"

char* FAST_FUNC regcomp_or_errmsg(regex_t *preg, const char *regex, int cflags)
{
	int ret = regcomp(preg, regex, cflags);
	if (ret) {
		int errmsgsz = regerror(ret, preg, NULL, 0);
		char *errmsg = xmalloc(errmsgsz);
		regerror(ret, preg, errmsg, errmsgsz);
		return errmsg;
	}
	return NULL;
}

void FAST_FUNC xregcomp(regex_t *preg, const char *regex, int cflags)
{
	char *errmsg = regcomp_or_errmsg(preg, regex, cflags);
	if (errmsg) {
		bb_error_msg_and_die("bad regex '%s': %s", regex, errmsg);
	}
}
