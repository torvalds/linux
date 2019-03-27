/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2001  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id: error.c,v 1.21 2007/06/19 23:47:17 tbox Exp $ */

/*! \file */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>

#include <isc/error.h>
#include <isc/msgs.h>

/*% Default unexpected callback. */
static void
default_unexpected_callback(const char *, int, const char *, va_list)
     ISC_FORMAT_PRINTF(3, 0);

/*% Default fatal callback. */
static void
default_fatal_callback(const char *, int, const char *, va_list)
     ISC_FORMAT_PRINTF(3, 0);

/*% unexpected_callback */
static isc_errorcallback_t unexpected_callback = default_unexpected_callback;
static isc_errorcallback_t fatal_callback = default_fatal_callback;

void
isc_error_setunexpected(isc_errorcallback_t cb) {
	if (cb == NULL)
		unexpected_callback = default_unexpected_callback;
	else
		unexpected_callback = cb;
}

void
isc_error_setfatal(isc_errorcallback_t cb) {
	if (cb == NULL)
		fatal_callback = default_fatal_callback;
	else
		fatal_callback = cb;
}

void
isc_error_unexpected(const char *file, int line, const char *format, ...) {
	va_list args;

	va_start(args, format);
	(unexpected_callback)(file, line, format, args);
	va_end(args);
}

void
isc_error_fatal(const char *file, int line, const char *format, ...) {
	va_list args;

	va_start(args, format);
	(fatal_callback)(file, line, format, args);
	va_end(args);
	abort();
}

void
isc_error_runtimecheck(const char *file, int line, const char *expression) {
	isc_error_fatal(file, line, "RUNTIME_CHECK(%s) %s", expression,
			isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
				       ISC_MSG_FAILED, "failed"));
}

static void
default_unexpected_callback(const char *file, int line, const char *format,
			    va_list args)
{
	fprintf(stderr, "%s:%d: ", file, line);
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	fflush(stderr);
}

static void
default_fatal_callback(const char *file, int line, const char *format,
		       va_list args)
{
	fprintf(stderr, "%s:%d: %s: ", file, line,
		isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
			       ISC_MSG_FATALERROR, "fatal error"));
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	fflush(stderr);
}
