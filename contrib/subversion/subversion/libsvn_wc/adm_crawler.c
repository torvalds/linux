/*
 * adm_crawler.c:  report local WC mods to an Editor.
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


#include <string.h>

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_hash.h>

#include "svn_hash.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_io.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"

#include "private/svn_wc_private.h"

#include "wc.h"
#include "adm_files.h"
#include "translate.h"
#include "workqueue.h"
#include "conflicts.h"

#include "svn_private_config.h"


/* Helper for report_revisions_and_depths().

   Perform an atomic restoration of the file LOCAL_ABSPATH; that is, copy
   the file's text-base to the administrative tmp area, and then move
   that file to LOCAL_ABSPATH with possible translations/expansions.  If
   USE_COMMIT_TIMES is set, then set working file's timestamp to
   last-commit-time.  Either way, set entry-timestamp to match that of
   the working file when all is finished.

   If MARK_RESOLVED_TEXT_CONFLICT is TRUE, mark as resolved any existing
   text conflict on LOCAL_ABSPATH.

   Not that a valid access baton with a write lock to the directory of
   LOCAL_ABSPATH must be available in DB.*/
static svn_error_t *
restore_file(svn_wc__db_t *db,
             const char *local_abspath,
             svn_boolean_t use_commit_times,
             svn_boolean_t mark_resolved_text_conflict,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             apr_pool_t *scratch_pool)
{
  svn_skel_t *work_item;

  SVN_ERR(svn_wc__wq_build_file_install(&work_item,
                                        db, local_abspath,
                                        NULL /* source_abspath */,
                                        use_commit_times,
                                        TRUE /* record_fileinfo */,
                                        scratch_pool, scratch_pool));
  /* ### we need an existing path for wq_add. not entirely WRI_ABSPATH yet  */
  SVN_ERR(svn_wc__db_wq_add(db,
                            svn_dirent_dirname(local_abspath, scratch_pool),
                            work_item, scratch_pool));

  /* Run the work item immediately.  */
  SVN_ERR(svn_wc__wq_run(db, local_abspath,
                         cancel_func, cancel_baton,
                         scratch_pool));

  /* Remove any text conflict */
  if (mark_resolved_text_conflict)
    SVN_ERR(svn_wc__mark_resolved_text_conflict(db, local_abspath,
                                                cancel_func, cancel_baton,
                                                scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_restore(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_boolean_t use_commit_times,
               apr_pool_t *scratch_pool)
{
  /* ### If ever revved: Add cancel func. */
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  svn_node_kind_t disk_kind;
  const svn_checksum_t *checksum;

  SVN_ERR(svn_io_check_path(local_abspath, &disk_kind, scratch_pool));

  if (disk_kind != svn_node_none)
    return svn_error_createf(SVN_ERR_WC_PATH_FOUND, NULL,
                             _("The existing node '%s' can not be restored."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, &checksum, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               wc_ctx->db, local_abspath,
                               scratch_pool, scratch_pool));

  if (status != svn_wc__db_status_normal
      && !((status == svn_wc__db_status_added
            || status == svn_wc__db_status_incomplete)
           && (kind == svn_node_dir
               || (kind == svn_node_file && checksum != NULL)
               /* || (kind == svn_node_symlink && target)*/)))
    {
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                               _("The node '%s' can not be restored."),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  if (kind == svn_node_file || kind == svn_node_symlink)
    SVN_ERR(restore_file(wc_ctx->db, local_abspath, use_commit_times,
                         FALSE /*mark_resolved_text_conflict*/,
                         NULL, NULL /* cancel func, baton */,
                         scratch_pool));
  else
    SVN_ERR(svn_io_dir_make(local_abspath, APR_OS_DEFAULT, scratch_pool));

  return SVN_NO_ERROR;
}

/* Try to restore LOCAL_ABSPATH of node type KIND and if successful,
   notify that the node is restored.  Use DB for accessing the working copy.
   If USE_COMMIT_TIMES is set, then set working file's timestamp to
   last-commit-time.

   This function does all temporary allocations in SCRATCH_POOL
 */
static svn_error_t *
restore_node(svn_wc__db_t *db,
             const char *local_abspath,
             svn_node_kind_t kind,
             svn_boolean_t mark_resolved_text_conflict,
             svn_boolean_t use_commit_times,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_wc_notify_func2_t notify_func,
             void *notify_baton,
             apr_pool_t *scratch_pool)
{
  if (kind == svn_node_file || kind == svn_node_symlink)
    {
      /* Recreate file from text-base; mark any text conflict as resolved */
      SVN_ERR(restore_file(db, local_abspath, use_commit_times,
                           mark_resolved_text_conflict,
                           cancel_func, cancel_baton,
                           scratch_pool));
    }
  else if (kind == svn_node_dir)
    {
      /* Recreating a directory is just a mkdir */
      SVN_ERR(svn_io_dir_make(local_abspath, APR_OS_DEFAULT, scratch_pool));
    }

  /* ... report the restoration to the caller.  */
  if (notify_func != NULL)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(local_abspath,
                                                     svn_wc_notify_restore,
                                                     scratch_pool);
      notify->kind = svn_node_file;
      (*notify_func)(notify_baton, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* The recursive crawler that describes a mixed-revision working
   copy to an RA layer.  Used to initiate updates.

   This is a depth-first recursive walk of the children of DIR_ABSPATH
   (not including DIR_ABSPATH itself) using DB.  Look at each node and
   check if its revision is different than DIR_REV.  If so, report this
   fact to REPORTER.  If a node has a different URL than expected, or
   a different depth than its parent, report that to REPORTER.

   Report DIR_ABSPATH to the reporter as REPORT_RELPATH.

   Alternatively, if REPORT_EVERYTHING is set, then report all
   children unconditionally.

   DEPTH is actually the *requested* depth for the update-like
   operation for which we are reporting working copy state.  However,
   certain requested depths affect the depth of the report crawl.  For
   example, if the requested depth is svn_depth_empty, there's no
   point descending into subdirs, no matter what their depths.  So:

   If DEPTH is svn_depth_empty, don't report any files and don't
   descend into any subdirs.  If svn_depth_files, report files but
   still don't descend into subdirs.  If svn_depth_immediates, report
   files, and report subdirs themselves but not their entries.  If
   svn_depth_infinity or svn_depth_unknown, report everything all the
   way down.  (That last sentence might sound counterintuitive, but
   since you can't go deeper than the local ambient depth anyway,
   requesting svn_depth_infinity really means "as deep as the various
   parts of this working copy go".  Of course, the information that
   comes back from the server will be different for svn_depth_unknown
   than for svn_depth_infinity.)

   DIR_REPOS_RELPATH, DIR_REPOS_ROOT and DIR_DEPTH are the repository
   relative path, the repository root and depth stored on the directory,
   passed here to avoid another database query.

   DEPTH_COMPATIBILITY_TRICK means the same thing here as it does
   in svn_wc_crawl_revisions5().

   If RESTORE_FILES is set, then unexpectedly missing working files
   will be restored from text-base and NOTIFY_FUNC/NOTIFY_BATON
   will be called to report the restoration.  USE_COMMIT_TIMES is
   passed to restore_file() helper. */
static svn_error_t *
report_revisions_and_depths(svn_wc__db_t *db,
                            const char *dir_abspath,
                            const char *report_relpath,
                            svn_revnum_t dir_rev,
                            const char *dir_repos_relpath,
                            const char *dir_repos_root,
                            svn_depth_t dir_depth,
                            const svn_ra_reporter3_t *reporter,
                            void *report_baton,
                            svn_boolean_t restore_files,
                            svn_depth_t depth,
                            svn_boolean_t honor_depth_exclude,
                            svn_boolean_t depth_compatibility_trick,
                            svn_boolean_t report_everything,
                            svn_boolean_t use_commit_times,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            svn_wc_notify_func2_t notify_func,
                            void *notify_baton,
                            apr_pool_t *scratch_pool)
{
  apr_hash_t *base_children;
  apr_hash_t *dirents;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;
  svn_error_t *err;


  /* Get both the SVN Entries and the actual on-disk entries.   Also
     notice that we're picking up hidden entries too (read_children never
     hides children). */
  SVN_ERR(svn_wc__db_base_get_children_info(&base_children, db, dir_abspath,
                                            scratch_pool, iterpool));

  if (restore_files)
    {
      err = svn_io_get_dirents3(&dirents, dir_abspath, TRUE,
                                scratch_pool, scratch_pool);

      if (err && (APR_STATUS_IS_ENOENT(err->apr_err)
                  || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)))
        {
          svn_error_clear(err);
          /* There is no directory, and if we could create the directory
             we would have already created it when walking the parent
             directory */
          restore_files = FALSE;
          dirents = NULL;
        }
      else
        SVN_ERR(err);
    }
  else
    dirents = NULL;

  /*** Do the real reporting and recursing. ***/

  /* Looping over current directory's BASE children: */
  for (hi = apr_hash_first(scratch_pool, base_children);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      const char *child = apr_hash_this_key(hi);
      const char *this_report_relpath;
      const char *this_abspath;
      svn_boolean_t this_switched = FALSE;
      struct svn_wc__db_base_info_t *ths = apr_hash_this_val(hi);

      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      /* Clear the iteration subpool here because the loop has a bunch
         of 'continue' jump statements. */
      svn_pool_clear(iterpool);

      /* Compute the paths and URLs we need. */
      this_report_relpath = svn_relpath_join(report_relpath, child, iterpool);
      this_abspath = svn_dirent_join(dir_abspath, child, iterpool);

      /*** File Externals **/
      if (ths->update_root)
        {
          /* File externals are ... special.  We ignore them. */;
          continue;
        }

      /* First check for exclusion */
      if (ths->status == svn_wc__db_status_excluded)
        {
          if (honor_depth_exclude)
            {
              /* Report the excluded path, no matter whether report_everything
                 flag is set.  Because the report_everything flag indicates
                 that the server will treat the wc as empty and thus push
                 full content of the files/subdirs. But we want to prevent the
                 server from pushing the full content of this_path at us. */

              /* The server does not support link_path report on excluded
                 path. We explicitly prohibit this situation in
                 svn_wc_crop_tree(). */
              SVN_ERR(reporter->set_path(report_baton,
                                         this_report_relpath,
                                         dir_rev,
                                         svn_depth_exclude,
                                         FALSE,
                                         NULL,
                                         iterpool));
            }
          else
            {
              /* We want to pull in the excluded target. So, report it as
                 deleted, and server will respond properly. */
              if (! report_everything)
                SVN_ERR(reporter->delete_path(report_baton,
                                              this_report_relpath, iterpool));
            }
          continue;
        }

      /*** The Big Tests: ***/
      if (ths->status == svn_wc__db_status_server_excluded
          || ths->status == svn_wc__db_status_not_present)
        {
          /* If the entry is 'absent' or 'not-present', make sure the server
             knows it's gone...
             ...unless we're reporting everything, in which case we're
             going to report it missing later anyway.

             This instructs the server to send it back to us, if it is
             now available (an addition after a not-present state), or if
             it is now authorized (change in authz for the absent item).  */
          if (! report_everything)
            SVN_ERR(reporter->delete_path(report_baton, this_report_relpath,
                                          iterpool));
          continue;
        }

      /* Is the entry NOT on the disk? We may be able to restore it.  */
      if (restore_files
          && svn_hash_gets(dirents, child) == NULL)
        {
          svn_wc__db_status_t wrk_status;
          svn_node_kind_t wrk_kind;
          const svn_checksum_t *checksum;
          svn_boolean_t conflicted;

          SVN_ERR(svn_wc__db_read_info(&wrk_status, &wrk_kind, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       &checksum, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, &conflicted,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       db, this_abspath, iterpool, iterpool));

          if ((wrk_status == svn_wc__db_status_normal
               || wrk_status == svn_wc__db_status_added
               || wrk_status == svn_wc__db_status_incomplete)
              && (wrk_kind == svn_node_dir || checksum))
            {
              svn_node_kind_t dirent_kind;

              /* It is possible on a case insensitive system that the
                 entry is not really missing, but just cased incorrectly.
                 In this case we can't overwrite it with the pristine
                 version */
              SVN_ERR(svn_io_check_path(this_abspath, &dirent_kind, iterpool));

              if (dirent_kind == svn_node_none)
                {
                  SVN_ERR(restore_node(db, this_abspath, wrk_kind,
                                       conflicted, use_commit_times,
                                       cancel_func, cancel_baton,
                                       notify_func, notify_baton, iterpool));
                }
            }
        }

      /* And finally prepare for reporting */
      if (!ths->repos_relpath)
        {
          ths->repos_relpath = svn_relpath_join(dir_repos_relpath, child,
                                                iterpool);
        }
      else
        {
          const char *childname
            = svn_relpath_skip_ancestor(dir_repos_relpath, ths->repos_relpath);

          if (childname == NULL || strcmp(childname, child) != 0)
            {
              this_switched = TRUE;
            }
        }

      /* Tweak THIS_DEPTH to a useful value.  */
      if (ths->depth == svn_depth_unknown)
        ths->depth = svn_depth_infinity;

      /*** Files ***/
      if (ths->kind == svn_node_file
          || ths->kind == svn_node_symlink)
        {
          if (report_everything)
            {
              /* Report the file unconditionally, one way or another. */
              if (this_switched)
                SVN_ERR(reporter->link_path(report_baton,
                                            this_report_relpath,
                                            svn_path_url_add_component2(
                                                dir_repos_root,
                                                ths->repos_relpath, iterpool),
                                            ths->revnum,
                                            ths->depth,
                                            FALSE,
                                            ths->lock ? ths->lock->token : NULL,
                                            iterpool));
              else
                SVN_ERR(reporter->set_path(report_baton,
                                           this_report_relpath,
                                           ths->revnum,
                                           ths->depth,
                                           FALSE,
                                           ths->lock ? ths->lock->token : NULL,
                                           iterpool));
            }

          /* Possibly report a disjoint URL ... */
          else if (this_switched)
            SVN_ERR(reporter->link_path(report_baton,
                                        this_report_relpath,
                                        svn_path_url_add_component2(
                                                dir_repos_root,
                                                ths->repos_relpath, iterpool),
                                        ths->revnum,
                                        ths->depth,
                                        FALSE,
                                        ths->lock ? ths->lock->token : NULL,
                                        iterpool));
          /* ... or perhaps just a differing revision or lock token,
             or the mere presence of the file in a depth-empty dir. */
          else if (ths->revnum != dir_rev
                   || ths->lock
                   || dir_depth == svn_depth_empty)
            SVN_ERR(reporter->set_path(report_baton,
                                       this_report_relpath,
                                       ths->revnum,
                                       ths->depth,
                                       FALSE,
                                       ths->lock ? ths->lock->token : NULL,
                                       iterpool));
        } /* end file case */

      /*** Directories (in recursive mode) ***/
      else if (ths->kind == svn_node_dir
               && (depth > svn_depth_files
                   || depth == svn_depth_unknown))
        {
          svn_boolean_t is_incomplete;
          svn_boolean_t start_empty;
          svn_depth_t report_depth = ths->depth;

          is_incomplete = (ths->status == svn_wc__db_status_incomplete);
          start_empty = is_incomplete;

          if (!SVN_DEPTH_IS_RECURSIVE(depth))
            report_depth = svn_depth_empty;

          /* When a <= 1.6 working copy is upgraded without some of its
             subdirectories we miss some information in the database. If we
             report the revision as -1, the update editor will receive an
             add_directory() while it still knows the directory.

             This would raise strange tree conflicts and probably assertions
             as it would a BASE vs BASE conflict */
          if (is_incomplete && !SVN_IS_VALID_REVNUM(ths->revnum))
            ths->revnum = dir_rev;

          if (depth_compatibility_trick
              && ths->depth <= svn_depth_files
              && depth > ths->depth)
            {
              start_empty = TRUE;
            }

          if (report_everything)
            {
              /* Report the dir unconditionally, one way or another... */
              if (this_switched)
                SVN_ERR(reporter->link_path(report_baton,
                                            this_report_relpath,
                                            svn_path_url_add_component2(
                                                dir_repos_root,
                                                ths->repos_relpath, iterpool),
                                            ths->revnum,
                                            report_depth,
                                            start_empty,
                                            ths->lock ? ths->lock->token
                                                      : NULL,
                                            iterpool));
              else
                SVN_ERR(reporter->set_path(report_baton,
                                           this_report_relpath,
                                           ths->revnum,
                                           report_depth,
                                           start_empty,
                                           ths->lock ? ths->lock->token : NULL,
                                           iterpool));
            }
          else if (this_switched)
            {
              /* ...or possibly report a disjoint URL ... */
              SVN_ERR(reporter->link_path(report_baton,
                                          this_report_relpath,
                                          svn_path_url_add_component2(
                                              dir_repos_root,
                                              ths->repos_relpath, iterpool),
                                          ths->revnum,
                                          report_depth,
                                          start_empty,
                                          ths->lock ? ths->lock->token : NULL,
                                          iterpool));
            }
          else if (ths->revnum != dir_rev
                   || ths->lock
                   || is_incomplete
                   || dir_depth == svn_depth_empty
                   || dir_depth == svn_depth_files
                   || (dir_depth == svn_depth_immediates
                       && ths->depth != svn_depth_empty)
                   || (ths->depth < svn_depth_infinity
                       && SVN_DEPTH_IS_RECURSIVE(depth)))
            {
              /* ... or perhaps just a differing revision, lock token,
                 incomplete subdir, the mere presence of the directory
                 in a depth-empty or depth-files dir, or if the parent
                 dir is at depth-immediates but the child is not at
                 depth-empty.  Also describe shallow subdirs if we are
                 trying to set depth to infinity. */
              SVN_ERR(reporter->set_path(report_baton,
                                         this_report_relpath,
                                         ths->revnum,
                                         report_depth,
                                         start_empty,
                                         ths->lock ? ths->lock->token : NULL,
                                         iterpool));
            }

          /* Finally, recurse if necessary and appropriate. */
          if (SVN_DEPTH_IS_RECURSIVE(depth))
            {
              const char *repos_relpath = ths->repos_relpath;

              if (repos_relpath == NULL)
                {
                  repos_relpath = svn_relpath_join(dir_repos_relpath, child,
                                                   iterpool);
                }

              SVN_ERR(report_revisions_and_depths(db,
                                                  this_abspath,
                                                  this_report_relpath,
                                                  ths->revnum,
                                                  repos_relpath,
                                                  dir_repos_root,
                                                  ths->depth,
                                                  reporter, report_baton,
                                                  restore_files, depth,
                                                  honor_depth_exclude,
                                                  depth_compatibility_trick,
                                                  start_empty,
                                                  use_commit_times,
                                                  cancel_func, cancel_baton,
                                                  notify_func, notify_baton,
                                                  iterpool));
            }
        } /* end directory case */
    } /* end main entries loop */

  /* We're done examining this dir's entries, so free everything. */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/*------------------------------------------------------------------*/
/*** Public Interfaces ***/


svn_error_t *
svn_wc_crawl_revisions5(svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        const svn_ra_reporter3_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_depth_t depth,
                        svn_boolean_t honor_depth_exclude,
                        svn_boolean_t depth_compatibility_trick,
                        svn_boolean_t use_commit_times,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = wc_ctx->db;
  svn_error_t *fserr, *err;
  svn_revnum_t target_rev = SVN_INVALID_REVNUM;
  svn_boolean_t start_empty;
  svn_wc__db_status_t status;
  svn_node_kind_t target_kind;
  const char *repos_relpath, *repos_root_url;
  svn_depth_t target_depth;
  svn_wc__db_lock_t *target_lock;
  svn_node_kind_t disk_kind;
  svn_depth_t report_depth;
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Get the base rev, which is the first revnum that entries will be
     compared to, and some other WC info about the target. */
  err = svn_wc__db_base_get_info(&status, &target_kind, &target_rev,
                                 &repos_relpath, &repos_root_url,
                                 NULL, NULL, NULL, NULL, &target_depth,
                                 NULL, NULL, &target_lock,
                                 NULL, NULL, NULL,
                                 db, local_abspath, scratch_pool,
                                 scratch_pool);

  if (err
      || (status != svn_wc__db_status_normal
          && status != svn_wc__db_status_incomplete))
    {
      if (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);

      svn_error_clear(err);

      /* We don't know about this node, so all we have to do is tell
         the reporter that we don't know this node.

         But first we have to start the report by sending some basic
         information for the root. */

      if (depth == svn_depth_unknown)
        depth = svn_depth_infinity;

      SVN_ERR(reporter->set_path(report_baton, "", 0, depth, FALSE,
                                 NULL, scratch_pool));
      SVN_ERR(reporter->delete_path(report_baton, "", scratch_pool));

      /* Finish the report, which causes the update editor to be
         driven. */
      SVN_ERR(reporter->finish_report(report_baton, scratch_pool));

      return SVN_NO_ERROR;
    }

  if (target_depth == svn_depth_unknown)
    target_depth = svn_depth_infinity;

  start_empty = (status == svn_wc__db_status_incomplete);
  if (depth_compatibility_trick
      && target_depth <= svn_depth_immediates
      && depth > target_depth)
    {
      start_empty = TRUE;
    }

  if (restore_files)
    SVN_ERR(svn_io_check_path(local_abspath, &disk_kind, scratch_pool));
  else
    disk_kind = svn_node_unknown;

  /* Determine if there is a missing node that should be restored */
  if (restore_files
      && disk_kind == svn_node_none)
    {
      svn_wc__db_status_t wrk_status;
      svn_node_kind_t wrk_kind;
      const svn_checksum_t *checksum;
      svn_boolean_t conflicted;

      err = svn_wc__db_read_info(&wrk_status, &wrk_kind, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, &checksum, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, &conflicted, NULL, NULL, NULL, NULL,
                                 NULL, NULL,
                                 db, local_abspath,
                                 scratch_pool, scratch_pool);


      if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          wrk_status = svn_wc__db_status_not_present;
          wrk_kind = svn_node_file;
        }
      else
        SVN_ERR(err);

      if ((wrk_status == svn_wc__db_status_normal
          || wrk_status == svn_wc__db_status_added
          || wrk_status == svn_wc__db_status_incomplete)
          && (wrk_kind == svn_node_dir || checksum))
        {
          SVN_ERR(restore_node(wc_ctx->db, local_abspath,
                               wrk_kind, conflicted, use_commit_times,
                               cancel_func, cancel_baton,
                               notify_func, notify_baton,
                               scratch_pool));
        }
    }

  {
    report_depth = target_depth;

    if (honor_depth_exclude
        && depth != svn_depth_unknown
        && depth < target_depth)
      report_depth = depth;

    /* The first call to the reporter merely informs it that the
       top-level directory being updated is at BASE_REV.  Its PATH
       argument is ignored. */
    SVN_ERR(reporter->set_path(report_baton, "", target_rev, report_depth,
                               start_empty, NULL, scratch_pool));
  }
  if (target_kind == svn_node_dir)
    {
      if (depth != svn_depth_empty)
        {
          /* Recursively crawl ROOT_DIRECTORY and report differing
             revisions. */
          err = report_revisions_and_depths(wc_ctx->db,
                                            local_abspath,
                                            "",
                                            target_rev,
                                            repos_relpath,
                                            repos_root_url,
                                            report_depth,
                                            reporter, report_baton,
                                            restore_files, depth,
                                            honor_depth_exclude,
                                            depth_compatibility_trick,
                                            start_empty,
                                            use_commit_times,
                                            cancel_func, cancel_baton,
                                            notify_func, notify_baton,
                                            scratch_pool);
          if (err)
            goto abort_report;
        }
    }

  else if (target_kind == svn_node_file || target_kind == svn_node_symlink)
    {
      const char *parent_abspath, *base;
      svn_wc__db_status_t parent_status;
      const char *parent_repos_relpath;

      svn_dirent_split(&parent_abspath, &base, local_abspath,
                       scratch_pool);

      /* We can assume a file is in the same repository as its parent
         directory, so we only look at the relpath. */
      err = svn_wc__db_base_get_info(&parent_status, NULL, NULL,
                                     &parent_repos_relpath, NULL, NULL, NULL,
                                     NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL,
                                     db, parent_abspath,
                                     scratch_pool, scratch_pool);

      if (err)
        goto abort_report;

      if (strcmp(repos_relpath,
                 svn_relpath_join(parent_repos_relpath, base,
                                  scratch_pool)) != 0)
        {
          /* This file is disjoint with respect to its parent
             directory.  Since we are looking at the actual target of
             the report (not some file in a subdirectory of a target
             directory), and that target is a file, we need to pass an
             empty string to link_path. */
          err = reporter->link_path(report_baton,
                                    "",
                                    svn_path_url_add_component2(
                                                    repos_root_url,
                                                    repos_relpath,
                                                    scratch_pool),
                                    target_rev,
                                    svn_depth_infinity,
                                    FALSE,
                                    target_lock ? target_lock->token : NULL,
                                    scratch_pool);
          if (err)
            goto abort_report;
        }
      else if (target_lock)
        {
          /* If this entry is a file node, we just want to report that
             node's revision.  Since we are looking at the actual target
             of the report (not some file in a subdirectory of a target
             directory), and that target is a file, we need to pass an
             empty string to set_path. */
          err = reporter->set_path(report_baton, "", target_rev,
                                   svn_depth_infinity,
                                   FALSE,
                                   target_lock ? target_lock->token : NULL,
                                   scratch_pool);
          if (err)
            goto abort_report;
        }
    }

  /* Finish the report, which causes the update editor to be driven. */
  return svn_error_trace(reporter->finish_report(report_baton, scratch_pool));

 abort_report:
  /* Clean up the fs transaction. */
  if ((fserr = reporter->abort_report(report_baton, scratch_pool)))
    {
      fserr = svn_error_quick_wrap(fserr, _("Error aborting report"));
      svn_error_compose(err, fserr);
    }
  return svn_error_trace(err);
}

/*** Copying stream ***/

/* A copying stream is a bit like the unix tee utility:
 *
 * It reads the SOURCE when asked for data and while returning it,
 * also writes the same data to TARGET.
 */
struct copying_stream_baton
{
  /* Stream to read input from. */
  svn_stream_t *source;

  /* Stream to write all data read to. */
  svn_stream_t *target;
};


/* */
static svn_error_t *
read_handler_copy(void *baton, char *buffer, apr_size_t *len)
{
  struct copying_stream_baton *btn = baton;

  SVN_ERR(svn_stream_read_full(btn->source, buffer, len));

  return svn_stream_write(btn->target, buffer, len);
}

/* */
static svn_error_t *
close_handler_copy(void *baton)
{
  struct copying_stream_baton *btn = baton;

  SVN_ERR(svn_stream_close(btn->target));
  return svn_stream_close(btn->source);
}

/* Implements svn_stream_seek_fn_t */
static svn_error_t *
seek_handler_copy(void *baton, const svn_stream_mark_t *mark)
{
  struct copying_stream_baton *btn = baton;

  /* Only reset support. */
  if (mark)
    {
      return svn_error_create(SVN_ERR_STREAM_SEEK_NOT_SUPPORTED,
                              NULL, NULL);
    }
  else
    {
      SVN_ERR(svn_stream_reset(btn->source));
      SVN_ERR(svn_stream_reset(btn->target));
    }

  return SVN_NO_ERROR;
}


/* Return a stream - allocated in POOL - which reads its input
 * from SOURCE and, while returning that to the caller, at the
 * same time writes that to TARGET.
 */
static svn_stream_t *
copying_stream(svn_stream_t *source,
               svn_stream_t *target,
               apr_pool_t *pool)
{
  struct copying_stream_baton *baton;
  svn_stream_t *stream;

  baton = apr_palloc(pool, sizeof (*baton));
  baton->source = source;
  baton->target = target;

  stream = svn_stream_create(baton, pool);
  svn_stream_set_read2(stream, NULL /* only full read support */,
                       read_handler_copy);
  svn_stream_set_close(stream, close_handler_copy);

  if (svn_stream_supports_reset(source) && svn_stream_supports_reset(target))
    {
      svn_stream_set_seek(stream, seek_handler_copy);
    }

  return stream;
}


/* Set *STREAM to a stream from which the caller can read the pristine text
 * of the working version of the file at LOCAL_ABSPATH.  If the working
 * version of LOCAL_ABSPATH has no pristine text because it is locally
 * added, set *STREAM to an empty stream.  If the working version of
 * LOCAL_ABSPATH is not a file, return an error.
 *
 * Set *EXPECTED_MD5_CHECKSUM to the recorded MD5 checksum.
 *
 * Arrange for the actual checksum of the text to be calculated and written
 * into *ACTUAL_MD5_CHECKSUM when the stream is read.
 */
static svn_error_t *
read_and_checksum_pristine_text(svn_stream_t **stream,
                                const svn_checksum_t **expected_md5_checksum,
                                svn_checksum_t **actual_md5_checksum,
                                svn_wc__db_t *db,
                                const char *local_abspath,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_stream_t *base_stream;

  SVN_ERR(svn_wc__get_pristine_contents(&base_stream, NULL, db, local_abspath,
                                        result_pool, scratch_pool));
  if (base_stream == NULL)
    {
      base_stream = svn_stream_empty(result_pool);
      *expected_md5_checksum = NULL;
      *actual_md5_checksum = NULL;
    }
  else
    {
      const svn_checksum_t *expected_md5;

      SVN_ERR(svn_wc__db_read_info(NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, &expected_md5,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL,
                                   db, local_abspath,
                                   result_pool, scratch_pool));
      if (expected_md5 == NULL)
        return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                 _("Pristine checksum for file '%s' is missing"),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));
      if (expected_md5->kind != svn_checksum_md5)
        SVN_ERR(svn_wc__db_pristine_get_md5(&expected_md5, db, local_abspath,
                                            expected_md5,
                                            result_pool, scratch_pool));
      *expected_md5_checksum = expected_md5;

      /* Arrange to set ACTUAL_MD5_CHECKSUM to the MD5 of what is *actually*
         found when the base stream is read. */
      base_stream = svn_stream_checksummed2(base_stream, actual_md5_checksum,
                                            NULL, svn_checksum_md5, TRUE,
                                            result_pool);
    }

  *stream = base_stream;
  return SVN_NO_ERROR;
}

typedef struct open_txdelta_stream_baton_t
{
  svn_boolean_t need_reset;
  svn_stream_t *base_stream;
  svn_stream_t *local_stream;
} open_txdelta_stream_baton_t;

/* Implements svn_txdelta_stream_open_func_t */
static svn_error_t *
open_txdelta_stream(svn_txdelta_stream_t **txdelta_stream_p,
                    void *baton,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  open_txdelta_stream_baton_t *b = baton;

  if (b->need_reset)
    {
      /* Under rare circumstances, we can be restarted and would need to
       * supply the delta stream again.  In this case, reset both streams. */
      SVN_ERR(svn_stream_reset(b->base_stream));
      SVN_ERR(svn_stream_reset(b->local_stream));
    }

  svn_txdelta2(txdelta_stream_p, b->base_stream, b->local_stream,
               FALSE, result_pool);
  b->need_reset = TRUE;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__internal_transmit_text_deltas(svn_stream_t *tempstream,
                                      const svn_checksum_t **new_text_base_md5_checksum,
                                      const svn_checksum_t **new_text_base_sha1_checksum,
                                      svn_wc__db_t *db,
                                      const char *local_abspath,
                                      svn_boolean_t fulltext,
                                      const svn_delta_editor_t *editor,
                                      void *file_baton,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  const svn_checksum_t *expected_md5_checksum;  /* recorded MD5 of BASE_S. */
  svn_checksum_t *verify_checksum;  /* calc'd MD5 of BASE_STREAM */
  svn_checksum_t *local_md5_checksum;  /* calc'd MD5 of LOCAL_STREAM */
  svn_checksum_t *local_sha1_checksum;  /* calc'd SHA1 of LOCAL_STREAM */
  svn_wc__db_install_data_t *install_data = NULL;
  svn_error_t *err;
  svn_error_t *err2;
  svn_stream_t *base_stream;  /* delta source */
  svn_stream_t *local_stream;  /* delta target: LOCAL_ABSPATH transl. to NF */

  /* Translated input */
  SVN_ERR(svn_wc__internal_translated_stream(&local_stream, db,
                                             local_abspath, local_abspath,
                                             SVN_WC_TRANSLATE_TO_NF,
                                             scratch_pool, scratch_pool));

  /* If the caller wants a copy of the working file translated to
   * repository-normal form, make the copy by tee-ing the TEMPSTREAM.
   * This is only needed for the 1.6 API.  Even when using the 1.6 API
   * this temporary file is not used by the functions that would have used
   * it when using the 1.6 code.  It's possible that 3rd party users (if
   * there are any) might expect this file to be a text-base. */
  if (tempstream)
    {
      /* Wrap the translated stream with a new stream that writes the
         translated contents into the new text base file as we read from it.
         Note that the new text base file will be closed when the new stream
         is closed. */
      local_stream = copying_stream(local_stream, tempstream, scratch_pool);
    }
  if (new_text_base_sha1_checksum)
    {
      svn_stream_t *new_pristine_stream;

      SVN_ERR(svn_wc__db_pristine_prepare_install(&new_pristine_stream,
                                                  &install_data,
                                                  &local_sha1_checksum, NULL,
                                                  db, local_abspath,
                                                  scratch_pool, scratch_pool));
      local_stream = copying_stream(local_stream, new_pristine_stream,
                                    scratch_pool);
    }

  /* If sending a full text is requested, or if there is no pristine text
   * (e.g. the node is locally added), then set BASE_STREAM to an empty
   * stream and leave EXPECTED_MD5_CHECKSUM and VERIFY_CHECKSUM as NULL.
   *
   * Otherwise, set BASE_STREAM to a stream providing the base (source) text
   * for the delta, set EXPECTED_MD5_CHECKSUM to its stored MD5 checksum,
   * and arrange for its VERIFY_CHECKSUM to be calculated later. */
  if (! fulltext)
    {
      /* We will be computing a delta against the pristine contents */
      /* We need the expected checksum to be an MD-5 checksum rather than a
       * SHA-1 because we want to pass it to apply_textdelta(). */
      SVN_ERR(read_and_checksum_pristine_text(&base_stream,
                                              &expected_md5_checksum,
                                              &verify_checksum,
                                              db, local_abspath,
                                              scratch_pool, scratch_pool));
    }
  else
    {
      /* Send a fulltext. */
      base_stream = svn_stream_empty(scratch_pool);
      expected_md5_checksum = NULL;
      verify_checksum = NULL;
    }

  /* Arrange the stream to calculate the resulting MD5. */
  local_stream = svn_stream_checksummed2(local_stream, &local_md5_checksum,
                                         NULL, svn_checksum_md5, TRUE,
                                         scratch_pool);

  /* Tell the editor to apply a textdelta stream to the file baton. */
  {
    open_txdelta_stream_baton_t baton = { 0 };

    /* apply_textdelta_stream() is working against a base with this checksum */
    const char *base_digest_hex = NULL;

    if (expected_md5_checksum)
      /* ### Why '..._display()'?  expected_md5_checksum should never be all-
       * zero, but if it is, we would want to pass NULL not an all-zero
       * digest to apply_textdelta_stream(), wouldn't we? */
      base_digest_hex = svn_checksum_to_cstring_display(expected_md5_checksum,
                                                        scratch_pool);

    baton.need_reset = FALSE;
    baton.base_stream = svn_stream_disown(base_stream, scratch_pool);
    baton.local_stream = svn_stream_disown(local_stream, scratch_pool);
    err = editor->apply_textdelta_stream(editor, file_baton, base_digest_hex,
                                         open_txdelta_stream, &baton,
                                         scratch_pool);
  }

  /* Close the two streams to force writing the digest */
  err2 = svn_stream_close(base_stream);
  if (err2)
    {
      /* Set verify_checksum to NULL if svn_stream_close() returns error
         because checksum will be uninitialized in this case. */
      verify_checksum = NULL;
      err = svn_error_compose_create(err, err2);
    }

  err = svn_error_compose_create(err, svn_stream_close(local_stream));

  /* If we have an error, it may be caused by a corrupt text base,
     so check the checksum. */
  if (expected_md5_checksum && verify_checksum
      && !svn_checksum_match(expected_md5_checksum, verify_checksum))
    {
      /* The entry checksum does not match the actual text
         base checksum.  Extreme badness. Of course,
         theoretically we could just switch to
         fulltext transmission here, and everything would
         work fine; after all, we're going to replace the
         text base with a new one in a moment anyway, and
         we'd fix the checksum then.  But it's better to
         error out.  People should know that their text
         bases are getting corrupted, so they can
         investigate.  Other commands could be affected,
         too, such as `svn diff'.  */

      err = svn_error_compose_create(
              svn_checksum_mismatch_err(expected_md5_checksum, verify_checksum,
                            scratch_pool,
                            _("Checksum mismatch for text base of '%s'"),
                            svn_dirent_local_style(local_abspath,
                                                   scratch_pool)),
              err);

      return svn_error_create(SVN_ERR_WC_CORRUPT_TEXT_BASE, err, NULL);
    }

  /* Now, handle that delta transmission error if any, so we can stop
     thinking about it after this point. */
  SVN_ERR_W(err, apr_psprintf(scratch_pool,
                              _("While preparing '%s' for commit"),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool)));

  if (new_text_base_md5_checksum)
    *new_text_base_md5_checksum = svn_checksum_dup(local_md5_checksum,
                                                   result_pool);
  if (new_text_base_sha1_checksum)
    {
      SVN_ERR(svn_wc__db_pristine_install(install_data,
                                          local_sha1_checksum,
                                          local_md5_checksum,
                                          scratch_pool));
      *new_text_base_sha1_checksum = svn_checksum_dup(local_sha1_checksum,
                                                      result_pool);
    }

  /* Close the file baton, and get outta here. */
  return svn_error_trace(
             editor->close_file(file_baton,
                                svn_checksum_to_cstring(local_md5_checksum,
                                                        scratch_pool),
                                scratch_pool));
}

svn_error_t *
svn_wc_transmit_text_deltas3(const svn_checksum_t **new_text_base_md5_checksum,
                             const svn_checksum_t **new_text_base_sha1_checksum,
                             svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             svn_boolean_t fulltext,
                             const svn_delta_editor_t *editor,
                             void *file_baton,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  return svn_wc__internal_transmit_text_deltas(NULL,
                                               new_text_base_md5_checksum,
                                               new_text_base_sha1_checksum,
                                               wc_ctx->db, local_abspath,
                                               fulltext, editor,
                                               file_baton, result_pool,
                                               scratch_pool);
}

svn_error_t *
svn_wc__internal_transmit_prop_deltas(svn_wc__db_t *db,
                                     const char *local_abspath,
                                     const svn_delta_editor_t *editor,
                                     void *baton,
                                     apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;
  apr_array_header_t *propmods;
  svn_node_kind_t kind;

  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath,
                               FALSE /* allow_missing */,
                               FALSE /* show_deleted */,
                               FALSE /* show_hidden */,
                               iterpool));

  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath, iterpool));

  /* Get an array of local changes by comparing the hashes. */
  SVN_ERR(svn_wc__internal_propdiff(&propmods, NULL, db, local_abspath,
                                    scratch_pool, iterpool));

  /* Apply each local change to the baton */
  for (i = 0; i < propmods->nelts; i++)
    {
      const svn_prop_t *p = &APR_ARRAY_IDX(propmods, i, svn_prop_t);

      svn_pool_clear(iterpool);

      if (kind == svn_node_file)
        SVN_ERR(editor->change_file_prop(baton, p->name, p->value,
                                         iterpool));
      else
        SVN_ERR(editor->change_dir_prop(baton, p->name, p->value,
                                        iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_transmit_prop_deltas2(svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             const svn_delta_editor_t *editor,
                             void *baton,
                             apr_pool_t *scratch_pool)
{
  return svn_wc__internal_transmit_prop_deltas(wc_ctx->db, local_abspath,
                                               editor, baton, scratch_pool);
}
