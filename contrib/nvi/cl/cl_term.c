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
static const char sccsid[] = "$Id: cl_term.c,v 10.35 2015/04/08 02:12:11 zy Exp $";
#endif /* not lint */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/stat.h>

#include <bitstring.h>
#include <errno.h>
#include <limits.h>
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

static int cl_pfmap(SCR *, seq_t, CHAR_T *, size_t, CHAR_T *, size_t);

/*
 * XXX
 * THIS REQUIRES THAT ALL SCREENS SHARE A TERMINAL TYPE.
 */
typedef struct _tklist {
	char	*ts;			/* Key's termcap string. */
	char	*output;		/* Corresponding vi command. */
	char	*name;			/* Name. */
	u_char	 value;			/* Special value (for lookup). */
} TKLIST;
static TKLIST const c_tklist[] = {	/* Command mappings. */
	{"kil1",	"O",	"insert line"},
	{"kdch1",	"x",	"delete character"},
	{"kcud1",	"j",	"cursor down"},
	{"kel",		"D",	"delete to eol"},
	{"kind",     "\004",	"scroll down"},			/* ^D */
	{"kll",		"$",	"go to eol"},
	{"kend",	"$",	"go to eol"},
	{"khome",	"^",	"go to sol"},
	{"kich1",	"i",	"insert at cursor"},
	{"kdl1",       "dd",	"delete line"},
	{"kcub1",	"h",	"cursor left"},
	{"knp",	     "\006",	"page down"},			/* ^F */
	{"kpp",	     "\002",	"page up"},			/* ^B */
	{"kri",	     "\025",	"scroll up"},			/* ^U */
	{"ked",	       "dG",	"delete to end of screen"},
	{"kcuf1",	"l",	"cursor right"},
	{"kcuu1",	"k",	"cursor up"},
	{NULL},
};
static TKLIST const m1_tklist[] = {	/* Input mappings (lookup). */
	{NULL},
};
static TKLIST const m2_tklist[] = {	/* Input mappings (set or delete). */
	{"kcud1",  "\033ja",	"cursor down"},			/* ^[ja */
	{"kcub1",  "\033ha",	"cursor left"},			/* ^[ha */
	{"kcuu1",  "\033ka",	"cursor up"},			/* ^[ka */
	{"kcuf1",  "\033la",	"cursor right"},		/* ^[la */
	{NULL},
};

/*
 * cl_term_init --
 *	Initialize the special keys defined by the termcap/terminfo entry.
 *
 * PUBLIC: int cl_term_init(SCR *);
 */
