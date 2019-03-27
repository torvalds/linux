/*
 * diff.c: comparing
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

#include <apr_strings.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include "svn_types.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_diff.h"
#include "svn_mergeinfo.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "svn_props.h"
#include "svn_subst.h"
#include "client.h"

#include "private/svn_wc_private.h"
#include "private/svn_diff_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_io_private.h"
#include "private/svn_ra_private.h"

#include "svn_private_config.h"


/* Utilities */

#define DIFF_REVNUM_NONEXISTENT ((svn_revnum_t) -100)

#define MAKE_ERR_BAD_RELATIVE_PATH(path, relative_to_dir) \
        svn_error_createf(SVN_ERR_BAD_RELATIVE_PATH, NULL, \
                          _("Path '%s' must be an immediate child of " \
                            "the directory '%s'"), path, relative_to_dir)

/* Calculate the repository relative path of DIFF_RELPATH, using
 * SESSION_RELPATH and WC_CTX, and return the result in *REPOS_RELPATH.
 * ORIG_TARGET is the related original target passed to the diff command,
 * and may be used to derive leading path components missing from PATH.
 * ANCHOR is the local path where the diff editor is anchored.
 * Do all allocations in POOL. */
static svn_error_t *
make_repos_relpath(const char **repos_relpath,
                   const char *diff_relpath,
                   const char *orig_target,
                   const char *session_relpath,
                   svn_wc_context_t *wc_ctx,
                   const char *anchor,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *local_abspath;

  if (! session_relpath
      || (anchor && !svn_path_is_url(orig_target)))
    {
      svn_error_t *err;
      /* We're doing a WC-WC diff, so we can retrieve all information we
       * need from the working copy. */
      SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                      svn_dirent_join(anchor, diff_relpath,
                                                      scratch_pool),
                                      scratch_pool));

      err = svn_wc__node_get_repos_info(NULL, repos_relpath, NULL, NULL,
                                        wc_ctx, local_abspath,
                                        result_pool, scratch_pool);

      if (!session_relpath
          || ! err
          || (err && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND))
        {
           return svn_error_trace(err);
        }

      /* The path represents a local working copy path, but does not
         exist. Fall through to calculate an in-repository location
         based on the ra session */

      /* ### Maybe we should use the nearest existing ancestor instead? */
      svn_error_clear(err);
    }

  *repos_relpath = svn_relpath_join(session_relpath, diff_relpath,
                                    result_pool);

  return SVN_NO_ERROR;
}

/* Adjust *INDEX_PATH, *ORIG_PATH_1 and *ORIG_PATH_2, representing the changed
 * node and the two original targets passed to the diff command, to handle the
 * case when we're dealing with different anchors. RELATIVE_TO_DIR is the
 * directory the diff target should be considered relative to.
 * ANCHOR is the local path where the diff editor is anchored. The resulting
 * values are allocated in RESULT_POOL and temporary allocations are performed
 * in SCRATCH_POOL. */
static svn_error_t *
adjust_paths_for_diff_labels(const char **index_path,
                             const char **orig_path_1,
                             const char **orig_path_2,
                             const char *relative_to_dir,
                             const char *anchor,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const char *new_path = *index_path;
  const char *new_path1 = *orig_path_1;
  const char *new_path2 = *orig_path_2;

  if (anchor)
    new_path = svn_dirent_join(anchor, new_path, result_pool);

  if (relative_to_dir)
    {
      /* Possibly adjust the paths shown in the output (see issue #2723). */
      const char *child_path = svn_dirent_is_child(relative_to_dir, new_path,
                                                   result_pool);

      if (child_path)
        new_path = child_path;
      else if (! strcmp(relative_to_dir, new_path))
        new_path = ".";
      else
        return MAKE_ERR_BAD_RELATIVE_PATH(new_path, relative_to_dir);
    }

  {
    apr_size_t len;
    svn_boolean_t is_url1;
    svn_boolean_t is_url2;
    /* ### Holy cow.  Due to anchor/target weirdness, we can't
       simply join dwi->orig_path_1 with path, ditto for
       orig_path_2.  That will work when they're directory URLs, but
       not for file URLs.  Nor can we just use anchor1 and anchor2
       from do_diff(), at least not without some more logic here.
       What a nightmare.

       For now, to distinguish the two paths, we'll just put the
       unique portions of the original targets in parentheses after
       the received path, with ellipses for handwaving.  This makes
       the labels a bit clumsy, but at least distinctive.  Better
       solutions are possible, they'll just take more thought. */

    /* ### BH: We can now just construct the repos_relpath, etc. as the
           anchor is available. See also make_repos_relpath() */

    is_url1 = svn_path_is_url(new_path1);
    is_url2 = svn_path_is_url(new_path2);

    if (is_url1 && is_url2)
      len = strlen(svn_uri_get_longest_ancestor(new_path1, new_path2,
                                                scratch_pool));
    else if (!is_url1 && !is_url2)
      len = strlen(svn_dirent_get_longest_ancestor(new_path1, new_path2,
                                                   scratch_pool));
    else
      len = 0; /* Path and URL */

    new_path1 += len;
    new_path2 += len;
  }

  /* ### Should diff labels print paths in local style?  Is there
     already a standard for this?  In any case, this code depends on
     a particular style, so not calling svn_dirent_local_style() on the
     paths below.*/

  if (new_path[0] == '\0')
    new_path = ".";

  if (new_path1[0] == '\0')
    new_path1 = new_path;
  else if (svn_path_is_url(new_path1))
    new_path1 = apr_psprintf(result_pool, "%s\t(%s)", new_path, new_path1);
  else if (new_path1[0] == '/')
    new_path1 = apr_psprintf(result_pool, "%s\t(...%s)", new_path, new_path1);
  else
    new_path1 = apr_psprintf(result_pool, "%s\t(.../%s)", new_path, new_path1);

  if (new_path2[0] == '\0')
    new_path2 = new_path;
  else if (svn_path_is_url(new_path2))
    new_path2 = apr_psprintf(result_pool, "%s\t(%s)", new_path, new_path2);
  else if (new_path2[0] == '/')
    new_path2 = apr_psprintf(result_pool, "%s\t(...%s)", new_path, new_path2);
  else
    new_path2 = apr_psprintf(result_pool, "%s\t(.../%s)", new_path, new_path2);

  *index_path = new_path;
  *orig_path_1 = new_path1;
  *orig_path_2 = new_path2;

  return SVN_NO_ERROR;
}


/* Generate a label for the diff output for file PATH at revision REVNUM.
   If REVNUM is invalid then it is assumed to be the current working
   copy.  Assumes the paths are already in the desired style (local
   vs internal).  Allocate the label in RESULT-POOL. */
static const char *
diff_label(const char *path,
           svn_revnum_t revnum,
           apr_pool_t *result_pool)
{
  const char *label;
  if (revnum >= 0)
    label = apr_psprintf(result_pool, _("%s\t(revision %ld)"), path, revnum);
  else if (revnum == DIFF_REVNUM_NONEXISTENT)
    label = apr_psprintf(result_pool, _("%s\t(nonexistent)"), path);
  else /* SVN_INVALID_REVNUM */
    label = apr_psprintf(result_pool, _("%s\t(working copy)"), path);

  return label;
}

/* Standard modes produced in git style diffs */
static const int exec_mode =                 0755;
static const int noexec_mode =               0644;
static const int kind_file_mode =         0100000;
/*static const kind_dir_mode =            0040000;*/
static const int kind_symlink_mode =      0120000;

/* Print a git diff header for an addition within a diff between PATH1 and
 * PATH2 to the stream OS using HEADER_ENCODING. */
static svn_error_t *
print_git_diff_header_added(svn_stream_t *os, const char *header_encoding,
                            const char *path1, const char *path2,
                            svn_boolean_t exec_bit,
                            svn_boolean_t symlink_bit,
                            apr_pool_t *scratch_pool)
{
  int new_mode = (exec_bit ? exec_mode : noexec_mode)
                 | (symlink_bit ? kind_symlink_mode : kind_file_mode);

  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "diff --git a/%s b/%s%s",
                                      path1, path2, APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "new file mode %06o" APR_EOL_STR,
                                      new_mode));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a deletion within a diff between PATH1 and
 * PATH2 to the stream OS using HEADER_ENCODING. */
static svn_error_t *
print_git_diff_header_deleted(svn_stream_t *os, const char *header_encoding,
                              const char *path1, const char *path2,
                              svn_boolean_t exec_bit,
                              svn_boolean_t symlink_bit,
                              apr_pool_t *scratch_pool)
{
  int old_mode = (exec_bit ? exec_mode : noexec_mode)
                 | (symlink_bit ? kind_symlink_mode : kind_file_mode);
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "diff --git a/%s b/%s%s",
                                      path1, path2, APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "deleted file mode %06o" APR_EOL_STR,
                                      old_mode));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a copy from COPYFROM_PATH to PATH to the stream
 * OS using HEADER_ENCODING. */
static svn_error_t *
print_git_diff_header_copied(svn_stream_t *os, const char *header_encoding,
                             const char *copyfrom_path,
                             svn_revnum_t copyfrom_rev,
                             const char *path,
                             apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "diff --git a/%s b/%s%s",
                                      copyfrom_path, path, APR_EOL_STR));
  if (copyfrom_rev != SVN_INVALID_REVNUM)
    SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                        "copy from %s@%ld%s", copyfrom_path,
                                        copyfrom_rev, APR_EOL_STR));
  else
    SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                        "copy from %s%s", copyfrom_path,
                                        APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "copy to %s%s", path, APR_EOL_STR));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a rename from COPYFROM_PATH to PATH to the
 * stream OS using HEADER_ENCODING. */
static svn_error_t *
print_git_diff_header_renamed(svn_stream_t *os, const char *header_encoding,
                              const char *copyfrom_path, const char *path,
                              apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "diff --git a/%s b/%s%s",
                                      copyfrom_path, path, APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "rename from %s%s", copyfrom_path,
                                      APR_EOL_STR));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "rename to %s%s", path, APR_EOL_STR));
  return SVN_NO_ERROR;
}

/* Print a git diff header for a modification within a diff between PATH1 and
 * PATH2 to the stream OS using HEADER_ENCODING. */
static svn_error_t *
print_git_diff_header_modified(svn_stream_t *os, const char *header_encoding,
                               const char *path1, const char *path2,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "diff --git a/%s b/%s%s",
                                      path1, path2, APR_EOL_STR));
  return SVN_NO_ERROR;
}

/* Helper function for print_git_diff_header */
static svn_error_t *
maybe_print_mode_change(svn_stream_t *os,
                        const char *header_encoding,
                        svn_boolean_t exec_bit1,
                        svn_boolean_t exec_bit2,
                        svn_boolean_t symlink_bit1,
                        svn_boolean_t symlink_bit2,
                        const char *git_index_shas,
                        apr_pool_t *scratch_pool)
{
  int old_mode = (exec_bit1 ? exec_mode : noexec_mode)
                 | (symlink_bit1 ? kind_symlink_mode : kind_file_mode);
  int new_mode = (exec_bit2 ? exec_mode : noexec_mode)
                 | (symlink_bit2 ? kind_symlink_mode : kind_file_mode);
  if (old_mode == new_mode)
    {
      if (git_index_shas)
        SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                            "index %s %06o" APR_EOL_STR,
                                            git_index_shas, old_mode));
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "old mode %06o" APR_EOL_STR, old_mode));
  SVN_ERR(svn_stream_printf_from_utf8(os, header_encoding, scratch_pool,
                                      "new mode %06o" APR_EOL_STR, new_mode));
  return SVN_NO_ERROR;
}

