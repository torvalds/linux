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

SM_RCSID("@(#)$Id: convtime.c,v 8.40 2013-11-22 20:51:55 ca Exp $")

/*
**  CONVTIME -- convert time
**
**	Takes a time as an ascii string with a trailing character
**	giving units:
**	  s -- seconds
**	  m -- minutes
**	  h -- hours
**	  d -- days (default)
**	  w -- weeks
**	For example, "3d12h" is three and a half days.
**
**	Parameters:
**		p -- pointer to ascii time.
**		units -- default units if none specified.
**
**	Returns:
**		time in seconds.
**
**	Side Effects:
**		none.
*/

time_t
convtime(p, units)
	char *p;
	int units;
{
	register time_t t, r;
	register char c;
	bool pos = true;

	r = 0;
	if (sm_strcasecmp(p, "now") == 0)
		return NOW;
	if (*p == '-')
	{
		pos = false;
		++p;
	}
	while (*p != '\0')
	{
		t = 0;
		while ((c = *p++) != '\0' && isascii(c) && isdigit(c))
			t = t * 10 + (c - '0');
		if (c == '\0')
		{
			c = units;
			p--;
		}
		else if (strchr("wdhms", c) == NULL)
		{
			usrerr("Invalid time unit `%c'", c);
			c = units;
		}
		switch (c)
		{
		  case 'w':		/* weeks */
			t *= 7;
			/* FALLTHROUGH */

		  case 'd':		/* days */
			/* FALLTHROUGH */
		  default:
			t *= 24;
			/* FALLTHROUGH */

		  case 'h':		/* hours */
			t *= 60;
			/* FALLTHROUGH */

		  case 'm':		/* minutes */
			t *= 60;
			/* FALLTHROUGH */

		  case 's':		/* seconds */
			break;
		}
		r += t;
	}

	return pos ? r : -r;
}
/*
**  PINTVL -- produce printable version of a time interval
**
**	Parameters:
**		intvl -- the interval to be converted
**		brief -- if true, print this in an extremely compact form
**			(basically used for logging).
**
**	Returns:
**		A pointer to a string version of intvl suitable for
**			printing or framing.
**
**	Side Effects:
**		none.
**
**	Warning:
**		The string returned is in a static buffer.
*/

#define PLURAL(n)	((n) == 1 ? "" : "s")

char *
pintvl(intvl, brief)
	time_t intvl;
	bool brief;
{
	static char buf[256];
	register char *p;
	int wk, dy, hr, mi, se;

	if (intvl == 0 && !brief)
		return "zero seconds";
	if (intvl == NOW)
		return "too long";

	/* decode the interval into weeks, days, hours, minutes, seconds */
	se = intvl % 60;
	intvl /= 60;
	mi = intvl % 60;
	intvl /= 60;
	hr = intvl % 24;
	intvl /= 24;
	if (brief)
	{
		dy = intvl;
		wk = 0;
	}
	else
	{
		dy = intvl % 7;
		intvl /= 7;
		wk = intvl;
	}

	/* now turn it into a sexy form */
	p = buf;
	if (brief)
	{
		if (dy > 0)
		{
			(void) sm_snprintf(p, SPACELEFT(buf, p), "%d+", dy);
			p += strlen(p);
		}
		(void) sm_snprintf(p, SPACELEFT(buf, p), "%02d:%02d:%02d",
				   hr, mi, se);
		return buf;
	}

	/* use the verbose form */
	if (wk > 0)
	{
		(void) sm_snprintf(p, SPACELEFT(buf, p), ", %d week%s", wk,
				   PLURAL(wk));
		p += strlen(p);
	}
	if (dy > 0)
	{
		(void) sm_snprintf(p, SPACELEFT(buf, p), ", %d day%s", dy,
				   PLURAL(dy));
		p += strlen(p);
	}
	if (hr > 0)
	{
		(void) sm_snprintf(p, SPACELEFT(buf, p), ", %d hour%s", hr,
				   PLURAL(hr));
		p += strlen(p);
	}
	if (mi > 0)
	{
		(void) sm_snprintf(p, SPACELEFT(buf, p), ", %d minute%s", mi,
				   PLURAL(mi));
		p += strlen(p);
	}
	if (se > 0)
	{
		(void) sm_snprintf(p, SPACELEFT(buf, p), ", %d second%s", se,
				   PLURAL(se));
		p += strlen(p);
	}

	return (buf + 2);
}
