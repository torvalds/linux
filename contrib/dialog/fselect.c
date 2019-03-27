/*
 *  $Id: fselect.c,v 1.102 2018/06/21 23:28:04 tom Exp $
 *
 *  fselect.c -- implements the file-selector box
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

#include <sys/types.h>
#include <sys/stat.h>

#if HAVE_DIRENT_H
# include <dirent.h>
# define NAMLEN(dirent) strlen((dirent)->d_name)
#else
# define dirent direct
# define NAMLEN(dirent) (dirent)->d_namlen
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif
# if HAVE_NDIR_H
#  include <ndir.h>
# endif
#endif

# if defined(_FILE_OFFSET_BITS) && defined(HAVE_STRUCT_DIRENT64)
#  if !defined(_LP64) && (_FILE_OFFSET_BITS == 64)
#   define      DIRENT  struct dirent64
#  else
#   define      DIRENT  struct dirent
#  endif
# else
#  define       DIRENT  struct dirent
# endif

#define EXT_WIDE 1
#define HDR_HIGH 1
#define BTN_HIGH (1 + 2 * MARGIN)	/* Ok/Cancel, also input-box */
#define MIN_HIGH (HDR_HIGH - MARGIN + (BTN_HIGH * 2) + 4 * MARGIN)
#define MIN_WIDE (2 * MAX(dlg_count_columns(d_label), dlg_count_columns(f_label)) + 6 * MARGIN + 2 * EXT_WIDE)

#define MOUSE_D (KEY_MAX + 0)
#define MOUSE_F (KEY_MAX + 10000)
#define MOUSE_T (KEY_MAX + 20000)

typedef enum {
    sDIRS = -3
    ,sFILES = -2
    ,sTEXT = -1
} STATES;

typedef struct {
    WINDOW *par;		/* parent window */
    WINDOW *win;		/* this window */
    int length;			/* length of the data[] array */
    int offset;			/* index of first item on screen */
    int choice;			/* index of the selection */
    int mousex;			/* base of mouse-code return-values */
    unsigned allocd;
    char **data;
} LIST;

typedef struct {
    int length;
    char **data;
} MATCH;

static void
init_list(LIST * list, WINDOW *par, WINDOW *win, int mousex)
{
    list->par = par;
    list->win = win;
    list->length = 0;
    list->offset = 0;
    list->choice = 0;
    list->mousex = mousex;
    list->allocd = 0;
    list->data = 0;
    dlg_mouse_mkbigregion(getbegy(win), getbegx(win),
			  getmaxy(win), getmaxx(win),
			  mousex, 1, 1, 1 /* by lines */ );
}

static char *
leaf_of(char *path)
{
    char *leaf = strrchr(path, '/');
    if (leaf != 0)
	leaf++;
    else
	leaf = path;
    return leaf;
}

static char *
data_of(LIST * list)
{
    if (list != 0
	&& list->data != 0)
	return list->data[list->choice];
    return 0;
}

static void
free_list(LIST * list, int reinit)
{
    int n;

    if (list->data != 0) {
	for (n = 0; list->data[n] != 0; n++)
	    free(list->data[n]);
	free(list->data);
	list->data = 0;
    }
    if (reinit)
	init_list(list, list->par, list->win, list->mousex);
}

static void
add_to_list(LIST * list, char *text)
{
    unsigned need;

    need = (unsigned) (list->length + 1);
    if (need + 1 > list->allocd) {
	list->allocd = 2 * (need + 1);
	if (list->data == 0) {
	    list->data = dlg_malloc(char *, list->allocd);
	} else {
	    list->data = dlg_realloc(char *, list->allocd, list->data);
	}
	assert_ptr(list->data, "add_to_list");
    }
    list->data[list->length++] = dlg_strclone(text);
    list->data[list->length] = 0;
}

