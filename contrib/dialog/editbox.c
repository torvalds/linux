/*
 *  $Id: editbox.c,v 1.70 2018/06/19 22:57:01 tom Exp $
 *
 *  editbox.c -- implements the edit box
 *
 *  Copyright 2007-2016,2018 Thomas E. Dickey
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
#include <dlg_keys.h>

#include <sys/stat.h>

#define sTEXT -1

static void
fail_list(void)
{
    dlg_exiterr("File too large");
}

static void
grow_list(char ***list, int *have, int want)
{
    if (want > *have) {
	size_t last = (size_t) *have;
	size_t need = (size_t) (want | 31) + 3;
	*have = (int) need;
	(*list) = dlg_realloc(char *, need, *list);
	if ((*list) == 0) {
	    fail_list();
	} else {
	    while (++last < need) {
		(*list)[last] = 0;
	    }
	}
    }
}

static void
load_list(const char *file, char ***list, int *rows)
{
    FILE *fp;
    char *blob = 0;
    struct stat sb;
    unsigned n, pass;
    unsigned need;
    size_t size;

    *list = 0;
    *rows = 0;

    if (stat(file, &sb) < 0 ||
	(sb.st_mode & S_IFMT) != S_IFREG)
	dlg_exiterr("Not a file: %s", file);

    size = (size_t) sb.st_size;
    if ((blob = dlg_malloc(char, size + 2)) == 0) {
	fail_list();
    } else {
	blob[size] = '\0';

	if ((fp = fopen(file, "r")) == 0)
	    dlg_exiterr("Cannot open: %s", file);
	size = fread(blob, sizeof(char), size, fp);
	fclose(fp);

	/*
	 * If the file is not empty, ensure that it ends with a newline.
	 */
	if (size != 0 && blob[size - 1] != '\n') {
	    blob[++size - 1] = '\n';
	    blob[size] = '\0';
	}

	for (pass = 0; pass < 2; ++pass) {
	    int first = TRUE;
	    need = 0;
	    for (n = 0; n < size; ++n) {
		if (first && pass) {
		    (*list)[need] = blob + n;
		    first = FALSE;
		}
		if (blob[n] == '\n') {
		    first = TRUE;
		    ++need;
		    if (pass)
			blob[n] = '\0';
		}
	    }
	    if (pass) {
		if (need == 0) {
		    (*list)[0] = dlg_strclone("");
		    (*list)[1] = 0;
		} else {
		    for (n = 0; n < need; ++n) {
			(*list)[n] = dlg_strclone((*list)[n]);
		    }
		    (*list)[need] = 0;
		}
	    } else {
		grow_list(list, rows, (int) need + 1);
	    }
	}
	free(blob);
    }
}

static void
free_list(char ***list, int *rows)
{
    if (*list != 0) {
	int n;
	for (n = 0; n < (*rows); ++n) {
	    if ((*list)[n] != 0)
		free((*list)[n]);
	}
	free(*list);
	*list = 0;
    }
    *rows = 0;
}

/*
 * Display a single row in the editing window:
 * thisrow is the actual row number that's being displayed.
 * show_row is the row number that's highlighted for edit.
 * base_row is the first row number in the window
 */
static bool
display_one(WINDOW *win,
	    char *text,
	    int thisrow,
	    int show_row,
	    int base_row,
	    int chr_offset)
{
    bool result;

    if (text != 0) {
	dlg_show_string(win,
			text,
			chr_offset,
			((thisrow == show_row)
			 ? form_active_text_attr
			 : form_text_attr),
			thisrow - base_row,
			0,
			getmaxx(win),
			FALSE,
			FALSE);
	result = TRUE;
    } else {
	result = FALSE;
    }
    return result;
}

static void
display_all(WINDOW *win,
	    char **list,
	    int show_row,
	    int firstrow,
	    int lastrow,
	    int chr_offset)
{
    int limit = getmaxy(win);
    int row;

    dlg_attr_clear(win, getmaxy(win), getmaxx(win), dialog_attr);
    if (lastrow - firstrow >= limit)
	lastrow = firstrow + limit;
    for (row = firstrow; row < lastrow; ++row) {
	if (!display_one(win, list[row],
			 row, show_row, firstrow,
			 (row == show_row) ? chr_offset : 0))
	    break;
    }
}

