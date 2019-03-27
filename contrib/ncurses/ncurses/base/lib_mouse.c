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
 *     and: Juergen Pfeifer                         2008                    *
 ****************************************************************************/

/*
 * This module is intended to encapsulate ncurses's interface to pointing
 * devices.
 *
 * The primary method used is xterm's internal mouse-tracking facility.
 * Additional methods depend on the platform:
 *	Alessandro Rubini's GPM server (Linux)
 *	sysmouse (FreeBSD)
 *	special-purpose mouse interface for OS/2 EMX.
 *
 * Notes for implementors of new mouse-interface methods:
 *
 * The code is logically split into a lower level that accepts event reports
 * in a device-dependent format and an upper level that parses mouse gestures
 * and filters events.  The mediating data structure is a circular queue of
 * MEVENT structures.
 *
 * Functionally, the lower level's job is to pick up primitive events and
 * put them on the circular queue.  This can happen in one of two ways:
 * either (a) _nc_mouse_event() detects a series of incoming mouse reports
 * and queues them, or (b) code in lib_getch.c detects the kmous prefix in
 * the keyboard input stream and calls _nc_mouse_inline to queue up a series
 * of adjacent mouse reports.
 *
 * In either case, _nc_mouse_parse() should be called after the series is
 * accepted to parse the digested mouse reports (low-level MEVENTs) into
 * a gesture (a high-level or composite MEVENT).
 *
 * Don't be too shy about adding new event types or modifiers, if you can find
 * room for them in the 32-bit mask.  The API is written so that users get
 * feedback on which theoretical event types they won't see when they call
 * mousemask. There's one bit per button (the RESERVED_EVENT bit) not being
 * used yet, and a couple of bits open at the high end.
 */

#ifdef __EMX__
#  include <io.h>
#  define  INCL_DOS
#  define  INCL_VIO
#  define  INCL_KBD
#  define  INCL_MOU
#  define  INCL_DOSPROCESS
#  include <os2.h>		/* Need to include before the others */
#endif

#include <curses.priv.h>

#ifndef CUR
#define CUR SP_TERMTYPE
#endif

MODULE_ID("$Id: lib_mouse.c,v 1.141 2013/09/21 19:09:19 tom Exp $")

#include <tic.h>

#if USE_GPM_SUPPORT
#include <linux/keyboard.h>	/* defines KG_* macros */

#ifdef HAVE_LIBDL
/* use dynamic loader to avoid linkage dependency */
#include <dlfcn.h>

#ifdef RTLD_NOW
#define my_RTLD RTLD_NOW
#else
#ifdef RTLD_LAZY
#define my_RTLD RTLD_LAZY
#else
make an error
#endif
#endif				/* RTLD_NOW */
#endif				/* HAVE_LIBDL */

#endif				/* USE_GPM_SUPPORT */

#if USE_SYSMOUSE
#undef buttons			/* symbol conflict in consio.h */
#undef mouse_info		/* symbol conflict in consio.h */
#include <osreldate.h>
#if defined(__DragonFly_version) || (defined(__FreeBSD__) && (__FreeBSD_version >= 400017))
#include <sys/consio.h>
#include <sys/fbio.h>
#else
#include <machine/console.h>
#endif
#endif				/* use_SYSMOUSE */

#if USE_KLIBC_MOUSE
#include <sys/socket.h>
#define pipe(handles) socketpair(AF_LOCAL, SOCK_STREAM, 0, handles)
#define DosWrite(hfile, pbuffer, cbwrite, pcbactual) \
		write(hfile, pbuffer, cbwrite)
#define DosExit(action, result )	/* do nothing */
#define DosCreateThread(ptid, pfn, param, flag, cbStack) \
		(*(ptid) = _beginthread(pfn, NULL, cbStack, \
					(void *)param), (*(ptid) == -1))
#endif

#define MY_TRACE TRACE_ICALLS|TRACE_IEVENT

#define	MASK_RELEASE(x)		(mmask_t) NCURSES_MOUSE_MASK(x, 001)
#define	MASK_PRESS(x)		(mmask_t) NCURSES_MOUSE_MASK(x, 002)
#define	MASK_CLICK(x)		(mmask_t) NCURSES_MOUSE_MASK(x, 004)
#define	MASK_DOUBLE_CLICK(x)	(mmask_t) NCURSES_MOUSE_MASK(x, 010)
#define	MASK_TRIPLE_CLICK(x)	(mmask_t) NCURSES_MOUSE_MASK(x, 020)
#define	MASK_RESERVED_EVENT(x)	(mmask_t) NCURSES_MOUSE_MASK(x, 040)

#if NCURSES_MOUSE_VERSION == 1
#define BUTTON_CLICKED        (BUTTON1_CLICKED        | BUTTON2_CLICKED        | BUTTON3_CLICKED        | BUTTON4_CLICKED)
#define BUTTON_PRESSED        (BUTTON1_PRESSED        | BUTTON2_PRESSED        | BUTTON3_PRESSED        | BUTTON4_PRESSED)
#define BUTTON_RELEASED       (BUTTON1_RELEASED       | BUTTON2_RELEASED       | BUTTON3_RELEASED       | BUTTON4_RELEASED)
#define BUTTON_DOUBLE_CLICKED (BUTTON1_DOUBLE_CLICKED | BUTTON2_DOUBLE_CLICKED | BUTTON3_DOUBLE_CLICKED | BUTTON4_DOUBLE_CLICKED)
#define BUTTON_TRIPLE_CLICKED (BUTTON1_TRIPLE_CLICKED | BUTTON2_TRIPLE_CLICKED | BUTTON3_TRIPLE_CLICKED | BUTTON4_TRIPLE_CLICKED)
#define MAX_BUTTONS  4
#else
#define BUTTON_CLICKED        (BUTTON1_CLICKED        | BUTTON2_CLICKED        | BUTTON3_CLICKED        | BUTTON4_CLICKED        | BUTTON5_CLICKED)
#define BUTTON_PRESSED        (BUTTON1_PRESSED        | BUTTON2_PRESSED        | BUTTON3_PRESSED        | BUTTON4_PRESSED        | BUTTON5_PRESSED)
#define BUTTON_RELEASED       (BUTTON1_RELEASED       | BUTTON2_RELEASED       | BUTTON3_RELEASED       | BUTTON4_RELEASED       | BUTTON5_RELEASED)
#define BUTTON_DOUBLE_CLICKED (BUTTON1_DOUBLE_CLICKED | BUTTON2_DOUBLE_CLICKED | BUTTON3_DOUBLE_CLICKED | BUTTON4_DOUBLE_CLICKED | BUTTON5_DOUBLE_CLICKED)
#define BUTTON_TRIPLE_CLICKED (BUTTON1_TRIPLE_CLICKED | BUTTON2_TRIPLE_CLICKED | BUTTON3_TRIPLE_CLICKED | BUTTON4_TRIPLE_CLICKED | BUTTON5_TRIPLE_CLICKED)
#define MAX_BUTTONS  5
#endif

#define INVALID_EVENT	-1
#define NORMAL_EVENT	0

#define ValidEvent(ep) ((ep)->id != INVALID_EVENT)
#define Invalidate(ep) (ep)->id = INVALID_EVENT

#if USE_GPM_SUPPORT

#ifndef LIBGPM_SONAME
#define LIBGPM_SONAME "libgpm.so"
#endif

