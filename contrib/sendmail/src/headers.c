/*
 * Copyright (c) 1998-2004, 2006, 2007 Proofpoint, Inc. and its suppliers.
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
#include <sm/sendmail.h>

SM_RCSID("@(#)$Id: headers.c,v 8.320 2013-11-22 20:51:55 ca Exp $")

static HDR	*allocheader __P((char *, char *, int, SM_RPOOL_T *, bool));
static size_t	fix_mime_header __P((HDR *, ENVELOPE *));
static int	priencode __P((char *));
static bool	put_vanilla_header __P((HDR *, char *, MCI *));

/*
**  SETUPHEADERS -- initialize headers in symbol table
**
**	Parameters:
**		none
**
**	Returns:
**		none
*/

void
setupheaders()
{
	struct hdrinfo *hi;
	STAB *s;

	for (hi = HdrInfo; hi->hi_field != NULL; hi++)
	{
		s = stab(hi->hi_field, ST_HEADER, ST_ENTER);
		s->s_header.hi_flags = hi->hi_flags;
		s->s_header.hi_ruleset = NULL;
	}
}

/*
**  DOCHOMPHEADER -- process and save a header line.
**
**	Called by chompheader.
**
**	Parameters:
**		line -- header as a text line.
**		pflag -- flags for chompheader() (from sendmail.h)
**		hdrp -- a pointer to the place to save the header.
**		e -- the envelope including this header.
**
**	Returns:
**		flags for this header.
**
**	Side Effects:
**		The header is saved on the header list.
**		Contents of 'line' are destroyed.
*/

static struct hdrinfo	NormalHeader =	{ NULL, 0, NULL };
static unsigned long	dochompheader __P((char *, int, HDR **, ENVELOPE *));

static unsigned long
dochompheader(line, pflag, hdrp, e)
	char *line;
	int pflag;
	HDR **hdrp;
	ENVELOPE *e;
{
	unsigned char mid = '\0';
	register char *p;
	register HDR *h;
	HDR **hp;
	char *fname;
	char *fvalue;
	bool cond = false;
	bool dropfrom;
	bool headeronly;
	STAB *s;
	struct hdrinfo *hi;
	bool nullheader = false;
	BITMAP256 mopts;

	headeronly = hdrp != NULL;
	if (!headeronly)
		hdrp = &e->e_header;

	/* strip off options */
	clrbitmap(mopts);
	p = line;
	if (!bitset(pflag, CHHDR_USER) && *p == '?')
	{
		int c;
		register char *q;

		q = strchr(++p, '?');
		if (q == NULL)
			goto hse;

		*q = '\0';
		c = *p & 0377;

		/* possibly macro conditional */
		if (c == MACROEXPAND)
		{
			/* catch ?$? */
			if (*++p == '\0')
			{
				*q = '?';
				goto hse;
			}

			mid = (unsigned char) *p++;

			/* catch ?$abc? */
			if (*p != '\0')
			{
				*q = '?';
				goto hse;
			}
		}
		else if (*p == '$')
		{
			/* catch ?$? */
			if (*++p == '\0')
			{
				*q = '?';
				goto hse;
			}

			mid = (unsigned char) macid(p);
			if (bitset(0200, mid))
			{
				p += strlen(macname(mid)) + 2;
				SM_ASSERT(p <= q);
			}
			else
				p++;

			/* catch ?$abc? */
			if (*p != '\0')
			{
				*q = '?';
				goto hse;
			}
		}
		else
		{
			while (*p != '\0')
			{
				if (!isascii(*p))
				{
					*q = '?';
					goto hse;
				}

				setbitn(bitidx(*p), mopts);
				cond = true;
				p++;
			}
		}
		p = q + 1;
	}

	/* find canonical name */
	fname = p;
	while (isascii(*p) && isgraph(*p) && *p != ':')
		p++;
	fvalue = p;
	while (isascii(*p) && isspace(*p))
		p++;
	if (*p++ != ':' || fname == fvalue)
	{
hse:
		syserr("553 5.3.0 header syntax error, line \"%s\"", line);
		return 0;
	}
	*fvalue = '\0';
	fvalue = p;

	/* if the field is null, go ahead and use the default */
	while (isascii(*p) && isspace(*p))
		p++;
	if (*p == '\0')
		nullheader = true;

	/* security scan: long field names are end-of-header */
	if (strlen(fname) > 100)
		return H_EOH;

	/* check to see if it represents a ruleset call */
	if (bitset(pflag, CHHDR_DEF))
	{
		char hbuf[50];

		(void) expand(fvalue, hbuf, sizeof(hbuf), e);
		for (p = hbuf; isascii(*p) && isspace(*p); )
			p++;
		if ((*p++ & 0377) == CALLSUBR)
		{
			auto char *endp;
			bool strc;

			strc = *p == '+';	/* strip comments? */
			if (strc)
				++p;
			if (strtorwset(p, &endp, ST_ENTER) > 0)
			{
				*endp = '\0';
				s = stab(fname, ST_HEADER, ST_ENTER);
				if (LogLevel > 9 &&
				    s->s_header.hi_ruleset != NULL)
					sm_syslog(LOG_WARNING, NOQID,
						  "Warning: redefined ruleset for header=%s, old=%s, new=%s",
						  fname,
						  s->s_header.hi_ruleset, p);
				s->s_header.hi_ruleset = newstr(p);
				if (!strc)
					s->s_header.hi_flags |= H_STRIPCOMM;
			}
			return 0;
		}
	}

	/* see if it is a known type */
	s = stab(fname, ST_HEADER, ST_FIND);
	if (s != NULL)
		hi = &s->s_header;
	else
		hi = &NormalHeader;

	if (tTd(31, 9))
	{
		if (s == NULL)
			sm_dprintf("no header flags match\n");
		else
			sm_dprintf("header match, flags=%lx, ruleset=%s\n",
				   hi->hi_flags,
				   hi->hi_ruleset == NULL ? "<NULL>"
							  : hi->hi_ruleset);
	}

	/* see if this is a resent message */
	if (!bitset(pflag, CHHDR_DEF) && !headeronly &&
	    bitset(H_RESENT, hi->hi_flags))
		e->e_flags |= EF_RESENT;

	/* if this is an Errors-To: header keep track of it now */
	if (UseErrorsTo && !bitset(pflag, CHHDR_DEF) && !headeronly &&
	    bitset(H_ERRORSTO, hi->hi_flags))
		(void) sendtolist(fvalue, NULLADDR, &e->e_errorqueue, 0, e);

	/* if this means "end of header" quit now */
	if (!headeronly && bitset(H_EOH, hi->hi_flags))
		return hi->hi_flags;

	/*
	**  Horrible hack to work around problem with Lotus Notes SMTP
	**  mail gateway, which generates From: headers with newlines in
	**  them and the <address> on the second line.  Although this is
	**  legal RFC 822, many MUAs don't handle this properly and thus
	**  never find the actual address.
	*/

	if (bitset(H_FROM, hi->hi_flags) && SingleLineFromHeader)
	{
		while ((p = strchr(fvalue, '\n')) != NULL)
			*p = ' ';
	}

	/*
	**  If there is a check ruleset, verify it against the header.
	*/

	if (bitset(pflag, CHHDR_CHECK))
	{
		int rscheckflags;
		char *rs;

		rscheckflags = RSF_COUNT;
		if (!bitset(hi->hi_flags, H_FROM|H_RCPT))
			rscheckflags |= RSF_UNSTRUCTURED;

		/* no ruleset? look for default */
		rs = hi->hi_ruleset;
		if (rs == NULL)
		{
			s = stab("*", ST_HEADER, ST_FIND);
			if (s != NULL)
			{
				rs = (&s->s_header)->hi_ruleset;
				if (bitset((&s->s_header)->hi_flags,
					   H_STRIPCOMM))
					rscheckflags |= RSF_RMCOMM;
			}
		}
		else if (bitset(hi->hi_flags, H_STRIPCOMM))
			rscheckflags |= RSF_RMCOMM;
		if (rs != NULL)
		{
			int l, k;
			char qval[MAXNAME];

			l = 0;
			qval[l++] = '"';

			/* - 3 to avoid problems with " at the end */
			/* should be sizeof(qval), not MAXNAME */
			for (k = 0; fvalue[k] != '\0' && l < MAXNAME - 3; k++)
			{
				switch (fvalue[k])
				{
				  /* XXX other control chars? */
				  case '\011': /* ht */
				  case '\012': /* nl */
				  case '\013': /* vt */
				  case '\014': /* np */
				  case '\015': /* cr */
					qval[l++] = ' ';
					break;
				  case '"':
					qval[l++] = '\\';
					/* FALLTHROUGH */
				  default:
					qval[l++] = fvalue[k];
					break;
				}
			}
			qval[l++] = '"';
			qval[l] = '\0';
			k += strlen(fvalue + k);
			if (k >= MAXNAME)
			{
				if (LogLevel > 9)
					sm_syslog(LOG_WARNING, e->e_id,
						  "Warning: truncated header '%s' before check with '%s' len=%d max=%d",
						  fname, rs, k, MAXNAME - 1);
			}
			macdefine(&e->e_macro, A_TEMP,
				macid("{currHeader}"), qval);
			macdefine(&e->e_macro, A_TEMP,
				macid("{hdr_name}"), fname);

			(void) sm_snprintf(qval, sizeof(qval), "%d", k);
			macdefine(&e->e_macro, A_TEMP, macid("{hdrlen}"), qval);
			if (bitset(H_FROM, hi->hi_flags))
				macdefine(&e->e_macro, A_PERM,
					macid("{addr_type}"), "h s");
			else if (bitset(H_RCPT, hi->hi_flags))
				macdefine(&e->e_macro, A_PERM,
					macid("{addr_type}"), "h r");
			else
				macdefine(&e->e_macro, A_PERM,
					macid("{addr_type}"), "h");
			(void) rscheck(rs, fvalue, NULL, e, rscheckflags, 3,
				       NULL, e->e_id, NULL, NULL);
		}
	}

	/*
	**  Drop explicit From: if same as what we would generate.
	**  This is to make MH (which doesn't always give a full name)
	**  insert the full name information in all circumstances.
	*/

	dropfrom = false;
	p = "resent-from";
	if (!bitset(EF_RESENT, e->e_flags))
		p += 7;
	if (!bitset(pflag, CHHDR_DEF) && !headeronly &&
	    !bitset(EF_QUEUERUN, e->e_flags) && sm_strcasecmp(fname, p) == 0)
	{
		if (e->e_from.q_paddr != NULL &&
		    e->e_from.q_mailer != NULL &&
		    bitnset(M_LOCALMAILER, e->e_from.q_mailer->m_flags) &&
		    (strcmp(fvalue, e->e_from.q_paddr) == 0 ||
		     strcmp(fvalue, e->e_from.q_user) == 0))
			dropfrom = true;
		if (tTd(31, 2))
		{
			sm_dprintf("comparing header from (%s) against default (%s or %s), drop=%d\n",
				fvalue, e->e_from.q_paddr, e->e_from.q_user,
				dropfrom);
		}
	}

	/* delete default value for this header */
	for (hp = hdrp; (h = *hp) != NULL; hp = &h->h_link)
	{
		if (sm_strcasecmp(fname, h->h_field) == 0 &&
		    !bitset(H_USER, h->h_flags) &&
		    !bitset(H_FORCE, h->h_flags))
		{
			if (nullheader)
			{
				/* user-supplied value was null */
				return 0;
			}
			if (dropfrom)
			{
				/* make this look like the user entered it */
				h->h_flags |= H_USER;

				/*
				**  If the MH hack is selected, allow to turn
				**  it off via a mailer flag to avoid problems
				**  with setups that remove the F flag from
				**  the RCPT mailer.
				*/

				if (bitnset(M_NOMHHACK,
					    e->e_from.q_mailer->m_flags))
				{
					h->h_flags &= ~H_CHECK;
				}
				return hi->hi_flags;
			}
			h->h_value = NULL;
			if (!cond)
			{
				/* copy conditions from default case */
				memmove((char *) mopts, (char *) h->h_mflags,
					sizeof(mopts));
			}
			h->h_macro = mid;
		}
	}

	/* create a new node */
	h = (HDR *) sm_rpool_malloc_x(e->e_rpool, sizeof(*h));
	h->h_field = sm_rpool_strdup_x(e->e_rpool, fname);
	h->h_value = sm_rpool_strdup_x(e->e_rpool, fvalue);
	h->h_link = NULL;
	memmove((char *) h->h_mflags, (char *) mopts, sizeof(mopts));
	h->h_macro = mid;
	*hp = h;
	h->h_flags = hi->hi_flags;
	if (bitset(pflag, CHHDR_USER) || bitset(pflag, CHHDR_QUEUE))
		h->h_flags |= H_USER;

	/* strip EOH flag if parsing MIME headers */
	if (headeronly)
		h->h_flags &= ~H_EOH;
	if (bitset(pflag, CHHDR_DEF))
		h->h_flags |= H_DEFAULT;
	if (cond || mid != '\0')
		h->h_flags |= H_CHECK;

	/* hack to see if this is a new format message */
	if (!bitset(pflag, CHHDR_DEF) && !headeronly &&
	    bitset(H_RCPT|H_FROM, h->h_flags) &&
	    (strchr(fvalue, ',') != NULL || strchr(fvalue, '(') != NULL ||
	     strchr(fvalue, '<') != NULL || strchr(fvalue, ';') != NULL))
	{
		e->e_flags &= ~EF_OLDSTYLE;
	}

	return h->h_flags;
}

