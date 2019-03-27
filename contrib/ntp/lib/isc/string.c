/*
 * Copyright (C) 2004-2007, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
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

/* $Id$ */

/*! \file */

#include <config.h>

#include <ctype.h>

#include <isc/mem.h>
#include <isc/print.h>
#include <isc/region.h>
#include <isc/string.h>
#include <isc/util.h>

static char digits[] = "0123456789abcdefghijklmnoprstuvwxyz";

isc_uint64_t
isc_string_touint64(char *source, char **end, int base) {
	isc_uint64_t tmp;
	isc_uint64_t overflow;
	char *s = source;
	char *o;
	char c;

	if ((base < 0) || (base == 1) || (base > 36)) {
		*end = source;
		return (0);
	}

	while (*s != 0 && isascii(*s&0xff) && isspace(*s&0xff))
		s++;
	if (*s == '+' /* || *s == '-' */)
		s++;
	if (base == 0) {
		if (*s == '0' && (*(s+1) == 'X' || *(s+1) == 'x')) {
			s += 2;
			base = 16;
		} else if (*s == '0')
			base = 8;
		else
			base = 10;
	}
	if (*s == 0) {
		*end = source;
		return (0);
	}
	overflow = ~0;
	overflow /= base;
	tmp = 0;

	while ((c = *s) != 0) {
		c = tolower(c&0xff);
		/* end ? */
		if ((o = strchr(digits, c)) == NULL) {
			*end = s;
			return (tmp);
		}
		/* end ? */
		if ((o - digits) >= base) {
			*end = s;
			return (tmp);
		}
		/* overflow ? */
		if (tmp > overflow) {
			*end = source;
			return (0);
		}
		tmp *= base;
		/* overflow ? */
		if ((tmp + (o - digits)) < tmp) {
			*end = source;
			return (0);
		}
		tmp += o - digits;
		s++;
	}
	*end = s;
	return (tmp);
}

isc_result_t
isc_string_copy(char *target, size_t size, const char *source) {
	REQUIRE(size > 0U);

	if (strlcpy(target, source, size) >= size) {
		memset(target, ISC_STRING_MAGIC, size);
		return (ISC_R_NOSPACE);
	}

	ENSURE(strlen(target) < size);

	return (ISC_R_SUCCESS);
}

void
isc_string_copy_truncate(char *target, size_t size, const char *source) {
	REQUIRE(size > 0U);

	strlcpy(target, source, size);

	ENSURE(strlen(target) < size);
}

isc_result_t
isc_string_append(char *target, size_t size, const char *source) {
	REQUIRE(size > 0U);
	REQUIRE(strlen(target) < size);

	if (strlcat(target, source, size) >= size) {
		memset(target, ISC_STRING_MAGIC, size);
		return (ISC_R_NOSPACE);
	}

	ENSURE(strlen(target) < size);

	return (ISC_R_SUCCESS);
}

void
isc_string_append_truncate(char *target, size_t size, const char *source) {
	REQUIRE(size > 0U);
	REQUIRE(strlen(target) < size);

	strlcat(target, source, size);

	ENSURE(strlen(target) < size);
}

isc_result_t
isc_string_printf(char *target, size_t size, const char *format, ...) {
	va_list args;
	size_t n;

	REQUIRE(size > 0U);

	va_start(args, format);
	n = vsnprintf(target, size, format, args);
	va_end(args);

	if (n >= size) {
		memset(target, ISC_STRING_MAGIC, size);
		return (ISC_R_NOSPACE);
	}

	ENSURE(strlen(target) < size);

	return (ISC_R_SUCCESS);
}

void
isc_string_printf_truncate(char *target, size_t size, const char *format, ...)
{
	va_list args;

	REQUIRE(size > 0U);

	va_start(args, format);
	/* check return code? */
	(void)vsnprintf(target, size, format, args);
	va_end(args);

	ENSURE(strlen(target) < size);
}

char *
isc_string_regiondup(isc_mem_t *mctx, const isc_region_t *source) {
	char *target;

	REQUIRE(mctx != NULL);
	REQUIRE(source != NULL);

	target = (char *) isc_mem_allocate(mctx, source->length + 1);
	if (target != NULL) {
		memcpy(source->base, target, source->length);
		target[source->length] = '\0';
	}

	return (target);
}

char *
isc_string_separate(char **stringp, const char *delim) {
	char *string = *stringp;
	char *s;
	const char *d;
	char sc, dc;

	if (string == NULL)
		return (NULL);

	for (s = string; (sc = *s) != '\0'; s++)
		for (d = delim; (dc = *d) != '\0'; d++)
			if (sc == dc) {
				*s++ = '\0';
				*stringp = s;
				return (string);
			}
	*stringp = NULL;
	return (string);
}

size_t
isc_string_strlcpy(char *dst, const char *src, size_t size)
{
	char *d = dst;
	const char *s = src;
	size_t n = size;

	/* Copy as many bytes as will fit */
	if (n != 0U && --n != 0U) {
		do {
			if ((*d++ = *s++) == 0)
				break;
		} while (--n != 0U);
	}

	/* Not enough room in dst, add NUL and traverse rest of src */
	if (n == 0U) {
		if (size != 0U)
			*d = '\0';		/* NUL-terminate dst */
		while (*s++)
			;
	}

	return(s - src - 1);	/* count does not include NUL */
}

size_t
isc_string_strlcat(char *dst, const char *src, size_t size)
{
	char *d = dst;
	const char *s = src;
	size_t n = size;
	size_t dlen;

	/* Find the end of dst and adjust bytes left but don't go past end */
	while (n-- != 0U && *d != '\0')
		d++;
	dlen = d - dst;
	n = size - dlen;

	if (n == 0U)
		return(dlen + strlen(s));
	while (*s != '\0') {
		if (n != 1U) {
			*d++ = *s;
			n--;
		}
		s++;
	}
	*d = '\0';

	return(dlen + (s - src));	/* count does not include NUL */
}
