/*
 * mergeinfo.c :  merge history functions for the libsvn_client library
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

#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_string.h"
#include "svn_opt.h"
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "svn_sorts.h"
#include "svn_ra.h"
#include "svn_client.h"
#include "svn_hash.h"

#include "private/svn_client_private.h"
#include "private/svn_opt_private.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_ra_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_wc_private.h"
#include "private/svn_fspath.h"
#include "client.h"
#include "mergeinfo.h"
#include "svn_private_config.h"



svn_client__merge_path_t *
svn_client__merge_path_dup(const svn_client__merge_path_t *old,
                           apr_pool_t *pool)
{
  svn_client__merge_path_t *new = apr_pmemdup(pool, old, sizeof(*old));

  new->abspath = apr_pstrdup(pool, old->abspath);
  if (new->remaining_ranges)
    new->remaining_ranges = svn_rangelist_dup(old->remaining_ranges, pool);
  if (new->pre_merge_mergeinfo)
    new->pre_merge_mergeinfo = svn_mergeinfo_dup(old->pre_merge_mergeinfo,
                                                 pool);
  if (new->implicit_mergeinfo)
    new->implicit_mergeinfo = svn_mergeinfo_dup(old->implicit_mergeinfo,
                                                pool);

  return new;
}

svn_client__merge_path_t *
svn_client__merge_path_create(const char *abspath,
                              apr_pool_t *pool)
{
  svn_client__merge_path_t *result = apr_pcalloc(pool, sizeof(*result));

  result->abspath = apr_pstrdup(pool, abspath);
  return result;
}

svn_error_t *
svn_client__parse_mergeinfo(svn_mergeinfo_t *mergeinfo,
                            svn_wc_context_t *wc_ctx,
                            const char *local_abspath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  const svn_string_t *propval;

  *mergeinfo = NULL;

  /* ### Use svn_wc_prop_get() would actually be sufficient for now.
     ### DannyB thinks that later we'll need behavior more like
     ### svn_client__get_prop_from_wc(). */
  SVN_ERR(svn_wc_prop_get2(&propval, wc_ctx, local_abspath, SVN_PROP_MERGEINFO,
                           scratch_pool, scratch_pool));
  if (propval)
    SVN_ERR(svn_mergeinfo_parse(mergeinfo, propval->data, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__record_wc_mergeinfo(const char *local_abspath,
                                svn_mergeinfo_t mergeinfo,
                                svn_boolean_t do_notification,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *scratch_pool)
{
  svn_string_t *mergeinfo_str = NULL;
  svn_boolean_t mergeinfo_changes = FALSE;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Convert MERGEINFO (if any) into text for storage as a property value. */
  if (mergeinfo)
    SVN_ERR(svn_mergeinfo_to_string(&mergeinfo_str, mergeinfo, scratch_pool));

  if (do_notification && ctx->notify_func2)
    SVN_ERR(svn_client__mergeinfo_status(&mergeinfo_changes, ctx->wc_ctx,
                                         local_abspath, scratch_pool));

  /* Record the new mergeinfo in the WC. */
  /* ### Later, we'll want behavior more analogous to
     ### svn_client__get_prop_from_wc(). */
  SVN_ERR(svn_wc_prop_set4(ctx->wc_ctx, local_abspath, SVN_PROP_MERGEINFO,
                           mergeinfo_str, svn_depth_empty,
                           TRUE /* skip checks */, NULL,
                           NULL, NULL /* cancellation */,
                           NULL, NULL /* notification */,
                           scratch_pool));

  if (do_notification && ctx->notify_func2)
    {
      svn_wc_notify_t *notify =
        svn_wc_create_notify(local_abspath,
                             svn_wc_notify_merge_record_info,
                             scratch_pool);
      if (mergeinfo_changes)
        notify->prop_state = svn_wc_notify_state_merged;
      else
        notify->prop_state = svn_wc_notify_state_changed;

      ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__record_wc_mergeinfo_catalog(apr_hash_t *result_catalog,
                                        svn_client_ctx_t *ctx,
                                        apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  if (apr_hash_count(result_catalog))
    {
      int i;
      apr_array_header_t *sorted_cat =
        svn_sort__hash(result_catalog, svn_sort_compare_items_as_paths,
                       scratch_pool);

      /* Write the mergeinfo out in sorted order of the paths (presumably just
       * so that the notifications are in a predictable, convenient order). */
      for (i = 0; i < sorted_cat->nelts; i++)
        {
          svn_sort__item_t elt = APR_ARRAY_IDX(sorted_cat, i,
                                               svn_sort__item_t);
          svn_error_t *err;

          svn_pool_clear(iterpool);
          err = svn_client__record_wc_mergeinfo(elt.key, elt.value, TRUE,
                                                ctx, iterpool);

          if (err && err->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
            {
              /* PATH isn't just missing, it's not even versioned as far
                 as this working copy knows.  But it was included in
                 MERGES, which means that the server knows about it.
                 Likely we don't have access to the source due to authz
                 restrictions.  For now just clear the error and
                 continue... */
              svn_error_clear(err);
            }
          else
            {
              SVN_ERR(err);
            }
        }
    }
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------------*/

/*** Retrieving mergeinfo. ***/

svn_error_t *
svn_client__get_wc_mergeinfo(svn_mergeinfo_t *mergeinfo,
                             svn_boolean_t *inherited_p,
                             svn_mergeinfo_inheritance_t inherit,
                             const char *local_abspath,
                             const char *limit_abspath,
                             const char **walked_path,
                             svn_boolean_t ignore_invalid_mergeinfo,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  const char *walk_relpath = "";
  svn_mergeinfo_t wc_mergeinfo;
  svn_revnum_t base_revision;
  apr_pool_t *iterpool;
  svn_boolean_t inherited;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  if (limit_abspath)
    SVN_ERR_ASSERT(svn_dirent_is_absolute(limit_abspath));

  SVN_ERR(svn_wc__node_get_base(NULL, &base_revision, NULL, NULL, NULL, NULL,
                                ctx->wc_ctx, local_abspath,
                                TRUE /* ignore_enoent */,
                                scratch_pool, scratch_pool));

  iterpool = svn_pool_create(scratch_pool);
  while (TRUE)
    {
      svn_pool_clear(iterpool);

      /* Don't look for explicit mergeinfo on LOCAL_ABSPATH if we are only
         interested in inherited mergeinfo. */
      if (inherit == svn_mergeinfo_nearest_ancestor)
        {
          wc_mergeinfo = NULL;
          inherit = svn_mergeinfo_inherited;
        }
      else
        {
          /* Look for mergeinfo on LOCAL_ABSPATH.  If there isn't any and we
             want inherited mergeinfo, walk towards the root of the WC until
             we encounter either (a) an unversioned directory, or
             (b) mergeinfo.  If we encounter (b), use that inherited
             mergeinfo as our baseline. */
          svn_error_t *err = svn_client__parse_mergeinfo(&wc_mergeinfo,
                                                         ctx->wc_ctx,
                                                         local_abspath,
                                                         result_pool,
                                                         iterpool);
          if ((ignore_invalid_mergeinfo || walk_relpath [0] != '\0')
              && err
              && err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
            {
              svn_error_clear(err);
              wc_mergeinfo = apr_hash_make(result_pool);
              break;
            }
          else
            {
              SVN_ERR(err);
            }
        }

      if (wc_mergeinfo == NULL &&
          inherit != svn_mergeinfo_explicit &&
          !svn_dirent_is_root(local_abspath, strlen(local_abspath)))
        {
          svn_boolean_t is_wc_root;
          svn_boolean_t is_switched;
          svn_revnum_t parent_base_rev;
          svn_revnum_t parent_changed_rev;

          /* Don't look any higher than the limit path. */
          if (limit_abspath && strcmp(limit_abspath, local_abspath) == 0)
            break;

          /* If we've reached the root of the working copy don't look any
             higher. */
          SVN_ERR(svn_wc_check_root(&is_wc_root, &is_switched, NULL,
                                    ctx->wc_ctx, local_abspath, iterpool));
          if (is_wc_root || is_switched)
            break;

          /* No explicit mergeinfo on this path.  Look higher up the
             directory tree while keeping track of what we've walked. */
          walk_relpath = svn_relpath_join(svn_dirent_basename(local_abspath,
                                                              iterpool),
                                          walk_relpath, result_pool);
          local_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

          SVN_ERR(svn_wc__node_get_base(NULL, &parent_base_rev, NULL, NULL,
                                        NULL, NULL,
                                        ctx->wc_ctx, local_abspath,
                                        TRUE /* ignore_enoent */,
                                        scratch_pool, scratch_pool));

          /* ### This checks the WORKING changed_rev, so invalid on replacement
             ### not even reliable in case an ancestor was copied from a
             ### different location */
          SVN_ERR(svn_wc__node_get_changed_info(&parent_changed_rev,
                                                NULL, NULL,
                                                ctx->wc_ctx, local_abspath,
                                                scratch_pool,
                                                scratch_pool));

          /* Look in LOCAL_ABSPATH's parent for inherited mergeinfo if
             LOCAL_ABSPATH has no base revision because it is an uncommitted
             addition, or if its base revision falls within the inclusive
             range of its parent's last changed revision to the parent's base
             revision; otherwise stop looking for inherited mergeinfo. */
          if (SVN_IS_VALID_REVNUM(base_revision)
              && (base_revision < parent_changed_rev
                  || parent_base_rev < base_revision))
            break;

          /* We haven't yet risen above the root of the WC. */
          continue;
        }
      break;
    }

  svn_pool_destroy(iterpool);

  if (svn_path_is_empty(walk_relpath))
    {
      /* Mergeinfo is explicit. */
      inherited = FALSE;
      *mergeinfo = wc_mergeinfo;
    }
  else
    {
      /* Mergeinfo may be inherited. */
      if (wc_mergeinfo)
        {
          inherited = TRUE;
          SVN_ERR(svn_mergeinfo__add_suffix_to_mergeinfo(mergeinfo,
                                                         wc_mergeinfo,
                                                         walk_relpath,
                                                         result_pool,
                                                         scratch_pool));
        }
      else
        {
          inherited = FALSE;
          *mergeinfo = NULL;
        }
    }

  if (walked_path)
    *walked_path = walk_relpath;

  /* Remove non-inheritable mergeinfo and paths mapped to empty ranges
     which may occur if WCPATH's mergeinfo is not explicit. */
  if (inherited
      && apr_hash_count(*mergeinfo)) /* Nothing to do for empty mergeinfo. */
    {
      SVN_ERR(svn_mergeinfo_inheritable2(mergeinfo, *mergeinfo, NULL,
                                         SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
                                         TRUE, result_pool, scratch_pool));
      svn_mergeinfo__remove_empty_rangelists(*mergeinfo, scratch_pool);
    }

  if (inherited_p)
    *inherited_p = inherited;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_wc_mergeinfo_catalog(svn_mergeinfo_catalog_t *mergeinfo_cat,
                                     svn_boolean_t *inherited,
                                     svn_boolean_t include_descendants,
                                     svn_mergeinfo_inheritance_t inherit,
                                     const char *local_abspath,
                                     const char *limit_path,
                                     const char **walked_path,
                                     svn_boolean_t ignore_invalid_mergeinfo,
                                     svn_client_ctx_t *ctx,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  const char *target_repos_relpath;
  svn_mergeinfo_t mergeinfo;
  const char *repos_root;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  *mergeinfo_cat = NULL;
  SVN_ERR(svn_wc__node_get_repos_info(NULL, &target_repos_relpath,
                                      &repos_root, NULL,
                                      ctx->wc_ctx, local_abspath,
                                      scratch_pool, scratch_pool));

  /* Get the mergeinfo for the LOCAL_ABSPATH target and set *INHERITED and
     *WALKED_PATH. */
  SVN_ERR(svn_client__get_wc_mergeinfo(&mergeinfo, inherited, inherit,
                                       local_abspath, limit_path,
                                       walked_path, ignore_invalid_mergeinfo,
                                       ctx, result_pool, scratch_pool));

  /* Add any explicit/inherited mergeinfo for LOCAL_ABSPATH to
     *MERGEINFO_CAT. */
  if (mergeinfo)
    {
      *mergeinfo_cat = apr_hash_make(result_pool);
      svn_hash_sets(*mergeinfo_cat,
                    apr_pstrdup(result_pool, target_repos_relpath), mergeinfo);
    }

  /* If LOCAL_ABSPATH is a directory and we want the subtree mergeinfo too,
     then get it.

     With WC-NG it is cheaper to do a single db transaction, than first
     looking if we really have a directory. */
  if (include_descendants)
    {
      apr_hash_t *mergeinfo_props;
      apr_hash_index_t *hi;

      SVN_ERR(svn_wc__prop_retrieve_recursive(&mergeinfo_props,
                                              ctx->wc_ctx, local_abspath,
                                              SVN_PROP_MERGEINFO,
                                              scratch_pool, scratch_pool));

      /* Convert *mergeinfo_props into a proper svn_mergeinfo_catalog_t */
      for (hi = apr_hash_first(scratch_pool, mergeinfo_props);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *node_abspath = apr_hash_this_key(hi);
          svn_string_t *propval = apr_hash_this_val(hi);
          svn_mergeinfo_t subtree_mergeinfo;
          const char *repos_relpath;

          if (strcmp(node_abspath, local_abspath) == 0)
            continue; /* Already parsed in svn_client__get_wc_mergeinfo */

          SVN_ERR(svn_wc__node_get_repos_info(NULL, &repos_relpath, NULL, NULL,
                                              ctx->wc_ctx, node_abspath,
                                              result_pool, scratch_pool));

          SVN_ERR(svn_mergeinfo_parse(&subtree_mergeinfo, propval->data,
                                      result_pool));

          /* If the target had no explicit/inherited mergeinfo and this is the
             first subtree with mergeinfo found, then the catalog will still
             be NULL. */
          if (*mergeinfo_cat == NULL)
            *mergeinfo_cat = apr_hash_make(result_pool);

          svn_hash_sets(*mergeinfo_cat, repos_relpath, subtree_mergeinfo);
        }
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_repos_mergeinfo(svn_mergeinfo_t *target_mergeinfo,
                                svn_ra_session_t *ra_session,
                                const char *url,
                                svn_revnum_t rev,
                                svn_mergeinfo_inheritance_t inherit,
                                svn_boolean_t squelch_incapable,
                                apr_pool_t *pool)
{
  svn_mergeinfo_catalog_t tgt_mergeinfo_cat;

  *target_mergeinfo = NULL;

  SVN_ERR(svn_client__get_repos_mergeinfo_catalog(&tgt_mergeinfo_cat,
                                                  ra_session,
                                                  url, rev, inherit,
                                                  squelch_incapable, FALSE,
                                                  pool, pool));

  if (tgt_mergeinfo_cat && apr_hash_count(tgt_mergeinfo_cat))
    {
      /* We asked only for the REL_PATH's mergeinfo, not any of its
         descendants.  So if there is anything in the catalog it is the
         mergeinfo for REL_PATH. */
      *target_mergeinfo =
        apr_hash_this_val(apr_hash_first(pool, tgt_mergeinfo_cat));

    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_repos_mergeinfo_catalog(svn_mergeinfo_catalog_t *mergeinfo_cat,
                                        svn_ra_session_t *ra_session,
                                        const char *url,
                                        svn_revnum_t rev,
                                        svn_mergeinfo_inheritance_t inherit,
                                        svn_boolean_t squelch_incapable,
                                        svn_boolean_t include_descendants,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_mergeinfo_catalog_t repos_mergeinfo_cat;
  apr_array_header_t *rel_paths = apr_array_make(scratch_pool, 1,
                                                 sizeof(const char *));
  const char *old_session_url;

  APR_ARRAY_PUSH(rel_paths, const char *) = "";

  /* Fetch the mergeinfo. */
  SVN_ERR(svn_client__ensure_ra_session_url(&old_session_url,
                                            ra_session, url, scratch_pool));
  err = svn_ra_get_mergeinfo(ra_session, &repos_mergeinfo_cat, rel_paths,
                             rev, inherit, include_descendants, result_pool);
  err = svn_error_compose_create(
          err, svn_ra_reparent(ra_session, old_session_url, scratch_pool));
  if (err)
    {
      if (squelch_incapable && err->apr_err == SVN_ERR_UNSUPPORTED_FEATURE)
        {
          svn_error_clear(err);
          *mergeinfo_cat = NULL;
          return SVN_NO_ERROR;
        }
      else
        return svn_error_trace(err);
    }

  if (repos_mergeinfo_cat == NULL)
    {
      *mergeinfo_cat = NULL;
    }
  else
    {
      const char *session_relpath;

      SVN_ERR(svn_ra_get_path_relative_to_root(ra_session, &session_relpath,
                                               url, scratch_pool));

      if (session_relpath[0] == '\0')
        *mergeinfo_cat = repos_mergeinfo_cat;
      else
        SVN_ERR(svn_mergeinfo__add_prefix_to_catalog(mergeinfo_cat,
                                                     repos_mergeinfo_cat,
                                                     session_relpath,
                                                     result_pool,
                                                     scratch_pool));
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__get_wc_or_repos_mergeinfo(svn_mergeinfo_t *target_mergeinfo,
                                      svn_boolean_t *inherited,
                                      svn_boolean_t *from_repos,
                                      svn_boolean_t repos_only,
                                      svn_mergeinfo_inheritance_t inherit,
                                      svn_ra_session_t *ra_session,
                                      const char *target_wcpath,
                                      svn_client_ctx_t *ctx,
                                      apr_pool_t *pool)
{
  svn_mergeinfo_catalog_t tgt_mergeinfo_cat;

  *target_mergeinfo = NULL;

  SVN_ERR(svn_client__get_wc_or_repos_mergeinfo_catalog(&tgt_mergeinfo_cat,
                                                        inherited, from_repos,
                                                        FALSE,
                                                        repos_only,
                                                        FALSE, inherit,
                                                        ra_session,
                                                        target_wcpath, ctx,
                                                        pool, pool));
  if (tgt_mergeinfo_cat && apr_hash_count(tgt_mergeinfo_cat))
    {
      /* We asked only for the TARGET_WCPATH's mergeinfo, not any of its
         descendants.  It this mergeinfo is in the catalog, it's keyed
         on TARGET_WCPATH's root-relative path.  We could dig that up
         so we can peek into our catalog, but it ought to be the only
         thing in the catalog, so we'll just fetch the first hash item. */
      *target_mergeinfo =
        apr_hash_this_val(apr_hash_first(pool, tgt_mergeinfo_cat));

    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__get_wc_or_repos_mergeinfo_catalog(
  svn_mergeinfo_catalog_t *target_mergeinfo_catalog,
  svn_boolean_t *inherited_p,
  svn_boolean_t *from_repos,
  svn_boolean_t include_descendants,
  svn_boolean_t repos_only,
  svn_boolean_t ignore_invalid_mergeinfo,
  svn_mergeinfo_inheritance_t inherit,
  svn_ra_session_t *ra_session,
  const char *target_wcpath,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  const char *url;
  svn_revnum_t target_rev;
  const char *local_abspath;
  const char *repos_root;
  const char *repos_relpath;
  svn_mergeinfo_catalog_t target_mergeinfo_cat_wc = NULL;
  svn_mergeinfo_catalog_t target_mergeinfo_cat_repos = NULL;

  SVN_ERR(svn_dirent_get_absolute(&local_abspath, target_wcpath,
                                  scratch_pool));

  if (from_repos)
    *from_repos = FALSE;

  /* We may get an entry with abbreviated information from TARGET_WCPATH's
     parent if TARGET_WCPATH is missing.  These limited entries do not have
     a URL and without that we cannot get accurate mergeinfo for
     TARGET_WCPATH. */
  SVN_ERR(svn_wc__node_get_origin(NULL, &target_rev, &repos_relpath,
                                  &repos_root, NULL, NULL, NULL,
                                  ctx->wc_ctx, local_abspath, FALSE,
                                  scratch_pool, scratch_pool));

  if (repos_relpath)
    url = svn_path_url_add_component2(repos_root, repos_relpath, scratch_pool);
  else
    url = NULL;

  if (!repos_only)
    {
      svn_boolean_t inherited;
      SVN_ERR(svn_client__get_wc_mergeinfo_catalog(&target_mergeinfo_cat_wc,
                                                   &inherited,
                                                   include_descendants,
                                                   inherit,
                                                   local_abspath,
                                                   NULL, NULL,
                                                   ignore_invalid_mergeinfo,
                                                   ctx,
                                                   result_pool,
                                                   scratch_pool));
      if (inherited_p)
        *inherited_p = inherited;

      /* If we want LOCAL_ABSPATH's inherited mergeinfo, were we able to
         get it from the working copy?  If not, then we must ask the
         repository. */
      if (! (inherited
             || (inherit == svn_mergeinfo_explicit)
             || (repos_relpath
                 && target_mergeinfo_cat_wc
                 && svn_hash_gets(target_mergeinfo_cat_wc, repos_relpath))))
        {
          repos_only = TRUE;
          /* We already have any subtree mergeinfo from the working copy, no
             need to ask the server for it again. */
          include_descendants = FALSE;
        }
    }

  if (repos_only)
    {
      /* No need to check the repos if this is a local addition. */
      if (url != NULL)
        {
          apr_hash_t *original_props;

          /* Check to see if we have local modifications which removed all of
             TARGET_WCPATH's pristine mergeinfo.  If that is the case then
             TARGET_WCPATH effectively has no mergeinfo. */
          SVN_ERR(svn_wc_get_pristine_props(&original_props,
                                            ctx->wc_ctx, local_abspath,
                                            result_pool, scratch_pool));
          if (!svn_hash_gets(original_props, SVN_PROP_MERGEINFO))
            {
              apr_pool_t *sesspool = NULL;

              if (! ra_session)
                {
                  sesspool = svn_pool_create(scratch_pool);
                  SVN_ERR(svn_client_open_ra_session2(&ra_session, url, NULL,
                                                      ctx,
                                                      sesspool, sesspool));
                }

              SVN_ERR(svn_client__get_repos_mergeinfo_catalog(
                        &target_mergeinfo_cat_repos, ra_session,
                        url, target_rev, inherit,
                        TRUE, include_descendants,
                        result_pool, scratch_pool));

              if (target_mergeinfo_cat_repos
                  && svn_hash_gets(target_mergeinfo_cat_repos, repos_relpath))
                {
                  if (inherited_p)
                    *inherited_p = TRUE;
                  if (from_repos)
                    *from_repos = TRUE;
                }

              /* If we created an RA_SESSION above, destroy it.
                 Otherwise, if reparented an existing session, point
                 it back where it was when we were called. */
              if (sesspool)
                {
                  svn_pool_destroy(sesspool);
                }
            }
        }
    }

  /* Combine the mergeinfo from the working copy and repository as needed. */
  if (target_mergeinfo_cat_wc)
    {
      *target_mergeinfo_catalog = target_mergeinfo_cat_wc;
      if (target_mergeinfo_cat_repos)
        SVN_ERR(svn_mergeinfo_catalog_merge(*target_mergeinfo_catalog,
                                            target_mergeinfo_cat_repos,
                                            result_pool, scratch_pool));
    }
  else if (target_mergeinfo_cat_repos)
    {
      *target_mergeinfo_catalog = target_mergeinfo_cat_repos;
    }
  else
    {
      *target_mergeinfo_catalog = NULL;
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__get_history_as_mergeinfo(svn_mergeinfo_t *mergeinfo_p,
                                      svn_boolean_t *has_rev_zero_history,
                                      const svn_client__pathrev_t *pathrev,
                                      svn_revnum_t range_youngest,
                                      svn_revnum_t range_oldest,
                                      svn_ra_session_t *ra_session,
                                      svn_client_ctx_t *ctx,
                                      apr_pool_t *pool)
{
  apr_array_header_t *segments;

  /* Fetch the location segments for our URL@PEG_REVNUM. */
  if (! SVN_IS_VALID_REVNUM(range_youngest))
    range_youngest = pathrev->rev;
  if (! SVN_IS_VALID_REVNUM(range_oldest))
    range_oldest = 0;

  SVN_ERR(svn_client__repos_location_segments(&segments, ra_session,
                                              pathrev->url, pathrev->rev,
                                              range_youngest, range_oldest,
                                              ctx, pool));

  if (has_rev_zero_history)
    {
      *has_rev_zero_history = FALSE;
        if (segments->nelts)
          {
            svn_location_segment_t *oldest_segment =
              APR_ARRAY_IDX(segments, 0, svn_location_segment_t *);
            if (oldest_segment->range_start == 0)
              *has_rev_zero_history = TRUE;
          }
    }

  SVN_ERR(svn_mergeinfo__mergeinfo_from_segments(mergeinfo_p, segments, pool));

  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/*** Eliding mergeinfo. ***/

/* Given the mergeinfo (CHILD_MERGEINFO) for a path, and the
   mergeinfo of its nearest ancestor with mergeinfo (PARENT_MERGEINFO), compare
   CHILD_MERGEINFO to PARENT_MERGEINFO to see if the former elides to
   the latter, following the elision rules described in
   svn_client__elide_mergeinfo()'s docstring.  Set *ELIDES to whether
   or not CHILD_MERGEINFO is redundant.

   Note: This function assumes that PARENT_MERGEINFO is definitive;
   i.e. if it is NULL then the caller not only walked the entire WC
   looking for inherited mergeinfo, but queried the repository if none
   was found in the WC.  This is rather important since this function
   says empty mergeinfo should be elided if PARENT_MERGEINFO is NULL,
   and we don't want to do that unless we are *certain* that the empty
   mergeinfo on PATH isn't overriding anything.

   If PATH_SUFFIX and PARENT_MERGEINFO are not NULL append PATH_SUFFIX
   to each path in PARENT_MERGEINFO before performing the comparison. */
static svn_error_t *
should_elide_mergeinfo(svn_boolean_t *elides,
                       svn_mergeinfo_t parent_mergeinfo,
                       svn_mergeinfo_t child_mergeinfo,
                       const char *path_suffix,
                       apr_pool_t *scratch_pool)
{
  /* Easy out: No child mergeinfo to elide. */
  if (child_mergeinfo == NULL)
    {
      *elides = FALSE;
    }
  else if (apr_hash_count(child_mergeinfo) == 0)
    {
      /* Empty mergeinfo elides to empty mergeinfo or to "nothing",
         i.e. it isn't overriding any parent. Otherwise it doesn't
         elide. */
      *elides = (!parent_mergeinfo || apr_hash_count(parent_mergeinfo) == 0);
    }
  else if (!parent_mergeinfo || apr_hash_count(parent_mergeinfo) == 0)
    {
      /* Non-empty mergeinfo never elides to empty mergeinfo
         or no mergeinfo. */
      *elides = FALSE;
    }
  else
    {
      /* Both CHILD_MERGEINFO and PARENT_MERGEINFO are non-NULL and
         non-empty. */
      svn_mergeinfo_t path_tweaked_parent_mergeinfo;

      /* If we need to adjust the paths in PARENT_MERGEINFO do it now. */
      if (path_suffix)
        SVN_ERR(svn_mergeinfo__add_suffix_to_mergeinfo(
                  &path_tweaked_parent_mergeinfo, parent_mergeinfo,
                  path_suffix, scratch_pool, scratch_pool));
      else
        path_tweaked_parent_mergeinfo = parent_mergeinfo;

      SVN_ERR(svn_mergeinfo__equals(elides,
                                    path_tweaked_parent_mergeinfo,
                                    child_mergeinfo, TRUE, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Helper for svn_client__elide_mergeinfo().

   Given a working copy LOCAL_ABSPATH, its mergeinfo hash CHILD_MERGEINFO, and
   the mergeinfo of LOCAL_ABSPATH's nearest ancestor PARENT_MERGEINFO, use
   should_elide_mergeinfo() to decide whether or not CHILD_MERGEINFO elides to
   PARENT_MERGEINFO; PATH_SUFFIX means the same as in that function.

   If elision does occur, then remove the mergeinfo for LOCAL_ABSPATH.

   If CHILD_MERGEINFO is NULL, do nothing.

   Use SCRATCH_POOL for temporary allocations.
*/
static svn_error_t *
elide_mergeinfo(svn_mergeinfo_t parent_mergeinfo,
                svn_mergeinfo_t child_mergeinfo,
                const char *local_abspath,
                svn_client_ctx_t *ctx,
                apr_pool_t *scratch_pool)
{
  svn_boolean_t elides;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(should_elide_mergeinfo(&elides,
                                 parent_mergeinfo, child_mergeinfo, NULL,
                                 scratch_pool));

  if (elides)
    {
      SVN_ERR(svn_wc_prop_set4(ctx->wc_ctx, local_abspath, SVN_PROP_MERGEINFO,
                               NULL, svn_depth_empty, TRUE, NULL,
                               NULL, NULL /* cancellation */,
                               NULL, NULL /* notification */,
                               scratch_pool));

      if (ctx->notify_func2)
        {
          svn_wc_notify_t *notify;

          notify = svn_wc_create_notify(local_abspath,
                                        svn_wc_notify_merge_elide_info,
                                        scratch_pool);
          ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);

          notify = svn_wc_create_notify(local_abspath,
                                        svn_wc_notify_update_update,
                                        scratch_pool);
          notify->prop_state = svn_wc_notify_state_changed;

          ctx->notify_func2(ctx->notify_baton2, notify, scratch_pool);
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_client__elide_mergeinfo(const char *target_abspath,
                            const char *wc_elision_limit_abspath,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool)
{
  const char *limit_abspath = wc_elision_limit_abspath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(target_abspath));
  SVN_ERR_ASSERT(!wc_elision_limit_abspath || svn_dirent_is_absolute(wc_elision_limit_abspath));

  /* Check for first easy out: We are already at the limit path. */
  if (!limit_abspath
      || strcmp(target_abspath, limit_abspath) != 0)
    {
      svn_mergeinfo_t target_mergeinfo;
      svn_mergeinfo_t mergeinfo = NULL;
      svn_error_t *err;

      /* Get the TARGET_WCPATH's explicit mergeinfo. */
      err = svn_client__get_wc_mergeinfo(&target_mergeinfo, NULL,
                                         svn_mergeinfo_explicit,
                                         target_abspath,
                                         NULL, NULL, FALSE,
                                         ctx, pool, pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
            {
              /* Issue #3896: If we attempt elision because invalid
                 mergeinfo is present on TARGET_WCPATH, then don't let
                 the merge fail, just skip the elision attempt. */
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
          else
            {
              return svn_error_trace(err);
            }
        }

     /* If TARGET_WCPATH has no explicit mergeinfo, there's nothing to
         elide, we're done. */
      if (target_mergeinfo == NULL)
        return SVN_NO_ERROR;

      /* Get TARGET_WCPATH's inherited mergeinfo from the WC. */
      err = svn_client__get_wc_mergeinfo(&mergeinfo, NULL,
                                         svn_mergeinfo_nearest_ancestor,
                                         target_abspath,
                                         limit_abspath,
                                         NULL, FALSE, ctx, pool, pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
            {
              /* Issue #3896 again, but invalid mergeinfo is inherited. */
              svn_error_clear(err);
              return SVN_NO_ERROR;
            }
          else
            {
              return svn_error_trace(err);
            }
        }

      /* If TARGET_WCPATH inherited no mergeinfo from the WC and we are
         not limiting our search to the working copy then check if it
         inherits any from the repos. */
      if (!mergeinfo && !wc_elision_limit_abspath)
        {
          err = svn_client__get_wc_or_repos_mergeinfo(
            &mergeinfo, NULL, NULL, TRUE,
            svn_mergeinfo_nearest_ancestor,
            NULL, target_abspath, ctx, pool);
          if (err)
            {
              if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
                {
                  /* Issue #3896 again, but invalid mergeinfo is inherited
                     from the repository. */
                  svn_error_clear(err);
                  return SVN_NO_ERROR;
                }
              else
                {
                  return svn_error_trace(err);
                }
            }
        }

      /* If there is nowhere to elide TARGET_WCPATH's mergeinfo to and
         the elision is limited, then we are done.*/
      if (!mergeinfo && wc_elision_limit_abspath)
        return SVN_NO_ERROR;

      SVN_ERR(elide_mergeinfo(mergeinfo, target_mergeinfo, target_abspath,
                              ctx, pool));
    }
  return SVN_NO_ERROR;
}


/* Set *MERGEINFO_CATALOG to the explicit or inherited mergeinfo for
   PATH_OR_URL@PEG_REVISION.  If INCLUDE_DESCENDANTS is true, also
   store in *MERGEINFO_CATALOG the explicit mergeinfo on any subtrees
   under PATH_OR_URL.  Key all mergeinfo in *MERGEINFO_CATALOG on
   repository relpaths.

   If no mergeinfo is found then set *MERGEINFO_CATALOG to NULL.

   Set *REPOS_ROOT to the root URL of the repository associated with
   PATH_OR_URL.

   If RA_SESSION is NOT NULL and PATH_OR_URL refers to a URL, RA_SESSION
   (which must be of the repository containing PATH_OR_URL) will be used
   instead of a temporary RA session. Caller is responsible for reparenting
   the session if it wants to use it after the call.

   Allocate *MERGEINFO_CATALOG and all its contents in RESULT_POOL.  Use
   SCRATCH_POOL for all temporary allocations.

   Return SVN_ERR_UNSUPPORTED_FEATURE if the server does not support
   Merge Tracking.  */
static svn_error_t *
get_mergeinfo(svn_mergeinfo_catalog_t *mergeinfo_catalog,
              const char **repos_root,
              const char *path_or_url,
              const svn_opt_revision_t *peg_revision,
              svn_boolean_t include_descendants,
              svn_boolean_t ignore_invalid_mergeinfo,
              svn_client_ctx_t *ctx,
              svn_ra_session_t *ra_session,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  const char *local_abspath;
  svn_boolean_t use_url = svn_path_is_url(path_or_url);
  svn_client__pathrev_t *peg_loc;

  if (ra_session && svn_path_is_url(path_or_url))
    {
      SVN_ERR(svn_ra_reparent(ra_session, path_or_url, scratch_pool));
      SVN_ERR(svn_client__resolve_rev_and_url(&peg_loc, ra_session,
                                              path_or_url,
                                              peg_revision,
                                              peg_revision,
                                              ctx, scratch_pool));
    }
  else
    {
      SVN_ERR(svn_client__ra_session_from_path2(&ra_session, &peg_loc,
                                                path_or_url, NULL,
                                                peg_revision,
                                                peg_revision, ctx, scratch_pool));
    }

  /* If PATH_OR_URL is as working copy path determine if we will need to
     contact the repository for the requested PEG_REVISION. */
  if (!use_url)
    {
      svn_client__pathrev_t *origin;
      SVN_ERR(svn_dirent_get_absolute(&local_abspath, path_or_url,
                                      scratch_pool));

      SVN_ERR(svn_client__wc_node_get_origin(&origin, local_abspath, ctx,
                                             scratch_pool, scratch_pool));
      if (!origin
          || strcmp(origin->url, peg_loc->url) != 0
          || peg_loc->rev != origin->rev)
      {
        use_url = TRUE; /* Don't rely on local mergeinfo */
      }
    }

  SVN_ERR(svn_ra_get_repos_root2(ra_session, repos_root, result_pool));

  if (use_url)
    {
      SVN_ERR(svn_client__get_repos_mergeinfo_catalog(
        mergeinfo_catalog, ra_session, peg_loc->url, peg_loc->rev,
        svn_mergeinfo_inherited, FALSE, include_descendants,
        result_pool, scratch_pool));
    }
  else /* ! svn_path_is_url() */
    {
      SVN_ERR(svn_client__get_wc_or_repos_mergeinfo_catalog(
        mergeinfo_catalog, NULL, NULL, include_descendants, FALSE,
        ignore_invalid_mergeinfo, svn_mergeinfo_inherited,
        ra_session, path_or_url, ctx,
        result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/*** In-memory mergeinfo elision ***/
svn_error_t *
svn_client__elide_mergeinfo_catalog(svn_mergeinfo_catalog_t mergeinfo_catalog,
                                    apr_pool_t *scratch_pool)
{
  apr_array_header_t *sorted_hash;
  apr_array_header_t *elidable_paths = apr_array_make(scratch_pool, 1,
                                                      sizeof(const char *));
  apr_array_header_t *dir_stack = apr_array_make(scratch_pool, 1,
                                                 sizeof(const char *));
  apr_pool_t *iterpool;
  int i;

  /* Here's the general algorithm:
     Walk through the paths sorted in tree order.  For each path, pop
     the dir_stack until it is either empty or the top item contains a parent
     of the current path. Check to see if that mergeinfo is then elidable,
     and build the list of elidable mergeinfo based upon that determination.
     Finally, push the path of interest onto the stack, and continue. */
  sorted_hash = svn_sort__hash(mergeinfo_catalog,
                               svn_sort_compare_items_as_paths,
                               scratch_pool);
  iterpool = svn_pool_create(scratch_pool);
  for (i = 0; i < sorted_hash->nelts; i++)
    {
      svn_sort__item_t *item = &APR_ARRAY_IDX(sorted_hash, i,
                                              svn_sort__item_t);
      const char *path = item->key;

      if (dir_stack->nelts > 0)
        {
          const char *top;
          const char *path_suffix;
          svn_boolean_t elides = FALSE;

          svn_pool_clear(iterpool);

          /* Pop off any paths which are not ancestors of PATH. */
          do
            {
              top = APR_ARRAY_IDX(dir_stack, dir_stack->nelts - 1,
                                          const char *);
              path_suffix = svn_dirent_is_child(top, path, NULL);

              if (!path_suffix)
                apr_array_pop(dir_stack);
            }
          while (dir_stack->nelts > 0 && !path_suffix);

          /* If we have a path suffix, it means we haven't popped the stack
             clean. */
          if (path_suffix)
            {
              SVN_ERR(should_elide_mergeinfo(&elides,
                                         svn_hash_gets(mergeinfo_catalog, top),
                                         svn_hash_gets(mergeinfo_catalog, path),
                                         path_suffix,
                                         iterpool));

              if (elides)
                APR_ARRAY_PUSH(elidable_paths, const char *) = path;
            }
        }

      APR_ARRAY_PUSH(dir_stack, const char *) = path;
    }
  svn_pool_destroy(iterpool);

  /* Now remove the elidable paths from the catalog. */
  for (i = 0; i < elidable_paths->nelts; i++)
    {
      const char *path = APR_ARRAY_IDX(elidable_paths, i, const char *);
      svn_hash_sets(mergeinfo_catalog, path, NULL);
    }

  return SVN_NO_ERROR;
}


/* Helper for filter_log_entry_with_rangelist().

   DEPTH_FIRST_CATALOG_INDEX is an array of svn_sort__item_t's.  The keys are
   repository-absolute const char *paths, the values are svn_mergeinfo_t for
   each path.

   Return a pointer to the mergeinfo value of the nearest path-wise ancestor
   of FSPATH in DEPTH_FIRST_CATALOG_INDEX.  A path is considered its
   own ancestor, so if a key exactly matches FSPATH, return that
   key's mergeinfo and set *ANCESTOR_IS_SELF to true (set it to false in all
   other cases).

   If DEPTH_FIRST_CATALOG_INDEX is NULL, empty, or no ancestor is found, then
   return NULL. */
static svn_mergeinfo_t
find_nearest_ancestor(const apr_array_header_t *depth_first_catalog_index,
                      svn_boolean_t *ancestor_is_self,
                      const char *fspath)
{
  int ancestor_index = -1;

  *ancestor_is_self = FALSE;

  if (depth_first_catalog_index)
    {
      int i;

      for (i = 0; i < depth_first_catalog_index->nelts; i++)
        {
          svn_sort__item_t item = APR_ARRAY_IDX(depth_first_catalog_index, i,
                                                svn_sort__item_t);
          if (svn_fspath__skip_ancestor(item.key, fspath))
            {
              ancestor_index = i;

              /* There's no nearer ancestor than FSPATH itself. */
              if (strcmp(item.key, fspath) == 0)
                {
                  *ancestor_is_self = TRUE;
                  break;
                }
            }

        }
    }

  if (ancestor_index == -1)
    return NULL;
  else
    return (APR_ARRAY_IDX(depth_first_catalog_index,
                          ancestor_index,
                          svn_sort__item_t)).value;
}

/* Baton for use with the filter_log_entry_with_rangelist()
   svn_log_entry_receiver_t callback. */
struct filter_log_entry_baton_t
{
  /* Is TRUE if RANGELIST describes potentially merged revisions, is FALSE
     if RANGELIST describes potentially eligible revisions. */
  svn_boolean_t filtering_merged;

  /* Unsorted array of repository relative paths representing the merge
     sources.  There will be more than one source  */
  const apr_array_header_t *merge_source_fspaths;

  /* The repository-absolute path we are calling svn_client_log5() on. */
  const char *target_fspath;

  /* Mergeinfo catalog for the tree rooted at TARGET_FSPATH.
     The path keys must be repository-absolute. */
  svn_mergeinfo_catalog_t target_mergeinfo_catalog;

  /* Depth first sorted array of svn_sort__item_t's for
     TARGET_MERGEINFO_CATALOG. */
  apr_array_header_t *depth_first_catalog_index;

  /* A rangelist describing all the revisions potentially merged or
     potentially eligible for merging (see FILTERING_MERGED) based on
     the target's explicit or inherited mergeinfo. */
  const svn_rangelist_t *rangelist;

  /* The wrapped svn_log_entry_receiver_t callback and baton which
     filter_log_entry_with_rangelist() is acting as a filter for. */
  svn_log_entry_receiver_t log_receiver;
  void *log_receiver_baton;

  svn_client_ctx_t *ctx;
};

/* Implements the svn_log_entry_receiver_t interface.  BATON is a
   `struct filter_log_entry_baton_t *'.

   Call the wrapped log receiver BATON->log_receiver (with
   BATON->log_receiver_baton) if:

   BATON->FILTERING_MERGED is FALSE and the changes represented by LOG_ENTRY
   have been fully merged from BATON->merge_source_fspaths to the WC target
   based on the mergeinfo for the WC contained in BATON->TARGET_MERGEINFO_CATALOG.

   Or

   BATON->FILTERING_MERGED is TRUE and the changes represented by LOG_ENTRY
   have not been merged, or only partially merged, from
   BATON->merge_source_fspaths to the WC target based on the mergeinfo for the
   WC contained in BATON->TARGET_MERGEINFO_CATALOG. */
static svn_error_t *
filter_log_entry_with_rangelist(void *baton,
                                svn_log_entry_t *log_entry,
                                apr_pool_t *pool)
{
  struct filter_log_entry_baton_t *fleb = baton;
  svn_rangelist_t *intersection, *this_rangelist;

  if (fleb->ctx->cancel_func)
    SVN_ERR(fleb->ctx->cancel_func(fleb->ctx->cancel_baton));

  /* Ignore r0 because there can be no "change 0" in a merge range. */
  if (log_entry->revision == 0)
    return SVN_NO_ERROR;

  this_rangelist = svn_rangelist__initialize(log_entry->revision - 1,
                                             log_entry->revision,
                                             TRUE, pool);

  /* Don't consider inheritance yet, see if LOG_ENTRY->REVISION is
     fully or partially represented in BATON->RANGELIST. */
  SVN_ERR(svn_rangelist_intersect(&intersection, fleb->rangelist,
                                  this_rangelist, FALSE, pool));
  if (! (intersection && intersection->nelts))
    return SVN_NO_ERROR;

  SVN_ERR_ASSERT(intersection->nelts == 1);

  /* Ok, we know LOG_ENTRY->REVISION is represented in BATON->RANGELIST,
     but is it only partially represented, i.e. is the corresponding range in
     BATON->RANGELIST non-inheritable?  Ask for the same intersection as
     above but consider inheritance this time, if the intersection is empty
     we know the range in BATON->RANGELIST is non-inheritable. */
  SVN_ERR(svn_rangelist_intersect(&intersection, fleb->rangelist,
                                  this_rangelist, TRUE, pool));
  log_entry->non_inheritable = !intersection->nelts;

  /* If the paths changed by LOG_ENTRY->REVISION are provided we can determine
     if LOG_ENTRY->REVISION, while only partially represented in
     BATON->RANGELIST, is in fact completely applied to all affected paths.
     ### And ... what if it is, or if it isn't? What do we do with the answer?
         And how do we cope if the changed paths are not provided? */
  if ((log_entry->non_inheritable || !fleb->filtering_merged)
      && log_entry->changed_paths2)
    {
      apr_hash_index_t *hi;
      svn_boolean_t all_subtrees_have_this_rev = TRUE;
      svn_rangelist_t *this_rev_rangelist =
        svn_rangelist__initialize(log_entry->revision - 1,
                                  log_entry->revision, TRUE, pool);
      apr_pool_t *iterpool = svn_pool_create(pool);

      for (hi = apr_hash_first(pool, log_entry->changed_paths2);
           hi;
           hi = apr_hash_next(hi))
        {
          int i;
          const char *path = apr_hash_this_key(hi);
          svn_log_changed_path2_t *change = apr_hash_this_val(hi);
          const char *target_fspath_affected;
          svn_mergeinfo_t nearest_ancestor_mergeinfo;
          svn_boolean_t found_this_revision = FALSE;
          const char *merge_source_rel_target;
          const char *merge_source_fspath;
          svn_boolean_t ancestor_is_self;

          svn_pool_clear(iterpool);

          /* Check that PATH is a subtree of at least one of the
             merge sources.  If not then ignore this path.  */
          for (i = 0; i < fleb->merge_source_fspaths->nelts; i++)
            {
              merge_source_fspath = APR_ARRAY_IDX(fleb->merge_source_fspaths,
                                                  i, const char *);

              merge_source_rel_target
                = svn_fspath__skip_ancestor(merge_source_fspath, path);
              if (merge_source_rel_target)
                {
                  /* If MERGE_SOURCE was itself deleted, replaced, or added
                     in LOG_ENTRY->REVISION then ignore this PATH since you
                     can't merge a addition or deletion of yourself. */
                  if (merge_source_rel_target[0] == '\0'
                      && (change->action != 'M'))
                    i = fleb->merge_source_fspaths->nelts;
                  break;
                }
            }
          /* If we examined every merge source path and PATH is a child of
             none of them then we can ignore this PATH. */
          if (i == fleb->merge_source_fspaths->nelts)
            continue;

          /* Calculate the target path which PATH would affect if merged. */
          target_fspath_affected = svn_fspath__join(fleb->target_fspath,
                                                    merge_source_rel_target,
                                                    iterpool);

          nearest_ancestor_mergeinfo =
            find_nearest_ancestor(fleb->depth_first_catalog_index,
                                  &ancestor_is_self,
                                  target_fspath_affected);

          /* Issue #3791: A path should never have explicit mergeinfo
             describing its own addition (that's self-referential).  Nor will
             it have explicit mergeinfo describing its own deletion (we
             obviously can't add new mergeinfo to a path we are deleting).

             This lack of explicit mergeinfo should not cause such revisions
             to show up as eligible however.  If PATH was deleted, replaced,
             or added in LOG_ENTRY->REVISION, but the corresponding
             TARGET_PATH_AFFECTED already exists and has explicit mergeinfo
             describing merges from PATH *after* LOG_ENTRY->REVISION, then
             ignore this PATH.  If it was deleted in LOG_ENTRY->REVISION it's
             obviously back.  If it was added or replaced it's still around
             possibly it was replaced one or more times, but it's back now.
             Regardless, LOG_ENTRY->REVISION is *not* an eligible revision! */
          if (nearest_ancestor_mergeinfo &&
              ancestor_is_self /* Explicit mergeinfo on TARGET_PATH_AFFECTED */
              && (change->action != 'M'))
            {
              svn_rangelist_t *rangelist =
                  svn_hash_gets(nearest_ancestor_mergeinfo, path);
              if (rangelist)
                {
                  svn_merge_range_t *youngest_range = APR_ARRAY_IDX(
                    rangelist, rangelist->nelts - 1, svn_merge_range_t *);

                  if (youngest_range
                      && (youngest_range->end > log_entry->revision))
                    continue;
                }
            }

          if (nearest_ancestor_mergeinfo)
            {
              apr_hash_index_t *hi2;

              for (hi2 = apr_hash_first(iterpool, nearest_ancestor_mergeinfo);
                   hi2;
                   hi2 = apr_hash_next(hi2))
                {
                  const char *mergeinfo_path = apr_hash_this_key(hi2);
                  svn_rangelist_t *rangelist = apr_hash_this_val(hi2);

                  /* Does the mergeinfo for PATH reflect if
                     LOG_ENTRY->REVISION was previously merged
                     from MERGE_SOURCE_FSPATH? */
                  if (svn_fspath__skip_ancestor(merge_source_fspath,
                                                mergeinfo_path))
                    {
                      /* Something was merged from MERGE_SOURCE_FSPATH, does
                         it include LOG_ENTRY->REVISION? */
                      SVN_ERR(svn_rangelist_intersect(&intersection,
                                                      rangelist,
                                                      this_rev_rangelist,
                                                      FALSE,
                                                      iterpool));
                      if (intersection->nelts)
                        {
                          if (ancestor_is_self)
                            {
                              /* TARGET_PATH_AFFECTED has explicit mergeinfo,
                                 so we don't need to worry if that mergeinfo
                                 is inheritable or not. */
                              found_this_revision = TRUE;
                              break;
                            }
                          else
                            {
                              /* TARGET_PATH_AFFECTED inherited its mergeinfo,
                                 so we have to ignore non-inheritable
                                 ranges. */
                              SVN_ERR(svn_rangelist_intersect(
                                &intersection,
                                rangelist,
                                this_rev_rangelist,
                                TRUE, iterpool));
                              if (intersection->nelts)
                                {
                                  found_this_revision = TRUE;
                                  break;
                                }
                            }
                        }
                    }
                }
            }

          if (!found_this_revision)
            {
              /* As soon as any PATH is found that is not fully merged for
                 LOG_ENTRY->REVISION then we can stop. */
              all_subtrees_have_this_rev = FALSE;
              break;
            }
        }

      svn_pool_destroy(iterpool);

      if (all_subtrees_have_this_rev)
        {
          if (fleb->filtering_merged)
            log_entry->non_inheritable = FALSE;
          else
            return SVN_NO_ERROR;
        }
    }

  /* Call the wrapped log receiver which this function is filtering for. */
  return fleb->log_receiver(fleb->log_receiver_baton, log_entry, pool);
}

static svn_error_t *
logs_for_mergeinfo_rangelist(const char *source_url,
                             const apr_array_header_t *merge_source_fspaths,
                             svn_boolean_t filtering_merged,
                             const svn_rangelist_t *rangelist,
                             svn_boolean_t oldest_revs_first,
                             svn_mergeinfo_catalog_t target_mergeinfo_catalog,
                             const char *target_fspath,
                             svn_boolean_t discover_changed_paths,
                             const apr_array_header_t *revprops,
                             svn_log_entry_receiver_t log_receiver,
                             void *log_receiver_baton,
                             svn_client_ctx_t *ctx,
                             svn_ra_session_t *ra_session,
                             apr_pool_t *scratch_pool)
{
  svn_merge_range_t *oldest_range, *youngest_range;
  svn_revnum_t oldest_rev, youngest_rev;
  struct filter_log_entry_baton_t fleb;

  if (! rangelist->nelts)
    return SVN_NO_ERROR;

  /* Calculate and construct the bounds of our log request. */
  youngest_range = APR_ARRAY_IDX(rangelist, rangelist->nelts - 1,
                                 svn_merge_range_t *);
  youngest_rev = youngest_range->end;
  oldest_range = APR_ARRAY_IDX(rangelist, 0, svn_merge_range_t *);
  oldest_rev = oldest_range->start;

  if (! target_mergeinfo_catalog)
    target_mergeinfo_catalog = apr_hash_make(scratch_pool);

  /* FILTER_LOG_ENTRY_BATON_T->TARGET_MERGEINFO_CATALOG's keys are required
     to be repository-absolute. */
  SVN_ERR(svn_mergeinfo__add_prefix_to_catalog(&target_mergeinfo_catalog,
                                               target_mergeinfo_catalog, "/",
                                               scratch_pool, scratch_pool));

  /* Build the log filtering callback baton. */
  fleb.filtering_merged = filtering_merged;
  fleb.merge_source_fspaths = merge_source_fspaths;
  fleb.target_mergeinfo_catalog = target_mergeinfo_catalog;
  fleb.depth_first_catalog_index =
    svn_sort__hash(target_mergeinfo_catalog,
                   svn_sort_compare_items_as_paths,
                   scratch_pool);
  fleb.target_fspath = target_fspath;
  fleb.rangelist = rangelist;
  fleb.log_receiver = log_receiver;
  fleb.log_receiver_baton = log_receiver_baton;
  fleb.ctx = ctx;

  if (!ra_session)
    SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, source_url,
                                                 NULL, NULL, FALSE, FALSE, ctx,
                                                 scratch_pool, scratch_pool));
  else
    SVN_ERR(svn_ra_reparent(ra_session, source_url, scratch_pool));

  {
    apr_array_header_t *target;
    target = apr_array_make(scratch_pool, 1, sizeof(const char *));
    APR_ARRAY_PUSH(target, const char *) = "";

    SVN_ERR(svn_ra_get_log2(ra_session, target,
                            oldest_revs_first ? oldest_rev : youngest_rev,
                            oldest_revs_first ? youngest_rev : oldest_rev,
                            0 /* limit */,
                            discover_changed_paths,
                            FALSE /* strict_node_history */,
                            FALSE /* include_merged_revisions */,
                            revprops,
                            filter_log_entry_with_rangelist, &fleb,
                            scratch_pool));
  }

  /* Check for cancellation. */
  if (ctx->cancel_func)
    SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

  return SVN_NO_ERROR;
}

/* Set *OUT_MERGEINFO to a shallow copy of MERGEINFO with each source path
   converted to a (URI-encoded) URL based on REPOS_ROOT_URL. *OUT_MERGEINFO
   is declared as 'apr_hash_t *' because its key do not obey the rules of
   'svn_mergeinfo_t'.

   Allocate *OUT_MERGEINFO and the new keys in RESULT_POOL.  Use
   SCRATCH_POOL for any temporary allocations. */
static svn_error_t *
mergeinfo_relpaths_to_urls(apr_hash_t **out_mergeinfo,
                           svn_mergeinfo_t mergeinfo,
                           const char *repos_root_url,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  *out_mergeinfo = NULL;
  if (mergeinfo)
    {
      apr_hash_index_t *hi;
      apr_hash_t *full_path_mergeinfo = apr_hash_make(result_pool);

      for (hi = apr_hash_first(scratch_pool, mergeinfo);
           hi; hi = apr_hash_next(hi))
        {
          const char *key = apr_hash_this_key(hi);
          void *val = apr_hash_this_val(hi);

          svn_hash_sets(full_path_mergeinfo,
                        svn_path_url_add_component2(repos_root_url, key + 1,
                                                    result_pool),
                        val);
        }
      *out_mergeinfo = full_path_mergeinfo;
    }

  return SVN_NO_ERROR;
}


/*** Public APIs ***/

svn_error_t *
svn_client_mergeinfo_get_merged(apr_hash_t **mergeinfo_p,
                                const char *path_or_url,
                                const svn_opt_revision_t *peg_revision,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *pool)
{
  const char *repos_root;
  svn_mergeinfo_catalog_t mergeinfo_cat;
  svn_mergeinfo_t mergeinfo;

  SVN_ERR(get_mergeinfo(&mergeinfo_cat, &repos_root, path_or_url,
                        peg_revision, FALSE, FALSE, ctx, NULL, pool, pool));
  if (mergeinfo_cat)
    {
      const char *repos_relpath;

      if (! svn_path_is_url(path_or_url))
        {
          SVN_ERR(svn_dirent_get_absolute(&path_or_url, path_or_url, pool));
          SVN_ERR(svn_wc__node_get_repos_info(NULL, &repos_relpath, NULL, NULL,
                                              ctx->wc_ctx, path_or_url,
                                              pool, pool));
        }
      else
        {
          repos_relpath = svn_uri_skip_ancestor(repos_root, path_or_url, pool);

          SVN_ERR_ASSERT(repos_relpath != NULL); /* Or get_mergeinfo failed */
        }

      mergeinfo = svn_hash_gets(mergeinfo_cat, repos_relpath);
    }
  else
    {
      mergeinfo = NULL;
    }

  SVN_ERR(mergeinfo_relpaths_to_urls(mergeinfo_p, mergeinfo,
                                     repos_root, pool, pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__mergeinfo_log(svn_boolean_t finding_merged,
                          const char *target_path_or_url,
                          const svn_opt_revision_t *target_peg_revision,
                          svn_mergeinfo_catalog_t *target_mergeinfo_catalog,
                          const char *source_path_or_url,
                          const svn_opt_revision_t *source_peg_revision,
                          const svn_opt_revision_t *source_start_revision,
                          const svn_opt_revision_t *source_end_revision,
                          svn_log_entry_receiver_t log_receiver,
                          void *log_receiver_baton,
                          svn_boolean_t discover_changed_paths,
                          svn_depth_t depth,
                          const apr_array_header_t *revprops,
                          svn_client_ctx_t *ctx,
                          svn_ra_session_t *ra_session,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  const char *log_target = NULL;
  const char *repos_root;
  const char *target_repos_relpath;
  svn_mergeinfo_catalog_t target_mergeinfo_cat;
  svn_ra_session_t *target_session = NULL;
  svn_client__pathrev_t *pathrev;

  /* A hash of paths, at or under TARGET_PATH_OR_URL, mapped to
     rangelists.  Not technically mergeinfo, so not using the
     svn_mergeinfo_t type. */
  apr_hash_t *inheritable_subtree_merges;

  svn_mergeinfo_t source_history;
  svn_mergeinfo_t target_history;
  svn_rangelist_t *master_noninheritable_rangelist;
  svn_rangelist_t *master_inheritable_rangelist;
  apr_array_header_t *merge_source_fspaths =
    apr_array_make(scratch_pool, 1, sizeof(const char *));
  apr_hash_index_t *hi_catalog;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;
  svn_boolean_t oldest_revs_first = TRUE;
  apr_pool_t *subpool;

  /* We currently only support depth = empty | infinity. */
  if (depth != svn_depth_infinity && depth != svn_depth_empty)
    return svn_error_create(
      SVN_ERR_UNSUPPORTED_FEATURE, NULL,
      _("Only depths 'infinity' and 'empty' are currently supported"));

  /* Validate and sanitize the incoming source operative revision range. */
  if (!((source_start_revision->kind == svn_opt_revision_unspecified) ||
        (source_start_revision->kind == svn_opt_revision_number) ||
        (source_start_revision->kind == svn_opt_revision_date) ||
        (source_start_revision->kind == svn_opt_revision_head)))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);
  if (!((source_end_revision->kind == svn_opt_revision_unspecified) ||
        (source_end_revision->kind == svn_opt_revision_number) ||
        (source_end_revision->kind == svn_opt_revision_date) ||
        (source_end_revision->kind == svn_opt_revision_head)))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);
  if ((source_end_revision->kind != svn_opt_revision_unspecified)
      && (source_start_revision->kind == svn_opt_revision_unspecified))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);
  if ((source_end_revision->kind == svn_opt_revision_unspecified)
      && (source_start_revision->kind != svn_opt_revision_unspecified))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL, NULL);

  subpool = svn_pool_create(scratch_pool);

  if (ra_session)
    target_session = ra_session;

  /* We need the union of TARGET_PATH_OR_URL@TARGET_PEG_REVISION's mergeinfo
     and MERGE_SOURCE_URL's history.  It's not enough to do path
     matching, because renames in the history of MERGE_SOURCE_URL
     throw that all in a tizzy.  Of course, if there's no mergeinfo on
     the target, that vastly simplifies matters (we'll have nothing to
     do). */
  /* This get_mergeinfo() call doubles as a mergeinfo capabilities check. */
  if (target_mergeinfo_catalog)
    {
      if (*target_mergeinfo_catalog)
        {
          /* The caller provided the mergeinfo catalog for
             TARGET_PATH_OR_URL, so we don't need to accquire
             it ourselves.  We do need to get the repos_root
             though, because get_mergeinfo() won't do it for us. */
          target_mergeinfo_cat = *target_mergeinfo_catalog;

          if (ra_session && svn_path_is_url(target_path_or_url))
            {
              SVN_ERR(svn_ra_reparent(ra_session, target_path_or_url, subpool));
              SVN_ERR(svn_client__resolve_rev_and_url(&pathrev, ra_session,
                                                      target_path_or_url,
                                                      target_peg_revision,
                                                      target_peg_revision,
                                                      ctx, subpool));
              target_session = ra_session;
            }
          else
            {
              SVN_ERR(svn_client__ra_session_from_path2(&target_session,
                                                        &pathrev,
                                                        target_path_or_url,
                                                        NULL,
                                                        target_peg_revision,
                                                        target_peg_revision,
                                                        ctx, subpool));
            }
          SVN_ERR(svn_ra_get_repos_root2(target_session, &repos_root,
                                         scratch_pool));
        }
      else
        {
          /* The caller didn't provide the mergeinfo catalog for
             TARGET_PATH_OR_URL, but wants us to pass a copy back
             when we get it, so use RESULT_POOL. */
          SVN_ERR(get_mergeinfo(target_mergeinfo_catalog, &repos_root,
                                target_path_or_url, target_peg_revision,
                                depth == svn_depth_infinity, TRUE,
                                ctx, ra_session, result_pool, scratch_pool));
          target_mergeinfo_cat = *target_mergeinfo_catalog;
        }
    }
  else
    {
      /* The caller didn't provide the mergeinfo catalog for
         TARGET_PATH_OR_URL, nor does it want a copy, so we can use
         nothing but SCRATCH_POOL. */
      SVN_ERR(get_mergeinfo(&target_mergeinfo_cat, &repos_root,
                            target_path_or_url, target_peg_revision,
                            depth == svn_depth_infinity, TRUE,
                            ctx, ra_session, scratch_pool, scratch_pool));
    }

  if (!svn_path_is_url(target_path_or_url))
    {
      SVN_ERR(svn_dirent_get_absolute(&target_path_or_url,
                                      target_path_or_url, scratch_pool));
      SVN_ERR(svn_wc__node_get_repos_info(NULL, &target_repos_relpath,
                                          NULL, NULL,
                                          ctx->wc_ctx, target_path_or_url,
                                          scratch_pool, scratch_pool));
    }
  else
    {
      target_repos_relpath = svn_uri_skip_ancestor(repos_root,
                                                   target_path_or_url,
                                                   scratch_pool);

      /* TARGET_REPOS_REL should be non-NULL, else get_mergeinfo
         should have failed.  */
      SVN_ERR_ASSERT(target_repos_relpath != NULL);
    }

  if (!target_mergeinfo_cat)
    {
      /* If we are looking for what has been merged and there is no
         mergeinfo then we already know the answer.  If we are looking
         for eligible revisions then create a catalog with empty mergeinfo
         on the target.  This is semantically equivalent to no mergeinfo
         and gives us something to combine with MERGE_SOURCE_URL's
         history. */
      if (finding_merged)
        {
          svn_pool_destroy(subpool);
          return SVN_NO_ERROR;
        }
      else
        {
          target_mergeinfo_cat = apr_hash_make(scratch_pool);
          svn_hash_sets(target_mergeinfo_cat, target_repos_relpath,
                        apr_hash_make(scratch_pool));
        }
    }

  /* Fetch the location history as mergeinfo, for the source branch
   * (between the given start and end revisions), and, if we're finding
   * merged revisions, then also for the entire target branch.
   *
   * ### TODO: As the source and target must be in the same repository, we
   * should share a single session, tracking the two URLs separately. */
  {
    svn_ra_session_t *source_session;
    svn_revnum_t start_rev, end_rev, youngest_rev = SVN_INVALID_REVNUM;

    if (! finding_merged)
      {
        if (!target_session)
          SVN_ERR(svn_client__ra_session_from_path2(&target_session, &pathrev,
                                                    target_path_or_url, NULL,
                                                    target_peg_revision,
                                                    target_peg_revision,
                                                    ctx, subpool));
        SVN_ERR(svn_client__get_history_as_mergeinfo(&target_history, NULL,
                                                     pathrev,
                                                     SVN_INVALID_REVNUM,
                                                     SVN_INVALID_REVNUM,
                                                     target_session, ctx,
                                                     scratch_pool));
      }

    if (target_session
        && svn_path_is_url(source_path_or_url)
        && repos_root
        && svn_uri_skip_ancestor(repos_root, source_path_or_url, subpool))
      {
        /* We can re-use the existing session */
        source_session = target_session;
        SVN_ERR(svn_ra_reparent(source_session, source_path_or_url, subpool));
        SVN_ERR(svn_client__resolve_rev_and_url(&pathrev, source_session,
                                                source_path_or_url,
                                                source_peg_revision,
                                                source_peg_revision,
                                                ctx, subpool));
      }
    else
      {
        SVN_ERR(svn_client__ra_session_from_path2(&source_session, &pathrev,
                                                  source_path_or_url, NULL,
                                                  source_peg_revision,
                                                  source_peg_revision,
                                                  ctx, subpool));
      }
    SVN_ERR(svn_client__get_revision_number(&start_rev, &youngest_rev,
                                            ctx->wc_ctx, source_path_or_url,
                                            source_session,
                                            source_start_revision,
                                            subpool));
    SVN_ERR(svn_client__get_revision_number(&end_rev, &youngest_rev,
                                            ctx->wc_ctx, source_path_or_url,
                                            source_session,
                                            source_end_revision,
                                            subpool));
    SVN_ERR(svn_client__get_history_as_mergeinfo(&source_history, NULL,
                                                 pathrev,
                                                 MAX(end_rev, start_rev),
                                                 MIN(end_rev, start_rev),
                                                 source_session, ctx,
                                                 scratch_pool));
    if (start_rev > end_rev)
      oldest_revs_first = FALSE;
  }

  /* Separate the explicit or inherited mergeinfo on TARGET_PATH_OR_URL,
     and possibly its explicit subtree mergeinfo, into their
     inheritable and non-inheritable parts. */
  master_noninheritable_rangelist = apr_array_make(scratch_pool, 64,
                                                   sizeof(svn_merge_range_t *));
  master_inheritable_rangelist = apr_array_make(scratch_pool, 64,
                                                sizeof(svn_merge_range_t *));
  inheritable_subtree_merges = apr_hash_make(scratch_pool);

  iterpool = svn_pool_create(scratch_pool);

  for (hi_catalog = apr_hash_first(scratch_pool, target_mergeinfo_cat);
       hi_catalog;
       hi_catalog = apr_hash_next(hi_catalog))
    {
      svn_mergeinfo_t subtree_mergeinfo = apr_hash_this_val(hi_catalog);
      svn_mergeinfo_t subtree_history;
      svn_mergeinfo_t subtree_source_history;
      svn_mergeinfo_t subtree_inheritable_mergeinfo;
      svn_mergeinfo_t subtree_noninheritable_mergeinfo;
      svn_mergeinfo_t merged_noninheritable;
      svn_mergeinfo_t merged;
      const char *subtree_path = apr_hash_this_key(hi_catalog);
      svn_boolean_t is_subtree = strcmp(subtree_path,
                                        target_repos_relpath) != 0;
      svn_pool_clear(iterpool);

      if (is_subtree)
        {
          /* If SUBTREE_PATH is a proper subtree of TARGET_PATH_OR_URL
             then make a copy of SOURCE_HISTORY that is path adjusted
             for the subtree.  */
          const char *subtree_rel_path =
            subtree_path + strlen(target_repos_relpath) + 1;

          SVN_ERR(svn_mergeinfo__add_suffix_to_mergeinfo(
            &subtree_source_history, source_history,
            subtree_rel_path, scratch_pool, iterpool));

          if (!finding_merged)
            SVN_ERR(svn_mergeinfo__add_suffix_to_mergeinfo(
                    &subtree_history, target_history,
                    subtree_rel_path, scratch_pool, iterpool));
        }
      else
        {
          subtree_source_history = source_history;
          if (!finding_merged)
            subtree_history = target_history;
        }

      if (!finding_merged)
        {
          svn_mergeinfo_t merged_via_history;
          SVN_ERR(svn_mergeinfo_intersect2(&merged_via_history,
                                           subtree_history,
                                           subtree_source_history, TRUE,
                                           scratch_pool, iterpool));
          SVN_ERR(svn_mergeinfo_merge2(subtree_mergeinfo,
                                       merged_via_history,
                                       scratch_pool, iterpool));
        }

      SVN_ERR(svn_mergeinfo_inheritable2(&subtree_inheritable_mergeinfo,
                                         subtree_mergeinfo, NULL,
                                         SVN_INVALID_REVNUM,
                                         SVN_INVALID_REVNUM,
                                         TRUE, scratch_pool, iterpool));
      SVN_ERR(svn_mergeinfo_inheritable2(&subtree_noninheritable_mergeinfo,
                                         subtree_mergeinfo, NULL,
                                         SVN_INVALID_REVNUM,
                                         SVN_INVALID_REVNUM,
                                         FALSE, scratch_pool, iterpool));

      /* Find the intersection of the non-inheritable part of
         SUBTREE_MERGEINFO and SOURCE_HISTORY.  svn_mergeinfo_intersect2()
         won't consider non-inheritable and inheritable ranges
         intersecting unless we ignore inheritance, but in doing so the
         resulting intersections have all inheritable ranges.  To get
         around this we set the inheritance on the result to all
         non-inheritable. */
      SVN_ERR(svn_mergeinfo_intersect2(&merged_noninheritable,
                                       subtree_noninheritable_mergeinfo,
                                       subtree_source_history, FALSE,
                                       scratch_pool, iterpool));
      svn_mergeinfo__set_inheritance(merged_noninheritable, FALSE,
                                     iterpool);

      /* Keep track of all ranges partially merged to any and all
         subtrees. */
      SVN_ERR(svn_rangelist__merge_many(master_noninheritable_rangelist,
                                        merged_noninheritable,
                                        scratch_pool, iterpool));

      /* Find the intersection of the inheritable part of TGT_MERGEINFO
         and SOURCE_HISTORY. */
      SVN_ERR(svn_mergeinfo_intersect2(&merged,
                                       subtree_inheritable_mergeinfo,
                                       subtree_source_history, FALSE,
                                       scratch_pool, iterpool));

      /* Keep track of all ranges fully merged to any and all
         subtrees. */
      if (apr_hash_count(merged))
        {
          /* The inheritable rangelist merged from SUBTREE_SOURCE_HISTORY
             to SUBTREE_PATH. */
          svn_rangelist_t *subtree_merged_rangelist =
            apr_array_make(scratch_pool, 1, sizeof(svn_merge_range_t *));

          SVN_ERR(svn_rangelist__merge_many(master_inheritable_rangelist,
                                            merged, scratch_pool, iterpool));
          SVN_ERR(svn_rangelist__merge_many(subtree_merged_rangelist,
                                            merged, scratch_pool, iterpool));

          svn_hash_sets(inheritable_subtree_merges, subtree_path,
                        subtree_merged_rangelist);
        }
      else
        {
          /* Map SUBTREE_PATH to an empty rangelist if there was nothing
             fully merged. e.g. Only empty or non-inheritable mergeinfo
             on the subtree or mergeinfo unrelated to the source. */
          svn_hash_sets(inheritable_subtree_merges, subtree_path,
                        apr_array_make(scratch_pool, 0,
                                       sizeof(svn_merge_range_t *)));
        }
    }

  /* Make sure every range in MASTER_INHERITABLE_RANGELIST is fully merged to
     each subtree (including the target itself).  Any revisions which don't
     exist in *every* subtree are *potentially* only partially merged to the
     tree rooted at TARGET_PATH_OR_URL, so move those revisions to
     MASTER_NONINHERITABLE_RANGELIST.  It may turn out that that a revision
     was merged to the only subtree it affects, but we need to examine the
     logs to make this determination (which will be done by
     logs_for_mergeinfo_rangelist). */
  if (master_inheritable_rangelist->nelts)
    {
      for (hi = apr_hash_first(scratch_pool, inheritable_subtree_merges);
           hi;
           hi = apr_hash_next(hi))
        {
          svn_rangelist_t *deleted_rangelist;
          svn_rangelist_t *added_rangelist;
          svn_rangelist_t *subtree_merged_rangelist = apr_hash_this_val(hi);

          svn_pool_clear(iterpool);

          SVN_ERR(svn_rangelist_diff(&deleted_rangelist, &added_rangelist,
                                     master_inheritable_rangelist,
                                     subtree_merged_rangelist, TRUE,
                                     iterpool));

          if (deleted_rangelist->nelts)
            {
              svn_rangelist__set_inheritance(deleted_rangelist, FALSE);
              SVN_ERR(svn_rangelist_merge2(master_noninheritable_rangelist,
                                           deleted_rangelist,
                                           scratch_pool, iterpool));
              SVN_ERR(svn_rangelist_remove(&master_inheritable_rangelist,
                                           deleted_rangelist,
                                           master_inheritable_rangelist,
                                           FALSE,
                                           scratch_pool));
            }
        }
    }

  if (finding_merged)
    {
      /* Roll all the merged revisions into one rangelist. */
      SVN_ERR(svn_rangelist_merge2(master_inheritable_rangelist,
                                   master_noninheritable_rangelist,
                                   scratch_pool, scratch_pool));

    }
  else
    {
      /* Create the starting rangelist for what might be eligible. */
      svn_rangelist_t *source_master_rangelist =
        apr_array_make(scratch_pool, 1, sizeof(svn_merge_range_t *));

      SVN_ERR(svn_rangelist__merge_many(source_master_rangelist,
                                        source_history,
                                        scratch_pool, scratch_pool));

      /* From what might be eligible subtract what we know is
         partially merged and then merge that back. */
      SVN_ERR(svn_rangelist_remove(&source_master_rangelist,
                                   master_noninheritable_rangelist,
                                   source_master_rangelist,
                                   FALSE, scratch_pool));
      SVN_ERR(svn_rangelist_merge2(source_master_rangelist,
                                   master_noninheritable_rangelist,
                                   scratch_pool, scratch_pool));
      SVN_ERR(svn_rangelist_remove(&master_inheritable_rangelist,
                                   master_inheritable_rangelist,
                                   source_master_rangelist,
                                   TRUE, scratch_pool));
    }

  /* Nothing merged?  Not even when considering shared history if
     looking for eligible revisions (i.e. !FINDING_MERGED)?  Then there
     is nothing more to do. */
  if (! master_inheritable_rangelist->nelts)
    {
      svn_pool_destroy(iterpool);
      return SVN_NO_ERROR;
    }
  else
    {
      /* Determine the correct (youngest) target for 'svn log'. */
      svn_merge_range_t *youngest_range
        = APR_ARRAY_IDX(master_inheritable_rangelist,
                        master_inheritable_rangelist->nelts - 1,
                        svn_merge_range_t *);
      svn_rangelist_t *youngest_rangelist =
        svn_rangelist__initialize(youngest_range->end - 1,
                                  youngest_range->end,
                                  youngest_range->inheritable,
                                  scratch_pool);

      for (hi = apr_hash_first(scratch_pool, source_history);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *key = apr_hash_this_key(hi);
          svn_rangelist_t *subtree_merged_rangelist = apr_hash_this_val(hi);
          svn_rangelist_t *intersecting_rangelist;

          svn_pool_clear(iterpool);
          SVN_ERR(svn_rangelist_intersect(&intersecting_rangelist,
                                          youngest_rangelist,
                                          subtree_merged_rangelist,
                                          FALSE, iterpool));

          APR_ARRAY_PUSH(merge_source_fspaths, const char *) = key;

          if (intersecting_rangelist->nelts)
            log_target = key;
        }
    }

  svn_pool_destroy(iterpool);

  /* Step 4: Finally, we run 'svn log' to drive our log receiver, but
     using a receiver filter to only allow revisions to pass through
     that are in our rangelist. */
  log_target = svn_path_url_add_component2(repos_root, log_target + 1,
                                           scratch_pool);

  {
    svn_error_t *err;

    err = logs_for_mergeinfo_rangelist(log_target, merge_source_fspaths,
                                       finding_merged,
                                       master_inheritable_rangelist,
                                       oldest_revs_first,
                                       target_mergeinfo_cat,
                                       svn_fspath__join("/",
                                                        target_repos_relpath,
                                                        scratch_pool),
                                       discover_changed_paths,
                                       revprops,
                                       log_receiver, log_receiver_baton,
                                       ctx, target_session, scratch_pool);

    /* Close the source and target sessions. */
    svn_pool_destroy(subpool); /* For SVN_ERR_CEASE_INVOCATION */

    return svn_error_trace(err);
  }
}

svn_error_t *
svn_client_mergeinfo_log2(svn_boolean_t finding_merged,
                          const char *target_path_or_url,
                          const svn_opt_revision_t *target_peg_revision,
                          const char *source_path_or_url,
                          const svn_opt_revision_t *source_peg_revision,
                          const svn_opt_revision_t *source_start_revision,
                          const svn_opt_revision_t *source_end_revision,
                          svn_log_entry_receiver_t log_receiver,
                          void *log_receiver_baton,
                          svn_boolean_t discover_changed_paths,
                          svn_depth_t depth,
                          const apr_array_header_t *revprops,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *scratch_pool)
{
  return svn_error_trace(
         svn_client__mergeinfo_log(finding_merged, target_path_or_url,
                                   target_peg_revision, NULL,
                                   source_path_or_url, source_peg_revision,
                                   source_start_revision, source_end_revision,
                                   log_receiver, log_receiver_baton,
                                   discover_changed_paths, depth, revprops,
                                   ctx, NULL,
                                   scratch_pool, scratch_pool));
}

svn_error_t *
svn_client_suggest_merge_sources(apr_array_header_t **suggestions,
                                 const char *path_or_url,
                                 const svn_opt_revision_t *peg_revision,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool)
{
  const char *repos_root;
  const char *copyfrom_path;
  apr_array_header_t *list;
  svn_revnum_t copyfrom_rev;
  svn_mergeinfo_catalog_t mergeinfo_cat;
  svn_mergeinfo_t mergeinfo;
  apr_hash_index_t *hi;
  apr_pool_t *session_pool = svn_pool_create(pool);
  svn_ra_session_t *ra_session;

  list = apr_array_make(pool, 1, sizeof(const char *));

  /* In our ideal algorithm, the list of recommendations should be
     ordered by:

        1. The most recent existing merge source.
        2. The copyfrom source (which will also be listed as a merge
           source if the copy was made with a 1.5+ client and server).
        3. All other merge sources, most recent to least recent.

     However, determining the order of application of merge sources
     requires a new RA API.  Until such an API is available, our
     algorithm will be:

        1. The copyfrom source.
        2. All remaining merge sources (unordered).
  */
  SVN_ERR(svn_client__ra_session_from_path2(&ra_session, NULL, path_or_url,
                                            NULL, peg_revision, peg_revision,
                                            ctx, session_pool));

  SVN_ERR(get_mergeinfo(&mergeinfo_cat, &repos_root, path_or_url,
                        peg_revision, FALSE, FALSE,
                        ctx, ra_session, session_pool, session_pool));

  if (mergeinfo_cat && apr_hash_count(mergeinfo_cat))
    {
      /* We asked only for the PATH_OR_URL's mergeinfo, not any of its
         descendants.  So if there is anything in the catalog it is the
         mergeinfo for PATH_OR_URL. */
      mergeinfo = apr_hash_this_val(apr_hash_first(session_pool,
                                                   mergeinfo_cat));
    }
  else
    {
      mergeinfo = NULL;
    }

  /* ### Should we only add the last source or all copy sources back to
         the origin? */
  SVN_ERR(svn_client__get_copy_source(&copyfrom_path, &copyfrom_rev,
                                      path_or_url, peg_revision, ra_session,
                                      ctx, session_pool, session_pool));
  if (copyfrom_path)
    {
      APR_ARRAY_PUSH(list, const char *) =
        svn_path_url_add_component2(repos_root, copyfrom_path, pool);
    }

  if (mergeinfo)
    {
      for (hi = apr_hash_first(session_pool, mergeinfo);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *rel_path = apr_hash_this_key(hi);

          if (copyfrom_path == NULL || strcmp(rel_path, copyfrom_path) != 0)
            APR_ARRAY_PUSH(list, const char *) = \
              svn_path_url_add_component2(repos_root, rel_path + 1, pool);
        }
    }

  svn_pool_destroy(session_pool);

  *suggestions = list;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__mergeinfo_status(svn_boolean_t *mergeinfo_changes,
                             svn_wc_context_t *wc_ctx,
                             const char *local_abspath,
                             apr_pool_t *scratch_pool)
{
  apr_array_header_t *propchanges;
  int i;

  *mergeinfo_changes = FALSE;

  SVN_ERR(svn_wc_get_prop_diffs2(&propchanges, NULL, wc_ctx,
                                 local_abspath, scratch_pool, scratch_pool));

  for (i = 0; i < propchanges->nelts; i++)
    {
      svn_prop_t prop = APR_ARRAY_IDX(propchanges, i, svn_prop_t);
      if (strcmp(prop.name, SVN_PROP_MERGEINFO) == 0)
        {
          *mergeinfo_changes = TRUE;
          break;
        }
    }

  return SVN_NO_ERROR;
}
