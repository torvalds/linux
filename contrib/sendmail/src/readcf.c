/*
 * Copyright (c) 1998-2006, 2008-2010, 2013 Proofpoint, Inc. and its suppliers.
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

SM_RCSID("@(#)$Id: readcf.c,v 8.692 2013-11-22 20:51:56 ca Exp $")

#if NETINET || NETINET6
# include <arpa/inet.h>
#endif /* NETINET || NETINET6 */


#define SECONDS
#define MINUTES	* 60
#define HOUR	* 3600
#define HOURS	HOUR

static void	fileclass __P((int, char *, char *, bool, bool, bool));
static char	**makeargv __P((char *));
static void	settimeout __P((char *, char *, bool));
static void	toomany __P((int, int));
static char	*extrquotstr __P((char *, char **, char *, bool *));
static void	parse_class_words __P((int, char *));


#if _FFR_BOUNCE_QUEUE
static char *bouncequeue = NULL;
static void initbouncequeue __P((void));

/*
**  INITBOUNCEQUEUE -- determine BounceQueue if option is set.
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		sets BounceQueue
*/

static void
initbouncequeue()
{
	STAB *s;

	BounceQueue = NOQGRP;
	if (bouncequeue == NULL || bouncequeue[0] == '\0')
		return;

	s = stab(bouncequeue, ST_QUEUE, ST_FIND);
	if (s == NULL)
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
			"Warning: option BounceQueue: unknown queue group %s\n",
			bouncequeue);
	}
	else
		BounceQueue = s->s_quegrp->qg_index;
}
#endif /* _FFR_BOUNCE_QUEUE */

#if _FFR_RCPTFLAGS
void setupdynmailers __P((void));
#else
#define setupdynmailers()
#endif

/*
**  READCF -- read configuration file.
**
**	This routine reads the configuration file and builds the internal
**	form.
**
**	The file is formatted as a sequence of lines, each taken
**	atomically.  The first character of each line describes how
**	the line is to be interpreted.  The lines are:
**		Dxval		Define macro x to have value val.
**		Cxword		Put word into class x.
**		Fxfile [fmt]	Read file for lines to put into
**				class x.  Use scanf string 'fmt'
**				or "%s" if not present.  Fmt should
**				only produce one string-valued result.
**		Hname: value	Define header with field-name 'name'
**				and value as specified; this will be
**				macro expanded immediately before
**				use.
**		Sn		Use rewriting set n.
**		Rlhs rhs	Rewrite addresses that match lhs to
**				be rhs.
**		Mn arg=val...	Define mailer.  n is the internal name.
**				Args specify mailer parameters.
**		Oxvalue		Set option x to value.
**		O option value	Set option (long name) to value.
**		Pname=value	Set precedence name to value.
**		Qn arg=val...	Define queue groups.  n is the internal name.
**				Args specify queue parameters.
**		Vversioncode[/vendorcode]
**				Version level/vendor name of
**				configuration syntax.
**		Kmapname mapclass arguments....
**				Define keyed lookup of a given class.
**				Arguments are class dependent.
**		Eenvar=value	Set the environment value to the given value.
**
**	Parameters:
**		cfname -- configuration file name.
**		safe -- true if this is the system config file;
**			false otherwise.
**		e -- the main envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Builds several internal tables.
*/

void
readcf(cfname, safe, e)
	char *cfname;
	bool safe;
	register ENVELOPE *e;
{
	SM_FILE_T *cf;
	int ruleset = -1;
	char *q;
	struct rewrite *rwp = NULL;
	char *bp;
	auto char *ep;
	int nfuzzy;
	char *file;
	bool optional;
	bool ok;
	bool ismap;
	int mid;
	register char *p;
	long sff = SFF_OPENASROOT;
	struct stat statb;
	char buf[MAXLINE];
	int bufsize;
	char exbuf[MAXLINE];
	char pvpbuf[MAXLINE + MAXATOM];
	static char *null_list[1] = { NULL };
	extern unsigned char TokTypeNoC[];

	FileName = cfname;
	LineNumber = 0;

#if STARTTLS
	Srv_SSL_Options = SSL_OP_ALL;
	Clt_SSL_Options = SSL_OP_ALL
# ifdef SSL_OP_NO_SSLv2
		| SSL_OP_NO_SSLv2
# endif
# ifdef SSL_OP_NO_TICKET
		| SSL_OP_NO_TICKET
# endif
		;
# ifdef SSL_OP_TLSEXT_PADDING
	/* SSL_OP_TLSEXT_PADDING breaks compatibility with some sites */
	Srv_SSL_Options &= ~SSL_OP_TLSEXT_PADDING;
	Clt_SSL_Options &= ~SSL_OP_TLSEXT_PADDING;
# endif /* SSL_OP_TLSEXT_PADDING */
#endif /* STARTTLS */
	if (DontLockReadFiles)
		sff |= SFF_NOLOCK;
	cf = safefopen(cfname, O_RDONLY, 0444, sff);
	if (cf == NULL)
	{
		syserr("cannot open");
		finis(false, true, EX_OSFILE);
	}

	if (fstat(sm_io_getinfo(cf, SM_IO_WHAT_FD, NULL), &statb) < 0)
	{
		syserr("cannot fstat");
		finis(false, true, EX_OSFILE);
	}

	if (!S_ISREG(statb.st_mode))
	{
		syserr("not a plain file");
		finis(false, true, EX_OSFILE);
	}

	if (OpMode != MD_TEST && bitset(S_IWGRP|S_IWOTH, statb.st_mode))
	{
		if (OpMode == MD_DAEMON || OpMode == MD_INITALIAS || OpMode == MD_CHECKCONFIG)
			(void) sm_io_fprintf(smioerr, SM_TIME_DEFAULT,
					     "%s: WARNING: dangerous write permissions\n",
					     FileName);
		if (LogLevel > 0)
			sm_syslog(LOG_CRIT, NOQID,
				  "%s: WARNING: dangerous write permissions",
				  FileName);
	}

#if XLA
	xla_zero();
#endif /* XLA */

	while (bufsize = sizeof(buf),
	       (bp = fgetfolded(buf, &bufsize, cf)) != NULL)
	{
		char *nbp;

		if (bp[0] == '#')
		{
			if (bp != buf)
				sm_free(bp); /* XXX */
			continue;
		}

		/* do macro expansion mappings */
		nbp = translate_dollars(bp, bp, &bufsize);
		if (nbp != bp && bp != buf)
			sm_free(bp);
		bp = nbp;

		/* interpret this line */
		errno = 0;
		switch (bp[0])
		{
		  case '\0':
		  case '#':		/* comment */
			break;

		  case 'R':		/* rewriting rule */
			if (ruleset < 0)
			{
				syserr("missing valid ruleset for \"%s\"", bp);
				break;
			}
			for (p = &bp[1]; *p != '\0' && *p != '\t'; p++)
				continue;

			if (*p == '\0')
			{
				syserr("invalid rewrite line \"%s\" (tab expected)", bp);
				break;
			}

			/* allocate space for the rule header */
			if (rwp == NULL)
			{
				RewriteRules[ruleset] = rwp =
					(struct rewrite *) xalloc(sizeof(*rwp));
			}
			else
			{
				rwp->r_next = (struct rewrite *) xalloc(sizeof(*rwp));
				rwp = rwp->r_next;
			}
			rwp->r_next = NULL;

			/* expand and save the LHS */
			*p = '\0';
			expand(&bp[1], exbuf, sizeof(exbuf), e);
			rwp->r_lhs = prescan(exbuf, '\t', pvpbuf,
					     sizeof(pvpbuf), NULL,
					     ConfigLevel >= 9 ? TokTypeNoC : IntTokenTab,
					     true);
			nfuzzy = 0;
			if (rwp->r_lhs != NULL)
			{
				register char **ap;

				rwp->r_lhs = copyplist(rwp->r_lhs, true, NULL);

				/* count the number of fuzzy matches in LHS */
				for (ap = rwp->r_lhs; *ap != NULL; ap++)
				{
					char *botch;

					botch = NULL;
					switch (ap[0][0] & 0377)
					{
					  case MATCHZANY:
					  case MATCHANY:
					  case MATCHONE:
					  case MATCHCLASS:
					  case MATCHNCLASS:
						nfuzzy++;
						break;

					  case MATCHREPL:
						botch = "$1-$9";
						break;

					  case CANONUSER:
						botch = "$:";
						break;

					  case CALLSUBR:
						botch = "$>";
						break;

					  case CONDIF:
						botch = "$?";
						break;

					  case CONDFI:
						botch = "$.";
						break;

					  case HOSTBEGIN:
						botch = "$[";
						break;

					  case HOSTEND:
						botch = "$]";
						break;

					  case LOOKUPBEGIN:
						botch = "$(";
						break;

					  case LOOKUPEND:
						botch = "$)";
						break;
					}
					if (botch != NULL)
						syserr("Inappropriate use of %s on LHS",
							botch);
				}
				rwp->r_line = LineNumber;
			}
			else
			{
				syserr("R line: null LHS");
				rwp->r_lhs = null_list;
			}
			if (nfuzzy > MAXMATCH)
			{
				syserr("R line: too many wildcards");
				rwp->r_lhs = null_list;
			}

			/* expand and save the RHS */
			while (*++p == '\t')
				continue;
			q = p;
			while (*p != '\0' && *p != '\t')
				p++;
			*p = '\0';
			expand(q, exbuf, sizeof(exbuf), e);
			rwp->r_rhs = prescan(exbuf, '\t', pvpbuf,
					     sizeof(pvpbuf), NULL,
					     ConfigLevel >= 9 ? TokTypeNoC : IntTokenTab,
					     true);
			if (rwp->r_rhs != NULL)
			{
				register char **ap;
				int args, endtoken;
#if _FFR_EXTRA_MAP_CHECK
				int nexttoken;
#endif /* _FFR_EXTRA_MAP_CHECK */
				bool inmap;

				rwp->r_rhs = copyplist(rwp->r_rhs, true, NULL);

				/* check no out-of-bounds replacements */
				nfuzzy += '0';
				inmap = false;
				args = 0;
				endtoken = 0;
				for (ap = rwp->r_rhs; *ap != NULL; ap++)
				{
					char *botch;

					botch = NULL;
					switch (ap[0][0] & 0377)
					{
					  case MATCHREPL:
						if (ap[0][1] <= '0' ||
						    ap[0][1] > nfuzzy)
						{
							syserr("replacement $%c out of bounds",
								ap[0][1]);
						}
						break;

					  case MATCHZANY:
						botch = "$*";
						break;

					  case MATCHANY:
						botch = "$+";
						break;

					  case MATCHONE:
						botch = "$-";
						break;

					  case MATCHCLASS:
						botch = "$=";
						break;

					  case MATCHNCLASS:
						botch = "$~";
						break;

					  case CANONHOST:
						if (!inmap)
							break;
						if (++args >= MAX_MAP_ARGS)
							syserr("too many arguments for map lookup");
						break;

					  case HOSTBEGIN:
						endtoken = HOSTEND;
						/* FALLTHROUGH */
					  case LOOKUPBEGIN:
						/* see above... */
						if ((ap[0][0] & 0377) == LOOKUPBEGIN)
							endtoken = LOOKUPEND;
						if (inmap)
							syserr("cannot nest map lookups");
						inmap = true;
						args = 0;
#if _FFR_EXTRA_MAP_CHECK
						if (ap[1] == NULL)
						{
							syserr("syntax error in map lookup");
							break;
						}
						nexttoken = ap[1][0] & 0377;
						if (nexttoken == CANONHOST ||
						    nexttoken == CANONUSER ||
						    nexttoken == endtoken))
						{
							syserr("missing map name for lookup");
							break;
						}
						if (ap[2] == NULL)
						{
							syserr("syntax error in map lookup");
							break;
						}
						if (ap[0][0] == HOSTBEGIN)
							break;
						nexttoken = ap[2][0] & 0377;
						if (nexttoken == CANONHOST ||
						    nexttoken == CANONUSER ||
						    nexttoken == endtoken)
						{
							syserr("missing key name for lookup");
							break;
						}
#endif /* _FFR_EXTRA_MAP_CHECK */
						break;

					  case HOSTEND:
					  case LOOKUPEND:
						if ((ap[0][0] & 0377) != endtoken)
							break;
						inmap = false;
						endtoken = 0;
						break;


#if 0
/*
**  This doesn't work yet as there are maps defined *after* the cf
**  is read such as host, user, and alias.  So for now, it's removed.
**  When it comes back, the RELEASE_NOTES entry will be:
**	Emit warnings for unknown maps when reading the .cf file.  Based on
**		patch from Robert Harker of Harker Systems.
*/

					  case LOOKUPBEGIN:
						/*
						**  Got a database lookup,
						**  check if map is defined.
						*/

						ep = ap[1];
						if ((ep[0] & 0377) != MACRODEXPAND &&
						    stab(ep, ST_MAP, ST_FIND) == NULL)
						{
							(void) sm_io_fprintf(smioout,
									     SM_TIME_DEFAULT,
									     "Warning: %s: line %d: map %s not found\n",
									     FileName,
									     LineNumber,
									     ep);
						}
						break;
#endif /* 0 */
					}
					if (botch != NULL)
						syserr("Inappropriate use of %s on RHS",
							botch);
				}
				if (inmap)
					syserr("missing map closing token");
			}
			else
			{
				syserr("R line: null RHS");
				rwp->r_rhs = null_list;
			}
			break;

		  case 'S':		/* select rewriting set */
			expand(&bp[1], exbuf, sizeof(exbuf), e);
			ruleset = strtorwset(exbuf, NULL, ST_ENTER);
			if (ruleset < 0)
				break;

			rwp = RewriteRules[ruleset];
			if (rwp != NULL)
			{
				if (OpMode == MD_TEST || OpMode == MD_CHECKCONFIG)
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT,
							     "WARNING: Ruleset %s has multiple definitions\n",
							    &bp[1]);
				if (tTd(37, 1))
					sm_dprintf("WARNING: Ruleset %s has multiple definitions\n",
						   &bp[1]);
				while (rwp->r_next != NULL)
					rwp = rwp->r_next;
			}
			break;

		  case 'D':		/* macro definition */
			mid = macid_parse(&bp[1], &ep);
			if (mid == 0)
				break;
			p = munchstring(ep, NULL, '\0');
			macdefine(&e->e_macro, A_TEMP, mid, p);
			break;

		  case 'H':		/* required header line */
			(void) chompheader(&bp[1], CHHDR_DEF, NULL, e);
			break;

		  case 'C':		/* word class */
		  case 'T':		/* trusted user (set class `t') */
			if (bp[0] == 'C')
			{
				mid = macid_parse(&bp[1], &ep);
				if (mid == 0)
					break;
				expand(ep, exbuf, sizeof(exbuf), e);
				p = exbuf;
			}
			else
			{
				mid = 't';
				p = &bp[1];
			}
			while (*p != '\0')
			{
				register char *wd;
				char delim;

				while (*p != '\0' && isascii(*p) && isspace(*p))
					p++;
				wd = p;
				while (*p != '\0' && !(isascii(*p) && isspace(*p)))
					p++;
				delim = *p;
				*p = '\0';
				if (wd[0] != '\0')
					setclass(mid, wd);
				*p = delim;
			}
			break;

		  case 'F':		/* word class from file */
			mid = macid_parse(&bp[1], &ep);
			if (mid == 0)
				break;
			for (p = ep; isascii(*p) && isspace(*p); )
				p++;
			if (p[0] == '-' && p[1] == 'o')
			{
				optional = true;
				while (*p != '\0' &&
				       !(isascii(*p) && isspace(*p)))
					p++;
				while (isascii(*p) && isspace(*p))
					p++;
			}
			else
				optional = false;

			/* check if [key]@map:spec */
			ismap = false;
			if (!SM_IS_DIR_DELIM(*p) &&
			    *p != '|' &&
			    (q = strchr(p, '@')) != NULL)
			{
				q++;

				/* look for @LDAP or @map: in string */
				if (strcmp(q, "LDAP") == 0 ||
				    (*q != ':' &&
				     strchr(q, ':') != NULL))
					ismap = true;
			}

			if (ismap)
			{
				/* use entire spec */
				file = p;
			}
			else
			{
				file = extrquotstr(p, &q, " ", &ok);
				if (!ok)
				{
					syserr("illegal filename '%s'", p);
					break;
				}
			}

			if (*file == '|' || ismap)
				p = "%s";
			else
			{
				p = q;
				if (*p == '\0')
					p = "%s";
				else
				{
					*p = '\0';
					while (isascii(*++p) && isspace(*p))
						continue;
				}
			}
			fileclass(mid, file, p, ismap, safe, optional);
			break;

#if XLA
		  case 'L':		/* extended load average description */
			xla_init(&bp[1]);
			break;
#endif /* XLA */

#if defined(SUN_EXTENSIONS) && defined(SUN_LOOKUP_MACRO)
		  case 'L':		/* lookup macro */
		  case 'G':		/* lookup class */
			/* reserved for Sun -- NIS+ database lookup */
			if (VendorCode != VENDOR_SUN)
				goto badline;
			sun_lg_config_line(bp, e);
			break;
#endif /* defined(SUN_EXTENSIONS) && defined(SUN_LOOKUP_MACRO) */

		  case 'M':		/* define mailer */
			makemailer(&bp[1]);
			break;

		  case 'O':		/* set option */
			setoption(bp[1], &bp[2], safe, false, e);
			break;

		  case 'P':		/* set precedence */
			if (NumPriorities >= MAXPRIORITIES)
			{
				toomany('P', MAXPRIORITIES);
				break;
			}
			for (p = &bp[1]; *p != '\0' && *p != '='; p++)
				continue;
			if (*p == '\0')
				goto badline;
			*p = '\0';
			Priorities[NumPriorities].pri_name = newstr(&bp[1]);
			Priorities[NumPriorities].pri_val = atoi(++p);
			NumPriorities++;
			break;

		  case 'Q':		/* define queue */
			makequeue(&bp[1], true);
			break;

		  case 'V':		/* configuration syntax version */
			for (p = &bp[1]; isascii(*p) && isspace(*p); p++)
				continue;
			if (!isascii(*p) || !isdigit(*p))
			{
				syserr("invalid argument to V line: \"%.20s\"",
					&bp[1]);
				break;
			}
			ConfigLevel = strtol(p, &ep, 10);

			/*
			**  Do heuristic tweaking for back compatibility.
			*/

			if (ConfigLevel >= 5)
			{
				/* level 5 configs have short name in $w */
				p = macvalue('w', e);
				if (p != NULL && (p = strchr(p, '.')) != NULL)
				{
					*p = '\0';
					macdefine(&e->e_macro, A_TEMP, 'w',
						  macvalue('w', e));
				}
			}
			if (ConfigLevel >= 6)
			{
				ColonOkInAddr = false;
			}

			/*
			**  Look for vendor code.
			*/

			if (*ep++ == '/')
			{
				/* extract vendor code */
				for (p = ep; isascii(*p) && isalpha(*p); )
					p++;
				*p = '\0';

				if (!setvendor(ep))
					syserr("invalid V line vendor code: \"%s\"",
						ep);
			}
			break;

		  case 'K':
			expand(&bp[1], exbuf, sizeof(exbuf), e);
			(void) makemapentry(exbuf);
			break;

		  case 'E':
			p = strchr(bp, '=');
			if (p != NULL)
				*p++ = '\0';
			sm_setuserenv(&bp[1], p);
			break;

		  case 'X':		/* mail filter */
#if MILTER
			milter_setup(&bp[1]);
#else /* MILTER */
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Warning: Filter usage ('X') requires Milter support (-DMILTER)\n");
#endif /* MILTER */
			break;

		  default:
		  badline:
			syserr("unknown configuration line \"%s\"", bp);
		}
		if (bp != buf)
			sm_free(bp); /* XXX */
	}
	if (sm_io_error(cf))
	{
		syserr("I/O read error");
		finis(false, true, EX_OSFILE);
	}
	(void) sm_io_close(cf, SM_TIME_DEFAULT);
	FileName = NULL;

#if _FFR_BOUNCE_QUEUE
	initbouncequeue();
