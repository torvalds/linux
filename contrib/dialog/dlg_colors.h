/*
 *  $Id: dlg_colors.h,v 1.17 2011/10/14 21:19:59 tom Exp $
 *
 *  colors.h -- color attribute definitions
 *
 *  Copyright 2000-2007,2011	Thomas E. Dickey
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

#ifndef COLORS_H_included
#define COLORS_H_included 1

#include <dialog.h>

/*
 *   Default color definitions (DLGC means "Dialog Color")
 *
 *   DLGC_FG_xxx = foreground for "xxx"
 *   DLGC_BG_xxx = background for "xxx"
 *   DLGC_HL_xxx = highlight for "xxx"
 */
#define DLGC_FG_SCREEN                 COLOR_CYAN
#define DLGC_BG_SCREEN                 COLOR_BLUE
#define DLGC_HL_SCREEN                 TRUE

#define DLGC_FG_SHADOW                 COLOR_BLACK
#define DLGC_BG_SHADOW                 COLOR_BLACK
#define DLGC_HL_SHADOW                 TRUE

#define DLGC_FG_DIALOG                 COLOR_BLACK
#define DLGC_BG_DIALOG                 COLOR_WHITE
#define DLGC_HL_DIALOG                 FALSE

#define DLGC_FG_TITLE                  COLOR_BLUE
#define DLGC_BG_TITLE                  COLOR_WHITE
#define DLGC_HL_TITLE                  TRUE

#define DLGC_FG_BORDER                 COLOR_WHITE
#define DLGC_BG_BORDER                 COLOR_WHITE
#define DLGC_HL_BORDER                 TRUE

#define DLGC_FG_BORDER2                DLGC_FG_DIALOG
#define DLGC_BG_BORDER2                DLGC_BG_DIALOG
#define DLGC_HL_BORDER2                DLGC_HL_DIALOG

#define DLGC_FG_BUTTON_ACTIVE          COLOR_WHITE
#define DLGC_BG_BUTTON_ACTIVE          COLOR_BLUE
#define DLGC_HL_BUTTON_ACTIVE          TRUE

#define DLGC_FG_BUTTON_INACTIVE        COLOR_BLACK
#define DLGC_BG_BUTTON_INACTIVE        COLOR_WHITE
#define DLGC_HL_BUTTON_INACTIVE        FALSE

#define DLGC_FG_BUTTON_KEY_ACTIVE      COLOR_WHITE
#define DLGC_BG_BUTTON_KEY_ACTIVE      COLOR_BLUE
#define DLGC_HL_BUTTON_KEY_ACTIVE      TRUE

#define DLGC_FG_BUTTON_KEY_INACTIVE    COLOR_RED
#define DLGC_BG_BUTTON_KEY_INACTIVE    COLOR_WHITE
#define DLGC_HL_BUTTON_KEY_INACTIVE    FALSE

#define DLGC_FG_BUTTON_LABEL_ACTIVE    COLOR_YELLOW
#define DLGC_BG_BUTTON_LABEL_ACTIVE    COLOR_BLUE
#define DLGC_HL_BUTTON_LABEL_ACTIVE    TRUE

#define DLGC_FG_BUTTON_LABEL_INACTIVE  COLOR_BLACK
#define DLGC_BG_BUTTON_LABEL_INACTIVE  COLOR_WHITE
#define DLGC_HL_BUTTON_LABEL_INACTIVE  TRUE

#define DLGC_FG_FORM_ITEM_READONLY     COLOR_CYAN
#define DLGC_BG_FORM_ITEM_READONLY     COLOR_WHITE
#define DLGC_HL_FORM_ITEM_READONLY     TRUE

#define DLGC_FG_INPUTBOX               COLOR_BLACK
#define DLGC_BG_INPUTBOX               COLOR_WHITE
#define DLGC_HL_INPUTBOX               FALSE

#define DLGC_FG_INPUTBOX_BORDER        COLOR_BLACK
#define DLGC_BG_INPUTBOX_BORDER        COLOR_WHITE
#define DLGC_HL_INPUTBOX_BORDER        FALSE

#define DLGC_FG_INPUTBOX_BORDER2       DLGC_FG_INPUTBOX
#define DLGC_BG_INPUTBOX_BORDER2       DLGC_BG_INPUTBOX
#define DLGC_HL_INPUTBOX_BORDER2       DLGC_HL_INPUTBOX

#define DLGC_FG_SEARCHBOX              COLOR_BLACK
#define DLGC_BG_SEARCHBOX              COLOR_WHITE
#define DLGC_HL_SEARCHBOX              FALSE

