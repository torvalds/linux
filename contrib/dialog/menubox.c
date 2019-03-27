/*
 *  $Id: menubox.c,v 1.159 2018/06/21 23:28:56 tom Exp $
 *
 *  menubox.c -- implements the menu box
 *
 *  Copyright 2000-2016,2018	Thomas E. Dickey
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public Licens, version 2.1e
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
 *	Savio Lam (lam836@cs.cuhk.hk)
 */

#include <dialog.h>
#include <dlg_keys.h>

typedef enum {
    Unselected = 0,
    Selected,
    Editing
} Mode;

typedef struct {
    /* the outer-window */
    WINDOW *dialog;
    int box_y;
    int box_x;
    int tag_x;
    int item_x;
    int menu_height;
    int menu_width;
    /* the inner-window */
    WINDOW *menu;
    DIALOG_LISTITEM *items;
    int item_no;
} ALL_DATA;

#define MIN_HIGH  (1 + (5 * MARGIN))

#define INPUT_ROWS     3	/* rows per inputmenu entry */

#define RowHeight(i) (is_inputmenu ? ((i) * INPUT_ROWS) : ((i) * 1))
#define ItemToRow(i) (is_inputmenu ? ((i) * INPUT_ROWS + 1) : (i))
#define RowToItem(i) (is_inputmenu ? ((i) / INPUT_ROWS + 0) : (i))

/*
 * Print menu item
 */
static void
print_item(ALL_DATA * data,
	   WINDOW *win,
	   DIALOG_LISTITEM * item,
	   int choice,
	   Mode selected,
	   bool is_inputmenu)
{
    chtype save = dlg_get_attrs(win);
    int n;
    int climit = (data->item_x - data->tag_x - GUTTER);
    int my_width = data->menu_width;
    int my_x = data->item_x;
    int my_y = ItemToRow(choice);
    bool both = (!dialog_vars.no_tags && !dialog_vars.no_items);
    bool first = TRUE;
    chtype bordchar;
    const char *show = (dialog_vars.no_items
			? item->name
			: item->text);

    switch (selected) {
    default:
    case Unselected:
	bordchar = item_attr;
	break;
    case Selected:
	bordchar = item_selected_attr;
	break;
    case Editing:
	bordchar = dialog_attr;
	break;
    }

    /* Clear 'residue' of last item and mark current current item */
    if (is_inputmenu) {
	dlg_attrset(win, (selected != Unselected) ? item_selected_attr : item_attr);
	for (n = my_y - 1; n < my_y + INPUT_ROWS - 1; n++) {
	    wmove(win, n, 0);
	    wprintw(win, "%*s", my_width, " ");
	}
    } else {
	dlg_attrset(win, menubox_attr);
	wmove(win, my_y, 0);
	wprintw(win, "%*s", my_width, " ");
    }

    /* highlight first char of the tag to be special */
    if (both) {
	(void) wmove(win, my_y, data->tag_x);
	dlg_print_listitem(win, item->name, climit, first, selected);
	first = FALSE;
    }

    /* Draw the input field box (only for inputmenu) */
    (void) wmove(win, my_y, my_x);
    if (is_inputmenu) {
	my_width -= 1;
	dlg_draw_box(win, my_y - 1, my_x, INPUT_ROWS, my_width - my_x - data->tag_x,
		     bordchar,
		     bordchar);
	my_width -= 1;
	++my_x;
    }

    /* print actual item */
    wmove(win, my_y, my_x);
    dlg_print_listitem(win, show, my_width - my_x, first, selected);

    if (selected) {
	dlg_item_help(item->help);
    }
    dlg_attrset(win, save);
}

/*
 * Allow the user to edit the text of a menu entry.
 */
