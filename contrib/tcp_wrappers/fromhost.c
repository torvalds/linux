 /*
  * On socket-only systems, fromhost() is nothing but an alias for the
  * socket-specific sock_host() function.
  * 
  * On systems with sockets and TLI, fromhost() determines the type of API
  * (sockets, TLI), then invokes the appropriate API-specific routines.
  * 
  * Diagnostics are reported through syslog(3).
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) fromhost.c 1.17 94/12/28 17:42:23";
#endif

#if defined(TLI) || defined(PTX) || defined(TLI_SEQUENT)

/* System libraries. */

#include <sys/types.h>
#include <sys/tiuser.h>
#include <stropts.h>

/* Local stuff. */

#include "tcpd.h"

/* fromhost - find out what network API we should use */

void    fromhost(request)
struct request_info *request;
{

    /*
     * On systems with streams support the IP network protocol family may be
     * accessible via more than one programming interface: Berkeley sockets
     * and the Transport Level Interface (TLI).
     * 
     * Thus, we must first find out what programming interface to use: sockets
     * or TLI. On some systems, sockets are not part of the streams system,
     * so if request->fd is not a stream we simply assume sockets.
     */

    if (ioctl(request->fd, I_FIND, "timod") > 0) {
	tli_host(request);
    } else {
	sock_host(request);
    }
}

#endif /* TLI || PTX || TLI_SEQUENT */