/* Print a git diff header showing the OPERATION to the stream OS using
 * HEADER_ENCODING. Return suitable diff labels for the git diff in *LABEL1
 * and *LABEL2. REPOS_RELPATH1 and REPOS_RELPATH2 are relative to reposroot.
 * are the paths passed to the original diff command. REV1 and REV2 are
 * revisions being diffed. COPYFROM_PATH and COPYFROM_REV indicate where the
 * diffed item was copied from.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
print_git_diff_header(svn_stream_t *os,
                      const char **label1, const char **label2,
                      svn_diff_operation_kind_t operation,
                      const char *repos_relpath1,
                      const char *repos_relpath2,
                      svn_revnum_t rev1,
                      svn_revnum_t rev2,
                      const char *copyfrom_path,
                      svn_revnum_t copyfrom_rev,
                      apr_hash_t *left_props,
                      apr_hash_t *right_props,
                      const char *git_index_shas,
                      const char *header_encoding,
                      apr_pool_t *scratch_pool)
{
  svn_boolean_t exec_bit1 = (svn_prop_get_value(left_props,
                                                SVN_PROP_EXECUTABLE) != NULL);
  svn_boolean_t exec_bit2 = (svn_prop_get_value(right_props,
                                                SVN_PROP_EXECUTABLE) != NULL);
  svn_boolean_t symlink_bit1 = (svn_prop_get_value(left_props,
                                                   SVN_PROP_SPECIAL) != NULL);
  svn_boolean_t symlink_bit2 = (svn_prop_get_value(right_props,
                                                   SVN_PROP_SPECIAL) != NULL);

  if (operation == svn_diff_op_deleted)
    {
      SVN_ERR(print_git_diff_header_deleted(os, header_encoding,
                                            repos_relpath1, repos_relpath2,
                                            exec_bit1, symlink_bit1,
                                            scratch_pool));
      *label1 = diff_label(apr_psprintf(scratch_pool, "a/%s", repos_relpath1),
                           rev1, scratch_pool);
      *label2 = diff_label(apr_psprintf(scratch_pool, "b/%s", repos_relpath2),
                           rev2, scratch_pool);

    }
  else if (operation == svn_diff_op_copied)
    {
      SVN_ERR(print_git_diff_header_copied(os, header_encoding,
                                           copyfrom_path, copyfrom_rev,
                                           repos_relpath2,
                                           scratch_pool));
      *label1 = diff_label(apr_psprintf(scratch_pool, "a/%s", copyfrom_path),
                           rev1, scratch_pool);
      *label2 = diff_label(apr_psprintf(scratch_pool, "b/%s", repos_relpath2),
                           rev2, scratch_pool);
      SVN_ERR(maybe_print_mode_change(os, header_encoding,
                                      exec_bit1, exec_bit2,
                                      symlink_bit1, symlink_bit2,
                                      git_index_shas,
                                      scratch_pool));
    }
  else if (operation == svn_diff_op_added)
    {
      SVN_ERR(print_git_diff_header_added(os, header_encoding,
                                          repos_relpath1, repos_relpath2,
                                          exec_bit2, symlink_bit2,
                                          scratch_pool));
      *label1 = diff_label(apr_psprintf(scratch_pool, "a/%s", repos_relpath1),
                           rev1, scratch_pool);
      *label2 = diff_label(apr_psprintf(scratch_pool, "b/%s", repos_relpath2),
                           rev2, scratch_pool);
    }
  else if (operation == svn_diff_op_modified)
    {
      SVN_ERR(print_git_diff_header_modified(os, header_encoding,
                                             repos_relpath1, repos_relpath2,
                                             scratch_pool));
      *label1 = diff_label(apr_psprintf(scratch_pool, "a/%s", repos_relpath1),
                           rev1, scratch_pool);
      *label2 = diff_label(apr_psprintf(scratch_pool, "b/%s", repos_relpath2),
                           rev2, scratch_pool);
      SVN_ERR(maybe_print_mode_change(os, header_encoding,
                                      exec_bit1, exec_bit2,
                                      symlink_bit1, symlink_bit2,
                                      git_index_shas,
                                      scratch_pool));
    }
  else if (operation == svn_diff_op_moved)
    {
      SVN_ERR(print_git_diff_header_renamed(os, header_encoding,
                                            copyfrom_path, repos_relpath2,
                                            scratch_pool));
      *label1 = diff_label(apr_psprintf(scratch_pool, "a/%s", copyfrom_path),
                           rev1, scratch_pool);
      *label2 = diff_label(apr_psprintf(scratch_pool, "b/%s", repos_relpath2),
                           rev2, scratch_pool);
      SVN_ERR(maybe_print_mode_change(os, header_encoding,
                                      exec_bit1, exec_bit2,
                                      symlink_bit1, symlink_bit2,
                                      git_index_shas,
                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* A helper func that writes out verbal descriptions of property diffs
   to OUTSTREAM.   Of course, OUTSTREAM will probably be whatever was
   passed to svn_client_diff6(), which is probably stdout.

   ### FIXME needs proper docstring

   If USE_GIT_DIFF_FORMAT is TRUE, pring git diff headers, which always
   show paths relative to the repository root. RA_SESSION and WC_CTX are
   needed to normalize paths relative the repository root, and are ignored
   if USE_GIT_DIFF_FORMAT is FALSE.

   ANCHOR is the local path where the diff editor is anchored. */
static svn_error_t *
display_prop_diffs(const apr_array_header_t *propchanges,
                   apr_hash_t *left_props,
                   apr_hash_t *right_props,
                   const char *diff_relpath,
                   const char *anchor,
                   const char *orig_path1,
                   const char *orig_path2,
                   svn_revnum_t rev1,
                   svn_revnum_t rev2,
                   const char *encoding,
                   svn_stream_t *outstream,
                   const char *relative_to_dir,
                   svn_boolean_t show_diff_header,
                   svn_boolean_t use_git_diff_format,
                   const char *ra_session_relpath,
                   svn_cancel_func_t cancel_func,
                   void *cancel_baton,
                   svn_wc_context_t *wc_ctx,
                   apr_pool_t *scratch_pool)
{
  const char *repos_relpath1 = NULL;
  const char *repos_relpath2 = NULL;
  const char *index_path = diff_relpath;
  const char *adjusted_path1 = orig_path1;
  const char *adjusted_path2 = orig_path2;

  if (use_git_diff_format)
    {
      SVN_ERR(make_repos_relpath(&repos_relpath1, diff_relpath, orig_path1,
                                 ra_session_relpath, wc_ctx, anchor,
                                 scratch_pool, scratch_pool));
      SVN_ERR(make_repos_relpath(&repos_relpath2, diff_relpath, orig_path2,
                                 ra_session_relpath, wc_ctx, anchor,
                                 scratch_pool, scratch_pool));
    }

  /* If we're creating a diff on the wc root, path would be empty. */
  SVN_ERR(adjust_paths_for_diff_labels(&index_path, &adjusted_path1,
                                       &adjusted_path2,
                                       relative_to_dir, anchor,
                                       scratch_pool, scratch_pool));

  if (show_diff_header)
    {
      const char *label1;
      const char *label2;

      label1 = diff_label(adjusted_path1, rev1, scratch_pool);
      label2 = diff_label(adjusted_path2, rev2, scratch_pool);

      /* ### Should we show the paths in platform specific format,
       * ### diff_content_changed() does not! */

      SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, scratch_pool,
                                          "Index: %s" APR_EOL_STR
                                          SVN_DIFF__EQUAL_STRING APR_EOL_STR,
                                          index_path));

      if (use_git_diff_format)
        SVN_ERR(print_git_diff_header(outstream, &label1, &label2,
                                      svn_diff_op_modified,
                                      repos_relpath1, repos_relpath2,
                                      rev1, rev2, NULL,
                                      SVN_INVALID_REVNUM,
                                      left_props,
                                      right_props,
                                      NULL,
                                      encoding, scratch_pool));

      /* --- label1
       * +++ label2 */
      SVN_ERR(svn_diff__unidiff_write_header(
        outstream, encoding, label1, label2, scratch_pool));
    }

  SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, scratch_pool,
                                      APR_EOL_STR
                                      "Property changes on: %s"
                                      APR_EOL_STR,
                                      use_git_diff_format
                                            ? repos_relpath1
                                            : index_path));

  SVN_ERR(svn_stream_printf_from_utf8(outstream, encoding, scratch_pool,
                                      SVN_DIFF__UNDER_STRING APR_EOL_STR));

  SVN_ERR(svn_diff__display_prop_diffs(
            outstream, encoding, propchanges, left_props,
            TRUE /* pretty_print_mergeinfo */,
            -1 /* context_size */,
            cancel_func, cancel_baton, scratch_pool));

  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------*/

/*** Callbacks for 'svn diff', invoked by the repos-diff editor. ***/

/* State provided by the diff drivers; used by the diff writer */
typedef struct diff_driver_info_t
{
  /* The anchor to prefix before wc paths */
  const char *anchor;

   /* Relative path of ra session from repos_root_url */
  const char *session_relpath;

  /* The original targets passed to the diff command.  We may need
     these to construct distinctive diff labels when comparing the
     same relative path in the same revision, under different anchors
     (for example, when comparing a trunk against a branch). */
  const char *orig_path_1;
  const char *orig_path_2;
} diff_driver_info_t;


/* Diff writer state */
typedef struct diff_writer_info_t
{
  /* If non-null, the external diff command to invoke. */
  const char *diff_cmd;

  /* This is allocated in this struct's pool or a higher-up pool. */
  union {
    /* If 'diff_cmd' is null, then this is the parsed options to
       pass to the internal libsvn_diff implementation. */
    svn_diff_file_options_t *for_internal;
    /* Else if 'diff_cmd' is non-null, then... */
    struct {
      /* ...this is an argument array for the external command, and */
      const char **argv;
      /* ...this is the length of argv. */
      int argc;
    } for_external;
  } options;

  apr_pool_t *pool;
  svn_stream_t *outstream;
  svn_stream_t *errstream;

  const char *header_encoding;

  /* Set this if you want diff output even for binary files. */
  svn_boolean_t force_binary;

  /* The directory that diff target paths should be considered as
     relative to for output generation (see issue #2723). */
  const char *relative_to_dir;

  /* Whether property differences are ignored. */
  svn_boolean_t ignore_properties;

  /* Whether to show only property changes. */
  svn_boolean_t properties_only;

  /* Whether we're producing a git-style diff. */
  svn_boolean_t use_git_diff_format;

  /* Whether addition of a file is summarized versus showing a full diff. */
  svn_boolean_t no_diff_added;

  /* Whether deletion of a file is summarized versus showing a full diff. */
  svn_boolean_t no_diff_deleted;

  /* Whether to ignore copyfrom information when showing adds */
  svn_boolean_t show_copies_as_adds;

  /* Empty files for creating diffs or NULL if not used yet */
  const char *empty_file;

  svn_wc_context_t *wc_ctx;

  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  struct diff_driver_info_t ddi;
} diff_writer_info_t;

/* An helper for diff_dir_props_changed, diff_file_changed and diff_file_added
 */
