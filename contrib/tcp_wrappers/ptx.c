 /*
  * The Dynix/PTX TLI implementation is not quite compatible with System V
  * Release 4. Some important functions are not present so we are limited to
  * IP-based services.
  * 
  * Diagnostics are reported through syslog(3).
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) ptx.c 1.3 94/12/28 17:42:38";
#endif

#ifdef PTX

/* System libraries. */

#include <sys/types.h>
#include <sys/tiuser.h>
#include <sys/socket.h>
#include <stropts.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <syslog.h>

/* Local stuff. */

#include "tcpd.h"

/* Forward declarations. */

static void ptx_sink();

/* tli_host - determine TLI endpoint info, PTX version */

void    tli_host(request)
struct request_info *request;
{
    static struct sockaddr_in client;
    static struct sockaddr_in server;

    /*
     * getpeerinaddr() was suggested by someone at Sequent. It seems to work
     * with connection-oriented (TCP) services such as rlogind and telnetd,
     * but it returns 0.0.0.0 with datagram (UDP) services. No problem: UDP
     * needs special treatment anyway, in case we must refuse service.
     */

    if (getpeerinaddr(request->fd, &client, sizeof(client)) == 0
	&& client.sin_addr.s_addr != 0) {
	request->client->sin = &client;
	if (getmyinaddr(request->fd, &server, sizeof(server)) == 0) {
	    request->server->sin = &server;
	} else {
	    tcpd_warn("warning: getmyinaddr: %m");
	}
	sock_methods(request);

    } else {

	/*
	 * Another suggestion was to temporarily switch to the socket
	 * interface, identify the endpoint addresses with socket calls, then
	 * to switch back to TLI. This seems to works OK with UDP services,
	 * which is exactly what we should be looking at right now.
	 */

#define SWAP_MODULE(f, old, new) (ioctl(f, I_POP, old), ioctl(f, I_PUSH, new))

	if (SWAP_MODULE(request->fd, "timod", "sockmod") != 0)
	    tcpd_warn("replace timod by sockmod: %m");
	sock_host(request);
	if (SWAP_MODULE(request->fd, "sockmod", "timod") != 0)
	    tcpd_warn("replace sockmod by timod: %m");
	if (request->sink != 0)
	    request->sink = ptx_sink;
    }
}

/* ptx_sink - absorb unreceived IP datagram */

static void ptx_sink(fd)
int     fd;
{
    char    buf[BUFSIZ];
    struct sockaddr sa;
    int     size = sizeof(sa);

    /*
     * Eat up the not-yet received datagram. Where needed, switch to the
     * socket programming interface.
     */

    if (ioctl(fd, I_FIND, "timod") != 0)
	ioctl(fd, I_POP, "timod");
    if (ioctl(fd, I_FIND, "sockmod") == 0)
	ioctl(fd, I_PUSH, "sockmod");
    (void) recvfrom(fd, buf, sizeof(buf), 0, &sa, &size);
}

#endif /* PTX */
