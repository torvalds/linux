/*
 * Copyright (c) 1998-2007, 2009 Proofpoint, Inc. and its suppliers.
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

SM_RCSID("@(#)$Id: util.c,v 8.427 2013-11-22 20:51:57 ca Exp $")

#include <sm/sendmail.h>
#include <sysexits.h>
#include <sm/xtrap.h>

/*
**  NEWSTR -- Create a copy of a C string
**
**	Parameters:
**		s -- the string to copy.
**
**	Returns:
**		pointer to newly allocated string.
*/

char *
newstr(s)
	const char *s;
{
	size_t l;
	char *n;

	l = strlen(s);
	SM_ASSERT(l + 1 > l);
	n = xalloc(l + 1);
	sm_strlcpy(n, s, l + 1);
	return n;
}

/*
**  ADDQUOTES -- Adds quotes & quote bits to a string.
**
**	Runs through a string and adds backslashes and quote bits.
**
**	Parameters:
**		s -- the string to modify.
**		rpool -- resource pool from which to allocate result
**
**	Returns:
**		pointer to quoted string.
*/

char *
addquotes(s, rpool)
	char *s;
	SM_RPOOL_T *rpool;
{
	int len = 0;
	char c;
	char *p = s, *q, *r;

	if (s == NULL)
		return NULL;

	/* Find length of quoted string */
	while ((c = *p++) != '\0')
	{
		len++;
		if (c == '\\' || c == '"')
			len++;
	}

	q = r = sm_rpool_malloc_x(rpool, len + 3);
	p = s;

	/* add leading quote */
	*q++ = '"';
	while ((c = *p++) != '\0')
	{
		/* quote \ or " */
		if (c == '\\' || c == '"')
			*q++ = '\\';
		*q++ = c;
	}
	*q++ = '"';
	*q = '\0';
	return r;
}

/*
**  STRIPBACKSLASH -- Strip all leading backslashes from a string, provided
**	the following character is alpha-numerical.
**
**	This is done in place.
**
**	Parameters:
**		s -- the string to strip.
**
**	Returns:
**		none.
*/

void
stripbackslash(s)
	char *s;
{
	char *p, *q, c;

	if (s == NULL || *s == '\0')
		return;
	p = q = s;
	while (*p == '\\' && (p[1] == '\\' || (isascii(p[1]) && isalnum(p[1]))))
		p++;
	do
	{
		c = *q++ = *p++;
	} while (c != '\0');
}

/*
**  RFC822_STRING -- Checks string for proper RFC822 string quoting.
**
**	Runs through a string and verifies RFC822 special characters
**	are only found inside comments, quoted strings, or backslash
**	escaped.  Also verified balanced quotes and parenthesis.
**
**	Parameters:
**		s -- the string to modify.
**
**	Returns:
**		true iff the string is RFC822 compliant, false otherwise.
*/

bool
rfc822_string(s)
	char *s;
{
	bool quoted = false;
	int commentlev = 0;
	char *c = s;

	if (s == NULL)
		return false;

	while (*c != '\0')
	{
		/* escaped character */
		if (*c == '\\')
		{
			c++;
			if (*c == '\0')
				return false;
		}
		else if (commentlev == 0 && *c == '"')
			quoted = !quoted;
		else if (!quoted)
		{
			if (*c == ')')
			{
				/* unbalanced ')' */
				if (commentlev == 0)
					return false;
				else
					commentlev--;
			}
			else if (*c == '(')
				commentlev++;
			else if (commentlev == 0 &&
				 strchr(MustQuoteChars, *c) != NULL)
				return false;
		}
		c++;
	}

	/* unbalanced '"' or '(' */
	return !quoted && commentlev == 0;
}

/*
**  SHORTEN_RFC822_STRING -- Truncate and rebalance an RFC822 string
**
**	Arbitrarily shorten (in place) an RFC822 string and rebalance
**	comments and quotes.
**
**	Parameters:
**		string -- the string to shorten
**		length -- the maximum size, 0 if no maximum
**
**	Returns:
**		true if string is changed, false otherwise
**
**	Side Effects:
**		Changes string in place, possibly resulting
**		in a shorter string.
*/

bool
shorten_rfc822_string(string, length)
	char *string;
	size_t length;
{
	bool backslash = false;
	bool modified = false;
	bool quoted = false;
	size_t slen;
	int parencount = 0;
	char *ptr = string;

	/*
	**  If have to rebalance an already short enough string,
	**  need to do it within allocated space.
	*/

	slen = strlen(string);
	if (length == 0 || slen < length)
		length = slen;

	while (*ptr != '\0')
	{
		if (backslash)
		{
			backslash = false;
			goto increment;
		}

		if (*ptr == '\\')
			backslash = true;
		else if (*ptr == '(')
		{
			if (!quoted)
				parencount++;
		}
		else if (*ptr == ')')
		{
			if (--parencount < 0)
				parencount = 0;
		}

		/* Inside a comment, quotes don't matter */
		if (parencount <= 0 && *ptr == '"')
			quoted = !quoted;

increment:
		/* Check for sufficient space for next character */
		if (length - (ptr - string) <= (size_t) ((backslash ? 1 : 0) +
						parencount +
						(quoted ? 1 : 0)))
		{
			/* Not enough, backtrack */
			if (*ptr == '\\')
				backslash = false;
			else if (*ptr == '(' && !quoted)
				parencount--;
			else if (*ptr == '"' && parencount == 0)
				quoted = false;
			break;
		}
		ptr++;
	}

	/* Rebalance */
	while (parencount-- > 0)
	{
		if (*ptr != ')')
		{
			modified = true;
			*ptr = ')';
		}
		ptr++;
	}
	if (quoted)
	{
		if (*ptr != '"')
		{
			modified = true;
			*ptr = '"';
		}
		ptr++;
	}
	if (*ptr != '\0')
	{
		modified = true;
		*ptr = '\0';
	}
	return modified;
}

/*
**  FIND_CHARACTER -- find an unquoted character in an RFC822 string
**
**	Find an unquoted, non-commented character in an RFC822
**	string and return a pointer to its location in the
**	string.
**
**	Parameters:
**		string -- the string to search
**		character -- the character to find
**
**	Returns:
**		pointer to the character, or
**		a pointer to the end of the line if character is not found
*/

char *
find_character(string, character)
	char *string;
	int character;
{
	bool backslash = false;
	bool quoted = false;
	int parencount = 0;

	while (string != NULL && *string != '\0')
	{
		if (backslash)
		{
			backslash = false;
			if (!quoted && character == '\\' && *string == '\\')
				break;
			string++;
			continue;
		}
		switch (*string)
		{
		  case '\\':
			backslash = true;
			break;

		  case '(':
			if (!quoted)
				parencount++;
			break;

		  case ')':
			if (--parencount < 0)
				parencount = 0;
			break;
		}

		/* Inside a comment, nothing matters */
		if (parencount > 0)
		{
			string++;
			continue;
		}

		if (*string == '"')
			quoted = !quoted;
		else if (*string == character && !quoted)
			break;
		string++;
	}

	/* Return pointer to the character */
	return string;
}

/*
**  CHECK_BODYTYPE -- check bodytype parameter
**
**	Parameters:
**		bodytype -- bodytype parameter
**
**	Returns:
**		BODYTYPE_* according to parameter
**
*/

int
check_bodytype(bodytype)
	char *bodytype;
{
	/* check body type for legality */
	if (bodytype == NULL)
		return BODYTYPE_NONE;
	if (sm_strcasecmp(bodytype, "7BIT") == 0)
		return BODYTYPE_7BIT;
	if (sm_strcasecmp(bodytype, "8BITMIME") == 0)
		return BODYTYPE_8BITMIME;
	return BODYTYPE_ILLEGAL;
}

/*
**  TRUNCATE_AT_DELIM -- truncate string at a delimiter and append "..."
**
**	Parameters:
**		str -- string to truncate
**		len -- maximum length (including '\0') (0 for unlimited)
**		delim -- delimiter character
**
**	Returns:
**		None.
*/

void
truncate_at_delim(str, len, delim)
	char *str;
	size_t len;
	int delim;
{
	char *p;

	if (str == NULL || len == 0 || strlen(str) < len)
		return;

	*(str + len - 1) = '\0';
	while ((p = strrchr(str, delim)) != NULL)
	{
		*p = '\0';
		if (p - str + 4 < len)
		{
			*p++ = (char) delim;
			*p = '\0';
			(void) sm_strlcat(str, "...", len);
			return;
		}
	}

	/* Couldn't find a place to append "..." */
	if (len > 3)
		(void) sm_strlcpy(str, "...", len);
	else
		str[0] = '\0';
}

/*
**  XALLOC -- Allocate memory, raise an exception on error
**
**	Parameters:
**		sz -- size of area to allocate.
**
**	Returns:
**		pointer to data region.
**
**	Exceptions:
**		SmHeapOutOfMemory (F:sm.heap) -- cannot allocate memory
**
**	Side Effects:
**		Memory is allocated.
*/

char *
#if SM_HEAP_CHECK
xalloc_tagged(sz, file, line)
	register int sz;
	char *file;
	int line;
#else /* SM_HEAP_CHECK */
xalloc(sz)
	register int sz;
