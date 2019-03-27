/* Checking macros for stdio functions.
   Copyright (C) 2004, 2005 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* As a special exception, if you link this library with files compiled with
   GCC to produce an executable, this does not cause the resulting executable
   to be covered by the GNU General Public License. This exception does not
   however invalidate any other reasons why the executable file might be
   covered by the GNU General Public License.  */

#ifndef _SSP_STDIO_H
#define _SSP_STDIO_H 1

#include <ssp.h>
#include_next <stdio.h>

#if __SSP_FORTIFY_LEVEL > 0

#include <stdarg.h>

#undef sprintf
#undef vsprintf
#undef snprintf
#undef vsnprintf
#undef gets
#undef fgets

extern int __sprintf_chk (char *__restrict__ __s, int __flag, size_t __slen,
			  __const char *__restrict__ __format, ...);
extern int __vsprintf_chk (char *__restrict__ __s, int __flag, size_t __slen,
			   __const char *__restrict__ __format,
			   va_list __ap);

#define sprintf(str, ...) \
  __builtin___sprintf_chk (str, 0, __ssp_bos (str), \
			   __VA_ARGS__)
#define vsprintf(str, fmt, ap) \
  __builtin___vsprintf_chk (str, 0, __ssp_bos (str), fmt, ap)

extern int __snprintf_chk (char *__restrict__ __s, size_t __n, int __flag,
			   size_t __slen, __const char *__restrict__ __format,
			   ...);
extern int __vsnprintf_chk (char *__restrict__ __s, size_t __n, int __flag,
			    size_t __slen, __const char *__restrict__ __format,
			    va_list __ap);

#define snprintf(str, len, ...) \
  __builtin___snprintf_chk (str, len, 0, __ssp_bos (str), __VA_ARGS__)
#define vsnprintf(str, len, fmt, ap) \
  __builtin___vsnprintf_chk (str, len, 0, __ssp_bos (str), fmt, ap)

extern char *__gets_chk (char *__str, size_t);
extern char *__SSP_REDIRECT (__gets_alias, (char *__str), gets);

extern inline __attribute__((__always_inline__)) char *
gets (char *__str)
{
  if (__ssp_bos (__str) != (size_t) -1)
    return __gets_chk (__str, __ssp_bos (__str));
  return __gets_alias (__str);
}

extern char *__SSP_REDIRECT (__fgets_alias,
			     (char *__restrict__ __s, int __n,
			      FILE *__restrict__ __stream), fgets);

extern inline __attribute__((__always_inline__)) char *
fgets (char *__restrict__ __s, int __n, FILE *__restrict__ __stream)
{
  if (__ssp_bos (__s) != (size_t) -1 && (size_t) __n > __ssp_bos (__s))
    __chk_fail ();
  return __fgets_alias (__s, __n, __stream);
}

#endif /* __SSP_FORTIFY_LEVEL > 0 */
#endif /* _SSP_STDIO_H */
