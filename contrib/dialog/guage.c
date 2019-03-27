/*
 *  $Id: guage.c,v 1.76 2018/06/21 08:23:43 tom Exp $
 *
 *  guage.c -- implements the gauge dialog
 *
 *  Copyright 2000-2015,2018	Thomas E. Dickey
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
 *  An earlier version of this program lists as authors
 *	Marc Ewing, Red Hat Software
 */

#include <dialog.h>

#include <errno.h>

#define MY_LEN (MAX_LEN)/2

#define MIN_HIGH (4)
#define MIN_WIDE (10 + 2 * (2 + MARGIN))

#define isMarker(buf) !strncmp(buf, "XXX", (size_t) 3)

typedef struct _my_obj {
    DIALOG_CALLBACK obj;	/* has to be first in struct */
    struct _my_obj *next;
    WINDOW *text;
    char *title;
    char *prompt;
    char prompt_buf[MY_LEN];
    int percent;
    int height;
    int width;
    char line[MAX_LEN + 1];
} MY_OBJ;

static MY_OBJ *all_objects;

static int
valid(MY_OBJ * obj)
{
    MY_OBJ *list = all_objects;
    int result = 0;

    while (list != 0) {
	if (list == obj) {
	    result = 1;
	    break;
	}
	list = list->next;
    }
    return result;
}

static void
delink(MY_OBJ * obj)
{
    MY_OBJ *p = all_objects;
    MY_OBJ *q = 0;
    while (p != 0) {
	if (p == obj) {
	    if (q != 0) {
		q->next = p->next;
	    } else {
		all_objects = p->next;
	    }
	    break;
	}
	q = p;
	p = p->next;
    }
}

static int
read_data(char *buffer, FILE *fp)
{
    int result;

    if (feof(fp)) {
	result = 0;
    } else if (fgets(buffer, MY_LEN, fp) != 0) {
	DLG_TRACE(("read_data:%s", buffer));
	buffer[MY_LEN] = '\0';
	dlg_trim_string(buffer);
	result = 1;
    } else {
	result = -1;
    }
    return result;
}

static int
decode_percent(char *buffer)
{
    char *tmp = 0;
    long value = strtol(buffer, &tmp, 10);

    if (tmp != 0 && (*tmp == 0 || isspace(UCH(*tmp))) && value >= 0) {
	return TRUE;
    }
    return FALSE;
}

static void
repaint_text(MY_OBJ * obj)
{
    WINDOW *dialog = obj->obj.win;
    int i, x;

    if (dialog != 0) {
	(void) werase(dialog);
	dlg_draw_box2(dialog, 0, 0, obj->height, obj->width, dialog_attr,
		      border_attr, border2_attr);

	dlg_draw_title(dialog, obj->title);

	dlg_attrset(dialog, dialog_attr);
	dlg_draw_helpline(dialog, FALSE);
	dlg_print_autowrap(dialog, obj->prompt, obj->height, obj->width);

	dlg_draw_box2(dialog,
		      obj->height - 4, 2 + MARGIN,
		      2 + MARGIN, obj->width - 2 * (2 + MARGIN),
		      dialog_attr,
		      border_attr,
		      border2_attr);

	/*
	 * Clear the area for the progress bar by filling it with spaces
	 * in the gauge-attribute, and write the percentage with that
	 * attribute.
	 */
	(void) wmove(dialog, obj->height - 3, 4);
	dlg_attrset(dialog, gauge_attr);

	for (i = 0; i < (obj->width - 2 * (3 + MARGIN)); i++)
	    (void) waddch(dialog, ' ');

	(void) wmove(dialog, obj->height - 3, (obj->width / 2) - 2);
	(void) wprintw(dialog, "%3d%%", obj->percent);

	/*
	 * Now draw a bar in reverse, relative to the background.
	 * The window attribute was useful for painting the background,
	 * but requires some tweaks to reverse it.
	 */
	x = (obj->percent * (obj->width - 2 * (3 + MARGIN))) / 100;
	if ((gauge_attr & A_REVERSE) != 0) {
	    dlg_attroff(dialog, A_REVERSE);
	} else {
	    dlg_attrset(dialog, A_REVERSE);
	}
	(void) wmove(dialog, obj->height - 3, 4);
	for (i = 0; i < x; i++) {
	    chtype ch2 = winch(dialog);
	    if (gauge_attr & A_REVERSE) {
		ch2 &= ~A_REVERSE;
	    }
	    (void) waddch(dialog, ch2);
	}

	(void) wrefresh(dialog);
    }
}

