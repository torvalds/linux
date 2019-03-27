/* Threads compatibility routines for libgcc2 and libobjc.  */
/* Compile this one with gcc.  */

/* Copyright (C) 1999, 2000, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Mumit Khan <khan@xraylith.wisc.edu>.

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

#ifndef GCC_GTHR_WIN32_H
#define GCC_GTHR_WIN32_H

/* Windows32 threads specific definitions. The windows32 threading model
   does not map well into pthread-inspired gcc's threading model, and so
   there are caveats one needs to be aware of.

   1. The destructor supplied to __gthread_key_create is ignored for
      generic x86-win32 ports. This will certainly cause memory leaks
      due to unreclaimed eh contexts (sizeof (eh_context) is at least
      24 bytes for x86 currently).

      This memory leak may be significant for long-running applications
      that make heavy use of C++ EH.

      However, Mingw runtime (version 0.3 or newer) provides a mechanism
      to emulate pthreads key dtors; the runtime provides a special DLL,
      linked in if -mthreads option is specified, that runs the dtors in
      the reverse order of registration when each thread exits. If
      -mthreads option is not given, a stub is linked in instead of the
      DLL, which results in memory leak. Other x86-win32 ports can use
      the same technique of course to avoid the leak.

   2. The error codes returned are non-POSIX like, and cast into ints.
      This may cause incorrect error return due to truncation values on
      hw where sizeof (DWORD) > sizeof (int).

   3. We are currently using a special mutex instead of the Critical
      Sections, since Win9x does not support TryEnterCriticalSection
      (while NT does).

   The basic framework should work well enough. In the long term, GCC
   needs to use Structured Exception Handling on Windows32.  */

#define __GTHREADS 1

#include <errno.h>
#ifdef __MINGW32__
#include <_mingw.h>
#endif

#ifdef _LIBOBJC

/* This is necessary to prevent windef.h (included from windows.h) from
   defining its own BOOL as a typedef.  */
#ifndef __OBJC__
#define __OBJC__
#endif
#include <windows.h>
/* Now undef the windows BOOL.  */
#undef BOOL

/* Key structure for maintaining thread specific storage */
static DWORD	__gthread_objc_data_tls = (DWORD) -1;

/* Backend initialization functions */

/* Initialize the threads subsystem.  */
int
__gthread_objc_init_thread_system (void)
{
  /* Initialize the thread storage key.  */
  if ((__gthread_objc_data_tls = TlsAlloc ()) != (DWORD) -1)
    return 0;
  else
    return -1;
}

/* Close the threads subsystem.  */
int
__gthread_objc_close_thread_system (void)
{
  if (__gthread_objc_data_tls != (DWORD) -1)
    TlsFree (__gthread_objc_data_tls);
  return 0;
}

/* Backend thread functions */

/* Create a new thread of execution.  */
objc_thread_t
__gthread_objc_thread_detach (void (*func)(void *arg), void *arg)
{
  DWORD	thread_id = 0;
  HANDLE win32_handle;

  if (!(win32_handle = CreateThread (NULL, 0, (LPTHREAD_START_ROUTINE) func,
				     arg, 0, &thread_id)))
    thread_id = 0;

  return (objc_thread_t) thread_id;
}

/* Set the current thread's priority.  */
int
__gthread_objc_thread_set_priority (int priority)
{
  int sys_priority = 0;

  switch (priority)
    {
    case OBJC_THREAD_INTERACTIVE_PRIORITY:
      sys_priority = THREAD_PRIORITY_NORMAL;
      break;
    default:
    case OBJC_THREAD_BACKGROUND_PRIORITY:
      sys_priority = THREAD_PRIORITY_BELOW_NORMAL;
      break;
    case OBJC_THREAD_LOW_PRIORITY:
      sys_priority = THREAD_PRIORITY_LOWEST;
      break;
    }

  /* Change priority */
  if (SetThreadPriority (GetCurrentThread (), sys_priority))
    return 0;
  else
    return -1;
}

