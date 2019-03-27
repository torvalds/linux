 /*
  * tli_host() determines the type of transport (connected, connectionless),
  * the transport address of a client host, and the transport address of a
  * server endpoint. In addition, it provides methods to map a transport
  * address to a printable host name or address. Socket address results are
  * in static memory; tli structures are allocated from the heap.
  * 
  * The result from the hostname lookup method is STRING_PARANOID when a host
  * pretends to have someone elses name, or when a host name is available but
  * could not be verified.
  * 
  * Diagnostics are reported through syslog(3).
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  *
  * $FreeBSD$
  */

#ifndef lint
static char sccsid[] = "@(#) tli.c 1.15 97/03/21 19:27:25";
#endif

#ifdef TLI

/* System libraries. */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stream.h>
#include <sys/stat.h>
#include <sys/mkdev.h>
#include <sys/tiuser.h>
#include <sys/timod.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <netconfig.h>
#include <netdir.h>
#include <string.h>

extern char *nc_sperror();
extern int errno;
extern char *sys_errlist[];
extern int sys_nerr;
extern int t_errno;
extern char *t_errlist[];
extern int t_nerr;

/* Local stuff. */

#include "tcpd.h"

/* Forward declarations. */

static void tli_endpoints();
static struct netconfig *tli_transport();
static void tli_hostname();
static void tli_hostaddr();
static void tli_cleanup();
static char *tli_error();
static void tli_sink();

/* tli_host - look up endpoint addresses and install conversion methods */

void    tli_host(request)
struct request_info *request;
{
#ifdef INET6
    static struct sockaddr_storage client;
    static struct sockaddr_storage server;
#else
    static struct sockaddr_in client;
    static struct sockaddr_in server;
#endif

    /*
     * If we discover that we are using an IP transport, pretend we never
     * were here. Otherwise, use the transport-independent method and stick
     * to generic network addresses. XXX hard-coded protocol family name.
     */

    tli_endpoints(request);
#ifdef INET6
    if ((request->config = tli_transport(request->fd)) != 0
	&& (STR_EQ(request->config->nc_protofmly, "inet") ||
	    STR_EQ(request->config->nc_protofmly, "inet6"))) {
#else
    if ((request->config = tli_transport(request->fd)) != 0
        && STR_EQ(request->config->nc_protofmly, "inet")) {
#endif
	if (request->client->unit != 0) {
#ifdef INET6
	    client = *(struct sockaddr_storage *) request->client->unit->addr.buf;
	    request->client->sin = (struct sockaddr *) &client;
#else
	    client = *(struct sockaddr_in *) request->client->unit->addr.buf;
	    request->client->sin = &client;
#endif
	}
	if (request->server->unit != 0) {
#ifdef INET6
            server = *(struct sockaddr_storage *) request->server->unit->addr.buf;
            request->server->sin = (struct sockaddr *) &server;
#else
            server = *(struct sockaddr_in *) request->server->unit->addr.buf;
            request->server->sin = &server;
#endif
	}
	tli_cleanup(request);
	sock_methods(request);
    } else {
	request->hostname = tli_hostname;
	request->hostaddr = tli_hostaddr;
	request->cleanup = tli_cleanup;
    }
}

/* tli_cleanup - cleanup some dynamically-allocated data structures */

static void tli_cleanup(request)
struct request_info *request;
{
    if (request->config != 0)
	freenetconfigent(request->config);
    if (request->client->unit != 0)
	t_free((char *) request->client->unit, T_UNITDATA);
    if (request->server->unit != 0)
	t_free((char *) request->server->unit, T_UNITDATA);
}

/* tli_endpoints - determine TLI client and server endpoint information */

static void tli_endpoints(request)
struct request_info *request;
{
    struct t_unitdata *server;
    struct t_unitdata *client;
    int     fd = request->fd;
    int     flags;

    /*
     * Determine the client endpoint address. With unconnected services, peek
     * at the sender address of the pending protocol data unit without
     * popping it off the receive queue. This trick works because only the
     * address member of the unitdata structure has been allocated.
     * 
     * Beware of successful returns with zero-length netbufs (for example,
     * Solaris 2.3 with ticlts transport). The netdir(3) routines can't
     * handle that. Assume connection-less transport when TI_GETPEERNAME
     * produces no usable result, even when t_rcvudata() is unable to figure
     * out the peer address. Better to hang than to loop.
     */

    if ((client = (struct t_unitdata *) t_alloc(fd, T_UNITDATA, T_ADDR)) == 0) {
	tcpd_warn("t_alloc: %s", tli_error());
	return;
    }
    if (ioctl(fd, TI_GETPEERNAME, &client->addr) < 0 || client->addr.len == 0) {
	request->sink = tli_sink;
	if (t_rcvudata(fd, client, &flags) < 0 || client->addr.len == 0) {
	    tcpd_warn("can't get client address: %s", tli_error());
	    t_free((void *) client, T_UNITDATA);
	    return;
	}
    }
    request->client->unit = client;

    /*
     * Look up the server endpoint address. This can be used for filtering on
     * server address or name, or to look up the client user.
     */

