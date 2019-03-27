/*
 * switch.c:  implement 'switch' feature via WC & RA interfaces.
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

#include "svn_client.h"
#include "svn_error.h"
#include "svn_hash.h"
#include "svn_time.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_config.h"
#include "svn_pools.h"
#include "client.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/*** Code. ***/

/* This feature is essentially identical to 'svn update' (see
   ./update.c), but with two differences:

     - the reporter->finish_report() routine needs to make the server
       run delta_dirs() on two *different* paths, rather than on two
       identical paths.

     - after the update runs, we need to more than just
       ensure_uniform_revision;  we need to rewrite all the entries'
       URL attributes.
*/


/* A conflict callback that simply records the conflicted path in BATON.

   Implements svn_wc_conflict_resolver_func2_t.
*/
static svn_error_t *
record_conflict(svn_wc_conflict_result_t **result,
                const svn_wc_conflict_description2_t *description,
                void *baton,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  apr_hash_t *conflicted_paths = baton;

  svn_hash_sets(conflicted_paths,
                apr_pstrdup(apr_hash_pool_get(conflicted_paths),
                            description->local_abspath), "");
  *result = svn_wc_create_conflict_result(svn_wc_conflict_choose_postpone,
                                          NULL, result_pool);
  return SVN_NO_ERROR;
}

