/* Context-format output routines for GNU DIFF.

   Copyright (C) 1988, 1989, 1991, 1992, 1993, 1994, 1995, 1998, 2001,
   2002, 2004 Free Software Foundation, Inc.

   This file is part of GNU DIFF.

   GNU DIFF is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GNU DIFF is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

#include "diff.h"
#include <inttostr.h>

#ifdef ST_MTIM_NSEC
# define TIMESPEC_NS(timespec) ((timespec).ST_MTIM_NSEC)
#else
# define TIMESPEC_NS(timespec) 0
#endif

size_t nstrftime (char *, size_t, char const *, struct tm const *, int, long);

static char const *find_function (char const * const *, lin);
static struct change *find_hunk (struct change *);
static void mark_ignorable (struct change *);
static void pr_context_hunk (struct change *);
static void pr_unidiff_hunk (struct change *);

/* Last place find_function started searching from.  */
static lin find_function_last_search;

/* The value find_function returned when it started searching there.  */
static lin find_function_last_match;

/* Print a label for a context diff, with a file name and date or a label.  */

static void
print_context_label (char const *mark,
		     struct file_data *inf,
		     char const *label)
{
  if (label)
    fprintf (outfile, "%s %s\n", mark, label);
  else
    {
      char buf[MAX (INT_STRLEN_BOUND (int) + 32,
		    INT_STRLEN_BOUND (time_t) + 11)];
      struct tm const *tm = localtime (&inf->stat.st_mtime);
      long nsec = TIMESPEC_NS (inf->stat.st_mtim);
      if (! (tm && nstrftime (buf, sizeof buf, time_format, tm, 0, nsec)))
	{
	  time_t sec = inf->stat.st_mtime;
	  verify (info_preserved, sizeof inf->stat.st_mtime <= sizeof sec);
	  sprintf (buf, "%jd.%.9ld", (intmax_t)sec, nsec);
	}
      fprintf (outfile, "%s %s\t%s\n", mark, inf->name, buf);
    }
}

/* Print a header for a context diff, with the file names and dates.  */

void
print_context_header (struct file_data inf[], bool unidiff)
{
  if (unidiff)
    {
      print_context_label ("---", &inf[0], file_label[0]);
      print_context_label ("+++", &inf[1], file_label[1]);
    }
  else
    {
      print_context_label ("***", &inf[0], file_label[0]);
      print_context_label ("---", &inf[1], file_label[1]);
    }
}

/* Print an edit script in context format.  */

void
print_context_script (struct change *script, bool unidiff)
{
  if (ignore_blank_lines || ignore_regexp.fastmap)
    mark_ignorable (script);
  else
    {
      struct change *e;
      for (e = script; e; e = e->link)
	e->ignore = false;
    }

  find_function_last_search = - files[0].prefix_lines;
  find_function_last_match = LIN_MAX;

  if (unidiff)
    print_script (script, find_hunk, pr_unidiff_hunk);
  else
    print_script (script, find_hunk, pr_context_hunk);
}

/* Print a pair of line numbers with a comma, translated for file FILE.
   If the second number is not greater, use the first in place of it.

   Args A and B are internal line numbers.
   We print the translated (real) line numbers.  */

static void
print_context_number_range (struct file_data const *file, lin a, lin b)
{
  long int trans_a, trans_b;
  translate_range (file, a, b, &trans_a, &trans_b);

  /* We can have B <= A in the case of a range of no lines.
     In this case, we should print the line number before the range,
     which is B.

     POSIX 1003.1-2001 requires two line numbers separated by a comma
     even if the line numbers are the same.  However, this does not
     match existing practice and is surely an error in the
     specification.  */

  if (trans_b <= trans_a)
    fprintf (outfile, "%ld", trans_b);
  else
    fprintf (outfile, "%ld,%ld", trans_a, trans_b);
}

/* Print FUNCTION in a context header.  */
static void
print_context_function (FILE *out, char const *function)
{
  int i;
  putc (' ', out);
  for (i = 0; i < 40 && function[i] != '\n'; i++)
    continue;
  fwrite (function, sizeof (char), i, out);
}

/* Print a portion of an edit script in context format.
   HUNK is the beginning of the portion to be printed.
   The end is marked by a `link' that has been nulled out.

   Prints out lines from both files, and precedes each
   line with the appropriate flag-character.  */

