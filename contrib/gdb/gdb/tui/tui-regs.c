/* TUI display registers in window.

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
#include "tui/tui.h"
#include "tui/tui-data.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "gdbcmd.h"
#include "frame.h"
#include "regcache.h"
#include "inferior.h"
#include "target.h"
#include "gdb_string.h"
#include "tui/tui-layout.h"
#include "tui/tui-win.h"
#include "tui/tui-windata.h"
#include "tui/tui-wingeneral.h"
#include "tui/tui-file.h"
#include "reggroups.h"

#include "gdb_curses.h"


/*****************************************
** STATIC LOCAL FUNCTIONS FORWARD DECLS    **
******************************************/
static void
tui_display_register (struct tui_data_element *data,
                      struct tui_gen_win_info *win_info);

static enum tui_status
tui_show_register_group (struct gdbarch *gdbarch, struct reggroup *group,
                         struct frame_info *frame, int refresh_values_only);

static enum tui_status
tui_get_register (struct gdbarch *gdbarch, struct frame_info *frame,
                  struct tui_data_element *data, int regnum, int *changedp);
static void tui_register_format
  (struct gdbarch *, struct frame_info *, struct tui_data_element*, int);
static void tui_scroll_regs_forward_command (char *, int);
static void tui_scroll_regs_backward_command (char *, int);



/*****************************************
** PUBLIC FUNCTIONS                     **
******************************************/

/* Answer the number of the last line in the regs display.  If there
   are no registers (-1) is returned.  */
int
tui_last_regs_line_no (void)
{
  int num_lines = (-1);

  if (TUI_DATA_WIN->detail.data_display_info.regs_content_count > 0)
    {
      num_lines = (TUI_DATA_WIN->detail.data_display_info.regs_content_count /
		  TUI_DATA_WIN->detail.data_display_info.regs_column_count);
      if (TUI_DATA_WIN->detail.data_display_info.regs_content_count %
	  TUI_DATA_WIN->detail.data_display_info.regs_column_count)
	num_lines++;
    }
  return num_lines;
}


/* Answer the line number that the register element at element_no is
   on.  If element_no is greater than the number of register elements
   there are, -1 is returned.  */
int
tui_line_from_reg_element_no (int element_no)
{
  if (element_no < TUI_DATA_WIN->detail.data_display_info.regs_content_count)
    {
      int i, line = (-1);

      i = 1;
      while (line == (-1))
	{
	  if (element_no <
	      (TUI_DATA_WIN->detail.data_display_info.regs_column_count * i))
	    line = i - 1;
	  else
	    i++;
	}

      return line;
    }
  else
    return (-1);
}


/* Answer the index of the first element in line_no.  If line_no is past
   the register area (-1) is returned.  */
int
tui_first_reg_element_no_inline (int line_no)
{
  if ((line_no * TUI_DATA_WIN->detail.data_display_info.regs_column_count)
      <= TUI_DATA_WIN->detail.data_display_info.regs_content_count)
    return ((line_no + 1) *
	    TUI_DATA_WIN->detail.data_display_info.regs_column_count) -
      TUI_DATA_WIN->detail.data_display_info.regs_column_count;
  else
    return (-1);
}


/* Answer the index of the last element in line_no.  If line_no is
   past the register area (-1) is returned.  */
int
tui_last_reg_element_no_in_line (int line_no)
{
  if ((line_no * TUI_DATA_WIN->detail.data_display_info.regs_column_count) <=
      TUI_DATA_WIN->detail.data_display_info.regs_content_count)
    return ((line_no + 1) *
	    TUI_DATA_WIN->detail.data_display_info.regs_column_count) - 1;
  else
    return (-1);
}

/* Show the registers of the given group in the data window
   and refresh the window.  */
