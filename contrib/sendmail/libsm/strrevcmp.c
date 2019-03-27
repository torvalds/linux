/*
 * Copyright (c) 2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: strrevcmp.c,v 1.6 2013-11-22 20:51:43 ca Exp $")

#include <sm/config.h>
#include <sm/string.h>
#include <string.h>

/* strcasecmp.c */
extern const unsigned char charmap[];

/*
**  SM_STRREVCASECMP -- compare two strings starting at the end (ignore case)
**
**	Parameters:
**		s1 -- first string.
**		s2 -- second string.
**
**	Returns:
**		strcasecmp(reverse(s1), reverse(s2))
*/

int
sm_strrevcasecmp(s1, s2)
	const char *s1, *s2;
{
	register int i1, i2;

	i1 = strlen(s1) - 1;
	i2 = strlen(s2) - 1;
	while (i1 >= 0 && i2 >= 0 &&
	       charmap[(unsigned char) s1[i1]] ==
	       charmap[(unsigned char) s2[i2]])
	{
		--i1;
		--i2;
	}
	if (i1 < 0)
	{
		if (i2 < 0)
			return 0;
		else
			return -1;
	}
	else
	{
		if (i2 < 0)
			return 1;
		else
			return (charmap[(unsigned char) s1[i1]] -
				charmap[(unsigned char) s2[i2]]);
	}
}
/*
**  SM_STRREVCMP -- compare two strings starting at the end
**
**	Parameters:
**		s1 -- first string.
**		s2 -- second string.
**
**	Returns:
**		strcmp(reverse(s1), reverse(s2))
*/

int
sm_strrevcmp(s1, s2)
	const char *s1, *s2;
{
	register int i1, i2;

	i1 = strlen(s1) - 1;
	i2 = strlen(s2) - 1;
	while (i1 >= 0 && i2 >= 0 && s1[i1] == s2[i2])
	{
		--i1;
		--i2;
	}
	if (i1 < 0)
	{
		if (i2 < 0)
			return 0;
		else
			return -1;
	}
	else
	{
		if (i2 < 0)
			return 1;
		else
			return s1[i1] - s2[i2];
	}
}