#endif /* SM_HEAP_CHECK */
{
	register char *p;

	SM_REQUIRE(sz >= 0);

	/* some systems can't handle size zero mallocs */
	if (sz <= 0)
		sz = 1;

	/* scaffolding for testing error handling code */
	sm_xtrap_raise_x(&SmHeapOutOfMemory);

	p = sm_malloc_tagged((unsigned) sz, file, line, sm_heap_group());
	if (p == NULL)
	{
		sm_exc_raise_x(&SmHeapOutOfMemory);
	}
	return p;
}

/*
**  COPYPLIST -- copy list of pointers.
**
**	This routine is the equivalent of strdup for lists of
**	pointers.
**
**	Parameters:
**		list -- list of pointers to copy.
**			Must be NULL terminated.
**		copycont -- if true, copy the contents of the vector
**			(which must be a string) also.
**		rpool -- resource pool from which to allocate storage,
**			or NULL
**
**	Returns:
**		a copy of 'list'.
*/

char **
copyplist(list, copycont, rpool)
	char **list;
	bool copycont;
	SM_RPOOL_T *rpool;
{
	register char **vp;
	register char **newvp;

	for (vp = list; *vp != NULL; vp++)
		continue;

	vp++;

	newvp = (char **) sm_rpool_malloc_x(rpool, (vp - list) * sizeof(*vp));
	memmove((char *) newvp, (char *) list, (int) (vp - list) * sizeof(*vp));

	if (copycont)
	{
		for (vp = newvp; *vp != NULL; vp++)
			*vp = sm_rpool_strdup_x(rpool, *vp);
	}

	return newvp;
}

/*
**  COPYQUEUE -- copy address queue.
**
**	This routine is the equivalent of strdup for address queues;
**	addresses marked as QS_IS_DEAD() aren't copied
**
**	Parameters:
**		addr -- list of address structures to copy.
**		rpool -- resource pool from which to allocate storage
**
**	Returns:
**		a copy of 'addr'.
*/

ADDRESS *
copyqueue(addr, rpool)
	ADDRESS *addr;
	SM_RPOOL_T *rpool;
{
	register ADDRESS *newaddr;
	ADDRESS *ret;
	register ADDRESS **tail = &ret;

	while (addr != NULL)
	{
		if (!QS_IS_DEAD(addr->q_state))
		{
			newaddr = (ADDRESS *) sm_rpool_malloc_x(rpool,
							sizeof(*newaddr));
			STRUCTCOPY(*addr, *newaddr);
			*tail = newaddr;
			tail = &newaddr->q_next;
		}
		addr = addr->q_next;
	}
	*tail = NULL;

	return ret;
}

/*
**  LOG_SENDMAIL_PID -- record sendmail pid and command line.
**
**	Parameters:
**		e -- the current envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		writes pidfile, logs command line.
**		keeps file open and locked to prevent overwrite of active file
*/

static SM_FILE_T	*Pidf = NULL;

void
log_sendmail_pid(e)
	ENVELOPE *e;
{
	long sff;
	char pidpath[MAXPATHLEN];
	extern char *CommandLineArgs;

	/* write the pid to the log file for posterity */
	sff = SFF_NOLINK|SFF_ROOTOK|SFF_REGONLY|SFF_CREAT|SFF_NBLOCK;
	if (TrustedUid != 0 && RealUid == TrustedUid)
		sff |= SFF_OPENASROOT;
	expand(PidFile, pidpath, sizeof(pidpath), e);
	Pidf = safefopen(pidpath, O_WRONLY|O_TRUNC, FileMode, sff);
	if (Pidf == NULL)
	{
		if (errno == EWOULDBLOCK)
			sm_syslog(LOG_ERR, NOQID,
				  "unable to write pid to %s: file in use by another process",
				  pidpath);
		else
			sm_syslog(LOG_ERR, NOQID,
				  "unable to write pid to %s: %s",
				  pidpath, sm_errstring(errno));
	}
	else
	{
		PidFilePid = getpid();

		/* write the process id on line 1 */
		(void) sm_io_fprintf(Pidf, SM_TIME_DEFAULT, "%ld\n",
				     (long) PidFilePid);

		/* line 2 contains all command line flags */
		(void) sm_io_fprintf(Pidf, SM_TIME_DEFAULT, "%s\n",
				     CommandLineArgs);

		/* flush */
		(void) sm_io_flush(Pidf, SM_TIME_DEFAULT);

		/*
		**  Leave pid file open until process ends
		**  so it's not overwritten by another
		**  process.
		*/
	}
	if (LogLevel > 9)
		sm_syslog(LOG_INFO, NOQID, "started as: %s", CommandLineArgs);
}

/*
**  CLOSE_SENDMAIL_PID -- close sendmail pid file
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
*/

void
close_sendmail_pid()
{
	if (Pidf == NULL)
		return;

	(void) sm_io_close(Pidf, SM_TIME_DEFAULT);
	Pidf = NULL;
}

/*
**  SET_DELIVERY_MODE -- set and record the delivery mode
**
**	Parameters:
**		mode -- delivery mode
**		e -- the current envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		sets {deliveryMode} macro
*/

void
set_delivery_mode(mode, e)
	int mode;
	ENVELOPE *e;
{
	char buf[2];

	e->e_sendmode = (char) mode;
	buf[0] = (char) mode;
	buf[1] = '\0';
	macdefine(&e->e_macro, A_TEMP, macid("{deliveryMode}"), buf);
}

/*
**  SET_OP_MODE -- set and record the op mode
**
**	Parameters:
**		mode -- op mode
**		e -- the current envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		sets {opMode} macro
*/

void
set_op_mode(mode)
	int mode;
{
	char buf[2];
	extern ENVELOPE BlankEnvelope;

	OpMode = (char) mode;
	buf[0] = (char) mode;
	buf[1] = '\0';
	macdefine(&BlankEnvelope.e_macro, A_TEMP, MID_OPMODE, buf);
}

/*
**  PRINTAV -- print argument vector.
**
**	Parameters:
**		fp -- output file pointer.
**		av -- argument vector.
**
**	Returns:
**		none.
**
**	Side Effects:
**		prints av.
*/

void
printav(fp, av)
	SM_FILE_T *fp;
	char **av;
{
	while (*av != NULL)
	{
		if (tTd(0, 44))
			sm_dprintf("\n\t%08lx=", (unsigned long) *av);
		else
			(void) sm_io_putc(fp, SM_TIME_DEFAULT, ' ');
		if (tTd(0, 99))
			sm_dprintf("%s", str2prt(*av++));
		else
			xputs(fp, *av++);
	}
	(void) sm_io_putc(fp, SM_TIME_DEFAULT, '\n');
}

/*
**  XPUTS -- put string doing control escapes.
**
**	Parameters:
**		fp -- output file pointer.
**		s -- string to put.
**
**	Returns:
**		none.
**
**	Side Effects:
**		output to stdout
*/

void
xputs(fp, s)
	SM_FILE_T *fp;
	const char *s;
{
	int c;
	struct metamac *mp;
	bool shiftout = false;
	extern struct metamac MetaMacros[];
	static SM_DEBUG_T DebugANSI = SM_DEBUG_INITIALIZER("ANSI",
		"@(#)$Debug: ANSI - enable reverse video in debug output $");

	/*
	**  TermEscape is set here, rather than in main(),
	**  because ANSI mode can be turned on or off at any time
	**  if we are in -bt rule testing mode.
	*/

	if (sm_debug_unknown(&DebugANSI))
	{
		if (sm_debug_active(&DebugANSI, 1))
		{
			TermEscape.te_rv_on = "\033[7m";
			TermEscape.te_normal = "\033[0m";
		}
		else
		{
			TermEscape.te_rv_on = "";
			TermEscape.te_normal = "";
		}
	}

	if (s == NULL)
	{
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s<null>%s",
				     TermEscape.te_rv_on, TermEscape.te_normal);
		return;
	}
	while ((c = (*s++ & 0377)) != '\0')
	{
		if (shiftout)
		{
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s",
					     TermEscape.te_normal);
			shiftout = false;
		}
		if (!isascii(c) && !tTd(84, 1))
		{
			if (c == MATCHREPL)
			{
				(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
						     "%s$",
						     TermEscape.te_rv_on);
				shiftout = true;
				if (*s == '\0')
					continue;
				c = *s++ & 0377;
				goto printchar;
			}
			if (c == MACROEXPAND || c == MACRODEXPAND)
			{
				(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
						     "%s$",
						     TermEscape.te_rv_on);
				if (c == MACRODEXPAND)
					(void) sm_io_putc(fp,
							  SM_TIME_DEFAULT, '&');
				shiftout = true;
				if (*s == '\0')
					continue;
				if (strchr("=~&?", *s) != NULL)
					(void) sm_io_putc(fp,
							  SM_TIME_DEFAULT,
							  *s++);
				if (bitset(0200, *s))
					(void) sm_io_fprintf(fp,
							     SM_TIME_DEFAULT,
							     "{%s}",
							     macname(bitidx(*s++)));
				else
					(void) sm_io_fprintf(fp,
							     SM_TIME_DEFAULT,
							     "%c",
							     *s++);
				continue;
			}
			for (mp = MetaMacros; mp->metaname != '\0'; mp++)
			{
				if (bitidx(mp->metaval) == c)
				{
					(void) sm_io_fprintf(fp,
							     SM_TIME_DEFAULT,
							     "%s$%c",
							     TermEscape.te_rv_on,
							     mp->metaname);
					shiftout = true;
					break;
				}
			}
			if (c == MATCHCLASS || c == MATCHNCLASS)
			{
				if (bitset(0200, *s))
					(void) sm_io_fprintf(fp,
							     SM_TIME_DEFAULT,
							     "{%s}",
							     macname(bitidx(*s++)));
				else if (*s != '\0')
					(void) sm_io_fprintf(fp,
							     SM_TIME_DEFAULT,
							     "%c",
							     *s++);
			}
			if (mp->metaname != '\0')
				continue;

			/* unrecognized meta character */
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%sM-",
					     TermEscape.te_rv_on);
			shiftout = true;
			c &= 0177;
		}
  printchar:
		if (isascii(c) && isprint(c))
		{
			(void) sm_io_putc(fp, SM_TIME_DEFAULT, c);
			continue;
		}

		/* wasn't a meta-macro -- find another way to print it */
		switch (c)
		{
		  case '\n':
			c = 'n';
			break;

		  case '\r':
			c = 'r';
			break;

		  case '\t':
			c = 't';
			break;
		}
		if (!shiftout)
		{
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s",
					     TermEscape.te_rv_on);
			shiftout = true;
		}
		if (isascii(c) && isprint(c))
		{
			(void) sm_io_putc(fp, SM_TIME_DEFAULT, '\\');
			(void) sm_io_putc(fp, SM_TIME_DEFAULT, c);
		}
		else if (tTd(84, 2))
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, " %o ", c);
		else if (tTd(84, 1))
			(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, " %#x ", c);
		else if (!isascii(c) && !tTd(84, 1))
		{
			(void) sm_io_putc(fp, SM_TIME_DEFAULT, '^');
			(void) sm_io_putc(fp, SM_TIME_DEFAULT, c ^ 0100);
		}
	}
	if (shiftout)
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s",
				     TermEscape.te_normal);
	(void) sm_io_flush(fp, SM_TIME_DEFAULT);
}

