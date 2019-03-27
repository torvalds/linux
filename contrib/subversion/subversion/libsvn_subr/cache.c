/*
 * cache.c: cache interface for Subversion
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

#include "cache.h"

svn_error_t *
svn_cache__set_error_handler(svn_cache__t *cache,
                             svn_cache__error_handler_t handler,
                             void *baton,
                             apr_pool_t *scratch_pool)
{
  cache->error_handler = handler;
  cache->error_baton = baton;
  return SVN_NO_ERROR;
}

svn_boolean_t
svn_cache__is_cachable(svn_cache__t *cache,
                       apr_size_t size)
{
  /* having no cache means we can't cache anything */
  if (cache == NULL)
    return FALSE;

  return cache->vtable->is_cachable(cache->cache_internal, size);
}

/* Give the error handler callback a chance to replace or ignore the
   error. */
static svn_error_t *
handle_error(svn_cache__t *cache,
             svn_error_t *err,
             apr_pool_t *pool)
{
  if (err)
    {
      cache->failures++;
      if (cache->error_handler)
        err = (cache->error_handler)(err, cache->error_baton, pool);
    }

  return err;
}


svn_error_t *
svn_cache__get(void **value_p,
               svn_boolean_t *found,
               svn_cache__t *cache,
               const void *key,
               apr_pool_t *result_pool)
{
  svn_error_t *err;

  /* In case any errors happen and are quelched, make sure we start
     out with FOUND set to false. */
  *found = FALSE;
#ifdef SVN_DEBUG
  if (cache->pretend_empty)
    return SVN_NO_ERROR;
#endif

  cache->reads++;
  err = handle_error(cache,
                     (cache->vtable->get)(value_p,
                                          found,
                                          cache->cache_internal,
                                          key,
                                          result_pool),
                     result_pool);

  if (*found)
    cache->hits++;

  return err;
}

svn_error_t *
svn_cache__has_key(svn_boolean_t *found,
                   svn_cache__t *cache,
                   const void *key,
                   apr_pool_t *scratch_pool)
{
  *found = FALSE;
#ifdef SVN_DEBUG
  if (cache->pretend_empty)
    return SVN_NO_ERROR;
#endif

  return handle_error(cache,
                      (cache->vtable->has_key)(found,
                                               cache->cache_internal,
                                               key,
                                               scratch_pool),
                      scratch_pool);
}

svn_error_t *
svn_cache__set(svn_cache__t *cache,
               const void *key,
               void *value,
               apr_pool_t *scratch_pool)
{
  cache->writes++;
  return handle_error(cache,
                      (cache->vtable->set)(cache->cache_internal,
                                           key,
                                           value,
                                           scratch_pool),
                      scratch_pool);
}


svn_error_t *
svn_cache__iter(svn_boolean_t *completed,
                svn_cache__t *cache,
                svn_iter_apr_hash_cb_t user_cb,
                void *user_baton,
                apr_pool_t *scratch_pool)
{
#ifdef SVN_DEBUG
  if (cache->pretend_empty)
    /* Pretend CACHE is empty. */
    return SVN_NO_ERROR;
#endif

  return (cache->vtable->iter)(completed,
                               cache->cache_internal,
                               user_cb,
                               user_baton,
                               scratch_pool);
}

svn_error_t *
svn_cache__get_partial(void **value,
                       svn_boolean_t *found,
                       svn_cache__t *cache,
                       const void *key,
                       svn_cache__partial_getter_func_t func,
                       void *baton,
                       apr_pool_t *result_pool)
{
  svn_error_t *err;

  /* In case any errors happen and are quelched, make sure we start
  out with FOUND set to false. */
  *found = FALSE;
#ifdef SVN_DEBUG
  if (cache->pretend_empty)
    return SVN_NO_ERROR;
#endif

  cache->reads++;
  err = handle_error(cache,
                     (cache->vtable->get_partial)(value,
                                                  found,
                                                  cache->cache_internal,
                                                  key,
                                                  func,
                                                  baton,
                                                  result_pool),
                     result_pool);

  if (*found)
    cache->hits++;

  return err;
}

