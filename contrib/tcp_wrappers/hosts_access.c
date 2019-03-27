 /*
  * This module implements a simple access control language that is based on
  * host (or domain) names, NIS (host) netgroup names, IP addresses (or
  * network numbers) and daemon process names. When a match is found the
  * search is terminated, and depending on whether PROCESS_OPTIONS is defined,
  * a list of options is executed or an optional shell command is executed.
  * 
  * Host and user names are looked up on demand, provided that suitable endpoint
  * information is available as sockaddr_in structures or TLI netbufs. As a
  * side effect, the pattern matching process may change the contents of
  * request structure fields.
  * 
  * Diagnostics are reported through syslog(3).
  * 
  * Compile with -DNETGROUP if your library provides support for netgroups.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  *
  * $FreeBSD$
  */

#ifndef lint
static char sccsid[] = "@(#) hosts_access.c 1.21 97/02/12 02:13:22";
#endif

/* System libraries. */

#include <sys/types.h>
#ifdef INT32_T
    typedef uint32_t u_int32_t;
#endif
#include <sys/param.h>
#ifdef INET6
#include <sys/socket.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <syslog.h>
#include <ctype.h>
#include <errno.h>
#include <setjmp.h>
#include <string.h>
#ifdef INET6
#include <netdb.h>
#endif
#include <stdlib.h>

extern char *fgets();
extern int errno;

#ifndef	INADDR_NONE
#define	INADDR_NONE	(-1)		/* XXX should be 0xffffffff */
#endif

/* Local stuff. */

#include "tcpd.h"

/* Error handling. */

extern jmp_buf tcpd_buf;

/* Delimiters for lists of daemons or clients. */

static char sep[] = ", \t\r\n";

/* Constants to be used in assignments only, not in comparisons... */

#define	YES		1
#define	NO		0

 /*
  * These variables are globally visible so that they can be redirected in
  * verification mode.
  */

char   *hosts_allow_table = HOSTS_ALLOW;
char   *hosts_deny_table = HOSTS_DENY;
int     hosts_access_verbose = 0;

 /*
  * In a long-running process, we are not at liberty to just go away.
  */

int     resident = (-1);		/* -1, 0: unknown; +1: yes */

/* Forward declarations. */

static int table_match();
static int list_match();
static int server_match();
static int client_match();
static int host_match();
static int string_match();
static int masked_match();
#ifdef INET6
static int masked_match4();
static int masked_match6();
#endif

/* Size of logical line buffer. */

#define	BUFLEN 2048

/* definition to be used from workarounds.c */
#ifdef NETGROUP
int     yp_get_default_domain(char  **);
#endif

/* hosts_access - host access control facility */

int     hosts_access(request)
struct request_info *request;
{
    int     verdict;

    /*
     * If the (daemon, client) pair is matched by an entry in the file
     * /etc/hosts.allow, access is granted. Otherwise, if the (daemon,
     * client) pair is matched by an entry in the file /etc/hosts.deny,
     * access is denied. Otherwise, access is granted. A non-existent
     * access-control file is treated as an empty file.
     * 
     * After a rule has been matched, the optional language extensions may
     * decide to grant or refuse service anyway. Or, while a rule is being
     * processed, a serious error is found, and it seems better to play safe
     * and deny service. All this is done by jumping back into the
     * hosts_access() routine, bypassing the regular return from the
     * table_match() function calls below.
     */

    if (resident <= 0)
	resident++;
    verdict = setjmp(tcpd_buf);
    if (verdict != 0)
	return (verdict == AC_PERMIT);
    if (table_match(hosts_allow_table, request))
	return (YES);
    if (table_match(hosts_deny_table, request))
	return (NO);
    return (YES);
}

/* table_match - match table entries with (daemon, client) pair */

static int table_match(table, request)
char   *table;
struct request_info *request;
{
    FILE   *fp;
    char    sv_list[BUFLEN];		/* becomes list of daemons */
    char   *cl_list;			/* becomes list of clients */
    char   *sh_cmd;			/* becomes optional shell command */
    int     match = NO;
    struct tcpd_context saved_context;
    char   *cp;

