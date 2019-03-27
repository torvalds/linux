 /*
  * Workarounds for known system software bugs. This module provides wrappers
  * around library functions and system calls that are known to have problems
  * on some systems. Most of these workarounds won't do any harm on regular
  * systems.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  *
  * $FreeBSD$
  */

#ifndef lint
char    sccsid[] = "@(#) workarounds.c 1.6 96/03/19 16:22:25";
#endif

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>

extern int errno;

#include "tcpd.h"

 /*
  * Some AIX versions advertise a too small MAXHOSTNAMELEN value (32).
  * Result: long hostnames would be truncated, and connections would be
  * dropped because of host name verification failures. Adrian van Bloois
  * (A.vanBloois@info.nic.surfnet.nl) figured out what was the problem.
  */

#if (MAXHOSTNAMELEN < 64)
#undef MAXHOSTNAMELEN
#endif

/* In case not defined in <sys/param.h>. */

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN  256             /* storage for host name */
#endif

 /*
  * Some DG/UX inet_addr() versions return a struct/union instead of a long.
  * You have this problem when the compiler complains about illegal lvalues
  * or something like that. The following code fixes this mutant behaviour.
  * It should not be enabled on "normal" systems.
  * 
  * Bug reported by ben@piglet.cr.usgs.gov (Rev. Ben A. Mesander).
  */

#ifdef INET_ADDR_BUG

#undef inet_addr

long    fix_inet_addr(string)
char   *string;
{
    return (inet_addr(string).s_addr);
}

#endif /* INET_ADDR_BUG */

 /*
  * With some System-V versions, the fgets() library function does not
  * account for partial reads from e.g. sockets. The result is that fgets()
  * gives up too soon, causing username lookups to fail. Problem first
  * reported for IRIX 4.0.5, by Steve Kotsopoulos <steve@ecf.toronto.edu>.
  * The following code works around the problem. It does no harm on "normal"
  * systems.
  */

#ifdef BROKEN_FGETS

#undef fgets

char   *fix_fgets(buf, len, fp)
char   *buf;
int     len;
FILE   *fp;
{
    char   *cp = buf;
    int     c;

    /*
     * Copy until the buffer fills up, until EOF, or until a newline is
     * found.
     */
    while (len > 1 && (c = getc(fp)) != EOF) {
	len--;
	*cp++ = c;
	if (c == '\n')
	    break;
    }

    /*
     * Return 0 if nothing was read. This is correct even when a silly buffer
     * length was specified.
     */
    if (cp > buf) {
	*cp = 0;
	return (buf);
    } else {
	return (0);
    }
}

#endif /* BROKEN_FGETS */

 /*
  * With early SunOS 5 versions, recvfrom() does not completely fill in the
  * source address structure when doing a non-destructive read. The following
  * code works around the problem. It does no harm on "normal" systems.
  */

#ifdef RECVFROM_BUG

#undef recvfrom

int     fix_recvfrom(sock, buf, buflen, flags, from, fromlen)
int     sock;
char   *buf;
int     buflen;
int     flags;
struct sockaddr *from;
int    *fromlen;
{
    int     ret;

    /* Assume that both ends of a socket belong to the same address family. */

    if ((ret = recvfrom(sock, buf, buflen, flags, from, fromlen)) >= 0) {
	if (from->sa_family == 0) {
	    struct sockaddr my_addr;
	    int     my_addr_len = sizeof(my_addr);

	    if (getsockname(0, &my_addr, &my_addr_len)) {
		tcpd_warn("getsockname: %m");
	    } else {
		from->sa_family = my_addr.sa_family;
	    }
	}
    }
    return (ret);
}

#endif /* RECVFROM_BUG */

 /*
  * The Apollo SR10.3 and some SYSV4 getpeername(2) versions do not return an
  * error in case of a datagram-oriented socket. Instead, they claim that all
  * UDP requests come from address 0.0.0.0. The following code works around
  * the problem. It does no harm on "normal" systems.
  */

#ifdef GETPEERNAME_BUG

#undef getpeername

