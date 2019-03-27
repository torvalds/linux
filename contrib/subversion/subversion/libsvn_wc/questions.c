/*
 * questions.c:  routines for asking questions about working copies
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

#include <apr_pools.h>
#include <apr_file_io.h>
#include <apr_file_info.h>
#include <apr_time.h>

#include "svn_pools.h"
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_time.h"
#include "svn_io.h"
#include "svn_props.h"

#include "wc.h"
#include "conflicts.h"
#include "translate.h"
#include "wc_db.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"



/*** svn_wc_text_modified_p ***/

/* svn_wc_text_modified_p answers the question:

   "Are the contents of F different than the contents of
   .svn/text-base/F.svn-base or .svn/tmp/text-base/F.svn-base?"

   In the first case, we're looking to see if a user has made local
   modifications to a file since the last update or commit.  In the
   second, the file may not be versioned yet (it doesn't exist in
   entries).  Support for the latter case came about to facilitate
   forced checkouts, updates, and switches, where an unversioned file
   may obstruct a file about to be added.

   Note: Assuming that F lives in a directory D at revision V, please
   notice that we are *NOT* answering the question, "are the contents
   of F different than revision V of F?"  While F may be at a different
   revision number than its parent directory, but we're only looking
   for local edits on F, not for consistent directory revisions.

   TODO:  the logic of the routines on this page might change in the
   future, as they bear some relation to the user interface.  For
   example, if a file is removed -- without telling subversion about
   it -- how should subversion react?  Should it copy the file back
   out of text-base?  Should it ask whether one meant to officially
   mark it for removal?
*/


/* Set *MODIFIED_P to TRUE if (after translation) VERSIONED_FILE_ABSPATH
 * (of VERSIONED_FILE_SIZE bytes) differs from PRISTINE_STREAM (of
 * PRISTINE_SIZE bytes), else to FALSE if not.
 *
 * If EXACT_COMPARISON is FALSE, translate VERSIONED_FILE_ABSPATH's EOL
 * style and keywords to repository-normal form according to its properties,
 * and compare the result with PRISTINE_STREAM.  If EXACT_COMPARISON is
 * TRUE, translate PRISTINE_STREAM's EOL style and keywords to working-copy
 * form according to VERSIONED_FILE_ABSPATH's properties, and compare the
 * result with VERSIONED_FILE_ABSPATH.
 *
 * HAS_PROPS should be TRUE if the file had properties when it was not
 * modified, otherwise FALSE.
 *
 * PROPS_MOD should be TRUE if the file's properties have been changed,
 * otherwise FALSE.
 *
 * PRISTINE_STREAM will be closed before a successful return.
 *
 * DB is a wc_db; use SCRATCH_POOL for temporary allocation.
 */
