/* TUI data manipulation routines.

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
#include "symtab.h"
#include "tui/tui.h"
#include "tui/tui-data.h"
#include "tui/tui-wingeneral.h"

#include "gdb_string.h"
#include "gdb_curses.h"

/****************************
** GLOBAL DECLARATIONS
****************************/
struct tui_win_info *(tui_win_list[MAX_MAJOR_WINDOWS]);

/***************************
** Private data
****************************/
static enum tui_layout_type current_layout = UNDEFINED_LAYOUT;
static int term_height, term_width;
static struct tui_gen_win_info _locator;
static struct tui_gen_win_info exec_info[2];
static struct tui_win_info * src_win_list[2];
static struct tui_list source_windows = {(void **) src_win_list, 0};
static int default_tab_len = DEFAULT_TAB_LEN;
static struct tui_win_info * win_with_focus = (struct tui_win_info *) NULL;
static struct tui_layout_def layout_def =
{SRC_WIN,			/* DISPLAY_MODE */
 FALSE,				/* SPLIT */
 TUI_UNDEFINED_REGS,		/* REGS_DISPLAY_TYPE */
 TUI_SFLOAT_REGS};		/* FLOAT_REGS_DISPLAY_TYPE */
static int win_resized = FALSE;


/*********************************
** Static function forward decls
**********************************/
static void free_content (tui_win_content, int, enum tui_win_type);
static void free_content_elements (tui_win_content, int, enum tui_win_type);



/*********************************
** PUBLIC FUNCTIONS
**********************************/

int
tui_win_is_source_type (enum tui_win_type win_type)
{
  return (win_type == SRC_WIN || win_type == DISASSEM_WIN);
}

int
tui_win_is_auxillary (enum tui_win_type win_type)
{
  return (win_type > MAX_MAJOR_WINDOWS);
}

int
tui_win_has_locator (struct tui_win_info *win_info)
{
  return (win_info != NULL \
	  && win_info->detail.source_info.has_locator);
}

void
tui_set_win_highlight (struct tui_win_info *win_info, int highlight)
{
  if (win_info != NULL)
    win_info->is_highlighted = highlight;
}

/******************************************
** ACCESSORS & MUTATORS FOR PRIVATE DATA
******************************************/

/* Answer a whether the terminal window has been resized or not.   */
int
tui_win_resized (void)
{
  return win_resized;
}


/* Set a whether the terminal window has been resized or not.   */
void
tui_set_win_resized_to (int resized)
{
  win_resized = resized;
}


/* Answer a pointer to the current layout definition.   */
struct tui_layout_def *
tui_layout_def (void)
{
  return &layout_def;
}


/* Answer the window with the logical focus.    */
struct tui_win_info *
tui_win_with_focus (void)
{
  return win_with_focus;
}


/* Set the window that has the logical focus.   */
void
tui_set_win_with_focus (struct tui_win_info * win_info)
{
  win_with_focus = win_info;
}


/* Answer the length in chars, of tabs.    */
int
tui_default_tab_len (void)
{
  return default_tab_len;
}


/* Set the length in chars, of tabs.   */
void
tui_set_default_tab_len (int len)
{
  default_tab_len = len;
}


/* Accessor for the current source window.  Usually there is only one
   source window (either source or disassembly), but both can be
   displayed at the same time.  */
struct tui_list *
tui_source_windows (void)
{
  return &source_windows;
}


/* Clear the list of source windows.  Usually there is only one source
   window (either source or disassembly), but both can be displayed at
   the same time.  */
void
tui_clear_source_windows (void)
{
  source_windows.list[0] = NULL;
  source_windows.list[1] = NULL;
  source_windows.count = 0;
}


/* Clear the pertinant detail in the source windows.   */
void
tui_clear_source_windows_detail (void)
{
  int i;

  for (i = 0; i < (tui_source_windows ())->count; i++)
    tui_clear_win_detail ((struct tui_win_info *) (tui_source_windows ())->list[i]);
}