static svn_error_t *
diff_props_changed(const char *diff_relpath,
                   svn_revnum_t rev1,
                   svn_revnum_t rev2,
                   const apr_array_header_t *propchanges,
                   apr_hash_t *left_props,
                   apr_hash_t *right_props,
                   svn_boolean_t show_diff_header,
                   diff_writer_info_t *dwi,
                   apr_pool_t *scratch_pool)
{
  apr_array_header_t *props;

  /* If property differences are ignored, there's nothing to do. */
  if (dwi->ignore_properties)
    return SVN_NO_ERROR;

  SVN_ERR(svn_categorize_props(propchanges, NULL, NULL, &props,
                               scratch_pool));

  if (props->nelts > 0)
    {
      /* We're using the revnums from the dwi since there's
       * no revision argument to the svn_wc_diff_callback_t
       * dir_props_changed(). */
      SVN_ERR(display_prop_diffs(props, left_props, right_props,
                                 diff_relpath,
                                 dwi->ddi.anchor,
                                 dwi->ddi.orig_path_1,
                                 dwi->ddi.orig_path_2,
                                 rev1,
                                 rev2,
                                 dwi->header_encoding,
                                 dwi->outstream,
                                 dwi->relative_to_dir,
                                 show_diff_header,
                                 dwi->use_git_diff_format,
                                 dwi->ddi.session_relpath,
                                 dwi->cancel_func,
                                 dwi->cancel_baton,
                                 dwi->wc_ctx,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Given a file ORIG_TMPFILE, return a path to a temporary file that lives at
 * least as long as RESULT_POOL, containing the git-like represention of
 * ORIG_TMPFILE */
static svn_error_t *
transform_link_to_git(const char **new_tmpfile,
                      const char **git_sha1,
                      const char *orig_tmpfile,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  apr_file_t *orig;
  apr_file_t *gitlike;
  svn_stringbuf_t *line;

  *git_sha1 = NULL;

  SVN_ERR(svn_io_file_open(&orig, orig_tmpfile, APR_READ, APR_OS_DEFAULT,
                           scratch_pool));
  SVN_ERR(svn_io_open_unique_file3(&gitlike, new_tmpfile, NULL,
                                   svn_io_file_del_on_pool_cleanup,
                                   result_pool, scratch_pool));

  SVN_ERR(svn_io_file_readline(orig, &line, NULL, NULL, 2 * APR_PATH_MAX + 2,
                               scratch_pool, scratch_pool));

  if (line->len > 5 && !strncmp(line->data, "link ", 5))
    {
      const char *sz_str;
      svn_checksum_t *checksum;

      svn_stringbuf_remove(line, 0, 5);

      SVN_ERR(svn_io_file_write_full(gitlike, line->data, line->len,
                                     NULL, scratch_pool));

      /* git calculates the sha over "blob X\0" + the actual data,
         where X is the decimal size of the blob. */
      sz_str = apr_psprintf(scratch_pool, "blob %u", (unsigned int)line->len);
      svn_stringbuf_insert(line, 0, sz_str, strlen(sz_str) + 1);

      SVN_ERR(svn_checksum(&checksum, svn_checksum_sha1,
                           line->data, line->len, scratch_pool));

      *git_sha1 = svn_checksum_to_cstring(checksum, result_pool);
    }
  else
    {
      /* Not a link... so can't convert */
      *new_tmpfile = apr_pstrdup(result_pool, orig_tmpfile);
    }

  SVN_ERR(svn_io_file_close(orig, scratch_pool));
  SVN_ERR(svn_io_file_close(gitlike, scratch_pool));
  return SVN_NO_ERROR;
}

/* Show differences between TMPFILE1 and TMPFILE2. DIFF_RELPATH, REV1, and
   REV2 are used in the headers to indicate the file and revisions.  If either
   MIMETYPE1 or MIMETYPE2 indicate binary content, don't show a diff,
   but instead print a warning message.

   If FORCE_DIFF is TRUE, always write a diff, even for empty diffs.

   Set *WROTE_HEADER to TRUE if a diff header was written */
static svn_error_t *
diff_content_changed(svn_boolean_t *wrote_header,
                     const char *diff_relpath,
                     const char *tmpfile1,
                     const char *tmpfile2,
                     svn_revnum_t rev1,
                     svn_revnum_t rev2,
                     apr_hash_t *left_props,
                     apr_hash_t *right_props,
                     svn_diff_operation_kind_t operation,
                     svn_boolean_t force_diff,
                     const char *copyfrom_path,
                     svn_revnum_t copyfrom_rev,
                     diff_writer_info_t *dwi,
                     apr_pool_t *scratch_pool)
{
  const char *rel_to_dir = dwi->relative_to_dir;
  svn_stream_t *outstream = dwi->outstream;
  const char *label1, *label2;
  svn_boolean_t mt1_binary = FALSE, mt2_binary = FALSE;
  const char *index_path = diff_relpath;
  const char *path1 = dwi->ddi.orig_path_1;
  const char *path2 = dwi->ddi.orig_path_2;
  const char *mimetype1 = svn_prop_get_value(left_props, SVN_PROP_MIME_TYPE);
  const char *mimetype2 = svn_prop_get_value(right_props, SVN_PROP_MIME_TYPE);
  const char *index_shas = NULL;

  /* If only property differences are shown, there's nothing to do. */
  if (dwi->properties_only)
    return SVN_NO_ERROR;

  /* Generate the diff headers. */
  SVN_ERR(adjust_paths_for_diff_labels(&index_path, &path1, &path2,
                                       rel_to_dir, dwi->ddi.anchor,
                                       scratch_pool, scratch_pool));

  label1 = diff_label(path1, rev1, scratch_pool);
  label2 = diff_label(path2, rev2, scratch_pool);

  /* Possible easy-out: if either mime-type is binary and force was not
     specified, don't attempt to generate a viewable diff at all.
     Print a warning and exit. */
  if (mimetype1)
    mt1_binary = svn_mime_type_is_binary(mimetype1);
  if (mimetype2)
    mt2_binary = svn_mime_type_is_binary(mimetype2);

  if (dwi->use_git_diff_format)
    {
      const char *l_hash = NULL;
      const char *r_hash = NULL;

      /* Change symlinks to their 'git like' plain format */
      if (svn_prop_get_value(left_props, SVN_PROP_SPECIAL))
        SVN_ERR(transform_link_to_git(&tmpfile1, &l_hash, tmpfile1,
                                      scratch_pool, scratch_pool));
      if (svn_prop_get_value(right_props, SVN_PROP_SPECIAL))
        SVN_ERR(transform_link_to_git(&tmpfile2, &r_hash, tmpfile2,
                                      scratch_pool, scratch_pool));

      if (l_hash && r_hash)
        {
          /* The symlink has changed. But we can't tell the user of the
             diff whether we are writing git diffs or svn diffs of the
             symlink... except when we add a git-like index line */

          l_hash = apr_pstrndup(scratch_pool, l_hash, 8);
          r_hash = apr_pstrndup(scratch_pool, r_hash, 8);

          index_shas = apr_psprintf(scratch_pool, "%8s..%8s",
                                    l_hash, r_hash);
        }
    }

  if (! dwi->force_binary && (mt1_binary || mt2_binary))
    {
      /* Print out the diff header. */
      SVN_ERR(svn_stream_printf_from_utf8(outstream,
               dwi->header_encoding, scratch_pool,
               "Index: %s" APR_EOL_STR
               SVN_DIFF__EQUAL_STRING APR_EOL_STR,
               index_path));

      *wrote_header = TRUE;

      /* ### Print git diff headers. */

      if (dwi->use_git_diff_format)
        {
          svn_stream_t *left_stream;
          svn_stream_t *right_stream;
          const char *repos_relpath1;
          const char *repos_relpath2;
          const char *copyfrom_repos_relpath = NULL;

          SVN_ERR(make_repos_relpath(&repos_relpath1, diff_relpath,
                                      dwi->ddi.orig_path_1,
                                      dwi->ddi.session_relpath,
                                      dwi->wc_ctx,
                                      dwi->ddi.anchor,
                                      scratch_pool, scratch_pool));
          SVN_ERR(make_repos_relpath(&repos_relpath2, diff_relpath,
                                      dwi->ddi.orig_path_2,
                                      dwi->ddi.session_relpath,
                                      dwi->wc_ctx,
                                      dwi->ddi.anchor,
                                      scratch_pool, scratch_pool));
          if (copyfrom_path)
            SVN_ERR(make_repos_relpath(&copyfrom_repos_relpath, copyfrom_path,
                                        dwi->ddi.orig_path_2,
                                        dwi->ddi.session_relpath,
                                        dwi->wc_ctx,
                                        dwi->ddi.anchor,
                                        scratch_pool, scratch_pool));
          SVN_ERR(print_git_diff_header(outstream, &label1, &label2,
                                        operation,
                                        repos_relpath1, repos_relpath2,
                                        rev1, rev2,
                                        copyfrom_repos_relpath,
                                        copyfrom_rev,
                                        left_props,
                                        right_props,
                                        index_shas,
                                        dwi->header_encoding,
                                        scratch_pool));

          SVN_ERR(svn_stream_open_readonly(&left_stream, tmpfile1,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_stream_open_readonly(&right_stream, tmpfile2,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_diff_output_binary(outstream,
                                         left_stream, right_stream,
                                         dwi->cancel_func, dwi->cancel_baton,
                                         scratch_pool));
        }
      else
        {
          SVN_ERR(svn_stream_printf_from_utf8(outstream,
                   dwi->header_encoding, scratch_pool,
                   _("Cannot display: file marked as a binary type.%s"),
                   APR_EOL_STR));

          if (mt1_binary && !mt2_binary)
            SVN_ERR(svn_stream_printf_from_utf8(outstream,
                     dwi->header_encoding, scratch_pool,
                     "svn:mime-type = %s" APR_EOL_STR, mimetype1));
          else if (mt2_binary && !mt1_binary)
            SVN_ERR(svn_stream_printf_from_utf8(outstream,
                     dwi->header_encoding, scratch_pool,
                     "svn:mime-type = %s" APR_EOL_STR, mimetype2));
          else if (mt1_binary && mt2_binary)
            {
              if (strcmp(mimetype1, mimetype2) == 0)
                SVN_ERR(svn_stream_printf_from_utf8(outstream,
                         dwi->header_encoding, scratch_pool,
                         "svn:mime-type = %s" APR_EOL_STR,
                         mimetype1));
              else
                SVN_ERR(svn_stream_printf_from_utf8(outstream,
                         dwi->header_encoding, scratch_pool,
                         "svn:mime-type = (%s, %s)" APR_EOL_STR,
                         mimetype1, mimetype2));
            }
        }

      /* Exit early. */
      return SVN_NO_ERROR;
    }


  if (dwi->diff_cmd)
    {
      svn_stream_t *errstream = dwi->errstream;
      apr_file_t *outfile;
      apr_file_t *errfile;
      const char *outfilename;
      const char *errfilename;
      svn_stream_t *stream;
      int exitcode;

      /* Print out the diff header. */
      SVN_ERR(svn_stream_printf_from_utf8(outstream,
               dwi->header_encoding, scratch_pool,
               "Index: %s" APR_EOL_STR
               SVN_DIFF__EQUAL_STRING APR_EOL_STR,
               index_path));

      /* ### Do we want to add git diff headers here too? I'd say no. The
       * ### 'Index' and '===' line is something subversion has added. The rest
       * ### is up to the external diff application. We may be dealing with
       * ### a non-git compatible diff application.*/

      /* We deal in streams, but svn_io_run_diff2() deals in file handles,
         so we may need to make temporary files and then copy the contents
         to our stream. */
      outfile = svn_stream__aprfile(outstream);
      if (outfile)
        outfilename = NULL;
      else
        SVN_ERR(svn_io_open_unique_file3(&outfile, &outfilename, NULL,
                                         svn_io_file_del_on_pool_cleanup,
                                         scratch_pool, scratch_pool));

      errfile = svn_stream__aprfile(errstream);
      if (errfile)
        errfilename = NULL;
      else
        SVN_ERR(svn_io_open_unique_file3(&errfile, &errfilename, NULL,
                                         svn_io_file_del_on_pool_cleanup,
                                         scratch_pool, scratch_pool));

      SVN_ERR(svn_io_run_diff2(".",
                               dwi->options.for_external.argv,
                               dwi->options.for_external.argc,
                               label1, label2,
                               tmpfile1, tmpfile2,
                               &exitcode, outfile, errfile,
                               dwi->diff_cmd, scratch_pool));

      /* Now, open and copy our files to our output streams. */
      if (outfilename)
        {
          SVN_ERR(svn_io_file_close(outfile, scratch_pool));
          SVN_ERR(svn_stream_open_readonly(&stream, outfilename,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_stream_copy3(stream, svn_stream_disown(outstream,
                                                             scratch_pool),
                                   NULL, NULL, scratch_pool));
        }
      if (errfilename)
        {
          SVN_ERR(svn_io_file_close(errfile, scratch_pool));
          SVN_ERR(svn_stream_open_readonly(&stream, errfilename,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_stream_copy3(stream, svn_stream_disown(errstream,
                                                             scratch_pool),
                                   NULL, NULL, scratch_pool));
        }

      /* If we have printed a diff for this path, mark it as visited. */
      if (exitcode == 1)
        *wrote_header = TRUE;
    }
  else   /* use libsvn_diff to generate the diff  */
    {
      svn_diff_t *diff;

      SVN_ERR(svn_diff_file_diff_2(&diff, tmpfile1, tmpfile2,
                                   dwi->options.for_internal,
                                   scratch_pool));

      if (force_diff
          || dwi->use_git_diff_format
          || svn_diff_contains_diffs(diff))
        {
          /* Print out the diff header. */
          SVN_ERR(svn_stream_printf_from_utf8(outstream,
                   dwi->header_encoding, scratch_pool,
                   "Index: %s" APR_EOL_STR
                   SVN_DIFF__EQUAL_STRING APR_EOL_STR,
                   index_path));

          if (dwi->use_git_diff_format)
            {
              const char *repos_relpath1;
              const char *repos_relpath2;
              const char *copyfrom_repos_relpath = NULL;

              SVN_ERR(make_repos_relpath(&repos_relpath1, diff_relpath,
                                         dwi->ddi.orig_path_1,
                                         dwi->ddi.session_relpath,
                                         dwi->wc_ctx,
                                         dwi->ddi.anchor,
                                         scratch_pool, scratch_pool));
              SVN_ERR(make_repos_relpath(&repos_relpath2, diff_relpath,
                                         dwi->ddi.orig_path_2,
                                         dwi->ddi.session_relpath,
                                         dwi->wc_ctx,
                                         dwi->ddi.anchor,
                                         scratch_pool, scratch_pool));
              if (copyfrom_path)
                SVN_ERR(make_repos_relpath(&copyfrom_repos_relpath,
                                           copyfrom_path,
                                           dwi->ddi.orig_path_2,
                                           dwi->ddi.session_relpath,
                                           dwi->wc_ctx,
                                           dwi->ddi.anchor,
                                           scratch_pool, scratch_pool));
              SVN_ERR(print_git_diff_header(outstream, &label1, &label2,
                                            operation,
                                            repos_relpath1, repos_relpath2,
                                            rev1, rev2,
                                            copyfrom_repos_relpath,
                                            copyfrom_rev,
                                            left_props,
                                            right_props,
                                            index_shas,
                                            dwi->header_encoding,
                                            scratch_pool));
            }

          /* Output the actual diff */
          if (force_diff || svn_diff_contains_diffs(diff))
            SVN_ERR(svn_diff_file_output_unified4(outstream, diff,
                     tmpfile1, tmpfile2, label1, label2,
                     dwi->header_encoding, rel_to_dir,
                     dwi->options.for_internal->show_c_function,
                     dwi->options.for_internal->context_size,
                     dwi->cancel_func, dwi->cancel_baton,
                     scratch_pool));

          /* If we have printed a diff for this path, mark it as visited. */
          if (dwi->use_git_diff_format || svn_diff_contains_diffs(diff))
            *wrote_header = TRUE;
        }
    }

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_file_changed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const char *left_file,
                  const char *right_file,
                  /*const*/ apr_hash_t *left_props,
                  /*const*/ apr_hash_t *right_props,
                  svn_boolean_t file_modified,
                  const apr_array_header_t *prop_changes,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;
  svn_boolean_t wrote_header = FALSE;

  if (file_modified)
    SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                 left_file, right_file,
                                 left_source->revision,
                                 right_source->revision,
                                 left_props, right_props,
                                 svn_diff_op_modified, FALSE,
                                 NULL,
                                 SVN_INVALID_REVNUM, dwi,
                                 scratch_pool));
  if (prop_changes->nelts > 0)
    SVN_ERR(diff_props_changed(relpath,
                               left_source->revision,
                               right_source->revision, prop_changes,
                               left_props, right_props, !wrote_header,
                               dwi, scratch_pool));
  return SVN_NO_ERROR;
}

/* Because the repos-diff editor passes at least one empty file to
   each of these next two functions, they can be dumb wrappers around
   the main workhorse routine. */

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_file_added(const char *relpath,
                const svn_diff_source_t *copyfrom_source,
                const svn_diff_source_t *right_source,
                const char *copyfrom_file,
                const char *right_file,
                /*const*/ apr_hash_t *copyfrom_props,
                /*const*/ apr_hash_t *right_props,
                void *file_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;
  svn_boolean_t wrote_header = FALSE;
  const char *left_file;
  apr_hash_t *left_props;
  apr_array_header_t *prop_changes;

  if (dwi->no_diff_added)
    {
      const char *index_path = relpath;

      if (dwi->ddi.anchor)
        index_path = svn_dirent_join(dwi->ddi.anchor, relpath,
                                     scratch_pool);

      SVN_ERR(svn_stream_printf_from_utf8(dwi->outstream,
                dwi->header_encoding, scratch_pool,
                "Index: %s (added)" APR_EOL_STR
                SVN_DIFF__EQUAL_STRING APR_EOL_STR,
                index_path));
      wrote_header = TRUE;
      return SVN_NO_ERROR;
    }

  /* During repos->wc diff of a copy revision numbers obtained
   * from the working copy are always SVN_INVALID_REVNUM. */
  if (copyfrom_source && !dwi->show_copies_as_adds)
    {
      left_file = copyfrom_file;
      left_props = copyfrom_props ? copyfrom_props : apr_hash_make(scratch_pool);
    }
  else
    {
      if (!dwi->empty_file)
        SVN_ERR(svn_io_open_unique_file3(NULL, &dwi->empty_file,
                                         NULL, svn_io_file_del_on_pool_cleanup,
                                         dwi->pool, scratch_pool));

      left_file = dwi->empty_file;
      left_props = apr_hash_make(scratch_pool);

      copyfrom_source = NULL;
      copyfrom_file = NULL;
    }

  SVN_ERR(svn_prop_diffs(&prop_changes, right_props, left_props, scratch_pool));

  if (copyfrom_source && right_file)
    SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                 left_file, right_file,
                                 copyfrom_source->revision,
                                 right_source->revision,
                                 left_props, right_props,
                                 copyfrom_source->moved_from_relpath
                                    ? svn_diff_op_moved
                                    : svn_diff_op_copied,
                                 TRUE /* force diff output */,
                                 copyfrom_source->moved_from_relpath
                                    ? copyfrom_source->moved_from_relpath
                                    : copyfrom_source->repos_relpath,
                                 copyfrom_source->revision,
                                 dwi, scratch_pool));
  else if (right_file)
    SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                 left_file, right_file,
                                 DIFF_REVNUM_NONEXISTENT,
                                 right_source->revision,
                                 left_props, right_props,
                                 svn_diff_op_added,
                                 TRUE /* force diff output */,
                                 NULL, SVN_INVALID_REVNUM,
                                 dwi, scratch_pool));

  if (prop_changes->nelts > 0)
    SVN_ERR(diff_props_changed(relpath,
                               copyfrom_source ? copyfrom_source->revision
                                               : DIFF_REVNUM_NONEXISTENT,
                               right_source->revision,
                               prop_changes,
                               left_props, right_props,
                               ! wrote_header, dwi, scratch_pool));

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_file_deleted(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const char *left_file,
                  /*const*/ apr_hash_t *left_props,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;

  if (dwi->no_diff_deleted)
    {
      const char *index_path = relpath;

      if (dwi->ddi.anchor)
        index_path = svn_dirent_join(dwi->ddi.anchor, relpath,
                                     scratch_pool);

      SVN_ERR(svn_stream_printf_from_utf8(dwi->outstream,
                dwi->header_encoding, scratch_pool,
                "Index: %s (deleted)" APR_EOL_STR
                SVN_DIFF__EQUAL_STRING APR_EOL_STR,
                index_path));
    }
  else
    {
      svn_boolean_t wrote_header = FALSE;

      if (!dwi->empty_file)
        SVN_ERR(svn_io_open_unique_file3(NULL, &dwi->empty_file,
                                         NULL, svn_io_file_del_on_pool_cleanup,
                                         dwi->pool, scratch_pool));

      if (left_file)
        SVN_ERR(diff_content_changed(&wrote_header, relpath,
                                     left_file, dwi->empty_file,
                                     left_source->revision,
                                     DIFF_REVNUM_NONEXISTENT,
                                     left_props,
                                     NULL,
                                     svn_diff_op_deleted, FALSE,
                                     NULL, SVN_INVALID_REVNUM,
                                     dwi,
                                     scratch_pool));

      if (left_props && apr_hash_count(left_props))
        {
          apr_array_header_t *prop_changes;

          SVN_ERR(svn_prop_diffs(&prop_changes, apr_hash_make(scratch_pool),
                                 left_props, scratch_pool));

          SVN_ERR(diff_props_changed(relpath,
                                     left_source->revision,
                                     DIFF_REVNUM_NONEXISTENT,
                                     prop_changes,
                                     left_props, NULL,
                                     ! wrote_header, dwi, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks4_t function. */
static svn_error_t *
diff_dir_changed(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 /*const*/ apr_hash_t *left_props,
                 /*const*/ apr_hash_t *right_props,
                 const apr_array_header_t *prop_changes,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;

  SVN_ERR(diff_props_changed(relpath,
                             left_source->revision,
                             right_source->revision,
                             prop_changes,
                             left_props, right_props,
                             TRUE /* show_diff_header */,
                             dwi,
                             scratch_pool));

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_dir_added(const char *relpath,
               const svn_diff_source_t *copyfrom_source,
               const svn_diff_source_t *right_source,
               /*const*/ apr_hash_t *copyfrom_props,
               /*const*/ apr_hash_t *right_props,
               void *dir_baton,
               const struct svn_diff_tree_processor_t *processor,
               apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;
  apr_hash_t *left_props;
  apr_array_header_t *prop_changes;

  if (dwi->no_diff_added)
    return SVN_NO_ERROR;

  if (copyfrom_source && !dwi->show_copies_as_adds)
    {
      left_props = copyfrom_props ? copyfrom_props
                                  : apr_hash_make(scratch_pool);
    }
  else
    {
      left_props = apr_hash_make(scratch_pool);
      copyfrom_source = NULL;
    }

  SVN_ERR(svn_prop_diffs(&prop_changes, right_props, left_props,
                         scratch_pool));

  return svn_error_trace(diff_props_changed(relpath,
                                            copyfrom_source ? copyfrom_source->revision
                                                            : DIFF_REVNUM_NONEXISTENT,
                                            right_source->revision,
                                            prop_changes,
                                            left_props, right_props,
                                            TRUE /* show_diff_header */,
                                            dwi,
                                            scratch_pool));
}

/* An svn_diff_tree_processor_t callback. */
static svn_error_t *
diff_dir_deleted(const char *relpath,
                 const svn_diff_source_t *left_source,
                 /*const*/ apr_hash_t *left_props,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  diff_writer_info_t *dwi = processor->baton;
  apr_array_header_t *prop_changes;
  apr_hash_t *right_props;

  if (dwi->no_diff_deleted)
    return SVN_NO_ERROR;

  right_props = apr_hash_make(scratch_pool);
  SVN_ERR(svn_prop_diffs(&prop_changes, right_props,
                         left_props, scratch_pool));

  SVN_ERR(diff_props_changed(relpath,
                             left_source->revision,
                             DIFF_REVNUM_NONEXISTENT,
                             prop_changes,
                             left_props, right_props,
                             TRUE /* show_diff_header */,
                             dwi,
                             scratch_pool));

  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------*/

/** The logic behind 'svn diff' and 'svn merge'.  */


/* Hi!  This is a comment left behind by Karl, and Ben is too afraid
   to erase it at this time, because he's not fully confident that all
   this knowledge has been grokked yet.

   There are five cases:
      1. path is not a URL and start_revision != end_revision
      2. path is not a URL and start_revision == end_revision
      3. path is a URL and start_revision != end_revision
      4. path is a URL and start_revision == end_revision
      5. path is not a URL and no revisions given

   With only one distinct revision the working copy provides the
   other.  When path is a URL there is no working copy. Thus

     1: compare repository versions for URL coresponding to working copy
     2: compare working copy against repository version
     3: compare repository versions for URL
     4: nothing to do.
     5: compare working copy against text-base

   Case 4 is not as stupid as it looks, for example it may occur if
   the user specifies two dates that resolve to the same revision.  */


/** Check if paths PATH_OR_URL1 and PATH_OR_URL2 are urls and if the
 * revisions REVISION1 and REVISION2 are local. If PEG_REVISION is not
 * unspecified, ensure that at least one of the two revisions is not
 * BASE or WORKING.
 * If PATH_OR_URL1 can only be found in the repository, set *IS_REPOS1
 * to TRUE. If PATH_OR_URL2 can only be found in the repository, set
 * *IS_REPOS2 to TRUE. */
static svn_error_t *
check_paths(svn_boolean_t *is_repos1,
            svn_boolean_t *is_repos2,
            const char *path_or_url1,
            const char *path_or_url2,
            const svn_opt_revision_t *revision1,
            const svn_opt_revision_t *revision2,
            const svn_opt_revision_t *peg_revision)
{
  svn_boolean_t is_local_rev1, is_local_rev2;

  /* Verify our revision arguments in light of the paths. */
  if ((revision1->kind == svn_opt_revision_unspecified)
      || (revision2->kind == svn_opt_revision_unspecified))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("Not all required revisions are specified"));

  /* Revisions can be said to be local or remote.
   * BASE and WORKING are local revisions.  */
  is_local_rev1 =
    ((revision1->kind == svn_opt_revision_base)
     || (revision1->kind == svn_opt_revision_working));
  is_local_rev2 =
    ((revision2->kind == svn_opt_revision_base)
     || (revision2->kind == svn_opt_revision_working));

  if (peg_revision->kind != svn_opt_revision_unspecified &&
      is_local_rev1 && is_local_rev2)
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("At least one revision must be something other "
                              "than BASE or WORKING when diffing a URL"));

  /* Working copy paths with non-local revisions get turned into
     URLs.  We don't do that here, though.  We simply record that it
     needs to be done, which is information that helps us choose our
     diff helper function.  */
  *is_repos1 = ! is_local_rev1 || svn_path_is_url(path_or_url1);
  *is_repos2 = ! is_local_rev2 || svn_path_is_url(path_or_url2);

  return SVN_NO_ERROR;
}

/* Raise an error if the diff target URL does not exist at REVISION.
 * If REVISION does not equal OTHER_REVISION, mention both revisions in
 * the error message. Use RA_SESSION to contact the repository.
 * Use POOL for temporary allocations. */
static svn_error_t *
check_diff_target_exists(const char *url,
                         svn_revnum_t revision,
                         svn_revnum_t other_revision,
                         svn_ra_session_t *ra_session,
                         apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *session_url;

  SVN_ERR(svn_ra_get_session_url(ra_session, &session_url, pool));

  if (strcmp(url, session_url) != 0)
    SVN_ERR(svn_ra_reparent(ra_session, url, pool));

  SVN_ERR(svn_ra_check_path(ra_session, "", revision, &kind, pool));
  if (kind == svn_node_none)
    {
      if (revision == other_revision)
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff target '%s' was not found in the "
                                   "repository at revision '%ld'"),
                                 url, revision);
      else
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff target '%s' was not found in the "
                                   "repository at revision '%ld' or '%ld'"),
                                 url, revision, other_revision);
     }

  if (strcmp(url, session_url) != 0)
    SVN_ERR(svn_ra_reparent(ra_session, session_url, pool));

  return SVN_NO_ERROR;
}

/** Prepare a repos repos diff between PATH_OR_URL1 and
 * PATH_OR_URL2@PEG_REVISION, in the revision range REVISION1:REVISION2.
 * Return URLs and peg revisions in *URL1, *REV1 and in *URL2, *REV2.
 * Return suitable anchors in *ANCHOR1 and *ANCHOR2, and targets in
 * *TARGET1 and *TARGET2, based on *URL1 and *URL2.
 * Indicate the corresponding node kinds in *KIND1 and *KIND2, and verify
 * that at least one of the diff targets exists.
 * Use client context CTX. Do all allocations in POOL. */
static svn_error_t *
diff_prepare_repos_repos(const char **url1,
                         const char **url2,
                         svn_revnum_t *rev1,
                         svn_revnum_t *rev2,
                         const char **anchor1,
                         const char **anchor2,
                         const char **target1,
                         const char **target2,
                         svn_node_kind_t *kind1,
                         svn_node_kind_t *kind2,
                         svn_ra_session_t **ra_session,
                         svn_client_ctx_t *ctx,
                         const char *path_or_url1,
                         const char *path_or_url2,
                         const svn_opt_revision_t *revision1,
                         const svn_opt_revision_t *revision2,
                         const svn_opt_revision_t *peg_revision,
                         apr_pool_t *pool)
{
  const char *local_abspath1 = NULL;
  const char *local_abspath2 = NULL;
  const char *repos_root_url;
  const char *wri_abspath = NULL;
  svn_client__pathrev_t *resolved1;
  svn_client__pathrev_t *resolved2 = NULL;
  enum svn_opt_revision_kind peg_kind = peg_revision->kind;

  if (!svn_path_is_url(path_or_url2))
    {
      SVN_ERR(svn_dirent_get_absolute(&local_abspath2, path_or_url2, pool));
      SVN_ERR(svn_wc__node_get_url(url2, ctx->wc_ctx, local_abspath2,
                                   pool, pool));
      wri_abspath = local_abspath2;
    }
  else
    *url2 = apr_pstrdup(pool, path_or_url2);

  if (!svn_path_is_url(path_or_url1))
    {
      SVN_ERR(svn_dirent_get_absolute(&local_abspath1, path_or_url1, pool));
      wri_abspath = local_abspath1;
    }

  SVN_ERR(svn_client_open_ra_session2(ra_session, *url2, wri_abspath,
                                      ctx, pool, pool));

  /* If we are performing a pegged diff, we need to find out what our
     actual URLs will be. */
  if (peg_kind != svn_opt_revision_unspecified
      || path_or_url1 == path_or_url2
      || local_abspath2)
    {
      svn_error_t *err;

      err = svn_client__resolve_rev_and_url(&resolved2,
                                            *ra_session, path_or_url2,
                                            peg_revision, revision2,
                                            ctx, pool);
      if (err)
        {
          if (err->apr_err != SVN_ERR_CLIENT_UNRELATED_RESOURCES
              && err->apr_err != SVN_ERR_FS_NOT_FOUND)
            return svn_error_trace(err);

          svn_error_clear(err);
          resolved2 = NULL;
        }
    }
  else
    resolved2 = NULL;

  if (peg_kind != svn_opt_revision_unspecified
      || path_or_url1 == path_or_url2
      || local_abspath1)
    {
      svn_error_t *err;

      err = svn_client__resolve_rev_and_url(&resolved1,
                                            *ra_session, path_or_url1,
                                            peg_revision, revision1,
                                            ctx, pool);
      if (err)
        {
          if (err->apr_err != SVN_ERR_CLIENT_UNRELATED_RESOURCES
              && err->apr_err != SVN_ERR_FS_NOT_FOUND)
            return svn_error_trace(err);

          svn_error_clear(err);
          resolved1 = NULL;
        }
    }
  else
    resolved1 = NULL;

  if (resolved1)
    {
      *url1 = resolved1->url;
      *rev1 = resolved1->rev;
    }
  else
    {
      /* It would be nice if we could just return an error when resolving a
         location fails... But in many such cases we prefer diffing against
         an not existing location to show adds od removes (see issue #4153) */

      if (resolved2
          && (peg_kind != svn_opt_revision_unspecified
              || path_or_url1 == path_or_url2))
        *url1 = resolved2->url;
      else if (! local_abspath1)
        *url1 = path_or_url1;
      else
        SVN_ERR(svn_wc__node_get_url(url1, ctx->wc_ctx, local_abspath1,
                                     pool, pool));

      SVN_ERR(svn_client__get_revision_number(rev1, NULL, ctx->wc_ctx,
                                              local_abspath1 /* may be NULL */,
                                              *ra_session, revision1, pool));
    }

  if (resolved2)
    {
      *url2 = resolved2->url;
      *rev2 = resolved2->rev;
    }
  else
    {
      /* It would be nice if we could just return an error when resolving a
         location fails... But in many such cases we prefer diffing against
         an not existing location to show adds od removes (see issue #4153) */

      if (resolved1
          && (peg_kind != svn_opt_revision_unspecified
              || path_or_url1 == path_or_url2))
        *url2 = resolved1->url;
      /* else keep url2 */

      SVN_ERR(svn_client__get_revision_number(rev2, NULL, ctx->wc_ctx,
                                              local_abspath2 /* may be NULL */,
                                              *ra_session, revision2, pool));
    }

  /* Resolve revision and get path kind for the second target. */
  SVN_ERR(svn_ra_reparent(*ra_session, *url2, pool));
  SVN_ERR(svn_ra_check_path(*ra_session, "", *rev2, kind2, pool));

  /* Do the same for the first target. */
  SVN_ERR(svn_ra_reparent(*ra_session, *url1, pool));
  SVN_ERR(svn_ra_check_path(*ra_session, "", *rev1, kind1, pool));

  /* Either both URLs must exist at their respective revisions,
   * or one of them may be missing from one side of the diff. */
  if (*kind1 == svn_node_none && *kind2 == svn_node_none)
    {
      if (strcmp(*url1, *url2) == 0)
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff target '%s' was not found in the "
                                   "repository at revisions '%ld' and '%ld'"),
                                 *url1, *rev1, *rev2);
      else
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Diff targets '%s' and '%s' were not found "
                                   "in the repository at revisions '%ld' and "
                                   "'%ld'"),
                                 *url1, *url2, *rev1, *rev2);
    }
  else if (*kind1 == svn_node_none)
    SVN_ERR(check_diff_target_exists(*url1, *rev2, *rev1, *ra_session, pool));
  else if (*kind2 == svn_node_none)
    SVN_ERR(check_diff_target_exists(*url2, *rev1, *rev2, *ra_session, pool));

  SVN_ERR(svn_ra_get_repos_root2(*ra_session, &repos_root_url, pool));

  /* Choose useful anchors and targets for our two URLs. */
  *anchor1 = *url1;
  *anchor2 = *url2;
  *target1 = "";
  *target2 = "";

  /* If none of the targets is the repository root open the parent directory
     to allow describing replacement of the target itself */
  if (strcmp(*url1, repos_root_url) != 0
      && strcmp(*url2, repos_root_url) != 0)
    {
      svn_node_kind_t ignored_kind;
      svn_error_t *err;

      svn_uri_split(anchor1, target1, *url1, pool);
      svn_uri_split(anchor2, target2, *url2, pool);

      SVN_ERR(svn_ra_reparent(*ra_session, *anchor1, pool));

      /* We might not have the necessary rights to read the root now.
         (It is ok to pass a revision here where the node doesn't exist) */
      err = svn_ra_check_path(*ra_session, "", *rev1, &ignored_kind, pool);

      if (err && (err->apr_err == SVN_ERR_RA_DAV_FORBIDDEN
                  || err->apr_err == SVN_ERR_RA_NOT_AUTHORIZED))
        {
          svn_error_clear(err);

          /* Ok, lets undo the reparent...

             We can't report replacements this way, but at least we can
             report changes on the descendants */

          *anchor1 = svn_path_url_add_component2(*anchor1, *target1, pool);
          *anchor2 = svn_path_url_add_component2(*anchor2, *target2, pool);
          *target1 = "";
          *target2 = "";

          SVN_ERR(svn_ra_reparent(*ra_session, *anchor1, pool));
        }
      else
        SVN_ERR(err);
    }

  return SVN_NO_ERROR;
}

