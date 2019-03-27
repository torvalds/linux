/****************************************************************************
 * Copyright (c) 1998-2012,2013 Free Software Foundation, Inc.              *
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
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

/*
**	lib_twait.c
**
**	The routine _nc_timed_wait().
**
**	(This file was originally written by Eric Raymond; however except for
**	comments, none of the original code remains - T.Dickey).
*/

#include <curses.priv.h>

#if defined __HAIKU__ && defined __BEOS__
#undef __BEOS__
#endif

#ifdef __BEOS__
#undef false
#undef true
#include <OS.h>
#endif

#if USE_KLIBC_KBD
#define INCL_KBD
#include <os2.h>
#endif

#if USE_FUNC_POLL
# if HAVE_SYS_TIME_H
#  include <sys/time.h>
# endif
#elif HAVE_SELECT
# if HAVE_SYS_TIME_H && HAVE_SYS_TIME_SELECT
#  include <sys/time.h>
# endif
# if HAVE_SYS_SELECT_H
#  include <sys/select.h>
# endif
#endif
#ifdef __MINGW32__
#  include <sys/time.h>
#endif
#undef CUR

MODULE_ID("$Id: lib_twait.c,v 1.67 2013/02/18 09:22:27 tom Exp $")

static long
_nc_gettime(TimeType * t0, int first)
{
    long res;

#if PRECISE_GETTIME
    TimeType t1;
    gettimeofday(&t1, (struct timezone *) 0);
    if (first) {
	*t0 = t1;
	res = 0;
    } else {
	/* .tv_sec and .tv_usec are unsigned, be careful when subtracting */
	if (t0->tv_usec > t1.tv_usec) {
	    t1.tv_usec += 1000000;	/* Convert 1s in 1e6 microsecs */
	    t1.tv_sec--;
	}
	res = (t1.tv_sec - t0->tv_sec) * 1000
	    + (t1.tv_usec - t0->tv_usec) / 1000;
    }
#else
    time_t t1 = time((time_t *) 0);
    if (first) {
	*t0 = t1;
    }
    res = (t1 - *t0) * 1000;
#endif
    TR(TRACE_IEVENT, ("%s time: %ld msec", first ? "get" : "elapsed", res));
    return res;
}

#ifdef NCURSES_WGETCH_EVENTS
NCURSES_EXPORT(int)
_nc_eventlist_timeout(_nc_eventlist * evl)
{
    int event_delay = -1;
    int n;

    if (evl != 0) {

	for (n = 0; n < evl->count; ++n) {
	    _nc_event *ev = evl->events[n];

	    if (ev->type == _NC_EVENT_TIMEOUT_MSEC) {
		event_delay = ev->data.timeout_msec;
		if (event_delay < 0)
		    event_delay = INT_MAX;	/* FIXME Is this defined? */
	    }
	}
    }
    return event_delay;
}
#endif /* NCURSES_WGETCH_EVENTS */

#if (USE_FUNC_POLL || HAVE_SELECT)
#  define MAYBE_UNUSED
#else
#  define MAYBE_UNUSED GCC_UNUSED
#endif

#if (USE_FUNC_POLL || HAVE_SELECT)
#  define MAYBE_UNUSED
#else
#  define MAYBE_UNUSED GCC_UNUSED
#endif

/*
 * Wait a specified number of milliseconds, returning nonzero if the timer
 * didn't expire before there is activity on the specified file descriptors.
 * The file-descriptors are specified by the mode:
 *	TW_NONE    0 - none (absolute time)
 *	TW_INPUT   1 - ncurses' normal input-descriptor
 *	TW_MOUSE   2 - mouse descriptor, if any
 *	TW_ANY     3 - either input or mouse.
 *      TW_EVENT   4 -
 * Experimental:  if NCURSES_WGETCH_EVENTS is defined, (mode & 4) determines
 * whether to pay attention to evl argument.  If set, the smallest of
 * millisecond and of timeout of evl is taken.
 *
 * We return a mask that corresponds to the mode (e.g., 2 for mouse activity).
 *
 * If the milliseconds given are -1, the wait blocks until activity on the
 * descriptors.
 */
