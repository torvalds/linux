/*
 *  $Id: textbox.c,v 1.117 2018/06/19 22:57:01 tom Exp $
 *
 *  textbox.c -- implements the text box
 *
 *  Copyright 2000-2017,2018	Thomas E.  Dickey
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

#define PAGE_LENGTH	(height - 4)
#define PAGE_WIDTH	(width - 2)

typedef struct {
    DIALOG_CALLBACK obj;
    WINDOW *text;
    const char **buttons;
    int hscroll;
    char line[MAX_LEN + 1];
    int fd;
    long file_size;
    long fd_bytes_read;
    long bytes_read;
    long buffer_len;
    bool begin_reached;
    bool buffer_first;
    bool end_reached;
    long page_length;		/* lines on the page which is shown */
    long in_buf;		/* ending index into buf[] for page */
    char *buf;
} MY_OBJ;

static long
lseek_obj(MY_OBJ * obj, long offset, int mode)
{
    long fpos;
    if ((fpos = (long) lseek(obj->fd, (off_t) offset, mode)) == -1) {
	switch (mode) {
	default:
	case SEEK_CUR:
	    dlg_exiterr("Cannot get file position");
	    break;
	case SEEK_END:
	    dlg_exiterr("Cannot seek to end of file");
	    break;
	case SEEK_SET:
	    dlg_exiterr("Cannot set file position to %ld", offset);
	    break;
	}
    }
    return fpos;
}

static long
ftell_obj(MY_OBJ * obj)
{
    return lseek_obj(obj, 0L, SEEK_CUR);
}

static void
lseek_set(MY_OBJ * obj, long offset)
{
    long actual = lseek_obj(obj, offset, SEEK_SET);

    if (actual != offset) {
	dlg_exiterr("Cannot set file position to %ld (actual %ld)\n",
		    offset, actual);
    }
}

static void
lseek_end(MY_OBJ * obj, long offset)
{
    long actual = lseek_obj(obj, offset, SEEK_END);

    if (offset == 0L && actual > offset) {
	obj->file_size = actual;
    }
}

static void
lseek_cur(MY_OBJ * obj, long offset)
{
    long actual = lseek_obj(obj, offset, SEEK_CUR);

    if (actual != offset) {
	DLG_TRACE(("# Lseek returned %ld, expected %ld\n", actual, offset));
    }
}

static char *
xalloc(size_t size)
{
    char *result = dlg_malloc(char, size);
    assert_ptr(result, "xalloc");
    return result;
}

/*
 * read_high() substitutes read() for tab->spaces conversion
 *
 * buffer_len, fd_bytes_read, bytes_read are modified
 * buf is allocated
 *
 * fd_bytes_read is the effective number of bytes read from file
 * bytes_read is the length of buf, that can be different if tab_correct
 */
