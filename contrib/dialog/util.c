/*
 *  $Id: util.c,v 1.272 2018/06/21 23:47:10 tom Exp $
 *
 *  util.c -- miscellaneous utilities for dialog
 *
 *  Copyright 2000-2016,2018	Thomas E. Dickey
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
 *	Savio Lam (lam836@cs.cuhk.hk)
 */

#include <dialog.h>
#include <dlg_keys.h>

#ifdef HAVE_SETLOCALE
#include <locale.h>
#endif

#ifdef NEED_WCHAR_H
#include <wchar.h>
#endif

#ifdef NCURSES_VERSION
#if defined(HAVE_NCURSESW_TERM_H)
#include <ncursesw/term.h>
#elif defined(HAVE_NCURSES_TERM_H)
#include <ncurses/term.h>
#else
#include <term.h>
#endif
#endif

#if defined(HAVE_WCHGAT)
#  if defined(NCURSES_VERSION_PATCH)
#    if NCURSES_VERSION_PATCH >= 20060715
#      define USE_WCHGAT 1
#    else
#      define USE_WCHGAT 0
#    endif
#  else
#    define USE_WCHGAT 1
#  endif
#else
#  define USE_WCHGAT 0
#endif

/* globals */
DIALOG_STATE dialog_state;
DIALOG_VARS dialog_vars;

#if !(defined(HAVE_WGETPARENT) && defined(HAVE_WINDOW__PARENT))
#define NEED_WGETPARENT 1
#else
#undef NEED_WGETPARENT
#endif

#define concat(a,b) a##b

#ifdef HAVE_RC_FILE
#define RC_DATA(name,comment) , #name "_color", comment " color"
#else
#define RC_DATA(name,comment)	/*nothing */
#endif

#ifdef HAVE_COLOR
#include <dlg_colors.h>
#define COLOR_DATA(upr) , \
	concat(DLGC_FG_,upr), \
	concat(DLGC_BG_,upr), \
	concat(DLGC_HL_,upr)
#else
#define COLOR_DATA(upr)		/*nothing */
#endif

#define DATA(atr,upr,lwr,cmt) { atr COLOR_DATA(upr) RC_DATA(lwr,cmt) }

#define UseShadow(dw) ((dw) != 0 && (dw)->normal != 0 && (dw)->shadow != 0)

/*
 * Table of color and attribute values, default is for mono display.
 * The order matches the DIALOG_ATR() values.
 */
/* *INDENT-OFF* */
DIALOG_COLORS dlg_color_table[] =
{
    DATA(A_NORMAL,	SCREEN,			screen, "Screen"),
    DATA(A_NORMAL,	SHADOW,			shadow, "Shadow"),
    DATA(A_REVERSE,	DIALOG,			dialog, "Dialog box"),
    DATA(A_REVERSE,	TITLE,			title, "Dialog box title"),
    DATA(A_REVERSE,	BORDER,			border, "Dialog box border"),
    DATA(A_BOLD,	BUTTON_ACTIVE,		button_active, "Active button"),
    DATA(A_DIM,		BUTTON_INACTIVE,	button_inactive, "Inactive button"),
    DATA(A_UNDERLINE,	BUTTON_KEY_ACTIVE,	button_key_active, "Active button key"),
    DATA(A_UNDERLINE,	BUTTON_KEY_INACTIVE,	button_key_inactive, "Inactive button key"),
    DATA(A_NORMAL,	BUTTON_LABEL_ACTIVE,	button_label_active, "Active button label"),
    DATA(A_NORMAL,	BUTTON_LABEL_INACTIVE,	button_label_inactive, "Inactive button label"),
    DATA(A_REVERSE,	INPUTBOX,		inputbox, "Input box"),
    DATA(A_REVERSE,	INPUTBOX_BORDER,	inputbox_border, "Input box border"),
    DATA(A_REVERSE,	SEARCHBOX,		searchbox, "Search box"),
    DATA(A_REVERSE,	SEARCHBOX_TITLE,	searchbox_title, "Search box title"),
    DATA(A_REVERSE,	SEARCHBOX_BORDER,	searchbox_border, "Search box border"),
    DATA(A_REVERSE,	POSITION_INDICATOR,	position_indicator, "File position indicator"),
    DATA(A_REVERSE,	MENUBOX,		menubox, "Menu box"),
    DATA(A_REVERSE,	MENUBOX_BORDER,		menubox_border, "Menu box border"),
    DATA(A_REVERSE,	ITEM,			item, "Item"),
    DATA(A_NORMAL,	ITEM_SELECTED,		item_selected, "Selected item"),
    DATA(A_REVERSE,	TAG,			tag, "Tag"),
    DATA(A_REVERSE,	TAG_SELECTED,		tag_selected, "Selected tag"),
    DATA(A_NORMAL,	TAG_KEY,		tag_key, "Tag key"),
    DATA(A_BOLD,	TAG_KEY_SELECTED,	tag_key_selected, "Selected tag key"),
    DATA(A_REVERSE,	CHECK,			check, "Check box"),
    DATA(A_REVERSE,	CHECK_SELECTED,		check_selected, "Selected check box"),
    DATA(A_REVERSE,	UARROW,			uarrow, "Up arrow"),
    DATA(A_REVERSE,	DARROW,			darrow, "Down arrow"),
    DATA(A_NORMAL,	ITEMHELP,		itemhelp, "Item help-text"),
    DATA(A_BOLD,	FORM_ACTIVE_TEXT,	form_active_text, "Active form text"),
    DATA(A_REVERSE,	FORM_TEXT,		form_text, "Form text"),
    DATA(A_NORMAL,	FORM_ITEM_READONLY,	form_item_readonly, "Readonly form item"),
    DATA(A_REVERSE,	GAUGE,			gauge, "Dialog box gauge"),
    DATA(A_REVERSE,	BORDER2,		border2, "Dialog box border2"),
    DATA(A_REVERSE,	INPUTBOX_BORDER2,	inputbox_border2, "Input box border2"),
    DATA(A_REVERSE,	SEARCHBOX_BORDER2,	searchbox_border2, "Search box border2"),
    DATA(A_REVERSE,	MENUBOX_BORDER2,	menubox_border2, "Menu box border2")
};
/* *INDENT-ON* */

/*
 * Maintain a list of subwindows so that we can delete them to cleanup.
 * More important, this provides a fallback when wgetparent() is not available.
 */
static void
add_subwindow(WINDOW *parent, WINDOW *child)
{
    DIALOG_WINDOWS *p = dlg_calloc(DIALOG_WINDOWS, 1);

    if (p != 0) {
	p->normal = parent;
	p->shadow = child;
	p->next = dialog_state.all_subwindows;
	dialog_state.all_subwindows = p;
    }
}

static void
del_subwindows(WINDOW *parent)
{
    DIALOG_WINDOWS *p = dialog_state.all_subwindows;
    DIALOG_WINDOWS *q = 0;
    DIALOG_WINDOWS *r;

    while (p != 0) {
	if (p->normal == parent) {
	    delwin(p->shadow);
	    r = p->next;
	    if (q == 0) {
		dialog_state.all_subwindows = r;
	    } else {
		q->next = r;
	    }
	    free(p);
	    p = r;
	} else {
	    q = p;
	    p = p->next;
	}
    }
}

/*
 * Display background title if it exists ...
 */
void
dlg_put_backtitle(void)
{
    int i;

    if (dialog_vars.backtitle != NULL) {
	chtype attr = A_NORMAL;
	int backwidth = dlg_count_columns(dialog_vars.backtitle);

	dlg_attrset(stdscr, screen_attr);
	(void) wmove(stdscr, 0, 1);
	dlg_print_text(stdscr, dialog_vars.backtitle, COLS - 2, &attr);
	for (i = 0; i < COLS - backwidth; i++)
	    (void) waddch(stdscr, ' ');
	(void) wmove(stdscr, 1, 1);
	for (i = 0; i < COLS - 2; i++)
	    (void) waddch(stdscr, dlg_boxchar(ACS_HLINE));
    }

    (void) wnoutrefresh(stdscr);
}

/*
 * Set window to attribute 'attr'.  There are more efficient ways to do this,
 * but will not work on older/buggy ncurses versions.
 */
void
dlg_attr_clear(WINDOW *win, int height, int width, chtype attr)
{
    int i, j;

    dlg_attrset(win, attr);
    for (i = 0; i < height; i++) {
	(void) wmove(win, i, 0);
	for (j = 0; j < width; j++)
	    (void) waddch(win, ' ');
    }
    (void) touchwin(win);
}

void
dlg_clear(void)
{
    dlg_attr_clear(stdscr, LINES, COLS, screen_attr);
}

#define isprivate(s) ((s) != 0 && strstr(s, "\033[?") != 0)

#define TTY_DEVICE "/dev/tty"

/*
 * If $DIALOG_TTY exists, allow the program to try to open the terminal
 * directly when stdout is redirected.  By default we require the "--stdout"
 * option to be given, but some scripts were written making use of the
 * behavior of dialog which tried opening the terminal anyway. 
 */
static char *
dialog_tty(void)
{
    char *result = getenv("DIALOG_TTY");
    if (result != 0 && atoi(result) == 0)
	result = 0;
    return result;
}

/*
 * Open the terminal directly.  If one of stdin, stdout or stderr really points
 * to a tty, use it.  Otherwise give up and open /dev/tty.
 */
static int
open_terminal(char **result, int mode)
{
    const char *device = TTY_DEVICE;
    if (!isatty(fileno(stderr))
	|| (device = ttyname(fileno(stderr))) == 0) {
	if (!isatty(fileno(stdout))
	    || (device = ttyname(fileno(stdout))) == 0) {
	    if (!isatty(fileno(stdin))
		|| (device = ttyname(fileno(stdin))) == 0) {
		device = TTY_DEVICE;
	    }
	}
    }
    *result = dlg_strclone(device);
    return open(device, mode);
}

#ifdef NCURSES_VERSION
static int
my_putc(int ch)
{
    char buffer[2];
    int fd = fileno(dialog_state.screen_output);

    buffer[0] = (char) ch;
    return (int) write(fd, buffer, (size_t) 1);
}
#endif

