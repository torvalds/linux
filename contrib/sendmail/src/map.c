/*
 * Copyright (c) 1998-2008 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1992, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>

SM_RCSID("@(#)$Id: map.c,v 8.713 2013-11-22 20:51:55 ca Exp $")

#if LDAPMAP
# include <sm/ldap.h>
#endif /* LDAPMAP */

#if NDBM
# include <ndbm.h>
# ifdef R_FIRST
  ERROR README:	You are running the Berkeley DB version of ndbm.h.  See
  ERROR README:	the README file about tweaking Berkeley DB so it can
  ERROR README:	coexist with NDBM, or delete -DNDBM from the Makefile
  ERROR README: and use -DNEWDB instead.
# endif /* R_FIRST */
#endif /* NDBM */
#if NEWDB
# include "sm/bdb.h"
#endif /* NEWDB */
#if NIS
  struct dom_binding;	/* forward reference needed on IRIX */
# include <rpcsvc/ypclnt.h>
# if NDBM
#  define NDBM_YP_COMPAT	/* create YP-compatible NDBM files */
# endif /* NDBM */
#endif /* NIS */

#include "map.h"

#if NEWDB
# if DB_VERSION_MAJOR < 2
static bool	db_map_open __P((MAP *, int, char *, DBTYPE, const void *));
# endif /* DB_VERSION_MAJOR < 2 */
# if DB_VERSION_MAJOR == 2
static bool	db_map_open __P((MAP *, int, char *, DBTYPE, DB_INFO *));
# endif /* DB_VERSION_MAJOR == 2 */
# if DB_VERSION_MAJOR > 2
static bool	db_map_open __P((MAP *, int, char *, DBTYPE, void **));
# endif /* DB_VERSION_MAJOR > 2 */
#endif /* NEWDB */
static bool	extract_canonname __P((char *, char *, char *, char[], int));
static void	map_close __P((STAB *, int));
static void	map_init __P((STAB *, int));
#ifdef LDAPMAP
static STAB *	ldapmap_findconn __P((SM_LDAP_STRUCT *));
#endif /* LDAPMAP */
#if NISPLUS
static bool	nisplus_getcanonname __P((char *, int, int *));
#endif /* NISPLUS */
#if NIS
static bool	nis_getcanonname __P((char *, int, int *));
#endif /* NIS */
#if NETINFO
static bool	ni_getcanonname __P((char *, int, int *));
#endif /* NETINFO */
static bool	text_getcanonname __P((char *, int, int *));
#if SOCKETMAP
static STAB	*socket_map_findconn __P((const char*));

/* XXX arbitrary limit for sanity */
# define SOCKETMAP_MAXL 1000000
#endif /* SOCKETMAP */

/* default error message for trying to open a map in write mode */
#ifdef ENOSYS
# define SM_EMAPCANTWRITE	ENOSYS
#else /* ENOSYS */
# ifdef EFTYPE
#  define SM_EMAPCANTWRITE	EFTYPE
# else /* EFTYPE */
#  define SM_EMAPCANTWRITE	ENXIO
# endif /* EFTYPE */
#endif /* ENOSYS */

/*
**  MAP.C -- implementations for various map classes.
**
**	Each map class implements a series of functions:
**
**	bool map_parse(MAP *map, char *args)
**		Parse the arguments from the config file.  Return true
**		if they were ok, false otherwise.  Fill in map with the
**		values.
**
**	char *map_lookup(MAP *map, char *key, char **args, int *pstat)
**		Look up the key in the given map.  If found, do any
**		rewriting the map wants (including "args" if desired)
**		and return the value.  Set *pstat to the appropriate status
**		on error and return NULL.  Args will be NULL if called
**		from the alias routines, although this should probably
**		not be relied upon.  It is suggested you call map_rewrite
**		to return the results -- it takes care of null termination
**		and uses a dynamically expanded buffer as needed.
**
**	void map_store(MAP *map, char *key, char *value)
**		Store the key:value pair in the map.
**
**	bool map_open(MAP *map, int mode)
**		Open the map for the indicated mode.  Mode should
**		be either O_RDONLY or O_RDWR.  Return true if it
**		was opened successfully, false otherwise.  If the open
**		failed and the MF_OPTIONAL flag is not set, it should
**		also print an error.  If the MF_ALIAS bit is set
**		and this map class understands the @:@ convention, it
**		should call aliaswait() before returning.
**
**	void map_close(MAP *map)
**		Close the map.
**
**	This file also includes the implementation for getcanonname.
**	It is currently implemented in a pretty ad-hoc manner; it ought
**	to be more properly integrated into the map structure.
*/

#if O_EXLOCK && HASFLOCK && !BOGUS_O_EXCL
# define LOCK_ON_OPEN	1	/* we can open/create a locked file */
#else /* O_EXLOCK && HASFLOCK && !BOGUS_O_EXCL */
# define LOCK_ON_OPEN	0	/* no such luck -- bend over backwards */
#endif /* O_EXLOCK && HASFLOCK && !BOGUS_O_EXCL */

/*
**  MAP_PARSEARGS -- parse config line arguments for database lookup
**
**	This is a generic version of the map_parse method.
**
**	Parameters:
**		map -- the map being initialized.
**		ap -- a pointer to the args on the config line.
**
**	Returns:
**		true -- if everything parsed OK.
**		false -- otherwise.
**
**	Side Effects:
**		null terminates the filename; stores it in map
*/

bool
map_parseargs(map, ap)
	MAP *map;
	char *ap;
{
	register char *p = ap;

	/*
	**  There is no check whether there is really an argument,
	**  but that's not important enough to warrant extra code.
	*/

	map->map_mflags |= MF_TRY0NULL|MF_TRY1NULL;
	map->map_spacesub = SpaceSub;	/* default value */
	for (;;)
	{
		while (isascii(*p) && isspace(*p))
			p++;
		if (*p != '-')
			break;
		switch (*++p)
		{
		  case 'N':
			map->map_mflags |= MF_INCLNULL;
			map->map_mflags &= ~MF_TRY0NULL;
			break;

		  case 'O':
			map->map_mflags &= ~MF_TRY1NULL;
			break;

		  case 'o':
			map->map_mflags |= MF_OPTIONAL;
			break;

		  case 'f':
			map->map_mflags |= MF_NOFOLDCASE;
			break;

		  case 'm':
			map->map_mflags |= MF_MATCHONLY;
			break;

		  case 'A':
			map->map_mflags |= MF_APPEND;
			break;

		  case 'q':
			map->map_mflags |= MF_KEEPQUOTES;
			break;

		  case 'a':
			map->map_app = ++p;
			break;

		  case 'd':
			{
				char *h;

				++p;
				h = strchr(p, ' ');
				if (h != NULL)
					*h = '\0';
				map->map_timeout = convtime(p, 's');
				if (h != NULL)
					*h = ' ';
			}
			break;

		  case 'T':
			map->map_tapp = ++p;
			break;

		  case 'k':
			while (isascii(*++p) && isspace(*p))
				continue;
			map->map_keycolnm = p;
			break;

		  case 'v':
			while (isascii(*++p) && isspace(*p))
				continue;
			map->map_valcolnm = p;
			break;

		  case 'z':
			if (*++p != '\\')
				map->map_coldelim = *p;
			else
			{
				switch (*++p)
				{
				  case 'n':
					map->map_coldelim = '\n';
					break;

				  case 't':
					map->map_coldelim = '\t';
					break;

				  default:
					map->map_coldelim = '\\';
				}
			}
			break;

		  case 't':
			map->map_mflags |= MF_NODEFER;
			break;


		  case 'S':
			map->map_spacesub = *++p;
			break;

		  case 'D':
			map->map_mflags |= MF_DEFER;
			break;

		  default:
			syserr("Illegal option %c map %s", *p, map->map_mname);
			break;
		}
		while (*p != '\0' && !(isascii(*p) && isspace(*p)))
			p++;
		if (*p != '\0')
			*p++ = '\0';
	}
	if (map->map_app != NULL)
		map->map_app = newstr(map->map_app);
	if (map->map_tapp != NULL)
		map->map_tapp = newstr(map->map_tapp);
	if (map->map_keycolnm != NULL)
		map->map_keycolnm = newstr(map->map_keycolnm);
	if (map->map_valcolnm != NULL)
		map->map_valcolnm = newstr(map->map_valcolnm);

	if (*p != '\0')
	{
		map->map_file = p;
		while (*p != '\0' && !(isascii(*p) && isspace(*p)))
			p++;
		if (*p != '\0')
			*p++ = '\0';
		map->map_file = newstr(map->map_file);
	}

	while (*p != '\0' && isascii(*p) && isspace(*p))
		p++;
	if (*p != '\0')
		map->map_rebuild = newstr(p);

	if (map->map_file == NULL &&
	    !bitset(MCF_OPTFILE, map->map_class->map_cflags))
	{
		syserr("No file name for %s map %s",
			map->map_class->map_cname, map->map_mname);
		return false;
	}
	return true;
}
/*
**  MAP_REWRITE -- rewrite a database key, interpolating %n indications.
**
**	It also adds the map_app string.  It can be used as a utility
**	in the map_lookup method.
**
**	Parameters:
**		map -- the map that causes this.
**		s -- the string to rewrite, NOT necessarily null terminated.
**		slen -- the length of s.
**		av -- arguments to interpolate into buf.
**
**	Returns:
**		Pointer to rewritten result.  This is static data that
**		should be copied if it is to be saved!
*/

char *
map_rewrite(map, s, slen, av)
	register MAP *map;
	register const char *s;
	size_t slen;
	char **av;
{
	register char *bp;
	register char c;
	char **avp;
	register char *ap;
	size_t l;
	size_t len;
	static size_t buflen = 0;
	static char *buf = NULL;

	if (tTd(39, 1))
	{
		sm_dprintf("map_rewrite(%.*s), av =", (int) slen, s);
		if (av == NULL)
			sm_dprintf(" (nullv)");
		else
		{
			for (avp = av; *avp != NULL; avp++)
				sm_dprintf("\n\t%s", *avp);
		}
		sm_dprintf("\n");
	}

	/* count expected size of output (can safely overestimate) */
	l = len = slen;
	if (av != NULL)
	{
		const char *sp = s;

		while (l-- > 0 && (c = *sp++) != '\0')
		{
			if (c != '%')
				continue;
			if (l-- <= 0)
				break;
			c = *sp++;
			if (!(isascii(c) && isdigit(c)))
				continue;
			for (avp = av; --c >= '0' && *avp != NULL; avp++)
				continue;
			if (*avp == NULL)
				continue;
			len += strlen(*avp);
		}
	}
	if (map->map_app != NULL)
		len += strlen(map->map_app);
	if (buflen < ++len)
	{
		/* need to malloc additional space */
		buflen = len;
		if (buf != NULL)
			sm_free(buf);
		buf = sm_pmalloc_x(buflen);
	}

	bp = buf;
	if (av == NULL)
	{
		memmove(bp, s, slen);
		bp += slen;

		/* assert(len > slen); */
		len -= slen;
	}
	else
	{
		while (slen-- > 0 && (c = *s++) != '\0')
		{
			if (c != '%')
			{
  pushc:
				if (len-- <= 1)
				     break;
				*bp++ = c;
				continue;
			}
			if (slen-- <= 0 || (c = *s++) == '\0')
				c = '%';
			if (c == '%')
				goto pushc;
			if (!(isascii(c) && isdigit(c)))
			{
				if (len-- <= 1)
				     break;
				*bp++ = '%';
				goto pushc;
			}
			for (avp = av; --c >= '0' && *avp != NULL; avp++)
				continue;
			if (*avp == NULL)
				continue;

			/* transliterate argument into output string */
			for (ap = *avp; (c = *ap++) != '\0' && len > 0; --len)
				*bp++ = c;
		}
	}
	if (map->map_app != NULL && len > 0)
		(void) sm_strlcpy(bp, map->map_app, len);
	else
		*bp = '\0';
	if (tTd(39, 1))
		sm_dprintf("map_rewrite => %s\n", buf);
	return buf;
}
/*
**  INITMAPS -- rebuild alias maps
**
**	Parameters:
**		none.
**
**	Returns:
**		none.
*/

void
initmaps()
{
#if XDEBUG
	checkfd012("entering initmaps");
#endif /* XDEBUG */
	stabapply(map_init, 0);
#if XDEBUG
	checkfd012("exiting initmaps");
#endif /* XDEBUG */
}
/*
**  MAP_INIT -- rebuild a map
**
**	Parameters:
**		s -- STAB entry: if map: try to rebuild
**		unused -- unused variable
**
**	Returns:
**		none.
**
**	Side Effects:
**		will close already open rebuildable map.
*/

/* ARGSUSED1 */
static void
map_init(s, unused)
	register STAB *s;
	int unused;
{
	register MAP *map;

	/* has to be a map */
	if (s->s_symtype != ST_MAP)
		return;

	map = &s->s_map;
	if (!bitset(MF_VALID, map->map_mflags))
		return;

	if (tTd(38, 2))
		sm_dprintf("map_init(%s:%s, %s)\n",
			map->map_class->map_cname == NULL ? "NULL" :
				map->map_class->map_cname,
			map->map_mname == NULL ? "NULL" : map->map_mname,
			map->map_file == NULL ? "NULL" : map->map_file);

	if (!bitset(MF_ALIAS, map->map_mflags) ||
	    !bitset(MCF_REBUILDABLE, map->map_class->map_cflags))
	{
		if (tTd(38, 3))
			sm_dprintf("\tnot rebuildable\n");
		return;
	}

	/* if already open, close it (for nested open) */
	if (bitset(MF_OPEN, map->map_mflags))
	{
		map->map_mflags |= MF_CLOSING;
		map->map_class->map_close(map);
		map->map_mflags &= ~(MF_OPEN|MF_WRITABLE|MF_CLOSING);
	}

	(void) rebuildaliases(map, false);
	return;
}
/*
**  OPENMAP -- open a map
**
**	Parameters:
**		map -- map to open (it must not be open).
**
**	Returns:
**		whether open succeeded.
*/

bool
openmap(map)
	MAP *map;
{
	bool restore = false;
	bool savehold = HoldErrs;
	bool savequick = QuickAbort;
	int saveerrors = Errors;

	if (!bitset(MF_VALID, map->map_mflags))
		return false;

	/* better safe than sorry... */
	if (bitset(MF_OPEN, map->map_mflags))
		return true;

	/* Don't send a map open error out via SMTP */
	if ((OnlyOneError || QuickAbort) &&
	    (OpMode == MD_SMTP || OpMode == MD_DAEMON))
	{
		restore = true;
		HoldErrs = true;
		QuickAbort = false;
	}

	errno = 0;
	if (map->map_class->map_open(map, O_RDONLY))
	{
		if (tTd(38, 4))
			sm_dprintf("openmap()\t%s:%s %s: valid\n",
				map->map_class->map_cname == NULL ? "NULL" :
					map->map_class->map_cname,
				map->map_mname == NULL ? "NULL" :
					map->map_mname,
				map->map_file == NULL ? "NULL" :
					map->map_file);
		map->map_mflags |= MF_OPEN;
		map->map_pid = CurrentPid;
	}
	else
	{
		if (tTd(38, 4))
			sm_dprintf("openmap()\t%s:%s %s: invalid%s%s\n",
				map->map_class->map_cname == NULL ? "NULL" :
					map->map_class->map_cname,
				map->map_mname == NULL ? "NULL" :
					map->map_mname,
				map->map_file == NULL ? "NULL" :
					map->map_file,
				errno == 0 ? "" : ": ",
				errno == 0 ? "" : sm_errstring(errno));
		if (!bitset(MF_OPTIONAL, map->map_mflags))
		{
			extern MAPCLASS BogusMapClass;

			map->map_orgclass = map->map_class;
			map->map_class = &BogusMapClass;
			map->map_mflags |= MF_OPEN|MF_OPENBOGUS;
			map->map_pid = CurrentPid;
		}
		else
		{
			/* don't try again */
			map->map_mflags &= ~MF_VALID;
		}
	}

	if (restore)
	{
		Errors = saveerrors;
		HoldErrs = savehold;
		QuickAbort = savequick;
	}

	return bitset(MF_OPEN, map->map_mflags);
}
/*
**  CLOSEMAPS -- close all open maps opened by the current pid.
**
**	Parameters:
**		bogus -- only close bogus maps.
**
**	Returns:
**		none.
*/

void
closemaps(bogus)
	bool bogus;
{
	stabapply(map_close, bogus);
}
/*
**  MAP_CLOSE -- close a map opened by the current pid.
**
**	Parameters:
**		s -- STAB entry: if map: try to close
**		bogus -- only close bogus maps or MCF_NOTPERSIST maps.
**
**	Returns:
**		none.
*/

/* ARGSUSED1 */
static void
map_close(s, bogus)
	register STAB *s;
	int bogus;	/* int because of stabapply(), used as bool */
{
	MAP *map;
	extern MAPCLASS BogusMapClass;

	if (s->s_symtype != ST_MAP)
		return;

	map = &s->s_map;

	/*
	**  close the map iff:
	**  it is valid and open and opened by this process
	**  and (!bogus or it's a bogus map or it is not persistent)
	**  negate this: return iff
	**  it is not valid or it is not open or not opened by this process
	**  or (bogus and it's not a bogus map and it's not not-persistent)
	*/

	if (!bitset(MF_VALID, map->map_mflags) ||
	    !bitset(MF_OPEN, map->map_mflags) ||
	    bitset(MF_CLOSING, map->map_mflags) ||
	    map->map_pid != CurrentPid ||
	    (bogus && map->map_class != &BogusMapClass &&
	     !bitset(MCF_NOTPERSIST, map->map_class->map_cflags)))
		return;

	if (map->map_class == &BogusMapClass && map->map_orgclass != NULL &&
	    map->map_orgclass != &BogusMapClass)
		map->map_class = map->map_orgclass;
	if (tTd(38, 5))
		sm_dprintf("closemaps: closing %s (%s)\n",
			map->map_mname == NULL ? "NULL" : map->map_mname,
			map->map_file == NULL ? "NULL" : map->map_file);

	if (!bitset(MF_OPENBOGUS, map->map_mflags))
	{
		map->map_mflags |= MF_CLOSING;
		map->map_class->map_close(map);
	}
	map->map_mflags &= ~(MF_OPEN|MF_WRITABLE|MF_OPENBOGUS|MF_CLOSING);
}

#if defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN)
extern int getdomainname();

/* this is mainly for backward compatibility in Sun environment */
static char *
sun_init_domain()
{
	/*
	**  Get the domain name from the kernel.
	**  If it does not start with a leading dot, then remove
	**  the first component.  Since leading dots are funny Unix
	**  files, we treat a leading "+" the same as a leading dot.
	**  Finally, force there to be at least one dot in the domain name
	**  (i.e. top-level domains are not allowed, like "com", must be
	**  something like "sun.com").
	*/

	char buf[MAXNAME];
	char *period, *autodomain;

	if (getdomainname(buf, sizeof buf) < 0)
		return NULL;

	if (buf[0] == '\0')
		return NULL;

	if (tTd(0, 20))
		printf("domainname = %s\n", buf);

	if (buf[0] == '+')
		buf[0] = '.';
	period = strchr(buf, '.');
	if (period == NULL)
		autodomain = buf;
	else
		autodomain = period + 1;
	if (strchr(autodomain, '.') == NULL)
		return newstr(buf);
	else
		return newstr(autodomain);
}
#endif /* SUN_EXTENSIONS && SUN_INIT_DOMAIN */

/*
**  GETCANONNAME -- look up name using service switch
**
**	Parameters:
**		host -- the host name to look up.
**		hbsize -- the size of the host buffer.
**		trymx -- if set, try MX records.
**		pttl -- pointer to return TTL (can be NULL).
**
**	Returns:
**		true -- if the host was found.
**		false -- otherwise.
*/

bool
getcanonname(host, hbsize, trymx, pttl)
	char *host;
	int hbsize;
	bool trymx;
	int *pttl;
{
	int nmaps;
	int mapno;
	bool found = false;
	bool got_tempfail = false;
	auto int status = EX_UNAVAILABLE;
	char *maptype[MAXMAPSTACK];
	short mapreturn[MAXMAPACTIONS];
#if defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN)
	bool should_try_nis_domain = false;
	static char *nis_domain = NULL;
#endif

	nmaps = switch_map_find("hosts", maptype, mapreturn);
	if (pttl != 0)
		*pttl = SM_DEFAULT_TTL;
	for (mapno = 0; mapno < nmaps; mapno++)
	{
		int i;

		if (tTd(38, 20))
			sm_dprintf("getcanonname(%s), trying %s\n",
				host, maptype[mapno]);
		if (strcmp("files", maptype[mapno]) == 0)
		{
			found = text_getcanonname(host, hbsize, &status);
		}
#if NIS
		else if (strcmp("nis", maptype[mapno]) == 0)
		{
			found = nis_getcanonname(host, hbsize, &status);
# if defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN)
			if (nis_domain == NULL)
				nis_domain = sun_init_domain();
# endif /* defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN) */
		}
#endif /* NIS */
#if NISPLUS
		else if (strcmp("nisplus", maptype[mapno]) == 0)
		{
			found = nisplus_getcanonname(host, hbsize, &status);
# if defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN)
			if (nis_domain == NULL)
				nis_domain = sun_init_domain();
# endif /* defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN) */
		}
#endif /* NISPLUS */
#if NAMED_BIND
		else if (strcmp("dns", maptype[mapno]) == 0)
		{
			found = dns_getcanonname(host, hbsize, trymx, &status,							 pttl);
		}
#endif /* NAMED_BIND */
#if NETINFO
		else if (strcmp("netinfo", maptype[mapno]) == 0)
		{
			found = ni_getcanonname(host, hbsize, &status);
		}
#endif /* NETINFO */
		else
		{
			found = false;
			status = EX_UNAVAILABLE;
		}

		/*
		**  Heuristic: if $m is not set, we are running during system
		**  startup.  In this case, when a name is apparently found
		**  but has no dot, treat is as not found.  This avoids
		**  problems if /etc/hosts has no FQDN but is listed first
		**  in the service switch.
		*/

		if (found &&
		    (macvalue('m', CurEnv) != NULL || strchr(host, '.') != NULL))
			break;

#if defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN)
		if (found)
			should_try_nis_domain = true;
		/* but don't break, as we need to try all methods first */
#endif /* defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN) */

		/* see if we should continue */
		if (status == EX_TEMPFAIL)
		{
			i = MA_TRYAGAIN;
			got_tempfail = true;
		}
		else if (status == EX_NOTFOUND)
			i = MA_NOTFOUND;
		else
			i = MA_UNAVAIL;
		if (bitset(1 << mapno, mapreturn[i]))
			break;
	}

	if (found)
	{
		char *d;

		if (tTd(38, 20))
			sm_dprintf("getcanonname(%s), found\n", host);

		/*
		**  If returned name is still single token, compensate
		**  by tagging on $m.  This is because some sites set
		**  up their DNS or NIS databases wrong.
		*/

		if ((d = strchr(host, '.')) == NULL || d[1] == '\0')
		{
			d = macvalue('m', CurEnv);
			if (d != NULL &&
			    hbsize > (int) (strlen(host) + strlen(d) + 1))
			{
				if (host[strlen(host) - 1] != '.')
					(void) sm_strlcat2(host, ".", d,
							   hbsize);
				else
					(void) sm_strlcat(host, d, hbsize);
			}
			else
			{
#if defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN)
				if (VendorCode == VENDOR_SUN &&
				    should_try_nis_domain)
				{
					goto try_nis_domain;
				}
#endif /* defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN) */
				return false;
			}
		}
		return true;
	}

#if defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN)
	if (VendorCode == VENDOR_SUN && should_try_nis_domain)
	{
  try_nis_domain:
		if (nis_domain != NULL &&
		    strlen(nis_domain) + strlen(host) + 1 < hbsize)
		{
			(void) sm_strlcat2(host, ".", nis_domain, hbsize);
			return true;
		}
	}
#endif /* defined(SUN_EXTENSIONS) && defined(SUN_INIT_DOMAIN) */

	if (tTd(38, 20))
		sm_dprintf("getcanonname(%s), failed, status=%d\n", host,
			status);

	if (got_tempfail)
		SM_SET_H_ERRNO(TRY_AGAIN);
	else
		SM_SET_H_ERRNO(HOST_NOT_FOUND);

	return false;
}
/*
**  EXTRACT_CANONNAME -- extract canonical name from /etc/hosts entry
**
**	Parameters:
**		name -- the name against which to match.
**		dot -- where to reinsert '.' to get FQDN
**		line -- the /etc/hosts line.
**		cbuf -- the location to store the result.
**		cbuflen -- the size of cbuf.
**
**	Returns:
**		true -- if the line matched the desired name.
**		false -- otherwise.
*/

static bool
extract_canonname(name, dot, line, cbuf, cbuflen)
	char *name;
	char *dot;
	char *line;
	char cbuf[];
	int cbuflen;
{
	int i;
	char *p;
	bool found = false;

	cbuf[0] = '\0';
	if (line[0] == '#')
		return false;

	for (i = 1; ; i++)
	{
		char nbuf[MAXNAME + 1];

		p = get_column(line, i, '\0', nbuf, sizeof(nbuf));
		if (p == NULL)
			break;
		if (*p == '\0')
			continue;
		if (cbuf[0] == '\0' ||
		    (strchr(cbuf, '.') == NULL && strchr(p, '.') != NULL))
		{
			(void) sm_strlcpy(cbuf, p, cbuflen);
		}
		if (sm_strcasecmp(name, p) == 0)
			found = true;
		else if (dot != NULL)
		{
			/* try looking for the FQDN as well */
			*dot = '.';
			if (sm_strcasecmp(name, p) == 0)
				found = true;
			*dot = '\0';
		}
	}
	if (found && strchr(cbuf, '.') == NULL)
	{
		/* try to add a domain on the end of the name */
		char *domain = macvalue('m', CurEnv);

		if (domain != NULL &&
		    strlen(domain) + (i = strlen(cbuf)) + 1 < (size_t) cbuflen)
		{
			p = &cbuf[i];
			*p++ = '.';
			(void) sm_strlcpy(p, domain, cbuflen - i - 1);
		}
	}
	return found;
}

/*
**  DNS modules
*/

#if NAMED_BIND
# if DNSMAP

#  include "sm_resolve.h"
#  if NETINET || NETINET6
#   include <arpa/inet.h>
#  endif /* NETINET || NETINET6 */

/*
**  DNS_MAP_OPEN -- stub to check proper value for dns map type
*/

bool
dns_map_open(map, mode)
	MAP *map;
	int mode;
{
	if (tTd(38,2))
		sm_dprintf("dns_map_open(%s, %d)\n", map->map_mname, mode);

	mode &= O_ACCMODE;
	if (mode != O_RDONLY)
	{
		/* issue a pseudo-error message */
		errno = SM_EMAPCANTWRITE;
		return false;
	}
	return true;
}

/*
**  DNS_MAP_PARSEARGS -- parse dns map definition args.
**
**	Parameters:
**		map -- pointer to MAP
**		args -- pointer to the args on the config line.
**
**	Returns:
**		true -- if everything parsed OK.
**		false -- otherwise.
*/

#define map_sizelimit	map_lockfd	/* overload field */

struct dns_map
{
	int dns_m_type;
};

