/*
 * Copyright (c) 1998-2001, 2003, 2006, 2007 Proofpoint, Inc. and its suppliers.
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

SM_RCSID("@(#)$Id: macro.c,v 8.108 2013-11-22 20:51:55 ca Exp $")

#include <sm/sendmail.h>
#if MAXMACROID != (BITMAPBITS - 1)
	ERROR Read the comment in conf.h
#endif /* MAXMACROID != (BITMAPBITS - 1) */

static char	*MacroName[MAXMACROID + 1];	/* macro id to name table */

/*
**  Codes for long named macros.
**  See also macname():
	* if not ASCII printable, look up the name *
	if (n <= 0x20 || n > 0x7f)
**  First use 1 to NEXTMACROID_L, then use NEXTMACROID_H to MAXMACROID.
*/

#define NEXTMACROID_L 037
#define NEXTMACROID_H 0240

#if _FFR_MORE_MACROS
/* table for next id in non-printable ASCII range: disallow some value */
static int NextMIdTable[] =
{
	/*  0  nul */	 1,
	/*  1  soh */	 2,
	/*  2  stx */	 3,
	/*  3  etx */	 4,
	/*  4  eot */	 5,
	/*  5  enq */	 6,
	/*  6  ack */	 7,
	/*  7  bel */	 8,
	/*  8  bs  */	14,
	/*  9  ht  */	-1,
	/* 10  nl  */	-1,
	/* 11  vt  */	-1,
	/* 12  np  */	-1,
	/* 13  cr  */	-1,
	/* 14  so  */	15,
	/* 15  si  */	16,
	/* 16  dle */	17,
	/* 17  dc1 */	18,
	/* 18  dc2 */	19,
	/* 19  dc3 */	20,
	/* 20  dc4 */	21,
	/* 21  nak */	22,
	/* 22  syn */	23,
	/* 23  etb */	24,
	/* 24  can */	25,
	/* 25  em  */	26,
	/* 26  sub */	27,
	/* 27  esc */	28,
	/* 28  fs  */	29,
	/* 29  gs  */	30,
	/* 30  rs  */	31,
	/* 31  us  */	32,
	/* 32  sp  */	-1,
};

#define NEXTMACROID(mid)	(		\
	(mid < NEXTMACROID_L) ? (NextMIdTable[mid]) :	\
	((mid < NEXTMACROID_H) ? NEXTMACROID_H : (mid + 1)))

int		NextMacroId = 1;	/* codes for long named macros */
/* see sendmail.h: Special characters in rewriting rules. */
#else /* _FFR_MORE_MACROS */
int		NextMacroId = 0240;	/* codes for long named macros */
#define NEXTMACROID(mid)	((mid) + 1)
#endif /* _FFR_MORE_MACROS */


/*
**  INITMACROS -- initialize the macro system
**
**	This just involves defining some macros that are actually
**	used internally as metasymbols to be themselves.
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		initializes several macros to be themselves.
*/

struct metamac	MetaMacros[] =
{
	/* LHS pattern matching characters */
	{ '*', MATCHZANY },	{ '+', MATCHANY },	{ '-', MATCHONE },
	{ '=', MATCHCLASS },	{ '~', MATCHNCLASS },

	/* these are RHS metasymbols */
	{ '#', CANONNET },	{ '@', CANONHOST },	{ ':', CANONUSER },
	{ '>', CALLSUBR },

	/* the conditional operations */
	{ '?', CONDIF },	{ '|', CONDELSE },	{ '.', CONDFI },

	/* the hostname lookup characters */
	{ '[', HOSTBEGIN },	{ ']', HOSTEND },
	{ '(', LOOKUPBEGIN },	{ ')', LOOKUPEND },

	/* miscellaneous control characters */
	{ '&', MACRODEXPAND },

	{ '\0', '\0' }
};

#define MACBINDING(name, mid) \
		stab(name, ST_MACRO, ST_ENTER)->s_macro = mid; \
		MacroName[mid] = name;

void
initmacros(e)
	ENVELOPE *e;
{
	struct metamac *m;
	int c;
	char buf[5];

	for (m = MetaMacros; m->metaname != '\0'; m++)
	{
		buf[0] = m->metaval;
		buf[1] = '\0';
		macdefine(&e->e_macro, A_TEMP, m->metaname, buf);
	}
	buf[0] = MATCHREPL;
	buf[2] = '\0';
	for (c = '0'; c <= '9'; c++)
	{
		buf[1] = c;
		macdefine(&e->e_macro, A_TEMP, c, buf);
	}

	/* set defaults for some macros sendmail will use later */
	macdefine(&e->e_macro, A_PERM, 'n', "MAILER-DAEMON");

	/* set up external names for some internal macros */
	MACBINDING("opMode", MID_OPMODE);
	/*XXX should probably add equivalents for all short macros here XXX*/
}