static void
keep_visible(LIST * list)
{
    int high = getmaxy(list->win);

    if (list->choice < list->offset) {
	list->offset = list->choice;
    }
    if (list->choice - list->offset >= high)
	list->offset = list->choice - high + 1;
}

#define Value(c) (int)((c) & 0xff)

static int
find_choice(char *target, LIST * list)
{
    int n;
    int choice = list->choice;
    int len_1, len_2, cmp_1, cmp_2;

    if (*target == 0) {
	list->choice = 0;
    } else {
	/* find the match with the longest length.  If more than one has the
	 * same length, choose the one with the closest match of the final
	 * character.
	 */
	len_1 = 0;
	cmp_1 = 256;
	for (n = 0; n < list->length; n++) {
	    char *a = target;
	    char *b = list->data[n];

	    len_2 = 0;
	    while ((*a != 0) && (*b != 0) && (*a == *b)) {
		a++;
		b++;
		len_2++;
	    }
	    cmp_2 = Value(*a) - Value(*b);
	    if (cmp_2 < 0)
		cmp_2 = -cmp_2;
	    if ((len_2 > len_1)
		|| (len_1 == len_2 && cmp_2 < cmp_1)) {
		len_1 = len_2;
		cmp_1 = cmp_2;
		list->choice = n;
	    }
	}
    }
    if (choice != list->choice) {
	keep_visible(list);
    }
    return (choice != list->choice);
}

static void
display_list(LIST * list)
{
    int n;
    int x;
    int y;
    int top;
    int bottom;

    if (list->win != 0) {
	dlg_attr_clear(list->win, getmaxy(list->win), getmaxx(list->win), item_attr);
	for (n = list->offset; n < list->length && list->data[n]; n++) {
	    y = n - list->offset;
	    if (y >= getmaxy(list->win))
		break;
	    (void) wmove(list->win, y, 0);
	    if (n == list->choice)
		dlg_attrset(list->win, item_selected_attr);
	    (void) waddstr(list->win, list->data[n]);
	    dlg_attrset(list->win, item_attr);
	}
	dlg_attrset(list->win, item_attr);

	getparyx(list->win, y, x);

	top = y - 1;
	bottom = y + getmaxy(list->win);
	dlg_draw_scrollbar(list->par,
			   (long) list->offset,
			   (long) list->offset,
			   (long) (list->offset + getmaxy(list->win)),
			   (long) (list->length),
			   x + 1,
			   x + getmaxx(list->win),
			   top,
			   bottom,
			   menubox_border2_attr,
			   menubox_border_attr);

	(void) wmove(list->win, list->choice - list->offset, 0);
	(void) wnoutrefresh(list->win);
    }
}

/* FIXME: see arrows.c
 * This workaround is used to allow two lists to have scroll-tabs at the same
 * time, by reassigning their return-values to be different.  Just for
 * readability, we use the names of keys with similar connotations, though all
 * that is really required is that they're distinct, so we can put them in a
 * switch statement.
 */
static void
fix_arrows(LIST * list)
{
    int x;
    int y;
    int top;
    int right;
    int bottom;

    if (list->win != 0) {
	getparyx(list->win, y, x);
	top = y - 1;
	right = getmaxx(list->win);
	bottom = y + getmaxy(list->win);

	mouse_mkbutton(top, x, right,
		       ((list->mousex == MOUSE_D)
			? KEY_PREVIOUS
			: KEY_PPAGE));
	mouse_mkbutton(bottom, x, right,
		       ((list->mousex == MOUSE_D)
			? KEY_NEXT
			: KEY_NPAGE));
    }
}

static bool
show_list(char *target, LIST * list, bool keep)
{
    bool changed = keep || find_choice(target, list);
    display_list(list);
    return changed;
}

/*
 * Highlight the closest match to 'target' in the given list, setting offset
 * to match.
 */
