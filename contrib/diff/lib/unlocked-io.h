/* Prefer faster, non-thread-safe stdio functions if available.

   Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.

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

/* Written by Jim Meyering.  */

#ifndef UNLOCKED_IO_H
# define UNLOCKED_IO_H 1

# ifndef USE_UNLOCKED_IO
#  define USE_UNLOCKED_IO 1
# endif

# if USE_UNLOCKED_IO

/* These are wrappers for functions/macros from the GNU C library, and
   from other C libraries supporting POSIX's optional thread-safe functions.

   The standard I/O functions are thread-safe.  These *_unlocked ones are
   more efficient but not thread-safe.  That they're not thread-safe is
   fine since all of the applications in this package are single threaded.

   Also, some code that is shared with the GNU C library may invoke
   the *_unlocked functions directly.  On hosts that lack those
   functions, invoke the non-thread-safe versions instead.  */

#  include <stdio.h>

#  if HAVE_DECL_CLEARERR_UNLOCKED
#   undef clearerr
#   define clearerr(x) clearerr_unlocked (x)
#  else
#   define clearerr_unlocked(x) clearerr (x)
#  endif
#  if HAVE_DECL_FEOF_UNLOCKED
#   undef feof
#   define feof(x) feof_unlocked (x)
#  else
#   define feof_unlocked(x) feof (x)
#  endif
#  if HAVE_DECL_FERROR_UNLOCKED
#   undef ferror
#   define ferror(x) ferror_unlocked (x)
#  else
#   define ferror_unlocked(x) ferror (x)
#  endif
#  if HAVE_DECL_FFLUSH_UNLOCKED
#   undef fflush
#   define fflush(x) fflush_unlocked (x)
#  else
#   define fflush_unlocked(x) fflush (x)
#  endif
#  if HAVE_DECL_FGETS_UNLOCKED
#   undef fgets
#   define fgets(x,y,z) fgets_unlocked (x,y,z)
#  else
#   define fgets_unlocked(x,y,z) fgets (x,y,z)
#  endif
#  if HAVE_DECL_FPUTC_UNLOCKED
#   undef fputc
#   define fputc(x,y) fputc_unlocked (x,y)
#  else
#   define fputc_unlocked(x,y) fputc (x,y)
#  endif
#  if HAVE_DECL_FPUTS_UNLOCKED
#   undef fputs
#   define fputs(x,y) fputs_unlocked (x,y)
#  else
#   define fputs_unlocked(x,y) fputs (x,y)
#  endif
#  if HAVE_DECL_FREAD_UNLOCKED
#   undef fread
#   define fread(w,x,y,z) fread_unlocked (w,x,y,z)
#  else
#   define fread_unlocked(w,x,y,z) fread (w,x,y,z)
#  endif
#  if HAVE_DECL_FWRITE_UNLOCKED
#   undef fwrite
#   define fwrite(w,x,y,z) fwrite_unlocked (w,x,y,z)
#  else
#   define fwrite_unlocked(w,x,y,z) fwrite (w,x,y,z)
#  endif
#  if HAVE_DECL_GETC_UNLOCKED
#   undef getc
#   define getc(x) getc_unlocked (x)
#  else
#   define getc_unlocked(x) getc (x)
#  endif
#  if HAVE_DECL_GETCHAR_UNLOCKED
#   undef getchar
#   define getchar() getchar_unlocked ()
#  else
#   define getchar_unlocked() getchar ()
#  endif
#  if HAVE_DECL_PUTC_UNLOCKED
#   undef putc
#   define putc(x,y) putc_unlocked (x,y)
#  else
#   define putc_unlocked(x,y) putc (x,y)
#  endif
#  if HAVE_DECL_PUTCHAR_UNLOCKED
#   undef putchar
#   define putchar(x) putchar_unlocked (x)
#  else
#   define putchar_unlocked(x) putchar (x)
#  endif

#  undef flockfile
#  define flockfile(x) ((void) 0)

#  undef ftrylockfile
#  define ftrylockfile(x) 0

#  undef funlockfile
#  define funlockfile(x) ((void) 0)

# endif /* USE_UNLOCKED_IO */
#endif /* UNLOCKED_IO_H */