#define GET_DLSYM(name) (my_##name = (TYPE_##name) dlsym(sp->_dlopen_gpm, #name))

#endif				/* USE_GPM_SUPPORT */

static bool _nc_mouse_parse(SCREEN *, int);
static void _nc_mouse_resume(SCREEN *);
static void _nc_mouse_wrap(SCREEN *);

/* maintain a circular list of mouse events */

#define FirstEV(sp)	((sp)->_mouse_events)
#define LastEV(sp)	((sp)->_mouse_events + EV_MAX - 1)

#undef  NEXT
#define NEXT(ep)	((ep >= LastEV(SP_PARM)) \
			 ? FirstEV(SP_PARM) \
			 : ep + 1)

#undef  PREV
#define PREV(ep)	((ep <= FirstEV(SP_PARM)) \
			 ? LastEV(SP_PARM) \
			 : ep - 1)

#define IndexEV(sp, ep)	(ep - FirstEV(sp))

#define RunParams(sp, eventp, runp) \
		(long) IndexEV(sp, runp), \
		(long) (IndexEV(sp, eventp) + (EV_MAX - 1)) % EV_MAX

#ifdef TRACE
static void
_trace_slot(SCREEN *sp, const char *tag)
{
    MEVENT *ep;

    _tracef("%s", tag);

    for (ep = FirstEV(sp); ep <= LastEV(sp); ep++)
	_tracef("mouse event queue slot %ld = %s",
		(long) IndexEV(sp, ep),
		_nc_tracemouse(sp, ep));
}
#endif

#if USE_EMX_MOUSE

#  define TOP_ROW          0
#  define LEFT_COL         0

#  define M_FD(sp) sp->_mouse_fd

static void
write_event(SCREEN *sp, int down, int button, int x, int y)
{
    char buf[6];
    unsigned long ignore;

    strcpy(buf, "\033[M");	/* should be the same as key_mouse */
    buf[3] = ' ' + (button - 1) + (down ? 0 : 0x40);
    buf[4] = ' ' + x - LEFT_COL + 1;
    buf[5] = ' ' + y - TOP_ROW + 1;
    DosWrite(sp->_emxmouse_wfd, buf, 6, &ignore);
}

static void
#if USE_KLIBC_MOUSE
mouse_server(void *param)
#else
mouse_server(unsigned long param)
#endif
{
    SCREEN *sp = (SCREEN *) param;
    unsigned short fWait = MOU_WAIT;
    /* NOPTRRECT mourt = { 0,0,24,79 }; */
    MOUEVENTINFO mouev;
    HMOU hmou;
    unsigned short mask = MOUSE_BN1_DOWN | MOUSE_BN2_DOWN | MOUSE_BN3_DOWN;
    int nbuttons = 3;
    int oldstate = 0;
    char err[80];
    unsigned long rc;

    /* open the handle for the mouse */
    if (MouOpen(NULL, &hmou) == 0) {
	rc = MouSetEventMask(&mask, hmou);
	if (rc) {		/* retry with 2 buttons */
	    mask = MOUSE_BN1_DOWN | MOUSE_BN2_DOWN;
	    rc = MouSetEventMask(&mask, hmou);
	    nbuttons = 2;
	}
	if (rc == 0 && MouDrawPtr(hmou) == 0) {
	    for (;;) {
		/* sit and wait on the event queue */
		rc = MouReadEventQue(&mouev, &fWait, hmou);
		if (rc) {
		    _nc_SPRINTF(err, _nc_SLIMIT(sizeof(err))
				"Error reading mouse queue, rc=%lu.\r\n", rc);
		    break;
		}
		if (!sp->_emxmouse_activated)
		    goto finish;

		/*
		 * OS/2 numbers a 3-button mouse inconsistently from other
		 * platforms:
		 *      1 = left
		 *      2 = right
		 *      3 = middle.
		 */
		if ((mouev.fs ^ oldstate) & MOUSE_BN1_DOWN)
		    write_event(sp, mouev.fs & MOUSE_BN1_DOWN,
				sp->_emxmouse_buttons[1], mouev.col, mouev.row);
		if ((mouev.fs ^ oldstate) & MOUSE_BN2_DOWN)
		    write_event(sp, mouev.fs & MOUSE_BN2_DOWN,
				sp->_emxmouse_buttons[3], mouev.col, mouev.row);
		if ((mouev.fs ^ oldstate) & MOUSE_BN3_DOWN)
		    write_event(sp, mouev.fs & MOUSE_BN3_DOWN,
				sp->_emxmouse_buttons[2], mouev.col, mouev.row);

	      finish:
		oldstate = mouev.fs;
	    }
	} else {
	    _nc_SPRINTF(err, _nc_SLIMIT(sizeof(err))
			"Error setting event mask, buttons=%d, rc=%lu.\r\n",
			nbuttons, rc);
	}

	DosWrite(2, err, strlen(err), &rc);
	MouClose(hmou);
    }
    DosExit(EXIT_THREAD, 0L);
}

#endif /* USE_EMX_MOUSE */

#if USE_SYSMOUSE
static void
sysmouse_server(SCREEN *sp)
{
    struct mouse_info the_mouse;
    MEVENT *work;

    the_mouse.operation = MOUSE_GETINFO;
    if (sp != 0
	&& sp->_mouse_fd >= 0
	&& sp->_sysmouse_tail < FIFO_SIZE
	&& ioctl(sp->_mouse_fd, CONS_MOUSECTL, &the_mouse) != -1) {

	if (sp->_sysmouse_head > sp->_sysmouse_tail) {
	    sp->_sysmouse_tail = 0;
	    sp->_sysmouse_head = 0;
	}
	work = &(sp->_sysmouse_fifo[sp->_sysmouse_tail]);
	memset(work, 0, sizeof(*work));
	work->id = NORMAL_EVENT;	/* there's only one mouse... */

	sp->_sysmouse_old_buttons = sp->_sysmouse_new_buttons;
	sp->_sysmouse_new_buttons = the_mouse.u.data.buttons & 0x7;

	if (sp->_sysmouse_new_buttons) {
	    if (sp->_sysmouse_new_buttons & 1)
		work->bstate |= BUTTON1_PRESSED;
	    if (sp->_sysmouse_new_buttons & 2)
		work->bstate |= BUTTON2_PRESSED;
	    if (sp->_sysmouse_new_buttons & 4)
		work->bstate |= BUTTON3_PRESSED;
	} else {
	    if (sp->_sysmouse_old_buttons & 1)
		work->bstate |= BUTTON1_RELEASED;
	    if (sp->_sysmouse_old_buttons & 2)
		work->bstate |= BUTTON2_RELEASED;
	    if (sp->_sysmouse_old_buttons & 4)
		work->bstate |= BUTTON3_RELEASED;
	}

	/* for cosmetic bug in syscons.c on FreeBSD 3.[34] */
	the_mouse.operation = MOUSE_HIDE;
	ioctl(sp->_mouse_fd, CONS_MOUSECTL, &the_mouse);
	the_mouse.operation = MOUSE_SHOW;
	ioctl(sp->_mouse_fd, CONS_MOUSECTL, &the_mouse);

	/*
	 * We're only interested if the button is pressed or released.
	 * FIXME: implement continuous event-tracking.
	 */
	if (sp->_sysmouse_new_buttons != sp->_sysmouse_old_buttons) {
	    sp->_sysmouse_tail += 1;
	}
	work->x = the_mouse.u.data.x / sp->_sysmouse_char_width;
	work->y = the_mouse.u.data.y / sp->_sysmouse_char_height;
    }
}

