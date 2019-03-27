/*
 * diff_local.c: comparing local trees with each other
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
#include "svn_hash.h"
#include "svn_types.h"
#include "svn_wc.h"
#include "svn_diff.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_sorts.h"
#include "svn_subst.h"
#include "client.h"

#include "private/svn_sorts_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_diff_tree.h"

#include "svn_private_config.h"


/* Try to get properties for LOCAL_ABSPATH and return them in the property
 * hash *PROPS. If there are no properties because LOCAL_ABSPATH is not
 * versioned, return an empty property hash. */
static svn_error_t *
get_props(apr_hash_t **props,
          const char *local_abspath,
          svn_wc_context_t *wc_ctx,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  err = svn_wc_prop_list2(props, wc_ctx, local_abspath, result_pool,
                          scratch_pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND ||
          err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY)
        {
          svn_error_clear(err);
          *props = apr_hash_make(result_pool);

          /* ### Apply autoprops, like 'svn add' would? */
        }
      else
        return svn_error_trace(err);
    }

  return SVN_NO_ERROR;
}

/* Forward declaration */
static svn_error_t *
do_file_diff(const char *left_abspath,
             const char *right_abspath,
             const char *left_root_abspath,
             const char *right_root_abspath,
             svn_boolean_t left_only,
             svn_boolean_t right_only,
             void *parent_baton,
             const svn_diff_tree_processor_t *diff_processor,
             svn_client_ctx_t *ctx,
             apr_pool_t *scratch_pool);

/* Forward declaration */
static svn_error_t *
do_dir_diff(const char *left_abspath,
            const char *right_abspath,
            const char *left_root_abspath,
            const char *right_root_abspath,
            svn_boolean_t left_only,
            svn_boolean_t right_only,
            svn_boolean_t left_before_right,
            svn_depth_t depth,
            void *parent_baton,
            const svn_diff_tree_processor_t *diff_processor,
            svn_client_ctx_t *ctx,
            apr_pool_t *scratch_pool);

/* Produce a diff of depth DEPTH between two arbitrary directories at
 * LEFT_ABSPATH1 and RIGHT_ABSPATH2, using the provided diff callbacks
 * to show file changes and, for versioned nodes, property changes.
 *
 * Report paths as relative from LEFT_ROOT_ABSPATH/RIGHT_ROOT_ABSPATH.
 *
 * If LEFT_ONLY is TRUE, only the left source exists (= everything will
 * be reported as deleted). If RIGHT_ONLY is TRUE, only the right source
 * exists (= everything will be reported as added).
 *
 * If LEFT_BEFORE_RIGHT is TRUE and left and right are unrelated, left is
 * reported first. If false, right is reported first. (This is to allow
 * producing a proper inverse diff).
 *
 * Walk the sources according to depth, and report with parent baton
 * PARENT_BATON. */
