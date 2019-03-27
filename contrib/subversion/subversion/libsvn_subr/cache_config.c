/* svn_cache_config.c : configuration of internal caches
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

#include <apr_atomic.h>

#include "svn_cache_config.h"
#include "private/svn_atomic.h"
#include "private/svn_cache.h"

#include "svn_pools.h"
#include "svn_sorts.h"

/* The cache settings as a process-wide singleton.
 */
static svn_cache_config_t cache_settings =
  {
    /* default configuration:
     *
     * Please note that the resources listed below will be allocated
     * PER PROCESS. Thus, the defaults chosen here are kept deliberately
     * low to still make a difference yet to ensure that pre-fork servers
     * on machines with small amounts of RAM aren't severely impacted.
     */
    0x1000000,   /* 16 MB for caches.
                  * If you are running a single server process,
                  * you may easily increase that to 50+% of your RAM
                  * using svn_fs_set_cache_config().
                  */
    16,          /* up to 16 files kept open.
                  * Most OS restrict the number of open file handles to
                  * about 1000. To minimize I/O and OS overhead, values
                  * of 500+ can be beneficial (use svn_fs_set_cache_config()
                  * to change the configuration).
                  * When running with a huge in-process cache, this number
                  * has little impact on performance and a more modest
                  * value (< 100) may be more suitable.
                  */
#if APR_HAS_THREADS
    FALSE        /* assume multi-threaded operation.
                  * Because this simply activates proper synchronization
                  * between threads, it is a safe default.
                  */
#else
    TRUE         /* single-threaded is the only supported mode of operation */
#endif
};

/* Get the current FSFS cache configuration. */
const svn_cache_config_t *
svn_cache_config_get(void)
{
  return &cache_settings;
}

/* Initializer function as required by svn_atomic__init_once.  Allocate
 * the process-global (singleton) membuffer cache and return it in the
 * svn_membuffer_t * in *BATON.  UNUSED_POOL is unused and should be NULL.
 */
static svn_error_t *
initialize_cache(void *baton, apr_pool_t *unused_pool)
{
  svn_membuffer_t **cache_p = baton;
  svn_membuffer_t *cache = NULL;

  /* Limit the cache size to about half the available address space
   * (typ. 1G under 32 bits).
   */
  apr_uint64_t cache_size = MIN(cache_settings.cache_size,
                                (apr_uint64_t)SVN_MAX_OBJECT_SIZE / 2);

  /* Create caches at all? */
  if (cache_size)
    {
      svn_error_t *err;

      /* auto-allocate cache */
      apr_allocator_t *allocator = NULL;
      apr_pool_t *pool = NULL;

      if (apr_allocator_create(&allocator))
        return SVN_NO_ERROR;

      /* Ensure that we free partially allocated data if we run OOM
       * before the cache is complete: If the cache cannot be allocated
       * in its full size, the create() function will clear the pool
       * explicitly. The allocator will make sure that any memory no
       * longer used by the pool will actually be returned to the OS.
       *
       * Please note that this pool and allocator is used *only* to
       * allocate the large membuffer. All later dynamic allocations
       * come from other, temporary pools and allocators.
       */
      apr_allocator_max_free_set(allocator, 1);

      /* don't terminate upon OOM but make pool return a NULL pointer
       * instead so we can disable caching gracefully and continue
       * operation without membuffer caches.
       */
      apr_pool_create_ex(&pool, NULL, NULL, allocator);
      if (pool == NULL)
        return SVN_NO_ERROR;
      apr_allocator_owner_set(allocator, pool);

      err = svn_cache__membuffer_cache_create(
          &cache,
          (apr_size_t)cache_size,
          (apr_size_t)(cache_size / 5),
          0,
          ! svn_cache_config_get()->single_threaded,
          FALSE,
          pool);

      /* Some error occurred. Most likely it's an OOM error but we don't
       * really care. Simply release all cache memory and disable caching
       */
      if (err)
        {
          /* Memory cleanup */
          svn_pool_destroy(pool);

          /* Document that we actually don't have a cache. */
          cache_settings.cache_size = 0;

          return svn_error_trace(err);
        }

      /* done */
      *cache_p = cache;
    }

  return SVN_NO_ERROR;
}

/* Access the process-global (singleton) membuffer cache. The first call
 * will automatically allocate the cache using the current cache config.
 * NULL will be returned if the desired cache size is 0 or if the cache
 * could not be created for some reason.
 */
svn_membuffer_t *
svn_cache__get_global_membuffer_cache(void)
{
  static svn_membuffer_t *cache = NULL;
  static svn_atomic_t initialized = 0;

  svn_error_t *err
    = svn_atomic__init_once(&initialized, initialize_cache, &cache, NULL);
  if (err)
    {
      /* no caches today ... */
      svn_error_clear(err);
      return NULL;
    }

  return cache;
}

void
svn_cache_config_set(const svn_cache_config_t *settings)
{
  cache_settings = *settings;
}

