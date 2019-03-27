/* Threads compatibility routines for libgcc2 and libobjc.  */
/* Compile this one with gcc.  */
/* Copyright (C) 2004, 2005 Free Software Foundation, Inc.

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

#ifndef GCC_GTHR_POSIX_H
#define GCC_GTHR_POSIX_H

/* POSIX threads specific definitions.
   Easy, since the interface is just one-to-one mapping.  */

#define __GTHREADS 1

/* Some implementations of <pthread.h> require this to be defined.  */
#ifndef _REENTRANT
#define _REENTRANT 1
#endif

#include <pthread.h>
#include <unistd.h>

typedef pthread_key_t __gthread_key_t;
typedef pthread_once_t __gthread_once_t;
typedef pthread_mutex_t __gthread_mutex_t;

typedef struct {
  long depth;
  pthread_t owner;
  pthread_mutex_t actual;
} __gthread_recursive_mutex_t;

#define __GTHREAD_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define __GTHREAD_ONCE_INIT PTHREAD_ONCE_INIT
#define __GTHREAD_RECURSIVE_MUTEX_INIT_FUNCTION __gthread_recursive_mutex_init_function

#if SUPPORTS_WEAK && GTHREAD_USE_WEAK
# define __gthrw(name) \
  static __typeof(name) __gthrw_ ## name __attribute__ ((__weakref__(#name)));
# define __gthrw_(name) __gthrw_ ## name
#else
# define __gthrw(name)
# define __gthrw_(name) name
#endif

__gthrw(pthread_once)
__gthrw(pthread_key_create)
__gthrw(pthread_key_delete)
__gthrw(pthread_getspecific)
__gthrw(pthread_setspecific)
__gthrw(pthread_create)
__gthrw(pthread_cancel)
__gthrw(pthread_self)

__gthrw(pthread_mutex_lock)
__gthrw(pthread_mutex_trylock)
__gthrw(pthread_mutex_unlock)
__gthrw(pthread_mutexattr_init)
__gthrw(pthread_mutexattr_destroy)

__gthrw(pthread_mutex_init)

#if defined(_LIBOBJC) || defined(_LIBOBJC_WEAK)
/* Objective-C.  */
__gthrw(pthread_cond_broadcast)
__gthrw(pthread_cond_destroy)
__gthrw(pthread_cond_init)
__gthrw(pthread_cond_signal)
__gthrw(pthread_cond_wait)
__gthrw(pthread_exit)
__gthrw(pthread_mutex_destroy)
#ifdef _POSIX_PRIORITY_SCHEDULING
#ifdef _POSIX_THREAD_PRIORITY_SCHEDULING
__gthrw(sched_get_priority_max)
__gthrw(sched_get_priority_min)
#endif /* _POSIX_THREAD_PRIORITY_SCHEDULING */
#endif /* _POSIX_PRIORITY_SCHEDULING */
__gthrw(sched_yield)
__gthrw(pthread_attr_destroy)
__gthrw(pthread_attr_init)
__gthrw(pthread_attr_setdetachstate)
#ifdef _POSIX_THREAD_PRIORITY_SCHEDULING
__gthrw(pthread_getschedparam)
__gthrw(pthread_setschedparam)
#endif /* _POSIX_THREAD_PRIORITY_SCHEDULING */
#endif /* _LIBOBJC || _LIBOBJC_WEAK */

#if SUPPORTS_WEAK && GTHREAD_USE_WEAK

/* On Solaris 2.6 up to 9, the libc exposes a POSIX threads interface even if
   -pthreads is not specified.  The functions are dummies and most return an
   error value.  However pthread_once returns 0 without invoking the routine
   it is passed so we cannot pretend that the interface is active if -pthreads
   is not specified.  On Solaris 2.5.1, the interface is not exposed at all so
   we need to play the usual game with weak symbols.  On Solaris 10 and up, a
   working interface is always exposed. On FreeBSD 6 and later, libc also
   exposes a dummy POSIX threads interface, similar to what Solaris 2.6 up
   to 9 does.  FreeBSD >= 700014 even provides a pthread_cancel stub in libc,
   which means the alternate __gthread_active_p below cannot be used there.  */


 */

#if defined(__FreeBSD__) || defined(__sun) && defined(__svr4__)

static volatile int __gthread_active = -1;

static void
__gthread_trigger (void)
{
  __gthread_active = 1;
}

