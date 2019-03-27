/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2001-2002 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Routines for memory management
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "memory.h"

static void
memory_bailout(void)
{
	(void) fprintf(stderr, "Out of memory\n");
	exit(1);
}

void *
xmalloc(size_t size)
{
	void *mem;

	if ((mem = malloc(size)) == NULL)
		memory_bailout();

	return (mem);
}

void *
xcalloc(size_t size)
{
	void *mem;

	mem = xmalloc(size);
	bzero(mem, size);

	return (mem);
}

char *
xstrdup(const char *str)
{
	char *newstr;

	if ((newstr = strdup(str)) == NULL)
		memory_bailout();

	return (newstr);
}

char *
xstrndup(char *str, size_t len)
{
	char *newstr;

	if ((newstr = malloc(len + 1)) == NULL)
		memory_bailout();

	(void) strncpy(newstr, str, len);
	newstr[len] = '\0';

	return (newstr);
}

void *
xrealloc(void *ptr, size_t size)
{
	void *mem;

	if ((mem = realloc(ptr, size)) == NULL)
		memory_bailout();

	return (mem);
}
