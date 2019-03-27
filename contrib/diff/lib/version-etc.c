/* Utility to help print --version output in a consistent format.
   Copyright (C) 1999-2004 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by Jim Meyering. */

#if HAVE_CONFIG_H
# include <config.h>
#endif

/* Specification.  */
#include "version-etc.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "unlocked-io.h"

#include "gettext.h"
#define _(msgid) gettext (msgid)

/* Default copyright goes to the FSF. */

const char* version_etc_copyright =
  /* Do *not* mark this string for translation.  */
  "Copyright (C) 2004 Free Software Foundation, Inc.";


/* Like version_etc, below, but with the NULL-terminated author list
   provided via a variable of type va_list.  */
void
version_etc_va (FILE *stream,
		const char *command_name, const char *package,
		const char *version, va_list authors)
{
  unsigned int n_authors;

  /* Count the number of authors.  */
  {
    va_list tmp_authors;

#ifdef __va_copy
    __va_copy (tmp_authors, authors);
#else
    tmp_authors = authors;
#endif

    n_authors = 0;
    while (va_arg (tmp_authors, const char *) != NULL)
      ++n_authors;
  }

  if (command_name)
    fprintf (stream, "%s (%s) %s\n", command_name, package, version);
  else
    fprintf (stream, "%s %s\n", package, version);

  switch (n_authors)
    {
    case 0:
      /* The caller must provide at least one author name.  */
      abort ();
    case 1:
      /* TRANSLATORS: %s denotes an author name.  */
      vfprintf (stream, _("Written by %s.\n"), authors);
      break;
    case 2:
      /* TRANSLATORS: Each %s denotes an author name.  */
      vfprintf (stream, _("Written by %s and %s.\n"), authors);
      break;
    case 3:
      /* TRANSLATORS: Each %s denotes an author name.  */
      vfprintf (stream, _("Written by %s, %s, and %s.\n"), authors);
      break;
    case 4:
      /* TRANSLATORS: Each %s denotes an author name.
	 You can use line breaks, estimating that each author name occupies
	 ca. 16 screen columns and that a screen line has ca. 80 columns.  */
      vfprintf (stream, _("Written by %s, %s, %s,\nand %s.\n"), authors);
      break;
    case 5:
      /* TRANSLATORS: Each %s denotes an author name.
	 You can use line breaks, estimating that each author name occupies
	 ca. 16 screen columns and that a screen line has ca. 80 columns.  */
      vfprintf (stream, _("Written by %s, %s, %s,\n%s, and %s.\n"), authors);
      break;
    case 6:
      /* TRANSLATORS: Each %s denotes an author name.
	 You can use line breaks, estimating that each author name occupies
	 ca. 16 screen columns and that a screen line has ca. 80 columns.  */
      vfprintf (stream, _("Written by %s, %s, %s,\n%s, %s, and %s.\n"),
		authors);
      break;
    case 7:
      /* TRANSLATORS: Each %s denotes an author name.
	 You can use line breaks, estimating that each author name occupies
	 ca. 16 screen columns and that a screen line has ca. 80 columns.  */
      vfprintf (stream, _("Written by %s, %s, %s,\n%s, %s, %s, and %s.\n"),
		authors);
      break;
    case 8:
      /* TRANSLATORS: Each %s denotes an author name.
	 You can use line breaks, estimating that each author name occupies
	 ca. 16 screen columns and that a screen line has ca. 80 columns.  */
      vfprintf (stream, _("\
Written by %s, %s, %s,\n%s, %s, %s, %s,\nand %s.\n"),
		authors);
      break;
    case 9:
      /* TRANSLATORS: Each %s denotes an author name.
	 You can use line breaks, estimating that each author name occupies
	 ca. 16 screen columns and that a screen line has ca. 80 columns.  */
      vfprintf (stream, _("\
Written by %s, %s, %s,\n%s, %s, %s, %s,\n%s, and %s.\n"),
		authors);
      break;
    default:
      /* 10 or more authors.  Use an abbreviation, since the human reader
	 will probably not want to read the entire list anyway.  */
      /* TRANSLATORS: Each %s denotes an author name.
	 You can use line breaks, estimating that each author name occupies
	 ca. 16 screen columns and that a screen line has ca. 80 columns.  */
      vfprintf (stream, _("\
Written by %s, %s, %s,\n%s, %s, %s, %s,\n%s, %s, and others.\n"),
		authors);
      break;
    }
  va_end (authors);
  putc ('\n', stream);

  fputs (version_etc_copyright, stream);
  putc ('\n', stream);

  fputs (_("\
This is free software; see the source for copying conditions.  There is NO\n\
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"),
	 stream);
}


/* Display the --version information the standard way.

   If COMMAND_NAME is NULL, the PACKAGE is asumed to be the name of
   the program.  The formats are therefore:

   PACKAGE VERSION

   or

   COMMAND_NAME (PACKAGE) VERSION.

   The author names are passed as separate arguments, with an additional
   NULL argument at the end.  */
void
version_etc (FILE *stream,
	     const char *command_name, const char *package,
	     const char *version, /* const char *author1, ...*/ ...)
{
  va_list authors;

  va_start (authors, version);
  version_etc_va (stream, command_name, package, version, authors);
}
