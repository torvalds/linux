/*
 *  $Id: inputbox.c,v 1.84 2018/06/21 23:29:35 tom Exp $
 *
 *  inputbox.c -- implements the input box
 *
 *  Copyright 2000-2016,2018 Thomas E. Dickey
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

#define sTEXT -1

#define NAVIGATE_BINDINGS \
	DLG_KEYS_DATA( DLGK_FIELD_NEXT,	KEY_DOWN ), \
	DLG_KEYS_DATA( DLGK_FIELD_NEXT,	KEY_RIGHT ), \
	DLG_KEYS_DATA( DLGK_FIELD_NEXT,	TAB ), \
	DLG_KEYS_DATA( DLGK_FIELD_PREV,	KEY_BTAB ), \
	DLG_KEYS_DATA( DLGK_FIELD_PREV,	KEY_LEFT ), \
	DLG_KEYS_DATA( DLGK_FIELD_PREV,	KEY_UP )

/*
 * Display a dialog box for entering a string
 */
int
dialog_inputbox(const char *title, const char *cprompt, int height, int width,
		const char *init, const int password)
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
    int xorg, yorg;
    int x, y, box_y, box_x, box_width;
    int show_buttons;
    int col_offset = 0;
    int chr_offset = 0;
    int key, fkey, code;
    int result = DLG_EXIT_UNKNOWN;
    int state;
    bool first;
    bool edited;
    char *input;
    WINDOW *dialog;
    WINDOW *editor;
    char *prompt = dlg_strclone(cprompt);
    const char **buttons = dlg_ok_labels();

    dlg_does_output();

    DLG_TRACE(("# inputbox args:\n"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("message", cprompt);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);
    DLG_TRACE2S("init", init);
    DLG_TRACE2N("password", password);

    dlg_tab_correct_str(prompt);

    /* Set up the initial value */
    input = dlg_set_result(init);
    edited = FALSE;

#ifdef KEY_RESIZE
  retry:
#endif
    show_buttons = TRUE;
    state = dialog_vars.default_button >= 0 ? dlg_default_button() : sTEXT;
    first = (state == sTEXT);
    key = fkey = 0;

    if (init != NULL) {
	dlg_auto_size(title, prompt, &height, &width, 5,
		      MIN(MAX(dlg_count_columns(init) + 7, 26),
			  SCOLS - (dialog_vars.begin_set ?
				   dialog_vars.begin_x : 0)));
	chr_offset = (int) strlen(init);
    } else {
	dlg_auto_size(title, prompt, &height, &width, 5, 26);
    }
    dlg_button_layout(buttons, &width);
    dlg_print_size(height, width);
    dlg_ctl_size(height, width);

    xorg = dlg_box_x_ordinate(width);
    yorg = dlg_box_y_ordinate(height);

    dialog = dlg_new_window(height, width, yorg, xorg);
    dlg_register_window(dialog, "inputbox", binding);
    dlg_register_buttons(dialog, "inputbox", buttons);

    dlg_mouse_setbase(xorg, yorg);

    dlg_draw_box2(dialog, 0, 0, height, width, dialog_attr, border_attr, border2_attr);
    dlg_draw_bottom_box2(dialog, border_attr, border2_attr, dialog_attr);
    dlg_draw_title(dialog, title);

    dlg_attrset(dialog, dialog_attr);
    dlg_draw_helpline(dialog, FALSE);
    dlg_print_autowrap(dialog, prompt, height, width);

    /* Draw the input field box */
    box_width = width - 6;
    getyx(dialog, y, x);
    (void) x;
    box_y = y + 2;
    box_x = (width - box_width) / 2;
    dlg_mouse_mkregion(y + 1, box_x - 1, 3, box_width + 2, 'i');
    dlg_draw_box(dialog, y + 1, box_x - 1, 3, box_width + 2,
		 border_attr, border2_attr);

    /* Make a window for the input-field, to associate bindings */
    editor = dlg_sub_window(dialog, 1, box_width, yorg + box_y, xorg + box_x);
    dlg_register_window(editor, "inputbox2", binding2);

    if (*input != '\0') {
	dlg_show_string(editor, input, chr_offset, inputbox_attr,
			0, 0, box_width, (bool) (password != 0), first);
	wsyncup(editor);
	wcursyncup(editor);
    }
    while (result == DLG_EXIT_UNKNOWN) {
	int edit = 0;

	/*
	 * The last field drawn determines where the cursor is shown:
	 */
	if (show_buttons) {
	    show_buttons = FALSE;
	    col_offset = dlg_edit_offset(input, chr_offset, box_width);
	    (void) wmove(dialog, box_y, box_x + col_offset);
	    dlg_draw_buttons(dialog, height - 2, 0, buttons, state, FALSE, width);
	}

	if (!first) {
	    if (*input != '\0' && !edited) {
		dlg_show_string(editor, input, chr_offset, inputbox_attr,
				0, 0, box_width, (bool) (password != 0), first);
		wmove(editor, 0, chr_offset);
		wsyncup(editor);
		wcursyncup(editor);
	    }
	    key = dlg_mouse_wgetch((state == sTEXT) ? editor : dialog, &fkey);
	    if (dlg_result_key(key, fkey, &result))
		break;
	}

	/*
	 * Handle mouse clicks first, since we want to know if this is a button,
	 * or something that dlg_edit_string() should handle.
	 */
	if (fkey
	    && is_DLGK_MOUSE(key)
	    && (code = dlg_ok_buttoncode(key - M_EVENT)) >= 0) {
	    result = code;
	    continue;
	}

	if (state == sTEXT) {	/* Input box selected */
	    edit = dlg_edit_string(input, &chr_offset, key, fkey, first);

	    if (edit) {
		dlg_show_string(editor, input, chr_offset, inputbox_attr,
				0, 0, box_width, (bool) (password != 0), first);
		wsyncup(editor);
		wcursyncup(editor);
		first = FALSE;
		edited = TRUE;
		continue;
	    } else if (first) {
		first = FALSE;
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
	    case DLGK_MOUSE('i'):	/* mouse enter events */
		state = 0;
		/* FALLTHRU */
	    case DLGK_FIELD_PREV:
		show_buttons = TRUE;
		state = dlg_prev_ok_buttonindex(state, sTEXT);
		break;
	    case DLGK_FIELD_NEXT:
		show_buttons = TRUE;
		state = dlg_next_ok_buttonindex(state, sTEXT);
		break;
	    case DLGK_TOGGLE:
	    case DLGK_ENTER:
		dlg_del_window(dialog);
		result = (state >= 0) ? dlg_enter_buttoncode(state) : DLG_EXIT_OK;
		break;
#ifdef KEY_RESIZE
	    case KEY_RESIZE:
		dlg_will_resize(dialog);
		/* reset data */
		height = old_height;
		width = old_width;
		/* repaint */
		dlg_clear();
		dlg_del_window(dialog);
		refresh();
		dlg_mouse_free_regions();
		goto retry;
#endif
	    default:
		beep();
		break;
	    }
	} else {
	    beep();
	}
    }

    dlg_unregister_window(editor);
    dlg_del_window(dialog);
    dlg_mouse_free_regions();
    free(prompt);
    return result;
}
