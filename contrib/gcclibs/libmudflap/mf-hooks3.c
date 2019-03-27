/* Mudflap: narrow-pointer bounds-checking by tree rewriting.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Frank Ch. Eigler <fche@redhat.com>
   and Graydon Hoare <graydon@redhat.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


#include "config.h"

#ifndef HAVE_SOCKLEN_T
#define socklen_t int
#endif

/* These attempt to coax various unix flavours to declare all our
   needed tidbits in the system headers.  */
#if !defined(__FreeBSD__) && !defined(__APPLE__)
#define _POSIX_SOURCE
#endif /* Some BSDs break <sys/socket.h> if this is defined. */
#define _GNU_SOURCE
#define _XOPEN_SOURCE
#define _BSD_TYPES
#define __EXTENSIONS__
#define _ALL_SOURCE
#define _LARGE_FILE_API
#define _XOPEN_SOURCE_EXTENDED 1

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>

#include "mf-runtime.h"
#include "mf-impl.h"

#ifdef _MUDFLAP
#error "Do not compile this file with -fmudflap!"
#endif

#ifndef LIBMUDFLAPTH
#error "pthreadstuff is to be included only in libmudflapth"
#endif

/* ??? Why isn't this done once in the header files.  */
DECLARE(void *, malloc, size_t sz);
DECLARE(void, free, void *ptr);
DECLARE(int, pthread_create, pthread_t *thr, const pthread_attr_t *attr,
	void * (*start) (void *), void *arg);


/* Multithreading support hooks.  */


#ifndef HAVE_TLS
/* We don't have TLS.  Ordinarily we could use pthread keys, but since we're
   commandeering malloc/free that presents a few problems.  The first is that
   we'll recurse from __mf_get_state to pthread_setspecific to malloc back to
   __mf_get_state during thread startup.  This can be solved with clever uses
   of a mutex.  The second problem is that thread shutdown is indistinguishable
   from thread startup, since libpthread is deallocating our state variable.
   I've no good solution for this.

   Which leaves us to handle this mess by totally by hand.  */

/* Yes, we want this prime.  If pthread_t is a pointer, it's almost always
   page aligned, and if we use a smaller power of 2, this results in "%N"
   being the worst possible hash -- all threads hash to zero.  */
#define LIBMUDFLAPTH_THREADS_MAX 1021

struct mf_thread_data
{
  pthread_t self;
  unsigned char used_p;
  unsigned char state;
};

static struct mf_thread_data mf_thread_data[LIBMUDFLAPTH_THREADS_MAX];
static pthread_mutex_t mf_thread_data_lock = PTHREAD_MUTEX_INITIALIZER;

#define PTHREAD_HASH(p) ((unsigned long) (p) % LIBMUDFLAPTH_THREADS_MAX)

static struct mf_thread_data *
__mf_find_threadinfo (int alloc)
{
  pthread_t self = pthread_self ();
  unsigned long hash = PTHREAD_HASH (self);
  unsigned long rehash;

#ifdef __alpha__
  /* Alpha has the loosest memory ordering rules of all.  We need a memory
     barrier to flush the reorder buffer before considering a *read* of a
     shared variable.  Since we're not always taking a lock, we have to do
     this by hand.  */
  __sync_synchronize ();
#endif

  rehash = hash;
  while (1)
    {
      if (mf_thread_data[rehash].used_p && mf_thread_data[rehash].self == self)
	return &mf_thread_data[rehash];

      rehash += 7;
      if (rehash >= LIBMUDFLAPTH_THREADS_MAX)
	rehash -= LIBMUDFLAPTH_THREADS_MAX;
      if (rehash == hash)
	break;
    }

  if (alloc)
    {
      pthread_mutex_lock (&mf_thread_data_lock);

      rehash = hash;
      while (1)
	{
	  if (!mf_thread_data[rehash].used_p)
	    {
	      mf_thread_data[rehash].self = self;
	      __sync_synchronize ();
	      mf_thread_data[rehash].used_p = 1;

	      pthread_mutex_unlock (&mf_thread_data_lock);
	      return &mf_thread_data[rehash];
	    }

	  rehash += 7;
	  if (rehash >= LIBMUDFLAPTH_THREADS_MAX)
	    rehash -= LIBMUDFLAPTH_THREADS_MAX;
	  if (rehash == hash)
	    break;
	}

      pthread_mutex_unlock (&mf_thread_data_lock);
    }

  return NULL;
}

