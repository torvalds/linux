/*
 * Copyright (c) 1998-2004, 2006, 2010 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 * Copyright (c) 1986, 1995-1997 Eric P. Allman.  All rights reserved.
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sendmail.h>
#include "map.h"

#if NAMED_BIND
SM_RCSID("@(#)$Id: domain.c,v 8.205 2013-11-22 20:51:55 ca Exp $ (with name server)")
#else /* NAMED_BIND */
SM_RCSID("@(#)$Id: domain.c,v 8.205 2013-11-22 20:51:55 ca Exp $ (without name server)")
#endif /* NAMED_BIND */

#if NAMED_BIND

# include <arpa/inet.h>


# ifndef MXHOSTBUFSIZE
#  define MXHOSTBUFSIZE	(128 * MAXMXHOSTS)
# endif /* ! MXHOSTBUFSIZE */

static char	MXHostBuf[MXHOSTBUFSIZE];
#if (MXHOSTBUFSIZE < 2) || (MXHOSTBUFSIZE >= INT_MAX/2)
	ERROR: _MXHOSTBUFSIZE is out of range
#endif /* (MXHOSTBUFSIZE < 2) || (MXHOSTBUFSIZE >= INT_MAX/2) */

# ifndef MAXDNSRCH
#  define MAXDNSRCH	6	/* number of possible domains to search */
# endif /* ! MAXDNSRCH */

# ifndef RES_DNSRCH_VARIABLE
#  define RES_DNSRCH_VARIABLE	_res.dnsrch
# endif /* ! RES_DNSRCH_VARIABLE */

# ifndef NO_DATA
#  define NO_DATA	NO_ADDRESS
# endif /* ! NO_DATA */

# ifndef HFIXEDSZ
#  define HFIXEDSZ	12	/* sizeof(HEADER) */
# endif /* ! HFIXEDSZ */

# define MAXCNAMEDEPTH	10	/* maximum depth of CNAME recursion */

# if defined(__RES) && (__RES >= 19940415)
#  define RES_UNC_T	char *
# else /* defined(__RES) && (__RES >= 19940415) */
#  define RES_UNC_T	unsigned char *
# endif /* defined(__RES) && (__RES >= 19940415) */

static int	mxrand __P((char *));
static int	fallbackmxrr __P((int, unsigned short *, char **));

/*
**  GETFALLBACKMXRR -- get MX resource records for fallback MX host.
**
**	We have to initialize this once before doing anything else.
**	Moreover, we have to repeat this from time to time to avoid
**	stale data, e.g., in persistent queue runners.
**	This should be done in a parent process so the child
**	processes have the right data.
**
**	Parameters:
**		host -- the name of the fallback MX host.
**
**	Returns:
**		number of MX records.
**
**	Side Effects:
**		Populates NumFallbackMXHosts and fbhosts.
**		Sets renewal time (based on TTL).
*/

int NumFallbackMXHosts = 0;	/* Number of fallback MX hosts (after MX expansion) */
static char *fbhosts[MAXMXHOSTS + 1];

int
getfallbackmxrr(host)
	char *host;
{
	int i, rcode;
	int ttl;
	static time_t renew = 0;

#if 0
	/* This is currently done before this function is called. */
	if (host == NULL || *host == '\0')
		return 0;
#endif /* 0 */
	if (NumFallbackMXHosts > 0 && renew > curtime())
		return NumFallbackMXHosts;
	if (host[0] == '[')
	{
		fbhosts[0] = host;
		NumFallbackMXHosts = 1;
	}
	else
	{
		/* free old data */
		for (i = 0; i < NumFallbackMXHosts; i++)
			sm_free(fbhosts[i]);

		/* get new data */
		NumFallbackMXHosts = getmxrr(host, fbhosts, NULL, false,
					     &rcode, false, &ttl);
		renew = curtime() + ttl;
		for (i = 0; i < NumFallbackMXHosts; i++)
			fbhosts[i] = newstr(fbhosts[i]);
	}
	return NumFallbackMXHosts;
}