#endif

	/* initialize host maps from local service tables */
	inithostmaps();

	/* initialize daemon (if not defined yet) */
	initdaemon();

	/* determine if we need to do special name-server frotz */
	{
		int nmaps;
		char *maptype[MAXMAPSTACK];
		short mapreturn[MAXMAPACTIONS];

		nmaps = switch_map_find("hosts", maptype, mapreturn);
		UseNameServer = false;
		if (nmaps > 0 && nmaps <= MAXMAPSTACK)
		{
			register int mapno;

			for (mapno = 0; mapno < nmaps && !UseNameServer;
			     mapno++)
			{
				if (strcmp(maptype[mapno], "dns") == 0)
					UseNameServer = true;
			}
		}
	}
	setupdynmailers();
}

/*
**  TRANSLATE_DOLLARS -- convert $x into internal form
**
**	Actually does all appropriate pre-processing of a config line
**	to turn it into internal form.
**
**	Parameters:
**		ibp -- the buffer to translate.
**		obp -- where to put the translation; may be the same as obp
**		bsp -- a pointer to the size of obp; will be updated if
**			the buffer needs to be replaced.
**
**	Returns:
**		The buffer pointer; may differ from obp if the expansion
**		is larger then *bsp, in which case this will point to
**		malloc()ed memory which must be free()d by the caller.
*/

char *
translate_dollars(ibp, obp, bsp)
	char *ibp;
	char *obp;
	int *bsp;
{
	register char *p;
	auto char *ep;
	char *bp;

	if (tTd(37, 53))
	{
		sm_dprintf("translate_dollars(");
		xputs(sm_debug_file(), ibp);
		sm_dprintf(")\n");
	}

	bp = quote_internal_chars(ibp, obp, bsp);

	for (p = bp; *p != '\0'; p++)
	{
		if (*p == '#' && p > bp && ConfigLevel >= 3)
		{
			register char *e;

			switch (*--p & 0377)
			{
			  case MACROEXPAND:
				/* it's from $# -- let it go through */
				p++;
				break;

			  case '\\':
				/* it's backslash escaped */
				(void) sm_strlcpy(p, p + 1, strlen(p));
				break;

			  default:
				/* delete leading white space */
				while (isascii(*p) && isspace(*p) &&
				       *p != '\n' && p > bp)
				{
					p--;
				}
				if ((e = strchr(++p, '\n')) != NULL)
					(void) sm_strlcpy(p, e, strlen(p));
				else
					*p-- = '\0';
				break;
			}
			continue;
		}

		if (*p != '$' || p[1] == '\0')
			continue;

		if (p[1] == '$')
		{
			/* actual dollar sign.... */
			(void) sm_strlcpy(p, p + 1, strlen(p));
			continue;
		}

		/* convert to macro expansion character */
		*p++ = MACROEXPAND;

		/* special handling for $=, $~, $&, and $? */
		if (*p == '=' || *p == '~' || *p == '&' || *p == '?')
			p++;

		/* convert macro name to code */
		*p = macid_parse(p, &ep);
		if (ep != p + 1)
			(void) sm_strlcpy(p + 1, ep, strlen(p + 1));
	}

	/* strip trailing white space from the line */
	while (--p > bp && isascii(*p) && isspace(*p))
		*p = '\0';

	if (tTd(37, 53))
	{
		sm_dprintf("  translate_dollars => ");
		xputs(sm_debug_file(), bp);
		sm_dprintf("\n");
	}

	return bp;
}
/*
**  TOOMANY -- signal too many of some option
**
**	Parameters:
**		id -- the id of the error line
**		maxcnt -- the maximum possible values
**
**	Returns:
**		none.
**
**	Side Effects:
**		gives a syserr.
*/

static void
toomany(id, maxcnt)
	int id;
	int maxcnt;
{
	syserr("too many %c lines, %d max", id, maxcnt);
}
/*
**  FILECLASS -- read members of a class from a file
**
**	Parameters:
**		class -- class to define.
**		filename -- name of file to read.
**		fmt -- scanf string to use for match.
**		ismap -- if set, this is a map lookup.
**		safe -- if set, this is a safe read.
**		optional -- if set, it is not an error for the file to
**			not exist.
**
**	Returns:
**		none
**
**	Side Effects:
**		puts all lines in filename that match a scanf into
**			the named class.
*/

/*
**  Break up the match into words and add to class.
*/

static void
parse_class_words(class, line)
	int class;
	char *line;
{
	while (line != NULL && *line != '\0')
	{
		register char *q;

		/* strip leading spaces */
		while (isascii(*line) && isspace(*line))
			line++;
		if (*line == '\0')
			break;

		/* find the end of the word */
		q = line;
		while (*line != '\0' && !(isascii(*line) && isspace(*line)))
			line++;
		if (*line != '\0')
			*line++ = '\0';

		/* enter the word in the symbol table */
		setclass(class, q);
	}
}

static void
fileclass(class, filename, fmt, ismap, safe, optional)
	int class;
	char *filename;
	char *fmt;
	bool ismap;
	bool safe;
	bool optional;
{
	SM_FILE_T *f;
	long sff;
	pid_t pid;
	register char *p;
	char buf[MAXLINE];

	if (tTd(37, 2))
		sm_dprintf("fileclass(%s, fmt=%s)\n", filename, fmt);

	if (*filename == '\0')
	{
		syserr("fileclass: missing file name");
		return;
	}
	else if (ismap)
	{
		int status = 0;
		char *key;
		char *mn;
		char *cl, *spec;
		STAB *mapclass;
		MAP map;

		mn = newstr(macname(class));

		key = filename;

		/* skip past key */
		if ((p = strchr(filename, '@')) == NULL)
		{
			/* should not happen */
			syserr("fileclass: bogus map specification");
			sm_free(mn);
			return;
		}

		/* skip past '@' */
		*p++ = '\0';
		cl = p;

#if LDAPMAP
		if (strcmp(cl, "LDAP") == 0)
		{
			int n;
			char *lc;
			char jbuf[MAXHOSTNAMELEN];
			char lcbuf[MAXLINE];

			/* Get $j */
			expand("\201j", jbuf, sizeof(jbuf), &BlankEnvelope);
			if (jbuf[0] == '\0')
			{
				(void) sm_strlcpy(jbuf, "localhost",
						  sizeof(jbuf));
			}

			/* impose the default schema */
			lc = macvalue(macid("{sendmailMTACluster}"), CurEnv);
			if (lc == NULL)
				lc = "";
			else
			{
				expand(lc, lcbuf, sizeof(lcbuf), CurEnv);
				lc = lcbuf;
			}

			cl = "ldap";
			n = sm_snprintf(buf, sizeof(buf),
					"-k (&(objectClass=sendmailMTAClass)(sendmailMTAClassName=%s)(|(sendmailMTACluster=%s)(sendmailMTAHost=%s))) -v sendmailMTAClassValue,sendmailMTAClassSearch:FILTER:sendmailMTAClass,sendmailMTAClassURL:URL:sendmailMTAClass",
					mn, lc, jbuf);
			if (n >= sizeof(buf))
			{
				syserr("fileclass: F{%s}: Default LDAP string too long",
				       mn);
				sm_free(mn);
				return;
			}
			spec = buf;
		}
		else
#endif /* LDAPMAP */
		{
			if ((spec = strchr(cl, ':')) == NULL)
			{
				syserr("fileclass: F{%s}: missing map class",
				       mn);
				sm_free(mn);
				return;
			}
			*spec++ ='\0';
		}

		/* set up map structure */
		mapclass = stab(cl, ST_MAPCLASS, ST_FIND);
		if (mapclass == NULL)
		{
			syserr("fileclass: F{%s}: class %s not available",
			       mn, cl);
			sm_free(mn);
			return;
		}
		memset(&map, '\0', sizeof(map));
		map.map_class = &mapclass->s_mapclass;
		map.map_mname = mn;
		map.map_mflags |= MF_FILECLASS;

		if (tTd(37, 5))
			sm_dprintf("fileclass: F{%s}: map class %s, key %s, spec %s\n",
				   mn, cl, key, spec);


		/* parse map spec */
		if (!map.map_class->map_parse(&map, spec))
		{
			/* map_parse() showed the error already */
			sm_free(mn);
			return;
		}
		map.map_mflags |= MF_VALID;

		/* open map */
		if (map.map_class->map_open(&map, O_RDONLY))
		{
			map.map_mflags |= MF_OPEN;
			map.map_pid = getpid();
		}
		else
		{
			if (!optional &&
			    !bitset(MF_OPTIONAL, map.map_mflags))
				syserr("fileclass: F{%s}: map open failed",
				       mn);
			sm_free(mn);
			return;
		}

		/* lookup */
		p = (*map.map_class->map_lookup)(&map, key, NULL, &status);
		if (status != EX_OK && status != EX_NOTFOUND)
		{
			if (!optional)
				syserr("fileclass: F{%s}: map lookup failed",
				       mn);
			p = NULL;
		}

		/* use the results */
		if (p != NULL)
			parse_class_words(class, p);

		/* close map */
		map.map_mflags |= MF_CLOSING;
		map.map_class->map_close(&map);
		map.map_mflags &= ~(MF_OPEN|MF_WRITABLE|MF_CLOSING);
		sm_free(mn);
		return;
	}
	else if (filename[0] == '|')
	{
		auto int fd;
		int i;
		char *argv[MAXPV + 1];

		i = 0;
		for (p = strtok(&filename[1], " \t");
		     p != NULL && i < MAXPV;
		     p = strtok(NULL, " \t"))
			argv[i++] = p;
		argv[i] = NULL;
		pid = prog_open(argv, &fd, CurEnv);
		if (pid < 0)
			f = NULL;
		else
			f = sm_io_open(SmFtStdiofd, SM_TIME_DEFAULT,
				       (void *) &fd, SM_IO_RDONLY, NULL);
	}
	else
	{
		pid = -1;
		sff = SFF_REGONLY;
		if (!bitnset(DBS_CLASSFILEINUNSAFEDIRPATH, DontBlameSendmail))
			sff |= SFF_SAFEDIRPATH;
		if (!bitnset(DBS_LINKEDCLASSFILEINWRITABLEDIR,
			     DontBlameSendmail))
			sff |= SFF_NOWLINK;
		if (safe)
			sff |= SFF_OPENASROOT;
		else if (RealUid == 0)
			sff |= SFF_ROOTOK;
		if (DontLockReadFiles)
			sff |= SFF_NOLOCK;
		f = safefopen(filename, O_RDONLY, 0, sff);
	}
	if (f == NULL)
	{
		if (!optional)
			syserr("fileclass: cannot open '%s'", filename);
		return;
	}

	while (sm_io_fgets(f, SM_TIME_DEFAULT, buf, sizeof(buf)) >= 0)
	{
#if SCANF
		char wordbuf[MAXLINE + 1];
#endif /* SCANF */

		if (buf[0] == '#')
			continue;
#if SCANF
		if (sm_io_sscanf(buf, fmt, wordbuf) != 1)
			continue;
		p = wordbuf;
#else /* SCANF */
		p = buf;
#endif /* SCANF */

		parse_class_words(class, p);

		/*
		**  If anything else is added here,
		**  check if the '@' map case above
		**  needs the code as well.
		*/
	}

	(void) sm_io_close(f, SM_TIME_DEFAULT);
	if (pid > 0)
		(void) waitfor(pid);
}

#if _FFR_RCPTFLAGS
/* first character for dynamically created mailers */
static char dynmailerp = ' ';

/* list of first characters for cf defined mailers */
static char frst[MAXMAILERS + 1];

/*
**  SETUPDYNMAILERS -- find a char that isn't used as first element of any
**		mailer name.
**
**	Parameters:
**		none
**
**	Returns:
**		none
**	
**	Note: space is not valid in cf defined mailers hence the function
**		will always find a char. It's not nice, but this is for
**		internal names only.
*/

void
setupdynmailers()
{
	int i;
	char pp[] = "YXZ0123456789ABCDEFGHIJKLMNOPQRSTUVWyxzabcfghijkmnoqtuvw ";

	frst[MAXMAILERS] = '\0';
	for (i = 0; i < strlen(pp); i++)
	{
		if (strchr(frst, pp[i]) == NULL)
		{
			dynmailerp = pp[i];
			if (tTd(25, 8))
				sm_dprintf("dynmailerp=%c\n", dynmailerp);
			return;
		}
	}

	/* NOTREACHED */
	SM_ASSERT(0);
}

/*
**  NEWMODMAILER -- Create a new mailer with modifications
**
**	Parameters:
**		rcpt -- current RCPT
**		fl -- flag to set
**
**	Returns:
**		true iff successful.
**
**	Note: this creates a copy of the mailer for the rcpt and
**		modifies exactly one flag.  It does not work
**		for multiple flags!
*/

bool
newmodmailer(rcpt, fl)
	ADDRESS *rcpt;
	int fl;
{
	int idx;
	struct mailer *m;
	STAB *s;
	char mname[256];

	SM_REQUIRE(rcpt != NULL);
	if (rcpt->q_mailer == NULL)
		return false;
	if (tTd(25, 8))
		sm_dprintf("newmodmailer: rcpt=%s\n", rcpt->q_paddr);
	SM_REQUIRE(rcpt->q_mailer->m_name != NULL);
	SM_REQUIRE(rcpt->q_mailer->m_name[0] != '\0');
	sm_strlcpy(mname, rcpt->q_mailer->m_name, sizeof(mname));
	mname[0] = dynmailerp;
	if (tTd(25, 8))
		sm_dprintf("newmodmailer: name=%s\n", mname);
	s = stab(mname, ST_MAILER, ST_ENTER);
	if (s->s_mailer != NULL)
	{
		idx = s->s_mailer->m_mno;
		if (tTd(25, 6))
			sm_dprintf("newmodmailer: found idx=%d\n", idx);
	}
	else
	{
		idx = rcpt->q_mailer->m_mno;
		idx += MAXMAILERS;
		if (tTd(25, 6))
			sm_dprintf("newmodmailer: idx=%d\n", idx);
		if (idx > SM_ARRAY_SIZE(Mailer))
			return false;
	}

	m = Mailer[idx];
	if (m == NULL)
		m = (struct mailer *) xalloc(sizeof(*m));
	memset((char *) m, '\0', sizeof(*m));
	STRUCTCOPY(*rcpt->q_mailer, *m);
	Mailer[idx] = m;

	/* "modify" the mailer */
	setbitn(bitidx(fl), m->m_flags);
	rcpt->q_mailer = m;
	m->m_mno = idx;
	m->m_name = newstr(mname);
	if (tTd(25, 1))
		sm_dprintf("newmodmailer: mailer[%d]=%s %p\n",
			idx, Mailer[idx]->m_name, Mailer[idx]);

	return true;
}

#endif /* _FFR_RCPTFLAGS */

/*
**  MAKEMAILER -- define a new mailer.
**
**	Parameters:
**		line -- description of mailer.  This is in labeled
**			fields.  The fields are:
**			   A -- the argv for this mailer
**			   C -- the character set for MIME conversions
**			   D -- the directory to run in
**			   E -- the eol string
**			   F -- the flags associated with the mailer
**			   L -- the maximum line length
**			   M -- the maximum message size
**			   N -- the niceness at which to run
**			   P -- the path to the mailer
**			   Q -- the queue group for the mailer
**			   R -- the recipient rewriting set
**			   S -- the sender rewriting set
**			   T -- the mailer type (for DSNs)
**			   U -- the uid to run as
**			   W -- the time to wait at the end
**			   m -- maximum messages per connection
**			   r -- maximum number of recipients per message
**			   / -- new root directory
**			The first word is the canonical name of the mailer.
**
**	Returns:
**		none.
**
**	Side Effects:
**		enters the mailer into the mailer table.
*/


void
makemailer(line)
	char *line;
{
	register char *p;
	register struct mailer *m;
	register STAB *s;
	int i;
	char fcode;
	auto char *endp;
	static int nextmailer = 0;	/* "free" index into Mailer struct */

	/* allocate a mailer and set up defaults */
	m = (struct mailer *) xalloc(sizeof(*m));
	memset((char *) m, '\0', sizeof(*m));
	errno = 0; /* avoid bogus error text */

	/* collect the mailer name */
	for (p = line;
	     *p != '\0' && *p != ',' && !(isascii(*p) && isspace(*p));
	     p++)
		continue;
	if (*p != '\0')
		*p++ = '\0';
	if (line[0] == '\0')
	{
		syserr("name required for mailer");
		return;
	}
	m->m_name = newstr(line);
#if _FFR_RCPTFLAGS
	frst[nextmailer] = line[0];
#endif
	m->m_qgrp = NOQGRP;
	m->m_uid = NO_UID;
	m->m_gid = NO_GID;

	/* now scan through and assign info from the fields */
	while (*p != '\0')
	{
		auto char *delimptr;

		while (*p != '\0' &&
		       (*p == ',' || (isascii(*p) && isspace(*p))))
			p++;

		/* p now points to field code */
		fcode = *p;
		while (*p != '\0' && *p != '=' && *p != ',')
			p++;
		if (*p++ != '=')
		{
			syserr("mailer %s: `=' expected", m->m_name);
			return;
		}
		while (isascii(*p) && isspace(*p))
			p++;

		/* p now points to the field body */
		p = munchstring(p, &delimptr, ',');

		/* install the field into the mailer struct */
		switch (fcode)
		{
		  case 'P':		/* pathname */
			if (*p != '\0')	/* error is issued below */
				m->m_mailer = newstr(p);
			break;

		  case 'F':		/* flags */
			for (; *p != '\0'; p++)
			{
				if (!(isascii(*p) && isspace(*p)))
				{
					if (*p == M_INTERNAL)
						sm_syslog(LOG_WARNING, NOQID,
							  "WARNING: mailer=%s, flag=%c deprecated",
							  m->m_name, *p);
					setbitn(bitidx(*p), m->m_flags);
				}
			}
			break;

		  case 'S':		/* sender rewriting ruleset */
		  case 'R':		/* recipient rewriting ruleset */
			i = strtorwset(p, &endp, ST_ENTER);
			if (i < 0)
				return;
			if (fcode == 'S')
				m->m_sh_rwset = m->m_se_rwset = i;
			else
				m->m_rh_rwset = m->m_re_rwset = i;

			p = endp;
			if (*p++ == '/')
			{
				i = strtorwset(p, NULL, ST_ENTER);
				if (i < 0)
					return;
				if (fcode == 'S')
					m->m_sh_rwset = i;
				else
					m->m_rh_rwset = i;
			}
			break;

		  case 'E':		/* end of line string */
			if (*p == '\0')
				syserr("mailer %s: null end-of-line string",
					m->m_name);
			else
				m->m_eol = newstr(p);
			break;

		  case 'A':		/* argument vector */
			if (*p != '\0')	/* error is issued below */
				m->m_argv = makeargv(p);
			break;

		  case 'M':		/* maximum message size */
			m->m_maxsize = atol(p);
			break;

		  case 'm':		/* maximum messages per connection */
			m->m_maxdeliveries = atoi(p);
			break;

		  case 'r':		/* max recipient per envelope */
			m->m_maxrcpt = atoi(p);
			break;

		  case 'L':		/* maximum line length */
			m->m_linelimit = atoi(p);
			if (m->m_linelimit < 0)
				m->m_linelimit = 0;
			break;

		  case 'N':		/* run niceness */
			m->m_nice = atoi(p);
			break;

		  case 'D':		/* working directory */
			if (*p == '\0')
				syserr("mailer %s: null working directory",
					m->m_name);
			else
				m->m_execdir = newstr(p);
			break;

		  case 'C':		/* default charset */
			if (*p == '\0')
				syserr("mailer %s: null charset", m->m_name);
			else
				m->m_defcharset = newstr(p);
			break;

		  case 'Q':		/* queue for this mailer */
			if (*p == '\0')
			{
				syserr("mailer %s: null queue", m->m_name);
				break;
			}
			s = stab(p, ST_QUEUE, ST_FIND);
			if (s == NULL)
				syserr("mailer %s: unknown queue %s",
					m->m_name, p);
			else
				m->m_qgrp = s->s_quegrp->qg_index;
			break;

		  case 'T':		/* MTA-Name/Address/Diagnostic types */
			/* extract MTA name type; default to "dns" */
			m->m_mtatype = newstr(p);
			p = strchr(m->m_mtatype, '/');
			if (p != NULL)
			{
				*p++ = '\0';
				if (*p == '\0')
					p = NULL;
			}
			if (*m->m_mtatype == '\0')
				m->m_mtatype = "dns";

			/* extract address type; default to "rfc822" */
			m->m_addrtype = p;
			if (p != NULL)
				p = strchr(p, '/');
			if (p != NULL)
			{
				*p++ = '\0';
				if (*p == '\0')
					p = NULL;
			}
			if (m->m_addrtype == NULL || *m->m_addrtype == '\0')
				m->m_addrtype = "rfc822";

			/* extract diagnostic type; default to "smtp" */
			m->m_diagtype = p;
			if (m->m_diagtype == NULL || *m->m_diagtype == '\0')
				m->m_diagtype = "smtp";
			break;

		  case 'U':		/* user id */
			if (isascii(*p) && !isdigit(*p))
			{
				char *q = p;
				struct passwd *pw;

				while (*p != '\0' && isascii(*p) &&
# if _FFR_DOTTED_USERNAMES
				       (isalnum(*p) || strchr(SM_PWN_CHARS, *p) != NULL))
# else /* _FFR_DOTTED_USERNAMES */
				       (isalnum(*p) || strchr("-_", *p) != NULL))
# endif /* _FFR_DOTTED_USERNAMES */
					p++;
				while (isascii(*p) && isspace(*p))
					*p++ = '\0';
				if (*p != '\0')
					*p++ = '\0';
				if (*q == '\0')
				{
					syserr("mailer %s: null user name",
						m->m_name);
					break;
				}
				pw = sm_getpwnam(q);
				if (pw == NULL)
				{
					syserr("readcf: mailer U= flag: unknown user %s", q);
					break;
				}
				else
				{
					m->m_uid = pw->pw_uid;
					m->m_gid = pw->pw_gid;
				}
			}
			else
			{
				auto char *q;

				m->m_uid = strtol(p, &q, 0);
				p = q;
				while (isascii(*p) && isspace(*p))
					p++;
				if (*p != '\0')
					p++;
			}
			while (isascii(*p) && isspace(*p))
				p++;
			if (*p == '\0')
				break;
			if (isascii(*p) && !isdigit(*p))
			{
				char *q = p;
				struct group *gr;

				while (isascii(*p) &&
				       (isalnum(*p) || strchr(SM_PWN_CHARS, *p) != NULL))
					p++;
				*p++ = '\0';
				if (*q == '\0')
				{
					syserr("mailer %s: null group name",
						m->m_name);
					break;
				}
				gr = getgrnam(q);
				if (gr == NULL)
				{
					syserr("readcf: mailer U= flag: unknown group %s", q);
					break;
				}
				else
					m->m_gid = gr->gr_gid;
			}
			else
			{
				m->m_gid = strtol(p, NULL, 0);
			}
			break;

		  case 'W':		/* wait timeout */
			m->m_wait = convtime(p, 's');
			break;

		  case '/':		/* new root directory */
			if (*p == '\0')
				syserr("mailer %s: null root directory",
					m->m_name);
			else
				m->m_rootdir = newstr(p);
			break;

		  default:
			syserr("M%s: unknown mailer equate %c=",
			       m->m_name, fcode);
			break;
		}

		p = delimptr;
	}

