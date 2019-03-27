/* TUI support I/O functions.

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

#ifndef TUI_IO_H
#define TUI_IO_H

struct ui_out;

/* Print the string in the curses command window.  */
extern void tui_puts (const char *);

/* Setup the IO for curses or non-curses mode.  */
extern void tui_setup_io (int mode);

/* Initialize the IO for gdb in curses mode.  */
extern void tui_initialize_io (void);

/* Get a character from the command window.  */
extern int tui_getc (FILE *);

/* Readline callback.
   Redisplay the command line with its prompt after readline has
   changed the edited text.  */
extern void tui_redisplay_readline (void);

extern struct ui_out *tui_out;
extern struct ui_out *tui_old_uiout;

extern int key_is_start_sequence (int ch);
extern int key_is_end_sequence (int ch);
extern int key_is_backspace (int ch);
extern int key_is_command_char (int ch);

#endif
