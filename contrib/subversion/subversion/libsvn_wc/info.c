/**
 * @copyright
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
 * @endcopyright
 */

#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_wc.h"

#include "wc.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"



svn_wc_info_t *
svn_wc_info_dup(const svn_wc_info_t *info,
                apr_pool_t *pool)
{
  svn_wc_info_t *new_info = apr_pmemdup(pool, info, sizeof(*new_info));

  if (info->changelist)
    new_info->changelist = apr_pstrdup(pool, info->changelist);
  new_info->checksum = svn_checksum_dup(info->checksum, pool);
  if (info->conflicts)
    {
      int i;

      apr_array_header_t *new_conflicts
        = apr_array_make(pool, info->conflicts->nelts, info->conflicts->elt_size);
      for (i = 0; i < info->conflicts->nelts; i++)
        {
          APR_ARRAY_PUSH(new_conflicts, svn_wc_conflict_description2_t *)
            = svn_wc_conflict_description2_dup(
                APR_ARRAY_IDX(info->conflicts, i,
                              const svn_wc_conflict_description2_t *),
                pool);
        }
      new_info->conflicts = new_conflicts;
    }
  if (info->copyfrom_url)
    new_info->copyfrom_url = apr_pstrdup(pool, info->copyfrom_url);
  if (info->wcroot_abspath)
    new_info->wcroot_abspath = apr_pstrdup(pool, info->wcroot_abspath);
  if (info->moved_from_abspath)
    new_info->moved_from_abspath = apr_pstrdup(pool, info->moved_from_abspath);
  if (info->moved_to_abspath)
    new_info->moved_to_abspath = apr_pstrdup(pool, info->moved_to_abspath);

  return new_info;
}


/* Set *INFO to a new struct, allocated in RESULT_POOL, built from the WC
   metadata of LOCAL_ABSPATH.  Pointer fields are copied by reference, not
   dup'd. */
