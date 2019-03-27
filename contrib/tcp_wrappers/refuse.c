 /*
  * refuse() reports a refused connection, and takes the consequences: in
  * case of a datagram-oriented service, the unread datagram is taken from
  * the input queue (or inetd would see the same datagram again and again);
  * the program is terminated.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  *
  * $FreeBSD$
  */

#ifndef lint
static char sccsid[] = "@(#) refuse.c 1.5 94/12/28 17:42:39";
#endif

/* System libraries. */

#include <stdio.h>
#include <syslog.h>

/* Local stuff. */

#include "tcpd.h"

/* refuse - refuse request */

void    refuse(request)
struct request_info *request;
{
#ifdef INET6
    syslog(deny_severity, "refused connect from %s (%s)",
	   eval_client(request), eval_hostaddr(request->client));
#else
    syslog(deny_severity, "refused connect from %s", eval_client(request));
#endif
    clean_exit(request);
    /* NOTREACHED */
}

