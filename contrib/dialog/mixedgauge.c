/*
 *  $Id: mixedgauge.c,v 1.34 2018/06/18 22:09:31 tom Exp $
 *
 *  mixedgauge.c -- implements the mixedgauge dialog
 *
 *  Copyright 2007-2012,2018	Thomas E. Dickey
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
 *  This is inspired by a patch from Kiran Cherupally
 *  (but different interface design).
 */

#include <dialog.h>

#define LLEN(n) ((n) * MIXEDGAUGE_TAGS)
#define ItemData(i)    &items[LLEN(i)]
#define ItemName(i)    items[LLEN(i)]
#define ItemText(i)    items[LLEN(i) + 1]

#define MIN_HIGH (4)
#define MIN_WIDE (10 + 2 * (2 + MARGIN))

typedef struct {
    WINDOW *dialog;
    WINDOW *caption;
    const char *title;
    char *prompt;
    int height, old_height, min_height;
    int width, old_width, min_width;
    int len_name, len_text;
    int item_no;
    DIALOG_LISTITEM *list;
} DIALOG_MIXEDGAUGE;

static const char *
status_string(char *given, char **freeMe)
{
    const char *result;

    *freeMe = 0;
    if (isdigit(UCH(*given))) {
	switch (*given) {
	case '0':
	    result = _("Succeeded");
	    break;
	case '1':
	    result = _("Failed");
	    break;
	case '2':
	    result = _("Passed");
	    break;
	case '3':
	    result = _("Completed");
	    break;
	case '4':
	    result = _("Checked");
	    break;
	case '5':
	    result = _("Done");
	    break;
	case '6':
	    result = _("Skipped");
	    break;
	case '7':
	    result = _("In Progress");
	    break;
	case '8':
	    result = "";
	    break;
	case '9':
	    result = _("N/A");
	    break;
	default:
	    result = "?";
	    break;
	}
    } else if (*given == '-') {
	size_t need = strlen(++given) + 4;
	char *temp = dlg_malloc(char, need);
	*freeMe = temp;
	sprintf(temp, "%3s%%", given);
	result = temp;
    } else if (!isspace(UCH(*given))) {
	result = given;
    } else {
	result = 0;
    }
    return result;
}

/* This function displays status messages */
static void
myprint_status(DIALOG_MIXEDGAUGE * dlg)
{
    WINDOW *win = dlg->dialog;
    int limit_y = dlg->height;
    int limit_x = dlg->width;

    int y = MARGIN;
    int item;
    int cells = dlg->len_text - 2;
    int lm = limit_x - dlg->len_text - 1;
    int bm = limit_y;		/* bottom margin */
    int last_y = 0, last_x = 0;
    int j, xxx;
    float percent;
    const char *status = "";
    char *freeMe = 0;

    bm -= (2 * MARGIN);
    getyx(win, last_y, last_x);
    for (item = 0; item < dlg->item_no; ++item) {
	chtype attr = A_NORMAL;

	y = item + MARGIN + 1;
	if (y > bm)
	    break;

	status = status_string(dlg->list[item].text, &freeMe);
	if (status == 0 || *status == 0)
	    continue;

	(void) wmove(win, y, 2 * MARGIN);
	dlg_attrset(win, dialog_attr);
	dlg_print_text(win, dlg->list[item].name, lm, &attr);

	(void) wmove(win, y, lm);
	(void) waddch(win, '[');
	(void) wmove(win, y, lm + (cells - (int) strlen(status)) / 2);
	if (freeMe) {
	    (void) wmove(win, y, lm + 1);
	    dlg_attrset(win, title_attr);
	    for (j = 0; j < cells; j++)
		(void) waddch(win, ' ');

	    (void) wmove(win, y, lm + (cells - (int) strlen(status)) / 2);
	    (void) waddstr(win, status);

	    if ((title_attr & A_REVERSE) != 0) {
		dlg_attroff(win, A_REVERSE);
	    } else {
		dlg_attrset(win, A_REVERSE);
	    }
	    (void) wmove(win, y, lm + 1);

	    if (sscanf(status, "%f%%", &percent) != 1)
		percent = 0.0;
	    xxx = (int) ((cells * (percent + 0.5)) / 100.0);
	    for (j = 0; j < xxx; j++) {
		chtype ch1 = winch(win);
		if (title_attr & A_REVERSE) {
		    ch1 &= ~A_REVERSE;
		}
		(void) waddch(win, ch1);
	    }
	    free(freeMe);

	} else {
	    (void) wmove(win, y, lm + (cells - (int) strlen(status)) / 2);
	    (void) waddstr(win, status);
	}
	(void) wmove(win, y, limit_x - 3);
	dlg_attrset(win, dialog_attr);
	(void) waddch(win, ']');
	(void) wnoutrefresh(win);
    }
    if (win != 0)
	wmove(win, last_y, last_x);
}

