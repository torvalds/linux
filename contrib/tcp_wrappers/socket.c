 /*
  * This module determines the type of socket (datagram, stream), the client
  * socket address and port, the server socket address and port. In addition,
  * it provides methods to map a transport address to a printable host name
  * or address. Socket address information results are in static memory.
  * 
  * The result from the hostname lookup method is STRING_PARANOID when a host
  * pretends to have someone elses name, or when a host name is available but
  * could not be verified.
  * 
  * When lookup or conversion fails the result is set to STRING_UNKNOWN.
  * 
  * Diagnostics are reported through syslog(3).
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  *
  * $FreeBSD$
  */

#ifndef lint
static char sccsid[] = "@(#) socket.c 1.15 97/03/21 19:27:24";
#endif

/* System libraries. */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>

#ifndef INET6
extern char *inet_ntoa();
#endif

/* Local stuff. */

#include "tcpd.h"

/* Forward declarations. */

static void sock_sink();

#ifdef APPEND_DOT

 /*
  * Speed up DNS lookups by terminating the host name with a dot. Should be
  * done with care. The speedup can give problems with lookups from sources
  * that lack DNS-style trailing dot magic, such as local files or NIS maps.
  */

static struct hostent *gethostbyname_dot(name)
char   *name;
{
    char    dot_name[MAXHOSTNAMELEN + 1];

    /*
     * Don't append dots to unqualified names. Such names are likely to come
     * from local hosts files or from NIS.
     */

    if (strchr(name, '.') == 0 || strlen(name) >= MAXHOSTNAMELEN - 1) {
	return (gethostbyname(name));
    } else {
	sprintf(dot_name, "%s.", name);
	return (gethostbyname(dot_name));
    }
}

#define gethostbyname gethostbyname_dot
#endif

/* sock_host - look up endpoint addresses and install conversion methods */

void    sock_host(request)
struct request_info *request;
{
#ifdef INET6
    static struct sockaddr_storage client;
    static struct sockaddr_storage server;
#else
    static struct sockaddr_in client;
    static struct sockaddr_in server;
#endif
    int     len;
    char    buf[BUFSIZ];
    int     fd = request->fd;

    sock_methods(request);

    /*
     * Look up the client host address. Hal R. Brand <BRAND@addvax.llnl.gov>
     * suggested how to get the client host info in case of UDP connections:
     * peek at the first message without actually looking at its contents. We
     * really should verify that client.sin_family gets the value AF_INET,
     * but this program has already caused too much grief on systems with
     * broken library code.
     */

    len = sizeof(client);
    if (getpeername(fd, (struct sockaddr *) & client, &len) < 0) {
	request->sink = sock_sink;
	len = sizeof(client);
	if (recvfrom(fd, buf, sizeof(buf), MSG_PEEK,
		     (struct sockaddr *) & client, &len) < 0) {
	    tcpd_warn("can't get client address: %m");
	    return;				/* give up */
	}
#ifdef really_paranoid
	memset(buf, 0, sizeof(buf));
#endif
    }
#ifdef INET6
    request->client->sin = (struct sockaddr *)&client;
#else
    request->client->sin = &client;
#endif

    /*
     * Determine the server binding. This is used for client username
     * lookups, and for access control rules that trigger on the server
     * address or name.
     */

    len = sizeof(server);
    if (getsockname(fd, (struct sockaddr *) & server, &len) < 0) {
	tcpd_warn("getsockname: %m");
	return;
    }
#ifdef INET6
    request->server->sin = (struct sockaddr *)&server;
#else
    request->server->sin = &server;
#endif
}

/* sock_hostaddr - map endpoint address to printable form */

void    sock_hostaddr(host)
struct host_info *host;
{
#ifdef INET6
    struct sockaddr *sin = host->sin;
    int salen;

    if (!sin)
	return;
#ifdef SIN6_LEN
    salen = sin->sa_len;
#else
    salen = (sin->sa_family == AF_INET) ? sizeof(struct sockaddr_in)
					: sizeof(struct sockaddr_in6);
#endif
    getnameinfo(sin, salen, host->addr, sizeof(host->addr),
		NULL, 0, NI_NUMERICHOST);
#else
    struct sockaddr_in *sin = host->sin;

    if (sin != 0)
	STRN_CPY(host->addr, inet_ntoa(sin->sin_addr), sizeof(host->addr));
#endif
}

