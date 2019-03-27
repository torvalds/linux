/*
 * copy_foreign.c:  copy from other repository support.
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
#include "svn_hash.h"
#include "svn_client.h"
#include "svn_delta.h"
#include "svn_dirent_uri.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_path.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_ra.h"
#include "svn_wc.h"

#include <apr_md5.h>

#include "client.h"
#include "private/svn_subr_private.h"
#include "private/svn_wc_private.h"
#include "svn_private_config.h"

struct edit_baton_t
{
  apr_pool_t *pool;
  const char *anchor_abspath;

  svn_wc_context_t *wc_ctx;
  svn_wc_notify_func2_t notify_func;
  void *notify_baton;
};

struct dir_baton_t
{
  apr_pool_t *pool;

  struct dir_baton_t *pb;
  struct edit_baton_t *eb;

  const char *local_abspath;

  svn_boolean_t created;
  apr_hash_t *properties;

  int users;
};

/* svn_delta_editor_t function */
static svn_error_t *
edit_open(void *edit_baton,
          svn_revnum_t base_revision,
          apr_pool_t *result_pool,
          void **root_baton)
{
  struct edit_baton_t *eb = edit_baton;
  apr_pool_t *dir_pool = svn_pool_create(eb->pool);
  struct dir_baton_t *db = apr_pcalloc(dir_pool, sizeof(*db));

  db->pool = dir_pool;
  db->eb = eb;
  db->users = 1;
  db->local_abspath = eb->anchor_abspath;

  SVN_ERR(svn_io_make_dir_recursively(eb->anchor_abspath, dir_pool));

  *root_baton = db;

  return SVN_NO_ERROR;
}