static void
mydraw_mixed_box(WINDOW *win, int y, int x, int height, int width,
		 chtype boxchar, chtype borderchar)
{
    dlg_draw_box(win, y, x, height, width, boxchar, borderchar);
    {
	chtype attr = A_NORMAL;
	const char *message = _("Overall Progress");
	chtype save2 = dlg_get_attrs(win);
	dlg_attrset(win, title_attr);
	(void) wmove(win, y, x + 2);
	dlg_print_text(win, message, width, &attr);
	dlg_attrset(win, save2);
    }
}

static char *
clean_copy(const char *string)
{
    char *result = dlg_strclone(string);

    dlg_trim_string(result);
    dlg_tab_correct_str(result);
    return result;
}

/*
 * Update mixed-gauge dialog (may be from pipe, may be via direct calls).
 */
static void
dlg_update_mixedgauge(DIALOG_MIXEDGAUGE * dlg, int percent)
{
    int i, x;

    /*
     * Clear the area for the progress bar by filling it with spaces
     * in the title-attribute, and write the percentage with that
     * attribute.
     */
    (void) wmove(dlg->dialog, dlg->height - 3, 4);
    dlg_attrset(dlg->dialog, gauge_attr);

    for (i = 0; i < (dlg->width - 2 * (3 + MARGIN)); i++)
	(void) waddch(dlg->dialog, ' ');

    (void) wmove(dlg->dialog, dlg->height - 3, (dlg->width / 2) - 2);
    (void) wprintw(dlg->dialog, "%3d%%", percent);

    /*
     * Now draw a bar in reverse, relative to the background.
     * The window attribute was useful for painting the background,
     * but requires some tweaks to reverse it.
     */
    x = (percent * (dlg->width - 2 * (3 + MARGIN))) / 100;
    if ((title_attr & A_REVERSE) != 0) {
	dlg_attroff(dlg->dialog, A_REVERSE);
    } else {
	dlg_attrset(dlg->dialog, A_REVERSE);
    }
    (void) wmove(dlg->dialog, dlg->height - 3, 4);
    for (i = 0; i < x; i++) {
	chtype ch = winch(dlg->dialog);
	if (title_attr & A_REVERSE) {
	    ch &= ~A_REVERSE;
	}
	(void) waddch(dlg->dialog, ch);
    }
    myprint_status(dlg);
    dlg_trace_win(dlg->dialog);
}

/*
 * Setup dialog.
 */