static void
handle_sysmouse(int sig GCC_UNUSED)
{
    sysmouse_server(CURRENT_SCREEN);
}
#endif /* USE_SYSMOUSE */

#ifndef USE_TERM_DRIVER
#define xterm_kmous "\033[M"

static void
init_xterm_mouse(SCREEN *sp)
{
    sp->_mouse_type = M_XTERM;
    sp->_mouse_xtermcap = tigetstr("XM");
    if (!VALID_STRING(sp->_mouse_xtermcap))
	sp->_mouse_xtermcap = "\033[?1000%?%p1%{1}%=%th%el%;";
}
#endif

static void
enable_xterm_mouse(SCREEN *sp, int enable)
{
#if USE_EMX_MOUSE
    sp->_emxmouse_activated = enable;
#else
    NCURSES_PUTP2("xterm-mouse", TPARM_1(sp->_mouse_xtermcap, enable));
#endif
    sp->_mouse_active = enable;
}

#if USE_GPM_SUPPORT
static bool
allow_gpm_mouse(SCREEN *sp)
{
    bool result = FALSE;

#if USE_WEAK_SYMBOLS
    /* Danger Robinson: do not use dlopen for libgpm if already loaded */
    if ((Gpm_Wgetch)) {
	if (!sp->_mouse_gpm_loaded) {
	    T(("GPM library was already dlopen'd, not by us"));
	}
    } else
#endif
	/* GPM does printf's without checking if stdout is a terminal */
    if (isatty(fileno(stdout))) {
	char *list = getenv("NCURSES_GPM_TERMS");
	char *env = getenv("TERM");
	if (list != 0) {
	    if (env != 0) {
		result = _nc_name_match(list, env, "|:");
	    }
	} else {
	    /* GPM checks the beginning of the $TERM variable to decide if it
	     * should pass xterm events through.  There is no real advantage in
	     * allowing GPM to do this.  Recent versions relax that check, and
	     * pretend that GPM can work with any terminal having the kmous
	     * capability.  Perhaps that works for someone.  If so, they can
	     * set the environment variable (above).
	     */
	    if (env != 0 && strstr(env, "linux") != 0) {
		result = TRUE;
	    }
	}
    }
    return result;
}

#ifdef HAVE_LIBDL
static void
unload_gpm_library(SCREEN *sp)
{
    if (sp->_dlopen_gpm != 0) {
	T(("unload GPM library"));
	sp->_mouse_gpm_loaded = FALSE;
	sp->_mouse_fd = -1;
	dlclose(sp->_dlopen_gpm);
	sp->_dlopen_gpm = 0;
    }
}

static void
load_gpm_library(SCREEN *sp)
{
    sp->_mouse_gpm_found = FALSE;
    if ((sp->_dlopen_gpm = dlopen(LIBGPM_SONAME, my_RTLD)) != 0) {
	if (GET_DLSYM(gpm_fd) == 0 ||
	    GET_DLSYM(Gpm_Open) == 0 ||
	    GET_DLSYM(Gpm_Close) == 0 ||
	    GET_DLSYM(Gpm_GetEvent) == 0) {
	    T(("GPM initialization failed: %s", dlerror()));
	    unload_gpm_library(sp);
	} else {
	    sp->_mouse_gpm_found = TRUE;
	    sp->_mouse_gpm_loaded = TRUE;
	}
    }
}
#endif

static bool
enable_gpm_mouse(SCREEN *sp, bool enable)
{
    bool result;

    T((T_CALLED("enable_gpm_mouse(%d)"), enable));

    if (enable && !sp->_mouse_active) {
#ifdef HAVE_LIBDL
	if (sp->_mouse_gpm_found && !sp->_mouse_gpm_loaded) {
	    load_gpm_library(sp);
	}
#endif
	if (sp->_mouse_gpm_loaded) {
	    int code;

	    /* GPM: initialize connection to gpm server */
	    sp->_mouse_gpm_connect.eventMask = GPM_DOWN | GPM_UP;
	    sp->_mouse_gpm_connect.defaultMask =
		(unsigned short) (~(sp->_mouse_gpm_connect.eventMask | GPM_HARD));
	    sp->_mouse_gpm_connect.minMod = 0;
	    sp->_mouse_gpm_connect.maxMod =
		(unsigned short) (~((1 << KG_SHIFT) |
				    (1 << KG_SHIFTL) |
				    (1 << KG_SHIFTR)));
	    /*
	     * Note: GPM hardcodes \E[?1001s and \E[?1000h during its open.
	     * The former is recognized by wscons (SunOS), and the latter by
	     * xterm.  Those will not show up in ncurses' traces.
	     */
	    code = my_Gpm_Open(&sp->_mouse_gpm_connect, 0);
	    result = (code >= 0);

	    /*
	     * GPM can return a -2 if it is trying to do something with xterm.
	     * Ignore that, since it conflicts with our use of stdin.
	     */
	    if (code == -2) {
		my_Gpm_Close();
	    }
	} else {
	    result = FALSE;
	}
	sp->_mouse_active = result;
	T(("GPM open %s", result ? "succeeded" : "failed"));
    } else {
	if (!enable && sp->_mouse_active) {
	    /* GPM: close connection to gpm server */
	    my_Gpm_Close();
	    sp->_mouse_active = FALSE;
	    T(("GPM closed"));
	}
	result = enable;
    }
#ifdef HAVE_LIBDL
    if (!result) {
	unload_gpm_library(sp);
    }
#endif
    returnBool(result);
}
#endif /* USE_GPM_SUPPORT */

