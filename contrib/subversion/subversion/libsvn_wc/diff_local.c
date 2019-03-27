/*
 * diff_local.c -- A simple diff walker which compares local files against
 *                 their pristine versions.
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
 *
 * This is the simple working copy diff algorithm which is used when you
 * just use 'svn diff PATH'. It shows what is modified in your working copy
 * since a node was checked out or copied but doesn't show most kinds of
 * restructuring operations.
 *
 * You can look at this as another form of the status walker.
 */

#include <apr_hash.h>

#include "svn_error.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"

#include "private/svn_wc_private.h"
#include "private/svn_diff_tree.h"

#include "wc.h"
#include "wc_db.h"
#include "props.h"
#include "diff.h"

#include "svn_private_config.h"

/*-------------------------------------------------------------------------*/

/* Baton containing the state of a directory
   reported open via a diff processor */
struct node_state_t
{
  struct node_state_t *parent;

  apr_pool_t *pool;

  const char *local_abspath;
  const char *relpath;
  void *baton;

  svn_diff_source_t *left_src;
  svn_diff_source_t *right_src;
  svn_diff_source_t *copy_src;

  svn_boolean_t skip;
  svn_boolean_t skip_children;

  apr_hash_t *left_props;
  apr_hash_t *right_props;
  const apr_array_header_t *propchanges;
};

/* The diff baton */
struct diff_baton
{
  /* A wc db. */
  svn_wc__db_t *db;

  /* Report editor paths relative from this directory */
  const char *anchor_abspath;

  struct node_state_t *cur;

  const svn_diff_tree_processor_t *processor;

  /* Should this diff ignore node ancestry? */
  svn_boolean_t ignore_ancestry;

  /* Cancel function/baton */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  apr_pool_t *pool;
};

/* Recursively opens directories on the stack in EB, until LOCAL_ABSPATH
   is reached. If RECURSIVE_SKIP is TRUE, don't open LOCAL_ABSPATH itself,
   but create it marked with skip+skip_children.
 */
static svn_error_t *
ensure_state(struct diff_baton *eb,
             const char *local_abspath,
             svn_boolean_t recursive_skip,
             apr_pool_t *scratch_pool)
{
  struct node_state_t *ns;
  apr_pool_t *ns_pool;
  if (!eb->cur)
    {
      const char *relpath;

      relpath = svn_dirent_skip_ancestor(eb->anchor_abspath, local_abspath);
      if (! relpath)
        return SVN_NO_ERROR;

      /* Don't recurse on the anchor, as that might loop infinitely because
            svn_dirent_dirname("/",...)   -> "/"
            svn_dirent_dirname("C:/",...) -> "C:/" (Windows) */
      if (*relpath)
        SVN_ERR(ensure_state(eb,
                             svn_dirent_dirname(local_abspath, scratch_pool),
                             FALSE,
                             scratch_pool));
    }
  else if (svn_dirent_is_child(eb->cur->local_abspath, local_abspath, NULL))
    SVN_ERR(ensure_state(eb, svn_dirent_dirname(local_abspath, scratch_pool),
                         FALSE,
                         scratch_pool));
  else
    return SVN_NO_ERROR;

  if (eb->cur && eb->cur->skip_children)
    return SVN_NO_ERROR;

  ns_pool = svn_pool_create(eb->cur ? eb->cur->pool : eb->pool);
  ns = apr_pcalloc(ns_pool, sizeof(*ns));

  ns->pool = ns_pool;
  ns->local_abspath = apr_pstrdup(ns_pool, local_abspath);
  ns->relpath = svn_dirent_skip_ancestor(eb->anchor_abspath, ns->local_abspath);
  ns->parent = eb->cur;
  eb->cur = ns;

  if (recursive_skip)
    {
      ns->skip = TRUE;
      ns->skip_children = TRUE;
      return SVN_NO_ERROR;
    }

  {
    svn_revnum_t revision;
    svn_error_t *err;

    err = svn_wc__db_base_get_info(NULL, NULL, &revision, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   eb->db, local_abspath,
                                   scratch_pool, scratch_pool);

    if (err)
      {
        if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
          return svn_error_trace(err);
        svn_error_clear(err);

        revision = 0; /* Use original revision? */
      }
    ns->left_src = svn_diff__source_create(revision, ns->pool);
    ns->right_src = svn_diff__source_create(SVN_INVALID_REVNUM, ns->pool);

    SVN_ERR(eb->processor->dir_opened(&ns->baton, &ns->skip,
                                      &ns->skip_children,
                                      ns->relpath,
                                      ns->left_src,
                                      ns->right_src,
                                      NULL /* copyfrom_source */,
                                      ns->parent ? ns->parent->baton : NULL,
                                      eb->processor,
                                      ns->pool, scratch_pool));
  }

  return SVN_NO_ERROR;
}

