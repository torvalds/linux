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

/* This file handles the (bare) PARALLEL construct.  */

#include "libgomp.h"


/* Determine the number of threads to be launched for a PARALLEL construct.
   This algorithm is explicitly described in OpenMP 2.5 section 2.4.1.
   SPECIFIED is a combination of the NUM_THREADS clause and the IF clause.
   If the IF clause is false, SPECIFIED is forced to 1.  When NUM_THREADS
   is not present, SPECIFIED is 0.  */

unsigned
gomp_resolve_num_threads (unsigned specified)
{
  /* Early exit for false IF condition or degenerate NUM_THREADS.  */
  if (specified == 1)
    return 1;

  /* If this is a nested region, and nested regions are disabled, force
     this team to use only one thread.  */
  if (gomp_thread()->ts.team && !gomp_nest_var)
    return 1;

  /* If NUM_THREADS not specified, use nthreads_var.  */
  if (specified == 0)
    specified = gomp_nthreads_var;

  /* If dynamic threads are enabled, bound the number of threads
     that we launch.  */
  if (gomp_dyn_var)
    {
      unsigned dyn = gomp_dynamic_max_threads ();
      if (dyn < specified)
	return dyn;
    }

  return specified;
}

void
GOMP_parallel_start (void (*fn) (void *), void *data, unsigned num_threads)
{
  num_threads = gomp_resolve_num_threads (num_threads);
  gomp_team_start (fn, data, num_threads, NULL);
}

void
GOMP_parallel_end (void)
{
  gomp_team_end ();
}


/* The public OpenMP API for thread and team related inquiries.  */

int
omp_get_num_threads (void)
{
  struct gomp_team *team = gomp_thread ()->ts.team;
  return team ? team->nthreads : 1;
}

/* ??? Does this function need to disregard dyn_var?  I don't see
   how else one could get a useable "maximum".  */

int
omp_get_max_threads (void)
{
  return gomp_resolve_num_threads (0);
}

int
omp_get_thread_num (void)
{
  return gomp_thread ()->ts.team_id;
}

/* ??? This isn't right.  The definition of this function is false if any
   of the IF clauses for any of the parallels is false.  Which is not the
   same thing as any outer team having more than one thread.  */

int omp_in_parallel (void)
{
  struct gomp_team *team = gomp_thread ()->ts.team;

  while (team)
    {
      if (team->nthreads > 1)
	return true;
      team = team->prev_ts.team;
    }

  return false;
}

ialias (omp_get_num_threads)
ialias (omp_get_max_threads)
ialias (omp_get_thread_num)
ialias (omp_in_parallel)