/* Add a window to the list of source windows.  Usually there is only
   one source window (either source or disassembly), but both can be
   displayed at the same time.  */
void
tui_add_to_source_windows (struct tui_win_info * win_info)
{
  if (source_windows.count < 2)
    source_windows.list[source_windows.count++] = (void *) win_info;
}


/* Clear the pertinant detail in the windows.   */
void
tui_clear_win_detail (struct tui_win_info * win_info)
{
  if (win_info != NULL)
    {
      switch (win_info->generic.type)
	{
	case SRC_WIN:
	case DISASSEM_WIN:
	  win_info->detail.source_info.start_line_or_addr.addr = 0;
	  win_info->detail.source_info.horizontal_offset = 0;
	  break;
	case CMD_WIN:
	  win_info->detail.command_info.cur_line =
	    win_info->detail.command_info.curch = 0;
	  break;
	case DATA_WIN:
	  win_info->detail.data_display_info.data_content =
	    (tui_win_content) NULL;
	  win_info->detail.data_display_info.data_content_count = 0;
	  win_info->detail.data_display_info.regs_content =
	    (tui_win_content) NULL;
	  win_info->detail.data_display_info.regs_content_count = 0;
	  win_info->detail.data_display_info.regs_display_type =
	    TUI_UNDEFINED_REGS;
	  win_info->detail.data_display_info.regs_column_count = 1;
	  win_info->detail.data_display_info.display_regs = FALSE;
	  break;
	default:
	  break;
	}
    }
}


/* Accessor for the source execution info ptr.  */
struct tui_gen_win_info *
tui_source_exec_info_win_ptr (void)
{
  return &exec_info[0];
}


/* Accessor for the disassem execution info ptr.  */
struct tui_gen_win_info *
tui_disassem_exec_info_win_ptr (void)
{
  return &exec_info[1];
}


/* Accessor for the locator win info.  Answers a pointer to the static
   locator win info struct.  */
struct tui_gen_win_info *
tui_locator_win_info_ptr (void)
{
  return &_locator;
}


/* Accessor for the term_height.  */
int
tui_term_height (void)
{
  return term_height;
}


/* Mutator for the term height.   */
void
tui_set_term_height_to (int h)
{
  term_height = h;
}


/* Accessor for the term_width.   */
int
tui_term_width (void)
{
  return term_width;
}


/* Mutator for the term_width.  */
void
tui_set_term_width_to (int w)
{
  term_width = w;
}


/* Accessor for the current layout.   */
enum tui_layout_type
tui_current_layout (void)
{
  return current_layout;
}


/* Mutator for the current layout.  */
void
tui_set_current_layout_to (enum tui_layout_type new_layout)
{
  current_layout = new_layout;
}


/* Set the origin of the window.  */
void
set_gen_win_origin (struct tui_gen_win_info * win_info, int x, int y)
{
  win_info->origin.x = x;
  win_info->origin.y = y;
}


/*****************************
** OTHER PUBLIC FUNCTIONS
*****************************/


/* Answer the next window in the list, cycling back to the top if
   necessary.  */
struct tui_win_info *
tui_next_win (struct tui_win_info * cur_win)
{
  enum tui_win_type type = cur_win->generic.type;
  struct tui_win_info * next_win = (struct tui_win_info *) NULL;

  if (cur_win->generic.type == CMD_WIN)
    type = SRC_WIN;
  else
    type = cur_win->generic.type + 1;
  while (type != cur_win->generic.type && (next_win == NULL))
    {
      if (tui_win_list[type] && tui_win_list[type]->generic.is_visible)
	next_win = tui_win_list[type];
      else
	{
	  if (type == CMD_WIN)
	    type = SRC_WIN;
	  else
	    type++;
	}
    }

  return next_win;
}


/* Answer the prev window in the list, cycling back to the bottom if
   necessary.  */