static void
initialize_mousetype(SCREEN *sp)
{
    T((T_CALLED("initialize_mousetype()")));

    /* Try gpm first, because gpm may be configured to run in xterm */
#if USE_GPM_SUPPORT
    if (allow_gpm_mouse(sp)) {
	if (!sp->_mouse_gpm_loaded) {
#ifdef HAVE_LIBDL
	    load_gpm_library(sp);
#else /* !HAVE_LIBDL */
	    sp->_mouse_gpm_found = TRUE;
	    sp->_mouse_gpm_loaded = TRUE;
#endif
	}

	/*
	 * The gpm_fd file-descriptor may be negative (xterm).  So we have to
	 * maintain our notion of whether the mouse connection is active
	 * without testing the file-descriptor.
	 */
	if (sp->_mouse_gpm_found && enable_gpm_mouse(sp, TRUE)) {
	    sp->_mouse_type = M_GPM;
	    sp->_mouse_fd = *(my_gpm_fd);
	    T(("GPM mouse_fd %d", sp->_mouse_fd));
	    returnVoid;
	}
    }
#endif /* USE_GPM_SUPPORT */

    /* OS/2 VIO */
#if USE_EMX_MOUSE
    if (!sp->_emxmouse_thread
	&& strstr(TerminalOf(sp)->type.term_names, "xterm") == 0
	&& key_mouse) {
	int handles[2];

	if (pipe(handles) < 0) {
	    perror("mouse pipe error");
	    returnVoid;
	} else {
	    int rc;

	    if (!sp->_emxmouse_buttons[0]) {
		char *s = getenv("MOUSE_BUTTONS_123");

		sp->_emxmouse_buttons[0] = 1;
		if (s && strlen(s) >= 3) {
		    sp->_emxmouse_buttons[1] = s[0] - '0';
		    sp->_emxmouse_buttons[2] = s[1] - '0';
		    sp->_emxmouse_buttons[3] = s[2] - '0';
		} else {
		    sp->_emxmouse_buttons[1] = 1;
		    sp->_emxmouse_buttons[2] = 3;
		    sp->_emxmouse_buttons[3] = 2;
		}
	    }
	    sp->_emxmouse_wfd = handles[1];
	    M_FD(sp) = handles[0];
	    /* Needed? */
	    setmode(handles[0], O_BINARY);
	    setmode(handles[1], O_BINARY);
	    /* Do not use CRT functions, we may single-threaded. */
	    rc = DosCreateThread((unsigned long *) &sp->_emxmouse_thread,
				 mouse_server, (long) sp, 0, 8192);
	    if (rc) {
		printf("mouse thread error %d=%#x", rc, rc);
	    } else {
		sp->_mouse_type = M_XTERM;
	    }
	    returnVoid;
	}
    }
#endif /* USE_EMX_MOUSE */

#if USE_SYSMOUSE
    {
	struct mouse_info the_mouse;
	char *the_device = 0;

	if (isatty(sp->_ifd))
	    the_device = ttyname(sp->_ifd);
	if (the_device == 0)
	    the_device = "/dev/tty";

	sp->_mouse_fd = open(the_device, O_RDWR);

	if (sp->_mouse_fd >= 0) {
	    /*
	     * sysmouse does not have a usable user interface for obtaining
	     * mouse events.  The logical way to proceed (reading data on a
	     * stream) only works if one opens the device as root.  Even in
	     * that mode, careful examination shows we lose events
	     * occasionally.  The interface provided for user programs is to
	     * establish a signal handler.  really.
	     *
	     * Take over SIGUSR2 for this purpose since SIGUSR1 is more
	     * likely to be used by an application.  getch() will have to
	     * handle the misleading EINTR's.
	     */
	    signal(SIGUSR2, SIG_IGN);
	    the_mouse.operation = MOUSE_MODE;
	    the_mouse.u.mode.mode = 0;
	    the_mouse.u.mode.signal = SIGUSR2;
	    if (ioctl(sp->_mouse_fd, CONS_MOUSECTL, &the_mouse) != -1) {
		signal(SIGUSR2, handle_sysmouse);
		the_mouse.operation = MOUSE_SHOW;
		ioctl(sp->_mouse_fd, CONS_MOUSECTL, &the_mouse);

#if defined(FBIO_MODEINFO) || defined(CONS_MODEINFO)	/* FreeBSD > 2.x */
		{
#ifndef FBIO_GETMODE		/* FreeBSD 3.x */
#define FBIO_GETMODE    CONS_GET
#define FBIO_MODEINFO   CONS_MODEINFO
#endif /* FBIO_GETMODE */
		    video_info_t the_video;

		    if (ioctl(sp->_mouse_fd,
			      FBIO_GETMODE,
			      &the_video.vi_mode) != -1
			&& ioctl(sp->_mouse_fd,
				 FBIO_MODEINFO,
				 &the_video) != -1) {
			sp->_sysmouse_char_width = the_video.vi_cwidth;
			sp->_sysmouse_char_height = the_video.vi_cheight;
		    }
		}
#endif /* defined(FBIO_MODEINFO) || defined(CONS_MODEINFO) */

		if (sp->_sysmouse_char_width <= 0)
		    sp->_sysmouse_char_width = 8;
		if (sp->_sysmouse_char_height <= 0)
		    sp->_sysmouse_char_height = 16;
		sp->_mouse_type = M_SYSMOUSE;
		returnVoid;
	    }
	}
    }
#endif /* USE_SYSMOUSE */

#ifdef USE_TERM_DRIVER
    CallDriver(sp, initmouse);
#else
    /* we know how to recognize mouse events under "xterm" */
    if (key_mouse != 0) {
	if (!strcmp(key_mouse, xterm_kmous)
	    || strstr(TerminalOf(sp)->type.term_names, "xterm") != 0) {
	    init_xterm_mouse(sp);
	}
    } else if (strstr(TerminalOf(sp)->type.term_names, "xterm") != 0) {
	if (_nc_add_to_try(&(sp->_keytry), xterm_kmous, KEY_MOUSE) == OK)
	    init_xterm_mouse(sp);
    }
#endif

    returnVoid;
}

static bool
_nc_mouse_init(SCREEN *sp)
/* initialize the mouse */
{
    bool result = FALSE;
    int i;

    if (sp != 0) {
	if (!sp->_mouse_initialized) {
	    sp->_mouse_initialized = TRUE;

	    TR(MY_TRACE, ("_nc_mouse_init() called"));

	    sp->_mouse_eventp = FirstEV(sp);
	    for (i = 0; i < EV_MAX; i++)
		Invalidate(sp->_mouse_events + i);

	    initialize_mousetype(sp);

	    T(("_nc_mouse_init() set mousetype to %d", sp->_mouse_type));
	}
	result = sp->_mouse_initialized;
    }
    return result;
}

/*
 * Query to see if there is a pending mouse event.  This is called from
 * fifo_push() in lib_getch.c
 */
static bool
_nc_mouse_event(SCREEN *sp)
{
    MEVENT *eventp = sp->_mouse_eventp;
    bool result = FALSE;

    (void) eventp;

    switch (sp->_mouse_type) {
    case M_XTERM:
	/* xterm: never have to query, mouse events are in the keyboard stream */
#if USE_EMX_MOUSE
	{
	    char kbuf[3];

	    int i, res = read(M_FD(sp), &kbuf, 3);	/* Eat the prefix */
	    if (res != 3)
		printf("Got %d chars instead of 3 for prefix.\n", res);
	    for (i = 0; i < res; i++) {
		if (kbuf[i] != key_mouse[i])
		    printf("Got char %d instead of %d for prefix.\n",
			   (int) kbuf[i], (int) key_mouse[i]);
	    }
	    result = TRUE;
	}
#endif /* USE_EMX_MOUSE */
	break;

#if USE_GPM_SUPPORT
    case M_GPM:
	if (sp->_mouse_fd >= 0) {
	    /* query server for event, return TRUE if we find one */
	    Gpm_Event ev;

	    switch (my_Gpm_GetEvent(&ev)) {
	    case 0:
		/* Connection closed, drop the mouse. */
		sp->_mouse_fd = -1;
		break;
	    case 1:
		/* there's only one mouse... */
		eventp->id = NORMAL_EVENT;

		eventp->bstate = 0;
		switch (ev.type & 0x0f) {
		case (GPM_DOWN):
		    if (ev.buttons & GPM_B_LEFT)
			eventp->bstate |= BUTTON1_PRESSED;
		    if (ev.buttons & GPM_B_MIDDLE)
			eventp->bstate |= BUTTON2_PRESSED;
		    if (ev.buttons & GPM_B_RIGHT)
			eventp->bstate |= BUTTON3_PRESSED;
		    break;
		case (GPM_UP):
		    if (ev.buttons & GPM_B_LEFT)
			eventp->bstate |= BUTTON1_RELEASED;
		    if (ev.buttons & GPM_B_MIDDLE)
			eventp->bstate |= BUTTON2_RELEASED;
		    if (ev.buttons & GPM_B_RIGHT)
			eventp->bstate |= BUTTON3_RELEASED;
		    break;
		default:
		    eventp->bstate |= REPORT_MOUSE_POSITION;
		    break;
		}

		eventp->x = ev.x - 1;
		eventp->y = ev.y - 1;
		eventp->z = 0;

		/* bump the next-free pointer into the circular list */
		sp->_mouse_eventp = NEXT(eventp);
		result = TRUE;
		break;
	    }
	}
	break;
#endif

#if USE_SYSMOUSE
    case M_SYSMOUSE:
	if (sp->_sysmouse_head < sp->_sysmouse_tail) {
	    *eventp = sp->_sysmouse_fifo[sp->_sysmouse_head];

	    /*
	     * Point the fifo-head to the next possible location.  If there
	     * are none, reset the indices.  This may be interrupted by the
	     * signal handler, doing essentially the same reset.
	     */
	    sp->_sysmouse_head += 1;
	    if (sp->_sysmouse_head == sp->_sysmouse_tail) {
		sp->_sysmouse_tail = 0;
		sp->_sysmouse_head = 0;
	    }

	    /* bump the next-free pointer into the circular list */
	    sp->_mouse_eventp = eventp = NEXT(eventp);
	    result = TRUE;
	}
	break;
#endif /* USE_SYSMOUSE */

#ifdef USE_TERM_DRIVER
    case M_TERM_DRIVER:
	while (sp->_drv_mouse_head < sp->_drv_mouse_tail) {
	    *eventp = sp->_drv_mouse_fifo[sp->_drv_mouse_head];

	    /*
	     * Point the fifo-head to the next possible location.  If there
	     * are none, reset the indices.
	     */
	    sp->_drv_mouse_head += 1;
	    if (sp->_drv_mouse_head == sp->_drv_mouse_tail) {
		sp->_drv_mouse_tail = 0;
		sp->_drv_mouse_head = 0;
	    }

	    /* bump the next-free pointer into the circular list */
	    sp->_mouse_eventp = eventp = NEXT(eventp);
	    result = TRUE;
	}
	break;
#endif

    case M_NONE:
	break;
    }

    return result;		/* true if we found an event */
}