static void
read_high(MY_OBJ * obj, size_t size_read)
{
    char *buftab, ch;
    int i = 0, j, n, tmpint;
    long begin_line;

    /* Allocate space for read buffer */
    buftab = xalloc(size_read + 1);

    if ((obj->fd_bytes_read = read(obj->fd, buftab, size_read)) != -1) {

	buftab[obj->fd_bytes_read] = '\0';	/* mark end of valid data */

	if (dialog_vars.tab_correct) {

	    /* calculate bytes_read by buftab and fd_bytes_read */
	    obj->bytes_read = begin_line = 0;
	    for (j = 0; j < obj->fd_bytes_read; j++)
		if (buftab[j] == TAB)
		    obj->bytes_read += dialog_state.tab_len
			- ((obj->bytes_read - begin_line)
			   % dialog_state.tab_len);
		else if (buftab[j] == '\n') {
		    obj->bytes_read++;
		    begin_line = obj->bytes_read;
		} else
		    obj->bytes_read++;

	    if (obj->bytes_read > obj->buffer_len) {
		if (obj->buffer_first)
		    obj->buffer_first = FALSE;	/* disp = 0 */
		else {
		    free(obj->buf);
		}

		obj->buffer_len = obj->bytes_read;

		/* Allocate space for read buffer */
		obj->buf = xalloc((size_t) obj->buffer_len + 1);
	    }

	} else {
	    if (obj->buffer_first) {
		obj->buffer_first = FALSE;

		/* Allocate space for read buffer */
		obj->buf = xalloc(size_read + 1);
	    }

	    obj->bytes_read = obj->fd_bytes_read;
	}

	j = 0;
	begin_line = 0;
	while (j < obj->fd_bytes_read)
	    if (((ch = buftab[j++]) == TAB) && (dialog_vars.tab_correct != 0)) {
		tmpint = (dialog_state.tab_len
			  - ((int) ((long) i - begin_line) % dialog_state.tab_len));
		for (n = 0; n < tmpint; n++)
		    obj->buf[i++] = ' ';
	    } else {
		if (ch == '\n')
		    begin_line = i + 1;
		obj->buf[i++] = ch;
	    }

	obj->buf[i] = '\0';	/* mark end of valid data */

    }
    if (obj->bytes_read == -1)
	dlg_exiterr("Error reading file");
    free(buftab);
}

static long
find_first(MY_OBJ * obj, char *buffer, long length)
{
    long recount = obj->page_length;
    long result = 0;

    while (length > 0) {
	if (buffer[length] == '\n') {
	    if (--recount < 0) {
		result = length;
		break;
	    }
	}
	--length;
    }
    return result;
}

static long
tabize(MY_OBJ * obj, long val, long *first_pos)
{
    long fpos;
    long i, count, begin_line;
    char *buftab;

    if (!dialog_vars.tab_correct)
	return val;

    fpos = ftell_obj(obj);

    lseek_set(obj, fpos - obj->fd_bytes_read);

    /* Allocate space for read buffer */
    buftab = xalloc((size_t) val + 1);

    if ((read(obj->fd, buftab, (size_t) val)) == -1)
	dlg_exiterr("Error reading file in tabize().");

    begin_line = count = 0;
    if (first_pos != 0)
	*first_pos = 0;

    for (i = 0; i < val; i++) {
	if ((first_pos != 0) && (count >= val)) {
	    *first_pos = find_first(obj, buftab, i);
	    break;
	}
	if (buftab[i] == TAB)
	    count += dialog_state.tab_len
		- ((count - begin_line) % dialog_state.tab_len);
	else if (buftab[i] == '\n') {
	    count++;
	    begin_line = count;
	} else
	    count++;
    }

    lseek_set(obj, fpos);
    free(buftab);
    return count;
}
/*
 * Return current line of text.
 * 'page' should point to start of current line before calling, and will be
 * updated to point to start of next line.
 */
static char *
get_line(MY_OBJ * obj)
{
    int i = 0;
    long fpos;

    obj->end_reached = FALSE;
    while (obj->buf[obj->in_buf] != '\n') {
	if (obj->buf[obj->in_buf] == '\0') {	/* Either end of file or end of buffer reached */
	    fpos = ftell_obj(obj);

	    if (fpos < obj->file_size) {	/* Not end of file yet */
		/* We've reached end of buffer, but not end of file yet, so
		 * read next part of file into buffer
		 */
		read_high(obj, BUF_SIZE);
		obj->in_buf = 0;
	    } else {
		if (!obj->end_reached)
		    obj->end_reached = TRUE;
		break;
	    }
	} else if (i < MAX_LEN)
	    obj->line[i++] = obj->buf[obj->in_buf++];
	else {
	    if (i == MAX_LEN)	/* Truncate lines longer than MAX_LEN characters */
		obj->line[i++] = '\0';
	    obj->in_buf++;
	}
    }
    if (i <= MAX_LEN)
	obj->line[i] = '\0';
    if (!obj->end_reached)
	obj->in_buf++;		/* move past '\n' */

    return obj->line;
}

