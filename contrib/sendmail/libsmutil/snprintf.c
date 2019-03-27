/*
 * Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>

SM_RCSID("@(#)$Id: snprintf.c,v 8.45 2013-11-22 20:51:50 ca Exp $")

/*
**  SHORTENSTRING -- return short version of a string
**
**	If the string is already short, just return it.  If it is too
**	long, return the head and tail of the string.
**
**	Parameters:
**		s -- the string to shorten.
**		m -- the max length of the string (strlen()).
**
**	Returns:
**		Either s or a short version of s.
*/

char *
shortenstring(s, m)
	register const char *s;
	size_t m;
{
	size_t l;
	static char buf[MAXSHORTSTR + 1];

	l = strlen(s);
	if (l < m)
		return (char *) s;
	if (m > MAXSHORTSTR)
		m = MAXSHORTSTR;
	else if (m < 10)
	{
		if (m < 5)
		{
			(void) sm_strlcpy(buf, s, m + 1);
			return buf;
		}
		(void) sm_strlcpy(buf, s, m - 2);
		(void) sm_strlcat(buf, "...", sizeof buf);
		return buf;
	}
	m = (m - 3) / 2;
	(void) sm_strlcpy(buf, s, m + 1);
	(void) sm_strlcat2(buf, "...", s + l - m, sizeof buf);
	return buf;
}