#if !HASRRESVPORT
	if (bitnset(M_SECURE_PORT, m->m_flags))
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "M%s: Warning: F=%c set on system that doesn't support rresvport()\n",
				     m->m_name, M_SECURE_PORT);
	}
#endif /* !HASRRESVPORT */

#if !HASNICE
	if (m->m_nice != 0)
	{
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "M%s: Warning: N= set on system that doesn't support nice()\n",
				     m->m_name);
	}
#endif /* !HASNICE */

	/* do some rationality checking */
	if (m->m_argv == NULL)
	{
		syserr("M%s: A= argument required", m->m_name);
		return;
	}
	if (m->m_mailer == NULL)
	{
		syserr("M%s: P= argument required", m->m_name);
		return;
	}

	if (nextmailer >= MAXMAILERS)
	{
		syserr("too many mailers defined (%d max)", MAXMAILERS);
		return;
	}

	if (m->m_maxrcpt <= 0)
		m->m_maxrcpt = DEFAULT_MAX_RCPT;

	/* do some heuristic cleanup for back compatibility */
	if (bitnset(M_LIMITS, m->m_flags))
	{
		if (m->m_linelimit == 0)
			m->m_linelimit = SMTPLINELIM;
		if (ConfigLevel < 2)
			setbitn(M_7BITS, m->m_flags);
	}

	if (strcmp(m->m_mailer, "[TCP]") == 0)
	{
		syserr("M%s: P=[TCP] must be replaced by P=[IPC]", m->m_name);
		return;
	}

	if (strcmp(m->m_mailer, "[IPC]") == 0)
	{
		/* Use the second argument for host or path to socket */
		if (m->m_argv[0] == NULL || m->m_argv[1] == NULL ||
		    m->m_argv[1][0] == '\0')
		{
			syserr("M%s: too few parameters for %s mailer",
			       m->m_name, m->m_mailer);
			return;
		}
		if (strcmp(m->m_argv[0], "TCP") != 0
#if NETUNIX
		    && strcmp(m->m_argv[0], "FILE") != 0
#endif /* NETUNIX */
		    )
		{
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "M%s: Warning: first argument in %s mailer must be %s\n",
					     m->m_name, m->m_mailer,
#if NETUNIX
					     "TCP or FILE"
#else /* NETUNIX */
					     "TCP"
#endif /* NETUNIX */
				     );
		}
		if (m->m_mtatype == NULL)
			m->m_mtatype = "dns";
		if (m->m_addrtype == NULL)
			m->m_addrtype = "rfc822";
		if (m->m_diagtype == NULL)
		{
			if (m->m_argv[0] != NULL &&
			    strcmp(m->m_argv[0], "FILE") == 0)
				m->m_diagtype = "x-unix";
			else
				m->m_diagtype = "smtp";
		}
	}
	else if (strcmp(m->m_mailer, "[FILE]") == 0)
	{
		/* Use the second argument for filename */
		if (m->m_argv[0] == NULL || m->m_argv[1] == NULL ||
		    m->m_argv[2] != NULL)
		{
			syserr("M%s: too %s parameters for [FILE] mailer",
			       m->m_name,
			       (m->m_argv[0] == NULL ||
				m->m_argv[1] == NULL) ? "few" : "many");
			return;
		}
		else if (strcmp(m->m_argv[0], "FILE") != 0)
		{
			syserr("M%s: first argument in [FILE] mailer must be FILE",
			       m->m_name);
			return;
		}
	}

	if (m->m_eol == NULL)
	{
		char **pp;

		/* default for SMTP is \r\n; use \n for local delivery */
		for (pp = m->m_argv; *pp != NULL; pp++)
		{
			for (p = *pp; *p != '\0'; )
			{
				if ((*p++ & 0377) == MACROEXPAND && *p == 'u')
					break;
			}
			if (*p != '\0')
				break;
		}
		if (*pp == NULL)
			m->m_eol = "\r\n";
		else
			m->m_eol = "\n";
	}

	/* enter the mailer into the symbol table */
	s = stab(m->m_name, ST_MAILER, ST_ENTER);
	if (s->s_mailer != NULL)
	{
		i = s->s_mailer->m_mno;
		sm_free(s->s_mailer); /* XXX */
	}
	else
	{
		i = nextmailer++;
	}
	Mailer[i] = s->s_mailer = m;
	m->m_mno = i;
}
/*
**  MUNCHSTRING -- translate a string into internal form.
**
**	Parameters:
**		p -- the string to munch.
**		delimptr -- if non-NULL, set to the pointer of the
**			field delimiter character.
**		delim -- the delimiter for the field.
**
**	Returns:
**		the munched string.
**
**	Side Effects:
**		the munched string is a local static buffer.
**		it must be copied before the function is called again.
*/

char *
munchstring(p, delimptr, delim)
	register char *p;
	char **delimptr;
	int delim;
{
	register char *q;
	bool backslash = false;
	bool quotemode = false;
	static char buf[MAXLINE];

	for (q = buf; *p != '\0' && q < &buf[sizeof(buf) - 1]; p++)
	{
		if (backslash)
		{
			/* everything is roughly literal */
			backslash = false;
			switch (*p)
			{
			  case 'r':		/* carriage return */
				*q++ = '\r';
				continue;

			  case 'n':		/* newline */
				*q++ = '\n';
				continue;

			  case 'f':		/* form feed */
				*q++ = '\f';
				continue;

			  case 'b':		/* backspace */
				*q++ = '\b';
				continue;
			}
			*q++ = *p;
		}
		else
		{
			if (*p == '\\')
				backslash = true;
			else if (*p == '"')
				quotemode = !quotemode;
			else if (quotemode || *p != delim)
				*q++ = *p;
			else
				break;
		}
	}

	if (delimptr != NULL)
		*delimptr = p;
	*q++ = '\0';
	return buf;
}
/*
**  EXTRQUOTSTR -- extract a (quoted) string.
**
**	This routine deals with quoted (") strings and escaped
**	spaces (\\ ).
**
**	Parameters:
**		p -- source string.
**		delimptr -- if non-NULL, set to the pointer of the
**			field delimiter character.
**		delimbuf -- delimiters for the field.
**		st -- if non-NULL, store the return value (whether the
**			string was correctly quoted) here.
**
**	Returns:
**		the extracted string.
**
**	Side Effects:
**		the returned string is a local static buffer.
**		it must be copied before the function is called again.
*/

static char *
extrquotstr(p, delimptr, delimbuf, st)
	register char *p;
	char **delimptr;
	char *delimbuf;
	bool *st;
{
	register char *q;
	bool backslash = false;
	bool quotemode = false;
	static char buf[MAXLINE];

	for (q = buf; *p != '\0' && q < &buf[sizeof(buf) - 1]; p++)
	{
		if (backslash)
		{
			backslash = false;
			if (*p != ' ')
				*q++ = '\\';
		}
		if (*p == '\\')
			backslash = true;
		else if (*p == '"')
			quotemode = !quotemode;
		else if (quotemode ||
			 strchr(delimbuf, (int) *p) == NULL)
			*q++ = *p;
		else
			break;
	}

	if (delimptr != NULL)
		*delimptr = p;
	*q++ = '\0';
	if (st != NULL)
		*st = !(quotemode || backslash);
	return buf;
}
/*
**  MAKEARGV -- break up a string into words
**
**	Parameters:
**		p -- the string to break up.
**
**	Returns:
**		a char **argv (dynamically allocated)
**
**	Side Effects:
**		munges p.
*/

static char **
makeargv(p)
	register char *p;
{
	char *q;
	int i;
	char **avp;
	char *argv[MAXPV + 1];

	/* take apart the words */
	i = 0;
	while (*p != '\0' && i < MAXPV)
	{
		q = p;
		while (*p != '\0' && !(isascii(*p) && isspace(*p)))
			p++;
		while (isascii(*p) && isspace(*p))
			*p++ = '\0';
		argv[i++] = newstr(q);
	}
	argv[i++] = NULL;

	/* now make a copy of the argv */
	avp = (char **) xalloc(sizeof(*avp) * i);
	memmove((char *) avp, (char *) argv, sizeof(*avp) * i);

	return avp;
}
/*
**  PRINTRULES -- print rewrite rules (for debugging)
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
**
**	Side Effects:
**		prints rewrite rules.
*/

void
printrules()
{
	register struct rewrite *rwp;
	register int ruleset;

	for (ruleset = 0; ruleset < 10; ruleset++)
	{
		if (RewriteRules[ruleset] == NULL)
			continue;
		sm_dprintf("\n----Rule Set %d:", ruleset);

		for (rwp = RewriteRules[ruleset]; rwp != NULL; rwp = rwp->r_next)
		{
			sm_dprintf("\nLHS:");
			printav(sm_debug_file(), rwp->r_lhs);
			sm_dprintf("RHS:");
			printav(sm_debug_file(), rwp->r_rhs);
		}
	}
}
/*
**  PRINTMAILER -- print mailer structure (for debugging)
**
**	Parameters:
**		fp -- output file
**		m -- the mailer to print
**
**	Returns:
**		none.
*/

void
printmailer(fp, m)
	SM_FILE_T *fp;
	register MAILER *m;
{
	int j;

	(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
			     "mailer %d (%s): P=%s S=", m->m_mno, m->m_name,
			     m->m_mailer);
	if (RuleSetNames[m->m_se_rwset] == NULL)
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%d/",
				     m->m_se_rwset);
	else
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s/",
				     RuleSetNames[m->m_se_rwset]);
	if (RuleSetNames[m->m_sh_rwset] == NULL)
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%d R=",
				     m->m_sh_rwset);
	else
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s R=",
				     RuleSetNames[m->m_sh_rwset]);
	if (RuleSetNames[m->m_re_rwset] == NULL)
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%d/",
				     m->m_re_rwset);
	else
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s/",
				     RuleSetNames[m->m_re_rwset]);
	if (RuleSetNames[m->m_rh_rwset] == NULL)
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%d ",
				     m->m_rh_rwset);
	else
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "%s ",
				     RuleSetNames[m->m_rh_rwset]);
	(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "M=%ld U=%d:%d F=",
			     m->m_maxsize, (int) m->m_uid, (int) m->m_gid);
	for (j = '\0'; j <= '\177'; j++)
		if (bitnset(j, m->m_flags))
			(void) sm_io_putc(fp, SM_TIME_DEFAULT, j);
	(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, " L=%d E=",
			     m->m_linelimit);
	xputs(fp, m->m_eol);
	if (m->m_defcharset != NULL)
		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, " C=%s",
				     m->m_defcharset);
	(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, " T=%s/%s/%s",
			     m->m_mtatype == NULL
				? "<undefined>" : m->m_mtatype,
			     m->m_addrtype == NULL
				? "<undefined>" : m->m_addrtype,
			     m->m_diagtype == NULL
				? "<undefined>" : m->m_diagtype);
	(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, " r=%d", m->m_maxrcpt);
	if (m->m_argv != NULL)
	{
		char **a = m->m_argv;

		(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, " A=");
		while (*a != NULL)
		{
			if (a != m->m_argv)
				(void) sm_io_fprintf(fp, SM_TIME_DEFAULT,
						     " ");
			xputs(fp, *a++);
		}
	}
	(void) sm_io_fprintf(fp, SM_TIME_DEFAULT, "\n");
}

#if STARTTLS
static struct ssl_options
{
	const char	*sslopt_name;	/* name of the flag */
	long		sslopt_bits;	/* bits to set/clear */
} SSL_Option[] =
{
/* Workaround for bugs are turned on by default (as well as some others) */
#ifdef SSL_OP_MICROSOFT_SESS_ID_BUG
	{ "SSL_OP_MICROSOFT_SESS_ID_BUG",	SSL_OP_MICROSOFT_SESS_ID_BUG	},
#endif
#ifdef SSL_OP_NETSCAPE_CHALLENGE_BUG
	{ "SSL_OP_NETSCAPE_CHALLENGE_BUG",	SSL_OP_NETSCAPE_CHALLENGE_BUG	},
#endif
#ifdef SSL_OP_LEGACY_SERVER_CONNECT
	{ "SSL_OP_LEGACY_SERVER_CONNECT",	SSL_OP_LEGACY_SERVER_CONNECT	},
#endif
#ifdef SSL_OP_NETSCAPE_REUSE_CIPHER_CHANGE_BUG
	{ "SSL_OP_NETSCAPE_REUSE_CIPHER_CHANGE_BUG",	SSL_OP_NETSCAPE_REUSE_CIPHER_CHANGE_BUG	},
#endif
#ifdef SSL_OP_SSLREF2_REUSE_CERT_TYPE_BUG
	{ "SSL_OP_SSLREF2_REUSE_CERT_TYPE_BUG",	SSL_OP_SSLREF2_REUSE_CERT_TYPE_BUG	},
#endif
#ifdef SSL_OP_MICROSOFT_BIG_SSLV3_BUFFER
	{ "SSL_OP_MICROSOFT_BIG_SSLV3_BUFFER",	SSL_OP_MICROSOFT_BIG_SSLV3_BUFFER	},
#endif
#ifdef SSL_OP_MSIE_SSLV2_RSA_PADDING
	{ "SSL_OP_MSIE_SSLV2_RSA_PADDING",	SSL_OP_MSIE_SSLV2_RSA_PADDING	},
#endif
#ifdef SSL_OP_SSLEAY_080_CLIENT_DH_BUG
	{ "SSL_OP_SSLEAY_080_CLIENT_DH_BUG",	SSL_OP_SSLEAY_080_CLIENT_DH_BUG	},
#endif
#ifdef SSL_OP_TLS_D5_BUG
	{ "SSL_OP_TLS_D5_BUG",	SSL_OP_TLS_D5_BUG	},
#endif
#ifdef SSL_OP_TLS_BLOCK_PADDING_BUG
	{ "SSL_OP_TLS_BLOCK_PADDING_BUG",	SSL_OP_TLS_BLOCK_PADDING_BUG	},
#endif
#ifdef SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS
	{ "SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS",	SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS	},
#endif
#ifdef SSL_OP_ALL
	{ "SSL_OP_ALL",	SSL_OP_ALL	},
#endif
#ifdef SSL_OP_NO_QUERY_MTU
	{ "SSL_OP_NO_QUERY_MTU",	SSL_OP_NO_QUERY_MTU	},
#endif
#ifdef SSL_OP_COOKIE_EXCHANGE
	{ "SSL_OP_COOKIE_EXCHANGE",	SSL_OP_COOKIE_EXCHANGE	},
#endif
#ifdef SSL_OP_NO_TICKET
	{ "SSL_OP_NO_TICKET",	SSL_OP_NO_TICKET	},
#endif
#ifdef SSL_OP_CISCO_ANYCONNECT
	{ "SSL_OP_CISCO_ANYCONNECT",	SSL_OP_CISCO_ANYCONNECT	},
#endif
#ifdef SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION
	{ "SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION",	SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION	},
#endif
#ifdef SSL_OP_NO_COMPRESSION
	{ "SSL_OP_NO_COMPRESSION",	SSL_OP_NO_COMPRESSION	},
#endif
#ifdef SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION
	{ "SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION",	SSL_OP_ALLOW_UNSAFE_LEGACY_RENEGOTIATION	},
#endif
#ifdef SSL_OP_SINGLE_ECDH_USE
	{ "SSL_OP_SINGLE_ECDH_USE",	SSL_OP_SINGLE_ECDH_USE	},
#endif
#ifdef SSL_OP_SINGLE_DH_USE
	{ "SSL_OP_SINGLE_DH_USE",	SSL_OP_SINGLE_DH_USE	},
#endif
#ifdef SSL_OP_EPHEMERAL_RSA
	{ "SSL_OP_EPHEMERAL_RSA",	SSL_OP_EPHEMERAL_RSA	},
#endif
#ifdef SSL_OP_CIPHER_SERVER_PREFERENCE
	{ "SSL_OP_CIPHER_SERVER_PREFERENCE",	SSL_OP_CIPHER_SERVER_PREFERENCE	},
#endif
#ifdef SSL_OP_TLS_ROLLBACK_BUG
	{ "SSL_OP_TLS_ROLLBACK_BUG",	SSL_OP_TLS_ROLLBACK_BUG	},
#endif
#ifdef SSL_OP_NO_SSLv2
	{ "SSL_OP_NO_SSLv2",	SSL_OP_NO_SSLv2	},
#endif
#ifdef SSL_OP_NO_SSLv3
	{ "SSL_OP_NO_SSLv3",	SSL_OP_NO_SSLv3	},
#endif
#ifdef SSL_OP_NO_TLSv1
	{ "SSL_OP_NO_TLSv1",	SSL_OP_NO_TLSv1	},
#endif
#ifdef SSL_OP_NO_TLSv1_2
	{ "SSL_OP_NO_TLSv1_2",	SSL_OP_NO_TLSv1_2	},
#endif
#ifdef SSL_OP_NO_TLSv1_1
	{ "SSL_OP_NO_TLSv1_1",	SSL_OP_NO_TLSv1_1	},
#endif
#ifdef SSL_OP_PKCS1_CHECK_1
	{ "SSL_OP_PKCS1_CHECK_1",	SSL_OP_PKCS1_CHECK_1	},
#endif
#ifdef SSL_OP_PKCS1_CHECK_2
	{ "SSL_OP_PKCS1_CHECK_2",	SSL_OP_PKCS1_CHECK_2	},
#endif
#ifdef SSL_OP_NETSCAPE_CA_DN_BUG
	{ "SSL_OP_NETSCAPE_CA_DN_BUG",	SSL_OP_NETSCAPE_CA_DN_BUG	},
#endif
#ifdef SSL_OP_NETSCAPE_DEMO_CIPHER_CHANGE_BUG
	{ "SSL_OP_NETSCAPE_DEMO_CIPHER_CHANGE_BUG",	SSL_OP_NETSCAPE_DEMO_CIPHER_CHANGE_BUG	},
#endif
#ifdef SSL_OP_CRYPTOPRO_TLSEXT_BUG
	{ "SSL_OP_CRYPTOPRO_TLSEXT_BUG",	SSL_OP_CRYPTOPRO_TLSEXT_BUG	},
#endif
#ifdef SSL_OP_TLSEXT_PADDING
	{ "SSL_OP_TLSEXT_PADDING",	SSL_OP_TLSEXT_PADDING	},
#endif
	{ NULL,		0		}
};