static int
input_menu_edit(ALL_DATA * data,
		DIALOG_LISTITEM * items,
		int choice,
		char **resultp)
{
    chtype save = dlg_get_attrs(data->menu);
    char *result;
    int offset = 0;
    int key = 0, fkey = 0;
    bool first = TRUE;
    /* see above */
    bool is_inputmenu = TRUE;
    int y = ItemToRow(choice);
    int code = TRUE;
    int max_len = dlg_max_input(MAX((int) strlen(items->text) + 1, MAX_LEN));

    result = dlg_malloc(char, (size_t) max_len);
    assert_ptr(result, "input_menu_edit");

    /* original item is used to initialize the input string. */
    result[0] = '\0';
    strcpy(result, items->text);

    print_item(data, data->menu, items, choice, Editing, TRUE);

    /* taken out of inputbox.c - but somewhat modified */
    for (;;) {
	if (!first)
	    key = dlg_mouse_wgetch(data->menu, &fkey);
	if (dlg_edit_string(result, &offset, key, fkey, first)) {
	    dlg_show_string(data->menu, result, offset, inputbox_attr,
			    y,
			    data->item_x + 1,
			    data->menu_width - data->item_x - 3,
			    FALSE, first);
	    first = FALSE;
	} else if (key == ESC || key == TAB) {
	    code = FALSE;
	    break;
	} else {
	    break;
	}
    }
    print_item(data, data->menu, items, choice, Selected, TRUE);
    dlg_attrset(data->menu, save);

    *resultp = result;
    return code;
}

static int
handle_button(int code, DIALOG_LISTITEM * items, int choice)
{
    char *help_result;

    switch (code) {
    case DLG_EXIT_OK:		/* FALLTHRU */
    case DLG_EXIT_EXTRA:
	dlg_add_string(items[choice].name);
	break;
    case DLG_EXIT_HELP:
	dlg_add_help_listitem(&code, &help_result, &items[choice]);
	dlg_add_string(help_result);
	break;
    }
    return code;
}

int
dlg_renamed_menutext(DIALOG_LISTITEM * items, int current, char *newtext)
{
    if (dialog_vars.input_result)
	dialog_vars.input_result[0] = '\0';
    dlg_add_result("RENAMED ");
    dlg_add_string(items[current].name);
    dlg_add_result(" ");
    dlg_add_string(newtext);
    return DLG_EXIT_EXTRA;
}

int
dlg_dummy_menutext(DIALOG_LISTITEM * items, int current, char *newtext)
{
    (void) items;
    (void) current;
    (void) newtext;
    return DLG_EXIT_ERROR;
}

static void
print_menu(ALL_DATA * data, int choice, int scrollamt, int max_choice, bool is_inputmenu)
{
    int i;

    for (i = 0; i < max_choice; i++) {
	print_item(data,
		   data->menu,
		   &data->items[i + scrollamt],
		   i,
		   (i == choice) ? Selected : Unselected,
		   is_inputmenu);
    }

    /* Clean bottom lines */
    if (is_inputmenu) {
	int spare_lines, x_count;
	spare_lines = data->menu_height % INPUT_ROWS;
	dlg_attrset(data->menu, menubox_attr);
	for (; spare_lines; spare_lines--) {
	    wmove(data->menu, data->menu_height - spare_lines, 0);
	    for (x_count = 0; x_count < data->menu_width;
		 x_count++) {
		waddch(data->menu, ' ');
	    }
	}
    }

    (void) wnoutrefresh(data->menu);

    dlg_draw_scrollbar(data->dialog,
		       scrollamt,
		       scrollamt,
		       scrollamt + max_choice,
		       data->item_no,
		       data->box_x,
		       data->box_x + data->menu_width,
		       data->box_y,
		       data->box_y + data->menu_height + 1,
		       menubox_border2_attr,
		       menubox_border_attr);
}

static bool
check_hotkey(DIALOG_LISTITEM * items, int choice)
{
    bool result = FALSE;

    if (dlg_match_char(dlg_last_getc(),
		       (dialog_vars.no_tags
			? items[choice].text
			: items[choice].name))) {
	result = TRUE;
    }
    return result;
}

/*
 * This is an alternate interface to 'menu' which allows the application
 * to read the list item states back directly without putting them in the
 * output buffer.
 */
