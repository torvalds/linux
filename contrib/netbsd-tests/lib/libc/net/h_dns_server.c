/*	$NetBSD: h_dns_server.c,v 1.4 2014/03/29 16:10:54 gson Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Andreas Gustafsson.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * A minimal DNS server capable of providing canned answers to the
 * specific queries issued by t_hostent.sh and nothing more.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: h_dns_server.c,v 1.4 2014/03/29 16:10:54 gson Exp $");

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/socket.h>

#include <netinet/in.h>
#ifdef __NetBSD__
#include <netinet6/in6.h>
#endif

#ifdef __FreeBSD__
#include <paths.h>
#endif

union sockaddr_either {
	struct sockaddr s;
	struct sockaddr_in sin;
	struct sockaddr_in6 sin6;
};

#ifdef DEBUG
#define DPRINTF(...)	fprintf(stderr, __VA_ARGS__)
#else
#define DPRINTF(...)	
#endif

/* A DNS question and its corresponding answer */

struct dns_data {
	size_t qname_size;
	const char *qname; /* Wire-encode question name */
	int qtype;
	size_t answer_size;
	const char *answer; /* One wire-encoded answer RDATA */
};

/* Convert C string constant to length + data pair */
#define STR_DATA(s) sizeof(s) - 1, s

/* Canned DNS queestion-answer pairs */
struct dns_data data[] = {
	/* Forward mappings */
	/* localhost IN A -> 127.0.0.1 */
	{ STR_DATA("\011localhost\000"), 1,
	  STR_DATA("\177\000\000\001") },
	/* localhost IN AAAA -> ::1 */
	{ STR_DATA("\011localhost\000"), 28,
	  STR_DATA("\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\001") },
	/* sixthavenue.astron.com IN A -> 38.117.134.16 */
	{ STR_DATA("\013sixthavenue\006astron\003com\000"), 1,
	  STR_DATA("\046\165\206\020") },
	/* sixthavenue.astron.com IN AAAA -> 2620:106:3003:1f00:3e4a:92ff:fef4:e180 */
	{ STR_DATA("\013sixthavenue\006astron\003com\000"), 28,
	  STR_DATA("\x26\x20\x01\x06\x30\x03\x1f\x00\x3e\x4a\x92\xff\xfe\xf4\xe1\x80") },
	/* Reverse mappings */
	{ STR_DATA("\0011\0010\0010\003127\007in-addr\004arpa\000"), 12,
	  STR_DATA("\011localhost\000") },
	{ STR_DATA("\0011\0010\0010\0010\0010\0010\0010\0010"
		   "\0010\0010\0010\0010\0010\0010\0010\0010"
		   "\0010\0010\0010\0010\0010\0010\0010\0010"
		   "\0010\0010\0010\0010\0010\0010\0010\0010"
		   "\003ip6\004arpa\000"), 12,
	  STR_DATA("\011localhost\000") },
	{ STR_DATA("\00216\003134\003117\00238"
		   "\007in-addr\004arpa\000"), 12,
	  STR_DATA("\013sixthavenue\006astron\003com\000") },
	{ STR_DATA("\0010\0018\0011\001e\0014\001f\001e\001f"
		   "\001f\001f\0012\0019\001a\0014\001e\0013"
		   "\0010\0010\001f\0011\0013\0010\0010\0013"
		   "\0016\0010\0011\0010\0010\0012\0016\0012"
		   "\003ip6\004arpa\000"), 12,
	  STR_DATA("\013sixthavenue\006astron\003com\000") },
	/* End marker */
	{ STR_DATA(""), 0, STR_DATA("") }
};

/*
 * Compare two DNS names for equality.	If equal, return their
 * length, and if not, return zero.  Does not handle compression.
 */
static int
name_eq(const unsigned char *a, const unsigned char *b) {
	const unsigned char *a_save = a;
	for (;;) {
		int i;
		int lena = *a++;
		int lenb = *b++;
		if (lena != lenb)
			return 0;
		if (lena == 0)
			return a - a_save;
		for (i = 0; i < lena; i++)
			if (tolower(a[i]) != tolower(b[i]))
				return 0;
		a += lena;
		b += lena;
	}
}

