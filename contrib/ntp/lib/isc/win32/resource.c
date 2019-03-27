/*
 * Copyright (C) 2004, 2007, 2008  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001  Internet Software Consortium.
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

/* $Id: resource.c,v 1.10 2008/07/11 23:47:09 tbox Exp $ */

#include <config.h>

#include <stdio.h>

#include <isc/platform.h>
#include <isc/resource.h>
#include <isc/result.h>
#include <isc/util.h>

#include "errno2result.h"

/*
 * Windows limits the maximum number of open files to 2048
 */

#define WIN32_MAX_OPEN_FILES	2048

isc_result_t
isc_resource_setlimit(isc_resource_t resource, isc_resourcevalue_t value) {
	isc_resourcevalue_t rlim_value;
	int wresult;

	if (resource != isc_resource_openfiles)
		return (ISC_R_NOTIMPLEMENTED);


	if (value == ISC_RESOURCE_UNLIMITED)
		rlim_value = WIN32_MAX_OPEN_FILES;
	else
		rlim_value = min(value, WIN32_MAX_OPEN_FILES);

	wresult = _setmaxstdio((int) rlim_value);

	if (wresult > 0)
		return (ISC_R_SUCCESS);
	else
		return (isc__errno2result(errno));
}

isc_result_t
isc_resource_getlimit(isc_resource_t resource, isc_resourcevalue_t *value) {

	if (resource != isc_resource_openfiles)
		return (ISC_R_NOTIMPLEMENTED);

	*value = WIN32_MAX_OPEN_FILES;
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_resource_getcurlimit(isc_resource_t resource, isc_resourcevalue_t *value) {
	return (isc_resource_getlimit(resource, value));
}
