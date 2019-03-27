/* Parameters for hosting on an RS6000, for GDB, the GNU debugger.
   Copyright 1986, 1987, 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1998,
   2000, 2001 Free Software Foundation, Inc.
   Contributed by IBM Corporation.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* The following text is taken from config/rs6000.mh:
 * # The IBM version of /usr/include/rpc/rpc.h has a bug -- it says
 * # `extern fd_set svc_fdset;' without ever defining the type fd_set.
 * # Unfortunately this occurs in the vx-share code, which is not configured
 * # like the rest of GDB (e.g. it doesn't include "defs.h").
 * # We circumvent this bug by #define-ing fd_set here, but undefining it in
 * # the xm-rs6000.h file before ordinary modules try to use it.  FIXME, IBM!
 * MH_CFLAGS='-Dfd_set=int'
 * So, here we do the undefine...which has to occur before we include
 * <sys/select.h> below.
 */
#undef fd_set

#include <sys/select.h>

/* Big end is at the low address */

/* At least as of AIX 3.2, we have termios.  */
#define	HAVE_TERMIOS 1
/* #define HAVE_TERMIO 1 */

#define	USG 1

#define FIVE_ARG_PTRACE

/* This system requires that we open a terminal with O_NOCTTY for it to
   not become our controlling terminal.  */

#define	USE_O_NOCTTY

/* Brain death inherited from PC's pervades.  */
#undef NULL
#define NULL 0

/* The IBM compiler requires this in order to properly compile alloca().  */
#pragma alloca

/* There is no vfork.  */

#define	vfork	fork

/* Signal handler for SIGWINCH `window size changed'. */

#define	SIGWINCH_HANDLER  aix_resizewindow
extern void aix_resizewindow (int);

/* This doesn't seem to be declared in any header file I can find.  */
char *termdef (int, int);

/* `lines_per_page' and `chars_per_line' are local to utils.c. Rectify this. */

#define	SIGWINCH_HANDLER_BODY	\
									\
/* Respond to SIGWINCH `window size changed' signal, and reset GDB's	\
   window settings appropriately. */					\
									\
void 						\
aix_resizewindow (signo)			\
     int signo;					\
{						\
  int fd = fileno (stdout);			\
  if (isatty (fd)) {				\
    int val;					\
						\
    val = atoi (termdef (fd, 'l'));		\
    if (val > 0)				\
      lines_per_page = val;			\
    val = atoi (termdef (fd, 'c'));		\
    if (val > 0)				\
      chars_per_line = val;			\
  }						\
}
