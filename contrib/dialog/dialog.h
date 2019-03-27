/*
 *  $Id: dialog.h,v 1.283 2018/06/19 22:52:11 tom Exp $
 *
 *  dialog.h -- common declarations for all dialog modules
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
 *
 *  An earlier version of this program lists as authors
 *	Savio Lam (lam836@cs.cuhk.hk)
 */

#ifndef DIALOG_H_included
#define DIALOG_H_included 1
/* *INDENT-OFF* */

#include <dlg_config.h>

#ifdef __hpux
#define __HP_CURSES_COMPAT	/* workaround for getattrs, etc. */
#endif

#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <signal.h>	/* fork() etc. */
#include <math.h>	/* sqrt() */

/* header conflict with Solaris xpg4 versus <sys/regset.h> */
#if defined(ERR) && (ERR == 13)
#undef ERR
#endif

#if defined(HAVE_NCURSESW_NCURSES_H)
#include <ncursesw/ncurses.h>
#elif defined(HAVE_NCURSES_NCURSES_H)
#include <ncurses/ncurses.h>
#elif defined(HAVE_NCURSES_CURSES_H)
#include <ncurses/curses.h>
#elif defined(HAVE_NCURSES_H)
#include <ncurses.h>
#else
#include <curses.h>
#if defined(HAVE_UNCTRL_H)
#include <unctrl.h> /* most curses.h headers include this, some do not */
#endif
#endif

/* Solaris xpg4 renames these */
#ifndef KEY_MAX
#ifdef __KEY_MAX
#define KEY_MAX __KEY_MAX
#endif
#endif

#ifndef KEY_MIN
#ifdef __KEY_MIN
#define KEY_MIN __KEY_MIN
#endif
#endif

/* possible conflicts with <term.h> which may be included in <curses.h> */
#ifdef color_names
#undef color_names
#endif

#ifdef buttons
#undef buttons
#endif

#ifdef ENABLE_NLS
#include <libintl.h>
#include <langinfo.h>
#define _(s) dgettext(PACKAGE, s)
#else
#undef _
#define _(s) s
#endif

#ifndef GCC_PRINTFLIKE
#define GCC_PRINTFLIKE(fmt,var) /*nothing*/
#endif

#ifndef GCC_NORETURN
#define GCC_NORETURN /*nothing*/
#endif

#ifndef GCC_UNUSED
#define GCC_UNUSED /*nothing*/
#endif

#ifndef HAVE_WGET_WCH
#undef USE_WIDE_CURSES
#endif

/*
 * FIXME: a configure check would be useful
 */
#ifdef __hpux
#undef ACS_UARROW
#undef ACS_DARROW
#undef ACS_BLOCK
#endif

/*
 * Change these if you want
 */
#define USE_SHADOW TRUE
#define USE_COLORS TRUE

#ifdef HAVE_COLOR
#define SCOLS	(COLS - (dialog_state.use_shadow ? SHADOW_COLS : 0))
#define SLINES	(LINES - (dialog_state.use_shadow ? SHADOW_ROWS : 0))
#else
#define SCOLS	COLS
#define SLINES	LINES
#endif

#define DLG_EXIT_ESC		255
#define DLG_EXIT_UNKNOWN	-2	/* never return this (internal use) */
#define DLG_EXIT_ERROR		-1	/* the shell sees this as 255 */
#define DLG_EXIT_OK		0
#define DLG_EXIT_CANCEL		1
#define DLG_EXIT_HELP		2
#define DLG_EXIT_EXTRA		3
#define DLG_EXIT_ITEM_HELP	4	/* actually DLG_EXIT_HELP */

#define DLG_CTRL(n)	((n) & 0x1f)	/* CTRL is preferred, but conflicts */

#define CHR_HELP	DLG_CTRL('E')
#define CHR_BACKSPACE	DLG_CTRL('H')
#define CHR_REPAINT	DLG_CTRL('L')
#define CHR_KILL	DLG_CTRL('U')
#define CHR_LITERAL	DLG_CTRL('V')
#define CHR_DELETE	127
#define CHR_NEXT	DLG_CTRL('N')
#define CHR_PREVIOUS	DLG_CTRL('P')
#define CHR_TRACE	DLG_CTRL('T')
#define CHR_SPACE 	' '

#define ESC		27
#define TAB		DLG_CTRL('I')

#define MARGIN 1	/* width of the line drawn around each box */
#define GUTTER 2	/* minimum columns between name/description in menu */
#define SHADOW_ROWS 1	/* rows to reserve for window's shadow */
#define SHADOW_COLS 2	/* columns to reserve for window's shadow */
#define ARROWS_COL  5	/* distance from left margin to up/down arrows */

#define MAX_LEN 2048
#define BUF_SIZE (10L*1024)

#undef  MIN
#define MIN(x,y) ((x) < (y) ? (x) : (y))

#undef  MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))

#define DEFAULT_SEPARATE_STR "\t"
#define DEFAULT_ASPECT_RATIO 9
/* how many spaces is a tab long (default)? */
#define TAB_LEN 8
#define WTIMEOUT_VAL        10	/* minimum amount of time needed for curses */

#ifndef A_CHARTEXT
#define A_CHARTEXT 0xff
#endif

#define CharOf(ch)  ((ch) & 0xff)