/*
**  FALLBACKMXRR -- add MX resource records for fallback MX host to list.
**
**	Parameters:
**		nmx -- current number of MX records.
**		prefs -- array of preferences.
**		mxhosts -- array of MX hosts (maximum size: MAXMXHOSTS)
**
**	Returns:
**		new number of MX records.
**
**	Side Effects:
**		If FallbackMX was set, it appends the MX records for
**		that host to mxhosts (and modifies prefs accordingly).
*/

static int
fallbackmxrr(nmx, prefs, mxhosts)
	int nmx;
	unsigned short *prefs;
	char **mxhosts;
{
	int i;

	for (i = 0; i < NumFallbackMXHosts && nmx < MAXMXHOSTS; i++)
	{
		if (nmx > 0)
			prefs[nmx] = prefs[nmx - 1] + 1;
		else
			prefs[nmx] = 0;
		mxhosts[nmx++] = fbhosts[i];
	}
	return nmx;
}

/*
**  GETMXRR -- get MX resource records for a domain
**
**	Parameters:
**		host -- the name of the host to MX.
**		mxhosts -- a pointer to a return buffer of MX records.
**		mxprefs -- a pointer to a return buffer of MX preferences.
**			If NULL, don't try to populate.
**		droplocalhost -- If true, all MX records less preferred
**			than the local host (as determined by $=w) will
**			be discarded.
**		rcode -- a pointer to an EX_ status code.
**		tryfallback -- add also fallback MX host?
**		pttl -- pointer to return TTL (can be NULL).
**
**	Returns:
**		The number of MX records found.
**		-1 if there is an internal failure.
**		If no MX records are found, mxhosts[0] is set to host
**			and 1 is returned.
**
**	Side Effects:
**		The entries made for mxhosts point to a static array
**		MXHostBuf[MXHOSTBUFSIZE], so the data needs to be copied,
**		if it must be preserved across calls to this function.
*/

