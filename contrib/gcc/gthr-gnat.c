/* Threads compatibility routines for libgcc2.  */
/* Compile this one with gcc.  */
/* Copyright (C) 2003, 2004 Free Software Foundation, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* As a special exception, if you link this library with other files,
   some of which are compiled with GCC, to produce an executable,
   this library does not by itself cause the resulting executable
   to be covered by the GNU General Public License.
   This exception does not however invalidate any other reasons why
   the executable file might be covered by the GNU General Public License.  */

#include "gthr-gnat.h"

#ifndef HIDE_EXPORTS
#pragma GCC visibility push(default)
#endif

#ifdef __cplusplus
#define UNUSED(x)
#else
#define UNUSED(x) x __attribute__((unused))
#endif

void __gnat_default_lock (void);
void __gnat_default_unlock (void);

void
__gnat_default_lock (void)
{
  return;
}

void
__gnat_default_unlock (void)
{
  return;
}

static void (*__gnat_task_lock) (void) = *__gnat_default_lock;
static void (*__gnat_task_unlock) (void) = *__gnat_default_unlock;

 void
__gnat_install_locks (void (*lock) (void), void (*unlock) (void))
{
  __gnat_task_lock = lock;
  __gnat_task_unlock = unlock;
}

int
__gthread_active_p (void)
{
  return 0;
}

int
__gthread_mutex_lock (__gthread_mutex_t * UNUSED (mutex))
{
  __gnat_task_lock ();
  return 0;
}

int
__gthread_mutex_unlock (__gthread_mutex_t * UNUSED (mutex))
{
  __gnat_task_unlock ();
  return 0;
}

#ifndef HIDE_EXPORTS
#pragma GCC visibility pop
#endif