/*
** READSSLOPTIONS  -- read SSL_OP_* values
**
**	Parameters:
**		opt -- name of option (can be NULL)
**		val -- string with SSL_OP_* values or hex value
**		delim -- end of string (e.g., '\0' or ';')
**		pssloptions -- return value (output)
**
**	Returns:
**		0 on success.
*/

#define SSLOPERR_NAN	1
#define SSLOPERR_NOTFOUND	2
#define SM_ISSPACE(c)	(isascii(c) && isspace(c))

static int
readssloptions(opt, val, pssloptions, delim)
	char *opt;
	char *val;
	unsigned long *pssloptions;
	int delim;
{
	char *p;
	int ret;

	ret = 0;
	for (p = val; *p != '\0' && *p != delim; )
	{
		bool clearmode;
		char *q;
		unsigned long sslopt_val;
		struct ssl_options *sslopts;

		while (*p == ' ')
			p++;
		if (*p == '\0')
			break;
		clearmode = false;
		if (*p == '-' || *p == '+')
			clearmode = *p++ == '-';
		q = p;
		while (*p != '\0' && !(SM_ISSPACE(*p)) && *p != ',')
			p++;
		if (*p != '\0')
			*p++ = '\0';
		sslopt_val = 0;
		if (isdigit(*q))
		{
			char *end;

			sslopt_val = strtoul(q, &end, 0);

			/* not a complete "syntax" check but good enough */
			if (end == q)
			{
				errno = 0;
				ret = SSLOPERR_NAN;
				if (opt != NULL)
					syserr("readcf: %s option value %s not a number",
						opt, q);
				sslopt_val = 0;
			}
		}
		else
		{
			for (sslopts = SSL_Option;
			     sslopts->sslopt_name != NULL; sslopts++)
			{
				if (sm_strcasecmp(q, sslopts->sslopt_name) == 0)
				{
					sslopt_val = sslopts->sslopt_bits;
					break;
				}
			}
			if (sslopts->sslopt_name == NULL)
			{
				errno = 0;
				ret = SSLOPERR_NOTFOUND;
				if (opt != NULL)
					syserr("readcf: %s option value %s unrecognized",
						opt, q);
			}
		}
		if (sslopt_val != 0)
		{
			if (clearmode)
				*pssloptions &= ~sslopt_val;
			else
				*pssloptions |= sslopt_val;
		}
	}
	return ret;
}

# if _FFR_TLS_SE_OPTS
/*
** GET_TLS_SE_OPTIONS -- get TLS session options (from ruleset)
**
**	Parameters:
**		e -- envelope
**		ssl -- TLS session context
**		srv -- server?
**
**	Returns:
**		0 on success.
*/

int
get_tls_se_options(e, ssl, srv)
	ENVELOPE *e;
	SSL *ssl;
	bool srv;
{
	bool saveQuickAbort, saveSuprErrs, ok;
	char *optionlist, *opt, *val;
	char *keyfile, *certfile;
	size_t len, i;
	int ret;

#  define who (srv ? "server" : "client")
#  define NAME_C_S macvalue(macid(srv ? "{client_name}" : "{server_name}"), e)
#  define ADDR_C_S macvalue(macid(srv ? "{client_addr}" : "{server_addr}"), e)
#  define WHICH srv ? "srv" : "clt"

	ret = 0;
	keyfile = certfile = opt = val = NULL;
	saveQuickAbort = QuickAbort;
	saveSuprErrs = SuprErrs;
	SuprErrs = true;
	QuickAbort = false;

	optionlist = NULL;
	ok = rscheck(srv ? "tls_srv_features" : "tls_clt_features",
		     NAME_C_S, ADDR_C_S, e,
		     RSF_RMCOMM|RSF_ADDR|RSF_STRING,
		     5, NULL, NOQID, NULL, &optionlist) == EX_OK;
	if (!ok && LogLevel > 8)
	{
		sm_syslog(LOG_NOTICE, NOQID,
			  "rscheck(tls_%s_features)=failed, relay=%s [%s], errors=%d",
			  WHICH, NAME_C_S, ADDR_C_S,
			  Errors);
	}
	QuickAbort = saveQuickAbort;
	SuprErrs = saveSuprErrs;
	if (ok && LogLevel > 9)
	{
		sm_syslog(LOG_INFO, NOQID,
			  "tls_%s_features=%s, relay=%s [%s]",
			  WHICH, optionlist, NAME_C_S, ADDR_C_S);
	}
	if (!ok || optionlist == NULL || (len = strlen(optionlist)) < 2)
	{
		if (LogLevel > 9)
			sm_syslog(LOG_INFO, NOQID,
				  "tls_%s_features=empty, relay=%s [%s]",
			  	  WHICH, NAME_C_S, ADDR_C_S);

		return ok ? 0 : 1;
	}

	i = 0;
	if (optionlist[0] == '"' && optionlist[len - 1] == '"')
	{
		optionlist[0] = ' ';
		optionlist[--len] = '\0';
		if (len <= 2)
		{
			if (LogLevel > 9 && len > 1)
				sm_syslog(LOG_INFO, NOQID,
				  "tls_%s_features=too_short, relay=%s [%s]",
			  	  WHICH, NAME_C_S, ADDR_C_S);

			/* this is not treated as error! */
			return 0;
		}
		i = 1;
	}

#  define INVALIDSYNTAX	\
	do {	\
		if (LogLevel > 7)	\
			sm_syslog(LOG_INFO, NOQID,	\
				  "tls_%s_features=invalid_syntax, opt=%s, relay=%s [%s]",	\
		  		  WHICH, opt, NAME_C_S, ADDR_C_S);	\
		return -1;	\
	} while (0)

#  define CHECKLEN	\
	do {	\
		if (i >= len)	\
			INVALIDSYNTAX;	\
	} while (0)

#  define SKIPWS	\
	do {	\
		while (i < len && SM_ISSPACE(optionlist[i]))	\
			++i;	\
		CHECKLEN;	\
	} while (0)

	/* parse and handle opt=val; */
	do {
		char sep;

		SKIPWS;
		opt = optionlist + i;
		sep = '=';
		while (i < len && optionlist[i] != sep
			&& optionlist[i] != '\0' && !SM_ISSPACE(optionlist[i]))
			++i;
		CHECKLEN;
		while (i < len && SM_ISSPACE(optionlist[i]))
			optionlist[i++] = '\0';
		CHECKLEN;
		if (optionlist[i] != sep)
			INVALIDSYNTAX;
		optionlist[i++] = '\0';

		SKIPWS;
		val = optionlist + i;
		sep = ';';
		while (i < len && optionlist[i] != sep && optionlist[i] != '\0')
			++i;
		if (optionlist[i] != '\0')
		{
			CHECKLEN;
			optionlist[i++] = '\0';
		}

		if (LogLevel > 13)
			sm_syslog(LOG_DEBUG, NOQID,
				  "tls_%s_features=parsed, %s=%s, relay=%s [%s]",
				  WHICH, opt, val, NAME_C_S, ADDR_C_S);

		if (sm_strcasecmp(opt, "options") == 0)
		{
			unsigned long ssloptions;

			ssloptions = 0;
			ret = readssloptions(NULL, val, &ssloptions, ';');
			if (ret == 0)
				(void) SSL_set_options(ssl, (long) ssloptions);
			else if (LogLevel > 8)
			{
				sm_syslog(LOG_WARNING, NOQID,
					  "tls_%s_features=%s, error=%s, relay=%s [%s]",
					  WHICH, val,
					  (ret == SSLOPERR_NAN) ? "not a number" :
					  ((ret == SSLOPERR_NOTFOUND) ? "SSL_OP not found" :
					  "unknown"),
					  NAME_C_S, ADDR_C_S);
			}
		}
		else if (sm_strcasecmp(opt, "cipherlist") == 0)
		{
			if (SSL_set_cipher_list(ssl, val) <= 0)
			{
				ret = 1;
				if (LogLevel > 7)
				{
					sm_syslog(LOG_WARNING, NOQID,
						  "STARTTLS=%s, error: SSL_set_cipher_list(%s) failed",
						  who, val);

					if (LogLevel > 9)
						tlslogerr(LOG_WARNING, who);
				}
			}
		}
		else if (sm_strcasecmp(opt, "keyfile") == 0)
			keyfile = val;
		else if (sm_strcasecmp(opt, "certfile") == 0)
			certfile = val;
		else
		{
			ret = 1;
			if (LogLevel > 7)
			{
				sm_syslog(LOG_INFO, NOQID,
					  "tls_%s_features=unknown_option, opt=%s, relay=%s [%s]",
				  	  WHICH, opt, NAME_C_S, ADDR_C_S);
			}
		}

	} while (optionlist[i] != '\0' && i < len);

	/* need cert and key before we can use the options */
	/* does not implement the "," hack for 2nd cert/key pair */
	if (keyfile != NULL && certfile != NULL)
	{
		load_certkey(ssl, srv, certfile, keyfile);
		keyfile = certfile = NULL;
	}
	else if (keyfile != NULL || certfile != NULL)
	{
		ret = 1;
		if (LogLevel > 7)
		{
			sm_syslog(LOG_INFO, NOQID,
				  "tls_%s_features=only_one_of_CertFile/KeyFile_specified, relay=%s [%s]",
			  	  WHICH, NAME_C_S, ADDR_C_S);
		}
	}

	return ret;
#  undef who
#  undef NAME_C_S
#  undef ADDR_C_S
#  undef WHICH
}
# endif /* _FFR_TLS_SE_OPTS */
#endif /* STARTTLS */

/*
**  SETOPTION -- set global processing option
**
**	Parameters:
**		opt -- option name.
**		val -- option value (as a text string).
**		safe -- set if this came from a configuration file.
**			Some options (if set from the command line) will
**			reset the user id to avoid security problems.
**		sticky -- if set, don't let other setoptions override
**			this value.
**		e -- the main envelope.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Sets options as implied by the arguments.
*/

static BITMAP256	StickyOpt;		/* set if option is stuck */

#if NAMED_BIND

static struct resolverflags
{
	char	*rf_name;	/* name of the flag */
	long	rf_bits;	/* bits to set/clear */
} ResolverFlags[] =
{
	{ "debug",	RES_DEBUG	},
	{ "aaonly",	RES_AAONLY	},
	{ "usevc",	RES_USEVC	},
	{ "primary",	RES_PRIMARY	},
	{ "igntc",	RES_IGNTC	},
	{ "recurse",	RES_RECURSE	},
	{ "defnames",	RES_DEFNAMES	},
	{ "stayopen",	RES_STAYOPEN	},
	{ "dnsrch",	RES_DNSRCH	},
# ifdef RES_USE_INET6
	{ "use_inet6",	RES_USE_INET6	},
# endif /* RES_USE_INET6 */
	{ "true",	0		},	/* avoid error on old syntax */
	{ NULL,		0		}
};

#endif /* NAMED_BIND */

#define OI_NONE		0	/* no special treatment */
#define OI_SAFE		0x0001	/* safe for random people to use */
#define OI_SUBOPT	0x0002	/* option has suboptions */

