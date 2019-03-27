/*	$FreeBSD$	*/

/*
 * Sample transparent proxy program.
 *
 * Sample implementation of a program which intercepts a TCP connectiona and
 * just echos all data back to the origin.  Written to work via inetd as a
 * "nonwait" program running as root; ie.
 * tcpmux          stream  tcp     nowait root /usr/local/bin/proxy proxy
 * with a NAT rue like this:
 * rdr smc0 0/0 port 80 -> 127.0.0.1/32 port 1
 */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <syslog.h>
#if !defined(__SVR4) && !defined(__svr4__)
#include <strings.h>
#else
#include <sys/byteorder.h>
#endif
#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#if defined(sun) && (defined(__svr4__) || defined(__SVR4))
# include <sys/ioccom.h>
# include <sys/sysmacros.h>
#endif
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <net/if.h>
#include <netdb.h>
#include <arpa/nameser.h>
#include <arpa/inet.h>
#include <resolv.h>
#include <ctype.h>
#include "netinet/ip_compat.h"
#include "netinet/ip_fil.h"
#include "netinet/ip_nat.h"
#include "netinet/ip_state.h"
#include "netinet/ip_proxy.h"
#include "netinet/ip_nat.h"
#include "netinet/ipl.h"


main(argc, argv)
	int argc;
	char *argv[];
{
	struct	sockaddr_in	sin, sloc, sout;
	ipfobj_t	obj;
	natlookup_t	natlook;
	char	buffer[512];
	int	namelen, fd, n;

	/*
	 * get IP# and port # of the remote end of the connection (at the
	 * origin).
	 */
	namelen = sizeof(sin);
	if (getpeername(0, (struct sockaddr *)&sin, &namelen) == -1) {
		perror("getpeername");
		exit(-1);
	}

	/*
	 * get IP# and port # of the local end of the connection (at the
	 * man-in-the-middle).
	 */
	namelen = sizeof(sin);
	if (getsockname(0, (struct sockaddr *)&sloc, &namelen) == -1) {
		perror("getsockname");
		exit(-1);
	}

	bzero((char *)&obj, sizeof(obj));
	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_size = sizeof(natlook);
	obj.ipfo_ptr = &natlook;
	obj.ipfo_type = IPFOBJ_NATLOOKUP;

	/*
	 * Build up the NAT natlookup structure.
	 */
	bzero((char *)&natlook, sizeof(natlook));
	natlook.nl_outip = sin.sin_addr;
	natlook.nl_inip = sloc.sin_addr;
	natlook.nl_flags = IPN_TCP;
	natlook.nl_outport = sin.sin_port;
	natlook.nl_inport = sloc.sin_port;

	/*
	 * Open the NAT device and lookup the mapping pair.
	 */
	fd = open(IPNAT_NAME, O_RDONLY);
	if (ioctl(fd, SIOCGNATL, &obj) == -1) {
		perror("ioctl(SIOCGNATL)");
		exit(-1);
	}

#define	DO_NAT_OUT
#ifdef	DO_NAT_OUT
	if (argc > 1)
		do_nat_out(0, 1, fd, &natlook, argv[1]);
#else

	/*
	 * Log it
	 */
	syslog(LOG_DAEMON|LOG_INFO, "connect to %s,%d",
		inet_ntoa(natlook.nl_realip), ntohs(natlook.nl_realport));
	printf("connect to %s,%d\n",
		inet_ntoa(natlook.nl_realip), ntohs(natlook.nl_realport));

	/*
	 * Just echo data read in from stdin to stdout
	 */
	while ((n = read(0, buffer, sizeof(buffer))) > 0)
		if (write(1, buffer, n) != n)
			break;
	close(0);
#endif
}