#define DLGC_FG_SEARCHBOX_TITLE        COLOR_BLUE
#define DLGC_BG_SEARCHBOX_TITLE        COLOR_WHITE
#define DLGC_HL_SEARCHBOX_TITLE        TRUE

#define DLGC_FG_SEARCHBOX_BORDER       COLOR_WHITE
#define DLGC_BG_SEARCHBOX_BORDER       COLOR_WHITE
#define DLGC_HL_SEARCHBOX_BORDER       TRUE

#define DLGC_FG_SEARCHBOX_BORDER2      DLGC_FG_SEARCHBOX
#define DLGC_BG_SEARCHBOX_BORDER2      DLGC_BG_SEARCHBOX
#define DLGC_HL_SEARCHBOX_BORDER2      DLGC_HL_SEARCHBOX

#define DLGC_FG_POSITION_INDICATOR     COLOR_BLUE
#define DLGC_BG_POSITION_INDICATOR     COLOR_WHITE
#define DLGC_HL_POSITION_INDICATOR     TRUE

#define DLGC_FG_MENUBOX                COLOR_BLACK
#define DLGC_BG_MENUBOX                COLOR_WHITE
#define DLGC_HL_MENUBOX                FALSE

#define DLGC_FG_MENUBOX_BORDER         COLOR_WHITE
#define DLGC_BG_MENUBOX_BORDER         COLOR_WHITE
#define DLGC_HL_MENUBOX_BORDER         TRUE

#define DLGC_FG_MENUBOX_BORDER2        DLGC_FG_MENUBOX
#define DLGC_BG_MENUBOX_BORDER2        DLGC_BG_MENUBOX
#define DLGC_HL_MENUBOX_BORDER2        DLGC_HL_MENUBOX

#define DLGC_FG_ITEM                   COLOR_BLACK
#define DLGC_BG_ITEM                   COLOR_WHITE
#define DLGC_HL_ITEM                   FALSE

#define DLGC_FG_ITEM_SELECTED          COLOR_WHITE
#define DLGC_BG_ITEM_SELECTED          COLOR_BLUE
#define DLGC_HL_ITEM_SELECTED          TRUE

#define DLGC_FG_TAG                    COLOR_BLUE
#define DLGC_BG_TAG                    COLOR_WHITE
#define DLGC_HL_TAG                    TRUE

#define DLGC_FG_TAG_SELECTED           COLOR_YELLOW
#define DLGC_BG_TAG_SELECTED           COLOR_BLUE
#define DLGC_HL_TAG_SELECTED           TRUE

#define DLGC_FG_TAG_KEY                COLOR_RED
#define DLGC_BG_TAG_KEY                COLOR_WHITE
#define DLGC_HL_TAG_KEY                FALSE

#define DLGC_FG_TAG_KEY_SELECTED       COLOR_RED
#define DLGC_BG_TAG_KEY_SELECTED       COLOR_BLUE
#define DLGC_HL_TAG_KEY_SELECTED       TRUE

#define DLGC_FG_CHECK                  COLOR_BLACK
#define DLGC_BG_CHECK                  COLOR_WHITE
#define DLGC_HL_CHECK                  FALSE

#define DLGC_FG_CHECK_SELECTED         COLOR_WHITE
#define DLGC_BG_CHECK_SELECTED         COLOR_BLUE
#define DLGC_HL_CHECK_SELECTED         TRUE

#define DLGC_FG_UARROW                 COLOR_GREEN
#define DLGC_BG_UARROW                 COLOR_WHITE
#define DLGC_HL_UARROW                 TRUE

#define DLGC_FG_DARROW                 COLOR_GREEN
#define DLGC_BG_DARROW                 COLOR_WHITE
#define DLGC_HL_DARROW                 TRUE

#define DLGC_FG_ITEMHELP               COLOR_WHITE
#define DLGC_BG_ITEMHELP               COLOR_BLACK
#define DLGC_HL_ITEMHELP               FALSE

#define DLGC_FG_FORM_ACTIVE_TEXT       COLOR_WHITE
#define DLGC_BG_FORM_ACTIVE_TEXT       COLOR_BLUE
#define DLGC_HL_FORM_ACTIVE_TEXT       TRUE

#define DLGC_FG_FORM_TEXT              COLOR_WHITE
#define DLGC_BG_FORM_TEXT              COLOR_CYAN
#define DLGC_HL_FORM_TEXT              TRUE

#define DLGC_FG_GAUGE                  COLOR_BLUE
#define DLGC_BG_GAUGE                  COLOR_WHITE
#define DLGC_HL_GAUGE                  TRUE

/* End of default color definitions */

/*
 * Global variables
 */

typedef struct {
    const char *name;
    int value;
} color_names_st;

#endif /* COLORS_H_included */