/*
 * Do some initialization for dialog.
 *
 * 'input' is the real tty input of dialog.  Usually it is stdin, but if
 * --input-fd option is used, it may be anything.
 *
 * 'output' is where dialog will send its result.  Usually it is stderr, but
 * if --stdout or --output-fd is used, it may be anything.  We are concerned
 * mainly with the case where it happens to be the same as stdout.
 */
void
init_dialog(FILE *input, FILE *output)
{
    int fd1, fd2;
    char *device = 0;

    setlocale(LC_ALL, "");

    dialog_state.output = output;
    dialog_state.tab_len = TAB_LEN;
    dialog_state.aspect_ratio = DEFAULT_ASPECT_RATIO;
#ifdef HAVE_COLOR
    dialog_state.use_colors = USE_COLORS;	/* use colors by default? */
    dialog_state.use_shadow = USE_SHADOW;	/* shadow dialog boxes by default? */
#endif

#ifdef HAVE_RC_FILE
    if (dlg_parse_rc() == -1)	/* Read the configuration file */
	dlg_exiterr("init_dialog: dlg_parse_rc");
#endif

    /*
     * Some widgets (such as gauge) may read from the standard input.  Pipes
     * only connect stdout/stdin, so there is not much choice.  But reading a
     * pipe would get in the way of curses' normal reading stdin for getch.
     *
     * As in the --stdout (see below), reopening the terminal does not always
     * work properly.  dialog provides a --pipe-fd option for this purpose.  We
     * test that case first (differing fileno's for input/stdin).  If the
     * fileno's are equal, but we're not reading from a tty, see if we can open
     * /dev/tty.
     */
    dialog_state.pipe_input = stdin;
    if (fileno(input) != fileno(stdin)) {
	if ((fd1 = dup(fileno(input))) >= 0
	    && (fd2 = dup(fileno(stdin))) >= 0) {
	    (void) dup2(fileno(input), fileno(stdin));
	    dialog_state.pipe_input = fdopen(fd2, "r");
	    if (fileno(stdin) != 0)	/* some functions may read fd #0 */
		(void) dup2(fileno(stdin), 0);
	} else {
	    dlg_exiterr("cannot open tty-input");
	}
	close(fd1);
    } else if (!isatty(fileno(stdin))) {
	if ((fd1 = open_terminal(&device, O_RDONLY)) >= 0) {
	    if ((fd2 = dup(fileno(stdin))) >= 0) {
		dialog_state.pipe_input = fdopen(fd2, "r");
		if (freopen(device, "r", stdin) == 0)
		    dlg_exiterr("cannot open tty-input");
		if (fileno(stdin) != 0)		/* some functions may read fd #0 */
		    (void) dup2(fileno(stdin), 0);
	    }
	    close(fd1);
	}
	free(device);
    }

    /*
     * If stdout is not a tty and dialog is called with the --stdout option, we
     * have to provide for a way to write to the screen.
     *
     * The curses library normally writes its output to stdout, leaving stderr
     * free for scripting.  Scripts are simpler when stdout is redirected.  The
     * newterm function is useful; it allows us to specify where the output
     * goes.  Reopening the terminal is not portable since several
     * configurations do not allow this to work properly:
     *
     * a) some getty implementations (and possibly broken tty drivers, e.g., on
     *    HPUX 10 and 11) cause stdin to act as if it is still in cooked mode
     *    even though results from ioctl's state that it is successfully
     *    altered to raw mode.  Broken is the proper term.
     *
     * b) the user may not have permissions on the device, e.g., if one su's
     *    from the login user to another non-privileged user.
     */
    if (!isatty(fileno(stdout))
	&& (fileno(stdout) == fileno(output) || dialog_tty())) {
	if ((fd1 = open_terminal(&device, O_WRONLY)) >= 0
	    && (dialog_state.screen_output = fdopen(fd1, "w")) != 0) {
	    if (newterm(NULL, dialog_state.screen_output, stdin) == 0) {
		dlg_exiterr("cannot initialize curses");
	    }
	    free(device);
	} else {
	    dlg_exiterr("cannot open tty-output");
	}
    } else {
	dialog_state.screen_output = stdout;
	(void) initscr();
    }
#ifdef NCURSES_VERSION
    /*
     * Cancel xterm's alternate-screen mode.
     */
    if (!dialog_vars.keep_tite
	&& (fileno(dialog_state.screen_output) != fileno(stdout)
	    || isatty(fileno(dialog_state.screen_output)))
	&& key_mouse != 0	/* xterm and kindred */
	&& isprivate(enter_ca_mode)
	&& isprivate(exit_ca_mode)) {
	/*
	 * initscr() or newterm() already wrote enter_ca_mode as a side
	 * effect of initializing the screen.  It would be nice to not even
	 * do that, but we do not really have access to the correct copy of
	 * the terminfo description until those functions have been invoked.
	 */
	(void) refresh();
	(void) tputs(exit_ca_mode, 0, my_putc);
	(void) tputs(clear_screen, 0, my_putc);
	/*
	 * Prevent ncurses from switching "back" to the normal screen when
	 * exiting from dialog.  That would move the cursor to the original
	 * location saved in xterm.  Normally curses sets the cursor position
	 * to the first line after the display, but the alternate screen
	 * switching is done after that point.
	 *
	 * Cancelling the strings altogether also works around the buggy
	 * implementation of alternate-screen in rxvt, etc., which clear
	 * more of the display than they should.
	 */
	enter_ca_mode = 0;
	exit_ca_mode = 0;
    }
#endif
#ifdef HAVE_FLUSHINP
    (void) flushinp();
#endif
    (void) keypad(stdscr, TRUE);
    (void) cbreak();
    (void) noecho();

    if (!dialog_state.no_mouse) {
	mouse_open();
    }

    dialog_state.screen_initialized = TRUE;

#ifdef HAVE_COLOR
    if (dialog_state.use_colors || dialog_state.use_shadow)
	dlg_color_setup();	/* Set up colors */
#endif

    /* Set screen to screen attribute */
    dlg_clear();
}

#ifdef HAVE_COLOR
static int defined_colors = 1;	/* pair-0 is reserved */
/*
 * Setup for color display
 */
void
dlg_color_setup(void)
{
    unsigned i;

    if (has_colors()) {		/* Terminal supports color? */
	(void) start_color();

#if defined(HAVE_USE_DEFAULT_COLORS)
	use_default_colors();
#endif

#if defined(__NetBSD__) && defined(_CURSES_)
#define C_ATTR(x,y) (((x) != 0 ? A_BOLD :  0) | COLOR_PAIR((y)))
	/* work around bug in NetBSD curses */
	for (i = 0; i < sizeof(dlg_color_table) /
	     sizeof(dlg_color_table[0]); i++) {

	    /* Initialize color pairs */
	    (void) init_pair(i + 1,
			     dlg_color_table[i].fg,
			     dlg_color_table[i].bg);

	    /* Setup color attributes */
	    dlg_color_table[i].atr = C_ATTR(dlg_color_table[i].hilite, i + 1);
	}
	defined_colors = i + 1;
#else
	for (i = 0; i < sizeof(dlg_color_table) /
	     sizeof(dlg_color_table[0]); i++) {

	    /* Initialize color pairs */
	    chtype color = dlg_color_pair(dlg_color_table[i].fg,
					  dlg_color_table[i].bg);

	    /* Setup color attributes */
	    dlg_color_table[i].atr = ((dlg_color_table[i].hilite
				       ? A_BOLD
				       : 0)
				      | color);
	}
#endif
    } else {
	dialog_state.use_colors = FALSE;
	dialog_state.use_shadow = FALSE;
    }
}

int
dlg_color_count(void)
{
    return sizeof(dlg_color_table) / sizeof(dlg_color_table[0]);
}

/*
 * Wrapper for getattrs(), or the more cumbersome X/Open wattr_get().
 */
chtype
dlg_get_attrs(WINDOW *win)
{
    chtype result;
#ifdef HAVE_GETATTRS
    result = (chtype) getattrs(win);
#else
    attr_t my_result;
    short my_pair;
    wattr_get(win, &my_result, &my_pair, NULL);
    result = my_result;
#endif
    return result;
}

/*
 * Reuse color pairs (they are limited), returning a COLOR_PAIR() value if we
 * have (or can) define a pair with the given color as foreground on the
 * window's defined background.
 */
chtype
dlg_color_pair(int foreground, int background)
{
    chtype result = 0;
    int pair;
    short fg, bg;
    bool found = FALSE;

    for (pair = 1; pair < defined_colors; ++pair) {
	if (pair_content((short) pair, &fg, &bg) != ERR
	    && fg == foreground
	    && bg == background) {
	    result = (chtype) COLOR_PAIR(pair);
	    found = TRUE;
	    break;
	}
    }
    if (!found && (defined_colors + 1) < COLOR_PAIRS) {
	pair = defined_colors++;
	(void) init_pair((short) pair, (short) foreground, (short) background);
	result = (chtype) COLOR_PAIR(pair);
    }
    return result;
}

/*
 * Reuse color pairs (they are limited), returning a COLOR_PAIR() value if we
 * have (or can) define a pair with the given color as foreground on the
 * window's defined background.
 */
static chtype
define_color(WINDOW *win, int foreground)
{
    int pair;
    short fg, bg, background;
    if (dialog_state.text_only) {
	background = COLOR_BLACK;
    } else {
	chtype attrs = dlg_get_attrs(win);

	if ((pair = PAIR_NUMBER(attrs)) != 0
	    && pair_content((short) pair, &fg, &bg) != ERR) {
	    background = bg;
	} else {
	    background = COLOR_BLACK;
	}
    }
    return dlg_color_pair(foreground, background);
}
#endif

/*
 * End using dialog functions.
 */
void
end_dialog(void)
{
    if (dialog_state.screen_initialized) {
	dialog_state.screen_initialized = FALSE;
	mouse_close();
	(void) endwin();
	(void) fflush(stdout);
    }
}

