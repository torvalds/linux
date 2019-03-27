/*
 * svn_mutex.c: routines for mutual exclusion.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

#include <apr_portable.h>

#include "svn_private_config.h"
#include "private/svn_atomic.h"
#include "private/svn_mutex.h"

/* With CHECKED set to TRUE, LOCKED and OWNER must be set *after* acquiring
 * the MUTEX and be reset *before* releasing it again.  This is sufficient
 * because we only want to check whether the current thread already holds
 * the lock.  And the current thread cannot be acquiring / releasing a lock
 * *while* checking for recursion at the same time.
 */
struct svn_mutex__t
{
#if APR_HAS_THREADS

  apr_thread_mutex_t *mutex;

#else

  /* Truly empty structs are not allowed. */
  int dummy;

#endif
};

svn_error_t *
svn_mutex__init(svn_mutex__t **mutex_p,
                svn_boolean_t mutex_required,
                apr_pool_t *result_pool)
{
  /* always initialize the mutex pointer, even though it is not
     strictly necessary if APR_HAS_THREADS has not been set */
  *mutex_p = NULL;

  if (mutex_required)
    {
      svn_mutex__t *mutex = apr_pcalloc(result_pool, sizeof(*mutex));

#if APR_HAS_THREADS
      apr_status_t status =
          apr_thread_mutex_create(&mutex->mutex,
                                  APR_THREAD_MUTEX_DEFAULT,
                                  result_pool);
      if (status)
        return svn_error_wrap_apr(status, _("Can't create mutex"));
#endif

      *mutex_p = mutex;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mutex__lock(svn_mutex__t *mutex)
{
  if (mutex)
    {
#if APR_HAS_THREADS
      apr_status_t status = apr_thread_mutex_lock(mutex->mutex);
      if (status)
        return svn_error_wrap_apr(status, _("Can't lock mutex"));
#endif
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_mutex__unlock(svn_mutex__t *mutex,
                  svn_error_t *err)
{
  if (mutex)
    {
#if APR_HAS_THREADS
      apr_status_t status = apr_thread_mutex_unlock(mutex->mutex);
      if (status && !err)
        return svn_error_wrap_apr(status, _("Can't unlock mutex"));
#endif
    }

  return err;
}

#if APR_HAS_THREADS

apr_thread_mutex_t *
svn_mutex__get(svn_mutex__t *mutex)
{
  return mutex->mutex;
}

#endif
