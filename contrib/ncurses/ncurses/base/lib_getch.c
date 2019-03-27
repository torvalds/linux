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
 *     and: Juergen Pfeifer                         2009                    *
 ****************************************************************************/

/*
**	lib_getch.c
**
**	The routine getch().
**
*/

#include <curses.priv.h>

MODULE_ID("$Id: lib_getch.c,v 1.126 2013/02/16 18:30:37 tom Exp $")

#include <fifo_defs.h>

#if USE_REENTRANT
#define GetEscdelay(sp) *_nc_ptr_Escdelay(sp)
NCURSES_EXPORT(int)
NCURSES_PUBLIC_VAR(ESCDELAY) (void)
{
    return *(_nc_ptr_Escdelay(CURRENT_SCREEN));
}

NCURSES_EXPORT(int *)
_nc_ptr_Escdelay(SCREEN *sp)
{
    return ptrEscdelay(sp);
}
#else
#define GetEscdelay(sp) ESCDELAY
NCURSES_EXPORT_VAR(int) ESCDELAY = 1000;
#endif

#if NCURSES_EXT_FUNCS
NCURSES_EXPORT(int)
NCURSES_SP_NAME(set_escdelay) (NCURSES_SP_DCLx int value)
{
    int code = OK;
#if USE_REENTRANT
    if (SP_PARM) {
	SET_ESCDELAY(value);
    } else {
	code = ERR;
    }
#else
    (void) SP_PARM;
    ESCDELAY = value;
#endif
    return code;
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
set_escdelay(int value)
{
    int code;
#if USE_REENTRANT
    code = NCURSES_SP_NAME(set_escdelay) (CURRENT_SCREEN, value);
#else
    ESCDELAY = value;
    code = OK;
#endif
    return code;
}
#endif
#endif /* NCURSES_EXT_FUNCS */

#if NCURSES_EXT_FUNCS
NCURSES_EXPORT(int)
NCURSES_SP_NAME(get_escdelay) (NCURSES_SP_DCL0)
{
#if !USE_REENTRANT
    (void) SP_PARM;
#endif
    return GetEscdelay(SP_PARM);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
get_escdelay(void)
{
    return NCURSES_SP_NAME(get_escdelay) (CURRENT_SCREEN);
}
#endif
#endif /* NCURSES_EXT_FUNCS */

static int
_nc_use_meta(WINDOW *win)
{
    SCREEN *sp = _nc_screen_of(win);
    return (sp ? sp->_use_meta : 0);
}

/*
 * Check for mouse activity, returning nonzero if we find any.
 */
static int
check_mouse_activity(SCREEN *sp, int delay EVENTLIST_2nd(_nc_eventlist * evl))
{
    int rc;

#ifdef USE_TERM_DRIVER
    rc = TCBOf(sp)->drv->testmouse(TCBOf(sp), delay EVENTLIST_2nd(evl));
#else
#if USE_SYSMOUSE
    if ((sp->_mouse_type == M_SYSMOUSE)
	&& (sp->_sysmouse_head < sp->_sysmouse_tail)) {
	rc = TW_MOUSE;
    } else
#endif
    {
	rc = _nc_timed_wait(sp,
			    TWAIT_MASK,
			    delay,
			    (int *) 0
			    EVENTLIST_2nd(evl));
#if USE_SYSMOUSE
	if ((sp->_mouse_type == M_SYSMOUSE)
	    && (sp->_sysmouse_head < sp->_sysmouse_tail)
	    && (rc == 0)
	    && (errno == EINTR)) {
	    rc |= TW_MOUSE;
	}
#endif
    }
#endif
    return rc;
}

static NCURSES_INLINE int
fifo_peek(SCREEN *sp)
{
    int ch = (peek >= 0) ? sp->_fifo[peek] : ERR;
    TR(TRACE_IEVENT, ("peeking at %d", peek));

    p_inc();
    return ch;
}

static NCURSES_INLINE int
fifo_pull(SCREEN *sp)
{
    int ch = (head >= 0) ? sp->_fifo[head] : ERR;

    TR(TRACE_IEVENT, ("pulling %s from %d", _nc_tracechar(sp, ch), head));

    if (peek == head) {
	h_inc();
	peek = head;
    } else {
	h_inc();
    }

#ifdef TRACE
    if (USE_TRACEF(TRACE_IEVENT)) {
	_nc_fifo_dump(sp);
	_nc_unlock_global(tracef);
    }
#endif
    return ch;
}

static NCURSES_INLINE int
fifo_push(SCREEN *sp EVENTLIST_2nd(_nc_eventlist * evl))
{
    int n;
    int ch = 0;
    int mask = 0;

    (void) mask;
    if (tail < 0)
	return ERR;

#ifdef HIDE_EINTR
  again:
    errno = 0;
#endif

#ifdef NCURSES_WGETCH_EVENTS
    if (evl
#if USE_GPM_SUPPORT || USE_EMX_MOUSE || USE_SYSMOUSE
	|| (sp->_mouse_fd >= 0)
#endif
	) {
	mask = check_mouse_activity(sp, -1 EVENTLIST_2nd(evl));
    } else
	mask = 0;

    if (mask & TW_EVENT) {
	T(("fifo_push: ungetch KEY_EVENT"));
	safe_ungetch(sp, KEY_EVENT);
	return KEY_EVENT;
    }
#elif USE_GPM_SUPPORT || USE_EMX_MOUSE || USE_SYSMOUSE
    if (sp->_mouse_fd >= 0) {
	mask = check_mouse_activity(sp, -1 EVENTLIST_2nd(evl));
    }
#endif

#if USE_GPM_SUPPORT || USE_EMX_MOUSE
    if ((sp->_mouse_fd >= 0) && (mask & TW_MOUSE)) {
	sp->_mouse_event(sp);
	ch = KEY_MOUSE;
	n = 1;
    } else
#endif
#if USE_SYSMOUSE
	if ((sp->_mouse_type == M_SYSMOUSE)
	    && (sp->_sysmouse_head < sp->_sysmouse_tail)) {
	sp->_mouse_event(sp);
	ch = KEY_MOUSE;
	n = 1;
    } else if ((sp->_mouse_type == M_SYSMOUSE)
	       && (mask <= 0) && errno == EINTR) {
	sp->_mouse_event(sp);
	ch = KEY_MOUSE;
	n = 1;
    } else
#endif
#ifdef USE_TERM_DRIVER
	if ((sp->_mouse_type == M_TERM_DRIVER)
	    && (sp->_drv_mouse_head < sp->_drv_mouse_tail)) {
	sp->_mouse_event(sp);
	ch = KEY_MOUSE;
	n = 1;
    } else
#endif
#if USE_KLIBC_KBD
    if (isatty(sp->_ifd) && sp->_cbreak) {
	ch = _read_kbd(0, 1, !sp->_raw);
	n = (ch == -1) ? -1 : 1;
	sp->_extended_key = (ch == 0);
    } else
#endif
    {				/* Can block... */
#ifdef USE_TERM_DRIVER
	int buf;
	n = CallDriver_1(sp, read, &buf);
	ch = buf;
#else
	unsigned char c2 = 0;
# if USE_PTHREADS_EINTR
#  if USE_WEAK_SYMBOLS
	if ((pthread_self) && (pthread_kill) && (pthread_equal))
#  endif
	    _nc_globals.read_thread = pthread_self();
# endif
	n = (int) read(sp->_ifd, &c2, (size_t) 1);
#if USE_PTHREADS_EINTR
	_nc_globals.read_thread = 0;
#endif
	ch = c2;
#endif
    }

#ifdef HIDE_EINTR
    /*
     * Under System V curses with non-restarting signals, getch() returns
     * with value ERR when a handled signal keeps it from completing.
     * If signals restart system calls, OTOH, the signal is invisible
     * except to its handler.
     *
     * We don't want this difference to show.  This piece of code
     * tries to make it look like we always have restarting signals.
     */
    if (n <= 0 && errno == EINTR
# if USE_PTHREADS_EINTR
	&& (_nc_globals.have_sigwinch == 0)
# endif
	)
	goto again;
#endif

    if ((n == -1) || (n == 0)) {
	TR(TRACE_IEVENT, ("read(%d,&ch,1)=%d, errno=%d", sp->_ifd, n, errno));
	ch = ERR;
    }
    TR(TRACE_IEVENT, ("read %d characters", n));

    sp->_fifo[tail] = ch;
    sp->_fifohold = 0;
    if (head == -1)
	head = peek = tail;
    t_inc();
    TR(TRACE_IEVENT, ("pushed %s at %d", _nc_tracechar(sp, ch), tail));
#ifdef TRACE
    if (USE_TRACEF(TRACE_IEVENT)) {
	_nc_fifo_dump(sp);
	_nc_unlock_global(tracef);
    }
#endif
    return ch;
}

static NCURSES_INLINE void
fifo_clear(SCREEN *sp)
{
    memset(sp->_fifo, 0, sizeof(sp->_fifo));
    head = -1;
    tail = peek = 0;
}

static int kgetch(SCREEN *EVENTLIST_2nd(_nc_eventlist * evl));

static void
recur_wrefresh(WINDOW *win)
{
#ifdef USE_PTHREADS
    SCREEN *sp = _nc_screen_of(win);
    if (_nc_use_pthreads && sp != CURRENT_SCREEN) {
	SCREEN *save_SP;

	/* temporarily switch to the window's screen to check/refresh */
	_nc_lock_global(curses);
	save_SP = CURRENT_SCREEN;
	_nc_set_screen(sp);
	recur_wrefresh(win);
	_nc_set_screen(save_SP);
	_nc_unlock_global(curses);
    } else
#endif
	if ((is_wintouched(win) || (win->_flags & _HASMOVED))
	    && !(win->_flags & _ISPAD)) {
	wrefresh(win);
    }
}

static int
recur_wgetnstr(WINDOW *win, char *buf)
{
    SCREEN *sp = _nc_screen_of(win);
    int rc;

    if (sp != 0) {
#ifdef USE_PTHREADS
	if (_nc_use_pthreads && sp != CURRENT_SCREEN) {
	    SCREEN *save_SP;

	    /* temporarily switch to the window's screen to get cooked input */
	    _nc_lock_global(curses);
	    save_SP = CURRENT_SCREEN;
	    _nc_set_screen(sp);
	    rc = recur_wgetnstr(win, buf);
	    _nc_set_screen(save_SP);
	    _nc_unlock_global(curses);
	} else
#endif
	{
	    sp->_called_wgetch = TRUE;
	    rc = wgetnstr(win, buf, MAXCOLUMNS);
	    sp->_called_wgetch = FALSE;
	}
    } else {
	rc = ERR;
    }
    return rc;
}

NCURSES_EXPORT(int)
_nc_wgetch(WINDOW *win,
	   int *result,
	   int use_meta
	   EVENTLIST_2nd(_nc_eventlist * evl))
{
    SCREEN *sp;
    int ch;
    int rc = 0;
#ifdef NCURSES_WGETCH_EVENTS
    long event_delay = -1;
#endif

    T((T_CALLED("_nc_wgetch(%p)"), (void *) win));

    *result = 0;

    sp = _nc_screen_of(win);
    if (win == 0 || sp == 0) {
	returnCode(ERR);
    }

    if (cooked_key_in_fifo()) {
	recur_wrefresh(win);
	*result = fifo_pull(sp);
	returnCode(*result >= KEY_MIN ? KEY_CODE_YES : OK);
    }
#ifdef NCURSES_WGETCH_EVENTS
    if (evl && (evl->count == 0))
	evl = NULL;
    event_delay = _nc_eventlist_timeout(evl);
#endif

    /*
     * Handle cooked mode.  Grab a string from the screen,
     * stuff its contents in the FIFO queue, and pop off
     * the first character to return it.
     */
    if (head == -1 &&
	!sp->_notty &&
	!sp->_raw &&
	!sp->_cbreak &&
	!sp->_called_wgetch) {
	char buf[MAXCOLUMNS], *bufp;

	TR(TRACE_IEVENT, ("filling queue in cooked mode"));

	/* ungetch in reverse order */
#ifdef NCURSES_WGETCH_EVENTS
	rc = recur_wgetnstr(win, buf);
	if (rc != KEY_EVENT && rc != ERR)
	    safe_ungetch(sp, '\n');
#else
	if (recur_wgetnstr(win, buf) != ERR)
	    safe_ungetch(sp, '\n');
#endif
	for (bufp = buf + strlen(buf); bufp > buf; bufp--)
	    safe_ungetch(sp, bufp[-1]);

#ifdef NCURSES_WGETCH_EVENTS
	/* Return it first */
	if (rc == KEY_EVENT) {
	    *result = rc;
	} else
#endif
	    *result = fifo_pull(sp);
	returnCode(*result >= KEY_MIN ? KEY_CODE_YES : OK);
    }

    if (win->_use_keypad != sp->_keypad_on)
	_nc_keypad(sp, win->_use_keypad);

    recur_wrefresh(win);

    if (win->_notimeout || (win->_delay >= 0) || (sp->_cbreak > 1)) {
	if (head == -1) {	/* fifo is empty */
	    int delay;

	    TR(TRACE_IEVENT, ("timed delay in wgetch()"));
	    if (sp->_cbreak > 1)
		delay = (sp->_cbreak - 1) * 100;
	    else
		delay = win->_delay;

#ifdef NCURSES_WGETCH_EVENTS
	    if (event_delay >= 0 && delay > event_delay)
		delay = event_delay;
#endif

	    TR(TRACE_IEVENT, ("delay is %d milliseconds", delay));

	    rc = check_mouse_activity(sp, delay EVENTLIST_2nd(evl));

#ifdef NCURSES_WGETCH_EVENTS
	    if (rc & TW_EVENT) {
		*result = KEY_EVENT;
		returnCode(KEY_CODE_YES);
	    }
#endif
	    if (!rc) {
		goto check_sigwinch;
	    }
	}
	/* else go on to read data available */
    }

    if (win->_use_keypad) {
	/*
	 * This is tricky.  We only want to get special-key
	 * events one at a time.  But we want to accumulate
	 * mouse events until either (a) the mouse logic tells
	 * us it's picked up a complete gesture, or (b)
	 * there's a detectable time lapse after one.
	 *
	 * Note: if the mouse code starts failing to compose
	 * press/release events into clicks, you should probably
	 * increase the wait with mouseinterval().
	 */
	int runcount = 0;

	do {
	    ch = kgetch(sp EVENTLIST_2nd(evl));
	    if (ch == KEY_MOUSE) {
		++runcount;
		if (sp->_mouse_inline(sp))
		    break;
	    }
	    if (sp->_maxclick < 0)
		break;
	} while
	    (ch == KEY_MOUSE
	     && (((rc = check_mouse_activity(sp, sp->_maxclick
					     EVENTLIST_2nd(evl))) != 0
		  && !(rc & TW_EVENT))
		 || !sp->_mouse_parse(sp, runcount)));
#ifdef NCURSES_WGETCH_EVENTS
	if ((rc & TW_EVENT) && !(ch == KEY_EVENT)) {
	    safe_ungetch(sp, ch);
	    ch = KEY_EVENT;
	}
#endif
	if (runcount > 0 && ch != KEY_MOUSE) {
#ifdef NCURSES_WGETCH_EVENTS
	    /* mouse event sequence ended by an event, report event */
	    if (ch == KEY_EVENT) {
		safe_ungetch(sp, KEY_MOUSE);	/* FIXME This interrupts a gesture... */
	    } else
#endif
	    {
		/* mouse event sequence ended by keystroke, store keystroke */
		safe_ungetch(sp, ch);
		ch = KEY_MOUSE;
	    }
	}
    } else {
	if (head == -1)
	    fifo_push(sp EVENTLIST_2nd(evl));
	ch = fifo_pull(sp);
    }

    if (ch == ERR) {
      check_sigwinch:
#if USE_SIZECHANGE
	if (_nc_handle_sigwinch(sp)) {
	    _nc_update_screensize(sp);
	    /* resizeterm can push KEY_RESIZE */
	    if (cooked_key_in_fifo()) {
		*result = fifo_pull(sp);
		/*
		 * Get the ERR from queue -- it is from WINCH,
		 * so we should take it out, the "error" is handled.
		 */
		if (fifo_peek(sp) == -1)
		    fifo_pull(sp);
		returnCode(*result >= KEY_MIN ? KEY_CODE_YES : OK);
	    }
	}
#endif
	returnCode(ERR);
    }

    /*
     * If echo() is in effect, display the printable version of the
     * key on the screen.  Carriage return and backspace are treated
     * specially by Solaris curses:
     *
     * If carriage return is defined as a function key in the
     * terminfo, e.g., kent, then Solaris may return either ^J (or ^M
     * if nonl() is set) or KEY_ENTER depending on the echo() mode.
     * We echo before translating carriage return based on nonl(),
     * since the visual result simply moves the cursor to column 0.
     *
     * Backspace is a different matter.  Solaris curses does not
     * translate it to KEY_BACKSPACE if kbs=^H.  This does not depend
     * on the stty modes, but appears to be a hardcoded special case.
     * This is a difference from ncurses, which uses the terminfo entry.
     * However, we provide the same visual result as Solaris, moving the
     * cursor to the left.
     */
    if (sp->_echo && !(win->_flags & _ISPAD)) {
	chtype backup = (chtype) ((ch == KEY_BACKSPACE) ? '\b' : ch);
	if (backup < KEY_MIN)
	    wechochar(win, backup);
    }

    /*
     * Simulate ICRNL mode
     */
    if ((ch == '\r') && sp->_nl)
	ch = '\n';

    /* Strip 8th-bit if so desired.  We do this only for characters that
     * are in the range 128-255, to provide compatibility with terminals
     * that display only 7-bit characters.  Note that 'ch' may be a
     * function key at this point, so we mustn't strip _those_.
     */
    if (!use_meta)
	if ((ch < KEY_MIN) && (ch & 0x80))
	    ch &= 0x7f;

    T(("wgetch returning : %s", _nc_tracechar(sp, ch)));

    *result = ch;
    returnCode(ch >= KEY_MIN ? KEY_CODE_YES : OK);
}

#ifdef NCURSES_WGETCH_EVENTS
NCURSES_EXPORT(int)
wgetch_events(WINDOW *win, _nc_eventlist * evl)
{
    int code;
    int value;

    T((T_CALLED("wgetch_events(%p,%p)"), win, evl));
    code = _nc_wgetch(win,
		      &value,
		      _nc_use_meta(win)
		      EVENTLIST_2nd(evl));
    if (code != ERR)
	code = value;
    returnCode(code);
}
#endif

NCURSES_EXPORT(int)
wgetch(WINDOW *win)
{
    int code;
    int value;

    T((T_CALLED("wgetch(%p)"), (void *) win));
    code = _nc_wgetch(win,
		      &value,
		      _nc_use_meta(win)
		      EVENTLIST_2nd((_nc_eventlist *) 0));
    if (code != ERR)
	code = value;
    returnCode(code);
}

/*
**      int
**      kgetch()
**
**      Get an input character, but take care of keypad sequences, returning
**      an appropriate code when one matches the input.  After each character
**      is received, set an alarm call based on ESCDELAY.  If no more of the
**      sequence is received by the time the alarm goes off, pass through
**      the sequence gotten so far.
**
**	This function must be called when there are no cooked keys in queue.
**	(that is head==-1 || peek==head)
**
*/

static int
kgetch(SCREEN *sp EVENTLIST_2nd(_nc_eventlist * evl))
{
    TRIES *ptr;
    int ch = 0;
    int timeleft = GetEscdelay(sp);

    TR(TRACE_IEVENT, ("kgetch() called"));

    ptr = sp->_keytry;

    for (;;) {
	if (cooked_key_in_fifo() && sp->_fifo[head] >= KEY_MIN) {
	    break;
	} else if (!raw_key_in_fifo()) {
	    ch = fifo_push(sp EVENTLIST_2nd(evl));
	    if (ch == ERR) {
		peek = head;	/* the keys stay uninterpreted */
		return ERR;
	    }
#ifdef NCURSES_WGETCH_EVENTS
	    else if (ch == KEY_EVENT) {
		peek = head;	/* the keys stay uninterpreted */
		return fifo_pull(sp);	/* Remove KEY_EVENT from the queue */
	    }
#endif
	}

	ch = fifo_peek(sp);
	if (ch >= KEY_MIN) {
	    /* If not first in queue, somebody put this key there on purpose in
	     * emergency.  Consider it higher priority than the unfinished
	     * keysequence we are parsing.
	     */
	    peek = head;
	    /* assume the key is the last in fifo */
	    t_dec();		/* remove the key */
	    return ch;
	}

	TR(TRACE_IEVENT, ("ch: %s", _nc_tracechar(sp, (unsigned char) ch)));
	while ((ptr != NULL) && (ptr->ch != (unsigned char) ch))
	    ptr = ptr->sibling;

	if (ptr == NULL) {
	    TR(TRACE_IEVENT, ("ptr is null"));
	    break;
	}
	TR(TRACE_IEVENT, ("ptr=%p, ch=%d, value=%d",
			  (void *) ptr, ptr->ch, ptr->value));

	if (ptr->value != 0) {	/* sequence terminated */
	    TR(TRACE_IEVENT, ("end of sequence"));
	    if (peek == tail) {
		fifo_clear(sp);
	    } else {
		head = peek;
	    }
	    return (ptr->value);
	}

	ptr = ptr->child;

	if (!raw_key_in_fifo()) {
	    int rc;

	    TR(TRACE_IEVENT, ("waiting for rest of sequence"));
	    rc = check_mouse_activity(sp, timeleft EVENTLIST_2nd(evl));
#ifdef NCURSES_WGETCH_EVENTS
	    if (rc & TW_EVENT) {
		TR(TRACE_IEVENT, ("interrupted by a user event"));
		/* FIXME Should have preserved remainder timeleft for reuse... */
		peek = head;	/* Restart interpreting later */
		return KEY_EVENT;
	    }
#endif
	    if (!rc) {
		TR(TRACE_IEVENT, ("ran out of time"));
		break;
	    }
	}
    }
    ch = fifo_pull(sp);
    peek = head;
    return ch;
}
