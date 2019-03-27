/*
 *  $Id: buttons.c,v 1.99 2018/06/18 22:11:16 tom Exp $
 *
 *  buttons.c -- draw buttons, e.g., OK/Cancel
 *
 *  Copyright 2000-2017,2018	Thomas E. Dickey
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

#ifdef NEED_WCHAR_H
#include <wchar.h>
#endif

#define MIN_BUTTON (-dialog_state.visit_cols)
#define CHR_BUTTON (!dialog_state.plain_buttons)

static void
center_label(char *buffer, int longest, const char *label)
{
    int len = dlg_count_columns(label);
    int left = 0, right = 0;

    *buffer = 0;
    if (len < longest) {
	left = (longest - len) / 2;
	right = (longest - len - left);
	if (left > 0)
	    sprintf(buffer, "%*s", left, " ");
    }
    strcat(buffer, label);
    if (right > 0)
	sprintf(buffer + strlen(buffer), "%*s", right, " ");
}

/*
 * Parse a multibyte character out of the string, set it past the parsed
 * character.
 */
static int
string_to_char(const char **stringp)
{
    int result;
#ifdef USE_WIDE_CURSES
    const char *string = *stringp;
    size_t have = strlen(string);
    size_t check;
    size_t len;
    wchar_t cmp2[2];
    mbstate_t state;

    memset(&state, 0, sizeof(state));
    len = mbrlen(string, have, &state);
    if ((int) len > 0 && len <= have) {
	memset(&state, 0, sizeof(state));
	memset(cmp2, 0, sizeof(cmp2));
	check = mbrtowc(cmp2, string, len, &state);
	if ((int) check <= 0)
	    cmp2[0] = 0;
	*stringp += len;
    } else {
	cmp2[0] = UCH(*string);
	*stringp += 1;
    }
    result = cmp2[0];
#else
    const char *string = *stringp;
    result = UCH(*string);
    *stringp += 1;
#endif
    return result;
}

static size_t
count_labels(const char **labels)
{
    size_t result = 0;
    if (labels != 0) {
	while (*labels++ != 0) {
	    ++result;
	}
    }
    return result;
}

/*
 * Check if the latest key should be added to the hotkey list.
 */
static int
was_hotkey(int this_key, int *used_keys, size_t next)
{
    int result = FALSE;

    if (next != 0) {
	size_t n;
	for (n = 0; n < next; ++n) {
	    if (used_keys[n] == this_key) {
		result = TRUE;
		break;
	    }
	}
    }
    return result;
}

/*
 * Determine the hot-keys for a set of button-labels.  Normally these are
 * the first uppercase character in each label.  However, if more than one
 * button has the same first-uppercase, then we will (attempt to) look for
 * an alternate.
 *
 * This allocates data which must be freed by the caller.
 */
static int *
get_hotkeys(const char **labels)
{
    int *result = 0;
    size_t count = count_labels(labels);
    size_t n;

    if ((result = dlg_calloc(int, count + 1)) != 0) {
	for (n = 0; n < count; ++n) {
	    const char *label = labels[n];
	    const int *indx = dlg_index_wchars(label);
	    int limit = dlg_count_wchars(label);
	    int i;

	    for (i = 0; i < limit; ++i) {
		int first = indx[i];
		int check = UCH(label[first]);
#ifdef USE_WIDE_CURSES
		int last = indx[i + 1];
		if ((last - first) != 1) {
		    const char *temp = (label + first);
		    check = string_to_char(&temp);
		}
#endif
		if (dlg_isupper(check) && !was_hotkey(check, result, n)) {
		    result[n] = check;
		    break;
		}
	    }
	}
    }
    return result;
}

typedef enum {
    sFIND_KEY = 0
    ,sHAVE_KEY = 1
    ,sHAD_KEY = 2
} HOTKEY;

/*
 * Print a button
 */
static void
print_button(WINDOW *win, char *label, int hotkey, int y, int x, int selected)
{
    int i;
    HOTKEY state = sFIND_KEY;
    const int *indx = dlg_index_wchars(label);
    int limit = dlg_count_wchars(label);
    chtype key_attr = (selected
		       ? button_key_active_attr
		       : button_key_inactive_attr);
    chtype label_attr = (selected
			 ? button_label_active_attr
			 : button_label_inactive_attr);

    (void) wmove(win, y, x);
    dlg_attrset(win, selected
		? button_active_attr
		: button_inactive_attr);
    (void) waddstr(win, "<");
    dlg_attrset(win, label_attr);
    for (i = 0; i < limit; ++i) {
	int check;
	int first = indx[i];
	int last = indx[i + 1];

	switch (state) {
	case sFIND_KEY:
	    check = UCH(label[first]);
#ifdef USE_WIDE_CURSES
	    if ((last - first) != 1) {
		const char *temp = (label + first);
		check = string_to_char(&temp);
	    }
#endif
	    if (check == hotkey) {
		dlg_attrset(win, key_attr);
		state = sHAVE_KEY;
	    }
	    break;
	case sHAVE_KEY:
	    dlg_attrset(win, label_attr);
	    state = sHAD_KEY;
	    break;
	default:
	    break;
	}
	waddnstr(win, label + first, last - first);
    }
    dlg_attrset(win, selected
		? button_active_attr
		: button_inactive_attr);
    (void) waddstr(win, ">");
    (void) wmove(win, y, x + ((int) (strspn) (label, " ")) + 1);
}