static bool
match_string(MY_OBJ * obj, char *string)
{
    char *match = get_line(obj);
    return strstr(match, string) != 0;
}

/*
 * Go back 'n' lines in text file. Called by dialog_textbox().
 * 'in_buf' will be updated to point to the desired line in 'buf'.
 */
static void
back_lines(MY_OBJ * obj, long n)
{
    int i;
    long fpos;
    long val_to_tabize;

    obj->begin_reached = FALSE;
    /* We have to distinguish between end_reached and !end_reached since at end
       * of file, the line is not ended by a '\n'.  The code inside 'if'
       * basically does a '--in_buf' to move one character backward so as to
       * skip '\n' of the previous line */
    if (!obj->end_reached) {
	/* Either beginning of buffer or beginning of file reached? */

	if (obj->in_buf == 0) {
	    fpos = ftell_obj(obj);

	    if (fpos > obj->fd_bytes_read) {	/* Not beginning of file yet */
		/* We've reached beginning of buffer, but not beginning of file
		 * yet, so read previous part of file into buffer.  Note that
		 * we only move backward for BUF_SIZE/2 bytes, but not BUF_SIZE
		 * bytes to avoid re-reading again in print_page() later
		 */
		/* Really possible to move backward BUF_SIZE/2 bytes? */
		if (fpos < BUF_SIZE / 2 + obj->fd_bytes_read) {
		    /* No, move less than */
		    lseek_set(obj, 0L);
		    val_to_tabize = fpos - obj->fd_bytes_read;
		} else {	/* Move backward BUF_SIZE/2 bytes */
		    lseek_cur(obj, -(BUF_SIZE / 2 + obj->fd_bytes_read));
		    val_to_tabize = BUF_SIZE / 2;
		}
		read_high(obj, BUF_SIZE);

		obj->in_buf = tabize(obj, val_to_tabize, (long *) 0);

	    } else {		/* Beginning of file reached */
		obj->begin_reached = TRUE;
		return;
	    }
	}
	obj->in_buf--;
	if (obj->buf[obj->in_buf] != '\n')
	    /* Something's wrong... */
	    dlg_exiterr("Internal error in back_lines().");
    }

    /* Go back 'n' lines */
    for (i = 0; i < n; i++) {
	do {
	    if (obj->in_buf == 0) {
		fpos = ftell_obj(obj);

		if (fpos > obj->fd_bytes_read) {
		    /* Really possible to move backward BUF_SIZE/2 bytes? */
		    if (fpos < BUF_SIZE / 2 + obj->fd_bytes_read) {
			/* No, move less than */
			lseek_set(obj, 0L);
			val_to_tabize = fpos - obj->fd_bytes_read;
		    } else {	/* Move backward BUF_SIZE/2 bytes */
			lseek_cur(obj, -(BUF_SIZE / 2 + obj->fd_bytes_read));
			val_to_tabize = BUF_SIZE / 2;
		    }
		    read_high(obj, BUF_SIZE);

		    obj->in_buf = tabize(obj, val_to_tabize, (long *) 0);

		} else {	/* Beginning of file reached */
		    obj->begin_reached = TRUE;
		    return;
		}
	    }
	} while (obj->buf[--(obj->in_buf)] != '\n');
    }
    obj->in_buf++;
}

/*
 * Print a new line of text.
 */
static void
print_line(MY_OBJ * obj, int row, int width)
{
    if (wmove(obj->text, row, 0) != ERR) {
	int i, y, x;
	char *line = get_line(obj);
	const int *cols = dlg_index_columns(line);
	const int *indx = dlg_index_wchars(line);
	int limit = dlg_count_wchars(line);
	int first = 0;
	int last = limit;

	if (width > getmaxx(obj->text))
	    width = getmaxx(obj->text);
	--width;		/* for the leading ' ' */

	for (i = 0; i <= limit && cols[i] < obj->hscroll; ++i)
	    first = i;

	for (i = first; (i <= limit) && ((cols[i] - cols[first]) < width); ++i)
	    last = i;

	(void) waddch(obj->text, ' ');
	(void) waddnstr(obj->text, line + indx[first], indx[last] - indx[first]);

	getyx(obj->text, y, x);
	if (y == row) {		/* Clear 'residue' of previous line */
	    for (i = 0; i <= width - x; i++) {
		(void) waddch(obj->text, ' ');
	    }
	}
    }
}

