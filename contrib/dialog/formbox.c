/*
 *  $Id: formbox.c,v 1.95 2018/06/21 08:23:31 tom Exp $
 *
 *  formbox.c -- implements the form (i.e., some pairs label/editbox)
 *
 *  Copyright 2003-2016,2018	Thomas E. Dickey
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
 *  This is adapted from source contributed by
 *	Valery Reznic (valery_reznic@users.sourceforge.net)
 */

#include <dialog.h>
#include <dlg_keys.h>

#define LLEN(n) ((n) * FORMBOX_TAGS)

#define ItemName(i)     items[LLEN(i) + 0]
#define ItemNameY(i)    items[LLEN(i) + 1]
#define ItemNameX(i)    items[LLEN(i) + 2]
#define ItemText(i)     items[LLEN(i) + 3]
#define ItemTextY(i)    items[LLEN(i) + 4]
#define ItemTextX(i)    items[LLEN(i) + 5]
#define ItemTextFLen(i) items[LLEN(i) + 6]
#define ItemTextILen(i) items[LLEN(i) + 7]
#define ItemHelp(i)     (dialog_vars.item_help ? items[LLEN(i) + 8] : dlg_strempty())

static bool
is_readonly(DIALOG_FORMITEM * item)
{
    return ((item->type & 2) != 0) || (item->text_flen <= 0);
}

static bool
is_hidden(DIALOG_FORMITEM * item)
{
    return ((item->type & 1) != 0);
}

static bool
in_window(WINDOW *win, int scrollamt, int y)
{
    return (y >= scrollamt && y - scrollamt < getmaxy(win));
}

static bool
ok_move(WINDOW *win, int scrollamt, int y, int x)
{
    return in_window(win, scrollamt, y)
	&& (wmove(win, y - scrollamt, x) != ERR);
}

static void
move_past(WINDOW *win, int y, int x)
{
    if (wmove(win, y, x) == ERR)
	wmove(win, y, getmaxx(win) - 1);
}

/*
 * Print form item
 */
static int
print_item(WINDOW *win, DIALOG_FORMITEM * item, int scrollamt, bool choice)
{
    int count = 0;
    int len;

    if (ok_move(win, scrollamt, item->name_y, item->name_x)) {
	len = item->name_len;
	len = MIN(len, getmaxx(win) - item->name_x);
	if (len > 0) {
	    dlg_show_string(win,
			    item->name,
			    0,
			    menubox_attr,
			    item->name_y - scrollamt,
			    item->name_x,
			    len,
			    FALSE,
			    FALSE);
	    move_past(win, item->name_y - scrollamt, item->name_x + len);
	    count = 1;
	}
    }
    if (item->text_len && ok_move(win, scrollamt, item->text_y, item->text_x)) {
	chtype this_item_attribute;

	len = item->text_len;
	len = MIN(len, getmaxx(win) - item->text_x);

	if (!is_readonly(item)) {
	    this_item_attribute = choice
		? form_active_text_attr
		: form_text_attr;
	} else {
	    this_item_attribute = form_item_readonly_attr;
	}

	if (len > 0) {
	    dlg_show_string(win,
			    item->text,
			    0,
			    this_item_attribute,
			    item->text_y - scrollamt,
			    item->text_x,
			    len,
			    is_hidden(item),
			    FALSE);
	    move_past(win, item->text_y - scrollamt, item->text_x + len);
	    count = 1;
	}
    }
    return count;
}

/*
 * Print the entire form.
 */
static void
print_form(WINDOW *win, DIALOG_FORMITEM * item, int total, int scrollamt, int choice)
{
    int n;
    int count = 0;

    for (n = 0; n < total; ++n) {
	count += print_item(win, item + n, scrollamt, n == choice);
    }
    if (count) {
	wbkgdset(win, menubox_attr | ' ');
	wclrtobot(win);
	(void) wnoutrefresh(win);
    }
}

