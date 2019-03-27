/*
 * revert.c: Handling of the in-wc side of the revert operation
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
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_io.h"

#include "wc.h"
#include "adm_files.h"
#include "workqueue.h"

#include "svn_private_config.h"
#include "private/svn_io_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_sorts_private.h"

/* Thoughts on Reversion.

    What does is mean to revert a given PATH in a tree?  We'll
    consider things by their modifications.

    Adds

    - For files, svn_wc_remove_from_revision_control(), baby.

    - Added directories may contain nothing but added children, and
      reverting the addition of a directory necessarily means reverting
      the addition of all the directory's children.  Again,
      svn_wc_remove_from_revision_control() should do the trick.

    Deletes

    - Restore properties to their unmodified state.

    - For files, restore the pristine contents, and reset the schedule
      to 'normal'.

    - For directories, reset the schedule to 'normal'.  All children
      of a directory marked for deletion must also be marked for
      deletion, but it's okay for those children to remain deleted even
      if their parent directory is restored.  That's what the
      recursive flag is for.

    Replaces

    - Restore properties to their unmodified state.

    - For files, restore the pristine contents, and reset the schedule
      to 'normal'.

    - For directories, reset the schedule to normal.  A replaced
      directory can have deleted children (left over from the initial
      deletion), replaced children (children of the initial deletion
      now re-added), and added children (new entries under the
      replaced directory).  Since this is technically an addition, it
      necessitates recursion.

    Modifications

    - Restore properties and, for files, contents to their unmodified
      state.

*/


/* Remove conflict file CONFLICT_ABSPATH, which may not exist, and set
 * *NOTIFY_REQUIRED to TRUE if the file was present and removed. */
static svn_error_t *
remove_conflict_file(svn_boolean_t *notify_required,
                     const char *conflict_abspath,
                     const char *local_abspath,
                     apr_pool_t *scratch_pool)
{
  if (conflict_abspath)
    {
      svn_error_t *err = svn_io_remove_file2(conflict_abspath, FALSE,
                                             scratch_pool);
      if (err)
        svn_error_clear(err);
      else
        *notify_required = TRUE;
    }

  return SVN_NO_ERROR;
}


/* Sort copied children obtained from the revert list based on
 * their paths in descending order (longest paths first). */
static int
compare_revert_list_copied_children(const void *a, const void *b)
{
  const svn_wc__db_revert_list_copied_child_info_t * const *ca = a;
  const svn_wc__db_revert_list_copied_child_info_t * const *cb = b;
  int i;

  i = svn_path_compare_paths(ca[0]->abspath, cb[0]->abspath);

  /* Reverse the result of svn_path_compare_paths() to achieve
   * descending order. */
  return -i;
}


/* Remove all reverted copied children from the directory at LOCAL_ABSPATH.
 * If REMOVE_SELF is TRUE, try to remove LOCAL_ABSPATH itself (REMOVE_SELF
 * should be set if LOCAL_ABSPATH is itself a reverted copy).
 *
 * If REMOVED_SELF is not NULL, indicate in *REMOVED_SELF whether
 * LOCAL_ABSPATH itself was removed.
 *
 * All reverted copied file children are removed from disk. Reverted copied
 * directories left empty as a result are also removed from disk.
 */
