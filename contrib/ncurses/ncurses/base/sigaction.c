/****************************************************************************
 * Copyright (c) 1998-2002,2003 Free Software Foundation, Inc.              *
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
 *     and: Thomas E. Dickey                        1996-2003               *
 ****************************************************************************/

/* This file provides sigaction() emulation using sigvec() */
/* Use only if this is non POSIX system */

MODULE_ID("$Id: sigaction.c,v 1.14 2003/12/07 01:06:52 tom Exp $")

static int
_nc_sigaction(int sig, sigaction_t * sigact, sigaction_t * osigact)
{
    return sigvec(sig, sigact, osigact);
}

static int
_nc_sigemptyset(sigset_t * mask)
{
    *mask = 0;
    return 0;
}

static int
_nc_sigprocmask(int mode, sigset_t * mask, sigset_t * omask)
{
    sigset_t current = sigsetmask(0);

    if (omask)
	*omask = current;

    if (mode == SIG_BLOCK)
	current |= *mask;
    else if (mode == SIG_UNBLOCK)
	current &= ~*mask;
    else if (mode == SIG_SETMASK)
	current = *mask;

    sigsetmask(current);
    return 0;
}

static int
_nc_sigaddset(sigset_t * mask, int sig)
{
    *mask |= sigmask(sig);
    return 0;
}

/* not used in lib_tstp.c */
#if 0
static int
_nc_sigsuspend(sigset_t * mask)
{
    return sigpause(*mask);
}

static int
_nc_sigdelset(sigset_t * mask, int sig)
{
    *mask &= ~sigmask(sig);
    return 0;
}

static int
_nc_sigismember(sigset_t * mask, int sig)
{
    return (*mask & sigmask(sig)) != 0;
}
#endif
