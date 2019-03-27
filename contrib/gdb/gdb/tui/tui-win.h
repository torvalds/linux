/* TUI window generic functions.

   Copyright 1998, 1999, 2000, 2001, 2002, 2004 Free Software
   Foundation, Inc.

   Contributed by Hewlett-Packard Company.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef TUI_WIN_H
#define TUI_WIN_H

#include "tui/tui-data.h"

struct tui_win_info;

extern void tui_scroll_forward (struct tui_win_info *, int);
extern void tui_scroll_backward (struct tui_win_info *, int);
extern void tui_scroll_left (struct tui_win_info *, int);
extern void tui_scroll_right (struct tui_win_info *, int);
extern void tui_scroll (enum tui_scroll_direction, struct tui_win_info *, int);
extern void tui_set_win_focus_to (struct tui_win_info *);
extern void tui_resize_all (void);
extern void tui_refresh_all_win (void);
extern void tui_sigwinch_handler (int);

extern chtype tui_border_ulcorner;
extern chtype tui_border_urcorner;
extern chtype tui_border_lrcorner;
extern chtype tui_border_llcorner;
extern chtype tui_border_vline;
extern chtype tui_border_hline;
extern int tui_border_attrs;
extern int tui_active_border_attrs;

extern int tui_update_variables (void);

/* Update gdb's knowledge of the terminal size.  */
extern void tui_update_gdb_sizes (void);

/* Create or get the TUI command list.  */
struct cmd_list_element **tui_get_cmd_list ();

#endif
