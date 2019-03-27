/*
 * ra.c :  routines for interacting with the RA layer
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

#include "svn_error.h"
#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_sorts.h"
#include "svn_ra.h"
#include "svn_client.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "client.h"
#include "mergeinfo.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_client_private.h"
#include "private/svn_sorts_private.h"


/* This is the baton that we pass svn_ra_open3(), and is associated with
   the callback table we provide to RA. */
typedef struct callback_baton_t
{
  /* Holds the directory that corresponds to the REPOS_URL at svn_ra_open3()
     time. When callbacks specify a relative path, they are joined with
     this base directory. */
  const char *base_dir_abspath;

  /* TEMPORARY: Is 'base_dir_abspath' a versioned path?  cmpilato
     suspects that the commit-to-multiple-disjoint-working-copies
     code is getting this all wrong, sometimes passing an unversioned
     (or versioned in a foreign wc) path here which sorta kinda
     happens to work most of the time but is ultimately incorrect.  */
  svn_boolean_t base_dir_isversioned;

  /* Used as wri_abspath for obtaining access to the pristine store */
  const char *wcroot_abspath;

  /* An array of svn_client_commit_item3_t * structures, present only
     during working copy commits. */
  const apr_array_header_t *commit_items;

  /* A client context. */
  svn_client_ctx_t *ctx;

  /* Last progress reported by progress callback. */
  apr_off_t last_progress;
} callback_baton_t;



static svn_error_t *
open_tmp_file(apr_file_t **fp,
              void *callback_baton,
              apr_pool_t *pool)
{
  return svn_error_trace(svn_io_open_unique_file3(fp, NULL, NULL,
                                  svn_io_file_del_on_pool_cleanup,
                                  pool, pool));
}


/* This implements the 'svn_ra_get_wc_prop_func_t' interface. */
static svn_error_t *
get_wc_prop(void *baton,
            const char *relpath,
            const char *name,
            const svn_string_t **value,
            apr_pool_t *pool)
{
  callback_baton_t *cb = baton;
  const char *local_abspath = NULL;
  svn_error_t *err;

  *value = NULL;

  /* If we have a list of commit_items, search through that for a
     match for this relative URL. */
  if (cb->commit_items)
    {
      int i;
      for (i = 0; i < cb->commit_items->nelts; i++)
        {
          svn_client_commit_item3_t *item
            = APR_ARRAY_IDX(cb->commit_items, i, svn_client_commit_item3_t *);

          if (! strcmp(relpath, item->session_relpath))
            {
              SVN_ERR_ASSERT(svn_dirent_is_absolute(item->path));
              local_abspath = item->path;
              break;
            }
        }

      /* Commits can only query relpaths in the commit_items list
         since the commit driver traverses paths as they are, or will
         be, in the repository.  Non-commits query relpaths in the
         working copy. */
      if (! local_abspath)
        return SVN_NO_ERROR;
    }

  /* If we don't have a base directory, then there are no properties. */
  else if (cb->base_dir_abspath == NULL)
    return SVN_NO_ERROR;

  else
    local_abspath = svn_dirent_join(cb->base_dir_abspath, relpath, pool);

  err = svn_wc_prop_get2(value, cb->ctx->wc_ctx, local_abspath, name,
                         pool, pool);
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      err = NULL;
    }
  return svn_error_trace(err);
}

/* This implements the 'svn_ra_push_wc_prop_func_t' interface. */
static svn_error_t *
push_wc_prop(void *baton,
             const char *relpath,
             const char *name,
             const svn_string_t *value,
             apr_pool_t *pool)
{
  callback_baton_t *cb = baton;
  int i;

  /* If we're committing, search through the commit_items list for a
     match for this relative URL. */
  if (! cb->commit_items)
    return svn_error_createf
      (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
       _("Attempt to set wcprop '%s' on '%s' in a non-commit operation"),
       name, svn_dirent_local_style(relpath, pool));

  for (i = 0; i < cb->commit_items->nelts; i++)
    {
      svn_client_commit_item3_t *item
        = APR_ARRAY_IDX(cb->commit_items, i, svn_client_commit_item3_t *);

      if (strcmp(relpath, item->session_relpath) == 0)
        {
          apr_pool_t *changes_pool = item->incoming_prop_changes->pool;
          svn_prop_t *prop = apr_palloc(changes_pool, sizeof(*prop));

          prop->name = apr_pstrdup(changes_pool, name);
          if (value)
            prop->value = svn_string_dup(value, changes_pool);
          else
            prop->value = NULL;

          /* Buffer the propchange to take effect during the
             post-commit process. */
          APR_ARRAY_PUSH(item->incoming_prop_changes, svn_prop_t *) = prop;
          return SVN_NO_ERROR;
        }
    }

  return SVN_NO_ERROR;
}


