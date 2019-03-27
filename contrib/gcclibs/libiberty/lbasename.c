/* Libiberty basename.  Like basename, but is not overridden by the
   system C library.
   Copyright (C) 2001, 2002 Free Software Foundation, Inc.

This file is part of the libiberty library.
Libiberty is free software; you can redistribute it and/or
modify it under the terms of the GNU Library General Public
License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Libiberty is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Library General Public License for more details.

You should have received a copy of the GNU Library General Public
License along with libiberty; see the file COPYING.LIB.  If
not, write to the Free Software Foundation, Inc., 51 Franklin Street - Fifth Floor,
Boston, MA 02110-1301, USA.  */

/*

@deftypefn Replacement {const char*} lbasename (const char *@var{name})

Given a pointer to a string containing a typical pathname
(@samp{/usr/src/cmd/ls/ls.c} for example), returns a pointer to the
last component of the pathname (@samp{ls.c} in this case).  The
returned pointer is guaranteed to lie within the original
string.  This latter fact is not true of many vendor C
libraries, which return special strings or modify the passed
strings for particular input.

In particular, the empty string returns the same empty string,
and a path ending in @code{/} returns the empty string after it.

@end deftypefn

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ansidecl.h"
#include "libiberty.h"
#include "safe-ctype.h"
#include "filenames.h"

const char *
lbasename (const char *name)
{
  const char *base;

#if defined (HAVE_DOS_BASED_FILE_SYSTEM)
  /* Skip over a possible disk name.  */
  if (ISALPHA (name[0]) && name[1] == ':') 
    name += 2;
#endif

  for (base = name; *name; name++)
    if (IS_DIR_SEPARATOR (*name))
      base = name + 1;

  return base;
}