static int
set_choice(DIALOG_FORMITEM item[], int choice, int item_no, bool * noneditable)
{
    int result = -1;
    int i;

    *noneditable = FALSE;
    if (!is_readonly(&item[choice])) {
	result = choice;
    } else {
	for (i = 0; i < item_no; i++) {
	    if (!is_readonly(&(item[i]))) {
		result = i;
		break;
	    }
	}
	if (result < 0) {
	    *noneditable = TRUE;
	    result = 0;
	}
    }
    return result;
}

/*
 * Find the last y-value in the form.
 */
static int
form_limit(DIALOG_FORMITEM item[])
{
    int n;
    int limit = 0;
    for (n = 0; item[n].name != 0; ++n) {
	if (limit < item[n].name_y)
	    limit = item[n].name_y;
	if (limit < item[n].text_y)
	    limit = item[n].text_y;
    }
    return limit;
}

static int
is_first_field(DIALOG_FORMITEM item[], int choice)
{
    int count = 0;
    while (choice >= 0) {
	if (item[choice].text_flen > 0) {
	    ++count;
	}
	--choice;
    }

    return (count == 1);
}

static int
is_last_field(DIALOG_FORMITEM item[], int choice, int item_no)
{
    int count = 0;
    while (choice < item_no) {
	if (item[choice].text_flen > 0) {
	    ++count;
	}
	++choice;
    }

    return (count == 1);
}

/*
 * Tab to the next field.
 */
static bool
tab_next(WINDOW *win,
	 DIALOG_FORMITEM item[],
	 int item_no,
	 int stepsize,
	 int *choice,
	 int *scrollamt)
{
    int old_choice = *choice;
    int old_scroll = *scrollamt;
    bool wrapped = FALSE;

    do {
	do {
	    *choice += stepsize;
	    if (*choice < 0) {
		*choice = item_no - 1;
		wrapped = TRUE;
	    } else if (*choice >= item_no) {
		*choice = 0;
		wrapped = TRUE;
	    }
	} while ((*choice != old_choice) && is_readonly(&(item[*choice])));

	if (item[*choice].text_flen > 0) {
	    int lo = MIN(item[*choice].name_y, item[*choice].text_y);
	    int hi = MAX(item[*choice].name_y, item[*choice].text_y);

	    if (old_choice == *choice)
		break;
	    print_item(win, item + old_choice, *scrollamt, FALSE);

	    if (*scrollamt < lo + 1 - getmaxy(win))
		*scrollamt = lo + 1 - getmaxy(win);
	    if (*scrollamt > hi)
		*scrollamt = hi;
	    /*
	     * If we have to scroll to show a wrap-around, it does get
	     * confusing.  Just give up rather than scroll.  Tab'ing to the
	     * next field in a multi-column form is a different matter.  Scroll
	     * for that.
	     */
	    if (*scrollamt != old_scroll) {
		if (wrapped) {
		    beep();
		    *scrollamt = old_scroll;
		    *choice = old_choice;
		} else {
		    scrollok(win, TRUE);
		    wscrl(win, *scrollamt - old_scroll);
		    scrollok(win, FALSE);
		}
	    }
	    break;
	}
    } while (*choice != old_choice);

    return (old_choice != *choice) || (old_scroll != *scrollamt);
}

/*
 * Scroll to the next page, putting the choice at the first editable field
 * in that page.  Note that fields are not necessarily in top-to-bottom order,
 * nor is there necessarily a field on each row of the window.
 */