bool
dns_map_parseargs(map,args)
	MAP *map;
	char *args;
{
	register char *p = args;
	struct dns_map *map_p;

	map_p = (struct dns_map *) xalloc(sizeof(*map_p));
	map_p->dns_m_type = -1;
	map->map_mflags |= MF_TRY0NULL|MF_TRY1NULL;

	for (;;)
	{
		while (isascii(*p) && isspace(*p))
			p++;
		if (*p != '-')
			break;
		switch (*++p)
		{
		  case 'N':
			map->map_mflags |= MF_INCLNULL;
			map->map_mflags &= ~MF_TRY0NULL;
			break;

		  case 'O':
			map->map_mflags &= ~MF_TRY1NULL;
			break;

		  case 'o':
			map->map_mflags |= MF_OPTIONAL;
			break;

		  case 'f':
			map->map_mflags |= MF_NOFOLDCASE;
			break;

		  case 'm':
			map->map_mflags |= MF_MATCHONLY;
			break;

		  case 'A':
			map->map_mflags |= MF_APPEND;
			break;

		  case 'q':
			map->map_mflags |= MF_KEEPQUOTES;
			break;

		  case 't':
			map->map_mflags |= MF_NODEFER;
			break;

		  case 'a':
			map->map_app = ++p;
			break;

		  case 'T':
			map->map_tapp = ++p;
			break;

		  case 'd':
			{
				char *h;

				++p;
				h = strchr(p, ' ');
				if (h != NULL)
					*h = '\0';
				map->map_timeout = convtime(p, 's');
				if (h != NULL)
					*h = ' ';
			}
			break;

		  case 'r':
			while (isascii(*++p) && isspace(*p))
				continue;
			map->map_retry = atoi(p);
			break;

		  case 'z':
			if (*++p != '\\')
				map->map_coldelim = *p;
			else
			{
				switch (*++p)
				{
				  case 'n':
					map->map_coldelim = '\n';
					break;

				  case 't':
					map->map_coldelim = '\t';
					break;

				  default:
					map->map_coldelim = '\\';
				}
			}
			break;

		  case 'Z':
			while (isascii(*++p) && isspace(*p))
				continue;
			map->map_sizelimit = atoi(p);
			break;

			/* Start of dns_map specific args */
		  case 'R':		/* search field */
			{
				char *h;

				while (isascii(*++p) && isspace(*p))
					continue;
				h = strchr(p, ' ');
				if (h != NULL)
					*h = '\0';
				map_p->dns_m_type = dns_string_to_type(p);
				if (h != NULL)
					*h = ' ';
				if (map_p->dns_m_type < 0)
					syserr("dns map %s: wrong type %s",
						map->map_mname, p);
			}
			break;

		  case 'B':		/* base domain */
			{
				char *h;

				while (isascii(*++p) && isspace(*p))
					continue;
				h = strchr(p, ' ');
				if (h != NULL)
					*h = '\0';

				/*
				**  slight abuse of map->map_file; it isn't
				**	used otherwise in this map type.
				*/

				map->map_file = newstr(p);
				if (h != NULL)
					*h = ' ';
			}
			break;
		}
		while (*p != '\0' && !(isascii(*p) && isspace(*p)))
			p++;
		if (*p != '\0')
			*p++ = '\0';
	}
	if (map_p->dns_m_type < 0)
		syserr("dns map %s: missing -R type", map->map_mname);
	if (map->map_app != NULL)
		map->map_app = newstr(map->map_app);
	if (map->map_tapp != NULL)
		map->map_tapp = newstr(map->map_tapp);

	/*
	**  Assumption: assert(sizeof(int) <= sizeof(ARBPTR_T));
	**  Even if this assumption is wrong, we use only one byte,
	**  so it doesn't really matter.
	*/

	map->map_db1 = (ARBPTR_T) map_p;
	return true;
}

/*
**  DNS_MAP_LOOKUP -- perform dns map lookup.
**
**	Parameters:
**		map -- pointer to MAP
**		name -- name to lookup
**		av -- arguments to interpolate into buf.
**		statp -- pointer to status (EX_)
**
**	Returns:
**		result of lookup if succeeded.
**		NULL -- otherwise.
*/

char *
dns_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	int resnum = 0;
	char *vp = NULL, *result = NULL;
	size_t vsize;
	struct dns_map *map_p;
	RESOURCE_RECORD_T *rr = NULL;
	DNS_REPLY_T *r = NULL;
#  if NETINET6
	static char buf6[INET6_ADDRSTRLEN];
#  endif /* NETINET6 */

	if (tTd(38, 20))
		sm_dprintf("dns_map_lookup(%s, %s)\n",
			   map->map_mname, name);

	map_p = (struct dns_map *)(map->map_db1);
	if (map->map_file != NULL && *map->map_file != '\0')
	{
		size_t len;
		char *appdomain;

		len = strlen(map->map_file) + strlen(name) + 2;
		appdomain = (char *) sm_malloc(len);
		if (appdomain == NULL)
		{
			*statp = EX_UNAVAILABLE;
			return NULL;
		}
		(void) sm_strlcpyn(appdomain, len, 3, name, ".", map->map_file);
		r = dns_lookup_int(appdomain, C_IN, map_p->dns_m_type,
				   map->map_timeout, map->map_retry);
		sm_free(appdomain);
	}
	else
	{
		r = dns_lookup_int(name, C_IN, map_p->dns_m_type,
				   map->map_timeout, map->map_retry);
	}

	if (r == NULL)
	{
		result = NULL;
		if (h_errno == TRY_AGAIN || transienterror(errno))
			*statp = EX_TEMPFAIL;
		else
			*statp = EX_NOTFOUND;
		goto cleanup;
	}
	*statp = EX_OK;
	for (rr = r->dns_r_head; rr != NULL; rr = rr->rr_next)
	{
		char *type = NULL;
		char *value = NULL;

		switch (rr->rr_type)
		{
		  case T_NS:
			type = "T_NS";
			value = rr->rr_u.rr_txt;
			break;
		  case T_CNAME:
			type = "T_CNAME";
			value = rr->rr_u.rr_txt;
			break;
		  case T_AFSDB:
			type = "T_AFSDB";
			value = rr->rr_u.rr_mx->mx_r_domain;
			break;
		  case T_SRV:
			type = "T_SRV";
			value = rr->rr_u.rr_srv->srv_r_target;
			break;
		  case T_PTR:
			type = "T_PTR";
			value = rr->rr_u.rr_txt;
			break;
		  case T_TXT:
			type = "T_TXT";
			value = rr->rr_u.rr_txt;
			break;
		  case T_MX:
			type = "T_MX";
			value = rr->rr_u.rr_mx->mx_r_domain;
			break;
#  if NETINET
		  case T_A:
			type = "T_A";
			value = inet_ntoa(*(rr->rr_u.rr_a));
			break;
#  endif /* NETINET */
#  if NETINET6
		  case T_AAAA:
			type = "T_AAAA";
			value = anynet_ntop(rr->rr_u.rr_aaaa, buf6,
					    sizeof(buf6));
			break;
#  endif /* NETINET6 */
		}

		(void) strreplnonprt(value, 'X');
		if (map_p->dns_m_type != rr->rr_type)
		{
			if (tTd(38, 40))
				sm_dprintf("\tskipping type %s (%d) value %s\n",
					   type != NULL ? type : "<UNKNOWN>",
					   rr->rr_type,
					   value != NULL ? value : "<NO VALUE>");
			continue;
		}

#  if NETINET6
		if (rr->rr_type == T_AAAA && value == NULL)
		{
			result = NULL;
			*statp = EX_DATAERR;
			if (tTd(38, 40))
				sm_dprintf("\tbad T_AAAA conversion\n");
			goto cleanup;
		}
#  endif /* NETINET6 */
		if (tTd(38, 40))
			sm_dprintf("\tfound type %s (%d) value %s\n",
				   type != NULL ? type : "<UNKNOWN>",
				   rr->rr_type,
				   value != NULL ? value : "<NO VALUE>");
		if (value != NULL &&
		    (map->map_coldelim == '\0' ||
		     map->map_sizelimit == 1 ||
		     bitset(MF_MATCHONLY, map->map_mflags)))
		{
			/* Only care about the first match */
			vp = newstr(value);
			break;
		}
		else if (vp == NULL)
		{
			/* First result */
			vp = newstr(value);
		}
		else
		{
			/* concatenate the results */
			int sz;
			char *new;

			sz = strlen(vp) + strlen(value) + 2;
			new = xalloc(sz);
			(void) sm_snprintf(new, sz, "%s%c%s",
					   vp, map->map_coldelim, value);
			sm_free(vp);
			vp = new;
			if (map->map_sizelimit > 0 &&
			    ++resnum >= map->map_sizelimit)
				break;
		}
	}
	if (vp == NULL)
	{
		result = NULL;
		*statp = EX_NOTFOUND;
		if (tTd(38, 40))
			sm_dprintf("\tno match found\n");
		goto cleanup;
	}

	/* Cleanly truncate for rulesets */
	truncate_at_delim(vp, PSBUFSIZE / 2, map->map_coldelim);

	vsize = strlen(vp);

	if (LogLevel > 9)
		sm_syslog(LOG_INFO, CurEnv->e_id, "dns %.100s => %s",
			  name, vp);
	if (bitset(MF_MATCHONLY, map->map_mflags))
		result = map_rewrite(map, name, strlen(name), NULL);
	else
		result = map_rewrite(map, vp, vsize, av);

  cleanup:
	if (vp != NULL)
		sm_free(vp);
	if (r != NULL)
		dns_free_data(r);
	return result;
}
# endif /* DNSMAP */
#endif /* NAMED_BIND */

/*
**  NDBM modules
*/

#if NDBM

/*
**  NDBM_MAP_OPEN -- DBM-style map open
*/

bool
ndbm_map_open(map, mode)
	MAP *map;
	int mode;
{
	register DBM *dbm;
	int save_errno;
	int dfd;
	int pfd;
	long sff;
	int ret;
	int smode = S_IREAD;
	char dirfile[MAXPATHLEN];
	char pagfile[MAXPATHLEN];
	struct stat st;
	struct stat std, stp;

	if (tTd(38, 2))
		sm_dprintf("ndbm_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);
	map->map_lockfd = -1;
	mode &= O_ACCMODE;

	/* do initial file and directory checks */
	if (sm_strlcpyn(dirfile, sizeof(dirfile), 2,
			map->map_file, ".dir") >= sizeof(dirfile) ||
	    sm_strlcpyn(pagfile, sizeof(pagfile), 2,
			map->map_file, ".pag") >= sizeof(pagfile))
	{
		errno = 0;
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("dbm map \"%s\": map file %s name too long",
				map->map_mname, map->map_file);
		return false;
	}
	sff = SFF_ROOTOK|SFF_REGONLY;
	if (mode == O_RDWR)
	{
		sff |= SFF_CREAT;
		if (!bitnset(DBS_WRITEMAPTOSYMLINK, DontBlameSendmail))
			sff |= SFF_NOSLINK;
		if (!bitnset(DBS_WRITEMAPTOHARDLINK, DontBlameSendmail))
			sff |= SFF_NOHLINK;
		smode = S_IWRITE;
	}
	else
	{
		if (!bitnset(DBS_LINKEDMAPINWRITABLEDIR, DontBlameSendmail))
			sff |= SFF_NOWLINK;
	}
	if (!bitnset(DBS_MAPINUNSAFEDIRPATH, DontBlameSendmail))
		sff |= SFF_SAFEDIRPATH;
	ret = safefile(dirfile, RunAsUid, RunAsGid, RunAsUserName,
		       sff, smode, &std);
	if (ret == 0)
		ret = safefile(pagfile, RunAsUid, RunAsGid, RunAsUserName,
			       sff, smode, &stp);

	if (ret != 0)
	{
		char *prob = "unsafe";

		/* cannot open this map */
		if (ret == ENOENT)
			prob = "missing";
		if (tTd(38, 2))
			sm_dprintf("\t%s map file: %d\n", prob, ret);
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("dbm map \"%s\": %s map file %s",
				map->map_mname, prob, map->map_file);
		return false;
	}
	if (std.st_mode == ST_MODE_NOFILE)
		mode |= O_CREAT|O_EXCL;

# if LOCK_ON_OPEN
	if (mode == O_RDONLY)
		mode |= O_SHLOCK;
	else
		mode |= O_TRUNC|O_EXLOCK;
# else /* LOCK_ON_OPEN */
	if ((mode & O_ACCMODE) == O_RDWR)
	{
#  if NOFTRUNCATE
		/*
		**  Warning: race condition.  Try to lock the file as
		**  quickly as possible after opening it.
		**	This may also have security problems on some systems,
		**	but there isn't anything we can do about it.
		*/

		mode |= O_TRUNC;
#  else /* NOFTRUNCATE */
		/*
		**  This ugly code opens the map without truncating it,
		**  locks the file, then truncates it.  Necessary to
		**  avoid race conditions.
		*/

		int dirfd;
		int pagfd;
		long sff = SFF_CREAT|SFF_OPENASROOT;

		if (!bitnset(DBS_WRITEMAPTOSYMLINK, DontBlameSendmail))
			sff |= SFF_NOSLINK;
		if (!bitnset(DBS_WRITEMAPTOHARDLINK, DontBlameSendmail))
			sff |= SFF_NOHLINK;

		dirfd = safeopen(dirfile, mode, DBMMODE, sff);
		pagfd = safeopen(pagfile, mode, DBMMODE, sff);

		if (dirfd < 0 || pagfd < 0)
		{
			save_errno = errno;
			if (dirfd >= 0)
				(void) close(dirfd);
			if (pagfd >= 0)
				(void) close(pagfd);
			errno = save_errno;
			syserr("ndbm_map_open: cannot create database %s",
				map->map_file);
			return false;
		}
		if (ftruncate(dirfd, (off_t) 0) < 0 ||
		    ftruncate(pagfd, (off_t) 0) < 0)
		{
			save_errno = errno;
			(void) close(dirfd);
			(void) close(pagfd);
			errno = save_errno;
			syserr("ndbm_map_open: cannot truncate %s.{dir,pag}",
				map->map_file);
			return false;
		}

		/* if new file, get "before" bits for later filechanged check */
		if (std.st_mode == ST_MODE_NOFILE &&
		    (fstat(dirfd, &std) < 0 || fstat(pagfd, &stp) < 0))
		{
			save_errno = errno;
			(void) close(dirfd);
			(void) close(pagfd);
			errno = save_errno;
			syserr("ndbm_map_open(%s.{dir,pag}): cannot fstat pre-opened file",
				map->map_file);
			return false;
		}

		/* have to save the lock for the duration (bletch) */
		map->map_lockfd = dirfd;
		(void) close(pagfd);

		/* twiddle bits for dbm_open */
		mode &= ~(O_CREAT|O_EXCL);
#  endif /* NOFTRUNCATE */
	}
# endif /* LOCK_ON_OPEN */

	/* open the database */
	dbm = dbm_open(map->map_file, mode, DBMMODE);
	if (dbm == NULL)
	{
		save_errno = errno;
		if (bitset(MF_ALIAS, map->map_mflags) &&
		    aliaswait(map, ".pag", false))
			return true;
# if !LOCK_ON_OPEN && !NOFTRUNCATE
		if (map->map_lockfd >= 0)
			(void) close(map->map_lockfd);
# endif /* !LOCK_ON_OPEN && !NOFTRUNCATE */
		errno = save_errno;
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("Cannot open DBM database %s", map->map_file);
		return false;
	}
	dfd = dbm_dirfno(dbm);
	pfd = dbm_pagfno(dbm);
	if (dfd == pfd)
	{
		/* heuristic: if files are linked, this is actually gdbm */
		dbm_close(dbm);
# if !LOCK_ON_OPEN && !NOFTRUNCATE
		if (map->map_lockfd >= 0)
			(void) close(map->map_lockfd);
# endif /* !LOCK_ON_OPEN && !NOFTRUNCATE */
		errno = 0;
		syserr("dbm map \"%s\": cannot support GDBM",
			map->map_mname);
		return false;
	}

	if (filechanged(dirfile, dfd, &std) ||
	    filechanged(pagfile, pfd, &stp))
	{
		save_errno = errno;
		dbm_close(dbm);
# if !LOCK_ON_OPEN && !NOFTRUNCATE
		if (map->map_lockfd >= 0)
			(void) close(map->map_lockfd);
# endif /* !LOCK_ON_OPEN && !NOFTRUNCATE */
		errno = save_errno;
		syserr("ndbm_map_open(%s): file changed after open",
			map->map_file);
		return false;
	}

	map->map_db1 = (ARBPTR_T) dbm;

	/*
	**  Need to set map_mtime before the call to aliaswait()
	**  as aliaswait() will call map_lookup() which requires
	**  map_mtime to be set
	*/

	if (fstat(pfd, &st) >= 0)
		map->map_mtime = st.st_mtime;

	if (mode == O_RDONLY)
	{
# if LOCK_ON_OPEN
		if (dfd >= 0)
			(void) lockfile(dfd, map->map_file, ".dir", LOCK_UN);
		if (pfd >= 0)
			(void) lockfile(pfd, map->map_file, ".pag", LOCK_UN);
# endif /* LOCK_ON_OPEN */
		if (bitset(MF_ALIAS, map->map_mflags) &&
		    !aliaswait(map, ".pag", true))
			return false;
	}
	else
	{
		map->map_mflags |= MF_LOCKED;
		if (geteuid() == 0 && TrustedUid != 0)
		{
#  if HASFCHOWN
			if (fchown(dfd, TrustedUid, -1) < 0 ||
			    fchown(pfd, TrustedUid, -1) < 0)
			{
				int err = errno;

				sm_syslog(LOG_ALERT, NOQID,
					  "ownership change on %s failed: %s",
					  map->map_file, sm_errstring(err));
				message("050 ownership change on %s failed: %s",
					map->map_file, sm_errstring(err));
			}
#  else /* HASFCHOWN */
			sm_syslog(LOG_ALERT, NOQID,
				  "no fchown(): cannot change ownership on %s",
				  map->map_file);
			message("050 no fchown(): cannot change ownership on %s",
				map->map_file);
#  endif /* HASFCHOWN */
		}
	}
	return true;
}


/*
**  NDBM_MAP_LOOKUP -- look up a datum in a DBM-type map
*/

char *
ndbm_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	datum key, val;
	int dfd, pfd;
	char keybuf[MAXNAME + 1];
	struct stat stbuf;

	if (tTd(38, 20))
		sm_dprintf("ndbm_map_lookup(%s, %s)\n",
			map->map_mname, name);

	key.dptr = name;
	key.dsize = strlen(name);
	if (!bitset(MF_NOFOLDCASE, map->map_mflags))
	{
		if (key.dsize > sizeof(keybuf) - 1)
			key.dsize = sizeof(keybuf) - 1;
		memmove(keybuf, key.dptr, key.dsize);
		keybuf[key.dsize] = '\0';
		makelower(keybuf);
		key.dptr = keybuf;
	}
lockdbm:
	dfd = dbm_dirfno((DBM *) map->map_db1);
	if (dfd >= 0 && !bitset(MF_LOCKED, map->map_mflags))
		(void) lockfile(dfd, map->map_file, ".dir", LOCK_SH);
	pfd = dbm_pagfno((DBM *) map->map_db1);
	if (pfd < 0 || fstat(pfd, &stbuf) < 0 ||
	    stbuf.st_mtime > map->map_mtime)
	{
		/* Reopen the database to sync the cache */
		int omode = bitset(map->map_mflags, MF_WRITABLE) ? O_RDWR
								 : O_RDONLY;

		if (dfd >= 0 && !bitset(MF_LOCKED, map->map_mflags))
			(void) lockfile(dfd, map->map_file, ".dir", LOCK_UN);
		map->map_mflags |= MF_CLOSING;
		map->map_class->map_close(map);
		map->map_mflags &= ~(MF_OPEN|MF_WRITABLE|MF_CLOSING);
		if (map->map_class->map_open(map, omode))
		{
			map->map_mflags |= MF_OPEN;
			map->map_pid = CurrentPid;
			if ((omode & O_ACCMODE) == O_RDWR)
				map->map_mflags |= MF_WRITABLE;
			goto lockdbm;
		}
		else
		{
			if (!bitset(MF_OPTIONAL, map->map_mflags))
			{
				extern MAPCLASS BogusMapClass;

				*statp = EX_TEMPFAIL;
				map->map_orgclass = map->map_class;
				map->map_class = &BogusMapClass;
				map->map_mflags |= MF_OPEN;
				map->map_pid = CurrentPid;
				syserr("Cannot reopen NDBM database %s",
					map->map_file);
			}
			return NULL;
		}
	}
	val.dptr = NULL;
	if (bitset(MF_TRY0NULL, map->map_mflags))
	{
		val = dbm_fetch((DBM *) map->map_db1, key);
		if (val.dptr != NULL)
			map->map_mflags &= ~MF_TRY1NULL;
	}
	if (val.dptr == NULL && bitset(MF_TRY1NULL, map->map_mflags))
	{
		key.dsize++;
		val = dbm_fetch((DBM *) map->map_db1, key);
		if (val.dptr != NULL)
			map->map_mflags &= ~MF_TRY0NULL;
	}
	if (dfd >= 0 && !bitset(MF_LOCKED, map->map_mflags))
		(void) lockfile(dfd, map->map_file, ".dir", LOCK_UN);
	if (val.dptr == NULL)
		return NULL;
	if (bitset(MF_MATCHONLY, map->map_mflags))
		return map_rewrite(map, name, strlen(name), NULL);
	else
		return map_rewrite(map, val.dptr, val.dsize, av);
}


/*
**  NDBM_MAP_STORE -- store a datum in the database
*/

void
ndbm_map_store(map, lhs, rhs)
	register MAP *map;
	char *lhs;
	char *rhs;
{
	datum key;
	datum data;
	int status;
	char keybuf[MAXNAME + 1];

	if (tTd(38, 12))
		sm_dprintf("ndbm_map_store(%s, %s, %s)\n",
			map->map_mname, lhs, rhs);

	key.dsize = strlen(lhs);
	key.dptr = lhs;
	if (!bitset(MF_NOFOLDCASE, map->map_mflags))
	{
		if (key.dsize > sizeof(keybuf) - 1)
			key.dsize = sizeof(keybuf) - 1;
		memmove(keybuf, key.dptr, key.dsize);
		keybuf[key.dsize] = '\0';
		makelower(keybuf);
		key.dptr = keybuf;
	}

	data.dsize = strlen(rhs);
	data.dptr = rhs;

	if (bitset(MF_INCLNULL, map->map_mflags))
	{
		key.dsize++;
		data.dsize++;
	}

	status = dbm_store((DBM *) map->map_db1, key, data, DBM_INSERT);
	if (status > 0)
	{
		if (!bitset(MF_APPEND, map->map_mflags))
			message("050 Warning: duplicate alias name %s", lhs);
		else
		{
			static char *buf = NULL;
			static int bufsiz = 0;
			auto int xstat;
			datum old;

			old.dptr = ndbm_map_lookup(map, key.dptr,
						   (char **) NULL, &xstat);
			if (old.dptr != NULL && *(char *) old.dptr != '\0')
			{
				old.dsize = strlen(old.dptr);
				if (data.dsize + old.dsize + 2 > bufsiz)
				{
					if (buf != NULL)
						(void) sm_free(buf);
					bufsiz = data.dsize + old.dsize + 2;
					buf = sm_pmalloc_x(bufsiz);
				}
				(void) sm_strlcpyn(buf, bufsiz, 3,
					data.dptr, ",", old.dptr);
				data.dsize = data.dsize + old.dsize + 1;
				data.dptr = buf;
				if (tTd(38, 9))
					sm_dprintf("ndbm_map_store append=%s\n",
						(char *)data.dptr);
			}
		}
		status = dbm_store((DBM *) map->map_db1,
				   key, data, DBM_REPLACE);
	}
	if (status != 0)
		syserr("readaliases: dbm put (%s): %d", lhs, status);
}


/*
**  NDBM_MAP_CLOSE -- close the database
*/

void
ndbm_map_close(map)
	register MAP  *map;
{
	if (tTd(38, 9))
		sm_dprintf("ndbm_map_close(%s, %s, %lx)\n",
			map->map_mname, map->map_file, map->map_mflags);

	if (bitset(MF_WRITABLE, map->map_mflags))
	{
# ifdef NDBM_YP_COMPAT
		bool inclnull;
		char buf[MAXHOSTNAMELEN];

		inclnull = bitset(MF_INCLNULL, map->map_mflags);
		map->map_mflags &= ~MF_INCLNULL;

		if (strstr(map->map_file, "/yp/") != NULL)
		{
			long save_mflags = map->map_mflags;

			map->map_mflags |= MF_NOFOLDCASE;

			(void) sm_snprintf(buf, sizeof(buf), "%010ld", curtime());
			ndbm_map_store(map, "YP_LAST_MODIFIED", buf);

			(void) gethostname(buf, sizeof(buf));
			ndbm_map_store(map, "YP_MASTER_NAME", buf);

			map->map_mflags = save_mflags;
		}

		if (inclnull)
			map->map_mflags |= MF_INCLNULL;
# endif /* NDBM_YP_COMPAT */

		/* write out the distinguished alias */
		ndbm_map_store(map, "@", "@");
	}
	dbm_close((DBM *) map->map_db1);

	/* release lock (if needed) */
# if !LOCK_ON_OPEN
	if (map->map_lockfd >= 0)
		(void) close(map->map_lockfd);
# endif /* !LOCK_ON_OPEN */
}

#endif /* NDBM */
/*
**  NEWDB (Hash and BTree) Modules
*/

#if NEWDB

/*
**  BT_MAP_OPEN, HASH_MAP_OPEN -- database open primitives.
**
**	These do rather bizarre locking.  If you can lock on open,
**	do that to avoid the condition of opening a database that
**	is being rebuilt.  If you don't, we'll try to fake it, but
**	there will be a race condition.  If opening for read-only,
**	we immediately release the lock to avoid freezing things up.
**	We really ought to hold the lock, but guarantee that we won't
**	be pokey about it.  That's hard to do.
*/

/* these should be K line arguments */
# if DB_VERSION_MAJOR < 2
#  define db_cachesize	cachesize
#  define h_nelem	nelem
#  ifndef DB_CACHE_SIZE
#   define DB_CACHE_SIZE	(1024 * 1024)	/* database memory cache size */
#  endif /* ! DB_CACHE_SIZE */
#  ifndef DB_HASH_NELEM
#   define DB_HASH_NELEM	4096		/* (starting) size of hash table */
#  endif /* ! DB_HASH_NELEM */
# endif /* DB_VERSION_MAJOR < 2 */