void
tui_show_registers (struct reggroup *group)
{
  enum tui_status ret = TUI_FAILURE;
  struct tui_data_info *display_info;

  /* Make sure the curses mode is enabled.  */
  tui_enable ();

  /* Make sure the register window is visible.  If not, select an
     appropriate layout.  */
  if (TUI_DATA_WIN == NULL || !TUI_DATA_WIN->generic.is_visible)
    tui_set_layout_for_display_command (DATA_NAME);

  display_info = &TUI_DATA_WIN->detail.data_display_info;
  if (group == 0)
    group = general_reggroup;

  /* Say that registers should be displayed, even if there is a problem.  */
  display_info->display_regs = TRUE;

  if (target_has_registers && target_has_stack && target_has_memory)
    {
      ret = tui_show_register_group (current_gdbarch, group,
                                     get_current_frame (),
                                     group == display_info->current_group);
    }
  if (ret == TUI_FAILURE)
    {
      display_info->current_group = 0;
      tui_erase_data_content (NO_REGS_STRING);
    }
  else
    {
      int i;

      /* Clear all notation of changed values */
      for (i = 0; i < display_info->regs_content_count; i++)
	{
	  struct tui_gen_win_info *data_item_win;
          struct tui_win_element *win;

	  data_item_win = &display_info->regs_content[i]
            ->which_element.data_window;
          win = (struct tui_win_element *) data_item_win->content[0];
          win->which_element.data.highlight = FALSE;
	}
      display_info->current_group = group;
      tui_display_all_data ();
    }
}


/* Set the data window to display the registers of the register group
   using the given frame.  Values are refreshed only when refresh_values_only
   is TRUE.  */

static enum tui_status
tui_show_register_group (struct gdbarch *gdbarch, struct reggroup *group,
                         struct frame_info *frame, int refresh_values_only)
{
  enum tui_status ret = TUI_FAILURE;
  int nr_regs;
  int allocated_here = FALSE;
  int regnum, pos;
  char title[80];
  struct tui_data_info *display_info = &TUI_DATA_WIN->detail.data_display_info;

  /* Make a new title showing which group we display.  */
  snprintf (title, sizeof (title) - 1, "Register group: %s",
            reggroup_name (group));
  xfree (TUI_DATA_WIN->generic.title);
  TUI_DATA_WIN->generic.title = xstrdup (title);

  /* See how many registers must be displayed.  */
  nr_regs = 0;
  for (regnum = 0; regnum < NUM_REGS + NUM_PSEUDO_REGS; regnum++)
    {
      /* Must be in the group and have a name.  */
      if (gdbarch_register_reggroup_p (gdbarch, regnum, group)
          && gdbarch_register_name (gdbarch, regnum) != 0)
        nr_regs++;
    }

  if (display_info->regs_content_count > 0 && !refresh_values_only)
    {
      tui_free_data_content (display_info->regs_content,
                             display_info->regs_content_count);
      display_info->regs_content_count = 0;
    }

  if (display_info->regs_content_count <= 0)
    {
      display_info->regs_content = tui_alloc_content (nr_regs, DATA_WIN);
      allocated_here = TRUE;
      refresh_values_only = FALSE;
    }

  if (display_info->regs_content != (tui_win_content) NULL)
    {
      if (!refresh_values_only || allocated_here)
	{
	  TUI_DATA_WIN->generic.content = (void*) NULL;
	  TUI_DATA_WIN->generic.content_size = 0;
	  tui_add_content_elements (&TUI_DATA_WIN->generic, nr_regs);
	  display_info->regs_content
            = (tui_win_content) TUI_DATA_WIN->generic.content;
	  display_info->regs_content_count = nr_regs;
	}

      /* Now set the register names and values */
      pos = 0;
      for (regnum = 0; regnum < NUM_REGS + NUM_PSEUDO_REGS; regnum++)
        {
	  struct tui_gen_win_info *data_item_win;
          struct tui_data_element *data;
          const char *name;

          if (!gdbarch_register_reggroup_p (gdbarch, regnum, group))
            continue;

          name = gdbarch_register_name (gdbarch, regnum);
          if (name == 0)
            continue;

	  data_item_win =
            &display_info->regs_content[pos]->which_element.data_window;
          data =
            &((struct tui_win_element *) data_item_win->content[0])->which_element.data;
          if (data)
            {
              if (!refresh_values_only)
                {
                  data->item_no = regnum;
                  data->name = name;
                  data->highlight = FALSE;
                }
              if (data->value == (void*) NULL)
                data->value = (void*) xmalloc (MAX_REGISTER_SIZE);

              tui_get_register (gdbarch, frame, data, regnum, 0);
            }
          pos++;
	}

      TUI_DATA_WIN->generic.content_size =
	display_info->regs_content_count + display_info->data_content_count;
      ret = TUI_SUCCESS;
    }

  return ret;
}

/* Function to display the registers in the content from
   'start_element_no' until the end of the register content or the end
   of the display height.  No checking for displaying past the end of
   the registers is done here.  */