#ifndef ACS_ULCORNER
#define ACS_ULCORNER '+'
#endif
#ifndef ACS_LLCORNER
#define ACS_LLCORNER '+'
#endif
#ifndef ACS_URCORNER
#define ACS_URCORNER '+'
#endif
#ifndef ACS_LRCORNER
#define ACS_LRCORNER '+'
#endif
#ifndef ACS_HLINE
#define ACS_HLINE '-'
#endif
#ifndef ACS_VLINE
#define ACS_VLINE '|'
#endif
#ifndef ACS_LTEE
#define ACS_LTEE '+'
#endif
#ifndef ACS_RTEE
#define ACS_RTEE '+'
#endif
#ifndef ACS_UARROW
#define ACS_UARROW '^'
#endif
#ifndef ACS_DARROW
#define ACS_DARROW 'v'
#endif
#ifndef ACS_BLOCK
#define ACS_BLOCK '#'
#endif

/* these definitions may work for antique versions of curses */
#ifndef HAVE_GETBEGYX
#undef  getbegyx
#define getbegyx(win,y,x)	(y = (win)?(win)->_begy:ERR, x = (win)?(win)->_begx:ERR)
#endif

#ifndef HAVE_GETMAXYX
#undef  getmaxyx
#define getmaxyx(win,y,x)	(y = (win)?(win)->_maxy:ERR, x = (win)?(win)->_maxx:ERR)
#endif

#ifndef HAVE_GETPARYX
#undef  getparyx
#define getparyx(win,y,x)	(y = (win)?(win)->_pary:ERR, x = (win)?(win)->_parx:ERR)
#endif

#if !defined(HAVE_WGETPARENT) && defined(HAVE_WINDOW__PARENT)
#undef  wgetparent
#define wgetparent(win)		((win) ? (win)->_parent : 0)
#endif

#if !defined(HAVE_WSYNCUP)
#undef wsyncup
#define wsyncup(win) /* nothing */
#endif

#if !defined(HAVE_WCURSYNCUP)
#undef wcursyncup
#define wcursyncup(win) /* nothing */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* these definitions may be needed for bleeding-edge curses implementations */
#if !(defined(HAVE_GETBEGX) && defined(HAVE_GETBEGY))
#undef getbegx
#undef getbegy
#define getbegx(win) dlg_getbegx(win)
#define getbegy(win) dlg_getbegy(win)
extern int dlg_getbegx(WINDOW * /*win*/);
extern int dlg_getbegy(WINDOW * /*win*/);
#endif

#if !(defined(HAVE_GETCURX) && defined(HAVE_GETCURY))
#undef getcurx
#undef getcury
#define getcurx(win) dlg_getcurx(win)
#define getcury(win) dlg_getcury(win)
extern int dlg_getcurx(WINDOW * /*win*/);
extern int dlg_getcury(WINDOW * /*win*/);
#endif

#if !(defined(HAVE_GETMAXX) && defined(HAVE_GETMAXY))
#undef getmaxx
#undef getmaxy
#define getmaxx(win) dlg_getmaxx(win)
#define getmaxy(win) dlg_getmaxy(win)
extern int dlg_getmaxx(WINDOW * /*win*/);
extern int dlg_getmaxy(WINDOW * /*win*/);
#endif

#if !(defined(HAVE_GETPARX) && defined(HAVE_GETPARY))
#undef getparx
#undef getpary
#define getparx(win) dlg_getparx(win)
#define getpary(win) dlg_getpary(win)
extern int dlg_getparx(WINDOW * /*win*/);
extern int dlg_getpary(WINDOW * /*win*/);
#endif

#if !(defined(HAVE_WGETPARENT) && defined(HAVE_WINDOW__PARENT))
#undef wgetparent
#define wgetparent(win) dlg_wgetparent(win)
extern WINDOW * dlg_wgetparent(WINDOW * /*win*/);
#endif

/*
 * This is a list of "old" names, which should be helpful in updating
 * applications that use libdialog.  Starting with 2003/11/26, all exported
 * symbols from libdialog have "dlg_" prefix, or "dialog_" prefix or "_dialog"
 * suffix (or suffix "_dialog", e.g., init_dialog).
 */
#ifdef __DIALOG_OLD_NAMES__
#define color_table                       dlg_color_table
#define attr_clear(win,h,w,a)             dlg_attr_clear(win,h,w,a)
#define auto_size(t,s,h,w,xl,mc)          dlg_auto_size(t,s,h,w,xl,mc)
#define auto_sizefile(t,f,h,w,xl,mc)      dlg_auto_sizefile(t,f,h,w,xl,mc)
#define beeping()                         dlg_beeping()
#define box_x_ordinate(w)                 dlg_box_x_ordinate(w)
#define box_y_ordinate(h)                 dlg_box_y_ordinate(h)
#define calc_listh(h,lh,in)               dlg_calc_listh(h,lh,in)
#define calc_listw(in,items,group)        dlg_calc_listw(in,items,group)
#define color_setup()                     dlg_color_setup()
#define create_rc(f)                      dlg_create_rc(f)
#define ctl_size(h,w)                     dlg_ctl_size(h,w)
#define del_window(win)                   dlg_del_window(win)
#define dialog_clear()                    dlg_clear()
#define draw_bottom_box(win)              dlg_draw_bottom_box(win)
#define draw_box(win,y,x,h,w,xc,bc)       dlg_draw_box(win,y,x,h,w,xc,bc)
#define draw_shadow(win,h,w,y,x)          dlg_draw_shadow(win,h,w,y,x)
#define draw_title(win,t)                 dlg_draw_title(win,t)
#define exiterr                           dlg_exiterr
#define killall_bg(n)                     dlg_killall_bg(n)
#define mouse_bigregion(y,x)              dlg_mouse_bigregion(y,x)
#define mouse_free_regions()              dlg_mouse_free_regions()
#define mouse_mkbigregion(y,x,h,w,n,ix,iy,m) dlg_mouse_mkbigregion(y,x,h,w,n,ix,iy,m)
#define mouse_mkregion(y,x,h,w,n)         dlg_mouse_mkregion(y,x,h,w,n)
#define mouse_region(y,x)                 dlg_mouse_region(y,x)
#define mouse_setbase(x,y)                dlg_mouse_setbase(x,y)
#define mouse_setcode(c)                  dlg_mouse_setcode(c)
#define mouse_wgetch(w,c)                 dlg_mouse_wgetch(w,c)
#define new_window(h,w,y,x)               dlg_new_window(h,w,y,x)
#define parse_rc()                        dlg_parse_rc()
#define print_autowrap(win,s,h,w)         dlg_print_autowrap(win,s,h,w)
#define print_size(h,w)                   dlg_print_size(h,w)
#define put_backtitle()                   dlg_put_backtitle()
#define strclone(cprompt)                 dlg_strclone(cprompt)
#define sub_window(win,h,w,y,x)           dlg_sub_window(win,h,w,y,x)
#define tab_correct_str(s)                dlg_tab_correct_str(s)
#endif

