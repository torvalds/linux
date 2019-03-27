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

/* This is the default PTHREADS implementation of the public OpenMP
   locking primitives.

   Because OpenMP uses different entry points for normal and recursive
   locks, and pthreads uses only one entry point, a system may be able
   to do better and streamline the locking as well as reduce the size
   of the types exported.  */

/* We need Unix98 extensions to get recursive locks.  On Tru64 UNIX V4.0F,
   the declarations are available without _XOPEN_SOURCE, which actually
   breaks compilation.  */
#ifndef __osf__
#define _XOPEN_SOURCE 500
#endif

#include "libgomp.h"


void
omp_init_lock (omp_lock_t *lock)
{
  pthread_mutex_init (lock, NULL);
}

void
omp_destroy_lock (omp_lock_t *lock)
{
  pthread_mutex_destroy (lock);
}

void
omp_set_lock (omp_lock_t *lock)
{
  pthread_mutex_lock (lock);
}

void
omp_unset_lock (omp_lock_t *lock)
{
  pthread_mutex_unlock (lock);
}

int
omp_test_lock (omp_lock_t *lock)
{
  return pthread_mutex_trylock (lock) == 0;
}

void
omp_init_nest_lock (omp_nest_lock_t *lock)
{
  pthread_mutexattr_t attr;

  pthread_mutexattr_init (&attr);
  pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init (&lock->lock, &attr);
  lock->count = 0;
  pthread_mutexattr_destroy (&attr);
}

void
omp_destroy_nest_lock (omp_nest_lock_t *lock)
{
  pthread_mutex_destroy (&lock->lock);
}

void
omp_set_nest_lock (omp_nest_lock_t *lock)
{
  pthread_mutex_lock (&lock->lock);
  lock->count++;
}

void
omp_unset_nest_lock (omp_nest_lock_t *lock)
{
  lock->count--;
  pthread_mutex_unlock (&lock->lock);
}

int
omp_test_nest_lock (omp_nest_lock_t *lock)
{
  if (pthread_mutex_trylock (&lock->lock) == 0)
    return ++lock->count;
  return 0;
}

ialias (omp_init_lock)
ialias (omp_init_nest_lock)
ialias (omp_destroy_lock)
ialias (omp_destroy_nest_lock)
ialias (omp_set_lock)
ialias (omp_set_nest_lock)
ialias (omp_unset_lock)
ialias (omp_unset_nest_lock)
ialias (omp_test_lock)
ialias (omp_test_nest_lock)
