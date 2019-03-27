/* TUI display source window.

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
#include <ctype.h>
#include "symtab.h"
#include "frame.h"
#include "breakpoint.h"
#include "source.h"
#include "symtab.h"

#include "tui/tui.h"
#include "tui/tui-data.h"
#include "tui/tui-stack.h"
#include "tui/tui-winsource.h"
#include "tui/tui-source.h"

#include "gdb_string.h"
#include "gdb_curses.h"

/* Function to display source in the source window.  */
enum tui_status
tui_set_source_content (struct symtab *s, int line_no, int noerror)
{
  enum tui_status ret = TUI_FAILURE;

  if (s != (struct symtab *) NULL && s->filename != (char *) NULL)
    {
      FILE *stream;
      int i, desc, c, line_width, nlines;
      char *src_line = 0;

      if ((ret = tui_alloc_source_buffer (TUI_SRC_WIN)) == TUI_SUCCESS)
	{
	  line_width = TUI_SRC_WIN->generic.width - 1;
	  /* Take hilite (window border) into account, when calculating
	     the number of lines  */
	  nlines = (line_no + (TUI_SRC_WIN->generic.height - 2)) - line_no;
	  desc = open_source_file (s);
	  if (desc < 0)
	    {
	      if (!noerror)
		{
		  char *name = alloca (strlen (s->filename) + 100);
		  sprintf (name, "%s:%d", s->filename, line_no);
		  print_sys_errmsg (name, errno);
		}
	      ret = TUI_FAILURE;
	    }
	  else
	    {
	      if (s->line_charpos == 0)
		find_source_lines (s, desc);

	      if (line_no < 1 || line_no > s->nlines)
		{
		  close (desc);
		  printf_unfiltered (
			  "Line number %d out of range; %s has %d lines.\n",
				      line_no, s->filename, s->nlines);
		}
	      else if (lseek (desc, s->line_charpos[line_no - 1], 0) < 0)
		{
		  close (desc);
		  perror_with_name (s->filename);
		}
	      else
		{
		  int offset, cur_line_no, cur_line, cur_len, threshold;
		  struct tui_gen_win_info * locator = tui_locator_win_info_ptr ();
                  struct tui_source_info * src = &TUI_SRC_WIN->detail.source_info;

                  if (TUI_SRC_WIN->generic.title)
                    xfree (TUI_SRC_WIN->generic.title);
                  TUI_SRC_WIN->generic.title = xstrdup (s->filename);

                  if (src->filename)
                    xfree (src->filename);
                  src->filename = xstrdup (s->filename);

		  /* Determine the threshold for the length of the line
                     and the offset to start the display.  */
		  offset = src->horizontal_offset;
		  threshold = (line_width - 1) + offset;
		  stream = fdopen (desc, FOPEN_RT);
		  clearerr (stream);
		  cur_line = 0;
		  cur_line_no = src->start_line_or_addr.line_no = line_no;
		  if (offset > 0)
		    src_line = (char *) xmalloc (
					   (threshold + 1) * sizeof (char));
		  while (cur_line < nlines)
		    {
		      struct tui_win_element * element = (struct tui_win_element *)
		      TUI_SRC_WIN->generic.content[cur_line];

		      /* get the first character in the line */
		      c = fgetc (stream);

		      if (offset == 0)
			src_line = ((struct tui_win_element *)
				   TUI_SRC_WIN->generic.content[
					cur_line])->which_element.source.line;
		      /* Init the line with the line number */
		      sprintf (src_line, "%-6d", cur_line_no);
		      cur_len = strlen (src_line);
		      i = cur_len -
			((cur_len / tui_default_tab_len ()) * tui_default_tab_len ());
		      while (i < tui_default_tab_len ())
			{
			  src_line[cur_len] = ' ';
			  i++;
			  cur_len++;
			}
		      src_line[cur_len] = (char) 0;

		      /* Set whether element is the execution point and
		         whether there is a break point on it.  */
		      element->which_element.source.line_or_addr.line_no =
			cur_line_no;
		      element->which_element.source.is_exec_point =
			(strcmp (((struct tui_win_element *)
			locator->content[0])->which_element.locator.file_name,
				 s->filename) == 0
			 && cur_line_no == ((struct tui_win_element *)
			 locator->content[0])->which_element.locator.line_no);
		      if (c != EOF)
			{
			  i = strlen (src_line) - 1;
			  do
			    {
			      if ((c != '\n') &&
				  (c != '\r') && (++i < threshold))
				{
				  if (c < 040 && c != '\t')
				    {
				      src_line[i++] = '^';
				      src_line[i] = c + 0100;
				    }
				  else if (c == 0177)
				    {
				      src_line[i++] = '^';
				      src_line[i] = '?';
				    }
				  else
				    {	/* Store the charcter in the line
					   buffer.  If it is a tab, then
					   translate to the correct number of
					   chars so we don't overwrite our
					   buffer.  */
				      if (c == '\t')
					{
					  int j, max_tab_len = tui_default_tab_len ();

					  for (j = i - (
					       (i / max_tab_len) * max_tab_len);
					       ((j < max_tab_len) &&
						i < threshold);
					       i++, j++)
					    src_line[i] = ' ';
					  i--;
					}
				      else
					src_line[i] = c;
				    }
				  src_line[i + 1] = 0;
				}
			      else
				{	/* If we have not reached EOL, then eat
                                           chars until we do  */
				  while (c != EOF && c != '\n' && c != '\r')
				    c = fgetc (stream);
				}
			    }
			  while (c != EOF && c != '\n' && c != '\r' &&
				 i < threshold && (c = fgetc (stream)));
			}
		      /* Now copy the line taking the offset into account */
		      if (strlen (src_line) > offset)
			strcpy (((struct tui_win_element *) TUI_SRC_WIN->generic.content[
					cur_line])->which_element.source.line,
				&src_line[offset]);
		      else
			((struct tui_win_element *)
			 TUI_SRC_WIN->generic.content[
			  cur_line])->which_element.source.line[0] = (char) 0;
		      cur_line++;
		      cur_line_no++;
		    }
		  if (offset > 0)
		    xfree (src_line);
		  fclose (stream);
		  TUI_SRC_WIN->generic.content_size = nlines;
		  ret = TUI_SUCCESS;
		}
	    }
	}
    }
  return ret;
}


