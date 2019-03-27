/*
 *  $Id: progressbox.c,v 1.47 2018/06/21 09:14:47 tom Exp $
 *
 *  progressbox.c -- implements the progress box
 *
 *  Copyright 2006-2014,2018	Thomas E. Dickey
 *  Copyright 2005		Valery Reznic
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as
 *  published by the Free Software Foundation; either version 2.1 of the
 *  License, or (at your option) any later version.
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
#include <dlg_keys.h>

#ifdef KEY_RESIZE
#include <errno.h>
#endif

#define MIN_HIGH (4)
#define MIN_WIDE (10 + 2 * (2 + MARGIN))

#ifdef KEY_RESIZE
typedef struct _wrote {
    struct _wrote *link;
    char *text;
} WROTE;
#endif

typedef struct {
    DIALOG_CALLBACK obj;
    WINDOW *text;
    char *prompt;
    int high, wide;
    int old_high, old_wide;
    char line[MAX_LEN + 1];
    int is_eof;
#ifdef KEY_RESIZE
    WROTE *wrote;
#endif
} MY_OBJ;

static void
free_obj(MY_OBJ * obj)
{
    dlg_del_window(obj->obj.win);
    free(obj->prompt);
#ifdef KEY_RESIZE
    while (obj->wrote) {
	WROTE *wrote = obj->wrote;
	obj->wrote = wrote->link;
	free(wrote->text);
	free(wrote);
    }
#endif
    free(obj);
}

static void
restart_obj(MY_OBJ * obj)
{
    free(obj->prompt);
    obj->high = obj->old_high;
    obj->wide = obj->old_wide;
    dlg_clear();
    dlg_del_window(obj->obj.win);
}

static void
start_obj(MY_OBJ * obj, const char *title, const char *cprompt)
{
    int y, x, thigh;
    int i;

    obj->prompt = dlg_strclone(cprompt);
    dlg_tab_correct_str(obj->prompt);
    dlg_auto_size(title, obj->prompt, &obj->high, &obj->wide, MIN_HIGH, MIN_WIDE);

    dlg_print_size(obj->high, obj->wide);
    dlg_ctl_size(obj->high, obj->wide);

    x = dlg_box_x_ordinate(obj->wide);
    y = dlg_box_y_ordinate(obj->high);
    thigh = obj->high - (2 * MARGIN);

    obj->obj.win = dlg_new_window(obj->high, obj->wide, y, x);

    dlg_draw_box2(obj->obj.win,
		  0, 0,
		  obj->high, obj->wide,
		  dialog_attr,
		  border_attr,
		  border2_attr);
    dlg_draw_title(obj->obj.win, title);
    dlg_draw_helpline(obj->obj.win, FALSE);

    if (obj->prompt[0] != '\0') {
	int y2, x2;

	dlg_attrset(obj->obj.win, dialog_attr);
	dlg_print_autowrap(obj->obj.win, obj->prompt, obj->high, obj->wide);
	getyx(obj->obj.win, y2, x2);
	(void) x2;
	++y2;
	wmove(obj->obj.win, y2, MARGIN);
	for (i = 0; i < getmaxx(obj->obj.win) - 2 * MARGIN; i++)
	    (void) waddch(obj->obj.win, dlg_boxchar(ACS_HLINE));
	y += y2;
	thigh -= y2;
    }

    /* Create window for text region, used for scrolling text */
    obj->text = dlg_sub_window(obj->obj.win,
			       thigh,
			       obj->wide - (2 * MARGIN),
			       y + MARGIN,
			       x + MARGIN);

    (void) wrefresh(obj->obj.win);

    (void) wmove(obj->obj.win, getmaxy(obj->text), (MARGIN + 1));
    (void) wnoutrefresh(obj->obj.win);

    dlg_attr_clear(obj->text, getmaxy(obj->text), getmaxx(obj->text), dialog_attr);
}

/*
 * Return current line of text.
 */