/* sock_hostname - map endpoint address to host name */

void    sock_hostname(host)
struct host_info *host;
{
#ifdef INET6
    struct sockaddr *sin = host->sin;
    struct sockaddr_in sin4;
    struct addrinfo hints, *res, *res0 = NULL;
    int salen, alen, err = 1;
    char *ap = NULL, *rap, hname[NI_MAXHOST];

    if (sin != NULL) {
	if (sin->sa_family == AF_INET6) {
	    struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sin;

	    if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
		memset(&sin4, 0, sizeof(sin4));
#ifdef SIN6_LEN
		sin4.sin_len = sizeof(sin4);
#endif
		sin4.sin_family = AF_INET;
		sin4.sin_port = sin6->sin6_port;
		sin4.sin_addr.s_addr = *(u_int32_t *)&sin6->sin6_addr.s6_addr[12];
		sin = (struct sockaddr *)&sin4;
	    }
	}
	switch (sin->sa_family) {
	case AF_INET:
	    ap = (char *)&((struct sockaddr_in *)sin)->sin_addr;
	    alen = sizeof(struct in_addr);
	    salen = sizeof(struct sockaddr_in);
	    break;
	case AF_INET6:
	    ap = (char *)&((struct sockaddr_in6 *)sin)->sin6_addr;
	    alen = sizeof(struct in6_addr);
	    salen = sizeof(struct sockaddr_in6);
	    break;
	default:
	    break;
	}
	if (ap)
	    err = getnameinfo(sin, salen, hname, sizeof(hname),
			      NULL, 0, NI_NAMEREQD);
    }
    if (!err) {

	STRN_CPY(host->name, hname, sizeof(host->name));

	/* reject numeric addresses */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = sin->sa_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST;
	if ((err = getaddrinfo(host->name, NULL, &hints, &res0)) == 0) {
	    freeaddrinfo(res0);
	    tcpd_warn("host name/name mismatch: "
		      "reverse lookup results in non-FQDN %s",
		      host->name);
	    strcpy(host->name, paranoid);	/* name is bad, clobber it */
	}
	err = !err;
    }
    if (!err) {
	/* we are now sure that this is non-numeric */

	/*
	 * Verify that the address is a member of the address list returned
	 * by gethostbyname(hostname).
	 * 
	 * Verify also that gethostbyaddr() and gethostbyname() return the same
	 * hostname, or rshd and rlogind may still end up being spoofed.
	 * 
	 * On some sites, gethostbyname("localhost") returns "localhost.domain".
	 * This is a DNS artefact. We treat it as a special case. When we
	 * can't believe the address list from gethostbyname("localhost")
	 * we're in big trouble anyway.
	 */

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = sin->sa_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE | AI_CANONNAME;
	if (getaddrinfo(host->name, NULL, &hints, &res0) != 0) {

	    /*
	     * Unable to verify that the host name matches the address. This
	     * may be a transient problem or a botched name server setup.
	     */

	    tcpd_warn("can't verify hostname: getaddrinfo(%s, %s) failed",
		      host->name,
		      (sin->sa_family == AF_INET) ? "AF_INET" : "AF_INET6");

	} else if ((res0->ai_canonname == NULL
		    || STR_NE(host->name, res0->ai_canonname))
		   && STR_NE(host->name, "localhost")) {

	    /*
	     * The gethostbyaddr() and gethostbyname() calls did not return
	     * the same hostname. This could be a nameserver configuration
	     * problem. It could also be that someone is trying to spoof us.
	     */

	    tcpd_warn("host name/name mismatch: %s != %.*s",
		      host->name, STRING_LENGTH,
		      (res0->ai_canonname == NULL) ? "" : res0->ai_canonname);

	} else {

	    /*
	     * The address should be a member of the address list returned by
	     * gethostbyname(). We should first verify that the h_addrtype
	     * field is AF_INET, but this program has already caused too much
	     * grief on systems with broken library code.
	     */

	    for (res = res0; res; res = res->ai_next) {
		if (res->ai_family != sin->sa_family)
		    continue;
		switch (res->ai_family) {
		case AF_INET:
		    rap = (char *)&((struct sockaddr_in *)res->ai_addr)->sin_addr;
		    break;
		case AF_INET6:
		    /* need to check scope_id */
		    if (((struct sockaddr_in6 *)sin)->sin6_scope_id !=
		        ((struct sockaddr_in6 *)res->ai_addr)->sin6_scope_id) {
			continue;
		    }
		    rap = (char *)&((struct sockaddr_in6 *)res->ai_addr)->sin6_addr;
		    break;
		default:
		    continue;
		}
		if (memcmp(rap, ap, alen) == 0) {
		    freeaddrinfo(res0);
		    return;			/* name is good, keep it */
		}
	    }

	    /*
	     * The host name does not map to the initial address. Perhaps
	     * someone has messed up. Perhaps someone compromised a name
	     * server.
	     */

	    getnameinfo(sin, salen, hname, sizeof(hname),
			NULL, 0, NI_NUMERICHOST);
	    tcpd_warn("host name/address mismatch: %s != %.*s",
		      hname, STRING_LENGTH,
		      (res0->ai_canonname == NULL) ? "" : res0->ai_canonname);
	}
	strcpy(host->name, paranoid);		/* name is bad, clobber it */
	if (res0)
	    freeaddrinfo(res0);
    }