NCURSES_EXPORT(int)
_nc_timed_wait(SCREEN *sp MAYBE_UNUSED,
	       int mode MAYBE_UNUSED,
	       int milliseconds,
	       int *timeleft
	       EVENTLIST_2nd(_nc_eventlist * evl))
{
    int count;
    int result = TW_NONE;
    TimeType t0;
#if (USE_FUNC_POLL || HAVE_SELECT)
    int fd;
#endif

#ifdef NCURSES_WGETCH_EVENTS
    int timeout_is_event = 0;
    int n;
#endif

#if USE_FUNC_POLL
#define MIN_FDS 2
    struct pollfd fd_list[MIN_FDS];
    struct pollfd *fds = fd_list;
#elif defined(__BEOS__)
#elif HAVE_SELECT
    fd_set set;
#endif

#if USE_KLIBC_KBD
    fd_set saved_set;
    KBDKEYINFO ki;
    struct timeval tv;
#endif

    long starttime, returntime;

    TR(TRACE_IEVENT, ("start twait: %d milliseconds, mode: %d",
		      milliseconds, mode));

#ifdef NCURSES_WGETCH_EVENTS
    if (mode & TW_EVENT) {
	int event_delay = _nc_eventlist_timeout(evl);

	if (event_delay >= 0
	    && (milliseconds >= event_delay || milliseconds < 0)) {
	    milliseconds = event_delay;
	    timeout_is_event = 1;
	}
    }
#endif

#if PRECISE_GETTIME && HAVE_NANOSLEEP
  retry:
#endif
    starttime = _nc_gettime(&t0, TRUE);

    count = 0;
    (void) count;

#ifdef NCURSES_WGETCH_EVENTS
    if ((mode & TW_EVENT) && evl)
	evl->result_flags = 0;
#endif

#if USE_FUNC_POLL
    memset(fd_list, 0, sizeof(fd_list));

#ifdef NCURSES_WGETCH_EVENTS
    if ((mode & TW_EVENT) && evl) {
	fds = typeMalloc(struct pollfd, MIN_FDS + evl->count);
	if (fds == 0)
	    return TW_NONE;
    }
#endif

    if (mode & TW_INPUT) {
	fds[count].fd = sp->_ifd;
	fds[count].events = POLLIN;
	count++;
    }
    if ((mode & TW_MOUSE)
	&& (fd = sp->_mouse_fd) >= 0) {
	fds[count].fd = fd;
	fds[count].events = POLLIN;
	count++;
    }
#ifdef NCURSES_WGETCH_EVENTS
    if ((mode & TW_EVENT) && evl) {
	for (n = 0; n < evl->count; ++n) {
	    _nc_event *ev = evl->events[n];

	    if (ev->type == _NC_EVENT_FILE
		&& (ev->data.fev.flags & _NC_EVENT_FILE_READABLE)) {
		fds[count].fd = ev->data.fev.fd;
		fds[count].events = POLLIN;
		count++;
	    }
	}
    }
#endif

    result = poll(fds, (size_t) count, milliseconds);

#ifdef NCURSES_WGETCH_EVENTS
    if ((mode & TW_EVENT) && evl) {
	int c;

	if (!result)
	    count = 0;

	for (n = 0; n < evl->count; ++n) {
	    _nc_event *ev = evl->events[n];

	    if (ev->type == _NC_EVENT_FILE
		&& (ev->data.fev.flags & _NC_EVENT_FILE_READABLE)) {
		ev->data.fev.result = 0;
		for (c = 0; c < count; c++)
		    if (fds[c].fd == ev->data.fev.fd
			&& fds[c].revents & POLLIN) {
			ev->data.fev.result |= _NC_EVENT_FILE_READABLE;
			evl->result_flags |= _NC_EVENT_FILE_READABLE;
		    }
	    } else if (ev->type == _NC_EVENT_TIMEOUT_MSEC
		       && !result && timeout_is_event) {
		evl->result_flags |= _NC_EVENT_TIMEOUT_MSEC;
	    }
	}
    }
#endif

#elif defined(__BEOS__)
    /*
     * BeOS's select() is declared in socket.h, so the configure script does
     * not see it.  That's just as well, since that function works only for
     * sockets.  This (using snooze and ioctl) was distilled from Be's patch
     * for ncurses which uses a separate thread to simulate select().
     *
     * FIXME: the return values from the ioctl aren't very clear if we get
     * interrupted.
     *
     * FIXME: this assumes mode&1 if milliseconds < 0 (see lib_getch.c).
     */
    result = TW_NONE;
    if (mode & TW_INPUT) {
	int step = (milliseconds < 0) ? 0 : 5000;
	bigtime_t d;
	bigtime_t useconds = milliseconds * 1000;
	int n, howmany;

	if (useconds <= 0)	/* we're here to go _through_ the loop */
	    useconds = 1;

	for (d = 0; d < useconds; d += step) {
	    n = 0;
	    howmany = ioctl(0, 'ichr', &n);
	    if (howmany >= 0 && n > 0) {
		result = 1;
		break;
	    }
	    if (useconds > 1 && step > 0) {
		snooze(step);
		milliseconds -= (step / 1000);
		if (milliseconds <= 0) {
		    milliseconds = 0;
		    break;
		}
	    }
	}
    } else if (milliseconds > 0) {
	snooze(milliseconds * 1000);
	milliseconds = 0;
    }
#elif HAVE_SELECT
    /*
     * select() modifies the fd_set arguments; do this in the
     * loop.
     */
    FD_ZERO(&set);

#if !USE_KLIBC_KBD
    if (mode & TW_INPUT) {
	FD_SET(sp->_ifd, &set);
	count = sp->_ifd + 1;
    }
#endif
    if ((mode & TW_MOUSE)
	&& (fd = sp->_mouse_fd) >= 0) {
	FD_SET(fd, &set);
	count = max(fd, count) + 1;
    }
#ifdef NCURSES_WGETCH_EVENTS
    if ((mode & TW_EVENT) && evl) {
	for (n = 0; n < evl->count; ++n) {
	    _nc_event *ev = evl->events[n];

	    if (ev->type == _NC_EVENT_FILE
		&& (ev->data.fev.flags & _NC_EVENT_FILE_READABLE)) {
		FD_SET(ev->data.fev.fd, &set);
		count = max(ev->data.fev.fd + 1, count);
	    }
	}
    }
#endif

#if USE_KLIBC_KBD
    for (saved_set = set;; set = saved_set) {
	if ((mode & TW_INPUT)
	    && (sp->_extended_key
		|| (KbdPeek(&ki, 0) == 0
		    && (ki.fbStatus & KBDTRF_FINAL_CHAR_IN)))) {
	    FD_ZERO(&set);
	    FD_SET(sp->_ifd, &set);
	    result = 1;
	    break;
	}

	tv.tv_sec = 0;
	tv.tv_usec = (milliseconds == 0) ? 0 : (10 * 1000);

	if ((result = select(count, &set, NULL, NULL, &tv)) != 0)
	    break;

	/* Time out ? */
	if (milliseconds >= 0 && _nc_gettime(&t0, FALSE) >= milliseconds) {
	    result = 0;
	    break;
	}
    }
#else
    if (milliseconds >= 0) {
	struct timeval ntimeout;
	ntimeout.tv_sec = milliseconds / 1000;
	ntimeout.tv_usec = (milliseconds % 1000) * 1000;
	result = select(count, &set, NULL, NULL, &ntimeout);
    } else {
	result = select(count, &set, NULL, NULL, NULL);
    }
#endif

#ifdef NCURSES_WGETCH_EVENTS
    if ((mode & TW_EVENT) && evl) {
	evl->result_flags = 0;
	for (n = 0; n < evl->count; ++n) {
	    _nc_event *ev = evl->events[n];

	    if (ev->type == _NC_EVENT_FILE
		&& (ev->data.fev.flags & _NC_EVENT_FILE_READABLE)) {
		ev->data.fev.result = 0;
		if (FD_ISSET(ev->data.fev.fd, &set)) {
		    ev->data.fev.result |= _NC_EVENT_FILE_READABLE;
		    evl->result_flags |= _NC_EVENT_FILE_READABLE;
		}
	    } else if (ev->type == _NC_EVENT_TIMEOUT_MSEC
		       && !result && timeout_is_event)
		evl->result_flags |= _NC_EVENT_TIMEOUT_MSEC;
	}
    }
#endif

#endif /* USE_FUNC_POLL, etc */

    returntime = _nc_gettime(&t0, FALSE);

    if (milliseconds >= 0)
	milliseconds -= (int) (returntime - starttime);

#ifdef NCURSES_WGETCH_EVENTS
    if (evl) {
	evl->result_flags = 0;
	for (n = 0; n < evl->count; ++n) {
	    _nc_event *ev = evl->events[n];

	    if (ev->type == _NC_EVENT_TIMEOUT_MSEC) {
		long diff = (returntime - starttime);
		if (ev->data.timeout_msec <= diff)
		    ev->data.timeout_msec = 0;
		else
		    ev->data.timeout_msec -= diff;
	    }

	}
    }
#endif

#if PRECISE_GETTIME && HAVE_NANOSLEEP
    /*
     * If the timeout hasn't expired, and we've gotten no data,
     * this is probably a system where 'select()' needs to be left
     * alone so that it can complete.  Make this process sleep,
     * then come back for more.
     */
    if (result == 0 && milliseconds > 100) {
	napms(100);		/* FIXME: this won't be right if I recur! */
	milliseconds -= 100;
	goto retry;
    }
#endif

    /* return approximate time left in milliseconds */
    if (timeleft)
	*timeleft = milliseconds;

    TR(TRACE_IEVENT, ("end twait: returned %d (%d), remaining time %d msec",
		      result, errno, milliseconds));

    /*
     * Both 'poll()' and 'select()' return the number of file descriptors
     * that are active.  Translate this back to the mask that denotes which
     * file-descriptors, so that we don't need all of this system-specific
     * code everywhere.
     */
    if (result != 0) {
	if (result > 0) {
	    result = 0;
#if USE_FUNC_POLL
	    for (count = 0; count < MIN_FDS; count++) {
		if ((mode & (1 << count))
		    && (fds[count].revents & POLLIN)) {
		    result |= (1 << count);
		}
	    }
#elif defined(__BEOS__)
	    result = TW_INPUT;	/* redundant, but simple */
#elif HAVE_SELECT
	    if ((mode & TW_MOUSE)
		&& (fd = sp->_mouse_fd) >= 0
		&& FD_ISSET(fd, &set))
		result |= TW_MOUSE;
	    if ((mode & TW_INPUT)
		&& FD_ISSET(sp->_ifd, &set))
		result |= TW_INPUT;
#endif
	} else
	    result = 0;
    }
#ifdef NCURSES_WGETCH_EVENTS
    if ((mode & TW_EVENT) && evl && evl->result_flags)
	result |= TW_EVENT;
#endif

#if USE_FUNC_POLL
#ifdef NCURSES_WGETCH_EVENTS
    if (fds != fd_list)
	free((char *) fds);
#endif
#endif

    return (result);
}
