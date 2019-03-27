/*
 * Copyright (C) 2004-2007  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id: formatcheck.h,v 1.13 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_FORMATCHECK_H
#define ISC_FORMATCHECK_H 1

/*! \file isc/formatcheck.h */

/*%
 * ISC_FORMAT_PRINTF().
 *
 * \li fmt is the location of the format string parameter.
 * \li args is the location of the first argument (or 0 for no argument checking).
 * 
 * Note:
 * \li The first parameter is 1, not 0.
 */
#ifdef __GNUC__
#define ISC_FORMAT_PRINTF(fmt, args) __attribute__((__format__(__printf__, fmt, args)))
#else
#define ISC_FORMAT_PRINTF(fmt, args)
#endif

#endif /* ISC_FORMATCHECK_H */