/* Return the current thread's priority.  */
int
__gthread_objc_thread_get_priority (void)
{
  int sys_priority;

  sys_priority = GetThreadPriority (GetCurrentThread ());

  switch (sys_priority)
    {
    case THREAD_PRIORITY_HIGHEST:
    case THREAD_PRIORITY_TIME_CRITICAL:
    case THREAD_PRIORITY_ABOVE_NORMAL:
    case THREAD_PRIORITY_NORMAL:
      return OBJC_THREAD_INTERACTIVE_PRIORITY;

    default:
    case THREAD_PRIORITY_BELOW_NORMAL:
      return OBJC_THREAD_BACKGROUND_PRIORITY;

    case THREAD_PRIORITY_IDLE:
    case THREAD_PRIORITY_LOWEST:
      return OBJC_THREAD_LOW_PRIORITY;
    }

  /* Couldn't get priority.  */
  return -1;
}

/* Yield our process time to another thread.  */
void
__gthread_objc_thread_yield (void)
{
  Sleep (0);
}

/* Terminate the current thread.  */
int
__gthread_objc_thread_exit (void)
{
  /* exit the thread */
  ExitThread (__objc_thread_exit_status);

  /* Failed if we reached here */
  return -1;
}

/* Returns an integer value which uniquely describes a thread.  */
objc_thread_t
__gthread_objc_thread_id (void)
{
  return (objc_thread_t) GetCurrentThreadId ();
}

/* Sets the thread's local storage pointer.  */
int
__gthread_objc_thread_set_data (void *value)
{
  if (TlsSetValue (__gthread_objc_data_tls, value))
    return 0;
  else
    return -1;
}

/* Returns the thread's local storage pointer.  */
void *
__gthread_objc_thread_get_data (void)
{
  DWORD lasterror;
  void *ptr;

  lasterror = GetLastError ();

  ptr = TlsGetValue (__gthread_objc_data_tls);          /* Return thread data.  */

  SetLastError (lasterror);

  return ptr;
}

/* Backend mutex functions */

/* Allocate a mutex.  */
int
__gthread_objc_mutex_allocate (objc_mutex_t mutex)
{
  if ((mutex->backend = (void *) CreateMutex (NULL, 0, NULL)) == NULL)
    return -1;
  else
    return 0;
}

/* Deallocate a mutex.  */
int
__gthread_objc_mutex_deallocate (objc_mutex_t mutex)
{
  CloseHandle ((HANDLE) (mutex->backend));
  return 0;
}

/* Grab a lock on a mutex.  */
int
__gthread_objc_mutex_lock (objc_mutex_t mutex)
{
  int status;

  status = WaitForSingleObject ((HANDLE) (mutex->backend), INFINITE);
  if (status != WAIT_OBJECT_0 && status != WAIT_ABANDONED)
    return -1;
  else
    return 0;
}

/* Try to grab a lock on a mutex.  */
int
__gthread_objc_mutex_trylock (objc_mutex_t mutex)
{
  int status;

  status = WaitForSingleObject ((HANDLE) (mutex->backend), 0);
  if (status != WAIT_OBJECT_0 && status != WAIT_ABANDONED)
    return -1;
  else
    return 0;
}

/* Unlock the mutex */
int
__gthread_objc_mutex_unlock (objc_mutex_t mutex)
{
  if (ReleaseMutex ((HANDLE) (mutex->backend)) == 0)
    return -1;
  else
    return 0;
}

/* Backend condition mutex functions */

/* Allocate a condition.  */
int
__gthread_objc_condition_allocate (objc_condition_t condition)
{
  /* Unimplemented.  */
  return -1;
}

/* Deallocate a condition.  */
int
__gthread_objc_condition_deallocate (objc_condition_t condition)
{
  /* Unimplemented.  */
  return -1;
}

/* Wait on the condition */
int
__gthread_objc_condition_wait (objc_condition_t condition, objc_mutex_t mutex)
{
  /* Unimplemented.  */
  return -1;
}

/* Wake up all threads waiting on this condition.  */
int
__gthread_objc_condition_broadcast (objc_condition_t condition)
{
  /* Unimplemented.  */
  return -1;
}