/* ...

   Add the paths of any conflict victims to CONFLICTED_PATHS, if that
   is not null.
*/
static svn_error_t *
switch_internal(svn_revnum_t *result_rev,
                apr_hash_t *conflicted_paths,
                const char *local_abspath,
                const char *anchor_abspath,
                const char *switch_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_depth_t depth,
                svn_boolean_t depth_is_sticky,
                svn_boolean_t ignore_externals,
                svn_boolean_t allow_unver_obstructions,
                svn_boolean_t ignore_ancestry,
                svn_boolean_t *timestamp_sleep,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  const svn_ra_reporter3_t *reporter;
  void *report_baton;
  const char *anchor_url, *target;
  svn_client__pathrev_t *switch_loc;
  svn_ra_session_t *ra_session;
  svn_revnum_t revnum;
  const char *diff3_cmd;
  apr_hash_t *wcroot_iprops;
  apr_array_header_t *inherited_props;
  svn_boolean_t use_commit_times;
  const svn_delta_editor_t *switch_editor;
  void *switch_edit_baton;
  const char *preserved_exts_str;
  apr_array_header_t *preserved_exts;
  svn_boolean_t server_supports_depth;
  struct svn_client__dirent_fetcher_baton_t dfb;
  svn_config_t *cfg = ctx->config
                      ? svn_hash_gets(ctx->config, SVN_CONFIG_CATEGORY_CONFIG)
                      : NULL;

  /* An unknown depth can't be sticky. */
  if (depth == svn_depth_unknown)
    depth_is_sticky = FALSE;

  /* Do not support the situation of both exclude and switch a target. */
  if (depth == svn_depth_exclude)
    return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("Cannot both exclude and switch a path"));

  /* Get the external diff3, if any. */
  svn_config_get(cfg, &diff3_cmd, SVN_CONFIG_SECTION_HELPERS,
                 SVN_CONFIG_OPTION_DIFF3_CMD, NULL);

  if (diff3_cmd != NULL)
    SVN_ERR(svn_path_cstring_to_utf8(&diff3_cmd, diff3_cmd, pool));

  /* See if the user wants last-commit timestamps instead of current ones. */
  SVN_ERR(svn_config_get_bool(cfg, &use_commit_times,
                              SVN_CONFIG_SECTION_MISCELLANY,
                              SVN_CONFIG_OPTION_USE_COMMIT_TIMES, FALSE));

  {
    svn_boolean_t has_working;
    SVN_ERR(svn_wc__node_has_working(&has_working, ctx->wc_ctx, local_abspath,
                                     pool));

    if (has_working)
      return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                               _("Cannot switch '%s' because it is not in the "
                                 "repository yet"),
                               svn_dirent_local_style(local_abspath, pool));
  }

  /* See which files the user wants to preserve the extension of when
     conflict files are made. */
  svn_config_get(cfg, &preserved_exts_str, SVN_CONFIG_SECTION_MISCELLANY,
                 SVN_CONFIG_OPTION_PRESERVED_CF_EXTS, "");
  preserved_exts = *preserved_exts_str
    ? svn_cstring_split(preserved_exts_str, "\n\r\t\v ", FALSE, pool)
    : NULL;

  /* Sanity check.  Without these, the switch is meaningless. */
  SVN_ERR_ASSERT(switch_url && (switch_url[0] != '\0'));

  if (strcmp(local_abspath, anchor_abspath))
    target = svn_dirent_basename(local_abspath, pool);
  else
    target = "";

  SVN_ERR(svn_wc__node_get_url(&anchor_url, ctx->wc_ctx, anchor_abspath,
                               pool, pool));
  if (! anchor_url)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("Directory '%s' has no URL"),
                             svn_dirent_local_style(anchor_abspath, pool));

  /* We may need to crop the tree if the depth is sticky */
  if (depth_is_sticky && depth < svn_depth_infinity)
    {
      svn_node_kind_t target_kind;

      if (depth == svn_depth_exclude)
        {
          SVN_ERR(svn_wc_exclude(ctx->wc_ctx,
                                 local_abspath,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 ctx->notify_func2, ctx->notify_baton2,
                                 pool));

          /* Target excluded, we are done now */
          return SVN_NO_ERROR;
        }

      SVN_ERR(svn_wc_read_kind2(&target_kind, ctx->wc_ctx, local_abspath,
                                TRUE, TRUE, pool));

      if (target_kind == svn_node_dir)
        SVN_ERR(svn_wc_crop_tree2(ctx->wc_ctx, local_abspath, depth,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  ctx->notify_func2, ctx->notify_baton2,
                                  pool));
    }

  /* Open an RA session to 'source' URL */
  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &switch_loc,
                                            switch_url, anchor_abspath,
                                            peg_revision, revision,
                                            ctx, pool));

  /* Disallow a switch operation to change the repository root of the
     target. */
  if (! svn_uri__is_ancestor(switch_loc->repos_root_url, anchor_url))
    return svn_error_createf(SVN_ERR_WC_INVALID_SWITCH, NULL,
                             _("'%s'\nis not the same repository as\n'%s'"),
                             anchor_url, switch_loc->repos_root_url);

  /* If we're not ignoring ancestry, then error out if the switch
     source and target don't have a common ancestory.

     ### We're acting on the anchor here, not the target.  Is that
     ### okay? */
  if (! ignore_ancestry)
    {
      svn_client__pathrev_t *target_base_loc, *yca;

      SVN_ERR(svn_client__wc_node_get_base(&target_base_loc, local_abspath,
                                           ctx->wc_ctx, pool, pool));

      if (!target_base_loc)
        yca = NULL; /* Not versioned */
      else
        {
          SVN_ERR(svn_client__get_youngest_common_ancestor(
                  &yca, switch_loc, target_base_loc, ra_session, ctx,
                  pool, pool));
        }
      if (! yca)
        return svn_error_createf(SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
                                 _("'%s' shares no common ancestry with '%s'"),
                                 switch_url,
                                 svn_dirent_local_style(local_abspath, pool));
    }

  wcroot_iprops = apr_hash_make(pool);

  /* Will the base of LOCAL_ABSPATH require an iprop cache post-switch?
     If we are switching LOCAL_ABSPATH to the root of the repository then
     we don't need to cache inherited properties.  In all other cases we
     *might* need to cache iprops. */
  if (strcmp(switch_loc->repos_root_url, switch_loc->url) != 0)
    {
      svn_boolean_t wc_root;
      svn_boolean_t needs_iprop_cache = TRUE;

      SVN_ERR(svn_wc__is_wcroot(&wc_root, ctx->wc_ctx, local_abspath,
                                pool));

      /* Switching the WC root to anything but the repos root means
         we need an iprop cache. */
      if (!wc_root)
        {
          /* We know we are switching a subtree to something other than the
             repos root, but if we are unswitching that subtree we don't
             need an iprops cache. */
          const char *target_parent_url;
          const char *unswitched_url;

          /* Calculate the URL LOCAL_ABSPATH would have if it was unswitched
             relative to its parent. */
          SVN_ERR(svn_wc__node_get_url(&target_parent_url, ctx->wc_ctx,
                                       svn_dirent_dirname(local_abspath,
                                                          pool),
                                       pool, pool));
          unswitched_url = svn_path_url_add_component2(
            target_parent_url,
            svn_dirent_basename(local_abspath, pool),
            pool);

          /* If LOCAL_ABSPATH will be unswitched relative to its parent, then
             it doesn't need an iprop cache.  Note: It doesn't matter if
             LOCAL_ABSPATH is withing a switched subtree, only if it's the
             *root* of a switched subtree.*/
          if (strcmp(unswitched_url, switch_loc->url) == 0)
            needs_iprop_cache = FALSE;
      }


      if (needs_iprop_cache)
        {
          SVN_ERR(svn_ra_get_inherited_props(ra_session, &inherited_props,
                                             "", switch_loc->rev, pool,
                                             pool));
          svn_hash_sets(wcroot_iprops, local_abspath, inherited_props);
        }
    }

  SVN_ERR(svn_ra_reparent(ra_session, anchor_url, pool));

  /* Fetch the switch (update) editor.  If REVISION is invalid, that's
     okay; the RA driver will call editor->set_target_revision() later on. */
  SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                SVN_RA_CAPABILITY_DEPTH, pool));

  dfb.ra_session = ra_session;
  dfb.anchor_url = anchor_url;
  dfb.target_revision = switch_loc->rev;

  SVN_ERR(svn_wc__get_switch_editor(&switch_editor, &switch_edit_baton,
                                    &revnum, ctx->wc_ctx, anchor_abspath,
                                    target, switch_loc->url, wcroot_iprops,
                                    use_commit_times, depth,
                                    depth_is_sticky, allow_unver_obstructions,
                                    server_supports_depth,
                                    diff3_cmd, preserved_exts,
                                    svn_client__dirent_fetcher, &dfb,
                                    conflicted_paths ? record_conflict : NULL,
                                    conflicted_paths,
                                    NULL, NULL,
                                    ctx->cancel_func, ctx->cancel_baton,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    pool, pool));

  /* Tell RA to do an update of URL+TARGET to REVISION; if we pass an
     invalid revnum, that means RA will use the latest revision. */
  SVN_ERR(svn_ra_do_switch3(ra_session, &reporter, &report_baton,
                            switch_loc->rev,
                            target,
                            depth_is_sticky ? depth : svn_depth_unknown,
                            switch_loc->url,
                            FALSE /* send_copyfrom_args */,
                            ignore_ancestry,
                            switch_editor, switch_edit_baton,
                            pool, pool));

  /* Past this point, we assume the WC is going to be modified so we will
   * need to sleep for timestamps. */
  *timestamp_sleep = TRUE;

  /* Drive the reporter structure, describing the revisions within
     LOCAL_ABSPATH.  When this calls reporter->finish_report, the
     reporter will drive the switch_editor. */
  SVN_ERR(svn_wc_crawl_revisions5(ctx->wc_ctx, local_abspath, reporter,
                                  report_baton, TRUE,
                                  depth, (! depth_is_sticky),
                                  (! server_supports_depth),
                                  use_commit_times,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  ctx->notify_func2, ctx->notify_baton2,
                                  pool));

  /* We handle externals after the switch is complete, so that
     handling external items (and any errors therefrom) doesn't delay
     the primary operation. */
  if (SVN_DEPTH_IS_RECURSIVE(depth) && (! ignore_externals))
    {
      apr_hash_t *new_externals;
      apr_hash_t *new_depths;
      SVN_ERR(svn_wc__externals_gather_definitions(&new_externals,
                                                   &new_depths,
                                                   ctx->wc_ctx, local_abspath,
                                                   depth, pool, pool));

      SVN_ERR(svn_client__handle_externals(new_externals,
                                           new_depths,
                                           switch_loc->repos_root_url,
                                           local_abspath,
                                           depth, timestamp_sleep, ra_session,
                                           ctx, pool));
    }

  /* Let everyone know we're finished here. */
  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(anchor_abspath, svn_wc_notify_update_completed,
                               pool);
      notify->kind = svn_node_none;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_inapplicable;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      notify->revision = revnum;
      ctx->notify_func2(ctx->notify_baton2, notify, pool);
    }

  /* If the caller wants the result revision, give it to them. */
  if (result_rev)
    *result_rev = revnum;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__switch_internal(svn_revnum_t *result_rev,
                            const char *path,
                            const char *switch_url,
                            const svn_opt_revision_t *peg_revision,
                            const svn_opt_revision_t *revision,
                            svn_depth_t depth,
                            svn_boolean_t depth_is_sticky,
                            svn_boolean_t ignore_externals,
                            svn_boolean_t allow_unver_obstructions,
                            svn_boolean_t ignore_ancestry,
                            svn_boolean_t *timestamp_sleep,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  const char *local_abspath, *anchor_abspath;
  svn_boolean_t acquired_lock;
  svn_error_t *err, *err1, *err2;
  apr_hash_t *conflicted_paths
    = ctx->conflict_func2 ? apr_hash_make(pool) : NULL;

  SVN_ERR_ASSERT(path);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  /* Rely on svn_wc__acquire_write_lock setting ANCHOR_ABSPATH even
     when it returns SVN_ERR_WC_LOCKED */
  err = svn_wc__acquire_write_lock(&anchor_abspath,
                                   ctx->wc_ctx, local_abspath, TRUE,
                                   pool, pool);
  if (err && err->apr_err != SVN_ERR_WC_LOCKED)
    return svn_error_trace(err);

  acquired_lock = (err == SVN_NO_ERROR);
  svn_error_clear(err);

  err1 = switch_internal(result_rev, conflicted_paths,
                         local_abspath, anchor_abspath,
                         switch_url, peg_revision, revision,
                         depth, depth_is_sticky,
                         ignore_externals,
                         allow_unver_obstructions, ignore_ancestry,
                         timestamp_sleep, ctx, pool);

  /* Give the conflict resolver callback the opportunity to
   * resolve any conflicts that were raised. */
  if (! err1 && ctx->conflict_func2)
    {
      err1 = svn_client__resolve_conflicts(NULL, conflicted_paths, ctx, pool);
    }

  if (acquired_lock)
    err2 = svn_wc__release_write_lock(ctx->wc_ctx, anchor_abspath, pool);
  else
    err2 = SVN_NO_ERROR;

  return svn_error_compose_create(err1, err2);
}

svn_error_t *
svn_client_switch3(svn_revnum_t *result_rev,
                   const char *path,
                   const char *switch_url,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t depth_is_sticky,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t allow_unver_obstructions,
                   svn_boolean_t ignore_ancestry,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_error_t *err;
  svn_boolean_t sleep_here = FALSE;

  if (svn_path_is_url(path))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"), path);

  err = svn_client__switch_internal(result_rev, path, switch_url,
                                    peg_revision, revision, depth,
                                    depth_is_sticky, ignore_externals,
                                    allow_unver_obstructions,
                                    ignore_ancestry, &sleep_here, ctx, pool);

  /* Sleep to ensure timestamp integrity (we do this regardless of
     errors in the actual switch operation(s)). */
  if (sleep_here)
    svn_io_sleep_for_timestamps(path, pool);

  return svn_error_trace(err);
}
