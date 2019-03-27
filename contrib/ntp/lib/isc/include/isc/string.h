/*
 * Copyright (C) 2004-2007  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 2000, 2001, 2003  Internet Software Consortium.
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

/* $Id: string.h,v 1.23 2007/09/13 04:48:16 each Exp $ */

#ifndef ISC_STRING_H
#define ISC_STRING_H 1

/*! \file isc/string.h */

#include <isc/formatcheck.h>
#include <isc/int.h>
#include <isc/lang.h>
#include <isc/platform.h>
#include <isc/types.h>

#include <string.h>

#ifdef ISC_PLATFORM_HAVESTRINGSH
#include <strings.h>
#endif

#define ISC_STRING_MAGIC 0x5e

ISC_LANG_BEGINDECLS

isc_uint64_t
isc_string_touint64(char *source, char **endp, int base);
/*%<
 * Convert the string pointed to by 'source' to isc_uint64_t.
 *
 * On successful conversion 'endp' points to the first character
 * after conversion is complete.
 *
 * 'base': 0 or 2..36
 *
 * If base is 0 the base is computed from the string type.
 *
 * On error 'endp' points to 'source'.
 */

isc_result_t
isc_string_copy(char *target, size_t size, const char *source);
/*
 * Copy the string pointed to by 'source' to 'target' which is a
 * pointer to a string of at least 'size' bytes.
 *
 * Requires:
 *	'target' is a pointer to a char[] of at least 'size' bytes.
 *	'size' an integer > 0.
 *	'source' == NULL or points to a NUL terminated string.
 *
 * Ensures:
 *	If result == ISC_R_SUCCESS
 *		'target' will be a NUL terminated string of no more
 *		than 'size' bytes (including NUL).
 *
 *	If result == ISC_R_NOSPACE
 *		'target' is undefined.
 *
 * Returns:
 *	ISC_R_SUCCESS  -- 'source' was successfully copied to 'target'.
 *	ISC_R_NOSPACE  -- 'source' could not be copied since 'target'
 *	                  is too small.
 */

void
isc_string_copy_truncate(char *target, size_t size, const char *source);
/*
 * Copy the string pointed to by 'source' to 'target' which is a
 * pointer to a string of at least 'size' bytes.
 *
 * Requires:
 *	'target' is a pointer to a char[] of at least 'size' bytes.
 *	'size' an integer > 0.
 *	'source' == NULL or points to a NUL terminated string.
 *
 * Ensures:
 *	'target' will be a NUL terminated string of no more
 *	than 'size' bytes (including NUL).
 */

isc_result_t
isc_string_append(char *target, size_t size, const char *source);
/*
 * Append the string pointed to by 'source' to 'target' which is a
 * pointer to a NUL terminated string of at least 'size' bytes.
 *
 * Requires:
 *	'target' is a pointer to a NUL terminated char[] of at
 *	least 'size' bytes.
 *	'size' an integer > 0.
 *	'source' == NULL or points to a NUL terminated string.
 *
 * Ensures:
 *	If result == ISC_R_SUCCESS
 *		'target' will be a NUL terminated string of no more
 *		than 'size' bytes (including NUL).
 *
 *	If result == ISC_R_NOSPACE
 *		'target' is undefined.
 *
 * Returns:
 *	ISC_R_SUCCESS  -- 'source' was successfully appended to 'target'.
 *	ISC_R_NOSPACE  -- 'source' could not be appended since 'target'
 *	                  is too small.
 */

void
isc_string_append_truncate(char *target, size_t size, const char *source);
/*
 * Append the string pointed to by 'source' to 'target' which is a
 * pointer to a NUL terminated string of at least 'size' bytes.
 *
 * Requires:
 *	'target' is a pointer to a NUL terminated char[] of at
 *	least 'size' bytes.
 *	'size' an integer > 0.
 *	'source' == NULL or points to a NUL terminated string.
 *
 * Ensures:
 *	'target' will be a NUL terminated string of no more
 *	than 'size' bytes (including NUL).
 */

isc_result_t
isc_string_printf(char *target, size_t size, const char *format, ...)
	ISC_FORMAT_PRINTF(3, 4);
/*
 * Print 'format' to 'target' which is a pointer to a string of at least
 * 'size' bytes.
 *
 * Requires:
 *	'target' is a pointer to a char[] of at least 'size' bytes.
 *	'size' an integer > 0.
 *	'format' == NULL or points to a NUL terminated string.
 *
 * Ensures:
 *	If result == ISC_R_SUCCESS
 *		'target' will be a NUL terminated string of no more
 *		than 'size' bytes (including NUL).
 *
 *	If result == ISC_R_NOSPACE
 *		'target' is undefined.
 *
 * Returns:
 *	ISC_R_SUCCESS  -- 'format' was successfully printed to 'target'.
 *	ISC_R_NOSPACE  -- 'format' could not be printed to 'target' since it
 *	                  is too small.
 */

void
isc_string_printf_truncate(char *target, size_t size, const char *format, ...)
	ISC_FORMAT_PRINTF(3, 4);
/*
 * Print 'format' to 'target' which is a pointer to a string of at least
 * 'size' bytes.
 *
 * Requires:
 *	'target' is a pointer to a char[] of at least 'size' bytes.
 *	'size' an integer > 0.
 *	'format' == NULL or points to a NUL terminated string.
 *
 * Ensures:
 *	'target' will be a NUL terminated string of no more
 *	than 'size' bytes (including NUL).
 */


char *
isc_string_regiondup(isc_mem_t *mctx, const isc_region_t *source);
/*
 * Copy the region pointed to by r to a NUL terminated string
 * allocated from the memory context pointed to by mctx.
 *
 * The result should be deallocated using isc_mem_free()
 *
 * Requires:
 *	'mctx' is a point to a valid memory context.
 *	'source' is a pointer to a valid region.
 *
 * Returns:
 *	a pointer to a NUL terminated string or
 *	NULL if memory for the copy could not be allocated
 *
 */

int
isc_tsmemcmp(const void *p1, const void *p2, size_t len);
/*
 * Lexicographic compare 'len' unsigned bytes from 'p1' and 'p2'
 * like 'memcmp()'.
 *
 * This function is safe from timing attacks as it has a runtime that
 * only depends on 'len' and has no early-out option.
 *
 * Use this to check MACs and other material that is security sensitive.
 *
 * Returns:
 *  (let x be the byte offset of the first different byte)
 *  -1 if (u_char)p1[x] < (u_char)p2[x]
 *   1 if (u_char)p1[x] > (u_char)p2[x]
 *   0 if byte series are equal
 */

char *
isc_string_separate(char **stringp, const char *delim);

#ifdef ISC_PLATFORM_NEEDSTRSEP
#define strsep isc_string_separate
#endif

#ifdef ISC_PLATFORM_NEEDMEMMOVE
#define memmove(a,b,c) bcopy(b,a,c)
#endif

size_t
isc_string_strlcpy(char *dst, const char *src, size_t size);


#ifdef ISC_PLATFORM_NEEDSTRLCPY
#define strlcpy isc_string_strlcpy
#endif


size_t
isc_string_strlcat(char *dst, const char *src, size_t size);

#ifdef ISC_PLATFORM_NEEDSTRLCAT
#define strlcat isc_string_strlcat
#endif

ISC_LANG_ENDDECLS

#endif /* ISC_STRING_H */