/* Implements svn_wc_status_func3_t */
static svn_error_t *
diff_status_callback(void *baton,
                     const char *local_abspath,
                     const svn_wc_status3_t *status,
                     apr_pool_t *scratch_pool)
{
  struct diff_baton *eb = baton;
  svn_wc__db_t *db = eb->db;

  if (! status->versioned)
    return SVN_NO_ERROR; /* unversioned (includes dir externals) */

  if (status->node_status == svn_wc_status_conflicted
      && status->text_status == svn_wc_status_none
      && status->prop_status == svn_wc_status_none)
    {
      /* Node is an actual only node describing a tree conflict */
      return SVN_NO_ERROR;
    }

  /* Not text/prop modified, not copied. Easy out */
  if (status->node_status == svn_wc_status_normal && !status->copied)
    return SVN_NO_ERROR;

  /* Mark all directories where we are no longer inside as closed */
  while (eb->cur
         && !svn_dirent_is_ancestor(eb->cur->local_abspath, local_abspath))
    {
      struct node_state_t *ns = eb->cur;

      if (!ns->skip)
        {
          if (ns->propchanges)
            SVN_ERR(eb->processor->dir_changed(ns->relpath,
                                               ns->left_src,
                                               ns->right_src,
                                               ns->left_props,
                                               ns->right_props,
                                               ns->propchanges,
                                               ns->baton,
                                               eb->processor,
                                               ns->pool));
          else
            SVN_ERR(eb->processor->dir_closed(ns->relpath,
                                              ns->left_src,
                                              ns->right_src,
                                              ns->baton,
                                              eb->processor,
                                              ns->pool));
        }
      eb->cur = ns->parent;
      svn_pool_clear(ns->pool);
    }
  SVN_ERR(ensure_state(eb, svn_dirent_dirname(local_abspath, scratch_pool),
                       FALSE, scratch_pool));

  if (eb->cur && eb->cur->skip_children)
    return SVN_NO_ERROR;

  /* This code does about the same thing as the inner body of
     walk_local_nodes_diff() in diff_editor.c, except that
     it is already filtered by the status walker, doesn't have to
     account for remote changes (and many tiny other details) */

  {
    svn_boolean_t repos_only;
    svn_boolean_t local_only;
    svn_wc__db_status_t db_status;
    svn_boolean_t have_base;
    svn_node_kind_t base_kind;
    svn_node_kind_t db_kind = status->kind;
    svn_depth_t depth_below_here = svn_depth_unknown;

    const char *child_abspath = local_abspath;
    const char *child_relpath = svn_dirent_skip_ancestor(eb->anchor_abspath,
                                                         local_abspath);


    repos_only = FALSE;
    local_only = FALSE;

    /* ### optimize away this call using status info. Should
           be possible in almost every case (except conflict, missing, obst.)*/
    SVN_ERR(svn_wc__db_read_info(&db_status, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL,
                                 &have_base, NULL, NULL,
                                 eb->db, local_abspath,
                                 scratch_pool, scratch_pool));
    if (!have_base)
      {
        local_only = TRUE; /* Only report additions */
      }
    else if (db_status == svn_wc__db_status_normal
             || db_status == svn_wc__db_status_incomplete)
      {
        /* Simple diff */
        base_kind = db_kind;
      }
    else if (db_status == svn_wc__db_status_deleted)
      {
        svn_wc__db_status_t base_status;
        repos_only = TRUE;
        SVN_ERR(svn_wc__db_base_get_info(&base_status, &base_kind, NULL,
                                         NULL, NULL, NULL, NULL, NULL,
                                         NULL, NULL, NULL, NULL, NULL,
                                         NULL, NULL, NULL,
                                         eb->db, local_abspath,
                                         scratch_pool, scratch_pool));

        if (base_status != svn_wc__db_status_normal
            && base_status != svn_wc__db_status_incomplete)
          return SVN_NO_ERROR;
      }
    else
      {
        /* working status is either added or deleted */
        svn_wc__db_status_t base_status;

        SVN_ERR(svn_wc__db_base_get_info(&base_status, &base_kind, NULL,
                                         NULL, NULL, NULL, NULL, NULL,
                                         NULL, NULL, NULL, NULL, NULL,
                                         NULL, NULL, NULL,
                                         eb->db, local_abspath,
                                         scratch_pool, scratch_pool));

        if (base_status != svn_wc__db_status_normal
            && base_status != svn_wc__db_status_incomplete)
          local_only = TRUE;
        else if (base_kind != db_kind || !eb->ignore_ancestry)
          {
            repos_only = TRUE;
            local_only = TRUE;
          }
      }

    if (repos_only)
      {
        /* Report repository form deleted */
        if (base_kind == svn_node_file)
          SVN_ERR(svn_wc__diff_base_only_file(db, child_abspath,
                                              child_relpath,
                                              SVN_INVALID_REVNUM,
                                              eb->processor,
                                              eb->cur ? eb->cur->baton : NULL,
                                              scratch_pool));
        else if (base_kind == svn_node_dir)
          SVN_ERR(svn_wc__diff_base_only_dir(db, child_abspath,
                                             child_relpath,
                                             SVN_INVALID_REVNUM,
                                             depth_below_here,
                                             eb->processor,
                                             eb->cur ? eb->cur->baton : NULL,
                                             eb->cancel_func,
                                             eb->cancel_baton,
                                             scratch_pool));
      }
    else if (!local_only)
      {
        /* Diff base against actual */
        if (db_kind == svn_node_file)
          {
            SVN_ERR(svn_wc__diff_base_working_diff(db, child_abspath,
                                                   child_relpath,
                                                   SVN_INVALID_REVNUM,
                                                   eb->processor,
                                                   eb->cur
                                                        ? eb->cur->baton
                                                        : NULL,
                                                   FALSE,
                                                   eb->cancel_func,
                                                   eb->cancel_baton,
                                                   scratch_pool));
          }
        else if (db_kind == svn_node_dir)
          {
            SVN_ERR(ensure_state(eb, local_abspath, FALSE, scratch_pool));

            if (status->prop_status != svn_wc_status_none
                && status->prop_status != svn_wc_status_normal)
              {
                apr_array_header_t *propchanges;
                SVN_ERR(svn_wc__db_base_get_props(&eb->cur->left_props,
                                                  eb->db, local_abspath,
                                                  eb->cur->pool,
                                                  scratch_pool));
                SVN_ERR(svn_wc__db_read_props(&eb->cur->right_props,
                                              eb->db, local_abspath,
                                              eb->cur->pool,
                                              scratch_pool));

                SVN_ERR(svn_prop_diffs(&propchanges,
                                       eb->cur->right_props,
                                       eb->cur->left_props,
                                       eb->cur->pool));

                eb->cur->propchanges = propchanges;
              }
          }
      }

    if (local_only && (db_status != svn_wc__db_status_deleted))
      {
        /* Moved from. Relative from diff anchor*/
        const char *moved_from_relpath = NULL;

        if (status->moved_from_abspath)
          {
            moved_from_relpath = svn_dirent_skip_ancestor(
                                          eb->anchor_abspath,
                                          status->moved_from_abspath);
          }

        if (db_kind == svn_node_file)
          SVN_ERR(svn_wc__diff_local_only_file(db, child_abspath,
                                               child_relpath,
                                               moved_from_relpath,
                                               eb->processor,
                                               eb->cur ? eb->cur->baton : NULL,
                                               FALSE,
                                               eb->cancel_func,
                                               eb->cancel_baton,
                                               scratch_pool));
        else if (db_kind == svn_node_dir)
          SVN_ERR(svn_wc__diff_local_only_dir(db, child_abspath,
                                              child_relpath, depth_below_here,
                                              moved_from_relpath,
                                              eb->processor,
                                              eb->cur ? eb->cur->baton : NULL,
                                              FALSE,
                                              eb->cancel_func,
                                              eb->cancel_baton,
                                              scratch_pool));
      }

    if (db_kind == svn_node_dir && (local_only || repos_only))
      SVN_ERR(ensure_state(eb, local_abspath, TRUE /* skip */, scratch_pool));
  }

  return SVN_NO_ERROR;
}