#define ESCAPE_LEN 3
#define isOurEscape(p) (((p)[0] == '\\') && ((p)[1] == 'Z') && ((p)[2] != 0))

int
dlg_count_real_columns(const char *text)
{
    int result = 0;
    if (*text) {
	result = dlg_count_columns(text);
	if (result && dialog_vars.colors) {
	    int hidden = 0;
	    while (*text) {
		if (dialog_vars.colors && isOurEscape(text)) {
		    hidden += ESCAPE_LEN;
		    text += ESCAPE_LEN;
		} else {
		    ++text;
		}
	    }
	    result -= hidden;
	}
    }
    return result;
}

static int
centered(int width, const char *string)
{
    int need = dlg_count_real_columns(string);
    int left;

    left = (width - need) / 2 - 1;
    if (left < 0)
	left = 0;
    return left;
}

#ifdef USE_WIDE_CURSES
static bool
is_combining(const char *txt, int *combined)
{
    bool result = FALSE;

    if (*combined == 0) {
	if (UCH(*txt) >= 128) {
	    wchar_t wch;
	    mbstate_t state;
	    size_t given = strlen(txt);
	    size_t len;

	    memset(&state, 0, sizeof(state));
	    len = mbrtowc(&wch, txt, given, &state);
	    if ((int) len > 0 && wcwidth(wch) == 0) {
		*combined = (int) len - 1;
		result = TRUE;
	    }
	}
    } else {
	result = TRUE;
	*combined -= 1;
    }
    return result;
}
#endif

/*
 * Print the name (tag) or text from a DIALOG_LISTITEM, highlighting the
 * first character if selected.
 */
void
dlg_print_listitem(WINDOW *win,
		   const char *text,
		   int climit,
		   bool first,
		   int selected)
{
    chtype attr = A_NORMAL;
    int limit;
    const int *cols;
    chtype attrs[4];

    if (text == 0)
	text = "";

    if (first) {
	const int *indx = dlg_index_wchars(text);
	attrs[3] = tag_key_selected_attr;
	attrs[2] = tag_key_attr;
	attrs[1] = tag_selected_attr;
	attrs[0] = tag_attr;

	dlg_attrset(win, selected ? attrs[3] : attrs[2]);
	(void) waddnstr(win, text, indx[1]);

	if ((int) strlen(text) > indx[1]) {
	    limit = dlg_limit_columns(text, climit, 1);
	    if (limit > 1) {
		dlg_attrset(win, selected ? attrs[1] : attrs[0]);
		(void) waddnstr(win,
				text + indx[1],
				indx[limit] - indx[1]);
	    }
	}
    } else {
	attrs[1] = item_selected_attr;
	attrs[0] = item_attr;

	cols = dlg_index_columns(text);
	limit = dlg_limit_columns(text, climit, 0);

	if (limit > 0) {
	    dlg_attrset(win, selected ? attrs[1] : attrs[0]);
	    dlg_print_text(win, text, cols[limit], &attr);
	}
    }
}

/*
 * Print up to 'cols' columns from 'text', optionally rendering our escape
 * sequence for attributes and color.
 */
void
dlg_print_text(WINDOW *win, const char *txt, int cols, chtype *attr)
{
    int y_origin, x_origin;
    int y_before, x_before = 0;
    int y_after, x_after;
    int tabbed = 0;
    bool thisTab;
    bool ended = FALSE;
    chtype useattr;
#ifdef USE_WIDE_CURSES
    int combined = 0;
#endif

    if (dialog_state.text_only) {
	y_origin = y_after = 0;
	x_origin = x_after = 0;
    } else {
	getyx(win, y_origin, x_origin);
    }
    while (cols > 0 && (*txt != '\0')) {
	if (dialog_vars.colors) {
	    while (isOurEscape(txt)) {
		int code;

		txt += 2;
		switch (code = CharOf(*txt)) {
#ifdef HAVE_COLOR
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		    *attr &= ~A_COLOR;
		    *attr |= define_color(win, code - '0');
		    break;
#endif
		case 'B':
		    *attr &= ~A_BOLD;
		    break;
		case 'b':
		    *attr |= A_BOLD;
		    break;
		case 'R':
		    *attr &= ~A_REVERSE;
		    break;
		case 'r':
		    *attr |= A_REVERSE;
		    break;
		case 'U':
		    *attr &= ~A_UNDERLINE;
		    break;
		case 'u':
		    *attr |= A_UNDERLINE;
		    break;
		case 'n':
		    *attr = A_NORMAL;
		    break;
		}
		++txt;
	    }
	}
	if (ended || *txt == '\n' || *txt == '\0')
	    break;
	useattr = (*attr) & A_ATTRIBUTES;
#ifdef HAVE_COLOR
	/*
	 * Prevent this from making text invisible when the foreground and
	 * background colors happen to be the same, and there's no bold
	 * attribute.
	 */
	if ((useattr & A_COLOR) != 0 && (useattr & A_BOLD) == 0) {
	    short pair = (short) PAIR_NUMBER(useattr);
	    short fg, bg;
	    if (pair_content(pair, &fg, &bg) != ERR
		&& fg == bg) {
		useattr &= ~A_COLOR;
		useattr |= dlg_color_pair(fg, ((bg == COLOR_BLACK)
					       ? COLOR_WHITE
					       : COLOR_BLACK));
	    }
	}
#endif
	/*
	 * Write the character, using curses to tell exactly how wide it
	 * is.  If it is a tab, discount that, since the caller thinks
	 * tabs are nonprinting, and curses will expand tabs to one or
	 * more blanks.
	 */
	thisTab = (CharOf(*txt) == TAB);
	if (dialog_state.text_only) {
	    y_before = y_after;
	    x_before = x_after;
	} else {
	    if (thisTab) {
		getyx(win, y_before, x_before);
		(void) y_before;
	    }
	}
	if (dialog_state.text_only) {
	    int ch = CharOf(*txt++);
	    if (thisTab) {
		while ((x_after++) % 8) {
		    fputc(' ', dialog_state.output);
		}
	    } else {
		fputc(ch, dialog_state.output);
		x_after++;	/* FIXME: handle meta per locale */
	    }
	} else {
	    (void) waddch(win, CharOf(*txt++) | useattr);
	    getyx(win, y_after, x_after);
	}
	if (thisTab && (y_after == y_origin))
	    tabbed += (x_after - x_before);
	if ((y_after != y_origin) ||
	    (x_after >= (cols + tabbed + x_origin)
#ifdef USE_WIDE_CURSES
	     && !is_combining(txt, &combined)
#endif
	    )) {
	    ended = TRUE;
	}
    }
    if (dialog_state.text_only) {
	fputc('\n', dialog_state.output);
    }
}

/*
 * Print one line of the prompt in the window within the limits of the
 * specified right margin.  The line will end on a word boundary and a pointer
 * to the start of the next line is returned, or a NULL pointer if the end of
 * *prompt is reached.
 */
const char *
dlg_print_line(WINDOW *win,
	       chtype *attr,
	       const char *prompt,
	       int lm, int rm, int *x)
{
    const char *wrap_ptr;
    const char *test_ptr;
    const char *hide_ptr = 0;
    const int *cols = dlg_index_columns(prompt);
    const int *indx = dlg_index_wchars(prompt);
    int wrap_inx = 0;
    int test_inx = 0;
    int cur_x = lm;
    int hidden = 0;
    int limit = dlg_count_wchars(prompt);
    int n;
    int tabbed = 0;

    *x = 1;

    /*
     * Set *test_ptr to the end of the line or the right margin (rm), whichever
     * is less, and set wrap_ptr to the end of the last word in the line.
     */
    for (n = 0; n < limit; ++n) {
	int ch = *(test_ptr = prompt + indx[test_inx]);
	if (ch == '\n' || ch == '\0' || cur_x >= (rm + hidden))
	    break;
	if (ch == TAB && n == 0) {
	    tabbed = 8;		/* workaround for leading tabs */
	} else if (isblank(UCH(ch))
		   && n != 0
		   && !isblank(UCH(prompt[indx[n - 1]]))) {
	    wrap_inx = n;
	    *x = cur_x;
	} else if (dialog_vars.colors && isOurEscape(test_ptr)) {
	    hide_ptr = test_ptr;
	    hidden += ESCAPE_LEN;
	    n += (ESCAPE_LEN - 1);
	}
	cur_x = lm + tabbed + cols[n + 1];
	if (cur_x > (rm + hidden))
	    break;
	test_inx = n + 1;
    }

    /*
     * If the line doesn't reach the right margin in the middle of a word, then
     * we don't have to wrap it at the end of the previous word.
     */
    test_ptr = prompt + indx[test_inx];
    if (*test_ptr == '\n' || isblank(UCH(*test_ptr)) || *test_ptr == '\0') {
	wrap_inx = test_inx;
	while (wrap_inx > 0 && isblank(UCH(prompt[indx[wrap_inx - 1]]))) {
	    wrap_inx--;
	}
	*x = lm + indx[wrap_inx];
    } else if (*x == 1 && cur_x >= rm) {
	/*
	 * If the line has no spaces, then wrap it anyway at the right margin
	 */
	*x = rm;
	wrap_inx = test_inx;
    }
    wrap_ptr = prompt + indx[wrap_inx];
#ifdef USE_WIDE_CURSES
    if (UCH(*wrap_ptr) >= 128) {
	int combined = 0;
	while (is_combining(wrap_ptr, &combined)) {
	    ++wrap_ptr;
	}
    }
#endif

    /*
     * If we found hidden text past the last point that we will display,
     * discount that from the displayed length.
     */
    if ((hide_ptr != 0) && (hide_ptr >= wrap_ptr)) {
	hidden -= ESCAPE_LEN;
	test_ptr = wrap_ptr;
	while (test_ptr < wrap_ptr) {
	    if (dialog_vars.colors && isOurEscape(test_ptr)) {
		hidden -= ESCAPE_LEN;
		test_ptr += ESCAPE_LEN;
	    } else {
		++test_ptr;
	    }
	}
    }

    /*
     * Print the line if we have a window pointer.  Otherwise this routine
     * is just being called for sizing the window.
     */
    if (dialog_state.text_only || win) {
	dlg_print_text(win, prompt, (cols[wrap_inx] - hidden), attr);
    }

    /* *x tells the calling function how long the line was */
    if (*x == 1) {
	*x = rm;
    }

    *x -= hidden;

    /* Find the start of the next line and return a pointer to it */
    test_ptr = wrap_ptr;
    while (isblank(UCH(*test_ptr)))
	test_ptr++;
    if (*test_ptr == '\n')
	test_ptr++;
    dlg_finish_string(prompt);
    return (test_ptr);
}