/*
**  MAKELOWER -- Translate a line into lower case
**
**	Parameters:
**		p -- the string to translate.  If NULL, return is
**			immediate.
**
**	Returns:
**		none.
**
**	Side Effects:
**		String pointed to by p is translated to lower case.
*/

void
makelower(p)
	register char *p;
{
	register char c;

	if (p == NULL)
		return;
	for (; (c = *p) != '\0'; p++)
		if (isascii(c) && isupper(c))
			*p = tolower(c);
}

/*
**  FIXCRLF -- fix <CR><LF> in line.
**
**	Looks for the <CR><LF> combination and turns it into the
**	UNIX canonical <NL> character.  It only takes one line,
**	i.e., it is assumed that the first <NL> found is the end
**	of the line.
**
**	Parameters:
**		line -- the line to fix.
**		stripnl -- if true, strip the newline also.
**
**	Returns:
**		none.
**
**	Side Effects:
**		line is changed in place.
*/

void
fixcrlf(line, stripnl)
	char *line;
	bool stripnl;
{
	register char *p;

	p = strchr(line, '\n');
	if (p == NULL)
		return;
	if (p > line && p[-1] == '\r')
		p--;
	if (!stripnl)
		*p++ = '\n';
	*p = '\0';
}

/*
**  PUTLINE -- put a line like fputs obeying SMTP conventions
**
**	This routine always guarantees outputing a newline (or CRLF,
**	as appropriate) at the end of the string.
**
**	Parameters:
**		l -- line to put.
**		mci -- the mailer connection information.
**
**	Returns:
**		true iff line was written successfully
**
**	Side Effects:
**		output of l to mci->mci_out.
*/

bool
putline(l, mci)
	register char *l;
	register MCI *mci;
{
	return putxline(l, strlen(l), mci, PXLF_MAPFROM);
}

/*
**  PUTXLINE -- putline with flags bits.
**
**	This routine always guarantees outputing a newline (or CRLF,
**	as appropriate) at the end of the string.
**
**	Parameters:
**		l -- line to put.
**		len -- the length of the line.
**		mci -- the mailer connection information.
**		pxflags -- flag bits:
**		    PXLF_MAPFROM -- map From_ to >From_.
**		    PXLF_STRIP8BIT -- strip 8th bit.
**		    PXLF_HEADER -- map bare newline in header to newline space.
**		    PXLF_NOADDEOL -- don't add an EOL if one wasn't present.
**		    PXLF_STRIPMQUOTE -- strip METAQUOTE bytes.
**
**	Returns:
**		true iff line was written successfully
**
**	Side Effects:
**		output of l to mci->mci_out.
*/


#define PUTX(limit)							\
	do								\
	{								\
		quotenext = false;					\
		while (l < limit)					\
		{							\
			unsigned char c = (unsigned char) *l++;		\
									\
			if (bitset(PXLF_STRIPMQUOTE, pxflags) &&	\
			    !quotenext && c == METAQUOTE)		\
			{						\
				quotenext = true;			\
				continue;				\
			}						\
			quotenext = false;				\
			if (strip8bit)					\
				c &= 0177;				\
			if (sm_io_putc(mci->mci_out, SM_TIME_DEFAULT,	\
				       c) == SM_IO_EOF)			\
			{						\
				dead = true;				\
				break;					\
			}						\
			if (TrafficLogFile != NULL)			\
				(void) sm_io_putc(TrafficLogFile,	\
						  SM_TIME_DEFAULT,	\
						  c);			\
		}							\
	} while (0)

bool
putxline(l, len, mci, pxflags)
	register char *l;
	size_t len;
	register MCI *mci;
	int pxflags;
{
	register char *p, *end;
	int slop;
	bool dead, quotenext, strip8bit;

	/* strip out 0200 bits -- these can look like TELNET protocol */
	strip8bit = bitset(MCIF_7BIT, mci->mci_flags) ||
		    bitset(PXLF_STRIP8BIT, pxflags);
	dead = false;
	slop = 0;

	end = l + len;
	do
	{
		bool noeol = false;

		/* find the end of the line */
		p = memchr(l, '\n', end - l);
		if (p == NULL)
		{
			p = end;
			noeol = true;
		}

		if (TrafficLogFile != NULL)
			(void) sm_io_fprintf(TrafficLogFile, SM_TIME_DEFAULT,
					     "%05d >>> ", (int) CurrentPid);

		/* check for line overflow */
		while (mci->mci_mailer->m_linelimit > 0 &&
		       (p - l + slop) > mci->mci_mailer->m_linelimit)
		{
			register char *q = &l[mci->mci_mailer->m_linelimit - slop - 1];

			if (l[0] == '.' && slop == 0 &&
			    bitnset(M_XDOT, mci->mci_mailer->m_flags))
			{
				if (sm_io_putc(mci->mci_out, SM_TIME_DEFAULT,
					       '.') == SM_IO_EOF)
					dead = true;
				if (TrafficLogFile != NULL)
					(void) sm_io_putc(TrafficLogFile,
							  SM_TIME_DEFAULT, '.');
			}
			else if (l[0] == 'F' && slop == 0 &&
				 bitset(PXLF_MAPFROM, pxflags) &&
				 strncmp(l, "From ", 5) == 0 &&
				 bitnset(M_ESCFROM, mci->mci_mailer->m_flags))
			{
				if (sm_io_putc(mci->mci_out, SM_TIME_DEFAULT,
					       '>') == SM_IO_EOF)
					dead = true;
				if (TrafficLogFile != NULL)
					(void) sm_io_putc(TrafficLogFile,
							  SM_TIME_DEFAULT,
							  '>');
			}
			if (dead)
				break;

			PUTX(q);
			if (dead)
				break;

			if (sm_io_putc(mci->mci_out, SM_TIME_DEFAULT,
					'!') == SM_IO_EOF ||
			    sm_io_fputs(mci->mci_out, SM_TIME_DEFAULT,
					mci->mci_mailer->m_eol) == SM_IO_EOF ||
			    sm_io_putc(mci->mci_out, SM_TIME_DEFAULT,
					' ') == SM_IO_EOF)
			{
				dead = true;
				break;
			}
			if (TrafficLogFile != NULL)
			{
				(void) sm_io_fprintf(TrafficLogFile,
						     SM_TIME_DEFAULT,
						     "!\n%05d >>>  ",
						     (int) CurrentPid);
			}
			slop = 1;
		}

		if (dead)
			break;

		/* output last part */
		if (l[0] == '.' && slop == 0 &&
		    bitnset(M_XDOT, mci->mci_mailer->m_flags) &&
		    !bitset(MCIF_INLONGLINE, mci->mci_flags))
		{
			if (sm_io_putc(mci->mci_out, SM_TIME_DEFAULT, '.') ==
			    SM_IO_EOF)
			{
				dead = true;
				break;
			}
			if (TrafficLogFile != NULL)
				(void) sm_io_putc(TrafficLogFile,
						  SM_TIME_DEFAULT, '.');
		}
		else if (l[0] == 'F' && slop == 0 &&
			 bitset(PXLF_MAPFROM, pxflags) &&
			 strncmp(l, "From ", 5) == 0 &&
			 bitnset(M_ESCFROM, mci->mci_mailer->m_flags) &&
			 !bitset(MCIF_INLONGLINE, mci->mci_flags))
		{
			if (sm_io_putc(mci->mci_out, SM_TIME_DEFAULT, '>') ==
			    SM_IO_EOF)
			{
				dead = true;
				break;
			}
			if (TrafficLogFile != NULL)
				(void) sm_io_putc(TrafficLogFile,
						  SM_TIME_DEFAULT, '>');
		}
		PUTX(p);
		if (dead)
			break;

		if (TrafficLogFile != NULL)
			(void) sm_io_putc(TrafficLogFile, SM_TIME_DEFAULT,
					  '\n');
		if ((!bitset(PXLF_NOADDEOL, pxflags) || !noeol))
		{
			mci->mci_flags &= ~MCIF_INLONGLINE;
			if (sm_io_fputs(mci->mci_out, SM_TIME_DEFAULT,
					mci->mci_mailer->m_eol) == SM_IO_EOF)
			{
				dead = true;
				break;
			}
		}
		else
			mci->mci_flags |= MCIF_INLONGLINE;

		if (l < end && *l == '\n')
		{
			if (*++l != ' ' && *l != '\t' && *l != '\0' &&
			    bitset(PXLF_HEADER, pxflags))
			{
				if (sm_io_putc(mci->mci_out, SM_TIME_DEFAULT,
					       ' ') == SM_IO_EOF)
				{
					dead = true;
					break;
				}

				if (TrafficLogFile != NULL)
					(void) sm_io_putc(TrafficLogFile,
							  SM_TIME_DEFAULT, ' ');
			}
		}

	} while (l < end);
	return !dead;
}