/*
**  EXPAND/DOEXPAND -- macro expand a string using $x escapes.
**
**	After expansion, the expansion will be in external form (that is,
**	there will be no sendmail metacharacters and METAQUOTEs will have
**	been stripped out).
**
**	Parameters:
**		s -- the string to expand.
**		buf -- the place to put the expansion.
**		bufsize -- the size of the buffer.
**		explevel -- the depth of expansion (doexpand only)
**		e -- envelope in which to work.
**
**	Returns:
**		none.
**
**	Side Effects:
**		none.
*/

static void doexpand __P(( char *, char *, size_t, int, ENVELOPE *));

static void
doexpand(s, buf, bufsize, explevel, e)
	char *s;
	char *buf;
	size_t bufsize;
	int explevel;
	ENVELOPE *e;
{
	char *xp;
	char *q;
	bool skipping;		/* set if conditionally skipping output */
	bool recurse;		/* set if recursion required */
	size_t i;
	int skiplev;		/* skipping nesting level */
	int iflev;		/* if nesting level */
	bool quotenext;		/* quote the following character */
	char xbuf[MACBUFSIZE];

	if (tTd(35, 24))
	{
		sm_dprintf("expand(");
		xputs(sm_debug_file(), s);
		sm_dprintf(")\n");
	}

	recurse = false;
	skipping = false;
	skiplev = 0;
	iflev = 0;
	quotenext = false;
	if (s == NULL)
		s = "";
	for (xp = xbuf; *s != '\0'; s++)
	{
		int c;

		/*
		**  Check for non-ordinary (special?) character.
		**	'q' will be the interpolated quantity.
		*/

		q = NULL;
		c = *s & 0377;

		if (quotenext)
		{
			quotenext = false;
			goto simpleinterpolate;
		}

		switch (c)
		{
		  case CONDIF:		/* see if var set */
			iflev++;
			c = *++s & 0377;
			if (skipping)
				skiplev++;
			else
			{
				char *mv;

				mv = macvalue(c, e);
				skipping = (mv == NULL || *mv == '\0');
			}
			continue;

		  case CONDELSE:	/* change state of skipping */
			if (iflev == 0)
				break;	/* XXX: error */
			if (skiplev == 0)
				skipping = !skipping;
			continue;

		  case CONDFI:		/* stop skipping */
			if (iflev == 0)
				break;	/* XXX: error */
			iflev--;
			if (skiplev == 0)
				skipping = false;
			if (skipping)
				skiplev--;
			continue;

		  case MACROEXPAND:	/* macro interpolation */
			c = bitidx(*++s);
			if (c != '\0')
				q = macvalue(c, e);
			else
			{
				s--;
				q = NULL;
			}
			if (q == NULL)
				continue;
			break;

		  case METAQUOTE:
			/* next octet completely quoted */
			quotenext = true;
			break;
		}

		/*
		**  Interpolate q or output one character
		*/

  simpleinterpolate:
		if (skipping || xp >= &xbuf[sizeof(xbuf) - 1])
			continue;
		if (q == NULL)
			*xp++ = c;
		else
		{
			/* copy to end of q or max space remaining in buf */
			bool hiderecurse = false;

			while ((c = *q++) != '\0' &&
				xp < &xbuf[sizeof(xbuf) - 1])
			{
				/* check for any sendmail metacharacters */
				if (!hiderecurse && (c & 0340) == 0200)
					recurse = true;
				*xp++ = c;

				/* give quoted characters a free ride */
				hiderecurse = (c & 0377) == METAQUOTE;
			}
		}
	}
	*xp = '\0';

	if (tTd(35, 28))
	{
		sm_dprintf("expand(%d) ==> ", explevel);
		xputs(sm_debug_file(), xbuf);
		sm_dprintf("\n");
	}

	/* recurse as appropriate */
	if (recurse)
	{
		if (explevel < MaxMacroRecursion)
		{
			doexpand(xbuf, buf, bufsize, explevel + 1, e);
			return;
		}
		syserr("expand: recursion too deep (%d max)",
			MaxMacroRecursion);
	}

	/* copy results out */
	if (explevel == 0)
		(void) sm_strlcpy(buf, xbuf, bufsize);
	else
	{
		/* leave in internal form */
		i = xp - xbuf;
		if (i >= bufsize)
			i = bufsize - 1;
		memmove(buf, xbuf, i);
		buf[i] = '\0';
	}

	if (tTd(35, 24))
	{
		sm_dprintf("expand ==> ");
		xputs(sm_debug_file(), buf);
		sm_dprintf("\n");
	}
}

