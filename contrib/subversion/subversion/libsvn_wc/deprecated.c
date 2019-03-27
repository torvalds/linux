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

/* We define this here to remove any further warnings about the usage of
   deprecated functions in this file. */
#define SVN_DEPRECATED

#include <apr_md5.h>

#include "svn_wc.h"
#include "svn_subst.h"
#include "svn_pools.h"
#include "svn_props.h"
#include "svn_hash.h"
#include "svn_time.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"

#include "private/svn_subr_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_io_private.h"

#include "wc.h"
#include "entries.h"
#include "lock.h"
#include "props.h"
#include "translate.h"
#include "workqueue.h"

#include "svn_private_config.h"

/* baton for traversal_info_update */
struct traversal_info_update_baton
{
  struct svn_wc_traversal_info_t *traversal;
  svn_wc__db_t *db;
};

/* Helper for updating svn_wc_traversal_info_t structures
 * Implements svn_wc_external_update_t */
static svn_error_t *
traversal_info_update(void *baton,
                      const char *local_abspath,
                      const svn_string_t *old_val,
                      const svn_string_t *new_val,
                      svn_depth_t depth,
                      apr_pool_t *scratch_pool)
{
  const char *dup_path;
  svn_wc_adm_access_t *adm_access;
  struct traversal_info_update_baton *ub = baton;
  apr_pool_t *dup_pool = ub->traversal->pool;
  const char *dup_val = NULL;

  /* We make the abspath relative by retrieving the access baton
     for the specific directory */
  adm_access = svn_wc__adm_retrieve_internal2(ub->db, local_abspath,
                                              scratch_pool);

  if (adm_access)
    dup_path = apr_pstrdup(dup_pool, svn_wc_adm_access_path(adm_access));
  else
    dup_path = apr_pstrdup(dup_pool, local_abspath);

  if (old_val)
    {
      dup_val = apr_pstrmemdup(dup_pool, old_val->data, old_val->len);

      svn_hash_sets(ub->traversal->externals_old, dup_path, dup_val);
    }

  if (new_val)
    {
      /* In most cases the value is identical */
      if (old_val != new_val)
        dup_val = apr_pstrmemdup(dup_pool, new_val->data, new_val->len);

      svn_hash_sets(ub->traversal->externals_new, dup_path, dup_val);
    }

  svn_hash_sets(ub->traversal->depths, dup_path, svn_depth_to_word(depth));

  return SVN_NO_ERROR;
}

/* Helper for functions that used to gather traversal_info */
static svn_error_t *
gather_traversal_info(svn_wc_context_t *wc_ctx,
                      const char *local_abspath,
                      const char *path,
                      svn_depth_t depth,
                      struct svn_wc_traversal_info_t *traversal_info,
                      svn_boolean_t gather_as_old,
                      svn_boolean_t gather_as_new,
                      apr_pool_t *scratch_pool)
{
  apr_hash_t *externals;
  apr_hash_t *ambient_depths;
  apr_hash_index_t *hi;

  SVN_ERR(svn_wc__externals_gather_definitions(&externals, &ambient_depths,
                                               wc_ctx, local_abspath,
                                               depth,
                                               scratch_pool, scratch_pool));

  for (hi = apr_hash_first(scratch_pool, externals);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *node_abspath = apr_hash_this_key(hi);
      const char *relpath;

      relpath = svn_dirent_join(path,
                                svn_dirent_skip_ancestor(local_abspath,
                                                         node_abspath),
                                traversal_info->pool);

      if (gather_as_old)
        svn_hash_sets(traversal_info->externals_old, relpath,
                      apr_hash_this_val(hi));

      if (gather_as_new)
        svn_hash_sets(traversal_info->externals_new, relpath,
                      apr_hash_this_val(hi));

      svn_hash_sets(traversal_info->depths, relpath,
                    svn_hash_gets(ambient_depths, node_abspath));
    }

  return SVN_NO_ERROR;
}


/*** From adm_crawler.c ***/

