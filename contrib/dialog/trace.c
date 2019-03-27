/*
 *  $Id: trace.c,v 1.26 2018/06/13 00:06:48 tom Exp $
 *
 *  trace.c -- implements screen-dump and keystroke-logging
 *
 *  Copyright 2007-2017,2018	Thomas E. Dickey
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License, version 2.1
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
 */

#include <dialog.h>

#ifdef HAVE_DLG_TRACE

#ifdef NEED_WCHAR_H
#include <wchar.h>
#endif

#include <dlg_keys.h>
#include <time.h>

#define myFP dialog_state.trace_output

static void
dlg_trace_time(const char *tag)
{
    time_t now = time((time_t *) 0);
    fprintf(myFP, "%s %s", tag, ctime(&now));
}

void
dlg_trace_msg(const char *fmt,...)
{
    if (myFP != 0) {
	va_list ap;
	va_start(ap, fmt);
	vfprintf(myFP, fmt, ap);
	va_end(ap);
	fflush(myFP);
    }
}

void
dlg_trace_2s(const char *name, const char *value)
{
    bool first = TRUE;
    const char *next;
    int left, right = 0;

    if (value == 0)
	value = "<NULL>";

    while (value[right] != '\0') {
	value += right;
	if ((next = strchr(value, '\n')) != 0) {
	    left = (int) (next - value);
	    right = left + 1;
	} else {
	    left = (int) strlen(value);
	    right = left;
	}
	if (first) {
	    first = FALSE;
	    dlg_trace_msg("#%14s=%.*s\n", name, left, value);
	} else {
	    dlg_trace_msg("#+\t\t%.*s\n", left, value);
	}
    }
}

void
dlg_trace_2n(const char *name, int value)
{
    dlg_trace_msg("#\t%7s=%d\n", name, value);
}

void
dlg_trace_win(WINDOW *win)
{
    if (myFP != 0) {
	int y, x;
	int j, k;
	WINDOW *top = wgetparent(win);

	while (top != 0 && top != stdscr) {
	    win = top;
	    top = wgetparent(win);
	}

	if (win != 0) {
	    int rc = getmaxy(win);
	    int cc = getmaxx(win);
	    chtype ch, c2;

	    fprintf(myFP, "window %dx%d at %d,%d\n",
		    rc, cc, getbegy(win), getbegx(win));

	    getyx(win, y, x);
	    for (j = 0; j < rc; ++j) {
		fprintf(myFP, "%3d:", j);
		for (k = 0; k < cc; ++k) {
#ifdef USE_WIDE_CURSES
		    char buffer[80];

		    ch = mvwinch(win, j, k) & (A_CHARTEXT | A_ALTCHARSET);
		    if (ch & A_ALTCHARSET) {
			c2 = dlg_asciibox(ch);
			if (c2 != 0) {
			    ch = c2;
			}
			buffer[0] = (char) ch;
			buffer[1] = '\0';
		    } else {
			cchar_t cch;
			wchar_t *uc;

			if (win_wch(win, &cch) == ERR
			    || (uc = wunctrl((&cch))) == 0
			    || uc[1] != 0
			    || wcwidth(uc[0]) <= 0) {
			    buffer[0] = '.';
			    buffer[1] = '\0';
			} else {
			    mbstate_t state;
			    const wchar_t *ucp = uc;

			    memset(&state, 0, sizeof(state));
			    wcsrtombs(buffer, &ucp, sizeof(buffer), &state);
			    k += wcwidth(uc[0]) - 1;
			}
		    }
		    fputs(buffer, myFP);
#else
		    ch = mvwinch(win, j, k) & (A_CHARTEXT | A_ALTCHARSET);
		    c2 = dlg_asciibox(ch);
		    if (c2 != 0) {
			ch = c2;
		    } else if (unctrl(ch) == 0 || strlen(unctrl(ch)) > 1) {
			ch = '.';
		    }
		    fputc((int) (ch & 0xff), myFP);
#endif
		}
		fputc('\n', myFP);
	    }
	    wmove(win, y, x);
	    fflush(myFP);
	}
    }
}

