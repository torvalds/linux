/*-
 * Copyright (c) 1991, 1993, 1994
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

#ifndef lint
#if 0
static char sccsid[] = "@(#)key.c	8.3 (Berkeley) 4/2/94";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>

#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "stty.h"
#include "extern.h"

__BEGIN_DECLS
static int c_key(const void *, const void *);
void	f_all(struct info *);
void	f_cbreak(struct info *);
void	f_columns(struct info *);
void	f_dec(struct info *);
void	f_ek(struct info *);
void	f_everything(struct info *);
void	f_extproc(struct info *);
void	f_ispeed(struct info *);
void	f_nl(struct info *);
void	f_ospeed(struct info *);
void	f_raw(struct info *);
void	f_rows(struct info *);
void	f_sane(struct info *);
void	f_size(struct info *);
void	f_speed(struct info *);
void	f_tty(struct info *);
__END_DECLS

static struct key {
	const char *name;			/* name */
	void (*f)(struct info *);		/* function */
#define	F_NEEDARG	0x01			/* needs an argument */
#define	F_OFFOK		0x02			/* can turn off */
	int flags;
} keys[] = {
	{ "all",	f_all,		0 },
	{ "cbreak",	f_cbreak,	F_OFFOK },
	{ "cols",	f_columns,	F_NEEDARG },
	{ "columns",	f_columns,	F_NEEDARG },
	{ "cooked", 	f_sane,		0 },
	{ "dec",	f_dec,		0 },
	{ "ek",		f_ek,		0 },
	{ "everything",	f_everything,	0 },
	{ "extproc",	f_extproc,	F_OFFOK },
	{ "ispeed",	f_ispeed,	F_NEEDARG },
	{ "new",	f_tty,		0 },
	{ "nl",		f_nl,		F_OFFOK },
	{ "old",	f_tty,		0 },
	{ "ospeed",	f_ospeed,	F_NEEDARG },
	{ "raw",	f_raw,		F_OFFOK },
	{ "rows",	f_rows,		F_NEEDARG },
	{ "sane",	f_sane,		0 },
	{ "size",	f_size,		0 },
	{ "speed",	f_speed,	0 },
	{ "tty",	f_tty,		0 },
};

static int
c_key(const void *a, const void *b)
{

        return (strcmp(((const struct key *)a)->name, ((const struct key *)b)->name));
}

int
ksearch(char ***argvp, struct info *ip)
{
	char *name;
	struct key *kp, tmp;

	name = **argvp;
	if (*name == '-') {
		ip->off = 1;
		++name;
	} else
		ip->off = 0;

	tmp.name = name;
	if (!(kp = (struct key *)bsearch(&tmp, keys,
	    sizeof(keys)/sizeof(struct key), sizeof(struct key), c_key)))
		return (0);
	if (!(kp->flags & F_OFFOK) && ip->off) {
		warnx("illegal option -- -%s", name);
		usage();
	}
	if (kp->flags & F_NEEDARG && !(ip->arg = *++*argvp)) {
		warnx("option requires an argument -- %s", name);
		usage();
	}
	kp->f(ip);
	return (1);
}

void
f_all(struct info *ip)
{
	print(&ip->t, &ip->win, ip->ldisc, BSD);
}

void
f_cbreak(struct info *ip)
{

	if (ip->off)
		f_sane(ip);
	else {
		ip->t.c_iflag |= BRKINT|IXON|IMAXBEL;
		ip->t.c_oflag |= OPOST;
		ip->t.c_lflag |= ISIG|IEXTEN;
		ip->t.c_lflag &= ~ICANON;
		ip->set = 1;
	}
}

void
f_columns(struct info *ip)
{

	ip->win.ws_col = atoi(ip->arg);
	ip->wset = 1;
}

void
f_dec(struct info *ip)
{

	ip->t.c_cc[VERASE] = (u_char)0177;
	ip->t.c_cc[VKILL] = CTRL('u');
	ip->t.c_cc[VINTR] = CTRL('c');
	ip->t.c_lflag &= ~ECHOPRT;
	ip->t.c_lflag |= ECHOE|ECHOKE|ECHOCTL;
	ip->t.c_iflag &= ~IXANY;
	ip->set = 1;
}

void
f_ek(struct info *ip)
{

	ip->t.c_cc[VERASE] = CERASE;
	ip->t.c_cc[VKILL] = CKILL;
	ip->set = 1;
}

void
f_everything(struct info *ip)
{

	print(&ip->t, &ip->win, ip->ldisc, BSD);
}

void
f_extproc(struct info *ip)
{

	if (ip->off) {
		int tmp = 0;
		(void)ioctl(ip->fd, TIOCEXT, &tmp);
	} else {
		int tmp = 1;
		(void)ioctl(ip->fd, TIOCEXT, &tmp);
	}
}

void
f_ispeed(struct info *ip)
{

	cfsetispeed(&ip->t, (speed_t)atoi(ip->arg));
	ip->set = 1;
}

void
f_nl(struct info *ip)
{

	if (ip->off) {
		ip->t.c_iflag |= ICRNL;
		ip->t.c_oflag |= ONLCR;
	} else {
		ip->t.c_iflag &= ~ICRNL;
		ip->t.c_oflag &= ~ONLCR;
	}
	ip->set = 1;
}

void
f_ospeed(struct info *ip)
{

	cfsetospeed(&ip->t, (speed_t)atoi(ip->arg));
	ip->set = 1;
}

void
f_raw(struct info *ip)
{

	if (ip->off)
		f_sane(ip);
	else {
		cfmakeraw(&ip->t);
		ip->t.c_cflag &= ~(CSIZE|PARENB);
		ip->t.c_cflag |= CS8;
		ip->set = 1;
	}
}

void
f_rows(struct info *ip)
{

	ip->win.ws_row = atoi(ip->arg);
	ip->wset = 1;
}

void
f_sane(struct info *ip)
{
	struct termios def;

	cfmakesane(&def);
	ip->t.c_cflag = def.c_cflag | (ip->t.c_cflag & CLOCAL);
	ip->t.c_iflag = def.c_iflag;
	/* preserve user-preference flags in lflag */
#define	LKEEP	(ECHOKE|ECHOE|ECHOK|ECHOPRT|ECHOCTL|ALTWERASE|TOSTOP|NOFLSH)
	ip->t.c_lflag = def.c_lflag | (ip->t.c_lflag & LKEEP);
	ip->t.c_oflag = def.c_oflag;
	ip->set = 1;
}

void
f_size(struct info *ip)
{

	(void)printf("%d %d\n", ip->win.ws_row, ip->win.ws_col);
}

void
f_speed(struct info *ip)
{

	(void)printf("%lu\n", (u_long)cfgetospeed(&ip->t));
}

void
f_tty(struct info *ip)
{
	int tmp;

	tmp = TTYDISC;
	if (ioctl(ip->fd, TIOCSETD, &tmp) < 0)
		err(1, "TIOCSETD");
}