static inline int
__gthread_active_p (void)
{
  static pthread_mutex_t __gthread_active_mutex = PTHREAD_MUTEX_INITIALIZER;
  static pthread_once_t __gthread_active_once = PTHREAD_ONCE_INIT;

  /* Avoid reading __gthread_active twice on the main code path.  */
  int __gthread_active_latest_value = __gthread_active;

  /* This test is not protected to avoid taking a lock on the main code
     path so every update of __gthread_active in a threaded program must
     be atomic with regard to the result of the test.  */
  if (__builtin_expect (__gthread_active_latest_value < 0, 0))
    {
      if (__gthrw_(pthread_once))
	{
	  /* If this really is a threaded program, then we must ensure that
	     __gthread_active has been set to 1 before exiting this block.  */
	  __gthrw_(pthread_mutex_lock) (&__gthread_active_mutex);
	  __gthrw_(pthread_once) (&__gthread_active_once, __gthread_trigger);
	  __gthrw_(pthread_mutex_unlock) (&__gthread_active_mutex);
	}

      /* Make sure we'll never enter this block again.  */
      if (__gthread_active < 0)
	__gthread_active = 0;

      __gthread_active_latest_value = __gthread_active;
    }

  return __gthread_active_latest_value != 0;
}

#else /* neither FreeBSD nor Solaris */

static inline int
__gthread_active_p (void)
{
  static void *const __gthread_active_ptr 
    = __extension__ (void *) &__gthrw_(pthread_cancel);
  return __gthread_active_ptr != 0;
}

#endif /* FreeBSD or Solaris */

#else /* not SUPPORTS_WEAK */

static inline int
__gthread_active_p (void)
{
  return 1;
}

#endif /* SUPPORTS_WEAK */

#ifdef _LIBOBJC

/* This is the config.h file in libobjc/ */
#include <config.h>

#ifdef HAVE_SCHED_H
# include <sched.h>
#endif

/* Key structure for maintaining thread specific storage */
static pthread_key_t _objc_thread_storage;
static pthread_attr_t _objc_thread_attribs;

/* Thread local storage for a single thread */
static void *thread_local_storage = NULL;

/* Backend initialization functions */

/* Initialize the threads subsystem.  */
static inline int
__gthread_objc_init_thread_system (void)
{
  if (__gthread_active_p ())
    {
      /* Initialize the thread storage key.  */
      if (__gthrw_(pthread_key_create) (&_objc_thread_storage, NULL) == 0)
	{
	  /* The normal default detach state for threads is
	   * PTHREAD_CREATE_JOINABLE which causes threads to not die
	   * when you think they should.  */
	  if (__gthrw_(pthread_attr_init) (&_objc_thread_attribs) == 0
	      && __gthrw_(pthread_attr_setdetachstate) (&_objc_thread_attribs,
					      PTHREAD_CREATE_DETACHED) == 0)
	    return 0;
	}
    }

  return -1;
}

/* Close the threads subsystem.  */
static inline int
__gthread_objc_close_thread_system (void)
{
  if (__gthread_active_p ()
      && __gthrw_(pthread_key_delete) (_objc_thread_storage) == 0
      && __gthrw_(pthread_attr_destroy) (&_objc_thread_attribs) == 0)
    return 0;

  return -1;
}

/* Backend thread functions */

/* Create a new thread of execution.  */
static inline objc_thread_t
__gthread_objc_thread_detach (void (*func)(void *), void *arg)
{
  objc_thread_t thread_id;
  pthread_t new_thread_handle;

  if (!__gthread_active_p ())
    return NULL;

  if (!(__gthrw_(pthread_create) (&new_thread_handle, NULL, (void *) func, arg)))
    thread_id = (objc_thread_t) new_thread_handle;
  else
    thread_id = NULL;

  return thread_id;
}

/* Set the current thread's priority.  */
static inline int
__gthread_objc_thread_set_priority (int priority)
{
  if (!__gthread_active_p ())
    return -1;
  else
    {
#ifdef _POSIX_PRIORITY_SCHEDULING
#ifdef _POSIX_THREAD_PRIORITY_SCHEDULING
      pthread_t thread_id = __gthrw_(pthread_self) ();
      int policy;
      struct sched_param params;
      int priority_min, priority_max;

      if (__gthrw_(pthread_getschedparam) (thread_id, &policy, &params) == 0)
	{
	  if ((priority_max = __gthrw_(sched_get_priority_max) (policy)) == -1)
	    return -1;

	  if ((priority_min = __gthrw_(sched_get_priority_min) (policy)) == -1)
	    return -1;

	  if (priority > priority_max)
	    priority = priority_max;
	  else if (priority < priority_min)
	    priority = priority_min;
	  params.sched_priority = priority;

	  /*
	   * The solaris 7 and several other man pages incorrectly state that
	   * this should be a pointer to policy but pthread.h is universally
	   * at odds with this.
	   */
	  if (__gthrw_(pthread_setschedparam) (thread_id, policy, &params) == 0)
	    return 0;
	}
#endif /* _POSIX_THREAD_PRIORITY_SCHEDULING */
#endif /* _POSIX_PRIORITY_SCHEDULING */
      return -1;
    }
}