svn_error_t *
svn_cache__set_partial(svn_cache__t *cache,
                       const void *key,
                       svn_cache__partial_setter_func_t func,
                       void *baton,
                       apr_pool_t *scratch_pool)
{
  cache->writes++;
  return handle_error(cache,
                      (cache->vtable->set_partial)(cache->cache_internal,
                                                   key,
                                                   func,
                                                   baton,
                                                   scratch_pool),
                      scratch_pool);
}

svn_error_t *
svn_cache__get_info(svn_cache__t *cache,
                    svn_cache__info_t *info,
                    svn_boolean_t reset,
                    apr_pool_t *result_pool)
{
  /* write general statistics */

  memset(info, 0, sizeof(*info));
  info->gets = cache->reads;
  info->hits = cache->hits;
  info->sets = cache->writes;
  info->failures = cache->failures;

  /* Call the cache implementation for filling the blanks.
   * It might also replace some of the general stats but
   * this is currently not done.
   */
  SVN_ERR((cache->vtable->get_info)(cache->cache_internal,
                                    info,
                                    reset,
                                    result_pool));

  /* reset statistics */

  if (reset)
    {
      cache->reads = 0;
      cache->hits = 0;
      cache->writes = 0;
      cache->failures = 0;
    }

  return SVN_NO_ERROR;
}

svn_string_t *
svn_cache__format_info(const svn_cache__info_t *info,
                       svn_boolean_t access_only,
                       apr_pool_t *result_pool)
{
  enum { _1MB = 1024 * 1024 };

  apr_uint64_t misses = info->gets - info->hits;
  double hit_rate = (100.0 * (double)info->hits)
                  / (double)(info->gets ? info->gets : 1);
  double write_rate = (100.0 * (double)info->sets)
                    / (double)(misses ? misses : 1);
  double data_usage_rate = (100.0 * (double)info->used_size)
                         / (double)(info->data_size ? info->data_size : 1);
  double data_entry_rate = (100.0 * (double)info->used_entries)
                 / (double)(info->total_entries ? info->total_entries : 1);

  const char *histogram = "";
  if (!access_only)
    {
      svn_stringbuf_t *text = svn_stringbuf_create_empty(result_pool);

      int i;
      int count = sizeof(info->histogram) / sizeof(info->histogram[0]);
      for (i = count - 1; i >= 0; --i)
        if (info->histogram[i] > 0 || text->len > 0)
          text = svn_stringbuf_createf(result_pool,
                                       i == count - 1
                                         ? "%s%12" APR_UINT64_T_FMT
                                           " buckets with >%d entries\n"
                                         : "%s%12" APR_UINT64_T_FMT
                                           " buckets with %d entries\n",
                                       text->data, info->histogram[i], i);

      histogram = text->data;
    }

  return access_only
       ? svn_string_createf(result_pool,
                            "%s\n"
                            "gets    : %" APR_UINT64_T_FMT
                            ", %" APR_UINT64_T_FMT " hits (%5.2f%%)\n"
                            "sets    : %" APR_UINT64_T_FMT
                            " (%5.2f%% of misses)\n",
                            info->id,
                            info->gets,
                            info->hits, hit_rate,
                            info->sets, write_rate)
       : svn_string_createf(result_pool,

                            "%s\n"
                            "gets    : %" APR_UINT64_T_FMT
                            ", %" APR_UINT64_T_FMT " hits (%5.2f%%)\n"
                            "sets    : %" APR_UINT64_T_FMT
                            " (%5.2f%% of misses)\n"
                            "failures: %" APR_UINT64_T_FMT "\n"
                            "used    : %" APR_UINT64_T_FMT " MB (%5.2f%%)"
                            " of %" APR_UINT64_T_FMT " MB data cache"
                            " / %" APR_UINT64_T_FMT " MB total cache memory\n"
                            "          %" APR_UINT64_T_FMT " entries (%5.2f%%)"
                            " of %" APR_UINT64_T_FMT " total\n%s",

                            info->id,

                            info->gets,
                            info->hits, hit_rate,
                            info->sets, write_rate,
                            info->failures,

                            info->used_size / _1MB, data_usage_rate,
                            info->data_size / _1MB,
                            info->total_size / _1MB,

                            info->used_entries, data_entry_rate,
                            info->total_entries,
                            histogram);
}