static svn_error_t *
inner_dir_diff(const char *left_abspath,
               const char *right_abspath,
               const char *left_root_abspath,
               const char *right_root_abspath,
               svn_boolean_t left_only,
               svn_boolean_t right_only,
               svn_boolean_t left_before_right,
               svn_depth_t depth,
               void *parent_baton,
               const svn_diff_tree_processor_t *diff_processor,
               svn_client_ctx_t *ctx,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_t *left_dirents;
  apr_hash_t *right_dirents;
  apr_array_header_t *sorted_dirents;
  svn_error_t *err;
  svn_depth_t depth_below_here;
  int i;

  SVN_ERR_ASSERT(depth >= svn_depth_files && depth <= svn_depth_infinity);

  if (!right_only)
    {
      err = svn_io_get_dirents3(&left_dirents, left_abspath, FALSE,
                                scratch_pool, iterpool);

      if (err && (APR_STATUS_IS_ENOENT(err->apr_err)
                  || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)))
        {
          svn_error_clear(err);
          left_dirents = apr_hash_make(scratch_pool);
          right_only = TRUE;
        }
      else
        SVN_ERR(err);
    }
  else
    left_dirents = apr_hash_make(scratch_pool);

  if (!left_only)
    {
      err = svn_io_get_dirents3(&right_dirents, right_abspath, FALSE,
                                scratch_pool, iterpool);

      if (err && (APR_STATUS_IS_ENOENT(err->apr_err)
                  || SVN__APR_STATUS_IS_ENOTDIR(err->apr_err)))
        {
          svn_error_clear(err);
          right_dirents = apr_hash_make(scratch_pool);
          left_only = TRUE;
        }
      else
        SVN_ERR(err);
    }
  else
    right_dirents = apr_hash_make(scratch_pool);

  if (left_only && right_only)
    return SVN_NO_ERROR; /* Somebody deleted the directory?? */

  if (depth != svn_depth_infinity)
    depth_below_here = svn_depth_empty;
  else
    depth_below_here = svn_depth_infinity;

  sorted_dirents = svn_sort__hash(apr_hash_merge(iterpool, left_dirents,
                                                 right_dirents, NULL, NULL),
                                  svn_sort_compare_items_as_paths,
                                  scratch_pool);

  for (i = 0; i < sorted_dirents->nelts; i++)
    {
      svn_sort__item_t* elt = &APR_ARRAY_IDX(sorted_dirents, i, svn_sort__item_t);
      svn_io_dirent2_t *left_dirent;
      svn_io_dirent2_t *right_dirent;
      const char *child_left_abspath;
      const char *child_right_abspath;

      svn_pool_clear(iterpool);

      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      if (svn_wc_is_adm_dir(elt->key, iterpool))
        continue;

      left_dirent = right_only ? NULL : svn_hash_gets(left_dirents, elt->key);
      right_dirent = left_only ? NULL : svn_hash_gets(right_dirents, elt->key);

      child_left_abspath = svn_dirent_join(left_abspath, elt->key, iterpool);
      child_right_abspath = svn_dirent_join(right_abspath, elt->key, iterpool);

      if (((left_dirent == NULL) != (right_dirent == NULL))
          || (left_dirent->kind != right_dirent->kind))
        {
          /* Report delete and/or add */
          if (left_dirent && left_before_right)
            {
              if (left_dirent->kind == svn_node_file)
                SVN_ERR(do_file_diff(child_left_abspath, child_right_abspath,
                                     left_root_abspath, right_root_abspath,
                                     TRUE, FALSE, parent_baton,
                                     diff_processor, ctx, iterpool));
              else if (depth >= svn_depth_immediates)
                SVN_ERR(do_dir_diff(child_left_abspath, child_right_abspath,
                                    left_root_abspath, right_root_abspath,
                                    TRUE, FALSE, left_before_right,
                                    depth_below_here, parent_baton,
                                    diff_processor, ctx, iterpool));
            }

          if (right_dirent)
            {
              if (right_dirent->kind == svn_node_file)
                SVN_ERR(do_file_diff(child_left_abspath, child_right_abspath,
                                     left_root_abspath, right_root_abspath,
                                     FALSE, TRUE, parent_baton,
                                     diff_processor, ctx, iterpool));
              else if (depth >= svn_depth_immediates)
                SVN_ERR(do_dir_diff(child_left_abspath, child_right_abspath,
                                    left_root_abspath, right_root_abspath,
                                    FALSE, TRUE,  left_before_right,
                                    depth_below_here, parent_baton,
                                    diff_processor, ctx, iterpool));
            }

          if (left_dirent && !left_before_right)
            {
              if (left_dirent->kind == svn_node_file)
                SVN_ERR(do_file_diff(child_left_abspath, child_right_abspath,
                                     left_root_abspath, right_root_abspath,
                                     TRUE, FALSE, parent_baton,
                                     diff_processor, ctx, iterpool));
              else if (depth >= svn_depth_immediates)
                SVN_ERR(do_dir_diff(child_left_abspath, child_right_abspath,
                                    left_root_abspath, right_root_abspath,
                                    TRUE, FALSE,  left_before_right,
                                    depth_below_here, parent_baton,
                                    diff_processor, ctx, iterpool));
            }
        }
      else if (left_dirent->kind == svn_node_file)
        {
          /* Perform file-file diff */
          SVN_ERR(do_file_diff(child_left_abspath, child_right_abspath,
                               left_root_abspath, right_root_abspath,
                               FALSE, FALSE, parent_baton,
                               diff_processor, ctx, iterpool));
        }
      else if (depth >= svn_depth_immediates)
        {
          /* Perform dir-dir diff */
          SVN_ERR(do_dir_diff(child_left_abspath, child_right_abspath,
                              left_root_abspath, right_root_abspath,
                              FALSE, FALSE,  left_before_right,
                              depth_below_here, parent_baton,
                              diff_processor, ctx, iterpool));
        }
    }

  return SVN_NO_ERROR;
}

