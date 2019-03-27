/*
 * iprops.c:  wrappers around wc inherited property functionality
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

#include "svn_error.h"
#include "svn_hash.h"
#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_ra.h"
#include "svn_props.h"
#include "svn_path.h"

#include "client.h"
#include "svn_private_config.h"

#include "private/svn_wc_private.h"


/*** Code. ***/

/* Determine if LOCAL_ABSPATH needs an inherited property cache.  If it does,
   then set *NEEDS_CACHE to TRUE, set it to FALSE otherwise.  All other args
   are as per svn_client__get_inheritable_props(). */
static svn_error_t *
need_to_cache_iprops(svn_boolean_t *needs_cache,
                     const char *local_abspath,
                     svn_ra_session_t *ra_session,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *scratch_pool)
{
  svn_boolean_t is_wc_root;
  svn_boolean_t is_switched;
  svn_error_t *err;

  err = svn_wc_check_root(&is_wc_root, &is_switched, NULL,
                          ctx->wc_ctx, local_abspath,
                           scratch_pool);

  /* LOCAL_ABSPATH doesn't need a cache if it doesn't exist. */
  if (err)
    {
      if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
        {
          svn_error_clear(err);
          is_wc_root = FALSE;
          is_switched = FALSE;
        }
      else
        {
          return svn_error_trace(err);
        }
    }

  /* Starting assumption. */
  *needs_cache = FALSE;

  if (is_wc_root || is_switched)
    {
      const char *session_url;
      const char *session_root_url;

      /* Looks likely that we need an inherited properties cache...Unless
         LOCAL_ABSPATH is a WC root that points to the repos root.  Then it
         doesn't need a cache because it has nowhere to inherit from.  Check
         for that case. */
      SVN_ERR(svn_ra_get_session_url(ra_session, &session_url, scratch_pool));
      SVN_ERR(svn_ra_get_repos_root2(ra_session, &session_root_url,
                                     scratch_pool));

      if (strcmp(session_root_url, session_url) != 0)
        *needs_cache = TRUE;
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__iprop_relpaths_to_urls(apr_array_header_t *inherited_props,
                                   const char *repos_root_url,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  int i;

  for (i = 0; i < inherited_props->nelts; i++)
    {
      svn_prop_inherited_item_t *elt =
        APR_ARRAY_IDX(inherited_props, i, svn_prop_inherited_item_t *);

      /* Convert repos root relpaths to full URLs. */
      if (! (svn_path_is_url(elt->path_or_url)
             || svn_dirent_is_absolute(elt->path_or_url)))
        {
          elt->path_or_url = svn_path_url_add_component2(repos_root_url,
                                                         elt->path_or_url,
                                                         result_pool);
        }
    }
  return SVN_NO_ERROR;
}

/* The real implementation of svn_client__get_inheritable_props */
static svn_error_t *
get_inheritable_props(apr_hash_t **wcroot_iprops,
                      const char *local_abspath,
                      svn_revnum_t revision,
                      svn_depth_t depth,
                      svn_ra_session_t *ra_session,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  apr_hash_t *iprop_paths;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_pool_t *session_pool = NULL;
  *wcroot_iprops = apr_hash_make(result_pool);

  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));

  /* If we don't have a base revision for LOCAL_ABSPATH then it can't
     possibly be a working copy root, nor can it contain any WC roots
     in the form of switched subtrees.  So there is nothing to cache. */

  SVN_ERR(svn_wc__get_cached_iprop_children(&iprop_paths, depth,
                                            ctx->wc_ctx, local_abspath,
                                            scratch_pool, iterpool));

  /* If we are in the midst of a checkout or an update that is bringing in
     an external, then svn_wc__get_cached_iprop_children won't return
     LOCAL_ABSPATH in IPROPS_PATHS because the former has no cached iprops
     yet.  So make sure LOCAL_ABSPATH is present if it's a WC root. */
  if (!svn_hash_gets(iprop_paths, local_abspath))
    {
      svn_boolean_t needs_cached_iprops;

      SVN_ERR(need_to_cache_iprops(&needs_cached_iprops, local_abspath,
                                   ra_session, ctx, iterpool));
      if (needs_cached_iprops)
        {
          const char *target_abspath = apr_pstrdup(scratch_pool,
                                                   local_abspath);

          /* As value we set TARGET_ABSPATH, but any string besides ""
             would do */
          svn_hash_sets(iprop_paths, target_abspath, target_abspath);
        }
    }

      for (hi = apr_hash_first(scratch_pool, iprop_paths);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *child_abspath = apr_hash_this_key(hi);
          const char *child_repos_relpath = apr_hash_this_val(hi);
          const char *url;
          apr_array_header_t *inherited_props;
          svn_error_t *err;

          svn_pool_clear(iterpool);

          if (*child_repos_relpath == '\0')
            {
              /* A repository root doesn't have inherited properties */
              continue;
            }

          SVN_ERR(svn_wc__node_get_url(&url, ctx->wc_ctx, child_abspath,
                                       iterpool, iterpool));
          if (ra_session)
            SVN_ERR(svn_ra_reparent(ra_session, url, scratch_pool));
          else
            {
              if (! session_pool)
                session_pool = svn_pool_create(scratch_pool);

              SVN_ERR(svn_client_open_ra_session2(&ra_session, url, NULL,
                                                  ctx,
                                                  session_pool, iterpool));
            }

          err = svn_ra_get_inherited_props(ra_session, &inherited_props,
                                           "", revision,
                                           result_pool, iterpool);

          if (err)
            {
              if (err->apr_err != SVN_ERR_FS_NOT_FOUND)
                return svn_error_trace(err);

              svn_error_clear(err);
              continue;
            }

          svn_hash_sets(*wcroot_iprops,
                        apr_pstrdup(result_pool, child_abspath),
                        inherited_props);
        }


  svn_pool_destroy(iterpool);
  if (session_pool)
    svn_pool_destroy(session_pool);

  return SVN_NO_ERROR;

}

svn_error_t *
svn_client__get_inheritable_props(apr_hash_t **wcroot_iprops,
                                  const char *local_abspath,
                                  svn_revnum_t revision,
                                  svn_depth_t depth,
                                  svn_ra_session_t *ra_session,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  const char *old_session_url;
  svn_error_t *err;

  *wcroot_iprops = NULL;

  if (!SVN_IS_VALID_REVNUM(revision))
    return SVN_NO_ERROR;

  if (ra_session)
    SVN_ERR(svn_ra_get_session_url(ra_session, &old_session_url, scratch_pool));

  /* We just wrap a simple helper function, as it is to easy to leave the ra
     session rooted at some wrong path without a wrapper like this.

     During development we had problems where some now deleted switched path
     made the update try to update to that url instead of the intended url
   */

  err = get_inheritable_props(wcroot_iprops, local_abspath, revision, depth,
                              ra_session, ctx, result_pool, scratch_pool);

  if (ra_session)
    {
      err = svn_error_compose_create(
                err,
                svn_ra_reparent(ra_session, old_session_url, scratch_pool));
    }
  return svn_error_trace(err);
}