    saved_context = tcpd_context;		/* stupid compilers */

    /*
     * Between the fopen() and fclose() calls, avoid jumps that may cause
     * file descriptor leaks.
     */

    if ((fp = fopen(table, "r")) != 0) {
	tcpd_context.file = table;
	tcpd_context.line = 0;
	while (match == NO && xgets(sv_list, sizeof(sv_list), fp) != 0) {
	    if (sv_list[strlen(sv_list) - 1] != '\n') {
		tcpd_warn("missing newline or line too long");
		continue;
	    }
	    /* Ignore anything after unescaped # character */
	    for (cp = strchr(sv_list, '#'); cp != NULL;) {
		if (cp > sv_list && cp[-1] == '\\') {
		    cp = strchr(cp + 1, '#');
		    continue;
		}
		*cp = '\0';
		break;
	    }
	    if (sv_list[strspn(sv_list, " \t\r\n")] == 0)
		continue;
	    if ((cl_list = split_at(sv_list, ':')) == 0) {
		tcpd_warn("missing \":\" separator");
		continue;
	    }
	    sh_cmd = split_at(cl_list, ':');
	    match = list_match(sv_list, request, server_match)
		&& list_match(cl_list, request, client_match);
	}
	(void) fclose(fp);
    } else if (errno != ENOENT) {
	tcpd_warn("cannot open %s: %m", table);
    }
    if (match) {
	if (hosts_access_verbose > 1)
	    syslog(LOG_DEBUG, "matched:  %s line %d",
		   tcpd_context.file, tcpd_context.line);
	if (sh_cmd) {
#ifdef PROCESS_OPTIONS
	    process_options(sh_cmd, request);
#else
	    char    cmd[BUFSIZ];
	    shell_cmd(percent_x(cmd, sizeof(cmd), sh_cmd, request));
#endif
	}
    }
    tcpd_context = saved_context;
    return (match);
}

/* list_match - match a request against a list of patterns with exceptions */

static int list_match(list, request, match_fn)
char   *list;
struct request_info *request;
int   (*match_fn) ();
{
    char   *tok;

    /*
     * Process tokens one at a time. We have exhausted all possible matches
     * when we reach an "EXCEPT" token or the end of the list. If we do find
     * a match, look for an "EXCEPT" list and recurse to determine whether
     * the match is affected by any exceptions.
     */

    for (tok = strtok(list, sep); tok != 0; tok = strtok((char *) 0, sep)) {
	if (STR_EQ(tok, "EXCEPT"))		/* EXCEPT: give up */
	    return (NO);
	if (match_fn(tok, request)) {		/* YES: look for exceptions */
	    while ((tok = strtok((char *) 0, sep)) && STR_NE(tok, "EXCEPT"))
		 /* VOID */ ;
	    return (tok == 0 || list_match((char *) 0, request, match_fn) == 0);
	}
    }
    return (NO);
}

/* server_match - match server information */

static int server_match(tok, request)
char   *tok;
struct request_info *request;
{
    char   *host;

    if ((host = split_at(tok + 1, '@')) == 0) {	/* plain daemon */
	return (string_match(tok, eval_daemon(request)));
    } else {					/* daemon@host */
	return (string_match(tok, eval_daemon(request))
		&& host_match(host, request->server));
    }
}

/* client_match - match client information */

static int client_match(tok, request)
char   *tok;
struct request_info *request;
{
    char   *host;

    if ((host = split_at(tok + 1, '@')) == 0) {	/* plain host */
	return (host_match(tok, request->client));
    } else {					/* user@host */
	return (host_match(host, request->client)
		&& string_match(tok, eval_user(request)));
    }
}

/* hostfile_match - look up host patterns from file */

static int hostfile_match(path, host)
char   *path;
struct host_info *host;
{
    char    tok[BUFSIZ];
    int     match = NO;
    FILE   *fp;