static struct optioninfo
{
	char		*o_name;	/* long name of option */
	unsigned char	o_code;		/* short name of option */
	unsigned short	o_flags;	/* option flags */
} OptionTab[] =
{
#if defined(SUN_EXTENSIONS) && defined(REMOTE_MODE)
	{ "RemoteMode",			'>',		OI_NONE	},
#endif /* defined(SUN_EXTENSIONS) && defined(REMOTE_MODE) */
	{ "SevenBitInput",		'7',		OI_SAFE	},
	{ "EightBitMode",		'8',		OI_SAFE	},
	{ "AliasFile",			'A',		OI_NONE	},
	{ "AliasWait",			'a',		OI_NONE	},
	{ "BlankSub",			'B',		OI_NONE	},
	{ "MinFreeBlocks",		'b',		OI_SAFE	},
	{ "CheckpointInterval",		'C',		OI_SAFE	},
	{ "HoldExpensive",		'c',		OI_NONE	},
	{ "DeliveryMode",		'd',		OI_SAFE	},
	{ "ErrorHeader",		'E',		OI_NONE	},
	{ "ErrorMode",			'e',		OI_SAFE	},
	{ "TempFileMode",		'F',		OI_NONE	},
	{ "SaveFromLine",		'f',		OI_NONE	},
	{ "MatchGECOS",			'G',		OI_NONE	},

	/* no long name, just here to avoid problems in setoption */
	{ "",				'g',		OI_NONE	},
	{ "HelpFile",			'H',		OI_NONE	},
	{ "MaxHopCount",		'h',		OI_NONE	},
	{ "ResolverOptions",		'I',		OI_NONE	},
	{ "IgnoreDots",			'i',		OI_SAFE	},
	{ "ForwardPath",		'J',		OI_NONE	},
	{ "SendMimeErrors",		'j',		OI_SAFE	},
	{ "ConnectionCacheSize",	'k',		OI_NONE	},
	{ "ConnectionCacheTimeout",	'K',		OI_NONE	},
	{ "UseErrorsTo",		'l',		OI_NONE	},
	{ "LogLevel",			'L',		OI_SAFE	},
	{ "MeToo",			'm',		OI_SAFE	},

	/* no long name, just here to avoid problems in setoption */
	{ "",				'M',		OI_NONE	},
	{ "CheckAliases",		'n',		OI_NONE	},
	{ "OldStyleHeaders",		'o',		OI_SAFE	},
	{ "DaemonPortOptions",		'O',		OI_NONE	},
	{ "PrivacyOptions",		'p',		OI_SAFE	},
	{ "PostmasterCopy",		'P',		OI_NONE	},
	{ "QueueFactor",		'q',		OI_NONE	},
	{ "QueueDirectory",		'Q',		OI_NONE	},
	{ "DontPruneRoutes",		'R',		OI_NONE	},
	{ "Timeout",			'r',		OI_SUBOPT },
	{ "StatusFile",			'S',		OI_NONE	},
	{ "SuperSafe",			's',		OI_SAFE	},
	{ "QueueTimeout",		'T',		OI_NONE	},
	{ "TimeZoneSpec",		't',		OI_NONE	},
	{ "UserDatabaseSpec",		'U',		OI_NONE	},
	{ "DefaultUser",		'u',		OI_NONE	},
	{ "FallbackMXhost",		'V',		OI_NONE	},
	{ "Verbose",			'v',		OI_SAFE	},
	{ "TryNullMXList",		'w',		OI_NONE	},
	{ "QueueLA",			'x',		OI_NONE	},
	{ "RefuseLA",			'X',		OI_NONE	},
	{ "RecipientFactor",		'y',		OI_NONE	},
	{ "ForkEachJob",		'Y',		OI_NONE	},
	{ "ClassFactor",		'z',		OI_NONE	},
	{ "RetryFactor",		'Z',		OI_NONE	},
#define O_QUEUESORTORD	0x81
	{ "QueueSortOrder",		O_QUEUESORTORD,	OI_SAFE	},
#define O_HOSTSFILE	0x82
	{ "HostsFile",			O_HOSTSFILE,	OI_NONE	},
#define O_MQA		0x83
	{ "MinQueueAge",		O_MQA,		OI_SAFE	},
#define O_DEFCHARSET	0x85
	{ "DefaultCharSet",		O_DEFCHARSET,	OI_SAFE	},
#define O_SSFILE	0x86
	{ "ServiceSwitchFile",		O_SSFILE,	OI_NONE	},
#define O_DIALDELAY	0x87
	{ "DialDelay",			O_DIALDELAY,	OI_SAFE	},
#define O_NORCPTACTION	0x88
	{ "NoRecipientAction",		O_NORCPTACTION,	OI_SAFE	},
#define O_SAFEFILEENV	0x89
	{ "SafeFileEnvironment",	O_SAFEFILEENV,	OI_NONE	},
#define O_MAXMSGSIZE	0x8a
	{ "MaxMessageSize",		O_MAXMSGSIZE,	OI_NONE	},
#define O_COLONOKINADDR	0x8b
	{ "ColonOkInAddr",		O_COLONOKINADDR, OI_SAFE },
#define O_MAXQUEUERUN	0x8c
	{ "MaxQueueRunSize",		O_MAXQUEUERUN,	OI_SAFE	},
#define O_MAXCHILDREN	0x8d
	{ "MaxDaemonChildren",		O_MAXCHILDREN,	OI_NONE	},
#define O_KEEPCNAMES	0x8e
	{ "DontExpandCnames",		O_KEEPCNAMES,	OI_NONE	},
#define O_MUSTQUOTE	0x8f
	{ "MustQuoteChars",		O_MUSTQUOTE,	OI_NONE	},
#define O_SMTPGREETING	0x90
	{ "SmtpGreetingMessage",	O_SMTPGREETING,	OI_NONE	},
#define O_UNIXFROM	0x91
	{ "UnixFromLine",		O_UNIXFROM,	OI_NONE	},
#define O_OPCHARS	0x92
	{ "OperatorChars",		O_OPCHARS,	OI_NONE	},
#define O_DONTINITGRPS	0x93
	{ "DontInitGroups",		O_DONTINITGRPS,	OI_NONE	},
#define O_SLFH		0x94
	{ "SingleLineFromHeader",	O_SLFH,		OI_SAFE	},
#define O_ABH		0x95
	{ "AllowBogusHELO",		O_ABH,		OI_SAFE	},
#define O_CONNTHROT	0x97
	{ "ConnectionRateThrottle",	O_CONNTHROT,	OI_NONE	},
#define O_UGW		0x99
	{ "UnsafeGroupWrites",		O_UGW,		OI_NONE	},
#define O_DBLBOUNCE	0x9a
	{ "DoubleBounceAddress",	O_DBLBOUNCE,	OI_NONE	},
#define O_HSDIR		0x9b
	{ "HostStatusDirectory",	O_HSDIR,	OI_NONE	},
#define O_SINGTHREAD	0x9c
	{ "SingleThreadDelivery",	O_SINGTHREAD,	OI_NONE	},
#define O_RUNASUSER	0x9d
	{ "RunAsUser",			O_RUNASUSER,	OI_NONE	},
#define O_DSN_RRT	0x9e
	{ "RrtImpliesDsn",		O_DSN_RRT,	OI_NONE	},
#define O_PIDFILE	0x9f
	{ "PidFile",			O_PIDFILE,	OI_NONE	},
#define O_DONTBLAMESENDMAIL	0xa0
	{ "DontBlameSendmail",		O_DONTBLAMESENDMAIL,	OI_NONE	},
#define O_DPI		0xa1
	{ "DontProbeInterfaces",	O_DPI,		OI_NONE	},
#define O_MAXRCPT	0xa2
	{ "MaxRecipientsPerMessage",	O_MAXRCPT,	OI_SAFE	},
#define O_DEADLETTER	0xa3
	{ "DeadLetterDrop",		O_DEADLETTER,	OI_NONE	},
#if _FFR_DONTLOCKFILESFORREAD_OPTION
# define O_DONTLOCK	0xa4
	{ "DontLockFilesForRead",	O_DONTLOCK,	OI_NONE	},
#endif /* _FFR_DONTLOCKFILESFORREAD_OPTION */
#define O_MAXALIASRCSN	0xa5
	{ "MaxAliasRecursion",		O_MAXALIASRCSN,	OI_NONE	},
#define O_CNCTONLYTO	0xa6
	{ "ConnectOnlyTo",		O_CNCTONLYTO,	OI_NONE	},
#define O_TRUSTUSER	0xa7
	{ "TrustedUser",		O_TRUSTUSER,	OI_NONE	},
#define O_MAXMIMEHDRLEN	0xa8
	{ "MaxMimeHeaderLength",	O_MAXMIMEHDRLEN,	OI_NONE	},
#define O_CONTROLSOCKET	0xa9
	{ "ControlSocketName",		O_CONTROLSOCKET,	OI_NONE	},
#define O_MAXHDRSLEN	0xaa
	{ "MaxHeadersLength",		O_MAXHDRSLEN,	OI_NONE	},
#if _FFR_MAX_FORWARD_ENTRIES
# define O_MAXFORWARD	0xab
	{ "MaxForwardEntries",		O_MAXFORWARD,	OI_NONE	},
#endif /* _FFR_MAX_FORWARD_ENTRIES */
#define O_PROCTITLEPREFIX	0xac
	{ "ProcessTitlePrefix",		O_PROCTITLEPREFIX,	OI_NONE	},
#define O_SASLINFO	0xad
#if _FFR_ALLOW_SASLINFO
	{ "DefaultAuthInfo",		O_SASLINFO,	OI_SAFE	},
#else /* _FFR_ALLOW_SASLINFO */
	{ "DefaultAuthInfo",		O_SASLINFO,	OI_NONE	},
#endif /* _FFR_ALLOW_SASLINFO */
#define O_SASLMECH	0xae
	{ "AuthMechanisms",		O_SASLMECH,	OI_NONE	},
#define O_CLIENTPORT	0xaf
	{ "ClientPortOptions",		O_CLIENTPORT,	OI_NONE	},
#define O_DF_BUFSIZE	0xb0
	{ "DataFileBufferSize",		O_DF_BUFSIZE,	OI_NONE	},
#define O_XF_BUFSIZE	0xb1
	{ "XscriptFileBufferSize",	O_XF_BUFSIZE,	OI_NONE	},
#define O_LDAPDEFAULTSPEC	0xb2
	{ "LDAPDefaultSpec",		O_LDAPDEFAULTSPEC,	OI_NONE	},
#define O_SRVCERTFILE	0xb4
	{ "ServerCertFile",		O_SRVCERTFILE,	OI_NONE	},
#define O_SRVKEYFILE	0xb5
	{ "ServerKeyFile",		O_SRVKEYFILE,	OI_NONE	},
#define O_CLTCERTFILE	0xb6
	{ "ClientCertFile",		O_CLTCERTFILE,	OI_NONE	},
#define O_CLTKEYFILE	0xb7
	{ "ClientKeyFile",		O_CLTKEYFILE,	OI_NONE	},
#define O_CACERTFILE	0xb8
	{ "CACertFile",			O_CACERTFILE,	OI_NONE	},
#define O_CACERTPATH	0xb9
	{ "CACertPath",			O_CACERTPATH,	OI_NONE	},
#define O_DHPARAMS	0xba
	{ "DHParameters",		O_DHPARAMS,	OI_NONE	},
#define O_INPUTMILTER	0xbb
	{ "InputMailFilters",		O_INPUTMILTER,	OI_NONE	},
#define O_MILTER	0xbc
	{ "Milter",			O_MILTER,	OI_SUBOPT	},
#define O_SASLOPTS	0xbd
	{ "AuthOptions",		O_SASLOPTS,	OI_NONE	},
#define O_QUEUE_FILE_MODE	0xbe
	{ "QueueFileMode",		O_QUEUE_FILE_MODE, OI_NONE	},
#define O_DIG_ALG	0xbf
	{ "CertFingerprintAlgorithm",		O_DIG_ALG,	OI_NONE	},
#define O_CIPHERLIST	0xc0
	{ "CipherList",			O_CIPHERLIST,	OI_NONE	},
#define O_RANDFILE	0xc1
	{ "RandFile",			O_RANDFILE,	OI_NONE	},
#define O_TLS_SRV_OPTS	0xc2
	{ "TLSSrvOptions",		O_TLS_SRV_OPTS,	OI_NONE	},
#define O_RCPTTHROT	0xc3
	{ "BadRcptThrottle",		O_RCPTTHROT,	OI_SAFE	},
#define O_DLVR_MIN	0xc4
	{ "DeliverByMin",		O_DLVR_MIN,	OI_NONE	},
#define O_MAXQUEUECHILDREN	0xc5
	{ "MaxQueueChildren",		O_MAXQUEUECHILDREN,	OI_NONE	},
#define O_MAXRUNNERSPERQUEUE	0xc6
	{ "MaxRunnersPerQueue",		O_MAXRUNNERSPERQUEUE,	OI_NONE },
#define O_DIRECTSUBMODIFIERS	0xc7
	{ "DirectSubmissionModifiers",	O_DIRECTSUBMODIFIERS,	OI_NONE },
#define O_NICEQUEUERUN	0xc8
	{ "NiceQueueRun",		O_NICEQUEUERUN,	OI_NONE	},
#define O_SHMKEY	0xc9
	{ "SharedMemoryKey",		O_SHMKEY,	OI_NONE	},
#define O_SASLBITS	0xca
	{ "AuthMaxBits",		O_SASLBITS,	OI_NONE	},
#define O_MBDB		0xcb
	{ "MailboxDatabase",		O_MBDB,		OI_NONE	},
#define O_MSQ		0xcc
	{ "UseMSP",	O_MSQ,		OI_NONE	},
#define O_DELAY_LA	0xcd
	{ "DelayLA",	O_DELAY_LA,	OI_NONE	},
#define O_FASTSPLIT	0xce
	{ "FastSplit",	O_FASTSPLIT,	OI_NONE	},
#define O_SOFTBOUNCE	0xcf
	{ "SoftBounce",	O_SOFTBOUNCE,	OI_NONE	},
#define O_SHMKEYFILE	0xd0
	{ "SharedMemoryKeyFile",	O_SHMKEYFILE,	OI_NONE	},
#define O_REJECTLOGINTERVAL	0xd1
	{ "RejectLogInterval",	O_REJECTLOGINTERVAL,	OI_NONE	},
#define O_REQUIRES_DIR_FSYNC	0xd2
	{ "RequiresDirfsync",	O_REQUIRES_DIR_FSYNC,	OI_NONE	},
#define O_CONNECTION_RATE_WINDOW_SIZE	0xd3
	{ "ConnectionRateWindowSize", O_CONNECTION_RATE_WINDOW_SIZE, OI_NONE },
#define O_CRLFILE	0xd4
	{ "CRLFile",		O_CRLFILE,	OI_NONE	},
#define O_FALLBACKSMARTHOST	0xd5
	{ "FallbackSmartHost",		O_FALLBACKSMARTHOST,	OI_NONE	},
#define O_SASLREALM	0xd6
	{ "AuthRealm",		O_SASLREALM,	OI_NONE	},
#if _FFR_CRLPATH
# define O_CRLPATH	0xd7
	{ "CRLPath",		O_CRLPATH,	OI_NONE	},
#endif /* _FFR_CRLPATH */
#define O_HELONAME 0xd8
	{ "HeloName",   O_HELONAME,     OI_NONE },
#if _FFR_MEMSTAT
# define O_REFUSELOWMEM	0xd9
	{ "RefuseLowMem",	O_REFUSELOWMEM,	OI_NONE },
# define O_QUEUELOWMEM	0xda
	{ "QueueLowMem",	O_QUEUELOWMEM,	OI_NONE },
# define O_MEMRESOURCE	0xdb
	{ "MemoryResource",	O_MEMRESOURCE,	OI_NONE },
#endif /* _FFR_MEMSTAT */
#define O_MAXNOOPCOMMANDS 0xdc
	{ "MaxNOOPCommands",	O_MAXNOOPCOMMANDS,	OI_NONE },
#if _FFR_MSG_ACCEPT
# define O_MSG_ACCEPT 0xdd
	{ "MessageAccept",	O_MSG_ACCEPT,	OI_NONE },
#endif /* _FFR_MSG_ACCEPT */
#if _FFR_QUEUE_RUN_PARANOIA
# define O_CHK_Q_RUNNERS 0xde
	{ "CheckQueueRunners",	O_CHK_Q_RUNNERS,	OI_NONE },
#endif /* _FFR_QUEUE_RUN_PARANOIA */
#if _FFR_EIGHT_BIT_ADDR_OK
# if !ALLOW_255
#  ERROR FFR_EIGHT_BIT_ADDR_OK requires _ALLOW_255
# endif /* !ALLOW_255 */
# define O_EIGHT_BIT_ADDR_OK	0xdf
	{ "EightBitAddrOK",	O_EIGHT_BIT_ADDR_OK,	OI_NONE },
#endif /* _FFR_EIGHT_BIT_ADDR_OK */
#if _FFR_ADDR_TYPE_MODES
# define O_ADDR_TYPE_MODES	0xe0
	{ "AddrTypeModes",	O_ADDR_TYPE_MODES,	OI_NONE },
#endif /* _FFR_ADDR_TYPE_MODES */
#if _FFR_BADRCPT_SHUTDOWN
# define O_RCPTSHUTD	0xe1
	{ "BadRcptShutdown",		O_RCPTSHUTD,	OI_SAFE },
# define O_RCPTSHUTDG	0xe2
	{ "BadRcptShutdownGood",	O_RCPTSHUTDG,	OI_SAFE	},
#endif /* _FFR_BADRCPT_SHUTDOWN */
#define O_SRV_SSL_OPTIONS	0xe3
	{ "ServerSSLOptions",		O_SRV_SSL_OPTIONS,	OI_NONE	},
#define O_CLT_SSL_OPTIONS	0xe4
	{ "ClientSSLOptions",		O_CLT_SSL_OPTIONS,	OI_NONE	},
#define O_MAX_QUEUE_AGE	0xe5
	{ "MaxQueueAge",	O_MAX_QUEUE_AGE,	OI_NONE },
#if _FFR_RCPTTHROTDELAY
# define O_RCPTTHROTDELAY	0xe6
	{ "BadRcptThrottleDelay",	O_RCPTTHROTDELAY,	OI_SAFE	},
#endif /* _FFR_RCPTTHROTDELAY */
#if 0 && _FFR_QOS && defined(SOL_IP) && defined(IP_TOS)
# define O_INETQOS	0xe7	/* reserved for FFR_QOS */
	{ "InetQoS",			O_INETQOS,	OI_NONE },
#endif
#if STARTTLS && _FFR_FIPSMODE
# define O_FIPSMODE	0xe8
	{ "FIPSMode",		O_FIPSMODE,	OI_NONE	},
#endif /* STARTTLS && _FFR_FIPSMODE  */
#if _FFR_REJECT_NUL_BYTE
# define O_REJECTNUL	0xe9
	{ "RejectNUL",	O_REJECTNUL,	OI_SAFE	},
#endif /* _FFR_REJECT_NUL_BYTE */
#if _FFR_BOUNCE_QUEUE
# define O_BOUNCEQUEUE 0xea
	{ "BounceQueue",		O_BOUNCEQUEUE,	OI_NONE },
#endif /* _FFR_BOUNCE_QUEUE */
#if _FFR_ADD_BCC
# define O_ADDBCC 0xeb
	{ "AddBcc",			O_ADDBCC,	OI_NONE },
#endif
#define O_USECOMPRESSEDIPV6ADDRESSES 0xec
	{ "UseCompressedIPv6Addresses",	O_USECOMPRESSEDIPV6ADDRESSES, OI_NONE },

	{ NULL,				'\0',		OI_NONE	}
};

# define CANONIFY(val)

# define SET_OPT_DEFAULT(opt, val)	opt = val

/* set a string option by expanding the value and assigning it */
/* WARNING this belongs ONLY into a case statement! */
#define SET_STRING_EXP(str)	\
		expand(val, exbuf, sizeof(exbuf), e);	\
		newval = sm_pstrdup_x(exbuf);		\
		if (str != NULL)	\
			sm_free(str);	\
		CANONIFY(newval);	\
		str = newval;		\
		break

#define OPTNAME	o->o_name == NULL ? "<unknown>" : o->o_name

void
setoption(opt, val, safe, sticky, e)
	int opt;
	char *val;
	bool safe;
	bool sticky;
	register ENVELOPE *e;
{
	register char *p;
	register struct optioninfo *o;
	char *subopt;
	int mid;
	bool can_setuid = RunAsUid == 0;
	auto char *ep;
	char buf[50];
	extern bool Warn_Q_option;
#if _FFR_ALLOW_SASLINFO
	extern unsigned int SubmitMode;
#endif /* _FFR_ALLOW_SASLINFO */
#if STARTTLS || SM_CONF_SHM
	char *newval;
	char exbuf[MAXLINE];
#endif /* STARTTLS || SM_CONF_SHM */
#if STARTTLS
	unsigned long *pssloptions = NULL;
#endif

	errno = 0;
	if (opt == ' ')
	{
		/* full word options */
		struct optioninfo *sel;

		p = strchr(val, '=');
		if (p == NULL)
			p = &val[strlen(val)];
		while (*--p == ' ')
			continue;
		while (*++p == ' ')
			*p = '\0';
		if (p == val)
		{
			syserr("readcf: null option name");
			return;
		}
		if (*p == '=')
			*p++ = '\0';
		while (*p == ' ')
			p++;
		subopt = strchr(val, '.');
		if (subopt != NULL)
			*subopt++ = '\0';
		sel = NULL;
		for (o = OptionTab; o->o_name != NULL; o++)
		{
			if (sm_strncasecmp(o->o_name, val, strlen(val)) != 0)
				continue;
			if (strlen(o->o_name) == strlen(val))
			{
				/* completely specified -- this must be it */
				sel = NULL;
				break;
			}
			if (sel != NULL)
				break;
			sel = o;
		}
		if (sel != NULL && o->o_name == NULL)
			o = sel;
		else if (o->o_name == NULL)
		{
			syserr("readcf: unknown option name %s", val);
			return;
		}
		else if (sel != NULL)
		{
			syserr("readcf: ambiguous option name %s (matches %s and %s)",
				val, sel->o_name, o->o_name);
			return;
		}
		if (strlen(val) != strlen(o->o_name))
		{
			int oldVerbose = Verbose;

			Verbose = 1;
			message("Option %s used as abbreviation for %s",
				val, o->o_name);
			Verbose = oldVerbose;
		}
		opt = o->o_code;
		val = p;
	}
	else
	{
		for (o = OptionTab; o->o_name != NULL; o++)
		{
			if (o->o_code == opt)
				break;
		}
		if (o->o_name == NULL)
		{
			syserr("readcf: unknown option name 0x%x", opt & 0xff);
			return;
		}
		subopt = NULL;
	}

	if (subopt != NULL && !bitset(OI_SUBOPT, o->o_flags))
	{
		if (tTd(37, 1))
			sm_dprintf("setoption: %s does not support suboptions, ignoring .%s\n",
				   OPTNAME, subopt);
		subopt = NULL;
	}

	if (tTd(37, 1))
	{
		sm_dprintf(isascii(opt) && isprint(opt) ?
			   "setoption %s (%c)%s%s=" :
			   "setoption %s (0x%x)%s%s=",
			   OPTNAME, opt, subopt == NULL ? "" : ".",
			   subopt == NULL ? "" : subopt);
		xputs(sm_debug_file(), val);
	}

	/*
	**  See if this option is preset for us.
	*/

	if (!sticky && bitnset(opt, StickyOpt))
	{
		if (tTd(37, 1))
			sm_dprintf(" (ignored)\n");
		return;
	}

	/*
	**  Check to see if this option can be specified by this user.
	*/

	if (!safe && RealUid == 0)
		safe = true;
	if (!safe && !bitset(OI_SAFE, o->o_flags))
	{
		if (opt != 'M' || (val[0] != 'r' && val[0] != 's'))
		{
			int dp;

			if (tTd(37, 1))
				sm_dprintf(" (unsafe)");
			dp = drop_privileges(true);
			setstat(dp);
		}
	}
	if (tTd(37, 1))
		sm_dprintf("\n");

	switch (opt & 0xff)
	{
	  case '7':		/* force seven-bit input */
		SevenBitInput = atobool(val);
		break;

	  case '8':		/* handling of 8-bit input */
#if MIME8TO7
		switch (*val)
		{
		  case 'p':		/* pass 8 bit, convert MIME */
			MimeMode = MM_CVTMIME|MM_PASS8BIT;
			break;

		  case 'm':		/* convert 8-bit, convert MIME */
			MimeMode = MM_CVTMIME|MM_MIME8BIT;
			break;

		  case 's':		/* strict adherence */
			MimeMode = MM_CVTMIME;
			break;

# if 0
		  case 'r':		/* reject 8-bit, don't convert MIME */
			MimeMode = 0;
			break;

		  case 'j':		/* "just send 8" */
			MimeMode = MM_PASS8BIT;
			break;

		  case 'a':		/* encode 8 bit if available */
			MimeMode = MM_MIME8BIT|MM_PASS8BIT|MM_CVTMIME;
			break;

		  case 'c':		/* convert 8 bit to MIME, never 7 bit */
			MimeMode = MM_MIME8BIT;
			break;
# endif /* 0 */

		  default:
			syserr("Unknown 8-bit mode %c", *val);
			finis(false, true, EX_USAGE);
		}
#else /* MIME8TO7 */
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Option: %s requires MIME8TO7 support\n",
				     OPTNAME);
#endif /* MIME8TO7 */
		break;

	  case 'A':		/* set default alias file */
		if (val[0] == '\0')
		{
			char *al;

			SET_OPT_DEFAULT(al, "aliases");
			setalias(al);
		}
		else
			setalias(val);
		break;

	  case 'a':		/* look N minutes for "@:@" in alias file */
		if (val[0] == '\0')
			SafeAlias = 5 MINUTES;
		else
			SafeAlias = convtime(val, 'm');
		break;

	  case 'B':		/* substitution for blank character */
		SpaceSub = val[0];
		if (SpaceSub == '\0')
			SpaceSub = ' ';
		break;

	  case 'b':		/* min blocks free on queue fs/max msg size */
		p = strchr(val, '/');
		if (p != NULL)
		{
			*p++ = '\0';
			MaxMessageSize = atol(p);
		}
		MinBlocksFree = atol(val);
		break;

	  case 'c':		/* don't connect to "expensive" mailers */
		NoConnect = atobool(val);
		break;

	  case 'C':		/* checkpoint every N addresses */
		if (safe || CheckpointInterval > atoi(val))
			CheckpointInterval = atoi(val);
		break;

	  case 'd':		/* delivery mode */
		switch (*val)
		{
		  case '\0':
			set_delivery_mode(SM_DELIVER, e);
			break;

		  case SM_QUEUE:	/* queue only */
		  case SM_DEFER:	/* queue only and defer map lookups */
		  case SM_DELIVER:	/* do everything */
		  case SM_FORK:		/* fork after verification */
#if _FFR_DM_ONE
		/* deliver first TA in background, then queue */
		  case SM_DM_ONE:
#endif /* _FFR_DM_ONE */
			set_delivery_mode(*val, e);
			break;

#if _FFR_PROXY
		  case SM_PROXY_REQ:
			set_delivery_mode(*val, e);
			break;
#endif /* _FFR_PROXY */

		  default:
			syserr("Unknown delivery mode %c", *val);
			finis(false, true, EX_USAGE);
		}
		break;

	  case 'E':		/* error message header/header file */
		if (*val != '\0')
			ErrMsgFile = newstr(val);
		break;

	  case 'e':		/* set error processing mode */
		switch (*val)
		{
		  case EM_QUIET:	/* be silent about it */
		  case EM_MAIL:		/* mail back */
		  case EM_BERKNET:	/* do berknet error processing */
		  case EM_WRITE:	/* write back (or mail) */
		  case EM_PRINT:	/* print errors normally (default) */
			e->e_errormode = *val;
			break;
		}
		break;

	  case 'F':		/* file mode */
		FileMode = atooct(val) & 0777;
		break;

	  case 'f':		/* save Unix-style From lines on front */
		SaveFrom = atobool(val);
		break;

	  case 'G':		/* match recipients against GECOS field */
		MatchGecos = atobool(val);
		break;

	  case 'g':		/* default gid */
  g_opt:
		if (isascii(*val) && isdigit(*val))
			DefGid = atoi(val);
		else
		{
			register struct group *gr;

			DefGid = -1;
			gr = getgrnam(val);
			if (gr == NULL)
				syserr("readcf: option %c: unknown group %s",
					opt, val);
			else
				DefGid = gr->gr_gid;
		}
		break;

	  case 'H':		/* help file */
		if (val[0] == '\0')
		{
			SET_OPT_DEFAULT(HelpFile, "helpfile");
		}
		else
		{
			CANONIFY(val);
			HelpFile = newstr(val);
		}
		break;

	  case 'h':		/* maximum hop count */
		MaxHopCount = atoi(val);
		break;

	  case 'I':		/* use internet domain name server */
#if NAMED_BIND
		for (p = val; *p != 0; )
		{
			bool clearmode;
			char *q;
			struct resolverflags *rfp;

			while (*p == ' ')
				p++;
			if (*p == '\0')
				break;
			clearmode = false;
			if (*p == '-')
				clearmode = true;
			else if (*p != '+')
				p--;
			p++;
			q = p;
			while (*p != '\0' && !(isascii(*p) && isspace(*p)))
				p++;
			if (*p != '\0')
				*p++ = '\0';
			if (sm_strcasecmp(q, "HasWildcardMX") == 0)
			{
				HasWildcardMX = !clearmode;
				continue;
			}
			if (sm_strcasecmp(q, "WorkAroundBrokenAAAA") == 0)
			{
				WorkAroundBrokenAAAA = !clearmode;
				continue;
			}
			for (rfp = ResolverFlags; rfp->rf_name != NULL; rfp++)
			{
				if (sm_strcasecmp(q, rfp->rf_name) == 0)
					break;
			}
			if (rfp->rf_name == NULL)
				syserr("readcf: I option value %s unrecognized", q);
			else if (clearmode)
				_res.options &= ~rfp->rf_bits;
			else
				_res.options |= rfp->rf_bits;
		}
		if (tTd(8, 2))
			sm_dprintf("_res.options = %x, HasWildcardMX = %d\n",
				   (unsigned int) _res.options, HasWildcardMX);
#else /* NAMED_BIND */
		usrerr("name server (I option) specified but BIND not compiled in");
#endif /* NAMED_BIND */
		break;

	  case 'i':		/* ignore dot lines in message */
		IgnrDot = atobool(val);
		break;

	  case 'j':		/* send errors in MIME (RFC 1341) format */
		SendMIMEErrors = atobool(val);
		break;

	  case 'J':		/* .forward search path */
		CANONIFY(val);
		ForwardPath = newstr(val);
		break;

	  case 'k':		/* connection cache size */
		MaxMciCache = atoi(val);
		if (MaxMciCache < 0)
			MaxMciCache = 0;
		break;

	  case 'K':		/* connection cache timeout */
		MciCacheTimeout = convtime(val, 'm');
		break;

	  case 'l':		/* use Errors-To: header */
		UseErrorsTo = atobool(val);
		break;

	  case 'L':		/* log level */
		if (safe || LogLevel < atoi(val))
			LogLevel = atoi(val);
		break;

	  case 'M':		/* define macro */
		sticky = false;
		mid = macid_parse(val, &ep);
		if (mid == 0)
			break;
		p = newstr(ep);
		if (!safe)
			cleanstrcpy(p, p, strlen(p) + 1);
		macdefine(&CurEnv->e_macro, A_TEMP, mid, p);
		break;

	  case 'm':		/* send to me too */
		MeToo = atobool(val);
		break;

	  case 'n':		/* validate RHS in newaliases */
		CheckAliases = atobool(val);
		break;

	    /* 'N' available -- was "net name" */

	  case 'O':		/* daemon options */
		if (!setdaemonoptions(val))
			syserr("too many daemons defined (%d max)", MAXDAEMONS);
		break;

	  case 'o':		/* assume old style headers */
		if (atobool(val))
			CurEnv->e_flags |= EF_OLDSTYLE;
		else
			CurEnv->e_flags &= ~EF_OLDSTYLE;
		break;

	  case 'p':		/* select privacy level */
		p = val;
		for (;;)
		{
			register struct prival *pv;
			extern struct prival PrivacyValues[];

			while (isascii(*p) && (isspace(*p) || ispunct(*p)))
				p++;
			if (*p == '\0')
				break;
			val = p;
			while (isascii(*p) && isalnum(*p))
				p++;
			if (*p != '\0')
				*p++ = '\0';

			for (pv = PrivacyValues; pv->pv_name != NULL; pv++)
			{
				if (sm_strcasecmp(val, pv->pv_name) == 0)
					break;
			}
			if (pv->pv_name == NULL)
				syserr("readcf: Op line: %s unrecognized", val);
			else
				PrivacyFlags |= pv->pv_flag;
		}
		sticky = false;
		break;

	  case 'P':		/* postmaster copy address for returned mail */
		PostMasterCopy = newstr(val);
		break;

	  case 'q':		/* slope of queue only function */
		QueueFactor = atoi(val);
		break;

	  case 'Q':		/* queue directory */
		if (val[0] == '\0')
		{
			QueueDir = "mqueue";
		}
		else
		{
			QueueDir = newstr(val);
		}
		if (RealUid != 0 && !safe)
			Warn_Q_option = true;
		break;

	  case 'R':		/* don't prune routes */
		DontPruneRoutes = atobool(val);
		break;

	  case 'r':		/* read timeout */
		if (subopt == NULL)
			inittimeouts(val, sticky);
		else
			settimeout(subopt, val, sticky);
		break;

	  case 'S':		/* status file */
		if (val[0] == '\0')
		{
			SET_OPT_DEFAULT(StatFile, "statistics");
		}
		else
		{
			CANONIFY(val);
			StatFile = newstr(val);
		}
		break;

	  case 's':		/* be super safe, even if expensive */
		if (tolower(*val) == 'i')
			SuperSafe = SAFE_INTERACTIVE;
		else if (tolower(*val) == 'p')
#if MILTER
			SuperSafe = SAFE_REALLY_POSTMILTER;
#else /* MILTER */
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				"Warning: SuperSafe=PostMilter requires Milter support (-DMILTER)\n");
#endif /* MILTER */
		else
			SuperSafe = atobool(val) ? SAFE_REALLY : SAFE_NO;
		break;

	  case 'T':		/* queue timeout */
		p = strchr(val, '/');
		if (p != NULL)
		{
			*p++ = '\0';
			settimeout("queuewarn", p, sticky);
		}
		settimeout("queuereturn", val, sticky);
		break;

	  case 't':		/* time zone name */
		TimeZoneSpec = newstr(val);
		break;

	  case 'U':		/* location of user database */
		UdbSpec = newstr(val);
		break;

	  case 'u':		/* set default uid */
		for (p = val; *p != '\0'; p++)
		{
# if _FFR_DOTTED_USERNAMES
			if (*p == '/' || *p == ':')
# else /* _FFR_DOTTED_USERNAMES */
			if (*p == '.' || *p == '/' || *p == ':')
# endif /* _FFR_DOTTED_USERNAMES */
			{
				*p++ = '\0';
				break;
			}
		}
		if (isascii(*val) && isdigit(*val))
		{
			DefUid = atoi(val);
			setdefuser();
		}
		else
		{
			register struct passwd *pw;

			DefUid = -1;
			pw = sm_getpwnam(val);
			if (pw == NULL)
			{
				syserr("readcf: option u: unknown user %s", val);
				break;
			}
			else
			{
				DefUid = pw->pw_uid;
				DefGid = pw->pw_gid;
				DefUser = newstr(pw->pw_name);
			}
		}

