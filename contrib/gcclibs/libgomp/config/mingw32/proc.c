/* Copyright (C) 2007 Free Software Foundation, Inc.
   Contributed by Danny Smith <dannysmith@users.sourceforge.net>

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
   online processors and dynamic load balancing.  It is expected that
   a system may well want to write special versions of each of these.

   The following implementation uses win32 API routines.  */

#include "libgomp.h"
#include <windows.h>

/* Count the CPU's currently available to this process.  */
static int
count_avail_process_cpus ()
{
  DWORD_PTR process_cpus;
  DWORD_PTR system_cpus;

  if (GetProcessAffinityMask (GetCurrentProcess (),
			      &process_cpus, &system_cpus))
    {
      unsigned int count;
      for (count = 0; process_cpus != 0; process_cpus >>= 1)  
	if (process_cpus & 1)
	  count++;
      return count;
    }
  return 1;
}

/* At startup, determine the default number of threads.  It would seem
   this should be related to the number of cpus available to the process.  */

void
gomp_init_num_threads (void)
{
  gomp_nthreads_var = count_avail_process_cpus ();
}

/* When OMP_DYNAMIC is set, at thread launch determine the number of
   threads we should spawn for this team.  FIXME:  How do we adjust for
   load average on MS Windows?  */

unsigned
gomp_dynamic_max_threads (void)
{
  int n_onln = count_avail_process_cpus ();
  return n_onln > gomp_nthreads_var ? gomp_nthreads_var : n_onln;
}

int
omp_get_num_procs (void)
{
  return count_avail_process_cpus ();
}

ialias (omp_get_num_procs)
