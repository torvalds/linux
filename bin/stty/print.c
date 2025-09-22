/*	$OpenBSD: print.c,v 1.16 2017/04/28 22:16:43 millert Exp $	*/
/*	$NetBSD: print.c,v 1.11 1996/05/07 18:20:10 jtc Exp $	*/

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

#include <sys/types.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>

#include "stty.h"
#include "extern.h"

static void  binit(char *);
static void  bput(const char *, unsigned int);
static char *ccval(const struct cchar *, int);

void
print(struct termios *tp, struct winsize *wp, int ldisc, enum FMT fmt)
{
	const struct cchar *p;
	long tmp;
	u_char *cc;
	int cnt, ispeed, ospeed;
	char buf1[100], buf2[100];

	cnt = 0;

	/* Line discipline. */
	if (ldisc != TTYDISC) {
		switch(ldisc) {
		case PPPDISC:
			cnt += printf("ppp disc; ");
			break;
		case NMEADISC:
			cnt += printf("nmea disc; ");
			break;
		default:
			cnt += printf("#%d disc; ", ldisc);
			break;
		}
	}

	/* Line speed. */
	ispeed = cfgetispeed(tp);
	ospeed = cfgetospeed(tp);
	if (ispeed != ospeed)
		cnt +=
		    printf("ispeed %d baud; ospeed %d baud;", ispeed, ospeed);
	else
		cnt += printf("speed %d baud;", ispeed);
	if (fmt >= BSD)
		cnt += printf(" %d rows; %d columns;", wp->ws_row, wp->ws_col);
	if (cnt)
		(void)printf("\n");

#define	on(f)	((tmp&f) != 0)
#define put(n, f, d) \
	if (fmt >= BSD || on(f) != d) \
		bput(n, on(f));

	/* "local" flags */
	tmp = tp->c_lflag;
	binit("lflags");
	put("-icanon", ICANON, 1);
	put("-isig", ISIG, 1);
	put("-iexten", IEXTEN, 1);
	put("-echo", ECHO, 1);
	put("-echoe", ECHOE, 0);
	put("-echok", ECHOK, 0);
	put("-echoke", ECHOKE, 0);
	put("-echonl", ECHONL, 0);
	put("-echoctl", ECHOCTL, 0);
	put("-echoprt", ECHOPRT, 0);
	put("-altwerase", ALTWERASE, 0);
	put("-noflsh", NOFLSH, 0);
	put("-tostop", TOSTOP, 0);
	put("-flusho", FLUSHO, 0);
	put("-pendin", PENDIN, 0);
	put("-nokerninfo", NOKERNINFO, 0);
	put("-extproc", EXTPROC, 0);
	put("-xcase", XCASE, 0);

	/* input flags */
	tmp = tp->c_iflag;
	binit("iflags");
	put("-istrip", ISTRIP, 0);
	put("-icrnl", ICRNL, 1);
	put("-inlcr", INLCR, 0);
	put("-igncr", IGNCR, 0);
	put("-iuclc", IUCLC, 0);
	put("-ixon", IXON, 1);
	put("-ixoff", IXOFF, 0);
	put("-ixany", IXANY, 1);
	put("-imaxbel", IMAXBEL, 1);
	put("-ignbrk", IGNBRK, 0);
	put("-brkint", BRKINT, 1);
	put("-inpck", INPCK, 0);
	put("-ignpar", IGNPAR, 0);
	put("-parmrk", PARMRK, 0);

	/* output flags */
	tmp = tp->c_oflag;
	binit("oflags");
	put("-opost", OPOST, 1);
	put("-onlcr", ONLCR, 1);
	put("-ocrnl", OCRNL, 0);
	put("-onocr", ONOCR, 0);
	put("-onlret", ONLRET, 0);
	put("-olcuc", OLCUC, 0);
	put("-oxtabs", OXTABS, 1);
	put("-onoeot", ONOEOT, 0);

	/* control flags (hardware state) */
	tmp = tp->c_cflag;
	binit("cflags");
	put("-cread", CREAD, 1);
	switch(tmp&CSIZE) {
	case CS5:
		bput("cs5", 0);
		break;
	case CS6:
		bput("cs6", 0);
		break;
	case CS7:
		bput("cs7", 0);
		break;
	case CS8:
		bput("cs8", 0);
		break;
	}
	bput("-parenb", on(PARENB));
	put("-parodd", PARODD, 0);
	put("-hupcl", HUPCL, 1);
	put("-clocal", CLOCAL, 0);
	put("-cstopb", CSTOPB, 0);
	put("-crtscts", CRTSCTS, 0);
	put("-mdmbuf", MDMBUF, 0);

	/* special control characters */
	cc = tp->c_cc;
	if (fmt == POSIX) {
		binit("cchars");
		for (p = cchars1; p->name; ++p) {
			(void)snprintf(buf1, sizeof(buf1), "%s = %s;",
			    p->name, ccval(p, cc[p->sub]));
			bput(buf1, 0);
		}
		binit(NULL);
	} else {
		binit(NULL);
		for (p = cchars1, cnt = 0; p->name; ++p) {
			if (fmt != BSD && cc[p->sub] == p->def)
				continue;
#define	WD	"%-8s"
			(void)snprintf(buf1 + cnt * 8, sizeof(buf1) - cnt * 8,
			    WD, p->name);
			(void)snprintf(buf2 + cnt * 8, sizeof(buf2) - cnt * 8,
			    WD, ccval(p, cc[p->sub]));
			if (++cnt == LINELENGTH / 8) {
				cnt = 0;
				(void)printf("%s\n", buf1);
				(void)printf("%s\n", buf2);
			}
		}
		if (cnt) {
			(void)printf("%s\n", buf1);
			(void)printf("%s\n", buf2);
		}
	}
}

static int col;
static char *label;

static void
binit(char *lb)
{

	if (col) {
		(void)printf("\n");
		col = 0;
	}
	label = lb;
}

static void
bput(const char *s, unsigned int off)
{
	s += off;

	if (col == 0) {
		col = printf("%s: %s", label, s);
		return;
	}
	if ((col + strlen(s)) > LINELENGTH) {
		(void)printf("\n\t");
		col = printf("%s", s) + 8;
		return;
	}
	col += printf(" %s", s);
}

static char *
ccval(const struct cchar *p, int c)
{
	static char buf[5];
	char *bp;

	if (p->sub == VMIN || p->sub == VTIME) {
		(void)snprintf(buf, sizeof(buf), "%d", c);
		return (buf);
	}
	if (c == _POSIX_VDISABLE)
		return ("<undef>");
	bp = buf;
	if (c & 0200) {
		*bp++ = 'M';
		*bp++ = '-';
		c &= 0177;
	}
	if (c == 0177) {
		*bp++ = '^';
		*bp++ = '?';
	}
	else if (c < 040) {
		*bp++ = '^';
		*bp++ = c + '@';
	}
	else
		*bp++ = c;
	*bp = '\0';
	return (buf);
}
