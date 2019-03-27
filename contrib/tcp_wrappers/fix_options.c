 /*
  * Routine to disable IP-level socket options. This code was taken from 4.4BSD
  * rlogind and kernel source, but all mistakes in it are my fault.
  *
  * Author: Wietse Venema, Eindhoven University of Technology, The Netherlands.
  *
  * $FreeBSD$
  */

#ifndef lint
static char sccsid[] = "@(#) fix_options.c 1.6 97/04/08 02:29:19";
#endif

#include <sys/types.h>
#include <sys/param.h>
#ifdef INET6
#include <sys/socket.h>
#endif
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netdb.h>
#include <stdio.h>
#include <syslog.h>

#ifndef IPOPT_OPTVAL
#define IPOPT_OPTVAL	0
#define IPOPT_OLEN	1
#endif

#include "tcpd.h"

#define BUFFER_SIZE	512		/* Was: BUFSIZ */

/* fix_options - get rid of IP-level socket options */

void
fix_options(request)
struct request_info *request;
{
#ifdef IP_OPTIONS
    unsigned char optbuf[BUFFER_SIZE / 3], *cp;
    char    lbuf[BUFFER_SIZE], *lp;
    int     optsize = sizeof(optbuf), ipproto;
    struct protoent *ip;
    int     fd = request->fd;
    unsigned int opt;
    int     optlen;
    struct in_addr dummy;
#ifdef INET6
    struct sockaddr_storage ss;
    int sslen;

    /*
     * check if this is AF_INET socket
     * XXX IPv6 support?
     */
    sslen = sizeof(ss);
    if (getsockname(fd, (struct sockaddr *)&ss, &sslen) < 0) {
	syslog(LOG_ERR, "getpeername: %m");
	clean_exit(request);
    }
    if (ss.ss_family != AF_INET)
	return;
#endif

    if ((ip = getprotobyname("ip")) != 0)
	ipproto = ip->p_proto;
    else
	ipproto = IPPROTO_IP;

    if (getsockopt(fd, ipproto, IP_OPTIONS, (char *) optbuf, &optsize) == 0
	&& optsize != 0) {

	/*
	 * Horror! 4.[34] BSD getsockopt() prepends the first-hop destination
	 * address to the result IP options list when source routing options
	 * are present (see <netinet/ip_var.h>), but produces no output for
	 * other IP options. Solaris 2.x getsockopt() does produce output for
	 * non-routing IP options, and uses the same format as BSD even when
	 * the space for the destination address is unused. The code below
	 * does the right thing with 4.[34]BSD derivatives and Solaris 2, but
	 * may occasionally miss source routing options on incompatible
	 * systems such as Linux. Their choice.
	 * 
	 * Look for source routing options. Drop the connection when one is
	 * found. Just wiping the IP options is insufficient: we would still
	 * help the attacker by providing a real TCP sequence number, and the
	 * attacker would still be able to send packets (blind spoofing). I
	 * discussed this attack with Niels Provos, half a year before the
	 * attack was described in open mailing lists.
	 * 
	 * It would be cleaner to just return a yes/no reply and let the caller
	 * decide how to deal with it. Resident servers should not terminate.
	 * However I am not prepared to make changes to internal interfaces
	 * on short notice.
	 */
#define ADDR_LEN sizeof(dummy.s_addr)

	for (cp = optbuf + ADDR_LEN; cp < optbuf + optsize; cp += optlen) {
	    opt = cp[IPOPT_OPTVAL];
	    if (opt == IPOPT_LSRR || opt == IPOPT_SSRR) {
		syslog(LOG_WARNING,
		   "refused connect from %s with IP source routing options",
		       eval_client(request));
		shutdown(fd, 2);
		return;
	    }
	    if (opt == IPOPT_EOL)
		break;
	    if (opt == IPOPT_NOP) {
		optlen = 1;
	    } else {
		optlen = cp[IPOPT_OLEN];
		if (optlen <= 0)		/* Do not loop! */
		    break;
	    }
	}
	lp = lbuf;
	for (cp = optbuf; optsize > 0; cp++, optsize--, lp += 3)
	    sprintf(lp, " %2.2x", *cp);
	syslog(LOG_NOTICE,
	       "connect from %s with IP options (ignored):%s",
	       eval_client(request), lbuf);
	if (setsockopt(fd, ipproto, IP_OPTIONS, (char *) 0, optsize) != 0) {
	    syslog(LOG_ERR, "setsockopt IP_OPTIONS NULL: %m");
	    shutdown(fd, 2);
	}
    }
#endif
}