/*
 * Attribute names
 */
#define DIALOG_ATR(n)                 dlg_color_table[n].atr

#define screen_attr                   DIALOG_ATR(0)
#define shadow_attr                   DIALOG_ATR(1)
#define dialog_attr                   DIALOG_ATR(2)
#define title_attr                    DIALOG_ATR(3)
#define border_attr                   DIALOG_ATR(4)
#define button_active_attr            DIALOG_ATR(5)
#define button_inactive_attr          DIALOG_ATR(6)
#define button_key_active_attr        DIALOG_ATR(7)
#define button_key_inactive_attr      DIALOG_ATR(8)
#define button_label_active_attr      DIALOG_ATR(9)
#define button_label_inactive_attr    DIALOG_ATR(10)
#define inputbox_attr                 DIALOG_ATR(11)
#define inputbox_border_attr          DIALOG_ATR(12)
#define searchbox_attr                DIALOG_ATR(13)
#define searchbox_title_attr          DIALOG_ATR(14)
#define searchbox_border_attr         DIALOG_ATR(15)
#define position_indicator_attr       DIALOG_ATR(16)
#define menubox_attr                  DIALOG_ATR(17)
#define menubox_border_attr           DIALOG_ATR(18)
#define item_attr                     DIALOG_ATR(19)
#define item_selected_attr            DIALOG_ATR(20)
#define tag_attr                      DIALOG_ATR(21)
#define tag_selected_attr             DIALOG_ATR(22)
#define tag_key_attr                  DIALOG_ATR(23)
#define tag_key_selected_attr         DIALOG_ATR(24)
#define check_attr                    DIALOG_ATR(25)
#define check_selected_attr           DIALOG_ATR(26)
#define uarrow_attr                   DIALOG_ATR(27)
#define darrow_attr                   DIALOG_ATR(28)
#define itemhelp_attr                 DIALOG_ATR(29)
#define form_active_text_attr         DIALOG_ATR(30)
#define form_text_attr                DIALOG_ATR(31)
#define form_item_readonly_attr       DIALOG_ATR(32)
#define gauge_attr                    DIALOG_ATR(33)
#define border2_attr                  DIALOG_ATR(34)
#define inputbox_border2_attr         DIALOG_ATR(35)
#define searchbox_border2_attr        DIALOG_ATR(36)
#define menubox_border2_attr          DIALOG_ATR(37)

#define DLGK_max (KEY_MAX + 256)

/*
 * Use attributes.
 */
#ifdef PDCURSES
#define dlg_attrset(w,a)  (void) wattrset((w), (a))
#define dlg_attron(w,a)   (void) wattron((w), (a))
#define dlg_attroff(w,a)  (void) wattroff((w), (a))
#else
#define dlg_attrset(w,a)  (void) wattrset((w), (int)(a))
#define dlg_attron(w,a)   (void) wattron((w), (int)(a))
#define dlg_attroff(w,a)  (void) wattroff((w), (int)(a))
#endif

/*
 * Callbacks are used to implement the "background" tailbox.
 */
struct _dlg_callback;

typedef void (*DIALOG_FREEBACK) (struct _dlg_callback * /* p */);

typedef struct _dlg_callback {
    struct _dlg_callback *next;
    FILE *input;
    WINDOW *win;
    bool keep_bg;	/* keep in background, on exit */
    bool bg_task;	/* true if this is background task */
    bool (*handle_getc)(struct _dlg_callback *p, int ch, int fkey, int *result);
    bool keep_win;	/* true to not erase window on exit */
    /* data for dlg_add_callback_ref */
    struct _dlg_callback **caller;
    DIALOG_FREEBACK freeback;
    /* 1.1-20110107 */
    bool (*handle_input)(struct _dlg_callback *p);
    bool input_ready;
} DIALOG_CALLBACK;

typedef struct _dlg_windows {
    struct _dlg_windows *next;
    WINDOW *normal;
    WINDOW *shadow;
} DIALOG_WINDOWS;

/*
 * Global variables, which are initialized as needed
 */
