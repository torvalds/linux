/*
 * Copyright (c) 2009 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

#include "hi_locl.h"
#ifdef HAVE_GCD
#include <dispatch/dispatch.h>
#else
#include "heim_threads.h"
#endif

struct heim_icred {
    uid_t uid;
    gid_t gid;
    pid_t pid;
    pid_t session;
};

void
heim_ipc_free_cred(heim_icred cred)
{
    free(cred);
}

uid_t
heim_ipc_cred_get_uid(heim_icred cred)
{
    return cred->uid;
}

gid_t
heim_ipc_cred_get_gid(heim_icred cred)
{
    return cred->gid;
}

pid_t
heim_ipc_cred_get_pid(heim_icred cred)
{
    return cred->pid;
}

pid_t
heim_ipc_cred_get_session(heim_icred cred)
{
    return cred->session;
}


int
_heim_ipc_create_cred(uid_t uid, gid_t gid, pid_t pid, pid_t session, heim_icred *cred)
{
    *cred = calloc(1, sizeof(**cred));
    if (*cred == NULL)
	return ENOMEM;
    (*cred)->uid = uid;
    (*cred)->gid = gid;
    (*cred)->pid = pid;
    (*cred)->session = session;
    return 0;
}

#ifndef HAVE_GCD
struct heim_isemaphore {
    HEIMDAL_MUTEX mutex;
    pthread_cond_t cond;
    long counter;
};
#endif

heim_isemaphore
heim_ipc_semaphore_create(long value)
{
#ifdef HAVE_GCD
    return (heim_isemaphore)dispatch_semaphore_create(value);
#elif !defined(ENABLE_PTHREAD_SUPPORT)
    heim_assert(0, "no semaphore support w/o pthreads");
    return NULL;
#else
    heim_isemaphore s = malloc(sizeof(*s));
    if (s == NULL)
	return NULL;
    HEIMDAL_MUTEX_init(&s->mutex);
    pthread_cond_init(&s->cond, NULL);
    s->counter = value;
    return s;
#endif
}

long
heim_ipc_semaphore_wait(heim_isemaphore s, time_t t)
{
#ifdef HAVE_GCD
    uint64_t timeout;
    if (t == HEIM_IPC_WAIT_FOREVER)
	timeout = DISPATCH_TIME_FOREVER;
    else
	timeout = (uint64_t)t * NSEC_PER_SEC;

    return dispatch_semaphore_wait((dispatch_semaphore_t)s, timeout);
#elif !defined(ENABLE_PTHREAD_SUPPORT)
    heim_assert(0, "no semaphore support w/o pthreads");
    return 0;
#else
    HEIMDAL_MUTEX_lock(&s->mutex);
    /* if counter hits below zero, we get to wait */
    if (--s->counter < 0) {
	int ret;

	if (t == HEIM_IPC_WAIT_FOREVER)
	    ret = pthread_cond_wait(&s->cond, &s->mutex);
	else {
	    struct timespec ts;
	    ts.tv_sec = t;
	    ts.tv_nsec = 0;
	    ret = pthread_cond_timedwait(&s->cond, &s->mutex, &ts);
	}
	if (ret) {
	    HEIMDAL_MUTEX_unlock(&s->mutex);
	    return errno;
	}
    }
    HEIMDAL_MUTEX_unlock(&s->mutex);

    return 0;
#endif
}

long
heim_ipc_semaphore_signal(heim_isemaphore s)
{
#ifdef HAVE_GCD
    return dispatch_semaphore_signal((dispatch_semaphore_t)s);
#elif !defined(ENABLE_PTHREAD_SUPPORT)
    heim_assert(0, "no semaphore support w/o pthreads");
    return EINVAL;
#else
    int wakeup;
    HEIMDAL_MUTEX_lock(&s->mutex);
    wakeup = (++s->counter == 0) ;
    HEIMDAL_MUTEX_unlock(&s->mutex);
    if (wakeup)
	pthread_cond_signal(&s->cond);
    return 0;
#endif
}

void
heim_ipc_semaphore_release(heim_isemaphore s)
{
#ifdef HAVE_GCD
    dispatch_release((dispatch_semaphore_t)s);
#elif !defined(ENABLE_PTHREAD_SUPPORT)
    heim_assert(0, "no semaphore support w/o pthreads");
#else
    HEIMDAL_MUTEX_lock(&s->mutex);
    if (s->counter != 0)
	abort();
    HEIMDAL_MUTEX_unlock(&s->mutex);
    HEIMDAL_MUTEX_destroy(&s->mutex);
    pthread_cond_destroy(&s->cond);
    free(s);
#endif
}

void
heim_ipc_free_data(heim_idata *data)
{
    if (data->data)
	free(data->data);
    data->data = NULL;
    data->length = 0;
}
