/*
 *  $Id: pause.c,v 1.39 2018/06/19 22:57:01 tom Exp $
 *
 *  pause.c -- implements the pause dialog
 *
 *  Copyright 2004-2012,2018	Thomas E. Dickey
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
 *	Yura Kalinichenko
 */

#include <dialog.h>
#include <dlg_keys.h>

#define MY_TIMEOUT 50

#define MIN_HIGH (4)
#define MIN_WIDE (10 + 2 * (2 + MARGIN))
#define BTN_HIGH (1 + 2 * MARGIN)

/*
 * This is like gauge, but can be interrupted.
 *
 * A pause box displays a meter along the bottom of the box.  The meter
 * indicates how many seconds remain until the end of the pause.  The pause
 * exits when timeout is reached (status OK) or the user presses:
 *   OK button (status OK) 
 *   CANCEL button (status CANCEL)
 *   Esc key (status ESC)
 *
 */
int
dialog_pause(const char *title,
	     const char *cprompt,
	     int height,
	     int width,
	     int seconds)
{
    /* *INDENT-OFF* */
    static DLG_KEYS_BINDING binding[] = {
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	TRAVERSE_BINDINGS,
	END_KEYS_BINDING
    };
    /* *INDENT-ON* */

#ifdef KEY_RESIZE
    int old_height = height;
    int old_width = width;
#endif

    int i, x, y, step;
    int button = dlg_default_button();
    int seconds_orig;
    WINDOW *dialog;
    const char **buttons = dlg_ok_labels();
    bool have_buttons = (dlg_button_count(buttons) != 0);
    bool first;
    int key = 0, fkey;
    int result = DLG_EXIT_UNKNOWN;
    int button_high = (have_buttons ? BTN_HIGH : MARGIN);
    int gauge_y;
    char *prompt;
    int save_timeout = dialog_vars.timeout_secs;

    DLG_TRACE(("# pause args:\n"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("message", cprompt);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);
    DLG_TRACE2N("seconds", seconds);

    curs_set(0);

    dialog_vars.timeout_secs = 0;
    seconds_orig = (seconds > 0) ? seconds : 1;

#ifdef KEY_RESIZE
  retry:
#endif

    prompt = dlg_strclone(cprompt);
    dlg_tab_correct_str(prompt);

    if (have_buttons) {
	dlg_auto_size(title, prompt, &height, &width,
		      MIN_HIGH,
		      MIN_WIDE);
	dlg_button_layout(buttons, &width);
    } else {
	dlg_auto_size(title, prompt, &height, &width,
		      MIN_HIGH + MARGIN - BTN_HIGH,
		      MIN_WIDE);
    }
    gauge_y = height - button_high - (1 + 2 * MARGIN);
    dlg_print_size(height, width);
    dlg_ctl_size(height, width);

    /* center dialog box on screen */
    x = dlg_box_x_ordinate(width);
    y = dlg_box_y_ordinate(height);

    dialog = dlg_new_window(height, width, y, x);
    dlg_register_window(dialog, "pause", binding);
    dlg_register_buttons(dialog, "pause", buttons);

    dlg_mouse_setbase(x, y);
    nodelay(dialog, TRUE);

    first = TRUE;
    do {
	(void) werase(dialog);
	dlg_draw_box2(dialog, 0, 0, height, width, dialog_attr, border_attr, border2_attr);

	dlg_draw_title(dialog, title);
	dlg_draw_helpline(dialog, FALSE);

	dlg_attrset(dialog, dialog_attr);
	dlg_print_autowrap(dialog, prompt, height, width);

	dlg_draw_box2(dialog,
		      gauge_y, 2 + MARGIN,
		      2 + MARGIN, width - 2 * (2 + MARGIN),
		      dialog_attr,
		      border_attr,
		      border2_attr);

	/*
	 * Clear the area for the progress bar by filling it with spaces
	 * in the title-attribute, and write the percentage with that
	 * attribute.
	 */
	(void) wmove(dialog, gauge_y + MARGIN, 4);
	dlg_attrset(dialog, title_attr);

	for (i = 0; i < (width - 2 * (3 + MARGIN)); i++)
	    (void) waddch(dialog, ' ');

	(void) wmove(dialog, gauge_y + MARGIN, (width / 2) - 2);
	(void) wprintw(dialog, "%3d", seconds);

	/*
	 * Now draw a bar in reverse, relative to the background.
	 * The window attribute was useful for painting the background,
	 * but requires some tweaks to reverse it.
	 */
	x = (seconds * (width - 2 * (3 + MARGIN))) / seconds_orig;
	if ((title_attr & A_REVERSE) != 0) {
	    dlg_attroff(dialog, A_REVERSE);
	} else {
	    dlg_attrset(dialog, A_REVERSE);
	}
	(void) wmove(dialog, gauge_y + MARGIN, 4);
	for (i = 0; i < x; i++) {
	    chtype ch = winch(dialog);
	    if (title_attr & A_REVERSE) {
		ch &= ~A_REVERSE;
	    }
	    (void) waddch(dialog, ch);
	}

	mouse_mkbutton(height - 2, width / 2 - 4, 6, '\n');
	if (have_buttons) {
	    dlg_draw_bottom_box2(dialog, border_attr, border2_attr, dialog_attr);
	    dlg_draw_buttons(dialog, height - 2, 0, buttons, button, FALSE, width);
	}
	if (first) {
	    (void) wrefresh(dialog);
	    dlg_trace_win(dialog);
	    first = FALSE;
	}

	for (step = 0;
	     (result == DLG_EXIT_UNKNOWN) && (step < 1000);
	     step += MY_TIMEOUT) {

	    napms(MY_TIMEOUT);
	    key = dlg_mouse_wgetch_nowait(dialog, &fkey);
	    if (key == ERR) {
		;		/* ignore errors in nodelay mode */
	    } else {
		if (dlg_result_key(key, fkey, &result))
		    break;
	    }

	    switch (key) {
#ifdef KEY_RESIZE
	    case KEY_RESIZE:
		dlg_will_resize(dialog);
		dlg_clear();	/* fill the background */
		dlg_del_window(dialog);		/* delete this window */
		height = old_height;
		width = old_width;
		free(prompt);
		refresh();	/* get it all onto the terminal */
		goto retry;
#endif
	    case DLGK_FIELD_NEXT:
		button = dlg_next_button(buttons, button);
		if (button < 0)
		    button = 0;
		dlg_draw_buttons(dialog,
				 height - 2, 0,
				 buttons, button,
				 FALSE, width);
		break;
	    case DLGK_FIELD_PREV:
		button = dlg_prev_button(buttons, button);
		if (button < 0)
		    button = 0;
		dlg_draw_buttons(dialog,
				 height - 2, 0,
				 buttons, button,
				 FALSE, width);
		break;
	    case DLGK_ENTER:
		result = dlg_enter_buttoncode(button);
		break;
	    case ERR:
		break;
	    default:
		if (is_DLGK_MOUSE(key)) {
		    result = dlg_ok_buttoncode(key - M_EVENT);
		    if (result < 0)
			result = DLG_EXIT_OK;
		}
		break;
	    }
	}
    } while ((result == DLG_EXIT_UNKNOWN) && (seconds-- > 0));

    curs_set(1);
    dlg_mouse_free_regions();
    dlg_del_window(dialog);
    free(prompt);

    dialog_vars.timeout_secs = save_timeout;

    return ((result == DLG_EXIT_UNKNOWN) ? DLG_EXIT_OK : result);
}
