/* Data/register window display.

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
#include "tui/tui-wingeneral.h"
#include "tui/tui-regs.h"

#include "gdb_string.h"
#include "gdb_curses.h"


/*****************************************
** STATIC LOCAL FUNCTIONS FORWARD DECLS    **
******************************************/



/*****************************************
** PUBLIC FUNCTIONS                        **
******************************************/


/* Answer the index first element displayed.  If none are displayed,
   then return (-1).  */
int
tui_first_data_item_displayed (void)
{
  int element_no = (-1);
  int i;

  for (i = 0; (i < TUI_DATA_WIN->generic.content_size && element_no < 0); i++)
    {
      struct tui_gen_win_info * data_item_win;

      data_item_win = &((tui_win_content)
		      TUI_DATA_WIN->generic.content)[i]->which_element.data_window;
      if (data_item_win->handle != (WINDOW *) NULL && data_item_win->is_visible)
	element_no = i;
    }

  return element_no;
}


/* Answer the index of the first element in line_no.  If line_no is
   past the data area (-1) is returned.  */
int
tui_first_data_element_no_in_line (int line_no)
{
  int first_element_no = (-1);

  /*
     ** First see if there is a register on line_no, and if so, set the
     ** first element number
   */
  if ((first_element_no = tui_first_reg_element_no_inline (line_no)) == -1)
    {				/*
				   ** Looking at the general data, the 1st element on line_no
				 */
    }

  return first_element_no;
}


/* Function to delete all the item windows in the data window.  This
   is usually done when the data window is scrolled.  */
void
tui_delete_data_content_windows (void)
{
  int i;
  struct tui_gen_win_info * data_item_win_ptr;

  for (i = 0; (i < TUI_DATA_WIN->generic.content_size); i++)
    {
      data_item_win_ptr = &((tui_win_content)
		      TUI_DATA_WIN->generic.content)[i]->which_element.data_window;
      tui_delete_win (data_item_win_ptr->handle);
      data_item_win_ptr->handle = (WINDOW *) NULL;
      data_item_win_ptr->is_visible = FALSE;
    }
}


void
tui_erase_data_content (char *prompt)
{
  werase (TUI_DATA_WIN->generic.handle);
  tui_check_and_display_highlight_if_needed (TUI_DATA_WIN);
  if (prompt != (char *) NULL)
    {
      int half_width = (TUI_DATA_WIN->generic.width - 2) / 2;
      int x_pos;

      if (strlen (prompt) >= half_width)
	x_pos = 1;
      else
	x_pos = half_width - strlen (prompt);
      mvwaddstr (TUI_DATA_WIN->generic.handle,
		 (TUI_DATA_WIN->generic.height / 2),
		 x_pos,
		 prompt);
    }
  wrefresh (TUI_DATA_WIN->generic.handle);
}


/* This function displays the data that is in the data window's
   content.  It does not set the content.  */
void
tui_display_all_data (void)
{
  if (TUI_DATA_WIN->generic.content_size <= 0)
    tui_erase_data_content (NO_DATA_STRING);
  else
    {
      tui_erase_data_content ((char *) NULL);
      tui_delete_data_content_windows ();
      tui_check_and_display_highlight_if_needed (TUI_DATA_WIN);
      tui_display_registers_from (0);
      /*
         ** Then display the other data
       */
      if (TUI_DATA_WIN->detail.data_display_info.data_content !=
	  (tui_win_content) NULL &&
	  TUI_DATA_WIN->detail.data_display_info.data_content_count > 0)
	{
	}
    }
}


/* Function to display the data starting at line, line_no, in the data
   window.  */
void
tui_display_data_from_line (int line_no)
{
  int _line_no = line_no;

  if (line_no < 0)
    _line_no = 0;

  tui_check_and_display_highlight_if_needed (TUI_DATA_WIN);

  /* there is no general data, force regs to display (if there are any) */
  if (TUI_DATA_WIN->detail.data_display_info.data_content_count <= 0)
    tui_display_registers_from_line (_line_no, TRUE);
  else
    {
      int element_no, start_line_no;
      int regs_last_line = tui_last_regs_line_no ();


      /* display regs if we can */
      if (tui_display_registers_from_line (_line_no, FALSE) < 0)
	{			/*
				   ** _line_no is past the regs display, so calc where the
				   ** start data element is
				 */
	  if (regs_last_line < _line_no)
	    {			/* figure out how many lines each element is to obtain
				   the start element_no */
	    }
	}
      else
	{			/*
				   ** calculate the starting element of the data display, given
				   ** regs_last_line and how many lines each element is, up to
				   ** _line_no
				 */
	}
      /* Now display the data , starting at element_no */
    }
}


/* Display data starting at element element_no.   */
void
tui_display_data_from (int element_no, int reuse_windows)
{
  int first_line = (-1);

  if (element_no < TUI_DATA_WIN->detail.data_display_info.regs_content_count)
    first_line = tui_line_from_reg_element_no (element_no);
  else
    {				/* calculate the first_line from the element number */
    }

  if (first_line >= 0)
    {
      tui_erase_data_content ((char *) NULL);
      if (!reuse_windows)
	tui_delete_data_content_windows ();
      tui_display_data_from_line (first_line);
    }
}


/* Function to redisplay the contents of the data window.  */
void
tui_refresh_data_win (void)
{
  tui_erase_data_content ((char *) NULL);
  if (TUI_DATA_WIN->generic.content_size > 0)
    {
      int first_element = tui_first_data_item_displayed ();

      if (first_element >= 0)	/* re-use existing windows */
	tui_display_data_from (first_element, TRUE);
    }
}


/* Function to check the data values and hilite any that have changed.  */
void
tui_check_data_values (struct frame_info *frame)
{
  tui_check_register_values (frame);

  /* Now check any other data values that there are */
  if (TUI_DATA_WIN != NULL && TUI_DATA_WIN->generic.is_visible)
    {
      int i;

      for (i = 0; TUI_DATA_WIN->detail.data_display_info.data_content_count; i++)
	{
#ifdef LATER
	  tui_data_element_ptr data_element_ptr;
	  struct tui_gen_win_info * data_item_win_ptr;
	  Opaque new_value;

	  data_item_ptr = &TUI_DATA_WIN->detail.data_display_info.
	    data_content[i]->which_element.data_window;
	  data_element_ptr = &((tui_win_content)
			     data_item_win_ptr->content)[0]->which_element.data;
	  if value
	    has changed (data_element_ptr, frame, &new_value)
	    {
	      data_element_ptr->value = new_value;
	      update the display with the new value, hiliting it.
	    }
#endif
	}
    }
}


/* Scroll the data window vertically forward or backward.   */
void
tui_vertical_data_scroll (enum tui_scroll_direction scroll_direction, int num_to_scroll)
{
  int first_element_no;
  int first_line = (-1);

  first_element_no = tui_first_data_item_displayed ();
  if (first_element_no < TUI_DATA_WIN->detail.data_display_info.regs_content_count)
    first_line = tui_line_from_reg_element_no (first_element_no);
  else
    {				/* calculate the first line from the element number which is in
				   ** the general data content
				 */
    }

  if (first_line >= 0)
    {
      int last_element_no, last_line;

      if (scroll_direction == FORWARD_SCROLL)
	first_line += num_to_scroll;
      else
	first_line -= num_to_scroll;
      tui_erase_data_content ((char *) NULL);
      tui_delete_data_content_windows ();
      tui_display_data_from_line (first_line);
    }
}


/*****************************************
** STATIC LOCAL FUNCTIONS               **
******************************************/