/*
 * Print a new page of text.
 */
static void
print_page(MY_OBJ * obj, int height, int width)
{
    int i, passed_end = 0;

    obj->page_length = 0;
    for (i = 0; i < height; i++) {
	print_line(obj, i, width);
	if (!passed_end)
	    obj->page_length++;
	if (obj->end_reached && !passed_end)
	    passed_end = 1;
    }
    (void) wnoutrefresh(obj->text);
    dlg_trace_win(obj->text);
}

/*
 * Print current position
 */
static void
print_position(MY_OBJ * obj, WINDOW *win, int height, int width)
{
    long fpos;
    long size;
    long first = -1;

    fpos = ftell_obj(obj);
    if (dialog_vars.tab_correct)
	size = tabize(obj, obj->in_buf, &first);
    else
	first = find_first(obj, obj->buf, size = obj->in_buf);

    dlg_draw_scrollbar(win,
		       first,
		       fpos - obj->fd_bytes_read + size,
		       fpos - obj->fd_bytes_read + size,
		       obj->file_size,
		       0, PAGE_WIDTH,
		       0, PAGE_LENGTH + 1,
		       border_attr,
		       border_attr);
}

/*
 * Display a dialog box and get the search term from user.
 */
static int
get_search_term(WINDOW *dialog, char *input, int height, int width)
{
    /* *INDENT-OFF* */
    static DLG_KEYS_BINDING binding[] = {
	INPUTSTR_BINDINGS,
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	END_KEYS_BINDING
    };
    /* *INDENT-ON* */

    int old_x, old_y;
    int box_x, box_y;
    int box_height, box_width;
    int offset = 0;
    int key = 0;
    int fkey = 0;
    bool first = TRUE;
    int result = DLG_EXIT_UNKNOWN;
    const char *caption = _("Search");
    int len_caption = dlg_count_columns(caption);
    const int *indx;
    int limit;
    WINDOW *widget;

    getbegyx(dialog, old_y, old_x);

    box_height = 1 + (2 * MARGIN);
    box_width = len_caption + (2 * (MARGIN + 2));
    box_width = MAX(box_width, 30);
    box_width = MIN(box_width, getmaxx(dialog) - 2 * MARGIN);
    len_caption = MIN(len_caption, box_width - (2 * (MARGIN + 1)));

    box_x = (width - box_width) / 2;
    box_y = (height - box_height) / 2;
    widget = dlg_new_modal_window(dialog,
				  box_height, box_width,
				  old_y + box_y, old_x + box_x);
    keypad(widget, TRUE);
    dlg_register_window(widget, "searchbox", binding);

    dlg_draw_box2(widget, 0, 0, box_height, box_width,
		  searchbox_attr,
		  searchbox_border_attr,
		  searchbox_border2_attr);
    dlg_attrset(widget, searchbox_title_attr);
    (void) wmove(widget, 0, (box_width - len_caption) / 2);

    indx = dlg_index_wchars(caption);
    limit = dlg_limit_columns(caption, len_caption, 0);
    (void) waddnstr(widget, caption + indx[0], indx[limit] - indx[0]);

    box_width -= 2;
    offset = dlg_count_columns(input);

    while (result == DLG_EXIT_UNKNOWN) {
	if (!first) {
	    key = dlg_getc(widget, &fkey);
	    if (fkey) {
		switch (fkey) {
#ifdef KEY_RESIZE
		case KEY_RESIZE:
		    result = DLG_EXIT_CANCEL;
		    continue;
#endif
		case DLGK_ENTER:
		    result = DLG_EXIT_OK;
		    continue;
		}
	    } else if (key == ESC) {
		result = DLG_EXIT_ESC;
		continue;
	    } else if (key == ERR) {
		napms(50);
		continue;
	    }
	}
	if (dlg_edit_string(input, &offset, key, fkey, first)) {
	    dlg_show_string(widget, input, offset, searchbox_attr,
			    1, 1, box_width, FALSE, first);
	    first = FALSE;
	}
    }
    dlg_del_window(widget);
    return result;
}