static bool
scroll_next(WINDOW *win, DIALOG_FORMITEM item[], int stepsize, int *choice, int *scrollamt)
{
    bool result = TRUE;
    int old_choice = *choice;
    int old_scroll = *scrollamt;
    int old_row = MIN(item[old_choice].text_y, item[old_choice].name_y);
    int target = old_scroll + stepsize;
    int n;

    if (stepsize < 0) {
	if (old_row != old_scroll)
	    target = old_scroll;
	else
	    target = old_scroll + stepsize;
	if (target < 0) {
	    result = FALSE;
	}
    } else {
	if (target > form_limit(item)) {
	    result = FALSE;
	}
    }

    if (result) {
	for (n = 0; item[n].name != 0; ++n) {
	    if (item[n].text_flen > 0) {
		int new_row = MIN(item[n].text_y, item[n].name_y);
		if (abs(new_row - target) < abs(old_row - target)) {
		    old_row = new_row;
		    *choice = n;
		}
	    }
	}

	if (old_choice != *choice)
	    print_item(win, item + old_choice, *scrollamt, FALSE);

	*scrollamt = *choice;
	if (*scrollamt != old_scroll) {
	    scrollok(win, TRUE);
	    wscrl(win, *scrollamt - old_scroll);
	    scrollok(win, FALSE);
	}
	result = (old_choice != *choice) || (old_scroll != *scrollamt);
    }
    if (!result)
	beep();
    return result;
}

/*
 * Do a sanity check on the field length, and return the "right" value.
 */
static int
real_length(DIALOG_FORMITEM * item)
{
    return (item->text_flen > 0
	    ? item->text_flen
	    : (item->text_flen < 0
	       ? -item->text_flen
	       : item->text_len));
}

/*
 * Compute the form size, setup field buffers.
 */
static void
make_FORM_ELTs(DIALOG_FORMITEM * item,
	       int item_no,
	       int *min_height,
	       int *min_width)
{
    int i;
    int min_w = 0;
    int min_h = 0;

    for (i = 0; i < item_no; ++i) {
	int real_len = real_length(item + i);

	/*
	 * Special value '0' for text_flen: no input allowed
	 * Special value '0' for text_ilen: 'be the same as text_flen'
	 */
	if (item[i].text_ilen == 0)
	    item[i].text_ilen = real_len;

	min_h = MAX(min_h, item[i].name_y + 1);
	min_h = MAX(min_h, item[i].text_y + 1);
	min_w = MAX(min_w, item[i].name_x + 1 + item[i].name_len);
	min_w = MAX(min_w, item[i].text_x + 1 + real_len);

	item[i].text_len = real_length(item + i);

	/*
	 * We do not know the actual length of .text, so we allocate it here
	 * to ensure it is big enough.
	 */
	if (item[i].text_flen > 0) {
	    int max_len = dlg_max_input(MAX(item[i].text_ilen + 1, MAX_LEN));
	    char *old_text = item[i].text;

	    item[i].text = dlg_malloc(char, (size_t) max_len + 1);
	    assert_ptr(item[i].text, "make_FORM_ELTs");

	    sprintf(item[i].text, "%.*s", item[i].text_ilen, old_text);

	    if (item[i].text_free) {
		item[i].text_free = FALSE;
		free(old_text);
	    }
	    item[i].text_free = TRUE;
	}
    }

    *min_height = min_h;
    *min_width = min_w;
}

int
dlg_default_formitem(DIALOG_FORMITEM * items)
{
    int result = 0;

    if (dialog_vars.default_item != 0) {
	int count = 0;
	while (items->name != 0) {
	    if (!strcmp(dialog_vars.default_item, items->name)) {
		result = count;
		break;
	    }
	    ++items;
	    count++;
	}
    }
    return result;
}

#define sTEXT -1

static int
next_valid_buttonindex(int state, int extra, bool non_editable)
{
    state = dlg_next_ok_buttonindex(state, extra);
    while (non_editable && state == sTEXT)
	state = dlg_next_ok_buttonindex(state, sTEXT);
    return state;
}

static int
prev_valid_buttonindex(int state, int extra, bool non_editable)
{
    state = dlg_prev_ok_buttonindex(state, extra);
    while (non_editable && state == sTEXT)
	state = dlg_prev_ok_buttonindex(state, sTEXT);
    return state;
}