/*
**  XUNLINK -- unlink a file, doing logging as appropriate.
**
**	Parameters:
**		f -- name of file to unlink.
**
**	Returns:
**		return value of unlink()
**
**	Side Effects:
**		f is unlinked.
*/

int
xunlink(f)
	char *f;
{
	register int i;
	int save_errno;

	if (LogLevel > 98)
		sm_syslog(LOG_DEBUG, CurEnv->e_id, "unlink %s", f);

	i = unlink(f);
	save_errno = errno;
	if (i < 0 && LogLevel > 97)
		sm_syslog(LOG_DEBUG, CurEnv->e_id, "%s: unlink-fail %d",
			  f, errno);
	if (i >= 0)
		SYNC_DIR(f, false);
	errno = save_errno;
	return i;
}

/*
**  SFGETS -- "safe" fgets -- times out and ignores random interrupts.
**
**	Parameters:
**		buf -- place to put the input line.
**		siz -- size of buf.
**		fp -- file to read from.
**		timeout -- the timeout before error occurs.
**		during -- what we are trying to read (for error messages).
**
**	Returns:
**		NULL on error (including timeout).  This may also leave
**			buf containing a null string.
**		buf otherwise.
*/


char *
sfgets(buf, siz, fp, timeout, during)
	char *buf;
	int siz;
	SM_FILE_T *fp;
	time_t timeout;
	char *during;
{
	register char *p;
	int save_errno, io_timeout, l;

	SM_REQUIRE(siz > 0);
	SM_REQUIRE(buf != NULL);

	if (fp == NULL)
	{
		buf[0] = '\0';
		errno = EBADF;
		return NULL;
	}

	/* try to read */
	l = -1;
	errno = 0;

	/* convert the timeout to sm_io notation */
	io_timeout = (timeout <= 0) ? SM_TIME_DEFAULT : timeout * 1000;
	while (!sm_io_eof(fp) && !sm_io_error(fp))
	{
		errno = 0;
		l = sm_io_fgets(fp, io_timeout, buf, siz);
		if (l < 0 && errno == EAGAIN)
		{
			/* The sm_io_fgets() call timedout */
			if (LogLevel > 1)
				sm_syslog(LOG_NOTICE, CurEnv->e_id,
					  "timeout waiting for input from %.100s during %s",
					  CURHOSTNAME,
					  during);
			buf[0] = '\0';
#if XDEBUG
			checkfd012(during);
#endif /* XDEBUG */
			if (TrafficLogFile != NULL)
				(void) sm_io_fprintf(TrafficLogFile,
						     SM_TIME_DEFAULT,
						     "%05d <<< [TIMEOUT]\n",
						     (int) CurrentPid);
			errno = ETIMEDOUT;
			return NULL;
		}
		if (l >= 0 || errno != EINTR)
			break;
		(void) sm_io_clearerr(fp);
	}
	save_errno = errno;

	/* clean up the books and exit */
	LineNumber++;
	if (l < 0)
	{
		buf[0] = '\0';
		if (TrafficLogFile != NULL)
			(void) sm_io_fprintf(TrafficLogFile, SM_TIME_DEFAULT,
					     "%05d <<< [EOF]\n",
					     (int) CurrentPid);
		errno = save_errno;
		return NULL;
	}
	if (TrafficLogFile != NULL)
		(void) sm_io_fprintf(TrafficLogFile, SM_TIME_DEFAULT,
				     "%05d <<< %s", (int) CurrentPid, buf);
	if (SevenBitInput)
	{
		for (p = buf; *p != '\0'; p++)
			*p &= ~0200;
	}
	else if (!HasEightBits)
	{
		for (p = buf; *p != '\0'; p++)
		{
			if (bitset(0200, *p))
			{
				HasEightBits = true;
				break;
			}
		}
	}
	return buf;
}

/*
**  FGETFOLDED -- like fgets, but knows about folded lines.
**
**	Parameters:
**		buf -- place to put result.
**		np -- pointer to bytes available; will be updated with
**			the actual buffer size (not number of bytes filled)
**			on return.
**		f -- file to read from.
**
**	Returns:
**		input line(s) on success, NULL on error or SM_IO_EOF.
**		This will normally be buf -- unless the line is too
**			long, when it will be sm_malloc_x()ed.
**
**	Side Effects:
**		buf gets lines from f, with continuation lines (lines
**		with leading white space) appended.  CRLF's are mapped
**		into single newlines.  Any trailing NL is stripped.
*/

char *
fgetfolded(buf, np, f)
	char *buf;
	int *np;
	SM_FILE_T *f;
{
	register char *p = buf;
	char *bp = buf;
	register int i;
	int n;

	SM_REQUIRE(np != NULL);
	n = *np;
	SM_REQUIRE(n > 0);
	SM_REQUIRE(buf != NULL);
	if (f == NULL)
	{
		buf[0] = '\0';
		errno = EBADF;
		return NULL;
	}

	n--;
	while ((i = sm_io_getc(f, SM_TIME_DEFAULT)) != SM_IO_EOF)
	{
		if (i == '\r')
		{
			i = sm_io_getc(f, SM_TIME_DEFAULT);
			if (i != '\n')
			{
				if (i != SM_IO_EOF)
					(void) sm_io_ungetc(f, SM_TIME_DEFAULT,
							    i);
				i = '\r';
			}
		}
		if (--n <= 0)
		{
			/* allocate new space */
			char *nbp;
			int nn;

			nn = (p - bp);
			if (nn < MEMCHUNKSIZE)
				nn *= 2;
			else
				nn += MEMCHUNKSIZE;
			nbp = sm_malloc_x(nn);
			memmove(nbp, bp, p - bp);
			p = &nbp[p - bp];
			if (bp != buf)
				sm_free(bp);
			bp = nbp;
			n = nn - (p - bp);
			*np = nn;
		}
		*p++ = i;
		if (i == '\n')
		{
			LineNumber++;
			i = sm_io_getc(f, SM_TIME_DEFAULT);
			if (i != SM_IO_EOF)
				(void) sm_io_ungetc(f, SM_TIME_DEFAULT, i);
			if (i != ' ' && i != '\t')
				break;
		}
	}
	if (p == bp)
		return NULL;
	if (p[-1] == '\n')
		p--;
	*p = '\0';
	return bp;
}

/*
**  CURTIME -- return current time.
**
**	Parameters:
**		none.
**
**	Returns:
**		the current time.
*/

time_t
curtime()
{
	auto time_t t;

	(void) time(&t);
	return t;
}

/*
**  ATOBOOL -- convert a string representation to boolean.
**
**	Defaults to false
**
**	Parameters:
**		s -- string to convert.  Takes "tTyY", empty, and NULL as true,
**			others as false.
**
**	Returns:
**		A boolean representation of the string.
*/

bool
atobool(s)
	register char *s;
{
	if (s == NULL || *s == '\0' || strchr("tTyY", *s) != NULL)
		return true;
	return false;
}

/*
**  ATOOCT -- convert a string representation to octal.
**
**	Parameters:
**		s -- string to convert.
**
**	Returns:
**		An integer representing the string interpreted as an
**		octal number.
*/

int
atooct(s)
	register char *s;
{
	register int i = 0;

	while (*s >= '0' && *s <= '7')
		i = (i << 3) | (*s++ - '0');
	return i;
}

/*
**  BITINTERSECT -- tell if two bitmaps intersect
**
**	Parameters:
**		a, b -- the bitmaps in question
**
**	Returns:
**		true if they have a non-null intersection
**		false otherwise
*/

bool
bitintersect(a, b)
	BITMAP256 a;
	BITMAP256 b;
{
	int i;

	for (i = BITMAPBYTES / sizeof(int); --i >= 0; )
	{
		if ((a[i] & b[i]) != 0)
			return true;
	}
	return false;
}

/*
**  BITZEROP -- tell if a bitmap is all zero
**
**	Parameters:
**		map -- the bit map to check
**
**	Returns:
**		true if map is all zero.
**		false if there are any bits set in map.
*/

bool
bitzerop(map)
	BITMAP256 map;
{
	int i;

	for (i = BITMAPBYTES / sizeof(int); --i >= 0; )
	{
		if (map[i] != 0)
			return false;
	}
	return true;
}

/*
**  STRCONTAINEDIN -- tell if one string is contained in another
**
**	Parameters:
**		icase -- ignore case?
**		a -- possible substring.
**		b -- possible superstring.
**
**	Returns:
**		true if a is contained in b (case insensitive).
**		false otherwise.
*/