/* Translates *LEFT_ABSPATH to a temporary file if PROPS specify that the
   file needs translation. *LEFT_ABSPATH is updated to point to a file that
   lives at least as long as RESULT_POOL when translation is necessary.
   Otherwise the value is not updated */
static svn_error_t *
translate_if_necessary(const char **local_abspath,
                       apr_hash_t *props,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  const svn_string_t *eol_style_val;
  const svn_string_t *keywords_val;
  svn_subst_eol_style_t eol_style;
  const char *eol;
  apr_hash_t *keywords;
  svn_stream_t *contents;
  svn_stream_t *dst;

  /* if (svn_hash_gets(props, SVN_PROP_SPECIAL))
      ### TODO: Implement */

  eol_style_val = svn_hash_gets(props, SVN_PROP_EOL_STYLE);
  keywords_val = svn_hash_gets(props, SVN_PROP_KEYWORDS);

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
                                      "", "", 0, "", scratch_pool));
  else
    keywords = NULL;

  if (!svn_subst_translation_required(eol_style, eol, keywords, FALSE, FALSE))
    return SVN_NO_ERROR;

  SVN_ERR(svn_stream_open_readonly(&contents, *local_abspath,
                                    scratch_pool, scratch_pool));

  SVN_ERR(svn_stream_open_unique(&dst, local_abspath, NULL,
                                  svn_io_file_del_on_pool_cleanup,
                                  result_pool, scratch_pool));

  dst = svn_subst_stream_translated(dst, eol, TRUE /* repair */,
                                    keywords, FALSE /* expand */,
                                    scratch_pool);

  SVN_ERR(svn_stream_copy3(contents, dst, cancel_func, cancel_baton,
                           scratch_pool));

  return SVN_NO_ERROR;
}

