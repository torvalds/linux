/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "apr_arch_file_io.h"
#include "apr_strings.h"
#include "apr_portable.h"
#include "apr_thread_mutex.h"
#include "apr_arch_inherit.h"

static apr_status_t file_dup(apr_file_t **new_file, 
                             apr_file_t *old_file, apr_pool_t *p,
                             int which_dup)
{
    int rv;
#ifdef HAVE_DUP3
    int flags = 0;
#endif

    if (which_dup == 2) {
        if ((*new_file) == NULL) {
            /* We can't dup2 unless we have a valid new_file */
            return APR_EINVAL;
        }
#ifdef HAVE_DUP3
        if (!((*new_file)->flags & (APR_FOPEN_NOCLEANUP|APR_INHERIT)))
            flags |= O_CLOEXEC;
        rv = dup3(old_file->filedes, (*new_file)->filedes, flags);
#else
        rv = dup2(old_file->filedes, (*new_file)->filedes);
        if (!((*new_file)->flags & (APR_FOPEN_NOCLEANUP|APR_INHERIT))) {
            int flags;

            if (rv == -1)
                return errno;

            if ((flags = fcntl((*new_file)->filedes, F_GETFD)) == -1)
                return errno;

            flags |= FD_CLOEXEC;
            if (fcntl((*new_file)->filedes, F_SETFD, flags) == -1)
                return errno;

        }
#endif
    } else {
        rv = dup(old_file->filedes);
    }

    if (rv == -1)
        return errno;
    
    if (which_dup == 1) {
        (*new_file) = (apr_file_t *)apr_pcalloc(p, sizeof(apr_file_t));
        (*new_file)->pool = p;
        (*new_file)->filedes = rv;
    }

    (*new_file)->fname = apr_pstrdup(p, old_file->fname);
    (*new_file)->buffered = old_file->buffered;

    /* If the existing socket in a dup2 is already buffered, we
     * have an existing and valid (hopefully) mutex, so we don't
     * want to create it again as we could leak!
     */
#if APR_HAS_THREADS
    if ((*new_file)->buffered && !(*new_file)->thlock && old_file->thlock) {
        apr_thread_mutex_create(&((*new_file)->thlock),
                                APR_THREAD_MUTEX_DEFAULT, p);
    }
#endif
    /* As above, only create the buffer if we haven't already
     * got one.
     */
    if ((*new_file)->buffered && !(*new_file)->buffer) {
        (*new_file)->buffer = apr_palloc(p, old_file->bufsize);
        (*new_file)->bufsize = old_file->bufsize;
    }

    /* this is the way dup() works */
    (*new_file)->blocking = old_file->blocking; 

    /* make sure unget behavior is consistent */
    (*new_file)->ungetchar = old_file->ungetchar;

    /* apr_file_dup2() retains the original cleanup, reflecting 
     * the existing inherit and nocleanup flags.  This means, 
     * that apr_file_dup2() cannot be called against an apr_file_t
     * already closed with apr_file_close, because the expected
     * cleanup was already killed.
     */
    if (which_dup == 2) {
        return APR_SUCCESS;
    }

    /* apr_file_dup() retains all old_file flags with the exceptions
     * of APR_INHERIT and APR_FOPEN_NOCLEANUP.
     * The user must call apr_file_inherit_set() on the dupped 
     * apr_file_t when desired.
     */
    (*new_file)->flags = old_file->flags
                       & ~(APR_INHERIT | APR_FOPEN_NOCLEANUP);

    apr_pool_cleanup_register((*new_file)->pool, (void *)(*new_file),
                              apr_unix_file_cleanup, 
                              apr_unix_child_file_cleanup);
#ifndef WAITIO_USES_POLL
    /* Start out with no pollset.  apr_wait_for_io_or_timeout() will
     * initialize the pollset if needed.
     */
    (*new_file)->pollset = NULL;
#endif
    return APR_SUCCESS;
}

APR_DECLARE(apr_status_t) apr_file_dup(apr_file_t **new_file,
                                       apr_file_t *old_file, apr_pool_t *p)
{
    return file_dup(new_file, old_file, p, 1);
}

APR_DECLARE(apr_status_t) apr_file_dup2(apr_file_t *new_file,
                                        apr_file_t *old_file, apr_pool_t *p)
{
    return file_dup(&new_file, old_file, p, 2);
}

APR_DECLARE(apr_status_t) apr_file_setaside(apr_file_t **new_file,
                                            apr_file_t *old_file,
                                            apr_pool_t *p)
{
    *new_file = (apr_file_t *)apr_pmemdup(p, old_file, sizeof(apr_file_t));
    (*new_file)->pool = p;
    if (old_file->buffered) {
        (*new_file)->buffer = apr_palloc(p, old_file->bufsize);
        (*new_file)->bufsize = old_file->bufsize;
        if (old_file->direction == 1) {
            memcpy((*new_file)->buffer, old_file->buffer, old_file->bufpos);
        }
        else {
            memcpy((*new_file)->buffer, old_file->buffer, old_file->dataRead);
        }
#if APR_HAS_THREADS
        if (old_file->thlock) {
            apr_thread_mutex_create(&((*new_file)->thlock),
                                    APR_THREAD_MUTEX_DEFAULT, p);
            apr_thread_mutex_destroy(old_file->thlock);
        }
#endif /* APR_HAS_THREADS */
    }
    if (old_file->fname) {
        (*new_file)->fname = apr_pstrdup(p, old_file->fname);
    }
    if (!(old_file->flags & APR_FOPEN_NOCLEANUP)) {
        apr_pool_cleanup_register(p, (void *)(*new_file), 
                                  apr_unix_file_cleanup,
                                  ((*new_file)->flags & APR_INHERIT)
                                     ? apr_pool_cleanup_null
                                     : apr_unix_child_file_cleanup);
    }

    old_file->filedes = -1;
    apr_pool_cleanup_kill(old_file->pool, (void *)old_file,
                          apr_unix_file_cleanup);
#ifndef WAITIO_USES_POLL
    (*new_file)->pollset = NULL;
#endif
    return APR_SUCCESS;
}
