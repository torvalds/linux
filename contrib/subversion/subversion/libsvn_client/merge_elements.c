/*
 * merge_elements.c: element-based merging
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

#include <assert.h>
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_hash.h>
#include "svn_types.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_dirent_uri.h"

#include "client.h"
#include "private/svn_element.h"

#include "svn_private_config.h"


/* Print a notification.
 * ### TODO: Send notifications through ctx->notify_func2().
 * ### TODO: Only when 'verbose' output is requested.
 */
static
__attribute__((format(printf, 1, 2)))
void
verbose_notify(const char *fmt,
               ...)
{
  va_list ap;

  va_start(ap, fmt);
  vprintf(fmt, ap);
  if (fmt[strlen(fmt) - 1] != '\n')
    printf("\n");
  va_end(ap);
}

/* Return a string representation of PATHREV. */
static const char *
pathrev_str(const svn_client__pathrev_t *pathrev,
            apr_pool_t *pool)
{
  const char *rrpath
    = svn_uri_skip_ancestor(pathrev->repos_root_url, pathrev->url, pool);

  return apr_psprintf(pool, "^/%s@%ld", rrpath, pathrev->rev);
}

/* Element matching info.
 */
typedef struct element_matching_info_t
{
  void *info;
} element_matching_info_t;

/* Return a string representation of INFO. */
static const char *
element_matching_info_str(const element_matching_info_t *info,
                          apr_pool_t *result_pool)
{
  /* ### */
  const char *str = "{...}";

  return str;
}

/* Assign EIDs (in memory) to the source-left, source-right and target
 * trees.
 */
static svn_error_t *
assign_eids_to_trees(svn_element__tree_t **tree_left_p,
                     svn_element__tree_t **tree_right_p,
                     svn_element__tree_t **tree_target_p,
                     const svn_client__pathrev_t *src_left,
                     const svn_client__pathrev_t *src_right,
                     merge_target_t *target,
                     svn_ra_session_t *ra_session,
                     element_matching_info_t *element_matching_info,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  verbose_notify("--- Assigning EIDs to trees");

  /* ### */
  return SVN_NO_ERROR;
}

/* Perform a three-way tree merge. Write the result to *MERGE_RESULT_P.
 *
 * Set *CONFLICTS_P to describe any conflicts, or set *CONFLICTS_P to
 * null if there are none.
 */
static svn_error_t *
merge_trees(svn_element__tree_t **merge_result_p,
            void **conflicts_p,
            svn_element__tree_t *tree_left,
            svn_element__tree_t *tree_right,
            svn_element__tree_t *tree_target,
            apr_pool_t *result_pool,
            apr_pool_t *scratch_pool)
{
  verbose_notify("--- Merging trees");

  /* ### */
  *merge_result_p = NULL;
  *conflicts_p = NULL;
  return SVN_NO_ERROR;
}

/* Convert the MERGE_RESULT to a series of WC edits and apply those to
 * the WC described in TARGET.
 */
static svn_error_t *
apply_merge_result_to_wc(merge_target_t *target,
                         svn_element__tree_t *merge_result,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool)
{
  verbose_notify("--- Writing merge result to WC");

  return SVN_NO_ERROR;
}

/* Do a three-way element-based merge for one merge source range,
 * SRC_LEFT:SRC_RIGHT. If there are no conflicts, write the result to the
 * WC described in TARGET.
 */
static svn_error_t *
merge_elements_one_source(svn_boolean_t *use_sleep,
                          const svn_client__pathrev_t *src_left,
                          const svn_client__pathrev_t *src_right,
                          merge_target_t *target,
                          svn_ra_session_t *ra_session,
                          element_matching_info_t *element_matching_info,
                          svn_boolean_t diff_ignore_ancestry,
                          svn_boolean_t force_delete,
                          svn_boolean_t dry_run,
                          const apr_array_header_t *merge_options,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *scratch_pool)
{
  svn_element__tree_t *tree_left, *tree_right, *tree_target;
  svn_element__tree_t *merge_result;
  void *conflicts;

  verbose_notify("--- Merging by elements: "
                 "left=%s, right=%s, matching=%s",
                 pathrev_str(src_left, scratch_pool),
                 pathrev_str(src_right, scratch_pool),
                 element_matching_info_str(element_matching_info,
                                           scratch_pool));

  /* assign EIDs (in memory) to the source-left, source-right and target
     trees */
  SVN_ERR(assign_eids_to_trees(&tree_left, &tree_right, &tree_target,
                               src_left, src_right, target, ra_session,
                               element_matching_info,
                               ctx, scratch_pool, scratch_pool));

  /* perform a tree merge, creating a temporary result (in memory) */
  SVN_ERR(merge_trees(&merge_result, &conflicts,
                      tree_left, tree_right, tree_target,
                      scratch_pool, scratch_pool));

  /* check for (new style) conflicts in the result; if any, bail out */
  if (conflicts)
    {
      return svn_error_create(SVN_ERR_UNSUPPORTED_FEATURE, NULL,
                              _("Merge had conflicts; "
                                "this is not yet supported"));
    }

  /* convert the result to a series of WC edits and apply those to the WC */
  if (dry_run)
    {
      verbose_notify("--- Dry run; not writing merge result to WC");
    }
  else
    {
      SVN_ERR(apply_merge_result_to_wc(target, merge_result,
                                       ctx, scratch_pool));
      *use_sleep = TRUE;
    }

  /* forget all the EID metadata */
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client__merge_elements(svn_boolean_t *use_sleep,
                           apr_array_header_t *merge_sources,
                           merge_target_t *target,
                           svn_ra_session_t *ra_session,
                           svn_boolean_t diff_ignore_ancestry,
                           svn_boolean_t force_delete,
                           svn_boolean_t dry_run,
                           const apr_array_header_t *merge_options,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  int i;

  /* Merge each source range in turn */
  for (i = 0; i < merge_sources->nelts; i++)
    {
      merge_source_t *source
        = APR_ARRAY_IDX(merge_sources, i, void *);
      element_matching_info_t *element_matching_info;

      /* ### TODO: get element matching info from the user */
      element_matching_info = NULL;

      SVN_ERR(merge_elements_one_source(use_sleep,
                                        source->loc1, source->loc2,
                                        target, ra_session,
                                        element_matching_info,
                                        diff_ignore_ancestry,
                                        force_delete, dry_run, merge_options,
                                        ctx, scratch_pool));
    }

  return SVN_NO_ERROR;
}