static void
justify_text(WINDOW *win,
	     const char *prompt,
	     int limit_y,
	     int limit_x,
	     int *high, int *wide)
{
    chtype attr = A_NORMAL;
    int x = (2 * MARGIN);
    int y = MARGIN;
    int max_x = 2;
    int lm = (2 * MARGIN);	/* left margin (box-border plus a space) */
    int rm = limit_x;		/* right margin */
    int bm = limit_y;		/* bottom margin */
    int last_y = 0, last_x = 0;

    dialog_state.text_height = 0;
    dialog_state.text_width = 0;
    if (dialog_state.text_only || win) {
	rm -= (2 * MARGIN);
	bm -= (2 * MARGIN);
    }
    if (prompt == 0)
	prompt = "";

    if (win != 0)
	getyx(win, last_y, last_x);
    while (y <= bm && *prompt) {
	x = lm;

	if (*prompt == '\n') {
	    while (*prompt == '\n' && y < bm) {
		if (*(prompt + 1) != '\0') {
		    ++y;
		    if (win != 0)
			(void) wmove(win, y, lm);
		}
		prompt++;
	    }
	} else if (win != 0)
	    (void) wmove(win, y, lm);

	if (*prompt) {
	    prompt = dlg_print_line(win, &attr, prompt, lm, rm, &x);
	    if (win != 0)
		getyx(win, last_y, last_x);
	}
	if (*prompt) {
	    ++y;
	    if (win != 0)
		(void) wmove(win, y, lm);
	}
	max_x = MAX(max_x, x);
    }
    /* Move back to the last position after drawing prompt, for msgbox. */
    if (win != 0)
	(void) wmove(win, last_y, last_x);

    /* Set the final height and width for the calling function */
    if (high != 0)
	*high = y;
    if (wide != 0)
	*wide = max_x;
}

/*
 * Print a string of text in a window, automatically wrap around to the next
 * line if the string is too long to fit on one line.  Note that the string may
 * contain embedded newlines.
 */
void
dlg_print_autowrap(WINDOW *win, const char *prompt, int height, int width)
{
    justify_text(win, prompt,
		 height,
		 width,
		 (int *) 0, (int *) 0);
}

/*
 * Display the message in a scrollable window.  Actually the way it works is
 * that we create a "tall" window of the proper width, let the text wrap within
 * that, and copy a slice of the result to the dialog.
 *
 * It works for ncurses.  Other curses implementations show only blanks (Tru64)
 * or garbage (NetBSD).
 */
int
dlg_print_scrolled(WINDOW *win,
		   const char *prompt,
		   int offset,
		   int height,
		   int width,
		   int pauseopt)
{
    int oldy, oldx;
    int last = 0;

    (void) pauseopt;		/* used only for ncurses */

    getyx(win, oldy, oldx);
#ifdef NCURSES_VERSION
    if (pauseopt) {
	int wide = width - (2 * MARGIN);
	int high = LINES;
	int y, x;
	int len;
	int percent;
	WINDOW *dummy;
	char buffer[5];

#if defined(NCURSES_VERSION_PATCH) && NCURSES_VERSION_PATCH >= 20040417
	/*
	 * If we're not limited by the screensize, allow text to possibly be
	 * one character per line.
	 */
	if ((len = dlg_count_columns(prompt)) > high)
	    high = len;
#endif
	dummy = newwin(high, width, 0, 0);
	if (dummy == 0) {
	    dlg_attrset(win, dialog_attr);
	    dlg_print_autowrap(win, prompt, height + 1 + (3 * MARGIN), width);
	    last = 0;
	} else {
	    wbkgdset(dummy, dialog_attr | ' ');
	    dlg_attrset(dummy, dialog_attr);
	    werase(dummy);
	    dlg_print_autowrap(dummy, prompt, high, width);
	    getyx(dummy, y, x);
	    (void) x;

	    copywin(dummy,	/* srcwin */
		    win,	/* dstwin */
		    offset + MARGIN,	/* sminrow */
		    MARGIN,	/* smincol */
		    MARGIN,	/* dminrow */
		    MARGIN,	/* dmincol */
		    height,	/* dmaxrow */
		    wide,	/* dmaxcol */
		    FALSE);

	    delwin(dummy);

	    /* if the text is incomplete, or we have scrolled, show the percentage */
	    if (y > 0 && wide > 4) {
		percent = (int) ((height + offset) * 100.0 / y);
		if (percent < 0)
		    percent = 0;
		if (percent > 100)
		    percent = 100;
		if (offset != 0 || percent != 100) {
		    dlg_attrset(win, position_indicator_attr);
		    (void) wmove(win, MARGIN + height, wide - 4);
		    (void) sprintf(buffer, "%d%%", percent);
		    (void) waddstr(win, buffer);
		    if ((len = (int) strlen(buffer)) < 4) {
			dlg_attrset(win, border_attr);
			whline(win, dlg_boxchar(ACS_HLINE), 4 - len);
		    }
		}
	    }
	    last = (y - height);
	}
    } else
#endif
    {
	(void) offset;
	dlg_attrset(win, dialog_attr);
	dlg_print_autowrap(win, prompt, height + 1 + (3 * MARGIN), width);
	last = 0;
    }
    wmove(win, oldy, oldx);
    return last;
}

int
dlg_check_scrolled(int key, int last, int page, bool * show, int *offset)
{
    int code = 0;

    *show = FALSE;

    switch (key) {
    case DLGK_PAGE_FIRST:
	if (*offset > 0) {
	    *offset = 0;
	    *show = TRUE;
	}
	break;
    case DLGK_PAGE_LAST:
	if (*offset < last) {
	    *offset = last;
	    *show = TRUE;
	}
	break;
    case DLGK_GRID_UP:
	if (*offset > 0) {
	    --(*offset);
	    *show = TRUE;
	}
	break;
    case DLGK_GRID_DOWN:
	if (*offset < last) {
	    ++(*offset);
	    *show = TRUE;
	}
	break;
    case DLGK_PAGE_PREV:
	if (*offset > 0) {
	    *offset -= page;
	    if (*offset < 0)
		*offset = 0;
	    *show = TRUE;
	}
	break;
    case DLGK_PAGE_NEXT:
	if (*offset < last) {
	    *offset += page;
	    if (*offset > last)
		*offset = last;
	    *show = TRUE;
	}
	break;
    default:
	code = -1;
	break;
    }
    return code;
}

/*
 * Calculate the window size for preformatted text.  This will calculate box
 * dimensions that are at or close to the specified aspect ratio for the prompt
 * string with all spaces and newlines preserved and additional newlines added
 * as necessary.
 */
static void
auto_size_preformatted(const char *prompt, int *height, int *width)
{
    int high = 0, wide = 0;
    float car;			/* Calculated Aspect Ratio */
    float diff;
    int max_y = SLINES - 1;
    int max_x = SCOLS - 2;
    int max_width = max_x;
    int ar = dialog_state.aspect_ratio;

    /* Get the initial dimensions */
    justify_text((WINDOW *) 0, prompt, max_y, max_x, &high, &wide);
    car = (float) (wide / high);

    /*
     * If the aspect ratio is greater than it should be, then decrease the
     * width proportionately.
     */
    if (car > ar) {
	diff = car / (float) ar;
	max_x = (int) ((float) wide / diff + 4);
	justify_text((WINDOW *) 0, prompt, max_y, max_x, &high, &wide);
	car = (float) wide / (float) high;
    }

    /*
     * If the aspect ratio is too small after decreasing the width, then
     * incrementally increase the width until the aspect ratio is equal to or
     * greater than the specified aspect ratio.
     */
    while (car < ar && max_x < max_width) {
	max_x += 4;
	justify_text((WINDOW *) 0, prompt, max_y, max_x, &high, &wide);
	car = (float) (wide / high);
    }

    *height = high;
    *width = wide;
}

/*
 * Find the length of the longest "word" in the given string.  By setting the
 * widget width at least this long, we can avoid splitting a word on the
 * margin.
 */
static int
longest_word(const char *string)
{
    int length, result = 0;

    while (*string != '\0') {
	length = 0;
	while (*string != '\0' && !isspace(UCH(*string))) {
	    length++;
	    string++;
	}
	result = MAX(result, length);
	if (*string != '\0')
	    string++;
    }
    return result;
}

/*
 * if (height or width == -1) Maximize()
 * if (height or width == 0), justify and return actual limits.
 */
static void
real_auto_size(const char *title,
	       const char *prompt,
	       int *height, int *width,
	       int boxlines, int mincols)
{
    int x = (dialog_vars.begin_set ? dialog_vars.begin_x : 2);
    int y = (dialog_vars.begin_set ? dialog_vars.begin_y : 1);
    int title_length = title ? dlg_count_columns(title) : 0;
    int high;
    int wide;
    int save_high = *height;
    int save_wide = *width;
    int max_high;
    int max_wide;

    if (prompt == 0) {
	if (*height == 0)
	    *height = -1;
	if (*width == 0)
	    *width = -1;
    }

    max_high = (*height < 0);
    max_wide = (*width < 0);

    if (*height > 0) {
	high = *height;
    } else {
	high = SLINES - y;
    }

    if (*width <= 0) {
	if (prompt != 0) {
	    wide = MAX(title_length, mincols);
	    if (strchr(prompt, '\n') == 0) {
		double val = (dialog_state.aspect_ratio *
			      dlg_count_real_columns(prompt));
		double xxx = sqrt(val);
		int tmp = (int) xxx;
		wide = MAX(wide, tmp);
		wide = MAX(wide, longest_word(prompt));
		justify_text((WINDOW *) 0, prompt, high, wide, height, width);
	    } else {
		auto_size_preformatted(prompt, height, width);
	    }
	} else {
	    wide = SCOLS - x;
	    justify_text((WINDOW *) 0, prompt, high, wide, height, width);
	}
    }

    if (*width < title_length) {
	justify_text((WINDOW *) 0, prompt, high, title_length, height, width);
	*width = title_length;
    }

    dialog_state.text_height = *height;
    dialog_state.text_width = *width;

    if (*width < mincols && save_wide == 0)
	*width = mincols;
    if (prompt != 0) {
	*width += ((2 * MARGIN) + SHADOW_COLS);
	*height += boxlines + (2 * MARGIN);
    }

    if (save_high > 0)
	*height = save_high;
    if (save_wide > 0)
	*width = save_wide;

    if (max_high)
	*height = SLINES - (dialog_vars.begin_set ? dialog_vars.begin_y : 0);
    if (max_wide)
	*width = SCOLS - (dialog_vars.begin_set ? dialog_vars.begin_x : 0);
}

