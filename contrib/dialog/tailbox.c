/*
 *  $Id: tailbox.c,v 1.72 2018/06/19 22:57:01 tom Exp $
 *
 *  tailbox.c -- implements the tail box
 *
 *  Copyright 2000-2012,2018	Thomas E. Dickey
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
 *
 *  An earlier version of this program lists as authors
 *	Pasquale De Marco (demarco_p@abramo.it)
 */

#include <dialog.h>
#include <dlg_keys.h>
#include <sys/stat.h>

typedef struct {
    DIALOG_CALLBACK obj;
    WINDOW *text;
    const char **buttons;
    int hscroll;
    int old_hscroll;
    char line[MAX_LEN + 2];
    off_t last_pos;
} MY_OBJ;

/*
 * Return current line of text.
 */
static char *
get_line(MY_OBJ * obj)
{
    FILE *fp = obj->obj.input;
    int col = -(obj->hscroll);
    int j, tmpint, ch;

    do {
	if (((ch = getc(fp)) == EOF) && !feof(fp))
	    dlg_exiterr("Error moving file pointer in get_line().");
	else if (!feof(fp) && (ch != '\n')) {
	    if ((ch == TAB) && (dialog_vars.tab_correct)) {
		tmpint = dialog_state.tab_len
		    - ((col + obj->hscroll) % dialog_state.tab_len);
		for (j = 0; j < tmpint; j++) {
		    if (col >= 0 && col < MAX_LEN)
			obj->line[col] = ' ';
		    ++col;
		}
	    } else {
		if (col >= 0)
		    obj->line[col] = (char) ch;
		++col;
	    }
	    if (col >= MAX_LEN)
		break;
	}
    } while (!feof(fp) && (ch != '\n'));

    if (col < 0)
	col = 0;
    obj->line[col] = '\0';

    return obj->line;
}

/*
 * Print a new line of text.
 */
static void
print_line(MY_OBJ * obj, WINDOW *win, int row, int width)
{
    int i, y, x;
    char *line = get_line(obj);

    (void) wmove(win, row, 0);	/* move cursor to correct line */
    (void) waddch(win, ' ');
    (void) waddnstr(win, line, MIN((int) strlen(line), width - 2));

    getyx(win, y, x);
    (void) y;
    /* Clear 'residue' of previous line */
    for (i = 0; i < width - x; i++)
	(void) waddch(win, ' ');
}

/*
 * Go back 'target' lines in text file.  BUFSIZ has to be in 'size_t' range.
 */
static void
last_lines(MY_OBJ * obj, int target)
{
    FILE *fp = obj->obj.input;
    size_t inx;
    int count = 0;
    char buf[BUFSIZ + 1];
    size_t size_to_read;
    size_t size_as_read;
    long offset = 0;
    long fpos = 0;

    if (fseek(fp, 0L, SEEK_END) == -1
	|| (fpos = ftell(fp)) < 0)
	dlg_exiterr("Error moving file pointer in last_lines().");

    if (fpos != 0) {
	++target;
	for (;;) {
	    if (fpos >= BUFSIZ) {
		size_to_read = BUFSIZ;
	    } else {
		size_to_read = (size_t) fpos;
	    }
	    fpos = fpos - (long) size_to_read;
	    if (fseek(fp, fpos, SEEK_SET) == -1)
		dlg_exiterr("Error moving file pointer in last_lines().");
	    size_as_read = fread(buf, sizeof(char), size_to_read, fp);
	    if (ferror(fp))
		dlg_exiterr("Error reading file in last_lines().");

	    if (size_as_read == 0) {
		fpos = 0;
		offset = 0;
		break;
	    }

	    offset += (long) size_as_read;
	    for (inx = size_as_read - 1; inx != 0; --inx) {
		if (buf[inx] == '\n') {
		    if (++count > target)
			break;
		    offset = (long) (inx + 1);
		}
	    }

	    if (count > target) {
		break;
	    } else if (fpos == 0) {
		offset = 0;
		break;
	    }
	}

	if (fseek(fp, fpos + offset, SEEK_SET) == -1)
	    dlg_exiterr("Error moving file pointer in last_lines().");
    }
}

