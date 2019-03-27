/*
 * node.c:  routines for getting information about nodes in the working copy.
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

/* A note about these functions:

   We aren't really sure yet which bits of data libsvn_client needs about
   nodes.  In wc-1, we just grab the entry, and then use whatever we want
   from it.  Such a pattern is Bad.

   This file is intended to hold functions which retrieve specific bits of
   information about a node, and will hopefully give us a better idea about
   what data libsvn_client needs, and how to best provide that data in 1.7
   final.  As such, these functions should only be called from outside
   libsvn_wc; any internal callers are encouraged to use the appropriate
   information fetching function, such as svn_wc__db_read_info().
*/

#include <apr_pools.h>
#include <apr_time.h>

#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_types.h"

#include "wc.h"
#include "props.h"
#include "entries.h"
#include "wc_db.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/* Set *CHILDREN_ABSPATHS to a new array of the full paths formed by joining
 * each name in REL_CHILDREN onto DIR_ABSPATH.
 *
 * Allocate the output array and its elements in RESULT_POOL. */
static void
make_absolute(const apr_array_header_t **children_abspaths,
              const char *dir_abspath,
              const apr_array_header_t *rel_children,
              apr_pool_t *result_pool)
{
  apr_array_header_t *children;
  int i;

  children = apr_array_make(result_pool, rel_children->nelts,
                            sizeof(const char *));
  for (i = 0; i < rel_children->nelts; i++)
    {
      const char *name = APR_ARRAY_IDX(rel_children, i, const char *);
      APR_ARRAY_PUSH(children, const char *) =
                        svn_dirent_join(dir_abspath, name,
                                        result_pool);
    }

  *children_abspaths = children;
}