struct tui_win_info *
tui_prev_win (struct tui_win_info * cur_win)
{
  enum tui_win_type type = cur_win->generic.type;
  struct tui_win_info * prev = (struct tui_win_info *) NULL;

  if (cur_win->generic.type == SRC_WIN)
    type = CMD_WIN;
  else
    type = cur_win->generic.type - 1;
  while (type != cur_win->generic.type && (prev == NULL))
    {
      if (tui_win_list[type]->generic.is_visible)
	prev = tui_win_list[type];
      else
	{
	  if (type == SRC_WIN)
	    type = CMD_WIN;
	  else
	    type--;
	}
    }

  return prev;
}


/* Answer the window represented by name.    */
struct tui_win_info *
tui_partial_win_by_name (char *name)
{
  struct tui_win_info * win_info = (struct tui_win_info *) NULL;

  if (name != (char *) NULL)
    {
      int i = 0;

      while (i < MAX_MAJOR_WINDOWS && win_info == NULL)
	{
          if (tui_win_list[i] != 0)
            {
              char *cur_name = tui_win_name (&tui_win_list[i]->generic);
              if (strlen (name) <= strlen (cur_name) &&
                  strncmp (name, cur_name, strlen (name)) == 0)
                win_info = tui_win_list[i];
            }
	  i++;
	}
    }

  return win_info;
}


/* Answer the name of the window.  */
char *
tui_win_name (struct tui_gen_win_info * win_info)
{
  char *name = (char *) NULL;

  switch (win_info->type)
    {
    case SRC_WIN:
      name = SRC_NAME;
      break;
    case CMD_WIN:
      name = CMD_NAME;
      break;
    case DISASSEM_WIN:
      name = DISASSEM_NAME;
      break;
    case DATA_WIN:
      name = DATA_NAME;
      break;
    default:
      name = "";
      break;
    }

  return name;
}


void
tui_initialize_static_data (void)
{
  tui_init_generic_part (tui_source_exec_info_win_ptr ());
  tui_init_generic_part (tui_disassem_exec_info_win_ptr ());
  tui_init_generic_part (tui_locator_win_info_ptr ());
}


struct tui_gen_win_info *
tui_alloc_generic_win_info (void)
{
  struct tui_gen_win_info * win;

  if ((win = (struct tui_gen_win_info *) xmalloc (
		     sizeof (struct tui_gen_win_info *))) != (struct tui_gen_win_info *) NULL)
    tui_init_generic_part (win);

  return win;
}


void
tui_init_generic_part (struct tui_gen_win_info * win)
{
  win->width =
    win->height =
    win->origin.x =
    win->origin.y =
    win->viewport_height =
    win->content_size =
    win->last_visible_line = 0;
  win->handle = (WINDOW *) NULL;
  win->content = NULL;
  win->content_in_use =
    win->is_visible = FALSE;
  win->title = 0;
}


/*
   ** init_content_element().
 */
void
init_content_element (struct tui_win_element * element, enum tui_win_type type)
{
  element->highlight = FALSE;
  switch (type)
    {
    case SRC_WIN:
    case DISASSEM_WIN:
      element->which_element.source.line = (char *) NULL;
      element->which_element.source.line_or_addr.line_no = 0;
      element->which_element.source.is_exec_point = FALSE;
      element->which_element.source.has_break = FALSE;
      break;
    case DATA_WIN:
      tui_init_generic_part (&element->which_element.data_window);
      element->which_element.data_window.type = DATA_ITEM_WIN;
      ((struct tui_gen_win_info *) & element->which_element.data_window)->content =
	(void **) tui_alloc_content (1, DATA_ITEM_WIN);
      ((struct tui_gen_win_info *)
       & element->which_element.data_window)->content_size = 1;
      break;
    case CMD_WIN:
      element->which_element.command.line = (char *) NULL;
      break;
    case DATA_ITEM_WIN:
      element->which_element.data.name = (char *) NULL;
      element->which_element.data.type = TUI_REGISTER;
      element->which_element.data.item_no = UNDEFINED_ITEM;
      element->which_element.data.value = NULL;
      element->which_element.data.highlight = FALSE;
      element->which_element.data.content = (char*) NULL;
      break;
    case LOCATOR_WIN:
      element->which_element.locator.file_name[0] =
	element->which_element.locator.proc_name[0] = (char) 0;
      element->which_element.locator.line_no = 0;
      element->which_element.locator.addr = 0;
      break;
    case EXEC_INFO_WIN:
      memset(element->which_element.simple_string, ' ',
             sizeof(element->which_element.simple_string));
      break;
    default:
      break;
    }
}

