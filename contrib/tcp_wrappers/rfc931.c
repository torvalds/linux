 /*
  * rfc931() speaks a common subset of the RFC 931, AUTH, TAP, IDENT and RFC
  * 1413 protocols. It queries an RFC 931 etc. compatible daemon on a remote
  * host to look up the owner of a connection. The information should not be
  * used for authentication purposes. This routine intercepts alarm signals.
  * 
  * Diagnostics are reported through syslog(3).
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  *
  * $FreeBSD$
  */

#ifndef lint
static char sccsid[] = "@(#) rfc931.c 1.10 95/01/02 16:11:34";
#endif

/* System libraries. */

#include <stdio.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#endif

/* Local stuff. */

#include "tcpd.h"

#define	RFC931_PORT	113		/* Semi-well-known port */
#define	ANY_PORT	0		/* Any old port will do */

int     rfc931_timeout = RFC931_TIMEOUT;/* Global so it can be changed */

static jmp_buf timebuf;

/* fsocket - open stdio stream on top of socket */

static FILE *fsocket(domain, type, protocol)
int     domain;
int     type;
int     protocol;
{
    int     s;
    FILE   *fp;

    if ((s = socket(domain, type, protocol)) < 0) {
	tcpd_warn("socket: %m");
	return (0);
    } else {
	if ((fp = fdopen(s, "r+")) == 0) {
	    tcpd_warn("fdopen: %m");
	    close(s);
	}
	return (fp);
    }
}

/* timeout - handle timeouts */

static void timeout(sig)
int     sig;
{
    longjmp(timebuf, sig);
}

/* rfc931 - return remote user name, given socket structures */

void    rfc931(rmt_sin, our_sin, dest)
#ifdef INET6
struct sockaddr *rmt_sin;
struct sockaddr *our_sin;
#else
struct sockaddr_in *rmt_sin;
struct sockaddr_in *our_sin;
#endif
char   *dest;
{
    unsigned rmt_port;
    unsigned our_port;
#ifdef INET6
    struct sockaddr_storage rmt_query_sin;
    struct sockaddr_storage our_query_sin;
    int alen;
#else
    struct sockaddr_in rmt_query_sin;
    struct sockaddr_in our_query_sin;
#endif
    char    user[256];			/* XXX */
    char    buffer[512];		/* XXX */
    char   *cp;
    char   *result = unknown;
    FILE   *fp;

#ifdef INET6
    /* address family must be the same */
    if (rmt_sin->sa_family != our_sin->sa_family) {
	STRN_CPY(dest, result, STRING_LENGTH);
	return;
    }
    switch (our_sin->sa_family) {
    case AF_INET:
	alen = sizeof(struct sockaddr_in);
	break;
    case AF_INET6:
	alen = sizeof(struct sockaddr_in6);
	break;
    default:
	STRN_CPY(dest, result, STRING_LENGTH);
	return;
    }
#endif

    /*
     * If we use a single, buffered, bidirectional stdio stream ("r+" or
     * "w+" mode) we may read our own output. Such behaviour would make sense
     * with resources that support random-access operations, but not with
     * sockets. ANSI C suggests several functions which can be called when
     * you want to change IO direction, fseek seems the most portable.
     */

#ifdef INET6
    if ((fp = fsocket(our_sin->sa_family, SOCK_STREAM, 0)) != 0) {
#else
    if ((fp = fsocket(AF_INET, SOCK_STREAM, 0)) != 0) {
#endif
	/*
	 * Set up a timer so we won't get stuck while waiting for the server.
	 */

	if (setjmp(timebuf) == 0) {
	    signal(SIGALRM, timeout);
	    alarm(rfc931_timeout);

	    /*
	     * Bind the local and remote ends of the query socket to the same
	     * IP addresses as the connection under investigation. We go
	     * through all this trouble because the local or remote system
	     * might have more than one network address. The RFC931 etc.
	     * client sends only port numbers; the server takes the IP
	     * addresses from the query socket.
	     */

#ifdef INET6
	    memcpy(&our_query_sin, our_sin, alen);
	    memcpy(&rmt_query_sin, rmt_sin, alen);
	    switch (our_sin->sa_family) {
	    case AF_INET:
		((struct sockaddr_in *)&our_query_sin)->sin_port = htons(ANY_PORT);
		((struct sockaddr_in *)&rmt_query_sin)->sin_port = htons(RFC931_PORT);
		break;
	    case AF_INET6:
		((struct sockaddr_in6 *)&our_query_sin)->sin6_port = htons(ANY_PORT);
		((struct sockaddr_in6 *)&rmt_query_sin)->sin6_port = htons(RFC931_PORT);
		break;
	    }

	    if (bind(fileno(fp), (struct sockaddr *) & our_query_sin,
		     alen) >= 0 &&
		connect(fileno(fp), (struct sockaddr *) & rmt_query_sin,
			alen) >= 0) {
#else
	    our_query_sin = *our_sin;
	    our_query_sin.sin_port = htons(ANY_PORT);
	    rmt_query_sin = *rmt_sin;
	    rmt_query_sin.sin_port = htons(RFC931_PORT);

	    if (bind(fileno(fp), (struct sockaddr *) & our_query_sin,
		     sizeof(our_query_sin)) >= 0 &&
		connect(fileno(fp), (struct sockaddr *) & rmt_query_sin,
			sizeof(rmt_query_sin)) >= 0) {
#endif

		/*
		 * Send query to server. Neglect the risk that a 13-byte
		 * write would have to be fragmented by the local system and
		 * cause trouble with buggy System V stdio libraries.
		 */

		fprintf(fp, "%u,%u\r\n",
#ifdef INET6
			ntohs(((struct sockaddr_in *)rmt_sin)->sin_port),
			ntohs(((struct sockaddr_in *)our_sin)->sin_port));
#else
			ntohs(rmt_sin->sin_port),
			ntohs(our_sin->sin_port));
#endif
		fflush(fp);
		fseek(fp, 0, SEEK_SET);

		/*
		 * Read response from server. Use fgets()/sscanf() so we can
		 * work around System V stdio libraries that incorrectly
		 * assume EOF when a read from a socket returns less than
		 * requested.
		 */

		if (fgets(buffer, sizeof(buffer), fp) != 0
		    && ferror(fp) == 0 && feof(fp) == 0
		    && sscanf(buffer, "%u , %u : USERID :%*[^:]:%255s",
			      &rmt_port, &our_port, user) == 3
#ifdef INET6
		    && ntohs(((struct sockaddr_in *)rmt_sin)->sin_port) == rmt_port
		    && ntohs(((struct sockaddr_in *)our_sin)->sin_port) == our_port) {
#else
		    && ntohs(rmt_sin->sin_port) == rmt_port
		    && ntohs(our_sin->sin_port) == our_port) {
#endif

		    /*
		     * Strip trailing carriage return. It is part of the
		     * protocol, not part of the data.
		     */

		    if (cp = strchr(user, '\r'))
			*cp = 0;
		    result = user;
		}
	    }
	    alarm(0);
	}
	fclose(fp);
    }
    STRN_CPY(dest, result, STRING_LENGTH);
}
