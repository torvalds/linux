/*	$FreeBSD$	*/

/*
 * (C)Copyright (C) 2012 by Darren Reed.
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>

#include <net/if.h>

#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

#include "ip_compat.h"
#include "ip_fil.h"
#include "ip_nat.h"

#include "ipf.h"

extern	char	*optarg;


typedef	struct	l4cfg	{
	struct	l4cfg		*l4_next;
	struct	ipnat		l4_nat;		/* NAT rule */
	struct	sockaddr_in	l4_sin;		/* remote socket to connect */
	time_t			l4_last;	/* when we last connected */
	int			l4_alive;	/* 1 = remote alive */
	int			l4_fd;
	int			l4_rw;		/* 0 = reading, 1 = writing */
	char			*l4_rbuf;	/* read buffer */
	int			l4_rsize;	/* size of buffer */
	int			l4_rlen;	/* how much used */
	char			*l4_wptr;	/* next byte to write */
	int			l4_wlen;	/* length yet to be written */
} l4cfg_t;


l4cfg_t *l4list = NULL;
char *response = NULL;
char *probe = NULL;
l4cfg_t template;
int frequency = 20;
int ctimeout = 1;
int rtimeout = 1;
size_t plen = 0;
size_t rlen = 0;
int natfd = -1;
int opts = 0;

#if defined(sun) && !defined(__svr4__) && !defined(__SVR4)
# define	strerror(x)	sys_errlist[x]
#endif


char *copystr(dst, src)
	char *dst, *src;
{
	register char *s, *t, c;
	register int esc = 0;

	for (s = src, t = dst; s && t && (c = *s++); )
		if (esc) {
			esc = 0;
			switch (c)
			{
			case 'n' :
				*t++ = '\n';
				break;
			case 'r' :
				*t++ = '\r';
				break;
			case 't' :
				*t++ = '\t';
				break;
			}
		} else if (c != '\\')
			*t++ = c;
		else
			esc = 1;
	*t = '\0';
	return dst;
}

void addnat(l4)
	l4cfg_t *l4;
{
	ipnat_t *ipn = &l4->l4_nat;

	printf("Add NAT rule for %s/%#x,%u -> ", inet_ntoa(ipn->in_out[0]),
		ipn->in_outmsk, ntohs(ipn->in_pmin));
	printf("%s,%u\n", inet_ntoa(ipn->in_in[0]), ntohs(ipn->in_pnext));
	if (!(opts & OPT_DONOTHING)) {
		if (ioctl(natfd, SIOCADNAT, &ipn) == -1)
			perror("ioctl(SIOCADNAT)");
	}
}


void delnat(l4)
	l4cfg_t *l4;
{
	ipnat_t *ipn = &l4->l4_nat;

	printf("Remove NAT rule for %s/%#x,%u -> ",
		inet_ntoa(ipn->in_out[0]), ipn->in_outmsk, ipn->in_pmin);
	printf("%s,%u\n", inet_ntoa(ipn->in_in[0]), ipn->in_pnext);
	if (!(opts & OPT_DONOTHING)) {
		if (ioctl(natfd, SIOCRMNAT, &ipn) == -1)
			perror("ioctl(SIOCRMNAT)");
	}
}


void connectl4(l4)
	l4cfg_t *l4;
{
	l4->l4_rw = 1;
	l4->l4_rlen = 0;
	l4->l4_wlen = plen;
	if (!l4->l4_wlen) {
		l4->l4_alive = 1;
		addnat(l4);
	} else
		l4->l4_wptr = probe;
}


void closel4(l4, dead)
	l4cfg_t *l4;
	int dead;
{
	close(l4->l4_fd);
	l4->l4_fd = -1;
	l4->l4_rw = -1;
	if (dead && l4->l4_alive) {
		l4->l4_alive = 0;
		delnat(l4);
	}
}


void connectfd(l4)
	l4cfg_t *l4;
{
	if (connect(l4->l4_fd, (struct sockaddr *)&l4->l4_sin,
		    sizeof(l4->l4_sin)) == -1) {
		if (errno == EISCONN) {
			if (opts & OPT_VERBOSE)
				fprintf(stderr, "Connected fd %d\n",
					l4->l4_fd);
			connectl4(l4);
			return;
		}
		if (opts & OPT_VERBOSE)
			fprintf(stderr, "Connect failed fd %d: %s\n",
				l4->l4_fd, strerror(errno));
		closel4(l4, 1);
		return;
	}
	l4->l4_rw = 1;
}