#ifdef DEBUG
static char *
name2str(const void *v, char *buf, size_t buflen) {
	const unsigned char *a = v;
	char *b = buf;
	char *eb = buf + buflen;

#define ADDC(c) do { \
		if (b < eb) \
			*b++ = c; \
		else \
			return NULL; \
	} while (/*CONSTCOND*/0)
	for (int did = 0;; did++) {
		int lena = *a++;
		if (lena == 0) {
			ADDC('\0');
			return buf;
		}
		if (did)
			ADDC('.');
		for (int i = 0; i < lena; i++)
			ADDC(a[i]);
		a += lena;
	}
}
#endif

#ifdef __FreeBSD__
/* XXX the daemon2_* functions should be in a library */

int __daemon2_detach_pipe[2];

static int
daemon2_fork(void)
{
	int r;
	int fd;
	int i;

	/*
	 * Set up the pipe, making sure the write end does not
	 * get allocated one of the file descriptors that will
	 * be closed in daemon2_detach().
	 */
	for (i = 0; i < 3; i++) {
	    r = pipe(__daemon2_detach_pipe);
	    if (r < 0)
		    return -1;
	    if (__daemon2_detach_pipe[1] <= STDERR_FILENO &&
		(fd = open(_PATH_DEVNULL, O_RDWR, 0)) != -1) {
		    (void)dup2(fd, __daemon2_detach_pipe[0]);
		    (void)dup2(fd, __daemon2_detach_pipe[1]);
		    if (fd > STDERR_FILENO)
			    (void)close(fd);
		    continue;
	    }
	    break;
	}

	r = fork();
	if (r < 0) {
		return -1;
	} else if (r == 0) {
		/* child */
		close(__daemon2_detach_pipe[0]);
		return 0;
       }
       /* Parent */

       (void) close(__daemon2_detach_pipe[1]);

       for (;;) {
	       char dummy;
	       r = read(__daemon2_detach_pipe[0], &dummy, 1);
	       if (r < 0) {
		       if (errno == EINTR)
			       continue;
		       _exit(1);
	       } else if (r == 0) {
		       _exit(1);
	       } else { /* r > 0 */
		       _exit(0);
	       }
       }
}

static int
daemon2_detach(int nochdir, int noclose)
{
	int r;
	int fd;

	if (setsid() == -1)
		return -1;

	if (!nochdir)
		(void)chdir("/");

	if (!noclose && (fd = open(_PATH_DEVNULL, O_RDWR, 0)) != -1) {
		(void)dup2(fd, STDIN_FILENO);
		(void)dup2(fd, STDOUT_FILENO);
		(void)dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			(void)close(fd);
	}

	while (1) {
		r = write(__daemon2_detach_pipe[1], "", 1);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			/* May get "broken pipe" here if parent is killed */
			return -1;
		} else if (r == 0) {
			/* Should not happen */
			return -1;
		} else {
			break;
		}
	}

	(void) close(__daemon2_detach_pipe[1]);

	return 0;
}
#endif

