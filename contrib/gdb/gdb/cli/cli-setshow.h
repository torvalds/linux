/* Header file for GDB CLI set and show commands implementation.
   Copyright 2000, 2001 Free Software Foundation, Inc.

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

#if !defined (CLI_SETSHOW_H)
#define CLI_SETSHOW_H 1

struct cmd_list_element;

/* Exported to cli/cli-cmds.c and gdb/top.c */

/* Do a "set" or "show" command.  ARG is NULL if no argument, or the text
   of the argument, and FROM_TTY is nonzero if this command is being entered
   directly by the user (i.e. these are just like any other
   command).  C is the command list element for the command.  */
extern void do_setshow_command (char *arg, int from_tty,
				struct cmd_list_element *c);

/* Exported to cli/cli-cmds.c and gdb/top.c, language.c and valprint.c */

extern void cmd_show_list (struct cmd_list_element *list, int from_tty,
			   char *prefix);

#endif /* !defined (CLI_SETSHOW_H) */