enum __mf_state_enum
__mf_get_state (void)
{
  struct mf_thread_data *data = __mf_find_threadinfo (0);
  if (data)
    return data->state;

  /* If we've never seen this thread before, consider it to be in the
     reentrant state.  The state gets reset to active for the main thread
     in __mf_init, and for child threads in __mf_pthread_spawner.

     The trickiest bit here is that the LinuxThreads pthread_manager thread
     should *always* be considered to be reentrant, so that none of our 
     hooks actually do anything.  Why?  Because that thread isn't a real
     thread from the point of view of the thread library, and so lots of
     stuff isn't initialized, leading to SEGV very quickly.  Even calling
     pthread_self is a bit suspect, but it happens to work.  */

  return reentrant;
}

void
__mf_set_state (enum __mf_state_enum new_state)
{
  struct mf_thread_data *data = __mf_find_threadinfo (1);
  data->state = new_state;
}
#endif

/* The following two functions are used only with __mf_opts.heur_std_data.
   We're interested in recording the location of the thread-local errno
   variable.

   Note that this doesn't handle TLS references in general; we have no
   visibility into __tls_get_data for when that memory is allocated at
   runtime.  Hopefully we get to see the malloc or mmap operation that
   eventually allocates the backing store.  */

/* Describe the startup information for a new user thread.  */
struct mf_thread_start_info
{
  /* The user's thread entry point and argument.  */
  void * (*user_fn)(void *);
  void *user_arg;
};


static void
__mf_pthread_cleanup (void *arg)
{
  if (__mf_opts.heur_std_data)
    __mf_unregister (&errno, sizeof (errno), __MF_TYPE_GUESS);

#ifndef HAVE_TLS
  struct mf_thread_data *data = __mf_find_threadinfo (0);
  if (data)
    data->used_p = 0;
#endif
}


static void *
__mf_pthread_spawner (void *arg)
{
  void *result = NULL;

  __mf_set_state (active);

  /* NB: We could use __MF_TYPE_STATIC here, but we guess that the thread
     errno is coming out of some dynamically allocated pool that we already
     know of as __MF_TYPE_HEAP. */
  if (__mf_opts.heur_std_data)
    __mf_register (&errno, sizeof (errno), __MF_TYPE_GUESS,
		   "errno area (thread)");

  /* We considered using pthread_key_t objects instead of these
     cleanup stacks, but they were less cooperative with the
     interposed malloc hooks in libmudflap.  */
  /* ??? The pthread_key_t problem is solved above...  */
  pthread_cleanup_push (__mf_pthread_cleanup, NULL);

  /* Extract given entry point and argument.  */
  struct mf_thread_start_info *psi = arg;
  void * (*user_fn)(void *) = psi->user_fn;
  void *user_arg = psi->user_arg;
  CALL_REAL (free, arg);

  result = (*user_fn)(user_arg);

  pthread_cleanup_pop (1 /* execute */);

  return result;
}


#if PIC
/* A special bootstrap variant. */
int
__mf_0fn_pthread_create (pthread_t *thr, const pthread_attr_t *attr,
			 void * (*start) (void *), void *arg)
{
  return -1;
}
#endif


#undef pthread_create
WRAPPER(int, pthread_create, pthread_t *thr, const pthread_attr_t *attr,
	 void * (*start) (void *), void *arg)
{
  struct mf_thread_start_info *si;

  TRACE ("pthread_create\n");

  /* Fill in startup-control fields.  */
  si = CALL_REAL (malloc, sizeof (*si));
  si->user_fn = start;
  si->user_arg = arg;

  /* Actually create the thread.  */
  return CALL_REAL (pthread_create, thr, attr, __mf_pthread_spawner, si);
}