int
dlg_menu(const char *title,
	 const char *cprompt,
	 int height,
	 int width,
	 int menu_height,
	 int item_no,
	 DIALOG_LISTITEM * items,
	 int *current_item,
	 DIALOG_INPUTMENU rename_menutext)
{
    /* *INDENT-OFF* */
    static DLG_KEYS_BINDING binding[] = {
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	TOGGLEKEY_BINDINGS,
	DLG_KEYS_DATA( DLGK_FIELD_NEXT,	KEY_RIGHT ),
	DLG_KEYS_DATA( DLGK_FIELD_NEXT,	TAB ),
	DLG_KEYS_DATA( DLGK_FIELD_PREV,	KEY_BTAB ),
	DLG_KEYS_DATA( DLGK_FIELD_PREV,	KEY_LEFT ),
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,	'+' ),
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,	KEY_DOWN ),
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,  CHR_NEXT ),
	DLG_KEYS_DATA( DLGK_ITEM_PREV,	'-' ),
	DLG_KEYS_DATA( DLGK_ITEM_PREV,	KEY_UP ),
	DLG_KEYS_DATA( DLGK_ITEM_PREV,  CHR_PREVIOUS ),
	DLG_KEYS_DATA( DLGK_PAGE_FIRST,	KEY_HOME ),
	DLG_KEYS_DATA( DLGK_PAGE_LAST,	KEY_END ),
	DLG_KEYS_DATA( DLGK_PAGE_LAST,	KEY_LL ),
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,	KEY_NPAGE ),
	DLG_KEYS_DATA( DLGK_PAGE_PREV,	KEY_PPAGE ),
	END_KEYS_BINDING
    };
    static DLG_KEYS_BINDING binding2[] = {
	INPUTSTR_BINDINGS,
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	END_KEYS_BINDING
    };
    /* *INDENT-ON* */

#ifdef KEY_RESIZE
    int old_LINES = LINES;
    int old_COLS = COLS;
    int old_height = height;
    int old_width = width;