bool
bt_map_open(map, mode)
	MAP *map;
	int mode;
{
# if DB_VERSION_MAJOR < 2
	BTREEINFO btinfo;
# endif /* DB_VERSION_MAJOR < 2 */
# if DB_VERSION_MAJOR == 2
	DB_INFO btinfo;
# endif /* DB_VERSION_MAJOR == 2 */
# if DB_VERSION_MAJOR > 2
	void *btinfo = NULL;
# endif /* DB_VERSION_MAJOR > 2 */

	if (tTd(38, 2))
		sm_dprintf("bt_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);

# if DB_VERSION_MAJOR < 3
	memset(&btinfo, '\0', sizeof(btinfo));
#  ifdef DB_CACHE_SIZE
	btinfo.db_cachesize = DB_CACHE_SIZE;
#  endif /* DB_CACHE_SIZE */
# endif /* DB_VERSION_MAJOR < 3 */

	return db_map_open(map, mode, "btree", DB_BTREE, &btinfo);
}

bool
hash_map_open(map, mode)
	MAP *map;
	int mode;
{
# if DB_VERSION_MAJOR < 2
	HASHINFO hinfo;
# endif /* DB_VERSION_MAJOR < 2 */
# if DB_VERSION_MAJOR == 2
	DB_INFO hinfo;
# endif /* DB_VERSION_MAJOR == 2 */
# if DB_VERSION_MAJOR > 2
	void *hinfo = NULL;
# endif /* DB_VERSION_MAJOR > 2 */

	if (tTd(38, 2))
		sm_dprintf("hash_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);

# if DB_VERSION_MAJOR < 3
	memset(&hinfo, '\0', sizeof(hinfo));
#  ifdef DB_HASH_NELEM
	hinfo.h_nelem = DB_HASH_NELEM;
#  endif /* DB_HASH_NELEM */
#  ifdef DB_CACHE_SIZE
	hinfo.db_cachesize = DB_CACHE_SIZE;
#  endif /* DB_CACHE_SIZE */
# endif /* DB_VERSION_MAJOR < 3 */

	return db_map_open(map, mode, "hash", DB_HASH, &hinfo);
}

static bool
db_map_open(map, mode, mapclassname, dbtype, openinfo)
	MAP *map;
	int mode;
	char *mapclassname;
	DBTYPE dbtype;
# if DB_VERSION_MAJOR < 2
	const void *openinfo;
# endif /* DB_VERSION_MAJOR < 2 */
# if DB_VERSION_MAJOR == 2
	DB_INFO *openinfo;
# endif /* DB_VERSION_MAJOR == 2 */
# if DB_VERSION_MAJOR > 2
	void **openinfo;
# endif /* DB_VERSION_MAJOR > 2 */
{
	DB *db = NULL;
	int i;
	int omode;
	int smode = S_IREAD;
	int fd;
	long sff;
	int save_errno;
	struct stat st;
	char buf[MAXPATHLEN];

	/* do initial file and directory checks */
	if (sm_strlcpy(buf, map->map_file, sizeof(buf)) >= sizeof(buf))
	{
		errno = 0;
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("map \"%s\": map file %s name too long",
				map->map_mname, map->map_file);
		return false;
	}
	i = strlen(buf);
	if (i < 3 || strcmp(&buf[i - 3], ".db") != 0)
	{
		if (sm_strlcat(buf, ".db", sizeof(buf)) >= sizeof(buf))
		{
			errno = 0;
			if (!bitset(MF_OPTIONAL, map->map_mflags))
				syserr("map \"%s\": map file %s name too long",
					map->map_mname, map->map_file);
			return false;
		}
	}

	mode &= O_ACCMODE;
	omode = mode;

	sff = SFF_ROOTOK|SFF_REGONLY;
	if (mode == O_RDWR)
	{
		sff |= SFF_CREAT;
		if (!bitnset(DBS_WRITEMAPTOSYMLINK, DontBlameSendmail))
			sff |= SFF_NOSLINK;
		if (!bitnset(DBS_WRITEMAPTOHARDLINK, DontBlameSendmail))
			sff |= SFF_NOHLINK;
		smode = S_IWRITE;
	}
	else
	{
		if (!bitnset(DBS_LINKEDMAPINWRITABLEDIR, DontBlameSendmail))
			sff |= SFF_NOWLINK;
	}
	if (!bitnset(DBS_MAPINUNSAFEDIRPATH, DontBlameSendmail))
		sff |= SFF_SAFEDIRPATH;
	i = safefile(buf, RunAsUid, RunAsGid, RunAsUserName, sff, smode, &st);

	if (i != 0)
	{
		char *prob = "unsafe";

		/* cannot open this map */
		if (i == ENOENT)
			prob = "missing";
		if (tTd(38, 2))
			sm_dprintf("\t%s map file: %s\n", prob, sm_errstring(i));
		errno = i;
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("%s map \"%s\": %s map file %s",
				mapclassname, map->map_mname, prob, buf);
		return false;
	}
	if (st.st_mode == ST_MODE_NOFILE)
		omode |= O_CREAT|O_EXCL;

	map->map_lockfd = -1;

# if LOCK_ON_OPEN
	if (mode == O_RDWR)
		omode |= O_TRUNC|O_EXLOCK;
	else
		omode |= O_SHLOCK;
# else /* LOCK_ON_OPEN */
	/*
	**  Pre-lock the file to avoid race conditions.  In particular,
	**  since dbopen returns NULL if the file is zero length, we
	**  must have a locked instance around the dbopen.
	*/

	fd = open(buf, omode, DBMMODE);
	if (fd < 0)
	{
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("db_map_open: cannot pre-open database %s", buf);
		return false;
	}

	/* make sure no baddies slipped in just before the open... */
	if (filechanged(buf, fd, &st))
	{
		save_errno = errno;
		(void) close(fd);
		errno = save_errno;
		syserr("db_map_open(%s): file changed after pre-open", buf);
		return false;
	}

	/* if new file, get the "before" bits for later filechanged check */
	if (st.st_mode == ST_MODE_NOFILE && fstat(fd, &st) < 0)
	{
		save_errno = errno;
		(void) close(fd);
		errno = save_errno;
		syserr("db_map_open(%s): cannot fstat pre-opened file",
			buf);
		return false;
	}

	/* actually lock the pre-opened file */
	if (!lockfile(fd, buf, NULL, mode == O_RDONLY ? LOCK_SH : LOCK_EX))
		syserr("db_map_open: cannot lock %s", buf);

	/* set up mode bits for dbopen */
	if (mode == O_RDWR)
		omode |= O_TRUNC;
	omode &= ~(O_EXCL|O_CREAT);
# endif /* LOCK_ON_OPEN */

# if DB_VERSION_MAJOR < 2
	db = dbopen(buf, omode, DBMMODE, dbtype, openinfo);
# else /* DB_VERSION_MAJOR < 2 */
	{
		int flags = 0;
#  if DB_VERSION_MAJOR > 2
		int ret;
#  endif /* DB_VERSION_MAJOR > 2 */

		if (mode == O_RDONLY)
			flags |= DB_RDONLY;
		if (bitset(O_CREAT, omode))
			flags |= DB_CREATE;
		if (bitset(O_TRUNC, omode))
			flags |= DB_TRUNCATE;
		SM_DB_FLAG_ADD(flags);

#  if DB_VERSION_MAJOR > 2
		ret = db_create(&db, NULL, 0);
#  ifdef DB_CACHE_SIZE
		if (ret == 0 && db != NULL)
		{
			ret = db->set_cachesize(db, 0, DB_CACHE_SIZE, 0);
			if (ret != 0)
			{
				(void) db->close(db, 0);
				db = NULL;
			}
		}
#  endif /* DB_CACHE_SIZE */
#  ifdef DB_HASH_NELEM
		if (dbtype == DB_HASH && ret == 0 && db != NULL)
		{
			ret = db->set_h_nelem(db, DB_HASH_NELEM);
			if (ret != 0)
			{
				(void) db->close(db, 0);
				db = NULL;
			}
		}
#  endif /* DB_HASH_NELEM */
		if (ret == 0 && db != NULL)
		{
			ret = db->open(db,
					DBTXN	/* transaction for DB 4.1 */
					buf, NULL, dbtype, flags, DBMMODE);
			if (ret != 0)
			{
#ifdef DB_OLD_VERSION
				if (ret == DB_OLD_VERSION)
					ret = EINVAL;
#endif /* DB_OLD_VERSION */
				(void) db->close(db, 0);
				db = NULL;
			}
		}
		errno = ret;
#  else /* DB_VERSION_MAJOR > 2 */
		errno = db_open(buf, dbtype, flags, DBMMODE,
				NULL, openinfo, &db);
#  endif /* DB_VERSION_MAJOR > 2 */
	}
# endif /* DB_VERSION_MAJOR < 2 */
	save_errno = errno;

# if !LOCK_ON_OPEN
	if (mode == O_RDWR)
		map->map_lockfd = fd;
	else
		(void) close(fd);
# endif /* !LOCK_ON_OPEN */

	if (db == NULL)
	{
		if (mode == O_RDONLY && bitset(MF_ALIAS, map->map_mflags) &&
		    aliaswait(map, ".db", false))
			return true;
# if !LOCK_ON_OPEN
		if (map->map_lockfd >= 0)
			(void) close(map->map_lockfd);
# endif /* !LOCK_ON_OPEN */
		errno = save_errno;
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("Cannot open %s database %s",
				mapclassname, buf);
		return false;
	}

# if DB_VERSION_MAJOR < 2
	fd = db->fd(db);
# else /* DB_VERSION_MAJOR < 2 */
	fd = -1;
	errno = db->fd(db, &fd);
# endif /* DB_VERSION_MAJOR < 2 */
	if (filechanged(buf, fd, &st))
	{
		save_errno = errno;
# if DB_VERSION_MAJOR < 2
		(void) db->close(db);
# else /* DB_VERSION_MAJOR < 2 */
		errno = db->close(db, 0);
# endif /* DB_VERSION_MAJOR < 2 */
# if !LOCK_ON_OPEN
		if (map->map_lockfd >= 0)
			(void) close(map->map_lockfd);
# endif /* !LOCK_ON_OPEN */
		errno = save_errno;
		syserr("db_map_open(%s): file changed after open", buf);
		return false;
	}

	if (mode == O_RDWR)
		map->map_mflags |= MF_LOCKED;
# if LOCK_ON_OPEN
	if (fd >= 0 && mode == O_RDONLY)
	{
		(void) lockfile(fd, buf, NULL, LOCK_UN);
	}
# endif /* LOCK_ON_OPEN */

	/* try to make sure that at least the database header is on disk */
	if (mode == O_RDWR)
	{
		(void) db->sync(db, 0);
		if (geteuid() == 0 && TrustedUid != 0)
		{
#  if HASFCHOWN
			if (fchown(fd, TrustedUid, -1) < 0)
			{
				int err = errno;

				sm_syslog(LOG_ALERT, NOQID,
					  "ownership change on %s failed: %s",
					  buf, sm_errstring(err));
				message("050 ownership change on %s failed: %s",
					buf, sm_errstring(err));
			}
#  else /* HASFCHOWN */
			sm_syslog(LOG_ALERT, NOQID,
				  "no fchown(): cannot change ownership on %s",
				  map->map_file);
			message("050 no fchown(): cannot change ownership on %s",
				map->map_file);
#  endif /* HASFCHOWN */
		}
	}

	map->map_db2 = (ARBPTR_T) db;

	/*
	**  Need to set map_mtime before the call to aliaswait()
	**  as aliaswait() will call map_lookup() which requires
	**  map_mtime to be set
	*/

	if (fd >= 0 && fstat(fd, &st) >= 0)
		map->map_mtime = st.st_mtime;

	if (mode == O_RDONLY && bitset(MF_ALIAS, map->map_mflags) &&
	    !aliaswait(map, ".db", true))
		return false;
	return true;
}


/*
**  DB_MAP_LOOKUP -- look up a datum in a BTREE- or HASH-type map
*/

char *
db_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	DBT key, val;
	register DB *db = (DB *) map->map_db2;
	int i;
	int st;
	int save_errno;
	int fd;
	struct stat stbuf;
	char keybuf[MAXNAME + 1];
	char buf[MAXPATHLEN];

	memset(&key, '\0', sizeof(key));
	memset(&val, '\0', sizeof(val));

	if (tTd(38, 20))
		sm_dprintf("db_map_lookup(%s, %s)\n",
			map->map_mname, name);

	if (sm_strlcpy(buf, map->map_file, sizeof(buf)) >= sizeof(buf))
	{
		errno = 0;
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("map \"%s\": map file %s name too long",
				map->map_mname, map->map_file);
		return NULL;
	}
	i = strlen(buf);
	if (i > 3 && strcmp(&buf[i - 3], ".db") == 0)
		buf[i - 3] = '\0';

	key.size = strlen(name);
	if (key.size > sizeof(keybuf) - 1)
		key.size = sizeof(keybuf) - 1;
	key.data = keybuf;
	memmove(keybuf, name, key.size);
	keybuf[key.size] = '\0';
	if (!bitset(MF_NOFOLDCASE, map->map_mflags))
		makelower(keybuf);
  lockdb:
# if DB_VERSION_MAJOR < 2
	fd = db->fd(db);
# else /* DB_VERSION_MAJOR < 2 */
	fd = -1;
	errno = db->fd(db, &fd);
# endif /* DB_VERSION_MAJOR < 2 */
	if (fd >= 0 && !bitset(MF_LOCKED, map->map_mflags))
		(void) lockfile(fd, buf, ".db", LOCK_SH);
	if (fd < 0 || fstat(fd, &stbuf) < 0 || stbuf.st_mtime > map->map_mtime)
	{
		/* Reopen the database to sync the cache */
		int omode = bitset(map->map_mflags, MF_WRITABLE) ? O_RDWR
								 : O_RDONLY;

		if (fd >= 0 && !bitset(MF_LOCKED, map->map_mflags))
			(void) lockfile(fd, buf, ".db", LOCK_UN);
		map->map_mflags |= MF_CLOSING;
		map->map_class->map_close(map);
		map->map_mflags &= ~(MF_OPEN|MF_WRITABLE|MF_CLOSING);
		if (map->map_class->map_open(map, omode))
		{
			map->map_mflags |= MF_OPEN;
			map->map_pid = CurrentPid;
			if ((omode & O_ACCMODE) == O_RDWR)
				map->map_mflags |= MF_WRITABLE;
			db = (DB *) map->map_db2;
			goto lockdb;
		}
		else
		{
			if (!bitset(MF_OPTIONAL, map->map_mflags))
			{
				extern MAPCLASS BogusMapClass;

				*statp = EX_TEMPFAIL;
				map->map_orgclass = map->map_class;
				map->map_class = &BogusMapClass;
				map->map_mflags |= MF_OPEN;
				map->map_pid = CurrentPid;
				syserr("Cannot reopen DB database %s",
					map->map_file);
			}
			return NULL;
		}
	}

	st = 1;
	if (bitset(MF_TRY0NULL, map->map_mflags))
	{
# if DB_VERSION_MAJOR < 2
		st = db->get(db, &key, &val, 0);
# else /* DB_VERSION_MAJOR < 2 */
		errno = db->get(db, NULL, &key, &val, 0);
		switch (errno)
		{
		  case DB_NOTFOUND:
		  case DB_KEYEMPTY:
			st = 1;
			break;

		  case 0:
			st = 0;
			break;

		  default:
			st = -1;
			break;
		}
# endif /* DB_VERSION_MAJOR < 2 */
		if (st == 0)
			map->map_mflags &= ~MF_TRY1NULL;
	}
	if (st != 0 && bitset(MF_TRY1NULL, map->map_mflags))
	{
		key.size++;
# if DB_VERSION_MAJOR < 2
		st = db->get(db, &key, &val, 0);
# else /* DB_VERSION_MAJOR < 2 */
		errno = db->get(db, NULL, &key, &val, 0);
		switch (errno)
		{
		  case DB_NOTFOUND:
		  case DB_KEYEMPTY:
			st = 1;
			break;

		  case 0:
			st = 0;
			break;

		  default:
			st = -1;
			break;
		}
# endif /* DB_VERSION_MAJOR < 2 */
		if (st == 0)
			map->map_mflags &= ~MF_TRY0NULL;
	}
	save_errno = errno;
	if (fd >= 0 && !bitset(MF_LOCKED, map->map_mflags))
		(void) lockfile(fd, buf, ".db", LOCK_UN);
	if (st != 0)
	{
		errno = save_errno;
		if (st < 0)
			syserr("db_map_lookup: get (%s)", name);
		return NULL;
	}
	if (bitset(MF_MATCHONLY, map->map_mflags))
		return map_rewrite(map, name, strlen(name), NULL);
	else
		return map_rewrite(map, val.data, val.size, av);
}


/*
**  DB_MAP_STORE -- store a datum in the NEWDB database
*/

void
db_map_store(map, lhs, rhs)
	register MAP *map;
	char *lhs;
	char *rhs;
{
	int status;
	DBT key;
	DBT data;
	register DB *db = map->map_db2;
	char keybuf[MAXNAME + 1];

	memset(&key, '\0', sizeof(key));
	memset(&data, '\0', sizeof(data));

	if (tTd(38, 12))
		sm_dprintf("db_map_store(%s, %s, %s)\n",
			map->map_mname, lhs, rhs);

	key.size = strlen(lhs);
	key.data = lhs;
	if (!bitset(MF_NOFOLDCASE, map->map_mflags))
	{
		if (key.size > sizeof(keybuf) - 1)
			key.size = sizeof(keybuf) - 1;
		memmove(keybuf, key.data, key.size);
		keybuf[key.size] = '\0';
		makelower(keybuf);
		key.data = keybuf;
	}

	data.size = strlen(rhs);
	data.data = rhs;

	if (bitset(MF_INCLNULL, map->map_mflags))
	{
		key.size++;
		data.size++;
	}

# if DB_VERSION_MAJOR < 2
	status = db->put(db, &key, &data, R_NOOVERWRITE);
# else /* DB_VERSION_MAJOR < 2 */
	errno = db->put(db, NULL, &key, &data, DB_NOOVERWRITE);
	switch (errno)
	{
	  case DB_KEYEXIST:
		status = 1;
		break;

	  case 0:
		status = 0;
		break;

	  default:
		status = -1;
		break;
	}
# endif /* DB_VERSION_MAJOR < 2 */
	if (status > 0)
	{
		if (!bitset(MF_APPEND, map->map_mflags))
			message("050 Warning: duplicate alias name %s", lhs);
		else
		{
			static char *buf = NULL;
			static int bufsiz = 0;
			DBT old;

			memset(&old, '\0', sizeof(old));

			old.data = db_map_lookup(map, key.data,
						 (char **) NULL, &status);
			if (old.data != NULL)
			{
				old.size = strlen(old.data);
				if (data.size + old.size + 2 > (size_t) bufsiz)
				{
					if (buf != NULL)
						sm_free(buf);
					bufsiz = data.size + old.size + 2;
					buf = sm_pmalloc_x(bufsiz);
				}
				(void) sm_strlcpyn(buf, bufsiz, 3,
					(char *) data.data, ",",
					(char *) old.data);
				data.size = data.size + old.size + 1;
				data.data = buf;
				if (tTd(38, 9))
					sm_dprintf("db_map_store append=%s\n",
						(char *) data.data);
			}
		}
# if DB_VERSION_MAJOR < 2
		status = db->put(db, &key, &data, 0);
# else /* DB_VERSION_MAJOR < 2 */
		status = errno = db->put(db, NULL, &key, &data, 0);
# endif /* DB_VERSION_MAJOR < 2 */
	}
	if (status != 0)
		syserr("readaliases: db put (%s)", lhs);
}


/*
**  DB_MAP_CLOSE -- add distinguished entries and close the database
*/

void
db_map_close(map)
	MAP *map;
{
	register DB *db = map->map_db2;

	if (tTd(38, 9))
		sm_dprintf("db_map_close(%s, %s, %lx)\n",
			map->map_mname, map->map_file, map->map_mflags);

	if (bitset(MF_WRITABLE, map->map_mflags))
	{
		/* write out the distinguished alias */
		db_map_store(map, "@", "@");
	}

	(void) db->sync(db, 0);

# if !LOCK_ON_OPEN
	if (map->map_lockfd >= 0)
		(void) close(map->map_lockfd);
# endif /* !LOCK_ON_OPEN */

# if DB_VERSION_MAJOR < 2
	if (db->close(db) != 0)
# else /* DB_VERSION_MAJOR < 2 */
	/*
	**  Berkeley DB can use internal shared memory
	**  locking for its memory pool.  Closing a map
	**  opened by another process will interfere
	**  with the shared memory and locks of the parent
	**  process leaving things in a bad state.
	*/

	/*
	**  If this map was not opened by the current
	**  process, do not close the map but recover
	**  the file descriptor.
	*/

	if (map->map_pid != CurrentPid)
	{
		int fd = -1;

		errno = db->fd(db, &fd);
		if (fd >= 0)
			(void) close(fd);
		return;
	}

	if ((errno = db->close(db, 0)) != 0)
# endif /* DB_VERSION_MAJOR < 2 */
		syserr("db_map_close(%s, %s, %lx): db close failure",
			map->map_mname, map->map_file, map->map_mflags);
}
#endif /* NEWDB */
/*
**  NIS Modules
*/

#if NIS

# ifndef YPERR_BUSY
#  define YPERR_BUSY	16
# endif /* ! YPERR_BUSY */

/*
**  NIS_MAP_OPEN -- open DBM map
*/

bool
nis_map_open(map, mode)
	MAP *map;
	int mode;
{
	int yperr;
	register char *p;
	auto char *vp;
	auto int vsize;

	if (tTd(38, 2))
		sm_dprintf("nis_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);

	mode &= O_ACCMODE;
	if (mode != O_RDONLY)
	{
		/* issue a pseudo-error message */
		errno = SM_EMAPCANTWRITE;
		return false;
	}

	p = strchr(map->map_file, '@');
	if (p != NULL)
	{
		*p++ = '\0';
		if (*p != '\0')
			map->map_domain = p;
	}

	if (*map->map_file == '\0')
		map->map_file = "mail.aliases";

	if (map->map_domain == NULL)
	{
		yperr = yp_get_default_domain(&map->map_domain);
		if (yperr != 0)
		{
			if (!bitset(MF_OPTIONAL, map->map_mflags))
				syserr("451 4.3.5 NIS map %s specified, but NIS not running",
				       map->map_file);
			return false;
		}
	}

	/* check to see if this map actually exists */
	vp = NULL;
	yperr = yp_match(map->map_domain, map->map_file, "@", 1,
			&vp, &vsize);
	if (tTd(38, 10))
		sm_dprintf("nis_map_open: yp_match(@, %s, %s) => %s\n",
			map->map_domain, map->map_file, yperr_string(yperr));
	if (vp != NULL)
		sm_free(vp);

	if (yperr == 0 || yperr == YPERR_KEY || yperr == YPERR_BUSY)
	{
		/*
		**  We ought to be calling aliaswait() here if this is an
		**  alias file, but powerful HP-UX NIS servers  apparently
		**  don't insert the @:@ token into the alias map when it
		**  is rebuilt, so aliaswait() just hangs.  I hate HP-UX.
		*/

# if 0
		if (!bitset(MF_ALIAS, map->map_mflags) ||
		    aliaswait(map, NULL, true))
# endif /* 0 */
			return true;
	}

	if (!bitset(MF_OPTIONAL, map->map_mflags))
	{
		syserr("451 4.3.5 Cannot bind to map %s in domain %s: %s",
			map->map_file, map->map_domain, yperr_string(yperr));
	}

	return false;
}


/*
**  NIS_MAP_LOOKUP -- look up a datum in a NIS map
*/

/* ARGSUSED3 */
char *
nis_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	char *vp;
	auto int vsize;
	int buflen;
	int yperr;
	char keybuf[MAXNAME + 1];
	char *SM_NONVOLATILE result = NULL;

	if (tTd(38, 20))
		sm_dprintf("nis_map_lookup(%s, %s)\n",
			map->map_mname, name);

	buflen = strlen(name);
	if (buflen > sizeof(keybuf) - 1)
		buflen = sizeof(keybuf) - 1;
	memmove(keybuf, name, buflen);
	keybuf[buflen] = '\0';
	if (!bitset(MF_NOFOLDCASE, map->map_mflags))
		makelower(keybuf);
	yperr = YPERR_KEY;
	vp = NULL;
	if (bitset(MF_TRY0NULL, map->map_mflags))
	{
		yperr = yp_match(map->map_domain, map->map_file, keybuf, buflen,
			     &vp, &vsize);
		if (yperr == 0)
			map->map_mflags &= ~MF_TRY1NULL;
	}
	if (yperr == YPERR_KEY && bitset(MF_TRY1NULL, map->map_mflags))
	{
		SM_FREE_CLR(vp);
		buflen++;
		yperr = yp_match(map->map_domain, map->map_file, keybuf, buflen,
			     &vp, &vsize);
		if (yperr == 0)
			map->map_mflags &= ~MF_TRY0NULL;
	}
	if (yperr != 0)
	{
		if (yperr != YPERR_KEY && yperr != YPERR_BUSY)
			map->map_mflags &= ~(MF_VALID|MF_OPEN);
		if (vp != NULL)
			sm_free(vp);
		return NULL;
	}
	SM_TRY
		if (bitset(MF_MATCHONLY, map->map_mflags))
			result = map_rewrite(map, name, strlen(name), NULL);
		else
			result = map_rewrite(map, vp, vsize, av);
	SM_FINALLY
		if (vp != NULL)
			sm_free(vp);
	SM_END_TRY
	return result;
}


/*
**  NIS_GETCANONNAME -- look up canonical name in NIS
*/

static bool
nis_getcanonname(name, hbsize, statp)
	char *name;
	int hbsize;
	int *statp;
{
	char *vp;
	auto int vsize;
	int keylen;
	int yperr;
	static bool try0null = true;
	static bool try1null = true;
	static char *yp_domain = NULL;
	char host_record[MAXLINE];
	char cbuf[MAXNAME];
	char nbuf[MAXNAME + 1];

	if (tTd(38, 20))
		sm_dprintf("nis_getcanonname(%s)\n", name);

	if (sm_strlcpy(nbuf, name, sizeof(nbuf)) >= sizeof(nbuf))
	{
		*statp = EX_UNAVAILABLE;
		return false;
	}
	(void) shorten_hostname(nbuf);
	keylen = strlen(nbuf);

	if (yp_domain == NULL)
		(void) yp_get_default_domain(&yp_domain);
	makelower(nbuf);
	yperr = YPERR_KEY;
	vp = NULL;
	if (try0null)
	{
		yperr = yp_match(yp_domain, "hosts.byname", nbuf, keylen,
			     &vp, &vsize);
		if (yperr == 0)
			try1null = false;
	}
	if (yperr == YPERR_KEY && try1null)
	{
		SM_FREE_CLR(vp);
		keylen++;
		yperr = yp_match(yp_domain, "hosts.byname", nbuf, keylen,
			     &vp, &vsize);
		if (yperr == 0)
			try0null = false;
	}
	if (yperr != 0)
	{
		if (yperr == YPERR_KEY)
			*statp = EX_NOHOST;
		else if (yperr == YPERR_BUSY)
			*statp = EX_TEMPFAIL;
		else
			*statp = EX_UNAVAILABLE;
		if (vp != NULL)
			sm_free(vp);
		return false;
	}
	(void) sm_strlcpy(host_record, vp, sizeof(host_record));
	sm_free(vp);
	if (tTd(38, 44))
		sm_dprintf("got record `%s'\n", host_record);
	vp = strpbrk(host_record, "#\n");
	if (vp != NULL)
		*vp = '\0';
	if (!extract_canonname(nbuf, NULL, host_record, cbuf, sizeof(cbuf)))
	{
		/* this should not happen, but.... */
		*statp = EX_NOHOST;
		return false;
	}
	if (sm_strlcpy(name, cbuf, hbsize) >= hbsize)
	{
		*statp = EX_UNAVAILABLE;
		return false;
	}
	*statp = EX_OK;
	return true;
}

#endif /* NIS */
/*
**  NISPLUS Modules
**
**	This code donated by Sun Microsystems.
*/

#if NISPLUS

# undef NIS		/* symbol conflict in nis.h */
# undef T_UNSPEC	/* symbol conflict in nis.h -> ... -> sys/tiuser.h */
# include <rpcsvc/nis.h>
# include <rpcsvc/nislib.h>
# ifndef NIS_TABLE_OBJ
#  define NIS_TABLE_OBJ TABLE_OBJ
# endif /* NIS_TABLE_OBJ */

# define EN_col(col)	zo_data.objdata_u.en_data.en_cols.en_cols_val[(col)].ec_value.ec_value_val
# define COL_NAME(res,i)	((res->objects.objects_val)->TA_data.ta_cols.ta_cols_val)[i].tc_name
# define COL_MAX(res)	((res->objects.objects_val)->TA_data.ta_cols.ta_cols_len)
# define PARTIAL_NAME(x)	((x)[strlen(x) - 1] != '.')

/*
**  NISPLUS_MAP_OPEN -- open nisplus table
*/

bool
nisplus_map_open(map, mode)
	MAP *map;
	int mode;
{
	nis_result *res = NULL;
	int retry_cnt, max_col, i;
	char qbuf[MAXLINE + NIS_MAXNAMELEN];

	if (tTd(38, 2))
		sm_dprintf("nisplus_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);

	mode &= O_ACCMODE;
	if (mode != O_RDONLY)
	{
		errno = EPERM;
		return false;
	}

	if (*map->map_file == '\0')
		map->map_file = "mail_aliases.org_dir";

	if (PARTIAL_NAME(map->map_file) && map->map_domain == NULL)
	{
		/* set default NISPLUS Domain to $m */
		map->map_domain = newstr(nisplus_default_domain());
		if (tTd(38, 2))
			sm_dprintf("nisplus_map_open(%s): using domain %s\n",
				map->map_file, map->map_domain);
	}
	if (!PARTIAL_NAME(map->map_file))
	{
		map->map_domain = newstr("");
		(void) sm_strlcpy(qbuf, map->map_file, sizeof(qbuf));
	}
	else
	{
		/* check to see if this map actually exists */
		(void) sm_strlcpyn(qbuf, sizeof(qbuf), 3,
				   map->map_file, ".", map->map_domain);
	}

	retry_cnt = 0;
	while (res == NULL || res->status != NIS_SUCCESS)
	{
		res = nis_lookup(qbuf, FOLLOW_LINKS);
		switch (res->status)
		{
		  case NIS_SUCCESS:
			break;

		  case NIS_TRYAGAIN:
		  case NIS_RPCERROR:
		  case NIS_NAMEUNREACHABLE:
			if (retry_cnt++ > 4)
			{
				errno = EAGAIN;
				return false;
			}
			/* try not to overwhelm hosed server */
			sleep(2);
			break;

		  default:		/* all other nisplus errors */
# if 0
			if (!bitset(MF_OPTIONAL, map->map_mflags))
				syserr("451 4.3.5 Cannot find table %s.%s: %s",
					map->map_file, map->map_domain,
					nis_sperrno(res->status));
# endif /* 0 */
			errno = EAGAIN;
			return false;
		}
	}

	if (NIS_RES_NUMOBJ(res) != 1 ||
	    (NIS_RES_OBJECT(res)->zo_data.zo_type != NIS_TABLE_OBJ))
	{
		if (tTd(38, 10))
			sm_dprintf("nisplus_map_open: %s is not a table\n", qbuf);
# if 0
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("451 4.3.5 %s.%s: %s is not a table",
				map->map_file, map->map_domain,
				nis_sperrno(res->status));
# endif /* 0 */
		errno = EBADF;
		return false;
	}
	/* default key column is column 0 */
	if (map->map_keycolnm == NULL)
		map->map_keycolnm = newstr(COL_NAME(res,0));

	max_col = COL_MAX(res);

	/* verify the key column exist */
	for (i = 0; i < max_col; i++)
	{
		if (strcmp(map->map_keycolnm, COL_NAME(res,i)) == 0)
			break;
	}
	if (i == max_col)
	{
		if (tTd(38, 2))
			sm_dprintf("nisplus_map_open(%s): can not find key column %s\n",
				map->map_file, map->map_keycolnm);
		errno = ENOENT;
		return false;
	}

	/* default value column is the last column */
	if (map->map_valcolnm == NULL)
	{
		map->map_valcolno = max_col - 1;
		return true;
	}

	for (i = 0; i< max_col; i++)
	{
		if (strcmp(map->map_valcolnm, COL_NAME(res,i)) == 0)
		{
			map->map_valcolno = i;
			return true;
		}
	}

	if (tTd(38, 2))
		sm_dprintf("nisplus_map_open(%s): can not find column %s\n",
			map->map_file, map->map_keycolnm);
	errno = ENOENT;
	return false;
}


/*
**  NISPLUS_MAP_LOOKUP -- look up a datum in a NISPLUS table
*/

char *
nisplus_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	char *p;
	auto int vsize;
	char *skp;
	int skleft;
	char search_key[MAXNAME + 4];
	char qbuf[MAXLINE + NIS_MAXNAMELEN];
	nis_result *result;

	if (tTd(38, 20))
		sm_dprintf("nisplus_map_lookup(%s, %s)\n",
			map->map_mname, name);

	if (!bitset(MF_OPEN, map->map_mflags))
	{
		if (nisplus_map_open(map, O_RDONLY))
		{
			map->map_mflags |= MF_OPEN;
			map->map_pid = CurrentPid;
		}
		else
		{
			*statp = EX_UNAVAILABLE;
			return NULL;
		}
	}

	/*
	**  Copy the name to the key buffer, escaping double quote characters
	**  by doubling them and quoting "]" and "," to avoid having the
	**  NIS+ parser choke on them.
	*/

	skleft = sizeof(search_key) - 4;
	skp = search_key;
	for (p = name; *p != '\0' && skleft > 0; p++)
	{
		switch (*p)
		{
		  case ']':
		  case ',':
			/* quote the character */
			*skp++ = '"';
			*skp++ = *p;
			*skp++ = '"';
			skleft -= 3;
			break;

		  case '"':
			/* double the quote */
			*skp++ = '"';
			skleft--;
			/* FALLTHROUGH */

		  default:
			*skp++ = *p;
			skleft--;
			break;
		}
	}
	*skp = '\0';
	if (!bitset(MF_NOFOLDCASE, map->map_mflags))
		makelower(search_key);

	/* construct the query */
	if (PARTIAL_NAME(map->map_file))
		(void) sm_snprintf(qbuf, sizeof(qbuf), "[%s=%s],%s.%s",
			map->map_keycolnm, search_key, map->map_file,
			map->map_domain);
	else
		(void) sm_snprintf(qbuf, sizeof(qbuf), "[%s=%s],%s",
			map->map_keycolnm, search_key, map->map_file);

	if (tTd(38, 20))
		sm_dprintf("qbuf=%s\n", qbuf);
	result = nis_list(qbuf, FOLLOW_LINKS | FOLLOW_PATH, NULL, NULL);
	if (result->status == NIS_SUCCESS)
	{
		int count;
		char *str;

		if ((count = NIS_RES_NUMOBJ(result)) != 1)
		{
			if (LogLevel > 10)
				sm_syslog(LOG_WARNING, CurEnv->e_id,
					  "%s: lookup error, expected 1 entry, got %d",
					  map->map_file, count);

			/* ignore second entry */
			if (tTd(38, 20))
				sm_dprintf("nisplus_map_lookup(%s), got %d entries, additional entries ignored\n",
					name, count);
		}

		p = ((NIS_RES_OBJECT(result))->EN_col(map->map_valcolno));
		/* set the length of the result */
		if (p == NULL)
			p = "";
		vsize = strlen(p);
		if (tTd(38, 20))
			sm_dprintf("nisplus_map_lookup(%s), found %s\n",
				name, p);
		if (bitset(MF_MATCHONLY, map->map_mflags))
			str = map_rewrite(map, name, strlen(name), NULL);
		else
			str = map_rewrite(map, p, vsize, av);
		nis_freeresult(result);
		*statp = EX_OK;
		return str;
	}
	else
	{
		if (result->status == NIS_NOTFOUND)
			*statp = EX_NOTFOUND;
		else if (result->status == NIS_TRYAGAIN)
			*statp = EX_TEMPFAIL;
		else
		{
			*statp = EX_UNAVAILABLE;
			map->map_mflags &= ~(MF_VALID|MF_OPEN);
		}
	}
	if (tTd(38, 20))
		sm_dprintf("nisplus_map_lookup(%s), failed\n", name);
	nis_freeresult(result);
	return NULL;
}



/*
**  NISPLUS_GETCANONNAME -- look up canonical name in NIS+
*/

static bool
nisplus_getcanonname(name, hbsize, statp)
	char *name;
	int hbsize;
	int *statp;
{
	char *vp;
	auto int vsize;
	nis_result *result;
	char *p;
	char nbuf[MAXNAME + 1];
	char qbuf[MAXLINE + NIS_MAXNAMELEN];

	if (sm_strlcpy(nbuf, name, sizeof(nbuf)) >= sizeof(nbuf))
	{
		*statp = EX_UNAVAILABLE;
		return false;
	}
	(void) shorten_hostname(nbuf);

	p = strchr(nbuf, '.');
	if (p == NULL)
	{
		/* single token */
		(void) sm_snprintf(qbuf, sizeof(qbuf),
			"[name=%s],hosts.org_dir", nbuf);
	}
	else if (p[1] != '\0')
	{
		/* multi token -- take only first token in nbuf */
		*p = '\0';
		(void) sm_snprintf(qbuf, sizeof(qbuf),
				   "[name=%s],hosts.org_dir.%s", nbuf, &p[1]);
	}
	else
	{
		*statp = EX_NOHOST;
		return false;
	}

	if (tTd(38, 20))
		sm_dprintf("\nnisplus_getcanonname(%s), qbuf=%s\n",
			   name, qbuf);

	result = nis_list(qbuf, EXPAND_NAME|FOLLOW_LINKS|FOLLOW_PATH,
			  NULL, NULL);

	if (result->status == NIS_SUCCESS)
	{
		int count;
		char *domain;

		if ((count = NIS_RES_NUMOBJ(result)) != 1)
		{
			if (LogLevel > 10)
				sm_syslog(LOG_WARNING, CurEnv->e_id,
					  "nisplus_getcanonname: lookup error, expected 1 entry, got %d",
					  count);

			/* ignore second entry */
			if (tTd(38, 20))
				sm_dprintf("nisplus_getcanonname(%s), got %d entries, all but first ignored\n",
					   name, count);
		}

		if (tTd(38, 20))
			sm_dprintf("nisplus_getcanonname(%s), found in directory \"%s\"\n",
				   name, (NIS_RES_OBJECT(result))->zo_domain);


		vp = ((NIS_RES_OBJECT(result))->EN_col(0));
		vsize = strlen(vp);
		if (tTd(38, 20))
			sm_dprintf("nisplus_getcanonname(%s), found %s\n",
				   name, vp);
		if (strchr(vp, '.') != NULL)
		{
			domain = "";
		}
		else
		{
			domain = macvalue('m', CurEnv);
			if (domain == NULL)
				domain = "";
		}
		if (hbsize > vsize + (int) strlen(domain) + 1)
		{
			if (domain[0] == '\0')
				(void) sm_strlcpy(name, vp, hbsize);
			else
				(void) sm_snprintf(name, hbsize,
						   "%s.%s", vp, domain);
			*statp = EX_OK;
		}
		else
			*statp = EX_NOHOST;
		nis_freeresult(result);
		return true;
	}
	else
	{
		if (result->status == NIS_NOTFOUND)
			*statp = EX_NOHOST;
		else if (result->status == NIS_TRYAGAIN)
			*statp = EX_TEMPFAIL;
		else
			*statp = EX_UNAVAILABLE;
	}
	if (tTd(38, 20))
		sm_dprintf("nisplus_getcanonname(%s), failed, status=%d, nsw_stat=%d\n",
			   name, result->status, *statp);
	nis_freeresult(result);
	return false;
}

char *
nisplus_default_domain()
{
	static char default_domain[MAXNAME + 1] = "";
	char *p;

	if (default_domain[0] != '\0')
		return default_domain;

	p = nis_local_directory();
	(void) sm_strlcpy(default_domain, p, sizeof(default_domain));
	return default_domain;
}

#endif /* NISPLUS */
/*
**  LDAP Modules
*/

/*
**  LDAPMAP_DEQUOTE - helper routine for ldapmap_parseargs
*/

#if defined(LDAPMAP) || defined(PH_MAP)

# if PH_MAP
#  define ph_map_dequote ldapmap_dequote
# endif /* PH_MAP */

static char *ldapmap_dequote __P((char *));

static char *
ldapmap_dequote(str)
	char *str;
{
	char *p;
	char *start;

	if (str == NULL)
		return NULL;

	p = str;
	if (*p == '"')
	{
		/* Should probably swallow initial whitespace here */
		start = ++p;
	}
	else
		return str;
	while (*p != '"' && *p != '\0')
		p++;
	if (*p != '\0')
		*p = '\0';
	return start;
}
#endif /* defined(LDAPMAP) || defined(PH_MAP) */

#if LDAPMAP

static SM_LDAP_STRUCT *LDAPDefaults = NULL;

/*
**  LDAPMAP_OPEN -- open LDAP map
**
**	Connect to the LDAP server.  Re-use existing connections since a
**	single server connection to a host (with the same host, port,
**	bind DN, and secret) can answer queries for multiple maps.
*/

bool
ldapmap_open(map, mode)
	MAP *map;
	int mode;
{
	SM_LDAP_STRUCT *lmap;
	STAB *s;
	char *id;

	if (tTd(38, 2))
		sm_dprintf("ldapmap_open(%s, %d): ", map->map_mname, mode);

#if defined(SUN_EXTENSIONS) && defined(SUN_SIMPLIFIED_LDAP) && \
    HASLDAPGETALIASBYNAME
	if (VendorCode == VENDOR_SUN &&
	    strcmp(map->map_mname, "aliases.ldap") == 0)
	{
		return true;
	}
#endif /* defined(SUN_EXTENSIONS) && defined(SUN_SIMPLIFIED_LDAP) && ... */

	mode &= O_ACCMODE;

	/* sendmail doesn't have the ability to write to LDAP (yet) */
	if (mode != O_RDONLY)
	{
		/* issue a pseudo-error message */
		errno = SM_EMAPCANTWRITE;
		return false;
	}

	lmap = (SM_LDAP_STRUCT *) map->map_db1;

	s = ldapmap_findconn(lmap);
	if (s->s_lmap != NULL)
	{
		/* Already have a connection open to this LDAP server */
		lmap->ldap_ld = ((SM_LDAP_STRUCT *)s->s_lmap->map_db1)->ldap_ld;
		lmap->ldap_pid = ((SM_LDAP_STRUCT *)s->s_lmap->map_db1)->ldap_pid;

		/* Add this map as head of linked list */
		lmap->ldap_next = s->s_lmap;
		s->s_lmap = map;

		if (tTd(38, 2))
			sm_dprintf("using cached connection\n");
		return true;
	}

	if (tTd(38, 2))
		sm_dprintf("opening new connection\n");

	if (lmap->ldap_host != NULL)
		id = lmap->ldap_host;
	else if (lmap->ldap_uri != NULL)
		id = lmap->ldap_uri;
	else
		id = "localhost";

	if (tTd(74, 104))
	{
		extern MAPCLASS NullMapClass;

		/* debug mode: don't actually open an LDAP connection */
		map->map_orgclass = map->map_class;
		map->map_class = &NullMapClass;
		map->map_mflags |= MF_OPEN;
		map->map_pid = CurrentPid;
		return true;
	}

	/* No connection yet, connect */
	if (!sm_ldap_start(map->map_mname, lmap))
	{
		if (errno == ETIMEDOUT)
		{
			if (LogLevel > 1)
				sm_syslog(LOG_NOTICE, CurEnv->e_id,
					  "timeout connecting to LDAP server %.100s",
					  id);
		}

		if (!bitset(MF_OPTIONAL, map->map_mflags))
		{
			if (bitset(MF_NODEFER, map->map_mflags))
			{
				syserr("%s failed to %s in map %s",
# if USE_LDAP_INIT
				       "ldap_init/ldap_bind",
# else /* USE_LDAP_INIT */
				       "ldap_open",
# endif /* USE_LDAP_INIT */
				       id, map->map_mname);
			}
			else
			{
				syserr("451 4.3.5 %s failed to %s in map %s",
# if USE_LDAP_INIT
				       "ldap_init/ldap_bind",
# else /* USE_LDAP_INIT */
				       "ldap_open",
# endif /* USE_LDAP_INIT */
				       id, map->map_mname);
			}
		}
		return false;
	}

	/* Save connection for reuse */
	s->s_lmap = map;
	return true;
}

/*
**  LDAPMAP_CLOSE -- close ldap map
*/

void
ldapmap_close(map)
	MAP *map;
{
	SM_LDAP_STRUCT *lmap;
	STAB *s;

	if (tTd(38, 2))
		sm_dprintf("ldapmap_close(%s)\n", map->map_mname);

	lmap = (SM_LDAP_STRUCT *) map->map_db1;

	/* Check if already closed */
	if (lmap->ldap_ld == NULL)
		return;

	/* Close the LDAP connection */
	sm_ldap_close(lmap);

	/* Mark all the maps that share the connection as closed */
	s = ldapmap_findconn(lmap);

	while (s->s_lmap != NULL)
	{
		MAP *smap = s->s_lmap;

		if (tTd(38, 2) && smap != map)
			sm_dprintf("ldapmap_close(%s): closed %s (shared LDAP connection)\n",
				   map->map_mname, smap->map_mname);
		smap->map_mflags &= ~(MF_OPEN|MF_WRITABLE);
		lmap = (SM_LDAP_STRUCT *) smap->map_db1;
		lmap->ldap_ld = NULL;
		s->s_lmap = lmap->ldap_next;
		lmap->ldap_next = NULL;
	}
}

# ifdef SUNET_ID
/*
**  SUNET_ID_HASH -- Convert a string to its Sunet_id canonical form
**  This only makes sense at Stanford University.
*/

static char *
sunet_id_hash(str)
	char *str;
{
	char *p, *p_last;

	p = str;
	p_last = p;
	while (*p != '\0')
	{
		if (isascii(*p) && (islower(*p) || isdigit(*p)))
		{
			*p_last = *p;
			p_last++;
		}
		else if (isascii(*p) && isupper(*p))
		{
			*p_last = tolower(*p);
			p_last++;
		}
		++p;
	}
	if (*p_last != '\0')
		*p_last = '\0';
	return str;
}
#  define SM_CONVERT_ID(str)	sunet_id_hash(str)
# else /* SUNET_ID */
#  define SM_CONVERT_ID(str)	makelower(str)
# endif /* SUNET_ID */

/*
**  LDAPMAP_LOOKUP -- look up a datum in a LDAP map
*/

char *
ldapmap_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	int flags;
	int i;
	int plen = 0;
	int psize = 0;
	int msgid;
	int save_errno;
	char *vp, *p;
	char *result = NULL;
	SM_RPOOL_T *rpool;
	SM_LDAP_STRUCT *lmap = NULL;
	char *argv[SM_LDAP_ARGS];
	char keybuf[MAXKEY];
#if SM_LDAP_ARGS != MAX_MAP_ARGS
# ERROR _SM_LDAP_ARGS must be the same as _MAX_MAP_ARGS
#endif /* SM_LDAP_ARGS != MAX_MAP_ARGS */

#if defined(SUN_EXTENSIONS) && defined(SUN_SIMPLIFIED_LDAP) && \
    HASLDAPGETALIASBYNAME
	if (VendorCode == VENDOR_SUN &&
	    strcmp(map->map_mname, "aliases.ldap") == 0)
	{
		int rc;
#if defined(GETLDAPALIASBYNAME_VERSION) && (GETLDAPALIASBYNAME_VERSION >= 2)
		extern char *__getldapaliasbyname();
		char *answer;

		answer = __getldapaliasbyname(name, &rc);
#else
		char answer[MAXNAME + 1];

		rc = __getldapaliasbyname(name, answer, sizeof(answer));
#endif
		if (rc != 0)
		{
			if (tTd(38, 20))
				sm_dprintf("getldapaliasbyname(%.100s) failed, errno=%d\n",
					   name, errno);
			*statp = EX_NOTFOUND;
			return NULL;
		}
		*statp = EX_OK;
		if (tTd(38, 20))
			sm_dprintf("getldapaliasbyname(%.100s) => %s\n", name,
				   answer);
		if (bitset(MF_MATCHONLY, map->map_mflags))
			result = map_rewrite(map, name, strlen(name), NULL);
		else
			result = map_rewrite(map, answer, strlen(answer), av);
#if defined(GETLDAPALIASBYNAME_VERSION) && (GETLDAPALIASBYNAME_VERSION >= 2)
		free(answer);
#endif
		return result;
	}
#endif /* defined(SUN_EXTENSIONS) && defined(SUN_SIMPLIFIED_LDAP) && ... */

	/* Get ldap struct pointer from map */
	lmap = (SM_LDAP_STRUCT *) map->map_db1;
	sm_ldap_setopts(lmap->ldap_ld, lmap);

	if (lmap->ldap_multi_args)
	{
		SM_REQUIRE(av != NULL);
		memset(argv, '\0', sizeof(argv));
		for (i = 0; i < SM_LDAP_ARGS && av[i] != NULL; i++)
		{
			argv[i] = sm_strdup(av[i]);
			if (argv[i] == NULL)
			{
				int save_errno, j;

				save_errno = errno;
				for (j = 0; j < i && argv[j] != NULL; j++)
					SM_FREE(argv[j]);
				*statp = EX_TEMPFAIL;
				errno = save_errno;
				return NULL;
			}

			if (!bitset(MF_NOFOLDCASE, map->map_mflags))
				SM_CONVERT_ID(av[i]);
		}
	}
	else
	{
		(void) sm_strlcpy(keybuf, name, sizeof(keybuf));

		if (!bitset(MF_NOFOLDCASE, map->map_mflags))
			SM_CONVERT_ID(keybuf);
	}

	if (tTd(38, 20))
	{
		if (lmap->ldap_multi_args)
		{
			sm_dprintf("ldapmap_lookup(%s, argv)\n",
				map->map_mname);
			for (i = 0; i < SM_LDAP_ARGS; i++)
			{
				sm_dprintf("   argv[%d] = %s\n", i,
					   argv[i] == NULL ? "NULL" : argv[i]);
			}
		}
		else
		{
			sm_dprintf("ldapmap_lookup(%s, %s)\n",
				   map->map_mname, name);
		}
	}

	if (lmap->ldap_multi_args)
	{
		msgid = sm_ldap_search_m(lmap, argv);

		/* free the argv array and its content, no longer needed */
		for (i = 0; i < SM_LDAP_ARGS && argv[i] != NULL; i++)
			SM_FREE(argv[i]);
	}
	else
		msgid = sm_ldap_search(lmap, keybuf);
	if (msgid == SM_LDAP_ERR)
	{
		errno = sm_ldap_geterrno(lmap->ldap_ld) + E_LDAPBASE;
		save_errno = errno;
		if (!bitset(MF_OPTIONAL, map->map_mflags))
		{
			/*
			**  Do not include keybuf as this error may be shown
			**  to outsiders.
			*/

			if (bitset(MF_NODEFER, map->map_mflags))
				syserr("Error in ldap_search in map %s",
				       map->map_mname);
			else
				syserr("451 4.3.5 Error in ldap_search in map %s",
				       map->map_mname);
		}
		*statp = EX_TEMPFAIL;
		switch (save_errno - E_LDAPBASE)
		{
# ifdef LDAP_SERVER_DOWN
		  case LDAP_SERVER_DOWN:
# endif /* LDAP_SERVER_DOWN */
		  case LDAP_TIMEOUT:
		  case LDAP_UNAVAILABLE:
			/* server disappeared, try reopen on next search */
			ldapmap_close(map);
			break;
		}
		errno = save_errno;
		return NULL;
	}
#if SM_LDAP_ERROR_ON_MISSING_ARGS
	else if (msgid == SM_LDAP_ERR_ARG_MISS)
	{
		if (bitset(MF_NODEFER, map->map_mflags))
			syserr("Error in ldap_search in map %s, too few arguments",
			       map->map_mname);
		else
			syserr("554 5.3.5 Error in ldap_search in map %s, too few arguments",
			       map->map_mname);
		*statp = EX_CONFIG;
		return NULL;
	}
#endif /* SM_LDAP_ERROR_ON_MISSING_ARGS */

	*statp = EX_NOTFOUND;
	vp = NULL;

	flags = 0;
	if (bitset(MF_SINGLEMATCH, map->map_mflags))
		flags |= SM_LDAP_SINGLEMATCH;
	if (bitset(MF_MATCHONLY, map->map_mflags))
		flags |= SM_LDAP_MATCHONLY;
# if _FFR_LDAP_SINGLEDN
	if (bitset(MF_SINGLEDN, map->map_mflags))
		flags |= SM_LDAP_SINGLEDN;
# endif /* _FFR_LDAP_SINGLEDN */

	/* Create an rpool for search related memory usage */
	rpool = sm_rpool_new_x(NULL);

	p = NULL;
	*statp = sm_ldap_results(lmap, msgid, flags, map->map_coldelim,
				 rpool, &p, &plen, &psize, NULL);
	save_errno = errno;

	/* Copy result so rpool can be freed */
	if (*statp == EX_OK && p != NULL)
		vp = newstr(p);
	sm_rpool_free(rpool);

	/* need to restart LDAP connection? */
	if (*statp == EX_RESTART)
	{
		*statp = EX_TEMPFAIL;
		ldapmap_close(map);
	}

	errno = save_errno;
	if (*statp != EX_OK && *statp != EX_NOTFOUND)
	{
		if (!bitset(MF_OPTIONAL, map->map_mflags))
		{
			if (bitset(MF_NODEFER, map->map_mflags))
				syserr("Error getting LDAP results, map=%s, name=%s",
				       map->map_mname, name);
			else
				syserr("451 4.3.5 Error getting LDAP results, map=%s, name=%s",
				       map->map_mname, name);
		}
		errno = save_errno;
		return NULL;
	}

	/* Did we match anything? */
	if (vp == NULL && !bitset(MF_MATCHONLY, map->map_mflags))
		return NULL;

	if (*statp == EX_OK)
	{
		if (LogLevel > 9)
			sm_syslog(LOG_INFO, CurEnv->e_id,
				  "ldap=%s, %.100s=>%s", map->map_mname, name,
				  vp == NULL ? "<NULL>" : vp);
		if (bitset(MF_MATCHONLY, map->map_mflags))
			result = map_rewrite(map, name, strlen(name), NULL);
		else
		{
			/* vp != NULL according to test above */
			result = map_rewrite(map, vp, strlen(vp), av);
		}
		if (vp != NULL)
			sm_free(vp); /* XXX */
	}
	return result;
}