# ifdef UID_MAX
		if (DefUid > UID_MAX)
		{
			syserr("readcf: option u: uid value (%ld) > UID_MAX (%ld); ignored",
				(long)DefUid, (long)UID_MAX);
			break;
		}
# endif /* UID_MAX */

		/* handle the group if it is there */
		if (*p == '\0')
			break;
		val = p;
		goto g_opt;

	  case 'V':		/* fallback MX host */
		if (val[0] != '\0')
			FallbackMX = newstr(val);
		break;

	  case 'v':		/* run in verbose mode */
		Verbose = atobool(val) ? 1 : 0;
		break;

	  case 'w':		/* if we are best MX, try host directly */
		TryNullMXList = atobool(val);
		break;

	    /* 'W' available -- was wizard password */

	  case 'x':		/* load avg at which to auto-queue msgs */
		QueueLA = atoi(val);
		break;

	  case 'X':	/* load avg at which to auto-reject connections */
		RefuseLA = atoi(val);
		break;

	  case O_DELAY_LA:	/* load avg at which to delay connections */
		DelayLA = atoi(val);
		break;

	  case 'y':		/* work recipient factor */
		WkRecipFact = atoi(val);
		break;

	  case 'Y':		/* fork jobs during queue runs */
		ForkQueueRuns = atobool(val);
		break;

	  case 'z':		/* work message class factor */
		WkClassFact = atoi(val);
		break;

	  case 'Z':		/* work time factor */
		WkTimeFact = atoi(val);
		break;


#if _FFR_QUEUE_GROUP_SORTORDER
	/* coordinate this with makequeue() */
#endif /* _FFR_QUEUE_GROUP_SORTORDER */
	  case O_QUEUESORTORD:	/* queue sorting order */
		switch (*val)
		{
		  case 'f':	/* File Name */
		  case 'F':
			QueueSortOrder = QSO_BYFILENAME;
			break;

		  case 'h':	/* Host first */
		  case 'H':
			QueueSortOrder = QSO_BYHOST;
			break;

		  case 'm':	/* Modification time */
		  case 'M':
			QueueSortOrder = QSO_BYMODTIME;
			break;

		  case 'p':	/* Priority order */
		  case 'P':
			QueueSortOrder = QSO_BYPRIORITY;
			break;

		  case 't':	/* Submission time */
		  case 'T':
			QueueSortOrder = QSO_BYTIME;
			break;

		  case 'r':	/* Random */
		  case 'R':
			QueueSortOrder = QSO_RANDOM;
			break;

#if _FFR_RHS
		  case 's':	/* Shuffled host name */
		  case 'S':
			QueueSortOrder = QSO_BYSHUFFLE;
			break;
#endif /* _FFR_RHS */

		  case 'n':	/* none */
		  case 'N':
			QueueSortOrder = QSO_NONE;
			break;

		  default:
			syserr("Invalid queue sort order \"%s\"", val);
		}
		break;

	  case O_HOSTSFILE:	/* pathname of /etc/hosts file */
		CANONIFY(val);
		HostsFile = newstr(val);
		break;

	  case O_MQA:		/* minimum queue age between deliveries */
		MinQueueAge = convtime(val, 'm');
		break;

	  case O_MAX_QUEUE_AGE:
		MaxQueueAge = convtime(val, 'm');
		break;

	  case O_DEFCHARSET:	/* default character set for mimefying */
		DefaultCharSet = newstr(denlstring(val, true, true));
		break;

	  case O_SSFILE:	/* service switch file */
		CANONIFY(val);
		ServiceSwitchFile = newstr(val);
		break;

	  case O_DIALDELAY:	/* delay for dial-on-demand operation */
		DialDelay = convtime(val, 's');
		break;

	  case O_NORCPTACTION:	/* what to do if no recipient */
		if (sm_strcasecmp(val, "none") == 0)
			NoRecipientAction = NRA_NO_ACTION;
		else if (sm_strcasecmp(val, "add-to") == 0)
			NoRecipientAction = NRA_ADD_TO;
		else if (sm_strcasecmp(val, "add-apparently-to") == 0)
			NoRecipientAction = NRA_ADD_APPARENTLY_TO;
		else if (sm_strcasecmp(val, "add-bcc") == 0)
			NoRecipientAction = NRA_ADD_BCC;
		else if (sm_strcasecmp(val, "add-to-undisclosed") == 0)
			NoRecipientAction = NRA_ADD_TO_UNDISCLOSED;
		else
			syserr("Invalid NoRecipientAction: %s", val);
		break;

	  case O_SAFEFILEENV:	/* chroot() environ for writing to files */
		if (*val == '\0')
			break;

		/* strip trailing slashes */
		p = val + strlen(val) - 1;
		while (p >= val && *p == '/')
			*p-- = '\0';

		if (*val == '\0')
			break;

		SafeFileEnv = newstr(val);
		break;

	  case O_MAXMSGSIZE:	/* maximum message size */
		MaxMessageSize = atol(val);
		break;

	  case O_COLONOKINADDR:	/* old style handling of colon addresses */
		ColonOkInAddr = atobool(val);
		break;

	  case O_MAXQUEUERUN:	/* max # of jobs in a single queue run */
		MaxQueueRun = atoi(val);
		break;

	  case O_MAXCHILDREN:	/* max # of children of daemon */
		MaxChildren = atoi(val);
		break;

	  case O_MAXQUEUECHILDREN: /* max # of children of daemon */
		MaxQueueChildren = atoi(val);
		break;

	  case O_MAXRUNNERSPERQUEUE: /* max # runners in a queue group */
		MaxRunnersPerQueue = atoi(val);
		break;

	  case O_NICEQUEUERUN:		/* nice queue runs */
#if !HASNICE
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: NiceQueueRun set on system that doesn't support nice()\n");
#endif /* !HASNICE */

		/* XXX do we want to check the range? > 0 ? */
		NiceQueueRun = atoi(val);
		break;

	  case O_SHMKEY:		/* shared memory key */
#if SM_CONF_SHM
		ShmKey = atol(val);
#else /* SM_CONF_SHM */
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Option: %s requires shared memory support (-DSM_CONF_SHM)\n",
				     OPTNAME);
#endif /* SM_CONF_SHM */
		break;

	  case O_SHMKEYFILE:		/* shared memory key file */
#if SM_CONF_SHM
		SET_STRING_EXP(ShmKeyFile);
#else /* SM_CONF_SHM */
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Option: %s requires shared memory support (-DSM_CONF_SHM)\n",
				     OPTNAME);
		break;
#endif /* SM_CONF_SHM */

#if _FFR_MAX_FORWARD_ENTRIES
	  case O_MAXFORWARD:	/* max # of forward entries */
		MaxForwardEntries = atoi(val);
		break;
#endif /* _FFR_MAX_FORWARD_ENTRIES */

	  case O_KEEPCNAMES:	/* don't expand CNAME records */
		DontExpandCnames = atobool(val);
		break;

	  case O_MUSTQUOTE:	/* must quote these characters in phrases */
		(void) sm_strlcpy(buf, "@,;:\\()[]", sizeof(buf));
		if (strlen(val) < sizeof(buf) - 10)
			(void) sm_strlcat(buf, val, sizeof(buf));
		else
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Warning: MustQuoteChars too long, ignored.\n");
		MustQuoteChars = newstr(buf);
		break;

	  case O_SMTPGREETING:	/* SMTP greeting message (old $e macro) */
		SmtpGreeting = newstr(munchstring(val, NULL, '\0'));
		break;

	  case O_UNIXFROM:	/* UNIX From_ line (old $l macro) */
		UnixFromLine = newstr(munchstring(val, NULL, '\0'));
		break;

	  case O_OPCHARS:	/* operator characters (old $o macro) */
		if (OperatorChars != NULL)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Warning: OperatorChars is being redefined.\n         It should only be set before ruleset definitions.\n");
		OperatorChars = newstr(munchstring(val, NULL, '\0'));
		break;

	  case O_DONTINITGRPS:	/* don't call initgroups(3) */
		DontInitGroups = atobool(val);
		break;

	  case O_SLFH:		/* make sure from fits on one line */
		SingleLineFromHeader = atobool(val);
		break;

	  case O_ABH:		/* allow HELO commands with syntax errors */
		AllowBogusHELO = atobool(val);
		break;

	  case O_CONNTHROT:	/* connection rate throttle */
		ConnRateThrottle = atoi(val);
		break;

	  case O_UGW:		/* group writable files are unsafe */
		if (!atobool(val))
		{
			setbitn(DBS_GROUPWRITABLEFORWARDFILESAFE,
				DontBlameSendmail);
			setbitn(DBS_GROUPWRITABLEINCLUDEFILESAFE,
				DontBlameSendmail);
		}
		break;

	  case O_DBLBOUNCE:	/* address to which to send double bounces */
		DoubleBounceAddr = newstr(val);
		break;

	  case O_HSDIR:		/* persistent host status directory */
		if (val[0] != '\0')
		{
			CANONIFY(val);
			HostStatDir = newstr(val);
		}
		break;

	  case O_SINGTHREAD:	/* single thread deliveries (requires hsdir) */
		SingleThreadDelivery = atobool(val);
		break;

	  case O_RUNASUSER:	/* run bulk of code as this user */
		for (p = val; *p != '\0'; p++)
		{
# if _FFR_DOTTED_USERNAMES
			if (*p == '/' || *p == ':')
# else /* _FFR_DOTTED_USERNAMES */
			if (*p == '.' || *p == '/' || *p == ':')
# endif /* _FFR_DOTTED_USERNAMES */
			{
				*p++ = '\0';
				break;
			}
		}
		if (isascii(*val) && isdigit(*val))
		{
			if (can_setuid)
				RunAsUid = atoi(val);
		}
		else
		{
			register struct passwd *pw;

			pw = sm_getpwnam(val);
			if (pw == NULL)
			{
				syserr("readcf: option RunAsUser: unknown user %s", val);
				break;
			}
			else if (can_setuid)
			{
				if (*p == '\0')
					RunAsUserName = newstr(val);
				RunAsUid = pw->pw_uid;
				RunAsGid = pw->pw_gid;
			}
			else if (EffGid == pw->pw_gid)
				RunAsGid = pw->pw_gid;
			else if (UseMSP && *p == '\0')
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "WARNING: RunAsUser for MSP ignored, check group ids (egid=%ld, want=%ld)\n",
						     (long) EffGid,
						     (long) pw->pw_gid);
		}
# ifdef UID_MAX
		if (RunAsUid > UID_MAX)
		{
			syserr("readcf: option RunAsUser: uid value (%ld) > UID_MAX (%ld); ignored",
				(long) RunAsUid, (long) UID_MAX);
			break;
		}
# endif /* UID_MAX */
		if (*p != '\0')
		{
			if (isascii(*p) && isdigit(*p))
			{
				gid_t runasgid;

				runasgid = (gid_t) atoi(p);
				if (can_setuid || EffGid == runasgid)
					RunAsGid = runasgid;
				else if (UseMSP)
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT,
							     "WARNING: RunAsUser for MSP ignored, check group ids (egid=%ld, want=%ld)\n",
							     (long) EffGid,
							     (long) runasgid);
			}
			else
			{
				register struct group *gr;

				gr = getgrnam(p);
				if (gr == NULL)
					syserr("readcf: option RunAsUser: unknown group %s",
						p);
				else if (can_setuid || EffGid == gr->gr_gid)
					RunAsGid = gr->gr_gid;
				else if (UseMSP)
					(void) sm_io_fprintf(smioout,
							     SM_TIME_DEFAULT,
							     "WARNING: RunAsUser for MSP ignored, check group ids (egid=%ld, want=%ld)\n",
							     (long) EffGid,
							     (long) gr->gr_gid);
			}
		}
		if (tTd(47, 5))
			sm_dprintf("readcf: RunAsUser = %d:%d\n",
				   (int) RunAsUid, (int) RunAsGid);
		break;

	  case O_DSN_RRT:
		RrtImpliesDsn = atobool(val);
		break;

	  case O_PIDFILE:
		PSTRSET(PidFile, val);
		break;

	  case O_DONTBLAMESENDMAIL:
		p = val;
		for (;;)
		{
			register struct dbsval *dbs;
			extern struct dbsval DontBlameSendmailValues[];

			while (isascii(*p) && (isspace(*p) || ispunct(*p)))
				p++;
			if (*p == '\0')
				break;
			val = p;
			while (isascii(*p) && isalnum(*p))
				p++;
			if (*p != '\0')
				*p++ = '\0';

			for (dbs = DontBlameSendmailValues;
			     dbs->dbs_name != NULL; dbs++)
			{
				if (sm_strcasecmp(val, dbs->dbs_name) == 0)
					break;
			}
			if (dbs->dbs_name == NULL)
				syserr("readcf: DontBlameSendmail option: %s unrecognized", val);
			else if (dbs->dbs_flag == DBS_SAFE)
				clrbitmap(DontBlameSendmail);
			else
				setbitn(dbs->dbs_flag, DontBlameSendmail);
		}
		sticky = false;
		break;

	  case O_DPI:
		if (sm_strcasecmp(val, "loopback") == 0)
			DontProbeInterfaces = DPI_SKIPLOOPBACK;
		else if (atobool(val))
			DontProbeInterfaces = DPI_PROBENONE;
		else
			DontProbeInterfaces = DPI_PROBEALL;
		break;

	  case O_MAXRCPT:
		MaxRcptPerMsg = atoi(val);
		break;

	  case O_RCPTTHROT:
		BadRcptThrottle = atoi(val);
		break;

