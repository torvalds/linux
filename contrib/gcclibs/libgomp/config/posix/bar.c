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

/* This is the default implementation of a barrier synchronization mechanism
   for libgomp.  This type is private to the library.  Note that we rely on
   being able to adjust the barrier count while threads are blocked, so the
   POSIX pthread_barrier_t won't work.  */

#include "libgomp.h"


void
gomp_barrier_init (gomp_barrier_t *bar, unsigned count)
{
  gomp_mutex_init (&bar->mutex1);
#ifndef HAVE_SYNC_BUILTINS
  gomp_mutex_init (&bar->mutex2);
#endif
  gomp_sem_init (&bar->sem1, 0);
  gomp_sem_init (&bar->sem2, 0);
  bar->total = count;
  bar->arrived = 0;
}

void
gomp_barrier_destroy (gomp_barrier_t *bar)
{
  /* Before destroying, make sure all threads have left the barrier.  */
  gomp_mutex_lock (&bar->mutex1);
  gomp_mutex_unlock (&bar->mutex1);

  gomp_mutex_destroy (&bar->mutex1);
#ifndef HAVE_SYNC_BUILTINS
  gomp_mutex_destroy (&bar->mutex2);
#endif
  gomp_sem_destroy (&bar->sem1);
  gomp_sem_destroy (&bar->sem2);
}

void
gomp_barrier_reinit (gomp_barrier_t *bar, unsigned count)
{
  gomp_mutex_lock (&bar->mutex1);
  bar->total = count;
  gomp_mutex_unlock (&bar->mutex1);
}

void
gomp_barrier_wait_end (gomp_barrier_t *bar, bool last)
{
  unsigned int n;

  if (last)
    {
      n = --bar->arrived;
      if (n > 0)
	{
	  do
	    gomp_sem_post (&bar->sem1);
	  while (--n != 0);
	  gomp_sem_wait (&bar->sem2);
	}
      gomp_mutex_unlock (&bar->mutex1);
    }
  else
    {
      gomp_mutex_unlock (&bar->mutex1);
      gomp_sem_wait (&bar->sem1);

#ifdef HAVE_SYNC_BUILTINS
      n = __sync_add_and_fetch (&bar->arrived, -1);
#else
      gomp_mutex_lock (&bar->mutex2);
      n = --bar->arrived;
      gomp_mutex_unlock (&bar->mutex2);
#endif

      if (n == 0)
	gomp_sem_post (&bar->sem2);
    }
}

void
gomp_barrier_wait (gomp_barrier_t *barrier)
{
  gomp_barrier_wait_end (barrier, gomp_barrier_wait_start (barrier));
}
