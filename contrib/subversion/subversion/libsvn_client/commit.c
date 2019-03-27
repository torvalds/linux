/*
 * commit.c:  wrappers around wc commit functionality.
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

#include <string.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_ra.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_sorts.h"

#include "client.h"
#include "private/svn_wc_private.h"
#include "private/svn_ra_private.h"
#include "private/svn_sorts_private.h"

#include "svn_private_config.h"

struct capture_baton_t {
  svn_commit_callback2_t original_callback;
  void *original_baton;

  svn_commit_info_t **info;
  apr_pool_t *pool;
};


static svn_error_t *
capture_commit_info(const svn_commit_info_t *commit_info,
                    void *baton,
                    apr_pool_t *pool)
{
  struct capture_baton_t *cb = baton;

  *(cb->info) = svn_commit_info_dup(commit_info, cb->pool);

  if (cb->original_callback)
    SVN_ERR((cb->original_callback)(commit_info, cb->original_baton, pool));

  return SVN_NO_ERROR;
}


static svn_error_t *
get_ra_editor(const svn_delta_editor_t **editor,
              void **edit_baton,
              svn_ra_session_t *ra_session,
              svn_client_ctx_t *ctx,
              const char *log_msg,
              const apr_array_header_t *commit_items,
              const apr_hash_t *revprop_table,
              apr_hash_t *lock_tokens,
              svn_boolean_t keep_locks,
              svn_commit_callback2_t commit_callback,
              void *commit_baton,
              apr_pool_t *pool)
{
  apr_hash_t *commit_revprops;
  apr_hash_t *relpath_map = NULL;

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           log_msg, ctx, pool));

#ifdef ENABLE_EV2_SHIMS
  if (commit_items)
    {
      int i;
      apr_pool_t *iterpool = svn_pool_create(pool);

      relpath_map = apr_hash_make(pool);
      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item3_t *item = APR_ARRAY_IDX(commit_items, i,
                                                  svn_client_commit_item3_t *);
          const char *relpath;

          if (!item->path)
            continue;

          svn_pool_clear(iterpool);
          SVN_ERR(svn_wc__node_get_origin(NULL, NULL, &relpath, NULL, NULL,
                                          NULL, NULL,
                                          ctx->wc_ctx, item->path, FALSE, pool,
                                          iterpool));
          if (relpath)
            svn_hash_sets(relpath_map, relpath, item->path);
        }
      svn_pool_destroy(iterpool);
    }
#endif

  /* Fetch RA commit editor. */
  SVN_ERR(svn_ra__register_editor_shim_callbacks(ra_session,
                        svn_client__get_shim_callbacks(ctx->wc_ctx,
                                                       relpath_map, pool)));
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, editor, edit_baton,
                                    commit_revprops, commit_callback,
                                    commit_baton, lock_tokens, keep_locks,
                                    pool));

  return SVN_NO_ERROR;
}


/*** Public Interfaces. ***/

static svn_error_t *
reconcile_errors(svn_error_t *commit_err,
                 svn_error_t *unlock_err,
                 svn_error_t *bump_err,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  /* Early release (for good behavior). */
  if (! (commit_err || unlock_err || bump_err))
    return SVN_NO_ERROR;

  /* If there was a commit error, start off our error chain with
     that. */
  if (commit_err)
    {
      commit_err = svn_error_quick_wrap
        (commit_err, _("Commit failed (details follow):"));
      err = commit_err;
    }

  /* Else, create a new "general" error that will lead off the errors
     that follow. */
  else
    err = svn_error_create(SVN_ERR_BASE, NULL,
                           _("Commit succeeded, but other errors follow:"));

  /* If there was an unlock error... */
  if (unlock_err)
    {
      /* Wrap the error with some headers. */
      unlock_err = svn_error_quick_wrap
        (unlock_err, _("Error unlocking locked dirs (details follow):"));

      /* Append this error to the chain. */
      svn_error_compose(err, unlock_err);
    }

  /* If there was a bumping error... */
  if (bump_err)
    {
      /* Wrap the error with some headers. */
      bump_err = svn_error_quick_wrap
        (bump_err, _("Error bumping revisions post-commit (details follow):"));

      /* Append this error to the chain. */
      svn_error_compose(err, bump_err);
    }

  return err;
}

