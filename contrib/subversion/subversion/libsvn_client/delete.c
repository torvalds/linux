/*
 * delete.c:  wrappers around wc delete functionality.
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

#include <apr_file_io.h>
#include "svn_hash.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "client.h"

#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_ra_private.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Baton for find_undeletables */
struct can_delete_baton_t
{
  const char *root_abspath;
  svn_boolean_t target_missing;
};

/* An svn_wc_status_func4_t callback function for finding
   status structures which are not safely deletable. */
static svn_error_t *
find_undeletables(void *baton,
                  const char *local_abspath,
                  const svn_wc_status3_t *status,
                  apr_pool_t *pool)
{
  if (status->node_status == svn_wc_status_missing)
    {
      struct can_delete_baton_t *cdt = baton;

      if (strcmp(cdt->root_abspath, local_abspath) == 0)
        cdt->target_missing = TRUE;
    }

  /* Check for error-ful states. */
  if (status->node_status == svn_wc_status_obstructed)
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("'%s' is in the way of the resource "
                               "actually under version control"),
                             svn_dirent_local_style(local_abspath, pool));
  else if (! status->versioned)
    return svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                             _("'%s' is not under version control"),
                             svn_dirent_local_style(local_abspath, pool));
  else if ((status->node_status == svn_wc_status_added
            || status->node_status == svn_wc_status_replaced)
           && status->text_status == svn_wc_status_normal
           && (status->prop_status == svn_wc_status_normal
               || status->prop_status == svn_wc_status_none))
    {
      /* Unmodified copy. Go ahead, remove it */
    }
  else if (status->node_status != svn_wc_status_normal
           && status->node_status != svn_wc_status_deleted
           && status->node_status != svn_wc_status_missing)
    return svn_error_createf(SVN_ERR_CLIENT_MODIFIED, NULL,
                             _("'%s' has local modifications -- commit or "
                               "revert them first"),
                             svn_dirent_local_style(local_abspath, pool));

  return SVN_NO_ERROR;
}

/* Check whether LOCAL_ABSPATH is an external and raise an error if it is.

   A file external should not be deleted since the file external is
   implemented as a switched file and it would delete the file the
   file external is switched to, which is not the behavior the user
   would probably want.

   A directory external should not be deleted since it is the root
   of a different working copy. */
static svn_error_t *
check_external(const char *local_abspath,
               svn_client_ctx_t *ctx,
               apr_pool_t *scratch_pool)
{
  svn_node_kind_t external_kind;
  const char *defining_abspath;

  SVN_ERR(svn_wc__read_external_info(&external_kind, &defining_abspath, NULL,
                                     NULL, NULL,
                                     ctx->wc_ctx, local_abspath,
                                     local_abspath, TRUE,
                                     scratch_pool, scratch_pool));

  if (external_kind != svn_node_none)
    return svn_error_createf(SVN_ERR_WC_CANNOT_DELETE_FILE_EXTERNAL, NULL,
                             _("Cannot remove the external at '%s'; "
                               "please edit or delete the svn:externals "
                               "property on '%s'"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool),
                             svn_dirent_local_style(defining_abspath,
                                                    scratch_pool));

  return SVN_NO_ERROR;
}

/* Verify that the path can be deleted without losing stuff,
   i.e. ensure that there are no modified or unversioned resources
   under PATH.  This is similar to checking the output of the status
   command.  CTX is used for the client's config options.  POOL is
   used for all temporary allocations. */
static svn_error_t *
can_delete_node(svn_boolean_t *target_missing,
                const char *local_abspath,
                svn_client_ctx_t *ctx,
                apr_pool_t *scratch_pool)
{
  apr_array_header_t *ignores;
  struct can_delete_baton_t cdt;

  /* Use an infinite-depth status check to see if there's anything in
     or under PATH which would make it unsafe for deletion.  The
     status callback function find_undeletables() makes the
     determination, returning an error if it finds anything that shouldn't
     be deleted. */

  SVN_ERR(svn_wc_get_default_ignores(&ignores, ctx->config, scratch_pool));

  cdt.root_abspath = local_abspath;
  cdt.target_missing = FALSE;

  SVN_ERR(svn_wc_walk_status(ctx->wc_ctx,
                             local_abspath,
                             svn_depth_infinity,
                             FALSE /* get_all */,
                             FALSE /* no_ignore */,
                             FALSE /* ignore_text_mod */,
                             ignores,
                             find_undeletables, &cdt,
                             ctx->cancel_func,
                             ctx->cancel_baton,
                             scratch_pool));

  if (target_missing)
    *target_missing = cdt.target_missing;

  return SVN_NO_ERROR;
}


