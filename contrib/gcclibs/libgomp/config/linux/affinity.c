/* Copyright (C) 2006, 2007 Free Software Foundation, Inc.
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

/* This is a Linux specific implementation of a CPU affinity setting.  */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "libgomp.h"
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>

#ifdef HAVE_PTHREAD_AFFINITY_NP

static unsigned int affinity_counter;
#ifndef HAVE_SYNC_BUILTINS
static gomp_mutex_t affinity_lock;
#endif

void
gomp_init_affinity (void)
{
  cpu_set_t cpuset;
  size_t idx, widx;

  if (pthread_getaffinity_np (pthread_self (), sizeof (cpuset), &cpuset))
    {
      gomp_error ("could not get CPU affinity set");
      free (gomp_cpu_affinity);
      gomp_cpu_affinity = NULL;
      gomp_cpu_affinity_len = 0;
      return;
    }

  for (widx = idx = 0; idx < gomp_cpu_affinity_len; idx++)
    if (gomp_cpu_affinity[idx] < CPU_SETSIZE
        && CPU_ISSET (gomp_cpu_affinity[idx], &cpuset))
      gomp_cpu_affinity[widx++] = gomp_cpu_affinity[idx];

  if (widx == 0)
    {
      gomp_error ("no CPUs left for affinity setting");
      free (gomp_cpu_affinity);
      gomp_cpu_affinity = NULL;
      gomp_cpu_affinity_len = 0;
      return;
    }

  gomp_cpu_affinity_len = widx;
  CPU_ZERO (&cpuset);
  CPU_SET (gomp_cpu_affinity[0], &cpuset);
  pthread_setaffinity_np (pthread_self (), sizeof (cpuset), &cpuset);
  affinity_counter = 1;
#ifndef HAVE_SYNC_BUILTINS
  gomp_mutex_init (&affinity_lock);
#endif
}

void
gomp_init_thread_affinity (pthread_attr_t *attr)
{
  unsigned int cpu;
  cpu_set_t cpuset;

#ifdef HAVE_SYNC_BUILTINS
  cpu = __sync_fetch_and_add (&affinity_counter, 1);
#else
  gomp_mutex_lock (&affinity_lock);
  cpu = affinity_counter++;
  gomp_mutex_unlock (&affinity_lock);
#endif
  cpu %= gomp_cpu_affinity_len;
  CPU_ZERO (&cpuset);
  CPU_SET (gomp_cpu_affinity[cpu], &cpuset);
  pthread_attr_setaffinity_np (attr, sizeof (cpu_set_t), &cpuset);
}

#else

#include "../posix/affinity.c"

#endif
