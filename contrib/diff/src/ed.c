/* Output routines for ed-script format.

   Copyright (C) 1988, 1989, 1991, 1992, 1993, 1995, 1998, 2001, 2004
   Free Software Foundation, Inc.

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

static void print_ed_hunk (struct change *);
static void print_rcs_hunk (struct change *);
static void pr_forward_ed_hunk (struct change *);

/* Print our script as ed commands.  */

void
print_ed_script (struct change *script)
{
  print_script (script, find_reverse_change, print_ed_hunk);
}

/* Print a hunk of an ed diff */

static void
print_ed_hunk (struct change *hunk)
{
  lin f0, l0, f1, l1;
  enum changes changes;

#ifdef DEBUG
  debug_script (hunk);
#endif

  /* Determine range of line numbers involved in each file.  */
  changes = analyze_hunk (hunk, &f0, &l0, &f1, &l1);
  if (!changes)
    return;

  begin_output ();

  /* Print out the line number header for this hunk */
  print_number_range (',', &files[0], f0, l0);
  fprintf (outfile, "%c\n", change_letter[changes]);

  /* Print new/changed lines from second file, if needed */
  if (changes != OLD)
    {
      lin i;
      for (i = f1; i <= l1; i++)
	{
	  if (files[1].linbuf[i][0] == '.' && files[1].linbuf[i][1] == '\n')
	    {
	      /* The file's line is just a dot, and it would exit
		 insert mode.  Precede the dot with another dot, exit
		 insert mode, remove the extra dot, and then resume
		 insert mode.  */
	      fprintf (outfile, "..\n.\ns/.//\na\n");
	    }
	  else
	    print_1_line ("", &files[1].linbuf[i]);
	}

      fprintf (outfile, ".\n");
    }
}

/* Print change script in the style of ed commands,
   but print the changes in the order they appear in the input files,
   which means that the commands are not truly useful with ed.  */

void
pr_forward_ed_script (struct change *script)
{
  print_script (script, find_change, pr_forward_ed_hunk);
}

static void
pr_forward_ed_hunk (struct change *hunk)
{
  lin i, f0, l0, f1, l1;

  /* Determine range of line numbers involved in each file.  */
  enum changes changes = analyze_hunk (hunk, &f0, &l0, &f1, &l1);
  if (!changes)
    return;

  begin_output ();

  fprintf (outfile, "%c", change_letter[changes]);
  print_number_range (' ', files, f0, l0);
  fprintf (outfile, "\n");

  /* If deletion only, print just the number range.  */

  if (changes == OLD)
    return;

  /* For insertion (with or without deletion), print the number range
     and the lines from file 2.  */

  for (i = f1; i <= l1; i++)
    print_1_line ("", &files[1].linbuf[i]);

  fprintf (outfile, ".\n");
}

/* Print in a format somewhat like ed commands
   except that each insert command states the number of lines it inserts.
   This format is used for RCS.  */

void
print_rcs_script (struct change *script)
{
  print_script (script, find_change, print_rcs_hunk);
}

/* Print a hunk of an RCS diff */

static void
print_rcs_hunk (struct change *hunk)
{
  lin i, f0, l0, f1, l1;
  long int tf0, tl0, tf1, tl1;

  /* Determine range of line numbers involved in each file.  */
  enum changes changes = analyze_hunk (hunk, &f0, &l0, &f1, &l1);
  if (!changes)
    return;

  begin_output ();

  translate_range (&files[0], f0, l0, &tf0, &tl0);

  if (changes & OLD)
    {
      fprintf (outfile, "d");
      /* For deletion, print just the starting line number from file 0
	 and the number of lines deleted.  */
      fprintf (outfile, "%ld %ld\n", tf0, tf0 <= tl0 ? tl0 - tf0 + 1 : 1);
    }

  if (changes & NEW)
    {
      fprintf (outfile, "a");

      /* Take last-line-number from file 0 and # lines from file 1.  */
      translate_range (&files[1], f1, l1, &tf1, &tl1);
      fprintf (outfile, "%ld %ld\n", tl0, tf1 <= tl1 ? tl1 - tf1 + 1 : 1);

      /* Print the inserted lines.  */
      for (i = f1; i <= l1; i++)
	print_1_line ("", &files[1].linbuf[i]);
    }
}