/*
**  LDAPMAP_FINDCONN -- find an LDAP connection to the server
**
**	Cache LDAP connections based on the host, port, bind DN,
**	secret, and PID so we don't have multiple connections open to
**	the same server for different maps.  Need a separate connection
**	per PID since a parent process may close the map before the
**	child is done with it.
**
**	Parameters:
**		lmap -- LDAP map information
**
**	Returns:
**		Symbol table entry for the LDAP connection.
*/

static STAB *
ldapmap_findconn(lmap)
	SM_LDAP_STRUCT *lmap;
{
	char *format;
	char *nbuf;
	char *id;
	STAB *SM_NONVOLATILE s = NULL;

	if (lmap->ldap_host != NULL)
		id = lmap->ldap_host;
	else if (lmap->ldap_uri != NULL)
		id = lmap->ldap_uri;
	else
		id = "localhost";

	format = "%s%c%d%c%d%c%s%c%s%d";
	nbuf = sm_stringf_x(format,
			    id,
			    CONDELSE,
			    lmap->ldap_port,
			    CONDELSE,
			    lmap->ldap_version,
			    CONDELSE,
			    (lmap->ldap_binddn == NULL ? ""
						       : lmap->ldap_binddn),
			    CONDELSE,
			    (lmap->ldap_secret == NULL ? ""
						       : lmap->ldap_secret),
			    (int) CurrentPid);
	SM_TRY
		s = stab(nbuf, ST_LMAP, ST_ENTER);
	SM_FINALLY
		sm_free(nbuf);
	SM_END_TRY
	return s;
}
/*
**  LDAPMAP_PARSEARGS -- parse ldap map definition args.
*/

static struct lamvalues LDAPAuthMethods[] =
{
	{	"none",		LDAP_AUTH_NONE		},
	{	"simple",	LDAP_AUTH_SIMPLE	},
# ifdef LDAP_AUTH_KRBV4
	{	"krbv4",	LDAP_AUTH_KRBV4		},
# endif /* LDAP_AUTH_KRBV4 */
	{	NULL,		0			}
};

static struct ladvalues LDAPAliasDereference[] =
{
	{	"never",	LDAP_DEREF_NEVER	},
	{	"always",	LDAP_DEREF_ALWAYS	},
	{	"search",	LDAP_DEREF_SEARCHING	},
	{	"find",		LDAP_DEREF_FINDING	},
	{	NULL,		0			}
};

static struct lssvalues LDAPSearchScope[] =
{
	{	"base",		LDAP_SCOPE_BASE		},
	{	"one",		LDAP_SCOPE_ONELEVEL	},
	{	"sub",		LDAP_SCOPE_SUBTREE	},
	{	NULL,		0			}
};