/*
**  CHOMPHEADER -- process and save a header line.
**
**	Called by collect, readcf, and readqf to deal with header lines.
**	This is just a wrapper for dochompheader().
**
**	Parameters:
**		line -- header as a text line.
**		pflag -- flags for chompheader() (from sendmail.h)
**		hdrp -- a pointer to the place to save the header.
**		e -- the envelope including this header.
**
**	Returns:
**		flags for this header.
**
**	Side Effects:
**		The header is saved on the header list.
**		Contents of 'line' are destroyed.
*/


unsigned long
chompheader(line, pflag, hdrp, e)
	char *line;
	int pflag;
	HDR **hdrp;
	register ENVELOPE *e;
{
	unsigned long rval;

	if (tTd(31, 6))
	{
		sm_dprintf("chompheader: ");
		xputs(sm_debug_file(), line);
		sm_dprintf("\n");
	}

	/* quote this if user (not config file) input */
	if (bitset(pflag, CHHDR_USER))
	{
		char xbuf[MAXLINE];
		char *xbp = NULL;
		int xbufs;

		xbufs = sizeof(xbuf);
		xbp = quote_internal_chars(line, xbuf, &xbufs);
		if (tTd(31, 7))
		{
			sm_dprintf("chompheader: quoted: ");
			xputs(sm_debug_file(), xbp);
			sm_dprintf("\n");
		}
		rval = dochompheader(xbp, pflag, hdrp, e);
		if (xbp != xbuf)
			sm_free(xbp);
	}
	else
		rval = dochompheader(line, pflag, hdrp, e);

	return rval;
}

/*
**  ALLOCHEADER -- allocate a header entry
**
**	Parameters:
**		field -- the name of the header field (will not be copied).
**		value -- the value of the field (will be copied).
**		flags -- flags to add to h_flags.
**		rp -- resource pool for allocations
**		space -- add leading space?
**
**	Returns:
**		Pointer to a newly allocated and populated HDR.
**
**	Notes:
**		o field and value must be in internal format, i.e.,
**		metacharacters must be "quoted", see quote_internal_chars().
**		o maybe add more flags to decide:
**		  - what to copy (field/value)
**		  - whether to convert value to an internal format
*/

static HDR *
allocheader(field, value, flags, rp, space)
	char *field;
	char *value;
	int flags;
	SM_RPOOL_T *rp;
	bool space;
{
	HDR *h;
	STAB *s;

	/* find info struct */
	s = stab(field, ST_HEADER, ST_FIND);

	/* allocate space for new header */
	h = (HDR *) sm_rpool_malloc_x(rp, sizeof(*h));
	h->h_field = field;
	if (space)
	{
		size_t l;
		char *n;

		l = strlen(value);
		SM_ASSERT(l + 2 > l);
		n = sm_rpool_malloc_x(rp, l + 2);
		n[0] = ' ';
		n[1] = '\0';
		sm_strlcpy(n + 1, value, l + 1);
		h->h_value = n;
	}
	else
		h->h_value = sm_rpool_strdup_x(rp, value);
	h->h_flags = flags;
	if (s != NULL)
		h->h_flags |= s->s_header.hi_flags;
	clrbitmap(h->h_mflags);
	h->h_macro = '\0';

	return h;
}