static void
pr_context_hunk (struct change *hunk)
{
  lin first0, last0, first1, last1, i;
  char const *prefix;
  char const *function;
  FILE *out;

  /* Determine range of line numbers involved in each file.  */

  enum changes changes = analyze_hunk (hunk, &first0, &last0, &first1, &last1);
  if (! changes)
    return;

  /* Include a context's width before and after.  */

  i = - files[0].prefix_lines;
  first0 = MAX (first0 - context, i);
  first1 = MAX (first1 - context, i);
  if (last0 < files[0].valid_lines - context)
    last0 += context;
  else
    last0 = files[0].valid_lines - 1;
  if (last1 < files[1].valid_lines - context)
    last1 += context;
  else
    last1 = files[1].valid_lines - 1;

  /* If desired, find the preceding function definition line in file 0.  */
  function = 0;
  if (function_regexp.fastmap)
    function = find_function (files[0].linbuf, first0);

  begin_output ();
  out = outfile;

  fprintf (out, "***************");

  if (function)
    print_context_function (out, function);

  fprintf (out, "\n*** ");
  print_context_number_range (&files[0], first0, last0);
  fprintf (out, " ****\n");

  if (changes & OLD)
    {
      struct change *next = hunk;

      for (i = first0; i <= last0; i++)
	{
	  /* Skip past changes that apply (in file 0)
	     only to lines before line I.  */

	  while (next && next->line0 + next->deleted <= i)
	    next = next->link;

	  /* Compute the marking for line I.  */

	  prefix = " ";
	  if (next && next->line0 <= i)
	    /* The change NEXT covers this line.
	       If lines were inserted here in file 1, this is "changed".
	       Otherwise it is "deleted".  */
	    prefix = (next->inserted > 0 ? "!" : "-");

	  print_1_line (prefix, &files[0].linbuf[i]);
	}
    }

  fprintf (out, "--- ");
  print_context_number_range (&files[1], first1, last1);
  fprintf (out, " ----\n");

  if (changes & NEW)
    {
      struct change *next = hunk;

      for (i = first1; i <= last1; i++)
	{
	  /* Skip past changes that apply (in file 1)
	     only to lines before line I.  */

	  while (next && next->line1 + next->inserted <= i)
	    next = next->link;

	  /* Compute the marking for line I.  */

	  prefix = " ";
	  if (next && next->line1 <= i)
	    /* The change NEXT covers this line.
	       If lines were deleted here in file 0, this is "changed".
	       Otherwise it is "inserted".  */
	    prefix = (next->deleted > 0 ? "!" : "+");

	  print_1_line (prefix, &files[1].linbuf[i]);
	}
    }
}

/* Print a pair of line numbers with a comma, translated for file FILE.
   If the second number is smaller, use the first in place of it.
   If the numbers are equal, print just one number.

   Args A and B are internal line numbers.
   We print the translated (real) line numbers.  */

static void
print_unidiff_number_range (struct file_data const *file, lin a, lin b)
{
  long int trans_a, trans_b;
  translate_range (file, a, b, &trans_a, &trans_b);

  /* We can have B < A in the case of a range of no lines.
     In this case, we print the line number before the range,
     which is B.  It would be more logical to print A, but
     'patch' expects B in order to detect diffs against empty files.  */
  if (trans_b <= trans_a)
    fprintf (outfile, trans_b < trans_a ? "%ld,0" : "%ld", trans_b);
  else
    fprintf (outfile, "%ld,%ld", trans_a, trans_b - trans_a + 1);
}

/* Print a portion of an edit script in unidiff format.
   HUNK is the beginning of the portion to be printed.
   The end is marked by a `link' that has been nulled out.

   Prints out lines from both files, and precedes each
   line with the appropriate flag-character.  */