bool
strcontainedin(icase, a, b)
	bool icase;
	register char *a;
	register char *b;
{
	int la;
	int lb;
	int c;

	la = strlen(a);
	lb = strlen(b);
	c = *a;
	if (icase && isascii(c) && isupper(c))
		c = tolower(c);
	for (; lb-- >= la; b++)
	{
		if (icase)
		{
			if (*b != c &&
			    isascii(*b) && isupper(*b) && tolower(*b) != c)
				continue;
			if (sm_strncasecmp(a, b, la) == 0)
				return true;
		}
		else
		{
			if (*b != c)
				continue;
			if (strncmp(a, b, la) == 0)
				return true;
		}
	}
	return false;
}

/*
**  CHECKFD012 -- check low numbered file descriptors
**
**	File descriptors 0, 1, and 2 should be open at all times.
**	This routine verifies that, and fixes it if not true.
**
**	Parameters:
**		where -- a tag printed if the assertion failed
**
**	Returns:
**		none
*/

void
checkfd012(where)
	char *where;
{
#if XDEBUG
	register int i;

	for (i = 0; i < 3; i++)
		fill_fd(i, where);
#endif /* XDEBUG */
}

/*
**  CHECKFDOPEN -- make sure file descriptor is open -- for extended debugging
**
**	Parameters:
**		fd -- file descriptor to check.
**		where -- tag to print on failure.
**
**	Returns:
**		none.
*/

void
checkfdopen(fd, where)
	int fd;
	char *where;
{
#if XDEBUG
	struct stat st;

	if (fstat(fd, &st) < 0 && errno == EBADF)
	{
		syserr("checkfdopen(%d): %s not open as expected!", fd, where);
		printopenfds(true);
	}
#endif /* XDEBUG */
}

/*
**  CHECKFDS -- check for new or missing file descriptors
**
**	Parameters:
**		where -- tag for printing.  If null, take a base line.
**
**	Returns:
**		none
**
**	Side Effects:
**		If where is set, shows changes since the last call.
*/

void
checkfds(where)
	char *where;
{
	int maxfd;
	register int fd;
	bool printhdr = true;
	int save_errno = errno;
	static BITMAP256 baseline;
	extern int DtableSize;

	if (DtableSize > BITMAPBITS)
		maxfd = BITMAPBITS;
	else
		maxfd = DtableSize;
	if (where == NULL)
		clrbitmap(baseline);

	for (fd = 0; fd < maxfd; fd++)
	{
		struct stat stbuf;

		if (fstat(fd, &stbuf) < 0 && errno != EOPNOTSUPP)
		{
			if (!bitnset(fd, baseline))
				continue;
			clrbitn(fd, baseline);
		}
		else if (!bitnset(fd, baseline))
			setbitn(fd, baseline);
		else
			continue;

		/* file state has changed */
		if (where == NULL)
			continue;
		if (printhdr)
		{
			sm_syslog(LOG_DEBUG, CurEnv->e_id,
				  "%s: changed fds:",
				  where);
			printhdr = false;
		}
		dumpfd(fd, true, true);
	}
	errno = save_errno;
}

/*
**  PRINTOPENFDS -- print the open file descriptors (for debugging)
**
**	Parameters:
**		logit -- if set, send output to syslog; otherwise
**			print for debugging.
**
**	Returns:
**		none.
*/

#if NETINET || NETINET6
# include <arpa/inet.h>
#endif /* NETINET || NETINET6 */

void
printopenfds(logit)
	bool logit;
{
	register int fd;
	extern int DtableSize;

	for (fd = 0; fd < DtableSize; fd++)
		dumpfd(fd, false, logit);
}

/*
**  DUMPFD -- dump a file descriptor
**
**	Parameters:
**		fd -- the file descriptor to dump.
**		printclosed -- if set, print a notification even if
**			it is closed; otherwise print nothing.
**		logit -- if set, use sm_syslog instead of sm_dprintf()
**
**	Returns:
**		none.
*/

void
dumpfd(fd, printclosed, logit)
	int fd;
	bool printclosed;
	bool logit;
{
	register char *p;
	char *hp;
#ifdef S_IFSOCK
	SOCKADDR sa;
#endif /* S_IFSOCK */
	auto SOCKADDR_LEN_T slen;
	int i;
#if STAT64 > 0
	struct stat64 st;
#else /* STAT64 > 0 */
	struct stat st;
#endif /* STAT64 > 0 */
	char buf[200];

	p = buf;
	(void) sm_snprintf(p, SPACELEFT(buf, p), "%3d: ", fd);
	p += strlen(p);

	if (
#if STAT64 > 0
	    fstat64(fd, &st)
#else /* STAT64 > 0 */
	    fstat(fd, &st)
#endif /* STAT64 > 0 */
	    < 0)
	{
		if (errno != EBADF)
		{
			(void) sm_snprintf(p, SPACELEFT(buf, p),
				"CANNOT STAT (%s)",
				sm_errstring(errno));
			goto printit;
		}
		else if (printclosed)
		{
			(void) sm_snprintf(p, SPACELEFT(buf, p), "CLOSED");
			goto printit;
		}
		return;
	}

	i = fcntl(fd, F_GETFL, 0);
	if (i != -1)
	{
		(void) sm_snprintf(p, SPACELEFT(buf, p), "fl=0x%x, ", i);
		p += strlen(p);
	}

	(void) sm_snprintf(p, SPACELEFT(buf, p), "mode=%o: ",
			(unsigned int) st.st_mode);
	p += strlen(p);
	switch (st.st_mode & S_IFMT)
	{
#ifdef S_IFSOCK
	  case S_IFSOCK:
		(void) sm_snprintf(p, SPACELEFT(buf, p), "SOCK ");
		p += strlen(p);
		memset(&sa, '\0', sizeof(sa));
		slen = sizeof(sa);
		if (getsockname(fd, &sa.sa, &slen) < 0)
			(void) sm_snprintf(p, SPACELEFT(buf, p), "(%s)",
				 sm_errstring(errno));
		else
		{
			hp = hostnamebyanyaddr(&sa);
			if (hp == NULL)
			{
				/* EMPTY */
				/* do nothing */
			}
# if NETINET
			else if (sa.sa.sa_family == AF_INET)
				(void) sm_snprintf(p, SPACELEFT(buf, p),
					"%s/%d", hp, ntohs(sa.sin.sin_port));
# endif /* NETINET */
# if NETINET6
			else if (sa.sa.sa_family == AF_INET6)
				(void) sm_snprintf(p, SPACELEFT(buf, p),
					"%s/%d", hp, ntohs(sa.sin6.sin6_port));
# endif /* NETINET6 */
			else
				(void) sm_snprintf(p, SPACELEFT(buf, p),
					"%s", hp);
		}
		p += strlen(p);
		(void) sm_snprintf(p, SPACELEFT(buf, p), "->");
		p += strlen(p);
		slen = sizeof(sa);
		if (getpeername(fd, &sa.sa, &slen) < 0)
			(void) sm_snprintf(p, SPACELEFT(buf, p), "(%s)",
					sm_errstring(errno));
		else
		{
			hp = hostnamebyanyaddr(&sa);
			if (hp == NULL)
			{
				/* EMPTY */
				/* do nothing */
			}
# if NETINET
			else if (sa.sa.sa_family == AF_INET)
				(void) sm_snprintf(p, SPACELEFT(buf, p),
					"%s/%d", hp, ntohs(sa.sin.sin_port));
# endif /* NETINET */
# if NETINET6
			else if (sa.sa.sa_family == AF_INET6)
				(void) sm_snprintf(p, SPACELEFT(buf, p),
					"%s/%d", hp, ntohs(sa.sin6.sin6_port));
# endif /* NETINET6 */
			else
				(void) sm_snprintf(p, SPACELEFT(buf, p),
					"%s", hp);
		}
		break;
#endif /* S_IFSOCK */

	  case S_IFCHR:
		(void) sm_snprintf(p, SPACELEFT(buf, p), "CHR: ");
		p += strlen(p);
		goto defprint;

#ifdef S_IFBLK
	  case S_IFBLK:
		(void) sm_snprintf(p, SPACELEFT(buf, p), "BLK: ");
		p += strlen(p);
		goto defprint;
#endif /* S_IFBLK */

#if defined(S_IFIFO) && (!defined(S_IFSOCK) || S_IFIFO != S_IFSOCK)
	  case S_IFIFO:
		(void) sm_snprintf(p, SPACELEFT(buf, p), "FIFO: ");
		p += strlen(p);
		goto defprint;
#endif /* defined(S_IFIFO) && (!defined(S_IFSOCK) || S_IFIFO != S_IFSOCK) */

#ifdef S_IFDIR
	  case S_IFDIR:
		(void) sm_snprintf(p, SPACELEFT(buf, p), "DIR: ");
		p += strlen(p);
		goto defprint;
#endif /* S_IFDIR */

#ifdef S_IFLNK
	  case S_IFLNK:
		(void) sm_snprintf(p, SPACELEFT(buf, p), "LNK: ");
		p += strlen(p);
		goto defprint;
#endif /* S_IFLNK */

	  default:
defprint:
		(void) sm_snprintf(p, SPACELEFT(buf, p),
			 "dev=%ld/%ld, ino=%llu, nlink=%d, u/gid=%ld/%ld, ",
			 (long) major(st.st_dev), (long) minor(st.st_dev),
			 (ULONGLONG_T) st.st_ino,
			 (int) st.st_nlink, (long) st.st_uid,
			 (long) st.st_gid);
		p += strlen(p);
		(void) sm_snprintf(p, SPACELEFT(buf, p), "size=%llu",
			 (ULONGLONG_T) st.st_size);
		break;
	}

printit:
	if (logit)
		sm_syslog(LOG_DEBUG, CurEnv ? CurEnv->e_id : NULL,
			  "%.800s", buf);
	else
		sm_dprintf("%s\n", buf);
}