bool
ldapmap_parseargs(map, args)
	MAP *map;
	char *args;
{
	bool secretread = true;
	bool attrssetup = false;
	int i;
	register char *p = args;
	SM_LDAP_STRUCT *lmap;
	struct lamvalues *lam;
	struct ladvalues *lad;
	struct lssvalues *lss;
	char ldapfilt[MAXLINE];
	char m_tmp[MAXPATHLEN + LDAPMAP_MAX_PASSWD];

	/* Get ldap struct pointer from map */
	lmap = (SM_LDAP_STRUCT *) map->map_db1;

	/* Check if setting the initial LDAP defaults */
	if (lmap == NULL || lmap != LDAPDefaults)
	{
		/* We need to alloc an SM_LDAP_STRUCT struct */
		lmap = (SM_LDAP_STRUCT *) xalloc(sizeof(*lmap));
		if (LDAPDefaults == NULL)
			sm_ldap_clear(lmap);
		else
			STRUCTCOPY(*LDAPDefaults, *lmap);
	}

	/* there is no check whether there is really an argument */
	map->map_mflags |= MF_TRY0NULL|MF_TRY1NULL;
	map->map_spacesub = SpaceSub;	/* default value */

	/* Check if setting up an alias or file class LDAP map */
	if (bitset(MF_ALIAS, map->map_mflags))
	{
		/* Comma separate if used as an alias file */
		map->map_coldelim = ',';
		if (*args == '\0')
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

			lc = macvalue(macid("{sendmailMTACluster}"), CurEnv);
			if (lc == NULL)
				lc = "";
			else
			{
				expand(lc, lcbuf, sizeof(lcbuf), CurEnv);
				lc = lcbuf;
			}

			n = sm_snprintf(ldapfilt, sizeof(ldapfilt),
					"(&(objectClass=sendmailMTAAliasObject)(sendmailMTAAliasGrouping=aliases)(|(sendmailMTACluster=%s)(sendmailMTAHost=%s))(sendmailMTAKey=%%0))",
					lc, jbuf);
			if (n >= sizeof(ldapfilt))
			{
				syserr("%s: Default LDAP string too long",
				       map->map_mname);
				return false;
			}

			/* default args for an alias LDAP entry */
			lmap->ldap_filter = ldapfilt;
			lmap->ldap_attr[0] = "objectClass";
			lmap->ldap_attr_type[0] = SM_LDAP_ATTR_OBJCLASS;
			lmap->ldap_attr_needobjclass[0] = NULL;
			lmap->ldap_attr[1] = "sendmailMTAAliasValue";
			lmap->ldap_attr_type[1] = SM_LDAP_ATTR_NORMAL;
			lmap->ldap_attr_needobjclass[1] = NULL;
			lmap->ldap_attr[2] = "sendmailMTAAliasSearch";
			lmap->ldap_attr_type[2] = SM_LDAP_ATTR_FILTER;
			lmap->ldap_attr_needobjclass[2] = "sendmailMTAMapObject";
			lmap->ldap_attr[3] = "sendmailMTAAliasURL";
			lmap->ldap_attr_type[3] = SM_LDAP_ATTR_URL;
			lmap->ldap_attr_needobjclass[3] = "sendmailMTAMapObject";
			lmap->ldap_attr[4] = NULL;
			lmap->ldap_attr_type[4] = SM_LDAP_ATTR_NONE;
			lmap->ldap_attr_needobjclass[4] = NULL;
			attrssetup = true;
		}
	}
	else if (bitset(MF_FILECLASS, map->map_mflags))
	{
		/* Space separate if used as a file class file */
		map->map_coldelim = ' ';
	}

# if _FFR_LDAP_NETWORK_TIMEOUT
	lmap->ldap_networktmo = 120;
# endif /* _FFR_LDAP_NETWORK_TIMEOUT */

	for (;;)
	{
		while (isascii(*p) && isspace(*p))
			p++;
		if (*p != '-')
			break;
		switch (*++p)
		{
		  case 'A':
			map->map_mflags |= MF_APPEND;
			break;

		  case 'a':
			map->map_app = ++p;
			break;

		  case 'D':
			map->map_mflags |= MF_DEFER;
			break;

		  case 'f':
			map->map_mflags |= MF_NOFOLDCASE;
			break;

		  case 'm':
			map->map_mflags |= MF_MATCHONLY;
			break;

		  case 'N':
			map->map_mflags |= MF_INCLNULL;
			map->map_mflags &= ~MF_TRY0NULL;
			break;

		  case 'O':
			map->map_mflags &= ~MF_TRY1NULL;
			break;

		  case 'o':
			map->map_mflags |= MF_OPTIONAL;
			break;

		  case 'q':
			map->map_mflags |= MF_KEEPQUOTES;
			break;

		  case 'S':
			map->map_spacesub = *++p;
			break;

		  case 'T':
			map->map_tapp = ++p;
			break;

		  case 't':
			map->map_mflags |= MF_NODEFER;
			break;

		  case 'z':
			if (*++p != '\\')
				map->map_coldelim = *p;
			else
			{
				switch (*++p)
				{
				  case 'n':
					map->map_coldelim = '\n';
					break;

				  case 't':
					map->map_coldelim = '\t';
					break;

				  default:
					map->map_coldelim = '\\';
				}
			}
			break;

			/* Start of ldapmap specific args */
		  case '1':
			map->map_mflags |= MF_SINGLEMATCH;
			break;

# if _FFR_LDAP_SINGLEDN
		  case '2':
			map->map_mflags |= MF_SINGLEDN;
			break;
# endif /* _FFR_LDAP_SINGLEDN */

		  case 'b':		/* search base */
			while (isascii(*++p) && isspace(*p))
				continue;
			lmap->ldap_base = p;
			break;

# if _FFR_LDAP_NETWORK_TIMEOUT
		  case 'c':		/* network (connect) timeout */
			while (isascii(*++p) && isspace(*p))
				continue;
			lmap->ldap_networktmo = atoi(p);
			break;
# endif /* _FFR_LDAP_NETWORK_TIMEOUT */

		  case 'd':		/* Dn to bind to server as */
			while (isascii(*++p) && isspace(*p))
				continue;
			lmap->ldap_binddn = p;
			break;

		  case 'H':		/* Use LDAP URI */
#  if !USE_LDAP_INIT
			syserr("Must compile with -DUSE_LDAP_INIT to use LDAP URIs (-H) in map %s",
			       map->map_mname);
			return false;
#   else /* !USE_LDAP_INIT */
			if (lmap->ldap_host != NULL)
			{
				syserr("Can not specify both an LDAP host and an LDAP URI in map %s",
				       map->map_mname);
				return false;
			}
			while (isascii(*++p) && isspace(*p))
				continue;
			lmap->ldap_uri = p;
			break;
#  endif /* !USE_LDAP_INIT */

		  case 'h':		/* ldap host */
			while (isascii(*++p) && isspace(*p))
				continue;
			if (lmap->ldap_uri != NULL)
			{
				syserr("Can not specify both an LDAP host and an LDAP URI in map %s",
				       map->map_mname);
				return false;
			}
			lmap->ldap_host = p;
			break;

		  case 'K':
			lmap->ldap_multi_args = true;
			break;

		  case 'k':		/* search field */
			while (isascii(*++p) && isspace(*p))
				continue;
			lmap->ldap_filter = p;
			break;

		  case 'l':		/* time limit */
			while (isascii(*++p) && isspace(*p))
				continue;
			lmap->ldap_timelimit = atoi(p);
			lmap->ldap_timeout.tv_sec = lmap->ldap_timelimit;
			break;

		  case 'M':		/* Method for binding */
			while (isascii(*++p) && isspace(*p))
				continue;

			if (sm_strncasecmp(p, "LDAP_AUTH_", 10) == 0)
				p += 10;

			for (lam = LDAPAuthMethods;
			     lam != NULL && lam->lam_name != NULL; lam++)
			{
				if (sm_strncasecmp(p, lam->lam_name,
						   strlen(lam->lam_name)) == 0)
					break;
			}
			if (lam->lam_name != NULL)
				lmap->ldap_method = lam->lam_code;
			else
			{
				/* bad config line */
				if (!bitset(MCF_OPTFILE,
					    map->map_class->map_cflags))
				{
					char *ptr;

					if ((ptr = strchr(p, ' ')) != NULL)
						*ptr = '\0';
					syserr("Method for binding must be [none|simple|krbv4] (not %s) in map %s",
						p, map->map_mname);
					if (ptr != NULL)
						*ptr = ' ';
					return false;
				}
			}
			break;

		  case 'n':		/* retrieve attribute names only */
			lmap->ldap_attrsonly = LDAPMAP_TRUE;
			break;

			/*
			**  This is a string that is dependent on the
			**  method used defined by 'M'.
			*/

		  case 'P':		/* Secret password for binding */
			 while (isascii(*++p) && isspace(*p))
				continue;
			lmap->ldap_secret = p;
			secretread = false;
			break;

		  case 'p':		/* ldap port */
			while (isascii(*++p) && isspace(*p))
				continue;
			lmap->ldap_port = atoi(p);
			break;

			/* args stolen from ldapsearch.c */
		  case 'R':		/* don't auto chase referrals */
# ifdef LDAP_REFERRALS
			lmap->ldap_options &= ~LDAP_OPT_REFERRALS;
# else /* LDAP_REFERRALS */
			syserr("compile with -DLDAP_REFERRALS for referral support");
# endif /* LDAP_REFERRALS */
			break;

		  case 'r':		/* alias dereferencing */
			while (isascii(*++p) && isspace(*p))
				continue;

			if (sm_strncasecmp(p, "LDAP_DEREF_", 11) == 0)
				p += 11;

			for (lad = LDAPAliasDereference;
			     lad != NULL && lad->lad_name != NULL; lad++)
			{
				if (sm_strncasecmp(p, lad->lad_name,
						   strlen(lad->lad_name)) == 0)
					break;
			}
			if (lad->lad_name != NULL)
				lmap->ldap_deref = lad->lad_code;
			else
			{
				/* bad config line */
				if (!bitset(MCF_OPTFILE,
					    map->map_class->map_cflags))
				{
					char *ptr;

					if ((ptr = strchr(p, ' ')) != NULL)
						*ptr = '\0';
					syserr("Deref must be [never|always|search|find] (not %s) in map %s",
						p, map->map_mname);
					if (ptr != NULL)
						*ptr = ' ';
					return false;
				}
			}
			break;

		  case 's':		/* search scope */
			while (isascii(*++p) && isspace(*p))
				continue;

			if (sm_strncasecmp(p, "LDAP_SCOPE_", 11) == 0)
				p += 11;

			for (lss = LDAPSearchScope;
			     lss != NULL && lss->lss_name != NULL; lss++)
			{
				if (sm_strncasecmp(p, lss->lss_name,
						   strlen(lss->lss_name)) == 0)
					break;
			}
			if (lss->lss_name != NULL)
				lmap->ldap_scope = lss->lss_code;
			else
			{
				/* bad config line */
				if (!bitset(MCF_OPTFILE,
					    map->map_class->map_cflags))
				{
					char *ptr;

					if ((ptr = strchr(p, ' ')) != NULL)
						*ptr = '\0';
					syserr("Scope must be [base|one|sub] (not %s) in map %s",
						p, map->map_mname);
					if (ptr != NULL)
						*ptr = ' ';
					return false;
				}
			}
			break;

		  case 'V':
			if (*++p != '\\')
				lmap->ldap_attrsep = *p;
			else
			{
				switch (*++p)
				{
				  case 'n':
					lmap->ldap_attrsep = '\n';
					break;

				  case 't':
					lmap->ldap_attrsep = '\t';
					break;

				  default:
					lmap->ldap_attrsep = '\\';
				}
			}
			break;

		  case 'v':		/* attr to return */
			while (isascii(*++p) && isspace(*p))
				continue;
			lmap->ldap_attr[0] = p;
			lmap->ldap_attr[1] = NULL;
			break;

		  case 'w':
			/* -w should be for passwd, -P should be for version */
			while (isascii(*++p) && isspace(*p))
				continue;
			lmap->ldap_version = atoi(p);
# ifdef LDAP_VERSION_MAX
			if (lmap->ldap_version > LDAP_VERSION_MAX)
			{
				syserr("LDAP version %d exceeds max of %d in map %s",
				       lmap->ldap_version, LDAP_VERSION_MAX,
				       map->map_mname);
				return false;
			}
# endif /* LDAP_VERSION_MAX */
# ifdef LDAP_VERSION_MIN
			if (lmap->ldap_version < LDAP_VERSION_MIN)
			{
				syserr("LDAP version %d is lower than min of %d in map %s",
				       lmap->ldap_version, LDAP_VERSION_MIN,
				       map->map_mname);
				return false;
			}
# endif /* LDAP_VERSION_MIN */
			break;

		  case 'Z':
			while (isascii(*++p) && isspace(*p))
				continue;
			lmap->ldap_sizelimit = atoi(p);
			break;

		  default:
			syserr("Illegal option %c map %s", *p, map->map_mname);
			break;
		}

		/* need to account for quoted strings here */
		while (*p != '\0' && !(isascii(*p) && isspace(*p)))
		{
			if (*p == '"')
			{
				while (*++p != '"' && *p != '\0')
					continue;
				if (*p != '\0')
					p++;
			}
			else
				p++;
		}

		if (*p != '\0')
			*p++ = '\0';
	}

	if (map->map_app != NULL)
		map->map_app = newstr(ldapmap_dequote(map->map_app));
	if (map->map_tapp != NULL)
		map->map_tapp = newstr(ldapmap_dequote(map->map_tapp));

	/*
	**  We need to swallow up all the stuff into a struct
	**  and dump it into map->map_dbptr1
	*/

	if (lmap->ldap_host != NULL &&
	    (LDAPDefaults == NULL ||
	     LDAPDefaults == lmap ||
	     LDAPDefaults->ldap_host != lmap->ldap_host))
		lmap->ldap_host = newstr(ldapmap_dequote(lmap->ldap_host));
	map->map_domain = lmap->ldap_host;

	if (lmap->ldap_uri != NULL &&
	    (LDAPDefaults == NULL ||
	     LDAPDefaults == lmap ||
	     LDAPDefaults->ldap_uri != lmap->ldap_uri))
		lmap->ldap_uri = newstr(ldapmap_dequote(lmap->ldap_uri));
	map->map_domain = lmap->ldap_uri;

	if (lmap->ldap_binddn != NULL &&
	    (LDAPDefaults == NULL ||
	     LDAPDefaults == lmap ||
	     LDAPDefaults->ldap_binddn != lmap->ldap_binddn))
		lmap->ldap_binddn = newstr(ldapmap_dequote(lmap->ldap_binddn));

	if (lmap->ldap_secret != NULL &&
	    (LDAPDefaults == NULL ||
	     LDAPDefaults == lmap ||
	     LDAPDefaults->ldap_secret != lmap->ldap_secret))
	{
		SM_FILE_T *sfd;
		long sff = SFF_OPENASROOT|SFF_ROOTOK|SFF_NOWLINK|SFF_NOWWFILES|SFF_NOGWFILES;

		if (DontLockReadFiles)
			sff |= SFF_NOLOCK;

		/* need to use method to map secret to passwd string */
		switch (lmap->ldap_method)
		{
		  case LDAP_AUTH_NONE:
			/* Do nothing */
			break;

		  case LDAP_AUTH_SIMPLE:

			/*
			**  Secret is the name of a file with
			**  the first line as the password.
			*/

			/* Already read in the secret? */
			if (secretread)
				break;

			sfd = safefopen(ldapmap_dequote(lmap->ldap_secret),
					O_RDONLY, 0, sff);
			if (sfd == NULL)
			{
				syserr("LDAP map: cannot open secret %s",
				       ldapmap_dequote(lmap->ldap_secret));
				return false;
			}
			lmap->ldap_secret = sfgets(m_tmp, sizeof(m_tmp),
						   sfd, TimeOuts.to_fileopen,
						   "ldapmap_parseargs");
			(void) sm_io_close(sfd, SM_TIME_DEFAULT);
			if (strlen(m_tmp) > LDAPMAP_MAX_PASSWD)
			{
				syserr("LDAP map: secret in %s too long",
				       ldapmap_dequote(lmap->ldap_secret));
				return false;
			}
			if (lmap->ldap_secret != NULL &&
			    strlen(m_tmp) > 0)
			{
				/* chomp newline */
				if (m_tmp[strlen(m_tmp) - 1] == '\n')
					m_tmp[strlen(m_tmp) - 1] = '\0';

				lmap->ldap_secret = m_tmp;
			}
			break;

# ifdef LDAP_AUTH_KRBV4
		  case LDAP_AUTH_KRBV4:

			/*
			**  Secret is where the ticket file is
			**  stashed
			*/

			(void) sm_snprintf(m_tmp, sizeof(m_tmp),
				"KRBTKFILE=%s",
				ldapmap_dequote(lmap->ldap_secret));
			lmap->ldap_secret = m_tmp;
			break;
# endif /* LDAP_AUTH_KRBV4 */

		  default:	       /* Should NEVER get here */
			syserr("LDAP map: Illegal value in lmap method");
			return false;
			/* NOTREACHED */
			break;
		}
	}

	if (lmap->ldap_secret != NULL &&
	    (LDAPDefaults == NULL ||
	     LDAPDefaults == lmap ||
	     LDAPDefaults->ldap_secret != lmap->ldap_secret))
		lmap->ldap_secret = newstr(ldapmap_dequote(lmap->ldap_secret));

	if (lmap->ldap_base != NULL &&
	    (LDAPDefaults == NULL ||
	     LDAPDefaults == lmap ||
	     LDAPDefaults->ldap_base != lmap->ldap_base))
		lmap->ldap_base = newstr(ldapmap_dequote(lmap->ldap_base));

	/*
	**  Save the server from extra work.  If request is for a single
	**  match, tell the server to only return enough records to
	**  determine if there is a single match or not.  This can not
	**  be one since the server would only return one and we wouldn't
	**  know if there were others available.
	*/

	if (bitset(MF_SINGLEMATCH, map->map_mflags))
		lmap->ldap_sizelimit = 2;

	/* If setting defaults, don't process ldap_filter and ldap_attr */
	if (lmap == LDAPDefaults)
		return true;

	if (lmap->ldap_filter != NULL)
		lmap->ldap_filter = newstr(ldapmap_dequote(lmap->ldap_filter));
	else
	{
		if (!bitset(MCF_OPTFILE, map->map_class->map_cflags))
		{
			syserr("No filter given in map %s", map->map_mname);
			return false;
		}
	}

	if (!attrssetup && lmap->ldap_attr[0] != NULL)
	{
		bool recurse = false;
		bool normalseen = false;

		i = 0;
		p = ldapmap_dequote(lmap->ldap_attr[0]);
		lmap->ldap_attr[0] = NULL;

		/* Prime the attr list with the objectClass attribute */
		lmap->ldap_attr[i] = "objectClass";
		lmap->ldap_attr_type[i] = SM_LDAP_ATTR_OBJCLASS;
		lmap->ldap_attr_needobjclass[i] = NULL;
		i++;

		while (p != NULL)
		{
			char *v;

			while (isascii(*p) && isspace(*p))
				p++;
			if (*p == '\0')
				break;
			v = p;
			p = strchr(v, ',');
			if (p != NULL)
				*p++ = '\0';

			if (i >= LDAPMAP_MAX_ATTR)
			{
				syserr("Too many return attributes in %s (max %d)",
				       map->map_mname, LDAPMAP_MAX_ATTR);
				return false;
			}
			if (*v != '\0')
			{
				int j;
				int use;
				char *type;
				char *needobjclass;

				type = strchr(v, ':');
				if (type != NULL)
				{
					*type++ = '\0';
					needobjclass = strchr(type, ':');
					if (needobjclass != NULL)
						*needobjclass++ = '\0';
				}
				else
				{
					needobjclass = NULL;
				}

				use = i;

				/* allow override on "objectClass" type */
				if (sm_strcasecmp(v, "objectClass") == 0 &&
				    lmap->ldap_attr_type[0] == SM_LDAP_ATTR_OBJCLASS)
				{
					use = 0;
				}
				else
				{
					/*
					**  Don't add something to attribute
					**  list twice.
					*/

					for (j = 1; j < i; j++)
					{
						if (sm_strcasecmp(v, lmap->ldap_attr[j]) == 0)
						{
							syserr("Duplicate attribute (%s) in %s",
							       v, map->map_mname);
							return false;
						}
					}

					lmap->ldap_attr[use] = newstr(v);
					if (needobjclass != NULL &&
					    *needobjclass != '\0' &&
					    *needobjclass != '*')
					{
						lmap->ldap_attr_needobjclass[use] = newstr(needobjclass);
					}
					else
					{
						lmap->ldap_attr_needobjclass[use] = NULL;
					}

				}

				if (type != NULL && *type != '\0')
				{
					if (sm_strcasecmp(type, "dn") == 0)
					{
						recurse = true;
						lmap->ldap_attr_type[use] = SM_LDAP_ATTR_DN;
					}
					else if (sm_strcasecmp(type, "filter") == 0)
					{
						recurse = true;
						lmap->ldap_attr_type[use] = SM_LDAP_ATTR_FILTER;
					}
					else if (sm_strcasecmp(type, "url") == 0)
					{
						recurse = true;
						lmap->ldap_attr_type[use] = SM_LDAP_ATTR_URL;
					}
					else if (sm_strcasecmp(type, "normal") == 0)
					{
						lmap->ldap_attr_type[use] = SM_LDAP_ATTR_NORMAL;
						normalseen = true;
					}
					else
					{
						syserr("Unknown attribute type (%s) in %s",
						       type, map->map_mname);
						return false;
					}
				}
				else
				{
					lmap->ldap_attr_type[use] = SM_LDAP_ATTR_NORMAL;
					normalseen = true;
				}
				i++;
			}
		}
		lmap->ldap_attr[i] = NULL;

		/* Set in case needed in future code */
		attrssetup = true;

		if (recurse && !normalseen)
		{
			syserr("LDAP recursion requested in %s but no returnable attribute given",
			       map->map_mname);
			return false;
		}
		if (recurse && lmap->ldap_attrsonly == LDAPMAP_TRUE)
		{
			syserr("LDAP recursion requested in %s can not be used with -n",
			       map->map_mname);
			return false;
		}
	}
	map->map_db1 = (ARBPTR_T) lmap;
	return true;
}

/*
**  LDAPMAP_SET_DEFAULTS -- Read default map spec from LDAPDefaults in .cf
**
**	Parameters:
**		spec -- map argument string from LDAPDefaults option
**
**	Returns:
**		None.
*/

void
ldapmap_set_defaults(spec)
	char *spec;
{
	STAB *class;
	MAP map;

	/* Allocate and set the default values */
	if (LDAPDefaults == NULL)
		LDAPDefaults = (SM_LDAP_STRUCT *) xalloc(sizeof(*LDAPDefaults));
	sm_ldap_clear(LDAPDefaults);

	memset(&map, '\0', sizeof(map));

	/* look up the class */
	class = stab("ldap", ST_MAPCLASS, ST_FIND);
	if (class == NULL)
	{
		syserr("readcf: LDAPDefaultSpec: class ldap not available");
		return;
	}
	map.map_class = &class->s_mapclass;
	map.map_db1 = (ARBPTR_T) LDAPDefaults;
	map.map_mname = "O LDAPDefaultSpec";

	(void) ldapmap_parseargs(&map, spec);

	/* These should never be set in LDAPDefaults */
	if (map.map_mflags != (MF_TRY0NULL|MF_TRY1NULL) ||
	    map.map_spacesub != SpaceSub ||
	    map.map_app != NULL ||
	    map.map_tapp != NULL)
	{
		syserr("readcf: option LDAPDefaultSpec: Do not set non-LDAP specific flags");
		SM_FREE_CLR(map.map_app);
		SM_FREE_CLR(map.map_tapp);
	}

	if (LDAPDefaults->ldap_filter != NULL)
	{
		syserr("readcf: option LDAPDefaultSpec: Do not set the LDAP search filter");

		/* don't free, it isn't malloc'ed in parseargs */
		LDAPDefaults->ldap_filter = NULL;
	}

	if (LDAPDefaults->ldap_attr[0] != NULL)
	{
		syserr("readcf: option LDAPDefaultSpec: Do not set the requested LDAP attributes");
		/* don't free, they aren't malloc'ed in parseargs */
		LDAPDefaults->ldap_attr[0] = NULL;
	}
}
#endif /* LDAPMAP */
/*
**  PH map
*/

#if PH_MAP

/*
**  Support for the CCSO Nameserver (ph/qi).
**  This code is intended to replace the so-called "ph mailer".
**  Contributed by Mark D. Roth.  Contact him for support.
*/

/* what version of the ph map code we're running */
static char phmap_id[128];

/* sendmail version for phmap id string */
extern const char Version[];

/* assume we're using nph-1.2.x if not specified */
# ifndef NPH_VERSION
#  define NPH_VERSION		10200
# endif

/* compatibility for versions older than nph-1.2.0 */
# if NPH_VERSION < 10200
#  define PH_OPEN_ROUNDROBIN	PH_ROUNDROBIN
#  define PH_OPEN_DONTID	PH_DONTID
#  define PH_CLOSE_FAST		PH_FASTCLOSE
#  define PH_ERR_DATAERR	PH_DATAERR
#  define PH_ERR_NOMATCH	PH_NOMATCH
# endif /* NPH_VERSION < 10200 */

/*
**  PH_MAP_PARSEARGS -- parse ph map definition args.
*/

bool
ph_map_parseargs(map, args)
	MAP *map;
	char *args;
{
	register bool done;
	register char *p = args;
	PH_MAP_STRUCT *pmap = NULL;

	/* initialize version string */
	(void) sm_snprintf(phmap_id, sizeof(phmap_id),
			   "sendmail-%s phmap-20010529 libphclient-%s",
			   Version, libphclient_version);

	pmap = (PH_MAP_STRUCT *) xalloc(sizeof(*pmap));

	/* defaults */
	pmap->ph_servers = NULL;
	pmap->ph_field_list = NULL;
	pmap->ph = NULL;
	pmap->ph_timeout = 0;
	pmap->ph_fastclose = 0;

	map->map_mflags |= MF_TRY0NULL|MF_TRY1NULL;
	for (;;)
	{
		while (isascii(*p) && isspace(*p))
			p++;
		if (*p != '-')
			break;
		switch (*++p)
		{
		  case 'N':
			map->map_mflags |= MF_INCLNULL;
			map->map_mflags &= ~MF_TRY0NULL;
			break;

		  case 'O':
			map->map_mflags &= ~MF_TRY1NULL;
			break;

		  case 'o':
			map->map_mflags |= MF_OPTIONAL;
			break;

		  case 'f':
			map->map_mflags |= MF_NOFOLDCASE;
			break;

		  case 'm':
			map->map_mflags |= MF_MATCHONLY;
			break;

		  case 'A':
			map->map_mflags |= MF_APPEND;
			break;

		  case 'q':
			map->map_mflags |= MF_KEEPQUOTES;
			break;

		  case 't':
			map->map_mflags |= MF_NODEFER;
			break;

		  case 'a':
			map->map_app = ++p;
			break;

		  case 'T':
			map->map_tapp = ++p;
			break;

		  case 'l':
			while (isascii(*++p) && isspace(*p))
				continue;
			pmap->ph_timeout = atoi(p);
			break;

		  case 'S':
			map->map_spacesub = *++p;
			break;

		  case 'D':
			map->map_mflags |= MF_DEFER;
			break;

		  case 'h':		/* PH server list */
			while (isascii(*++p) && isspace(*p))
				continue;
			pmap->ph_servers = p;
			break;

		  case 'k':		/* fields to search for */
			while (isascii(*++p) && isspace(*p))
				continue;
			pmap->ph_field_list = p;
			break;

		  default:
			syserr("ph_map_parseargs: unknown option -%c", *p);
		}

		/* try to account for quoted strings */
		done = isascii(*p) && isspace(*p);
		while (*p != '\0' && !done)
		{
			if (*p == '"')
			{
				while (*++p != '"' && *p != '\0')
					continue;
				if (*p != '\0')
					p++;
			}
			else
				p++;
			done = isascii(*p) && isspace(*p);
		}

		if (*p != '\0')
			*p++ = '\0';
	}

	if (map->map_app != NULL)
		map->map_app = newstr(ph_map_dequote(map->map_app));
	if (map->map_tapp != NULL)
		map->map_tapp = newstr(ph_map_dequote(map->map_tapp));

	if (pmap->ph_field_list != NULL)
		pmap->ph_field_list = newstr(ph_map_dequote(pmap->ph_field_list));

	if (pmap->ph_servers != NULL)
		pmap->ph_servers = newstr(ph_map_dequote(pmap->ph_servers));
	else
	{
		syserr("ph_map_parseargs: -h flag is required");
		return false;
	}

	map->map_db1 = (ARBPTR_T) pmap;
	return true;
}