int main(int argc, char **argv) {
	int s, r, protocol;
	union sockaddr_either saddr;
	struct dns_data *dp;
	unsigned char *p;
	char pidfile_name[40];
	FILE *f;
	int one = 1;
#ifdef DEBUG
	char buf1[1024], buf2[1024];
#endif

#ifdef __FreeBSD__
	daemon2_fork();
#endif
	if (argc < 2 || ((protocol = argv[1][0]) != '4' && protocol != '6'))
		errx(1, "usage: dns_server 4 | 6");
	s = socket(protocol == '4' ? PF_INET : PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0)
		err(1, "socket");
	if (protocol == '4') {
		memset(&saddr.sin, 0, sizeof(saddr.sin));
		saddr.sin.sin_family = AF_INET;
		saddr.sin.sin_len = sizeof(saddr.sin);
		saddr.sin.sin_port = htons(53);
		saddr.sin.sin_addr.s_addr = INADDR_ANY;
	} else {
		static struct in6_addr loopback = IN6ADDR_LOOPBACK_INIT;
		memset(&saddr.sin6, 0, sizeof(saddr.sin6));
		saddr.sin6.sin6_family = AF_INET6;
		saddr.sin6.sin6_len = sizeof(saddr.sin6);
		saddr.sin6.sin6_port = htons(53);
		saddr.sin6.sin6_addr = loopback;
	}

	r = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
	if (r < 0)
		err(1, "setsockopt");

	r = bind(s,
		 (struct sockaddr *) &saddr,
		 protocol == '4' ? sizeof(struct sockaddr_in) :
				   sizeof(struct sockaddr_in6));
	if (r < 0)
		err(1, "bind");

	snprintf(pidfile_name, sizeof pidfile_name,
		 "dns_server_%c.pid", protocol);
	f = fopen(pidfile_name, "w");
	fprintf(f, "%d", getpid());
	fclose(f);
#ifdef __FreeBSD__
#ifdef DEBUG
	daemon2_detach(0, 1);
#else
	daemon2_detach(0, 0);
#endif
#else
#ifdef DEBUG
	daemon(0, 1);
#else
	daemon(0, 0);
#endif
#endif

	for (;;) {
		unsigned char buf[512];
		union sockaddr_either from;
		ssize_t nrecv, nsent;
		socklen_t fromlen =
			protocol == '4' ? sizeof(struct sockaddr_in) :
					  sizeof(struct sockaddr_in6);
		memset(buf, 0, sizeof buf);
		nrecv = recvfrom(s, buf, sizeof buf, 0, &from.s, &fromlen);
		if (nrecv < 0)
			err(1, "recvfrom");
		if (nrecv < 12) {
			DPRINTF("Too short %zd\n", nrecv);
			continue;
		}
		if ((buf[2] & 0x80) != 0) {
			DPRINTF("Not a query 0x%x\n", buf[2]);
			continue;
		}
		if (!(buf[4] == 0 && buf[5] == 1)) {
			DPRINTF("QCOUNT is not 1 0x%x 0x%x\n", buf[4], buf[5]);
			continue; /* QDCOUNT is not 1 */
		}

		for (dp = data; dp->qname_size != 0; dp++) {
			int qtype, qclass;
			p = buf + 12; /* Point to QNAME */
			int n = name_eq(p, (const unsigned char *) dp->qname);
			if (n == 0) {
				DPRINTF("no match name %s != %s\n",
				    name2str(p, buf1, sizeof(buf1)),
				    name2str(dp->qname, buf2, sizeof(buf2)));
				continue; /* Name does not match */
			}
			DPRINTF("match name %s\n",
			    name2str(p, buf1, sizeof(buf1)));
			p += n; /* Skip QNAME */
			qtype = *p++ << 8;
			qtype |= *p++;
			if (qtype != dp->qtype) {
				DPRINTF("no match name 0x%x != 0x%x\n",
				    qtype, dp->qtype);
				continue;
			}
			DPRINTF("match type 0x%x\n", qtype);
			qclass = *p++ << 8;
			qclass |= *p++;
			if (qclass != 1) { /* IN */
				DPRINTF("no match class %d != 1\n", qclass);
				continue;
			}
			DPRINTF("match class %d\n", qclass);
			goto found;
		}
		continue;
	found:
		buf[2] |= 0x80; /* QR */
		buf[3] |= 0x80; /* RA */
		memset(buf + 6, 0, 6); /* Clear ANCOUNT, NSCOUNT, ARCOUNT */
		buf[7] = 1; /* ANCOUNT */
		memcpy(p, dp->qname, dp->qname_size);
		p += dp->qname_size;
		*p++ = dp->qtype >> 8;
		*p++ = dp->qtype & 0xFF;
		*p++ = 0;
		*p++ = 1; /* IN */
		memset(p, 0, 4); /* TTL = 0 */
		p += 4;
		*p++ = 0;		/* RDLENGTH MSB */
		*p++ = dp->answer_size;	/* RDLENGTH LSB */
		memcpy(p, dp->answer, dp->answer_size);
		p += dp->answer_size;
		nsent = sendto(s, buf, p - buf, 0, &from.s, fromlen);
		DPRINTF("sent %zd\n", nsent);
		if (nsent != p - buf)
			warn("sendto");
	}
}