/* Return the current thread's priority.  */
static inline int
__gthread_objc_thread_get_priority (void)
{
#ifdef _POSIX_PRIORITY_SCHEDULING
#ifdef _POSIX_THREAD_PRIORITY_SCHEDULING
  if (__gthread_active_p ())
    {
      int policy;
      struct sched_param params;

      if (__gthrw_(pthread_getschedparam) (__gthrw_(pthread_self) (), &policy, &params) == 0)
	return params.sched_priority;
      else
	return -1;
    }
  else
#endif /* _POSIX_THREAD_PRIORITY_SCHEDULING */
#endif /* _POSIX_PRIORITY_SCHEDULING */
    return OBJC_THREAD_INTERACTIVE_PRIORITY;
}

/* Yield our process time to another thread.  */
static inline void
__gthread_objc_thread_yield (void)
{
  if (__gthread_active_p ())
    __gthrw_(sched_yield) ();
}

/* Terminate the current thread.  */
static inline int
__gthread_objc_thread_exit (void)
{
  if (__gthread_active_p ())
    /* exit the thread */
    __gthrw_(pthread_exit) (&__objc_thread_exit_status);

  /* Failed if we reached here */
  return -1;
}

/* Returns an integer value which uniquely describes a thread.  */
static inline objc_thread_t
__gthread_objc_thread_id (void)
{
  if (__gthread_active_p ())
    return (objc_thread_t) __gthrw_(pthread_self) ();
  else
    return (objc_thread_t) 1;
}

/* Sets the thread's local storage pointer.  */
static inline int
__gthread_objc_thread_set_data (void *value)
{
  if (__gthread_active_p ())
    return __gthrw_(pthread_setspecific) (_objc_thread_storage, value);
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
  if (__gthread_active_p ())
    return __gthrw_(pthread_getspecific) (_objc_thread_storage);
  else
    return thread_local_storage;
}

/* Backend mutex functions */

/* Allocate a mutex.  */
static inline int
__gthread_objc_mutex_allocate (objc_mutex_t mutex)
{
  if (__gthread_active_p ())
    {
      mutex->backend = objc_malloc (sizeof (pthread_mutex_t));

      if (__gthrw_(pthread_mutex_init) ((pthread_mutex_t *) mutex->backend, NULL))
	{
	  objc_free (mutex->backend);
	  mutex->backend = NULL;
	  return -1;
	}
    }

  return 0;
}

/* Deallocate a mutex.  */
static inline int
__gthread_objc_mutex_deallocate (objc_mutex_t mutex)
{
  if (__gthread_active_p ())
    {
      int count;

      /*
       * Posix Threads specifically require that the thread be unlocked
       * for __gthrw_(pthread_mutex_destroy) to work.
       */

      do
	{
	  count = __gthrw_(pthread_mutex_unlock) ((pthread_mutex_t *) mutex->backend);
	  if (count < 0)
	    return -1;
	}
      while (count);

      if (__gthrw_(pthread_mutex_destroy) ((pthread_mutex_t *) mutex->backend))
	return -1;

      objc_free (mutex->backend);
      mutex->backend = NULL;
    }
  return 0;
}

/* Grab a lock on a mutex.  */
static inline int
__gthread_objc_mutex_lock (objc_mutex_t mutex)
{
  if (__gthread_active_p ()
      && __gthrw_(pthread_mutex_lock) ((pthread_mutex_t *) mutex->backend) != 0)
    {
      return -1;
    }

  return 0;
}

/* Try to grab a lock on a mutex.  */
static inline int
__gthread_objc_mutex_trylock (objc_mutex_t mutex)
{
  if (__gthread_active_p ()
      && __gthrw_(pthread_mutex_trylock) ((pthread_mutex_t *) mutex->backend) != 0)
    {
      return -1;
    }

  return 0;
}