/* This implements the 'svn_ra_set_wc_prop_func_t' interface. */
static svn_error_t *
set_wc_prop(void *baton,
            const char *path,
            const char *name,
            const svn_string_t *value,
            apr_pool_t *pool)
{
  callback_baton_t *cb = baton;
  const char *local_abspath;

  local_abspath = svn_dirent_join(cb->base_dir_abspath, path, pool);

  /* We pass 1 for the 'force' parameter here.  Since the property is
     coming from the repository, we definitely want to accept it.
     Ideally, we'd raise a conflict if, say, the received property is
     svn:eol-style yet the file has a locally added svn:mime-type
     claiming that it's binary.  Probably the repository is still
     right, but the conflict would remind the user to make sure.
     Unfortunately, we don't have a clean mechanism for doing that
     here, so we just set the property and hope for the best. */
  return svn_error_trace(svn_wc_prop_set4(cb->ctx->wc_ctx, local_abspath,
                                          name,
                                          value, svn_depth_empty,
                                          TRUE /* skip_checks */,
                                          NULL /* changelist_filter */,
                                          NULL, NULL /* cancellation */,
                                          NULL, NULL /* notification */,
                                          pool));
}


/* This implements the `svn_ra_invalidate_wc_props_func_t' interface. */
static svn_error_t *
invalidate_wc_props(void *baton,
                    const char *path,
                    const char *prop_name,
                    apr_pool_t *pool)
{
  callback_baton_t *cb = baton;
  const char *local_abspath;

  local_abspath = svn_dirent_join(cb->base_dir_abspath, path, pool);

  /* It's easier just to clear the whole dav_cache than to remove
     individual items from it recursively like this.  And since we
     know that the RA providers that ship with Subversion only
     invalidate the one property they use the most from this cache,
     and that we're intentionally trying to get away from the use of
     the cache altogether anyway, there's little to lose in wiping the
     whole cache.  Is it the most well-behaved approach to take?  Not
     so much.  We choose not to care.  */
  return svn_error_trace(svn_wc__node_clear_dav_cache_recursive(
                              cb->ctx->wc_ctx, local_abspath, pool));
}


/* This implements the `svn_ra_get_wc_contents_func_t' interface. */
static svn_error_t *
get_wc_contents(void *baton,
                svn_stream_t **contents,
                const svn_checksum_t *checksum,
                apr_pool_t *pool)
{
  callback_baton_t *cb = baton;

  if (! cb->wcroot_abspath)
    {
      *contents = NULL;
      return SVN_NO_ERROR;
    }

  return svn_error_trace(
             svn_wc__get_pristine_contents_by_checksum(contents,
                                                       cb->ctx->wc_ctx,
                                                       cb->wcroot_abspath,
                                                       checksum,
                                                       pool, pool));
}


static svn_error_t *
cancel_callback(void *baton)
{
  callback_baton_t *b = baton;
  return svn_error_trace((b->ctx->cancel_func)(b->ctx->cancel_baton));
}


static svn_error_t *
get_client_string(void *baton,
                  const char **name,
                  apr_pool_t *pool)
{
  callback_baton_t *b = baton;
  *name = apr_pstrdup(pool, b->ctx->client_name);
  return SVN_NO_ERROR;
}

/* Implements svn_ra_progress_notify_func_t. Accumulates progress information
 * for different RA sessions and reports total progress to caller. */
static void
progress_func(apr_off_t progress,
              apr_off_t total,
              void *baton,
              apr_pool_t *pool)
{
  callback_baton_t *b = baton;
  svn_client_ctx_t *public_ctx = b->ctx;
  svn_client__private_ctx_t *private_ctx =
    svn_client__get_private_ctx(public_ctx);

  private_ctx->total_progress += (progress - b->last_progress);
  b->last_progress = progress;

  if (public_ctx->progress_func)
    {
      /* All RA implementations currently provide -1 for total. So it doesn't
         make sense to develop some complex logic to combine total across all
         RA sessions. */
      public_ctx->progress_func(private_ctx->total_progress, -1,
                                public_ctx->progress_baton, pool);
    }
}

#define SVN_CLIENT__MAX_REDIRECT_ATTEMPTS 3 /* ### TODO:  Make configurable. */

