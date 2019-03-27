/*
 * copy.c:  wc 'copy' functionality.
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
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"

#include "wc.h"
#include "workqueue.h"
#include "props.h"
#include "conflicts.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"

/* #define RECORD_MIXED_MOVE */

/*** Code. ***/

/* Make a copy of the filesystem node (or tree if RECURSIVE) at
   SRC_ABSPATH under a temporary name in the directory
   TMPDIR_ABSPATH and return the absolute path of the copy in
   *DST_ABSPATH.  Return the node kind of SRC_ABSPATH in *KIND.  If
   SRC_ABSPATH doesn't exist then set *DST_ABSPATH to NULL to indicate
   that no copy was made.

   If DIRENT is not NULL, it contains the on-disk information of SRC_ABSPATH.
   RECORDED_SIZE (if not SVN_INVALID_FILESIZE) contains the recorded size of
   SRC_ABSPATH, and RECORDED_TIME the recorded size or 0.

   These values will be used to avoid unneeded work.
 */
static svn_error_t *
copy_to_tmpdir(svn_skel_t **work_item,
               svn_node_kind_t *kind,
               svn_wc__db_t *db,
               const char *src_abspath,
               const char *dst_abspath,
               const char *tmpdir_abspath,
               svn_boolean_t file_copy,
               svn_boolean_t unversioned,
               const svn_io_dirent2_t *dirent,
               svn_filesize_t recorded_size,
               apr_time_t recorded_time,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  svn_boolean_t is_special;
  svn_io_file_del_t delete_when;
  const char *dst_tmp_abspath;
  svn_node_kind_t dsk_kind;
  if (!kind)
    kind = &dsk_kind;

  *work_item = NULL;

  if (dirent)
    {
      *kind = dirent->kind;
      is_special = dirent->special;
    }
  else
    SVN_ERR(svn_io_check_special_path(src_abspath, kind, &is_special,
                                      scratch_pool));
  if (*kind == svn_node_none)
    {
      return SVN_NO_ERROR;
    }
  else if (*kind == svn_node_unknown)
    {
      return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                               _("Source '%s' is unexpected kind"),
                               svn_dirent_local_style(src_abspath,
                                                      scratch_pool));
    }
  else if (*kind == svn_node_dir || is_special)
    delete_when = svn_io_file_del_on_close;
  else /* the default case: (*kind == svn_node_file) */
    delete_when = svn_io_file_del_none;

  /* ### Do we need a pool cleanup to remove the copy?  We can't use
     ### svn_io_file_del_on_pool_cleanup above because a) it won't
     ### handle the directory case and b) we need to be able to remove
     ### the cleanup before queueing the move work item. */

  if (file_copy && !unversioned)
    {
      svn_boolean_t modified;
      /* It's faster to look for mods on the source now, as
         the timestamp might match, than to examine the
         destination later as the destination timestamp will
         never match. */

      if (dirent
          && dirent->kind == svn_node_file
          && recorded_size != SVN_INVALID_FILESIZE
          && recorded_size == dirent->filesize
          && recorded_time == dirent->mtime)
        {
          modified = FALSE; /* Recorded matches on-disk. Easy out */
        }
      else
        {
          SVN_ERR(svn_wc__internal_file_modified_p(&modified, db, src_abspath,
                                                   FALSE, scratch_pool));
        }

      if (!modified)
        {
          /* Why create a temp copy if we can just reinstall from pristine? */
          SVN_ERR(svn_wc__wq_build_file_install(work_item,
                                                db, dst_abspath, NULL, FALSE,
                                                TRUE,
                                                result_pool, scratch_pool));
          return SVN_NO_ERROR;
        }
    }
  else if (*kind == svn_node_dir && !file_copy)
    {
      /* Just build a new direcory from the workqueue */
      SVN_ERR(svn_wc__wq_build_dir_install(work_item,
                                           db, dst_abspath,
                                           result_pool, scratch_pool));

      return SVN_NO_ERROR;
    }

  /* Set DST_TMP_ABSPATH to a temporary unique path.  If *KIND is file, leave
     a file there and then overwrite it; otherwise leave no node on disk at
     that path.  In the latter case, something else might use that path
     before we get around to using it a moment later, but never mind. */
  SVN_ERR(svn_io_open_unique_file3(NULL, &dst_tmp_abspath, tmpdir_abspath,
                                   delete_when, scratch_pool, scratch_pool));

  if (*kind == svn_node_dir)
    {
      if (file_copy)
        SVN_ERR(svn_io_copy_dir_recursively(
                           src_abspath,
                           tmpdir_abspath,
                           svn_dirent_basename(dst_tmp_abspath, scratch_pool),
                           TRUE, /* copy_perms */
                           cancel_func, cancel_baton,
                           scratch_pool));
      else
        SVN_ERR(svn_io_dir_make(dst_tmp_abspath, APR_OS_DEFAULT, scratch_pool));
    }
  else if (!is_special)
    SVN_ERR(svn_io_copy_file(src_abspath, dst_tmp_abspath,
                             TRUE /* copy_perms */,
                             scratch_pool));
  else
    SVN_ERR(svn_io_copy_link(src_abspath, dst_tmp_abspath, scratch_pool));

  if (file_copy)
    {
      /* Remove 'read-only' from the destination file; it's a local add now. */
      SVN_ERR(svn_io_set_file_read_write(dst_tmp_abspath,
                                         FALSE, scratch_pool));
    }

  SVN_ERR(svn_wc__wq_build_file_move(work_item, db, dst_abspath,
                                     dst_tmp_abspath, dst_abspath,
                                     result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Copy the versioned file SRC_ABSPATH in DB to the path DST_ABSPATH in DB.
   If METADATA_ONLY is true, copy only the versioned metadata,
   otherwise copy both the versioned metadata and the filesystem node (even
   if it is the wrong kind, and recursively if it is a dir).

   If IS_MOVE is true, record move information in working copy meta
   data in addition to copying the file.

   If the versioned file has a text conflict, and the .mine file exists in
   the filesystem, copy the .mine file to DST_ABSPATH.  Otherwise, copy the
   versioned file itself.

   This also works for versioned symlinks that are stored in the db as
   svn_node_file with svn:special set.

   If DIRENT is not NULL, it contains the on-disk information of SRC_ABSPATH.
   RECORDED_SIZE (if not SVN_INVALID_FILESIZE) contains the recorded size of
   SRC_ABSPATH, and RECORDED_TIME the recorded size or 0.

   These values will be used to avoid unneeded work.
*/
static svn_error_t *
copy_versioned_file(svn_wc__db_t *db,
                    const char *src_abspath,
                    const char *dst_abspath,
                    const char *dst_op_root_abspath,
                    const char *tmpdir_abspath,
                    svn_boolean_t metadata_only,
                    svn_boolean_t conflicted,
                    svn_boolean_t is_move,
                    const svn_io_dirent2_t *dirent,
                    svn_filesize_t recorded_size,
                    apr_time_t recorded_time,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    svn_wc_notify_func2_t notify_func,
                    void *notify_baton,
                    apr_pool_t *scratch_pool)
{
  svn_skel_t *work_items = NULL;

  /* In case we are copying from one WC to another (e.g. an external dir),
     ensure the destination WC has a copy of the pristine text. */

  /* Prepare a temp copy of the filesystem node.  It is usually a file, but
     copy recursively if it's a dir. */
  if (!metadata_only)
    {
      const char *my_src_abspath = NULL;
      svn_boolean_t handle_as_unversioned = FALSE;

      /* By default, take the copy source as given. */
      my_src_abspath = src_abspath;

      if (conflicted)
        {
          svn_skel_t *conflict;
          const char *conflict_working;
          svn_error_t *err;

          /* Is there a text conflict at the source path? */
          SVN_ERR(svn_wc__db_read_conflict(&conflict, NULL, NULL,
                                           db, src_abspath,
                                           scratch_pool, scratch_pool));

          err = svn_wc__conflict_read_text_conflict(&conflict_working, NULL, NULL,
                                                    db, src_abspath, conflict,
                                                    scratch_pool,
                                                    scratch_pool);

          if (err && err->apr_err == SVN_ERR_WC_MISSING)
            {
              /* not text conflicted */
              svn_error_clear(err);
              conflict_working = NULL;
            }
          else
            SVN_ERR(err);

          if (conflict_working)
            {
              svn_node_kind_t working_kind;

              /* Does the ".mine" file exist? */
              SVN_ERR(svn_io_check_path(conflict_working, &working_kind,
                                        scratch_pool));

              if (working_kind == svn_node_file)
                {
                   /* Don't perform unmodified/pristine optimization */
                  handle_as_unversioned = TRUE;
                  my_src_abspath = conflict_working;
                }
            }
        }

      SVN_ERR(copy_to_tmpdir(&work_items, NULL, db, my_src_abspath,
                             dst_abspath, tmpdir_abspath,
                             TRUE /* file_copy */,
                             handle_as_unversioned /* unversioned */,
                             dirent, recorded_size, recorded_time,
                             cancel_func, cancel_baton,
                             scratch_pool, scratch_pool));
    }

  /* Copy the (single) node's metadata, and move the new filesystem node
     into place. */
  SVN_ERR(svn_wc__db_op_copy(db, src_abspath, dst_abspath,
                             dst_op_root_abspath, is_move, work_items,
                             scratch_pool));

  if (notify_func)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(dst_abspath, svn_wc_notify_add,
                               scratch_pool);
      notify->kind = svn_node_file;

      (*notify_func)(notify_baton, notify, scratch_pool);
    }
  return SVN_NO_ERROR;
}

/* Copy the versioned dir SRC_ABSPATH in DB to the path DST_ABSPATH in DB,
   recursively.  If METADATA_ONLY is true, copy only the versioned metadata,
   otherwise copy both the versioned metadata and the filesystem nodes (even
   if they are the wrong kind, and including unversioned children).
   If IS_MOVE is true, record move information in working copy meta
   data in addition to copying the directory.

   WITHIN_ONE_WC is TRUE if the copy/move is within a single working copy (root)

   If DIRENT is not NULL, it contains the on-disk information of SRC_ABSPATH.
 */
static svn_error_t *
copy_versioned_dir(svn_wc__db_t *db,
                   const char *src_abspath,
                   const char *dst_abspath,
                   const char *dst_op_root_abspath,
                   const char *tmpdir_abspath,
                   svn_boolean_t metadata_only,
                   svn_boolean_t is_move,
                   const svn_io_dirent2_t *dirent,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   svn_wc_notify_func2_t notify_func,
                   void *notify_baton,
                   apr_pool_t *scratch_pool)
{
  svn_skel_t *work_items = NULL;
  const char *dir_abspath = svn_dirent_dirname(dst_abspath, scratch_pool);
  apr_hash_t *versioned_children;
  apr_hash_t *conflicted_children;
  apr_hash_t *disk_children;
  apr_hash_index_t *hi;
  svn_node_kind_t disk_kind;
  apr_pool_t *iterpool;

  /* Prepare a temp copy of the single filesystem node (usually a dir). */
  if (!metadata_only)
    {
      SVN_ERR(copy_to_tmpdir(&work_items, &disk_kind,
                             db, src_abspath, dst_abspath,
                             tmpdir_abspath,
                             FALSE /* file_copy */,
                             FALSE /* unversioned */,
                             dirent, SVN_INVALID_FILESIZE, 0,
                             cancel_func, cancel_baton,
                             scratch_pool, scratch_pool));
    }

  /* Copy the (single) node's metadata, and move the new filesystem node
     into place. */
  SVN_ERR(svn_wc__db_op_copy(db, src_abspath, dst_abspath,
                             dst_op_root_abspath, is_move, work_items,
                             scratch_pool));

  if (notify_func)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(dst_abspath, svn_wc_notify_add,
                               scratch_pool);
      notify->kind = svn_node_dir;

      /* When we notify that we performed a copy, make sure we already did */
      if (work_items != NULL)
        SVN_ERR(svn_wc__wq_run(db, dir_abspath,
                               cancel_func, cancel_baton, scratch_pool));

      (*notify_func)(notify_baton, notify, scratch_pool);
    }

  if (!metadata_only && disk_kind == svn_node_dir)
    /* All filesystem children, versioned and unversioned.  We're only
       interested in their names, so we can pass TRUE as the only_check_type
       param. */
    SVN_ERR(svn_io_get_dirents3(&disk_children, src_abspath, TRUE,
                                scratch_pool, scratch_pool));
  else
    disk_children = NULL;

  /* Copy all the versioned children */
  iterpool = svn_pool_create(scratch_pool);
  SVN_ERR(svn_wc__db_read_children_info(&versioned_children,
                                        &conflicted_children,
                                        db, src_abspath,
                                        FALSE /* base_tree_only */,
                                        scratch_pool, iterpool));
  for (hi = apr_hash_first(scratch_pool, versioned_children);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *child_name, *child_src_abspath, *child_dst_abspath;
      struct svn_wc__db_info_t *info;

      svn_pool_clear(iterpool);

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      child_name = apr_hash_this_key(hi);
      info = apr_hash_this_val(hi);
      child_src_abspath = svn_dirent_join(src_abspath, child_name, iterpool);
      child_dst_abspath = svn_dirent_join(dst_abspath, child_name, iterpool);

      if (info->op_root)
        SVN_ERR(svn_wc__db_op_copy_shadowed_layer(db,
                                                  child_src_abspath,
                                                  child_dst_abspath,
                                                  is_move,
                                                  scratch_pool));

      if (info->status == svn_wc__db_status_normal
          || info->status == svn_wc__db_status_added)
        {
          /* We have more work to do than just changing the DB */
          if (info->kind == svn_node_file)
            {
              /* We should skip this node if this child is a file external
                 (issues #3589, #4000) */
              if (!info->file_external)
                SVN_ERR(copy_versioned_file(db,
                                            child_src_abspath,
                                            child_dst_abspath,
                                            dst_op_root_abspath,
                                            tmpdir_abspath,
                                            metadata_only, info->conflicted,
                                            is_move,
                                            disk_children
                                              ? svn_hash_gets(disk_children,
                                                              child_name)
                                              : NULL,
                                            info->recorded_size,
                                            info->recorded_time,
                                            cancel_func, cancel_baton,
                                            NULL, NULL,
                                            iterpool));
            }
          else if (info->kind == svn_node_dir)
            SVN_ERR(copy_versioned_dir(db,
                                       child_src_abspath, child_dst_abspath,
                                       dst_op_root_abspath, tmpdir_abspath,
                                       metadata_only, is_move,
                                       disk_children
                                              ? svn_hash_gets(disk_children,
                                                              child_name)
                                              : NULL,
                                       cancel_func, cancel_baton, NULL, NULL,
                                       iterpool));
          else
            return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                                     _("cannot handle node kind for '%s'"),
                                     svn_dirent_local_style(child_src_abspath,
                                                            scratch_pool));
        }
      else if (info->status == svn_wc__db_status_deleted
          || info->status == svn_wc__db_status_not_present
          || info->status == svn_wc__db_status_excluded)
        {
          /* This will be copied as some kind of deletion. Don't touch
             any actual files */
          SVN_ERR(svn_wc__db_op_copy(db, child_src_abspath,
                                     child_dst_abspath, dst_op_root_abspath,
                                     is_move, NULL, iterpool));

          /* Don't recurse on children when all we do is creating not-present
             children */
        }
      else if (info->status == svn_wc__db_status_incomplete)
        {
          /* Should go ahead and copy incomplete to incomplete? Try to
             copy as much as possible, or give up early? */
          return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                                   _("Cannot handle status of '%s'"),
                                   svn_dirent_local_style(child_src_abspath,
                                                          iterpool));
        }
      else
        {
          SVN_ERR_ASSERT(info->status == svn_wc__db_status_server_excluded);

          return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                                   _("Cannot copy '%s' excluded by server"),
                                   svn_dirent_local_style(child_src_abspath,
                                                          iterpool));
        }

      if (disk_children
          && (info->status == svn_wc__db_status_normal
              || info->status == svn_wc__db_status_added))
        {
          /* Remove versioned child as it has been handled */
          svn_hash_sets(disk_children, child_name, NULL);
        }
    }

  /* Copy the remaining filesystem children, which are unversioned, skipping
     any conflict-marker files. */
  if (disk_children && apr_hash_count(disk_children))
    {
      apr_hash_t *marker_files;

      SVN_ERR(svn_wc__db_get_conflict_marker_files(&marker_files, db,
                                                   src_abspath, scratch_pool,
                                                   scratch_pool));

      work_items = NULL;

      for (hi = apr_hash_first(scratch_pool, disk_children); hi;
           hi = apr_hash_next(hi))
        {
          const char *name = apr_hash_this_key(hi);
          const char *unver_src_abspath, *unver_dst_abspath;
          svn_skel_t *work_item;

          if (svn_wc_is_adm_dir(name, iterpool))
            continue;

          if (cancel_func)
            SVN_ERR(cancel_func(cancel_baton));

          svn_pool_clear(iterpool);
          unver_src_abspath = svn_dirent_join(src_abspath, name, iterpool);
          unver_dst_abspath = svn_dirent_join(dst_abspath, name, iterpool);

          if (marker_files &&
              svn_hash_gets(marker_files, unver_src_abspath))
            continue;

          SVN_ERR(copy_to_tmpdir(&work_item, NULL, db, unver_src_abspath,
                                 unver_dst_abspath, tmpdir_abspath,
                                 TRUE /* recursive */, TRUE /* unversioned */,
                                 NULL, SVN_INVALID_FILESIZE, 0,
                                 cancel_func, cancel_baton,
                                 scratch_pool, iterpool));

          if (work_item)
            work_items = svn_wc__wq_merge(work_items, work_item, scratch_pool);
        }
      SVN_ERR(svn_wc__db_wq_add(db, dst_abspath, work_items, iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* The guts of svn_wc_copy3() and svn_wc_move().
 * The additional parameter IS_MOVE indicates whether this is a copy or
 * a move operation.
 *
 * If RECORD_MOVE_ON_DELETE is not NULL and a move had to be degraded
 * to a copy, then set *RECORD_MOVE_ON_DELETE to FALSE. */
static svn_error_t *
copy_or_move(svn_boolean_t *record_move_on_delete,
             svn_wc_context_t *wc_ctx,
             const char *src_abspath,
             const char *dst_abspath,
             svn_boolean_t metadata_only,
             svn_boolean_t is_move,
             svn_boolean_t allow_mixed_revisions,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_wc_notify_func2_t notify_func,
             void *notify_baton,
             apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  svn_node_kind_t src_db_kind;
  const char *dstdir_abspath;
  svn_boolean_t conflicted;
  const char *tmpdir_abspath;
  const char *src_wcroot_abspath;
  const char *dst_wcroot_abspath;
  svn_boolean_t within_one_wc;
  svn_wc__db_status_t src_status;
  svn_error_t *err;
  svn_filesize_t recorded_size;
  apr_time_t recorded_time;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  dstdir_abspath = svn_dirent_dirname(dst_abspath, scratch_pool);

  /* Ensure DSTDIR_ABSPATH belongs to the same repository as SRC_ABSPATH;
     throw an error if not. */
  {
    svn_wc__db_status_t dstdir_status;
    const char *src_repos_root_url, *dst_repos_root_url;
    const char *src_repos_uuid, *dst_repos_uuid;
    const char *src_repos_relpath;

    err = svn_wc__db_read_info(&src_status, &src_db_kind, NULL,
                               &src_repos_relpath, &src_repos_root_url,
                               &src_repos_uuid, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               &recorded_size, &recorded_time,
                               NULL, &conflicted, NULL, NULL, NULL, NULL,
                               NULL, NULL,
                               db, src_abspath, scratch_pool, scratch_pool);

    if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
      {
        /* Replicate old error code and text */
        svn_error_clear(err);
        return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                                 _("'%s' is not under version control"),
                                 svn_dirent_local_style(src_abspath,
                                                        scratch_pool));
      }
    else
      SVN_ERR(err);

    /* Do this now, as we know the right data is cached */
    SVN_ERR(svn_wc__db_get_wcroot(&src_wcroot_abspath, db, src_abspath,
                                  scratch_pool, scratch_pool));

    switch (src_status)
      {
        case svn_wc__db_status_deleted:
          return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                                   _("Deleted node '%s' can't be copied."),
                                   svn_dirent_local_style(src_abspath,
                                                          scratch_pool));

        case svn_wc__db_status_excluded:
        case svn_wc__db_status_server_excluded:
        case svn_wc__db_status_not_present:
          return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                   _("The node '%s' was not found."),
                                   svn_dirent_local_style(src_abspath,
                                                          scratch_pool));
        default:
          break;
      }

     if (is_move && ! strcmp(src_abspath, src_wcroot_abspath))
      {
        return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                                 _("'%s' is the root of a working copy and "
                                   "cannot be moved"),
                                   svn_dirent_local_style(src_abspath,
                                                          scratch_pool));
      }
    if (is_move && src_repos_relpath && !src_repos_relpath[0])
      {
        return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                                 _("'%s' represents the repository root "
                                   "and cannot be moved"),
                                 svn_dirent_local_style(src_abspath,
                                                        scratch_pool));
      }

    err = svn_wc__db_read_info(&dstdir_status, NULL, NULL, NULL,
                               &dst_repos_root_url, &dst_repos_uuid, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL,
                               db, dstdir_abspath,
                               scratch_pool, scratch_pool);

    if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
      {
        /* An unversioned destination directory exists on disk. */
        svn_error_clear(err);
        return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                                 _("'%s' is not under version control"),
                                 svn_dirent_local_style(dstdir_abspath,
                                                        scratch_pool));
      }
    else
      SVN_ERR(err);

    /* Do this now, as we know the right data is cached */
    SVN_ERR(svn_wc__db_get_wcroot(&dst_wcroot_abspath, db, dstdir_abspath,
                                  scratch_pool, scratch_pool));

    if (!src_repos_root_url)
      {
        if (src_status == svn_wc__db_status_added)
          SVN_ERR(svn_wc__db_scan_addition(NULL, NULL, NULL,
                                           &src_repos_root_url,
                                           &src_repos_uuid, NULL, NULL, NULL,
                                           NULL,
                                           db, src_abspath,
                                           scratch_pool, scratch_pool));
        else
          /* If not added, the node must have a base or we can't copy */
          SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, NULL, NULL,
                                           &src_repos_root_url,
                                           &src_repos_uuid, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL, NULL,
                                           NULL,
                                           db, src_abspath,
                                           scratch_pool, scratch_pool));
      }

    if (!dst_repos_root_url)
      {
        if (dstdir_status == svn_wc__db_status_added)
          SVN_ERR(svn_wc__db_scan_addition(NULL, NULL, NULL,
                                           &dst_repos_root_url,
                                           &dst_repos_uuid, NULL, NULL, NULL,
                                           NULL,
                                           db, dstdir_abspath,
                                           scratch_pool, scratch_pool));
        else
          /* If not added, the node must have a base or we can't copy */
          SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, NULL, NULL,
                                           &dst_repos_root_url,
                                           &dst_repos_uuid, NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL, NULL,
                                           NULL,
                                           db, dstdir_abspath,
                                           scratch_pool, scratch_pool));
      }

    if (strcmp(src_repos_root_url, dst_repos_root_url) != 0
        || strcmp(src_repos_uuid, dst_repos_uuid) != 0)
      return svn_error_createf(
         SVN_ERR_WC_INVALID_SCHEDULE, NULL,
         _("Cannot copy to '%s', as it is not from repository '%s'; "
           "it is from '%s'"),
         svn_dirent_local_style(dst_abspath, scratch_pool),
         src_repos_root_url, dst_repos_root_url);

    if (dstdir_status == svn_wc__db_status_deleted)
      return svn_error_createf(
         SVN_ERR_WC_INVALID_SCHEDULE, NULL,
         _("Cannot copy to '%s' as it is scheduled for deletion"),
         svn_dirent_local_style(dst_abspath, scratch_pool));
         /* ### should report dstdir_abspath instead of dst_abspath? */
  }

  /* TODO(#2843): Rework the error report. */
  /* Check if the copy target is missing or hidden and thus not exist on the
     disk, before actually doing the file copy. */
  {
    svn_wc__db_status_t dst_status;

    err = svn_wc__db_read_info(&dst_status, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL,
                               db, dst_abspath, scratch_pool, scratch_pool);

    if (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
      return svn_error_trace(err);

    svn_error_clear(err);

    if (!err)
      switch (dst_status)
        {
          case svn_wc__db_status_excluded:
            return svn_error_createf(
                     SVN_ERR_ENTRY_EXISTS, NULL,
                     _("'%s' is already under version control "
                       "but is excluded."),
                     svn_dirent_local_style(dst_abspath, scratch_pool));
          case svn_wc__db_status_server_excluded:
            return svn_error_createf(
                     SVN_ERR_ENTRY_EXISTS, NULL,
                     _("'%s' is already under version control"),
                     svn_dirent_local_style(dst_abspath, scratch_pool));

          case svn_wc__db_status_deleted:
          case svn_wc__db_status_not_present:
            break; /* OK to add */

          default:
            if (!metadata_only)
              return svn_error_createf(SVN_ERR_ENTRY_EXISTS, NULL,
                                 _("There is already a versioned item '%s'"),
                                 svn_dirent_local_style(dst_abspath,
                                                        scratch_pool));
        }
  }

  /* Check that the target path is not obstructed, if required. */
  if (!metadata_only)
    {
      svn_node_kind_t dst_kind;

      /* (We need only to check the root of the copy, not every path inside
         copy_versioned_file/_dir.) */
      SVN_ERR(svn_io_check_path(dst_abspath, &dst_kind, scratch_pool));
      if (dst_kind != svn_node_none)
        return svn_error_createf(SVN_ERR_ENTRY_EXISTS, NULL,
                                 _("'%s' already exists and is in the way"),
                                 svn_dirent_local_style(dst_abspath,
                                                        scratch_pool));
    }

  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&tmpdir_abspath, db,
                                         dstdir_abspath,
                                         scratch_pool, scratch_pool));

  within_one_wc = (strcmp(src_wcroot_abspath, dst_wcroot_abspath) == 0);

  if (is_move
      && !within_one_wc)
    {
      if (record_move_on_delete)
        *record_move_on_delete = FALSE;

      is_move = FALSE;
    }

  if (!within_one_wc)
    SVN_ERR(svn_wc__db_pristine_transfer(db, src_abspath, dst_wcroot_abspath,
                                         cancel_func, cancel_baton,
                                         scratch_pool));

  if (src_db_kind == svn_node_file
      || src_db_kind == svn_node_symlink)
    {
      err = copy_versioned_file(db, src_abspath, dst_abspath, dst_abspath,
                                tmpdir_abspath,
                                metadata_only, conflicted, is_move,
                                NULL, recorded_size, recorded_time,
                                cancel_func, cancel_baton,
                                notify_func, notify_baton,
                                scratch_pool);
    }
  else
    {
      if (is_move
          && src_status == svn_wc__db_status_normal)
        {
          svn_revnum_t min_rev;
          svn_revnum_t max_rev;

          /* Verify that the move source is a single-revision subtree. */
          SVN_ERR(svn_wc__db_min_max_revisions(&min_rev, &max_rev, db,
                                               src_abspath, FALSE, scratch_pool));
          if (SVN_IS_VALID_REVNUM(min_rev) && SVN_IS_VALID_REVNUM(max_rev) &&
              min_rev != max_rev)
            {
              if (!allow_mixed_revisions)
                return svn_error_createf(SVN_ERR_WC_MIXED_REVISIONS, NULL,
                                         _("Cannot move mixed-revision "
                                           "subtree '%s' [%ld:%ld]; "
                                           "try updating it first"),
                                         svn_dirent_local_style(src_abspath,
                                                                scratch_pool),
                                         min_rev, max_rev);

#ifndef RECORD_MIXED_MOVE
              is_move = FALSE;
              if (record_move_on_delete)
                *record_move_on_delete = FALSE;
#endif
            }
        }

      err = copy_versioned_dir(db, src_abspath, dst_abspath, dst_abspath,
                               tmpdir_abspath, metadata_only, is_move,
                               NULL /* dirent */,
                               cancel_func, cancel_baton,
                               notify_func, notify_baton,
                               scratch_pool);
    }

  if (err && svn_error_find_cause(err, SVN_ERR_CANCELLED))
    return svn_error_trace(err);

  if (is_move)
    err = svn_error_compose_create(err,
                svn_wc__db_op_handle_move_back(NULL,
                                               db, dst_abspath, src_abspath,
                                               NULL /* work_items */,
                                               scratch_pool));

  /* Run the work queue with the remaining work */
  SVN_ERR(svn_error_compose_create(
                                err,
                                svn_wc__wq_run(db, dst_abspath,
                                                   cancel_func, cancel_baton,
                                                   scratch_pool)));

  return SVN_NO_ERROR;
}