/*
**  ADDHEADER -- add a header entry to the end of the queue.
**
**	This bypasses the special checking of chompheader.
**
**	Parameters:
**		field -- the name of the header field (will not be copied).
**		value -- the value of the field (will be copied).
**		flags -- flags to add to h_flags.
**		e -- envelope.
**		space -- add leading space?
**
**	Returns:
**		none.
**
**	Side Effects:
**		adds the field on the list of headers for this envelope.
**
**	Notes: field and value must be in internal format, i.e.,
**		metacharacters must be "quoted", see quote_internal_chars().
*/

void
addheader(field, value, flags, e, space)
	char *field;
	char *value;
	int flags;
	ENVELOPE *e;
	bool space;
{
	register HDR *h;
	HDR **hp;
	HDR **hdrlist = &e->e_header;

	/* find current place in list -- keep back pointer? */
	for (hp = hdrlist; (h = *hp) != NULL; hp = &h->h_link)
	{
		if (sm_strcasecmp(field, h->h_field) == 0)
			break;
	}

	/* allocate space for new header */
	h = allocheader(field, value, flags, e->e_rpool, space);
	h->h_link = *hp;
	*hp = h;
}

/*
**  INSHEADER -- insert a header entry at the specified index
**	This bypasses the special checking of chompheader.
**
**	Parameters:
**		idx -- index into the header list at which to insert
**		field -- the name of the header field (will be copied).
**		value -- the value of the field (will be copied).
**		flags -- flags to add to h_flags.
**		e -- envelope.
**		space -- add leading space?
**
**	Returns:
**		none.
**
**	Side Effects:
**		inserts the field on the list of headers for this envelope.
**
**	Notes:
**		- field and value must be in internal format, i.e.,
**		metacharacters must be "quoted", see quote_internal_chars().
**		- the header list contains headers that might not be
**		sent "out" (see putheader(): "skip"), hence there is no
**		reliable way to insert a header at an exact position
**		(except at the front or end).
*/

void
insheader(idx, field, value, flags, e, space)
	int idx;
	char *field;
	char *value;
	int flags;
	ENVELOPE *e;
	bool space;
{
	HDR *h, *srch, *last = NULL;

	/* allocate space for new header */
	h = allocheader(field, value, flags, e->e_rpool, space);

	/* find insertion position */
	for (srch = e->e_header; srch != NULL && idx > 0;
	     srch = srch->h_link, idx--)
		last = srch;

	if (e->e_header == NULL)
	{
		e->e_header = h;
		h->h_link = NULL;
	}
	else if (srch == NULL)
	{
		SM_ASSERT(last != NULL);
		last->h_link = h;
		h->h_link = NULL;
	}
	else
	{
		h->h_link = srch->h_link;
		srch->h_link = h;
	}
}

/*
**  HVALUE -- return value of a header.
**
**	Only "real" fields (i.e., ones that have not been supplied
**	as a default) are used.
**
**	Parameters:
**		field -- the field name.
**		header -- the header list.
**
**	Returns:
**		pointer to the value part (internal format).
**		NULL if not found.
**
**	Side Effects:
**		none.
*/

char *
hvalue(field, header)
	char *field;
	HDR *header;
{
	register HDR *h;

	for (h = header; h != NULL; h = h->h_link)
	{
		if (!bitset(H_DEFAULT, h->h_flags) &&
		    sm_strcasecmp(h->h_field, field) == 0)
		{
			char *s;

			s = h->h_value;
			if (s == NULL)
				return NULL;
			while (isascii(*s) && isspace(*s))
				s++;
			return s;
		}
	}
	return NULL;
}

/*
**  ISHEADER -- predicate telling if argument is a header.
**
**	A line is a header if it has a single word followed by
**	optional white space followed by a colon.
**
**	Header fields beginning with two dashes, although technically
**	permitted by RFC822, are automatically rejected in order
**	to make MIME work out.  Without this we could have a technically
**	legal header such as ``--"foo:bar"'' that would also be a legal
**	MIME separator.
**
**	Parameters:
**		h -- string to check for possible headerness.
**
**	Returns:
**		true if h is a header.
**		false otherwise.
**
**	Side Effects:
**		none.
*/

bool
isheader(h)
	char *h;
{
	char *s;

	s = h;
	if (s[0] == '-' && s[1] == '-')
		return false;

	while (*s > ' ' && *s != ':' && *s != '\0')
		s++;

	if (h == s)
		return false;

	/* following technically violates RFC822 */
	while (isascii(*s) && isspace(*s))
		s++;

	return (*s == ':');
}

/*
**  EATHEADER -- run through the stored header and extract info.
**
**	Parameters:
**		e -- the envelope to process.
**		full -- if set, do full processing (e.g., compute
**			message priority).  This should not be set
**			when reading a queue file because some info
**			needed to compute the priority is wrong.
**		log -- call logsender()?
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets a bunch of global variables from information
**			in the collected header.
*/