static svn_error_t *
path_driver_cb_func(void **dir_baton,
                    void *parent_baton,
                    void *callback_baton,
                    const char *path,
                    apr_pool_t *pool)
{
  const svn_delta_editor_t *editor = callback_baton;
  *dir_baton = NULL;
  return editor->delete_entry(path, SVN_INVALID_REVNUM, parent_baton, pool);
}

static svn_error_t *
single_repos_delete(svn_ra_session_t *ra_session,
                    const char *base_uri,
                    const apr_array_header_t *relpaths,
                    const apr_hash_t *revprop_table,
                    svn_commit_callback2_t commit_callback,
                    void *commit_baton,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  const svn_delta_editor_t *editor;
  apr_hash_t *commit_revprops;
  void *edit_baton;
  const char *log_msg;
  int i;
  svn_error_t *err;

  /* Create new commit items and add them to the array. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items
        = apr_array_make(pool, relpaths->nelts, sizeof(item));

      for (i = 0; i < relpaths->nelts; i++)
        {
          const char *relpath = APR_ARRAY_IDX(relpaths, i, const char *);

          item = svn_client_commit_item3_create(pool);
          item->url = svn_path_url_add_component2(base_uri, relpath, pool);
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_DELETE;
          APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
        }
      SVN_ERR(svn_client__get_log_msg(&log_msg, &tmp_file, commit_items,
                                      ctx, pool));
      if (! log_msg)
        return SVN_NO_ERROR;
    }
  else
    log_msg = "";

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           log_msg, ctx, pool));

  /* Fetch RA commit editor */
  SVN_ERR(svn_ra__register_editor_shim_callbacks(ra_session,
                        svn_client__get_shim_callbacks(ctx->wc_ctx,
                                                       NULL, pool)));
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    commit_revprops,
                                    commit_callback,
                                    commit_baton,
                                    NULL, TRUE, /* No lock tokens */
                                    pool));

  /* Call the path-based editor driver. */
  err = svn_delta_path_driver2(editor, edit_baton, relpaths, TRUE,
                               path_driver_cb_func, (void *)editor, pool);

  if (err)
    {
      return svn_error_trace(
               svn_error_compose_create(err,
                                        editor->abort_edit(edit_baton, pool)));
    }

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;
      notify = svn_wc_create_notify_url(base_uri,
                                        svn_wc_notify_commit_finalizing,
                                        pool);
      ctx->notify_func2(ctx->notify_baton2, notify, pool);
    }

  /* Close the edit. */
  return svn_error_trace(editor->close_edit(edit_baton, pool));
}


/* Structure for tracking remote delete targets associated with a
   specific repository. */
struct repos_deletables_t
{
  svn_ra_session_t *ra_session;
  apr_array_header_t *target_uris;
};