typedef struct {
    DIALOG_CALLBACK *getc_callbacks;
    DIALOG_CALLBACK *getc_redirect;
    DIALOG_WINDOWS *all_windows;
    DIALOG_WINDOWS *all_subwindows;
    FILE *output;		/* option "--output-fd fd" */
    FILE *pipe_input;		/* used for gauge widget */
    FILE *screen_output;	/* newterm(), etc. */
    bool screen_initialized;
    bool use_colors;		/* use colors by default? */
    bool use_scrollbar;		/* option "--scrollbar" */
    bool use_shadow;		/* shadow dialog boxes by default? */
    bool visit_items;		/* option "--visit-items" */
    char *separate_str;		/* option "--separate-widget string" */
    int aspect_ratio;		/* option "--aspect ratio" */
    int output_count;		/* # of widgets that may have done output */
    int tab_len;		/* option "--tab-len n" */
    /* 1.0-20070227 */
    FILE *input;		/* option "--input-fd fd" */
#ifdef HAVE_DLG_TRACE
    FILE *trace_output;		/* option "--trace file" */
#endif
    /* 1.1-20110106 */
    bool no_mouse;		/* option "--no-mouse" */
    int visit_cols;		/* option "--visit-items" */
    /* 1.2-20130922 */
    bool finish_string;		/* caching optimization for gauge */
    /* 1.2-20150125 */
    bool plain_buttons;		/* true to suppress button-label highlight */
    /* 1.3-20180610 */
    bool text_only;		/* option "--print-text-only", etc. */
    int text_height;
    int text_width;
} DIALOG_STATE;

extern DIALOG_STATE dialog_state;

/*
 * Global variables, which dialog resets before each widget
 */
typedef struct {
    bool beep_after_signal;	/* option "--beep-after" */
    bool beep_signal;		/* option "--beep" */
    bool begin_set;		/* option "--begin y x" was used */
    bool cant_kill;		/* option "--no-kill" */
    bool colors;		/* option "--colors" */
    bool cr_wrap;		/* option "--cr-wrap" */
    bool defaultno;		/* option "--defaultno" */
    bool dlg_clear_screen;	/* option "--clear" */
    bool extra_button;		/* option "--extra-button" */
    bool help_button;		/* option "--help-button" */
    bool help_status;		/* option "--help-status" */
    bool input_menu;		/* menu vs inputmenu widget */
    bool insecure;		/* option "--insecure" */
    bool item_help;		/* option "--item-help" */
    bool keep_window;		/* option "--keep-window" */
    bool nocancel;		/* option "--no-cancel" */
    bool nocollapse;		/* option "--no-collapse" */
    bool print_siz;		/* option "--print-size" */
    bool separate_output;	/* option "--separate-output" */
    bool single_quoted;		/* option "--single-quoted" */
    bool size_err;		/* option "--size-err" */
    bool tab_correct;		/* option "--tab-correct" */
    bool trim_whitespace;	/* option "--trim" */
    char *backtitle;		/* option "--backtitle backtitle" */
    char *cancel_label;		/* option "--cancel-label string" */
    char *default_item;		/* option "--default-item string" */
    char *exit_label;		/* option "--exit-label string" */
    char *extra_label;		/* option "--extra-label string" */
    char *help_label;		/* option "--help-label string" */
    char *input_result;
    char *no_label;		/* option "--no-label string" */
    char *ok_label;		/* option "--ok-label string" */
    char *title;		/* option "--title title" */
    char *yes_label;		/* option "--yes-label string" */
    int begin_x;		/* option "--begin y x" (second value) */
    int begin_y;		/* option "--begin y x" (first value) */
    int max_input;		/* option "--max-input size" */
    int scale_factor;		/* RESERVED */
    int sleep_secs;		/* option "--sleep secs" */
    int timeout_secs;		/* option "--timeout secs" */
    unsigned input_length;	/* nonzero if input_result is allocated */
    /* 1.0-20051207 */
    unsigned formitem_type;	/* DIALOG_FORMITEM.type in dialog_form() */
    /* 1.1-20070227 */
    bool keep_tite;		/* option "--keep-tite" */
    bool ascii_lines;		/* option "--ascii-lines" */
    bool no_lines;		/* option "--no-lines" */
    /* 1.1-20070930 */
    bool nook;			/* option "--no-ok" */
    /* 1.1-20080727 */
    bool quoted;		/* option "--quoted" */
    char *column_header;	/* RESERVED "--column-header" */
    char *column_separator;	/* option "--column-separator" */
    char *output_separator;	/* option "--output-separator" */
    /* 1.1-20100118 */
    char *date_format;		/* option "--date-format" */
    char *time_format;		/* option "--time-format" */
    /* 1.1-20110629 */
    char *help_line;		/* option "--hline" */
    char *help_file;		/* option "--hfile" */
    bool in_helpfile;		/* flag to prevent recursion in --hfile */
    bool no_nl_expand;		/* option "--no-nl-expand" */
    /* 1.1-20120701 */
    int default_button;		/* option "--default-button" (exit code) */
    /* 1.1-20121218 */
    bool no_tags;		/* option "--no-tags" */
    bool no_items;		/* option "--no-items" */
    /* 1.2-20130315 */
    bool last_key;		/* option "--last-key" */
    /* 1.2-20130902 */
    bool help_tags;		/* option "--help-tags" */
    /* 1.3-20160126 */
    char *week_start;		/* option "--week-start" */
    /* 1.3-20160206 */
    bool iso_week;		/* option "--iso-week" */
    /* 1.3-20170131 */
    bool reorder;		/* option "--reorder" */
} DIALOG_VARS;

#define USE_ITEM_HELP(s)        (dialog_vars.item_help && (s) != 0)

/*
 * Some settings change the number of data items per row which dialog reads
 * from a script.
 */
#define DLG__NO_ITEMS		(dialog_vars.no_items ? 0 : 1)
#define DLG__ITEM_HELP          (dialog_vars.item_help ? 1 : 0)

/*
 * These are the total number of data items per row used for each widget type.
 */
