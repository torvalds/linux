/*-
 * Copyright (c) 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1993, 1994, 1995, 1996
 *	Keith Bostic.  All rights reserved.
 *
 * See the LICENSE file for redistribution information.
 */

#include "config.h"

#ifndef lint
static const char sccsid[] = "$Id: cl_screen.c,v 10.58 2015/04/08 02:12:11 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <bitstring.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_TERM_H
#include <term.h>
#endif
#include <termios.h>
#include <unistd.h>

#include "../common/common.h"
#include "cl.h"

static int	cl_ex_end(GS *);
static int	cl_ex_init(SCR *);
static void	cl_freecap(CL_PRIVATE *);
static int	cl_vi_end(GS *);
static int	cl_vi_init(SCR *);
static int	cl_putenv(char *, char *, u_long);

/*
 * cl_screen --
 *	Switch screen types.
 *
 * PUBLIC: int cl_screen(SCR *, u_int32_t);
 */
int
cl_screen(SCR *sp, u_int32_t flags)
{
	CL_PRIVATE *clp;
	WINDOW *win;
	GS *gp;

	gp = sp->gp;
	clp = CLP(sp);
	win = CLSP(sp) ? CLSP(sp) : stdscr;

	/* See if the current information is incorrect. */
	if (F_ISSET(gp, G_SRESTART)) {
		if ((!F_ISSET(sp, SC_SCR_EX | SC_SCR_VI) ||
		     resizeterm(O_VAL(sp, O_LINES), O_VAL(sp, O_COLUMNS))) &&
		    cl_quit(gp))
			return (1);
		F_CLR(gp, G_SRESTART);
	}
	
	/* See if we're already in the right mode. */
	if ((LF_ISSET(SC_EX) && F_ISSET(sp, SC_SCR_EX)) ||
	    (LF_ISSET(SC_VI) && F_ISSET(sp, SC_SCR_VI)))
		return (0);

	/*
	 * Fake leaving ex mode.
	 *
	 * We don't actually exit ex or vi mode unless forced (e.g. by a window
	 * size change).  This is because many curses implementations can't be
	 * called twice in a single program.  Plus, it's faster.  If the editor
	 * "leaves" vi to enter ex, when it exits ex we'll just fall back into
	 * vi.
	 */
	if (F_ISSET(sp, SC_SCR_EX))
		F_CLR(sp, SC_SCR_EX);

	/*
	 * Fake leaving vi mode.
	 *
	 * Clear out the rest of the screen if we're in the middle of a split
	 * screen.  Move to the last line in the current screen -- this makes
	 * terminal scrolling happen naturally.  Note: *don't* move past the
	 * end of the screen, as there are ex commands (e.g., :read ! cat file)
	 * that don't want to.  Don't clear the info line, its contents may be
	 * valid, e.g. :file|append.
	 */
	if (F_ISSET(sp, SC_SCR_VI)) {
		F_CLR(sp, SC_SCR_VI);

		if (TAILQ_NEXT(sp, q) != NULL) {
			(void)wmove(win, RLNO(sp, sp->rows), 0);
			wclrtobot(win);
		}
		(void)wmove(win, RLNO(sp, sp->rows) - 1, 0);
		wrefresh(win);
	}

	/* Enter the requested mode. */
	if (LF_ISSET(SC_EX)) {
		if (cl_ex_init(sp))
			return (1);
		F_SET(clp, CL_IN_EX | CL_SCR_EX_INIT);

		/*
		 * If doing an ex screen for ex mode, move to the last line
		 * on the screen.
		 */
		if (F_ISSET(sp, SC_EX) && clp->cup != NULL)
			tputs(tgoto(clp->cup,
			    0, O_VAL(sp, O_LINES) - 1), 1, cl_putchar);
	} else {
		if (cl_vi_init(sp))
			return (1);
		F_CLR(clp, CL_IN_EX);
		F_SET(clp, CL_SCR_VI_INIT);
	}
	return (0);
}

/*
 * cl_quit --
 *	Shutdown the screens.
 *
 * PUBLIC: int cl_quit(GS *);
 */