static bool
perform_search(MY_OBJ * obj, int height, int width, int key, char *search_term)
{
    int dir;
    long tempinx;
    long fpos;
    int result;
    bool found;
    bool temp, temp1;
    bool moved = FALSE;

    /* set search direction */
    dir = (key == '/' || key == 'n') ? 1 : 0;
    if (dir ? !obj->end_reached : !obj->begin_reached) {
	if (key == 'n' || key == 'N') {
	    if (search_term[0] == '\0') {	/* No search term yet */
		(void) beep();
		return FALSE;
	    }
	    /* Get search term from user */
	} else if ((result = get_search_term(obj->text, search_term,
					     PAGE_LENGTH,
					     PAGE_WIDTH)) != DLG_EXIT_OK
		   || search_term[0] == '\0') {
#ifdef KEY_RESIZE
	    if (result == DLG_EXIT_CANCEL) {
		ungetch(key);
		ungetch(KEY_RESIZE);
		/* FALLTHRU */
	    }
#endif
	    /* ESC pressed, or no search term, reprint page to clear box */
	    dlg_attrset(obj->text, dialog_attr);
	    back_lines(obj, obj->page_length);
	    return TRUE;
	}
	/* Save variables for restoring in case search term can't be found */
	tempinx = obj->in_buf;
	temp = obj->begin_reached;
	temp1 = obj->end_reached;
	fpos = ftell_obj(obj) - obj->fd_bytes_read;
	/* update 'in_buf' to point to next (previous) line before
	   forward (backward) searching */
	back_lines(obj, (dir
			 ? obj->page_length - 1
			 : obj->page_length + 1));
	if (dir) {		/* Forward search */
	    while ((found = match_string(obj, search_term)) == FALSE) {
		if (obj->end_reached)
		    break;
	    }
	} else {		/* Backward search */
	    while ((found = match_string(obj, search_term)) == FALSE) {
		if (obj->begin_reached)
		    break;
		back_lines(obj, 2L);
	    }
	}
	if (found == FALSE) {	/* not found */
	    (void) beep();
	    /* Restore program state to that before searching */
	    lseek_set(obj, fpos);

	    read_high(obj, BUF_SIZE);

	    obj->in_buf = tempinx;
	    obj->begin_reached = temp;
	    obj->end_reached = temp1;
	    /* move 'in_buf' to point to start of current page to
	     * re-print current page.  Note that 'in_buf' always points
	     * to start of next page, so this is necessary
	     */
	    back_lines(obj, obj->page_length);
	} else {		/* Search term found */
	    back_lines(obj, 1L);
	}
	/* Reprint page */
	dlg_attrset(obj->text, dialog_attr);
	moved = TRUE;
    } else {			/* no need to find */
	(void) beep();
    }
    return moved;
}

/*
 * Display text from a file in a dialog box.
 */
