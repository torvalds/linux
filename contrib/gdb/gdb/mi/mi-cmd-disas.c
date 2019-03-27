/* MI Command Set - disassemble commands.
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
#include "target.h"
#include "value.h"
#include "mi-cmds.h"
#include "mi-getopt.h"
#include "gdb_string.h"
#include "ui-out.h"
#include "disasm.h"

/* The arguments to be passed on the command line and parsed here are:

   either:

   START-ADDRESS: address to start the disassembly at.
   END-ADDRESS: address to end the disassembly at.

   or:

   FILENAME: The name of the file where we want disassemble from.
   LINE: The line around which we want to disassemble. It will
   disassemble the function that contins that line.
   HOW_MANY: Number of disassembly lines to display. In mixed mode, it
   is the number of disassembly lines only, not counting the source
   lines.  

   always required:

   MODE: 0 or 1 for disassembly only, or mixed source and disassembly,
   respectively. */
enum mi_cmd_result
mi_cmd_disassemble (char *command, char **argv, int argc)
{
  enum mi_cmd_result retval;
  CORE_ADDR start;

  int mixed_source_and_assembly;
  struct symtab *s;

  /* Which options have we processed ... */
  int file_seen = 0;
  int line_seen = 0;
  int num_seen = 0;
  int start_seen = 0;
  int end_seen = 0;

  /* ... and their corresponding value. */
  char *file_string = NULL;
  int line_num = -1;
  int how_many = -1;
  CORE_ADDR low = 0;
  CORE_ADDR high = 0;

  /* Options processing stuff. */
  int optind = 0;
  char *optarg;
  enum opt
  {
    FILE_OPT, LINE_OPT, NUM_OPT, START_OPT, END_OPT
  };
  static struct mi_opt opts[] = {
    {"f", FILE_OPT, 1},
    {"l", LINE_OPT, 1},
    {"n", NUM_OPT, 1},
    {"s", START_OPT, 1},
    {"e", END_OPT, 1},
    0
  };

  /* Get the options with their arguments. Keep track of what we
     encountered. */
  while (1)
    {
      int opt = mi_getopt ("mi_cmd_disassemble", argc, argv, opts,
			   &optind, &optarg);
      if (opt < 0)
	break;
      switch ((enum opt) opt)
	{
	case FILE_OPT:
	  file_string = xstrdup (optarg);
	  file_seen = 1;
	  break;
	case LINE_OPT:
	  line_num = atoi (optarg);
	  line_seen = 1;
	  break;
	case NUM_OPT:
	  how_many = atoi (optarg);
	  num_seen = 1;
	  break;
	case START_OPT:
	  low = parse_and_eval_address (optarg);
	  start_seen = 1;
	  break;
	case END_OPT:
	  high = parse_and_eval_address (optarg);
	  end_seen = 1;
	  break;
	}
    }
  argv += optind;
  argc -= optind;

  /* Allow only filename + linenum (with how_many which is not
     required) OR start_addr + and_addr */

  if (!((line_seen && file_seen && num_seen && !start_seen && !end_seen)
	|| (line_seen && file_seen && !num_seen && !start_seen && !end_seen)
	|| (!line_seen && !file_seen && !num_seen && start_seen && end_seen)))
    error
      ("mi_cmd_disassemble: Usage: ( [-f filename -l linenum [-n howmany]] | [-s startaddr -e endaddr]) [--] mixed_mode.");

  if (argc != 1)
    error
      ("mi_cmd_disassemble: Usage: [-f filename -l linenum [-n howmany]] [-s startaddr -e endaddr] [--] mixed_mode.");

  mixed_source_and_assembly = atoi (argv[0]);
  if ((mixed_source_and_assembly != 0) && (mixed_source_and_assembly != 1))
    error ("mi_cmd_disassemble: Mixed_mode argument must be 0 or 1.");


  /* We must get the function beginning and end where line_num is
     contained. */

  if (line_seen && file_seen)
    {
      s = lookup_symtab (file_string);
      if (s == NULL)
	error ("mi_cmd_disassemble: Invalid filename.");
      if (!find_line_pc (s, line_num, &start))
	error ("mi_cmd_disassemble: Invalid line number");
      if (find_pc_partial_function (start, NULL, &low, &high) == 0)
	error ("mi_cmd_disassemble: No function contains specified address");
    }

  gdb_disassembly (uiout,
  		   file_string,
		   line_num,
		   mixed_source_and_assembly, how_many, low, high);

  return MI_CMD_DONE;
}