/*
**  SHORTEN_HOSTNAME -- strip local domain information off of hostname.
**
**	Parameters:
**		host -- the host to shorten (stripped in place).
**
**	Returns:
**		place where string was truncated, NULL if not truncated.
*/

char *
shorten_hostname(host)
	char host[];
{
	register char *p;
	char *mydom;
	int i;
	bool canon = false;

	/* strip off final dot */
	i = strlen(host);
	p = &host[(i == 0) ? 0 : i - 1];
	if (*p == '.')
	{
		*p = '\0';
		canon = true;
	}

	/* see if there is any domain at all -- if not, we are done */
	p = strchr(host, '.');
	if (p == NULL)
		return NULL;

	/* yes, we have a domain -- see if it looks like us */
	mydom = macvalue('m', CurEnv);
	if (mydom == NULL)
		mydom = "";
	i = strlen(++p);
	if ((canon ? sm_strcasecmp(p, mydom)
		   : sm_strncasecmp(p, mydom, i)) == 0 &&
			(mydom[i] == '.' || mydom[i] == '\0'))
	{
		*--p = '\0';
		return p;
	}
	return NULL;
}

/*
**  PROG_OPEN -- open a program for reading
**
**	Parameters:
**		argv -- the argument list.
**		pfd -- pointer to a place to store the file descriptor.
**		e -- the current envelope.
**
**	Returns:
**		pid of the process -- -1 if it failed.
*/

pid_t
prog_open(argv, pfd, e)
	char **argv;
	int *pfd;
	ENVELOPE *e;
{
	pid_t pid;
	int save_errno;
	int sff;
	int ret;
	int fdv[2];
	char *p, *q;
	char buf[MAXPATHLEN];
	extern int DtableSize;

	if (pipe(fdv) < 0)
	{
		syserr("%s: cannot create pipe for stdout", argv[0]);
		return -1;
	}
	pid = fork();
	if (pid < 0)
	{
		syserr("%s: cannot fork", argv[0]);
		(void) close(fdv[0]);
		(void) close(fdv[1]);
		return -1;
	}
	if (pid > 0)
	{
		/* parent */
		(void) close(fdv[1]);
		*pfd = fdv[0];
		return pid;
	}

	/* Reset global flags */
	RestartRequest = NULL;
	RestartWorkGroup = false;
	ShutdownRequest = NULL;
	PendingSignal = 0;
	CurrentPid = getpid();

	/*
	**  Initialize exception stack and default exception
	**  handler for child process.
	*/

	sm_exc_newthread(fatal_error);

	/* child -- close stdin */
	(void) close(0);

	/* stdout goes back to parent */
	(void) close(fdv[0]);
	if (dup2(fdv[1], 1) < 0)
	{
		syserr("%s: cannot dup2 for stdout", argv[0]);
		_exit(EX_OSERR);
	}
	(void) close(fdv[1]);

	/* stderr goes to transcript if available */
	if (e->e_xfp != NULL)
	{
		int xfd;

		xfd = sm_io_getinfo(e->e_xfp, SM_IO_WHAT_FD, NULL);
		if (xfd >= 0 && dup2(xfd, 2) < 0)
		{
			syserr("%s: cannot dup2 for stderr", argv[0]);
			_exit(EX_OSERR);
		}
	}

	/* this process has no right to the queue file */
	if (e->e_lockfp != NULL)
	{
		int fd;

		fd = sm_io_getinfo(e->e_lockfp, SM_IO_WHAT_FD, NULL);
		if (fd >= 0)
			(void) close(fd);
		else
			syserr("%s: lockfp does not have a fd", argv[0]);
	}

	/* chroot to the program mailer directory, if defined */
	if (ProgMailer != NULL && ProgMailer->m_rootdir != NULL)
	{
		expand(ProgMailer->m_rootdir, buf, sizeof(buf), e);
		if (chroot(buf) < 0)
		{
			syserr("prog_open: cannot chroot(%s)", buf);
			exit(EX_TEMPFAIL);
		}
		if (chdir("/") < 0)
		{
			syserr("prog_open: cannot chdir(/)");
			exit(EX_TEMPFAIL);
		}
	}

	/* run as default user */
	endpwent();
	sm_mbdb_terminate();
#if _FFR_MEMSTAT
	(void) sm_memstat_close();
#endif /* _FFR_MEMSTAT */
	if (setgid(DefGid) < 0 && geteuid() == 0)
	{
		syserr("prog_open: setgid(%ld) failed", (long) DefGid);
		exit(EX_TEMPFAIL);
	}
	if (setuid(DefUid) < 0 && geteuid() == 0)
	{
		syserr("prog_open: setuid(%ld) failed", (long) DefUid);
		exit(EX_TEMPFAIL);
	}

	/* run in some directory */
	if (ProgMailer != NULL)
		p = ProgMailer->m_execdir;
	else
		p = NULL;
	for (; p != NULL; p = q)
	{
		q = strchr(p, ':');
		if (q != NULL)
			*q = '\0';
		expand(p, buf, sizeof(buf), e);
		if (q != NULL)
			*q++ = ':';
		if (buf[0] != '\0' && chdir(buf) >= 0)
			break;
	}
	if (p == NULL)
	{
		/* backup directories */
		if (chdir("/tmp") < 0)
			(void) chdir("/");
	}

	/* Check safety of program to be run */
	sff = SFF_ROOTOK|SFF_EXECOK;
	if (!bitnset(DBS_RUNWRITABLEPROGRAM, DontBlameSendmail))
		sff |= SFF_NOGWFILES|SFF_NOWWFILES;
	if (bitnset(DBS_RUNPROGRAMINUNSAFEDIRPATH, DontBlameSendmail))
		sff |= SFF_NOPATHCHECK;
	else
		sff |= SFF_SAFEDIRPATH;
	ret = safefile(argv[0], DefUid, DefGid, DefUser, sff, 0, NULL);
	if (ret != 0)
		sm_syslog(LOG_INFO, e->e_id,
			  "Warning: prog_open: program %s unsafe: %s",
			  argv[0], sm_errstring(ret));

	/* arrange for all the files to be closed */
	sm_close_on_exec(STDERR_FILENO + 1, DtableSize);

	/* now exec the process */
	(void) execve(argv[0], (ARGV_T) argv, (ARGV_T) UserEnviron);

	/* woops!  failed */
	save_errno = errno;
	syserr("%s: cannot exec", argv[0]);
	if (transienterror(save_errno))
		_exit(EX_OSERR);
	_exit(EX_CONFIG);
	return -1;	/* avoid compiler warning on IRIX */
}

/*
**  GET_COLUMN -- look up a Column in a line buffer
**
**	Parameters:
**		line -- the raw text line to search.
**		col -- the column number to fetch.
**		delim -- the delimiter between columns.  If null,
**			use white space.
**		buf -- the output buffer.
**		buflen -- the length of buf.
**
**	Returns:
**		buf if successful.
**		NULL otherwise.
*/

char *
get_column(line, col, delim, buf, buflen)
	char line[];
	int col;
	int delim;
	char buf[];
	int buflen;
{
	char *p;
	char *begin, *end;
	int i;
	char delimbuf[4];

	if ((char) delim == '\0')
		(void) sm_strlcpy(delimbuf, "\n\t ", sizeof(delimbuf));
	else
	{
		delimbuf[0] = (char) delim;
		delimbuf[1] = '\0';
	}

	p = line;
	if (*p == '\0')
		return NULL;			/* line empty */
	if (*p == (char) delim && col == 0)
		return NULL;			/* first column empty */

	begin = line;

	if (col == 0 && (char) delim == '\0')
	{
		while (*begin != '\0' && isascii(*begin) && isspace(*begin))
			begin++;
	}

	for (i = 0; i < col; i++)
	{
		if ((begin = strpbrk(begin, delimbuf)) == NULL)
			return NULL;		/* no such column */
		begin++;
		if ((char) delim == '\0')
		{
			while (*begin != '\0' && isascii(*begin) && isspace(*begin))
				begin++;
		}
	}

	end = strpbrk(begin, delimbuf);
	if (end == NULL)
		i = strlen(begin);
	else
		i = end - begin;
	if (i >= buflen)
		i = buflen - 1;
	(void) sm_strlcpy(buf, begin, i + 1);
	return buf;
}

/*
**  CLEANSTRCPY -- copy string keeping out bogus characters
**
**	Parameters:
**		t -- "to" string.
**		f -- "from" string.
**		l -- length of space available in "to" string.
**
**	Returns:
**		none.
*/

void
cleanstrcpy(t, f, l)
	register char *t;
	register char *f;
	int l;
{
	/* check for newlines and log if necessary */
	(void) denlstring(f, true, true);

	if (l <= 0)
		syserr("!cleanstrcpy: length == 0");

	l--;
	while (l > 0 && *f != '\0')
	{
		if (isascii(*f) &&
		    (isalnum(*f) || strchr("!#$%&'*+-./^_`{|}~", *f) != NULL))
		{
			l--;
			*t++ = *f;
		}
		f++;
	}
	*t = '\0';
}