/* Wake up one thread waiting on this condition.  */
int
__gthread_objc_condition_signal (objc_condition_t condition)
{
  /* Unimplemented.  */
  return -1;
}

#else /* _LIBOBJC */

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long __gthread_key_t;

typedef struct {
  int done;
  long started;
} __gthread_once_t;

typedef struct {
  long counter;
  void *sema;
} __gthread_mutex_t;

typedef struct {
  long counter;
  long depth;
  unsigned long owner;
  void *sema;
} __gthread_recursive_mutex_t;

#define __GTHREAD_ONCE_INIT {0, -1}
#define __GTHREAD_MUTEX_INIT_FUNCTION __gthread_mutex_init_function
#define __GTHREAD_MUTEX_INIT_DEFAULT {-1, 0}
#define __GTHREAD_RECURSIVE_MUTEX_INIT_FUNCTION \
  __gthread_recursive_mutex_init_function
#define __GTHREAD_RECURSIVE_MUTEX_INIT_DEFAULT {-1, 0, 0, 0}

#if __MINGW32_MAJOR_VERSION >= 1 || \
  (__MINGW32_MAJOR_VERSION == 0 && __MINGW32_MINOR_VERSION > 2)
#define MINGW32_SUPPORTS_MT_EH 1
/* Mingw runtime >= v0.3 provides a magic variable that is set to nonzero
   if -mthreads option was specified, or 0 otherwise. This is to get around
   the lack of weak symbols in PE-COFF.  */
extern int _CRT_MT;
extern int __mingwthr_key_dtor (unsigned long, void (*) (void *));
#endif /* __MINGW32__ version */

/* The Windows95 kernel does not export InterlockedCompareExchange.
   This provides a substitute.   When building apps that reference
   gthread_mutex_try_lock, the  __GTHREAD_I486_INLINE_LOCK_PRIMITIVES
   macro  must be defined if Windows95 is a target.  Currently
   gthread_mutex_try_lock is not referenced by libgcc or libstdc++.  */
#ifdef __GTHREAD_I486_INLINE_LOCK_PRIMITIVES
static inline long
__gthr_i486_lock_cmp_xchg(long *dest, long xchg, long comperand)
{
  long result;
  __asm__ __volatile__ ("\n\
	lock\n\
	cmpxchg{l} {%4, %1|%1, %4}\n"
	: "=a" (result), "=m" (*dest)
	: "0" (comperand), "m" (*dest), "r" (xchg)
	: "cc");
  return result;
}
#define __GTHR_W32_InterlockedCompareExchange __gthr_i486_lock_cmp_xchg
#else  /* __GTHREAD_I486_INLINE_LOCK_PRIMITIVES */
#define __GTHR_W32_InterlockedCompareExchange InterlockedCompareExchange
#endif /* __GTHREAD_I486_INLINE_LOCK_PRIMITIVES */

static inline int
__gthread_active_p (void)
{
#ifdef MINGW32_SUPPORTS_MT_EH
  return _CRT_MT;
#else
  return 1;
#endif
}

#if __GTHREAD_HIDE_WIN32API

/* The implementations are in config/i386/gthr-win32.c in libgcc.a.
   Only stubs are exposed to avoid polluting the C++ namespace with
   windows api definitions.  */

extern int __gthr_win32_once (__gthread_once_t *, void (*) (void));
extern int __gthr_win32_key_create (__gthread_key_t *, void (*) (void*));
extern int __gthr_win32_key_delete (__gthread_key_t);
extern void * __gthr_win32_getspecific (__gthread_key_t);
extern int __gthr_win32_setspecific (__gthread_key_t, const void *);
extern void __gthr_win32_mutex_init_function (__gthread_mutex_t *);
extern int __gthr_win32_mutex_lock (__gthread_mutex_t *);
extern int __gthr_win32_mutex_trylock (__gthread_mutex_t *);
extern int __gthr_win32_mutex_unlock (__gthread_mutex_t *);
extern void
  __gthr_win32_recursive_mutex_init_function (__gthread_recursive_mutex_t *);
extern int __gthr_win32_recursive_mutex_lock (__gthread_recursive_mutex_t *);
extern int
  __gthr_win32_recursive_mutex_trylock (__gthread_recursive_mutex_t *);