svn_error_t *
svn_client__open_ra_session_internal(svn_ra_session_t **ra_session,
                                     const char **corrected_url,
                                     const char *base_url,
                                     const char *base_dir_abspath,
                                     const apr_array_header_t *commit_items,
                                     svn_boolean_t write_dav_props,
                                     svn_boolean_t read_dav_props,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  svn_ra_callbacks2_t *cbtable;
  callback_baton_t *cb = apr_pcalloc(result_pool, sizeof(*cb));
  const char *uuid = NULL;

  SVN_ERR_ASSERT(!write_dav_props || read_dav_props);
  SVN_ERR_ASSERT(!read_dav_props || base_dir_abspath != NULL);
  SVN_ERR_ASSERT(base_dir_abspath == NULL
                        || svn_dirent_is_absolute(base_dir_abspath));

  SVN_ERR(svn_ra_create_callbacks(&cbtable, result_pool));
  cbtable->open_tmp_file = open_tmp_file;
  cbtable->get_wc_prop = read_dav_props ? get_wc_prop : NULL;
  cbtable->set_wc_prop = (write_dav_props && read_dav_props)
                          ? set_wc_prop : NULL;
  cbtable->push_wc_prop = commit_items ? push_wc_prop : NULL;
  cbtable->invalidate_wc_props = (write_dav_props && read_dav_props)
                                  ? invalidate_wc_props : NULL;
  cbtable->auth_baton = ctx->auth_baton; /* new-style */
  cbtable->progress_func = progress_func;
  cbtable->progress_baton = cb;
  cbtable->cancel_func = ctx->cancel_func ? cancel_callback : NULL;
  cbtable->get_client_string = get_client_string;
  if (base_dir_abspath)
    cbtable->get_wc_contents = get_wc_contents;
  cbtable->check_tunnel_func = ctx->check_tunnel_func;
  cbtable->open_tunnel_func = ctx->open_tunnel_func;
  cbtable->tunnel_baton = ctx->tunnel_baton;

  cb->commit_items = commit_items;
  cb->ctx = ctx;

  if (base_dir_abspath && (read_dav_props || write_dav_props))
    {
      svn_error_t *err = svn_wc__node_get_repos_info(NULL, NULL, NULL, &uuid,
                                                     ctx->wc_ctx,
                                                     base_dir_abspath,
                                                     result_pool,
                                                     scratch_pool);

      if (err && (err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY
                  || err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND
                  || err->apr_err == SVN_ERR_WC_UPGRADE_REQUIRED))
        {
          svn_error_clear(err);
          uuid = NULL;
        }
      else
        {
          SVN_ERR(err);
          cb->base_dir_isversioned = TRUE;
        }
      cb->base_dir_abspath = apr_pstrdup(result_pool, base_dir_abspath);
    }

  if (base_dir_abspath)
    {
      svn_error_t *err = svn_wc__get_wcroot(&cb->wcroot_abspath,
                                            ctx->wc_ctx, base_dir_abspath,
                                            result_pool, scratch_pool);

      if (err)
        {
          if (err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY
              && err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND
              && err->apr_err != SVN_ERR_WC_UPGRADE_REQUIRED)
            return svn_error_trace(err);

          svn_error_clear(err);
          cb->wcroot_abspath = NULL;
        }
    }

  /* If the caller allows for auto-following redirections, and the
     RA->open() call above reveals a CORRECTED_URL, try the new URL.
     We'll do this in a loop up to some maximum number follow-and-retry
     attempts.  */
  if (corrected_url)
    {
      apr_hash_t *attempted = apr_hash_make(scratch_pool);
      int attempts_left = SVN_CLIENT__MAX_REDIRECT_ATTEMPTS;

      *corrected_url = NULL;
      while (attempts_left--)
        {
          const char *corrected = NULL;

          /* Try to open the RA session.  If this is our last attempt,
             don't accept corrected URLs from the RA provider. */
          SVN_ERR(svn_ra_open4(ra_session,
                               attempts_left == 0 ? NULL : &corrected,
                               base_url, uuid, cbtable, cb, ctx->config,
                               result_pool));

          /* No error and no corrected URL?  We're done here. */
          if (! corrected)
            break;

          /* Notify the user that a redirect is being followed. */
          if (ctx->notify_func2 != NULL)
            {
              svn_wc_notify_t *notify =
                svn_wc_create_notify_url(corrected,
                                         svn_wc_notify_url_redirect,
                                         scratch_pool);
              ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
            }

          /* Our caller will want to know what our final corrected URL was. */
          *corrected_url = corrected;

          /* Make sure we've not attempted this URL before. */
          if (svn_hash_gets(attempted, corrected))
            return svn_error_createf(SVN_ERR_CLIENT_CYCLE_DETECTED, NULL,
                                     _("Redirect cycle detected for URL '%s'"),
                                     corrected);

          /* Remember this CORRECTED_URL so we don't wind up in a loop. */
          svn_hash_sets(attempted, corrected, (void *)1);
          base_url = corrected;
        }
    }
  else
    {
      SVN_ERR(svn_ra_open4(ra_session, NULL, base_url,
                           uuid, cbtable, cb, ctx->config, result_pool));
    }

  return SVN_NO_ERROR;
}
#undef SVN_CLIENT__MAX_REDIRECT_ATTEMPTS


