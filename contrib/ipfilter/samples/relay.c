/*	$FreeBSD$	*/

/*
 * Sample program to be used as a transparent proxy.
 *
 * Must be executed with permission enough to do an ioctl on /dev/ipl
 * or equivalent.  This is just a sample and is only alpha quality.
 * - Darren Reed (8 April 1996)
 */
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/syslog.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include "netinet/ip_compat.h"
#include "netinet/ip_fil.h"
#include "netinet/ip_nat.h"
#include "netinet/ipl.h"

#define	RELAY_BUFSZ	8192

char	ibuff[RELAY_BUFSZ];
char	obuff[RELAY_BUFSZ];

int relay(ifd, ofd, rfd)
	int ifd, ofd, rfd;
{
	fd_set	rfds, wfds;
	char	*irh, *irt, *rrh, *rrt;
	char	*iwh, *iwt, *rwh, *rwt;
	int	nfd, n, rw;

	irh = irt = ibuff;
	iwh = iwt = obuff;
	nfd = ifd;
	if (nfd < ofd)
		nfd = ofd;
	if (nfd < rfd)
		nfd = rfd;

	while (1) {
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		if (irh > irt)
			FD_SET(rfd, &wfds);
		if (irh < (ibuff + RELAY_BUFSZ))
			FD_SET(ifd, &rfds);
		if (iwh > iwt)
			FD_SET(ofd, &wfds);
		if (iwh < (obuff + RELAY_BUFSZ))
			FD_SET(rfd, &rfds);

		switch ((n = select(nfd + 1, &rfds, &wfds, NULL, NULL)))
		{
		case -1 :
		case 0 :
			return -1;
		default :
			if (FD_ISSET(ifd, &rfds)) {
				rw = read(ifd, irh, ibuff + RELAY_BUFSZ - irh);
				if (rw == -1)
					return -1;
				if (rw == 0)
					return 0;
				irh += rw;
				n--;
			}
			if (n && FD_ISSET(ofd, &wfds)) {
				rw = write(ofd, iwt, iwh  - iwt);
				if (rw == -1)
					return -1;
				iwt += rw;
				n--;
			}
			if (n && FD_ISSET(rfd, &rfds)) {
				rw = read(rfd, iwh, obuff + RELAY_BUFSZ - iwh);
				if (rw == -1)
					return -1;
				if (rw == 0)
					return 0;
				iwh += rw;
				n--;
			}
			if (n && FD_ISSET(rfd, &wfds)) {
				rw = write(rfd, irt, irh  - irt);
				if (rw == -1)
					return -1;
				irt += rw;
				n--;
			}
			if (irh == irt)
				irh = irt = ibuff;
			if (iwh == iwt)
				iwh = iwt = obuff;
		}
	}
}

main(argc, argv)
	int argc;
	char *argv[];
{
	struct	sockaddr_in	sin;
	ipfobj_t	obj;
	natlookup_t	nl;
	natlookup_t	*nlp = &nl;
	int	fd, sl = sizeof(sl), se;

	openlog(argv[0], LOG_PID|LOG_NDELAY, LOG_DAEMON);
	if ((fd = open(IPNAT_NAME, O_RDONLY)) == -1) {
		se = errno;
		perror("open");
		errno = se;
		syslog(LOG_ERR, "open: %m\n");
		exit(-1);
	}

	bzero(&obj, sizeof(obj));
	obj.ipfo_rev = IPFILTER_VERSION;
	obj.ipfo_size = sizeof(nl);
	obj.ipfo_ptr = &nl;
	obj.ipfo_type = IPFOBJ_NATLOOKUP;

	bzero(&nl, sizeof(nl));
	nl.nl_flags = IPN_TCP;

	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sl = sizeof(sin);
	if (getsockname(0, (struct sockaddr *)&sin, &sl) == -1) {
		se = errno;
		perror("getsockname");
		errno = se;
		syslog(LOG_ERR, "getsockname: %m\n");
		exit(-1);
	} else {
		nl.nl_inip.s_addr = sin.sin_addr.s_addr;
		nl.nl_inport = sin.sin_port;
	}

	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sl = sizeof(sin);
	if (getpeername(0, (struct sockaddr *)&sin, &sl) == -1) {
		se = errno;
		perror("getpeername");
		errno = se;
		syslog(LOG_ERR, "getpeername: %m\n");
		exit(-1);
	} else {
		nl.nl_outip.s_addr = sin.sin_addr.s_addr;
		nl.nl_outport = sin.sin_port;
	}

	if (ioctl(fd, SIOCGNATL, &obj) == -1) {
		se = errno;
		perror("ioctl");
		errno = se;
		syslog(LOG_ERR, "ioctl: %m\n");
		exit(-1);
	}

	sin.sin_port = nl.nl_realport;
	sin.sin_addr = nl.nl_realip;
	sl = sizeof(sin);

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (connect(fd, (struct sockaddr *)&sin, sl) == -1) {
		se = errno;
		perror("connect");
		errno = se;
		syslog(LOG_ERR, "connect: %m\n");
		exit(-1);
	}

	(void) ioctl(fd, F_SETFL, ioctl(fd, F_GETFL, 0)|O_NONBLOCK);
	(void) ioctl(0, F_SETFL, ioctl(fd, F_GETFL, 0)|O_NONBLOCK);
	(void) ioctl(1, F_SETFL, ioctl(fd, F_GETFL, 0)|O_NONBLOCK);

	syslog(LOG_NOTICE, "connected to %s,%d\n", inet_ntoa(sin.sin_addr),
		ntohs(sin.sin_port));
	if (relay(0, 1, fd) == -1) {
		se = errno;
		perror("relay");
		errno = se;
		syslog(LOG_ERR, "relay: %m\n");
		exit(-1);
	}
	exit(0);
}