/* For all lock tokens in ALL_TOKENS for URLs under BASE_URL, add them
   to a new hashtable allocated in POOL.  *RESULT is set to point to this
   new hash table.  *RESULT will be keyed on const char * URI-decoded paths
   relative to BASE_URL.  The lock tokens will not be duplicated. */
static svn_error_t *
collect_lock_tokens(apr_hash_t **result,
                    apr_hash_t *all_tokens,
                    const char *base_url,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  *result = apr_hash_make(pool);

  for (hi = apr_hash_first(pool, all_tokens); hi; hi = apr_hash_next(hi))
    {
      const char *url = apr_hash_this_key(hi);
      const char *token = apr_hash_this_val(hi);
      const char *relpath = svn_uri_skip_ancestor(base_url, url, pool);

      if (relpath)
        {
          svn_hash_sets(*result, relpath, token);
        }
    }

  return SVN_NO_ERROR;
}

/* Put ITEM onto QUEUE, allocating it in QUEUE's pool...
 * If a checksum is provided, it can be the MD5 and/or the SHA1. */
static svn_error_t *
post_process_commit_item(svn_wc_committed_queue_t *queue,
                         const svn_client_commit_item3_t *item,
                         svn_wc_context_t *wc_ctx,
                         svn_boolean_t keep_changelists,
                         svn_boolean_t keep_locks,
                         svn_boolean_t commit_as_operations,
                         const svn_checksum_t *sha1_checksum,
                         apr_pool_t *scratch_pool)
{
  svn_boolean_t loop_recurse = FALSE;
  svn_boolean_t remove_lock;

  if (! commit_as_operations
      && (item->state_flags & SVN_CLIENT_COMMIT_ITEM_ADD)
      && (item->kind == svn_node_dir)
      && (item->copyfrom_url))
    loop_recurse = TRUE;

  remove_lock = (! keep_locks && (item->state_flags
                                       & (SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN
                                          | SVN_CLIENT_COMMIT_ITEM_ADD
                                          | SVN_CLIENT_COMMIT_ITEM_DELETE)));

  /* When the node was deleted (or replaced), we need to always remove the
     locks, as they're invalidated on the server. We cannot honor the
     SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN flag here because it does not tell
     us whether we have locked children. */
  if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
    remove_lock = TRUE;

  return svn_error_trace(
         svn_wc_queue_committed4(queue, wc_ctx, item->path,
                                 loop_recurse,
                                 0 != (item->state_flags &
                                       (SVN_CLIENT_COMMIT_ITEM_ADD
                                        | SVN_CLIENT_COMMIT_ITEM_DELETE
                                        | SVN_CLIENT_COMMIT_ITEM_TEXT_MODS
                                        | SVN_CLIENT_COMMIT_ITEM_PROP_MODS)),
                                 item->incoming_prop_changes,
                                 remove_lock, !keep_changelists,
                                 sha1_checksum, scratch_pool));
}

/* Given a list of committables described by their common base abspath
   BASE_ABSPATH and a list of relative dirents TARGET_RELPATHS determine
   which absolute paths must be locked to commit all these targets and
   return this as a const char * array in LOCK_TARGETS

   Allocate the result in RESULT_POOL and use SCRATCH_POOL for temporary
   storage */