#define CHECKBOX_TAGS           (2 + DLG__ITEM_HELP + DLG__NO_ITEMS)
#define MENUBOX_TAGS            (1 + DLG__ITEM_HELP + DLG__NO_ITEMS)
#define FORMBOX_TAGS            (8 + DLG__ITEM_HELP)
#define MIXEDFORM_TAGS          (1 + FORMBOX_TAGS)
#define MIXEDGAUGE_TAGS         2
#define TREEVIEW_TAGS           (3 + DLG__ITEM_HELP + DLG__NO_ITEMS)

extern DIALOG_VARS dialog_vars;

#ifndef HAVE_TYPE_CHTYPE
#define chtype long
#endif

#ifndef isblank
#define isblank(c)       ((c) == ' ' || (c) == TAB)
#endif

#define UCH(ch)			((unsigned char)(ch))

#define assert_ptr(ptr,msg) if ((ptr) == 0) dlg_exiterr("cannot allocate memory in " msg)

#define dlg_malloc(t,n)    (t *) malloc((size_t)(n) * sizeof(t))
#define dlg_calloc(t,n)    (t *) calloc((size_t)(n), sizeof(t))
#define dlg_realloc(t,n,p) (t *) realloc((p), (n) * sizeof(t))

/*
 * Table for attribute- and color-values.
 */
typedef struct {
    chtype atr;
#ifdef HAVE_COLOR
    int fg;
    int bg;
    int hilite;
#endif
#ifdef HAVE_RC_FILE
    const char *name;
    const char *comment;
#endif
} DIALOG_COLORS;

extern DIALOG_COLORS dlg_color_table[];

/*
 * Function prototypes
 */
extern const char *dialog_version(void);

/* widgets, each in separate files */
extern int dialog_buildlist(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*list_height*/, int /*item_no*/, char ** /*items*/, int /*order_mode*/);
extern int dialog_calendar(const char * /*title*/, const char * /*subtitle*/, int /*height*/, int /*width*/, int /*day*/, int /*month*/, int /*year*/);
extern int dialog_checklist(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*list_height*/, int /*item_no*/, char ** /*items*/, int /*flag*/);
extern int dialog_dselect(const char * /*title*/, const char * /*path*/, int /*height*/, int /*width*/);
extern int dialog_editbox(const char * /*title*/, const char * /*file*/, int /*height*/, int /*width*/);
extern int dialog_form(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*form_height*/, int /*item_no*/, char ** /*items*/);
extern int dialog_fselect(const char * /*title*/, const char * /*path*/, int /*height*/, int /*width*/);
extern int dialog_gauge(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*percent*/);
extern int dialog_helpfile(const char * /*title*/, const char * /*file*/, int /*height*/, int /*width*/);
extern int dialog_inputbox(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, const char * /*init*/, const int /*password*/);
extern int dialog_menu(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*menu_height*/, int /*item_no*/, char ** /*items*/);
extern int dialog_mixedform(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*form_height*/, int /*item_no*/, char ** /*items*/);
extern int dialog_mixedgauge(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*percent*/, int /*item_no*/, char ** /*items*/);
extern int dialog_msgbox(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*pauseopt*/);
extern int dialog_pause(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*seconds*/);
extern int dialog_prgbox(const char * /*title*/, const char * /*cprompt*/, const char * /*command*/, int /*height*/, int /*width*/, int /*pauseopt*/);
extern int dialog_progressbox(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/);
extern int dialog_rangebox(const char * /*title*/, const char * /*file*/, int /*height*/, int /*width*/, int /*min_value*/, int /*max_value*/, int /*default_value*/);
extern int dialog_tailbox(const char * /*title*/, const char * /*file*/, int /*height*/, int /*width*/, int /*bg_task*/);
extern int dialog_textbox(const char * /*title*/, const char * /*file*/, int /*height*/, int /*width*/);
extern int dialog_timebox(const char * /*title*/, const char * /*subtitle*/, int /*height*/, int /*width*/, int /*hour*/, int /*minute*/, int /*second*/);
extern int dialog_treeview(const char * /*title*/, const char * /*subtitle*/, int /*height*/, int /*width*/, int /*list_height*/, int /*item_no*/, char ** /*items*/, int /*flag*/);
extern int dialog_yesno(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/);

/* some widgets have alternate entrypoints, to allow list manipulation */
typedef struct {
    char *name;
    char *text;
    char *help;
    int state;
} DIALOG_LISTITEM;

typedef struct {
    unsigned type;		/* the field type (0=input, 1=password) */
    char *name;			/* the field label */
    int name_len;		/* ...its length */
    int name_y;			/* ...its y-ordinate */
    int name_x;			/* ...its x-ordinate */
    bool name_free;		/* ...true if .name can be freed */
    char *text;			/* the field contents */
    int text_len;		/* ...its length on the screen */
    int text_y;			/* ...its y-ordinate */
    int text_x;			/* ...its x-ordinate */
    int text_flen;		/* ...its length on screen, or 0 if no input allowed */
    int text_ilen;		/* ...its limit on amount to be entered */
    bool text_free;		/* ...true if .text can be freed */
    char *help;			/* help-message, if any */
    bool help_free;		/* ...true if .help can be freed */
} DIALOG_FORMITEM;

typedef	int (DIALOG_INPUTMENU) (DIALOG_LISTITEM * /*items*/, int /*current*/, char * /*newtext*/);

extern int dlg_checklist(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*list_height*/, int /*item_no*/, DIALOG_LISTITEM * /*items*/, const char * /*states*/, int /*flag*/, int * /*current_item*/);
extern int dlg_form(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*form_height*/, int /*item_no*/, DIALOG_FORMITEM * /*items*/, int * /*current_item*/);
extern int dlg_menu(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*menu_height*/, int /*item_no*/, DIALOG_LISTITEM * /*items*/, int * /*current_item*/, DIALOG_INPUTMENU /*rename_menu*/);
extern int dlg_progressbox(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*pauseopt*/, FILE * /* fp */);