/*
 * Count the buttons in the list.
 */
int
dlg_button_count(const char **labels)
{
    int result = 0;
    while (*labels++ != 0)
	++result;
    return result;
}

/*
 * Compute the size of the button array in columns.  Return the total number of
 * columns in *length, and the longest button's columns in *longest
 */
void
dlg_button_sizes(const char **labels,
		 int vertical,
		 int *longest,
		 int *length)
{
    int n;

    *length = 0;
    *longest = 0;
    for (n = 0; labels[n] != 0; n++) {
	if (vertical) {
	    *length += 1;
	    *longest = 1;
	} else {
	    int len = dlg_count_columns(labels[n]);
	    if (len > *longest)
		*longest = len;
	    *length += len;
	}
    }
    /*
     * If we can, make all of the buttons the same size.  This is only optional
     * for buttons laid out horizontally.
     */
    if (*longest < 6 - (*longest & 1))
	*longest = 6 - (*longest & 1);
    if (!vertical)
	*length = *longest * n;
}

/*
 * Compute the size of the button array.
 */
int
dlg_button_x_step(const char **labels, int limit, int *gap, int *margin, int *step)
{
    int count = dlg_button_count(labels);
    int longest;
    int length;
    int unused;
    int used;
    int result;

    *margin = 0;
    if (count != 0) {
	dlg_button_sizes(labels, FALSE, &longest, &length);
	used = (length + (count * 2));
	unused = limit - used;

	if ((*gap = unused / (count + 3)) <= 0) {
	    if ((*gap = unused / (count + 1)) <= 0)
		*gap = 1;
	    *margin = *gap;
	} else {
	    *margin = *gap * 2;
	}
	*step = *gap + (used + count - 1) / count;
	result = (*gap > 0) && (unused >= 0);
    } else {
	result = 0;
    }
    return result;
}

/*
 * Make sure there is enough space for the buttons
 */
void
dlg_button_layout(const char **labels, int *limit)
{
    int width = 1;
    int gap, margin, step;

    if (labels != 0 && dlg_button_count(labels)) {
	while (!dlg_button_x_step(labels, width, &gap, &margin, &step))
	    ++width;
	width += (4 * MARGIN);
	if (width > COLS)
	    width = COLS;
	if (width > *limit)
	    *limit = width;
    }
}

/*
 * Print a list of buttons at the given position.
 */
void
dlg_draw_buttons(WINDOW *win,
		 int y, int x,
		 const char **labels,
		 int selected,
		 int vertical,
		 int limit)
{
    chtype save = dlg_get_attrs(win);
    int n;
    int step = 0;
    int length;
    int longest;
    int final_x;
    int final_y;
    int gap;
    int margin;
    size_t need;
    char *buffer;

    dlg_mouse_setbase(getbegx(win), getbegy(win));

    getyx(win, final_y, final_x);

    dlg_button_sizes(labels, vertical, &longest, &length);

    if (vertical) {
	y += 1;
	step = 1;
    } else {
	dlg_button_x_step(labels, limit, &gap, &margin, &step);
	x += margin;
    }

    /*
     * Allocate a buffer big enough for any label.
     */
    need = (size_t) longest;
    if (need != 0) {
	int *hotkeys = get_hotkeys(labels);
	assert_ptr(hotkeys, "dlg_draw_buttons");

	for (n = 0; labels[n] != 0; ++n) {
	    need += strlen(labels[n]) + 1;
	}
	buffer = dlg_malloc(char, need);
	assert_ptr(buffer, "dlg_draw_buttons");

	/*
	 * Draw the labels.
	 */
	for (n = 0; labels[n] != 0; n++) {
	    center_label(buffer, longest, labels[n]);
	    mouse_mkbutton(y, x, dlg_count_columns(buffer), n);
	    print_button(win, buffer,
			 CHR_BUTTON ? hotkeys[n] : -1,
			 y, x,
			 (selected == n) || (n == 0 && selected < 0));
	    if (selected == n)
		getyx(win, final_y, final_x);

	    if (vertical) {
		if ((y += step) > limit)
		    break;
	    } else {
		if ((x += step) > limit)
		    break;
	    }
	}
	(void) wmove(win, final_y, final_x);
	wrefresh(win);
	dlg_attrset(win, save);
	free(buffer);
	free(hotkeys);
    }
}

