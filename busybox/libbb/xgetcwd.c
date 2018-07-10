/* vi: set sw=4 ts=4: */
/*
 * xgetcwd.c -- return current directory with unlimited length
 * Copyright (C) 1992, 1996 Free Software Foundation, Inc.
 * Written by David MacKenzie <djm@gnu.ai.mit.edu>.
 *
 * Special function for busybox written by Vladimir Oleynik <dzo@simtreas.ru>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* Return the current directory, newly allocated, arbitrarily long.
   Return NULL and set errno on error.
   If argument is not NULL (previous usage allocate memory), call free()
*/

char* FAST_FUNC
xrealloc_getcwd_or_warn(char *cwd)
{
#define PATH_INCR 64

	char *ret;
	unsigned path_max;

	path_max = 128; /* 128 + 64 should be enough for 99% of cases */

	while (1) {
		path_max += PATH_INCR;
		cwd = xrealloc(cwd, path_max);
		ret = getcwd(cwd, path_max);
		if (ret == NULL) {
			if (errno == ERANGE)
				continue;
			free(cwd);
			bb_perror_msg("getcwd");
			return NULL;
		}
		cwd = xrealloc(cwd, strlen(cwd) + 1);
		return cwd;
	}
}
