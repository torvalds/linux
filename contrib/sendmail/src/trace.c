/*
 * Copyright (c) 1998-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1983, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>
#include <sm/debug.h>
#include <sm/string.h>

SM_RCSID("@(#)$Id: trace.c,v 8.39 2013-11-22 20:51:57 ca Exp $")

static char	*tTnewflag __P((char *));
static char	*tToldflag __P((char *));

/*
**  TtSETUP -- set up for trace package.
**
**	Parameters:
**		vect -- pointer to trace vector.
**		size -- number of flags in trace vector.
**		defflags -- flags to set if no value given.
**
**	Returns:
**		none
**
**	Side Effects:
**		environment is set up.
*/

static unsigned char	*tTvect;
static unsigned int	tTsize;
static char	*DefFlags;

void
tTsetup(vect, size, defflags)
	unsigned char *vect;
	unsigned int size;
	char *defflags;
{
	tTvect = vect;
	tTsize = size;
	DefFlags = defflags;
}

/*
**  tToldflag -- process an old style trace flag
**
**	Parameters:
**		s -- points to a [\0, \t] terminated string,
**		     and the initial character is a digit.
**
**	Returns:
**		pointer to terminating [\0, \t] character
**
**	Side Effects:
**		modifies tTvect
*/

static char *
tToldflag(s)
	register char *s;
{
	unsigned int first, last;
	register unsigned int i;

	/* find first flag to set */
	i = 0;
	while (isascii(*s) && isdigit(*s) && i < tTsize)
		i = i * 10 + (*s++ - '0');

	/*
	**  skip over rest of a too large number
	**  Maybe we should complain if out-of-bounds values are used.
	*/

	while (isascii(*s) && isdigit(*s) && i >= tTsize)
		s++;
	first = i;

	/* find last flag to set */
	if (*s == '-')
	{
		i = 0;
		while (isascii(*++s) && isdigit(*s) && i < tTsize)
			i = i * 10 + (*s - '0');

		/* skip over rest of a too large number */
		while (isascii(*s) && isdigit(*s) && i >= tTsize)
			s++;
	}
	last = i;

	/* find the level to set it to */
	i = 1;
	if (*s == '.')
	{
		i = 0;
		while (isascii(*++s) && isdigit(*s))
			i = i * 10 + (*s - '0');
	}

	/* clean up args */
	if (first >= tTsize)
		first = tTsize - 1;
	if (last >= tTsize)
		last = tTsize - 1;

	/* set the flags */
	while (first <= last)
		tTvect[first++] = (unsigned char) i;

	/* skip trailing junk */
	while (*s != '\0' && *s != ',' && *s != ' ' && *s != '\t')
		++s;

	return s;
}

/*
**  tTnewflag -- process a new style trace flag
**
**	Parameters:
**		s -- Points to a non-empty [\0, \t] terminated string,
**		     of which the initial character is not a digit.
**
**	Returns:
**		pointer to terminating [\0, \t] character
**
**	Side Effects:
**		adds trace flag to libsm debug database
*/

static char *
tTnewflag(s)
	register char *s;
{
	char *pat, *endpat;
	int level;

	pat = s;
	while (*s != '\0' && *s != ',' && *s != ' ' && *s != '\t' && *s != '.')
		++s;
	endpat = s;
	if (*s == '.')
	{
		++s;
		level = 0;
		while (isascii(*s) && isdigit(*s))
		{
			level = level * 10 + (*s - '0');
			++s;
		}
		if (level < 0)
			level = 0;
	}
	else
	{
		level = 1;
	}

	sm_debug_addsetting_x(sm_strndup_x(pat, endpat - pat), level);

	/* skip trailing junk */
	while (*s != '\0' && *s != ',' && *s != ' ' && *s != '\t')
		++s;

	return s;
}

/*
**  TtFLAG -- process an external trace flag list.
**
**	Parameters:
**		s -- the trace flag.
**
**		The syntax of a trace flag list is as follows:
**
**		<flags> ::= <flag> | <flags> "," <flag>
**		<flag> ::= <categories> | <categories> "." <level>
**		<categories> ::= <int> | <int> "-" <int> | <pattern>
**		<pattern> ::= <an sh glob pattern matching a C identifier>
**
**		White space is ignored before and after a flag.
**		However, note that we skip over anything we don't
**		understand, rather than report an error.
**
**	Returns:
**		none.
**
**	Side Effects:
**		sets/clears old-style trace flags.
**		registers new-style trace flags with the libsm debug package.
*/

void
tTflag(s)
	register char *s;
{
	if (s == NULL || *s == '\0')
		s = DefFlags;

	for (;;)
	{
		if (*s == '\0')
			return;
		if (*s == ',' || *s == ' ' || *s == '\t')
		{
			++s;
			continue;
		}
		if (isascii(*s) && isdigit(*s))
			s = tToldflag(s);
		else
			s = tTnewflag(s);
	}
}