void
tui_display_registers_from (int start_element_no)
{
  struct tui_data_info *display_info = &TUI_DATA_WIN->detail.data_display_info;

  if (display_info->regs_content != (tui_win_content) NULL &&
      display_info->regs_content_count > 0)
    {
      int i = start_element_no;
      int j, value_chars_wide, item_win_width, cur_y;

      int max_len = 0;
      for (i = 0; i < display_info->regs_content_count; i++)
        {
          struct tui_data_element *data;
          struct tui_gen_win_info *data_item_win;
          char *p;
          int len;

          data_item_win = &display_info->regs_content[i]->which_element.data_window;
          data = &((struct tui_win_element *)
                   data_item_win->content[0])->which_element.data;
          len = 0;
          p = data->content;
          if (p != 0)
            while (*p)
              {
                if (*p++ == '\t')
                  len = 8 * ((len / 8) + 1);
                else
                  len++;
              }

          if (len > max_len)
            max_len = len;
        }
      item_win_width = max_len + 1;
      i = start_element_no;

      display_info->regs_column_count =
        (TUI_DATA_WIN->generic.width - 2) / item_win_width;
      if (display_info->regs_column_count == 0)
        display_info->regs_column_count = 1;
      item_win_width =
        (TUI_DATA_WIN->generic.width - 2) / display_info->regs_column_count;

      /*
         ** Now create each data "sub" window, and write the display into it.
       */
      cur_y = 1;
      while (i < display_info->regs_content_count &&
	     cur_y <= TUI_DATA_WIN->generic.viewport_height)
	{
	  for (j = 0;
	       (j < display_info->regs_column_count &&
		i < display_info->regs_content_count); j++)
	    {
	      struct tui_gen_win_info * data_item_win;
	      struct tui_data_element * data_element_ptr;

	      /* create the window if necessary */
	      data_item_win = &display_info->regs_content[i]
                ->which_element.data_window;
	      data_element_ptr = &((struct tui_win_element *)
				 data_item_win->content[0])->which_element.data;
              if (data_item_win->handle != (WINDOW*) NULL
                  && (data_item_win->height != 1
                      || data_item_win->width != item_win_width
                      || data_item_win->origin.x != (item_win_width * j) + 1
                      || data_item_win->origin.y != cur_y))
                {
                  tui_delete_win (data_item_win->handle);
                  data_item_win->handle = 0;
                }
                  
	      if (data_item_win->handle == (WINDOW *) NULL)
		{
		  data_item_win->height = 1;
		  data_item_win->width = item_win_width;
		  data_item_win->origin.x = (item_win_width * j) + 1;
		  data_item_win->origin.y = cur_y;
		  tui_make_window (data_item_win, DONT_BOX_WINDOW);
                  scrollok (data_item_win->handle, FALSE);
		}
              touchwin (data_item_win->handle);

	      /* Get the printable representation of the register
                 and display it.  */
              tui_display_register (data_element_ptr, data_item_win);
	      i++;		/* next register */
	    }
	  cur_y++;		/* next row; */
	}
    }
}


/* Function to display the registers in the content from
   'start_element_no' on 'start_line_no' until the end of the register
   content or the end of the display height.  This function checks
   that we won't display off the end of the register display.  */
void
tui_display_reg_element_at_line (int start_element_no, int start_line_no)
{
  if (TUI_DATA_WIN->detail.data_display_info.regs_content != (tui_win_content) NULL &&
      TUI_DATA_WIN->detail.data_display_info.regs_content_count > 0)
    {
      int element_no = start_element_no;

      if (start_element_no != 0 && start_line_no != 0)
	{
	  int last_line_no, first_line_on_last_page;

	  last_line_no = tui_last_regs_line_no ();
	  first_line_on_last_page = last_line_no - (TUI_DATA_WIN->generic.height - 2);
	  if (first_line_on_last_page < 0)
	    first_line_on_last_page = 0;
	  /*
	     ** If there is no other data displayed except registers,
	     ** and the element_no causes us to scroll past the end of the
	     ** registers, adjust what element to really start the display at.
	   */
	  if (TUI_DATA_WIN->detail.data_display_info.data_content_count <= 0 &&
	      start_line_no > first_line_on_last_page)
	    element_no = tui_first_reg_element_no_inline (first_line_on_last_page);
	}
      tui_display_registers_from (element_no);
    }
}



/* Function to display the registers starting at line line_no in the
   data window.  Answers the line number that the display actually
   started from.  If nothing is displayed (-1) is returned.  */
