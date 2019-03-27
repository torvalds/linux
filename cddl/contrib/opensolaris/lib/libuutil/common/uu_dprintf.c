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

#include <errno.h>
#include <libintl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define	FACILITY_FMT	"%s (%s): "

#if !defined(TEXT_DOMAIN)
#define	TEXT_DOMAIN "SYS_TEST"
#endif

static const char *
strseverity(uu_dprintf_severity_t severity)
{
	switch (severity) {
	case UU_DPRINTF_SILENT:
		return (dgettext(TEXT_DOMAIN, "silent"));
	case UU_DPRINTF_FATAL:
		return (dgettext(TEXT_DOMAIN, "FATAL"));
	case UU_DPRINTF_WARNING:
		return (dgettext(TEXT_DOMAIN, "WARNING"));
	case UU_DPRINTF_NOTICE:
		return (dgettext(TEXT_DOMAIN, "note"));
	case UU_DPRINTF_INFO:
		return (dgettext(TEXT_DOMAIN, "info"));
	case UU_DPRINTF_DEBUG:
		return (dgettext(TEXT_DOMAIN, "debug"));
	default:
		return (dgettext(TEXT_DOMAIN, "unspecified"));
	}
}

uu_dprintf_t *
uu_dprintf_create(const char *name, uu_dprintf_severity_t severity,
    uint_t flags)
{
	uu_dprintf_t *D;

	if (uu_check_name(name, UU_NAME_DOMAIN) == -1) {
		uu_set_error(UU_ERROR_INVALID_ARGUMENT);
		return (NULL);
	}

	if ((D = uu_zalloc(sizeof (uu_dprintf_t))) == NULL)
		return (NULL);

	if (name != NULL) {
		D->uud_name = strdup(name);
		if (D->uud_name == NULL) {
			uu_free(D);
			return (NULL);
		}
	} else {
		D->uud_name = NULL;
	}

	D->uud_severity = severity;
	D->uud_flags = flags;

	return (D);
}

/*PRINTFLIKE3*/
void
uu_dprintf(uu_dprintf_t *D, uu_dprintf_severity_t severity,
    const char *format, ...)
{
	va_list alist;

	/* XXX Assert that severity is not UU_DPRINTF_SILENT. */

	if (severity > D->uud_severity)
		return;

	(void) fprintf(stderr, FACILITY_FMT, D->uud_name,
	    strseverity(severity));

	va_start(alist, format);
	(void) vfprintf(stderr, format, alist);
	va_end(alist);
}

void
uu_dprintf_destroy(uu_dprintf_t *D)
{
	if (D->uud_name)
		free(D->uud_name);

	uu_free(D);
}

const char *
uu_dprintf_getname(uu_dprintf_t *D)
{
	return (D->uud_name);
}