void
eatheader(e, full, log)
	register ENVELOPE *e;
	bool full;
	bool log;
{
	register HDR *h;
	register char *p;
	int hopcnt = 0;
	char buf[MAXLINE];

	/*
	**  Set up macros for possible expansion in headers.
	*/

	macdefine(&e->e_macro, A_PERM, 'f', e->e_sender);
	macdefine(&e->e_macro, A_PERM, 'g', e->e_sender);
	if (e->e_origrcpt != NULL && *e->e_origrcpt != '\0')
		macdefine(&e->e_macro, A_PERM, 'u', e->e_origrcpt);
	else
		macdefine(&e->e_macro, A_PERM, 'u', NULL);

	/* full name of from person */
	p = hvalue("full-name", e->e_header);
	if (p != NULL)
	{
		if (!rfc822_string(p))
		{
			/*
			**  Quote a full name with special characters
			**  as a comment so crackaddr() doesn't destroy
			**  the name portion of the address.
			*/

			p = addquotes(p, e->e_rpool);
		}
		macdefine(&e->e_macro, A_PERM, 'x', p);
	}

	if (tTd(32, 1))
		sm_dprintf("----- collected header -----\n");
	e->e_msgid = NULL;
	for (h = e->e_header; h != NULL; h = h->h_link)
	{
		if (tTd(32, 1))
			sm_dprintf("%s:", h->h_field);
		if (h->h_value == NULL)
		{
			if (tTd(32, 1))
				sm_dprintf("<NULL>\n");
			continue;
		}

		/* do early binding */
		if (bitset(H_DEFAULT, h->h_flags) &&
		    !bitset(H_BINDLATE, h->h_flags))
		{
			if (tTd(32, 1))
			{
				sm_dprintf("(");
				xputs(sm_debug_file(), h->h_value);
				sm_dprintf(") ");
			}
			expand(h->h_value, buf, sizeof(buf), e);
			if (buf[0] != '\0' &&
			    (buf[0] != ' ' || buf[1] != '\0'))
			{
				if (bitset(H_FROM, h->h_flags))
					expand(crackaddr(buf, e),
					       buf, sizeof(buf), e);
				h->h_value = sm_rpool_strdup_x(e->e_rpool, buf);
				h->h_flags &= ~H_DEFAULT;
			}
		}
		if (tTd(32, 1))
		{
			xputs(sm_debug_file(), h->h_value);
			sm_dprintf("\n");
		}

		/* count the number of times it has been processed */
		if (bitset(H_TRACE, h->h_flags))
			hopcnt++;

		/* send to this person if we so desire */
		if (GrabTo && bitset(H_RCPT, h->h_flags) &&
		    !bitset(H_DEFAULT, h->h_flags) &&
		    (!bitset(EF_RESENT, e->e_flags) ||
		     bitset(H_RESENT, h->h_flags)))
		{
#if 0
			int saveflags = e->e_flags;
#endif /* 0 */

			(void) sendtolist(denlstring(h->h_value, true, false),
					  NULLADDR, &e->e_sendqueue, 0, e);

#if 0
			/*
			**  Change functionality so a fatal error on an
			**  address doesn't affect the entire envelope.
			*/

			/* delete fatal errors generated by this address */
			if (!bitset(EF_FATALERRS, saveflags))
				e->e_flags &= ~EF_FATALERRS;
#endif /* 0 */
		}

		/* save the message-id for logging */
		p = "resent-message-id";
		if (!bitset(EF_RESENT, e->e_flags))
			p += 7;
		if (sm_strcasecmp(h->h_field, p) == 0)
		{
			e->e_msgid = h->h_value;
			while (isascii(*e->e_msgid) && isspace(*e->e_msgid))
				e->e_msgid++;
			macdefine(&e->e_macro, A_PERM, macid("{msg_id}"),
				  e->e_msgid);
		}
	}
	if (tTd(32, 1))
		sm_dprintf("----------------------------\n");

	/* if we are just verifying (that is, sendmail -t -bv), drop out now */
	if (OpMode == MD_VERIFY)
		return;

	/* store hop count */
	if (hopcnt > e->e_hopcount)
	{
		e->e_hopcount = hopcnt;
		(void) sm_snprintf(buf, sizeof(buf), "%d", e->e_hopcount);
		macdefine(&e->e_macro, A_TEMP, 'c', buf);
	}

	/* message priority */
	p = hvalue("precedence", e->e_header);
	if (p != NULL)
		e->e_class = priencode(p);
	if (e->e_class < 0)
		e->e_timeoutclass = TOC_NONURGENT;
	else if (e->e_class > 0)
		e->e_timeoutclass = TOC_URGENT;
	if (full)
	{
		e->e_msgpriority = e->e_msgsize
				 - e->e_class * WkClassFact
				 + e->e_nrcpts * WkRecipFact;
	}

	/* check for DSN to properly set e_timeoutclass */
	p = hvalue("content-type", e->e_header);
	if (p != NULL)
	{
		bool oldsupr;
		char **pvp;
		char pvpbuf[MAXLINE];
		extern unsigned char MimeTokenTab[256];

		/* tokenize header */
		oldsupr = SuprErrs;
		SuprErrs = true;
		pvp = prescan(p, '\0', pvpbuf, sizeof(pvpbuf), NULL,
			      MimeTokenTab, false);
		SuprErrs = oldsupr;

		/* Check if multipart/report */
		if (pvp != NULL && pvp[0] != NULL &&
		    pvp[1] != NULL && pvp[2] != NULL &&
		    sm_strcasecmp(*pvp++, "multipart") == 0 &&
		    strcmp(*pvp++, "/") == 0 &&
		    sm_strcasecmp(*pvp++, "report") == 0)
		{
			/* Look for report-type=delivery-status */
			while (*pvp != NULL)
			{
				/* skip to semicolon separator */
				while (*pvp != NULL && strcmp(*pvp, ";") != 0)
					pvp++;

				/* skip semicolon */
				if (*pvp++ == NULL || *pvp == NULL)
					break;

				/* look for report-type */
				if (sm_strcasecmp(*pvp++, "report-type") != 0)
					continue;

				/* skip equal */
				if (*pvp == NULL || strcmp(*pvp, "=") != 0)
					continue;

				/* check value */
				if (*++pvp != NULL &&
				    sm_strcasecmp(*pvp,
						  "delivery-status") == 0)
					e->e_timeoutclass = TOC_DSN;

				/* found report-type, no need to continue */
				break;
			}
		}
	}

	/* message timeout priority */
	p = hvalue("priority", e->e_header);
	if (p != NULL)
	{
		/* (this should be in the configuration file) */
		if (sm_strcasecmp(p, "urgent") == 0)
			e->e_timeoutclass = TOC_URGENT;
		else if (sm_strcasecmp(p, "normal") == 0)
			e->e_timeoutclass = TOC_NORMAL;
		else if (sm_strcasecmp(p, "non-urgent") == 0)
			e->e_timeoutclass = TOC_NONURGENT;
		else if (bitset(EF_RESPONSE, e->e_flags))
			e->e_timeoutclass = TOC_DSN;
	}
	else if (bitset(EF_RESPONSE, e->e_flags))
		e->e_timeoutclass = TOC_DSN;

	/* date message originated */
	p = hvalue("posted-date", e->e_header);
	if (p == NULL)
		p = hvalue("date", e->e_header);
	if (p != NULL)
		macdefine(&e->e_macro, A_PERM, 'a', p);

	/* check to see if this is a MIME message */
	if ((e->e_bodytype != NULL &&
	     sm_strcasecmp(e->e_bodytype, "8BITMIME") == 0) ||
	    hvalue("MIME-Version", e->e_header) != NULL)
	{
		e->e_flags |= EF_IS_MIME;
		if (HasEightBits)
			e->e_bodytype = "8BITMIME";
	}
	else if ((p = hvalue("Content-Type", e->e_header)) != NULL)
	{
		/* this may be an RFC 1049 message */
		p = strpbrk(p, ";/");
		if (p == NULL || *p == ';')
		{
			/* yep, it is */
			e->e_flags |= EF_DONT_MIME;
		}
	}

	/*
	**  From person in antiquated ARPANET mode
	**	required by UK Grey Book e-mail gateways (sigh)
	*/

	if (OpMode == MD_ARPAFTP)
	{
		register struct hdrinfo *hi;

		for (hi = HdrInfo; hi->hi_field != NULL; hi++)
		{
			if (bitset(H_FROM, hi->hi_flags) &&
			    (!bitset(H_RESENT, hi->hi_flags) ||
			     bitset(EF_RESENT, e->e_flags)) &&
			    (p = hvalue(hi->hi_field, e->e_header)) != NULL)
				break;
		}
		if (hi->hi_field != NULL)
		{
			if (tTd(32, 2))
				sm_dprintf("eatheader: setsender(*%s == %s)\n",
					hi->hi_field, p);
			setsender(p, e, NULL, '\0', true);
		}
	}

	/*
	**  Log collection information.
	*/

	if (tTd(92, 2))
		sm_dprintf("eatheader: e_id=%s, EF_LOGSENDER=%d, LogLevel=%d, log=%d\n",
			e->e_id, bitset(EF_LOGSENDER, e->e_flags), LogLevel,
			log);
	if (log && bitset(EF_LOGSENDER, e->e_flags) && LogLevel > 4)
	{
		logsender(e, e->e_msgid);
		e->e_flags &= ~EF_LOGSENDER;
	}
}

/*
**  LOGSENDER -- log sender information
**
**	Parameters:
**		e -- the envelope to log
**		msgid -- the message id
**
**	Returns:
**		none
*/