/* argv.c */
extern char ** dlg_string_to_argv(char * /* blob */);
extern int dlg_count_argv(char ** /* argv */);
extern int dlg_eat_argv(int * /* argcp */, char *** /* argvp */, int /* start */, int /* count */);

/* arrows.c */
extern void dlg_draw_arrows(WINDOW * /*dialog*/, int /*top_arrow*/, int /*bottom_arrow*/, int /*x*/, int /*top*/, int /*bottom*/);
extern void dlg_draw_arrows2(WINDOW * /*dialog*/, int /*top_arrow*/, int /*bottom_arrow*/, int /*x*/, int /*top*/, int /*bottom*/, chtype /*attr*/, chtype /*borderattr*/);
extern void dlg_draw_helpline(WINDOW * /*dialog*/, bool /*decorations*/);
extern void dlg_draw_scrollbar(WINDOW * /*dialog*/, long /* first_data */, long /* this_data */, long /* next_data */, long /* total_data */, int /* left */, int /* right */, int /*top*/, int /*bottom*/, chtype /*attr*/, chtype /*borderattr*/);

/* buildlist.c */
extern int dlg_buildlist(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*list_height*/, int /*item_no*/, DIALOG_LISTITEM * /*items*/, const char * /*states*/, int /*order_mode*/, int * /*current_item*/);

/* buttons.c */
extern const char ** dlg_exit_label(void);
extern const char ** dlg_ok_label(void);
extern const char ** dlg_ok_labels(void);
extern const char ** dlg_yes_labels(void);
extern int dlg_button_count(const char ** /*labels*/);
extern int dlg_button_to_char(const char * /*label*/);
extern int dlg_button_x_step(const char ** /*labels*/, int /*limit*/, int * /*gap*/, int * /*margin*/, int * /*step*/);
extern int dlg_char_to_button(int /*ch*/, const char ** /*labels*/);
extern int dlg_exit_buttoncode(int /*button*/);
extern int dlg_match_char(int /*ch*/, const char * /*string*/);
extern int dlg_next_button(const char ** /*labels*/, int /*button*/);
extern int dlg_next_ok_buttonindex(int /*current*/, int /*extra*/);
extern int dlg_ok_buttoncode(int /*button*/);
extern int dlg_prev_button(const char ** /*labels*/, int /*button*/);
extern int dlg_prev_ok_buttonindex(int /*current*/, int /*extra*/);
extern int dlg_yes_buttoncode(int /*button*/);
extern void dlg_button_layout(const char ** /*labels*/, int * /*limit*/);
extern void dlg_button_sizes(const char ** /*labels*/, int /*vertical*/, int * /*longest*/, int * /*length*/);
extern void dlg_draw_buttons(WINDOW * /*win*/, int /*y*/, int /*x*/, const char ** /*labels*/, int /*selected*/, int /*vertical*/, int /*limit*/);

/* columns.c */
extern void dlg_align_columns(char ** /* target */, int  /* per_row */, int /* num_rows */);
extern void dlg_free_columns(char ** /* target */, int  /* per_row */, int /* num_rows */);

/* editbox.c */
extern int dlg_editbox(const char * /*title*/, char *** /*list*/, int * /*rows*/, int /*height*/, int /*width*/);

/* formbox.c */
extern int dlg_default_formitem(DIALOG_FORMITEM * /*items*/);
extern int dlg_ordinate(const char * /*s*/);
extern void dlg_free_formitems(DIALOG_FORMITEM * /*items*/);

/* guage.c */
extern void * dlg_allocate_gauge(const char * /* title */, const char * /* cprompt */, int /* height */, int /* width */, int /* percent */);
extern void * dlg_reallocate_gauge(void * /* objptr */, const char * /* title */, const char * /* cprompt */, int /* height */, int /* width */, int /* percent */);
extern void dlg_free_gauge(void * /* objptr */);
extern void dlg_update_gauge(void * /* objptr */, int /* percent */);

/* inputstr.c */
extern bool dlg_edit_string(char * /*string*/, int * /*offset*/, int /*key*/, int /*fkey*/, bool /*force*/);
extern const int * dlg_index_columns(const char * /*string*/);
extern const int * dlg_index_wchars(const char * /*string*/);
extern int dlg_count_columns(const char * /*string*/);
extern int dlg_count_wchars(const char * /*string*/);
extern int dlg_edit_offset(char * /*string*/, int /*offset*/, int /*x_last*/);
extern int dlg_find_index(const int * /*list*/, int  /*limit*/, int /*to_find*/);
extern int dlg_limit_columns(const char * /*string*/, int /*limit*/, int /*offset*/);
extern void dlg_finish_string(const char * /* string */);
extern void dlg_show_string(WINDOW * /*win*/, const char * /*string*/, int /*offset*/, chtype /*attr*/, int /*y_base*/, int /*x_base*/, int /*x_last*/, bool /*hidden*/, bool /*force*/);

/* menubox.c */
extern int dlg_dummy_menutext(DIALOG_LISTITEM * /*items*/, int /*current*/, char * /*newtext*/);
extern int dlg_renamed_menutext(DIALOG_LISTITEM * /*items*/, int /*current*/, char * /*newtext*/);

/* prgbox.c */
extern FILE * dlg_popen(const char * /*command */, const char * /*type */);

/* rc.c */
#ifdef HAVE_RC_FILE
extern int dlg_parse_rc(void);
extern void dlg_create_rc(const char * /*filename*/);
#endif

