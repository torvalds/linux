/* Threads compatibility routines for libgcc2 and libobjc.  */
/* Compile this one with gcc.  */
/* Copyright (C) 2002, 2003, 2004 Free Software Foundation, Inc.

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

#ifndef GCC_GTHR_NKS_H
#define GCC_GTHR_NKS_H

/* NKS threads specific definitions.
   Easy, since the interface is mostly one-to-one mapping.  */

#define __GTHREADS 1

#define NKS_NO_INLINE_FUNCS
#include <nksapi.h>
#include <string.h>

typedef NXKey_t __gthread_key_t;
typedef NXMutex_t *__gthread_mutex_t;
typedef NXMutex_t *__gthread_recursive_mutex_t;

#define __GTHREAD_MUTEX_INIT_FUNCTION __gthread_mutex_init_function
#define __GTHREAD_RECURSIVE_MUTEX_INIT_FUNCTION __gthread_recursive_mutex_init_function

static inline int
__gthread_active_p (void)
{
  return 1;
}

#ifdef _LIBOBJC

/* This is the config.h file in libobjc/ */
#include <config.h>

#ifdef HAVE_SCHED_H
# include <sched.h>
#endif

/* Key structure for maintaining thread specific storage */
static NXKey_t _objc_thread_storage;

/* Backend initialization functions */

/* Initialize the threads subsystem.  */
static inline int
__gthread_objc_init_thread_system (void)
{
  /* Initialize the thread storage key.  */
  if (NXKeyCreate (NULL, NULL, &_objc_thread_storage) == 0)
    return 0;
  return -1;
}

/* Close the threads subsystem.  */
static inline int
__gthread_objc_close_thread_system (void)
{
  if (NXKeyDelete (_objc_thread_storage) == 0)
    return 0;
  return -1;
}

/* Backend thread functions */

/* Create a new thread of execution.  */
static inline objc_thread_t
__gthread_objc_thread_detach (void (*func)(void *), void *arg)
{
  objc_thread_t thread_id;
  NXContext_t context;
  NXThreadId_t new_thread_handle;
  int err;

  if ((context = NXContextAlloc (func, arg, NX_PRIO_MED, 0, 0, 0, &err)) == NULL)
    thread_id = NULL;
  else if (NXThreadCreate (context, NX_THR_DETACHED, &new_thread_handle) == 0)
    thread_id = (objc_thread_t) new_thread_handle;
  else {
    NXContextFree (context);
    thread_id = NULL;
  }
  
  return thread_id;
}

/* Set the current thread's priority.  */
static inline int
__gthread_objc_thread_set_priority (int priority)
{
  if (NXThreadSetPriority (NXThreadGetId (), priority) == 0)
    return 0;
  return -1;
}

/* Return the current thread's priority.  */
static inline int
__gthread_objc_thread_get_priority (void)
{
  int priority;

  if (NXThreadGetPriority (NXThreadGetId (), &priority) == 0)
    return priority;
  return -1;
}

/* Yield our process time to another thread.  */
static inline void
__gthread_objc_thread_yield (void)
{
  NXThreadYield ();
}

/* Terminate the current thread.  */
static inline int
__gthread_objc_thread_exit (void)
{
  /* exit the thread */
  NXThreadExit (&__objc_thread_exit_status);

  /* Failed if we reached here */
  return -1;
}

/* Returns an integer value which uniquely describes a thread.  */
static inline objc_thread_t
__gthread_objc_thread_id (void)
{
  (objc_thread_t) NXThreadGetId ();
}

/* Sets the thread's local storage pointer.  */
static inline int
__gthread_objc_thread_set_data (void *value)
{
  return NXKeySetValue (_objc_thread_storage, value);
}

/* Returns the thread's local storage pointer.  */
static inline void *
__gthread_objc_thread_get_data (void)
{
  void *value;

  if (NXKeyGetValue (_objc_thread_storage, &value) == 0)
    return value;
  return NULL;
}

/* Backend mutex functions */

/* Allocate a mutex.  */
static inline int
__gthread_objc_mutex_allocate (objc_mutex_t mutex)
{
  static const NX_LOCK_INFO_ALLOC (info, "GNU ObjC", 0);

  if ((mutex->backend = NXMutexAlloc (0, 0, &info)) == NULL)
    return 0;
  return -1;
}

/* Deallocate a mutex.  */
static inline int
__gthread_objc_mutex_deallocate (objc_mutex_t mutex)
{
  while (NXMutexIsOwned ((NXMutex_t *)mutex->backend))
    NXUnlock ((NXMutex_t *)mutex->backend);
  if (NXMutexFree ((NXMutex_t *)mutex->backend) != 0)
    return -1;
  mutex->backend = NULL;
  return 0;
}

/* Grab a lock on a mutex.  */
static inline int
__gthread_objc_mutex_lock (objc_mutex_t mutex)
{
  return NXLock ((NXMutex_t *)mutex->backend);
}