/* A Theoretical Note From Ben, regarding do_diff().

   This function is really svn_client_diff6().  If you read the public
   API description for svn_client_diff6(), it sounds quite Grand.  It
   sounds really generalized and abstract and beautiful: that it will
   diff any two paths, be they working-copy paths or URLs, at any two
   revisions.

   Now, the *reality* is that we have exactly three 'tools' for doing
   diffing, and thus this routine is built around the use of the three
   tools.  Here they are, for clarity:

     - svn_wc_diff:  assumes both paths are the same wcpath.
                     compares wcpath@BASE vs. wcpath@WORKING

     - svn_wc_get_diff_editor:  compares some URL@REV vs. wcpath@WORKING

     - svn_client__get_diff_editor:  compares some URL1@REV1 vs. URL2@REV2

   Since Subversion 1.8 we also have a variant of svn_wc_diff called
   svn_client__arbitrary_nodes_diff, that allows handling WORKING-WORKING
   comparisons between nodes in the working copy.

   So the truth of the matter is, if the caller's arguments can't be
   pigeonholed into one of these use-cases, we currently bail with a
   friendly apology.

   Perhaps someday a brave soul will truly make svn_client_diff6()
   perfectly general.  For now, we live with the 90% case.  Certainly,
   the commandline client only calls this function in legal ways.
   When there are other users of svn_client.h, maybe this will become
   a more pressing issue.
 */

