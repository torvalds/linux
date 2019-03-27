/* Simple implementation of vsprintf for systems without it.
   Highly system-dependent, but should work on most "traditional"
   implementations of stdio; newer ones should already have vsprintf.
   Written by Per Bothner of Cygnus Support.
   Based on libg++'s "form" (written by Doug Lea; dl@rocky.oswego.edu).
   Copyright (C) 1991, 1995, 2002 Free Software Foundation, Inc.

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

#include <ansidecl.h>
#include <stdarg.h>
#include <stdio.h>
#undef vsprintf

#if defined _IOSTRG && defined _IOWRT

int
vsprintf (char *buf, const char *format, va_list ap)
{
  FILE b;
  int ret;
#ifdef VMS
  b->_flag = _IOWRT|_IOSTRG;
  b->_ptr = buf;
  b->_cnt = 12000;
#else
  b._flag = _IOWRT|_IOSTRG;
  b._ptr = buf;
  b._cnt = 12000;
#endif
  ret = _doprnt(format, ap, &b);
  putc('\0', &b);
  return ret;

}

#endif