    if ((fp = fopen(path, "r")) != 0) {
	while (fscanf(fp, "%s", tok) == 1 && !(match = host_match(tok, host)))
	     /* void */ ;
	fclose(fp);
    } else if (errno != ENOENT) {
	tcpd_warn("open %s: %m", path);
    }
    return (match);
}

/* host_match - match host name and/or address against pattern */

static int host_match(tok, host)
char   *tok;
struct host_info *host;
{
    char   *mask;

    /*
     * This code looks a little hairy because we want to avoid unnecessary
     * hostname lookups.
     * 
     * The KNOWN pattern requires that both address AND name be known; some
     * patterns are specific to host names or to host addresses; all other
     * patterns are satisfied when either the address OR the name match.
     */

    if (tok[0] == '@') {			/* netgroup: look it up */
#ifdef  NETGROUP
	static char *mydomain = 0;
	if (mydomain == 0)
	    yp_get_default_domain(&mydomain);
	return (innetgr(tok + 1, eval_hostname(host), (char *) 0, mydomain));
#else
	tcpd_warn("netgroup support is disabled");	/* not tcpd_jump() */
	return (NO);
#endif
    } else if (tok[0] == '/') {			/* /file hack */
	return (hostfile_match(tok, host));
    } else if (STR_EQ(tok, "KNOWN")) {		/* check address and name */
	char   *name = eval_hostname(host);
	return (STR_NE(eval_hostaddr(host), unknown) && HOSTNAME_KNOWN(name));
    } else if (STR_EQ(tok, "LOCAL")) {		/* local: no dots in name */
	char   *name = eval_hostname(host);
	return (strchr(name, '.') == 0 && HOSTNAME_KNOWN(name));
    } else if ((mask = split_at(tok, '/')) != 0) {	/* net/mask */
	return (masked_match(tok, mask, eval_hostaddr(host)));
    } else {					/* anything else */
	return (string_match(tok, eval_hostaddr(host))
	    || (NOT_INADDR(tok) && string_match(tok, eval_hostname(host))));
    }
}

/* string_match - match string against pattern */

static int string_match(tok, string)
char   *tok;
char   *string;
{
    int     n;

#ifdef INET6
    /* convert IPv4 mapped IPv6 address to IPv4 address */
    if (STRN_EQ(string, "::ffff:", 7)
	&& dot_quad_addr(string + 7) != INADDR_NONE) {
	string += 7;
    }
#endif
    if (tok[0] == '.') {			/* suffix */
	n = strlen(string) - strlen(tok);
	return (n > 0 && STR_EQ(tok, string + n));
    } else if (STR_EQ(tok, "ALL")) {		/* all: match any */
	return (YES);
    } else if (STR_EQ(tok, "KNOWN")) {		/* not unknown */
	return (STR_NE(string, unknown));
    } else if (tok[(n = strlen(tok)) - 1] == '.') {	/* prefix */
	return (STRN_EQ(tok, string, n));
    } else {					/* exact match */
#ifdef INET6
	struct addrinfo hints, *res;
	struct sockaddr_in6 pat, addr;
	int len, ret;
	char ch;

	len = strlen(tok);
	if (*tok == '[' && tok[len - 1] == ']') {
	    ch = tok[len - 1];
	    tok[len - 1] = '\0';
	    memset(&hints, 0, sizeof(hints));
	    hints.ai_family = AF_INET6;
	    hints.ai_socktype = SOCK_STREAM;
	    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
	    if ((ret = getaddrinfo(tok + 1, NULL, &hints, &res)) == 0) {
		memcpy(&pat, res->ai_addr, sizeof(pat));
		freeaddrinfo(res);
	    }
	    tok[len - 1] = ch;
	    if (ret != 0 || getaddrinfo(string, NULL, &hints, &res) != 0)
		return NO;
	    memcpy(&addr, res->ai_addr, sizeof(addr));
	    freeaddrinfo(res);
	    if (pat.sin6_scope_id != 0 &&
		addr.sin6_scope_id != pat.sin6_scope_id)
		return NO;
	    return (!memcmp(&pat.sin6_addr, &addr.sin6_addr,
			    sizeof(struct in6_addr)));
	    return (ret);
	}
#endif
	return (STR_EQ(tok, string));
    }
}