svn_error_t *
svn_client_open_ra_session2(svn_ra_session_t **session,
                            const char *url,
                            const char *wri_abspath,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  return svn_error_trace(
             svn_client__open_ra_session_internal(session, NULL, url,
                                                  wri_abspath, NULL,
                                                  FALSE, FALSE,
                                                  ctx, result_pool,
                                                  scratch_pool));
}

svn_error_t *
svn_client__resolve_rev_and_url(svn_client__pathrev_t **resolved_loc_p,
                                svn_ra_session_t *ra_session,
                                const char *path_or_url,
                                const svn_opt_revision_t *peg_revision,
                                const svn_opt_revision_t *revision,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *pool)
{
  svn_opt_revision_t peg_rev = *peg_revision;
  svn_opt_revision_t start_rev = *revision;
  const char *url;
  svn_revnum_t rev;

  /* Default revisions: peg -> working or head; operative -> peg. */
  SVN_ERR(svn_opt_resolve_revisions(&peg_rev, &start_rev,
                                    svn_path_is_url(path_or_url),
                                    TRUE /* notice_local_mods */,
                                    pool));

  /* Run the history function to get the object's (possibly
     different) url in REVISION. */
  SVN_ERR(svn_client__repos_locations(&url, &rev, NULL, NULL,
                                      ra_session, path_or_url, &peg_rev,
                                      &start_rev, NULL, ctx, pool));

  SVN_ERR(svn_client__pathrev_create_with_session(resolved_loc_p,
                                                  ra_session, rev, url, pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__ra_session_from_path2(svn_ra_session_t **ra_session_p,
                                  svn_client__pathrev_t **resolved_loc_p,
                                  const char *path_or_url,
                                  const char *base_dir_abspath,
                                  const svn_opt_revision_t *peg_revision,
                                  const svn_opt_revision_t *revision,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const char *initial_url;
  const char *corrected_url;
  svn_client__pathrev_t *resolved_loc;
  const char *wri_abspath;

  SVN_ERR(svn_client_url_from_path2(&initial_url, path_or_url, ctx, pool,
                                    pool));
  if (! initial_url)
    return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                             _("'%s' has no URL"), path_or_url);

  if (base_dir_abspath)
    wri_abspath = base_dir_abspath;
  else if (!svn_path_is_url(path_or_url))
    SVN_ERR(svn_dirent_get_absolute(&wri_abspath, path_or_url, pool));
  else
    wri_abspath = NULL;

  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, &corrected_url,
                                               initial_url,
                                               wri_abspath,
                                               NULL /* commit_items */,
                                               base_dir_abspath != NULL,
                                               base_dir_abspath != NULL,
                                               ctx, pool, pool));

  /* If we got a CORRECTED_URL, we'll want to refer to that as the
     URL-ized form of PATH_OR_URL from now on. */
  if (corrected_url && svn_path_is_url(path_or_url))
    path_or_url = corrected_url;

  SVN_ERR(svn_client__resolve_rev_and_url(&resolved_loc, ra_session,
                                          path_or_url, peg_revision, revision,
                                          ctx, pool));

  /* Make the session point to the real URL. */
  SVN_ERR(svn_ra_reparent(ra_session, resolved_loc->url, pool));

  *ra_session_p = ra_session;
  if (resolved_loc_p)
    *resolved_loc_p = resolved_loc;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__ensure_ra_session_url(const char **old_session_url,
                                  svn_ra_session_t *ra_session,
                                  const char *session_url,
                                  apr_pool_t *pool)
{
  SVN_ERR(svn_ra_get_session_url(ra_session, old_session_url, pool));
  if (! session_url)
    SVN_ERR(svn_ra_get_repos_root2(ra_session, &session_url, pool));
  if (strcmp(*old_session_url, session_url) != 0)
    SVN_ERR(svn_ra_reparent(ra_session, session_url, pool));
  return SVN_NO_ERROR;
}



/*** Repository Locations ***/

struct gls_receiver_baton_t
{
  apr_array_header_t *segments;
  svn_client_ctx_t *ctx;
  apr_pool_t *pool;
};

static svn_error_t *
gls_receiver(svn_location_segment_t *segment,
             void *baton,
             apr_pool_t *pool)
{
  struct gls_receiver_baton_t *b = baton;
  APR_ARRAY_PUSH(b->segments, svn_location_segment_t *) =
    svn_location_segment_dup(segment, b->pool);
  if (b->ctx->cancel_func)
    SVN_ERR((b->ctx->cancel_func)(b->ctx->cancel_baton));
  return SVN_NO_ERROR;
}

/* A qsort-compatible function which sorts svn_location_segment_t's
   based on their revision range covering, resulting in ascending
   (oldest-to-youngest) ordering. */
static int
compare_segments(const void *a, const void *b)
{
  const svn_location_segment_t *a_seg
    = *((const svn_location_segment_t * const *) a);
  const svn_location_segment_t *b_seg
    = *((const svn_location_segment_t * const *) b);
  if (a_seg->range_start == b_seg->range_start)
    return 0;
  return (a_seg->range_start < b_seg->range_start) ? -1 : 1;
}

svn_error_t *
svn_client__repos_location_segments(apr_array_header_t **segments,
                                    svn_ra_session_t *ra_session,
                                    const char *url,
                                    svn_revnum_t peg_revision,
                                    svn_revnum_t start_revision,
                                    svn_revnum_t end_revision,
                                    svn_client_ctx_t *ctx,
                                    apr_pool_t *pool)
{
  struct gls_receiver_baton_t gls_receiver_baton;
  const char *old_session_url;
  svn_error_t *err;

  *segments = apr_array_make(pool, 8, sizeof(svn_location_segment_t *));
  gls_receiver_baton.segments = *segments;
  gls_receiver_baton.ctx = ctx;
  gls_receiver_baton.pool = pool;
  SVN_ERR(svn_client__ensure_ra_session_url(&old_session_url, ra_session,
                                            url, pool));
  err = svn_ra_get_location_segments(ra_session, "", peg_revision,
                                     start_revision, end_revision,
                                     gls_receiver, &gls_receiver_baton,
                                     pool);
  SVN_ERR(svn_error_compose_create(
            err, svn_ra_reparent(ra_session, old_session_url, pool)));
  svn_sort__array(*segments, compare_segments);
  return SVN_NO_ERROR;
}

/* Set *START_URL and *END_URL to the URLs that the object URL@PEG_REVNUM
 * had in revisions START_REVNUM and END_REVNUM.  Return an error if the
 * node cannot be traced back to one of the requested revisions.
 *
 * START_URL and/or END_URL may be NULL if not wanted.  START_REVNUM and
 * END_REVNUM must be valid revision numbers except that END_REVNUM may
 * be SVN_INVALID_REVNUM if END_URL is NULL.
 *
 * YOUNGEST_REV is the already retrieved youngest revision of the ra session,
 * but can be SVN_INVALID_REVNUM if the value is not already retrieved.
 *
 * RA_SESSION is an open RA session parented at URL.
 */
static svn_error_t *
repos_locations(const char **start_url,
                const char **end_url,
                svn_ra_session_t *ra_session,
                const char *url,
                svn_revnum_t peg_revnum,
                svn_revnum_t start_revnum,
                svn_revnum_t end_revnum,
                svn_revnum_t youngest_rev,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  const char *repos_url, *start_path, *end_path;
  apr_array_header_t *revs;
  apr_hash_t *rev_locs;

  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(peg_revnum));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(start_revnum));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(end_revnum) || end_url == NULL);

  /* Avoid a network request in the common easy case. */
  if (start_revnum == peg_revnum
      && (end_revnum == peg_revnum || end_revnum == SVN_INVALID_REVNUM))
    {
      if (start_url)
        *start_url = apr_pstrdup(result_pool, url);
      if (end_url)
        *end_url = apr_pstrdup(result_pool, url);
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_url, scratch_pool));

  /* Handle another common case: The repository root can't move */
  if (! strcmp(repos_url, url))
    {
      if (! SVN_IS_VALID_REVNUM(youngest_rev))
        SVN_ERR(svn_ra_get_latest_revnum(ra_session, &youngest_rev,
                                         scratch_pool));

      if (start_revnum > youngest_rev
          || (SVN_IS_VALID_REVNUM(end_revnum) && (end_revnum > youngest_rev)))
        return svn_error_createf(SVN_ERR_FS_NO_SUCH_REVISION, NULL,
                                 _("No such revision %ld"),
                                 (start_revnum > youngest_rev)
                                        ? start_revnum : end_revnum);

      if (start_url)
        *start_url = apr_pstrdup(result_pool, repos_url);
      if (end_url)
        *end_url = apr_pstrdup(result_pool, repos_url);
      return SVN_NO_ERROR;
    }

  revs = apr_array_make(scratch_pool, 2, sizeof(svn_revnum_t));
  APR_ARRAY_PUSH(revs, svn_revnum_t) = start_revnum;
  if (end_revnum != start_revnum && end_revnum != SVN_INVALID_REVNUM)
    APR_ARRAY_PUSH(revs, svn_revnum_t) = end_revnum;

  SVN_ERR(svn_ra_get_locations(ra_session, &rev_locs, "", peg_revnum,
                               revs, scratch_pool));

  /* We'd better have all the paths we were looking for! */
  if (start_url)
    {
      start_path = apr_hash_get(rev_locs, &start_revnum, sizeof(start_revnum));
      if (! start_path)
        return svn_error_createf
          (SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
           _("Unable to find repository location for '%s' in revision %ld"),
           url, start_revnum);
      *start_url = svn_path_url_add_component2(repos_url, start_path + 1,
                                               result_pool);
    }

  if (end_url)
    {
      end_path = apr_hash_get(rev_locs, &end_revnum, sizeof(end_revnum));
      if (! end_path)
        return svn_error_createf
          (SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
           _("The location for '%s' for revision %ld does not exist in the "
             "repository or refers to an unrelated object"),
           url, end_revnum);

      *end_url = svn_path_url_add_component2(repos_url, end_path + 1,
                                             result_pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__repos_location(svn_client__pathrev_t **op_loc_p,
                           svn_ra_session_t *ra_session,
                           const svn_client__pathrev_t *peg_loc,
                           svn_revnum_t op_revnum,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *old_session_url;
  const char *op_url;
  svn_error_t *err;

  SVN_ERR(svn_client__ensure_ra_session_url(&old_session_url, ra_session,
                                            peg_loc->url, scratch_pool));
  err = repos_locations(&op_url, NULL, ra_session,
                        peg_loc->url, peg_loc->rev,
                        op_revnum, SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
                        result_pool, scratch_pool);
  SVN_ERR(svn_error_compose_create(
            err, svn_ra_reparent(ra_session, old_session_url, scratch_pool)));

  *op_loc_p = svn_client__pathrev_create(peg_loc->repos_root_url,
                                         peg_loc->repos_uuid,
                                         op_revnum, op_url, result_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__repos_locations(const char **start_url,
                            svn_revnum_t *start_revision,
                            const char **end_url,
                            svn_revnum_t *end_revision,
                            svn_ra_session_t *ra_session,
                            const char *path,
                            const svn_opt_revision_t *revision,
                            const svn_opt_revision_t *start,
                            const svn_opt_revision_t *end,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  const char *url;
  const char *local_abspath_or_url;
  svn_revnum_t peg_revnum = SVN_INVALID_REVNUM;
  svn_revnum_t start_revnum, end_revnum;
  svn_revnum_t youngest_rev = SVN_INVALID_REVNUM;
  apr_pool_t *subpool = svn_pool_create(pool);

  /* Ensure that we are given some real revision data to work with.
     (It's okay if the END is unspecified -- in that case, we'll just
     set it to the same thing as START.)  */
  if (revision->kind == svn_opt_revision_unspecified
      || start->kind == svn_opt_revision_unspecified)
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);

  if (end == NULL)
    {
      static const svn_opt_revision_t unspecified_rev
        = { svn_opt_revision_unspecified, { 0 } };

      end = &unspecified_rev;
    }

  /* Determine LOCAL_ABSPATH_OR_URL, URL, and possibly PEG_REVNUM.
     If we are looking at the working version of a WC path that is scheduled
     as a copy, then we need to use the copy-from URL and peg revision. */
  if (! svn_path_is_url(path))
    {
      SVN_ERR(svn_dirent_get_absolute(&local_abspath_or_url, path, subpool));

      if (revision->kind == svn_opt_revision_working)
        {
          const char *repos_root_url;
          const char *repos_relpath;
          svn_boolean_t is_copy;

          SVN_ERR(svn_wc__node_get_origin(&is_copy, &peg_revnum, &repos_relpath,
                                          &repos_root_url, NULL, NULL, NULL,
                                          ctx->wc_ctx, local_abspath_or_url,
                                          FALSE, subpool, subpool));

          if (repos_relpath)
            url = svn_path_url_add_component2(repos_root_url, repos_relpath,
                                              pool);
          else
            url = NULL;

          if (url && is_copy && ra_session)
            {
              const char *session_url;
              SVN_ERR(svn_ra_get_session_url(ra_session, &session_url,
                                             subpool));

              if (strcmp(session_url, url) != 0)
                {
                  /* We can't use the caller provided RA session now :( */
                  ra_session = NULL;
                }
            }
        }
      else
        url = NULL;

      if (! url)
        SVN_ERR(svn_wc__node_get_url(&url, ctx->wc_ctx,
                                     local_abspath_or_url, pool, subpool));

      if (!url)
        {
          return svn_error_createf(SVN_ERR_ENTRY_MISSING_URL, NULL,
                                   _("'%s' has no URL"),
                                   svn_dirent_local_style(path, pool));
        }
    }
  else
    {
      local_abspath_or_url = path;
      url = path;
    }

  /* ### We should be smarter here.  If the callers just asks for BASE and
     WORKING revisions, we should already have the correct URLs, so we
     don't need to do anything more here in that case. */

  /* Open a RA session to this URL if we don't have one already. */
  if (! ra_session)
    SVN_ERR(svn_client_open_ra_session2(&ra_session, url, NULL,
                                        ctx, subpool, subpool));

  /* Resolve the opt_revision_ts. */
  if (peg_revnum == SVN_INVALID_REVNUM)
    SVN_ERR(svn_client__get_revision_number(&peg_revnum, &youngest_rev,
                                            ctx->wc_ctx, local_abspath_or_url,
                                            ra_session, revision, pool));

  SVN_ERR(svn_client__get_revision_number(&start_revnum, &youngest_rev,
                                          ctx->wc_ctx, local_abspath_or_url,
                                          ra_session, start, pool));
  if (end->kind == svn_opt_revision_unspecified)
    end_revnum = start_revnum;
  else
    SVN_ERR(svn_client__get_revision_number(&end_revnum, &youngest_rev,
                                            ctx->wc_ctx, local_abspath_or_url,
                                            ra_session, end, pool));

  /* Set the output revision variables. */
  if (start_revision)
    {
      *start_revision = start_revnum;
    }
  if (end_revision && end->kind != svn_opt_revision_unspecified)
    {
      *end_revision = end_revnum;
    }

  SVN_ERR(repos_locations(start_url, end_url,
                          ra_session, url, peg_revnum,
                          start_revnum, end_revnum, youngest_rev,
                          pool, subpool));
  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__calc_youngest_common_ancestor(svn_client__pathrev_t **ancestor_p,
                                          const svn_client__pathrev_t *loc1,
                                          apr_hash_t *history1,
                                          svn_boolean_t has_rev_zero_history1,
                                          const svn_client__pathrev_t *loc2,
                                          apr_hash_t *history2,
                                          svn_boolean_t has_rev_zero_history2,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  svn_revnum_t yc_revision = SVN_INVALID_REVNUM;
  const char *yc_relpath = NULL;

  if (strcmp(loc1->repos_root_url, loc2->repos_root_url) != 0)
    {
      *ancestor_p = NULL;
      return SVN_NO_ERROR;
    }

  /* Loop through the first location's history, check for overlapping
     paths and ranges in the second location's history, and
     remembering the youngest matching location. */
  for (hi = apr_hash_first(scratch_pool, history1); hi; hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      apr_ssize_t path_len = apr_hash_this_key_len(hi);
      svn_rangelist_t *ranges1 = apr_hash_this_val(hi);
      svn_rangelist_t *ranges2, *common;

      ranges2 = apr_hash_get(history2, path, path_len);
      if (ranges2)
        {
          /* We have a path match.  Now, did our two histories share
             any revisions at that path? */
          SVN_ERR(svn_rangelist_intersect(&common, ranges1, ranges2,
                                          TRUE, scratch_pool));
          if (common->nelts)
            {
              svn_merge_range_t *yc_range =
                APR_ARRAY_IDX(common, common->nelts - 1, svn_merge_range_t *);
              if ((! SVN_IS_VALID_REVNUM(yc_revision))
                  || (yc_range->end > yc_revision))
                {
                  yc_revision = yc_range->end;
                  yc_relpath = path + 1;
                }
            }
        }
    }

  /* It's possible that PATH_OR_URL1 and PATH_OR_URL2's only common
     history is revision 0. */
  if (!yc_relpath && has_rev_zero_history1 && has_rev_zero_history2)
    {
      yc_relpath = "";
      yc_revision = 0;
    }

  if (yc_relpath)
    {
      *ancestor_p = svn_client__pathrev_create_with_relpath(
                      loc1->repos_root_url, loc1->repos_uuid,
                      yc_revision, yc_relpath, result_pool);
    }
  else
    {
      *ancestor_p = NULL;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_youngest_common_ancestor(svn_client__pathrev_t **ancestor_p,
                                         const svn_client__pathrev_t *loc1,
                                         const svn_client__pathrev_t *loc2,
                                         svn_ra_session_t *session,
                                         svn_client_ctx_t *ctx,
                                         apr_pool_t *result_pool,
                                         apr_pool_t *scratch_pool)
{
  apr_pool_t *sesspool = NULL;
  apr_hash_t *history1, *history2;
  svn_boolean_t has_rev_zero_history1;
  svn_boolean_t has_rev_zero_history2;

  if (strcmp(loc1->repos_root_url, loc2->repos_root_url) != 0)
    {
      *ancestor_p = NULL;
      return SVN_NO_ERROR;
    }

  /* Open an RA session for the two locations. */
  if (session == NULL)
    {
      sesspool = svn_pool_create(scratch_pool);
      SVN_ERR(svn_client_open_ra_session2(&session, loc1->url, NULL, ctx,
                                          sesspool, sesspool));
    }

  /* We're going to cheat and use history-as-mergeinfo because it
     saves us a bunch of annoying custom data comparisons and such. */
  SVN_ERR(svn_client__get_history_as_mergeinfo(&history1,
                                               &has_rev_zero_history1,
                                               loc1,
                                               SVN_INVALID_REVNUM,
                                               SVN_INVALID_REVNUM,
                                               session, ctx, scratch_pool));
  SVN_ERR(svn_client__get_history_as_mergeinfo(&history2,
                                               &has_rev_zero_history2,
                                               loc2,
                                               SVN_INVALID_REVNUM,
                                               SVN_INVALID_REVNUM,
                                               session, ctx, scratch_pool));
  /* Close the ra session if we opened one. */
  if (sesspool)
    svn_pool_destroy(sesspool);

  SVN_ERR(svn_client__calc_youngest_common_ancestor(ancestor_p,
                                                    loc1, history1,
                                                    has_rev_zero_history1,
                                                    loc2, history2,
                                                    has_rev_zero_history2,
                                                    result_pool,
                                                    scratch_pool));

  return SVN_NO_ERROR;
}

struct ra_ev2_baton {
  /* The working copy context, from the client context.  */
  svn_wc_context_t *wc_ctx;

  /* For a given REPOS_RELPATH, provide a LOCAL_ABSPATH that represents
     that repository node.  */
  apr_hash_t *relpath_map;
};


svn_error_t *
svn_client__ra_provide_base(svn_stream_t **contents,
                            svn_revnum_t *revision,
                            void *baton,
                            const char *repos_relpath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  struct ra_ev2_baton *reb = baton;
  const char *local_abspath;
  svn_error_t *err;

  local_abspath = svn_hash_gets(reb->relpath_map, repos_relpath);
  if (!local_abspath)
    {
      *contents = NULL;
      return SVN_NO_ERROR;
    }

  err = svn_wc_get_pristine_contents2(contents, reb->wc_ctx, local_abspath,
                                      result_pool, scratch_pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);

      svn_error_clear(err);
      *contents = NULL;
      return SVN_NO_ERROR;
    }

  if (*contents != NULL)
    {
      /* The pristine contents refer to the BASE, or to the pristine of
         a copy/move to this location. Fetch the correct revision.  */
      SVN_ERR(svn_wc__node_get_origin(NULL, revision, NULL, NULL, NULL, NULL,
                                      NULL,
                                      reb->wc_ctx, local_abspath, FALSE,
                                      scratch_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__ra_provide_props(apr_hash_t **props,
                             svn_revnum_t *revision,
                             void *baton,
                             const char *repos_relpath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  struct ra_ev2_baton *reb = baton;
  const char *local_abspath;
  svn_error_t *err;

  local_abspath = svn_hash_gets(reb->relpath_map, repos_relpath);
  if (!local_abspath)
    {
      *props = NULL;
      return SVN_NO_ERROR;
    }

  err = svn_wc_get_pristine_props(props, reb->wc_ctx, local_abspath,
                                  result_pool, scratch_pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_trace(err);

      svn_error_clear(err);
      *props = NULL;
      return SVN_NO_ERROR;
    }

  if (*props != NULL)
    {
      /* The pristine props refer to the BASE, or to the pristine props of
         a copy/move to this location. Fetch the correct revision.  */
      SVN_ERR(svn_wc__node_get_origin(NULL, revision, NULL, NULL, NULL, NULL,
                                      NULL,
                                      reb->wc_ctx, local_abspath, FALSE,
                                      scratch_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__ra_get_copysrc_kind(svn_node_kind_t *kind,
                                void *baton,
                                const char *repos_relpath,
                                svn_revnum_t src_revision,
                                apr_pool_t *scratch_pool)
{
  struct ra_ev2_baton *reb = baton;
  const char *local_abspath;

  local_abspath = svn_hash_gets(reb->relpath_map, repos_relpath);
  if (!local_abspath)
    {
      *kind = svn_node_unknown;
      return SVN_NO_ERROR;
    }

  /* ### what to do with SRC_REVISION?  */

  SVN_ERR(svn_wc_read_kind2(kind, reb->wc_ctx, local_abspath,
                            FALSE, FALSE, scratch_pool));

  return SVN_NO_ERROR;
}


void *
svn_client__ra_make_cb_baton(svn_wc_context_t *wc_ctx,
                             apr_hash_t *relpath_map,
                             apr_pool_t *result_pool)
{
  struct ra_ev2_baton *reb = apr_palloc(result_pool, sizeof(*reb));

  SVN_ERR_ASSERT_NO_RETURN(wc_ctx != NULL);
  SVN_ERR_ASSERT_NO_RETURN(relpath_map != NULL);

  reb->wc_ctx = wc_ctx;
  reb->relpath_map = relpath_map;

  return reb;
}
