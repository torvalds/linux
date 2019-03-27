/* MI Command Set - breakpoint and watchpoint commands.
   Copyright 2000, 2001, 2002 Free Software Foundation, Inc.
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

#include "defs.h"
#include "mi-cmds.h"
#include "mi-getopt.h"
#include "ui-out.h"
#include "symtab.h"
#include "source.h"

/* Return to the client the absolute path and line number of the 
   current file being executed. */

enum mi_cmd_result
mi_cmd_file_list_exec_source_file(char *command, char **argv, int argc)
{
  struct symtab_and_line st;
  int optind = 0;
  char *optarg;
  
  if ( !mi_valid_noargs("mi_cmd_file_list_exec_source_file", argc, argv) )
    error ("mi_cmd_file_list_exec_source_file: Usage: No args");

  
  /* Set the default file and line, also get them */
  set_default_source_symtab_and_line();
  st = get_current_source_symtab_and_line();

  /* We should always get a symtab. 
     Apparently, filename does not need to be tested for NULL.
     The documentation in symtab.h suggests it will always be correct */
  if (!st.symtab)
    error ("mi_cmd_file_list_exec_source_file: No symtab");

  /* Extract the fullname if it is not known yet */
  if (st.symtab->fullname == NULL)
    symtab_to_filename (st.symtab);

  /* We may not be able to open the file (not available). */
  if (st.symtab->fullname == NULL)
    error ("mi_cmd_file_list_exec_source_file: File not found");

  /* Print to the user the line, filename and fullname */
  ui_out_field_int (uiout, "line", st.line);
  ui_out_field_string (uiout, "file", st.symtab->filename);
  ui_out_field_string (uiout, "fullname", st.symtab->fullname);

  return MI_CMD_DONE;
}
