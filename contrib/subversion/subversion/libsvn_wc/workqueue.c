/*
 * workqueue.c :  manipulating work queue items
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

#include <apr_pools.h>

#include "svn_private_config.h"
#include "svn_types.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_subst.h"
#include "svn_hash.h"
#include "svn_io.h"

#include "wc.h"
#include "wc_db.h"
#include "workqueue.h"
#include "adm_files.h"
#include "conflicts.h"
#include "translate.h"

#include "private/svn_io_private.h"
#include "private/svn_skel.h"


/* Workqueue operation names.  */
#define OP_FILE_COMMIT "file-commit"
#define OP_FILE_INSTALL "file-install"
#define OP_FILE_REMOVE "file-remove"
#define OP_FILE_MOVE "file-move"
#define OP_FILE_COPY_TRANSLATED "file-translate"
#define OP_SYNC_FILE_FLAGS "sync-file-flags"
#define OP_PREJ_INSTALL "prej-install"
#define OP_DIRECTORY_REMOVE "dir-remove"
#define OP_DIRECTORY_INSTALL "dir-install"

#define OP_POSTUPGRADE "postupgrade"

/* Legacy items */
#define OP_BASE_REMOVE "base-remove"
#define OP_RECORD_FILEINFO "record-fileinfo"
#define OP_TMP_SET_TEXT_CONFLICT_MARKERS "tmp-set-text-conflict-markers"
#define OP_TMP_SET_PROPERTY_CONFLICT_MARKER "tmp-set-property-conflict-marker"

/* For work queue debugging. Generates output about its operation.  */
/* #define SVN_DEBUG_WORK_QUEUE */

typedef struct work_item_baton_t work_item_baton_t;

struct work_item_dispatch {
  const char *name;
  svn_error_t *(*func)(work_item_baton_t *wqb,
                       svn_wc__db_t *db,
                       const svn_skel_t *work_item,
                       const char *wri_abspath,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *scratch_pool);
};

/* Forward definition */
static svn_error_t *
get_and_record_fileinfo(work_item_baton_t *wqb,
                        const char *local_abspath,
                        svn_boolean_t ignore_enoent,
                        apr_pool_t *scratch_pool);

/* ------------------------------------------------------------------------ */
/* OP_REMOVE_BASE  */

/* Removes a BASE_NODE and all it's data, leaving any adds and copies as is.
   Do this as a depth first traversal to make sure than any parent still exists
   on error conditions.
 */

/* Process the OP_REMOVE_BASE work item WORK_ITEM.
 * See svn_wc__wq_build_remove_base() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_base_remove(work_item_baton_t *wqb,
                svn_wc__db_t *db,
                const svn_skel_t *work_item,
                const char *wri_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  svn_revnum_t not_present_rev = SVN_INVALID_REVNUM;
  apr_int64_t val;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));
  SVN_ERR(svn_skel__parse_int(&val, arg1->next, scratch_pool));

  if (arg1->next->next)
    {
      not_present_rev = (svn_revnum_t)val;

      SVN_ERR(svn_skel__parse_int(&val, arg1->next->next, scratch_pool));
    }
  else
    {
      svn_boolean_t keep_not_present;

      SVN_ERR_ASSERT(SVN_WC__VERSION <= 28); /* Case unused in later versions*/

      keep_not_present = (val != 0);

      if (keep_not_present)
        {
          SVN_ERR(svn_wc__db_base_get_info(NULL, NULL,
                                           &not_present_rev, NULL,
                                           NULL, NULL, NULL,
                                           NULL, NULL, NULL, NULL, NULL, NULL,
                                           NULL, NULL, NULL,
                                           db, local_abspath,
                                           scratch_pool, scratch_pool));
        }
    }

  SVN_ERR(svn_wc__db_base_remove(db, local_abspath,
                                 FALSE /* keep_as_working */,
                                 SVN_IS_VALID_REVNUM(not_present_rev), FALSE,
                                 not_present_rev,
                                 NULL, NULL, scratch_pool));

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* ------------------------------------------------------------------------ */

/* OP_FILE_COMMIT  */


/* FILE_ABSPATH is the new text base of the newly-committed versioned file,
 * in repository-normal form (aka "detranslated" form).  Adjust the working
 * file accordingly.
 *
 * If eol and/or keyword translation would cause the working file to
 * change, then overwrite the working file with a translated copy of
 * the new text base (but only if the translated copy differs from the
 * current working file -- if they are the same, do nothing, to avoid
 * clobbering timestamps unnecessarily).
 *
 * Set the working file's executability according to its svn:executable
 * property.
 *
 * Set the working file's read-only attribute according to its properties
 * and lock status (see svn_wc__maybe_set_read_only()).
 *
 * If the working file was re-translated or had its executability or
 * read-only state changed,
 * then set OVERWROTE_WORKING to TRUE.  If the working file isn't
 * touched at all, then set to FALSE.
 *
 * Use SCRATCH_POOL for any temporary allocation.
 */