/* Handles reporting of a file for inner_dir_diff */
static svn_error_t *
do_file_diff(const char *left_abspath,
             const char *right_abspath,
             const char *left_root_abspath,
             const char *right_root_abspath,
             svn_boolean_t left_only,
             svn_boolean_t right_only,
             void *parent_baton,
             const svn_diff_tree_processor_t *diff_processor,
             svn_client_ctx_t *ctx,
             apr_pool_t *scratch_pool)
{
  const char *relpath;
  svn_diff_source_t *left_source;
  svn_diff_source_t *right_source;
  svn_boolean_t skip = FALSE;
  apr_hash_t *left_props;
  apr_hash_t *right_props;
  void *file_baton;

  relpath = svn_dirent_skip_ancestor(left_root_abspath, left_abspath);

 if (! right_only)
    left_source = svn_diff__source_create(SVN_INVALID_REVNUM, scratch_pool);
  else
    left_source = NULL;

  if (! left_only)
    right_source = svn_diff__source_create(SVN_INVALID_REVNUM, scratch_pool);
  else
    right_source = NULL;

  SVN_ERR(diff_processor->file_opened(&file_baton, &skip,
                                      relpath,
                                      left_source,
                                      right_source,
                                      NULL /* copyfrom_source */,
                                      parent_baton,
                                      diff_processor,
                                      scratch_pool,
                                      scratch_pool));

  if (skip)
    return SVN_NO_ERROR;

   if (! right_only)
    {
      SVN_ERR(get_props(&left_props, left_abspath, ctx->wc_ctx,
                        scratch_pool, scratch_pool));

      /* We perform a mimetype detection to avoid diffing binary files
         for textual changes.*/
      if (! svn_hash_gets(left_props, SVN_PROP_MIME_TYPE))
        {
          const char *mime_type;

          /* ### Use libmagic magic? */
          SVN_ERR(svn_io_detect_mimetype2(&mime_type, left_abspath,
                                          ctx->mimetypes_map, scratch_pool));

          if (mime_type)
            svn_hash_sets(left_props, SVN_PROP_MIME_TYPE,
                          svn_string_create(mime_type, scratch_pool));
        }

      SVN_ERR(translate_if_necessary(&left_abspath, left_props,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     scratch_pool, scratch_pool));
    }
  else
    left_props = NULL;

  if (! left_only)
    {
      SVN_ERR(get_props(&right_props, right_abspath, ctx->wc_ctx,
                        scratch_pool, scratch_pool));

      /* We perform a mimetype detection to avoid diffing binary files
         for textual changes.*/
      if (! svn_hash_gets(right_props, SVN_PROP_MIME_TYPE))
        {
          const char *mime_type;

          /* ### Use libmagic magic? */
          SVN_ERR(svn_io_detect_mimetype2(&mime_type, right_abspath,
                                          ctx->mimetypes_map, scratch_pool));

          if (mime_type)
            svn_hash_sets(right_props, SVN_PROP_MIME_TYPE,
                          svn_string_create(mime_type, scratch_pool));
        }

      SVN_ERR(translate_if_necessary(&right_abspath, right_props,
                                     ctx->cancel_func, ctx->cancel_baton,
                                     scratch_pool, scratch_pool));

    }
  else
    right_props = NULL;

  if (left_only)
    {
      SVN_ERR(diff_processor->file_deleted(relpath,
                                           left_source,
                                           left_abspath,
                                           left_props,
                                           file_baton,
                                           diff_processor,
                                           scratch_pool));
    }
  else if (right_only)
    {
      SVN_ERR(diff_processor->file_added(relpath,
                                         NULL /* copyfrom_source */,
                                         right_source,
                                         NULL /* copyfrom_file */,
                                         right_abspath,
                                         NULL /* copyfrom_props */,
                                         right_props,
                                         file_baton,
                                         diff_processor,
                                         scratch_pool));
    }
  else
    {
      /* ### Perform diff -> close/changed */
      svn_boolean_t same;
      apr_array_header_t *prop_changes;

      SVN_ERR(svn_io_files_contents_same_p(&same, left_abspath, right_abspath,
                                           scratch_pool));

      SVN_ERR(svn_prop_diffs(&prop_changes, right_props, left_props,
                             scratch_pool));

      if (!same || prop_changes->nelts > 0)
        {
          SVN_ERR(diff_processor->file_changed(relpath,
                                               left_source,
                                               right_source,
                                               same ? NULL : left_abspath,
                                               same ? NULL : right_abspath,
                                               left_props,
                                               right_props,
                                               !same,
                                               prop_changes,
                                               file_baton,
                                               diff_processor,
                                               scratch_pool));
        }
      else
        {
          SVN_ERR(diff_processor->file_closed(relpath,
                                            left_source,
                                            right_source,
                                            file_baton,
                                            diff_processor,
                                            scratch_pool));
        }
    }
  return SVN_NO_ERROR;
}