static int
size_list(char **list)
{
    int result = 0;

    if (list != 0) {
	while (*list++ != 0) {
	    ++result;
	}
    }
    return result;
}

static bool
scroll_to(int pagesize, int rows, int *base_row, int *this_row, int target)
{
    bool result = FALSE;

    if (target < *base_row) {
	if (target < 0) {
	    if (*base_row == 0 && *this_row == 0) {
		beep();
	    } else {
		*this_row = 0;
		*base_row = 0;
		result = TRUE;
	    }
	} else {
	    *this_row = target;
	    *base_row = target;
	    result = TRUE;
	}
    } else if (target >= rows) {
	if (*this_row < rows - 1) {
	    *this_row = rows - 1;
	    *base_row = rows - 1;
	    result = TRUE;
	} else {
	    beep();
	}
    } else if (target >= *base_row + pagesize) {
	*this_row = target;
	*base_row = target;
	result = TRUE;
    } else {
	*this_row = target;
	result = FALSE;
    }
    if (pagesize < rows) {
	if (*base_row + pagesize >= rows) {
	    *base_row = rows - pagesize;
	}
    } else {
	*base_row = 0;
    }
    return result;
}

static int
col_to_chr_offset(const char *text, int col)
{
    const int *cols = dlg_index_columns(text);
    const int *indx = dlg_index_wchars(text);
    bool found = FALSE;
    int result = 0;
    unsigned n;
    unsigned len = (unsigned) dlg_count_wchars(text);

    for (n = 0; n < len; ++n) {
	if (cols[n] <= col && cols[n + 1] > col) {
	    result = indx[n];
	    found = TRUE;
	    break;
	}
    }
    if (!found && len && cols[len] == col) {
	result = indx[len];
    }
    return result;
}

#define SCROLL_TO(target) show_all = scroll_to(pagesize, listsize, &base_row, &thisrow, target)

#define PREV_ROW (*list)[thisrow - 1]
#define THIS_ROW (*list)[thisrow]
#define NEXT_ROW (*list)[thisrow + 1]

#define UPDATE_COL(input) col_offset = dlg_edit_offset(input, chr_offset, box_width)

static int
widest_line(char **list)
{
    int result = MAX_LEN;
    char *value;

    if (list != 0) {
	while ((value = *list++) != 0) {
	    int check = (int) strlen(value);
	    if (check > result)
		result = check;
	}
    }
    return result;
}

#define NAVIGATE_BINDINGS \
	DLG_KEYS_DATA( DLGK_GRID_DOWN,	KEY_DOWN ), \
	DLG_KEYS_DATA( DLGK_GRID_RIGHT,	KEY_RIGHT ), \
	DLG_KEYS_DATA( DLGK_GRID_LEFT,	KEY_LEFT ), \
	DLG_KEYS_DATA( DLGK_GRID_UP,	KEY_UP ), \
	DLG_KEYS_DATA( DLGK_FIELD_NEXT,	TAB ), \
	DLG_KEYS_DATA( DLGK_FIELD_PREV,	KEY_BTAB ), \
	DLG_KEYS_DATA( DLGK_PAGE_FIRST,	KEY_HOME ), \
	DLG_KEYS_DATA( DLGK_PAGE_LAST,	KEY_END ), \
	DLG_KEYS_DATA( DLGK_PAGE_LAST,	KEY_LL ), \
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,	KEY_NPAGE ), \
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,	DLGK_MOUSE(KEY_NPAGE) ), \
	DLG_KEYS_DATA( DLGK_PAGE_PREV,	KEY_PPAGE ), \
	DLG_KEYS_DATA( DLGK_PAGE_PREV,	DLGK_MOUSE(KEY_PPAGE) )
/*
 * Display a dialog box for editing a copy of a file
 */