void writefd(l4)
	l4cfg_t *l4;
{
	char buf[80], *ptr;
	int n, i, fd;

	fd = l4->l4_fd;

	if (l4->l4_rw == -2) {
		connectfd(l4);
		return;
	}

	n = l4->l4_wlen;

	i = send(fd, l4->l4_wptr, n, 0);
	if (i == 0 || i == -1) {
		if (opts & OPT_VERBOSE)
			fprintf(stderr, "Send on fd %d failed: %s\n",
				fd, strerror(errno));
		closel4(l4, 1);
	} else {
		l4->l4_wptr += i;
		l4->l4_wlen -= i;
		if (l4->l4_wlen == 0)
			l4->l4_rw = 0;
		if (opts & OPT_VERBOSE)
			fprintf(stderr, "Sent %d bytes to fd %d\n", i, fd);
	}
}


void readfd(l4)
	l4cfg_t *l4;
{
	char buf[80], *ptr;
	int n, i, fd;

	fd = l4->l4_fd;

	if (l4->l4_rw == -2) {
		connectfd(l4);
		return;
	}

	if (l4->l4_rsize) {
		n = l4->l4_rsize - l4->l4_rlen;
		ptr = l4->l4_rbuf + l4->l4_rlen;
	} else {
		n = sizeof(buf) - 1;
		ptr = buf;
	}

	if (opts & OPT_VERBOSE)
		fprintf(stderr, "Read %d bytes on fd %d to %p\n",
			n, fd, ptr);
	i = recv(fd, ptr, n, 0);
	if (i == 0 || i == -1) {
		if (opts & OPT_VERBOSE)
			fprintf(stderr, "Read error on fd %d: %s\n",
				fd, (i == 0) ? "EOF" : strerror(errno));
		closel4(l4, 1);
	} else {
		if (ptr == buf)
			ptr[i] = '\0';
		if (opts & OPT_VERBOSE)
			fprintf(stderr, "%d: Read %d bytes [%*.*s]\n",
				fd, i, i, i, ptr);
		if (ptr != buf) {
			l4->l4_rlen += i;
			if (l4->l4_rlen >= l4->l4_rsize) {
				if (!strncmp(response, l4->l4_rbuf,
					     l4->l4_rsize)) {
					printf("%d: Good response\n",
						fd);
					if (!l4->l4_alive) {
						l4->l4_alive = 1;
						addnat(l4);
					}
					closel4(l4, 0);
				} else {
					if (opts & OPT_VERBOSE)
						printf("%d: Bad response\n",
							fd);
					closel4(l4, 1);
				}
			}
		} else if (!l4->l4_alive) {
			l4->l4_alive = 1;
			addnat(l4);
			closel4(l4, 0);
		}
	}
}