static bool
_nc_mouse_inline(SCREEN *sp)
/* mouse report received in the keyboard stream -- parse its info */
{
    int b;
    bool result = FALSE;
    MEVENT *eventp = sp->_mouse_eventp;

    TR(MY_TRACE, ("_nc_mouse_inline() called"));

    if (sp->_mouse_type == M_XTERM) {
	unsigned char kbuf[4];
	size_t grabbed;
	int res;

	/* This code requires that your xterm entry contain the kmous
	 * capability and that it be set to the \E[M documented in the
	 * Xterm Control Sequences reference.  This is how we
	 * arrange for mouse events to be reported via a KEY_MOUSE
	 * return value from wgetch().  After this value is received,
	 * _nc_mouse_inline() gets called and is immediately
	 * responsible for parsing the mouse status information
	 * following the prefix.
	 *
	 * The following quotes from the ctrlseqs.ms document in the
	 * X distribution, describing the X mouse tracking feature:
	 *
	 * Parameters for all mouse tracking escape sequences
	 * generated by xterm encode numeric parameters in a single
	 * character as value+040.  For example, !  is 1.
	 *
	 * On button press or release, xterm sends ESC [ M CbCxCy.
	 * The low two bits of Cb encode button information: 0=MB1
	 * pressed, 1=MB2 pressed, 2=MB3 pressed, 3=release.  The
	 * upper bits encode what modifiers were down when the
	 * button was pressed and are added together.  4=Shift,
	 * 8=Meta, 16=Control.  Cx and Cy are the x and y coordinates
	 * of the mouse event.  The upper left corner is (1,1).
	 *
	 * (End quote)  By the time we get here, we've eaten the
	 * key prefix.  FYI, the loop below is necessary because
	 * mouse click info isn't guaranteed to present as a
	 * single clist item.
	 *
	 * Wheel mice may return buttons 4 and 5 when the wheel is turned.
	 * We encode those as button presses.
	 */
# if USE_PTHREADS_EINTR
#  if USE_WEAK_SYMBOLS
	if ((pthread_self) && (pthread_kill) && (pthread_equal))
#  endif
	    _nc_globals.read_thread = pthread_self();
# endif
	for (grabbed = 0; grabbed < 3; grabbed += (size_t) res) {

	    /* For VIO mouse we add extra bit 64 to disambiguate button-up. */
#if USE_EMX_MOUSE
	    res = (int) read(M_FD(sp) >= 0 ? M_FD(sp) : sp->_ifd, &kbuf, 3);
#else
	    res = (int) read(sp->_ifd, kbuf + grabbed, 3 - grabbed);
#endif
	    if (res == -1)
		break;
	}
#if USE_PTHREADS_EINTR
	_nc_globals.read_thread = 0;
#endif
	kbuf[3] = '\0';

	TR(TRACE_IEVENT,
	   ("_nc_mouse_inline sees the following xterm data: '%s'", kbuf));

	/* there's only one mouse... */
	eventp->id = NORMAL_EVENT;

	/* processing code goes here */
	eventp->bstate = 0;

#if USE_EMX_MOUSE
#define PRESS_POSITION(n) \
	do { \
		eventp->bstate = MASK_PRESS(n); \
		sp->_mouse_bstate |= MASK_PRESS(n); \
		if (kbuf[0] & 0x40) { \
			eventp->bstate = MASK_RELEASE(n); \
			sp->_mouse_bstate &= ~MASK_PRESS(n); \
		} \
	} while (0)
#else
#define PRESS_POSITION(n) \
	do { \
		eventp->bstate = (mmask_t) (sp->_mouse_bstate & MASK_PRESS(n) \
					? REPORT_MOUSE_POSITION \
					: MASK_PRESS(n)); \
		sp->_mouse_bstate |= MASK_PRESS(n); \
	} while (0)
#endif

	switch (kbuf[0] & 0x3) {
	case 0x0:
	    if ((kbuf[0] & 96) == 96) {
		eventp->bstate = MASK_PRESS(4);
		/* Do not record in sp->_mouse_bstate; there will be no
		 * corresponding release event.
		 */
	    } else {
		PRESS_POSITION(1);
	    }
	    break;

	case 0x1:
	    if ((kbuf[0] & 96) == 96) {
#if NCURSES_MOUSE_VERSION == 2
		eventp->bstate = MASK_PRESS(5);
		/* See comment above for button 4 */
#else
		/* Ignore this event as it is not a true press of the button */
		eventp->bstate = REPORT_MOUSE_POSITION;
#endif
	    } else {
		PRESS_POSITION(2);
	    }
	    break;

	case 0x2:
	    PRESS_POSITION(3);
	    break;

	case 0x3:
	    /*
	     * Release events aren't reported for individual buttons, just for
	     * the button set as a whole.  However, because there are normally
	     * no mouse events under xterm that intervene between press and
	     * release, we can infer the button actually released by looking at
	     * the previous event.
	     */
	    if (sp->_mouse_bstate & BUTTON_PRESSED) {
		eventp->bstate = BUTTON_RELEASED;
		for (b = 1; b <= MAX_BUTTONS; ++b) {
		    if (!(sp->_mouse_bstate & MASK_PRESS(b)))
			eventp->bstate &= ~MASK_RELEASE(b);
		}
		sp->_mouse_bstate = 0;
	    } else {
		/*
		 * XFree86 xterm will return a stream of release-events to
		 * let the application know where the mouse is going, if the
		 * private mode 1002 or 1003 is enabled.
		 */
		eventp->bstate = REPORT_MOUSE_POSITION;
	    }
	    break;
	}
	result = (eventp->bstate & REPORT_MOUSE_POSITION) ? TRUE : FALSE;

	if (kbuf[0] & 4) {
	    eventp->bstate |= BUTTON_SHIFT;
	}
	if (kbuf[0] & 8) {
	    eventp->bstate |= BUTTON_ALT;
	}
	if (kbuf[0] & 16) {
	    eventp->bstate |= BUTTON_CTRL;
	}

	eventp->x = (kbuf[1] - ' ') - 1;
	eventp->y = (kbuf[2] - ' ') - 1;
	TR(MY_TRACE,
	   ("_nc_mouse_inline: primitive mouse-event %s has slot %ld",
	    _nc_tracemouse(sp, eventp),
	    (long) IndexEV(sp, eventp)));

	/* bump the next-free pointer into the circular list */
	sp->_mouse_eventp = NEXT(eventp);
#if 0				/* this return would be needed for QNX's mods to lib_getch.c */
	return (TRUE);
#endif
    }

    return (result);
}

