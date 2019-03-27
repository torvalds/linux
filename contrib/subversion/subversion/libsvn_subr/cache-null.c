/*
 * cache-null.c: dummy caching object for Subversion
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

#include "svn_private_config.h"
#include "cache.h"

/* There is no dedicated cache data structure.  Instead, we store the
 * cache ID directly in svn_cache__t.cache_internal .
 */

static svn_error_t *
null_cache_get(void **value_p,
               svn_boolean_t *found,
               void *cache_void,
               const void *key,
               apr_pool_t *result_pool)
{
  /* We know there is nothing to be found in this cache. */
  *value_p = NULL;
  *found = FALSE;

  return SVN_NO_ERROR;
}

static svn_error_t *
null_cache_has_key(svn_boolean_t *found,
                   void *cache_void,
                   const void *key,
                   apr_pool_t *scratch_pool)
{
  /* We know there is nothing to be found in this cache. */
  *found = FALSE;
  return SVN_NO_ERROR;
}

static svn_error_t *
null_cache_set(void *cache_void,
               const void *key,
               void *value,
               apr_pool_t *scratch_pool)
{
  /* We won't cache anything. */
  return SVN_NO_ERROR;
}

static svn_error_t *
null_cache_iter(svn_boolean_t *completed,
                void *cache_void,
                svn_iter_apr_hash_cb_t user_cb,
                void *user_baton,
                apr_pool_t *scratch_pool)
{
  /* Iteration over an empty set is a no-op. */
  if (completed)
    *completed = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
null_cache_get_partial(void **value_p,
                       svn_boolean_t *found,
                       void *cache_void,
                       const void *key,
                       svn_cache__partial_getter_func_t func,
                       void *baton,
                       apr_pool_t *result_pool)
{
  /* We know there is nothing to be found in this cache. */
  *found = FALSE;
  return SVN_NO_ERROR;
}

static svn_error_t *
null_cache_set_partial(void *cache_void,
                       const void *key,
                       svn_cache__partial_setter_func_t func,
                       void *baton,
                       apr_pool_t *scratch_pool)
{
  /* We know there is nothing to update in this cache. */
  return SVN_NO_ERROR;
}

static svn_boolean_t
null_cache_is_cachable(void *cache_void,
                       apr_size_t size)
{
  /* We won't cache anything */
  return FALSE;
}

static svn_error_t *
null_cache_get_info(void *cache_void,
                    svn_cache__info_t *info,
                    svn_boolean_t reset,
                    apr_pool_t *result_pool)
{
  const char *id = cache_void;

  info->id = apr_pstrdup(result_pool, id);

  info->used_entries = 0;
  info->total_entries = 0;

  info->used_size = 0;
  info->data_size = 0;
  info->total_size = 0;

  return SVN_NO_ERROR;
}

static svn_cache__vtable_t null_cache_vtable = {
  null_cache_get,
  null_cache_has_key,
  null_cache_set,
  null_cache_iter,
  null_cache_is_cachable,
  null_cache_get_partial,
  null_cache_set_partial,
  null_cache_get_info
};

svn_error_t *
svn_cache__create_null(svn_cache__t **cache_p,
                       const char *id,
                       apr_pool_t *result_pool)
{
  svn_cache__t *cache = apr_pcalloc(result_pool, sizeof(*cache));
  cache->vtable = &null_cache_vtable;
  cache->cache_internal = apr_pstrdup(result_pool, id);
  cache->pretend_empty = FALSE; /* There is no point in pretending --
                                   this cache _is_ empty. */

  *cache_p = cache;
  return SVN_NO_ERROR;
}