/* svn_delta_editor_t function */
static svn_error_t *
edit_close(void *edit_baton,
           apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

static svn_error_t *
dir_add(const char *path,
        void *parent_baton,
        const char *copyfrom_path,
        svn_revnum_t copyfrom_revision,
        apr_pool_t *result_pool,
        void **child_baton)
{
  struct dir_baton_t *pb = parent_baton;
  struct edit_baton_t *eb = pb->eb;
  apr_pool_t *dir_pool = svn_pool_create(pb->pool);
  struct dir_baton_t *db = apr_pcalloc(dir_pool, sizeof(*db));
  svn_boolean_t under_root;

  pb->users++;

  db->pb = pb;
  db->eb = pb->eb;
  db->pool = dir_pool;
  db->users = 1;

  SVN_ERR(svn_dirent_is_under_root(&under_root, &db->local_abspath,
                                   eb->anchor_abspath, path, db->pool));
  if (! under_root)
    {
      return svn_error_createf(
                    SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                    _("Path '%s' is not in the working copy"),
                    svn_dirent_local_style(path, db->pool));
    }

  SVN_ERR(svn_io_make_dir_recursively(db->local_abspath, db->pool));

  *child_baton = db;
  return SVN_NO_ERROR;
}

static svn_error_t *
dir_change_prop(void *dir_baton,
                const char *name,
                const svn_string_t *value,
                apr_pool_t *scratch_pool)
{
  struct dir_baton_t *db = dir_baton;
  struct edit_baton_t *eb = db->eb;
  svn_prop_kind_t prop_kind;

  prop_kind = svn_property_kind2(name);

  if (prop_kind != svn_prop_regular_kind
      || ! strcmp(name, SVN_PROP_MERGEINFO))
    {
      /* We can't handle DAV, ENTRY and merge specific props here */
      return SVN_NO_ERROR;
    }

  if (! db->created)
    {
      /* We can still store them in the hash for immediate addition
         with the svn_wc_add_from_disk3() call */
      if (! db->properties)
        db->properties = apr_hash_make(db->pool);

      if (value != NULL)
        svn_hash_sets(db->properties, apr_pstrdup(db->pool, name),
                      svn_string_dup(value, db->pool));
    }
  else
    {
      /* We have already notified for this directory, so don't do that again */
      SVN_ERR(svn_wc_prop_set4(eb->wc_ctx, db->local_abspath, name, value,
                               svn_depth_empty, FALSE, NULL,
                               NULL, NULL, /* Cancellation */
                               NULL, NULL, /* Notification */
                               scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Releases the directory baton if there are no more users */
static svn_error_t *
maybe_done(struct dir_baton_t *db)
{
  db->users--;

  if (db->users == 0)
    {
      struct dir_baton_t *pb = db->pb;

      svn_pool_clear(db->pool);

      if (pb)
        SVN_ERR(maybe_done(pb));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
ensure_added(struct dir_baton_t *db,
             apr_pool_t *scratch_pool)
{
  if (db->created)
    return SVN_NO_ERROR;

  if (db->pb)
    SVN_ERR(ensure_added(db->pb, scratch_pool));

  db->created = TRUE;

  /* Add the directory with all the already collected properties */
  SVN_ERR(svn_wc_add_from_disk3(db->eb->wc_ctx,
                                db->local_abspath,
                                db->properties,
                                TRUE /* skip checks */,
                                db->eb->notify_func,
                                db->eb->notify_baton,
                                scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
dir_close(void *dir_baton,
          apr_pool_t *scratch_pool)
{
  struct dir_baton_t *db = dir_baton;
  /*struct edit_baton_t *eb = db->eb;*/

  SVN_ERR(ensure_added(db, scratch_pool));

  SVN_ERR(maybe_done(db));

  return SVN_NO_ERROR;
}

struct file_baton_t
{
  apr_pool_t *pool;

  struct dir_baton_t *pb;
  struct edit_baton_t *eb;

  const char *local_abspath;
  apr_hash_t *properties;

  svn_boolean_t writing;
  unsigned char digest[APR_MD5_DIGESTSIZE];

  const char *tmp_path;
};

static svn_error_t *
file_add(const char *path,
         void *parent_baton,
         const char *copyfrom_path,
         svn_revnum_t copyfrom_revision,
         apr_pool_t *result_pool,
         void **file_baton)
{
  struct dir_baton_t *pb = parent_baton;
  struct edit_baton_t *eb = pb->eb;
  apr_pool_t *file_pool = svn_pool_create(pb->pool);
  struct file_baton_t *fb = apr_pcalloc(file_pool, sizeof(*fb));
  svn_boolean_t under_root;

  pb->users++;

  fb->pool = file_pool;
  fb->eb = eb;
  fb->pb = pb;

  SVN_ERR(svn_dirent_is_under_root(&under_root, &fb->local_abspath,
                                   eb->anchor_abspath, path, fb->pool));
  if (! under_root)
    {
      return svn_error_createf(
                    SVN_ERR_WC_OBSTRUCTED_UPDATE, NULL,
                    _("Path '%s' is not in the working copy"),
                    svn_dirent_local_style(path, fb->pool));
    }

  *file_baton = fb;
  return SVN_NO_ERROR;
}

static svn_error_t *
file_change_prop(void *file_baton,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *scratch_pool)
{
  struct file_baton_t *fb = file_baton;
  svn_prop_kind_t prop_kind;

  prop_kind = svn_property_kind2(name);

  if (prop_kind != svn_prop_regular_kind
      || ! strcmp(name, SVN_PROP_MERGEINFO))
    {
      /* We can't handle DAV, ENTRY and merge specific props here */
      return SVN_NO_ERROR;
    }

  /* We store all properties in the hash for immediate addition
      with the svn_wc_add_from_disk3() call */
  if (! fb->properties)
    fb->properties = apr_hash_make(fb->pool);

  if (value != NULL)
    svn_hash_sets(fb->properties, apr_pstrdup(fb->pool, name),
                  svn_string_dup(value, fb->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
file_textdelta(void *file_baton,
               const char *base_checksum,
               apr_pool_t *result_pool,
               svn_txdelta_window_handler_t *handler,
               void **handler_baton)
{
  struct file_baton_t *fb = file_baton;
  svn_stream_t *target;

  SVN_ERR_ASSERT(! fb->writing);

  SVN_ERR(svn_stream_open_writable(&target, fb->local_abspath, fb->pool,
                                   fb->pool));

  fb->writing = TRUE;
  svn_txdelta_apply(svn_stream_empty(fb->pool) /* source */,
                    target,
                    fb->digest,
                    fb->local_abspath,
                    fb->pool,
                    /* Provide the handler directly */
                    handler, handler_baton);

  return SVN_NO_ERROR;
}

static svn_error_t *
file_close(void *file_baton,
           const char *text_checksum,
           apr_pool_t *scratch_pool)
{
  struct file_baton_t *fb = file_baton;
  struct edit_baton_t *eb = fb->eb;
  struct dir_baton_t *pb = fb->pb;

  SVN_ERR(ensure_added(pb, fb->pool));

  if (text_checksum)
    {
      svn_checksum_t *expected_checksum;
      svn_checksum_t *actual_checksum;

      SVN_ERR(svn_checksum_parse_hex(&expected_checksum, svn_checksum_md5,
                                     text_checksum, fb->pool));
      actual_checksum = svn_checksum__from_digest_md5(fb->digest, fb->pool);

      if (! svn_checksum_match(expected_checksum, actual_checksum))
        return svn_error_trace(
                    svn_checksum_mismatch_err(expected_checksum,
                                              actual_checksum,
                                              fb->pool,
                                         _("Checksum mismatch for '%s'"),
                                              svn_dirent_local_style(
                                                    fb->local_abspath,
                                                    fb->pool)));
    }

  SVN_ERR(svn_wc_add_from_disk3(eb->wc_ctx, fb->local_abspath, fb->properties,
                                TRUE /* skip checks */,
                                eb->notify_func, eb->notify_baton,
                                fb->pool));

  svn_pool_destroy(fb->pool);
  SVN_ERR(maybe_done(pb));

  return SVN_NO_ERROR;
}

static svn_error_t *
copy_foreign_dir(svn_ra_session_t *ra_session,
                 svn_client__pathrev_t *location,
                 svn_wc_context_t *wc_ctx,
                 const char *dst_abspath,
                 svn_depth_t depth,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  struct edit_baton_t eb;
  svn_delta_editor_t *editor = svn_delta_default_editor(scratch_pool);
  const svn_delta_editor_t *wrapped_editor;
  void *wrapped_baton;
  const svn_ra_reporter3_t *reporter;
  void *reporter_baton;

  eb.pool = scratch_pool;
  eb.anchor_abspath = dst_abspath;

  eb.wc_ctx = wc_ctx;
  eb.notify_func = notify_func;
  eb.notify_baton  = notify_baton;

  editor->open_root = edit_open;
  editor->close_edit = edit_close;

  editor->add_directory = dir_add;
  editor->change_dir_prop = dir_change_prop;
  editor->close_directory = dir_close;

  editor->add_file = file_add;
  editor->change_file_prop = file_change_prop;
  editor->apply_textdelta = file_textdelta;
  editor->close_file = file_close;

  SVN_ERR(svn_delta_get_cancellation_editor(cancel_func, cancel_baton,
                                            editor, &eb,
                                            &wrapped_editor, &wrapped_baton,
                                            scratch_pool));

  SVN_ERR(svn_ra_do_update3(ra_session, &reporter, &reporter_baton,
                            location->rev, "", svn_depth_infinity,
                            FALSE, FALSE, wrapped_editor, wrapped_baton,
                            scratch_pool, scratch_pool));

  SVN_ERR(reporter->set_path(reporter_baton, "", location->rev, depth,
                             TRUE /* incomplete */,
                             NULL, scratch_pool));

  SVN_ERR(reporter->finish_report(reporter_baton, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__copy_foreign(const char *url,
                         const char *dst_abspath,
                         svn_opt_revision_t *peg_revision,
                         svn_opt_revision_t *revision,
                         svn_depth_t depth,
                         svn_boolean_t make_parents,
                         svn_boolean_t already_locked,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;
  svn_client__pathrev_t *loc;
  svn_node_kind_t kind;
  svn_node_kind_t wc_kind;
  const char *dir_abspath;

  SVN_ERR_ASSERT(svn_path_is_url(url));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  /* Do we need to validate/update revisions? */

  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &loc,
                                            url, NULL,
                                            peg_revision,
                                            revision, ctx,
                                            scratch_pool));

  SVN_ERR(svn_ra_check_path(ra_session, "", loc->rev, &kind, scratch_pool));

  if (kind != svn_node_file && kind != svn_node_dir)
    return svn_error_createf(
                SVN_ERR_ILLEGAL_TARGET, NULL,
                _("'%s' is not a valid location inside a repository"),
                url);

  SVN_ERR(svn_wc_read_kind2(&wc_kind, ctx->wc_ctx, dst_abspath, FALSE, TRUE,
                            scratch_pool));

  if (wc_kind != svn_node_none)
    {
      return svn_error_createf(
                SVN_ERR_ENTRY_EXISTS, NULL,
                _("'%s' is already under version control"),
                svn_dirent_local_style(dst_abspath, scratch_pool));
    }

  dir_abspath = svn_dirent_dirname(dst_abspath, scratch_pool);
  SVN_ERR(svn_wc_read_kind2(&wc_kind, ctx->wc_ctx, dir_abspath,
                            FALSE, FALSE, scratch_pool));

  if (wc_kind == svn_node_none)
    {
      if (make_parents)
        SVN_ERR(svn_client__make_local_parents(dir_abspath, make_parents, ctx,
                                               scratch_pool));

      SVN_ERR(svn_wc_read_kind2(&wc_kind, ctx->wc_ctx, dir_abspath,
                                FALSE, FALSE, scratch_pool));
    }

  if (wc_kind != svn_node_dir)
    return svn_error_createf(
                SVN_ERR_ENTRY_NOT_FOUND, NULL,
                _("Can't add '%s', because no parent directory is found"),
                svn_dirent_local_style(dst_abspath, scratch_pool));


  if (kind == svn_node_file)
    {
      svn_stream_t *target;
      apr_hash_t *props;
      apr_hash_index_t *hi;
      SVN_ERR(svn_stream_open_writable(&target, dst_abspath, scratch_pool,
                                       scratch_pool));

      SVN_ERR(svn_ra_get_file(ra_session, "", loc->rev, target, NULL, &props,
                              scratch_pool));

      if (props != NULL)
        for (hi = apr_hash_first(scratch_pool, props); hi;
             hi = apr_hash_next(hi))
          {
            const char *name = apr_hash_this_key(hi);

            if (svn_property_kind2(name) != svn_prop_regular_kind
                || ! strcmp(name, SVN_PROP_MERGEINFO))
              {
                /* We can't handle DAV, ENTRY and merge specific props here */
                svn_hash_sets(props, name, NULL);
              }
          }

      if (!already_locked)
        SVN_WC__CALL_WITH_WRITE_LOCK(
              svn_wc_add_from_disk3(ctx->wc_ctx, dst_abspath, props,
                                    TRUE /* skip checks */,
                                    ctx->notify_func2, ctx->notify_baton2,
                                    scratch_pool),
              ctx->wc_ctx, dir_abspath, FALSE, scratch_pool);
      else
        SVN_ERR(svn_wc_add_from_disk3(ctx->wc_ctx, dst_abspath, props,
                                      TRUE /* skip checks */,
                                      ctx->notify_func2, ctx->notify_baton2,
                                      scratch_pool));
    }
  else
    {
      if (!already_locked)
        SVN_WC__CALL_WITH_WRITE_LOCK(
              copy_foreign_dir(ra_session, loc,
                               ctx->wc_ctx, dst_abspath,
                               depth,
                               ctx->notify_func2, ctx->notify_baton2,
                               ctx->cancel_func, ctx->cancel_baton,
                               scratch_pool),
              ctx->wc_ctx, dir_abspath, FALSE, scratch_pool);
      else
        SVN_ERR(copy_foreign_dir(ra_session, loc,
                                 ctx->wc_ctx, dst_abspath,
                                 depth,
                                 ctx->notify_func2, ctx->notify_baton2,
                                 ctx->cancel_func, ctx->cancel_baton,
                                 scratch_pool));
    }

  return SVN_NO_ERROR;
}
