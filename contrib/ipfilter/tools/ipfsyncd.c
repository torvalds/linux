/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#if !defined(lint)
static const char sccsid[] = "@(#)ip_fil.c	2.41 6/5/96 (C) 1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$Id: ipfsyncd.c,v 1.1.2.2 2012/07/22 08:04:24 darren_r Exp $";
#endif
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sockio.h>
#include <sys/errno.h>

#include <netinet/in.h>
#include <net/if.h>

#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <signal.h>

#include "ipf.h"
#include "opts.h"


#define	R_IO_ERROR	-1
#define	R_OKAY		0
#define	R_MORE		1
#define	R_SKIP		2
#if	defined(sun) && !defined(SOLARIS2)
# define	STRERROR(x)     sys_errlist[x]
extern  char    *sys_errlist[];
#else
# define	STRERROR(x)     strerror(x)
#endif


int	main __P((int, char *[]));
void	usage __P((char *));
void	printsynchdr __P((synchdr_t *));
void	printtable __P((int));
void	printsmcproto __P((char *));
void	printcommand __P((int));
int	do_kbuff __P((int, char *, int *));
int	do_packet __P((int, char *));
int	buildsocket __P((char *, struct sockaddr_in *));
void	do_io __P((void));
void	handleterm __P((int));

int	terminate = 0;
int	igmpfd = -1;
int	nfd = -1;
int	lfd = -1;
int	opts = 0;

void
usage(progname)
	char *progname;
{
	fprintf(stderr,
		"Usage: %s [-d] [-p port] [-i address] -I <interface>\n",
		progname);
}

void
handleterm(sig)
	int sig;
{
	terminate = sig;
}


/* should be large enough to hold header + any datatype */
#define BUFFERLEN 1400