/* treeview.c */
extern int dlg_treeview(const char * /*title*/, const char * /*cprompt*/, int /*height*/, int /*width*/, int /*list_height*/, int /*item_no*/, DIALOG_LISTITEM * /*items*/, const char * /*states*/, int * /*depths*/, int /*flag*/, int * /*current_item*/);

/* ttysize.c */
extern int dlg_ttysize(int /* fd */, int * /* height */, int * /* width */);

/* ui_getc.c */
extern int dlg_getc(WINDOW * /*win*/, int * /*fkey*/);
extern int dlg_getc_callbacks(int /*ch*/, int /*fkey*/, int * /*result*/);
extern int dlg_last_getc(void);
extern void dlg_add_last_key(int /*mode*/);
extern void dlg_add_callback(DIALOG_CALLBACK * /*p*/);
extern void dlg_add_callback_ref(DIALOG_CALLBACK ** /*p*/, DIALOG_FREEBACK /* cleanup */);
extern void dlg_flush_getc(void);
extern void dlg_remove_callback(DIALOG_CALLBACK * /*p*/);
extern void dlg_killall_bg(int *retval);

/* util.c */
extern WINDOW * dlg_new_modal_window(WINDOW * /*parent*/, int /*height*/, int /*width*/, int /*y*/, int /*x*/);
extern WINDOW * dlg_new_window(int /*height*/, int /*width*/, int /*y*/, int /*x*/);
extern WINDOW * dlg_sub_window(WINDOW * /*win*/, int /*height*/, int /*width*/, int /*y*/, int /*x*/);
extern bool dlg_need_separator(void);
extern char * dlg_set_result(const char * /*string*/);
extern char * dlg_strclone(const char * /*cprompt*/);
extern char * dlg_strempty(void);
extern chtype dlg_asciibox(chtype /*ch*/);
extern chtype dlg_boxchar(chtype /*ch*/);
extern chtype dlg_get_attrs(WINDOW * /*win*/);
extern const char * dlg_print_line(WINDOW * /*win*/, chtype * /*attr*/, const char * /*prompt*/, int /*lm*/, int /*rm*/, int * /*x*/);
extern int dlg_box_x_ordinate(int /*width*/);
extern int dlg_box_y_ordinate(int /*height*/);
extern int dlg_calc_list_width(int /*item_no*/, DIALOG_LISTITEM * /*items*/);
extern int dlg_calc_listw(int /*item_no*/, char ** /*items*/, int /*group*/);
extern int dlg_check_scrolled(int /* key */, int /* last */, int /* page */, bool * /* show */, int * /* offset */);
extern int dlg_count_real_columns(const char * /*text*/);
extern int dlg_default_item(char ** /*items*/, int /*llen*/);
extern int dlg_default_listitem(DIALOG_LISTITEM * /*items*/);
extern int dlg_defaultno_button(void);
extern int dlg_default_button(void);
extern int dlg_max_input(int /*max_len*/);
extern int dlg_print_scrolled(WINDOW * /* win */, const char * /* prompt */, int /* offset */, int /* height */, int /* width */, int /* pauseopt */);
extern void dlg_add_help_formitem(int * /* result */, char ** /* tag */, DIALOG_FORMITEM * /* item */);
extern void dlg_add_help_listitem(int * /* result */, char ** /* tag */, DIALOG_LISTITEM * /* item */);
extern void dlg_add_quoted(char * /*string*/);
extern void dlg_add_result(const char * /*string*/);
extern void dlg_add_separator(void);
extern void dlg_add_string(char * /*string*/);
extern void dlg_attr_clear(WINDOW * /*win*/, int /*height*/, int /*width*/, chtype /*attr*/);
extern void dlg_auto_size(const char * /*title*/, const char * /*prompt*/, int * /*height*/, int * /*width*/, int /*boxlines*/, int /*mincols*/);
extern void dlg_auto_sizefile(const char * /*title*/, const char * /*file*/, int * /*height*/, int * /*width*/, int /*boxlines*/, int /*mincols*/);
extern void dlg_beeping(void);
extern void dlg_calc_listh(int * /*height*/, int * /*list_height*/, int /*item_no*/);
extern void dlg_clear(void);
extern void dlg_clr_result(void);
extern void dlg_ctl_size(int /*height*/, int /*width*/);
extern void dlg_del_window(WINDOW * /*win*/);
extern void dlg_does_output(void);
extern void dlg_draw_bottom_box(WINDOW * /*win*/);
extern void dlg_draw_bottom_box2(WINDOW * /*win*/, chtype /*on_left*/, chtype /*on_right*/, chtype /*on_inside*/);
extern void dlg_draw_box(WINDOW * /*win*/, int /*y*/, int /*x*/, int /*height*/, int /*width*/, chtype /*boxchar*/, chtype /*borderchar*/);
extern void dlg_draw_box2(WINDOW * /*win*/, int /*y*/, int /*x*/, int /*height*/, int /*width*/, chtype /*boxchar*/, chtype /*borderchar*/, chtype /*borderchar2*/);
extern void dlg_draw_title(WINDOW *win, const char *title);
extern void dlg_exit(int /*code*/) GCC_NORETURN;
extern void dlg_item_help(const char * /*txt*/);
extern void dlg_print_autowrap(WINDOW * /*win*/, const char * /*prompt*/, int /*height*/, int /*width*/);
extern void dlg_print_listitem(WINDOW * /*win*/, const char * /*text*/, int /*climit*/, bool /*first*/, int /*selected*/);
extern void dlg_print_size(int /*height*/, int /*width*/);
extern void dlg_print_text(WINDOW * /*win*/, const char * /*txt*/, int /*len*/, chtype * /*attr*/);
extern void dlg_put_backtitle(void);
extern void dlg_restore_vars(DIALOG_VARS * /* save */);
extern void dlg_save_vars(DIALOG_VARS * /* save */);
extern void dlg_set_focus(WINDOW * /*parent*/, WINDOW * /*win*/);
extern void dlg_tab_correct_str(char * /*prompt*/);
extern void dlg_trim_string(char * /*src*/);
extern void end_dialog(void);
extern void init_dialog(FILE * /*input*/, FILE * /*output*/);