int
dlg_editbox(const char *title,
	    char ***list,
	    int *rows,
	    int height,
	    int width)
{
    /* *INDENT-OFF* */
    static DLG_KEYS_BINDING binding[] = {
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	NAVIGATE_BINDINGS,
	TOGGLEKEY_BINDINGS,
	END_KEYS_BINDING
    };
    static DLG_KEYS_BINDING binding2[] = {
	INPUTSTR_BINDINGS,
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	NAVIGATE_BINDINGS,
	/* no TOGGLEKEY_BINDINGS, since that includes space... */
	END_KEYS_BINDING
    };
    /* *INDENT-ON* */

#ifdef KEY_RESIZE
    int old_height = height;
    int old_width = width;
#endif
    int x, y, box_y, box_x, box_height, box_width;
    int show_buttons;
    int thisrow, base_row, lastrow;
    int goal_col = -1;
    int col_offset = 0;
    int chr_offset = 0;
    int key, fkey, code;
    int pagesize;
    int listsize = size_list(*list);
    int result = DLG_EXIT_UNKNOWN;
    int state;
    size_t max_len = (size_t) dlg_max_input(widest_line(*list));
    char *input, *buffer;
    bool show_all, show_one, was_mouse;
    bool first_trace = TRUE;
    WINDOW *dialog;
    WINDOW *editing;
    DIALOG_VARS save_vars;
    const char **buttons = dlg_ok_labels();
    int mincols = (3 * COLS / 4);

    DLG_TRACE(("# editbox args:\n"));
    DLG_TRACE2S("title", title);
    /* FIXME dump the rows & list */
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);

    dlg_save_vars(&save_vars);
    dialog_vars.separate_output = TRUE;

    dlg_does_output();

    buffer = dlg_malloc(char, max_len + 1);
    assert_ptr(buffer, "dlg_editbox");

    thisrow = base_row = lastrow = 0;

#ifdef KEY_RESIZE
  retry:
