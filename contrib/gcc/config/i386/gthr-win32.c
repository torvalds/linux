/* Implementation of W32-specific threads compatibility routines for
   libgcc2.  */

/* Copyright (C) 1999, 2000, 2002, 2004 Free Software Foundation, Inc.
   Contributed by Mumit Khan <khan@xraylith.wisc.edu>.
   Modified and moved to separate file by Danny Smith
   <dannysmith@users.sourceforge.net>.

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


#include <windows.h>
#ifndef __GTHREAD_HIDE_WIN32API
# define __GTHREAD_HIDE_WIN32API 1
#endif
#undef  __GTHREAD_I486_INLINE_LOCK_PRIMITIVES
#define __GTHREAD_I486_INLINE_LOCK_PRIMITIVES
#include <gthr-win32.h>

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

int
__gthr_win32_once (__gthread_once_t *once, void (*func) (void))
{
  if (once == NULL || func == NULL)
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

int
__gthr_win32_key_create (__gthread_key_t *key, void (*dtor) (void *))
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

int
__gthr_win32_key_delete (__gthread_key_t key)
{
  return (TlsFree (key) != 0) ? 0 : (int) GetLastError ();
}

void *
__gthr_win32_getspecific (__gthread_key_t key)
{
  DWORD lasterror;
  void *ptr;
  lasterror = GetLastError();
  ptr = TlsGetValue(key);
  SetLastError( lasterror );
  return ptr;
}

int
__gthr_win32_setspecific (__gthread_key_t key, const void *ptr)
{
  return (TlsSetValue (key, (void*) ptr) != 0) ? 0 : (int) GetLastError ();
}

void
__gthr_win32_mutex_init_function (__gthread_mutex_t *mutex)
{
  mutex->counter = -1;
  mutex->sema = CreateSemaphore (NULL, 0, 65535, NULL);
}

int
__gthr_win32_mutex_lock (__gthread_mutex_t *mutex)
{
  if (InterlockedIncrement (&mutex->counter) == 0 ||
      WaitForSingleObject (mutex->sema, INFINITE) == WAIT_OBJECT_0)
    return 0;
  else
    {
      /* WaitForSingleObject returns WAIT_FAILED, and we can only do
         some best-effort cleanup here.  */
      InterlockedDecrement (&mutex->counter);
      return 1;
    }
}

int
__gthr_win32_mutex_trylock (__gthread_mutex_t *mutex)
{
  if (__GTHR_W32_InterlockedCompareExchange (&mutex->counter, 0, -1) < 0)
    return 0;
  else
    return 1;
}

int
__gthr_win32_mutex_unlock (__gthread_mutex_t *mutex)
{
  if (InterlockedDecrement (&mutex->counter) >= 0)
    return ReleaseSemaphore (mutex->sema, 1, NULL) ? 0 : 1;
  else
    return 0;
}

void
__gthr_win32_recursive_mutex_init_function (__gthread_recursive_mutex_t *mutex)
{
  mutex->counter = -1;
  mutex->depth = 0;
  mutex->owner = 0;
  mutex->sema = CreateSemaphore (NULL, 0, 65535, NULL);
}

int
__gthr_win32_recursive_mutex_lock (__gthread_recursive_mutex_t *mutex)
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
  return 0;
}

int
__gthr_win32_recursive_mutex_trylock (__gthread_recursive_mutex_t *mutex)
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

  return 0;
}

int
__gthr_win32_recursive_mutex_unlock (__gthread_recursive_mutex_t *mutex)
{
  --(mutex->depth);
  if (mutex->depth == 0)
    {
      mutex->owner = 0;

      if (InterlockedDecrement (&mutex->counter) >= 0)
	return ReleaseSemaphore (mutex->sema, 1, NULL) ? 0 : 1;
    }

  return 0;
}
