/* depend.c - Handle dependency tracking.
   Copyright 1997, 1998, 2000, 2001, 2003 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "as.h"

/* The file to write to, or NULL if no dependencies being kept.  */
static char * dep_file = NULL;

struct dependency
  {
    char * file;
    struct dependency * next;
  };

/* All the files we depend on.  */
static struct dependency * dep_chain = NULL;

/* Current column in output file.  */
static int column = 0;

static int quote_string_for_make (FILE *, char *);
static void wrap_output (FILE *, char *, int);

/* Number of columns allowable.  */
#define MAX_COLUMNS 72

/* Start saving dependencies, to be written to FILENAME.  If this is
   never called, then dependency tracking is simply skipped.  */

void
start_dependencies (char *filename)
{
  dep_file = filename;
}

/* Noticed a new filename, so try to register it.  */

void
register_dependency (char *filename)
{
  struct dependency *dep;

  if (dep_file == NULL)
    return;

  for (dep = dep_chain; dep != NULL; dep = dep->next)
    {
      if (!strcmp (filename, dep->file))
	return;
    }

  dep = (struct dependency *) xmalloc (sizeof (struct dependency));
  dep->file = xstrdup (filename);
  dep->next = dep_chain;
  dep_chain = dep;
}

/* Quote a file name the way `make' wants it, and print it to FILE.
   If FILE is NULL, do no printing, but return the length of the
   quoted string.

   This code is taken from gcc with only minor changes.  */

static int
quote_string_for_make (FILE *file, char *src)
{
  char *p = src;
  int i = 0;

  for (;;)
    {
      char c = *p++;

      switch (c)
	{
	case '\0':
	case ' ':
	case '\t':
	  {
	    /* GNU make uses a weird quoting scheme for white space.
	       A space or tab preceded by 2N+1 backslashes represents
	       N backslashes followed by space; a space or tab
	       preceded by 2N backslashes represents N backslashes at
	       the end of a file name; and backslashes in other
	       contexts should not be doubled.  */
	    char *q;

	    for (q = p - 1; src < q && q[-1] == '\\'; q--)
	      {
		if (file)
		  putc ('\\', file);
		i++;
	      }
	  }
	  if (!c)
	    return i;
	  if (file)
	    putc ('\\', file);
	  i++;
	  goto ordinary_char;

	case '$':
	  if (file)
	    putc (c, file);
	  i++;
	  /* Fall through.  This can mishandle things like "$(" but
	     there's no easy fix.  */
	default:
	ordinary_char:
	  /* This can mishandle characters in the string "\0\n%*?[\\~";
	     exactly which chars are mishandled depends on the `make' version.
	     We know of no portable solution for this;
	     even GNU make 3.76.1 doesn't solve the problem entirely.
	     (Also, '\0' is mishandled due to our calling conventions.)  */
	  if (file)
	    putc (c, file);
	  i++;
	  break;
	}
    }
}

/* Append some output to the file, keeping track of columns and doing
   wrapping as necessary.  */

static void
wrap_output (FILE *f, char *string, int spacer)
{
  int len = quote_string_for_make (NULL, string);

  if (len == 0)
    return;

  if (column
      && (MAX_COLUMNS
	  - 1 /* spacer */
	  - 2 /* ` \'   */
	  < column + len))
    {
      fprintf (f, " \\\n ");
      column = 0;
      if (spacer == ' ')
	spacer = '\0';
    }

  if (spacer == ' ')
    {
      putc (spacer, f);
      ++column;
    }

  quote_string_for_make (f, string);
  column += len;

  if (spacer == ':')
    {
      putc (spacer, f);
      ++column;
    }
}

/* Print dependency file.  */

void
print_dependencies (void)
{
  FILE *f;
  struct dependency *dep;

  if (dep_file == NULL)
    return;

  f = fopen (dep_file, FOPEN_WT);
  if (f == NULL)
    {
      as_warn (_("can't open `%s' for writing"), dep_file);
      return;
    }

  column = 0;
  wrap_output (f, out_file_name, ':');
  for (dep = dep_chain; dep != NULL; dep = dep->next)
    wrap_output (f, dep->file, ' ');

  putc ('\n', f);

  if (fclose (f))
    as_warn (_("can't close `%s'"), dep_file);
}