/* masked_match - match address against netnumber/netmask */

#ifdef INET6
static int masked_match(net_tok, mask_tok, string)
char   *net_tok;
char   *mask_tok;
char   *string;
{
    return (masked_match4(net_tok, mask_tok, string) ||
	    masked_match6(net_tok, mask_tok, string));
}

static int masked_match4(net_tok, mask_tok, string)
#else
static int masked_match(net_tok, mask_tok, string)
#endif
char   *net_tok;
char   *mask_tok;
char   *string;
{
#ifdef INET6
    u_int32_t net;
    u_int32_t mask;
    u_int32_t addr;
#else
    unsigned long net;
    unsigned long mask;
    unsigned long addr;
#endif

    /*
     * Disallow forms other than dotted quad: the treatment that inet_addr()
     * gives to forms with less than four components is inconsistent with the
     * access control language. John P. Rouillard <rouilj@cs.umb.edu>.
     */

    if ((addr = dot_quad_addr(string)) == INADDR_NONE)
	return (NO);
    if ((net = dot_quad_addr(net_tok)) == INADDR_NONE
	|| (mask = dot_quad_addr(mask_tok)) == INADDR_NONE) {
#ifndef INET6
	tcpd_warn("bad net/mask expression: %s/%s", net_tok, mask_tok);
#endif
	return (NO);				/* not tcpd_jump() */
    }
    return ((addr & mask) == net);
}

#ifdef INET6
static int masked_match6(net_tok, mask_tok, string)
char   *net_tok;
char   *mask_tok;
char   *string;
{
    struct addrinfo hints, *res;
    struct sockaddr_in6 net, addr;
    u_int32_t mask;
    int len, mask_len, i = 0;
    char ch;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
    if (getaddrinfo(string, NULL, &hints, &res) != 0)
	return NO;
    memcpy(&addr, res->ai_addr, sizeof(addr));
    freeaddrinfo(res);

    if (IN6_IS_ADDR_V4MAPPED(&addr.sin6_addr)) {
	if ((*(u_int32_t *)&net.sin6_addr.s6_addr[12] = dot_quad_addr(net_tok)) == INADDR_NONE
	 || (mask = dot_quad_addr(mask_tok)) == INADDR_NONE)
	    return (NO);
	return ((*(u_int32_t *)&addr.sin6_addr.s6_addr[12] & mask) == *(u_int32_t *)&net.sin6_addr.s6_addr[12]);
    }

    /* match IPv6 address against netnumber/prefixlen */
    len = strlen(net_tok);
    if (*net_tok != '[' || net_tok[len - 1] != ']')
	return NO;
    ch = net_tok[len - 1];
    net_tok[len - 1] = '\0';
    if (getaddrinfo(net_tok + 1, NULL, &hints, &res) != 0) {
	net_tok[len - 1] = ch;
	return NO;
    }
    memcpy(&net, res->ai_addr, sizeof(net));
    freeaddrinfo(res);
    net_tok[len - 1] = ch;
    if ((mask_len = atoi(mask_tok)) < 0 || mask_len > 128)
	return NO;

    if (net.sin6_scope_id != 0 && addr.sin6_scope_id != net.sin6_scope_id)
	return NO;
    while (mask_len > 0) {
	if (mask_len < 32) {
	    mask = htonl(~(0xffffffff >> mask_len));
	    if ((*(u_int32_t *)&addr.sin6_addr.s6_addr[i] & mask) != (*(u_int32_t *)&net.sin6_addr.s6_addr[i] & mask))
		return NO;
	    break;
	}
	if (*(u_int32_t *)&addr.sin6_addr.s6_addr[i] != *(u_int32_t *)&net.sin6_addr.s6_addr[i])
	    return NO;
	i += 4;
	mask_len -= 32;
    }
    return YES;
}
#endif /* INET6 */
