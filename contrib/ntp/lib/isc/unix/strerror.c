/*
 * Copyright (C) 2004, 2005, 2007, 2009  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2001  Internet Software Consortium.
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

/* $Id: strerror.c,v 1.10 2009/02/16 23:48:04 tbox Exp $ */

/*! \file */

#include <config.h>

#include <stdio.h>
#include <string.h>

#include <isc/mutex.h>
#include <isc/once.h>
#include <isc/print.h>
#include <isc/strerror.h>
#include <isc/util.h>

#include "l_stdlib.h"		/* NTP local change */

#ifdef HAVE_STRERROR
/*%
 * We need to do this this way for profiled locks.
 */
static isc_mutex_t isc_strerror_lock;
static void init_lock(void) {
	RUNTIME_CHECK(isc_mutex_init(&isc_strerror_lock) == ISC_R_SUCCESS);
}
#else
extern const char * const sys_errlist[];
extern const int sys_nerr;
#endif

void
isc__strerror(int num, char *buf, size_t size) {
#ifdef HAVE_STRERROR
	char *msg;
	unsigned int unum = (unsigned int)num;
	static isc_once_t once = ISC_ONCE_INIT;

	REQUIRE(buf != NULL);

	RUNTIME_CHECK(isc_once_do(&once, init_lock) == ISC_R_SUCCESS);

	LOCK(&isc_strerror_lock);
	msg = strerror(num);
	if (msg != NULL)
		snprintf(buf, size, "%s", msg);
	else
		snprintf(buf, size, "Unknown error: %u", unum);
	UNLOCK(&isc_strerror_lock);
#else
	unsigned int unum = (unsigned int)num;

	REQUIRE(buf != NULL);

	if (num >= 0 && num < sys_nerr)
		snprintf(buf, size, "%s", sys_errlist[num]);
	else
		snprintf(buf, size, "Unknown error: %u", unum);
#endif
}