void
expand(s, buf, bufsize, e)
	char *s;
	char *buf;
	size_t bufsize;
	ENVELOPE *e;
{
	doexpand(s, buf, bufsize, 0, e);
}

/*
**  MACDEFINE -- bind a macro name to a value
**
**	Set a macro to a value, with fancy storage management.
**	macdefine will make a copy of the value, if required,
**	and will ensure that the storage for the previous value
**	is not leaked.
**
**	Parameters:
**		mac -- Macro table.
**		vclass -- storage class of 'value', ignored if value==NULL.
**			A_HEAP	means that the value was allocated by
**				malloc, and that macdefine owns the storage.
**			A_TEMP	means that value points to temporary storage,
**				and thus macdefine needs to make a copy.
**			A_PERM	means that value points to storage that
**				will remain allocated and unchanged for
**				at least the lifetime of mac.  Use A_PERM if:
**				-- value == NULL,
**				-- value points to a string literal,
**				-- value was allocated from mac->mac_rpool
**				   or (in the case of an envelope macro)
**				   from e->e_rpool,
**				-- in the case of an envelope macro,
**				   value is a string member of the envelope
**				   such as e->e_sender.
**		id -- Macro id.  This is a single character macro name
**			such as 'g', or a value returned by macid().
**		value -- Macro value: either NULL, or a string.
*/

void
#if SM_HEAP_CHECK
macdefine_tagged(mac, vclass, id, value, file, line, grp)
#else /* SM_HEAP_CHECK */
macdefine(mac, vclass, id, value)
#endif /* SM_HEAP_CHECK */
	MACROS_T *mac;
	ARGCLASS_T vclass;
	int id;
	char *value;
#if SM_HEAP_CHECK
	char *file;
	int line;
	int grp;
#endif /* SM_HEAP_CHECK */
{
	char *newvalue;

	if (id < 0 || id > MAXMACROID)
		return;

	if (tTd(35, 9))
	{
		sm_dprintf("%sdefine(%s as ",
			mac->mac_table[id] == NULL ? "" : "re", macname(id));
		xputs(sm_debug_file(), value);
		sm_dprintf(")\n");
	}

	if (mac->mac_rpool == NULL)
	{
		char *freeit = NULL;

		if (mac->mac_table[id] != NULL &&
		    bitnset(id, mac->mac_allocated))
			freeit = mac->mac_table[id];

		if (value == NULL || vclass == A_HEAP)
		{
			sm_heap_checkptr_tagged(value, file, line);
			newvalue = value;
			clrbitn(id, mac->mac_allocated);
		}
		else
		{
#if SM_HEAP_CHECK
			newvalue = sm_strdup_tagged_x(value, file, line, 0);
#else /* SM_HEAP_CHECK */
			newvalue = sm_strdup_x(value);
#endif /* SM_HEAP_CHECK */
			setbitn(id, mac->mac_allocated);
		}
		mac->mac_table[id] = newvalue;
		if (freeit != NULL)
			sm_free(freeit);
	}
	else
	{
		if (value == NULL || vclass == A_PERM)
			newvalue = value;
		else
			newvalue = sm_rpool_strdup_x(mac->mac_rpool, value);
		mac->mac_table[id] = newvalue;
		if (vclass == A_HEAP)
			sm_free(value);
	}

#if _FFR_RESET_MACRO_GLOBALS
	switch (id)
	{
	  case 'j':
		PSTRSET(MyHostName, value);
		break;
	}
#endif /* _FFR_RESET_MACRO_GLOBALS */
}

/*
**  MACSET -- set a named macro to a value (low level)
**
**	No fancy storage management; the caller takes full responsibility.
**	Often used with macget; see also macdefine.
**
**	Parameters:
**		mac -- Macro table.
**		i -- Macro name, specified as an integer offset.
**		value -- Macro value: either NULL, or a string.
*/

void
macset(mac, i, value)
	MACROS_T *mac;
	int i;
	char *value;
{
	if (i < 0 || i > MAXMACROID)
		return;

	if (tTd(35, 9))
	{
		sm_dprintf("macset(%s as ", macname(i));
		xputs(sm_debug_file(), value);
		sm_dprintf(")\n");
	}
	mac->mac_table[i] = value;
}

/*
**  MACVALUE -- return uninterpreted value of a macro.
**
**	Does fancy path searching.
**	The low level counterpart is macget.
**
**	Parameters:
**		n -- the name of the macro.
**		e -- envelope in which to start looking for the macro.
**
**	Returns:
**		The value of n.
**
**	Side Effects:
**		none.
*/

char *
macvalue(n, e)
	int n;
	ENVELOPE *e;
{
	n = bitidx(n);
	if (e != NULL && e->e_mci != NULL)
	{
		char *p = e->e_mci->mci_macro.mac_table[n];

		if (p != NULL)
			return p;
	}
	while (e != NULL)
	{
		char *p = e->e_macro.mac_table[n];

		if (p != NULL)
			return p;
		if (e == e->e_parent)
			break;
		e = e->e_parent;
	}
	return GlobalMacros.mac_table[n];
}