static svn_error_t *
determine_lock_targets(apr_array_header_t **lock_targets,
                       svn_wc_context_t *wc_ctx,
                       const char *base_abspath,
                       const apr_array_header_t *target_relpaths,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *wc_items; /* const char *wcroot -> apr_array_header_t */
  apr_hash_index_t *hi;
  int i;

  wc_items = apr_hash_make(scratch_pool);

  /* Create an array of targets for each working copy used */
  for (i = 0; i < target_relpaths->nelts; i++)
    {
      const char *target_abspath;
      const char *wcroot_abspath;
      apr_array_header_t *wc_targets;
      svn_error_t *err;
      const char *target_relpath = APR_ARRAY_IDX(target_relpaths, i,
                                                 const char *);

      svn_pool_clear(iterpool);
      target_abspath = svn_dirent_join(base_abspath, target_relpath,
                                       scratch_pool);

      err = svn_wc__get_wcroot(&wcroot_abspath, wc_ctx, target_abspath,
                               iterpool, iterpool);

      if (err)
        {
          if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
            {
              svn_error_clear(err);
              continue;
            }
          return svn_error_trace(err);
        }

      wc_targets = svn_hash_gets(wc_items, wcroot_abspath);

      if (! wc_targets)
        {
          wc_targets = apr_array_make(scratch_pool, 4, sizeof(const char *));
          svn_hash_sets(wc_items, apr_pstrdup(scratch_pool, wcroot_abspath),
                        wc_targets);
        }

      APR_ARRAY_PUSH(wc_targets, const char *) = target_abspath;
    }

  *lock_targets = apr_array_make(result_pool, apr_hash_count(wc_items),
                                 sizeof(const char *));

  /* For each working copy determine where to lock */
  for (hi = apr_hash_first(scratch_pool, wc_items);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *common;
      const char *wcroot_abspath = apr_hash_this_key(hi);
      apr_array_header_t *wc_targets = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);

      if (wc_targets->nelts == 1)
        {
          const char *target_abspath;
          target_abspath = APR_ARRAY_IDX(wc_targets, 0, const char *);

          if (! strcmp(wcroot_abspath, target_abspath))
            {
              APR_ARRAY_PUSH(*lock_targets, const char *)
                      = apr_pstrdup(result_pool, target_abspath);
            }
          else
            {
              /* Lock the parent to allow deleting the target */
              APR_ARRAY_PUSH(*lock_targets, const char *)
                      = svn_dirent_dirname(target_abspath, result_pool);
            }
        }
      else if (wc_targets->nelts > 1)
        {
          SVN_ERR(svn_dirent_condense_targets(&common, &wc_targets, wc_targets,
                                              FALSE, iterpool, iterpool));

          svn_sort__array(wc_targets, svn_sort_compare_paths);

          if (wc_targets->nelts == 0
              || !svn_path_is_empty(APR_ARRAY_IDX(wc_targets, 0, const char*))
              || !strcmp(common, wcroot_abspath))
            {
              APR_ARRAY_PUSH(*lock_targets, const char *)
                    = apr_pstrdup(result_pool, common);
            }
          else
            {
              /* Lock the parent to allow deleting the target */
              APR_ARRAY_PUSH(*lock_targets, const char *)
                       = svn_dirent_dirname(common, result_pool);
            }
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Baton for check_url_kind */
struct check_url_kind_baton
{
  apr_pool_t *pool;
  svn_ra_session_t *session;
  const char *repos_root_url;
  svn_client_ctx_t *ctx;
};

/* Implements svn_client__check_url_kind_t for svn_client_commit5 */
static svn_error_t *
check_url_kind(void *baton,
               svn_node_kind_t *kind,
               const char *url,
               svn_revnum_t revision,
               apr_pool_t *scratch_pool)
{
  struct check_url_kind_baton *cukb = baton;

  /* If we don't have a session or can't use the session, get one */
  if (!cukb->session || !svn_uri__is_ancestor(cukb->repos_root_url, url))
    {
      SVN_ERR(svn_client_open_ra_session2(&cukb->session, url, NULL, cukb->ctx,
                                          cukb->pool, scratch_pool));
      SVN_ERR(svn_ra_get_repos_root2(cukb->session, &cukb->repos_root_url,
                                     cukb->pool));
    }
  else
    SVN_ERR(svn_ra_reparent(cukb->session, url, scratch_pool));

  return svn_error_trace(
                svn_ra_check_path(cukb->session, "", revision,
                                  kind, scratch_pool));
}

/* Recurse into every target in REL_TARGETS, finding committable externals
 * nested within. Append these to REL_TARGETS itself. The paths in REL_TARGETS
 * are assumed to be / will be created relative to BASE_ABSPATH. The remaining
 * arguments correspond to those of svn_client_commit6(). */
static svn_error_t*
append_externals_as_explicit_targets(apr_array_header_t *rel_targets,
                                     const char *base_abspath,
                                     svn_boolean_t include_file_externals,
                                     svn_boolean_t include_dir_externals,
                                     svn_depth_t depth,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  int rel_targets_nelts_fixed;
  int i;
  apr_pool_t *iterpool;

  if (! (include_file_externals || include_dir_externals))
    return SVN_NO_ERROR;

  /* Easy part of applying DEPTH to externals. */
  if (depth == svn_depth_empty)
    {
      /* Don't recurse. */
      return SVN_NO_ERROR;
    }

  /* Iterate *and* grow REL_TARGETS at the same time. */
  rel_targets_nelts_fixed = rel_targets->nelts;

  iterpool = svn_pool_create(scratch_pool);

  for (i = 0; i < rel_targets_nelts_fixed; i++)
    {
      int j;
      const char *target;
      apr_array_header_t *externals = NULL;

      svn_pool_clear(iterpool);

      target = svn_dirent_join(base_abspath,
                               APR_ARRAY_IDX(rel_targets, i, const char *),
                               iterpool);

      /* ### TODO: Possible optimization: No need to do this for file targets.
       * ### But what's cheaper, stat'ing the file system or querying the db?
       * ### --> future. */

      SVN_ERR(svn_wc__committable_externals_below(&externals, ctx->wc_ctx,
                                                  target, depth,
                                                  iterpool, iterpool));

      if (externals != NULL)
        {
          const char *rel_target;

          for (j = 0; j < externals->nelts; j++)
            {
              svn_wc__committable_external_info_t *xinfo =
                         APR_ARRAY_IDX(externals, j,
                                       svn_wc__committable_external_info_t *);

              if ((xinfo->kind == svn_node_file && ! include_file_externals)
                  || (xinfo->kind == svn_node_dir && ! include_dir_externals))
                continue;

              rel_target = svn_dirent_skip_ancestor(base_abspath,
                                                    xinfo->local_abspath);

              SVN_ERR_ASSERT(rel_target != NULL && *rel_target != '\0');

              APR_ARRAY_PUSH(rel_targets, const char *) =
                                         apr_pstrdup(result_pool, rel_target);
            }
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_commit6(const apr_array_header_t *targets,
                   svn_depth_t depth,
                   svn_boolean_t keep_locks,
                   svn_boolean_t keep_changelists,
                   svn_boolean_t commit_as_operations,
                   svn_boolean_t include_file_externals,
                   svn_boolean_t include_dir_externals,
                   const apr_array_header_t *changelists,
                   const apr_hash_t *revprop_table,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const svn_delta_editor_t *editor;
  void *edit_baton;
  struct capture_baton_t cb;
  svn_ra_session_t *ra_session;
  const char *log_msg;
  const char *base_abspath;
  const char *base_url;
  apr_array_header_t *rel_targets;
  apr_array_header_t *lock_targets;
  apr_array_header_t *locks_obtained;
  svn_client__committables_t *committables;
  apr_hash_t *lock_tokens;
  apr_hash_t *sha1_checksums;
  apr_array_header_t *commit_items;
  svn_error_t *cmt_err = SVN_NO_ERROR;
  svn_error_t *bump_err = SVN_NO_ERROR;
  svn_error_t *unlock_err = SVN_NO_ERROR;
  svn_boolean_t commit_in_progress = FALSE;
  svn_boolean_t timestamp_sleep = FALSE;
  svn_commit_info_t *commit_info = NULL;
  apr_pool_t *iterpool = svn_pool_create(pool);
  const char *current_abspath;
  const char *notify_prefix;
  int depth_empty_after = -1;
  apr_hash_t *move_youngest = NULL;
  int i;

  SVN_ERR_ASSERT(depth != svn_depth_unknown && depth != svn_depth_exclude);

  /* Committing URLs doesn't make sense, so error if it's tried. */
  for (i = 0; i < targets->nelts; i++)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      if (svn_path_is_url(target))
        return svn_error_createf
          (SVN_ERR_ILLEGAL_TARGET, NULL,
           _("'%s' is a URL, but URLs cannot be commit targets"), target);
    }

  /* Condense the target list. This makes all targets absolute. */
  SVN_ERR(svn_dirent_condense_targets(&base_abspath, &rel_targets, targets,
                                      FALSE, pool, iterpool));

  /* No targets means nothing to commit, so just return. */
  if (base_abspath == NULL)
    return SVN_NO_ERROR;

  SVN_ERR_ASSERT(rel_targets != NULL);

  /* If we calculated only a base and no relative targets, this
     must mean that we are being asked to commit (effectively) a
     single path. */
  if (rel_targets->nelts == 0)
    APR_ARRAY_PUSH(rel_targets, const char *) = "";

  if (include_file_externals || include_dir_externals)
    {
      if (depth != svn_depth_unknown && depth != svn_depth_infinity)
        {
          /* All targets after this will be handled as depth empty */
          depth_empty_after = rel_targets->nelts;
        }

      SVN_ERR(append_externals_as_explicit_targets(rel_targets, base_abspath,
                                                   include_file_externals,
                                                   include_dir_externals,
                                                   depth, ctx,
                                                   pool, pool));
    }

  SVN_ERR(determine_lock_targets(&lock_targets, ctx->wc_ctx, base_abspath,
                                 rel_targets, pool, iterpool));

  locks_obtained = apr_array_make(pool, lock_targets->nelts,
                                  sizeof(const char *));

  for (i = 0; i < lock_targets->nelts; i++)
    {
      const char *lock_root;
      const char *target = APR_ARRAY_IDX(lock_targets, i, const char *);

      svn_pool_clear(iterpool);

      cmt_err = svn_error_trace(
                    svn_wc__acquire_write_lock(&lock_root, ctx->wc_ctx, target,
                                           FALSE, pool, iterpool));

      if (cmt_err)
        goto cleanup;

      APR_ARRAY_PUSH(locks_obtained, const char *) = lock_root;
    }

  /* Determine prefix to strip from the commit notify messages */
  SVN_ERR(svn_dirent_get_absolute(&current_abspath, "", pool));
  notify_prefix = svn_dirent_get_longest_ancestor(current_abspath,
                                                  base_abspath,
                                                  pool);

  /* Crawl the working copy for commit items. */
  {
    struct check_url_kind_baton cukb;

    /* Prepare for when we have a copy containing not-present nodes. */
    cukb.pool = iterpool;
    cukb.session = NULL; /* ### Can we somehow reuse session? */
    cukb.repos_root_url = NULL;
    cukb.ctx = ctx;

    cmt_err = svn_error_trace(
                   svn_client__harvest_committables(&committables,
                                                    &lock_tokens,
                                                    base_abspath,
                                                    rel_targets,
                                                    depth_empty_after,
                                                    depth,
                                                    ! keep_locks,
                                                    changelists,
                                                    check_url_kind,
                                                    &cukb,
                                                    ctx,
                                                    pool,
                                                    iterpool));

    svn_pool_clear(iterpool);
  }

  if (cmt_err)
    goto cleanup;

  if (apr_hash_count(committables->by_repository) == 0)
    {
      goto cleanup; /* Nothing to do */
    }
  else if (apr_hash_count(committables->by_repository) > 1)
    {
      cmt_err = svn_error_create(
             SVN_ERR_UNSUPPORTED_FEATURE, NULL,
             _("Commit can only commit to a single repository at a time.\n"
               "Are all targets part of the same working copy?"));
      goto cleanup;
    }

  {
    apr_hash_index_t *hi = apr_hash_first(iterpool,
                                          committables->by_repository);

    commit_items = apr_hash_this_val(hi);
  }

  /* If our array of targets contains only locks (and no actual file
     or prop modifications), then we return here to avoid committing a
     revision with no changes. */
  {
    svn_boolean_t found_changed_path = FALSE;

    for (i = 0; i < commit_items->nelts; ++i)
      {
        svn_client_commit_item3_t *item =
          APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);

        if (item->state_flags != SVN_CLIENT_COMMIT_ITEM_LOCK_TOKEN)
          {
            found_changed_path = TRUE;
            break;
          }
      }

    if (!found_changed_path)
      goto cleanup;
  }

  /* For every target that was moved verify that both halves of the
   * move are part of the commit. */
  for (i = 0; i < commit_items->nelts; i++)
    {
      svn_client_commit_item3_t *item =
        APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);

      svn_pool_clear(iterpool);

      if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_MOVED_HERE)
        {
          /* ### item->moved_from_abspath contains the move origin */
          const char *moved_from_abspath;
          const char *delete_op_root_abspath;

          cmt_err = svn_error_trace(svn_wc__node_was_moved_here(
                                      &moved_from_abspath,
                                      &delete_op_root_abspath,
                                      ctx->wc_ctx, item->path,
                                      iterpool, iterpool));
          if (cmt_err)
            goto cleanup;

          if (moved_from_abspath && delete_op_root_abspath)
            {
              svn_client_commit_item3_t *delete_half =
                svn_hash_gets(committables->by_path, delete_op_root_abspath);

              if (!delete_half)
                {
                  cmt_err = svn_error_createf(
                              SVN_ERR_ILLEGAL_TARGET, NULL,
                              _("Cannot commit '%s' because it was moved from "
                                "'%s' which is not part of the commit; both "
                                "sides of the move must be committed together"),
                              svn_dirent_local_style(item->path, iterpool),
                              svn_dirent_local_style(delete_op_root_abspath,
                                                     iterpool));

                  if (ctx->notify_func2)
                    {
                      svn_wc_notify_t *notify;
                      notify = svn_wc_create_notify(
                                    delete_op_root_abspath,
                                    svn_wc_notify_failed_requires_target,
                                    iterpool);
                      notify->err = cmt_err;

                      ctx->notify_func2(ctx->notify_baton2, notify, iterpool);
                    }

                  goto cleanup;
                }
              else if (delete_half->revision == item->copyfrom_rev)
                {
                  /* Ok, now we know that we perform an out-of-date check
                     on the copyfrom location. Remember this for a fixup
                     round right before committing. */

                  if (!move_youngest)
                    move_youngest = apr_hash_make(pool);

                  svn_hash_sets(move_youngest, item->path, item);
                }
            }
        }

      if (item->state_flags & SVN_CLIENT_COMMIT_ITEM_DELETE)
        {
          const char *moved_to_abspath;
          const char *copy_op_root_abspath;

          cmt_err = svn_error_trace(svn_wc__node_was_moved_away(
                                      &moved_to_abspath,
                                      &copy_op_root_abspath,
                                      ctx->wc_ctx, item->path,
                                      iterpool, iterpool));
          if (cmt_err)
            goto cleanup;

          if (moved_to_abspath && copy_op_root_abspath &&
              strcmp(moved_to_abspath, copy_op_root_abspath) == 0 &&
              svn_hash_gets(committables->by_path, copy_op_root_abspath)
              == NULL)
            {
              cmt_err = svn_error_createf(
                          SVN_ERR_ILLEGAL_TARGET, NULL,
                         _("Cannot commit '%s' because it was moved to '%s' "
                           "which is not part of the commit; both sides of "
                           "the move must be committed together"),
                         svn_dirent_local_style(item->path, iterpool),
                         svn_dirent_local_style(copy_op_root_abspath,
                                                iterpool));

              if (ctx->notify_func2)
                {
                    svn_wc_notify_t *notify;
                    notify = svn_wc_create_notify(
                                copy_op_root_abspath,
                                svn_wc_notify_failed_requires_target,
                                iterpool);
                    notify->err = cmt_err;

                    ctx->notify_func2(ctx->notify_baton2, notify, iterpool);
                }

              goto cleanup;
            }
        }
    }

  /* Go get a log message.  If an error occurs, or no log message is
     specified, abort the operation. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      const char *tmp_file;
      cmt_err = svn_error_trace(
                     svn_client__get_log_msg(&log_msg, &tmp_file, commit_items,
                                             ctx, pool));

      if (cmt_err || (! log_msg))
        goto cleanup;
    }
  else
    log_msg = "";

  /* Sort and condense our COMMIT_ITEMS. */
  cmt_err = svn_error_trace(svn_client__condense_commit_items(&base_url,
                                                              commit_items,
                                                              pool));

  if (cmt_err)
    goto cleanup;

  /* Collect our lock tokens with paths relative to base_url. */
  cmt_err = svn_error_trace(collect_lock_tokens(&lock_tokens, lock_tokens,
                                                base_url, pool));

  if (cmt_err)
    goto cleanup;

  cb.original_callback = commit_callback;
  cb.original_baton = commit_baton;
  cb.info = &commit_info;
  cb.pool = pool;

  /* Get the RA editor from the first lock target, rather than BASE_ABSPATH.
   * When committing from multiple WCs, BASE_ABSPATH might be an unrelated
   * parent of nested working copies. We don't support commits to multiple
   * repositories so using the first WC to get the RA session is safe. */
  cmt_err = svn_error_trace(
              svn_client__open_ra_session_internal(&ra_session, NULL, base_url,
                                                   APR_ARRAY_IDX(lock_targets,
                                                                 0,
                                                                 const char *),
                                                   commit_items,
                                                   TRUE, TRUE, ctx,
                                                   pool, pool));

  if (cmt_err)
    goto cleanup;

  if (move_youngest != NULL)
    {
      apr_hash_index_t *hi;
      svn_revnum_t youngest;

      SVN_ERR(svn_ra_get_latest_revnum(ra_session, &youngest, pool));

      for (hi = apr_hash_first(iterpool, move_youngest);
           hi;
           hi = apr_hash_next(hi))
        {
          svn_client_commit_item3_t *item = apr_hash_this_val(hi);

          /* We delete the original side with its original revision and will
             receive an out-of-date error if that node changed since that
             revision.

             The copy is of that same revision and we know that this revision
             didn't change between this revision and youngest. So we can just
             as well commit a copy from youngest.

            Note that it is still possible to see gaps between the delete and
            copy revisions as the repository might handle multiple commits
            at the same time (or when an out of date proxy is involved), but
            in general it should decrease the number of gaps. */

          if (item->copyfrom_rev < youngest)
            item->copyfrom_rev = youngest;
        }
    }

  cmt_err = svn_error_trace(
              get_ra_editor(&editor, &edit_baton, ra_session, ctx,
                            log_msg, commit_items, revprop_table,
                            lock_tokens, keep_locks, capture_commit_info,
                            &cb, pool));

  if (cmt_err)
    goto cleanup;

  /* Make a note that we have a commit-in-progress. */
  commit_in_progress = TRUE;

  /* We'll assume that, once we pass this point, we are going to need to
   * sleep for timestamps.  Really, we may not need to do unless and until
   * we reach the point where we post-commit 'bump' the WC metadata. */
  timestamp_sleep = TRUE;

  /* Perform the commit. */
  cmt_err = svn_error_trace(
              svn_client__do_commit(base_url, commit_items, editor, edit_baton,
                                    notify_prefix, &sha1_checksums, ctx, pool,
                                    iterpool));

  /* Handle a successful commit. */
  if ((! cmt_err)
      || (cmt_err->apr_err == SVN_ERR_REPOS_POST_COMMIT_HOOK_FAILED))
    {
      svn_wc_committed_queue_t *queue = svn_wc_committed_queue_create(pool);

      /* Make a note that our commit is finished. */
      commit_in_progress = FALSE;

      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item3_t *item
            = APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);

          svn_pool_clear(iterpool);
          bump_err = post_process_commit_item(
                       queue, item, ctx->wc_ctx,
                       keep_changelists, keep_locks, commit_as_operations,
                       svn_hash_gets(sha1_checksums, item->path),
                       iterpool);
          if (bump_err)
            goto cleanup;
        }

      SVN_ERR_ASSERT(commit_info);
      bump_err = svn_wc_process_committed_queue2(
                   queue, ctx->wc_ctx,
                   commit_info->revision,
                   commit_info->date,
                   commit_info->author,
                   ctx->cancel_func, ctx->cancel_baton,
                   iterpool);

      if (bump_err)
        goto cleanup;
    }

 cleanup:
  /* Sleep to ensure timestamp integrity.  BASE_ABSPATH may have been
     removed by the commit or it may the common ancestor of multiple
     working copies. */
  if (timestamp_sleep)
    {
      const char *sleep_abspath;
      svn_error_t *err = svn_wc__get_wcroot(&sleep_abspath, ctx->wc_ctx,
                                            base_abspath, pool, pool);
      if (err)
        {
          svn_error_clear(err);
          sleep_abspath = base_abspath;
        }

      svn_io_sleep_for_timestamps(sleep_abspath, pool);
    }

  /* Abort the commit if it is still in progress. */
  svn_pool_clear(iterpool); /* Close open handles before aborting */
  if (commit_in_progress)
    cmt_err = svn_error_compose_create(cmt_err,
                                       editor->abort_edit(edit_baton, pool));

  /* A bump error is likely to occur while running a working copy log file,
     explicitly unlocking and removing temporary files would be wrong in
     that case.  A commit error (cmt_err) should only occur before any
     attempt to modify the working copy, so it doesn't prevent explicit
     clean-up. */
  if (! bump_err)
    {
      /* Release all locks we obtained */
      for (i = 0; i < locks_obtained->nelts; i++)
        {
          const char *lock_root = APR_ARRAY_IDX(locks_obtained, i,
                                                const char *);

          svn_pool_clear(iterpool);

          unlock_err = svn_error_compose_create(
                           svn_wc__release_write_lock(ctx->wc_ctx, lock_root,
                                                      iterpool),
                           unlock_err);
        }
    }

  svn_pool_destroy(iterpool);

  return svn_error_trace(reconcile_errors(cmt_err, unlock_err, bump_err,
                                          pool));
}