int     fix_getpeername(sock, sa, len)
int     sock;
struct sockaddr *sa;
int    *len;
{
    int     ret;
#ifdef INET6
    struct sockaddr *sin = sa;
#else
    struct sockaddr_in *sin = (struct sockaddr_in *) sa;
#endif

    if ((ret = getpeername(sock, sa, len)) >= 0
#ifdef INET6
	&& ((sin->su_si.si_family == AF_INET6
	     && IN6_IS_ADDR_UNSPECIFIED(&sin->su_sin6.sin6_addr))
	    || (sin->su_si.si_family == AF_INET
		&& sin->su_sin.sin_addr.s_addr == 0))) {
#else
	&& sa->sa_family == AF_INET
	&& sin->sin_addr.s_addr == 0) {
#endif
	errno = ENOTCONN;
	return (-1);
    } else {
	return (ret);
    }
}

#endif /* GETPEERNAME_BUG */

 /*
  * According to Karl Vogel (vogelke@c-17igp.wpafb.af.mil) some Pyramid
  * versions have no yp_default_domain() function. We use getdomainname()
  * instead.
  */

#ifdef USE_GETDOMAIN

int     yp_get_default_domain(ptr)
char  **ptr;
{
    static char mydomain[MAXHOSTNAMELEN];

    *ptr = mydomain;
    return (getdomainname(mydomain, MAXHOSTNAMELEN));
}

#endif /* USE_GETDOMAIN */

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

 /*
  * Solaris 2.4 gethostbyname() has problems with multihomed hosts. When
  * doing DNS through NIS, only one host address ends up in the address list.
  * All other addresses end up in the hostname alias list, interspersed with
  * copies of the official host name. This would wreak havoc with tcpd's
  * hostname double checks. Below is a workaround that should do no harm when
  * accidentally left in. A side effect of the workaround is that address
  * list members are no longer properly aligned for structure access.
  */

#ifdef SOLARIS_24_GETHOSTBYNAME_BUG

#undef gethostbyname

struct hostent *fix_gethostbyname(name)
char   *name;
{
    struct hostent *hp;
    struct in_addr addr;
    char  **o_addr_list;
    char  **o_aliases;
    char  **n_addr_list;
    int     broken_gethostbyname = 0;

    if ((hp = gethostbyname(name)) && !hp->h_addr_list[1] && hp->h_aliases[1]) {
	for (o_aliases = n_addr_list = hp->h_aliases; *o_aliases; o_aliases++) {
	    if ((addr.s_addr = inet_addr(*o_aliases)) != INADDR_NONE) {
		memcpy(*n_addr_list++, (char *) &addr, hp->h_length);
		broken_gethostbyname = 1;
	    }
	}
	if (broken_gethostbyname) {
	    o_addr_list = hp->h_addr_list;
	    memcpy(*n_addr_list++, *o_addr_list, hp->h_length);
	    *n_addr_list = 0;
	    hp->h_addr_list = hp->h_aliases;
	    hp->h_aliases = o_addr_list + 1;
	}
    }
    return (hp);
}

#endif /* SOLARIS_24_GETHOSTBYNAME_BUG */

 /*
  * Horror! Some FreeBSD 2.0 libc routines call strtok(). Since tcpd depends
  * heavily on strtok(), strange things may happen. Workaround: use our
  * private strtok(). This has been fixed in the meantime.
  */

#ifdef USE_STRSEP

char   *fix_strtok(buf, sep)
char   *buf;
char   *sep;
{
    static char *state;
    char   *result;

    if (buf)
	state = buf;
    while ((result = strsep(&state, sep)) && result[0] == 0)
	 /* void */ ;
    return (result);
}

#endif /* USE_STRSEP */

 /*
  * IRIX 5.3 (and possibly earlier versions, too) library routines call the
  * non-reentrant strtok() library routine, causing hosts to slip through
  * allow/deny filters. Workaround: don't rely on the vendor and use our own
  * strtok() function. FreeBSD 2.0 has a similar problem (fixed in 2.0.5).
  */

#ifdef LIBC_CALLS_STRTOK

char   *my_strtok(buf, sep)
char   *buf;
char   *sep;
{
    static char *state;
    char   *result;

    if (buf)
	state = buf;

    /*
     * Skip over separator characters and detect end of string.
     */
    if (*(state += strspn(state, sep)) == 0)
	return (0);

    /*
     * Skip over non-separator characters and terminate result.
     */
    result = state;
    if (*(state += strcspn(state, sep)) != 0)
	*state++ = 0;
    return (result);
}

#endif /* LIBC_CALLS_STRTOK */