static void
pr_unidiff_hunk (struct change *hunk)
{
  lin first0, last0, first1, last1;
  lin i, j, k;
  struct change *next;
  char const *function;
  FILE *out;

  /* Determine range of line numbers involved in each file.  */

  if (! analyze_hunk (hunk, &first0, &last0, &first1, &last1))
    return;

  /* Include a context's width before and after.  */

  i = - files[0].prefix_lines;
  first0 = MAX (first0 - context, i);
  first1 = MAX (first1 - context, i);
  if (last0 < files[0].valid_lines - context)
    last0 += context;
  else
    last0 = files[0].valid_lines - 1;
  if (last1 < files[1].valid_lines - context)
    last1 += context;
  else
    last1 = files[1].valid_lines - 1;

  /* If desired, find the preceding function definition line in file 0.  */
  function = 0;
  if (function_regexp.fastmap)
    function = find_function (files[0].linbuf, first0);

  begin_output ();
  out = outfile;

  fprintf (out, "@@ -");
  print_unidiff_number_range (&files[0], first0, last0);
  fprintf (out, " +");
  print_unidiff_number_range (&files[1], first1, last1);
  fprintf (out, " @@");

  if (function)
    print_context_function (out, function);

  putc ('\n', out);

  next = hunk;
  i = first0;
  j = first1;

  while (i <= last0 || j <= last1)
    {

      /* If the line isn't a difference, output the context from file 0. */

      if (!next || i < next->line0)
	{
	  putc (initial_tab ? '\t' : ' ', out);
	  print_1_line (0, &files[0].linbuf[i++]);
	  j++;
	}
      else
	{
	  /* For each difference, first output the deleted part. */

	  k = next->deleted;
	  while (k--)
	    {
	      putc ('-', out);
	      if (initial_tab)
		putc ('\t', out);
	      print_1_line (0, &files[0].linbuf[i++]);
	    }

	  /* Then output the inserted part. */

	  k = next->inserted;
	  while (k--)
	    {
	      putc ('+', out);
	      if (initial_tab)
		putc ('\t', out);
	      print_1_line (0, &files[1].linbuf[j++]);
	    }

	  /* We're done with this hunk, so on to the next! */

	  next = next->link;
	}
    }
}

/* Scan a (forward-ordered) edit script for the first place that more than
   2*CONTEXT unchanged lines appear, and return a pointer
   to the `struct change' for the last change before those lines.  */

static struct change *
find_hunk (struct change *start)
{
  struct change *prev;
  lin top0, top1;
  lin thresh;

  /* Threshold distance is 2 * CONTEXT + 1 between two non-ignorable
     changes, but only CONTEXT if one is ignorable.  Watch out for
     integer overflow, though.  */
  lin non_ignorable_threshold =
    (LIN_MAX - 1) / 2 < context ? LIN_MAX : 2 * context + 1;
  lin ignorable_threshold = context;

  do
    {
      /* Compute number of first line in each file beyond this changed.  */
      top0 = start->line0 + start->deleted;
      top1 = start->line1 + start->inserted;
      prev = start;
      start = start->link;
      thresh = (prev->ignore || (start && start->ignore)
		? ignorable_threshold
		: non_ignorable_threshold);
      /* It is not supposed to matter which file we check in the end-test.
	 If it would matter, crash.  */
      if (start && start->line0 - top0 != start->line1 - top1)
	abort ();
    } while (start
	     /* Keep going if less than THRESH lines
		elapse before the affected line.  */
	     && start->line0 - top0 < thresh);

  return prev;
}

/* Set the `ignore' flag properly in each change in SCRIPT.
   It should be 1 if all the lines inserted or deleted in that change
   are ignorable lines.  */

static void
mark_ignorable (struct change *script)
{
  while (script)
    {
      struct change *next = script->link;
      lin first0, last0, first1, last1;

      /* Turn this change into a hunk: detach it from the others.  */
      script->link = 0;

      /* Determine whether this change is ignorable.  */
      script->ignore = ! analyze_hunk (script,
				       &first0, &last0, &first1, &last1);

      /* Reconnect the chain as before.  */
      script->link = next;

      /* Advance to the following change.  */
      script = next;
    }
}

/* Find the last function-header line in LINBUF prior to line number LINENUM.
   This is a line containing a match for the regexp in `function_regexp'.
   Return the address of the text, or 0 if no function-header is found.  */

static char const *
find_function (char const * const *linbuf, lin linenum)
{
  lin i = linenum;
  lin last = find_function_last_search;
  find_function_last_search = i;

  while (last <= --i)
    {
      /* See if this line is what we want.  */
      char const *line = linbuf[i];
      size_t linelen = linbuf[i + 1] - line - 1;

      /* FIXME: re_search's size args should be size_t, not int.  */
      int len = MIN (linelen, INT_MAX);

      if (0 <= re_search (&function_regexp, line, len, 0, len, 0))
	{
	  find_function_last_match = i;
	  return line;
	}
    }
  /* If we search back to where we started searching the previous time,
     find the line we found last time.  */
  if (find_function_last_match != LIN_MAX)
    return linbuf[find_function_last_match];

  return 0;
}