/*
**  DENLSTRING -- convert newlines in a string to spaces
**
**	Parameters:
**		s -- the input string
**		strict -- if set, don't permit continuation lines.
**		logattacks -- if set, log attempted attacks.
**
**	Returns:
**		A pointer to a version of the string with newlines
**		mapped to spaces.  This should be copied.
*/

char *
denlstring(s, strict, logattacks)
	char *s;
	bool strict;
	bool logattacks;
{
	register char *p;
	int l;
	static char *bp = NULL;
	static int bl = 0;

	p = s;
	while ((p = strchr(p, '\n')) != NULL)
		if (strict || (*++p != ' ' && *p != '\t'))
			break;
	if (p == NULL)
		return s;

	l = strlen(s) + 1;
	if (bl < l)
	{
		/* allocate more space */
		char *nbp = sm_pmalloc_x(l);

		if (bp != NULL)
			sm_free(bp);
		bp = nbp;
		bl = l;
	}
	(void) sm_strlcpy(bp, s, l);
	for (p = bp; (p = strchr(p, '\n')) != NULL; )
		*p++ = ' ';

	if (logattacks)
	{
		sm_syslog(LOG_NOTICE, CurEnv ? CurEnv->e_id : NULL,
			  "POSSIBLE ATTACK from %.100s: newline in string \"%s\"",
			  RealHostName == NULL ? "[UNKNOWN]" : RealHostName,
			  shortenstring(bp, MAXSHORTSTR));
	}

	return bp;
}

/*
**  STRREPLNONPRT -- replace "unprintable" characters in a string with subst
**
**	Parameters:
**		s -- string to manipulate (in place)
**		subst -- character to use as replacement
**
**	Returns:
**		true iff string did not contain "unprintable" characters
*/

bool
strreplnonprt(s, c)
	char *s;
	int c;
{
	bool ok;

	ok = true;
	if (s == NULL)
		return ok;
	while (*s != '\0')
	{
		if (!(isascii(*s) && isprint(*s)))
		{
			*s = c;
			ok = false;
		}
		++s;
	}
	return ok;
}

/*
**  PATH_IS_DIR -- check to see if file exists and is a directory.
**
**	There are some additional checks for security violations in
**	here.  This routine is intended to be used for the host status
**	support.
**
**	Parameters:
**		pathname -- pathname to check for directory-ness.
**		createflag -- if set, create directory if needed.
**
**	Returns:
**		true -- if the indicated pathname is a directory
**		false -- otherwise
*/

bool
path_is_dir(pathname, createflag)
	char *pathname;
	bool createflag;
{
	struct stat statbuf;

#if HASLSTAT
	if (lstat(pathname, &statbuf) < 0)
#else /* HASLSTAT */
	if (stat(pathname, &statbuf) < 0)
#endif /* HASLSTAT */
	{
		if (errno != ENOENT || !createflag)
			return false;
		if (mkdir(pathname, 0755) < 0)
			return false;
		return true;
	}
	if (!S_ISDIR(statbuf.st_mode))
	{
		errno = ENOTDIR;
		return false;
	}

	/* security: don't allow writable directories */
	if (bitset(S_IWGRP|S_IWOTH, statbuf.st_mode))
	{
		errno = EACCES;
		return false;
	}
	return true;
}

/*
**  PROC_LIST_ADD -- add process id to list of our children
**
**	Parameters:
**		pid -- pid to add to list.
**		task -- task of pid.
**		type -- type of process.
**		count -- number of processes.
**		other -- other information for this type.
**
**	Returns:
**		none
**
**	Side Effects:
**		May increase CurChildren. May grow ProcList.
*/

typedef struct procs	PROCS_T;

struct procs
{
	pid_t		proc_pid;
	char		*proc_task;
	int		proc_type;
	int		proc_count;
	int		proc_other;
	SOCKADDR	proc_hostaddr;
};

static PROCS_T	*volatile ProcListVec = NULL;
static int	ProcListSize = 0;

void
proc_list_add(pid, task, type, count, other, hostaddr)
	pid_t pid;
	char *task;
	int type;
	int count;
	int other;
	SOCKADDR *hostaddr;
{
	int i;

	for (i = 0; i < ProcListSize; i++)
	{
		if (ProcListVec[i].proc_pid == NO_PID)
			break;
	}
	if (i >= ProcListSize)
	{
		/* probe the existing vector to avoid growing infinitely */
		proc_list_probe();

		/* now scan again */
		for (i = 0; i < ProcListSize; i++)
		{
			if (ProcListVec[i].proc_pid == NO_PID)
				break;
		}
	}
	if (i >= ProcListSize)
	{
		/* grow process list */
		int chldwasblocked;
		PROCS_T *npv;

		SM_ASSERT(ProcListSize < INT_MAX - PROC_LIST_SEG);
		npv = (PROCS_T *) sm_pmalloc_x((sizeof(*npv)) *
					       (ProcListSize + PROC_LIST_SEG));

		/* Block SIGCHLD so reapchild() doesn't mess with us */
		chldwasblocked = sm_blocksignal(SIGCHLD);
		if (ProcListSize > 0)
		{
			memmove(npv, ProcListVec,
				ProcListSize * sizeof(PROCS_T));
			sm_free(ProcListVec);
		}

		/* XXX just use memset() to initialize this part? */
		for (i = ProcListSize; i < ProcListSize + PROC_LIST_SEG; i++)
		{
			npv[i].proc_pid = NO_PID;
			npv[i].proc_task = NULL;
			npv[i].proc_type = PROC_NONE;
		}
		i = ProcListSize;
		ProcListSize += PROC_LIST_SEG;
		ProcListVec = npv;
		if (chldwasblocked == 0)
			(void) sm_releasesignal(SIGCHLD);
	}
	ProcListVec[i].proc_pid = pid;
	PSTRSET(ProcListVec[i].proc_task, task);
	ProcListVec[i].proc_type = type;
	ProcListVec[i].proc_count = count;
	ProcListVec[i].proc_other = other;
	if (hostaddr != NULL)
		ProcListVec[i].proc_hostaddr = *hostaddr;
	else
		memset(&ProcListVec[i].proc_hostaddr, 0,
			sizeof(ProcListVec[i].proc_hostaddr));

	/* if process adding itself, it's not a child */
	if (pid != CurrentPid)
	{
		SM_ASSERT(CurChildren < INT_MAX);
		CurChildren++;
	}
}

/*
**  PROC_LIST_SET -- set pid task in process list
**
**	Parameters:
**		pid -- pid to set
**		task -- task of pid
**
**	Returns:
**		none.
*/

void
proc_list_set(pid, task)
	pid_t pid;
	char *task;
{
	int i;

	for (i = 0; i < ProcListSize; i++)
	{
		if (ProcListVec[i].proc_pid == pid)
		{
			PSTRSET(ProcListVec[i].proc_task, task);
			break;
		}
	}
}

/*
**  PROC_LIST_DROP -- drop pid from process list
**
**	Parameters:
**		pid -- pid to drop
**		st -- process status
**		other -- storage for proc_other (return).
**
**	Returns:
**		none.
**
**	Side Effects:
**		May decrease CurChildren, CurRunners, or
**		set RestartRequest or ShutdownRequest.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

void
proc_list_drop(pid, st, other)
	pid_t pid;
	int st;
	int *other;
{
	int i;
	int type = PROC_NONE;

	for (i = 0; i < ProcListSize; i++)
	{
		if (ProcListVec[i].proc_pid == pid)
		{
			ProcListVec[i].proc_pid = NO_PID;
			type = ProcListVec[i].proc_type;
			if (other != NULL)
				*other = ProcListVec[i].proc_other;
			if (CurChildren > 0)
				CurChildren--;
			break;
		}
	}


	if (type == PROC_CONTROL && WIFEXITED(st))
	{
		/* if so, see if we need to restart or shutdown */
		if (WEXITSTATUS(st) == EX_RESTART)
			RestartRequest = "control socket";
		else if (WEXITSTATUS(st) == EX_SHUTDOWN)
			ShutdownRequest = "control socket";
	}
	else if (type == PROC_QUEUE_CHILD && !WIFSTOPPED(st) &&
		 ProcListVec[i].proc_other > -1)
	{
		/* restart this persistent runner */
		mark_work_group_restart(ProcListVec[i].proc_other, st);
	}
	else if (type == PROC_QUEUE)
	{
		CurRunners -= ProcListVec[i].proc_count;

		/* CHK_CUR_RUNNERS() can't be used here: uses syslog() */
		if (CurRunners < 0)
			CurRunners = 0;
	}
}

/*
**  PROC_LIST_CLEAR -- clear the process list
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets CurChildren to zero.
*/

void
proc_list_clear()
{
	int i;

	/* start from 1 since 0 is the daemon itself */
	for (i = 1; i < ProcListSize; i++)
		ProcListVec[i].proc_pid = NO_PID;
	CurChildren = 0;
}

/*
**  PROC_LIST_PROBE -- probe processes in the list to see if they still exist
**
**	Parameters:
**		none
**
**	Returns:
**		none
**
**	Side Effects:
**		May decrease CurChildren.
*/

