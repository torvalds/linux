/*
 * revert.c:  wrapper around wc revert functionality.
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

/* ==================================================================== */



/*** Includes. ***/

#include "svn_path.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_time.h"
#include "svn_config.h"
#include "client.h"
#include "private/svn_wc_private.h"

#include "svn_private_config.h"


/*** Code. ***/

struct revert_with_write_lock_baton {
  const char *local_abspath;
  svn_depth_t depth;
  svn_boolean_t use_commit_times;
  const apr_array_header_t *changelists;
  svn_boolean_t clear_changelists;
  svn_boolean_t metadata_only;
  svn_client_ctx_t *ctx;
};

/* (Note: All arguments are in the baton above.)

   Attempt to revert LOCAL_ABSPATH.

   If DEPTH is svn_depth_empty, revert just the properties on the
   directory; else if svn_depth_files, revert the properties and any
   files immediately under the directory; else if
   svn_depth_immediates, revert all of the preceding plus properties
   on immediate subdirectories; else if svn_depth_infinity, revert
   path and everything under it fully recursively.

   CHANGELISTS is an array of const char * changelist names, used as a
   restrictive filter on items reverted; that is, don't revert any
   item unless it's a member of one of those changelists.  If
   CHANGELISTS is empty (or altogether NULL), no changelist filtering occurs.

   Consult CTX to determine whether or not to revert timestamp to the
   time of last commit ('use-commit-times = yes').

   If PATH is unversioned, return SVN_ERR_UNVERSIONED_RESOURCE. */
static svn_error_t *
revert(void *baton, apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  struct revert_with_write_lock_baton *b = baton;
  svn_error_t *err;

  err = svn_wc_revert5(b->ctx->wc_ctx,
                       b->local_abspath,
                       b->depth,
                       b->use_commit_times,
                       b->changelists,
                       b->clear_changelists,
                       b->metadata_only,
                       b->ctx->cancel_func, b->ctx->cancel_baton,
                       b->ctx->notify_func2, b->ctx->notify_baton2,
                       scratch_pool);

  if (err)
    {
      /* If target isn't versioned, just send a 'skip'
         notification and move on. */
      if (err->apr_err == SVN_ERR_ENTRY_NOT_FOUND
          || err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE
          || err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          if (b->ctx->notify_func2)
            {
              svn_wc_notify_t *notify;

              notify = svn_wc_create_notify(b->local_abspath,
                                            svn_wc_notify_skip,
                                            scratch_pool);

              notify->err = err;

              b->ctx->notify_func2(b->ctx->notify_baton2,
                                   notify, scratch_pool);
            }
          svn_error_clear(err);
        }
      else
        return svn_error_trace(err);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client_revert3(const apr_array_header_t *paths,
                   svn_depth_t depth,
                   const apr_array_header_t *changelists,
                   svn_boolean_t clear_changelists,
                   svn_boolean_t metadata_only,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  apr_pool_t *iterpool;
  svn_error_t *err = SVN_NO_ERROR;
  int i;
  svn_config_t *cfg;
  svn_boolean_t use_commit_times;
  struct revert_with_write_lock_baton baton;

  /* Don't even attempt to modify the working copy if any of the
   * targets look like URLs. URLs are invalid input. */
  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      if (svn_path_is_url(path))
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("'%s' is not a local path"), path);
    }

  cfg = ctx->config
        ? svn_hash_gets(ctx->config, SVN_CONFIG_CATEGORY_CONFIG)
        : NULL;

  SVN_ERR(svn_config_get_bool(cfg, &use_commit_times,
                              SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_USE_COMMIT_TIMES,
                              FALSE));

  iterpool = svn_pool_create(pool);

  for (i = 0; i < paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);
      const char *local_abspath, *lock_target;
      svn_boolean_t wc_root;

      svn_pool_clear(iterpool);

      /* See if we've been asked to cancel this operation. */
      if ((ctx->cancel_func)
          && ((err = ctx->cancel_func(ctx->cancel_baton))))
        goto errorful;

      err = svn_dirent_get_absolute(&local_abspath, path, iterpool);
      if (err)
        goto errorful;

      baton.local_abspath = local_abspath;
      baton.depth = depth;
      baton.use_commit_times = use_commit_times;
      baton.changelists = changelists;
      baton.clear_changelists = clear_changelists;
      baton.metadata_only = metadata_only;
      baton.ctx = ctx;

      err = svn_wc__is_wcroot(&wc_root, ctx->wc_ctx, local_abspath, iterpool);
      if (err)
        goto errorful;
      lock_target = wc_root ? local_abspath
                            : svn_dirent_dirname(local_abspath, pool);
      err = svn_wc__call_with_write_lock(revert, &baton, ctx->wc_ctx,
                                         lock_target, FALSE,
                                         iterpool, iterpool);
      if (err)
        goto errorful;
    }

 errorful:

  {
    /* Sleep to ensure timestamp integrity. */
    const char *sleep_path = NULL;

    /* Only specify a path if we are certain all paths are on the
       same filesystem */
    if (paths->nelts == 1)
      sleep_path = APR_ARRAY_IDX(paths, 0, const char *);

    svn_io_sleep_for_timestamps(sleep_path, iterpool);
  }

  svn_pool_destroy(iterpool);

  return svn_error_trace(err);
}
