/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: match.c,v 1.11 2013-11-22 20:51:43 ca Exp $")

#include <sm/string.h>

/*
**  SM_MATCH -- Match a character string against a glob pattern.
**
**	Parameters:
**		str -- string.
**		par -- pattern to find in str.
**
**	Returns:
**		true on match, false on non-match.
**
**  A pattern consists of normal characters, which match themselves,
**  and meta-sequences.  A * matches any sequence of characters.
**  A ? matches any single character.  A [ introduces a character class.
**  A ] marks the end of a character class; if the ] is missing then
**  the [ matches itself rather than introducing a character class.
**  A character class matches any of the characters between the brackets.
**  The range of characters from X to Y inclusive is written X-Y.
**  If the first character after the [ is ! then the character class is
**  complemented.
**
**  To include a ] in a character class, make it the first character
**  listed (after the !, if any).  To include a -, make it the first
**  character listed (after the !, if any) or the last character.
**  It is impossible for a ] to be the final character in a range.
**  For glob patterns that literally match "*", "?" or "[",
**  use [*], [?] or [[].
*/

bool
sm_match(str, pat)
	const char *str;
	const char *pat;
{
	bool ccnot, ccmatch, ccfirst;
	const char *ccstart;
	char c, c2;

	for (;;)
	{
		switch (*pat)
		{
		  case '\0':
			return *str == '\0';
		  case '?':
			if (*str == '\0')
				return false;
			++pat;
			++str;
			continue;
		  case '*':
			++pat;
			if (*pat == '\0')
			{
				/* optimize case of trailing '*' */
				return true;
			}
			for (;;)
			{
				if (sm_match(pat, str))
					return true;
				if (*str == '\0')
					return false;
				++str;
			}
			/* NOTREACHED */
		  case '[':
			ccstart = pat++;
			ccnot = false;
			if (*pat == '!')
			{
				ccnot = true;
				++pat;
			}
			ccmatch = false;
			ccfirst = true;
			for (;;)
			{
				if (*pat == '\0')
				{
					pat = ccstart;
					goto defl;
				}
				if (*pat == ']' && !ccfirst)
					break;
				c = *pat++;
				ccfirst = false;
				if (*pat == '-' && pat[1] != ']')
				{
					++pat;
					if (*pat == '\0')
					{
						pat = ccstart;
						goto defl;
					}
					c2 = *pat++;
					if (*str >= c && *str <= c2)
						ccmatch = true;
				}
				else
				{
					if (*str == c)
						ccmatch = true;
				}
			}
			if (ccmatch ^ ccnot)
			{
				++pat;
				++str;
			}
			else
				return false;
			continue;
		default:
		defl:
			if (*pat != *str)
				return false;
			++pat;
			++str;
			continue;
		}
	}
}
