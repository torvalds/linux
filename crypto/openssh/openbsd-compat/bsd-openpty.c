/*
 * Please note: this implementation of openpty() is far from complete.
 * it is just enough for portable OpenSSH's needs.
 */

/*
 * Copyright (c) 2004 Damien Miller <djm@mindrot.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 * Allocating a pseudo-terminal, and making it the controlling tty.
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 */

#include "includes.h"
#if !defined(HAVE_OPENPTY)

#include <sys/types.h>

#include <stdlib.h>

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#ifdef HAVE_UTIL_H
# include <util.h>
#endif /* HAVE_UTIL_H */

#ifdef HAVE_PTY_H
# include <pty.h>
#endif
#if defined(HAVE_DEV_PTMX) && defined(HAVE_SYS_STROPTS_H)
# include <sys/stropts.h>
#endif

#include <signal.h>
#include <string.h>
#include <unistd.h>

#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

int
openpty(int *amaster, int *aslave, char *name, struct termios *termp,
   struct winsize *winp)
{
#if defined(HAVE__GETPTY)
	/*
	 * _getpty(3) exists in SGI Irix 4.x, 5.x & 6.x -- it generates more
	 * pty's automagically when needed
	 */
	char *slave;

	if ((slave = _getpty(amaster, O_RDWR, 0622, 0)) == NULL)
		return (-1);

	/* Open the slave side. */
	if ((*aslave = open(slave, O_RDWR | O_NOCTTY)) == -1) {
		close(*amaster);
		return (-1);
	}
	return (0);

#elif defined(HAVE_DEV_PTMX)
	/*
	 * This code is used e.g. on Solaris 2.x.  (Note that Solaris 2.3
	 * also has bsd-style ptys, but they simply do not work.)
	 */
	int ptm;
	char *pts;
	mysig_t old_signal;

	if ((ptm = open("/dev/ptmx", O_RDWR | O_NOCTTY)) == -1)
		return (-1);

	/* XXX: need to close ptm on error? */
	old_signal = signal(SIGCHLD, SIG_DFL);
	if (grantpt(ptm) < 0)
		return (-1);
	signal(SIGCHLD, old_signal);

	if (unlockpt(ptm) < 0)
		return (-1);

	if ((pts = ptsname(ptm)) == NULL)
		return (-1);
	*amaster = ptm;

	/* Open the slave side. */
	if ((*aslave = open(pts, O_RDWR | O_NOCTTY)) == -1) {
		close(*amaster);
		return (-1);
	}

	/*
	 * Try to push the appropriate streams modules, as described
	 * in Solaris pts(7).
	 */
	ioctl(*aslave, I_PUSH, "ptem");
	ioctl(*aslave, I_PUSH, "ldterm");
# ifndef __hpux
	ioctl(*aslave, I_PUSH, "ttcompat");
# endif /* __hpux */

	return (0);

#elif defined(HAVE_DEV_PTS_AND_PTC)
	/* AIX-style pty code. */
	const char *ttname;

	if ((*amaster = open("/dev/ptc", O_RDWR | O_NOCTTY)) == -1)
		return (-1);
	if ((ttname = ttyname(*amaster)) == NULL)
		return (-1);
	if ((*aslave = open(ttname, O_RDWR | O_NOCTTY)) == -1) {
		close(*amaster);
		return (-1);
	}
	return (0);

#else
	/* BSD-style pty code. */
	char ptbuf[64], ttbuf[64];
	int i;
	const char *ptymajors = "pqrstuvwxyzabcdefghijklmno"
	    "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char *ptyminors = "0123456789abcdef";
	int num_minors = strlen(ptyminors);
	int num_ptys = strlen(ptymajors) * num_minors;
	struct termios tio;

	for (i = 0; i < num_ptys; i++) {
		snprintf(ptbuf, sizeof(ptbuf), "/dev/pty%c%c",
		    ptymajors[i / num_minors], ptyminors[i % num_minors]);
		snprintf(ttbuf, sizeof(ttbuf), "/dev/tty%c%c",
		    ptymajors[i / num_minors], ptyminors[i % num_minors]);

		if ((*amaster = open(ptbuf, O_RDWR | O_NOCTTY)) == -1) {
			/* Try SCO style naming */
			snprintf(ptbuf, sizeof(ptbuf), "/dev/ptyp%d", i);
			snprintf(ttbuf, sizeof(ttbuf), "/dev/ttyp%d", i);
			if ((*amaster = open(ptbuf, O_RDWR | O_NOCTTY)) == -1)
				continue;
		}

		/* Open the slave side. */
		if ((*aslave = open(ttbuf, O_RDWR | O_NOCTTY)) == -1) {
			close(*amaster);
			return (-1);
		}
		/* set tty modes to a sane state for broken clients */
		if (tcgetattr(*amaster, &tio) != -1) {
			tio.c_lflag |= (ECHO | ISIG | ICANON);
			tio.c_oflag |= (OPOST | ONLCR);
			tio.c_iflag |= ICRNL;
			tcsetattr(*amaster, TCSANOW, &tio);
		}

		return (0);
	}
	return (-1);
#endif
}

#endif /* !defined(HAVE_OPENPTY) */

