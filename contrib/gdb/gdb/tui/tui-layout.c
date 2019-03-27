/* TUI layout window management.

   Copyright 1998, 1999, 2000, 2001, 2002, 2003, 2004 Free Software
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

#include "defs.h"
#include "command.h"
#include "symtab.h"
#include "frame.h"
#include "source.h"
#include <ctype.h>

#include "tui/tui.h"
#include "tui/tui-data.h"
#include "tui/tui-windata.h"
#include "tui/tui-wingeneral.h"
#include "tui/tui-stack.h"
#include "tui/tui-regs.h"
#include "tui/tui-win.h"
#include "tui/tui-winsource.h"
#include "tui/tui-disasm.h"

#include "gdb_string.h"
#include "gdb_curses.h"

/*******************************
** Static Local Decls
********************************/
static void show_layout (enum tui_layout_type);
static void init_gen_win_info (struct tui_gen_win_info *, enum tui_win_type, int, int, int, int);
static void init_and_make_win (void **, enum tui_win_type, int, int, int, int, int);
static void show_source_or_disasm_and_command (enum tui_layout_type);
static void make_source_or_disasm_window (struct tui_win_info * *, enum tui_win_type, int, int);
static void make_command_window (struct tui_win_info * *, int, int);
static void make_source_window (struct tui_win_info * *, int, int);
static void make_disasm_window (struct tui_win_info * *, int, int);
static void make_data_window (struct tui_win_info * *, int, int);
static void show_source_command (void);
static void show_disasm_command (void);
static void show_source_disasm_command (void);
static void show_data (enum tui_layout_type);
static enum tui_layout_type next_layout (void);
static enum tui_layout_type prev_layout (void);
static void tui_layout_command (char *, int);
static void tui_toggle_layout_command (char *, int);
static void tui_toggle_split_layout_command (char *, int);
static CORE_ADDR extract_display_start_addr (void);
static void tui_handle_xdb_layout (struct tui_layout_def *);


/***************************************
** DEFINITIONS
***************************************/

#define LAYOUT_USAGE     "Usage: layout prev | next | <layout_name> \n"

/* Show the screen layout defined.  */
static void
show_layout (enum tui_layout_type layout)
{
  enum tui_layout_type cur_layout = tui_current_layout ();

  if (layout != cur_layout)
    {
      /*
         ** Since the new layout may cause changes in window size, we
         ** should free the content and reallocate on next display of
         ** source/asm
       */
      tui_free_all_source_wins_content ();
      tui_clear_source_windows ();
      if (layout == SRC_DATA_COMMAND || layout == DISASSEM_DATA_COMMAND)
	{
	  show_data (layout);
	  tui_refresh_all (tui_win_list);
	}
      else
	{
	  /* First make the current layout be invisible */
	  tui_make_all_invisible ();
	  tui_make_invisible (tui_locator_win_info_ptr ());

	  switch (layout)
	    {
	      /* Now show the new layout */
	    case SRC_COMMAND:
	      show_source_command ();
	      tui_add_to_source_windows (TUI_SRC_WIN);
	      break;
	    case DISASSEM_COMMAND:
	      show_disasm_command ();
	      tui_add_to_source_windows (TUI_DISASM_WIN);
	      break;
	    case SRC_DISASSEM_COMMAND:
	      show_source_disasm_command ();
	      tui_add_to_source_windows (TUI_SRC_WIN);
	      tui_add_to_source_windows (TUI_DISASM_WIN);
	      break;
	    default:
	      break;
	    }
	}
    }
}


/* Function to set the layout to SRC_COMMAND, DISASSEM_COMMAND,
   SRC_DISASSEM_COMMAND, SRC_DATA_COMMAND, or DISASSEM_DATA_COMMAND.
   If the layout is SRC_DATA_COMMAND, DISASSEM_DATA_COMMAND, or
   UNDEFINED_LAYOUT, then the data window is populated according to
   regs_display_type.  */
enum tui_status
tui_set_layout (enum tui_layout_type layout_type,
		enum tui_register_display_type regs_display_type)
{
  enum tui_status status = TUI_SUCCESS;

