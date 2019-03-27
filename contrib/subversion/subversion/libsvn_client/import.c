/*
 * import.c:  wrappers around import functionality.
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
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_md5.h>

#include "svn_hash.h"
#include "svn_ra.h"
#include "svn_delta.h"
#include "svn_subst.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_pools.h"
#include "svn_error_codes.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_sorts.h"
#include "svn_props.h"

#include "client.h"
#include "private/svn_ra_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_magic.h"

#include "svn_private_config.h"

/* Import context baton. */

typedef struct import_ctx_t
{
  /* Whether any changes were made to the repository */
  svn_boolean_t repos_changed;

  /* A magic cookie for mime-type detection. */
  svn_magic__cookie_t *magic_cookie;

  /* Collection of all possible configuration file dictated auto-props and
     svn:auto-props.  A hash mapping const char * file patterns to a
     second hash which maps const char * property names to const char *
     property values.  Properties which don't have a value, e.g.
     svn:executable, simply map the property name to an empty string.
     May be NULL if autoprops are disabled. */
  apr_hash_t *autoprops;
} import_ctx_t;

typedef struct open_txdelta_stream_baton_t
{
  svn_boolean_t need_reset;
  svn_stream_t *stream;
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
       * supply the delta stream again.  In this case, reset the base
       * stream. */
      SVN_ERR(svn_stream_reset(b->stream));
    }

  /* Get the delta stream (delta against the empty string). */
  svn_txdelta2(txdelta_stream_p, svn_stream_empty(result_pool),
               b->stream, FALSE, result_pool);
  b->need_reset = TRUE;
  return SVN_NO_ERROR;
}

/* Apply LOCAL_ABSPATH's contents (as a delta against the empty string) to
   FILE_BATON in EDITOR.  Use POOL for any temporary allocation.
   PROPERTIES is the set of node properties set on this file.

   Return the resulting checksum in *RESULT_MD5_CHECKSUM_P. */

/* ### how does this compare against svn_wc_transmit_text_deltas2() ??? */

static svn_error_t *
send_file_contents(svn_checksum_t **result_md5_checksum_p,
                   const char *local_abspath,
                   void *file_baton,
                   const svn_delta_editor_t *editor,
                   apr_hash_t *properties,
                   apr_pool_t *pool)
{
  svn_stream_t *contents;
  const svn_string_t *eol_style_val = NULL, *keywords_val = NULL;
  svn_boolean_t special = FALSE;
  svn_subst_eol_style_t eol_style;
  const char *eol;
  apr_hash_t *keywords;
  open_txdelta_stream_baton_t baton = { 0 };

  /* If there are properties, look for EOL-style and keywords ones. */
  if (properties)
    {
      eol_style_val = apr_hash_get(properties, SVN_PROP_EOL_STYLE,
                                   sizeof(SVN_PROP_EOL_STYLE) - 1);
      keywords_val = apr_hash_get(properties, SVN_PROP_KEYWORDS,
                                  sizeof(SVN_PROP_KEYWORDS) - 1);
      if (svn_hash_gets(properties, SVN_PROP_SPECIAL))
        special = TRUE;
    }

  if (eol_style_val)
    svn_subst_eol_style_from_value(&eol_style, &eol, eol_style_val->data);
  else
    {
      eol = NULL;
      eol_style = svn_subst_eol_style_none;
    }

  if (keywords_val)
    SVN_ERR(svn_subst_build_keywords3(&keywords, keywords_val->data,
                                      APR_STRINGIFY(SVN_INVALID_REVNUM),
                                      "", "", 0, "", pool));
  else
    keywords = NULL;

  if (special)
    {
      SVN_ERR(svn_subst_read_specialfile(&contents, local_abspath,
                                         pool, pool));
    }
  else
    {
      /* Open the working copy file. */
      SVN_ERR(svn_stream_open_readonly(&contents, local_abspath, pool, pool));

      /* If we have EOL styles or keywords, then detranslate the file. */
      if (svn_subst_translation_required(eol_style, eol, keywords,
                                         FALSE, TRUE))
        {
          if (eol_style == svn_subst_eol_style_unknown)
            return svn_error_createf(SVN_ERR_IO_UNKNOWN_EOL, NULL,
                                    _("%s property on '%s' contains "
                                      "unrecognized EOL-style '%s'"),
                                    SVN_PROP_EOL_STYLE,
                                    svn_dirent_local_style(local_abspath,
                                                           pool),
                                    eol_style_val->data);

          /* We're importing, so translate files with 'native' eol-style to
           * repository-normal form, not to this platform's native EOL. */
          if (eol_style == svn_subst_eol_style_native)
            eol = SVN_SUBST_NATIVE_EOL_STR;

          /* Wrap the working copy stream with a filter to detranslate it. */
          contents = svn_subst_stream_translated(contents,
                                                 eol,
                                                 TRUE /* repair */,
                                                 keywords,
                                                 FALSE /* expand */,
                                                 pool);
        }
    }

  /* Arrange the stream to calculate the resulting MD5. */
  contents = svn_stream_checksummed2(contents, result_md5_checksum_p, NULL,
                                     svn_checksum_md5, TRUE, pool);
  /* Send the contents. */
  baton.need_reset = FALSE;
  baton.stream = svn_stream_disown(contents, pool);
  SVN_ERR(editor->apply_textdelta_stream(editor, file_baton, NULL,
                                         open_txdelta_stream, &baton, pool));
  SVN_ERR(svn_stream_close(contents));

  return SVN_NO_ERROR;
}