void
logsender(e, msgid)
	register ENVELOPE *e;
	char *msgid;
{
	char *name;
	register char *sbp;
	register char *p;
	char hbuf[MAXNAME + 1];
	char sbuf[MAXLINE + 1];
	char mbuf[MAXNAME + 1];

	/* don't allow newlines in the message-id */
	/* XXX do we still need this? sm_syslog() replaces control chars */
	if (msgid != NULL)
	{
		size_t l;

		l = strlen(msgid);
		if (l > sizeof(mbuf) - 1)
			l = sizeof(mbuf) - 1;
		memmove(mbuf, msgid, l);
		mbuf[l] = '\0';
		p = mbuf;
		while ((p = strchr(p, '\n')) != NULL)
			*p++ = ' ';
	}

	if (bitset(EF_RESPONSE, e->e_flags))
		name = "[RESPONSE]";
	else if ((name = macvalue('_', e)) != NULL)
		/* EMPTY */
		;
	else if (RealHostName == NULL)
		name = "localhost";
	else if (RealHostName[0] == '[')
		name = RealHostName;
	else
	{
		name = hbuf;
		(void) sm_snprintf(hbuf, sizeof(hbuf), "%.80s", RealHostName);
		if (RealHostAddr.sa.sa_family != 0)
		{
			p = &hbuf[strlen(hbuf)];
			(void) sm_snprintf(p, SPACELEFT(hbuf, p),
					   " (%.100s)",
					   anynet_ntoa(&RealHostAddr));
		}
	}

	/* some versions of syslog only take 5 printf args */
#if (SYSLOG_BUFSIZE) >= 256
	sbp = sbuf;
	(void) sm_snprintf(sbp, SPACELEFT(sbuf, sbp),
		"from=%.200s, size=%ld, class=%d, nrcpts=%d",
		e->e_from.q_paddr == NULL ? "<NONE>" : e->e_from.q_paddr,
		PRT_NONNEGL(e->e_msgsize), e->e_class, e->e_nrcpts);
	sbp += strlen(sbp);
	if (msgid != NULL)
	{
		(void) sm_snprintf(sbp, SPACELEFT(sbuf, sbp),
				", msgid=%.100s", mbuf);
		sbp += strlen(sbp);
	}
	if (e->e_bodytype != NULL)
	{
		(void) sm_snprintf(sbp, SPACELEFT(sbuf, sbp),
				", bodytype=%.20s", e->e_bodytype);
		sbp += strlen(sbp);
	}
	p = macvalue('r', e);
	if (p != NULL)
	{
		(void) sm_snprintf(sbp, SPACELEFT(sbuf, sbp),
				", proto=%.20s", p);
		sbp += strlen(sbp);
	}
	p = macvalue(macid("{daemon_name}"), e);
	if (p != NULL)
	{
		(void) sm_snprintf(sbp, SPACELEFT(sbuf, sbp),
				", daemon=%.20s", p);
		sbp += strlen(sbp);
	}
# if _FFR_LOG_MORE1
#  if STARTTLS
	p = macvalue(macid("{verify}"), e);
	if (p == NULL || *p == '\0')
		p = "NONE";
	(void) sm_snprintf(sbp, SPACELEFT(sbuf, sbp), ", tls_verify=%.20s", p);
	sbp += strlen(sbp);
#  endif /* STARTTLS */
#  if SASL
	p = macvalue(macid("{auth_type}"), e);
	if (p == NULL || *p == '\0')
		p = "NONE";
	(void) sm_snprintf(sbp, SPACELEFT(sbuf, sbp), ", auth=%.20s", p);
	sbp += strlen(sbp);
#  endif /* SASL */
# endif /* _FFR_LOG_MORE1 */
	sm_syslog(LOG_INFO, e->e_id, "%.850s, relay=%s", sbuf, name);

#else /* (SYSLOG_BUFSIZE) >= 256 */

	sm_syslog(LOG_INFO, e->e_id,
		  "from=%s",
		  e->e_from.q_paddr == NULL ? "<NONE>"
					    : shortenstring(e->e_from.q_paddr,
							    83));
	sm_syslog(LOG_INFO, e->e_id,
		  "size=%ld, class=%ld, nrcpts=%d",
		  PRT_NONNEGL(e->e_msgsize), e->e_class, e->e_nrcpts);
	if (msgid != NULL)
		sm_syslog(LOG_INFO, e->e_id,
			  "msgid=%s",
			  shortenstring(mbuf, 83));
	sbp = sbuf;
	*sbp = '\0';
	if (e->e_bodytype != NULL)
	{
		(void) sm_snprintf(sbp, SPACELEFT(sbuf, sbp),
				"bodytype=%.20s, ", e->e_bodytype);
		sbp += strlen(sbp);
	}
	p = macvalue('r', e);
	if (p != NULL)
	{
		(void) sm_snprintf(sbp, SPACELEFT(sbuf, sbp),
				"proto=%.20s, ", p);
		sbp += strlen(sbp);
	}
	sm_syslog(LOG_INFO, e->e_id,
		  "%.400srelay=%s", sbuf, name);
#endif /* (SYSLOG_BUFSIZE) >= 256 */
}

/*
**  PRIENCODE -- encode external priority names into internal values.
**
**	Parameters:
**		p -- priority in ascii.
**
**	Returns:
**		priority as a numeric level.
**
**	Side Effects:
**		none.
*/

static int
priencode(p)
	char *p;
{
	register int i;

	for (i = 0; i < NumPriorities; i++)
	{
		if (sm_strcasecmp(p, Priorities[i].pri_name) == 0)
			return Priorities[i].pri_val;
	}

	/* unknown priority */
	return 0;
}

/*
**  CRACKADDR -- parse an address and turn it into a macro
**
**	This doesn't actually parse the address -- it just extracts
**	it and replaces it with "$g".  The parse is totally ad hoc
**	and isn't even guaranteed to leave something syntactically
**	identical to what it started with.  However, it does leave
**	something semantically identical if possible, else at least
**	syntactically correct.
**
**	For example, it changes "Real Name <real@example.com> (Comment)"
**	to "Real Name <$g> (Comment)".
**
**	This algorithm has been cleaned up to handle a wider range
**	of cases -- notably quoted and backslash escaped strings.
**	This modification makes it substantially better at preserving
**	the original syntax.
**
**	Parameters:
**		addr -- the address to be cracked.
**		e -- the current envelope.
**
**	Returns:
**		a pointer to the new version.
**
**	Side Effects:
**		none.
**
**	Warning:
**		The return value is saved in local storage and should
**		be copied if it is to be reused.
*/

#define SM_HAVE_ROOM		((bp < buflim) && (buflim <= bufend))

/*
**  Append a character to bp if we have room.
**  If not, punt and return $g.
*/

#define SM_APPEND_CHAR(c)					\
	do							\
	{							\
		if (SM_HAVE_ROOM)				\
			*bp++ = (c);				\
		else						\
			goto returng;				\
	} while (0)

#if MAXNAME < 10
ERROR MAXNAME must be at least 10
#endif /* MAXNAME < 10 */