/*
**  PH_MAP_CLOSE -- close the connection to the ph server
*/

void
ph_map_close(map)
	MAP *map;
{
	PH_MAP_STRUCT *pmap;

	pmap = (PH_MAP_STRUCT *)map->map_db1;
	if (tTd(38, 9))
		sm_dprintf("ph_map_close(%s): pmap->ph_fastclose=%d\n",
			   map->map_mname, pmap->ph_fastclose);


	if (pmap->ph != NULL)
	{
		ph_set_sendhook(pmap->ph, NULL);
		ph_set_recvhook(pmap->ph, NULL);
		ph_close(pmap->ph, pmap->ph_fastclose);
	}

	map->map_mflags &= ~(MF_OPEN|MF_WRITABLE);
}

static jmp_buf  PHTimeout;

/* ARGSUSED */
static void
ph_timeout(unused)
	int unused;
{
	/*
	**  NOTE: THIS CAN BE CALLED FROM A SIGNAL HANDLER.  DO NOT ADD
	**	ANYTHING TO THIS ROUTINE UNLESS YOU KNOW WHAT YOU ARE
	**	DOING.
	*/

	errno = ETIMEDOUT;
	longjmp(PHTimeout, 1);
}

static void
#if NPH_VERSION >= 10200
ph_map_send_debug(appdata, text)
	void *appdata;
#else
ph_map_send_debug(text)
#endif
	char *text;
{
	if (LogLevel > 9)
		sm_syslog(LOG_NOTICE, CurEnv->e_id,
			  "ph_map_send_debug: ==> %s", text);
	if (tTd(38, 20))
		sm_dprintf("ph_map_send_debug: ==> %s\n", text);
}

static void
#if NPH_VERSION >= 10200
ph_map_recv_debug(appdata, text)
	void *appdata;
#else
ph_map_recv_debug(text)
#endif
	char *text;
{
	if (LogLevel > 10)
		sm_syslog(LOG_NOTICE, CurEnv->e_id,
			  "ph_map_recv_debug: <== %s", text);
	if (tTd(38, 21))
		sm_dprintf("ph_map_recv_debug: <== %s\n", text);
}

/*
**  PH_MAP_OPEN -- sub for opening PH map
*/
bool
ph_map_open(map, mode)
	MAP *map;
	int mode;
{
	PH_MAP_STRUCT *pmap;
	register SM_EVENT *ev = NULL;
	int save_errno = 0;
	char *hostlist, *host;

	if (tTd(38, 2))
		sm_dprintf("ph_map_open(%s)\n", map->map_mname);

	mode &= O_ACCMODE;
	if (mode != O_RDONLY)
	{
		/* issue a pseudo-error message */
		errno = SM_EMAPCANTWRITE;
		return false;
	}

	if (CurEnv != NULL && CurEnv->e_sendmode == SM_DEFER &&
	    bitset(MF_DEFER, map->map_mflags))
	{
		if (tTd(9, 1))
			sm_dprintf("ph_map_open(%s) => DEFERRED\n",
				   map->map_mname);

		/*
		**  Unset MF_DEFER here so that map_lookup() returns
		**  a temporary failure using the bogus map and
		**  map->map_tapp instead of the default permanent error.
		*/

		map->map_mflags &= ~MF_DEFER;
		return false;
	}

	pmap = (PH_MAP_STRUCT *)map->map_db1;
	pmap->ph_fastclose = 0;		/* refresh field for reopen */

	/* try each host in the list */
	hostlist = newstr(pmap->ph_servers);
	for (host = strtok(hostlist, " ");
	     host != NULL;
	     host = strtok(NULL, " "))
	{
		/* set timeout */
		if (pmap->ph_timeout != 0)
		{
			if (setjmp(PHTimeout) != 0)
			{
				ev = NULL;
				if (LogLevel > 1)
					sm_syslog(LOG_NOTICE, CurEnv->e_id,
						  "timeout connecting to PH server %.100s",
						  host);
				errno = ETIMEDOUT;
				goto ph_map_open_abort;
			}
			ev = sm_setevent(pmap->ph_timeout, ph_timeout, 0);
		}

		/* open connection to server */
		if (ph_open(&(pmap->ph), host,
			    PH_OPEN_ROUNDROBIN|PH_OPEN_DONTID,
			    ph_map_send_debug, ph_map_recv_debug
#if NPH_VERSION >= 10200
			    , NULL
#endif
			    ) == 0
		    && ph_id(pmap->ph, phmap_id) == 0)
		{
			if (ev != NULL)
				sm_clrevent(ev);
			sm_free(hostlist); /* XXX */
			return true;
		}

  ph_map_open_abort:
		save_errno = errno;
		if (ev != NULL)
			sm_clrevent(ev);
		pmap->ph_fastclose = PH_CLOSE_FAST;
		ph_map_close(map);
		errno = save_errno;
	}

	if (bitset(MF_NODEFER, map->map_mflags))
	{
		if (errno == 0)
			errno = EAGAIN;
		syserr("ph_map_open: %s: cannot connect to PH server",
		       map->map_mname);
	}
	else if (!bitset(MF_OPTIONAL, map->map_mflags) && LogLevel > 1)
		sm_syslog(LOG_NOTICE, CurEnv->e_id,
			  "ph_map_open: %s: cannot connect to PH server",
			  map->map_mname);
	sm_free(hostlist); /* XXX */
	return false;
}

/*
**  PH_MAP_LOOKUP -- look up key from ph server
*/

char *
ph_map_lookup(map, key, args, pstat)
	MAP *map;
	char *key;
	char **args;
	int *pstat;
{
	int i, save_errno = 0;
	register SM_EVENT *ev = NULL;
	PH_MAP_STRUCT *pmap;
	char *value = NULL;

	pmap = (PH_MAP_STRUCT *)map->map_db1;

	*pstat = EX_OK;

	/* set timeout */
	if (pmap->ph_timeout != 0)
	{
		if (setjmp(PHTimeout) != 0)
		{
			ev = NULL;
			if (LogLevel > 1)
				sm_syslog(LOG_NOTICE, CurEnv->e_id,
					  "timeout during PH lookup of %.100s",
					  key);
			errno = ETIMEDOUT;
			*pstat = EX_TEMPFAIL;
			goto ph_map_lookup_abort;
		}
		ev = sm_setevent(pmap->ph_timeout, ph_timeout, 0);
	}

	/* perform lookup */
	i = ph_email_resolve(pmap->ph, key, pmap->ph_field_list, &value);
	if (i == -1)
		*pstat = EX_TEMPFAIL;
	else if (i == PH_ERR_NOMATCH || i == PH_ERR_DATAERR)
		*pstat = EX_UNAVAILABLE;

  ph_map_lookup_abort:
	if (ev != NULL)
		sm_clrevent(ev);

	/*
	**  Close the connection if the timer popped
	**  or we got a temporary PH error
	*/

	if (*pstat == EX_TEMPFAIL)
	{
		save_errno = errno;
		pmap->ph_fastclose = PH_CLOSE_FAST;
		ph_map_close(map);
		errno = save_errno;
	}

	if (*pstat == EX_OK)
	{
		if (tTd(38,20))
			sm_dprintf("ph_map_lookup: %s => %s\n", key, value);

		if (bitset(MF_MATCHONLY, map->map_mflags))
			return map_rewrite(map, key, strlen(key), NULL);
		else
			return map_rewrite(map, value, strlen(value), args);
	}

	return NULL;
}
#endif /* PH_MAP */

/*
**  syslog map
*/

#define map_prio	map_lockfd	/* overload field */

/*
**  SYSLOG_MAP_PARSEARGS -- check for priority level to syslog messages.
*/

bool
syslog_map_parseargs(map, args)
	MAP *map;
	char *args;
{
	char *p = args;
	char *priority = NULL;

	/* there is no check whether there is really an argument */
	while (*p != '\0')
	{
		while (isascii(*p) && isspace(*p))
			p++;
		if (*p != '-')
			break;
		++p;
		if (*p == 'D')
		{
			map->map_mflags |= MF_DEFER;
			++p;
		}
		else if (*p == 'S')
		{
			map->map_spacesub = *++p;
			if (*p != '\0')
				p++;
		}
		else if (*p == 'L')
		{
			while (*++p != '\0' && isascii(*p) && isspace(*p))
				continue;
			if (*p == '\0')
				break;
			priority = p;
			while (*p != '\0' && !(isascii(*p) && isspace(*p)))
				p++;
			if (*p != '\0')
				*p++ = '\0';
		}
		else
		{
			syserr("Illegal option %c map syslog", *p);
			++p;
		}
	}

	if (priority == NULL)
		map->map_prio = LOG_INFO;
	else
	{
		if (sm_strncasecmp("LOG_", priority, 4) == 0)
			priority += 4;

#ifdef LOG_EMERG
		if (sm_strcasecmp("EMERG", priority) == 0)
			map->map_prio = LOG_EMERG;
		else
#endif /* LOG_EMERG */
#ifdef LOG_ALERT
		if (sm_strcasecmp("ALERT", priority) == 0)
			map->map_prio = LOG_ALERT;
		else
#endif /* LOG_ALERT */
#ifdef LOG_CRIT
		if (sm_strcasecmp("CRIT", priority) == 0)
			map->map_prio = LOG_CRIT;
		else
#endif /* LOG_CRIT */
#ifdef LOG_ERR
		if (sm_strcasecmp("ERR", priority) == 0)
			map->map_prio = LOG_ERR;
		else
#endif /* LOG_ERR */
#ifdef LOG_WARNING
		if (sm_strcasecmp("WARNING", priority) == 0)
			map->map_prio = LOG_WARNING;
		else
#endif /* LOG_WARNING */
#ifdef LOG_NOTICE
		if (sm_strcasecmp("NOTICE", priority) == 0)
			map->map_prio = LOG_NOTICE;
		else
#endif /* LOG_NOTICE */
#ifdef LOG_INFO
		if (sm_strcasecmp("INFO", priority) == 0)
			map->map_prio = LOG_INFO;
		else
#endif /* LOG_INFO */
#ifdef LOG_DEBUG
		if (sm_strcasecmp("DEBUG", priority) == 0)
			map->map_prio = LOG_DEBUG;
		else
#endif /* LOG_DEBUG */
		{
			syserr("syslog_map_parseargs: Unknown priority %s",
			       priority);
			return false;
		}
	}
	return true;
}

/*
**  SYSLOG_MAP_LOOKUP -- rewrite and syslog message.  Always return empty string
*/

char *
syslog_map_lookup(map, string, args, statp)
	MAP *map;
	char *string;
	char **args;
	int *statp;
{
	char *ptr = map_rewrite(map, string, strlen(string), args);

	if (ptr != NULL)
	{
		if (tTd(38, 20))
			sm_dprintf("syslog_map_lookup(%s (priority %d): %s\n",
				map->map_mname, map->map_prio, ptr);

		sm_syslog(map->map_prio, CurEnv->e_id, "%s", ptr);
	}

	*statp = EX_OK;
	return "";
}

#if _FFR_DPRINTF_MAP
/*
**  dprintf map
*/

#define map_dbg_level	map_lockfd	/* overload field */

/*
**  DPRINTF_MAP_PARSEARGS -- check for priority level to dprintf messages.
*/

bool
dprintf_map_parseargs(map, args)
	MAP *map;
	char *args;
{
	char *p = args;
	char *dbg_level = NULL;

	/* there is no check whether there is really an argument */
	while (*p != '\0')
	{
		while (isascii(*p) && isspace(*p))
			p++;
		if (*p != '-')
			break;
		++p;
		if (*p == 'D')
		{
			map->map_mflags |= MF_DEFER;
			++p;
		}
		else if (*p == 'S')
		{
			map->map_spacesub = *++p;
			if (*p != '\0')
				p++;
		}
		else if (*p == 'd')
		{
			while (*++p != '\0' && isascii(*p) && isspace(*p))
				continue;
			if (*p == '\0')
				break;
			dbg_level = p;
			while (*p != '\0' && !(isascii(*p) && isspace(*p)))
				p++;
			if (*p != '\0')
				*p++ = '\0';
		}
		else
		{
			syserr("Illegal option %c map dprintf", *p);
			++p;
		}
	}

	if (dbg_level == NULL)
		map->map_dbg_level = 0;
	else
	{
		if (!(isascii(*dbg_level) && isdigit(*dbg_level)))
		{
			syserr("dprintf map \"%s\", file %s: -d should specify a number, not %s",
				map->map_mname, map->map_file,
				dbg_level);
			return false;
		}
		map->map_dbg_level = atoi(dbg_level);
	}
	return true;
}

/*
**  DPRINTF_MAP_LOOKUP -- rewrite and print message.  Always return empty string
*/

char *
dprintf_map_lookup(map, string, args, statp)
	MAP *map;
	char *string;
	char **args;
	int *statp;
{
	char *ptr = map_rewrite(map, string, strlen(string), args);

	if (ptr != NULL && tTd(85, map->map_dbg_level))
		sm_dprintf("%s\n", ptr);
	*statp = EX_OK;
	return "";
}
#endif /* _FFR_DPRINTF_MAP */

/*
**  HESIOD Modules
*/

#if HESIOD

bool
hes_map_open(map, mode)
	MAP *map;
	int mode;
{
	if (tTd(38, 2))
		sm_dprintf("hes_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);

	if (mode != O_RDONLY)
	{
		/* issue a pseudo-error message */
		errno = SM_EMAPCANTWRITE;
		return false;
	}

# ifdef HESIOD_INIT
	if (HesiodContext != NULL || hesiod_init(&HesiodContext) == 0)
		return true;

	if (!bitset(MF_OPTIONAL, map->map_mflags))
		syserr("451 4.3.5 cannot initialize Hesiod map (%s)",
			sm_errstring(errno));
	return false;
# else /* HESIOD_INIT */
	if (hes_error() == HES_ER_UNINIT)
		hes_init();
	switch (hes_error())
	{
	  case HES_ER_OK:
	  case HES_ER_NOTFOUND:
		return true;
	}

	if (!bitset(MF_OPTIONAL, map->map_mflags))
		syserr("451 4.3.5 cannot initialize Hesiod map (%d)", hes_error());

	return false;
# endif /* HESIOD_INIT */
}

char *
hes_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	char **hp;

	if (tTd(38, 20))
		sm_dprintf("hes_map_lookup(%s, %s)\n", map->map_file, name);

	if (name[0] == '\\')
	{
		char *np;
		int nl;
		int save_errno;
		char nbuf[MAXNAME];

		nl = strlen(name);
		if (nl < sizeof(nbuf) - 1)
			np = nbuf;
		else
			np = xalloc(strlen(name) + 2);
		np[0] = '\\';
		(void) sm_strlcpy(&np[1], name, (sizeof(nbuf)) - 1);
# ifdef HESIOD_INIT
		hp = hesiod_resolve(HesiodContext, np, map->map_file);
# else /* HESIOD_INIT */
		hp = hes_resolve(np, map->map_file);
# endif /* HESIOD_INIT */
		save_errno = errno;
		if (np != nbuf)
			sm_free(np); /* XXX */
		errno = save_errno;
	}
	else
	{
# ifdef HESIOD_INIT
		hp = hesiod_resolve(HesiodContext, name, map->map_file);
# else /* HESIOD_INIT */
		hp = hes_resolve(name, map->map_file);
# endif /* HESIOD_INIT */
	}
# ifdef HESIOD_INIT
	if (hp == NULL || *hp == NULL)
	{
		switch (errno)
		{
		  case ENOENT:
			  *statp = EX_NOTFOUND;
			  break;
		  case ECONNREFUSED:
			  *statp = EX_TEMPFAIL;
			  break;
		  case EMSGSIZE:
		  case ENOMEM:
		  default:
			  *statp = EX_UNAVAILABLE;
			  break;
		}
		if (hp != NULL)
			hesiod_free_list(HesiodContext, hp);
		return NULL;
	}
# else /* HESIOD_INIT */
	if (hp == NULL || hp[0] == NULL)
	{
		switch (hes_error())
		{
		  case HES_ER_OK:
			*statp = EX_OK;
			break;

		  case HES_ER_NOTFOUND:
			*statp = EX_NOTFOUND;
			break;

		  case HES_ER_CONFIG:
			*statp = EX_UNAVAILABLE;
			break;

		  case HES_ER_NET:
			*statp = EX_TEMPFAIL;
			break;
		}
		return NULL;
	}
# endif /* HESIOD_INIT */

	if (bitset(MF_MATCHONLY, map->map_mflags))
		return map_rewrite(map, name, strlen(name), NULL);
	else
		return map_rewrite(map, hp[0], strlen(hp[0]), av);
}

/*
**  HES_MAP_CLOSE -- free the Hesiod context
*/

void
hes_map_close(map)
	MAP *map;
{
	if (tTd(38, 20))
		sm_dprintf("hes_map_close(%s)\n", map->map_file);

# ifdef HESIOD_INIT
	/* Free the hesiod context */
	if (HesiodContext != NULL)
	{
		hesiod_end(HesiodContext);
		HesiodContext = NULL;
	}
# endif /* HESIOD_INIT */
}

#endif /* HESIOD */
/*
**  NeXT NETINFO Modules
*/

#if NETINFO

# define NETINFO_DEFAULT_DIR		"/aliases"
# define NETINFO_DEFAULT_PROPERTY	"members"

/*
**  NI_MAP_OPEN -- open NetInfo Aliases
*/

bool
ni_map_open(map, mode)
	MAP *map;
	int mode;
{
	if (tTd(38, 2))
		sm_dprintf("ni_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);
	mode &= O_ACCMODE;

	if (*map->map_file == '\0')
		map->map_file = NETINFO_DEFAULT_DIR;

	if (map->map_valcolnm == NULL)
		map->map_valcolnm = NETINFO_DEFAULT_PROPERTY;

	if (map->map_coldelim == '\0')
	{
		if (bitset(MF_ALIAS, map->map_mflags))
			map->map_coldelim = ',';
		else if (bitset(MF_FILECLASS, map->map_mflags))
			map->map_coldelim = ' ';
	}
	return true;
}


/*
**  NI_MAP_LOOKUP -- look up a datum in NetInfo
*/

char *
ni_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	char *res;
	char *propval;

	if (tTd(38, 20))
		sm_dprintf("ni_map_lookup(%s, %s)\n", map->map_mname, name);

	propval = ni_propval(map->map_file, map->map_keycolnm, name,
			     map->map_valcolnm, map->map_coldelim);

	if (propval == NULL)
		return NULL;

	SM_TRY
		if (bitset(MF_MATCHONLY, map->map_mflags))
			res = map_rewrite(map, name, strlen(name), NULL);
		else
			res = map_rewrite(map, propval, strlen(propval), av);
	SM_FINALLY
		sm_free(propval);
	SM_END_TRY
	return res;
}


static bool
ni_getcanonname(name, hbsize, statp)
	char *name;
	int hbsize;
	int *statp;
{
	char *vptr;
	char *ptr;
	char nbuf[MAXNAME + 1];

	if (tTd(38, 20))
		sm_dprintf("ni_getcanonname(%s)\n", name);

	if (sm_strlcpy(nbuf, name, sizeof(nbuf)) >= sizeof(nbuf))
	{
		*statp = EX_UNAVAILABLE;
		return false;
	}
	(void) shorten_hostname(nbuf);

	/* we only accept single token search key */
	if (strchr(nbuf, '.'))
	{
		*statp = EX_NOHOST;
		return false;
	}

	/* Do the search */
	vptr = ni_propval("/machines", NULL, nbuf, "name", '\n');

	if (vptr == NULL)
	{
		*statp = EX_NOHOST;
		return false;
	}

	/* Only want the first machine name */
	if ((ptr = strchr(vptr, '\n')) != NULL)
		*ptr = '\0';

	if (sm_strlcpy(name, vptr, hbsize) >= hbsize)
	{
		sm_free(vptr);
		*statp = EX_UNAVAILABLE;
		return true;
	}
	sm_free(vptr);
	*statp = EX_OK;
	return false;
}
#endif /* NETINFO */
/*
**  TEXT (unindexed text file) Modules
**
**	This code donated by Sun Microsystems.
*/

#define map_sff		map_lockfd	/* overload field */


/*
**  TEXT_MAP_OPEN -- open text table
*/

bool
text_map_open(map, mode)
	MAP *map;
	int mode;
{
	long sff;
	int i;

	if (tTd(38, 2))
		sm_dprintf("text_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);

	mode &= O_ACCMODE;
	if (mode != O_RDONLY)
	{
		errno = EPERM;
		return false;
	}

	if (*map->map_file == '\0')
	{
		syserr("text map \"%s\": file name required",
			map->map_mname);
		return false;
	}

	if (map->map_file[0] != '/')
	{
		syserr("text map \"%s\": file name must be fully qualified",
			map->map_mname);
		return false;
	}

	sff = SFF_ROOTOK|SFF_REGONLY;
	if (!bitnset(DBS_LINKEDMAPINWRITABLEDIR, DontBlameSendmail))
		sff |= SFF_NOWLINK;
	if (!bitnset(DBS_MAPINUNSAFEDIRPATH, DontBlameSendmail))
		sff |= SFF_SAFEDIRPATH;
	if ((i = safefile(map->map_file, RunAsUid, RunAsGid, RunAsUserName,
			  sff, S_IRUSR, NULL)) != 0)
	{
		int save_errno = errno;

		/* cannot open this map */
		if (tTd(38, 2))
			sm_dprintf("\tunsafe map file: %d\n", i);
		errno = save_errno;
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("text map \"%s\": unsafe map file %s",
				map->map_mname, map->map_file);
		return false;
	}

	if (map->map_keycolnm == NULL)
		map->map_keycolno = 0;
	else
	{
		if (!(isascii(*map->map_keycolnm) && isdigit(*map->map_keycolnm)))
		{
			syserr("text map \"%s\", file %s: -k should specify a number, not %s",
				map->map_mname, map->map_file,
				map->map_keycolnm);
			return false;
		}
		map->map_keycolno = atoi(map->map_keycolnm);
	}

	if (map->map_valcolnm == NULL)
		map->map_valcolno = 0;
	else
	{
		if (!(isascii(*map->map_valcolnm) && isdigit(*map->map_valcolnm)))
		{
			syserr("text map \"%s\", file %s: -v should specify a number, not %s",
					map->map_mname, map->map_file,
					map->map_valcolnm);
			return false;
		}
		map->map_valcolno = atoi(map->map_valcolnm);
	}

	if (tTd(38, 2))
	{
		sm_dprintf("text_map_open(%s, %s): delimiter = ",
			map->map_mname, map->map_file);
		if (map->map_coldelim == '\0')
			sm_dprintf("(white space)\n");
		else
			sm_dprintf("%c\n", map->map_coldelim);
	}

	map->map_sff = sff;
	return true;
}


/*
**  TEXT_MAP_LOOKUP -- look up a datum in a TEXT table
*/

char *
text_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	char *vp;
	auto int vsize;
	int buflen;
	SM_FILE_T *f;
	char delim;
	int key_idx;
	bool found_it;
	long sff = map->map_sff;
	char search_key[MAXNAME + 1];
	char linebuf[MAXLINE];
	char buf[MAXNAME + 1];

	found_it = false;
	if (tTd(38, 20))
		sm_dprintf("text_map_lookup(%s, %s)\n", map->map_mname,  name);

	buflen = strlen(name);
	if (buflen > sizeof(search_key) - 1)
		buflen = sizeof(search_key) - 1;	/* XXX just cut if off? */
	memmove(search_key, name, buflen);
	search_key[buflen] = '\0';
	if (!bitset(MF_NOFOLDCASE, map->map_mflags))
		makelower(search_key);

	f = safefopen(map->map_file, O_RDONLY, FileMode, sff);
	if (f == NULL)
	{
		map->map_mflags &= ~(MF_VALID|MF_OPEN);
		*statp = EX_UNAVAILABLE;
		return NULL;
	}
	key_idx = map->map_keycolno;
	delim = map->map_coldelim;
	while (sm_io_fgets(f, SM_TIME_DEFAULT,
			   linebuf, sizeof(linebuf)) >= 0)
	{
		char *p;

		/* skip comment line */
		if (linebuf[0] == '#')
			continue;
		p = strchr(linebuf, '\n');
		if (p != NULL)
			*p = '\0';
		p = get_column(linebuf, key_idx, delim, buf, sizeof(buf));
		if (p != NULL && sm_strcasecmp(search_key, p) == 0)
		{
			found_it = true;
			break;
		}
	}
	(void) sm_io_close(f, SM_TIME_DEFAULT);
	if (!found_it)
	{
		*statp = EX_NOTFOUND;
		return NULL;
	}
	vp = get_column(linebuf, map->map_valcolno, delim, buf, sizeof(buf));
	if (vp == NULL)
	{
		*statp = EX_NOTFOUND;
		return NULL;
	}
	vsize = strlen(vp);
	*statp = EX_OK;
	if (bitset(MF_MATCHONLY, map->map_mflags))
		return map_rewrite(map, name, strlen(name), NULL);
	else
		return map_rewrite(map, vp, vsize, av);
}

/*
**  TEXT_GETCANONNAME -- look up canonical name in hosts file
*/

static bool
text_getcanonname(name, hbsize, statp)
	char *name;
	int hbsize;
	int *statp;
{
	bool found;
	char *dot;
	SM_FILE_T *f;
	char linebuf[MAXLINE];
	char cbuf[MAXNAME + 1];
	char nbuf[MAXNAME + 1];

	if (tTd(38, 20))
		sm_dprintf("text_getcanonname(%s)\n", name);

	if (sm_strlcpy(nbuf, name, sizeof(nbuf)) >= sizeof(nbuf))
	{
		*statp = EX_UNAVAILABLE;
		return false;
	}
	dot = shorten_hostname(nbuf);

	f = sm_io_open(SmFtStdio, SM_TIME_DEFAULT, HostsFile, SM_IO_RDONLY,
		       NULL);
	if (f == NULL)
	{
		*statp = EX_UNAVAILABLE;
		return false;
	}
	found = false;
	while (!found &&
		sm_io_fgets(f, SM_TIME_DEFAULT,
			    linebuf, sizeof(linebuf)) >= 0)
	{
		char *p = strpbrk(linebuf, "#\n");

		if (p != NULL)
			*p = '\0';
		if (linebuf[0] != '\0')
			found = extract_canonname(nbuf, dot, linebuf,
						  cbuf, sizeof(cbuf));
	}
	(void) sm_io_close(f, SM_TIME_DEFAULT);
	if (!found)
	{
		*statp = EX_NOHOST;
		return false;
	}

	if (sm_strlcpy(name, cbuf, hbsize) >= hbsize)
	{
		*statp = EX_UNAVAILABLE;
		return false;
	}
	*statp = EX_OK;
	return true;
}
/*
**  STAB (Symbol Table) Modules
*/


/*
**  STAB_MAP_LOOKUP -- look up alias in symbol table
*/

/* ARGSUSED2 */
char *
stab_map_lookup(map, name, av, pstat)
	register MAP *map;
	char *name;
	char **av;
	int *pstat;
{
	register STAB *s;

	if (tTd(38, 20))
		sm_dprintf("stab_lookup(%s, %s)\n",
			map->map_mname, name);

	s = stab(name, ST_ALIAS, ST_FIND);
	if (s == NULL)
		return NULL;
	if (bitset(MF_MATCHONLY, map->map_mflags))
		return map_rewrite(map, name, strlen(name), NULL);
	else
		return map_rewrite(map, s->s_alias, strlen(s->s_alias), av);
}

/*
**  STAB_MAP_STORE -- store in symtab (actually using during init, not rebuild)
*/