static bool
show_both_lists(char *input, LIST * d_list, LIST * f_list, bool keep)
{
    char *leaf = leaf_of(input);

    return show_list(leaf, d_list, keep) || show_list(leaf, f_list, keep);
}

/*
 * Move up/down in the given list
 */
static bool
change_list(int choice, LIST * list)
{
    if (data_of(list) != 0) {
	int last = list->length - 1;

	choice += list->choice;
	if (choice < 0)
	    choice = 0;
	if (choice > last)
	    choice = last;
	list->choice = choice;
	keep_visible(list);
	display_list(list);
	return TRUE;
    }
    return FALSE;
}

static void
scroll_list(int direction, LIST * list)
{
    if (data_of(list) != 0) {
	int length = getmaxy(list->win);
	if (change_list(direction * length, list))
	    return;
    }
    beep();
}

static int
compar(const void *a, const void *b)
{
    return strcmp(*(const char *const *) a, *(const char *const *) b);
}

static void
match(char *name, LIST * d_list, LIST * f_list, MATCH * match_list)
{
    char *test = leaf_of(name);
    size_t test_len = strlen(test);
    char **matches = dlg_malloc(char *, (size_t) (d_list->length + f_list->length));
    size_t data_len = 0;
    int i;
    for (i = 2; i < d_list->length; i++) {
	if (strncmp(test, d_list->data[i], test_len) == 0) {
	    matches[data_len++] = d_list->data[i];
	}
    }
    for (i = 0; i < f_list->length; i++) {
	if (strncmp(test, f_list->data[i], test_len) == 0) {
	    matches[data_len++] = f_list->data[i];
	}
    }
    matches = dlg_realloc(char *, data_len + 1, matches);
    match_list->data = matches;
    match_list->length = (int) data_len;
}

static void
free_match(MATCH * match_list)
{
    free(match_list->data);
    match_list->length = 0;
}

static int
complete(char *name, LIST * d_list, LIST * f_list, char **buff_ptr)
{
    MATCH match_list;
    char *test;
    size_t test_len;
    size_t i;
    int j;
    char *buff;

    match(name, d_list, f_list, &match_list);
    if (match_list.length == 0) {
	*buff_ptr = NULL;
	return 0;
    }

    test = match_list.data[0];
    test_len = strlen(test);
    buff = dlg_malloc(char, test_len + 2);
    if (match_list.length == 1) {
	strcpy(buff, test);
	i = test_len;
	if (test == data_of(d_list)) {
	    buff[test_len] = '/';
	    i++;
	}
    } else {
	for (i = 0; i < test_len; i++) {
	    char test_char = test[i];
	    if (test_char == '\0')
		break;
	    for (j = 0; j < match_list.length; j++) {
		if (match_list.data[j][i] != test_char) {
		    break;
		}
	    }
	    if (j == match_list.length) {
		(buff)[i] = test_char;
	    } else
		break;
	}
	buff = dlg_realloc(char, i + 1, buff);
    }
    free_match(&match_list);
    buff[i] = '\0';
    *buff_ptr = buff;
    return (i != 0);
}