int
getmxrr(host, mxhosts, mxprefs, droplocalhost, rcode, tryfallback, pttl)
	char *host;
	char **mxhosts;
	unsigned short *mxprefs;
	bool droplocalhost;
	int *rcode;
	bool tryfallback;
	int *pttl;
{
	register unsigned char *eom, *cp;
	register int i, j, n;
	int nmx = 0;
	register char *bp;
	HEADER *hp;
	querybuf answer;
	int ancount, qdcount, buflen;
	bool seenlocal = false;
	unsigned short pref, type;
	unsigned short localpref = 256;
	char *fallbackMX = FallbackMX;
	bool trycanon = false;
	unsigned short *prefs;
	int (*resfunc) __P((const char *, int, int, u_char *, int));
	unsigned short prefer[MAXMXHOSTS];
	int weight[MAXMXHOSTS];
	int ttl = 0;
	extern int res_query(), res_search();

	if (tTd(8, 2))
		sm_dprintf("getmxrr(%s, droplocalhost=%d)\n",
			   host, droplocalhost);
	*rcode = EX_OK;
	if (pttl != NULL)
		*pttl = SM_DEFAULT_TTL;
	if (*host == '\0')
		return 0;

	if ((fallbackMX != NULL && droplocalhost &&
	     wordinclass(fallbackMX, 'w')) || !tryfallback)
	{
		/* don't use fallback for this pass */
		fallbackMX = NULL;
	}

	if (mxprefs != NULL)
		prefs = mxprefs;
	else
		prefs = prefer;

	/* efficiency hack -- numeric or non-MX lookups */
	if (host[0] == '[')
		goto punt;

	/*
	**  If we don't have MX records in our host switch, don't
	**  try for MX records.  Note that this really isn't "right",
	**  since we might be set up to try NIS first and then DNS;
	**  if the host is found in NIS we really shouldn't be doing
	**  MX lookups.  However, that should be a degenerate case.
	*/

	if (!UseNameServer)
		goto punt;
	if (HasWildcardMX && ConfigLevel >= 6)
		resfunc = res_query;
	else
		resfunc = res_search;

	errno = 0;
	n = (*resfunc)(host, C_IN, T_MX, (unsigned char *) &answer,
		       sizeof(answer));
	if (n < 0)
	{
		if (tTd(8, 1))
			sm_dprintf("getmxrr: res_search(%s) failed (errno=%d, h_errno=%d)\n",
				host, errno, h_errno);
		switch (h_errno)
		{
		  case NO_DATA:
			trycanon = true;
			/* FALLTHROUGH */

		  case NO_RECOVERY:
			/* no MX data on this host */
			goto punt;

		  case HOST_NOT_FOUND:
# if BROKEN_RES_SEARCH
		  case 0:	/* Ultrix resolver retns failure w/ h_errno=0 */
# endif /* BROKEN_RES_SEARCH */
			/* host doesn't exist in DNS; might be in /etc/hosts */
			trycanon = true;
			*rcode = EX_NOHOST;
			goto punt;

		  case TRY_AGAIN:
		  case -1:
			/* couldn't connect to the name server */
			if (fallbackMX != NULL)
			{
				/* name server is hosed -- push to fallback */
				return fallbackmxrr(nmx, prefs, mxhosts);
			}
			/* it might come up later; better queue it up */
			*rcode = EX_TEMPFAIL;
			break;

		  default:
			syserr("getmxrr: res_search (%s) failed with impossible h_errno (%d)",
				host, h_errno);
			*rcode = EX_OSERR;
			break;
		}

		/* irreconcilable differences */
		return -1;
	}

	/* avoid problems after truncation in tcp packets */
	if (n > sizeof(answer))
		n = sizeof(answer);

	/* find first satisfactory answer */
	hp = (HEADER *)&answer;
	cp = (unsigned char *)&answer + HFIXEDSZ;
	eom = (unsigned char *)&answer + n;
	for (qdcount = ntohs((unsigned short) hp->qdcount);
	     qdcount--;
	     cp += n + QFIXEDSZ)
	{
		if ((n = dn_skipname(cp, eom)) < 0)
			goto punt;
	}

	/* NOTE: see definition of MXHostBuf! */
	buflen = sizeof(MXHostBuf) - 1;
	SM_ASSERT(buflen > 0);
	bp = MXHostBuf;
	ancount = ntohs((unsigned short) hp->ancount);

	/* See RFC 1035 for layout of RRs. */
	/* XXX leave room for FallbackMX ? */
	while (--ancount >= 0 && cp < eom && nmx < MAXMXHOSTS - 1)
	{
		if ((n = dn_expand((unsigned char *)&answer, eom, cp,
				   (RES_UNC_T) bp, buflen)) < 0)
			break;
		cp += n;
		GETSHORT(type, cp);
		cp += INT16SZ;		/* skip over class */
		GETLONG(ttl, cp);
		GETSHORT(n, cp);	/* rdlength */
		if (type != T_MX)
		{
			if (tTd(8, 8) || _res.options & RES_DEBUG)
				sm_dprintf("unexpected answer type %d, size %d\n",
					type, n);
			cp += n;
			continue;
		}
		GETSHORT(pref, cp);
		if ((n = dn_expand((unsigned char *)&answer, eom, cp,
				   (RES_UNC_T) bp, buflen)) < 0)
			break;
		cp += n;
		n = strlen(bp);
# if 0
		/* Can this happen? */
		if (n == 0)
		{
			if (LogLevel > 4)
				sm_syslog(LOG_ERR, NOQID,
					  "MX records for %s contain empty string",
					  host);
			continue;
		}
# endif /* 0 */
		if (wordinclass(bp, 'w'))
		{
			if (tTd(8, 3))
				sm_dprintf("found localhost (%s) in MX list, pref=%d\n",
					bp, pref);
			if (droplocalhost)
			{
				if (!seenlocal || pref < localpref)
					localpref = pref;
				seenlocal = true;
				continue;
			}
			weight[nmx] = 0;
		}
		else
			weight[nmx] = mxrand(bp);
		prefs[nmx] = pref;
		mxhosts[nmx++] = bp;
		bp += n;
		if (bp[-1] != '.')
		{
			*bp++ = '.';
			n++;
		}
		*bp++ = '\0';
		if (buflen < n + 1)
		{
			/* don't want to wrap buflen */
			break;
		}
		buflen -= n + 1;
	}

	/* return only one TTL entry, that should be sufficient */
	if (ttl > 0 && pttl != NULL)
		*pttl = ttl;

	/* sort the records */
	for (i = 0; i < nmx; i++)
	{
		for (j = i + 1; j < nmx; j++)
		{
			if (prefs[i] > prefs[j] ||
			    (prefs[i] == prefs[j] && weight[i] > weight[j]))
			{
				register int temp;
				register char *temp1;

				temp = prefs[i];
				prefs[i] = prefs[j];
				prefs[j] = temp;
				temp1 = mxhosts[i];
				mxhosts[i] = mxhosts[j];
				mxhosts[j] = temp1;
				temp = weight[i];
				weight[i] = weight[j];
				weight[j] = temp;
			}
		}
		if (seenlocal && prefs[i] >= localpref)
		{
			/* truncate higher preference part of list */
			nmx = i;
		}
	}

	/* delete duplicates from list (yes, some bozos have duplicates) */
	for (i = 0; i < nmx - 1; )
	{
		if (sm_strcasecmp(mxhosts[i], mxhosts[i + 1]) != 0)
			i++;
		else
		{
			/* compress out duplicate */
			for (j = i + 1; j < nmx; j++)
			{
				mxhosts[j] = mxhosts[j + 1];
				prefs[j] = prefs[j + 1];
			}
			nmx--;
		}
	}

	if (nmx == 0)
	{
punt:
		if (seenlocal)
		{
			struct hostent *h = NULL;

			/*
			**  If we have deleted all MX entries, this is
			**  an error -- we should NEVER send to a host that
			**  has an MX, and this should have been caught
			**  earlier in the config file.
			**
			**  Some sites prefer to go ahead and try the
			**  A record anyway; that case is handled by
			**  setting TryNullMXList.  I believe this is a
			**  bad idea, but it's up to you....
			*/

			if (TryNullMXList)
			{
				SM_SET_H_ERRNO(0);
				errno = 0;
				h = sm_gethostbyname(host, AF_INET);
				if (h == NULL)
				{
					if (errno == ETIMEDOUT ||
					    h_errno == TRY_AGAIN ||
					    (errno == ECONNREFUSED &&
					     UseNameServer))
					{
						*rcode = EX_TEMPFAIL;
						return -1;
					}
# if NETINET6
					SM_SET_H_ERRNO(0);
					errno = 0;
					h = sm_gethostbyname(host, AF_INET6);
					if (h == NULL &&
					    (errno == ETIMEDOUT ||
					     h_errno == TRY_AGAIN ||
					     (errno == ECONNREFUSED &&
					      UseNameServer)))
					{
						*rcode = EX_TEMPFAIL;
						return -1;
					}
# endif /* NETINET6 */
				}
			}

			if (h == NULL)
			{
				*rcode = EX_CONFIG;
				syserr("MX list for %s points back to %s",
				       host, MyHostName);
				return -1;
			}
# if NETINET6
			freehostent(h);
			h = NULL;
# endif /* NETINET6 */
		}
		if (strlen(host) >= sizeof(MXHostBuf))
		{
			*rcode = EX_CONFIG;
			syserr("Host name %s too long",
			       shortenstring(host, MAXSHORTSTR));
			return -1;
		}
		(void) sm_strlcpy(MXHostBuf, host, sizeof(MXHostBuf));
		mxhosts[0] = MXHostBuf;
		prefs[0] = 0;
		if (host[0] == '[')
		{
			register char *p;
# if NETINET6
			struct sockaddr_in6 tmp6;
# endif /* NETINET6 */

			/* this may be an MX suppression-style address */
			p = strchr(MXHostBuf, ']');
			if (p != NULL)
			{
				*p = '\0';

				if (inet_addr(&MXHostBuf[1]) != INADDR_NONE)
				{
					nmx++;
					*p = ']';
				}
# if NETINET6
				else if (anynet_pton(AF_INET6, &MXHostBuf[1],
						     &tmp6.sin6_addr) == 1)
				{
					nmx++;
					*p = ']';
				}
# endif /* NETINET6 */
				else
				{
					trycanon = true;
					mxhosts[0]++;
				}
			}
		}
		if (trycanon &&
		    getcanonname(mxhosts[0], sizeof(MXHostBuf) - 2, false, pttl))
		{
			/* XXX MXHostBuf == "" ?  is that possible? */
			bp = &MXHostBuf[strlen(MXHostBuf)];
			if (bp[-1] != '.')
			{
				*bp++ = '.';
				*bp = '\0';
			}
			nmx = 1;
		}
	}

	/* if we have a default lowest preference, include that */
	if (fallbackMX != NULL && !seenlocal)
	{
		nmx = fallbackmxrr(nmx, prefs, mxhosts);
	}
	return nmx;
}
/*
**  MXRAND -- create a randomizer for equal MX preferences
**
**	If two MX hosts have equal preferences we want to randomize
**	the selection.  But in order for signatures to be the same,
**	we need to randomize the same way each time.  This function
**	computes a pseudo-random hash function from the host name.
**
**	Parameters:
**		host -- the name of the host.
**
**	Returns:
**		A random but repeatable value based on the host name.
*/