  if (layout_type != UNDEFINED_LAYOUT || regs_display_type != TUI_UNDEFINED_REGS)
    {
      enum tui_layout_type cur_layout = tui_current_layout (), new_layout = UNDEFINED_LAYOUT;
      int regs_populate = FALSE;
      CORE_ADDR addr = extract_display_start_addr ();
      struct tui_win_info * new_win_with_focus = (struct tui_win_info *) NULL;
      struct tui_win_info * win_with_focus = tui_win_with_focus ();
      struct tui_layout_def * layout_def = tui_layout_def ();


      if (layout_type == UNDEFINED_LAYOUT &&
	  regs_display_type != TUI_UNDEFINED_REGS)
	{
	  if (cur_layout == SRC_DISASSEM_COMMAND)
	    new_layout = DISASSEM_DATA_COMMAND;
	  else if (cur_layout == SRC_COMMAND || cur_layout == SRC_DATA_COMMAND)
	    new_layout = SRC_DATA_COMMAND;
	  else if (cur_layout == DISASSEM_COMMAND ||
		   cur_layout == DISASSEM_DATA_COMMAND)
	    new_layout = DISASSEM_DATA_COMMAND;
	}
      else
	new_layout = layout_type;

      regs_populate = (new_layout == SRC_DATA_COMMAND ||
		      new_layout == DISASSEM_DATA_COMMAND ||
		      regs_display_type != TUI_UNDEFINED_REGS);
      if (new_layout != cur_layout || regs_display_type != TUI_UNDEFINED_REGS)
	{
	  if (new_layout != cur_layout)
	    {
	      show_layout (new_layout);
	      /*
	         ** Now determine where focus should be
	       */
	      if (win_with_focus != TUI_CMD_WIN)
		{
		  switch (new_layout)
		    {
		    case SRC_COMMAND:
		      tui_set_win_focus_to (TUI_SRC_WIN);
		      layout_def->display_mode = SRC_WIN;
		      layout_def->split = FALSE;
		      break;
		    case DISASSEM_COMMAND:
		      /* the previous layout was not showing
		         ** code. this can happen if there is no
		         ** source available:
		         ** 1. if the source file is in another dir OR
		         ** 2. if target was compiled without -g
		         ** We still want to show the assembly though!
		       */
		      addr = tui_get_begin_asm_address ();
		      tui_set_win_focus_to (TUI_DISASM_WIN);
		      layout_def->display_mode = DISASSEM_WIN;
		      layout_def->split = FALSE;
		      break;
		    case SRC_DISASSEM_COMMAND:
		      /* the previous layout was not showing
		         ** code. this can happen if there is no
		         ** source available:
		         ** 1. if the source file is in another dir OR
		         ** 2. if target was compiled without -g
		         ** We still want to show the assembly though!
		       */
		      addr = tui_get_begin_asm_address ();
		      if (win_with_focus == TUI_SRC_WIN)
			tui_set_win_focus_to (TUI_SRC_WIN);
		      else
			tui_set_win_focus_to (TUI_DISASM_WIN);
		      layout_def->split = TRUE;
		      break;
		    case SRC_DATA_COMMAND:
		      if (win_with_focus != TUI_DATA_WIN)
			tui_set_win_focus_to (TUI_SRC_WIN);
		      else
			tui_set_win_focus_to (TUI_DATA_WIN);
		      layout_def->display_mode = SRC_WIN;
		      layout_def->split = FALSE;
		      break;
		    case DISASSEM_DATA_COMMAND:
		      /* the previous layout was not showing
		         ** code. this can happen if there is no
		         ** source available:
		         ** 1. if the source file is in another dir OR
		         ** 2. if target was compiled without -g
		         ** We still want to show the assembly though!
		       */
		      addr = tui_get_begin_asm_address ();
		      if (win_with_focus != TUI_DATA_WIN)
			tui_set_win_focus_to (TUI_DISASM_WIN);
		      else
			tui_set_win_focus_to (TUI_DATA_WIN);
		      layout_def->display_mode = DISASSEM_WIN;
		      layout_def->split = FALSE;
		      break;
		    default:
		      break;
		    }
		}
	      if (new_win_with_focus != (struct tui_win_info *) NULL)
		tui_set_win_focus_to (new_win_with_focus);
	      /*
	         ** Now update the window content
	       */
	      if (!regs_populate &&
		  (new_layout == SRC_DATA_COMMAND ||
		   new_layout == DISASSEM_DATA_COMMAND))
		tui_display_all_data ();

	      tui_update_source_windows_with_addr (addr);
	    }
	  if (regs_populate)
	    {
              tui_show_registers (TUI_DATA_WIN->detail.data_display_info.current_group);
	    }
	}
    }
  else
    status = TUI_FAILURE;