/* End of real_auto_size() */

void
dlg_auto_size(const char *title,
	      const char *prompt,
	      int *height,
	      int *width,
	      int boxlines,
	      int mincols)
{
    DLG_TRACE(("# dlg_auto_size(%d,%d) limits %d,%d\n",
	       *height, *width,
	       boxlines, mincols));

    real_auto_size(title, prompt, height, width, boxlines, mincols);

    if (*width > SCOLS) {
	(*height)++;
	*width = SCOLS;
    }

    if (*height > SLINES) {
	*height = SLINES;
    }
    DLG_TRACE(("# ...dlg_auto_size(%d,%d) also %d,%d\n",
	       *height, *width,
	       dialog_state.text_height, dialog_state.text_width));
}

/*
 * if (height or width == -1) Maximize()
 * if (height or width == 0)
 *    height=MIN(SLINES, num.lines in fd+n);
 *    width=MIN(SCOLS, MAX(longer line+n, mincols));
 */
void
dlg_auto_sizefile(const char *title,
		  const char *file,
		  int *height,
		  int *width,
		  int boxlines,
		  int mincols)
{
    int count = 0;
    int len = title ? dlg_count_columns(title) : 0;
    int nc = 4;
    int numlines = 2;
    long offset;
    int ch;
    FILE *fd;

    /* Open input file for reading */
    if ((fd = fopen(file, "rb")) == NULL)
	dlg_exiterr("dlg_auto_sizefile: Cannot open input file %s", file);

    if ((*height == -1) || (*width == -1)) {
	*height = SLINES - (dialog_vars.begin_set ? dialog_vars.begin_y : 0);
	*width = SCOLS - (dialog_vars.begin_set ? dialog_vars.begin_x : 0);
    }
    if ((*height != 0) && (*width != 0)) {
	(void) fclose(fd);
	if (*width > SCOLS)
	    *width = SCOLS;
	if (*height > SLINES)
	    *height = SLINES;
	return;
    }

    while (!feof(fd)) {
	if (ferror(fd))
	    break;
	offset = 0;
	while (((ch = getc(fd)) != '\n') && !feof(fd)) {
	    if ((ch == TAB) && (dialog_vars.tab_correct)) {
		offset += dialog_state.tab_len - (offset % dialog_state.tab_len);
	    } else {
		offset++;
	    }
	}

	if (offset > len)
	    len = (int) offset;

	count++;
    }

    /* now 'count' has the number of lines of fd and 'len' the max length */

    *height = MIN(SLINES, count + numlines + boxlines);
    *width = MIN(SCOLS, MAX((len + nc), mincols));
    /* here width and height can be maximized if > SCOLS|SLINES because
       textbox-like widgets don't put all <file> on the screen.
       Msgbox-like widget instead have to put all <text> correctly. */

    (void) fclose(fd);
}

/*
 * Draw a rectangular box with line drawing characters.
 *
 * borderchar is used to color the upper/left edges.
 *
 * boxchar is used to color the right/lower edges.  It also is fill-color used
 * for the box contents.
 *
 * Normally, if you are drawing a scrollable box, use menubox_border_attr for
 * boxchar, and menubox_attr for borderchar since the scroll-arrows are drawn
 * with menubox_attr at the top, and menubox_border_attr at the bottom.  That
 * also (given the default color choices) produces a recessed effect.
 *
 * If you want a raised effect (and are not going to use the scroll-arrows),
 * reverse this choice.
 */
void
dlg_draw_box2(WINDOW *win, int y, int x, int height, int width,
	      chtype boxchar, chtype borderchar, chtype borderchar2)
{
    int i, j;
    chtype save = dlg_get_attrs(win);

    dlg_attrset(win, 0);
    for (i = 0; i < height; i++) {
	(void) wmove(win, y + i, x);
	for (j = 0; j < width; j++)
	    if (!i && !j)
		(void) waddch(win, borderchar | dlg_boxchar(ACS_ULCORNER));
	    else if (i == height - 1 && !j)
		(void) waddch(win, borderchar | dlg_boxchar(ACS_LLCORNER));
	    else if (!i && j == width - 1)
		(void) waddch(win, borderchar2 | dlg_boxchar(ACS_URCORNER));
	    else if (i == height - 1 && j == width - 1)
		(void) waddch(win, borderchar2 | dlg_boxchar(ACS_LRCORNER));
	    else if (!i)
		(void) waddch(win, borderchar | dlg_boxchar(ACS_HLINE));
	    else if (i == height - 1)
		(void) waddch(win, borderchar2 | dlg_boxchar(ACS_HLINE));
	    else if (!j)
		(void) waddch(win, borderchar | dlg_boxchar(ACS_VLINE));
	    else if (j == width - 1)
		(void) waddch(win, borderchar2 | dlg_boxchar(ACS_VLINE));
	    else
		(void) waddch(win, boxchar | ' ');
    }
    dlg_attrset(win, save);
}

void
dlg_draw_box(WINDOW *win, int y, int x, int height, int width,
	     chtype boxchar, chtype borderchar)
{
    dlg_draw_box2(win, y, x, height, width, boxchar, borderchar, boxchar);
}

static DIALOG_WINDOWS *
find_window(WINDOW *win)
{
    DIALOG_WINDOWS *result = 0;
    DIALOG_WINDOWS *p;

    for (p = dialog_state.all_windows; p != 0; p = p->next) {
	if (p->normal == win) {
	    result = p;
	    break;
	}
    }
    return result;
}

#ifdef HAVE_COLOR
/*
 * If we have wchgat(), use that for updating shadow attributes, to work with
 * wide-character data.
 */

/*
 * Check if the given point is "in" the given window.  If so, return the window
 * pointer, otherwise null.
 */
static WINDOW *
in_window(WINDOW *win, int y, int x)
{
    WINDOW *result = 0;
    int y_base = getbegy(win);
    int x_base = getbegx(win);
    int y_last = getmaxy(win) + y_base;
    int x_last = getmaxx(win) + x_base;

    if (y >= y_base && y <= y_last && x >= x_base && x <= x_last)
	result = win;
    return result;
}

static WINDOW *
window_at_cell(DIALOG_WINDOWS * dw, int y, int x)
{
    WINDOW *result = 0;
    DIALOG_WINDOWS *p;
    int y_want = y + getbegy(dw->shadow);
    int x_want = x + getbegx(dw->shadow);

    for (p = dialog_state.all_windows; p != 0; p = p->next) {
	if (dw->normal != p->normal
	    && dw->shadow != p->normal
	    && (result = in_window(p->normal, y_want, x_want)) != 0) {
	    break;
	}
    }
    if (result == 0) {
	result = stdscr;
    }
    return result;
}

static bool
in_shadow(WINDOW *normal, WINDOW *shadow, int y, int x)
{
    bool result = FALSE;
    int ybase = getbegy(normal);
    int ylast = getmaxy(normal) + ybase;
    int xbase = getbegx(normal);
    int xlast = getmaxx(normal) + xbase;

    y += getbegy(shadow);
    x += getbegx(shadow);

    if (y >= ybase + SHADOW_ROWS
	&& y < ylast + SHADOW_ROWS
	&& x >= xlast
	&& x < xlast + SHADOW_COLS) {
	/* in the right-side */
	result = TRUE;
    } else if (y >= ylast
	       && y < ylast + SHADOW_ROWS
	       && x >= ybase + SHADOW_COLS
	       && x < ylast + SHADOW_COLS) {
	/* check the bottom */
	result = TRUE;
    }

    return result;
}

/*
 * When erasing a shadow, check each cell to make sure that it is not part of
 * another box's shadow.  This is a little complicated since most shadows are
 * merged onto stdscr.
 */
static bool
last_shadow(DIALOG_WINDOWS * dw, int y, int x)
{
    DIALOG_WINDOWS *p;
    bool result = TRUE;

    for (p = dialog_state.all_windows; p != 0; p = p->next) {
	if (p->normal != dw->normal
	    && in_shadow(p->normal, dw->shadow, y, x)) {
	    result = FALSE;
	    break;
	}
    }
    return result;
}

static void
repaint_cell(DIALOG_WINDOWS * dw, bool draw, int y, int x)
{
    WINDOW *win = dw->shadow;
    WINDOW *cellwin;
    int y2, x2;

    if ((cellwin = window_at_cell(dw, y, x)) != 0
	&& (draw || last_shadow(dw, y, x))
	&& (y2 = (y + getbegy(win) - getbegy(cellwin))) >= 0
	&& (x2 = (x + getbegx(win) - getbegx(cellwin))) >= 0
	&& wmove(cellwin, y2, x2) != ERR) {
	chtype the_cell = dlg_get_attrs(cellwin);
	chtype the_attr = (draw ? shadow_attr : the_cell);

	if (winch(cellwin) & A_ALTCHARSET) {
	    the_attr |= A_ALTCHARSET;
	}
#if USE_WCHGAT
	wchgat(cellwin, 1,
	       the_attr & (chtype) (~A_COLOR),
	       (short) PAIR_NUMBER(the_attr),
	       NULL);
#else
	{
	    chtype the_char = ((winch(cellwin) & A_CHARTEXT) | the_attr);
	    (void) waddch(cellwin, the_char);
	}
#endif
	wnoutrefresh(cellwin);
    }
}

