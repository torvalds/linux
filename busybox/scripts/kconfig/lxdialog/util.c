/*
 *  util.c
 *
 *  ORIGINAL AUTHOR: Savio Lam (lam836@cs.cuhk.hk)
 *  MODIFIED FOR LINUX KERNEL CONFIG BY: William Roadcap (roadcap@cfw.com)
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "dialog.h"

/* use colors by default? */
bool use_colors = 1;

const char *backtitle = NULL;

/*
 * Attribute values, default is for mono display
 */
chtype attributes[] = {
	A_NORMAL,		/* screen_attr */
	A_NORMAL,		/* shadow_attr */
	A_NORMAL,		/* dialog_attr */
	A_BOLD,			/* title_attr */
	A_NORMAL,		/* border_attr */
	A_REVERSE,		/* button_active_attr */
	A_DIM,			/* button_inactive_attr */
	A_REVERSE,		/* button_key_active_attr */
	A_BOLD,			/* button_key_inactive_attr */
	A_REVERSE,		/* button_label_active_attr */
	A_NORMAL,		/* button_label_inactive_attr */
	A_NORMAL,		/* inputbox_attr */
	A_NORMAL,		/* inputbox_border_attr */
	A_NORMAL,		/* searchbox_attr */
	A_BOLD,			/* searchbox_title_attr */
	A_NORMAL,		/* searchbox_border_attr */
	A_BOLD,			/* position_indicator_attr */
	A_NORMAL,		/* menubox_attr */
	A_NORMAL,		/* menubox_border_attr */
	A_NORMAL,		/* item_attr */
	A_REVERSE,		/* item_selected_attr */
	A_BOLD,			/* tag_attr */
	A_REVERSE,		/* tag_selected_attr */
	A_BOLD,			/* tag_key_attr */
	A_REVERSE,		/* tag_key_selected_attr */
	A_BOLD,			/* check_attr */
	A_REVERSE,		/* check_selected_attr */
	A_BOLD,			/* uarrow_attr */
	A_BOLD			/* darrow_attr */
};

#include "colors.h"

/*
 * Table of color values
 */
int color_table[][3] = {
	{SCREEN_FG, SCREEN_BG, SCREEN_HL},
	{SHADOW_FG, SHADOW_BG, SHADOW_HL},
	{DIALOG_FG, DIALOG_BG, DIALOG_HL},
	{TITLE_FG, TITLE_BG, TITLE_HL},
	{BORDER_FG, BORDER_BG, BORDER_HL},
	{BUTTON_ACTIVE_FG, BUTTON_ACTIVE_BG, BUTTON_ACTIVE_HL},
	{BUTTON_INACTIVE_FG, BUTTON_INACTIVE_BG, BUTTON_INACTIVE_HL},
	{BUTTON_KEY_ACTIVE_FG, BUTTON_KEY_ACTIVE_BG, BUTTON_KEY_ACTIVE_HL},
	{BUTTON_KEY_INACTIVE_FG, BUTTON_KEY_INACTIVE_BG,
	 BUTTON_KEY_INACTIVE_HL},
	{BUTTON_LABEL_ACTIVE_FG, BUTTON_LABEL_ACTIVE_BG,
	 BUTTON_LABEL_ACTIVE_HL},
	{BUTTON_LABEL_INACTIVE_FG, BUTTON_LABEL_INACTIVE_BG,
	 BUTTON_LABEL_INACTIVE_HL},
	{INPUTBOX_FG, INPUTBOX_BG, INPUTBOX_HL},
	{INPUTBOX_BORDER_FG, INPUTBOX_BORDER_BG, INPUTBOX_BORDER_HL},
	{SEARCHBOX_FG, SEARCHBOX_BG, SEARCHBOX_HL},
	{SEARCHBOX_TITLE_FG, SEARCHBOX_TITLE_BG, SEARCHBOX_TITLE_HL},
	{SEARCHBOX_BORDER_FG, SEARCHBOX_BORDER_BG, SEARCHBOX_BORDER_HL},
	{POSITION_INDICATOR_FG, POSITION_INDICATOR_BG, POSITION_INDICATOR_HL},
	{MENUBOX_FG, MENUBOX_BG, MENUBOX_HL},
	{MENUBOX_BORDER_FG, MENUBOX_BORDER_BG, MENUBOX_BORDER_HL},
	{ITEM_FG, ITEM_BG, ITEM_HL},
	{ITEM_SELECTED_FG, ITEM_SELECTED_BG, ITEM_SELECTED_HL},
	{TAG_FG, TAG_BG, TAG_HL},
	{TAG_SELECTED_FG, TAG_SELECTED_BG, TAG_SELECTED_HL},
	{TAG_KEY_FG, TAG_KEY_BG, TAG_KEY_HL},
	{TAG_KEY_SELECTED_FG, TAG_KEY_SELECTED_BG, TAG_KEY_SELECTED_HL},
	{CHECK_FG, CHECK_BG, CHECK_HL},
	{CHECK_SELECTED_FG, CHECK_SELECTED_BG, CHECK_SELECTED_HL},
	{UARROW_FG, UARROW_BG, UARROW_HL},
	{DARROW_FG, DARROW_BG, DARROW_HL},
};				/* color_table */