void
init_win_info (struct tui_win_info * win_info)
{
  tui_init_generic_part (&win_info->generic);
  win_info->can_highlight =
    win_info->is_highlighted = FALSE;
  switch (win_info->generic.type)
    {
    case SRC_WIN:
    case DISASSEM_WIN:
      win_info->detail.source_info.execution_info = (struct tui_gen_win_info *) NULL;
      win_info->detail.source_info.has_locator = FALSE;
      win_info->detail.source_info.horizontal_offset = 0;
      win_info->detail.source_info.start_line_or_addr.addr = 0;
      win_info->detail.source_info.filename = 0;
      break;
    case DATA_WIN:
      win_info->detail.data_display_info.data_content = (tui_win_content) NULL;
      win_info->detail.data_display_info.data_content_count = 0;
      win_info->detail.data_display_info.regs_content = (tui_win_content) NULL;
      win_info->detail.data_display_info.regs_content_count = 0;
      win_info->detail.data_display_info.regs_display_type =
	TUI_UNDEFINED_REGS;
      win_info->detail.data_display_info.regs_column_count = 1;
      win_info->detail.data_display_info.display_regs = FALSE;
      win_info->detail.data_display_info.current_group = 0;
      break;
    case CMD_WIN:
      win_info->detail.command_info.cur_line = 0;
      win_info->detail.command_info.curch = 0;
      break;
    default:
      win_info->detail.opaque = NULL;
      break;
    }
}


struct tui_win_info *
tui_alloc_win_info (enum tui_win_type type)
{
  struct tui_win_info * win_info = (struct tui_win_info *) NULL;

  win_info = (struct tui_win_info *) xmalloc (sizeof (struct tui_win_info));
  if ((win_info != NULL))
    {
      win_info->generic.type = type;
      init_win_info (win_info);
    }

  return win_info;
}


/* Allocates the content and elements in a block.  */
tui_win_content
tui_alloc_content (int num_elements, enum tui_win_type type)
{
  tui_win_content content = (tui_win_content) NULL;
  char *element_block_ptr = (char *) NULL;
  int i;

  if ((content = (tui_win_content)
  xmalloc (sizeof (struct tui_win_element *) * num_elements)) != (tui_win_content) NULL)
    {				/*
				   ** All windows, except the data window, can allocate the elements
				   ** in a chunk.  The data window cannot because items can be
				   ** added/removed from the data display by the user at any time.
				 */
      if (type != DATA_WIN)
	{
	  if ((element_block_ptr = (char *)
	   xmalloc (sizeof (struct tui_win_element) * num_elements)) != (char *) NULL)
	    {
	      for (i = 0; i < num_elements; i++)
		{
		  content[i] = (struct tui_win_element *) element_block_ptr;
		  init_content_element (content[i], type);
		  element_block_ptr += sizeof (struct tui_win_element);
		}
	    }
	  else
	    {
	      xfree (content);
	      content = (tui_win_content) NULL;
	    }
	}
    }

  return content;
}


/* Adds the input number of elements to the windows's content.  If no
   content has been allocated yet, alloc_content() is called to do
   this.  The index of the first element added is returned, unless
   there is a memory allocation error, in which case, (-1) is
   returned.  */
