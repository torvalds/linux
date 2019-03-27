/*	$NetBSD: ftp.c,v 1.18 2009/05/20 12:53:47 lukem Exp $	*/
/*	from	NetBSD: ftp.c,v 1.159 2009/04/15 03:42:33 jld Exp	*/

/*-
 * Copyright (c) 1996-2009 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Luke Mewburn.
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
 * Copyright (c) 1985, 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (C) 1997 and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "tnftp.h"
#include <arpa/telnet.h>

#if 0	/* tnftp */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)ftp.c	8.6 (Berkeley) 10/27/94";
#else
__RCSID(" NetBSD: ftp.c,v 1.159 2009/04/15 03:42:33 jld Exp  ");
#endif
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <arpa/ftp.h>
#include <arpa/telnet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>

#endif	/* tnftp */

#include "ftp_var.h"

volatile sig_atomic_t	abrtflag;
volatile sig_atomic_t	timeoutflag;

sigjmp_buf	ptabort;
int	ptabflg;
int	ptflag = 0;
char	pasv[BUFSIZ];	/* passive port for proxy data connection */

static int empty(FILE *, FILE *, int);

struct sockinet {
	union sockunion {
		struct sockaddr_in  su_sin;
#ifdef INET6
		struct sockaddr_in6 su_sin6;
#endif
	} si_su;
#if !defined(HAVE_STRUCT_SOCKADDR_IN_SIN_LEN)
	int	si_len;
#endif
};

#if !defined(HAVE_STRUCT_SOCKADDR_IN_SIN_LEN)
# define su_len		si_len
#else
# define su_len		si_su.su_sin.sin_len
#endif
#define su_family	si_su.su_sin.sin_family
#define su_port		si_su.su_sin.sin_port

struct sockinet myctladdr, hisctladdr, data_addr;