int
cl_term_init(SCR *sp)
{
	KEYLIST *kp;
	SEQ *qp;
	TKLIST const *tkp;
	char *t;
	CHAR_T name[60];
	CHAR_T output[5];
	CHAR_T ts[20];
	CHAR_T *wp;
	size_t wlen;

	/* Command mappings. */
	for (tkp = c_tklist; tkp->name != NULL; ++tkp) {
		if ((t = tigetstr(tkp->ts)) == NULL || t == (char *)-1)
			continue;
		CHAR2INT(sp, tkp->name, strlen(tkp->name), wp, wlen);
		MEMCPY(name, wp, wlen);
		CHAR2INT(sp, t, strlen(t), wp, wlen);
		MEMCPY(ts, wp, wlen);
		CHAR2INT(sp, tkp->output, strlen(tkp->output), wp, wlen);
		MEMCPY(output, wp, wlen);
		if (seq_set(sp, name, strlen(tkp->name), ts, strlen(t),
		    output, strlen(tkp->output), SEQ_COMMAND,
		    SEQ_NOOVERWRITE | SEQ_SCREEN))
			return (1);
	}

	/* Input mappings needing to be looked up. */
	for (tkp = m1_tklist; tkp->name != NULL; ++tkp) {
		if ((t = tigetstr(tkp->ts)) == NULL || t == (char *)-1)
			continue;
		for (kp = keylist;; ++kp)
			if (kp->value == tkp->value)
				break;
		if (kp == NULL)
			continue;
		CHAR2INT(sp, tkp->name, strlen(tkp->name), wp, wlen);
		MEMCPY(name, wp, wlen);
		CHAR2INT(sp, t, strlen(t), wp, wlen);
		MEMCPY(ts, wp, wlen);
		output[0] = (UCHAR_T)kp->ch;
		if (seq_set(sp, name, strlen(tkp->name), ts, strlen(t),
		    output, 1, SEQ_INPUT, SEQ_NOOVERWRITE | SEQ_SCREEN))
			return (1);
	}

	/* Input mappings that are already set or are text deletions. */
	for (tkp = m2_tklist; tkp->name != NULL; ++tkp) {
		if ((t = tigetstr(tkp->ts)) == NULL || t == (char *)-1)
			continue;
		/*
		 * !!!
		 * Some terminals' <cursor_left> keys send single <backspace>
		 * characters.  This is okay in command mapping, but not okay
		 * in input mapping.  That combination is the only one we'll
		 * ever see, hopefully, so kluge it here for now.
		 */
		if (!strcmp(t, "\b"))
			continue;
		if (tkp->output == NULL) {
			CHAR2INT(sp, tkp->name, strlen(tkp->name), wp, wlen);
			MEMCPY(name, wp, wlen);
			CHAR2INT(sp, t, strlen(t), wp, wlen);
			MEMCPY(ts, wp, wlen);
			if (seq_set(sp, name, strlen(tkp->name),
			    ts, strlen(t), NULL, 0,
			    SEQ_INPUT, SEQ_NOOVERWRITE | SEQ_SCREEN))
				return (1);
		} else {
			CHAR2INT(sp, tkp->name, strlen(tkp->name), wp, wlen);
			MEMCPY(name, wp, wlen);
			CHAR2INT(sp, t, strlen(t), wp, wlen);
			MEMCPY(ts, wp, wlen);
			CHAR2INT(sp, tkp->output, strlen(tkp->output), wp, wlen);
			MEMCPY(output, wp, wlen);
			if (seq_set(sp, name, strlen(tkp->name),
			    ts, strlen(t), output, strlen(tkp->output),
			    SEQ_INPUT, SEQ_NOOVERWRITE | SEQ_SCREEN))
				return (1);
		}
	}

	/*
	 * Rework any function key mappings that were set before the
	 * screen was initialized.
	 */
	SLIST_FOREACH(qp, sp->gp->seqq, q)
		if (F_ISSET(qp, SEQ_FUNCMAP))
			(void)cl_pfmap(sp, qp->stype,
			    qp->input, qp->ilen, qp->output, qp->olen);
	return (0);
}

/*
 * cl_term_end --
 *	End the special keys defined by the termcap/terminfo entry.
 *
 * PUBLIC: int cl_term_end(GS *);
 */
int
cl_term_end(GS *gp)
{
	SEQ *qp, *nqp, *pre_qp = NULL;

	/* Delete screen specific mappings. */
	SLIST_FOREACH_SAFE(qp, gp->seqq, q, nqp)
		if (F_ISSET(qp, SEQ_SCREEN)) {
			if (qp == SLIST_FIRST(gp->seqq))
				SLIST_REMOVE_HEAD(gp->seqq, q);
			else
				SLIST_REMOVE_AFTER(pre_qp, q);
			(void)seq_free(qp);
		} else
			pre_qp = qp;
	return (0);
}

/*
 * cl_fmap --
 *	Map a function key.
 *
 * PUBLIC: int cl_fmap(SCR *, seq_t, CHAR_T *, size_t, CHAR_T *, size_t);
 */
int
cl_fmap(SCR *sp, seq_t stype, CHAR_T *from, size_t flen, CHAR_T *to, size_t tlen)
{
	/* Ignore until the screen is running, do the real work then. */
	if (F_ISSET(sp, SC_VI) && !F_ISSET(sp, SC_SCR_VI))
		return (0);
	if (F_ISSET(sp, SC_EX) && !F_ISSET(sp, SC_SCR_EX))
		return (0);

	return (cl_pfmap(sp, stype, from, flen, to, tlen));
}