  return status;
}

/* Add the specified window to the layout in a logical way.  This
   means setting up the most logical layout given the window to be
   added.  */
void
tui_add_win_to_layout (enum tui_win_type type)
{
  enum tui_layout_type cur_layout = tui_current_layout ();

  switch (type)
    {
    case SRC_WIN:
      if (cur_layout != SRC_COMMAND &&
	  cur_layout != SRC_DISASSEM_COMMAND &&
	  cur_layout != SRC_DATA_COMMAND)
	{
	  tui_clear_source_windows_detail ();
	  if (cur_layout == DISASSEM_DATA_COMMAND)
	    show_layout (SRC_DATA_COMMAND);
	  else
	    show_layout (SRC_COMMAND);
	}
      break;
    case DISASSEM_WIN:
      if (cur_layout != DISASSEM_COMMAND &&
	  cur_layout != SRC_DISASSEM_COMMAND &&
	  cur_layout != DISASSEM_DATA_COMMAND)
	{
	  tui_clear_source_windows_detail ();
	  if (cur_layout == SRC_DATA_COMMAND)
	    show_layout (DISASSEM_DATA_COMMAND);
	  else
	    show_layout (DISASSEM_COMMAND);
	}
      break;
    case DATA_WIN:
      if (cur_layout != SRC_DATA_COMMAND &&
	  cur_layout != DISASSEM_DATA_COMMAND)
	{
	  if (cur_layout == DISASSEM_COMMAND)
	    show_layout (DISASSEM_DATA_COMMAND);
	  else
	    show_layout (SRC_DATA_COMMAND);
	}
      break;
    default:
      break;
    }
}


/* Answer the height of a window.  If it hasn't been created yet,
   answer what the height of a window would be based upon its type and
   the layout.  */
int
tui_default_win_height (enum tui_win_type type, enum tui_layout_type layout)
{
  int h;

  if (tui_win_list[type] != (struct tui_win_info *) NULL)
    h = tui_win_list[type]->generic.height;
  else
    {
      switch (layout)
	{
	case SRC_COMMAND:
	case DISASSEM_COMMAND:
	  if (TUI_CMD_WIN == NULL)
	    h = tui_term_height () / 2;
	  else
	    h = tui_term_height () - TUI_CMD_WIN->generic.height;
	  break;
	case SRC_DISASSEM_COMMAND:
	case SRC_DATA_COMMAND:
	case DISASSEM_DATA_COMMAND:
	  if (TUI_CMD_WIN == NULL)
	    h = tui_term_height () / 3;
	  else
	    h = (tui_term_height () - TUI_CMD_WIN->generic.height) / 2;
	  break;
	default:
	  h = 0;
	  break;
	}
    }

  return h;
}


/* Answer the height of a window.  If it hasn't been created yet,
   answer what the height of a window would be based upon its type and
   the layout.  */
int
tui_default_win_viewport_height (enum tui_win_type type,
				 enum tui_layout_type layout)
{
  int h;

  h = tui_default_win_height (type, layout);

  if (tui_win_list[type] == TUI_CMD_WIN)
    h -= 1;
  else
    h -= 2;

  return h;
}


/* Function to initialize gdb commands, for tui window layout
   manipulation.  */
void
_initialize_tui_layout (void)
{
  add_com ("layout", class_tui, tui_layout_command,
           "Change the layout of windows.\n\
Usage: layout prev | next | <layout_name> \n\
Layout names are:\n\
   src   : Displays source and command windows.\n\
   asm   : Displays disassembly and command windows.\n\
   split : Displays source, disassembly and command windows.\n\
   regs  : Displays register window. If existing layout\n\
           is source/command or assembly/command, the \n\
           register window is displayed. If the\n\
           source/assembly/command (split) is displayed, \n\
           the register window is displayed with \n\
           the window that has current logical focus.\n");
  if (xdb_commands)
    {
      add_com ("td", class_tui, tui_toggle_layout_command,
               "Toggle between Source/Command and Disassembly/Command layouts.\n");
      add_com ("ts", class_tui, tui_toggle_split_layout_command,
               "Toggle between Source/Command or Disassembly/Command and \n\
Source/Disassembly/Command layouts.\n");
    }
}


/*************************
** STATIC LOCAL FUNCTIONS
**************************/


/* Function to set the layout to SRC, ASM, SPLIT, NEXT, PREV, DATA,
   REGS, $REGS, $GREGS, $FREGS, $SREGS.  */