int
tui_display_registers_from_line (int line_no, int force_display)
{
  if (TUI_DATA_WIN->detail.data_display_info.regs_content_count > 0)
    {
      int line, element_no;

      if (line_no < 0)
	line = 0;
      else if (force_display)
	{			/*
				   ** If we must display regs (force_display is true), then make
				   ** sure that we don't display off the end of the registers.
				 */
	  if (line_no >= tui_last_regs_line_no ())
	    {
	      if ((line = tui_line_from_reg_element_no (
		 TUI_DATA_WIN->detail.data_display_info.regs_content_count - 1)) < 0)
		line = 0;
	    }
	  else
	    line = line_no;
	}
      else
	line = line_no;

      element_no = tui_first_reg_element_no_inline (line);
      if (element_no < TUI_DATA_WIN->detail.data_display_info.regs_content_count)
	tui_display_reg_element_at_line (element_no, line);
      else
	line = (-1);

      return line;
    }

  return (-1);			/* nothing was displayed */
}


/* This function check all displayed registers for changes in values,
   given a particular frame.  If the values have changed, they are
   updated with the new value and highlighted.  */
void
tui_check_register_values (struct frame_info *frame)
{
  if (TUI_DATA_WIN != NULL && TUI_DATA_WIN->generic.is_visible)
    {
      struct tui_data_info *display_info
        = &TUI_DATA_WIN->detail.data_display_info;

      if (display_info->regs_content_count <= 0 && display_info->display_regs)
	tui_show_registers (display_info->current_group);
      else
	{
	  int i, j;

	  for (i = 0; (i < display_info->regs_content_count); i++)
	    {
	      struct tui_data_element *data;
	      struct tui_gen_win_info *data_item_win_ptr;
	      int was_hilighted;

	      data_item_win_ptr = &display_info->regs_content[i]->
                which_element.data_window;
	      data = &((struct tui_win_element *)
                       data_item_win_ptr->content[0])->which_element.data;
	      was_hilighted = data->highlight;

              tui_get_register (current_gdbarch, frame, data,
                                data->item_no, &data->highlight);

	      if (data->highlight || was_hilighted)
		{
                  tui_display_register (data, data_item_win_ptr);
		}
	    }
	}
    }
}

/* Display a register in a window.  If hilite is TRUE,
   then the value will be displayed in reverse video  */
static void
tui_display_register (struct tui_data_element *data,
                      struct tui_gen_win_info *win_info)
{
  if (win_info->handle != (WINDOW *) NULL)
    {
      int i;

      if (data->highlight)
	wstandout (win_info->handle);
      
      wmove (win_info->handle, 0, 0);
      for (i = 1; i < win_info->width; i++)
        waddch (win_info->handle, ' ');
      wmove (win_info->handle, 0, 0);
      if (data->content)
        waddstr (win_info->handle, data->content);

      if (data->highlight)
	wstandend (win_info->handle);
      tui_refresh_win (win_info);
    }
}

static void
tui_reg_next_command (char *arg, int from_tty)
{
  if (TUI_DATA_WIN != 0)
    {
      struct reggroup *group
        = TUI_DATA_WIN->detail.data_display_info.current_group;

      group = reggroup_next (current_gdbarch, group);
      if (group == 0)
        group = reggroup_next (current_gdbarch, 0);

      if (group)
        tui_show_registers (group);
    }
}

static void
tui_reg_float_command (char *arg, int from_tty)
{
  tui_show_registers (float_reggroup);
}

static void
tui_reg_general_command (char *arg, int from_tty)
{
  tui_show_registers (general_reggroup);
}

static void
tui_reg_system_command (char *arg, int from_tty)
{
  tui_show_registers (system_reggroup);
}

static struct cmd_list_element *tuireglist;

static void
tui_reg_command (char *args, int from_tty)
{
  printf_unfiltered ("\"tui reg\" must be followed by the name of a "
                     "tui reg command.\n");
  help_list (tuireglist, "tui reg ", -1, gdb_stdout);
}

