/*
 * copy.c:  copy/move wrappers around wc 'copy' functionality.
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
#include "svn_error.h"
#include "svn_error_codes.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_opt.h"
#include "svn_time.h"
#include "svn_props.h"
#include "svn_mergeinfo.h"
#include "svn_pools.h"

#include "client.h"
#include "mergeinfo.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_ra_private.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_client_private.h"


/*
 * OUR BASIC APPROACH TO COPIES
 * ============================
 *
 * for each source/destination pair
 *   if (not exist src_path)
 *     return ERR_BAD_SRC error
 *
 *   if (exist dst_path)
 *     return ERR_OBSTRUCTION error
 *   else
 *     copy src_path into parent_of_dst_path as basename (dst_path)
 *
 *   if (this is a move)
 *     delete src_path
 */



/*** Code. ***/

/* Extend the mergeinfo for the single WC path TARGET_WCPATH, adding
   MERGEINFO to any mergeinfo pre-existing in the WC. */
static svn_error_t *
extend_wc_mergeinfo(const char *target_abspath,
                    apr_hash_t *mergeinfo,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *pool)
{
  apr_hash_t *wc_mergeinfo;

  /* Get a fresh copy of the pre-existing state of the WC's mergeinfo
     updating it. */
  SVN_ERR(svn_client__parse_mergeinfo(&wc_mergeinfo, ctx->wc_ctx,
                                      target_abspath, pool, pool));

  /* Combine the provided mergeinfo with any mergeinfo from the WC. */
  if (wc_mergeinfo && mergeinfo)
    SVN_ERR(svn_mergeinfo_merge2(wc_mergeinfo, mergeinfo, pool, pool));
  else if (! wc_mergeinfo)
    wc_mergeinfo = mergeinfo;

  return svn_error_trace(
    svn_client__record_wc_mergeinfo(target_abspath, wc_mergeinfo,
                                    FALSE, ctx, pool));
}

/* Find the longest common ancestor of paths in COPY_PAIRS.  If
   SRC_ANCESTOR is NULL, ignore source paths in this calculation.  If
   DST_ANCESTOR is NULL, ignore destination paths in this calculation.
   COMMON_ANCESTOR will be the common ancestor of both the
   SRC_ANCESTOR and DST_ANCESTOR, and will only be set if it is not
   NULL.
 */
static svn_error_t *
get_copy_pair_ancestors(const apr_array_header_t *copy_pairs,
                        const char **src_ancestor,
                        const char **dst_ancestor,
                        const char **common_ancestor,
                        apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  svn_client__copy_pair_t *first;
  const char *first_dst;
  const char *first_src;
  const char *top_dst;
  svn_boolean_t src_is_url;
  svn_boolean_t dst_is_url;
  char *top_src;
  int i;

  first = APR_ARRAY_IDX(copy_pairs, 0, svn_client__copy_pair_t *);

  /* Because all the destinations are in the same directory, we can easily
     determine their common ancestor. */
  first_dst = first->dst_abspath_or_url;
  dst_is_url = svn_path_is_url(first_dst);

  if (copy_pairs->nelts == 1)
    top_dst = apr_pstrdup(subpool, first_dst);
  else
    top_dst = dst_is_url ? svn_uri_dirname(first_dst, subpool)
                         : svn_dirent_dirname(first_dst, subpool);

  /* Sources can came from anywhere, so we have to actually do some
     work for them.  */
  first_src = first->src_abspath_or_url;
  src_is_url = svn_path_is_url(first_src);
  top_src = apr_pstrdup(subpool, first_src);
  for (i = 1; i < copy_pairs->nelts; i++)
    {
      /* We don't need to clear the subpool here for several reasons:
         1)  If we do, we can't use it to allocate the initial versions of
             top_src and top_dst (above).
         2)  We don't return any errors in the following loop, so we
             are guanteed to destroy the subpool at the end of this function.
         3)  The number of iterations is likely to be few, and the loop will
             be through quickly, so memory leakage will not be significant,
             in time or space.
      */
      const svn_client__copy_pair_t *pair =
        APR_ARRAY_IDX(copy_pairs, i, svn_client__copy_pair_t *);

      top_src = src_is_url
        ? svn_uri_get_longest_ancestor(top_src, pair->src_abspath_or_url,
                                       subpool)
        : svn_dirent_get_longest_ancestor(top_src, pair->src_abspath_or_url,
                                          subpool);
    }

  if (src_ancestor)
    *src_ancestor = apr_pstrdup(pool, top_src);

  if (dst_ancestor)
    *dst_ancestor = apr_pstrdup(pool, top_dst);

  if (common_ancestor)
    *common_ancestor =
               src_is_url
                    ? svn_uri_get_longest_ancestor(top_src, top_dst, pool)
                    : svn_dirent_get_longest_ancestor(top_src, top_dst, pool);

  svn_pool_destroy(subpool);

  return SVN_NO_ERROR;
}

/* Quote a string if it would be handled as multiple or different tokens
   during externals parsing */
static const char *
maybe_quote(const char *value,
            apr_pool_t *result_pool)
{
  apr_status_t status;
  char **argv;

  status = apr_tokenize_to_argv(value, &argv, result_pool);

  if (!status && argv[0] && !argv[1] && strcmp(argv[0], value) == 0)
    return apr_pstrdup(result_pool, value);

  {
    svn_stringbuf_t *sb = svn_stringbuf_create_empty(result_pool);
    const char *c;

    svn_stringbuf_appendbyte(sb, '\"');

    for (c = value; *c; c++)
      {
        if (*c == '\\' || *c == '\"' || *c == '\'')
          svn_stringbuf_appendbyte(sb, '\\');

        svn_stringbuf_appendbyte(sb, *c);
      }

    svn_stringbuf_appendbyte(sb, '\"');

#ifdef SVN_DEBUG
    status = apr_tokenize_to_argv(sb->data, &argv, result_pool);

    SVN_ERR_ASSERT_NO_RETURN(!status && argv[0] && !argv[1]
                             && !strcmp(argv[0], value));
#endif

    return sb->data;
  }
}

/* In *NEW_EXTERNALS_DESCRIPTION, return a new external description for
 * use as a line in an svn:externals property, based on the external item
 * ITEM and the additional parser information in INFO. Pin the external
 * to EXTERNAL_PEGREV. Use POOL for all allocations. */
static svn_error_t *
make_external_description(const char **new_external_description,
                          const char *local_abspath_or_url,
                          svn_wc_external_item2_t *item,
                          svn_wc__externals_parser_info_t *info,
                          svn_opt_revision_t external_pegrev,
                          apr_pool_t *pool)
{
  const char *rev_str;
  const char *peg_rev_str;

  switch (info->format)
    {
      case svn_wc__external_description_format_1:
        if (external_pegrev.kind == svn_opt_revision_unspecified)
          {
            /* If info->rev_str is NULL, this yields an empty string. */
            rev_str = apr_pstrcat(pool, info->rev_str, " ", SVN_VA_NULL);
          }
        else if (info->rev_str && item->revision.kind != svn_opt_revision_head)
          rev_str = apr_psprintf(pool, "%s ", info->rev_str);
        else
          {
            /* ### can't handle svn_opt_revision_date without info->rev_str */
            SVN_ERR_ASSERT(external_pegrev.kind == svn_opt_revision_number);
            rev_str = apr_psprintf(pool, "-r%ld ",
                                   external_pegrev.value.number);
          }

        *new_external_description =
          apr_psprintf(pool, "%s %s%s\n", maybe_quote(item->target_dir, pool),
                                          rev_str,
                                          maybe_quote(item->url, pool));
        break;

      case svn_wc__external_description_format_2:
        if (external_pegrev.kind == svn_opt_revision_unspecified)
          {
            /* If info->rev_str is NULL, this yields an empty string. */
            rev_str = apr_pstrcat(pool, info->rev_str, " ", SVN_VA_NULL);
          }
        else if (info->rev_str && item->revision.kind != svn_opt_revision_head)
          rev_str = apr_psprintf(pool, "%s ", info->rev_str);
        else
          rev_str = "";

        if (external_pegrev.kind == svn_opt_revision_unspecified)
          peg_rev_str = info->peg_rev_str ? info->peg_rev_str : "";
        else if (info->peg_rev_str &&
                 item->peg_revision.kind != svn_opt_revision_head)
          peg_rev_str = info->peg_rev_str;
        else
          {
            /* ### can't handle svn_opt_revision_date without info->rev_str */
            SVN_ERR_ASSERT(external_pegrev.kind == svn_opt_revision_number);
            peg_rev_str = apr_psprintf(pool, "@%ld",
                                       external_pegrev.value.number);
          }

        *new_external_description =
          apr_psprintf(pool, "%s%s %s\n", rev_str,
                       maybe_quote(apr_psprintf(pool, "%s%s", item->url,
                                                peg_rev_str),
                                   pool),
                       maybe_quote(item->target_dir, pool));
        break;

      default:
        return svn_error_createf(
                 SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION, NULL,
                 _("%s property defined at '%s' is using an unsupported "
                   "syntax"), SVN_PROP_EXTERNALS,
                 svn_dirent_local_style(local_abspath_or_url, pool));
    }

  return SVN_NO_ERROR;
}

/* Pin all externals listed in EXTERNALS_PROP_VAL to their
 * last-changed revision. Set *PINNED_EXTERNALS to a new property
 * value allocated in RESULT_POOL, or to NULL if none of the externals
 * in EXTERNALS_PROP_VAL were changed. LOCAL_ABSPATH_OR_URL is the
 * path or URL defining the svn:externals property. Use SCRATCH_POOL
 * for temporary allocations.
 */
static svn_error_t *
pin_externals_prop(svn_string_t **pinned_externals,
                   svn_string_t *externals_prop_val,
                   const apr_hash_t *externals_to_pin,
                   const char *repos_root_url,
                   const char *local_abspath_or_url,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *buf;
  apr_array_header_t *external_items;
  apr_array_header_t *parser_infos;
  apr_array_header_t *items_to_pin;
  int pinned_items;
  int i;
  apr_pool_t *iterpool;

  SVN_ERR(svn_wc__parse_externals_description(&external_items,
                                              &parser_infos,
                                              local_abspath_or_url,
                                              externals_prop_val->data,
                                              FALSE /* canonicalize_url */,
                                              scratch_pool));

  if (externals_to_pin)
    {
      items_to_pin = svn_hash_gets((apr_hash_t *)externals_to_pin,
                                   local_abspath_or_url);
      if (!items_to_pin)
        {
          /* No pinning at all for this path. */
          *pinned_externals = NULL;
          return SVN_NO_ERROR;
        }
    }
  else
    items_to_pin = NULL;

  buf = svn_stringbuf_create_empty(scratch_pool);
  iterpool = svn_pool_create(scratch_pool);
  pinned_items = 0;
  for (i = 0; i < external_items->nelts; i++)
    {
      svn_wc_external_item2_t *item;
      svn_wc__externals_parser_info_t *info;
      svn_opt_revision_t external_pegrev;
      const char *pinned_desc;

      svn_pool_clear(iterpool);

      item = APR_ARRAY_IDX(external_items, i, svn_wc_external_item2_t *);
      info = APR_ARRAY_IDX(parser_infos, i, svn_wc__externals_parser_info_t *);

      if (items_to_pin)
        {
          int j;
          svn_wc_external_item2_t *item_to_pin = NULL;

          for (j = 0; j < items_to_pin->nelts; j++)
            {
              svn_wc_external_item2_t *const current =
                APR_ARRAY_IDX(items_to_pin, j, svn_wc_external_item2_t *);


              if (current
                  && 0 == strcmp(item->url, current->url)
                  && 0 == strcmp(item->target_dir, current->target_dir))
                {
                  item_to_pin = current;
                  break;
                }
            }

          /* If this item is not in our list of external items to pin then
           * simply keep the external at its original value. */
          if (!item_to_pin)
            {
              const char *desc;

              external_pegrev.kind = svn_opt_revision_unspecified;
              SVN_ERR(make_external_description(&desc, local_abspath_or_url,
                                                item, info, external_pegrev,
                                                iterpool));
              svn_stringbuf_appendcstr(buf, desc);
              continue;
            }
        }

      if (item->peg_revision.kind == svn_opt_revision_date)
        {
          /* Already pinned ... copy the peg date. */
          external_pegrev.kind = svn_opt_revision_date;
          external_pegrev.value.date = item->peg_revision.value.date;
        }
      else if (item->peg_revision.kind == svn_opt_revision_number)
        {
          /* Already pinned ... copy the peg revision number. */
          external_pegrev.kind = svn_opt_revision_number;
          external_pegrev.value.number = item->peg_revision.value.number;
        }
      else
        {
          SVN_ERR_ASSERT(
            item->peg_revision.kind == svn_opt_revision_head ||
            item->peg_revision.kind == svn_opt_revision_unspecified);

          /* We're actually going to change the peg revision. */
          ++pinned_items;

          if (svn_path_is_url(local_abspath_or_url))
            {
              const char *resolved_url;
              svn_ra_session_t *external_ra_session;
              svn_revnum_t latest_revnum;

              SVN_ERR(svn_wc__resolve_relative_external_url(
                        &resolved_url, item, repos_root_url,
                        local_abspath_or_url, iterpool, iterpool));
              SVN_ERR(svn_client__open_ra_session_internal(&external_ra_session,
                                                           NULL, resolved_url,
                                                           NULL, NULL, FALSE,
                                                           FALSE, ctx,
                                                           iterpool,
                                                           iterpool));
              SVN_ERR(svn_ra_get_latest_revnum(external_ra_session,
                                               &latest_revnum,
                                               iterpool));

              external_pegrev.kind = svn_opt_revision_number;
              external_pegrev.value.number = latest_revnum;
            }
          else
            {
              const char *external_abspath;
              svn_node_kind_t external_kind;
              svn_revnum_t external_checked_out_rev;

              external_abspath = svn_dirent_join(local_abspath_or_url,
                                                 item->target_dir,
                                                 iterpool);
              SVN_ERR(svn_wc__read_external_info(&external_kind, NULL, NULL,
                                                 NULL, NULL, ctx->wc_ctx,
                                                 local_abspath_or_url,
                                                 external_abspath, TRUE,
                                                 iterpool,
                                                 iterpool));
              if (external_kind == svn_node_none)
                return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                                         NULL,
                                         _("Cannot pin external '%s' defined "
                                           "in %s at '%s' because it is not "
                                           "checked out in the working copy "
                                           "at '%s'"),
                                           item->url, SVN_PROP_EXTERNALS,
                                           svn_dirent_local_style(
                                             local_abspath_or_url, iterpool),
                                           svn_dirent_local_style(
                                             external_abspath, iterpool));
              else if (external_kind == svn_node_dir)
                {
                  svn_boolean_t is_switched;
                  svn_boolean_t is_modified;
                  svn_revnum_t min_rev;
                  svn_revnum_t max_rev;

                  /* Perform some sanity checks on the checked-out external. */

                  SVN_ERR(svn_wc__has_switched_subtrees(&is_switched,
                                                        ctx->wc_ctx,
                                                        external_abspath, NULL,
                                                        iterpool));
                  if (is_switched)
                    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                                             NULL,
                                             _("Cannot pin external '%s' defined "
                                               "in %s at '%s' because '%s' has "
                                               "switched subtrees (switches "
                                               "cannot be represented in %s)"),
                                             item->url, SVN_PROP_EXTERNALS,
                                             svn_dirent_local_style(
                                               local_abspath_or_url, iterpool),
                                             svn_dirent_local_style(
                                               external_abspath, iterpool),
                                             SVN_PROP_EXTERNALS);

                  SVN_ERR(svn_wc__has_local_mods(&is_modified, ctx->wc_ctx,
                                                 external_abspath, TRUE,
                                                 ctx->cancel_func,
                                                 ctx->cancel_baton,
                                                 iterpool));
                  if (is_modified)
                    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                                             NULL,
                                             _("Cannot pin external '%s' defined "
                                               "in %s at '%s' because '%s' has "
                                               "local modifications (local "
                                               "modifications cannot be "
                                               "represented in %s)"),
                                             item->url, SVN_PROP_EXTERNALS,
                                             svn_dirent_local_style(
                                               local_abspath_or_url, iterpool),
                                             svn_dirent_local_style(
                                               external_abspath, iterpool),
                                             SVN_PROP_EXTERNALS);

                  SVN_ERR(svn_wc__min_max_revisions(&min_rev, &max_rev, ctx->wc_ctx,
                                                    external_abspath, FALSE,
                                                    iterpool));
                  if (min_rev != max_rev)
                    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                                             NULL,
                                             _("Cannot pin external '%s' defined "
                                               "in %s at '%s' because '%s' is a "
                                               "mixed-revision working copy "
                                               "(mixed-revisions cannot be "
                                               "represented in %s)"),
                                             item->url, SVN_PROP_EXTERNALS,
                                             svn_dirent_local_style(
                                               local_abspath_or_url, iterpool),
                                             svn_dirent_local_style(
                                               external_abspath, iterpool),
                                             SVN_PROP_EXTERNALS);
                  external_checked_out_rev = min_rev;
                }
              else
                {
                  SVN_ERR_ASSERT(external_kind == svn_node_file);
                  SVN_ERR(svn_wc__node_get_repos_info(&external_checked_out_rev,
                                                      NULL, NULL, NULL,
                                                      ctx->wc_ctx, external_abspath,
                                                      iterpool, iterpool));
                }

              external_pegrev.kind = svn_opt_revision_number;
              external_pegrev.value.number = external_checked_out_rev;
            }
        }

      SVN_ERR_ASSERT(external_pegrev.kind == svn_opt_revision_date ||
                     external_pegrev.kind == svn_opt_revision_number);

      SVN_ERR(make_external_description(&pinned_desc, local_abspath_or_url,
                                        item, info, external_pegrev, iterpool));

      svn_stringbuf_appendcstr(buf, pinned_desc);
    }
  svn_pool_destroy(iterpool);

  if (pinned_items > 0)
    *pinned_externals = svn_string_create_from_buf(buf, result_pool);
  else
    *pinned_externals = NULL;

  return SVN_NO_ERROR;
}

