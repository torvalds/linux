 /*
  * hosts_ctl() combines common applications of the host access control
  * library routines. It bundles its arguments then calls the hosts_access()
  * access control checker. The host name and user name arguments should be
  * empty strings, STRING_UNKNOWN or real data. If a match is found, the
  * optional shell command is executed.
  * 
  * Restriction: this interface does not pass enough information to support
  * selective remote username lookups or selective hostname double checks.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) hosts_ctl.c 1.4 94/12/28 17:42:27";
#endif

#include <stdio.h>

#include "tcpd.h"

/* hosts_ctl - limited interface to the hosts_access() routine */

int     hosts_ctl(daemon, name, addr, user)
char   *daemon;
char   *name;
char   *addr;
char   *user;
{
    struct request_info request;

    return (hosts_access(request_init(&request,
				      RQ_DAEMON, daemon,
				      RQ_CLIENT_NAME, name,
				      RQ_CLIENT_ADDR, addr,
				      RQ_USER, user,
				      0)));
}