/*
 * Print a new page of text.
 */
static void
print_page(MY_OBJ * obj, int height, int width)
{
    int i;

    for (i = 0; i < height; i++) {
	print_line(obj, obj->text, i, width);
    }
    (void) wnoutrefresh(obj->text);
}

static void
print_last_page(MY_OBJ * obj)
{
    int high = getmaxy(obj->obj.win) - (2 * MARGIN + (obj->obj.bg_task ? 1 : 3));
    int wide = getmaxx(obj->text);

    last_lines(obj, high);
    print_page(obj, high, wide);
}

static void
repaint_text(MY_OBJ * obj)
{
    FILE *fp = obj->obj.input;
    int cur_y, cur_x;

    getyx(obj->obj.win, cur_y, cur_x);
    obj->old_hscroll = obj->hscroll;

    print_last_page(obj);
    obj->last_pos = ftell(fp);

    (void) wmove(obj->obj.win, cur_y, cur_x);	/* Restore cursor position */
    wrefresh(obj->obj.win);
}

static bool
handle_input(DIALOG_CALLBACK * cb)
{
    MY_OBJ *obj = (MY_OBJ *) cb;
    FILE *fp = obj->obj.input;
    struct stat sb;

    if (fstat(fileno(fp), &sb) == 0
	&& sb.st_size != obj->last_pos) {
	repaint_text(obj);
    }

    return TRUE;
}

static bool
valid_callback(DIALOG_CALLBACK * cb)
{
    bool valid = FALSE;
    DIALOG_CALLBACK *p;
    for (p = dialog_state.getc_callbacks; p != 0; p = p->next) {
	if (p == cb) {
	    valid = TRUE;
	    break;
	}
    }
    return valid;
}

static bool
handle_my_getc(DIALOG_CALLBACK * cb, int ch, int fkey, int *result)
{
    MY_OBJ *obj = (MY_OBJ *) cb;
    bool done = FALSE;

    if (!valid_callback(cb))
	return FALSE;

    if (!fkey && dlg_char_to_button(ch, obj->buttons) == 0) {
	ch = DLGK_ENTER;
	fkey = TRUE;
    }

    if (fkey) {
	switch (ch) {
	case DLGK_ENTER:
	    *result = DLG_EXIT_OK;
	    done = TRUE;
	    break;
	case DLGK_BEGIN:	/* Beginning of line */
	    obj->hscroll = 0;
	    break;
	case DLGK_GRID_LEFT:	/* Scroll left */
	    if (obj->hscroll > 0) {
		obj->hscroll -= 1;
	    }
	    break;
	case DLGK_GRID_RIGHT:	/* Scroll right */
	    if (obj->hscroll < MAX_LEN)
		obj->hscroll += 1;
	    break;
	default:
	    beep();
	    break;
	}
	if ((obj->hscroll != obj->old_hscroll))
	    repaint_text(obj);
    } else {
	switch (ch) {
	case ERR:
	    clearerr(cb->input);
	    ch = getc(cb->input);
	    (void) ungetc(ch, cb->input);
	    if (ch != EOF) {
		handle_input(cb);
	    }
	    break;
	case ESC:
	    done = TRUE;
	    *result = DLG_EXIT_ESC;
	    break;
	default:
	    beep();
	    break;
	}
    }

    return !done;
}

/*
 * Display text from a file in a dialog box, like in a "tail -f".
 */