static bool
handle_input(DIALOG_CALLBACK * cb)
{
    MY_OBJ *obj = (MY_OBJ *) cb;
    bool result;
    bool cleanup = FALSE;
    int status;
    char buf[MY_LEN + 1];

    if (dialog_state.pipe_input == 0) {
	status = -1;
	cleanup = TRUE;
    } else if ((status = read_data(buf, dialog_state.pipe_input)) > 0) {

	if (isMarker(buf)) {
	    /*
	     * Historically, next line should be percentage, but one of the
	     * worse-written clones of 'dialog' assumes the number is missing.
	     * (Gresham's Law applied to software).
	     */
	    if ((status = read_data(buf, dialog_state.pipe_input)) > 0) {

		obj->prompt_buf[0] = '\0';
		if (decode_percent(buf))
		    obj->percent = atoi(buf);
		else
		    strcpy(obj->prompt_buf, buf);

		/* Rest is message text */
		while ((status = read_data(buf, dialog_state.pipe_input)) > 0
		       && !isMarker(buf)) {
		    if (strlen(obj->prompt_buf) + strlen(buf) <
			sizeof(obj->prompt_buf) - 1) {
			strcat(obj->prompt_buf, buf);
		    }
		}

		if (obj->prompt != obj->prompt_buf)
		    free(obj->prompt);
		obj->prompt = obj->prompt_buf;
	    }
	} else if (decode_percent(buf)) {
	    obj->percent = atoi(buf);
	}
    } else {
	if (feof(dialog_state.pipe_input) ||
	    (ferror(dialog_state.pipe_input) && errno != EINTR)) {
	    cleanup = TRUE;
	}
    }

    repaint_text(obj);
    if (status > 0) {
	result = TRUE;
    } else {
	result = FALSE;
	if (cleanup) {
	    dlg_remove_callback(cb);
	    delink(obj);
	}
    }

    return result;
}

static bool
handle_my_getc(DIALOG_CALLBACK * cb, int ch, int fkey, int *result)
{
    bool status = TRUE;

    *result = DLG_EXIT_OK;
    if (cb != 0) {
	if (!fkey && (ch == ERR)) {
	    (void) handle_input(cb);
	    /* cb might be freed in handle_input */
	    status = (valid((MY_OBJ *) cb) && (cb->input != 0));
	}
    } else {
	status = FALSE;
    }
    return status;
}

static void
my_cleanup(DIALOG_CALLBACK * cb)
{
    MY_OBJ *obj = (MY_OBJ *) cb;

    if (valid(obj)) {
	if (obj->prompt != obj->prompt_buf) {
	    free(obj->prompt);
	    obj->prompt = obj->prompt_buf;
	}
	free(obj->title);
	dlg_del_window(obj->obj.win);
	delink(obj);
    }
}

void
dlg_update_gauge(void *objptr, int percent)
{
    MY_OBJ *obj = (MY_OBJ *) objptr;
    bool save_finish_string = dialog_state.finish_string;

    dialog_state.finish_string = TRUE;
    curs_set(0);
    obj->percent = percent;
    repaint_text(obj);
    dialog_state.finish_string = save_finish_string;
}

/*
 * (Re)Allocates an object and fills it as per the arguments
 */