/* Return a "you can't do that" error, optionally wrapping another
   error CHILD_ERR. */
static svn_error_t *
unsupported_diff_error(svn_error_t *child_err)
{
  return svn_error_create(SVN_ERR_INCORRECT_PARAMS, child_err,
                          _("Sorry, svn_client_diff6 was called in a way "
                            "that is not yet supported"));
}

/* Perform a diff between two working-copy paths.

   PATH1 and PATH2 are both working copy paths.  REVISION1 and
   REVISION2 are their respective revisions.

   All other options are the same as those passed to svn_client_diff6(). */
static svn_error_t *
diff_wc_wc(const char **root_relpath,
           svn_boolean_t *root_is_dir,
           struct diff_driver_info_t *ddi,
           const char *path1,
           const svn_opt_revision_t *revision1,
           const char *path2,
           const svn_opt_revision_t *revision2,
           svn_depth_t depth,
           svn_boolean_t ignore_ancestry,
           const apr_array_header_t *changelists,
           const svn_diff_tree_processor_t *diff_processor,
           svn_client_ctx_t *ctx,
           apr_pool_t *result_pool,
           apr_pool_t *scratch_pool)
{
  const char *abspath1;

  SVN_ERR_ASSERT(! svn_path_is_url(path1));
  SVN_ERR_ASSERT(! svn_path_is_url(path2));

  SVN_ERR(svn_dirent_get_absolute(&abspath1, path1, scratch_pool));

  /* Currently we support only the case where path1 and path2 are the
     same path. */
  if ((strcmp(path1, path2) != 0)
      || (! ((revision1->kind == svn_opt_revision_base)
             && (revision2->kind == svn_opt_revision_working))))
    return unsupported_diff_error(
       svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                        _("Only diffs between a path's text-base "
                          "and its working files are supported at this time"
                          )));

  if (ddi)
    {
      svn_node_kind_t kind;

      SVN_ERR(svn_wc_read_kind2(&kind, ctx->wc_ctx, abspath1,
                              TRUE, FALSE, scratch_pool));

      if (kind != svn_node_dir)
        ddi->anchor = svn_dirent_dirname(path1, scratch_pool);
      else
        ddi->anchor = path1;
    }

  SVN_ERR(svn_wc__diff7(root_relpath, root_is_dir,
                        ctx->wc_ctx, abspath1, depth,
                        ignore_ancestry, changelists,
                        diff_processor,
                        ctx->cancel_func, ctx->cancel_baton,
                        result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

/* Perform a diff between two repository paths.

   PATH_OR_URL1 and PATH_OR_URL2 may be either URLs or the working copy paths.
   REVISION1 and REVISION2 are their respective revisions.
   If PEG_REVISION is specified, PATH_OR_URL2 is the path at the peg revision,
   and the actual two paths compared are determined by following copy
   history from PATH_OR_URL2.

   All other options are the same as those passed to svn_client_diff6(). */
static svn_error_t *
diff_repos_repos(const char **root_relpath,
                 svn_boolean_t *root_is_dir,
                 struct diff_driver_info_t *ddi,
                 const char *path_or_url1,
                 const char *path_or_url2,
                 const svn_opt_revision_t *revision1,
                 const svn_opt_revision_t *revision2,
                 const svn_opt_revision_t *peg_revision,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t text_deltas,
                 const svn_diff_tree_processor_t *diff_processor,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  svn_ra_session_t *extra_ra_session;

  const svn_ra_reporter3_t *reporter;
  void *reporter_baton;

  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;

  const char *url1;
  const char *url2;
  svn_revnum_t rev1;
  svn_revnum_t rev2;
  svn_node_kind_t kind1;
  svn_node_kind_t kind2;
  const char *anchor1;
  const char *anchor2;
  const char *target1;
  const char *target2;
  svn_ra_session_t *ra_session;

  /* Prepare info for the repos repos diff. */
  SVN_ERR(diff_prepare_repos_repos(&url1, &url2, &rev1, &rev2,
                                   &anchor1, &anchor2, &target1, &target2,
                                   &kind1, &kind2, &ra_session,
                                   ctx, path_or_url1, path_or_url2,
                                   revision1, revision2, peg_revision,
                                   scratch_pool));

  /* Set up the repos_diff editor on BASE_PATH, if available.
     Otherwise, we just use "". */

  if (ddi)
    {
      /* Get actual URLs. */
      ddi->orig_path_1 = url1;
      ddi->orig_path_2 = url2;

      /* This should be moved to the diff writer
         - path_or_url are provided by the caller
         - target1 is available as *root_relpath
         - (kind1 != svn_node_dir || kind2 != svn_node_dir) = !*root_is_dir */

      if (!svn_path_is_url(path_or_url2))
        ddi->anchor = path_or_url2;
      else if (!svn_path_is_url(path_or_url1))
        ddi->anchor = path_or_url1;
      else
        ddi->anchor = NULL;

      if (*target1 && ddi->anchor
          && (kind1 != svn_node_dir || kind2 != svn_node_dir))
        ddi->anchor = svn_dirent_dirname(ddi->anchor, result_pool);
    }

  /* The repository can bring in a new working copy, but not delete
     everything. Luckily our new diff handler can just be reversed. */
  if (kind2 == svn_node_none)
    {
      const char *str_tmp;
      svn_revnum_t rev_tmp;

      str_tmp = url2;
      url2 = url1;
      url1 = str_tmp;

      rev_tmp = rev2;
      rev2 = rev1;
      rev1 = rev_tmp;

      str_tmp = anchor2;
      anchor2 = anchor1;
      anchor1 = str_tmp;

      str_tmp = target2;
      target2 = target1;
      target1 = str_tmp;

      diff_processor = svn_diff__tree_processor_reverse_create(diff_processor,
                                                               NULL,
                                                               scratch_pool);
    }

  /* Filter the first path component using a filter processor, until we fixed
     the diff processing to handle this directly */
  if (root_relpath)
    *root_relpath = apr_pstrdup(result_pool, target1);
  else if ((kind1 != svn_node_file && kind2 != svn_node_file)
           && target1[0] != '\0')
    {
      diff_processor = svn_diff__tree_processor_filter_create(
                                        diff_processor, target1, scratch_pool);
    }

  /* Now, we open an extra RA session to the correct anchor
     location for URL1.  This is used during the editor calls to fetch file
     contents.  */
  SVN_ERR(svn_ra__dup_session(&extra_ra_session, ra_session, anchor1,
                              scratch_pool, scratch_pool));

  if (ddi)
    {
      const char *repos_root_url;
      const char *session_url;

      SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url,
                                      scratch_pool));
      SVN_ERR(svn_ra_get_session_url(ra_session, &session_url,
                                      scratch_pool));

      ddi->session_relpath = svn_uri_skip_ancestor(repos_root_url,
                                                    session_url,
                                                    result_pool);
    }

  SVN_ERR(svn_client__get_diff_editor2(
                &diff_editor, &diff_edit_baton,
                extra_ra_session, depth,
                rev1,
                text_deltas,
                diff_processor,
                ctx->cancel_func, ctx->cancel_baton,
                scratch_pool));

  /* We want to switch our txn into URL2 */
  SVN_ERR(svn_ra_do_diff3(ra_session, &reporter, &reporter_baton,
                          rev2, target1,
                          depth, ignore_ancestry, text_deltas,
                          url2, diff_editor, diff_edit_baton, scratch_pool));

  /* Drive the reporter; do the diff. */
  SVN_ERR(reporter->set_path(reporter_baton, "", rev1,
                             svn_depth_infinity,
                             FALSE, NULL,
                             scratch_pool));

  return svn_error_trace(
                  reporter->finish_report(reporter_baton, scratch_pool));
}

