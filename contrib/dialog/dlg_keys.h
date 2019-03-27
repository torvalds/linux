/*
 *  $Id: dlg_keys.h,v 1.36 2016/08/28 21:23:17 tom Exp $
 *
 *  dlg_keys.h -- runtime binding support for dialog
 *
 *  Copyright 2005-2012,2016 Thomas E.  Dickey
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

#ifndef DLG_KEYS_H_included
#define DLG_KEYS_H_included 1
/* *INDENT-OFF* */

#include <dialog.h>

#ifdef USE_WIDE_CURSES
#include <wctype.h>
#define dlg_toupper(ch) towupper((wint_t)ch)
#define dlg_isupper(ch) iswupper((wint_t)ch)
#else
#define dlg_toupper(ch) (((ch) > 0 && (ch) <= 255) ? toupper(ch) : (ch))
#define dlg_isupper(ch) (isalpha(ch) && isupper(ch))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int is_function_key;
    int	curses_key;
    int dialog_key;
} DLG_KEYS_BINDING;

#define DLG_KEYS_DATA(dialog, curses)  { (curses) >= KEY_MIN, curses, dialog }

#define END_KEYS_BINDING { -1, 0, 0 }

/*
 * Define dialog's internal function-keys past the range used by curses.
 */
typedef enum {
    DLGK_MIN = KEY_MAX + 1,
    /* predefined buttons */
    DLGK_OK,
    DLGK_CANCEL,
    DLGK_EXTRA,
    DLGK_HELP,
    DLGK_ESC,
    /* moving from screen to screen (pages) */
    DLGK_PAGE_FIRST,
    DLGK_PAGE_LAST,
    DLGK_PAGE_NEXT,
    DLGK_PAGE_PREV,
    /* moving within a list */
    DLGK_ITEM_FIRST,
    DLGK_ITEM_LAST,
    DLGK_ITEM_NEXT,
    DLGK_ITEM_PREV,
    /* moving from field to field (or buttons) */
    DLGK_FIELD_FIRST,
    DLGK_FIELD_LAST,
    DLGK_FIELD_NEXT,
    DLGK_FIELD_PREV,
    /* moving from form-field to form-field (or buttons) */
    DLGK_FORM_FIRST,
    DLGK_FORM_LAST,
    DLGK_FORM_NEXT,
    DLGK_FORM_PREV,
    /* moving within a grid */
    DLGK_GRID_UP,
    DLGK_GRID_DOWN,
    DLGK_GRID_LEFT,
    DLGK_GRID_RIGHT,
    /* delete */
    DLGK_DELETE_LEFT,
    DLGK_DELETE_RIGHT,
    DLGK_DELETE_ALL,
    /* special */
    DLGK_ENTER,
    DLGK_BEGIN,
    DLGK_FINAL,
    DLGK_SELECT,
    DLGK_HELPFILE,
    DLGK_TRACE,
    DLGK_TOGGLE
} DLG_KEYS_ENUM;

#define is_DLGK_MOUSE(code)	((code) >= M_EVENT)
#define DLGK_MOUSE(code)	((code) + M_EVENT)

#define HELPKEY_BINDINGS \
	DLG_KEYS_DATA( DLGK_HELPFILE,	   CHR_HELP ), \
	DLG_KEYS_DATA( DLGK_HELPFILE,	   KEY_F(1) ), \
	DLG_KEYS_DATA( DLGK_HELPFILE,	   KEY_HELP )

#define ENTERKEY_BINDINGS \
	DLG_KEYS_DATA( DLGK_ENTER,	   '\n' ), \
	DLG_KEYS_DATA( DLGK_ENTER,	   '\r' ), \
	DLG_KEYS_DATA( DLGK_ENTER,	   KEY_ENTER )