/* Public Interface */

svn_error_t *
svn_wc_copy3(svn_wc_context_t *wc_ctx,
             const char *src_abspath,
             const char *dst_abspath,
             svn_boolean_t metadata_only,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_wc_notify_func2_t notify_func,
             void *notify_baton,
             apr_pool_t *scratch_pool)
{
  /* Verify that we have the required write lock. */
  SVN_ERR(svn_wc__write_check(wc_ctx->db,
                              svn_dirent_dirname(dst_abspath, scratch_pool),
                              scratch_pool));

  return svn_error_trace(copy_or_move(NULL, wc_ctx, src_abspath, dst_abspath,
                                      metadata_only, FALSE /* is_move */,
                                      TRUE /* allow_mixed_revisions */,
                                      cancel_func, cancel_baton,
                                      notify_func, notify_baton,
                                      scratch_pool));
}


/* Remove the conflict markers of NODE_ABSPATH, that were left over after
   copying NODE_ABSPATH from SRC_ABSPATH.

   Only use this function when you know what you're doing. This function
   explicitly ignores some case insensitivity issues!

   */
static svn_error_t *
remove_node_conflict_markers(svn_wc__db_t *db,
                             const char *src_abspath,
                             const char *node_abspath,
                             apr_pool_t *scratch_pool)
{
  svn_skel_t *conflict;

  SVN_ERR(svn_wc__db_read_conflict(&conflict, NULL, NULL,
                                   db, src_abspath,
                                   scratch_pool, scratch_pool));

  /* Do we have conflict markers that should be removed? */
  if (conflict != NULL)
    {
      const apr_array_header_t *markers;
      int i;
      const char *src_dir = svn_dirent_dirname(src_abspath, scratch_pool);
      const char *dst_dir = svn_dirent_dirname(node_abspath, scratch_pool);

      SVN_ERR(svn_wc__conflict_read_markers(&markers, db, src_abspath,
                                            conflict,
                                            scratch_pool, scratch_pool));

      /* No iterpool: Maximum number of possible conflict markers is 4 */
      for (i = 0; markers && (i < markers->nelts); i++)
        {
          const char *marker_abspath;
          const char *child_relpath;
          const char *child_abspath;

          marker_abspath = APR_ARRAY_IDX(markers, i, const char *);

          child_relpath = svn_dirent_skip_ancestor(src_dir, marker_abspath);

          if (child_relpath)
            {
              child_abspath = svn_dirent_join(dst_dir, child_relpath,
                                              scratch_pool);

              SVN_ERR(svn_io_remove_file2(child_abspath, TRUE, scratch_pool));
            }
        }
    }

  return SVN_NO_ERROR;
}