void
stab_map_store(map, lhs, rhs)
	register MAP *map;
	char *lhs;
	char *rhs;
{
	register STAB *s;

	s = stab(lhs, ST_ALIAS, ST_ENTER);
	s->s_alias = newstr(rhs);
}


/*
**  STAB_MAP_OPEN -- initialize (reads data file)
**
**	This is a weird case -- it is only intended as a fallback for
**	aliases.  For this reason, opens for write (only during a
**	"newaliases") always fails, and opens for read open the
**	actual underlying text file instead of the database.
*/

bool
stab_map_open(map, mode)
	register MAP *map;
	int mode;
{
	SM_FILE_T *af;
	long sff;
	struct stat st;

	if (tTd(38, 2))
		sm_dprintf("stab_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);

	mode &= O_ACCMODE;
	if (mode != O_RDONLY)
	{
		errno = EPERM;
		return false;
	}

	sff = SFF_ROOTOK|SFF_REGONLY;
	if (!bitnset(DBS_LINKEDMAPINWRITABLEDIR, DontBlameSendmail))
		sff |= SFF_NOWLINK;
	if (!bitnset(DBS_MAPINUNSAFEDIRPATH, DontBlameSendmail))
		sff |= SFF_SAFEDIRPATH;
	af = safefopen(map->map_file, O_RDONLY, 0444, sff);
	if (af == NULL)
		return false;
	readaliases(map, af, false, false);

	if (fstat(sm_io_getinfo(af, SM_IO_WHAT_FD, NULL), &st) >= 0)
		map->map_mtime = st.st_mtime;
	(void) sm_io_close(af, SM_TIME_DEFAULT);

	return true;
}
/*
**  Implicit Modules
**
**	Tries several types.  For back compatibility of aliases.
*/


/*
**  IMPL_MAP_LOOKUP -- lookup in best open database
*/

char *
impl_map_lookup(map, name, av, pstat)
	MAP *map;
	char *name;
	char **av;
	int *pstat;
{
	if (tTd(38, 20))
		sm_dprintf("impl_map_lookup(%s, %s)\n",
			map->map_mname, name);

#if NEWDB
	if (bitset(MF_IMPL_HASH, map->map_mflags))
		return db_map_lookup(map, name, av, pstat);
#endif /* NEWDB */
#if NDBM
	if (bitset(MF_IMPL_NDBM, map->map_mflags))
		return ndbm_map_lookup(map, name, av, pstat);
#endif /* NDBM */
	return stab_map_lookup(map, name, av, pstat);
}

/*
**  IMPL_MAP_STORE -- store in open databases
*/

void
impl_map_store(map, lhs, rhs)
	MAP *map;
	char *lhs;
	char *rhs;
{
	if (tTd(38, 12))
		sm_dprintf("impl_map_store(%s, %s, %s)\n",
			map->map_mname, lhs, rhs);
#if NEWDB
	if (bitset(MF_IMPL_HASH, map->map_mflags))
		db_map_store(map, lhs, rhs);
#endif /* NEWDB */
#if NDBM
	if (bitset(MF_IMPL_NDBM, map->map_mflags))
		ndbm_map_store(map, lhs, rhs);
#endif /* NDBM */
	stab_map_store(map, lhs, rhs);
}

/*
**  IMPL_MAP_OPEN -- implicit database open
*/

bool
impl_map_open(map, mode)
	MAP *map;
	int mode;
{
	if (tTd(38, 2))
		sm_dprintf("impl_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);

	mode &= O_ACCMODE;
#if NEWDB
	map->map_mflags |= MF_IMPL_HASH;
	if (hash_map_open(map, mode))
	{
# ifdef NDBM_YP_COMPAT
		if (mode == O_RDONLY || strstr(map->map_file, "/yp/") == NULL)
# endif /* NDBM_YP_COMPAT */
			return true;
	}
	else
		map->map_mflags &= ~MF_IMPL_HASH;
#endif /* NEWDB */
#if NDBM
	map->map_mflags |= MF_IMPL_NDBM;
	if (ndbm_map_open(map, mode))
	{
		return true;
	}
	else
		map->map_mflags &= ~MF_IMPL_NDBM;
#endif /* NDBM */

#if defined(NEWDB) || defined(NDBM)
	if (Verbose)
		message("WARNING: cannot open alias database %s%s",
			map->map_file,
			mode == O_RDONLY ? "; reading text version" : "");
#else /* defined(NEWDB) || defined(NDBM) */
	if (mode != O_RDONLY)
		usrerr("Cannot rebuild aliases: no database format defined");
#endif /* defined(NEWDB) || defined(NDBM) */

	if (mode == O_RDONLY)
		return stab_map_open(map, mode);
	else
		return false;
}


/*
**  IMPL_MAP_CLOSE -- close any open database(s)
*/

void
impl_map_close(map)
	MAP *map;
{
	if (tTd(38, 9))
		sm_dprintf("impl_map_close(%s, %s, %lx)\n",
			map->map_mname, map->map_file, map->map_mflags);
#if NEWDB
	if (bitset(MF_IMPL_HASH, map->map_mflags))
	{
		db_map_close(map);
		map->map_mflags &= ~MF_IMPL_HASH;
	}
#endif /* NEWDB */

#if NDBM
	if (bitset(MF_IMPL_NDBM, map->map_mflags))
	{
		ndbm_map_close(map);
		map->map_mflags &= ~MF_IMPL_NDBM;
	}
#endif /* NDBM */
}
/*
**  User map class.
**
**	Provides access to the system password file.
*/

/*
**  USER_MAP_OPEN -- open user map
**
**	Really just binds field names to field numbers.
*/

bool
user_map_open(map, mode)
	MAP *map;
	int mode;
{
	if (tTd(38, 2))
		sm_dprintf("user_map_open(%s, %d)\n",
			map->map_mname, mode);

	mode &= O_ACCMODE;
	if (mode != O_RDONLY)
	{
		/* issue a pseudo-error message */
		errno = SM_EMAPCANTWRITE;
		return false;
	}
	if (map->map_valcolnm == NULL)
		/* EMPTY */
		/* nothing */ ;
	else if (sm_strcasecmp(map->map_valcolnm, "name") == 0)
		map->map_valcolno = 1;
	else if (sm_strcasecmp(map->map_valcolnm, "passwd") == 0)
		map->map_valcolno = 2;
	else if (sm_strcasecmp(map->map_valcolnm, "uid") == 0)
		map->map_valcolno = 3;
	else if (sm_strcasecmp(map->map_valcolnm, "gid") == 0)
		map->map_valcolno = 4;
	else if (sm_strcasecmp(map->map_valcolnm, "gecos") == 0)
		map->map_valcolno = 5;
	else if (sm_strcasecmp(map->map_valcolnm, "dir") == 0)
		map->map_valcolno = 6;
	else if (sm_strcasecmp(map->map_valcolnm, "shell") == 0)
		map->map_valcolno = 7;
	else
	{
		syserr("User map %s: unknown column name %s",
			map->map_mname, map->map_valcolnm);
		return false;
	}
	return true;
}


/*
**  USER_MAP_LOOKUP -- look up a user in the passwd file.
*/

/* ARGSUSED3 */
char *
user_map_lookup(map, key, av, statp)
	MAP *map;
	char *key;
	char **av;
	int *statp;
{
	auto bool fuzzy;
	SM_MBDB_T user;

	if (tTd(38, 20))
		sm_dprintf("user_map_lookup(%s, %s)\n",
			map->map_mname, key);

	*statp = finduser(key, &fuzzy, &user);
	if (*statp != EX_OK)
		return NULL;
	if (bitset(MF_MATCHONLY, map->map_mflags))
		return map_rewrite(map, key, strlen(key), NULL);
	else
	{
		char *rwval = NULL;
		char buf[30];

		switch (map->map_valcolno)
		{
		  case 0:
		  case 1:
			rwval = user.mbdb_name;
			break;

		  case 2:
			rwval = "x";	/* passwd no longer supported */
			break;

		  case 3:
			(void) sm_snprintf(buf, sizeof(buf), "%d",
					   (int) user.mbdb_uid);
			rwval = buf;
			break;

		  case 4:
			(void) sm_snprintf(buf, sizeof(buf), "%d",
					   (int) user.mbdb_gid);
			rwval = buf;
			break;

		  case 5:
			rwval = user.mbdb_fullname;
			break;

		  case 6:
			rwval = user.mbdb_homedir;
			break;

		  case 7:
			rwval = user.mbdb_shell;
			break;
		  default:
			syserr("user_map %s: bogus field %d",
				map->map_mname, map->map_valcolno);
			return NULL;
		}
		return map_rewrite(map, rwval, strlen(rwval), av);
	}
}
/*
**  Program map type.
**
**	This provides access to arbitrary programs.  It should be used
**	only very sparingly, since there is no way to bound the cost
**	of invoking an arbitrary program.
*/

char *
prog_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	int i;
	int save_errno;
	int fd;
	int status;
	auto pid_t pid;
	register char *p;
	char *rval;
	char *argv[MAXPV + 1];
	char buf[MAXLINE];

	if (tTd(38, 20))
		sm_dprintf("prog_map_lookup(%s, %s) %s\n",
			map->map_mname, name, map->map_file);

	i = 0;
	argv[i++] = map->map_file;
	if (map->map_rebuild != NULL)
	{
		(void) sm_strlcpy(buf, map->map_rebuild, sizeof(buf));
		for (p = strtok(buf, " \t"); p != NULL; p = strtok(NULL, " \t"))
		{
			if (i >= MAXPV - 1)
				break;
			argv[i++] = p;
		}
	}
	argv[i++] = name;
	argv[i] = NULL;
	if (tTd(38, 21))
	{
		sm_dprintf("prog_open:");
		for (i = 0; argv[i] != NULL; i++)
			sm_dprintf(" %s", argv[i]);
		sm_dprintf("\n");
	}
	(void) sm_blocksignal(SIGCHLD);
	pid = prog_open(argv, &fd, CurEnv);
	if (pid < 0)
	{
		if (!bitset(MF_OPTIONAL, map->map_mflags))
			syserr("prog_map_lookup(%s) failed (%s) -- closing",
			       map->map_mname, sm_errstring(errno));
		else if (tTd(38, 9))
			sm_dprintf("prog_map_lookup(%s) failed (%s) -- closing",
				   map->map_mname, sm_errstring(errno));
		map->map_mflags &= ~(MF_VALID|MF_OPEN);
		*statp = EX_OSFILE;
		return NULL;
	}
	i = read(fd, buf, sizeof(buf) - 1);
	if (i < 0)
	{
		syserr("prog_map_lookup(%s): read error %s",
		       map->map_mname, sm_errstring(errno));
		rval = NULL;
	}
	else if (i == 0)
	{
		if (tTd(38, 20))
			sm_dprintf("prog_map_lookup(%s): empty answer\n",
				   map->map_mname);
		rval = NULL;
	}
	else
	{
		buf[i] = '\0';
		p = strchr(buf, '\n');
		if (p != NULL)
			*p = '\0';

		/* collect the return value */
		if (bitset(MF_MATCHONLY, map->map_mflags))
			rval = map_rewrite(map, name, strlen(name), NULL);
		else
			rval = map_rewrite(map, buf, strlen(buf), av);

		/* now flush any additional output */
		while ((i = read(fd, buf, sizeof(buf))) > 0)
			continue;
	}

	/* wait for the process to terminate */
	(void) close(fd);
	status = waitfor(pid);
	save_errno = errno;
	(void) sm_releasesignal(SIGCHLD);
	errno = save_errno;

	if (status == -1)
	{
		syserr("prog_map_lookup(%s): wait error %s",
		       map->map_mname, sm_errstring(errno));
		*statp = EX_SOFTWARE;
		rval = NULL;
	}
	else if (WIFEXITED(status))
	{
		if ((*statp = WEXITSTATUS(status)) != EX_OK)
			rval = NULL;
	}
	else
	{
		syserr("prog_map_lookup(%s): child died on signal %d",
		       map->map_mname, status);
		*statp = EX_UNAVAILABLE;
		rval = NULL;
	}
	return rval;
}
/*
**  Sequenced map type.
**
**	Tries each map in order until something matches, much like
**	implicit.  Stores go to the first map in the list that can
**	support storing.
**
**	This is slightly unusual in that there are two interfaces.
**	The "sequence" interface lets you stack maps arbitrarily.
**	The "switch" interface builds a sequence map by looking
**	at a system-dependent configuration file such as
**	/etc/nsswitch.conf on Solaris or /etc/svc.conf on Ultrix.
**
**	We don't need an explicit open, since all maps are
**	opened on demand.
*/

/*
**  SEQ_MAP_PARSE -- Sequenced map parsing
*/

bool
seq_map_parse(map, ap)
	MAP *map;
	char *ap;
{
	int maxmap;

	if (tTd(38, 2))
		sm_dprintf("seq_map_parse(%s, %s)\n", map->map_mname, ap);
	maxmap = 0;
	while (*ap != '\0')
	{
		register char *p;
		STAB *s;

		/* find beginning of map name */
		while (isascii(*ap) && isspace(*ap))
			ap++;
		for (p = ap;
		     (isascii(*p) && isalnum(*p)) || *p == '_' || *p == '.';
		     p++)
			continue;
		if (*p != '\0')
			*p++ = '\0';
		while (*p != '\0' && (!isascii(*p) || !isalnum(*p)))
			p++;
		if (*ap == '\0')
		{
			ap = p;
			continue;
		}
		s = stab(ap, ST_MAP, ST_FIND);
		if (s == NULL)
		{
			syserr("Sequence map %s: unknown member map %s",
				map->map_mname, ap);
		}
		else if (maxmap >= MAXMAPSTACK)
		{
			syserr("Sequence map %s: too many member maps (%d max)",
				map->map_mname, MAXMAPSTACK);
			maxmap++;
		}
		else if (maxmap < MAXMAPSTACK)
		{
			map->map_stack[maxmap++] = &s->s_map;
		}
		ap = p;
	}
	return true;
}

/*
**  SWITCH_MAP_OPEN -- open a switched map
**
**	This looks at the system-dependent configuration and builds
**	a sequence map that does the same thing.
**
**	Every system must define a switch_map_find routine in conf.c
**	that will return the list of service types associated with a
**	given service class.
*/

bool
switch_map_open(map, mode)
	MAP *map;
	int mode;
{
	int mapno;
	int nmaps;
	char *maptype[MAXMAPSTACK];

	if (tTd(38, 2))
		sm_dprintf("switch_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);

	mode &= O_ACCMODE;
	nmaps = switch_map_find(map->map_file, maptype, map->map_return);
	if (tTd(38, 19))
	{
		sm_dprintf("\tswitch_map_find => %d\n", nmaps);
		for (mapno = 0; mapno < nmaps; mapno++)
			sm_dprintf("\t\t%s\n", maptype[mapno]);
	}
	if (nmaps <= 0 || nmaps > MAXMAPSTACK)
		return false;

	for (mapno = 0; mapno < nmaps; mapno++)
	{
		register STAB *s;
		char nbuf[MAXNAME + 1];

		if (maptype[mapno] == NULL)
			continue;
		(void) sm_strlcpyn(nbuf, sizeof(nbuf), 3,
				   map->map_mname, ".", maptype[mapno]);
		s = stab(nbuf, ST_MAP, ST_FIND);
		if (s == NULL)
		{
			syserr("Switch map %s: unknown member map %s",
				map->map_mname, nbuf);
		}
		else
		{
			map->map_stack[mapno] = &s->s_map;
			if (tTd(38, 4))
				sm_dprintf("\tmap_stack[%d] = %s:%s\n",
					   mapno,
					   s->s_map.map_class->map_cname,
					   nbuf);
		}
	}
	return true;
}

#if 0
/*
**  SEQ_MAP_CLOSE -- close all underlying maps
*/

void
seq_map_close(map)
	MAP *map;
{
	int mapno;

	if (tTd(38, 9))
		sm_dprintf("seq_map_close(%s)\n", map->map_mname);

	for (mapno = 0; mapno < MAXMAPSTACK; mapno++)
	{
		MAP *mm = map->map_stack[mapno];

		if (mm == NULL || !bitset(MF_OPEN, mm->map_mflags))
			continue;
		mm->map_mflags |= MF_CLOSING;
		mm->map_class->map_close(mm);
		mm->map_mflags &= ~(MF_OPEN|MF_WRITABLE|MF_CLOSING);
	}
}
#endif /* 0 */

/*
**  SEQ_MAP_LOOKUP -- sequenced map lookup
*/

char *
seq_map_lookup(map, key, args, pstat)
	MAP *map;
	char *key;
	char **args;
	int *pstat;
{
	int mapno;
	int mapbit = 0x01;
	bool tempfail = false;

	if (tTd(38, 20))
		sm_dprintf("seq_map_lookup(%s, %s)\n", map->map_mname, key);

	for (mapno = 0; mapno < MAXMAPSTACK; mapbit <<= 1, mapno++)
	{
		MAP *mm = map->map_stack[mapno];
		char *rv;

		if (mm == NULL)
			continue;
		if (!bitset(MF_OPEN, mm->map_mflags) &&
		    !openmap(mm))
		{
			if (bitset(mapbit, map->map_return[MA_UNAVAIL]))
			{
				*pstat = EX_UNAVAILABLE;
				return NULL;
			}
			continue;
		}
		*pstat = EX_OK;
		rv = mm->map_class->map_lookup(mm, key, args, pstat);
		if (rv != NULL)
			return rv;
		if (*pstat == EX_TEMPFAIL)
		{
			if (bitset(mapbit, map->map_return[MA_TRYAGAIN]))
				return NULL;
			tempfail = true;
		}
		else if (bitset(mapbit, map->map_return[MA_NOTFOUND]))
			break;
	}
	if (tempfail)
		*pstat = EX_TEMPFAIL;
	else if (*pstat == EX_OK)
		*pstat = EX_NOTFOUND;
	return NULL;
}

/*
**  SEQ_MAP_STORE -- sequenced map store
*/

void
seq_map_store(map, key, val)
	MAP *map;
	char *key;
	char *val;
{
	int mapno;

	if (tTd(38, 12))
		sm_dprintf("seq_map_store(%s, %s, %s)\n",
			map->map_mname, key, val);

	for (mapno = 0; mapno < MAXMAPSTACK; mapno++)
	{
		MAP *mm = map->map_stack[mapno];

		if (mm == NULL || !bitset(MF_WRITABLE, mm->map_mflags))
			continue;

		mm->map_class->map_store(mm, key, val);
		return;
	}
	syserr("seq_map_store(%s, %s, %s): no writable map",
		map->map_mname, key, val);
}
/*
**  NULL stubs
*/

/* ARGSUSED */
bool
null_map_open(map, mode)
	MAP *map;
	int mode;
{
	return true;
}

/* ARGSUSED */
void
null_map_close(map)
	MAP *map;
{
	return;
}

char *
null_map_lookup(map, key, args, pstat)
	MAP *map;
	char *key;
	char **args;
	int *pstat;
{
	*pstat = EX_NOTFOUND;
	return NULL;
}

/* ARGSUSED */
void
null_map_store(map, key, val)
	MAP *map;
	char *key;
	char *val;
{
	return;
}

MAPCLASS	NullMapClass =
{
	"null-map",		NULL,			0,
	NULL,			null_map_lookup,	null_map_store,
	null_map_open,		null_map_close,
};

/*
**  BOGUS stubs
*/

char *
bogus_map_lookup(map, key, args, pstat)
	MAP *map;
	char *key;
	char **args;
	int *pstat;
{
	*pstat = EX_TEMPFAIL;
	return NULL;
}

MAPCLASS	BogusMapClass =
{
	"bogus-map",		NULL,			0,
	NULL,			bogus_map_lookup,	null_map_store,
	null_map_open,		null_map_close,
};
/*
**  MACRO modules
*/

char *
macro_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	int mid;

	if (tTd(38, 20))
		sm_dprintf("macro_map_lookup(%s, %s)\n", map->map_mname,
			name == NULL ? "NULL" : name);

	if (name == NULL ||
	    *name == '\0' ||
	    (mid = macid(name)) == 0)
	{
		*statp = EX_CONFIG;
		return NULL;
	}

	if (av[1] == NULL)
		macdefine(&CurEnv->e_macro, A_PERM, mid, NULL);
	else
		macdefine(&CurEnv->e_macro, A_TEMP, mid, av[1]);

	*statp = EX_OK;
	return "";
}
/*
**  REGEX modules
*/

#if MAP_REGEX

# include <regex.h>

# define DEFAULT_DELIM	CONDELSE
# define END_OF_FIELDS	-1
# define ERRBUF_SIZE	80
# define MAX_MATCH	32

# define xnalloc(s)	memset(xalloc(s), '\0', s);

struct regex_map
{
	regex_t	*regex_pattern_buf;	/* xalloc it */
	int	*regex_subfields;	/* move to type MAP */
	char	*regex_delim;		/* move to type MAP */
};

static int	parse_fields __P((char *, int *, int, int));
static char	*regex_map_rewrite __P((MAP *, const char*, size_t, char **));

static int
parse_fields(s, ibuf, blen, nr_substrings)
	char *s;
	int *ibuf;		/* array */
	int blen;		/* number of elements in ibuf */
	int nr_substrings;	/* number of substrings in the pattern */
{
	register char *cp;
	int i = 0;
	bool lastone = false;

	blen--;		/* for terminating END_OF_FIELDS */
	cp = s;
	do
	{
		for (;; cp++)
		{
			if (*cp == ',')
			{
				*cp = '\0';
				break;
			}
			if (*cp == '\0')
			{
				lastone = true;
				break;
			}
		}
		if (i < blen)
		{
			int val = atoi(s);

			if (val < 0 || val >= nr_substrings)
			{
				syserr("field (%d) out of range, only %d substrings in pattern",
				       val, nr_substrings);
				return -1;
			}
			ibuf[i++] = val;
		}
		else
		{
			syserr("too many fields, %d max", blen);
			return -1;
		}
		s = ++cp;
	} while (!lastone);
	ibuf[i] = END_OF_FIELDS;
	return i;
}

bool
regex_map_init(map, ap)
	MAP *map;
	char *ap;
{
	int regerr;
	struct regex_map *map_p;
	register char *p;
	char *sub_param = NULL;
	int pflags;
	static char defdstr[] = { (char) DEFAULT_DELIM, '\0' };

	if (tTd(38, 2))
		sm_dprintf("regex_map_init: mapname '%s', args '%s'\n",
			map->map_mname, ap);

	pflags = REG_ICASE | REG_EXTENDED | REG_NOSUB;
	p = ap;
	map_p = (struct regex_map *) xnalloc(sizeof(*map_p));
	map_p->regex_pattern_buf = (regex_t *)xnalloc(sizeof(regex_t));

	for (;;)
	{
		while (isascii(*p) && isspace(*p))
			p++;
		if (*p != '-')
			break;
		switch (*++p)
		{
		  case 'n':	/* not */
			map->map_mflags |= MF_REGEX_NOT;
			break;

		  case 'f':	/* case sensitive */
			map->map_mflags |= MF_NOFOLDCASE;
			pflags &= ~REG_ICASE;
			break;

		  case 'b':	/* basic regular expressions */
			pflags &= ~REG_EXTENDED;
			break;

		  case 's':	/* substring match () syntax */
			sub_param = ++p;
			pflags &= ~REG_NOSUB;
			break;

		  case 'd':	/* delimiter */
			map_p->regex_delim = ++p;
			break;

		  case 'a':	/* map append */
			map->map_app = ++p;
			break;

		  case 'm':	/* matchonly */
			map->map_mflags |= MF_MATCHONLY;
			break;

		  case 'q':
			map->map_mflags |= MF_KEEPQUOTES;
			break;

		  case 'S':
			map->map_spacesub = *++p;
			break;

		  case 'D':
			map->map_mflags |= MF_DEFER;
			break;

		}
		while (*p != '\0' && !(isascii(*p) && isspace(*p)))
			p++;
		if (*p != '\0')
			*p++ = '\0';
	}
	if (tTd(38, 3))
		sm_dprintf("regex_map_init: compile '%s' 0x%x\n", p, pflags);

	if ((regerr = regcomp(map_p->regex_pattern_buf, p, pflags)) != 0)
	{
		/* Errorhandling */
		char errbuf[ERRBUF_SIZE];

		(void) regerror(regerr, map_p->regex_pattern_buf,
			 errbuf, sizeof(errbuf));
		syserr("pattern-compile-error: %s", errbuf);
		sm_free(map_p->regex_pattern_buf); /* XXX */
		sm_free(map_p); /* XXX */
		return false;
	}

	if (map->map_app != NULL)
		map->map_app = newstr(map->map_app);
	if (map_p->regex_delim != NULL)
		map_p->regex_delim = newstr(map_p->regex_delim);
	else
		map_p->regex_delim = defdstr;

	if (!bitset(REG_NOSUB, pflags))
	{
		/* substring matching */
		int substrings;
		int *fields = (int *) xalloc(sizeof(int) * (MAX_MATCH + 1));

		substrings = map_p->regex_pattern_buf->re_nsub + 1;

		if (tTd(38, 3))
			sm_dprintf("regex_map_init: nr of substrings %d\n",
				substrings);

		if (substrings >= MAX_MATCH)
		{
			syserr("too many substrings, %d max", MAX_MATCH);
			sm_free(map_p->regex_pattern_buf); /* XXX */
			sm_free(map_p); /* XXX */
			return false;
		}
		if (sub_param != NULL && sub_param[0] != '\0')
		{
			/* optional parameter -sfields */
			if (parse_fields(sub_param, fields,
					 MAX_MATCH + 1, substrings) == -1)
				return false;
		}
		else
		{
			int i;

			/* set default fields */
			for (i = 0; i < substrings; i++)
				fields[i] = i;
			fields[i] = END_OF_FIELDS;
		}
		map_p->regex_subfields = fields;
		if (tTd(38, 3))
		{
			int *ip;

			sm_dprintf("regex_map_init: subfields");
			for (ip = fields; *ip != END_OF_FIELDS; ip++)
				sm_dprintf(" %d", *ip);
			sm_dprintf("\n");
		}
	}
	map->map_db1 = (ARBPTR_T) map_p;	/* dirty hack */
	return true;
}

static char *
regex_map_rewrite(map, s, slen, av)
	MAP *map;
	const char *s;
	size_t slen;
	char **av;
{
	if (bitset(MF_MATCHONLY, map->map_mflags))
		return map_rewrite(map, av[0], strlen(av[0]), NULL);
	else
		return map_rewrite(map, s, slen, av);
}

char *
regex_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	int reg_res;
	struct regex_map *map_p;
	regmatch_t pmatch[MAX_MATCH];

	if (tTd(38, 20))
	{
		char **cpp;

		sm_dprintf("regex_map_lookup: key '%s'\n", name);
		for (cpp = av; cpp != NULL && *cpp != NULL; cpp++)
			sm_dprintf("regex_map_lookup: arg '%s'\n", *cpp);
	}

	map_p = (struct regex_map *)(map->map_db1);
	reg_res = regexec(map_p->regex_pattern_buf,
			  name, MAX_MATCH, pmatch, 0);

	if (bitset(MF_REGEX_NOT, map->map_mflags))
	{
		/* option -n */
		if (reg_res == REG_NOMATCH)
			return regex_map_rewrite(map, "", (size_t) 0, av);
		else
			return NULL;
	}
	if (reg_res == REG_NOMATCH)
		return NULL;

	if (map_p->regex_subfields != NULL)
	{
		/* option -s */
		static char retbuf[MAXNAME];
		int fields[MAX_MATCH + 1];
		bool first = true;
		int anglecnt = 0, cmntcnt = 0, spacecnt = 0;
		bool quotemode = false, bslashmode = false;
		register char *dp, *sp;
		char *endp, *ldp;
		int *ip;

		dp = retbuf;
		ldp = retbuf + sizeof(retbuf) - 1;

		if (av[1] != NULL)
		{
			if (parse_fields(av[1], fields, MAX_MATCH + 1,
					 (int) map_p->regex_pattern_buf->re_nsub + 1) == -1)
			{
				*statp = EX_CONFIG;
				return NULL;
			}
			ip = fields;
		}
		else
			ip = map_p->regex_subfields;

		for ( ; *ip != END_OF_FIELDS; ip++)
		{
			if (!first)
			{
				for (sp = map_p->regex_delim; *sp; sp++)
				{
					if (dp < ldp)
						*dp++ = *sp;
				}
			}
			else
				first = false;

			if (*ip >= MAX_MATCH ||
			    pmatch[*ip].rm_so < 0 || pmatch[*ip].rm_eo < 0)
				continue;

			sp = name + pmatch[*ip].rm_so;
			endp = name + pmatch[*ip].rm_eo;
			for (; endp > sp; sp++)
			{
				if (dp < ldp)
				{
					if (bslashmode)
					{
						*dp++ = *sp;
						bslashmode = false;
					}
					else if (quotemode && *sp != '"' &&
						*sp != '\\')
					{
						*dp++ = *sp;
					}
					else switch (*dp++ = *sp)
					{
					  case '\\':
						bslashmode = true;
						break;

					  case '(':
						cmntcnt++;
						break;

					  case ')':
						cmntcnt--;
						break;

					  case '<':
						anglecnt++;
						break;

					  case '>':
						anglecnt--;
						break;

					  case ' ':
						spacecnt++;
						break;

					  case '"':
						quotemode = !quotemode;
						break;
					}
				}
			}
		}
		if (anglecnt != 0 || cmntcnt != 0 || quotemode ||
		    bslashmode || spacecnt != 0)
		{
			sm_syslog(LOG_WARNING, NOQID,
				  "Warning: regex may cause prescan() failure map=%s lookup=%s",
				  map->map_mname, name);
			return NULL;
		}

		*dp = '\0';

		return regex_map_rewrite(map, retbuf, strlen(retbuf), av);
	}
	return regex_map_rewrite(map, "", (size_t)0, av);
}
#endif /* MAP_REGEX */
/*
**  NSD modules
*/
#if MAP_NSD