#define RepaintCell(dw, draw, y, x) repaint_cell(dw, draw, y, x)

static void
repaint_shadow(DIALOG_WINDOWS * dw, bool draw, int y, int x, int height, int width)
{
    int i, j;

    if (UseShadow(dw)) {
#if !USE_WCHGAT
	chtype save = dlg_get_attrs(dw->shadow);
	dlg_attrset(dw->shadow, draw ? shadow_attr : screen_attr);
#endif
	for (i = 0; i < SHADOW_ROWS; ++i) {
	    for (j = 0; j < width; ++j) {
		RepaintCell(dw, draw, i + y + height, j + x + SHADOW_COLS);
	    }
	}
	for (i = 0; i < height; i++) {
	    for (j = 0; j < SHADOW_COLS; ++j) {
		RepaintCell(dw, draw, i + y + SHADOW_ROWS, j + x + width);
	    }
	}
	(void) wnoutrefresh(dw->shadow);
#if !USE_WCHGAT
	dlg_attrset(dw->shadow, save);
#endif
    }
}

/*
 * Draw a shadow on the parent window corresponding to the right- and
 * bottom-edge of the child window, to give a 3-dimensional look.
 */
static void
draw_childs_shadow(DIALOG_WINDOWS * dw)
{
    if (UseShadow(dw)) {
	repaint_shadow(dw,
		       TRUE,
		       getbegy(dw->normal) - getbegy(dw->shadow),
		       getbegx(dw->normal) - getbegx(dw->shadow),
		       getmaxy(dw->normal),
		       getmaxx(dw->normal));
    }
}

/*
 * Erase a shadow on the parent window corresponding to the right- and
 * bottom-edge of the child window.
 */
static void
erase_childs_shadow(DIALOG_WINDOWS * dw)
{
    if (UseShadow(dw)) {
	repaint_shadow(dw,
		       FALSE,
		       getbegy(dw->normal) - getbegy(dw->shadow),
		       getbegx(dw->normal) - getbegx(dw->shadow),
		       getmaxy(dw->normal),
		       getmaxx(dw->normal));
    }
}

/*
 * Draw shadows along the right and bottom edge to give a more 3D look
 * to the boxes.
 */
void
dlg_draw_shadow(WINDOW *win, int y, int x, int height, int width)
{
    repaint_shadow(find_window(win), TRUE, y, x, height, width);
}
#endif /* HAVE_COLOR */

/*
 * Allow shell scripts to remap the exit codes so they can distinguish ESC
 * from ERROR.
 */
void
dlg_exit(int code)
{
    /* *INDENT-OFF* */
    static const struct {
	int code;
	const char *name;
    } table[] = {
	{ DLG_EXIT_CANCEL, 	"DIALOG_CANCEL" },
	{ DLG_EXIT_ERROR,  	"DIALOG_ERROR" },
	{ DLG_EXIT_ESC,	   	"DIALOG_ESC" },
	{ DLG_EXIT_EXTRA,  	"DIALOG_EXTRA" },
	{ DLG_EXIT_HELP,   	"DIALOG_HELP" },
	{ DLG_EXIT_OK,	   	"DIALOG_OK" },
	{ DLG_EXIT_ITEM_HELP,	"DIALOG_ITEM_HELP" },
    };
    /* *INDENT-ON* */

    unsigned n;
    char *name;
    char *temp;
    long value;
    bool overridden = FALSE;

  retry:
    for (n = 0; n < sizeof(table) / sizeof(table[0]); n++) {
	if (table[n].code == code) {
	    if ((name = getenv(table[n].name)) != 0) {
		value = strtol(name, &temp, 0);
		if (temp != 0 && temp != name && *temp == '\0') {
		    code = (int) value;
		    overridden = TRUE;
		}
	    }
	    break;
	}
    }

    /*
     * Prior to 2004/12/19, a widget using --item-help would exit with "OK"
     * if the help button were selected.  Now we want to exit with "HELP",
     * but allow the environment variable to override.
     */
    if (code == DLG_EXIT_ITEM_HELP && !overridden) {
	code = DLG_EXIT_HELP;
	goto retry;
    }
#ifdef HAVE_DLG_TRACE
    dlg_trace((const char *) 0);	/* close it */
#endif

#ifdef NO_LEAKS
    _dlg_inputstr_leaks();
#if defined(NCURSES_VERSION) && defined(HAVE__NC_FREE_AND_EXIT)
    _nc_free_and_exit(code);
#endif
#endif

    if (dialog_state.input == stdin) {
	exit(code);
    } else {
	/*
	 * Just in case of using --input-fd option, do not
	 * call atexit functions of ncurses which may hang.
	 */
	if (dialog_state.input) {
	    fclose(dialog_state.input);
	    dialog_state.input = 0;
	}
	if (dialog_state.pipe_input) {
	    if (dialog_state.pipe_input != stdin) {
		fclose(dialog_state.pipe_input);
		dialog_state.pipe_input = 0;
	    }
	}
	_exit(code);
    }
}

/* quit program killing all tailbg */
void
dlg_exiterr(const char *fmt,...)
{
    int retval;
    va_list ap;

    end_dialog();

    (void) fputc('\n', stderr);
    va_start(ap, fmt);
    (void) vfprintf(stderr, fmt, ap);
    va_end(ap);
    (void) fputc('\n', stderr);

    dlg_killall_bg(&retval);

    (void) fflush(stderr);
    (void) fflush(stdout);
    dlg_exit(DLG_EXIT_ERROR);
}

void
dlg_beeping(void)
{
    if (dialog_vars.beep_signal) {
	(void) beep();
	dialog_vars.beep_signal = 0;
    }
}

void
dlg_print_size(int height, int width)
{
    if (dialog_vars.print_siz) {
	fprintf(dialog_state.output, "Size: %d, %d\n", height, width);
	DLG_TRACE(("# print size: %dx%d\n", height, width));
    }
}

void
dlg_ctl_size(int height, int width)
{
    if (dialog_vars.size_err) {
	if ((width > COLS) || (height > LINES)) {
	    dlg_exiterr("Window too big. (height, width) = (%d, %d). Max allowed (%d, %d).",
			height, width, LINES, COLS);
	}
#ifdef HAVE_COLOR
	else if ((dialog_state.use_shadow)
		 && ((width > SCOLS || height > SLINES))) {
	    if ((width <= COLS) && (height <= LINES)) {
		/* try again, without shadows */
		dialog_state.use_shadow = 0;
	    } else {
		dlg_exiterr("Window+Shadow too big. (height, width) = (%d, %d). Max allowed (%d, %d).",
			    height, width, SLINES, SCOLS);
	    }
	}
#endif
    }
}

/*
 * If the --tab-correct was not selected, convert tabs to single spaces.
 */
void
dlg_tab_correct_str(char *prompt)
{
    char *ptr;

    if (dialog_vars.tab_correct) {
	while ((ptr = strchr(prompt, TAB)) != NULL) {
	    *ptr = ' ';
	    prompt = ptr;
	}
    }
}

void
dlg_calc_listh(int *height, int *list_height, int item_no)
{
    /* calculate new height and list_height */
    int rows = SLINES - (dialog_vars.begin_set ? dialog_vars.begin_y : 0);
    if (rows - (*height) > 0) {
	if (rows - (*height) > item_no)
	    *list_height = item_no;
	else
	    *list_height = rows - (*height);
    }
    (*height) += (*list_height);
}

/* obsolete */
int
dlg_calc_listw(int item_no, char **items, int group)
{
    int n, i, len1 = 0, len2 = 0;
    for (i = 0; i < (item_no * group); i += group) {
	if ((n = dlg_count_columns(items[i])) > len1)
	    len1 = n;
	if ((n = dlg_count_columns(items[i + 1])) > len2)
	    len2 = n;
    }
    return len1 + len2;
}

int
dlg_calc_list_width(int item_no, DIALOG_LISTITEM * items)
{
    int n, i, len1 = 0, len2 = 0;
    int bits = ((dialog_vars.no_tags ? 1 : 0)
		+ (dialog_vars.no_items ? 2 : 0));

    for (i = 0; i < item_no; ++i) {
	switch (bits) {
	case 0:
	    /* FALLTHRU */
	case 1:
	    if ((n = dlg_count_columns(items[i].name)) > len1)
		len1 = n;
	    if ((n = dlg_count_columns(items[i].text)) > len2)
		len2 = n;
	    break;
	case 2:
	    /* FALLTHRU */
	case 3:
	    if ((n = dlg_count_columns(items[i].name)) > len1)
		len1 = n;
	    break;
	}
    }
    return len1 + len2;
}

char *
dlg_strempty(void)
{
    static char empty[] = "";
    return empty;
}

char *
dlg_strclone(const char *cprompt)
{
    char *prompt = 0;
    if (cprompt != 0) {
	prompt = dlg_malloc(char, strlen(cprompt) + 1);
	assert_ptr(prompt, "dlg_strclone");
	strcpy(prompt, cprompt);
    }
    return prompt;
}

chtype
dlg_asciibox(chtype ch)
{
    chtype result = 0;

    if (ch == ACS_ULCORNER)
	result = '+';
    else if (ch == ACS_LLCORNER)
	result = '+';
    else if (ch == ACS_URCORNER)
	result = '+';
    else if (ch == ACS_LRCORNER)
	result = '+';
    else if (ch == ACS_HLINE)
	result = '-';
    else if (ch == ACS_VLINE)
	result = '|';
    else if (ch == ACS_LTEE)
	result = '+';
    else if (ch == ACS_RTEE)
	result = '+';
    else if (ch == ACS_UARROW)
	result = '^';
    else if (ch == ACS_DARROW)
	result = 'v';

    return result;
}