#ifdef	DO_NAT_OUT
do_nat_out(in, out, fd, nlp, extif)
	int fd;
	natlookup_t *nlp;
	char *extif;
{
	nat_save_t ns, *nsp = &ns;
	struct sockaddr_in usin;
	u_32_t sum1, sum2, sumd;
	int onoff, ofd, slen;
	ipfobj_t obj;
	ipnat_t *ipn;
	nat_t *nat;

	bzero((char *)&ns, sizeof(ns));

	nat = &ns.ipn_nat;
	nat->nat_p = IPPROTO_TCP;
	nat->nat_dir = NAT_OUTBOUND;
	if ((extif != NULL) && (*extif != '\0')) {
		strncpy(nat->nat_ifnames[0], extif,
			sizeof(nat->nat_ifnames[0]));
		strncpy(nat->nat_ifnames[1], extif,
			sizeof(nat->nat_ifnames[1]));
		nat->nat_ifnames[0][sizeof(nat->nat_ifnames[0]) - 1] = '\0';
		nat->nat_ifnames[1][sizeof(nat->nat_ifnames[1]) - 1] = '\0';
	}

	ofd = socket(AF_INET, SOCK_DGRAM, 0);
	bzero((char *)&usin, sizeof(usin));
	usin.sin_family = AF_INET;
	usin.sin_addr = nlp->nl_realip;
	usin.sin_port = nlp->nl_realport;
	(void) connect(ofd, (struct sockaddr *)&usin, sizeof(usin));
	slen = sizeof(usin);
	(void) getsockname(ofd, (struct sockaddr *)&usin, &slen);
	close(ofd);
printf("local IP# to use: %s\n", inet_ntoa(usin.sin_addr));

	if ((ofd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
		perror("socket");
	usin.sin_port = 0;
	if (bind(ofd, (struct sockaddr *)&usin, sizeof(usin)))
		perror("bind");
	slen = sizeof(usin);
	if (getsockname(ofd, (struct sockaddr *)&usin, &slen))
		perror("getsockname");
printf("local port# to use: %d\n", ntohs(usin.sin_port));

	nat->nat_inip = usin.sin_addr;
	nat->nat_outip = nlp->nl_outip;
	nat->nat_oip = nlp->nl_realip;

	sum1 = LONG_SUM(ntohl(usin.sin_addr.s_addr)) + ntohs(usin.sin_port);
	sum2 = LONG_SUM(ntohl(nat->nat_outip.s_addr)) + ntohs(nlp->nl_outport);
	CALC_SUMD(sum1, sum2, sumd);
	nat->nat_sumd[0] = (sumd & 0xffff) + (sumd >> 16);
	nat->nat_sumd[1] = nat->nat_sumd[0];

	sum1 = LONG_SUM(ntohl(usin.sin_addr.s_addr));
	sum2 = LONG_SUM(ntohl(nat->nat_outip.s_addr));
	CALC_SUMD(sum1, sum2, sumd);
	nat->nat_ipsumd = (sumd & 0xffff) + (sumd >> 16);

	nat->nat_inport = usin.sin_port;
	nat->nat_outport = nlp->nl_outport;
	nat->nat_oport = nlp->nl_realport;

	nat->nat_flags = IPN_TCPUDP;

	bzero((char *)&obj, sizeof(obj));
	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_size = sizeof(*nsp);
	obj.ipfo_ptr = nsp;
	obj.ipfo_type = IPFOBJ_NATSAVE;

	onoff = 1;
	if (ioctl(fd, SIOCSTLCK, &onoff) == 0) {
		if (ioctl(fd, SIOCSTPUT, &obj) != 0)
			perror("SIOCSTPUT");
		onoff = 0;
		if (ioctl(fd, SIOCSTLCK, &onoff) != 0)
			perror("SIOCSTLCK");
	}

	usin.sin_addr = nlp->nl_realip;
	usin.sin_port = nlp->nl_realport;
printf("remote end for connection: %s,%d\n", inet_ntoa(usin.sin_addr),
ntohs(usin.sin_port));
fflush(stdout);
	if (connect(ofd, (struct sockaddr *)&usin, sizeof(usin)))
		perror("connect");

	relay(in, out, ofd);
}


relay(in, out, net)
	int in, out, net;
{
	char netbuf[1024], outbuf[1024];
	char *nwptr, *nrptr, *owptr, *orptr;
	size_t nsz, osz;
	fd_set rd, wr;
	int i, n, maxfd;

	n = 0;
	maxfd = in;
	if (out > maxfd)
		maxfd = out;
	if (net > maxfd)
		maxfd = net;

	nrptr = netbuf;
	nwptr = netbuf;
	nsz = sizeof(netbuf);
	orptr = outbuf;
	owptr = outbuf;
	osz = sizeof(outbuf);

	while (n >= 0) {
		FD_ZERO(&rd);
		FD_ZERO(&wr);

		if (nrptr - netbuf < sizeof(netbuf))
			FD_SET(in, &rd);
		if (orptr - outbuf < sizeof(outbuf))
			FD_SET(net, &rd);

		if (nsz < sizeof(netbuf))
			FD_SET(net, &wr);
		if (osz < sizeof(outbuf))
			FD_SET(out, &wr);

		n = select(maxfd + 1, &rd, &wr, NULL, NULL);

		if ((n > 0) && FD_ISSET(in, &rd)) {
			i = read(in, nrptr, sizeof(netbuf) - (nrptr - netbuf));
			if (i <= 0)
				break;
			nsz -= i;
			nrptr += i;
			n--;
		}

		if ((n > 0) && FD_ISSET(net, &rd)) {
			i = read(net, orptr, sizeof(outbuf) - (orptr - outbuf));
			if (i <= 0)
				break;
			osz -= i;
			orptr += i;
			n--;
		}

		if ((n > 0) && FD_ISSET(out, &wr)) {
			i = write(out, owptr, orptr - owptr);
			if (i <= 0)
				break;
			osz += i;
			if (osz == sizeof(outbuf) || owptr == orptr) {
				orptr = outbuf;
				owptr = outbuf;
			} else
				owptr += i;
			n--;
		}

		if ((n > 0) && FD_ISSET(net, &wr)) {
			i = write(net, nwptr, nrptr - nwptr);
			if (i <= 0)
				break;
			nsz += i;
			if (nsz == sizeof(netbuf) || nwptr == nrptr) {
				nrptr = netbuf;
				nwptr = netbuf;
			} else
				nwptr += i;
		}
	}

	close(net);
	close(out);
	close(in);
}
#endif
