/* MI Command Set - symbol commands.
   Copyright 2003 Free Software Foundation, Inc.

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

#include "defs.h"
#include "mi-cmds.h"
#include "symtab.h"
#include "ui-out.h"

/* SYMBOL-LIST-LINES:

   Print the list of all pc addresses and lines of code for
   the provided (full or base) source file name.  The entries
   are sorted in ascending PC order. */

enum mi_cmd_result
mi_cmd_symbol_list_lines (char *command, char **argv, int argc)
{
  char *filename;
  struct symtab *s;
  int i;
  struct cleanup *cleanup_stack, *cleanup_tuple;

  if (argc != 1)
    error ("mi_cmd_symbol_list_lines: Usage: SOURCE_FILENAME");

  filename = argv[0];
  s = lookup_symtab (filename);

  if (s == NULL)
    error ("mi_cmd_symbol_list_lines: Unknown source file name.");

  /* Now, dump the associated line table.  The pc addresses are already
     sorted by increasing values in the symbol table, so no need to
     perform any other sorting. */

  cleanup_stack = make_cleanup_ui_out_list_begin_end (uiout, "lines");

  if (LINETABLE (s) != NULL && LINETABLE (s)->nitems > 0)
    for (i = 0; i < LINETABLE (s)->nitems; i++)
    {
      cleanup_tuple = make_cleanup_ui_out_tuple_begin_end (uiout, NULL);
      ui_out_field_core_addr (uiout, "pc", LINETABLE (s)->item[i].pc);
      ui_out_field_int (uiout, "line", LINETABLE (s)->item[i].line);
      do_cleanups (cleanup_tuple);
    }

  do_cleanups (cleanup_stack);

  return MI_CMD_DONE;
}