extern int __gthr_win32_recursive_mutex_unlock (__gthread_recursive_mutex_t *);

static inline int
__gthread_once (__gthread_once_t *once, void (*func) (void))
{
  if (__gthread_active_p ())
    return __gthr_win32_once (once, func);
  else
    return -1;
}

static inline int
__gthread_key_create (__gthread_key_t *key, void (*dtor) (void *))
{
  return __gthr_win32_key_create (key, dtor);
}

static inline int
__gthread_key_delete (__gthread_key_t key)
{
  return __gthr_win32_key_delete (key);
}

static inline void *
__gthread_getspecific (__gthread_key_t key)
{
  return __gthr_win32_getspecific (key);
}

static inline int
__gthread_setspecific (__gthread_key_t key, const void *ptr)
{
  return __gthr_win32_setspecific (key, ptr);
}

static inline void
__gthread_mutex_init_function (__gthread_mutex_t *mutex)
{
  __gthr_win32_mutex_init_function (mutex);
}

static inline int
__gthread_mutex_lock (__gthread_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthr_win32_mutex_lock (mutex);
  else
    return 0;
}

static inline int
__gthread_mutex_trylock (__gthread_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthr_win32_mutex_trylock (mutex);
  else
    return 0;
}

static inline int
__gthread_mutex_unlock (__gthread_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthr_win32_mutex_unlock (mutex);
  else
    return 0;
}

static inline void
__gthread_recursive_mutex_init_function (__gthread_recursive_mutex_t *mutex)
{
   __gthr_win32_recursive_mutex_init_function (mutex);
}

static inline int
__gthread_recursive_mutex_lock (__gthread_recursive_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthr_win32_recursive_mutex_lock (mutex);
  else
    return 0;
}

static inline int
__gthread_recursive_mutex_trylock (__gthread_recursive_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthr_win32_recursive_mutex_trylock (mutex);
  else
    return 0;
}

static inline int
__gthread_recursive_mutex_unlock (__gthread_recursive_mutex_t *mutex)
{
  if (__gthread_active_p ())
    return __gthr_win32_recursive_mutex_unlock (mutex);
  else
    return 0;
}

#else /* ! __GTHREAD_HIDE_WIN32API */

#include <windows.h>
#include <errno.h>

static inline int
__gthread_once (__gthread_once_t *once, void (*func) (void))
{
  if (! __gthread_active_p ())
    return -1;
  else if (once == NULL || func == NULL)
    return EINVAL;

  if (! once->done)
    {
      if (InterlockedIncrement (&(once->started)) == 0)
	{
	  (*func) ();
	  once->done = TRUE;
	}
      else
	{
	  /* Another thread is currently executing the code, so wait for it
	     to finish; yield the CPU in the meantime.  If performance
	     does become an issue, the solution is to use an Event that
	     we wait on here (and set above), but that implies a place to
	     create the event before this routine is called.  */
	  while (! once->done)
	    Sleep (0);
	}
    }

  return 0;
}

/* Windows32 thread local keys don't support destructors; this leads to
   leaks, especially in threaded applications making extensive use of
   C++ EH. Mingw uses a thread-support DLL to work-around this problem.  */
static inline int
__gthread_key_create (__gthread_key_t *key, void (*dtor) (void *))
{
  int status = 0;
  DWORD tls_index = TlsAlloc ();
  if (tls_index != 0xFFFFFFFF)
    {
      *key = tls_index;
#ifdef MINGW32_SUPPORTS_MT_EH
      /* Mingw runtime will run the dtors in reverse order for each thread
         when the thread exits.  */
      status = __mingwthr_key_dtor (*key, dtor);
#endif
    }
  else
    status = (int) GetLastError ();
  return status;
}

static inline int
__gthread_key_delete (__gthread_key_t key)
{
  return (TlsFree (key) != 0) ? 0 : (int) GetLastError ();
}

static inline void *
__gthread_getspecific (__gthread_key_t key)
{
  DWORD lasterror;
  void *ptr;

  lasterror = GetLastError ();

  ptr = TlsGetValue (key);

  SetLastError (lasterror);

  return ptr;
}