static char *
get_line(MY_OBJ * obj, int *restart)
{
    FILE *fp = obj->obj.input;
    int col = 0;
    int j, tmpint, ch;
    char *result = obj->line;

    *restart = 0;
    for (;;) {
	ch = getc(fp);
#ifdef KEY_RESIZE
	/* SIGWINCH may have interrupted this - try to ignore if resizable */
	if (ferror(fp)) {
	    switch (errno) {
	    case EINTR:
		clearerr(fp);
		continue;
	    default:
		break;
	    }
	}
#endif
	if (feof(fp) || ferror(fp)) {
	    obj->is_eof = 1;
	    if (!col) {
		result = NULL;
	    }
	    break;
	}
	if (ch == '\n')
	    break;
	if (ch == '\r')
	    break;
	if (col >= MAX_LEN)
	    continue;
	if ((ch == TAB) && (dialog_vars.tab_correct)) {
	    tmpint = dialog_state.tab_len
		- (col % dialog_state.tab_len);
	    for (j = 0; j < tmpint; j++) {
		if (col < MAX_LEN) {
		    obj->line[col] = ' ';
		    ++col;
		} else {
		    break;
		}
	    }
	} else {
	    obj->line[col] = (char) ch;
	    ++col;
	}
    }

    obj->line[col] = '\0';

#ifdef KEY_RESIZE
    if (result != NULL) {
	WINDOW *win = obj->text;
	WROTE *wrote = dlg_calloc(WROTE, 1);

	if (wrote != 0) {
	    wrote->text = dlg_strclone(obj->line);
	    wrote->link = obj->wrote;
	    obj->wrote = wrote;
	}

	nodelay(win, TRUE);
	if ((ch = wgetch(win)) == KEY_RESIZE) {
	    *restart = 1;
	}
	nodelay(win, FALSE);
    }
#endif
    return result;
}

/*
 * Print a new line of text.
 */
static void
print_line(MY_OBJ * obj, const char *line, int row)
{
    int width = obj->wide - (2 * MARGIN);
    int limit = MIN((int) strlen(line), width - 2);

    (void) wmove(obj->text, row, 0);	/* move cursor to correct line */
    wprintw(obj->text, " %.*s", limit, line);
    wclrtoeol(obj->text);
}

#ifdef KEY_RESIZE
static int
wrote_size(MY_OBJ * obj, int want)
{
    int result = 0;
    WROTE *wrote = obj->wrote;
    while (wrote != NULL && want > 0) {
	wrote = wrote->link;
	want--;
	result++;
    }
    return result;
}

static const char *
wrote_data(MY_OBJ * obj, int want)
{
    const char *result = NULL;
    WROTE *wrote = obj->wrote;
    while (wrote != NULL && want > 0) {
	result = wrote->text;
	wrote = wrote->link;
	want--;
    }
    return result;
}

static int
reprint_lines(MY_OBJ * obj, int buttons)
{
    int want = getmaxy(obj->text) - (buttons ? 2 : 0);
    int have = wrote_size(obj, want);
    int n;
    for (n = 0; n < have; ++n) {
	print_line(obj, wrote_data(obj, have - n), n);
    }
    (void) wrefresh(obj->text);
    return have;
}
#endif

