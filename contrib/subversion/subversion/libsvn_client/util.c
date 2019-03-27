/*
 * util.c :  utility functions for the libsvn_client library
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
#include <apr_strings.h>

#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_types.h"
#include "svn_opt.h"
#include "svn_props.h"
#include "svn_path.h"
#include "svn_wc.h"
#include "svn_client.h"

#include "private/svn_client_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_fspath.h"

#include "client.h"

#include "svn_private_config.h"

svn_client__pathrev_t *
svn_client__pathrev_create(const char *repos_root_url,
                           const char *repos_uuid,
                           svn_revnum_t rev,
                           const char *url,
                           apr_pool_t *result_pool)
{
  svn_client__pathrev_t *loc = apr_palloc(result_pool, sizeof(*loc));

  SVN_ERR_ASSERT_NO_RETURN(svn_path_is_url(repos_root_url));
  SVN_ERR_ASSERT_NO_RETURN(svn_path_is_url(url));

  loc->repos_root_url = apr_pstrdup(result_pool, repos_root_url);
  loc->repos_uuid = apr_pstrdup(result_pool, repos_uuid);
  loc->rev = rev;
  loc->url = apr_pstrdup(result_pool, url);
  return loc;
}

svn_client__pathrev_t *
svn_client__pathrev_create_with_relpath(const char *repos_root_url,
                                        const char *repos_uuid,
                                        svn_revnum_t rev,
                                        const char *relpath,
                                        apr_pool_t *result_pool)
{
  SVN_ERR_ASSERT_NO_RETURN(svn_relpath_is_canonical(relpath));

  return svn_client__pathrev_create(
           repos_root_url, repos_uuid, rev,
           svn_path_url_add_component2(repos_root_url, relpath, result_pool),
           result_pool);
}

svn_error_t *
svn_client__pathrev_create_with_session(svn_client__pathrev_t **pathrev_p,
                                        svn_ra_session_t *ra_session,
                                        svn_revnum_t rev,
                                        const char *url,
                                        apr_pool_t *result_pool)
{
  svn_client__pathrev_t *pathrev = apr_palloc(result_pool, sizeof(*pathrev));

  SVN_ERR_ASSERT(svn_path_is_url(url));

  SVN_ERR(svn_ra_get_repos_root2(ra_session, &pathrev->repos_root_url,
                                 result_pool));
  SVN_ERR(svn_ra_get_uuid2(ra_session, &pathrev->repos_uuid, result_pool));
  pathrev->rev = rev;
  pathrev->url = apr_pstrdup(result_pool, url);
  *pathrev_p = pathrev;
  return SVN_NO_ERROR;
}

svn_client__pathrev_t *
svn_client__pathrev_dup(const svn_client__pathrev_t *pathrev,
                        apr_pool_t *result_pool)
{
  return svn_client__pathrev_create(
           pathrev->repos_root_url, pathrev->repos_uuid,
           pathrev->rev, pathrev->url, result_pool);
}

svn_client__pathrev_t *
svn_client__pathrev_join_relpath(const svn_client__pathrev_t *pathrev,
                                 const char *relpath,
                                 apr_pool_t *result_pool)
{
  return svn_client__pathrev_create(
           pathrev->repos_root_url, pathrev->repos_uuid, pathrev->rev,
           svn_path_url_add_component2(pathrev->url, relpath, result_pool),
           result_pool);
}

const char *
svn_client__pathrev_relpath(const svn_client__pathrev_t *pathrev,
                            apr_pool_t *result_pool)
{
  return svn_uri_skip_ancestor(pathrev->repos_root_url, pathrev->url,
                               result_pool);
}

const char *
svn_client__pathrev_fspath(const svn_client__pathrev_t *pathrev,
                           apr_pool_t *result_pool)
{
  return svn_fspath__canonicalize(svn_uri_skip_ancestor(
                                    pathrev->repos_root_url, pathrev->url,
                                    result_pool),
                                  result_pool);
}


svn_client_commit_item3_t *
svn_client_commit_item3_create(apr_pool_t *pool)
{
  svn_client_commit_item3_t *item = apr_pcalloc(pool, sizeof(*item));

  item->revision = SVN_INVALID_REVNUM;
  item->copyfrom_rev = SVN_INVALID_REVNUM;
  item->kind = svn_node_unknown;

  return item;
}

svn_client_commit_item3_t *
svn_client_commit_item3_dup(const svn_client_commit_item3_t *item,
                            apr_pool_t *pool)
{
  svn_client_commit_item3_t *new_item = apr_palloc(pool, sizeof(*new_item));

  *new_item = *item;

  if (new_item->path)
    new_item->path = apr_pstrdup(pool, new_item->path);

  if (new_item->url)
    new_item->url = apr_pstrdup(pool, new_item->url);

  if (new_item->copyfrom_url)
    new_item->copyfrom_url = apr_pstrdup(pool, new_item->copyfrom_url);

  if (new_item->incoming_prop_changes)
    new_item->incoming_prop_changes =
      svn_prop_array_dup(new_item->incoming_prop_changes, pool);

  if (new_item->outgoing_prop_changes)
    new_item->outgoing_prop_changes =
      svn_prop_array_dup(new_item->outgoing_prop_changes, pool);

  if (new_item->session_relpath)
    new_item->session_relpath = apr_pstrdup(pool, new_item->session_relpath);

  if (new_item->moved_from_abspath)
    new_item->moved_from_abspath = apr_pstrdup(pool,
                                               new_item->moved_from_abspath);

  return new_item;
}

svn_error_t *
svn_client__wc_node_get_base(svn_client__pathrev_t **base_p,
                             const char *wc_abspath,
                             svn_wc_context_t *wc_ctx,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const char *relpath;

  *base_p = apr_palloc(result_pool, sizeof(**base_p));

  SVN_ERR(svn_wc__node_get_base(NULL,
                                &(*base_p)->rev,
                                &relpath,
                                &(*base_p)->repos_root_url,
                                &(*base_p)->repos_uuid,
                                NULL,
                                wc_ctx, wc_abspath,
                                TRUE /* ignore_enoent */,
                                result_pool, scratch_pool));
  if ((*base_p)->repos_root_url && relpath)
    {
      (*base_p)->url = svn_path_url_add_component2(
                           (*base_p)->repos_root_url, relpath, result_pool);
    }
  else
    {
      *base_p = NULL;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__wc_node_get_origin(svn_client__pathrev_t **origin_p,
                               const char *wc_abspath,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  const char *relpath;

  *origin_p = apr_palloc(result_pool, sizeof(**origin_p));

  SVN_ERR(svn_wc__node_get_origin(NULL /* is_copy */,
                                  &(*origin_p)->rev,
                                  &relpath,
                                  &(*origin_p)->repos_root_url,
                                  &(*origin_p)->repos_uuid,
                                  NULL, NULL,
                                  ctx->wc_ctx, wc_abspath,
                                  FALSE /* scan_deleted */,
                                  result_pool, scratch_pool));
  if ((*origin_p)->repos_root_url && relpath)
    {
      (*origin_p)->url = svn_path_url_add_component2(
                           (*origin_p)->repos_root_url, relpath, result_pool);
    }
  else
    {
      *origin_p = NULL;
    }
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_get_repos_root(const char **repos_root,
                          const char **repos_uuid,
                          const char *abspath_or_url,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_ra_session_t *ra_session;

  /* If PATH_OR_URL is a local path we can fetch the repos root locally. */
  if (!svn_path_is_url(abspath_or_url))
    {
      svn_error_t *err;
      err = svn_wc__node_get_repos_info(NULL, NULL, repos_root, repos_uuid,
                                        ctx->wc_ctx, abspath_or_url,
                                        result_pool, scratch_pool);

      if (err)
        {
          if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND
              && err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
            return svn_error_trace(err);

          svn_error_clear(err);
          if (repos_root)
            *repos_root = NULL;
          if (repos_uuid)
            *repos_uuid = NULL;
        }
      return SVN_NO_ERROR;
    }

  /* If PATH_OR_URL was a URL, we use the RA layer to look it up. */
  SVN_ERR(svn_client_open_ra_session2(&ra_session,  abspath_or_url, NULL,
                                      ctx, scratch_pool, scratch_pool));

  if (repos_root)
    SVN_ERR(svn_ra_get_repos_root2(ra_session, repos_root, result_pool));
  if (repos_uuid)
    SVN_ERR(svn_ra_get_uuid2(ra_session, repos_uuid, result_pool));

  return SVN_NO_ERROR;
}

const svn_opt_revision_t *
svn_cl__rev_default_to_head_or_base(const svn_opt_revision_t *revision,
                                    const char *path_or_url)
{
  static svn_opt_revision_t head_rev = { svn_opt_revision_head, { 0 } };
  static svn_opt_revision_t base_rev = { svn_opt_revision_base, { 0 } };

  if (revision->kind == svn_opt_revision_unspecified)
    return svn_path_is_url(path_or_url) ? &head_rev : &base_rev;
  return revision;
}

const svn_opt_revision_t *
svn_cl__rev_default_to_head_or_working(const svn_opt_revision_t *revision,
                                       const char *path_or_url)
{
  static svn_opt_revision_t head_rev = { svn_opt_revision_head, { 0 } };
  static svn_opt_revision_t work_rev = { svn_opt_revision_working, { 0 } };

  if (revision->kind == svn_opt_revision_unspecified)
    return svn_path_is_url(path_or_url) ? &head_rev : &work_rev;
  return revision;
}

const svn_opt_revision_t *
svn_cl__rev_default_to_peg(const svn_opt_revision_t *revision,
                           const svn_opt_revision_t *peg_revision)
{
  if (revision->kind == svn_opt_revision_unspecified)
    return peg_revision;
  return revision;
}

svn_error_t *
svn_client__assert_homogeneous_target_type(const apr_array_header_t *targets)
{
  svn_boolean_t wc_present = FALSE, url_present = FALSE;
  int i;

  for (i = 0; i < targets->nelts; ++i)
    {
      const char *target = APR_ARRAY_IDX(targets, i, const char *);
      if (! svn_path_is_url(target))
        wc_present = TRUE;
      else
        url_present = TRUE;
      if (url_present && wc_present)
        return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                                 _("Cannot mix repository and working copy "
                                   "targets"));
    }

  return SVN_NO_ERROR;
}

struct shim_callbacks_baton
{
  svn_wc_context_t *wc_ctx;
  apr_hash_t *relpath_map;
};

static svn_error_t *
fetch_props_func(apr_hash_t **props,
                 void *baton,
                 const char *path,
                 svn_revnum_t base_revision,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  struct shim_callbacks_baton *scb = baton;
  const char *local_abspath;

  local_abspath = svn_hash_gets(scb->relpath_map, path);
  if (!local_abspath)
    {
      *props = apr_hash_make(result_pool);
      return SVN_NO_ERROR;
    }

  /* Reads the pristine properties of WORKING, not those of BASE */
  SVN_ERR(svn_wc_get_pristine_props(props, scb->wc_ctx, local_abspath,
                                    result_pool, scratch_pool));

  if (!*props)
    *props = apr_hash_make(result_pool);

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_kind_func(svn_node_kind_t *kind,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *scratch_pool)
{
  struct shim_callbacks_baton *scb = baton;
  const char *local_abspath;

  local_abspath = svn_hash_gets(scb->relpath_map, path);
  if (!local_abspath)
    {
      *kind = svn_node_unknown;
      return SVN_NO_ERROR;
    }
  /* Reads the WORKING kind. Not the BASE kind */
  SVN_ERR(svn_wc_read_kind2(kind, scb->wc_ctx, local_abspath,
                            TRUE, FALSE, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_base_func(const char **filename,
                void *baton,
                const char *path,
                svn_revnum_t base_revision,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  struct shim_callbacks_baton *scb = baton;
  const char *local_abspath;
  svn_stream_t *pristine_stream;
  svn_stream_t *temp_stream;
  svn_error_t *err;

  local_abspath = svn_hash_gets(scb->relpath_map, path);
  if (!local_abspath)
    {
      *filename = NULL;
      return SVN_NO_ERROR;
    }

  /* Reads the pristine of WORKING, not of BASE */
  err = svn_wc_get_pristine_contents2(&pristine_stream, scb->wc_ctx,
                                      local_abspath, scratch_pool,
                                      scratch_pool);
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      *filename = NULL;
      return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_trace(err);

  SVN_ERR(svn_stream_open_unique(&temp_stream, filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 result_pool, scratch_pool));
  SVN_ERR(svn_stream_copy3(pristine_stream, temp_stream, NULL, NULL,
                           scratch_pool));

  return SVN_NO_ERROR;
}

svn_delta_shim_callbacks_t *
svn_client__get_shim_callbacks(svn_wc_context_t *wc_ctx,
                               apr_hash_t *relpath_map,
                               apr_pool_t *result_pool)
{
  svn_delta_shim_callbacks_t *callbacks =
                            svn_delta_shim_callbacks_default(result_pool);
  struct shim_callbacks_baton *scb = apr_pcalloc(result_pool, sizeof(*scb));

  scb->wc_ctx = wc_ctx;
  if (relpath_map)
    scb->relpath_map = relpath_map;
  else
    scb->relpath_map = apr_hash_make(result_pool);

  callbacks->fetch_props_func = fetch_props_func;
  callbacks->fetch_kind_func = fetch_kind_func;
  callbacks->fetch_base_func = fetch_base_func;
  callbacks->fetch_baton = scb;

  return callbacks;
}