char *
crackaddr(addr, e)
	register char *addr;
	ENVELOPE *e;
{
	register char *p;
	register char c;
	int cmtlev;			/* comment level in input string */
	int realcmtlev;			/* comment level in output string */
	int anglelev;			/* angle level in input string */
	int copylev;			/* 0 == in address, >0 copying */
	int bracklev;			/* bracket level for IPv6 addr check */
	bool addangle;			/* put closing angle in output */
	bool qmode;			/* quoting in original string? */
	bool realqmode;			/* quoting in output string? */
	bool putgmac = false;		/* already wrote $g */
	bool quoteit = false;		/* need to quote next character */
	bool gotangle = false;		/* found first '<' */
	bool gotcolon = false;		/* found a ':' */
	register char *bp;
	char *buflim;
	char *bufhead;
	char *addrhead;
	char *bufend;
	static char buf[MAXNAME + 1];

	if (tTd(33, 1))
		sm_dprintf("crackaddr(%s)\n", addr);

	buflim = bufend = &buf[sizeof(buf) - 1];
	bp = bufhead = buf;

	/* skip over leading spaces but preserve them */
	while (*addr != '\0' && isascii(*addr) && isspace(*addr))
	{
		SM_APPEND_CHAR(*addr);
		addr++;
	}
	bufhead = bp;

	/*
	**  Start by assuming we have no angle brackets.  This will be
	**  adjusted later if we find them.
	*/

	p = addrhead = addr;
	copylev = anglelev = cmtlev = realcmtlev = 0;
	bracklev = 0;
	qmode = realqmode = addangle = false;

	while ((c = *p++) != '\0')
	{
		/*
		**  Try to keep legal syntax using spare buffer space
		**  (maintained by buflim).
		*/

		if (copylev > 0)
			SM_APPEND_CHAR(c);

		/* check for backslash escapes */
		if (c == '\\')
		{
			/* arrange to quote the address */
			if (cmtlev <= 0 && !qmode)
				quoteit = true;

			if ((c = *p++) == '\0')
			{
				/* too far */
				p--;
				goto putg;
			}
			if (copylev > 0)
				SM_APPEND_CHAR(c);
			goto putg;
		}

		/* check for quoted strings */
		if (c == '"' && cmtlev <= 0)
		{
			qmode = !qmode;
			if (copylev > 0 && SM_HAVE_ROOM)
			{
				if (realqmode)
					buflim--;
				else
					buflim++;
				realqmode = !realqmode;
			}
			continue;
		}
		if (qmode)
			goto putg;

		/* check for comments */
		if (c == '(')
		{
			cmtlev++;

			/* allow space for closing paren */
			if (SM_HAVE_ROOM)
			{
				buflim--;
				realcmtlev++;
				if (copylev++ <= 0)
				{
					if (bp != bufhead)
						SM_APPEND_CHAR(' ');
					SM_APPEND_CHAR(c);
				}
			}
		}
		if (cmtlev > 0)
		{
			if (c == ')')
			{
				cmtlev--;
				copylev--;
				if (SM_HAVE_ROOM)
				{
					realcmtlev--;
					buflim++;
				}
			}
			continue;
		}
		else if (c == ')')
		{
			/* syntax error: unmatched ) */
			if (copylev > 0 && SM_HAVE_ROOM && bp > bufhead)
				bp--;
		}

		/* count nesting on [ ... ] (for IPv6 domain literals) */
		if (c == '[')
			bracklev++;
		else if (c == ']')
			bracklev--;

		/* check for group: list; syntax */
		if (c == ':' && anglelev <= 0 && bracklev <= 0 &&
		    !gotcolon && !ColonOkInAddr)
		{
			register char *q;

			/*
			**  Check for DECnet phase IV ``::'' (host::user)
			**  or DECnet phase V ``:.'' syntaxes.  The latter
			**  covers ``user@DEC:.tay.myhost'' and
			**  ``DEC:.tay.myhost::user'' syntaxes (bletch).
			*/

			if (*p == ':' || *p == '.')
			{
				if (cmtlev <= 0 && !qmode)
					quoteit = true;
				if (copylev > 0)
				{
					SM_APPEND_CHAR(c);
					SM_APPEND_CHAR(*p);
				}
				p++;
				goto putg;
			}

			gotcolon = true;

			bp = bufhead;
			if (quoteit)
			{
				SM_APPEND_CHAR('"');

				/* back up over the ':' and any spaces */
				--p;
				while (p > addr &&
				       isascii(*--p) && isspace(*p))
					continue;
				p++;
			}
			for (q = addrhead; q < p; )
			{
				c = *q++;
				if (quoteit && c == '"')
					SM_APPEND_CHAR('\\');
				SM_APPEND_CHAR(c);
			}
			if (quoteit)
			{
				if (bp == &bufhead[1])
					bp--;
				else
					SM_APPEND_CHAR('"');
				while ((c = *p++) != ':')
					SM_APPEND_CHAR(c);
				SM_APPEND_CHAR(c);
			}

			/* any trailing white space is part of group: */
			while (isascii(*p) && isspace(*p))
			{
				SM_APPEND_CHAR(*p);
				p++;
			}
			copylev = 0;
			putgmac = quoteit = false;
			bufhead = bp;
			addrhead = p;
			continue;
		}

		if (c == ';' && copylev <= 0 && !ColonOkInAddr)
			SM_APPEND_CHAR(c);

		/* check for characters that may have to be quoted */
		if (strchr(MustQuoteChars, c) != NULL)
		{
			/*
			**  If these occur as the phrase part of a <>
			**  construct, but are not inside of () or already
			**  quoted, they will have to be quoted.  Note that
			**  now (but don't actually do the quoting).
			*/

			if (cmtlev <= 0 && !qmode)
				quoteit = true;
		}

		/* check for angle brackets */
		if (c == '<')
		{
			register char *q;

			/* assume first of two angles is bogus */
			if (gotangle)
				quoteit = true;
			gotangle = true;

			/* oops -- have to change our mind */
			anglelev = 1;
			if (SM_HAVE_ROOM)
			{
				if (!addangle)
					buflim--;
				addangle = true;
			}

			bp = bufhead;
			if (quoteit)
			{
				SM_APPEND_CHAR('"');

				/* back up over the '<' and any spaces */
				--p;
				while (p > addr &&
				       isascii(*--p) && isspace(*p))
					continue;
				p++;
			}
			for (q = addrhead; q < p; )
			{
				c = *q++;
				if (quoteit && c == '"')
				{
					SM_APPEND_CHAR('\\');
					SM_APPEND_CHAR(c);
				}
				else
					SM_APPEND_CHAR(c);
			}
			if (quoteit)
			{
				if (bp == &buf[1])
					bp--;
				else
					SM_APPEND_CHAR('"');
				while ((c = *p++) != '<')
					SM_APPEND_CHAR(c);
				SM_APPEND_CHAR(c);
			}
			copylev = 0;
			putgmac = quoteit = false;
			continue;
		}

		if (c == '>')
		{
			if (anglelev > 0)
			{
				anglelev--;
				if (SM_HAVE_ROOM)
				{
					if (addangle)
						buflim++;
					addangle = false;
				}
			}
			else if (SM_HAVE_ROOM)
			{
				/* syntax error: unmatched > */
				if (copylev > 0 && bp > bufhead)
					bp--;
				quoteit = true;
				continue;
			}
			if (copylev++ <= 0)
				SM_APPEND_CHAR(c);
			continue;
		}

		/* must be a real address character */
	putg:
		if (copylev <= 0 && !putgmac)
		{
			if (bp > buf && bp[-1] == ')')
				SM_APPEND_CHAR(' ');
			SM_APPEND_CHAR(MACROEXPAND);
			SM_APPEND_CHAR('g');
			putgmac = true;
		}
	}

	/* repair any syntactic damage */
	if (realqmode && bp < bufend)
		*bp++ = '"';
	while (realcmtlev-- > 0 && bp < bufend)
		*bp++ = ')';
	if (addangle && bp < bufend)
		*bp++ = '>';
	*bp = '\0';
	if (bp < bufend)
		goto success;

 returng:
	/* String too long, punt */
	buf[0] = '<';
	buf[1] = MACROEXPAND;
	buf[2]= 'g';
	buf[3] = '>';
	buf[4]= '\0';
	sm_syslog(LOG_ALERT, e->e_id,
		  "Dropped invalid comments from header address");

 success:
	if (tTd(33, 1))
	{
		sm_dprintf("crackaddr=>`");
		xputs(sm_debug_file(), buf);
		sm_dprintf("'\n");
	}
	return buf;
}

/*
**  PUTHEADER -- put the header part of a message from the in-core copy
**
**	Parameters:
**		mci -- the connection information.
**		hdr -- the header to put.
**		e -- envelope to use.
**		flags -- MIME conversion flags.
**
**	Returns:
**		true iff header part was written successfully
**
**	Side Effects:
**		none.
*/

