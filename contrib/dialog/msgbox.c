/*
 *  $Id: msgbox.c,v 1.81 2018/06/21 23:29:59 tom Exp $
 *
 *  msgbox.c -- implements the message box and info box
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
 *  An earlier version of this program lists as authors:
 *	Savio Lam (lam836@cs.cuhk.hk)
 */

#include <dialog.h>
#include <dlg_keys.h>

/*
 * Display a message box. Program will pause and display an "OK" button
 * if the parameter 'pauseopt' is non-zero.
 */
int
dialog_msgbox(const char *title, const char *cprompt, int height, int width,
	      int pauseopt)
{
    /* *INDENT-OFF* */
    static DLG_KEYS_BINDING binding[] = {
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	SCROLLKEY_BINDINGS,
	TRAVERSE_BINDINGS,
	END_KEYS_BINDING
    };
    /* *INDENT-ON* */

    int x, y, last = 0, page;
    int button;
    int key = 0, fkey;
    int result = DLG_EXIT_UNKNOWN;
    WINDOW *dialog = 0;
    char *prompt;
    const char **buttons = dlg_ok_label();
    int offset = 0;
    int check;
    bool show = TRUE;
    int min_width = (pauseopt == 1 ? 12 : 0);
    bool save_nocancel = dialog_vars.nocancel;
#ifdef KEY_RESIZE
    int req_high;
    int req_wide;
#endif

    DLG_TRACE(("# msgbox args:\n"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("message", cprompt);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);
    DLG_TRACE2N("pauseopt", pauseopt);

    dialog_vars.nocancel = TRUE;
    button = dlg_default_button();

#ifdef KEY_RESIZE
    req_high = height;
    req_wide = width;
  restart:
#endif

    dlg_button_layout(buttons, &min_width);

    prompt = dlg_strclone(cprompt);
    dlg_tab_correct_str(prompt);
    dlg_auto_size(title, prompt, &height, &width,
		  (pauseopt == 1 ? 2 : 0),
		  min_width);
    dlg_print_size(height, width);
    dlg_ctl_size(height, width);

    x = dlg_box_x_ordinate(width);
    y = dlg_box_y_ordinate(height);

#ifdef KEY_RESIZE
    if (dialog != 0)
	dlg_move_window(dialog, height, width, y, x);
    else
#endif
    {
	dialog = dlg_new_window(height, width, y, x);
	dlg_register_window(dialog, "msgbox", binding);
	dlg_register_buttons(dialog, "msgbox", buttons);
    }
    page = height - (1 + 3 * MARGIN);

    dlg_mouse_setbase(x, y);

    dlg_draw_box2(dialog, 0, 0, height, width, dialog_attr, border_attr, border2_attr);
    dlg_draw_title(dialog, title);

    dlg_attrset(dialog, dialog_attr);

    if (pauseopt) {
	dlg_draw_bottom_box2(dialog, border_attr, border2_attr, dialog_attr);
	mouse_mkbutton(height - 2, width / 2 - 4, 6, '\n');
	dlg_draw_buttons(dialog, height - 2, 0, buttons, button, FALSE, width);
	dlg_draw_helpline(dialog, FALSE);

	while (result == DLG_EXIT_UNKNOWN) {
	    if (show) {
		last = dlg_print_scrolled(dialog, prompt, offset,
					  page, width, pauseopt);
		dlg_trace_win(dialog);
		show = FALSE;
	    }
	    key = dlg_mouse_wgetch(dialog, &fkey);
	    if (dlg_result_key(key, fkey, &result))
		break;

	    if (!fkey && (check = dlg_char_to_button(key, buttons)) >= 0) {
		result = dlg_ok_buttoncode(check);
		break;
	    }

	    if (fkey) {
		switch (key) {
#ifdef KEY_RESIZE
		case KEY_RESIZE:
		    dlg_will_resize(dialog);
		    dlg_clear();
		    free(prompt);
		    height = req_high;
		    width = req_wide;
		    show = TRUE;
		    goto restart;
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
		    result = dlg_ok_buttoncode(button);
		    break;
		default:
		    if (is_DLGK_MOUSE(key)) {
			result = dlg_ok_buttoncode(key - M_EVENT);
			if (result < 0)
			    result = DLG_EXIT_OK;
		    } else if (dlg_check_scrolled(key,
						  last,
						  page,
						  &show,
						  &offset) == 0) {
		    } else {
			beep();
		    }
		    break;
		}
	    } else {
		beep();
	    }
	}
    } else {
	dlg_print_scrolled(dialog, prompt, offset, page, width, pauseopt);
	dlg_draw_helpline(dialog, FALSE);
	wrefresh(dialog);
	dlg_trace_win(dialog);
	result = DLG_EXIT_OK;
    }

    dlg_del_window(dialog);
    dlg_mouse_free_regions();
    free(prompt);

    dialog_vars.nocancel = save_nocancel;

    return result;
}