/*
 * cl_pfmap --
 *	Map a function key (private version).
 */
static int
cl_pfmap(SCR *sp, seq_t stype, CHAR_T *from, size_t flen, CHAR_T *to, size_t tlen)
{
	size_t nlen;
	char *p;
	char name[64];
	CHAR_T keyname[64];
	CHAR_T ts[20];
	CHAR_T *wp;
	size_t wlen;

	(void)snprintf(name, sizeof(name), "kf%d", 
			(int)STRTOL(from+1,NULL,10));
	if ((p = tigetstr(name)) == NULL ||
	    p == (char *)-1 || strlen(p) == 0)
		p = NULL;
	if (p == NULL) {
		msgq_wstr(sp, M_ERR, from, "233|This terminal has no %s key");
		return (1);
	}

	nlen = SPRINTF(keyname,
	    SIZE(keyname), L("function key %d"), 
			(int)STRTOL(from+1,NULL,10));
	CHAR2INT(sp, p, strlen(p), wp, wlen);
	MEMCPY(ts, wp, wlen);
	return (seq_set(sp, keyname, nlen,
	    ts, strlen(p), to, tlen, stype, SEQ_NOOVERWRITE | SEQ_SCREEN));
}

/*
 * cl_optchange --
 *	Curses screen specific "option changed" routine.
 *
 * PUBLIC: int cl_optchange(SCR *, int, char *, u_long *);
 */
int
cl_optchange(SCR *sp, int opt, char *str, u_long *valp)
{
	CL_PRIVATE *clp;

	clp = CLP(sp);

	switch (opt) {
	case O_TERM:
		F_CLR(sp, SC_SCR_EX | SC_SCR_VI);
		/* FALLTHROUGH */
	case O_COLUMNS:
	case O_LINES:
		/*
		 * Changing the terminal type requires that we reinitialize
		 * curses, while resizing does not.
		 */
		F_SET(sp->gp, G_SRESTART);
		break;
	case O_MESG:
		(void)cl_omesg(sp, clp, *valp);
		break;
	case O_WINDOWNAME:
		if (*valp) {
			F_SET(clp, CL_RENAME_OK);

			/*
			 * If the screen is live, i.e. we're not reading the
			 * .exrc file, update the window.
			 */
			if (sp->frp != NULL && sp->frp->name != NULL)
				(void)cl_rename(sp, sp->frp->name, 1);
		} else {
			F_CLR(clp, CL_RENAME_OK);

			(void)cl_rename(sp, NULL, 0);
		}
		break;
	}
	return (0);
}

/*
 * cl_omesg --
 *	Turn the tty write permission on or off.
 *
 * PUBLIC: int cl_omesg(SCR *, CL_PRIVATE *, int);
 */
int
cl_omesg(SCR *sp, CL_PRIVATE *clp, int on)
{
	struct stat sb;
	char *tty;

	/* Find the tty, get the current permissions. */
	if ((tty = ttyname(STDERR_FILENO)) == NULL) {
		if (sp != NULL)
			msgq(sp, M_SYSERR, "stderr");
		return (1);
	}
	if (stat(tty, &sb) < 0) {
		if (sp != NULL)
			msgq(sp, M_SYSERR, "%s", tty);
		return (1);
	}

	/* Save the original status if it's unknown. */
	if (clp->tgw == TGW_UNKNOWN)
		clp->tgw = sb.st_mode & S_IWGRP ? TGW_SET : TGW_UNSET;

	/* Toggle the permissions. */
	if (on) {
		if (chmod(tty, sb.st_mode | S_IWGRP) < 0) {
			if (sp != NULL)
				msgq(sp, M_SYSERR,
				    "046|messages not turned on: %s", tty);
			return (1);
		}
	} else
		if (chmod(tty, sb.st_mode & ~S_IWGRP) < 0) {
			if (sp != NULL)
				msgq(sp, M_SYSERR,
				    "045|messages not turned off: %s", tty);
			return (1);
		}
	return (0);
}

