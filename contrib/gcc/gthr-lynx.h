/* Threads compatibility routines for libgcc2 and libobjc for
   LynxOS.  */
/* Compile this one with gcc.  */
/* Copyright (C) 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

#ifndef GCC_GTHR_LYNX_H
#define GCC_GTHR_LYNX_H

#ifdef _MULTITHREADED

/* Using the macro version of pthread_setspecific leads to a
   compilation error.  Instead we have two choices either kill all
   macros in pthread.h with defining _POSIX_THREADS_CALLS or undefine
   individual macros where we should fall back on the function
   implementation.  We choose the second approach.  */

#include <pthread.h>
#undef pthread_setspecific

/* When using static libc on LynxOS, we cannot define pthread_create
   weak.  If the multi-threaded application includes iostream.h,
   gthr-posix.h is included and pthread_create will be defined weak.
   If pthead_create is weak its defining module in libc is not
   necessarily included in the link and the symbol is resolved to zero.
   Therefore the first call to it will crash.

   Since -mthreads is a multilib switch on LynxOS we know that at this
   point we are compiling for multi-threaded.  Omitting the weak
   definitions at this point should have no effect.  */

#undef  GTHREAD_USE_WEAK
#define GTHREAD_USE_WEAK 0

#include "gthr-posix.h"

#else
#include "gthr-single.h"
#endif

#endif /* GCC_GTHR_LYNX_H */
