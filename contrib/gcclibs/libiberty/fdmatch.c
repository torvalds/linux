/* Compare two open file descriptors to see if they refer to the same file.
   Copyright (C) 1991 Free Software Foundation, Inc.

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

@deftypefn Extension int fdmatch (int @var{fd1}, int @var{fd2})

Check to see if two open file descriptors refer to the same file.
This is useful, for example, when we have an open file descriptor for
an unnamed file, and the name of a file that we believe to correspond
to that fd.  This can happen when we are exec'd with an already open
file (@code{stdout} for example) or from the SVR4 @file{/proc} calls
that return open file descriptors for mapped address spaces.  All we
have to do is open the file by name and check the two file descriptors
for a match, which is done by comparing major and minor device numbers
and inode numbers.

@end deftypefn

BUGS

	(FIXME: does this work for networks?)
	It works for NFS, which assigns a device number to each mount.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "ansidecl.h"
#include "libiberty.h"
#include <sys/types.h>
#include <sys/stat.h>

int fdmatch (int fd1, int fd2)
{
  struct stat sbuf1;
  struct stat sbuf2;

  if ((fstat (fd1, &sbuf1) == 0) &&
      (fstat (fd2, &sbuf2) == 0) &&
      (sbuf1.st_dev == sbuf2.st_dev) &&
      (sbuf1.st_ino == sbuf2.st_ino))
    {
      return (1);
    }
  else
    {
      return (0);
    }
}