int
tui_add_content_elements (struct tui_gen_win_info * win_info, int num_elements)
{
  struct tui_win_element * element_ptr;
  int i, index_start;

  if (win_info->content == NULL)
    {
      win_info->content = (void **) tui_alloc_content (num_elements, win_info->type);
      index_start = 0;
    }
  else
    index_start = win_info->content_size;
  if (win_info->content != NULL)
    {
      for (i = index_start; (i < num_elements + index_start); i++)
	{
	  if ((element_ptr = (struct tui_win_element *)
	       xmalloc (sizeof (struct tui_win_element))) != (struct tui_win_element *) NULL)
	    {
	      win_info->content[i] = (void *) element_ptr;
	      init_content_element (element_ptr, win_info->type);
	      win_info->content_size++;
	    }
	  else			/* things must be really hosed now! We ran out of memory!? */
	    return (-1);
	}
    }

  return index_start;
}


/* Delete all curses windows associated with win_info, leaving everything
   else intact.  */
void
tui_del_window (struct tui_win_info * win_info)
{
  struct tui_gen_win_info * generic_win;

  switch (win_info->generic.type)
    {
    case SRC_WIN:
    case DISASSEM_WIN:
      generic_win = tui_locator_win_info_ptr ();
      if (generic_win != (struct tui_gen_win_info *) NULL)
	{
	  tui_delete_win (generic_win->handle);
	  generic_win->handle = (WINDOW *) NULL;
	  generic_win->is_visible = FALSE;
	}
      if (win_info->detail.source_info.filename)
        {
          xfree (win_info->detail.source_info.filename);
          win_info->detail.source_info.filename = 0;
        }
      generic_win = win_info->detail.source_info.execution_info;
      if (generic_win != (struct tui_gen_win_info *) NULL)
	{
	  tui_delete_win (generic_win->handle);
	  generic_win->handle = (WINDOW *) NULL;
	  generic_win->is_visible = FALSE;
	}
      break;
    case DATA_WIN:
      if (win_info->generic.content != NULL)
	{
	  tui_del_data_windows (win_info->detail.data_display_info.regs_content,
				win_info->detail.data_display_info.regs_content_count);
	  tui_del_data_windows (win_info->detail.data_display_info.data_content,
				win_info->detail.data_display_info.data_content_count);
	}
      break;
    default:
      break;
    }
  if (win_info->generic.handle != (WINDOW *) NULL)
    {
      tui_delete_win (win_info->generic.handle);
      win_info->generic.handle = (WINDOW *) NULL;
      win_info->generic.is_visible = FALSE;
    }
}


void
tui_free_window (struct tui_win_info * win_info)
{
  struct tui_gen_win_info * generic_win;

  switch (win_info->generic.type)
    {
    case SRC_WIN:
    case DISASSEM_WIN:
      generic_win = tui_locator_win_info_ptr ();
      if (generic_win != (struct tui_gen_win_info *) NULL)
	{
	  tui_delete_win (generic_win->handle);
	  generic_win->handle = (WINDOW *) NULL;
	}
      tui_free_win_content (generic_win);
      if (win_info->detail.source_info.filename)
        {
          xfree (win_info->detail.source_info.filename);
          win_info->detail.source_info.filename = 0;
        }
      generic_win = win_info->detail.source_info.execution_info;
      if (generic_win != (struct tui_gen_win_info *) NULL)
	{
	  tui_delete_win (generic_win->handle);
	  generic_win->handle = (WINDOW *) NULL;
	  tui_free_win_content (generic_win);
	}
      break;
    case DATA_WIN:
      if (win_info->generic.content != NULL)
	{
	  tui_free_data_content (win_info->detail.data_display_info.regs_content,
				 win_info->detail.data_display_info.regs_content_count);
	  win_info->detail.data_display_info.regs_content =
	    (tui_win_content) NULL;
	  win_info->detail.data_display_info.regs_content_count = 0;
	  tui_free_data_content (win_info->detail.data_display_info.data_content,
				 win_info->detail.data_display_info.data_content_count);
	  win_info->detail.data_display_info.data_content =
	    (tui_win_content) NULL;
	  win_info->detail.data_display_info.data_content_count = 0;
	  win_info->detail.data_display_info.regs_display_type =
	    TUI_UNDEFINED_REGS;
	  win_info->detail.data_display_info.regs_column_count = 1;
	  win_info->detail.data_display_info.display_regs = FALSE;
	  win_info->generic.content = NULL;
	  win_info->generic.content_size = 0;
	}
      break;
    default:
      break;
    }
  if (win_info->generic.handle != (WINDOW *) NULL)
    {
      tui_delete_win (win_info->generic.handle);
      win_info->generic.handle = (WINDOW *) NULL;
      tui_free_win_content (&win_info->generic);
    }
  if (win_info->generic.title)
    xfree (win_info->generic.title);
  xfree (win_info);
}