int
dialog_tailbox(const char *title,
	       const char *filename,
	       int height,
	       int width,
	       int bg_task)
{
    /* *INDENT-OFF* */
    static DLG_KEYS_BINDING binding[] = {
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	DLG_KEYS_DATA( DLGK_BEGIN,      '0' ),
	DLG_KEYS_DATA( DLGK_BEGIN,      KEY_BEG ),
	DLG_KEYS_DATA( DLGK_GRID_LEFT,  'H' ),
	DLG_KEYS_DATA( DLGK_GRID_LEFT,  'h' ),
	DLG_KEYS_DATA( DLGK_GRID_LEFT,  KEY_LEFT ),
	DLG_KEYS_DATA( DLGK_GRID_RIGHT, 'L' ),
	DLG_KEYS_DATA( DLGK_GRID_RIGHT, 'l' ),
	DLG_KEYS_DATA( DLGK_GRID_RIGHT, KEY_RIGHT ),
	END_KEYS_BINDING
    };
    /* *INDENT-ON* */

#ifdef KEY_RESIZE
    int old_height = height;
    int old_width = width;
#endif
    int fkey;
    int x, y, result, thigh;
    WINDOW *dialog, *text;
    const char **buttons = 0;
    MY_OBJ *obj;
    FILE *fd;
    int min_width = 12;

    DLG_TRACE(("# tailbox args:\n"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("filename", filename);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);
    DLG_TRACE2N("bg_task", bg_task);

    /* Open input file for reading */
    if ((fd = fopen(filename, "rb")) == NULL)
	dlg_exiterr("Can't open input file in dialog_tailbox().");

#ifdef KEY_RESIZE
  retry:
#endif
    dlg_auto_sizefile(title, filename, &height, &width, 2, min_width);
    dlg_print_size(height, width);
    dlg_ctl_size(height, width);

    x = dlg_box_x_ordinate(width);
    y = dlg_box_y_ordinate(height);
    thigh = height - ((2 * MARGIN) + (bg_task ? 0 : 2));

    dialog = dlg_new_window(height, width, y, x);

    dlg_mouse_setbase(x, y);

    /* Create window for text region, used for scrolling text */
    text = dlg_sub_window(dialog,
			  thigh,
			  width - (2 * MARGIN),
			  y + MARGIN,
			  x + MARGIN);

    dlg_draw_box2(dialog, 0, 0, height, width, dialog_attr, border_attr, border2_attr);
    dlg_draw_bottom_box2(dialog, border_attr, border2_attr, dialog_attr);
    dlg_draw_title(dialog, title);
    dlg_draw_helpline(dialog, FALSE);

    if (!bg_task) {
	buttons = dlg_exit_label();
	dlg_button_layout(buttons, &min_width);
	dlg_draw_buttons(dialog, height - (2 * MARGIN), 0, buttons, FALSE,
			 FALSE, width);
    }

    (void) wmove(dialog, thigh, (MARGIN + 1));
    (void) wnoutrefresh(dialog);

    obj = dlg_calloc(MY_OBJ, 1);
    assert_ptr(obj, "dialog_tailbox");

    obj->obj.input = fd;
    obj->obj.win = dialog;
    obj->obj.handle_getc = handle_my_getc;
    obj->obj.handle_input = bg_task ? handle_input : 0;
    obj->obj.keep_bg = bg_task && dialog_vars.cant_kill;
    obj->obj.bg_task = bg_task;
    obj->text = text;
    obj->buttons = buttons;
    dlg_add_callback(&(obj->obj));

    dlg_register_window(dialog, "tailbox", binding);
    dlg_register_buttons(dialog, "tailbox", buttons);

    /* Print last page of text */
    dlg_attr_clear(text, thigh, getmaxx(text), dialog_attr);
    repaint_text(obj);

    dlg_trace_win(dialog);
    if (bg_task) {
	result = DLG_EXIT_OK;
    } else {
	int ch;
	do {
	    ch = dlg_getc(dialog, &fkey);
#ifdef KEY_RESIZE
	    if (fkey && ch == KEY_RESIZE) {
		dlg_will_resize(dialog);
		/* reset data */
		height = old_height;
		width = old_width;
		/* repaint */
		dlg_clear();
		dlg_del_window(dialog);
		refresh();
		dlg_mouse_free_regions();
		dlg_button_layout(buttons, &min_width);
		goto retry;
	    }
#endif
	}
	while (handle_my_getc(&(obj->obj), ch, fkey, &result));
    }
    dlg_mouse_free_regions();
    return result;
}
