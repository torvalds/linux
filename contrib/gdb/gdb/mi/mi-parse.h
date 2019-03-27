/* MI Command Set - MI Command Parser.
   Copyright 2000 Free Software Foundation, Inc.
   Contributed by Cygnus Solutions (a Red Hat company).

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

#ifndef MI_PARSE_H
#define MI_PARSE_H

/* MI parser */

enum mi_command_type
  {
    MI_COMMAND, CLI_COMMAND
  };

struct mi_parse
  {
    enum mi_command_type op;
    char *command;
    char *token;
    const struct mi_cmd *cmd;
    char *args;
    char **argv;
    int argc;
  };

/* Attempts to parse CMD returning a ``struct mi_command''.  If CMD is
   invalid, an error mesage is reported (MI format) and NULL is
   returned. For a CLI_COMMAND, COMMAND, TOKEN and OP are initialized.
   For an MI_COMMAND COMMAND, TOKEN, ARGS and OP are
   initialized. Un-initialized fields are zero. */

extern struct mi_parse *mi_parse (char *cmd);

/* Free a command returned by mi_parse_command. */

extern void mi_parse_free (struct mi_parse *cmd);

#endif