static inline int
__gthread_setspecific (__gthread_key_t key, const void *ptr)
{
  return (TlsSetValue (key, (void*) ptr) != 0) ? 0 : (int) GetLastError ();
}

static inline void
__gthread_mutex_init_function (__gthread_mutex_t *mutex)
{
  mutex->counter = -1;
  mutex->sema = CreateSemaphore (NULL, 0, 65535, NULL);
}

static inline int
__gthread_mutex_lock (__gthread_mutex_t *mutex)
{
  int status = 0;

  if (__gthread_active_p ())
    {
      if (InterlockedIncrement (&mutex->counter) == 0 ||
	  WaitForSingleObject (mutex->sema, INFINITE) == WAIT_OBJECT_0)
	status = 0;
      else
	{
	  /* WaitForSingleObject returns WAIT_FAILED, and we can only do
	     some best-effort cleanup here.  */
	  InterlockedDecrement (&mutex->counter);
	  status = 1;
	}
    }
  return status;
}

static inline int
__gthread_mutex_trylock (__gthread_mutex_t *mutex)
{
  int status = 0;

  if (__gthread_active_p ())
    {
      if (__GTHR_W32_InterlockedCompareExchange (&mutex->counter, 0, -1) < 0)
	status = 0;
      else
	status = 1;
    }
  return status;
}

static inline int
__gthread_mutex_unlock (__gthread_mutex_t *mutex)
{
  if (__gthread_active_p ())
    {
      if (InterlockedDecrement (&mutex->counter) >= 0)
	return ReleaseSemaphore (mutex->sema, 1, NULL) ? 0 : 1;
    }
  return 0;
}

static inline void
__gthread_recursive_mutex_init_function (__gthread_recursive_mutex_t *mutex)
{
  mutex->counter = -1;
  mutex->depth = 0;
  mutex->owner = 0;
  mutex->sema = CreateSemaphore (NULL, 0, 65535, NULL);
}

static inline int
__gthread_recursive_mutex_lock (__gthread_recursive_mutex_t *mutex)
{
  if (__gthread_active_p ())
    {
      DWORD me = GetCurrentThreadId();
      if (InterlockedIncrement (&mutex->counter) == 0)
	{
	  mutex->depth = 1;
	  mutex->owner = me;
	}
      else if (mutex->owner == me)
	{
	  InterlockedDecrement (&mutex->counter);
	  ++(mutex->depth);
	}
      else if (WaitForSingleObject (mutex->sema, INFINITE) == WAIT_OBJECT_0)
	{
	  mutex->depth = 1;
	  mutex->owner = me;
	}
      else
	{
	  /* WaitForSingleObject returns WAIT_FAILED, and we can only do
	     some best-effort cleanup here.  */
	  InterlockedDecrement (&mutex->counter);
	  return 1;
	}
    }
  return 0;
}

static inline int
__gthread_recursive_mutex_trylock (__gthread_recursive_mutex_t *mutex)
{
  if (__gthread_active_p ())
    {
      DWORD me = GetCurrentThreadId();
      if (__GTHR_W32_InterlockedCompareExchange (&mutex->counter, 0, -1) < 0)
	{
	  mutex->depth = 1;
	  mutex->owner = me;
	}
      else if (mutex->owner == me)
	++(mutex->depth);
      else
	return 1;
    }
  return 0;
}

static inline int
__gthread_recursive_mutex_unlock (__gthread_recursive_mutex_t *mutex)
{
  if (__gthread_active_p ())
    {
      --(mutex->depth);
      if (mutex->depth == 0)
	{
	  mutex->owner = 0;

	  if (InterlockedDecrement (&mutex->counter) >= 0)
	    return ReleaseSemaphore (mutex->sema, 1, NULL) ? 0 : 1;
	}
    }
  return 0;
}

#endif /*  __GTHREAD_HIDE_WIN32API */

#ifdef __cplusplus
}
#endif

#endif /* _LIBOBJC */

#endif /* ! GCC_GTHR_WIN32_H */