static svn_error_t *
install_committed_file(svn_boolean_t *overwrote_working,
                       svn_wc__db_t *db,
                       const char *file_abspath,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *scratch_pool)
{
  svn_boolean_t same;
  const char *tmp_wfile;
  svn_boolean_t special;

  /* start off assuming that the working file isn't touched. */
  *overwrote_working = FALSE;

  /* In the commit, newlines and keywords may have been
   * canonicalized and/or contracted... Or they may not have
   * been.  It's kind of hard to know.  Here's how we find out:
   *
   *    1. Make a translated tmp copy of the committed text base,
   *       translated according to the versioned file's properties.
   *       Or, if no committed text base exists (the commit must have
   *       been a propchange only), make a translated tmp copy of the
   *       working file.
   *    2. Compare the translated tmpfile to the working file.
   *    3. If different, copy the tmpfile over working file.
   *
   * This means we only rewrite the working file if we absolutely
   * have to, which is good because it avoids changing the file's
   * timestamp unless necessary, so editors aren't tempted to
   * reread the file if they don't really need to.
   */

  /* Copy and translate the new base-to-be file (if found, else the working
   * file) from repository-normal form to working form, writing a new
   * temporary file if any translation was actually done.  Set TMP_WFILE to
   * the translated file's path, which may be the source file's path if no
   * translation was done.  Set SAME to indicate whether the new working
   * text is the same as the old working text (or TRUE if it's a special
   * file). */
  {
    const char *tmp = file_abspath;

    /* Copy and translate, if necessary. The output file will be deleted at
     * scratch_pool cleanup.
     * ### That's not quite safe: we might rename the file and then maybe
     * its path will get re-used for another temp file before pool clean-up.
     * Instead, we should take responsibility for deleting it. */
    SVN_ERR(svn_wc__internal_translated_file(&tmp_wfile, tmp, db,
                                             file_abspath,
                                             SVN_WC_TRANSLATE_FROM_NF,
                                             cancel_func, cancel_baton,
                                             scratch_pool, scratch_pool));

    /* If the translation is a no-op, the text base and the working copy
     * file contain the same content, because we use the same props here
     * as were used to detranslate from working file to text base.
     *
     * In that case: don't replace the working file, but make sure
     * it has the right executable and read_write attributes set.
     */

    SVN_ERR(svn_wc__get_translate_info(NULL, NULL,
                                       NULL,
                                       &special,
                                       db, file_abspath, NULL, FALSE,
                                       scratch_pool, scratch_pool));
    /* Translated file returns the exact pointer if not translated. */
    if (! special && tmp != tmp_wfile)
      SVN_ERR(svn_io_files_contents_same_p(&same, tmp_wfile,
                                           file_abspath, scratch_pool));
    else
      same = TRUE;
  }

  if (! same)
    {
      SVN_ERR(svn_io_file_rename2(tmp_wfile, file_abspath, FALSE,
                                  scratch_pool));
      *overwrote_working = TRUE;
    }

  /* ### should be using OP_SYNC_FILE_FLAGS, or an internal version of
     ### that here. do we need to set *OVERWROTE_WORKING? */

  /* ### Re: OVERWROTE_WORKING, the following function is rather liberal
     ### with setting that flag, so we should probably decide if we really
     ### care about it when syncing flags. */
  SVN_ERR(svn_wc__sync_flags_with_props(overwrote_working, db, file_abspath,
                                        scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
process_commit_file_install(svn_wc__db_t *db,
                       const char *local_abspath,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *scratch_pool)
{
  svn_boolean_t overwrote_working;

  /* Install the new file, which may involve expanding keywords.
     A copy of this file should have been dropped into our `tmp/text-base'
     directory during the commit process.  Part of this process
     involves recording the textual timestamp for this entry.  We'd like
     to just use the timestamp of the working file, but it is possible
     that at some point during the commit, the real working file might
     have changed again.
   */

  SVN_ERR(install_committed_file(&overwrote_working, db,
                                 local_abspath,
                                 cancel_func, cancel_baton,
                                 scratch_pool));

  /* We will compute and modify the size and timestamp */
  if (overwrote_working)
    {
      apr_finfo_t finfo;

      SVN_ERR(svn_io_stat(&finfo, local_abspath,
                          APR_FINFO_MIN | APR_FINFO_LINK, scratch_pool));
      SVN_ERR(svn_wc__db_global_record_fileinfo(db, local_abspath,
                                                finfo.size, finfo.mtime,
                                                scratch_pool));
    }
  else
    {
      svn_boolean_t modified;

      /* The working copy file hasn't been overwritten.  We just
         removed the recorded size and modification time from the nodes
         record by calling svn_wc__db_global_commit().

         Now we have some file in our working copy that might be what
         we just committed, but we are not certain at this point.

         We still have a write lock here, so we check if the file is
         what we expect it to be and if it is the right file we update
         the recorded information. (If it isn't we keep the null data).

         Instead of reimplementing all this here, we just call a function
         that already does implement this when it notices that we have the
         right kind of lock (and we ignore the result)
       */
      SVN_ERR(svn_wc__internal_file_modified_p(&modified,
                                               db, local_abspath, FALSE,
                                               scratch_pool));
    }
  return SVN_NO_ERROR;
}


static svn_error_t *
run_file_commit(work_item_baton_t *wqb,
                svn_wc__db_t *db,
                const svn_skel_t *work_item,
                const char *wri_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  /* We don't both parsing the other two values in the skel. */

  return svn_error_trace(
                process_commit_file_install(db, local_abspath,
                                            cancel_func, cancel_baton,
                                            scratch_pool));
}

svn_error_t *
svn_wc__wq_build_file_commit(svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             svn_boolean_t props_mod,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  svn_skel__prepend_str(OP_FILE_COMMIT, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */
/* OP_POSTUPGRADE  */

static svn_error_t *
run_postupgrade(work_item_baton_t *wqb,
                svn_wc__db_t *db,
                const svn_skel_t *work_item,
                const char *wri_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  const char *entries_path;
  const char *format_path;
  const char *wcroot_abspath;
  svn_error_t *err;

  err = svn_wc__wipe_postupgrade(wri_abspath, FALSE,
                                 cancel_func, cancel_baton, scratch_pool);
  if (err && err->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
    /* No entry, this can happen when the wq item is rerun. */
    svn_error_clear(err);
  else
    SVN_ERR(err);

  SVN_ERR(svn_wc__db_get_wcroot(&wcroot_abspath, db, wri_abspath,
                                scratch_pool, scratch_pool));

  entries_path = svn_wc__adm_child(wcroot_abspath, SVN_WC__ADM_ENTRIES,
                                   scratch_pool);
  format_path = svn_wc__adm_child(wcroot_abspath, SVN_WC__ADM_FORMAT,
                                   scratch_pool);

  /* Write the 'format' and 'entries' files.

     ### The order may matter for some sufficiently old clients.. but
     ### this code only runs during upgrade after the files had been
     ### removed earlier during the upgrade. */
  SVN_ERR(svn_io_write_atomic2(format_path, SVN_WC__NON_ENTRIES_STRING,
                               sizeof(SVN_WC__NON_ENTRIES_STRING) - 1,
                               NULL, TRUE, scratch_pool));

  SVN_ERR(svn_io_write_atomic2(entries_path, SVN_WC__NON_ENTRIES_STRING,
                               sizeof(SVN_WC__NON_ENTRIES_STRING) - 1,
                               NULL, TRUE, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__wq_build_postupgrade(svn_skel_t **work_item,
                             apr_pool_t *result_pool)
{
  *work_item = svn_skel__make_empty_list(result_pool);

  svn_skel__prepend_str(OP_POSTUPGRADE, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_FILE_INSTALL */

/* Process the OP_FILE_INSTALL work item WORK_ITEM.
 * See svn_wc__wq_build_file_install() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_file_install(work_item_baton_t *wqb,
                 svn_wc__db_t *db,
                 const svn_skel_t *work_item,
                 const char *wri_abspath,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const svn_skel_t *arg4 = arg1->next->next->next;
  const char *local_relpath;
  const char *local_abspath;
  svn_boolean_t use_commit_times;
  svn_boolean_t record_fileinfo;
  svn_boolean_t special;
  svn_stream_t *src_stream;
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  const char *temp_dir_abspath;
  svn_stream_t *dst_stream;
  apr_int64_t val;
  const char *wcroot_abspath;
  const char *source_abspath;
  const svn_checksum_t *checksum;
  apr_hash_t *props;
  apr_time_t changed_date;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  SVN_ERR(svn_skel__parse_int(&val, arg1->next, scratch_pool));
  use_commit_times = (val != 0);
  SVN_ERR(svn_skel__parse_int(&val, arg1->next->next, scratch_pool));
  record_fileinfo = (val != 0);

  SVN_ERR(svn_wc__db_read_node_install_info(&wcroot_abspath,
                                            &checksum, &props,
                                            &changed_date,
                                            db, local_abspath, wri_abspath,
                                            scratch_pool, scratch_pool));

  if (arg4 != NULL)
    {
      /* Use the provided path for the source.  */
      local_relpath = apr_pstrmemdup(scratch_pool, arg4->data, arg4->len);
      SVN_ERR(svn_wc__db_from_relpath(&source_abspath, db, wri_abspath,
                                      local_relpath,
                                      scratch_pool, scratch_pool));
    }
  else if (! checksum)
    {
      /* This error replaces a previous assertion. Reporting an error from here
         leaves the workingqueue operation in place, so the working copy is
         still broken!

         But when we report this error the user at least knows what node has
         this specific problem, so maybe we can find out why users see this
         error */
      return svn_error_createf(SVN_ERR_WC_CORRUPT_TEXT_BASE, NULL,
                               _("Can't install '%s' from pristine store, "
                                 "because no checksum is recorded for this "
                                 "file"),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }
  else
    {
      SVN_ERR(svn_wc__db_pristine_get_future_path(&source_abspath,
                                                  wcroot_abspath,
                                                  checksum,
                                                  scratch_pool, scratch_pool));
    }

  SVN_ERR(svn_stream_open_readonly(&src_stream, source_abspath,
                                   scratch_pool, scratch_pool));

  /* Fetch all the translation bits.  */
  SVN_ERR(svn_wc__get_translate_info(&style, &eol,
                                     &keywords,
                                     &special, db, local_abspath,
                                     props, FALSE,
                                     scratch_pool, scratch_pool));
  if (special)
    {
      /* When this stream is closed, the resulting special file will
         atomically be created/moved into place at LOCAL_ABSPATH.  */
      SVN_ERR(svn_subst_create_specialfile(&dst_stream, local_abspath,
                                           scratch_pool, scratch_pool));

      /* Copy the "repository normal" form of the special file into the
         special stream.  */
      SVN_ERR(svn_stream_copy3(src_stream, dst_stream,
                               cancel_func, cancel_baton,
                               scratch_pool));

      /* No need to set exec or read-only flags on special files.  */

      /* ### Shouldn't this record a timestamp and size, etc.? */
      return SVN_NO_ERROR;
    }

  if (svn_subst_translation_required(style, eol, keywords,
                                     FALSE /* special */,
                                     TRUE /* force_eol_check */))
    {
      /* Wrap it in a translating (expanding) stream.  */
      src_stream = svn_subst_stream_translated(src_stream, eol,
                                               TRUE /* repair */,
                                               keywords,
                                               TRUE /* expand */,
                                               scratch_pool);
    }

  /* Where is the Right Place to put a temp file in this working copy?  */
  SVN_ERR(svn_wc__db_temp_wcroot_tempdir(&temp_dir_abspath,
                                         db, wcroot_abspath,
                                         scratch_pool, scratch_pool));

  /* Translate to a temporary file. We don't want the user seeing a partial
     file, nor let them muck with it while we translate. We may also need to
     get its TRANSLATED_SIZE before the user can monkey it.  */
  SVN_ERR(svn_stream__create_for_install(&dst_stream, temp_dir_abspath,
                                         scratch_pool, scratch_pool));

  /* Copy from the source to the dest, translating as we go. This will also
     close both streams.  */
  SVN_ERR(svn_stream_copy3(src_stream, dst_stream,
                           cancel_func, cancel_baton,
                           scratch_pool));

  /* All done. Move the file into place.  */
  /* With a single db we might want to install files in a missing directory.
     Simply trying this scenario on error won't do any harm and at least
     one user reported this problem on IRC. */
  SVN_ERR(svn_stream__install_stream(dst_stream, local_abspath,
                                     TRUE /* make_parents*/, scratch_pool));

  /* Tweak the on-disk file according to its properties.  */
#ifndef WIN32
  if (props && svn_hash_gets(props, SVN_PROP_EXECUTABLE))
    SVN_ERR(svn_io_set_file_executable(local_abspath, TRUE, FALSE,
                                       scratch_pool));
#endif

  /* Note that this explicitly checks the pristine properties, to make sure
     that when the lock is locally set (=modification) it is not read only */
  if (props && svn_hash_gets(props, SVN_PROP_NEEDS_LOCK))
    {
      svn_wc__db_status_t status;
      svn_wc__db_lock_t *lock;
      SVN_ERR(svn_wc__db_read_info(&status, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, &lock, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL,
                                   db, local_abspath,
                                   scratch_pool, scratch_pool));

      if (!lock && status != svn_wc__db_status_added)
        SVN_ERR(svn_io_set_file_read_only(local_abspath, FALSE, scratch_pool));
    }

  if (use_commit_times)
    {
      if (changed_date)
        SVN_ERR(svn_io_set_file_affected_time(changed_date,
                                              local_abspath,
                                              scratch_pool));
    }

  /* ### this should happen before we rename the file into place.  */
  if (record_fileinfo)
    {
      SVN_ERR(get_and_record_fileinfo(wqb, local_abspath,
                                      FALSE /* ignore_enoent */,
                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wq_build_file_install(svn_skel_t **work_item,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              const char *source_abspath,
                              svn_boolean_t use_commit_times,
                              svn_boolean_t record_fileinfo,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  const char *wri_abspath;
  *work_item = svn_skel__make_empty_list(result_pool);

  /* Use the directory of the file to install as wri_abspath to avoid
     filestats on just obtaining the wc-root */
  wri_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  /* If a SOURCE_ABSPATH was provided, then put it into the skel. If this
     value is not provided, then the file's pristine contents will be used. */
  if (source_abspath != NULL)
    {
      SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, wri_abspath,
                                    source_abspath,
                                    result_pool, scratch_pool));

      svn_skel__prepend_str(local_relpath, *work_item, result_pool);
    }

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, wri_abspath,
                                local_abspath, result_pool, scratch_pool));

  svn_skel__prepend_int(record_fileinfo, *work_item, result_pool);
  svn_skel__prepend_int(use_commit_times, *work_item, result_pool);
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_FILE_INSTALL, *work_item, result_pool);

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* OP_FILE_REMOVE  */

/* Process the OP_FILE_REMOVE work item WORK_ITEM.
 * See svn_wc__wq_build_file_remove() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_file_remove(work_item_baton_t *wqb,
                svn_wc__db_t *db,
                const svn_skel_t *work_item,
                const char *wri_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  /* Remove the path, no worrying if it isn't there.  */
  return svn_error_trace(svn_io_remove_file2(local_abspath, TRUE,
                                             scratch_pool));
}


svn_error_t *
svn_wc__wq_build_file_remove(svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *wri_abspath,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, wri_abspath,
                                local_abspath, result_pool, scratch_pool));

  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_FILE_REMOVE, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_DIRECTORY_REMOVE  */

/* Process the OP_FILE_REMOVE work item WORK_ITEM.
 * See svn_wc__wq_build_file_remove() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_dir_remove(work_item_baton_t *wqb,
               svn_wc__db_t *db,
               const svn_skel_t *work_item,
               const char *wri_abspath,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  svn_boolean_t recursive;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  recursive = FALSE;
  if (arg1->next)
    {
      apr_int64_t val;
      SVN_ERR(svn_skel__parse_int(&val, arg1->next, scratch_pool));

      recursive = (val != 0);
    }

  /* Remove the path, no worrying if it isn't there.  */
  if (recursive)
    return svn_error_trace(
                svn_io_remove_dir2(local_abspath, TRUE,
                                   cancel_func, cancel_baton,
                                   scratch_pool));
  else
    {
      svn_error_t *err;

      err = svn_io_dir_remove_nonrecursive(local_abspath, scratch_pool);

      if (err && (APR_STATUS_IS_ENOENT(err->apr_err)
                  || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)
                  || APR_STATUS_IS_ENOTEMPTY(err->apr_err)))
        {
          svn_error_clear(err);
          err = NULL;
        }

      return svn_error_trace(err);
    }
}

svn_error_t *
svn_wc__wq_build_dir_remove(svn_skel_t **work_item,
                            svn_wc__db_t *db,
                            const char *wri_abspath,
                            const char *local_abspath,
                            svn_boolean_t recursive,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, wri_abspath,
                                local_abspath, result_pool, scratch_pool));

  if (recursive)
    svn_skel__prepend_int(TRUE, *work_item, result_pool);

  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_DIRECTORY_REMOVE, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_FILE_MOVE  */

/* Process the OP_FILE_MOVE work item WORK_ITEM.
 * See svn_wc__wq_build_file_move() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_file_move(work_item_baton_t *wqb,
              svn_wc__db_t *db,
              const svn_skel_t *work_item,
              const char *wri_abspath,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *src_abspath, *dst_abspath;
  const char *local_relpath;
  svn_error_t *err;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&src_abspath, db, wri_abspath, local_relpath,
                                  scratch_pool, scratch_pool));
  local_relpath = apr_pstrmemdup(scratch_pool, arg1->next->data,
                                 arg1->next->len);
  SVN_ERR(svn_wc__db_from_relpath(&dst_abspath, db, wri_abspath, local_relpath,
                                  scratch_pool, scratch_pool));

  /* Use svn_io_file_move() instead of svn_io_file_rename() to allow cross
     device copies. We should not fail in the workqueue. */

  err = svn_io_file_move(src_abspath, dst_abspath, scratch_pool);

  /* If the source is not found, we assume the wq op is already handled */
  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    svn_error_clear(err);
  else
    SVN_ERR(err);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wq_build_file_move(svn_skel_t **work_item,
                           svn_wc__db_t *db,
                           const char *wri_abspath,
                           const char *src_abspath,
                           const char *dst_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  /* File must exist */
  SVN_ERR(svn_io_check_path(src_abspath, &kind, result_pool));

  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("'%s' not found"),
                             svn_dirent_local_style(src_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, wri_abspath, dst_abspath,
                                result_pool, scratch_pool));
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, wri_abspath, src_abspath,
                                result_pool, scratch_pool));
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  svn_skel__prepend_str(OP_FILE_MOVE, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_FILE_COPY_TRANSLATED */

/* Process the OP_FILE_COPY_TRANSLATED work item WORK_ITEM.
 * See run_file_copy_translated() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_file_copy_translated(work_item_baton_t *wqb,
                         svn_wc__db_t *db,
                         const svn_skel_t *work_item,
                         const char *wri_abspath,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_abspath, *src_abspath, *dst_abspath;
  const char *local_relpath;
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  svn_boolean_t special;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->next->data,
                               arg1->next->len);
  SVN_ERR(svn_wc__db_from_relpath(&src_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->next->next->data,
                                arg1->next->next->len);
  SVN_ERR(svn_wc__db_from_relpath(&dst_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__get_translate_info(&style, &eol,
                                     &keywords,
                                     &special,
                                     db, local_abspath, NULL, FALSE,
                                     scratch_pool, scratch_pool));

  SVN_ERR(svn_subst_copy_and_translate4(src_abspath, dst_abspath,
                                        eol, TRUE /* repair */,
                                        keywords, TRUE /* expand */,
                                        special,
                                        cancel_func, cancel_baton,
                                        scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wq_build_file_copy_translated(svn_skel_t **work_item,
                                      svn_wc__db_t *db,
                                      const char *local_abspath,
                                      const char *src_abspath,
                                      const char *dst_abspath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  const char *local_relpath;

  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  /* File must exist */
  SVN_ERR(svn_io_check_path(src_abspath, &kind, result_pool));

  if (kind == svn_node_none)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("'%s' not found"),
                             svn_dirent_local_style(src_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath, dst_abspath,
                                result_pool, scratch_pool));
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath, src_abspath,
                                result_pool, scratch_pool));
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  svn_skel__prepend_str(OP_FILE_COPY_TRANSLATED, *work_item, result_pool);

  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_DIRECTORY_INSTALL  */

static svn_error_t *
run_dir_install(work_item_baton_t *wqb,
                svn_wc__db_t *db,
                const svn_skel_t *work_item,
                const char *wri_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__ensure_directory(local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__wq_build_dir_install(svn_skel_t **work_item,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const char *local_relpath;

  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);

  svn_skel__prepend_str(OP_DIRECTORY_INSTALL, *work_item, result_pool);

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* OP_SYNC_FILE_FLAGS  */

/* Process the OP_SYNC_FILE_FLAGS work item WORK_ITEM.
 * See svn_wc__wq_build_sync_file_flags() which generates this work item.
 * Implements (struct work_item_dispatch).func. */
static svn_error_t *
run_sync_file_flags(work_item_baton_t *wqb,
                    svn_wc__db_t *db,
                    const svn_skel_t *work_item,
                    const char *wri_abspath,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  return svn_error_trace(svn_wc__sync_flags_with_props(NULL, db,
                                            local_abspath, scratch_pool));
}


svn_error_t *
svn_wc__wq_build_sync_file_flags(svn_skel_t **work_item,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_SYNC_FILE_FLAGS, *work_item, result_pool);

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* OP_PREJ_INSTALL  */

static svn_error_t *
run_prej_install(work_item_baton_t *wqb,
                 svn_wc__db_t *db,
                 const svn_skel_t *work_item,
                 const char *wri_abspath,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  svn_skel_t *conflicts;
  const svn_skel_t *prop_conflict_skel;
  const char *tmp_prejfile_abspath;
  const char *prejfile_abspath;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__db_read_conflict(&conflicts, NULL, NULL, db, local_abspath,
                                   scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__conflict_read_prop_conflict(&prejfile_abspath,
                                              NULL, NULL, NULL, NULL,
                                              db, local_abspath, conflicts,
                                              scratch_pool, scratch_pool));

  if (arg1->next != NULL)
    prop_conflict_skel = arg1->next; /* Before Subversion 1.9 */
  else
    prop_conflict_skel = NULL; /* Read from DB */

  /* Construct a property reject file in the temporary area.  */
  SVN_ERR(svn_wc__create_prejfile(&tmp_prejfile_abspath,
                                  db, local_abspath,
                                  prop_conflict_skel,
                                  cancel_func, cancel_baton,
                                  scratch_pool, scratch_pool));

  /* ... and atomically move it into place.  */
  SVN_ERR(svn_io_file_rename2(tmp_prejfile_abspath,
                              prejfile_abspath, FALSE,
                              scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wq_build_prej_install(svn_skel_t **work_item,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              /*svn_skel_t *conflict_skel,*/
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  *work_item = svn_skel__make_empty_list(result_pool);

  SVN_ERR(svn_wc__db_to_relpath(&local_relpath, db, local_abspath,
                                local_abspath, result_pool, scratch_pool));

  /* ### In Subversion 1.7 and 1.8 we created a legacy property conflict skel
         here:
    if (conflict_skel != NULL)
      svn_skel__prepend(conflict_skel, *work_item);
   */
  svn_skel__prepend_str(local_relpath, *work_item, result_pool);
  svn_skel__prepend_str(OP_PREJ_INSTALL, *work_item, result_pool);

  return SVN_NO_ERROR;
}


/* ------------------------------------------------------------------------ */

/* OP_RECORD_FILEINFO  */


static svn_error_t *
run_record_fileinfo(work_item_baton_t *wqb,
                    svn_wc__db_t *db,
                    const svn_skel_t *work_item,
                    const char *wri_abspath,
                    svn_cancel_func_t cancel_func,
                    void *cancel_baton,
                    apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg1 = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  apr_time_t set_time = 0;

  local_relpath = apr_pstrmemdup(scratch_pool, arg1->data, arg1->len);

  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  if (arg1->next)
    {
      apr_int64_t val;

      SVN_ERR(svn_skel__parse_int(&val, arg1->next, scratch_pool));
      set_time = (apr_time_t)val;
    }

  if (set_time != 0)
    {
      svn_node_kind_t kind;
      svn_boolean_t is_special;

      /* Do not set the timestamp on special files. */
      SVN_ERR(svn_io_check_special_path(local_abspath, &kind, &is_special,
                                        scratch_pool));

      /* Don't set affected time when local_abspath does not exist or is
         a special file */
      if (kind == svn_node_file && !is_special)
        SVN_ERR(svn_io_set_file_affected_time(set_time, local_abspath,
                                              scratch_pool));

      /* Note that we can't use the value we get here for recording as the
         filesystem might have a different timestamp granularity */
    }


  return svn_error_trace(get_and_record_fileinfo(wqb, local_abspath,
                                                 TRUE /* ignore_enoent */,
                                                 scratch_pool));
}

/* ------------------------------------------------------------------------ */

/* OP_TMP_SET_TEXT_CONFLICT_MARKERS  */


static svn_error_t *
run_set_text_conflict_markers(work_item_baton_t *wqb,
                              svn_wc__db_t *db,
                              const svn_skel_t *work_item,
                              const char *wri_abspath,
                              svn_cancel_func_t cancel_func,
                              void *cancel_baton,
                              apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  const char *old_abspath = NULL;
  const char *new_abspath = NULL;
  const char *wrk_abspath = NULL;

  local_relpath = apr_pstrmemdup(scratch_pool, arg->data, arg->len);
  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));

  arg = arg->next;
  local_relpath = arg->len ? apr_pstrmemdup(scratch_pool, arg->data, arg->len)
                           : NULL;

  if (local_relpath)
    {
      SVN_ERR(svn_wc__db_from_relpath(&old_abspath, db, wri_abspath,
                                      local_relpath,
                                      scratch_pool, scratch_pool));
    }

  arg = arg->next;
  local_relpath = arg->len ? apr_pstrmemdup(scratch_pool, arg->data, arg->len)
                           : NULL;
  if (local_relpath)
    {
      SVN_ERR(svn_wc__db_from_relpath(&new_abspath, db, wri_abspath,
                                      local_relpath,
                                      scratch_pool, scratch_pool));
    }

  arg = arg->next;
  local_relpath = arg->len ? apr_pstrmemdup(scratch_pool, arg->data, arg->len)
                           : NULL;

  if (local_relpath)
    {
      SVN_ERR(svn_wc__db_from_relpath(&wrk_abspath, db, wri_abspath,
                                      local_relpath,
                                      scratch_pool, scratch_pool));
    }

  /* Upgrade scenario: We have a workqueue item that describes how to install a
     non skel conflict. Fetch all the information we can to create a new style
     conflict. */
  /* ### Before format 30 this is/was a common code path as we didn't install
     ### the conflict directly in the db. It just calls the wc_db code
     ### to set the right fields. */

  {
    /* Check if we should combine with a property conflict... */
    svn_skel_t *conflicts;

    SVN_ERR(svn_wc__db_read_conflict(&conflicts, NULL, NULL, db, local_abspath,
                                     scratch_pool, scratch_pool));

    if (! conflicts)
      {
        /* No conflict exists, create a basic skel */
        conflicts = svn_wc__conflict_skel_create(scratch_pool);

        SVN_ERR(svn_wc__conflict_skel_set_op_update(conflicts, NULL, NULL,
                                                    scratch_pool,
                                                    scratch_pool));
      }

    /* Add the text conflict to the existing onflict */
    SVN_ERR(svn_wc__conflict_skel_add_text_conflict(conflicts, db,
                                                    local_abspath,
                                                    wrk_abspath,
                                                    old_abspath,
                                                    new_abspath,
                                                    scratch_pool,
                                                    scratch_pool));

    SVN_ERR(svn_wc__db_op_mark_conflict(db, local_abspath, conflicts,
                                        NULL, scratch_pool));
  }
  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

/* OP_TMP_SET_PROPERTY_CONFLICT_MARKER  */

static svn_error_t *
run_set_property_conflict_marker(work_item_baton_t *wqb,
                                 svn_wc__db_t *db,
                                 const svn_skel_t *work_item,
                                 const char *wri_abspath,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *scratch_pool)
{
  const svn_skel_t *arg = work_item->children->next;
  const char *local_relpath;
  const char *local_abspath;
  const char *prej_abspath = NULL;

  local_relpath = apr_pstrmemdup(scratch_pool, arg->data, arg->len);

  SVN_ERR(svn_wc__db_from_relpath(&local_abspath, db, wri_abspath,
                                  local_relpath, scratch_pool, scratch_pool));


  arg = arg->next;
  local_relpath = arg->len ? apr_pstrmemdup(scratch_pool, arg->data, arg->len)
                           : NULL;

  if (local_relpath)
    SVN_ERR(svn_wc__db_from_relpath(&prej_abspath, db, wri_abspath,
                                    local_relpath,
                                    scratch_pool, scratch_pool));

  {
    /* Check if we should combine with a text conflict... */
    svn_skel_t *conflicts;
    apr_hash_t *prop_names;

    SVN_ERR(svn_wc__db_read_conflict(&conflicts, NULL, NULL,
                                     db, local_abspath,
                                     scratch_pool, scratch_pool));

    if (! conflicts)
      {
        /* No conflict exists, create a basic skel */
        conflicts = svn_wc__conflict_skel_create(scratch_pool);

        SVN_ERR(svn_wc__conflict_skel_set_op_update(conflicts, NULL, NULL,
                                                    scratch_pool,
                                                    scratch_pool));
      }

    prop_names = apr_hash_make(scratch_pool);
    SVN_ERR(svn_wc__conflict_skel_add_prop_conflict(conflicts, db,
                                                    local_abspath,
                                                    prej_abspath,
                                                    NULL, NULL, NULL,
                                                    prop_names,
                                                    scratch_pool,
                                                    scratch_pool));

    SVN_ERR(svn_wc__db_op_mark_conflict(db, local_abspath, conflicts,
                                        NULL, scratch_pool));
  }
  return SVN_NO_ERROR;
}

/* ------------------------------------------------------------------------ */

static const struct work_item_dispatch dispatch_table[] = {
  { OP_FILE_COMMIT, run_file_commit },
  { OP_FILE_INSTALL, run_file_install },
  { OP_FILE_REMOVE, run_file_remove },
  { OP_FILE_MOVE, run_file_move },
  { OP_FILE_COPY_TRANSLATED, run_file_copy_translated },
  { OP_SYNC_FILE_FLAGS, run_sync_file_flags },
  { OP_PREJ_INSTALL, run_prej_install },
  { OP_DIRECTORY_REMOVE, run_dir_remove },
  { OP_DIRECTORY_INSTALL, run_dir_install },

  /* Upgrade steps */
  { OP_POSTUPGRADE, run_postupgrade },

  /* Legacy workqueue items. No longer created */
  { OP_BASE_REMOVE, run_base_remove },
  { OP_RECORD_FILEINFO, run_record_fileinfo },
  { OP_TMP_SET_TEXT_CONFLICT_MARKERS, run_set_text_conflict_markers },
  { OP_TMP_SET_PROPERTY_CONFLICT_MARKER, run_set_property_conflict_marker },

  /* Sentinel.  */
  { NULL }
};

struct work_item_baton_t
{
  apr_pool_t *result_pool; /* Pool to allocate result in */

  svn_boolean_t used; /* needs reset */

  apr_hash_t *record_map; /* const char * -> svn_io_dirent2_t map */
};


static svn_error_t *
dispatch_work_item(work_item_baton_t *wqb,
                   svn_wc__db_t *db,
                   const char *wri_abspath,
                   const svn_skel_t *work_item,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   apr_pool_t *scratch_pool)
{
  const struct work_item_dispatch *scan;

  /* Scan the dispatch table for a function to handle this work item.  */
  for (scan = &dispatch_table[0]; scan->name != NULL; ++scan)
    {
      if (svn_skel__matches_atom(work_item->children, scan->name))
        {

#ifdef SVN_DEBUG_WORK_QUEUE
          SVN_DBG(("dispatch: operation='%s'\n", scan->name));
#endif
          SVN_ERR((*scan->func)(wqb, db, work_item, wri_abspath,
                                cancel_func, cancel_baton,
                                scratch_pool));

#ifdef SVN_RUN_WORK_QUEUE_TWICE
#ifdef SVN_DEBUG_WORK_QUEUE
          SVN_DBG(("dispatch: operation='%s'\n", scan->name));
#endif
          /* Being able to run every workqueue item twice is one
             requirement for workqueues to be restartable. */
          SVN_ERR((*scan->func)(db, work_item, wri_abspath,
                                cancel_func, cancel_baton,
                                scratch_pool));
#endif

          break;
        }
    }

  if (scan->name == NULL)
    {
      /* We should know about ALL possible work items here. If we do not,
         then something is wrong. Most likely, some kind of format/code
         skew. There is nothing more we can do. Erasing or ignoring this
         work item could leave the WC in an even more broken state.

         Contrary to issue #1581, we cannot simply remove work items and
         continue, so bail out with an error.  */
      return svn_error_createf(SVN_ERR_WC_BAD_ADM_LOG, NULL,
                               _("Unrecognized work item in the queue"));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__wq_run(svn_wc__db_t *db,
               const char *wri_abspath,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_uint64_t last_id = 0;
  work_item_baton_t wib = { 0 };
  wib.result_pool = svn_pool_create(scratch_pool);

#ifdef SVN_DEBUG_WORK_QUEUE
  SVN_DBG(("wq_run: wri='%s'\n", wri_abspath));
  {
    static int count = 0;
    const char *count_env_var = getenv("SVN_DEBUG_WORK_QUEUE");
    int count_env_val;

    SVN_ERR(svn_cstring_atoi(&count_env_val, count_env_var));

    if (count_env_var && ++count == count_env_val)
      return svn_error_create(SVN_ERR_CANCELLED, NULL, "fake cancel");
  }
#endif

  while (TRUE)
    {
      apr_uint64_t id;
      svn_skel_t *work_item;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      if (! wib.used)
        {
          /* Make sure to do this *early* in the loop iteration. There may
             be a LAST_ID that needs to be marked as completed, *before* we
             start worrying about anything else.  */
          SVN_ERR(svn_wc__db_wq_fetch_next(&id, &work_item, db, wri_abspath,
                                           last_id, iterpool, iterpool));
        }
      else
        {
          /* Make sure to do this *early* in the loop iteration. There may
             be a LAST_ID that needs to be marked as completed, *before* we
             start worrying about anything else.  */
          SVN_ERR(svn_wc__db_wq_record_and_fetch_next(&id, &work_item,
                                                      db, wri_abspath,
                                                      last_id, wib.record_map,
                                                      iterpool,
                                                      wib.result_pool));

          svn_pool_clear(wib.result_pool);
          wib.record_map = NULL;
          wib.used = FALSE;
        }

      /* Stop work queue processing, if requested. A future 'svn cleanup'
         should be able to continue the processing. Note that we may
         have WORK_ITEM, but we'll just skip its processing for now.  */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      /* If we have a WORK_ITEM, then process the sucker. Otherwise,
         we're done.  */
      if (work_item == NULL)
        break;

      err = dispatch_work_item(&wib, db, wri_abspath, work_item,
                               cancel_func, cancel_baton, iterpool);
      if (err)
        {
          const char *skel = svn_skel__unparse(work_item, scratch_pool)->data;

          return svn_error_createf(SVN_ERR_WC_BAD_ADM_LOG, err,
                                   _("Failed to run the WC DB work queue "
                                     "associated with '%s', work item %d %s"),
                                   svn_dirent_local_style(wri_abspath,
                                                          scratch_pool),
                                   (int)id, skel);
        }

      /* The work item finished without error. Mark it completed
         in the next loop.  */
      last_id = id;
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


svn_skel_t *
svn_wc__wq_merge(svn_skel_t *work_item1,
                 svn_skel_t *work_item2,
                 apr_pool_t *result_pool)
{
  /* If either argument is NULL, then just return the other.  */
  if (work_item1 == NULL)
    return work_item2;
  if (work_item2 == NULL)
    return work_item1;

  /* We have two items. Figure out how to join them.  */
  if (SVN_WC__SINGLE_WORK_ITEM(work_item1))
    {
      if (SVN_WC__SINGLE_WORK_ITEM(work_item2))
        {
          /* Both are singular work items. Construct a list, then put
             both work items into it (in the proper order).  */

          svn_skel_t *result = svn_skel__make_empty_list(result_pool);

          svn_skel__prepend(work_item2, result);
          svn_skel__prepend(work_item1, result);
          return result;
        }

      /* WORK_ITEM2 is a list of work items. We can simply shove WORK_ITEM1
         in the front to keep the ordering.  */
      svn_skel__prepend(work_item1, work_item2);
      return work_item2;
    }
  /* WORK_ITEM1 is a list of work items.  */

  if (SVN_WC__SINGLE_WORK_ITEM(work_item2))
    {
      /* Put WORK_ITEM2 onto the end of the WORK_ITEM1 list.  */
      svn_skel__append(work_item1, work_item2);
      return work_item1;
    }

  /* We have two lists of work items. We need to chain all of the work
     items into one big list. We will leave behind the WORK_ITEM2 skel,
     as we only want its children.  */
  svn_skel__append(work_item1, work_item2->children);
  return work_item1;
}


static svn_error_t *
get_and_record_fileinfo(work_item_baton_t *wqb,
                        const char *local_abspath,
                        svn_boolean_t ignore_enoent,
                        apr_pool_t *scratch_pool)
{
  const svn_io_dirent2_t *dirent;

  SVN_ERR(svn_io_stat_dirent2(&dirent, local_abspath, FALSE, ignore_enoent,
                              wqb->result_pool, scratch_pool));

  if (dirent->kind != svn_node_file)
    return SVN_NO_ERROR;

  wqb->used = TRUE;

  if (! wqb->record_map)
    wqb->record_map = apr_hash_make(wqb->result_pool);

  svn_hash_sets(wqb->record_map, apr_pstrdup(wqb->result_pool, local_abspath),
                dirent);

  return SVN_NO_ERROR;
}
