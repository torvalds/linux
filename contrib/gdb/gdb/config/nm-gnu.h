/* Common declarations for the GNU Hurd

   Copyright 1995, 1996, 1998, 1999 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   The GNU Hurd is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   The GNU Hurd is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA. */

#ifndef __NM_GNU_H__
#define __NM_GNU_H__

#include <unistd.h>
#include <mach.h>
#include <mach/exception.h>
#include "regcache.h"

extern char *gnu_target_pid_to_str (int pid);

/* Before storing, we need to read all the registers.  */
#define CHILD_PREPARE_TO_STORE() deprecated_read_register_bytes (0, NULL, DEPRECATED_REGISTER_BYTES)

/* Don't do wait_for_inferior on attach.  */
#define ATTACH_NO_WAIT

/* Use SVR4 style shared library support */
#define SVR4_SHARED_LIBS
#include "solib.h"
#define NO_CORE_OPS

#endif /* __NM_GNU_H__ */
