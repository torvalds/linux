/*
 *  $Id: arrows.c,v 1.52 2018/06/18 22:10:54 tom Exp $
 *
 *  arrows.c -- draw arrows to indicate end-of-range for lists
 *
 *  Copyright 2000-2013,2018	Thomas E. Dickey
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
 */

#include <dialog.h>

#ifdef USE_WIDE_CURSES
#if defined(CURSES_WACS_ARRAY) && !defined(CURSES_WACS_SYMBOLS)
/* workaround for NetBSD 5.1 curses */
#undef WACS_DARROW
#undef WACS_UARROW
#define WACS_DARROW &(CURSES_WACS_ARRAY['.'])
#define WACS_UARROW &(CURSES_WACS_ARRAY['-'])
#endif
#define add_acs(win, code) wadd_wch(win, W ## code)
#else
#define add_acs(win, code) waddch(win, dlg_boxchar(code))
#endif

/* size of decorations */
#define ON_LEFT 4
#define ON_RIGHT 3

#ifdef HAVE_COLOR
static chtype
merge_colors(chtype foreground, chtype background)
{
    chtype result = foreground;
    if ((foreground & A_COLOR) != (background & A_COLOR)) {
	short fg_f, bg_f;
	short fg_b, bg_b;
	short fg_pair = (short) PAIR_NUMBER(foreground);
	short bg_pair = (short) PAIR_NUMBER(background);

	if (pair_content(fg_pair, &fg_f, &bg_f) != ERR
	    && pair_content(bg_pair, &fg_b, &bg_b) != ERR) {
	    result &= ~A_COLOR;
	    result |= dlg_color_pair(fg_f, bg_b);
	}
    }
    return result;
}
#else
#define merge_colors(f,b) (f)
#endif

/*
 * If we have help-line text, e.g., from "--hline", draw it between the other
 * decorations at the bottom of the dialog window.
 */
void
dlg_draw_helpline(WINDOW *win, bool decorations)
{
    int cur_x, cur_y;
    int bottom;

    if (dialog_vars.help_line != 0
	&& dialog_vars.help_line[0] != 0
	&& (bottom = getmaxy(win) - 1) > 0) {
	chtype attr = A_NORMAL;
	int cols = dlg_count_columns(dialog_vars.help_line);
	int other = decorations ? (ON_LEFT + ON_RIGHT) : 0;
	int avail = (getmaxx(win) - other - 2);
	int limit = dlg_count_real_columns(dialog_vars.help_line) + 2;

	if (limit < avail) {
	    getyx(win, cur_y, cur_x);
	    other = decorations ? ON_LEFT : 0;
	    (void) wmove(win, bottom, other + (avail - limit) / 2);
	    waddch(win, '[');
	    dlg_print_text(win, dialog_vars.help_line, cols, &attr);
	    waddch(win, ']');
	    wmove(win, cur_y, cur_x);
	}
    }
}

void
dlg_draw_arrows2(WINDOW *win,
		 int top_arrow,
		 int bottom_arrow,
		 int x,
		 int top,
		 int bottom,
		 chtype attr,
		 chtype borderattr)
{
    chtype save = dlg_get_attrs(win);
    int cur_x, cur_y;
    int limit_x = getmaxx(win);
    bool draw_top = TRUE;
    bool is_toplevel = (wgetparent(win) == stdscr);

    getyx(win, cur_y, cur_x);

    /*
     * If we're drawing a centered title, do not overwrite with the arrows.
     */
    if (dialog_vars.title && is_toplevel && (top - getbegy(win)) < MARGIN) {
	int have = (limit_x - dlg_count_columns(dialog_vars.title)) / 2;
	int need = x + 5;
	if (need > have)
	    draw_top = FALSE;
    }

    if (draw_top) {
	(void) wmove(win, top, x);
	if (top_arrow) {
	    dlg_attrset(win, merge_colors(uarrow_attr, attr));
	    (void) add_acs(win, ACS_UARROW);
	    (void) waddstr(win, "(-)");
	} else {
	    dlg_attrset(win, attr);
	    (void) whline(win, dlg_boxchar(ACS_HLINE), ON_LEFT);
	}
    }
    mouse_mkbutton(top, x - 1, 6, KEY_PPAGE);

    (void) wmove(win, bottom, x);
    if (bottom_arrow) {
	dlg_attrset(win, merge_colors(darrow_attr, borderattr));
	(void) add_acs(win, ACS_DARROW);
	(void) waddstr(win, "(+)");
    } else {
	dlg_attrset(win, borderattr);
	(void) whline(win, dlg_boxchar(ACS_HLINE), ON_LEFT);
    }
    mouse_mkbutton(bottom, x - 1, 6, KEY_NPAGE);

    (void) wmove(win, cur_y, cur_x);
    wrefresh(win);

    dlg_attrset(win, save);
}