/*
 * Set window to attribute 'attr'
 */
void attr_clear(WINDOW * win, int height, int width, chtype attr)
{
	int i, j;

	wattrset(win, attr);
	for (i = 0; i < height; i++) {
		wmove(win, i, 0);
		for (j = 0; j < width; j++)
			waddch(win, ' ');
	}
	touchwin(win);
}

void dialog_clear(void)
{
	attr_clear(stdscr, LINES, COLS, screen_attr);
	/* Display background title if it exists ... - SLH */
	if (backtitle != NULL) {
		int i;

		wattrset(stdscr, screen_attr);
		mvwaddstr(stdscr, 0, 1, (char *)backtitle);
		wmove(stdscr, 1, 1);
		for (i = 1; i < COLS - 1; i++)
			waddch(stdscr, ACS_HLINE);
	}
	wnoutrefresh(stdscr);
}

/*
 * Do some initialization for dialog
 */
void init_dialog(void)
{
	initscr();		/* Init curses */
	keypad(stdscr, TRUE);
	cbreak();
	noecho();

	if (use_colors)		/* Set up colors */
		color_setup();

	dialog_clear();
}

/*
 * Setup for color display
 */
void color_setup(void)
{
	int i;

	if (has_colors()) {	/* Terminal supports color? */
		start_color();

		/* Initialize color pairs */
		for (i = 0; i < ATTRIBUTE_COUNT; i++)
			init_pair(i + 1, color_table[i][0], color_table[i][1]);

		/* Setup color attributes */
		for (i = 0; i < ATTRIBUTE_COUNT; i++)
			attributes[i] = C_ATTR(color_table[i][2], i + 1);
	}
}

/*
 * End using dialog functions.
 */
void end_dialog(void)
{
	endwin();
}

/* Print the title of the dialog. Center the title and truncate
 * tile if wider than dialog (- 2 chars).
 **/
void print_title(WINDOW *dialog, const char *title, int width)
{
	if (title) {
		int tlen = MIN(width - 2, strlen(title));
		wattrset(dialog, title_attr);
		mvwaddch(dialog, 0, (width - tlen) / 2 - 1, ' ');
		mvwaddnstr(dialog, 0, (width - tlen)/2, title, tlen);
		waddch(dialog, ' ');
	}
}

/*
 * Print a string of text in a window, automatically wrap around to the
 * next line if the string is too long to fit on one line. Newline
 * characters '\n' are replaced by spaces.  We start on a new line
 * if there is no room for at least 4 nonblanks following a double-space.
 */