enum tui_status
tui_set_layout_for_display_command (const char *layout_name)
{
  enum tui_status status = TUI_SUCCESS;

  if (layout_name != (char *) NULL)
    {
      int i;
      char *buf_ptr;
      enum tui_layout_type new_layout = UNDEFINED_LAYOUT;
      enum tui_register_display_type dpy_type = TUI_UNDEFINED_REGS;
      enum tui_layout_type cur_layout = tui_current_layout ();

      buf_ptr = (char *) xstrdup (layout_name);
      for (i = 0; (i < strlen (layout_name)); i++)
	buf_ptr[i] = toupper (buf_ptr[i]);

      /* First check for ambiguous input */
      if (strlen (buf_ptr) <= 1 && (*buf_ptr == 'S' || *buf_ptr == '$'))
	{
	  warning ("Ambiguous command input.\n");
	  status = TUI_FAILURE;
	}
      else
	{
	  if (subset_compare (buf_ptr, "SRC"))
	    new_layout = SRC_COMMAND;
	  else if (subset_compare (buf_ptr, "ASM"))
	    new_layout = DISASSEM_COMMAND;
	  else if (subset_compare (buf_ptr, "SPLIT"))
	    new_layout = SRC_DISASSEM_COMMAND;
	  else if (subset_compare (buf_ptr, "REGS") ||
		   subset_compare (buf_ptr, TUI_GENERAL_SPECIAL_REGS_NAME) ||
		   subset_compare (buf_ptr, TUI_GENERAL_REGS_NAME) ||
		   subset_compare (buf_ptr, TUI_FLOAT_REGS_NAME) ||
		   subset_compare (buf_ptr, TUI_SPECIAL_REGS_NAME))
	    {
	      if (cur_layout == SRC_COMMAND || cur_layout == SRC_DATA_COMMAND)
		new_layout = SRC_DATA_COMMAND;
	      else
		new_layout = DISASSEM_DATA_COMMAND;

/* could ifdef out the following code. when compile with -z, there are null 
   pointer references that cause a core dump if 'layout regs' is the first 
   layout command issued by the user. HP has asked us to hook up this code 
   - edie epstein
 */
	      if (subset_compare (buf_ptr, TUI_FLOAT_REGS_NAME))
		{
		  if (TUI_DATA_WIN->detail.data_display_info.regs_display_type !=
		      TUI_SFLOAT_REGS &&
		      TUI_DATA_WIN->detail.data_display_info.regs_display_type !=
		      TUI_DFLOAT_REGS)
		    dpy_type = TUI_SFLOAT_REGS;
		  else
		    dpy_type =
		      TUI_DATA_WIN->detail.data_display_info.regs_display_type;
		}
	      else if (subset_compare (buf_ptr,
				      TUI_GENERAL_SPECIAL_REGS_NAME))
		dpy_type = TUI_GENERAL_AND_SPECIAL_REGS;
	      else if (subset_compare (buf_ptr, TUI_GENERAL_REGS_NAME))
		dpy_type = TUI_GENERAL_REGS;
	      else if (subset_compare (buf_ptr, TUI_SPECIAL_REGS_NAME))
		dpy_type = TUI_SPECIAL_REGS;
	      else if (TUI_DATA_WIN)
		{
		  if (TUI_DATA_WIN->detail.data_display_info.regs_display_type !=
		      TUI_UNDEFINED_REGS)
		    dpy_type =
		      TUI_DATA_WIN->detail.data_display_info.regs_display_type;
		  else
		    dpy_type = TUI_GENERAL_REGS;
		}

/* end of potential ifdef 
 */

/* if ifdefed out code above, then assume that the user wishes to display the 
   general purpose registers 
 */

/*              dpy_type = TUI_GENERAL_REGS; 
 */
	    }
	  else if (subset_compare (buf_ptr, "NEXT"))
	    new_layout = next_layout ();
	  else if (subset_compare (buf_ptr, "PREV"))
	    new_layout = prev_layout ();
	  else
	    status = TUI_FAILURE;
	  xfree (buf_ptr);

	  tui_set_layout (new_layout, dpy_type);
	}
    }
  else
    status = TUI_FAILURE;

  return status;
}


