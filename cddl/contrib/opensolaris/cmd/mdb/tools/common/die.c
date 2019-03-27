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

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <elf.h>

#include <util.h>

void
die(char *format, ...)
{
	va_list ap;
	int err = errno;
#ifndef illumos
	const char *progname = getprogname();
#endif

	(void) fprintf(stderr, "%s: ", progname);

	va_start(ap, format);
	/* LINTED - variable format specifier */
	(void) vfprintf(stderr, format, ap);
	va_end(ap);

	if (format[strlen(format) - 1] != '\n')
		(void) fprintf(stderr, ": %s\n", strerror(err));

#ifndef illumos
	exit(0);
#else
	exit(1);
#endif
}

void
elfdie(char *format, ...)
{
	va_list ap;
#ifndef illumos
	const char *progname = getprogname();
#endif

	(void) fprintf(stderr, "%s: ", progname);

	va_start(ap, format);
	/* LINTED - variable format specifier */
	(void) vfprintf(stderr, format, ap);
	va_end(ap);

	if (format[strlen(format) - 1] != '\n')
		(void) fprintf(stderr, ": %s\n", elf_errmsg(elf_errno()));

#ifndef illumos
	exit(0);
#else
	exit(1);
#endif
}