#endif
    ALL_DATA all;
    int i, j, x, y, cur_x, cur_y;
    int key = 0, fkey;
    int button = dialog_state.visit_items ? -1 : dlg_default_button();
    int choice = dlg_default_listitem(items);
    int result = DLG_EXIT_UNKNOWN;
    int scrollamt = 0;
    int max_choice;
    int found;
    int use_width, name_width, text_width, list_width;
    WINDOW *dialog, *menu;
    char *prompt = 0;
    const char **buttons = dlg_ok_labels();
    bool is_inputmenu = ((rename_menutext != 0)
			 && (rename_menutext != dlg_dummy_menutext));

    DLG_TRACE(("# menubox args:\n"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("message", cprompt);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);
    DLG_TRACE2N("lheight", menu_height);
    DLG_TRACE2N("llength", item_no);
    /* FIXME dump the items[][] too */
    DLG_TRACE2N("current", *current_item);
    DLG_TRACE2N("rename", rename_menutext != 0);

    dialog_state.plain_buttons = TRUE;

    all.items = items;
    all.item_no = item_no;

    dlg_does_output();

#ifdef KEY_RESIZE
  retry:
#endif

    prompt = dlg_strclone(cprompt);
    dlg_tab_correct_str(prompt);

    all.menu_height = menu_height;
    use_width = dlg_calc_list_width(item_no, items) + 10;
    use_width = MAX(26, use_width);
    if (all.menu_height == 0) {
	/* calculate height without items (4) */
	dlg_auto_size(title, prompt, &height, &width, MIN_HIGH, use_width);
	dlg_calc_listh(&height, &all.menu_height, item_no);
    } else {
	dlg_auto_size(title, prompt,
		      &height, &width,
		      MIN_HIGH + all.menu_height, use_width);
    }
    dlg_button_layout(buttons, &width);
    dlg_print_size(height, width);
    dlg_ctl_size(height, width);

    x = dlg_box_x_ordinate(width);
    y = dlg_box_y_ordinate(height);

    dialog = dlg_new_window(height, width, y, x);
    all.dialog = dialog;

    dlg_register_window(dialog, "menubox", binding);
    dlg_register_buttons(dialog, "menubox", buttons);

    dlg_mouse_setbase(x, y);

    dlg_draw_box2(dialog, 0, 0, height, width, dialog_attr, border_attr, border2_attr);
    dlg_draw_bottom_box2(dialog, border_attr, border2_attr, dialog_attr);
    dlg_draw_title(dialog, title);

    dlg_attrset(dialog, dialog_attr);
    dlg_print_autowrap(dialog, prompt, height, width);

    all.menu_width = width - 6;
    getyx(dialog, cur_y, cur_x);
    all.box_y = cur_y + 1;
    all.box_x = (width - all.menu_width) / 2 - 1;

    /*
     * After displaying the prompt, we know how much space we really have.
     * Limit the list to avoid overwriting the ok-button.
     */
    if (all.menu_height + MIN_HIGH > height - cur_y)
	all.menu_height = height - MIN_HIGH - cur_y;
    if (all.menu_height <= 0)
	all.menu_height = 1;

    /* Find out maximal number of displayable items at once. */
    max_choice = MIN(all.menu_height,
		     RowHeight(item_no));
    if (is_inputmenu)
	max_choice /= INPUT_ROWS;

    /* create new window for the menu */
    menu = dlg_sub_window(dialog, all.menu_height, all.menu_width,
			  y + all.box_y + 1,
			  x + all.box_x + 1);
    all.menu = menu;

    dlg_register_window(menu, "menu", binding2);
    dlg_register_buttons(menu, "menu", buttons);

    /* draw a box around the menu items */
    dlg_draw_box(dialog,
		 all.box_y, all.box_x,
		 all.menu_height + 2, all.menu_width + 2,
		 menubox_border_attr, menubox_border2_attr);

    name_width = 0;
    text_width = 0;

    /* Find length of longest item to center menu  *
     * only if --menu was given, using --inputmenu *
     * won't be centered.                         */
    for (i = 0; i < item_no; i++) {
	name_width = MAX(name_width, dlg_count_columns(items[i].name));
	text_width = MAX(text_width, dlg_count_columns(items[i].text));
    }

    /* If the name+text is wider than the list is allowed, then truncate
     * one or both of them.  If the name is no wider than 30% of the list,
     * leave it intact.
     *
     * FIXME: the gutter width and name/list ratio should be configurable.
     */
    use_width = (all.menu_width - GUTTER);
    if (dialog_vars.no_tags) {
	list_width = MIN(use_width, text_width);
    } else if (dialog_vars.no_items) {
	list_width = MIN(use_width, name_width);
    } else {
	if (text_width >= 0
	    && name_width >= 0
	    && use_width > 0
	    && text_width + name_width > use_width) {
	    int need = (int) (0.30 * use_width);
	    if (name_width > need) {
		int want = (int) (use_width
				  * ((double) name_width)
				  / (text_width + name_width));
		name_width = (want > need) ? want : need;
	    }
	    text_width = use_width - name_width;
	}
	list_width = (text_width + name_width);
    }

    all.tag_x = (is_inputmenu
		 ? 0
		 : (use_width - list_width) / 2);
    all.item_x = ((dialog_vars.no_tags
		   ? 0
		   : (dialog_vars.no_items
		      ? 0
		      : (GUTTER + name_width)))
		  + all.tag_x);

    if (choice - scrollamt >= max_choice) {
	scrollamt = choice - (max_choice - 1);
	choice = max_choice - 1;
    }

    print_menu(&all, choice, scrollamt, max_choice, is_inputmenu);

    /* register the new window, along with its borders */
    dlg_mouse_mkbigregion(all.box_y + 1, all.box_x,
			  all.menu_height + 2, all.menu_width + 2,
			  KEY_MAX, 1, 1, 1 /* by lines */ );

    dlg_draw_buttons(dialog, height - 2, 0, buttons, button, FALSE, width);

    dlg_trace_win(dialog);
    while (result == DLG_EXIT_UNKNOWN) {
	if (button < 0)		/* --visit-items */
	    wmove(dialog,
		  all.box_y + ItemToRow(choice) + 1,
		  all.box_x + all.tag_x + 1);

	key = dlg_mouse_wgetch(dialog, &fkey);
	if (dlg_result_key(key, fkey, &result))
	    break;

	found = FALSE;
	if (fkey) {
	    /*
	     * Allow a mouse-click on a box to switch selection to that box.
	     * Handling a button click is a little more complicated, since we
	     * push a KEY_ENTER back onto the input stream so we'll put the
	     * cursor at the right place before handling the "keypress".
	     */
	    if (key >= DLGK_MOUSE(KEY_MAX)) {
		key -= DLGK_MOUSE(KEY_MAX);
		i = RowToItem(key);
		if (i < max_choice) {
		    found = TRUE;
		} else {
		    beep();
		    continue;
		}
	    } else if (is_DLGK_MOUSE(key)
		       && dlg_ok_buttoncode(key - M_EVENT) >= 0) {
		button = (key - M_EVENT);
		ungetch('\n');
		continue;
	    }
	} else {
	    /*
	     * Check if key pressed matches first character of any item tag in
	     * list.  If there is more than one match, we will cycle through
	     * each one as the same key is pressed repeatedly.
	     */
	    if (button < 0 || !dialog_state.visit_items) {
		for (j = scrollamt + choice + 1; j < item_no; j++) {
		    if (check_hotkey(items, j)) {
			found = TRUE;
			i = j - scrollamt;
			break;
		    }
		}
		if (!found) {
		    for (j = 0; j <= scrollamt + choice; j++) {
			if (check_hotkey(items, j)) {
			    found = TRUE;
			    i = j - scrollamt;
			    break;
			}
		    }
		}
		if (found)
		    dlg_flush_getc();
	    } else if ((j = dlg_char_to_button(key, buttons)) >= 0) {
		button = j;
		ungetch('\n');
		continue;
	    }

	    /*
	     * A single digit (1-9) positions the selection to that line in the
	     * current screen.
	     */
	    if (!found
		&& (key <= '9')
		&& (key > '0')
		&& (key - '1' < max_choice)) {
		found = TRUE;
		i = key - '1';
	    }
	}

	if (!found && fkey) {
	    found = TRUE;
	    switch (key) {
	    case DLGK_PAGE_FIRST:
		i = -scrollamt;
		break;
	    case DLGK_PAGE_LAST:
		i = item_no - 1 - scrollamt;
		break;
	    case DLGK_MOUSE(KEY_PPAGE):
	    case DLGK_PAGE_PREV:
		if (choice)
		    i = 0;
		else if (scrollamt != 0)
		    i = -MIN(scrollamt, max_choice);
		else
		    continue;
		break;
	    case DLGK_MOUSE(KEY_NPAGE):
	    case DLGK_PAGE_NEXT:
		i = MIN(choice + max_choice, item_no - scrollamt - 1);
		break;
	    case DLGK_ITEM_PREV:
		i = choice - 1;
		if (choice == 0 && scrollamt == 0)
		    continue;
		break;
	    case DLGK_ITEM_NEXT:
		i = choice + 1;
		if (scrollamt + choice >= item_no - 1)
		    continue;
		break;
	    default:
		found = FALSE;
		break;
	    }
	}

	if (found) {
	    if (i != choice) {
		getyx(dialog, cur_y, cur_x);
		if (i < 0 || i >= max_choice) {
		    if (i < 0) {
			scrollamt += i;
			choice = 0;
		    } else {
			choice = max_choice - 1;
			scrollamt += (i - max_choice + 1);
		    }
		    print_menu(&all, choice, scrollamt, max_choice, is_inputmenu);
		} else {
		    choice = i;
		    print_menu(&all, choice, scrollamt, max_choice, is_inputmenu);
		    (void) wmove(dialog, cur_y, cur_x);
		    wrefresh(dialog);
		}
	    }
	    continue;		/* wait for another key press */
	}

	if (fkey) {
	    switch (key) {
	    case DLGK_FIELD_PREV:
		button = dlg_prev_button(buttons, button);
		dlg_draw_buttons(dialog, height - 2, 0, buttons, button,
				 FALSE, width);
		break;
	    case DLGK_FIELD_NEXT:
		button = dlg_next_button(buttons, button);
		dlg_draw_buttons(dialog, height - 2, 0, buttons, button,
				 FALSE, width);
		break;
	    case DLGK_TOGGLE:
	    case DLGK_ENTER:
		if (is_inputmenu)
		    result = dlg_ok_buttoncode(button);
		else
		    result = dlg_enter_buttoncode(button);

		/*
		 * If dlg_menu() is called from dialog_menu(), we want to
		 * capture the results into dialog_vars.input_result.
		 */
		if (result == DLG_EXIT_ERROR) {
		    result = DLG_EXIT_UNKNOWN;
		} else if (is_inputmenu
			   || rename_menutext == dlg_dummy_menutext) {
		    result = handle_button(result,
					   items,
					   scrollamt + choice);
		}

		/*
		 * If we have a rename_menutext function, interpret the Extra
		 * button as a request to rename the menu's text.  If that
		 * function doesn't return "Unknown", we will exit from this
		 * function.  Usually that is done for dialog_menu(), so the
		 * shell script can use the updated value.  If it does return
		 * "Unknown", update the list item only.  A direct caller of
		 * dlg_menu() can free the renamed value - we cannot.
		 */
		if (is_inputmenu && result == DLG_EXIT_EXTRA) {
		    char *tmp;

		    if (input_menu_edit(&all,
					&items[scrollamt + choice],
					choice,
					&tmp)) {
			result = rename_menutext(items, scrollamt + choice, tmp);
			if (result == DLG_EXIT_UNKNOWN) {
			    items[scrollamt + choice].text = tmp;
			} else {
			    free(tmp);
			}
		    } else {
			result = DLG_EXIT_UNKNOWN;
			print_item(&all,
				   menu,
				   &items[scrollamt + choice],
				   choice,
				   Selected,
				   is_inputmenu);
			(void) wnoutrefresh(menu);
			free(tmp);
		    }

		    if (result == DLG_EXIT_UNKNOWN) {
			dlg_draw_buttons(dialog, height - 2, 0,
					 buttons, button, FALSE, width);
		    }
		}
		break;
#ifdef KEY_RESIZE
	    case KEY_RESIZE:
		dlg_will_resize(dialog);
		/* reset data */
#define resizeit(name, NAME) \
		name = ((NAME >= old_##NAME) \
			? (NAME - (old_##NAME - old_##name)) \
			: old_##name)
		resizeit(height, LINES);
		resizeit(width, COLS);
		free(prompt);
		dlg_clear();
		dlg_del_window(dialog);
		dlg_mouse_free_regions();
		/* repaint */
		goto retry;
#endif
	    default:
		flash();
		break;
	    }
	}
    }

    dlg_mouse_free_regions();
    dlg_unregister_window(menu);
    dlg_del_window(dialog);
    free(prompt);

    *current_item = scrollamt + choice;
    return result;
}

/*
 * Display a menu for choosing among a number of options
 */
int
dialog_menu(const char *title,
	    const char *cprompt,
	    int height,
	    int width,
	    int menu_height,
	    int item_no,
	    char **items)
{
    int result;
    int choice;
    int i, j;
    DIALOG_LISTITEM *listitems;

    listitems = dlg_calloc(DIALOG_LISTITEM, (size_t) item_no + 1);
    assert_ptr(listitems, "dialog_menu");

    for (i = j = 0; i < item_no; ++i) {
	listitems[i].name = items[j++];
	listitems[i].text = (dialog_vars.no_items
			     ? dlg_strempty()
			     : items[j++]);
	listitems[i].help = ((dialog_vars.item_help)
			     ? items[j++]
			     : dlg_strempty());
    }
    dlg_align_columns(&listitems[0].text, sizeof(DIALOG_LISTITEM), item_no);

    result = dlg_menu(title,
		      cprompt,
		      height,
		      width,
		      menu_height,
		      item_no,
		      listitems,
		      &choice,
		      (dialog_vars.input_menu
		       ? dlg_renamed_menutext
		       : dlg_dummy_menutext));

    dlg_free_columns(&listitems[0].text, sizeof(DIALOG_LISTITEM), item_no);
    free(listitems);
    return result;
}