/* Perform a diff between a repository path and a working-copy path.

   PATH_OR_URL1 may be either a URL or a working copy path.  PATH2 is a
   working copy path.  REVISION1 is the revision of URL1. If PEG_REVISION1
   is specified, then PATH_OR_URL1 is the path in the peg revision, and the
   actual repository path to be compared is determined by following copy
   history.

   REVISION_KIND2 specifies which revision should be reported from the
   working copy (BASE or WORKING)

   If REVERSE is TRUE, the diff will be reported in reverse.

   All other options are the same as those passed to svn_client_diff6(). */
static svn_error_t *
diff_repos_wc(const char **root_relpath,
              svn_boolean_t *root_is_dir,
              struct diff_driver_info_t *ddi,
              const char *path_or_url1,
              const svn_opt_revision_t *revision1,
              const svn_opt_revision_t *peg_revision1,
              const char *path2,
              enum svn_opt_revision_kind revision2_kind,
              svn_boolean_t reverse,
              svn_depth_t depth,
              svn_boolean_t ignore_ancestry,
              const apr_array_header_t *changelists,
              const svn_diff_tree_processor_t *diff_processor,
              svn_client_ctx_t *ctx,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  const char *anchor, *anchor_url, *target;
  svn_ra_session_t *ra_session;
  svn_depth_t diff_depth;
  const svn_ra_reporter3_t *reporter;
  void *reporter_baton;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  svn_boolean_t rev2_is_base = (revision2_kind == svn_opt_revision_base);
  svn_boolean_t server_supports_depth;
  const char *abspath_or_url1;
  const char *abspath2;
  const char *anchor_abspath;
  svn_boolean_t is_copy;
  svn_revnum_t cf_revision;
  const char *cf_repos_relpath;
  const char *cf_repos_root_url;
  svn_depth_t cf_depth;
  const char *copy_root_abspath;
  const char *target_url;
  svn_client__pathrev_t *loc1;

  SVN_ERR_ASSERT(! svn_path_is_url(path2));

  if (!svn_path_is_url(path_or_url1))
    {
      SVN_ERR(svn_dirent_get_absolute(&abspath_or_url1, path_or_url1,
                                      scratch_pool));
    }
  else
    {
      abspath_or_url1 = path_or_url1;
    }

  SVN_ERR(svn_dirent_get_absolute(&abspath2, path2, scratch_pool));

  /* Check if our diff target is a copied node. */
  SVN_ERR(svn_wc__node_get_origin(&is_copy,
                                  &cf_revision,
                                  &cf_repos_relpath,
                                  &cf_repos_root_url,
                                  NULL, &cf_depth,
                                  &copy_root_abspath,
                                  ctx->wc_ctx, abspath2,
                                  FALSE, scratch_pool, scratch_pool));

  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc1,
                                            path_or_url1, abspath2,
                                            peg_revision1, revision1,
                                            ctx, scratch_pool));

  if (revision2_kind == svn_opt_revision_base || !is_copy)
    {
      /* Convert path_or_url1 to a URL to feed to do_diff. */
      SVN_ERR(svn_wc_get_actual_target2(&anchor, &target, ctx->wc_ctx, path2,
                                        scratch_pool, scratch_pool));

      /* Handle the ugly case where target is ".." */
      if (*target && !svn_path_is_single_path_component(target))
        {
          anchor = svn_dirent_join(anchor, target, scratch_pool);
          target = "";
        }

      if (root_relpath)
        *root_relpath = apr_pstrdup(result_pool, target);
      if (root_is_dir)
        *root_is_dir = (*target == '\0');

      /* Fetch the URL of the anchor directory. */
      SVN_ERR(svn_dirent_get_absolute(&anchor_abspath, anchor, scratch_pool));
      SVN_ERR(svn_wc__node_get_url(&anchor_url, ctx->wc_ctx, anchor_abspath,
                                   scratch_pool, scratch_pool));
      SVN_ERR_ASSERT(anchor_url != NULL);

      target_url = NULL;
    }
  else /* is_copy && revision2->kind == svn_opt_revision_base */
    {
#if 0
      svn_node_kind_t kind;
#endif
      /* ### Ugly hack ahead ###
       *
       * We're diffing a locally copied/moved node.
       * Describe the copy source to the reporter instead of the copy itself.
       * Doing the latter would generate a single add_directory() call to the
       * diff editor which results in an unexpected diff (the copy would
       * be shown as deleted).
       *
       * ### But if we will receive any real changes from the repositor we
       * will most likely fail to apply them as the wc diff editor assumes
       * that we have the data to which the change applies in BASE...
       */

      target_url = svn_path_url_add_component2(cf_repos_root_url,
                                               cf_repos_relpath,
                                               scratch_pool);

#if 0
      /*SVN_ERR(svn_wc_read_kind2(&kind, ctx->wc_ctx, abspath2, FALSE, FALSE,
                                scratch_pool));

      if (kind != svn_node_dir
          || strcmp(copy_root_abspath, abspath2) != 0) */
#endif
        {
          /* We are looking at a subdirectory of the repository,
             We can describe the parent directory as the anchor..

             ### This 'appears to work', but that is really dumb luck
             ### for the simple cases in the test suite */
          anchor_abspath = svn_dirent_dirname(abspath2, scratch_pool);
          anchor_url = svn_path_url_add_component2(cf_repos_root_url,
                                                   svn_relpath_dirname(
                                                            cf_repos_relpath,
                                                            scratch_pool),
                                                   scratch_pool);
          target = svn_dirent_basename(abspath2, NULL);
          anchor = svn_dirent_dirname(path2, scratch_pool);
        }
#if 0
      else
        {
          /* This code, while ok can't be enabled without causing test
           * failures. The repository will send some changes against
           * BASE for nodes that don't have BASE...
           */
          anchor_abspath = abspath2;
          anchor_url = svn_path_url_add_component2(cf_repos_root_url,
                                                   cf_repos_relpath,
                                                   scratch_pool);
          anchor = path2;
          target = "";
        }
#endif
    }

  SVN_ERR(svn_ra_reparent(ra_session, anchor_url, scratch_pool));

  if (ddi)
    {
      const char *repos_root_url;

      ddi->anchor = anchor;

      if (!reverse)
        {
          ddi->orig_path_1 = apr_pstrdup(result_pool, loc1->url);
          ddi->orig_path_2 =
            svn_path_url_add_component2(anchor_url, target, result_pool);
        }
      else
        {
          ddi->orig_path_1 =
            svn_path_url_add_component2(anchor_url, target, result_pool);
          ddi->orig_path_2 = apr_pstrdup(result_pool, loc1->url);
        }

      SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url,
                                      scratch_pool));

      ddi->session_relpath = svn_uri_skip_ancestor(repos_root_url,
                                                   anchor_url,
                                                   result_pool);
    }

  if (reverse)
    diff_processor = svn_diff__tree_processor_reverse_create(
                              diff_processor, NULL, scratch_pool);

  /* Use the diff editor to generate the diff. */
  SVN_ERR(svn_ra_has_capability(ra_session, &server_supports_depth,
                                SVN_RA_CAPABILITY_DEPTH, scratch_pool));
  SVN_ERR(svn_wc__get_diff_editor(&diff_editor, &diff_edit_baton,
                                  ctx->wc_ctx,
                                  anchor_abspath,
                                  target,
                                  depth,
                                  ignore_ancestry,
                                  rev2_is_base,
                                  reverse,
                                  server_supports_depth,
                                  changelists,
                                  diff_processor,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  scratch_pool, scratch_pool));

  if (depth != svn_depth_infinity)
    diff_depth = depth;
  else
    diff_depth = svn_depth_unknown;

  /* Tell the RA layer we want a delta to change our txn to URL1 */
  SVN_ERR(svn_ra_do_diff3(ra_session,
                          &reporter, &reporter_baton,
                          loc1->rev,
                          target,
                          diff_depth,
                          ignore_ancestry,
                          TRUE, /* text_deltas */
                          loc1->url,
                          diff_editor, diff_edit_baton,
                          scratch_pool));

  if (is_copy && revision2_kind != svn_opt_revision_base)
    {
      /* Report the copy source. */
      if (cf_depth == svn_depth_unknown)
        cf_depth = svn_depth_infinity;

      /* Reporting the in-wc revision as r0, makes the repository send
         everything as added, which avoids using BASE for pristine information,
         which is not there (or unrelated) for a copy */

      SVN_ERR(reporter->set_path(reporter_baton, "",
                                 ignore_ancestry ? 0 : cf_revision,
                                 cf_depth, FALSE, NULL, scratch_pool));

      if (*target)
        SVN_ERR(reporter->link_path(reporter_baton, target,
                                    target_url,
                                    ignore_ancestry ? 0 : cf_revision,
                                    cf_depth, FALSE, NULL, scratch_pool));

      /* Finish the report to generate the diff. */
      SVN_ERR(reporter->finish_report(reporter_baton, scratch_pool));
    }
  else
    {
      /* Create a txn mirror of path2;  the diff editor will print
         diffs in reverse.  :-)  */
      SVN_ERR(svn_wc_crawl_revisions5(ctx->wc_ctx, abspath2,
                                      reporter, reporter_baton,
                                      FALSE, depth, TRUE,
                                      (! server_supports_depth),
                                      FALSE,
                                      ctx->cancel_func, ctx->cancel_baton,
                                      NULL, NULL, /* notification is N/A */
                                      scratch_pool));
    }

  return SVN_NO_ERROR;
}