static CORE_ADDR
extract_display_start_addr (void)
{
  enum tui_layout_type cur_layout = tui_current_layout ();
  CORE_ADDR addr;
  CORE_ADDR pc;
  struct symtab_and_line cursal = get_current_source_symtab_and_line ();

  switch (cur_layout)
    {
    case SRC_COMMAND:
    case SRC_DATA_COMMAND:
      find_line_pc (cursal.symtab,
		    TUI_SRC_WIN->detail.source_info.start_line_or_addr.line_no,
		    &pc);
      addr = pc;
      break;
    case DISASSEM_COMMAND:
    case SRC_DISASSEM_COMMAND:
    case DISASSEM_DATA_COMMAND:
      addr = TUI_DISASM_WIN->detail.source_info.start_line_or_addr.addr;
      break;
    default:
      addr = 0;
      break;
    }

  return addr;
}


static void
tui_handle_xdb_layout (struct tui_layout_def * layout_def)
{
  if (layout_def->split)
    {
      tui_set_layout (SRC_DISASSEM_COMMAND, TUI_UNDEFINED_REGS);
      tui_set_win_focus_to (tui_win_list[layout_def->display_mode]);
    }
  else
    {
      if (layout_def->display_mode == SRC_WIN)
	tui_set_layout (SRC_COMMAND, TUI_UNDEFINED_REGS);
      else
	tui_set_layout (DISASSEM_DATA_COMMAND, layout_def->regs_display_type);
    }
}


static void
tui_toggle_layout_command (char *arg, int from_tty)
{
  struct tui_layout_def * layout_def = tui_layout_def ();

  /* Make sure the curses mode is enabled.  */
  tui_enable ();
  if (layout_def->display_mode == SRC_WIN)
    layout_def->display_mode = DISASSEM_WIN;
  else
    layout_def->display_mode = SRC_WIN;

  if (!layout_def->split)
    tui_handle_xdb_layout (layout_def);
}


static void
tui_toggle_split_layout_command (char *arg, int from_tty)
{
  struct tui_layout_def * layout_def = tui_layout_def ();

  /* Make sure the curses mode is enabled.  */
  tui_enable ();
  layout_def->split = (!layout_def->split);
  tui_handle_xdb_layout (layout_def);
}


static void
tui_layout_command (char *arg, int from_tty)
{
  /* Make sure the curses mode is enabled.  */
  tui_enable ();

  /* Switch to the selected layout.  */
  if (tui_set_layout_for_display_command (arg) != TUI_SUCCESS)
    warning ("Invalid layout specified.\n%s", LAYOUT_USAGE);

}

/* Answer the previous layout to cycle to.  */
static enum tui_layout_type
next_layout (void)
{
  enum tui_layout_type new_layout;

  new_layout = tui_current_layout ();
  if (new_layout == UNDEFINED_LAYOUT)
    new_layout = SRC_COMMAND;
  else
    {
      new_layout++;
      if (new_layout == UNDEFINED_LAYOUT)
	new_layout = SRC_COMMAND;
    }

  return new_layout;
}


/* Answer the next layout to cycle to.  */
static enum tui_layout_type
prev_layout (void)
{
  enum tui_layout_type new_layout;

  new_layout = tui_current_layout ();
  if (new_layout == SRC_COMMAND)
    new_layout = DISASSEM_DATA_COMMAND;
  else
    {
      new_layout--;
      if (new_layout == UNDEFINED_LAYOUT)
	new_layout = DISASSEM_DATA_COMMAND;
    }

  return new_layout;
}



static void
make_command_window (struct tui_win_info * * win_info_ptr, int height, int origin_y)
{
  init_and_make_win ((void **) win_info_ptr,
		   CMD_WIN,
		   height,
		   tui_term_width (),
		   0,
		   origin_y,
		   DONT_BOX_WINDOW);

  (*win_info_ptr)->can_highlight = FALSE;
}


/*
   ** make_source_window().
 */
static void
make_source_window (struct tui_win_info * * win_info_ptr, int height, int origin_y)
{
  make_source_or_disasm_window (win_info_ptr, SRC_WIN, height, origin_y);

  return;
}				/* make_source_window */


/*
   ** make_disasm_window().
 */
static void
make_disasm_window (struct tui_win_info * * win_info_ptr, int height, int origin_y)
{
  make_source_or_disasm_window (win_info_ptr, DISASSEM_WIN, height, origin_y);

  return;
}				/* make_disasm_window */


static void
make_data_window (struct tui_win_info * * win_info_ptr, int height, int origin_y)
{
  init_and_make_win ((void **) win_info_ptr,
		   DATA_WIN,
		   height,
		   tui_term_width (),
		   0,
		   origin_y,
		   BOX_WINDOW);
}



