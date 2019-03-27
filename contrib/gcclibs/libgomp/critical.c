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

/* This file handles the CRITICAL construct.  */

#include "libgomp.h"
#include <stdlib.h>


static gomp_mutex_t default_lock;

void
GOMP_critical_start (void)
{
  gomp_mutex_lock (&default_lock);
}

void
GOMP_critical_end (void)
{
  gomp_mutex_unlock (&default_lock);
}

#ifndef HAVE_SYNC_BUILTINS
static gomp_mutex_t create_lock_lock;
#endif

void
GOMP_critical_name_start (void **pptr)
{
  gomp_mutex_t *plock;

  /* If a mutex fits within the space for a pointer, and is zero initialized,
     then use the pointer space directly.  */
  if (GOMP_MUTEX_INIT_0
      && sizeof (gomp_mutex_t) <= sizeof (void *)
      && __alignof (gomp_mutex_t) <= sizeof (void *))
    plock = (gomp_mutex_t *)pptr;

  /* Otherwise we have to be prepared to malloc storage.  */
  else
    {
      plock = *pptr;

      if (plock == NULL)
	{
#ifdef HAVE_SYNC_BUILTINS
	  gomp_mutex_t *nlock = gomp_malloc (sizeof (gomp_mutex_t));
	  gomp_mutex_init (nlock);

	  plock = __sync_val_compare_and_swap (pptr, NULL, nlock);
	  if (plock != NULL)
	    {
	      gomp_mutex_destroy (nlock);
	      free (nlock);
	    }
	  else
	    plock = nlock;
#else
	  gomp_mutex_lock (&create_lock_lock);
	  plock = *pptr;
	  if (plock == NULL)
	    {
	      plock = gomp_malloc (sizeof (gomp_mutex_t));
	      gomp_mutex_init (plock);
	      __sync_synchronize ();
	      *pptr = plock;
	    }
	  gomp_mutex_unlock (&create_lock_lock);
#endif
	}
    }

  gomp_mutex_lock (plock);
}

void
GOMP_critical_name_end (void **pptr)
{
  gomp_mutex_t *plock;

  /* If a mutex fits within the space for a pointer, and is zero initialized,
     then use the pointer space directly.  */
  if (GOMP_MUTEX_INIT_0
      && sizeof (gomp_mutex_t) <= sizeof (void *)
      && __alignof (gomp_mutex_t) <= sizeof (void *))
    plock = (gomp_mutex_t *)pptr;
  else
    plock = *pptr;

  gomp_mutex_unlock (plock);
}

/* This mutex is used when atomic operations don't exist for the target
   in the mode requested.  The result is not globally atomic, but works so
   long as all parallel references are within #pragma omp atomic directives.
   According to responses received from omp@openmp.org, appears to be within
   spec.  Which makes sense, since that's how several other compilers 
   handle this situation as well.  */

static gomp_mutex_t atomic_lock;

void
GOMP_atomic_start (void)
{
  gomp_mutex_lock (&atomic_lock);
}

void
GOMP_atomic_end (void)
{
  gomp_mutex_unlock (&atomic_lock);
}

#if !GOMP_MUTEX_INIT_0
static void __attribute__((constructor))
initialize_critical (void)
{
  gomp_mutex_init (&default_lock);
  gomp_mutex_init (&atomic_lock);
#ifndef HAVE_SYNC_BUILTINS
  gomp_mutex_init (&create_lock_lock);
#endif
}
#endif