static svn_error_t *
revert_restore_handle_copied_dirs(svn_boolean_t *removed_self,
                                  svn_wc__db_t *db,
                                  const char *local_abspath,
                                  svn_boolean_t remove_self,
                                  svn_cancel_func_t cancel_func,
                                  void *cancel_baton,
                                  apr_pool_t *scratch_pool)
{
  apr_array_header_t *copied_children;
  svn_wc__db_revert_list_copied_child_info_t *child_info;
  int i;
  svn_node_kind_t on_disk;
  apr_pool_t *iterpool;
  svn_error_t *err;

  if (removed_self)
    *removed_self = FALSE;

  SVN_ERR(svn_wc__db_revert_list_read_copied_children(&copied_children,
                                                      db, local_abspath,
                                                      scratch_pool,
                                                      scratch_pool));
  iterpool = svn_pool_create(scratch_pool);

  /* Remove all copied file children. */
  for (i = 0; i < copied_children->nelts; i++)
    {
      child_info = APR_ARRAY_IDX(
                     copied_children, i,
                     svn_wc__db_revert_list_copied_child_info_t *);

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      if (child_info->kind != svn_node_file)
        continue;

      svn_pool_clear(iterpool);

      /* Make sure what we delete from disk is really a file. */
      SVN_ERR(svn_io_check_path(child_info->abspath, &on_disk, iterpool));
      if (on_disk != svn_node_file)
        continue;

      SVN_ERR(svn_io_remove_file2(child_info->abspath, TRUE, iterpool));
    }

  /* Delete every empty child directory.
   * We cannot delete children recursively since we want to keep any files
   * that still exist on disk (e.g. unversioned files within the copied tree).
   * So sort the children list such that longest paths come first and try to
   * remove each child directory in order. */
  svn_sort__array(copied_children, compare_revert_list_copied_children);
  for (i = 0; i < copied_children->nelts; i++)
    {
      child_info = APR_ARRAY_IDX(
                     copied_children, i,
                     svn_wc__db_revert_list_copied_child_info_t *);

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      if (child_info->kind != svn_node_dir)
        continue;

      svn_pool_clear(iterpool);

      err = svn_io_dir_remove_nonrecursive(child_info->abspath, iterpool);
      if (err)
        {
          if (APR_STATUS_IS_ENOENT(err->apr_err) ||
              SVN__APR_STATUS_IS_ENOTDIR(err->apr_err) ||
              APR_STATUS_IS_ENOTEMPTY(err->apr_err))
            svn_error_clear(err);
          else
            return svn_error_trace(err);
        }
    }

  if (remove_self)
    {
      /* Delete LOCAL_ABSPATH itself if no children are left. */
      err = svn_io_dir_remove_nonrecursive(local_abspath, iterpool);
      if (err)
       {
          if (APR_STATUS_IS_ENOTEMPTY(err->apr_err))
            svn_error_clear(err);
          else
            return svn_error_trace(err);
        }
      else if (removed_self)
        *removed_self = TRUE;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Forward definition */
static svn_error_t *
revert_wc_data(svn_boolean_t *run_wq,
               svn_boolean_t *notify_required,
               svn_wc__db_t *db,
               const char *local_abspath,
               svn_wc__db_status_t status,
               svn_node_kind_t kind,
               svn_node_kind_t reverted_kind,
               svn_filesize_t recorded_size,
               apr_time_t recorded_time,
               svn_boolean_t copied_here,
               svn_boolean_t use_commit_times,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool);

/* Make the working tree under LOCAL_ABSPATH to depth DEPTH match the
   versioned tree.  This function is called after svn_wc__db_op_revert
   has done the database revert and created the revert list.  Notifies
   for all paths equal to or below LOCAL_ABSPATH that are reverted.

   REVERT_ROOT is true for explicit revert targets and FALSE for targets
   reached via recursion.

   Sets *RUN_WQ to TRUE when the caller should (eventually) run the workqueue.
   (The function sets it to FALSE when it has run the WQ itself)

   If INFO is NULL, LOCAL_ABSPATH doesn't exist in DB. Otherwise INFO
   specifies the state of LOCAL_ABSPATH in DB.
 */
static svn_error_t *
revert_restore(svn_boolean_t *run_wq,
               svn_wc__db_t *db,
               const char *local_abspath,
               svn_depth_t depth,
               svn_boolean_t metadata_only,
               svn_boolean_t use_commit_times,
               svn_boolean_t revert_root,
               const struct svn_wc__db_info_t *info,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  svn_boolean_t notify_required;
  const apr_array_header_t *conflict_files;
  svn_filesize_t recorded_size;
  apr_time_t recorded_time;
  svn_boolean_t copied_here;
  svn_node_kind_t reverted_kind;
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  if (!revert_root)
    {
      svn_boolean_t is_wcroot;

      SVN_ERR(svn_wc__db_is_wcroot(&is_wcroot, db, local_abspath, scratch_pool));
      if (is_wcroot)
        {
          /* Issue #4162: Obstructing working copy. We can't access the working
             copy data from the parent working copy for this node by just using
             local_abspath */

          if (notify_func)
            {
              svn_wc_notify_t *notify =
                        svn_wc_create_notify(
                                        local_abspath,
                                        svn_wc_notify_update_skip_obstruction,
                                        scratch_pool);

              notify_func(notify_baton, notify, scratch_pool);
            }

          return SVN_NO_ERROR; /* We don't revert obstructing working copies */
        }
    }

  SVN_ERR(svn_wc__db_revert_list_read(&notify_required,
                                      &conflict_files,
                                      &copied_here, &reverted_kind,
                                      db, local_abspath,
                                      scratch_pool, scratch_pool));

  if (info)
    {
      status = info->status;
      kind = info->kind;
      recorded_size = info->recorded_size;
      recorded_time = info->recorded_time;
    }
  else
    {
      if (!copied_here)
        {
          if (notify_func && notify_required)
            notify_func(notify_baton,
                        svn_wc_create_notify(local_abspath,
                                             svn_wc_notify_revert,
                                             scratch_pool),
                        scratch_pool);

          if (notify_func)
            SVN_ERR(svn_wc__db_revert_list_notify(notify_func, notify_baton,
                                                  db, local_abspath,
                                                  scratch_pool));
          return SVN_NO_ERROR;
        }
      else
        {
          /* ### Initialise to values which prevent the code below from
           * ### trying to restore anything to disk.
           * ### 'status' should be status_unknown but that doesn't exist. */
          status = svn_wc__db_status_normal;
          kind = svn_node_unknown;
          recorded_size = SVN_INVALID_FILESIZE;
          recorded_time = 0;
        }
    }

  if (!metadata_only)
    {
      SVN_ERR(revert_wc_data(run_wq,
                             &notify_required,
                             db, local_abspath, status, kind,
                             reverted_kind, recorded_size, recorded_time,
                             copied_here, use_commit_times,
                             cancel_func, cancel_baton, scratch_pool));
    }

  /* We delete these marker files even though they are not strictly metadata.
     But for users that use revert as an API with metadata_only, these are. */
  if (conflict_files)
    {
      int i;
      for (i = 0; i < conflict_files->nelts; i++)
        {
          SVN_ERR(remove_conflict_file(&notify_required,
                                       APR_ARRAY_IDX(conflict_files, i,
                                                     const char *),
                                       local_abspath, scratch_pool));
        }
    }

  if (notify_func && notify_required)
    notify_func(notify_baton,
                svn_wc_create_notify(local_abspath, svn_wc_notify_revert,
                                     scratch_pool),
                scratch_pool);

  if (depth == svn_depth_infinity && kind == svn_node_dir)
    {
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);
      apr_hash_t *children, *conflicts;
      apr_hash_index_t *hi;

      SVN_ERR(revert_restore_handle_copied_dirs(NULL, db, local_abspath, FALSE,
                                                cancel_func, cancel_baton,
                                                iterpool));

      SVN_ERR(svn_wc__db_read_children_info(&children, &conflicts,
                                            db, local_abspath, FALSE,
                                            scratch_pool, iterpool));

      for (hi = apr_hash_first(scratch_pool, children);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *child_name = apr_hash_this_key(hi);
          const char *child_abspath;

          svn_pool_clear(iterpool);

          child_abspath = svn_dirent_join(local_abspath, child_name, iterpool);

          SVN_ERR(revert_restore(run_wq,
                                 db, child_abspath, depth, metadata_only,
                                 use_commit_times, FALSE /* revert root */,
                                 apr_hash_this_val(hi),
                                 cancel_func, cancel_baton,
                                 notify_func, notify_baton,
                                 iterpool));
        }

      /* Run the queue per directory */
      if (*run_wq)
        {
          SVN_ERR(svn_wc__wq_run(db, local_abspath, cancel_func, cancel_baton,
                                 iterpool));
          *run_wq = FALSE;
        }

      svn_pool_destroy(iterpool);
    }

  if (notify_func && (revert_root || kind == svn_node_dir))
    SVN_ERR(svn_wc__db_revert_list_notify(notify_func, notify_baton,
                                          db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

/* Perform the in-working copy revert of LOCAL_ABSPATH, to what is stored in DB */
static svn_error_t *
revert_wc_data(svn_boolean_t *run_wq,
               svn_boolean_t *notify_required,
               svn_wc__db_t *db,
               const char *local_abspath,
               svn_wc__db_status_t status,
               svn_node_kind_t kind,
               svn_node_kind_t reverted_kind,
               svn_filesize_t recorded_size,
               apr_time_t recorded_time,
               svn_boolean_t copied_here,
               svn_boolean_t use_commit_times,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  apr_finfo_t finfo;
  svn_node_kind_t on_disk;
#ifdef HAVE_SYMLINK
  svn_boolean_t special;
#endif

  /* Would be nice to use svn_io_dirent2_t here, but the performance
     improvement that provides doesn't work, because we need the read
     only and executable bits later on, in the most likely code path */
  err = svn_io_stat(&finfo, local_abspath,
                    APR_FINFO_TYPE | APR_FINFO_LINK
                    | APR_FINFO_SIZE | APR_FINFO_MTIME
                    | SVN__APR_FINFO_EXECUTABLE
                    | SVN__APR_FINFO_READONLY,
                    scratch_pool);

  if (err && (APR_STATUS_IS_ENOENT(err->apr_err)
              || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)))
    {
      svn_error_clear(err);
      on_disk = svn_node_none;
#ifdef HAVE_SYMLINK
      special = FALSE;
#endif
    }
  else if (!err)
    {
      if (finfo.filetype == APR_REG || finfo.filetype == APR_LNK)
        on_disk = svn_node_file;
      else if (finfo.filetype == APR_DIR)
        on_disk = svn_node_dir;
      else
        on_disk = svn_node_unknown;

#ifdef HAVE_SYMLINK
      special = (finfo.filetype == APR_LNK);
#endif
    }
  else
    return svn_error_trace(err);

  if (copied_here)
    {
      /* The revert target itself is the op-root of a copy. */
      if (reverted_kind == svn_node_file && on_disk == svn_node_file)
        {
          SVN_ERR(svn_io_remove_file2(local_abspath, TRUE, scratch_pool));
          on_disk = svn_node_none;
        }
      else if (reverted_kind == svn_node_dir && on_disk == svn_node_dir)
        {
          svn_boolean_t removed;

          SVN_ERR(revert_restore_handle_copied_dirs(&removed, db,
                                                    local_abspath, TRUE,
                                                    cancel_func, cancel_baton,
                                                    scratch_pool));
          if (removed)
            on_disk = svn_node_none;
        }
    }

  /* If we expect a versioned item to be present then check that any
     item on disk matches the versioned item, if it doesn't match then
     fix it or delete it.  */
  if (on_disk != svn_node_none
      && status != svn_wc__db_status_server_excluded
      && status != svn_wc__db_status_deleted
      && status != svn_wc__db_status_excluded
      && status != svn_wc__db_status_not_present)
    {
      if (on_disk == svn_node_dir && kind != svn_node_dir)
        {
          SVN_ERR(svn_io_remove_dir2(local_abspath, FALSE,
                                     cancel_func, cancel_baton, scratch_pool));
          on_disk = svn_node_none;
        }
      else if (on_disk == svn_node_file && kind != svn_node_file)
        {
#ifdef HAVE_SYMLINK
          /* Preserve symlinks pointing at directories. Changes on the
           * directory node have been reverted. The symlink should remain. */
          if (!(special && kind == svn_node_dir))
#endif
            {
              SVN_ERR(svn_io_remove_file2(local_abspath, FALSE, scratch_pool));
              on_disk = svn_node_none;
            }
        }
      else if (on_disk == svn_node_file)
        {
          svn_boolean_t modified;
          apr_hash_t *props;
#ifdef HAVE_SYMLINK
          svn_string_t *special_prop;
#endif

          SVN_ERR(svn_wc__db_read_pristine_props(&props, db, local_abspath,
                                                 scratch_pool, scratch_pool));

#ifdef HAVE_SYMLINK
          special_prop = svn_hash_gets(props, SVN_PROP_SPECIAL);

          if ((special_prop != NULL) != special)
            {
              /* File/symlink mismatch. */
              SVN_ERR(svn_io_remove_file2(local_abspath, FALSE, scratch_pool));
              on_disk = svn_node_none;
            }
          else
#endif
            {
              /* Issue #1663 asserts that we should compare a file in its
                 working copy format here, but before r1101473 we would only
                 do that if the file was already unequal to its recorded
                 information.

                 r1101473 removes the option of asking for a working format
                 compare but *also* check the recorded information first, as
                 that combination doesn't guarantee a stable behavior.
                 (See the revert_test.py: revert_reexpand_keyword)

                 But to have the same issue #1663 behavior for revert as we
                 had in <=1.6 we only have to check the recorded information
                 ourselves. And we already have everything we need, because
                 we called stat ourselves. */
              if (recorded_size != SVN_INVALID_FILESIZE
                  && recorded_time != 0
                  && recorded_size == finfo.size
                  && recorded_time == finfo.mtime)
                {
                  modified = FALSE;
                }
              else
                /* Side effect: fixes recorded timestamps */
                SVN_ERR(svn_wc__internal_file_modified_p(&modified,
                                                         db, local_abspath,
                                                         TRUE, scratch_pool));

              if (modified)
                {
                  /* Install will replace the file */
                  on_disk = svn_node_none;
                }
              else
                {
                  if (status == svn_wc__db_status_normal)
                    {
                      svn_boolean_t read_only;
                      svn_string_t *needs_lock_prop;

                      SVN_ERR(svn_io__is_finfo_read_only(&read_only, &finfo,
                                                         scratch_pool));

                      needs_lock_prop = svn_hash_gets(props,
                                                      SVN_PROP_NEEDS_LOCK);
                      if (needs_lock_prop && !read_only)
                        {
                          SVN_ERR(svn_io_set_file_read_only(local_abspath,
                                                            FALSE,
                                                            scratch_pool));
                          *notify_required = TRUE;
                        }
                      else if (!needs_lock_prop && read_only)
                        {
                          SVN_ERR(svn_io_set_file_read_write(local_abspath,
                                                             FALSE,
                                                             scratch_pool));
                          *notify_required = TRUE;
                        }
                    }

#if !defined(WIN32) && !defined(__OS2__)
#ifdef HAVE_SYMLINK
                  if (!special)
#endif
                    {
                      svn_boolean_t executable;
                      svn_string_t *executable_prop;

                      SVN_ERR(svn_io__is_finfo_executable(&executable, &finfo,
                                                          scratch_pool));
                      executable_prop = svn_hash_gets(props,
                                                      SVN_PROP_EXECUTABLE);
                      if (executable_prop && !executable)
                        {
                          SVN_ERR(svn_io_set_file_executable(local_abspath,
                                                             TRUE, FALSE,
                                                             scratch_pool));
                          *notify_required = TRUE;
                        }
                      else if (!executable_prop && executable)
                        {
                          SVN_ERR(svn_io_set_file_executable(local_abspath,
                                                             FALSE, FALSE,
                                                             scratch_pool));
                          *notify_required = TRUE;
                        }
                    }
#endif
                }
            }
        }
    }

  /* If we expect a versioned item to be present and there is nothing
     on disk then recreate it. */
  if (on_disk == svn_node_none
      && status != svn_wc__db_status_server_excluded
      && status != svn_wc__db_status_deleted
      && status != svn_wc__db_status_excluded
      && status != svn_wc__db_status_not_present)
    {
      if (kind == svn_node_dir)
        SVN_ERR(svn_io_dir_make(local_abspath, APR_OS_DEFAULT, scratch_pool));

      if (kind == svn_node_file)
        {
          svn_skel_t *work_item;

          SVN_ERR(svn_wc__wq_build_file_install(&work_item, db, local_abspath,
                                                NULL, use_commit_times, TRUE,
                                                scratch_pool, scratch_pool));
          SVN_ERR(svn_wc__db_wq_add(db, local_abspath, work_item,
                                    scratch_pool));
          *run_wq = TRUE;
        }
      *notify_required = TRUE;
    }

  return SVN_NO_ERROR;
}

/* Revert tree LOCAL_ABSPATH to depth DEPTH and notify for all reverts. */
static svn_error_t *
revert(svn_wc__db_t *db,
       const char *local_abspath,
       svn_depth_t depth,
       svn_boolean_t use_commit_times,
       svn_boolean_t clear_changelists,
       svn_boolean_t metadata_only,
       svn_cancel_func_t cancel_func,
       void *cancel_baton,
       svn_wc_notify_func2_t notify_func,
       void *notify_baton,
       apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const struct svn_wc__db_info_t *info = NULL;
  svn_boolean_t run_queue = FALSE;

  SVN_ERR_ASSERT(depth == svn_depth_empty || depth == svn_depth_infinity);

  /* We should have a write lock on the parent of local_abspath, except
     when local_abspath is the working copy root. */
  {
    const char *dir_abspath;
    svn_boolean_t is_wcroot;

    SVN_ERR(svn_wc__db_is_wcroot(&is_wcroot, db, local_abspath, scratch_pool));

    if (! is_wcroot)
      dir_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
    else
      dir_abspath = local_abspath;

    SVN_ERR(svn_wc__write_check(db, dir_abspath, scratch_pool));
  }

  err = svn_error_trace(
        svn_wc__db_op_revert(db, local_abspath, depth, clear_changelists,
                             scratch_pool, scratch_pool));

  if (!err)
    {
      err = svn_error_trace(
              svn_wc__db_read_single_info(&info, db, local_abspath, FALSE,
                                          scratch_pool, scratch_pool));

      if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          err = NULL;
          info = NULL;
        }
    }

  if (!err)
    err = svn_error_trace(
              revert_restore(&run_queue, db, local_abspath, depth, metadata_only,
                             use_commit_times, TRUE /* revert root */,
                             info, cancel_func, cancel_baton,
                             notify_func, notify_baton,
                             scratch_pool));

  if (run_queue)
    err = svn_error_compose_create(err,
                                   svn_wc__wq_run(db, local_abspath,
                                                  cancel_func, cancel_baton,
                                                  scratch_pool));

  err = svn_error_compose_create(err,
                                 svn_wc__db_revert_list_done(db,
                                                             local_abspath,
                                                             scratch_pool));

  return err;
}


/* Revert files in LOCAL_ABSPATH to depth DEPTH that match
   CHANGELIST_HASH and notify for all reverts. */
static svn_error_t *
revert_changelist(svn_wc__db_t *db,
                  const char *local_abspath,
                  svn_depth_t depth,
                  svn_boolean_t use_commit_times,
                  apr_hash_t *changelist_hash,
                  svn_boolean_t clear_changelists,
                  svn_boolean_t metadata_only,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  svn_wc_notify_func2_t notify_func,
                  void *notify_baton,
                  apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  const apr_array_header_t *children;
  int i;

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  /* Revert this node (depth=empty) if it matches one of the changelists.  */
  if (svn_wc__internal_changelist_match(db, local_abspath, changelist_hash,
                                        scratch_pool))
    SVN_ERR(revert(db, local_abspath,
                   svn_depth_empty, use_commit_times, clear_changelists,
                   metadata_only,
                   cancel_func, cancel_baton,
                   notify_func, notify_baton,
                   scratch_pool));

  if (depth == svn_depth_empty)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(scratch_pool);

  /* We can handle both depth=files and depth=immediates by setting
     depth=empty here.  We don't need to distinguish files and
     directories when making the recursive call because directories
     can never match a changelist, so making the recursive call for
     directories when asked for depth=files is a no-op. */
  if (depth == svn_depth_files || depth == svn_depth_immediates)
    depth = svn_depth_empty;

  SVN_ERR(svn_wc__db_read_children_of_working_node(&children, db,
                                                   local_abspath,
                                                   scratch_pool,
                                                   iterpool));
  for (i = 0; i < children->nelts; ++i)
    {
      const char *child_abspath;

      svn_pool_clear(iterpool);

      child_abspath = svn_dirent_join(local_abspath,
                                      APR_ARRAY_IDX(children, i,
                                                    const char *),
                                      iterpool);

      SVN_ERR(revert_changelist(db, child_abspath, depth,
                                use_commit_times, changelist_hash,
                                clear_changelists, metadata_only,
                                cancel_func, cancel_baton,
                                notify_func, notify_baton,
                                iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Does a partially recursive revert of LOCAL_ABSPATH to depth DEPTH
   (which must be either svn_depth_files or svn_depth_immediates) by
   doing a non-recursive revert on each permissible path.  Notifies
   all reverted paths.

   ### This won't revert a copied dir with one level of children since
   ### the non-recursive revert on the dir will fail.  Not sure how a
   ### partially recursive revert should handle actual-only nodes. */
static svn_error_t *
revert_partial(svn_wc__db_t *db,
               const char *local_abspath,
               svn_depth_t depth,
               svn_boolean_t use_commit_times,
               svn_boolean_t clear_changelists,
               svn_boolean_t metadata_only,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;
  const apr_array_header_t *children;
  int i;

  SVN_ERR_ASSERT(depth == svn_depth_files || depth == svn_depth_immediates);

  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  iterpool = svn_pool_create(scratch_pool);

  /* Revert the root node itself (depth=empty), then move on to the
     children.  */
  SVN_ERR(revert(db, local_abspath, svn_depth_empty,
                 use_commit_times, clear_changelists, metadata_only,
                 cancel_func, cancel_baton,
                 notify_func, notify_baton, iterpool));

  SVN_ERR(svn_wc__db_read_children_of_working_node(&children, db,
                                                   local_abspath,
                                                   scratch_pool,
                                                   iterpool));
  for (i = 0; i < children->nelts; ++i)
    {
      const char *child_abspath;

      svn_pool_clear(iterpool);

      child_abspath = svn_dirent_join(local_abspath,
                                      APR_ARRAY_IDX(children, i, const char *),
                                      iterpool);

      /* For svn_depth_files: don't revert non-files.  */
      if (depth == svn_depth_files)
        {
          svn_node_kind_t kind;

          SVN_ERR(svn_wc__db_read_kind(&kind, db, child_abspath,
                                       FALSE /* allow_missing */,
                                       TRUE /* show_deleted */,
                                       FALSE /* show_hidden */,
                                       iterpool));
          if (kind != svn_node_file)
            continue;
        }

      /* Revert just this node (depth=empty).  */
      SVN_ERR(revert(db, child_abspath,
                     svn_depth_empty, use_commit_times, clear_changelists,
                     metadata_only,
                     cancel_func, cancel_baton,
                     notify_func, notify_baton,
                     iterpool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_revert5(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_depth_t depth,
               svn_boolean_t use_commit_times,
               const apr_array_header_t *changelist_filter,
               svn_boolean_t clear_changelists,
               svn_boolean_t metadata_only,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool)
{
  if (changelist_filter && changelist_filter->nelts)
    {
      apr_hash_t *changelist_hash;

      SVN_ERR(svn_hash_from_cstring_keys(&changelist_hash, changelist_filter,
                                         scratch_pool));
      return svn_error_trace(revert_changelist(wc_ctx->db, local_abspath,
                                               depth, use_commit_times,
                                               changelist_hash,
                                               clear_changelists,
                                               metadata_only,
                                               cancel_func, cancel_baton,
                                               notify_func, notify_baton,
                                               scratch_pool));
    }

  if (depth == svn_depth_empty || depth == svn_depth_infinity)
    return svn_error_trace(revert(wc_ctx->db, local_abspath,
                                  depth, use_commit_times, clear_changelists,
                                  metadata_only,
                                  cancel_func, cancel_baton,
                                  notify_func, notify_baton,
                                  scratch_pool));

  /* The user may expect svn_depth_files/svn_depth_immediates to work
     on copied dirs with one level of children.  It doesn't, the user
     will get an error and will need to invoke an infinite revert.  If
     we identified those cases where svn_depth_infinity would not
     revert too much we could invoke the recursive call above. */

  if (depth == svn_depth_files || depth == svn_depth_immediates)
    return svn_error_trace(revert_partial(wc_ctx->db, local_abspath,
                                          depth, use_commit_times,
                                          clear_changelists, metadata_only,
                                          cancel_func, cancel_baton,
                                          notify_func, notify_baton,
                                          scratch_pool));

  /* Bogus depth. Tell the caller.  */
  return svn_error_create(SVN_ERR_WC_INVALID_OPERATION_DEPTH, NULL, NULL);
}
