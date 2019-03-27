/* Threads compatibility routines for libgcc2 and libobjc.  */
/* Compile this one with gcc.  */
/* Copyright (C) 1997, 1999, 2000, 2004, 2005, 2006
   Free Software Foundation, Inc.

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

#ifndef GCC_GTHR_SOLARIS_H
#define GCC_GTHR_SOLARIS_H

/* Solaris threads as found in Solaris 2.[456].
   Actually these are Unix International (UI) threads, but I don't
   know if anyone else implements these.  */

#define __GTHREADS 1

#include <thread.h>
#include <errno.h>

#ifdef __cplusplus
#define UNUSED(x)
#else
#define UNUSED(x) x __attribute__((unused))
#endif

typedef thread_key_t __gthread_key_t;
typedef struct {
  mutex_t mutex;
  int once;
} __gthread_once_t;
typedef mutex_t __gthread_mutex_t;

typedef struct {
  long depth;
  thread_t owner;
  mutex_t actual;
} __gthread_recursive_mutex_t;

#define __GTHREAD_ONCE_INIT { DEFAULTMUTEX, 0 }
#define __GTHREAD_MUTEX_INIT DEFAULTMUTEX
#define __GTHREAD_RECURSIVE_MUTEX_INIT_FUNCTION __gthread_recursive_mutex_init_function

#if SUPPORTS_WEAK && GTHREAD_USE_WEAK
# define __gthrw(name) \
  static __typeof(name) __gthrw_ ## name __attribute__ ((__weakref__(#name)));
# define __gthrw_(name) __gthrw_ ## name
#else
# define __gthrw(name)
# define __gthrw_(name) name
#endif

__gthrw(thr_keycreate)
__gthrw(thr_getspecific)
__gthrw(thr_setspecific)
__gthrw(thr_create)
__gthrw(thr_self)

__gthrw(mutex_init)
__gthrw(mutex_destroy)
__gthrw(mutex_lock)
__gthrw(mutex_trylock)
__gthrw(mutex_unlock)

#ifdef _LIBOBJC
__gthrw(thr_exit)
__gthrw(thr_getprio)
__gthrw(thr_setprio)
__gthrw(thr_yield)

__gthrw(cond_init)
__gthrw(cond_destroy)
__gthrw(cond_wait)
__gthrw(cond_broadcast)
__gthrw(cond_signal)

#endif

#if SUPPORTS_WEAK && GTHREAD_USE_WEAK

/* This will not actually work in Solaris 2.5, since libc contains
   dummy symbols of all thr_* routines.  */

static inline int
__gthread_active_p (void)
{
  static void *const __gthread_active_ptr = (void *) &__gthrw_(thr_create);
  return __gthread_active_ptr != 0;
}

#else /* not SUPPORTS_WEAK */

static inline int
__gthread_active_p (void)
{
  return 1;
}

#endif /* SUPPORTS_WEAK */

#ifdef _LIBOBJC

/* Key structure for maintaining thread specific storage */
static thread_key_t _objc_thread_storage;

/* Thread local storage for a single thread */
static void *thread_local_storage = NULL;

/* Backend initialization functions */

/* Initialize the threads subsystem.  */
static inline int
__gthread_objc_init_thread_system (void)
{
  /* Initialize the thread storage key.  */
  if (__gthread_active_p ()
      && __gthrw_(thr_keycreate) (&_objc_thread_storage, NULL) == 0)
    return 0;

  return -1;
}

/* Close the threads subsystem.  */
static inline int
__gthread_objc_close_thread_system (void)
{
  if (__gthread_active_p ())
    return 0;
  else
    return -1;
}

/* Backend thread functions */

/* Create a new thread of execution.  */
static inline objc_thread_t
__gthread_objc_thread_detach (void (*func)(void *), void *arg)
{
  objc_thread_t thread_id;
  thread_t new_thread_id = 0;

  if (!__gthread_active_p ())
    return NULL;

  if (__gthrw_(thr_create) (NULL, 0, (void *) func, arg,
		  THR_DETACHED | THR_NEW_LWP,
		  &new_thread_id) == 0)
    thread_id = *(objc_thread_t *) &new_thread_id;
  else
    thread_id = NULL;

  return thread_id;
}

