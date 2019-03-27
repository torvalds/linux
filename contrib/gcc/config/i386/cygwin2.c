/* Helper routines for cygwin-specific command-line parsing.
   Contributed by Christopher Faylor (cgf@redhat.com)
   Copyright 2003 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"

#include "safe-ctype.h"
#include <string.h>

/*
static void remove_w32api (void);
*/
static void add_mingw (void);
static void set_mingw (void) __attribute__ ((constructor));

static void
add_mingw (void)
{
  char **av;
  char *p;
  for (av = cvt_to_mingw; *av; av++)
    {
      int sawcygwin = 0;
      while ((p = strstr (*av, "-cygwin")))
	{
	  char *over = p + sizeof ("-cygwin") - 1;
	  memmove (over + 1, over, strlen (over));
	  memcpy (p, "-mingw32", sizeof("-mingw32") - 1);
	  p = ++over;
	  while (ISALNUM (*p))
	    p++;
	  strcpy (over, p);
	  sawcygwin = 1;
	}
      if (!sawcygwin && !strstr (*av, "mingw"))
	strcat (*av, CYGWIN_MINGW_SUBDIR);
    }
}


static void
set_mingw (void)
{
  char *env = getenv ("GCC_CYGWIN_MINGW");
  if (env && *env == '1')
    add_mingw ();
}