int
dialog_textbox(const char *title, const char *filename, int height, int width)
{
    /* *INDENT-OFF* */
    static DLG_KEYS_BINDING binding[] = {
	HELPKEY_BINDINGS,
	ENTERKEY_BINDINGS,
	DLG_KEYS_DATA( DLGK_GRID_DOWN,  'J' ),
	DLG_KEYS_DATA( DLGK_GRID_DOWN,  'j' ),
	DLG_KEYS_DATA( DLGK_GRID_DOWN,  KEY_DOWN ),
	DLG_KEYS_DATA( DLGK_GRID_LEFT,  'H' ),
	DLG_KEYS_DATA( DLGK_GRID_LEFT,  'h' ),
	DLG_KEYS_DATA( DLGK_GRID_LEFT,  KEY_LEFT ),
	DLG_KEYS_DATA( DLGK_GRID_RIGHT, 'L' ),
	DLG_KEYS_DATA( DLGK_GRID_RIGHT, 'l' ),
	DLG_KEYS_DATA( DLGK_GRID_RIGHT, KEY_RIGHT ),
	DLG_KEYS_DATA( DLGK_GRID_UP,    'K' ),
	DLG_KEYS_DATA( DLGK_GRID_UP,    'k' ),
	DLG_KEYS_DATA( DLGK_GRID_UP,    KEY_UP ),
	DLG_KEYS_DATA( DLGK_PAGE_FIRST, 'g' ),
	DLG_KEYS_DATA( DLGK_PAGE_FIRST, KEY_HOME ),
	DLG_KEYS_DATA( DLGK_PAGE_LAST,  'G' ),
	DLG_KEYS_DATA( DLGK_PAGE_LAST,  KEY_END ),
	DLG_KEYS_DATA( DLGK_PAGE_LAST,  KEY_LL ),
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,  CHR_SPACE ),
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,  KEY_NPAGE ),
	DLG_KEYS_DATA( DLGK_PAGE_PREV,  'B' ),
	DLG_KEYS_DATA( DLGK_PAGE_PREV,  'b' ),
	DLG_KEYS_DATA( DLGK_PAGE_PREV,  KEY_PPAGE ),
	DLG_KEYS_DATA( DLGK_BEGIN,	'0' ),
	DLG_KEYS_DATA( DLGK_BEGIN,	KEY_BEG ),
	DLG_KEYS_DATA( DLGK_FIELD_NEXT, TAB ),
	DLG_KEYS_DATA( DLGK_FIELD_PREV, KEY_BTAB ),
	END_KEYS_BINDING
    };
    /* *INDENT-ON* */

#ifdef KEY_RESIZE
    int old_height = height;
    int old_width = width;