extern void dlg_exiterr(const char *, ...) GCC_NORETURN GCC_PRINTFLIKE(1,2);

#ifdef HAVE_COLOR
extern chtype dlg_color_pair(int /*foreground*/, int /*background*/);
extern int dlg_color_count(void);
extern void dlg_color_setup(void);
extern void dlg_draw_shadow(WINDOW * /*win*/, int /*height*/, int /*width*/, int /*y*/, int /*x*/);
#endif

#ifdef HAVE_STRCASECMP
#define dlg_strcmp(a,b) strcasecmp(a,b)
#else
extern int dlg_strcmp(const char * /*a*/, const char * /*b*/);
#endif

#ifdef HAVE_DLG_TRACE
#define DLG_TRACE(params) dlg_trace_msg params
extern void dlg_trace_msg(const char *fmt, ...) GCC_PRINTFLIKE(1,2);
#define DLG_TRACE2S(name,value) dlg_trace_2s (name,value)
#define DLG_TRACE2N(name,value) dlg_trace_2n (name,value)
extern void dlg_trace_2s(const char * /*name*/, const char * /*value*/);
extern void dlg_trace_2n(const char * /*name*/, int /*value*/);
extern void dlg_trace_win(WINDOW * /*win*/);
extern void dlg_trace_chr(int /*ch*/, int /*fkey*/);
extern void dlg_trace(const char * /*fname*/);
#else
#define DLG_TRACE(params) /* nothing */
#define DLG_TRACE2S(name,value) /* nothing */
#define DLG_TRACE2N(name,value) /* nothing */
#define dlg_trace_win(win) /* nothing */
#define dlg_trace_chr(ch,fkey) /* nothing */
#define dlg_trace(fname) /* nothing */
#endif

#ifdef KEY_RESIZE
extern void dlg_move_window(WINDOW * /*win*/, int /*height*/, int /*width*/, int /*y*/, int /*x*/);
extern void dlg_will_resize(WINDOW * /*win*/);
#endif

/*
 * Normally "enter" means "ok".  Use this macro to handle the explicit
 * check for DLGK_ENTER:
 */
#define dlg_enter_buttoncode(code) (dialog_vars.nook ? DLG_EXIT_OK : dlg_ok_buttoncode(code))

/*
 * The following stuff is needed for mouse support
 */
typedef struct mseRegion {
    int x, y, X, Y, code;
    int mode, step_x, step_y;
    struct mseRegion *next;
} mseRegion;

#if defined(NCURSES_MOUSE_VERSION)

#define	mouse_open() mousemask(BUTTON1_PRESSED, (mmask_t *) 0)
#define	mouse_close() mousemask(0, (mmask_t *) 0)

extern mseRegion * dlg_mouse_mkregion (int /*y*/, int /*x*/, int /*height*/, int /*width*/, int /*code*/);
extern void dlg_mouse_free_regions (void);
extern void dlg_mouse_mkbigregion (int /*y*/, int /*x*/, int /*height*/, int /*width*/, int /*code*/, int /*step_x*/, int /*step_y*/, int /*mode*/);
extern void dlg_mouse_setbase (int /*x*/, int /*y*/);
extern void dlg_mouse_setcode (int /*code*/);

#define USE_MOUSE 1

#else

#define	mouse_open() /*nothing*/
#define	mouse_close() /*nothing*/
#define dlg_mouse_free_regions() /* nothing */
#define	dlg_mouse_mkregion(y, x, height, width, code) /*nothing*/
#define	dlg_mouse_mkbigregion(y, x, height, width, code, step_x, step_y, mode) /*nothing*/
#define	dlg_mouse_setbase(x, y) /*nothing*/
#define	dlg_mouse_setcode(c) /*nothing*/

#define USE_MOUSE 0

#endif

extern mseRegion *dlg_mouse_region (int /*y*/, int /*x*/);
extern mseRegion *dlg_mouse_bigregion (int /*y*/, int /*x*/);
extern int dlg_mouse_wgetch (WINDOW * /*win*/, int * /*fkey*/);
extern int dlg_mouse_wgetch_nowait (WINDOW * /*win*/, int * /*fkey*/);

#define mouse_mkbutton(y,x,len,code) dlg_mouse_mkregion(y,x,1,len,code);

/*
 * This is the base for fictitious keys, which activate
 * the buttons.
 *
 * Mouse-generated keys are the following:
 *   -- the first 32 are used as numbers, in addition to '0'-'9'
 *   -- uppercase chars are used to invoke the button (M_EVENT + 'O')
 */
#define M_EVENT (DLGK_max + 1)

/*
 * The `flag' parameter in checklist is used to select between
 * radiolist and checklist
 */
#define FLAG_CHECK 1
#define FLAG_RADIO 0

/*
 * This is used only for debugging (FIXME: should have a separate header).
 */
#ifdef NO_LEAKS
extern void _dlg_inputstr_leaks(void);
#if defined(NCURSES_VERSION) && defined(HAVE__NC_FREE_AND_EXIT)
extern void _nc_free_and_exit(int);	/* nc_alloc.h normally not installed */
#endif
#endif

#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* DIALOG_H_included */