/* Try to grab a lock on a mutex.  */
static inline int
__gthread_objc_mutex_trylock (objc_mutex_t mutex)
{
  if (!NXTryLock ((NXMutex_t *)mutex->backend))
    return -1;
  return 0;
}

/* Unlock the mutex */
static inline int
__gthread_objc_mutex_unlock (objc_mutex_t mutex)
{
  return NXUnlock ((NXMutex_t *)mutex->backend);
}

/* Backend condition mutex functions */

/* Allocate a condition.  */
static inline int
__gthread_objc_condition_allocate (objc_condition_t condition)
{
  condition->backend = NXCondAlloc (NULL);
  if (condition->backend == NULL)
    return -1;

  return 0;
}

/* Deallocate a condition.  */
static inline int
__gthread_objc_condition_deallocate (objc_condition_t condition)
{
   if (NXCondFree ((NXCond_t *)condition->backend) != 0)
     return -1;
   condition->backend = NULL;
   return 0;
}

/* Wait on the condition */
static inline int
__gthread_objc_condition_wait (objc_condition_t condition, objc_mutex_t mutex)
{
  return NXCondWait ((NXCond_t *)condition->backend, (NXMutex_t *)mutex->backend);
}

/* Wake up all threads waiting on this condition.  */
static inline int
__gthread_objc_condition_broadcast (objc_condition_t condition)
{
  return NXCondBroadcast ((NXCond_t *)condition->backend);
}

/* Wake up one thread waiting on this condition.  */
static inline int
__gthread_objc_condition_signal (objc_condition_t condition)
{
  return NXCondSignal ((NXCond_t *)condition->backend);
}

#else /* _LIBOBJC */

#if defined(__cplusplus)
# include <bits/atomicity.h>
/* The remaining conditions here are temporary until there is
   an application accessible atomic operations API set... */
#elif defined(_M_IA64) || defined(__ia64__)
# include <../libstdc++-v3/config/cpu/ia64/bits/atomicity.h>
#elif defined(_M_IX86) || defined(__i486__)
# include <../libstdc++-v3/config/cpu/i486/bits/atomicity.h>
#elif defined(_M_AMD64) || defined(__x86_64__)
# include <../libstdc++-v3/config/cpu/x86-64/bits/atomicity.h>
#endif

typedef volatile long __gthread_once_t;

#define __GTHREAD_ONCE_INIT 0

static inline int
__gthread_once (__gthread_once_t *once, void (*func) (void))
{
  if (__compare_and_swap (once, 0, 1))
  {
    func();
    *once |= 2;
  }
  else
  {
    while (!(*once & 2))
      NXThreadYield ();
  }
  return 0;
}

static inline int
__gthread_key_create (__gthread_key_t *key, void (*dtor) (void *))
{
  return NXKeyCreate (dtor, NULL, key);
}

static inline int
__gthread_key_dtor (__gthread_key_t key, void *ptr)
{
  /* Just reset the key value to zero. */
  if (ptr)
    return NXKeySetValue (key, NULL);
  return 0;
}

static inline int
__gthread_key_delete (__gthread_key_t key)
{
  return NXKeyDelete (key);
}

static inline void *
__gthread_getspecific (__gthread_key_t key)
{
  void *value;

  if (NXKeyGetValue (key, &value) == 0)
    return value;
  return NULL;
}

static inline int
__gthread_setspecific (__gthread_key_t key, const void *ptr)
{
  return NXKeySetValue (key, (void *)ptr);
}

static inline void
__gthread_mutex_init_function (__gthread_mutex_t *mutex)
{
  static const NX_LOCK_INFO_ALLOC (info, "GTHREADS", 0);

  *mutex = NXMutexAlloc (0, 0, &info);
}

static inline int
__gthread_mutex_lock (__gthread_mutex_t *mutex)
{
  return NXLock (*mutex);
}

static inline int
__gthread_mutex_trylock (__gthread_mutex_t *mutex)
{
  if (NXTryLock (*mutex))
    return 0;
  return -1;
}

static inline int
__gthread_mutex_unlock (__gthread_mutex_t *mutex)
{
  return NXUnlock (*mutex);
}

static inline void
__gthread_recursive_mutex_init_function (__gthread_recursive_mutex_t *mutex)
{
  static const NX_LOCK_INFO_ALLOC (info, "GTHREADS", 0);

  *mutex = NXMutexAlloc (NX_MUTEX_RECURSIVE, 0, &info);
}

static inline int
__gthread_recursive_mutex_lock (__gthread_recursive_mutex_t *mutex)
{
  return NXLock (*mutex);
}

static inline int
__gthread_recursive_mutex_trylock (__gthread_recursive_mutex_t *mutex)
{
  if (NXTryLock (*mutex))
    return 0;
  return -1;
}

static inline int
__gthread_recursive_mutex_unlock (__gthread_recursive_mutex_t *mutex)
{
  return NXUnlock (*mutex);
}

#endif /* _LIBOBJC */

#endif /* not GCC_GTHR_NKS_H */
