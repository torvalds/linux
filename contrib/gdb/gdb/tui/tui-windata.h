/* Data/register window display.

   Copyright 1998, 1999, 2000, 2001, 2004 Free Software Foundation,
   Inc.

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

#ifndef TUI_WINDATA_H
#define TUI_WINDATA_H

#include "tui/tui-data.h"

extern void tui_erase_data_content (char *);
extern void tui_display_all_data (void);
extern void tui_check_data_values (struct frame_info *);
extern void tui_display_data_from_line (int);
extern int tui_first_data_item_displayed (void);
extern int tui_first_data_element_no_in_line (int);
extern void tui_delete_data_content_windows (void);
extern void tui_refresh_data_win (void);
extern void tui_display_data_from (int, int);
extern void tui_vertical_data_scroll (enum tui_scroll_direction, int);

#endif