/*
 * Match a given character against the beginning of the string, ignoring case
 * of the given character.  The matching string must begin with an uppercase
 * character.
 */
int
dlg_match_char(int ch, const char *string)
{
    if (string != 0) {
	int cmp2 = string_to_char(&string);
#ifdef USE_WIDE_CURSES
	wint_t cmp1 = dlg_toupper(ch);
	if (cmp2 != 0 && (wchar_t) cmp1 == (wchar_t) dlg_toupper(cmp2)) {
	    return TRUE;
	}
#else
	if (ch > 0 && ch < 256) {
	    if (dlg_toupper(ch) == dlg_toupper(cmp2))
		return TRUE;
	}
#endif
    }
    return FALSE;
}

/*
 * Find the first uppercase character in the label, which we may use for an
 * abbreviation.
 */
int
dlg_button_to_char(const char *label)
{
    int cmp = -1;

    while (*label != 0) {
	cmp = string_to_char(&label);
	if (dlg_isupper(cmp)) {
	    break;
	}
    }
    return cmp;
}

/*
 * Given a list of button labels, and a character which may be the abbreviation
 * for one, find it, if it exists.  An abbreviation will be the first character
 * which happens to be capitalized in the label.
 */
int
dlg_char_to_button(int ch, const char **labels)
{
    int result = DLG_EXIT_UNKNOWN;

    if (labels != 0) {
	int *hotkeys = get_hotkeys(labels);
	int j;

	ch = (int) dlg_toupper(dlg_last_getc());

	if (hotkeys != 0) {
	    for (j = 0; labels[j] != 0; ++j) {
		if (ch == hotkeys[j]) {
		    dlg_flush_getc();
		    result = j;
		    break;
		}
	    }
	    free(hotkeys);
	}
    }

    return result;
}

static const char *
my_yes_label(void)
{
    return (dialog_vars.yes_label != NULL)
	? dialog_vars.yes_label
	: _("Yes");
}

static const char *
my_no_label(void)
{
    return (dialog_vars.no_label != NULL)
	? dialog_vars.no_label
	: _("No");
}

static const char *
my_ok_label(void)
{
    return (dialog_vars.ok_label != NULL)
	? dialog_vars.ok_label
	: _("OK");
}

static const char *
my_cancel_label(void)
{
    return (dialog_vars.cancel_label != NULL)
	? dialog_vars.cancel_label
	: _("Cancel");
}

static const char *
my_exit_label(void)
{
    return (dialog_vars.exit_label != NULL)
	? dialog_vars.exit_label
	: _("EXIT");
}

static const char *
my_extra_label(void)
{
    return (dialog_vars.extra_label != NULL)
	? dialog_vars.extra_label
	: _("Extra");
}

static const char *
my_help_label(void)
{
    return (dialog_vars.help_label != NULL)
	? dialog_vars.help_label
	: _("Help");
}

/*
 * Return a list of button labels.
 */
const char **
dlg_exit_label(void)
{
    const char **result;
    DIALOG_VARS save;

    if (dialog_vars.extra_button) {
	dlg_save_vars(&save);
	dialog_vars.nocancel = TRUE;
	result = dlg_ok_labels();
	dlg_restore_vars(&save);
    } else {
	static const char *labels[3];
	int n = 0;

	if (!dialog_vars.nook)
	    labels[n++] = my_exit_label();
	if (dialog_vars.help_button)
	    labels[n++] = my_help_label();
	if (n == 0)
	    labels[n++] = my_exit_label();
	labels[n] = 0;

	result = labels;
    }
    return result;
}

/*
 * Map the given button index for dlg_exit_label() into our exit-code.
 */
int
dlg_exit_buttoncode(int button)
{
    int result;
    DIALOG_VARS save;

    dlg_save_vars(&save);
    dialog_vars.nocancel = TRUE;

    result = dlg_ok_buttoncode(button);

    dlg_restore_vars(&save);

    return result;
}

const char **
dlg_ok_label(void)
{
    static const char *labels[4];
    int n = 0;

    labels[n++] = my_ok_label();
    if (dialog_vars.extra_button)
	labels[n++] = my_extra_label();
    if (dialog_vars.help_button)
	labels[n++] = my_help_label();
    labels[n] = 0;
    return labels;
}

/*
 * Return a list of button labels for the OK/Cancel group.
 */