/* Set the current thread's priority.  */
static inline int
__gthread_objc_thread_set_priority (int priority)
{
  int sys_priority = 0;

  if (!__gthread_active_p ())
    return -1;

  switch (priority)
    {
    case OBJC_THREAD_INTERACTIVE_PRIORITY:
      sys_priority = 300;
      break;
    default:
    case OBJC_THREAD_BACKGROUND_PRIORITY:
      sys_priority = 200;
      break;
    case OBJC_THREAD_LOW_PRIORITY:
      sys_priority = 1000;
      break;
    }

  /* Change priority */
  if (__gthrw_(thr_setprio) (__gthrw_(thr_self) (), sys_priority) == 0)
    return 0;
  else
    return -1;
}

/* Return the current thread's priority.  */
static inline int
__gthread_objc_thread_get_priority (void)
{
  int sys_priority;

  if (!__gthread_active_p ())
    return OBJC_THREAD_INTERACTIVE_PRIORITY;

  if (__gthrw_(thr_getprio) (__gthrw_(thr_self) (), &sys_priority) == 0)
    {
      if (sys_priority >= 250)
	return OBJC_THREAD_INTERACTIVE_PRIORITY;
      else if (sys_priority >= 150)
	return OBJC_THREAD_BACKGROUND_PRIORITY;
      return OBJC_THREAD_LOW_PRIORITY;
    }

  /* Couldn't get priority.  */
  return -1;
}

/* Yield our process time to another thread.  */
static inline void
__gthread_objc_thread_yield (void)
{
  if (__gthread_active_p ())
    __gthrw_(thr_yield) ();
}

/* Terminate the current thread.  */
static inline int
__gthread_objc_thread_exit (void)
{
  if (__gthread_active_p ())
    /* exit the thread */
    __gthrw_(thr_exit) (&__objc_thread_exit_status);

  /* Failed if we reached here */
  return -1;
}

/* Returns an integer value which uniquely describes a thread.  */
static inline objc_thread_t
__gthread_objc_thread_id (void)
{
  if (__gthread_active_p ())
    return (objc_thread_t) __gthrw_(thr_self) ();
  else
    return (objc_thread_t) 1;
}

/* Sets the thread's local storage pointer.  */
static inline int
__gthread_objc_thread_set_data (void *value)
{
  if (__gthread_active_p ())
    {
      if (__gthrw_(thr_setspecific) (_objc_thread_storage, value) == 0)
	return 0;
      else
	return -1;
    }
  else
    {
      thread_local_storage = value;
      return 0;
    }
}

/* Returns the thread's local storage pointer.  */
static inline void *
__gthread_objc_thread_get_data (void)
{
  void *value = NULL;

  if (__gthread_active_p ())
    {
      if (__gthrw_(thr_getspecific) (_objc_thread_storage, &value) == 0)
	return value;
      else
	return NULL;
    }
  else
    return thread_local_storage;
}

/* Backend mutex functions */

/* Allocate a mutex.  */
static inline int
__gthread_objc_mutex_allocate (objc_mutex_t mutex)
{
  if (__gthread_active_p ()
      && __gthrw_(mutex_init) ((mutex_t *) (&(mutex->backend)), USYNC_THREAD, 0))
    return -1;

  return 0;
}

/* Deallocate a mutex.  */
static inline int
__gthread_objc_mutex_deallocate (objc_mutex_t mutex)
{
  if (__gthread_active_p ())
    __gthrw_(mutex_destroy) ((mutex_t *) (&(mutex->backend)));

  return 0;
}

/* Grab a lock on a mutex.  */
static inline int
__gthread_objc_mutex_lock (objc_mutex_t mutex)
{
  if (__gthread_active_p ()
      && __gthrw_(mutex_lock) ((mutex_t *) (&(mutex->backend))) != 0)
    return -1;

  return 0;
}

/* Try to grab a lock on a mutex.  */
static inline int
__gthread_objc_mutex_trylock (objc_mutex_t mutex)
{
  if (__gthread_active_p ()
      && __gthrw_(mutex_trylock) ((mutex_t *) (&(mutex->backend))) != 0)
    return -1;

  return 0;
}

/* Unlock the mutex */
static inline int
__gthread_objc_mutex_unlock (objc_mutex_t mutex)
{
  if (__gthread_active_p ()
      && __gthrw_(mutex_unlock) ((mutex_t *) (&(mutex->backend))) != 0)
    return -1;

  return 0;
}

/* Backend condition mutex functions */

/* Allocate a condition.  */
static inline int
__gthread_objc_condition_allocate (objc_condition_t condition)
{
  if (__gthread_active_p ())
    return __gthrw_(cond_init) ((cond_t *) (&(condition->backend)), USYNC_THREAD,
		      NULL);
  else
    return 0;
}

