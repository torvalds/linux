/*
 * delete.c: Handling of the in-wc side of the delete operation
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



#include <string.h>
#include <stdlib.h>

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_wc.h"
#include "svn_io.h"

#include "wc.h"
#include "adm_files.h"
#include "conflicts.h"
#include "workqueue.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"


/* Remove/erase PATH from the working copy. This involves deleting PATH
 * from the physical filesystem. PATH is assumed to be an unversioned file
 * or directory.
 *
 * If ignore_enoent is TRUE, ignore missing targets.
 *
 * If CANCEL_FUNC is non-null, invoke it with CANCEL_BATON at various
 * points, return any error immediately.
 */
static svn_error_t *
erase_unversioned_from_wc(const char *path,
                          svn_boolean_t ignore_enoent,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  /* Optimize the common case: try to delete the file */
  err = svn_io_remove_file2(path, ignore_enoent, scratch_pool);
  if (err)
    {
      /* Then maybe it was a directory? */
      svn_error_clear(err);

      err = svn_io_remove_dir2(path, ignore_enoent, cancel_func, cancel_baton,
                               scratch_pool);

      if (err)
        {
          /* We're unlikely to end up here. But we need this fallback
             to make sure we report the right error *and* try the
             correct deletion at least once. */
          svn_node_kind_t kind;

          svn_error_clear(err);
          SVN_ERR(svn_io_check_path(path, &kind, scratch_pool));
          if (kind == svn_node_file)
            SVN_ERR(svn_io_remove_file2(path, ignore_enoent, scratch_pool));
          else if (kind == svn_node_dir)
            SVN_ERR(svn_io_remove_dir2(path, ignore_enoent,
                                       cancel_func, cancel_baton,
                                       scratch_pool));
          else if (kind == svn_node_none)
            return svn_error_createf(SVN_ERR_BAD_FILENAME, NULL,
                                     _("'%s' does not exist"),
                                     svn_dirent_local_style(path,
                                                            scratch_pool));
          else
            return svn_error_createf(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                                     _("Unsupported node kind for path '%s'"),
                                     svn_dirent_local_style(path,
                                                            scratch_pool));

        }
    }

  return SVN_NO_ERROR;
}