void
_initialize_tui_regs (void)
{
  struct cmd_list_element **tuicmd;

  tuicmd = tui_get_cmd_list ();

  add_prefix_cmd ("reg", class_tui, tui_reg_command,
                  "TUI commands to control the register window.",
                  &tuireglist, "tui reg ", 0,
                  tuicmd);

  add_cmd ("float", class_tui, tui_reg_float_command,
           "Display only floating point registers\n",
           &tuireglist);
  add_cmd ("general", class_tui, tui_reg_general_command,
           "Display only general registers\n",
           &tuireglist);
  add_cmd ("system", class_tui, tui_reg_system_command,
           "Display only system registers\n",
           &tuireglist);
  add_cmd ("next", class_tui, tui_reg_next_command,
           "Display next register group\n",
           &tuireglist);

  if (xdb_commands)
    {
      add_com ("fr", class_tui, tui_reg_float_command,
	       "Display only floating point registers\n");
      add_com ("gr", class_tui, tui_reg_general_command,
	       "Display only general registers\n");
      add_com ("sr", class_tui, tui_reg_system_command,
	       "Display only special registers\n");
      add_com ("+r", class_tui, tui_scroll_regs_forward_command,
	       "Scroll the registers window forward\n");
      add_com ("-r", class_tui, tui_scroll_regs_backward_command,
	       "Scroll the register window backward\n");
    }
}


/*****************************************
** STATIC LOCAL FUNCTIONS                 **
******************************************/

extern int pagination_enabled;

static void
tui_restore_gdbout (void *ui)
{
  ui_file_delete (gdb_stdout);
  gdb_stdout = (struct ui_file*) ui;
  pagination_enabled = 1;
}

/* Get the register from the frame and make a printable representation
   of it in the data element.  */
static void
tui_register_format (struct gdbarch *gdbarch, struct frame_info *frame,
                     struct tui_data_element *data_element, int regnum)
{
  struct ui_file *stream;
  struct ui_file *old_stdout;
  const char *name;
  struct cleanup *cleanups;
  char *p, *s;
  int pos;
  struct type *type = gdbarch_register_type (gdbarch, regnum);

  name = gdbarch_register_name (gdbarch, regnum);
  if (name == 0)
    {
      return;
    }
  
  pagination_enabled = 0;
  old_stdout = gdb_stdout;
  stream = tui_sfileopen (256);
  gdb_stdout = stream;
  cleanups = make_cleanup (tui_restore_gdbout, (void*) old_stdout);
  if (TYPE_VECTOR (type) != 0 && 0)
    {
      char buf[MAX_REGISTER_SIZE];
      int len;

      len = register_size (current_gdbarch, regnum);
      fprintf_filtered (stream, "%-14s ", name);
      get_frame_register (frame, regnum, buf);
      print_scalar_formatted (buf, type, 'f', len, stream);
    }
  else
    {
      gdbarch_print_registers_info (current_gdbarch, stream,
                                    frame, regnum, 1);
    }

  /* Save formatted output in the buffer.  */
  p = tui_file_get_strbuf (stream);

  /* Remove the possible \n.  */
  s = strrchr (p, '\n');
  if (s && s[1] == 0)
    *s = 0;

  xfree (data_element->content);
  data_element->content = xstrdup (p);
  do_cleanups (cleanups);
}

/* Get the register value from the given frame and format it for
   the display.  When changep is set, check if the new register value
   has changed with respect to the previous call.  */
static enum tui_status
tui_get_register (struct gdbarch *gdbarch, struct frame_info *frame,
                  struct tui_data_element *data, int regnum, int *changedp)
{
  enum tui_status ret = TUI_FAILURE;

  if (changedp)
    *changedp = FALSE;
  if (target_has_registers)
    {
      char buf[MAX_REGISTER_SIZE];

      get_frame_register (frame, regnum, buf);
      /* NOTE: cagney/2003-03-13: This is bogus.  It is refering to
         the register cache and not the frame which could have pulled
         the register value off the stack.  */
      if (register_cached (regnum) >= 0)
        {
          if (changedp)
            {
              int size = register_size (gdbarch, regnum);
              char *old = (char*) data->value;
              int i;

              for (i = 0; i < size; i++)
                if (buf[i] != old[i])
                  {
                    *changedp = TRUE;
                    old[i] = buf[i];
                  }
            }

          /* Reformat the data content if the value changed.  */
          if (changedp == 0 || *changedp == TRUE)
            tui_register_format (gdbarch, frame, data, regnum);
          ret = TUI_SUCCESS;
        }
    }
  return ret;
}

static void
tui_scroll_regs_forward_command (char *arg, int from_tty)
{
  tui_scroll (FORWARD_SCROLL, TUI_DATA_WIN, 1);
}


static void
tui_scroll_regs_backward_command (char *arg, int from_tty)
{
  tui_scroll (BACKWARD_SCROLL, TUI_DATA_WIN, 1);
}
