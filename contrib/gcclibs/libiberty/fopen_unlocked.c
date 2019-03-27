/* Implement fopen_unlocked and related functions.
   Copyright (C) 2005 Free Software Foundation, Inc.
   Written by Kaveh R. Ghazi <ghazi@caip.rutgers.edu>.

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

@deftypefn Extension void unlock_stream (FILE * @var{stream})

If the OS supports it, ensure that the supplied stream is setup to
avoid any multi-threaded locking.  Otherwise leave the @code{FILE}
pointer unchanged.  If the @var{stream} is @code{NULL} do nothing.

@end deftypefn

@deftypefn Extension void unlock_std_streams (void)

If the OS supports it, ensure that the standard I/O streams,
@code{stdin}, @code{stdout} and @code{stderr} are setup to avoid any
multi-threaded locking.  Otherwise do nothing.

@end deftypefn

@deftypefn Extension {FILE *} fopen_unlocked (const char *@var{path}, const char * @var{mode})

Opens and returns a @code{FILE} pointer via @code{fopen}.  If the
operating system supports it, ensure that the stream is setup to avoid
any multi-threaded locking.  Otherwise return the @code{FILE} pointer
unchanged.

@end deftypefn

@deftypefn Extension {FILE *} fdopen_unlocked (int @var{fildes}, const char * @var{mode})

Opens and returns a @code{FILE} pointer via @code{fdopen}.  If the
operating system supports it, ensure that the stream is setup to avoid
any multi-threaded locking.  Otherwise return the @code{FILE} pointer
unchanged.

@end deftypefn

@deftypefn Extension {FILE *} freopen_unlocked (const char * @var{path}, const char * @var{mode}, FILE * @var{stream})

Opens and returns a @code{FILE} pointer via @code{freopen}.  If the
operating system supports it, ensure that the stream is setup to avoid
any multi-threaded locking.  Otherwise return the @code{FILE} pointer
unchanged.

@end deftypefn

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdio.h>
#ifdef HAVE_STDIO_EXT_H
#include <stdio_ext.h>
#endif

#include "libiberty.h"

/* This is an inline helper function to consolidate attempts to unlock
   a stream.  */

static inline void
unlock_1 (FILE *const fp ATTRIBUTE_UNUSED)
{
#if defined(HAVE___FSETLOCKING) && defined(FSETLOCKING_BYCALLER)
  if (fp)
    __fsetlocking (fp, FSETLOCKING_BYCALLER);
#endif
}

void
unlock_stream (FILE *fp)
{
  unlock_1 (fp);
}

void
unlock_std_streams (void)
{
  unlock_1 (stdin);
  unlock_1 (stdout);
  unlock_1 (stderr);
}

FILE *
fopen_unlocked (const char *path, const char *mode)		
{
  FILE *const fp = fopen (path, mode);
  unlock_1 (fp);
  return fp;
}

FILE *
fdopen_unlocked (int fildes, const char *mode)
{
  FILE *const fp = fdopen (fildes, mode);
  unlock_1 (fp);
  return fp;
}

FILE *
freopen_unlocked (const char *path, const char *mode, FILE *stream)
{
  FILE *const fp = freopen (path, mode, stream);
  unlock_1 (fp);
  return fp;
}