/* Import file PATH as EDIT_PATH in the repository directory indicated
 * by DIR_BATON in EDITOR.
 *
 * Accumulate file paths and their batons in FILES, which must be
 * non-null.  (These are used to send postfix textdeltas later).
 *
 * If CTX->NOTIFY_FUNC is non-null, invoke it with CTX->NOTIFY_BATON
 * for each file.
 *
 * Use POOL for any temporary allocation.
 */
static svn_error_t *
import_file(const svn_delta_editor_t *editor,
            void *dir_baton,
            const char *local_abspath,
            const char *edit_path,
            const svn_io_dirent2_t *dirent,
            import_ctx_t *import_ctx,
            svn_client_ctx_t *ctx,
            apr_pool_t *pool)
{
  void *file_baton;
  const char *mimetype = NULL;
  svn_checksum_t *result_md5_checksum;
  const char *text_checksum;
  apr_hash_t* properties;
  apr_hash_index_t *hi;

  SVN_ERR(svn_path_check_valid(local_abspath, pool));

  /* Add the file, using the pool from the FILES hash. */
  SVN_ERR(editor->add_file(edit_path, dir_baton, NULL, SVN_INVALID_REVNUM,
                           pool, &file_baton));

  /* Remember that the repository was modified */
  import_ctx->repos_changed = TRUE;

  if (! dirent->special)
    {
      /* add automatic properties */
      SVN_ERR(svn_client__get_paths_auto_props(&properties, &mimetype,
                                               local_abspath,
                                               import_ctx->magic_cookie,
                                               import_ctx->autoprops,
                                               ctx, pool, pool));
    }
  else
    properties = apr_hash_make(pool);

  if (properties)
    {
      for (hi = apr_hash_first(pool, properties); hi; hi = apr_hash_next(hi))
        {
          const char *pname = apr_hash_this_key(hi);
          const svn_string_t *pval = apr_hash_this_val(hi);

          SVN_ERR(editor->change_file_prop(file_baton, pname, pval, pool));
        }
    }

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify
        = svn_wc_create_notify(local_abspath, svn_wc_notify_commit_added,
                               pool);
      notify->kind = svn_node_file;
      notify->mime_type = mimetype;
      notify->content_state = notify->prop_state
        = svn_wc_notify_state_inapplicable;
      notify->lock_state = svn_wc_notify_lock_state_inapplicable;
      ctx->notify_func2(ctx->notify_baton2, notify, pool);
    }

  /* If this is a special file, we need to set the svn:special
     property and create a temporary detranslated version in order to
     send to the server. */
  if (dirent->special)
    {
      svn_hash_sets(properties, SVN_PROP_SPECIAL,
                    svn_string_create(SVN_PROP_BOOLEAN_TRUE, pool));
      SVN_ERR(editor->change_file_prop(file_baton, SVN_PROP_SPECIAL,
                                       svn_hash_gets(properties,
                                                     SVN_PROP_SPECIAL),
                                       pool));
    }

  /* Now, transmit the file contents. */
  SVN_ERR(send_file_contents(&result_md5_checksum, local_abspath,
                             file_baton, editor, properties, pool));

  /* Finally, close the file. */
  text_checksum = svn_checksum_to_cstring(result_md5_checksum, pool);
  return svn_error_trace(editor->close_file(file_baton, text_checksum, pool));
}


