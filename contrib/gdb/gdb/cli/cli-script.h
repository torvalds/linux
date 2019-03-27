/* Header file for GDB CLI command implementation library.
   Copyright 2000, 2002 Free Software Foundation, Inc.

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

#if !defined (CLI_SCRIPT_H)
#define CLI_SCRIPT_H 1

struct ui_file;
struct command_line;
struct cmd_list_element;

/* Exported to cli/cli-cmds.c */

extern void script_from_file (FILE *stream, char *file);

extern void document_command (char *, int);

extern void define_command (char *, int);

extern void while_command (char *arg, int from_tty);

extern void if_command (char *arg, int from_tty);

extern void show_user_1 (struct cmd_list_element *c, struct ui_file *stream);

/* Exported to gdb/breakpoint.c */

extern enum command_control_type
	execute_control_command (struct command_line *cmd);

extern void print_command_lines (struct ui_out *,
				 struct command_line *, unsigned int);

extern struct command_line * copy_command_lines (struct command_line *cmds);

struct cleanup *make_cleanup_free_command_lines (struct command_line **arg);

/* Exported to gdb/infrun.c */

extern void execute_user_command (struct cmd_list_element *c, char *args);

#endif /* !defined (CLI_SCRIPT_H) */