/*
 * cl_ssize --
 *	Return the terminal size.
 *
 * PUBLIC: int cl_ssize(SCR *, int, size_t *, size_t *, int *);
 */
int
cl_ssize(SCR *sp, int sigwinch, size_t *rowp, size_t *colp, int *changedp)
{
	struct winsize win;
	size_t col, row;
	int rval;
	char *p;

	/* Assume it's changed. */
	if (changedp != NULL)
		*changedp = 1;

	/*
	 * !!!
	 * sp may be NULL.
	 *
	 * Get the screen rows and columns.  If the values are wrong, it's
	 * not a big deal -- as soon as the user sets them explicitly the
	 * environment will be set and the screen package will use the new
	 * values.
	 *
	 * Try TIOCGWINSZ.
	 */
	row = col = 0;
	if (ioctl(STDERR_FILENO, TIOCGWINSZ, &win) != -1) {
		row = win.ws_row;
		col = win.ws_col;
	}
	/* If here because of suspend or a signal, only trust TIOCGWINSZ. */
	if (sigwinch) {
		/*
		 * Somebody didn't get TIOCGWINSZ right, or has suspend
		 * without window resizing support.  The user just lost,
		 * but there's nothing we can do.
		 */
		if (row == 0 || col == 0) {
			if (changedp != NULL)
				*changedp = 0;
			return (0);
		}

		/*
		 * SunOS systems deliver SIGWINCH when windows are uncovered
		 * as well as when they change size.  In addition, we call
		 * here when continuing after being suspended since the window
		 * may have changed size.  Since we don't want to background
		 * all of the screens just because the window was uncovered,
		 * ignore the signal if there's no change.
		 */
		if (sp != NULL &&
		    row == O_VAL(sp, O_LINES) && col == O_VAL(sp, O_COLUMNS)) {
			if (changedp != NULL)
				*changedp = 0;
			return (0);
		}

		if (rowp != NULL)
			*rowp = row;
		if (colp != NULL)
			*colp = col;
		return (0);
	}

	/*
	 * !!!
	 * If TIOCGWINSZ failed, or had entries of 0, try termcap.  This
	 * routine is called before any termcap or terminal information
	 * has been set up.  If there's no TERM environmental variable set,
	 * let it go, at least ex can run.
	 */
	if (row == 0 || col == 0) {
		if ((p = getenv("TERM")) == NULL)
			goto noterm;
		if (row == 0)
			if ((rval = tigetnum("lines")) < 0)
				msgq(sp, M_SYSERR, "tigetnum: lines");
			else
				row = rval;
		if (col == 0)
			if ((rval = tigetnum("cols")) < 0)
				msgq(sp, M_SYSERR, "tigetnum: cols");
			else
				col = rval;
	}

	/* If nothing else, well, it's probably a VT100. */
noterm:	if (row == 0)
		row = 24;
	if (col == 0)
		col = 80;

	/*
	 * !!!
	 * POSIX 1003.2 requires the environment to override everything.
	 * Often, people can get nvi to stop messing up their screen by
	 * deleting the LINES and COLUMNS environment variables from their
	 * dot-files.
	 */
	if ((p = getenv("LINES")) != NULL)
		row = strtol(p, NULL, 10);
	if ((p = getenv("COLUMNS")) != NULL)
		col = strtol(p, NULL, 10);

	if (rowp != NULL)
		*rowp = row;
	if (colp != NULL)
		*colp = col;
	return (0);
}

/*
 * cl_putchar --
 *	Function version of putchar, for tputs.
 *
 * PUBLIC: int cl_putchar(int);
 */
int
cl_putchar(int ch)
{
	return (putchar(ch));
}
