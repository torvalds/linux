/*
 *  Copyright (c) 1999-2001, 2004, 2010, 2013 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: sm_gethost.c,v 8.32 2013-11-22 20:51:36 ca Exp $")

#include <sendmail.h>
#if NETINET || NETINET6
# include <arpa/inet.h>
#endif /* NETINET || NETINET6 */
#include "libmilter.h"

/*
**  MI_GETHOSTBY{NAME,ADDR} -- compatibility routines for gethostbyXXX
**
**	Some operating systems have wierd problems with the gethostbyXXX
**	routines.  For example, Solaris versions at least through 2.3
**	don't properly deliver a canonical h_name field.  This tries to
**	work around these problems.
**
**	Support IPv6 as well as IPv4.
*/

#if NETINET6 && NEEDSGETIPNODE

static struct hostent *sm_getipnodebyname __P((const char *, int, int, int *));

# ifndef AI_ADDRCONFIG
#  define AI_ADDRCONFIG	0	/* dummy */
# endif /* ! AI_ADDRCONFIG */
# ifndef AI_ALL
#  define AI_ALL	0	/* dummy */
# endif /* ! AI_ALL */
# ifndef AI_DEFAULT
#  define AI_DEFAULT	0	/* dummy */
# endif /* ! AI_DEFAULT */

static struct hostent *
sm_getipnodebyname(name, family, flags, err)
	const char *name;
	int family;
	int flags;
	int *err;
{
	bool resv6 = true;
	struct hostent *h;

	if (family == AF_INET6)
	{
		/* From RFC2133, section 6.1 */
		resv6 = bitset(RES_USE_INET6, _res.options);
		_res.options |= RES_USE_INET6;
	}
	SM_SET_H_ERRNO(0);
	h = gethostbyname(name);
	if (family == AF_INET6 && !resv6)
		_res.options &= ~RES_USE_INET6;

	/* the function is supposed to return only the requested family */
	if (h != NULL && h->h_addrtype != family)
	{
# if NETINET6
		freehostent(h);
# endif /* NETINET6 */
		h = NULL;
		*err = NO_DATA;
	}
	else
		*err = h_errno;
	return h;
}

void
freehostent(h)
	struct hostent *h;
{
	/*
	**  Stub routine -- if they don't have getipnodeby*(),
	**  they probably don't have the free routine either.
	*/

	return;
}
#else /* NEEDSGETIPNODE && NETINET6 */
#define sm_getipnodebyname getipnodebyname 
#endif /* NEEDSGETIPNODE && NETINET6 */

struct hostent *
mi_gethostbyname(name, family)
	char *name;
	int family;
{
	struct hostent *h = NULL;
#if (SOLARIS > 10000 && SOLARIS < 20400) || (defined(SOLARIS) && SOLARIS < 204) || (defined(sony_news) && defined(__svr4))
# if SOLARIS == 20300 || SOLARIS == 203
	static struct hostent hp;
	static char buf[1000];
	extern struct hostent *_switch_gethostbyname_r();

	h = _switch_gethostbyname_r(name, &hp, buf, sizeof(buf), &h_errno);
# else /* SOLARIS == 20300 || SOLARIS == 203 */
	extern struct hostent *__switch_gethostbyname();

	h = __switch_gethostbyname(name);
# endif /* SOLARIS == 20300 || SOLARIS == 203 */
#else /* (SOLARIS > 10000 && SOLARIS < 20400) || (defined(SOLARIS) && SOLARIS < 204) || (defined(sony_news) && defined(__svr4)) */
# if NETINET6
#  ifndef SM_IPNODEBYNAME_FLAGS
    /* For IPv4-mapped addresses, use: AI_DEFAULT|AI_ALL */
#   define SM_IPNODEBYNAME_FLAGS	AI_ADDRCONFIG
#  endif /* SM_IPNODEBYNAME_FLAGS */

	int flags = SM_IPNODEBYNAME_FLAGS;
	int err;
# endif /* NETINET6 */

# if NETINET6
#  if ADDRCONFIG_IS_BROKEN
	flags &= ~AI_ADDRCONFIG;
#  endif /* ADDRCONFIG_IS_BROKEN */
	h = sm_getipnodebyname(name, family, flags, &err);
	SM_SET_H_ERRNO(err);
# else /* NETINET6 */
	h = gethostbyname(name);
# endif /* NETINET6 */

#endif /* (SOLARIS > 10000 && SOLARIS < 20400) || (defined(SOLARIS) && SOLARIS < 204) || (defined(sony_news) && defined(__svr4)) */

	/* the function is supposed to return only the requested family */
	if (h != NULL && h->h_addrtype != family)
	{
# if NETINET6
		freehostent(h);
# endif /* NETINET6 */
		h = NULL;
		SM_SET_H_ERRNO(NO_DATA);
	}
	return h;
}

#if NETINET6
/*
**  MI_INET_PTON -- convert printed form to network address.
**
**	Wrapper for inet_pton() which handles IPv6: labels.
**
**	Parameters:
**		family -- address family
**		src -- string
**		dst -- destination address structure
**
**	Returns:
**		1 if the address was valid
**		0 if the address wasn't parseable
**		-1 if error
*/

int
mi_inet_pton(family, src, dst)
	int family;
	const char *src;
	void *dst;
{
	if (family == AF_INET6 &&
	    strncasecmp(src, "IPv6:", 5) == 0)
		src += 5;
	return inet_pton(family, src, dst);
}
#endif /* NETINET6 */
