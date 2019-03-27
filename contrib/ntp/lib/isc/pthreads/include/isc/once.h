/*
 * Copyright (C) 2004, 2005, 2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2001  Internet Software Consortium.
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

/* $Id: once.h,v 1.13 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_ONCE_H
#define ISC_ONCE_H 1

/*! \file */

#include <pthread.h>

#include <isc/platform.h>
#include <isc/result.h>

typedef pthread_once_t isc_once_t;

#ifdef ISC_PLATFORM_BRACEPTHREADONCEINIT
/*!
 * This accomodates systems that define PTHRAD_ONCE_INIT improperly.
 */
#define ISC_ONCE_INIT { PTHREAD_ONCE_INIT }
#else
/*!
 * This is the usual case.
 */
#define ISC_ONCE_INIT PTHREAD_ONCE_INIT
#endif

/* XXX We could do fancier error handling... */

#define isc_once_do(op, f) \
	((pthread_once((op), (f)) == 0) ? \
	 ISC_R_SUCCESS : ISC_R_UNEXPECTED)

#endif /* ISC_ONCE_H */
