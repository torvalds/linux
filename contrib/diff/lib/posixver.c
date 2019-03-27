/* Which POSIX version to conform to, for utilities.

   Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/* Written by Paul Eggert.  */

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include "posixver.h"

#include <limits.h>
#include <stdlib.h>

#if HAVE_UNISTD_H
# include <unistd.h>
#endif
#ifndef _POSIX2_VERSION
# define _POSIX2_VERSION 0
#endif

#ifndef DEFAULT_POSIX2_VERSION
# define DEFAULT_POSIX2_VERSION _POSIX2_VERSION
#endif

/* The POSIX version that utilities should conform to.  The default is
   specified by the system.  */

int
posix2_version (void)
{
  long int v = DEFAULT_POSIX2_VERSION;
  char const *s = getenv ("_POSIX2_VERSION");

  if (s && *s)
    {
      char *e;
      long int i = strtol (s, &e, 10);
      if (! *e)
	v = i;
    }

  return v < INT_MIN ? INT_MIN : v < INT_MAX ? v : INT_MAX;
}