static void
dlg_begin_mixedgauge(DIALOG_MIXEDGAUGE * dlg,
		     int *began,
		     const char *aTitle,
		     const char *aPrompt,
		     int aHeight,
		     int aWidth,
		     int aItemNo,
		     char **items)
{
    int n, y, x;

    if (!*began) {
	curs_set(0);

	memset(dlg, 0, sizeof(*dlg));
	dlg->title = aTitle;
	dlg->prompt = clean_copy(aPrompt);
	dlg->height = dlg->old_height = aHeight;
	dlg->width = dlg->old_width = aWidth;
	dlg->item_no = aItemNo;

	dlg->list = dlg_calloc(DIALOG_LISTITEM, (size_t) aItemNo);
	assert_ptr(dlg->list, "dialog_mixedgauge");

	dlg->len_name = 0;
	dlg->len_text = 15;

	for (n = 0; n < aItemNo; ++n) {
	    int thisWidth = (int) strlen(ItemName(n));
	    if (dlg->len_name < thisWidth)
		dlg->len_name = thisWidth;
	    dlg->list[n].name = ItemName(n);
	    dlg->list[n].text = ItemText(n);
	}

	dlg->min_height = MIN_HIGH + aItemNo;
	dlg->min_width = MIN_WIDE + dlg->len_name + GUTTER + dlg->len_text;

	if (dlg->prompt != 0 && *(dlg->prompt) != 0)
	    dlg->min_height += (2 * MARGIN);
#ifdef KEY_RESIZE
	nodelay(stdscr, TRUE);
#endif
    }
#ifdef KEY_RESIZE
    else {
	dlg_del_window(dlg->dialog);
	dlg->height = dlg->old_height;
	dlg->width = dlg->old_width;
    }
#endif

    dlg_auto_size(dlg->title, dlg->prompt,
		  &(dlg->height),
		  &(dlg->width),
		  dlg->min_height,
		  dlg->min_width);
    dlg_print_size(dlg->height, dlg->width);
    dlg_ctl_size(dlg->height, dlg->width);

    /* center dialog box on screen */
    x = dlg_box_x_ordinate(dlg->width);
    y = dlg_box_y_ordinate(dlg->height);

    dlg->dialog = dlg_new_window(dlg->height, dlg->width, y, x);

    (void) werase(dlg->dialog);
    dlg_draw_box2(dlg->dialog,
		  0, 0,
		  dlg->height,
		  dlg->width,
		  dialog_attr, border_attr, border2_attr);

    dlg_draw_title(dlg->dialog, dlg->title);
    dlg_draw_helpline(dlg->dialog, FALSE);

    if ((dlg->prompt != 0 && *(dlg->prompt) != 0)
	&& wmove(dlg->dialog, dlg->item_no, 0) != ERR) {
	dlg->caption = dlg_sub_window(dlg->dialog,
				      dlg->height - dlg->item_no - (2 * MARGIN),
				      dlg->width,
				      y + dlg->item_no + (2 * MARGIN),
				      x);
	dlg_attrset(dlg->caption, dialog_attr);
	dlg_print_autowrap(dlg->caption, dlg->prompt, dlg->height, dlg->width);
    }

    mydraw_mixed_box(dlg->dialog,
		     dlg->height - 4,
		     2 + MARGIN,
		     2 + MARGIN,
		     dlg->width - 2 * (2 + MARGIN),
		     dialog_attr,
		     border_attr);

    *began += 1;
}

/*
 * Discard the mixed-gauge dialog.
 */
static int
dlg_finish_mixedgauge(DIALOG_MIXEDGAUGE * dlg, int status)
{
    (void) wrefresh(dlg->dialog);
#ifdef KEY_RESIZE
    nodelay(stdscr, FALSE);
#endif
    curs_set(1);
    dlg_del_window(dlg->dialog);
    return status;
}

/*
 * Setup dialog, read mixed-gauge data from pipe.
 */
int
dialog_mixedgauge(const char *title,
		  const char *cprompt,
		  int height,
		  int width,
		  int percent,
		  int item_no,
		  char **items)
{
    DIALOG_MIXEDGAUGE dlg;
    int began = 0;

    DLG_TRACE(("# mixedgauge args:\n"));
    DLG_TRACE2S("title", title);
    DLG_TRACE2S("message", cprompt);
    DLG_TRACE2N("height", height);
    DLG_TRACE2N("width", width);
    DLG_TRACE2N("percent", percent);
    DLG_TRACE2N("llength", item_no);
    /* FIXME dump the items[][] too */

    dlg_begin_mixedgauge(&dlg, &began, title, cprompt, height,
			 width, item_no, items);

    dlg_update_mixedgauge(&dlg, percent);

    return dlg_finish_mixedgauge(&dlg, DLG_EXIT_OK);
}