void
tui_free_all_source_wins_content (void)
{
  int i;

  for (i = 0; i < (tui_source_windows ())->count; i++)
    {
      struct tui_win_info * win_info = (struct tui_win_info *) (tui_source_windows ())->list[i];

      if (win_info != NULL)
	{
	  tui_free_win_content (&(win_info->generic));
	  tui_free_win_content (win_info->detail.source_info.execution_info);
	}
    }
}


void
tui_free_win_content (struct tui_gen_win_info * win_info)
{
  if (win_info->content != NULL)
    {
      free_content ((tui_win_content) win_info->content,
		   win_info->content_size,
		   win_info->type);
      win_info->content = NULL;
    }
  win_info->content_size = 0;
}


void
tui_del_data_windows (tui_win_content content, int content_size)
{
  int i;

  /*
     ** Remember that data window content elements are of type struct tui_gen_win_info *,
     ** each of which whose single element is a data element.
   */
  for (i = 0; i < content_size; i++)
    {
      struct tui_gen_win_info * generic_win = &content[i]->which_element.data_window;

      if (generic_win != (struct tui_gen_win_info *) NULL)
	{
	  tui_delete_win (generic_win->handle);
	  generic_win->handle = (WINDOW *) NULL;
	  generic_win->is_visible = FALSE;
	}
    }
}


void
tui_free_data_content (tui_win_content content, int content_size)
{
  int i;

  /*
     ** Remember that data window content elements are of type struct tui_gen_win_info *,
     ** each of which whose single element is a data element.
   */
  for (i = 0; i < content_size; i++)
    {
      struct tui_gen_win_info * generic_win = &content[i]->which_element.data_window;

      if (generic_win != (struct tui_gen_win_info *) NULL)
	{
	  tui_delete_win (generic_win->handle);
	  generic_win->handle = (WINDOW *) NULL;
	  tui_free_win_content (generic_win);
	}
    }
  free_content (content,
	       content_size,
	       DATA_WIN);
}


/**********************************
** LOCAL STATIC FUNCTIONS        **
**********************************/


static void
free_content (tui_win_content content, int content_size, enum tui_win_type win_type)
{
  if (content != (tui_win_content) NULL)
    {
      free_content_elements (content, content_size, win_type);
      xfree (content);
    }
}


/*
   ** free_content_elements().
 */
static void
free_content_elements (tui_win_content content, int content_size, enum tui_win_type type)
{
  if (content != (tui_win_content) NULL)
    {
      int i;

      if (type == SRC_WIN || type == DISASSEM_WIN)
	{
	  /* free whole source block */
	  xfree (content[0]->which_element.source.line);
	}
      else
	{
	  for (i = 0; i < content_size; i++)
	    {
	      struct tui_win_element * element;

	      element = content[i];
	      if (element != (struct tui_win_element *) NULL)
		{
		  switch (type)
		    {
		    case DATA_WIN:
		      xfree (element);
		      break;
		    case DATA_ITEM_WIN:
		      /*
		         ** Note that data elements are not allocated
		         ** in a single block, but individually, as needed.
		       */
		      if (element->which_element.data.type != TUI_REGISTER)
			xfree ((void *)element->which_element.data.name);
		      xfree (element->which_element.data.value);
                      xfree (element->which_element.data.content);
		      xfree (element);
		      break;
		    case CMD_WIN:
		      xfree (element->which_element.command.line);
		      break;
		    default:
		      break;
		    }
		}
	    }
	}
      if (type != DATA_WIN && type != DATA_ITEM_WIN)
	xfree (content[0]);	/* free the element block */
    }
}
