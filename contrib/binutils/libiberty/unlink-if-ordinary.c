/* unlink-if-ordinary.c - remove link to a file unless it is special
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

This file is part of the libiberty library.  This library is free
software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option)
any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU CC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.

As a special exception, if you link this library with files
compiled with a GNU compiler to produce an executable, this does not cause
the resulting executable to be covered by the GNU General Public License.
This exception does not however invalidate any other reasons why
the executable file might be covered by the GNU General Public License. */

/*

@deftypefn Supplemental int unlink_if_ordinary (const char*)

Unlinks the named file, unless it is special (e.g. a device file).
Returns 0 when the file was unlinked, a negative value (and errno set) when
there was an error deleting the file, and a positive value if no attempt
was made to unlink the file because it is special.

@end deftypefn

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#include "libiberty.h"

#ifndef S_ISLNK
#ifdef S_IFLNK
#define S_ISLNK(m) (((m) & S_IFMT) == S_IFLNK)
#else
#define S_ISLNK(m) 0
#define lstat stat
#endif
#endif

int
unlink_if_ordinary (const char *name)
{
  struct stat st;

  if (lstat (name, &st) == 0
      && (S_ISREG (st.st_mode) || S_ISLNK (st.st_mode)))
    return unlink (name);

  return 1;
}
