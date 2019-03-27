/****************************************************************************
 * Copyright (c) 1998,2000 Free Software Foundation, Inc.                   *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 ****************************************************************************/

/*
 * $Id: SigAction.h,v 1.8 2005/08/06 20:05:32 tom Exp $
 *
 * This file exists to handle non-POSIX systems which don't have <unistd.h>,
 * and usually no sigaction() nor <termios.h>
 */

#ifndef _SIGACTION_H
#define _SIGACTION_H

#ifndef HAVE_SIGACTION
#define HAVE_SIGACTION 0
#endif

#ifndef HAVE_SIGVEC
#define HAVE_SIGVEC 0
#endif

#if HAVE_SIGACTION

#if !HAVE_TYPE_SIGACTION
typedef struct sigaction sigaction_t;
#endif

#else	/* !HAVE_SIGACTION */

#if HAVE_SIGVEC

#undef  SIG_BLOCK
#define SIG_BLOCK       00

#undef  SIG_UNBLOCK
#define SIG_UNBLOCK     01

#undef  SIG_SETMASK
#define SIG_SETMASK     02

 	/*
	 * <bsd/signal.h> is in the Linux 1.2.8 + gcc 2.7.0 configuration,
	 * and is useful for testing this header file.
	 */
#if HAVE_BSD_SIGNAL_H
#include <bsd/signal.h>
#endif

typedef struct sigvec sigaction_t;

#define sigset_t _nc_sigset_t
typedef unsigned long sigset_t;

#undef  sa_mask
#define sa_mask sv_mask
#undef  sa_handler
#define sa_handler sv_handler
#undef  sa_flags
#define sa_flags sv_flags

#undef  sigaction
#define sigaction   _nc_sigaction
#undef  sigprocmask
#define sigprocmask _nc_sigprocmask
#undef  sigemptyset
#define sigemptyset _nc_sigemptyset
#undef  sigsuspend
#define sigsuspend  _nc_sigsuspend
#undef  sigdelset
#define sigdelset   _nc_sigdelset
#undef  sigaddset
#define sigaddset   _nc_sigaddset

/* tty/lib_tstp.c is the only user */
#include <base/sigaction.c>

#endif /* HAVE_SIGVEC */
#endif /* HAVE_SIGACTION */
#endif /* !defined(_SIGACTION_H) */
