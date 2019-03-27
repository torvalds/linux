/*
 *  $Id: rangebox.c,v 1.24 2018/06/19 22:57:01 tom Exp $
 *
 *  rangebox.c -- implements the rangebox dialog
 *
 *  Copyright 2012-2017,2018	Thomas E. Dickey
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
#include <dlg_keys.h>

#define ONE_HIGH 1

#define MIN_HIGH (ONE_HIGH + 1 + (4 * MARGIN))
#define MIN_WIDE (10 + 2 + (2 * MARGIN))

struct _box;

typedef struct _box {
    WINDOW *parent;
    WINDOW *window;
    int x;
    int y;
    int width;
    int height;
    int period;
    int value;
} BOX;

typedef struct {
    /* window in which the value and slider are drawn */
    WINDOW *window;
    int min_value;
    int max_value;
    /* position and width of the numeric field */
    int value_x;
    int value_len;
    int value_col;
    /* position and width of the slider field */
    int slide_x;
    int slide_y;
    int slide_len;
    /* current value drawn */
    int current;
    /* value to add to make slider move by one cell */
    int slide_inc;
} VALUE;

static int
digits_of(int value)
{
    char temp[80];
    sprintf(temp, "%d", value);
    return (int) strlen(temp);
}

static int
digit_of(VALUE * data)
{
    int col = data->value_col;
    int result = 1;

    while (++col < data->value_len) {
	result *= 10;
    }
    return result;
}

static bool
set_digit(VALUE * data, int chr)
{
    bool result = FALSE;
    char buffer[80];
    long check;
    char *next = 0;

    sprintf(buffer, "%*d", data->value_len, data->current);
    buffer[data->value_col] = (char) chr;
    check = strtol(buffer, &next, 10);
    if (next == 0 || *next == '\0') {
	if ((check <= (long) data->max_value) &&
	    (check >= (long) data->min_value)) {
	    result = TRUE;
	    data->current = (int) check;
	}
    }

    return result;
}

/*
 * This is similar to the gauge code, but differs in the way the number
 * is displayed, etc.
 */
static void
draw_value(VALUE * data, int value)
{
    if (value != data->current) {
	WINDOW *win = data->window;
	int y, x;
	int n;
	int ranges = (data->max_value + 1 - data->min_value);
	int offset = (value - data->min_value);
	int scaled;

	getyx(win, y, x);

	if (ranges > data->slide_len) {
	    scaled = (offset + data->slide_inc) / data->slide_inc;
	} else if (ranges < data->slide_len) {
	    scaled = (offset + 1) * data->slide_inc;
	} else {
	    scaled = offset;
	}

	dlg_attrset(win, gauge_attr);
	wmove(win, data->slide_y, data->slide_x);
	for (n = 0; n < data->slide_len; ++n) {
	    (void) waddch(win, ' ');
	}
	wmove(win, data->slide_y, data->value_x);
	wprintw(win, "%*d", data->value_len, value);
	if ((gauge_attr & A_REVERSE) != 0) {
	    dlg_attroff(win, A_REVERSE);
	} else {
	    dlg_attrset(win, A_REVERSE);
	}
	wmove(win, data->slide_y, data->slide_x);
	for (n = 0; n < scaled; ++n) {
	    chtype ch2 = winch(win);
	    if (gauge_attr & A_REVERSE) {
		ch2 &= ~A_REVERSE;
	    }
	    (void) waddch(win, ch2);
	}
	dlg_attrset(win, dialog_attr);

	wmove(win, y, x);
	data->current = value;

	DLG_TRACE(("# drew %d offset %d scaled %d limit %d inc %d\n",
		   value,
		   offset,
		   scaled,
		   data->slide_len,
		   data->slide_inc));

	dlg_trace_win(win);
    }
}

/*
 * Allow the user to select from a range of values, e.g., using a slider.
 */
