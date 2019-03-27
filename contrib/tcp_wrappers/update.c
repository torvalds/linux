 /*
  * Routines for controlled update/initialization of request structures.
  * 
  * request_init() initializes its argument. Pointers and string-valued members
  * are initialized to zero, to indicate that no lookup has been attempted.
  * 
  * request_set() adds information to an already initialized request structure.
  * 
  * Both functions take a variable-length name-value list.
  * 
  * Diagnostics are reported through syslog(3).
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  *
  * $FreeBSD$
  */

#ifndef lint
static char sccsid[] = "@(#) update.c 1.1 94/12/28 17:42:56";
#endif

/* System libraries */

#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <unistd.h>

/* Local stuff. */

#include "mystdarg.h"
#include "tcpd.h"

/* request_fill - request update engine */

static struct request_info *request_fill(request, ap)
struct request_info *request;
va_list ap;
{
    int     key;
    char   *ptr;

    while ((key = va_arg(ap, int)) > 0) {
	switch (key) {
	default:
	    tcpd_warn("request_fill: invalid key: %d", key);
	    return (request);
	case RQ_FILE:
	    request->fd = va_arg(ap, int);
	    continue;
	case RQ_CLIENT_SIN:
#ifdef INET6
	    request->client->sin = va_arg(ap, struct sockaddr *);
#else
	    request->client->sin = va_arg(ap, struct sockaddr_in *);
#endif
	    continue;
	case RQ_SERVER_SIN:
#ifdef INET6
	    request->server->sin = va_arg(ap, struct sockaddr *);
#else
	    request->server->sin = va_arg(ap, struct sockaddr_in *);
#endif
	    continue;

	    /*
	     * All other fields are strings with the same maximal length.
	     */

	case RQ_DAEMON:
	    ptr = request->daemon;
	    break;
	case RQ_USER:
	    ptr = request->user;
	    break;
	case RQ_CLIENT_NAME:
	    ptr = request->client->name;
	    break;
	case RQ_CLIENT_ADDR:
	    ptr = request->client->addr;
	    break;
	case RQ_SERVER_NAME:
	    ptr = request->server->name;
	    break;
	case RQ_SERVER_ADDR:
	    ptr = request->server->addr;
	    break;
	}
	STRN_CPY(ptr, va_arg(ap, char *), STRING_LENGTH);
    }
    return (request);
}

/* request_init - initialize request structure */

struct request_info *VARARGS(request_init, struct request_info *, request)
{
    static struct request_info default_info;
    struct request_info *r;
    va_list ap;

    /*
     * Initialize data members. We do not assign default function pointer
     * members, to avoid pulling in the whole socket module when it is not
     * really needed.
     */
    VASTART(ap, struct request_info *, request);
    *request = default_info;
    request->fd = -1;
    strcpy(request->daemon, unknown);
    sprintf(request->pid, "%d", getpid());
    request->client->request = request;
    request->server->request = request;
    r = request_fill(request, ap);
    VAEND(ap);
    return (r);
}

/* request_set - update request structure */

struct request_info *VARARGS(request_set, struct request_info *, request)
{
    struct request_info *r;
    va_list ap;

    VASTART(ap, struct request_info *, request);
    r = request_fill(request, ap);
    VAEND(ap);
    return (r);
}
