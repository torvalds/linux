/*
 * cleanup.c:  wrapper around wc cleanup functionality.
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

#include "svn_time.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_config.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "client.h"
#include "svn_props.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

struct cleanup_status_walk_baton
{
  svn_boolean_t break_locks;
  svn_boolean_t fix_timestamps;
  svn_boolean_t clear_dav_cache;
  svn_boolean_t vacuum_pristines;
  svn_boolean_t remove_unversioned_items;
  svn_boolean_t remove_ignored_items;
  svn_boolean_t include_externals;
  svn_client_ctx_t *ctx;
};

/* Forward declararion. */
static svn_error_t *
cleanup_status_walk(void *baton,
                    const char *local_abspath,
                    const svn_wc_status3_t *status,
                    apr_pool_t *scratch_pool);

static svn_error_t *
do_cleanup(const char *local_abspath,
           svn_boolean_t break_locks,
           svn_boolean_t fix_timestamps,
           svn_boolean_t clear_dav_cache,
           svn_boolean_t vacuum_pristines,
           svn_boolean_t remove_unversioned_items,
           svn_boolean_t remove_ignored_items,
           svn_boolean_t include_externals,
           svn_client_ctx_t *ctx,
           apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc_cleanup4(ctx->wc_ctx,
                          local_abspath,
                          break_locks,
                          fix_timestamps,
                          clear_dav_cache,
                          vacuum_pristines,
                          ctx->cancel_func, ctx->cancel_baton,
                          ctx->notify_func2, ctx->notify_baton2,
                          scratch_pool));

  if (fix_timestamps)
    svn_io_sleep_for_timestamps(local_abspath, scratch_pool);

  if (remove_unversioned_items || remove_ignored_items || include_externals)
    {
      struct cleanup_status_walk_baton b;
      apr_array_header_t *ignores;

      b.break_locks = break_locks;
      b.fix_timestamps = fix_timestamps;
      b.clear_dav_cache = clear_dav_cache;
      b.vacuum_pristines = vacuum_pristines;
      b.remove_unversioned_items = remove_unversioned_items;
      b.remove_ignored_items = remove_ignored_items;
      b.include_externals = include_externals;
      b.ctx = ctx;

      SVN_ERR(svn_wc_get_default_ignores(&ignores, ctx->config, scratch_pool));

      SVN_WC__CALL_WITH_WRITE_LOCK(
              svn_wc_walk_status(ctx->wc_ctx, local_abspath,
                                 svn_depth_infinity,
                                 TRUE,  /* get all */
                                 remove_ignored_items,
                                 TRUE,  /* ignore textmods */
                                 ignores,
                                 cleanup_status_walk, &b,
                                 ctx->cancel_func,
                                 ctx->cancel_baton,
                                 scratch_pool),
              ctx->wc_ctx,
              local_abspath,
              FALSE /* lock_anchor */,
              scratch_pool);
    }

  return SVN_NO_ERROR;
}


/* An implementation of svn_wc_status_func4_t. */
static svn_error_t *
cleanup_status_walk(void *baton,
                    const char *local_abspath,
                    const svn_wc_status3_t *status,
                    apr_pool_t *scratch_pool)
{
  struct cleanup_status_walk_baton *b = baton;
  svn_node_kind_t kind_on_disk;
  svn_wc_notify_t *notify;

  if (status->node_status == svn_wc_status_external && b->include_externals)
    {
      svn_error_t *err;

      SVN_ERR(svn_io_check_path(local_abspath, &kind_on_disk, scratch_pool));
      if (kind_on_disk == svn_node_dir)
        {
          if (b->ctx->notify_func2)
            {
              notify = svn_wc_create_notify(local_abspath,
                                            svn_wc_notify_cleanup_external,
                                            scratch_pool);
              b->ctx->notify_func2(b->ctx->notify_baton2, notify,
                                   scratch_pool);
            }

          err = do_cleanup(local_abspath,
                           b->break_locks,
                           b->fix_timestamps,
                           b->clear_dav_cache,
                           b->vacuum_pristines,
                           b->remove_unversioned_items,
                           b->remove_ignored_items,
                           TRUE /* include_externals */,
                           b->ctx, scratch_pool);
          if (err && err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY)
            {
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
          else
            SVN_ERR(err);
        }

      return SVN_NO_ERROR;
    }

  if (status->node_status == svn_wc_status_ignored)
    {
      if (!b->remove_ignored_items)
        return SVN_NO_ERROR;
    }
  else if (status->node_status == svn_wc_status_unversioned)
    {
      if (!b->remove_unversioned_items)
        return SVN_NO_ERROR;
    }
  else
    return SVN_NO_ERROR;

  SVN_ERR(svn_io_check_path(local_abspath, &kind_on_disk, scratch_pool));
  switch (kind_on_disk)
    {
      case svn_node_file:
      case svn_node_symlink:
        SVN_ERR(svn_io_remove_file2(local_abspath, FALSE, scratch_pool));
        break;
      case svn_node_dir:
        SVN_ERR(svn_io_remove_dir2(local_abspath, FALSE,
                                   b->ctx->cancel_func, b->ctx->cancel_baton,
                                   scratch_pool));
        break;
      case svn_node_none:
      default:
        return SVN_NO_ERROR;
    }

  if (b->ctx->notify_func2)
    {
      notify = svn_wc_create_notify(local_abspath, svn_wc_notify_delete,
                                    scratch_pool);
      notify->kind = kind_on_disk;
      b->ctx->notify_func2(b->ctx->notify_baton2, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_cleanup2(const char *dir_abspath,
                    svn_boolean_t break_locks,
                    svn_boolean_t fix_recorded_timestamps,
                    svn_boolean_t clear_dav_cache,
                    svn_boolean_t vacuum_pristines,
                    svn_boolean_t include_externals,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dir_abspath));

  SVN_ERR(do_cleanup(dir_abspath,
                     break_locks,
                     fix_recorded_timestamps,
                     clear_dav_cache,
                     vacuum_pristines,
                     FALSE /* remove_unversioned_items */,
                     FALSE /* remove_ignored_items */,
                     include_externals,
                     ctx, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_vacuum(const char *dir_abspath,
                  svn_boolean_t remove_unversioned_items,
                  svn_boolean_t remove_ignored_items,
                  svn_boolean_t fix_recorded_timestamps,
                  svn_boolean_t vacuum_pristines,
                  svn_boolean_t include_externals,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dir_abspath));

  SVN_ERR(do_cleanup(dir_abspath,
                     FALSE /* break_locks */,
                     fix_recorded_timestamps,
                     FALSE /* clear_dav_cache */,
                     vacuum_pristines,
                     remove_unversioned_items,
                     remove_ignored_items,
                     include_externals,
                     ctx, scratch_pool));

  return SVN_NO_ERROR;
}