int
dialog_rangebox(const char *title,
		const char *cprompt,
		int height,
		int width,
		int min_value,
		int max_value,
		int default_value)
{
    /* *INDENT-OFF* */
    static DLG_KEYS_BINDING binding[] = {
	DLG_KEYS_DATA( DLGK_DELETE_RIGHT,KEY_DC ),
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	TOGGLEKEY_BINDINGS,
	DLG_KEYS_DATA( DLGK_FIELD_NEXT, CHR_NEXT ),
	DLG_KEYS_DATA( DLGK_FIELD_NEXT, KEY_RIGHT ),
	DLG_KEYS_DATA( DLGK_FIELD_NEXT, TAB ),
	DLG_KEYS_DATA( DLGK_FIELD_PREV, CHR_BACKSPACE ),
	DLG_KEYS_DATA( DLGK_FIELD_PREV, CHR_PREVIOUS ),
	DLG_KEYS_DATA( DLGK_FIELD_PREV, KEY_BTAB ),
	DLG_KEYS_DATA( DLGK_FIELD_PREV, KEY_LEFT ),
	DLG_KEYS_DATA( DLGK_ITEM_FIRST, KEY_HOME),
	DLG_KEYS_DATA( DLGK_ITEM_LAST,  KEY_END),
	DLG_KEYS_DATA( DLGK_ITEM_LAST,  KEY_LL ),
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,  '+'),
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,  KEY_DOWN),
	DLG_KEYS_DATA( DLGK_ITEM_PREV,  '-' ),
	DLG_KEYS_DATA( DLGK_ITEM_PREV,  KEY_UP ),
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,  KEY_NEXT),
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,  KEY_NPAGE),
	DLG_KEYS_DATA( DLGK_PAGE_PREV,  KEY_PPAGE ),
	DLG_KEYS_DATA( DLGK_PAGE_PREV,  KEY_PREVIOUS ),
	END_KEYS_BINDING
    };
    /* *INDENT-ON* */

#ifdef KEY_RESIZE
    int old_height = height;
    int old_width = width;