#endif
    long fpos;
    int x, y, cur_x, cur_y;
    int key = 0, fkey;
    int next = 0;
    int i, code, passed_end;
    char search_term[MAX_LEN + 1];
    MY_OBJ obj;
    WINDOW *dialog;
    bool moved;
    int result = DLG_EXIT_UNKNOWN;
    int button = dlg_default_button();
    int min_width = 12;

    DLG_TRACE(("# textbox args:\n"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("filename", filename);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);

    search_term[0] = '\0';	/* no search term entered yet */

    memset(&obj, 0, sizeof(obj));

    obj.begin_reached = TRUE;
    obj.buffer_first = TRUE;
    obj.end_reached = FALSE;
    obj.buttons = dlg_exit_label();

    /* Open input file for reading */
    if ((obj.fd = open(filename, O_RDONLY)) == -1)
	dlg_exiterr("Can't open input file %s", filename);

    /* Get file size. Actually, 'file_size' is the real file size - 1,
       since it's only the last byte offset from the beginning */
    lseek_end(&obj, 0L);

    /* Restore file pointer to beginning of file after getting file size */
    lseek_set(&obj, 0L);

    read_high(&obj, BUF_SIZE);

    dlg_button_layout(obj.buttons, &min_width);

#ifdef KEY_RESIZE
  retry:
#endif
    moved = TRUE;

    dlg_auto_sizefile(title, filename, &height, &width, 2, min_width);
    dlg_print_size(height, width);
    dlg_ctl_size(height, width);

    x = dlg_box_x_ordinate(width);
    y = dlg_box_y_ordinate(height);

    dialog = dlg_new_window(height, width, y, x);
    dlg_register_window(dialog, "textbox", binding);
    dlg_register_buttons(dialog, "textbox", obj.buttons);

    dlg_mouse_setbase(x, y);

    /* Create window for text region, used for scrolling text */
    obj.text = dlg_sub_window(dialog, PAGE_LENGTH, PAGE_WIDTH, y + 1, x + 1);

    /* register the new window, along with its borders */
    dlg_mouse_mkbigregion(0, 0, PAGE_LENGTH + 2, width, KEY_MAX, 1, 1, 1 /* lines */ );
    dlg_draw_box2(dialog, 0, 0, height, width, dialog_attr, border_attr, border2_attr);
    dlg_draw_bottom_box2(dialog, border_attr, border2_attr, dialog_attr);
    dlg_draw_title(dialog, title);

    dlg_draw_buttons(dialog, PAGE_LENGTH + 2, 0, obj.buttons, button, FALSE, width);
    (void) wnoutrefresh(dialog);
    getyx(dialog, cur_y, cur_x);	/* Save cursor position */

    dlg_attr_clear(obj.text, PAGE_LENGTH, PAGE_WIDTH, dialog_attr);

    while (result == DLG_EXIT_UNKNOWN) {

	/*
	 * Update the screen according to whether we shifted up/down by a line
	 * or not.
	 */
	if (moved) {
	    if (next < 0) {
		(void) scrollok(obj.text, TRUE);
		(void) scroll(obj.text);	/* Scroll text region up one line */
		(void) scrollok(obj.text, FALSE);
		print_line(&obj, PAGE_LENGTH - 1, PAGE_WIDTH);
		(void) wnoutrefresh(obj.text);
	    } else if (next > 0) {
		/*
		 * We don't call print_page() here but use scrolling to ensure
		 * faster screen update.  However, 'end_reached' and
		 * 'page_length' should still be updated, and 'in_buf' should
		 * point to start of next page.  This is done by calling
		 * get_line() in the following 'for' loop.
		 */
		(void) scrollok(obj.text, TRUE);
		(void) wscrl(obj.text, -1);	/* Scroll text region down one line */
		(void) scrollok(obj.text, FALSE);
		obj.page_length = 0;
		passed_end = 0;
		for (i = 0; i < PAGE_LENGTH; i++) {
		    if (!i) {
			print_line(&obj, 0, PAGE_WIDTH);	/* print first line of page */
			(void) wnoutrefresh(obj.text);
		    } else
			(void) get_line(&obj);	/* Called to update 'end_reached' and 'in_buf' */
		    if (!passed_end)
			obj.page_length++;
		    if (obj.end_reached && !passed_end)
			passed_end = 1;
		}
	    } else {
		print_page(&obj, PAGE_LENGTH, PAGE_WIDTH);
	    }
	    print_position(&obj, dialog, height, width);
	    (void) wmove(dialog, cur_y, cur_x);		/* Restore cursor position */
	    wrefresh(dialog);
	}
	moved = FALSE;		/* assume we'll not move */
	next = 0;		/* ...but not scroll by a line */

	key = dlg_mouse_wgetch(dialog, &fkey);
	if (dlg_result_key(key, fkey, &result))
	    break;

	if (!fkey && (code = dlg_char_to_button(key, obj.buttons)) >= 0) {
	    result = dlg_ok_buttoncode(code);
	    break;
	}

	if (fkey) {
	    switch (key) {
	    default:
		if (is_DLGK_MOUSE(key)) {
		    result = dlg_exit_buttoncode(key - M_EVENT);
		    if (result < 0)
			result = DLG_EXIT_OK;
		} else {
		    beep();
		}
		break;
	    case DLGK_FIELD_NEXT:
		button = dlg_next_button(obj.buttons, button);
		if (button < 0)
		    button = 0;
		dlg_draw_buttons(dialog,
				 height - 2, 0,
				 obj.buttons, button,
				 FALSE, width);
		break;
	    case DLGK_FIELD_PREV:
		button = dlg_prev_button(obj.buttons, button);
		if (button < 0)
		    button = 0;
		dlg_draw_buttons(dialog,
				 height - 2, 0,
				 obj.buttons, button,
				 FALSE, width);
		break;
	    case DLGK_ENTER:
		if (dialog_vars.nook)
		    result = DLG_EXIT_OK;
		else
		    result = dlg_exit_buttoncode(button);
		break;
	    case DLGK_PAGE_FIRST:
		if (!obj.begin_reached) {
		    obj.begin_reached = 1;
		    /* First page not in buffer? */
		    fpos = ftell_obj(&obj);

		    if (fpos > obj.fd_bytes_read) {
			/* Yes, we have to read it in */
			lseek_set(&obj, 0L);

			read_high(&obj, BUF_SIZE);
		    }
		    obj.in_buf = 0;
		    moved = TRUE;
		}
		break;
	    case DLGK_PAGE_LAST:
		obj.end_reached = TRUE;
		/* Last page not in buffer? */
		fpos = ftell_obj(&obj);

		if (fpos < obj.file_size) {
		    /* Yes, we have to read it in */
		    lseek_end(&obj, -BUF_SIZE);

		    read_high(&obj, BUF_SIZE);
		}
		obj.in_buf = obj.bytes_read;
		back_lines(&obj, (long) PAGE_LENGTH);
		moved = TRUE;
		break;
	    case DLGK_GRID_UP:	/* Previous line */
		if (!obj.begin_reached) {
		    back_lines(&obj, obj.page_length + 1);
		    next = 1;
		    moved = TRUE;
		}
		break;
	    case DLGK_PAGE_PREV:	/* Previous page */
	    case DLGK_MOUSE(KEY_PPAGE):
		if (!obj.begin_reached) {
		    back_lines(&obj, obj.page_length + PAGE_LENGTH);
		    moved = TRUE;
		}
		break;
	    case DLGK_GRID_DOWN:	/* Next line */
		if (!obj.end_reached) {
		    obj.begin_reached = 0;
		    next = -1;
		    moved = TRUE;
		}
		break;
	    case DLGK_PAGE_NEXT:	/* Next page */
	    case DLGK_MOUSE(KEY_NPAGE):
		if (!obj.end_reached) {
		    obj.begin_reached = 0;
		    moved = TRUE;
		}
		break;
	    case DLGK_BEGIN:	/* Beginning of line */
		if (obj.hscroll > 0) {
		    obj.hscroll = 0;
		    /* Reprint current page to scroll horizontally */
		    back_lines(&obj, obj.page_length);
		    moved = TRUE;
		}
		break;
	    case DLGK_GRID_LEFT:	/* Scroll left */
		if (obj.hscroll > 0) {
		    obj.hscroll--;
		    /* Reprint current page to scroll horizontally */
		    back_lines(&obj, obj.page_length);
		    moved = TRUE;
		}
		break;
	    case DLGK_GRID_RIGHT:	/* Scroll right */
		if (obj.hscroll < MAX_LEN) {
		    obj.hscroll++;
		    /* Reprint current page to scroll horizontally */
		    back_lines(&obj, obj.page_length);
		    moved = TRUE;
		}
		break;
#ifdef KEY_RESIZE
	    case KEY_RESIZE:
		dlg_will_resize(dialog);
		/* reset data */
		height = old_height;
		width = old_width;
		back_lines(&obj, obj.page_length);
		/* repaint */
		dlg_clear();
		dlg_del_window(dialog);
		dlg_mouse_free_regions();
		goto retry;
#endif
	    }
	} else {
	    switch (key) {
	    case '/':		/* Forward search */
	    case 'n':		/* Repeat forward search */
	    case '?':		/* Backward search */
	    case 'N':		/* Repeat backward search */
		moved = perform_search(&obj, height, width, key, search_term);
		fkey = FALSE;
		break;
	    default:
		beep();
		break;
	    }
	}
    }

    dlg_del_window(dialog);
    free(obj.buf);
    (void) close(obj.fd);
    dlg_mouse_free_regions();
    return result;
}
