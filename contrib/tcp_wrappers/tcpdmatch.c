 /*
  * tcpdmatch - explain what tcpd would do in a specific case
  * 
  * usage: tcpdmatch [-d] [-i inet_conf] daemon[@host] [user@]host
  * 
  * -d: use the access control tables in the current directory.
  * 
  * -i: location of inetd.conf file.
  * 
  * All errors are reported to the standard error stream, including the errors
  * that would normally be reported via the syslog daemon.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  *
  * $FreeBSD$
  */

#ifndef lint
static char sccsid[] = "@(#) tcpdmatch.c 1.5 96/02/11 17:01:36";
#endif

/* System libraries. */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <syslog.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>

extern void exit();
extern int optind;
extern char *optarg;

#ifndef	INADDR_NONE
#define	INADDR_NONE	(-1)		/* XXX should be 0xffffffff */
#endif

#ifndef S_ISDIR
#define S_ISDIR(m)	(((m) & S_IFMT) == S_IFDIR)
#endif

/* Application-specific. */

#include "tcpd.h"
#include "inetcf.h"
#include "scaffold.h"

static void usage();
static void tcpdmatch();

/* The main program */

int     main(argc, argv)
int     argc;
char  **argv;
{
#ifdef INET6
    struct addrinfo hints, *hp, *res;
#else
    struct hostent *hp;
#endif
    char   *myname = argv[0];
    char   *client;
    char   *server;
    char   *addr;
    char   *user;
    char   *daemon;
    struct request_info request;
    int     ch;
    char   *inetcf = 0;
    int     count;
#ifdef INET6
    struct sockaddr_storage server_sin;
    struct sockaddr_storage client_sin;
#else
    struct sockaddr_in server_sin;
    struct sockaddr_in client_sin;
#endif
    struct stat st;

    /*
     * Show what rule actually matched.
     */
    hosts_access_verbose = 2;

    /*
     * Parse the JCL.
     */
    while ((ch = getopt(argc, argv, "di:")) != EOF) {
	switch (ch) {
	case 'd':
	    hosts_allow_table = "hosts.allow";
	    hosts_deny_table = "hosts.deny";
	    break;
	case 'i':
	    inetcf = optarg;
	    break;
	default:
	    usage(myname);
	    /* NOTREACHED */
	}
    }
    if (argc != optind + 2)
	usage(myname);

    /*
     * When confusion really strikes...
     */
    if (check_path(REAL_DAEMON_DIR, &st) < 0) {
	tcpd_warn("REAL_DAEMON_DIR %s: %m", REAL_DAEMON_DIR);
    } else if (!S_ISDIR(st.st_mode)) {
	tcpd_warn("REAL_DAEMON_DIR %s is not a directory", REAL_DAEMON_DIR);
    }

    /*
     * Default is to specify a daemon process name. When daemon@host is
     * specified, separate the two parts.
     */
    if ((server = split_at(argv[optind], '@')) == 0)
	server = unknown;
    if (argv[optind][0] == '/') {
	daemon = strrchr(argv[optind], '/') + 1;
	tcpd_warn("%s: daemon name normalized to: %s", argv[optind], daemon);
    } else {
	daemon = argv[optind];
    }

    /*
     * Default is to specify a client hostname or address. When user@host is
     * specified, separate the two parts.
     */
    if ((client = split_at(argv[optind + 1], '@')) != 0) {
	user = argv[optind + 1];
    } else {
	client = argv[optind + 1];
	user = unknown;
    }

    /*
     * Analyze the inetd (or tlid) configuration file, so that we can warn
     * the user about services that may not be wrapped, services that are not
     * configured, or services that are wrapped in an incorrect manner. Allow
     * for services that are not run from inetd, or that have tcpd access
     * control built into them.
     */
    inetcf = inet_cfg(inetcf);
    inet_set("portmap", WR_NOT);
    inet_set("rpcbind", WR_NOT);
    switch (inet_get(daemon)) {
    case WR_UNKNOWN:
	tcpd_warn("%s: no such process name in %s", daemon, inetcf);
	break;
    case WR_NOT:
	tcpd_warn("%s: service possibly not wrapped", daemon);
	break;
    }

    /*
     * Check accessibility of access control files.
     */
    (void) check_path(hosts_allow_table, &st);
    (void) check_path(hosts_deny_table, &st);

    /*
     * Fill in what we have figured out sofar. Use socket and DNS routines
     * for address and name conversions. We attach stdout to the request so
     * that banner messages will become visible.
     */
    request_init(&request, RQ_DAEMON, daemon, RQ_USER, user, RQ_FILE, 1, 0);
    sock_methods(&request);

    /*
     * If a server hostname is specified, insist that the name maps to at
     * most one address. eval_hostname() warns the user about name server
     * problems, while using the request.server structure as a cache for host
     * address and name conversion results.
     */
    if (NOT_INADDR(server) == 0 || HOSTNAME_KNOWN(server)) {
	if ((hp = find_inet_addr(server)) == 0)
	    exit(1);
#ifndef INET6
	memset((char *) &server_sin, 0, sizeof(server_sin));
	server_sin.sin_family = AF_INET;
#endif
	request_set(&request, RQ_SERVER_SIN, &server_sin, 0);

#ifdef INET6
	for (res = hp, count = 0; res; res = res->ai_next, count++) {
	    memcpy(&server_sin, res->ai_addr, res->ai_addrlen);
#else
	for (count = 0; (addr = hp->h_addr_list[count]) != 0; count++) {
	    memcpy((char *) &server_sin.sin_addr, addr,
		   sizeof(server_sin.sin_addr));
#endif

	    /*
	     * Force evaluation of server host name and address. Host name
	     * conflicts will be reported while eval_hostname() does its job.
	     */
	    request_set(&request, RQ_SERVER_NAME, "", RQ_SERVER_ADDR, "", 0);
	    if (STR_EQ(eval_hostname(request.server), unknown))
		tcpd_warn("host address %s->name lookup failed",
			  eval_hostaddr(request.server));
	}
	if (count > 1) {
	    fprintf(stderr, "Error: %s has more than one address\n", server);
	    fprintf(stderr, "Please specify an address instead\n");
	    exit(1);
	}
#ifdef INET6
	freeaddrinfo(hp);
#else
	free((char *) hp);
#endif
    } else {
	request_set(&request, RQ_SERVER_NAME, server, 0);
    }

    /*
     * If a client address is specified, we simulate the effect of client
     * hostname lookup failure.
     */
    if (dot_quad_addr(client) != INADDR_NONE) {
	request_set(&request, RQ_CLIENT_ADDR, client, 0);
	tcpdmatch(&request);
	exit(0);
    }
#ifdef INET6
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;
    if (getaddrinfo(client, NULL, &hints, &res) == 0) {
	freeaddrinfo(res);
	request_set(&request, RQ_CLIENT_ADDR, client, 0);
	tcpdmatch(&request);
	exit(0);
    }
#endif

    /*
     * Perhaps they are testing special client hostname patterns that aren't
     * really host names at all.
     */
    if (NOT_INADDR(client) && HOSTNAME_KNOWN(client) == 0) {
	request_set(&request, RQ_CLIENT_NAME, client, 0);
	tcpdmatch(&request);
	exit(0);
    }

    /*
     * Otherwise, assume that a client hostname is specified, and insist that
     * the address can be looked up. The reason for this requirement is that
     * in real life the client address is available (at least with IP). Let
     * eval_hostname() figure out if this host is properly registered, while
     * using the request.client structure as a cache for host name and
     * address conversion results.
     */
    if ((hp = find_inet_addr(client)) == 0)
	exit(1);
#ifdef INET6
    request_set(&request, RQ_CLIENT_SIN, &client_sin, 0);

    for (res = hp, count = 0; res; res = res->ai_next, count++) {
	memcpy(&client_sin, res->ai_addr, res->ai_addrlen);

	/*
	 * getnameinfo() doesn't do reverse lookup against link-local
	 * address.  So, we pass through host name evaluation against
	 * such addresses.
	 */
	if (res->ai_family != AF_INET6 ||
	    !IN6_IS_ADDR_LINKLOCAL(&((struct sockaddr_in6 *)res->ai_addr)->sin6_addr)) {
	    /*
	     * Force evaluation of client host name and address. Host name
	     * conflicts will be reported while eval_hostname() does its job.
	     */
	    request_set(&request, RQ_CLIENT_NAME, "", RQ_CLIENT_ADDR, "", 0);
	    if (STR_EQ(eval_hostname(request.client), unknown))
		tcpd_warn("host address %s->name lookup failed",
			  eval_hostaddr(request.client));
	}
	tcpdmatch(&request);
	if (res->ai_next)
	    printf("\n");
    }
    freeaddrinfo(hp);
#else
    memset((char *) &client_sin, 0, sizeof(client_sin));
    client_sin.sin_family = AF_INET;
    request_set(&request, RQ_CLIENT_SIN, &client_sin, 0);

    for (count = 0; (addr = hp->h_addr_list[count]) != 0; count++) {
	memcpy((char *) &client_sin.sin_addr, addr,
	       sizeof(client_sin.sin_addr));

	/*
	 * Force evaluation of client host name and address. Host name
	 * conflicts will be reported while eval_hostname() does its job.
	 */
	request_set(&request, RQ_CLIENT_NAME, "", RQ_CLIENT_ADDR, "", 0);
	if (STR_EQ(eval_hostname(request.client), unknown))
	    tcpd_warn("host address %s->name lookup failed",
		      eval_hostaddr(request.client));
	tcpdmatch(&request);
	if (hp->h_addr_list[count + 1])
	    printf("\n");
    }
    free((char *) hp);
#endif
    exit(0);
}

/* Explain how to use this program */

static void usage(myname)
char   *myname;
{
    fprintf(stderr, "usage: %s [-d] [-i inet_conf] daemon[@host] [user@]host\n",
	    myname);
    fprintf(stderr, "	-d: use allow/deny files in current directory\n");
    fprintf(stderr, "	-i: location of inetd.conf file\n");
    exit(1);
}

/* Print interesting expansions */

static void expand(text, pattern, request)
char   *text;
char   *pattern;
struct request_info *request;
{
    char    buf[BUFSIZ];

    if (STR_NE(percent_x(buf, sizeof(buf), pattern, request), unknown))
	printf("%s %s\n", text, buf);
}

/* Try out a (server,client) pair */

static void tcpdmatch(request)
struct request_info *request;
{
    int     verdict;

    /*
     * Show what we really know. Suppress uninteresting noise.
     */
    expand("client:   hostname", "%n", request);
    expand("client:   address ", "%a", request);
    expand("client:   username", "%u", request);
    expand("server:   hostname", "%N", request);
    expand("server:   address ", "%A", request);
    expand("server:   process ", "%d", request);

    /*
     * Reset stuff that might be changed by options handlers. In dry-run
     * mode, extension language routines that would not return should inform
     * us of their plan, by clearing the dry_run flag. This is a bit clumsy
     * but we must be able to verify hosts with more than one network
     * address.
     */
    rfc931_timeout = RFC931_TIMEOUT;
    allow_severity = SEVERITY;
    deny_severity = LOG_WARNING;
    dry_run = 1;

    /*
     * When paranoid mode is enabled, access is rejected no matter what the
     * access control rules say.
     */
#ifdef PARANOID
    if (STR_EQ(eval_hostname(request->client), paranoid)) {
	printf("access:   denied (PARANOID mode)\n\n");
	return;
    }
#endif

    /*
     * Report the access control verdict.
     */
    verdict = hosts_access(request);
    printf("access:   %s\n",
	   dry_run == 0 ? "delegated" :
	   verdict ? "granted" : "denied");
}