/*
**  MACNAME -- return the name of a macro given its internal id
**
**	Parameter:
**		n -- the id of the macro
**
**	Returns:
**		The name of n.
**
**	Side Effects:
**		none.
**
**	WARNING:
**		Not thread-safe.
*/

char *
macname(n)
	int n;
{
	static char mbuf[2];

	n = (int)(unsigned char)n;
	if (n > MAXMACROID)
		return "***OUT OF RANGE MACRO***";

	/* if not ASCII printable, look up the name */
	if (n <= 0x20 || n > 0x7f)
	{
		char *p = MacroName[n];

		if (p != NULL)
			return p;
		return "***UNDEFINED MACRO***";
	}

	/* if in the ASCII graphic range, just return the id directly */
	mbuf[0] = n;
	mbuf[1] = '\0';
	return mbuf;
}

/*
**  MACID_PARSE -- return id of macro identified by its name
**
**	Parameters:
**		p -- pointer to name string -- either a single
**			character or {name}.
**		ep -- filled in with the pointer to the byte
**			after the name.
**
**	Returns:
**		0 -- An error was detected.
**		1..MAXMACROID -- The internal id code for this macro.
**
**	Side Effects:
**		If this is a new macro name, a new id is allocated.
**		On error, syserr is called.
*/

int
macid_parse(p, ep)
	char *p;
	char **ep;
{
	int mid;
	char *bp;
	char mbuf[MAXMACNAMELEN + 1];

	if (tTd(35, 14))
	{
		sm_dprintf("macid(");
		xputs(sm_debug_file(), p);
		sm_dprintf(") => ");
	}

	if (*p == '\0' || (p[0] == '{' && p[1] == '}'))
	{
		syserr("Name required for macro/class");
		if (ep != NULL)
			*ep = p;
		if (tTd(35, 14))
			sm_dprintf("NULL\n");
		return 0;
	}
	if (*p != '{')
	{
		/* the macro is its own code */
		if (ep != NULL)
			*ep = p + 1;
		if (tTd(35, 14))
		{
			char buf[2];

			buf[0] = *p;
			buf[1] = '\0';
			xputs(sm_debug_file(), buf);
			sm_dprintf("\n");
		}
		return bitidx(*p);
	}
	bp = mbuf;
	while (*++p != '\0' && *p != '}' && bp < &mbuf[sizeof(mbuf) - 1])
	{
		if (isascii(*p) && (isalnum(*p) || *p == '_'))
			*bp++ = *p;
		else
			syserr("Invalid macro/class character %c", *p);
	}
	*bp = '\0';
	mid = -1;
	if (*p == '\0')
	{
		syserr("Unbalanced { on %s", mbuf);	/* missing } */
	}
	else if (*p != '}')
	{
		syserr("Macro/class name ({%s}) too long (%d chars max)",
			mbuf, (int) (sizeof(mbuf) - 1));
	}
	else if (mbuf[1] == '\0' && mbuf[0] >= 0x20)
	{
		/* ${x} == $x */
		mid = bitidx(mbuf[0]);
		p++;
	}
	else
	{
		STAB *s;

		s = stab(mbuf, ST_MACRO, ST_ENTER);
		if (s->s_macro != 0)
			mid = s->s_macro;
		else
		{
			if (NextMacroId > MAXMACROID)
			{
				syserr("Macro/class {%s}: too many long names",
					mbuf);
				s->s_macro = -1;
			}
			else
			{
				MacroName[NextMacroId] = s->s_name;
				s->s_macro = mid = NextMacroId;
				NextMacroId = NEXTMACROID(NextMacroId);
			}
		}
		p++;
	}
	if (ep != NULL)
		*ep = p;
	if (mid < 0 || mid > MAXMACROID)
	{
		syserr("Unable to assign macro/class ID (mid = 0x%x)", mid);
		if (tTd(35, 14))
			sm_dprintf("NULL\n");
		return 0;
	}
	if (tTd(35, 14))
		sm_dprintf("0x%x\n", mid);
	return mid;
}

/*
**  WORDINCLASS -- tell if a word is in a specific class
**
**	Parameters:
**		str -- the name of the word to look up.
**		cl -- the class name.
**
**	Returns:
**		true if str can be found in cl.
**		false otherwise.
*/

bool
wordinclass(str, cl)
	char *str;
	int cl;
{
	STAB *s;

	s = stab(str, ST_CLASS, ST_FIND);
	return s != NULL && bitnset(bitidx(cl), s->s_class);
}