/* elz: this function sets the contents of the source window to empty
   except for a line in the middle with a warning message about the
   source not being available. This function is called by
   tui_erase_source_contents(), which in turn is invoked when the
   source files cannot be accessed.  */

void
tui_set_source_content_nil (struct tui_win_info * win_info, char *warning_string)
{
  int line_width;
  int n_lines;
  int curr_line = 0;

  line_width = win_info->generic.width - 1;
  n_lines = win_info->generic.height - 2;

  /* set to empty each line in the window, except for the one
     which contains the message */
  while (curr_line < win_info->generic.content_size)
    {
      /* set the information related to each displayed line
         to null: i.e. the line number is 0, there is no bp,
         it is not where the program is stopped */

      struct tui_win_element * element =
      (struct tui_win_element *) win_info->generic.content[curr_line];
      element->which_element.source.line_or_addr.line_no = 0;
      element->which_element.source.is_exec_point = FALSE;
      element->which_element.source.has_break = FALSE;

      /* set the contents of the line to blank */
      element->which_element.source.line[0] = (char) 0;

      /* if the current line is in the middle of the screen, then we
         want to display the 'no source available' message in it.
         Note: the 'weird' arithmetic with the line width and height
         comes from the function tui_erase_source_content(). We need
         to keep the screen and the window's actual contents in synch.  */

      if (curr_line == (n_lines / 2 + 1))
	{
	  int i;
	  int xpos;
	  int warning_length = strlen (warning_string);
	  char *src_line;

	  src_line = element->which_element.source.line;

	  if (warning_length >= ((line_width - 1) / 2))
	    xpos = 1;
	  else
	    xpos = (line_width - 1) / 2 - warning_length;

	  for (i = 0; i < xpos; i++)
	    src_line[i] = ' ';

	  sprintf (src_line + i, "%s", warning_string);

	  for (i = xpos + warning_length; i < line_width; i++)
	    src_line[i] = ' ';

	  src_line[i] = '\n';

	}			/* end if */

      curr_line++;

    }				/* end while */
}


/* Function to display source in the source window.  This function
   initializes the horizontal scroll to 0.  */
void
tui_show_symtab_source (struct symtab *s, union tui_line_or_address line, int noerror)
{
  TUI_SRC_WIN->detail.source_info.horizontal_offset = 0;
  tui_update_source_window_as_is (TUI_SRC_WIN, s, line, noerror);
}


/* Answer whether the source is currently displayed in the source
   window.  */
int
tui_source_is_displayed (char *fname)
{
  return (TUI_SRC_WIN->generic.content_in_use &&
	  (strcmp (((struct tui_win_element *) (tui_locator_win_info_ptr ())->
		  content[0])->which_element.locator.file_name, fname) == 0));
}


/* Scroll the source forward or backward vertically.  */
void
tui_vertical_source_scroll (enum tui_scroll_direction scroll_direction,
			    int num_to_scroll)
{
  if (TUI_SRC_WIN->generic.content != NULL)
    {
      union tui_line_or_address l;
      struct symtab *s;
      tui_win_content content = (tui_win_content) TUI_SRC_WIN->generic.content;
      struct symtab_and_line cursal = get_current_source_symtab_and_line ();

      if (cursal.symtab == (struct symtab *) NULL)
	s = find_pc_symtab (get_frame_pc (deprecated_selected_frame));
      else
	s = cursal.symtab;

      if (scroll_direction == FORWARD_SCROLL)
	{
	  l.line_no = content[0]->which_element.source.line_or_addr.line_no +
	    num_to_scroll;
	  if (l.line_no > s->nlines)
	    /*line = s->nlines - win_info->generic.content_size + 1; */
	    /*elz: fix for dts 23398 */
	    l.line_no = content[0]->which_element.source.line_or_addr.line_no;
	}
      else
	{
	  l.line_no = content[0]->which_element.source.line_or_addr.line_no -
	    num_to_scroll;
	  if (l.line_no <= 0)
	    l.line_no = 1;
	}

      print_source_lines (s, l.line_no, l.line_no + 1, 0);
    }
}
