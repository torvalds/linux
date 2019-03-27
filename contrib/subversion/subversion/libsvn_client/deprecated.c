/*
 * deprecated.c:  holding file for all deprecated APIs.
 *                "we can't lose 'em, but we can shun 'em!"
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

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include <string.h>
#include "svn_client.h"
#include "svn_path.h"
#include "svn_compat.h"
#include "svn_hash.h"
#include "svn_props.h"
#include "svn_utf.h"
#include "svn_string.h"
#include "svn_pools.h"

#include "client.h"
#include "mergeinfo.h"

#include "private/svn_opt_private.h"
#include "private/svn_wc_private.h"
#include "svn_private_config.h"




/*** Code. ***/


/* Baton for capture_commit_info() */
struct capture_baton_t {
  svn_commit_info_t **info;
  apr_pool_t *pool;
};


/* Callback which implements svn_commit_callback2_t for use with some
   backward compat functions. */
static svn_error_t *
capture_commit_info(const svn_commit_info_t *commit_info,
                    void *baton,
                    apr_pool_t *pool)
{
  struct capture_baton_t *cb = baton;

  *(cb->info) = svn_commit_info_dup(commit_info, cb->pool);

  return SVN_NO_ERROR;
}


/*** From add.c ***/
svn_error_t *
svn_client_add4(const char *path,
                svn_depth_t depth,
                svn_boolean_t force,
                svn_boolean_t no_ignore,
                svn_boolean_t add_parents,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_add5(path, depth, force, no_ignore, FALSE, add_parents,
                         ctx, pool);
}

svn_error_t *
svn_client_add3(const char *path,
                svn_boolean_t recursive,
                svn_boolean_t force,
                svn_boolean_t no_ignore,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_add4(path, SVN_DEPTH_INFINITY_OR_EMPTY(recursive),
                         force, no_ignore, FALSE, ctx,
                         pool);
}

svn_error_t *
svn_client_add2(const char *path,
                svn_boolean_t recursive,
                svn_boolean_t force,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_add3(path, recursive, force, FALSE, ctx, pool);
}

svn_error_t *
svn_client_add(const char *path,
               svn_boolean_t recursive,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  return svn_client_add3(path, recursive, FALSE, FALSE, ctx, pool);
}

svn_error_t *
svn_client_mkdir3(svn_commit_info_t **commit_info_p,
                  const apr_array_header_t *paths,
                  svn_boolean_t make_parents,
                  const apr_hash_t *revprop_table,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  struct capture_baton_t cb;

  cb.info = commit_info_p;
  cb.pool = pool;

  return svn_client_mkdir4(paths, make_parents, revprop_table,
                           capture_commit_info, &cb, ctx, pool);
}

svn_error_t *
svn_client_mkdir2(svn_commit_info_t **commit_info_p,
                  const apr_array_header_t *paths,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_mkdir3(commit_info_p, paths, FALSE, NULL, ctx, pool);
}