#else /* INET6 */
    struct sockaddr_in *sin = host->sin;
    struct hostent *hp;
    int     i;

    /*
     * On some systems, for example Solaris 2.3, gethostbyaddr(0.0.0.0) does
     * not fail. Instead it returns "INADDR_ANY". Unfortunately, this does
     * not work the other way around: gethostbyname("INADDR_ANY") fails. We
     * have to special-case 0.0.0.0, in order to avoid false alerts from the
     * host name/address checking code below.
     */
    if (sin != 0 && sin->sin_addr.s_addr != 0
	&& (hp = gethostbyaddr((char *) &(sin->sin_addr),
			       sizeof(sin->sin_addr), AF_INET)) != 0) {

	STRN_CPY(host->name, hp->h_name, sizeof(host->name));

	/*
	 * Verify that the address is a member of the address list returned
	 * by gethostbyname(hostname).
	 * 
	 * Verify also that gethostbyaddr() and gethostbyname() return the same
	 * hostname, or rshd and rlogind may still end up being spoofed.
	 * 
	 * On some sites, gethostbyname("localhost") returns "localhost.domain".
	 * This is a DNS artefact. We treat it as a special case. When we
	 * can't believe the address list from gethostbyname("localhost")
	 * we're in big trouble anyway.
	 */

	if ((hp = gethostbyname(host->name)) == 0) {

	    /*
	     * Unable to verify that the host name matches the address. This
	     * may be a transient problem or a botched name server setup.
	     */

	    tcpd_warn("can't verify hostname: gethostbyname(%s) failed",
		      host->name);

	} else if (STR_NE(host->name, hp->h_name)
		   && STR_NE(host->name, "localhost")) {

	    /*
	     * The gethostbyaddr() and gethostbyname() calls did not return
	     * the same hostname. This could be a nameserver configuration
	     * problem. It could also be that someone is trying to spoof us.
	     */

	    tcpd_warn("host name/name mismatch: %s != %.*s",
		      host->name, STRING_LENGTH, hp->h_name);

	} else {

	    /*
	     * The address should be a member of the address list returned by
	     * gethostbyname(). We should first verify that the h_addrtype
	     * field is AF_INET, but this program has already caused too much
	     * grief on systems with broken library code.
	     */

	    for (i = 0; hp->h_addr_list[i]; i++) {
		if (memcmp(hp->h_addr_list[i],
			   (char *) &sin->sin_addr,
			   sizeof(sin->sin_addr)) == 0)
		    return;			/* name is good, keep it */
	    }

	    /*
	     * The host name does not map to the initial address. Perhaps
	     * someone has messed up. Perhaps someone compromised a name
	     * server.
	     */

	    tcpd_warn("host name/address mismatch: %s != %.*s",
		      inet_ntoa(sin->sin_addr), STRING_LENGTH, hp->h_name);
	}
	strcpy(host->name, paranoid);		/* name is bad, clobber it */
    }
#endif /* INET6 */
}

/* sock_sink - absorb unreceived IP datagram */

static void sock_sink(fd)
int     fd;
{
    char    buf[BUFSIZ];
#ifdef INET6
    struct sockaddr_storage sin;
#else
    struct sockaddr_in sin;
#endif
    int     size = sizeof(sin);

    /*
     * Eat up the not-yet received datagram. Some systems insist on a
     * non-zero source address argument in the recvfrom() call below.
     */

    (void) recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *) & sin, &size);
}