/* Return, in *PINNED_EXTERNALS, a new hash mapping URLs or local abspaths
 * to svn:externals property values (as const char *), where some or all
 * external references have been pinned.
 * If EXTERNALS_TO_PIN is NULL, pin all externals, else pin the externals
 * mentioned in EXTERNALS_TO_PIN.
 * The pinning operation takes place as part of the copy operation for
 * the source/destination pair PAIR. Use RA_SESSION and REPOS_ROOT_URL
 * to contact the repository containing the externals definition, if neccesary.
 * Use CX to fopen additional RA sessions to external repositories, if
 * neccessary. Allocate *NEW_EXTERNALS in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
resolve_pinned_externals(apr_hash_t **pinned_externals,
                         const apr_hash_t *externals_to_pin,
                         svn_client__copy_pair_t *pair,
                         svn_ra_session_t *ra_session,
                         const char *repos_root_url,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  const char *old_url = NULL;
  apr_hash_t *externals_props;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  *pinned_externals = apr_hash_make(result_pool);

  if (svn_path_is_url(pair->src_abspath_or_url))
    {
      SVN_ERR(svn_client__ensure_ra_session_url(&old_url, ra_session,
                                                pair->src_abspath_or_url,
                                                scratch_pool));
      externals_props = apr_hash_make(scratch_pool);
      SVN_ERR(svn_client__remote_propget(externals_props, NULL,
                                         SVN_PROP_EXTERNALS,
                                         pair->src_abspath_or_url, "",
                                         svn_node_dir,
                                         pair->src_revnum,
                                         ra_session,
                                         svn_depth_infinity,
                                         scratch_pool,
                                         scratch_pool));
    }
  else
    {
      SVN_ERR(svn_wc__externals_gather_definitions(&externals_props, NULL,
                                                   ctx->wc_ctx,
                                                   pair->src_abspath_or_url,
                                                   svn_depth_infinity,
                                                   scratch_pool, scratch_pool));

      /* ### gather_definitions returns propvals as const char * */
      for (hi = apr_hash_first(scratch_pool, externals_props);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *local_abspath_or_url = apr_hash_this_key(hi);
          const char *propval = apr_hash_this_val(hi);
          svn_string_t *new_propval = svn_string_create(propval, scratch_pool);

          svn_hash_sets(externals_props, local_abspath_or_url, new_propval);
        }
    }

  if (apr_hash_count(externals_props) == 0)
    {
      if (old_url)
        SVN_ERR(svn_ra_reparent(ra_session, old_url, scratch_pool));
      return SVN_NO_ERROR;
    }

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, externals_props);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *local_abspath_or_url = apr_hash_this_key(hi);
      svn_string_t *externals_propval = apr_hash_this_val(hi);
      const char *relpath;
      svn_string_t *new_propval;

      svn_pool_clear(iterpool);

      SVN_ERR(pin_externals_prop(&new_propval, externals_propval,
                                 externals_to_pin,
                                 repos_root_url, local_abspath_or_url, ctx,
                                 result_pool, iterpool));
      if (new_propval)
        {
          if (svn_path_is_url(pair->src_abspath_or_url))
            relpath = svn_uri_skip_ancestor(pair->src_abspath_or_url,
                                            local_abspath_or_url,
                                            result_pool);
          else
            relpath = svn_dirent_skip_ancestor(pair->src_abspath_or_url,
                                               local_abspath_or_url);
          SVN_ERR_ASSERT(relpath);

          svn_hash_sets(*pinned_externals, relpath, new_propval);
        }
    }
  svn_pool_destroy(iterpool);

  if (old_url)
    SVN_ERR(svn_ra_reparent(ra_session, old_url, scratch_pool));

  return SVN_NO_ERROR;
}



/* The guts of do_wc_to_wc_copies */
static svn_error_t *
do_wc_to_wc_copies_with_write_lock(svn_boolean_t *timestamp_sleep,
                                   const apr_array_header_t *copy_pairs,
                                   const char *dst_parent,
                                   svn_boolean_t metadata_only,
                                   svn_boolean_t pin_externals,
                                   const apr_hash_t *externals_to_pin,
                                   svn_client_ctx_t *ctx,
                                   apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_error_t *err = SVN_NO_ERROR;

  for (i = 0; i < copy_pairs->nelts; i++)
    {
      const char *dst_abspath;
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      apr_hash_t *pinned_externals = NULL;

      svn_pool_clear(iterpool);

      /* Check for cancellation */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      if (pin_externals)
        {
          const char *repos_root_url;

          SVN_ERR(svn_wc__node_get_origin(NULL, NULL, NULL, &repos_root_url,
                                          NULL, NULL, NULL, ctx->wc_ctx,
                                          pair->src_abspath_or_url, FALSE,
                                          scratch_pool, iterpool));
          SVN_ERR(resolve_pinned_externals(&pinned_externals,
                                           externals_to_pin, pair, NULL,
                                           repos_root_url, ctx,
                                           iterpool, iterpool));
        }

      /* Perform the copy */
      dst_abspath = svn_dirent_join(pair->dst_parent_abspath, pair->base_name,
                                    iterpool);
      *timestamp_sleep = TRUE;
      err = svn_wc_copy3(ctx->wc_ctx, pair->src_abspath_or_url, dst_abspath,
                         metadata_only,
                         ctx->cancel_func, ctx->cancel_baton,
                         ctx->notify_func2, ctx->notify_baton2, iterpool);
      if (err)
        break;

      if (pinned_externals)
        {
          apr_hash_index_t *hi;

          for (hi = apr_hash_first(iterpool, pinned_externals);
               hi;
               hi = apr_hash_next(hi))
            {
              const char *dst_relpath = apr_hash_this_key(hi);
              svn_string_t *externals_propval = apr_hash_this_val(hi);
              const char *local_abspath;

              local_abspath = svn_dirent_join(pair->dst_abspath_or_url,
                                              dst_relpath, iterpool);
              /* ### use a work queue? */
              SVN_ERR(svn_wc_prop_set4(ctx->wc_ctx, local_abspath,
                                       SVN_PROP_EXTERNALS, externals_propval,
                                       svn_depth_empty, TRUE /* skip_checks */,
                                       NULL  /* changelist_filter */,
                                       ctx->cancel_func, ctx->cancel_baton,
                                       NULL, NULL, /* no extra notification */
                                       iterpool));
            }
        }
    }
  svn_pool_destroy(iterpool);

  SVN_ERR(err);
  return SVN_NO_ERROR;
}

/* Copy each COPY_PAIR->SRC into COPY_PAIR->DST.  Use POOL for temporary
   allocations. */
static svn_error_t *
do_wc_to_wc_copies(svn_boolean_t *timestamp_sleep,
                   const apr_array_header_t *copy_pairs,
                   svn_boolean_t metadata_only,
                   svn_boolean_t pin_externals,
                   const apr_hash_t *externals_to_pin,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *pool)
{
  const char *dst_parent, *dst_parent_abspath;

  SVN_ERR(get_copy_pair_ancestors(copy_pairs, NULL, &dst_parent, NULL, pool));
  if (copy_pairs->nelts == 1)
    dst_parent = svn_dirent_dirname(dst_parent, pool);

  SVN_ERR(svn_dirent_get_absolute(&dst_parent_abspath, dst_parent, pool));

  SVN_WC__CALL_WITH_WRITE_LOCK(
    do_wc_to_wc_copies_with_write_lock(timestamp_sleep, copy_pairs, dst_parent,
                                       metadata_only, pin_externals,
                                       externals_to_pin, ctx, pool),
    ctx->wc_ctx, dst_parent_abspath, FALSE, pool);

  return SVN_NO_ERROR;
}

/* The locked bit of do_wc_to_wc_moves. */
static svn_error_t *
do_wc_to_wc_moves_with_locks2(svn_client__copy_pair_t *pair,
                              const char *dst_parent_abspath,
                              svn_boolean_t lock_src,
                              svn_boolean_t lock_dst,
                              svn_boolean_t allow_mixed_revisions,
                              svn_boolean_t metadata_only,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *scratch_pool)
{
  const char *dst_abspath;

  dst_abspath = svn_dirent_join(dst_parent_abspath, pair->base_name,
                                scratch_pool);

  SVN_ERR(svn_wc__move2(ctx->wc_ctx, pair->src_abspath_or_url,
                        dst_abspath, metadata_only,
                        allow_mixed_revisions,
                        ctx->cancel_func, ctx->cancel_baton,
                        ctx->notify_func2, ctx->notify_baton2,
                        scratch_pool));

  return SVN_NO_ERROR;
}

/* Wrapper to add an optional second lock */
static svn_error_t *
do_wc_to_wc_moves_with_locks1(svn_client__copy_pair_t *pair,
                              const char *dst_parent_abspath,
                              svn_boolean_t lock_src,
                              svn_boolean_t lock_dst,
                              svn_boolean_t allow_mixed_revisions,
                              svn_boolean_t metadata_only,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *scratch_pool)
{
  if (lock_dst)
    SVN_WC__CALL_WITH_WRITE_LOCK(
      do_wc_to_wc_moves_with_locks2(pair, dst_parent_abspath, lock_src,
                                    lock_dst, allow_mixed_revisions,
                                    metadata_only,
                                    ctx, scratch_pool),
      ctx->wc_ctx, dst_parent_abspath, FALSE, scratch_pool);
  else
    SVN_ERR(do_wc_to_wc_moves_with_locks2(pair, dst_parent_abspath, lock_src,
                                          lock_dst, allow_mixed_revisions,
                                          metadata_only,
                                          ctx, scratch_pool));

  return SVN_NO_ERROR;
}

/* Move each COPY_PAIR->SRC into COPY_PAIR->DST, deleting COPY_PAIR->SRC
   afterwards.  Use POOL for temporary allocations. */
