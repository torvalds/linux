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

#include "libuutil_common.h"

#include <limits.h>
#include <ctype.h>

#define	MAX_BASE	36

#define	IS_DIGIT(x)	((x) >= '0' && (x) <= '9')

#define	CTOI(x) (((x) >= '0' && (x) <= '9') ? (x) - '0' : \
	    ((x) >= 'a' && (x) <= 'z') ? (x) + 10 - 'a' : (x) + 10 - 'A')

static int
strtoint(const char *s_arg, uint64_t *out, uint32_t base, int sign)
{
	const unsigned char *s = (const unsigned char *)s_arg;

	uint64_t val = 0;
	uint64_t multmax;

	unsigned c, i;

	int neg = 0;

	int bad_digit = 0;
	int bad_char = 0;
	int overflow = 0;

	if (s == NULL || base == 1 || base > MAX_BASE) {
		uu_set_error(UU_ERROR_INVALID_ARGUMENT);
		return (-1);
	}

	while ((c = *s) != 0 && isspace(c))
		s++;

	switch (c) {
	case '-':
		if (!sign)
			overflow = 1;		/* becomes underflow below */
		neg = 1;
		/*FALLTHRU*/
	case '+':
		c = *++s;
		break;
	default:
		break;
	}

	if (c == '\0') {
		uu_set_error(UU_ERROR_EMPTY);
		return (-1);
	}

	if (base == 0) {
		if (c != '0')
			base = 10;
		else if (s[1] == 'x' || s[1] == 'X')
			base = 16;
		else
			base = 8;
	}

	if (base == 16 && c == '0' && (s[1] == 'x' || s[1] == 'X'))
		c = *(s += 2);

	if ((val = CTOI(c)) >= base) {
		if (IS_DIGIT(c))
			bad_digit = 1;
		else
			bad_char = 1;
		val = 0;
	}

	multmax = (uint64_t)UINT64_MAX / (uint64_t)base;

	for (c = *++s; c != '\0'; c = *++s) {
		if ((i = CTOI(c)) >= base) {
			if (isspace(c))
				break;
			if (IS_DIGIT(c))
				bad_digit = 1;
			else
				bad_char = 1;
			i = 0;
		}

		if (val > multmax)
			overflow = 1;

		val *= base;
		if ((uint64_t)UINT64_MAX - val < (uint64_t)i)
			overflow = 1;

		val += i;
	}

	while ((c = *s) != 0) {
		if (!isspace(c))
			bad_char = 1;
		s++;
	}

	if (sign) {
		if (neg) {
			if (val > -(uint64_t)INT64_MIN)
				overflow = 1;
		} else {
			if (val > INT64_MAX)
				overflow = 1;
		}
	}

	if (neg)
		val = -val;

	if (bad_char | bad_digit | overflow) {
		if (bad_char)
			uu_set_error(UU_ERROR_INVALID_CHAR);
		else if (bad_digit)
			uu_set_error(UU_ERROR_INVALID_DIGIT);
		else if (overflow) {
			if (neg)
				uu_set_error(UU_ERROR_UNDERFLOW);
			else
				uu_set_error(UU_ERROR_OVERFLOW);
		}
		return (-1);
	}

	*out = val;
	return (0);
}

int
uu_strtoint(const char *s, void *v, size_t sz, int base,
    int64_t min, int64_t max)
{
	uint64_t val_u;
	int64_t val;

	if (min > max)
		goto bad_argument;

	switch (sz) {
	case 1:
		if (max > INT8_MAX || min < INT8_MIN)
			goto bad_argument;
		break;
	case 2:
		if (max > INT16_MAX || min < INT16_MIN)
			goto bad_argument;
		break;
	case 4:
		if (max > INT32_MAX || min < INT32_MIN)
			goto bad_argument;
		break;
	case 8:
		if (max > INT64_MAX || min < INT64_MIN)
			goto bad_argument;
		break;
	default:
		goto bad_argument;
	}

	if (min == 0 && max == 0) {
		min = -(1ULL << (8 * sz - 1));
		max = (1ULL << (8 * sz - 1)) - 1;
	}

	if (strtoint(s, &val_u, base, 1) == -1)
		return (-1);

	val = (int64_t)val_u;

	if (val < min) {
		uu_set_error(UU_ERROR_UNDERFLOW);
		return (-1);
	} else if (val > max) {
		uu_set_error(UU_ERROR_OVERFLOW);
		return (-1);
	}

	switch (sz) {
	case 1:
		*(int8_t *)v = val;
		return (0);
	case 2:
		*(int16_t *)v = val;
		return (0);
	case 4:
		*(int32_t *)v = val;
		return (0);
	case 8:
		*(int64_t *)v = val;
		return (0);
	default:
		break;		/* fall through to bad_argument */
	}

bad_argument:
	uu_set_error(UU_ERROR_INVALID_ARGUMENT);
	return (-1);
}

int
uu_strtouint(const char *s, void *v, size_t sz, int base,
    uint64_t min, uint64_t max)
{
	uint64_t val;

	if (min > max)
		goto bad_argument;

	switch (sz) {
	case 1:
		if (max > UINT8_MAX)
			goto bad_argument;
		break;
	case 2:
		if (max > UINT16_MAX)
			goto bad_argument;
		break;
	case 4:
		if (max > UINT32_MAX)
			goto bad_argument;
		break;
	case 8:
		if (max > UINT64_MAX)
			goto bad_argument;
		break;
	default:
		goto bad_argument;
	}

	if (min == 0 && max == 0) {
		/* we have to be careful, since << can overflow */
		max = (1ULL << (8 * sz - 1)) * 2 - 1;
	}

	if (strtoint(s, &val, base, 0) == -1)
		return (-1);

	if (val < min) {
		uu_set_error(UU_ERROR_UNDERFLOW);
		return (-1);
	} else if (val > max) {
		uu_set_error(UU_ERROR_OVERFLOW);
		return (-1);
	}

	switch (sz) {
	case 1:
		*(uint8_t *)v = val;
		return (0);
	case 2:
		*(uint16_t *)v = val;
		return (0);
	case 4:
		*(uint32_t *)v = val;
		return (0);
	case 8:
		*(uint64_t *)v = val;
		return (0);
	default:
		break;		/* shouldn't happen, fall through */
	}

bad_argument:
	uu_set_error(UU_ERROR_INVALID_ARGUMENT);
	return (-1);
}