/* Remove all the conflict markers below SRC_DIR_ABSPATH, that were left over
   after copying WC_DIR_ABSPATH from SRC_DIR_ABSPATH.

   This function doesn't remove the conflict markers on WC_DIR_ABSPATH
   itself!

   Only use this function when you know what you're doing. This function
   explicitly ignores some case insensitivity issues!
   */
static svn_error_t *
remove_all_conflict_markers(svn_wc__db_t *db,
                            const char *src_dir_abspath,
                            const char *dst_dir_abspath,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *nodes;
  apr_hash_t *conflicts; /* Unused */
  apr_hash_index_t *hi;

  /* Reuse a status helper to obtain all subdirs and conflicts in a single
     db transaction. */
  /* ### This uses a rifle to kill a fly. But at least it doesn't use heavy
          artillery. */
  SVN_ERR(svn_wc__db_read_children_info(&nodes, &conflicts, db,
                                        src_dir_abspath,
                                        FALSE /* base_tree_only */,
                                        scratch_pool, iterpool));

  for (hi = apr_hash_first(scratch_pool, nodes);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *name = apr_hash_this_key(hi);
      struct svn_wc__db_info_t *info = apr_hash_this_val(hi);

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      if (info->conflicted)
        {
          svn_pool_clear(iterpool);
          SVN_ERR(remove_node_conflict_markers(
                            db,
                            svn_dirent_join(src_dir_abspath, name, iterpool),
                            svn_dirent_join(dst_dir_abspath, name, iterpool),
                            iterpool));
        }
      if (info->kind == svn_node_dir)
        {
          svn_pool_clear(iterpool);
          SVN_ERR(remove_all_conflict_markers(
                            db,
                            svn_dirent_join(src_dir_abspath, name, iterpool),
                            svn_dirent_join(dst_dir_abspath, name, iterpool),
                            cancel_func, cancel_baton,
                            iterpool));
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__move2(svn_wc_context_t *wc_ctx,
              const char *src_abspath,
              const char *dst_abspath,
              svn_boolean_t metadata_only,
              svn_boolean_t allow_mixed_revisions,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              svn_wc_notify_func2_t notify_func,
              void *notify_baton,
              apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  svn_boolean_t record_on_delete = TRUE;
  svn_node_kind_t kind;
  svn_boolean_t conflicted;

  /* Verify that we have the required write locks. */
  SVN_ERR(svn_wc__write_check(wc_ctx->db,
                              svn_dirent_dirname(src_abspath, scratch_pool),
                              scratch_pool));
  SVN_ERR(svn_wc__write_check(wc_ctx->db,
                              svn_dirent_dirname(dst_abspath, scratch_pool),
                              scratch_pool));

  SVN_ERR(copy_or_move(&record_on_delete,
                       wc_ctx, src_abspath, dst_abspath,
                       TRUE /* metadata_only */,
                       TRUE /* is_move */,
                       allow_mixed_revisions,
                       cancel_func, cancel_baton,
                       notify_func, notify_baton,
                       scratch_pool));

  /* An interrupt at this point will leave the new copy marked as
     moved-here but the source has not yet been deleted or marked as
     moved-to. */

  /* Should we be using a workqueue for this move?  It's not clear.
     What should happen if the copy above is interrupted?  The user
     may want to abort the move and a workqueue might interfere with
     that.

     BH: On Windows it is not unlikely to encounter an access denied on
     this line. Installing the move in the workqueue via the copy_or_move
     might make it hard to recover from that situation, while the DB
     is still in a valid state. So be careful when switching this over
     to the workqueue. */
  if (!metadata_only)
    {
      svn_error_t *err;

      err = svn_error_trace(svn_io_file_rename2(src_abspath, dst_abspath,
                                                FALSE, scratch_pool));

      /* Let's try if we can keep wc.db consistent even when the move
         fails. Deleting the target is a wc.db only operation, while
         going forward (delaying the error) would try to change
         conflict markers, which might also fail. */
      if (err)
        return svn_error_trace(
          svn_error_compose_create(
              err,
              svn_wc__db_op_delete(wc_ctx->db, dst_abspath, NULL, TRUE,
                                   NULL, NULL, cancel_func, cancel_baton,
                                   NULL, NULL,
                                   scratch_pool)));
    }

  SVN_ERR(svn_wc__db_read_info(NULL, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               &conflicted, NULL, NULL, NULL,
                               NULL, NULL, NULL,
                               db, src_abspath,
                               scratch_pool, scratch_pool));

  if (kind == svn_node_dir)
    SVN_ERR(remove_all_conflict_markers(db, src_abspath, dst_abspath,
                                        cancel_func, cancel_baton,
                                        scratch_pool));

  if (conflicted)
    {
      /* When we moved a directory, we moved the conflict markers
         with the target... if we moved a file we only moved the
         file itself and the markers are still in the old location */
      SVN_ERR(remove_node_conflict_markers(db, src_abspath,
                                           (kind == svn_node_dir)
                                             ? dst_abspath
                                             : src_abspath,
                                           scratch_pool));
    }

  SVN_ERR(svn_wc__db_op_delete(db, src_abspath,
                               record_on_delete ? dst_abspath : NULL,
                               TRUE /* delete_dir_externals */,
                               NULL /* conflict */, NULL /* work_items */,
                               cancel_func, cancel_baton,
                               notify_func, notify_baton,
                               scratch_pool));

  return SVN_NO_ERROR;
}