static svn_error_t *
do_wc_to_wc_moves(svn_boolean_t *timestamp_sleep,
                  const apr_array_header_t *copy_pairs,
                  const char *dst_path,
                  svn_boolean_t allow_mixed_revisions,
                  svn_boolean_t metadata_only,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_error_t *err = SVN_NO_ERROR;

  for (i = 0; i < copy_pairs->nelts; i++)
    {
      const char *src_parent_abspath;
      svn_boolean_t lock_src, lock_dst;
      const char *src_wcroot_abspath;
      const char *dst_wcroot_abspath;

      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_pool_clear(iterpool);

      /* Check for cancellation */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      src_parent_abspath = svn_dirent_dirname(pair->src_abspath_or_url,
                                              iterpool);

      SVN_ERR(svn_wc__get_wcroot(&src_wcroot_abspath,
                                 ctx->wc_ctx, src_parent_abspath,
                                 iterpool, iterpool));
      SVN_ERR(svn_wc__get_wcroot(&dst_wcroot_abspath,
                                 ctx->wc_ctx, pair->dst_parent_abspath,
                                 iterpool, iterpool));

      /* We now need to lock the right combination of batons.
         Four cases:
           1) src_parent == dst_parent
           2) src_parent is parent of dst_parent
           3) dst_parent is parent of src_parent
           4) src_parent and dst_parent are disjoint
         We can handle 1) as either 2) or 3) */
      if (strcmp(src_parent_abspath, pair->dst_parent_abspath) == 0
          || (svn_dirent_is_child(src_parent_abspath, pair->dst_parent_abspath,
                                  NULL)
              && !svn_dirent_is_child(src_parent_abspath, dst_wcroot_abspath,
                                      NULL)))
        {
          lock_src = TRUE;
          lock_dst = FALSE;
        }
      else if (svn_dirent_is_child(pair->dst_parent_abspath,
                                   src_parent_abspath, NULL)
               && !svn_dirent_is_child(pair->dst_parent_abspath,
                                       src_wcroot_abspath, NULL))
        {
          lock_src = FALSE;
          lock_dst = TRUE;
        }
      else
        {
          lock_src = TRUE;
          lock_dst = TRUE;
        }

      *timestamp_sleep = TRUE;

      /* Perform the copy and then the delete. */
      if (lock_src)
        SVN_WC__CALL_WITH_WRITE_LOCK(
          do_wc_to_wc_moves_with_locks1(pair, pair->dst_parent_abspath,
                                        lock_src, lock_dst,
                                        allow_mixed_revisions,
                                        metadata_only,
                                        ctx, iterpool),
          ctx->wc_ctx, src_parent_abspath,
          FALSE, iterpool);
      else
        SVN_ERR(do_wc_to_wc_moves_with_locks1(pair, pair->dst_parent_abspath,
                                              lock_src, lock_dst,
                                              allow_mixed_revisions,
                                              metadata_only,
                                              ctx, iterpool));

    }
  svn_pool_destroy(iterpool);

  return svn_error_trace(err);
}

/* Verify that the destinations stored in COPY_PAIRS are valid working copy
   destinations and set pair->dst_parent_abspath and pair->base_name for each
   item to the resulting location if they do */
static svn_error_t *
verify_wc_dsts(const apr_array_header_t *copy_pairs,
               svn_boolean_t make_parents,
               svn_boolean_t is_move,
               svn_boolean_t metadata_only,
               svn_client_ctx_t *ctx,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Check that DST does not exist, but its parent does */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_node_kind_t dst_kind, dst_parent_kind;

      svn_pool_clear(iterpool);

      /* If DST_PATH does not exist, then its basename will become a new
         file or dir added to its parent (possibly an implicit '.').
         Else, just error out. */
      SVN_ERR(svn_wc_read_kind2(&dst_kind, ctx->wc_ctx,
                                pair->dst_abspath_or_url,
                                FALSE /* show_deleted */,
                                TRUE /* show_hidden */,
                                iterpool));
      if (dst_kind != svn_node_none)
        {
          svn_boolean_t is_excluded;
          svn_boolean_t is_server_excluded;

          SVN_ERR(svn_wc__node_is_not_present(NULL, &is_excluded,
                                              &is_server_excluded, ctx->wc_ctx,
                                              pair->dst_abspath_or_url, FALSE,
                                              iterpool));

          if (is_excluded || is_server_excluded)
            {
              return svn_error_createf(
                  SVN_ERR_WC_OBSTRUCTED_UPDATE,
                  NULL, _("Path '%s' exists, but is excluded"),
                  svn_dirent_local_style(pair->dst_abspath_or_url, iterpool));
            }
          else
            return svn_error_createf(
                            SVN_ERR_ENTRY_EXISTS, NULL,
                            _("Path '%s' already exists"),
                            svn_dirent_local_style(pair->dst_abspath_or_url,
                                                   scratch_pool));
        }

      /* Check that there is no unversioned obstruction */
      if (metadata_only)
        dst_kind = svn_node_none;
      else
        SVN_ERR(svn_io_check_path(pair->dst_abspath_or_url, &dst_kind,
                                  iterpool));

      if (dst_kind != svn_node_none)
        {
          if (is_move
              && copy_pairs->nelts == 1
              && strcmp(svn_dirent_dirname(pair->src_abspath_or_url, iterpool),
                        svn_dirent_dirname(pair->dst_abspath_or_url,
                                           iterpool)) == 0)
            {
              const char *dst;
              char *dst_apr;
              apr_status_t apr_err;
              /* We have a rename inside a directory, which might collide
                 just because the case insensivity of the filesystem makes
                 the source match the destination. */

              SVN_ERR(svn_path_cstring_from_utf8(&dst,
                                                 pair->dst_abspath_or_url,
                                                 scratch_pool));

              apr_err = apr_filepath_merge(&dst_apr, NULL, dst,
                                           APR_FILEPATH_TRUENAME, iterpool);

              if (!apr_err)
                {
                  /* And now bring it back to our canonical format */
                  SVN_ERR(svn_path_cstring_to_utf8(&dst, dst_apr, iterpool));
                  dst = svn_dirent_canonicalize(dst, iterpool);
                }
              /* else: Don't report this error; just report the normal error */

              if (!apr_err && strcmp(dst, pair->src_abspath_or_url) == 0)
                {
                  /* Ok, we have a single case only rename. Get out of here */
                  svn_dirent_split(&pair->dst_parent_abspath, &pair->base_name,
                                   pair->dst_abspath_or_url, result_pool);

                  svn_pool_destroy(iterpool);
                  return SVN_NO_ERROR;
                }
            }

          return svn_error_createf(
                            SVN_ERR_ENTRY_EXISTS, NULL,
                            _("Path '%s' already exists as unversioned node"),
                            svn_dirent_local_style(pair->dst_abspath_or_url,
                                                   scratch_pool));
        }

      svn_dirent_split(&pair->dst_parent_abspath, &pair->base_name,
                       pair->dst_abspath_or_url, result_pool);

      /* Make sure the destination parent is a directory and produce a clear
         error message if it is not. */
      SVN_ERR(svn_wc_read_kind2(&dst_parent_kind,
                                ctx->wc_ctx, pair->dst_parent_abspath,
                                FALSE, TRUE,
                                iterpool));
      if (dst_parent_kind == svn_node_none)
        {
          if (make_parents)
            SVN_ERR(svn_client__make_local_parents(pair->dst_parent_abspath,
                                                   TRUE, ctx, iterpool));
          else
            {
              SVN_ERR(svn_io_check_path(pair->dst_parent_abspath,
                                        &dst_parent_kind, scratch_pool));
              return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                       (dst_parent_kind == svn_node_dir)
                                         ? _("Directory '%s' is not under "
                                             "version control")
                                         : _("Path '%s' is not a directory"),
                                       svn_dirent_local_style(
                                         pair->dst_parent_abspath,
                                         scratch_pool));
            }
        }
      else if (dst_parent_kind != svn_node_dir)
        {
          return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                   _("Path '%s' is not a directory"),
                                   svn_dirent_local_style(
                                     pair->dst_parent_abspath, scratch_pool));
        }

      SVN_ERR(svn_io_check_path(pair->dst_parent_abspath,
                                &dst_parent_kind, scratch_pool));

      if (dst_parent_kind != svn_node_dir)
        return svn_error_createf(SVN_ERR_WC_MISSING, NULL,
                                 _("Path '%s' is not a directory"),
                                 svn_dirent_local_style(
                                     pair->dst_parent_abspath, scratch_pool));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