/* Show the Source/Command layout.  */
static void
show_source_command (void)
{
  show_source_or_disasm_and_command (SRC_COMMAND);
}


/* Show the Dissassem/Command layout.  */
static void
show_disasm_command (void)
{
  show_source_or_disasm_and_command (DISASSEM_COMMAND);
}


/* Show the Source/Disassem/Command layout.  */
static void
show_source_disasm_command (void)
{
  if (tui_current_layout () != SRC_DISASSEM_COMMAND)
    {
      int cmd_height, src_height, asm_height;

      if (TUI_CMD_WIN != NULL)
	cmd_height = TUI_CMD_WIN->generic.height;
      else
	cmd_height = tui_term_height () / 3;

      src_height = (tui_term_height () - cmd_height) / 2;
      asm_height = tui_term_height () - (src_height + cmd_height);

      if (TUI_SRC_WIN == NULL)
	make_source_window (&TUI_SRC_WIN, src_height, 0);
      else
	{
	  init_gen_win_info (&TUI_SRC_WIN->generic,
			   TUI_SRC_WIN->generic.type,
			   src_height,
			   TUI_SRC_WIN->generic.width,
			   TUI_SRC_WIN->detail.source_info.execution_info->width,
			   0);
	  TUI_SRC_WIN->can_highlight = TRUE;
	  init_gen_win_info (TUI_SRC_WIN->detail.source_info.execution_info,
			   EXEC_INFO_WIN,
			   src_height,
			   3,
			   0,
			   0);
	  tui_make_visible (&TUI_SRC_WIN->generic);
	  tui_make_visible (TUI_SRC_WIN->detail.source_info.execution_info);
	  TUI_SRC_WIN->detail.source_info.has_locator = FALSE;;
	}
      if (TUI_SRC_WIN != NULL)
	{
	  struct tui_gen_win_info * locator = tui_locator_win_info_ptr ();

	  tui_show_source_content (TUI_SRC_WIN);
	  if (TUI_DISASM_WIN == NULL)
	    {
	      make_disasm_window (&TUI_DISASM_WIN, asm_height, src_height - 1);
	      init_and_make_win ((void **) & locator,
			       LOCATOR_WIN,
			       2 /* 1 */ ,
			       tui_term_width (),
			       0,
			       (src_height + asm_height) - 1,
			       DONT_BOX_WINDOW);
	    }
	  else
	    {
	      init_gen_win_info (locator,
			       LOCATOR_WIN,
			       2 /* 1 */ ,
			       tui_term_width (),
			       0,
			       (src_height + asm_height) - 1);
	      TUI_DISASM_WIN->detail.source_info.has_locator = TRUE;
	      init_gen_win_info (
				&TUI_DISASM_WIN->generic,
				TUI_DISASM_WIN->generic.type,
				asm_height,
				TUI_DISASM_WIN->generic.width,
			TUI_DISASM_WIN->detail.source_info.execution_info->width,
				src_height - 1);
	      init_gen_win_info (TUI_DISASM_WIN->detail.source_info.execution_info,
			       EXEC_INFO_WIN,
			       asm_height,
			       3,
			       0,
			       src_height - 1);
	      TUI_DISASM_WIN->can_highlight = TRUE;
	      tui_make_visible (&TUI_DISASM_WIN->generic);
	      tui_make_visible (TUI_DISASM_WIN->detail.source_info.execution_info);
	    }
	  if (TUI_DISASM_WIN != NULL)
	    {
	      TUI_SRC_WIN->detail.source_info.has_locator = FALSE;
	      TUI_DISASM_WIN->detail.source_info.has_locator = TRUE;
	      tui_make_visible (locator);
	      tui_show_locator_content ();
	      tui_show_source_content (TUI_DISASM_WIN);

	      if (TUI_CMD_WIN == NULL)
		make_command_window (&TUI_CMD_WIN,
				    cmd_height,
				    tui_term_height () - cmd_height);
	      else
		{
		  init_gen_win_info (&TUI_CMD_WIN->generic,
				   TUI_CMD_WIN->generic.type,
				   TUI_CMD_WIN->generic.height,
				   TUI_CMD_WIN->generic.width,
				   0,
				   TUI_CMD_WIN->generic.origin.y);
		  TUI_CMD_WIN->can_highlight = FALSE;
		  tui_make_visible (&TUI_CMD_WIN->generic);
		}
	      if (TUI_CMD_WIN != NULL)
		tui_refresh_win (&TUI_CMD_WIN->generic);
	    }
	}
      tui_set_current_layout_to (SRC_DISASSEM_COMMAND);
    }
}