int runconfig()
{
	int fd, opt, res, mfd, i;
	struct timeval tv;
	time_t now, now1;
	fd_set rfd, wfd;
	l4cfg_t *l4;

	mfd = 0;
	opt = 1;
	now = time(NULL);

	/*
	 * First, initiate connections that are closed, as required.
	 */
	for (l4 = l4list; l4; l4 = l4->l4_next) {
		if ((l4->l4_last + frequency < now) && (l4->l4_fd == -1)) {
			l4->l4_last = now;
			fd = socket(AF_INET, SOCK_STREAM, 0);
			if (fd == -1)
				continue;
			setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt,
				   sizeof(opt));
#ifdef	O_NONBLOCK
			if ((res = fcntl(fd, F_GETFL, 0)) != -1)
				fcntl(fd, F_SETFL, res | O_NONBLOCK);
#endif
			if (opts & OPT_VERBOSE)
				fprintf(stderr,
					"Connecting to %s,%d (fd %d)...",
					inet_ntoa(l4->l4_sin.sin_addr),
					ntohs(l4->l4_sin.sin_port), fd);
			if (connect(fd, (struct sockaddr *)&l4->l4_sin,
				    sizeof(l4->l4_sin)) == -1) {
				if (errno != EINPROGRESS) {
					if (opts & OPT_VERBOSE)
						fprintf(stderr, "failed\n");
					perror("connect");
					close(fd);
					fd = -1;
				} else {
					if (opts & OPT_VERBOSE)
						fprintf(stderr, "waiting\n");
					l4->l4_rw = -2;
				}
			} else {
				if (opts & OPT_VERBOSE)
					fprintf(stderr, "connected\n");
				connectl4(l4);
			}
			l4->l4_fd = fd;
		}
	}

	/*
	 * Now look for fd's which we're expecting to read/write from.
	 */
	FD_ZERO(&rfd);
	FD_ZERO(&wfd);
	tv.tv_sec = MIN(rtimeout, ctimeout);
	tv.tv_usec = 0;

	for (l4 = l4list; l4; l4 = l4->l4_next)
		if (l4->l4_rw == 0) {
			if (now - l4->l4_last > rtimeout) {
				if (opts & OPT_VERBOSE)
					fprintf(stderr, "%d: Read timeout\n",
						l4->l4_fd);
				closel4(l4, 1);
				continue;
			}
			if (opts & OPT_VERBOSE)
				fprintf(stderr, "Wait for read on fd %d\n",
					l4->l4_fd);
			FD_SET(l4->l4_fd, &rfd);
			if (l4->l4_fd > mfd)
				mfd = l4->l4_fd;
		} else if ((l4->l4_rw == 1 && l4->l4_wlen) ||
			   l4->l4_rw == -2) {
			if ((l4->l4_rw == -2) &&
			    (now - l4->l4_last > ctimeout)) {
				if (opts & OPT_VERBOSE)
					fprintf(stderr,
						"%d: connect timeout\n",
						l4->l4_fd);
				closel4(l4);
				continue;
			}
			if (opts & OPT_VERBOSE)
				fprintf(stderr, "Wait for write on fd %d\n",
					l4->l4_fd);
			FD_SET(l4->l4_fd, &wfd);
			if (l4->l4_fd > mfd)
				mfd = l4->l4_fd;
		}

	if (opts & OPT_VERBOSE)
		fprintf(stderr, "Select: max fd %d wait %d\n", mfd + 1,
			tv.tv_sec);
	i = select(mfd + 1, &rfd, &wfd, NULL, &tv);
	if (i == -1) {
		perror("select");
		return -1;
	}

	now1 = time(NULL);

	for (l4 = l4list; (i > 0) && l4; l4 = l4->l4_next) {
		if (l4->l4_fd < 0)
			continue;
		if (FD_ISSET(l4->l4_fd, &rfd)) {
			if (opts & OPT_VERBOSE)
				fprintf(stderr, "Ready to read on fd %d\n",
					l4->l4_fd);
			readfd(l4);
			i--;
		}

		if ((l4->l4_fd >= 0) && FD_ISSET(l4->l4_fd, &wfd)) {
			if (opts & OPT_VERBOSE)
				fprintf(stderr, "Ready to write on fd %d\n",
					l4->l4_fd);
			writefd(l4);
			i--;
		}
	}
	return 0;
}


int gethostport(str, lnum, ipp, portp)
	char *str;
	int lnum;
	u_32_t *ipp;
	u_short *portp;
{
	struct servent *sp;
	struct hostent *hp;
	char *host, *port;
	struct in_addr ip;

	host = str;
	port = strchr(host, ',');
	if (port)
		*port++ = '\0';

#ifdef	HAVE_INET_ATON
	if (ISDIGIT(*host) && inet_aton(host, &ip))
		*ipp = ip.s_addr;
#else
	if (ISDIGIT(*host))
		*ipp = inet_addr(host);
#endif
	else {
		if (!(hp = gethostbyname(host))) {
			fprintf(stderr, "%d: can't resolve hostname: %s\n",
				lnum, host);
			return 0;
		}
		*ipp = *(u_32_t *)hp->h_addr;
	}

	if (port) {
		if (ISDIGIT(*port))
			*portp = htons(atoi(port));
		else {
			sp = getservbyname(port, "tcp");
			if (sp)
				*portp = sp->s_port;
			else {
				fprintf(stderr, "%d: unknown service %s\n",
					lnum, port);
				return 0;
			}
		}
	} else
		*portp = 0;
	return 1;
}


char *mapfile(file, sizep)
	char *file;
	size_t *sizep;
{
	struct stat sb;
	caddr_t addr;
	int fd;

	fd = open(file, O_RDONLY);
	if (fd == -1) {
		perror("open(mapfile)");
		return NULL;
	}

	if (fstat(fd, &sb) == -1) {
		perror("fstat(mapfile)");
		close(fd);
		return NULL;
	}

	addr = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (addr == (caddr_t)-1) {
		perror("mmap(mapfile)");
		close(fd);
		return NULL;
	}
	close(fd);
	*sizep = sb.st_size;
	return (char *)addr;
}