static bool
fill_lists(char *current, char *input, LIST * d_list, LIST * f_list, bool keep)
{
    bool result = TRUE;
    bool rescan = FALSE;
    DIR *dp;
    DIRENT *de;
    struct stat sb;
    int n;
    char path[MAX_LEN + 1];
    char *leaf;

    /* check if we've updated the lists */
    for (n = 0; current[n] && input[n]; n++) {
	if (current[n] != input[n])
	    break;
    }

    if (current[n] == input[n]) {
	result = FALSE;
	rescan = (n == 0 && d_list->length == 0);
    } else if (strchr(current + n, '/') == 0
	       && strchr(input + n, '/') == 0) {
	result = show_both_lists(input, d_list, f_list, keep);
    } else {
	rescan = TRUE;
    }

    if (rescan) {
	size_t have = strlen(input);

	if (have > MAX_LEN)
	    have = MAX_LEN;
	memcpy(current, input, have);
	current[have] = '\0';

	/* refill the lists */
	free_list(d_list, TRUE);
	free_list(f_list, TRUE);
	memcpy(path, current, have);
	path[have] = '\0';
	if ((leaf = strrchr(path, '/')) != 0) {
	    *++leaf = 0;
	} else {
	    strcpy(path, "./");
	    leaf = path + strlen(path);
	}
	DLG_TRACE(("opendir '%s'\n", path));
	if ((dp = opendir(path)) != 0) {
	    while ((de = readdir(dp)) != 0) {
		size_t len = NAMLEN(de);
		if (len == 0 || (len + have + 2) >= MAX_LEN)
		    continue;
		memcpy(leaf, de->d_name, len);
		leaf[len] = '\0';
		if (stat(path, &sb) == 0) {
		    if ((sb.st_mode & S_IFMT) == S_IFDIR)
			add_to_list(d_list, leaf);
		    else if (f_list->win)
			add_to_list(f_list, leaf);
		}
	    }
	    (void) closedir(dp);
	    /* sort the lists */
	    if (d_list->data != 0 && d_list->length > 1) {
		qsort(d_list->data,
		      (size_t) d_list->length,
		      sizeof(d_list->data[0]),
		      compar);
	    }
	    if (f_list->data != 0 && f_list->length > 1) {
		qsort(f_list->data,
		      (size_t) f_list->length,
		      sizeof(f_list->data[0]),
		      compar);
	    }
	}

	(void) show_both_lists(input, d_list, f_list, FALSE);
	d_list->offset = d_list->choice;
	f_list->offset = f_list->choice;
	result = TRUE;
    }
    return result;
}

static bool
usable_state(int state, LIST * dirs, LIST * files)
{
    bool result;

    switch (state) {
    case sDIRS:
	result = (dirs->win != 0) && (data_of(dirs) != 0);
	break;
    case sFILES:
	result = (files->win != 0) && (data_of(files) != 0);
	break;
    default:
	result = TRUE;
	break;
    }
    return result;
}

#define which_list() ((state == sFILES) \
			? &f_list \
			: ((state == sDIRS) \
			  ? &d_list \
			  : 0))
#define NAVIGATE_BINDINGS \
	DLG_KEYS_DATA( DLGK_FIELD_NEXT, KEY_RIGHT ), \
	DLG_KEYS_DATA( DLGK_FIELD_NEXT, TAB ), \
	DLG_KEYS_DATA( DLGK_FIELD_PREV, KEY_BTAB ), \
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,  KEY_DOWN ), \
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,  CHR_NEXT ), \
	DLG_KEYS_DATA( DLGK_ITEM_NEXT,  KEY_NEXT ), \
	DLG_KEYS_DATA( DLGK_ITEM_PREV,  CHR_PREVIOUS ), \
	DLG_KEYS_DATA( DLGK_ITEM_PREV,  KEY_UP ), \
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,  KEY_NPAGE ), \
	DLG_KEYS_DATA( DLGK_PAGE_PREV,  KEY_PPAGE )

/*
 * Display a dialog box for entering a filename
 */
static int
dlg_fselect(const char *title, const char *path, int height, int width, int dselect)
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
	TOGGLEKEY_BINDINGS,
	END_KEYS_BINDING
    };
    /* *INDENT-ON* */

#ifdef KEY_RESIZE
    int old_height = height;
    int old_width = width;
    bool resized = FALSE;