int
cl_quit(GS *gp)
{
	CL_PRIVATE *clp;
	int rval;

	rval = 0;
	clp = GCLP(gp);

	/*
	 * If we weren't really running, ignore it.  This happens if the
	 * screen changes size before we've called curses.
	 */
	if (!F_ISSET(clp, CL_SCR_EX_INIT | CL_SCR_VI_INIT))
		return (0);

	/* Clean up the terminal mappings. */
	if (cl_term_end(gp))
		rval = 1;

	/* Really leave vi mode. */
	if (F_ISSET(clp, CL_STDIN_TTY) &&
	    F_ISSET(clp, CL_SCR_VI_INIT) && cl_vi_end(gp))
		rval = 1;

	/* Really leave ex mode. */
	if (F_ISSET(clp, CL_STDIN_TTY) &&
	    F_ISSET(clp, CL_SCR_EX_INIT) && cl_ex_end(gp))
		rval = 1;

	/*
	 * If we were running ex when we quit, or we're using an implementation
	 * of curses where endwin() doesn't get this right, restore the original
	 * terminal modes.
	 *
	 * XXX
	 * We always do this because it's too hard to figure out what curses
	 * implementations get it wrong.  It may discard type-ahead characters
	 * from the tty queue.
	 */
	(void)tcsetattr(STDIN_FILENO, TCSADRAIN | TCSASOFT, &clp->orig);

	F_CLR(clp, CL_SCR_EX_INIT | CL_SCR_VI_INIT);
	return (rval);
}

/*
 * cl_vi_init --
 *	Initialize the curses vi screen.
 */
static int
cl_vi_init(SCR *sp)
{
	CL_PRIVATE *clp;
	GS *gp;
	char *o_cols, *o_lines, *o_term, *ttype;

	gp = sp->gp;
	clp = CLP(sp);

	/* If already initialized, just set the terminal modes. */
	if (F_ISSET(clp, CL_SCR_VI_INIT))
		goto fast;

	/* Curses vi always reads from (and writes to) a terminal. */
	if (!F_ISSET(clp, CL_STDIN_TTY) || !isatty(STDOUT_FILENO)) {
		msgq(sp, M_ERR,
		    "016|Vi's standard input and output must be a terminal");
		return (1);
	}

	/* We'll need a terminal type. */
	if (opts_empty(sp, O_TERM, 0))
		return (1);
	ttype = O_STR(sp, O_TERM);

	/*
	 * XXX
	 * Changing the row/column and terminal values is done by putting them
	 * into the environment, which is then read by curses.  What this loses
	 * in ugliness, it makes up for in stupidity.  We can't simply put the
	 * values into the environment ourselves, because in the presence of a
	 * kernel mechanism for returning the window size, entering values into
	 * the environment will screw up future screen resizing events, e.g. if
	 * the user enters a :shell command and then resizes their window.  So,
	 * if they weren't already in the environment, we make sure to delete
	 * them immediately after setting them.
	 *
	 * XXX
	 * Putting the TERM variable into the environment is necessary, even
	 * though we're using newterm() here.  We may be using initscr() as
	 * the underlying function.
	 */
	o_term = getenv("TERM");
	cl_putenv("TERM", ttype, 0);
	o_lines = getenv("LINES");
	cl_putenv("LINES", NULL, (u_long)O_VAL(sp, O_LINES));
	o_cols = getenv("COLUMNS");
	cl_putenv("COLUMNS", NULL, (u_long)O_VAL(sp, O_COLUMNS));

	/*
	 * The terminal is aways initialized, either in `main`, or by a
	 * previous call to newterm(3X).
	 */
	(void)del_curterm(cur_term);

	/*
	 * We never have more than one SCREEN at a time, so set_term(NULL) will
	 * give us the last SCREEN.
	 */
	errno = 0;
	if (newterm(ttype, stdout, stdin) == NULL) {
		if (errno)
			msgq(sp, M_SYSERR, "%s", ttype);
		else
			msgq(sp, M_ERR, "%s: unknown terminal type", ttype);
		return (1);
	}

	if (o_term == NULL)
		unsetenv("TERM");
	if (o_lines == NULL)
		unsetenv("LINES");
	if (o_cols == NULL)
		unsetenv("COLUMNS");

	/*
	 * XXX
	 * Someone got let out alone without adult supervision -- the SunOS
	 * newterm resets the signal handlers.  There's a race, but it's not
	 * worth closing.
	 */
	(void)sig_init(sp->gp, sp);

	/*
	 * We use raw mode.  What we want is 8-bit clean, however, signals
	 * and flow control should continue to work.  Admittedly, it sounds
	 * like cbreak, but it isn't.  Using cbreak() can get you additional
	 * things like IEXTEN, which turns on flags like DISCARD and LNEXT.
	 *
	 * !!!
	 * If raw isn't turning off echo and newlines, something's wrong.
	 * However, it shouldn't hurt.
	 */
	noecho();			/* No character echo. */
	nonl();				/* No CR/NL translation. */
	raw();				/* 8-bit clean. */
	idlok(stdscr, 1);		/* Use hardware insert/delete line. */

	/* Put the cursor keys into application mode. */
	(void)keypad(stdscr, TRUE);

	/*
	 * XXX
	 * The screen TI sequence just got sent.  See the comment in
	 * cl_funcs.c:cl_attr().
	 */
	clp->ti_te = TI_SENT;

	/*
	 * XXX
	 * Historic implementations of curses handled SIGTSTP signals
	 * in one of three ways.  They either:
	 *
	 *	1: Set their own handler, regardless.
	 *	2: Did not set a handler if a handler was already installed.
	 *	3: Set their own handler, but then called any previously set
	 *	   handler after completing their own cleanup.
	 *
	 * We don't try and figure out which behavior is in place, we force
	 * it to SIG_DFL after initializing the curses interface, which means
	 * that curses isn't going to take the signal.  Since curses isn't
	 * reentrant (i.e., the whole curses SIGTSTP interface is a fantasy),
	 * we're doing The Right Thing.
	 */
	(void)signal(SIGTSTP, SIG_DFL);

	/*
	 * If flow control was on, turn it back on.  Turn signals on.  ISIG
	 * turns on VINTR, VQUIT, VDSUSP and VSUSP.   The main curses code
	 * already installed a handler for VINTR.  We're going to disable the
	 * other three.
	 *
	 * XXX
	 * We want to use ^Y as a vi scrolling command.  If the user has the
	 * DSUSP character set to ^Y (common practice) clean it up.  As it's
	 * equally possible that the user has VDSUSP set to 'a', we disable
	 * it regardless.  It doesn't make much sense to suspend vi at read,
	 * so I don't think anyone will care.  Alternatively, we could look
	 * it up in the table of legal command characters and turn it off if
	 * it matches one.  VDSUSP wasn't in POSIX 1003.1-1990, so we test for
	 * it.
	 *
	 * XXX
	 * We don't check to see if the user had signals enabled originally.
	 * If they didn't, it's unclear what we're supposed to do here, but
	 * it's also pretty unlikely.
	 */
	if (tcgetattr(STDIN_FILENO, &clp->vi_enter)) {
		msgq(sp, M_SYSERR, "tcgetattr");
		goto err;
	}
	if (clp->orig.c_iflag & IXON)
		clp->vi_enter.c_iflag |= IXON;
	if (clp->orig.c_iflag & IXOFF)
		clp->vi_enter.c_iflag |= IXOFF;

	clp->vi_enter.c_lflag |= ISIG;
#ifdef VDSUSP
	clp->vi_enter.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
	clp->vi_enter.c_cc[VQUIT] = _POSIX_VDISABLE;
	clp->vi_enter.c_cc[VSUSP] = _POSIX_VDISABLE;

	/*
	 * XXX
	 * OSF/1 doesn't turn off the <discard>, <literal-next> or <status>
	 * characters when curses switches into raw mode.  It should be OK
	 * to do it explicitly for everyone.
	 */
#ifdef VDISCARD
	clp->vi_enter.c_cc[VDISCARD] = _POSIX_VDISABLE;
#endif
#ifdef VLNEXT
	clp->vi_enter.c_cc[VLNEXT] = _POSIX_VDISABLE;
#endif
#ifdef VSTATUS
	clp->vi_enter.c_cc[VSTATUS] = _POSIX_VDISABLE;
#endif

	/* Initialize terminal based information. */
	if (cl_term_init(sp))
		goto err;

fast:	/* Set the terminal modes. */
	if (tcsetattr(STDIN_FILENO, TCSASOFT | TCSADRAIN, &clp->vi_enter)) {
		if (errno == EINTR)
			goto fast;
		msgq(sp, M_SYSERR, "tcsetattr");
err:		(void)cl_vi_end(sp->gp);
		return (1);
	}
	return (0);
}