#if _FFR_RCPTTHROTDELAY
	  case O_RCPTTHROTDELAY:
		BadRcptThrottleDelay = atoi(val);
		break;
#endif /* _FFR_RCPTTHROTDELAY */

	  case O_DEADLETTER:
		CANONIFY(val);
		PSTRSET(DeadLetterDrop, val);
		break;

#if _FFR_DONTLOCKFILESFORREAD_OPTION
	  case O_DONTLOCK:
		DontLockReadFiles = atobool(val);
		break;
#endif /* _FFR_DONTLOCKFILESFORREAD_OPTION */

	  case O_MAXALIASRCSN:
		MaxAliasRecursion = atoi(val);
		break;

	  case O_CNCTONLYTO:
		/* XXX should probably use gethostbyname */
#if NETINET || NETINET6
		ConnectOnlyTo.sa.sa_family = AF_UNSPEC;
# if NETINET6
		if (anynet_pton(AF_INET6, val,
				&ConnectOnlyTo.sin6.sin6_addr) == 1)
			ConnectOnlyTo.sa.sa_family = AF_INET6;
		else
# endif /* NETINET6 */
# if NETINET
		{
			ConnectOnlyTo.sin.sin_addr.s_addr = inet_addr(val);
			if (ConnectOnlyTo.sin.sin_addr.s_addr != INADDR_NONE)
				ConnectOnlyTo.sa.sa_family = AF_INET;
		}

# endif /* NETINET */
		if (ConnectOnlyTo.sa.sa_family == AF_UNSPEC)
		{
			syserr("readcf: option ConnectOnlyTo: invalid IP address %s",
			       val);
			break;
		}
#endif /* NETINET || NETINET6 */
		break;

	  case O_TRUSTUSER:
# if !HASFCHOWN && !defined(_FFR_DROP_TRUSTUSER_WARNING)
		if (!UseMSP)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "readcf: option TrustedUser may cause problems on systems\n        which do not support fchown() if UseMSP is not set.\n");
# endif /* !HASFCHOWN && !defined(_FFR_DROP_TRUSTUSER_WARNING) */
		if (isascii(*val) && isdigit(*val))
			TrustedUid = atoi(val);
		else
		{
			register struct passwd *pw;

			TrustedUid = 0;
			pw = sm_getpwnam(val);
			if (pw == NULL)
			{
				syserr("readcf: option TrustedUser: unknown user %s", val);
				break;
			}
			else
				TrustedUid = pw->pw_uid;
		}

# ifdef UID_MAX
		if (TrustedUid > UID_MAX)
		{
			syserr("readcf: option TrustedUser: uid value (%ld) > UID_MAX (%ld)",
				(long) TrustedUid, (long) UID_MAX);
			TrustedUid = 0;
		}
# endif /* UID_MAX */
		break;

	  case O_MAXMIMEHDRLEN:
		p = strchr(val, '/');
		if (p != NULL)
			*p++ = '\0';
		MaxMimeHeaderLength = atoi(val);
		if (p != NULL && *p != '\0')
			MaxMimeFieldLength = atoi(p);
		else
			MaxMimeFieldLength = MaxMimeHeaderLength / 2;

		if (MaxMimeHeaderLength <= 0)
			MaxMimeHeaderLength = 0;
		else if (MaxMimeHeaderLength < 128)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Warning: MaxMimeHeaderLength: header length limit set lower than 128\n");

		if (MaxMimeFieldLength <= 0)
			MaxMimeFieldLength = 0;
		else if (MaxMimeFieldLength < 40)
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Warning: MaxMimeHeaderLength: field length limit set lower than 40\n");

		/*
		**  Headers field values now include leading space, so let's
		**  adjust the values to be "backward compatible".
		*/

		if (MaxMimeHeaderLength > 0)
			MaxMimeHeaderLength++;
		if (MaxMimeFieldLength > 0)
			MaxMimeFieldLength++;
		break;

	  case O_CONTROLSOCKET:
		PSTRSET(ControlSocketName, val);
		break;

	  case O_MAXHDRSLEN:
		MaxHeadersLength = atoi(val);

		if (MaxHeadersLength > 0 &&
		    MaxHeadersLength < (MAXHDRSLEN / 2))
			(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
					     "Warning: MaxHeadersLength: headers length limit set lower than %d\n",
					     (MAXHDRSLEN / 2));
		break;

	  case O_PROCTITLEPREFIX:
		PSTRSET(ProcTitlePrefix, val);
		break;

#if SASL
	  case O_SASLINFO:
# if _FFR_ALLOW_SASLINFO
		/*
		**  Allow users to select their own authinfo file
		**  under certain circumstances, otherwise just ignore
		**  the option.  If the option isn't ignored, several
		**  commands don't work very well, e.g., mailq.
		**  However, this is not a "perfect" solution.
		**  If mail is queued, the authentication info
		**  will not be used in subsequent delivery attempts.
		**  If we really want to support this, then it has
		**  to be stored in the queue file.
		*/
		if (!bitset(SUBMIT_MSA, SubmitMode) && RealUid != 0 &&
		    RunAsUid != RealUid)
			break;
# endif /* _FFR_ALLOW_SASLINFO */
		PSTRSET(SASLInfo, val);
		break;

	  case O_SASLMECH:
		if (AuthMechanisms != NULL)
			sm_free(AuthMechanisms); /* XXX */
		if (*val != '\0')
			AuthMechanisms = newstr(val);
		else
			AuthMechanisms = NULL;
		break;

	  case O_SASLREALM:
		if (AuthRealm != NULL)
			sm_free(AuthRealm);
		if (*val != '\0')
			AuthRealm = newstr(val);
		else
			AuthRealm = NULL;
		break;

	  case O_SASLOPTS:
		while (val != NULL && *val != '\0')
		{
			switch (*val)
			{
			  case 'A':
				SASLOpts |= SASL_AUTH_AUTH;
				break;

			  case 'a':
				SASLOpts |= SASL_SEC_NOACTIVE;
				break;

			  case 'c':
				SASLOpts |= SASL_SEC_PASS_CREDENTIALS;
				break;

			  case 'd':
				SASLOpts |= SASL_SEC_NODICTIONARY;
				break;

			  case 'f':
				SASLOpts |= SASL_SEC_FORWARD_SECRECY;
				break;

#  if SASL >= 20101
			  case 'm':
				SASLOpts |= SASL_SEC_MUTUAL_AUTH;
				break;
#  endif /* SASL >= 20101 */

			  case 'p':
				SASLOpts |= SASL_SEC_NOPLAINTEXT;
				break;

			  case 'y':
				SASLOpts |= SASL_SEC_NOANONYMOUS;
				break;

			  case ' ':	/* ignore */
			  case '\t':	/* ignore */
			  case ',':	/* ignore */
				break;

			  default:
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Warning: Option: %s unknown parameter '%c'\n",
						     OPTNAME,
						     (isascii(*val) &&
							isprint(*val))
							? *val : '?');
				break;
			}
			++val;
			val = strpbrk(val, ", \t");
			if (val != NULL)
				++val;
		}
		break;

	  case O_SASLBITS:
		MaxSLBits = atoi(val);
		break;

#else /* SASL */
	  case O_SASLINFO:
	  case O_SASLMECH:
	  case O_SASLREALM:
	  case O_SASLOPTS:
	  case O_SASLBITS:
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Option: %s requires SASL support (-DSASL)\n",
				     OPTNAME);
		break;
#endif /* SASL */

#if STARTTLS
	  case O_SRVCERTFILE:
		SET_STRING_EXP(SrvCertFile);
	  case O_SRVKEYFILE:
		SET_STRING_EXP(SrvKeyFile);
	  case O_CLTCERTFILE:
		SET_STRING_EXP(CltCertFile);
	  case O_CLTKEYFILE:
		SET_STRING_EXP(CltKeyFile);
	  case O_CACERTFILE:
		SET_STRING_EXP(CACertFile);
	  case O_CACERTPATH:
		SET_STRING_EXP(CACertPath);
	  case O_DHPARAMS:
		SET_STRING_EXP(DHParams);
	  case O_CIPHERLIST:
		SET_STRING_EXP(CipherList);
	  case O_DIG_ALG:
		SET_STRING_EXP(CertFingerprintAlgorithm);
	  case O_SRV_SSL_OPTIONS:
		pssloptions = &Srv_SSL_Options;
	  case O_CLT_SSL_OPTIONS:
		if (pssloptions == NULL)
			pssloptions = &Clt_SSL_Options;
		(void) readssloptions(o->o_name, val, pssloptions, '\0');
		if (tTd(37, 8))
			sm_dprintf("ssloptions=%#lx\n", *pssloptions);

		pssloptions = NULL;
		break;

	  case O_CRLFILE:
# if OPENSSL_VERSION_NUMBER > 0x00907000L
		SET_STRING_EXP(CRLFile);
# else /* OPENSSL_VERSION_NUMBER > 0x00907000L */
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Option: %s requires at least OpenSSL 0.9.7\n",
				     OPTNAME);
		break;
# endif /* OPENSSL_VERSION_NUMBER > 0x00907000L */

# if _FFR_CRLPATH
	  case O_CRLPATH:
#  if OPENSSL_VERSION_NUMBER > 0x00907000L
		SET_STRING_EXP(CRLPath);
#  else /* OPENSSL_VERSION_NUMBER > 0x00907000L */
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Option: %s requires at least OpenSSL 0.9.7\n",
				     OPTNAME);
		break;
#  endif /* OPENSSL_VERSION_NUMBER > 0x00907000L */
# endif /* _FFR_CRLPATH */

	/*
	**  XXX How about options per daemon/client instead of globally?
	**  This doesn't work well for some options, e.g., no server cert,
	**  but fine for others.
	**
	**  XXX Some people may want different certs per server.
	**
	**  See also srvfeatures()
	*/

	  case O_TLS_SRV_OPTS:
		while (val != NULL && *val != '\0')
		{
			switch (*val)
			{
			  case 'V':
				TLS_Srv_Opts |= TLS_I_NO_VRFY;
				break;
			/*
			**  Server without a cert? That works only if
			**  AnonDH is enabled as cipher, which is not in the
			**  default list. Hence the CipherList option must
			**  be available. Moreover: which clients support this
			**  besides sendmail with this setting?
			*/

			  case 'C':
				TLS_Srv_Opts &= ~TLS_I_SRV_CERT;
				break;
			  case ' ':	/* ignore */
			  case '\t':	/* ignore */
			  case ',':	/* ignore */
				break;
			  default:
				(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
						     "Warning: Option: %s unknown parameter '%c'\n",
						     OPTNAME,
						     (isascii(*val) &&
							isprint(*val))
							? *val : '?');
				break;
			}
			++val;
			val = strpbrk(val, ", \t");
			if (val != NULL)
				++val;
		}
		break;

	  case O_RANDFILE:
		PSTRSET(RandFile, val);
		break;

#else /* STARTTLS */
	  case O_SRVCERTFILE:
	  case O_SRVKEYFILE:
	  case O_CLTCERTFILE:
	  case O_CLTKEYFILE:
	  case O_CACERTFILE:
	  case O_CACERTPATH:
	  case O_DHPARAMS:
	  case O_SRV_SSL_OPTIONS:
	  case O_CLT_SSL_OPTIONS:
	  case O_CIPHERLIST:
	  case O_CRLFILE:
# if _FFR_CRLPATH
	  case O_CRLPATH:
# endif /* _FFR_CRLPATH */
	  case O_RANDFILE:
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Option: %s requires TLS support\n",
				     OPTNAME);
		break;

#endif /* STARTTLS */
#if STARTTLS && _FFR_FIPSMODE
	  case O_FIPSMODE:
		FipsMode = atobool(val);
		break;
#endif /* STARTTLS && _FFR_FIPSMODE  */

	  case O_CLIENTPORT:
		setclientoptions(val);
		break;

	  case O_DF_BUFSIZE:
		DataFileBufferSize = atoi(val);
		break;

	  case O_XF_BUFSIZE:
		XscriptFileBufferSize = atoi(val);
		break;

	  case O_LDAPDEFAULTSPEC:
#if LDAPMAP
		ldapmap_set_defaults(val);
#else /* LDAPMAP */
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Option: %s requires LDAP support (-DLDAPMAP)\n",
				     OPTNAME);
#endif /* LDAPMAP */
		break;

	  case O_INPUTMILTER:
#if MILTER
		InputFilterList = newstr(val);
#else /* MILTER */
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Option: %s requires Milter support (-DMILTER)\n",
				     OPTNAME);
#endif /* MILTER */
		break;

	  case O_MILTER:
#if MILTER
		milter_set_option(subopt, val, sticky);
#else /* MILTER */
		(void) sm_io_fprintf(smioout, SM_TIME_DEFAULT,
				     "Warning: Option: %s requires Milter support (-DMILTER)\n",
				     OPTNAME);
#endif /* MILTER */
		break;

	  case O_QUEUE_FILE_MODE:	/* queue file mode */
		QueueFileMode = atooct(val) & 0777;
		break;

	  case O_DLVR_MIN:	/* deliver by minimum time */
		DeliverByMin = convtime(val, 's');
		break;

	  /* modifiers {daemon_flags} for direct submissions */
	  case O_DIRECTSUBMODIFIERS:
		{
			BITMAP256 m;	/* ignored */
			extern ENVELOPE BlankEnvelope;

			macdefine(&BlankEnvelope.e_macro, A_PERM,
				  macid("{daemon_flags}"),
				  getmodifiers(val, m));
		}
		break;

	  case O_FASTSPLIT:
		FastSplit = atoi(val);
		break;

	  case O_MBDB:
		Mbdb = newstr(val);
		break;

	  case O_MSQ:
		UseMSP = atobool(val);
		break;

	  case O_SOFTBOUNCE:
		SoftBounce = atobool(val);
		break;

	  case O_REJECTLOGINTERVAL:	/* time btwn log msgs while refusing */
		RejectLogInterval = convtime(val, 'h');
		break;

	  case O_REQUIRES_DIR_FSYNC:
#if REQUIRES_DIR_FSYNC
		RequiresDirfsync = atobool(val);
#else /* REQUIRES_DIR_FSYNC */
		/* silently ignored... required for cf file option */
#endif /* REQUIRES_DIR_FSYNC */
		break;

	  case O_CONNECTION_RATE_WINDOW_SIZE:
		ConnectionRateWindowSize = convtime(val, 's');
		break;

	  case O_FALLBACKSMARTHOST:	/* fallback smart host */
		if (val[0] != '\0')
			FallbackSmartHost = newstr(val);
		break;

	  case O_HELONAME:
		HeloName = newstr(val);
		break;

#if _FFR_MEMSTAT
	  case O_REFUSELOWMEM:
		RefuseLowMem = atoi(val);
		break;
	  case O_QUEUELOWMEM:
		QueueLowMem = atoi(val);
		break;
	  case O_MEMRESOURCE:
		MemoryResource = newstr(val);
		break;
#endif /* _FFR_MEMSTAT */

	  case O_MAXNOOPCOMMANDS:
		MaxNOOPCommands = atoi(val);
		break;

#if _FFR_MSG_ACCEPT
	  case O_MSG_ACCEPT:
		MessageAccept = newstr(val);
		break;
#endif /* _FFR_MSG_ACCEPT */

#if _FFR_QUEUE_RUN_PARANOIA
	  case O_CHK_Q_RUNNERS:
		CheckQueueRunners = atoi(val);
		break;
#endif /* _FFR_QUEUE_RUN_PARANOIA */

#if _FFR_EIGHT_BIT_ADDR_OK
	  case O_EIGHT_BIT_ADDR_OK:
		EightBitAddrOK = atobool(val);
		break;
#endif /* _FFR_EIGHT_BIT_ADDR_OK */

#if _FFR_ADDR_TYPE_MODES
	  case O_ADDR_TYPE_MODES:
		AddrTypeModes = atobool(val);
		break;
#endif /* _FFR_ADDR_TYPE_MODES */

#if _FFR_BADRCPT_SHUTDOWN
	  case O_RCPTSHUTD:
		BadRcptShutdown = atoi(val);
		break;

	  case O_RCPTSHUTDG:
		BadRcptShutdownGood = atoi(val);
		break;
#endif /* _FFR_BADRCPT_SHUTDOWN */

#if _FFR_REJECT_NUL_BYTE
	  case O_REJECTNUL:
		RejectNUL = atobool(val);
		break;
#endif /* _FFR_REJECT_NUL_BYTE */

#if _FFR_BOUNCE_QUEUE
	  case O_BOUNCEQUEUE:
		bouncequeue = newstr(val);
		break;
#endif /* _FFR_BOUNCE_QUEUE */

#if _FFR_ADD_BCC
	  case O_ADDBCC:
		AddBcc = atobool(val);
		break;
#endif
	  case O_USECOMPRESSEDIPV6ADDRESSES:
		UseCompressedIPv6Addresses = atobool(val);
		break;

	  default:
		if (tTd(37, 1))
		{
			if (isascii(opt) && isprint(opt))
				sm_dprintf("Warning: option %c unknown\n", opt);
			else
				sm_dprintf("Warning: option 0x%x unknown\n", opt);
		}
		break;
	}

	/*
	**  Options with suboptions are responsible for taking care
	**  of sticky-ness (e.g., that a command line setting is kept
	**  when reading in the sendmail.cf file).  This has to be done
	**  when the suboptions are parsed since each suboption must be
	**  sticky, not the root option.
	*/

	if (sticky && !bitset(OI_SUBOPT, o->o_flags))
		setbitn(opt, StickyOpt);
}
/*
**  SETCLASS -- set a string into a class
**
**	Parameters:
**		class -- the class to put the string in.
**		str -- the string to enter
**
**	Returns:
**		none.
**
**	Side Effects:
**		puts the word into the symbol table.
*/

void
setclass(class, str)
	int class;
	char *str;
{
	register STAB *s;

	if ((str[0] & 0377) == MATCHCLASS)
	{
		int mid;

		str++;
		mid = macid(str);
		if (mid == 0)
			return;

		if (tTd(37, 8))
			sm_dprintf("setclass(%s, $=%s)\n",
				   macname(class), macname(mid));
		copy_class(mid, class);
	}
	else
	{
		if (tTd(37, 8))
			sm_dprintf("setclass(%s, %s)\n", macname(class), str);

		s = stab(str, ST_CLASS, ST_ENTER);
		setbitn(bitidx(class), s->s_class);
	}
}
/*
**  MAKEMAPENTRY -- create a map entry
**
**	Parameters:
**		line -- the config file line
**
**	Returns:
**		A pointer to the map that has been created.
**		NULL if there was a syntax error.
**
**	Side Effects:
**		Enters the map into the dictionary.
*/

MAP *
makemapentry(line)
	char *line;
{
	register char *p;
	char *mapname;
	char *classname;
	register STAB *s;
	STAB *class;

	for (p = line; isascii(*p) && isspace(*p); p++)
		continue;
	if (!(isascii(*p) && isalnum(*p)))
	{
		syserr("readcf: config K line: no map name");
		return NULL;
	}

	mapname = p;
	while ((isascii(*++p) && isalnum(*p)) || *p == '_' || *p == '.')
		continue;
	if (*p != '\0')
		*p++ = '\0';
	while (isascii(*p) && isspace(*p))
		p++;
	if (!(isascii(*p) && isalnum(*p)))
	{
		syserr("readcf: config K line, map %s: no map class", mapname);
		return NULL;
	}
	classname = p;
	while (isascii(*++p) && isalnum(*p))
		continue;
	if (*p != '\0')
		*p++ = '\0';
	while (isascii(*p) && isspace(*p))
		p++;

	/* look up the class */
	class = stab(classname, ST_MAPCLASS, ST_FIND);
	if (class == NULL)
	{
		syserr("readcf: map %s: class %s not available", mapname,
			classname);
		return NULL;
	}

	/* enter the map */
	s = stab(mapname, ST_MAP, ST_ENTER);
	s->s_map.map_class = &class->s_mapclass;
	s->s_map.map_mname = newstr(mapname);

	if (class->s_mapclass.map_parse(&s->s_map, p))
		s->s_map.map_mflags |= MF_VALID;

	if (tTd(37, 5))
	{
		sm_dprintf("map %s, class %s, flags %lx, file %s,\n",
			   s->s_map.map_mname, s->s_map.map_class->map_cname,
			   s->s_map.map_mflags, s->s_map.map_file);
		sm_dprintf("\tapp %s, domain %s, rebuild %s\n",
			   s->s_map.map_app, s->s_map.map_domain,
			   s->s_map.map_rebuild);
	}
	return &s->s_map;
}
/*
**  STRTORWSET -- convert string to rewriting set number
**
**	Parameters:
**		p -- the pointer to the string to decode.
**		endp -- if set, store the trailing delimiter here.
**		stabmode -- ST_ENTER to create this entry, ST_FIND if
**			it must already exist.
**
**	Returns:
**		The appropriate ruleset number.
**		-1 if it is not valid (error already printed)
*/