/* Public Interface */
svn_error_t *
svn_wc__diff7(const char **root_relpath,
              svn_boolean_t *root_is_dir,
              svn_wc_context_t *wc_ctx,
              const char *local_abspath,
              svn_depth_t depth,
              svn_boolean_t ignore_ancestry,
              const apr_array_header_t *changelist_filter,
              const svn_diff_tree_processor_t *diff_processor,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  struct diff_baton eb = { 0 };
  svn_node_kind_t kind;
  svn_boolean_t get_all;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR(svn_wc__db_read_kind(&kind, wc_ctx->db, local_abspath,
                               FALSE /* allow_missing */,
                               TRUE /* show_deleted */,
                               FALSE /* show_hidden */,
                               scratch_pool));

  eb.anchor_abspath = local_abspath;

  if (root_relpath)
    {
      svn_boolean_t is_wcroot;

      SVN_ERR(svn_wc__db_is_wcroot(&is_wcroot,
                                   wc_ctx->db, local_abspath, scratch_pool));

      if (!is_wcroot)
        eb.anchor_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
    }
  else if (kind != svn_node_dir)
    eb.anchor_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  if (root_relpath)
    *root_relpath = apr_pstrdup(result_pool,
                                svn_dirent_skip_ancestor(eb.anchor_abspath,
                                                         local_abspath));
  if (root_is_dir)
    *root_is_dir = (kind == svn_node_dir);

  /* Apply changelist filtering to the output */
  if (changelist_filter && changelist_filter->nelts)
    {
      apr_hash_t *changelist_hash;

      SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelist_filter,
                                         result_pool));
      diff_processor = svn_wc__changelist_filter_tree_processor_create(
                         diff_processor, wc_ctx, local_abspath,
                         changelist_hash, result_pool);
    }

  eb.db = wc_ctx->db;
  eb.processor = diff_processor;
  eb.ignore_ancestry = ignore_ancestry;
  eb.pool = scratch_pool;

  if (ignore_ancestry)
    get_all = TRUE; /* We need unmodified descendants of copies */
  else
    get_all = FALSE;

  /* Walk status handles files and directories */
  SVN_ERR(svn_wc__internal_walk_status(wc_ctx->db, local_abspath, depth,
                                       get_all,
                                       TRUE /* no_ignore */,
                                       FALSE /* ignore_text_mods */,
                                       NULL /* ignore_patterns */,
                                       diff_status_callback, &eb,
                                       cancel_func, cancel_baton,
                                       scratch_pool));

  /* Close the remaining open directories */
  while (eb.cur)
    {
      struct node_state_t *ns = eb.cur;

      if (!ns->skip)
        {
          if (ns->propchanges)
            SVN_ERR(diff_processor->dir_changed(ns->relpath,
                                                ns->left_src,
                                                ns->right_src,
                                                ns->left_props,
                                                ns->right_props,
                                                ns->propchanges,
                                                ns->baton,
                                                diff_processor,
                                                ns->pool));
          else
            SVN_ERR(diff_processor->dir_closed(ns->relpath,
                                               ns->left_src,
                                               ns->right_src,
                                               ns->baton,
                                               diff_processor,
                                               ns->pool));
        }
      eb.cur = ns->parent;
      svn_pool_clear(ns->pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_diff6(svn_wc_context_t *wc_ctx,
             const char *local_abspath,
             const svn_wc_diff_callbacks4_t *callbacks,
             void *callback_baton,
             svn_depth_t depth,
             svn_boolean_t ignore_ancestry,
             svn_boolean_t show_copies_as_adds,
             svn_boolean_t use_git_diff_format,
             const apr_array_header_t *changelist_filter,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             apr_pool_t *scratch_pool)
{
  const svn_diff_tree_processor_t *processor;

  SVN_ERR(svn_wc__wrap_diff_callbacks(&processor,
                                      callbacks, callback_baton, TRUE,
                                      scratch_pool, scratch_pool));

  if (use_git_diff_format)
    show_copies_as_adds = TRUE;
  if (show_copies_as_adds)
    ignore_ancestry = FALSE;

  if (! show_copies_as_adds && !use_git_diff_format)
    processor = svn_diff__tree_processor_copy_as_changed_create(processor,
                                                                scratch_pool);

  return svn_error_trace(svn_wc__diff7(NULL, NULL,
                                       wc_ctx, local_abspath,
                                       depth,
                                       ignore_ancestry,
                                       changelist_filter,
                                       processor,
                                       cancel_func, cancel_baton,
                                       scratch_pool, scratch_pool));
}