/*
 * cl_vi_end --
 *	Shutdown the vi screen.
 */
static int
cl_vi_end(GS *gp)
{
	CL_PRIVATE *clp;

	clp = GCLP(gp);

	/* Restore the cursor keys to normal mode. */
	(void)keypad(stdscr, FALSE);

	/*
	 * If we were running vi when we quit, scroll the screen up a single
	 * line so we don't lose any information.
	 *
	 * Move to the bottom of the window (some endwin implementations don't
	 * do this for you).
	 */
	if (!F_ISSET(clp, CL_IN_EX)) {
		(void)move(0, 0);
		(void)deleteln();
		(void)move(LINES - 1, 0);
		(void)refresh();
	}

	cl_freecap(clp);

	/* End curses window. */
	(void)endwin();

	/* Free the SCREEN created by newterm(3X). */
	delscreen(set_term(NULL));

	/*
	 * XXX
	 * The screen TE sequence just got sent.  See the comment in
	 * cl_funcs.c:cl_attr().
	 */
	clp->ti_te = TE_SENT;

	return (0);
}

/*
 * cl_ex_init --
 *	Initialize the ex screen.
 */
static int
cl_ex_init(SCR *sp)
{
	CL_PRIVATE *clp;

	clp = CLP(sp);

	/* If already initialized, just set the terminal modes. */
	if (F_ISSET(clp, CL_SCR_EX_INIT))
		goto fast;

	/* If not reading from a file, we're done. */
	if (!F_ISSET(clp, CL_STDIN_TTY))
		return (0);

	/* Get the ex termcap/terminfo strings. */
	(void)cl_getcap(sp, "cup", &clp->cup);
	(void)cl_getcap(sp, "smso", &clp->smso);
	(void)cl_getcap(sp, "rmso", &clp->rmso);
	(void)cl_getcap(sp, "el", &clp->el);
	(void)cl_getcap(sp, "cuu1", &clp->cuu1);

	/* Enter_standout_mode and exit_standout_mode are paired. */
	if (clp->smso == NULL || clp->rmso == NULL) {
		if (clp->smso != NULL) {
			free(clp->smso);
			clp->smso = NULL;
		}
		if (clp->rmso != NULL) {
			free(clp->rmso);
			clp->rmso = NULL;
		}
	}

	/*
	 * Turn on canonical mode, with normal input and output processing.
	 * Start with the original terminal settings as the user probably
	 * had them (including any local extensions) set correctly for the
	 * current terminal.
	 *
	 * !!!
	 * We can't get everything that we need portably; for example, ONLCR,
	 * mapping <newline> to <carriage-return> on output isn't required
	 * by POSIX 1003.1b-1993.  If this turns out to be a problem, then
	 * we'll either have to play some games on the mapping, or we'll have
	 * to make all ex printf's output \r\n instead of \n.
	 */
	clp->ex_enter = clp->orig;
	clp->ex_enter.c_lflag  |= ECHO | ECHOE | ECHOK | ICANON | IEXTEN | ISIG;
#ifdef ECHOCTL
	clp->ex_enter.c_lflag |= ECHOCTL;
#endif
#ifdef ECHOKE
	clp->ex_enter.c_lflag |= ECHOKE;
#endif
	clp->ex_enter.c_iflag |= ICRNL;
	clp->ex_enter.c_oflag |= OPOST;
#ifdef ONLCR
	clp->ex_enter.c_oflag |= ONLCR;
#endif

fast:	if (tcsetattr(STDIN_FILENO, TCSADRAIN | TCSASOFT, &clp->ex_enter)) {
		if (errno == EINTR)
			goto fast;
		msgq(sp, M_SYSERR, "tcsetattr");
		return (1);
	}
	return (0);
}

