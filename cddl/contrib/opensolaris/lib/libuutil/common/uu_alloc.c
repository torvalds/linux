/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include "libuutil_common.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *
uu_zalloc(size_t n)
{
	void *p = malloc(n);

	if (p == NULL) {
		uu_set_error(UU_ERROR_SYSTEM);
		return (NULL);
	}

	(void) memset(p, 0, n);

	return (p);
}

void
uu_free(void *p)
{
	free(p);
}

char *
uu_strdup(const char *str)
{
	char *buf = NULL;

	if (str != NULL) {
		size_t sz;

		sz = strlen(str) + 1;
		buf = uu_zalloc(sz);
		if (buf != NULL)
			(void) memcpy(buf, str, sz);
	}
	return (buf);
}

/*
 * Duplicate up to n bytes of a string.  Kind of sort of like
 * strdup(strlcpy(s, n)).
 */
char *
uu_strndup(const char *s, size_t n)
{
	size_t len;
	char *p;

	len = strnlen(s, n);
	p = uu_zalloc(len + 1);
	if (p == NULL)
		return (NULL);

	if (len > 0)
		(void) memcpy(p, s, len);
	p[len] = '\0';

	return (p);
}

/*
 * Duplicate a block of memory.  Combines malloc with memcpy, much as
 * strdup combines malloc, strlen, and strcpy.
 */
void *
uu_memdup(const void *buf, size_t sz)
{
	void *p;

	p = uu_zalloc(sz);
	if (p == NULL)
		return (NULL);
	(void) memcpy(p, buf, sz);
	return (p);
}

char *
uu_msprintf(const char *format, ...)
{
	va_list args;
	char attic[1];
	uint_t M, m;
	char *b;

	va_start(args, format);
	M = vsnprintf(attic, 1, format, args);
	va_end(args);

	for (;;) {
		m = M;
		if ((b = uu_zalloc(m + 1)) == NULL)
			return (NULL);

		va_start(args, format);
		M = vsnprintf(b, m + 1, format, args);
		va_end(args);

		if (M == m)
			break;		/* sizes match */

		uu_free(b);
	}

	return (b);
}