svn_error_t *
svn_wc_crawl_revisions4(const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const svn_ra_reporter3_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_depth_t depth,
                        svn_boolean_t honor_depth_exclude,
                        svn_boolean_t depth_compatibility_trick,
                        svn_boolean_t use_commit_times,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        svn_wc_traversal_info_t *traversal_info,
                        apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *wc_db = svn_wc__adm_get_db(adm_access);
  const char *local_abspath;

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, wc_db, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(svn_wc_crawl_revisions5(wc_ctx,
                                  local_abspath,
                                  reporter,
                                  report_baton,
                                  restore_files,
                                  depth,
                                  honor_depth_exclude,
                                  depth_compatibility_trick,
                                  use_commit_times,
                                  NULL /* cancel_func */,
                                  NULL /* cancel_baton */,
                                  notify_func,
                                  notify_baton,
                                  pool));

  if (traversal_info)
    SVN_ERR(gather_traversal_info(wc_ctx, local_abspath, path, depth,
                                  traversal_info, TRUE, FALSE, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

/*** Compatibility wrapper: turns an svn_ra_reporter2_t into an
     svn_ra_reporter3_t.

     This code looks like it duplicates code in libsvn_ra/ra_loader.c,
     but it does not.  That code makes an new thing look like an old
     thing; this code makes an old thing look like a new thing. ***/

struct wrap_3to2_report_baton {
  const svn_ra_reporter2_t *reporter;
  void *baton;
};

/* */
static svn_error_t *wrap_3to2_set_path(void *report_baton,
                                       const char *path,
                                       svn_revnum_t revision,
                                       svn_depth_t depth,
                                       svn_boolean_t start_empty,
                                       const char *lock_token,
                                       apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->set_path(wrb->baton, path, revision, start_empty,
                                 lock_token, pool);
}

/* */
static svn_error_t *wrap_3to2_delete_path(void *report_baton,
                                          const char *path,
                                          apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->delete_path(wrb->baton, path, pool);
}

/* */
static svn_error_t *wrap_3to2_link_path(void *report_baton,
                                        const char *path,
                                        const char *url,
                                        svn_revnum_t revision,
                                        svn_depth_t depth,
                                        svn_boolean_t start_empty,
                                        const char *lock_token,
                                        apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->link_path(wrb->baton, path, url, revision,
                                  start_empty, lock_token, pool);
}

/* */
static svn_error_t *wrap_3to2_finish_report(void *report_baton,
                                            apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->finish_report(wrb->baton, pool);
}

/* */
static svn_error_t *wrap_3to2_abort_report(void *report_baton,
                                           apr_pool_t *pool)
{
  struct wrap_3to2_report_baton *wrb = report_baton;

  return wrb->reporter->abort_report(wrb->baton, pool);
}

static const svn_ra_reporter3_t wrap_3to2_reporter = {
  wrap_3to2_set_path,
  wrap_3to2_delete_path,
  wrap_3to2_link_path,
  wrap_3to2_finish_report,
  wrap_3to2_abort_report
};

svn_error_t *
svn_wc_crawl_revisions3(const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const svn_ra_reporter3_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_depth_t depth,
                        svn_boolean_t depth_compatibility_trick,
                        svn_boolean_t use_commit_times,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        svn_wc_traversal_info_t *traversal_info,
                        apr_pool_t *pool)
{
  return svn_wc_crawl_revisions4(path,
                                 adm_access,
                                 reporter, report_baton,
                                 restore_files,
                                 depth,
                                 FALSE,
                                 depth_compatibility_trick,
                                 use_commit_times,
                                 notify_func,
                                 notify_baton,
                                 traversal_info,
                                 pool);
}

svn_error_t *
svn_wc_crawl_revisions2(const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const svn_ra_reporter2_t *reporter,
                        void *report_baton,
                        svn_boolean_t restore_files,
                        svn_boolean_t recurse,
                        svn_boolean_t use_commit_times,
                        svn_wc_notify_func2_t notify_func,
                        void *notify_baton,
                        svn_wc_traversal_info_t *traversal_info,
                        apr_pool_t *pool)
{
  struct wrap_3to2_report_baton wrb;
  wrb.reporter = reporter;
  wrb.baton = report_baton;

  return svn_wc_crawl_revisions3(path,
                                 adm_access,
                                 &wrap_3to2_reporter, &wrb,
                                 restore_files,
                                 SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                 FALSE,
                                 use_commit_times,
                                 notify_func,
                                 notify_baton,
                                 traversal_info,
                                 pool);
}


/* Baton for compat_call_notify_func below.  */
struct compat_notify_baton_t {
  /* Wrapped func/baton. */
  svn_wc_notify_func_t func;
  void *baton;
};


/* Implements svn_wc_notify_func2_t.  Call BATON->func (BATON is of type
   svn_wc__compat_notify_baton_t), passing BATON->baton and the appropriate
   arguments from NOTIFY.  */
static void
compat_call_notify_func(void *baton,
                        const svn_wc_notify_t *n,
                        apr_pool_t *pool)
{
  struct compat_notify_baton_t *nb = baton;

  if (nb->func)
    (*nb->func)(nb->baton, n->path, n->action, n->kind, n->mime_type,
                n->content_state, n->prop_state, n->revision);
}


/*** Compatibility wrapper: turns an svn_ra_reporter_t into an
     svn_ra_reporter2_t.

     This code looks like it duplicates code in libsvn_ra/ra_loader.c,
     but it does not.  That code makes an new thing look like an old
     thing; this code makes an old thing look like a new thing. ***/

struct wrap_2to1_report_baton {
  const svn_ra_reporter_t *reporter;
  void *baton;
};

/* */
static svn_error_t *wrap_2to1_set_path(void *report_baton,
                                       const char *path,
                                       svn_revnum_t revision,
                                       svn_boolean_t start_empty,
                                       const char *lock_token,
                                       apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->set_path(wrb->baton, path, revision, start_empty,
                                 pool);
}

/* */
static svn_error_t *wrap_2to1_delete_path(void *report_baton,
                                          const char *path,
                                          apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->delete_path(wrb->baton, path, pool);
}

/* */
static svn_error_t *wrap_2to1_link_path(void *report_baton,
                                        const char *path,
                                        const char *url,
                                        svn_revnum_t revision,
                                        svn_boolean_t start_empty,
                                        const char *lock_token,
                                        apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->link_path(wrb->baton, path, url, revision,
                                  start_empty, pool);
}

/* */
static svn_error_t *wrap_2to1_finish_report(void *report_baton,
                                            apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->finish_report(wrb->baton, pool);
}

/* */
static svn_error_t *wrap_2to1_abort_report(void *report_baton,
                                           apr_pool_t *pool)
{
  struct wrap_2to1_report_baton *wrb = report_baton;

  return wrb->reporter->abort_report(wrb->baton, pool);
}

static const svn_ra_reporter2_t wrap_2to1_reporter = {
  wrap_2to1_set_path,
  wrap_2to1_delete_path,
  wrap_2to1_link_path,
  wrap_2to1_finish_report,
  wrap_2to1_abort_report
};

svn_error_t *
svn_wc_crawl_revisions(const char *path,
                       svn_wc_adm_access_t *adm_access,
                       const svn_ra_reporter_t *reporter,
                       void *report_baton,
                       svn_boolean_t restore_files,
                       svn_boolean_t recurse,
                       svn_boolean_t use_commit_times,
                       svn_wc_notify_func_t notify_func,
                       void *notify_baton,
                       svn_wc_traversal_info_t *traversal_info,
                       apr_pool_t *pool)
{
  struct wrap_2to1_report_baton wrb;
  struct compat_notify_baton_t nb;

  wrb.reporter = reporter;
  wrb.baton = report_baton;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_crawl_revisions2(path, adm_access, &wrap_2to1_reporter, &wrb,
                                 restore_files, recurse, use_commit_times,
                                 compat_call_notify_func, &nb,
                                 traversal_info,
                                 pool);
}

svn_error_t *
svn_wc_transmit_text_deltas2(const char **tempfile,
                             unsigned char digest[],
                             const char *path,
                             svn_wc_adm_access_t *adm_access,
                             svn_boolean_t fulltext,
                             const svn_delta_editor_t *editor,
                             void *file_baton,
                             apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;
  const svn_checksum_t *new_text_base_md5_checksum;
  svn_stream_t *tempstream;
  svn_error_t *err;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));
  if (tempfile)
    {
      apr_file_t *f;

      /* The temporary file can't be the same location as in 1.6 because the
       * admin directory no longer exists. */
      SVN_ERR(svn_io_open_unique_file3(&f, tempfile, NULL,
                                       svn_io_file_del_none,
                                       pool, pool));
      tempstream = svn_stream__from_aprfile(f, FALSE, TRUE, pool);
    }
  else
    {
      tempstream = NULL;
    }

  err = svn_wc__internal_transmit_text_deltas(svn_stream_disown(tempstream, pool),
                                              (digest
                                               ? &new_text_base_md5_checksum
                                               : NULL),
                                              NULL, wc_ctx->db,
                                              local_abspath, fulltext,
                                              editor, file_baton,
                                              pool, pool);
  if (tempfile)
    {
      err = svn_error_compose_create(err, svn_stream_close(tempstream));

      if (err)
        {
          err = svn_error_compose_create(
                  err, svn_io_remove_file2(*tempfile, TRUE, pool));
        }
    }

  SVN_ERR(err);

  if (digest)
    memcpy(digest, new_text_base_md5_checksum->digest, APR_MD5_DIGESTSIZE);

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_transmit_text_deltas(const char *path,
                            svn_wc_adm_access_t *adm_access,
                            svn_boolean_t fulltext,
                            const svn_delta_editor_t *editor,
                            void *file_baton,
                            const char **tempfile,
                            apr_pool_t *pool)
{
  return svn_wc_transmit_text_deltas2(tempfile, NULL, path, adm_access,
                                      fulltext, editor, file_baton, pool);
}

svn_error_t *
svn_wc_transmit_prop_deltas(const char *path,
                            svn_wc_adm_access_t *adm_access,
                            const svn_wc_entry_t *entry,
                            const svn_delta_editor_t *editor,
                            void *baton,
                            const char **tempfile,
                            apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;

  if (tempfile)
    *tempfile = NULL;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  SVN_ERR(svn_wc_transmit_prop_deltas2(wc_ctx, local_abspath, editor, baton,
                                       pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

/*** From adm_files.c ***/
svn_error_t *
svn_wc_ensure_adm3(const char *path,
                   const char *uuid,
                   const char *url,
                   const char *repos,
                   svn_revnum_t revision,
                   svn_depth_t depth,
                   apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;

  if (uuid == NULL)
    return svn_error_create(SVN_ERR_BAD_UUID, NULL, NULL);
  if (repos == NULL)
    return svn_error_create(SVN_ERR_BAD_URL, NULL, NULL);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc_context_create(&wc_ctx, NULL /* config */, pool, pool));

  SVN_ERR(svn_wc_ensure_adm4(wc_ctx, local_abspath, url, repos, uuid, revision,
                             depth, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_ensure_adm2(const char *path,
                   const char *uuid,
                   const char *url,
                   const char *repos,
                   svn_revnum_t revision,
                   apr_pool_t *pool)
{
  return svn_wc_ensure_adm3(path, uuid, url, repos, revision,
                            svn_depth_infinity, pool);
}


svn_error_t *
svn_wc_ensure_adm(const char *path,
                  const char *uuid,
                  const char *url,
                  svn_revnum_t revision,
                  apr_pool_t *pool)
{
  return svn_wc_ensure_adm2(path, uuid, url, NULL, revision, pool);
}

svn_error_t *
svn_wc_create_tmp_file(apr_file_t **fp,
                       const char *path,
                       svn_boolean_t delete_on_close,
                       apr_pool_t *pool)
{
  return svn_wc_create_tmp_file2(fp, NULL, path,
                                 delete_on_close
                                 ? svn_io_file_del_on_close
                                 : svn_io_file_del_none,
                                 pool);
}

svn_error_t *
svn_wc_create_tmp_file2(apr_file_t **fp,
                        const char **new_name,
                        const char *path,
                        svn_io_file_del_t delete_when,
                        apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;
  const char *temp_dir;
  svn_error_t *err;

  SVN_ERR_ASSERT(fp || new_name);

  SVN_ERR(svn_wc_context_create(&wc_ctx, NULL /* config */, pool, pool));

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  err = svn_wc__get_tmpdir(&temp_dir, wc_ctx, local_abspath, pool, pool);
  err = svn_error_compose_create(err, svn_wc_context_destroy(wc_ctx));
  if (err)
    return svn_error_trace(err);

  SVN_ERR(svn_io_open_unique_file3(fp, new_name, temp_dir,
                                   delete_when, pool, pool));

  return SVN_NO_ERROR;
}


/*** From adm_ops.c ***/
svn_error_t *
svn_wc_get_pristine_contents(svn_stream_t **contents,
                             const char *path,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;

  SVN_ERR(svn_wc_context_create(&wc_ctx, NULL, scratch_pool, scratch_pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));

  SVN_ERR(svn_wc_get_pristine_contents2(contents,
                                        wc_ctx,
                                        local_abspath,
                                        result_pool,
                                        scratch_pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_queue_committed3(svn_wc_committed_queue_t *queue,
                        svn_wc_context_t *wc_ctx,
                        const char *local_abspath,
                        svn_boolean_t recurse,
                        const apr_array_header_t *wcprop_changes,
                        svn_boolean_t remove_lock,
                        svn_boolean_t remove_changelist,
                        const svn_checksum_t *sha1_checksum,
                        apr_pool_t *scratch_pool)
{
  return svn_error_trace(
            svn_wc_queue_committed4(queue, wc_ctx, local_abspath,
                                    recurse, TRUE /* is_committed */,
                                    wcprop_changes, remove_lock,
                                    remove_changelist, sha1_checksum,
                                    scratch_pool));
}

svn_error_t *
svn_wc_queue_committed2(svn_wc_committed_queue_t *queue,
                        const char *path,
                        svn_wc_adm_access_t *adm_access,
                        svn_boolean_t recurse,
                        const apr_array_header_t *wcprop_changes,
                        svn_boolean_t remove_lock,
                        svn_boolean_t remove_changelist,
                        const svn_checksum_t *md5_checksum,
                        apr_pool_t *scratch_pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;
  const svn_checksum_t *sha1_checksum = NULL;

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL,
                                         svn_wc__adm_get_db(adm_access),
                                         scratch_pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));

  if (md5_checksum != NULL)
    {
      svn_error_t *err;
      err = svn_wc__db_pristine_get_sha1(&sha1_checksum, wc_ctx->db,
                                         local_abspath, md5_checksum,
                                         svn_wc__get_committed_queue_pool(queue),
                                         scratch_pool);

      /* Don't fail on SHA1 not found */
      if (err && err->apr_err == SVN_ERR_WC_DB_ERROR)
        {
          svn_error_clear(err);
          sha1_checksum = NULL;
        }
      else
        SVN_ERR(err);
    }

  SVN_ERR(svn_wc_queue_committed3(queue, wc_ctx, local_abspath, recurse,
                                  wcprop_changes,
                                  remove_lock, remove_changelist,
                                  sha1_checksum, scratch_pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_queue_committed(svn_wc_committed_queue_t **queue,
                       const char *path,
                       svn_wc_adm_access_t *adm_access,
                       svn_boolean_t recurse,
                       const apr_array_header_t *wcprop_changes,
                       svn_boolean_t remove_lock,
                       svn_boolean_t remove_changelist,
                       const unsigned char *digest,
                       apr_pool_t *pool)
{
  const svn_checksum_t *md5_checksum;

  if (digest)
    md5_checksum = svn_checksum__from_digest_md5(
                     digest, svn_wc__get_committed_queue_pool(*queue));
  else
    md5_checksum = NULL;

  return svn_wc_queue_committed2(*queue, path, adm_access, recurse,
                                 wcprop_changes, remove_lock,
                                 remove_changelist, md5_checksum, pool);
}

svn_error_t *
svn_wc_process_committed_queue(svn_wc_committed_queue_t *queue,
                               svn_wc_adm_access_t *adm_access,
                               svn_revnum_t new_revnum,
                               const char *rev_date,
                               const char *rev_author,
                               apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));
  SVN_ERR(svn_wc_process_committed_queue2(queue, wc_ctx, new_revnum,
                                          rev_date, rev_author,
                                          NULL, NULL, pool));
  SVN_ERR(svn_wc_context_destroy(wc_ctx));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_process_committed4(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t recurse,
                          svn_revnum_t new_revnum,
                          const char *rev_date,
                          const char *rev_author,
                          const apr_array_header_t *wcprop_changes,
                          svn_boolean_t remove_lock,
                          svn_boolean_t remove_changelist,
                          const unsigned char *digest,
                          apr_pool_t *pool)
{
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
  const char *local_abspath;
  const svn_checksum_t *md5_checksum;
  const svn_checksum_t *sha1_checksum = NULL;
  svn_wc_context_t *wc_ctx;
  svn_wc_committed_queue_t *queue;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, db, pool));

  if (digest)
    md5_checksum = svn_checksum__from_digest_md5(digest, pool);
  else
    md5_checksum = NULL;

  if (md5_checksum != NULL)
    {
      svn_error_t *err;
      err = svn_wc__db_pristine_get_sha1(&sha1_checksum, db,
                                         local_abspath, md5_checksum,
                                         pool, pool);

      if (err && err->apr_err == SVN_ERR_WC_DB_ERROR)
        {
          svn_error_clear(err);
          sha1_checksum = NULL;
        }
      else
        SVN_ERR(err);
    }

  queue = svn_wc_committed_queue_create(pool);
  SVN_ERR(svn_wc_queue_committed3(queue, wc_ctx, local_abspath, recurse,
                                  wcprop_changes, remove_lock,
                                  remove_changelist,
                                  sha1_checksum /* or NULL if not modified
                                                           or directory */,
                                  pool));

  SVN_ERR(svn_wc_process_committed_queue2(queue, wc_ctx,
                                          new_revnum, rev_date, rev_author,
                                          NULL, NULL /* cancel */,
                                          pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}


svn_error_t *
svn_wc_process_committed3(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t recurse,
                          svn_revnum_t new_revnum,
                          const char *rev_date,
                          const char *rev_author,
                          const apr_array_header_t *wcprop_changes,
                          svn_boolean_t remove_lock,
                          const unsigned char *digest,
                          apr_pool_t *pool)
{
  return svn_wc_process_committed4(path, adm_access, recurse, new_revnum,
                                   rev_date, rev_author, wcprop_changes,
                                   remove_lock, FALSE, digest, pool);
}

svn_error_t *
svn_wc_process_committed2(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t recurse,
                          svn_revnum_t new_revnum,
                          const char *rev_date,
                          const char *rev_author,
                          const apr_array_header_t *wcprop_changes,
                          svn_boolean_t remove_lock,
                          apr_pool_t *pool)
{
  return svn_wc_process_committed3(path, adm_access, recurse, new_revnum,
                                   rev_date, rev_author, wcprop_changes,
                                   remove_lock, NULL, pool);
}

svn_error_t *
svn_wc_process_committed(const char *path,
                         svn_wc_adm_access_t *adm_access,
                         svn_boolean_t recurse,
                         svn_revnum_t new_revnum,
                         const char *rev_date,
                         const char *rev_author,
                         const apr_array_header_t *wcprop_changes,
                         apr_pool_t *pool)
{
  return svn_wc_process_committed2(path, adm_access, recurse, new_revnum,
                                   rev_date, rev_author, wcprop_changes,
                                   FALSE, pool);
}

svn_error_t *
svn_wc_maybe_set_repos_root(svn_wc_adm_access_t *adm_access,
                            const char *path,
                            const char *repos,
                            apr_pool_t *pool)
{
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_delete3(const char *path,
               svn_wc_adm_access_t *adm_access,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               svn_boolean_t keep_local,
               apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *wc_db = svn_wc__adm_get_db(adm_access);
  svn_wc_adm_access_t *dir_access;
  const char *local_abspath;

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, wc_db, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  /* Open access batons for everything below path, because we used to open
     these before. */
  SVN_ERR(svn_wc_adm_probe_try3(&dir_access, adm_access, path,
                                TRUE, -1, cancel_func, cancel_baton, pool));

  SVN_ERR(svn_wc_delete4(wc_ctx,
                         local_abspath,
                         keep_local,
                         TRUE,
                         cancel_func, cancel_baton,
                         notify_func, notify_baton,
                         pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_delete2(const char *path,
               svn_wc_adm_access_t *adm_access,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  return svn_wc_delete3(path, adm_access, cancel_func, cancel_baton,
                        notify_func, notify_baton, FALSE, pool);
}

svn_error_t *
svn_wc_delete(const char *path,
              svn_wc_adm_access_t *adm_access,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              svn_wc_notify_func_t notify_func,
              void *notify_baton,
              apr_pool_t *pool)
{
  struct compat_notify_baton_t nb;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_delete2(path, adm_access, cancel_func, cancel_baton,
                        compat_call_notify_func, &nb, pool);
}

svn_error_t *
svn_wc_add_from_disk2(svn_wc_context_t *wc_ctx,
                     const char *local_abspath,
                     const apr_hash_t *props,
                     svn_wc_notify_func2_t notify_func,
                     void *notify_baton,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc_add_from_disk3(wc_ctx, local_abspath, props, FALSE,
                                 notify_func, notify_baton, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_add_from_disk(svn_wc_context_t *wc_ctx,
                     const char *local_abspath,
                     svn_wc_notify_func2_t notify_func,
                     void *notify_baton,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc_add_from_disk2(wc_ctx, local_abspath, NULL,
                                 notify_func, notify_baton, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_add3(const char *path,
            svn_wc_adm_access_t *parent_access,
            svn_depth_t depth,
            const char *copyfrom_url,
            svn_revnum_t copyfrom_rev,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func2_t notify_func,
            void *notify_baton,
            apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *wc_db = svn_wc__adm_get_db(parent_access);
  const char *local_abspath;

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, wc_db, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(svn_wc_add4(wc_ctx, local_abspath,
                      depth, copyfrom_url,
                      copyfrom_rev,
                      cancel_func, cancel_baton,
                      notify_func, notify_baton, pool));

  /* Make sure the caller gets the new access baton in the set. */
  if (svn_wc__adm_retrieve_internal2(wc_db, local_abspath, pool) == NULL)
    {
      svn_node_kind_t kind;

      SVN_ERR(svn_wc__db_read_kind(&kind, wc_db, local_abspath,
                                   FALSE /* allow_missing */,
                                   TRUE /* show_deleted */,
                                   FALSE /* show_hidden */, pool));
      if (kind == svn_node_dir)
        {
          svn_wc_adm_access_t *adm_access;

          /* Open the access baton in adm_access' pool to give it the same
             lifetime */
          SVN_ERR(svn_wc_adm_open3(&adm_access, parent_access, path, TRUE,
                                   copyfrom_url ? -1 : 0,
                                   cancel_func, cancel_baton,
                                   svn_wc_adm_access_pool(parent_access)));
        }
    }

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}


svn_error_t *
svn_wc_add2(const char *path,
            svn_wc_adm_access_t *parent_access,
            const char *copyfrom_url,
            svn_revnum_t copyfrom_rev,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func2_t notify_func,
            void *notify_baton,
            apr_pool_t *pool)
{
  return svn_wc_add3(path, parent_access, svn_depth_infinity,
                     copyfrom_url, copyfrom_rev,
                     cancel_func, cancel_baton,
                     notify_func, notify_baton, pool);
}

svn_error_t *
svn_wc_add(const char *path,
           svn_wc_adm_access_t *parent_access,
           const char *copyfrom_url,
           svn_revnum_t copyfrom_rev,
           svn_cancel_func_t cancel_func,
           void *cancel_baton,
           svn_wc_notify_func_t notify_func,
           void *notify_baton,
           apr_pool_t *pool)
{
  struct compat_notify_baton_t nb;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_add2(path, parent_access, copyfrom_url, copyfrom_rev,
                     cancel_func, cancel_baton,
                     compat_call_notify_func, &nb, pool);
}

/*** From revert.c ***/
svn_error_t *
svn_wc_revert4(svn_wc_context_t *wc_ctx,
               const char *local_abspath,
               svn_depth_t depth,
               svn_boolean_t use_commit_times,
               const apr_array_header_t *changelist_filter,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc_revert5(wc_ctx, local_abspath,
                                        depth,
                                        use_commit_times,
                                        changelist_filter,
                                        FALSE /* clear_changelists */,
                                        FALSE /* metadata_only */,
                                        cancel_func, cancel_baton,
                                        notify_func, notify_baton,
                                        scratch_pool));
}

svn_error_t *
svn_wc_revert3(const char *path,
               svn_wc_adm_access_t *parent_access,
               svn_depth_t depth,
               svn_boolean_t use_commit_times,
               const apr_array_header_t *changelist_filter,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *wc_db = svn_wc__adm_get_db(parent_access);
  const char *local_abspath;

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, wc_db, pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(svn_wc_revert4(wc_ctx,
                         local_abspath,
                         depth,
                         use_commit_times,
                         changelist_filter,
                         cancel_func, cancel_baton,
                         notify_func, notify_baton,
                         pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_revert2(const char *path,
               svn_wc_adm_access_t *parent_access,
               svn_boolean_t recursive,
               svn_boolean_t use_commit_times,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               svn_wc_notify_func2_t notify_func,
               void *notify_baton,
               apr_pool_t *pool)
{
  return svn_wc_revert3(path, parent_access,
                        SVN_DEPTH_INFINITY_OR_EMPTY(recursive),
                        use_commit_times, NULL, cancel_func, cancel_baton,
                        notify_func, notify_baton, pool);
}

svn_error_t *
svn_wc_revert(const char *path,
              svn_wc_adm_access_t *parent_access,
              svn_boolean_t recursive,
              svn_boolean_t use_commit_times,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              svn_wc_notify_func_t notify_func,
              void *notify_baton,
              apr_pool_t *pool)
{
  struct compat_notify_baton_t nb;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_revert2(path, parent_access, recursive, use_commit_times,
                        cancel_func, cancel_baton,
                        compat_call_notify_func, &nb, pool);
}

svn_error_t *
svn_wc_remove_from_revision_control(svn_wc_adm_access_t *adm_access,
                                    const char *name,
                                    svn_boolean_t destroy_wf,
                                    svn_boolean_t instant_error,
                                    svn_cancel_func_t cancel_func,
                                    void *cancel_baton,
                                    apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *wc_db = svn_wc__adm_get_db(adm_access);
  const char *local_abspath = svn_dirent_join(
                                    svn_wc__adm_access_abspath(adm_access),
                                    name,
                                    pool);

  /* name must be an entry in adm_access, fail if not */
  SVN_ERR_ASSERT(strcmp(svn_dirent_basename(name, NULL), name) == 0);
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, wc_db, pool));

  SVN_ERR(svn_wc_remove_from_revision_control2(wc_ctx,
                                               local_abspath,
                                               destroy_wf,
                                               instant_error,
                                               cancel_func, cancel_baton,
                                               pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_resolved_conflict4(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t resolve_text,
                          svn_boolean_t resolve_props,
                          svn_boolean_t resolve_tree,
                          svn_depth_t depth,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *wc_db = svn_wc__adm_get_db(adm_access);
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, wc_db, pool));

  SVN_ERR(svn_wc_resolved_conflict5(wc_ctx,
                                    local_abspath,
                                    depth,
                                    resolve_text,
                                    resolve_props ? "" : NULL,
                                    resolve_tree,
                                    conflict_choice,
                                    cancel_func,
                                    cancel_baton,
                                    notify_func,
                                    notify_baton,
                                    pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));

}

svn_error_t *
svn_wc_resolved_conflict(const char *path,
                         svn_wc_adm_access_t *adm_access,
                         svn_boolean_t resolve_text,
                         svn_boolean_t resolve_props,
                         svn_boolean_t recurse,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         apr_pool_t *pool)
{
  struct compat_notify_baton_t nb;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_resolved_conflict2(path, adm_access,
                                   resolve_text, resolve_props, recurse,
                                   compat_call_notify_func, &nb,
                                   NULL, NULL, pool);

}

svn_error_t *
svn_wc_resolved_conflict2(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t resolve_text,
                          svn_boolean_t resolve_props,
                          svn_boolean_t recurse,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *pool)
{
  return svn_wc_resolved_conflict3(path, adm_access, resolve_text,
                                   resolve_props,
                                   SVN_DEPTH_INFINITY_OR_EMPTY(recurse),
                                   svn_wc_conflict_choose_merged,
                                   notify_func, notify_baton, cancel_func,
                                   cancel_baton, pool);
}

svn_error_t *
svn_wc_resolved_conflict3(const char *path,
                          svn_wc_adm_access_t *adm_access,
                          svn_boolean_t resolve_text,
                          svn_boolean_t resolve_props,
                          svn_depth_t depth,
                          svn_wc_conflict_choice_t conflict_choice,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *pool)
{
  return svn_wc_resolved_conflict4(path, adm_access, resolve_text,
                                   resolve_props, FALSE, depth,
                                   svn_wc_conflict_choose_merged,
                                   notify_func, notify_baton, cancel_func,
                                   cancel_baton, pool);
}

svn_error_t *
svn_wc_add_lock(const char *path,
                const svn_lock_t *lock,
                svn_wc_adm_access_t *adm_access,
                apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  SVN_ERR(svn_wc_add_lock2(wc_ctx, local_abspath, lock, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_remove_lock(const char *path,
                   svn_wc_adm_access_t *adm_access,
                   apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  SVN_ERR(svn_wc_remove_lock2(wc_ctx, local_abspath, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));

}

svn_error_t *
svn_wc_get_ancestry(char **url,
                    svn_revnum_t *rev,
                    const char *path,
                    svn_wc_adm_access_t *adm_access,
                    apr_pool_t *pool)
{
  const char *local_abspath;
  const svn_wc_entry_t *entry;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(svn_wc__get_entry(&entry, svn_wc__adm_get_db(adm_access),
                            local_abspath, FALSE,
                            svn_node_unknown,
                            pool, pool));

  if (url)
    *url = apr_pstrdup(pool, entry->url);

  if (rev)
    *rev = entry->revision;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_set_changelist(const char *path,
                      const char *changelist,
                      svn_wc_adm_access_t *adm_access,
                      svn_cancel_func_t cancel_func,
                      void *cancel_baton,
                      svn_wc_notify_func2_t notify_func,
                      void *notify_baton,
                      apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  SVN_ERR(svn_wc_set_changelist2(wc_ctx, local_abspath, changelist,
                                 svn_depth_empty, NULL,
                                 cancel_func, cancel_baton, notify_func,
                                 notify_baton, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}


/*** From diff.c ***/
/* Used to wrap svn_wc_diff_callbacks_t. */
struct diff_callbacks_wrapper_baton {
  const svn_wc_diff_callbacks_t *callbacks;
  void *baton;
};

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
wrap_3to1_file_changed(svn_wc_adm_access_t *adm_access,
                       svn_wc_notify_state_t *contentstate,
                       svn_wc_notify_state_t *propstate,
                       svn_boolean_t *tree_conflicted,
                       const char *path,
                       const char *tmpfile1,
                       const char *tmpfile2,
                       svn_revnum_t rev1,
                       svn_revnum_t rev2,
                       const char *mimetype1,
                       const char *mimetype2,
                       const apr_array_header_t *propchanges,
                       apr_hash_t *originalprops,
                       void *diff_baton)
{
  struct diff_callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  if (tmpfile2 != NULL)
    SVN_ERR(b->callbacks->file_changed(adm_access, contentstate, path,
                                       tmpfile1, tmpfile2,
                                       rev1, rev2, mimetype1, mimetype2,
                                       b->baton));
  if (propchanges->nelts > 0)
    SVN_ERR(b->callbacks->props_changed(adm_access, propstate, path,
                                        propchanges, originalprops,
                                        b->baton));

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
wrap_3to1_file_added(svn_wc_adm_access_t *adm_access,
                     svn_wc_notify_state_t *contentstate,
                     svn_wc_notify_state_t *propstate,
                     svn_boolean_t *tree_conflicted,
                     const char *path,
                     const char *tmpfile1,
                     const char *tmpfile2,
                     svn_revnum_t rev1,
                     svn_revnum_t rev2,
                     const char *mimetype1,
                     const char *mimetype2,
                     const apr_array_header_t *propchanges,
                     apr_hash_t *originalprops,
                     void *diff_baton)
{
  struct diff_callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  SVN_ERR(b->callbacks->file_added(adm_access, contentstate, path,
                                   tmpfile1, tmpfile2, rev1, rev2,
                                   mimetype1, mimetype2, b->baton));
  if (propchanges->nelts > 0)
    SVN_ERR(b->callbacks->props_changed(adm_access, propstate, path,
                                        propchanges, originalprops,
                                        b->baton));

  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
wrap_3to1_file_deleted(svn_wc_adm_access_t *adm_access,
                       svn_wc_notify_state_t *state,
                       svn_boolean_t *tree_conflicted,
                       const char *path,
                       const char *tmpfile1,
                       const char *tmpfile2,
                       const char *mimetype1,
                       const char *mimetype2,
                       apr_hash_t *originalprops,
                       void *diff_baton)
{
  struct diff_callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  SVN_ERR_ASSERT(originalprops);

  return b->callbacks->file_deleted(adm_access, state, path,
                                    tmpfile1, tmpfile2, mimetype1, mimetype2,
                                    b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
wrap_3to1_dir_added(svn_wc_adm_access_t *adm_access,
                    svn_wc_notify_state_t *state,
                    svn_boolean_t *tree_conflicted,
                    const char *path,
                    svn_revnum_t rev,
                    void *diff_baton)
{
  struct diff_callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks->dir_added(adm_access, state, path, rev, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
wrap_3to1_dir_deleted(svn_wc_adm_access_t *adm_access,
                      svn_wc_notify_state_t *state,
                      svn_boolean_t *tree_conflicted,
                      const char *path,
                      void *diff_baton)
{
  struct diff_callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks->dir_deleted(adm_access, state, path, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t. */
static svn_error_t *
wrap_3to1_dir_props_changed(svn_wc_adm_access_t *adm_access,
                            svn_wc_notify_state_t *state,
                            svn_boolean_t *tree_conflicted,
                            const char *path,
                            const apr_array_header_t *propchanges,
                            apr_hash_t *originalprops,
                            void *diff_baton)
{
  struct diff_callbacks_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks->props_changed(adm_access, state, path, propchanges,
                                     originalprops, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t
   and svn_wc_diff_callbacks2_t. */
static svn_error_t *
wrap_3to1or2_dir_opened(svn_wc_adm_access_t *adm_access,
                        svn_boolean_t *tree_conflicted,
                        const char *path,
                        svn_revnum_t rev,
                        void *diff_baton)
{
  if (tree_conflicted)
    *tree_conflicted = FALSE;
  /* Do nothing. */
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks3_t function for wrapping svn_wc_diff_callbacks_t
   and svn_wc_diff_callbacks2_t. */
static svn_error_t *
wrap_3to1or2_dir_closed(svn_wc_adm_access_t *adm_access,
                        svn_wc_notify_state_t *propstate,
                        svn_wc_notify_state_t *contentstate,
                        svn_boolean_t *tree_conflicted,
                        const char *path,
                        void *diff_baton)
{
  if (contentstate)
    *contentstate = svn_wc_notify_state_unknown;
  if (propstate)
    *propstate = svn_wc_notify_state_unknown;
  if (tree_conflicted)
    *tree_conflicted = FALSE;
  /* Do nothing. */
  return SVN_NO_ERROR;
}

/* Used to wrap svn_diff_callbacks_t as an svn_wc_diff_callbacks3_t. */
static struct svn_wc_diff_callbacks3_t diff_callbacks_wrapper = {
  wrap_3to1_file_changed,
  wrap_3to1_file_added,
  wrap_3to1_file_deleted,
  wrap_3to1_dir_added,
  wrap_3to1_dir_deleted,
  wrap_3to1_dir_props_changed,
  wrap_3to1or2_dir_opened,
  wrap_3to1or2_dir_closed
};



/* Used to wrap svn_wc_diff_callbacks2_t. */
struct diff_callbacks2_wrapper_baton {
  const svn_wc_diff_callbacks2_t *callbacks2;
  void *baton;
};

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
wrap_3to2_file_changed(svn_wc_adm_access_t *adm_access,
                       svn_wc_notify_state_t *contentstate,
                       svn_wc_notify_state_t *propstate,
                       svn_boolean_t *tree_conflicted,
                       const char *path,
                       const char *tmpfile1,
                       const char *tmpfile2,
                       svn_revnum_t rev1,
                       svn_revnum_t rev2,
                       const char *mimetype1,
                       const char *mimetype2,
                       const apr_array_header_t *propchanges,
                       apr_hash_t *originalprops,
                       void *diff_baton)
{
  struct diff_callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->file_changed(adm_access, contentstate, propstate,
                                     path, tmpfile1, tmpfile2,
                                     rev1, rev2, mimetype1, mimetype2,
                                     propchanges, originalprops, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
wrap_3to2_file_added(svn_wc_adm_access_t *adm_access,
                     svn_wc_notify_state_t *contentstate,
                     svn_wc_notify_state_t *propstate,
                     svn_boolean_t *tree_conflicted,
                     const char *path,
                     const char *tmpfile1,
                     const char *tmpfile2,
                     svn_revnum_t rev1,
                     svn_revnum_t rev2,
                     const char *mimetype1,
                     const char *mimetype2,
                     const apr_array_header_t *propchanges,
                     apr_hash_t *originalprops,
                     void *diff_baton)
{
  struct diff_callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->file_added(adm_access, contentstate, propstate, path,
                                   tmpfile1, tmpfile2, rev1, rev2,
                                   mimetype1, mimetype2, propchanges,
                                   originalprops, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
wrap_3to2_file_deleted(svn_wc_adm_access_t *adm_access,
                       svn_wc_notify_state_t *state,
                       svn_boolean_t *tree_conflicted,
                       const char *path,
                       const char *tmpfile1,
                       const char *tmpfile2,
                       const char *mimetype1,
                       const char *mimetype2,
                       apr_hash_t *originalprops,
                       void *diff_baton)
{
  struct diff_callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->file_deleted(adm_access, state, path,
                                     tmpfile1, tmpfile2, mimetype1, mimetype2,
                                     originalprops, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
wrap_3to2_dir_added(svn_wc_adm_access_t *adm_access,
                    svn_wc_notify_state_t *state,
                    svn_boolean_t *tree_conflicted,
                    const char *path,
                    svn_revnum_t rev,
                    void *diff_baton)
{
  struct diff_callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->dir_added(adm_access, state, path, rev, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
wrap_3to2_dir_deleted(svn_wc_adm_access_t *adm_access,
                      svn_wc_notify_state_t *state,
                      svn_boolean_t *tree_conflicted,
                      const char *path,
                      void *diff_baton)
{
  struct diff_callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->dir_deleted(adm_access, state, path, b->baton);
}

/* An svn_wc_diff_callbacks3_t function for wrapping
 * svn_wc_diff_callbacks2_t. */
static svn_error_t *
wrap_3to2_dir_props_changed(svn_wc_adm_access_t *adm_access,
                            svn_wc_notify_state_t *state,
                            svn_boolean_t *tree_conflicted,
                            const char *path,
                            const apr_array_header_t *propchanges,
                            apr_hash_t *originalprops,
                            void *diff_baton)
{
  struct diff_callbacks2_wrapper_baton *b = diff_baton;

  if (tree_conflicted)
    *tree_conflicted = FALSE;

  return b->callbacks2->dir_props_changed(adm_access, state, path, propchanges,
                                          originalprops, b->baton);
}

/* Used to wrap svn_diff_callbacks2_t as an svn_wc_diff_callbacks3_t. */
static struct svn_wc_diff_callbacks3_t diff_callbacks2_wrapper = {
  wrap_3to2_file_changed,
  wrap_3to2_file_added,
  wrap_3to2_file_deleted,
  wrap_3to2_dir_added,
  wrap_3to2_dir_deleted,
  wrap_3to2_dir_props_changed,
  wrap_3to1or2_dir_opened,
  wrap_3to1or2_dir_closed
};



/* Used to wrap svn_wc_diff_callbacks3_t. */
struct diff_callbacks3_wrapper_baton {
  const svn_wc_diff_callbacks3_t *callbacks3;
  svn_wc__db_t *db;
  void *baton;
  const char *anchor;
  const char *anchor_abspath;
};

static svn_error_t *
wrap_4to3_file_opened(svn_boolean_t *tree_conflicted,
                      svn_boolean_t *skip,
                      const char *path,
                      svn_revnum_t rev,
                      void *diff_baton,
                      apr_pool_t *scratch_pool)
{
  return SVN_NO_ERROR;
}

/* An svn_wc_diff_callbacks4_t function for wrapping
 * svn_wc_diff_callbacks3_t. */
static svn_error_t *
wrap_4to3_file_changed(svn_wc_notify_state_t *contentstate,
                       svn_wc_notify_state_t *propstate,
                       svn_boolean_t *tree_conflicted,
                       const char *path,
                       const char *tmpfile1,
                       const char *tmpfile2,
                       svn_revnum_t rev1,
                       svn_revnum_t rev2,
                       const char *mimetype1,
                       const char *mimetype2,
                       const apr_array_header_t *propchanges,
                       apr_hash_t *originalprops,
                       void *diff_baton,
                       apr_pool_t *scratch_pool)
{
  struct diff_callbacks3_wrapper_baton *b = diff_baton;
  svn_wc_adm_access_t *adm_access;
  const char *dir = svn_relpath_dirname(path, scratch_pool);

  adm_access = svn_wc__adm_retrieve_internal2(
                        b->db,
                        svn_dirent_join(b->anchor_abspath, dir, scratch_pool),
                        scratch_pool);

  return b->callbacks3->file_changed(adm_access, contentstate, propstate,
                                     tree_conflicted,
                                     svn_dirent_join(b->anchor, path,
                                                     scratch_pool),
                                     tmpfile1, tmpfile2,
                                     rev1, rev2, mimetype1, mimetype2,
                                     propchanges, originalprops, b->baton);
}

/* An svn_wc_diff_callbacks4_t function for wrapping
 * svn_wc_diff_callbacks3_t. */
static svn_error_t *
wrap_4to3_file_added(svn_wc_notify_state_t *contentstate,
                     svn_wc_notify_state_t *propstate,
                     svn_boolean_t *tree_conflicted,
                     const char *path,
                     const char *tmpfile1,
                     const char *tmpfile2,
                     svn_revnum_t rev1,
                     svn_revnum_t rev2,
                     const char *mimetype1,
                     const char *mimetype2,
                     const char *copyfrom_path,
                     svn_revnum_t copyfrom_revision,
                     const apr_array_header_t *propchanges,
                     apr_hash_t *originalprops,
                     void *diff_baton,
                     apr_pool_t *scratch_pool)
{
  struct diff_callbacks3_wrapper_baton *b = diff_baton;
  svn_wc_adm_access_t *adm_access;
  const char *dir = svn_relpath_dirname(path, scratch_pool);

  adm_access = svn_wc__adm_retrieve_internal2(
                        b->db,
                        svn_dirent_join(b->anchor_abspath, dir, scratch_pool),
                        scratch_pool);

  return b->callbacks3->file_added(adm_access, contentstate, propstate,
                                   tree_conflicted,
                                   svn_dirent_join(b->anchor, path,
                                                   scratch_pool),
                                   tmpfile1, tmpfile2,
                                   rev1, rev2, mimetype1, mimetype2,
                                   propchanges, originalprops, b->baton);
}

/* An svn_wc_diff_callbacks4_t function for wrapping
 * svn_wc_diff_callbacks3_t. */
static svn_error_t *
wrap_4to3_file_deleted(svn_wc_notify_state_t *state,
                       svn_boolean_t *tree_conflicted,
                       const char *path,
                       const char *tmpfile1,
                       const char *tmpfile2,
                       const char *mimetype1,
                       const char *mimetype2,
                       apr_hash_t *originalprops,
                       void *diff_baton,
                       apr_pool_t *scratch_pool)
{
  struct diff_callbacks3_wrapper_baton *b = diff_baton;
  svn_wc_adm_access_t *adm_access;
  const char *dir = svn_relpath_dirname(path, scratch_pool);

  adm_access = svn_wc__adm_retrieve_internal2(
                        b->db,
                        svn_dirent_join(b->anchor_abspath, dir, scratch_pool),
                        scratch_pool);

  return b->callbacks3->file_deleted(adm_access, state, tree_conflicted,
                                     svn_dirent_join(b->anchor, path,
                                                     scratch_pool),
                                     tmpfile1, tmpfile2,
                                     mimetype1, mimetype2, originalprops,
                                     b->baton);
}

/* An svn_wc_diff_callbacks4_t function for wrapping
 * svn_wc_diff_callbacks3_t. */
static svn_error_t *
wrap_4to3_dir_added(svn_wc_notify_state_t *state,
                    svn_boolean_t *tree_conflicted,
                    svn_boolean_t *skip,
                    svn_boolean_t *skip_children,
                    const char *path,
                    svn_revnum_t rev,
                    const char *copyfrom_path,
                    svn_revnum_t copyfrom_revision,
                    void *diff_baton,
                    apr_pool_t *scratch_pool)
{
  struct diff_callbacks3_wrapper_baton *b = diff_baton;
  svn_wc_adm_access_t *adm_access;

  adm_access = svn_wc__adm_retrieve_internal2(
                        b->db,
                        svn_dirent_join(b->anchor_abspath, path, scratch_pool),
                        scratch_pool);

  return b->callbacks3->dir_added(adm_access, state, tree_conflicted,
                                  svn_dirent_join(b->anchor, path,
                                                     scratch_pool),
                                  rev, b->baton);
}

/* An svn_wc_diff_callbacks4_t function for wrapping
 * svn_wc_diff_callbacks3_t. */
static svn_error_t *
wrap_4to3_dir_deleted(svn_wc_notify_state_t *state,
                      svn_boolean_t *tree_conflicted,
                      const char *path,
                      void *diff_baton,
                      apr_pool_t *scratch_pool)
{
  struct diff_callbacks3_wrapper_baton *b = diff_baton;
  svn_wc_adm_access_t *adm_access;

  adm_access = svn_wc__adm_retrieve_internal2(
                        b->db,
                        svn_dirent_join(b->anchor_abspath, path, scratch_pool),
                        scratch_pool);

  return b->callbacks3->dir_deleted(adm_access, state, tree_conflicted,
                                    svn_dirent_join(b->anchor, path,
                                                     scratch_pool),
                                    b->baton);
}

/* An svn_wc_diff_callbacks4_t function for wrapping
 * svn_wc_diff_callbacks3_t. */
static svn_error_t *
wrap_4to3_dir_props_changed(svn_wc_notify_state_t *propstate,
                            svn_boolean_t *tree_conflicted,
                            const char *path,
                            svn_boolean_t dir_was_added,
                            const apr_array_header_t *propchanges,
                            apr_hash_t *original_props,
                            void *diff_baton,
                            apr_pool_t *scratch_pool)
{
  struct diff_callbacks3_wrapper_baton *b = diff_baton;
  svn_wc_adm_access_t *adm_access;

  adm_access = svn_wc__adm_retrieve_internal2(
                        b->db,
                        svn_dirent_join(b->anchor_abspath, path, scratch_pool),
                        scratch_pool);

  return b->callbacks3->dir_props_changed(adm_access, propstate,
                                          tree_conflicted,
                                          svn_dirent_join(b->anchor, path,
                                                     scratch_pool),
                                          propchanges, original_props,
                                          b->baton);
}

/* An svn_wc_diff_callbacks4_t function for wrapping
 * svn_wc_diff_callbacks3_t. */
static svn_error_t *
wrap_4to3_dir_opened(svn_boolean_t *tree_conflicted,
                     svn_boolean_t *skip,
                     svn_boolean_t *skip_children,
                     const char *path,
                     svn_revnum_t rev,
                     void *diff_baton,
                     apr_pool_t *scratch_pool)
{
  struct diff_callbacks3_wrapper_baton *b = diff_baton;
  svn_wc_adm_access_t *adm_access;

  adm_access = svn_wc__adm_retrieve_internal2(
                        b->db,
                        svn_dirent_join(b->anchor_abspath, path, scratch_pool),
                        scratch_pool);
  if (skip_children)
    *skip_children = FALSE;

  return b->callbacks3->dir_opened(adm_access, tree_conflicted,
                                   svn_dirent_join(b->anchor, path,
                                                     scratch_pool),
                                   rev, b->baton);
}

/* An svn_wc_diff_callbacks4_t function for wrapping
 * svn_wc_diff_callbacks3_t. */
static svn_error_t *
wrap_4to3_dir_closed(svn_wc_notify_state_t *contentstate,
                     svn_wc_notify_state_t *propstate,
                     svn_boolean_t *tree_conflicted,
                     const char *path,
                     svn_boolean_t dir_was_added,
                     void *diff_baton,
                     apr_pool_t *scratch_pool)
{
  struct diff_callbacks3_wrapper_baton *b = diff_baton;
  svn_wc_adm_access_t *adm_access;

  adm_access = svn_wc__adm_retrieve_internal2(
                        b->db,
                        svn_dirent_join(b->anchor_abspath, path, scratch_pool),
                        scratch_pool);

  return b->callbacks3->dir_closed(adm_access, contentstate, propstate,
                                   tree_conflicted,
                                   svn_dirent_join(b->anchor, path,
                                                     scratch_pool),
                                   b->baton);
}


/* Used to wrap svn_diff_callbacks3_t as an svn_wc_diff_callbacks4_t. */
static struct svn_wc_diff_callbacks4_t diff_callbacks3_wrapper = {
  wrap_4to3_file_opened,
  wrap_4to3_file_changed,
  wrap_4to3_file_added,
  wrap_4to3_file_deleted,
  wrap_4to3_dir_deleted,
  wrap_4to3_dir_opened,
  wrap_4to3_dir_added,
  wrap_4to3_dir_props_changed,
  wrap_4to3_dir_closed
};


svn_error_t *
svn_wc_get_diff_editor6(const svn_delta_editor_t **editor,
                        void **edit_baton,
                        svn_wc_context_t *wc_ctx,
                        const char *anchor_abspath,
                        const char *target,
                        svn_depth_t depth,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t show_copies_as_adds,
                        svn_boolean_t use_git_diff_format,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_boolean_t server_performs_filtering,
                        const apr_array_header_t *changelist_filter,
                        const svn_wc_diff_callbacks4_t *callbacks,
                        void *callback_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const svn_diff_tree_processor_t *diff_processor;

  /* --git implies --show-copies-as-adds */
  if (use_git_diff_format)
    show_copies_as_adds = TRUE;

  /* --show-copies-as-adds implies --notice-ancestry */
  if (show_copies_as_adds)
    ignore_ancestry = FALSE;

  SVN_ERR(svn_wc__wrap_diff_callbacks(&diff_processor,
                                      callbacks, callback_baton, TRUE,
                                      result_pool, scratch_pool));

  if (reverse_order)
    diff_processor = svn_diff__tree_processor_reverse_create(
                              diff_processor, NULL, result_pool);

  if (! show_copies_as_adds)
    diff_processor = svn_diff__tree_processor_copy_as_changed_create(
                              diff_processor, result_pool);

  return svn_error_trace(
    svn_wc__get_diff_editor(editor, edit_baton,
                            wc_ctx,
                            anchor_abspath, target,
                            depth,
                            ignore_ancestry, use_text_base,
                            reverse_order, server_performs_filtering,
                            changelist_filter,
                            diff_processor,
                            cancel_func, cancel_baton,
                            result_pool, scratch_pool));
}


svn_error_t *
svn_wc_get_diff_editor5(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks3_t *callbacks,
                        void *callback_baton,
                        svn_depth_t depth,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const apr_array_header_t *changelist_filter,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool)
{
  struct diff_callbacks3_wrapper_baton *b = apr_palloc(pool, sizeof(*b));
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *db = svn_wc__adm_get_db(anchor);

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, db, pool));

  b->callbacks3 = callbacks;
  b->baton = callback_baton;
  b->db = db;
  b->anchor = svn_wc_adm_access_path(anchor);
  b->anchor_abspath = svn_wc__adm_access_abspath(anchor);

  SVN_ERR(svn_wc_get_diff_editor6(editor,
                                   edit_baton,
                                   wc_ctx,
                                   b->anchor_abspath,
                                   target,
                                   depth,
                                   ignore_ancestry,
                                   FALSE,
                                   FALSE,
                                   use_text_base,
                                   reverse_order,
                                   FALSE,
                                   changelist_filter,
                                   &diff_callbacks3_wrapper,
                                   b,
                                   cancel_func,
                                   cancel_baton,
                                   pool,
                                   pool));

  /* Can't destroy wc_ctx. It is used by the diff editor */

   return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_get_diff_editor4(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks2_t *callbacks,
                        void *callback_baton,
                        svn_depth_t depth,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const apr_array_header_t *changelist_filter,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool)
{
  struct diff_callbacks2_wrapper_baton *b = apr_palloc(pool, sizeof(*b));
  b->callbacks2 = callbacks;
  b->baton = callback_baton;
  return svn_wc_get_diff_editor5(anchor,
                                 target,
                                 &diff_callbacks2_wrapper,
                                 b,
                                 depth,
                                 ignore_ancestry,
                                 use_text_base,
                                 reverse_order,
                                 cancel_func,
                                 cancel_baton,
                                 changelist_filter,
                                 editor,
                                 edit_baton,
                                 pool);
}

svn_error_t *
svn_wc_get_diff_editor3(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks2_t *callbacks,
                        void *callback_baton,
                        svn_boolean_t recurse,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool)
{
  return svn_wc_get_diff_editor4(anchor,
                                 target,
                                 callbacks,
                                 callback_baton,
                                 SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                 ignore_ancestry,
                                 use_text_base,
                                 reverse_order,
                                 cancel_func,
                                 cancel_baton,
                                 NULL,
                                 editor,
                                 edit_baton,
                                 pool);
}

svn_error_t *
svn_wc_get_diff_editor2(svn_wc_adm_access_t *anchor,
                        const char *target,
                        const svn_wc_diff_callbacks_t *callbacks,
                        void *callback_baton,
                        svn_boolean_t recurse,
                        svn_boolean_t ignore_ancestry,
                        svn_boolean_t use_text_base,
                        svn_boolean_t reverse_order,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        const svn_delta_editor_t **editor,
                        void **edit_baton,
                        apr_pool_t *pool)
{
  struct diff_callbacks_wrapper_baton *b = apr_palloc(pool, sizeof(*b));
  b->callbacks = callbacks;
  b->baton = callback_baton;
  return svn_wc_get_diff_editor5(anchor, target, &diff_callbacks_wrapper, b,
                                 SVN_DEPTH_INFINITY_OR_FILES(recurse),
                                 ignore_ancestry, use_text_base,
                                 reverse_order, cancel_func, cancel_baton,
                                 NULL, editor, edit_baton, pool);
}

svn_error_t *
svn_wc_get_diff_editor(svn_wc_adm_access_t *anchor,
                       const char *target,
                       const svn_wc_diff_callbacks_t *callbacks,
                       void *callback_baton,
                       svn_boolean_t recurse,
                       svn_boolean_t use_text_base,
                       svn_boolean_t reverse_order,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       const svn_delta_editor_t **editor,
                       void **edit_baton,
                       apr_pool_t *pool)
{
  return svn_wc_get_diff_editor2(anchor, target, callbacks, callback_baton,
                                 recurse, FALSE, use_text_base, reverse_order,
                                 cancel_func, cancel_baton,
                                 editor, edit_baton, pool);
}

svn_error_t *
svn_wc_diff5(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks3_t *callbacks,
             void *callback_baton,
             svn_depth_t depth,
             svn_boolean_t ignore_ancestry,
             const apr_array_header_t *changelist_filter,
             apr_pool_t *pool)
{
  struct diff_callbacks3_wrapper_baton *b = apr_palloc(pool, sizeof(*b));
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *db = svn_wc__adm_get_db(anchor);

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, db, pool));

  b->callbacks3 = callbacks;
  b->baton = callback_baton;
  b->anchor = svn_wc_adm_access_path(anchor);
  b->anchor_abspath = svn_wc__adm_access_abspath(anchor);

  SVN_ERR(svn_wc_diff6(wc_ctx,
                       svn_dirent_join(b->anchor_abspath, target, pool),
                       &diff_callbacks3_wrapper,
                       b,
                       depth,
                       ignore_ancestry,
                       FALSE,
                       FALSE,
                       changelist_filter,
                       NULL, NULL,
                       pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_diff4(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks2_t *callbacks,
             void *callback_baton,
             svn_depth_t depth,
             svn_boolean_t ignore_ancestry,
             const apr_array_header_t *changelist_filter,
             apr_pool_t *pool)
{
  struct diff_callbacks2_wrapper_baton *b = apr_palloc(pool, sizeof(*b));
  b->callbacks2 = callbacks;
  b->baton = callback_baton;

  return svn_wc_diff5(anchor, target, &diff_callbacks2_wrapper, b,
                      depth, ignore_ancestry, changelist_filter, pool);
}

svn_error_t *
svn_wc_diff3(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks2_t *callbacks,
             void *callback_baton,
             svn_boolean_t recurse,
             svn_boolean_t ignore_ancestry,
             apr_pool_t *pool)
{
  return svn_wc_diff4(anchor, target, callbacks, callback_baton,
                      SVN_DEPTH_INFINITY_OR_FILES(recurse), ignore_ancestry,
                      NULL, pool);
}

svn_error_t *
svn_wc_diff2(svn_wc_adm_access_t *anchor,
             const char *target,
             const svn_wc_diff_callbacks_t *callbacks,
             void *callback_baton,
             svn_boolean_t recurse,
             svn_boolean_t ignore_ancestry,
             apr_pool_t *pool)
{
  struct diff_callbacks_wrapper_baton *b = apr_pcalloc(pool, sizeof(*b));
  b->callbacks = callbacks;
  b->baton = callback_baton;
  return svn_wc_diff5(anchor, target, &diff_callbacks_wrapper, b,
                      SVN_DEPTH_INFINITY_OR_FILES(recurse), ignore_ancestry,
                      NULL, pool);
}

svn_error_t *
svn_wc_diff(svn_wc_adm_access_t *anchor,
            const char *target,
            const svn_wc_diff_callbacks_t *callbacks,
            void *callback_baton,
            svn_boolean_t recurse,
            apr_pool_t *pool)
{
  return svn_wc_diff2(anchor, target, callbacks, callback_baton,
                      recurse, FALSE, pool);
}

/*** From entries.c ***/
svn_error_t *
svn_wc_walk_entries2(const char *path,
                     svn_wc_adm_access_t *adm_access,
                     const svn_wc_entry_callbacks_t *walk_callbacks,
                     void *walk_baton,
                     svn_boolean_t show_hidden,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *pool)
{
  svn_wc_entry_callbacks2_t walk_cb2 = { 0 };
  walk_cb2.found_entry = walk_callbacks->found_entry;
  walk_cb2.handle_error = svn_wc__walker_default_error_handler;
  return svn_wc_walk_entries3(path, adm_access,
                              &walk_cb2, walk_baton, svn_depth_infinity,
                              show_hidden, cancel_func, cancel_baton, pool);
}

svn_error_t *
svn_wc_walk_entries(const char *path,
                    svn_wc_adm_access_t *adm_access,
                    const svn_wc_entry_callbacks_t *walk_callbacks,
                    void *walk_baton,
                    svn_boolean_t show_hidden,
                    apr_pool_t *pool)
{
  return svn_wc_walk_entries2(path, adm_access, walk_callbacks,
                              walk_baton, show_hidden, NULL, NULL,
                              pool);
}

svn_error_t *
svn_wc_mark_missing_deleted(const char *path,
                            svn_wc_adm_access_t *parent,
                            apr_pool_t *pool)
{
  /* With a single DB a node will never be missing */
  return svn_error_createf(SVN_ERR_WC_PATH_FOUND, NULL,
                           _("Unexpectedly found '%s': "
                             "path is marked 'missing'"),
                           svn_dirent_local_style(path, pool));
}


/*** From props.c ***/
svn_error_t *
svn_wc_parse_externals_description2(apr_array_header_t **externals_p,
                                    const char *parent_directory,
                                    const char *desc,
                                    apr_pool_t *pool)
{
  apr_array_header_t *list;
  apr_pool_t *subpool = svn_pool_create(pool);

  SVN_ERR(svn_wc_parse_externals_description3(externals_p ? &list : NULL,
                                              parent_directory, desc,
                                              TRUE, subpool));

  if (externals_p)
    {
      int i;

      *externals_p = apr_array_make(pool, list->nelts,
                                    sizeof(svn_wc_external_item_t *));
      for (i = 0; i < list->nelts; i++)
        {
          svn_wc_external_item2_t *item2 = APR_ARRAY_IDX(list, i,
                                             svn_wc_external_item2_t *);
          svn_wc_external_item_t *item = apr_palloc(pool, sizeof (*item));

          if (item2->target_dir)
            item->target_dir = apr_pstrdup(pool, item2->target_dir);
          if (item2->url)
            item->url = apr_pstrdup(pool, item2->url);
          item->revision = item2->revision;

          APR_ARRAY_PUSH(*externals_p, svn_wc_external_item_t *) = item;
        }
    }

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_parse_externals_description(apr_hash_t **externals_p,
                                   const char *parent_directory,
                                   const char *desc,
                                   apr_pool_t *pool)
{
  apr_array_header_t *list;

  SVN_ERR(svn_wc_parse_externals_description2(externals_p ? &list : NULL,
                                              parent_directory, desc, pool));

  /* Store all of the items into the hash if that was requested. */
  if (externals_p)
    {
      int i;

      *externals_p = apr_hash_make(pool);
      for (i = 0; i < list->nelts; i++)
        {
          svn_wc_external_item_t *item;
          item = APR_ARRAY_IDX(list, i, svn_wc_external_item_t *);

          svn_hash_sets(*externals_p, item->target_dir, item);
        }
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_prop_set3(const char *name,
                 const svn_string_t *value,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 svn_boolean_t skip_checks,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;
  svn_error_t *err;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  err = svn_wc_prop_set4(wc_ctx, local_abspath,
                         name, value,
                         svn_depth_empty,
                         skip_checks, NULL /* changelist_filter */,
                         NULL, NULL /* cancellation */,
                         notify_func, notify_baton,
                         pool);

  if (err && err->apr_err == SVN_ERR_WC_INVALID_SCHEDULE)
    svn_error_clear(err);
  else
    SVN_ERR(err);

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_prop_set2(const char *name,
                 const svn_string_t *value,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 svn_boolean_t skip_checks,
                 apr_pool_t *pool)
{
  return svn_wc_prop_set3(name, value, path, adm_access, skip_checks,
                          NULL, NULL, pool);
}

svn_error_t *
svn_wc_prop_set(const char *name,
                const svn_string_t *value,
                const char *path,
                svn_wc_adm_access_t *adm_access,
                apr_pool_t *pool)
{
  return svn_wc_prop_set2(name, value, path, adm_access, FALSE, pool);
}

svn_error_t *
svn_wc_prop_list(apr_hash_t **props,
                 const char *path,
                 svn_wc_adm_access_t *adm_access,
                 apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;
  svn_error_t *err;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access), pool));

  err = svn_wc_prop_list2(props, wc_ctx, local_abspath, pool, pool);
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      *props = apr_hash_make(pool);
      svn_error_clear(err);
      err = NULL;
    }

  return svn_error_compose_create(err, svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_prop_get(const svn_string_t **value,
                const char *name,
                const char *path,
                svn_wc_adm_access_t *adm_access,
                apr_pool_t *pool)
{

  svn_wc_context_t *wc_ctx;
  const char *local_abspath;
  svn_error_t *err;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access), pool));

  err = svn_wc_prop_get2(value, wc_ctx, local_abspath, name, pool, pool);

  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      *value = NULL;
      svn_error_clear(err);
      err = NULL;
    }

  return svn_error_compose_create(err, svn_wc_context_destroy(wc_ctx));
}

/* baton for conflict_func_1to2_wrapper */
struct conflict_func_1to2_baton
{
  svn_wc_conflict_resolver_func_t inner_func;
  void *inner_baton;
};


/* Implements svn_wc_conflict_resolver_func2_t */
static svn_error_t *
conflict_func_1to2_wrapper(svn_wc_conflict_result_t **result,
                           const svn_wc_conflict_description2_t *conflict,
                           void *baton,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  struct conflict_func_1to2_baton *btn = baton;
  svn_wc_conflict_description_t *cd = svn_wc__cd2_to_cd(conflict,
                                                        scratch_pool);

  return svn_error_trace(btn->inner_func(result, cd, btn->inner_baton,
                                         result_pool));
}

svn_error_t *
svn_wc_merge_props2(svn_wc_notify_state_t *state,
                    const char *path,
                    svn_wc_adm_access_t *adm_access,
                    apr_hash_t *baseprops,
                    const apr_array_header_t *propchanges,
                    svn_boolean_t base_merge,
                    svn_boolean_t dry_run,
                    svn_wc_conflict_resolver_func_t conflict_func,
                    void *conflict_baton,
                    apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  svn_error_t *err;
  svn_wc_context_t *wc_ctx;
  struct conflict_func_1to2_baton conflict_wrapper;

  if (base_merge && !dry_run)
    return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                            U_("base_merge=TRUE is no longer supported; "
                               "see notes/api-errata/1.7/wc006.txt"));

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, scratch_pool));

  conflict_wrapper.inner_func = conflict_func;
  conflict_wrapper.inner_baton = conflict_baton;

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL,
                                         svn_wc__adm_get_db(adm_access),
                                         scratch_pool));

  err = svn_wc_merge_props3(state,
                            wc_ctx,
                            local_abspath,
                            NULL /* left_version */,
                            NULL /* right_version */,
                            baseprops,
                            propchanges,
                            dry_run,
                            conflict_func ? conflict_func_1to2_wrapper
                                          : NULL,
                            &conflict_wrapper,
                            NULL, NULL,
                            scratch_pool);

  if (err)
    switch(err->apr_err)
      {
        case SVN_ERR_WC_PATH_NOT_FOUND:
        case SVN_ERR_WC_PATH_UNEXPECTED_STATUS:
          err->apr_err = SVN_ERR_UNVERSIONED_RESOURCE;
          break;
      }
  return svn_error_trace(
            svn_error_compose_create(err,
                                     svn_wc_context_destroy(wc_ctx)));
}

svn_error_t *
svn_wc_merge_props(svn_wc_notify_state_t *state,
                   const char *path,
                   svn_wc_adm_access_t *adm_access,
                   apr_hash_t *baseprops,
                   const apr_array_header_t *propchanges,
                   svn_boolean_t base_merge,
                   svn_boolean_t dry_run,
                   apr_pool_t *pool)
{
  return svn_wc_merge_props2(state, path, adm_access, baseprops, propchanges,
                             base_merge, dry_run, NULL, NULL, pool);
}


svn_error_t *
svn_wc_merge_prop_diffs(svn_wc_notify_state_t *state,
                        const char *path,
                        svn_wc_adm_access_t *adm_access,
                        const apr_array_header_t *propchanges,
                        svn_boolean_t base_merge,
                        svn_boolean_t dry_run,
                        apr_pool_t *pool)
{
  /* NOTE: Here, we use implementation knowledge.  The public
     svn_wc_merge_props2 doesn't allow NULL as baseprops argument, but we know
     that it works. */
  return svn_wc_merge_props2(state, path, adm_access, NULL, propchanges,
                             base_merge, dry_run, NULL, NULL, pool);
}

svn_error_t *
svn_wc_get_prop_diffs(apr_array_header_t **propchanges,
                      apr_hash_t **original_props,
                      const char *path,
                      svn_wc_adm_access_t *adm_access,
                      apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access), pool));

  SVN_ERR(svn_wc_get_prop_diffs2(propchanges, original_props, wc_ctx,
                                 local_abspath, pool, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}


svn_error_t *
svn_wc_props_modified_p(svn_boolean_t *modified_p,
                        const char *path,
                        svn_wc_adm_access_t *adm_access,
                        apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;
  svn_error_t *err;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access), pool));

  err = svn_wc_props_modified_p2(modified_p,
                                 wc_ctx,
                                 local_abspath,
                                 pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);

      svn_error_clear(err);
      *modified_p = FALSE;
    }

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}


svn_error_t *
svn_wc__status2_from_3(svn_wc_status2_t **status,
                       const svn_wc_status3_t *old_status,
                       svn_wc_context_t *wc_ctx,
                       const char *local_abspath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  const svn_wc_entry_t *entry = NULL;

  if (old_status == NULL)
    {
      *status = NULL;
      return SVN_NO_ERROR;
    }

  *status = apr_pcalloc(result_pool, sizeof(**status));

  if (old_status->versioned)
    {
      svn_error_t *err;
      err= svn_wc__get_entry(&entry, wc_ctx->db, local_abspath, FALSE,
                             svn_node_unknown, result_pool, scratch_pool);

      if (err && err->apr_err == SVN_ERR_NODE_UNEXPECTED_KIND)
        svn_error_clear(err);
      else
        SVN_ERR(err);
    }

  (*status)->entry = entry;
  (*status)->copied = old_status->copied;
  (*status)->repos_lock = svn_lock_dup(old_status->repos_lock, result_pool);

  if (old_status->repos_relpath)
    (*status)->url = svn_path_url_add_component2(old_status->repos_root_url,
                                                 old_status->repos_relpath,
                                                 result_pool);
  (*status)->ood_last_cmt_rev = old_status->ood_changed_rev;
  (*status)->ood_last_cmt_date = old_status->ood_changed_date;
  (*status)->ood_kind = old_status->ood_kind;
  (*status)->ood_last_cmt_author = old_status->ood_changed_author;

  if (old_status->conflicted)
    {
      const svn_wc_conflict_description2_t *tree_conflict2;
      SVN_ERR(svn_wc__get_tree_conflict(&tree_conflict2, wc_ctx, local_abspath,
                                        scratch_pool, scratch_pool));
      (*status)->tree_conflict = svn_wc__cd2_to_cd(tree_conflict2, result_pool);
    }

  (*status)->switched = old_status->switched;

  (*status)->text_status = old_status->node_status;
  (*status)->prop_status = old_status->prop_status;

  (*status)->repos_text_status = old_status->repos_node_status;
  (*status)->repos_prop_status = old_status->repos_prop_status;

  /* Some values might be inherited from properties */
  if (old_status->node_status == svn_wc_status_modified
      || old_status->node_status == svn_wc_status_conflicted)
    (*status)->text_status = old_status->text_status;

  /* (Currently a no-op, but just make sure it is ok) */
  if (old_status->repos_node_status == svn_wc_status_modified
      || old_status->repos_node_status == svn_wc_status_conflicted)
    (*status)->repos_text_status = old_status->repos_text_status;

  if (old_status->node_status == svn_wc_status_added)
    (*status)->prop_status = svn_wc_status_none; /* No separate info */

  /* Find pristine_text_status value */
  switch (old_status->text_status)
    {
      case svn_wc_status_none:
      case svn_wc_status_normal:
      case svn_wc_status_modified:
        (*status)->pristine_text_status = old_status->text_status;
        break;
      case svn_wc_status_conflicted:
      default:
        /* ### Fetch compare data, or fall back to the documented
               not retrieved behavior? */
        (*status)->pristine_text_status = svn_wc_status_none;
        break;
    }

  /* Find pristine_prop_status value */
  switch (old_status->prop_status)
    {
      case svn_wc_status_none:
      case svn_wc_status_normal:
      case svn_wc_status_modified:
        if (old_status->node_status != svn_wc_status_added
            && old_status->node_status != svn_wc_status_deleted
            && old_status->node_status != svn_wc_status_replaced)
          {
            (*status)->pristine_prop_status = old_status->prop_status;
          }
        else
          (*status)->pristine_prop_status = svn_wc_status_none;
        break;
      case svn_wc_status_conflicted:
      default:
        /* ### Fetch compare data, or fall back to the documented
               not retrieved behavior? */
        (*status)->pristine_prop_status = svn_wc_status_none;
        break;
    }

  if (old_status->versioned
      && old_status->conflicted
      && old_status->node_status != svn_wc_status_obstructed
      && (old_status->kind == svn_node_file
          || old_status->node_status != svn_wc_status_missing))
    {
      svn_boolean_t text_conflict_p, prop_conflict_p;

      /* The entry says there was a conflict, but the user might have
         marked it as resolved by deleting the artifact files, so check
         for that. */
      SVN_ERR(svn_wc__internal_conflicted_p(&text_conflict_p,
                                            &prop_conflict_p,
                                            NULL,
                                            wc_ctx->db, local_abspath,
                                            scratch_pool));

      if (text_conflict_p)
        (*status)->text_status = svn_wc_status_conflicted;

      if (prop_conflict_p)
        (*status)->prop_status = svn_wc_status_conflicted;
    }

  return SVN_NO_ERROR;
}



/*** From status.c ***/

struct status4_wrapper_baton
{
  svn_wc_status_func3_t old_func;
  void *old_baton;
  const char *anchor_abspath;
  const char *anchor_relpath;
  svn_wc_context_t *wc_ctx;
};

/* */
static svn_error_t *
status4_wrapper_func(void *baton,
                     const char *local_abspath,
                     const svn_wc_status3_t *status,
                     apr_pool_t *scratch_pool)
{
  struct status4_wrapper_baton *swb = baton;
  svn_wc_status2_t *dup;
  const char *path = local_abspath;

  SVN_ERR(svn_wc__status2_from_3(&dup, status, swb->wc_ctx, local_abspath,
                                 scratch_pool, scratch_pool));

  if (swb->anchor_abspath != NULL)
    {
      path = svn_dirent_join(
                swb->anchor_relpath,
                svn_dirent_skip_ancestor(swb->anchor_abspath, local_abspath),
                scratch_pool);
    }

  return (*swb->old_func)(swb->old_baton, path, dup, scratch_pool);
}


svn_error_t *
svn_wc_get_status_editor5(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
                          svn_depth_t depth,
                          svn_boolean_t get_all,
                          svn_boolean_t no_ignore,
                          svn_boolean_t depth_as_sticky,
                          svn_boolean_t server_performs_filtering,
                          const apr_array_header_t *ignore_patterns,
                          svn_wc_status_func4_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(
    svn_wc__get_status_editor(editor, edit_baton,
                              set_locks_baton,
                              edit_revision,
                              wc_ctx,
                              anchor_abspath,
                              target_basename,
                              depth, get_all,
                              TRUE, /* check_working_copy */
                              no_ignore, depth_as_sticky,
                              server_performs_filtering,
                              ignore_patterns,
                              status_func, status_baton,
                              cancel_func, cancel_baton,
                              result_pool,
                              scratch_pool));
}


svn_error_t *
svn_wc_get_status_editor4(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_depth_t depth,
                          svn_boolean_t get_all,
                          svn_boolean_t no_ignore,
                          const apr_array_header_t *ignore_patterns,
                          svn_wc_status_func3_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  struct status4_wrapper_baton *swb = apr_palloc(pool, sizeof(*swb));
  svn_wc__db_t *wc_db;
  svn_wc_context_t *wc_ctx;
  const char *anchor_abspath;

  swb->old_func = status_func;
  swb->old_baton = status_baton;

  wc_db = svn_wc__adm_get_db(anchor);

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         wc_db, pool));

  swb->wc_ctx = wc_ctx;

  anchor_abspath = svn_wc__adm_access_abspath(anchor);

  if (!svn_dirent_is_absolute(svn_wc_adm_access_path(anchor)))
    {
      swb->anchor_abspath = anchor_abspath;
      swb->anchor_relpath = svn_wc_adm_access_path(anchor);
    }
  else
    {
      swb->anchor_abspath = NULL;
      swb->anchor_relpath = NULL;
    }

  /* Before subversion 1.7 status always handled depth as sticky. 1.7 made
     the output of svn status by default match the result of what would be
     updated by a similar svn update. (Following the documentation) */

  SVN_ERR(svn_wc_get_status_editor5(editor, edit_baton, set_locks_baton,
                                    edit_revision, wc_ctx, anchor_abspath,
                                    target, depth, get_all,
                                    no_ignore,
                                    (depth != svn_depth_unknown) /*as_sticky*/,
                                    FALSE /* server_performs_filtering */,
                                    ignore_patterns,
                                    status4_wrapper_func, swb,
                                    cancel_func, cancel_baton,
                                    pool, pool));

  if (traversal_info)
    {
      const char *local_path = svn_wc_adm_access_path(anchor);
      const char *local_abspath = anchor_abspath;
      if (*target)
        {
          local_path = svn_dirent_join(local_path, target, pool);
          local_abspath = svn_dirent_join(local_abspath, target, pool);
        }

      SVN_ERR(gather_traversal_info(wc_ctx, local_abspath, local_path, depth,
                                    traversal_info, TRUE, TRUE,
                                    pool));
    }

  /* We can't destroy wc_ctx here, because the editor needs it while it's
     driven. */
  return SVN_NO_ERROR;
}

struct status_editor3_compat_baton
{
  svn_wc_status_func2_t old_func;
  void *old_baton;
};

/* */
static svn_error_t *
status_editor3_compat_func(void *baton,
                           const char *path,
                           svn_wc_status2_t *status,
                           apr_pool_t *pool)
{
  struct status_editor3_compat_baton *secb = baton;

  secb->old_func(secb->old_baton, path, status);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_get_status_editor3(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_depth_t depth,
                          svn_boolean_t get_all,
                          svn_boolean_t no_ignore,
                          const apr_array_header_t *ignore_patterns,
                          svn_wc_status_func2_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  /* This baton must live beyond this function. Alloc on heap.  */
  struct status_editor3_compat_baton *secb = apr_palloc(pool, sizeof(*secb));

  secb->old_func = status_func;
  secb->old_baton = status_baton;

  return svn_wc_get_status_editor4(editor, edit_baton, set_locks_baton,
                                   edit_revision, anchor, target, depth,
                                   get_all, no_ignore, ignore_patterns,
                                   status_editor3_compat_func, secb,
                                   cancel_func, cancel_baton, traversal_info,
                                   pool);
}

svn_error_t *
svn_wc_get_status_editor2(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          void **set_locks_baton,
                          svn_revnum_t *edit_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          apr_hash_t *config,
                          svn_boolean_t recurse,
                          svn_boolean_t get_all,
                          svn_boolean_t no_ignore,
                          svn_wc_status_func2_t status_func,
                          void *status_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  apr_array_header_t *ignores;

  SVN_ERR(svn_wc_get_default_ignores(&ignores, config, pool));
  return svn_wc_get_status_editor3(editor,
                                   edit_baton,
                                   set_locks_baton,
                                   edit_revision,
                                   anchor,
                                   target,
                                   SVN_DEPTH_INFINITY_OR_IMMEDIATES(recurse),
                                   get_all,
                                   no_ignore,
                                   ignores,
                                   status_func,
                                   status_baton,
                                   cancel_func,
                                   cancel_baton,
                                   traversal_info,
                                   pool);
}


/* Helpers for deprecated svn_wc_status_editor(), of type
   svn_wc_status_func2_t. */
struct old_status_func_cb_baton
{
  svn_wc_status_func_t original_func;
  void *original_baton;
};

/* */
static void old_status_func_cb(void *baton,
                               const char *path,
                               svn_wc_status2_t *status)
{
  struct old_status_func_cb_baton *b = baton;
  svn_wc_status_t *stat = (svn_wc_status_t *) status;

  b->original_func(b->original_baton, path, stat);
}

svn_error_t *
svn_wc_get_status_editor(const svn_delta_editor_t **editor,
                         void **edit_baton,
                         svn_revnum_t *edit_revision,
                         svn_wc_adm_access_t *anchor,
                         const char *target,
                         apr_hash_t *config,
                         svn_boolean_t recurse,
                         svn_boolean_t get_all,
                         svn_boolean_t no_ignore,
                         svn_wc_status_func_t status_func,
                         void *status_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         svn_wc_traversal_info_t *traversal_info,
                         apr_pool_t *pool)
{
  struct old_status_func_cb_baton *b = apr_pcalloc(pool, sizeof(*b));
  apr_array_header_t *ignores;
  b->original_func = status_func;
  b->original_baton = status_baton;
  SVN_ERR(svn_wc_get_default_ignores(&ignores, config, pool));
  return svn_wc_get_status_editor3(editor, edit_baton, NULL, edit_revision,
                                   anchor, target,
                                   SVN_DEPTH_INFINITY_OR_IMMEDIATES(recurse),
                                   get_all, no_ignore, ignores,
                                   old_status_func_cb, b,
                                   cancel_func, cancel_baton,
                                   traversal_info, pool);
}

svn_error_t *
svn_wc_status(svn_wc_status_t **status,
              const char *path,
              svn_wc_adm_access_t *adm_access,
              apr_pool_t *pool)
{
  svn_wc_status2_t *stat2;

  SVN_ERR(svn_wc_status2(&stat2, path, adm_access, pool));
  *status = (svn_wc_status_t *) stat2;
  return SVN_NO_ERROR;
}


static svn_wc_conflict_description_t *
conflict_description_dup(const svn_wc_conflict_description_t *conflict,
                         apr_pool_t *pool)
{
  svn_wc_conflict_description_t *new_conflict;

  new_conflict = apr_pcalloc(pool, sizeof(*new_conflict));

  /* Shallow copy all members. */
  *new_conflict = *conflict;

  if (conflict->path)
    new_conflict->path = apr_pstrdup(pool, conflict->path);
  if (conflict->property_name)
    new_conflict->property_name = apr_pstrdup(pool, conflict->property_name);
  if (conflict->mime_type)
    new_conflict->mime_type = apr_pstrdup(pool, conflict->mime_type);
  /* NOTE: We cannot make a deep copy of adm_access. */
  if (conflict->base_file)
    new_conflict->base_file = apr_pstrdup(pool, conflict->base_file);
  if (conflict->their_file)
    new_conflict->their_file = apr_pstrdup(pool, conflict->their_file);
  if (conflict->my_file)
    new_conflict->my_file = apr_pstrdup(pool, conflict->my_file);
  if (conflict->merged_file)
    new_conflict->merged_file = apr_pstrdup(pool, conflict->merged_file);
  if (conflict->src_left_version)
    new_conflict->src_left_version =
      svn_wc_conflict_version_dup(conflict->src_left_version, pool);
  if (conflict->src_right_version)
    new_conflict->src_right_version =
      svn_wc_conflict_version_dup(conflict->src_right_version, pool);

  return new_conflict;
}


svn_wc_status2_t *
svn_wc_dup_status2(const svn_wc_status2_t *orig_stat,
                   apr_pool_t *pool)
{
  svn_wc_status2_t *new_stat = apr_palloc(pool, sizeof(*new_stat));

  /* Shallow copy all members. */
  *new_stat = *orig_stat;

  /* Now go back and dup the deep items into this pool. */
  if (orig_stat->entry)
    new_stat->entry = svn_wc_entry_dup(orig_stat->entry, pool);

  if (orig_stat->repos_lock)
    new_stat->repos_lock = svn_lock_dup(orig_stat->repos_lock, pool);

  if (orig_stat->url)
    new_stat->url = apr_pstrdup(pool, orig_stat->url);

  if (orig_stat->ood_last_cmt_author)
    new_stat->ood_last_cmt_author
      = apr_pstrdup(pool, orig_stat->ood_last_cmt_author);

  if (orig_stat->tree_conflict)
    new_stat->tree_conflict
      = conflict_description_dup(orig_stat->tree_conflict, pool);

  /* Return the new hotness. */
  return new_stat;
}

svn_wc_status_t *
svn_wc_dup_status(const svn_wc_status_t *orig_stat,
                  apr_pool_t *pool)
{
  svn_wc_status_t *new_stat = apr_palloc(pool, sizeof(*new_stat));

  /* Shallow copy all members. */
  *new_stat = *orig_stat;

  /* Now go back and dup the deep item into this pool. */
  if (orig_stat->entry)
    new_stat->entry = svn_wc_entry_dup(orig_stat->entry, pool);

  /* Return the new hotness. */
  return new_stat;
}

svn_error_t *
svn_wc_get_ignores(apr_array_header_t **patterns,
                   apr_hash_t *config,
                   svn_wc_adm_access_t *adm_access,
                   apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  svn_wc_adm_access_path(adm_access), pool));

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  SVN_ERR(svn_wc_get_ignores2(patterns, wc_ctx, local_abspath, config, pool,
                              pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_status2(svn_wc_status2_t **status,
               const char *path,
               svn_wc_adm_access_t *adm_access,
               apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;
  svn_wc_status3_t *stat3;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  SVN_ERR(svn_wc_status3(&stat3, wc_ctx, local_abspath, pool, pool));
  SVN_ERR(svn_wc__status2_from_3(status, stat3, wc_ctx, local_abspath,
                                 pool, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}


/*** From update_editor.c ***/

svn_error_t *
svn_wc_add_repos_file3(const char *dst_path,
                       svn_wc_adm_access_t *adm_access,
                       svn_stream_t *new_base_contents,
                       svn_stream_t *new_contents,
                       apr_hash_t *new_base_props,
                       apr_hash_t *new_props,
                       const char *copyfrom_url,
                       svn_revnum_t copyfrom_rev,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       svn_wc_notify_func2_t notify_func,
                       void *notify_baton,
                       apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, dst_path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  SVN_ERR(svn_wc_add_repos_file4(wc_ctx,
                                 local_abspath,
                                 new_base_contents,
                                 new_contents,
                                 new_base_props,
                                 new_props,
                                 copyfrom_url,
                                 copyfrom_rev,
                                 cancel_func, cancel_baton,
                                 pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_add_repos_file2(const char *dst_path,
                       svn_wc_adm_access_t *adm_access,
                       const char *new_text_base_path,
                       const char *new_text_path,
                       apr_hash_t *new_base_props,
                       apr_hash_t *new_props,
                       const char *copyfrom_url,
                       svn_revnum_t copyfrom_rev,
                       apr_pool_t *pool)
{
  svn_stream_t *new_base_contents;
  svn_stream_t *new_contents = NULL;

  SVN_ERR(svn_stream_open_readonly(&new_base_contents, new_text_base_path,
                                   pool, pool));

  if (new_text_path)
    {
      /* NOTE: the specified path may *not* be under version control.
         It is most likely sitting in .svn/tmp/. Thus, we cannot use the
         typical WC functions to access "special", "keywords" or "EOL"
         information. We need to look at the properties given to us. */

      /* If the new file is special, then we can simply open the given
         contents since it is already in normal form. */
      if (svn_hash_gets(new_props, SVN_PROP_SPECIAL) != NULL)
        {
          SVN_ERR(svn_stream_open_readonly(&new_contents, new_text_path,
                                           pool, pool));
        }
      else
        {
          /* The new text contents need to be detrans'd into normal form. */
          svn_subst_eol_style_t eol_style;
          const char *eol_str;
          apr_hash_t *keywords = NULL;
          svn_string_t *list;

          list = svn_hash_gets(new_props, SVN_PROP_KEYWORDS);
          if (list != NULL)
            {
              /* Since we are detranslating, all of the keyword values
                 can be "". */
              SVN_ERR(svn_subst_build_keywords2(&keywords,
                                                list->data,
                                                "", "", 0, "",
                                                pool));
              if (apr_hash_count(keywords) == 0)
                keywords = NULL;
            }

          svn_subst_eol_style_from_value(&eol_style, &eol_str,
                                         svn_hash_gets(new_props,
                                                       SVN_PROP_EOL_STYLE));

          if (svn_subst_translation_required(eol_style, eol_str, keywords,
                                             FALSE, FALSE))
            {
              SVN_ERR(svn_subst_stream_detranslated(&new_contents,
                                                    new_text_path,
                                                    eol_style, eol_str,
                                                    FALSE,
                                                    keywords,
                                                    FALSE,
                                                    pool));
            }
          else
            {
              SVN_ERR(svn_stream_open_readonly(&new_contents, new_text_path,
                                               pool, pool));
            }
        }
    }

  SVN_ERR(svn_wc_add_repos_file3(dst_path, adm_access,
                                 new_base_contents, new_contents,
                                 new_base_props, new_props,
                                 copyfrom_url, copyfrom_rev,
                                 NULL, NULL, NULL, NULL,
                                 pool));

  /* The API contract states that the text files will be removed upon
     successful completion. add_repos_file3() does not remove the files
     since it only has streams on them. Toss 'em now. */
  svn_error_clear(svn_io_remove_file(new_text_base_path, pool));
  if (new_text_path)
    svn_error_clear(svn_io_remove_file(new_text_path, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_add_repos_file(const char *dst_path,
                      svn_wc_adm_access_t *adm_access,
                      const char *new_text_path,
                      apr_hash_t *new_props,
                      const char *copyfrom_url,
                      svn_revnum_t copyfrom_rev,
                      apr_pool_t *pool)
{
  return svn_wc_add_repos_file2(dst_path, adm_access,
                                new_text_path, NULL,
                                new_props, NULL,
                                copyfrom_url, copyfrom_rev,
                                pool);
}

svn_error_t *
svn_wc_get_actual_target(const char *path,
                         const char **anchor,
                         const char **target,
                         apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;

  SVN_ERR(svn_wc_context_create(&wc_ctx, NULL, pool, pool));
  SVN_ERR(svn_wc_get_actual_target2(anchor, target, wc_ctx, path, pool, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

/* This function has no internal variant as its behavior on switched
   non-directories is not what you would expect. But this happens to
   be the legacy behavior of this function. */
svn_error_t *
svn_wc_is_wc_root2(svn_boolean_t *wc_root,
                   svn_wc_context_t *wc_ctx,
                   const char *local_abspath,
                   apr_pool_t *scratch_pool)
{
  svn_boolean_t is_root;
  svn_boolean_t is_switched;
  svn_node_kind_t kind;
  svn_error_t *err;
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  err = svn_wc__db_is_switched(&is_root, &is_switched, &kind,
                               wc_ctx->db, local_abspath, scratch_pool);

  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND &&
          err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
        return svn_error_trace(err);

      return svn_error_create(SVN_ERR_ENTRY_NOT_FOUND, err, err->message);
    }

  *wc_root = is_root || (kind == svn_node_dir && is_switched);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_is_wc_root(svn_boolean_t *wc_root,
                  const char *path,
                  svn_wc_adm_access_t *adm_access,
                  apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;
  svn_error_t *err;

  /* Subversion <= 1.6 said that '.' or a drive root is a WC root. */
  if (svn_path_is_empty(path) || svn_dirent_is_root(path, strlen(path)))
    {
      *wc_root = TRUE;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  err = svn_wc_is_wc_root2(wc_root, wc_ctx, local_abspath, pool);

  if (err
      && (err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY
          || err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND))
    {
      /* Subversion <= 1.6 said that an unversioned path is a WC root. */
      svn_error_clear(err);
      *wc_root = TRUE;
    }
  else
    SVN_ERR(err);

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}


svn_error_t *
svn_wc_get_update_editor4(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_revnum_t *target_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
                          svn_boolean_t use_commit_times,
                          svn_depth_t depth,
                          svn_boolean_t depth_is_sticky,
                          svn_boolean_t allow_unver_obstructions,
                          svn_boolean_t adds_as_modification,
                          svn_boolean_t server_performs_filtering,
                          svn_boolean_t clean_checkout,
                          const char *diff3_cmd,
                          const apr_array_header_t *preserved_exts,
                          svn_wc_dirents_func_t fetch_dirents_func,
                          void *fetch_dirents_baton,
                          svn_wc_conflict_resolver_func2_t conflict_func,
                          void *conflict_baton,
                          svn_wc_external_update_t external_func,
                          void *external_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(
    svn_wc__get_update_editor(editor, edit_baton,
                              target_revision,
                              wc_ctx,
                              anchor_abspath,
                              target_basename, NULL,
                              use_commit_times,
                              depth, depth_is_sticky,
                              allow_unver_obstructions,
                              adds_as_modification,
                              server_performs_filtering,
                              clean_checkout,
                              diff3_cmd,
                              preserved_exts,
                              fetch_dirents_func, fetch_dirents_baton,
                              conflict_func, conflict_baton,
                              external_func, external_baton,
                              cancel_func, cancel_baton,
                              notify_func, notify_baton,
                              result_pool, scratch_pool));
}


svn_error_t *
svn_wc_get_update_editor3(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_boolean_t use_commit_times,
                          svn_depth_t depth,
                          svn_boolean_t depth_is_sticky,
                          svn_boolean_t allow_unver_obstructions,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_conflict_resolver_func_t conflict_func,
                          void *conflict_baton,
                          svn_wc_get_file_t fetch_func,
                          void *fetch_baton,
                          const char *diff3_cmd,
                          const apr_array_header_t *preserved_exts,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *db = svn_wc__adm_get_db(anchor);
  svn_wc_external_update_t external_func = NULL;
  struct traversal_info_update_baton *eb = NULL;
  struct conflict_func_1to2_baton *cfw = NULL;

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, db, pool));

  if (traversal_info)
    {
      eb = apr_palloc(pool, sizeof(*eb));
      eb->db = db;
      eb->traversal = traversal_info;
      external_func = traversal_info_update;
    }

  if (conflict_func)
    {
      cfw = apr_pcalloc(pool, sizeof(*cfw));
      cfw->inner_func = conflict_func;
      cfw->inner_baton = conflict_baton;
    }

  if (diff3_cmd)
    SVN_ERR(svn_path_cstring_to_utf8(&diff3_cmd, diff3_cmd, pool));

  SVN_ERR(svn_wc_get_update_editor4(editor, edit_baton,
                                    target_revision,
                                    wc_ctx,
                                    svn_wc__adm_access_abspath(anchor),
                                    target,
                                    use_commit_times,
                                    depth, depth_is_sticky,
                                    allow_unver_obstructions,
                                    TRUE /* adds_as_modification */,
                                    FALSE /* server_performs_filtering */,
                                    FALSE /* clean_checkout */,
                                    diff3_cmd,
                                    preserved_exts,
                                    NULL, NULL, /* fetch_dirents_func, baton */
                                    conflict_func ? conflict_func_1to2_wrapper
                                                  : NULL,
                                    cfw,
                                    external_func, eb,
                                    cancel_func, cancel_baton,
                                    notify_func, notify_baton,
                                    pool, pool));

  /* We can't destroy wc_ctx here, because the editor needs it while it's
     driven. */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_get_update_editor2(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          svn_boolean_t use_commit_times,
                          svn_boolean_t recurse,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          const char *diff3_cmd,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  return svn_wc_get_update_editor3(target_revision, anchor, target,
                                   use_commit_times,
                                   SVN_DEPTH_INFINITY_OR_FILES(recurse), FALSE,
                                   FALSE, notify_func, notify_baton,
                                   cancel_func, cancel_baton, NULL, NULL,
                                   NULL, NULL,
                                   diff3_cmd, NULL, editor, edit_baton,
                                   traversal_info, pool);
}

svn_error_t *
svn_wc_get_update_editor(svn_revnum_t *target_revision,
                         svn_wc_adm_access_t *anchor,
                         const char *target,
                         svn_boolean_t use_commit_times,
                         svn_boolean_t recurse,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         const char *diff3_cmd,
                         const svn_delta_editor_t **editor,
                         void **edit_baton,
                         svn_wc_traversal_info_t *traversal_info,
                         apr_pool_t *pool)
{
  /* This baton must live beyond this function. Alloc on heap.  */
  struct compat_notify_baton_t *nb = apr_palloc(pool, sizeof(*nb));

  nb->func = notify_func;
  nb->baton = notify_baton;

  return svn_wc_get_update_editor3(target_revision, anchor, target,
                                   use_commit_times,
                                   SVN_DEPTH_INFINITY_OR_FILES(recurse), FALSE,
                                   FALSE, compat_call_notify_func, nb,
                                   cancel_func, cancel_baton, NULL, NULL,
                                   NULL, NULL,
                                   diff3_cmd, NULL, editor, edit_baton,
                                   traversal_info, pool);
}


svn_error_t *
svn_wc_get_switch_editor4(const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_revnum_t *target_revision,
                          svn_wc_context_t *wc_ctx,
                          const char *anchor_abspath,
                          const char *target_basename,
                          const char *switch_url,
                          svn_boolean_t use_commit_times,
                          svn_depth_t depth,
                          svn_boolean_t depth_is_sticky,
                          svn_boolean_t allow_unver_obstructions,
                          svn_boolean_t server_performs_filtering,
                          const char *diff3_cmd,
                          const apr_array_header_t *preserved_exts,
                          svn_wc_dirents_func_t fetch_dirents_func,
                          void *fetch_dirents_baton,
                          svn_wc_conflict_resolver_func2_t conflict_func,
                          void *conflict_baton,
                          svn_wc_external_update_t external_func,
                          void *external_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(
    svn_wc__get_switch_editor(editor, edit_baton,
                              target_revision,
                              wc_ctx,
                              anchor_abspath, target_basename,
                              switch_url, NULL,
                              use_commit_times,
                              depth, depth_is_sticky,
                              allow_unver_obstructions,
                              server_performs_filtering,
                              diff3_cmd,
                              preserved_exts,
                              fetch_dirents_func, fetch_dirents_baton,
                              conflict_func, conflict_baton,
                              external_func, external_baton,
                              cancel_func, cancel_baton,
                              notify_func, notify_baton,
                              result_pool, scratch_pool));
}


svn_error_t *
svn_wc_get_switch_editor3(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          const char *switch_url,
                          svn_boolean_t use_commit_times,
                          svn_depth_t depth,
                          svn_boolean_t depth_is_sticky,
                          svn_boolean_t allow_unver_obstructions,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          svn_wc_conflict_resolver_func_t conflict_func,
                          void *conflict_baton,
                          const char *diff3_cmd,
                          const apr_array_header_t *preserved_exts,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *db = svn_wc__adm_get_db(anchor);
  svn_wc_external_update_t external_func = NULL;
  struct traversal_info_update_baton *eb = NULL;
  struct conflict_func_1to2_baton *cfw = NULL;

  SVN_ERR_ASSERT(switch_url && svn_uri_is_canonical(switch_url, pool));

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, db, pool));

  if (traversal_info)
    {
      eb = apr_palloc(pool, sizeof(*eb));
      eb->db = db;
      eb->traversal = traversal_info;
      external_func = traversal_info_update;
    }

  if (conflict_func)
    {
      cfw = apr_pcalloc(pool, sizeof(*cfw));
      cfw->inner_func = conflict_func;
      cfw->inner_baton = conflict_baton;
    }

  if (diff3_cmd)
    SVN_ERR(svn_path_cstring_to_utf8(&diff3_cmd, diff3_cmd, pool));

  SVN_ERR(svn_wc_get_switch_editor4(editor, edit_baton,
                                    target_revision,
                                    wc_ctx,
                                    svn_wc__adm_access_abspath(anchor),
                                    target, switch_url,
                                    use_commit_times,
                                    depth, depth_is_sticky,
                                    allow_unver_obstructions,
                                    FALSE /* server_performs_filtering */,
                                    diff3_cmd,
                                    preserved_exts,
                                    NULL, NULL, /* fetch_dirents_func, baton */
                                    conflict_func ? conflict_func_1to2_wrapper
                                                  : NULL,
                                    cfw,
                                    external_func, eb,
                                    cancel_func, cancel_baton,
                                    notify_func, notify_baton,
                                    pool, pool));

  /* We can't destroy wc_ctx here, because the editor needs it while it's
     driven. */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_get_switch_editor2(svn_revnum_t *target_revision,
                          svn_wc_adm_access_t *anchor,
                          const char *target,
                          const char *switch_url,
                          svn_boolean_t use_commit_times,
                          svn_boolean_t recurse,
                          svn_wc_notify_func2_t notify_func,
                          void *notify_baton,
                          svn_cancel_func_t cancel_func,
                          void *cancel_baton,
                          const char *diff3_cmd,
                          const svn_delta_editor_t **editor,
                          void **edit_baton,
                          svn_wc_traversal_info_t *traversal_info,
                          apr_pool_t *pool)
{
  SVN_ERR_ASSERT(switch_url);

  return svn_wc_get_switch_editor3(target_revision, anchor, target,
                                   switch_url, use_commit_times,
                                   SVN_DEPTH_INFINITY_OR_FILES(recurse), FALSE,
                                   FALSE, notify_func, notify_baton,
                                   cancel_func, cancel_baton,
                                   NULL, NULL, diff3_cmd,
                                   NULL, editor, edit_baton, traversal_info,
                                   pool);
}

svn_error_t *
svn_wc_get_switch_editor(svn_revnum_t *target_revision,
                         svn_wc_adm_access_t *anchor,
                         const char *target,
                         const char *switch_url,
                         svn_boolean_t use_commit_times,
                         svn_boolean_t recurse,
                         svn_wc_notify_func_t notify_func,
                         void *notify_baton,
                         svn_cancel_func_t cancel_func,
                         void *cancel_baton,
                         const char *diff3_cmd,
                         const svn_delta_editor_t **editor,
                         void **edit_baton,
                         svn_wc_traversal_info_t *traversal_info,
                         apr_pool_t *pool)
{
  /* This baton must live beyond this function. Alloc on heap.  */
  struct compat_notify_baton_t *nb = apr_palloc(pool, sizeof(*nb));

  nb->func = notify_func;
  nb->baton = notify_baton;

  return svn_wc_get_switch_editor3(target_revision, anchor, target,
                                   switch_url, use_commit_times,
                                   SVN_DEPTH_INFINITY_OR_FILES(recurse), FALSE,
                                   FALSE, compat_call_notify_func, nb,
                                   cancel_func, cancel_baton,
                                   NULL, NULL, diff3_cmd,
                                   NULL, editor, edit_baton, traversal_info,
                                   pool);
}


svn_error_t *
svn_wc_external_item_create(const svn_wc_external_item2_t **item,
                            apr_pool_t *pool)
{
  *item = apr_pcalloc(pool, sizeof(svn_wc_external_item2_t));
  return SVN_NO_ERROR;
}

svn_wc_external_item_t *
svn_wc_external_item_dup(const svn_wc_external_item_t *item,
                         apr_pool_t *pool)
{
  svn_wc_external_item_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->target_dir)
    new_item->target_dir = apr_pstrdup(pool, new_item->target_dir);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  return new_item;
}


svn_wc_traversal_info_t *
svn_wc_init_traversal_info(apr_pool_t *pool)
{
  svn_wc_traversal_info_t *ti = apr_palloc(pool, sizeof(*ti));

  ti->pool           = pool;
  ti->externals_old  = apr_hash_make(pool);
  ti->externals_new  = apr_hash_make(pool);
  ti->depths         = apr_hash_make(pool);

  return ti;
}


void
svn_wc_edited_externals(apr_hash_t **externals_old,
                        apr_hash_t **externals_new,
                        svn_wc_traversal_info_t *traversal_info)
{
  *externals_old = traversal_info->externals_old;
  *externals_new = traversal_info->externals_new;
}


void
svn_wc_traversed_depths(apr_hash_t **depths,
                        svn_wc_traversal_info_t *traversal_info)
{
  *depths = traversal_info->depths;
}


/*** From lock.c ***/

/* To preserve API compatibility with Subversion 1.0.0 */
svn_error_t *
svn_wc_adm_open(svn_wc_adm_access_t **adm_access,
                svn_wc_adm_access_t *associated,
                const char *path,
                svn_boolean_t write_lock,
                svn_boolean_t tree_lock,
                apr_pool_t *pool)
{
  return svn_wc_adm_open3(adm_access, associated, path, write_lock,
                          (tree_lock ? -1 : 0), NULL, NULL, pool);
}

svn_error_t *
svn_wc_adm_open2(svn_wc_adm_access_t **adm_access,
                 svn_wc_adm_access_t *associated,
                 const char *path,
                 svn_boolean_t write_lock,
                 int levels_to_lock,
                 apr_pool_t *pool)
{
  return svn_wc_adm_open3(adm_access, associated, path, write_lock,
                          levels_to_lock, NULL, NULL, pool);
}

svn_error_t *
svn_wc_adm_probe_open(svn_wc_adm_access_t **adm_access,
                      svn_wc_adm_access_t *associated,
                      const char *path,
                      svn_boolean_t write_lock,
                      svn_boolean_t tree_lock,
                      apr_pool_t *pool)
{
  return svn_wc_adm_probe_open3(adm_access, associated, path,
                                write_lock, (tree_lock ? -1 : 0),
                                NULL, NULL, pool);
}


svn_error_t *
svn_wc_adm_probe_open2(svn_wc_adm_access_t **adm_access,
                       svn_wc_adm_access_t *associated,
                       const char *path,
                       svn_boolean_t write_lock,
                       int levels_to_lock,
                       apr_pool_t *pool)
{
  return svn_wc_adm_probe_open3(adm_access, associated, path, write_lock,
                                levels_to_lock, NULL, NULL, pool);
}

svn_error_t *
svn_wc_adm_probe_try2(svn_wc_adm_access_t **adm_access,
                      svn_wc_adm_access_t *associated,
                      const char *path,
                      svn_boolean_t write_lock,
                      int levels_to_lock,
                      apr_pool_t *pool)
{
  return svn_wc_adm_probe_try3(adm_access, associated, path, write_lock,
                               levels_to_lock, NULL, NULL, pool);
}

svn_error_t *
svn_wc_adm_probe_try(svn_wc_adm_access_t **adm_access,
                     svn_wc_adm_access_t *associated,
                     const char *path,
                     svn_boolean_t write_lock,
                     svn_boolean_t tree_lock,
                     apr_pool_t *pool)
{
  return svn_wc_adm_probe_try3(adm_access, associated, path, write_lock,
                               (tree_lock ? -1 : 0), NULL, NULL, pool);
}

svn_error_t *
svn_wc_adm_close(svn_wc_adm_access_t *adm_access)
{
  /* This is the only pool we have access to. */
  apr_pool_t *scratch_pool = svn_wc_adm_access_pool(adm_access);

  return svn_wc_adm_close2(adm_access, scratch_pool);
}

svn_error_t *
svn_wc_locked(svn_boolean_t *locked,
              const char *path,
              apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc_context_create(&wc_ctx, NULL, pool, pool));

  SVN_ERR(svn_wc_locked2(NULL, locked, wc_ctx, local_abspath, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_check_wc(const char *path,
                int *wc_format,
                apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc_context_create(&wc_ctx, NULL, pool, pool));

  SVN_ERR(svn_wc_check_wc2(wc_format, wc_ctx, local_abspath, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}


/*** From translate.c ***/

svn_error_t *
svn_wc_translated_file(const char **xlated_p,
                       const char *vfile,
                       svn_wc_adm_access_t *adm_access,
                       svn_boolean_t force_repair,
                       apr_pool_t *pool)
{
  return svn_wc_translated_file2(xlated_p, vfile, vfile, adm_access,
                                 SVN_WC_TRANSLATE_TO_NF
                                 | (force_repair ?
                                    SVN_WC_TRANSLATE_FORCE_EOL_REPAIR : 0),
                                 pool);
}

svn_error_t *
svn_wc_translated_stream(svn_stream_t **stream,
                         const char *path,
                         const char *versioned_file,
                         svn_wc_adm_access_t *adm_access,
                         apr_uint32_t flags,
                         apr_pool_t *pool)
{
  const char *local_abspath;
  const char *versioned_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_dirent_get_absolute(&versioned_abspath, versioned_file, pool));

  return svn_error_trace(
    svn_wc__internal_translated_stream(stream, svn_wc__adm_get_db(adm_access),
                                       local_abspath, versioned_abspath, flags,
                                       pool, pool));
}

svn_error_t *
svn_wc_translated_file2(const char **xlated_path,
                        const char *src,
                        const char *versioned_file,
                        svn_wc_adm_access_t *adm_access,
                        apr_uint32_t flags,
                        apr_pool_t *pool)
{
  const char *versioned_abspath;
  const char *root;
  const char *tmp_root;
  const char *src_abspath;

  SVN_ERR(svn_dirent_get_absolute(&versioned_abspath, versioned_file, pool));
  SVN_ERR(svn_dirent_get_absolute(&src_abspath, src, pool));

  SVN_ERR(svn_wc__internal_translated_file(xlated_path, src_abspath,
                                           svn_wc__adm_get_db(adm_access),
                                           versioned_abspath,
                                           flags, NULL, NULL, pool, pool));

  if (strcmp(*xlated_path, src_abspath) == 0)
    *xlated_path = src;
  else if (! svn_dirent_is_absolute(versioned_file))
    {
      SVN_ERR(svn_io_temp_dir(&tmp_root, pool));
      if (! svn_dirent_is_child(tmp_root, *xlated_path, pool))
        {
          SVN_ERR(svn_dirent_get_absolute(&root, "", pool));

          if (svn_dirent_is_child(root, *xlated_path, pool))
            *xlated_path = svn_dirent_is_child(root, *xlated_path, pool);
        }
    }

  return SVN_NO_ERROR;
}

/*** From relocate.c ***/
svn_error_t *
svn_wc_relocate3(const char *path,
                 svn_wc_adm_access_t *adm_access,
                 const char *from,
                 const char *to,
                 svn_boolean_t recurse,
                 svn_wc_relocation_validator3_t validator,
                 void *validator_baton,
                 apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;

  if (! recurse)
    SVN_ERR(svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                             _("Non-recursive relocation not supported")));

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  SVN_ERR(svn_wc_relocate4(wc_ctx, local_abspath, from, to,
                           validator, validator_baton, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

/* Compatibility baton and wrapper. */
struct compat2_baton {
  svn_wc_relocation_validator2_t validator;
  void *baton;
};

/* Compatibility baton and wrapper. */
struct compat_baton {
  svn_wc_relocation_validator_t validator;
  void *baton;
};

/* This implements svn_wc_relocate_validator3_t. */
static svn_error_t *
compat2_validator(void *baton,
                  const char *uuid,
                  const char *url,
                  const char *root_url,
                  apr_pool_t *pool)
{
  struct compat2_baton *cb = baton;
  /* The old callback type doesn't set root_url. */
  return cb->validator(cb->baton, uuid,
                       (root_url ? root_url : url), (root_url != NULL),
                       pool);
}

/* This implements svn_wc_relocate_validator3_t. */
static svn_error_t *
compat_validator(void *baton,
                 const char *uuid,
                 const char *url,
                 const char *root_url,
                 apr_pool_t *pool)
{
  struct compat_baton *cb = baton;
  /* The old callback type doesn't allow uuid to be NULL. */
  if (uuid)
    return cb->validator(cb->baton, uuid, url);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_relocate2(const char *path,
                 svn_wc_adm_access_t *adm_access,
                 const char *from,
                 const char *to,
                 svn_boolean_t recurse,
                 svn_wc_relocation_validator2_t validator,
                 void *validator_baton,
                 apr_pool_t *pool)
{
  struct compat2_baton cb;

  cb.validator = validator;
  cb.baton = validator_baton;

  return svn_wc_relocate3(path, adm_access, from, to, recurse,
                          compat2_validator, &cb, pool);
}

svn_error_t *
svn_wc_relocate(const char *path,
                svn_wc_adm_access_t *adm_access,
                const char *from,
                const char *to,
                svn_boolean_t recurse,
                svn_wc_relocation_validator_t validator,
                void *validator_baton,
                apr_pool_t *pool)
{
  struct compat_baton cb;

  cb.validator = validator;
  cb.baton = validator_baton;

  return svn_wc_relocate3(path, adm_access, from, to, recurse,
                          compat_validator, &cb, pool);
}


/*** From log.c / cleanup.c ***/

svn_error_t *
svn_wc_cleanup3(svn_wc_context_t *wc_ctx,
                const char *local_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  return svn_error_trace(
            svn_wc_cleanup4(wc_ctx,
                            local_abspath,
                            TRUE /* break_locks */,
                            TRUE /* fix_recorded_timestamps */,
                            TRUE /* clear_dav_cache */,
                            TRUE /* clean_pristines */,
                            cancel_func, cancel_baton,
                            NULL, NULL /* notify */,
                            scratch_pool));
}

svn_error_t *
svn_wc_cleanup2(const char *path,
                const char *diff3_cmd,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc_context_create(&wc_ctx, NULL, pool, pool));

  SVN_ERR(svn_wc_cleanup3(wc_ctx, local_abspath, cancel_func,
                          cancel_baton, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_cleanup(const char *path,
               svn_wc_adm_access_t *optional_adm_access,
               const char *diff3_cmd,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *pool)
{
  return svn_wc_cleanup2(path, diff3_cmd, cancel_func, cancel_baton, pool);
}

/*** From questions.c ***/

svn_error_t *
svn_wc_has_binary_prop(svn_boolean_t *has_binary_prop,
                       const char *path,
                       svn_wc_adm_access_t *adm_access,
                       apr_pool_t *pool)
{
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
  const char *local_abspath;
  const svn_string_t *value;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));

  SVN_ERR(svn_wc__internal_propget(&value, db, local_abspath,
                                   SVN_PROP_MIME_TYPE,
                                   pool, pool));

  if (value && (svn_mime_type_is_binary(value->data)))
    *has_binary_prop = TRUE;
  else
    *has_binary_prop = FALSE;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_conflicted_p2(svn_boolean_t *text_conflicted_p,
                     svn_boolean_t *prop_conflicted_p,
                     svn_boolean_t *tree_conflicted_p,
                     const char *path,
                     svn_wc_adm_access_t *adm_access,
                     apr_pool_t *pool)
{
  const char *local_abspath;
  svn_wc_context_t *wc_ctx;
  svn_error_t *err;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, path, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */,
                                         svn_wc__adm_get_db(adm_access),
                                         pool));

  err = svn_wc_conflicted_p3(text_conflicted_p, prop_conflicted_p,
                             tree_conflicted_p, wc_ctx, local_abspath, pool);

  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);

      if (text_conflicted_p)
        *text_conflicted_p = FALSE;
      if (prop_conflicted_p)
        *prop_conflicted_p = FALSE;
      if (tree_conflicted_p)
        *tree_conflicted_p = FALSE;
    }
  else if (err)
    return err;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_conflicted_p(svn_boolean_t *text_conflicted_p,
                    svn_boolean_t *prop_conflicted_p,
                    const char *dir_path,
                    const svn_wc_entry_t *entry,
                    apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *path;

  *text_conflicted_p = FALSE;
  *prop_conflicted_p = FALSE;

  if (entry->conflict_old)
    {
      path = svn_dirent_join(dir_path, entry->conflict_old, pool);
      SVN_ERR(svn_io_check_path(path, &kind, pool));
      *text_conflicted_p = (kind == svn_node_file);
    }

  if ((! *text_conflicted_p) && (entry->conflict_new))
    {
      path = svn_dirent_join(dir_path, entry->conflict_new, pool);
      SVN_ERR(svn_io_check_path(path, &kind, pool));
      *text_conflicted_p = (kind == svn_node_file);
    }

  if ((! *text_conflicted_p) && (entry->conflict_wrk))
    {
      path = svn_dirent_join(dir_path, entry->conflict_wrk, pool);
      SVN_ERR(svn_io_check_path(path, &kind, pool));
      *text_conflicted_p = (kind == svn_node_file);
    }

  if (entry->prejfile)
    {
      path = svn_dirent_join(dir_path, entry->prejfile, pool);
      SVN_ERR(svn_io_check_path(path, &kind, pool));
      *prop_conflicted_p = (kind == svn_node_file);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc_text_modified_p(svn_boolean_t *modified_p,
                       const char *filename,
                       svn_boolean_t force_comparison,
                       svn_wc_adm_access_t *adm_access,
                       apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, filename, pool));
  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, db, pool));

  SVN_ERR(svn_wc_text_modified_p2(modified_p, wc_ctx, local_abspath,
                                  force_comparison, pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}


/*** From copy.c ***/
svn_error_t *
svn_wc_copy2(const char *src,
             svn_wc_adm_access_t *dst_parent,
             const char *dst_basename,
             svn_cancel_func_t cancel_func,
             void *cancel_baton,
             svn_wc_notify_func2_t notify_func,
             void *notify_baton,
             apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *wc_db = svn_wc__adm_get_db(dst_parent);
  const char *src_abspath;
  const char *dst_abspath;

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, wc_db, pool));
  SVN_ERR(svn_dirent_get_absolute(&src_abspath, src, pool));

  dst_abspath = svn_dirent_join(svn_wc__adm_access_abspath(dst_parent),
                                dst_basename, pool);

  SVN_ERR(svn_wc_copy3(wc_ctx,
                       src_abspath,
                       dst_abspath,
                       FALSE /* metadata_only */,
                       cancel_func, cancel_baton,
                       notify_func, notify_baton,
                       pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_copy(const char *src_path,
            svn_wc_adm_access_t *dst_parent,
            const char *dst_basename,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func_t notify_func,
            void *notify_baton,
            apr_pool_t *pool)
{
  struct compat_notify_baton_t nb;

  nb.func = notify_func;
  nb.baton = notify_baton;

  return svn_wc_copy2(src_path, dst_parent, dst_basename, cancel_func,
                      cancel_baton, compat_call_notify_func,
                      &nb, pool);
}


/*** From merge.c ***/

svn_error_t *
svn_wc_merge4(enum svn_wc_merge_outcome_t *merge_outcome,
              svn_wc_context_t *wc_ctx,
              const char *left_abspath,
              const char *right_abspath,
              const char *target_abspath,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              const svn_wc_conflict_version_t *left_version,
              const svn_wc_conflict_version_t *right_version,
              svn_boolean_t dry_run,
              const char *diff3_cmd,
              const apr_array_header_t *merge_options,
              const apr_array_header_t *prop_diff,
              svn_wc_conflict_resolver_func2_t conflict_func,
              void *conflict_baton,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  return svn_error_trace(
            svn_wc_merge5(merge_outcome,
                          NULL /* merge_props_outcome */,
                          wc_ctx,
                          left_abspath,
                          right_abspath,
                          target_abspath,
                          left_label,
                          right_label,
                          target_label,
                          left_version,
                          right_version,
                          dry_run,
                          diff3_cmd,
                          merge_options,
                          NULL /* original_props */,
                          prop_diff,
                          conflict_func, conflict_baton,
                          cancel_func, cancel_baton,
                          scratch_pool));
}

svn_error_t *
svn_wc_merge3(enum svn_wc_merge_outcome_t *merge_outcome,
              const char *left,
              const char *right,
              const char *merge_target,
              svn_wc_adm_access_t *adm_access,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              svn_boolean_t dry_run,
              const char *diff3_cmd,
              const apr_array_header_t *merge_options,
              const apr_array_header_t *prop_diff,
              svn_wc_conflict_resolver_func_t conflict_func,
              void *conflict_baton,
              apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
  const char *left_abspath, *right_abspath, *target_abspath;
  struct conflict_func_1to2_baton cfw;

  SVN_ERR(svn_dirent_get_absolute(&left_abspath, left, pool));
  SVN_ERR(svn_dirent_get_absolute(&right_abspath, right, pool));
  SVN_ERR(svn_dirent_get_absolute(&target_abspath, merge_target, pool));

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL /* config */, db, pool));

  cfw.inner_func = conflict_func;
  cfw.inner_baton = conflict_baton;

  if (diff3_cmd)
    SVN_ERR(svn_path_cstring_to_utf8(&diff3_cmd, diff3_cmd, pool));

  SVN_ERR(svn_wc_merge4(merge_outcome,
                        wc_ctx,
                        left_abspath,
                        right_abspath,
                        target_abspath,
                        left_label,
                        right_label,
                        target_label,
                        NULL,
                        NULL,
                        dry_run,
                        diff3_cmd,
                        merge_options,
                        prop_diff,
                        conflict_func ? conflict_func_1to2_wrapper : NULL,
                        &cfw,
                        NULL, NULL,
                        pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_merge2(enum svn_wc_merge_outcome_t *merge_outcome,
              const char *left,
              const char *right,
              const char *merge_target,
              svn_wc_adm_access_t *adm_access,
              const char *left_label,
              const char *right_label,
              const char *target_label,
              svn_boolean_t dry_run,
              const char *diff3_cmd,
              const apr_array_header_t *merge_options,
              apr_pool_t *pool)
{
  return svn_wc_merge3(merge_outcome,
                       left, right, merge_target, adm_access,
                       left_label, right_label, target_label,
                       dry_run, diff3_cmd, merge_options, NULL,
                       NULL, NULL, pool);
}

svn_error_t *
svn_wc_merge(const char *left,
             const char *right,
             const char *merge_target,
             svn_wc_adm_access_t *adm_access,
             const char *left_label,
             const char *right_label,
             const char *target_label,
             svn_boolean_t dry_run,
             enum svn_wc_merge_outcome_t *merge_outcome,
             const char *diff3_cmd,
             apr_pool_t *pool)
{
  return svn_wc_merge3(merge_outcome,
                       left, right, merge_target, adm_access,
                       left_label, right_label, target_label,
                       dry_run, diff3_cmd, NULL, NULL, NULL,
                       NULL, pool);
}


/*** From util.c ***/

svn_wc_conflict_version_t *
svn_wc_conflict_version_create(const char *repos_url,
                               const char *path_in_repos,
                               svn_revnum_t peg_rev,
                               svn_node_kind_t node_kind,
                               apr_pool_t *pool)
{
  return svn_wc_conflict_version_create2(repos_url, NULL, path_in_repos,
                                         peg_rev, node_kind, pool);
}

svn_wc_conflict_description_t *
svn_wc_conflict_description_create_text(const char *path,
                                        svn_wc_adm_access_t *adm_access,
                                        apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict;

  conflict = apr_pcalloc(pool, sizeof(*conflict));
  conflict->path = path;
  conflict->node_kind = svn_node_file;
  conflict->kind = svn_wc_conflict_kind_text;
  conflict->access = adm_access;
  conflict->action = svn_wc_conflict_action_edit;
  conflict->reason = svn_wc_conflict_reason_edited;
  return conflict;
}

svn_wc_conflict_description_t *
svn_wc_conflict_description_create_prop(const char *path,
                                        svn_wc_adm_access_t *adm_access,
                                        svn_node_kind_t node_kind,
                                        const char *property_name,
                                        apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict;

  conflict = apr_pcalloc(pool, sizeof(*conflict));
  conflict->path = path;
  conflict->node_kind = node_kind;
  conflict->kind = svn_wc_conflict_kind_property;
  conflict->access = adm_access;
  conflict->property_name = property_name;
  return conflict;
}

svn_wc_conflict_description_t *
svn_wc_conflict_description_create_tree(
                            const char *path,
                            svn_wc_adm_access_t *adm_access,
                            svn_node_kind_t node_kind,
                            svn_wc_operation_t operation,
                            svn_wc_conflict_version_t *src_left_version,
                            svn_wc_conflict_version_t *src_right_version,
                            apr_pool_t *pool)
{
  svn_wc_conflict_description_t *conflict;

  conflict = apr_pcalloc(pool, sizeof(*conflict));
  conflict->path = path;
  conflict->node_kind = node_kind;
  conflict->kind = svn_wc_conflict_kind_tree;
  conflict->access = adm_access;
  conflict->operation = operation;
  conflict->src_left_version = src_left_version;
  conflict->src_right_version = src_right_version;
  return conflict;
}


/*** From revision_status.c ***/

svn_error_t *
svn_wc_revision_status(svn_wc_revision_status_t **result_p,
                       const char *wc_path,
                       const char *trail_url,
                       svn_boolean_t committed,
                       svn_cancel_func_t cancel_func,
                       void *cancel_baton,
                       apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  const char *local_abspath;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, wc_path, pool));
  SVN_ERR(svn_wc_context_create(&wc_ctx, NULL /* config */, pool, pool));

  SVN_ERR(svn_wc_revision_status2(result_p, wc_ctx, local_abspath, trail_url,
                                  committed, cancel_func, cancel_baton, pool,
                                  pool));

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

/*** From crop.c ***/
svn_error_t *
svn_wc_crop_tree(svn_wc_adm_access_t *anchor,
                 const char *target,
                 svn_depth_t depth,
                 svn_wc_notify_func2_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool)
{
  svn_wc_context_t *wc_ctx;
  svn_wc__db_t *db = svn_wc__adm_get_db(anchor);
  const char *local_abspath;

  local_abspath = svn_dirent_join(svn_wc__adm_access_abspath(anchor),
                                  target, pool);

  SVN_ERR(svn_wc__context_create_with_db(&wc_ctx, NULL, db, pool));

  if (depth == svn_depth_exclude)
    {
      SVN_ERR(svn_wc_exclude(wc_ctx,
                             local_abspath,
                             cancel_func, cancel_baton,
                             notify_func, notify_baton,
                             pool));
    }
  else
    {
      SVN_ERR(svn_wc_crop_tree2(wc_ctx,
                                local_abspath,
                                depth,
                                cancel_func, cancel_baton,
                                notify_func, notify_baton,
                                pool));
    }

  return svn_error_trace(svn_wc_context_destroy(wc_ctx));
}

svn_error_t *
svn_wc_move(svn_wc_context_t *wc_ctx,
            const char *src_abspath,
            const char *dst_abspath,
            svn_boolean_t metadata_only,
            svn_cancel_func_t cancel_func,
            void *cancel_baton,
            svn_wc_notify_func2_t notify_func,
            void *notify_baton,
            apr_pool_t *scratch_pool)
{
  return svn_error_trace(svn_wc__move2(wc_ctx, src_abspath, dst_abspath,
                                       metadata_only,
                                       TRUE, /* allow_mixed_revisions */
                                       cancel_func, cancel_baton,
                                       notify_func, notify_baton,
                                       scratch_pool));
}

svn_error_t *
svn_wc_read_kind(svn_node_kind_t *kind,
                 svn_wc_context_t *wc_ctx,
                 const char *abspath,
                 svn_boolean_t show_hidden,
                 apr_pool_t *scratch_pool)
{
  return svn_error_trace(
          svn_wc_read_kind2(kind,
                            wc_ctx, abspath,
                            TRUE /* show_deleted */,
                            show_hidden,
                            scratch_pool));
}

svn_wc_conflict_description2_t *
svn_wc__conflict_description2_dup(const svn_wc_conflict_description2_t *conflict,
                                  apr_pool_t *pool)
{
  return svn_wc_conflict_description2_dup(conflict, pool);
}
