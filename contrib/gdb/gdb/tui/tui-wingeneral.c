/* General window behavior.

   Copyright 1998, 1999, 2000, 2001, 2002, 2003 Free Software Foundation,
   Inc.

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
#include "tui/tui-win.h"

#include "gdb_curses.h"

/***********************
** PUBLIC FUNCTIONS
***********************/

/* Refresh the window.   */
void
tui_refresh_win (struct tui_gen_win_info * win_info)
{
  if (win_info->type == DATA_WIN && win_info->content_size > 0)
    {
      int i;

      for (i = 0; (i < win_info->content_size); i++)
	{
	  struct tui_gen_win_info * data_item_win_ptr;

	  data_item_win_ptr = &((tui_win_content)
			     win_info->content)[i]->which_element.data_window;
	  if (data_item_win_ptr != NULL
	      && data_item_win_ptr->handle != (WINDOW *) NULL)
	    wrefresh (data_item_win_ptr->handle);
	}
    }
  else if (win_info->type == CMD_WIN)
    {
      /* Do nothing */
    }
  else
    {
      if (win_info->handle != (WINDOW *) NULL)
	wrefresh (win_info->handle);
    }

  return;
}


/* Function to delete the curses window, checking for NULL.   */
void
tui_delete_win (WINDOW * window)
{
  if (window != (WINDOW *) NULL)
    delwin (window);

  return;
}


/* Draw a border arround the window.  */
void
box_win (struct tui_gen_win_info * win_info, int highlight_flag)
{
  if (win_info && win_info->handle)
    {
      WINDOW *win;
      int attrs;

      win = win_info->handle;
      if (highlight_flag == HILITE)
        attrs = tui_active_border_attrs;
      else
        attrs = tui_border_attrs;

      wattron (win, attrs);
      wborder (win, tui_border_vline, tui_border_vline,
               tui_border_hline, tui_border_hline,
               tui_border_ulcorner, tui_border_urcorner,
               tui_border_llcorner, tui_border_lrcorner);
      if (win_info->title)
        mvwaddstr (win, 0, 3, win_info->title);
      wattroff (win, attrs);
    }
}


void
tui_unhighlight_win (struct tui_win_info * win_info)
{
  if (win_info != NULL && win_info->generic.handle != (WINDOW *) NULL)
    {
      box_win ((struct tui_gen_win_info *) win_info, NO_HILITE);
      wrefresh (win_info->generic.handle);
      tui_set_win_highlight (win_info, 0);
    }
}


void
tui_highlight_win (struct tui_win_info * win_info)
{
  if (win_info != NULL
      && win_info->can_highlight
      && win_info->generic.handle != (WINDOW *) NULL)
    {
      box_win ((struct tui_gen_win_info *) win_info, HILITE);
      wrefresh (win_info->generic.handle);
      tui_set_win_highlight (win_info, 1);
    }
}

void
tui_check_and_display_highlight_if_needed (struct tui_win_info * win_info)
{
  if (win_info != NULL && win_info->generic.type != CMD_WIN)
    {
      if (win_info->is_highlighted)
	tui_highlight_win (win_info);
      else
	tui_unhighlight_win (win_info);

    }
  return;
}


void
tui_make_window (struct tui_gen_win_info * win_info, int box_it)
{
  WINDOW *handle;

  handle = newwin (win_info->height,
		   win_info->width,
		   win_info->origin.y,
		   win_info->origin.x);
  win_info->handle = handle;
  if (handle != (WINDOW *) NULL)
    {
      if (box_it == BOX_WINDOW)
	box_win (win_info, NO_HILITE);
      win_info->is_visible = TRUE;
      scrollok (handle, TRUE);
    }
}


/* We can't really make windows visible, or invisible.  So we have to
   delete the entire window when making it visible, and create it
   again when making it visible.  */
static void
make_visible (struct tui_gen_win_info *win_info, int visible)
{
  /* Don't tear down/recreate command window */
  if (win_info->type == CMD_WIN)
    return;

  if (visible)
    {
      if (!win_info->is_visible)
	{
	  tui_make_window (win_info,
			   (win_info->type != CMD_WIN
			    && !tui_win_is_auxillary (win_info->type)));
	  win_info->is_visible = TRUE;
	}
    }
  else if (!visible &&
	   win_info->is_visible && win_info->handle != (WINDOW *) NULL)
    {
      win_info->is_visible = FALSE;
      tui_delete_win (win_info->handle);
      win_info->handle = (WINDOW *) NULL;
    }

  return;
}

void
tui_make_visible (struct tui_gen_win_info *win_info)
{
  make_visible (win_info, 1);
}

void
tui_make_invisible (struct tui_gen_win_info *win_info)
{
  make_visible (win_info, 0);
}


/* Makes all windows invisible (except the command and locator windows).   */
static void
make_all_visible (int visible)
{
  int i;

  for (i = 0; i < MAX_MAJOR_WINDOWS; i++)
    {
      if (tui_win_list[i] != NULL
	  && ((tui_win_list[i])->generic.type) != CMD_WIN)
	{
	  if (tui_win_is_source_type ((tui_win_list[i])->generic.type))
	    make_visible ((tui_win_list[i])->detail.source_info.execution_info,
			  visible);
	  make_visible ((struct tui_gen_win_info *) tui_win_list[i], visible);
	}
    }

  return;
}

void
tui_make_all_visible (void)
{
  make_all_visible (1);
}

void
tui_make_all_invisible (void)
{
  make_all_visible (0);
}

/* Function to refresh all the windows currently displayed.  */

void
tui_refresh_all (struct tui_win_info * * list)
{
  enum tui_win_type type;
  struct tui_gen_win_info * locator = tui_locator_win_info_ptr ();

  for (type = SRC_WIN; (type < MAX_MAJOR_WINDOWS); type++)
    {
      if (list[type] && list[type]->generic.is_visible)
	{
	  if (type == SRC_WIN || type == DISASSEM_WIN)
	    {
	      touchwin (list[type]->detail.source_info.execution_info->handle);
	      tui_refresh_win (list[type]->detail.source_info.execution_info);
	    }
	  touchwin (list[type]->generic.handle);
	  tui_refresh_win (&list[type]->generic);
	}
    }
  if (locator->is_visible)
    {
      touchwin (locator->handle);
      tui_refresh_win (locator);
    }
}


/*********************************
** Local Static Functions
*********************************/
