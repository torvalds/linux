/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 */
#if !defined(lint)
static const char sccsid[] = "@(#)ip_fil.c	2.41 6/5/96 (C) 1993-2000 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>

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

#include "netinet/ip_compat.h"
#include "netinet/ip_fil.h"
#include "netinet/ip_nat.h"
#include "netinet/ip_state.h"
#include "netinet/ip_sync.h"


int	main __P((int, char *[]));
void	usage __P((const char *));

int	terminate = 0;

void usage(const char *progname) {
	fprintf(stderr, "Usage: %s <destination IP> <destination port>\n", progname);
}

#if 0
static void handleterm(int sig)
{
	terminate = sig;
}
#endif


/* should be large enough to hold header + any datatype */
#define BUFFERLEN 1400

int main(argc, argv)
	int argc;
	char *argv[];
{
	struct sockaddr_in sin;
	char buff[BUFFERLEN];
	synclogent_t *sl;
	syncupdent_t *su;
	int nfd = -1, lfd = -1, n1, n2, n3, len;
	int inbuf;
	u_32_t magic;
	synchdr_t *sh;
	char *progname;

	progname = strrchr(argv[0], '/');
	if (progname) {
		progname++;
	} else {
		progname = argv[0];
	}


	if (argc < 2) {
		usage(progname);
		exit(1);
	}

#if 0
       	signal(SIGHUP, handleterm);
       	signal(SIGINT, handleterm);
       	signal(SIGTERM, handleterm);
#endif

	openlog(progname, LOG_PID, LOG_SECURITY);

	bzero((char *)&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = inet_addr(argv[1]);
	if (argc > 2)
		sin.sin_port = htons(atoi(argv[2]));
	else
		sin.sin_port = htons(43434);

	while (1) {

		if (lfd != -1)
			close(lfd);
		if (nfd != -1)
			close(nfd);

		lfd = open(IPSYNC_NAME, O_RDONLY);
		if (lfd == -1) {
			syslog(LOG_ERR, "Opening %s :%m", IPSYNC_NAME);
			goto tryagain;
		}

		nfd = socket(AF_INET, SOCK_DGRAM, 0);
		if (nfd == -1) {
			syslog(LOG_ERR, "Socket :%m");
			goto tryagain;
		}

		if (connect(nfd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
			syslog(LOG_ERR, "Connect: %m");
			goto tryagain;
		}

		syslog(LOG_INFO, "Sending data to %s",
		       inet_ntoa(sin.sin_addr));

		inbuf = 0;
		while (1) {

			n1 = read(lfd, buff+inbuf, BUFFERLEN-inbuf);

			printf("header : %d bytes read (header = %d bytes)\n",
			       n1, (int) sizeof(*sh));

			if (n1 < 0) {
				syslog(LOG_ERR, "Read error (header): %m");
				goto tryagain;
			}

			if (n1 == 0) {
				/* XXX can this happen??? */
				syslog(LOG_ERR,
				       "Read error (header) : No data");
				sleep(1);
				continue;
			}

			inbuf += n1;

moreinbuf:
			if (inbuf < sizeof(*sh)) {
				continue; /* need more data */
			}

			sh = (synchdr_t *)buff;
			len = ntohl(sh->sm_len);
			magic = ntohl(sh->sm_magic);

			if (magic != SYNHDRMAGIC) {
				syslog(LOG_ERR,
				       "Invalid header magic %x", magic);
				goto tryagain;
			}

#define IPSYNC_DEBUG
#ifdef IPSYNC_DEBUG
			printf("v:%d p:%d len:%d magic:%x", sh->sm_v,
			       sh->sm_p, len, magic);

			if (sh->sm_cmd == SMC_CREATE)
				printf(" cmd:CREATE");
			else if (sh->sm_cmd == SMC_UPDATE)
				printf(" cmd:UPDATE");
			else
				printf(" cmd:Unknown(%d)", sh->sm_cmd);

			if (sh->sm_table == SMC_NAT)
				printf(" table:NAT");
			else if (sh->sm_table == SMC_STATE)
				printf(" table:STATE");
			else
				printf(" table:Unknown(%d)", sh->sm_table);

			printf(" num:%d\n", (u_32_t)ntohl(sh->sm_num));
#endif

			if (inbuf < sizeof(*sh) + len) {
				continue; /* need more data */
				goto tryagain;
			}

#ifdef IPSYNC_DEBUG
			if (sh->sm_cmd == SMC_CREATE) {
				sl = (synclogent_t *)buff;

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
#endif

			n2 = sizeof(*sh) + len;
			n3 = write(nfd, buff, n2);
			if (n3 <= 0) {
				syslog(LOG_ERR, "Write error: %m");
				goto tryagain;
			}


			if (n3 != n2) {
				syslog(LOG_ERR, "Incomplete write (%d/%d)",
				       n3, n2);
				goto tryagain;
			}

			/* signal received? */
			if (terminate)
				break;

			/* move buffer to the front,we might need to make
			 * this more efficient, by using a rolling pointer
			 * over the buffer and only copying it, when
			 * we are reaching the end
			 */
			inbuf -= n2;
			if (inbuf) {
				bcopy(buff+n2, buff, inbuf);
				printf("More data in buffer\n");
				goto moreinbuf;
			}
		}

		if (terminate)
			break;
tryagain:
		sleep(1);
	}


	/* terminate */
	if (lfd != -1)
		close(lfd);
	if (nfd != -1)
		close(nfd);

	syslog(LOG_ERR, "signal %d received, exiting...", terminate);

	exit(1);
}