chtype
dlg_boxchar(chtype ch)
{
    chtype result = dlg_asciibox(ch);

    if (result != 0) {
	if (dialog_vars.ascii_lines)
	    ch = result;
	else if (dialog_vars.no_lines)
	    ch = ' ';
    }
    return ch;
}

int
dlg_box_x_ordinate(int width)
{
    int x;

    if (dialog_vars.begin_set == 1) {
	x = dialog_vars.begin_x;
    } else {
	/* center dialog box on screen unless --begin-set */
	x = (SCOLS - width) / 2;
    }
    return x;
}

int
dlg_box_y_ordinate(int height)
{
    int y;

    if (dialog_vars.begin_set == 1) {
	y = dialog_vars.begin_y;
    } else {
	/* center dialog box on screen unless --begin-set */
	y = (SLINES - height) / 2;
    }
    return y;
}

void
dlg_draw_title(WINDOW *win, const char *title)
{
    if (title != NULL) {
	chtype attr = A_NORMAL;
	chtype save = dlg_get_attrs(win);
	int x = centered(getmaxx(win), title);

	dlg_attrset(win, title_attr);
	wmove(win, 0, x);
	dlg_print_text(win, title, getmaxx(win) - x, &attr);
	dlg_attrset(win, save);
	dlg_finish_string(title);
    }
}

void
dlg_draw_bottom_box2(WINDOW *win, chtype on_left, chtype on_right, chtype on_inside)
{
    int width = getmaxx(win);
    int height = getmaxy(win);
    int i;

    dlg_attrset(win, on_left);
    (void) wmove(win, height - 3, 0);
    (void) waddch(win, dlg_boxchar(ACS_LTEE));
    for (i = 0; i < width - 2; i++)
	(void) waddch(win, dlg_boxchar(ACS_HLINE));
    dlg_attrset(win, on_right);
    (void) waddch(win, dlg_boxchar(ACS_RTEE));
    dlg_attrset(win, on_inside);
    (void) wmove(win, height - 2, 1);
    for (i = 0; i < width - 2; i++)
	(void) waddch(win, ' ');
}

void
dlg_draw_bottom_box(WINDOW *win)
{
    dlg_draw_bottom_box2(win, border_attr, dialog_attr, dialog_attr);
}

/*
 * Remove a window, repainting everything else.  This would be simpler if we
 * used the panel library, but that is not _always_ available.
 */
void
dlg_del_window(WINDOW *win)
{
    DIALOG_WINDOWS *p, *q, *r;

    /*
     * If --keep-window was set, do not delete/repaint the windows.
     */
    if (dialog_vars.keep_window)
	return;

    /* Leave the main window untouched if there are no background windows.
     * We do this so the current window will not be cleared on exit, allowing
     * things like the infobox demo to run without flicker.
     */
    if (dialog_state.getc_callbacks != 0) {
	touchwin(stdscr);
	wnoutrefresh(stdscr);
    }

    for (p = dialog_state.all_windows, q = r = 0; p != 0; r = p, p = p->next) {
	if (p->normal == win) {
	    q = p;		/* found a match - should be only one */
	    if (r == 0) {
		dialog_state.all_windows = p->next;
	    } else {
		r->next = p->next;
	    }
	} else {
	    if (p->shadow != 0) {
		touchwin(p->shadow);
		wnoutrefresh(p->shadow);
	    }
	    touchwin(p->normal);
	    wnoutrefresh(p->normal);
	}
    }

    if (q) {
	if (dialog_state.all_windows != 0)
	    erase_childs_shadow(q);
	del_subwindows(q->normal);
	dlg_unregister_window(q->normal);
	delwin(q->normal);
	free(q);
    }
    doupdate();
}

/*
 * Create a window, optionally with a shadow.
 */
WINDOW *
dlg_new_window(int height, int width, int y, int x)
{
    return dlg_new_modal_window(stdscr, height, width, y, x);
}

/*
 * "Modal" windows differ from normal ones by having a shadow in a window
 * separate from the standard screen.
 */
WINDOW *
dlg_new_modal_window(WINDOW *parent, int height, int width, int y, int x)
{
    WINDOW *win;
    DIALOG_WINDOWS *p = dlg_calloc(DIALOG_WINDOWS, 1);

    (void) parent;
    if (p == 0
	|| (win = newwin(height, width, y, x)) == 0) {
	dlg_exiterr("Can't make new window at (%d,%d), size (%d,%d).\n",
		    y, x, height, width);
    }
    p->next = dialog_state.all_windows;
    p->normal = win;
    dialog_state.all_windows = p;
#ifdef HAVE_COLOR
    if (dialog_state.use_shadow) {
	p->shadow = parent;
	draw_childs_shadow(p);
    }
#endif

    (void) keypad(win, TRUE);
    return win;
}

/*
 * Move/Resize a window, optionally with a shadow.
 */
#ifdef KEY_RESIZE
void
dlg_move_window(WINDOW *win, int height, int width, int y, int x)
{
    DIALOG_WINDOWS *p;

    if (win != 0) {
	dlg_ctl_size(height, width);

	if ((p = find_window(win)) != 0) {
	    (void) wresize(win, height, width);
	    (void) mvwin(win, y, x);
#ifdef HAVE_COLOR
	    if (p->shadow != 0) {
		if (dialog_state.use_shadow) {
		    (void) mvwin(p->shadow, y + SHADOW_ROWS, x + SHADOW_COLS);
		} else {
		    p->shadow = 0;
		}
	    }
#endif
	    (void) refresh();

#ifdef HAVE_COLOR
	    draw_childs_shadow(p);
#endif
	}
    }
}

/*
 * Having just received a KEY_RESIZE, wait a short time to ignore followup
 * KEY_RESIZE events.
 */
void
dlg_will_resize(WINDOW *win)
{
    int n, ch, base;
    int caught = 0;

    dlg_trace_win(win);
    wtimeout(win, 20);
    for (n = base = 0; n < base + 10; ++n) {
	if ((ch = wgetch(win)) != ERR) {
	    if (ch == KEY_RESIZE) {
		base = n;
		++caught;
	    } else {
		ungetch(ch);
		break;
	    }
	}
    }
    dlg_trace_msg("# caught %d KEY_RESIZE key%s\n",
		  1 + caught,
		  caught == 1 ? "" : "s");
}
#endif /* KEY_RESIZE */

WINDOW *
dlg_sub_window(WINDOW *parent, int height, int width, int y, int x)
{
    WINDOW *win;

    if ((win = subwin(parent, height, width, y, x)) == 0) {
	dlg_exiterr("Can't make sub-window at (%d,%d), size (%d,%d).\n",
		    y, x, height, width);
    }

    add_subwindow(parent, win);
    (void) keypad(win, TRUE);
    return win;
}

/* obsolete */
int
dlg_default_item(char **items, int llen)
{
    int result = 0;

    if (dialog_vars.default_item != 0) {
	int count = 0;
	while (*items != 0) {
	    if (!strcmp(dialog_vars.default_item, *items)) {
		result = count;
		break;
	    }
	    items += llen;
	    count++;
	}
    }
    return result;
}

int
dlg_default_listitem(DIALOG_LISTITEM * items)
{
    int result = 0;

    if (dialog_vars.default_item != 0) {
	int count = 0;
	while (items->name != 0) {
	    if (!strcmp(dialog_vars.default_item, items->name)) {
		result = count;
		break;
	    }
	    ++items;
	    count++;
	}
    }
    return result;
}

/*
 * Draw the string for item_help
 */
void
dlg_item_help(const char *txt)
{
    if (USE_ITEM_HELP(txt)) {
	chtype attr = A_NORMAL;
	int y, x;

	dlg_attrset(stdscr, itemhelp_attr);
	(void) wmove(stdscr, LINES - 1, 0);
	(void) wclrtoeol(stdscr);
	(void) addch(' ');
	dlg_print_text(stdscr, txt, COLS - 1, &attr);
	if (itemhelp_attr & A_COLOR) {
	    /* fill the remainder of the line with the window's attributes */
	    getyx(stdscr, y, x);
	    (void) y;
	    while (x < COLS) {
		(void) addch(' ');
		++x;
	    }
	}
	(void) wnoutrefresh(stdscr);
    }
}

#ifndef HAVE_STRCASECMP
int
dlg_strcmp(const char *a, const char *b)
{
    int ac, bc, cmp;

    for (;;) {
	ac = UCH(*a++);
	bc = UCH(*b++);
	if (isalpha(ac) && islower(ac))
	    ac = _toupper(ac);
	if (isalpha(bc) && islower(bc))
	    bc = _toupper(bc);
	cmp = ac - bc;
	if (ac == 0 || bc == 0 || cmp != 0)
	    break;
    }
    return cmp;
}
#endif

/*
 * Returns true if 'dst' points to a blank which follows another blank which
 * is not a leading blank on a line.
 */
static bool
trim_blank(char *base, char *dst)
{
    int count = isblank(UCH(*dst));

    while (dst-- != base) {
	if (*dst == '\n') {
	    break;
	} else if (isblank(UCH(*dst))) {
	    count++;
	} else {
	    break;
	}
    }
    return (count > 1);
}

/*
 * Change embedded "\n" substrings to '\n' characters and tabs to single
 * spaces.  If there are no "\n"s, it will strip all extra spaces, for
 * justification.  If it has "\n"'s, it will preserve extra spaces.  If cr_wrap
 * is set, it will preserve '\n's.
 */
