 /*
  * Routines for controlled evaluation of host names, user names, and so on.
  * They are, in fact, wrappers around the functions that are specific for
  * the sockets or TLI programming interfaces. The request_info and host_info
  * structures are used for result cacheing.
  * 
  * These routines allows us to postpone expensive operations until their
  * results are really needed. Examples are hostname lookups and double
  * checks, or username lookups. Information that cannot be retrieved is
  * given the value "unknown" ("paranoid" in case of hostname problems).
  * 
  * When ALWAYS_HOSTNAME is off, hostname lookup is done only when required by
  * tcpd paranoid mode, by access control patterns, or by %letter expansions.
  * 
  * When ALWAYS_RFC931 mode is off, user lookup is done only when required by
  * access control patterns or %letter expansions.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) eval.c 1.3 95/01/30 19:51:45";
#endif

/* System libraries. */

#include <stdio.h>
#include <string.h>

/* Local stuff. */

#include "tcpd.h"

 /*
  * When a string has the value STRING_UNKNOWN, it means: don't bother, I
  * tried to look up the data but it was unavailable for some reason. When a
  * host name has the value STRING_PARANOID it means there was a name/address
  * conflict.
  */
char    unknown[] = STRING_UNKNOWN;
char    paranoid[] = STRING_PARANOID;

/* eval_user - look up user name */

char   *eval_user(request)
struct request_info *request;
{
    if (request->user[0] == 0) {
	strcpy(request->user, unknown);
	if (request->sink == 0 && request->client->sin && request->server->sin)
	    rfc931(request->client->sin, request->server->sin, request->user);
    }
    return (request->user);
}

/* eval_hostaddr - look up printable address */

char   *eval_hostaddr(host)
struct host_info *host;
{
    if (host->addr[0] == 0) {
	strcpy(host->addr, unknown);
	if (host->request->hostaddr != 0)
	    host->request->hostaddr(host);
    }
    return (host->addr);
}

/* eval_hostname - look up host name */

char   *eval_hostname(host)
struct host_info *host;
{
    if (host->name[0] == 0) {
	strcpy(host->name, unknown);
	if (host->request->hostname != 0)
	    host->request->hostname(host);
    }
    return (host->name);
}

/* eval_hostinfo - return string with host name (preferred) or address */

char   *eval_hostinfo(host)
struct host_info *host;
{
    char   *hostname;

#ifndef ALWAYS_HOSTNAME				/* no implicit host lookups */
    if (host->name[0] == 0)
	return (eval_hostaddr(host));
#endif
    hostname = eval_hostname(host);
    if (HOSTNAME_KNOWN(hostname)) {
	return (host->name);
    } else {
	return (eval_hostaddr(host));
    }
}

/* eval_client - return string with as much about the client as we know */

char   *eval_client(request)
struct request_info *request;
{
    static char both[2 * STRING_LENGTH];
    char   *hostinfo = eval_hostinfo(request->client);

#ifndef ALWAYS_RFC931				/* no implicit user lookups */
    if (request->user[0] == 0)
	return (hostinfo);
#endif
    if (STR_NE(eval_user(request), unknown)) {
	sprintf(both, "%s@%s", request->user, hostinfo);
	return (both);
    } else {
	return (hostinfo);
    }
}

/* eval_server - return string with as much about the server as we know */

char   *eval_server(request)
struct request_info *request;
{
    static char both[2 * STRING_LENGTH];
    char   *host = eval_hostinfo(request->server);
    char   *daemon = eval_daemon(request);

    if (STR_NE(host, unknown)) {
	sprintf(both, "%s@%s", daemon, host);
	return (both);
    } else {
	return (daemon);
    }
}