/* Show the Source/Data/Command or the Dissassembly/Data/Command
   layout.  */
static void
show_data (enum tui_layout_type new_layout)
{
  int total_height = (tui_term_height () - TUI_CMD_WIN->generic.height);
  int src_height, data_height;
  enum tui_win_type win_type;
  struct tui_gen_win_info * locator = tui_locator_win_info_ptr ();


  data_height = total_height / 2;
  src_height = total_height - data_height;
  tui_make_all_invisible ();
  tui_make_invisible (locator);
  make_data_window (&TUI_DATA_WIN, data_height, 0);
  TUI_DATA_WIN->can_highlight = TRUE;
  if (new_layout == SRC_DATA_COMMAND)
    win_type = SRC_WIN;
  else
    win_type = DISASSEM_WIN;
  if (tui_win_list[win_type] == NULL)
    {
      if (win_type == SRC_WIN)
	make_source_window (&tui_win_list[win_type], src_height, data_height - 1);
      else
	make_disasm_window (&tui_win_list[win_type], src_height, data_height - 1);
      init_and_make_win ((void **) & locator,
		       LOCATOR_WIN,
		       2 /* 1 */ ,
		       tui_term_width (),
		       0,
		       total_height - 1,
		       DONT_BOX_WINDOW);
    }
  else
    {
      init_gen_win_info (&tui_win_list[win_type]->generic,
		       tui_win_list[win_type]->generic.type,
		       src_height,
		       tui_win_list[win_type]->generic.width,
		   tui_win_list[win_type]->detail.source_info.execution_info->width,
		       data_height - 1);
      init_gen_win_info (tui_win_list[win_type]->detail.source_info.execution_info,
		       EXEC_INFO_WIN,
		       src_height,
		       3,
		       0,
		       data_height - 1);
      tui_make_visible (&tui_win_list[win_type]->generic);
      tui_make_visible (tui_win_list[win_type]->detail.source_info.execution_info);
      init_gen_win_info (locator,
		       LOCATOR_WIN,
		       2 /* 1 */ ,
		       tui_term_width (),
		       0,
		       total_height - 1);
    }
  tui_win_list[win_type]->detail.source_info.has_locator = TRUE;
  tui_make_visible (locator);
  tui_show_locator_content ();
  tui_add_to_source_windows (tui_win_list[win_type]);
  tui_set_current_layout_to (new_layout);
}

/*
   ** init_gen_win_info().
 */
static void
init_gen_win_info (struct tui_gen_win_info * win_info, enum tui_win_type type,
                 int height, int width, int origin_x, int origin_y)
{
  int h = height;

  win_info->type = type;
  win_info->width = width;
  win_info->height = h;
  if (h > 1)
    {
      win_info->viewport_height = h - 1;
      if (win_info->type != CMD_WIN)
	win_info->viewport_height--;
    }
  else
    win_info->viewport_height = 1;
  win_info->origin.x = origin_x;
  win_info->origin.y = origin_y;

  return;
}				/* init_gen_win_info */

/*
   ** init_and_make_win().
 */
static void
init_and_make_win (void ** win_info_ptr, enum tui_win_type win_type,
                 int height, int width, int origin_x, int origin_y, int box_it)
{
  void *opaque_win_info = *win_info_ptr;
  struct tui_gen_win_info * generic;

  if (opaque_win_info == NULL)
    {
      if (tui_win_is_auxillary (win_type))
	opaque_win_info = (void *) tui_alloc_generic_win_info ();
      else
	opaque_win_info = (void *) tui_alloc_win_info (win_type);
    }
  if (tui_win_is_auxillary (win_type))
    generic = (struct tui_gen_win_info *) opaque_win_info;
  else
    generic = &((struct tui_win_info *) opaque_win_info)->generic;

  if (opaque_win_info != NULL)
    {
      init_gen_win_info (generic, win_type, height, width, origin_x, origin_y);
      if (!tui_win_is_auxillary (win_type))
	{
	  if (generic->type == CMD_WIN)
	    ((struct tui_win_info *) opaque_win_info)->can_highlight = FALSE;
	  else
	    ((struct tui_win_info *) opaque_win_info)->can_highlight = TRUE;
	}
      tui_make_window (generic, box_it);
    }
  *win_info_ptr = opaque_win_info;
}


