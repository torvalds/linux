/*
 * root_pools.c :  Implement svn_root_pools__* API
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

#include "svn_pools.h"

#include "private/svn_subr_private.h"
#include "private/svn_mutex.h"

struct svn_root_pools__t
{
  /* unused pools.
   * Use MUTEX to serialize access to this collection.
   */
  apr_array_header_t *unused_pools;

  /* Mutex to serialize access to UNUSED_POOLS */
  svn_mutex__t *mutex;

};

svn_error_t *
svn_root_pools__create(svn_root_pools__t **pools)
{
  /* the collection of root pools must be managed independently from
     any other pool */
  apr_pool_t *pool
    = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));

  /* construct result object */
  svn_root_pools__t *result = apr_pcalloc(pool, sizeof(*result));
  SVN_ERR(svn_mutex__init(&result->mutex, TRUE, pool));
  result->unused_pools = apr_array_make(pool, 16, sizeof(apr_pool_t *));

  /* done */
  *pools = result;

  return SVN_NO_ERROR;
}

/* Return a currently unused connection pool in *POOL. If no such pool
 * exists, create a new root pool and return that in *POOL.
 */
static svn_error_t *
acquire_pool_internal(apr_pool_t **pool,
                      svn_root_pools__t *pools)
{
  SVN_ERR(svn_mutex__lock(pools->mutex));
  *pool = pools->unused_pools->nelts
        ? *(apr_pool_t **)apr_array_pop(pools->unused_pools)
        : apr_allocator_owner_get(svn_pool_create_allocator(FALSE));
  SVN_ERR(svn_mutex__unlock(pools->mutex, SVN_NO_ERROR));

  return SVN_NO_ERROR;
}

apr_pool_t *
svn_root_pools__acquire_pool(svn_root_pools__t *pools)
{
  apr_pool_t *pool;
  svn_error_t *err = acquire_pool_internal(&pool, pools);
  if (err)
    {
      /* Mutex failure?!  Well, try to continue with unrecycled data. */
      svn_error_clear(err);
      pool = apr_allocator_owner_get(svn_pool_create_allocator(FALSE));
    }

  return pool;
}

void
svn_root_pools__release_pool(apr_pool_t *pool,
                             svn_root_pools__t *pools)
{
  svn_error_t *err;

  svn_pool_clear(pool);

  err = svn_mutex__lock(pools->mutex);
  if (err)
    {
      svn_error_clear(err);
      svn_pool_destroy(pool);
    }
  else
    {
      APR_ARRAY_PUSH(pools->unused_pools, apr_pool_t *) = pool;
      svn_error_clear(svn_mutex__unlock(pools->mutex, SVN_NO_ERROR));
    }
}
