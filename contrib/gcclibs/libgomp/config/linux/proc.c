/* Copyright (C) 2005, 2006, 2007 Free Software Foundation, Inc.
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

/* This file contains system specific routines related to counting
   online processors and dynamic load balancing.  */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "libgomp.h"
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>
#ifdef HAVE_GETLOADAVG
# ifdef HAVE_SYS_LOADAVG_H
#  include <sys/loadavg.h>
# endif
#endif

#ifdef HAVE_PTHREAD_AFFINITY_NP
static unsigned long
cpuset_popcount (cpu_set_t *cpusetp)
{
#ifdef CPU_COUNT
  /* glibc 2.6 and above provide a macro for this.  */
  return CPU_COUNT (cpusetp);
#else
  size_t i;
  unsigned long ret = 0;
  extern int check[sizeof (cpusetp->__bits[0]) == sizeof (unsigned long int)];

  (void) check;
  for (i = 0; i < sizeof (*cpusetp) / sizeof (cpusetp->__bits[0]); i++)
    {
      unsigned long int mask = cpusetp->__bits[i];
      if (mask == 0)
	continue;
      ret += __builtin_popcountl (mask);
    }
  return ret;
#endif
}
#endif

/* At startup, determine the default number of threads.  It would seem
   this should be related to the number of cpus online.  */

void
gomp_init_num_threads (void)
{
#ifdef HAVE_PTHREAD_AFFINITY_NP
  cpu_set_t cpuset;

  if (pthread_getaffinity_np (pthread_self (), sizeof (cpuset), &cpuset) == 0)
    {
      /* Count only the CPUs this process can use.  */
      gomp_nthreads_var = cpuset_popcount (&cpuset);
      if (gomp_nthreads_var == 0)
	gomp_nthreads_var = 1;
      return;
    }
#endif
#ifdef _SC_NPROCESSORS_ONLN
  gomp_nthreads_var = sysconf (_SC_NPROCESSORS_ONLN);
#endif
}

static int
get_num_procs (void)
{
#ifdef HAVE_PTHREAD_AFFINITY_NP
  cpu_set_t cpuset;

  if (gomp_cpu_affinity == NULL)
    {
      /* Count only the CPUs this process can use.  */
      if (pthread_getaffinity_np (pthread_self (), sizeof (cpuset),
				  &cpuset) == 0)
	{
	  int ret = cpuset_popcount (&cpuset);
	  return ret != 0 ? ret : 1;
	}
    }
  else
    {
      size_t idx;
      static int affinity_cpus;

      /* We can't use pthread_getaffinity_np in this case
	 (we have changed it ourselves, it binds to just one CPU).
	 Count instead the number of different CPUs we are
	 using.  */
      CPU_ZERO (&cpuset);
      if (affinity_cpus == 0)
	{
	  int cpus = 0;
	  for (idx = 0; idx < gomp_cpu_affinity_len; idx++)
	    if (! CPU_ISSET (gomp_cpu_affinity[idx], &cpuset))
	      {
		cpus++;
		CPU_SET (gomp_cpu_affinity[idx], &cpuset);
	      }
	  affinity_cpus = cpus;
	}
      return affinity_cpus;
    }
#endif
#ifdef _SC_NPROCESSORS_ONLN
  return sysconf (_SC_NPROCESSORS_ONLN);
#else
  return gomp_nthreads_var;
#endif
}

/* When OMP_DYNAMIC is set, at thread launch determine the number of
   threads we should spawn for this team.  */
/* ??? I have no idea what best practice for this is.  Surely some
   function of the number of processors that are *still* online and
   the load average.  Here I use the number of processors online
   minus the 15 minute load average.  */

unsigned
gomp_dynamic_max_threads (void)
{
  unsigned n_onln, loadavg;

  n_onln = get_num_procs ();
  if (n_onln > gomp_nthreads_var)
    n_onln = gomp_nthreads_var;

  loadavg = 0;
#ifdef HAVE_GETLOADAVG
  {
    double dloadavg[3];
    if (getloadavg (dloadavg, 3) == 3)
      {
	/* Add 0.1 to get a kind of biased rounding.  */
	loadavg = dloadavg[2] + 0.1;
      }
  }
#endif

  if (loadavg >= n_onln)
    return 1;
  else
    return n_onln - loadavg;
}

int
omp_get_num_procs (void)
{
  return get_num_procs ();
}

ialias (omp_get_num_procs)
