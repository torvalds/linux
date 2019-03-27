/*
 *  $Id: ttysize.c,v 1.1 2018/06/09 02:03:03 tom Exp $
 *
 *  ttysize.c -- obtain terminal-size for dialog
 *
 *  Copyright 2018	Thomas E. Dickey
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License, version 2.1
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to
 *	Free Software Foundation, Inc.
 *	51 Franklin St., Fifth Floor
 *	Boston, MA 02110, USA.
 *
 *  An earlier version of this program lists as authors
 *	Savio Lam (lam836@cs.cuhk.hk)
 */

#include <dialog.h>

/*
 * This is based on work I did for ncurses in 1997, and improved/extended for
 * other terminal-based programs.  The comments are from my original version -TD
 */

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif

#ifdef NEED_PTEM_H
 /* On SCO, they neglected to define struct winsize in termios.h -- it's only
  * in termio.h and ptem.h (the former conflicts with other definitions).
  */
# include <sys/stream.h>
# include <sys/ptem.h>
#endif

/*
 * SCO defines TIOCGSIZE and the corresponding struct.  Other systems (SunOS,
 * Solaris, IRIX) define TIOCGWINSZ and struct winsize.
 */
#if defined(TIOCGSIZE)
# define IOCTL_WINSIZE TIOCGSIZE
# define STRUCT_WINSIZE struct ttysize
# define WINSIZE_ROWS(n) (int)n.ts_lines
# define WINSIZE_COLS(n) (int)n.ts_cols
#elif defined(TIOCGWINSZ)
# define IOCTL_WINSIZE TIOCGWINSZ
# define STRUCT_WINSIZE struct winsize
# define WINSIZE_ROWS(n) (int)n.ws_row
# define WINSIZE_COLS(n) (int)n.ws_col
#else
# undef HAVE_SIZECHANGE
#endif

int
dlg_ttysize(int fd, int *high, int *wide)
{
    int rc = -1;
#ifdef HAVE_SIZECHANGE
    if (isatty(fd)) {
	STRUCT_WINSIZE size;

	if (ioctl(fd, IOCTL_WINSIZE, &size) >= 0) {
	    *high = WINSIZE_ROWS(size);
	    *wide = WINSIZE_COLS(size);
	    rc = 0;
	}
    }
#else
    high = 24;
    wide = 80;
#endif /* HAVE_SIZECHANGE */
    return rc;
}