bool
putheader(mci, hdr, e, flags)
	register MCI *mci;
	HDR *hdr;
	register ENVELOPE *e;
	int flags;
{
	register HDR *h;
	char buf[SM_MAX(MAXLINE,BUFSIZ)];
	char obuf[MAXLINE];

	if (tTd(34, 1))
		sm_dprintf("--- putheader, mailer = %s ---\n",
			mci->mci_mailer->m_name);

	/*
	**  If we're in MIME mode, we're not really in the header of the
	**  message, just the header of one of the parts of the body of
	**  the message.  Therefore MCIF_INHEADER should not be turned on.
	*/

	if (!bitset(MCIF_INMIME, mci->mci_flags))
		mci->mci_flags |= MCIF_INHEADER;

	for (h = hdr; h != NULL; h = h->h_link)
	{
		register char *p = h->h_value;
		char *q;

		if (tTd(34, 11))
		{
			sm_dprintf("  %s:", h->h_field);
			xputs(sm_debug_file(), p);
		}

		/* Skip empty headers */
		if (h->h_value == NULL)
			continue;

		/* heuristic shortening of MIME fields to avoid MUA overflows */
		if (MaxMimeFieldLength > 0 &&
		    wordinclass(h->h_field,
				macid("{checkMIMEFieldHeaders}")))
		{
			size_t len;

			len = fix_mime_header(h, e);
			if (len > 0)
			{
				sm_syslog(LOG_ALERT, e->e_id,
					  "Truncated MIME %s header due to field size (length = %ld) (possible attack)",
					  h->h_field, (unsigned long) len);
				if (tTd(34, 11))
					sm_dprintf("  truncated MIME %s header due to field size  (length = %ld) (possible attack)\n",
						   h->h_field,
						   (unsigned long) len);
			}
		}

		if (MaxMimeHeaderLength > 0 &&
		    wordinclass(h->h_field,
				macid("{checkMIMETextHeaders}")))
		{
			size_t len;

			len = strlen(h->h_value);
			if (len > (size_t) MaxMimeHeaderLength)
			{
				h->h_value[MaxMimeHeaderLength - 1] = '\0';
				sm_syslog(LOG_ALERT, e->e_id,
					  "Truncated long MIME %s header (length = %ld) (possible attack)",
					  h->h_field, (unsigned long) len);
				if (tTd(34, 11))
					sm_dprintf("  truncated long MIME %s header (length = %ld) (possible attack)\n",
						   h->h_field,
						   (unsigned long) len);
			}
		}

		if (MaxMimeHeaderLength > 0 &&
		    wordinclass(h->h_field,
				macid("{checkMIMEHeaders}")))
		{
			size_t len;

			len = strlen(h->h_value);
			if (shorten_rfc822_string(h->h_value,
						  MaxMimeHeaderLength))
			{
				if (len < MaxMimeHeaderLength)
				{
					/* we only rebalanced a bogus header */
					sm_syslog(LOG_ALERT, e->e_id,
						  "Fixed MIME %s header (possible attack)",
						  h->h_field);
					if (tTd(34, 11))
						sm_dprintf("  fixed MIME %s header (possible attack)\n",
							   h->h_field);
				}
				else
				{
					/* we actually shortened header */
					sm_syslog(LOG_ALERT, e->e_id,
						  "Truncated long MIME %s header (length = %ld) (possible attack)",
						  h->h_field,
						  (unsigned long) len);
					if (tTd(34, 11))
						sm_dprintf("  truncated long MIME %s header (length = %ld) (possible attack)\n",
							   h->h_field,
							   (unsigned long) len);
				}
			}
		}

		/*
		**  Suppress Content-Transfer-Encoding: if we are MIMEing
		**  and we are potentially converting from 8 bit to 7 bit
		**  MIME.  If converting, add a new CTE header in
		**  mime8to7().
		*/

		if (bitset(H_CTE, h->h_flags) &&
		    bitset(MCIF_CVT8TO7|MCIF_CVT7TO8|MCIF_INMIME,
			   mci->mci_flags) &&
		    !bitset(M87F_NO8TO7, flags))
		{
			if (tTd(34, 11))
				sm_dprintf(" (skipped (content-transfer-encoding))\n");
			continue;
		}

		if (bitset(MCIF_INMIME, mci->mci_flags))
		{
			if (tTd(34, 11))
				sm_dprintf("\n");
			if (!put_vanilla_header(h, p, mci))
				goto writeerr;
			continue;
		}

		if (bitset(H_CHECK|H_ACHECK, h->h_flags) &&
		    !bitintersect(h->h_mflags, mci->mci_mailer->m_flags) &&
		    (h->h_macro == '\0' ||
		     (q = macvalue(bitidx(h->h_macro), e)) == NULL ||
		     *q == '\0'))
		{
			if (tTd(34, 11))
				sm_dprintf(" (skipped)\n");
			continue;
		}

		/* handle Resent-... headers specially */
		if (bitset(H_RESENT, h->h_flags) && !bitset(EF_RESENT, e->e_flags))
		{
			if (tTd(34, 11))
				sm_dprintf(" (skipped (resent))\n");
			continue;
		}

		/* suppress return receipts if requested */
		if (bitset(H_RECEIPTTO, h->h_flags) &&
		    (RrtImpliesDsn || bitset(EF_NORECEIPT, e->e_flags)))
		{
			if (tTd(34, 11))
				sm_dprintf(" (skipped (receipt))\n");
			continue;
		}

		/* macro expand value if generated internally */
		if (bitset(H_DEFAULT, h->h_flags) ||
		    bitset(H_BINDLATE, h->h_flags))
		{
			expand(p, buf, sizeof(buf), e);
			p = buf;
			if (*p == '\0')
			{
				if (tTd(34, 11))
					sm_dprintf(" (skipped -- null value)\n");
				continue;
			}
		}

		if (bitset(H_BCC, h->h_flags))
		{
			/* Bcc: field -- either truncate or delete */
			if (bitset(EF_DELETE_BCC, e->e_flags))
			{
				if (tTd(34, 11))
					sm_dprintf(" (skipped -- bcc)\n");
			}
			else
			{
				/* no other recipient headers: truncate value */
				(void) sm_strlcpyn(obuf, sizeof(obuf), 2,
						   h->h_field, ":");
				if (!putline(obuf, mci))
					goto writeerr;
			}
			continue;
		}

		if (tTd(34, 11))
			sm_dprintf("\n");

		if (bitset(H_FROM|H_RCPT, h->h_flags))
		{
			/* address field */
			bool oldstyle = bitset(EF_OLDSTYLE, e->e_flags);

			if (bitset(H_FROM, h->h_flags))
				oldstyle = false;
			if (!commaize(h, p, oldstyle, mci, e,
				      PXLF_HEADER | PXLF_STRIPMQUOTE)
			    && bitnset(M_xSMTP, mci->mci_mailer->m_flags))
				goto writeerr;
		}
		else
		{
			if (!put_vanilla_header(h, p, mci))
				goto writeerr;
		}
	}

	/*
	**  If we are converting this to a MIME message, add the
	**  MIME headers (but not in MIME mode!).
	*/

#if MIME8TO7
	if (bitset(MM_MIME8BIT, MimeMode) &&
	    bitset(EF_HAS8BIT, e->e_flags) &&
	    !bitset(EF_DONT_MIME, e->e_flags) &&
	    !bitnset(M_8BITS, mci->mci_mailer->m_flags) &&
	    !bitset(MCIF_CVT8TO7|MCIF_CVT7TO8|MCIF_INMIME, mci->mci_flags) &&
	    hvalue("MIME-Version", e->e_header) == NULL)
	{
		if (!putline("MIME-Version: 1.0", mci))
			goto writeerr;
		if (hvalue("Content-Type", e->e_header) == NULL)
		{
			(void) sm_snprintf(obuf, sizeof(obuf),
					"Content-Type: text/plain; charset=%s",
					defcharset(e));
			if (!putline(obuf, mci))
				goto writeerr;
		}
		if (hvalue("Content-Transfer-Encoding", e->e_header) == NULL
		    && !putline("Content-Transfer-Encoding: 8bit", mci))
			goto writeerr;
	}
#endif /* MIME8TO7 */
	return true;

  writeerr:
	return false;
}

/*
**  PUT_VANILLA_HEADER -- output a fairly ordinary header
**
**	Parameters:
**		h -- the structure describing this header
**		v -- the value of this header
**		mci -- the connection info for output
**
**	Returns:
**		true iff header was written successfully
*/

static bool
put_vanilla_header(h, v, mci)
	HDR *h;
	char *v;
	MCI *mci;
{
	register char *nlp;
	register char *obp;
	int putflags;
	char obuf[MAXLINE + 256];	/* additional length for h_field */

	putflags = PXLF_HEADER | PXLF_STRIPMQUOTE;
	if (bitnset(M_7BITHDRS, mci->mci_mailer->m_flags))
		putflags |= PXLF_STRIP8BIT;
	(void) sm_snprintf(obuf, sizeof(obuf), "%.200s:", h->h_field);
	obp = obuf + strlen(obuf);
	while ((nlp = strchr(v, '\n')) != NULL)
	{
		int l;

		l = nlp - v;

		/*
		**  XXX This is broken for SPACELEFT()==0
		**  However, SPACELEFT() is always > 0 unless MAXLINE==1.
		*/

		if (SPACELEFT(obuf, obp) - 1 < (size_t) l)
			l = SPACELEFT(obuf, obp) - 1;

		(void) sm_snprintf(obp, SPACELEFT(obuf, obp), "%.*s", l, v);
		if (!putxline(obuf, strlen(obuf), mci, putflags))
			goto writeerr;
		v += l + 1;
		obp = obuf;
		if (*v != ' ' && *v != '\t')
			*obp++ = ' ';
	}

	/* XXX This is broken for SPACELEFT()==0 */
	(void) sm_snprintf(obp, SPACELEFT(obuf, obp), "%.*s",
			   (int) (SPACELEFT(obuf, obp) - 1), v);
	return putxline(obuf, strlen(obuf), mci, putflags);

  writeerr:
	return false;
}

/*
**  COMMAIZE -- output a header field, making a comma-translated list.
**
**	Parameters:
**		h -- the header field to output.
**		p -- the value to put in it.
**		oldstyle -- true if this is an old style header.
**		mci -- the connection information.
**		e -- the envelope containing the message.
**		putflags -- flags for putxline()
**
**	Returns:
**		true iff header field was written successfully
**
**	Side Effects:
**		outputs "p" to "mci".
*/

