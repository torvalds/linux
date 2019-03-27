/*
 * Copyright (c) 2003 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $Id$ */

/*
 * Provide wrapper macros for thread synchronization primitives so we
 * can use native thread functions for those operating system that
 * supports it.
 *
 * This is so libkrb5.so (or more importantly, libgssapi.so) can have
 * thread support while the program that that dlopen(3)s the library
 * don't need to be linked to libpthread.
 */

#ifndef HEIM_THREADS_H
#define HEIM_THREADS_H 1

/* assume headers already included */

#if defined(__NetBSD__) && __NetBSD_Version__ >= 106120000 && __NetBSD_Version__< 299001200 && defined(ENABLE_PTHREAD_SUPPORT)

/*
 * NetBSD have a thread lib that we can use that part of libc that
 * works regardless if application are linked to pthreads or not.
 * NetBSD newer then 2.99.11 just use pthread.h, and the same thing
 * will happen.
 */
#include <threadlib.h>

#define HEIMDAL_MUTEX mutex_t
#define HEIMDAL_MUTEX_INITIALIZER MUTEX_INITIALIZER
#define HEIMDAL_MUTEX_init(m) mutex_init(m, NULL)
#define HEIMDAL_MUTEX_lock(m) mutex_lock(m)
#define HEIMDAL_MUTEX_unlock(m) mutex_unlock(m)
#define HEIMDAL_MUTEX_destroy(m) mutex_destroy(m)

#define HEIMDAL_RWLOCK rwlock_t
#define HEIMDAL_RWLOCK_INITIALIZER RWLOCK_INITIALIZER
#define	HEIMDAL_RWLOCK_init(l) rwlock_init(l, NULL)
#define	HEIMDAL_RWLOCK_rdlock(l) rwlock_rdlock(l)
#define	HEIMDAL_RWLOCK_wrlock(l) rwlock_wrlock(l)
#define	HEIMDAL_RWLOCK_tryrdlock(l) rwlock_tryrdlock(l)
#define	HEIMDAL_RWLOCK_trywrlock(l) rwlock_trywrlock(l)
#define	HEIMDAL_RWLOCK_unlock(l) rwlock_unlock(l)
#define	HEIMDAL_RWLOCK_destroy(l) rwlock_destroy(l)

#define HEIMDAL_thread_key thread_key_t
#define HEIMDAL_key_create(k,d,r) do { r = thr_keycreate(k,d); } while(0)
#define HEIMDAL_setspecific(k,s,r) do { r = thr_setspecific(k,s); } while(0)
#define HEIMDAL_getspecific(k) thr_getspecific(k)
#define HEIMDAL_key_delete(k) thr_keydelete(k)

#elif defined(ENABLE_PTHREAD_SUPPORT) && (!defined(__NetBSD__) || __NetBSD_Version__ >= 299001200)

#include <pthread.h>

#define HEIMDAL_MUTEX pthread_mutex_t
#define HEIMDAL_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define HEIMDAL_MUTEX_init(m) pthread_mutex_init(m, NULL)
#define HEIMDAL_MUTEX_lock(m) pthread_mutex_lock(m)
#define HEIMDAL_MUTEX_unlock(m) pthread_mutex_unlock(m)
#define HEIMDAL_MUTEX_destroy(m) pthread_mutex_destroy(m)

#define HEIMDAL_RWLOCK rwlock_t
#define HEIMDAL_RWLOCK_INITIALIZER RWLOCK_INITIALIZER
#define	HEIMDAL_RWLOCK_init(l) pthread_rwlock_init(l, NULL)
#define	HEIMDAL_RWLOCK_rdlock(l) pthread_rwlock_rdlock(l)
#define	HEIMDAL_RWLOCK_wrlock(l) pthread_rwlock_wrlock(l)
#define	HEIMDAL_RWLOCK_tryrdlock(l) pthread_rwlock_tryrdlock(l)
#define	HEIMDAL_RWLOCK_trywrlock(l) pthread_rwlock_trywrlock(l)
#define	HEIMDAL_RWLOCK_unlock(l) pthread_rwlock_unlock(l)
#define	HEIMDAL_RWLOCK_destroy(l) pthread_rwlock_destroy(l)

