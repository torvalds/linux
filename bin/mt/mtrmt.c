/*	$OpenBSD: mtrmt.c,v 1.25 2023/03/08 04:43:04 guenther Exp $	*/
/*	$NetBSD: mtrmt.c,v 1.2 1996/03/06 06:22:07 scottr Exp $	*/

/*-
 * Copyright (c) 1980, 1993
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

#include <sys/types.h>
#include <sys/mtio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <ufs/ufs/dinode.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <protocols/dumprestore.h>

#include <ctype.h>
#include <err.h>
#include <netdb.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>

#include "pathnames.h"
#include "mt.h"

#define	TS_CLOSED	0
#define	TS_OPEN		1

static	int rmtstate = TS_CLOSED;
static	int rmtape;
static	char *rmtpeer;

static	int okname(char *);
static	int rmtcall(char *, char *);
static	void rmtconnaborted(void);
static	void sigrmtconnaborted(int);
static	int rmtgetb(void);
static	void rmtgetconn(void);
static	void rmtgets(char *, int);
static	int rmtreply(char *);

int
rmthost(char *host)
{
	if ((rmtpeer = strdup(host)) == NULL)
		err(1, "strdup");
	signal(SIGPIPE, sigrmtconnaborted);
	rmtgetconn();
	if (rmtape < 0)
		return (0);
	return (1);
}

static void
sigrmtconnaborted(int signo)
{

	warnx("Lost connection to remote host.");
	_exit(1);
}

static void
rmtconnaborted(void)
{

	errx(1, "Lost connection to remote host.");
}

void
rmtgetconn(void)
{
	char *cp;
	static struct passwd *pwd = NULL;
#ifdef notdef
	static int on = 1;
#endif
	char *tuser;
	int size;
	int maxseg;

	if ((cp = strchr(rmtpeer, '@')) != NULL) {
		tuser = rmtpeer;
		*cp = '\0';
		if (!okname(tuser))
			exit(1);
		rmtpeer = ++cp;
	} else
		tuser = pwd->pw_name;

	rmtape = rcmdsh(&rmtpeer, -1, pwd->pw_name, tuser,
	    _PATH_RMT, NULL);
	if (rmtape == -1)
		exit(1);		/* rcmd already printed error message */

	size = TP_BSIZE;
	if (size > 60 * 1024)		/* XXX */
		size = 60 * 1024;
	/* Leave some space for rmt request/response protocol */
	size += 2 * 1024;

	while (size > TP_BSIZE &&
	    setsockopt(rmtape, SOL_SOCKET, SO_SNDBUF, &size, sizeof (size)) == -1)
		    size -= TP_BSIZE;
	(void)setsockopt(rmtape, SOL_SOCKET, SO_RCVBUF, &size, sizeof (size));

	maxseg = 1024;
	(void)setsockopt(rmtape, IPPROTO_TCP, TCP_MAXSEG, &maxseg,
	    sizeof (maxseg));

#ifdef notdef
	if (setsockopt(rmtape, IPPROTO_TCP, TCP_NODELAY, &on, sizeof (on)) == -1)
		perror("TCP_NODELAY setsockopt");
#endif
	if (pledge("stdio", NULL) == -1)
		err(1, "pledge");
}

static int
okname(char *cp0)
{
	unsigned char *cp;
	int c;

	for (cp = cp0; *cp; cp++) {
		c = (unsigned char)*cp;
		if (!isascii(c) || !(isalnum(c) || c == '_' || c == '-')) {
			warnx("invalid user name: %s", cp0);
			return (0);
		}
	}
	return (1);
}

int
rmtopen(char *tape, int mode)
{
	char buf[1 + PATH_MAX+1 + 10+1 +1];
	int r;

	r = snprintf(buf, sizeof (buf), "O%s\n%d\n", tape, mode);
	if (r < 0 || r >= sizeof buf)
		errx(1, "tape name too long");
	rmtstate = TS_OPEN;
	return (rmtcall(tape, buf));
}

void
rmtclose(void)
{

	if (rmtstate != TS_OPEN)
		return;
	rmtcall("close", "C\n");
	rmtstate = TS_CLOSED;
}

struct	mtget mts;

struct mtget *
rmtstatus(void)
{
	int i;
	char *cp;

	if (rmtstate != TS_OPEN)
		return (NULL);
	rmtcall("status", "S\n");
	for (i = 0, cp = (char *)&mts; i < sizeof(mts); i++)
		*cp++ = rmtgetb();
	return (&mts);
}

int
rmtioctl(int cmd, int count)
{
	char buf[1 + 10+1 + 10+1 +1];
	int r;

	if (count < 0)
		return (-1);
	r = snprintf(buf, sizeof (buf), "I%d\n%d\n", cmd, count);
	if (r < 0 || r >= sizeof buf)
		errx(1, "string error during ioctl");
	return (rmtcall("ioctl", buf));
}

static int
rmtcall(char *cmd, char *buf)
{

	if (write(rmtape, buf, strlen(buf)) != strlen(buf))
		rmtconnaborted();
	return (rmtreply(cmd));
}

static int
rmtreply(char *cmd)
{
	char *cp;
	char code[30], emsg[BUFSIZ];

	rmtgets(code, sizeof (code));
	if (*code == 'E' || *code == 'F') {
		rmtgets(emsg, sizeof (emsg));
		warnx("%s: %s", cmd, emsg);
		if (*code == 'F') {
			rmtstate = TS_CLOSED;
			return (-1);
		}
		return (-1);
	}
	if (*code != 'A') {
		/* Kill trailing newline */
		cp = code + strlen(code);
		if (cp > code && *--cp == '\n')
			*cp = '\0';

		warnx("Protocol to remote tape server botched (code \"%s\").",
		    code);
		rmtconnaborted();
	}
	return (atoi(code + 1));
}

int
rmtgetb(void)
{
	char c;

	if (read(rmtape, &c, 1) != 1)
		rmtconnaborted();
	return (c);
}

/* Get a line (guaranteed to have a trailing newline). */
void
rmtgets(char *line, int len)
{
	char *cp = line;

	while (len > 1) {
		*cp = rmtgetb();
		if (*cp == '\n') {
			cp[1] = '\0';
			return;
		}
		cp++;
		len--;
	}
	*cp = '\0';
	warnx("Protocol to remote tape server botched.");
	warnx("(rmtgets got \"%s\").", line);
	rmtconnaborted();
}