bool
commaize(h, p, oldstyle, mci, e, putflags)
	register HDR *h;
	register char *p;
	bool oldstyle;
	register MCI *mci;
	register ENVELOPE *e;
	int putflags;
{
	register char *obp;
	int opos, omax, spaces;
	bool firstone = true;
	char **res;
	char obuf[MAXLINE + 3];

	/*
	**  Output the address list translated by the
	**  mailer and with commas.
	*/

	if (tTd(14, 2))
		sm_dprintf("commaize(%s:%s)\n", h->h_field, p);

	if (bitnset(M_7BITHDRS, mci->mci_mailer->m_flags))
		putflags |= PXLF_STRIP8BIT;

	obp = obuf;
	(void) sm_snprintf(obp, SPACELEFT(obuf, obp), "%.200s:", h->h_field);
	/* opos = strlen(obp); instead of the next 3 lines? */
	opos = strlen(h->h_field) + 1;
	if (opos > 201)
		opos = 201;
	obp += opos;

	spaces = 0;
	while (*p != '\0' && isascii(*p) && isspace(*p))
	{
		++spaces;
		++p;
	}
	if (spaces > 0)
	{
		SM_ASSERT(sizeof(obuf) > opos  * 2);

		/*
		**  Restrict number of spaces to half the length of buffer
		**  so the header field body can be put in here too.
		**  Note: this is a hack...
		*/

		if (spaces > sizeof(obuf) / 2)
			spaces = sizeof(obuf) / 2;
		(void) sm_snprintf(obp, SPACELEFT(obuf, obp), "%*s", spaces,
				"");
		opos += spaces;
		obp += spaces;
		SM_ASSERT(obp < &obuf[MAXLINE]);
	}

	omax = mci->mci_mailer->m_linelimit - 2;
	if (omax < 0 || omax > 78)
		omax = 78;

	/*
	**  Run through the list of values.
	*/

	while (*p != '\0')
	{
		register char *name;
		register int c;
		char savechar;
		int flags;
		auto int status;

		/*
		**  Find the end of the name.  New style names
		**  end with a comma, old style names end with
		**  a space character.  However, spaces do not
		**  necessarily delimit an old-style name -- at
		**  signs mean keep going.
		*/

		/* find end of name */
		while ((isascii(*p) && isspace(*p)) || *p == ',')
			p++;
		name = p;
		res = NULL;
		for (;;)
		{
			auto char *oldp;
			char pvpbuf[PSBUFSIZE];

			res = prescan(p, oldstyle ? ' ' : ',', pvpbuf,
				      sizeof(pvpbuf), &oldp, ExtTokenTab, false);
			p = oldp;
#if _FFR_IGNORE_BOGUS_ADDR
			/* ignore addresses that can't be parsed */
			if (res == NULL)
			{
				name = p;
				continue;
			}
#endif /* _FFR_IGNORE_BOGUS_ADDR */

			/* look to see if we have an at sign */
			while (*p != '\0' && isascii(*p) && isspace(*p))
				p++;

			if (*p != '@')
			{
				p = oldp;
				break;
			}
			++p;
			while (*p != '\0' && isascii(*p) && isspace(*p))
				p++;
		}
		/* at the end of one complete name */

		/* strip off trailing white space */
		while (p >= name &&
		       ((isascii(*p) && isspace(*p)) || *p == ',' || *p == '\0'))
			p--;
		if (++p == name)
			continue;

		/*
		**  if prescan() failed go a bit backwards; this is a hack,
		**  there should be some better error recovery.
		*/

		if (res == NULL && p > name &&
		    !((isascii(*p) && isspace(*p)) || *p == ',' || *p == '\0'))
			--p;
		savechar = *p;
		*p = '\0';

		/* translate the name to be relative */
		flags = RF_HEADERADDR|RF_ADDDOMAIN;
		if (bitset(H_FROM, h->h_flags))
			flags |= RF_SENDERADDR;
#if USERDB
		else if (e->e_from.q_mailer != NULL &&
			 bitnset(M_UDBRECIPIENT, e->e_from.q_mailer->m_flags))
		{
			char *q;

			q = udbsender(name, e->e_rpool);
			if (q != NULL)
				name = q;
		}
#endif /* USERDB */
		status = EX_OK;
		name = remotename(name, mci->mci_mailer, flags, &status, e);
		if (status != EX_OK && bitnset(M_xSMTP, mci->mci_mailer->m_flags))
		{
			if (status == EX_TEMPFAIL)
				mci->mci_flags |= MCIF_NOTSTICKY;
			goto writeerr;
		}
		if (*name == '\0')
		{
			*p = savechar;
			continue;
		}
		name = denlstring(name, false, true);

		/* output the name with nice formatting */
		opos += strlen(name);
		if (!firstone)
			opos += 2;
		if (opos > omax && !firstone)
		{
			(void) sm_strlcpy(obp, ",\n", SPACELEFT(obuf, obp));
			if (!putxline(obuf, strlen(obuf), mci, putflags))
				goto writeerr;
			obp = obuf;
			(void) sm_strlcpy(obp, "        ", sizeof(obuf));
			opos = strlen(obp);
			obp += opos;
			opos += strlen(name);
		}
		else if (!firstone)
		{
			(void) sm_strlcpy(obp, ", ", SPACELEFT(obuf, obp));
			obp += 2;
		}

		while ((c = *name++) != '\0' && obp < &obuf[MAXLINE])
			*obp++ = c;
		firstone = false;
		*p = savechar;
	}
	if (obp < &obuf[sizeof(obuf)])
		*obp = '\0';
	else
		obuf[sizeof(obuf) - 1] = '\0';
	return putxline(obuf, strlen(obuf), mci, putflags);

  writeerr:
	return false;
}

/*
**  COPYHEADER -- copy header list
**
**	This routine is the equivalent of newstr for header lists
**
**	Parameters:
**		header -- list of header structures to copy.
**		rpool -- resource pool, or NULL
**
**	Returns:
**		a copy of 'header'.
**
**	Side Effects:
**		none.
*/

HDR *
copyheader(header, rpool)
	register HDR *header;
	SM_RPOOL_T *rpool;
{
	register HDR *newhdr;
	HDR *ret;
	register HDR **tail = &ret;

	while (header != NULL)
	{
		newhdr = (HDR *) sm_rpool_malloc_x(rpool, sizeof(*newhdr));
		STRUCTCOPY(*header, *newhdr);
		*tail = newhdr;
		tail = &newhdr->h_link;
		header = header->h_link;
	}
	*tail = NULL;

	return ret;
}

/*
**  FIX_MIME_HEADER -- possibly truncate/rebalance parameters in a MIME header
**
**	Run through all of the parameters of a MIME header and
**	possibly truncate and rebalance the parameter according
**	to MaxMimeFieldLength.
**
**	Parameters:
**		h -- the header to truncate/rebalance
**		e -- the current envelope
**
**	Returns:
**		length of last offending field, 0 if all ok.
**
**	Side Effects:
**		string modified in place
*/

static size_t
fix_mime_header(h, e)
	HDR *h;
	ENVELOPE *e;
{
	char *begin = h->h_value;
	char *end;
	size_t len = 0;
	size_t retlen = 0;

	if (begin == NULL || *begin == '\0')
		return 0;

	/* Split on each ';' */
	/* find_character() never returns NULL */
	while ((end = find_character(begin, ';')) != NULL)
	{
		char save = *end;
		char *bp;

		*end = '\0';

		len = strlen(begin);

		/* Shorten individual parameter */
		if (shorten_rfc822_string(begin, MaxMimeFieldLength))
		{
			if (len < MaxMimeFieldLength)
			{
				/* we only rebalanced a bogus field */
				sm_syslog(LOG_ALERT, e->e_id,
					  "Fixed MIME %s header field (possible attack)",
					  h->h_field);
				if (tTd(34, 11))
					sm_dprintf("  fixed MIME %s header field (possible attack)\n",
						   h->h_field);
			}
			else
			{
				/* we actually shortened the header */
				retlen = len;
			}
		}

		/* Collapse the possibly shortened string with rest */
		bp = begin + strlen(begin);
		if (bp != end)
		{
			char *ep = end;

			*end = save;
			end = bp;

			/* copy character by character due to overlap */
			while (*ep != '\0')
				*bp++ = *ep++;
			*bp = '\0';
		}
		else
			*end = save;
		if (*end == '\0')
			break;

		/* Move past ';' */
		begin = end + 1;
	}
	return retlen;
}
