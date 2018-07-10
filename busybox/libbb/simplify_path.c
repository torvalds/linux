/* vi: set sw=4 ts=4: */
/*
 * bb_simplify_path implementation for busybox
 *
 * Copyright (C) 2001  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

char* FAST_FUNC bb_simplify_abs_path_inplace(char *start)
{
	char *s, *p;

	p = s = start;
	do {
		if (*p == '/') {
			if (*s == '/') {  /* skip duplicate (or initial) slash */
				continue;
			}
			if (*s == '.') {
				if (s[1] == '/' || !s[1]) {  /* remove extra '.' */
					continue;
				}
				if ((s[1] == '.') && (s[2] == '/' || !s[2])) {
					++s;
					if (p > start) {
						while (*--p != '/')  /* omit previous dir */
							continue;
					}
					continue;
				}
			}
		}
		*++p = *s;
	} while (*++s);

	if ((p == start) || (*p != '/')) {  /* not a trailing slash */
		++p;  /* so keep last character */
	}
	*p = '\0';
	return p;
}

char* FAST_FUNC bb_simplify_path(const char *path)
{
	char *s, *p;

	if (path[0] == '/')
		s = xstrdup(path);
	else {
		p = xrealloc_getcwd_or_warn(NULL);
		s = concat_path_file(p, path);
		free(p);
	}

	bb_simplify_abs_path_inplace(s);
	return s;
}