verify_wc_srcs_and_dsts(const apr_array_header_t *copy_pairs,
                        svn_boolean_t make_parents,
                        svn_boolean_t is_move,
                        svn_boolean_t metadata_only,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Check that all of our SRCs exist. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_boolean_t deleted_ok;
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_pool_clear(iterpool);

      deleted_ok = (pair->src_peg_revision.kind == svn_opt_revision_base
                    || pair->src_op_revision.kind == svn_opt_revision_base);

      /* Verify that SRC_PATH exists. */
      SVN_ERR(svn_wc_read_kind2(&pair->src_kind, ctx->wc_ctx,
                               pair->src_abspath_or_url,
                               deleted_ok, FALSE, iterpool));
      if (pair->src_kind == svn_node_none)
        return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("Path '%s' does not exist"),
                                 svn_dirent_local_style(
                                        pair->src_abspath_or_url,
                                        scratch_pool));
    }

  SVN_ERR(verify_wc_dsts(copy_pairs, make_parents, is_move, metadata_only, ctx,
                         result_pool, iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Path-specific state used as part of path_driver_cb_baton. */
typedef struct path_driver_info_t
{
  const char *src_url;
  const char *src_path;
  const char *dst_path;
  svn_node_kind_t src_kind;
  svn_revnum_t src_revnum;
  svn_boolean_t resurrection;
  svn_boolean_t dir_add;
  svn_string_t *mergeinfo;  /* the new mergeinfo for the target */
  svn_string_t *externals; /* new externals definitions for the target */
  svn_boolean_t only_pin_externals;
} path_driver_info_t;


/* The baton used with the path_driver_cb_func() callback for a copy
   or move operation. */
struct path_driver_cb_baton
{
  /* The editor (and its state) used to perform the operation. */
  const svn_delta_editor_t *editor;
  void *edit_baton;

  /* A hash of path -> path_driver_info_t *'s. */
  apr_hash_t *action_hash;

  /* Whether the operation is a move or copy. */
  svn_boolean_t is_move;
};

static svn_error_t *
path_driver_cb_func(void **dir_baton,
                    void *parent_baton,
                    void *callback_baton,
                    const char *path,
                    apr_pool_t *pool)
{
  struct path_driver_cb_baton *cb_baton = callback_baton;
  svn_boolean_t do_delete = FALSE, do_add = FALSE;
  path_driver_info_t *path_info = svn_hash_gets(cb_baton->action_hash, path);

  /* Initialize return value. */
  *dir_baton = NULL;

  /* This function should never get an empty PATH.  We can neither
     create nor delete the empty PATH, so if someone is calling us
     with such, the code is just plain wrong. */
  SVN_ERR_ASSERT(! svn_path_is_empty(path));

  /* Check to see if we need to add the path as a parent directory. */
  if (path_info->dir_add)
    {
      return cb_baton->editor->add_directory(path, parent_baton, NULL,
                                             SVN_INVALID_REVNUM, pool,
                                             dir_baton);
    }

  /* If this is a resurrection, we know the source and dest paths are
     the same, and that our driver will only be calling us once.  */
  if (path_info->resurrection)
    {
      /* If this is a move, we do nothing.  Otherwise, we do the copy.  */
      if (! cb_baton->is_move)
        do_add = TRUE;
    }
  /* Not a resurrection. */
  else
    {
      /* If this is a move, we check PATH to see if it is the source
         or the destination of the move. */
      if (cb_baton->is_move)
        {
          if (strcmp(path_info->src_path, path) == 0)
            do_delete = TRUE;
          else
            do_add = TRUE;
        }
      /* Not a move?  This must just be the copy addition. */
      else
        {
          do_add = !path_info->only_pin_externals;
        }
    }

  if (do_delete)
    {
      SVN_ERR(cb_baton->editor->delete_entry(path, SVN_INVALID_REVNUM,
                                             parent_baton, pool));
    }
  if (do_add)
    {
      SVN_ERR(svn_path_check_valid(path, pool));

      if (path_info->src_kind == svn_node_file)
        {
          void *file_baton;
          SVN_ERR(cb_baton->editor->add_file(path, parent_baton,
                                             path_info->src_url,
                                             path_info->src_revnum,
                                             pool, &file_baton));
          if (path_info->mergeinfo)
            SVN_ERR(cb_baton->editor->change_file_prop(file_baton,
                                                       SVN_PROP_MERGEINFO,
                                                       path_info->mergeinfo,
                                                       pool));
          SVN_ERR(cb_baton->editor->close_file(file_baton, NULL, pool));
        }
      else
        {
          SVN_ERR(cb_baton->editor->add_directory(path, parent_baton,
                                                  path_info->src_url,
                                                  path_info->src_revnum,
                                                  pool, dir_baton));
          if (path_info->mergeinfo)
            SVN_ERR(cb_baton->editor->change_dir_prop(*dir_baton,
                                                      SVN_PROP_MERGEINFO,
                                                      path_info->mergeinfo,
                                                      pool));
        }
    }

  if (path_info->externals)
    {
      if (*dir_baton == NULL)
        SVN_ERR(cb_baton->editor->open_directory(path, parent_baton,
                                                 SVN_INVALID_REVNUM,
                                                 pool, dir_baton));

      SVN_ERR(cb_baton->editor->change_dir_prop(*dir_baton, SVN_PROP_EXTERNALS,
                                                path_info->externals, pool));
    }

  return SVN_NO_ERROR;
}


/* Starting with the path DIR relative to the RA_SESSION's session
   URL, work up through DIR's parents until an existing node is found.
   Push each nonexistent path onto the array NEW_DIRS, allocating in
   POOL.  Raise an error if the existing node is not a directory.

   ### Multiple requests for HEAD (SVN_INVALID_REVNUM) make this
   ### implementation susceptible to race conditions.  */
static svn_error_t *
find_absent_parents1(svn_ra_session_t *ra_session,
                     const char *dir,
                     apr_array_header_t *new_dirs,
                     apr_pool_t *pool)
{
  svn_node_kind_t kind;
  apr_pool_t *iterpool = svn_pool_create(pool);

  SVN_ERR(svn_ra_check_path(ra_session, dir, SVN_INVALID_REVNUM, &kind,
                            iterpool));

  while (kind == svn_node_none)
    {
      svn_pool_clear(iterpool);

      APR_ARRAY_PUSH(new_dirs, const char *) = dir;
      dir = svn_dirent_dirname(dir, pool);

      SVN_ERR(svn_ra_check_path(ra_session, dir, SVN_INVALID_REVNUM,
                                &kind, iterpool));
    }

  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                             _("Path '%s' already exists, but is not a "
                               "directory"), dir);

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Starting with the URL *TOP_DST_URL which is also the root of
   RA_SESSION, work up through its parents until an existing node is
   found. Push each nonexistent URL onto the array NEW_DIRS,
   allocating in POOL.  Raise an error if the existing node is not a
   directory.

   Set *TOP_DST_URL and the RA session's root to the existing node's URL.

   ### Multiple requests for HEAD (SVN_INVALID_REVNUM) make this
   ### implementation susceptible to race conditions.  */
static svn_error_t *
find_absent_parents2(svn_ra_session_t *ra_session,
                     const char **top_dst_url,
                     apr_array_header_t *new_dirs,
                     apr_pool_t *pool)
{
  const char *root_url = *top_dst_url;
  svn_node_kind_t kind;

  SVN_ERR(svn_ra_check_path(ra_session, "", SVN_INVALID_REVNUM, &kind,
                            pool));

  while (kind == svn_node_none)
    {
      APR_ARRAY_PUSH(new_dirs, const char *) = root_url;
      root_url = svn_uri_dirname(root_url, pool);

      SVN_ERR(svn_ra_reparent(ra_session, root_url, pool));
      SVN_ERR(svn_ra_check_path(ra_session, "", SVN_INVALID_REVNUM, &kind,
                                pool));
    }

  if (kind != svn_node_dir)
    return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                _("Path '%s' already exists, but is not a directory"),
                root_url);

  *top_dst_url = root_url;
  return SVN_NO_ERROR;
}

/* Queue property changes for pinning svn:externals properties set on
 * descendants of the path corresponding to PARENT_INFO. PINNED_EXTERNALS
 * is keyed by the relative path of each descendant which should have some
 * or all of its externals pinned, with the corresponding pinned svn:externals
 * properties as values. Property changes are queued in a new list of path
 * infos *NEW_PATH_INFOS, or in an existing item of the PATH_INFOS list if an
 * existing item is found for the descendant. Allocate results in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
queue_externals_change_path_infos(apr_array_header_t *new_path_infos,
                                  apr_array_header_t *path_infos,
                                  apr_hash_t *pinned_externals,
                                  path_driver_info_t *parent_info,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, pinned_externals);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *dst_relpath = apr_hash_this_key(hi);
      svn_string_t *externals_prop = apr_hash_this_val(hi);
      const char *src_url;
      path_driver_info_t *info;
      int i;

      svn_pool_clear(iterpool);

      src_url = svn_path_url_add_component2(parent_info->src_url,
                                            dst_relpath, iterpool);

      /* Try to find a path info the external change can be applied to. */
      info = NULL;
      for (i = 0; i < path_infos->nelts; i++)
        {
          path_driver_info_t *existing_info;

          existing_info = APR_ARRAY_IDX(path_infos, i, path_driver_info_t *);
          if (strcmp(src_url, existing_info->src_url) == 0)
            {
              info = existing_info;
              break;
            }
        }

      if (info == NULL)
        {
          /* A copied-along child needs its externals pinned.
             Create a new path info for this property change. */
          info = apr_pcalloc(result_pool, sizeof(*info));
          info->src_url = svn_path_url_add_component2(
                                parent_info->src_url, dst_relpath,
                                result_pool);
          info->src_path = NULL; /* Only needed on copied dirs */
          info->dst_path = svn_relpath_join(parent_info->dst_path,
                                            dst_relpath,
                                            result_pool);
          info->src_kind = svn_node_dir;
          info->only_pin_externals = TRUE;
          APR_ARRAY_PUSH(new_path_infos, path_driver_info_t *) = info;
        }

      info->externals = externals_prop;
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
repos_to_repos_copy(const apr_array_header_t *copy_pairs,
                    svn_boolean_t make_parents,
                    const apr_hash_t *revprop_table,
                    svn_commit_callback2_t commit_callback,
                    void *commit_baton,
                    svn_client_ctx_t *ctx,
                    svn_boolean_t is_move,
                    svn_boolean_t pin_externals,
                    const apr_hash_t *externals_to_pin,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  apr_array_header_t *paths = apr_array_make(pool, 2 * copy_pairs->nelts,
                                             sizeof(const char *));
  apr_hash_t *action_hash = apr_hash_make(pool);
  apr_array_header_t *path_infos;
  const char *top_url, *top_url_all, *top_url_dst;
  const char *message, *repos_root;
  svn_ra_session_t *ra_session = NULL;
  const svn_delta_editor_t *editor;
  void *edit_baton;
  struct path_driver_cb_baton cb_baton;
  apr_array_header_t *new_dirs = NULL;
  apr_hash_t *commit_revprops;
  apr_array_header_t *pin_externals_only_infos = NULL;
  int i;
  svn_client__copy_pair_t *first_pair =
    APR_ARRAY_IDX(copy_pairs, 0, svn_client__copy_pair_t *);

  /* Open an RA session to the first copy pair's destination.  We'll
     be verifying that every one of our copy source and destination
     URLs is or is beneath this sucker's repository root URL as a form
     of a cheap(ish) sanity check.  */
  SVN_ERR(svn_client_open_ra_session2(&ra_session,
                                      first_pair->src_abspath_or_url, NULL,
                                      ctx, pool, pool));
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root, pool));

  /* Verify that sources and destinations are all at or under
     REPOS_ROOT.  While here, create a path_info struct for each
     src/dst pair and initialize portions of it with normalized source
     location information.  */
  path_infos = apr_array_make(pool, copy_pairs->nelts,
                              sizeof(path_driver_info_t *));
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      path_driver_info_t *info = apr_pcalloc(pool, sizeof(*info));
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      apr_hash_t *mergeinfo;

      /* Are the source and destination URLs at or under REPOS_ROOT? */
      if (! (svn_uri__is_ancestor(repos_root, pair->src_abspath_or_url)
             && svn_uri__is_ancestor(repos_root, pair->dst_abspath_or_url)))
        return svn_error_create
          (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
           _("Source and destination URLs appear not to point to the "
             "same repository."));

      /* Run the history function to get the source's URL and revnum in the
         operational revision. */
      SVN_ERR(svn_ra_reparent(ra_session, pair->src_abspath_or_url, pool));
      SVN_ERR(svn_client__repos_locations(&pair->src_abspath_or_url,
                                          &pair->src_revnum,
                                          NULL, NULL,
                                          ra_session,
                                          pair->src_abspath_or_url,
                                          &pair->src_peg_revision,
                                          &pair->src_op_revision, NULL,
                                          ctx, pool));

      /* Go ahead and grab mergeinfo from the source, too. */
      SVN_ERR(svn_ra_reparent(ra_session, pair->src_abspath_or_url, pool));
      SVN_ERR(svn_client__get_repos_mergeinfo(
                &mergeinfo, ra_session,
                pair->src_abspath_or_url, pair->src_revnum,
                svn_mergeinfo_inherited, TRUE /*squelch_incapable*/, pool));
      if (mergeinfo)
        SVN_ERR(svn_mergeinfo_to_string(&info->mergeinfo, mergeinfo, pool));

      /* Plop an INFO structure onto our array thereof. */
      info->src_url = pair->src_abspath_or_url;
      info->src_revnum = pair->src_revnum;
      info->resurrection = FALSE;
      APR_ARRAY_PUSH(path_infos, path_driver_info_t *) = info;
    }

  /* If this is a move, we have to open our session to the longest
     path common to all SRC_URLS and DST_URLS in the repository so we
     can do existence checks on all paths, and so we can operate on
     all paths in the case of a move.  But if this is *not* a move,
     then opening our session at the longest path common to sources
     *and* destinations might be an optimization when the user is
     authorized to access all that stuff, but could cause the
     operation to fail altogether otherwise.  See issue #3242.  */
  SVN_ERR(get_copy_pair_ancestors(copy_pairs, NULL, &top_url_dst, &top_url_all,
                                  pool));
  top_url = is_move ? top_url_all : top_url_dst;

  /* Check each src/dst pair for resurrection, and verify that TOP_URL
     is anchored high enough to cover all the editor_t activities
     required for this operation.  */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      path_driver_info_t *info = APR_ARRAY_IDX(path_infos, i,
                                               path_driver_info_t *);

      /* Source and destination are the same?  It's a resurrection. */
      if (strcmp(pair->src_abspath_or_url, pair->dst_abspath_or_url) == 0)
        info->resurrection = TRUE;

      /* We need to add each dst_URL, and (in a move) we'll need to
         delete each src_URL.  Our selection of TOP_URL so far ensures
         that all our destination URLs (and source URLs, for moves)
         are at least as deep as TOP_URL, but we need to make sure
         that TOP_URL is an *ancestor* of all our to-be-edited paths.

         Issue #683 is demonstrates this scenario.  If you're
         resurrecting a deleted item like this: 'svn cp -rN src_URL
         dst_URL', then src_URL == dst_URL == top_url.  In this
         situation, we want to open an RA session to be at least the
         *parent* of all three. */
      if ((strcmp(top_url, pair->dst_abspath_or_url) == 0)
          && (strcmp(top_url, repos_root) != 0))
        {
          top_url = svn_uri_dirname(top_url, pool);
        }
      if (is_move
          && (strcmp(top_url, pair->src_abspath_or_url) == 0)
          && (strcmp(top_url, repos_root) != 0))
        {
          top_url = svn_uri_dirname(top_url, pool);
        }
    }

  /* Point the RA session to our current TOP_URL. */
  SVN_ERR(svn_ra_reparent(ra_session, top_url, pool));

  /* If we're allowed to create nonexistent parent directories of our
     destinations, then make a list in NEW_DIRS of the parent
     directories of the destination that don't yet exist.  */
  if (make_parents)
    {
      new_dirs = apr_array_make(pool, 0, sizeof(const char *));

      /* If this is a move, TOP_URL is at least the common ancestor of
         all the paths (sources and destinations) involved.  Assuming
         the sources exist (which is fair, because if they don't, this
         whole operation will fail anyway), TOP_URL must also exist.
         So it's the paths between TOP_URL and the destinations which
         we have to check for existence.  But here, we take advantage
         of the knowledge of our caller.  We know that if there are
         multiple copy/move operations being requested, then the
         destinations of the copies/moves will all be siblings of one
         another.  Therefore, we need only to check for the
         nonexistent paths between TOP_URL and *one* of our
         destinations to find nonexistent parents of all of them.  */
      if (is_move)
        {
          /* Imagine a situation where the user tries to copy an
             existing source directory to nonexistent directory with
             --parents options specified:

                svn copy --parents URL/src URL/dst

             where src exists and dst does not.  If the dirname of the
             destination path is equal to TOP_URL,
             do not try to add dst to the NEW_DIRS list since it
             will be added to the commit items array later in this
             function. */
          const char *dir = svn_uri_skip_ancestor(
                              top_url,
                              svn_uri_dirname(first_pair->dst_abspath_or_url,
                                              pool),
                              pool);
          if (dir && *dir)
            SVN_ERR(find_absent_parents1(ra_session, dir, new_dirs, pool));
        }
      /* If, however, this is *not* a move, TOP_URL only points to the
         common ancestor of our destination path(s), or possibly one
         level higher.  We'll need to do an existence crawl toward the
         root of the repository, starting with one of our destinations
         (see "... take advantage of the knowledge of our caller ..."
         above), and possibly adjusting TOP_URL as we go. */
      else
        {
          apr_array_header_t *new_urls =
            apr_array_make(pool, 0, sizeof(const char *));
          SVN_ERR(find_absent_parents2(ra_session, &top_url, new_urls, pool));

          /* Convert absolute URLs into relpaths relative to TOP_URL. */
          for (i = 0; i < new_urls->nelts; i++)
            {
              const char *new_url = APR_ARRAY_IDX(new_urls, i, const char *);
              const char *dir = svn_uri_skip_ancestor(top_url, new_url, pool);

              APR_ARRAY_PUSH(new_dirs, const char *) = dir;
            }
        }
    }

  /* For each src/dst pair, check to see if that SRC_URL is a child of
     the DST_URL (excepting the case where DST_URL is the repo root).
     If it is, and the parent of DST_URL is the current TOP_URL, then we
     need to reparent the session one directory higher, the parent of
     the DST_URL. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      path_driver_info_t *info = APR_ARRAY_IDX(path_infos, i,
                                               path_driver_info_t *);
      const char *relpath = svn_uri_skip_ancestor(pair->dst_abspath_or_url,
                                                  pair->src_abspath_or_url,
                                                  pool);

      if ((strcmp(pair->dst_abspath_or_url, repos_root) != 0)
          && (relpath != NULL && *relpath != '\0'))
        {
          info->resurrection = TRUE;
          top_url = svn_uri_get_longest_ancestor(
                            top_url,
                            svn_uri_dirname(pair->dst_abspath_or_url, pool),
                            pool);
          SVN_ERR(svn_ra_reparent(ra_session, top_url, pool));
        }
    }

  /* Get the portions of the SRC and DST URLs that are relative to
     TOP_URL (URI-decoding them while we're at it), verify that the
     source exists and the proposed destination does not, and toss
     what we've learned into the INFO array.  (For copies -- that is,
     non-moves -- the relative source URL NULL because it isn't a
     child of the TOP_URL at all.  That's okay, we'll deal with
     it.)  */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair =
        APR_ARRAY_IDX(copy_pairs, i, svn_client__copy_pair_t *);
      path_driver_info_t *info =
        APR_ARRAY_IDX(path_infos, i, path_driver_info_t *);
      svn_node_kind_t dst_kind;
      const char *src_rel, *dst_rel;

      src_rel = svn_uri_skip_ancestor(top_url, pair->src_abspath_or_url, pool);
      if (src_rel)
        {
          SVN_ERR(svn_ra_check_path(ra_session, src_rel, pair->src_revnum,
                                    &info->src_kind, pool));
        }
      else
        {
          const char *old_url;

          src_rel = NULL;
          SVN_ERR_ASSERT(! is_move);

          SVN_ERR(svn_client__ensure_ra_session_url(&old_url, ra_session,
                                                    pair->src_abspath_or_url,
                                                    pool));
          SVN_ERR(svn_ra_check_path(ra_session, "", pair->src_revnum,
                                    &info->src_kind, pool));
          SVN_ERR(svn_ra_reparent(ra_session, old_url, pool));
        }
      if (info->src_kind == svn_node_none)
        return svn_error_createf(SVN_ERR_FS_NOT_FOUND, NULL,
                                 _("Path '%s' does not exist in revision %ld"),
                                 pair->src_abspath_or_url, pair->src_revnum);

      /* Figure out the basename that will result from this operation,
         and ensure that we aren't trying to overwrite existing paths.  */
      dst_rel = svn_uri_skip_ancestor(top_url, pair->dst_abspath_or_url, pool);
      SVN_ERR(svn_ra_check_path(ra_session, dst_rel, SVN_INVALID_REVNUM,
                                &dst_kind, pool));
      if (dst_kind != svn_node_none)
        return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                 _("Path '%s' already exists"),
                                 pair->dst_abspath_or_url);

      /* More info for our INFO structure.  */
      info->src_path = src_rel; /* May be NULL, if outside RA session scope */
      info->dst_path = dst_rel;

      svn_hash_sets(action_hash, info->dst_path, info);
      if (is_move && (! info->resurrection))
        svn_hash_sets(action_hash, info->src_path, info);

      if (pin_externals)
        {
          apr_hash_t *pinned_externals;

          SVN_ERR(resolve_pinned_externals(&pinned_externals,
                                           externals_to_pin, pair,
                                           ra_session, repos_root,
                                           ctx, pool, pool));
          if (pin_externals_only_infos == NULL)
            {
              pin_externals_only_infos =
                apr_array_make(pool, 0, sizeof(path_driver_info_t *));
            }
          SVN_ERR(queue_externals_change_path_infos(pin_externals_only_infos,
                                                    path_infos,
                                                    pinned_externals,
                                                    info, pool, pool));
        }
    }

  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      /* Produce a list of new paths to add, and provide it to the
         mechanism used to acquire a log message. */
      svn_client_commit_item3_t *item;
      const char *tmp_file;
      apr_array_header_t *commit_items
        = apr_array_make(pool, 2 * copy_pairs->nelts, sizeof(item));

      /* Add any intermediate directories to the message */
      if (make_parents)
        {
          for (i = 0; i < new_dirs->nelts; i++)
            {
              const char *relpath = APR_ARRAY_IDX(new_dirs, i, const char *);

              item = svn_client_commit_item3_create(pool);
              item->url = svn_path_url_add_component2(top_url, relpath, pool);
              item->kind = svn_node_dir;
              item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
              APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
            }
        }

      for (i = 0; i < path_infos->nelts; i++)
        {
          path_driver_info_t *info = APR_ARRAY_IDX(path_infos, i,
                                                   path_driver_info_t *);

          item = svn_client_commit_item3_create(pool);
          item->url = svn_path_url_add_component2(top_url, info->dst_path,
                                                  pool);
          item->kind = info->src_kind;
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD
                              | SVN_CLIENT_COMMIT_ITEM_IS_COPY;
          item->copyfrom_url = info->src_url;
          item->copyfrom_rev = info->src_revnum;
          APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;

          if (is_move && (! info->resurrection))
            {
              item = svn_client_commit_item3_create(pool);
              item->url = svn_path_url_add_component2(top_url, info->src_path,
                                                      pool);
              item->kind = info->src_kind;
              item->state_flags = SVN_CLIENT_COMMIT_ITEM_DELETE;
              APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
            }
        }

      SVN_ERR(svn_client__get_log_msg(&message, &tmp_file, commit_items,
                                      ctx, pool));
      if (! message)
        return SVN_NO_ERROR;
    }
  else
    message = "";

  /* Setup our PATHS for the path-based editor drive. */
  /* First any intermediate directories. */
  if (make_parents)
    {
      for (i = 0; i < new_dirs->nelts; i++)
        {
          const char *relpath = APR_ARRAY_IDX(new_dirs, i, const char *);
          path_driver_info_t *info = apr_pcalloc(pool, sizeof(*info));

          info->dst_path = relpath;
          info->dir_add = TRUE;

          APR_ARRAY_PUSH(paths, const char *) = relpath;
          svn_hash_sets(action_hash, relpath, info);
        }
    }

  /* Then our copy destinations and move sources (if any). */
  for (i = 0; i < path_infos->nelts; i++)
    {
      path_driver_info_t *info = APR_ARRAY_IDX(path_infos, i,
                                               path_driver_info_t *);

      APR_ARRAY_PUSH(paths, const char *) = info->dst_path;
      if (is_move && (! info->resurrection))
        APR_ARRAY_PUSH(paths, const char *) = info->src_path;
    }

  /* Add any items which only need their externals pinned. */
  if (pin_externals_only_infos)
    {
      for (i = 0; i < pin_externals_only_infos->nelts; i++)
        {
          path_driver_info_t *info;

          info = APR_ARRAY_IDX(pin_externals_only_infos, i, path_driver_info_t *);
          APR_ARRAY_PUSH(paths, const char *) = info->dst_path;
          svn_hash_sets(action_hash, info->dst_path, info);
        }
    }

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           message, ctx, pool));

  /* Fetch RA commit editor. */
  SVN_ERR(svn_ra__register_editor_shim_callbacks(ra_session,
                        svn_client__get_shim_callbacks(ctx->wc_ctx,
                                                       NULL, pool)));
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    commit_revprops,
                                    commit_callback,
                                    commit_baton,
                                    NULL, TRUE, /* No lock tokens */
                                    pool));

  /* Setup the callback baton. */
  cb_baton.editor = editor;
  cb_baton.edit_baton = edit_baton;
  cb_baton.action_hash = action_hash;
  cb_baton.is_move = is_move;

  /* Call the path-based editor driver. */
  err = svn_delta_path_driver2(editor, edit_baton, paths, TRUE,
                               path_driver_cb_func, &cb_baton, pool);
  if (err)
    {
      /* At least try to abort the edit (and fs txn) before throwing err. */
      return svn_error_compose_create(
                    err,
                    editor->abort_edit(edit_baton, pool));
    }

  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify;
      notify = svn_wc_create_notify_url(top_url,
                                        svn_wc_notify_commit_finalizing,
                                        pool);
      ctx->notify_func2(ctx->notify_baton2, notify, pool);
    }

  /* Close the edit. */
  return svn_error_trace(editor->close_edit(edit_baton, pool));
}