/* ^U == 21 */
#define INPUTSTR_BINDINGS \
	DLG_KEYS_DATA( DLGK_BEGIN,	   KEY_HOME ), \
	DLG_KEYS_DATA( DLGK_DELETE_ALL,    CHR_KILL ), \
	DLG_KEYS_DATA( DLGK_DELETE_LEFT,   CHR_BACKSPACE ), \
	DLG_KEYS_DATA( DLGK_DELETE_LEFT,   KEY_BACKSPACE ), \
	DLG_KEYS_DATA( DLGK_DELETE_RIGHT,  CHR_DELETE ), \
	DLG_KEYS_DATA( DLGK_DELETE_RIGHT,  KEY_DC ), \
	DLG_KEYS_DATA( DLGK_FINAL,	   KEY_END ), \
	DLG_KEYS_DATA( DLGK_GRID_LEFT,	   KEY_LEFT ), \
	DLG_KEYS_DATA( DLGK_GRID_RIGHT,	   KEY_RIGHT )

#define SCROLL_FKEY_BINDINGS \
	DLG_KEYS_DATA( DLGK_GRID_DOWN,	KEY_DOWN ), \
	DLG_KEYS_DATA( DLGK_GRID_UP,	KEY_UP ), \
	DLG_KEYS_DATA( DLGK_PAGE_FIRST,	KEY_HOME ), \
	DLG_KEYS_DATA( DLGK_PAGE_LAST,	KEY_END ), \
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,	KEY_NPAGE ), \
	DLG_KEYS_DATA( DLGK_PAGE_PREV,	KEY_PPAGE )

#define SCROLLKEY_BINDINGS \
	SCROLL_FKEY_BINDINGS, \
	DLG_KEYS_DATA( DLGK_GRID_DOWN,	'J' ), \
	DLG_KEYS_DATA( DLGK_GRID_DOWN,	'j' ), \
	DLG_KEYS_DATA( DLGK_GRID_UP,	'K' ), \
	DLG_KEYS_DATA( DLGK_GRID_UP,	'k' ), \
	DLG_KEYS_DATA( DLGK_PAGE_FIRST,	'g' ), \
	DLG_KEYS_DATA( DLGK_PAGE_LAST,	'G' ), \
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,	'F' ), \
	DLG_KEYS_DATA( DLGK_PAGE_NEXT,	'f' ), \
	DLG_KEYS_DATA( DLGK_PAGE_PREV,	'B' ), \
	DLG_KEYS_DATA( DLGK_PAGE_PREV,	'b' )

#define TRAVERSE_BINDINGS \
	DLG_KEYS_DATA( DLGK_ENTER,	CHR_SPACE ), \
	DLG_KEYS_DATA( DLGK_FIELD_NEXT,	KEY_DOWN ), \
	DLG_KEYS_DATA( DLGK_FIELD_NEXT, KEY_RIGHT ), \
	DLG_KEYS_DATA( DLGK_FIELD_NEXT, TAB ), \
	DLG_KEYS_DATA( DLGK_FIELD_PREV,	KEY_UP ), \
	DLG_KEYS_DATA( DLGK_FIELD_PREV, KEY_BTAB ), \
	DLG_KEYS_DATA( DLGK_FIELD_PREV, KEY_LEFT )

#define TOGGLEKEY_BINDINGS \
	DLG_KEYS_DATA( DLGK_TOGGLE,	CHR_SPACE )

extern int dlg_lookup_key(WINDOW * /*win*/, int /*curses_key*/, int * /*dialog_key*/);
extern int dlg_result_key(int /*dialog_key*/, int /*fkey*/, int * /*resultp*/);
extern void dlg_register_buttons(WINDOW * /*win*/, const char * /*name*/, const char ** /*buttons*/);
extern void dlg_register_window(WINDOW * /*win*/, const char * /*name*/, DLG_KEYS_BINDING * /*binding*/);
extern void dlg_unregister_window(WINDOW * /*win*/);

#ifdef HAVE_RC_FILE
extern int dlg_parse_bindkey(char * /*params*/);
extern void dlg_dump_keys(FILE * /*fp*/);
extern void dlg_dump_window_keys(FILE * /*fp*/, WINDOW * /*win*/);
#endif

#ifdef __cplusplus
}
#endif
/* *INDENT-ON* */

#endif /* DLG_KEYS_H_included */