static svn_error_t *
build_info_for_node(svn_wc__info2_t **info,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     svn_node_kind_t kind,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_wc__info2_t *tmpinfo;
  const char *repos_relpath;
  svn_wc__db_status_t status;
  svn_node_kind_t db_kind;
  const char *original_repos_relpath;
  const char *original_repos_root_url;
  const char *original_uuid;
  svn_revnum_t original_revision;
  svn_wc__db_lock_t *lock;
  svn_boolean_t conflicted;
  svn_boolean_t op_root;
  svn_boolean_t have_base;
  svn_boolean_t have_more_work;
  svn_wc_info_t *wc_info;

  tmpinfo = apr_pcalloc(result_pool, sizeof(*tmpinfo));
  tmpinfo->kind = kind;

  wc_info = apr_pcalloc(result_pool, sizeof(*wc_info));
  tmpinfo->wc_info = wc_info;

  wc_info->copyfrom_rev = SVN_INVALID_REVNUM;

  SVN_ERR(svn_wc__db_read_info(&status, &db_kind, &tmpinfo->rev,
                               &repos_relpath,
                               &tmpinfo->repos_root_URL, &tmpinfo->repos_UUID,
                               &tmpinfo->last_changed_rev,
                               &tmpinfo->last_changed_date,
                               &tmpinfo->last_changed_author,
                               &wc_info->depth, &wc_info->checksum, NULL,
                               &original_repos_relpath,
                               &original_repos_root_url, &original_uuid,
                               &original_revision, &lock,
                               &wc_info->recorded_size,
                               &wc_info->recorded_time,
                               &wc_info->changelist,
                               &conflicted, &op_root, NULL, NULL,
                               &have_base, &have_more_work, NULL,
                               db, local_abspath,
                               result_pool, scratch_pool));

  if (original_repos_root_url != NULL)
    {
      tmpinfo->repos_root_URL = original_repos_root_url;
      tmpinfo->repos_UUID = original_uuid;
    }

  if (status == svn_wc__db_status_added)
    {
      /* ### We should also just be fetching the true BASE revision
         ### here, which means copied items would also not have a
         ### revision to display.  But WC-1 wants to show the revision of
         ### copy targets as the copyfrom-rev.  *sigh* */

      if (original_repos_relpath)
        {
          /* Root or child of copy */
          tmpinfo->rev = original_revision;

          if (op_root)
            {
              svn_error_t *err;
              wc_info->copyfrom_url =
                    svn_path_url_add_component2(tmpinfo->repos_root_URL,
                                                original_repos_relpath,
                                                result_pool);

              wc_info->copyfrom_rev = original_revision;

              err = svn_wc__db_scan_moved(&wc_info->moved_from_abspath,
                                          NULL, NULL, NULL,
                                          db, local_abspath,
                                          result_pool, scratch_pool);

              if (err)
                {
                   if (err->apr_err != SVN_ERR_WC_PATH_UNEXPECTED_STATUS)
                      return svn_error_trace(err);
                   svn_error_clear(err);
                   wc_info->moved_from_abspath = NULL;
                }
            }
        }

      /* ### We should be able to avoid both these calls with the information
         from read_info() in most cases */
      if (! op_root)
        wc_info->schedule = svn_wc_schedule_normal;
      else if (! have_more_work && ! have_base)
        wc_info->schedule = svn_wc_schedule_add;
      else
        {
          svn_wc__db_status_t below_working;
          svn_boolean_t have_work;

          SVN_ERR(svn_wc__db_info_below_working(&have_base, &have_work,
                                                &below_working,
                                                db, local_abspath,
                                                scratch_pool));

          /* If the node is not present or deleted (read: not present
             in working), then the node is not a replacement */
          if (below_working != svn_wc__db_status_not_present
              && below_working != svn_wc__db_status_deleted)
            {
              wc_info->schedule = svn_wc_schedule_replace;
            }
          else
            wc_info->schedule = svn_wc_schedule_add;
        }
      SVN_ERR(svn_wc__db_read_repos_info(NULL, &repos_relpath,
                                         &tmpinfo->repos_root_URL,
                                         &tmpinfo->repos_UUID,
                                         db, local_abspath,
                                         result_pool, scratch_pool));

      tmpinfo->URL = svn_path_url_add_component2(tmpinfo->repos_root_URL,
                                                 repos_relpath, result_pool);
    }
  else if (status == svn_wc__db_status_deleted)
    {
      svn_wc__db_status_t w_status;

      SVN_ERR(svn_wc__db_read_pristine_info(&w_status, &tmpinfo->kind,
                                            &tmpinfo->last_changed_rev,
                                            &tmpinfo->last_changed_date,
                                            &tmpinfo->last_changed_author,
                                            &wc_info->depth,
                                            &wc_info->checksum,
                                            NULL, NULL, NULL,
                                            db, local_abspath,
                                            result_pool, scratch_pool));

      if (w_status == svn_wc__db_status_deleted)
        {
          /* We have a working not-present status. We don't know anything
             about this node, but it *is visible* in STATUS.

             Let's tell that it is excluded */

          wc_info->depth = svn_depth_exclude;
          tmpinfo->kind = svn_node_unknown;
        }

      /* And now fetch the url and revision of what will be deleted */
      SVN_ERR(svn_wc__db_scan_deletion(NULL, &wc_info->moved_to_abspath,
                                       NULL, NULL,
                                       db, local_abspath,
                                       scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__db_read_repos_info(&tmpinfo->rev, &repos_relpath,
                                         &tmpinfo->repos_root_URL,
                                         &tmpinfo->repos_UUID,
                                         db, local_abspath,
                                         result_pool, scratch_pool));

      wc_info->schedule = svn_wc_schedule_delete;
      tmpinfo->URL = svn_path_url_add_component2(tmpinfo->repos_root_URL,
                                                 repos_relpath, result_pool);
    }
  else if (status == svn_wc__db_status_not_present
           || status == svn_wc__db_status_server_excluded)
    {
      *info = NULL;
      return SVN_NO_ERROR;
    }
  else if (status == svn_wc__db_status_excluded && !repos_relpath)
    {
      /* We have a WORKING exclude. Avoid segfault on no repos info */

      SVN_ERR(svn_wc__db_read_repos_info(NULL, &repos_relpath,
                                         &tmpinfo->repos_root_URL,
                                         &tmpinfo->repos_UUID,
                                         db, local_abspath,
                                         result_pool, scratch_pool));

      wc_info->schedule = svn_wc_schedule_normal;
      tmpinfo->URL = svn_path_url_add_component2(tmpinfo->repos_root_URL,
                                                 repos_relpath, result_pool);
      tmpinfo->wc_info->depth = svn_depth_exclude;
    }
  else
    {
      /* Just a BASE node. We have all the info we need */
      tmpinfo->URL = svn_path_url_add_component2(tmpinfo->repos_root_URL,
                                                 repos_relpath,
                                                 result_pool);
      wc_info->schedule = svn_wc_schedule_normal;

      if (status == svn_wc__db_status_excluded)
        wc_info->depth = svn_depth_exclude;
    }

  /* A default */
  tmpinfo->size = SVN_INVALID_FILESIZE;

  SVN_ERR(svn_wc__db_get_wcroot(&tmpinfo->wc_info->wcroot_abspath, db,
                                local_abspath, result_pool, scratch_pool));

  if (conflicted)
    SVN_ERR(svn_wc__read_conflicts(&wc_info->conflicts, NULL,
                                   db, local_abspath,
                                   FALSE /* create tempfiles */,
                                   FALSE /* only tree conflicts */,
                                   result_pool, scratch_pool));
  else
    wc_info->conflicts = NULL;

  /* lock stuff */
  if (lock != NULL)
    {
      tmpinfo->lock = apr_pcalloc(result_pool, sizeof(*(tmpinfo->lock)));
      tmpinfo->lock->token         = lock->token;
      tmpinfo->lock->owner         = lock->owner;
      tmpinfo->lock->comment       = lock->comment;
      tmpinfo->lock->creation_date = lock->date;
    }

  *info = tmpinfo;
  return SVN_NO_ERROR;
}


/* Set *INFO to a new struct with minimal content, to be
   used in reporting info for unversioned tree conflict victims. */
/* ### Some fields we could fill out based on the parent dir's entry
       or by looking at an obstructing item. */
static svn_error_t *
build_info_for_unversioned(svn_wc__info2_t **info,
                           apr_pool_t *pool)
{
  svn_wc__info2_t *tmpinfo = apr_pcalloc(pool, sizeof(*tmpinfo));
  svn_wc_info_t *wc_info = apr_pcalloc(pool, sizeof (*wc_info));

  tmpinfo->URL                  = NULL;
  tmpinfo->repos_UUID           = NULL;
  tmpinfo->repos_root_URL       = NULL;
  tmpinfo->rev                  = SVN_INVALID_REVNUM;
  tmpinfo->kind                 = svn_node_none;
  tmpinfo->size                 = SVN_INVALID_FILESIZE;
  tmpinfo->last_changed_rev     = SVN_INVALID_REVNUM;
  tmpinfo->last_changed_date    = 0;
  tmpinfo->last_changed_author  = NULL;
  tmpinfo->lock                 = NULL;

  tmpinfo->wc_info = wc_info;

  wc_info->copyfrom_rev = SVN_INVALID_REVNUM;
  wc_info->depth = svn_depth_unknown;
  wc_info->recorded_size = SVN_INVALID_FILESIZE;

  *info = tmpinfo;
  return SVN_NO_ERROR;
}

/* Callback and baton for crawl_entries() walk over entries files. */
struct found_entry_baton
{
  svn_wc__info_receiver2_t receiver;
  void *receiver_baton;
  svn_wc__db_t *db;
  svn_boolean_t actual_only;
  svn_boolean_t first;
  /* The set of tree conflicts that have been found but not (yet) visited by
   * the tree walker.  Map of abspath -> empty string. */
  apr_hash_t *tree_conflicts;
  apr_pool_t *pool;
};

/* Call WALK_BATON->receiver with WALK_BATON->receiver_baton, passing to it
 * info about the path LOCAL_ABSPATH.
 * An svn_wc__node_found_func_t callback function. */
static svn_error_t *
info_found_node_callback(const char *local_abspath,
                         svn_node_kind_t kind,
                         void *walk_baton,
                         apr_pool_t *scratch_pool)
{
  struct found_entry_baton *fe_baton = walk_baton;
  svn_wc__info2_t *info;

  SVN_ERR(build_info_for_node(&info, fe_baton->db, local_abspath,
                               kind, scratch_pool, scratch_pool));

  if (info == NULL)
    {
      if (!fe_baton->first)
        return SVN_NO_ERROR; /* not present or server excluded descendant */

      /* If the info root is not found, that is an error */
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("The node '%s' was not found."),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  fe_baton->first = FALSE;

  SVN_ERR_ASSERT(info->wc_info != NULL);
  SVN_ERR(fe_baton->receiver(fe_baton->receiver_baton, local_abspath,
                             info, scratch_pool));

  /* If this node is a versioned directory, make a note of any tree conflicts
   * on all immediate children.  Some of these may be visited later in this
   * walk, at which point they will be removed from the list, while any that
   * are not visited will remain in the list. */
  if (fe_baton->actual_only && kind == svn_node_dir)
    {
      const apr_array_header_t *victims;
      int i;

      SVN_ERR(svn_wc__db_read_conflict_victims(&victims,
                                               fe_baton->db, local_abspath,
                                               scratch_pool, scratch_pool));

      for (i = 0; i < victims->nelts; i++)
        {
          const char *this_basename = APR_ARRAY_IDX(victims, i, const char *);

          svn_hash_sets(fe_baton->tree_conflicts,
                        svn_dirent_join(local_abspath, this_basename,
                                        fe_baton->pool),
                        "");
        }
    }

  /* Delete this path which we are currently visiting from the list of tree
   * conflicts.  This relies on the walker visiting a directory before visiting
   * its children. */
  svn_hash_sets(fe_baton->tree_conflicts, local_abspath, NULL);

  return SVN_NO_ERROR;
}


/* Return TRUE iff the subtree at ROOT_ABSPATH, restricted to depth DEPTH,
 * would include the path CHILD_ABSPATH of kind CHILD_KIND. */
static svn_boolean_t
depth_includes(const char *root_abspath,
               svn_depth_t depth,
               const char *child_abspath,
               svn_node_kind_t child_kind,
               apr_pool_t *scratch_pool)
{
  const char *parent_abspath = svn_dirent_dirname(child_abspath, scratch_pool);

  return (depth == svn_depth_infinity
          || ((depth == svn_depth_immediates
               || (depth == svn_depth_files && child_kind == svn_node_file))
              && strcmp(root_abspath, parent_abspath) == 0)
          || strcmp(root_abspath, child_abspath) == 0);
}


svn_error_t *
svn_wc__get_info(svn_wc_context_t *wc_ctx,
                 const char *local_abspath,
                 svn_depth_t depth,
                 svn_boolean_t fetch_excluded,
                 svn_boolean_t fetch_actual_only,
                 const apr_array_header_t *changelist_filter,
                 svn_wc__info_receiver2_t receiver,
                 void *receiver_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  struct found_entry_baton fe_baton;
  svn_error_t *err;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;
  const char *repos_root_url = NULL;
  const char *repos_uuid = NULL;

  fe_baton.receiver = receiver;
  fe_baton.receiver_baton = receiver_baton;
  fe_baton.db = wc_ctx->db;
  fe_baton.actual_only = fetch_actual_only;
  fe_baton.first = TRUE;
  fe_baton.tree_conflicts = apr_hash_make(scratch_pool);
  fe_baton.pool = scratch_pool;

  err = svn_wc__internal_walk_children(wc_ctx->db, local_abspath,
                                       fetch_excluded,
                                       changelist_filter,
                                       info_found_node_callback,
                                       &fe_baton, depth,
                                       cancel_func, cancel_baton,
                                       iterpool);

  /* If the target root node is not present, svn_wc__internal_walk_children()
     returns a PATH_NOT_FOUND error and doesn't call the callback.  If there
     is a tree conflict on this node, that is not an error. */
  if (fe_baton.first /* not visited by walk_children */
      && fetch_actual_only
      && err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_boolean_t tree_conflicted;
      svn_error_t *err2;

      err2 = svn_wc__internal_conflicted_p(NULL, NULL, &tree_conflicted,
                                           wc_ctx->db, local_abspath,
                                           iterpool);

      if ((err2 && err2->apr_err == SVN_ERR_WC_PATH_NOT_FOUND))
        {
          svn_error_clear(err2);
          return svn_error_trace(err);
        }
      else if (err2 || !tree_conflicted)
        return svn_error_compose_create(err, err2);

      svn_error_clear(err);

      svn_hash_sets(fe_baton.tree_conflicts, local_abspath, "");
    }
  else
    SVN_ERR(err);

  /* If there are any tree conflicts that we have found but have not reported,
   * send a minimal info struct for each one now. */
  for (hi = apr_hash_first(scratch_pool, fe_baton.tree_conflicts); hi;
       hi = apr_hash_next(hi))
    {
      const char *this_abspath = apr_hash_this_key(hi);
      const svn_wc_conflict_description2_t *tree_conflict;
      svn_wc__info2_t *info;
      const apr_array_header_t *conflicts;

      svn_pool_clear(iterpool);

      SVN_ERR(build_info_for_unversioned(&info, iterpool));

      if (!repos_root_url)
        {
          SVN_ERR(svn_wc__db_read_repos_info(NULL, NULL,
                                             &repos_root_url,
                                             &repos_uuid,
                                             wc_ctx->db,
                                             svn_dirent_dirname(
                                                            this_abspath,
                                                            iterpool),
                                             scratch_pool, iterpool));
        }

      info->repos_root_URL = repos_root_url;
      info->repos_UUID = repos_uuid;

      SVN_ERR(svn_wc__read_conflicts(&conflicts, NULL,
                                     wc_ctx->db, this_abspath,
                                     FALSE /* create tempfiles */,
                                     FALSE /* only tree conflicts */,
                                     iterpool, iterpool));
      if (! conflicts || ! conflicts->nelts)
        continue;

      tree_conflict = APR_ARRAY_IDX(conflicts, 0,
                                    const svn_wc_conflict_description2_t *);

      if (!depth_includes(local_abspath, depth, tree_conflict->local_abspath,
                          tree_conflict->node_kind, iterpool))
        continue;

      info->wc_info->conflicts = conflicts;
      SVN_ERR(receiver(receiver_baton, this_abspath, info, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
