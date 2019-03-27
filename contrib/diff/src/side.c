/* sdiff-format output routines for GNU DIFF.

   Copyright (C) 1991, 1992, 1993, 1998, 2001, 2002, 2004 Free
   Software Foundation, Inc.

   This file is part of GNU DIFF.

   GNU DIFF is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.  No author or distributor
   accepts responsibility to anyone for the consequences of using it
   or for whether it serves any particular purpose or works at all,
   unless he says so in writing.  Refer to the GNU DIFF General Public
   License for full details.

   Everyone is granted permission to copy, modify and redistribute
   GNU DIFF, but only under the conditions described in the
   GNU DIFF General Public License.   A copy of this license is
   supposed to have been given to you along with GNU DIFF so you
   can know your rights and responsibilities.  It should be in a
   file named COPYING.  Among other things, the copyright notice
   and this notice must be preserved on all copies.  */

#include "diff.h"

static void print_sdiff_common_lines (lin, lin);
static void print_sdiff_hunk (struct change *);

/* Next line number to be printed in the two input files.  */
static lin next0, next1;

/* Print the edit-script SCRIPT as a sdiff style output.  */

void
print_sdiff_script (struct change *script)
{
  begin_output ();

  next0 = next1 = - files[0].prefix_lines;
  print_script (script, find_change, print_sdiff_hunk);

  print_sdiff_common_lines (files[0].valid_lines, files[1].valid_lines);
}

/* Tab from column FROM to column TO, where FROM <= TO.  Yield TO.  */

static size_t
tab_from_to (size_t from, size_t to)
{
  FILE *out = outfile;
  size_t tab;
  size_t tab_size = tabsize;

  if (!expand_tabs)
    for (tab = from + tab_size - from % tab_size;  tab <= to;  tab += tab_size)
      {
	putc ('\t', out);
	from = tab;
      }
  while (from++ < to)
    putc (' ', out);
  return to;
}

/* Print the text for half an sdiff line.  This means truncate to
   width observing tabs, and trim a trailing newline.  Return the
   last column written (not the number of chars).  */

static size_t
print_half_line (char const *const *line, size_t indent, size_t out_bound)
{
  FILE *out = outfile;
  register size_t in_position = 0;
  register size_t out_position = 0;
  register char const *text_pointer = line[0];
  register char const *text_limit = line[1];

  while (text_pointer < text_limit)
    {
      register unsigned char c = *text_pointer++;

      switch (c)
	{
	case '\t':
	  {
	    size_t spaces = tabsize - in_position % tabsize;
	    if (in_position == out_position)
	      {
		size_t tabstop = out_position + spaces;
		if (expand_tabs)
		  {
		    if (out_bound < tabstop)
		      tabstop = out_bound;
		    for (;  out_position < tabstop;  out_position++)
		      putc (' ', out);
		  }
		else
		  if (tabstop < out_bound)
		    {
		      out_position = tabstop;
		      putc (c, out);
		    }
	      }
	    in_position += spaces;
	  }
	  break;

	case '\r':
	  {
	    putc (c, out);
	    tab_from_to (0, indent);
	    in_position = out_position = 0;
	  }
	  break;

	case '\b':
	  if (in_position != 0 && --in_position < out_bound)
	    {
	      if (out_position <= in_position)
		/* Add spaces to make up for suppressed tab past out_bound.  */
		for (;  out_position < in_position;  out_position++)
		  putc (' ', out);
	      else
		{
		  out_position = in_position;
		  putc (c, out);
		}
	    }
	  break;

	case '\f':
	case '\v':
	control_char:
	  if (in_position < out_bound)
	    putc (c, out);
	  break;

	default:
	  if (! isprint (c))
	    goto control_char;
	  /* falls through */
	case ' ':
	  if (in_position++ < out_bound)
	    {
	      out_position = in_position;
	      putc (c, out);
	    }
	  break;

	case '\n':
	  return out_position;
	}
    }

  return out_position;
}

/* Print side by side lines with a separator in the middle.
   0 parameters are taken to indicate white space text.
   Blank lines that can easily be caught are reduced to a single newline.  */

static void
print_1sdiff_line (char const *const *left, char sep,
		   char const *const *right)
{
  FILE *out = outfile;
  size_t hw = sdiff_half_width;
  size_t c2o = sdiff_column2_offset;
  size_t col = 0;
  bool put_newline = false;

  if (left)
    {
      put_newline |= left[1][-1] == '\n';
      col = print_half_line (left, 0, hw);
    }

  if (sep != ' ')
    {
      col = tab_from_to (col, (hw + c2o - 1) / 2) + 1;
      if (sep == '|' && put_newline != (right[1][-1] == '\n'))
	sep = put_newline ? '/' : '\\';
      putc (sep, out);
    }

  if (right)
    {
      put_newline |= right[1][-1] == '\n';
      if (**right != '\n')
	{
	  col = tab_from_to (col, c2o);
	  print_half_line (right, col, hw);
	}
    }

  if (put_newline)
    putc ('\n', out);
}

/* Print lines common to both files in side-by-side format.  */
static void
print_sdiff_common_lines (lin limit0, lin limit1)
{
  lin i0 = next0, i1 = next1;

  if (!suppress_common_lines && (i0 != limit0 || i1 != limit1))
    {
      if (sdiff_merge_assist)
	{
	  long int len0 = limit0 - i0;
	  long int len1 = limit1 - i1;
	  fprintf (outfile, "i%ld,%ld\n", len0, len1);
	}

      if (!left_column)
	{
	  while (i0 != limit0 && i1 != limit1)
	    print_1sdiff_line (&files[0].linbuf[i0++], ' ',
			       &files[1].linbuf[i1++]);
	  while (i1 != limit1)
	    print_1sdiff_line (0, ')', &files[1].linbuf[i1++]);
	}
      while (i0 != limit0)
	print_1sdiff_line (&files[0].linbuf[i0++], '(', 0);
    }

  next0 = limit0;
  next1 = limit1;
}

/* Print a hunk of an sdiff diff.
   This is a contiguous portion of a complete edit script,
   describing changes in consecutive lines.  */

static void
print_sdiff_hunk (struct change *hunk)
{
  lin first0, last0, first1, last1;
  register lin i, j;

  /* Determine range of line numbers involved in each file.  */
  enum changes changes =
    analyze_hunk (hunk, &first0, &last0, &first1, &last1);
  if (!changes)
    return;

  /* Print out lines up to this change.  */
  print_sdiff_common_lines (first0, first1);

  if (sdiff_merge_assist)
    {
      long int len0 = last0 - first0 + 1;
      long int len1 = last1 - first1 + 1;
      fprintf (outfile, "c%ld,%ld\n", len0, len1);
    }

  /* Print ``xxx  |  xxx '' lines */
  if (changes == CHANGED)
    {
      for (i = first0, j = first1;  i <= last0 && j <= last1;  i++, j++)
	print_1sdiff_line (&files[0].linbuf[i], '|', &files[1].linbuf[j]);
      changes = (i <= last0 ? OLD : 0) + (j <= last1 ? NEW : 0);
      next0 = first0 = i;
      next1 = first1 = j;
    }

  /* Print ``     >  xxx '' lines */
  if (changes & NEW)
    {
      for (j = first1; j <= last1; ++j)
	print_1sdiff_line (0, '>', &files[1].linbuf[j]);
      next1 = j;
    }

  /* Print ``xxx  <     '' lines */
  if (changes & OLD)
    {
      for (i = first0; i <= last0; ++i)
	print_1sdiff_line (&files[0].linbuf[i], '<', 0);
      next0 = i;
    }
}