static void
mouse_activate(SCREEN *sp, int on)
{
    if (!on && !sp->_mouse_initialized)
	return;

    if (!_nc_mouse_init(sp))
	return;

    if (on) {
	sp->_mouse_bstate = 0;
	switch (sp->_mouse_type) {
	case M_XTERM:
#if NCURSES_EXT_FUNCS
	    NCURSES_SP_NAME(keyok) (NCURSES_SP_ARGx KEY_MOUSE, on);
#endif
	    TPUTS_TRACE("xterm mouse initialization");
	    enable_xterm_mouse(sp, 1);
	    break;
#if USE_GPM_SUPPORT
	case M_GPM:
	    if (enable_gpm_mouse(sp, TRUE)) {
		sp->_mouse_fd = *(my_gpm_fd);
		T(("GPM mouse_fd %d", sp->_mouse_fd));
	    }
	    break;
#endif
#if USE_SYSMOUSE
	case M_SYSMOUSE:
	    signal(SIGUSR2, handle_sysmouse);
	    sp->_mouse_active = TRUE;
	    break;
#endif
#ifdef USE_TERM_DRIVER
	case M_TERM_DRIVER:
	    sp->_mouse_active = TRUE;
	    break;
#endif
	case M_NONE:
	    return;
	}
	/* Make runtime binding to cut down on object size of applications that
	 * do not use the mouse (e.g., 'clear').
	 */
	sp->_mouse_event = _nc_mouse_event;
	sp->_mouse_inline = _nc_mouse_inline;
	sp->_mouse_parse = _nc_mouse_parse;
	sp->_mouse_resume = _nc_mouse_resume;
	sp->_mouse_wrap = _nc_mouse_wrap;
    } else {

	switch (sp->_mouse_type) {
	case M_XTERM:
	    TPUTS_TRACE("xterm mouse deinitialization");
	    enable_xterm_mouse(sp, 0);
	    break;
#if USE_GPM_SUPPORT
	case M_GPM:
	    enable_gpm_mouse(sp, FALSE);
	    break;
#endif
#if USE_SYSMOUSE
	case M_SYSMOUSE:
	    signal(SIGUSR2, SIG_IGN);
	    sp->_mouse_active = FALSE;
	    break;
#endif
#ifdef USE_TERM_DRIVER
	case M_TERM_DRIVER:
	    sp->_mouse_active = FALSE;
	    break;
#endif
	case M_NONE:
	    return;
	}
    }
    NCURSES_SP_NAME(_nc_flush) (NCURSES_SP_ARG);
}

/**************************************************************************
 *
 * Device-independent code
 *
 **************************************************************************/

static bool
_nc_mouse_parse(SCREEN *sp, int runcount)
/* parse a run of atomic mouse events into a gesture */
{
    MEVENT *eventp = sp->_mouse_eventp;
    MEVENT *next, *ep;
    MEVENT *first_valid = NULL;
    MEVENT *first_invalid = NULL;
    int n;
    int b;
    bool merge;
    bool endLoop;

    TR(MY_TRACE, ("_nc_mouse_parse(%d) called", runcount));

    /*
     * When we enter this routine, the event list next-free pointer
     * points just past a run of mouse events that we know were separated
     * in time by less than the critical click interval. The job of this
     * routine is to collapse this run into a single higher-level event
     * or gesture.
     *
     * We accomplish this in two passes.  The first pass merges press/release
     * pairs into click events.  The second merges runs of click events into
     * double or triple-click events.
     *
     * It's possible that the run may not resolve to a single event (for
     * example, if the user quadruple-clicks).  If so, leading events
     * in the run are ignored if user does not call getmouse in a loop (getting
     * them from newest to older).
     *
     * Note that this routine is independent of the format of the specific
     * format of the pointing-device's reports.  We can use it to parse
     * gestures on anything that reports press/release events on a per-
     * button basis, as long as the device-dependent mouse code puts stuff
     * on the queue in MEVENT format.
     */

    /*
     * Reset all events that were not set, in case the user sometimes calls
     * getmouse only once and other times until there are no more events in
     * queue.
     *
     * This also allows reaching the beginning of the run.
     */
    ep = eventp;
    for (n = runcount; n < EV_MAX; n++) {
	Invalidate(ep);
	ep = NEXT(ep);
    }

#ifdef TRACE
    if (USE_TRACEF(TRACE_IEVENT)) {
	_trace_slot(sp, "before mouse press/release merge:");
	_tracef("_nc_mouse_parse: run starts at %ld, ends at %ld, count %d",
		RunParams(sp, eventp, ep),
		runcount);
	_nc_unlock_global(tracef);
    }
#endif /* TRACE */

    /* first pass; merge press/release pairs */
    endLoop = FALSE;
    while (!endLoop) {
	next = NEXT(ep);
	if (next == eventp) {
	    /* Will end the loop, but compact before */
	    endLoop = TRUE;
	} else {

#define MASK_CHANGED(x) (!(ep->bstate & MASK_PRESS(x)) \
		      == !(next->bstate & MASK_RELEASE(x)))

	    if (ValidEvent(ep) && ValidEvent(next)
		&& ep->x == next->x && ep->y == next->y
		&& (ep->bstate & BUTTON_PRESSED)
		&& (!(next->bstate & BUTTON_PRESSED))) {
		bool changed = TRUE;

		for (b = 1; b <= MAX_BUTTONS; ++b) {
		    if (!MASK_CHANGED(b)) {
			changed = FALSE;
			break;
		    }
		}

		if (changed) {
		    merge = FALSE;
		    for (b = 1; b <= MAX_BUTTONS; ++b) {
			if ((sp->_mouse_mask & MASK_CLICK(b))
			    && (ep->bstate & MASK_PRESS(b))) {
			    next->bstate &= ~MASK_RELEASE(b);
			    next->bstate |= MASK_CLICK(b);
			    merge = TRUE;
			}
		    }
		    if (merge) {
			Invalidate(ep);
		    }
		}
	    }
	}

	/* Compact valid events */
	if (!ValidEvent(ep)) {
	    if ((first_valid != NULL) && (first_invalid == NULL)) {
		first_invalid = ep;
	    }
	} else {
	    if (first_valid == NULL) {
		first_valid = ep;
	    } else if (first_invalid != NULL) {
		*first_invalid = *ep;
		Invalidate(ep);
		first_invalid = NEXT(first_invalid);
	    }
	}

	ep = next;
    }

    if (first_invalid != NULL) {
	eventp = first_invalid;
    }