void
dlg_trim_string(char *s)
{
    char *base = s;
    char *p1;
    char *p = s;
    int has_newlines = !dialog_vars.no_nl_expand && (strstr(s, "\\n") != 0);

    while (*p != '\0') {
	if (*p == TAB && !dialog_vars.nocollapse)
	    *p = ' ';

	if (has_newlines) {	/* If prompt contains "\n" strings */
	    if (*p == '\\' && *(p + 1) == 'n') {
		*s++ = '\n';
		p += 2;
		p1 = p;
		/*
		 * Handle end of lines intelligently.  If '\n' follows "\n"
		 * then ignore the '\n'.  This eliminates the need to escape
		 * the '\n' character (no need to use "\n\").
		 */
		while (isblank(UCH(*p1)))
		    p1++;
		if (*p1 == '\n')
		    p = p1 + 1;
	    } else if (*p == '\n') {
		if (dialog_vars.cr_wrap)
		    *s++ = *p++;
		else {
		    /* Replace the '\n' with a space if cr_wrap is not set */
		    if (!trim_blank(base, p))
			*s++ = ' ';
		    p++;
		}
	    } else		/* If *p != '\n' */
		*s++ = *p++;
	} else if (dialog_vars.trim_whitespace) {
	    if (isblank(UCH(*p))) {
		if (!isblank(UCH(*(s - 1)))) {
		    *s++ = ' ';
		    p++;
		} else
		    p++;
	    } else if (*p == '\n') {
		if (dialog_vars.cr_wrap)
		    *s++ = *p++;
		else if (!isblank(UCH(*(s - 1)))) {
		    /* Strip '\n's if cr_wrap is not set. */
		    *s++ = ' ';
		    p++;
		} else
		    p++;
	    } else
		*s++ = *p++;
	} else {		/* If there are no "\n" strings */
	    if (isblank(UCH(*p)) && !dialog_vars.nocollapse) {
		if (!trim_blank(base, p))
		    *s++ = *p;
		p++;
	    } else
		*s++ = *p++;
	}
    }

    *s = '\0';
}

void
dlg_set_focus(WINDOW *parent, WINDOW *win)
{
    if (win != 0) {
	(void) wmove(parent,
		     getpary(win) + getcury(win),
		     getparx(win) + getcurx(win));
	(void) wnoutrefresh(win);
	(void) doupdate();
    }
}

/*
 * Returns the nominal maximum buffer size.
 */
int
dlg_max_input(int max_len)
{
    if (dialog_vars.max_input != 0 && dialog_vars.max_input < MAX_LEN)
	max_len = dialog_vars.max_input;

    return max_len;
}

/*
 * Free storage used for the result buffer.
 */
void
dlg_clr_result(void)
{
    if (dialog_vars.input_length) {
	dialog_vars.input_length = 0;
	if (dialog_vars.input_result)
	    free(dialog_vars.input_result);
    }
    dialog_vars.input_result = 0;
}

/*
 * Setup a fixed-buffer for the result.
 */
char *
dlg_set_result(const char *string)
{
    unsigned need = string ? (unsigned) strlen(string) + 1 : 0;

    /* inputstr.c needs a fixed buffer */
    if (need < MAX_LEN)
	need = MAX_LEN;

    /*
     * If the buffer is not big enough, allocate a new one.
     */
    if (dialog_vars.input_length != 0
	|| dialog_vars.input_result == 0
	|| need > MAX_LEN) {

	dlg_clr_result();

	dialog_vars.input_length = need;
	dialog_vars.input_result = dlg_malloc(char, need);
	assert_ptr(dialog_vars.input_result, "dlg_set_result");
    }

    strcpy(dialog_vars.input_result, string ? string : "");

    return dialog_vars.input_result;
}

/*
 * Accumulate results in dynamically allocated buffer.
 * If input_length is zero, it is a MAX_LEN buffer belonging to the caller.
 */
void
dlg_add_result(const char *string)
{
    unsigned have = (dialog_vars.input_result
		     ? (unsigned) strlen(dialog_vars.input_result)
		     : 0);
    unsigned want = (unsigned) strlen(string) + 1 + have;

    if ((want >= MAX_LEN)
	|| (dialog_vars.input_length != 0)
	|| (dialog_vars.input_result == 0)) {

	if (dialog_vars.input_length == 0
	    || dialog_vars.input_result == 0) {

	    char *save_result = dialog_vars.input_result;

	    dialog_vars.input_length = want * 2;
	    dialog_vars.input_result = dlg_malloc(char, dialog_vars.input_length);
	    assert_ptr(dialog_vars.input_result, "dlg_add_result malloc");
	    dialog_vars.input_result[0] = '\0';
	    if (save_result != 0)
		strcpy(dialog_vars.input_result, save_result);
	} else if (want >= dialog_vars.input_length) {
	    dialog_vars.input_length = want * 2;
	    dialog_vars.input_result = dlg_realloc(char,
						   dialog_vars.input_length,
						   dialog_vars.input_result);
	    assert_ptr(dialog_vars.input_result, "dlg_add_result realloc");
	}
    }
    strcat(dialog_vars.input_result, string);
}

/*
 * These are characters that (aside from the quote-delimiter) will have to
 * be escaped in a single- or double-quoted string.
 */
#define FIX_SINGLE "\n\\"
#define FIX_DOUBLE FIX_SINGLE "[]{}?*;`~#$^&()|<>"

/*
 * Returns the quote-delimiter.
 */
static const char *
quote_delimiter(void)
{
    return dialog_vars.single_quoted ? "'" : "\"";
}

/*
 * Returns true if we should quote the given string.
 */
static bool
must_quote(char *string)
{
    bool code = FALSE;

    if (*string != '\0') {
	size_t len = strlen(string);
	if (strcspn(string, quote_delimiter()) != len)
	    code = TRUE;
	else if (strcspn(string, "\n\t ") != len)
	    code = TRUE;
	else
	    code = (strcspn(string, FIX_DOUBLE) != len);
    } else {
	code = TRUE;
    }

    return code;
}

/*
 * Add a quoted string to the result buffer.
 */
void
dlg_add_quoted(char *string)
{
    char temp[2];
    const char *my_quote = quote_delimiter();
    const char *must_fix = (dialog_vars.single_quoted
			    ? FIX_SINGLE
			    : FIX_DOUBLE);

    if (must_quote(string)) {
	temp[1] = '\0';
	dlg_add_result(my_quote);
	while (*string != '\0') {
	    temp[0] = *string++;
	    if ((strchr) (my_quote, *temp) || (strchr) (must_fix, *temp))
		dlg_add_result("\\");
	    dlg_add_result(temp);
	}
	dlg_add_result(my_quote);
    } else {
	dlg_add_result(string);
    }
}

/*
 * When adding a result, make that depend on whether "--quoted" is used.
 */
void
dlg_add_string(char *string)
{
    if (dialog_vars.quoted) {
	dlg_add_quoted(string);
    } else {
	dlg_add_result(string);
    }
}

bool
dlg_need_separator(void)
{
    bool result = FALSE;

    if (dialog_vars.output_separator) {
	result = TRUE;
    } else if (dialog_vars.input_result && *(dialog_vars.input_result)) {
	result = TRUE;
    }
    return result;
}

void
dlg_add_separator(void)
{
    const char *separator = (dialog_vars.separate_output) ? "\n" : " ";

    if (dialog_vars.output_separator)
	separator = dialog_vars.output_separator;

    dlg_add_result(separator);
}

#define HELP_PREFIX		"HELP "

void
dlg_add_help_listitem(int *result, char **tag, DIALOG_LISTITEM * item)
{
    dlg_add_result(HELP_PREFIX);
    if (USE_ITEM_HELP(item->help)) {
	*tag = dialog_vars.help_tags ? item->name : item->help;
	*result = DLG_EXIT_ITEM_HELP;
    } else {
	*tag = item->name;
    }
}

void
dlg_add_help_formitem(int *result, char **tag, DIALOG_FORMITEM * item)
{
    dlg_add_result(HELP_PREFIX);
    if (USE_ITEM_HELP(item->help)) {
	*tag = dialog_vars.help_tags ? item->name : item->help;
	*result = DLG_EXIT_ITEM_HELP;
    } else {
	*tag = item->name;
    }
}

/*
 * Some widgets support only one value of a given variable - save/restore the
 * global dialog_vars so we can override it consistently.
 */
void
dlg_save_vars(DIALOG_VARS * vars)
{
    *vars = dialog_vars;
}

/*
 * Most of the data in DIALOG_VARS is normally set by command-line options.
 * The input_result member is an exception; it is normally set by the dialog
 * library to return result values.
 */
void
dlg_restore_vars(DIALOG_VARS * vars)
{
    char *save_result = dialog_vars.input_result;
    unsigned save_length = dialog_vars.input_length;

    dialog_vars = *vars;
    dialog_vars.input_result = save_result;
    dialog_vars.input_length = save_length;
}

/*
 * Called each time a widget is invoked which may do output, increment a count.
 */
void
dlg_does_output(void)
{
    dialog_state.output_count += 1;
}

/*
 * Compatibility for different versions of curses.
 */
#if !(defined(HAVE_GETBEGX) && defined(HAVE_GETBEGY))
int
dlg_getbegx(WINDOW *win)
{
    int y, x;
    getbegyx(win, y, x);
    return x;
}
int
dlg_getbegy(WINDOW *win)
{
    int y, x;
    getbegyx(win, y, x);
    return y;
}
#endif

#if !(defined(HAVE_GETCURX) && defined(HAVE_GETCURY))
int
dlg_getcurx(WINDOW *win)
{
    int y, x;
    getyx(win, y, x);
    return x;
}
int
dlg_getcury(WINDOW *win)
{
    int y, x;
    getyx(win, y, x);
    return y;
}
#endif

#if !(defined(HAVE_GETMAXX) && defined(HAVE_GETMAXY))
int
dlg_getmaxx(WINDOW *win)
{
    int y, x;
    getmaxyx(win, y, x);
    return x;
}
int
dlg_getmaxy(WINDOW *win)
{
    int y, x;
    getmaxyx(win, y, x);
    return y;
}
#endif

#if !(defined(HAVE_GETPARX) && defined(HAVE_GETPARY))
int
dlg_getparx(WINDOW *win)
{
    int y, x;
    getparyx(win, y, x);
    return x;
}
int
dlg_getpary(WINDOW *win)
{
    int y, x;
    getparyx(win, y, x);
    return y;
}
#endif

#ifdef NEED_WGETPARENT
WINDOW *
dlg_wgetparent(WINDOW *win)
{
#undef wgetparent
    WINDOW *result = 0;
    DIALOG_WINDOWS *p;

    for (p = dialog_state.all_subwindows; p != 0; p = p->next) {
	if (p->shadow == win) {
	    result = p->normal;
	    break;
	}
    }
    return result;
}
#endif