#define NAVIGATE_BINDINGS \
	DLG_KEYS_DATA( DLGK_FIELD_NEXT, TAB ), \
	DLG_KEYS_DATA( DLGK_FIELD_PREV, KEY_BTAB ), \
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,  CHR_NEXT ), \
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,  KEY_DOWN ), \
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,  KEY_RIGHT ), \
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,  KEY_NEXT ), \
	DLG_KEYS_DATA( DLGK_ITEM_PREV,  CHR_PREVIOUS ), \
	DLG_KEYS_DATA( DLGK_ITEM_PREV,  KEY_PREVIOUS ), \
	DLG_KEYS_DATA( DLGK_ITEM_PREV,  KEY_LEFT ), \
	DLG_KEYS_DATA( DLGK_ITEM_PREV,  KEY_UP ), \
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,  KEY_NPAGE ), \
	DLG_KEYS_DATA( DLGK_PAGE_PREV,  KEY_PPAGE )
/*
 * Display a form for entering a number of fields
 */
int
dlg_form(const char *title,
	 const char *cprompt,
	 int height,
	 int width,
	 int form_height,
	 int item_no,
	 DIALOG_FORMITEM * items,
	 int *current_item)
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

    int form_width;
    bool first = TRUE;
    bool first_trace = TRUE;
    int chr_offset = 0;
    int state = (dialog_vars.default_button >= 0
		 ? dlg_default_button()
		 : sTEXT);
    int x, y, cur_x, cur_y, box_x, box_y;
    int code;
    int key = 0;
    int fkey;
    int choice = dlg_default_formitem(items);
    int new_choice, new_scroll;
    int scrollamt = 0;
    int result = DLG_EXIT_UNKNOWN;
    int min_width = 0, min_height = 0;
    bool was_autosize = (height == 0 || width == 0);
    bool show_buttons = FALSE;
    bool scroll_changed = FALSE;
    bool field_changed = FALSE;
    bool non_editable = FALSE;
    WINDOW *dialog, *form;
    char *prompt;
    const char **buttons = dlg_ok_labels();
    DIALOG_FORMITEM *current;

    DLG_TRACE(("# %sform args:\n", (dialog_vars.formitem_type
				    ? "password"
				    : "")));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("message", cprompt);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);
    DLG_TRACE2N("lheight", form_height);
    DLG_TRACE2N("llength", item_no);
    /* FIXME dump the items[][] too */
    DLG_TRACE2N("current", *current_item);

    make_FORM_ELTs(items, item_no, &min_height, &min_width);
    dlg_button_layout(buttons, &min_width);
    dlg_does_output();

#ifdef KEY_RESIZE
  retry:
