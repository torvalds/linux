/* File name comparison routine.

   Copyright (C) 2007 Free Software Foundation, Inc.

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
   Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include "filenames.h"
#include "safe-ctype.h"

/*

@deftypefn Extension int filename_cmp (const char *@var{s1}, const char *@var{s2})

Return zero if the two file names @var{s1} and @var{s2} are equivalent.
If not equivalent, the returned value is similar to what @code{strcmp}
would return.  In other words, it returns a negative value if @var{s1}
is less than @var{s2}, or a positive value if @var{s2} is greater than
@var{s2}.

This function does not normalize file names.  As a result, this function
will treat filenames that are spelled differently as different even in
the case when the two filenames point to the same underlying file.
However, it does handle the fact that on DOS-like file systems, forward
and backward slashes are equal.

@end deftypefn

*/

int
filename_cmp (const char *s1, const char *s2)
{
#ifndef HAVE_DOS_BASED_FILE_SYSTEM
  return strcmp(s1, s2);
#else
  for (;;)
    {
      int c1 = TOLOWER (*s1);
      int c2 = TOLOWER (*s2);

      /* On DOS-based file systems, the '/' and the '\' are equivalent.  */
      if (c1 == '/')
        c1 = '\\';
      if (c2 == '/')
        c2 = '\\';

      if (c1 != c2)
        return (c1 - c2);

      if (c1 == '\0')
        return 0;

      s1++;
      s2++;
    }
#endif
}