#endif
    int tbox_y, tbox_x, tbox_width, tbox_height;
    int dbox_y, dbox_x, dbox_width, dbox_height;
    int fbox_y, fbox_x, fbox_width, fbox_height;
    int show_buttons = TRUE;
    int offset = 0;
    int key = 0;
    int fkey = FALSE;
    int code;
    int result = DLG_EXIT_UNKNOWN;
    int state = dialog_vars.default_button >= 0 ? dlg_default_button() : sTEXT;
    int button;
    bool first = (state == sTEXT);
    bool first_trace = TRUE;
    char *input;
    char *completed;
    char current[MAX_LEN + 1];
    WINDOW *dialog = 0;
    WINDOW *w_text = 0;
    WINDOW *w_work = 0;
    const char **buttons = dlg_ok_labels();
    const char *d_label = _("Directories");
    const char *f_label = _("Files");
    char *partial = 0;
    int min_wide = MIN_WIDE;
    int min_items = height ? 0 : 4;
    LIST d_list, f_list;

    DLG_TRACE(("# %s args:\n", dselect ? "dselect" : "fselect"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("path", path);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);

    dlg_does_output();

    /* Set up the initial value */
    input = dlg_set_result(path);
    offset = (int) strlen(input);
    *current = 0;

    dlg_button_layout(buttons, &min_wide);

#ifdef KEY_RESIZE
  retry:
#endif
    dlg_auto_size(title, (char *) 0, &height, &width, 6, 25);
    height += MIN_HIGH + min_items;
    if (width < min_wide)
	width = min_wide;
    dlg_print_size(height, width);
    dlg_ctl_size(height, width);

    dialog = dlg_new_window(height, width,
			    dlg_box_y_ordinate(height),
			    dlg_box_x_ordinate(width));
    dlg_register_window(dialog, "fselect", binding);
    dlg_register_buttons(dialog, "fselect", buttons);

    dlg_mouse_setbase(0, 0);

    dlg_draw_box2(dialog, 0, 0, height, width, dialog_attr, border_attr, border2_attr);
    dlg_draw_bottom_box2(dialog, border_attr, border2_attr, dialog_attr);
    dlg_draw_title(dialog, title);

    dlg_attrset(dialog, dialog_attr);

    /* Draw the input field box */
    tbox_height = 1;
    tbox_width = width - (4 * MARGIN + 2);
    tbox_y = height - (BTN_HIGH * 2) + MARGIN;
    tbox_x = (width - tbox_width) / 2;

    w_text = derwin(dialog, tbox_height, tbox_width, tbox_y, tbox_x);
    if (w_text == 0) {
	result = DLG_EXIT_ERROR;
	goto finish;
    }

    (void) keypad(w_text, TRUE);
    dlg_draw_box(dialog, tbox_y - MARGIN, tbox_x - MARGIN,
		 (2 * MARGIN + 1), tbox_width + (MARGIN + EXT_WIDE),
		 menubox_border_attr, menubox_border2_attr);
    dlg_mouse_mkbigregion(getbegy(dialog) + tbox_y - MARGIN,
			  getbegx(dialog) + tbox_x - MARGIN,
			  1 + (2 * MARGIN),
			  tbox_width + (MARGIN + EXT_WIDE),
			  MOUSE_T, 1, 1, 3 /* doesn't matter */ );

    dlg_register_window(w_text, "fselect2", binding2);

    /* Draw the directory listing box */
    if (dselect)
	dbox_width = (width - (6 * MARGIN));
    else
	dbox_width = (width - (6 * MARGIN + 2 * EXT_WIDE)) / 2;
    dbox_height = height - MIN_HIGH;
    dbox_y = (2 * MARGIN + 1);
    dbox_x = tbox_x;

    w_work = derwin(dialog, dbox_height, dbox_width, dbox_y, dbox_x);
    if (w_work == 0) {
	result = DLG_EXIT_ERROR;
	goto finish;
    }

    (void) keypad(w_work, TRUE);
    (void) mvwaddstr(dialog, dbox_y - (MARGIN + 1), dbox_x - MARGIN, d_label);
    dlg_draw_box(dialog,
		 dbox_y - MARGIN, dbox_x - MARGIN,
		 dbox_height + (MARGIN + 1), dbox_width + (MARGIN + 1),
		 menubox_border_attr, menubox_border2_attr);
    init_list(&d_list, dialog, w_work, MOUSE_D);

    if (!dselect) {
	/* Draw the filename listing box */
	fbox_height = dbox_height;
	fbox_width = dbox_width;
	fbox_y = dbox_y;
	fbox_x = tbox_x + dbox_width + (2 * MARGIN);

	w_work = derwin(dialog, fbox_height, fbox_width, fbox_y, fbox_x);
	if (w_work == 0) {
	    result = DLG_EXIT_ERROR;
	    goto finish;
	}

	(void) keypad(w_work, TRUE);
	(void) mvwaddstr(dialog, fbox_y - (MARGIN + 1), fbox_x - MARGIN, f_label);
	dlg_draw_box(dialog,
		     fbox_y - MARGIN, fbox_x - MARGIN,
		     fbox_height + (MARGIN + 1), fbox_width + (MARGIN + 1),
		     menubox_border_attr, menubox_border2_attr);
	init_list(&f_list, dialog, w_work, MOUSE_F);
    } else {
	memset(&f_list, 0, sizeof(f_list));
    }

    while (result == DLG_EXIT_UNKNOWN) {

	if (fill_lists(current, input, &d_list, &f_list, state < sTEXT))
	    show_buttons = TRUE;

#ifdef KEY_RESIZE
	if (resized) {
	    resized = FALSE;
	    dlg_show_string(w_text, input, offset, inputbox_attr,
			    0, 0, tbox_width, FALSE, first);
	}
#endif

	/*
	 * The last field drawn determines where the cursor is shown:
	 */
	if (show_buttons) {
	    show_buttons = FALSE;
	    button = (state < 0) ? 0 : state;
	    dlg_draw_buttons(dialog, height - 2, 0, buttons, button, FALSE, width);
	}

	if (first_trace) {
	    first_trace = FALSE;
	    dlg_trace_win(dialog);
	}

	if (state < 0) {
	    switch (state) {
	    case sTEXT:
		dlg_set_focus(dialog, w_text);
		break;
	    case sFILES:
		dlg_set_focus(dialog, f_list.win);
		break;
	    case sDIRS:
		dlg_set_focus(dialog, d_list.win);
		break;
	    }
	}

	if (first) {
	    (void) wrefresh(dialog);
	} else {
	    fix_arrows(&d_list);
	    fix_arrows(&f_list);
	    key = dlg_mouse_wgetch((state == sTEXT) ? w_text : dialog, &fkey);
	    if (dlg_result_key(key, fkey, &result))
		break;
	}

	if (key == DLGK_TOGGLE) {
	    key = DLGK_SELECT;
	    fkey = TRUE;
	}

	if (fkey) {
	    switch (key) {
	    case DLGK_MOUSE(KEY_PREVIOUS):
		state = sDIRS;
		scroll_list(-1, which_list());
		continue;
	    case DLGK_MOUSE(KEY_NEXT):
		state = sDIRS;
		scroll_list(1, which_list());
		continue;
	    case DLGK_MOUSE(KEY_PPAGE):
		state = sFILES;
		scroll_list(-1, which_list());
		continue;
	    case DLGK_MOUSE(KEY_NPAGE):
		state = sFILES;
		scroll_list(1, which_list());
		continue;
	    case DLGK_PAGE_PREV:
		scroll_list(-1, which_list());
		continue;
	    case DLGK_PAGE_NEXT:
		scroll_list(1, which_list());
		continue;
	    case DLGK_ITEM_PREV:
		if (change_list(-1, which_list()))
		    continue;
		/* FALLTHRU */
	    case DLGK_FIELD_PREV:
		show_buttons = TRUE;
		do {
		    state = dlg_prev_ok_buttonindex(state, sDIRS);
		} while (!usable_state(state, &d_list, &f_list));
		continue;
	    case DLGK_ITEM_NEXT:
		if (change_list(1, which_list()))
		    continue;
		/* FALLTHRU */
	    case DLGK_FIELD_NEXT:
		show_buttons = TRUE;
		do {
		    state = dlg_next_ok_buttonindex(state, sDIRS);
		} while (!usable_state(state, &d_list, &f_list));
		continue;
	    case DLGK_SELECT:
		completed = 0;
		if (partial != 0) {
		    free(partial);
		    partial = 0;
		}
		if (state == sFILES && !dselect) {
		    completed = data_of(&f_list);
		} else if (state == sDIRS) {
		    completed = data_of(&d_list);
		} else {
		    if (complete(input, &d_list, &f_list, &partial)) {
			completed = partial;
		    }
		}
		if (completed != 0) {
		    state = sTEXT;
		    show_buttons = TRUE;
		    strcpy(leaf_of(input), completed);
		    offset = (int) strlen(input);
		    dlg_show_string(w_text, input, offset, inputbox_attr,
				    0, 0, tbox_width, 0, first);
		    if (partial != NULL) {
			free(partial);
			partial = 0;
		    }
		    continue;
		} else {	/* if (state < sTEXT) */
		    (void) beep();
		    continue;
		}
		/* FALLTHRU */
	    case DLGK_ENTER:
		result = (state > 0) ? dlg_enter_buttoncode(state) : DLG_EXIT_OK;
		continue;
#ifdef KEY_RESIZE
	    case KEY_RESIZE:
		dlg_will_resize(dialog);
		/* reset data */
		height = old_height;
		width = old_width;
		show_buttons = TRUE;
		*current = 0;
		resized = TRUE;
		/* repaint */
		dlg_clear();
		dlg_del_window(dialog);
		refresh();
		dlg_mouse_free_regions();
		goto retry;
#endif
	    default:
		if (key >= DLGK_MOUSE(MOUSE_T)) {
		    state = sTEXT;
		    continue;
		} else if (key >= DLGK_MOUSE(MOUSE_F)) {
		    if (f_list.win != 0) {
			state = sFILES;
			f_list.choice = (key - DLGK_MOUSE(MOUSE_F)) + f_list.offset;
			display_list(&f_list);
		    }
		    continue;
		} else if (key >= DLGK_MOUSE(MOUSE_D)) {
		    if (d_list.win != 0) {
			state = sDIRS;
			d_list.choice = (key - DLGK_MOUSE(MOUSE_D)) + d_list.offset;
			display_list(&d_list);
		    }
		    continue;
		} else if (is_DLGK_MOUSE(key)
			   && (code = dlg_ok_buttoncode(key - M_EVENT)) >= 0) {
		    result = code;
		    continue;
		}
		break;
	    }
	}

	if (state < 0) {	/* Input box selected if we're editing */
	    int edit = dlg_edit_string(input, &offset, key, fkey, first);

	    if (edit) {
		dlg_show_string(w_text, input, offset, inputbox_attr,
				0, 0, tbox_width, 0, first);
		first = FALSE;
		state = sTEXT;
	    }
	} else if (state >= 0 &&
		   (code = dlg_char_to_button(key, buttons)) >= 0) {
	    result = dlg_ok_buttoncode(code);
	    break;
	}
    }

    dlg_unregister_window(w_text);
    dlg_del_window(dialog);
    dlg_mouse_free_regions();
    free_list(&d_list, FALSE);
    free_list(&f_list, FALSE);

  finish:
    if (partial != 0)
	free(partial);
    return result;
}

/*
 * Display a dialog box for entering a filename
 */
int
dialog_fselect(const char *title, const char *path, int height, int width)
{
    return dlg_fselect(title, path, height, width, FALSE);
}

/*
 * Display a dialog box for entering a directory
 */
int
dialog_dselect(const char *title, const char *path, int height, int width)
{
    return dlg_fselect(title, path, height, width, TRUE);
}
