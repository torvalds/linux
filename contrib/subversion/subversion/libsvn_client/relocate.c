/*
 * relocate.c:  wrapper around wc relocation functionality.
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

#include "svn_wc.h"
#include "svn_client.h"
#include "svn_pools.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "client.h"

#include "private/svn_wc_private.h"

#include "svn_private_config.h"


/*** Code. ***/

/* Repository root and UUID for a repository. */
struct url_uuid_t
{
  const char *root;
  const char *uuid;
};

struct validator_baton_t
{
  svn_client_ctx_t *ctx;
  const char *path;
  apr_array_header_t *url_uuids;
  apr_pool_t *pool;

};


static svn_error_t *
validator_func(void *baton,
               const char *uuid,
               const char *url,
               const char *root_url,
               apr_pool_t *pool)
{
  struct validator_baton_t *b = baton;
  struct url_uuid_t *url_uuid = NULL;
  const char *disable_checks;

  apr_array_header_t *uuids = b->url_uuids;
  int i;

  for (i = 0; i < uuids->nelts; ++i)
    {
      struct url_uuid_t *uu = &APR_ARRAY_IDX(uuids, i,
                                             struct url_uuid_t);
      if (svn_uri__is_ancestor(uu->root, url))
        {
          url_uuid = uu;
          break;
        }
    }

  disable_checks = getenv("SVN_I_LOVE_CORRUPTED_WORKING_COPIES_SO_DISABLE_RELOCATE_VALIDATION");
  if (disable_checks && (strcmp(disable_checks, "yes") == 0))
    {
      /* Lie about URL_UUID's components, claiming they match the
         expectations of the validation code below.  */
      url_uuid = apr_pcalloc(pool, sizeof(*url_uuid));
      url_uuid->root = apr_pstrdup(pool, root_url);
      url_uuid->uuid = apr_pstrdup(pool, uuid);
    }

  /* We use an RA session in a subpool to get the UUID of the
     repository at the new URL so we can force the RA session to close
     by destroying the subpool. */
  if (! url_uuid)
    {
      apr_pool_t *sesspool = svn_pool_create(pool);

      url_uuid = &APR_ARRAY_PUSH(uuids, struct url_uuid_t);
      SVN_ERR(svn_client_get_repos_root(&url_uuid->root,
                                        &url_uuid->uuid,
                                        url, b->ctx,
                                        pool, sesspool));

      svn_pool_destroy(sesspool);
    }

  /* Make sure the url is a repository root if desired. */
  if (root_url
      && strcmp(root_url, url_uuid->root) != 0)
    return svn_error_createf(SVN_ERR_CLIENT_INVALID_RELOCATION, NULL,
                             _("'%s' is not the root of the repository"),
                             url);

  /* Make sure the UUIDs match. */
  if (uuid && strcmp(uuid, url_uuid->uuid) != 0)
    return svn_error_createf
      (SVN_ERR_CLIENT_INVALID_RELOCATION, NULL,
       _("The repository at '%s' has uuid '%s', but the WC has '%s'"),
       url, url_uuid->uuid, uuid);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_relocate2(const char *wcroot_dir,
                     const char *from_prefix,
                     const char *to_prefix,
                     svn_boolean_t ignore_externals,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *pool)
{
  struct validator_baton_t vb;
  const char *local_abspath;
  apr_hash_t *externals_hash = NULL;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = NULL;
  const char *old_repos_root_url, *new_repos_root_url;
  char *sig_from_prefix, *sig_to_prefix;
  apr_size_t index_from, index_to;

  /* Populate our validator callback baton, and call the relocate code. */
  vb.ctx = ctx;
  vb.path = wcroot_dir;
  vb.url_uuids = apr_array_make(pool, 1, sizeof(struct url_uuid_t));
  vb.pool = pool;

  if (svn_path_is_url(wcroot_dir))
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("'%s' is not a local path"),
                             wcroot_dir);

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, wcroot_dir, pool));

  /* If we're ignoring externals, just relocate and get outta here. */
  if (ignore_externals)
    {
      return svn_error_trace(svn_wc_relocate4(ctx->wc_ctx, local_abspath,
                                              from_prefix, to_prefix,
                                              validator_func, &vb, pool));
    }

  /* Fetch our current root URL. */
  SVN_ERR(svn_client_get_repos_root(&old_repos_root_url, NULL /* uuid */,
                                    local_abspath, ctx, pool, pool));

  /* Perform the relocation. */
  SVN_ERR(svn_wc_relocate4(ctx->wc_ctx, local_abspath, from_prefix, to_prefix,
                           validator_func, &vb, pool));

  /* Now fetch new current root URL. */
  SVN_ERR(svn_client_get_repos_root(&new_repos_root_url, NULL /* uuid */,
                                    local_abspath, ctx, pool, pool));


  /* Relocate externals, too (if any). */
  SVN_ERR(svn_wc__externals_defined_below(&externals_hash,
                                          ctx->wc_ctx, local_abspath,
                                          pool, pool));
  if (! apr_hash_count(externals_hash))
    return SVN_NO_ERROR;

  /* A valid prefix for the main working copy may be too long to be
     valid for an external.  Trim any common trailing characters to
     leave the significant part that changes. */
  sig_from_prefix = apr_pstrdup(pool, from_prefix);
  sig_to_prefix = apr_pstrdup(pool, to_prefix);
  index_from = strlen(sig_from_prefix);
  index_to = strlen(sig_to_prefix);
  while (index_from && index_to
         && sig_from_prefix[index_from] == sig_to_prefix[index_to])
    {
      sig_from_prefix[index_from] = sig_to_prefix[index_to] = '\0';
      --index_from;
      --index_to;
    }

  iterpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, externals_hash);
       hi != NULL;
       hi = apr_hash_next(hi))
    {
      svn_node_kind_t kind;
      const char *this_abspath = apr_hash_this_key(hi);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc__read_external_info(&kind, NULL, NULL, NULL, NULL,
                                         ctx->wc_ctx,
                                         local_abspath, this_abspath,
                                         FALSE, iterpool, iterpool));

      if (kind == svn_node_dir)
        {
          const char *this_repos_root_url;
          svn_error_t *err;

          err = svn_client_get_repos_root(&this_repos_root_url, NULL /* uuid */,
                                          this_abspath, ctx, iterpool, iterpool);

          /* Ignore externals that aren't present in the working copy.
           * This can happen if an external is deleted from disk accidentally,
           * or if an external is configured on a locally added directory. */
          if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
            {
              svn_error_clear(err);
              continue;
            }
          SVN_ERR(err);

          if (strcmp(old_repos_root_url, this_repos_root_url) == 0)
            SVN_ERR(svn_client_relocate2(this_abspath,
                                         sig_from_prefix, sig_to_prefix,
                                         FALSE /* ignore_externals */,
                                         ctx, iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
