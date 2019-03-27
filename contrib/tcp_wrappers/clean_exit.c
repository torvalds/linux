 /*
  * clean_exit() cleans up and terminates the program. It should be called
  * instead of exit() when for some reason the real network daemon will not or
  * cannot be run. Reason: in the case of a datagram-oriented service we must
  * discard the not-yet received data from the client. Otherwise, inetd will
  * see the same datagram again and again, and go into a loop.
  * 
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  */

#ifndef lint
static char sccsid[] = "@(#) clean_exit.c 1.4 94/12/28 17:42:19";
#endif

#include <stdio.h>
#include <unistd.h>

extern void exit();

#include "tcpd.h"

/* clean_exit - clean up and exit */

void    clean_exit(request)
struct request_info *request;
{

    /*
     * In case of unconnected protocols we must eat up the not-yet received
     * data or inetd will loop.
     */

    if (request->sink)
	request->sink(request->fd);

    /*
     * Be kind to the inetd. We already reported the problem via the syslogd,
     * and there is no need for additional garbage in the logfile.
     */

    sleep(5);
    exit(0);
}
