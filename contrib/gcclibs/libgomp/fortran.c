/* Copyright (C) 2005 Free Software Foundation, Inc.
   Contributed by Jakub Jelinek <jakub@redhat.com>.

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

/* This file contains Fortran wrapper routines.  */

#include "libgomp.h"
#include "libgomp_f.h"
#include <stdlib.h>

#ifdef HAVE_ATTRIBUTE_ALIAS
/* Use internal aliases if possible.  */
#define ULP	STR1(__USER_LABEL_PREFIX__)
#define STR1(x)	STR2(x)
#define STR2(x)	#x
# define ialias_redirect(fn) \
  extern __typeof (fn) fn __asm__ (ULP "gomp_ialias_" #fn) attribute_hidden;
ialias_redirect (omp_init_lock)
ialias_redirect (omp_init_nest_lock)
ialias_redirect (omp_destroy_lock)
ialias_redirect (omp_destroy_nest_lock)
ialias_redirect (omp_set_lock)
ialias_redirect (omp_set_nest_lock)
ialias_redirect (omp_unset_lock)
ialias_redirect (omp_unset_nest_lock)
ialias_redirect (omp_test_lock)
ialias_redirect (omp_test_nest_lock)
ialias_redirect (omp_set_dynamic)
ialias_redirect (omp_set_nested)
ialias_redirect (omp_set_num_threads)
ialias_redirect (omp_get_dynamic)
ialias_redirect (omp_get_nested)
ialias_redirect (omp_in_parallel)
ialias_redirect (omp_get_max_threads)
ialias_redirect (omp_get_num_procs)
ialias_redirect (omp_get_num_threads)
ialias_redirect (omp_get_thread_num)
ialias_redirect (omp_get_wtick)
ialias_redirect (omp_get_wtime)
#endif        

void
omp_init_lock_ (omp_lock_arg_t lock)
{
#ifndef OMP_LOCK_DIRECT
  omp_lock_arg (lock) = malloc (sizeof (omp_lock_t));
#endif
  omp_init_lock (omp_lock_arg (lock));
}

void
omp_init_nest_lock_ (omp_nest_lock_arg_t lock)
{
#ifndef OMP_NEST_LOCK_DIRECT
  omp_nest_lock_arg (lock) = malloc (sizeof (omp_nest_lock_t));
#endif
  omp_init_nest_lock (omp_nest_lock_arg (lock));
}

void
omp_destroy_lock_ (omp_lock_arg_t lock)
{
  omp_destroy_lock (omp_lock_arg (lock));
#ifndef OMP_LOCK_DIRECT
  free (omp_lock_arg (lock));
  omp_lock_arg (lock) = NULL;
#endif
}

void
omp_destroy_nest_lock_ (omp_nest_lock_arg_t lock)
{
  omp_destroy_nest_lock (omp_nest_lock_arg (lock));
#ifndef OMP_NEST_LOCK_DIRECT
  free (omp_nest_lock_arg (lock));
  omp_nest_lock_arg (lock) = NULL;
#endif
}

void
omp_set_lock_ (omp_lock_arg_t lock)
{
  omp_set_lock (omp_lock_arg (lock));
}

void
omp_set_nest_lock_ (omp_nest_lock_arg_t lock)
{
  omp_set_nest_lock (omp_nest_lock_arg (lock));
}

void
omp_unset_lock_ (omp_lock_arg_t lock)
{
  omp_unset_lock (omp_lock_arg (lock));
}

void
omp_unset_nest_lock_ (omp_nest_lock_arg_t lock)
{
  omp_unset_nest_lock (omp_nest_lock_arg (lock));
}

void
omp_set_dynamic_ (const int32_t *set)
{
  omp_set_dynamic (*set);
}

void
omp_set_dynamic_8_ (const int64_t *set)
{
  omp_set_dynamic (*set);
}

void
omp_set_nested_ (const int32_t *set)
{
  omp_set_nested (*set);
}

void
omp_set_nested_8_ (const int64_t *set)
{
  omp_set_nested (*set);
}

void
omp_set_num_threads_ (const int32_t *set)
{
  omp_set_num_threads (*set);
}

void
omp_set_num_threads_8_ (const int64_t *set)
{
  omp_set_num_threads (*set);
}

int32_t
omp_get_dynamic_ (void)
{
  return omp_get_dynamic ();
}

int32_t
omp_get_nested_ (void)
{
  return omp_get_nested ();
}

int32_t
omp_in_parallel_ (void)
{
  return omp_in_parallel ();
}

int32_t
omp_test_lock_ (omp_lock_arg_t lock)
{
  return omp_test_lock (omp_lock_arg (lock));
}

int32_t
omp_get_max_threads_ (void)
{
  return omp_get_max_threads ();
}

int32_t
omp_get_num_procs_ (void)
{
  return omp_get_num_procs ();
}

int32_t
omp_get_num_threads_ (void)
{
  return omp_get_num_threads ();
}

int32_t
omp_get_thread_num_ (void)
{
  return omp_get_thread_num ();
}

int32_t
omp_test_nest_lock_ (omp_nest_lock_arg_t lock)
{
  return omp_test_nest_lock (omp_nest_lock_arg (lock));
}

double
omp_get_wtick_ (void)
{
  return omp_get_wtick ();
}

double
omp_get_wtime_ (void)
{
  return omp_get_wtime ();
}