/* Unlock the mutex */
static inline int
__gthread_objc_mutex_unlock (objc_mutex_t mutex)
{
  if (__gthread_active_p ()
      && __gthrw_(pthread_mutex_unlock) ((pthread_mutex_t *) mutex->backend) != 0)
    {
      return -1;
    }

  return 0;
}

/* Backend condition mutex functions */

/* Allocate a condition.  */
static inline int
__gthread_objc_condition_allocate (objc_condition_t condition)
{
  if (__gthread_active_p ())
    {
      condition->backend = objc_malloc (sizeof (pthread_cond_t));

      if (__gthrw_(pthread_cond_init) ((pthread_cond_t *) condition->backend, NULL))
	{
	  objc_free (condition->backend);
	  condition->backend = NULL;
	  return -1;
	}
    }

  return 0;
}

/* Deallocate a condition.  */
static inline int
__gthread_objc_condition_deallocate (objc_condition_t condition)
{
  if (__gthread_active_p ())
    {
      if (__gthrw_(pthread_cond_destroy) ((pthread_cond_t *) condition->backend))
	return -1;

      objc_free (condition->backend);
      condition->backend = NULL;
    }
  return 0;
}

/* Wait on the condition */
static inline int
__gthread_objc_condition_wait (objc_condition_t condition, objc_mutex_t mutex)
{
  if (__gthread_active_p ())
    return __gthrw_(pthread_cond_wait) ((pthread_cond_t *) condition->backend,
			      (pthread_mutex_t *) mutex->backend);
  else
    return 0;
}

/* Wake up all threads waiting on this condition.  */
static inline int
__gthread_objc_condition_broadcast (objc_condition_t condition)
{
  if (__gthread_active_p ())
    return __gthrw_(pthread_cond_broadcast) ((pthread_cond_t *) condition->backend);
  else
    return 0;
}

/* Wake up one thread waiting on this condition.  */
static inline int
__gthread_objc_condition_signal (objc_condition_t condition)
{
  if (__gthread_active_p ())
    return __gthrw_(pthread_cond_signal) ((pthread_cond_t *) condition->backend);
  else
    return 0;
}

#else /* _LIBOBJC */

static inline int
__gthread_once (__gthread_once_t *once, void (*func) (void))
{
  if (__gthread_active_p ())
    return __gthrw_(pthread_once) (once, func);
  else
    return -1;
}

static inline int
__gthread_key_create (__gthread_key_t *key, void (*dtor) (void *))
{
  return __gthrw_(pthread_key_create) (key, dtor);
}

static inline int
__gthread_key_delete (__gthread_key_t key)
{
  return __gthrw_(pthread_key_delete) (key);
}

static inline void *
__gthread_getspecific (__gthread_key_t key)
{
  return __gthrw_(pthread_getspecific) (key);
}

static inline int
__gthread_setspecific (__gthread_key_t key, const void *ptr)
{
  return __gthrw_(pthread_setspecific) (key, ptr);
}

static inline int
__gthread_mutex_lock (__gthread_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthrw_(pthread_mutex_lock) (mutex);
  else
    return 0;
}

static inline int
__gthread_mutex_trylock (__gthread_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthrw_(pthread_mutex_trylock) (mutex);
  else
    return 0;
}

static inline int
__gthread_mutex_unlock (__gthread_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthrw_(pthread_mutex_unlock) (mutex);
  else
    return 0;
}

static inline int
__gthread_recursive_mutex_init_function (__gthread_recursive_mutex_t *mutex)
{
  mutex->depth = 0;
  mutex->owner = (pthread_t) 0;
  return __gthrw_(pthread_mutex_init) (&mutex->actual, NULL);
}

static inline int
__gthread_recursive_mutex_lock (__gthread_recursive_mutex_t *mutex)
{
  if (__gthread_active_p ())
    {
      pthread_t me = __gthrw_(pthread_self) ();

      if (mutex->owner != me)
	{
	  __gthrw_(pthread_mutex_lock) (&mutex->actual);
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
      pthread_t me = __gthrw_(pthread_self) ();

      if (mutex->owner != me)
	{
	  if (__gthrw_(pthread_mutex_trylock) (&mutex->actual))
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
	   mutex->owner = (pthread_t) 0;
	   __gthrw_(pthread_mutex_unlock) (&mutex->actual);
	}
    }
  return 0;
}

#endif /* _LIBOBJC */

#endif /* ! GCC_GTHR_POSIX_H */
