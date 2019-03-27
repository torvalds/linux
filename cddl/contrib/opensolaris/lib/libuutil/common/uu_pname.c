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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include "libuutil_common.h"

#include <libintl.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <wchar.h>
#include <unistd.h>

static const char PNAME_FMT[] = "%s: ";
static const char ERRNO_FMT[] = ": %s\n";

static const char *pname;

static void
uu_die_internal(int status, const char *format, va_list alist) __NORETURN;

int uu_exit_ok_value = EXIT_SUCCESS;
int uu_exit_fatal_value = EXIT_FAILURE;
int uu_exit_usage_value = 2;

int *
uu_exit_ok(void)
{
	return (&uu_exit_ok_value);
}

int *
uu_exit_fatal(void)
{
	return (&uu_exit_fatal_value);
}

int *
uu_exit_usage(void)
{
	return (&uu_exit_usage_value);
}

void
uu_alt_exit(int profile)
{
	switch (profile) {
	case UU_PROFILE_DEFAULT:
		uu_exit_ok_value = EXIT_SUCCESS;
		uu_exit_fatal_value = EXIT_FAILURE;
		uu_exit_usage_value = 2;
		break;
	case UU_PROFILE_LAUNCHER:
		uu_exit_ok_value = EXIT_SUCCESS;
		uu_exit_fatal_value = 124;
		uu_exit_usage_value = 125;
		break;
	}
}

static void
uu_warn_internal(int err, const char *format, va_list alist)
{
	if (pname != NULL)
		(void) fprintf(stderr, PNAME_FMT, pname);

	(void) vfprintf(stderr, format, alist);

	if (strrchr(format, '\n') == NULL)
		(void) fprintf(stderr, ERRNO_FMT, strerror(err));
}

void
uu_vwarn(const char *format, va_list alist)
{
	uu_warn_internal(errno, format, alist);
}

/*PRINTFLIKE1*/
void
uu_warn(const char *format, ...)
{
	va_list alist;
	va_start(alist, format);
	uu_warn_internal(errno, format, alist);
	va_end(alist);
}

static void
uu_die_internal(int status, const char *format, va_list alist)
{
	uu_warn_internal(errno, format, alist);
#ifdef DEBUG
	{
		char *cp;

		if (!issetugid()) {
			cp = getenv("UU_DIE_ABORTS");
			if (cp != NULL && *cp != '\0')
				abort();
		}
	}
#endif
	exit(status);
}

void
uu_vdie(const char *format, va_list alist)
{
	uu_die_internal(UU_EXIT_FATAL, format, alist);
}

/*PRINTFLIKE1*/
void
uu_die(const char *format, ...)
{
	va_list alist;
	va_start(alist, format);
	uu_die_internal(UU_EXIT_FATAL, format, alist);
	va_end(alist);
}

void
uu_vxdie(int status, const char *format, va_list alist)
{
	uu_die_internal(status, format, alist);
}

/*PRINTFLIKE2*/
void
uu_xdie(int status, const char *format, ...)
{
	va_list alist;
	va_start(alist, format);
	uu_die_internal(status, format, alist);
	va_end(alist);
}

const char *
uu_setpname(char *arg0)
{
	/*
	 * Having a NULL argv[0], while uncommon, is possible.  It
	 * makes more sense to handle this event in uu_setpname rather
	 * than in each of its consumers.
	 */
	if (arg0 == NULL) {
		pname = "unknown_command";
		return (pname);
	}

	/*
	 * Guard against '/' at end of command invocation.
	 */
	for (;;) {
		char *p = strrchr(arg0, '/');
		if (p == NULL) {
			pname = arg0;
			break;
		} else {
			if (*(p + 1) == '\0') {
				*p = '\0';
				continue;
			}

			pname = p + 1;
			break;
		}
	}

	return (pname);
}

const char *
uu_getpname(void)
{
	return (pname);
}