/* Return in CHILDREN a mapping of basenames to dirents for the importable
 * children of DIR_ABSPATH.  EXCLUDES is a hash of absolute paths to filter
 * out.  IGNORES and GLOBAL_IGNORES, if non-NULL, are lists of basename
 * patterns to filter out.
 * FILTER_CALLBACK and FILTER_BATON will be called for each absolute path,
 * allowing users to further filter the list of returned entries.
 *
 * Results are returned in RESULT_POOL; use SCRATCH_POOL for temporary data.*/
static svn_error_t *
get_filtered_children(apr_hash_t **children,
                      const char *dir_abspath,
                      apr_hash_t *excludes,
                      apr_array_header_t *ignores,
                      apr_array_header_t *global_ignores,
                      svn_client_import_filter_func_t filter_callback,
                      void *filter_baton,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  apr_hash_t *dirents;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR(svn_io_get_dirents3(&dirents, dir_abspath, TRUE, result_pool,
                              scratch_pool));

  for (hi = apr_hash_first(scratch_pool, dirents); hi; hi = apr_hash_next(hi))
    {
      const char *base_name = apr_hash_this_key(hi);
      const svn_io_dirent2_t *dirent = apr_hash_this_val(hi);
      const char *local_abspath;

      svn_pool_clear(iterpool);

      local_abspath = svn_dirent_join(dir_abspath, base_name, iterpool);

      if (svn_wc_is_adm_dir(base_name, iterpool))
        {
          /* If someone's trying to import a directory named the same
             as our administrative directories, that's probably not
             what they wanted to do.  If they are importing a file
             with that name, something is bound to blow up when they
             checkout what they've imported.  So, just skip items with
             that name.  */
          if (ctx->notify_func2)
            {
              svn_wc_notify_t *notify
                = svn_wc_create_notify(svn_dirent_join(local_abspath, base_name,
                                                       iterpool),
                                       svn_wc_notify_skip, iterpool);
              notify->kind = svn_node_dir;
              notify->content_state = notify->prop_state
                = svn_wc_notify_state_inapplicable;
              notify->lock_state = svn_wc_notify_lock_state_inapplicable;
              ctx->notify_func2(ctx->notify_baton2, notify, iterpool);
            }

          svn_hash_sets(dirents, base_name, NULL);
          continue;
        }
            /* If this is an excluded path, exclude it. */
      if (svn_hash_gets(excludes, local_abspath))
        {
          svn_hash_sets(dirents, base_name, NULL);
          continue;
        }

      if (ignores && svn_wc_match_ignore_list(base_name, ignores, iterpool))
        {
          svn_hash_sets(dirents, base_name, NULL);
          continue;
        }

      if (global_ignores &&
          svn_wc_match_ignore_list(base_name, global_ignores, iterpool))
        {
          svn_hash_sets(dirents, base_name, NULL);
          continue;
        }

      if (filter_callback)
        {
          svn_boolean_t filter = FALSE;

          SVN_ERR(filter_callback(filter_baton, &filter, local_abspath,
                                  dirent, iterpool));

          if (filter)
            {
              svn_hash_sets(dirents, base_name, NULL);
              continue;
            }
        }
    }
  svn_pool_destroy(iterpool);

  *children = dirents;
  return SVN_NO_ERROR;
}

static svn_error_t *
import_dir(const svn_delta_editor_t *editor,
           void *dir_baton,
           const char *local_abspath,
           const char *edit_path,
           svn_depth_t depth,
           apr_hash_t *excludes,
           apr_array_header_t *global_ignores,
           svn_boolean_t no_ignore,
           svn_boolean_t no_autoprops,
           svn_boolean_t ignore_unknown_node_types,
           svn_client_import_filter_func_t filter_callback,
           void *filter_baton,
           import_ctx_t *import_ctx,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool);


/* Import the children of DIR_ABSPATH, with other arguments similar to
 * import_dir(). */