static svn_error_t *
compare_and_verify(svn_boolean_t *modified_p,
                   svn_wc__db_t *db,
                   const char *versioned_file_abspath,
                   svn_filesize_t versioned_file_size,
                   svn_stream_t *pristine_stream,
                   svn_filesize_t pristine_size,
                   svn_boolean_t has_props,
                   svn_boolean_t props_mod,
                   svn_boolean_t exact_comparison,
                   apr_pool_t *scratch_pool)
{
  svn_boolean_t same;
  svn_subst_eol_style_t eol_style;
  const char *eol_str;
  apr_hash_t *keywords;
  svn_boolean_t special = FALSE;
  svn_boolean_t need_translation;
  svn_stream_t *v_stream; /* versioned_file */

  SVN_ERR_ASSERT(svn_dirent_is_absolute(versioned_file_abspath));

  if (props_mod)
    has_props = TRUE; /* Maybe it didn't have properties; but it has now */

  if (has_props)
    {
      SVN_ERR(svn_wc__get_translate_info(&eol_style, &eol_str,
                                         &keywords,
                                         &special,
                                         db, versioned_file_abspath, NULL,
                                         !exact_comparison,
                                         scratch_pool, scratch_pool));

      need_translation = svn_subst_translation_required(eol_style, eol_str,
                                                        keywords, special,
                                                        TRUE);
    }
  else
    need_translation = FALSE;

  if (! need_translation
      && (versioned_file_size != pristine_size))
    {
      *modified_p = TRUE;

      /* ### Why did we open the pristine? */
      return svn_error_trace(svn_stream_close(pristine_stream));
    }

  /* ### Other checks possible? */

  /* Reading files is necessary. */
  if (special && need_translation)
    {
      SVN_ERR(svn_subst_read_specialfile(&v_stream, versioned_file_abspath,
                                          scratch_pool, scratch_pool));
    }
  else
    {
      /* We don't use APR-level buffering because the comparison function
       * will do its own buffering. */
      apr_file_t *file;
      SVN_ERR(svn_io_file_open(&file, versioned_file_abspath, APR_READ,
                               APR_OS_DEFAULT, scratch_pool));
      v_stream = svn_stream_from_aprfile2(file, FALSE, scratch_pool);

      if (need_translation)
        {
          if (!exact_comparison)
            {
              if (eol_style == svn_subst_eol_style_native)
                eol_str = SVN_SUBST_NATIVE_EOL_STR;
              else if (eol_style != svn_subst_eol_style_fixed
                       && eol_style != svn_subst_eol_style_none)
                return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL,
                                        svn_stream_close(v_stream), NULL);

              /* Wrap file stream to detranslate into normal form,
               * "repairing" the EOL style if it is inconsistent. */
              v_stream = svn_subst_stream_translated(v_stream,
                                                     eol_str,
                                                     TRUE /* repair */,
                                                     keywords,
                                                     FALSE /* expand */,
                                                     scratch_pool);
            }
          else
            {
              /* Wrap base stream to translate into working copy form, and
               * arrange to throw an error if its EOL style is inconsistent. */
              pristine_stream = svn_subst_stream_translated(pristine_stream,
                                                            eol_str, FALSE,
                                                            keywords, TRUE,
                                                            scratch_pool);
            }
        }
    }

  SVN_ERR(svn_stream_contents_same2(&same, pristine_stream, v_stream,
                                    scratch_pool));

  *modified_p = (! same);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__internal_file_modified_p(svn_boolean_t *modified_p,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 svn_boolean_t exact_comparison,
                                 apr_pool_t *scratch_pool)
{
  svn_stream_t *pristine_stream;
  svn_filesize_t pristine_size;
  svn_wc__db_status_t status;
  svn_node_kind_t kind;
  const svn_checksum_t *checksum;
  svn_filesize_t recorded_size;
  apr_time_t recorded_mod_time;
  svn_boolean_t has_props;
  svn_boolean_t props_mod;
  const svn_io_dirent2_t *dirent;

  /* Read the relevant info */
  SVN_ERR(svn_wc__db_read_info(&status, &kind, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, &checksum, NULL, NULL, NULL,
                               NULL, NULL, NULL,
                               &recorded_size, &recorded_mod_time,
                               NULL, NULL, NULL, &has_props, &props_mod,
                               NULL, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));

  /* If we don't have a pristine or the node has a status that allows a
     pristine, just say that the node is modified */
  if (!checksum
      || (kind != svn_node_file)
      || ((status != svn_wc__db_status_normal)
          && (status != svn_wc__db_status_added)))
    {
      *modified_p = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_io_stat_dirent2(&dirent, local_abspath, FALSE, TRUE,
                              scratch_pool, scratch_pool));

  if (dirent->kind != svn_node_file)
    {
      /* There is no file on disk, so the text is missing, not modified. */
      *modified_p = FALSE;
      return SVN_NO_ERROR;
    }

  if (! exact_comparison)
    {
      /* We're allowed to use a heuristic to determine whether files may
         have changed.  The heuristic has these steps:

         1. Compare the working file's size
            with the size cached in the entries file
         2. If they differ, do a full file compare
         3. Compare the working file's timestamp
            with the timestamp cached in the entries file
         4. If they differ, do a full file compare
         5. Otherwise, return indicating an unchanged file.

         There are 2 problematic situations which may occur:

         1. The cached working size is missing
         --> In this case, we forget we ever tried to compare
             and skip to the timestamp comparison.  This is
             because old working copies do not contain cached sizes

         2. The cached timestamp is missing
         --> In this case, we forget we ever tried to compare
             and skip to full file comparison.  This is because
             the timestamp will be removed when the library
             updates a locally changed file.  (ie, this only happens
             when the file was locally modified.)

      */

      /* Compare the sizes, if applicable */
      if (recorded_size != SVN_INVALID_FILESIZE
          && dirent->filesize != recorded_size)
        goto compare_them;

      /* Compare the timestamps

         Note: recorded_mod_time == 0 means not available,
               which also means the timestamps won't be equal,
               so there's no need to explicitly check the 'absent' value. */
      if (recorded_mod_time != dirent->mtime)
        goto compare_them;

      *modified_p = FALSE;
      return SVN_NO_ERROR;
    }

 compare_them:
  SVN_ERR(svn_wc__db_pristine_read(&pristine_stream, &pristine_size,
                                   db, local_abspath, checksum,
                                   scratch_pool, scratch_pool));

  /* Check all bytes, and verify checksum if requested. */
  {
    svn_error_t *err;
    err = compare_and_verify(modified_p, db,
                             local_abspath, dirent->filesize,
                             pristine_stream, pristine_size,
                             has_props, props_mod,
                             exact_comparison,
                             scratch_pool);

    /* At this point we already opened the pristine file, so we know that
       the access denied applies to the working copy path */
    if (err && APR_STATUS_IS_EACCES(err->apr_err))
      return svn_error_create(SVN_ERR_WC_PATH_ACCESS_DENIED, err, NULL);
    else
      SVN_ERR(err);
  }

  if (!*modified_p)
    {
      svn_boolean_t own_lock;

      /* The timestamp is missing or "broken" so "repair" it if we can. */
      SVN_ERR(svn_wc__db_wclock_owns_lock(&own_lock, db, local_abspath, FALSE,
                                          scratch_pool));
      if (own_lock)
        SVN_ERR(svn_wc__db_global_record_fileinfo(db, local_abspath,
                                                  dirent->filesize,
                                                  dirent->mtime,
                                                  scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_text_modified_p2(svn_boolean_t *modified_p,
                        svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        svn_boolean_t unused,
                        apr_pool_t *scratch_pool)
{
  return svn_wc__internal_file_modified_p(modified_p, wc_ctx->db,
                                          local_abspath, FALSE, scratch_pool);
}



static svn_error_t *
internal_conflicted_p(svn_boolean_t *text_conflicted_p,
                      svn_boolean_t *prop_conflicted_p,
                      svn_boolean_t *tree_conflicted_p,
                      svn_boolean_t *ignore_move_edit_p,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  svn_skel_t *conflicts;
  svn_boolean_t resolved_text = FALSE;
  svn_boolean_t resolved_props = FALSE;

  SVN_ERR(svn_wc__db_read_conflict(&conflicts, NULL, NULL,
                                   db, local_abspath,
                                   scratch_pool, scratch_pool));

  if (!conflicts)
    {
      if (text_conflicted_p)
        *text_conflicted_p = FALSE;
      if (prop_conflicted_p)
        *prop_conflicted_p = FALSE;
      if (tree_conflicted_p)
        *tree_conflicted_p = FALSE;
      if (ignore_move_edit_p)
        *ignore_move_edit_p = FALSE;

      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__conflict_read_info(NULL, NULL, text_conflicted_p,
                                     prop_conflicted_p, tree_conflicted_p,
                                     db, local_abspath, conflicts,
                                     scratch_pool, scratch_pool));

  if (text_conflicted_p && *text_conflicted_p)
    {
      const char *mine_abspath;
      const char *their_old_abspath;
      const char *their_abspath;
      svn_boolean_t done = FALSE;

      /* Look for any text conflict, exercising only as much effort as
         necessary to obtain a definitive answer.  This only applies to
         files, but we don't have to explicitly check that entry is a
         file, since these attributes would never be set on a directory
         anyway.  A conflict file entry notation only counts if the
         conflict file still exists on disk.  */

      SVN_ERR(svn_wc__conflict_read_text_conflict(&mine_abspath,
                                                  &their_old_abspath,
                                                  &their_abspath,
                                                  db, local_abspath, conflicts,
                                                  scratch_pool, scratch_pool));

      if (mine_abspath)
        {
          SVN_ERR(svn_io_check_path(mine_abspath, &kind, scratch_pool));

          *text_conflicted_p = (kind == svn_node_file);

          if (*text_conflicted_p)
            done = TRUE;
        }

      if (!done && their_abspath)
        {
          SVN_ERR(svn_io_check_path(their_abspath, &kind, scratch_pool));

          *text_conflicted_p = (kind == svn_node_file);

          if (*text_conflicted_p)
            done = TRUE;
        }

        if (!done && their_old_abspath)
        {
          SVN_ERR(svn_io_check_path(their_old_abspath, &kind, scratch_pool));

          *text_conflicted_p = (kind == svn_node_file);

          if (*text_conflicted_p)
            done = TRUE;
        }

        if (!done && (mine_abspath || their_abspath || their_old_abspath))
          resolved_text = TRUE; /* Remove in-db conflict marker */
    }

  if (prop_conflicted_p && *prop_conflicted_p)
    {
      const char *prej_abspath;

      SVN_ERR(svn_wc__conflict_read_prop_conflict(&prej_abspath,
                                                  NULL, NULL, NULL, NULL,
                                                  db, local_abspath, conflicts,
                                                  scratch_pool, scratch_pool));

      if (prej_abspath)
        {
          SVN_ERR(svn_io_check_path(prej_abspath, &kind, scratch_pool));

          *prop_conflicted_p = (kind == svn_node_file);

          if (! *prop_conflicted_p)
            resolved_props = TRUE; /* Remove in-db conflict marker */
        }
    }

  if (ignore_move_edit_p)
    {
      *ignore_move_edit_p = FALSE;
      if (tree_conflicted_p && *tree_conflicted_p)
        {
          svn_wc_conflict_reason_t reason;
          svn_wc_conflict_action_t action;

          SVN_ERR(svn_wc__conflict_read_tree_conflict(&reason, &action, NULL,
                                                      db, local_abspath,
                                                      conflicts,
                                                      scratch_pool,
                                                      scratch_pool));

          if (reason == svn_wc_conflict_reason_moved_away
              && action == svn_wc_conflict_action_edit)
            {
              *tree_conflicted_p = FALSE;
              *ignore_move_edit_p = TRUE;
            }
        }
    }

  if (resolved_text || resolved_props)
    {
      svn_boolean_t own_lock;

      /* The marker files are missing, so "repair" wc.db if we can */
      SVN_ERR(svn_wc__db_wclock_owns_lock(&own_lock, db, local_abspath, FALSE,
                                          scratch_pool));
      if (own_lock)
        SVN_ERR(svn_wc__db_op_mark_resolved(db, local_abspath,
                                            resolved_text,
                                            resolved_props,
                                            FALSE /* resolved_tree */,
                                            NULL /* work_items */,
                                            scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__internal_conflicted_p(svn_boolean_t *text_conflicted_p,
                              svn_boolean_t *prop_conflicted_p,
                              svn_boolean_t *tree_conflicted_p,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR(internal_conflicted_p(text_conflicted_p, prop_conflicted_p,
                                tree_conflicted_p, NULL,
                                db, local_abspath, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__conflicted_for_update_p(svn_boolean_t *conflicted_p,
                                svn_boolean_t *conflict_ignored_p,
                                svn_wc__db_t *db,
                                const char *local_abspath,
                                svn_boolean_t tree_only,
                                apr_pool_t *scratch_pool)
{
  svn_boolean_t text_conflicted, prop_conflicted, tree_conflicted;
  svn_boolean_t conflict_ignored;

  if (!conflict_ignored_p)
    conflict_ignored_p = &conflict_ignored;

  SVN_ERR(internal_conflicted_p(tree_only ? NULL: &text_conflicted,
                                tree_only ? NULL: &prop_conflicted,
                                &tree_conflicted, conflict_ignored_p,
                                db, local_abspath, scratch_pool));
  if (tree_only)
    *conflicted_p = tree_conflicted;
  else
    *conflicted_p = text_conflicted || prop_conflicted || tree_conflicted;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_conflicted_p3(svn_boolean_t *text_conflicted_p,
                     svn_boolean_t *prop_conflicted_p,
                     svn_boolean_t *tree_conflicted_p,
                     svn_wc_context_t *wc_ctx,
                     const char *local_abspath,
                     apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__internal_conflicted_p(text_conflicted_p,
                                                       prop_conflicted_p,
                                                       tree_conflicted_p,
                                                       wc_ctx->db,
                                                       local_abspath,
                                                       scratch_pool));
}

svn_error_t *
svn_wc__min_max_revisions(svn_revnum_t *min_revision,
                          svn_revnum_t *max_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          svn_boolean_t committed,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__db_min_max_revisions(min_revision,
                                                      max_revision,
                                                      wc_ctx->db,
                                                      local_abspath,
                                                      committed,
                                                      scratch_pool));
}


svn_error_t *
svn_wc__has_switched_subtrees(svn_boolean_t *is_switched,
                              svn_wc_context_t *wc_ctx,
                              const char *local_abspath,
                              const char *trail_url,
                              apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__db_has_switched_subtrees(is_switched,
                                                          wc_ctx->db,
                                                          local_abspath,
                                                          trail_url,
                                                          scratch_pool));
}


/* A baton for use with modcheck_found_entry(). */
typedef struct modcheck_baton_t {
  svn_boolean_t ignore_unversioned;
  svn_boolean_t found_mod;  /* whether a modification has been found */
  svn_boolean_t found_not_delete;  /* Found a not-delete modification */
} modcheck_baton_t;

/* An implementation of svn_wc_status_func4_t. */
static svn_error_t *
modcheck_callback(void *baton,
                  const char *local_abspath,
                  const svn_wc_status3_t *status,
                  apr_pool_t *scratch_pool)
{
  modcheck_baton_t *mb = baton;

  switch (status->node_status)
    {
      case svn_wc_status_normal:
      case svn_wc_status_ignored:
      case svn_wc_status_none:
      case svn_wc_status_external:
        break;

      case svn_wc_status_incomplete:
        if ((status->text_status != svn_wc_status_normal
             && status->text_status != svn_wc_status_none)
            || (status->prop_status != svn_wc_status_normal
                && status->prop_status != svn_wc_status_none))
          {
            mb->found_mod = TRUE;
            mb->found_not_delete = TRUE;
            /* Incomplete, but local modifications */
            return svn_error_create(SVN_ERR_CEASE_INVOCATION, NULL, NULL);
          }
        break;

      case svn_wc_status_deleted:
        mb->found_mod = TRUE;
        if (!mb->ignore_unversioned
            && status->actual_kind != svn_node_none
            && status->actual_kind != svn_node_unknown)
          {
            /* The delete is obstructed by something unversioned */
            mb->found_not_delete = TRUE;
            return svn_error_create(SVN_ERR_CEASE_INVOCATION, NULL, NULL);
          }
        break;

      case svn_wc_status_unversioned:
        if (mb->ignore_unversioned)
          break;
        /* else fall through */
      case svn_wc_status_missing:
      case svn_wc_status_obstructed:
        mb->found_mod = TRUE;
        mb->found_not_delete = TRUE;
        /* Exit from the status walker: We know what we want to know */
        return svn_error_create(SVN_ERR_CEASE_INVOCATION, NULL, NULL);

      default:
      case svn_wc_status_added:
      case svn_wc_status_replaced:
      case svn_wc_status_modified:
        mb->found_mod = TRUE;
        mb->found_not_delete = TRUE;
        /* Exit from the status walker: We know what we want to know */
        return svn_error_create(SVN_ERR_CEASE_INVOCATION, NULL, NULL);
    }

  return SVN_NO_ERROR;
}


/* Set *MODIFIED to true iff there are any local modifications within the
 * tree rooted at LOCAL_ABSPATH, using DB. If *MODIFIED
 * is set to true and all the local modifications were deletes then set
 * *ALL_EDITS_ARE_DELETES to true, set it to false otherwise.  LOCAL_ABSPATH
 * may be a file or a directory. */
svn_error_t *
svn_wc__node_has_local_mods(svn_boolean_t *modified,
                            svn_boolean_t *all_edits_are_deletes,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            svn_boolean_t ignore_unversioned,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *scratch_pool)
{
  modcheck_baton_t modcheck_baton = { FALSE, FALSE, FALSE };
  svn_error_t *err;

  if (!all_edits_are_deletes)
    {
      SVN_ERR(svn_wc__db_has_db_mods(modified, db, local_abspath,
                                     scratch_pool));

      if (*modified)
        return SVN_NO_ERROR;
    }

  modcheck_baton.ignore_unversioned = ignore_unversioned;

  /* Walk the WC tree for status with depth infinity, looking for any local
   * modifications. If it's a "sparse" directory, that's OK: there can be
   * no local mods in the pieces that aren't present in the WC. */

  err = svn_wc__internal_walk_status(db, local_abspath,
                                     svn_depth_infinity,
                                     FALSE, FALSE, FALSE, NULL,
                                     modcheck_callback, &modcheck_baton,
                                     cancel_func, cancel_baton,
                                     scratch_pool);

  if (err && err->apr_err == SVN_ERR_CEASE_INVOCATION)
    svn_error_clear(err);
  else
    SVN_ERR(err);

  *modified = modcheck_baton.found_mod;
  if (all_edits_are_deletes)
    *all_edits_are_deletes = (modcheck_baton.found_mod
                              && !modcheck_baton.found_not_delete);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__has_local_mods(svn_boolean_t *is_modified,
                       svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       svn_boolean_t ignore_unversioned,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *scratch_pool)
{
  svn_boolean_t modified;

  SVN_ERR(svn_wc__node_has_local_mods(&modified, NULL,
                                      wc_ctx->db, local_abspath,
                                      ignore_unversioned,
                                      cancel_func, cancel_baton,
                                      scratch_pool));

  *is_modified = modified;
  return SVN_NO_ERROR;
}