static int
mxrand(host)
	register char *host;
{
	int hfunc;
	static unsigned int seed;

	if (seed == 0)
	{
		seed = (int) curtime() & 0xffff;
		if (seed == 0)
			seed++;
	}

	if (tTd(17, 9))
		sm_dprintf("mxrand(%s)", host);

	hfunc = seed;
	while (*host != '\0')
	{
		int c = *host++;

		if (isascii(c) && isupper(c))
			c = tolower(c);
		hfunc = ((hfunc << 1) ^ c) % 2003;
	}

	hfunc &= 0xff;
	hfunc++;

	if (tTd(17, 9))
		sm_dprintf(" = %d\n", hfunc);
	return hfunc;
}
/*
**  BESTMX -- find the best MX for a name
**
**	This is really a hack, but I don't see any obvious way
**	to generalize it at the moment.
*/

/* ARGSUSED3 */
char *
bestmx_map_lookup(map, name, av, statp)
	MAP *map;
	char *name;
	char **av;
	int *statp;
{
	int nmx;
	int saveopts = _res.options;
	int i;
	ssize_t len = 0;
	char *result;
	char *mxhosts[MAXMXHOSTS + 1];
#if _FFR_BESTMX_BETTER_TRUNCATION
	char *buf;
#else /* _FFR_BESTMX_BETTER_TRUNCATION */
	char *p;
	char buf[PSBUFSIZE / 2];
#endif /* _FFR_BESTMX_BETTER_TRUNCATION */

	_res.options &= ~(RES_DNSRCH|RES_DEFNAMES);
	nmx = getmxrr(name, mxhosts, NULL, false, statp, false, NULL);
	_res.options = saveopts;
	if (nmx <= 0)
		return NULL;
	if (bitset(MF_MATCHONLY, map->map_mflags))
		return map_rewrite(map, name, strlen(name), NULL);
	if ((map->map_coldelim == '\0') || (nmx == 1))
		return map_rewrite(map, mxhosts[0], strlen(mxhosts[0]), av);

	/*
	**  We were given a -z flag (return all MXs) and there are multiple
	**  ones.  We need to build them all into a list.
	*/

#if _FFR_BESTMX_BETTER_TRUNCATION
	for (i = 0; i < nmx; i++)
	{
		if (strchr(mxhosts[i], map->map_coldelim) != NULL)
		{
			syserr("bestmx_map_lookup: MX host %.64s includes map delimiter character 0x%02X",
			       mxhosts[i], map->map_coldelim);
			return NULL;
		}
		len += strlen(mxhosts[i]) + 1;
		if (len < 0)
		{
			len -= strlen(mxhosts[i]) + 1;
			break;
		}
	}
	buf = (char *) sm_malloc(len);
	if (buf == NULL)
	{
		*statp = EX_UNAVAILABLE;
		return NULL;
	}
	*buf = '\0';
	for (i = 0; i < nmx; i++)
	{
		int end;

		end = sm_strlcat(buf, mxhosts[i], len);
		if (i != nmx && end + 1 < len)
		{
			buf[end] = map->map_coldelim;
			buf[end + 1] = '\0';
		}
	}

	/* Cleanly truncate for rulesets */
	truncate_at_delim(buf, PSBUFSIZE / 2, map->map_coldelim);
#else /* _FFR_BESTMX_BETTER_TRUNCATION */
	p = buf;
	for (i = 0; i < nmx; i++)
	{
		size_t slen;

		if (strchr(mxhosts[i], map->map_coldelim) != NULL)
		{
			syserr("bestmx_map_lookup: MX host %.64s includes map delimiter character 0x%02X",
			       mxhosts[i], map->map_coldelim);
			return NULL;
		}
		slen = strlen(mxhosts[i]);
		if (len + slen + 2 > sizeof(buf))
			break;
		if (i > 0)
		{
			*p++ = map->map_coldelim;
			len++;
		}
		(void) sm_strlcpy(p, mxhosts[i], sizeof(buf) - len);
		p += slen;
		len += slen;
	}
#endif /* _FFR_BESTMX_BETTER_TRUNCATION */

	result = map_rewrite(map, buf, len, av);
#if _FFR_BESTMX_BETTER_TRUNCATION
	sm_free(buf);
#endif /* _FFR_BESTMX_BETTER_TRUNCATION */
	return result;
}
/*
**  DNS_GETCANONNAME -- get the canonical name for named host using DNS
**
**	This algorithm tries to be smart about wildcard MX records.
**	This is hard to do because DNS doesn't tell is if we matched
**	against a wildcard or a specific MX.
**
**	We always prefer A & CNAME records, since these are presumed
**	to be specific.
**
**	If we match an MX in one pass and lose it in the next, we use
**	the old one.  For example, consider an MX matching *.FOO.BAR.COM.
**	A hostname bletch.foo.bar.com will match against this MX, but
**	will stop matching when we try bletch.bar.com -- so we know
**	that bletch.foo.bar.com must have been right.  This fails if
**	there was also an MX record matching *.BAR.COM, but there are
**	some things that just can't be fixed.
**
**	Parameters:
**		host -- a buffer containing the name of the host.
**			This is a value-result parameter.
**		hbsize -- the size of the host buffer.
**		trymx -- if set, try MX records as well as A and CNAME.
**		statp -- pointer to place to store status.
**		pttl -- pointer to return TTL (can be NULL).
**
**	Returns:
**		true -- if the host matched.
**		false -- otherwise.
*/