static svn_error_t *
delete_urls_multi_repos(const apr_array_header_t *uris,
                        const apr_hash_t *revprop_table,
                        svn_commit_callback2_t commit_callback,
                        void *commit_baton,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  apr_hash_t *deletables = apr_hash_make(pool);
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;
  int i;

  /* Create a hash mapping repository root URLs -> repos_deletables_t *
     structures.  */
  for (i = 0; i < uris->nelts; i++)
    {
      const char *uri = APR_ARRAY_IDX(uris, i, const char *);
      struct repos_deletables_t *repos_deletables = NULL;
      const char *repos_relpath;
      svn_node_kind_t kind;

      for (hi = apr_hash_first(pool, deletables); hi; hi = apr_hash_next(hi))
        {
          const char *repos_root = apr_hash_this_key(hi);

          repos_relpath = svn_uri_skip_ancestor(repos_root, uri, pool);
          if (repos_relpath)
            {
              /* Great!  We've found another URI underneath this
                 session.  We'll pick out the related RA session for
                 use later, store the new target, and move on.  */
              repos_deletables = apr_hash_this_val(hi);
              APR_ARRAY_PUSH(repos_deletables->target_uris, const char *) =
                apr_pstrdup(pool, uri);
              break;
            }
        }

      /* If we haven't created a repos_deletable structure for this
         delete target, we need to do.  That means opening up an RA
         session and initializing its targets list.  */
      if (!repos_deletables)
        {
          svn_ra_session_t *ra_session = NULL;
          const char *repos_root;
          apr_array_header_t *target_uris;

          /* Open an RA session to (ultimately) the root of the
             repository in which URI is found.  */
          SVN_ERR(svn_client_open_ra_session2(&ra_session, uri, NULL,
                                              ctx, pool, pool));
          SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root, pool));
          SVN_ERR(svn_ra_reparent(ra_session, repos_root, pool));
          repos_relpath = svn_uri_skip_ancestor(repos_root, uri, pool);

          /* Make a new relpaths list for this repository, and add
             this URI's relpath to it. */
          target_uris = apr_array_make(pool, 1, sizeof(const char *));
          APR_ARRAY_PUSH(target_uris, const char *) = apr_pstrdup(pool, uri);

          /* Build our repos_deletables_t item and stash it in the
             hash. */
          repos_deletables = apr_pcalloc(pool, sizeof(*repos_deletables));
          repos_deletables->ra_session = ra_session;
          repos_deletables->target_uris = target_uris;
          svn_hash_sets(deletables, repos_root, repos_deletables);
        }

      /* If we get here, we should have been able to calculate a
         repos_relpath for this URI.  Let's make sure.  (We return an
         RA error code otherwise for 1.6 compatibility.)  */
      if (!repos_relpath || !*repos_relpath)
        return svn_error_createf(SVN_ERR_RA_ILLEGAL_URL, NULL,
                                 _("URL '%s' not within a repository"), uri);

      /* Now, test to see if the thing actually exists in HEAD. */
      SVN_ERR(svn_ra_check_path(repos_deletables->ra_session, repos_relpath,
                                SVN_INVALID_REVNUM, &kind, pool));
      if (kind == svn_node_none)
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("URL '%s' does not exist"), uri);
    }

  /* Now we iterate over the DELETABLES hash, issuing a commit for
     each repository with its associated collected targets. */
  iterpool = svn_pool_create(pool);
  for (hi = apr_hash_first(pool, deletables); hi; hi = apr_hash_next(hi))
    {
      struct repos_deletables_t *repos_deletables = apr_hash_this_val(hi);
      const char *base_uri;
      apr_array_header_t *target_relpaths;

      svn_pool_clear(iterpool);

      /* We want to anchor the commit on the longest common path
         across the targets for this one repository.  If, however, one
         of our targets is that longest common path, we need instead
         anchor the commit on that path's immediate parent.  Because
         we're asking svn_uri_condense_targets() to remove
         redundancies, this situation should be detectable by their
         being returned either a) only a single, empty-path, target
         relpath, or b) no target relpaths at all.  */
      SVN_ERR(svn_uri_condense_targets(&base_uri, &target_relpaths,
                                       repos_deletables->target_uris,
                                       TRUE, iterpool, iterpool));
      SVN_ERR_ASSERT(!svn_path_is_empty(base_uri));
      if (target_relpaths->nelts == 0)
        {
          const char *target_relpath;

          svn_uri_split(&base_uri, &target_relpath, base_uri, iterpool);
          APR_ARRAY_PUSH(target_relpaths, const char *) = target_relpath;
        }
      else if ((target_relpaths->nelts == 1)
               && (svn_path_is_empty(APR_ARRAY_IDX(target_relpaths, 0,
                                                   const char *))))
        {
          const char *target_relpath;

          svn_uri_split(&base_uri, &target_relpath, base_uri, iterpool);
          APR_ARRAY_IDX(target_relpaths, 0, const char *) = target_relpath;
        }

      SVN_ERR(svn_ra_reparent(repos_deletables->ra_session, base_uri, pool));
      SVN_ERR(single_repos_delete(repos_deletables->ra_session, base_uri,
                                  target_relpaths,
                                  revprop_table, commit_callback,
                                  commit_baton, ctx, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__wc_delete(const char *local_abspath,
                      svn_boolean_t force,
                      svn_boolean_t dry_run,
                      svn_boolean_t keep_local,
                      svn_wc_notify_func2_t notify_func,
                      void *notify_baton,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  svn_boolean_t target_missing = FALSE;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(check_external(local_abspath, ctx, pool));

  if (!force && !keep_local)
    /* Verify that there are no "awkward" files */
    SVN_ERR(can_delete_node(&target_missing, local_abspath, ctx, pool));

  if (!dry_run)
    /* Mark the entry for commit deletion and perform wc deletion */
    return svn_error_trace(svn_wc_delete4(ctx->wc_ctx, local_abspath,
                                          keep_local || target_missing
                                                            /*keep_local */,
                                          TRUE /* delete_unversioned */,
                                          ctx->cancel_func, ctx->cancel_baton,
                                          notify_func, notify_baton, pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__wc_delete_many(const apr_array_header_t *targets,
                           svn_boolean_t force,
                           svn_boolean_t dry_run,
                           svn_boolean_t keep_local,
                           svn_wc_notify_func2_t notify_func,
                           void *notify_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  int i;
  svn_boolean_t has_non_missing = FALSE;

  for (i = 0; i < targets->nelts; i++)
    {
      const char *local_abspath = APR_ARRAY_IDX(targets, i, const char *);

      SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

      SVN_ERR(check_external(local_abspath, ctx, pool));

      if (!force && !keep_local)
        {
          svn_boolean_t missing;
          /* Verify that there are no "awkward" files */

          SVN_ERR(can_delete_node(&missing, local_abspath, ctx, pool));

          if (! missing)
            has_non_missing = TRUE;
        }
      else
        has_non_missing = TRUE;
    }

  if (!dry_run)
    {
      /* Mark the entry for commit deletion and perform wc deletion */

      /* If none of the targets exists, pass keep local TRUE, to avoid
         deleting case-different files. Detecting this in the generic case
         from the delete code is expensive */
      return svn_error_trace(svn_wc__delete_many(ctx->wc_ctx, targets,
                                                 keep_local || !has_non_missing,
                                                 TRUE /* delete_unversioned_target */,
                                                 ctx->cancel_func,
                                                 ctx->cancel_baton,
                                                 notify_func, notify_baton,
                                                 pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_delete4(const apr_array_header_t *paths,
                   svn_boolean_t force,
                   svn_boolean_t keep_local,
                   const apr_hash_t *revprop_table,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_boolean_t is_url;

  if (! paths->nelts)
    return SVN_NO_ERROR;

  SVN_ERR(svn_client__assert_homogeneous_target_type(paths));
  is_url = svn_path_is_url(APR_ARRAY_IDX(paths, 0, const char *));

  if (is_url)
    {
      SVN_ERR(delete_urls_multi_repos(paths, revprop_table, commit_callback,
                                      commit_baton, ctx, pool));
    }
  else
    {
      const char *local_abspath;
      apr_hash_t *wcroots;
      apr_hash_index_t *hi;
      int i;
      int j;
      apr_pool_t *iterpool;
      svn_boolean_t is_new_target;

      /* Build a map of wcroots and targets within them. */
      wcroots = apr_hash_make(pool);
      iterpool = svn_pool_create(pool);
      for (i = 0; i < paths->nelts; i++)
        {
          const char *wcroot_abspath;
          apr_array_header_t *targets;

          svn_pool_clear(iterpool);

          /* See if the user wants us to stop. */
          if (ctx->cancel_func)
            SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

          SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                          APR_ARRAY_IDX(paths, i,
                                                        const char *),
                                          pool));
          SVN_ERR(svn_wc__get_wcroot(&wcroot_abspath, ctx->wc_ctx,
                                     local_abspath, pool, iterpool));
          targets = svn_hash_gets(wcroots, wcroot_abspath);
          if (targets == NULL)
            {
              targets = apr_array_make(pool, 1, sizeof(const char *));
              svn_hash_sets(wcroots, wcroot_abspath, targets);
             }

          /* Make sure targets are unique. */
          is_new_target = TRUE;
          for (j = 0; j < targets->nelts; j++)
            {
              if (strcmp(APR_ARRAY_IDX(targets, j, const char *),
                         local_abspath) == 0)
                {
                  is_new_target = FALSE;
                  break;
                }
            }

          if (is_new_target)
            APR_ARRAY_PUSH(targets, const char *) = local_abspath;
        }

      /* Delete the targets from each working copy in turn. */
      for (hi = apr_hash_first(pool, wcroots); hi; hi = apr_hash_next(hi))
        {
          const char *root_abspath;
          const apr_array_header_t *targets = apr_hash_this_val(hi);

          svn_pool_clear(iterpool);

          SVN_ERR(svn_dirent_condense_targets(&root_abspath, NULL, targets,
                                              FALSE, iterpool, iterpool));

          SVN_WC__CALL_WITH_WRITE_LOCK(
            svn_client__wc_delete_many(targets, force, FALSE, keep_local,
                                       ctx->notify_func2, ctx->notify_baton2,
                                       ctx, iterpool),
            ctx->wc_ctx, root_abspath, TRUE /* lock_anchor */,
            iterpool);
        }
      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}