#endif
    show_buttons = TRUE;
    state = dialog_vars.default_button >= 0 ? dlg_default_button() : sTEXT;
    fkey = 0;

    dlg_button_layout(buttons, &mincols);
    dlg_auto_size(title, "", &height, &width, 3 * LINES / 4, mincols);
    dlg_print_size(height, width);
    dlg_ctl_size(height, width);

    x = dlg_box_x_ordinate(width);
    y = dlg_box_y_ordinate(height);

    dialog = dlg_new_window(height, width, y, x);
    dlg_register_window(dialog, "editbox", binding);
    dlg_register_buttons(dialog, "editbox", buttons);

    dlg_mouse_setbase(x, y);

    dlg_draw_box2(dialog, 0, 0, height, width, dialog_attr, border_attr, border2_attr);
    dlg_draw_bottom_box2(dialog, border_attr, border2_attr, dialog_attr);
    dlg_draw_title(dialog, title);

    dlg_attrset(dialog, dialog_attr);

    /* Draw the editing field in a box */
    box_y = MARGIN + 0;
    box_x = MARGIN + 1;
    box_width = width - 2 - (2 * MARGIN);
    box_height = height - (4 * MARGIN);

    dlg_draw_box(dialog,
		 box_y,
		 box_x,
		 box_height,
		 box_width,
		 border_attr, border2_attr);
    dlg_mouse_mkbigregion(box_y + MARGIN,
			  box_x + MARGIN,
			  box_height - (2 * MARGIN),
			  box_width - (2 * MARGIN),
			  KEY_MAX, 1, 1, 3);
    editing = dlg_sub_window(dialog,
			     box_height - (2 * MARGIN),
			     box_width - (2 * MARGIN),
			     getbegy(dialog) + box_y + 1,
			     getbegx(dialog) + box_x + 1);
    dlg_register_window(editing, "editbox2", binding2);

    show_all = TRUE;
    show_one = FALSE;
    pagesize = getmaxy(editing);

    while (result == DLG_EXIT_UNKNOWN) {
	int edit = 0;

	if (show_all) {
	    display_all(editing, *list, thisrow, base_row, listsize, chr_offset);
	    display_one(editing, THIS_ROW,
			thisrow, thisrow, base_row, chr_offset);
	    show_all = FALSE;
	    show_one = TRUE;
	} else {
	    if (thisrow != lastrow) {
		display_one(editing, (*list)[lastrow],
			    lastrow, thisrow, base_row, 0);
		show_one = TRUE;
	    }
	}
	if (show_one) {
	    display_one(editing, THIS_ROW,
			thisrow, thisrow, base_row, chr_offset);
	    getyx(editing, y, x);
	    dlg_draw_scrollbar(dialog,
			       base_row,
			       base_row,
			       base_row + pagesize,
			       listsize,
			       box_x,
			       box_x + getmaxx(editing),
			       box_y + 0,
			       box_y + getmaxy(editing) + 1,
			       border2_attr,
			       border_attr);
	    wmove(editing, y, x);
	    show_one = FALSE;
	}
	lastrow = thisrow;
	input = THIS_ROW;

	/*
	 * The last field drawn determines where the cursor is shown:
	 */
	if (show_buttons) {
	    show_buttons = FALSE;
	    UPDATE_COL(input);
	    if (state != sTEXT) {
		display_one(editing, input, thisrow,
			    -1, base_row, 0);
		wrefresh(editing);
	    }
	    dlg_draw_buttons(dialog,
			     height - 2,
			     0,
			     buttons,
			     (state != sTEXT) ? state : 99,
			     FALSE,
			     width);
	    if (state == sTEXT) {
		display_one(editing, input, thisrow,
			    thisrow, base_row, chr_offset);
	    }
	}

	if (first_trace) {
	    first_trace = FALSE;
	    dlg_trace_win(dialog);
	}

	key = dlg_mouse_wgetch((state == sTEXT) ? editing : dialog, &fkey);
	if (key == ERR) {
	    result = DLG_EXIT_ERROR;
	    break;
	} else if (key == ESC) {
	    result = DLG_EXIT_ESC;
	    break;
	}
	if (state != sTEXT) {
	    if (dlg_result_key(key, fkey, &result))
		break;
	}

	was_mouse = (fkey && is_DLGK_MOUSE(key));
	if (was_mouse)
	    key -= M_EVENT;

	/*
	 * Handle mouse clicks first, since we want to know if this is a
	 * button, or something that dlg_edit_string() should handle.
	 */
	if (fkey
	    && was_mouse
	    && (code = dlg_ok_buttoncode(key)) >= 0) {
	    result = code;
	    continue;
	}

	if (was_mouse
	    && (key >= KEY_MAX)) {
	    int wide = getmaxx(editing);
	    int cell = key - KEY_MAX;
	    int check = (cell / wide) + base_row;
	    if (check < listsize) {
		thisrow = check;
		col_offset = (cell % wide);
		chr_offset = col_to_chr_offset(THIS_ROW, col_offset);
		show_one = TRUE;
		if (state != sTEXT) {
		    state = sTEXT;
		    show_buttons = TRUE;
		}
	    } else {
		beep();
	    }
	    continue;
	} else if (was_mouse && key >= KEY_MIN) {
	    key = dlg_lookup_key(dialog, key, &fkey);
	}

	if (state == sTEXT) {	/* editing box selected */
	    /*
	     * Intercept scrolling keys that dlg_edit_string() does not
	     * understand.
	     */
	    if (fkey) {
		bool moved = TRUE;

		switch (key) {
		case DLGK_GRID_UP:
		    SCROLL_TO(thisrow - 1);
		    break;
		case DLGK_GRID_DOWN:
		    SCROLL_TO(thisrow + 1);
		    break;
		case DLGK_PAGE_FIRST:
		    SCROLL_TO(0);
		    break;
		case DLGK_PAGE_LAST:
		    SCROLL_TO(listsize);
		    break;
		case DLGK_PAGE_NEXT:
		    SCROLL_TO(base_row + pagesize);
		    break;
		case DLGK_PAGE_PREV:
		    if (thisrow > base_row) {
			SCROLL_TO(base_row);
		    } else {
			SCROLL_TO(base_row - pagesize);
		    }
		    break;
		case DLGK_DELETE_LEFT:
		    if (chr_offset == 0) {
			if (thisrow == 0) {
			    beep();
			} else {
			    size_t len = (strlen(THIS_ROW) +
					  strlen(PREV_ROW) + 1);
			    char *tmp = dlg_malloc(char, len);

			    assert_ptr(tmp, "dlg_editbox");

			    chr_offset = dlg_count_wchars(PREV_ROW);
			    UPDATE_COL(PREV_ROW);
			    goal_col = col_offset;

			    sprintf(tmp, "%s%s", PREV_ROW, THIS_ROW);
			    if (len > max_len)
				tmp[max_len] = '\0';

			    free(PREV_ROW);
			    PREV_ROW = tmp;
			    for (y = thisrow; y < listsize; ++y) {
				(*list)[y] = (*list)[y + 1];
			    }
			    --listsize;
			    --thisrow;
			    SCROLL_TO(thisrow);

			    show_all = TRUE;
			}
		    } else {
			/* dlg_edit_string() can handle this case */
			moved = FALSE;
		    }
		    break;
		default:
		    moved = FALSE;
		    break;
		}
		if (moved) {
		    if (thisrow != lastrow) {
			if (goal_col < 0)
			    goal_col = col_offset;
			chr_offset = col_to_chr_offset(THIS_ROW, goal_col);
		    } else {
			UPDATE_COL(THIS_ROW);
		    }
		    continue;
		}
	    }
	    strncpy(buffer, input, max_len - 1)[max_len - 1] = '\0';
	    edit = dlg_edit_string(buffer, &chr_offset, key, fkey, FALSE);

	    if (edit) {
		goal_col = UPDATE_COL(input);
		if (strcmp(input, buffer)) {
		    free(input);
		    THIS_ROW = dlg_strclone(buffer);
		    input = THIS_ROW;
		}
		display_one(editing, input, thisrow,
			    thisrow, base_row, chr_offset);
		continue;
	    }
	}

	/* handle non-functionkeys */
	if (!fkey && (code = dlg_char_to_button(key, buttons)) >= 0) {
	    dlg_del_window(dialog);
	    result = dlg_ok_buttoncode(code);
	    continue;
	}

	/* handle functionkeys */
	if (fkey) {
	    switch (key) {
	    case DLGK_GRID_UP:
	    case DLGK_GRID_LEFT:
	    case DLGK_FIELD_PREV:
		show_buttons = TRUE;
		state = dlg_prev_ok_buttonindex(state, sTEXT);
		break;
	    case DLGK_GRID_RIGHT:
	    case DLGK_GRID_DOWN:
	    case DLGK_FIELD_NEXT:
		show_buttons = TRUE;
		state = dlg_next_ok_buttonindex(state, sTEXT);
		break;
	    case DLGK_ENTER:
		if (state == sTEXT) {
		    const int *indx = dlg_index_wchars(THIS_ROW);
		    int split = indx[chr_offset];
		    char *tmp = dlg_strclone(THIS_ROW + split);

		    assert_ptr(tmp, "dlg_editbox");
		    grow_list(list, rows, listsize + 1);
		    ++listsize;
		    for (y = listsize; y > thisrow; --y) {
			(*list)[y] = (*list)[y - 1];
		    }
		    THIS_ROW[split] = '\0';
		    ++thisrow;
		    chr_offset = 0;
		    col_offset = 0;
		    THIS_ROW = tmp;
		    SCROLL_TO(thisrow);
		    show_all = TRUE;
		} else {
		    result = dlg_ok_buttoncode(state);
		}
		break;
#ifdef KEY_RESIZE
	    case KEY_RESIZE:
		dlg_will_resize(dialog);
		/* reset data */
		height = old_height;
		width = old_width;
		dlg_clear();
		dlg_unregister_window(editing);
		dlg_del_window(editing);
		dlg_del_window(dialog);
		dlg_mouse_free_regions();
		/* repaint */
		goto retry;
#endif
	    case DLGK_TOGGLE:
		if (state != sTEXT) {
		    result = dlg_ok_buttoncode(state);
		} else {
		    beep();
		}
		break;
	    default:
		beep();
		break;
	    }
	} else {
	    beep();
	}
    }

    dlg_unregister_window(editing);
    dlg_del_window(editing);
    dlg_del_window(dialog);
    dlg_mouse_free_regions();

    /*
     * The caller's copy of the (*list)[] array has been updated, but for
     * consistency with the other widgets, we put the "real" result in
     * the output buffer.
     */
    if (result == DLG_EXIT_OK) {
	int n;
	for (n = 0; n < listsize; ++n) {
	    dlg_add_result((*list)[n]);
	    dlg_add_separator();
	}
	dlg_add_last_key(-1);
    }
    free(buffer);
    dlg_restore_vars(&save_vars);
    return result;
}

int
dialog_editbox(const char *title, const char *file, int height, int width)
{
    int result;
    char **list;
    int rows;

    load_list(file, &list, &rows);
    result = dlg_editbox(title, &list, &rows, height, width);
    free_list(&list, &rows);
    return result;
}