#ifdef TRACE
    if (USE_TRACEF(TRACE_IEVENT)) {
	_trace_slot(sp, "before mouse click merge:");
	if (first_valid == NULL) {
	    _tracef("_nc_mouse_parse: no valid event");
	} else {
	    _tracef("_nc_mouse_parse: run starts at %ld, ends at %ld, count %d",
		    RunParams(sp, eventp, first_valid),
		    runcount);
	    _nc_unlock_global(tracef);
	}
    }
#endif /* TRACE */

    /*
     * Second pass; merge click runs.  We merge click events forward in the
     * queue.  For example, double click can be changed to triple click.
     *
     * NOTE: There is a problem with this design!  If the application
     * allows enough click events to pile up in the circular queue so
     * they wrap around, it will cheerfully merge the newest forward
     * into the oldest, creating a bogus doubleclick and confusing
     * the queue-traversal logic rather badly.  Generally this won't
     * happen, because calling getmouse() marks old events invalid and
     * ineligible for merges.  The true solution to this problem would
     * be to timestamp each MEVENT and perform the obvious sanity check,
     * but the timer element would have to have sub-second resolution,
     * which would get us into portability trouble.
     */
    first_invalid = NULL;
    endLoop = (first_valid == NULL);
    ep = first_valid;
    while (!endLoop) {
	next = NEXT(ep);

	if (next == eventp) {
	    /* Will end the loop, but check event type and compact before */
	    endLoop = TRUE;
	} else if (!ValidEvent(next)) {
	    continue;
	} else {
	    /* merge click events forward */
	    if ((ep->bstate & BUTTON_CLICKED)
		&& (next->bstate & BUTTON_CLICKED)) {
		merge = FALSE;
		for (b = 1; b <= MAX_BUTTONS; ++b) {
		    if ((sp->_mouse_mask & MASK_DOUBLE_CLICK(b))
			&& (ep->bstate & MASK_CLICK(b))
			&& (next->bstate & MASK_CLICK(b))) {
			next->bstate &= ~MASK_CLICK(b);
			next->bstate |= MASK_DOUBLE_CLICK(b);
			merge = TRUE;
		    }
		}
		if (merge) {
		    Invalidate(ep);
		}
	    }

	    /* merge double-click events forward */
	    if ((ep->bstate & BUTTON_DOUBLE_CLICKED)
		&& (next->bstate & BUTTON_CLICKED)) {
		merge = FALSE;
		for (b = 1; b <= MAX_BUTTONS; ++b) {
		    if ((sp->_mouse_mask & MASK_TRIPLE_CLICK(b))
			&& (ep->bstate & MASK_DOUBLE_CLICK(b))
			&& (next->bstate & MASK_CLICK(b))) {
			next->bstate &= ~MASK_CLICK(b);
			next->bstate |= MASK_TRIPLE_CLICK(b);
			merge = TRUE;
		    }
		}
		if (merge) {
		    Invalidate(ep);
		}
	    }
	}

	/* Discard event if it does not match event mask */
	if (!(ep->bstate & sp->_mouse_mask2)) {
	    Invalidate(ep);
	}

	/* Compact valid events */
	if (!ValidEvent(ep)) {
	    if (ep == first_valid) {
		first_valid = next;
	    } else if (first_invalid == NULL) {
		first_invalid = ep;
	    }
	} else if (first_invalid != NULL) {
	    *first_invalid = *ep;
	    Invalidate(ep);
	    first_invalid = NEXT(first_invalid);
	}

	ep = next;
    }

    if (first_invalid == NULL) {
	first_invalid = eventp;
    }
    sp->_mouse_eventp = first_invalid;

#ifdef TRACE
    if (first_valid != NULL) {
	if (USE_TRACEF(TRACE_IEVENT)) {
	    _trace_slot(sp, "after mouse event queue compaction:");
	    _tracef("_nc_mouse_parse: run starts at %ld, ends at %ld, count %d",
		    RunParams(sp, first_invalid, first_valid),
		    runcount);
	    _nc_unlock_global(tracef);
	}
	for (ep = first_valid; ep != first_invalid; ep = NEXT(ep)) {
	    if (ValidEvent(ep))
		TR(MY_TRACE,
		   ("_nc_mouse_parse: returning composite mouse event %s at slot %ld",
		    _nc_tracemouse(sp, ep),
		    (long) IndexEV(sp, ep)));
	}
    }
#endif /* TRACE */

    /* after all this, do we have a valid event? */
    return ValidEvent(PREV(first_invalid));
}

static void
_nc_mouse_wrap(SCREEN *sp)
/* release mouse -- called by endwin() before shellout/exit */
{
    TR(MY_TRACE, ("_nc_mouse_wrap() called"));

    switch (sp->_mouse_type) {
    case M_XTERM:
	if (sp->_mouse_mask)
	    mouse_activate(sp, FALSE);
	break;
#if USE_GPM_SUPPORT
	/* GPM: pass all mouse events to next client */
    case M_GPM:
	if (sp->_mouse_mask)
	    mouse_activate(sp, FALSE);
	break;
#endif
#if USE_SYSMOUSE
    case M_SYSMOUSE:
	mouse_activate(sp, FALSE);
	break;
#endif
#ifdef USE_TERM_DRIVER
    case M_TERM_DRIVER:
	mouse_activate(sp, FALSE);
	break;
#endif
    case M_NONE:
	break;
    }
}

static void
_nc_mouse_resume(SCREEN *sp)
/* re-connect to mouse -- called by doupdate() after shellout */
{
    TR(MY_TRACE, ("_nc_mouse_resume() called"));

    switch (sp->_mouse_type) {
    case M_XTERM:
	/* xterm: re-enable reporting */
	if (sp->_mouse_mask)
	    mouse_activate(sp, TRUE);
	break;

#if USE_GPM_SUPPORT
    case M_GPM:
	/* GPM: reclaim our event set */
	if (sp->_mouse_mask)
	    mouse_activate(sp, TRUE);
	break;
#endif

#if USE_SYSMOUSE
    case M_SYSMOUSE:
	mouse_activate(sp, TRUE);
	break;
#endif

#ifdef USE_TERM_DRIVER
    case M_TERM_DRIVER:
	mouse_activate(sp, TRUE);
	break;
#endif

    case M_NONE:
	break;
    }
}

/**************************************************************************
 *
 * Mouse interface entry points for the API
 *
 **************************************************************************/

