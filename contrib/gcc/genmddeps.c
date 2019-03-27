/* genmddeps.c - creates a makefile dependency fragment for the md file.
   Copyright (C) 2004 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "bconfig.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "gensupport.h"
#include "errors.h"


struct filedep
{
  struct filedep *next;
  const char *pathname;
};

static struct filedep *deps, **last = &deps;

static void
add_filedep (const char *pathname)
{
  struct filedep *n = XNEW (struct filedep);
  n->pathname = pathname;
  *last = n;
  last = &n->next;
}

int
main (int argc, char **argv)
{
  struct filedep *d;

  progname = "genmddeps";
  include_callback = add_filedep;
  
  if (init_md_reader_args (argc, argv) != SUCCESS_EXIT_CODE)
    return (FATAL_EXIT_CODE);

  *last = NULL;

  /* Output a variable containing all of the include files.  */
  fputs ("MD_INCLUDES =", stdout);
  for (d = deps; d ; d = d->next)
    printf (" \\\n\t%s", d->pathname);
  putchar ('\n');

  /* Output make targets for these includes with empty actions.  This
     will guard against make errors when includes are removed.  */
  for (d = deps; d ; d = d->next)
    printf ("\n%s:\n", d->pathname);

  fflush (stdout);
  return (ferror (stdout) != 0 ? FATAL_EXIT_CODE : SUCCESS_EXIT_CODE);
}