char *
hookup(const char *host, const char *port)
{
	int s = -1, error;
	struct addrinfo hints, *res, *res0;
	static char hostnamebuf[MAXHOSTNAMELEN];
	socklen_t len;
	int on = 1;

	memset((char *)&hisctladdr, 0, sizeof (hisctladdr));
	memset((char *)&myctladdr, 0, sizeof (myctladdr));
	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_CANONNAME;
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	error = getaddrinfo(host, port, &hints, &res0);
	if (error) {
		warnx("Can't lookup `%s:%s': %s", host, port,
		    (error == EAI_SYSTEM) ? strerror(errno)
					  : gai_strerror(error));
		code = -1;
		return (0);
	}

	if (res0->ai_canonname)
		(void)strlcpy(hostnamebuf, res0->ai_canonname,
		    sizeof(hostnamebuf));
	else
		(void)strlcpy(hostnamebuf, host, sizeof(hostnamebuf));
	hostname = hostnamebuf;

	for (res = res0; res; res = res->ai_next) {
		char hname[NI_MAXHOST], sname[NI_MAXSERV];

		ai_unmapped(res);
		if (getnameinfo(res->ai_addr, res->ai_addrlen,
		    hname, sizeof(hname), sname, sizeof(sname),
		    NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
			strlcpy(hname, "?", sizeof(hname));
			strlcpy(sname, "?", sizeof(sname));
		}
		if (verbose && res0->ai_next) {
				/* if we have multiple possibilities */
			fprintf(ttyout, "Trying %s:%s ...\n", hname, sname);
		}
		s = socket(res->ai_family, SOCK_STREAM, res->ai_protocol);
		if (s < 0) {
			warn("Can't create socket for connection to `%s:%s'",
			    hname, sname);
			continue;
		}
		if (ftp_connect(s, res->ai_addr, res->ai_addrlen) < 0) {
			close(s);
			s = -1;
			continue;
		}

		/* finally we got one */
		break;
	}
	if (s < 0) {
		warnx("Can't connect to `%s:%s'", host, port);
		code = -1;
		freeaddrinfo(res0);
		return 0;
	}
	memcpy(&hisctladdr.si_su, res->ai_addr, res->ai_addrlen);
	hisctladdr.su_len = res->ai_addrlen;
	freeaddrinfo(res0);
	res0 = res = NULL;

	len = hisctladdr.su_len;
	if (getsockname(s, (struct sockaddr *)&myctladdr.si_su, &len) == -1) {
		warn("Can't determine my address of connection to `%s:%s'",
		    host, port);
		code = -1;
		goto bad;
	}
	myctladdr.su_len = len;

#ifdef IPTOS_LOWDELAY
	if (hisctladdr.su_family == AF_INET) {
		int tos = IPTOS_LOWDELAY;
		if (setsockopt(s, IPPROTO_IP, IP_TOS,
				(void *)&tos, sizeof(tos)) == -1) {
				DWARN("setsockopt %s (ignored)",
				    "IPTOS_LOWDELAY");
		}
	}
#endif
	cin = fdopen(s, "r");
	cout = fdopen(s, "w");
	if (cin == NULL || cout == NULL) {
		warnx("Can't fdopen socket");
		if (cin)
			(void)fclose(cin);
		if (cout)
			(void)fclose(cout);
		code = -1;
		goto bad;
	}
	if (verbose)
		fprintf(ttyout, "Connected to %s.\n", hostname);
	if (getreply(0) > 2) {	/* read startup message from server */
		if (cin)
			(void)fclose(cin);
		if (cout)
			(void)fclose(cout);
		code = -1;
		goto bad;
	}

	if (setsockopt(s, SOL_SOCKET, SO_OOBINLINE,
			(void *)&on, sizeof(on)) == -1) {
		DWARN("setsockopt %s (ignored)", "SO_OOBINLINE");
	}

	return (hostname);
 bad:
	(void)close(s);
	return (NULL);
}

void
cmdabort(int notused)
{
	int oerrno = errno;

	sigint_raised = 1;
	alarmtimer(0);
	if (fromatty)
		write(fileno(ttyout), "\n", 1);
	abrtflag++;
	if (ptflag)
		siglongjmp(ptabort, 1);
	errno = oerrno;
}

void
cmdtimeout(int notused)
{
	int oerrno = errno;

	alarmtimer(0);
	if (fromatty)
		write(fileno(ttyout), "\n", 1);
	timeoutflag++;
	if (ptflag)
		siglongjmp(ptabort, 1);
	errno = oerrno;
}

/*VARARGS*/
int
command(const char *fmt, ...)
{
	va_list ap;
	int r;
	sigfunc oldsigint;

#ifndef NO_DEBUG
	if (ftp_debug) {
		fputs("---> ", ttyout);
		va_start(ap, fmt);
		if (strncmp("PASS ", fmt, 5) == 0)
			fputs("PASS XXXX", ttyout);
		else if (strncmp("ACCT ", fmt, 5) == 0)
			fputs("ACCT XXXX", ttyout);
		else
			vfprintf(ttyout, fmt, ap);
		va_end(ap);
		putc('\n', ttyout);
	}
#endif
	if (cout == NULL) {
		warnx("No control connection for command");
		code = -1;
		return (0);
	}

	abrtflag = 0;

	oldsigint = xsignal(SIGINT, cmdabort);

	va_start(ap, fmt);
	vfprintf(cout, fmt, ap);
	va_end(ap);
	fputs("\r\n", cout);
	(void)fflush(cout);
	cpend = 1;
	r = getreply(!strcmp(fmt, "QUIT"));
	if (abrtflag && oldsigint != SIG_IGN)
		(*oldsigint)(SIGINT);
	(void)xsignal(SIGINT, oldsigint);
	return (r);
}

static const char *m421[] = {
	"remote server timed out. Connection closed",
	"user interrupt. Connection closed",
	"remote server has closed connection",
};

int
getreply(int expecteof)
{
	char current_line[BUFSIZ];	/* last line of previous reply */
	int c, n, lineno;
	int dig;
	int originalcode = 0, continuation = 0;
	sigfunc oldsigint, oldsigalrm;
	int pflag = 0;
	char *cp, *pt = pasv;

	abrtflag = 0;
	timeoutflag = 0;

	oldsigint = xsignal(SIGINT, cmdabort);
	oldsigalrm = xsignal(SIGALRM, cmdtimeout);

	for (lineno = 0 ;; lineno++) {
		dig = n = code = 0;
		cp = current_line;
		while (alarmtimer(quit_time ? quit_time : 60),
		       ((c = getc(cin)) != '\n')) {
			if (c == IAC) {     /* handle telnet commands */
				switch (c = getc(cin)) {
				case WILL:
				case WONT:
					c = getc(cin);
					fprintf(cout, "%c%c%c", IAC, DONT, c);
					(void)fflush(cout);
					break;
				case DO:
				case DONT:
					c = getc(cin);
					fprintf(cout, "%c%c%c", IAC, WONT, c);
					(void)fflush(cout);
					break;
				default:
					break;
				}
				continue;
			}
			dig++;
			if (c == EOF) {
				/*
				 * these will get trashed by pswitch()
				 * in lostpeer()
				 */
				int reply_timeoutflag = timeoutflag;
				int reply_abrtflag = abrtflag;

				alarmtimer(0);
				if (expecteof && feof(cin)) {
					(void)xsignal(SIGINT, oldsigint);
					(void)xsignal(SIGALRM, oldsigalrm);
					code = 221;
					return (0);
				}
				cpend = 0;
				lostpeer(0);
				if (verbose) {
					size_t midx;
					if (reply_timeoutflag)
						midx = 0;
					else if (reply_abrtflag)
						midx = 1;
					else
						midx = 2;
					(void)fprintf(ttyout,
			    "421 Service not available, %s.\n", m421[midx]);
					(void)fflush(ttyout);
				}
				code = 421;
				(void)xsignal(SIGINT, oldsigint);
				(void)xsignal(SIGALRM, oldsigalrm);
				return (4);
			}
			if (c != '\r' && (verbose > 0 ||
			    ((verbose > -1 && n == '5' && dig > 4) &&
			    (((!n && c < '5') || (n && n < '5'))
			     || !retry_connect)))) {
				if (proxflag &&
				   (dig == 1 || (dig == 5 && verbose == 0)))
					fprintf(ttyout, "%s:", hostname);
				(void)putc(c, ttyout);
			}
			if (dig < 4 && isdigit(c))
				code = code * 10 + (c - '0');
			if (!pflag && (code == 227 || code == 228))
				pflag = 1;
			else if (!pflag && code == 229)
				pflag = 100;
			if (dig > 4 && pflag == 1 && isdigit(c))
				pflag = 2;
			if (pflag == 2) {
				if (c != '\r' && c != ')') {
					if (pt < &pasv[sizeof(pasv) - 1])
						*pt++ = c;
				} else {
					*pt = '\0';
					pflag = 3;
				}
			}
			if (pflag == 100 && c == '(')
				pflag = 2;
			if (dig == 4 && c == '-') {
				if (continuation)
					code = 0;
				continuation++;
			}
			if (n == 0)
				n = c;
			if (cp < &current_line[sizeof(current_line) - 1])
				*cp++ = c;
		}
		if (verbose > 0 || ((verbose > -1 && n == '5') &&
		    (n < '5' || !retry_connect))) {
			(void)putc(c, ttyout);
			(void)fflush(ttyout);
		}
		if (cp[-1] == '\r')
			cp[-1] = '\0';
		*cp = '\0';
		if (lineno == 0)
			(void)strlcpy(reply_string, current_line,
			    sizeof(reply_string));
		if (lineno > 0 && code == 0 && reply_callback != NULL)
			(*reply_callback)(current_line);
		if (continuation && code != originalcode) {
			if (originalcode == 0)
				originalcode = code;
			continue;
		}
		if (n != '1')
			cpend = 0;
		alarmtimer(0);
		(void)xsignal(SIGINT, oldsigint);
		(void)xsignal(SIGALRM, oldsigalrm);
		if (code == 421 || originalcode == 421)
			lostpeer(0);
		if (abrtflag && oldsigint != cmdabort && oldsigint != SIG_IGN)
			(*oldsigint)(SIGINT);
		if (timeoutflag && oldsigalrm != cmdtimeout &&
		    oldsigalrm != SIG_IGN)
			(*oldsigalrm)(SIGINT);
		return (n - '0');
	}
}

static int
empty(FILE *ecin, FILE *din, int sec)
{
	int		nr, nfd;
	struct pollfd	pfd[2];

	nfd = 0;
	if (ecin) {
		pfd[nfd].fd = fileno(ecin);
		pfd[nfd++].events = POLLIN;
	}

	if (din) {
		pfd[nfd].fd = fileno(din);
		pfd[nfd++].events = POLLIN;
	}

	if ((nr = ftp_poll(pfd, nfd, sec * 1000)) <= 0)
		return nr;

	nr = 0;
	nfd = 0;
	if (ecin)
		nr |= (pfd[nfd++].revents & POLLIN) ? 1 : 0;
	if (din)
		nr |= (pfd[nfd++].revents & POLLIN) ? 2 : 0;
	return nr;
}

sigjmp_buf	xferabort;

void
abortxfer(int notused)
{
	char msgbuf[100];
	size_t len;

	sigint_raised = 1;
	alarmtimer(0);
	mflag = 0;
	abrtflag = 0;
	switch (direction[0]) {
	case 'r':
		strlcpy(msgbuf, "\nreceive", sizeof(msgbuf));
		break;
	case 's':
		strlcpy(msgbuf, "\nsend", sizeof(msgbuf));
		break;
	default:
		errx(1, "abortxfer: unknown direction `%s'", direction);
	}
	len = strlcat(msgbuf, " aborted. Waiting for remote to finish abort.\n",
	    sizeof(msgbuf));
	write(fileno(ttyout), msgbuf, len);
	siglongjmp(xferabort, 1);
}

/*
 * Read data from infd & write to outfd, using buf/bufsize as the temporary
 * buffer, dealing with short writes.
 * If rate_limit != 0, rate-limit the transfer.
 * If hash_interval != 0, fputc('c', ttyout) every hash_interval bytes.
 * Updates global variables: bytes.
 * Returns 0 if ok, 1 if there was a read error, 2 if there was a write error.
 * In the case of error, errno contains the appropriate error code.
 */
static int
copy_bytes(int infd, int outfd, char *buf, size_t bufsize,
	int rate_limit, int hash_interval)
{
	volatile off_t	hashc;
	ssize_t		inc, outc;
	char		*bufp;
	struct timeval	tvthen, tvnow, tvdiff;
	off_t		bufrem, bufchunk;
	int		serr;

	hashc = hash_interval;
	if (rate_limit)
		bufchunk = rate_limit;
	else
		bufchunk = bufsize;

	while (1) {
		if (rate_limit) {
			(void)gettimeofday(&tvthen, NULL);
		}
		errno = 0;
		inc = outc = 0;
					/* copy bufchunk at a time */
		bufrem = bufchunk;
		while (bufrem > 0) {
			inc = read(infd, buf, MIN((off_t)bufsize, bufrem));
			if (inc <= 0)
				goto copy_done;
			bytes += inc;
			bufrem -= inc;
			bufp = buf;
			while (inc > 0) {
				outc = write(outfd, bufp, inc);
				if (outc < 0)
					goto copy_done;
				inc -= outc;
				bufp += outc;
			}
			if (hash_interval) {
				while (bytes >= hashc) {
					(void)putc('#', ttyout);
					hashc += hash_interval;
				}
				(void)fflush(ttyout);
			}
		}
		if (rate_limit) {	/* rate limited; wait if necessary */
			while (1) {
				(void)gettimeofday(&tvnow, NULL);
				timersub(&tvnow, &tvthen, &tvdiff);
				if (tvdiff.tv_sec > 0)
					break;
				usleep(1000000 - tvdiff.tv_usec);
			}
		}
	}

 copy_done:
	serr = errno;
	if (hash_interval && bytes > 0) {
		if (bytes < hash_interval)
			(void)putc('#', ttyout);
		(void)putc('\n', ttyout);
		(void)fflush(ttyout);
	}
	errno = serr;
	if (inc == -1)
		return 1;
	if (outc == -1)
		return 2;

	return 0;
}

void
sendrequest(const char *cmd, const char *local, const char *remote,
	    int printnames)
{
	struct stat st;
	int c;
	FILE *volatile fin;
	FILE *volatile dout;
	int (*volatile closefunc)(FILE *);
	sigfunc volatile oldintr;
	sigfunc volatile oldintp;
	off_t volatile hashbytes;
	int hash_interval;
	const char *lmode;
	static size_t bufsize;
	static char *buf;
	int oprogress;

	hashbytes = mark;
	direction = "sent";
	dout = NULL;
	bytes = 0;
	filesize = -1;
	oprogress = progress;
	if (verbose && printnames) {
		if (*local != '-')
			fprintf(ttyout, "local: %s ", local);
		if (remote)
			fprintf(ttyout, "remote: %s\n", remote);
	}
	if (proxy) {
		proxtrans(cmd, local, remote);
		return;
	}
	if (curtype != type)
		changetype(type, 0);
	closefunc = NULL;
	oldintr = NULL;
	oldintp = NULL;
	lmode = "w";
	if (sigsetjmp(xferabort, 1)) {
		while (cpend)
			(void)getreply(0);
		code = -1;
		goto cleanupsend;
	}
	(void)xsignal(SIGQUIT, psummary);
	oldintr = xsignal(SIGINT, abortxfer);
	if (strcmp(local, "-") == 0) {
		fin = stdin;
		progress = 0;
	} else if (*local == '|') {
		oldintp = xsignal(SIGPIPE, SIG_IGN);
		fin = popen(local + 1, "r");
		if (fin == NULL) {
			warn("Can't execute `%s'", local + 1);
			code = -1;
			goto cleanupsend;
		}
		progress = 0;
		closefunc = pclose;
	} else {
		fin = fopen(local, "r");
		if (fin == NULL) {
			warn("Can't open `%s'", local);
			code = -1;
			goto cleanupsend;
		}
		closefunc = fclose;
		if (fstat(fileno(fin), &st) < 0 || !S_ISREG(st.st_mode)) {
			fprintf(ttyout, "%s: not a plain file.\n", local);
			code = -1;
			goto cleanupsend;
		}
		filesize = st.st_size;
	}
	if (initconn()) {
		code = -1;
		goto cleanupsend;
	}
	if (sigsetjmp(xferabort, 1))
		goto abort;

	if (restart_point &&
	    (strcmp(cmd, "STOR") == 0 || strcmp(cmd, "APPE") == 0)) {
		int rc;

		rc = -1;
		switch (curtype) {
		case TYPE_A:
			rc = fseeko(fin, restart_point, SEEK_SET);
			break;
		case TYPE_I:
		case TYPE_L:
			rc = lseek(fileno(fin), restart_point, SEEK_SET);
			break;
		}
		if (rc < 0) {
			warn("Can't seek to restart `%s'", local);
			goto cleanupsend;
		}
		if (command("REST " LLF, (LLT)restart_point) != CONTINUE)
			goto cleanupsend;
		lmode = "r+";
	}
	if (remote) {
		if (command("%s %s", cmd, remote) != PRELIM)
			goto cleanupsend;
	} else {
		if (command("%s", cmd) != PRELIM)
			goto cleanupsend;
	}
	dirchange = 1;
	dout = dataconn(lmode);
	if (dout == NULL)
		goto abort;

	if ((size_t)sndbuf_size > bufsize) {
		if (buf)
			(void)free(buf);
		bufsize = sndbuf_size;
		buf = ftp_malloc(bufsize);
	}

	progressmeter(-1);
	oldintp = xsignal(SIGPIPE, SIG_IGN);
	hash_interval = (hash && (!progress || filesize < 0)) ? mark : 0;

	switch (curtype) {

	case TYPE_I:
	case TYPE_L:
		c = copy_bytes(fileno(fin), fileno(dout), buf, bufsize,
			       rate_put, hash_interval);
		if (c == 1) {
			warn("Reading `%s'", local);
		} else if (c == 2) {
			if (errno != EPIPE)
				warn("Writing to network");
			bytes = -1;
		}
		break;

	case TYPE_A:
		while ((c = getc(fin)) != EOF) {
			if (c == '\n') {
				while (hash_interval && bytes >= hashbytes) {
					(void)putc('#', ttyout);
					(void)fflush(ttyout);
					hashbytes += mark;
				}
				if (ferror(dout))
					break;
				(void)putc('\r', dout);
				bytes++;
			}
			(void)putc(c, dout);
			bytes++;
#if 0	/* this violates RFC0959 */
			if (c == '\r') {
				(void)putc('\0', dout);
				bytes++;
			}
#endif
		}
		if (hash_interval) {
			if (bytes < hashbytes)
				(void)putc('#', ttyout);
			(void)putc('\n', ttyout);
		}
		if (ferror(fin))
			warn("Reading `%s'", local);
		if (ferror(dout)) {
			if (errno != EPIPE)
				warn("Writing to network");
			bytes = -1;
		}
		break;
	}

	progressmeter(1);
	if (closefunc != NULL) {
		(*closefunc)(fin);
		fin = NULL;
	}
	(void)fclose(dout);
	dout = NULL;
	(void)getreply(0);
	if (bytes > 0)
		ptransfer(0);
	goto cleanupsend;

 abort:
	(void)xsignal(SIGINT, oldintr);
	oldintr = NULL;
	if (!cpend) {
		code = -1;
		goto cleanupsend;
	}
	if (data >= 0) {
		(void)close(data);
		data = -1;
	}
	if (dout) {
		(void)fclose(dout);
		dout = NULL;
	}
	(void)getreply(0);
	code = -1;
	if (bytes > 0)
		ptransfer(0);

 cleanupsend:
	if (oldintr)
		(void)xsignal(SIGINT, oldintr);
	if (oldintp)
		(void)xsignal(SIGPIPE, oldintp);
	if (data >= 0) {
		(void)close(data);
		data = -1;
	}
	if (closefunc != NULL && fin != NULL)
		(*closefunc)(fin);
	if (dout)
		(void)fclose(dout);
	progress = oprogress;
	restart_point = 0;
	bytes = 0;
}

void
recvrequest(const char *cmd, const char *volatile local, const char *remote,
	    const char *lmode, int printnames, int ignorespecial)
{
	FILE *volatile fout;
	FILE *volatile din;
	int (*volatile closefunc)(FILE *);
	sigfunc volatile oldintr;
	sigfunc volatile oldintp;
	int c, d;
	int volatile is_retr;
	int volatile tcrflag;
	int volatile bare_lfs;
	static size_t bufsize;
	static char *buf;
	off_t volatile hashbytes;
	int hash_interval;
	struct stat st;
	time_t mtime;
	struct timeval tval[2];
	int oprogress;
	int opreserve;

	fout = NULL;
	din = NULL;
	hashbytes = mark;
	direction = "received";
	bytes = 0;
	bare_lfs = 0;
	filesize = -1;
	oprogress = progress;
	opreserve = preserve;
	is_retr = (strcmp(cmd, "RETR") == 0);
	if (is_retr && verbose && printnames) {
		if (ignorespecial || *local != '-')
			fprintf(ttyout, "local: %s ", local);
		if (remote)
			fprintf(ttyout, "remote: %s\n", remote);
	}
	if (proxy && is_retr) {
		proxtrans(cmd, local, remote);
		return;
	}
	closefunc = NULL;
	oldintr = NULL;
	oldintp = NULL;
	tcrflag = !crflag && is_retr;
	if (sigsetjmp(xferabort, 1)) {
		while (cpend)
			(void)getreply(0);
		code = -1;
		goto cleanuprecv;
	}
	(void)xsignal(SIGQUIT, psummary);
	oldintr = xsignal(SIGINT, abortxfer);
	if (ignorespecial || (strcmp(local, "-") && *local != '|')) {
		if (access(local, W_OK) < 0) {
			char *dir = strrchr(local, '/');

			if (errno != ENOENT && errno != EACCES) {
				warn("Can't access `%s'", local);
				code = -1;
				goto cleanuprecv;
			}
			if (dir != NULL)
				*dir = 0;
			d = access(dir == local ? "/" :
			    dir ? local : ".", W_OK);
			if (dir != NULL)
				*dir = '/';
			if (d < 0) {
				warn("Can't access `%s'", local);
				code = -1;
				goto cleanuprecv;
			}
			if (!runique && errno == EACCES &&
			    chmod(local, (S_IRUSR|S_IWUSR)) < 0) {
				warn("Can't chmod `%s'", local);
				code = -1;
				goto cleanuprecv;
			}
			if (runique && errno == EACCES &&
			   (local = gunique(local)) == NULL) {
				code = -1;
				goto cleanuprecv;
			}
		}
		else if (runique && (local = gunique(local)) == NULL) {
			code = -1;
			goto cleanuprecv;
		}
	}
	if (!is_retr) {
		if (curtype != TYPE_A)
			changetype(TYPE_A, 0);
	} else {
		if (curtype != type)
			changetype(type, 0);
		filesize = remotesize(remote, 0);
		if (code == 421 || code == -1)
			goto cleanuprecv;
	}
	if (initconn()) {
		code = -1;
		goto cleanuprecv;
	}
	if (sigsetjmp(xferabort, 1))
		goto abort;
	if (is_retr && restart_point &&
	    command("REST " LLF, (LLT) restart_point) != CONTINUE)
		goto cleanuprecv;
	if (! EMPTYSTRING(remote)) {
		if (command("%s %s", cmd, remote) != PRELIM)
			goto cleanuprecv;
	} else {
		if (command("%s", cmd) != PRELIM)
			goto cleanuprecv;
	}
	din = dataconn("r");
	if (din == NULL)
		goto abort;
	if (!ignorespecial && strcmp(local, "-") == 0) {
		fout = stdout;
		progress = 0;
		preserve = 0;
	} else if (!ignorespecial && *local == '|') {
		oldintp = xsignal(SIGPIPE, SIG_IGN);
		fout = popen(local + 1, "w");
		if (fout == NULL) {
			warn("Can't execute `%s'", local+1);
			goto abort;
		}
		progress = 0;
		preserve = 0;
		closefunc = pclose;
	} else {
		fout = fopen(local, lmode);
		if (fout == NULL) {
			warn("Can't open `%s'", local);
			goto abort;
		}
		closefunc = fclose;
	}

	if (fstat(fileno(fout), &st) != -1 && !S_ISREG(st.st_mode)) {
		progress = 0;
		preserve = 0;
	}
	if ((size_t)rcvbuf_size > bufsize) {
		if (buf)
			(void)free(buf);
		bufsize = rcvbuf_size;
		buf = ftp_malloc(bufsize);
	}

	progressmeter(-1);
	hash_interval = (hash && (!progress || filesize < 0)) ? mark : 0;

	switch (curtype) {

	case TYPE_I:
	case TYPE_L:
		if (is_retr && restart_point &&
		    lseek(fileno(fout), restart_point, SEEK_SET) < 0) {
			warn("Can't seek to restart `%s'", local);
			goto cleanuprecv;
		}
		c = copy_bytes(fileno(din), fileno(fout), buf, bufsize,
			       rate_get, hash_interval);
		if (c == 1) {
			if (errno != EPIPE)
				warn("Reading from network");
			bytes = -1;
		} else if (c == 2) {
			warn("Writing `%s'", local);
		}
		break;

	case TYPE_A:
		if (is_retr && restart_point) {
			int ch;
			off_t i;

			if (fseeko(fout, (off_t)0, SEEK_SET) < 0)
				goto done;
			for (i = 0; i++ < restart_point;) {
				if ((ch = getc(fout)) == EOF)
					goto done;
				if (ch == '\n')
					i++;
			}
			if (fseeko(fout, (off_t)0, SEEK_CUR) < 0) {
 done:
				warn("Can't seek to restart `%s'", local);
				goto cleanuprecv;
			}
		}
		while ((c = getc(din)) != EOF) {
			if (c == '\n')
				bare_lfs++;
			while (c == '\r') {
				while (hash_interval && bytes >= hashbytes) {
					(void)putc('#', ttyout);
					(void)fflush(ttyout);
					hashbytes += mark;
				}
				bytes++;
				if ((c = getc(din)) != '\n' || tcrflag) {
					if (ferror(fout))
						goto break2;
					(void)putc('\r', fout);
					if (c == '\0') {
						bytes++;
						goto contin2;
					}
					if (c == EOF)
						goto contin2;
				}
			}
			(void)putc(c, fout);
			bytes++;
	contin2:	;
		}
 break2:
		if (hash_interval) {
			if (bytes < hashbytes)
				(void)putc('#', ttyout);
			(void)putc('\n', ttyout);
		}
		if (ferror(din)) {
			if (errno != EPIPE)
				warn("Reading from network");
			bytes = -1;
		}
		if (ferror(fout))
			warn("Writing `%s'", local);
		break;
	}

	progressmeter(1);
	if (closefunc != NULL) {
		(*closefunc)(fout);
		fout = NULL;
	}
	(void)fclose(din);
	din = NULL;
	(void)getreply(0);
	if (bare_lfs) {
		fprintf(ttyout,
		    "WARNING! %d bare linefeeds received in ASCII mode.\n",
		    bare_lfs);
		fputs("File may not have transferred correctly.\n", ttyout);
	}
	if (bytes >= 0 && is_retr) {
		if (bytes > 0)
			ptransfer(0);
		if (preserve && (closefunc == fclose)) {
			mtime = remotemodtime(remote, 0);
			if (mtime != -1) {
				(void)gettimeofday(&tval[0], NULL);
				tval[1].tv_sec = mtime;
				tval[1].tv_usec = 0;
				if (utimes(local, tval) == -1) {
					fprintf(ttyout,
				"Can't change modification time on %s to %s",
					    local,
					    rfc2822time(localtime(&mtime)));
				}
			}
		}
	}
	goto cleanuprecv;

 abort:
			/*
			 * abort using RFC0959 recommended IP,SYNC sequence
			 */
	if (! sigsetjmp(xferabort, 1)) {
			/* this is the first call */
		(void)xsignal(SIGINT, abort_squared);
		if (!cpend) {
			code = -1;
			goto cleanuprecv;
		}
		abort_remote(din);
	}
	code = -1;
	if (bytes > 0)
		ptransfer(0);

 cleanuprecv:
	if (oldintr)
		(void)xsignal(SIGINT, oldintr);
	if (oldintp)
		(void)xsignal(SIGPIPE, oldintp);
	if (data >= 0) {
		(void)close(data);
		data = -1;
	}
	if (closefunc != NULL && fout != NULL)
		(*closefunc)(fout);
	if (din)
		(void)fclose(din);
	progress = oprogress;
	preserve = opreserve;
	bytes = 0;
}

/*
 * Need to start a listen on the data channel before we send the command,
 * otherwise the server's connect may fail.
 */
int
initconn(void)
{
	char *p, *a;
	int result, tmpno = 0;
	int on = 1;
	int error;
	unsigned int addr[16], port[2];
	unsigned int af, hal, pal;
	socklen_t len;
	const char *pasvcmd = NULL;
	int overbose;

#ifdef INET6
#ifndef NO_DEBUG
	if (myctladdr.su_family == AF_INET6 && ftp_debug &&
	    (IN6_IS_ADDR_LINKLOCAL(&myctladdr.si_su.su_sin6.sin6_addr) ||
	     IN6_IS_ADDR_SITELOCAL(&myctladdr.si_su.su_sin6.sin6_addr))) {
		warnx("Use of scoped addresses can be troublesome");
	}
#endif
#endif

 reinit:
	if (passivemode) {
		data_addr = myctladdr;
		data = socket(data_addr.su_family, SOCK_STREAM, 0);
		if (data < 0) {
			warn("Can't create socket for data connection");
			return (1);
		}
		if ((options & SO_DEBUG) &&
		    setsockopt(data, SOL_SOCKET, SO_DEBUG,
				(void *)&on, sizeof(on)) == -1) {
			DWARN("setsockopt %s (ignored)", "SO_DEBUG");
		}
		result = COMPLETE + 1;
		switch (data_addr.su_family) {
		case AF_INET:
			if (epsv4 && !epsv4bad) {
				pasvcmd = "EPSV";
				overbose = verbose;
				if (ftp_debug == 0)
					verbose = -1;
				result = command("EPSV");
				verbose = overbose;
				if (verbose > 0 &&
				    (result == COMPLETE || !connected))
					fprintf(ttyout, "%s\n", reply_string);
				if (!connected)
					return (1);
				/*
				 * this code is to be friendly with broken
				 * BSDI ftpd
				 */
				if (code / 10 == 22 && code != 229) {
					fputs(
"wrong server: return code must be 229\n",
						ttyout);
					result = COMPLETE + 1;
				}
				if (result != COMPLETE) {
					epsv4bad = 1;
					DPRINTF("disabling epsv4 for this "
					    "connection\n");
				}
			}
			if (result != COMPLETE) {
				pasvcmd = "PASV";
				result = command("PASV");
				if (!connected)
					return (1);
			}
			break;
#ifdef INET6
		case AF_INET6:
			if (epsv6 && !epsv6bad) {
				pasvcmd = "EPSV";
				overbose = verbose;
				if (ftp_debug == 0)
					verbose = -1;
				result = command("EPSV");
				verbose = overbose;
				if (verbose > 0 &&
				    (result == COMPLETE || !connected))
					fprintf(ttyout, "%s\n", reply_string);
				if (!connected)
					return (1);
				/*
				 * this code is to be friendly with
				 * broken BSDI ftpd
				 */
				if (code / 10 == 22 && code != 229) {
					fputs(
						"wrong server: return code must be 229\n",
						ttyout);
					result = COMPLETE + 1;
				}
				if (result != COMPLETE) {
					epsv6bad = 1;
					DPRINTF("disabling epsv6 for this "
					    "connection\n");
				}
			}
			if (result != COMPLETE) {
				pasvcmd = "LPSV";
				result = command("LPSV");
			}
			if (!connected)
				return (1);
			break;
#endif
		default:
			result = COMPLETE + 1;
			break;
		}
		if (result != COMPLETE) {
			if (activefallback) {
				(void)close(data);
				data = -1;
				passivemode = 0;
#if 0
				activefallback = 0;
#endif
				goto reinit;
			}
			fputs("Passive mode refused.\n", ttyout);
			goto bad;
		}

#define	pack2(var, off) \
	(((var[(off) + 0] & 0xff) << 8) | ((var[(off) + 1] & 0xff) << 0))
#define	pack4(var, off) \
	(((var[(off) + 0] & 0xff) << 24) | ((var[(off) + 1] & 0xff) << 16) | \
	 ((var[(off) + 2] & 0xff) << 8) | ((var[(off) + 3] & 0xff) << 0))
#define	UC(b)	(((int)b)&0xff)

		/*
		 * What we've got at this point is a string of comma separated
		 * one-byte unsigned integer values, separated by commas.
		 */
		if (strcmp(pasvcmd, "PASV") == 0) {
			if (data_addr.su_family != AF_INET) {
				fputs(
    "Passive mode AF mismatch. Shouldn't happen!\n", ttyout);
				error = 1;
				goto bad;
			}
			if (code / 10 == 22 && code != 227) {
				fputs("wrong server: return code must be 227\n",
					ttyout);
				error = 1;
				goto bad;
			}
			error = sscanf(pasv, "%u,%u,%u,%u,%u,%u",
					&addr[0], &addr[1], &addr[2], &addr[3],
					&port[0], &port[1]);
			if (error != 6) {
				fputs(
"Passive mode address scan failure. Shouldn't happen!\n", ttyout);
				error = 1;
				goto bad;
			}
			error = 0;
			memset(&data_addr, 0, sizeof(data_addr));
			data_addr.su_family = AF_INET;
			data_addr.su_len = sizeof(struct sockaddr_in);
			data_addr.si_su.su_sin.sin_addr.s_addr =
			    htonl(pack4(addr, 0));
			data_addr.su_port = htons(pack2(port, 0));
		} else if (strcmp(pasvcmd, "LPSV") == 0) {
			if (code / 10 == 22 && code != 228) {
				fputs("wrong server: return code must be 228\n",
					ttyout);
				error = 1;
				goto bad;
			}
			switch (data_addr.su_family) {
			case AF_INET:
				error = sscanf(pasv,
"%u,%u,%u,%u,%u,%u,%u,%u,%u",
					&af, &hal,
					&addr[0], &addr[1], &addr[2], &addr[3],
					&pal, &port[0], &port[1]);
				if (error != 9) {
					fputs(
"Passive mode address scan failure. Shouldn't happen!\n", ttyout);
					error = 1;
					goto bad;
				}
				if (af != 4 || hal != 4 || pal != 2) {
					fputs(
"Passive mode AF mismatch. Shouldn't happen!\n", ttyout);
					error = 1;
					goto bad;
				}

				error = 0;
				memset(&data_addr, 0, sizeof(data_addr));
				data_addr.su_family = AF_INET;
				data_addr.su_len = sizeof(struct sockaddr_in);
				data_addr.si_su.su_sin.sin_addr.s_addr =
				    htonl(pack4(addr, 0));
				data_addr.su_port = htons(pack2(port, 0));
				break;
#ifdef INET6
			case AF_INET6:
				error = sscanf(pasv,
"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u",
					&af, &hal,
					&addr[0], &addr[1], &addr[2], &addr[3],
					&addr[4], &addr[5], &addr[6], &addr[7],
					&addr[8], &addr[9], &addr[10],
					&addr[11], &addr[12], &addr[13],
					&addr[14], &addr[15],
					&pal, &port[0], &port[1]);
				if (error != 21) {
					fputs(
"Passive mode address scan failure. Shouldn't happen!\n", ttyout);
					error = 1;
					goto bad;
				}
				if (af != 6 || hal != 16 || pal != 2) {
					fputs(
"Passive mode AF mismatch. Shouldn't happen!\n", ttyout);
					error = 1;
					goto bad;
				}

				error = 0;
				memset(&data_addr, 0, sizeof(data_addr));
				data_addr.su_family = AF_INET6;
				data_addr.su_len = sizeof(struct sockaddr_in6);
			    {
				size_t i;
				for (i = 0; i < sizeof(struct in6_addr); i++) {
					data_addr.si_su.su_sin6.sin6_addr.s6_addr[i] =
					    UC(addr[i]);
				}
			    }
				data_addr.su_port = htons(pack2(port, 0));
				break;
#endif
			default:
				error = 1;
			}
		} else if (strcmp(pasvcmd, "EPSV") == 0) {
			char delim[4];

			port[0] = 0;
			if (code / 10 == 22 && code != 229) {
				fputs("wrong server: return code must be 229\n",
					ttyout);
				error = 1;
				goto bad;
			}
			if (sscanf(pasv, "%c%c%c%d%c", &delim[0],
					&delim[1], &delim[2], &port[1],
					&delim[3]) != 5) {
				fputs("parse error!\n", ttyout);
				error = 1;
				goto bad;
			}
			if (delim[0] != delim[1] || delim[0] != delim[2]
			 || delim[0] != delim[3]) {
				fputs("parse error!\n", ttyout);
				error = 1;
				goto bad;
			}
			data_addr = hisctladdr;
			data_addr.su_port = htons(port[1]);
		} else
			goto bad;

		if (ftp_connect(data, (struct sockaddr *)&data_addr.si_su,
		    data_addr.su_len) < 0) {
			if (activefallback) {
				(void)close(data);
				data = -1;
				passivemode = 0;
#if 0
				activefallback = 0;
#endif
				goto reinit;
			}
			goto bad;
		}
#ifdef IPTOS_THROUGHPUT
		if (data_addr.su_family == AF_INET) {
			on = IPTOS_THROUGHPUT;
			if (setsockopt(data, IPPROTO_IP, IP_TOS,
					(void *)&on, sizeof(on)) == -1) {
				DWARN("setsockopt %s (ignored)",
				    "IPTOS_THROUGHPUT");
			}
		}
#endif
		return (0);
	}

 noport:
	data_addr = myctladdr;
	if (sendport)
		data_addr.su_port = 0;	/* let system pick one */
	if (data != -1)
		(void)close(data);
	data = socket(data_addr.su_family, SOCK_STREAM, 0);
	if (data < 0) {
		warn("Can't create socket for data connection");
		if (tmpno)
			sendport = 1;
		return (1);
	}
	if (!sendport)
		if (setsockopt(data, SOL_SOCKET, SO_REUSEADDR,
				(void *)&on, sizeof(on)) == -1) {
			warn("Can't set SO_REUSEADDR on data connection");
			goto bad;
		}
	if (bind(data, (struct sockaddr *)&data_addr.si_su,
	    data_addr.su_len) < 0) {
		warn("Can't bind for data connection");
		goto bad;
	}
	if ((options & SO_DEBUG) &&
	    setsockopt(data, SOL_SOCKET, SO_DEBUG,
			(void *)&on, sizeof(on)) == -1) {
		DWARN("setsockopt %s (ignored)", "SO_DEBUG");
	}
	len = sizeof(data_addr.si_su);
	memset((char *)&data_addr, 0, sizeof (data_addr));
	if (getsockname(data, (struct sockaddr *)&data_addr.si_su, &len) == -1) {
		warn("Can't determine my address of data connection");
		goto bad;
	}
	data_addr.su_len = len;
	if (ftp_listen(data, 1) < 0)
		warn("Can't listen to data connection");

	if (sendport) {
		char hname[NI_MAXHOST], sname[NI_MAXSERV];
		struct sockinet tmp;

		switch (data_addr.su_family) {
		case AF_INET:
			if (!epsv4 || epsv4bad) {
				result = COMPLETE + 1;
				break;
			}
			/* FALLTHROUGH */
#ifdef INET6
		case AF_INET6:
			if (!epsv6 || epsv6bad) {
				result = COMPLETE + 1;
				break;
			}
#endif
			af = (data_addr.su_family == AF_INET) ? 1 : 2;
			tmp = data_addr;
#ifdef INET6
			if (tmp.su_family == AF_INET6)
				tmp.si_su.su_sin6.sin6_scope_id = 0;
#endif
			if (getnameinfo((struct sockaddr *)&tmp.si_su,
			    tmp.su_len, hname, sizeof(hname), sname,
			    sizeof(sname), NI_NUMERICHOST | NI_NUMERICSERV)) {
				result = ERROR;
			} else {
				overbose = verbose;
				if (ftp_debug == 0)
					verbose = -1;
				result = command("EPRT |%u|%s|%s|", af, hname,
				    sname);
				verbose = overbose;
				if (verbose > 0 &&
				    (result == COMPLETE || !connected))
					fprintf(ttyout, "%s\n", reply_string);
				if (!connected)
					return (1);
				if (result != COMPLETE) {
					epsv4bad = 1;
					DPRINTF("disabling epsv4 for this "
					    "connection\n");
				}
			}
			break;
		default:
			result = COMPLETE + 1;
			break;
		}
		if (result == COMPLETE)
			goto skip_port;

		switch (data_addr.su_family) {
		case AF_INET:
			a = (char *)&data_addr.si_su.su_sin.sin_addr;
			p = (char *)&data_addr.su_port;
			result = command("PORT %d,%d,%d,%d,%d,%d",
				 UC(a[0]), UC(a[1]), UC(a[2]), UC(a[3]),
				 UC(p[0]), UC(p[1]));
			break;
#ifdef INET6
		case AF_INET6:
			a = (char *)&data_addr.si_su.su_sin6.sin6_addr;
			p = (char *)&data_addr.su_port;
			result = command(
	"LPRT %d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
				 6, 16,
				 UC(a[0]),UC(a[1]),UC(a[2]),UC(a[3]),
				 UC(a[4]),UC(a[5]),UC(a[6]),UC(a[7]),
				 UC(a[8]),UC(a[9]),UC(a[10]),UC(a[11]),
				 UC(a[12]),UC(a[13]),UC(a[14]),UC(a[15]),
				 2, UC(p[0]), UC(p[1]));
			break;
#endif
		default:
			result = COMPLETE + 1; /* xxx */
		}
		if (!connected)
			return (1);
	skip_port:

		if (result == ERROR && sendport == -1) {
			sendport = 0;
			tmpno = 1;
			goto noport;
		}
		return (result != COMPLETE);
	}
	if (tmpno)
		sendport = 1;
#ifdef IPTOS_THROUGHPUT
	if (data_addr.su_family == AF_INET) {
		on = IPTOS_THROUGHPUT;
		if (setsockopt(data, IPPROTO_IP, IP_TOS,
				(void *)&on, sizeof(on)) == -1) {
			DWARN("setsockopt %s (ignored)", "IPTOS_THROUGHPUT");
		}
	}
#endif
	return (0);
 bad:
	(void)close(data);
	data = -1;
	if (tmpno)
		sendport = 1;
	return (1);
}

FILE *
dataconn(const char *lmode)
{
	struct sockinet	from;
	int		s, flags, rv, timeout;
	struct timeval	endtime, now, td;
	struct pollfd	pfd[1];
	socklen_t	fromlen;

	if (passivemode)	/* passive data connection */
		return (fdopen(data, lmode));

				/* active mode data connection */

	if ((flags = fcntl(data, F_GETFL, 0)) == -1)
		goto dataconn_failed;		/* get current socket flags  */
	if (fcntl(data, F_SETFL, flags | O_NONBLOCK) == -1)
		goto dataconn_failed;		/* set non-blocking connect */

		/* NOTE: we now must restore socket flags on successful exit */

				/* limit time waiting on listening socket */
	pfd[0].fd = data;
	pfd[0].events = POLLIN;
	(void)gettimeofday(&endtime, NULL);	/* determine end time */
	endtime.tv_sec += (quit_time > 0) ? quit_time: 60;
						/* without -q, default to 60s */
	do {
		(void)gettimeofday(&now, NULL);
		timersub(&endtime, &now, &td);
		timeout = td.tv_sec * 1000 + td.tv_usec/1000;
		if (timeout < 0)
			timeout = 0;
		rv = ftp_poll(pfd, 1, timeout);
	} while (rv == -1 && errno == EINTR);	/* loop until poll ! EINTR */
	if (rv == -1) {
		warn("Can't poll waiting before accept");
		goto dataconn_failed;
	}
	if (rv == 0) {
		warnx("Poll timeout waiting before accept");
		goto dataconn_failed;
	}

				/* (non-blocking) accept the connection */
	fromlen = myctladdr.su_len;
	do {
		s = accept(data, (struct sockaddr *) &from.si_su, &fromlen);
	} while (s == -1 && errno == EINTR);	/* loop until accept ! EINTR */
	if (s == -1) {
		warn("Can't accept data connection");
		goto dataconn_failed;
	}

	(void)close(data);
	data = s;
	if (fcntl(data, F_SETFL, flags) == -1)	/* restore socket flags */
		goto dataconn_failed;

#ifdef IPTOS_THROUGHPUT
	if (from.su_family == AF_INET) {
		int tos = IPTOS_THROUGHPUT;
		if (setsockopt(s, IPPROTO_IP, IP_TOS,
				(void *)&tos, sizeof(tos)) == -1) {
			DWARN("setsockopt %s (ignored)", "IPTOS_THROUGHPUT");
		}
	}
#endif
	return (fdopen(data, lmode));

 dataconn_failed:
	(void)close(data);
	data = -1;
	return (NULL);
}

void
psabort(int notused)
{
	int oerrno = errno;

	sigint_raised = 1;
	alarmtimer(0);
	abrtflag++;
	errno = oerrno;
}

void
pswitch(int flag)
{
	sigfunc oldintr;
	static struct comvars {
		int connect;
		char name[MAXHOSTNAMELEN];
		struct sockinet mctl;
		struct sockinet hctl;
		FILE *in;
		FILE *out;
		int tpe;
		int curtpe;
		int cpnd;
		int sunqe;
		int runqe;
		int mcse;
		int ntflg;
		char nti[17];
		char nto[17];
		int mapflg;
		char mi[MAXPATHLEN];
		char mo[MAXPATHLEN];
	} proxstruct, tmpstruct;
	struct comvars *ip, *op;

	abrtflag = 0;
	oldintr = xsignal(SIGINT, psabort);
	if (flag) {
		if (proxy)
			return;
		ip = &tmpstruct;
		op = &proxstruct;
		proxy++;
	} else {
		if (!proxy)
			return;
		ip = &proxstruct;
		op = &tmpstruct;
		proxy = 0;
	}
	ip->connect = connected;
	connected = op->connect;
	if (hostname)
		(void)strlcpy(ip->name, hostname, sizeof(ip->name));
	else
		ip->name[0] = '\0';
	hostname = op->name;
	ip->hctl = hisctladdr;
	hisctladdr = op->hctl;
	ip->mctl = myctladdr;
	myctladdr = op->mctl;
	ip->in = cin;
	cin = op->in;
	ip->out = cout;
	cout = op->out;
	ip->tpe = type;
	type = op->tpe;
	ip->curtpe = curtype;
	curtype = op->curtpe;
	ip->cpnd = cpend;
	cpend = op->cpnd;
	ip->sunqe = sunique;
	sunique = op->sunqe;
	ip->runqe = runique;
	runique = op->runqe;
	ip->mcse = mcase;
	mcase = op->mcse;
	ip->ntflg = ntflag;
	ntflag = op->ntflg;
	(void)strlcpy(ip->nti, ntin, sizeof(ip->nti));
	(void)strlcpy(ntin, op->nti, sizeof(ntin));
	(void)strlcpy(ip->nto, ntout, sizeof(ip->nto));
	(void)strlcpy(ntout, op->nto, sizeof(ntout));
	ip->mapflg = mapflag;
	mapflag = op->mapflg;
	(void)strlcpy(ip->mi, mapin, sizeof(ip->mi));
	(void)strlcpy(mapin, op->mi, sizeof(mapin));
	(void)strlcpy(ip->mo, mapout, sizeof(ip->mo));
	(void)strlcpy(mapout, op->mo, sizeof(mapout));
	(void)xsignal(SIGINT, oldintr);
	if (abrtflag) {
		abrtflag = 0;
		(*oldintr)(SIGINT);
	}
}

void
abortpt(int notused)
{

	sigint_raised = 1;
	alarmtimer(0);
	if (fromatty)
		write(fileno(ttyout), "\n", 1);
	ptabflg++;
	mflag = 0;
	abrtflag = 0;
	siglongjmp(ptabort, 1);
}

void
proxtrans(const char *cmd, const char *local, const char *remote)
{
	sigfunc volatile oldintr;
	int prox_type, nfnd;
	int volatile secndflag;
	const char *volatile cmd2;

	oldintr = NULL;
	secndflag = 0;
	if (strcmp(cmd, "RETR"))
		cmd2 = "RETR";
	else
		cmd2 = runique ? "STOU" : "STOR";
	if ((prox_type = type) == 0) {
		if (unix_server && unix_proxy)
			prox_type = TYPE_I;
		else
			prox_type = TYPE_A;
	}
	if (curtype != prox_type)
		changetype(prox_type, 1);
	if (command("PASV") != COMPLETE) {
		fputs("proxy server does not support third party transfers.\n",
		    ttyout);
		return;
	}
	pswitch(0);
	if (!connected) {
		fputs("No primary connection.\n", ttyout);
		pswitch(1);
		code = -1;
		return;
	}
	if (curtype != prox_type)
		changetype(prox_type, 1);
	if (command("PORT %s", pasv) != COMPLETE) {
		pswitch(1);
		return;
	}
	if (sigsetjmp(ptabort, 1))
		goto abort;
	oldintr = xsignal(SIGINT, abortpt);
	if ((restart_point &&
	    (command("REST " LLF, (LLT) restart_point) != CONTINUE))
	    || (command("%s %s", cmd, remote) != PRELIM)) {
		(void)xsignal(SIGINT, oldintr);
		pswitch(1);
		return;
	}
	sleep(2);
	pswitch(1);
	secndflag++;
	if ((restart_point &&
	    (command("REST " LLF, (LLT) restart_point) != CONTINUE))
	    || (command("%s %s", cmd2, local) != PRELIM))
		goto abort;
	ptflag++;
	(void)getreply(0);
	pswitch(0);
	(void)getreply(0);
	(void)xsignal(SIGINT, oldintr);
	pswitch(1);
	ptflag = 0;
	fprintf(ttyout, "local: %s remote: %s\n", local, remote);
	return;
 abort:
	if (sigsetjmp(xferabort, 1)) {
		(void)xsignal(SIGINT, oldintr);
		return;
	}
	(void)xsignal(SIGINT, abort_squared);
	ptflag = 0;
	if (strcmp(cmd, "RETR") && !proxy)
		pswitch(1);
	else if (!strcmp(cmd, "RETR") && proxy)
		pswitch(0);
	if (!cpend && !secndflag) {  /* only here if cmd = "STOR" (proxy=1) */
		if (command("%s %s", cmd2, local) != PRELIM) {
			pswitch(0);
			if (cpend)
				abort_remote(NULL);
		}
		pswitch(1);
		if (ptabflg)
			code = -1;
		(void)xsignal(SIGINT, oldintr);
		return;
	}
	if (cpend)
		abort_remote(NULL);
	pswitch(!proxy);
	if (!cpend && !secndflag) {  /* only if cmd = "RETR" (proxy=1) */
		if (command("%s %s", cmd2, local) != PRELIM) {
			pswitch(0);
			if (cpend)
				abort_remote(NULL);
			pswitch(1);
			if (ptabflg)
				code = -1;
			(void)xsignal(SIGINT, oldintr);
			return;
		}
	}
	if (cpend)
		abort_remote(NULL);
	pswitch(!proxy);
	if (cpend) {
		if ((nfnd = empty(cin, NULL, 10)) <= 0) {
			if (nfnd < 0)
				warn("Error aborting proxy command");
			if (ptabflg)
				code = -1;
			lostpeer(0);
		}
		(void)getreply(0);
		(void)getreply(0);
	}
	if (proxy)
		pswitch(0);
	pswitch(1);
	if (ptabflg)
		code = -1;
	(void)xsignal(SIGINT, oldintr);
}

void
reset(int argc, char *argv[])
{
	int nfnd = 1;

	if (argc == 0 && argv != NULL) {
		UPRINTF("usage: %s\n", argv[0]);
		code = -1;
		return;
	}
	while (nfnd > 0) {
		if ((nfnd = empty(cin, NULL, 0)) < 0) {
			warn("Error resetting connection");
			code = -1;
			lostpeer(0);
		} else if (nfnd)
			(void)getreply(0);
	}
}

char *
gunique(const char *local)
{
	static char new[MAXPATHLEN];
	char *cp = strrchr(local, '/');
	int d, count=0, len;
	char ext = '1';

	if (cp)
		*cp = '\0';
	d = access(cp == local ? "/" : cp ? local : ".", W_OK);
	if (cp)
		*cp = '/';
	if (d < 0) {
		warn("Can't access `%s'", local);
		return (NULL);
	}
	len = strlcpy(new, local, sizeof(new));
	cp = &new[len];
	*cp++ = '.';
	while (!d) {
		if (++count == 100) {
			fputs("runique: can't find unique file name.\n",
			    ttyout);
			return (NULL);
		}
		*cp++ = ext;
		*cp = '\0';
		if (ext == '9')
			ext = '0';
		else
			ext++;
		if ((d = access(new, F_OK)) < 0)
			break;
		if (ext != '0')
			cp--;
		else if (*(cp - 2) == '.')
			*(cp - 1) = '1';
		else {
			*(cp - 2) = *(cp - 2) + 1;
			cp--;
		}
	}
	return (new);
}

/*
 * abort_squared --
 *	aborts abort_remote(). lostpeer() is called because if the user is
 *	too impatient to wait or there's another problem then ftp really
 *	needs to get back to a known state.
 */
void
abort_squared(int dummy)
{
	char msgbuf[100];
	size_t len;

	sigint_raised = 1;
	alarmtimer(0);
	len = strlcpy(msgbuf, "\nremote abort aborted; closing connection.\n",
	    sizeof(msgbuf));
	write(fileno(ttyout), msgbuf, len);
	lostpeer(0);
	siglongjmp(xferabort, 1);
}

void
abort_remote(FILE *din)
{
	unsigned char buf[BUFSIZ];
	int nfnd;

	if (cout == NULL) {
		warnx("Lost control connection for abort");
		if (ptabflg)
			code = -1;
		lostpeer(0);
		return;
	}
	/*
	 * send IAC in urgent mode instead of DM because 4.3BSD places oob mark
	 * after urgent byte rather than before as is protocol now
	 */
	buf[0] = IAC;
	buf[1] = IP;
	buf[2] = IAC;
	if (send(fileno(cout), buf, 3, MSG_OOB) != 3)
		warn("Can't send abort message");
	fprintf(cout, "%cABOR\r\n", DM);
	(void)fflush(cout);
	if ((nfnd = empty(cin, din, 10)) <= 0) {
		if (nfnd < 0)
			warn("Can't send abort message");
		if (ptabflg)
			code = -1;
		lostpeer(0);
	}
	if (din && (nfnd & 2)) {
		while (read(fileno(din), buf, BUFSIZ) > 0)
			continue;
	}
	if (getreply(0) == ERROR && code == 552) {
		/* 552 needed for nic style abort */
		(void)getreply(0);
	}
	(void)getreply(0);
}

/*
 * Ensure that ai->ai_addr is NOT an IPv4 mapped address.
 * IPv4 mapped address complicates too many things in FTP
 * protocol handling, as FTP protocol is defined differently
 * between IPv4 and IPv6.
 *
 * This may not be the best way to handle this situation,
 * since the semantics of IPv4 mapped address is defined in
 * the kernel.  There are configurations where we should use
 * IPv4 mapped address as native IPv6 address, not as
 * "an IPv6 address that embeds IPv4 address" (namely, SIIT).
 *
 * More complete solution would be to have an additional
 * getsockopt to grab "real" peername/sockname.  "real"
 * peername/sockname will be AF_INET if IPv4 mapped address
 * is used to embed IPv4 address, and will be AF_INET6 if
 * we use it as native.  What a mess!
 */
void
ai_unmapped(struct addrinfo *ai)
{
#ifdef INET6
	struct sockaddr_in6 *sin6;
	struct sockaddr_in sin;
	socklen_t len;

	if (ai->ai_family != AF_INET6)
		return;
	if (ai->ai_addrlen != sizeof(struct sockaddr_in6) ||
	    sizeof(sin) > ai->ai_addrlen)
		return;
	sin6 = (struct sockaddr_in6 *)ai->ai_addr;
	if (!IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr))
		return;

	memset(&sin, 0, sizeof(sin));
	sin.sin_family = AF_INET;
	len = sizeof(struct sockaddr_in);
	memcpy(&sin.sin_addr, &sin6->sin6_addr.s6_addr[12],
	    sizeof(sin.sin_addr));
	sin.sin_port = sin6->sin6_port;

	ai->ai_family = AF_INET;
#if defined(HAVE_STRUCT_SOCKADDR_IN_SIN_LEN)
	sin.sin_len = len;
#endif
	memcpy(ai->ai_addr, &sin, len);
	ai->ai_addrlen = len;
#endif
}

#ifdef NO_USAGE
void
xusage(void)
{
	fputs("Usage error\n", ttyout);
}
#endif
