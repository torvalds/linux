/* MI Command Set - stack commands.
   Copyright 2000, 2002, 2003, 2004 Free Software Foundation, Inc.
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
#include "frame.h"
#include "value.h"
#include "mi-cmds.h"
#include "ui-out.h"
#include "symtab.h"
#include "block.h"
#include "stack.h"
#include "dictionary.h"
#include "gdb_string.h"

static void list_args_or_locals (int locals, int values, struct frame_info *fi);

/* Print a list of the stack frames. Args can be none, in which case
   we want to print the whole backtrace, or a pair of numbers
   specifying the frame numbers at which to start and stop the
   display. If the two numbers are equal, a single frame will be
   displayed. */
enum mi_cmd_result
mi_cmd_stack_list_frames (char *command, char **argv, int argc)
{
  int frame_low;
  int frame_high;
  int i;
  struct cleanup *cleanup_stack;
  struct frame_info *fi;

  if (!target_has_stack)
    error ("mi_cmd_stack_list_frames: No stack.");

  if (argc > 2 || argc == 1)
    error ("mi_cmd_stack_list_frames: Usage: [FRAME_LOW FRAME_HIGH]");

  if (argc == 2)
    {
      frame_low = atoi (argv[0]);
      frame_high = atoi (argv[1]);
    }
  else
    {
      /* Called with no arguments, it means we want the whole
         backtrace. */
      frame_low = -1;
      frame_high = -1;
    }

  /* Let's position fi on the frame at which to start the
     display. Could be the innermost frame if the whole stack needs
     displaying, or if frame_low is 0. */
  for (i = 0, fi = get_current_frame ();
       fi && i < frame_low;
       i++, fi = get_prev_frame (fi));

  if (fi == NULL)
    error ("mi_cmd_stack_list_frames: Not enough frames in stack.");

  cleanup_stack = make_cleanup_ui_out_list_begin_end (uiout, "stack");

  /* Now let;s print the frames up to frame_high, or until there are
     frames in the stack. */
  for (;
       fi && (i <= frame_high || frame_high == -1);
       i++, fi = get_prev_frame (fi))
    {
      QUIT;
      /* level == i: always print the level 'i'
         source == LOC_AND_ADDRESS: print the location and the address 
         always, even for level 0.
         args == 0: don't print the arguments. */
      print_frame_info (fi /* frame info */ ,
			i /* level */ ,
			LOC_AND_ADDRESS /* source */ ,
			0 /* args */ );
    }

  do_cleanups (cleanup_stack);
  if (i < frame_high)
    error ("mi_cmd_stack_list_frames: Not enough frames in stack.");

  return MI_CMD_DONE;
}

enum mi_cmd_result
mi_cmd_stack_info_depth (char *command, char **argv, int argc)
{
  int frame_high;
  int i;
  struct frame_info *fi;

  if (!target_has_stack)
    error ("mi_cmd_stack_info_depth: No stack.");

  if (argc > 1)
    error ("mi_cmd_stack_info_depth: Usage: [MAX_DEPTH]");

  if (argc == 1)
    frame_high = atoi (argv[0]);
  else
    /* Called with no arguments, it means we want the real depth of
       the stack. */
    frame_high = -1;

  for (i = 0, fi = get_current_frame ();
       fi && (i < frame_high || frame_high == -1);
       i++, fi = get_prev_frame (fi))
    QUIT;

  ui_out_field_int (uiout, "depth", i);

  return MI_CMD_DONE;
}

/* Print a list of the locals for the current frame. With argument of
   0, print only the names, with argument of 1 print also the
   values. */
enum mi_cmd_result
mi_cmd_stack_list_locals (char *command, char **argv, int argc)
{
  struct frame_info *frame;
  enum print_values print_values;

  if (argc != 1)
    error ("mi_cmd_stack_list_locals: Usage: PRINT_VALUES");

   frame = get_selected_frame ();

   if (strcmp (argv[0], "0") == 0
       || strcmp (argv[0], "--no-values") == 0)
     print_values = PRINT_NO_VALUES;
   else if (strcmp (argv[0], "1") == 0
	    || strcmp (argv[0], "--all-values") == 0)
     print_values = PRINT_ALL_VALUES;
   else if (strcmp (argv[0], "2") == 0
	    || strcmp (argv[0], "--simple-values") == 0)
     print_values = PRINT_SIMPLE_VALUES;
   else
     error ("Unknown value for PRINT_VALUES: must be: 0 or \"--no-values\", 1 or \"--all-values\", 2 or \"--simple-values\"");
  list_args_or_locals (1, print_values, frame);
  return MI_CMD_DONE;
}

/* Print a list of the arguments for the current frame. With argument
   of 0, print only the names, with argument of 1 print also the
   values. */