bool
dns_getcanonname(host, hbsize, trymx, statp, pttl)
	char *host;
	int hbsize;
	bool trymx;
	int *statp;
	int *pttl;
{
	register unsigned char *eom, *ap;
	register char *cp;
	register int n;
	HEADER *hp;
	querybuf answer;
	int ancount, qdcount;
	int ret;
	char **domain;
	int type;
	int ttl = 0;
	char **dp;
	char *mxmatch;
	bool amatch;
	bool gotmx = false;
	int qtype;
	int initial;
	int loopcnt;
	char nbuf[SM_MAX(MAXPACKET, MAXDNAME*2+2)];
	char *searchlist[MAXDNSRCH + 2];

	if (tTd(8, 2))
		sm_dprintf("dns_getcanonname(%s, trymx=%d)\n", host, trymx);

	if ((_res.options & RES_INIT) == 0 && res_init() == -1)
	{
		*statp = EX_UNAVAILABLE;
		return false;
	}

	*statp = EX_OK;

	/*
	**  Initialize domain search list.  If there is at least one
	**  dot in the name, search the unmodified name first so we
	**  find "vse.CS" in Czechoslovakia instead of in the local
	**  domain (e.g., vse.CS.Berkeley.EDU).  Note that there is no
	**  longer a country named Czechoslovakia but this type of problem
	**  is still present.
	**
	**  Older versions of the resolver could create this
	**  list by tearing apart the host name.
	*/

	loopcnt = 0;
cnameloop:
	/* Check for dots in the name */
	for (cp = host, n = 0; *cp != '\0'; cp++)
		if (*cp == '.')
			n++;

	/*
	**  Build the search list.
	**	If there is at least one dot in name, start with a null
	**	domain to search the unmodified name first.
	**	If name does not end with a dot and search up local domain
	**	tree desired, append each local domain component to the
	**	search list; if name contains no dots and default domain
	**	name is desired, append default domain name to search list;
	**	else if name ends in a dot, remove that dot.
	*/

	dp = searchlist;
	if (n > 0)
		*dp++ = "";
	if (n >= 0 && *--cp != '.' && bitset(RES_DNSRCH, _res.options))
	{
		/* make sure there are less than MAXDNSRCH domains */
		for (domain = RES_DNSRCH_VARIABLE, ret = 0;
		     *domain != NULL && ret < MAXDNSRCH;
		     ret++)
			*dp++ = *domain++;
	}
	else if (n == 0 && bitset(RES_DEFNAMES, _res.options))
	{
		*dp++ = _res.defdname;
	}
	else if (*cp == '.')
	{
		*cp = '\0';
	}
	*dp = NULL;

	/*
	**  Now loop through the search list, appending each domain in turn
	**  name and searching for a match.
	*/

	mxmatch = NULL;
	initial = T_A;
# if NETINET6
	if (InetMode == AF_INET6)
		initial = T_AAAA;
# endif /* NETINET6 */
	qtype = initial;

	for (dp = searchlist; *dp != NULL; )
	{
		if (qtype == initial)
			gotmx = false;
		if (tTd(8, 5))
			sm_dprintf("dns_getcanonname: trying %s.%s (%s)\n",
				host, *dp,
# if NETINET6
				qtype == T_AAAA ? "AAAA" :
# endif /* NETINET6 */
				qtype == T_A ? "A" :
				qtype == T_MX ? "MX" :
				"???");
		errno = 0;
		ret = res_querydomain(host, *dp, C_IN, qtype,
				      answer.qb2, sizeof(answer.qb2));
		if (ret <= 0)
		{
			int save_errno = errno;

			if (tTd(8, 7))
				sm_dprintf("\tNO: errno=%d, h_errno=%d\n",
					   save_errno, h_errno);

			if (save_errno == ECONNREFUSED || h_errno == TRY_AGAIN)
			{
				/*
				**  the name server seems to be down or broken.
				*/

				SM_SET_H_ERRNO(TRY_AGAIN);
				if (**dp == '\0')
				{
					if (*statp == EX_OK)
						*statp = EX_TEMPFAIL;
					goto nexttype;
				}
				*statp = EX_TEMPFAIL;

				if (WorkAroundBrokenAAAA)
				{
					/*
					**  Only return if not TRY_AGAIN as an
					**  attempt with a different qtype may
					**  succeed (res_querydomain() calls
					**  res_query() calls res_send() which
					**  sets errno to ETIMEDOUT if the
					**  nameservers could be contacted but
					**  didn't give an answer).
					*/

					if (save_errno != ETIMEDOUT)
						return false;
				}
				else
					return false;
			}

nexttype:
			if (h_errno != HOST_NOT_FOUND)
			{
				/* might have another type of interest */
# if NETINET6
				if (qtype == T_AAAA)
				{
					qtype = T_A;
					continue;
				}
				else
# endif /* NETINET6 */
				if (qtype == T_A && !gotmx &&
				    (trymx || **dp == '\0'))
				{
					qtype = T_MX;
					continue;
				}
			}

			/* definite no -- try the next domain */
			dp++;
			qtype = initial;
			continue;
		}
		else if (tTd(8, 7))
			sm_dprintf("\tYES\n");

		/* avoid problems after truncation in tcp packets */
		if (ret > sizeof(answer))
			ret = sizeof(answer);
		SM_ASSERT(ret >= 0);

		/*
		**  Appear to have a match.  Confirm it by searching for A or
		**  CNAME records.  If we don't have a local domain
		**  wild card MX record, we will accept MX as well.
		*/

		hp = (HEADER *) &answer;
		ap = (unsigned char *) &answer + HFIXEDSZ;
		eom = (unsigned char *) &answer + ret;

		/* skip question part of response -- we know what we asked */
		for (qdcount = ntohs((unsigned short) hp->qdcount);
		     qdcount--;
		     ap += ret + QFIXEDSZ)
		{
			if ((ret = dn_skipname(ap, eom)) < 0)
			{
				if (tTd(8, 20))
					sm_dprintf("qdcount failure (%d)\n",
						ntohs((unsigned short) hp->qdcount));
				*statp = EX_SOFTWARE;
				return false;		/* ???XXX??? */
			}
		}

		amatch = false;
		for (ancount = ntohs((unsigned short) hp->ancount);
		     --ancount >= 0 && ap < eom;
		     ap += n)
		{
			n = dn_expand((unsigned char *) &answer, eom, ap,
				      (RES_UNC_T) nbuf, sizeof(nbuf));
			if (n < 0)
				break;
			ap += n;
			GETSHORT(type, ap);
			ap += INT16SZ;		/* skip over class */
			GETLONG(ttl, ap);
			GETSHORT(n, ap);	/* rdlength */
			switch (type)
			{
			  case T_MX:
				gotmx = true;
				if (**dp != '\0' && HasWildcardMX)
				{
					/*
					**  If we are using MX matches and have
					**  not yet gotten one, save this one
					**  but keep searching for an A or
					**  CNAME match.
					*/

					if (trymx && mxmatch == NULL)
						mxmatch = *dp;
					continue;
				}

				/*
				**  If we did not append a domain name, this
				**  must have been a canonical name to start
				**  with.  Even if we did append a domain name,
				**  in the absence of a wildcard MX this must
				**  still be a real MX match.
				**  Such MX matches are as good as an A match,
				**  fall through.
				*/
				/* FALLTHROUGH */

# if NETINET6
			  case T_AAAA:
# endif /* NETINET6 */
			  case T_A:
				/* Flag that a good match was found */
				amatch = true;

				/* continue in case a CNAME also exists */
				continue;

			  case T_CNAME:
				if (DontExpandCnames)
				{
					/* got CNAME -- guaranteed canonical */
					amatch = true;
					break;
				}

				if (loopcnt++ > MAXCNAMEDEPTH)
				{
					/*XXX should notify postmaster XXX*/
					message("DNS failure: CNAME loop for %s",
						host);
					if (CurEnv->e_message == NULL)
					{
						char ebuf[MAXLINE];

						(void) sm_snprintf(ebuf,
							sizeof(ebuf),
							"Deferred: DNS failure: CNAME loop for %.100s",
							host);
						CurEnv->e_message =
						    sm_rpool_strdup_x(
							CurEnv->e_rpool, ebuf);
					}
					SM_SET_H_ERRNO(NO_RECOVERY);
					*statp = EX_CONFIG;
					return false;
				}

				/* value points at name */
				if ((ret = dn_expand((unsigned char *)&answer,
						     eom, ap, (RES_UNC_T) nbuf,
						     sizeof(nbuf))) < 0)
					break;
				(void) sm_strlcpy(host, nbuf, hbsize);

				/*
				**  RFC 1034 section 3.6 specifies that CNAME
				**  should point at the canonical name -- but
				**  urges software to try again anyway.
				*/

				goto cnameloop;

			  default:
				/* not a record of interest */
				continue;
			}
		}

		if (amatch)
		{
			/*
			**  Got a good match -- either an A, CNAME, or an
			**  exact MX record.  Save it and get out of here.
			*/

			mxmatch = *dp;
			break;
		}

		/*
		**  Nothing definitive yet.
		**	If this was a T_A query and we haven't yet found a MX
		**		match, try T_MX if allowed to do so.
		**	Otherwise, try the next domain.
		*/

# if NETINET6
		if (qtype == T_AAAA)
			qtype = T_A;
		else
# endif /* NETINET6 */
		if (qtype == T_A && !gotmx && (trymx || **dp == '\0'))
			qtype = T_MX;
		else
		{
			qtype = initial;
			dp++;
		}
	}

	/* if nothing was found, we are done */
	if (mxmatch == NULL)
	{
		if (*statp == EX_OK)
			*statp = EX_NOHOST;
		return false;
	}

	/*
	**  Create canonical name and return.
	**  If saved domain name is null, name was already canonical.
	**  Otherwise append the saved domain name.
	*/

	(void) sm_snprintf(nbuf, sizeof(nbuf), "%.*s%s%.*s", MAXDNAME, host,
			   *mxmatch == '\0' ? "" : ".",
			   MAXDNAME, mxmatch);
	(void) sm_strlcpy(host, nbuf, hbsize);
	if (tTd(8, 5))
		sm_dprintf("dns_getcanonname: %s\n", host);
	*statp = EX_OK;

	/* return only one TTL entry, that should be sufficient */
	if (ttl > 0 && pttl != NULL)
		*pttl = ttl;
	return true;
}
#endif /* NAMED_BIND */
