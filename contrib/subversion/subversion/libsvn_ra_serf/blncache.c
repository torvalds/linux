/*
 * blncache.c: DAV baseline information cache.
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

#include <apr_pools.h>

#include "svn_hash.h"
#include "svn_dirent_uri.h"
#include "svn_types.h"
#include "svn_pools.h"

#include "blncache.h"

/* Baseline information cache object. */
typedef struct baseline_info_t
{
  const char *bc_url;    /* baseline collection URL. */
  svn_revnum_t revision; /* revision associated with the baseline. */

} baseline_info_t;

/* Module-private structure used to hold the caches. */
struct svn_ra_serf__blncache_t
{
  /* A hash mapping 'svn_revnum_t *' baseline revisions to 'const
   * char *' baseline collection URLs.
   */
  apr_hash_t *revnum_to_bc;

  /* A hash mapping 'const char *' baseline URLs to 'baseline_info_t *'
   * structures. (Allocated from the same pool as 'revnum_to_bc'.)
   */
  apr_hash_t *baseline_info;
};



/* Return a pointer to an 'baseline_info_t' structure allocated from
 * POOL and populated with BC_URL (which is duped into POOL) and
 * REVISION.
 */
static baseline_info_t *
baseline_info_make(const char *bc_url,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  baseline_info_t *result = apr_palloc(pool, sizeof(*result));

  result->bc_url = apr_pstrdup(pool, bc_url);
  result->revision = revision;

  return result;
}

/* Set in HASH the value VAL for the KEY (whose key length is KLEN).
 * KEY will be duped into HASH's pool.
 */
static void
hash_set_copy(apr_hash_t *hash,
              const void *key,
              apr_ssize_t klen,
              const void *val)
{
  if (klen == APR_HASH_KEY_STRING)
    klen = strlen(key);
  apr_hash_set(hash, apr_pmemdup(apr_hash_pool_get(hash), key, klen),
               klen, val);
}


svn_error_t *
svn_ra_serf__blncache_create(svn_ra_serf__blncache_t **blncache_p,
                             apr_pool_t *pool)
{
  svn_ra_serf__blncache_t *blncache = apr_pcalloc(pool, sizeof(*blncache));
  apr_pool_t *cache_pool;

  /* Create subpool for cached data. It will be cleared if we reach maximum
   * cache size.*/
  cache_pool = svn_pool_create(pool);
  blncache->revnum_to_bc = apr_hash_make(cache_pool);
  blncache->baseline_info = apr_hash_make(cache_pool);

  *blncache_p = blncache;

  return SVN_NO_ERROR;
}

#define MAX_CACHE_SIZE 1000

svn_error_t *
svn_ra_serf__blncache_set(svn_ra_serf__blncache_t *blncache,
                          const char *baseline_url,
                          svn_revnum_t revision,
                          const char *bc_url,
                          apr_pool_t *scratch_pool)
{
  if (bc_url && SVN_IS_VALID_REVNUM(revision))
    {
      apr_pool_t *cache_pool = apr_hash_pool_get(blncache->revnum_to_bc);

      /* If the caches are too big, delete and recreate 'em and move along. */
      if (MAX_CACHE_SIZE < (apr_hash_count(blncache->baseline_info)
                            + apr_hash_count(blncache->revnum_to_bc)))
        {
          svn_pool_clear(cache_pool);
          blncache->revnum_to_bc = apr_hash_make(cache_pool);
          blncache->baseline_info = apr_hash_make(cache_pool);
        }

      hash_set_copy(blncache->revnum_to_bc, &revision, sizeof(revision),
                    apr_pstrdup(cache_pool, bc_url));

      if (baseline_url)
        {
          hash_set_copy(blncache->baseline_info, baseline_url,
                        APR_HASH_KEY_STRING,
                        baseline_info_make(bc_url, revision, cache_pool));
        }
    }

  return SVN_NO_ERROR;
}

#undef MAX_CACHE_SIZE

svn_error_t *
svn_ra_serf__blncache_get_bc_url(const char **bc_url_p,
                                 svn_ra_serf__blncache_t *blncache,
                                 svn_revnum_t revnum,
                                 apr_pool_t *result_pool)
{
  const char *value = apr_hash_get(blncache->revnum_to_bc,
                                   &revnum, sizeof(revnum));
  *bc_url_p = value ? apr_pstrdup(result_pool, value) : NULL;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_serf__blncache_get_baseline_info(const char **bc_url_p,
                                        svn_revnum_t *revision_p,
                                        svn_ra_serf__blncache_t *blncache,
                                        const char *baseline_url,
                                        apr_pool_t *pool)
{
  baseline_info_t *info = svn_hash_gets(blncache->baseline_info, baseline_url);
  if (info)
    {
      *bc_url_p = apr_pstrdup(pool, info->bc_url);
      *revision_p = info->revision;
    }
  else
    {
      *bc_url_p = NULL;
      *revision_p = SVN_INVALID_REVNUM;
    }

  return SVN_NO_ERROR;
}