#endif
    VALUE data;
    int key = 0, key2, fkey;
    int button;
    int result = DLG_EXIT_UNKNOWN;
    WINDOW *dialog;
    int state = dlg_default_button();
    const char **buttons = dlg_ok_labels();
    char *prompt;
    char buffer[MAX_LEN];
    int cur_value = default_value;
    int usable;
    int ranges;
    int yorg, xorg;

    DLG_TRACE(("# tailbox args:\n"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("message", cprompt);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);
    DLG_TRACE2N("minval", min_value);
    DLG_TRACE2N("maxval", max_value);
    DLG_TRACE2N("default", default_value);

    if (max_value < min_value)
	max_value = min_value;
    if (cur_value > max_value)
	cur_value = max_value;
    if (cur_value < min_value)
	cur_value = min_value;

    dlg_does_output();

#ifdef KEY_RESIZE
  retry:
#endif

    prompt = dlg_strclone(cprompt);
    dlg_auto_size(title, prompt, &height, &width, 0, 0);

    height += MIN_HIGH;
    if (width < MIN_WIDE)
	width = MIN_WIDE;
    dlg_button_layout(buttons, &width);
    dlg_print_size(height, width);
    dlg_ctl_size(height, width);

    dialog = dlg_new_window(height, width,
			    yorg = dlg_box_y_ordinate(height),
			    xorg = dlg_box_x_ordinate(width));

    data.window = dialog;

    data.min_value = min_value;
    data.max_value = max_value;

    usable = (width - 2 - 4 * MARGIN);
    ranges = max_value - min_value + 1;

    /*
     * Center the number after allowing for its maximum number of digits.
     */
    data.value_len = digits_of(max_value);
    if (digits_of(min_value) > data.value_len)
	data.value_len = digits_of(min_value);
    data.value_x = (usable - data.value_len) / 2 + MARGIN;
    data.value_col = data.value_len - 1;

    /*
     * The slider is scaled, to try to use the width of the dialog.
     */
    if (ranges > usable) {
	data.slide_inc = (ranges + usable - 1) / usable;
	data.slide_len = 1 + ranges / data.slide_inc;
    } else if (ranges < usable) {
	data.slide_inc = usable / ranges;
	data.slide_len = ranges * data.slide_inc;
    } else {
	data.slide_inc = 1;
	data.slide_len = usable;
    }
    data.slide_x = (usable - data.slide_len) / 2 + MARGIN + 2;
    data.slide_y = height - 5;

    data.current = cur_value - 1;

    dlg_register_window(dialog, "rangebox", binding);
    dlg_register_buttons(dialog, "rangebox", buttons);

    dlg_draw_box2(dialog, 0, 0, height, width, dialog_attr, border_attr, border2_attr);
    dlg_mouse_setbase(xorg, yorg);
    dlg_mouse_mkregion(data.slide_y - 1, data.slide_x - 1, 3, usable + 2, 'i');
    dlg_draw_box2(dialog,
		  height - 6, data.slide_x - MARGIN,
		  2 + MARGIN, data.slide_len + 2 * MARGIN,
		  dialog_attr,
		  border_attr,
		  border2_attr);
    dlg_draw_bottom_box2(dialog, border_attr, border2_attr, dialog_attr);
    dlg_draw_title(dialog, title);
    dlg_draw_helpline(dialog, FALSE);

    dlg_attrset(dialog, dialog_attr);
    dlg_print_autowrap(dialog, prompt, height, width);

    dlg_trace_win(dialog);
    while (result == DLG_EXIT_UNKNOWN) {
	draw_value(&data, cur_value);
	button = (state < 0) ? 0 : state;
	dlg_draw_buttons(dialog, height - 2, 0, buttons, button, FALSE, width);
	if (state < 0) {
	    data.value_col = data.value_len + state;
	    wmove(dialog, data.slide_y, data.value_x + data.value_col);
	}

	key = dlg_mouse_wgetch(dialog, &fkey);
	if (dlg_result_key(key, fkey, &result))
	    break;

	if ((key2 = dlg_char_to_button(key, buttons)) >= 0) {
	    result = key2;
	} else {
	    /* handle function-keys */
	    if (fkey) {
		switch (key) {
		case DLGK_TOGGLE:
		case DLGK_ENTER:
		    result = dlg_ok_buttoncode(button);
		    break;
		case DLGK_FIELD_PREV:
		    if (state < 0 && state > -data.value_len) {
			--state;
		    } else {
			state = dlg_prev_ok_buttonindex(state, -data.value_len);
		    }
		    break;
		case DLGK_FIELD_NEXT:
		    if (state < 0) {
			++state;
		    } else {
			state = dlg_next_ok_buttonindex(state, -data.value_len);
		    }
		    break;
		case DLGK_ITEM_FIRST:
		    cur_value = min_value;
		    break;
		case DLGK_ITEM_LAST:
		    cur_value = max_value;
		    break;
		case DLGK_ITEM_PREV:
		    if (state < 0) {
			cur_value -= digit_of(&data);
		    } else {
			cur_value -= 1;
		    }
		    if (cur_value < min_value)
			cur_value = min_value;
		    break;
		case DLGK_ITEM_NEXT:
		    if (state < 0) {
			cur_value += digit_of(&data);
		    } else {
			cur_value += 1;
		    }
		    if (cur_value > max_value)
			cur_value = max_value;
		    break;
		case DLGK_PAGE_PREV:
		    cur_value -= data.slide_inc;
		    if (cur_value < min_value)
			cur_value = min_value;
		    break;
		case DLGK_PAGE_NEXT:
		    cur_value += data.slide_inc;
		    if (cur_value > max_value)
			cur_value = max_value;
		    break;
#ifdef KEY_RESIZE
		case KEY_RESIZE:
		    dlg_will_resize(dialog);
		    /* reset data */
		    height = old_height;
		    width = old_width;
		    /* repaint */
		    free(prompt);
		    dlg_clear();
		    dlg_del_window(dialog);
		    dlg_mouse_free_regions();
		    goto retry;
#endif
		case DLGK_MOUSE('i'):
		    state = -data.value_len;
		    break;
		default:
		    if (is_DLGK_MOUSE(key)) {
			result = dlg_ok_buttoncode(key - M_EVENT);
			if (result < 0)
			    result = DLG_EXIT_OK;
		    }
		    break;
		}
	    } else if (isdigit(key) && state < 0) {
		if (set_digit(&data, key)) {
		    cur_value = data.current;
		    data.current--;
		}
	    } else {
		beep();
	    }
	}
    }

    sprintf(buffer, "%d", cur_value);
    dlg_add_result(buffer);
    dlg_add_separator();
    dlg_add_last_key(-1);

    dlg_del_window(dialog);
    dlg_mouse_free_regions();
    free(prompt);

    return result;
}