#define HEIMDAL_thread_key pthread_key_t
#define HEIMDAL_key_create(k,d,r) do { r = pthread_key_create(k,d); } while(0)
#define HEIMDAL_setspecific(k,s,r) do { r = pthread_setspecific(k,s); } while(0)
#define HEIMDAL_getspecific(k) pthread_getspecific(k)
#define HEIMDAL_key_delete(k) pthread_key_delete(k)

#elif defined(HEIMDAL_DEBUG_THREADS)

/* no threads support, just do consistency checks */
#include <stdlib.h>

#define HEIMDAL_MUTEX int
#define HEIMDAL_MUTEX_INITIALIZER 0
#define HEIMDAL_MUTEX_init(m)  do { (*(m)) = 0; } while(0)
#define HEIMDAL_MUTEX_lock(m)  do { if ((*(m))++ != 0) abort(); } while(0)
#define HEIMDAL_MUTEX_unlock(m) do { if ((*(m))-- != 1) abort(); } while(0)
#define HEIMDAL_MUTEX_destroy(m) do {if ((*(m)) != 0) abort(); } while(0)

#define HEIMDAL_RWLOCK rwlock_t int
#define HEIMDAL_RWLOCK_INITIALIZER 0
#define	HEIMDAL_RWLOCK_init(l) do { } while(0)
#define	HEIMDAL_RWLOCK_rdlock(l) do { } while(0)
#define	HEIMDAL_RWLOCK_wrlock(l) do { } while(0)
#define	HEIMDAL_RWLOCK_tryrdlock(l) do { } while(0)
#define	HEIMDAL_RWLOCK_trywrlock(l) do { } while(0)
#define	HEIMDAL_RWLOCK_unlock(l) do { } while(0)
#define	HEIMDAL_RWLOCK_destroy(l) do { } while(0)

#define HEIMDAL_internal_thread_key 1

#else /* no thread support, no debug case */

#define HEIMDAL_MUTEX int
#define HEIMDAL_MUTEX_INITIALIZER 0
#define HEIMDAL_MUTEX_init(m)  do { (void)(m); } while(0)
#define HEIMDAL_MUTEX_lock(m)  do { (void)(m); } while(0)
#define HEIMDAL_MUTEX_unlock(m) do { (void)(m); } while(0)
#define HEIMDAL_MUTEX_destroy(m) do { (void)(m); } while(0)

#define HEIMDAL_RWLOCK rwlock_t int
#define HEIMDAL_RWLOCK_INITIALIZER 0
#define	HEIMDAL_RWLOCK_init(l) do { } while(0)
#define	HEIMDAL_RWLOCK_rdlock(l) do { } while(0)
#define	HEIMDAL_RWLOCK_wrlock(l) do { } while(0)
#define	HEIMDAL_RWLOCK_tryrdlock(l) do { } while(0)
#define	HEIMDAL_RWLOCK_trywrlock(l) do { } while(0)
#define	HEIMDAL_RWLOCK_unlock(l) do { } while(0)
#define	HEIMDAL_RWLOCK_destroy(l) do { } while(0)

#define HEIMDAL_internal_thread_key 1

#endif /* no thread support */

#ifdef HEIMDAL_internal_thread_key

typedef struct heim_thread_key {
    void *value;
    void (*destructor)(void *);
} heim_thread_key;

#define HEIMDAL_thread_key heim_thread_key
#define HEIMDAL_key_create(k,d,r) \
	do { (k)->value = NULL; (k)->destructor = (d); r = 0; } while(0)
#define HEIMDAL_setspecific(k,s,r) do { (k).value = s ; r = 0; } while(0)
#define HEIMDAL_getspecific(k) ((k).value)
#define HEIMDAL_key_delete(k) do { (*(k).destructor)((k).value); } while(0)

#undef HEIMDAL_internal_thread_key
#endif /* HEIMDAL_internal_thread_key */

#endif /* HEIM_THREADS_H */
