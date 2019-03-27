/* Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Richard Henderson <rth@redhat.com>.

   This file is part of the GNU OpenMP Library (libgomp).

   Libgomp is free software; you can redistribute it and/or modify it
   under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   Libgomp is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
   more details.

   You should have received a copy of the GNU Lesser General Public License 
   along with libgomp; see the file COPYING.LIB.  If not, write to the
   Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/* As a special exception, if you link this library with other files, some
   of which are compiled with GCC, to produce an executable, this library
   does not by itself cause the resulting executable to be covered by the
   GNU General Public License.  This exception does not however invalidate
   any other reasons why the executable file might be covered by the GNU
   General Public License.  */

/* This is a Linux specific implementation of a mutex synchronization
   mechanism for libgomp.  This type is private to the library.  This
   implementation uses atomic instructions and the futex syscall.  */

#ifndef GOMP_MUTEX_H
#define GOMP_MUTEX_H 1

typedef int gomp_mutex_t;

#define GOMP_MUTEX_INIT_0 1

static inline void gomp_mutex_init (gomp_mutex_t *mutex)
{
  *mutex = 0;
}

extern void gomp_mutex_lock_slow (gomp_mutex_t *mutex);
static inline void gomp_mutex_lock (gomp_mutex_t *mutex)
{
  if (!__sync_bool_compare_and_swap (mutex, 0, 1))
    gomp_mutex_lock_slow (mutex);
}

extern void gomp_mutex_unlock_slow (gomp_mutex_t *mutex);
static inline void gomp_mutex_unlock (gomp_mutex_t *mutex)
{
  int val = __sync_lock_test_and_set (mutex, 0);
  if (__builtin_expect (val > 1, 0))
    gomp_mutex_unlock_slow (mutex);
}

static inline void gomp_mutex_destroy (gomp_mutex_t *mutex)
{
}

#endif /* GOMP_MUTEX_H */