#endif

    prompt = dlg_strclone(cprompt);
    dlg_tab_correct_str(prompt);
    dlg_auto_size(title, prompt, &height, &width,
		  1 + 3 * MARGIN,
		  MAX(26, 2 + min_width));

    if (form_height == 0)
	form_height = min_height;

    if (was_autosize) {
	form_height = MIN(SLINES - height, form_height);
	height += form_height;
    } else {
	int thigh = 0;
	int twide = 0;
	dlg_auto_size(title, prompt, &thigh, &twide, 0, width);
	thigh = SLINES - (height - (thigh + 1 + 3 * MARGIN));
	form_height = MIN(thigh, form_height);
    }

    dlg_print_size(height, width);
    dlg_ctl_size(height, width);

    x = dlg_box_x_ordinate(width);
    y = dlg_box_y_ordinate(height);

    dialog = dlg_new_window(height, width, y, x);
    dlg_register_window(dialog, "formbox", binding);
    dlg_register_buttons(dialog, "formbox", buttons);

    dlg_mouse_setbase(x, y);

    dlg_draw_box2(dialog, 0, 0, height, width, dialog_attr, border_attr, border2_attr);
    dlg_draw_bottom_box2(dialog, border_attr, border2_attr, dialog_attr);
    dlg_draw_title(dialog, title);

    dlg_attrset(dialog, dialog_attr);
    dlg_print_autowrap(dialog, prompt, height, width);

    form_width = width - 6;
    getyx(dialog, cur_y, cur_x);
    (void) cur_x;
    box_y = cur_y + 1;
    box_x = (width - form_width) / 2 - 1;

    /* create new window for the form */
    form = dlg_sub_window(dialog, form_height, form_width, y + box_y + 1,
			  x + box_x + 1);
    dlg_register_window(form, "formfield", binding2);

    /* draw a box around the form items */
    dlg_draw_box(dialog, box_y, box_x, form_height + 2, form_width + 2,
		 menubox_border_attr, menubox_border2_attr);

    /* register the new window, along with its borders */
    dlg_mouse_mkbigregion(getbegy(form) - getbegy(dialog),
			  getbegx(form) - getbegx(dialog),
			  getmaxy(form),
			  getmaxx(form),
			  KEY_MAX, 1, 1, 3 /* by cells */ );

    show_buttons = TRUE;
    scroll_changed = TRUE;

    choice = set_choice(items, choice, item_no, &non_editable);
    current = &items[choice];
    if (non_editable)
	state = next_valid_buttonindex(state, sTEXT, non_editable);

    while (result == DLG_EXIT_UNKNOWN) {
	int edit = FALSE;

	if (scroll_changed) {
	    print_form(form, items, item_no, scrollamt, choice);
	    dlg_draw_scrollbar(dialog,
			       scrollamt,
			       scrollamt,
			       scrollamt + form_height + 1,
			       min_height,
			       box_x + 1,
			       box_x + form_width,
			       box_y,
			       box_y + form_height + 1,
			       menubox_border2_attr,
			       menubox_border_attr);
	    scroll_changed = FALSE;
	}

	if (show_buttons) {
	    dlg_item_help("");
	    dlg_draw_buttons(dialog, height - 2, 0, buttons,
			     ((state < 0)
			      ? 1000	/* no such button, not highlighted */
			      : state),
			     FALSE, width);
	    show_buttons = FALSE;
	}

	if (first_trace) {
	    first_trace = FALSE;
	    dlg_trace_win(dialog);
	}

	if (field_changed || state == sTEXT) {
	    if (field_changed)
		chr_offset = 0;
	    current = &items[choice];
	    dialog_vars.max_input = current->text_ilen;
	    dlg_item_help(current->help);
	    dlg_show_string(form, current->text, chr_offset,
			    form_active_text_attr,
			    current->text_y - scrollamt,
			    current->text_x,
			    current->text_len,
			    is_hidden(current), first);
	    wsyncup(form);
	    wcursyncup(form);
	    field_changed = FALSE;
	}

	key = dlg_mouse_wgetch((state == sTEXT) ? form : dialog, &fkey);
	if (dlg_result_key(key, fkey, &result))
	    break;

	/* handle non-functionkeys */
	if (!fkey) {
	    if (state != sTEXT) {
		code = dlg_char_to_button(key, buttons);
		if (code >= 0) {
		    dlg_del_window(dialog);
		    result = dlg_ok_buttoncode(code);
		    continue;
		}
	    }
	}

	/* handle functionkeys */
	if (fkey) {
	    bool do_scroll = FALSE;
	    bool do_tab = FALSE;
	    int move_by = 0;

	    switch (key) {
	    case DLGK_MOUSE(KEY_PPAGE):
	    case DLGK_PAGE_PREV:
		do_scroll = TRUE;
		move_by = -form_height;
		break;

	    case DLGK_MOUSE(KEY_NPAGE):
	    case DLGK_PAGE_NEXT:
		do_scroll = TRUE;
		move_by = form_height;
		break;

	    case DLGK_TOGGLE:
	    case DLGK_ENTER:
		dlg_del_window(dialog);
		result = (state >= 0) ? dlg_enter_buttoncode(state) : DLG_EXIT_OK;
		continue;

	    case DLGK_GRID_LEFT:
		if (state == sTEXT)
		    break;
		/* FALLTHRU */
	    case DLGK_ITEM_PREV:
		if (state == sTEXT) {
		    do_tab = TRUE;
		    move_by = -1;
		    break;
		} else {
		    state = prev_valid_buttonindex(state, 0, non_editable);
		    show_buttons = TRUE;
		    continue;
		}

	    case DLGK_FORM_PREV:
		if (state == sTEXT && !is_first_field(items, choice)) {
		    do_tab = TRUE;
		    move_by = -1;
		    break;
		} else {
		    int old_state = state;
		    state = prev_valid_buttonindex(state, sTEXT, non_editable);
		    show_buttons = TRUE;
		    if (old_state >= 0 && state == sTEXT) {
			new_choice = item_no - 1;
			if (choice != new_choice) {
			    print_item(form, items + choice, scrollamt, FALSE);
			    choice = new_choice;
			}
		    }
		    continue;
		}

	    case DLGK_FIELD_PREV:
		state = prev_valid_buttonindex(state, sTEXT, non_editable);
		show_buttons = TRUE;
		continue;

	    case DLGK_FIELD_NEXT:
		state = next_valid_buttonindex(state, sTEXT, non_editable);
		show_buttons = TRUE;
		continue;

	    case DLGK_GRID_RIGHT:
		if (state == sTEXT)
		    break;
		/* FALLTHRU */

	    case DLGK_ITEM_NEXT:
		if (state == sTEXT) {
		    do_tab = TRUE;
		    move_by = 1;
		    break;
		} else {
		    state = next_valid_buttonindex(state, 0, non_editable);
		    show_buttons = TRUE;
		    continue;
		}

	    case DLGK_FORM_NEXT:
		if (state == sTEXT && !is_last_field(items, choice, item_no)) {
		    do_tab = TRUE;
		    move_by = 1;
		    break;
		} else {
		    state = next_valid_buttonindex(state, sTEXT, non_editable);
		    show_buttons = TRUE;
		    if (state == sTEXT && choice) {
			print_item(form, items + choice, scrollamt, FALSE);
			choice = 0;
		    }
		    continue;
		}

#ifdef KEY_RESIZE
	    case KEY_RESIZE:
		dlg_will_resize(dialog);
		/* reset data */
		height = old_height;
		width = old_width;
		free(prompt);
		dlg_clear();
		dlg_unregister_window(form);
		dlg_del_window(dialog);
		dlg_mouse_free_regions();
		/* repaint */
		goto retry;
#endif
	    default:
#if USE_MOUSE
		if (is_DLGK_MOUSE(key)) {
		    if (key >= DLGK_MOUSE(KEY_MAX)) {
			int cell = key - DLGK_MOUSE(KEY_MAX);
			int row = (cell / getmaxx(form)) + scrollamt;
			int col = (cell % getmaxx(form));
			int n;

			for (n = 0; n < item_no; ++n) {
			    if (items[n].name_y == row
				&& items[n].name_x <= col
				&& (items[n].name_x + items[n].name_len > col
				    || (items[n].name_y == items[n].text_y
					&& items[n].text_x > col))) {
				if (!is_readonly(&(items[n]))) {
				    field_changed = TRUE;
				    break;
				}
			    }
			    if (items[n].text_y == row
				&& items[n].text_x <= col
				&& items[n].text_x + items[n].text_ilen > col) {
				if (!is_readonly(&(items[n]))) {
				    field_changed = TRUE;
				    break;
				}
			    }
			}
			if (field_changed) {
			    print_item(form, items + choice, scrollamt, FALSE);
			    choice = n;
			    continue;
			}
			beep();
		    } else if ((code = dlg_ok_buttoncode(key - M_EVENT)) >= 0) {
			result = code;
		    }
		    continue;
		}
#endif
		break;
	    }

	    new_scroll = scrollamt;
	    new_choice = choice;
	    if (do_scroll) {
		if (scroll_next(form, items, move_by, &new_choice, &new_scroll)) {
		    if (choice != new_choice) {
			choice = new_choice;
			field_changed = TRUE;
		    }
		    if (scrollamt != new_scroll) {
			scrollamt = new_scroll;
			scroll_changed = TRUE;
		    }
		}
		continue;
	    }
	    if (do_tab) {
		if (tab_next(form, items, item_no, move_by, &new_choice, &new_scroll)) {
		    if (choice != new_choice) {
			choice = new_choice;
			field_changed = TRUE;
		    }
		    if (scrollamt != new_scroll) {
			scrollamt = new_scroll;
			scroll_changed = TRUE;
		    }
		}
		continue;
	    }
	}

	if (state == sTEXT) {	/* Input box selected */
	    if (!is_readonly(current))
		edit = dlg_edit_string(current->text, &chr_offset, key,
				       fkey, first);
	    if (edit) {
		dlg_show_string(form, current->text, chr_offset,
				form_active_text_attr,
				current->text_y - scrollamt,
				current->text_x,
				current->text_len,
				is_hidden(current), first);
		continue;
	    }
	}

    }

    dlg_mouse_free_regions();
    dlg_unregister_window(form);
    dlg_del_window(dialog);
    free(prompt);

    *current_item = choice;
    return result;
}