void *
dlg_reallocate_gauge(void *objptr,
		     const char *title,
		     const char *cprompt,
		     int height,
		     int width,
		     int percent)
{
    char *prompt = dlg_strclone(cprompt);
    MY_OBJ *obj = objptr;
    bool save_finish_string = dialog_state.finish_string;

    dialog_state.finish_string = TRUE;
    dlg_tab_correct_str(prompt);

    if (objptr == 0) {
	/* create a new object */
	obj = dlg_calloc(MY_OBJ, 1);
	assert_ptr(obj, "dialog_gauge");

	dlg_auto_size(title, prompt, &height, &width, MIN_HIGH, MIN_WIDE);
	dlg_print_size(height, width);
	dlg_ctl_size(height, width);

    } else {
	/* reuse an existing object */
	obj = objptr;
	height = obj->height;
	width = obj->width;
    }

    if (obj->obj.win == 0) {
	/* center dialog box on screen */
	int x = dlg_box_x_ordinate(width);
	int y = dlg_box_y_ordinate(height);
	WINDOW *dialog = dlg_new_window(height, width, y, x);
	obj->obj.win = dialog;
    }

    obj->obj.input = dialog_state.pipe_input;
    obj->obj.keep_win = TRUE;
    obj->obj.bg_task = TRUE;
    obj->obj.handle_getc = handle_my_getc;
    obj->obj.handle_input = handle_input;

    if (obj->title == 0 || strcmp(obj->title, title)) {
	dlg_finish_string(obj->title);
	free(obj->title);
	obj->title = dlg_strclone(title);
    }

    dlg_finish_string(obj->prompt);
    free(obj->prompt);

    obj->prompt = prompt;
    obj->percent = percent;
    obj->height = height;
    obj->width = width;

    /* if this was a new object, link it into the list */
    if (objptr == 0) {
	obj->next = all_objects;
	all_objects = obj;
    }

    dialog_state.finish_string = save_finish_string;
    return (void *) obj;
}

void *
dlg_allocate_gauge(const char *title,
		   const char *cprompt,
		   int height,
		   int width,
		   int percent)
{
    return dlg_reallocate_gauge(NULL, title, cprompt, height, width, percent);
}

void
dlg_free_gauge(void *objptr)
{
    MY_OBJ *obj = (MY_OBJ *) objptr;

    if (valid(obj)) {
	obj->obj.keep_win = FALSE;
	dlg_remove_callback(&(obj->obj));
	delink(obj);
    }
    curs_set(1);
}

/*
 * Display a gauge, or progress meter.  Starts at percent% and reads stdin.  If
 * stdin is not XXX, then it is interpreted as a percentage, and the display is
 * updated accordingly.  Otherwise the next line is the percentage, and
 * subsequent lines up to another XXX are used for the new prompt.  Note that
 * the size of the window never changes, so the prompt can not get any larger
 * than the height and width specified.
 */
int
dialog_gauge(const char *title,
	     const char *cprompt,
	     int height,
	     int width,
	     int percent)
{
    int fkey;
    int ch, result;
    void *objptr = dlg_allocate_gauge(title, cprompt, height, width, percent);
    MY_OBJ *obj = (MY_OBJ *) objptr;

    DLG_TRACE(("# gauge args:\n"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("message", cprompt);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);
    DLG_TRACE2N("percent", percent);

    dlg_add_callback_ref((DIALOG_CALLBACK **) & obj, my_cleanup);
    dlg_update_gauge(obj, percent);

    dlg_trace_win(obj->obj.win);
    do {
	ch = dlg_getc(obj->obj.win, &fkey);
#ifdef KEY_RESIZE
	if (fkey && ch == KEY_RESIZE) {
	    MY_OBJ *oldobj = obj;

	    dlg_will_resize(obj->obj.win);
	    dlg_mouse_free_regions();

	    obj = dlg_allocate_gauge(title,
				     cprompt,
				     height,
				     width,
				     oldobj->percent);

	    /* avoid breaking new window in dlg_remove_callback */
	    oldobj->obj.caller = 0;
	    oldobj->obj.input = 0;
	    oldobj->obj.keep_win = FALSE;

	    /* remove the old version of the gauge */
	    dlg_clear();
	    dlg_remove_callback(&(oldobj->obj));
	    refresh();

	    dlg_add_callback_ref((DIALOG_CALLBACK **) & obj, my_cleanup);
	    dlg_update_gauge(obj, obj->percent);
	}
#endif
    }
    while (valid(obj) && handle_my_getc(&(obj->obj), ch, fkey, &result));

    dlg_free_gauge(obj);

    return (DLG_EXIT_OK);
}