static svn_error_t *
import_children(const char *dir_abspath,
                const char *edit_path,
                apr_hash_t *dirents,
                const svn_delta_editor_t *editor,
                void *dir_baton,
                svn_depth_t depth,
                apr_hash_t *excludes,
                apr_array_header_t *global_ignores,
                svn_boolean_t no_ignore,
                svn_boolean_t no_autoprops,
                svn_boolean_t ignore_unknown_node_types,
                svn_client_import_filter_func_t filter_callback,
                void *filter_baton,
                import_ctx_t *import_ctx,
                svn_client_ctx_t *ctx,
                apr_pool_t *scratch_pool)
{
  apr_array_header_t *sorted_dirents;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  sorted_dirents = svn_sort__hash(dirents, svn_sort_compare_items_lexically,
                                  scratch_pool);
  for (i = 0; i < sorted_dirents->nelts; i++)
    {
      const char *this_abspath, *this_edit_path;
      svn_sort__item_t item = APR_ARRAY_IDX(sorted_dirents, i,
                                            svn_sort__item_t);
      const char *filename = item.key;
      const svn_io_dirent2_t *dirent = item.value;

      svn_pool_clear(iterpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      /* Typically, we started importing from ".", in which case
         edit_path is "".  So below, this_path might become "./blah",
         and this_edit_path might become "blah", for example. */
      this_abspath = svn_dirent_join(dir_abspath, filename, iterpool);
      this_edit_path = svn_relpath_join(edit_path, filename, iterpool);

      if (dirent->kind == svn_node_dir && depth >= svn_depth_immediates)
        {
          /* Recurse. */
          svn_depth_t depth_below_here = depth;
          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          SVN_ERR(import_dir(editor, dir_baton, this_abspath,
                             this_edit_path, depth_below_here, excludes,
                             global_ignores, no_ignore, no_autoprops,
                             ignore_unknown_node_types, filter_callback,
                             filter_baton, import_ctx, ctx, iterpool));
        }
      else if (dirent->kind == svn_node_file && depth >= svn_depth_files)
        {
          SVN_ERR(import_file(editor, dir_baton, this_abspath,
                              this_edit_path, dirent,
                              import_ctx, ctx, iterpool));
        }
      else if (dirent->kind != svn_node_dir && dirent->kind != svn_node_file)
        {
          if (ignore_unknown_node_types)
            {
              /*## warn about it*/
              if (ctx->notify_func2)
                {
                  svn_wc_notify_t *notify
                    = svn_wc_create_notify(this_abspath,
                                           svn_wc_notify_skip, iterpool);
                  notify->kind = svn_node_dir;
                  notify->content_state = notify->prop_state
                    = svn_wc_notify_state_inapplicable;
                  notify->lock_state = svn_wc_notify_lock_state_inapplicable;
                  ctx->notify_func2(ctx->notify_baton2, notify, iterpool);
                }
            }
          else
            return svn_error_createf
              (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
               _("Unknown or unversionable type for '%s'"),
               svn_dirent_local_style(this_abspath, iterpool));
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/* Import directory LOCAL_ABSPATH into the repository directory indicated by
 * DIR_BATON in EDITOR.  EDIT_PATH is the path imported as the root
 * directory, so all edits are relative to that.
 *
 * DEPTH is the depth at this point in the descent (it may be changed
 * for recursive calls).
 *
 * Accumulate file paths and their batons in FILES, which must be
 * non-null.  (These are used to send postfix textdeltas later).
 *
 * EXCLUDES is a hash whose keys are absolute paths to exclude from
 * the import (values are unused).
 *
 * GLOBAL_IGNORES is an array of const char * ignore patterns.  Any child
 * of LOCAL_ABSPATH which matches one or more of the patterns is not imported.
 *
 * If NO_IGNORE is FALSE, don't import files or directories that match
 * ignore patterns.
 *
 * If FILTER_CALLBACK is not NULL, call it with FILTER_BATON on each to be
 * imported node below LOCAL_ABSPATH to allow filtering nodes.
 *
 * If CTX->NOTIFY_FUNC is non-null, invoke it with CTX->NOTIFY_BATON for each
 * directory.
 *
 * Use POOL for any temporary allocation.  */
static svn_error_t *
import_dir(const svn_delta_editor_t *editor,
           void *dir_baton,
           const char *local_abspath,
           const char *edit_path,
           svn_depth_t depth,
           apr_hash_t *excludes,
           apr_array_header_t *global_ignores,
           svn_boolean_t no_ignore,
           svn_boolean_t no_autoprops,
           svn_boolean_t ignore_unknown_node_types,
           svn_client_import_filter_func_t filter_callback,
           void *filter_baton,
           import_ctx_t *import_ctx,
           svn_client_ctx_t *ctx,
           apr_pool_t *pool)
{
  apr_hash_t *dirents;
  void *this_dir_baton;

  SVN_ERR(svn_path_check_valid(local_abspath, pool));
  SVN_ERR(get_filtered_children(&dirents, local_abspath, excludes, NULL,
                                global_ignores, filter_callback,
                                filter_baton, ctx, pool, pool));

  /* Import this directory, but not yet its children. */
  {
    /* Add the new subdirectory, getting a descent baton from the editor. */
    SVN_ERR(editor->add_directory(edit_path, dir_baton, NULL,
                                  SVN_INVALID_REVNUM, pool, &this_dir_baton));

    /* Remember that the repository was modified */
    import_ctx->repos_changed = TRUE;

    /* By notifying before the recursive call below, we display
       a directory add before displaying adds underneath the
       directory.  To do it the other way around, just move this
       after the recursive call. */
    if (ctx->notify_func2)
      {
        svn_wc_notify_t *notify
          = svn_wc_create_notify(local_abspath, svn_wc_notify_commit_added,
                                 pool);
        notify->kind = svn_node_dir;
        notify->content_state = notify->prop_state
          = svn_wc_notify_state_inapplicable;
        notify->lock_state = svn_wc_notify_lock_state_inapplicable;
        ctx->notify_func2(ctx->notify_baton2, notify, pool);
      }
  }

  /* Now import the children recursively. */
  SVN_ERR(import_children(local_abspath, edit_path, dirents, editor,
                          this_dir_baton, depth, excludes, global_ignores,
                          no_ignore, no_autoprops, ignore_unknown_node_types,
                          filter_callback, filter_baton,
                          import_ctx, ctx, pool));

  /* Finally, close the sub-directory. */
  SVN_ERR(editor->close_directory(this_dir_baton, pool));

  return SVN_NO_ERROR;
}


/* Recursively import LOCAL_ABSPATH to a repository using EDITOR and
 * EDIT_BATON.  LOCAL_ABSPATH can be a file or directory.
 *
 * Sets *UPDATED_REPOSITORY to TRUE when the repository was modified by
 * a successfull commit, otherwise to FALSE.
 *
 * DEPTH is the depth at which to import LOCAL_ABSPATH; it behaves as for
 * svn_client_import5().
 *
 * BASE_REV is the revision to use for the root of the commit. We
 * checked the preconditions against this revision.
 *
 * NEW_ENTRIES is an ordered array of path components that must be
 * created in the repository (where the ordering direction is
 * parent-to-child).  If LOCAL_ABSPATH is a directory, NEW_ENTRIES may be empty
 * -- the result is an import which creates as many new entries in the
 * top repository target directory as there are importable entries in
 * the top of LOCAL_ABSPATH; but if NEW_ENTRIES is not empty, its last item is
 * the name of a new subdirectory in the repository to hold the
 * import.  If LOCAL_ABSPATH is a file, NEW_ENTRIES may not be empty, and its
 * last item is the name used for the file in the repository.  If
 * NEW_ENTRIES contains more than one item, all but the last item are
 * the names of intermediate directories that are created before the
 * real import begins.  NEW_ENTRIES may NOT be NULL.
 *
 * EXCLUDES is a hash whose keys are absolute paths to exclude from
 * the import (values are unused).
 *
 * AUTOPROPS is hash of all config file autoprops and
 * svn:auto-props inherited by the import target, see the
 * IMPORT_CTX member of the same name.
 *
 * LOCAL_IGNORES is an array of const char * ignore patterns which
 * correspond to the svn:ignore property (if any) set on the root of the
 * repository target and thus dictates which immediate children of that
 * target should be ignored and not imported.
 *
 * GLOBAL_IGNORES is an array of const char * ignore patterns which
 * correspond to the svn:global-ignores properties (if any) set on
 * the root of the repository target or inherited by it.
 *
 * If NO_IGNORE is FALSE, don't import files or directories that match
 * ignore patterns.
 *
 * If CTX->NOTIFY_FUNC is non-null, invoke it with CTX->NOTIFY_BATON for
 * each imported path, passing actions svn_wc_notify_commit_added.
 *
 * URL is used only in the 'commit_finalizing' notification.
 *
 * Use POOL for any temporary allocation.
 *
 * Note: the repository directory receiving the import was specified
 * when the editor was fetched.  (I.e, when EDITOR->open_root() is
 * called, it returns a directory baton for that directory, which is
 * not necessarily the root.)
 */
static svn_error_t *
import(svn_boolean_t *updated_repository,
       const char *local_abspath,
       const char *url,
       const apr_array_header_t *new_entries,
       const svn_delta_editor_t *editor,
       void *edit_baton,
       svn_depth_t depth,
       svn_revnum_t base_rev,
       apr_hash_t *excludes,
       apr_hash_t *autoprops,
       apr_array_header_t *local_ignores,
       apr_array_header_t *global_ignores,
       svn_boolean_t no_ignore,
       svn_boolean_t no_autoprops,
       svn_boolean_t ignore_unknown_node_types,
       svn_client_import_filter_func_t filter_callback,
       void *filter_baton,
       svn_client_ctx_t *ctx,
       apr_pool_t *pool)
{
  void *root_baton;
  apr_array_header_t *batons = NULL;
  const char *edit_path = "";
  import_ctx_t import_ctx = { FALSE };
  const svn_io_dirent2_t *dirent;

  *updated_repository = FALSE;

  import_ctx.autoprops = autoprops;
  SVN_ERR(svn_magic__init(&import_ctx.magic_cookie, ctx->config, pool));

  /* Get a root dir baton.  We pass the revnum we used for testing our
     assumptions and obtaining inherited properties. */
  SVN_ERR(editor->open_root(edit_baton, base_rev, pool, &root_baton));

  /* Import a file or a directory tree. */
  SVN_ERR(svn_io_stat_dirent2(&dirent, local_abspath, FALSE, FALSE,
                              pool, pool));

  /* Make the intermediate directory components necessary for properly
     rooting our import source tree.  */
  if (new_entries->nelts)
    {
      int i;

      batons = apr_array_make(pool, new_entries->nelts, sizeof(void *));
      for (i = 0; i < new_entries->nelts; i++)
        {
          const char *component = APR_ARRAY_IDX(new_entries, i, const char *);
          edit_path = svn_relpath_join(edit_path, component, pool);

          /* If this is the last path component, and we're importing a
             file, then this component is the name of the file, not an
             intermediate directory. */
          if ((i == new_entries->nelts - 1) && (dirent->kind == svn_node_file))
            break;

          APR_ARRAY_PUSH(batons, void *) = root_baton;
          SVN_ERR(editor->add_directory(edit_path,
                                        root_baton,
                                        NULL, SVN_INVALID_REVNUM,
                                        pool, &root_baton));

          /* Remember that the repository was modified */
          import_ctx.repos_changed = TRUE;
        }
    }
  else if (dirent->kind == svn_node_file)
    {
      return svn_error_create
        (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
         _("New entry name required when importing a file"));
    }

  /* Note that there is no need to check whether PATH's basename is
     the same name that we reserve for our administrative
     subdirectories.  It would be strange -- though not illegal -- to
     import the contents of a directory of that name, because the
     directory's own name is not part of those contents.  Of course,
     if something underneath it also has our reserved name, then we'll
     error. */

  if (dirent->kind == svn_node_file)
    {
      /* This code path ignores EXCLUDES and FILTER, but they don't make
         much sense for a single file import anyway. */
      svn_boolean_t ignores_match = FALSE;

      if (!no_ignore)
        ignores_match =
          (svn_wc_match_ignore_list(local_abspath, global_ignores, pool)
           || svn_wc_match_ignore_list(local_abspath, local_ignores, pool));

      if (!ignores_match)
        SVN_ERR(import_file(editor, root_baton, local_abspath, edit_path,
                            dirent, &import_ctx, ctx, pool));
    }
  else if (dirent->kind == svn_node_dir)
    {
      apr_hash_t *dirents;

      /* If we are creating a new repository directory path to import to,
         then we disregard any svn:ignore property. */
      if (!no_ignore && new_entries->nelts)
        local_ignores = NULL;

      SVN_ERR(get_filtered_children(&dirents, local_abspath, excludes,
                                    local_ignores, global_ignores,
                                    filter_callback, filter_baton, ctx,
                                    pool, pool));

      SVN_ERR(import_children(local_abspath, edit_path, dirents, editor,
                              root_baton, depth, excludes, global_ignores,
                              no_ignore, no_autoprops,
                              ignore_unknown_node_types, filter_callback,
                              filter_baton, &import_ctx, ctx, pool));

    }
  else if (dirent->kind == svn_node_none
           || dirent->kind == svn_node_unknown)
    {
      return svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                               _("'%s' does not exist"),
                               svn_dirent_local_style(local_abspath, pool));
    }

  /* Close up shop; it's time to go home. */
  SVN_ERR(editor->close_directory(root_baton, pool));
  if (batons && batons->nelts)
    {
      void **baton;
      while ((baton = (void **) apr_array_pop(batons)))
        {
          SVN_ERR(editor->close_directory(*baton, pool));
        }
    }

  if (import_ctx.repos_changed)
    {
      if (ctx->notify_func2)
        {
          svn_wc_notify_t *notify;
          notify = svn_wc_create_notify_url(url,
                                            svn_wc_notify_commit_finalizing,
                                            pool);
          ctx->notify_func2(ctx->notify_baton2, notify, pool);
        }

      SVN_ERR(editor->close_edit(edit_baton, pool));

      *updated_repository = TRUE;
    }

  return SVN_NO_ERROR;
}


/*** Public Interfaces. ***/

svn_error_t *
svn_client_import5(const char *path,
                   const char *url,
                   svn_depth_t depth,
                   svn_boolean_t no_ignore,
                   svn_boolean_t no_autoprops,
                   svn_boolean_t ignore_unknown_node_types,
                   const apr_hash_t *revprop_table,
                   svn_client_import_filter_func_t filter_callback,
                   void *filter_baton,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  const char *log_msg = "";
  const svn_delta_editor_t *editor;
  void *edit_baton;
  svn_ra_session_t *ra_session;
  apr_hash_t *excludes = apr_hash_make(scratch_pool);
  svn_node_kind_t kind;
  const char *local_abspath;
  apr_array_header_t *new_entries = apr_array_make(scratch_pool, 4,
                                                   sizeof(const char *));
  apr_hash_t *commit_revprops;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *autoprops = NULL;
  apr_array_header_t *global_ignores;
  apr_array_header_t *local_ignores_arr;
  svn_revnum_t base_rev;
  apr_array_header_t *inherited_props = NULL;
  apr_hash_t *url_props = NULL;
  svn_boolean_t updated_repository;

  if (svn_path_is_url(path))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"), path);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));

  SVN_ERR(svn_io_check_path(local_abspath, &kind, scratch_pool));

  /* Create a new commit item and add it to the array. */
  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      /* If there's a log message gatherer, create a temporary commit
         item array solely to help generate the log message.  The
         array is not used for the import itself. */
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items
        = apr_array_make(scratch_pool, 1, sizeof(item));

      item = svn_client_commit_item3_create(scratch_pool);
      item->path = local_abspath;
      item->url = url;
      item->kind = kind;
      item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
      APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;

      SVN_ERR(svn_client__get_log_msg(&log_msg, &tmp_file, commit_items,
                                      ctx, scratch_pool));
      if (! log_msg)
        return SVN_NO_ERROR;
      if (tmp_file)
        {
          const char *abs_path;
          SVN_ERR(svn_dirent_get_absolute(&abs_path, tmp_file, scratch_pool));
          svn_hash_sets(excludes, abs_path, (void *)1);
        }
    }

  SVN_ERR(svn_client_open_ra_session2(&ra_session, url, NULL,
                                      ctx, scratch_pool, iterpool));

  SVN_ERR(svn_ra_get_latest_revnum(ra_session, &base_rev, iterpool));

  /* Figure out all the path components we need to create just to have
     a place to stick our imported tree. */
  SVN_ERR(svn_ra_check_path(ra_session, "", base_rev, &kind, iterpool));

  /* We can import into directories, but if a file already exists, that's
     an error. */
  if (kind == svn_node_file)
    return svn_error_createf
      (SVN_ERR_ENTRY_EXISTS, NULL,
       _("Path '%s' already exists"), url);

  while (kind == svn_node_none)
    {
      const char *dir;

      svn_pool_clear(iterpool);

      svn_uri_split(&url, &dir, url, scratch_pool);
      APR_ARRAY_PUSH(new_entries, const char *) = dir;
      SVN_ERR(svn_ra_reparent(ra_session, url, iterpool));

      SVN_ERR(svn_ra_check_path(ra_session, "", base_rev, &kind, iterpool));
    }

  /* Reverse the order of the components we added to our NEW_ENTRIES array. */
  svn_sort__array_reverse(new_entries, scratch_pool);

  /* The repository doesn't know about the reserved administrative
     directory. */
  if (new_entries->nelts)
    {
      const char *last_component
        = APR_ARRAY_IDX(new_entries, new_entries->nelts - 1, const char *);

      if (svn_wc_is_adm_dir(last_component, scratch_pool))
        return svn_error_createf
          (SVN_ERR_CL_ADM_DIR_RESERVED, NULL,
           _("'%s' is a reserved name and cannot be imported"),
           svn_dirent_local_style(last_component, scratch_pool));
    }

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           log_msg, ctx, scratch_pool));

  /* Obtain properties before opening the commit editor, as at that point we are
     not allowed to use the existing ra-session */
  if (! no_ignore /*|| ! no_autoprops*/)
    {
      SVN_ERR(svn_ra_get_dir2(ra_session, NULL, NULL, &url_props, "",
                              base_rev, SVN_DIRENT_KIND, scratch_pool));

      SVN_ERR(svn_ra_get_inherited_props(ra_session, &inherited_props, "", base_rev,
                                         scratch_pool, iterpool));
    }

  /* Fetch RA commit editor. */
  SVN_ERR(svn_ra__register_editor_shim_callbacks(ra_session,
                        svn_client__get_shim_callbacks(ctx->wc_ctx,
                                                       NULL, scratch_pool)));
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    commit_revprops, commit_callback,
                                    commit_baton, NULL, TRUE,
                                    scratch_pool));

  /* Get inherited svn:auto-props, svn:global-ignores, and
     svn:ignores for the location we are importing to. */
  if (!no_autoprops)
    {
      /* ### This should use inherited_props and url_props to avoid creating
             another ra session to obtain the same values, but using a possibly
             different HEAD revision */
      SVN_ERR(svn_client__get_all_auto_props(&autoprops, url, ctx,
                                             scratch_pool, iterpool));
    }
  if (no_ignore)
    {
      global_ignores = NULL;
      local_ignores_arr = NULL;
    }
  else
    {
      apr_array_header_t *config_ignores;
      svn_string_t *val;
      int i;

      global_ignores = apr_array_make(scratch_pool, 64, sizeof(const char *));

      SVN_ERR(svn_wc_get_default_ignores(&config_ignores, ctx->config,
                                         scratch_pool));
      global_ignores = apr_array_append(scratch_pool, global_ignores,
                                        config_ignores);

      val = svn_hash_gets(url_props, SVN_PROP_INHERITABLE_IGNORES);
      if (val)
        svn_cstring_split_append(global_ignores, val->data, "\n\r\t\v ",
                                 FALSE, scratch_pool);

      for (i = 0; i < inherited_props->nelts; i++)
        {
          svn_prop_inherited_item_t *elt = APR_ARRAY_IDX(
            inherited_props, i, svn_prop_inherited_item_t *);

          val = svn_hash_gets(elt->prop_hash, SVN_PROP_INHERITABLE_IGNORES);

          if (val)
            svn_cstring_split_append(global_ignores, val->data, "\n\r\t\v ",
                                     FALSE, scratch_pool);
        }
      local_ignores_arr = apr_array_make(scratch_pool, 1, sizeof(const char *));

      val = svn_hash_gets(url_props, SVN_PROP_IGNORE);

      if (val)
        {
          svn_cstring_split_append(local_ignores_arr, val->data,
                                   "\n\r\t\v ", FALSE, scratch_pool);
        }
    }

  /* If an error occurred during the commit, properly abort the edit.  */
  err = svn_error_trace(import(&updated_repository,
                               local_abspath, url, new_entries, editor,
                               edit_baton, depth, base_rev, excludes,
                               autoprops, local_ignores_arr, global_ignores,
                               no_ignore, no_autoprops,
                               ignore_unknown_node_types, filter_callback,
                               filter_baton, ctx, iterpool));

  svn_pool_destroy(iterpool);

  if (err || !updated_repository)
    {
      return svn_error_compose_create(
                    err,
                    editor->abort_edit(edit_baton, scratch_pool));
    }

  return SVN_NO_ERROR;
}

