/* TUI display source/assembly window.

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

#ifndef TUI_SOURCEWIN_H
#define TUI_SOURCEWIN_H

#include "tui/tui-data.h"

struct tui_win_info;

/* Update the execution windows to show the active breakpoints.  This
   is called whenever a breakpoint is inserted, removed or has its
   state changed.  */
extern void tui_update_all_breakpoint_info (void);

/* Scan the source window and the breakpoints to update the hasBreak
   information for each line.  Returns 1 if something changed and the
   execution window must be refreshed.  */
extern int tui_update_breakpoint_info (struct tui_win_info * win,
				       int current_only);

/* Function to display the "main" routine.  */
extern void tui_display_main (void);
extern void tui_update_source_window (struct tui_win_info *, struct symtab *,
				      union tui_line_or_address, int);
extern void tui_update_source_window_as_is (struct tui_win_info *,
					    struct symtab *,
					    union tui_line_or_address, int);
extern void tui_update_source_windows_with_addr (CORE_ADDR);
extern void tui_update_source_windows_with_line (struct symtab *, int);
extern void tui_clear_source_content (struct tui_win_info *, int);
extern void tui_erase_source_content (struct tui_win_info *, int);
extern void tui_show_source_content (struct tui_win_info *);
extern void tui_horizontal_source_scroll (struct tui_win_info *,
					  enum tui_scroll_direction, int);
extern enum tui_status tui_set_exec_info_content (struct tui_win_info *);
extern void tui_show_exec_info_content (struct tui_win_info *);
extern void tui_erase_exec_info_content (struct tui_win_info *);
extern void tui_clear_exec_info_content (struct tui_win_info *);
extern void tui_update_exec_info (struct tui_win_info *);

extern void tui_set_is_exec_point_at (union tui_line_or_address,
				      struct tui_win_info *);
extern enum tui_status tui_alloc_source_buffer (struct tui_win_info *);
extern int tui_line_is_displayed (int, struct tui_win_info *, int);
extern int tui_addr_is_displayed (CORE_ADDR, struct tui_win_info *, int);


/* Constant definitions. */
#define        SCROLL_THRESHOLD            2	/* threshold for lazy scroll */

#endif