int readconfig(filename)
	char *filename;
{
	char c, buf[512], *s, *t, *errtxt = NULL, *line;
	int num, err = 0;
	ipnat_t *ipn;
	l4cfg_t *l4;
	FILE *fp;

	fp = fopen(filename, "r");
	if (!fp) {
		perror("open(configfile)");
		return -1;
	}

	bzero((char *)&template, sizeof(template));
	template.l4_fd = -1;
	template.l4_rw = -1;
	template.l4_sin.sin_family = AF_INET;
	ipn = &template.l4_nat;
	ipn->in_flags = IPN_TCP|IPN_ROUNDR;
	ipn->in_redir = NAT_REDIRECT;

	for (num = 1; fgets(buf, sizeof(buf), fp); num++) {
		s = strchr(buf, '\n');
		if  (!s) {
			fprintf(stderr, "%d: line too long\n", num);
			fclose(fp);
			return -1;
		}

		*s = '\0';

		/*
		 * lines which are comments
		 */
		s = strchr(buf, '#');
		if (s)
			*s = '\0';

		/*
		 * Skip leading whitespace
		 */
		for (line = buf; (c = *line) && ISSPACE(c); line++)
			;
		if (!*line)
			continue;

		if (opts & OPT_VERBOSE)
			fprintf(stderr, "Parsing: [%s]\n", line);
		t = strtok(line, " \t");
		if (!t)
			continue;
		if (!strcasecmp(t, "interface")) {
			s = strtok(NULL, " \t");
			if (s)
				t = strtok(NULL, "\t");
			if (!s || !t) {
				errtxt = line;
				err = -1;
				break;
			}

			if (!strchr(t, ',')) {
				fprintf(stderr,
					"%d: local address,port missing\n",
					num);
				err = -1;
				break;
			}

			strncpy(ipn->in_ifname, s, sizeof(ipn->in_ifname));
			if (!gethostport(t, num, &ipn->in_outip,
					 &ipn->in_pmin)) {
				errtxt = line;
				err = -1;
				break;
			}
			ipn->in_outmsk = 0xffffffff;
			ipn->in_pmax = ipn->in_pmin;
			if (opts & OPT_VERBOSE)
				fprintf(stderr,
					"Interface %s %s/%#x port %u\n",
					ipn->in_ifname,
					inet_ntoa(ipn->in_out[0]),
					ipn->in_outmsk, ipn->in_pmin);
		} else if (!strcasecmp(t, "remote")) {
			if (!*ipn->in_ifname) {
				fprintf(stderr,
					"%d: ifname not set prior to remote\n",
					num);
				err = -1;
				break;
			}
			s = strtok(NULL, " \t");
			if (s)
				t = strtok(NULL, "");
			if (!s || !t || strcasecmp(s, "server")) {
				errtxt = line;
				err = -1;
				break;
			}

			ipn->in_pnext = 0;
			if (!gethostport(t, num, &ipn->in_inip,
					 &ipn->in_pnext)) {
				errtxt = line;
				err = -1;
				break;
			}
			ipn->in_inmsk = 0xffffffff;
			if (ipn->in_pnext == 0)
				ipn->in_pnext = ipn->in_pmin;

			l4 = (l4cfg_t *)malloc(sizeof(*l4));
			if (!l4) {
				fprintf(stderr, "%d: out of memory (%d)\n",
					num, sizeof(*l4));
				err = -1;
				break;
			}
			bcopy((char *)&template, (char *)l4, sizeof(*l4));
			l4->l4_sin.sin_addr = ipn->in_in[0];
			l4->l4_sin.sin_port = ipn->in_pnext;
			l4->l4_next = l4list;
			l4list = l4;
		} else if (!strcasecmp(t, "connect")) {
			s = strtok(NULL, " \t");
			if (s)
				t = strtok(NULL, "\t");
			if (!s || !t) {
				errtxt = line;
				err = -1;
				break;
			} else if (!strcasecmp(s, "timeout")) {
				ctimeout = atoi(t);
				if (opts & OPT_VERBOSE)
					fprintf(stderr, "connect timeout %d\n",
						ctimeout);
			} else if (!strcasecmp(s, "frequency")) {
				frequency = atoi(t);
				if (opts & OPT_VERBOSE)
					fprintf(stderr,
						"connect frequency %d\n",
						frequency);
			} else {
				errtxt = line;
				err = -1;
				break;
			}
		} else if (!strcasecmp(t, "probe")) {
			s = strtok(NULL, " \t");
			if (!s) {
				errtxt = line;
				err = -1;
				break;
			} else if (!strcasecmp(s, "string")) {
				if (probe) {
					fprintf(stderr,
						"%d: probe already set\n",
						num);
					err = -1;
					break;
				}
				t = strtok(NULL, "");
				if (!t) {
					fprintf(stderr,
						"%d: No probe string\n", num);
					err = -1;
					break;
				}

				probe = malloc(strlen(t));
				copystr(probe, t);
				plen = strlen(probe);
				if (opts & OPT_VERBOSE)
					fprintf(stderr, "Probe string [%s]\n",
						probe);
			} else if (!strcasecmp(s, "file")) {
				t = strtok(NULL, " \t");
				if (!t) {
					errtxt = line;
					err = -1;
					break;
				}
				if (probe) {
					fprintf(stderr,
						"%d: probe already set\n",
						num);
					err = -1;
					break;
				}
				probe = mapfile(t, &plen);
				if (opts & OPT_VERBOSE)
					fprintf(stderr,
						"Probe file %s len %u@%p\n",
						t, plen, probe);
			}
		} else if (!strcasecmp(t, "response")) {
			s = strtok(NULL, " \t");
			if (!s) {
				errtxt = line;
				err = -1;
				break;
			} else if (!strcasecmp(s, "timeout")) {
				t = strtok(NULL, " \t");
				if (!t) {
					errtxt = line;
					err = -1;
					break;
				}
				rtimeout = atoi(t);
				if (opts & OPT_VERBOSE)
					fprintf(stderr,
						"response timeout %d\n",
						rtimeout);
			} else if (!strcasecmp(s, "string")) {
				if (response) {
					fprintf(stderr,
						"%d: response already set\n",
						num);
					err = -1;
					break;
				}
				response = strdup(strtok(NULL, ""));
				rlen = strlen(response);
				template.l4_rsize = rlen;
				template.l4_rbuf = malloc(rlen);
				if (opts & OPT_VERBOSE)
					fprintf(stderr,
						"Response string [%s]\n",
						response);
			} else if (!strcasecmp(s, "file")) {
				t = strtok(NULL, " \t");
				if (!t) {
					errtxt = line;
					err = -1;
					break;
				}
				if (response) {
					fprintf(stderr,
						"%d: response already set\n",
						num);
					err = -1;
					break;
				}
				response = mapfile(t, &rlen);
				template.l4_rsize = rlen;
				template.l4_rbuf = malloc(rlen);
				if (opts & OPT_VERBOSE)
					fprintf(stderr,
						"Response file %s len %u@%p\n",
						t, rlen, response);
			}
		} else {
			errtxt = line;
			err = -1;
			break;
		}
	}

	if (errtxt)
		fprintf(stderr, "%d: syntax error at \"%s\"\n", num, errtxt);
	fclose(fp);
	return err;
}


void usage(prog)
	char *prog;
{
	fprintf(stderr, "Usage: %s -f <configfile>\n", prog);
	exit(1);
}


int main(argc, argv)
	int argc;
	char *argv[];
{
	char *config = NULL;
	int c;

	while ((c = getopt(argc, argv, "f:nv")) != -1)
		switch (c)
		{
		case 'f' :
			config = optarg;
			break;
		case 'n' :
			opts |= OPT_DONOTHING;
			break;
		case 'v' :
			opts |= OPT_VERBOSE;
			break;
		}

	if (config == NULL)
		usage(argv[0]);

	if (readconfig(config))
		exit(1);

	if (!l4list) {
		fprintf(stderr, "No remote servers, exiting.");
		exit(1);
	}

	if (!(opts & OPT_DONOTHING)) {
		natfd = open(IPL_NAT, O_RDWR);
		if (natfd == -1) {
			perror("open(IPL_NAT)");
			exit(1);
		}
	}

	if (opts & OPT_VERBOSE)
		fprintf(stderr, "Starting...\n");
	while (runconfig() == 0)
		;
}