/* Deallocate a condition.  */
static inline int
__gthread_objc_condition_deallocate (objc_condition_t condition)
{
  if (__gthread_active_p ())
    return __gthrw_(cond_destroy) ((cond_t *) (&(condition->backend)));
  else
    return 0;
}

/* Wait on the condition */
static inline int
__gthread_objc_condition_wait (objc_condition_t condition, objc_mutex_t mutex)
{
  if (__gthread_active_p ())
    return __gthrw_(cond_wait) ((cond_t *) (&(condition->backend)),
		      (mutex_t *) (&(mutex->backend)));
  else
    return 0;
}

/* Wake up all threads waiting on this condition.  */
static inline int
__gthread_objc_condition_broadcast (objc_condition_t condition)
{
  if (__gthread_active_p ())
    return __gthrw_(cond_broadcast) ((cond_t *) (&(condition->backend)));
  else
    return 0;
}

/* Wake up one thread waiting on this condition.  */
static inline int
__gthread_objc_condition_signal (objc_condition_t condition)
{
  if (__gthread_active_p ())
    return __gthrw_(cond_signal) ((cond_t *) (&(condition->backend)));
  else
    return 0;
}

#else /* _LIBOBJC */

static inline int
__gthread_once (__gthread_once_t *once, void (*func) (void))
{
  if (! __gthread_active_p ())
    return -1;

  if (once == 0 || func == 0)
    return EINVAL;

  if (once->once == 0)
    {
      int status = __gthrw_(mutex_lock) (&once->mutex);
      if (status != 0)
	return status;
      if (once->once == 0)
	{
	  (*func) ();
	  once->once++;
	}
      __gthrw_(mutex_unlock) (&once->mutex);
    }
  return 0;
}

static inline int
__gthread_key_create (__gthread_key_t *key, void (*dtor) (void *))
{
  /* Solaris 2.5 contains thr_* routines no-op in libc, so test if we actually
     got a reasonable key value, and if not, fail.  */
  *key = (__gthread_key_t)-1;
  if (__gthrw_(thr_keycreate) (key, dtor) != 0 || *key == (__gthread_key_t)-1)
    return -1;
  else
    return 0;
}

static inline int
__gthread_key_delete (__gthread_key_t UNUSED (key))
{
  /* Not possible.  */
  return -1;
}

static inline void *
__gthread_getspecific (__gthread_key_t key)
{
  void *ptr;
  if (__gthrw_(thr_getspecific) (key, &ptr) == 0)
    return ptr;
  else
    return 0;
}

static inline int
__gthread_setspecific (__gthread_key_t key, const void *ptr)
{
  return __gthrw_(thr_setspecific) (key, (void *) ptr);
}

static inline int
__gthread_mutex_lock (__gthread_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthrw_(mutex_lock) (mutex);
  else
    return 0;
}

static inline int
__gthread_mutex_trylock (__gthread_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthrw_(mutex_trylock) (mutex);
  else
    return 0;
}

static inline int
__gthread_mutex_unlock (__gthread_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthrw_(mutex_unlock) (mutex);
  else
    return 0;
}

static inline int
__gthread_recursive_mutex_init_function (__gthread_recursive_mutex_t *mutex)
{
  mutex->depth = 0;
  mutex->owner = (thread_t) 0;
  return __gthrw_(mutex_init) (&mutex->actual, USYNC_THREAD, 0);
}

static inline int
__gthread_recursive_mutex_lock (__gthread_recursive_mutex_t *mutex)
{
  if (__gthread_active_p ())
    {
      thread_t me = __gthrw_(thr_self) ();

      if (mutex->owner != me)
	{
	  __gthrw_(mutex_lock) (&mutex->actual);
	  mutex->owner = me;
	}

      mutex->depth++;
    }
  return 0;
}

static inline int
__gthread_recursive_mutex_trylock (__gthread_recursive_mutex_t *mutex)
{
  if (__gthread_active_p ())
    {
      thread_t me = __gthrw_(thr_self) ();

      if (mutex->owner != me)
	{
	  if (__gthrw_(mutex_trylock) (&mutex->actual))
	    return 1;
	  mutex->owner = me;
	}

      mutex->depth++;
    }
  return 0;
}

static inline int
__gthread_recursive_mutex_unlock (__gthread_recursive_mutex_t *mutex)
{
  if (__gthread_active_p ())
    {
      if (--mutex->depth == 0)
	{
	   mutex->owner = (thread_t) 0;
	   __gthrw_(mutex_unlock) (&mutex->actual);
	}
    }
  return 0;
}

#endif /* _LIBOBJC */

#undef UNUSED

#endif /* ! GCC_GTHR_SOLARIS_H */