svn_error_t *
svn_client_mkdir(svn_client_commit_info_t **commit_info_p,
                 const apr_array_header_t *paths,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_mkdir2(&commit_info, paths, ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return svn_error_trace(err);
}

/*** From blame.c ***/

struct blame_receiver_wrapper_baton2 {
  void *baton;
  svn_client_blame_receiver2_t receiver;
};

static svn_error_t *
blame_wrapper_receiver2(void *baton,
   svn_revnum_t start_revnum,
   svn_revnum_t end_revnum,
   apr_int64_t line_no,
   svn_revnum_t revision,
   apr_hash_t *rev_props,
   svn_revnum_t merged_revision,
   apr_hash_t *merged_rev_props,
   const char *merged_path,
   const char *line,
   svn_boolean_t local_change,
   apr_pool_t *pool)
{
  struct blame_receiver_wrapper_baton2 *brwb = baton;
  const char *author = NULL;
  const char *date = NULL;
  const char *merged_author = NULL;
  const char *merged_date = NULL;

  if (rev_props != NULL)
    {
      author = svn_prop_get_value(rev_props, SVN_PROP_REVISION_AUTHOR);
      date = svn_prop_get_value(rev_props, SVN_PROP_REVISION_DATE);
    }
  if (merged_rev_props != NULL)
    {
      merged_author = svn_prop_get_value(merged_rev_props,
                                         SVN_PROP_REVISION_AUTHOR);
      merged_date = svn_prop_get_value(merged_rev_props,
                                       SVN_PROP_REVISION_DATE);
    }

  if (brwb->receiver)
    return brwb->receiver(brwb->baton, line_no, revision, author, date,
                          merged_revision, merged_author, merged_date,
                          merged_path, line, pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_blame4(const char *target,
                  const svn_opt_revision_t *peg_revision,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  const svn_diff_file_options_t *diff_options,
                  svn_boolean_t ignore_mime_type,
                  svn_boolean_t include_merged_revisions,
                  svn_client_blame_receiver2_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  struct blame_receiver_wrapper_baton2 baton;

  baton.receiver = receiver;
  baton.baton = receiver_baton;

  return svn_client_blame5(target, peg_revision, start, end, diff_options,
                           ignore_mime_type, include_merged_revisions,
                           blame_wrapper_receiver2, &baton, ctx, pool);
}


/* Baton for use with wrap_blame_receiver */
struct blame_receiver_wrapper_baton {
  void *baton;
  svn_client_blame_receiver_t receiver;
};

/* This implements svn_client_blame_receiver2_t */
static svn_error_t *
blame_wrapper_receiver(void *baton,
                       apr_int64_t line_no,
                       svn_revnum_t revision,
                       const char *author,
                       const char *date,
                       svn_revnum_t merged_revision,
                       const char *merged_author,
                       const char *merged_date,
                       const char *merged_path,
                       const char *line,
                       apr_pool_t *pool)
{
  struct blame_receiver_wrapper_baton *brwb = baton;

  if (brwb->receiver)
    return brwb->receiver(brwb->baton,
                          line_no, revision, author, date, line, pool);

  return SVN_NO_ERROR;
}

static void
wrap_blame_receiver(svn_client_blame_receiver2_t *receiver2,
                    void **receiver2_baton,
                    svn_client_blame_receiver_t receiver,
                    void *receiver_baton,
                    apr_pool_t *pool)
{
  struct blame_receiver_wrapper_baton *brwb = apr_palloc(pool, sizeof(*brwb));

  /* Set the user provided old format callback in the baton. */
  brwb->baton = receiver_baton;
  brwb->receiver = receiver;

  *receiver2_baton = brwb;
  *receiver2 = blame_wrapper_receiver;
}

svn_error_t *
svn_client_blame3(const char *target,
                  const svn_opt_revision_t *peg_revision,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  const svn_diff_file_options_t *diff_options,
                  svn_boolean_t ignore_mime_type,
                  svn_client_blame_receiver_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_client_blame_receiver2_t receiver2;
  void *receiver2_baton;

  wrap_blame_receiver(&receiver2, &receiver2_baton, receiver, receiver_baton,
                      pool);

  return svn_client_blame4(target, peg_revision, start, end, diff_options,
                           ignore_mime_type, FALSE, receiver2, receiver2_baton,
                           ctx, pool);
}

/* svn_client_blame3 guarantees 'no EOL chars' as part of the receiver
   LINE argument.  Older versions depend on the fact that if a CR is
   required, that CR is already part of the LINE data.

   Because of this difference, we need to trap old receivers and append
   a CR to LINE before passing it on to the actual receiver on platforms
   which want CRLF line termination.

*/

struct wrapped_receiver_baton_s
{
  svn_client_blame_receiver_t orig_receiver;
  void *orig_baton;
};

static svn_error_t *
wrapped_receiver(void *baton,
                 apr_int64_t line_no,
                 svn_revnum_t revision,
                 const char *author,
                 const char *date,
                 const char *line,
                 apr_pool_t *pool)
{
  struct wrapped_receiver_baton_s *b = baton;
  svn_stringbuf_t *expanded_line = svn_stringbuf_create(line, pool);

  svn_stringbuf_appendbyte(expanded_line, '\r');

  return b->orig_receiver(b->orig_baton, line_no, revision, author,
                          date, expanded_line->data, pool);
}

static void
wrap_pre_blame3_receiver(svn_client_blame_receiver_t *receiver,
                         void **receiver_baton,
                         apr_pool_t *pool)
{
  if (sizeof(APR_EOL_STR) == 3)
    {
      struct wrapped_receiver_baton_s *b = apr_palloc(pool,sizeof(*b));

      b->orig_receiver = *receiver;
      b->orig_baton = *receiver_baton;

      *receiver_baton = b;
      *receiver = wrapped_receiver;
    }
}

svn_error_t *
svn_client_blame2(const char *target,
                  const svn_opt_revision_t *peg_revision,
                  const svn_opt_revision_t *start,
                  const svn_opt_revision_t *end,
                  svn_client_blame_receiver_t receiver,
                  void *receiver_baton,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  wrap_pre_blame3_receiver(&receiver, &receiver_baton, pool);
  return svn_client_blame3(target, peg_revision, start, end,
                           svn_diff_file_options_create(pool), FALSE,
                           receiver, receiver_baton, ctx, pool);
}
svn_error_t *
svn_client_blame(const char *target,
                 const svn_opt_revision_t *start,
                 const svn_opt_revision_t *end,
                 svn_client_blame_receiver_t receiver,
                 void *receiver_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  wrap_pre_blame3_receiver(&receiver, &receiver_baton, pool);
  return svn_client_blame2(target, end, start, end,
                           receiver, receiver_baton, ctx, pool);
}

/*** From cmdline.c ***/
svn_error_t *
svn_client_args_to_target_array(apr_array_header_t **targets_p,
                                apr_getopt_t *os,
                                const apr_array_header_t *known_targets,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *pool)
{
  return svn_client_args_to_target_array2(targets_p, os, known_targets, ctx,
                                          FALSE, pool);
}

/*** From commit.c ***/
svn_error_t *
svn_client_import4(const char *path,
                   const char *url,
                   svn_depth_t depth,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_unknown_node_types,
                   const apr_hash_t *revprop_table,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_error_trace(svn_client_import5(path, url, depth, no_ignore,
                                            FALSE, ignore_unknown_node_types,
                                            revprop_table,
                                            NULL, NULL,
                                            commit_callback, commit_baton,
                                            ctx, pool));
}


svn_error_t *
svn_client_import3(svn_commit_info_t **commit_info_p,
                   const char *path,
                   const char *url,
                   svn_depth_t depth,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_unknown_node_types,
                   const apr_hash_t *revprop_table,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  struct capture_baton_t cb;

  cb.info = commit_info_p;
  cb.pool = pool;

  return svn_client_import4(path, url, depth, no_ignore,
                            ignore_unknown_node_types, revprop_table,
                            capture_commit_info, &cb, ctx, pool);
}

svn_error_t *
svn_client_import2(svn_commit_info_t **commit_info_p,
                   const char *path,
                   const char *url,
                   svn_boolean_t nonrecursive,
                   svn_boolean_t no_ignore,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_import3(commit_info_p,
                            path, url,
                            SVN_DEPTH_INFINITY_OR_FILES(! nonrecursive),
                            no_ignore, FALSE, NULL, ctx, pool);
}

svn_error_t *
svn_client_import(svn_client_commit_info_t **commit_info_p,
                  const char *path,
                  const char *url,
                  svn_boolean_t nonrecursive,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_import2(&commit_info,
                           path, url, nonrecursive,
                           FALSE, ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return svn_error_trace(err);
}


/* Wrapper notify_func2 function and baton for downgrading
   svn_wc_notify_commit_copied and svn_wc_notify_commit_copied_replaced
   to svn_wc_notify_commit_added and svn_wc_notify_commit_replaced,
   respectively. */
struct downgrade_commit_copied_notify_baton
{
  svn_wc_notify_func2_t orig_notify_func2;
  void *orig_notify_baton2;
};

static void
downgrade_commit_copied_notify_func(void *baton,
                                    const svn_wc_notify_t *notify,
                                    apr_pool_t *pool)
{
  struct downgrade_commit_copied_notify_baton *b = baton;

  if (notify->action == svn_wc_notify_commit_copied)
    {
      svn_wc_notify_t *my_notify = svn_wc_dup_notify(notify, pool);
      my_notify->action = svn_wc_notify_commit_added;
      notify = my_notify;
    }
  else if (notify->action == svn_wc_notify_commit_copied_replaced)
    {
      svn_wc_notify_t *my_notify = svn_wc_dup_notify(notify, pool);
      my_notify->action = svn_wc_notify_commit_replaced;
      notify = my_notify;
    }

  /* Call the wrapped notification system (if any) with MY_NOTIFY,
     which is either the original NOTIFY object, or a tweaked deep
     copy thereof. */
  if (b->orig_notify_func2)
    b->orig_notify_func2(b->orig_notify_baton2, notify, pool);
}

svn_error_t *
svn_client_commit5(const apr_array_header_t *targets,
                   svn_depth_t depth,
                   svn_boolean_t keep_locks,
                   svn_boolean_t keep_changelists,
                   svn_boolean_t commit_as_operations,
                   const apr_array_header_t *changelists,
                   const apr_hash_t *revprop_table,
                   svn_commit_callback2_t commit_callback,
                   void *commit_baton,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_commit6(targets, depth, keep_locks, keep_changelists,
                            commit_as_operations,
                            FALSE,  /* include_file_externals */
                            FALSE, /* include_dir_externals */
                            changelists, revprop_table, commit_callback,
                            commit_baton, ctx, pool);
}

svn_error_t *
svn_client_commit4(svn_commit_info_t **commit_info_p,
                   const apr_array_header_t *targets,
                   svn_depth_t depth,
                   svn_boolean_t keep_locks,
                   svn_boolean_t keep_changelists,
                   const apr_array_header_t *changelists,
                   const apr_hash_t *revprop_table,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  struct capture_baton_t cb;
  struct downgrade_commit_copied_notify_baton notify_baton;
  svn_error_t *err;

  notify_baton.orig_notify_func2 = ctx->notify_func2;
  notify_baton.orig_notify_baton2 = ctx->notify_baton2;

  *commit_info_p = NULL;
  cb.info = commit_info_p;
  cb.pool = pool;

  /* Swap out the notification system (if any) with a thin filtering
     wrapper. */
  if (ctx->notify_func2)
    {
      ctx->notify_func2 = downgrade_commit_copied_notify_func;
      ctx->notify_baton2 = &notify_baton;
    }

  err = svn_client_commit5(targets, depth, keep_locks, keep_changelists, FALSE,
                           changelists, revprop_table,
                           capture_commit_info, &cb, ctx, pool);

  /* Ensure that the original notification system is in place. */
  ctx->notify_func2 = notify_baton.orig_notify_func2;
  ctx->notify_baton2 = notify_baton.orig_notify_baton2;

  SVN_ERR(err);

  if (! *commit_info_p)
    *commit_info_p = svn_create_commit_info(pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_commit3(svn_commit_info_t **commit_info_p,
                   const apr_array_header_t *targets,
                   svn_boolean_t recurse,
                   svn_boolean_t keep_locks,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_depth_t depth = SVN_DEPTH_INFINITY_OR_EMPTY(recurse);

  return svn_client_commit4(commit_info_p, targets, depth, keep_locks,
                            FALSE, NULL, NULL, ctx, pool);
}

svn_error_t *
svn_client_commit2(svn_client_commit_info_t **commit_info_p,
                   const apr_array_header_t *targets,
                   svn_boolean_t recurse,
                   svn_boolean_t keep_locks,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_commit3(&commit_info, targets, recurse, keep_locks,
                           ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return svn_error_trace(err);
}

svn_error_t *
svn_client_commit(svn_client_commit_info_t **commit_info_p,
                  const apr_array_header_t *targets,
                  svn_boolean_t nonrecursive,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_commit2(commit_info_p, targets,
                            ! nonrecursive,
                            TRUE,
                            ctx, pool);
}

/*** From copy.c ***/
svn_error_t *
svn_client_copy6(const apr_array_header_t *sources,
                 const char *dst_path,
                 svn_boolean_t copy_as_child,
                 svn_boolean_t make_parents,
                 svn_boolean_t ignore_externals,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_error_trace(svn_client_copy7(sources, dst_path, copy_as_child,
                                          make_parents, ignore_externals,
                                          FALSE /* metadata_only */,
                                          FALSE /* pin_externals */,
                                          NULL /* externals_to_pin */,
                                          revprop_table,
                                          commit_callback, commit_baton,
                                          ctx, pool));
}

svn_error_t *
svn_client_copy5(svn_commit_info_t **commit_info_p,
                 const apr_array_header_t *sources,
                 const char *dst_path,
                 svn_boolean_t copy_as_child,
                 svn_boolean_t make_parents,
                 svn_boolean_t ignore_externals,
                 const apr_hash_t *revprop_table,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  struct capture_baton_t cb;

  cb.info = commit_info_p;
  cb.pool = pool;

  return svn_client_copy6(sources, dst_path, copy_as_child, make_parents,
                          ignore_externals, revprop_table,
                          capture_commit_info, &cb, ctx, pool);
}

svn_error_t *
svn_client_copy4(svn_commit_info_t **commit_info_p,
                 const apr_array_header_t *sources,
                 const char *dst_path,
                 svn_boolean_t copy_as_child,
                 svn_boolean_t make_parents,
                 const apr_hash_t *revprop_table,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_copy5(commit_info_p, sources, dst_path, copy_as_child,
                          make_parents, FALSE, revprop_table, ctx, pool);
}

svn_error_t *
svn_client_copy3(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const svn_opt_revision_t *src_revision,
                 const char *dst_path,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  apr_array_header_t *sources = apr_array_make(pool, 1,
                                  sizeof(const svn_client_copy_source_t *));
  svn_client_copy_source_t copy_source;

  copy_source.path = src_path;
  copy_source.revision = src_revision;
  copy_source.peg_revision = src_revision;

  APR_ARRAY_PUSH(sources, const svn_client_copy_source_t *) = &copy_source;

  return svn_client_copy4(commit_info_p, sources, dst_path, FALSE, FALSE,
                          NULL, ctx, pool);
}

svn_error_t *
svn_client_copy2(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const svn_opt_revision_t *src_revision,
                 const char *dst_path,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_client_copy3(commit_info_p, src_path, src_revision,
                         dst_path, ctx, pool);

  /* If the target exists, try to copy the source as a child of the target.
     This will obviously fail if target is not a directory, but that's exactly
     what we want. */
  if (err && (err->apr_err == SVN_ERR_ENTRY_EXISTS
              || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_basename = svn_path_basename(src_path, pool);

      svn_error_clear(err);

      return svn_client_copy3(commit_info_p, src_path, src_revision,
                              svn_path_join(dst_path, src_basename, pool),
                              ctx, pool);
    }

  return svn_error_trace(err);
}

svn_error_t *
svn_client_copy(svn_client_commit_info_t **commit_info_p,
                const char *src_path,
                const svn_opt_revision_t *src_revision,
                const char *dst_path,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_copy2(&commit_info, src_path, src_revision, dst_path,
                         ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return svn_error_trace(err);
}

svn_error_t *
svn_client_move6(const apr_array_header_t *src_paths,
                 const char *dst_path,
                 svn_boolean_t move_as_child,
                 svn_boolean_t make_parents,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_error_trace(svn_client_move7(src_paths, dst_path,
                                          move_as_child, make_parents,
                                          TRUE /* allow_mixed_revisions */,
                                          FALSE /* metadata_only */,
                                          revprop_table,
                                          commit_callback, commit_baton,
                                          ctx, pool));
}

svn_error_t *
svn_client_move5(svn_commit_info_t **commit_info_p,
                 const apr_array_header_t *src_paths,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_boolean_t move_as_child,
                 svn_boolean_t make_parents,
                 const apr_hash_t *revprop_table,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  struct capture_baton_t cb;

  cb.info = commit_info_p;
  cb.pool = pool;

  return svn_client_move6(src_paths, dst_path, move_as_child,
                          make_parents, revprop_table,
                          capture_commit_info, &cb, ctx, pool);
}

svn_error_t *
svn_client_move4(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  apr_array_header_t *src_paths =
    apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(src_paths, const char *) = src_path;


  return svn_client_move5(commit_info_p, src_paths, dst_path, force, FALSE,
                          FALSE, NULL, ctx, pool);
}

svn_error_t *
svn_client_move3(svn_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  err = svn_client_move4(commit_info_p, src_path, dst_path, force, ctx, pool);

  /* If the target exists, try to move the source as a child of the target.
     This will obviously fail if target is not a directory, but that's exactly
     what we want. */
  if (err && (err->apr_err == SVN_ERR_ENTRY_EXISTS
              || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_basename = svn_path_basename(src_path, pool);

      svn_error_clear(err);

      return svn_client_move4(commit_info_p, src_path,
                              svn_path_join(dst_path, src_basename, pool),
                              force, ctx, pool);
    }

  return svn_error_trace(err);
}

svn_error_t *
svn_client_move2(svn_client_commit_info_t **commit_info_p,
                 const char *src_path,
                 const char *dst_path,
                 svn_boolean_t force,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err;

  err = svn_client_move3(&commit_info, src_path, dst_path, force, ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return svn_error_trace(err);
}


svn_error_t *
svn_client_move(svn_client_commit_info_t **commit_info_p,
                const char *src_path,
                const svn_opt_revision_t *src_revision,
                const char *dst_path,
                svn_boolean_t force,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  /* It doesn't make sense to specify revisions in a move. */

  /* ### todo: this check could fail wrongly.  For example,
     someone could pass in an svn_opt_revision_number that just
     happens to be the HEAD.  It's fair enough to punt then, IMHO,
     and just demand that the user not specify a revision at all;
     beats mucking up this function with RA calls and such. */
  if (src_revision->kind != svn_opt_revision_unspecified
      && src_revision->kind != svn_opt_revision_head)
    {
      return svn_error_create
        (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("Cannot specify revisions (except HEAD) with move operations"));
    }

  return svn_client_move2(commit_info_p, src_path, dst_path, force, ctx, pool);
}

/*** From delete.c ***/
svn_error_t *
svn_client_delete3(svn_commit_info_t **commit_info_p,
                   const apr_array_header_t *paths,
                   svn_boolean_t force,
                   svn_boolean_t keep_local,
                   const apr_hash_t *revprop_table,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  struct capture_baton_t cb;

  cb.info = commit_info_p;
  cb.pool = pool;

  return svn_client_delete4(paths, force, keep_local, revprop_table,
                            capture_commit_info, &cb, ctx, pool);
}

svn_error_t *
svn_client_delete2(svn_commit_info_t **commit_info_p,
                   const apr_array_header_t *paths,
                   svn_boolean_t force,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_delete3(commit_info_p, paths, force, FALSE, NULL,
                            ctx, pool);
}

svn_error_t *
svn_client_delete(svn_client_commit_info_t **commit_info_p,
                  const apr_array_header_t *paths,
                  svn_boolean_t force,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_commit_info_t *commit_info = NULL;
  svn_error_t *err = NULL;

  err = svn_client_delete2(&commit_info, paths, force, ctx, pool);
  /* These structs have the same layout for the common fields. */
  *commit_info_p = (svn_client_commit_info_t *) commit_info;
  return svn_error_trace(err);
}

/*** From diff.c ***/

svn_error_t *
svn_client_diff5(const apr_array_header_t *diff_options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 const char *relative_to_dir,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t show_copies_as_adds,
                 svn_boolean_t ignore_content_type,
                 svn_boolean_t use_git_diff_format,
                 const char *header_encoding,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_stream_t *outstream = svn_stream_from_aprfile2(outfile, TRUE, pool);
  svn_stream_t *errstream = svn_stream_from_aprfile2(errfile, TRUE, pool);

  return svn_client_diff6(diff_options, path1, revision1, path2,
                          revision2, relative_to_dir, depth,
                          ignore_ancestry, FALSE /* no_diff_added */,
                          no_diff_deleted, show_copies_as_adds,
                          ignore_content_type, FALSE /* ignore_properties */,
                          FALSE /* properties_only */, use_git_diff_format,
                          header_encoding,
                          outstream, errstream, changelists, ctx, pool);
}

svn_error_t *
svn_client_diff4(const apr_array_header_t *options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 const char *relative_to_dir,
                 svn_depth_t depth,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t ignore_content_type,
                 const char *header_encoding,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_diff5(options, path1, revision1, path2,
                          revision2, relative_to_dir, depth,
                          ignore_ancestry, no_diff_deleted, FALSE,
                          ignore_content_type, FALSE, header_encoding,
                          outfile, errfile, changelists, ctx, pool);
}

svn_error_t *
svn_client_diff3(const apr_array_header_t *options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t ignore_content_type,
                 const char *header_encoding,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_diff4(options, path1, revision1, path2,
                          revision2, NULL,
                          SVN_DEPTH_INFINITY_OR_FILES(recurse),
                          ignore_ancestry, no_diff_deleted,
                          ignore_content_type, header_encoding,
                          outfile, errfile, NULL, ctx, pool);
}

svn_error_t *
svn_client_diff2(const apr_array_header_t *options,
                 const char *path1,
                 const svn_opt_revision_t *revision1,
                 const char *path2,
                 const svn_opt_revision_t *revision2,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t no_diff_deleted,
                 svn_boolean_t ignore_content_type,
                 apr_file_t *outfile,
                 apr_file_t *errfile,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_diff3(options, path1, revision1, path2, revision2,
                          recurse, ignore_ancestry, no_diff_deleted,
                          ignore_content_type, SVN_APR_LOCALE_CHARSET,
                          outfile, errfile, ctx, pool);
}

svn_error_t *
svn_client_diff(const apr_array_header_t *options,
                const char *path1,
                const svn_opt_revision_t *revision1,
                const char *path2,
                const svn_opt_revision_t *revision2,
                svn_boolean_t recurse,
                svn_boolean_t ignore_ancestry,
                svn_boolean_t no_diff_deleted,
                apr_file_t *outfile,
                apr_file_t *errfile,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_diff2(options, path1, revision1, path2, revision2,
                          recurse, ignore_ancestry, no_diff_deleted, FALSE,
                          outfile, errfile, ctx, pool);
}

svn_error_t *
svn_client_diff_peg5(const apr_array_header_t *diff_options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     const char *relative_to_dir,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t show_copies_as_adds,
                     svn_boolean_t ignore_content_type,
                     svn_boolean_t use_git_diff_format,
                     const char *header_encoding,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     const apr_array_header_t *changelists,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  svn_stream_t *outstream = svn_stream_from_aprfile2(outfile, TRUE, pool);
  svn_stream_t *errstream = svn_stream_from_aprfile2(errfile, TRUE, pool);

  return svn_client_diff_peg6(diff_options,
                              path,
                              peg_revision,
                              start_revision,
                              end_revision,
                              relative_to_dir,
                              depth,
                              ignore_ancestry,
                              FALSE /* no_diff_added */,
                              no_diff_deleted,
                              show_copies_as_adds,
                              ignore_content_type,
                              FALSE /* ignore_properties */,
                              FALSE /* properties_only */,
                              use_git_diff_format,
                              header_encoding,
                              outstream,
                              errstream,
                              changelists,
                              ctx,
                              pool);
}

svn_error_t *
svn_client_diff_peg4(const apr_array_header_t *options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     const char *relative_to_dir,
                     svn_depth_t depth,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t ignore_content_type,
                     const char *header_encoding,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     const apr_array_header_t *changelists,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client_diff_peg5(options,
                              path,
                              peg_revision,
                              start_revision,
                              end_revision,
                              relative_to_dir,
                              depth,
                              ignore_ancestry,
                              no_diff_deleted,
                              FALSE,
                              ignore_content_type,
                              FALSE,
                              header_encoding,
                              outfile,
                              errfile,
                              changelists,
                              ctx,
                              pool);
}

svn_error_t *
svn_client_diff_peg3(const apr_array_header_t *options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t ignore_content_type,
                     const char *header_encoding,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client_diff_peg4(options,
                              path,
                              peg_revision,
                              start_revision,
                              end_revision,
                              NULL,
                              SVN_DEPTH_INFINITY_OR_FILES(recurse),
                              ignore_ancestry,
                              no_diff_deleted,
                              ignore_content_type,
                              header_encoding,
                              outfile,
                              errfile,
                              NULL,
                              ctx,
                              pool);
}

svn_error_t *
svn_client_diff_peg2(const apr_array_header_t *options,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *start_revision,
                     const svn_opt_revision_t *end_revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t no_diff_deleted,
                     svn_boolean_t ignore_content_type,
                     apr_file_t *outfile,
                     apr_file_t *errfile,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client_diff_peg3(options, path, peg_revision, start_revision,
                              end_revision,
                              SVN_DEPTH_INFINITY_OR_FILES(recurse),
                              ignore_ancestry, no_diff_deleted,
                              ignore_content_type, SVN_APR_LOCALE_CHARSET,
                              outfile, errfile, ctx, pool);
}

svn_error_t *
svn_client_diff_peg(const apr_array_header_t *options,
                    const char *path,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *start_revision,
                    const svn_opt_revision_t *end_revision,
                    svn_boolean_t recurse,
                    svn_boolean_t ignore_ancestry,
                    svn_boolean_t no_diff_deleted,
                    apr_file_t *outfile,
                    apr_file_t *errfile,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  return svn_client_diff_peg2(options, path, peg_revision,
                              start_revision, end_revision, recurse,
                              ignore_ancestry, no_diff_deleted, FALSE,
                              outfile, errfile, ctx, pool);
}

svn_error_t *
svn_client_diff_summarize(const char *path1,
                          const svn_opt_revision_t *revision1,
                          const char *path2,
                          const svn_opt_revision_t *revision2,
                          svn_boolean_t recurse,
                          svn_boolean_t ignore_ancestry,
                          svn_client_diff_summarize_func_t summarize_func,
                          void *summarize_baton,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool)
{
  return svn_client_diff_summarize2(path1, revision1, path2,
                                    revision2,
                                    SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                    ignore_ancestry, NULL, summarize_func,
                                    summarize_baton, ctx, pool);
}

svn_error_t *
svn_client_diff_summarize_peg(const char *path,
                              const svn_opt_revision_t *peg_revision,
                              const svn_opt_revision_t *start_revision,
                              const svn_opt_revision_t *end_revision,
                              svn_boolean_t recurse,
                              svn_boolean_t ignore_ancestry,
                              svn_client_diff_summarize_func_t summarize_func,
                              void *summarize_baton,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool)
{
  return svn_client_diff_summarize_peg2(path, peg_revision,
                                        start_revision, end_revision,
                                        SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                        ignore_ancestry, NULL,
                                        summarize_func, summarize_baton,
                                        ctx, pool);
}

/*** From export.c ***/
svn_error_t *
svn_client_export4(svn_revnum_t *result_rev,
                   const char *from_path_or_url,
                   const char *to_path,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t overwrite,
                   svn_boolean_t ignore_externals,
                   svn_depth_t depth,
                   const char *native_eol,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_export5(result_rev, from_path_or_url, to_path,
                            peg_revision, revision, overwrite, ignore_externals,
                            FALSE, depth, native_eol, ctx, pool);
}

svn_error_t *
svn_client_export3(svn_revnum_t *result_rev,
                   const char *from_path_or_url,
                   const char *to_path,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t overwrite,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t recurse,
                   const char *native_eol,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_export4(result_rev, from_path_or_url, to_path,
                            peg_revision, revision, overwrite, ignore_externals,
                            SVN_DEPTH_INFINITY_OR_FILES(recurse),
                            native_eol, ctx, pool);
}

svn_error_t *
svn_client_export2(svn_revnum_t *result_rev,
                   const char *from_path_or_url,
                   const char *to_path,
                   svn_opt_revision_t *revision,
                   svn_boolean_t force,
                   const char *native_eol,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;

  peg_revision.kind = svn_opt_revision_unspecified;

  return svn_client_export3(result_rev, from_path_or_url, to_path,
                            &peg_revision, revision, force, FALSE, TRUE,
                            native_eol, ctx, pool);
}


svn_error_t *
svn_client_export(svn_revnum_t *result_rev,
                  const char *from_path_or_url,
                  const char *to_path,
                  svn_opt_revision_t *revision,
                  svn_boolean_t force,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_export2(result_rev, from_path_or_url, to_path, revision,
                            force, NULL, ctx, pool);
}

/*** From list.c ***/

svn_error_t *
svn_client_list3(const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_depth_t depth,
                 apr_uint32_t dirent_fields,
                 svn_boolean_t fetch_locks,
                 svn_boolean_t include_externals,
                 svn_client_list_func2_t list_func,
                 void *baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_error_trace(svn_client_list4(path_or_url, peg_revision,
                                          revision, NULL, depth,
                                          dirent_fields, fetch_locks,
                                          include_externals,
                                          list_func, baton, ctx, pool));
}

/* Baton for use with wrap_list_func */
struct list_func_wrapper_baton {
    void *list_func1_baton;
    svn_client_list_func_t list_func1;
};

/* This implements svn_client_list_func2_t */
static svn_error_t *
list_func_wrapper(void *baton,
                  const char *path,
                  const svn_dirent_t *dirent,
                  const svn_lock_t *lock,
                  const char *abs_path,
                  const char *external_parent_url,
                  const char *external_target,
                  apr_pool_t *scratch_pool)
{
  struct list_func_wrapper_baton *lfwb = baton;

  if (lfwb->list_func1)
    return lfwb->list_func1(lfwb->list_func1_baton, path, dirent,
                           lock, abs_path, scratch_pool);

  return SVN_NO_ERROR;
}

/* Helper function for svn_client_list2().  It wraps old format baton
   and callback function in list_func_wrapper_baton and
   returns new baton and callback to use with svn_client_list3(). */
static void
wrap_list_func(svn_client_list_func2_t *list_func2,
               void **list_func2_baton,
               svn_client_list_func_t list_func,
               void *baton,
               apr_pool_t *result_pool)
{
  struct list_func_wrapper_baton *lfwb = apr_palloc(result_pool,
                                                    sizeof(*lfwb));

  /* Set the user provided old format callback in the baton. */
  lfwb->list_func1_baton = baton;
  lfwb->list_func1 = list_func;

  *list_func2_baton = lfwb;
  *list_func2 = list_func_wrapper;
}

svn_error_t *
svn_client_list2(const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_depth_t depth,
                 apr_uint32_t dirent_fields,
                 svn_boolean_t fetch_locks,
                 svn_client_list_func_t list_func,
                 void *baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_client_list_func2_t list_func2;
  void *list_func2_baton;

  wrap_list_func(&list_func2, &list_func2_baton, list_func, baton, pool);

  return svn_client_list3(path_or_url, peg_revision, revision, depth,
                          dirent_fields, fetch_locks,
                          FALSE /* include externals */,
                          list_func2, list_func2_baton, ctx, pool);
}

svn_error_t *
svn_client_list(const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_boolean_t recurse,
                apr_uint32_t dirent_fields,
                svn_boolean_t fetch_locks,
                svn_client_list_func_t list_func,
                void *baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_list2(path_or_url,
                          peg_revision,
                          revision,
                          SVN_DEPTH_INFINITY_OR_IMMEDIATES(recurse),
                          dirent_fields,
                          fetch_locks,
                          list_func,
                          baton,
                          ctx,
                          pool);
}

/* Baton used by compatibility wrapper svn_client_ls3. */
struct ls_baton {
  apr_hash_t *dirents;
  apr_hash_t *locks;
  apr_pool_t *pool;
};

/* This implements svn_client_list_func_t. */
static svn_error_t *
store_dirent(void *baton, const char *path, const svn_dirent_t *dirent,
             const svn_lock_t *lock, const char *abs_path, apr_pool_t *pool)
{
  struct ls_baton *lb = baton;

  /* The dirent is allocated in a temporary pool, so duplicate it into the
     correct pool.  Note, however, that the locks are stored in the correct
     pool already. */
  dirent = svn_dirent_dup(dirent, lb->pool);

  /* An empty path means we are called for the target of the operation.
     For compatibility, we only store the target if it is a file, and we
     store it under the basename of the URL.  Note that this makes it
     impossible to differentiate between the target being a directory with a
     child with the same basename as the target and the target being a file,
     but that's how it was implemented. */
  if (path[0] == '\0')
    {
      if (dirent->kind == svn_node_file)
        {
          const char *base_name = svn_path_basename(abs_path, lb->pool);
          svn_hash_sets(lb->dirents, base_name, dirent);
          if (lock)
            svn_hash_sets(lb->locks, base_name, lock);
        }
    }
  else
    {
      path = apr_pstrdup(lb->pool, path);
      svn_hash_sets(lb->dirents, path, dirent);
      if (lock)
        svn_hash_sets(lb->locks, path, lock);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_ls3(apr_hash_t **dirents,
               apr_hash_t **locks,
               const char *path_or_url,
               const svn_opt_revision_t *peg_revision,
               const svn_opt_revision_t *revision,
               svn_boolean_t recurse,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  struct ls_baton lb;

  *dirents = lb.dirents = apr_hash_make(pool);
  if (locks)
    *locks = lb.locks = apr_hash_make(pool);
  lb.pool = pool;

  return svn_client_list(path_or_url, peg_revision, revision, recurse,
                         SVN_DIRENT_ALL, locks != NULL,
                         store_dirent, &lb, ctx, pool);
}

svn_error_t *
svn_client_ls2(apr_hash_t **dirents,
               const char *path_or_url,
               const svn_opt_revision_t *peg_revision,
               const svn_opt_revision_t *revision,
               svn_boolean_t recurse,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{

  return svn_client_ls3(dirents, NULL, path_or_url, peg_revision,
                        revision, recurse, ctx, pool);
}


svn_error_t *
svn_client_ls(apr_hash_t **dirents,
              const char *path_or_url,
              svn_opt_revision_t *revision,
              svn_boolean_t recurse,
              svn_client_ctx_t *ctx,
              apr_pool_t *pool)
{
  return svn_client_ls2(dirents, path_or_url, revision,
                        revision, recurse, ctx, pool);
}

/*** From log.c ***/

svn_error_t *
svn_client_log4(const apr_array_header_t *targets,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_boolean_t include_merged_revisions,
                const apr_array_header_t *revprops,
                svn_log_entry_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  apr_array_header_t *revision_ranges;

  revision_ranges = apr_array_make(pool, 1,
                                   sizeof(svn_opt_revision_range_t *));
  APR_ARRAY_PUSH(revision_ranges, svn_opt_revision_range_t *)
    = svn_opt__revision_range_create(start, end, pool);

  return svn_client_log5(targets, peg_revision, revision_ranges, limit,
                         discover_changed_paths, strict_node_history,
                         include_merged_revisions, revprops, receiver,
                         receiver_baton, ctx, pool);
}


svn_error_t *
svn_client_log3(const apr_array_header_t *targets,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_log_entry_receiver_t receiver2;
  void *receiver2_baton;

  svn_compat_wrap_log_receiver(&receiver2, &receiver2_baton,
                               receiver, receiver_baton,
                               pool);

  return svn_client_log4(targets, peg_revision, start, end, limit,
                         discover_changed_paths, strict_node_history, FALSE,
                         svn_compat_log_revprops_in(pool),
                         receiver2, receiver2_baton, ctx, pool);
}

svn_error_t *
svn_client_log2(const apr_array_header_t *targets,
                const svn_opt_revision_t *start,
                const svn_opt_revision_t *end,
                int limit,
                svn_boolean_t discover_changed_paths,
                svn_boolean_t strict_node_history,
                svn_log_message_receiver_t receiver,
                void *receiver_baton,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;
  peg_revision.kind = svn_opt_revision_unspecified;
  return svn_client_log3(targets, &peg_revision, start, end, limit,
                         discover_changed_paths, strict_node_history,
                         receiver, receiver_baton, ctx, pool);
}

svn_error_t *
svn_client_log(const apr_array_header_t *targets,
               const svn_opt_revision_t *start,
               const svn_opt_revision_t *end,
               svn_boolean_t discover_changed_paths,
               svn_boolean_t strict_node_history,
               svn_log_message_receiver_t receiver,
               void *receiver_baton,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;

  err = svn_client_log2(targets, start, end, 0, discover_changed_paths,
                        strict_node_history, receiver, receiver_baton, ctx,
                        pool);

  /* Special case: If there have been no commits, we'll get an error
   * for requesting log of a revision higher than 0.  But the
   * default behavior of "svn log" is to give revisions HEAD through
   * 1, on the assumption that HEAD >= 1.
   *
   * So if we got that error for that reason, and it looks like the
   * user was just depending on the defaults (rather than explicitly
   * requesting the log for revision 1), then we don't error.  Instead
   * we just invoke the receiver manually on a hand-constructed log
   * message for revision 0.
   *
   * See also http://subversion.tigris.org/issues/show_bug.cgi?id=692.
   */
  if (err && (err->apr_err == SVN_ERR_FS_NO_SUCH_REVISION)
      && (start->kind == svn_opt_revision_head)
      && ((end->kind == svn_opt_revision_number)
          && (end->value.number == 1)))
    {

      /* We don't need to check if HEAD is 0, because that must be the case,
       * by logical deduction: The revision range specified is HEAD:1.
       * HEAD cannot not exist, so the revision to which "no such revision"
       * applies is 1. If revision 1 does not exist, then HEAD is 0.
       * Hence, we deduce the repository is empty without needing access
       * to further information. */

      svn_error_clear(err);
      err = SVN_NO_ERROR;

      /* Log receivers are free to handle revision 0 specially... But
         just in case some don't, we make up a message here. */
      SVN_ERR(receiver(receiver_baton,
                       NULL, 0, "", "", _("No commits in repository"),
                       pool));
    }

  return svn_error_trace(err);
}

/*** From merge.c ***/

svn_error_t *
svn_client_merge4(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_depth_t depth,
                  svn_boolean_t ignore_ancestry,
                  svn_boolean_t force_delete,
                  svn_boolean_t record_only,
                  svn_boolean_t dry_run,
                  svn_boolean_t allow_mixed_rev,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  SVN_ERR(svn_client_merge5(source1, revision1,
                            source2, revision2,
                            target_wcpath,
                            depth,
                            ignore_ancestry /*ignore_mergeinfo*/,
                            ignore_ancestry /*diff_ignore_ancestry*/,
                            force_delete, record_only,
                            dry_run, allow_mixed_rev,
                            merge_options, ctx, pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_merge3(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_depth_t depth,
                  svn_boolean_t ignore_ancestry,
                  svn_boolean_t force,
                  svn_boolean_t record_only,
                  svn_boolean_t dry_run,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_merge4(source1, revision1, source2, revision2,
                           target_wcpath, depth, ignore_ancestry, force,
                           record_only, dry_run, TRUE, merge_options,
                           ctx, pool);
}

svn_error_t *
svn_client_merge2(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_boolean_t recurse,
                  svn_boolean_t ignore_ancestry,
                  svn_boolean_t force,
                  svn_boolean_t dry_run,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_merge3(source1, revision1, source2, revision2,
                           target_wcpath,
                           SVN_DEPTH_INFINITY_OR_FILES(recurse),
                           ignore_ancestry, force, FALSE, dry_run,
                           merge_options, ctx, pool);
}

svn_error_t *
svn_client_merge(const char *source1,
                 const svn_opt_revision_t *revision1,
                 const char *source2,
                 const svn_opt_revision_t *revision2,
                 const char *target_wcpath,
                 svn_boolean_t recurse,
                 svn_boolean_t ignore_ancestry,
                 svn_boolean_t force,
                 svn_boolean_t dry_run,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_client_merge2(source1, revision1, source2, revision2,
                           target_wcpath, recurse, ignore_ancestry, force,
                           dry_run, NULL, ctx, pool);
}

svn_error_t *
svn_client_merge_peg4(const char *source_path_or_url,
                      const apr_array_header_t *ranges_to_merge,
                      const svn_opt_revision_t *source_peg_revision,
                      const char *target_wcpath,
                      svn_depth_t depth,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t force_delete,
                      svn_boolean_t record_only,
                      svn_boolean_t dry_run,
                      svn_boolean_t allow_mixed_rev,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  SVN_ERR(svn_client_merge_peg5(source_path_or_url,
                                ranges_to_merge,
                                source_peg_revision,
                                target_wcpath,
                                depth,
                                ignore_ancestry /*ignore_mergeinfo*/,
                                ignore_ancestry /*diff_ignore_ancestry*/,
                                force_delete, record_only,
                                dry_run, allow_mixed_rev,
                                merge_options, ctx, pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_merge_peg3(const char *source,
                      const apr_array_header_t *ranges_to_merge,
                      const svn_opt_revision_t *peg_revision,
                      const char *target_wcpath,
                      svn_depth_t depth,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t force,
                      svn_boolean_t record_only,
                      svn_boolean_t dry_run,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  return svn_client_merge_peg4(source, ranges_to_merge, peg_revision,
                               target_wcpath, depth, ignore_ancestry, force,
                               record_only, dry_run, TRUE, merge_options,
                               ctx, pool);
}

svn_error_t *
svn_client_merge_peg2(const char *source,
                      const svn_opt_revision_t *revision1,
                      const svn_opt_revision_t *revision2,
                      const svn_opt_revision_t *peg_revision,
                      const char *target_wcpath,
                      svn_boolean_t recurse,
                      svn_boolean_t ignore_ancestry,
                      svn_boolean_t force,
                      svn_boolean_t dry_run,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  apr_array_header_t *ranges_to_merge =
    apr_array_make(pool, 1, sizeof(svn_opt_revision_range_t *));

  APR_ARRAY_PUSH(ranges_to_merge, svn_opt_revision_range_t *)
    = svn_opt__revision_range_create(revision1, revision2, pool);
  return svn_client_merge_peg3(source, ranges_to_merge,
                               peg_revision,
                               target_wcpath,
                               SVN_DEPTH_INFINITY_OR_FILES(recurse),
                               ignore_ancestry, force, FALSE, dry_run,
                               merge_options, ctx, pool);
}

svn_error_t *
svn_client_merge_peg(const char *source,
                     const svn_opt_revision_t *revision1,
                     const svn_opt_revision_t *revision2,
                     const svn_opt_revision_t *peg_revision,
                     const char *target_wcpath,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_ancestry,
                     svn_boolean_t force,
                     svn_boolean_t dry_run,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_client_merge_peg2(source, revision1, revision2, peg_revision,
                               target_wcpath, recurse, ignore_ancestry, force,
                               dry_run, NULL, ctx, pool);
}

/*** From prop_commands.c ***/
svn_error_t *
svn_client_propset3(svn_commit_info_t **commit_info_p,
                    const char *propname,
                    const svn_string_t *propval,
                    const char *target,
                    svn_depth_t depth,
                    svn_boolean_t skip_checks,
                    svn_revnum_t base_revision_for_url,
                    const apr_array_header_t *changelists,
                    const apr_hash_t *revprop_table,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  if (svn_path_is_url(target))
    {
      struct capture_baton_t cb;

      cb.info = commit_info_p;
      cb.pool = pool;

      SVN_ERR(svn_client_propset_remote(propname, propval, target, skip_checks,
                                        base_revision_for_url, revprop_table,
                                        capture_commit_info, &cb, ctx, pool));
    }
  else
    {
      apr_array_header_t *targets = apr_array_make(pool, 1,
                                                   sizeof(const char *));

      APR_ARRAY_PUSH(targets, const char *) = target;
      SVN_ERR(svn_client_propset_local(propname, propval, targets, depth,
                                       skip_checks, changelists, ctx, pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_propset2(const char *propname,
                    const svn_string_t *propval,
                    const char *target,
                    svn_boolean_t recurse,
                    svn_boolean_t skip_checks,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  return svn_client_propset3(NULL, propname, propval, target,
                             SVN_DEPTH_INFINITY_OR_EMPTY(recurse),
                             skip_checks, SVN_INVALID_REVNUM,
                             NULL, NULL, ctx, pool);
}


svn_error_t *
svn_client_propset(const char *propname,
                   const svn_string_t *propval,
                   const char *target,
                   svn_boolean_t recurse,
                   apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;

  SVN_ERR(svn_client_create_context(&ctx, pool));

  return svn_client_propset2(propname, propval, target, recurse, FALSE,
                             ctx, pool);
}

svn_error_t *
svn_client_revprop_set(const char *propname,
                       const svn_string_t *propval,
                       const char *URL,
                       const svn_opt_revision_t *revision,
                       svn_revnum_t *set_rev,
                       svn_boolean_t force,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  return svn_client_revprop_set2(propname, propval, NULL, URL,
                                 revision, set_rev, force, ctx, pool);

}

svn_error_t *
svn_client_propget4(apr_hash_t **props,
                    const char *propname,
                    const char *target,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    svn_revnum_t *actual_revnum,
                    svn_depth_t depth,
                    const apr_array_header_t *changelists,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_client_propget5(props, NULL, propname, target,
                                             peg_revision, revision,
                                             actual_revnum, depth,
                                             changelists, ctx,
                                             result_pool, scratch_pool));
}

svn_error_t *
svn_client_propget3(apr_hash_t **props,
                    const char *propname,
                    const char *path_or_url,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    svn_revnum_t *actual_revnum,
                    svn_depth_t depth,
                    const apr_array_header_t *changelists,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  const char *target;
  apr_hash_t *temp_props;
  svn_error_t *err;

  if (svn_path_is_url(path_or_url))
    target = path_or_url;
  else
    SVN_ERR(svn_dirent_get_absolute(&target, path_or_url, pool));

  err = svn_client_propget4(&temp_props, propname, target,
                            peg_revision, revision, actual_revnum,
                            depth, changelists, ctx, pool, pool);

  if (err && err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE)
    {
      err->apr_err = SVN_ERR_ENTRY_NOT_FOUND;
      return svn_error_trace(err);
    }
  else
    SVN_ERR(err);

  if (actual_revnum
        && !svn_path_is_url(path_or_url)
        && !SVN_IS_VALID_REVNUM(*actual_revnum))
    {
      /* Get the actual_revnum; added nodes have no revision yet, and old
       * callers expected the mock-up revision of 0. */
      svn_boolean_t added;

      SVN_ERR(svn_wc__node_is_added(&added, ctx->wc_ctx, target, pool));
      if (added)
        *actual_revnum = 0;
    }

  /* We may need to fix up our hash keys for legacy callers. */
  if (!svn_path_is_url(path_or_url) && strcmp(target, path_or_url) != 0)
    {
      apr_hash_index_t *hi;

      *props = apr_hash_make(pool);
      for (hi = apr_hash_first(pool, temp_props); hi;
            hi = apr_hash_next(hi))
        {
          const char *abspath = apr_hash_this_key(hi);
          svn_string_t *value = apr_hash_this_val(hi);
          const char *relpath = svn_dirent_join(path_or_url,
                                     svn_dirent_skip_ancestor(target, abspath),
                                     pool);

          svn_hash_sets(*props, relpath, value);
        }
    }
  else
    *props = temp_props;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_propget2(apr_hash_t **props,
                    const char *propname,
                    const char *target,
                    const svn_opt_revision_t *peg_revision,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  return svn_client_propget3(props,
                             propname,
                             target,
                             peg_revision,
                             revision,
                             NULL,
                             SVN_DEPTH_INFINITY_OR_EMPTY(recurse),
                             NULL,
                             ctx,
                             pool);
}

svn_error_t *
svn_client_propget(apr_hash_t **props,
                   const char *propname,
                   const char *target,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_propget2(props, propname, target, revision, revision,
                             recurse, ctx, pool);
}


/* Duplicate a HASH containing (char * -> svn_string_t *) key/value
   pairs using POOL. */
static apr_hash_t *
string_hash_dup(apr_hash_t *hash, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *new_hash = apr_hash_make(pool);

  for (hi = apr_hash_first(pool, hash); hi; hi = apr_hash_next(hi))
    {
      const char *key = apr_pstrdup(pool, apr_hash_this_key(hi));
      apr_ssize_t klen = apr_hash_this_key_len(hi);
      svn_string_t *val = svn_string_dup(apr_hash_this_val(hi), pool);

      apr_hash_set(new_hash, key, klen, val);
    }
  return new_hash;
}

svn_client_proplist_item_t *
svn_client_proplist_item_dup(const svn_client_proplist_item_t *item,
                             apr_pool_t * pool)
{
  svn_client_proplist_item_t *new_item = apr_pcalloc(pool, sizeof(*new_item));

  if (item->node_name)
    new_item->node_name = svn_stringbuf_dup(item->node_name, pool);

  if (item->prop_hash)
    new_item->prop_hash = string_hash_dup(item->prop_hash, pool);

  return new_item;
}

/* Baton for use with wrap_proplist_receiver */
struct proplist_receiver_wrapper_baton {
  void *baton;
  svn_proplist_receiver_t receiver;
};

/* This implements svn_client_proplist_receiver2_t */
static svn_error_t *
proplist_wrapper_receiver(void *baton,
                          const char *path,
                          apr_hash_t *prop_hash,
                          apr_array_header_t *inherited_props,
                          apr_pool_t *pool)
{
  struct proplist_receiver_wrapper_baton *plrwb = baton;

  if (plrwb->receiver)
    return plrwb->receiver(plrwb->baton, path, prop_hash, pool);

  return SVN_NO_ERROR;
}

static void
wrap_proplist_receiver(svn_proplist_receiver2_t *receiver2,
                       void **receiver2_baton,
                       svn_proplist_receiver_t receiver,
                       void *receiver_baton,
                       apr_pool_t *pool)
{
  struct proplist_receiver_wrapper_baton *plrwb = apr_palloc(pool,
                                                             sizeof(*plrwb));

  /* Set the user provided old format callback in the baton. */
  plrwb->baton = receiver_baton;
  plrwb->receiver = receiver;

  *receiver2_baton = plrwb;
  *receiver2 = proplist_wrapper_receiver;
}

svn_error_t *
svn_client_proplist3(const char *target,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_depth_t depth,
                     const apr_array_header_t *changelists,
                     svn_proplist_receiver_t receiver,
                     void *receiver_baton,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{

  svn_proplist_receiver2_t receiver2;
  void *receiver2_baton;

  wrap_proplist_receiver(&receiver2, &receiver2_baton, receiver, receiver_baton,
                         pool);

  return svn_error_trace(svn_client_proplist4(target, peg_revision, revision,
                                              depth, changelists, FALSE,
                                              receiver2, receiver2_baton,
                                              ctx, pool));
}

/* Receiver baton used by proplist2() */
struct proplist_receiver_baton {
  apr_array_header_t *props;
  apr_pool_t *pool;
};

/* Receiver function used by proplist2(). */
static svn_error_t *
proplist_receiver_cb(void *baton,
                     const char *path,
                     apr_hash_t *prop_hash,
                     apr_pool_t *pool)
{
  struct proplist_receiver_baton *pl_baton =
    (struct proplist_receiver_baton *) baton;
  svn_client_proplist_item_t *tmp_item = apr_palloc(pool, sizeof(*tmp_item));
  svn_client_proplist_item_t *item;

  /* Because the pool passed to the receiver function is likely to be a
     temporary pool of some kind, we need to make copies of *path and
     *prop_hash in the pool provided by the baton. */
  tmp_item->node_name = svn_stringbuf_create(path, pl_baton->pool);
  tmp_item->prop_hash = prop_hash;

  item = svn_client_proplist_item_dup(tmp_item, pl_baton->pool);

  APR_ARRAY_PUSH(pl_baton->props, const svn_client_proplist_item_t *) = item;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_proplist2(apr_array_header_t **props,
                     const char *target,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_boolean_t recurse,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  struct proplist_receiver_baton pl_baton;

  *props = apr_array_make(pool, 5, sizeof(svn_client_proplist_item_t *));
  pl_baton.props = *props;
  pl_baton.pool = pool;

  return svn_client_proplist3(target, peg_revision, revision,
                              SVN_DEPTH_INFINITY_OR_EMPTY(recurse), NULL,
                              proplist_receiver_cb, &pl_baton, ctx, pool);
}


svn_error_t *
svn_client_proplist(apr_array_header_t **props,
                    const char *target,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  return svn_client_proplist2(props, target, revision, revision,
                              recurse, ctx, pool);
}

/*** From status.c ***/

svn_error_t *
svn_client_status5(svn_revnum_t *result_rev,
                   svn_client_ctx_t *ctx,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t depth_as_sticky,
                   const apr_array_header_t *changelists,
                   svn_client_status_func_t status_func,
                   void *status_baton,
                   apr_pool_t *scratch_pool)
{
  return svn_client_status6(result_rev, ctx, path, revision, depth,
                            get_all, update, TRUE, no_ignore,
                            ignore_externals, depth_as_sticky, changelists,
                            status_func, status_baton, scratch_pool);
}

struct status4_wrapper_baton
{
  svn_wc_context_t *wc_ctx;
  svn_wc_status_func3_t old_func;
  void *old_baton;
};

/* Implements svn_client_status_func_t */
static svn_error_t *
status4_wrapper_func(void *baton,
                     const char *path,
                     const svn_client_status_t *status,
                     apr_pool_t *scratch_pool)
{
  struct status4_wrapper_baton *swb = baton;
  svn_wc_status2_t *dup;
  const char *local_abspath;
  const svn_wc_status3_t *wc_status = status->backwards_compatibility_baton;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));
  SVN_ERR(svn_wc__status2_from_3(&dup, wc_status, swb->wc_ctx,
                                 local_abspath, scratch_pool,
                                 scratch_pool));

  return (*swb->old_func)(swb->old_baton, path, dup, scratch_pool);
}

svn_error_t *
svn_client_status4(svn_revnum_t *result_rev,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_wc_status_func3_t status_func,
                   void *status_baton,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   const apr_array_header_t *changelists,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  struct status4_wrapper_baton swb;

  swb.wc_ctx = ctx->wc_ctx;
  swb.old_func = status_func;
  swb.old_baton = status_baton;

  return svn_client_status5(result_rev, ctx, path, revision, depth, get_all,
                            update, no_ignore, ignore_externals, TRUE,
                            changelists, status4_wrapper_func, &swb, pool);
}

struct status3_wrapper_baton
{
  svn_wc_status_func2_t old_func;
  void *old_baton;
};

static svn_error_t *
status3_wrapper_func(void *baton,
                     const char *path,
                     svn_wc_status2_t *status,
                     apr_pool_t *pool)
{
  struct status3_wrapper_baton *swb = baton;

  swb->old_func(swb->old_baton, path, status);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_status3(svn_revnum_t *result_rev,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_wc_status_func2_t status_func,
                   void *status_baton,
                   svn_depth_t depth,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   const apr_array_header_t *changelists,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  struct status3_wrapper_baton swb = { 0 };
  swb.old_func = status_func;
  swb.old_baton = status_baton;
  return svn_client_status4(result_rev, path, revision, status3_wrapper_func,
                            &swb, depth, get_all, update, no_ignore,
                            ignore_externals, changelists, ctx, pool);
}

svn_error_t *
svn_client_status2(svn_revnum_t *result_rev,
                   const char *path,
                   const svn_opt_revision_t *revision,
                   svn_wc_status_func2_t status_func,
                   void *status_baton,
                   svn_boolean_t recurse,
                   svn_boolean_t get_all,
                   svn_boolean_t update,
                   svn_boolean_t no_ignore,
                   svn_boolean_t ignore_externals,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_status3(result_rev, path, revision,
                            status_func, status_baton,
                            SVN_DEPTH_INFINITY_OR_IMMEDIATES(recurse),
                            get_all, update, no_ignore, ignore_externals, NULL,
                            ctx, pool);
}


/* Baton for old_status_func_cb; does what you think it does. */
struct old_status_func_cb_baton
{
  svn_wc_status_func_t original_func;
  void *original_baton;
};

/* Help svn_client_status() accept an old-style status func and baton,
   by wrapping them before passing along to svn_client_status2().

   This implements the 'svn_wc_status_func2_t' function type. */
static void old_status_func_cb(void *baton,
                               const char *path,
                               svn_wc_status2_t *status)
{
  struct old_status_func_cb_baton *b = baton;
  svn_wc_status_t *stat = (svn_wc_status_t *) status;

  b->original_func(b->original_baton, path, stat);
}


svn_error_t *
svn_client_status(svn_revnum_t *result_rev,
                  const char *path,
                  svn_opt_revision_t *revision,
                  svn_wc_status_func_t status_func,
                  void *status_baton,
                  svn_boolean_t recurse,
                  svn_boolean_t get_all,
                  svn_boolean_t update,
                  svn_boolean_t no_ignore,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  struct old_status_func_cb_baton *b = apr_pcalloc(pool, sizeof(*b));
  b->original_func = status_func;
  b->original_baton = status_baton;

  return svn_client_status2(result_rev, path, revision,
                            old_status_func_cb, b,
                            recurse, get_all, update, no_ignore, FALSE,
                            ctx, pool);
}

/*** From update.c ***/
svn_error_t *
svn_client_update3(apr_array_header_t **result_revs,
                   const apr_array_header_t *paths,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t depth_is_sticky,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t allow_unver_obstructions,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_update4(result_revs, paths, revision,
                            depth, depth_is_sticky, ignore_externals,
                            allow_unver_obstructions, TRUE, FALSE,
                            ctx, pool);
}

svn_error_t *
svn_client_update2(apr_array_header_t **result_revs,
                   const apr_array_header_t *paths,
                   const svn_opt_revision_t *revision,
                   svn_boolean_t recurse,
                   svn_boolean_t ignore_externals,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_update3(result_revs, paths, revision,
                            SVN_DEPTH_INFINITY_OR_FILES(recurse), FALSE,
                            ignore_externals, FALSE, ctx, pool);
}

svn_error_t *
svn_client_update(svn_revnum_t *result_rev,
                  const char *path,
                  const svn_opt_revision_t *revision,
                  svn_boolean_t recurse,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  apr_array_header_t *paths = apr_array_make(pool, 1, sizeof(const char *));
  apr_array_header_t *result_revs;

  APR_ARRAY_PUSH(paths, const char *) = path;

  SVN_ERR(svn_client_update2(&result_revs, paths, revision, recurse, FALSE,
                             ctx, pool));

  *result_rev = APR_ARRAY_IDX(result_revs, 0, svn_revnum_t);

  return SVN_NO_ERROR;
}

/*** From switch.c ***/
svn_error_t *
svn_client_switch2(svn_revnum_t *result_rev,
                   const char *path,
                   const char *switch_url,
                   const svn_opt_revision_t *peg_revision,
                   const svn_opt_revision_t *revision,
                   svn_depth_t depth,
                   svn_boolean_t depth_is_sticky,
                   svn_boolean_t ignore_externals,
                   svn_boolean_t allow_unver_obstructions,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_client_switch3(result_rev, path, switch_url, peg_revision,
                            revision, depth, depth_is_sticky, ignore_externals,
                            allow_unver_obstructions,
                            TRUE /* ignore_ancestry */,
                            ctx, pool);
}

svn_error_t *
svn_client_switch(svn_revnum_t *result_rev,
                  const char *path,
                  const char *switch_url,
                  const svn_opt_revision_t *revision,
                  svn_boolean_t recurse,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;
  peg_revision.kind = svn_opt_revision_unspecified;
  return svn_client_switch2(result_rev, path, switch_url,
                            &peg_revision, revision,
                            SVN_DEPTH_INFINITY_OR_FILES(recurse),
                            FALSE, FALSE, FALSE, ctx, pool);
}

/*** From cat.c ***/
svn_error_t *
svn_client_cat2(svn_stream_t *out,
                const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_cat3(NULL /* props */,
                         out, path_or_url, peg_revision, revision,
                         TRUE /* expand_keywords */,
                         ctx, pool, pool);
}


svn_error_t *
svn_client_cat(svn_stream_t *out,
               const char *path_or_url,
               const svn_opt_revision_t *revision,
               svn_client_ctx_t *ctx,
               apr_pool_t *pool)
{
  return svn_client_cat2(out, path_or_url, revision, revision,
                         ctx, pool);
}

/*** From checkout.c ***/
svn_error_t *
svn_client_checkout2(svn_revnum_t *result_rev,
                     const char *URL,
                     const char *path,
                     const svn_opt_revision_t *peg_revision,
                     const svn_opt_revision_t *revision,
                     svn_boolean_t recurse,
                     svn_boolean_t ignore_externals,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  return svn_error_trace(svn_client_checkout3(result_rev, URL, path,
                                        peg_revision, revision,
                                        SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                        ignore_externals, FALSE, ctx, pool));
}

svn_error_t *
svn_client_checkout(svn_revnum_t *result_rev,
                    const char *URL,
                    const char *path,
                    const svn_opt_revision_t *revision,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  svn_opt_revision_t peg_revision;

  peg_revision.kind = svn_opt_revision_unspecified;

  return svn_error_trace(svn_client_checkout2(result_rev, URL, path,
                                              &peg_revision, revision, recurse,
                                              FALSE, ctx, pool));
}

/*** From info.c ***/

svn_error_t *
svn_client_info3(const char *abspath_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_depth_t depth,
                 svn_boolean_t fetch_excluded,
                 svn_boolean_t fetch_actual_only,
                 const apr_array_header_t *changelists,
                 svn_client_info_receiver2_t receiver,
                 void *receiver_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  return svn_error_trace(
            svn_client_info4(abspath_or_url,
                             peg_revision,
                             revision,
                             depth,
                             fetch_excluded,
                             fetch_actual_only,
                             FALSE /* include_externals */,
                             changelists,
                             receiver, receiver_baton,
                             ctx, pool));
}

svn_info_t *
svn_info_dup(const svn_info_t *info, apr_pool_t *pool)
{
  svn_info_t *dupinfo = apr_palloc(pool, sizeof(*dupinfo));

  /* Perform a trivial copy ... */
  *dupinfo = *info;

  /* ...and then re-copy stuff that needs to be duped into our pool. */
  if (info->URL)
    dupinfo->URL = apr_pstrdup(pool, info->URL);
  if (info->repos_root_URL)
    dupinfo->repos_root_URL = apr_pstrdup(pool, info->repos_root_URL);
  if (info->repos_UUID)
    dupinfo->repos_UUID = apr_pstrdup(pool, info->repos_UUID);
  if (info->last_changed_author)
    dupinfo->last_changed_author = apr_pstrdup(pool,
                                               info->last_changed_author);
  if (info->lock)
    dupinfo->lock = svn_lock_dup(info->lock, pool);
  if (info->copyfrom_url)
    dupinfo->copyfrom_url = apr_pstrdup(pool, info->copyfrom_url);
  if (info->checksum)
    dupinfo->checksum = apr_pstrdup(pool, info->checksum);
  if (info->conflict_old)
    dupinfo->conflict_old = apr_pstrdup(pool, info->conflict_old);
  if (info->conflict_new)
    dupinfo->conflict_new = apr_pstrdup(pool, info->conflict_new);
  if (info->conflict_wrk)
    dupinfo->conflict_wrk = apr_pstrdup(pool, info->conflict_wrk);
  if (info->prejfile)
    dupinfo->prejfile = apr_pstrdup(pool, info->prejfile);

  return dupinfo;
}

/* Convert an svn_client_info2_t to an svn_info_t, doing shallow copies. */
static svn_error_t *
info_from_info2(svn_info_t **new_info,
                svn_wc_context_t *wc_ctx,
                const svn_client_info2_t *info2,
                apr_pool_t *pool)
{
  svn_info_t *info = apr_pcalloc(pool, sizeof(*info));

  info->URL                 = info2->URL;
  /* Goofy backward compat handling for added nodes. */
  if (SVN_IS_VALID_REVNUM(info2->rev))
    info->rev               = info2->rev;
  else
    info->rev               = 0;

  info->kind                = info2->kind;
  info->repos_root_URL      = info2->repos_root_URL;
  info->repos_UUID          = info2->repos_UUID;
  info->last_changed_rev    = info2->last_changed_rev;
  info->last_changed_date   = info2->last_changed_date;
  info->last_changed_author = info2->last_changed_author;

  /* Stupid old structure has a non-const LOCK member. Sigh.  */
  info->lock                = (svn_lock_t *)info2->lock;

  info->size64              = info2->size;
  if (info2->size == SVN_INVALID_FILESIZE)
    info->size               = SVN_INFO_SIZE_UNKNOWN;
  else if (((svn_filesize_t)(apr_size_t)info->size64) == info->size64)
    info->size               = (apr_size_t)info->size64;
  else /* >= 4GB */
    info->size               = SVN_INFO_SIZE_UNKNOWN;

  if (info2->wc_info)
    {
      info->has_wc_info         = TRUE;
      info->schedule            = info2->wc_info->schedule;
      info->copyfrom_url        = info2->wc_info->copyfrom_url;
      info->copyfrom_rev        = info2->wc_info->copyfrom_rev;
      info->text_time           = info2->wc_info->recorded_time;
      info->prop_time           = 0;
      if (info2->wc_info->checksum
            && info2->wc_info->checksum->kind == svn_checksum_md5)
        info->checksum          = svn_checksum_to_cstring(
                                        info2->wc_info->checksum, pool);
      else
        info->checksum          = NULL;
      info->changelist          = info2->wc_info->changelist;
      info->depth               = info2->wc_info->depth;

      if (info->depth == svn_depth_unknown && info->kind == svn_node_file)
        info->depth = svn_depth_infinity;

      info->working_size64      = info2->wc_info->recorded_size;
      if (((svn_filesize_t)(apr_size_t)info->working_size64) == info->working_size64)
        info->working_size       = (apr_size_t)info->working_size64;
      else /* >= 4GB */
        info->working_size       = SVN_INFO_SIZE_UNKNOWN;
    }
  else
    {
      info->has_wc_info           = FALSE;
      info->working_size          = SVN_INFO_SIZE_UNKNOWN;
      info->working_size64        = SVN_INVALID_FILESIZE;
      info->depth                 = svn_depth_unknown;
    }

  /* Populate conflict fields. */
  if (info2->wc_info && info2->wc_info->conflicts)
    {
      int i;

      for (i = 0; i < info2->wc_info->conflicts->nelts; i++)
        {
          const svn_wc_conflict_description2_t *conflict
                              = APR_ARRAY_IDX(info2->wc_info->conflicts, i,
                                    const svn_wc_conflict_description2_t *);

          /* ### Not really sure what we should do if we get multiple
             ### conflicts of the same type. */
          switch (conflict->kind)
            {
              case svn_wc_conflict_kind_tree:
                info->tree_conflict = svn_wc__cd2_to_cd(conflict, pool);
                break;

              case svn_wc_conflict_kind_text:
                info->conflict_old = conflict->base_abspath;
                info->conflict_new = conflict->my_abspath;
                info->conflict_wrk = conflict->their_abspath;
                break;

              case svn_wc_conflict_kind_property:
                info->prejfile = conflict->their_abspath;
                break;
            }
        }
    }

  if (info2->wc_info && info2->wc_info->checksum)
    {
      const svn_checksum_t *md5_checksum;

      SVN_ERR(svn_wc__node_get_md5_from_sha1(&md5_checksum,
                                             wc_ctx,
                                             info2->wc_info->wcroot_abspath,
                                             info2->wc_info->checksum,
                                             pool, pool));

      info->checksum = svn_checksum_to_cstring(md5_checksum, pool);
    }

  *new_info = info;
  return SVN_NO_ERROR;
}

struct info_to_relpath_baton
{
  const char *anchor_abspath;
  const char *anchor_relpath;
  svn_info_receiver_t info_receiver;
  void *info_baton;
  svn_wc_context_t *wc_ctx;
};

static svn_error_t *
info_receiver_relpath_wrapper(void *baton,
                              const char *abspath_or_url,
                              const svn_client_info2_t *info2,
                              apr_pool_t *scratch_pool)
{
  struct info_to_relpath_baton *rb = baton;
  const char *path = abspath_or_url;
  svn_info_t *info;

  if (rb->anchor_relpath &&
      svn_dirent_is_ancestor(rb->anchor_abspath, abspath_or_url))
    {
      path = svn_dirent_join(rb->anchor_relpath,
                             svn_dirent_skip_ancestor(rb->anchor_abspath,
                                                      abspath_or_url),
                             scratch_pool);
    }

  SVN_ERR(info_from_info2(&info, rb->wc_ctx, info2, scratch_pool));

  SVN_ERR(rb->info_receiver(rb->info_baton,
                            path,
                            info,
                            scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_info2(const char *path_or_url,
                 const svn_opt_revision_t *peg_revision,
                 const svn_opt_revision_t *revision,
                 svn_info_receiver_t receiver,
                 void *receiver_baton,
                 svn_depth_t depth,
                 const apr_array_header_t *changelists,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  struct info_to_relpath_baton rb;
  const char *abspath_or_url = path_or_url;

  rb.anchor_relpath = NULL;
  rb.info_receiver = receiver;
  rb.info_baton = receiver_baton;
  rb.wc_ctx = ctx->wc_ctx;

  if (!svn_path_is_url(path_or_url))
    {
      SVN_ERR(svn_dirent_get_absolute(&abspath_or_url, path_or_url, pool));
      rb.anchor_abspath = abspath_or_url;
      rb.anchor_relpath = path_or_url;
    }

  SVN_ERR(svn_client_info3(abspath_or_url,
                           peg_revision,
                           revision,
                           depth,
                           FALSE, TRUE,
                           changelists,
                           info_receiver_relpath_wrapper,
                           &rb,
                           ctx,
                           pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_info(const char *path_or_url,
                const svn_opt_revision_t *peg_revision,
                const svn_opt_revision_t *revision,
                svn_info_receiver_t receiver,
                void *receiver_baton,
                svn_boolean_t recurse,
                svn_client_ctx_t *ctx,
                apr_pool_t *pool)
{
  return svn_client_info2(path_or_url, peg_revision, revision,
                          receiver, receiver_baton,
                          SVN_DEPTH_INFINITY_OR_EMPTY(recurse),
                          NULL, ctx, pool);
}

/*** From resolved.c ***/
svn_error_t *
svn_client_resolved(const char *path,
                    svn_boolean_t recursive,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  svn_depth_t depth = SVN_DEPTH_INFINITY_OR_EMPTY(recursive);
  return svn_client_resolve(path, depth,
                            svn_wc_conflict_choose_merged, ctx, pool);
}
/*** From revert.c ***/
svn_error_t *
svn_client_revert2(const apr_array_header_t *paths,
                   svn_depth_t depth,
                   const apr_array_header_t *changelists,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  return svn_error_trace(svn_client_revert3(paths,
                                            depth,
                                            changelists,
                                            FALSE /* clear_changelists */,
                                            FALSE /* metadata_only */,
                                            ctx,
                                            pool));
}

svn_error_t *
svn_client_revert(const apr_array_header_t *paths,
                  svn_boolean_t recursive,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  return svn_client_revert2(paths, SVN_DEPTH_INFINITY_OR_EMPTY(recursive),
                            NULL, ctx, pool);
}

/*** From ra.c ***/
svn_error_t *
svn_client_open_ra_session(svn_ra_session_t **session,
                           const char *url,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  return svn_error_trace(
             svn_client_open_ra_session2(session, url,
                                         NULL, ctx,
                                         pool, pool));
}

svn_error_t *
svn_client_uuid_from_url(const char **uuid,
                         const char *url,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *pool)
{
  svn_error_t *err;
  apr_pool_t *subpool = svn_pool_create(pool);

  err = svn_client_get_repos_root(NULL, uuid, url,
                                  ctx, pool, subpool);
  /* destroy the RA session */
  svn_pool_destroy(subpool);

  return svn_error_trace(err);
}

svn_error_t *
svn_client_uuid_from_path2(const char **uuid,
                           const char *local_abspath,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  return svn_error_trace(
      svn_client_get_repos_root(NULL, uuid,
                                local_abspath, ctx,
                                result_pool, scratch_pool));
}

svn_error_t *
svn_client_uuid_from_path(const char **uuid,
                          const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *pool)
{
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  return svn_error_trace(
    svn_client_uuid_from_path2(uuid, local_abspath, ctx, pool, pool));
}

/*** From url.c ***/
svn_error_t *
svn_client_root_url_from_path(const char **url,
                              const char *path_or_url,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_error_t *err;
  if (!svn_path_is_url(path_or_url))
    SVN_ERR(svn_dirent_get_absolute(&path_or_url, path_or_url, pool));

  err = svn_client_get_repos_root(url, NULL, path_or_url,
                                  ctx, pool, subpool);

  /* close ra session */
  svn_pool_destroy(subpool);
  return svn_error_trace(err);
}

svn_error_t *
svn_client_url_from_path(const char **url,
                         const char *path_or_url,
                         apr_pool_t *pool)
{
  svn_client_ctx_t *ctx;

  SVN_ERR(svn_client_create_context(&ctx, pool));

  return svn_client_url_from_path2(url, path_or_url, ctx, pool, pool);
}

/*** From mergeinfo.c ***/
svn_error_t *
svn_client_mergeinfo_log(svn_boolean_t finding_merged,
                         const char *target_path_or_url,
                         const svn_opt_revision_t *target_peg_revision,
                         const char *source_path_or_url,
                         const svn_opt_revision_t *source_peg_revision,
                         svn_log_entry_receiver_t receiver,
                         void *receiver_baton,
                         svn_boolean_t discover_changed_paths,
                         svn_depth_t depth,
                         const apr_array_header_t *revprops,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool)
{
  svn_opt_revision_t start_revision, end_revision;

  start_revision.kind = svn_opt_revision_unspecified;
  end_revision.kind = svn_opt_revision_unspecified;

  return svn_client_mergeinfo_log2(finding_merged,
                                   target_path_or_url, target_peg_revision,
                                   source_path_or_url, source_peg_revision,
                                   &start_revision, &end_revision,
                                   receiver, receiver_baton,
                                   discover_changed_paths, depth, revprops,
                                   ctx, scratch_pool);
}

svn_error_t *
svn_client_mergeinfo_log_merged(const char *path_or_url,
                                const svn_opt_revision_t *peg_revision,
                                const char *merge_source_path_or_url,
                                const svn_opt_revision_t *src_peg_revision,
                                svn_log_entry_receiver_t log_receiver,
                                void *log_receiver_baton,
                                svn_boolean_t discover_changed_paths,
                                const apr_array_header_t *revprops,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *pool)
{
  return svn_client_mergeinfo_log(TRUE, path_or_url, peg_revision,
                                  merge_source_path_or_url,
                                  src_peg_revision,
                                  log_receiver, log_receiver_baton,
                                  discover_changed_paths,
                                  svn_depth_empty, revprops, ctx,
                                  pool);
}

svn_error_t *
svn_client_mergeinfo_log_eligible(const char *path_or_url,
                                  const svn_opt_revision_t *peg_revision,
                                  const char *merge_source_path_or_url,
                                  const svn_opt_revision_t *src_peg_revision,
                                  svn_log_entry_receiver_t log_receiver,
                                  void *log_receiver_baton,
                                  svn_boolean_t discover_changed_paths,
                                  const apr_array_header_t *revprops,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *pool)
{
  return svn_client_mergeinfo_log(FALSE, path_or_url, peg_revision,
                                  merge_source_path_or_url,
                                  src_peg_revision,
                                  log_receiver, log_receiver_baton,
                                  discover_changed_paths,
                                  svn_depth_empty, revprops, ctx,
                                  pool);
}

/*** From relocate.c ***/
svn_error_t *
svn_client_relocate(const char *path,
                    const char *from_prefix,
                    const char *to_prefix,
                    svn_boolean_t recurse,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  if (! recurse)
    SVN_ERR(svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("Non-recursive relocation not supported")));
  return svn_client_relocate2(path, from_prefix, to_prefix, TRUE, ctx, pool);
}

/*** From util.c ***/
svn_error_t *
svn_client_commit_item_create(const svn_client_commit_item3_t **item,
                              apr_pool_t *pool)
{
  *item = svn_client_commit_item3_create(pool);
  return SVN_NO_ERROR;
}

svn_client_commit_item2_t *
svn_client_commit_item2_dup(const svn_client_commit_item2_t *item,
                            apr_pool_t *pool)
{
  svn_client_commit_item2_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->path)
    new_item->path = apr_pstrdup(pool, new_item->path);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  if (new_item->copyfrom_url)
    new_item->copyfrom_url = apr_pstrdup(pool, new_item->copyfrom_url);

  if (new_item->wcprop_changes)
    new_item->wcprop_changes = svn_prop_array_dup(new_item->wcprop_changes,
                                                  pool);

  return new_item;
}

svn_error_t *
svn_client_cleanup(const char *path,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *scratch_pool)
{
  const char *local_abspath;

  if (svn_path_is_url(path))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"), path);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));

  return svn_error_trace(svn_client_cleanup2(local_abspath,
                                             TRUE /* break_locks */,
                                             TRUE /* fix_recorded_timestamps */,
                                             TRUE /* clear_dav_cache */,
                                             TRUE /* vacuum_pristines */,
                                             FALSE /* include_externals */,
                                             ctx, scratch_pool));
}