/* Baton for check_url_kind */
struct check_url_kind_baton
{
  svn_ra_session_t *session;
  const char *repos_root_url;
  svn_boolean_t should_reparent;
};

/* Implements svn_client__check_url_kind_t for wc_to_repos_copy */
static svn_error_t *
check_url_kind(void *baton,
               svn_node_kind_t *kind,
               const char *url,
               svn_revnum_t revision,
               apr_pool_t *scratch_pool)
{
  struct check_url_kind_baton *cukb = baton;

  /* If we don't have a session or can't use the session, get one */
  if (!svn_uri__is_ancestor(cukb->repos_root_url, url))
    *kind = svn_node_none;
  else
    {
      cukb->should_reparent = TRUE;

      SVN_ERR(svn_ra_reparent(cukb->session, url, scratch_pool));

      SVN_ERR(svn_ra_check_path(cukb->session, "", revision,
                                kind, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Queue a property change on a copy of LOCAL_ABSPATH to COMMIT_URL
 * in the COMMIT_ITEMS list.
 * If the list does not already have a commit item for COMMIT_URL
 * add a new commit item for the property change.
 * Allocate results in RESULT_POOL.
 * Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
queue_prop_change_commit_items(const char *local_abspath,
                               const char *commit_url,
                               apr_array_header_t *commit_items,
                               const char *propname,
                               svn_string_t *propval,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_client_commit_item3_t *item = NULL;
  svn_prop_t *prop;
  int i;

  for (i = 0; i < commit_items->nelts; i++)
    {
      svn_client_commit_item3_t *existing_item;

      existing_item = APR_ARRAY_IDX(commit_items, i,
                                    svn_client_commit_item3_t *);
      if (strcmp(existing_item->url, commit_url) == 0)
        {
          item = existing_item;
          break;
        }
    }

  if (item == NULL)
    {
      item = svn_client_commit_item3_create(result_pool);
      item->path = local_abspath;
      item->url = commit_url;
      item->kind = svn_node_dir;
      item->state_flags = SVN_CLIENT_COMMIT_ITEM_PROP_MODS;

      item->incoming_prop_changes = apr_array_make(result_pool, 1,
                                                   sizeof(svn_prop_t *));
      APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
    }
  else
    item->state_flags |= SVN_CLIENT_COMMIT_ITEM_PROP_MODS;

  if (item->outgoing_prop_changes == NULL)
    item->outgoing_prop_changes = apr_array_make(result_pool, 1,
                                                 sizeof(svn_prop_t *));

  prop = apr_palloc(result_pool, sizeof(*prop));
  prop->name = propname;
  prop->value = propval;
  APR_ARRAY_PUSH(item->outgoing_prop_changes, svn_prop_t *) = prop;

  return SVN_NO_ERROR;
}

/* ### Copy ...
 * COMMIT_INFO_P is ...
 * COPY_PAIRS is ... such that each 'src_abspath_or_url' is a local abspath
 * and each 'dst_abspath_or_url' is a URL.
 * MAKE_PARENTS is ...
 * REVPROP_TABLE is ...
 * CTX is ... */
static svn_error_t *
wc_to_repos_copy(const apr_array_header_t *copy_pairs,
                 svn_boolean_t make_parents,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_boolean_t pin_externals,
                 const apr_hash_t *externals_to_pin,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *scratch_pool)
{
  const char *message;
  const char *top_src_path, *top_dst_url;
  struct check_url_kind_baton cukb;
  const char *top_src_abspath;
  svn_ra_session_t *ra_session;
  const svn_delta_editor_t *editor;
#ifdef ENABLE_EV2_SHIMS
  apr_hash_t *relpath_map = NULL;
#endif
  void *edit_baton;
  svn_client__committables_t *committables;
  apr_array_header_t *commit_items;
  apr_pool_t *iterpool;
  apr_array_header_t *new_dirs = NULL;
  apr_hash_t *commit_revprops;
  svn_client__copy_pair_t *first_pair;
  apr_pool_t *session_pool = svn_pool_create(scratch_pool);
  apr_array_header_t *commit_items_for_dav;
  int i;

  /* Find the common root of all the source paths */
  SVN_ERR(get_copy_pair_ancestors(copy_pairs, &top_src_path, NULL, NULL,
                                  scratch_pool));

  /* Do we need to lock the working copy?  1.6 didn't take a write
     lock, but what happens if the working copy changes during the copy
     operation? */

  iterpool = svn_pool_create(scratch_pool);

  /* Determine the longest common ancestor for the destinations, and open an RA
     session to that location. */
  /* ### But why start by getting the _parent_ of the first one? */
  /* --- That works because multiple destinations always point to the same
   *     directory. I'm rather wondering why we need to find a common
   *     destination parent here at all, instead of simply getting
   *     top_dst_url from get_copy_pair_ancestors() above?
   *     It looks like the entire block of code hanging off this comment
   *     is redundant. */
  first_pair = APR_ARRAY_IDX(copy_pairs, 0, svn_client__copy_pair_t *);
  top_dst_url = svn_uri_dirname(first_pair->dst_abspath_or_url, scratch_pool);
  for (i = 1; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      top_dst_url = svn_uri_get_longest_ancestor(top_dst_url,
                                                 pair->dst_abspath_or_url,
                                                 scratch_pool);
    }

  SVN_ERR(svn_dirent_get_absolute(&top_src_abspath, top_src_path, scratch_pool));

  commit_items_for_dav = apr_array_make(session_pool, 0,
                                        sizeof(svn_client_commit_item3_t*));

  /* Open a session to help while determining the exact targets */
  SVN_ERR(svn_client__open_ra_session_internal(&ra_session, NULL, top_dst_url,
                                               top_src_abspath,
                                               commit_items_for_dav,
                                               FALSE /* write_dav_props */,
                                               TRUE /* read_dav_props */,
                                               ctx,
                                               session_pool, session_pool));

  /* If requested, determine the nearest existing parent of the destination,
     and reparent the ra session there. */
  if (make_parents)
    {
      new_dirs = apr_array_make(scratch_pool, 0, sizeof(const char *));
      SVN_ERR(find_absent_parents2(ra_session, &top_dst_url, new_dirs,
                                   scratch_pool));
    }

  /* Figure out the basename that will result from each copy and check to make
     sure it doesn't exist already. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_node_kind_t dst_kind;
      const char *dst_rel;
      svn_client__copy_pair_t *pair =
        APR_ARRAY_IDX(copy_pairs, i, svn_client__copy_pair_t *);

      svn_pool_clear(iterpool);
      dst_rel = svn_uri_skip_ancestor(top_dst_url, pair->dst_abspath_or_url,
                                      iterpool);
      SVN_ERR(svn_ra_check_path(ra_session, dst_rel, SVN_INVALID_REVNUM,
                                &dst_kind, iterpool));
      if (dst_kind != svn_node_none)
        {
          return svn_error_createf(SVN_ERR_FS_ALREADY_EXISTS, NULL,
                                   _("Path '%s' already exists"),
                                   pair->dst_abspath_or_url);
        }
    }

  cukb.session = ra_session;
  SVN_ERR(svn_ra_get_repos_root2(ra_session, &cukb.repos_root_url, session_pool));
  cukb.should_reparent = FALSE;

  /* Crawl the working copy for commit items. */
  /* ### TODO: Pass check_url_func for issue #3314 handling */
  SVN_ERR(svn_client__get_copy_committables(&committables,
                                            copy_pairs,
                                            check_url_kind, &cukb,
                                            ctx, scratch_pool, iterpool));

  /* The committables are keyed by the repository root */
  commit_items = svn_hash_gets(committables->by_repository,
                               cukb.repos_root_url);
  SVN_ERR_ASSERT(commit_items != NULL);

  if (cukb.should_reparent)
    SVN_ERR(svn_ra_reparent(ra_session, top_dst_url, session_pool));

  /* If we are creating intermediate directories, tack them onto the list
     of committables. */
  if (make_parents)
    {
      for (i = 0; i < new_dirs->nelts; i++)
        {
          const char *url = APR_ARRAY_IDX(new_dirs, i, const char *);
          svn_client_commit_item3_t *item;

          item = svn_client_commit_item3_create(scratch_pool);
          item->url = url;
          item->kind = svn_node_dir;
          item->state_flags = SVN_CLIENT_COMMIT_ITEM_ADD;
          item->incoming_prop_changes = apr_array_make(scratch_pool, 1,
                                                       sizeof(svn_prop_t *));
          APR_ARRAY_PUSH(commit_items, svn_client_commit_item3_t *) = item;
        }
    }

  /* ### TODO: This extra loop would be unnecessary if this code lived
     ### in svn_client__get_copy_committables(), which is incidentally
     ### only used above (so should really be in this source file). */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      apr_hash_t *mergeinfo, *wc_mergeinfo;
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_client_commit_item3_t *item =
        APR_ARRAY_IDX(commit_items, i, svn_client_commit_item3_t *);
      svn_client__pathrev_t *src_origin;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_client__wc_node_get_origin(&src_origin,
                                             pair->src_abspath_or_url,
                                             ctx, iterpool, iterpool));

      /* Set the mergeinfo for the destination to the combined merge
         info known to the WC and the repository. */
      /* Repository mergeinfo (or NULL if it's locally added)... */
      if (src_origin)
        SVN_ERR(svn_client__get_repos_mergeinfo(
                  &mergeinfo, ra_session, src_origin->url, src_origin->rev,
                  svn_mergeinfo_inherited, TRUE /*sqelch_inc.*/, iterpool));
      else
        mergeinfo = NULL;
      /* ... and WC mergeinfo. */
      SVN_ERR(svn_client__parse_mergeinfo(&wc_mergeinfo, ctx->wc_ctx,
                                          pair->src_abspath_or_url,
                                          iterpool, iterpool));
      if (wc_mergeinfo && mergeinfo)
        SVN_ERR(svn_mergeinfo_merge2(mergeinfo, wc_mergeinfo, iterpool,
                                     iterpool));
      else if (! mergeinfo)
        mergeinfo = wc_mergeinfo;

      if (mergeinfo)
        {
          /* Push a mergeinfo prop representing MERGEINFO onto the
           * OUTGOING_PROP_CHANGES array. */

          svn_prop_t *mergeinfo_prop
                            = apr_palloc(scratch_pool, sizeof(*mergeinfo_prop));
          svn_string_t *prop_value;

          SVN_ERR(svn_mergeinfo_to_string(&prop_value, mergeinfo,
                                          scratch_pool));

          if (!item->outgoing_prop_changes)
            {
              item->outgoing_prop_changes = apr_array_make(scratch_pool, 1,
                                                           sizeof(svn_prop_t *));
            }

          mergeinfo_prop->name = SVN_PROP_MERGEINFO;
          mergeinfo_prop->value = prop_value;
          APR_ARRAY_PUSH(item->outgoing_prop_changes, svn_prop_t *)
            = mergeinfo_prop;
        }

      if (pin_externals)
        {
          apr_hash_t *pinned_externals;
          apr_hash_index_t *hi;

          SVN_ERR(resolve_pinned_externals(&pinned_externals,
                                           externals_to_pin, pair,
                                           ra_session, cukb.repos_root_url,
                                           ctx, scratch_pool, iterpool));
          for (hi = apr_hash_first(scratch_pool, pinned_externals);
               hi;
               hi = apr_hash_next(hi))
            {
              const char *dst_relpath = apr_hash_this_key(hi);
              svn_string_t *externals_propval = apr_hash_this_val(hi);
              const char *dst_url;
              const char *commit_url;
              const char *src_abspath;

              if (svn_path_is_url(pair->dst_abspath_or_url))
                dst_url = pair->dst_abspath_or_url;
              else
                SVN_ERR(svn_wc__node_get_url(&dst_url, ctx->wc_ctx,
                                             pair->dst_abspath_or_url,
                                             scratch_pool, iterpool));
              commit_url = svn_path_url_add_component2(dst_url, dst_relpath,
                                                       scratch_pool);
              src_abspath = svn_dirent_join(pair->src_abspath_or_url,
                                            dst_relpath, iterpool);
              SVN_ERR(queue_prop_change_commit_items(src_abspath,
                                                     commit_url, commit_items,
                                                     SVN_PROP_EXTERNALS,
                                                     externals_propval,
                                                     scratch_pool, iterpool));
            }
        }
    }

  if (SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx))
    {
      const char *tmp_file;

      SVN_ERR(svn_client__get_log_msg(&message, &tmp_file, commit_items,
                                      ctx, scratch_pool));
      if (! message)
        {
          svn_pool_destroy(iterpool);
          svn_pool_destroy(session_pool);
          return SVN_NO_ERROR;
        }
    }
  else
    message = "";

  /* Sort and condense our COMMIT_ITEMS. */
  SVN_ERR(svn_client__condense_commit_items(&top_dst_url,
                                            commit_items, scratch_pool));

  /* Add the commit items to the DAV commit item list to provide access
     to dav properties (for pre http-v2 DAV) */
  apr_array_cat(commit_items_for_dav, commit_items);

#ifdef ENABLE_EV2_SHIMS
  if (commit_items)
    {
      relpath_map = apr_hash_make(scratch_pool);
      for (i = 0; i < commit_items->nelts; i++)
        {
          svn_client_commit_item3_t *item = APR_ARRAY_IDX(commit_items, i,
                                                  svn_client_commit_item3_t *);
          const char *relpath;

          if (!item->path)
            continue;

          svn_pool_clear(iterpool);
          SVN_ERR(svn_wc__node_get_origin(NULL, NULL, &relpath, NULL, NULL,
                                          NULL, NULL,
                                          ctx->wc_ctx, item->path, FALSE,
                                          scratch_pool, iterpool));
          if (relpath)
            svn_hash_sets(relpath_map, relpath, item->path);
        }
    }
#endif

  SVN_ERR(svn_ra_reparent(ra_session, top_dst_url, session_pool));

  SVN_ERR(svn_client__ensure_revprop_table(&commit_revprops, revprop_table,
                                           message, ctx, session_pool));

  /* Fetch RA commit editor. */
#ifdef ENABLE_EV2_SHIMS
  SVN_ERR(svn_ra__register_editor_shim_callbacks(ra_session,
                        svn_client__get_shim_callbacks(ctx->wc_ctx, relpath_map,
                                                       session_pool)));
#endif
  SVN_ERR(svn_ra_get_commit_editor3(ra_session, &editor, &edit_baton,
                                    commit_revprops,
                                    commit_callback,
                                    commit_baton, NULL,
                                    TRUE, /* No lock tokens */
                                    session_pool));

  /* Perform the commit. */
  SVN_ERR_W(svn_client__do_commit(top_dst_url, commit_items,
                                  editor, edit_baton,
                                  NULL /* notify_path_prefix */,
                                  NULL, ctx, session_pool, session_pool),
            _("Commit failed (details follow):"));

  svn_pool_destroy(iterpool);
  svn_pool_destroy(session_pool);

  return SVN_NO_ERROR;
}

/* A baton for notification_adjust_func(). */
struct notification_adjust_baton
{
  svn_wc_notify_func2_t inner_func;
  void *inner_baton;
  const char *checkout_abspath;
  const char *final_abspath;
};

/* A svn_wc_notify_func2_t function that wraps BATON->inner_func (whose
 * baton is BATON->inner_baton) and adjusts the notification paths that
 * start with BATON->checkout_abspath to start instead with
 * BATON->final_abspath. */
static void
notification_adjust_func(void *baton,
                         const svn_wc_notify_t *notify,
                         apr_pool_t *pool)
{
  struct notification_adjust_baton *nb = baton;
  svn_wc_notify_t *inner_notify = svn_wc_dup_notify(notify, pool);
  const char *relpath;

  relpath = svn_dirent_skip_ancestor(nb->checkout_abspath, notify->path);
  inner_notify->path = svn_dirent_join(nb->final_abspath, relpath, pool);

  if (nb->inner_func)
    nb->inner_func(nb->inner_baton, inner_notify, pool);
}

/* Peform each individual copy operation for a repos -> wc copy.  A
   helper for repos_to_wc_copy().

   Resolve PAIR->src_revnum to a real revision number if it isn't already. */
static svn_error_t *
repos_to_wc_copy_single(svn_boolean_t *timestamp_sleep,
                        svn_client__copy_pair_t *pair,
                        svn_boolean_t same_repositories,
                        svn_boolean_t ignore_externals,
                        svn_boolean_t pin_externals,
                        const apr_hash_t *externals_to_pin,
                        svn_ra_session_t *ra_session,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool)
{
  apr_hash_t *src_mergeinfo;
  const char *dst_abspath = pair->dst_abspath_or_url;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  if (!same_repositories && ctx->notify_func2)
    {
      svn_wc_notify_t *notify;
      notify = svn_wc_create_notify_url(
                            pair->src_abspath_or_url,
                            svn_wc_notify_foreign_copy_begin,
                            pool);
      notify->kind = pair->src_kind;
      ctx->notify_func2(ctx->notify_baton2, notify, pool);

      /* Allow a theoretical cancel to get through. */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));
    }

  if (pair->src_kind == svn_node_dir)
    {
      if (same_repositories)
        {
          const char *tmpdir_abspath, *tmp_abspath;

          /* Find a temporary location in which to check out the copy source. */
          SVN_ERR(svn_wc__get_tmpdir(&tmpdir_abspath, ctx->wc_ctx, dst_abspath,
                                     pool, pool));

          SVN_ERR(svn_io_open_unique_file3(NULL, &tmp_abspath, tmpdir_abspath,
                                           svn_io_file_del_on_close, pool, pool));

          /* Make a new checkout of the requested source. While doing so,
           * resolve pair->src_revnum to an actual revision number in case it
           * was until now 'invalid' meaning 'head'.  Ask this function not to
           * sleep for timestamps, by passing a sleep_needed output param.
           * Send notifications for all nodes except the root node, and adjust
           * them to refer to the destination rather than this temporary path. */
          {
            svn_wc_notify_func2_t old_notify_func2 = ctx->notify_func2;
            void *old_notify_baton2 = ctx->notify_baton2;
            struct notification_adjust_baton nb;
            svn_error_t *err;

            nb.inner_func = ctx->notify_func2;
            nb.inner_baton = ctx->notify_baton2;
            nb.checkout_abspath = tmp_abspath;
            nb.final_abspath = dst_abspath;
            ctx->notify_func2 = notification_adjust_func;
            ctx->notify_baton2 = &nb;

            /* Avoid a chicken-and-egg problem:
             * If pinning externals we'll need to adjust externals
             * properties before checking out any externals.
             * But copy needs to happen before pinning because else there
             * are no svn:externals properties to pin. */
            if (pin_externals)
              ignore_externals = TRUE;

            err = svn_client__checkout_internal(&pair->src_revnum, timestamp_sleep,
                                                pair->src_original,
                                                tmp_abspath,
                                                &pair->src_peg_revision,
                                                &pair->src_op_revision,
                                                svn_depth_infinity,
                                                ignore_externals, FALSE,
                                                ra_session, ctx, pool);

            ctx->notify_func2 = old_notify_func2;
            ctx->notify_baton2 = old_notify_baton2;

            SVN_ERR(err);
          }

          *timestamp_sleep = TRUE;

          /* Schedule dst_path for addition in parent, with copy history.
             Don't send any notification here.
             Then remove the temporary checkout's .svn dir in preparation for
             moving the rest of it into the final destination. */
          SVN_ERR(svn_wc_copy3(ctx->wc_ctx, tmp_abspath, dst_abspath,
                               TRUE /* metadata_only */,
                               ctx->cancel_func, ctx->cancel_baton,
                               NULL, NULL, pool));
          SVN_ERR(svn_wc__acquire_write_lock(NULL, ctx->wc_ctx, tmp_abspath,
                                             FALSE, pool, pool));
          SVN_ERR(svn_wc_remove_from_revision_control2(ctx->wc_ctx,
                                                       tmp_abspath,
                                                       FALSE, FALSE,
                                                       ctx->cancel_func,
                                                       ctx->cancel_baton,
                                                       pool));

          /* Move the temporary disk tree into place. */
          SVN_ERR(svn_io_file_rename2(tmp_abspath, dst_abspath, FALSE, pool));
        }
      else
        {
          *timestamp_sleep = TRUE;

          SVN_ERR(svn_client__copy_foreign(pair->src_abspath_or_url,
                                           dst_abspath,
                                           &pair->src_peg_revision,
                                           &pair->src_op_revision,
                                           svn_depth_infinity,
                                           FALSE /* make_parents */,
                                           TRUE /* already_locked */,
                                           ctx, pool));

          return SVN_NO_ERROR;
        }

      if (pin_externals)
        {
          apr_hash_t *pinned_externals;
          apr_hash_index_t *hi;
          apr_pool_t *iterpool;
          const char *repos_root_url;
          apr_hash_t *new_externals;
          apr_hash_t *new_depths;

          SVN_ERR(svn_ra_get_repos_root2(ra_session, &repos_root_url, pool));
          SVN_ERR(resolve_pinned_externals(&pinned_externals,
                                           externals_to_pin, pair,
                                           ra_session, repos_root_url,
                                           ctx, pool, pool));

          iterpool = svn_pool_create(pool);
          for (hi = apr_hash_first(pool, pinned_externals);
               hi;
               hi = apr_hash_next(hi))
            {
              const char *dst_relpath = apr_hash_this_key(hi);
              svn_string_t *externals_propval = apr_hash_this_val(hi);
              const char *local_abspath;

              svn_pool_clear(iterpool);

              local_abspath = svn_dirent_join(pair->dst_abspath_or_url,
                                              dst_relpath, iterpool);
              /* ### use a work queue? */
              SVN_ERR(svn_wc_prop_set4(ctx->wc_ctx, local_abspath,
                                       SVN_PROP_EXTERNALS, externals_propval,
                                       svn_depth_empty, TRUE /* skip_checks */,
                                       NULL  /* changelist_filter */,
                                       ctx->cancel_func, ctx->cancel_baton,
                                       NULL, NULL, /* no extra notification */
                                       iterpool));
            }

          /* Now update all externals in the newly created copy. */
          SVN_ERR(svn_wc__externals_gather_definitions(&new_externals,
                                                       &new_depths,
                                                       ctx->wc_ctx,
                                                       dst_abspath,
                                                       svn_depth_infinity,
                                                       iterpool, iterpool));
          SVN_ERR(svn_client__handle_externals(new_externals,
                                               new_depths,
                                               repos_root_url, dst_abspath,
                                               svn_depth_infinity,
                                               timestamp_sleep,
                                               ra_session,
                                               ctx, iterpool));
          svn_pool_destroy(iterpool);
        }
    } /* end directory case */

  else if (pair->src_kind == svn_node_file)
    {
      apr_hash_t *new_props;
      const char *src_rel;
      svn_stream_t *new_base_contents = svn_stream_buffered(pool);

      SVN_ERR(svn_ra_get_path_relative_to_session(ra_session, &src_rel,
                                                  pair->src_abspath_or_url,
                                                  pool));
      /* Fetch the file content. While doing so, resolve pair->src_revnum
       * to an actual revision number if it's 'invalid' meaning 'head'. */
      SVN_ERR(svn_ra_get_file(ra_session, src_rel, pair->src_revnum,
                              new_base_contents,
                              &pair->src_revnum, &new_props, pool));

      if (new_props && ! same_repositories)
        svn_hash_sets(new_props, SVN_PROP_MERGEINFO, NULL);

      *timestamp_sleep = TRUE;

      SVN_ERR(svn_wc_add_repos_file4(
         ctx->wc_ctx, dst_abspath,
         new_base_contents, NULL, new_props, NULL,
         same_repositories ? pair->src_abspath_or_url : NULL,
         same_repositories ? pair->src_revnum : SVN_INVALID_REVNUM,
         ctx->cancel_func, ctx->cancel_baton,
         pool));
    }

  /* Record the implied mergeinfo (before the notification callback
     is invoked for the root node). */
  SVN_ERR(svn_client__get_repos_mergeinfo(
            &src_mergeinfo, ra_session,
            pair->src_abspath_or_url, pair->src_revnum,
            svn_mergeinfo_inherited, TRUE /*squelch_incapable*/, pool));
  SVN_ERR(extend_wc_mergeinfo(dst_abspath, src_mergeinfo, ctx, pool));

  /* Do our own notification for the root node, even if we could possibly
     have delegated it.  See also issue #1552.

     ### Maybe this notification should mention the mergeinfo change. */
  if (ctx->notify_func2)
    {
      svn_wc_notify_t *notify = svn_wc_create_notify(
                                  dst_abspath, svn_wc_notify_add, pool);
      notify->kind = pair->src_kind;
      ctx->notify_func2(ctx->notify_baton2, notify, pool);
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
repos_to_wc_copy_locked(svn_boolean_t *timestamp_sleep,
                        const apr_array_header_t *copy_pairs,
                        const char *top_dst_abspath,
                        svn_boolean_t ignore_externals,
                        svn_boolean_t pin_externals,
                        const apr_hash_t *externals_to_pin,
                        svn_ra_session_t *ra_session,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *scratch_pool)
{
  int i;
  svn_boolean_t same_repositories;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* We've already checked for physical obstruction by a working file.
     But there could also be logical obstruction by an entry whose
     working file happens to be missing.*/
  SVN_ERR(verify_wc_dsts(copy_pairs, FALSE, FALSE, FALSE /* metadata_only */,
                         ctx, scratch_pool, iterpool));

  /* Decide whether the two repositories are the same or not. */
  {
    const char *parent_abspath;
    const char *src_uuid, *dst_uuid;

    /* Get the repository uuid of SRC_URL */
    SVN_ERR(svn_ra_get_uuid2(ra_session, &src_uuid, iterpool));

    /* Get repository uuid of dst's parent directory, since dst may
       not exist.  ### TODO:  we should probably walk up the wc here,
       in case the parent dir has an imaginary URL.  */
    if (copy_pairs->nelts == 1)
      parent_abspath = svn_dirent_dirname(top_dst_abspath, scratch_pool);
    else
      parent_abspath = top_dst_abspath;

    SVN_ERR(svn_client_get_repos_root(NULL /* root_url */, &dst_uuid,
                                      parent_abspath, ctx,
                                      iterpool, iterpool));
    /* ### Also check repos_root_url? */
    same_repositories = (strcmp(src_uuid, dst_uuid) == 0);
  }

  /* Perform the move for each of the copy_pairs. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      /* Check for cancellation */
      if (ctx->cancel_func)
        SVN_ERR(ctx->cancel_func(ctx->cancel_baton));

      svn_pool_clear(iterpool);

      SVN_ERR(repos_to_wc_copy_single(timestamp_sleep,
                                      APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *),
                                      same_repositories,
                                      ignore_externals,
                                      pin_externals, externals_to_pin,
                                      ra_session, ctx, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

static svn_error_t *
repos_to_wc_copy(svn_boolean_t *timestamp_sleep,
                 const apr_array_header_t *copy_pairs,
                 svn_boolean_t make_parents,
                 svn_boolean_t ignore_externals,
                 svn_boolean_t pin_externals,
                 const apr_hash_t *externals_to_pin,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_ra_session_t *ra_session;
  const char *top_src_url, *top_dst_abspath;
  apr_pool_t *iterpool = svn_pool_create(pool);
  const char *lock_abspath;
  int i;

  /* Get the real path for the source, based upon its peg revision. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      const char *src;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_client__repos_locations(&src, &pair->src_revnum, NULL, NULL,
                                          NULL,
                                          pair->src_abspath_or_url,
                                          &pair->src_peg_revision,
                                          &pair->src_op_revision, NULL,
                                          ctx, iterpool));

      pair->src_original = pair->src_abspath_or_url;
      pair->src_abspath_or_url = apr_pstrdup(pool, src);
    }

  SVN_ERR(get_copy_pair_ancestors(copy_pairs, &top_src_url, &top_dst_abspath,
                                  NULL, pool));
  lock_abspath = top_dst_abspath;
  if (copy_pairs->nelts == 1)
    {
      top_src_url = svn_uri_dirname(top_src_url, pool);
      lock_abspath = svn_dirent_dirname(top_dst_abspath, pool);
    }

  /* Open a repository session to the longest common src ancestor.  We do not
     (yet) have a working copy, so we don't have a corresponding path and
     tempfiles cannot go into the admin area. */
  SVN_ERR(svn_client_open_ra_session2(&ra_session, top_src_url, lock_abspath,
                                      ctx, pool, pool));

  /* Get the correct src path for the peg revision used, and verify that we
     aren't overwriting an existing path. */
  for (i = 0; i < copy_pairs->nelts; i++)
    {
      svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);
      svn_node_kind_t dst_parent_kind, dst_kind;
      const char *dst_parent;
      const char *src_rel;

      svn_pool_clear(iterpool);

      /* Next, make sure that the path exists in the repository. */
      SVN_ERR(svn_ra_get_path_relative_to_session(ra_session, &src_rel,
                                                  pair->src_abspath_or_url,
                                                  iterpool));
      SVN_ERR(svn_ra_check_path(ra_session, src_rel, pair->src_revnum,
                                &pair->src_kind, pool));
      if (pair->src_kind == svn_node_none)
        {
          if (SVN_IS_VALID_REVNUM(pair->src_revnum))
            return svn_error_createf
              (SVN_ERR_FS_NOT_FOUND, NULL,
               _("Path '%s' not found in revision %ld"),
               pair->src_abspath_or_url, pair->src_revnum);
          else
            return svn_error_createf
              (SVN_ERR_FS_NOT_FOUND, NULL,
               _("Path '%s' not found in head revision"),
               pair->src_abspath_or_url);
        }

      /* Figure out about dst. */
      SVN_ERR(svn_io_check_path(pair->dst_abspath_or_url, &dst_kind,
                                iterpool));
      if (dst_kind != svn_node_none)
        {
          return svn_error_createf(
            SVN_ERR_ENTRY_EXISTS, NULL,
            _("Path '%s' already exists"),
            svn_dirent_local_style(pair->dst_abspath_or_url, pool));
        }

      /* Make sure the destination parent is a directory and produce a clear
         error message if it is not. */
      dst_parent = svn_dirent_dirname(pair->dst_abspath_or_url, iterpool);
      SVN_ERR(svn_io_check_path(dst_parent, &dst_parent_kind, iterpool));
      if (make_parents && dst_parent_kind == svn_node_none)
        {
          SVN_ERR(svn_client__make_local_parents(dst_parent, TRUE, ctx,
                                                 iterpool));
        }
      else if (dst_parent_kind != svn_node_dir)
        {
          return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                                   _("Path '%s' is not a directory"),
                                   svn_dirent_local_style(dst_parent, pool));
        }
    }
  svn_pool_destroy(iterpool);

  SVN_WC__CALL_WITH_WRITE_LOCK(
    repos_to_wc_copy_locked(timestamp_sleep,
                            copy_pairs, top_dst_abspath, ignore_externals,
                            pin_externals, externals_to_pin,
                            ra_session, ctx, pool),
    ctx->wc_ctx, lock_abspath, FALSE, pool);
  return SVN_NO_ERROR;
}

#define NEED_REPOS_REVNUM(revision) \
        ((revision.kind != svn_opt_revision_unspecified) \
          && (revision.kind != svn_opt_revision_working))

/* ...
 *
 * Set *TIMESTAMP_SLEEP to TRUE if a sleep is required; otherwise do not
 * change *TIMESTAMP_SLEEP.  This output will be valid even if the
 * function returns an error.
 *
 * Perform all allocations in POOL.
 */
static svn_error_t *
try_copy(svn_boolean_t *timestamp_sleep,
         const apr_array_header_t *sources,
         const char *dst_path_in,
         svn_boolean_t is_move,
         svn_boolean_t allow_mixed_revisions,
         svn_boolean_t metadata_only,
         svn_boolean_t make_parents,
         svn_boolean_t ignore_externals,
         svn_boolean_t pin_externals,
         const apr_hash_t *externals_to_pin,
         const apr_hash_t *revprop_table,
         svn_commit_callback2_t commit_callback,
         void *commit_baton,
         svn_client_ctx_t *ctx,
         apr_pool_t *pool)
{
  apr_array_header_t *copy_pairs =
                        apr_array_make(pool, sources->nelts,
                                       sizeof(svn_client__copy_pair_t *));
  svn_boolean_t srcs_are_urls, dst_is_url;
  int i;

  /* Assert instead of crashing if the sources list is empty. */
  SVN_ERR_ASSERT(sources->nelts > 0);

  /* Are either of our paths URLs?  Just check the first src_path.  If
     there are more than one, we'll check for homogeneity among them
     down below. */
  srcs_are_urls = svn_path_is_url(APR_ARRAY_IDX(sources, 0,
                                  svn_client_copy_source_t *)->path);
  dst_is_url = svn_path_is_url(dst_path_in);
  if (!dst_is_url)
    SVN_ERR(svn_dirent_get_absolute(&dst_path_in, dst_path_in, pool));

  /* If we have multiple source paths, it implies the dst_path is a
     directory we are moving or copying into.  Populate the COPY_PAIRS
     array to contain a destination path for each of the source paths. */
  if (sources->nelts > 1)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);

      for (i = 0; i < sources->nelts; i++)
        {
          svn_client_copy_source_t *source = APR_ARRAY_IDX(sources, i,
                                               svn_client_copy_source_t *);
          svn_client__copy_pair_t *pair = apr_pcalloc(pool, sizeof(*pair));
          const char *src_basename;
          svn_boolean_t src_is_url = svn_path_is_url(source->path);

          svn_pool_clear(iterpool);

          if (src_is_url)
            {
              pair->src_abspath_or_url = apr_pstrdup(pool, source->path);
              src_basename = svn_uri_basename(pair->src_abspath_or_url,
                                              iterpool);
            }
          else
            {
              SVN_ERR(svn_dirent_get_absolute(&pair->src_abspath_or_url,
                                              source->path, pool));
              src_basename = svn_dirent_basename(pair->src_abspath_or_url,
                                                 iterpool);
            }

          pair->src_op_revision = *source->revision;
          pair->src_peg_revision = *source->peg_revision;
          pair->src_kind = svn_node_unknown;

          SVN_ERR(svn_opt_resolve_revisions(&pair->src_peg_revision,
                                            &pair->src_op_revision,
                                            src_is_url,
                                            TRUE,
                                            iterpool));

          /* Check to see if all the sources are urls or all working copy
           * paths. */
          if (src_is_url != srcs_are_urls)
            return svn_error_create
              (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
               _("Cannot mix repository and working copy sources"));

          if (dst_is_url)
            pair->dst_abspath_or_url =
              svn_path_url_add_component2(dst_path_in, src_basename, pool);
          else
            pair->dst_abspath_or_url = svn_dirent_join(dst_path_in,
                                                       src_basename, pool);
          APR_ARRAY_PUSH(copy_pairs, svn_client__copy_pair_t *) = pair;
        }

      svn_pool_destroy(iterpool);
    }
  else
    {
      /* Only one source path. */
      svn_client__copy_pair_t *pair = apr_pcalloc(pool, sizeof(*pair));
      svn_client_copy_source_t *source =
        APR_ARRAY_IDX(sources, 0, svn_client_copy_source_t *);
      svn_boolean_t src_is_url = svn_path_is_url(source->path);

      if (src_is_url)
        pair->src_abspath_or_url = apr_pstrdup(pool, source->path);
      else
        SVN_ERR(svn_dirent_get_absolute(&pair->src_abspath_or_url,
                                        source->path, pool));
      pair->src_op_revision = *source->revision;
      pair->src_peg_revision = *source->peg_revision;
      pair->src_kind = svn_node_unknown;

      SVN_ERR(svn_opt_resolve_revisions(&pair->src_peg_revision,
                                        &pair->src_op_revision,
                                        src_is_url, TRUE, pool));

      pair->dst_abspath_or_url = dst_path_in;
      APR_ARRAY_PUSH(copy_pairs, svn_client__copy_pair_t *) = pair;
    }

  if (!srcs_are_urls && !dst_is_url)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);

      for (i = 0; i < copy_pairs->nelts; i++)
        {
          svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                            svn_client__copy_pair_t *);

          svn_pool_clear(iterpool);

          if (svn_dirent_is_child(pair->src_abspath_or_url,
                                  pair->dst_abspath_or_url, iterpool))
            return svn_error_createf
              (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
               _("Cannot copy path '%s' into its own child '%s'"),
               svn_dirent_local_style(pair->src_abspath_or_url, pool),
               svn_dirent_local_style(pair->dst_abspath_or_url, pool));
        }

      svn_pool_destroy(iterpool);
    }

  /* A file external should not be moved since the file external is
     implemented as a switched file and it would delete the file the
     file external is switched to, which is not the behavior the user
     would probably want. */
  if (is_move && !srcs_are_urls)
    {
      apr_pool_t *iterpool = svn_pool_create(pool);

      for (i = 0; i < copy_pairs->nelts; i++)
        {
          svn_client__copy_pair_t *pair =
            APR_ARRAY_IDX(copy_pairs, i, svn_client__copy_pair_t *);
          svn_node_kind_t external_kind;
          const char *defining_abspath;

          svn_pool_clear(iterpool);

          SVN_ERR_ASSERT(svn_dirent_is_absolute(pair->src_abspath_or_url));
          SVN_ERR(svn_wc__read_external_info(&external_kind, &defining_abspath,
                                             NULL, NULL, NULL, ctx->wc_ctx,
                                             pair->src_abspath_or_url,
                                             pair->src_abspath_or_url, TRUE,
                                             iterpool, iterpool));

          if (external_kind != svn_node_none)
            return svn_error_createf(
                     SVN_ERR_WC_CANNOT_MOVE_FILE_EXTERNAL,
                     NULL,
                     _("Cannot move the external at '%s'; please "
                       "edit the svn:externals property on '%s'."),
                     svn_dirent_local_style(pair->src_abspath_or_url, pool),
                     svn_dirent_local_style(defining_abspath, pool));
        }
      svn_pool_destroy(iterpool);
    }

  if (is_move)
    {
      /* Disallow moves between the working copy and the repository. */
      if (srcs_are_urls != dst_is_url)
        {
          return svn_error_create
            (SVN_ERR_UNSUPPORTED_FEATURE, NULL,
             _("Moves between the working copy and the repository are not "
               "supported"));
        }

      /* Disallow moving any path/URL onto or into itself. */
      for (i = 0; i < copy_pairs->nelts; i++)
        {
          svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                            svn_client__copy_pair_t *);

          if (strcmp(pair->src_abspath_or_url,
                     pair->dst_abspath_or_url) == 0)
            return svn_error_createf(
              SVN_ERR_UNSUPPORTED_FEATURE, NULL,
              srcs_are_urls ?
                _("Cannot move URL '%s' into itself") :
                _("Cannot move path '%s' into itself"),
              srcs_are_urls ?
                pair->src_abspath_or_url :
                svn_dirent_local_style(pair->src_abspath_or_url, pool));
        }
    }
  else
    {
      if (!srcs_are_urls)
        {
          /* If we are doing a wc->* copy, but with an operational revision
             other than the working copy revision, we are really doing a
             repo->* copy, because we're going to need to get the rev from the
             repo. */

          svn_boolean_t need_repos_op_rev = FALSE;
          svn_boolean_t need_repos_peg_rev = FALSE;

          /* Check to see if any revision is something other than
             svn_opt_revision_unspecified or svn_opt_revision_working. */
          for (i = 0; i < copy_pairs->nelts; i++)
            {
              svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                svn_client__copy_pair_t *);

              if (NEED_REPOS_REVNUM(pair->src_op_revision))
                need_repos_op_rev = TRUE;

              if (NEED_REPOS_REVNUM(pair->src_peg_revision))
                need_repos_peg_rev = TRUE;

              if (need_repos_op_rev || need_repos_peg_rev)
                break;
            }

          if (need_repos_op_rev || need_repos_peg_rev)
            {
              apr_pool_t *iterpool = svn_pool_create(pool);

              for (i = 0; i < copy_pairs->nelts; i++)
                {
                  const char *copyfrom_repos_root_url;
                  const char *copyfrom_repos_relpath;
                  const char *url;
                  svn_revnum_t copyfrom_rev;
                  svn_client__copy_pair_t *pair = APR_ARRAY_IDX(copy_pairs, i,
                                                    svn_client__copy_pair_t *);

                  svn_pool_clear(iterpool);

                  SVN_ERR_ASSERT(svn_dirent_is_absolute(pair->src_abspath_or_url));

                  SVN_ERR(svn_wc__node_get_origin(NULL, &copyfrom_rev,
                                                  &copyfrom_repos_relpath,
                                                  &copyfrom_repos_root_url,
                                                  NULL, NULL, NULL,
                                                  ctx->wc_ctx,
                                                  pair->src_abspath_or_url,
                                                  TRUE, iterpool, iterpool));

                  if (copyfrom_repos_relpath)
                    url = svn_path_url_add_component2(copyfrom_repos_root_url,
                                                      copyfrom_repos_relpath,
                                                      pool);
                  else
                    return svn_error_createf
                      (SVN_ERR_ENTRY_MISSING_URL, NULL,
                       _("'%s' does not have a URL associated with it"),
                       svn_dirent_local_style(pair->src_abspath_or_url, pool));

                  pair->src_abspath_or_url = url;

                  if (!need_repos_peg_rev
                      || pair->src_peg_revision.kind == svn_opt_revision_base)
                    {
                      /* Default the peg revision to that of the WC entry. */
                      pair->src_peg_revision.kind = svn_opt_revision_number;
                      pair->src_peg_revision.value.number = copyfrom_rev;
                    }

                  if (pair->src_op_revision.kind == svn_opt_revision_base)
                    {
                      /* Use the entry's revision as the operational rev. */
                      pair->src_op_revision.kind = svn_opt_revision_number;
                      pair->src_op_revision.value.number = copyfrom_rev;
                    }
                }

              svn_pool_destroy(iterpool);
              srcs_are_urls = TRUE;
            }
        }
    }

  /* Now, call the right handler for the operation. */
  if ((! srcs_are_urls) && (! dst_is_url))
    {
      SVN_ERR(verify_wc_srcs_and_dsts(copy_pairs, make_parents, is_move,
                                      metadata_only, ctx, pool, pool));

      /* Copy or move all targets. */
      if (is_move)
        return svn_error_trace(do_wc_to_wc_moves(timestamp_sleep,
                                                 copy_pairs, dst_path_in,
                                                 allow_mixed_revisions,
                                                 metadata_only,
                                                 ctx, pool));
      else
        {
          /* We ignore these values, so assert the default value */
          SVN_ERR_ASSERT(allow_mixed_revisions);
          return svn_error_trace(do_wc_to_wc_copies(timestamp_sleep,
                                                    copy_pairs,
                                                    metadata_only,
                                                    pin_externals,
                                                    externals_to_pin,
                                                    ctx, pool));
        }
    }
  else if ((! srcs_are_urls) && (dst_is_url))
    {
      return svn_error_trace(
        wc_to_repos_copy(copy_pairs, make_parents, revprop_table,
                         commit_callback, commit_baton,
                         pin_externals, externals_to_pin, ctx, pool));
    }
  else if ((srcs_are_urls) && (! dst_is_url))
    {
      return svn_error_trace(
        repos_to_wc_copy(timestamp_sleep,
                         copy_pairs, make_parents, ignore_externals,
                         pin_externals, externals_to_pin, ctx, pool));
    }
  else
    {
      return svn_error_trace(
        repos_to_repos_copy(copy_pairs, make_parents, revprop_table,
                            commit_callback, commit_baton, ctx, is_move,
                            pin_externals, externals_to_pin, pool));
    }
}