void print_autowrap(WINDOW * win, const char *prompt, int width, int y, int x)
{
	int newl, cur_x, cur_y;
	int i, prompt_len, room, wlen;
	char tempstr[MAX_LEN + 1], *word, *sp, *sp2;

	strcpy(tempstr, prompt);

	prompt_len = strlen(tempstr);

	/*
	 * Remove newlines
	 */
	for (i = 0; i < prompt_len; i++) {
		if (tempstr[i] == '\n')
			tempstr[i] = ' ';
	}

	if (prompt_len <= width - x * 2) {	/* If prompt is short */
		wmove(win, y, (width - prompt_len) / 2);
		waddstr(win, tempstr);
	} else {
		cur_x = x;
		cur_y = y;
		newl = 1;
		word = tempstr;
		while (word && *word) {
			sp = strchr(word, ' ');
			if (sp)
				*sp++ = 0;

			/* Wrap to next line if either the word does not fit,
			   or it is the first word of a new sentence, and it is
			   short, and the next word does not fit. */
			room = width - cur_x;
			wlen = strlen(word);
			if (wlen > room ||
			    (newl && wlen < 4 && sp
			     && wlen + 1 + strlen(sp) > room
			     && (!(sp2 = strchr(sp, ' '))
				 || wlen + 1 + (sp2 - sp) > room))) {
				cur_y++;
				cur_x = x;
			}
			wmove(win, cur_y, cur_x);
			waddstr(win, word);
			getyx(win, cur_y, cur_x);
			cur_x++;
			if (sp && *sp == ' ') {
				cur_x++;	/* double space */
				while (*++sp == ' ') ;
				newl = 1;
			} else
				newl = 0;
			word = sp;
		}
	}
}

/*
 * Print a button
 */
void print_button(WINDOW * win, const char *label, int y, int x, int selected)
{
	int i, temp;

	wmove(win, y, x);
	wattrset(win, selected ? button_active_attr : button_inactive_attr);
	waddstr(win, "<");
	temp = strspn(label, " ");
	label += temp;
	wattrset(win, selected ? button_label_active_attr
		 : button_label_inactive_attr);
	for (i = 0; i < temp; i++)
		waddch(win, ' ');
	wattrset(win, selected ? button_key_active_attr
		 : button_key_inactive_attr);
	waddch(win, label[0]);
	wattrset(win, selected ? button_label_active_attr
		 : button_label_inactive_attr);
	waddstr(win, (char *)label + 1);
	wattrset(win, selected ? button_active_attr : button_inactive_attr);
	waddstr(win, ">");
	wmove(win, y, x + temp + 1);
}

/*
 * Draw a rectangular box with line drawing characters
 */
void
draw_box(WINDOW * win, int y, int x, int height, int width,
	 chtype box, chtype border)
{
	int i, j;

	wattrset(win, 0);
	for (i = 0; i < height; i++) {
		wmove(win, y + i, x);
		for (j = 0; j < width; j++)
			if (!i && !j)
				waddch(win, border | ACS_ULCORNER);
			else if (i == height - 1 && !j)
				waddch(win, border | ACS_LLCORNER);
			else if (!i && j == width - 1)
				waddch(win, box | ACS_URCORNER);
			else if (i == height - 1 && j == width - 1)
				waddch(win, box | ACS_LRCORNER);
			else if (!i)
				waddch(win, border | ACS_HLINE);
			else if (i == height - 1)
				waddch(win, box | ACS_HLINE);
			else if (!j)
				waddch(win, border | ACS_VLINE);
			else if (j == width - 1)
				waddch(win, box | ACS_VLINE);
			else
				waddch(win, box | ' ');
	}
}

/*
 * Draw shadows along the right and bottom edge to give a more 3D look
 * to the boxes
 */
void draw_shadow(WINDOW * win, int y, int x, int height, int width)
{
	int i;

	if (has_colors()) {	/* Whether terminal supports color? */
		wattrset(win, shadow_attr);
		wmove(win, y + height, x + 2);
		for (i = 0; i < width; i++)
			waddch(win, winch(win) & A_CHARTEXT);
		for (i = y + 1; i < y + height + 1; i++) {
			wmove(win, i, x + width);
			waddch(win, winch(win) & A_CHARTEXT);
			waddch(win, winch(win) & A_CHARTEXT);
		}
		wnoutrefresh(win);
	}
}

/*
 *  Return the position of the first alphabetic character in a string.
 */
int first_alpha(const char *string, const char *exempt)
{
	int i, in_paren = 0, c;

	for (i = 0; i < strlen(string); i++) {
		c = tolower(string[i]);

		if (strchr("<[(", c))
			++in_paren;
		if (strchr(">])", c) && in_paren > 0)
			--in_paren;

		if ((!in_paren) && isalpha(c) && strchr(exempt, c) == 0)
			return i;
	}

	return 0;
}