# include <ndbm.h>
# define _DATUM_DEFINED
# include <ns_api.h>

typedef struct ns_map_list
{
	ns_map_t		*map;		/* XXX ns_ ? */
	char			*mapname;
	struct ns_map_list	*next;
} ns_map_list_t;

static ns_map_t *
ns_map_t_find(mapname)
	char *mapname;
{
	static ns_map_list_t *ns_maps = NULL;
	ns_map_list_t *ns_map;

	/* walk the list of maps looking for the correctly named map */
	for (ns_map = ns_maps; ns_map != NULL; ns_map = ns_map->next)
	{
		if (strcmp(ns_map->mapname, mapname) == 0)
			break;
	}

	/* if we are looking at a NULL ns_map_list_t, then create a new one */
	if (ns_map == NULL)
	{
		ns_map = (ns_map_list_t *) xalloc(sizeof(*ns_map));
		ns_map->mapname = newstr(mapname);
		ns_map->map = (ns_map_t *) xalloc(sizeof(*ns_map->map));
		memset(ns_map->map, '\0', sizeof(*ns_map->map));
		ns_map->next = ns_maps;
		ns_maps = ns_map;
	}
	return ns_map->map;
}

char *
nsd_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	int buflen, r;
	char *p;
	ns_map_t *ns_map;
	char keybuf[MAXNAME + 1];
	char buf[MAXLINE];

	if (tTd(38, 20))
		sm_dprintf("nsd_map_lookup(%s, %s)\n", map->map_mname, name);

	buflen = strlen(name);
	if (buflen > sizeof(keybuf) - 1)
		buflen = sizeof(keybuf) - 1;	/* XXX simply cut off? */
	memmove(keybuf, name, buflen);
	keybuf[buflen] = '\0';
	if (!bitset(MF_NOFOLDCASE, map->map_mflags))
		makelower(keybuf);

	ns_map = ns_map_t_find(map->map_file);
	if (ns_map == NULL)
	{
		if (tTd(38, 20))
			sm_dprintf("nsd_map_t_find failed\n");
		*statp = EX_UNAVAILABLE;
		return NULL;
	}
	r = ns_lookup(ns_map, NULL, map->map_file, keybuf, NULL,
		      buf, sizeof(buf));
	if (r == NS_UNAVAIL || r == NS_TRYAGAIN)
	{
		*statp = EX_TEMPFAIL;
		return NULL;
	}
	if (r == NS_BADREQ
# ifdef NS_NOPERM
	    || r == NS_NOPERM
# endif /* NS_NOPERM */
	    )
	{
		*statp = EX_CONFIG;
		return NULL;
	}
	if (r != NS_SUCCESS)
	{
		*statp = EX_NOTFOUND;
		return NULL;
	}

	*statp = EX_OK;

	/* Null out trailing \n */
	if ((p = strchr(buf, '\n')) != NULL)
		*p = '\0';

	return map_rewrite(map, buf, strlen(buf), av);
}
#endif /* MAP_NSD */

char *
arith_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	long r;
	long v[2];
	bool res = false;
	bool boolres;
	static char result[16];
	char **cpp;

	if (tTd(38, 2))
	{
		sm_dprintf("arith_map_lookup: key '%s'\n", name);
		for (cpp = av; cpp != NULL && *cpp != NULL; cpp++)
			sm_dprintf("arith_map_lookup: arg '%s'\n", *cpp);
	}
	r = 0;
	boolres = false;
	cpp = av;
	*statp = EX_OK;

	/*
	**  read arguments for arith map
	**  - no check is made whether they are really numbers
	**  - just ignores args after the second
	*/

	for (++cpp; cpp != NULL && *cpp != NULL && r < 2; cpp++)
		v[r++] = strtol(*cpp, NULL, 0);

	/* operator and (at least) two operands given? */
	if (name != NULL && r == 2)
	{
		switch (*name)
		{
		  case '|':
			r = v[0] | v[1];
			break;

		  case '&':
			r = v[0] & v[1];
			break;

		  case '%':
			if (v[1] == 0)
				return NULL;
			r = v[0] % v[1];
			break;
		  case '+':
			r = v[0] + v[1];
			break;

		  case '-':
			r = v[0] - v[1];
			break;

		  case '*':
			r = v[0] * v[1];
			break;

		  case '/':
			if (v[1] == 0)
				return NULL;
			r = v[0] / v[1];
			break;

		  case 'l':
			res = v[0] < v[1];
			boolres = true;
			break;

		  case '=':
			res = v[0] == v[1];
			boolres = true;
			break;

		  case 'r':
			r = v[1] - v[0] + 1;
			if (r <= 0)
				return NULL;
			r = get_random() % r + v[0];
			break;

		  default:
			/* XXX */
			*statp = EX_CONFIG;
			if (LogLevel > 10)
				sm_syslog(LOG_WARNING, NOQID,
					  "arith_map: unknown operator %c",
					  (isascii(*name) && isprint(*name)) ?
					  *name : '?');
			return NULL;
		}
		if (boolres)
			(void) sm_snprintf(result, sizeof(result),
				res ? "TRUE" : "FALSE");
		else
			(void) sm_snprintf(result, sizeof(result), "%ld", r);
		return result;
	}
	*statp = EX_CONFIG;
	return NULL;
}

char *
arpa_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	int r;
	char *rval;
	char result[128];	/* IPv6: 64 + 10 + 1 would be enough */

	if (tTd(38, 2))
		sm_dprintf("arpa_map_lookup: key '%s'\n", name);
	*statp = EX_DATAERR;
	r = 1;
	memset(result, '\0', sizeof(result));
	rval = NULL;

# if NETINET6
	if (sm_strncasecmp(name, "IPv6:", 5) == 0)
	{
		struct in6_addr in6_addr;

		r = anynet_pton(AF_INET6, name, &in6_addr);
		if (r == 1)
		{
			static char hex_digits[] =
				{ '0', '1', '2', '3', '4', '5', '6', '7', '8',
				  '9', 'a', 'b', 'c', 'd', 'e', 'f' };

			unsigned char *src;
			char *dst;
			int i;

			src = (unsigned char *) &in6_addr;
			dst = result;
			for (i = 15; i >= 0; i--) {
				*dst++ = hex_digits[src[i] & 0x0f];
				*dst++ = '.';
				*dst++ = hex_digits[(src[i] >> 4) & 0x0f];
				if (i > 0)
					*dst++ = '.';
			}
			*statp = EX_OK;
		}
	}
	else
# endif /* NETINET6 */
# if NETINET
	{
		struct in_addr in_addr;

		r = inet_pton(AF_INET, name, &in_addr);
		if (r == 1)
		{
			unsigned char *src;

			src = (unsigned char *) &in_addr;
			(void) snprintf(result, sizeof(result),
				"%u.%u.%u.%u",
				src[3], src[2], src[1], src[0]);
			*statp = EX_OK;
		}
	}
# endif /* NETINET */
	if (r < 0)
		*statp = EX_UNAVAILABLE;
	if (tTd(38, 2))
		sm_dprintf("arpa_map_lookup: r=%d, result='%s'\n", r, result);
	if (*statp == EX_OK)
	{
		if (bitset(MF_MATCHONLY, map->map_mflags))
			rval = map_rewrite(map, name, strlen(name), NULL);
		else
			rval = map_rewrite(map, result, strlen(result), av);
	}
	return rval;
}

#if SOCKETMAP

# if NETINET || NETINET6
#  include <arpa/inet.h>
# endif /* NETINET || NETINET6 */

# define socket_map_next map_stack[0]

/*
**  SOCKET_MAP_OPEN -- open socket table
*/

bool
socket_map_open(map, mode)
	MAP *map;
	int mode;
{
	STAB *s;
	int sock = 0;
	int tmo;
	SOCKADDR_LEN_T addrlen = 0;
	int addrno = 0;
	int save_errno;
	char *p;
	char *colon;
	char *at;
	struct hostent *hp = NULL;
	SOCKADDR addr;

	if (tTd(38, 2))
		sm_dprintf("socket_map_open(%s, %s, %d)\n",
			map->map_mname, map->map_file, mode);

	mode &= O_ACCMODE;

	/* sendmail doesn't have the ability to write to SOCKET (yet) */
	if (mode != O_RDONLY)
	{
		/* issue a pseudo-error message */
		errno = SM_EMAPCANTWRITE;
		return false;
	}

	if (*map->map_file == '\0')
	{
		syserr("socket map \"%s\": empty or missing socket information",
			map->map_mname);
		return false;
	}

	s = socket_map_findconn(map->map_file);
	if (s->s_socketmap != NULL)
	{
		/* Copy open connection */
		map->map_db1 = s->s_socketmap->map_db1;

		/* Add this map as head of linked list */
		map->socket_map_next = s->s_socketmap;
		s->s_socketmap = map;

		if (tTd(38, 2))
			sm_dprintf("using cached connection\n");
		return true;
	}

	if (tTd(38, 2))
		sm_dprintf("opening new connection\n");

	/* following code is ripped from milter.c */
	/* XXX It should be put in a library... */

	/* protocol:filename or protocol:port@host */
	memset(&addr, '\0', sizeof(addr));
	p = map->map_file;
	colon = strchr(p, ':');
	if (colon != NULL)
	{
		*colon = '\0';

		if (*p == '\0')
		{
# if NETUNIX
			/* default to AF_UNIX */
			addr.sa.sa_family = AF_UNIX;
# else /* NETUNIX */
#  if NETINET
			/* default to AF_INET */
			addr.sa.sa_family = AF_INET;
#  else /* NETINET */
#   if NETINET6
			/* default to AF_INET6 */
			addr.sa.sa_family = AF_INET6;
#   else /* NETINET6 */
			/* no protocols available */
			syserr("socket map \"%s\": no valid socket protocols available",
			map->map_mname);
			return false;
#   endif /* NETINET6 */
#  endif /* NETINET */
# endif /* NETUNIX */
		}
# if NETUNIX
		else if (sm_strcasecmp(p, "unix") == 0 ||
			 sm_strcasecmp(p, "local") == 0)
			addr.sa.sa_family = AF_UNIX;
# endif /* NETUNIX */
# if NETINET
		else if (sm_strcasecmp(p, "inet") == 0)
			addr.sa.sa_family = AF_INET;
# endif /* NETINET */
# if NETINET6
		else if (sm_strcasecmp(p, "inet6") == 0)
			addr.sa.sa_family = AF_INET6;
# endif /* NETINET6 */
		else
		{
# ifdef EPROTONOSUPPORT
			errno = EPROTONOSUPPORT;
# else /* EPROTONOSUPPORT */
			errno = EINVAL;
# endif /* EPROTONOSUPPORT */
			syserr("socket map \"%s\": unknown socket type %s",
			       map->map_mname, p);
			return false;
		}
		*colon++ = ':';
	}
	else
	{
		colon = p;
#if NETUNIX
		/* default to AF_UNIX */
		addr.sa.sa_family = AF_UNIX;
#else /* NETUNIX */
# if NETINET
		/* default to AF_INET */
		addr.sa.sa_family = AF_INET;
# else /* NETINET */
#  if NETINET6
		/* default to AF_INET6 */
		addr.sa.sa_family = AF_INET6;
#  else /* NETINET6 */
		syserr("socket map \"%s\": unknown socket type %s",
		       map->map_mname, p);
		return false;
#  endif /* NETINET6 */
# endif /* NETINET */
#endif /* NETUNIX */
	}

# if NETUNIX
	if (addr.sa.sa_family == AF_UNIX)
	{
		long sff = SFF_SAFEDIRPATH|SFF_OPENASROOT|SFF_NOLINK|SFF_EXECOK;

		at = colon;
		if (strlen(colon) >= sizeof(addr.sunix.sun_path))
		{
			syserr("socket map \"%s\": local socket name %s too long",
			       map->map_mname, colon);
			return false;
		}
		errno = safefile(colon, RunAsUid, RunAsGid, RunAsUserName, sff,
				 S_IRUSR|S_IWUSR, NULL);

		if (errno != 0)
		{
			/* if not safe, don't create */
				syserr("socket map \"%s\": local socket name %s unsafe",
			       map->map_mname, colon);
			return false;
		}

		(void) sm_strlcpy(addr.sunix.sun_path, colon,
			       sizeof(addr.sunix.sun_path));
		addrlen = sizeof(struct sockaddr_un);
	}
	else
# endif /* NETUNIX */
# if NETINET || NETINET6
	if (false
#  if NETINET
		 || addr.sa.sa_family == AF_INET
#  endif /* NETINET */
#  if NETINET6
		 || addr.sa.sa_family == AF_INET6
#  endif /* NETINET6 */
		 )
	{
		unsigned short port;

		/* Parse port@host */
		at = strchr(colon, '@');
		if (at == NULL)
		{
			syserr("socket map \"%s\": bad address %s (expected port@host)",
				       map->map_mname, colon);
			return false;
		}
		*at = '\0';
		if (isascii(*colon) && isdigit(*colon))
			port = htons((unsigned short) atoi(colon));
		else
		{
#  ifdef NO_GETSERVBYNAME
			syserr("socket map \"%s\": invalid port number %s",
				       map->map_mname, colon);
			return false;
#  else /* NO_GETSERVBYNAME */
			register struct servent *sp;

			sp = getservbyname(colon, "tcp");
			if (sp == NULL)
			{
				syserr("socket map \"%s\": unknown port name %s",
					       map->map_mname, colon);
				return false;
			}
			port = sp->s_port;
#  endif /* NO_GETSERVBYNAME */
		}
		*at++ = '@';
		if (*at == '[')
		{
			char *end;

			end = strchr(at, ']');
			if (end != NULL)
			{
				bool found = false;
#  if NETINET
				unsigned long hid = INADDR_NONE;
#  endif /* NETINET */
#  if NETINET6
				struct sockaddr_in6 hid6;
#  endif /* NETINET6 */

				*end = '\0';
#  if NETINET
				if (addr.sa.sa_family == AF_INET &&
				    (hid = inet_addr(&at[1])) != INADDR_NONE)
				{
					addr.sin.sin_addr.s_addr = hid;
					addr.sin.sin_port = port;
					found = true;
				}
#  endif /* NETINET */
#  if NETINET6
				(void) memset(&hid6, '\0', sizeof(hid6));
				if (addr.sa.sa_family == AF_INET6 &&
				    anynet_pton(AF_INET6, &at[1],
						&hid6.sin6_addr) == 1)
				{
					addr.sin6.sin6_addr = hid6.sin6_addr;
					addr.sin6.sin6_port = port;
					found = true;
				}
#  endif /* NETINET6 */
				*end = ']';
				if (!found)
				{
					syserr("socket map \"%s\": Invalid numeric domain spec \"%s\"",
					       map->map_mname, at);
					return false;
				}
			}
			else
			{
				syserr("socket map \"%s\": Invalid numeric domain spec \"%s\"",
				       map->map_mname, at);
				return false;
			}
		}
		else
		{
			hp = sm_gethostbyname(at, addr.sa.sa_family);
			if (hp == NULL)
			{
				syserr("socket map \"%s\": Unknown host name %s",
					map->map_mname, at);
				return false;
			}
			addr.sa.sa_family = hp->h_addrtype;
			switch (hp->h_addrtype)
			{
#  if NETINET
			  case AF_INET:
				memmove(&addr.sin.sin_addr,
					hp->h_addr, INADDRSZ);
				addr.sin.sin_port = port;
				addrlen = sizeof(struct sockaddr_in);
				addrno = 1;
				break;
#  endif /* NETINET */

#  if NETINET6
			  case AF_INET6:
				memmove(&addr.sin6.sin6_addr,
					hp->h_addr, IN6ADDRSZ);
				addr.sin6.sin6_port = port;
				addrlen = sizeof(struct sockaddr_in6);
				addrno = 1;
				break;
#  endif /* NETINET6 */

			  default:
				syserr("socket map \"%s\": Unknown protocol for %s (%d)",
					map->map_mname, at, hp->h_addrtype);
#  if NETINET6
				freehostent(hp);
#  endif /* NETINET6 */
				return false;
			}
		}
	}
	else
# endif /* NETINET || NETINET6 */
	{
		syserr("socket map \"%s\": unknown socket protocol",
			map->map_mname);
		return false;
	}

	/* nope, actually connecting */
	for (;;)
	{
		sock = socket(addr.sa.sa_family, SOCK_STREAM, 0);
		if (sock < 0)
		{
			save_errno = errno;
			if (tTd(38, 5))
				sm_dprintf("socket map \"%s\": error creating socket: %s\n",
					   map->map_mname,
					   sm_errstring(save_errno));
# if NETINET6
			if (hp != NULL)
				freehostent(hp);
# endif /* NETINET6 */
			return false;
		}

		if (connect(sock, (struct sockaddr *) &addr, addrlen) >= 0)
			break;

		/* couldn't connect.... try next address */
		save_errno = errno;
		p = CurHostName;
		CurHostName = at;
		if (tTd(38, 5))
			sm_dprintf("socket_open (%s): open %s failed: %s\n",
				map->map_mname, at, sm_errstring(save_errno));
		CurHostName = p;
		(void) close(sock);

		/* try next address */
		if (hp != NULL && hp->h_addr_list[addrno] != NULL)
		{
			switch (addr.sa.sa_family)
			{
# if NETINET
			  case AF_INET:
				memmove(&addr.sin.sin_addr,
					hp->h_addr_list[addrno++],
					INADDRSZ);
				break;
# endif /* NETINET */

# if NETINET6
			  case AF_INET6:
				memmove(&addr.sin6.sin6_addr,
					hp->h_addr_list[addrno++],
					IN6ADDRSZ);
				break;
# endif /* NETINET6 */

			  default:
				if (tTd(38, 5))
					sm_dprintf("socket map \"%s\": Unknown protocol for %s (%d)\n",
						   map->map_mname, at,
						   hp->h_addrtype);
# if NETINET6
				freehostent(hp);
# endif /* NETINET6 */
				return false;
			}
			continue;
		}
		p = CurHostName;
		CurHostName = at;
		if (tTd(38, 5))
			sm_dprintf("socket map \"%s\": error connecting to socket map: %s\n",
				   map->map_mname, sm_errstring(save_errno));
		CurHostName = p;
# if NETINET6
		if (hp != NULL)
			freehostent(hp);
# endif /* NETINET6 */
		return false;
	}
# if NETINET6
	if (hp != NULL)
	{
		freehostent(hp);
		hp = NULL;
	}
# endif /* NETINET6 */
	if ((map->map_db1 = (ARBPTR_T) sm_io_open(SmFtStdiofd,
						  SM_TIME_DEFAULT,
						  (void *) &sock,
						  SM_IO_RDWR,
						  NULL)) == NULL)
	{
		close(sock);
		if (tTd(38, 2))
		    sm_dprintf("socket_open (%s): failed to create stream: %s\n",
			       map->map_mname, sm_errstring(errno));
		return false;
	}

	tmo = map->map_timeout;
	if (tmo == 0)
		tmo = 30000;	/* default: 30s */
	else
		tmo *= 1000;	/* s -> ms */
	sm_io_setinfo(map->map_db1, SM_IO_WHAT_TIMEOUT, &tmo);

	/* Save connection for reuse */
	s->s_socketmap = map;
	return true;
}

/*
**  SOCKET_MAP_FINDCONN -- find a SOCKET connection to the server
**
**	Cache SOCKET connections based on the connection specifier
**	and PID so we don't have multiple connections open to
**	the same server for different maps.  Need a separate connection
**	per PID since a parent process may close the map before the
**	child is done with it.
**
**	Parameters:
**		conn -- SOCKET map connection specifier
**
**	Returns:
**		Symbol table entry for the SOCKET connection.
*/

static STAB *
socket_map_findconn(conn)
	const char *conn;
{
	char *nbuf;
	STAB *SM_NONVOLATILE s = NULL;

	nbuf = sm_stringf_x("%s%c%d", conn, CONDELSE, (int) CurrentPid);
	SM_TRY
		s = stab(nbuf, ST_SOCKETMAP, ST_ENTER);
	SM_FINALLY
		sm_free(nbuf);
	SM_END_TRY
	return s;
}

/*
**  SOCKET_MAP_CLOSE -- close the socket
*/

void
socket_map_close(map)
	MAP *map;
{
	STAB *s;
	MAP *smap;

	if (tTd(38, 20))
		sm_dprintf("socket_map_close(%s), pid=%ld\n", map->map_file,
			(long) CurrentPid);

	/* Check if already closed */
	if (map->map_db1 == NULL)
	{
		if (tTd(38, 20))
			sm_dprintf("socket_map_close(%s) already closed\n",
				map->map_file);
		return;
	}
	sm_io_close((SM_FILE_T *)map->map_db1, SM_TIME_DEFAULT);

	/* Mark all the maps that share the connection as closed */
	s = socket_map_findconn(map->map_file);
	smap = s->s_socketmap;
	while (smap != NULL)
	{
		MAP *next;

		if (tTd(38, 2) && smap != map)
			sm_dprintf("socket_map_close(%s): closed %s (shared SOCKET connection)\n",
				map->map_mname, smap->map_mname);

		smap->map_mflags &= ~(MF_OPEN|MF_WRITABLE);
		smap->map_db1 = NULL;
		next = smap->socket_map_next;
		smap->socket_map_next = NULL;
		smap = next;
	}
	s->s_socketmap = NULL;
}

/*
** SOCKET_MAP_LOOKUP -- look up a datum in a SOCKET table
*/

char *
socket_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	unsigned int nettolen, replylen, recvlen;
	char *replybuf, *rval, *value, *status, *key;
	SM_FILE_T *f;
	char keybuf[MAXNAME + 1];

	replybuf = NULL;
	rval = NULL;
	f = (SM_FILE_T *)map->map_db1;
	if (tTd(38, 20))
		sm_dprintf("socket_map_lookup(%s, %s) %s\n",
			map->map_mname, name, map->map_file);

	if (!bitset(MF_NOFOLDCASE, map->map_mflags))
	{
		nettolen = strlen(name);
		if (nettolen > sizeof(keybuf) - 1)
			nettolen = sizeof(keybuf) - 1;
		memmove(keybuf, name, nettolen);
		keybuf[nettolen] = '\0';
		makelower(keybuf);
		key = keybuf;
	}
	else
		key = name;

	nettolen = strlen(map->map_mname) + 1 + strlen(key);
	SM_ASSERT(nettolen > strlen(map->map_mname));
	SM_ASSERT(nettolen > strlen(key));
	if ((sm_io_fprintf(f, SM_TIME_DEFAULT, "%u:%s %s,",
			   nettolen, map->map_mname, key) == SM_IO_EOF) ||
	    (sm_io_flush(f, SM_TIME_DEFAULT) != 0) ||
	    (sm_io_error(f)))
	{
		syserr("451 4.3.0 socket_map_lookup(%s): failed to send lookup request",
			map->map_mname);
		*statp = EX_TEMPFAIL;
		goto errcl;
	}

	if (sm_io_fscanf(f, SM_TIME_DEFAULT, "%9u", &replylen) != 1)
	{
		if (errno == EAGAIN)
		{
			syserr("451 4.3.0 socket_map_lookup(%s): read timeout",
				map->map_mname);
		}
		else
		{
			syserr("451 4.3.0 socket_map_lookup(%s): failed to read length parameter of reply %d",
				map->map_mname, errno);
		}
		*statp = EX_TEMPFAIL;
		goto errcl;
	}
	if (replylen > SOCKETMAP_MAXL)
	{
		syserr("451 4.3.0 socket_map_lookup(%s): reply too long: %u",
			   map->map_mname, replylen);
		*statp = EX_TEMPFAIL;
		goto errcl;
	}
	if (sm_io_getc(f, SM_TIME_DEFAULT) != ':')
	{
		syserr("451 4.3.0 socket_map_lookup(%s): missing ':' in reply",
			map->map_mname);
		*statp = EX_TEMPFAIL;
		goto error;
	}

	replybuf = (char *) sm_malloc(replylen + 1);
	if (replybuf == NULL)
	{
		syserr("451 4.3.0 socket_map_lookup(%s): can't allocate %u bytes",
			map->map_mname, replylen + 1);
		*statp = EX_OSERR;
		goto error;
	}

	recvlen = sm_io_read(f, SM_TIME_DEFAULT, replybuf, replylen);
	if (recvlen < replylen)
	{
		syserr("451 4.3.0 socket_map_lookup(%s): received only %u of %u reply characters",
			   map->map_mname, recvlen, replylen);
		*statp = EX_TEMPFAIL;
		goto errcl;
	}
	if (sm_io_getc(f, SM_TIME_DEFAULT) != ',')
	{
		syserr("451 4.3.0 socket_map_lookup(%s): missing ',' in reply",
			map->map_mname);
		*statp = EX_TEMPFAIL;
		goto errcl;
	}
	status = replybuf;
	replybuf[recvlen] = '\0';
	value = strchr(replybuf, ' ');
	if (value != NULL)
	{
		*value = '\0';
		value++;
	}
	if (strcmp(status, "OK") == 0)
	{
		*statp = EX_OK;

		/* collect the return value */
		if (bitset(MF_MATCHONLY, map->map_mflags))
			rval = map_rewrite(map, key, strlen(key), NULL);
		else
			rval = map_rewrite(map, value, strlen(value), av);
	}
	else if (strcmp(status, "NOTFOUND") == 0)
	{
		*statp = EX_NOTFOUND;
		if (tTd(38, 20))
			sm_dprintf("socket_map_lookup(%s): %s not found\n",
				map->map_mname, key);
	}
	else
	{
		if (tTd(38, 5))
			sm_dprintf("socket_map_lookup(%s, %s): server returned error: type=%s, reason=%s\n",
				map->map_mname, key, status,
				value ? value : "");
		if ((strcmp(status, "TEMP") == 0) ||
		    (strcmp(status, "TIMEOUT") == 0))
			*statp = EX_TEMPFAIL;
		else if(strcmp(status, "PERM") == 0)
			*statp = EX_UNAVAILABLE;
		else
			*statp = EX_PROTOCOL;
	}

	if (replybuf != NULL)
		sm_free(replybuf);
	return rval;

  errcl:
	socket_map_close(map);
  error:
	if (replybuf != NULL)
		sm_free(replybuf);
	return rval;
}
#endif /* SOCKETMAP */
