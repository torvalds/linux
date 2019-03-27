/* pool.c:  pool wrappers for Subversion
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



#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include <apr.h>
#include <apr_version.h>
#include <apr_general.h>
#include <apr_pools.h>

#include "svn_pools.h"

#include "pools.h"

#if APR_POOL_DEBUG
/* file_line for the non-debug case. */
static const char SVN_FILE_LINE_UNDEFINED[] = "svn:<undefined>";
#endif /* APR_POOL_DEBUG */



/*-----------------------------------------------------------------*/


/* Pool allocation handler which just aborts, since we aren't generally
   prepared to deal with out-of-memory errors.
 */
static int
abort_on_pool_failure(int retcode)
{
  /* Don't translate this string! It requires memory allocation to do so!
     And we don't have any of it... */
  printf("libsvn: Out of memory - terminating application.\n");

#ifdef WIN32
  /* Provide a way to distinguish the out-of-memory error from abort(). */
  if (retcode == APR_ENOMEM)
    RaiseException(STATUS_NO_MEMORY, EXCEPTION_NONCONTINUABLE, 0, NULL);
#endif

  abort();
  return 0; /* not reached */
}


#if APR_POOL_DEBUG
#undef svn_pool_create_ex
#endif /* APR_POOL_DEBUG */

#if !APR_POOL_DEBUG

apr_pool_t *
svn_pool_create_ex(apr_pool_t *parent_pool, apr_allocator_t *allocator)
{
  apr_pool_t *pool;
  apr_pool_create_ex(&pool, parent_pool, abort_on_pool_failure, allocator);
  return pool;
}

/* Wrapper that ensures binary compatibility */
apr_pool_t *
svn_pool_create_ex_debug(apr_pool_t *pool, apr_allocator_t *allocator,
                         const char *file_line)
{
  return svn_pool_create_ex(pool, allocator);
}

#else /* APR_POOL_DEBUG */

apr_pool_t *
svn_pool_create_ex_debug(apr_pool_t *parent_pool, apr_allocator_t *allocator,
                         const char *file_line)
{
  apr_pool_t *pool;
  apr_pool_create_ex_debug(&pool, parent_pool, abort_on_pool_failure,
                           allocator, file_line);
  return pool;
}

/* Wrapper that ensures binary compatibility */
apr_pool_t *
svn_pool_create_ex(apr_pool_t *pool, apr_allocator_t *allocator)
{
  return svn_pool_create_ex_debug(pool, allocator, SVN_FILE_LINE_UNDEFINED);
}

#endif /* APR_POOL_DEBUG */

apr_allocator_t *
svn_pool_create_allocator(svn_boolean_t thread_safe)
{
  apr_allocator_t *allocator;
  apr_pool_t *pool;

  /* create the allocator and limit it's internal free list to keep
   * memory usage in check */

  if (apr_allocator_create(&allocator))
    abort_on_pool_failure(EXIT_FAILURE);

  apr_allocator_max_free_set(allocator, SVN_ALLOCATOR_RECOMMENDED_MAX_FREE);

  /* create the root pool */

  pool = svn_pool_create_ex(NULL, allocator);
  apr_allocator_owner_set(allocator, pool);

#if APR_POOL_DEBUG
  apr_pool_tag (pool, "svn root pool");
#endif

  /* By default, allocators are *not* thread-safe. We must provide a mutex
   * if we want thread-safety for that mutex. */

#if APR_HAS_THREADS
  if (thread_safe)
    {
      apr_thread_mutex_t *mutex;
      apr_thread_mutex_create(&mutex, APR_THREAD_MUTEX_DEFAULT, pool);
      apr_allocator_mutex_set(allocator, mutex);
    }
#endif

  /* better safe than sorry */
  SVN_ERR_ASSERT_NO_RETURN(allocator != NULL);

  return allocator;
}


/*
 * apr_pool_create_core_ex was introduced in APR 1.3.0, then
 * deprecated and renamed to apr_pool_create_unmanaged_ex in 1.3.3.
 * Since our minimum requirement is APR 1.3.0, one or the other of
 * these functions will always be available.
 */
#if !APR_VERSION_AT_LEAST(1,3,3)
#define apr_pool_create_unmanaged_ex apr_pool_create_core_ex
#endif

/* Private function that creates an unmanaged pool. */
apr_pool_t *
svn_pool__create_unmanaged(svn_boolean_t thread_safe)
{
  apr_pool_t *pool;
  apr_pool_create_unmanaged_ex(&pool, abort_on_pool_failure,
                               svn_pool_create_allocator(thread_safe));
  return pool;
}