void
dlg_draw_scrollbar(WINDOW *win,
		   long first_data,
		   long this_data,
		   long next_data,
		   long total_data,
		   int left,
		   int right,
		   int top,
		   int bottom,
		   chtype attr,
		   chtype borderattr)
{
    char buffer[80];
    int percent;
    int len;
    int oldy, oldx;

    chtype save = dlg_get_attrs(win);
    int top_arrow = (first_data != 0);
    int bottom_arrow = (next_data < total_data);

    getyx(win, oldy, oldx);

    dlg_draw_helpline(win, TRUE);
    if (bottom_arrow || top_arrow || dialog_state.use_scrollbar) {
	percent = (!total_data
		   ? 100
		   : (int) ((next_data * 100)
			    / total_data));

	if (percent < 0)
	    percent = 0;
	else if (percent > 100)
	    percent = 100;

	dlg_attrset(win, position_indicator_attr);
	(void) sprintf(buffer, "%d%%", percent);
	(void) wmove(win, bottom, right - 7);
	(void) waddstr(win, buffer);
	if ((len = dlg_count_columns(buffer)) < 4) {
	    dlg_attrset(win, border_attr);
	    whline(win, dlg_boxchar(ACS_HLINE), 4 - len);
	}
    }
#define BARSIZE(num) (int) (0.5 + (double) ((all_high * (int) (num)) / (double) total_data))
#define ORDSIZE(num) (int) ((double) ((all_high * (int) (num)) / (double) all_diff))

    if (dialog_state.use_scrollbar) {
	int all_high = (bottom - top - 1);

	this_data = MAX(0, this_data);

	if (total_data > 0 && all_high > 0) {
	    int all_diff = (int) (total_data + 1);
	    int bar_diff = (int) (next_data + 1 - this_data);
	    int bar_high;
	    int bar_y;

	    bar_high = ORDSIZE(bar_diff);
	    if (bar_high <= 0)
		bar_high = 1;

	    if (bar_high < all_high) {
		int bar_last = BARSIZE(next_data);

		wmove(win, top + 1, right);

		dlg_attrset(win, save);
		wvline(win, ACS_VLINE | A_REVERSE, all_high);

		bar_y = ORDSIZE(this_data);
		if (bar_y >= bar_last && bar_y > 0)
		    bar_y = bar_last - 1;
		if (bar_last - bar_y > bar_high && bar_high > 1)
		    ++bar_y;
		bar_last = MIN(bar_last, all_high);

		wmove(win, top + 1 + bar_y, right);

		dlg_attrset(win, position_indicator_attr);
		dlg_attron(win, A_REVERSE);
#if defined(WACS_BLOCK) && defined(NCURSES_VERSION) && defined(USE_WIDE_CURSES)
		wvline_set(win, WACS_BLOCK, bar_last - bar_y);
#else
		wvline(win, ACS_BLOCK, bar_last - bar_y);
#endif
	    }
	}
    }
    dlg_draw_arrows2(win,
		     top_arrow,
		     bottom_arrow,
		     left + ARROWS_COL,
		     top,
		     bottom,
		     attr,
		     borderattr);

    dlg_attrset(win, save);
    wmove(win, oldy, oldx);
}

void
dlg_draw_arrows(WINDOW *win,
		int top_arrow,
		int bottom_arrow,
		int x,
		int top,
		int bottom)
{
    dlg_draw_helpline(win, TRUE);
    dlg_draw_arrows2(win,
		     top_arrow,
		     bottom_arrow,
		     x,
		     top,
		     bottom,
		     menubox_border2_attr,
		     menubox_border_attr);
}