static int
pause_for_ok(MY_OBJ * obj, const char *title, const char *cprompt)
{
    /* *INDENT-OFF* */
    static DLG_KEYS_BINDING binding[] = {
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	TRAVERSE_BINDINGS,
	END_KEYS_BINDING
    };
    /* *INDENT-ON* */

    int button;
    int key = 0, fkey;
    int result = DLG_EXIT_UNKNOWN;
    const char **buttons = dlg_ok_label();
    int check;
    bool save_nocancel = dialog_vars.nocancel;
    bool redraw = TRUE;

    dialog_vars.nocancel = TRUE;
    button = dlg_default_button();

#ifdef KEY_RESIZE
  restart:
#endif

    dlg_register_window(obj->obj.win, "progressbox", binding);
    dlg_register_buttons(obj->obj.win, "progressbox", buttons);

    dlg_draw_bottom_box2(obj->obj.win, border_attr, border2_attr, dialog_attr);

    while (result == DLG_EXIT_UNKNOWN) {
	if (redraw) {
	    redraw = FALSE;
	    if (button < 0)
		button = 0;
	    dlg_draw_buttons(obj->obj.win,
			     obj->high - 2, 0,
			     buttons, button,
			     FALSE, obj->wide);
	}

	key = dlg_mouse_wgetch(obj->obj.win, &fkey);
	if (dlg_result_key(key, fkey, &result))
	    break;

	if (!fkey && (check = dlg_char_to_button(key, buttons)) >= 0) {
	    result = dlg_ok_buttoncode(check);
	    break;
	}

	if (fkey) {
	    switch (key) {
	    case DLGK_FIELD_NEXT:
		button = dlg_next_button(buttons, button);
		redraw = TRUE;
		break;
	    case DLGK_FIELD_PREV:
		button = dlg_prev_button(buttons, button);
		redraw = TRUE;
		break;
	    case DLGK_ENTER:
		result = dlg_ok_buttoncode(button);
		break;
#ifdef KEY_RESIZE
	    case KEY_RESIZE:
		dlg_will_resize(obj->obj.win);
		restart_obj(obj);
		start_obj(obj, title, cprompt);
		reprint_lines(obj, TRUE);
		redraw = TRUE;
		goto restart;
#endif
	    default:
		if (is_DLGK_MOUSE(key)) {
		    result = dlg_ok_buttoncode(key - M_EVENT);
		    if (result < 0)
			result = DLG_EXIT_OK;
		} else {
		    beep();
		}
		break;
	    }

	} else {
	    beep();
	}
    }
    dlg_mouse_free_regions();
    dlg_unregister_window(obj->obj.win);

    dialog_vars.nocancel = save_nocancel;
    return result;
}

int
dlg_progressbox(const char *title,
		const char *cprompt,
		int height,
		int width,
		int pauseopt,
		FILE *fp)
{
    int i;
    MY_OBJ *obj;
    int again = 0;
    int toprow = 0;
    int result;

    DLG_TRACE(("# progressbox args:\n"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("message", cprompt);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);
    DLG_TRACE2N("pause", pauseopt);
    DLG_TRACE2N("fp", fp ? fileno(fp) : -1);

    obj = dlg_calloc(MY_OBJ, 1);
    assert_ptr(obj, "dlg_progressbox");
    obj->obj.input = fp;

    obj->high = height;
    obj->wide = width;

#ifdef KEY_RESIZE
    obj->old_high = height;
    obj->old_wide = width;

    curs_set(0);
  restart:
#endif

    start_obj(obj, title, cprompt);
#ifdef KEY_RESIZE
    if (again) {
	toprow = reprint_lines(obj, FALSE);
    }
#endif

    for (i = toprow; get_line(obj, &again); i++) {
#ifdef KEY_RESIZE
	if (again) {
	    dlg_will_resize(obj->obj.win);
	    restart_obj(obj);
	    goto restart;
	}
#endif
	if (i < getmaxy(obj->text)) {
	    print_line(obj, obj->line, i);
	} else {
	    scrollok(obj->text, TRUE);
	    scroll(obj->text);
	    scrollok(obj->text, FALSE);
	    print_line(obj, obj->line, getmaxy(obj->text) - 1);
	}
	(void) wrefresh(obj->text);
	if (obj->is_eof)
	    break;
    }

    dlg_trace_win(obj->obj.win);
    curs_set(1);

    if (pauseopt) {
	int need = 1 + MARGIN;
	int base = getmaxy(obj->text) - need;
	if (i >= base) {
	    i -= base;
	    if (i > need)
		i = need;
	    if (i > 0) {
		scrollok(obj->text, TRUE);
	    }
	    wscrl(obj->text, i);
	}
	(void) wrefresh(obj->text);
	result = pause_for_ok(obj, title, cprompt);
    } else {
	wrefresh(obj->obj.win);
	result = DLG_EXIT_OK;
    }

    free_obj(obj);

    return result;
}

/*
 * Display text from a stdin in a scrolling window.
 */
int
dialog_progressbox(const char *title, const char *cprompt, int height, int width)
{
    int result;
    result = dlg_progressbox(title,
			     cprompt,
			     height,
			     width,
			     FALSE,
			     dialog_state.pipe_input);
    dialog_state.pipe_input = 0;
    return result;
}
