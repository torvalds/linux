/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 * Copyright (c) 1992
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_IDSTR(id, "@(#)$Id: strto.c,v 1.19 2013-11-22 20:51:43 ca Exp $")

#include <sys/param.h>
#include <sys/types.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sm/limits.h>
#include <sm/conf.h>
#include <sm/string.h>

/*
**  SM_STRTOLL --  Convert a string to a (signed) long long integer.
**
**  Ignores `locale' stuff.  Assumes that the upper and lower case
**  alphabets and digits are each contiguous.
**
**	Parameters:
**		nptr -- string containing number
**		endptr -- location of first invalid character
**		base -- numeric base that 'nptr' number is based in
**
**	Returns:
**		Failure: on underflow LLONG_MIN is returned; on overflow
**			LLONG_MAX is returned and errno is set.
**			When 'endptr' == '\0' then the entire string 'nptr'
**			was valid.
**		Success: returns the converted number
*/

LONGLONG_T
sm_strtoll(nptr, endptr, base)
	const char *nptr;
	char **endptr;
	register int base;
{
	register bool neg;
	register const char *s;
	register LONGLONG_T acc, cutoff;
	register int c;
	register int any, cutlim;

	/*
	**  Skip white space and pick up leading +/- sign if any.
	**  If base is 0, allow 0x for hex and 0 for octal, else
	**  assume decimal; if base is already 16, allow 0x.
	*/

	s = nptr;
	do
	{
		c = (unsigned char) *s++;
	} while (isascii(c) && isspace(c));
	if (c == '-')
	{
		neg = true;
		c = *s++;
	}
	else
	{
		neg = false;
		if (c == '+')
			c = *s++;
	}
	if ((base == 0 || base == 16) &&
	    c == '0' && (*s == 'x' || *s == 'X'))
	{
		c = s[1];
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = c == '0' ? 8 : 10;

	/*
	**  Compute the cutoff value between legal numbers and illegal
	**  numbers.  That is the largest legal value, divided by the
	**  base.  An input number that is greater than this value, if
	**  followed by a legal input character, is too big.  One that
	**  is equal to this value may be valid or not; the limit
	**  between valid and invalid numbers is then based on the last
	**  digit.  For instance, if the range for long-long's is
	**  [-9223372036854775808..9223372036854775807] and the input base
	**  is 10, cutoff will be set to 922337203685477580 and cutlim to
	**  either 7 (!neg) or 8 (neg), meaning that if we have
	**  accumulated a value > 922337203685477580, or equal but the
	**  next digit is > 7 (or 8), the number is too big, and we will
	**  return a range error.
	**
	**  Set any if any `digits' consumed; make it negative to indicate
	**  overflow.
	*/

	cutoff = neg ? LLONG_MIN : LLONG_MAX;
	cutlim = cutoff % base;
	cutoff /= base;
	if (neg)
	{
		if (cutlim > 0)
		{
			cutlim -= base;
			cutoff += 1;
		}
		cutlim = -cutlim;
	}
	for (acc = 0, any = 0;; c = (unsigned char) *s++)
	{
		if (isascii(c) && isdigit(c))
			c -= '0';
		else if (isascii(c) && isalpha(c))
			c -= isupper(c) ? 'A' - 10 : 'a' - 10;
		else
			break;
		if (c >= base)
			break;
		if (any < 0)
			continue;
		if (neg)
		{
			if (acc < cutoff || (acc == cutoff && c > cutlim))
			{
				any = -1;
				acc = LLONG_MIN;
				errno = ERANGE;
			}
			else
			{
				any = 1;
				acc *= base;
				acc -= c;
			}
		}
		else
		{
			if (acc > cutoff || (acc == cutoff && c > cutlim))
			{
				any = -1;
				acc = LLONG_MAX;
				errno = ERANGE;
			}
			else
			{
				any = 1;
				acc *= base;
				acc += c;
			}
		}
	}
	if (endptr != 0)
		*endptr = (char *) (any ? s - 1 : nptr);
	return acc;
}

/*
**  SM_STRTOULL --  Convert a string to an unsigned long long integer.
**
**  Ignores `locale' stuff.  Assumes that the upper and lower case
**  alphabets and digits are each contiguous.
**
**	Parameters:
**		nptr -- string containing (unsigned) number
**		endptr -- location of first invalid character
**		base -- numeric base that 'nptr' number is based in
**
**	Returns:
**		Failure: on overflow ULLONG_MAX is returned and errno is set.
**			When 'endptr' == '\0' then the entire string 'nptr'
**			was valid.
**		Success: returns the converted number
*/

ULONGLONG_T
sm_strtoull(nptr, endptr, base)
	const char *nptr;
	char **endptr;
	register int base;
{
	register const char *s;
	register ULONGLONG_T acc, cutoff;
	register int c;
	register bool neg;
	register int any, cutlim;

	/* See sm_strtoll for comments as to the logic used. */
	s = nptr;
	do
	{
		c = (unsigned char) *s++;
	} while (isascii(c) && isspace(c));
	neg = (c == '-');
	if (neg)
	{
		c = *s++;
	}
	else
	{
		if (c == '+')
			c = *s++;
	}
	if ((base == 0 || base == 16) &&
	    c == '0' && (*s == 'x' || *s == 'X'))
	{
		c = s[1];
		s += 2;
		base = 16;
	}
	if (base == 0)
		base = c == '0' ? 8 : 10;

	cutoff = ULLONG_MAX / (ULONGLONG_T)base;
	cutlim = ULLONG_MAX % (ULONGLONG_T)base;
	for (acc = 0, any = 0;; c = (unsigned char) *s++)
	{
		if (isascii(c) && isdigit(c))
			c -= '0';
		else if (isascii(c) && isalpha(c))
			c -= isupper(c) ? 'A' - 10 : 'a' - 10;
		else
			break;
		if (c >= base)
			break;
		if (any < 0)
			continue;
		if (acc > cutoff || (acc == cutoff && c > cutlim))
		{
			any = -1;
			acc = ULLONG_MAX;
			errno = ERANGE;
		}
		else
		{
			any = 1;
			acc *= (ULONGLONG_T)base;
			acc += c;
		}
	}
	if (neg && any > 0)
		acc = -((LONGLONG_T) acc);
	if (endptr != 0)
		*endptr = (char *) (any ? s - 1 : nptr);
	return acc;
}
