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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "libuutil_common.h"

#include <string.h>

/*
 * We require names of the form:
 *	[provider,]identifier[/[provider,]identifier]...
 *
 * Where provider is either a stock symbol (SUNW) or a java-style reversed
 * domain name (com.sun).
 *
 * Both providers and identifiers must start with a letter, and may
 * only contain alphanumerics, dashes, and underlines.  Providers
 * may also contain periods.
 *
 * Note that we do _not_ use the macros in <ctype.h>, since they are affected
 * by the current locale settings.
 */

#define	IS_ALPHA(c) \
	(((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))

#define	IS_DIGIT(c) \
	((c) >= '0' && (c) <= '9')

static int
is_valid_ident(const char *s, const char *e, int allowdot)
{
	char c;

	if (s >= e)
		return (0);		/* name is empty */

	c = *s++;
	if (!IS_ALPHA(c))
		return (0);		/* does not start with letter */

	while (s < e && (c = *s++) != 0) {
		if (IS_ALPHA(c) || IS_DIGIT(c) || c == '-' || c == '_' ||
		    (allowdot && c == '.'))
			continue;
		return (0);		/* invalid character */
	}
	return (1);
}

static int
is_valid_component(const char *b, const char *e, uint_t flags)
{
	char *sp;

	if (flags & UU_NAME_DOMAIN) {
		sp = strchr(b, ',');
		if (sp != NULL && sp < e) {
			if (!is_valid_ident(b, sp, 1))
				return (0);
			b = sp + 1;
		}
	}

	return (is_valid_ident(b, e, 0));
}

int
uu_check_name(const char *name, uint_t flags)
{
	const char *end = name + strlen(name);
	const char *p;

	if (flags & ~(UU_NAME_DOMAIN | UU_NAME_PATH)) {
		uu_set_error(UU_ERROR_UNKNOWN_FLAG);
		return (-1);
	}

	if (!(flags & UU_NAME_PATH)) {
		if (!is_valid_component(name, end, flags))
			goto bad;
		return (0);
	}

	while ((p = strchr(name, '/')) != NULL) {
		if (!is_valid_component(name, p - 1, flags))
			goto bad;
		name = p + 1;
	}
	if (!is_valid_component(name, end, flags))
		goto bad;

	return (0);

bad:
	uu_set_error(UU_ERROR_INVALID_ARGUMENT);
	return (-1);
}