svn_error_t *
svn_wc__node_get_children_of_working_node(const apr_array_header_t **children,
                                          svn_wc_context_t *wc_ctx,
                                          const char *dir_abspath,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool)
{
  const apr_array_header_t *child_names;

  SVN_ERR(svn_wc__db_read_children_of_working_node(&child_names,
                                                   wc_ctx->db, dir_abspath,
                                                   scratch_pool, scratch_pool));
  make_absolute(children, dir_abspath, child_names, result_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__node_get_not_present_children(const apr_array_header_t **children,
                                      svn_wc_context_t *wc_ctx,
                                      const char *dir_abspath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  const apr_array_header_t *child_names;

  SVN_ERR(svn_wc__db_base_read_not_present_children(
                                   &child_names,
                                   wc_ctx->db, dir_abspath,
                                   scratch_pool, scratch_pool));
  make_absolute(children, dir_abspath, child_names, result_pool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__node_get_repos_info(svn_revnum_t *revision,
                            const char **repos_relpath,
                            const char **repos_root_url,
                            const char **repos_uuid,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  return svn_error_trace(
            svn_wc__db_read_repos_info(revision,
                                       repos_relpath,
                                       repos_root_url,
                                       repos_uuid,
                                       wc_ctx->db, local_abspath,
                                       result_pool, scratch_pool));
}

/* Convert DB_KIND into the appropriate NODE_KIND value.
 * If SHOW_HIDDEN is TRUE, report the node kind as found in the DB
 * even if DB_STATUS indicates that the node is hidden.
 * Else, return svn_node_none for such nodes.
 *
 * ### This is a bit ugly. We should consider promoting svn_kind_t
 * ### to the de-facto node kind type instead of converting between them
 * ### in non-backwards compat code.
 * ### See also comments at the definition of svn_kind_t.
 *
 * ### In reality, the previous comment is out of date, as there is
 * ### now only one enumeration for node kinds, and that is
 * ### svn_node_kind_t (svn_kind_t was merged with that). But it's
 * ### still ugly.
 */
static svn_error_t *
convert_db_kind_to_node_kind(svn_node_kind_t *node_kind,
                             svn_node_kind_t db_kind,
                             svn_wc__db_status_t db_status,
                             svn_boolean_t show_hidden)
{
  *node_kind = db_kind;

  /* Make sure hidden nodes return svn_node_none. */
  if (! show_hidden)
    switch (db_status)
      {
        case svn_wc__db_status_not_present:
        case svn_wc__db_status_server_excluded:
        case svn_wc__db_status_excluded:
          *node_kind = svn_node_none;

        default:
          break;
      }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_read_kind2(svn_node_kind_t *kind,
                  svn_wc_context_t *wc_ctx,
                  const char *local_abspath,
                  svn_boolean_t show_deleted,
                  svn_boolean_t show_hidden,
                  apr_pool_t *scratch_pool)
{
  svn_node_kind_t db_kind;

  SVN_ERR(svn_wc__db_read_kind(&db_kind,
                               wc_ctx->db, local_abspath,
                               TRUE,
                               show_deleted,
                               show_hidden,
                               scratch_pool));

  if (db_kind == svn_node_dir)
    *kind = svn_node_dir;
  else if (db_kind == svn_node_file || db_kind == svn_node_symlink)
    *kind = svn_node_file;
  else
    *kind = svn_node_none;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__node_get_changed_info(svn_revnum_t *changed_rev,
                              apr_time_t *changed_date,
                              const char **changed_author,
                              svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  return svn_error_trace(
    svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL, changed_rev,
                         changed_date, changed_author, NULL, NULL, NULL,
                         NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                         NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                         wc_ctx->db, local_abspath, result_pool,
                         scratch_pool));
}

svn_error_t *
svn_wc__node_get_url(const char **url,
                     svn_wc_context_t *wc_ctx,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  const char *repos_root_url;
  const char *repos_relpath;

  SVN_ERR(svn_wc__db_read_repos_info(NULL, &repos_relpath, &repos_root_url,
                                     NULL,
                                     wc_ctx->db, local_abspath,
                                     scratch_pool, scratch_pool));

  *url = svn_path_url_add_component2(repos_root_url, repos_relpath,
                                     result_pool);

  return SVN_NO_ERROR;
}

/* A recursive node-walker, helper for svn_wc__internal_walk_children().
 *
 * Call WALK_CALLBACK with WALK_BATON on all children (recursively) of
 * DIR_ABSPATH in DB, but not on DIR_ABSPATH itself. DIR_ABSPATH must be a
 * versioned directory. If SHOW_HIDDEN is true, visit hidden nodes, else
 * ignore them. Restrict the depth of the walk to DEPTH.
 *
 * ### Is it possible for a subdirectory to be hidden and known to be a
 *     directory?  If so, and if show_hidden is true, this will try to
 *     recurse into it.  */
static svn_error_t *
walker_helper(svn_wc__db_t *db,
              const char *dir_abspath,
              svn_boolean_t show_hidden,
              const apr_hash_t *changelist_filter,
              svn_wc__node_found_func_t walk_callback,
              void *walk_baton,
              svn_depth_t depth,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  const apr_array_header_t *items;
  int i;

  if (depth == svn_depth_empty)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_wc__db_read_children_walker_info(&items, db,
                                               dir_abspath, scratch_pool,
                                               iterpool));

  for (i = 0; i < items->nelts; i++)
    {
      struct svn_wc__db_walker_info_t *wi =
              APR_ARRAY_IDX(items, i, struct svn_wc__db_walker_info_t *);
      const char *child_name = wi->name;
      svn_node_kind_t child_kind = wi->kind;
      svn_wc__db_status_t child_status = wi->status;
      const char *child_abspath;

      svn_pool_clear(iterpool);

      /* See if someone wants to cancel this operation. */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      child_abspath = svn_dirent_join(dir_abspath, child_name, iterpool);

      if (!show_hidden)
        switch (child_status)
          {
            case svn_wc__db_status_not_present:
            case svn_wc__db_status_server_excluded:
            case svn_wc__db_status_excluded:
              continue;
            default:
              break;
          }

      /* Return the child, if appropriate. */
      if ( (child_kind == svn_node_file
             || depth >= svn_depth_immediates)
           && svn_wc__internal_changelist_match(db, child_abspath,
                                                changelist_filter,
                                                scratch_pool) )
        {
          svn_node_kind_t kind;

          SVN_ERR(convert_db_kind_to_node_kind(&kind, child_kind,
                                               child_status, show_hidden));
          /* ### We might want to pass child_status as well because at least
           * ### one callee is asking for it.
           * ### But is it OK to use an svn_wc__db type in this API?
           * ###    Not yet, we need to get the node walker
           * ###    libsvn_wc-internal first. -hkw */
          SVN_ERR(walk_callback(child_abspath, kind, walk_baton, iterpool));
        }

      /* Recurse into this directory, if appropriate. */
      if (child_kind == svn_node_dir
            && depth >= svn_depth_immediates)
        {
          svn_depth_t depth_below_here = depth;

          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          SVN_ERR(walker_helper(db, child_abspath, show_hidden,
                                changelist_filter,
                                walk_callback, walk_baton,
                                depth_below_here,
                                cancel_func, cancel_baton,
                                iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__internal_walk_children(svn_wc__db_t *db,
                               const char *local_abspath,
                               svn_boolean_t show_hidden,
                               const apr_array_header_t *changelist_filter,
                               svn_wc__node_found_func_t walk_callback,
                               void *walk_baton,
                               svn_depth_t walk_depth,
                               svn_cancel_func_t cancel_func,
                               void *cancel_baton,
                               apr_pool_t *scratch_pool)
{
  svn_node_kind_t db_kind;
  svn_node_kind_t kind;
  svn_wc__db_status_t status;
  apr_hash_t *changelist_hash = NULL;
  const char *changelist = NULL;

  SVN_ERR_ASSERT(walk_depth >= svn_depth_empty
                 && walk_depth <= svn_depth_infinity);

  if (changelist_filter && changelist_filter->nelts)
    SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelist_filter,
                                       scratch_pool));

  /* Check if the node exists before the first callback */
  SVN_ERR(svn_wc__db_read_info(&status, &db_kind, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               changelist_hash ? &changelist : NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               db, local_abspath, scratch_pool, scratch_pool));

  SVN_ERR(convert_db_kind_to_node_kind(&kind, db_kind, status, show_hidden));

  if (!changelist_hash
      || (changelist && svn_hash_gets(changelist_hash, changelist)))
    {
      SVN_ERR(walk_callback(local_abspath, kind, walk_baton, scratch_pool));
    }

  if (db_kind == svn_node_file
      || status == svn_wc__db_status_not_present
      || status == svn_wc__db_status_excluded
      || status == svn_wc__db_status_server_excluded)
    return SVN_NO_ERROR;

  if (db_kind == svn_node_dir)
    {
      return svn_error_trace(
        walker_helper(db, local_abspath, show_hidden, changelist_hash,
                      walk_callback, walk_baton,
                      walk_depth, cancel_func, cancel_baton, scratch_pool));
    }

  return svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                           _("'%s' has an unrecognized node kind"),
                           svn_dirent_local_style(local_abspath,
                                                  scratch_pool));
}

svn_error_t *
svn_wc__node_is_not_present(svn_boolean_t *is_not_present,
                            svn_boolean_t *is_excluded,
                            svn_boolean_t *is_server_excluded,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            svn_boolean_t base_only,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;

  if (base_only)
    {
      SVN_ERR(svn_wc__db_base_get_info(&status,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL,
                                       wc_ctx->db, local_abspath,
                                       scratch_pool, scratch_pool));
    }
  else
    {
      SVN_ERR(svn_wc__db_read_info(&status,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL,
                                   wc_ctx->db, local_abspath,
                                   scratch_pool, scratch_pool));
    }

  if (is_not_present)
    *is_not_present = (status == svn_wc__db_status_not_present);

  if (is_excluded)
    *is_excluded = (status == svn_wc__db_status_excluded);

  if (is_server_excluded)
    *is_server_excluded = (status == svn_wc__db_status_server_excluded);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__node_is_added(svn_boolean_t *is_added,
                      svn_wc_context_t *wc_ctx,
                      const char *local_abspath,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;

  SVN_ERR(svn_wc__db_read_info(&status,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               wc_ctx->db, local_abspath,
                               scratch_pool, scratch_pool));
  *is_added = (status == svn_wc__db_status_added);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__node_has_working(svn_boolean_t *has_working,
                         svn_wc_context_t *wc_ctx,
                         const char *local_abspath,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;

  SVN_ERR(svn_wc__db_read_info(&status,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, has_working,
                               wc_ctx->db, local_abspath,
                               scratch_pool, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__node_get_base(svn_node_kind_t *kind,
                      svn_revnum_t *revision,
                      const char **repos_relpath,
                      const char **repos_root_url,
                      const char **repos_uuid,
                      const char **lock_token,
                      svn_wc_context_t *wc_ctx,
                      const char *local_abspath,
                      svn_boolean_t ignore_enoent,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_wc__db_status_t status;
  svn_wc__db_lock_t *lock;
  svn_node_kind_t db_kind;

  err = svn_wc__db_base_get_info(&status, &db_kind, revision, repos_relpath,
                                 repos_root_url, repos_uuid, NULL,
                                 NULL, NULL, NULL, NULL, NULL,
                                 lock_token ? &lock : NULL,
                                 NULL, NULL, NULL,
                                 wc_ctx->db, local_abspath,
                                 result_pool, scratch_pool);

  if (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
    return svn_error_trace(err);
  else if (err
           || (status != svn_wc__db_status_normal
               && status != svn_wc__db_status_incomplete))
    {
      if (!ignore_enoent)
        {
          if (err)
            return svn_error_trace(err);
          else
            return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                     _("The node '%s' was not found."),
                                     svn_dirent_local_style(local_abspath,
                                                            scratch_pool));
        }
      svn_error_clear(err);

      if (kind)
        *kind = svn_node_unknown;
      if (revision)
        *revision = SVN_INVALID_REVNUM;
      if (repos_relpath)
        *repos_relpath = NULL;
      if (repos_root_url)
        *repos_root_url = NULL;
      if (repos_uuid)
        *repos_uuid = NULL;
      if (lock_token)
        *lock_token = NULL;
      return SVN_NO_ERROR;
    }

  if (kind)
    *kind = db_kind;
  if (lock_token)
    *lock_token = lock ? lock->token : NULL;

  SVN_ERR_ASSERT(!revision || SVN_IS_VALID_REVNUM(*revision));
  SVN_ERR_ASSERT(!repos_relpath || *repos_relpath);
  SVN_ERR_ASSERT(!repos_root_url || *repos_root_url);
  SVN_ERR_ASSERT(!repos_uuid || *repos_uuid);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__node_get_pre_ng_status_data(svn_revnum_t *revision,
                                   svn_revnum_t *changed_rev,
                                   apr_time_t *changed_date,
                                   const char **changed_author,
                                   svn_wc_context_t *wc_ctx,
                                   const char *local_abspath,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_boolean_t have_base, have_more_work, have_work;

  SVN_ERR(svn_wc__db_read_info(&status, NULL, revision, NULL, NULL, NULL,
                               changed_rev, changed_date, changed_author,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               &have_base, &have_more_work, &have_work,
                               wc_ctx->db, local_abspath,
                               result_pool, scratch_pool));

  if (!have_work
      || ((!changed_rev || SVN_IS_VALID_REVNUM(*changed_rev))
          && (!revision || SVN_IS_VALID_REVNUM(*revision)))
      || ((status != svn_wc__db_status_added)
          && (status != svn_wc__db_status_deleted)))
    {
      return SVN_NO_ERROR; /* We got everything we need */
    }

  if (have_base && !have_more_work)
    SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, revision, NULL, NULL, NULL,
                                     changed_rev, changed_date, changed_author,
                                     NULL, NULL, NULL,
                                     NULL, NULL, NULL, NULL,
                                     wc_ctx->db, local_abspath,
                                     result_pool, scratch_pool));
  else if (status == svn_wc__db_status_deleted)
    /* Check the information below a WORKING delete */
    SVN_ERR(svn_wc__db_read_pristine_info(NULL, NULL, changed_rev,
                                          changed_date, changed_author, NULL,
                                          NULL, NULL, NULL, NULL,
                                          wc_ctx->db, local_abspath,
                                          result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__node_clear_dav_cache_recursive(svn_wc_context_t *wc_ctx,
                                       const char *local_abspath,
                                       apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__db_base_clear_dav_cache_recursive(
                              wc_ctx->db, local_abspath, scratch_pool));
}


svn_error_t *
svn_wc__node_get_lock_tokens_recursive(apr_hash_t **lock_tokens,
                                       svn_wc_context_t *wc_ctx,
                                       const char *local_abspath,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__db_base_get_lock_tokens_recursive(
                              lock_tokens, wc_ctx->db, local_abspath,
                              result_pool, scratch_pool));
}

svn_error_t *
svn_wc__get_excluded_subtrees(apr_hash_t **server_excluded_subtrees,
                              svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  return svn_error_trace(
           svn_wc__db_get_excluded_subtrees(server_excluded_subtrees,
                                            wc_ctx->db,
                                            local_abspath,
                                            result_pool,
                                            scratch_pool));
}

svn_error_t *
svn_wc__internal_get_origin(svn_boolean_t *is_copy,
                            svn_revnum_t *revision,
                            const char **repos_relpath,
                            const char **repos_root_url,
                            const char **repos_uuid,
                            svn_depth_t *depth,
                            const char **copy_root_abspath,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            svn_boolean_t scan_deleted,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  const char *original_repos_relpath;
  const char *original_repos_root_url;
  const char *original_repos_uuid;
  svn_revnum_t original_revision;
  svn_wc__db_status_t status;
  svn_boolean_t have_more_work;
  svn_boolean_t op_root;

  const char *tmp_repos_relpath;

  if (copy_root_abspath)
    *copy_root_abspath = NULL;
  if (!repos_relpath)
    repos_relpath = &tmp_repos_relpath;

  SVN_ERR(svn_wc__db_read_info(&status, NULL, revision, repos_relpath,
                               repos_root_url, repos_uuid, NULL, NULL, NULL,
                               depth, NULL, NULL,
                               &original_repos_relpath,
                               &original_repos_root_url,
                               &original_repos_uuid, &original_revision,
                               NULL, NULL, NULL, NULL, NULL, &op_root, NULL,
                               NULL, NULL, &have_more_work, is_copy,
                               db, local_abspath, result_pool, scratch_pool));

  if (*repos_relpath)
    {
      return SVN_NO_ERROR; /* Returned BASE information */
    }

  if (status == svn_wc__db_status_deleted && !scan_deleted)
    {
      if (is_copy)
        *is_copy = FALSE; /* Deletes are stored in working; default to FALSE */

      return SVN_NO_ERROR; /* No info */
    }

  if (original_repos_relpath)
    {
      /* We an have a copy */
      *repos_relpath = original_repos_relpath;
      if (revision)
        *revision = original_revision;
      if (repos_root_url)
        *repos_root_url = original_repos_root_url;
      if (repos_uuid)
        *repos_uuid = original_repos_uuid;

      if (copy_root_abspath == NULL)
        return SVN_NO_ERROR;
      else if (op_root)
        {
          *copy_root_abspath = apr_pstrdup(result_pool, local_abspath);
          return SVN_NO_ERROR;
        }
    }

  {
    svn_boolean_t scan_working = FALSE;

    if (status == svn_wc__db_status_added
        || (status == svn_wc__db_status_deleted && have_more_work))
      scan_working = TRUE;

    if (scan_working)
      {
        const char *op_root_abspath;

        SVN_ERR(svn_wc__db_scan_addition(&status, &op_root_abspath, NULL,
                                         NULL, NULL, &original_repos_relpath,
                                         repos_root_url,
                                         repos_uuid, revision,
                                         db, local_abspath,
                                         result_pool, scratch_pool));

        if (status == svn_wc__db_status_added)
          {
            if (is_copy)
              *is_copy = FALSE;
            return SVN_NO_ERROR; /* Local addition */
          }

        /* We don't know how the following error condition can be fulfilled
         * but we have seen that happening in the wild.  Better to create
         * an error than a SEGFAULT. */
        if (status == svn_wc__db_status_incomplete && !original_repos_relpath)
          return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                               _("Incomplete copy information on path '%s'."),
                                   svn_dirent_local_style(local_abspath,
                                                          scratch_pool));

        *repos_relpath = svn_relpath_join(
                                original_repos_relpath,
                                svn_dirent_skip_ancestor(op_root_abspath,
                                                         local_abspath),
                                result_pool);
        if (copy_root_abspath)
          *copy_root_abspath = op_root_abspath;
      }
    else /* Deleted, excluded, not-present, server-excluded, ... */
      {
        if (is_copy)
          *is_copy = FALSE;

        SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, revision, repos_relpath,
                                         repos_root_url, repos_uuid, NULL,
                                         NULL, NULL, NULL, NULL, NULL, NULL,
                                         NULL, NULL, NULL,
                                         db, local_abspath,
                                         result_pool, scratch_pool));
      }

    return SVN_NO_ERROR;
  }
}

svn_error_t *
svn_wc__node_get_origin(svn_boolean_t *is_copy,
                        svn_revnum_t *revision,
                        const char **repos_relpath,
                        const char **repos_root_url,
                        const char **repos_uuid,
                        svn_depth_t *depth,
                        const char **copy_root_abspath,
                        svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        svn_boolean_t scan_deleted,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__internal_get_origin(is_copy, revision,
                           repos_relpath, repos_root_url, repos_uuid,
                           depth, copy_root_abspath,
                           wc_ctx->db, local_abspath, scan_deleted,
                           result_pool, scratch_pool));
}

svn_error_t *
svn_wc__node_get_commit_status(svn_boolean_t *added,
                               svn_boolean_t *deleted,
                               svn_boolean_t *is_replace_root,
                               svn_boolean_t *is_op_root,
                               svn_revnum_t *revision,
                               svn_revnum_t *original_revision,
                               const char **original_repos_relpath,
                               svn_wc_context_t *wc_ctx,
                               const char *local_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_boolean_t have_base;
  svn_boolean_t have_more_work;
  svn_boolean_t op_root;

  /* ### All of this should be handled inside a single read transaction */
  SVN_ERR(svn_wc__db_read_info(&status, NULL, revision, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               original_repos_relpath, NULL, NULL,
                               original_revision, NULL, NULL, NULL,
                               NULL, NULL,
                               &op_root, NULL, NULL,
                               &have_base, &have_more_work, NULL,
                               wc_ctx->db, local_abspath,
                               result_pool, scratch_pool));

  if (added)
    *added = (status == svn_wc__db_status_added);
  if (deleted)
    *deleted = (status == svn_wc__db_status_deleted);
  if (is_op_root)
    *is_op_root = op_root;

  if (is_replace_root)
    {
      if (status == svn_wc__db_status_added
          && op_root
          && (have_base || have_more_work))
        SVN_ERR(svn_wc__db_node_check_replace(is_replace_root, NULL, NULL,
                                              wc_ctx->db, local_abspath,
                                              scratch_pool));
      else
        *is_replace_root = FALSE;
    }

  /* Retrieve some information from BASE which is needed for replacing
     and/or deleting BASE nodes. */
  if (have_base
      && !have_more_work
      && op_root
      && (revision && !SVN_IS_VALID_REVNUM(*revision)))
    {
      SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, revision, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL,
                                       wc_ctx->db, local_abspath,
                                       scratch_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__node_get_md5_from_sha1(const svn_checksum_t **md5_checksum,
                               svn_wc_context_t *wc_ctx,
                               const char *wri_abspath,
                               const svn_checksum_t *sha1_checksum,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__db_pristine_get_md5(md5_checksum,
                                                     wc_ctx->db,
                                                     wri_abspath,
                                                     sha1_checksum,
                                                     result_pool,
                                                     scratch_pool));
}

svn_error_t *
svn_wc__get_not_present_descendants(const apr_array_header_t **descendants,
                                    svn_wc_context_t *wc_ctx,
                                    const char *local_abspath,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool)
{
  return svn_error_trace(
                svn_wc__db_get_not_present_descendants(descendants,
                                                       wc_ctx->db,
                                                       local_abspath,
                                                       result_pool,
                                                       scratch_pool));
}

svn_error_t *
svn_wc__rename_wc(svn_wc_context_t *wc_ctx,
                  const char *from_abspath,
                  const char *dst_abspath,
                  apr_pool_t *scratch_pool)
{
  const char *wcroot_abspath;
  SVN_ERR(svn_wc__db_get_wcroot(&wcroot_abspath, wc_ctx->db, from_abspath,
                                scratch_pool, scratch_pool));

  if (! strcmp(from_abspath, wcroot_abspath))
    {
      SVN_ERR(svn_wc__db_drop_root(wc_ctx->db, wcroot_abspath, scratch_pool));

      SVN_ERR(svn_io_file_rename2(from_abspath, dst_abspath, FALSE,
                                  scratch_pool));
    }
  else
    return svn_error_createf(
                    SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                    _("'%s' is not the root of the working copy '%s'"),
                    svn_dirent_local_style(from_abspath, scratch_pool),
                    svn_dirent_local_style(wcroot_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__check_for_obstructions(svn_wc_notify_state_t *obstruction_state,
                               svn_node_kind_t *kind,
                               svn_boolean_t *deleted,
                               svn_boolean_t *excluded,
                               svn_depth_t *parent_depth,
                               svn_wc_context_t *wc_ctx,
                               const char *local_abspath,
                               svn_boolean_t no_wcroot_check,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_node_kind_t db_kind;
  svn_node_kind_t disk_kind;
  svn_error_t *err;

  *obstruction_state = svn_wc_notify_state_inapplicable;
  if (kind)
    *kind = svn_node_none;
  if (deleted)
    *deleted = FALSE;
  if (excluded)
    *excluded = FALSE;
  if (parent_depth)
    *parent_depth = svn_depth_unknown;

  SVN_ERR(svn_io_check_path(local_abspath, &disk_kind, scratch_pool));

  err = svn_wc__db_read_info(&status, &db_kind, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL,
                             wc_ctx->db, local_abspath,
                             scratch_pool, scratch_pool);

  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);

      if (disk_kind != svn_node_none)
        {
          /* Nothing in the DB, but something on disk */
          *obstruction_state = svn_wc_notify_state_obstructed;
          return SVN_NO_ERROR;
        }

      err = svn_wc__db_read_info(&status, &db_kind, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, parent_depth, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL,
                                 wc_ctx->db, svn_dirent_dirname(local_abspath,
                                                                scratch_pool),
                                 scratch_pool, scratch_pool);

      if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          /* No versioned parent; we can't add a node here */
          *obstruction_state = svn_wc_notify_state_obstructed;
          return SVN_NO_ERROR;
        }
      else
        SVN_ERR(err);

      if (db_kind != svn_node_dir
          || (status != svn_wc__db_status_normal
              && status != svn_wc__db_status_added))
        {
          /* The parent doesn't allow nodes to be added below it */
          *obstruction_state = svn_wc_notify_state_obstructed;
        }

      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  /* Check for obstructing working copies */
  if (!no_wcroot_check
      && db_kind == svn_node_dir
      && status == svn_wc__db_status_normal)
    {
      svn_boolean_t is_root;
      SVN_ERR(svn_wc__db_is_wcroot(&is_root, wc_ctx->db, local_abspath,
                                   scratch_pool));

      if (is_root)
        {
          /* Callers should handle this as unversioned */
          *obstruction_state = svn_wc_notify_state_obstructed;
          return SVN_NO_ERROR;
        }
    }

  if (kind)
    SVN_ERR(convert_db_kind_to_node_kind(kind, db_kind, status, FALSE));

  switch (status)
    {
      case svn_wc__db_status_deleted:
        if (deleted)
          *deleted = TRUE;
        /* Fall through to svn_wc__db_status_not_present */
      case svn_wc__db_status_not_present:
        if (disk_kind != svn_node_none)
          *obstruction_state = svn_wc_notify_state_obstructed;
        break;

      case svn_wc__db_status_excluded:
      case svn_wc__db_status_server_excluded:
        if (excluded)
          *excluded = TRUE;
        /* fall through */
      case svn_wc__db_status_incomplete:
        *obstruction_state = svn_wc_notify_state_missing;
        break;

      case svn_wc__db_status_added:
      case svn_wc__db_status_normal:
        if (disk_kind == svn_node_none)
          *obstruction_state = svn_wc_notify_state_missing;
        else
          {
            svn_node_kind_t expected_kind;

            SVN_ERR(convert_db_kind_to_node_kind(&expected_kind, db_kind,
                                                 status, FALSE));

            if (disk_kind != expected_kind)
              *obstruction_state = svn_wc_notify_state_obstructed;
          }
        break;
      default:
        SVN_ERR_MALFUNCTION();
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__node_was_moved_away(const char **moved_to_abspath,
                            const char **op_root_abspath,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;

  if (moved_to_abspath)
    *moved_to_abspath = NULL;
  if (op_root_abspath)
    *op_root_abspath = NULL;

  SVN_ERR(svn_wc__db_read_info(&status,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               wc_ctx->db, local_abspath,
                               scratch_pool, scratch_pool));

  if (status == svn_wc__db_status_deleted)
    SVN_ERR(svn_wc__db_scan_deletion(NULL, moved_to_abspath, NULL,
                                     op_root_abspath, wc_ctx->db,
                                     local_abspath,
                                     result_pool, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__node_was_moved_here(const char **moved_from_abspath,
                            const char **delete_op_root_abspath,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  if (moved_from_abspath)
    *moved_from_abspath = NULL;
  if (delete_op_root_abspath)
    *delete_op_root_abspath = NULL;

  err = svn_wc__db_scan_moved(moved_from_abspath, NULL, NULL,
                              delete_op_root_abspath,
                              wc_ctx->db, local_abspath,
                              result_pool, scratch_pool);

  if (err)
    {
      /* Return error for not added nodes */
      if (err->apr_err != SVN_ERR_WC_PATH_UNEXPECTED_STATUS)
        return svn_error_trace(err);

      /* Path not moved here */
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }

  return SVN_NO_ERROR;
}