NCURSES_EXPORT(int)
NCURSES_SP_NAME(getmouse) (NCURSES_SP_DCLx MEVENT * aevent)
{
    int result = ERR;

    T((T_CALLED("getmouse(%p,%p)"), (void *) SP_PARM, (void *) aevent));

    if ((aevent != 0) && (SP_PARM != 0) && (SP_PARM->_mouse_type != M_NONE)) {
	MEVENT *eventp = SP_PARM->_mouse_eventp;
	/* compute the current-event pointer */
	MEVENT *prev = PREV(eventp);

	/*
	 * Discard events not matching mask (there could be still some if
	 * _nc_mouse_parse was not called, e.g., when _nc_mouse_inline returns
	 * false).
	 */
	while (ValidEvent(prev) && (!(prev->bstate & SP_PARM->_mouse_mask2))) {
	    Invalidate(prev);
	    prev = PREV(prev);
	}
	if (ValidEvent(prev)) {
	    /* copy the event we find there */
	    *aevent = *prev;

	    TR(TRACE_IEVENT, ("getmouse: returning event %s from slot %ld",
			      _nc_tracemouse(SP_PARM, prev),
			      (long) IndexEV(SP_PARM, prev)));

	    Invalidate(prev);	/* so the queue slot becomes free */
	    SP_PARM->_mouse_eventp = prev;
	    result = OK;
	} else {
	    /* Reset the provided event */
	    aevent->bstate = 0;
	    Invalidate(aevent);
	    aevent->x = 0;
	    aevent->y = 0;
	    aevent->z = 0;
	}
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
/* grab a copy of the current mouse event */
NCURSES_EXPORT(int)
getmouse(MEVENT * aevent)
{
    return NCURSES_SP_NAME(getmouse) (CURRENT_SCREEN, aevent);
}
#endif

NCURSES_EXPORT(int)
NCURSES_SP_NAME(ungetmouse) (NCURSES_SP_DCLx MEVENT * aevent)
{
    int result = ERR;

    T((T_CALLED("ungetmouse(%p,%p)"), (void *) SP_PARM, (void *) aevent));

    if (aevent != 0 && SP_PARM != 0) {
	MEVENT *eventp = SP_PARM->_mouse_eventp;

	/* stick the given event in the next-free slot */
	*eventp = *aevent;

	/* bump the next-free pointer into the circular list */
	SP_PARM->_mouse_eventp = NEXT(eventp);

	/* push back the notification event on the keyboard queue */
	result = NCURSES_SP_NAME(ungetch) (NCURSES_SP_ARGx KEY_MOUSE);
    }
    returnCode(result);
}

#if NCURSES_SP_FUNCS
/* enqueue a synthesized mouse event to be seen by the next wgetch() */
NCURSES_EXPORT(int)
ungetmouse(MEVENT * aevent)
{
    return NCURSES_SP_NAME(ungetmouse) (CURRENT_SCREEN, aevent);
}
#endif

NCURSES_EXPORT(mmask_t)
NCURSES_SP_NAME(mousemask) (NCURSES_SP_DCLx mmask_t newmask, mmask_t * oldmask)
/* set the mouse event mask */
{
    mmask_t result = 0;
    int b;

    T((T_CALLED("mousemask(%p,%#lx,%p)"),
       (void *) SP_PARM,
       (unsigned long) newmask,
       (void *) oldmask));

    if (SP_PARM != 0) {
	if (oldmask)
	    *oldmask = SP_PARM->_mouse_mask;

	if (newmask || SP_PARM->_mouse_initialized) {
	    _nc_mouse_init(SP_PARM);
	    if (SP_PARM->_mouse_type != M_NONE) {
		result = newmask &
		    (REPORT_MOUSE_POSITION
		     | BUTTON_ALT
		     | BUTTON_CTRL
		     | BUTTON_SHIFT
		     | BUTTON_PRESSED
		     | BUTTON_RELEASED
		     | BUTTON_CLICKED
		     | BUTTON_DOUBLE_CLICKED
		     | BUTTON_TRIPLE_CLICKED);

		mouse_activate(SP_PARM, (bool) (result != 0));

		SP_PARM->_mouse_mask = result;
		SP_PARM->_mouse_mask2 = result;

		/*
		 * Make a mask corresponding to the states we will need to
		 * retain (temporarily) while building up the state that the
		 * user asked for.
		 */
		for (b = 1; b <= MAX_BUTTONS; ++b) {
		    if (SP_PARM->_mouse_mask2 & MASK_TRIPLE_CLICK(b))
			SP_PARM->_mouse_mask2 |= MASK_DOUBLE_CLICK(b);
		    if (SP_PARM->_mouse_mask2 & MASK_DOUBLE_CLICK(b))
			SP_PARM->_mouse_mask2 |= MASK_CLICK(b);
		    if (SP_PARM->_mouse_mask2 & MASK_CLICK(b))
			SP_PARM->_mouse_mask2 |= (MASK_PRESS(b) |
						  MASK_RELEASE(b));
		}
	    }
	}
    }
    returnMMask(result);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(mmask_t)
mousemask(mmask_t newmask, mmask_t * oldmask)
{
    return NCURSES_SP_NAME(mousemask) (CURRENT_SCREEN, newmask, oldmask);
}
#endif

NCURSES_EXPORT(bool)
wenclose(const WINDOW *win, int y, int x)
/* check to see if given window encloses given screen location */
{
    bool result = FALSE;

    T((T_CALLED("wenclose(%p,%d,%d)"), (const void *) win, y, x));

    if (win != 0) {
	y -= win->_yoffset;
	result = ((win->_begy <= y &&
		   win->_begx <= x &&
		   (win->_begx + win->_maxx) >= x &&
		   (win->_begy + win->_maxy) >= y) ? TRUE : FALSE);
    }
    returnBool(result);
}

NCURSES_EXPORT(int)
NCURSES_SP_NAME(mouseinterval) (NCURSES_SP_DCLx int maxclick)
/* set the maximum mouse interval within which to recognize a click */
{
    int oldval;

    T((T_CALLED("mouseinterval(%p,%d)"), (void *) SP_PARM, maxclick));

    if (SP_PARM != 0) {
	oldval = SP_PARM->_maxclick;
	if (maxclick >= 0)
	    SP_PARM->_maxclick = maxclick;
    } else {
	oldval = DEFAULT_MAXCLICK;
    }

    returnCode(oldval);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(int)
mouseinterval(int maxclick)
{
    return NCURSES_SP_NAME(mouseinterval) (CURRENT_SCREEN, maxclick);
}
#endif

/* This may be used by other routines to ask for the existence of mouse
   support */
NCURSES_EXPORT(bool)
_nc_has_mouse(SCREEN *sp)
{
    return (((0 == sp) || (sp->_mouse_type == M_NONE)) ? FALSE : TRUE);
}

NCURSES_EXPORT(bool)
NCURSES_SP_NAME(has_mouse) (NCURSES_SP_DCL0)
{
    return _nc_has_mouse(SP_PARM);
}

#if NCURSES_SP_FUNCS
NCURSES_EXPORT(bool)
has_mouse(void)
{
    return _nc_has_mouse(CURRENT_SCREEN);
}
#endif

NCURSES_EXPORT(bool)
wmouse_trafo(const WINDOW *win, int *pY, int *pX, bool to_screen)
{
    bool result = FALSE;

    T((T_CALLED("wmouse_trafo(%p,%p,%p,%d)"),
       (const void *) win,
       (void *) pY,
       (void *) pX,
       to_screen));

    if (win && pY && pX) {
	int y = *pY;
	int x = *pX;

	if (to_screen) {
	    y += win->_begy + win->_yoffset;
	    x += win->_begx;
	    if (wenclose(win, y, x))
		result = TRUE;
	} else {
	    if (wenclose(win, y, x)) {
		y -= (win->_begy + win->_yoffset);
		x -= win->_begx;
		result = TRUE;
	    }
	}
	if (result) {
	    *pX = x;
	    *pY = y;
	}
    }
    returnBool(result);
}