static void
make_source_or_disasm_window (struct tui_win_info * * win_info_ptr, enum tui_win_type type,
                             int height, int origin_y)
{
  struct tui_gen_win_info * execution_info = (struct tui_gen_win_info *) NULL;

  /*
     ** Create the exeuction info window.
   */
  if (type == SRC_WIN)
    execution_info = tui_source_exec_info_win_ptr ();
  else
    execution_info = tui_disassem_exec_info_win_ptr ();
  init_and_make_win ((void **) & execution_info,
		   EXEC_INFO_WIN,
		   height,
		   3,
		   0,
		   origin_y,
		   DONT_BOX_WINDOW);
  /*
     ** Now create the source window.
   */
  init_and_make_win ((void **) win_info_ptr,
		   type,
		   height,
		   tui_term_width () - execution_info->width,
		   execution_info->width,
		   origin_y,
		   BOX_WINDOW);

  (*win_info_ptr)->detail.source_info.execution_info = execution_info;
}


/* Show the Source/Command or the Disassem layout.   */
static void
show_source_or_disasm_and_command (enum tui_layout_type layout_type)
{
  if (tui_current_layout () != layout_type)
    {
      struct tui_win_info * *win_info_ptr;
      int src_height, cmd_height;
      struct tui_gen_win_info * locator = tui_locator_win_info_ptr ();

      if (TUI_CMD_WIN != NULL)
	cmd_height = TUI_CMD_WIN->generic.height;
      else
	cmd_height = tui_term_height () / 3;
      src_height = tui_term_height () - cmd_height;


      if (layout_type == SRC_COMMAND)
	win_info_ptr = &TUI_SRC_WIN;
      else
	win_info_ptr = &TUI_DISASM_WIN;

      if ((*win_info_ptr) == NULL)
	{
	  if (layout_type == SRC_COMMAND)
	    make_source_window (win_info_ptr, src_height - 1, 0);
	  else
	    make_disasm_window (win_info_ptr, src_height - 1, 0);
	  init_and_make_win ((void **) & locator,
			   LOCATOR_WIN,
			   2 /* 1 */ ,
			   tui_term_width (),
			   0,
			   src_height - 1,
			   DONT_BOX_WINDOW);
	}
      else
	{
	  init_gen_win_info (locator,
			   LOCATOR_WIN,
			   2 /* 1 */ ,
			   tui_term_width (),
			   0,
			   src_height - 1);
	  (*win_info_ptr)->detail.source_info.has_locator = TRUE;
	  init_gen_win_info (
			    &(*win_info_ptr)->generic,
			    (*win_info_ptr)->generic.type,
			    src_height - 1,
			    (*win_info_ptr)->generic.width,
		      (*win_info_ptr)->detail.source_info.execution_info->width,
			    0);
	  init_gen_win_info ((*win_info_ptr)->detail.source_info.execution_info,
			   EXEC_INFO_WIN,
			   src_height - 1,
			   3,
			   0,
			   0);
	  (*win_info_ptr)->can_highlight = TRUE;
	  tui_make_visible (&(*win_info_ptr)->generic);
	  tui_make_visible ((*win_info_ptr)->detail.source_info.execution_info);
	}
      if ((*win_info_ptr) != NULL)
	{
	  (*win_info_ptr)->detail.source_info.has_locator = TRUE;
	  tui_make_visible (locator);
	  tui_show_locator_content ();
	  tui_show_source_content (*win_info_ptr);

	  if (TUI_CMD_WIN == NULL)
	    {
	      make_command_window (&TUI_CMD_WIN, cmd_height, src_height);
	      tui_refresh_win (&TUI_CMD_WIN->generic);
	    }
	  else
	    {
	      init_gen_win_info (&TUI_CMD_WIN->generic,
			       TUI_CMD_WIN->generic.type,
			       TUI_CMD_WIN->generic.height,
			       TUI_CMD_WIN->generic.width,
			       TUI_CMD_WIN->generic.origin.x,
			       TUI_CMD_WIN->generic.origin.y);
	      TUI_CMD_WIN->can_highlight = FALSE;
	      tui_make_visible (&TUI_CMD_WIN->generic);
	    }
	}
      tui_set_current_layout_to (layout_type);
    }
}