/* This is basically just the guts of svn_client_diff[_summarize][_peg]6(). */
static svn_error_t *
do_diff(const char **root_relpath,
        svn_boolean_t *root_is_dir,
        diff_driver_info_t *ddi,
        const char *path_or_url1,
        const char *path_or_url2,
        const svn_opt_revision_t *revision1,
        const svn_opt_revision_t *revision2,
        const svn_opt_revision_t *peg_revision,
        svn_boolean_t no_peg_revision,
        svn_depth_t depth,
        svn_boolean_t ignore_ancestry,
        const apr_array_header_t *changelists,
        svn_boolean_t text_deltas,
        const svn_diff_tree_processor_t *diff_processor,
        svn_client_ctx_t *ctx,
        apr_pool_t *result_pool,
        apr_pool_t *scratch_pool)
{
  svn_boolean_t is_repos1;
  svn_boolean_t is_repos2;

  /* Check if paths/revisions are urls/local. */
  SVN_ERR(check_paths(&is_repos1, &is_repos2, path_or_url1, path_or_url2,
                      revision1, revision2, peg_revision));

  if (is_repos1)
    {
      if (is_repos2)
        {
          /* Ignores changelists. */
          SVN_ERR(diff_repos_repos(root_relpath, root_is_dir,
                                   ddi,
                                   path_or_url1, path_or_url2,
                                   revision1, revision2,
                                   peg_revision, depth, ignore_ancestry,
                                   text_deltas,
                                   diff_processor, ctx,
                                   result_pool, scratch_pool));
        }
      else /* path_or_url2 is a working copy path */
        {
          SVN_ERR(diff_repos_wc(root_relpath, root_is_dir, ddi,
                                path_or_url1, revision1,
                                no_peg_revision ? revision1
                                                : peg_revision,
                                path_or_url2, revision2->kind,
                                FALSE, depth,
                                ignore_ancestry, changelists,
                                diff_processor, ctx,
                                result_pool, scratch_pool));
        }
    }
  else /* path_or_url1 is a working copy path */
    {
      if (is_repos2)
        {
          SVN_ERR(diff_repos_wc(root_relpath, root_is_dir, ddi,
                                path_or_url2, revision2,
                                no_peg_revision ? revision2
                                                : peg_revision,
                                path_or_url1,
                                revision1->kind,
                                TRUE, depth,
                                ignore_ancestry, changelists,
                                diff_processor, ctx,
                                result_pool, scratch_pool));
        }
      else /* path_or_url2 is a working copy path */
        {
          if (revision1->kind == svn_opt_revision_working
              && revision2->kind == svn_opt_revision_working)
            {
              const char *abspath1;
              const char *abspath2;

              SVN_ERR(svn_dirent_get_absolute(&abspath1, path_or_url1,
                                              scratch_pool));
              SVN_ERR(svn_dirent_get_absolute(&abspath2, path_or_url2,
                                              scratch_pool));

              /* ### What about ddi? */
              /* Ignores changelists, ignore_ancestry */
              SVN_ERR(svn_client__arbitrary_nodes_diff(root_relpath, root_is_dir,
                                                       abspath1, abspath2,
                                                       depth,
                                                       diff_processor,
                                                       ctx,
                                                       result_pool, scratch_pool));
            }
          else
            {
              SVN_ERR(diff_wc_wc(root_relpath, root_is_dir, ddi,
                                 path_or_url1, revision1,
                                 path_or_url2, revision2,
                                 depth, ignore_ancestry, changelists,
                                 diff_processor, ctx,
                                 result_pool, scratch_pool));
            }
        }
    }

  return SVN_NO_ERROR;
}

