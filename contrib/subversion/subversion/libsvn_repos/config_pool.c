/*
 * config_pool.c :  pool of configuration objects
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




#include "svn_checksum.h"
#include "svn_path.h"
#include "svn_pools.h"

#include "private/svn_subr_private.h"
#include "private/svn_repos_private.h"

#include "svn_private_config.h"

#include "config_file.h"


/* Return a memory buffer structure allocated in POOL and containing the
 * data from CHECKSUM.
 */
static svn_membuf_t *
checksum_as_key(svn_checksum_t *checksum,
                apr_pool_t *pool)
{
  svn_membuf_t *result = apr_pcalloc(pool, sizeof(*result));
  apr_size_t size = svn_checksum_size(checksum);

  svn_membuf__create(result, size, pool);
  result->size = size; /* exact length is required! */
  memcpy(result->data, checksum->digest, size);

  return result;
}

/* Set *CFG to the configuration serialized in STREAM and cache it in
 * CONFIG_POOL under CHECKSUM.  The configuration will only be parsed if
 * we can't find it the CONFIG_POOL already.
 *
 * RESULT_POOL determines the lifetime of the returned reference and
 * SCRATCH_POOL is being used for temporary allocations.
 */
static svn_error_t *
find_config(svn_config_t **cfg,
            svn_repos__config_pool_t *config_pool,
            svn_stream_t *stream,
            svn_checksum_t *checksum,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  /* First, attempt the cache lookup. */
  svn_membuf_t *key = checksum_as_key(checksum, scratch_pool);
  SVN_ERR(svn_object_pool__lookup((void **)cfg, config_pool, key,
                                  result_pool));

  /* Not found? => parse and cache */
  if (!*cfg)
    {
      svn_config_t *config;

      /* create a pool for the new config object and parse the data into it */
      apr_pool_t *cfg_pool = svn_object_pool__new_item_pool(config_pool);
      SVN_ERR(svn_config_parse(&config, stream, FALSE, FALSE, cfg_pool));

      /* switch config data to r/o mode to guarantee thread-safe access */
      svn_config__set_read_only(config, cfg_pool);

      /* add config in pool, handle loads races and return the right config */
      SVN_ERR(svn_object_pool__insert((void **)cfg, config_pool, key,
                                      config, cfg_pool, result_pool));
    }

  return SVN_NO_ERROR;
}

/* API implementation */

svn_error_t *
svn_repos__config_pool_create(svn_repos__config_pool_t **config_pool,
                              svn_boolean_t thread_safe,
                              apr_pool_t *pool)
{
  return svn_error_trace(svn_object_pool__create(config_pool,
                                                 thread_safe, pool));
}

svn_error_t *
svn_repos__config_pool_get(svn_config_t **cfg,
                           svn_repos__config_pool_t *config_pool,
                           const char *path,
                           svn_boolean_t must_exist,
                           svn_repos_t *preferred_repos,
                           apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  config_access_t *access = svn_repos__create_config_access(preferred_repos,
                                                            scratch_pool);
  svn_stream_t *stream;
  svn_checksum_t *checksum;

  *cfg = NULL;
  err = svn_repos__get_config(&stream, &checksum, access, path, must_exist,
                              scratch_pool);
  if (!err)
    err = svn_error_quick_wrapf(find_config(cfg, config_pool, stream,
                                            checksum, pool, scratch_pool),
                                "Error while parsing config file: '%s':",
                                path);

  /* Let the standard implementation handle all the difficult cases.
   * Note that for in-repo configs, there are no further special cases to
   * check for and deal with. */
  if (!*cfg && !svn_path_is_url(path))
    {
      svn_error_clear(err);
      err = svn_config_read3(cfg, path, must_exist, FALSE, FALSE, pool);
    }

  svn_repos__destroy_config_access(access);
  svn_pool_destroy(scratch_pool);

  /* we need to duplicate the root structure as it contains temp. buffers */
  if (*cfg)
    *cfg = svn_config__shallow_copy(*cfg, pool);

  return svn_error_trace(err);
}
