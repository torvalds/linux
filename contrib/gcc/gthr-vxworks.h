/* Threads compatibility routines for libgcc2 and libobjc for VxWorks.  */
/* Compile this one with gcc.  */
/* Copyright (C) 1997, 1999, 2000 Free Software Foundation, Inc.
   Contributed by Mike Stump <mrs@wrs.com>.

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

#ifndef GCC_GTHR_VXWORKS_H
#define GCC_GTHR_VXWORKS_H

#ifdef _LIBOBJC

/* libobjc requires the optional pthreads component.  */
#include "gthr-posix.h"

#else

#define __GTHREADS 1
#define __gthread_active_p() 1

/* Mutexes are easy, except that they need to be initialized at runtime.  */

#include <semLib.h>

typedef SEM_ID __gthread_mutex_t;
/* All VxWorks mutexes are recursive.  */
typedef SEM_ID __gthread_recursive_mutex_t;
#define __GTHREAD_MUTEX_INIT_FUNCTION __gthread_mutex_init_function
#define __GTHREAD_RECURSIVE_MUTEX_INIT_FUNCTION __gthread_recursive_mutex_init_function

static inline void
__gthread_mutex_init_function (__gthread_mutex_t *mutex)
{
  *mutex = semMCreate (SEM_Q_PRIORITY | SEM_INVERSION_SAFE | SEM_DELETE_SAFE);
}

static inline int
__gthread_mutex_lock (__gthread_mutex_t *mutex)
{
  return semTake (*mutex, WAIT_FOREVER);
}

static inline int
__gthread_mutex_trylock (__gthread_mutex_t *mutex)
{
  return semTake (*mutex, NO_WAIT);
}

static inline int
__gthread_mutex_unlock (__gthread_mutex_t *mutex)
{
  return semGive (*mutex);
}

static inline void
__gthread_recursive_mutex_init_function (__gthread_recursive_mutex_t *mutex)
{
  __gthread_mutex_init_function (mutex);
}

static inline int
__gthread_recursive_mutex_lock (__gthread_recursive_mutex_t *mutex)
{
  return __gthread_mutex_lock (mutex);
}

static inline int
__gthread_recursive_mutex_trylock (__gthread_recursive_mutex_t *mutex)
{
  return __gthread_mutex_trylock (mutex);
}

static inline int
__gthread_recursive_mutex_unlock (__gthread_recursive_mutex_t *mutex)
{
  return __gthread_mutex_unlock (mutex);
}

/* pthread_once is complicated enough that it's implemented
   out-of-line.  See config/vxlib.c.  */

typedef struct
{
  volatile unsigned char busy;
  volatile unsigned char done;
}
__gthread_once_t;

#define __GTHREAD_ONCE_INIT { 0, 0 }

extern int __gthread_once (__gthread_once_t *once, void (*func)(void));

/* Thread-specific data requires a great deal of effort, since VxWorks
   is not really set up for it.  See config/vxlib.c for the gory
   details.  All the TSD routines are sufficiently complex that they
   need to be implemented out of line.  */

typedef unsigned int __gthread_key_t;

extern int __gthread_key_create (__gthread_key_t *keyp, void (*dtor)(void *));
extern int __gthread_key_delete (__gthread_key_t key);

extern void *__gthread_getspecific (__gthread_key_t key);
extern int __gthread_setspecific (__gthread_key_t key, void *ptr);

#endif /* not _LIBOBJC */

#endif /* gthr-vxworks.h */