void
dlg_trace_chr(int ch, int fkey)
{
    static int last_err = 0;

    /*
     * Do not bother to trace ERR's indefinitely, since those are usually due
     * to relatively short polling timeouts.
     */
    if (last_err && !fkey && ch == ERR) {
	++last_err;
    } else if (myFP != 0) {
	const char *fkey_name = "?";

	if (last_err) {
	    fprintf(myFP, "skipped %d ERR's\n", last_err);
	    last_err = 0;
	}

	if (fkey) {
	    if (fkey > KEY_MAX || (fkey_name = keyname(fkey)) == 0) {
#define CASE(name) case name: fkey_name = #name; break
		switch ((DLG_KEYS_ENUM) fkey) {
		    CASE(DLGK_MIN);
		    CASE(DLGK_OK);
		    CASE(DLGK_CANCEL);
		    CASE(DLGK_EXTRA);
		    CASE(DLGK_HELP);
		    CASE(DLGK_ESC);
		    CASE(DLGK_PAGE_FIRST);
		    CASE(DLGK_PAGE_LAST);
		    CASE(DLGK_PAGE_NEXT);
		    CASE(DLGK_PAGE_PREV);
		    CASE(DLGK_ITEM_FIRST);
		    CASE(DLGK_ITEM_LAST);
		    CASE(DLGK_ITEM_NEXT);
		    CASE(DLGK_ITEM_PREV);
		    CASE(DLGK_FIELD_FIRST);
		    CASE(DLGK_FIELD_LAST);
		    CASE(DLGK_FIELD_NEXT);
		    CASE(DLGK_FIELD_PREV);
		    CASE(DLGK_FORM_FIRST);
		    CASE(DLGK_FORM_LAST);
		    CASE(DLGK_FORM_NEXT);
		    CASE(DLGK_FORM_PREV);
		    CASE(DLGK_GRID_UP);
		    CASE(DLGK_GRID_DOWN);
		    CASE(DLGK_GRID_LEFT);
		    CASE(DLGK_GRID_RIGHT);
		    CASE(DLGK_DELETE_LEFT);
		    CASE(DLGK_DELETE_RIGHT);
		    CASE(DLGK_DELETE_ALL);
		    CASE(DLGK_ENTER);
		    CASE(DLGK_BEGIN);
		    CASE(DLGK_FINAL);
		    CASE(DLGK_SELECT);
		    CASE(DLGK_HELPFILE);
		    CASE(DLGK_TRACE);
		    CASE(DLGK_TOGGLE);
		}
	    }
	} else if (ch == ERR) {
	    fkey_name = "ERR";
	    last_err = 1;
	} else {
	    fkey_name = unctrl((chtype) ch);
	    if (fkey_name == 0)
		fkey_name = "UNKNOWN";
	}
	if (ch >= 0) {
	    fprintf(myFP, "chr %s (ch=%#x, fkey=%d)\n", fkey_name, ch, fkey);
	} else {
	    fprintf(myFP, "chr %s (ch=%d, fkey=%d)\n", fkey_name, ch, fkey);
	}
	fflush(myFP);
    }
}

void
dlg_trace(const char *fname)
{
    if (fname != 0) {
	if (myFP == 0) {
	    myFP = fopen(fname, "a");
	    if (myFP != 0) {
		dlg_trace_time("## opened at");
		DLG_TRACE(("## dialog %s\n", dialog_version()));
		DLG_TRACE(("## vile: confmode\n"));
	    }
	}
    } else if (myFP != 0) {
	dlg_trace_time("## closed at");
	fclose(myFP);
	myFP = 0;
    }
}
#else
#undef dlg_trace
extern void dlg_trace(const char *);
void
dlg_trace(const char *fname)
{
    (void) fname;
}
#endif
