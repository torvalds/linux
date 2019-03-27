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

/* This is a Linux specific implementation of the public OpenMP locking
   primitives.  This implementation uses atomic instructions and the futex
   syscall.  */

#include "libgomp.h"
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "futex.h"


/* The internal gomp_mutex_t and the external non-recursive omp_lock_t
   have the same form.  Re-use it.  */

void
omp_init_lock (omp_lock_t *lock)
{
  gomp_mutex_init (lock);
}

void
omp_destroy_lock (omp_lock_t *lock)
{
  gomp_mutex_destroy (lock);
}

void
omp_set_lock (omp_lock_t *lock)
{
  gomp_mutex_lock (lock);
}

void
omp_unset_lock (omp_lock_t *lock)
{
  gomp_mutex_unlock (lock);
}

int
omp_test_lock (omp_lock_t *lock)
{
  return __sync_bool_compare_and_swap (lock, 0, 1);
}

/* The external recursive omp_nest_lock_t form requires additional work.  */

/* We need an integer to uniquely identify this thread.  Most generally
   this is the thread's TID, which ideally we'd get this straight from
   the TLS block where glibc keeps it.  Unfortunately, we can't get at
   that directly.

   If we don't support (or have disabled) TLS, one function call is as
   good (or bad) as any other.  Use the syscall all the time.

   On an ILP32 system (defined here as not LP64), we can make do with
   any thread-local pointer.  Ideally we'd use the TLS base address,
   since that requires the least amount of arithmetic, but that's not
   always available directly.  Make do with the gomp_thread pointer
   since it's handy.  */

#if !defined (HAVE_TLS)
static inline int gomp_tid (void)
{
  return syscall (SYS_gettid);
}
#elif !defined(__LP64__)
static inline int gomp_tid (void)
{
  return (int) gomp_thread ();
}
#else
static __thread int tid_cache;
static inline int gomp_tid (void)
{
  int tid = tid_cache;
  if (__builtin_expect (tid == 0, 0))
    tid_cache = tid = syscall (SYS_gettid);
  return tid;
}
#endif


void
omp_init_nest_lock (omp_nest_lock_t *lock)
{
  memset (lock, 0, sizeof (lock));
}

void
omp_destroy_nest_lock (omp_nest_lock_t *lock)
{
}

void
omp_set_nest_lock (omp_nest_lock_t *lock)
{
  int otid, tid = gomp_tid ();

  while (1)
    {
      otid = __sync_val_compare_and_swap (&lock->owner, 0, tid);
      if (otid == 0)
	{
	  lock->count = 1;
	  return;
	}
      if (otid == tid)
	{
	  lock->count++;
	  return;
	}

      futex_wait (&lock->owner, otid);
    }
}

void
omp_unset_nest_lock (omp_nest_lock_t *lock)
{
  /* ??? Validate that we own the lock here.  */

  if (--lock->count == 0)
    {
      __sync_lock_release (&lock->owner);
      futex_wake (&lock->owner, 1);
    }
}

int
omp_test_nest_lock (omp_nest_lock_t *lock)
{
  int otid, tid = gomp_tid ();

  otid = __sync_val_compare_and_swap (&lock->owner, 0, tid);
  if (otid == 0)
    {
      lock->count = 1;
      return 1;
    }
  if (otid == tid)
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