/* Helper for svn_wc__delete and svn_wc__delete_many */
static svn_error_t *
create_delete_wq_items(svn_skel_t **work_items,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       svn_node_kind_t kind,
                       svn_boolean_t conflicted,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  *work_items = NULL;

  /* Schedule the on-disk delete */
  if (kind == svn_node_dir)
    SVN_ERR(svn_wc__wq_build_dir_remove(work_items, db, local_abspath,
                                        local_abspath,
                                        TRUE /* recursive */,
                                        result_pool, scratch_pool));
  else
    SVN_ERR(svn_wc__wq_build_file_remove(work_items, db, local_abspath,
                                         local_abspath,
                                         result_pool, scratch_pool));

  /* Read conflicts, to allow deleting the markers after updating the DB */
  if (conflicted)
    {
      svn_skel_t *conflict;
      const apr_array_header_t *markers;
      int i;

      SVN_ERR(svn_wc__db_read_conflict(&conflict, NULL, NULL,
                                       db, local_abspath,
                                       scratch_pool, scratch_pool));

      SVN_ERR(svn_wc__conflict_read_markers(&markers, db, local_abspath,
                                            conflict,
                                            scratch_pool, scratch_pool));

      /* Maximum number of markers is 4, so no iterpool */
      for (i = 0; markers && i < markers->nelts; i++)
        {
          const char *marker_abspath;
          svn_node_kind_t marker_kind;

          marker_abspath = APR_ARRAY_IDX(markers, i, const char *);
          SVN_ERR(svn_io_check_path(marker_abspath, &marker_kind,
                                    scratch_pool));

          if (marker_kind == svn_node_file)
            {
              svn_skel_t *work_item;

              SVN_ERR(svn_wc__wq_build_file_remove(&work_item, db,
                                                   local_abspath,
                                                   marker_abspath,
                                                   result_pool,
                                                   scratch_pool));

              *work_items = svn_wc__wq_merge(*work_items, work_item,
                                             result_pool);
            }
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__delete_many(svn_wc_context_t *wc_ctx,
                    const apr_array_header_t *targets,
                    svn_boolean_t keep_local,
                    svn_boolean_t delete_unversioned_target,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    svn_wc_notify_func2_t notify_func,
                    void *notify_baton,
                    apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  svn_error_t *err;
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  svn_skel_t *work_items = NULL;
  apr_array_header_t *versioned_targets;
  const char *local_abspath;
  int i;
  apr_pool_t *iterpool;

  iterpool = svn_pool_create(scratch_pool);
  versioned_targets = apr_array_make(scratch_pool, targets->nelts,
                                     sizeof(const char *));
  for (i = 0; i < targets->nelts; i++)
    {
      svn_boolean_t conflicted = FALSE;
      const char *repos_relpath;

      svn_pool_clear(iterpool);

      local_abspath = APR_ARRAY_IDX(targets, i, const char *);
      err = svn_wc__db_read_info(&status, &kind, NULL, &repos_relpath, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, &conflicted,
                                 NULL, NULL, NULL, NULL, NULL, NULL,
                                 db, local_abspath, iterpool, iterpool);

      if (err)
        {
          if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
            {
              svn_error_clear(err);
              if (delete_unversioned_target && !keep_local)
                SVN_ERR(erase_unversioned_from_wc(local_abspath, FALSE,
                                                  cancel_func, cancel_baton,
                                                  iterpool));
              continue;
            }
         else
          return svn_error_trace(err);
        }

      APR_ARRAY_PUSH(versioned_targets, const char *) = local_abspath;

      switch (status)
        {
          /* svn_wc__db_status_server_excluded handled by
           * svn_wc__db_op_delete_many */
          case svn_wc__db_status_excluded:
          case svn_wc__db_status_not_present:
            return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                     _("'%s' cannot be deleted"),
                                     svn_dirent_local_style(local_abspath,
                                                            iterpool));

          /* Explicitly ignore other statii */
          default:
            break;
        }

      if (status == svn_wc__db_status_normal
          && kind == svn_node_dir)
        {
          svn_boolean_t is_wcroot;
          SVN_ERR(svn_wc__db_is_wcroot(&is_wcroot, db, local_abspath,
                                       iterpool));

          if (is_wcroot)
            return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                                     _("'%s' is the root of a working copy and "
                                       "cannot be deleted"),
                                     svn_dirent_local_style(local_abspath,
                                                            iterpool));
        }
      if (repos_relpath && !repos_relpath[0])
        return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                                     _("'%s' represents the repository root "
                                       "and cannot be deleted"),
                                     svn_dirent_local_style(local_abspath,
                                                            iterpool));

      /* Verify if we have a write lock on the parent of this node as we might
         be changing the childlist of that directory. */
      SVN_ERR(svn_wc__write_check(db, svn_dirent_dirname(local_abspath,
                                                         iterpool),
                                  iterpool));

      /* Prepare the on-disk delete */
      if (!keep_local)
        {
          svn_skel_t *work_item;

          SVN_ERR(create_delete_wq_items(&work_item, db, local_abspath, kind,
                                         conflicted,
                                         scratch_pool, iterpool));

          work_items = svn_wc__wq_merge(work_items, work_item,
                                        scratch_pool);
        }
    }

  if (versioned_targets->nelts == 0)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__db_op_delete_many(db, versioned_targets,
                                    !keep_local /* delete_dir_externals */,
                                    work_items,
                                    cancel_func, cancel_baton,
                                    notify_func, notify_baton,
                                    iterpool));

  if (work_items != NULL)
    {
      /* Our only caller locked the wc, so for now assume it only passed
         nodes from a single wc (asserted in svn_wc__db_op_delete_many) */
      local_abspath = APR_ARRAY_IDX(versioned_targets, 0, const char *);

      SVN_ERR(svn_wc__wq_run(db, local_abspath, cancel_func, cancel_baton,
                             iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_delete4(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_boolean_t keep_local,
               svn_boolean_t delete_unversioned_target,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *pool = scratch_pool;
  svn_wc__db_t *db = wc_ctx->db;
  svn_error_t *err;
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  svn_boolean_t conflicted;
  svn_skel_t *work_items = NULL;
  const char *repos_relpath;

  err = svn_wc__db_read_info(&status, &kind, NULL, &repos_relpath, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, &conflicted,
                             NULL, NULL, NULL, NULL, NULL, NULL,
                             db, local_abspath, pool, pool);

  if (delete_unversioned_target &&
      err != NULL && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);

      if (!keep_local)
        SVN_ERR(erase_unversioned_from_wc(local_abspath, FALSE,
                                          cancel_func, cancel_baton,
                                          pool));
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  switch (status)
    {
      /* svn_wc__db_status_server_excluded handled by svn_wc__db_op_delete */
      case svn_wc__db_status_excluded:
      case svn_wc__db_status_not_present:
        return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("'%s' cannot be deleted"),
                                 svn_dirent_local_style(local_abspath, pool));

      /* Explicitly ignore other statii */
      default:
        break;
    }

  if (status == svn_wc__db_status_normal
      && kind == svn_node_dir)
    {
      svn_boolean_t is_wcroot;
      SVN_ERR(svn_wc__db_is_wcroot(&is_wcroot, db, local_abspath, pool));

      if (is_wcroot)
        return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                                 _("'%s' is the root of a working copy and "
                                   "cannot be deleted"),
                                 svn_dirent_local_style(local_abspath, pool));
    }
  if (repos_relpath && !repos_relpath[0])
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("'%s' represents the repository root "
                               "and cannot be deleted"),
                               svn_dirent_local_style(local_abspath, pool));

  /* Verify if we have a write lock on the parent of this node as we might
     be changing the childlist of that directory. */
  SVN_ERR(svn_wc__write_check(db, svn_dirent_dirname(local_abspath, pool),
                              pool));

  /* Prepare the on-disk delete */
      if (!keep_local)
        {
          SVN_ERR(create_delete_wq_items(&work_items, db, local_abspath, kind,
                                         conflicted,
                                         scratch_pool, scratch_pool));
        }

  SVN_ERR(svn_wc__db_op_delete(db, local_abspath,
                               NULL /*moved_to_abspath */,
                               !keep_local /* delete_dir_externals */,
                               NULL, work_items,
                               cancel_func, cancel_baton,
                               notify_func, notify_baton,
                               pool));

  if (work_items)
    SVN_ERR(svn_wc__wq_run(db, local_abspath, cancel_func, cancel_baton,
                           scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__internal_remove_from_revision_control(svn_wc__db_t *db,
                                              const char *local_abspath,
                                              svn_boolean_t destroy_wf,
                                              svn_cancel_func_t cancel_func,
                                              void *cancel_baton,
                                              apr_pool_t *scratch_pool)
{
  svn_boolean_t left_something = FALSE;
  svn_boolean_t is_root;
  svn_error_t *err = NULL;

  SVN_ERR(svn_wc__db_is_wcroot(&is_root, db, local_abspath, scratch_pool));

  SVN_ERR(svn_wc__write_check(db, is_root ? local_abspath
                                          : svn_dirent_dirname(local_abspath,
                                                               scratch_pool),
                              scratch_pool));

  SVN_ERR(svn_wc__db_op_remove_node(&left_something,
                                    db, local_abspath,
                                    destroy_wf /* destroy_wc */,
                                    destroy_wf /* destroy_changes */,
                                    NULL, NULL,
                                    cancel_func, cancel_baton,
                                    scratch_pool));

  SVN_ERR(svn_wc__wq_run(db, local_abspath,
                         cancel_func, cancel_baton,
                         scratch_pool));

  if (is_root)
    {
      /* Destroy the administrative area */
      SVN_ERR(svn_wc__adm_destroy(db, local_abspath, cancel_func, cancel_baton,
                                  scratch_pool));

      /* And if we didn't leave something interesting, remove the directory */
      if (!left_something && destroy_wf)
        err = svn_io_dir_remove_nonrecursive(local_abspath, scratch_pool);
    }

  if (left_something || err)
    return svn_error_create(SVN_ERR_WC_LEFT_LOCAL_MOD, err, NULL);

  return SVN_NO_ERROR;
}

/* Implements svn_wc_status_func4_t for svn_wc_remove_from_revision_control2 */
static svn_error_t *
remove_from_revision_status_callback(void *baton,
                                     const char *local_abspath,
                                     const svn_wc_status3_t *status,
                                     apr_pool_t *scratch_pool)
{
  /* For legacy reasons we only check the file contents for changes */
  if (status->versioned
      && status->kind == svn_node_file
      && (status->text_status == svn_wc_status_modified
          || status->text_status == svn_wc_status_conflicted))
    {
      return svn_error_createf(SVN_ERR_WC_LEFT_LOCAL_MOD, NULL,
                               _("File '%s' has local modifications"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_remove_from_revision_control2(svn_wc_context_t *wc_ctx,
                                    const char *local_abspath,
                                    svn_boolean_t destroy_wf,
                                    svn_boolean_t instant_error,
                                    svn_cancel_func_t cancel_func,
                                    void *cancel_baton,
                                    apr_pool_t *scratch_pool)
{
  if (instant_error)
    {
      SVN_ERR(svn_wc_walk_status(wc_ctx, local_abspath, svn_depth_infinity,
                                 FALSE, FALSE, FALSE, NULL,
                                 remove_from_revision_status_callback, NULL,
                                 cancel_func, cancel_baton,
                                 scratch_pool));
    }
  return svn_error_trace(
      svn_wc__internal_remove_from_revision_control(wc_ctx->db,
                                                    local_abspath,
                                                    destroy_wf,
                                                    cancel_func,
                                                    cancel_baton,
                                                    scratch_pool));
}

