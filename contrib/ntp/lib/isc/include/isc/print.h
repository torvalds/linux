/*
 * Copyright (C) 2004-2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1999-2001, 2003  Internet Software Consortium.
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

/* $Id: print.h,v 1.26 2007/06/19 23:47:18 tbox Exp $ */

#ifndef ISC_PRINT_H
#define ISC_PRINT_H 1

/*! \file isc/print.h */

/***
 *** Imports
 ***/

#include <isc/formatcheck.h>    /* Required for ISC_FORMAT_PRINTF() macro. */
#include <isc/lang.h>
#include <isc/platform.h>

/*!
 * This block allows lib/isc/print.c to be cleanly compiled even if
 * the platform does not need it.  The standard Makefile will still
 * not compile print.c or archive print.o, so this is just to make test
 * compilation ("make print.o") easier.
 */
#if !defined(ISC_PLATFORM_NEEDVSNPRINTF) && defined(ISC__PRINT_SOURCE)
#define ISC_PLATFORM_NEEDVSNPRINTF
#endif

#if !defined(ISC_PLATFORM_NEEDSPRINTF) && defined(ISC__PRINT_SOURCE)
#define ISC_PLATFORM_NEEDSPRINTF
#endif

/***
 *** Macros
 ***/
#define ISC_PRINT_QUADFORMAT ISC_PLATFORM_QUADFORMAT

/***
 *** Functions
 ***/

#ifdef ISC_PLATFORM_NEEDVSNPRINTF
#include <stdarg.h>
#include <stddef.h>
#endif
#ifdef ISC_PLATFORM_NEEDSPRINTF
#include <stdio.h>
#endif


ISC_LANG_BEGINDECLS

#ifdef ISC_PLATFORM_NEEDVSNPRINTF
int
isc_print_vsnprintf(char *str, size_t size, const char *format, va_list ap)
     ISC_FORMAT_PRINTF(3, 0);
#define vsnprintf isc_print_vsnprintf

int
isc_print_snprintf(char *str, size_t size, const char *format, ...)
     ISC_FORMAT_PRINTF(3, 4);
#define snprintf isc_print_snprintf
#endif /* ISC_PLATFORM_NEEDVSNPRINTF */

#ifdef ISC_PLATFORM_NEEDSPRINTF
int
isc_print_sprintf(char *str, const char *format, ...) ISC_FORMAT_PRINTF(2, 3);
#define sprintf isc_print_sprintf
#endif

ISC_LANG_ENDDECLS

#endif /* ISC_PRINT_H */