const char **
dlg_ok_labels(void)
{
    static const char *labels[5];
    int n = 0;

    if (!dialog_vars.nook)
	labels[n++] = my_ok_label();
    if (dialog_vars.extra_button)
	labels[n++] = my_extra_label();
    if (!dialog_vars.nocancel)
	labels[n++] = my_cancel_label();
    if (dialog_vars.help_button)
	labels[n++] = my_help_label();
    labels[n] = 0;
    return labels;
}

/*
 * Map the given button index for dlg_ok_labels() into our exit-code
 */
int
dlg_ok_buttoncode(int button)
{
    int result = DLG_EXIT_ERROR;
    int n = !dialog_vars.nook;

    if (!dialog_vars.nook && (button <= 0)) {
	result = DLG_EXIT_OK;
    } else if (dialog_vars.extra_button && (button == n++)) {
	result = DLG_EXIT_EXTRA;
    } else if (!dialog_vars.nocancel && (button == n++)) {
	result = DLG_EXIT_CANCEL;
    } else if (dialog_vars.help_button && (button == n)) {
	result = DLG_EXIT_HELP;
    }
    DLG_TRACE(("# dlg_ok_buttoncode(%d) = %d\n", button, result));
    return result;
}

/*
 * Given that we're using dlg_ok_labels() to list buttons, find the next index
 * in the list of buttons.  The 'extra' parameter if negative provides a way to
 * enumerate extra active areas on the widget.
 */
int
dlg_next_ok_buttonindex(int current, int extra)
{
    int result = current + 1;

    if (current >= 0
	&& dlg_ok_buttoncode(result) < 0)
	result = extra;
    return result;
}

/*
 * Similarly, find the previous button index.
 */
int
dlg_prev_ok_buttonindex(int current, int extra)
{
    int result = current - 1;

    if (result < extra) {
	for (result = 0; dlg_ok_buttoncode(result + 1) >= 0; ++result) {
	    ;
	}
    }
    return result;
}

/*
 * Find the button-index for the "OK" or "Cancel" button, according to
 * whether --defaultno is given.  If --nocancel was given, we always return
 * the index for the first button (usually "OK" unless --nook was used).
 */
int
dlg_defaultno_button(void)
{
    int result = 0;

    if (dialog_vars.defaultno && !dialog_vars.nocancel) {
	while (dlg_ok_buttoncode(result) != DLG_EXIT_CANCEL)
	    ++result;
    }
    DLG_TRACE(("# dlg_defaultno_button() = %d\n", result));
    return result;
}

/*
 * Find the button-index for a button named with --default-button. If the
 * option was not specified, or if the selected button does not exist, return
 * the index of the first button (usually "OK" unless --nook was used).
 */
int
dlg_default_button(void)
{
    int i, n;
    int result = 0;

    if (dialog_vars.default_button >= 0) {
	for (i = 0; (n = dlg_ok_buttoncode(i)) >= 0; i++) {
	    if (n == dialog_vars.default_button) {
		result = i;
		break;
	    }
	}
    }
    DLG_TRACE(("# dlg_default_button() = %d\n", result));
    return result;
}

/*
 * Return a list of buttons for Yes/No labels.
 */
const char **
dlg_yes_labels(void)
{
    const char **result;

    if (dialog_vars.extra_button) {
	result = dlg_ok_labels();
    } else {
	static const char *labels[4];
	int n = 0;

	labels[n++] = my_yes_label();
	labels[n++] = my_no_label();
	if (dialog_vars.help_button)
	    labels[n++] = my_help_label();
	labels[n] = 0;

	result = labels;
    }

    return result;
}

/*
 * Map the given button index for dlg_yes_labels() into our exit-code.
 */
int
dlg_yes_buttoncode(int button)
{
    int result = DLG_EXIT_ERROR;

    if (dialog_vars.extra_button) {
	result = dlg_ok_buttoncode(button);
    } else if (button == 0) {
	result = DLG_EXIT_OK;
    } else if (button == 1) {
	result = DLG_EXIT_CANCEL;
    } else if (button == 2 && dialog_vars.help_button) {
	result = DLG_EXIT_HELP;
    }

    return result;
}

/*
 * Return the next index in labels[];
 */
int
dlg_next_button(const char **labels, int button)
{
    if (button < -1)
	button = -1;

    if (labels[button + 1] != 0) {
	++button;
    } else {
	button = MIN_BUTTON;
    }
    return button;
}

/*
 * Return the previous index in labels[];
 */
int
dlg_prev_button(const char **labels, int button)
{
    if (button > MIN_BUTTON) {
	--button;
    } else {
	if (button < -1)
	    button = -1;

	while (labels[button + 1] != 0)
	    ++button;
    }
    return button;
}