int
main(argc, argv)
	int argc;
	char *argv[];
{
	struct sockaddr_in sin;
	char *interface;
	char *progname;
	int opt, tries;

	progname = strrchr(argv[0], '/');
	if (progname) {
		progname++;
	} else {
		progname = argv[0];
	}

	opts = 0;
	tries = 0;
	interface = NULL;

	bzero((char *)&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_port = htons(0xaf6c);
	sin.sin_addr.s_addr = htonl(INADDR_UNSPEC_GROUP | 0x697066);

	while ((opt = getopt(argc, argv, "di:I:p:")) != -1)
		switch (opt)
		{
		case 'd' :
			debuglevel++;
			break;
		case 'I' :
			interface = optarg;
			break;
		case 'i' :
			sin.sin_addr.s_addr = inet_addr(optarg);
			break;
		case 'p' :
			sin.sin_port = htons(atoi(optarg));
			break;
		}

	if (interface == NULL) {
		usage(progname);
		exit(1);
	}

	if (!debuglevel) {

#if BSD >= 199306
		daemon(0, 0);
#else
		int fd = open("/dev/null", O_RDWR);

		switch (fork())
		{
		case 0 :
			break;

		case -1 :
			fprintf(stderr, "%s: fork() failed: %s\n",
				argv[0], STRERROR(errno));
			exit(1);
			/* NOTREACHED */

		default :
			exit(0);
			/* NOTREACHED */
		}

		dup2(fd, 0);
		dup2(fd, 1);
		dup2(fd, 2);
		close(fd);

		setsid();
#endif
	}

       	signal(SIGHUP, handleterm);
       	signal(SIGINT, handleterm);
       	signal(SIGTERM, handleterm);

	openlog(progname, LOG_PID, LOG_SECURITY);

	while (!terminate) {
		if (lfd != -1) {
			close(lfd);
			lfd = -1;
		}
		if (nfd != -1) {
			close(nfd);
			nfd = -1;
		}
		if (igmpfd != -1) {
			close(igmpfd);
			igmpfd = -1;
		}

		if (buildsocket(interface, &sin) == -1)
			goto tryagain;

		lfd = open(IPSYNC_NAME, O_RDWR);
		if (lfd == -1) {
			syslog(LOG_ERR, "open(%s):%m", IPSYNC_NAME);
			debug(1, "open(%s): %s\n", IPSYNC_NAME,
			      STRERROR(errno));
			goto tryagain;
		}

		tries = -1;
		do_io();
tryagain:
		tries++;
		syslog(LOG_INFO, "retry in %d seconds", 1 << tries);
		debug(1, "wait %d seconds\n", 1 << tries);
		sleep(1 << tries);
	}


	/* terminate */
	if (lfd != -1)
		close(lfd);
	if (nfd != -1)
		close(nfd);

	syslog(LOG_ERR, "signal %d received, exiting...", terminate);
	debug(1, "signal %d received, exiting...", terminate);

	exit(1);
}


void
do_io()
{
	char nbuff[BUFFERLEN];
	char buff[BUFFERLEN];
	fd_set mrd, rd;
	int maxfd;
	int inbuf;
	int n1;
	int left;

	FD_ZERO(&mrd);
	FD_SET(lfd, &mrd);
	FD_SET(nfd, &mrd);
	maxfd = nfd;
	if (lfd > maxfd)
		maxfd = lfd;
	debug(2, "nfd %d lfd %d maxfd %d\n", nfd, lfd, maxfd);

	inbuf = 0;
	/*
	 * A threaded approach to this loop would have one thread
	 * work on reading lfd (only) all the time and another thread
	 * working on reading nfd all the time.
	 */
	while (!terminate) {
		int n;

		rd = mrd;

		n = select(maxfd + 1, &rd, NULL, NULL, NULL);
		if (n < 0) {
			switch (errno)
			{
			case EINTR :
				continue;
			default :
				syslog(LOG_ERR, "select error: %m");
				debug(1, "select error: %s\n", STRERROR(errno));
				return;
			}
		}

		if (FD_ISSET(lfd, &rd)) {
			n1 = read(lfd, buff+inbuf, BUFFERLEN-inbuf);

			debug(3, "read(K):%d\n", n1);

			if (n1 <= 0) {
				syslog(LOG_ERR, "read error (k-header): %m");
				debug(1, "read error (k-header): %s\n",
				      STRERROR(errno));
				return;
			}

			left = 0;

			switch (do_kbuff(n1, buff, &left))
			{
			case R_IO_ERROR :
				return;
			case R_MORE :
				inbuf += left;
				break;
			default :
				inbuf = 0;
				break;
			}
		}

		if (FD_ISSET(nfd, &rd)) {
			n1 = recv(nfd, nbuff, sizeof(nbuff), 0);

			debug(3, "read(N):%d\n", n1);

			if (n1 <= 0) {
				syslog(LOG_ERR, "read error (n-header): %m");
				debug(1, "read error (n-header): %s\n",
				      STRERROR(errno));
				return;
			}

			switch (do_packet(n1, nbuff))
			{
			case R_IO_ERROR :
				return;
			default :
				break;
			}
		}
	}
}


int
buildsocket(nicname, sinp)
	char *nicname;
	struct sockaddr_in *sinp;
{
	struct sockaddr_in *reqip;
	struct ifreq req;
	char opt;

	debug(2, "binding to %s:%s\n", nicname, inet_ntoa(sinp->sin_addr));

	if (IN_MULTICAST(ntohl(sinp->sin_addr.s_addr))) {
		struct in_addr addr;
		struct ip_mreq mreq;

		igmpfd = socket(AF_INET, SOCK_RAW, IPPROTO_IGMP);
		if (igmpfd == -1) {
			syslog(LOG_ERR, "socket:%m");
			debug(1, "socket:%s\n", STRERROR(errno));
			return -1;
		}

		bzero((char *)&req, sizeof(req));
		strncpy(req.ifr_name, nicname, sizeof(req.ifr_name));
		req.ifr_name[sizeof(req.ifr_name) - 1] = '\0';
		if (ioctl(igmpfd, SIOCGIFADDR, &req) == -1) {
			syslog(LOG_ERR, "ioctl(SIOCGIFADDR):%m");
			debug(1, "ioctl(SIOCGIFADDR):%s\n", STRERROR(errno));
			close(igmpfd);
			igmpfd = -1;
			return -1;
		}
		reqip = (struct sockaddr_in *)&req.ifr_addr;

		addr = reqip->sin_addr;
		if (setsockopt(igmpfd, IPPROTO_IP, IP_MULTICAST_IF,
			       (char *)&addr, sizeof(addr)) == -1) {
			syslog(LOG_ERR, "setsockopt(IP_MULTICAST_IF(%s)):%m",
			       inet_ntoa(addr));
			debug(1, "setsockopt(IP_MULTICAST_IF(%s)):%s\n",
			      inet_ntoa(addr), STRERROR(errno));
			close(igmpfd);
			igmpfd = -1;
			return -1;
		}

		opt = 0;
		if (setsockopt(igmpfd, IPPROTO_IP, IP_MULTICAST_LOOP,
			       (char *)&opt, sizeof(opt)) == -1) {
			syslog(LOG_ERR, "setsockopt(IP_MULTICAST_LOOP=0):%m");
			debug(1, "setsockopt(IP_MULTICAST_LOOP=0):%s\n",
			      STRERROR(errno));
			close(igmpfd);
			igmpfd = -1;
			return -1;
		}

		opt = 63;
		if (setsockopt(igmpfd, IPPROTO_IP, IP_MULTICAST_TTL,
			       (char *)&opt, sizeof(opt)) == -1) {
			syslog(LOG_ERR, "setsockopt(IP_MULTICAST_TTL=%d):%m",
			       opt);
			debug(1, "setsockopt(IP_MULTICAST_TTL=%d):%s\n", opt,
			      STRERROR(errno));
			close(igmpfd);
			igmpfd = -1;
			return -1;
		}

		mreq.imr_multiaddr.s_addr = sinp->sin_addr.s_addr;
		mreq.imr_interface.s_addr = reqip->sin_addr.s_addr;

		if (setsockopt(igmpfd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			       (char *)&mreq, sizeof(mreq)) == -1) {
			char buffer[80];

			sprintf(buffer, "%s,", inet_ntoa(sinp->sin_addr));
			strcat(buffer, inet_ntoa(reqip->sin_addr));

			syslog(LOG_ERR,
			       "setsockpt(IP_ADD_MEMBERSHIP,%s):%m", buffer);
			debug(1, "setsockpt(IP_ADD_MEMBERSHIP,%s):%s\n",
			      buffer, STRERROR(errno));
			close(igmpfd);
			igmpfd = -1;
			return -1;
		}
	}
	nfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (nfd == -1) {
		syslog(LOG_ERR, "socket:%m");
		if (igmpfd != -1) {
			close(igmpfd);
			igmpfd = -1;
		}
		return -1;
	}
	bzero((char *)&req, sizeof(req));
	strncpy(req.ifr_name, nicname, sizeof(req.ifr_name));
	req.ifr_name[sizeof(req.ifr_name) - 1] = '\0';
	if (ioctl(nfd, SIOCGIFADDR, &req) == -1) {
		syslog(LOG_ERR, "ioctl(SIOCGIFADDR):%m");
		debug(1, "ioctl(SIOCGIFADDR):%s\n", STRERROR(errno));
		close(igmpfd);
		igmpfd = -1;
		return -1;
	}

	if (bind(nfd, (struct sockaddr *)&req.ifr_addr,
		 sizeof(req.ifr_addr)) == -1) {
		syslog(LOG_ERR, "bind:%m");
		debug(1, "bind:%s\n", STRERROR(errno));
		close(nfd);
		if (igmpfd != -1) {
			close(igmpfd);
			igmpfd = -1;
		}
		nfd = -1;
		return -1;
	}

	if (connect(nfd, (struct sockaddr *)sinp, sizeof(*sinp)) == -1) {
		syslog(LOG_ERR, "connect:%m");
		debug(1, "connect:%s\n", STRERROR(errno));
		close(nfd);
		if (igmpfd != -1) {
			close(igmpfd);
			igmpfd = -1;
		}
		nfd = -1;
		return -1;
	}
	syslog(LOG_INFO, "Sending data to %s", inet_ntoa(sinp->sin_addr));
	debug(3, "Sending data to %s\n", inet_ntoa(sinp->sin_addr));

	return nfd;
}


int
do_packet(pklen, buff)
	int pklen;
	char *buff;
{
	synchdr_t *sh;
	u_32_t magic;
	int len;
	int n2;
	int n3;

	while (pklen > 0) {
		if (pklen < sizeof(*sh)) {
			syslog(LOG_ERR, "packet length too short:%d", pklen);
			debug(2, "packet length too short:%d\n", pklen);
			return R_SKIP;
		}

		sh = (synchdr_t *)buff;
		len = ntohl(sh->sm_len);
		magic = ntohl(sh->sm_magic);

		if (magic != SYNHDRMAGIC) {
			syslog(LOG_ERR, "invalid header magic %x", magic);
			debug(2, "invalid header magic %x\n", magic);
			return R_SKIP;
		}

		if (pklen < len + sizeof(*sh)) {
			syslog(LOG_ERR, "packet length too short:%d", pklen);
			debug(2, "packet length too short:%d\n", pklen);
			return R_SKIP;
		}

		if (debuglevel > 3) {
			printsynchdr(sh);
			printcommand(sh->sm_cmd);
			printtable(sh->sm_table);
			printsmcproto(buff);
		}

		n2 = sizeof(*sh) + len;

		do {
			n3 = write(lfd, buff, n2);
			if (n3 <= 0) {
				syslog(LOG_ERR, "write error: %m");
				debug(1, "write error: %s\n", STRERROR(errno));
				return R_IO_ERROR;
			}

			n2 -= n3;
			buff += n3;
			pklen -= n3;
		} while (n3 != 0);
	}

	return R_OKAY;
}



int
do_kbuff(inbuf, buf, left)
	int inbuf, *left;
	char *buf;
{
	synchdr_t *sh;
	u_32_t magic;
	int complete;
	int sendlen;
	int error;
	int bytes;
	int len;
	int n2;
	int n3;

	sendlen = 0;
	bytes = inbuf;
	error = R_OKAY;
	sh = (synchdr_t *)buf;

	for (complete = 0; bytes > 0; complete++) {
		len = ntohl(sh->sm_len);
		magic = ntohl(sh->sm_magic);

		if (magic != SYNHDRMAGIC) {
			syslog(LOG_ERR,
			       "read invalid header magic 0x%x, flushing",
			       magic);
			debug(2, "read invalid header magic 0x%x, flushing\n",
			       magic);
			n2 = SMC_RLOG;
			(void) ioctl(lfd, SIOCIPFFL, &n2);
			break;
		}

		if (debuglevel > 3) {
			printsynchdr(sh);
			printcommand(sh->sm_cmd);
			printtable(sh->sm_table);
			putchar('\n');
		}

		if (bytes < sizeof(*sh) + len) {
			debug(3, "Not enough bytes %d < %d\n", bytes,
			      sizeof(*sh) + len);
			error = R_MORE;
			break;
		}

		if (debuglevel > 3) {
			printsmcproto(buf);
		}

		sendlen += len + sizeof(*sh);
		sh = (synchdr_t *)(buf + sendlen);
		bytes -= sendlen;
	}

	if (complete) {
		n3 = send(nfd, buf, sendlen, 0);
		if (n3 <= 0) {
			syslog(LOG_ERR, "write error: %m");
			debug(1, "write error: %s\n", STRERROR(errno));
			return R_IO_ERROR;
		}
		debug(3, "send on %d len %d = %d\n", nfd, sendlen, n3);
		error = R_OKAY;
	}

	/* move buffer to the front,we might need to make
	 * this more efficient, by using a rolling pointer
	 * over the buffer and only copying it, when
	 * we are reaching the end
	 */
	if (bytes > 0) {
		bcopy(buf + bytes, buf, bytes);
		error = R_MORE;
	}
	debug(4, "complete %d bytes %d error %d\n", complete, bytes, error);

	*left = bytes;

	return error;
}


void
printcommand(cmd)
	int cmd;
{

	switch (cmd)
	{
	case SMC_CREATE :
		printf(" cmd:CREATE");
		break;
	case SMC_UPDATE :
		printf(" cmd:UPDATE");
		break;
	default :
		printf(" cmd:Unknown(%d)", cmd);
		break;
	}
}


void
printtable(table)
	int table;
{
	switch (table)
	{
	case SMC_NAT :
		printf(" table:NAT");
		break;
	case SMC_STATE :
		printf(" table:STATE");
		break;
	default :
		printf(" table:Unknown(%d)", table);
		break;
	}
}


void
printsmcproto(buff)
	char *buff;
{
	syncupdent_t *su;
	synchdr_t *sh;

	sh = (synchdr_t *)buff;

	if (sh->sm_cmd == SMC_CREATE) {
		;

	} else if (sh->sm_cmd == SMC_UPDATE) {
		su = (syncupdent_t *)buff;
		if (sh->sm_p == IPPROTO_TCP) {
			printf(" TCP Update: age %lu state %d/%d\n",
				su->sup_tcp.stu_age,
				su->sup_tcp.stu_state[0],
				su->sup_tcp.stu_state[1]);
		}
	} else {
		printf("Unknown command\n");
	}
}


void
printsynchdr(sh)
	synchdr_t *sh;
{

	printf("v:%d p:%d num:%d len:%d magic:%x", sh->sm_v, sh->sm_p,
	       ntohl(sh->sm_num), ntohl(sh->sm_len), ntohl(sh->sm_magic));
}