/* Handles reporting of a directory and its children for inner_dir_diff */
static svn_error_t *
do_dir_diff(const char *left_abspath,
            const char *right_abspath,
            const char *left_root_abspath,
            const char *right_root_abspath,
            svn_boolean_t left_only,
            svn_boolean_t right_only,
            svn_boolean_t left_before_right,
            svn_depth_t depth,
            void *parent_baton,
            const svn_diff_tree_processor_t *diff_processor,
            svn_client_ctx_t *ctx,
            apr_pool_t *scratch_pool)
{
  const char *relpath;
  svn_diff_source_t *left_source;
  svn_diff_source_t *right_source;
  svn_boolean_t skip = FALSE;
  svn_boolean_t skip_children = FALSE;
  void *dir_baton;
  apr_hash_t *left_props;
  apr_hash_t *right_props;

  relpath = svn_dirent_skip_ancestor(left_root_abspath, left_abspath);

  if (! right_only)
    {
      left_source = svn_diff__source_create(SVN_INVALID_REVNUM, scratch_pool);
      SVN_ERR(get_props(&left_props, left_abspath, ctx->wc_ctx,
                        scratch_pool, scratch_pool));
    }
  else
    {
      left_source = NULL;
      left_props = NULL;
    }

  if (! left_only)
    {
      right_source = svn_diff__source_create(SVN_INVALID_REVNUM, scratch_pool);
      SVN_ERR(get_props(&right_props, right_abspath, ctx->wc_ctx,
                        scratch_pool, scratch_pool));
    }
  else
    {
      right_source = NULL;
      right_props = NULL;
    }

  SVN_ERR(diff_processor->dir_opened(&dir_baton, &skip, &skip_children,
                                     relpath,
                                     left_source,
                                     right_source,
                                     NULL /* copyfrom_source */,
                                     parent_baton,
                                     diff_processor,
                                     scratch_pool, scratch_pool));

  if (!skip_children)
    {
      if (depth >= svn_depth_files)
        SVN_ERR(inner_dir_diff(left_abspath, right_abspath,
                               left_root_abspath, right_root_abspath,
                               left_only, right_only,
                               left_before_right, depth,
                               dir_baton,
                               diff_processor, ctx, scratch_pool));
    }
  else if (skip)
    return SVN_NO_ERROR;

  if (left_props && right_props)
    {
      apr_array_header_t *prop_diffs;

      SVN_ERR(svn_prop_diffs(&prop_diffs, right_props, left_props,
                             scratch_pool));

      if (prop_diffs->nelts)
        {
          SVN_ERR(diff_processor->dir_changed(relpath,
                                              left_source,
                                              right_source,
                                              left_props,
                                              right_props,
                                              prop_diffs,
                                              dir_baton,
                                              diff_processor,
                                              scratch_pool));
          return SVN_NO_ERROR;
        }
    }

  if (left_source && right_source)
    {
      SVN_ERR(diff_processor->dir_closed(relpath,
                                         left_source,
                                         right_source,
                                         dir_baton,
                                         diff_processor,
                                         scratch_pool));
    }
  else if (left_source)
    {
      SVN_ERR(diff_processor->dir_deleted(relpath,
                                          left_source,
                                          left_props,
                                          dir_baton,
                                          diff_processor,
                                          scratch_pool));
    }
  else
    {
      SVN_ERR(diff_processor->dir_added(relpath,
                                        NULL /* copyfrom_source */,
                                        right_source,
                                        NULL /* copyfrom_props */,
                                        right_props,
                                        dir_baton,
                                        diff_processor,
                                        scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__arbitrary_nodes_diff(const char **root_relpath,
                                 svn_boolean_t *root_is_dir,
                                 const char *left_abspath,
                                 const char *right_abspath,
                                 svn_depth_t depth,
                                 const svn_diff_tree_processor_t *diff_processor,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_node_kind_t left_kind;
  svn_node_kind_t right_kind;
  const char *left_root_abspath;
  const char *right_root_abspath;
  svn_boolean_t left_before_right = TRUE; /* Future argument? */

  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  SVN_ERR(svn_io_check_resolved_path(left_abspath, &left_kind, scratch_pool));
  SVN_ERR(svn_io_check_resolved_path(right_abspath, &right_kind, scratch_pool));

  if (left_kind == svn_node_dir && right_kind == svn_node_dir)
    {
      left_root_abspath = left_abspath;
      right_root_abspath = right_abspath;

      if (root_relpath)
        *root_relpath = "";
      if (root_is_dir)
        *root_is_dir = TRUE;
    }
  else
    {
      svn_dirent_split(&left_root_abspath, root_relpath, left_abspath,
                       scratch_pool);
      right_root_abspath = svn_dirent_dirname(right_abspath, scratch_pool);

      if (root_relpath)
        *root_relpath = apr_pstrdup(result_pool, *root_relpath);
      if (root_is_dir)
        *root_is_dir = FALSE;
    }

  if (left_kind == svn_node_dir && right_kind == svn_node_dir)
    {
      SVN_ERR(do_dir_diff(left_abspath, right_abspath,
                          left_root_abspath, right_root_abspath,
                          FALSE, FALSE, left_before_right,
                          depth, NULL /* parent_baton */,
                          diff_processor, ctx, scratch_pool));
    }
  else if (left_kind == svn_node_file && right_kind == svn_node_file)
    {
      SVN_ERR(do_file_diff(left_abspath, right_abspath,
                           left_root_abspath, right_root_abspath,
                           FALSE, FALSE,
                           NULL /* parent_baton */,
                           diff_processor, ctx, scratch_pool));
    }
  else if (left_kind == svn_node_file || left_kind == svn_node_dir
           || right_kind == svn_node_file || right_kind == svn_node_dir)
    {
      void *dir_baton;
      svn_boolean_t skip = FALSE;
      svn_boolean_t skip_children = FALSE;
      svn_diff_source_t *left_src;
      svn_diff_source_t *right_src;

      left_src = svn_diff__source_create(SVN_INVALID_REVNUM, scratch_pool);
      right_src = svn_diff__source_create(SVN_INVALID_REVNUM, scratch_pool);

      /* The root is replaced... */
      /* Report delete and/or add */

      SVN_ERR(diff_processor->dir_opened(&dir_baton, &skip, &skip_children, "",
                                         left_src,
                                         right_src,
                                         NULL /* copyfrom_src */,
                                         NULL,
                                         diff_processor,
                                         scratch_pool, scratch_pool));

      if (skip)
        return SVN_NO_ERROR;
      else if (!skip_children)
        {
          if (left_before_right)
            {
              if (left_kind == svn_node_file)
                SVN_ERR(do_file_diff(left_abspath, right_abspath,
                                     left_root_abspath, right_root_abspath,
                                     TRUE, FALSE, NULL /* parent_baton */,
                                     diff_processor, ctx, scratch_pool));
              else if (left_kind == svn_node_dir)
                SVN_ERR(do_dir_diff(left_abspath, right_abspath,
                                    left_root_abspath, right_root_abspath,
                                    TRUE, FALSE, left_before_right,
                                    depth, NULL /* parent_baton */,
                                    diff_processor, ctx, scratch_pool));
            }

          if (right_kind == svn_node_file)
            SVN_ERR(do_file_diff(left_abspath, right_abspath,
                                 left_root_abspath, right_root_abspath,
                                 FALSE, TRUE, NULL /* parent_baton */,
                                 diff_processor, ctx, scratch_pool));
          else if (right_kind == svn_node_dir)
            SVN_ERR(do_dir_diff(left_abspath, right_abspath,
                                left_root_abspath, right_root_abspath,
                                FALSE, TRUE,  left_before_right,
                                depth, NULL /* parent_baton */,
                                diff_processor, ctx, scratch_pool));

          if (! left_before_right)
            {
              if (left_kind == svn_node_file)
                SVN_ERR(do_file_diff(left_abspath, right_abspath,
                                     left_root_abspath, right_root_abspath,
                                     TRUE, FALSE, NULL /* parent_baton */,
                                     diff_processor, ctx, scratch_pool));
              else if (left_kind == svn_node_dir)
                SVN_ERR(do_dir_diff(left_abspath, right_abspath,
                                    left_root_abspath, right_root_abspath,
                                    TRUE, FALSE,  left_before_right,
                                    depth, NULL /* parent_baton */,
                                    diff_processor, ctx, scratch_pool));
            }
        }

      SVN_ERR(diff_processor->dir_closed("",
                                         left_src,
                                         right_src,
                                         dir_baton,
                                         diff_processor,
                                         scratch_pool));
    }
  else
    return svn_error_createf(SVN_ERR_NODE_UNEXPECTED_KIND, NULL,
                             _("'%s' is not a file or directory"),
                             svn_dirent_local_style(
                                    (left_kind == svn_node_none)
                                        ? left_abspath
                                        : right_abspath,
                                    scratch_pool));

  return SVN_NO_ERROR;
}