void
proc_list_probe()
{
	int i, children;
	int chldwasblocked;
	pid_t pid;

	children = 0;
	chldwasblocked = sm_blocksignal(SIGCHLD);

	/* start from 1 since 0 is the daemon itself */
	for (i = 1; i < ProcListSize; i++)
	{
		pid = ProcListVec[i].proc_pid;
		if (pid == NO_PID || pid == CurrentPid)
			continue;
		if (kill(pid, 0) < 0)
		{
			if (LogLevel > 3)
				sm_syslog(LOG_DEBUG, CurEnv->e_id,
					  "proc_list_probe: lost pid %d",
					  (int) ProcListVec[i].proc_pid);
			ProcListVec[i].proc_pid = NO_PID;
			SM_FREE_CLR(ProcListVec[i].proc_task);

			if (ProcListVec[i].proc_type == PROC_QUEUE)
			{
				CurRunners -= ProcListVec[i].proc_count;
				CHK_CUR_RUNNERS("proc_list_probe", i,
						ProcListVec[i].proc_count);
			}

			CurChildren--;
		}
		else
		{
			++children;
		}
	}
	if (CurChildren < 0)
		CurChildren = 0;
	if (chldwasblocked == 0)
		(void) sm_releasesignal(SIGCHLD);
	if (LogLevel > 10 && children != CurChildren && CurrentPid == DaemonPid)
	{
		sm_syslog(LOG_ERR, NOQID,
			  "proc_list_probe: found %d children, expected %d",
			  children, CurChildren);
	}
}

/*
**  PROC_LIST_DISPLAY -- display the process list
**
**	Parameters:
**		out -- output file pointer
**		prefix -- string to output in front of each line.
**
**	Returns:
**		none.
*/

void
proc_list_display(out, prefix)
	SM_FILE_T *out;
	char *prefix;
{
	int i;

	for (i = 0; i < ProcListSize; i++)
	{
		if (ProcListVec[i].proc_pid == NO_PID)
			continue;

		(void) sm_io_fprintf(out, SM_TIME_DEFAULT, "%s%d %s%s\n",
				     prefix,
				     (int) ProcListVec[i].proc_pid,
				     ProcListVec[i].proc_task != NULL ?
				     ProcListVec[i].proc_task : "(unknown)",
				     (OpMode == MD_SMTP ||
				      OpMode == MD_DAEMON ||
				      OpMode == MD_ARPAFTP) ? "\r" : "");
	}
}

/*
**  PROC_LIST_SIGNAL -- send a signal to a type of process in the list
**
**	Parameters:
**		type -- type of process to signal
**		signal -- the type of signal to send
**
**	Results:
**		none.
**
**	NOTE:	THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
**		ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
**		DOING.
*/

void
proc_list_signal(type, signal)
	int type;
	int signal;
{
	int chldwasblocked;
	int alrmwasblocked;
	int i;
	pid_t mypid = getpid();

	/* block these signals so that we may signal cleanly */
	chldwasblocked = sm_blocksignal(SIGCHLD);
	alrmwasblocked = sm_blocksignal(SIGALRM);

	/* Find all processes of type and send signal */
	for (i = 0; i < ProcListSize; i++)
	{
		if (ProcListVec[i].proc_pid == NO_PID ||
		    ProcListVec[i].proc_pid == mypid)
			continue;
		if (ProcListVec[i].proc_type != type)
			continue;
		(void) kill(ProcListVec[i].proc_pid, signal);
	}

	/* restore the signals */
	if (alrmwasblocked == 0)
		(void) sm_releasesignal(SIGALRM);
	if (chldwasblocked == 0)
		(void) sm_releasesignal(SIGCHLD);
}

/*
**  COUNT_OPEN_CONNECTIONS
**
**	Parameters:
**		hostaddr - ClientAddress
**
**	Returns:
**		the number of open connections for this client
**
*/

int
count_open_connections(hostaddr)
	SOCKADDR *hostaddr;
{
	int i, n;

	if (hostaddr == NULL)
		return 0;

	/*
	**  This code gets called before proc_list_add() gets called,
	**  so we (the daemon child for this connection) have not yet
	**  counted ourselves.  Hence initialize the counter to 1
	**  instead of 0 to compensate.
	*/

	n = 1;
	for (i = 0; i < ProcListSize; i++)
	{
		if (ProcListVec[i].proc_pid == NO_PID)
			continue;
		if (hostaddr->sa.sa_family !=
		    ProcListVec[i].proc_hostaddr.sa.sa_family)
			continue;
#if NETINET
		if (hostaddr->sa.sa_family == AF_INET &&
		    (hostaddr->sin.sin_addr.s_addr ==
		     ProcListVec[i].proc_hostaddr.sin.sin_addr.s_addr))
			n++;
#endif /* NETINET */
#if NETINET6
		if (hostaddr->sa.sa_family == AF_INET6 &&
		    IN6_ARE_ADDR_EQUAL(&(hostaddr->sin6.sin6_addr),
				       &(ProcListVec[i].proc_hostaddr.sin6.sin6_addr)))
			n++;
#endif /* NETINET6 */
	}
	return n;
}

#if _FFR_XCNCT
/*
**  XCONNECT -- get X-CONNECT info
**
**	Parameters:
**		inchannel -- FILE to check
**
**	Returns:
**		-1 on error
**		0 if X-CONNECT was not given
**		>0 if X-CONNECT was used successfully (D_XCNCT*)
*/

int
xconnect(inchannel)
	SM_FILE_T *inchannel;
{
	int r, i;
	char *p, *b, delim, inp[MAXINPLINE];
	SOCKADDR addr;
	char **pvp;
	char pvpbuf[PSBUFSIZE];
	char *peerhostname;	/* name of SMTP peer or "localhost" */
	extern ENVELOPE BlankEnvelope;

#define XCONNECT "X-CONNECT "
#define XCNNCTLEN (sizeof(XCONNECT) - 1)

	/* Ask the ruleset whether to use x-connect */
	pvp = NULL;
	peerhostname = RealHostName;
	if (peerhostname == NULL)
		peerhostname = "localhost";
	r = rscap("x_connect", peerhostname,
		  anynet_ntoa(&RealHostAddr), &BlankEnvelope,
		  &pvp, pvpbuf, sizeof(pvpbuf));
	if (tTd(75, 8))
		sm_syslog(LOG_INFO, NOQID, "x-connect: rscap=%d", r);
	if (r == EX_UNAVAILABLE)
		return 0;
	if (r != EX_OK)
	{
		/* ruleset error */
		sm_syslog(LOG_INFO, NOQID, "x-connect: rscap=%d", r);
		return 0;
	}
	if (pvp != NULL && pvp[0] != NULL && (pvp[0][0] & 0377) == CANONNET)
	{
		/* $#: no x-connect */
		if (tTd(75, 7))
			sm_syslog(LOG_INFO, NOQID, "x-connect: nope");
		return 0;
	}

	p = sfgets(inp, sizeof(inp), InChannel, TimeOuts.to_nextcommand, "pre");
	if (tTd(75, 6))
		sm_syslog(LOG_INFO, NOQID, "x-connect: input=%s", p);
	if (p == NULL || strncasecmp(p, XCONNECT, XCNNCTLEN) != 0)
		return -1;
	p += XCNNCTLEN;
	while (isascii(*p) && isspace(*p))
		p++;

	/* parameters: IPAddress [Hostname[ M]] */
	b = p;
	while (*p != '\0' && isascii(*p) &&
	       (isalnum(*p) || *p == '.' || *p== ':'))
		p++;
	delim = *p;
	*p = '\0';

	memset(&addr, '\0', sizeof(addr));
	addr.sin.sin_addr.s_addr = inet_addr(b);
	if (addr.sin.sin_addr.s_addr != INADDR_NONE)
	{
		addr.sa.sa_family = AF_INET;
		memcpy(&RealHostAddr, &addr, sizeof(addr));
		if (tTd(75, 2))
			sm_syslog(LOG_INFO, NOQID, "x-connect: addr=%s",
				anynet_ntoa(&RealHostAddr));
	}
# if NETINET6
	else if ((r = inet_pton(AF_INET6, b, &addr.sin6.sin6_addr)) == 1)
	{
		addr.sa.sa_family = AF_INET6;
		memcpy(&RealHostAddr, &addr, sizeof(addr));
	}
# endif /* NETINET6 */
	else
		return -1;

	/* more parameters? */
	if (delim != ' ')
		return D_XCNCT;
	while (*p != '\0' && isascii(*p) && isspace(*p))
		p++;

	for (b = ++p, i = 0;
	     *p != '\0' && isascii(*p) && (isalnum(*p) || *p == '.' || *p == '-');
	     p++, i++)
		;
	if (i == 0)
		return D_XCNCT;
	delim = *p;
	if (i > MAXNAME)
		b[MAXNAME] = '\0';
	else
		b[i] = '\0';
	SM_FREE_CLR(RealHostName);
	RealHostName = newstr(b);
	if (tTd(75, 2))
		sm_syslog(LOG_INFO, NOQID, "x-connect: host=%s", b);
	*p = delim;

	b = p;
	if (*p != ' ')
		return D_XCNCT;

	while (*p != '\0' && isascii(*p) && isspace(*p))
		p++;

	if (tTd(75, 4))
	{
		char *e;

		e = strpbrk(p, "\r\n");
		if (e != NULL)
			*e = '\0';
		sm_syslog(LOG_INFO, NOQID, "x-connect: rest=%s", p);
	}
	if (*p == 'M')
		return D_XCNCT_M;

	return D_XCNCT;
}
#endif /* _FFR_XCNCT */