int
strtorwset(p, endp, stabmode)
	char *p;
	char **endp;
	int stabmode;
{
	int ruleset;
	static int nextruleset = MAXRWSETS;

	while (isascii(*p) && isspace(*p))
		p++;
	if (!isascii(*p))
	{
		syserr("invalid ruleset name: \"%.20s\"", p);
		return -1;
	}
	if (isdigit(*p))
	{
		ruleset = strtol(p, endp, 10);
		if (ruleset >= MAXRWSETS / 2 || ruleset < 0)
		{
			syserr("bad ruleset %d (%d max)",
				ruleset, MAXRWSETS / 2);
			ruleset = -1;
		}
	}
	else
	{
		STAB *s;
		char delim;
		char *q = NULL;

		q = p;
		while (*p != '\0' && isascii(*p) && (isalnum(*p) || *p == '_'))
			p++;
		if (q == p || !(isascii(*q) && isalpha(*q)))
		{
			/* no valid characters */
			syserr("invalid ruleset name: \"%.20s\"", q);
			return -1;
		}
		while (isascii(*p) && isspace(*p))
			*p++ = '\0';
		delim = *p;
		if (delim != '\0')
			*p = '\0';
		s = stab(q, ST_RULESET, stabmode);
		if (delim != '\0')
			*p = delim;

		if (s == NULL)
			return -1;

		if (stabmode == ST_ENTER && delim == '=')
		{
			while (isascii(*++p) && isspace(*p))
				continue;
			if (!(isascii(*p) && isdigit(*p)))
			{
				syserr("bad ruleset definition \"%s\" (number required after `=')", q);
				ruleset = -1;
			}
			else
			{
				ruleset = strtol(p, endp, 10);
				if (ruleset >= MAXRWSETS / 2 || ruleset < 0)
				{
					syserr("bad ruleset number %d in \"%s\" (%d max)",
						ruleset, q, MAXRWSETS / 2);
					ruleset = -1;
				}
			}
		}
		else
		{
			if (endp != NULL)
				*endp = p;
			if (s->s_ruleset >= 0)
				ruleset = s->s_ruleset;
			else if ((ruleset = --nextruleset) < MAXRWSETS / 2)
			{
				syserr("%s: too many named rulesets (%d max)",
					q, MAXRWSETS / 2);
				ruleset = -1;
			}
		}
		if (s->s_ruleset >= 0 &&
		    ruleset >= 0 &&
		    ruleset != s->s_ruleset)
		{
			syserr("%s: ruleset changed value (old %d, new %d)",
				q, s->s_ruleset, ruleset);
			ruleset = s->s_ruleset;
		}
		else if (ruleset >= 0)
		{
			s->s_ruleset = ruleset;
		}
		if (stabmode == ST_ENTER && ruleset >= 0)
		{
			char *h = NULL;

			if (RuleSetNames[ruleset] != NULL)
				sm_free(RuleSetNames[ruleset]); /* XXX */
			if (delim != '\0' && (h = strchr(q, delim)) != NULL)
				*h = '\0';
			RuleSetNames[ruleset] = newstr(q);
			if (delim == '/' && h != NULL)
				*h = delim;	/* put back delim */
		}
	}
	return ruleset;
}
/*
**  SETTIMEOUT -- set an individual timeout
**
**	Parameters:
**		name -- the name of the timeout.
**		val -- the value of the timeout.
**		sticky -- if set, don't let other setoptions override
**			this value.
**
**	Returns:
**		none.
*/

/* set if Timeout sub-option is stuck */
static BITMAP256	StickyTimeoutOpt;

static struct timeoutinfo
{
	char		*to_name;	/* long name of timeout */
	unsigned char	to_code;	/* code for option */
} TimeOutTab[] =
{
#define TO_INITIAL			0x01
	{ "initial",			TO_INITIAL			},
#define TO_MAIL				0x02
	{ "mail",			TO_MAIL				},
#define TO_RCPT				0x03
	{ "rcpt",			TO_RCPT				},
#define TO_DATAINIT			0x04
	{ "datainit",			TO_DATAINIT			},
#define TO_DATABLOCK			0x05
	{ "datablock",			TO_DATABLOCK			},
#define TO_DATAFINAL			0x06
	{ "datafinal",			TO_DATAFINAL			},
#define TO_COMMAND			0x07
	{ "command",			TO_COMMAND			},
#define TO_RSET				0x08
	{ "rset",			TO_RSET				},
#define TO_HELO				0x09
	{ "helo",			TO_HELO				},
#define TO_QUIT				0x0A
	{ "quit",			TO_QUIT				},
#define TO_MISC				0x0B
	{ "misc",			TO_MISC				},
#define TO_IDENT			0x0C
	{ "ident",			TO_IDENT			},
#define TO_FILEOPEN			0x0D
	{ "fileopen",			TO_FILEOPEN			},
#define TO_CONNECT			0x0E
	{ "connect",			TO_CONNECT			},
#define TO_ICONNECT			0x0F
	{ "iconnect",			TO_ICONNECT			},
#define TO_QUEUEWARN			0x10
	{ "queuewarn",			TO_QUEUEWARN			},
	{ "queuewarn.*",		TO_QUEUEWARN			},
#define TO_QUEUEWARN_NORMAL		0x11
	{ "queuewarn.normal",		TO_QUEUEWARN_NORMAL		},
#define TO_QUEUEWARN_URGENT		0x12
	{ "queuewarn.urgent",		TO_QUEUEWARN_URGENT		},
#define TO_QUEUEWARN_NON_URGENT		0x13
	{ "queuewarn.non-urgent",	TO_QUEUEWARN_NON_URGENT		},
#define TO_QUEUERETURN			0x14
	{ "queuereturn",		TO_QUEUERETURN			},
	{ "queuereturn.*",		TO_QUEUERETURN			},
#define TO_QUEUERETURN_NORMAL		0x15
	{ "queuereturn.normal",		TO_QUEUERETURN_NORMAL		},
#define TO_QUEUERETURN_URGENT		0x16
	{ "queuereturn.urgent",		TO_QUEUERETURN_URGENT		},
#define TO_QUEUERETURN_NON_URGENT	0x17
	{ "queuereturn.non-urgent",	TO_QUEUERETURN_NON_URGENT	},
#define TO_HOSTSTATUS			0x18
	{ "hoststatus",			TO_HOSTSTATUS			},
#define TO_RESOLVER_RETRANS		0x19
	{ "resolver.retrans",		TO_RESOLVER_RETRANS		},
#define TO_RESOLVER_RETRANS_NORMAL	0x1A
	{ "resolver.retrans.normal",	TO_RESOLVER_RETRANS_NORMAL	},
#define TO_RESOLVER_RETRANS_FIRST	0x1B
	{ "resolver.retrans.first",	TO_RESOLVER_RETRANS_FIRST	},
#define TO_RESOLVER_RETRY		0x1C
	{ "resolver.retry",		TO_RESOLVER_RETRY		},
#define TO_RESOLVER_RETRY_NORMAL	0x1D
	{ "resolver.retry.normal",	TO_RESOLVER_RETRY_NORMAL	},
#define TO_RESOLVER_RETRY_FIRST		0x1E
	{ "resolver.retry.first",	TO_RESOLVER_RETRY_FIRST		},
#define TO_CONTROL			0x1F
	{ "control",			TO_CONTROL			},
#define TO_LHLO				0x20
	{ "lhlo",			TO_LHLO				},
#define TO_AUTH				0x21
	{ "auth",			TO_AUTH				},
#define TO_STARTTLS			0x22
	{ "starttls",			TO_STARTTLS			},
#define TO_ACONNECT			0x23
	{ "aconnect",			TO_ACONNECT			},
#define TO_QUEUEWARN_DSN		0x24
	{ "queuewarn.dsn",		TO_QUEUEWARN_DSN		},
#define TO_QUEUERETURN_DSN		0x25
	{ "queuereturn.dsn",		TO_QUEUERETURN_DSN		},
	{ NULL,				0				},
};


static void
settimeout(name, val, sticky)
	char *name;
	char *val;
	bool sticky;
{
	register struct timeoutinfo *to;
	int i, addopts;
	time_t toval;

	if (tTd(37, 2))
		sm_dprintf("settimeout(%s = %s)", name, val);

	for (to = TimeOutTab; to->to_name != NULL; to++)
	{
		if (sm_strcasecmp(to->to_name, name) == 0)
			break;
	}

	if (to->to_name == NULL)
	{
		errno = 0; /* avoid bogus error text */
		syserr("settimeout: invalid timeout %s", name);
		return;
	}

	/*
	**  See if this option is preset for us.
	*/

	if (!sticky && bitnset(to->to_code, StickyTimeoutOpt))
	{
		if (tTd(37, 2))
			sm_dprintf(" (ignored)\n");
		return;
	}

	if (tTd(37, 2))
		sm_dprintf("\n");

	toval = convtime(val, 'm');
	addopts = 0;

	switch (to->to_code)
	{
	  case TO_INITIAL:
		TimeOuts.to_initial = toval;
		break;

	  case TO_MAIL:
		TimeOuts.to_mail = toval;
		break;

	  case TO_RCPT:
		TimeOuts.to_rcpt = toval;
		break;

	  case TO_DATAINIT:
		TimeOuts.to_datainit = toval;
		break;

	  case TO_DATABLOCK:
		TimeOuts.to_datablock = toval;
		break;

	  case TO_DATAFINAL:
		TimeOuts.to_datafinal = toval;
		break;

	  case TO_COMMAND:
		TimeOuts.to_nextcommand = toval;
		break;

	  case TO_RSET:
		TimeOuts.to_rset = toval;
		break;

	  case TO_HELO:
		TimeOuts.to_helo = toval;
		break;

	  case TO_QUIT:
		TimeOuts.to_quit = toval;
		break;

	  case TO_MISC:
		TimeOuts.to_miscshort = toval;
		break;

	  case TO_IDENT:
		TimeOuts.to_ident = toval;
		break;

	  case TO_FILEOPEN:
		TimeOuts.to_fileopen = toval;
		break;

	  case TO_CONNECT:
		TimeOuts.to_connect = toval;
		break;

	  case TO_ICONNECT:
		TimeOuts.to_iconnect = toval;
		break;

	  case TO_ACONNECT:
		TimeOuts.to_aconnect = toval;
		break;

	  case TO_QUEUEWARN:
		toval = convtime(val, 'h');
		TimeOuts.to_q_warning[TOC_NORMAL] = toval;
		TimeOuts.to_q_warning[TOC_URGENT] = toval;
		TimeOuts.to_q_warning[TOC_NONURGENT] = toval;
		TimeOuts.to_q_warning[TOC_DSN] = toval;
		addopts = 2;
		break;

	  case TO_QUEUEWARN_NORMAL:
		toval = convtime(val, 'h');
		TimeOuts.to_q_warning[TOC_NORMAL] = toval;
		break;

	  case TO_QUEUEWARN_URGENT:
		toval = convtime(val, 'h');
		TimeOuts.to_q_warning[TOC_URGENT] = toval;
		break;

	  case TO_QUEUEWARN_NON_URGENT:
		toval = convtime(val, 'h');
		TimeOuts.to_q_warning[TOC_NONURGENT] = toval;
		break;

	  case TO_QUEUEWARN_DSN:
		toval = convtime(val, 'h');
		TimeOuts.to_q_warning[TOC_DSN] = toval;
		break;

	  case TO_QUEUERETURN:
		toval = convtime(val, 'd');
		TimeOuts.to_q_return[TOC_NORMAL] = toval;
		TimeOuts.to_q_return[TOC_URGENT] = toval;
		TimeOuts.to_q_return[TOC_NONURGENT] = toval;
		TimeOuts.to_q_return[TOC_DSN] = toval;
		addopts = 2;
		break;

	  case TO_QUEUERETURN_NORMAL:
		toval = convtime(val, 'd');
		TimeOuts.to_q_return[TOC_NORMAL] = toval;
		break;

	  case TO_QUEUERETURN_URGENT:
		toval = convtime(val, 'd');
		TimeOuts.to_q_return[TOC_URGENT] = toval;
		break;

	  case TO_QUEUERETURN_NON_URGENT:
		toval = convtime(val, 'd');
		TimeOuts.to_q_return[TOC_NONURGENT] = toval;
		break;

	  case TO_QUEUERETURN_DSN:
		toval = convtime(val, 'd');
		TimeOuts.to_q_return[TOC_DSN] = toval;
		break;

	  case TO_HOSTSTATUS:
		MciInfoTimeout = toval;
		break;

	  case TO_RESOLVER_RETRANS:
		toval = convtime(val, 's');
		TimeOuts.res_retrans[RES_TO_DEFAULT] = toval;
		TimeOuts.res_retrans[RES_TO_FIRST] = toval;
		TimeOuts.res_retrans[RES_TO_NORMAL] = toval;
		addopts = 2;
		break;

	  case TO_RESOLVER_RETRY:
		i = atoi(val);
		TimeOuts.res_retry[RES_TO_DEFAULT] = i;
		TimeOuts.res_retry[RES_TO_FIRST] = i;
		TimeOuts.res_retry[RES_TO_NORMAL] = i;
		addopts = 2;
		break;

	  case TO_RESOLVER_RETRANS_NORMAL:
		TimeOuts.res_retrans[RES_TO_NORMAL] = convtime(val, 's');
		break;

	  case TO_RESOLVER_RETRY_NORMAL:
		TimeOuts.res_retry[RES_TO_NORMAL] = atoi(val);
		break;

	  case TO_RESOLVER_RETRANS_FIRST:
		TimeOuts.res_retrans[RES_TO_FIRST] = convtime(val, 's');
		break;

	  case TO_RESOLVER_RETRY_FIRST:
		TimeOuts.res_retry[RES_TO_FIRST] = atoi(val);
		break;

	  case TO_CONTROL:
		TimeOuts.to_control = toval;
		break;

	  case TO_LHLO:
		TimeOuts.to_lhlo = toval;
		break;

#if SASL
	  case TO_AUTH:
		TimeOuts.to_auth = toval;
		break;
#endif /* SASL */

#if STARTTLS
	  case TO_STARTTLS:
		TimeOuts.to_starttls = toval;
		break;
#endif /* STARTTLS */

	  default:
		syserr("settimeout: invalid timeout %s", name);
		break;
	}

	if (sticky)
	{
		for (i = 0; i <= addopts; i++)
			setbitn(to->to_code + i, StickyTimeoutOpt);
	}
}
/*
**  INITTIMEOUTS -- parse and set timeout values
**
**	Parameters:
**		val -- a pointer to the values.  If NULL, do initial
**			settings.
**		sticky -- if set, don't let other setoptions override
**			this suboption value.
**
**	Returns:
**		none.
**
**	Side Effects:
**		Initializes the TimeOuts structure
*/

void
inittimeouts(val, sticky)
	register char *val;
	bool sticky;
{
	register char *p;

	if (tTd(37, 2))
		sm_dprintf("inittimeouts(%s)\n", val == NULL ? "<NULL>" : val);
	if (val == NULL)
	{
		TimeOuts.to_connect = (time_t) 0 SECONDS;
		TimeOuts.to_aconnect = (time_t) 0 SECONDS;
		TimeOuts.to_iconnect = (time_t) 0 SECONDS;
		TimeOuts.to_initial = (time_t) 5 MINUTES;
		TimeOuts.to_helo = (time_t) 5 MINUTES;
		TimeOuts.to_mail = (time_t) 10 MINUTES;
		TimeOuts.to_rcpt = (time_t) 1 HOUR;
		TimeOuts.to_datainit = (time_t) 5 MINUTES;
		TimeOuts.to_datablock = (time_t) 1 HOUR;
		TimeOuts.to_datafinal = (time_t) 1 HOUR;
		TimeOuts.to_rset = (time_t) 5 MINUTES;
		TimeOuts.to_quit = (time_t) 2 MINUTES;
		TimeOuts.to_nextcommand = (time_t) 1 HOUR;
		TimeOuts.to_miscshort = (time_t) 2 MINUTES;
#if IDENTPROTO
		TimeOuts.to_ident = (time_t) 5 SECONDS;
#else /* IDENTPROTO */
		TimeOuts.to_ident = (time_t) 0 SECONDS;
#endif /* IDENTPROTO */
		TimeOuts.to_fileopen = (time_t) 60 SECONDS;
		TimeOuts.to_control = (time_t) 2 MINUTES;
		TimeOuts.to_lhlo = (time_t) 2 MINUTES;
#if SASL
		TimeOuts.to_auth = (time_t) 10 MINUTES;
#endif /* SASL */
#if STARTTLS
		TimeOuts.to_starttls = (time_t) 1 HOUR;
#endif /* STARTTLS */
		if (tTd(37, 5))
		{
			sm_dprintf("Timeouts:\n");
			sm_dprintf("  connect = %ld\n",
				   (long) TimeOuts.to_connect);
			sm_dprintf("  aconnect = %ld\n",
				   (long) TimeOuts.to_aconnect);
			sm_dprintf("  initial = %ld\n",
				   (long) TimeOuts.to_initial);
			sm_dprintf("  helo = %ld\n", (long) TimeOuts.to_helo);
			sm_dprintf("  mail = %ld\n", (long) TimeOuts.to_mail);
			sm_dprintf("  rcpt = %ld\n", (long) TimeOuts.to_rcpt);
			sm_dprintf("  datainit = %ld\n",
				   (long) TimeOuts.to_datainit);
			sm_dprintf("  datablock = %ld\n",
				   (long) TimeOuts.to_datablock);
			sm_dprintf("  datafinal = %ld\n",
				   (long) TimeOuts.to_datafinal);
			sm_dprintf("  rset = %ld\n", (long) TimeOuts.to_rset);
			sm_dprintf("  quit = %ld\n", (long) TimeOuts.to_quit);
			sm_dprintf("  nextcommand = %ld\n",
				   (long) TimeOuts.to_nextcommand);
			sm_dprintf("  miscshort = %ld\n",
				   (long) TimeOuts.to_miscshort);
			sm_dprintf("  ident = %ld\n", (long) TimeOuts.to_ident);
			sm_dprintf("  fileopen = %ld\n",
				   (long) TimeOuts.to_fileopen);
			sm_dprintf("  lhlo = %ld\n",
				   (long) TimeOuts.to_lhlo);
			sm_dprintf("  control = %ld\n",
				   (long) TimeOuts.to_control);
		}
		return;
	}

	for (;; val = p)
	{
		while (isascii(*val) && isspace(*val))
			val++;
		if (*val == '\0')
			break;
		for (p = val; *p != '\0' && *p != ','; p++)
			continue;
		if (*p != '\0')
			*p++ = '\0';

		if (isascii(*val) && isdigit(*val))
		{
			/* old syntax -- set everything */
			TimeOuts.to_mail = convtime(val, 'm');
			TimeOuts.to_rcpt = TimeOuts.to_mail;
			TimeOuts.to_datainit = TimeOuts.to_mail;
			TimeOuts.to_datablock = TimeOuts.to_mail;
			TimeOuts.to_datafinal = TimeOuts.to_mail;
			TimeOuts.to_nextcommand = TimeOuts.to_mail;
			if (sticky)
			{
				setbitn(TO_MAIL, StickyTimeoutOpt);
				setbitn(TO_RCPT, StickyTimeoutOpt);
				setbitn(TO_DATAINIT, StickyTimeoutOpt);
				setbitn(TO_DATABLOCK, StickyTimeoutOpt);
				setbitn(TO_DATAFINAL, StickyTimeoutOpt);
				setbitn(TO_COMMAND, StickyTimeoutOpt);
			}
			continue;
		}
		else
		{
			register char *q = strchr(val, ':');

			if (q == NULL && (q = strchr(val, '=')) == NULL)
			{
				/* syntax error */
				continue;
			}
			*q++ = '\0';
			settimeout(val, q, sticky);
		}
	}
}