enum mi_cmd_result
mi_cmd_stack_list_args (char *command, char **argv, int argc)
{
  int frame_low;
  int frame_high;
  int i;
  struct frame_info *fi;
  struct cleanup *cleanup_stack_args;

  if (argc < 1 || argc > 3 || argc == 2)
    error ("mi_cmd_stack_list_args: Usage: PRINT_VALUES [FRAME_LOW FRAME_HIGH]");

  if (argc == 3)
    {
      frame_low = atoi (argv[1]);
      frame_high = atoi (argv[2]);
    }
  else
    {
      /* Called with no arguments, it means we want args for the whole
         backtrace. */
      frame_low = -1;
      frame_high = -1;
    }

  /* Let's position fi on the frame at which to start the
     display. Could be the innermost frame if the whole stack needs
     displaying, or if frame_low is 0. */
  for (i = 0, fi = get_current_frame ();
       fi && i < frame_low;
       i++, fi = get_prev_frame (fi));

  if (fi == NULL)
    error ("mi_cmd_stack_list_args: Not enough frames in stack.");

  cleanup_stack_args = make_cleanup_ui_out_list_begin_end (uiout, "stack-args");

  /* Now let's print the frames up to frame_high, or until there are
     frames in the stack. */
  for (;
       fi && (i <= frame_high || frame_high == -1);
       i++, fi = get_prev_frame (fi))
    {
      struct cleanup *cleanup_frame;
      QUIT;
      cleanup_frame = make_cleanup_ui_out_tuple_begin_end (uiout, "frame");
      ui_out_field_int (uiout, "level", i);
      list_args_or_locals (0, atoi (argv[0]), fi);
      do_cleanups (cleanup_frame);
    }

  do_cleanups (cleanup_stack_args);
  if (i < frame_high)
    error ("mi_cmd_stack_list_args: Not enough frames in stack.");

  return MI_CMD_DONE;
}

/* Print a list of the locals or the arguments for the currently
   selected frame.  If the argument passed is 0, printonly the names
   of the variables, if an argument of 1 is passed, print the values
   as well. */
static void
list_args_or_locals (int locals, int values, struct frame_info *fi)
{
  struct block *block;
  struct symbol *sym;
  struct dict_iterator iter;
  int nsyms;
  struct cleanup *cleanup_list;
  static struct ui_stream *stb = NULL;
  struct type *type;

  stb = ui_out_stream_new (uiout);

  block = get_frame_block (fi, 0);

  cleanup_list = make_cleanup_ui_out_list_begin_end (uiout, locals ? "locals" : "args");

  while (block != 0)
    {
      ALL_BLOCK_SYMBOLS (block, iter, sym)
	{
          int print_me = 0;

	  switch (SYMBOL_CLASS (sym))
	    {
	    default:
	    case LOC_UNDEF:	/* catches errors        */
	    case LOC_CONST:	/* constant              */
	    case LOC_TYPEDEF:	/* local typedef         */
	    case LOC_LABEL:	/* local label           */
	    case LOC_BLOCK:	/* local function        */
	    case LOC_CONST_BYTES:	/* loc. byte seq.        */
	    case LOC_UNRESOLVED:	/* unresolved static     */
	    case LOC_OPTIMIZED_OUT:	/* optimized out         */
	      print_me = 0;
	      break;

	    case LOC_ARG:	/* argument              */
	    case LOC_REF_ARG:	/* reference arg         */
	    case LOC_REGPARM:	/* register arg          */
	    case LOC_REGPARM_ADDR:	/* indirect register arg */
	    case LOC_LOCAL_ARG:	/* stack arg             */
	    case LOC_BASEREG_ARG:	/* basereg arg           */
	    case LOC_COMPUTED_ARG:	/* arg with computed location */
	      if (!locals)
		print_me = 1;
	      break;

	    case LOC_LOCAL:	/* stack local           */
	    case LOC_BASEREG:	/* basereg local         */
	    case LOC_STATIC:	/* static                */
	    case LOC_REGISTER:	/* register              */
	    case LOC_COMPUTED:	/* computed location     */
	      if (locals)
		print_me = 1;
	      break;
	    }
	  if (print_me)
	    {
	      struct cleanup *cleanup_tuple = NULL;
	      struct symbol *sym2;
	      if (values != PRINT_NO_VALUES)
		cleanup_tuple =
		  make_cleanup_ui_out_tuple_begin_end (uiout, NULL);
	      ui_out_field_string (uiout, "name", SYMBOL_PRINT_NAME (sym));

	      if (!locals)
		sym2 = lookup_symbol (SYMBOL_NATURAL_NAME (sym),
				      block, VAR_DOMAIN,
				      (int *) NULL,
				      (struct symtab **) NULL);
	      else
		    sym2 = sym;
	      switch (values)
		{
		case PRINT_SIMPLE_VALUES:
		  type = check_typedef (sym2->type);
		  type_print (sym2->type, "", stb->stream, -1);
		  ui_out_field_stream (uiout, "type", stb);
		  if (TYPE_CODE (type) != TYPE_CODE_ARRAY
		      && TYPE_CODE (type) != TYPE_CODE_STRUCT
		      && TYPE_CODE (type) != TYPE_CODE_UNION)
		    {
		      print_variable_value (sym2, fi, stb->stream);
		      ui_out_field_stream (uiout, "value", stb);
		    }
		  do_cleanups (cleanup_tuple);
		  break;
		case PRINT_ALL_VALUES:
		  print_variable_value (sym2, fi, stb->stream);
		  ui_out_field_stream (uiout, "value", stb);
		  do_cleanups (cleanup_tuple);
		  break;
		}
	    }
	}
      if (BLOCK_FUNCTION (block))
	break;
      else
	block = BLOCK_SUPERBLOCK (block);
    }
  do_cleanups (cleanup_list);
  ui_out_stream_delete (stb);
}

enum mi_cmd_result
mi_cmd_stack_select_frame (char *command, char **argv, int argc)
{
  if (!target_has_stack)
    error ("mi_cmd_stack_select_frame: No stack.");

  if (argc > 1)
    error ("mi_cmd_stack_select_frame: Usage: [FRAME_SPEC]");

  /* with no args, don't change frame */
  if (argc == 0)
    select_frame_command (0, 1 /* not used */ );
  else
    select_frame_command (argv[0], 1 /* not used */ );
  return MI_CMD_DONE;
}