/* Public Interfaces */
svn_error_t *
svn_client_copy7(const apr_array_header_t *sources,
                 const char *dst_path,
                 svn_boolean_t copy_as_child,
                 svn_boolean_t make_parents,
                 svn_boolean_t ignore_externals,
                 svn_boolean_t metadata_only,
                 svn_boolean_t pin_externals,
                 const apr_hash_t *externals_to_pin,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  svn_error_t *err;
  svn_boolean_t timestamp_sleep = FALSE;
  apr_pool_t *subpool = svn_pool_create(pool);

  if (sources->nelts > 1 && !copy_as_child)
    return svn_error_create(SVN_ERR_CLIENT_MULTIPLE_SOURCES_DISALLOWED,
                            NULL, NULL);

  err = try_copy(&timestamp_sleep,
                 sources, dst_path,
                 FALSE /* is_move */,
                 TRUE /* allow_mixed_revisions */,
                 metadata_only,
                 make_parents,
                 ignore_externals,
                 pin_externals,
                 externals_to_pin,
                 revprop_table,
                 commit_callback, commit_baton,
                 ctx,
                 subpool);

  /* If the destination exists, try to copy the sources as children of the
     destination. */
  if (copy_as_child && err && (sources->nelts == 1)
        && (err->apr_err == SVN_ERR_ENTRY_EXISTS
            || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_path = APR_ARRAY_IDX(sources, 0,
                                           svn_client_copy_source_t *)->path;
      const char *src_basename;
      svn_boolean_t src_is_url = svn_path_is_url(src_path);
      svn_boolean_t dst_is_url = svn_path_is_url(dst_path);

      svn_error_clear(err);
      svn_pool_clear(subpool);

      src_basename = src_is_url ? svn_uri_basename(src_path, subpool)
                                : svn_dirent_basename(src_path, subpool);
      dst_path
        = dst_is_url ? svn_path_url_add_component2(dst_path, src_basename,
                                                   subpool)
                     : svn_dirent_join(dst_path, src_basename, subpool);

      err = try_copy(&timestamp_sleep,
                     sources, dst_path,
                     FALSE /* is_move */,
                     TRUE /* allow_mixed_revisions */,
                     metadata_only,
                     make_parents,
                     ignore_externals,
                     pin_externals,
                     externals_to_pin,
                     revprop_table,
                     commit_callback, commit_baton,
                     ctx,
                     subpool);
    }

  /* Sleep if required.  DST_PATH is not a URL in these cases. */
  if (timestamp_sleep)
    svn_io_sleep_for_timestamps(dst_path, subpool);

  svn_pool_destroy(subpool);
  return svn_error_trace(err);
}


svn_error_t *
svn_client_move7(const apr_array_header_t *src_paths,
                 const char *dst_path,
                 svn_boolean_t move_as_child,
                 svn_boolean_t make_parents,
                 svn_boolean_t allow_mixed_revisions,
                 svn_boolean_t metadata_only,
                 const apr_hash_t *revprop_table,
                 svn_commit_callback2_t commit_callback,
                 void *commit_baton,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *pool)
{
  const svn_opt_revision_t head_revision
    = { svn_opt_revision_head, { 0 } };
  svn_error_t *err;
  svn_boolean_t timestamp_sleep = FALSE;
  int i;
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_array_header_t *sources = apr_array_make(pool, src_paths->nelts,
                                  sizeof(const svn_client_copy_source_t *));

  if (src_paths->nelts > 1 && !move_as_child)
    return svn_error_create(SVN_ERR_CLIENT_MULTIPLE_SOURCES_DISALLOWED,
                            NULL, NULL);

  for (i = 0; i < src_paths->nelts; i++)
    {
      const char *src_path = APR_ARRAY_IDX(src_paths, i, const char *);
      svn_client_copy_source_t *copy_source = apr_palloc(pool,
                                                         sizeof(*copy_source));

      copy_source->path = src_path;
      copy_source->revision = &head_revision;
      copy_source->peg_revision = &head_revision;

      APR_ARRAY_PUSH(sources, svn_client_copy_source_t *) = copy_source;
    }

  err = try_copy(&timestamp_sleep,
                 sources, dst_path,
                 TRUE /* is_move */,
                 allow_mixed_revisions,
                 metadata_only,
                 make_parents,
                 FALSE /* ignore_externals */,
                 FALSE /* pin_externals */,
                 NULL /* externals_to_pin */,
                 revprop_table,
                 commit_callback, commit_baton,
                 ctx,
                 subpool);

  /* If the destination exists, try to move the sources as children of the
     destination. */
  if (move_as_child && err && (src_paths->nelts == 1)
        && (err->apr_err == SVN_ERR_ENTRY_EXISTS
            || err->apr_err == SVN_ERR_FS_ALREADY_EXISTS))
    {
      const char *src_path = APR_ARRAY_IDX(src_paths, 0, const char *);
      const char *src_basename;
      svn_boolean_t src_is_url = svn_path_is_url(src_path);
      svn_boolean_t dst_is_url = svn_path_is_url(dst_path);

      svn_error_clear(err);
      svn_pool_clear(subpool);

      src_basename = src_is_url ? svn_uri_basename(src_path, pool)
                                : svn_dirent_basename(src_path, pool);
      dst_path
        = dst_is_url ? svn_path_url_add_component2(dst_path, src_basename,
                                                   subpool)
                     : svn_dirent_join(dst_path, src_basename, subpool);

      err = try_copy(&timestamp_sleep,
                     sources, dst_path,
                     TRUE /* is_move */,
                     allow_mixed_revisions,
                     metadata_only,
                     make_parents,
                     FALSE /* ignore_externals */,
                     FALSE /* pin_externals */,
                     NULL /* externals_to_pin */,
                     revprop_table,
                     commit_callback, commit_baton,
                     ctx,
                     subpool);
    }

  /* Sleep if required.  DST_PATH is not a URL in these cases. */
  if (timestamp_sleep)
    svn_io_sleep_for_timestamps(dst_path, subpool);

  svn_pool_destroy(subpool);
  return svn_error_trace(err);
}