/* Initialize DWI.diff_cmd and DWI.options,
 * according to OPTIONS and CONFIG.  CONFIG and OPTIONS may be null.
 * Allocate the fields in RESULT_POOL, which should be at least as long-lived
 * as the pool DWI itself is allocated in.
 */
static svn_error_t *
create_diff_writer_info(diff_writer_info_t *dwi,
                        const apr_array_header_t *options,
                        apr_hash_t *config, apr_pool_t *result_pool)
{
  const char *diff_cmd = NULL;

  /* See if there is a diff command and/or diff arguments. */
  if (config)
    {
      svn_config_t *cfg = svn_hash_gets(config, SVN_CONFIG_CATEGORY_CONFIG);
      svn_config_get(cfg, &diff_cmd, SVN_CONFIG_SECTION_HELPERS,
                     SVN_CONFIG_OPTION_DIFF_CMD, NULL);
      if (options == NULL)
        {
          const char *diff_extensions;
          svn_config_get(cfg, &diff_extensions, SVN_CONFIG_SECTION_HELPERS,
                         SVN_CONFIG_OPTION_DIFF_EXTENSIONS, NULL);
          if (diff_extensions)
            options = svn_cstring_split(diff_extensions, " \t\n\r", TRUE,
                                        result_pool);
        }
    }

  if (options == NULL)
    options = apr_array_make(result_pool, 0, sizeof(const char *));

  if (diff_cmd)
    SVN_ERR(svn_path_cstring_to_utf8(&dwi->diff_cmd, diff_cmd,
                                     result_pool));
  else
    dwi->diff_cmd = NULL;

  /* If there was a command, arrange options to pass to it. */
  if (dwi->diff_cmd)
    {
      const char **argv = NULL;
      int argc = options->nelts;
      if (argc)
        {
          int i;
          argv = apr_palloc(result_pool, argc * sizeof(char *));
          for (i = 0; i < argc; i++)
            SVN_ERR(svn_utf_cstring_to_utf8(&argv[i],
                      APR_ARRAY_IDX(options, i, const char *), result_pool));
        }
      dwi->options.for_external.argv = argv;
      dwi->options.for_external.argc = argc;
    }
  else  /* No command, so arrange options for internal invocation instead. */
    {
      dwi->options.for_internal = svn_diff_file_options_create(result_pool);
      SVN_ERR(svn_diff_file_options_parse(dwi->options.for_internal,
                                          options, result_pool));
    }

  return SVN_NO_ERROR;
}

/*----------------------------------------------------------------------- */

/*** Public Interfaces. ***/

/* Display context diffs between two PATH/REVISION pairs.  Each of
   these inputs will be one of the following:

   - a repository URL at a given revision.
   - a working copy path, ignoring local mods.
   - a working copy path, including local mods.

   We can establish a matrix that shows the nine possible types of
   diffs we expect to support.


      ` .     DST ||  URL:rev   | WC:base    | WC:working |
          ` .     ||            |            |            |
      SRC     ` . ||            |            |            |
      ============++============+============+============+
       URL:rev    || (*)        | (*)        | (*)        |
                  ||            |            |            |
                  ||            |            |            |
                  ||            |            |            |
      ------------++------------+------------+------------+
       WC:base    || (*)        |                         |
                  ||            | New svn_wc_diff which   |
                  ||            | is smart enough to      |
                  ||            | handle two WC paths     |
      ------------++------------+ and their related       +
       WC:working || (*)        | text-bases and working  |
                  ||            | files.  This operation  |
                  ||            | is entirely local.      |
                  ||            |                         |
      ------------++------------+------------+------------+
      * These cases require server communication.
*/
svn_error_t *
svn_client_diff6(const apr_array_header_t *options,
                 const char *path_or_url1,
                 const svn_opt_revision_t *revision1,
                 const char *path_or_url2,
                 const svn_opt_revision_t *revision2,
                 const char *relative_to_dir,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_added,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t show_copies_as_adds,
                 svn_boolean_t ignore_content_type,
                 svn_boolean_t ignore_properties,
                 svn_boolean_t properties_only,
                 svn_boolean_t use_git_diff_format,
                 const char *header_encoding,
                 svn_stream_t *outstream,
                 svn_stream_t *errstream,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  diff_writer_info_t dwi = { 0 };
  svn_opt_revision_t peg_revision;
  const svn_diff_tree_processor_t *diff_processor;
  svn_diff_tree_processor_t *processor;

  if (ignore_properties && properties_only)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Cannot ignore properties and show only "
                              "properties at the same time"));

  /* We will never do a pegged diff from here. */
  peg_revision.kind = svn_opt_revision_unspecified;

  /* setup callback and baton */
  dwi.ddi.orig_path_1 = path_or_url1;
  dwi.ddi.orig_path_2 = path_or_url2;

  SVN_ERR(create_diff_writer_info(&dwi, options,
                                  ctx->config, pool));
  dwi.pool = pool;
  dwi.outstream = outstream;
  dwi.errstream = errstream;
  dwi.header_encoding = header_encoding;

  dwi.force_binary = ignore_content_type;
  dwi.ignore_properties = ignore_properties;
  dwi.properties_only = properties_only;
  dwi.relative_to_dir = relative_to_dir;
  dwi.use_git_diff_format = use_git_diff_format;
  dwi.no_diff_added = no_diff_added;
  dwi.no_diff_deleted = no_diff_deleted;
  dwi.show_copies_as_adds = show_copies_as_adds;

  dwi.cancel_func = ctx->cancel_func;
  dwi.cancel_baton = ctx->cancel_baton;

  dwi.wc_ctx = ctx->wc_ctx;
  dwi.ddi.session_relpath = NULL;
  dwi.ddi.anchor = NULL;

  processor = svn_diff__tree_processor_create(&dwi, pool);

  processor->dir_added = diff_dir_added;
  processor->dir_changed = diff_dir_changed;
  processor->dir_deleted = diff_dir_deleted;

  processor->file_added = diff_file_added;
  processor->file_changed = diff_file_changed;
  processor->file_deleted = diff_file_deleted;

  diff_processor = processor;

  /* --show-copies-as-adds and --git imply --notice-ancestry */
  if (show_copies_as_adds || use_git_diff_format)
    ignore_ancestry = FALSE;

  return svn_error_trace(do_diff(NULL, NULL, &dwi.ddi,
                                 path_or_url1, path_or_url2,
                                 revision1, revision2,
                                 &peg_revision, TRUE /* no_peg_revision */,
                                 depth, ignore_ancestry, changelists,
                                 TRUE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));
}

svn_error_t *
svn_client_diff_peg6(const apr_array_header_t *options,
                     const char *path_or_url,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     const char *relative_to_dir,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_added,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t show_copies_as_adds,
                     svn_boolean_t ignore_content_type,
                     svn_boolean_t ignore_properties,
                     svn_boolean_t properties_only,
                     svn_boolean_t use_git_diff_format,
                     const char *header_encoding,
                     svn_stream_t *outstream,
                     svn_stream_t *errstream,
                     const apr_array_header_t *changelists,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  diff_writer_info_t dwi = { 0 };
  const svn_diff_tree_processor_t *diff_processor;
  svn_diff_tree_processor_t *processor;

  if (ignore_properties && properties_only)
    return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                            _("Cannot ignore properties and show only "
                              "properties at the same time"));

  /* setup callback and baton */
  dwi.ddi.orig_path_1 = path_or_url;
  dwi.ddi.orig_path_2 = path_or_url;

  SVN_ERR(create_diff_writer_info(&dwi, options,
                                  ctx->config, pool));
  dwi.pool = pool;
  dwi.outstream = outstream;
  dwi.errstream = errstream;
  dwi.header_encoding = header_encoding;

  dwi.force_binary = ignore_content_type;
  dwi.ignore_properties = ignore_properties;
  dwi.properties_only = properties_only;
  dwi.relative_to_dir = relative_to_dir;
  dwi.use_git_diff_format = use_git_diff_format;
  dwi.no_diff_added = no_diff_added;
  dwi.no_diff_deleted = no_diff_deleted;
  dwi.show_copies_as_adds = show_copies_as_adds;

  dwi.cancel_func = ctx->cancel_func;
  dwi.cancel_baton = ctx->cancel_baton;

  dwi.wc_ctx = ctx->wc_ctx;
  dwi.ddi.session_relpath = NULL;
  dwi.ddi.anchor = NULL;

  processor = svn_diff__tree_processor_create(&dwi, pool);

  processor->dir_added = diff_dir_added;
  processor->dir_changed = diff_dir_changed;
  processor->dir_deleted = diff_dir_deleted;

  processor->file_added = diff_file_added;
  processor->file_changed = diff_file_changed;
  processor->file_deleted = diff_file_deleted;

  diff_processor = processor;

  /* --show-copies-as-adds and --git imply --notice-ancestry */
  if (show_copies_as_adds || use_git_diff_format)
    ignore_ancestry = FALSE;

  return svn_error_trace(do_diff(NULL, NULL, &dwi.ddi,
                                 path_or_url, path_or_url,
                                 start_revision, end_revision,
                                 peg_revision, FALSE /* no_peg_revision */,
                                 depth, ignore_ancestry, changelists,
                                 TRUE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));
}

svn_error_t *
svn_client_diff_summarize2(const char *path_or_url1,
                           const svn_opt_revision_t *revision1,
                           const char *path_or_url2,
                           const svn_opt_revision_t *revision2,
                           svn_depth_t depth,
                           svn_boolean_t ignore_ancestry,
                           const apr_array_header_t *changelists,
                           svn_client_diff_summarize_func_t summarize_func,
                           void *summarize_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  const svn_diff_tree_processor_t *diff_processor;
  svn_opt_revision_t peg_revision;
  const char **p_root_relpath;

  /* We will never do a pegged diff from here. */
  peg_revision.kind = svn_opt_revision_unspecified;

  SVN_ERR(svn_client__get_diff_summarize_callbacks(
                     &diff_processor, &p_root_relpath,
                     summarize_func, summarize_baton,
                     path_or_url1, pool, pool));

  return svn_error_trace(do_diff(p_root_relpath, NULL, NULL,
                                 path_or_url1, path_or_url2,
                                 revision1, revision2,
                                 &peg_revision, TRUE /* no_peg_revision */,
                                 depth, ignore_ancestry, changelists,
                                 FALSE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));
}

svn_error_t *
svn_client_diff_summarize_peg2(const char *path_or_url,
                               const svn_opt_revision_t *peg_revision,
                               const svn_opt_revision_t *start_revision,
                               const svn_opt_revision_t *end_revision,
                               svn_depth_t depth,
                               svn_boolean_t ignore_ancestry,
                               const apr_array_header_t *changelists,
                               svn_client_diff_summarize_func_t summarize_func,
                               void *summarize_baton,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool)
{
  const svn_diff_tree_processor_t *diff_processor;
  const char **p_root_relpath;

  SVN_ERR(svn_client__get_diff_summarize_callbacks(
                     &diff_processor, &p_root_relpath,
                     summarize_func, summarize_baton,
                     path_or_url, pool, pool));

  return svn_error_trace(do_diff(p_root_relpath, NULL, NULL,
                                 path_or_url, path_or_url,
                                 start_revision, end_revision,
                                 peg_revision, FALSE /* no_peg_revision */,
                                 depth, ignore_ancestry, changelists,
                                 FALSE /* text_deltas */,
                                 diff_processor, ctx, pool, pool));
}