/*
 * Free memory owned by a list of DIALOG_FORMITEM's.
 */
void
dlg_free_formitems(DIALOG_FORMITEM * items)
{
    int n;
    for (n = 0; items[n].name != 0; ++n) {
	if (items[n].name_free)
	    free(items[n].name);
	if (items[n].text_free)
	    free(items[n].text);
	if (items[n].help_free && items[n].help != dlg_strempty())
	    free(items[n].help);
    }
    free(items);
}

/*
 * The script accepts values beginning at 1, while curses starts at 0.
 */
int
dlg_ordinate(const char *s)
{
    int result = atoi(s);
    if (result > 0)
	--result;
    else
	result = 0;
    return result;
}

int
dialog_form(const char *title,
	    const char *cprompt,
	    int height,
	    int width,
	    int form_height,
	    int item_no,
	    char **items)
{
    int result;
    int choice = 0;
    int i;
    DIALOG_FORMITEM *listitems;
    DIALOG_VARS save_vars;
    bool show_status = FALSE;
    char *help_result;

    dlg_save_vars(&save_vars);
    dialog_vars.separate_output = TRUE;

    listitems = dlg_calloc(DIALOG_FORMITEM, (size_t) item_no + 1);
    assert_ptr(listitems, "dialog_form");

    for (i = 0; i < item_no; ++i) {
	listitems[i].type = dialog_vars.formitem_type;
	listitems[i].name = ItemName(i);
	listitems[i].name_len = (int) strlen(ItemName(i));
	listitems[i].name_y = dlg_ordinate(ItemNameY(i));
	listitems[i].name_x = dlg_ordinate(ItemNameX(i));
	listitems[i].text = ItemText(i);
	listitems[i].text_len = (int) strlen(ItemText(i));
	listitems[i].text_y = dlg_ordinate(ItemTextY(i));
	listitems[i].text_x = dlg_ordinate(ItemTextX(i));
	listitems[i].text_flen = atoi(ItemTextFLen(i));
	listitems[i].text_ilen = atoi(ItemTextILen(i));
	listitems[i].help = ((dialog_vars.item_help)
			     ? ItemHelp(i)
			     : dlg_strempty());
    }

    result = dlg_form(title,
		      cprompt,
		      height,
		      width,
		      form_height,
		      item_no,
		      listitems,
		      &choice);

    switch (result) {
    case DLG_EXIT_OK:		/* FALLTHRU */
    case DLG_EXIT_EXTRA:
	show_status = TRUE;
	break;
    case DLG_EXIT_HELP:
	dlg_add_help_formitem(&result, &help_result, &listitems[choice]);
	show_status = dialog_vars.help_status;
	dlg_add_string(help_result);
	if (show_status)
	    dlg_add_separator();
	break;
    }
    if (show_status) {
	for (i = 0; i < item_no; i++) {
	    if (listitems[i].text_flen > 0) {
		dlg_add_string(listitems[i].text);
		dlg_add_separator();
	    }
	}
	dlg_add_last_key(-1);
    }

    dlg_free_formitems(listitems);
    dlg_restore_vars(&save_vars);

    return result;
}
