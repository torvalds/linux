/* External/Public TUI Header File.

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

#ifndef TUI_H
#define TUI_H

struct ui_file;

extern void strcat_to_buf (char *, int, const char *);

/* Types of error returns.  */
enum tui_status
{
  TUI_SUCCESS,
  TUI_FAILURE
};

/* Types of windows */
enum tui_win_type
{
  SRC_WIN = 0,
  DISASSEM_WIN,
  DATA_WIN,
  CMD_WIN,
  /* This must ALWAYS be AFTER the major windows last.  */
  MAX_MAJOR_WINDOWS,
  /* Auxillary windows.  */
  LOCATOR_WIN,
  EXEC_INFO_WIN,
  DATA_ITEM_WIN,
  /* This must ALWAYS be next to last.  */
  MAX_WINDOWS,
  UNDEFINED_WIN		/* LAST */
};

/* GENERAL TUI FUNCTIONS */
/* tui.c */
extern CORE_ADDR tui_get_low_disassembly_address (CORE_ADDR, CORE_ADDR);
extern void tui_show_assembly (CORE_ADDR addr);
extern int tui_is_window_visible (enum tui_win_type type);
extern int tui_get_command_dimension (int *width, int *height);

/* Initialize readline and configure the keymap for the switching
   key shortcut.  */
extern void tui_initialize_readline (void);

/* Enter in the tui mode (curses).  */
extern void tui_enable (void);

/* Leave the tui mode.  */
extern void tui_disable (void);

enum tui_key_mode
{
  /* Plain command mode to enter gdb commands.  */
  TUI_COMMAND_MODE,

  /* SingleKey mode with some keys bound to gdb commands.  */
  TUI_SINGLE_KEY_MODE,

  /* Read/edit one command and return to SingleKey after it's processed.  */
  TUI_ONE_COMMAND_MODE
};

extern enum tui_key_mode tui_current_key_mode;

/* Change the TUI key mode by installing the appropriate readline keymap.  */
extern void tui_set_key_mode (enum tui_key_mode mode);

extern int tui_active;

extern void tui_show_source (const char *file, int line);

extern struct ui_out *tui_out_new (struct ui_file *stream);

/* tui-layout.c */
extern enum tui_status tui_set_layout_for_display_command (const char *name);

#endif