    if ((server = (struct t_unitdata *) t_alloc(fd, T_UNITDATA, T_ADDR)) == 0) {
	tcpd_warn("t_alloc: %s", tli_error());
	return;
    }
    if (ioctl(fd, TI_GETMYNAME, &server->addr) < 0) {
	tcpd_warn("TI_GETMYNAME: %m");
	t_free((void *) server, T_UNITDATA);
	return;
    }
    request->server->unit = server;
}

/* tli_transport - find out TLI transport type */

static struct netconfig *tli_transport(fd)
int     fd;
{
    struct stat from_client;
    struct stat from_config;
    void   *handlep;
    struct netconfig *config;

    /*
     * Assuming that the network device is a clone device, we must compare
     * the major device number of stdin to the minor device number of the
     * devices listed in the netconfig table.
     */

    if (fstat(fd, &from_client) != 0) {
	tcpd_warn("fstat(fd %d): %m", fd);
	return (0);
    }
    if ((handlep = setnetconfig()) == 0) {
	tcpd_warn("setnetconfig: %m");
	return (0);
    }
    while (config = getnetconfig(handlep)) {
	if (stat(config->nc_device, &from_config) == 0) {
#ifdef NO_CLONE_DEVICE
	/*
	 * If the network devices are not cloned (as is the case for
	 * Solaris 8 Beta), we must compare the major device numbers.
	 */
	    if (major(from_config.st_rdev) == major(from_client.st_rdev))
#else
	    if (minor(from_config.st_rdev) == major(from_client.st_rdev))
#endif
		break;
	}
    }
    if (config == 0) {
	tcpd_warn("unable to identify transport protocol");
	return (0);
    }

    /*
     * Something else may clobber our getnetconfig() result, so we'd better
     * acquire our private copy.
     */

    if ((config = getnetconfigent(config->nc_netid)) == 0) {
	tcpd_warn("getnetconfigent(%s): %s", config->nc_netid, nc_sperror());
	return (0);
    }
    return (config);
}

/* tli_hostaddr - map TLI transport address to printable address */

static void tli_hostaddr(host)
struct host_info *host;
{
    struct request_info *request = host->request;
    struct netconfig *config = request->config;
    struct t_unitdata *unit = host->unit;
    char   *uaddr;

    if (config != 0 && unit != 0
	&& (uaddr = taddr2uaddr(config, &unit->addr)) != 0) {
	STRN_CPY(host->addr, uaddr, sizeof(host->addr));
	free(uaddr);
    }
}

/* tli_hostname - map TLI transport address to hostname */

static void tli_hostname(host)
struct host_info *host;
{
    struct request_info *request = host->request;
    struct netconfig *config = request->config;
    struct t_unitdata *unit = host->unit;
    struct nd_hostservlist *servlist;

    if (config != 0 && unit != 0
	&& netdir_getbyaddr(config, &servlist, &unit->addr) == ND_OK) {

	struct nd_hostserv *service = servlist->h_hostservs;
	struct nd_addrlist *addr_list;
	int     found = 0;

	if (netdir_getbyname(config, service, &addr_list) != ND_OK) {

	    /*
	     * Unable to verify that the name matches the address. This may
	     * be a transient problem or a botched name server setup. We
	     * decide to play safe.
	     */

	    tcpd_warn("can't verify hostname: netdir_getbyname(%.*s) failed",
		      STRING_LENGTH, service->h_host);

	} else {

	    /*
	     * Look up the host address in the address list we just got. The
	     * comparison is done on the textual representation, because the
	     * transport address is an opaque structure that may have holes
	     * with uninitialized garbage. This approach obviously loses when
	     * the address does not have a textual representation.
	     */

	    char   *uaddr = eval_hostaddr(host);
	    char   *ua;
	    int     i;

	    for (i = 0; found == 0 && i < addr_list->n_cnt; i++) {
		if ((ua = taddr2uaddr(config, &(addr_list->n_addrs[i]))) != 0) {
		    found = !strcmp(ua, uaddr);
		    free(ua);
		}
	    }
	    netdir_free((void *) addr_list, ND_ADDRLIST);

	    /*
	     * When the host name does not map to the initial address, assume
	     * someone has compromised a name server. More likely someone
	     * botched it, but that could be dangerous, too.
	     */

	    if (found == 0)
		tcpd_warn("host name/address mismatch: %s != %.*s",
			  host->addr, STRING_LENGTH, service->h_host);
	}
	STRN_CPY(host->name, found ? service->h_host : paranoid,
		 sizeof(host->name));
	netdir_free((void *) servlist, ND_HOSTSERVLIST);
    }
}

/* tli_error - convert tli error number to text */

static char *tli_error()
{
    static char buf[40];

    if (t_errno != TSYSERR) {
	if (t_errno < 0 || t_errno >= t_nerr) {
	    sprintf(buf, "Unknown TLI error %d", t_errno);
	    return (buf);
	} else {
	    return (t_errlist[t_errno]);
	}
    } else {
	if (errno < 0 || errno >= sys_nerr) {
	    sprintf(buf, "Unknown UNIX error %d", errno);
	    return (buf);
	} else {
	    return (sys_errlist[errno]);
	}
    }
}

/* tli_sink - absorb unreceived datagram */

static void tli_sink(fd)
int     fd;
{
    struct t_unitdata *unit;
    int     flags;

    /*
     * Something went wrong. Absorb the datagram to keep inetd from looping.
     * Allocate storage for address, control and data. If that fails, sleep
     * for a couple of seconds in an attempt to keep inetd from looping too
     * fast.
     */

    if ((unit = (struct t_unitdata *) t_alloc(fd, T_UNITDATA, T_ALL)) == 0) {
	tcpd_warn("t_alloc: %s", tli_error());
	sleep(5);
    } else {
	(void) t_rcvudata(fd, unit, &flags);
	t_free((void *) unit, T_UNITDATA);
    }
}

#endif /* TLI */