/*
 * cl_ex_end --
 *	Shutdown the ex screen.
 */
static int
cl_ex_end(GS *gp)
{
	CL_PRIVATE *clp;

	clp = GCLP(gp);

	cl_freecap(clp);

	return (0);
}

/*
 * cl_getcap --
 *	Retrieve termcap/terminfo strings.
 *
 * PUBLIC: int cl_getcap(SCR *, char *, char **);
 */
int
cl_getcap(SCR *sp, char *name, char **elementp)
{
	size_t len;
	char *t;

	if ((t = tigetstr(name)) != NULL &&
	    t != (char *)-1 && (len = strlen(t)) != 0) {
		MALLOC_RET(sp, *elementp, char *, len + 1);
		memmove(*elementp, t, len + 1);
	}
	return (0);
}

/*
 * cl_freecap --
 *	Free any allocated termcap/terminfo strings.
 */
static void
cl_freecap(CL_PRIVATE *clp)
{
	if (clp->el != NULL) {
		free(clp->el);
		clp->el = NULL;
	}
	if (clp->cup != NULL) {
		free(clp->cup);
		clp->cup = NULL;
	}
	if (clp->cuu1 != NULL) {
		free(clp->cuu1);
		clp->cuu1 = NULL;
	}
	if (clp->rmso != NULL) {
		free(clp->rmso);
		clp->rmso = NULL;
	}
	if (clp->smso != NULL) {
		free(clp->smso);
		clp->smso = NULL;
	}
	/* Required by libcursesw :) */
	if (clp->cw.bp1.c != NULL) {
		free(clp->cw.bp1.c);
		clp->cw.bp1.c = NULL;
		clp->cw.blen1 = 0;
	}
}

/*
 * cl_putenv --
 *	Put a value into the environment.
 */
static int
cl_putenv(char *name, char *str, u_long value)
{
	char buf[40];

	if (str == NULL) {
		(void)snprintf(buf, sizeof(buf), "%lu", value);
		return (setenv(name, buf, 1));
	} else
		return (setenv(name, str, 1));
}
