/*
 * merge.c: merging
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



/*** Includes ***/

#include <assert.h>
#include <apr_strings.h>
#include <apr_tables.h>
#include <apr_hash.h>
#include "svn_types.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_delta.h"
#include "svn_diff.h"
#include "svn_mergeinfo.h"
#include "svn_client.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_utf.h"
#include "svn_pools.h"
#include "svn_config.h"
#include "svn_props.h"
#include "svn_time.h"
#include "svn_sorts.h"
#include "svn_subst.h"
#include "svn_ra.h"
#include "client.h"
#include "mergeinfo.h"

#include "private/svn_fspath.h"
#include "private/svn_mergeinfo_private.h"
#include "private/svn_client_private.h"
#include "private/svn_sorts_private.h"
#include "private/svn_subr_private.h"
#include "private/svn_wc_private.h"

#include "svn_private_config.h"


/*-----------------------------------------------------------------------*/

/* MERGEINFO MERGE SOURCE NORMALIZATION
 *
 * Nearly any helper function herein that accepts two URL/revision
 * pairs (or equivalent struct merge_source_t) expects one of two things
 * to be true:
 *
 *    1.  that mergeinfo is not being recorded at all for this
 *        operation, or
 *
 *    2.  that the pairs represent two locations along a single line
 *        of version history such that there are no copies in the
 *        history of the object between the locations when treating
 *        the oldest of the two locations as non-inclusive.  In other
 *        words, if there is a copy at all between them, there is only
 *        one copy and its source was the oldest of the two locations.
 *
 * We use svn_ra_get_location_segments() to split a given range of
 * revisions across an object's history into several which obey these
 * rules.  For example, an extract from the log of Subversion's own
 * /subversion/tags/1.4.5 directory shows the following copies between
 * r859500 and r866500 (omitting the '/subversion' prefix for clarity):
 *
 *    r859598:
 *      A /branches/1.4.x  (from /trunk:859597)
 *
 *    r865417:
 *      A /tags/1.4.4      (from /branches/1.4.x:865262)
 *    # Notice that this copy leaves a gap between 865262 and 865417.
 *
 *    r866420:
 *      A /branches/1.4.5  (from /tags/1.4.4:866419)
 *
 *    r866425:
 *      D /branches/1.4.5
 *      A /tags/1.4.5      (from /branches/1.4.5:866424)
 *
 * In graphical form:
 *
 *                859500 859597 865262        866419 866424 866500
 *                  .      .      .             .      .      .
 *    trunk       ------------------------------------------------
 *                         \      .             .      .
 *    branches/1.4.x        A-------------------------------------
 *                          .     \______       .      .
 *                          .            \      .      .
 *    tags/1.4.4            .             A-----------------------
 *                          .             .     \      .
 *    branches/1.4.5        .             .      A------D
 *                          .             .      .     \.
 *    tags/1.4.5            .             .      .      A---------
 *                          .             .      .      .
 *                       859598        865417 866420 866425
 *
 * A merge of the difference between r859500 and r866500 of this directory
 * gets split into sequential merges of the following location pairs.
 *
 *                859500 859597 865262 865416 866419 866424 866500
 *                  .      .      .      .      .      .      .
 *    trunk         (======]      .      .      .      .      .
 *                                .      .      .      .      .
 *    trunk                (      .      .      .      .      .
 *    branches/1.4.x        ======]      .      .      .      .
 *                                       .      .      .      .
 *    branches/1.4.x              (      .      .      .      .
 *    tags/1.4.4                   =============]      .      .
 *    implicit_src_gap            (======]      .      .      .
 *                                              .      .      .
 *    tags/1.4.4                                (      .      .
 *    branches/1.4.5                             ======]      .
 *                                                     .      .
 *    branches/1.4.5                                   (      .
 *    tags/1.4.5                                        ======]
 *
 * which are represented in merge_source_t as:
 *
 *    [/trunk:859500, /trunk:859597]
 *    (recorded in svn:mergeinfo as /trunk:859501-859597)
 *
 *    [/trunk:859597, /branches/1.4.x:865262]
 *    (recorded in svn:mergeinfo as /branches/1.4.x:859598-865262)
 *
 *    [/branches/1.4.x:865262, /tags/1.4.4@866419]
 *    (recorded in svn:mergeinfo as /tags/1.4.4:865263-866419)
 *    (and there is a gap, the revision range [865262, 865416])
 *
 *    [/tags/1.4.4@866419, /branches/1.4.5@866424]
 *    (recorded in svn:mergeinfo as /branches/1.4.5:866420-866424)
 *
 *    [/branches/1.4.5@866424, /tags/1.4.5@866500]
 *    (recorded in svn:mergeinfo as /tags/1.4.5:866425-866500)
 *
 * Our helper functions would then operate on one of these location
 * pairs at a time.
 */

/* WHICH SVN_CLIENT_MERGE* API DO I WANT?
 *
 * libsvn_client has three public merge APIs; they are all wrappers
 * around the do_merge engine.  Which one to use depends on the number
 * of URLs passed as arguments and whether or not specific merge
 * ranges (-c/-r) are specified.
 *
 *                 1 URL                        2 URLs
 * +----+--------------------------------+---------------------+
 * | -c |       mergeinfo-driven         |                     |
 * | or |        cherrypicking           |                     |
 * | -r |    (svn_client_merge_peg)      |                     |
 * |----+--------------------------------+                     |
 * |    |       mergeinfo-driven         |     unsupported     |
 * |    |  'cherry harvest', i.e. merge  |                     |
 * |    |  all revisions from URL that   |                     |
 * | no |  have not already been merged  |                     |
 * | -c |    (svn_client_merge_peg)      |                     |
 * | or +--------------------------------+---------------------+
 * | -r |      mergeinfo-driven          |   mergeinfo-writing |
 * |    |        whole-branch            |    diff-and-apply   |
 * |    |       heuristic merge          |  (svn_client_merge) |
 * |    | (svn_client_merge_reintegrate) |                     |
 * +----+--------------------------------+---------------------+
 *
 *
 */

/* THE CHILDREN_WITH_MERGEINFO ARRAY
 *
 * Many of the helper functions in this file pass around an
 * apr_array_header_t *CHILDREN_WITH_MERGEINFO.  This is a depth first
 * sorted array filled with svn_client__merge_path_t * describing the
 * merge target and any of its subtrees which have explicit mergeinfo
 * or otherwise need special attention during a merge.
 *
 * During mergeinfo unaware merges, CHILDREN_WITH_MERGEINFO contains
 * contains only one element (added by do_mergeinfo_unaware_dir_merge)
 * describing a contiguous range to be merged to the WC merge target.
 *
 * During mergeinfo aware merges CHILDREN_WITH_MERGEINFO is created
 * by get_mergeinfo_paths() and outside of that function and its helpers
 * should always meet the criteria dictated in get_mergeinfo_paths()'s doc
 * string.  The elements of CHILDREN_WITH_MERGEINFO should never be NULL.
 *
 * For clarification on mergeinfo aware vs. mergeinfo unaware merges, see
 * the doc string for HONOR_MERGEINFO().
 */


/*-----------------------------------------------------------------------*/

/*** Repos-Diff Editor Callbacks ***/

typedef struct merge_cmd_baton_t {
  svn_boolean_t force_delete;         /* Delete a file/dir even if modified */
  svn_boolean_t dry_run;
  svn_boolean_t record_only;          /* Whether to merge only mergeinfo
                                         differences. */
  svn_boolean_t same_repos;           /* Whether the merge source repository
                                         is the same repository as the
                                         target.  Defaults to FALSE if DRY_RUN
                                         is TRUE.*/
  svn_boolean_t mergeinfo_capable;    /* Whether the merge source server
                                         is capable of Merge Tracking. */
  svn_boolean_t ignore_mergeinfo;     /* Don't honor mergeinfo; see
                                         doc string of do_merge().  FALSE if
                                         MERGE_SOURCE->ancestral is FALSE. */
  svn_boolean_t diff_ignore_ancestry; /* Diff unrelated nodes as if related; see
                                         doc string of do_merge().  FALSE if
                                         MERGE_SOURCE->ancestral is FALSE. */
  svn_boolean_t reintegrate_merge;    /* Whether this is a --reintegrate
                                         merge or not. */
  const merge_target_t *target;       /* Description of merge target node */

  /* The left and right URLs and revs.  The value of this field changes to
     reflect the merge_source_t *currently* being merged by do_merge(). */
  merge_source_t merge_source;

  /* Rangelist containing single range which describes the gap, if any,
     in the natural history of the merge source currently being processed.
     See http://subversion.tigris.org/issues/show_bug.cgi?id=3432.
     Updated during each call to do_directory_merge().  May be NULL if there
     is no gap. */
  svn_rangelist_t *implicit_src_gap;

  svn_client_ctx_t *ctx;              /* Client context for callbacks, etc. */

  /* The list of any paths which remained in conflict after a
     resolution attempt was made.  We track this in-memory, rather
     than just using WC entry state, since the latter doesn't help us
     when in dry_run mode.
     ### And because we only want to resolve conflicts that were
         generated by this merge, not pre-existing ones? */
  apr_hash_t *conflicted_paths;

  /* A list of absolute paths which had no explicit mergeinfo prior to the
     merge but got explicit mergeinfo added by the merge.  This is populated
     by merge_change_props() and is allocated in POOL so it is subject to the
     lifetime limitations of POOL.  Is NULL if no paths are found which
     meet the criteria or DRY_RUN is true. */
  apr_hash_t *paths_with_new_mergeinfo;

  /* A list of absolute paths whose mergeinfo doesn't need updating after
     the merge. This can be caused by the removal of mergeinfo by the merge
     or by deleting the node itself.  This is populated by merge_change_props()
     and the delete callbacks and is allocated in POOL so it is subject to the
     lifetime limitations of POOL.  Is NULL if no paths are found which
     meet the criteria or DRY_RUN is true. */
  apr_hash_t *paths_with_deleted_mergeinfo;

  /* The list of absolute skipped paths, which should be examined and
     cleared after each invocation of the callback.  The paths
     are absolute.  Is NULL if MERGE_B->MERGE_SOURCE->ancestral and
     MERGE_B->REINTEGRATE_MERGE are both false. */
  apr_hash_t *skipped_abspaths;

  /* The list of absolute merged paths.  Unused if MERGE_B->MERGE_SOURCE->ancestral
     and MERGE_B->REINTEGRATE_MERGE are both false. */
  apr_hash_t *merged_abspaths;

  /* A hash of (const char *) absolute WC paths mapped to the same which
     represent the roots of subtrees added by the merge. */
  apr_hash_t *added_abspaths;

  /* A list of tree conflict victim absolute paths which may be NULL. */
  apr_hash_t *tree_conflicted_abspaths;

  /* The diff3_cmd in ctx->config, if any, else null.  We could just
     extract this as needed, but since more than one caller uses it,
     we just set it up when this baton is created. */
  const char *diff3_cmd;
  const apr_array_header_t *merge_options;

  /* Array of file extension patterns to preserve as extensions in
     generated conflict files. */
  const apr_array_header_t *ext_patterns;

  /* RA sessions used throughout a merge operation.  Opened/re-parented
     as needed.

     NOTE: During the actual merge editor drive, RA_SESSION1 is used
     for the primary editing and RA_SESSION2 for fetching additional
     information -- as necessary -- from the repository.  So during
     this phase of the merge, you *must not* reparent RA_SESSION1; use
     (temporarily reparenting if you must) RA_SESSION2 instead.  */
  svn_ra_session_t *ra_session1;
  svn_ra_session_t *ra_session2;

  /* During the merge, *USE_SLEEP is set to TRUE if a sleep will be required
     afterwards to ensure timestamp integrity, or unchanged if not. */
  svn_boolean_t *use_sleep;

  /* Pool which has a lifetime limited to one iteration over a given
     merge source, i.e. it is cleared on every call to do_directory_merge()
     or do_file_merge() in do_merge(). */
  apr_pool_t *pool;


  /* State for notify_merge_begin() */
  struct notify_begin_state_t
  {
    /* Cache of which abspath was last notified. */
    const char *last_abspath;

    /* Reference to the one-and-only CHILDREN_WITH_MERGEINFO (see global
       comment) or a similar list for single-file-merges */
    const apr_array_header_t *nodes_with_mergeinfo;
  } notify_begin;

} merge_cmd_baton_t;


/* Return TRUE iff we should be taking account of mergeinfo in deciding what
   changes to merge, for the merge described by MERGE_B.  Specifically, that
   is if the merge source server is capable of merge tracking, the left-side
   merge source is an ancestor of the right-side (or vice-versa), the merge
   source is in the same repository as the merge target, and we are not
   ignoring mergeinfo. */
#define HONOR_MERGEINFO(merge_b) ((merge_b)->mergeinfo_capable      \
                                  && (merge_b)->merge_source.ancestral  \
                                  && (merge_b)->same_repos          \
                                  && (! (merge_b)->ignore_mergeinfo))


/* Return TRUE iff we should be recording mergeinfo for the merge described
   by MERGE_B.  Specifically, that is if we are honoring mergeinfo and the
   merge is not a dry run.  */
#define RECORD_MERGEINFO(merge_b) (HONOR_MERGEINFO(merge_b) \
                                   && !(merge_b)->dry_run)


/*-----------------------------------------------------------------------*/

/*** Utilities ***/

/* Return TRUE iff the session URL of RA_SESSION is equal to URL.  Useful in
 * asserting preconditions. */
static svn_boolean_t
session_url_is(svn_ra_session_t *ra_session,
               const char *url,
               apr_pool_t *scratch_pool)
{
  const char *session_url;
  svn_error_t *err
    = svn_ra_get_session_url(ra_session, &session_url, scratch_pool);

  SVN_ERR_ASSERT_NO_RETURN(! err);
  return strcmp(url, session_url) == 0;
}

/* Return a new merge_source_t structure, allocated in RESULT_POOL,
 * initialized with deep copies of LOC1 and LOC2 and ANCESTRAL. */
static merge_source_t *
merge_source_create(const svn_client__pathrev_t *loc1,
                    const svn_client__pathrev_t *loc2,
                    svn_boolean_t ancestral,
                    apr_pool_t *result_pool)
{
  merge_source_t *s
    = apr_palloc(result_pool, sizeof(*s));

  s->loc1 = svn_client__pathrev_dup(loc1, result_pool);
  s->loc2 = svn_client__pathrev_dup(loc2, result_pool);
  s->ancestral = ancestral;
  return s;
}

/* Return a deep copy of SOURCE, allocated in RESULT_POOL. */
static merge_source_t *
merge_source_dup(const merge_source_t *source,
                 apr_pool_t *result_pool)
{
  merge_source_t *s = apr_palloc(result_pool, sizeof(*s));

  s->loc1 = svn_client__pathrev_dup(source->loc1, result_pool);
  s->loc2 = svn_client__pathrev_dup(source->loc2, result_pool);
  s->ancestral = source->ancestral;
  return s;
}

/* Return SVN_ERR_UNSUPPORTED_FEATURE if URL is not inside the repository
   of LOCAL_ABSPATH.  Use SCRATCH_POOL for temporary allocations. */
static svn_error_t *
check_repos_match(const merge_target_t *target,
                  const char *local_abspath,
                  const char *url,
                  apr_pool_t *scratch_pool)
{
  if (!svn_uri__is_ancestor(target->loc.repos_root_url, url))
    return svn_error_createf(
        SVN_ERR_UNSUPPORTED_FEATURE, NULL,
         _("URL '%s' of '%s' is not in repository '%s'"),
         url, svn_dirent_local_style(local_abspath, scratch_pool),
         target->loc.repos_root_url);

  return SVN_NO_ERROR;
}

/* Return TRUE iff the repository of LOCATION1 is the same as
 * that of LOCATION2.  If STRICT_URLS is true, the URLs must
 * match (and the UUIDs, just to be sure), otherwise just the UUIDs must
 * match and the URLs can differ (a common case is http versus https). */
static svn_boolean_t
is_same_repos(const svn_client__pathrev_t *location1,
              const svn_client__pathrev_t *location2,
              svn_boolean_t strict_urls)
{
  if (strict_urls)
    return (strcmp(location1->repos_root_url, location2->repos_root_url) == 0
            && strcmp(location1->repos_uuid, location2->repos_uuid) == 0);
  else
    return (strcmp(location1->repos_uuid, location2->repos_uuid) == 0);
}

/* If the repository identified of LOCATION1 is not the same as that
 * of LOCATION2, throw a SVN_ERR_CLIENT_UNRELATED_RESOURCES
 * error mentioning PATH1 and PATH2. For STRICT_URLS, see is_same_repos().
 */
static svn_error_t *
check_same_repos(const svn_client__pathrev_t *location1,
                 const char *path1,
                 const svn_client__pathrev_t *location2,
                 const char *path2,
                 svn_boolean_t strict_urls,
                 apr_pool_t *scratch_pool)
{
  if (! is_same_repos(location1, location2, strict_urls))
    return svn_error_createf(SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
                             _("'%s' must be from the same repository as "
                               "'%s'"), path1, path2);
  return SVN_NO_ERROR;
}

/* Store LOCAL_ABSPATH in PATH_HASH after duplicating it into the pool
   containing PATH_HASH. */
static APR_INLINE void
store_path(apr_hash_t *path_hash, const char *local_abspath)
{
  const char *dup_path = apr_pstrdup(apr_hash_pool_get(path_hash),
                                     local_abspath);

  svn_hash_sets(path_hash, dup_path, dup_path);
}

/* Store LOCAL_ABSPATH in *PATH_HASH_P after duplicating it into the pool
   containing *PATH_HASH_P.  If *PATH_HASH_P is NULL, then first set
   *PATH_HASH_P to a new hash allocated from POOL.  */
static APR_INLINE void
alloc_and_store_path(apr_hash_t **path_hash_p,
                     const char *local_abspath,
                     apr_pool_t *pool)
{
  if (! *path_hash_p)
    *path_hash_p = apr_hash_make(pool);
  store_path(*path_hash_p, local_abspath);
}

/* Return whether any WC path was put in conflict by the merge
   operation corresponding to MERGE_B. */
static APR_INLINE svn_boolean_t
is_path_conflicted_by_merge(merge_cmd_baton_t *merge_b)
{
  return (merge_b->conflicted_paths &&
          apr_hash_count(merge_b->conflicted_paths) > 0);
}

/* Return a state indicating whether the WC metadata matches the
 * node kind on disk of the local path LOCAL_ABSPATH.
 * Use MERGE_B to determine the dry-run details; particularly, if a dry run
 * noted that it deleted this path, assume matching node kinds (as if both
 * kinds were svn_node_none).
 *
 *   - Return svn_wc_notify_state_inapplicable if the node kind matches.
 *   - Return 'obstructed' if there is a node on disk where none or a
 *     different kind is expected, or if the disk node cannot be read.
 *   - Return 'missing' if there is no node on disk but one is expected.
 *     Also return 'missing' for server-excluded nodes (not here due to
 *     authz or other reasons determined by the server).
 *
 * Optionally return a bit more info for interested users.
 **/
static svn_error_t *
perform_obstruction_check(svn_wc_notify_state_t *obstruction_state,
                          svn_boolean_t *deleted,
                          svn_boolean_t *excluded,
                          svn_node_kind_t *kind,
                          svn_depth_t *parent_depth,
                          const merge_cmd_baton_t *merge_b,
                          const char *local_abspath,
                          apr_pool_t *scratch_pool)
{
  svn_wc_context_t *wc_ctx = merge_b->ctx->wc_ctx;
  svn_node_kind_t wc_kind;
  svn_boolean_t check_root;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  *obstruction_state = svn_wc_notify_state_inapplicable;

  if (deleted)
    *deleted = FALSE;
  if (kind)
    *kind = svn_node_none;

  if (kind == NULL)
    kind = &wc_kind;

  check_root = ! strcmp(local_abspath, merge_b->target->abspath);

  SVN_ERR(svn_wc__check_for_obstructions(obstruction_state,
                                         kind,
                                         deleted,
                                         excluded,
                                         parent_depth,
                                         wc_ctx, local_abspath,
                                         check_root,
                                         scratch_pool));
  return SVN_NO_ERROR;
}

/* Create *LEFT and *RIGHT conflict versions for conflict victim
 * at VICTIM_ABSPATH, with merge-left node kind MERGE_LEFT_NODE_KIND
 * and merge-right node kind MERGE_RIGHT_NODE_KIND, using information
 * obtained from MERGE_SOURCE and TARGET.
 * Allocate returned conflict versions in RESULT_POOL. */
static svn_error_t *
make_conflict_versions(const svn_wc_conflict_version_t **left,
                       const svn_wc_conflict_version_t **right,
                       const char *victim_abspath,
                       svn_node_kind_t merge_left_node_kind,
                       svn_node_kind_t merge_right_node_kind,
                       const merge_source_t *merge_source,
                       const merge_target_t *target,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  const char *child = svn_dirent_skip_ancestor(target->abspath,
                                               victim_abspath);
  const char *left_relpath, *right_relpath;

  SVN_ERR_ASSERT(child != NULL);
  left_relpath = svn_client__pathrev_relpath(merge_source->loc1,
                                             scratch_pool);
  right_relpath = svn_client__pathrev_relpath(merge_source->loc2,
                                              scratch_pool);

  *left = svn_wc_conflict_version_create2(
            merge_source->loc1->repos_root_url,
            merge_source->loc1->repos_uuid,
            svn_relpath_join(left_relpath, child, scratch_pool),
            merge_source->loc1->rev,
            merge_left_node_kind, result_pool);

  *right = svn_wc_conflict_version_create2(
             merge_source->loc2->repos_root_url,
             merge_source->loc2->repos_uuid,
             svn_relpath_join(right_relpath, child, scratch_pool),
             merge_source->loc2->rev,
             merge_right_node_kind, result_pool);

  return SVN_NO_ERROR;
}

/* Helper for filter_self_referential_mergeinfo()

   *MERGEINFO is a non-empty, non-null collection of mergeinfo.

   Remove all mergeinfo from *MERGEINFO that describes revision ranges
   greater than REVISION.  Put a copy of any removed mergeinfo, allocated
   in POOL, into *YOUNGER_MERGEINFO.

   If no mergeinfo is removed from *MERGEINFO then *YOUNGER_MERGEINFO is set
   to NULL.  If all mergeinfo is removed from *MERGEINFO then *MERGEINFO is
   set to NULL.
   */
static svn_error_t*
split_mergeinfo_on_revision(svn_mergeinfo_t *younger_mergeinfo,
                            svn_mergeinfo_t *mergeinfo,
                            svn_revnum_t revision,
                            apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(pool);

  *younger_mergeinfo = NULL;
  for (hi = apr_hash_first(pool, *mergeinfo); hi; hi = apr_hash_next(hi))
    {
      int i;
      const char *merge_source_path = apr_hash_this_key(hi);
      svn_rangelist_t *rangelist = apr_hash_this_val(hi);

      svn_pool_clear(iterpool);

      for (i = 0; i < rangelist->nelts; i++)
        {
          svn_merge_range_t *range =
            APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
          if (range->end <= revision)
            {
              /* This entirely of this range is as old or older than
                 REVISION, so leave it in *MERGEINFO. */
              continue;
            }
          else
            {
              /* Since the rangelists in svn_mergeinfo_t's are sorted in
                 increasing order we know that part or all of *this* range
                 and *all* of the remaining ranges in *RANGELIST are younger
                 than REVISION.  Remove the younger rangelists from
                 *MERGEINFO and put them in *YOUNGER_MERGEINFO. */
              int j;
              svn_rangelist_t *younger_rangelist =
                apr_array_make(pool, 1, sizeof(svn_merge_range_t *));

              for (j = i; j < rangelist->nelts; j++)
                {
                  svn_merge_range_t *younger_range = svn_merge_range_dup(
                    APR_ARRAY_IDX(rangelist, j, svn_merge_range_t *), pool);

                  /* REVISION might intersect with the first range where
                     range->end > REVISION.  If that is the case then split
                     the current range into two, putting the younger half
                     into *YOUNGER_MERGEINFO and leaving the older half in
                     *MERGEINFO. */
                  if (j == i && range->start + 1 <= revision)
                    younger_range->start = range->end = revision;

                  APR_ARRAY_PUSH(younger_rangelist, svn_merge_range_t *) =
                    younger_range;
                }

              /* So far we've only been manipulating rangelists, now we
                 actually create *YOUNGER_MERGEINFO and then remove the older
                 ranges from *MERGEINFO */
              if (!(*younger_mergeinfo))
                *younger_mergeinfo = apr_hash_make(pool);
              svn_hash_sets(*younger_mergeinfo, merge_source_path,
                            younger_rangelist);
              SVN_ERR(svn_mergeinfo_remove2(mergeinfo, *younger_mergeinfo,
                                            *mergeinfo, TRUE, pool, iterpool));
              break; /* ...out of for (i = 0; i < rangelist->nelts; i++) */
            }
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Make a copy of PROPCHANGES (array of svn_prop_t) into *TRIMMED_PROPCHANGES,
   omitting any svn:mergeinfo changes.  */
static svn_error_t *
omit_mergeinfo_changes(apr_array_header_t **trimmed_propchanges,
                       const apr_array_header_t *propchanges,
                       apr_pool_t *result_pool)
{
  int i;

  *trimmed_propchanges = apr_array_make(result_pool,
                                        propchanges->nelts,
                                        sizeof(svn_prop_t));

  for (i = 0; i < propchanges->nelts; ++i)
    {
      const svn_prop_t *change = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);

      /* If this property is not svn:mergeinfo, then copy it.  */
      if (strcmp(change->name, SVN_PROP_MERGEINFO) != 0)
        APR_ARRAY_PUSH(*trimmed_propchanges, svn_prop_t) = *change;
    }

  return SVN_NO_ERROR;
}


/* Helper for merge_props_changed().

   *PROPS is an array of svn_prop_t structures representing regular properties
   to be added to the working copy TARGET_ABSPATH.

   The merge source and target are assumed to be in the same repository.

   Filter out mergeinfo property additions to TARGET_ABSPATH when
   those additions refer to the same line of history as TARGET_ABSPATH as
   described below.

   Examine the added mergeinfo, looking at each range (or single rev)
   of each source path.  If a source_path/range refers to the same line of
   history as TARGET_ABSPATH (pegged at its base revision), then filter out
   that range.  If the entire rangelist for a given path is filtered then
   filter out the path as well.

   RA_SESSION is an open RA session to the repository
   in which both the source and target live, else RA_SESSION is not used. It
   may be temporarily reparented as needed by this function.

   Use CTX for any further client operations.

   If any filtering occurs, set outgoing *PROPS to a shallow copy (allocated
   in POOL) of incoming *PROPS minus the filtered mergeinfo. */
static svn_error_t *
filter_self_referential_mergeinfo(apr_array_header_t **props,
                                  const char *target_abspath,
                                  svn_ra_session_t *ra_session,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *pool)
{
  apr_array_header_t *adjusted_props;
  int i;
  apr_pool_t *iterpool;
  svn_boolean_t is_copy;
  const char *repos_relpath;
  svn_client__pathrev_t target_base;

  /* If PATH itself has been added there is no need to filter. */
  SVN_ERR(svn_wc__node_get_origin(&is_copy,  &target_base.rev, &repos_relpath,
                                  &target_base.repos_root_url,
                                  &target_base.repos_uuid, NULL, NULL,
                                  ctx->wc_ctx, target_abspath, FALSE,
                                  pool, pool));

  if (is_copy || !repos_relpath)
    return SVN_NO_ERROR; /* A copy or a local addition */

  target_base.url = svn_path_url_add_component2(target_base.repos_root_url,
                                                repos_relpath, pool);

  adjusted_props = apr_array_make(pool, (*props)->nelts, sizeof(svn_prop_t));
  iterpool = svn_pool_create(pool);
  for (i = 0; i < (*props)->nelts; ++i)
    {
      svn_prop_t *prop = &APR_ARRAY_IDX((*props), i, svn_prop_t);

      svn_mergeinfo_t mergeinfo, younger_mergeinfo;
      svn_mergeinfo_t filtered_mergeinfo = NULL;
      svn_mergeinfo_t filtered_younger_mergeinfo = NULL;
      svn_error_t *err;

      /* If this property isn't mergeinfo or is NULL valued (i.e. prop removal)
         or empty mergeinfo it does not require any special handling.  There
         is nothing to filter out of empty mergeinfo and the concept of
         filtering doesn't apply if we are trying to remove mergeinfo
         entirely.  */
      if ((strcmp(prop->name, SVN_PROP_MERGEINFO) != 0)
          || (! prop->value)       /* Removal of mergeinfo */
          || (! prop->value->len)) /* Empty mergeinfo */
        {
          APR_ARRAY_PUSH(adjusted_props, svn_prop_t) = *prop;
          continue;
        }

      svn_pool_clear(iterpool);

      /* Non-empty mergeinfo; filter self-referential mergeinfo out. */

      /* Parse the incoming mergeinfo to allow easier manipulation. */
      err = svn_mergeinfo_parse(&mergeinfo, prop->value->data, iterpool);

      if (err)
        {
          /* Issue #3896: If we can't parse it, we certainly can't
             filter it. */
          if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
            {
              svn_error_clear(err);
              APR_ARRAY_PUSH(adjusted_props, svn_prop_t) = *prop;
              continue;
            }
          else
            {
              return svn_error_trace(err);
            }
        }

      /* The working copy target PATH is at BASE_REVISION.  Divide the
         incoming mergeinfo into two groups.  One where all revision ranges
         are as old or older than BASE_REVISION and one where all revision
         ranges are younger.

         Note: You may be wondering why we do this.

         For the incoming mergeinfo "older" than target's base revision we
         can filter out self-referential mergeinfo efficiently using
         svn_client__get_history_as_mergeinfo().  We simply look at PATH's
         natural history as mergeinfo and remove that from any incoming
         mergeinfo.

         For mergeinfo "younger" than the base revision we can't use
         svn_ra_get_location_segments() to look into PATH's future
         history.  Instead we must use svn_client__repos_locations() and
         look at each incoming source/range individually and see if PATH
         at its base revision and PATH at the start of the incoming range
         exist on the same line of history.  If they do then we can filter
         out the incoming range.  But since we have to do this for each
         range there is a substantial performance penalty to pay if the
         incoming ranges are not contiguous, i.e. we call
         svn_client__repos_locations for each discrete range and incur
         the cost of a roundtrip communication with the repository. */
      SVN_ERR(split_mergeinfo_on_revision(&younger_mergeinfo,
                                          &mergeinfo,
                                          target_base.rev,
                                          iterpool));

      /* Filter self-referential mergeinfo from younger_mergeinfo. */
      if (younger_mergeinfo)
        {
          apr_hash_index_t *hi;
          const char *merge_source_root_url;

          SVN_ERR(svn_ra_get_repos_root2(ra_session,
                                         &merge_source_root_url, iterpool));

          for (hi = apr_hash_first(iterpool, younger_mergeinfo);
               hi; hi = apr_hash_next(hi))
            {
              int j;
              const char *source_path = apr_hash_this_key(hi);
              svn_rangelist_t *rangelist = apr_hash_this_val(hi);
              const char *merge_source_url;
              svn_rangelist_t *adjusted_rangelist =
                apr_array_make(iterpool, 0, sizeof(svn_merge_range_t *));

              merge_source_url =
                    svn_path_url_add_component2(merge_source_root_url,
                                                source_path + 1, iterpool);

              for (j = 0; j < rangelist->nelts; j++)
                {
                  svn_error_t *err2;
                  svn_client__pathrev_t *start_loc;
                  svn_merge_range_t *range =
                    APR_ARRAY_IDX(rangelist, j, svn_merge_range_t *);

                  /* Because the merge source normalization code
                     ensures mergeinfo refers to real locations on
                     the same line of history, there's no need to
                     look at the whole range, just the start. */

                  /* Check if PATH@BASE_REVISION exists at
                     RANGE->START on the same line of history.
                     (start+1 because RANGE->start is not inclusive.) */
                  err2 = svn_client__repos_location(&start_loc, ra_session,
                                                    &target_base,
                                                    range->start + 1,
                                                    ctx, iterpool, iterpool);
                  if (err2)
                    {
                      if (err2->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES
                          || err2->apr_err == SVN_ERR_FS_NOT_FOUND
                          || err2->apr_err == SVN_ERR_FS_NO_SUCH_REVISION)
                        {
                          /* PATH@BASE_REVISION didn't exist at
                             RANGE->START + 1 or is unrelated to the
                             resource PATH@RANGE->START.  Some of the
                             requested revisions may not even exist in
                             the repository; a real possibility since
                             mergeinfo is hand editable.  In all of these
                             cases clear and ignore the error and don't
                             do any filtering.

                             Note: In this last case it is possible that
                             we will allow self-referential mergeinfo to
                             be applied, but fixing it here is potentially
                             very costly in terms of finding what part of
                             a range is actually valid.  Simply allowing
                             the merge to proceed without filtering the
                             offending range seems the least worst
                             option. */
                          svn_error_clear(err2);
                          err2 = NULL;
                          APR_ARRAY_PUSH(adjusted_rangelist,
                                         svn_merge_range_t *) = range;
                        }
                      else
                        {
                          return svn_error_trace(err2);
                        }
                     }
                  else
                    {
                      /* PATH@BASE_REVISION exists on the same
                         line of history at RANGE->START and RANGE->END.
                         Now check that PATH@BASE_REVISION's path
                         names at RANGE->START and RANGE->END are the same.
                         If the names are not the same then the mergeinfo
                         describing PATH@RANGE->START through
                         PATH@RANGE->END actually belong to some other
                         line of history and we want to record this
                         mergeinfo, not filter it. */
                      if (strcmp(start_loc->url, merge_source_url) != 0)
                        {
                          APR_ARRAY_PUSH(adjusted_rangelist,
                                         svn_merge_range_t *) = range;
                        }
                    }
                    /* else no need to add, this mergeinfo is
                       all on the same line of history. */
                } /* for (j = 0; j < rangelist->nelts; j++) */

              /* Add any rangelists for source_path that are not
                 self-referential. */
              if (adjusted_rangelist->nelts)
                {
                  if (!filtered_younger_mergeinfo)
                    filtered_younger_mergeinfo = apr_hash_make(iterpool);
                  svn_hash_sets(filtered_younger_mergeinfo, source_path,
                                adjusted_rangelist);
                }

            } /* Iteration over each merge source in younger_mergeinfo. */
        } /* if (younger_mergeinfo) */

      /* Filter self-referential mergeinfo from "older" mergeinfo. */
      if (mergeinfo)
        {
          svn_mergeinfo_t implicit_mergeinfo;

          SVN_ERR(svn_client__get_history_as_mergeinfo(
            &implicit_mergeinfo, NULL,
            &target_base, target_base.rev, SVN_INVALID_REVNUM,
            ra_session, ctx, iterpool));

          /* Remove PATH's implicit mergeinfo from the incoming mergeinfo. */
          SVN_ERR(svn_mergeinfo_remove2(&filtered_mergeinfo,
                                        implicit_mergeinfo,
                                        mergeinfo, TRUE, iterpool, iterpool));
        }

      /* Combine whatever older and younger filtered mergeinfo exists
         into filtered_mergeinfo. */
      if (filtered_mergeinfo && filtered_younger_mergeinfo)
        SVN_ERR(svn_mergeinfo_merge2(filtered_mergeinfo,
                                     filtered_younger_mergeinfo, iterpool,
                                     iterpool));
      else if (filtered_younger_mergeinfo)
        filtered_mergeinfo = filtered_younger_mergeinfo;

      /* If there is any incoming mergeinfo remaining after filtering
         then put it in adjusted_props. */
      if (filtered_mergeinfo && apr_hash_count(filtered_mergeinfo))
        {
          /* Convert filtered_mergeinfo to a svn_prop_t and put it
             back in the array. */
          svn_string_t *filtered_mergeinfo_str;
          svn_prop_t *adjusted_prop = apr_pcalloc(pool,
                                                  sizeof(*adjusted_prop));
          SVN_ERR(svn_mergeinfo_to_string(&filtered_mergeinfo_str,
                                          filtered_mergeinfo,
                                          pool));
          adjusted_prop->name = SVN_PROP_MERGEINFO;
          adjusted_prop->value = filtered_mergeinfo_str;
          APR_ARRAY_PUSH(adjusted_props, svn_prop_t) = *adjusted_prop;
        }
    }
  svn_pool_destroy(iterpool);

  *props = adjusted_props;
  return SVN_NO_ERROR;
}

/* Prepare a set of property changes PROPCHANGES to be used for a merge
   operation on LOCAL_ABSPATH.

   Remove all non-regular prop-changes (entry-props and WC-props).
   Remove all non-mergeinfo prop-changes if it's a record-only merge.
   Remove self-referential mergeinfo (### in some cases...)
   Remove foreign-repository mergeinfo (### in some cases...)

   Store the resulting property changes in *PROP_UPDATES.
   Store information on where mergeinfo is updated in MERGE_B.

   Used for both file and directory property merges. */
static svn_error_t *
prepare_merge_props_changed(const apr_array_header_t **prop_updates,
                            const char *local_abspath,
                            const apr_array_header_t *propchanges,
                            merge_cmd_baton_t *merge_b,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  apr_array_header_t *props;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* We only want to merge "regular" version properties:  by
     definition, 'svn merge' shouldn't touch any data within .svn/  */
  SVN_ERR(svn_categorize_props(propchanges, NULL, NULL, &props,
                               result_pool));

  /* If we are only applying mergeinfo changes then we need to do
     additional filtering of PROPS so it contains only mergeinfo changes. */
  if (merge_b->record_only && props->nelts)
    {
      apr_array_header_t *mergeinfo_props =
        apr_array_make(result_pool, 1, sizeof(svn_prop_t));
      int i;

      for (i = 0; i < props->nelts; i++)
        {
          svn_prop_t *prop = &APR_ARRAY_IDX(props, i, svn_prop_t);

          if (strcmp(prop->name, SVN_PROP_MERGEINFO) == 0)
            {
              APR_ARRAY_PUSH(mergeinfo_props, svn_prop_t) = *prop;
              break;
            }
        }
      props = mergeinfo_props;
    }

  if (props->nelts)
    {
      /* Issue #3383: We don't want mergeinfo from a foreign repos.

         If this is a merge from a foreign repository we must strip all
         incoming mergeinfo (including mergeinfo deletions). */
      if (! merge_b->same_repos)
        SVN_ERR(omit_mergeinfo_changes(&props, props, result_pool));

      /* If this is a forward merge then don't add new mergeinfo to
         PATH that is already part of PATH's own history, see
         http://svn.haxx.se/dev/archive-2008-09/0006.shtml.  If the
         merge sources are not ancestral then there is no concept of a
         'forward' or 'reverse' merge and we filter unconditionally. */
      if (merge_b->merge_source.loc1->rev < merge_b->merge_source.loc2->rev
          || !merge_b->merge_source.ancestral)
        {
          if (HONOR_MERGEINFO(merge_b) || merge_b->reintegrate_merge)
            SVN_ERR(filter_self_referential_mergeinfo(&props,
                                                      local_abspath,
                                                      merge_b->ra_session2,
                                                      merge_b->ctx,
                                                      result_pool));
        }
    }
  *prop_updates = props;

  /* Make a record in BATON if we find a PATH where mergeinfo is added
     where none existed previously or PATH is having its existing
     mergeinfo deleted. */
  if (props->nelts)
    {
      int i;

      for (i = 0; i < props->nelts; ++i)
        {
          svn_prop_t *prop = &APR_ARRAY_IDX(props, i, svn_prop_t);

          if (strcmp(prop->name, SVN_PROP_MERGEINFO) == 0)
            {
              /* Does LOCAL_ABSPATH have any pristine mergeinfo? */
              svn_boolean_t has_pristine_mergeinfo = FALSE;
              apr_hash_t *pristine_props;

              SVN_ERR(svn_wc_get_pristine_props(&pristine_props,
                                                merge_b->ctx->wc_ctx,
                                                local_abspath,
                                                scratch_pool,
                                                scratch_pool));

              if (pristine_props
                  && svn_hash_gets(pristine_props, SVN_PROP_MERGEINFO))
                has_pristine_mergeinfo = TRUE;

              if (!has_pristine_mergeinfo && prop->value)
                {
                  alloc_and_store_path(&merge_b->paths_with_new_mergeinfo,
                                       local_abspath, merge_b->pool);
                }
              else if (has_pristine_mergeinfo && !prop->value)
                {
                  alloc_and_store_path(&merge_b->paths_with_deleted_mergeinfo,
                                       local_abspath, merge_b->pool);
                }
            }
        }
    }

  return SVN_NO_ERROR;
}

#define CONFLICT_REASON_NONE       ((svn_wc_conflict_reason_t)-1)
#define CONFLICT_REASON_SKIP       ((svn_wc_conflict_reason_t)-2)
#define CONFLICT_REASON_SKIP_WC    ((svn_wc_conflict_reason_t)-3)

/* Baton used for testing trees for being editted while performing tree
   conflict detection for incoming deletes */
struct dir_delete_baton_t
{
  /* Reference to dir baton of directory that is the root of the deletion */
  struct merge_dir_baton_t *del_root;

  /* Boolean indicating that some edit is found. Allows avoiding more work */
  svn_boolean_t found_edit;

  /* A list of paths that are compared. Kept up to date until FOUND_EDIT is
     set to TRUE */
  apr_hash_t *compared_abspaths;
};

/* Baton for the merge_dir_*() functions. Initialized in merge_dir_opened() */
struct merge_dir_baton_t
{
  /* Reference to the parent baton, unless the parent is the anchor, in which
     case PARENT_BATON is NULL */
  struct merge_dir_baton_t *parent_baton;

  /* The pool containing this baton. Use for RESULT_POOL for storing in this
     baton */
  apr_pool_t *pool;

  /* This directory doesn't have a representation in the working copy, so any
     operation on it will be skipped and possibly cause a tree conflict on the
     shadow root */
  svn_boolean_t shadowed;

  /* This node or one of its descendants received operational changes from the
     merge. If this node is the shadow root its tree conflict status has been
     applied */
  svn_boolean_t edited;

  /* If a tree conflict will be installed once edited, it's reason. If a skip
     should be produced its reason. Otherwise CONFLICT_REASON_NONE for no tree
     conflict.

     Special values:
       CONFLICT_REASON_SKIP:
            The node will be skipped with content and property state as stored in
            SKIP_REASON.

       CONFLICT_REASON_SKIP_WC:
            The node will be skipped as an obstructing working copy.
   */
  svn_wc_conflict_reason_t tree_conflict_reason;
  svn_wc_conflict_action_t tree_conflict_action;
  svn_node_kind_t tree_conflict_local_node_kind;
  svn_node_kind_t tree_conflict_merge_left_node_kind;
  svn_node_kind_t tree_conflict_merge_right_node_kind;

  /* When TREE_CONFLICT_REASON is CONFLICT_REASON_SKIP, the skip state to
     add to the notification */
  svn_wc_notify_state_t skip_reason;

  /* TRUE if the node was added by this merge. Otherwise FALSE */
  svn_boolean_t added;
  svn_boolean_t add_is_replace; /* Add is second part of replace */

  /* TRUE if we are taking over an existing directory as addition, otherwise
     FALSE. */
  svn_boolean_t add_existing;

  /* NULL, or an hashtable mapping const char * local_abspaths to
     const char *kind mapping, containing deleted nodes that still need a delete
     notification (which may be a replaced notification if the node is not just
     deleted) */
  apr_hash_t *pending_deletes;

  /* NULL, or an hashtable mapping const char * LOCAL_ABSPATHs to
     a const svn_wc_conflict_description2_t * instance, describing the just
     installed conflict */
  apr_hash_t *new_tree_conflicts;

  /* If not NULL, a reference to the information of the delete test that is
     currently in progress. Allocated in the root-directory baton, referenced
     from all descendants */
  struct dir_delete_baton_t *delete_state;
};

/* Baton for the merge_dir_*() functions. Initialized in merge_file_opened() */
struct merge_file_baton_t
{
  /* Reference to the parent baton, unless the parent is the anchor, in which
     case PARENT_BATON is NULL */
  struct merge_dir_baton_t *parent_baton;

  /* This file doesn't have a representation in the working copy, so any
     operation on it will be skipped and possibly cause a tree conflict
     on the shadow root */
  svn_boolean_t shadowed;

  /* This node received operational changes from the merge. If this node
     is the shadow root its tree conflict status has been applied */
  svn_boolean_t edited;

  /* If a tree conflict will be installed once edited, it's reason. If a skip
     should be produced its reason. Some special values are defined. See the
     merge_dir_baton_t for an explanation. */
  svn_wc_conflict_reason_t tree_conflict_reason;
  svn_wc_conflict_action_t tree_conflict_action;
  svn_node_kind_t tree_conflict_local_node_kind;
  svn_node_kind_t tree_conflict_merge_left_node_kind;
  svn_node_kind_t tree_conflict_merge_right_node_kind;

  /* When TREE_CONFLICT_REASON is CONFLICT_REASON_SKIP, the skip state to
     add to the notification */
  svn_wc_notify_state_t skip_reason;

  /* TRUE if the node was added by this merge. Otherwise FALSE */
  svn_boolean_t added;
  svn_boolean_t add_is_replace; /* Add is second part of replace */
};

/* Forward declaration */
static svn_error_t *
notify_merge_begin(merge_cmd_baton_t *merge_b,
                   const char *local_abspath,
                   svn_boolean_t delete_action,
                   apr_pool_t *scratch_pool);

/* Record the skip for future processing and (later) produce the
   skip notification */
static svn_error_t *
record_skip(merge_cmd_baton_t *merge_b,
            const char *local_abspath,
            svn_node_kind_t kind,
            svn_wc_notify_action_t action,
            svn_wc_notify_state_t state,
            struct merge_dir_baton_t *pdb,
            apr_pool_t *scratch_pool)
{
  if (merge_b->record_only)
    return SVN_NO_ERROR; /* ### Why? - Legacy compatibility */

  if ((merge_b->merge_source.ancestral || merge_b->reintegrate_merge)
      && !(pdb && pdb->shadowed))
    {
      store_path(merge_b->skipped_abspaths, local_abspath);
    }

  if (merge_b->ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      SVN_ERR(notify_merge_begin(merge_b, local_abspath, FALSE, scratch_pool));

      notify = svn_wc_create_notify(local_abspath, action, scratch_pool);
      notify->kind = kind;
      notify->content_state = notify->prop_state = state;

      merge_b->ctx->notify_func2(merge_b->ctx->notify_baton2, notify,
                                 scratch_pool);
    }
  return SVN_NO_ERROR;
}

/* Forward declaration */
static svn_client__merge_path_t *
find_nearest_ancestor_with_intersecting_ranges(
  svn_revnum_t *start,
  svn_revnum_t *end,
  const apr_array_header_t *children_with_mergeinfo,
  svn_boolean_t path_is_own_ancestor,
  const char *local_abspath);

/* Record a tree conflict in the WC, unless this is a dry run or a record-
 * only merge, or if a tree conflict is already flagged for the VICTIM_PATH.
 * (The latter can happen if a merge-tracking-aware merge is doing multiple
 * editor drives because of a gap in the range of eligible revisions.)
 *
 * The tree conflict, with its victim specified by VICTIM_PATH, is
 * assumed to have happened during a merge using merge baton MERGE_B.
 *
 * ACTION and REASON correspond to the fields
 * of the same names in svn_wc_tree_conflict_description_t.
 */
static svn_error_t *
record_tree_conflict(merge_cmd_baton_t *merge_b,
                     const char *local_abspath,
                     struct merge_dir_baton_t *parent_baton,
                     svn_node_kind_t local_node_kind,
                     svn_node_kind_t merge_left_node_kind,
                     svn_node_kind_t merge_right_node_kind,
                     svn_wc_conflict_action_t action,
                     svn_wc_conflict_reason_t reason,
                     const svn_wc_conflict_description2_t *existing_conflict,
                     svn_boolean_t notify_tc,
                     apr_pool_t *scratch_pool)
{
  svn_wc_context_t *wc_ctx = merge_b->ctx->wc_ctx;

  if (merge_b->record_only)
    return SVN_NO_ERROR;

  if (merge_b->merge_source.ancestral
      || merge_b->reintegrate_merge)
    {
      store_path(merge_b->tree_conflicted_abspaths, local_abspath);
    }

  alloc_and_store_path(&merge_b->conflicted_paths, local_abspath,
                       merge_b->pool);

  if (!merge_b->dry_run)
    {
       svn_wc_conflict_description2_t *conflict;
       const svn_wc_conflict_version_t *left;
       const svn_wc_conflict_version_t *right;
       apr_pool_t *result_pool = parent_baton ? parent_baton->pool
                                              : scratch_pool;

      if (reason == svn_wc_conflict_reason_deleted)
        {
          const char *moved_to_abspath;

          SVN_ERR(svn_wc__node_was_moved_away(&moved_to_abspath, NULL,
                                              wc_ctx, local_abspath,
                                              scratch_pool, scratch_pool));

          if (moved_to_abspath)
            {
              /* Local abspath itself has been moved away. If only a
                 descendant is moved away, we call the node itself deleted */
              reason = svn_wc_conflict_reason_moved_away;
            }
        }
      else if (reason == svn_wc_conflict_reason_added)
        {
          const char *moved_from_abspath;
          SVN_ERR(svn_wc__node_was_moved_here(&moved_from_abspath, NULL,
                                              wc_ctx, local_abspath,
                                              scratch_pool, scratch_pool));
          if (moved_from_abspath)
            reason = svn_wc_conflict_reason_moved_here;
        }

      if (HONOR_MERGEINFO(merge_b) && merge_b->merge_source.ancestral)
        {
          struct merge_source_t *source;
          svn_client__pathrev_t *loc1;
          svn_client__pathrev_t *loc2;
          svn_merge_range_t range =
            {SVN_INVALID_REVNUM, SVN_INVALID_REVNUM, TRUE};

          /* We are honoring mergeinfo so do not blindly record
           * a conflict describing the merge of
           * SOURCE->LOC1->URL@SOURCE->LOC1->REV through
           * SOURCE->LOC2->URL@SOURCE->LOC2->REV
           * but figure out the actual revision range merged. */
          (void)find_nearest_ancestor_with_intersecting_ranges(
            &(range.start), &(range.end),
            merge_b->notify_begin.nodes_with_mergeinfo,
            action != svn_wc_conflict_action_delete,
            local_abspath);
          loc1 = svn_client__pathrev_dup(merge_b->merge_source.loc1,
                                         scratch_pool);
          loc2 = svn_client__pathrev_dup(merge_b->merge_source.loc2,
                                         scratch_pool);
          loc1->rev = range.start;
          loc2->rev = range.end;
          source = merge_source_create(loc1, loc2,
                                       merge_b->merge_source.ancestral,
                                       scratch_pool);
          SVN_ERR(make_conflict_versions(&left, &right, local_abspath,
                                         merge_left_node_kind,
                                         merge_right_node_kind,
                                         source, merge_b->target,
                                         result_pool, scratch_pool));
        }
      else
        SVN_ERR(make_conflict_versions(&left, &right, local_abspath,
                                       merge_left_node_kind,
                                       merge_right_node_kind,
                                       &merge_b->merge_source, merge_b->target,
                                       result_pool, scratch_pool));

      /* Fix up delete of file, add of dir replacement (or other way around) */
      if (existing_conflict != NULL && existing_conflict->src_left_version)
          left = existing_conflict->src_left_version;

      conflict = svn_wc_conflict_description_create_tree2(
                        local_abspath, local_node_kind,
                        svn_wc_operation_merge,
                        left, right, result_pool);

      conflict->action = action;
      conflict->reason = reason;

      /* May return SVN_ERR_WC_PATH_UNEXPECTED_STATUS */
      if (existing_conflict)
        SVN_ERR(svn_wc__del_tree_conflict(wc_ctx, local_abspath,
                                          scratch_pool));

      SVN_ERR(svn_wc__add_tree_conflict(merge_b->ctx->wc_ctx, conflict,
                                        scratch_pool));

      if (parent_baton)
        {
          if (! parent_baton->new_tree_conflicts)
            parent_baton->new_tree_conflicts = apr_hash_make(result_pool);

          svn_hash_sets(parent_baton->new_tree_conflicts,
                        apr_pstrdup(result_pool, local_abspath),
                        conflict);
        }

      /* ### TODO: Store in parent baton */
    }

  /* On a replacement we currently get two tree conflicts */
  if (merge_b->ctx->notify_func2 && notify_tc)
    {
      svn_wc_notify_t *notify;

      SVN_ERR(notify_merge_begin(merge_b, local_abspath, FALSE, scratch_pool));

      notify = svn_wc_create_notify(local_abspath, svn_wc_notify_tree_conflict,
                                    scratch_pool);
      notify->kind = local_node_kind;

      merge_b->ctx->notify_func2(merge_b->ctx->notify_baton2, notify,
                                 scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* Record the add for future processing and produce the
   update_add notification
 */
static svn_error_t *
record_update_add(merge_cmd_baton_t *merge_b,
                  const char *local_abspath,
                  svn_node_kind_t kind,
                  svn_boolean_t notify_replaced,
                  apr_pool_t *scratch_pool)
{
  if (merge_b->merge_source.ancestral || merge_b->reintegrate_merge)
    {
      store_path(merge_b->merged_abspaths, local_abspath);
    }

  if (merge_b->ctx->notify_func2)
    {
      svn_wc_notify_t *notify;
      svn_wc_notify_action_t action = svn_wc_notify_update_add;

      SVN_ERR(notify_merge_begin(merge_b, local_abspath, FALSE, scratch_pool));

      if (notify_replaced)
        action = svn_wc_notify_update_replace;

      notify = svn_wc_create_notify(local_abspath, action, scratch_pool);
      notify->kind = kind;

      merge_b->ctx->notify_func2(merge_b->ctx->notify_baton2, notify,
                                 scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* Record the update for future processing and produce the
   update_update notification */
static svn_error_t *
record_update_update(merge_cmd_baton_t *merge_b,
                     const char *local_abspath,
                     svn_node_kind_t kind,
                     svn_wc_notify_state_t content_state,
                     svn_wc_notify_state_t prop_state,
                     apr_pool_t *scratch_pool)
{
  if (merge_b->merge_source.ancestral || merge_b->reintegrate_merge)
    {
      store_path(merge_b->merged_abspaths, local_abspath);
    }

  if (merge_b->ctx->notify_func2)
    {
      svn_wc_notify_t *notify;

      SVN_ERR(notify_merge_begin(merge_b, local_abspath, FALSE, scratch_pool));

      notify = svn_wc_create_notify(local_abspath, svn_wc_notify_update_update,
                                    scratch_pool);
      notify->kind = kind;
      notify->content_state = content_state;
      notify->prop_state = prop_state;

      merge_b->ctx->notify_func2(merge_b->ctx->notify_baton2, notify,
                                 scratch_pool);
    }

  return SVN_NO_ERROR;
}

/* Record the delete for future processing and for (later) producing the
   update_delete notification */
static svn_error_t *
record_update_delete(merge_cmd_baton_t *merge_b,
                     struct merge_dir_baton_t *parent_db,
                     const char *local_abspath,
                     svn_node_kind_t kind,
                     apr_pool_t *scratch_pool)
{
  /* Update the lists of merged, skipped, tree-conflicted and added paths. */
  if (merge_b->merge_source.ancestral
      || merge_b->reintegrate_merge)
    {
      /* Issue #4166: If a previous merge added NOTIFY_ABSPATH, but we
         are now deleting it, then remove it from the list of added
         paths. */
      svn_hash_sets(merge_b->added_abspaths, local_abspath, NULL);
      store_path(merge_b->merged_abspaths, local_abspath);
    }

  SVN_ERR(notify_merge_begin(merge_b, local_abspath, TRUE, scratch_pool));

  if (parent_db)
    {
      const char *dup_abspath = apr_pstrdup(parent_db->pool, local_abspath);

      if (!parent_db->pending_deletes)
        parent_db->pending_deletes = apr_hash_make(parent_db->pool);

      svn_hash_sets(parent_db->pending_deletes, dup_abspath,
                    svn_node_kind_to_word(kind));
    }

  return SVN_NO_ERROR;
}

/* Notify the pending 'D'eletes, that were waiting to see if a matching 'A'dd
   might make them a 'R'eplace. */
static svn_error_t *
handle_pending_notifications(merge_cmd_baton_t *merge_b,
                             struct merge_dir_baton_t *db,
                             apr_pool_t *scratch_pool)
{
  if (merge_b->ctx->notify_func2 && db->pending_deletes)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, db->pending_deletes);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *del_abspath = apr_hash_this_key(hi);
          svn_wc_notify_t *notify;

          notify = svn_wc_create_notify(del_abspath,
                                        svn_wc_notify_update_delete,
                                        scratch_pool);
          notify->kind = svn_node_kind_from_word(
                                    apr_hash_this_val(hi));

          merge_b->ctx->notify_func2(merge_b->ctx->notify_baton2,
                                     notify, scratch_pool);
        }

      db->pending_deletes = NULL;
    }
  return SVN_NO_ERROR;
}

/* Helper function for the merge_dir_*() and merge_file_*() functions.

   Installs and notifies pre-recorded tree conflicts and skips for
   ancestors of operational merges
 */
static svn_error_t *
mark_dir_edited(merge_cmd_baton_t *merge_b,
                struct merge_dir_baton_t *db,
                const char *local_abspath,
                apr_pool_t *scratch_pool)
{
  /* ### Too much common code with mark_file_edited */
  if (db->edited)
    return SVN_NO_ERROR;

  if (db->parent_baton && !db->parent_baton->edited)
    {
      const char *dir_abspath = svn_dirent_dirname(local_abspath,
                                                   scratch_pool);

      SVN_ERR(mark_dir_edited(merge_b, db->parent_baton, dir_abspath,
                              scratch_pool));
    }

  db->edited = TRUE;

  if (! db->shadowed)
    return SVN_NO_ERROR; /* Easy out */

  if (db->parent_baton
      && db->parent_baton->delete_state
      && db->tree_conflict_reason != CONFLICT_REASON_NONE)
    {
      db->parent_baton->delete_state->found_edit = TRUE;
    }
  else if (db->tree_conflict_reason == CONFLICT_REASON_SKIP
           || db->tree_conflict_reason == CONFLICT_REASON_SKIP_WC)
    {
      /* open_directory() decided not to flag a tree conflict, but
         for clarity we produce a skip for this node that
         most likely isn't touched by the merge itself */

      if (merge_b->ctx->notify_func2)
        {
          svn_wc_notify_t *notify;

          SVN_ERR(notify_merge_begin(merge_b, local_abspath, FALSE,
                                     scratch_pool));

          notify = svn_wc_create_notify(
                            local_abspath,
                            (db->tree_conflict_reason == CONFLICT_REASON_SKIP)
                                ? svn_wc_notify_skip
                                : svn_wc_notify_update_skip_obstruction,
                            scratch_pool);
          notify->kind = svn_node_dir;
          notify->content_state = notify->prop_state = db->skip_reason;

          merge_b->ctx->notify_func2(merge_b->ctx->notify_baton2,
                                     notify,
                                     scratch_pool);
        }

      if (merge_b->merge_source.ancestral
          || merge_b->reintegrate_merge)
        {
          store_path(merge_b->skipped_abspaths, local_abspath);
        }
    }
  else if (db->tree_conflict_reason != CONFLICT_REASON_NONE)
    {
      /* open_directory() decided that a tree conflict should be raised */

      SVN_ERR(record_tree_conflict(merge_b, local_abspath, db->parent_baton,
                                   db->tree_conflict_local_node_kind,
                                   db->tree_conflict_merge_left_node_kind,
                                   db->tree_conflict_merge_right_node_kind,
                                   db->tree_conflict_action,
                                   db->tree_conflict_reason,
                                   NULL, TRUE,
                                   scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Helper function for the merge_file_*() functions.

   Installs and notifies pre-recorded tree conflicts and skips for
   ancestors of operational merges
 */
static svn_error_t *
mark_file_edited(merge_cmd_baton_t *merge_b,
                 struct merge_file_baton_t *fb,
                 const char *local_abspath,
                 apr_pool_t *scratch_pool)
{
  /* ### Too much common code with mark_dir_edited */
  if (fb->edited)
    return SVN_NO_ERROR;

  if (fb->parent_baton && !fb->parent_baton->edited)
    {
      const char *dir_abspath = svn_dirent_dirname(local_abspath,
                                                   scratch_pool);

      SVN_ERR(mark_dir_edited(merge_b, fb->parent_baton, dir_abspath,
                              scratch_pool));
    }

  fb->edited = TRUE;

  if (! fb->shadowed)
    return SVN_NO_ERROR; /* Easy out */

  if (fb->parent_baton
      && fb->parent_baton->delete_state
      && fb->tree_conflict_reason != CONFLICT_REASON_NONE)
    {
      fb->parent_baton->delete_state->found_edit = TRUE;
    }
  else if (fb->tree_conflict_reason == CONFLICT_REASON_SKIP
           || fb->tree_conflict_reason == CONFLICT_REASON_SKIP_WC)
    {
      /* open_directory() decided not to flag a tree conflict, but
         for clarity we produce a skip for this node that
         most likely isn't touched by the merge itself */

      if (merge_b->ctx->notify_func2)
        {
          svn_wc_notify_t *notify;

          SVN_ERR(notify_merge_begin(merge_b, local_abspath, FALSE,
                                     scratch_pool));

          notify = svn_wc_create_notify(local_abspath, svn_wc_notify_skip,
                                        scratch_pool);
          notify->kind = svn_node_file;
          notify->content_state = notify->prop_state = fb->skip_reason;

          merge_b->ctx->notify_func2(merge_b->ctx->notify_baton2,
                                     notify,
                                     scratch_pool);
        }

      if (merge_b->merge_source.ancestral
          || merge_b->reintegrate_merge)
        {
          store_path(merge_b->skipped_abspaths, local_abspath);
        }
    }
  else if (fb->tree_conflict_reason != CONFLICT_REASON_NONE)
    {
      /* open_file() decided that a tree conflict should be raised */

      SVN_ERR(record_tree_conflict(merge_b, local_abspath, fb->parent_baton,
                                   fb->tree_conflict_local_node_kind,
                                   fb->tree_conflict_merge_left_node_kind,
                                   fb->tree_conflict_merge_right_node_kind,
                                   fb->tree_conflict_action,
                                   fb->tree_conflict_reason,
                                   NULL, TRUE,
                                   scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t function.

   Called before either merge_file_changed(), merge_file_added(),
   merge_file_deleted() or merge_file_closed(), unless it sets *SKIP to TRUE.

   When *SKIP is TRUE, the diff driver avoids work on getting the details
   for the closing callbacks.
 */
static svn_error_t *
merge_file_opened(void **new_file_baton,
                  svn_boolean_t *skip,
                  const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const svn_diff_source_t *copyfrom_source,
                  void *dir_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  merge_cmd_baton_t *merge_b = processor->baton;
  struct merge_dir_baton_t *pdb = dir_baton;
  struct merge_file_baton_t *fb;
  const char *local_abspath = svn_dirent_join(merge_b->target->abspath,
                                              relpath, scratch_pool);

  fb = apr_pcalloc(result_pool, sizeof(*fb));
  fb->tree_conflict_reason = CONFLICT_REASON_NONE;
  fb->tree_conflict_action = svn_wc_conflict_action_edit;
  fb->skip_reason = svn_wc_notify_state_unknown;

  if (left_source)
    fb->tree_conflict_merge_left_node_kind = svn_node_file;
  else
    fb->tree_conflict_merge_left_node_kind = svn_node_none;

  if (right_source)
    fb->tree_conflict_merge_right_node_kind = svn_node_file;
  else
    fb->tree_conflict_merge_right_node_kind = svn_node_none;

  *new_file_baton = fb;

  if (pdb)
    {
      fb->parent_baton = pdb;
      fb->shadowed = pdb->shadowed;
      fb->skip_reason = pdb->skip_reason;
    }

  if (fb->shadowed)
    {
      /* An ancestor is tree conflicted. Nothing to do here. */
    }
  else if (left_source != NULL)
    {
      /* Node is expected to be a file, which will be changed or deleted. */
      svn_boolean_t is_deleted;
      svn_boolean_t excluded;
      svn_depth_t parent_depth;

      if (! right_source)
        fb->tree_conflict_action = svn_wc_conflict_action_delete;

      {
        svn_wc_notify_state_t obstr_state;

        SVN_ERR(perform_obstruction_check(&obstr_state, &is_deleted, &excluded,
                                          &fb->tree_conflict_local_node_kind,
                                          &parent_depth,
                                          merge_b, local_abspath,
                                          scratch_pool));

        if (obstr_state != svn_wc_notify_state_inapplicable)
          {
            fb->shadowed = TRUE;
            fb->tree_conflict_reason = CONFLICT_REASON_SKIP;
            fb->skip_reason = obstr_state;
            return SVN_NO_ERROR;
          }

        if (is_deleted)
          fb->tree_conflict_local_node_kind = svn_node_none;
      }

      if (fb->tree_conflict_local_node_kind == svn_node_none)
        {
          fb->shadowed = TRUE;

          /* If this is not the merge target and the parent is too shallow to
             contain this directory, and the directory is not present
             via exclusion or depth filtering, skip it instead of recording
             a tree conflict.

             Non-inheritable mergeinfo will be recorded, allowing
             future merges into non-shallow working copies to merge
             changes we missed this time around. */
          if (pdb && (excluded
                      || (parent_depth != svn_depth_unknown &&
                          parent_depth < svn_depth_files)))
            {
                fb->shadowed = TRUE;

                fb->tree_conflict_reason = CONFLICT_REASON_SKIP;
                fb->skip_reason = svn_wc_notify_state_missing;
                return SVN_NO_ERROR;
            }

          if (is_deleted)
            fb->tree_conflict_reason = svn_wc_conflict_reason_deleted;
          else
            fb->tree_conflict_reason = svn_wc_conflict_reason_missing;

          /* ### Similar to directory */
          *skip = TRUE;
          SVN_ERR(mark_file_edited(merge_b, fb, local_abspath, scratch_pool));
          return SVN_NO_ERROR;
          /* ### /Similar */
        }
      else if (fb->tree_conflict_local_node_kind != svn_node_file)
        {
          svn_boolean_t added;
          fb->shadowed = TRUE;

          SVN_ERR(svn_wc__node_is_added(&added, merge_b->ctx->wc_ctx,
                                        local_abspath, scratch_pool));

          fb->tree_conflict_reason = added ? svn_wc_conflict_reason_added
                                           : svn_wc_conflict_reason_obstructed;

          /* ### Similar to directory */
          *skip = TRUE;
          SVN_ERR(mark_file_edited(merge_b, fb, local_abspath, scratch_pool));
          return SVN_NO_ERROR;
          /* ### /Similar */
        }

      if (! right_source)
        {
          /* We want to delete the directory */
          fb->tree_conflict_action = svn_wc_conflict_action_delete;
          SVN_ERR(mark_file_edited(merge_b, fb, local_abspath, scratch_pool));

          if (fb->shadowed)
            {
              return SVN_NO_ERROR; /* Already set a tree conflict */
            }

          /* Comparison mode to verify for delete tree conflicts? */
          if (pdb && pdb->delete_state
              && pdb->delete_state->found_edit)
            {
              /* Earlier nodes found a conflict. Done. */
              *skip = TRUE;
            }
        }
    }
  else
    {
      const svn_wc_conflict_description2_t *old_tc = NULL;

      /* The node doesn't exist pre-merge: We have an addition */
      fb->added = TRUE;
      fb->tree_conflict_action = svn_wc_conflict_action_add;

      if (pdb && pdb->pending_deletes
          && svn_hash_gets(pdb->pending_deletes, local_abspath))
        {
          fb->add_is_replace = TRUE;
          fb->tree_conflict_action = svn_wc_conflict_action_replace;

          svn_hash_sets(pdb->pending_deletes, local_abspath, NULL);
        }

      if (pdb
          && pdb->new_tree_conflicts
          && (old_tc = svn_hash_gets(pdb->new_tree_conflicts, local_abspath)))
        {
          fb->tree_conflict_action = svn_wc_conflict_action_replace;
          fb->tree_conflict_reason = old_tc->reason;

          /* Update the tree conflict to store that this is a replace */
          SVN_ERR(record_tree_conflict(merge_b, local_abspath, pdb,
                                       old_tc->node_kind,
                                       old_tc->src_left_version->node_kind,
                                       svn_node_file,
                                       fb->tree_conflict_action,
                                       fb->tree_conflict_reason,
                                       old_tc, FALSE,
                                       scratch_pool));

          if (old_tc->reason == svn_wc_conflict_reason_deleted
              || old_tc->reason == svn_wc_conflict_reason_moved_away)
            {
              /* Issue #3806: Incoming replacements on local deletes produce
                 inconsistent result.

                 In this specific case we can continue applying the add part
                 of the replacement. */
            }
          else
            {
              *skip = TRUE;

              return SVN_NO_ERROR;
            }
        }
      else if (! (merge_b->dry_run
                  && ((pdb && pdb->added) || fb->add_is_replace)))
        {
          svn_wc_notify_state_t obstr_state;
          svn_boolean_t is_deleted;

          SVN_ERR(perform_obstruction_check(&obstr_state, &is_deleted, NULL,
                                            &fb->tree_conflict_local_node_kind,
                                            NULL, merge_b, local_abspath,
                                            scratch_pool));

          if (obstr_state != svn_wc_notify_state_inapplicable)
            {
              /* Skip the obstruction */
              fb->shadowed = TRUE;
              fb->tree_conflict_reason = CONFLICT_REASON_SKIP;
              fb->skip_reason = obstr_state;
            }
          else if (fb->tree_conflict_local_node_kind != svn_node_none
                   && !is_deleted)
            {
              /* Set a tree conflict */
              svn_boolean_t added;

              fb->shadowed = TRUE;
              SVN_ERR(svn_wc__node_is_added(&added, merge_b->ctx->wc_ctx,
                                            local_abspath, scratch_pool));

              fb->tree_conflict_reason = added ? svn_wc_conflict_reason_added
                                               : svn_wc_conflict_reason_obstructed;
            }
        }

      /* Handle pending conflicts */
      SVN_ERR(mark_file_edited(merge_b, fb, local_abspath, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t function.
 *
 * Called after merge_file_opened() when a node receives only text and/or
 * property changes between LEFT_SOURCE and RIGHT_SOURCE.
 *
 * left_file and right_file can be NULL when the file is not modified.
 * left_props and right_props are always available.
 */
static svn_error_t *
merge_file_changed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  const char *left_file,
                  const char *right_file,
                  /*const*/ apr_hash_t *left_props,
                  /*const*/ apr_hash_t *right_props,
                  svn_boolean_t file_modified,
                  const apr_array_header_t *prop_changes,
                  void *file_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  merge_cmd_baton_t *merge_b = processor->baton;
  struct merge_file_baton_t *fb = file_baton;
  svn_client_ctx_t *ctx = merge_b->ctx;
  const char *local_abspath = svn_dirent_join(merge_b->target->abspath,
                                              relpath, scratch_pool);
  const svn_wc_conflict_version_t *left;
  const svn_wc_conflict_version_t *right;
  svn_wc_notify_state_t text_state;
  svn_wc_notify_state_t property_state;

  SVN_ERR_ASSERT(local_abspath && svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(!left_file || svn_dirent_is_absolute(left_file));
  SVN_ERR_ASSERT(!right_file || svn_dirent_is_absolute(right_file));

  SVN_ERR(mark_file_edited(merge_b, fb, local_abspath, scratch_pool));

  if (fb->shadowed)
    {
      if (fb->tree_conflict_reason == CONFLICT_REASON_NONE)
        {
          /* We haven't notified for this node yet: report a skip */
          SVN_ERR(record_skip(merge_b, local_abspath, svn_node_file,
                              svn_wc_notify_update_shadowed_update,
                              fb->skip_reason, fb->parent_baton,
                              scratch_pool));
        }

      return SVN_NO_ERROR;
    }

  /* This callback is essentially no more than a wrapper around
     svn_wc_merge5().  Thank goodness that all the
     diff-editor-mechanisms are doing the hard work of getting the
     fulltexts! */

  property_state = svn_wc_notify_state_unchanged;
  text_state = svn_wc_notify_state_unchanged;

  SVN_ERR(prepare_merge_props_changed(&prop_changes, local_abspath,
                                      prop_changes, merge_b,
                                      scratch_pool, scratch_pool));

  SVN_ERR(make_conflict_versions(&left, &right, local_abspath,
                                 svn_node_file, svn_node_file,
                                 &merge_b->merge_source, merge_b->target,
                                 scratch_pool, scratch_pool));

  /* Do property merge now, if we are not going to perform a text merge */
  if ((merge_b->record_only || !left_file) && prop_changes->nelts)
    {
      SVN_ERR(svn_wc_merge_props3(&property_state, ctx->wc_ctx, local_abspath,
                                  left, right,
                                  left_props, prop_changes,
                                  merge_b->dry_run,
                                  NULL, NULL,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  scratch_pool));
      if (property_state == svn_wc_notify_state_conflicted)
        {
          alloc_and_store_path(&merge_b->conflicted_paths, local_abspath,
                               merge_b->pool);
        }
    }

  /* Easy out: We are only applying mergeinfo differences. */
  if (merge_b->record_only)
    {
      /* NO-OP */
    }
  else if (left_file)
    {
      svn_boolean_t has_local_mods;
      enum svn_wc_merge_outcome_t content_outcome;
      const char *target_label;
      const char *left_label;
      const char *right_label;
      const char *path_ext = "";

      if (merge_b->ext_patterns && merge_b->ext_patterns->nelts)
        {
          svn_path_splitext(NULL, &path_ext, local_abspath, scratch_pool);
          if (! (*path_ext
                 && svn_cstring_match_glob_list(path_ext,
                                                merge_b->ext_patterns)))
            {
              path_ext = "";
            }
        }

      /* xgettext: the '.working', '.merge-left.r%ld' and
         '.merge-right.r%ld' strings are used to tag onto a file
         name in case of a merge conflict */

      target_label = apr_psprintf(scratch_pool, _(".working%s%s"),
                                  *path_ext ? "." : "", path_ext);
      left_label = apr_psprintf(scratch_pool,
                                _(".merge-left.r%ld%s%s"),
                                left_source->revision,
                                *path_ext ? "." : "", path_ext);
      right_label = apr_psprintf(scratch_pool,
                                 _(".merge-right.r%ld%s%s"),
                                 right_source->revision,
                                 *path_ext ? "." : "", path_ext);

      SVN_ERR(svn_wc_text_modified_p2(&has_local_mods, ctx->wc_ctx,
                                      local_abspath, FALSE, scratch_pool));

      /* Do property merge and text merge in one step so that keyword expansion
         takes into account the new property values. */
      SVN_ERR(svn_wc_merge5(&content_outcome, &property_state, ctx->wc_ctx,
                            left_file, right_file, local_abspath,
                            left_label, right_label, target_label,
                            left, right,
                            merge_b->dry_run, merge_b->diff3_cmd,
                            merge_b->merge_options,
                            left_props, prop_changes,
                            NULL, NULL,
                            ctx->cancel_func,
                            ctx->cancel_baton,
                            scratch_pool));

      if (content_outcome == svn_wc_merge_conflict
          || property_state == svn_wc_notify_state_conflicted)
        {
          alloc_and_store_path(&merge_b->conflicted_paths, local_abspath,
                               merge_b->pool);
        }

      if (content_outcome == svn_wc_merge_conflict)
        text_state = svn_wc_notify_state_conflicted;
      else if (has_local_mods
               && content_outcome != svn_wc_merge_unchanged)
        text_state = svn_wc_notify_state_merged;
      else if (content_outcome == svn_wc_merge_merged)
        text_state = svn_wc_notify_state_changed;
      else if (content_outcome == svn_wc_merge_no_merge)
        text_state = svn_wc_notify_state_missing;
      else /* merge_outcome == svn_wc_merge_unchanged */
        text_state = svn_wc_notify_state_unchanged;
    }

  if (text_state == svn_wc_notify_state_conflicted
      || text_state == svn_wc_notify_state_merged
      || text_state == svn_wc_notify_state_changed
      || property_state == svn_wc_notify_state_conflicted
      || property_state == svn_wc_notify_state_merged
      || property_state == svn_wc_notify_state_changed)
    {
      SVN_ERR(record_update_update(merge_b, local_abspath, svn_node_file,
                                   text_state, property_state,
                                   scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t function.
 *
 * Called after merge_file_opened() when a node doesn't exist in LEFT_SOURCE,
 * but does in RIGHT_SOURCE.
 *
 * When a node is replaced instead of just added a separate opened+deleted will
 * be invoked before the current open+added.
 */
static svn_error_t *
merge_file_added(const char *relpath,
                 const svn_diff_source_t *copyfrom_source,
                 const svn_diff_source_t *right_source,
                 const char *copyfrom_file,
                 const char *right_file,
                 /*const*/ apr_hash_t *copyfrom_props,
                 /*const*/ apr_hash_t *right_props,
                 void *file_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  merge_cmd_baton_t *merge_b = processor->baton;
  struct merge_file_baton_t *fb = file_baton;
  const char *local_abspath = svn_dirent_join(merge_b->target->abspath,
                                              relpath, scratch_pool);
  apr_hash_t *pristine_props;
  apr_hash_t *new_props;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(mark_file_edited(merge_b, fb, local_abspath, scratch_pool));

  if (fb->shadowed)
    {
      if (fb->tree_conflict_reason == CONFLICT_REASON_NONE)
        {
          /* We haven't notified for this node yet: report a skip */
          SVN_ERR(record_skip(merge_b, local_abspath, svn_node_file,
                              svn_wc_notify_update_shadowed_add,
                              fb->skip_reason, fb->parent_baton,
                              scratch_pool));
        }

      return SVN_NO_ERROR;
    }

  /* Easy out: We are only applying mergeinfo differences. */
  if (merge_b->record_only)
    {
      return SVN_NO_ERROR;
    }

  if ((merge_b->merge_source.ancestral || merge_b->reintegrate_merge)
      && ( !fb->parent_baton || !fb->parent_baton->added))
    {
      /* Store the roots of added subtrees */
      store_path(merge_b->added_abspaths, local_abspath);
    }

  if (!merge_b->dry_run)
    {
      const char *copyfrom_url;
      svn_revnum_t copyfrom_rev;
      svn_stream_t *new_contents, *pristine_contents;

      /* If this is a merge from the same repository as our
         working copy, we handle adds as add-with-history.
         Otherwise, we'll use a pure add. */
      if (merge_b->same_repos)
        {
          copyfrom_url = svn_path_url_add_component2(
                                       merge_b->merge_source.loc2->url,
                                       relpath, scratch_pool);
          copyfrom_rev = right_source->revision;
          SVN_ERR(check_repos_match(merge_b->target, local_abspath,
                                    copyfrom_url, scratch_pool));
          SVN_ERR(svn_stream_open_readonly(&pristine_contents,
                                           right_file,
                                           scratch_pool,
                                           scratch_pool));
          new_contents = NULL; /* inherit from new_base_contents */

          pristine_props = right_props; /* Includes last_* information */
          new_props = NULL; /* No local changes */

          if (svn_hash_gets(pristine_props, SVN_PROP_MERGEINFO))
            {
              alloc_and_store_path(&merge_b->paths_with_new_mergeinfo,
                                   local_abspath, merge_b->pool);
            }
        }
      else
        {
          apr_array_header_t *regular_props;

          copyfrom_url = NULL;
          copyfrom_rev = SVN_INVALID_REVNUM;

          pristine_contents = svn_stream_empty(scratch_pool);
          SVN_ERR(svn_stream_open_readonly(&new_contents, right_file,
                                           scratch_pool, scratch_pool));

          pristine_props = apr_hash_make(scratch_pool); /* Local addition */

          /* We don't want any foreign properties */
          SVN_ERR(svn_categorize_props(svn_prop_hash_to_array(right_props,
                                                              scratch_pool),
                                       NULL, NULL, &regular_props,
                                       scratch_pool));

          new_props = svn_prop_array_to_hash(regular_props, scratch_pool);

          /* Issue #3383: We don't want mergeinfo from a foreign repository. */
          svn_hash_sets(new_props, SVN_PROP_MERGEINFO, NULL);
        }

      /* Do everything like if we had called 'svn cp PATH1 PATH2'. */
      SVN_ERR(svn_wc_add_repos_file4(merge_b->ctx->wc_ctx,
                                      local_abspath,
                                      pristine_contents,
                                      new_contents,
                                      pristine_props, new_props,
                                      copyfrom_url, copyfrom_rev,
                                      merge_b->ctx->cancel_func,
                                      merge_b->ctx->cancel_baton,
                                      scratch_pool));

      /* Caller must call svn_sleep_for_timestamps() */
      *merge_b->use_sleep = TRUE;
    }

  SVN_ERR(record_update_add(merge_b, local_abspath, svn_node_file,
                            fb->add_is_replace, scratch_pool));

  return SVN_NO_ERROR;
}

/* Compare the two sets of properties PROPS1 and PROPS2, ignoring the
 * "svn:mergeinfo" property, and noticing only "normal" props. Set *SAME to
 * true if the rest of the properties are identical or false if they differ.
 */
static svn_error_t *
properties_same_p(svn_boolean_t *same,
                  apr_hash_t *props1,
                  apr_hash_t *props2,
                  apr_pool_t *scratch_pool)
{
  apr_array_header_t *prop_changes;
  int i, diffs;

  /* Examine the properties that differ */
  SVN_ERR(svn_prop_diffs(&prop_changes, props1, props2, scratch_pool));
  diffs = 0;
  for (i = 0; i < prop_changes->nelts; i++)
    {
      const char *pname = APR_ARRAY_IDX(prop_changes, i, svn_prop_t).name;

      /* Count the properties we're interested in; ignore the rest */
      if (svn_wc_is_normal_prop(pname)
          && strcmp(pname, SVN_PROP_MERGEINFO) != 0)
        diffs++;
    }
  *same = (diffs == 0);
  return SVN_NO_ERROR;
}

/* Compare the file OLDER_ABSPATH (together with its normal properties in
 * ORIGINAL_PROPS which may also contain WC props and entry props) with the
 * versioned file MINE_ABSPATH (together with its versioned properties).
 * Set *SAME to true if they are the same or false if they differ, ignoring
 * the "svn:mergeinfo" property, and ignoring differences in keyword
 * expansion and end-of-line style. */
static svn_error_t *
files_same_p(svn_boolean_t *same,
             const char *older_abspath,
             apr_hash_t *original_props,
             const char *mine_abspath,
             svn_wc_context_t *wc_ctx,
             apr_pool_t *scratch_pool)
{
  apr_hash_t *working_props;

  SVN_ERR(svn_wc_prop_list2(&working_props, wc_ctx, mine_abspath,
                            scratch_pool, scratch_pool));

  /* Compare the properties */
  SVN_ERR(properties_same_p(same, original_props, working_props,
                            scratch_pool));
  if (*same)
    {
      svn_stream_t *mine_stream;
      svn_stream_t *older_stream;
      svn_string_t *special = svn_hash_gets(working_props, SVN_PROP_SPECIAL);
      svn_string_t *eol_style = svn_hash_gets(working_props, SVN_PROP_EOL_STYLE);
      svn_string_t *keywords = svn_hash_gets(working_props, SVN_PROP_KEYWORDS);

      /* Compare the file content, translating 'mine' to 'normal' form. */
      if (special != NULL)
        SVN_ERR(svn_subst_read_specialfile(&mine_stream, mine_abspath,
                                           scratch_pool, scratch_pool));
      else
        SVN_ERR(svn_stream_open_readonly(&mine_stream, mine_abspath,
                                         scratch_pool, scratch_pool));

      if (!special && (eol_style || keywords))
        {
          apr_hash_t *kw = NULL;
          const char *eol = NULL;
          svn_subst_eol_style_t style;

          /* We used to use svn_client__get_normalized_stream() here, but
             that doesn't work in 100% of the cases because it doesn't
             convert EOLs to the repository form; just to '\n'.
           */

          if (eol_style)
            {
              svn_subst_eol_style_from_value(&style, &eol, eol_style->data);

              if (style == svn_subst_eol_style_native)
                eol = SVN_SUBST_NATIVE_EOL_STR;
              else if (style != svn_subst_eol_style_fixed
                       && style != svn_subst_eol_style_none)
                return svn_error_create(SVN_ERR_IO_UNKNOWN_EOL, NULL, NULL);
            }

          if (keywords)
            SVN_ERR(svn_subst_build_keywords3(&kw, keywords->data, "", "",
                                              "", 0, "", scratch_pool));

          mine_stream = svn_subst_stream_translated(
            mine_stream, eol, FALSE, kw, FALSE, scratch_pool);
        }

      SVN_ERR(svn_stream_open_readonly(&older_stream, older_abspath,
                                       scratch_pool, scratch_pool));

      SVN_ERR(svn_stream_contents_same2(same, mine_stream, older_stream,
                                        scratch_pool));

    }

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t function.
 *
 * Called after merge_file_opened() when a node does exist in LEFT_SOURCE, but
 * no longer exists (or is replaced) in RIGHT_SOURCE.
 *
 * When a node is replaced instead of just added a separate opened+added will
 * be invoked after the current open+deleted.
 */
static svn_error_t *
merge_file_deleted(const char *relpath,
                   const svn_diff_source_t *left_source,
                   const char *left_file,
                   /*const*/ apr_hash_t *left_props,
                   void *file_baton,
                   const struct svn_diff_tree_processor_t *processor,
                   apr_pool_t *scratch_pool)
{
  merge_cmd_baton_t *merge_b = processor->baton;
  struct merge_file_baton_t *fb = file_baton;
  const char *local_abspath = svn_dirent_join(merge_b->target->abspath,
                                              relpath, scratch_pool);
  svn_boolean_t same;

  SVN_ERR(mark_file_edited(merge_b, fb, local_abspath, scratch_pool));

  if (fb->shadowed)
    {
      if (fb->tree_conflict_reason == CONFLICT_REASON_NONE)
        {
          /* We haven't notified for this node yet: report a skip */
          SVN_ERR(record_skip(merge_b, local_abspath, svn_node_file,
                              svn_wc_notify_update_shadowed_delete,
                              fb->skip_reason, fb->parent_baton,
                              scratch_pool));
        }

      return SVN_NO_ERROR;
    }

  /* Easy out: We are only applying mergeinfo differences. */
  if (merge_b->record_only)
    {
      return SVN_NO_ERROR;
    }

  /* If the files are identical, attempt deletion */
  if (merge_b->force_delete)
    same = TRUE;
  else
    SVN_ERR(files_same_p(&same, left_file, left_props,
                         local_abspath, merge_b->ctx->wc_ctx,
                         scratch_pool));

  if (fb->parent_baton
      && fb->parent_baton->delete_state)
    {
      if (same)
        {
          /* Note that we checked this file */
          store_path(fb->parent_baton->delete_state->compared_abspaths,
                     local_abspath);
        }
      else
        {
          /* We found some modification. Parent should raise a tree conflict */
          fb->parent_baton->delete_state->found_edit = TRUE;
        }

      return SVN_NO_ERROR;
    }
  else if (same)
    {
      if (!merge_b->dry_run)
        SVN_ERR(svn_wc_delete4(merge_b->ctx->wc_ctx, local_abspath,
                               FALSE /* keep_local */, FALSE /* unversioned */,
                               merge_b->ctx->cancel_func,
                               merge_b->ctx->cancel_baton,
                               NULL, NULL /* no notify */,
                               scratch_pool));

      /* Record that we might have deleted mergeinfo */
      alloc_and_store_path(&merge_b->paths_with_deleted_mergeinfo,
                           local_abspath, merge_b->pool);

      /* And notify the deletion */
      SVN_ERR(record_update_delete(merge_b, fb->parent_baton, local_abspath,
                                   svn_node_file, scratch_pool));
    }
  else
    {
      /* The files differ, so raise a conflict instead of deleting */

      /* This is use case 5 described in the paper attached to issue
       * #2282.  See also notes/tree-conflicts/detection.txt
       */
      SVN_ERR(record_tree_conflict(merge_b, local_abspath, fb->parent_baton,
                                   svn_node_file,
                                   svn_node_file,
                                   svn_node_none,
                                   svn_wc_conflict_action_delete,
                                   svn_wc_conflict_reason_edited,
                                   NULL, TRUE,
                                   scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t function.

   Called before either merge_dir_changed(), merge_dir_added(),
   merge_dir_deleted() or merge_dir_closed(), unless it sets *SKIP to TRUE.

   After this call and before the close call, all descendants will receive
   their changes, unless *SKIP_CHILDREN is set to TRUE.

   When *SKIP is TRUE, the diff driver avoids work on getting the details
   for the closing callbacks.

   The SKIP and SKIP_DESCENDANTS work independently.
 */
static svn_error_t *
merge_dir_opened(void **new_dir_baton,
                 svn_boolean_t *skip,
                 svn_boolean_t *skip_children,
                 const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 const svn_diff_source_t *copyfrom_source,
                 void *parent_dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  merge_cmd_baton_t *merge_b = processor->baton;
  struct merge_dir_baton_t *db;
  struct merge_dir_baton_t *pdb = parent_dir_baton;

  const char *local_abspath = svn_dirent_join(merge_b->target->abspath,
                                              relpath, scratch_pool);

  db = apr_pcalloc(result_pool, sizeof(*db));
  db->pool = result_pool;
  db->tree_conflict_reason = CONFLICT_REASON_NONE;
  db->tree_conflict_action = svn_wc_conflict_action_edit;
  db->skip_reason = svn_wc_notify_state_unknown;

  *new_dir_baton = db;

  if (left_source)
    db->tree_conflict_merge_left_node_kind = svn_node_dir;
  else
    db->tree_conflict_merge_left_node_kind = svn_node_none;

  if (right_source)
    db->tree_conflict_merge_right_node_kind = svn_node_dir;
  else
    db->tree_conflict_merge_right_node_kind = svn_node_none;

  if (pdb)
    {
      db->parent_baton = pdb;
      db->shadowed = pdb->shadowed;
      db->skip_reason = pdb->skip_reason;
    }

  if (db->shadowed)
    {
      /* An ancestor is tree conflicted. Nothing to do here. */
      if (! left_source)
        db->added = TRUE;
    }
  else if (left_source != NULL)
    {
      /* Node is expected to be a directory. */
      svn_boolean_t is_deleted;
      svn_boolean_t excluded;
      svn_depth_t parent_depth;

      if (! right_source)
          db->tree_conflict_action = svn_wc_conflict_action_delete;

      /* Check for an obstructed or missing node on disk. */
      {
        svn_wc_notify_state_t obstr_state;
        SVN_ERR(perform_obstruction_check(&obstr_state, &is_deleted, &excluded,
                                          &db->tree_conflict_local_node_kind,
                                          &parent_depth, merge_b,
                                          local_abspath, scratch_pool));

        if (obstr_state != svn_wc_notify_state_inapplicable)
          {
            db->shadowed = TRUE;

            if (obstr_state == svn_wc_notify_state_obstructed)
              {
                svn_boolean_t is_wcroot;

                SVN_ERR(svn_wc_check_root(&is_wcroot, NULL, NULL,
                                        merge_b->ctx->wc_ctx,
                                        local_abspath, scratch_pool));

                if (is_wcroot)
                  {
                    db->tree_conflict_reason = CONFLICT_REASON_SKIP_WC;
                    return SVN_NO_ERROR;
                  }
              }

            db->tree_conflict_reason = CONFLICT_REASON_SKIP;
            db->skip_reason = obstr_state;

            if (! right_source)
              {
                *skip = *skip_children = TRUE;
                SVN_ERR(mark_dir_edited(merge_b, db, local_abspath,
                                        scratch_pool));
              }

            return SVN_NO_ERROR;
          }

        if (is_deleted)
          db->tree_conflict_local_node_kind = svn_node_none;
      }

      if (db->tree_conflict_local_node_kind == svn_node_none)
        {
          db->shadowed = TRUE;

          /* If this is not the merge target and the parent is too shallow to
             contain this directory, and the directory is not presen
             via exclusion or depth filtering, skip it instead of recording
             a tree conflict.

             Non-inheritable mergeinfo will be recorded, allowing
             future merges into non-shallow working copies to merge
             changes we missed this time around. */
          if (pdb && (excluded
                      || (parent_depth != svn_depth_unknown &&
                          parent_depth < svn_depth_immediates)))
            {
              db->shadowed = TRUE;

              db->tree_conflict_reason = CONFLICT_REASON_SKIP;
              db->skip_reason = svn_wc_notify_state_missing;

              return SVN_NO_ERROR;
            }

          if (is_deleted)
            db->tree_conflict_reason = svn_wc_conflict_reason_deleted;
          else
            db->tree_conflict_reason = svn_wc_conflict_reason_missing;

          /* ### To avoid breaking tests */
          *skip = TRUE;
          *skip_children = TRUE;
          SVN_ERR(mark_dir_edited(merge_b, db, local_abspath, scratch_pool));
          return SVN_NO_ERROR;
          /* ### /avoid breaking tests */
        }
      else if (db->tree_conflict_local_node_kind != svn_node_dir)
        {
          svn_boolean_t added;

          db->shadowed = TRUE;
          SVN_ERR(svn_wc__node_is_added(&added, merge_b->ctx->wc_ctx,
                                        local_abspath, scratch_pool));

          db->tree_conflict_reason = added ? svn_wc_conflict_reason_added
                                           : svn_wc_conflict_reason_obstructed;

          /* ### To avoid breaking tests */
          *skip = TRUE;
          *skip_children = TRUE;
          SVN_ERR(mark_dir_edited(merge_b, db, local_abspath, scratch_pool));
          return SVN_NO_ERROR;
          /* ### /avoid breaking tests */
        }

      if (! right_source)
        {
          /* We want to delete the directory */
          /* Mark PB edited now? */
          db->tree_conflict_action = svn_wc_conflict_action_delete;
          SVN_ERR(mark_dir_edited(merge_b, db, local_abspath, scratch_pool));

          if (db->shadowed)
            {
              *skip_children = TRUE;
              return SVN_NO_ERROR; /* Already set a tree conflict */
            }

          db->delete_state = (pdb != NULL) ? pdb->delete_state : NULL;

          if (db->delete_state && db->delete_state->found_edit)
            {
              /* A sibling found a conflict. Done. */
              *skip = TRUE;
              *skip_children = TRUE;
            }
          else if (merge_b->force_delete)
            {
              /* No comparison necessary */
              *skip_children = TRUE;
            }
          else if (! db->delete_state)
            {
              /* Start descendant comparison */
              db->delete_state = apr_pcalloc(db->pool,
                                             sizeof(*db->delete_state));

              db->delete_state->del_root = db;
              db->delete_state->compared_abspaths = apr_hash_make(db->pool);
            }
        }
    }
  else
    {
      const svn_wc_conflict_description2_t *old_tc = NULL;

      /* The node doesn't exist pre-merge: We have an addition */
      db->added = TRUE;
      db->tree_conflict_action = svn_wc_conflict_action_add;

      if (pdb && pdb->pending_deletes
          && svn_hash_gets(pdb->pending_deletes, local_abspath))
        {
          db->add_is_replace = TRUE;
          db->tree_conflict_action = svn_wc_conflict_action_replace;

          svn_hash_sets(pdb->pending_deletes, local_abspath, NULL);
        }

      if (pdb
          && pdb->new_tree_conflicts
          && (old_tc = svn_hash_gets(pdb->new_tree_conflicts, local_abspath)))
        {
          db->tree_conflict_action = svn_wc_conflict_action_replace;
          db->tree_conflict_reason = old_tc->reason;

          if (old_tc->reason == svn_wc_conflict_reason_deleted
             || old_tc->reason == svn_wc_conflict_reason_moved_away)
            {
              /* Issue #3806: Incoming replacements on local deletes produce
                 inconsistent result.

                 In this specific case we can continue applying the add part
                 of the replacement. */
            }
          else
            {
              *skip = TRUE;
              *skip_children = TRUE;

              /* Update the tree conflict to store that this is a replace */
              SVN_ERR(record_tree_conflict(merge_b, local_abspath, pdb,
                                           old_tc->node_kind,
                                           old_tc->src_left_version->node_kind,
                                           svn_node_dir,
                                           db->tree_conflict_action,
                                           db->tree_conflict_reason,
                                           old_tc, FALSE,
                                           scratch_pool));

              return SVN_NO_ERROR;
            }
        }

      if (! (merge_b->dry_run
             && ((pdb && pdb->added) || db->add_is_replace)))
        {
          svn_wc_notify_state_t obstr_state;
          svn_boolean_t is_deleted;

          SVN_ERR(perform_obstruction_check(&obstr_state, &is_deleted, NULL,
                                            &db->tree_conflict_local_node_kind,
                                            NULL, merge_b, local_abspath,
                                            scratch_pool));

          /* In this case of adding a directory, we have an exception to the
           * usual "skip if it's inconsistent" rule. If the directory exists
           * on disk unexpectedly, we simply make it versioned, because we can
           * do so without risk of destroying data. Only skip if it is
           * versioned but unexpectedly missing from disk, or is unversioned
           * but obstructed by a node of the wrong kind. */
          if (obstr_state == svn_wc_notify_state_obstructed
              && (is_deleted ||
                  db->tree_conflict_local_node_kind == svn_node_none))
            {
              svn_node_kind_t disk_kind;

              SVN_ERR(svn_io_check_path(local_abspath, &disk_kind,
                                        scratch_pool));

              if (disk_kind == svn_node_dir)
                {
                  obstr_state = svn_wc_notify_state_inapplicable;
                  db->add_existing = TRUE; /* Take over existing directory */
                }
            }

          if (obstr_state != svn_wc_notify_state_inapplicable)
            {
              /* Skip the obstruction */
              db->shadowed = TRUE;
              db->tree_conflict_reason = CONFLICT_REASON_SKIP;
              db->skip_reason = obstr_state;
            }
          else if (db->tree_conflict_local_node_kind != svn_node_none
                   && !is_deleted)
            {
              /* Set a tree conflict */
              svn_boolean_t added;
              db->shadowed = TRUE;

              SVN_ERR(svn_wc__node_is_added(&added, merge_b->ctx->wc_ctx,
                                            local_abspath, scratch_pool));

              db->tree_conflict_reason = added ? svn_wc_conflict_reason_added
                                               : svn_wc_conflict_reason_obstructed;
            }
        }

      /* Handle pending conflicts */
      SVN_ERR(mark_dir_edited(merge_b, db, local_abspath, scratch_pool));

      if (db->shadowed)
        {
          /* Notified and done. Skip children? */
        }
      else if (merge_b->record_only)
        {
          /* Ok, we are done for this node and its descendants */
          *skip = TRUE;
          *skip_children = TRUE;
        }
      else if (! merge_b->dry_run)
        {
          /* Create the directory on disk, to allow descendants to be added */
          if (! db->add_existing)
            SVN_ERR(svn_io_dir_make(local_abspath, APR_OS_DEFAULT,
                                    scratch_pool));

          if (old_tc)
            {
              /* svn_wc_add4 and svn_wc_add_from_disk3 can't add a node
                 over an existing tree conflict */

              /* ### These functions should take some tree conflict argument
                     and allow overwriting the tc when one is passed */

              SVN_ERR(svn_wc__del_tree_conflict(merge_b->ctx->wc_ctx,
                                                local_abspath,
                                                scratch_pool));
            }

          if (merge_b->same_repos)
            {
              const char *original_url;

              original_url = svn_path_url_add_component2(
                                        merge_b->merge_source.loc2->url,
                                        relpath, scratch_pool);

              /* Limitation (aka HACK):
                 We create a newly added directory with an original URL and
                 revision as that in the repository, but without its properties
                 and children.

                 When the merge is cancelled before the final dir_added(), the
                 copy won't really represent the in-repository state of the node.
               */
              SVN_ERR(svn_wc_add4(merge_b->ctx->wc_ctx, local_abspath,
                                  svn_depth_infinity,
                                  original_url,
                                  right_source->revision,
                                  merge_b->ctx->cancel_func,
                                  merge_b->ctx->cancel_baton,
                                  NULL, NULL /* no notify! */,
                                  scratch_pool));
            }
          else
            {
              SVN_ERR(svn_wc_add_from_disk3(merge_b->ctx->wc_ctx, local_abspath,
                                            apr_hash_make(scratch_pool),
                                            FALSE /* skip checks */,
                                            NULL, NULL /* no notify! */,
                                            scratch_pool));
            }

          if (old_tc != NULL)
            {
              /* ### Should be atomic with svn_wc_add(4|_from_disk2)() */
              SVN_ERR(record_tree_conflict(merge_b, local_abspath, pdb,
                                           old_tc->node_kind,
                                           svn_node_none,
                                           svn_node_dir,
                                           db->tree_conflict_action,
                                           db->tree_conflict_reason,
                                           old_tc, FALSE,
                                           scratch_pool));
            }
        }

      if (! db->shadowed && !merge_b->record_only)
        SVN_ERR(record_update_add(merge_b, local_abspath, svn_node_dir,
                                  db->add_is_replace, scratch_pool));
    }
  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t function.
 *
 * Called after merge_dir_opened() when a node exists in both the left and
 * right source, but has its properties changed inbetween.
 *
 * After the merge_dir_opened() but before the call to this merge_dir_changed()
 * function all descendants will have been updated.
 */
static svn_error_t *
merge_dir_changed(const char *relpath,
                  const svn_diff_source_t *left_source,
                  const svn_diff_source_t *right_source,
                  /*const*/ apr_hash_t *left_props,
                  /*const*/ apr_hash_t *right_props,
                  const apr_array_header_t *prop_changes,
                  void *dir_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  merge_cmd_baton_t *merge_b = processor->baton;
  struct merge_dir_baton_t *db = dir_baton;
  const apr_array_header_t *props;
  const char *local_abspath = svn_dirent_join(merge_b->target->abspath,
                                              relpath, scratch_pool);

  SVN_ERR(handle_pending_notifications(merge_b, db, scratch_pool));

  SVN_ERR(mark_dir_edited(merge_b, db, local_abspath, scratch_pool));

  if (db->shadowed)
    {
      if (db->tree_conflict_reason == CONFLICT_REASON_NONE)
        {
          /* We haven't notified for this node yet: report a skip */
          SVN_ERR(record_skip(merge_b, local_abspath, svn_node_dir,
                              svn_wc_notify_update_shadowed_update,
                              db->skip_reason, db->parent_baton,
                              scratch_pool));
        }

      return SVN_NO_ERROR;
    }

  SVN_ERR(prepare_merge_props_changed(&props, local_abspath, prop_changes,
                                      merge_b, scratch_pool, scratch_pool));

  if (props->nelts)
    {
      const svn_wc_conflict_version_t *left;
      const svn_wc_conflict_version_t *right;
      svn_client_ctx_t *ctx = merge_b->ctx;
      svn_wc_notify_state_t prop_state;

      SVN_ERR(make_conflict_versions(&left, &right, local_abspath,
                                     svn_node_dir, svn_node_dir,
                                     &merge_b->merge_source,
                                     merge_b->target,
                                     scratch_pool, scratch_pool));

      SVN_ERR(svn_wc_merge_props3(&prop_state, ctx->wc_ctx, local_abspath,
                                  left, right,
                                  left_props, props,
                                  merge_b->dry_run,
                                  NULL, NULL,
                                  ctx->cancel_func, ctx->cancel_baton,
                                  scratch_pool));

      if (prop_state == svn_wc_notify_state_conflicted)
        {
          alloc_and_store_path(&merge_b->conflicted_paths, local_abspath,
                               merge_b->pool);
        }

      if (prop_state == svn_wc_notify_state_conflicted
          || prop_state == svn_wc_notify_state_merged
          || prop_state == svn_wc_notify_state_changed)
        {
          SVN_ERR(record_update_update(merge_b, local_abspath, svn_node_file,
                                       svn_wc_notify_state_inapplicable,
                                       prop_state, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}


/* An svn_diff_tree_processor_t function.
 *
 * Called after merge_dir_opened() when a node doesn't exist in LEFT_SOURCE,
 * but does in RIGHT_SOURCE. After the merge_dir_opened() but before the call
 * to this merge_dir_added() function all descendants will have been added.
 *
 * When a node is replaced instead of just added a separate opened+deleted will
 * be invoked before the current open+added.
 */
static svn_error_t *
merge_dir_added(const char *relpath,
                const svn_diff_source_t *copyfrom_source,
                const svn_diff_source_t *right_source,
                /*const*/ apr_hash_t *copyfrom_props,
                /*const*/ apr_hash_t *right_props,
                void *dir_baton,
                const struct svn_diff_tree_processor_t *processor,
                apr_pool_t *scratch_pool)
{
  merge_cmd_baton_t *merge_b = processor->baton;
  struct merge_dir_baton_t *db = dir_baton;
  const char *local_abspath = svn_dirent_join(merge_b->target->abspath,
                                              relpath, scratch_pool);

  /* For consistency; usually a no-op from _dir_added() */
  SVN_ERR(handle_pending_notifications(merge_b, db, scratch_pool));
  SVN_ERR(mark_dir_edited(merge_b, db, local_abspath, scratch_pool));

  if (db->shadowed)
    {
      if (db->tree_conflict_reason == CONFLICT_REASON_NONE)
        {
          /* We haven't notified for this node yet: report a skip */
          SVN_ERR(record_skip(merge_b, local_abspath, svn_node_dir,
                              svn_wc_notify_update_shadowed_add,
                              db->skip_reason, db->parent_baton,
                              scratch_pool));
        }

      return SVN_NO_ERROR;
    }

  SVN_ERR_ASSERT(
                 db->edited                  /* Marked edited from merge_open_dir() */
                 && ! merge_b->record_only /* Skip details from merge_open_dir() */
                 );

  if ((merge_b->merge_source.ancestral || merge_b->reintegrate_merge)
      && ( !db->parent_baton || !db->parent_baton->added))
    {
      /* Store the roots of added subtrees */
      store_path(merge_b->added_abspaths, local_abspath);
    }

  if (merge_b->same_repos)
    {
      /* When the directory was added in merge_dir_added() we didn't update its
         pristine properties. Instead we receive the property changes later and
         apply them in this function.

         If we would apply them as changes (such as before fixing issue #3405),
         we would see the unmodified properties as local changes, and the
         pristine properties would be out of sync with what the repository
         expects for this directory.

         Instead of doing that we now simply set the properties as the pristine
         properties via a private libsvn_wc api.
      */

      const char *copyfrom_url;
      svn_revnum_t copyfrom_rev;
      const char *parent_abspath;
      const char *child;

      /* Creating a hash containing regular and entry props */
      apr_hash_t *new_pristine_props = right_props;

      parent_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
      child = svn_dirent_is_child(merge_b->target->abspath, local_abspath, NULL);
      SVN_ERR_ASSERT(child != NULL);

      copyfrom_url = svn_path_url_add_component2(merge_b->merge_source.loc2->url,
                                                 child, scratch_pool);
      copyfrom_rev = right_source->revision;

      SVN_ERR(check_repos_match(merge_b->target, parent_abspath, copyfrom_url,
                                scratch_pool));

      if (!merge_b->dry_run)
        {
          SVN_ERR(svn_wc__complete_directory_add(merge_b->ctx->wc_ctx,
                                                local_abspath,
                                                new_pristine_props,
                                                copyfrom_url, copyfrom_rev,
                                                scratch_pool));
        }

      if (svn_hash_gets(new_pristine_props, SVN_PROP_MERGEINFO))
        {
          alloc_and_store_path(&merge_b->paths_with_new_mergeinfo,
                               local_abspath, merge_b->pool);
        }
    }
  else
    {
      apr_array_header_t *regular_props;
      apr_hash_t *new_props;
      svn_wc_notify_state_t prop_state;

      SVN_ERR(svn_categorize_props(svn_prop_hash_to_array(right_props,
                                                          scratch_pool),
                                   NULL, NULL, &regular_props, scratch_pool));

      new_props = svn_prop_array_to_hash(regular_props, scratch_pool);

      svn_hash_sets(new_props, SVN_PROP_MERGEINFO, NULL);

      /* ### What is the easiest way to set new_props on LOCAL_ABSPATH?

         ### This doesn't need a merge as we just added the node
         ### (or installed a tree conflict and skipped this node)*/

      SVN_ERR(svn_wc_merge_props3(&prop_state, merge_b->ctx->wc_ctx,
                                  local_abspath,
                                  NULL, NULL,
                                  apr_hash_make(scratch_pool),
                                  svn_prop_hash_to_array(new_props,
                                                         scratch_pool),
                                  merge_b->dry_run,
                                  NULL, NULL,
                                  merge_b->ctx->cancel_func,
                                  merge_b->ctx->cancel_baton,
                                  scratch_pool));
      if (prop_state == svn_wc_notify_state_conflicted)
        {
          alloc_and_store_path(&merge_b->conflicted_paths, local_abspath,
                               merge_b->pool);
        }
    }

  return SVN_NO_ERROR;
}

/* Helper for merge_dir_deleted. Implement svn_wc_status_func4_t */
static svn_error_t *
verify_touched_by_del_check(void *baton,
                            const char *local_abspath,
                            const svn_wc_status3_t *status,
                            apr_pool_t *scratch_pool)
{
  struct dir_delete_baton_t *delb = baton;

  if (svn_hash_gets(delb->compared_abspaths, local_abspath))
    return SVN_NO_ERROR;

  switch (status->node_status)
    {
      case svn_wc_status_deleted:
      case svn_wc_status_ignored:
      case svn_wc_status_none:
        return SVN_NO_ERROR;

      default:
        delb->found_edit = TRUE;
        return svn_error_create(SVN_ERR_CEASE_INVOCATION, NULL, NULL);
    }
}

/* An svn_diff_tree_processor_t function.
 *
 * Called after merge_dir_opened() when a node existed only in the left source.
 *
 * After the merge_dir_opened() but before the call to this merge_dir_deleted()
 * function all descendants that existed in left_source will have been deleted.
 *
 * If this node is replaced, an _opened() followed by a matching _add() will
 * be invoked after this function.
 */
static svn_error_t *
merge_dir_deleted(const char *relpath,
                  const svn_diff_source_t *left_source,
                  /*const*/ apr_hash_t *left_props,
                  void *dir_baton,
                  const struct svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  merge_cmd_baton_t *merge_b = processor->baton;
  struct merge_dir_baton_t *db = dir_baton;
  const char *local_abspath = svn_dirent_join(merge_b->target->abspath,
                                              relpath, scratch_pool);
  svn_boolean_t same;
  apr_hash_t *working_props;

  SVN_ERR(handle_pending_notifications(merge_b, db, scratch_pool));
  SVN_ERR(mark_dir_edited(merge_b, db, local_abspath, scratch_pool));

  if (db->shadowed)
    {
      if (db->tree_conflict_reason == CONFLICT_REASON_NONE)
        {
          /* We haven't notified for this node yet: report a skip */
          SVN_ERR(record_skip(merge_b, local_abspath, svn_node_dir,
                              svn_wc_notify_update_shadowed_delete,
                              db->skip_reason, db->parent_baton,
                              scratch_pool));
        }

      return SVN_NO_ERROR;
    }

  /* Easy out: We are only applying mergeinfo differences. */
  if (merge_b->record_only)
    {
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc_prop_list2(&working_props,
                            merge_b->ctx->wc_ctx, local_abspath,
                            scratch_pool, scratch_pool));

  if (merge_b->force_delete)
    {
      /* In this legacy mode we just assume that a directory delete
         matches any directory. db->delete_state is NULL */
      same = TRUE;
    }
  else
    {
      struct dir_delete_baton_t *delb;

      /* Compare the properties */
      SVN_ERR(properties_same_p(&same, left_props, working_props,
                                scratch_pool));
      delb = db->delete_state;
      assert(delb != NULL);

      if (! same)
        {
          delb->found_edit = TRUE;
        }
      else
        {
          store_path(delb->compared_abspaths, local_abspath);
        }

      if (delb->del_root != db)
        return SVN_NO_ERROR;

      if (delb->found_edit)
        same = FALSE;
      else
        {
          apr_array_header_t *ignores;
          svn_error_t *err;
          same = TRUE;

          SVN_ERR(svn_wc_get_default_ignores(&ignores, merge_b->ctx->config,
                                             scratch_pool));

          /* None of the descendants was modified, but maybe there are
             descendants we haven't walked?

             Note that we aren't interested in changes, as we already verified
             changes in the paths touched by the merge. And the existence of
             other paths is enough to mark the directory edited */
          err = svn_wc_walk_status(merge_b->ctx->wc_ctx, local_abspath,
                                   svn_depth_infinity, TRUE /* get-all */,
                                   FALSE /* no-ignore */,
                                   TRUE /* ignore-text-mods */, ignores,
                                   verify_touched_by_del_check, delb,
                                   merge_b->ctx->cancel_func,
                                   merge_b->ctx->cancel_baton,
                                   scratch_pool);

          if (err)
            {
              if (err->apr_err != SVN_ERR_CEASE_INVOCATION)
                return svn_error_trace(err);

              svn_error_clear(err);
            }

          same = ! delb->found_edit;
        }
    }

  if (same && !merge_b->dry_run)
    {
      svn_error_t *err;

      err = svn_wc_delete4(merge_b->ctx->wc_ctx, local_abspath,
                           FALSE /* keep_local */, FALSE /* unversioned */,
                           merge_b->ctx->cancel_func,
                           merge_b->ctx->cancel_baton,
                           NULL, NULL /* no notify */,
                           scratch_pool);

      if (err)
        {
          if (err->apr_err != SVN_ERR_WC_LEFT_LOCAL_MOD)
            return svn_error_trace(err);

          svn_error_clear(err);
          same = FALSE;
        }
    }

  if (! same)
    {
      /* If the attempt to delete an existing directory failed,
       * the directory has local modifications (e.g. locally added
       * files, or property changes). Flag a tree conflict. */

      /* This handles use case 5 described in the paper attached to issue
       * #2282.  See also notes/tree-conflicts/detection.txt
       */
      SVN_ERR(record_tree_conflict(merge_b, local_abspath, db->parent_baton,
                                   svn_node_dir,
                                   svn_node_dir,
                                   svn_node_none,
                                   svn_wc_conflict_action_delete,
                                   svn_wc_conflict_reason_edited,
                                   NULL, TRUE,
                                   scratch_pool));
    }
  else
    {
      /* Record that we might have deleted mergeinfo */
      if (working_props
          && svn_hash_gets(working_props, SVN_PROP_MERGEINFO))
        {
          alloc_and_store_path(&merge_b->paths_with_deleted_mergeinfo,
                               local_abspath, merge_b->pool);
        }

      SVN_ERR(record_update_delete(merge_b, db->parent_baton, local_abspath,
                                   svn_node_dir, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t function.
 *
 * Called after merge_dir_opened() when a node itself didn't change between
 * the left and right source.
 *
 * After the merge_dir_opened() but before the call to this merge_dir_closed()
 * function all descendants will have been processed.
 */
static svn_error_t *
merge_dir_closed(const char *relpath,
                 const svn_diff_source_t *left_source,
                 const svn_diff_source_t *right_source,
                 void *dir_baton,
                 const struct svn_diff_tree_processor_t *processor,
                 apr_pool_t *scratch_pool)
{
  merge_cmd_baton_t *merge_b = processor->baton;
  struct merge_dir_baton_t *db = dir_baton;

  SVN_ERR(handle_pending_notifications(merge_b, db, scratch_pool));

  return SVN_NO_ERROR;
}

/* An svn_diff_tree_processor_t function.

   Called when the diff driver wants to report an absent path.

   In case of merges this happens when the diff encounters a server-excluded
   path.

   We register a skipped path, which will make parent mergeinfo non-
   inheritable. This ensures that a future merge might see these skipped
   changes as eligable for merging.

   For legacy reasons we also notify the path as skipped.
 */
static svn_error_t *
merge_node_absent(const char *relpath,
                  void *dir_baton,
                  const svn_diff_tree_processor_t *processor,
                  apr_pool_t *scratch_pool)
{
  merge_cmd_baton_t *merge_b = processor->baton;
  struct merge_dir_baton_t *db = dir_baton;

  const char *local_abspath = svn_dirent_join(merge_b->target->abspath,
                                              relpath, scratch_pool);

  SVN_ERR(record_skip(merge_b, local_abspath, svn_node_unknown,
                      svn_wc_notify_skip, svn_wc_notify_state_missing,
                      db, scratch_pool));

  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------------*/

/*** Merge Notification ***/


/* Finds a nearest ancestor in CHILDREN_WITH_MERGEINFO for LOCAL_ABSPATH. If
   PATH_IS_OWN_ANCESTOR is TRUE then a child in CHILDREN_WITH_MERGEINFO
   where child->abspath == PATH is considered PATH's ancestor.  If FALSE,
   then child->abspath must be a proper ancestor of PATH.

   CHILDREN_WITH_MERGEINFO is expected to be sorted in Depth first
   order of path. */
static svn_client__merge_path_t *
find_nearest_ancestor(const apr_array_header_t *children_with_mergeinfo,
                      svn_boolean_t path_is_own_ancestor,
                      const char *local_abspath)
{
  int i;

  SVN_ERR_ASSERT_NO_RETURN(children_with_mergeinfo != NULL);

  for (i = children_with_mergeinfo->nelts - 1; i >= 0 ; i--)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);

      if (svn_dirent_is_ancestor(child->abspath, local_abspath)
          && (path_is_own_ancestor
              || strcmp(child->abspath, local_abspath) != 0))
        return child;
    }
  return NULL;
}

/* Find the highest level path in a merge target (possibly the merge target
   itself) to use in a merge notification header.

   Return the svn_client__merge_path_t * representing the most distant
   ancestor in CHILDREN_WITH_MERGEINFO of LOCAL_ABSPATH where said
   ancestor's first remaining ranges element (per the REMAINING_RANGES
   member of the ancestor) intersect with the first remaining ranges element
   for every intermediate ancestor svn_client__merge_path_t * of
   LOCAL_ABSPATH.  If no such ancestor is found return NULL.

   If the remaining ranges of the elements in CHILDREN_WITH_MERGEINFO
   represent a forward merge, then set *START to the oldest revision found
   in any of the intersecting ancestors and *END to the youngest revision
   found.  If the remaining ranges of the elements in CHILDREN_WITH_MERGEINFO
   represent a reverse merge, then set *START to the youngest revision
   found and *END to the oldest revision found.  If no ancestors are found
   then set *START and *END to SVN_INVALID_REVNUM.

   If PATH_IS_OWN_ANCESTOR is TRUE then a child in CHILDREN_WITH_MERGEINFO
   where child->abspath == PATH is considered PATH's ancestor.  If FALSE,
   then child->abspath must be a proper ancestor of PATH.

   See the CHILDREN_WITH_MERGEINFO ARRAY global comment for more
   information. */
static svn_client__merge_path_t *
find_nearest_ancestor_with_intersecting_ranges(
  svn_revnum_t *start,
  svn_revnum_t *end,
  const apr_array_header_t *children_with_mergeinfo,
  svn_boolean_t path_is_own_ancestor,
  const char *local_abspath)
{
  int i;
  svn_client__merge_path_t *nearest_ancestor = NULL;

  *start = SVN_INVALID_REVNUM;
  *end = SVN_INVALID_REVNUM;

  SVN_ERR_ASSERT_NO_RETURN(children_with_mergeinfo != NULL);

  for (i = children_with_mergeinfo->nelts - 1; i >= 0 ; i--)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);

      if (svn_dirent_is_ancestor(child->abspath, local_abspath)
          && (path_is_own_ancestor
              || strcmp(child->abspath, local_abspath) != 0))
        {
          if (nearest_ancestor == NULL)
            {
              /* Found an ancestor. */
              nearest_ancestor = child;

              if (child->remaining_ranges)
                {
                  svn_merge_range_t *r1 = APR_ARRAY_IDX(
                    child->remaining_ranges, 0, svn_merge_range_t *);
                  *start = r1->start;
                  *end = r1->end;
                }
              else
                {
                  /* If CHILD->REMAINING_RANGES is null then LOCAL_ABSPATH
                     is inside an absent subtree in the merge target. */
                  *start = SVN_INVALID_REVNUM;
                  *end = SVN_INVALID_REVNUM;
                  break;
                }
            }
          else
            {
              /* We'e found another ancestor for LOCAL_ABSPATH.  Do its
                 first remaining range intersect with the previously
                 found ancestor? */
              svn_merge_range_t *r1 =
                APR_ARRAY_IDX(nearest_ancestor->remaining_ranges, 0,
                              svn_merge_range_t *);
              svn_merge_range_t *r2 =
                APR_ARRAY_IDX(child->remaining_ranges, 0,
                              svn_merge_range_t *);

              if (r1 && r2)
                {
                  svn_merge_range_t range1;
                  svn_merge_range_t range2;
                  svn_boolean_t reverse_merge = r1->start > r2->end;

                  /* Flip endpoints if this is a reverse merge. */
                  if (reverse_merge)
                    {
                      range1.start = r1->end;
                      range1.end = r1->start;
                      range2.start = r2->end;
                      range2.end = r2->start;
                    }
                  else
                    {
                      range1.start = r1->start;
                      range1.end = r1->end;
                      range2.start = r2->start;
                      range2.end = r2->end;
                    }

                  if (range1.start < range2.end && range2.start < range1.end)
                    {
                      *start = reverse_merge ?
                        MAX(r1->start, r2->start) : MIN(r1->start, r2->start);
                      *end = reverse_merge ?
                        MIN(r1->end, r2->end) : MAX(r1->end, r2->end);
                      nearest_ancestor = child;
                    }
                }
            }
        }
    }
  return nearest_ancestor;
}

/* Notify that we're starting to record mergeinfo for the merge of the
 * revision range RANGE into TARGET_ABSPATH.  RANGE should be null if the
 * merge sources are not from the same URL.
 *
 * This calls the client's notification receiver (as found in the client
 * context), with a WC abspath.
 */
static void
notify_mergeinfo_recording(const char *target_abspath,
                           const svn_merge_range_t *range,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool)
{
  if (ctx->notify_func2)
    {
      svn_wc_notify_t *n = svn_wc_create_notify(
        target_abspath, svn_wc_notify_merge_record_info_begin, pool);

      n->merge_range = range ? svn_merge_range_dup(range, pool) : NULL;
      ctx->notify_func2(ctx->notify_baton2, n, pool);
    }
}

/* Notify that we're completing the merge into TARGET_ABSPATH.
 *
 * This calls the client's notification receiver (as found in the client
 * context), with a WC abspath.
 */
static void
notify_merge_completed(const char *target_abspath,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *pool)
{
  if (ctx->notify_func2)
    {
      svn_wc_notify_t *n
        = svn_wc_create_notify(target_abspath, svn_wc_notify_merge_completed,
                               pool);
      ctx->notify_func2(ctx->notify_baton2, n, pool);
    }
}

/* Is the notification the result of a real operative merge? */
#define IS_OPERATIVE_NOTIFICATION(notify)  \
                    (notify->content_state == svn_wc_notify_state_conflicted \
                     || notify->content_state == svn_wc_notify_state_merged  \
                     || notify->content_state == svn_wc_notify_state_changed \
                     || notify->prop_state == svn_wc_notify_state_conflicted \
                     || notify->prop_state == svn_wc_notify_state_merged     \
                     || notify->prop_state == svn_wc_notify_state_changed    \
                     || notify->action == svn_wc_notify_update_add \
                     || notify->action == svn_wc_notify_tree_conflict)


/* Remove merge source gaps from range used for merge notifications.
   See http://subversion.tigris.org/issues/show_bug.cgi?id=4138

   If IMPLICIT_SRC_GAP is not NULL then it is a rangelist containing a
   single range (see the implicit_src_gap member of merge_cmd_baton_t).
   RANGE describes a (possibly reverse) merge.

   If IMPLICIT_SRC_GAP is not NULL and it's sole range intersects with
   the older revision in *RANGE, then remove IMPLICIT_SRC_GAP's range
   from *RANGE. */
static void
remove_source_gap(svn_merge_range_t *range,
                  apr_array_header_t *implicit_src_gap)
{
  if (implicit_src_gap)
    {
      svn_merge_range_t *gap_range =
        APR_ARRAY_IDX(implicit_src_gap, 0, svn_merge_range_t *);
      if (range->start < range->end)
        {
          if (gap_range->start == range->start)
            range->start = gap_range->end;
        }
      else /* Reverse merge */
        {
          if (gap_range->start == range->end)
            range->end = gap_range->end;
        }
    }
}

/* Notify that we're starting a merge
 *
 * This calls the client's notification receiver (as found in the client
 * context), with a WC abspath.
 */
static svn_error_t *
notify_merge_begin(merge_cmd_baton_t *merge_b,
                   const char *local_abspath,
                   svn_boolean_t delete_action,
                   apr_pool_t *scratch_pool)
{
  svn_wc_notify_t *notify;
  svn_merge_range_t n_range =
    {SVN_INVALID_REVNUM, SVN_INVALID_REVNUM, TRUE};
  const char *notify_abspath;

  if (! merge_b->ctx->notify_func2)
    return SVN_NO_ERROR;

  /* If our merge sources are ancestors of one another... */
  if (merge_b->merge_source.ancestral)
    {
      const svn_client__merge_path_t *child;
      /* Find NOTIFY->PATH's nearest ancestor in
         NOTIFY->CHILDREN_WITH_MERGEINFO.  Normally we consider a child in
         NOTIFY->CHILDREN_WITH_MERGEINFO representing PATH to be an
         ancestor of PATH, but if this is a deletion of PATH then the
         notification must be for a proper ancestor of PATH.  This ensures
         we don't get notifications like:

           --- Merging rX into 'PARENT/CHILD'
           D    PARENT/CHILD

         But rather:

           --- Merging rX into 'PARENT'
           D    PARENT/CHILD
      */

      child = find_nearest_ancestor_with_intersecting_ranges(
        &(n_range.start), &(n_range.end),
        merge_b->notify_begin.nodes_with_mergeinfo,
        ! delete_action, local_abspath);

      if (!child && delete_action)
        {
          /* Triggered by file replace in single-file-merge */
          child = find_nearest_ancestor(merge_b->notify_begin.nodes_with_mergeinfo,
                                        TRUE, local_abspath);
        }

      assert(child != NULL); /* Should always find the merge anchor */

      if (! child)
        return SVN_NO_ERROR;

      if (merge_b->notify_begin.last_abspath != NULL
          && strcmp(child->abspath, merge_b->notify_begin.last_abspath) == 0)
        {
          /* Don't notify the same merge again */
          return SVN_NO_ERROR;
        }

      merge_b->notify_begin.last_abspath = child->abspath;

      if (child->absent || child->remaining_ranges->nelts == 0
          || !SVN_IS_VALID_REVNUM(n_range.start))
        {
          /* No valid information for an header */
          return SVN_NO_ERROR;
        }

      notify_abspath = child->abspath;
    }
  else
    {
      if (merge_b->notify_begin.last_abspath)
        return SVN_NO_ERROR; /* already notified */

      notify_abspath = merge_b->target->abspath;
      /* Store something in last_abspath. Any value would do */
      merge_b->notify_begin.last_abspath = merge_b->target->abspath;
    }

  notify = svn_wc_create_notify(notify_abspath,
                                merge_b->same_repos
                                      ? svn_wc_notify_merge_begin
                                      : svn_wc_notify_foreign_merge_begin,
                                scratch_pool);

  if (SVN_IS_VALID_REVNUM(n_range.start))
    {
      /* If the merge source has a gap, then don't mention
         those gap revisions in the notification. */
      remove_source_gap(&n_range, merge_b->implicit_src_gap);
      notify->merge_range = &n_range;
    }
  else
    {
      notify->merge_range = NULL;
    }

  merge_b->ctx->notify_func2(merge_b->ctx->notify_baton2, notify,
                             scratch_pool);

  return SVN_NO_ERROR;
}

/* Set *OUT_RANGELIST to the intersection of IN_RANGELIST with the simple
 * (inheritable) revision range REV1:REV2, according to CONSIDER_INHERITANCE.
 * If REV1 is equal to REV2, the result is an empty rangelist, otherwise
 * REV1 must be less than REV2.
 *
 * Note: If CONSIDER_INHERITANCE is FALSE, the effect is to treat any non-
 * inheritable input ranges as if they were inheritable.  If it is TRUE, the
 * effect is to discard any non-inheritable input ranges.  Therefore the
 * ranges in *OUT_RANGELIST will always be inheritable. */
static svn_error_t *
rangelist_intersect_range(svn_rangelist_t **out_rangelist,
                          const svn_rangelist_t *in_rangelist,
                          svn_revnum_t rev1,
                          svn_revnum_t rev2,
                          svn_boolean_t consider_inheritance,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(rev1 <= rev2);

  if (rev1 < rev2)
    {
      svn_rangelist_t *simple_rangelist =
        svn_rangelist__initialize(rev1, rev2, TRUE, scratch_pool);

      SVN_ERR(svn_rangelist_intersect(out_rangelist,
                                      simple_rangelist, in_rangelist,
                                      consider_inheritance, result_pool));
    }
  else
    {
      *out_rangelist = apr_array_make(result_pool, 0,
                                      sizeof(svn_merge_range_t *));
    }
  return SVN_NO_ERROR;
}

/* Helper for fix_deleted_subtree_ranges().  Like fix_deleted_subtree_ranges()
   this function should only be called when honoring mergeinfo.

   CHILD, PARENT, REVISION1, REVISION2, and RA_SESSION are all cascaded from
   fix_deleted_subtree_ranges() -- see that function for more information on
   each.

   If PARENT is not the merge target then PARENT must have already have been
   processed by this function as a child.  Specifically, this means that
   PARENT->REMAINING_RANGES must already be populated -- it can be an empty
   rangelist but cannot be NULL.

   PRIMARY_URL is the merge source url of CHILD at the younger of REVISION1
   and REVISION2.

   Since this function is only invoked for subtrees of the merge target, the
   guarantees afforded by normalize_merge_sources() don't apply - see the
   'MERGEINFO MERGE SOURCE NORMALIZATION' comment at the top of this file.
   Therefore it is possible that PRIMARY_URL@REVISION1 and
   PRIMARY_URL@REVISION2 don't describe the endpoints of an unbroken line of
   history.  The purpose of this helper is to identify these cases of broken
   history and adjust CHILD->REMAINING_RANGES in such a way we don't later try
   to describe nonexistent path/revisions to the merge report editor -- see
   drive_merge_report_editor().

   If PRIMARY_URL@REVISION1 and PRIMARY_URL@REVISION2 describe an unbroken
   line of history then do nothing and leave CHILD->REMAINING_RANGES as-is.

   If neither PRIMARY_URL@REVISION1 nor PRIMARY_URL@REVISION2 exist then
   there is nothing to merge to CHILD->ABSPATH so set CHILD->REMAINING_RANGES
   equal to PARENT->REMAINING_RANGES.  This will cause the subtree to
   effectively ignore CHILD -- see 'Note: If the first svn_merge_range_t...'
   in drive_merge_report_editor()'s doc string.

   If PRIMARY_URL@REVISION1 *xor* PRIMARY_URL@REVISION2 exist then we take the
   subset of REVISION1:REVISION2 in CHILD->REMAINING_RANGES at which
   PRIMARY_URL doesn't exist and set that subset equal to
   PARENT->REMAINING_RANGES' intersection with that non-existent range.  Why?
   Because this causes CHILD->REMAINING_RANGES to be identical to
   PARENT->REMAINING_RANGES for revisions between REVISION1 and REVISION2 at
   which PRIMARY_URL doesn't exist.  As mentioned above this means that
   drive_merge_report_editor() won't attempt to describe these non-existent
   subtree path/ranges to the reporter (which would break the merge).

   If the preceding paragraph wasn't terribly clear then what follows spells
   out this function's behavior a bit more explicitly:

   For forward merges (REVISION1 < REVISION2)

     If PRIMARY_URL@REVISION1 exists but PRIMARY_URL@REVISION2 doesn't, then
     find the revision 'N' in which PRIMARY_URL@REVISION1 was deleted.  Leave
     the subset of CHILD->REMAINING_RANGES that intersects with
     REVISION1:(N - 1) as-is and set the subset of CHILD->REMAINING_RANGES
     that intersects with (N - 1):REVISION2 equal to PARENT->REMAINING_RANGES'
     intersection with (N - 1):REVISION2.

     If PRIMARY_URL@REVISION1 doesn't exist but PRIMARY_URL@REVISION2 does,
     then find the revision 'M' in which PRIMARY_URL@REVISION2 came into
     existence.  Leave the subset of CHILD->REMAINING_RANGES that intersects with
     (M - 1):REVISION2 as-is and set the subset of CHILD->REMAINING_RANGES
     that intersects with REVISION1:(M - 1) equal to PARENT->REMAINING_RANGES'
     intersection with REVISION1:(M - 1).

   For reverse merges (REVISION1 > REVISION2)

     If PRIMARY_URL@REVISION1 exists but PRIMARY_URL@REVISION2 doesn't, then
     find the revision 'N' in which PRIMARY_URL@REVISION1 came into existence.
     Leave the subset of CHILD->REMAINING_RANGES that intersects with
     REVISION2:(N - 1) as-is and set the subset of CHILD->REMAINING_RANGES
     that intersects with (N - 1):REVISION1 equal to PARENT->REMAINING_RANGES'
     intersection with (N - 1):REVISION1.

     If PRIMARY_URL@REVISION1 doesn't exist but PRIMARY_URL@REVISION2 does,
     then find the revision 'M' in which PRIMARY_URL@REVISION2 came into
     existence.  Leave the subset of CHILD->REMAINING_RANGES that intersects with
     REVISION2:(M - 1) as-is and set the subset of CHILD->REMAINING_RANGES
     that intersects with (M - 1):REVISION1 equal to PARENT->REMAINING_RANGES'
     intersection with REVISION1:(M - 1).

   SCRATCH_POOL is used for all temporary allocations.  Changes to CHILD are
   allocated in RESULT_POOL. */
static svn_error_t *
adjust_deleted_subtree_ranges(svn_client__merge_path_t *child,
                              svn_client__merge_path_t *parent,
                              svn_revnum_t revision1,
                              svn_revnum_t revision2,
                              const char *primary_url,
                              svn_ra_session_t *ra_session,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_boolean_t is_rollback = revision2 < revision1;
  svn_revnum_t younger_rev = is_rollback ? revision1 : revision2;
  svn_revnum_t peg_rev = younger_rev;
  svn_revnum_t older_rev = is_rollback ? revision2 : revision1;
  apr_array_header_t *segments;
  svn_error_t *err;

  SVN_ERR_ASSERT(parent->remaining_ranges);

  err = svn_client__repos_location_segments(&segments, ra_session,
                                            primary_url, peg_rev,
                                            younger_rev, older_rev, ctx,
                                            scratch_pool);

  if (err)
    {
      const char *rel_source_path;  /* PRIMARY_URL relative to RA_SESSION */
      svn_node_kind_t kind;

      if (err->apr_err != SVN_ERR_FS_NOT_FOUND)
        return svn_error_trace(err);

      svn_error_clear(err);

      /* PRIMARY_URL@peg_rev doesn't exist.  Check if PRIMARY_URL@older_rev
         exists, if neither exist then the editor can simply ignore this
         subtree. */

      SVN_ERR(svn_ra_get_path_relative_to_session(
                ra_session, &rel_source_path, primary_url, scratch_pool));

      SVN_ERR(svn_ra_check_path(ra_session, rel_source_path,
                                older_rev, &kind, scratch_pool));
      if (kind == svn_node_none)
        {
          /* Neither PRIMARY_URL@peg_rev nor PRIMARY_URL@older_rev exist,
             so there is nothing to merge.  Set CHILD->REMAINING_RANGES
             identical to PARENT's. */
          child->remaining_ranges =
            svn_rangelist_dup(parent->remaining_ranges, scratch_pool);
        }
      else
        {
          svn_rangelist_t *deleted_rangelist;
          svn_revnum_t rev_primary_url_deleted;

          /* PRIMARY_URL@older_rev exists, so it was deleted at some
             revision prior to peg_rev, find that revision. */
          SVN_ERR(svn_ra_get_deleted_rev(ra_session, rel_source_path,
                                         older_rev, younger_rev,
                                         &rev_primary_url_deleted,
                                         scratch_pool));

          /* PRIMARY_URL@older_rev exists and PRIMARY_URL@peg_rev doesn't,
             so svn_ra_get_deleted_rev() should always find the revision
             PRIMARY_URL@older_rev was deleted. */
          SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(rev_primary_url_deleted));

          /* If this is a reverse merge reorder CHILD->REMAINING_RANGES and
             PARENT->REMAINING_RANGES so both will work with the
             svn_rangelist_* APIs below. */
          if (is_rollback)
            {
              /* svn_rangelist_reverse operates in place so it's safe
                 to use our scratch_pool. */
              SVN_ERR(svn_rangelist_reverse(child->remaining_ranges,
                                            scratch_pool));
              SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges,
                                            scratch_pool));
            }

          /* Find the intersection of CHILD->REMAINING_RANGES with the
             range over which PRIMARY_URL@older_rev exists (ending at
             the youngest revision at which it still exists). */
          SVN_ERR(rangelist_intersect_range(&child->remaining_ranges,
                                            child->remaining_ranges,
                                            older_rev,
                                            rev_primary_url_deleted - 1,
                                            FALSE,
                                            scratch_pool, scratch_pool));

          /* Merge into CHILD->REMAINING_RANGES the intersection of
             PARENT->REMAINING_RANGES with the range beginning when
             PRIMARY_URL@older_rev was deleted until younger_rev. */
          SVN_ERR(rangelist_intersect_range(&deleted_rangelist,
                                            parent->remaining_ranges,
                                            rev_primary_url_deleted - 1,
                                            peg_rev,
                                            FALSE,
                                            scratch_pool, scratch_pool));
          SVN_ERR(svn_rangelist_merge2(child->remaining_ranges,
                                       deleted_rangelist, scratch_pool,
                                       scratch_pool));

          /* Return CHILD->REMAINING_RANGES and PARENT->REMAINING_RANGES
             to reverse order if necessary. */
          if (is_rollback)
            {
              SVN_ERR(svn_rangelist_reverse(child->remaining_ranges,
                                            scratch_pool));
              SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges,
                                            scratch_pool));
            }
        }
    }
  else /* PRIMARY_URL@peg_rev exists. */
    {
      svn_rangelist_t *non_existent_rangelist;
      svn_location_segment_t *segment =
        APR_ARRAY_IDX(segments, (segments->nelts - 1),
                      svn_location_segment_t *);

      /* We know PRIMARY_URL@peg_rev exists as the call to
         svn_client__repos_location_segments() succeeded.  If there is only
         one segment that starts at oldest_rev then we know that
         PRIMARY_URL@oldest_rev:PRIMARY_URL@peg_rev describes an unbroken
         line of history, so there is nothing more to adjust in
         CHILD->REMAINING_RANGES. */
      if (segment->range_start == older_rev)
        {
          return SVN_NO_ERROR;
        }

      /* If this is a reverse merge reorder CHILD->REMAINING_RANGES and
         PARENT->REMAINING_RANGES so both will work with the
         svn_rangelist_* APIs below. */
      if (is_rollback)
        {
          SVN_ERR(svn_rangelist_reverse(child->remaining_ranges,
                                        scratch_pool));
          SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges,
                                        scratch_pool));
        }

      /* Intersect CHILD->REMAINING_RANGES with the range where PRIMARY_URL
         exists.  Since segment doesn't span older_rev:peg_rev we know
         PRIMARY_URL@peg_rev didn't come into existence until
         segment->range_start + 1. */
      SVN_ERR(rangelist_intersect_range(&child->remaining_ranges,
                                        child->remaining_ranges,
                                        segment->range_start, peg_rev,
                                        FALSE, scratch_pool, scratch_pool));

      /* Merge into CHILD->REMAINING_RANGES the intersection of
         PARENT->REMAINING_RANGES with the range before PRIMARY_URL@peg_rev
         came into existence. */
      SVN_ERR(rangelist_intersect_range(&non_existent_rangelist,
                                        parent->remaining_ranges,
                                        older_rev, segment->range_start,
                                        FALSE, scratch_pool, scratch_pool));
      SVN_ERR(svn_rangelist_merge2(child->remaining_ranges,
                                   non_existent_rangelist, scratch_pool,
                                   scratch_pool));

      /* Return CHILD->REMAINING_RANGES and PARENT->REMAINING_RANGES
         to reverse order if necessary. */
      if (is_rollback)
        {
          SVN_ERR(svn_rangelist_reverse(child->remaining_ranges,
                                        scratch_pool));
          SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges,
                                        scratch_pool));
        }
    }

  /* Make a lasting copy of CHILD->REMAINING_RANGES using POOL. */
  child->remaining_ranges = svn_rangelist_dup(child->remaining_ranges,
                                              result_pool);
  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge().

   SOURCE is cascaded from the argument of the same name in
   do_directory_merge().  TARGET is the merge target.  RA_SESSION is the
   session for the younger of SOURCE->loc1 and SOURCE->loc2.

   Adjust the subtrees in CHILDREN_WITH_MERGEINFO so that we don't
   later try to describe invalid paths in drive_merge_report_editor().
   This function is just a thin wrapper around
   adjust_deleted_subtree_ranges(), which see for further details.

   SCRATCH_POOL is used for all temporary allocations.  Changes to
   CHILDREN_WITH_MERGEINFO are allocated in RESULT_POOL.
*/
static svn_error_t *
fix_deleted_subtree_ranges(const merge_source_t *source,
                           const merge_target_t *target,
                           svn_ra_session_t *ra_session,
                           apr_array_header_t *children_with_mergeinfo,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_boolean_t is_rollback = source->loc2->rev < source->loc1->rev;

  assert(session_url_is(ra_session,
                        (is_rollback ? source->loc1 : source->loc2)->url,
                        scratch_pool));

  /* CHILDREN_WITH_MERGEINFO is sorted in depth-first order, so
     start at index 1 to examine only subtrees. */
  for (i = 1; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);
      svn_client__merge_path_t *parent;
      svn_rangelist_t *deleted_rangelist, *added_rangelist;

      SVN_ERR_ASSERT(child);
      if (child->absent)
        continue;

      svn_pool_clear(iterpool);

      /* Find CHILD's parent. */
      parent = find_nearest_ancestor(children_with_mergeinfo,
                                     FALSE, child->abspath);

      /* Since CHILD is a subtree then its parent must be in
         CHILDREN_WITH_MERGEINFO, see the global comment
         'THE CHILDREN_WITH_MERGEINFO ARRAY'. */
      SVN_ERR_ASSERT(parent);

      /* If this is a reverse merge reorder CHILD->REMAINING_RANGES
         so it will work with the svn_rangelist_diff API. */
      if (is_rollback)
        {
          SVN_ERR(svn_rangelist_reverse(child->remaining_ranges, iterpool));
          SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges, iterpool));
        }

      SVN_ERR(svn_rangelist_diff(&deleted_rangelist, &added_rangelist,
                                 child->remaining_ranges,
                                 parent->remaining_ranges,
                                 TRUE, iterpool));

      if (is_rollback)
        {
          SVN_ERR(svn_rangelist_reverse(child->remaining_ranges, iterpool));
          SVN_ERR(svn_rangelist_reverse(parent->remaining_ranges, iterpool));
        }

      /* If CHILD is the merge target we then know that SOURCE is provided
         by normalize_merge_sources() -- see 'MERGEINFO MERGE SOURCE
         NORMALIZATION'.  Due to this normalization we know that SOURCE
         describes an unbroken line of history such that the entire range
         described by SOURCE can potentially be merged to CHILD.

         But if CHILD is a subtree we don't have the same guarantees about
         SOURCE as we do for the merge target.  SOURCE->loc1 and/or
         SOURCE->loc2 might not exist.

         If one or both doesn't exist, then adjust CHILD->REMAINING_RANGES
         such that we don't later try to describe invalid subtrees in
         drive_merge_report_editor(), as that will break the merge.
         If CHILD has the same remaining ranges as PARENT however, then
         there is no need to make these adjustments, since
         drive_merge_report_editor() won't attempt to describe CHILD in this
         case, see the 'Note' in drive_merge_report_editor's docstring. */
      if (deleted_rangelist->nelts || added_rangelist->nelts)
        {
          const char *child_primary_source_url;
          const char *child_repos_src_path =
            svn_dirent_is_child(target->abspath, child->abspath, iterpool);

          /* This loop is only processing subtrees, so CHILD->ABSPATH
             better be a proper child of the merge target. */
          SVN_ERR_ASSERT(child_repos_src_path);

          child_primary_source_url =
            svn_path_url_add_component2((source->loc1->rev < source->loc2->rev)
                                        ? source->loc2->url : source->loc1->url,
                                        child_repos_src_path, iterpool);

          SVN_ERR(adjust_deleted_subtree_ranges(child, parent,
                                                source->loc1->rev,
                                                source->loc2->rev,
                                                child_primary_source_url,
                                                ra_session,
                                                ctx, result_pool, iterpool));
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------------*/

/*** Determining What Remains To Be Merged ***/

/* Get explicit and/or implicit mergeinfo for the working copy path
   TARGET_ABSPATH.

   If RECORDED_MERGEINFO is not NULL then set *RECORDED_MERGEINFO
   to TARGET_ABSPATH's explicit or inherited mergeinfo as dictated by
   INHERIT.

   If IMPLICIT_MERGEINFO is not NULL then set *IMPLICIT_MERGEINFO
   to TARGET_ABSPATH's implicit mergeinfo (a.k.a. natural history).

   If both RECORDED_MERGEINFO and IMPLICIT_MERGEINFO are not NULL and
   *RECORDED_MERGEINFO is inherited, then *IMPLICIT_MERGEINFO will be
   removed from *RECORDED_MERGEINFO.

   If INHERITED is not NULL set *INHERITED to TRUE if *RECORDED_MERGEINFO
   is inherited rather than explicit.  If RECORDED_MERGEINFO is NULL then
   INHERITED is ignored.


   If IMPLICIT_MERGEINFO is not NULL then START and END are limits on
   the natural history sought, must both be valid revision numbers, and
   START must be greater than END.  If TARGET_ABSPATH's base revision
   is older than START, then the base revision is used as the younger
   bound in place of START.

   RA_SESSION is an RA session open to the repository in which TARGET_ABSPATH
   lives.  It may be temporarily reparented as needed by this function.

   Allocate *RECORDED_MERGEINFO and *IMPLICIT_MERGEINFO in RESULT_POOL.
   Use SCRATCH_POOL for any temporary allocations. */
static svn_error_t *
get_full_mergeinfo(svn_mergeinfo_t *recorded_mergeinfo,
                   svn_mergeinfo_t *implicit_mergeinfo,
                   svn_boolean_t *inherited,
                   svn_mergeinfo_inheritance_t inherit,
                   svn_ra_session_t *ra_session,
                   const char *target_abspath,
                   svn_revnum_t start,
                   svn_revnum_t end,
                   svn_client_ctx_t *ctx,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  /* First, we get the real mergeinfo. */
  if (recorded_mergeinfo)
    {
      SVN_ERR(svn_client__get_wc_or_repos_mergeinfo(recorded_mergeinfo,
                                                    inherited,
                                                    NULL /* from_repos */,
                                                    FALSE,
                                                    inherit, ra_session,
                                                    target_abspath,
                                                    ctx, result_pool));
    }

  if (implicit_mergeinfo)
    {
      svn_client__pathrev_t *target;

      /* Assert that we have sane input. */
      SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(start) && SVN_IS_VALID_REVNUM(end)
                     && (start > end));

      /* Retrieve the origin (original_*) of the node, or just the
         url if the node was not copied. */
      SVN_ERR(svn_client__wc_node_get_origin(&target, target_abspath, ctx,
                                             scratch_pool, scratch_pool));

      if (! target)
        {
          /* We've been asked to operate on a locally added target, so its
           * implicit mergeinfo is empty. */
          *implicit_mergeinfo = apr_hash_make(result_pool);
        }
      else if (target->rev <= end)
        {
          /* We're asking about a range outside our natural history
             altogether.  That means our implicit mergeinfo is empty. */
          *implicit_mergeinfo = apr_hash_make(result_pool);
        }
      else
        {
          /* Fetch so-called "implicit mergeinfo" (that is, natural
             history). */

          /* Do not ask for implicit mergeinfo from TARGET_ABSPATH's future.
             TARGET_ABSPATH might not even exist, and even if it does the
             working copy is *at* TARGET_REV so its implicit history ends
             at TARGET_REV! */
          if (target->rev < start)
            start = target->rev;

          /* Fetch the implicit mergeinfo. */
          SVN_ERR(svn_client__get_history_as_mergeinfo(implicit_mergeinfo,
                                                       NULL,
                                                       target, start, end,
                                                       ra_session, ctx,
                                                       result_pool));
        }
    } /*if (implicit_mergeinfo) */

  return SVN_NO_ERROR;
}

/* Helper for ensure_implicit_mergeinfo().

   PARENT, CHILD, REVISION1, REVISION2 and CTX
   are all cascaded from the arguments of the same names in
   ensure_implicit_mergeinfo().  PARENT and CHILD must both exist, i.e.
   this function should never be called where CHILD is the merge target.

   If PARENT->IMPLICIT_MERGEINFO is NULL, obtain it from the server.

   Set CHILD->IMPLICIT_MERGEINFO to the mergeinfo inherited from
   PARENT->IMPLICIT_MERGEINFO.  CHILD->IMPLICIT_MERGEINFO is allocated
   in RESULT_POOL.

   RA_SESSION is an RA session open to the repository that contains CHILD.
   It may be temporarily reparented by this function.
   */
static svn_error_t *
inherit_implicit_mergeinfo_from_parent(svn_client__merge_path_t *parent,
                                       svn_client__merge_path_t *child,
                                       svn_revnum_t revision1,
                                       svn_revnum_t revision2,
                                       svn_ra_session_t *ra_session,
                                       svn_client_ctx_t *ctx,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool)
{
  const char *path_diff;

  /* This only works on subtrees! */
  SVN_ERR_ASSERT(parent);
  SVN_ERR_ASSERT(child);

  /* While PARENT must exist, it is possible we've deferred
     getting its implicit mergeinfo.  If so get it now. */
  if (!parent->implicit_mergeinfo)
    SVN_ERR(get_full_mergeinfo(NULL, &(parent->implicit_mergeinfo),
                               NULL, svn_mergeinfo_inherited,
                               ra_session, child->abspath,
                               MAX(revision1, revision2),
                               MIN(revision1, revision2),
                               ctx, result_pool, scratch_pool));

  /* Let CHILD inherit PARENT's implicit mergeinfo. */

  path_diff = svn_dirent_is_child(parent->abspath, child->abspath,
                                  scratch_pool);
  /* PARENT->PATH better be an ancestor of CHILD->ABSPATH! */
  SVN_ERR_ASSERT(path_diff);

  SVN_ERR(svn_mergeinfo__add_suffix_to_mergeinfo(
            &child->implicit_mergeinfo, parent->implicit_mergeinfo,
            path_diff, result_pool, scratch_pool));
  child->implicit_mergeinfo = svn_mergeinfo_dup(child->implicit_mergeinfo,
                                                result_pool);
  return SVN_NO_ERROR;
}

/* Helper of filter_merged_revisions().

   If we have deferred obtaining CHILD->IMPLICIT_MERGEINFO, then get
   it now, allocating it in RESULT_POOL.  If CHILD_INHERITS_PARENT is true
   then set CHILD->IMPLICIT_MERGEINFO to the mergeinfo inherited from
   PARENT->IMPLICIT_MERGEINFO, otherwise contact the repository.  Use
   SCRATCH_POOL for all temporary allocations.

   RA_SESSION is an RA session open to the repository that contains CHILD.
   It may be temporarily reparented by this function.

   PARENT, CHILD, REVISION1, REVISION2 and
   CTX are all cascaded from the arguments of the same name in
   filter_merged_revisions() and the same conditions for that function
   hold here. */
static svn_error_t *
ensure_implicit_mergeinfo(svn_client__merge_path_t *parent,
                          svn_client__merge_path_t *child,
                          svn_boolean_t child_inherits_parent,
                          svn_revnum_t revision1,
                          svn_revnum_t revision2,
                          svn_ra_session_t *ra_session,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  /* If we haven't already found CHILD->IMPLICIT_MERGEINFO then
     contact the server to get it. */

  if (child->implicit_mergeinfo)
    return SVN_NO_ERROR;

  if (child_inherits_parent)
    SVN_ERR(inherit_implicit_mergeinfo_from_parent(parent,
                                                   child,
                                                   revision1,
                                                   revision2,
                                                   ra_session,
                                                   ctx,
                                                   result_pool,
                                                   scratch_pool));
  else
    SVN_ERR(get_full_mergeinfo(NULL,
                               &(child->implicit_mergeinfo),
                               NULL, svn_mergeinfo_inherited,
                               ra_session, child->abspath,
                               MAX(revision1, revision2),
                               MIN(revision1, revision2),
                               ctx, result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Helper for calculate_remaining_ranges().

   Initialize CHILD->REMAINING_RANGES to a rangelist representing the
   requested merge of REVISION1:REVISION2 from MERGEINFO_PATH to
   CHILD->ABSPATH.

   For forward merges remove any ranges from CHILD->REMAINING_RANGES that
   have already been merged to CHILD->ABSPATH per TARGET_MERGEINFO or
   CHILD->IMPLICIT_MERGEINFO.  For reverse merges remove any ranges from
   CHILD->REMAINING_RANGES that have not already been merged to CHILD->ABSPATH
   per TARGET_MERGEINFO or CHILD->IMPLICIT_MERGEINFO.  If we have deferred
   obtaining CHILD->IMPLICIT_MERGEINFO and it is necessary to use it for
   these calculations, then get it from the server, allocating it in
   RESULT_POOL.

   CHILD represents a working copy path which is the merge target or one of
   the target's subtrees.  If not NULL, PARENT is CHILD's nearest path-wise
   ancestor - see 'THE CHILDREN_WITH_MERGEINFO ARRAY'.

   If the function needs to consider CHILD->IMPLICIT_MERGEINFO and
   CHILD_INHERITS_IMPLICIT is true, then set CHILD->IMPLICIT_MERGEINFO to the
   mergeinfo inherited from PARENT->IMPLICIT_MERGEINFO.  Otherwise contact
   the repository for CHILD->IMPLICIT_MERGEINFO.

   NOTE: If PARENT is present then this function must have previously been
   called for PARENT, i.e. if populate_remaining_ranges() is calling this
   function for a set of svn_client__merge_path_t* the calls must be made
   in depth-first order.

   MERGEINFO_PATH is the merge source relative to the repository root.

   REVISION1 and REVISION2 describe the merge range requested from
   MERGEINFO_PATH.

   TARGET_RANGELIST is the portion of CHILD->ABSPATH's explicit or inherited
   mergeinfo that intersects with the merge history described by
   MERGEINFO_PATH@REVISION1:MERGEINFO_PATH@REVISION2.  TARGET_RANGELIST
   should be NULL if there is no explicit or inherited mergeinfo on
   CHILD->ABSPATH or an empty list if CHILD->ABSPATH has empty mergeinfo or
   explicit mergeinfo that exclusively describes non-intersecting history
   with MERGEINFO_PATH@REVISION1:MERGEINFO_PATH@REVISION2.

   SCRATCH_POOL is used for all temporary allocations.

   NOTE: This should only be called when honoring mergeinfo.

   NOTE: Like calculate_remaining_ranges() if PARENT is present then this
   function must have previously been called for PARENT.
*/
static svn_error_t *
filter_merged_revisions(svn_client__merge_path_t *parent,
                        svn_client__merge_path_t *child,
                        const char *mergeinfo_path,
                        svn_rangelist_t *target_rangelist,
                        svn_revnum_t revision1,
                        svn_revnum_t revision2,
                        svn_boolean_t child_inherits_implicit,
                        svn_ra_session_t *ra_session,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_rangelist_t *requested_rangelist,
    *target_implicit_rangelist, *explicit_rangelist;

  /* Convert REVISION1 and REVISION2 to a rangelist.

     Note: Talking about a requested merge range's inheritability
     doesn't make much sense, but as we are using svn_merge_range_t
     to describe it we need to pick *something*.  Since all the
     rangelist manipulations in this function either don't consider
     inheritance by default or we are requesting that they don't (i.e.
     svn_rangelist_remove and svn_rangelist_intersect) then we could
     set the inheritability as FALSE, it won't matter either way. */
  requested_rangelist = svn_rangelist__initialize(revision1, revision2,
                                                  TRUE, scratch_pool);

  /* Now filter out revisions that have already been merged to CHILD. */

  if (revision1 > revision2) /* This is a reverse merge. */
    {
      svn_rangelist_t *added_rangelist, *deleted_rangelist;

      /* The revert range and will need to be reversed for
         our svn_rangelist_* APIs to work properly. */
      SVN_ERR(svn_rangelist_reverse(requested_rangelist, scratch_pool));

      /* Set EXPLICIT_RANGELIST to the list of source-range revs that are
         already recorded as merged to target. */
      if (target_rangelist)
        {
          /* Return the intersection of the revs which are both already
             represented by CHILD's explicit or inherited mergeinfo.

             We don't consider inheritance when determining intersecting
             ranges.  If we *did* consider inheritance, then our calculation
             would be wrong.  For example, if the CHILD->REMAINING_RANGES is
             5:3 and TARGET_RANGELIST is r5* (non-inheritable) then the
             intersection would be r4.  And that would be wrong as we clearly
             want to reverse merge both r4 and r5 in this case.  Ignoring the
             ranges' inheritance results in an intersection of r4-5.

             You might be wondering about CHILD's children, doesn't the above
             imply that we will reverse merge r4-5 from them?  Nope, this is
             safe to do because any path whose parent has non-inheritable
             ranges is always considered a subtree with differing mergeinfo
             even if that path has no explicit mergeinfo prior to the
             merge -- See condition 3 in the doc string for
             merge.c:get_mergeinfo_paths(). */
          SVN_ERR(svn_rangelist_intersect(&explicit_rangelist,
                                          target_rangelist,
                                          requested_rangelist,
                                          FALSE, scratch_pool));
        }
      else
        {
          explicit_rangelist =
            apr_array_make(result_pool, 0, sizeof(svn_merge_range_t *));
        }

      /* Was any part of the requested reverse merge not accounted for in
         CHILD's explicit or inherited mergeinfo? */
      SVN_ERR(svn_rangelist_diff(&deleted_rangelist, &added_rangelist,
                                 requested_rangelist, explicit_rangelist,
                                 FALSE, scratch_pool));

      if (deleted_rangelist->nelts == 0)
        {
          /* The whole of REVISION1:REVISION2 was represented in CHILD's
             explicit/inherited mergeinfo, allocate CHILD's remaining
             ranges in POOL and then we are done. */
          SVN_ERR(svn_rangelist_reverse(requested_rangelist, scratch_pool));
          child->remaining_ranges = svn_rangelist_dup(requested_rangelist,
                                                      result_pool);
        }
      else /* We need to check CHILD's implicit mergeinfo. */
        {
          svn_rangelist_t *implicit_rangelist;

          SVN_ERR(ensure_implicit_mergeinfo(parent,
                                            child,
                                            child_inherits_implicit,
                                            revision1,
                                            revision2,
                                            ra_session,
                                            ctx,
                                            result_pool,
                                            scratch_pool));

          target_implicit_rangelist = svn_hash_gets(child->implicit_mergeinfo,
                                                    mergeinfo_path);

          if (target_implicit_rangelist)
            SVN_ERR(svn_rangelist_intersect(&implicit_rangelist,
                                            target_implicit_rangelist,
                                            requested_rangelist,
                                            FALSE, scratch_pool));
          else
            implicit_rangelist = apr_array_make(scratch_pool, 0,
                                                sizeof(svn_merge_range_t *));

          SVN_ERR(svn_rangelist_merge2(implicit_rangelist,
                                       explicit_rangelist, scratch_pool,
                                       scratch_pool));
          SVN_ERR(svn_rangelist_reverse(implicit_rangelist, scratch_pool));
          child->remaining_ranges = svn_rangelist_dup(implicit_rangelist,
                                                      result_pool);
        }
    }
  else /* This is a forward merge */
    {
      /* Set EXPLICIT_RANGELIST to the list of source-range revs that are
         NOT already recorded as merged to target. */
      if (target_rangelist)
        {
          /* See earlier comment preceding svn_rangelist_intersect() for
             why we don't consider inheritance here. */
          SVN_ERR(svn_rangelist_remove(&explicit_rangelist,
                                       target_rangelist,
                                       requested_rangelist, FALSE,
                                       scratch_pool));
        }
      else
        {
          explicit_rangelist = svn_rangelist_dup(requested_rangelist,
                                                 scratch_pool);
        }

      if (explicit_rangelist->nelts == 0)
        {
          child->remaining_ranges =
            apr_array_make(result_pool, 0, sizeof(svn_merge_range_t *));
        }
      else
/* ### TODO:  Which evil shall we choose?
   ###
   ### If we allow all forward-merges not already found in recorded
   ### mergeinfo, we destroy the ability to, say, merge the whole of a
   ### branch to the trunk while automatically ignoring the revisions
   ### common to both.  That's bad.
   ###
   ### If we allow only forward-merges not found in either recorded
   ### mergeinfo or implicit mergeinfo (natural history), then the
   ### previous scenario works great, but we can't reverse-merge a
   ### previous change made to our line of history and then remake it
   ### (because the reverse-merge will leave no mergeinfo trace, and
   ### the remake-it attempt will still find the original change in
   ### natural mergeinfo.  But you know, that we happen to use 'merge'
   ### for revision undoing is somewhat unnatural anyway, so I'm
   ### finding myself having little interest in caring too much about
   ### this.  That said, if we had a way of storing reverse merge
   ### ranges, we'd be in good shape either way.
*/
#ifdef SVN_MERGE__ALLOW_ALL_FORWARD_MERGES_FROM_SELF
        {
          /* ### Don't consider implicit mergeinfo. */
          child->remaining_ranges = svn_rangelist_dup(explicit_rangelist,
                                                      pool);
        }
#else
        {
          /* Based on CHILD's TARGET_MERGEINFO there are ranges to merge.
             Check CHILD's implicit mergeinfo to see if these remaining
             ranges are represented there. */
          SVN_ERR(ensure_implicit_mergeinfo(parent,
                                            child,
                                            child_inherits_implicit,
                                            revision1,
                                            revision2,
                                            ra_session,
                                            ctx,
                                            result_pool,
                                            scratch_pool));

          target_implicit_rangelist = svn_hash_gets(child->implicit_mergeinfo,
                                                    mergeinfo_path);
          if (target_implicit_rangelist)
            SVN_ERR(svn_rangelist_remove(&(child->remaining_ranges),
                                         target_implicit_rangelist,
                                         explicit_rangelist,
                                         FALSE, result_pool));
          else
            child->remaining_ranges = svn_rangelist_dup(explicit_rangelist,
                                                        result_pool);
        }
#endif
    }

  return SVN_NO_ERROR;
}

/* Helper for do_file_merge and do_directory_merge (by way of
   populate_remaining_ranges() for the latter).

   Determine what portions of SOURCE have already
   been merged to CHILD->ABSPATH and populate CHILD->REMAINING_RANGES with
   the ranges that still need merging.

   SOURCE and CTX are all cascaded from the caller's arguments of the same
   names.  Note that this means SOURCE adheres to the requirements noted in
   `MERGEINFO MERGE SOURCE NORMALIZATION'.

   CHILD represents a working copy path which is the merge target or one of
   the target's subtrees.  If not NULL, PARENT is CHILD's nearest path-wise
   ancestor - see 'THE CHILDREN_WITH_MERGEINFO ARRAY'.  TARGET_MERGEINFO is
   the working mergeinfo on CHILD.

   RA_SESSION is the session for the younger of SOURCE->loc1 and
   SOURCE->loc2.

   If the function needs to consider CHILD->IMPLICIT_MERGEINFO and
   CHILD_INHERITS_IMPLICIT is true, then set CHILD->IMPLICIT_MERGEINFO to the
   mergeinfo inherited from PARENT->IMPLICIT_MERGEINFO.  Otherwise contact
   the repository for CHILD->IMPLICIT_MERGEINFO.

   If not null, IMPLICIT_SRC_GAP is the gap, if any, in the natural history
   of SOURCE, see merge_cmd_baton_t.implicit_src_gap.

   SCRATCH_POOL is used for all temporary allocations.  Changes to CHILD and
   PARENT are made in RESULT_POOL.

   NOTE: This should only be called when honoring mergeinfo.

   NOTE: If PARENT is present then this function must have previously been
   called for PARENT, i.e. if populate_remaining_ranges() is calling this
   function for a set of svn_client__merge_path_t* the calls must be made
   in depth-first order.

   NOTE: When performing reverse merges, return
   SVN_ERR_CLIENT_NOT_READY_TO_MERGE if both locations in SOURCE and
   CHILD->ABSPATH are all on the same line of history but CHILD->ABSPATH's
   base revision is older than the SOURCE->rev1:rev2 range, see comment re
   issue #2973 below.
*/
static svn_error_t *
calculate_remaining_ranges(svn_client__merge_path_t *parent,
                           svn_client__merge_path_t *child,
                           const merge_source_t *source,
                           svn_mergeinfo_t target_mergeinfo,
                           const apr_array_header_t *implicit_src_gap,
                           svn_boolean_t child_inherits_implicit,
                           svn_ra_session_t *ra_session,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const svn_client__pathrev_t *primary_src
    = (source->loc1->rev < source->loc2->rev) ? source->loc2 : source->loc1;
  const char *mergeinfo_path = svn_client__pathrev_fspath(primary_src,
                                                          scratch_pool);
  /* Intersection of TARGET_MERGEINFO and the merge history
     described by SOURCE. */
  svn_rangelist_t *target_rangelist;
  svn_revnum_t child_base_revision;

  /* Since this function should only be called when honoring mergeinfo and
   * SOURCE adheres to the requirements noted in 'MERGEINFO MERGE SOURCE
   * NORMALIZATION', SOURCE must be 'ancestral'. */
  SVN_ERR_ASSERT(source->ancestral);

  /* Determine which of the requested ranges to consider merging... */

  /* Set TARGET_RANGELIST to the portion of TARGET_MERGEINFO that refers
     to SOURCE (excluding any gap in SOURCE): first get all ranges from
     TARGET_MERGEINFO that refer to the path of SOURCE, and then prune
     any ranges that lie in the gap in SOURCE.

     ### [JAF] In fact, that may still leave some ranges that lie entirely
     outside the range of SOURCE; it seems we don't care about that.  */
  if (target_mergeinfo)
    target_rangelist = svn_hash_gets(target_mergeinfo, mergeinfo_path);
  else
    target_rangelist = NULL;
  if (implicit_src_gap && target_rangelist)
    {
      /* Remove any mergeinfo referring to the 'gap' in SOURCE, as that
         mergeinfo doesn't really refer to SOURCE at all but instead
         refers to locations that are non-existent or on a different
         line of history.  (Issue #3242.) */
      SVN_ERR(svn_rangelist_remove(&target_rangelist,
                                   implicit_src_gap, target_rangelist,
                                   FALSE, result_pool));
    }

  /* Initialize CHILD->REMAINING_RANGES and filter out revisions already
     merged (or, in the case of reverse merges, ranges not yet merged). */
  SVN_ERR(filter_merged_revisions(parent, child, mergeinfo_path,
                                  target_rangelist,
                                  source->loc1->rev, source->loc2->rev,
                                  child_inherits_implicit,
                                  ra_session, ctx, result_pool,
                                  scratch_pool));

  /* Issue #2973 -- from the continuing series of "Why, since the advent of
     merge tracking, allowing merges into mixed rev and locally modified
     working copies isn't simple and could be considered downright evil".

     If reverse merging a range to the WC path represented by CHILD, from
     that path's own history, where the path inherits no locally modified
     mergeinfo from its WC parents (i.e. there is no uncommitted merge to
     the WC), and the path's base revision is older than the range, then
     the merge will always be a no-op.  This is because we only allow reverse
     merges of ranges in the path's explicit or natural mergeinfo and a
     reverse merge from the path's future history obviously isn't going to be
     in either, hence the no-op.

     The problem is two-fold.  First, in a mixed rev WC, the change we
     want to revert might actually be to some child of the target path
     which is at a younger base revision.  Sure, we can merge directly
     to that child or update the WC or even use --ignore-ancestry and then
     successfully run the reverse merge, but that gets to the second
     problem: Those courses of action are not very obvious.  Before 1.5 if
     a user committed a change that didn't touch the commit target, then
     immediately decided to revert that change via a reverse merge it would
     just DTRT.  But with the advent of merge tracking the user gets a no-op.

     So in the name of user friendliness, return an error suggesting a helpful
     course of action.
  */
  SVN_ERR(svn_wc__node_get_base(NULL, &child_base_revision,
                                NULL, NULL, NULL, NULL,
                                ctx->wc_ctx, child->abspath,
                                TRUE /* ignore_enoent */,
                                scratch_pool, scratch_pool));
  /* If CHILD has no base revision then it hasn't been committed yet, so it
     can't have any "future" history. */
  if (SVN_IS_VALID_REVNUM(child_base_revision)
      && ((child->remaining_ranges)->nelts == 0) /* Inoperative merge */
      && (source->loc2->rev < source->loc1->rev)     /* Reverse merge */
      && (child_base_revision <= source->loc2->rev))  /* From CHILD's future */
    {
      /* Hmmm, an inoperative reverse merge from the "future".  If it is
         from our own future return a helpful error. */
      svn_error_t *err;
      svn_client__pathrev_t *start_loc;

      err = svn_client__repos_location(&start_loc,
                                       ra_session,
                                       source->loc1,
                                       child_base_revision,
                                       ctx, scratch_pool, scratch_pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_FS_NOT_FOUND
              || err->apr_err == SVN_ERR_CLIENT_UNRELATED_RESOURCES)
            svn_error_clear(err);
          else
            return svn_error_trace(err);
        }
      else
        {
          const char *url;

          SVN_ERR(svn_wc__node_get_url(&url, ctx->wc_ctx, child->abspath,
                                       scratch_pool, scratch_pool));
          if (strcmp(start_loc->url, url) == 0)
            return svn_error_create(SVN_ERR_CLIENT_MERGE_UPDATE_REQUIRED, NULL,
                                    _("Cannot reverse-merge a range from a "
                                      "path's own future history; try "
                                      "updating first"));
        }
    }

  return SVN_NO_ERROR;
}

/* Helper for populate_remaining_ranges().

   SOURCE is cascaded from the arguments of the same name in
   populate_remaining_ranges().

   Note: The following comments assume a forward merge, i.e.
   SOURCE->loc1->rev < SOURCE->loc2->rev.  If this is a reverse merge then
   all the following comments still apply, but with SOURCE->loc1 switched
   with SOURCE->loc2.

   Like populate_remaining_ranges(), SOURCE must adhere to the restrictions
   documented in 'MERGEINFO MERGE SOURCE NORMALIZATION'.  These restrictions
   allow for a *single* gap in SOURCE, GAP_REV1:GAP_REV2 exclusive:inclusive
   (where SOURCE->loc1->rev == GAP_REV1 <= GAP_REV2 < SOURCE->loc2->rev),
   if SOURCE->loc2->url@(GAP_REV2+1) was copied from SOURCE->loc1.  If such
   a gap exists, set *GAP_START and *GAP_END to the starting and ending
   revisions of the gap.  Otherwise set both to SVN_INVALID_REVNUM.

   For example, if the natural history of URL@2:URL@9 is 'trunk/:2,7-9' this
   would indicate that trunk@7 was copied from trunk@2.  This function would
   return GAP_START:GAP_END of 2:6 in this case.  Note that a path 'trunk'
   might exist at r3-6, but it would not be on the same line of history as
   trunk@9.

   ### GAP_START is basically redundant, as (if there is a gap at all) it is
   necessarily the older revision of SOURCE.

   RA_SESSION is an open RA session to the repository in which SOURCE lives.
*/
static svn_error_t *
find_gaps_in_merge_source_history(svn_revnum_t *gap_start,
                                  svn_revnum_t *gap_end,
                                  const merge_source_t *source,
                                  svn_ra_session_t *ra_session,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *scratch_pool)
{
  svn_mergeinfo_t implicit_src_mergeinfo;
  svn_revnum_t old_rev = MIN(source->loc1->rev, source->loc2->rev);
  const svn_client__pathrev_t *primary_src
    = (source->loc1->rev < source->loc2->rev) ? source->loc2 : source->loc1;
  const char *merge_src_fspath = svn_client__pathrev_fspath(primary_src,
                                                            scratch_pool);
  svn_rangelist_t *rangelist;

  SVN_ERR_ASSERT(source->ancestral);

  /* Start by assuming there is no gap. */
  *gap_start = *gap_end = SVN_INVALID_REVNUM;

  /* Easy out: There can't be a gap between adjacent revisions. */
  if (labs(source->loc1->rev - source->loc2->rev) == 1)
    return SVN_NO_ERROR;

  /* Get SOURCE as mergeinfo. */
  SVN_ERR(svn_client__get_history_as_mergeinfo(&implicit_src_mergeinfo, NULL,
                                               primary_src,
                                               primary_src->rev, old_rev,
                                               ra_session,
                                               ctx, scratch_pool));

  rangelist = svn_hash_gets(implicit_src_mergeinfo, merge_src_fspath);

  if (!rangelist) /* ### Can we ever not find a rangelist? */
    return SVN_NO_ERROR;

  /* A gap in natural history can result from either a copy or
     a rename.  If from a copy then history as mergeinfo will look
     something like this:

       '/trunk:X,Y-Z'

     If from a rename it will look like this:

       '/trunk_old_name:X'
       '/trunk_new_name:Y-Z'

    In both cases the gap, if it exists, is M-N, where M = X + 1 and
    N = Y - 1.

    Note that per the rules of 'MERGEINFO MERGE SOURCE NORMALIZATION' we
    should never have multiple gaps, e.g. if we see anything like the
    following then something is quite wrong:

        '/trunk_old_name:A,B-C'
        '/trunk_new_name:D-E'
  */

  if (rangelist->nelts > 1) /* Copy */
    {
      const svn_merge_range_t *gap;
      /* As mentioned above, multiple gaps *shouldn't* be possible. */
      SVN_ERR_ASSERT(apr_hash_count(implicit_src_mergeinfo) == 1);

      gap = APR_ARRAY_IDX(rangelist, rangelist->nelts - 1,
                          const svn_merge_range_t *);

      *gap_start = MIN(source->loc1->rev, source->loc2->rev);
      *gap_end = gap->start;

      /* ### Issue #4132:
         ### This assertion triggers in merge_tests.py svnmucc_abuse_1()
         ### when a node is replaced by an older copy of itself.

         BH: I think we should review this and the 'rename' case to find
             out which behavior we really want, and if we can really
             determine what happened this way. */
      SVN_ERR_ASSERT(*gap_start < *gap_end);
    }
  else if (apr_hash_count(implicit_src_mergeinfo) > 1) /* Rename */
    {
      svn_rangelist_t *requested_rangelist =
        svn_rangelist__initialize(MIN(source->loc1->rev, source->loc2->rev),
                                  MAX(source->loc1->rev, source->loc2->rev),
                                  TRUE, scratch_pool);
      svn_rangelist_t *implicit_rangelist =
        apr_array_make(scratch_pool, 2, sizeof(svn_merge_range_t *));
      svn_rangelist_t *gap_rangelist;

      SVN_ERR(svn_rangelist__merge_many(implicit_rangelist,
                                        implicit_src_mergeinfo,
                                        scratch_pool, scratch_pool));
      SVN_ERR(svn_rangelist_remove(&gap_rangelist, implicit_rangelist,
                                   requested_rangelist, FALSE,
                                   scratch_pool));

      /* If there is anything left it is the gap. */
      if (gap_rangelist->nelts)
        {
          svn_merge_range_t *gap_range =
            APR_ARRAY_IDX(gap_rangelist, 0, svn_merge_range_t *);

          *gap_start = gap_range->start;
          *gap_end = gap_range->end;
        }
    }

  SVN_ERR_ASSERT(*gap_start == MIN(source->loc1->rev, source->loc2->rev)
                 || (*gap_start == SVN_INVALID_REVNUM
                     && *gap_end == SVN_INVALID_REVNUM));
  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge().

   For each (svn_client__merge_path_t *) child in CHILDREN_WITH_MERGEINFO,
   populate that child's 'remaining_ranges' list with (### ... what?),
   and populate that child's 'implicit_mergeinfo' with its implicit
   mergeinfo (natural history).  CHILDREN_WITH_MERGEINFO is expected
   to be sorted in depth first order and each child must be processed in
   that order.  The inheritability of all calculated ranges is TRUE.

   If mergeinfo is being honored (based on MERGE_B -- see HONOR_MERGEINFO()
   for how this is determined), this function will actually try to be
   intelligent about populating remaining_ranges list.  Otherwise, it
   will claim that each child has a single remaining range, from
   SOURCE->rev1, to SOURCE->rev2.
   ### We also take the short-cut if doing record-only.  Why?

   SCRATCH_POOL is used for all temporary allocations.  Changes to
   CHILDREN_WITH_MERGEINFO are made in RESULT_POOL.

   Note that if SOURCE->rev1 > SOURCE->rev2, then each child's remaining_ranges
   member does not adhere to the API rules for rangelists described in
   svn_mergeinfo.h -- See svn_client__merge_path_t.

   See `MERGEINFO MERGE SOURCE NORMALIZATION' for more requirements
   around SOURCE.
*/
static svn_error_t *
populate_remaining_ranges(apr_array_header_t *children_with_mergeinfo,
                          const merge_source_t *source,
                          svn_ra_session_t *ra_session,
                          merge_cmd_baton_t *merge_b,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;
  svn_revnum_t gap_start, gap_end;

  /* If we aren't honoring mergeinfo or this is a --record-only merge,
     we'll make quick work of this by simply adding dummy SOURCE->rev1:rev2
     ranges for all children. */
  if (! HONOR_MERGEINFO(merge_b) || merge_b->record_only)
    {
      for (i = 0; i < children_with_mergeinfo->nelts; i++)
        {
          svn_client__merge_path_t *child =
            APR_ARRAY_IDX(children_with_mergeinfo, i,
                          svn_client__merge_path_t *);

          svn_pool_clear(iterpool);

          /* Issue #3646 'record-only merges create self-referential
             mergeinfo'.  Get the merge target's implicit mergeinfo (natural
             history).  We'll use it later to avoid setting self-referential
             mergeinfo -- see filter_natural_history_from_mergeinfo(). */
          if (i == 0) /* First item is always the merge target. */
            {
              SVN_ERR(get_full_mergeinfo(NULL, /* child->pre_merge_mergeinfo */
                                         &(child->implicit_mergeinfo),
                                         NULL, /* child->inherited_mergeinfo */
                                         svn_mergeinfo_inherited, ra_session,
                                         child->abspath,
                                         MAX(source->loc1->rev,
                                             source->loc2->rev),
                                         MIN(source->loc1->rev,
                                             source->loc2->rev),
                                         merge_b->ctx, result_pool,
                                         iterpool));
            }
          else
            {
              /* Issue #3443 - Subtrees of the merge target can inherit
                 their parent's implicit mergeinfo in most cases. */
              svn_client__merge_path_t *parent
                = find_nearest_ancestor(children_with_mergeinfo,
                                        FALSE, child->abspath);
              svn_boolean_t child_inherits_implicit;

              /* If CHILD is a subtree then its parent must be in
                 CHILDREN_WITH_MERGEINFO, see the global comment
                 'THE CHILDREN_WITH_MERGEINFO ARRAY'. */
              SVN_ERR_ASSERT(parent);

              child_inherits_implicit = (parent && !child->switched);
              SVN_ERR(ensure_implicit_mergeinfo(parent, child,
                                                child_inherits_implicit,
                                                source->loc1->rev,
                                                source->loc2->rev,
                                                ra_session, merge_b->ctx,
                                                result_pool, iterpool));
            }

          child->remaining_ranges = svn_rangelist__initialize(source->loc1->rev,
                                                              source->loc2->rev,
                                                              TRUE,
                                                              result_pool);
        }
      svn_pool_destroy(iterpool);
      return SVN_NO_ERROR;
    }

  /* If, in the merge source's history, there was a copy from an older
     revision, then SOURCE->loc2->url won't exist at some range M:N, where
     SOURCE->loc1->rev < M < N < SOURCE->loc2->rev. The rules of 'MERGEINFO
     MERGE SOURCE NORMALIZATION' allow this, but we must ignore these gaps
     when calculating what ranges remain to be merged from SOURCE. If we
     don't and try to merge any part of SOURCE->loc2->url@M:N we would
     break the editor since no part of that actually exists.  See
     http://svn.haxx.se/dev/archive-2008-11/0618.shtml.

     Find the gaps in the merge target's history, if any.  Eventually
     we will adjust CHILD->REMAINING_RANGES such that we don't describe
     non-existent paths to the editor. */
  SVN_ERR(find_gaps_in_merge_source_history(&gap_start, &gap_end,
                                            source,
                                            ra_session, merge_b->ctx,
                                            iterpool));

  /* Stash any gap in the merge command baton, we'll need it later when
     recording mergeinfo describing this merge. */
  if (SVN_IS_VALID_REVNUM(gap_start) && SVN_IS_VALID_REVNUM(gap_end))
    merge_b->implicit_src_gap = svn_rangelist__initialize(gap_start, gap_end,
                                                          TRUE, result_pool);

  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);
      const char *child_repos_path
        = svn_dirent_skip_ancestor(merge_b->target->abspath, child->abspath);
      merge_source_t child_source;
      svn_client__merge_path_t *parent = NULL;
      svn_boolean_t child_inherits_implicit;

      svn_pool_clear(iterpool);

      /* If the path is absent don't do subtree merge either. */
      SVN_ERR_ASSERT(child);
      if (child->absent)
        continue;

      SVN_ERR_ASSERT(child_repos_path != NULL);
      child_source.loc1 = svn_client__pathrev_join_relpath(
                            source->loc1, child_repos_path, iterpool);
      child_source.loc2 = svn_client__pathrev_join_relpath(
                            source->loc2, child_repos_path, iterpool);
      /* ### Is the child 'ancestral' over the same revision range?  It's
       * not necessarily true that a child is 'ancestral' if the parent is,
       * nor that it's not if the parent is not.  However, here we claim
       * that it is.  Before we had this 'ancestral' field that we need to
       * set explicitly, the claim was implicit.  Either way, the impact is
       * that we might pass calculate_remaining_ranges() a source that is
       * not in fact 'ancestral' (despite its 'ancestral' field being true),
       * contrary to its doc-string. */
      child_source.ancestral = source->ancestral;

      /* Get the explicit/inherited mergeinfo for CHILD.  If CHILD is the
         merge target then also get its implicit mergeinfo.  Otherwise defer
         this until we know it is absolutely necessary, since it requires an
         expensive round trip communication with the server. */
      SVN_ERR(get_full_mergeinfo(
        child->pre_merge_mergeinfo ? NULL : &(child->pre_merge_mergeinfo),
        /* Get implicit only for merge target. */
        (i == 0) ? &(child->implicit_mergeinfo) : NULL,
        &(child->inherited_mergeinfo),
        svn_mergeinfo_inherited, ra_session,
        child->abspath,
        MAX(source->loc1->rev, source->loc2->rev),
        MIN(source->loc1->rev, source->loc2->rev),
        merge_b->ctx, result_pool, iterpool));

      /* If CHILD isn't the merge target find its parent. */
      if (i > 0)
        {
          parent = find_nearest_ancestor(children_with_mergeinfo,
                                         FALSE, child->abspath);
          /* If CHILD is a subtree then its parent must be in
             CHILDREN_WITH_MERGEINFO, see the global comment
             'THE CHILDREN_WITH_MERGEINFO ARRAY'. */
          SVN_ERR_ASSERT(parent);
        }

      /* Issue #3443 - Can CHILD inherit PARENT's implicit mergeinfo, saving
         us from having to ask the repos?  The only time we can't do this is if
         CHILD is the merge target and so there is no PARENT to inherit from
         or if CHILD is the root of a switched subtree, in which case PARENT
         exists but is not CHILD's repository parent. */
      child_inherits_implicit = (parent && !child->switched);

      SVN_ERR(calculate_remaining_ranges(parent, child,
                                         &child_source,
                                         child->pre_merge_mergeinfo,
                                         merge_b->implicit_src_gap,
                                         child_inherits_implicit,
                                         ra_session,
                                         merge_b->ctx, result_pool,
                                         iterpool));

      /* Deal with any gap in SOURCE's natural history.

         If the gap is a proper subset of CHILD->REMAINING_RANGES then we can
         safely ignore it since we won't describe this path/rev pair.

         If the gap exactly matches or is a superset of a range in
         CHILD->REMAINING_RANGES then we must remove that range so we don't
         attempt to describe non-existent paths via the reporter, this will
         break the editor and our merge.

         If the gap adjoins or overlaps a range in CHILD->REMAINING_RANGES
         then we must *add* the gap so we span the missing revisions. */
      if (child->remaining_ranges->nelts
          && merge_b->implicit_src_gap)
        {
          int j;
          svn_boolean_t proper_subset = FALSE;
          svn_boolean_t overlaps_or_adjoins = FALSE;

          /* If this is a reverse merge reorder CHILD->REMAINING_RANGES
              so it will work with the svn_rangelist_* APIs below. */
          if (source->loc1->rev > source->loc2->rev)
            SVN_ERR(svn_rangelist_reverse(child->remaining_ranges, iterpool));

          for (j = 0; j < child->remaining_ranges->nelts; j++)
            {
              svn_merge_range_t *range
                = APR_ARRAY_IDX(child->remaining_ranges, j, svn_merge_range_t *);

              if ((range->start <= gap_start && gap_end < range->end)
                  || (range->start < gap_start && gap_end <= range->end))
                {
                  proper_subset = TRUE;
                  break;
                }
              else if ((gap_start == range->start) && (range->end == gap_end))
                {
                  break;
                }
              else if (gap_start <= range->end && range->start <= gap_end)
                /* intersect */
                {
                  overlaps_or_adjoins = TRUE;
                  break;
                }
            }

          if (!proper_subset)
            {
              /* We need to make adjustments.  Remove from, or add the gap
                 to, CHILD->REMAINING_RANGES as appropriate. */

              if (overlaps_or_adjoins)
                SVN_ERR(svn_rangelist_merge2(child->remaining_ranges,
                                             merge_b->implicit_src_gap,
                                             result_pool, iterpool));
              else /* equals == TRUE */
                SVN_ERR(svn_rangelist_remove(&(child->remaining_ranges),
                                             merge_b->implicit_src_gap,
                                             child->remaining_ranges, FALSE,
                                             result_pool));
            }

          if (source->loc1->rev > source->loc2->rev) /* Reverse merge */
            SVN_ERR(svn_rangelist_reverse(child->remaining_ranges, iterpool));
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/*** Other Helper Functions ***/

/* Calculate the new mergeinfo for the target tree rooted at TARGET_ABSPATH
   based on MERGES (a mapping of absolute WC paths to rangelists representing
   a merge from the source SOURCE_FSPATH).

   If RESULT_CATALOG is NULL, then record the new mergeinfo in the WC (at,
   and possibly below, TARGET_ABSPATH).

   If RESULT_CATALOG is not NULL, then don't record the new mergeinfo on the
   WC, but instead record it in RESULT_CATALOG, where the keys are absolute
   working copy paths and the values are the new mergeinfos for each.
   Allocate additions to RESULT_CATALOG in pool which RESULT_CATALOG was
   created in. */
static svn_error_t *
update_wc_mergeinfo(svn_mergeinfo_catalog_t result_catalog,
                    const char *target_abspath,
                    const char *source_fspath,
                    apr_hash_t *merges,
                    svn_boolean_t is_rollback,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;

  /* Combine the mergeinfo for the revision range just merged into
     the WC with its on-disk mergeinfo. */
  for (hi = apr_hash_first(scratch_pool, merges); hi; hi = apr_hash_next(hi))
    {
      const char *local_abspath = apr_hash_this_key(hi);
      svn_rangelist_t *ranges = apr_hash_this_val(hi);
      svn_rangelist_t *rangelist;
      svn_error_t *err;
      const char *local_abspath_rel_to_target;
      const char *fspath;
      svn_mergeinfo_t mergeinfo;

      svn_pool_clear(iterpool);

      /* As some of the merges may've changed the WC's mergeinfo, get
         a fresh copy before using it to update the WC's mergeinfo. */
      err = svn_client__parse_mergeinfo(&mergeinfo, ctx->wc_ctx,
                                        local_abspath, iterpool, iterpool);

      /* If a directory PATH was skipped because it is missing or was
         obstructed by an unversioned item then there's nothing we can
         do with that, so skip it. */
      if (err)
        {
          if (err->apr_err == SVN_ERR_WC_NOT_LOCKED
              || err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
            {
              svn_error_clear(err);
              continue;
            }
          else
            {
              return svn_error_trace(err);
            }
        }

      /* If we are attempting to set empty revision range override mergeinfo
         on a path with no explicit mergeinfo, we first need the
         mergeinfo that path inherits. */
      if (mergeinfo == NULL && ranges->nelts == 0)
        {
          SVN_ERR(svn_client__get_wc_mergeinfo(&mergeinfo, NULL,
                                               svn_mergeinfo_nearest_ancestor,
                                               local_abspath, NULL, NULL,
                                               FALSE, ctx, iterpool, iterpool));
        }

      if (mergeinfo == NULL)
        mergeinfo = apr_hash_make(iterpool);

      local_abspath_rel_to_target = svn_dirent_skip_ancestor(target_abspath,
                                                             local_abspath);
      SVN_ERR_ASSERT(local_abspath_rel_to_target != NULL);
      fspath = svn_fspath__join(source_fspath,
                                local_abspath_rel_to_target,
                                iterpool);
      rangelist = svn_hash_gets(mergeinfo, fspath);
      if (rangelist == NULL)
        rangelist = apr_array_make(iterpool, 0, sizeof(svn_merge_range_t *));

      if (is_rollback)
        {
          ranges = svn_rangelist_dup(ranges, iterpool);
          SVN_ERR(svn_rangelist_reverse(ranges, iterpool));
          SVN_ERR(svn_rangelist_remove(&rangelist, ranges, rangelist,
                                       FALSE,
                                       iterpool));
        }
      else
        {
          SVN_ERR(svn_rangelist_merge2(rangelist, ranges, iterpool, iterpool));
        }
      /* Update the mergeinfo by adjusting the path's rangelist. */
      svn_hash_sets(mergeinfo, fspath, rangelist);

      if (is_rollback && apr_hash_count(mergeinfo) == 0)
        mergeinfo = NULL;

      svn_mergeinfo__remove_empty_rangelists(mergeinfo, scratch_pool);

      if (result_catalog)
        {
          svn_mergeinfo_t existing_mergeinfo =
            svn_hash_gets(result_catalog, local_abspath);
          apr_pool_t *result_catalog_pool = apr_hash_pool_get(result_catalog);

          if (existing_mergeinfo)
            SVN_ERR(svn_mergeinfo_merge2(mergeinfo, existing_mergeinfo,
                                         result_catalog_pool, scratch_pool));
          svn_hash_sets(result_catalog,
                        apr_pstrdup(result_catalog_pool, local_abspath),
                        svn_mergeinfo_dup(mergeinfo, result_catalog_pool));
        }
      else
        {
          err = svn_client__record_wc_mergeinfo(local_abspath, mergeinfo,
                                                TRUE, ctx, iterpool);

          if (err && err->apr_err == SVN_ERR_ENTRY_NOT_FOUND)
            {
              /* PATH isn't just missing, it's not even versioned as far
                 as this working copy knows.  But it was included in
                 MERGES, which means that the server knows about it.
                 Likely we don't have access to the source due to authz
                 restrictions.  For now just clear the error and
                 continue...

                 ### TODO:  Set non-inheritable mergeinfo on PATH's immediate
                 ### parent and normal mergeinfo on PATH's siblings which we
                 ### do have access to. */
              svn_error_clear(err);
            }
          else
            SVN_ERR(err);
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Helper for record_mergeinfo_for_dir_merge().

   Record override mergeinfo on any paths skipped during a merge.

   Set empty mergeinfo on each path in MERGE_B->SKIPPED_ABSPATHS so the path
   does not incorrectly inherit mergeinfo that will later be describing
   the merge.

   MERGEINFO_PATH and MERGE_B are cascaded from
   arguments of the same name in the caller.

   IS_ROLLBACK is true if the caller is recording a reverse merge and false
   otherwise.  RANGELIST is the set of revisions being merged from
   MERGEINFO_PATH to MERGE_B->target. */
static svn_error_t *
record_skips_in_mergeinfo(const char *mergeinfo_path,
                          const svn_rangelist_t *rangelist,
                          svn_boolean_t is_rollback,
                          merge_cmd_baton_t *merge_b,
                          apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *merges;
  apr_size_t nbr_skips = apr_hash_count(merge_b->skipped_abspaths);
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  if (nbr_skips == 0)
    return SVN_NO_ERROR;

  merges = apr_hash_make(scratch_pool);

  /* Override the mergeinfo for child paths which weren't actually merged. */
  for (hi = apr_hash_first(scratch_pool, merge_b->skipped_abspaths); hi;
       hi = apr_hash_next(hi))
    {
      const char *skipped_abspath = apr_hash_this_key(hi);
      svn_wc_notify_state_t obstruction_state;

      svn_pool_clear(iterpool);

      /* Before we override, make sure this is a versioned path, it might
         be an external or missing from disk due to authz restrictions. */
      SVN_ERR(perform_obstruction_check(&obstruction_state, NULL, NULL,
                                        NULL, NULL,
                                        merge_b, skipped_abspath,
                                        iterpool));
      if (obstruction_state == svn_wc_notify_state_obstructed
          || obstruction_state == svn_wc_notify_state_missing)
        continue;

      /* Add an empty range list for this path.

         ### TODO: This works fine for a file path skipped because it is
         ### missing as long as the file's parent directory is present.
         ### But missing directory paths skipped are not handled yet,
         ### see issue #2915.

         ### TODO: An empty range is fine if the skipped path doesn't
         ### inherit any mergeinfo from a parent, but if it does
         ### we need to account for that.  See issue #3440
         ### http://subversion.tigris.org/issues/show_bug.cgi?id=3440. */
      svn_hash_sets(merges, skipped_abspath,
                    apr_array_make(scratch_pool, 0,
                                   sizeof(svn_merge_range_t *)));

      /* if (nbr_skips < notify_b->nbr_notifications)
           ### Use RANGELIST as the mergeinfo for all children of
           ### this path which were not also explicitly
           ### skipped? */
    }
  SVN_ERR(update_wc_mergeinfo(NULL, merge_b->target->abspath,
                              mergeinfo_path, merges,
                              is_rollback, merge_b->ctx, iterpool));
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Data for reporting when a merge aborted because of raising conflicts.
 */
typedef struct single_range_conflict_report_t
{
  /* What sub-range of the requested source raised conflicts?
   * The 'inheritable' flag is ignored. */
  merge_source_t *conflicted_range;
  /* What sub-range of the requested source remains to be merged?
   * NULL if no more.  The 'inheritable' flag is ignored. */
  merge_source_t *remaining_source;

} single_range_conflict_report_t;

/* Create a single_range_conflict_report_t, containing deep copies of
 * CONFLICTED_RANGE and REMAINING_SOURCE, allocated in RESULT_POOL. */
static single_range_conflict_report_t *
single_range_conflict_report_create(const merge_source_t *conflicted_range,
                                    const merge_source_t *remaining_source,
                                    apr_pool_t *result_pool)
{
  single_range_conflict_report_t *report
    = apr_palloc(result_pool, sizeof(*report));

  assert(conflicted_range != NULL);

  report->conflicted_range = merge_source_dup(conflicted_range, result_pool);
  report->remaining_source
    = remaining_source ? merge_source_dup(remaining_source, result_pool)
                       : NULL;
  return report;
}

/* Return a new svn_client__conflict_report_t containing deep copies of the
 * parameters, allocated in RESULT_POOL. */
static svn_client__conflict_report_t *
conflict_report_create(const char *target_abspath,
                       const merge_source_t *conflicted_range,
                       svn_boolean_t was_last_range,
                       apr_pool_t *result_pool)
{
  svn_client__conflict_report_t *report = apr_palloc(result_pool,
                                                     sizeof(*report));

  report->target_abspath = apr_pstrdup(result_pool, target_abspath);
  report->conflicted_range = merge_source_dup(conflicted_range, result_pool);
  report->was_last_range = was_last_range;
  return report;
}

/* Return a deep copy of REPORT, allocated in RESULT_POOL. */
static svn_client__conflict_report_t *
conflict_report_dup(const svn_client__conflict_report_t *report,
                    apr_pool_t *result_pool)
{
  svn_client__conflict_report_t *new = apr_pmemdup(result_pool, report,
                                                   sizeof(*new));

  new->target_abspath = apr_pstrdup(result_pool, report->target_abspath);
  new->conflicted_range = merge_source_dup(report->conflicted_range,
                                           result_pool);
  return new;
}

svn_error_t *
svn_client__make_merge_conflict_error(svn_client__conflict_report_t *report,
                                      apr_pool_t *scratch_pool)
{
  assert(!report || svn_dirent_is_absolute(report->target_abspath));

  if (report && ! report->was_last_range)
    {
      svn_error_t *err = svn_error_createf(SVN_ERR_WC_FOUND_CONFLICT, NULL,
       _("One or more conflicts were produced while merging r%ld:%ld into\n"
         "'%s' --\n"
         "resolve all conflicts and rerun the merge to apply the remaining\n"
         "unmerged revisions"),
       report->conflicted_range->loc1->rev, report->conflicted_range->loc2->rev,
       svn_dirent_local_style(report->target_abspath, scratch_pool));
      assert(report->conflicted_range->loc1->rev != report->conflicted_range->loc2->rev); /* ### is a valid case in a 2-URL merge */
      return err;
    }
  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge().

   TARGET_WCPATH is a directory and CHILDREN_WITH_MERGEINFO is filled
   with paths (svn_client__merge_path_t *) arranged in depth first order,
   which have mergeinfo set on them or meet one of the other criteria
   defined in get_mergeinfo_paths().  Remove any paths absent from disk
   or scheduled for deletion from CHILDREN_WITH_MERGEINFO which are equal to
   or are descendants of TARGET_WCPATH by setting those children to NULL. */
static void
remove_absent_children(const char *target_wcpath,
                       apr_array_header_t *children_with_mergeinfo)
{
  /* Before we try to override mergeinfo for skipped paths, make sure
     the path isn't absent due to authz restrictions, because there's
     nothing we can do about those. */
  int i;
  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);
      if ((child->absent || child->scheduled_for_deletion)
          && svn_dirent_is_ancestor(target_wcpath, child->abspath))
        {
          svn_sort__array_delete(children_with_mergeinfo, i--, 1);
        }
    }
}

/* Helper for do_directory_merge() to handle the case where a merge editor
   drive removes explicit mergeinfo from a subtree of the merge target.

   MERGE_B is cascaded from the argument of the same name in
   do_directory_merge().  For each path (if any) in
   MERGE_B->PATHS_WITH_DELETED_MERGEINFO remove that path from
   CHILDREN_WITH_MERGEINFO.

   The one exception is for the merge target itself,
   MERGE_B->target->abspath, this must always be present in
   CHILDREN_WITH_MERGEINFO so this is never removed by this
   function. */
static void
remove_children_with_deleted_mergeinfo(merge_cmd_baton_t *merge_b,
                                       apr_array_header_t *children_with_mergeinfo)
{
  int i;

  if (!merge_b->paths_with_deleted_mergeinfo)
    return;

  /* CHILDREN_WITH_MERGEINFO[0] is the always the merge target
     so start at the first child. */
  for (i = 1; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);

      if (svn_hash_gets(merge_b->paths_with_deleted_mergeinfo, child->abspath))
        {
          svn_sort__array_delete(children_with_mergeinfo, i--, 1);
        }
    }
}

/* Helper for do_directory_merge().

   Set up the diff editor report to merge the SOURCE diff
   into TARGET_ABSPATH and drive it.

   If mergeinfo is not being honored (based on MERGE_B -- see the doc
   string for HONOR_MERGEINFO() for how this is determined), then ignore
   CHILDREN_WITH_MERGEINFO and merge the SOURCE diff to TARGET_ABSPATH.

   If mergeinfo is being honored then perform a history-aware merge,
   describing TARGET_ABSPATH and its subtrees to the reporter in such as way
   as to avoid repeating merges already performed per the mergeinfo and
   natural history of TARGET_ABSPATH and its subtrees.

   The ranges that still need to be merged to the TARGET_ABSPATH and its
   subtrees are described in CHILDREN_WITH_MERGEINFO, an array of
   svn_client__merge_path_t * -- see 'THE CHILDREN_WITH_MERGEINFO ARRAY'
   comment at the top of this file for more info.  Note that it is possible
   TARGET_ABSPATH and/or some of its subtrees need only a subset, or no part,
   of SOURCE to be merged.  Though there is little point to
   calling this function if TARGET_ABSPATH and all its subtrees have already
   had SOURCE merged, this will work but is a no-op.

   SOURCE->rev1 and SOURCE->rev2 must be bound by the set of remaining_ranges
   fields in CHILDREN_WITH_MERGEINFO's elements, specifically:

   For forward merges (SOURCE->rev1 < SOURCE->rev2):

     1) The first svn_merge_range_t * element of each child's remaining_ranges
        array must meet one of the following conditions:

        a) The range's start field is greater than or equal to SOURCE->rev2.

        b) The range's end field is SOURCE->rev2.

     2) Among all the ranges that meet condition 'b' the oldest start
        revision must equal SOURCE->rev1.

   For reverse merges (SOURCE->rev1 > SOURCE->rev2):

     1) The first svn_merge_range_t * element of each child's remaining_ranges
        array must meet one of the following conditions:

        a) The range's start field is less than or equal to SOURCE->rev2.

        b) The range's end field is SOURCE->rev2.

     2) Among all the ranges that meet condition 'b' the youngest start
        revision must equal SOURCE->rev1.

   Note: If the first svn_merge_range_t * element of some subtree child's
   remaining_ranges array is the same as the first range of that child's
   nearest path-wise ancestor, then the subtree child *will not* be described
   to the reporter.

   DEPTH, NOTIFY_B, and MERGE_B are cascaded from do_directory_merge(), see
   that function for more info.

   MERGE_B->ra_session1 and MERGE_B->ra_session2 are RA sessions open to any
   URL in the repository of SOURCE; they may be temporarily reparented within
   this function.

   If SOURCE->ancestral is set, then SOURCE->loc1 must be a
   historical ancestor of SOURCE->loc2, or vice-versa (see
   `MERGEINFO MERGE SOURCE NORMALIZATION' for more requirements around
   SOURCE in this case).
*/
static svn_error_t *
drive_merge_report_editor(const char *target_abspath,
                          const merge_source_t *source,
                          const apr_array_header_t *children_with_mergeinfo,
                          const svn_diff_tree_processor_t *processor,
                          svn_depth_t depth,
                          merge_cmd_baton_t *merge_b,
                          apr_pool_t *scratch_pool)
{
  const svn_ra_reporter3_t *reporter;
  const svn_delta_editor_t *diff_editor;
  void *diff_edit_baton;
  void *report_baton;
  svn_revnum_t target_start;
  svn_boolean_t honor_mergeinfo = HONOR_MERGEINFO(merge_b);
  const char *old_sess1_url, *old_sess2_url;
  svn_boolean_t is_rollback = source->loc1->rev > source->loc2->rev;

  /* Start with a safe default starting revision for the editor and the
     merge target. */
  target_start = source->loc1->rev;

  /* If we are honoring mergeinfo the starting revision for the merge target
     might not be SOURCE->rev1, in fact the merge target might not need *any*
     part of SOURCE merged -- Instead some subtree of the target
     needs SOURCE -- So get the right starting revision for the
     target. */
  if (honor_mergeinfo)
    {
      svn_client__merge_path_t *child;

      /* CHILDREN_WITH_MERGEINFO must always exist if we are honoring
         mergeinfo and must have at least one element (describing the
         merge target). */
      SVN_ERR_ASSERT(children_with_mergeinfo);
      SVN_ERR_ASSERT(children_with_mergeinfo->nelts);

      /* Get the merge target's svn_client__merge_path_t, which is always
         the first in the array due to depth first sorting requirement,
         see 'THE CHILDREN_WITH_MERGEINFO ARRAY'. */
      child = APR_ARRAY_IDX(children_with_mergeinfo, 0,
                            svn_client__merge_path_t *);
      SVN_ERR_ASSERT(child);
      if (child->remaining_ranges->nelts == 0)
        {
          /* The merge target doesn't need anything merged. */
          target_start = source->loc2->rev;
        }
      else
        {
          /* The merge target has remaining revisions to merge.  These
             ranges may fully or partially overlap the range described
             by SOURCE->rev1:rev2 or may not intersect that range at
             all. */
          svn_merge_range_t *range =
            APR_ARRAY_IDX(child->remaining_ranges, 0,
                          svn_merge_range_t *);
          if ((!is_rollback && range->start > source->loc2->rev)
              || (is_rollback && range->start < source->loc2->rev))
            {
              /* Merge target's first remaining range doesn't intersect. */
              target_start = source->loc2->rev;
            }
          else
            {
              /* Merge target's first remaining range partially or
                 fully overlaps. */
              target_start = range->start;
            }
        }
    }

  SVN_ERR(svn_client__ensure_ra_session_url(&old_sess1_url,
                                            merge_b->ra_session1,
                                            source->loc1->url, scratch_pool));
  /* Temporarily point our second RA session to SOURCE->loc1->url, too.  We use
     this to request individual file contents. */
  SVN_ERR(svn_client__ensure_ra_session_url(&old_sess2_url,
                                            merge_b->ra_session2,
                                            source->loc1->url, scratch_pool));

  /* Get the diff editor and a reporter with which to, ultimately,
     drive it. */
  SVN_ERR(svn_client__get_diff_editor2(&diff_editor, &diff_edit_baton,
                                       merge_b->ra_session2,
                                       depth,
                                       source->loc1->rev,
                                       TRUE /* text_deltas */,
                                       processor,
                                       merge_b->ctx->cancel_func,
                                       merge_b->ctx->cancel_baton,
                                       scratch_pool));
  SVN_ERR(svn_ra_do_diff3(merge_b->ra_session1,
                          &reporter, &report_baton, source->loc2->rev,
                          "", depth, merge_b->diff_ignore_ancestry,
                          TRUE,  /* text_deltas */
                          source->loc2->url, diff_editor, diff_edit_baton,
                          scratch_pool));

  /* Drive the reporter. */
  SVN_ERR(reporter->set_path(report_baton, "", target_start, depth,
                             FALSE, NULL, scratch_pool));
  if (honor_mergeinfo && children_with_mergeinfo)
    {
      /* Describe children with mergeinfo overlapping this merge
         operation such that no repeated diff is retrieved for them from
         the repository. */
      int i;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      /* Start with CHILDREN_WITH_MERGEINFO[1], CHILDREN_WITH_MERGEINFO[0]
         is always the merge target (TARGET_ABSPATH). */
      for (i = 1; i < children_with_mergeinfo->nelts; i++)
        {
          svn_merge_range_t *range;
          const char *child_repos_path;
          const svn_client__merge_path_t *parent;
          const svn_client__merge_path_t *child =
            APR_ARRAY_IDX(children_with_mergeinfo, i,
                          svn_client__merge_path_t *);

          SVN_ERR_ASSERT(child);
          if (child->absent)
            continue;

          svn_pool_clear(iterpool);

          /* Find this child's nearest wc ancestor with mergeinfo. */
          parent = find_nearest_ancestor(children_with_mergeinfo,
                                         FALSE, child->abspath);

          /* If a subtree needs the same range applied as its nearest parent
             with mergeinfo or neither the subtree nor this parent need
             SOURCE->rev1:rev2 merged, then we don't need to describe the
             subtree separately.  In the latter case this could break the
             editor if child->abspath didn't exist at SOURCE->rev2 and we
             attempt to describe it via a reporter set_path call. */
          if (child->remaining_ranges->nelts)
            {
              range = APR_ARRAY_IDX(child->remaining_ranges, 0,
                                    svn_merge_range_t *);
              if ((!is_rollback && range->start > source->loc2->rev)
                  || (is_rollback && range->start < source->loc2->rev))
                {
                  /* This child's first remaining range comes after the range
                     we are currently merging, so skip it. We expect to get
                     to it in a subsequent call to this function. */
                  continue;
                }
              else if (parent->remaining_ranges->nelts)
                {
                   svn_merge_range_t *parent_range =
                    APR_ARRAY_IDX(parent->remaining_ranges, 0,
                                  svn_merge_range_t *);
                   svn_merge_range_t *child_range =
                    APR_ARRAY_IDX(child->remaining_ranges, 0,
                                  svn_merge_range_t *);
                  if (parent_range->start == child_range->start)
                    continue; /* Subtree needs same range as parent. */
                }
            }
          else /* child->remaining_ranges->nelts == 0*/
            {
              /* If both the subtree and its parent need no ranges applied
                 consider that as the "same ranges" and don't describe
                 the subtree. */
              if (parent->remaining_ranges->nelts == 0)
                continue;
            }

          /* Ok, we really need to describe this subtree as it needs different
             ranges applied than its nearest working copy parent. */
          child_repos_path = svn_dirent_is_child(target_abspath,
                                                 child->abspath,
                                                 iterpool);
          /* This loop is only processing subtrees, so CHILD->ABSPATH
             better be a proper child of the merge target. */
          SVN_ERR_ASSERT(child_repos_path);

          if ((child->remaining_ranges->nelts == 0)
              || (is_rollback && (range->start < source->loc2->rev))
              || (!is_rollback && (range->start > source->loc2->rev)))
            {
              /* Nothing to merge to this child.  We'll claim we have
                 it up to date so the server doesn't send us
                 anything. */
              SVN_ERR(reporter->set_path(report_baton, child_repos_path,
                                         source->loc2->rev, depth, FALSE,
                                         NULL, iterpool));
            }
          else
            {
              SVN_ERR(reporter->set_path(report_baton, child_repos_path,
                                         range->start, depth, FALSE,
                                         NULL, iterpool));
            }
        }
      svn_pool_destroy(iterpool);
    }
  SVN_ERR(reporter->finish_report(report_baton, scratch_pool));

  /* Point the merge baton's RA sessions back where they were. */
  SVN_ERR(svn_ra_reparent(merge_b->ra_session1, old_sess1_url, scratch_pool));
  SVN_ERR(svn_ra_reparent(merge_b->ra_session2, old_sess2_url, scratch_pool));

  return SVN_NO_ERROR;
}

/* Iterate over each svn_client__merge_path_t * element in
   CHILDREN_WITH_MERGEINFO and, if START_REV is true, find the most inclusive
   start revision among those element's first remaining_ranges element.  If
   START_REV is false, then look for the most inclusive end revision.

   If IS_ROLLBACK is true the youngest start or end (as per START_REV)
   revision is considered the "most inclusive" otherwise the oldest revision
   is.

   If none of CHILDREN_WITH_MERGEINFO's elements have any remaining ranges
   return SVN_INVALID_REVNUM. */
static svn_revnum_t
get_most_inclusive_rev(const apr_array_header_t *children_with_mergeinfo,
                       svn_boolean_t is_rollback,
                       svn_boolean_t start_rev)
{
  int i;
  svn_revnum_t most_inclusive_rev = SVN_INVALID_REVNUM;

  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);

      if ((! child) || child->absent)
        continue;
      if (child->remaining_ranges->nelts > 0)
        {
          svn_merge_range_t *range =
            APR_ARRAY_IDX(child->remaining_ranges, 0, svn_merge_range_t *);

          /* Are we looking for the most inclusive start or end rev? */
          svn_revnum_t rev = start_rev ? range->start : range->end;

          if ((most_inclusive_rev == SVN_INVALID_REVNUM)
              || (is_rollback && (rev > most_inclusive_rev))
              || ((! is_rollback) && (rev < most_inclusive_rev)))
            most_inclusive_rev = rev;
        }
    }
  return most_inclusive_rev;
}


/* If first item in each child of CHILDREN_WITH_MERGEINFO's
   remaining_ranges is inclusive of END_REV, Slice the first range in
   to two at END_REV. All the allocations are persistent and allocated
   from POOL. */
static void
slice_remaining_ranges(apr_array_header_t *children_with_mergeinfo,
                       svn_boolean_t is_rollback, svn_revnum_t end_rev,
                       apr_pool_t *pool)
{
  int i;
  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
                                     APR_ARRAY_IDX(children_with_mergeinfo, i,
                                                   svn_client__merge_path_t *);
      if (!child || child->absent)
        continue;
      if (child->remaining_ranges->nelts > 0)
        {
          svn_merge_range_t *range = APR_ARRAY_IDX(child->remaining_ranges, 0,
                                                   svn_merge_range_t *);
          if ((is_rollback && (range->start > end_rev)
               && (range->end < end_rev))
              || (!is_rollback && (range->start < end_rev)
                  && (range->end > end_rev)))
            {
              svn_merge_range_t *split_range1, *split_range2;

              split_range1 = svn_merge_range_dup(range, pool);
              split_range2 = svn_merge_range_dup(range, pool);
              split_range1->end = end_rev;
              split_range2->start = end_rev;
              APR_ARRAY_IDX(child->remaining_ranges, 0,
                            svn_merge_range_t *) = split_range1;
              svn_sort__array_insert(child->remaining_ranges, &split_range2, 1);
            }
        }
    }
}

/* Helper for do_directory_merge().

   For each child in CHILDREN_WITH_MERGEINFO remove the first remaining_ranges
   svn_merge_range_t *element of the child if that range has an end revision
   equal to REVISION.

   If a range is removed from a child's remaining_ranges array, allocate the
   new remaining_ranges array in POOL.
 */
static void
remove_first_range_from_remaining_ranges(svn_revnum_t revision,
                                         apr_array_header_t
                                           *children_with_mergeinfo,
                                         apr_pool_t *pool)
{
  int i;

  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
                                APR_ARRAY_IDX(children_with_mergeinfo, i,
                                              svn_client__merge_path_t *);
      if (!child || child->absent)
        continue;
      if (child->remaining_ranges->nelts > 0)
        {
          svn_merge_range_t *first_range =
            APR_ARRAY_IDX(child->remaining_ranges, 0, svn_merge_range_t *);
          if (first_range->end == revision)
            {
              svn_sort__array_delete(child->remaining_ranges, 0, 1);
            }
        }
    }
}

/* Get a file's content and properties from the repository.
   Set *FILENAME to the local path to a new temporary file holding its text,
   and set *PROPS to a new hash of its properties.

   RA_SESSION is a session open to the correct repository, which will be
   temporarily reparented to the URL of the file itself.  LOCATION is the
   repository location of the file.

   The resulting file and the return values live as long as RESULT_POOL, all
   other allocations occur in SCRATCH_POOL.
*/
static svn_error_t *
single_file_merge_get_file(const char **filename,
                           apr_hash_t **props,
                           svn_ra_session_t *ra_session,
                           const svn_client__pathrev_t *location,
                           const char *wc_target,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  const char *old_sess_url;
  svn_error_t *err;

  SVN_ERR(svn_stream_open_unique(&stream, filename, NULL,
                                 svn_io_file_del_on_pool_cleanup,
                                 result_pool, scratch_pool));

  SVN_ERR(svn_client__ensure_ra_session_url(&old_sess_url, ra_session, location->url,
                                            scratch_pool));
  err = svn_ra_get_file(ra_session, "", location->rev,
                        stream, NULL, props, scratch_pool);
  SVN_ERR(svn_error_compose_create(
            err, svn_ra_reparent(ra_session, old_sess_url, scratch_pool)));

  return svn_error_trace(svn_stream_close(stream));
}

/* Compare two svn_client__merge_path_t elements **A and **B, given the
   addresses of pointers to them. Return an integer less than, equal to, or
   greater than zero if A sorts before, the same as, or after B, respectively.
   This is a helper for qsort() and bsearch() on an array of such elements. */
static int
compare_merge_path_t_as_paths(const void *a,
                              const void *b)
{
  const svn_client__merge_path_t *child1
    = *((const svn_client__merge_path_t * const *) a);
  const svn_client__merge_path_t *child2
    = *((const svn_client__merge_path_t * const *) b);

  return svn_path_compare_paths(child1->abspath, child2->abspath);
}

/* Return a pointer to the element of CHILDREN_WITH_MERGEINFO whose path
 * is PATH, or return NULL if there is no such element. */
static svn_client__merge_path_t *
get_child_with_mergeinfo(const apr_array_header_t *children_with_mergeinfo,
                         const char *abspath)
{
  svn_client__merge_path_t merge_path;
  svn_client__merge_path_t *key;
  svn_client__merge_path_t **pchild;

  merge_path.abspath = abspath;
  key = &merge_path;
  pchild = bsearch(&key, children_with_mergeinfo->elts,
                   children_with_mergeinfo->nelts,
                   children_with_mergeinfo->elt_size,
                   compare_merge_path_t_as_paths);
  return pchild ? *pchild : NULL;
}

/* Insert a deep copy of INSERT_ELEMENT into the CHILDREN_WITH_MERGEINFO
   array at its correct position.  Allocate the new storage in POOL.
   CHILDREN_WITH_MERGEINFO is a depth first sorted array of
   (svn_client__merge_path_t *).

   ### Most callers don't need this to deep-copy the new element.
   ### It may be more efficient for some callers to insert a bunch of items
       out of order and then sort afterwards. (One caller is doing a qsort
       after calling this anyway.)
 */
static void
insert_child_to_merge(apr_array_header_t *children_with_mergeinfo,
                      const svn_client__merge_path_t *insert_element,
                      apr_pool_t *pool)
{
  int insert_index;
  const svn_client__merge_path_t *new_element;

  /* Find where to insert the new element */
  insert_index =
    svn_sort__bsearch_lower_bound(children_with_mergeinfo, &insert_element,
                                  compare_merge_path_t_as_paths);

  new_element = svn_client__merge_path_dup(insert_element, pool);
  svn_sort__array_insert(children_with_mergeinfo, &new_element, insert_index);
}

/* Helper for get_mergeinfo_paths().

   CHILDREN_WITH_MERGEINFO, DEPTH, and POOL are
   all cascaded from the arguments of the same name to get_mergeinfo_paths().

   TARGET is the merge target.

   *CHILD is the element in in CHILDREN_WITH_MERGEINFO that
   get_mergeinfo_paths() is iterating over and *CURR_INDEX is index for
   *CHILD.

   If CHILD->ABSPATH is equal to MERGE_CMD_BATON->target->abspath do nothing.
   Else if CHILD->ABSPATH is switched or absent then make sure its immediate
   (as opposed to nearest) parent in CHILDREN_WITH_MERGEINFO is marked as
   missing a child.  If the immediate parent does not exist in
   CHILDREN_WITH_MERGEINFO then create it (and increment *CURR_INDEX so that
   caller doesn't process the inserted element).  Also ensure that
   CHILD->ABSPATH's siblings which are not already present in
   CHILDREN_WITH_MERGEINFO are also added to the array, limited by DEPTH
   (e.g. don't add directory siblings of a switched file).
   Use POOL for temporary allocations only, any new CHILDREN_WITH_MERGEINFO
   elements are allocated in POOL. */
static svn_error_t *
insert_parent_and_sibs_of_sw_absent_del_subtree(
                                   apr_array_header_t *children_with_mergeinfo,
                                   const merge_target_t *target,
                                   int *curr_index,
                                   svn_client__merge_path_t *child,
                                   svn_depth_t depth,
                                   svn_client_ctx_t *ctx,
                                   apr_pool_t *pool)
{
  svn_client__merge_path_t *parent;
  const char *parent_abspath;
  apr_pool_t *iterpool;
  const apr_array_header_t *children;
  int i;

  if (!(child->absent
          || (child->switched
              && strcmp(target->abspath,
                        child->abspath) != 0)))
    return SVN_NO_ERROR;

  parent_abspath = svn_dirent_dirname(child->abspath, pool);
  parent = get_child_with_mergeinfo(children_with_mergeinfo, parent_abspath);
  if (parent)
    {
      parent->missing_child = child->absent;
      parent->switched_child = child->switched;
    }
  else
    {
      /* Create a new element to insert into CHILDREN_WITH_MERGEINFO. */
      parent = svn_client__merge_path_create(parent_abspath, pool);
      parent->missing_child = child->absent;
      parent->switched_child = child->switched;
      /* Insert PARENT into CHILDREN_WITH_MERGEINFO. */
      insert_child_to_merge(children_with_mergeinfo, parent, pool);
      /* Increment for loop index so we don't process the inserted element. */
      (*curr_index)++;
    } /*(parent == NULL) */

  /* Add all of PARENT's non-missing children that are not already present.*/
  SVN_ERR(svn_wc__node_get_children_of_working_node(&children, ctx->wc_ctx,
                                                    parent_abspath,
                                                    pool, pool));
  iterpool = svn_pool_create(pool);
  for (i = 0; i < children->nelts; i++)
    {
      const char *child_abspath = APR_ARRAY_IDX(children, i, const char *);
      svn_client__merge_path_t *sibling_of_missing;

      svn_pool_clear(iterpool);

      /* Does this child already exist in CHILDREN_WITH_MERGEINFO? */
      sibling_of_missing = get_child_with_mergeinfo(children_with_mergeinfo,
                                                    child_abspath);
      /* Create the missing child and insert it into CHILDREN_WITH_MERGEINFO.*/
      if (!sibling_of_missing)
        {
          /* Don't add directory children if DEPTH is svn_depth_files. */
          if (depth == svn_depth_files)
            {
              svn_node_kind_t child_kind;

              SVN_ERR(svn_wc_read_kind2(&child_kind,
                                        ctx->wc_ctx, child_abspath,
                                        FALSE, FALSE, iterpool));
              if (child_kind != svn_node_file)
                continue;
            }

          sibling_of_missing = svn_client__merge_path_create(child_abspath,
                                                             pool);
          insert_child_to_merge(children_with_mergeinfo, sibling_of_missing,
                                pool);
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* pre_merge_status_cb's baton */
struct pre_merge_status_baton_t
{
  svn_wc_context_t *wc_ctx;

  /* const char *absolute_wc_path to svn_depth_t * mapping for depths
     of empty, immediates, and files. */
  apr_hash_t *shallow_subtrees;

  /* const char *absolute_wc_path to the same, for all paths missing
     from the working copy. */
  apr_hash_t *missing_subtrees;

  /* const char *absolute_wc_path const char * repos relative path, describing
     the root of each switched subtree in the working copy and the repository
     relative path it is switched to. */
  apr_hash_t *switched_subtrees;

  /* A pool to allocate additions to the above hashes in. */
  apr_pool_t *pool;
};

/* A svn_wc_status_func4_t callback used by get_mergeinfo_paths to gather
   all switched, depth filtered and missing subtrees under a merge target.

   Note that this doesn't see server and user excluded trees. */
static svn_error_t *
pre_merge_status_cb(void *baton,
                    const char *local_abspath,
                    const svn_wc_status3_t *status,
                    apr_pool_t *scratch_pool)
{
  struct pre_merge_status_baton_t *pmsb = baton;

  if (status->switched && !status->file_external)
    {
      store_path(pmsb->switched_subtrees, local_abspath);
    }

  if (status->depth == svn_depth_empty
      || status->depth == svn_depth_files)
    {
      const char *dup_abspath;
      svn_depth_t *depth = apr_pmemdup(pmsb->pool, &status->depth,
                                       sizeof *depth);

      dup_abspath = apr_pstrdup(pmsb->pool, local_abspath);

      svn_hash_sets(pmsb->shallow_subtrees, dup_abspath, depth);
    }

  if (status->node_status == svn_wc_status_missing)
    {
      svn_boolean_t new_missing_root = TRUE;
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, pmsb->missing_subtrees);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *missing_root_path = apr_hash_this_key(hi);

          if (svn_dirent_is_ancestor(missing_root_path,
                                     local_abspath))
            {
              new_missing_root = FALSE;
              break;
            }
        }

      if (new_missing_root)
        store_path(pmsb->missing_subtrees, local_abspath);
    }

  return SVN_NO_ERROR;
}

/* Find all the subtrees in the working copy tree rooted at TARGET_ABSPATH
 * that have explicit mergeinfo.
 * Set *SUBTREES_WITH_MERGEINFO to a hash mapping (const char *) absolute
 * WC path to (svn_mergeinfo_t *) mergeinfo.
 *
 * ### Is this function equivalent to:
 *
 *   svn_client__get_wc_mergeinfo_catalog(
 *     subtrees_with_mergeinfo, inherited=NULL, include_descendants=TRUE,
 *     svn_mergeinfo_explicit, target_abspath, limit_path=NULL,
 *     walked_path=NULL, ignore_invalid_mergeinfo=FALSE, ...)
 *
 *   except for the catalog keys being abspaths instead of repo-relpaths?
 */
static svn_error_t *
get_wc_explicit_mergeinfo_catalog(apr_hash_t **subtrees_with_mergeinfo,
                                  const char *target_abspath,
                                  svn_depth_t depth,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_opt_revision_t working_revision = { svn_opt_revision_working, { 0 } };
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;
  apr_hash_t *externals;

  SVN_ERR(svn_client_propget5(subtrees_with_mergeinfo, NULL,
                              SVN_PROP_MERGEINFO, target_abspath,
                              &working_revision, &working_revision, NULL,
                              depth, NULL, ctx, result_pool, scratch_pool));

  SVN_ERR(svn_wc__externals_defined_below(&externals, ctx->wc_ctx,
                                          target_abspath, scratch_pool,
                                          scratch_pool));

  /* Convert property values to svn_mergeinfo_t. */
  for (hi = apr_hash_first(scratch_pool, *subtrees_with_mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *wc_path = apr_hash_this_key(hi);
      svn_string_t *mergeinfo_string = apr_hash_this_val(hi);
      svn_mergeinfo_t mergeinfo;
      svn_error_t *err;

      /* svn_client_propget5 picks up file externals with
         mergeinfo, but we don't want those. */
      if (svn_hash_gets(externals, wc_path))
        {
          svn_hash_sets(*subtrees_with_mergeinfo, wc_path, NULL);
          continue;
        }

      svn_pool_clear(iterpool);

      err = svn_mergeinfo_parse(&mergeinfo, mergeinfo_string->data,
                                result_pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
            {
              err = svn_error_createf(
                SVN_ERR_CLIENT_INVALID_MERGEINFO_NO_MERGETRACKING, err,
                _("Invalid mergeinfo detected on '%s', "
                  "merge tracking not possible"),
                svn_dirent_local_style(wc_path, scratch_pool));
            }
          return svn_error_trace(err);
        }
      svn_hash_sets(*subtrees_with_mergeinfo, wc_path, mergeinfo);
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge() when performing merge-tracking aware
   merges.

   Walk of the working copy tree rooted at TARGET->abspath to
   depth DEPTH.  Create an svn_client__merge_path_t * for any path which meets
   one or more of the following criteria:

     1) Path has working svn:mergeinfo.
     2) Path is switched.
     3) Path is a subtree of the merge target (i.e. is not equal to
        TARGET->abspath) and has no mergeinfo of its own but
        its immediate parent has mergeinfo with non-inheritable ranges.  If
        this isn't a dry-run and the merge is between differences in the same
        repository, then this function will set working mergeinfo on the path
        equal to the mergeinfo inheritable from its parent.
     4) Path has an immediate child (or children) missing from the WC because
        the child is switched or absent from the WC, or due to a sparse
        checkout.
     5) Path has a sibling (or siblings) missing from the WC because the
        sibling is switched, absent, scheduled for deletion, or missing due to
        a sparse checkout.
     6) Path is absent from disk due to an authz restriction.
     7) Path is equal to TARGET->abspath.
     8) Path is an immediate *directory* child of
        TARGET->abspath and DEPTH is svn_depth_immediates.
     9) Path is an immediate *file* child of TARGET->abspath
        and DEPTH is svn_depth_files.
     10) Path is at a depth of 'empty' or 'files'.
     11) Path is missing from disk (e.g. due to an OS-level deletion).

   If subtrees within the requested DEPTH are unexpectedly missing disk,
   then raise SVN_ERR_CLIENT_NOT_READY_TO_MERGE.

   Store the svn_client__merge_path_t *'s in *CHILDREN_WITH_MERGEINFO in
   depth-first order based on the svn_client__merge_path_t *s path member as
   sorted by svn_path_compare_paths().  Set the remaining_ranges field of each
   element to NULL.

   Note: Since the walk is rooted at TARGET->abspath, the
   latter is guaranteed to be in *CHILDREN_WITH_MERGEINFO and due to the
   depth-first ordering it is guaranteed to be the first element in
   *CHILDREN_WITH_MERGEINFO.

   MERGE_CMD_BATON is cascaded from the argument of the same name in
   do_directory_merge().
*/
static svn_error_t *
get_mergeinfo_paths(apr_array_header_t *children_with_mergeinfo,
                    const merge_target_t *target,
                    svn_depth_t depth,
                    svn_boolean_t dry_run,
                    svn_boolean_t same_repos,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_pool_t *swmi_pool;
  apr_hash_t *subtrees_with_mergeinfo;
  apr_hash_t *excluded_subtrees;
  apr_hash_t *switched_subtrees;
  apr_hash_t *shallow_subtrees;
  apr_hash_t *missing_subtrees;
  struct pre_merge_status_baton_t pre_merge_status_baton;

  /* Case 1: Subtrees with explicit mergeinfo. */
  /* Use a subpool for subtrees_with_mergeinfo, as it can be very large
     and is temporary. */
  swmi_pool = svn_pool_create(scratch_pool);
  SVN_ERR(get_wc_explicit_mergeinfo_catalog(&subtrees_with_mergeinfo,
                                            target->abspath,
                                            depth, ctx,
                                            swmi_pool, swmi_pool));
  if (subtrees_with_mergeinfo)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, subtrees_with_mergeinfo);
           hi;
           hi = apr_hash_next(hi))
        {
          const char *wc_path = apr_hash_this_key(hi);
          svn_mergeinfo_t mergeinfo = apr_hash_this_val(hi);
          svn_client__merge_path_t *mergeinfo_child =
            svn_client__merge_path_create(wc_path, result_pool);

          svn_pool_clear(iterpool);

          /* Stash this child's pre-existing mergeinfo. */
          mergeinfo_child->pre_merge_mergeinfo = mergeinfo;

          /* Note if this child has non-inheritable mergeinfo */
          mergeinfo_child->has_noninheritable
            = svn_mergeinfo__is_noninheritable(
                mergeinfo_child->pre_merge_mergeinfo, iterpool);

          /* Append it.  We'll sort below. */
          APR_ARRAY_PUSH(children_with_mergeinfo, svn_client__merge_path_t *)
            = svn_client__merge_path_dup(mergeinfo_child, result_pool);
        }

      /* Sort CHILDREN_WITH_MERGEINFO by each child's path (i.e. as per
         compare_merge_path_t_as_paths).  Any subsequent insertions of new
         children with insert_child_to_merge() require this ordering. */
      svn_sort__array(children_with_mergeinfo, compare_merge_path_t_as_paths);
    }
  svn_pool_destroy(swmi_pool);

  /* Case 2: Switched subtrees
     Case 10: Paths at depths of 'empty' or 'files'
     Case 11: Paths missing from disk */
  pre_merge_status_baton.wc_ctx = ctx->wc_ctx;
  switched_subtrees = apr_hash_make(scratch_pool);
  pre_merge_status_baton.switched_subtrees = switched_subtrees;
  shallow_subtrees = apr_hash_make(scratch_pool);
  pre_merge_status_baton.shallow_subtrees = shallow_subtrees;
  missing_subtrees = apr_hash_make(scratch_pool);
  pre_merge_status_baton.missing_subtrees = missing_subtrees;
  pre_merge_status_baton.pool = scratch_pool;
  SVN_ERR(svn_wc_walk_status(ctx->wc_ctx,
                             target->abspath,
                             depth,
                             TRUE /* get_all */,
                             FALSE /* no_ignore */,
                             TRUE /* ignore_text_mods */,
                             NULL /* ingore_patterns */,
                             pre_merge_status_cb, &pre_merge_status_baton,
                             ctx->cancel_func, ctx->cancel_baton,
                             scratch_pool));

  /* Issue #2915: Raise an error describing the roots of any missing
     subtrees, i.e. those that the WC thinks are on disk but have been
     removed outside of Subversion. */
  if (apr_hash_count(missing_subtrees))
    {
      apr_hash_index_t *hi;
      svn_stringbuf_t *missing_subtree_err_buf =
        svn_stringbuf_create(_("Merge tracking not allowed with missing "
                               "subtrees; try restoring these items "
                               "first:\n"), scratch_pool);

      for (hi = apr_hash_first(scratch_pool, missing_subtrees);
           hi;
           hi = apr_hash_next(hi))
        {
          svn_pool_clear(iterpool);
          svn_stringbuf_appendcstr(missing_subtree_err_buf,
                                   svn_dirent_local_style(
                                     apr_hash_this_key(hi), iterpool));
          svn_stringbuf_appendcstr(missing_subtree_err_buf, "\n");
        }

      return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE,
                              NULL, missing_subtree_err_buf->data);
    }

  if (apr_hash_count(switched_subtrees))
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, switched_subtrees);
           hi;
           hi = apr_hash_next(hi))
        {
           const char *wc_path = apr_hash_this_key(hi);
           svn_client__merge_path_t *child = get_child_with_mergeinfo(
             children_with_mergeinfo, wc_path);

           if (child)
             {
               child->switched = TRUE;
             }
           else
             {
               svn_client__merge_path_t *switched_child =
                 svn_client__merge_path_create(wc_path, result_pool);
               switched_child->switched = TRUE;
               insert_child_to_merge(children_with_mergeinfo, switched_child,
                                     result_pool);
             }
        }
    }

  if (apr_hash_count(shallow_subtrees))
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, shallow_subtrees);
           hi;
           hi = apr_hash_next(hi))
        {
           svn_boolean_t new_shallow_child = FALSE;
           const char *wc_path = apr_hash_this_key(hi);
           svn_depth_t *child_depth = apr_hash_this_val(hi);
           svn_client__merge_path_t *shallow_child = get_child_with_mergeinfo(
             children_with_mergeinfo, wc_path);

           if (shallow_child)
             {
               if (*child_depth == svn_depth_empty
                   || *child_depth == svn_depth_files)
                 shallow_child->missing_child = TRUE;
             }
           else
             {
               shallow_child = svn_client__merge_path_create(wc_path,
                                                             result_pool);
               new_shallow_child = TRUE;

               if (*child_depth == svn_depth_empty
                   || *child_depth == svn_depth_files)
                 shallow_child->missing_child = TRUE;
             }

          /* A little trickery: If PATH doesn't have any mergeinfo or has
             only inheritable mergeinfo, we still describe it as having
             non-inheritable mergeinfo if it is missing a child due to
             a shallow depth.  Why? Because the mergeinfo we'll add to PATH
             to describe the merge must be non-inheritable, so PATH's missing
             children don't inherit it.  Marking these PATHs as non-
             inheritable allows the logic for case 3 to properly account
             for PATH's children. */
          if (!shallow_child->has_noninheritable
              && (*child_depth == svn_depth_empty
                  || *child_depth == svn_depth_files))
            {
              shallow_child->has_noninheritable = TRUE;
            }

          if (new_shallow_child)
            insert_child_to_merge(children_with_mergeinfo, shallow_child,
                                  result_pool);
       }
    }

  /* Case 6: Paths absent from disk due to server or user exclusion. */
  SVN_ERR(svn_wc__get_excluded_subtrees(&excluded_subtrees,
                                        ctx->wc_ctx, target->abspath,
                                        result_pool, scratch_pool));
  if (excluded_subtrees)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, excluded_subtrees);
           hi;
           hi = apr_hash_next(hi))
        {
           const char *wc_path = apr_hash_this_key(hi);
           svn_client__merge_path_t *child = get_child_with_mergeinfo(
             children_with_mergeinfo, wc_path);

           if (child)
             {
               child->absent = TRUE;
             }
           else
             {
               svn_client__merge_path_t *absent_child =
                 svn_client__merge_path_create(wc_path, result_pool);
               absent_child->absent = TRUE;
               insert_child_to_merge(children_with_mergeinfo, absent_child,
                                     result_pool);
             }
        }
    }

  /* Case 7: The merge target MERGE_CMD_BATON->target->abspath is always
     present. */
  if (!get_child_with_mergeinfo(children_with_mergeinfo,
                                target->abspath))
    {
      svn_client__merge_path_t *target_child =
        svn_client__merge_path_create(target->abspath,
                                      result_pool);
      insert_child_to_merge(children_with_mergeinfo, target_child,
                            result_pool);
    }

  /* Case 8: Path is an immediate *directory* child of
     MERGE_CMD_BATON->target->abspath and DEPTH is svn_depth_immediates.

     Case 9: Path is an immediate *file* child of
     MERGE_CMD_BATON->target->abspath and DEPTH is svn_depth_files. */
  if (depth == svn_depth_immediates || depth == svn_depth_files)
    {
      int j;
      const apr_array_header_t *immediate_children;

      SVN_ERR(svn_wc__node_get_children_of_working_node(
        &immediate_children, ctx->wc_ctx,
        target->abspath, scratch_pool, scratch_pool));

      for (j = 0; j < immediate_children->nelts; j++)
        {
          const char *immediate_child_abspath =
            APR_ARRAY_IDX(immediate_children, j, const char *);
          svn_node_kind_t immediate_child_kind;

          svn_pool_clear(iterpool);
          SVN_ERR(svn_wc_read_kind2(&immediate_child_kind,
                                    ctx->wc_ctx, immediate_child_abspath,
                                    FALSE, FALSE, iterpool));
          if ((immediate_child_kind == svn_node_dir
               && depth == svn_depth_immediates)
              || (immediate_child_kind == svn_node_file
                  && depth == svn_depth_files))
            {
              if (!get_child_with_mergeinfo(children_with_mergeinfo,
                                            immediate_child_abspath))
                {
                  svn_client__merge_path_t *immediate_child =
                    svn_client__merge_path_create(immediate_child_abspath,
                                                  result_pool);

                  if (immediate_child_kind == svn_node_dir
                      && depth == svn_depth_immediates)
                    immediate_child->immediate_child_dir = TRUE;

                  insert_child_to_merge(children_with_mergeinfo,
                                        immediate_child, result_pool);
                }
            }
        }
    }

  /* If DEPTH isn't empty then cover cases 3), 4), and 5), possibly adding
     elements to CHILDREN_WITH_MERGEINFO. */
  if (depth <= svn_depth_empty)
    return SVN_NO_ERROR;

  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i,
                      svn_client__merge_path_t *);
      svn_pool_clear(iterpool);

      /* Case 3) Where merging to a path with a switched child the path
         gets non-inheritable mergeinfo for the merge range performed and
         the child gets its own set of mergeinfo.  If the switched child
         later "returns", e.g. a switched path is unswitched, the child
         may not have any explicit mergeinfo.  If the initial merge is
         repeated we don't want to repeat the merge for the path, but we
         do want to repeat it for the previously switched child.  To
         ensure this we check if all of CHILD's non-missing children have
         explicit mergeinfo (they should already be present in
         CHILDREN_WITH_MERGEINFO if they do).  If not,
         add the children without mergeinfo to CHILDREN_WITH_MERGEINFO so
         do_directory_merge() will merge them independently.

         But that's not enough!  Since do_directory_merge() performs
         the merges on the paths in CHILDREN_WITH_MERGEINFO in a depth
         first manner it will merge the previously switched path's parent
         first.  As part of this merge it will update the parent's
         previously non-inheritable mergeinfo and make it inheritable
         (since it notices the path has no missing children), then when
         do_directory_merge() finally merges the previously missing
         child it needs to get mergeinfo from the child's nearest
         ancestor, but since do_directory_merge() already tweaked that
         mergeinfo, removing the non-inheritable flag, it appears that the
         child already has been merged to.  To prevent this we set
         override mergeinfo on the child now, before any merging is done,
         so it has explicit mergeinfo that reflects only CHILD's
         inheritable mergeinfo. */

      /* If depth is immediates or files then don't add new children if
         CHILD is a subtree of the merge target; those children are below
         the operational depth of the merge. */
      if (child->has_noninheritable
          && (i == 0 || depth == svn_depth_infinity))
        {
          const apr_array_header_t *children;
          int j;

          SVN_ERR(svn_wc__node_get_children_of_working_node(
                                            &children,
                                            ctx->wc_ctx,
                                            child->abspath,
                                            iterpool, iterpool));
          for (j = 0; j < children->nelts; j++)
            {
              svn_client__merge_path_t *child_of_noninheritable;
              const char *child_abspath = APR_ARRAY_IDX(children, j,
                                                        const char*);

              /* Does this child already exist in CHILDREN_WITH_MERGEINFO?
                 If not, create it and insert it into
                 CHILDREN_WITH_MERGEINFO and set override mergeinfo on
                 it. */
              child_of_noninheritable =
                get_child_with_mergeinfo(children_with_mergeinfo,
                                         child_abspath);
              if (!child_of_noninheritable)
                {
                  /* Don't add directory children if DEPTH
                     is svn_depth_files. */
                  if (depth == svn_depth_files)
                    {
                      svn_node_kind_t child_kind;
                      SVN_ERR(svn_wc_read_kind2(&child_kind,
                                                ctx->wc_ctx, child_abspath,
                                                FALSE, FALSE, iterpool));
                      if (child_kind != svn_node_file)
                        continue;
                    }
                  /* else DEPTH is infinity or immediates so we want both
                     directory and file children. */

                  child_of_noninheritable =
                    svn_client__merge_path_create(child_abspath, result_pool);
                  child_of_noninheritable->child_of_noninheritable = TRUE;
                  insert_child_to_merge(children_with_mergeinfo,
                                        child_of_noninheritable,
                                        result_pool);
                  if (!dry_run && same_repos)
                    {
                      svn_mergeinfo_t mergeinfo;

                      SVN_ERR(svn_client__get_wc_mergeinfo(
                        &mergeinfo, NULL,
                        svn_mergeinfo_nearest_ancestor,
                        child_of_noninheritable->abspath,
                        target->abspath, NULL, FALSE,
                        ctx, iterpool, iterpool));

                      SVN_ERR(svn_client__record_wc_mergeinfo(
                        child_of_noninheritable->abspath, mergeinfo,
                        FALSE, ctx, iterpool));
                    }
                }
            }
        }
      /* Case 4 and 5 are handled by the following function. */
      SVN_ERR(insert_parent_and_sibs_of_sw_absent_del_subtree(
        children_with_mergeinfo, target, &i, child,
        depth, ctx, result_pool));
    } /* i < children_with_mergeinfo->nelts */
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Implements the svn_log_entry_receiver_t interface.
 *
 * BATON is an 'apr_array_header_t *' array of 'svn_revnum_t'.
 * Push a copy of LOG_ENTRY->revision onto BATON.  Thus, a
 * series of invocations of this callback accumulates the
 * corresponding set of revisions into BATON.
 */
static svn_error_t *
log_changed_revs(void *baton,
                 svn_log_entry_t *log_entry,
                 apr_pool_t *pool)
{
  apr_array_header_t *revs = baton;

  APR_ARRAY_PUSH(revs, svn_revnum_t) = log_entry->revision;
  return SVN_NO_ERROR;
}


/* Set *MIN_REV_P to the oldest and *MAX_REV_P to the youngest start or end
 * revision occurring in RANGELIST, or to SVN_INVALID_REVNUM if RANGELIST
 * is empty. */
static void
merge_range_find_extremes(svn_revnum_t *min_rev_p,
                          svn_revnum_t *max_rev_p,
                          const svn_rangelist_t *rangelist)
{
  int i;

  *min_rev_p = SVN_INVALID_REVNUM;
  *max_rev_p = SVN_INVALID_REVNUM;
  for (i = 0; i < rangelist->nelts; i++)
    {
      svn_merge_range_t *range
        = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
      svn_revnum_t range_min = MIN(range->start, range->end);
      svn_revnum_t range_max = MAX(range->start, range->end);

      if ((! SVN_IS_VALID_REVNUM(*min_rev_p)) || (range_min < *min_rev_p))
        *min_rev_p = range_min;
      if ((! SVN_IS_VALID_REVNUM(*max_rev_p)) || (range_max > *max_rev_p))
        *max_rev_p = range_max;
    }
}

/* Wrapper around svn_ra_get_log2(). Invoke RECEIVER with RECEIVER_BATON
 * on each commit from YOUNGEST_REV to OLDEST_REV in which TARGET_RELPATH
 * changed.  TARGET_RELPATH is relative to RA_SESSION's URL.
 * Important: Revision properties are not retrieved by this function for
 * performance reasons.
 */
static svn_error_t *
get_log(svn_ra_session_t *ra_session,
        const char *target_relpath,
        svn_revnum_t youngest_rev,
        svn_revnum_t oldest_rev,
        svn_boolean_t discover_changed_paths,
        svn_log_entry_receiver_t receiver,
        void *receiver_baton,
        apr_pool_t *pool)
{
  apr_array_header_t *log_targets;
  apr_array_header_t *revprops;

  log_targets = apr_array_make(pool, 1, sizeof(const char *));
  APR_ARRAY_PUSH(log_targets, const char *) = target_relpath;

  revprops = apr_array_make(pool, 0, sizeof(const char *));

  SVN_ERR(svn_ra_get_log2(ra_session, log_targets, youngest_rev,
                          oldest_rev, 0 /* limit */, discover_changed_paths,
                          FALSE /* strict_node_history */,
                          FALSE /* include_merged_revisions */,
                          revprops, receiver, receiver_baton, pool));

  return SVN_NO_ERROR;
}

/* Set *OPERATIVE_RANGES_P to an array of svn_merge_range_t * merge
   range objects copied wholesale from RANGES which have the property
   that in some revision within that range the object identified by
   RA_SESSION was modified (if by "modified" we mean "'svn log' would
   return that revision).  *OPERATIVE_RANGES_P is allocated from the
   same pool as RANGES, and the ranges within it are shared with
   RANGES, too.

   *OPERATIVE_RANGES_P may be the same as RANGES (that is, the output
   parameter is set only after the input is no longer used).

   Use POOL for temporary allocations.  */
static svn_error_t *
remove_noop_merge_ranges(svn_rangelist_t **operative_ranges_p,
                         svn_ra_session_t *ra_session,
                         const svn_rangelist_t *ranges,
                         apr_pool_t *pool)
{
  int i;
  svn_revnum_t oldest_rev, youngest_rev;
  apr_array_header_t *changed_revs =
    apr_array_make(pool, ranges->nelts, sizeof(svn_revnum_t));
  svn_rangelist_t *operative_ranges =
    apr_array_make(ranges->pool, ranges->nelts, ranges->elt_size);

  /* Find the revision extremes of the RANGES we have. */
  merge_range_find_extremes(&oldest_rev, &youngest_rev, ranges);
  if (SVN_IS_VALID_REVNUM(oldest_rev))
    oldest_rev++;  /* make it inclusive */

  /* Get logs across those ranges, recording which revisions hold
     changes to our object's history. */
  SVN_ERR(get_log(ra_session, "", youngest_rev, oldest_rev, FALSE,
                  log_changed_revs, changed_revs, pool));

  /* Are there *any* changes? */
  if (changed_revs->nelts)
    {
      /* Our list of changed revisions should be in youngest-to-oldest
         order. */
      svn_revnum_t youngest_changed_rev
        = APR_ARRAY_IDX(changed_revs, 0, svn_revnum_t);
      svn_revnum_t oldest_changed_rev
        = APR_ARRAY_IDX(changed_revs, changed_revs->nelts - 1, svn_revnum_t);

      /* Now, copy from RANGES to *OPERATIVE_RANGES, filtering out ranges
         that aren't operative (by virtue of not having any revisions
         represented in the CHANGED_REVS array). */
      for (i = 0; i < ranges->nelts; i++)
        {
          svn_merge_range_t *range = APR_ARRAY_IDX(ranges, i,
                                                   svn_merge_range_t *);
          svn_revnum_t range_min = MIN(range->start, range->end) + 1;
          svn_revnum_t range_max = MAX(range->start, range->end);
          int j;

          /* If the merge range is entirely outside the range of changed
             revisions, we've no use for it. */
          if ((range_min > youngest_changed_rev)
              || (range_max < oldest_changed_rev))
            continue;

          /* Walk through the changed_revs to see if any of them fall
             inside our current range. */
          for (j = 0; j < changed_revs->nelts; j++)
            {
              svn_revnum_t changed_rev
                = APR_ARRAY_IDX(changed_revs, j, svn_revnum_t);
              if ((changed_rev >= range_min) && (changed_rev <= range_max))
                {
                  APR_ARRAY_PUSH(operative_ranges, svn_merge_range_t *) =
                    range;
                  break;
                }
            }
        }
    }

  *operative_ranges_p = operative_ranges;
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/*** Merge Source Normalization ***/

/* qsort-compatible sort routine, rating merge_source_t * objects to
   be in descending (youngest-to-oldest) order based on their ->loc1->rev
   component. */
static int
compare_merge_source_ts(const void *a,
                        const void *b)
{
  svn_revnum_t a_rev = (*(const merge_source_t *const *)a)->loc1->rev;
  svn_revnum_t b_rev = (*(const merge_source_t *const *)b)->loc1->rev;
  if (a_rev == b_rev)
    return 0;
  return a_rev < b_rev ? 1 : -1;
}

/* Set *MERGE_SOURCE_TS_P to a list of merge sources generated by
   slicing history location SEGMENTS with a given requested merge
   RANGE.  Use SOURCE_LOC for full source URL calculation.

   Order the merge sources in *MERGE_SOURCE_TS_P from oldest to
   youngest. */
static svn_error_t *
combine_range_with_segments(apr_array_header_t **merge_source_ts_p,
                            const svn_merge_range_t *range,
                            const apr_array_header_t *segments,
                            const svn_client__pathrev_t *source_loc,
                            apr_pool_t *pool)
{
  apr_array_header_t *merge_source_ts =
    apr_array_make(pool, 1, sizeof(merge_source_t *));
  svn_revnum_t minrev = MIN(range->start, range->end) + 1;
  svn_revnum_t maxrev = MAX(range->start, range->end);
  svn_boolean_t subtractive = (range->start > range->end);
  int i;

  for (i = 0; i < segments->nelts; i++)
    {
      svn_location_segment_t *segment =
        APR_ARRAY_IDX(segments, i, svn_location_segment_t *);
      svn_client__pathrev_t *loc1, *loc2;
      merge_source_t *merge_source;
      const char *path1 = NULL;
      svn_revnum_t rev1;

      /* If this segment doesn't overlap our range at all, or
         represents a gap, ignore it. */
      if ((segment->range_end < minrev)
          || (segment->range_start > maxrev)
          || (! segment->path))
        continue;

      /* If our range spans a segment boundary, we have to point our
         merge_source_t's path1 to the path of the immediately older
         segment, else it points to the same location as its path2.  */
      rev1 = MAX(segment->range_start, minrev) - 1;
      if (minrev <= segment->range_start)
        {
          if (i > 0)
            {
              path1 = (APR_ARRAY_IDX(segments, i - 1,
                                     svn_location_segment_t *))->path;
            }
          /* If we've backed PATH1 up into a segment gap, let's back
             it up further still to the segment before the gap.  We'll
             have to adjust rev1, too. */
          if ((! path1) && (i > 1))
            {
              path1 = (APR_ARRAY_IDX(segments, i - 2,
                                     svn_location_segment_t *))->path;
              rev1 = (APR_ARRAY_IDX(segments, i - 2,
                                    svn_location_segment_t *))->range_end;
            }
        }
      else
        {
          path1 = apr_pstrdup(pool, segment->path);
        }

      /* If we don't have two valid paths, we won't know what to do
         when merging.  This could happen if someone requested a merge
         where the source didn't exist in a particular revision or
         something.  The merge code would probably bomb out anyway, so
         we'll just *not* create a merge source in this case. */
      if (! (path1 && segment->path))
        continue;

      /* Build our merge source structure. */
      loc1 = svn_client__pathrev_create_with_relpath(
               source_loc->repos_root_url, source_loc->repos_uuid,
               rev1, path1, pool);
      loc2 = svn_client__pathrev_create_with_relpath(
               source_loc->repos_root_url, source_loc->repos_uuid,
               MIN(segment->range_end, maxrev), segment->path, pool);
      /* If this is subtractive, reverse the whole calculation. */
      if (subtractive)
        merge_source = merge_source_create(loc2, loc1, TRUE /* ancestral */,
                                           pool);
      else
        merge_source = merge_source_create(loc1, loc2, TRUE /* ancestral */,
                                           pool);

      APR_ARRAY_PUSH(merge_source_ts, merge_source_t *) = merge_source;
    }

  /* If this was a subtractive merge, and we created more than one
     merge source, we need to reverse the sort ordering of our sources. */
  if (subtractive && (merge_source_ts->nelts > 1))
    svn_sort__array(merge_source_ts, compare_merge_source_ts);

  *merge_source_ts_p = merge_source_ts;
  return SVN_NO_ERROR;
}

/* Similar to normalize_merge_sources() except the input MERGE_RANGE_TS is a
 * rangelist.
 */
static svn_error_t *
normalize_merge_sources_internal(apr_array_header_t **merge_sources_p,
                                 const svn_client__pathrev_t *source_loc,
                                 const svn_rangelist_t *merge_range_ts,
                                 svn_ra_session_t *ra_session,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_revnum_t source_peg_revnum = source_loc->rev;
  svn_revnum_t oldest_requested, youngest_requested;
  svn_revnum_t trim_revision = SVN_INVALID_REVNUM;
  apr_array_header_t *segments;
  int i;

  /* Initialize our return variable. */
  *merge_sources_p = apr_array_make(result_pool, 1, sizeof(merge_source_t *));

  /* No ranges to merge?  No problem. */
  if (merge_range_ts->nelts == 0)
    return SVN_NO_ERROR;

  /* Find the extremes of the revisions across our set of ranges. */
  merge_range_find_extremes(&oldest_requested, &youngest_requested,
                            merge_range_ts);

  /* ### FIXME:  Our underlying APIs can't yet handle the case where
     the peg revision isn't the youngest of the three revisions.  So
     we'll just verify that the source in the peg revision is related
     to the source in the youngest requested revision (which is
     all the underlying APIs would do in this case right now anyway). */
  if (source_peg_revnum < youngest_requested)
    {
      svn_client__pathrev_t *start_loc;

      SVN_ERR(svn_client__repos_location(&start_loc,
                                         ra_session, source_loc,
                                         youngest_requested,
                                         ctx, scratch_pool, scratch_pool));
      source_peg_revnum = youngest_requested;
    }

  /* Fetch the locations for our merge range span. */
  SVN_ERR(svn_client__repos_location_segments(&segments,
                                              ra_session, source_loc->url,
                                              source_peg_revnum,
                                              youngest_requested,
                                              oldest_requested,
                                              ctx, result_pool));

  /* See if we fetched enough history to do the job.  "Surely we did,"
     you say.  "After all, we covered the entire requested merge
     range."  Yes, that's true, but if our first segment doesn't
     extend back to the oldest request revision, we've got a special
     case to deal with.  Or if the first segment represents a gap,
     that's another special case.  */
  trim_revision = SVN_INVALID_REVNUM;
  if (segments->nelts)
    {
      svn_location_segment_t *first_segment =
        APR_ARRAY_IDX(segments, 0, svn_location_segment_t *);

      /* If the first segment doesn't start with the OLDEST_REQUESTED
         revision, we'll need to pass a trim revision to our range
         cruncher. */
      if (first_segment->range_start != oldest_requested)
        {
          trim_revision = first_segment->range_start;
        }

      /* Else, if the first segment has no path (and therefore is a
         gap), then we'll fetch the copy source revision from the
         second segment (provided there is one, of course) and use it
         to prepend an extra pathful segment to our list.

         ### We could avoid this bit entirely if we'd passed
         ### SVN_INVALID_REVNUM instead of OLDEST_REQUESTED to
         ### svn_client__repos_location_segments(), but that would
         ### really penalize clients hitting pre-1.5 repositories with
         ### the typical small merge range request (because of the
         ### lack of a node-origins cache in the repository).  */
      else if (! first_segment->path)
        {
          if (segments->nelts > 1)
            {
              svn_location_segment_t *second_segment =
                APR_ARRAY_IDX(segments, 1, svn_location_segment_t *);
              const char *segment_url;
              const char *original_repos_relpath;
              svn_revnum_t original_revision;
              svn_opt_revision_t range_start_rev;
              range_start_rev.kind = svn_opt_revision_number;
              range_start_rev.value.number = second_segment->range_start;

              segment_url = svn_path_url_add_component2(
                              source_loc->repos_root_url, second_segment->path,
                              scratch_pool);
              SVN_ERR(svn_client__get_copy_source(&original_repos_relpath,
                                                  &original_revision,
                                                  segment_url,
                                                  &range_start_rev,
                                                  ra_session, ctx,
                                                  result_pool, scratch_pool));
              /* Got copyfrom data?  Fix up the first segment to cover
                 back to COPYFROM_REV + 1, and then prepend a new
                 segment covering just COPYFROM_REV. */
              if (original_repos_relpath)
                {
                  svn_location_segment_t *new_segment =
                    apr_pcalloc(result_pool, sizeof(*new_segment));

                  new_segment->path = original_repos_relpath;
                  new_segment->range_start = original_revision;
                  new_segment->range_end = original_revision;
                  svn_sort__array_insert(segments, &new_segment, 0);
                }
            }
        }
    }

  /* For each range in our requested range set, try to determine the
     path(s) associated with that range.  */
  for (i = 0; i < merge_range_ts->nelts; i++)
    {
      svn_merge_range_t *range =
        APR_ARRAY_IDX(merge_range_ts, i, svn_merge_range_t *);
      apr_array_header_t *merge_sources;

      if (SVN_IS_VALID_REVNUM(trim_revision))
        {
          /* If the range predates the trim revision, discard it. */
          if (MAX(range->start, range->end) < trim_revision)
            continue;

          /* If the range overlaps the trim revision, trim it. */
          if (range->start < trim_revision)
            range->start = trim_revision;
          if (range->end < trim_revision)
            range->end = trim_revision;
        }

      /* Copy the resulting merge sources into master list thereof. */
      SVN_ERR(combine_range_with_segments(&merge_sources, range,
                                          segments, source_loc,
                                          result_pool));
      apr_array_cat(*merge_sources_p, merge_sources);
    }

  return SVN_NO_ERROR;
}

/* Determine the normalized ranges to merge from a given line of history.

   Calculate the result by intersecting the list of location segments at
   which SOURCE_LOC existed along its line of history with the requested
   revision ranges in RANGES_TO_MERGE.  RANGES_TO_MERGE is an array of
   (svn_opt_revision_range_t *) revision ranges.  Use SOURCE_PATH_OR_URL to
   resolve any WC-relative revision specifiers (such as 'base') in
   RANGES_TO_MERGE.

   Set *MERGE_SOURCES_P to an array of merge_source_t * objects, each
   describing a normalized range of revisions to be merged from the line
   history of SOURCE_LOC.  Order the objects from oldest to youngest.

   RA_SESSION is an RA session open to the repository of SOURCE_LOC; it may
   be temporarily reparented within this function.  Use RA_SESSION to find
   the location segments along the line of history of SOURCE_LOC.

   Allocate MERGE_SOURCES_P and its contents in RESULT_POOL.

   See `MERGEINFO MERGE SOURCE NORMALIZATION' for more on the
   background of this function.
*/
static svn_error_t *
normalize_merge_sources(apr_array_header_t **merge_sources_p,
                        const char *source_path_or_url,
                        const svn_client__pathrev_t *source_loc,
                        const apr_array_header_t *ranges_to_merge,
                        svn_ra_session_t *ra_session,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const char *source_abspath_or_url;
  svn_revnum_t youngest_rev = SVN_INVALID_REVNUM;
  svn_rangelist_t *merge_range_ts;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  if(!svn_path_is_url(source_path_or_url))
    SVN_ERR(svn_dirent_get_absolute(&source_abspath_or_url, source_path_or_url,
                                    scratch_pool));
  else
    source_abspath_or_url = source_path_or_url;

  /* Create a list to hold svn_merge_range_t's. */
  merge_range_ts = apr_array_make(scratch_pool, ranges_to_merge->nelts,
                                  sizeof(svn_merge_range_t *));

  for (i = 0; i < ranges_to_merge->nelts; i++)
    {
      svn_opt_revision_range_t *range
        = APR_ARRAY_IDX(ranges_to_merge, i, svn_opt_revision_range_t *);
      svn_merge_range_t mrange;

      svn_pool_clear(iterpool);

      /* Resolve revisions to real numbers, validating as we go. */
      if ((range->start.kind == svn_opt_revision_unspecified)
          || (range->end.kind == svn_opt_revision_unspecified))
        return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                                _("Not all required revisions are specified"));

      SVN_ERR(svn_client__get_revision_number(&mrange.start, &youngest_rev,
                                              ctx->wc_ctx,
                                              source_abspath_or_url,
                                              ra_session, &range->start,
                                              iterpool));
      SVN_ERR(svn_client__get_revision_number(&mrange.end, &youngest_rev,
                                              ctx->wc_ctx,
                                              source_abspath_or_url,
                                              ra_session, &range->end,
                                              iterpool));

      /* If this isn't a no-op range... */
      if (mrange.start != mrange.end)
        {
          /* ...then add it to the list. */
          mrange.inheritable = TRUE;
          APR_ARRAY_PUSH(merge_range_ts, svn_merge_range_t *)
            = svn_merge_range_dup(&mrange, scratch_pool);
        }
    }

  SVN_ERR(normalize_merge_sources_internal(
            merge_sources_p, source_loc,
            merge_range_ts, ra_session, ctx, result_pool, scratch_pool));

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


/*-----------------------------------------------------------------------*/

/*** Merge Workhorse Functions ***/

/* Helper for do_directory_merge() and do_file_merge() which filters out a
   path's own natural history from the mergeinfo describing a merge.

   Given the natural history IMPLICIT_MERGEINFO of some wc merge target path,
   the repository-relative merge source path SOURCE_REL_PATH, and the
   requested merge range REQUESTED_RANGE from SOURCE_REL_PATH, remove any
   portion of REQUESTED_RANGE which is already described in
   IMPLICIT_MERGEINFO.  Store the result in *FILTERED_RANGELIST.

   This function only filters natural history for mergeinfo that will be
   *added* during a forward merge.  Removing natural history from explicit
   mergeinfo is harmless.  If REQUESTED_RANGE describes a reverse merge,
   then *FILTERED_RANGELIST is simply populated with one range described
   by REQUESTED_RANGE.  *FILTERED_RANGELIST is never NULL.

   Allocate *FILTERED_RANGELIST in POOL. */
static svn_error_t *
filter_natural_history_from_mergeinfo(svn_rangelist_t **filtered_rangelist,
                                      const char *source_rel_path,
                                      svn_mergeinfo_t implicit_mergeinfo,
                                      svn_merge_range_t *requested_range,
                                      apr_pool_t *pool)
{
  /* Make the REQUESTED_RANGE into a rangelist. */
  svn_rangelist_t *requested_rangelist =
    svn_rangelist__initialize(requested_range->start, requested_range->end,
                              requested_range->inheritable, pool);

  *filtered_rangelist = NULL;

  /* For forward merges: If the IMPLICIT_MERGEINFO already describes ranges
     associated with SOURCE_REL_PATH then filter those ranges out. */
  if (implicit_mergeinfo
      && (requested_range->start < requested_range->end))
    {
      svn_rangelist_t *implied_rangelist =
                        svn_hash_gets(implicit_mergeinfo, source_rel_path);

      if (implied_rangelist)
        SVN_ERR(svn_rangelist_remove(filtered_rangelist,
                                     implied_rangelist,
                                     requested_rangelist,
                                     FALSE, pool));
    }

  /* If no filtering was performed the filtered rangelist is
     simply the requested rangelist.*/
  if (! (*filtered_rangelist))
    *filtered_rangelist = requested_rangelist;

  return SVN_NO_ERROR;
}

/* Return a merge source representing the sub-range from START_REV to
   END_REV of SOURCE.  SOURCE obeys the rules described in the
   'MERGEINFO MERGE SOURCE NORMALIZATION' comment at the top of this file.
   The younger of START_REV and END_REV is inclusive while the older is
   exclusive.

   Allocate the result structure in POOL but leave the URLs in it as shallow
   copies of the URLs in SOURCE.
*/
static merge_source_t *
subrange_source(const merge_source_t *source,
                svn_revnum_t start_rev,
                svn_revnum_t end_rev,
                apr_pool_t *pool)
{
  svn_boolean_t is_rollback = (source->loc1->rev > source->loc2->rev);
  svn_boolean_t same_urls = (strcmp(source->loc1->url, source->loc2->url) == 0);
  svn_client__pathrev_t loc1 = *source->loc1;
  svn_client__pathrev_t loc2 = *source->loc2;

  /* For this function we require that the input source is 'ancestral'. */
  SVN_ERR_ASSERT_NO_RETURN(source->ancestral);
  SVN_ERR_ASSERT_NO_RETURN(start_rev != end_rev);

  loc1.rev = start_rev;
  loc2.rev = end_rev;
  if (! same_urls)
    {
      if (is_rollback && (end_rev != source->loc2->rev))
        {
          loc2.url = source->loc1->url;
        }
      if ((! is_rollback) && (start_rev != source->loc1->rev))
        {
          loc1.url = source->loc2->url;
        }
    }
  return merge_source_create(&loc1, &loc2, source->ancestral, pool);
}

/* The single-file, simplified version of do_directory_merge(), which see for
   parameter descriptions.

   Additional parameters:

   If SOURCES_RELATED is set, the "left" and "right" sides of SOURCE are
   historically related (ancestors, uncles, second
   cousins thrice removed, etc...).  (This is used to simulate the
   history checks that the repository logic does in the directory case.)

   If mergeinfo is being recorded to describe this merge, and RESULT_CATALOG
   is not NULL, then don't record the new mergeinfo on the TARGET_ABSPATH,
   but instead record it in RESULT_CATALOG, where the key is TARGET_ABSPATH
   and the value is the new mergeinfo for that path.  Allocate additions
   to RESULT_CATALOG in pool which RESULT_CATALOG was created in.

   CONFLICTED_RANGE is as documented for do_directory_merge().

   Note: MERGE_B->RA_SESSION1 must be associated with SOURCE->loc1->url and
   MERGE_B->RA_SESSION2 with SOURCE->loc2->url.
*/
static svn_error_t *
do_file_merge(svn_mergeinfo_catalog_t result_catalog,
              single_range_conflict_report_t **conflict_report,
              const merge_source_t *source,
              const char *target_abspath,
              const svn_diff_tree_processor_t *processor,
              svn_boolean_t sources_related,
              svn_boolean_t squelch_mergeinfo_notifications,
              merge_cmd_baton_t *merge_b,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_rangelist_t *remaining_ranges;
  svn_client_ctx_t *ctx = merge_b->ctx;
  svn_merge_range_t range;
  svn_mergeinfo_t target_mergeinfo;
  svn_boolean_t inherited = FALSE;
  svn_boolean_t is_rollback = (source->loc1->rev > source->loc2->rev);
  const svn_client__pathrev_t *primary_src
    = is_rollback ? source->loc1 : source->loc2;
  svn_boolean_t honor_mergeinfo = HONOR_MERGEINFO(merge_b);
  svn_client__merge_path_t *merge_target = NULL;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  SVN_ERR_ASSERT(svn_dirent_is_absolute(target_abspath));

  *conflict_report = NULL;

  /* Note that this is a single-file merge. */
  range.start = source->loc1->rev;
  range.end = source->loc2->rev;
  range.inheritable = TRUE;

  merge_target = svn_client__merge_path_create(target_abspath, scratch_pool);

  if (honor_mergeinfo)
    {
      svn_error_t *err;

      /* Fetch mergeinfo. */
      err = get_full_mergeinfo(&target_mergeinfo,
                               &(merge_target->implicit_mergeinfo),
                               &inherited, svn_mergeinfo_inherited,
                               merge_b->ra_session1, target_abspath,
                               MAX(source->loc1->rev, source->loc2->rev),
                               MIN(source->loc1->rev, source->loc2->rev),
                               ctx, scratch_pool, iterpool);

      if (err)
        {
          if (err->apr_err == SVN_ERR_MERGEINFO_PARSE_ERROR)
            {
              err = svn_error_createf(
                SVN_ERR_CLIENT_INVALID_MERGEINFO_NO_MERGETRACKING, err,
                _("Invalid mergeinfo detected on merge target '%s', "
                  "merge tracking not possible"),
                svn_dirent_local_style(target_abspath, scratch_pool));
            }
          return svn_error_trace(err);
        }

      /* Calculate remaining merges unless this is a record only merge.
         In that case the remaining range is the whole range described
         by SOURCE->rev1:rev2. */
      if (!merge_b->record_only)
        {
          /* ### Bug?  calculate_remaining_ranges() needs 'source' to adhere
           *   to the requirements of 'MERGEINFO MERGE SOURCE NORMALIZATION'
           *   here, but it doesn't appear to be guaranteed so. */
          SVN_ERR(calculate_remaining_ranges(NULL, merge_target,
                                             source,
                                             target_mergeinfo,
                                             merge_b->implicit_src_gap, FALSE,
                                             merge_b->ra_session1,
                                             ctx, scratch_pool,
                                             iterpool));
          remaining_ranges = merge_target->remaining_ranges;

          /* We are honoring mergeinfo and this is not a simple record only
             merge which blindly records mergeinfo describing the merge of
             SOURCE->LOC1->URL@SOURCE->LOC1->REV through
             SOURCE->LOC2->URL@SOURCE->LOC2->REV.  This means that the oldest
             and youngest revisions merged (as determined above by
             calculate_remaining_ranges) might differ from those described
             in SOURCE.  To keep the '--- Merging *' notifications consistent
             with the '--- Recording mergeinfo *' notifications, we adjust
             RANGE to account for such changes. */
          if (remaining_ranges->nelts)
            {
              svn_merge_range_t *adj_start_range =
                APR_ARRAY_IDX(remaining_ranges, 0, svn_merge_range_t *);
              svn_merge_range_t *adj_end_range =
                APR_ARRAY_IDX(remaining_ranges, remaining_ranges->nelts - 1,
                              svn_merge_range_t *);
              range.start = adj_start_range->start;
              range.end = adj_end_range->end;
            }
        }
    }

  /* The simple cases where our remaining range is SOURCE->rev1:rev2. */
  if (!honor_mergeinfo || merge_b->record_only)
    {
      remaining_ranges = apr_array_make(scratch_pool, 1, sizeof(&range));
      APR_ARRAY_PUSH(remaining_ranges, svn_merge_range_t *) = &range;
    }

  if (!merge_b->record_only)
    {
      svn_rangelist_t *ranges_to_merge = apr_array_copy(scratch_pool,
                                                        remaining_ranges);
      const char *target_relpath = "";  /* relative to root of merge */

      if (source->ancestral)
        {
          apr_array_header_t *child_with_mergeinfo;
          svn_client__merge_path_t *target_info;

          /* If we have ancestrally related sources and more than one
             range to merge, eliminate no-op ranges before going through
             the effort of downloading the many copies of the file
             required to do these merges (two copies per range). */
          if (remaining_ranges->nelts > 1)
            {
              const char *old_sess_url;
              svn_error_t *err;

              SVN_ERR(svn_client__ensure_ra_session_url(&old_sess_url,
                                                        merge_b->ra_session1,
                                                        primary_src->url,
                                                        iterpool));
              err = remove_noop_merge_ranges(&ranges_to_merge,
                                             merge_b->ra_session1,
                                             remaining_ranges, scratch_pool);
              SVN_ERR(svn_error_compose_create(
                        err, svn_ra_reparent(merge_b->ra_session1,
                                             old_sess_url, iterpool)));
            }

          /* To support notify_merge_begin() initialize our
             CHILD_WITH_MERGEINFO. See the comment
             'THE CHILDREN_WITH_MERGEINFO ARRAY' at the start of this file. */

          child_with_mergeinfo = apr_array_make(scratch_pool, 1,
                                        sizeof(svn_client__merge_path_t *));

          /* ### Create a fake copy of merge_target as we don't keep
                 remaining_ranges in sync (yet). */
          target_info = apr_pcalloc(scratch_pool, sizeof(*target_info));

          target_info->abspath = merge_target->abspath;
          target_info->remaining_ranges = ranges_to_merge;

          APR_ARRAY_PUSH(child_with_mergeinfo, svn_client__merge_path_t *)
                                    = target_info;

          /* And store in baton to allow using it from notify_merge_begin() */
          merge_b->notify_begin.nodes_with_mergeinfo = child_with_mergeinfo;
        }

      while (ranges_to_merge->nelts > 0)
        {
          svn_merge_range_t *r = APR_ARRAY_IDX(ranges_to_merge, 0,
                                               svn_merge_range_t *);
          const merge_source_t *real_source;
          const char *left_file, *right_file;
          apr_hash_t *left_props, *right_props;
          const svn_diff_source_t *left_source;
          const svn_diff_source_t *right_source;

          svn_pool_clear(iterpool);

          /* Ensure any subsequent drives gets their own notification. */
          merge_b->notify_begin.last_abspath = NULL;

          /* While we currently don't allow it, in theory we could be
             fetching two fulltexts from two different repositories here. */
          if (source->ancestral)
            real_source = subrange_source(source, r->start, r->end, iterpool);
          else
            real_source = source;
          SVN_ERR(single_file_merge_get_file(&left_file, &left_props,
                                             merge_b->ra_session1,
                                             real_source->loc1,
                                             target_abspath,
                                             iterpool, iterpool));
          SVN_ERR(single_file_merge_get_file(&right_file, &right_props,
                                             merge_b->ra_session2,
                                             real_source->loc2,
                                             target_abspath,
                                             iterpool, iterpool));
          /* Calculate sources for the diff processor */
          left_source = svn_diff__source_create(r->start, iterpool);
          right_source = svn_diff__source_create(r->end, iterpool);


          /* If the sources are related or we're ignoring ancestry in diffs,
             do a text-n-props merge; otherwise, do a delete-n-add merge. */
          if (! (merge_b->diff_ignore_ancestry || sources_related))
            {
              struct merge_dir_baton_t dir_baton;
              void *file_baton;
              svn_boolean_t skip;

              /* Initialize minimal dir baton to allow calculating 'R'eplace
                 from 'D'elete + 'A'dd. */

              memset(&dir_baton, 0, sizeof(dir_baton));
              dir_baton.pool = iterpool;
              dir_baton.tree_conflict_reason = CONFLICT_REASON_NONE;
              dir_baton.tree_conflict_action = svn_wc_conflict_action_edit;
              dir_baton.skip_reason = svn_wc_notify_state_unknown;

              /* Delete... */
              file_baton = NULL;
              skip = FALSE;
              SVN_ERR(processor->file_opened(&file_baton, &skip, target_relpath,
                                             left_source,
                                             NULL /* right_source */,
                                             NULL /* copyfrom_source */,
                                             &dir_baton,
                                             processor,
                                             iterpool, iterpool));
              if (! skip)
                SVN_ERR(processor->file_deleted(target_relpath,
                                                left_source,
                                                left_file,
                                                left_props,
                                                file_baton,
                                                processor,
                                                iterpool));

              /* ...plus add... */
              file_baton = NULL;
              skip = FALSE;
              SVN_ERR(processor->file_opened(&file_baton, &skip, target_relpath,
                                             NULL /* left_source */,
                                             right_source,
                                             NULL /* copyfrom_source */,
                                             &dir_baton,
                                             processor,
                                             iterpool, iterpool));
              if (! skip)
                SVN_ERR(processor->file_added(target_relpath,
                                              NULL /* copyfrom_source */,
                                              right_source,
                                              NULL /* copyfrom_file */,
                                              right_file,
                                              NULL /* copyfrom_props */,
                                              right_props,
                                              file_baton,
                                              processor,
                                              iterpool));
              /* ... equals replace. */
            }
          else
            {
              void *file_baton = NULL;
              svn_boolean_t skip = FALSE;
              apr_array_header_t *propchanges;


              /* Deduce property diffs. */
              SVN_ERR(svn_prop_diffs(&propchanges, right_props, left_props,
                                     iterpool));

              SVN_ERR(processor->file_opened(&file_baton, &skip, target_relpath,
                                             left_source,
                                             right_source,
                                             NULL /* copyfrom_source */,
                                             NULL /* dir_baton */,
                                             processor,
                                             iterpool, iterpool));
              if (! skip)
                SVN_ERR(processor->file_changed(target_relpath,
                                              left_source,
                                              right_source,
                                              left_file,
                                              right_file,
                                              left_props,
                                              right_props,
                                              TRUE /* file changed */,
                                              propchanges,
                                              file_baton,
                                              processor,
                                              iterpool));
            }

          if (is_path_conflicted_by_merge(merge_b))
            {
              merge_source_t *remaining_range = NULL;

              if (real_source->loc2->rev != source->loc2->rev)
                remaining_range = subrange_source(source,
                                                  real_source->loc2->rev,
                                                  source->loc2->rev,
                                                  scratch_pool);
              *conflict_report = single_range_conflict_report_create(
                                   real_source, remaining_range, result_pool);

              /* Only record partial mergeinfo if only a partial merge was
                 performed before a conflict was encountered. */
              range.end = r->end;
              break;
            }

          /* Now delete the just merged range from the hash
             (This list is used from notify_merge_begin)

            Directory merges use remove_first_range_from_remaining_ranges() */
          svn_sort__array_delete(ranges_to_merge, 0, 1);
        }
      merge_b->notify_begin.last_abspath = NULL;
    } /* !merge_b->record_only */

  /* Record updated WC mergeinfo to account for our new merges, minus
     any unresolved conflicts and skips.  We use the original
     REMAINING_RANGES here because we want to record all the requested
     merge ranges, include the noop ones.  */
  if (RECORD_MERGEINFO(merge_b) && remaining_ranges->nelts)
    {
      const char *mergeinfo_path = svn_client__pathrev_fspath(primary_src,
                                                              scratch_pool);
      svn_rangelist_t *filtered_rangelist;

      /* Filter any ranges from TARGET_WCPATH's own history, there is no
         need to record this explicitly in mergeinfo, it is already part
         of TARGET_WCPATH's natural history (implicit mergeinfo). */
      SVN_ERR(filter_natural_history_from_mergeinfo(
        &filtered_rangelist,
        mergeinfo_path,
        merge_target->implicit_mergeinfo,
        &range,
        iterpool));

      /* Only record mergeinfo if there is something other than
         self-referential mergeinfo, but don't record mergeinfo if
         TARGET_WCPATH was skipped. */
      if (filtered_rangelist->nelts
          && (apr_hash_count(merge_b->skipped_abspaths) == 0))
        {
          apr_hash_t *merges = apr_hash_make(iterpool);

          /* If merge target has inherited mergeinfo set it before
             recording the first merge range. */
          if (inherited)
            SVN_ERR(svn_client__record_wc_mergeinfo(target_abspath,
                                                    target_mergeinfo,
                                                    FALSE, ctx,
                                                    iterpool));

          svn_hash_sets(merges, target_abspath, filtered_rangelist);

          if (!squelch_mergeinfo_notifications)
            {
              /* Notify that we are recording mergeinfo describing a merge. */
              svn_merge_range_t n_range;

              SVN_ERR(svn_mergeinfo__get_range_endpoints(
                        &n_range.end, &n_range.start, merges, iterpool));
              n_range.inheritable = TRUE;
              notify_mergeinfo_recording(target_abspath, &n_range,
                                         merge_b->ctx, iterpool);
            }

          SVN_ERR(update_wc_mergeinfo(result_catalog, target_abspath,
                                      mergeinfo_path, merges, is_rollback,
                                      ctx, iterpool));
        }
    }

  merge_b->notify_begin.nodes_with_mergeinfo = NULL;

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge() to handle the case where a merge editor
   drive adds explicit mergeinfo to a path which didn't have any explicit
   mergeinfo previously.

   MERGE_B is cascaded from the argument of the same
   name in do_directory_merge().  Should be called only after
   do_directory_merge() has called populate_remaining_ranges() and populated
   the remaining_ranges field of each child in
   CHILDREN_WITH_MERGEINFO (i.e. the remaining_ranges fields can be
   empty but never NULL).

   If MERGE_B->DRY_RUN is true do nothing, if it is false then
   for each path (if any) in MERGE_B->PATHS_WITH_NEW_MERGEINFO merge that
   path's inherited mergeinfo (if any) with its working explicit mergeinfo
   and set that as the path's new explicit mergeinfo.  Then add an
   svn_client__merge_path_t * element representing the path to
   CHILDREN_WITH_MERGEINFO if it isn't already present.  All fields
   in any elements added to CHILDREN_WITH_MERGEINFO are initialized
   to FALSE/NULL with the exception of 'path' and 'remaining_ranges'.  The
   latter is set to a rangelist equal to the remaining_ranges of the path's
   nearest path-wise ancestor in CHILDREN_WITH_MERGEINFO.

   Any elements added to CHILDREN_WITH_MERGEINFO are allocated
   in POOL. */
static svn_error_t *
process_children_with_new_mergeinfo(merge_cmd_baton_t *merge_b,
                                    apr_array_header_t *children_with_mergeinfo,
                                    apr_pool_t *pool)
{
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;

  if (!merge_b->paths_with_new_mergeinfo || merge_b->dry_run)
    return SVN_NO_ERROR;

  /* Iterate over each path with explicit mergeinfo added by the merge. */
  iterpool = svn_pool_create(pool);
  for (hi = apr_hash_first(pool, merge_b->paths_with_new_mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *abspath_with_new_mergeinfo = apr_hash_this_key(hi);
      svn_mergeinfo_t path_inherited_mergeinfo;
      svn_mergeinfo_t path_explicit_mergeinfo;
      svn_client__merge_path_t *new_child;

      svn_pool_clear(iterpool);

      /* Note: We could skip recording inherited mergeinfo here if this path
         was added (with preexisting mergeinfo) by the merge.  That's actually
         more correct, since the inherited mergeinfo likely describes
         non-existent or unrelated merge history, but it's not quite so simple
         as that, see http://subversion.tigris.org/issues/show_bug.cgi?id=4309
         */

      /* Get the path's new explicit mergeinfo... */
      SVN_ERR(svn_client__get_wc_mergeinfo(&path_explicit_mergeinfo, NULL,
                                           svn_mergeinfo_explicit,
                                           abspath_with_new_mergeinfo,
                                           NULL, NULL, FALSE,
                                           merge_b->ctx,
                                           iterpool, iterpool));
      /* ...there *should* always be explicit mergeinfo at this point
         but you can't be too careful. */
      if (path_explicit_mergeinfo)
        {
          /* Get the mergeinfo the path would have inherited before
             the merge. */
          SVN_ERR(svn_client__get_wc_or_repos_mergeinfo(
            &path_inherited_mergeinfo,
            NULL, NULL,
            FALSE,
            svn_mergeinfo_nearest_ancestor, /* We only want inherited MI */
            merge_b->ra_session2,
            abspath_with_new_mergeinfo,
            merge_b->ctx,
            iterpool));

          /* If the path inherited any mergeinfo then merge that with the
             explicit mergeinfo and record the result as the path's new
             explicit mergeinfo. */
          if (path_inherited_mergeinfo)
            {
              SVN_ERR(svn_mergeinfo_merge2(path_explicit_mergeinfo,
                                           path_inherited_mergeinfo,
                                           iterpool, iterpool));
              SVN_ERR(svn_client__record_wc_mergeinfo(
                                          abspath_with_new_mergeinfo,
                                          path_explicit_mergeinfo,
                                          FALSE, merge_b->ctx, iterpool));
            }

          /* If the path is not in CHILDREN_WITH_MERGEINFO then add it. */
          new_child =
            get_child_with_mergeinfo(children_with_mergeinfo,
                                     abspath_with_new_mergeinfo);
          if (!new_child)
            {
              const svn_client__merge_path_t *parent
                = find_nearest_ancestor(children_with_mergeinfo,
                                        FALSE, abspath_with_new_mergeinfo);
              new_child
                = svn_client__merge_path_create(abspath_with_new_mergeinfo,
                                                pool);

              /* If path_with_new_mergeinfo is the merge target itself
                 then it should already be in
                 CHILDREN_WITH_MERGEINFO per the criteria of
                 get_mergeinfo_paths() and we shouldn't be in this block.
                 If path_with_new_mergeinfo is a subtree then it must have
                 a parent in CHILDREN_WITH_MERGEINFO if only
                 the merge target itself...so if we don't find a parent
                 the caller has done something quite wrong. */
              SVN_ERR_ASSERT(parent);
              SVN_ERR_ASSERT(parent->remaining_ranges);

              /* Set the path's remaining_ranges equal to its parent's. */
              new_child->remaining_ranges = svn_rangelist_dup(
                 parent->remaining_ranges, pool);
              insert_child_to_merge(children_with_mergeinfo, new_child, pool);
            }
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* Return true if any path in SUBTREES is equal to, or is a subtree of,
   LOCAL_ABSPATH.  Return false otherwise.  The keys of SUBTREES are
   (const char *) absolute paths and its values are irrelevant.
   If SUBTREES is NULL return false. */
static svn_boolean_t
path_is_subtree(const char *local_abspath,
                apr_hash_t *subtrees,
                apr_pool_t *pool)
{
  if (subtrees)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(pool, subtrees);
           hi; hi = apr_hash_next(hi))
        {
          const char *path_touched_by_merge = apr_hash_this_key(hi);
          if (svn_dirent_is_ancestor(local_abspath, path_touched_by_merge))
            return TRUE;
        }
    }
  return FALSE;
}

/* Return true if any merged, skipped, added or tree-conflicted path
   recorded in MERGE_B is equal to, or is a subtree of LOCAL_ABSPATH.  Return
   false otherwise.

   ### Why not text- or prop-conflicted paths? Are such paths guaranteed
       to be recorded as 'merged' or 'skipped' or 'added', perhaps?
*/
static svn_boolean_t
subtree_touched_by_merge(const char *local_abspath,
                         merge_cmd_baton_t *merge_b,
                         apr_pool_t *pool)
{
  return (path_is_subtree(local_abspath, merge_b->merged_abspaths, pool)
          || path_is_subtree(local_abspath, merge_b->skipped_abspaths, pool)
          || path_is_subtree(local_abspath, merge_b->added_abspaths, pool)
          || path_is_subtree(local_abspath, merge_b->tree_conflicted_abspaths,
                             pool));
}

/* Helper for do_directory_merge() when performing mergeinfo unaware merges.

   Merge the SOURCE diff into TARGET_DIR_WCPATH.

   SOURCE, DEPTH, NOTIFY_B, and MERGE_B
   are all cascaded from do_directory_merge's arguments of the same names.

   CONFLICT_REPORT is as documented for do_directory_merge().

   NOTE: This is a very thin wrapper around drive_merge_report_editor() and
   exists only to populate CHILDREN_WITH_MERGEINFO with the single element
   expected during mergeinfo unaware merges.
*/
static svn_error_t *
do_mergeinfo_unaware_dir_merge(single_range_conflict_report_t **conflict_report,
                               const merge_source_t *source,
                               const char *target_dir_wcpath,
                               apr_array_header_t *children_with_mergeinfo,
                               const svn_diff_tree_processor_t *processor,
                               svn_depth_t depth,
                               merge_cmd_baton_t *merge_b,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  /* Initialize CHILDREN_WITH_MERGEINFO and populate it with
     one element describing the merge of SOURCE->rev1:rev2 to
     TARGET_DIR_WCPATH. */
  svn_client__merge_path_t *item
    = svn_client__merge_path_create(target_dir_wcpath, scratch_pool);

  *conflict_report = NULL;
  item->remaining_ranges = svn_rangelist__initialize(source->loc1->rev,
                                                     source->loc2->rev,
                                                     TRUE, scratch_pool);
  APR_ARRAY_PUSH(children_with_mergeinfo,
                 svn_client__merge_path_t *) = item;
  SVN_ERR(drive_merge_report_editor(target_dir_wcpath,
                                    source,
                                    NULL, processor, depth,
                                    merge_b, scratch_pool));
  if (is_path_conflicted_by_merge(merge_b))
    {
      *conflict_report = single_range_conflict_report_create(
                           source, NULL, result_pool);
    }
  return SVN_NO_ERROR;
}

/* A svn_log_entry_receiver_t baton for log_find_operative_subtree_revs(). */
typedef struct log_find_operative_subtree_baton_t
{
  /* Mapping of const char * absolute working copy paths to those
     path's const char * repos absolute paths. */
  apr_hash_t *operative_children;

  /* As per the arguments of the same name to
     get_operative_immediate_children(). */
  const char *merge_source_fspath;
  const char *merge_target_abspath;
  svn_depth_t depth;
  svn_wc_context_t *wc_ctx;

  /* A pool to allocate additions to the hashes in. */
  apr_pool_t *result_pool;
} log_find_operative_subtree_baton_t;

/* A svn_log_entry_receiver_t callback for
   get_inoperative_immediate_children(). */
static svn_error_t *
log_find_operative_subtree_revs(void *baton,
                                svn_log_entry_t *log_entry,
                                apr_pool_t *pool)
{
  log_find_operative_subtree_baton_t *log_baton = baton;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  /* It's possible that authz restrictions on the merge source prevent us
     from knowing about any of the changes for LOG_ENTRY->REVISION. */
  if (!log_entry->changed_paths2)
    return SVN_NO_ERROR;

  iterpool = svn_pool_create(pool);

  for (hi = apr_hash_first(pool, log_entry->changed_paths2);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *path = apr_hash_this_key(hi);
      svn_log_changed_path2_t *change = apr_hash_this_val(hi);

        {
          const char *child;
          const char *potential_child;
          const char *rel_path =
            svn_fspath__skip_ancestor(log_baton->merge_source_fspath, path);

          /* Some affected paths might be the root of the merge source or
             entirely outside our subtree of interest. In either case they
             are not operative *immediate* children. */
          if (rel_path == NULL
              || rel_path[0] == '\0')
            continue;

          svn_pool_clear(iterpool);

          child = svn_relpath_dirname(rel_path, iterpool);
          if (child[0] == '\0')
            {
              /* The svn_log_changed_path2_t.node_kind members in
                 LOG_ENTRY->CHANGED_PATHS2 may be set to
                 svn_node_unknown, see svn_log_changed_path2_t and
                 svn_fs_paths_changed2.  In that case we check the
                 type of the corresponding subtree in the merge
                 target. */
              svn_node_kind_t node_kind;

              if (change->node_kind == svn_node_unknown)
                {
                  const char *wc_child_abspath =
                    svn_dirent_join(log_baton->merge_target_abspath,
                                    rel_path, iterpool);

                  SVN_ERR(svn_wc_read_kind2(&node_kind, log_baton->wc_ctx,
                                            wc_child_abspath, FALSE, FALSE,
                                            iterpool));
                }
              else
                {
                  node_kind = change->node_kind;
                }

              /* We only care about immediate directory children if
                 DEPTH is svn_depth_files. */
              if (log_baton->depth == svn_depth_files
                  && node_kind != svn_node_dir)
                continue;

              /* If depth is svn_depth_immediates, then we only care
                 about changes to proper subtrees of PATH.  If the change
                 is to PATH itself then PATH is within the operational
                 depth of the merge. */
              if (log_baton->depth == svn_depth_immediates)
                continue;

              child = rel_path;
            }

          potential_child = svn_dirent_join(log_baton->merge_target_abspath,
                                            child, iterpool);

          if (change->action == 'A'
              || !svn_hash_gets(log_baton->operative_children,
                                potential_child))
            {
              svn_hash_sets(log_baton->operative_children,
                            apr_pstrdup(log_baton->result_pool,
                                        potential_child),
                            apr_pstrdup(log_baton->result_pool, path));
            }
        }
    }
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Find immediate subtrees of MERGE_TARGET_ABSPATH which would have
   additional differences applied if record_mergeinfo_for_dir_merge() were
   recording mergeinfo describing a merge at svn_depth_infinity, rather
   than at DEPTH (which is assumed to be shallow; if
   DEPTH == svn_depth_infinity then this function does nothing beyond
   setting *OPERATIVE_CHILDREN to an empty hash).

   MERGE_SOURCE_FSPATH is the absolute repository path of the merge
   source.  OLDEST_REV and YOUNGEST_REV are the revisions merged from
   MERGE_SOURCE_FSPATH to MERGE_TARGET_ABSPATH.

   RA_SESSION points to MERGE_SOURCE_FSPATH.

   Set *OPERATIVE_CHILDREN to a hash (mapping const char * absolute
   working copy paths to those path's const char * repos absolute paths)
   containing all the immediate subtrees of MERGE_TARGET_ABSPATH which would
   have a different diff applied if MERGE_SOURCE_FSPATH
   -r(OLDEST_REV - 1):YOUNGEST_REV were merged to MERGE_TARGET_ABSPATH at
   svn_depth_infinity rather than DEPTH.

   RESULT_POOL is used to allocate the contents of *OPERATIVE_CHILDREN.
   SCRATCH_POOL is used for temporary allocations. */
static svn_error_t *
get_operative_immediate_children(apr_hash_t **operative_children,
                                 const char *merge_source_fspath,
                                 svn_revnum_t oldest_rev,
                                 svn_revnum_t youngest_rev,
                                 const char *merge_target_abspath,
                                 svn_depth_t depth,
                                 svn_wc_context_t *wc_ctx,
                                 svn_ra_session_t *ra_session,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  log_find_operative_subtree_baton_t log_baton;

  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(oldest_rev));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  SVN_ERR_ASSERT(oldest_rev <= youngest_rev);

  *operative_children = apr_hash_make(result_pool);

  if (depth == svn_depth_infinity)
    return SVN_NO_ERROR;

  /* Now remove any paths from *OPERATIVE_CHILDREN that are inoperative when
     merging MERGE_SOURCE_REPOS_PATH -r(OLDEST_REV - 1):YOUNGEST_REV to
     MERGE_TARGET_ABSPATH at --depth infinity. */
  log_baton.operative_children = *operative_children;
  log_baton.merge_source_fspath = merge_source_fspath;
  log_baton.merge_target_abspath = merge_target_abspath;
  log_baton.depth = depth;
  log_baton.wc_ctx = wc_ctx;
  log_baton.result_pool = result_pool;

  SVN_ERR(get_log(ra_session, "", youngest_rev, oldest_rev,
                  TRUE, /* discover_changed_paths */
                  log_find_operative_subtree_revs,
                  &log_baton, scratch_pool));

  return SVN_NO_ERROR;
}

/* Helper for record_mergeinfo_for_dir_merge(): Identify which elements of
   CHILDREN_WITH_MERGEINFO need new mergeinfo set to accurately
   describe a merge, what inheritance type such new mergeinfo should have,
   and what subtrees can be ignored altogether.

   For each svn_client__merge_path_t CHILD in CHILDREN_WITH_MERGEINFO,
   set CHILD->RECORD_MERGEINFO and CHILD->RECORD_NONINHERITABLE to true
   if the subtree needs mergeinfo to describe the merge and if that
   mergeinfo should be non-inheritable respectively.

   If OPERATIVE_MERGE is true, then the merge being described is operative
   as per subtree_touched_by_merge().  OPERATIVE_MERGE is false otherwise.

   MERGED_RANGE, MERGEINFO_FSPATH, DEPTH, NOTIFY_B, and MERGE_B are all
   cascaded from record_mergeinfo_for_dir_merge's arguments of the same
   names.

   SCRATCH_POOL is used for temporary allocations.
*/
static svn_error_t *
flag_subtrees_needing_mergeinfo(svn_boolean_t operative_merge,
                                const svn_merge_range_t *merged_range,
                                apr_array_header_t *children_with_mergeinfo,
                                const char *mergeinfo_fspath,
                                svn_depth_t depth,
                                merge_cmd_baton_t *merge_b,
                                apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;
  apr_hash_t *operative_immediate_children = NULL;

  assert(! merge_b->dry_run);

  if (!merge_b->record_only
      && merged_range->start <= merged_range->end
      && (depth < svn_depth_infinity))
    SVN_ERR(get_operative_immediate_children(
      &operative_immediate_children,
      mergeinfo_fspath, merged_range->start + 1, merged_range->end,
      merge_b->target->abspath, depth, merge_b->ctx->wc_ctx,
      merge_b->ra_session1, scratch_pool, iterpool));

  /* Issue #4056: Walk NOTIFY_B->CHILDREN_WITH_MERGEINFO reverse depth-first
     order.  This way each child knows if it has operative missing/switched
     children which necessitates non-inheritable mergeinfo. */
  for (i = children_with_mergeinfo->nelts - 1; i >= 0; i--)
    {
      svn_client__merge_path_t *child =
                     APR_ARRAY_IDX(children_with_mergeinfo, i,
                                   svn_client__merge_path_t *);

      /* Can't record mergeinfo on something that isn't here. */
      if (child->absent)
        continue;

      /* Verify that remove_children_with_deleted_mergeinfo() did its job */
      assert((i == 0)
             ||! merge_b->paths_with_deleted_mergeinfo
             || !svn_hash_gets(merge_b->paths_with_deleted_mergeinfo,
                               child->abspath));

      /* Don't record mergeinfo on skipped paths. */
      if (svn_hash_gets(merge_b->skipped_abspaths, child->abspath))
        continue;

      /* ### ptb: Yes, we could combine the following into a single
         ### conditional, but clarity would suffer (even more than
         ### it does now). */
      if (i == 0)
        {
          /* Always record mergeinfo on the merge target. */
          child->record_mergeinfo = TRUE;
        }
      else if (merge_b->record_only && !merge_b->reintegrate_merge)
        {
          /* Always record mergeinfo for --record-only merges. */
          child->record_mergeinfo = TRUE;
        }
      else if (child->immediate_child_dir
               && !child->pre_merge_mergeinfo
               && operative_immediate_children
               && svn_hash_gets(operative_immediate_children, child->abspath))
        {
          /* We must record mergeinfo on those issue #3642 children
             that are operative at a greater depth. */
          child->record_mergeinfo = TRUE;
        }

      if (operative_merge
          && subtree_touched_by_merge(child->abspath, merge_b, iterpool))
        {
          svn_pool_clear(iterpool);

          /* This subtree was affected by the merge. */
          child->record_mergeinfo = TRUE;

          /* Were any CHILD's missing children skipped by the merge?
             If not, then CHILD's missing children don't need to be
             considered when recording mergeinfo describing the merge. */
          if (! merge_b->reintegrate_merge
              && child->missing_child
              && !path_is_subtree(child->abspath,
                                  merge_b->skipped_abspaths,
                                  iterpool))
            {
              child->missing_child = FALSE;
            }

          /* If CHILD has an immediate switched child or children and
             none of these were touched by the merge, then we don't need
             need to do any special handling of those switched subtrees
             (e.g. record non-inheritable mergeinfo) when recording
             mergeinfo describing the merge. */
          if (child->switched_child)
            {
              int j;
              svn_boolean_t operative_switched_child = FALSE;

              for (j = i + 1;
                   j < children_with_mergeinfo->nelts;
                   j++)
                {
                  svn_client__merge_path_t *potential_child =
                    APR_ARRAY_IDX(children_with_mergeinfo, j,
                                  svn_client__merge_path_t *);
                  if (!svn_dirent_is_ancestor(child->abspath,
                                              potential_child->abspath))
                    break;

                  /* POTENTIAL_CHILD is a subtree of CHILD, but is it
                     an immediate child? */
                  if (strcmp(child->abspath,
                             svn_dirent_dirname(potential_child->abspath,
                                                iterpool)))
                    continue;

                  if (potential_child->switched
                      && potential_child->record_mergeinfo)
                    {
                      operative_switched_child = TRUE;
                      break;
                    }
                }

              /* Can we treat CHILD as if it has no switched children? */
              if (! operative_switched_child)
                child->switched_child = FALSE;
            }
        }

      if (child->record_mergeinfo)
        {
          /* We need to record mergeinfo, but should that mergeinfo be
             non-inheritable? */
          svn_node_kind_t path_kind;
          SVN_ERR(svn_wc_read_kind2(&path_kind, merge_b->ctx->wc_ctx,
                                    child->abspath, FALSE, FALSE, iterpool));

          /* Only directories can have non-inheritable mergeinfo. */
          if (path_kind == svn_node_dir)
            {
              /* There are two general cases where non-inheritable mergeinfo
                 is required:

                 1) There merge target has missing subtrees (due to authz
                    restrictions, switched subtrees, or a shallow working
                    copy).

                 2) The operational depth of the merge itself is shallow. */

              /* We've already determined the first case. */
              child->record_noninheritable =
                child->missing_child || child->switched_child;

              /* The second case requires a bit more work. */
              if (i == 0)
                {
                  /* If CHILD is the root of the merge target and the
                     operational depth is empty or files, then the mere
                     existence of operative immediate children means we
                     must record non-inheritable mergeinfo.

                     ### What about svn_depth_immediates?  In that case
                     ### the merge target needs only normal inheritable
                     ### mergeinfo and the target's immediate children will
                     ### get non-inheritable mergeinfo, assuming they
                     ### need even that. */
                  if (depth < svn_depth_immediates
                      && operative_immediate_children
                      && apr_hash_count(operative_immediate_children))
                    child->record_noninheritable = TRUE;
                }
              else if (depth == svn_depth_immediates)
                {
                  /* An immediate directory child of the merge target, which
                      was affected by a --depth=immediates merge, needs
                      non-inheritable mergeinfo. */
                  if (svn_hash_gets(operative_immediate_children,
                                    child->abspath))
                    child->record_noninheritable = TRUE;
                }
            }
        }
      else /* child->record_mergeinfo */
        {
          /* If CHILD is in NOTIFY_B->CHILDREN_WITH_MERGEINFO simply
             because it had no explicit mergeinfo of its own at the
             start of the merge but is the child of of some path with
             non-inheritable mergeinfo, then the explicit mergeinfo it
             has *now* was set by get_mergeinfo_paths() -- see criteria
             3 in that function's doc string.  So since CHILD->ABSPATH
             was not touched by the merge we can remove the
             mergeinfo. */
          if (child->child_of_noninheritable)
            SVN_ERR(svn_client__record_wc_mergeinfo(child->abspath,
                                                    NULL, FALSE,
                                                    merge_b->ctx,
                                                    iterpool));
        }
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge().

   If RESULT_CATALOG is NULL then record mergeinfo describing a merge of
   MERGED_RANGE->START:MERGED_RANGE->END from the repository relative path
   MERGEINFO_FSPATH to the merge target (and possibly its subtrees) described
   by NOTIFY_B->CHILDREN_WITH_MERGEINFO -- see the global comment
   'THE CHILDREN_WITH_MERGEINFO ARRAY'.  Obviously this should only
   be called if recording mergeinfo -- see doc string for RECORD_MERGEINFO().

   If RESULT_CATALOG is not NULL, then don't record the new mergeinfo on the
   WC, but instead record it in RESULT_CATALOG, where the keys are absolute
   working copy paths and the values are the new mergeinfos for each.
   Allocate additions to RESULT_CATALOG in pool which RESULT_CATALOG was
   created in.

   DEPTH, NOTIFY_B, MERGE_B, and SQUELCH_MERGEINFO_NOTIFICATIONS are all
   cascaded from do_directory_merge's arguments of the same names.

   SCRATCH_POOL is used for temporary allocations.
*/
static svn_error_t *
record_mergeinfo_for_dir_merge(svn_mergeinfo_catalog_t result_catalog,
                               const svn_merge_range_t *merged_range,
                               const char *mergeinfo_fspath,
                               apr_array_header_t *children_with_mergeinfo,
                               svn_depth_t depth,
                               svn_boolean_t squelch_mergeinfo_notifications,
                               merge_cmd_baton_t *merge_b,
                               apr_pool_t *scratch_pool)
{
  int i;
  svn_boolean_t is_rollback = (merged_range->start > merged_range->end);
  svn_boolean_t operative_merge;

  /* Update the WC mergeinfo here to account for our new
     merges, minus any unresolved conflicts and skips. */

  /* We need a scratch pool for iterations below. */
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  svn_merge_range_t range = *merged_range;

  assert(! merge_b->dry_run);

  /* Regardless of what subtrees in MERGE_B->target->abspath might be missing
     could this merge have been operative? */
  operative_merge = subtree_touched_by_merge(merge_b->target->abspath,
                                             merge_b, iterpool);

  /* If this couldn't be an operative merge then don't bother with
     the added complexity (and user confusion) of non-inheritable ranges.
     There is no harm in subtrees inheriting inoperative mergeinfo. */
  if (!operative_merge)
    range.inheritable = TRUE;

  /* Remove absent children at or under MERGE_B->target->abspath from
     NOTIFY_B->CHILDREN_WITH_MERGEINFO
     before we calculate the merges performed. */
  remove_absent_children(merge_b->target->abspath,
                         children_with_mergeinfo);

  /* Determine which subtrees of interest need mergeinfo recorded... */
  SVN_ERR(flag_subtrees_needing_mergeinfo(operative_merge, &range,
                                          children_with_mergeinfo,
                                          mergeinfo_fspath, depth,
                                          merge_b, iterpool));

  /* ...and then record it. */
  for (i = 0; i < children_with_mergeinfo->nelts; i++)
    {
      const char *child_repos_path;
      const char *child_merge_src_fspath;
      svn_rangelist_t *child_merge_rangelist;
      apr_hash_t *child_merges;
      svn_client__merge_path_t *child =
                     APR_ARRAY_IDX(children_with_mergeinfo, i,
                                   svn_client__merge_path_t *);
      SVN_ERR_ASSERT(child);

      svn_pool_clear(iterpool);

      if (child->record_mergeinfo)
        {
          child_repos_path = svn_dirent_skip_ancestor(merge_b->target->abspath,
                                                      child->abspath);
          SVN_ERR_ASSERT(child_repos_path != NULL);
          child_merge_src_fspath = svn_fspath__join(mergeinfo_fspath,
                                                    child_repos_path,
                                                    iterpool);
          /* Filter any ranges from each child's natural history before
             setting mergeinfo describing the merge. */
          SVN_ERR(filter_natural_history_from_mergeinfo(
            &child_merge_rangelist, child_merge_src_fspath,
            child->implicit_mergeinfo, &range, iterpool));

          if (child_merge_rangelist->nelts == 0)
            continue;

          if (!squelch_mergeinfo_notifications)
            {
              /* If the merge source has a gap, then don't mention
                 those gap revisions in the notification. */
              remove_source_gap(&range, merge_b->implicit_src_gap);
              notify_mergeinfo_recording(child->abspath, &range,
                                         merge_b->ctx, iterpool);
            }

          /* If we are here we know we will be recording some mergeinfo, but
             before we do, set override mergeinfo on skipped paths so they
             don't incorrectly inherit the mergeinfo we are about to set. */
          if (i == 0)
            SVN_ERR(record_skips_in_mergeinfo(mergeinfo_fspath,
                                              child_merge_rangelist,
                                              is_rollback, merge_b, iterpool));

          /* We may need to record non-inheritable mergeinfo that applies
             only to CHILD->ABSPATH. */
          if (child->record_noninheritable)
            svn_rangelist__set_inheritance(child_merge_rangelist, FALSE);

          /* If CHILD has inherited mergeinfo set it before
             recording the first merge range. */
          if (child->inherited_mergeinfo)
            SVN_ERR(svn_client__record_wc_mergeinfo(
              child->abspath,
              child->pre_merge_mergeinfo,
              FALSE, merge_b->ctx,
              iterpool));
          if (merge_b->implicit_src_gap)
            {
              /* If this is a reverse merge reorder CHILD->REMAINING_RANGES
                 so it will work with the svn_rangelist_remove API. */
              if (is_rollback)
                SVN_ERR(svn_rangelist_reverse(child_merge_rangelist,
                                              iterpool));

              SVN_ERR(svn_rangelist_remove(&child_merge_rangelist,
                                           merge_b->implicit_src_gap,
                                           child_merge_rangelist, FALSE,
                                           iterpool));
              if (is_rollback)
                SVN_ERR(svn_rangelist_reverse(child_merge_rangelist,
                                              iterpool));
            }

          child_merges = apr_hash_make(iterpool);

          /* The short story:

             If we are describing a forward merge, then the naive mergeinfo
             defined by MERGE_SOURCE_PATH:MERGED_RANGE->START:
             MERGE_SOURCE_PATH:MERGED_RANGE->END may contain non-existent
             path-revs or may describe other lines of history.  We must
             remove these invalid portion(s) before recording mergeinfo
             describing the merge.

             The long story:

             If CHILD is the merge target we know that
             MERGE_SOURCE_PATH:MERGED_RANGE->END exists.  Further, if there
             were no copies in MERGE_SOURCE_PATH's history going back to
             RANGE->START then we know that
             MERGE_SOURCE_PATH:MERGED_RANGE->START exists too and the two
             describe an unbroken line of history, and thus
             MERGE_SOURCE_PATH:MERGED_RANGE->START:
             MERGE_SOURCE_PATH:MERGED_RANGE->END is a valid description of
             the merge -- see normalize_merge_sources() and the global comment
             'MERGEINFO MERGE SOURCE NORMALIZATION'.

             However, if there *was* a copy, then
             MERGE_SOURCE_PATH:MERGED_RANGE->START doesn't exist or is
             unrelated to MERGE_SOURCE_PATH:MERGED_RANGE->END.  Also, we
             don't know if (MERGE_SOURCE_PATH:MERGED_RANGE->START)+1 through
             (MERGE_SOURCE_PATH:MERGED_RANGE->END)-1 actually exist.

             If CHILD is a subtree of the merge target, then nothing is
             guaranteed beyond the fact that MERGE_SOURCE_PATH exists at
             MERGED_RANGE->END. */
          if ((!merge_b->record_only || merge_b->reintegrate_merge)
              && (!is_rollback))
            {
              svn_error_t *err;
              svn_mergeinfo_t subtree_history_as_mergeinfo;
              svn_rangelist_t *child_merge_src_rangelist;
              svn_client__pathrev_t *subtree_mergeinfo_pathrev
                = svn_client__pathrev_create_with_relpath(
                    merge_b->target->loc.repos_root_url,
                    merge_b->target->loc.repos_uuid,
                    merged_range->end, child_merge_src_fspath + 1,
                    iterpool);

              /* Confirm that the naive mergeinfo we want to set on
                 CHILD->ABSPATH both exists and is part of
                 (MERGE_SOURCE_PATH+CHILD_REPOS_PATH)@MERGED_RANGE->END's
                 history. */
              /* We know MERGED_RANGE->END is younger than MERGE_RANGE->START
                 because we only do this for forward merges. */
              err = svn_client__get_history_as_mergeinfo(
                &subtree_history_as_mergeinfo, NULL,
                subtree_mergeinfo_pathrev,
                merged_range->end, merged_range->start,
                merge_b->ra_session2, merge_b->ctx, iterpool);

              /* If CHILD is a subtree it may have been deleted prior to
                 MERGED_RANGE->END so the above call to get its history
                 will fail. */
              if (err)
                {
                  if (err->apr_err != SVN_ERR_FS_NOT_FOUND)
                      return svn_error_trace(err);
                  svn_error_clear(err);
                }
              else
                {
                  child_merge_src_rangelist = svn_hash_gets(
                                                subtree_history_as_mergeinfo,
                                                child_merge_src_fspath);
                  SVN_ERR(svn_rangelist_intersect(&child_merge_rangelist,
                                                  child_merge_rangelist,
                                                  child_merge_src_rangelist,
                                                  FALSE, iterpool));
                  if (child->record_noninheritable)
                    svn_rangelist__set_inheritance(child_merge_rangelist,
                                                   FALSE);
                }
            }

          svn_hash_sets(child_merges, child->abspath, child_merge_rangelist);
          SVN_ERR(update_wc_mergeinfo(result_catalog,
                                      child->abspath,
                                      child_merge_src_fspath,
                                      child_merges, is_rollback,
                                      merge_b->ctx, iterpool));

          /* Once is enough: We don't need to record mergeinfo describing
             the merge a second.  If CHILD->ABSPATH is in
             MERGE_B->ADDED_ABSPATHS, we'll do just that, so remove the
             former from the latter. */
          svn_hash_sets(merge_b->added_abspaths, child->abspath, NULL);
        }

      /* Elide explicit subtree mergeinfo whether or not we updated it. */
      if (i > 0)
        {
          svn_boolean_t in_switched_subtree = FALSE;

          if (child->switched)
            in_switched_subtree = TRUE;
          else if (i > 1)
            {
              /* Check if CHILD is part of a switched subtree */
              svn_client__merge_path_t *parent;
              int j = i - 1;
              for (; j > 0; j--)
                {
                  parent = APR_ARRAY_IDX(children_with_mergeinfo,
                                         j, svn_client__merge_path_t *);
                  if (parent
                      && parent->switched
                      && svn_dirent_is_ancestor(parent->abspath,
                                                child->abspath))
                    {
                      in_switched_subtree = TRUE;
                      break;
                    }
                }
            }

          /* Allow mergeinfo on switched subtrees to elide to the
             repository. Otherwise limit elision to the merge target
             for now.  do_merge() will eventually try to
             elide that when the merge is complete. */
          SVN_ERR(svn_client__elide_mergeinfo(
            child->abspath,
            in_switched_subtree ? NULL : merge_b->target->abspath,
            merge_b->ctx, iterpool));
        }
    } /* (i = 0; i < notify_b->children_with_mergeinfo->nelts; i++) */

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge().

   Record mergeinfo describing a merge of
   MERGED_RANGE->START:MERGED_RANGE->END from the repository relative path
   MERGEINFO_FSPATH to each path in ADDED_ABSPATHS which has explicit
   mergeinfo or is the immediate child of a parent with explicit
   non-inheritable mergeinfo.

   DEPTH, MERGE_B, and SQUELCH_MERGEINFO_NOTIFICATIONS, are
   cascaded from do_directory_merge's arguments of the same names.

   Note: This is intended to support forward merges only, i.e.
   MERGED_RANGE->START must be older than MERGED_RANGE->END.
*/
static svn_error_t *
record_mergeinfo_for_added_subtrees(
  svn_merge_range_t *merged_range,
  const char *mergeinfo_fspath,
  svn_depth_t depth,
  svn_boolean_t squelch_mergeinfo_notifications,
  apr_hash_t *added_abspaths,
  merge_cmd_baton_t *merge_b,
  apr_pool_t *pool)
{
  apr_pool_t *iterpool;
  apr_hash_index_t *hi;

  /* If no paths were added by the merge then we have nothing to do. */
  if (!added_abspaths)
    return SVN_NO_ERROR;

  SVN_ERR_ASSERT(merged_range->start < merged_range->end);

  iterpool = svn_pool_create(pool);
  for (hi = apr_hash_first(pool, added_abspaths); hi; hi = apr_hash_next(hi))
    {
      const char *added_abspath = apr_hash_this_key(hi);
      const char *dir_abspath;
      svn_mergeinfo_t parent_mergeinfo;
      svn_mergeinfo_t added_path_mergeinfo;

      svn_pool_clear(iterpool);
      dir_abspath = svn_dirent_dirname(added_abspath, iterpool);

      /* Grab the added path's explicit mergeinfo. */
      SVN_ERR(svn_client__get_wc_mergeinfo(&added_path_mergeinfo, NULL,
                                           svn_mergeinfo_explicit,
                                           added_abspath, NULL, NULL, FALSE,
                                           merge_b->ctx, iterpool, iterpool));

      /* If the added path doesn't have explicit mergeinfo, does its immediate
         parent have non-inheritable mergeinfo? */
      if (!added_path_mergeinfo)
        SVN_ERR(svn_client__get_wc_mergeinfo(&parent_mergeinfo, NULL,
                                             svn_mergeinfo_explicit,
                                             dir_abspath, NULL, NULL, FALSE,
                                             merge_b->ctx,
                                             iterpool, iterpool));

      if (added_path_mergeinfo
          || svn_mergeinfo__is_noninheritable(parent_mergeinfo, iterpool))
        {
          svn_node_kind_t added_path_kind;
          svn_mergeinfo_t merge_mergeinfo;
          svn_mergeinfo_t adds_history_as_mergeinfo;
          svn_rangelist_t *rangelist;
          const char *rel_added_path;
          const char *added_path_mergeinfo_fspath;
          svn_client__pathrev_t *added_path_pathrev;

          SVN_ERR(svn_wc_read_kind2(&added_path_kind, merge_b->ctx->wc_ctx,
                                    added_abspath, FALSE, FALSE, iterpool));

          /* Calculate the naive mergeinfo describing the merge. */
          merge_mergeinfo = apr_hash_make(iterpool);
          rangelist = svn_rangelist__initialize(
                        merged_range->start, merged_range->end,
                        ((added_path_kind == svn_node_file)
                         || (!(depth == svn_depth_infinity
                               || depth == svn_depth_immediates))),
                        iterpool);

          /* Create the new mergeinfo path for added_path's mergeinfo.
             (added_abspath had better be a child of MERGE_B->target->abspath
             or something is *really* wrong.) */
          rel_added_path = svn_dirent_is_child(merge_b->target->abspath,
                                               added_abspath, iterpool);
          SVN_ERR_ASSERT(rel_added_path);
          added_path_mergeinfo_fspath = svn_fspath__join(mergeinfo_fspath,
                                                         rel_added_path,
                                                         iterpool);
          svn_hash_sets(merge_mergeinfo, added_path_mergeinfo_fspath,
                        rangelist);

          /* Don't add new mergeinfo to describe the merge if that mergeinfo
             contains non-existent merge sources.

             We know that MERGEINFO_PATH/rel_added_path's history does not
             span MERGED_RANGE->START:MERGED_RANGE->END but rather that it
             was added at some revions greater than MERGED_RANGE->START
             (assuming this is a forward merge).  It may have been added,
             deleted, and re-added many times.  The point is that we cannot
             blindly apply the naive mergeinfo calculated above because it
             will describe non-existent merge sources. To avoid this we get
             take the intersection of the naive mergeinfo with
             MERGEINFO_PATH/rel_added_path's history. */
          added_path_pathrev = svn_client__pathrev_create_with_relpath(
                                 merge_b->target->loc.repos_root_url,
                                 merge_b->target->loc.repos_uuid,
                                 MAX(merged_range->start, merged_range->end),
                                 added_path_mergeinfo_fspath + 1, iterpool);
          SVN_ERR(svn_client__get_history_as_mergeinfo(
            &adds_history_as_mergeinfo, NULL,
            added_path_pathrev,
            MAX(merged_range->start, merged_range->end),
            MIN(merged_range->start, merged_range->end),
            merge_b->ra_session2, merge_b->ctx, iterpool));

          SVN_ERR(svn_mergeinfo_intersect2(&merge_mergeinfo,
                                           merge_mergeinfo,
                                           adds_history_as_mergeinfo,
                                           FALSE, iterpool, iterpool));

          /* Combine the explicit mergeinfo on the added path (if any)
             with the mergeinfo describing this merge. */
          if (added_path_mergeinfo)
            SVN_ERR(svn_mergeinfo_merge2(merge_mergeinfo,
                                         added_path_mergeinfo,
                                         iterpool, iterpool));
          SVN_ERR(svn_client__record_wc_mergeinfo(
            added_abspath, merge_mergeinfo,
            !squelch_mergeinfo_notifications, merge_b->ctx, iterpool));
        }
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}
/* Baton structure for log_noop_revs. */
typedef struct log_noop_baton_t
{
  /* See the comment 'THE CHILDREN_WITH_MERGEINFO ARRAY' at the start
     of this file.*/
  apr_array_header_t *children_with_mergeinfo;

  /* Absolute repository path of younger of the two merge sources
     being diffed. */
  const char *source_fspath;

  /* The merge target. */
  const merge_target_t *target;

  /* Initially empty rangelists allocated in POOL. The rangelists are
   * populated across multiple invocations of log_noop_revs(). */
  svn_rangelist_t *operative_ranges;
  svn_rangelist_t *merged_ranges;

  /* Pool to store the rangelists. */
  apr_pool_t *pool;
} log_noop_baton_t;

/* Helper for log_noop_revs: Merge a svn_merge_range_t representation of
   REVISION into RANGELIST. New elements added to rangelist are allocated
   in RESULT_POOL.

   This is *not* a general purpose rangelist merge but a special replacement
   for svn_rangelist_merge when REVISION is guaranteed to be younger than any
   element in RANGELIST.  svn_rangelist_merge is O(n) worst-case (i.e. when
   all the ranges in output rangelist are older than the incoming changes).
   This turns the special case of a single incoming younger range into O(1).
   */
static svn_error_t *
rangelist_merge_revision(svn_rangelist_t *rangelist,
                         svn_revnum_t revision,
                         apr_pool_t *result_pool)
{
  svn_merge_range_t *new_range;
  if (rangelist->nelts)
    {
      svn_merge_range_t *range = APR_ARRAY_IDX(rangelist, rangelist->nelts - 1,
                                               svn_merge_range_t *);
      if (range->end == revision - 1)
        {
          /* REVISION is adjacent to the youngest range in RANGELIST
             so we can simply expand that range to encompass REVISION. */
          range->end = revision;
          return SVN_NO_ERROR;
        }
    }
  new_range = apr_palloc(result_pool, sizeof(*new_range));
  new_range->start = revision - 1;
  new_range->end = revision;
  new_range->inheritable = TRUE;

  APR_ARRAY_PUSH(rangelist, svn_merge_range_t *) = new_range;

  return SVN_NO_ERROR;
}

/* Implements the svn_log_entry_receiver_t interface.

   BATON is an log_noop_baton_t *.

   Add LOG_ENTRY->REVISION to BATON->OPERATIVE_RANGES.

   If LOG_ENTRY->REVISION has already been fully merged to
   BATON->target->abspath per the mergeinfo in BATON->CHILDREN_WITH_MERGEINFO,
   then add LOG_ENTRY->REVISION to BATON->MERGED_RANGES.

   Use SCRATCH_POOL for temporary allocations.  Allocate additions to
   BATON->MERGED_RANGES and BATON->OPERATIVE_RANGES in BATON->POOL.

   Note: This callback must be invoked from oldest LOG_ENTRY->REVISION
   to youngest LOG_ENTRY->REVISION -- see rangelist_merge_revision().
*/
static svn_error_t *
log_noop_revs(void *baton,
              svn_log_entry_t *log_entry,
              apr_pool_t *scratch_pool)
{
  log_noop_baton_t *log_gap_baton = baton;
  apr_hash_index_t *hi;
  svn_revnum_t revision;
  svn_boolean_t log_entry_rev_required = FALSE;

  revision = log_entry->revision;

  /* It's possible that authz restrictions on the merge source prevent us
     from knowing about any of the changes for LOG_ENTRY->REVISION. */
  if (!log_entry->changed_paths2)
    return SVN_NO_ERROR;

  /* Unconditionally add LOG_ENTRY->REVISION to BATON->OPERATIVE_MERGES. */
  SVN_ERR(rangelist_merge_revision(log_gap_baton->operative_ranges,
                                   revision,
                                   log_gap_baton->pool));

  /* Examine each path affected by LOG_ENTRY->REVISION.  If the explicit or
     inherited mergeinfo for *all* of the corresponding paths under
     BATON->target->abspath reflects that LOG_ENTRY->REVISION has been
     merged, then add LOG_ENTRY->REVISION to BATON->MERGED_RANGES. */
  for (hi = apr_hash_first(scratch_pool, log_entry->changed_paths2);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *fspath = apr_hash_this_key(hi);
      const char *rel_path;
      const char *cwmi_abspath;
      svn_rangelist_t *paths_explicit_rangelist = NULL;
      svn_boolean_t mergeinfo_inherited = FALSE;

      /* Adjust REL_PATH so it is relative to the merge source then use it to
         calculate what path in the merge target would be affected by this
         revision. */
      rel_path = svn_fspath__skip_ancestor(log_gap_baton->source_fspath,
                                           fspath);
      /* Is PATH even within the merge target?  If it isn't we
         can disregard it altogether. */
      if (rel_path == NULL)
        continue;
      cwmi_abspath = svn_dirent_join(log_gap_baton->target->abspath,
                                     rel_path, scratch_pool);

      /* Find any explicit or inherited mergeinfo for PATH. */
      while (!log_entry_rev_required)
        {
          svn_client__merge_path_t *child = get_child_with_mergeinfo(
            log_gap_baton->children_with_mergeinfo, cwmi_abspath);

          if (child && child->pre_merge_mergeinfo)
            {
              /* Found some explicit mergeinfo, grab any ranges
                 for PATH. */
              paths_explicit_rangelist =
                            svn_hash_gets(child->pre_merge_mergeinfo, fspath);
              break;
            }

          if (cwmi_abspath[0] == '\0'
              || svn_dirent_is_root(cwmi_abspath, strlen(cwmi_abspath))
              || strcmp(log_gap_baton->target->abspath, cwmi_abspath) == 0)
            {
              /* Can't crawl any higher. */
              break;
            }

          /* Didn't find anything so crawl up to the parent. */
          cwmi_abspath = svn_dirent_dirname(cwmi_abspath, scratch_pool);
          fspath = svn_fspath__dirname(fspath, scratch_pool);

          /* At this point *if* we find mergeinfo it will be inherited. */
          mergeinfo_inherited = TRUE;
        }

      if (paths_explicit_rangelist)
        {
          svn_rangelist_t *intersecting_range;
          svn_rangelist_t *rangelist;

          rangelist = svn_rangelist__initialize(revision - 1, revision, TRUE,
                                                scratch_pool);

          /* If PATH inherited mergeinfo we must consider inheritance in the
             event the inherited mergeinfo is actually non-inheritable. */
          SVN_ERR(svn_rangelist_intersect(&intersecting_range,
                                          paths_explicit_rangelist,
                                          rangelist,
                                          mergeinfo_inherited, scratch_pool));

          if (intersecting_range->nelts == 0)
            log_entry_rev_required = TRUE;
        }
      else
        {
          log_entry_rev_required = TRUE;
        }
    }

  if (!log_entry_rev_required)
    SVN_ERR(rangelist_merge_revision(log_gap_baton->merged_ranges,
                                     revision,
                                     log_gap_baton->pool));

  return SVN_NO_ERROR;
}

/* Helper for do_directory_merge().

   SOURCE is cascaded from the argument of the same name in
   do_directory_merge().  TARGET is the merge target.  RA_SESSION is the
   session for SOURCE->loc2.

   Find all the ranges required by subtrees in
   CHILDREN_WITH_MERGEINFO that are *not* required by
   TARGET->abspath (i.e. CHILDREN_WITH_MERGEINFO[0]).  If such
   ranges exist, then find any subset of ranges which, if merged, would be
   inoperative.  Finally, if any inoperative ranges are found then remove
   these ranges from all of the subtree's REMAINING_RANGES.

   This function should only be called when honoring mergeinfo during
   forward merges (i.e. SOURCE->rev1 < SOURCE->rev2).
*/
static svn_error_t *
remove_noop_subtree_ranges(const merge_source_t *source,
                           const merge_target_t *target,
                           svn_ra_session_t *ra_session,
                           apr_array_header_t *children_with_mergeinfo,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  /* ### Do we need to check that we are at a uniform working revision? */
  int i;
  svn_client__merge_path_t *root_child =
    APR_ARRAY_IDX(children_with_mergeinfo, 0, svn_client__merge_path_t *);
  svn_rangelist_t *requested_ranges;
  svn_rangelist_t *subtree_gap_ranges;
  svn_rangelist_t *subtree_remaining_ranges;
  log_noop_baton_t log_gap_baton;
  svn_merge_range_t *oldest_gap_rev;
  svn_merge_range_t *youngest_gap_rev;
  svn_rangelist_t *inoperative_ranges;
  apr_pool_t *iterpool;
  const char *longest_common_subtree_ancestor = NULL;
  svn_error_t *err;

  assert(session_url_is(ra_session, source->loc2->url, scratch_pool));

  /* This function is only intended to work with forward merges. */
  if (source->loc1->rev > source->loc2->rev)
    return SVN_NO_ERROR;

  /* Another easy out: There are no subtrees. */
  if (children_with_mergeinfo->nelts < 2)
    return SVN_NO_ERROR;

  subtree_remaining_ranges = apr_array_make(scratch_pool, 1,
                                            sizeof(svn_merge_range_t *));

  /* Given the requested merge of SOURCE->rev1:rev2 might there be any
     part of this range required for subtrees but not for the target? */
  requested_ranges = svn_rangelist__initialize(MIN(source->loc1->rev,
                                                   source->loc2->rev),
                                               MAX(source->loc1->rev,
                                                   source->loc2->rev),
                                               TRUE, scratch_pool);
  SVN_ERR(svn_rangelist_remove(&subtree_gap_ranges,
                               root_child->remaining_ranges,
                               requested_ranges, FALSE, scratch_pool));

  /* Early out, nothing to operate on */
  if (!subtree_gap_ranges->nelts)
    return SVN_NO_ERROR;

  /* Create a rangelist describing every range required across all subtrees. */
  iterpool = svn_pool_create(scratch_pool);
  for (i = 1; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);

      svn_pool_clear(iterpool);

      /* CHILD->REMAINING_RANGES will be NULL if child is absent. */
      if (child->remaining_ranges && child->remaining_ranges->nelts)
        {
          /* Issue #4269: Keep track of the longest common ancestor of all the
             subtrees which require merges.  This may be a child of
             TARGET->ABSPATH, which will allow us to narrow the log request
             below. */
          if (longest_common_subtree_ancestor)
            longest_common_subtree_ancestor = svn_dirent_get_longest_ancestor(
              longest_common_subtree_ancestor, child->abspath, scratch_pool);
          else
            longest_common_subtree_ancestor = child->abspath;

          SVN_ERR(svn_rangelist_merge2(subtree_remaining_ranges,
                                       child->remaining_ranges,
                                       scratch_pool, iterpool));
        }
    }
  svn_pool_destroy(iterpool);

  /* It's possible that none of the subtrees had any remaining ranges. */
  if (!subtree_remaining_ranges->nelts)
    return SVN_NO_ERROR;

  /* Ok, *finally* we can answer what part(s) of SOURCE->rev1:rev2 are
     required for the subtrees but not the target. */
  SVN_ERR(svn_rangelist_intersect(&subtree_gap_ranges,
                                  subtree_gap_ranges,
                                  subtree_remaining_ranges, FALSE,
                                  scratch_pool));

  /* Another early out */
  if (!subtree_gap_ranges->nelts)
    return SVN_NO_ERROR;

  /* One or more subtrees need some revisions that the target doesn't need.
     Use log to determine if any of these revisions are inoperative. */
  oldest_gap_rev = APR_ARRAY_IDX(subtree_gap_ranges, 0, svn_merge_range_t *);
  youngest_gap_rev = APR_ARRAY_IDX(subtree_gap_ranges,
                         subtree_gap_ranges->nelts - 1, svn_merge_range_t *);

  /* Set up the log baton. */
  log_gap_baton.children_with_mergeinfo = children_with_mergeinfo;
  log_gap_baton.source_fspath
    = svn_client__pathrev_fspath(source->loc2, result_pool);
  log_gap_baton.target = target;
  log_gap_baton.merged_ranges = apr_array_make(scratch_pool, 0,
                                               sizeof(svn_revnum_t *));
  log_gap_baton.operative_ranges = apr_array_make(scratch_pool, 0,
                                                  sizeof(svn_revnum_t *));
  log_gap_baton.pool = svn_pool_create(scratch_pool);

  /* Find the longest common ancestor of all subtrees relative to
     RA_SESSION's URL. */
  if (longest_common_subtree_ancestor)
    longest_common_subtree_ancestor =
      svn_dirent_skip_ancestor(target->abspath,
                               longest_common_subtree_ancestor);
  else
    longest_common_subtree_ancestor = "";

  /* Invoke the svn_log_entry_receiver_t receiver log_noop_revs() from
     oldest to youngest.  The receiver is optimized to add ranges to
     log_gap_baton.merged_ranges and log_gap_baton.operative_ranges, but
     requires that the revs arrive oldest to youngest -- see log_noop_revs()
     and rangelist_merge_revision(). */
  err = get_log(ra_session, longest_common_subtree_ancestor,
                oldest_gap_rev->start + 1, youngest_gap_rev->end, TRUE,
                log_noop_revs, &log_gap_baton, scratch_pool);

  /* It's possible that the only subtrees with mergeinfo in TARGET don't have
     any corresponding subtree in SOURCE between SOURCE->REV1 < SOURCE->REV2.
     So it's also possible that we may ask for the logs of non-existent paths.
     If we do, then assume that no subtree requires any ranges that are not
     already required by the TARGET. */
  if (err)
    {
      if (err->apr_err != SVN_ERR_FS_NOT_FOUND
          && longest_common_subtree_ancestor[0] != '\0')
        return svn_error_trace(err);

      /* Asked about a non-existent subtree in SOURCE. */
      svn_error_clear(err);
      log_gap_baton.merged_ranges =
        svn_rangelist__initialize(oldest_gap_rev->start,
                                  youngest_gap_rev->end,
                                  TRUE, scratch_pool);
    }
  else
    {
      inoperative_ranges = svn_rangelist__initialize(oldest_gap_rev->start,
                                                     youngest_gap_rev->end,
                                                     TRUE, scratch_pool);
      SVN_ERR(svn_rangelist_remove(&(inoperative_ranges),
                                   log_gap_baton.operative_ranges,
                                   inoperative_ranges, FALSE, scratch_pool));
      SVN_ERR(svn_rangelist_merge2(log_gap_baton.merged_ranges, inoperative_ranges,
                                   scratch_pool, scratch_pool));
    }

  for (i = 1; i < children_with_mergeinfo->nelts; i++)
    {
      svn_client__merge_path_t *child =
        APR_ARRAY_IDX(children_with_mergeinfo, i, svn_client__merge_path_t *);

      /* CHILD->REMAINING_RANGES will be NULL if child is absent. */
      if (child->remaining_ranges && child->remaining_ranges->nelts)
        {
          /* Remove inoperative ranges from all children so we don't perform
             inoperative editor drives. */
          SVN_ERR(svn_rangelist_remove(&(child->remaining_ranges),
                                       log_gap_baton.merged_ranges,
                                       child->remaining_ranges,
                                       FALSE, result_pool));
        }
    }

  svn_pool_destroy(log_gap_baton.pool);

  return SVN_NO_ERROR;
}

/* Perform a merge of changes in SOURCE to the working copy path
   TARGET_ABSPATH. Both URLs in SOURCE, and TARGET_ABSPATH all represent
   directories -- for the single file case, the caller should use
   do_file_merge().

   CHILDREN_WITH_MERGEINFO and MERGE_B describe the merge being performed
   As this function is for a mergeinfo-aware merge, SOURCE->ancestral
   should be TRUE, and SOURCE->loc1 must be a historical ancestor of
   SOURCE->loc2, or vice-versa (see `MERGEINFO MERGE SOURCE NORMALIZATION'
   for more requirements around SOURCE).

   Mergeinfo changes will be recorded unless MERGE_B->dry_run is true.

   If mergeinfo is being recorded, SQUELCH_MERGEINFO_NOTIFICATIONS is FALSE,
   and MERGE_B->CTX->NOTIFY_FUNC2 is not NULL, then call
   MERGE_B->CTX->NOTIFY_FUNC2 with MERGE_B->CTX->NOTIFY_BATON2 and a
   svn_wc_notify_merge_record_info_begin notification before any mergeinfo
   changes are made to describe the merge performed.

   If mergeinfo is being recorded to describe this merge, and RESULT_CATALOG
   is not NULL, then don't record the new mergeinfo on the WC, but instead
   record it in RESULT_CATALOG, where the keys are absolute working copy
   paths and the values are the new mergeinfos for each.  Allocate additions
   to RESULT_CATALOG in pool which RESULT_CATALOG was created in.

   Handle DEPTH as documented for svn_client_merge5().

   CONFLICT_REPORT is as documented for do_directory_merge().

   Perform any temporary allocations in SCRATCH_POOL.

   NOTE: This is a wrapper around drive_merge_report_editor() which
   handles the complexities inherent to situations where a given
   directory's children may have intersecting merges (because they
   meet one or more of the criteria described in get_mergeinfo_paths()).
*/
static svn_error_t *
do_mergeinfo_aware_dir_merge(svn_mergeinfo_catalog_t result_catalog,
                             single_range_conflict_report_t **conflict_report,
                             const merge_source_t *source,
                             const char *target_abspath,
                             apr_array_header_t *children_with_mergeinfo,
                             const svn_diff_tree_processor_t *processor,
                             svn_depth_t depth,
                             svn_boolean_t squelch_mergeinfo_notifications,
                             merge_cmd_baton_t *merge_b,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  /* The range defining the mergeinfo we will record to describe the merge
     (assuming we are recording mergeinfo

     Note: This may be a subset of SOURCE->rev1:rev2 if
     populate_remaining_ranges() determines that some part of
     SOURCE->rev1:rev2 has already been wholly merged to TARGET_ABSPATH.
     Also, the actual editor drive(s) may be a subset of RANGE, if
     remove_noop_subtree_ranges() and/or fix_deleted_subtree_ranges()
     further tweak things. */
  svn_merge_range_t range;

  svn_ra_session_t *ra_session;
  svn_client__merge_path_t *target_merge_path;
  svn_boolean_t is_rollback = (source->loc1->rev > source->loc2->rev);

  SVN_ERR_ASSERT(source->ancestral);

  /*** If we get here, we're dealing with related sources from the
       same repository as the target -- merge tracking might be
       happenin'! ***/

  *conflict_report = NULL;

  /* Point our RA_SESSION to the URL of our youngest merge source side. */
  ra_session = is_rollback ? merge_b->ra_session1 : merge_b->ra_session2;

  /* Fill NOTIFY_B->CHILDREN_WITH_MERGEINFO with child paths (const
     svn_client__merge_path_t *) which might have intersecting merges
     because they meet one or more of the criteria described in
     get_mergeinfo_paths(). Here the paths are arranged in a depth
     first order. */
  SVN_ERR(get_mergeinfo_paths(children_with_mergeinfo,
                              merge_b->target, depth,
                              merge_b->dry_run, merge_b->same_repos,
                              merge_b->ctx, scratch_pool, scratch_pool));

  /* The first item from the NOTIFY_B->CHILDREN_WITH_MERGEINFO is always
     the target thanks to depth-first ordering. */
  target_merge_path = APR_ARRAY_IDX(children_with_mergeinfo, 0,
                                    svn_client__merge_path_t *);

  /* If we are honoring mergeinfo, then for each item in
     NOTIFY_B->CHILDREN_WITH_MERGEINFO, we need to calculate what needs to be
     merged, and then merge it.  Otherwise, we just merge what we were asked
     to merge across the whole tree.  */
  SVN_ERR(populate_remaining_ranges(children_with_mergeinfo,
                                    source, ra_session,
                                    merge_b, scratch_pool, scratch_pool));

  /* Always start with a range which describes the most inclusive merge
     possible, i.e. SOURCE->rev1:rev2. */
  range.start = source->loc1->rev;
  range.end = source->loc2->rev;
  range.inheritable = TRUE;

  if (!merge_b->reintegrate_merge)
    {
      svn_revnum_t new_range_start, start_rev;
      apr_pool_t *iterpool = svn_pool_create(scratch_pool);

      /* The merge target TARGET_ABSPATH and/or its subtrees may not need all
         of SOURCE->rev1:rev2 applied.  So examine
         NOTIFY_B->CHILDREN_WITH_MERGEINFO to find the oldest starting
         revision that actually needs to be merged (for reverse merges this is
         the youngest starting revision).

         We'll do this twice, right now for the start of the mergeinfo we will
         ultimately record to describe this merge and then later for the
         start of the actual editor drive. */
      new_range_start = get_most_inclusive_rev(
        children_with_mergeinfo, is_rollback, TRUE);
      if (SVN_IS_VALID_REVNUM(new_range_start))
        range.start = new_range_start;

      /* Remove inoperative ranges from any subtrees' remaining_ranges
         to spare the expense of noop editor drives. */
      if (!is_rollback)
        SVN_ERR(remove_noop_subtree_ranges(source, merge_b->target,
                                           ra_session,
                                           children_with_mergeinfo,
                                           scratch_pool, iterpool));

      /* Adjust subtrees' remaining_ranges to deal with issue #3067:
       * "subtrees that don't exist at the start or end of a merge range
       * shouldn't break the merge". */
      SVN_ERR(fix_deleted_subtree_ranges(source, merge_b->target,
                                         ra_session,
                                         children_with_mergeinfo,
                                         merge_b->ctx, scratch_pool, iterpool));

      /* remove_noop_subtree_ranges() and/or fix_deleted_subtree_range()
         may have further refined the starting revision for our editor
         drive. */
      start_rev =
        get_most_inclusive_rev(children_with_mergeinfo,
                               is_rollback, TRUE);

      /* Is there anything to merge? */
      if (SVN_IS_VALID_REVNUM(start_rev))
        {
          /* Now examine NOTIFY_B->CHILDREN_WITH_MERGEINFO to find the oldest
             ending revision that actually needs to be merged (for reverse
             merges this is the youngest ending revision). */
           svn_revnum_t end_rev =
             get_most_inclusive_rev(children_with_mergeinfo,
                                    is_rollback, FALSE);

          /* While END_REV is valid, do the following:

             1. Tweak each NOTIFY_B->CHILDREN_WITH_MERGEINFO element so that
                the element's remaining_ranges member has as its first element
                a range that ends with end_rev.

             2. Starting with start_rev, call drive_merge_report_editor()
                on MERGE_B->target->abspath for start_rev:end_rev.

             3. Remove the first element from each
                NOTIFY_B->CHILDREN_WITH_MERGEINFO element's remaining_ranges
                member.

             4. Again examine NOTIFY_B->CHILDREN_WITH_MERGEINFO to find the most
                inclusive starting revision that actually needs to be merged and
                update start_rev.  This prevents us from needlessly contacting the
                repository and doing a diff where we describe the entire target
                tree as *not* needing any of the requested range.  This can happen
                whenever we have mergeinfo with gaps in it for the merge source.

             5. Again examine NOTIFY_B->CHILDREN_WITH_MERGEINFO to find the most
                inclusive ending revision that actually needs to be merged and
                update end_rev.

             6. Lather, rinse, repeat.
          */

          while (end_rev != SVN_INVALID_REVNUM)
            {
              merge_source_t *real_source;
              svn_merge_range_t *first_target_range
                = (target_merge_path->remaining_ranges->nelts == 0 ? NULL
                   : APR_ARRAY_IDX(target_merge_path->remaining_ranges, 0,
                                   svn_merge_range_t *));

              /* Issue #3324: Stop editor abuse!  Don't call
                 drive_merge_report_editor() in such a way that we request an
                 editor with svn_client__get_diff_editor() for some rev X,
                 then call svn_ra_do_diff3() for some revision Y, and then
                 call reporter->set_path(PATH=="") to set the root revision
                 for the editor drive to revision Z where
                 (X != Z && X < Z < Y).  This is bogus because the server will
                 send us the diff between X:Y but the client is expecting the
                 diff between Y:Z.  See issue #3324 for full details on the
                 problems this can cause. */
              if (first_target_range
                  && start_rev != first_target_range->start)
                {
                  if (is_rollback)
                    {
                      if (end_rev < first_target_range->start)
                        end_rev = first_target_range->start;
                    }
                  else
                    {
                      if (end_rev > first_target_range->start)
                        end_rev = first_target_range->start;
                    }
                }

              svn_pool_clear(iterpool);

              slice_remaining_ranges(children_with_mergeinfo,
                                     is_rollback, end_rev, scratch_pool);

              /* Reset variables that must be reset for every drive */
              merge_b->notify_begin.last_abspath = NULL;

              real_source = subrange_source(source, start_rev, end_rev, iterpool);
              SVN_ERR(drive_merge_report_editor(
                merge_b->target->abspath,
                real_source,
                children_with_mergeinfo,
                processor,
                depth,
                merge_b,
                iterpool));

              /* If any paths picked up explicit mergeinfo as a result of
                 the merge we need to make sure any mergeinfo those paths
                 inherited is recorded and then add these paths to
                 NOTIFY_B->CHILDREN_WITH_MERGEINFO.*/
              SVN_ERR(process_children_with_new_mergeinfo(
                        merge_b, children_with_mergeinfo,
                        scratch_pool));

              /* If any subtrees had their explicit mergeinfo deleted as a
                 result of the merge then remove these paths from
                 NOTIFY_B->CHILDREN_WITH_MERGEINFO since there is no need
                 to consider these subtrees for subsequent editor drives
                 nor do we want to record mergeinfo on them describing
                 the merge itself. */
              remove_children_with_deleted_mergeinfo(
                merge_b, children_with_mergeinfo);

              /* Prepare for the next iteration (if any). */
              remove_first_range_from_remaining_ranges(
                end_rev, children_with_mergeinfo, scratch_pool);

              /* If we raised any conflicts, break out and report how much
                 we have merged. */
              if (is_path_conflicted_by_merge(merge_b))
                {
                  merge_source_t *remaining_range = NULL;

                  if (real_source->loc2->rev != source->loc2->rev)
                    remaining_range = subrange_source(source,
                                                      real_source->loc2->rev,
                                                      source->loc2->rev,
                                                      scratch_pool);
                  *conflict_report = single_range_conflict_report_create(
                                       real_source, remaining_range,
                                       result_pool);

                  range.end = end_rev;
                  break;
                }

              start_rev =
                get_most_inclusive_rev(children_with_mergeinfo,
                                       is_rollback, TRUE);
              end_rev =
                get_most_inclusive_rev(children_with_mergeinfo,
                                       is_rollback, FALSE);
            }
        }
      svn_pool_destroy(iterpool);
    }
  else
    {
      if (!merge_b->record_only)
        {
          /* Reset the last notification path so that subsequent cherry
             picked revision ranges will be notified upon subsequent
             operative merge. */
          merge_b->notify_begin.last_abspath = NULL;

          SVN_ERR(drive_merge_report_editor(merge_b->target->abspath,
                                            source,
                                            NULL,
                                            processor,
                                            depth,
                                            merge_b,
                                            scratch_pool));
        }
    }

  /* Record mergeinfo where appropriate.*/
  if (RECORD_MERGEINFO(merge_b))
    {
      const svn_client__pathrev_t *primary_src
        = is_rollback ? source->loc1 : source->loc2;
      const char *mergeinfo_path
        = svn_client__pathrev_fspath(primary_src, scratch_pool);

      SVN_ERR(record_mergeinfo_for_dir_merge(result_catalog,
                                             &range,
                                             mergeinfo_path,
                                             children_with_mergeinfo,
                                             depth,
                                             squelch_mergeinfo_notifications,
                                             merge_b,
                                             scratch_pool));

      /* If a path has an immediate parent with non-inheritable mergeinfo at
         this point, then it meets criteria 3 or 5 described in
         get_mergeinfo_paths' doc string.  For paths which exist prior to a
         merge explicit mergeinfo has already been set.  But for paths added
         during the merge this is not the case.  The path might have explicit
         mergeinfo from the merge source, but no mergeinfo yet exists
         describing *this* merge.  So the added path has either incomplete
         explicit mergeinfo or inherits incomplete mergeinfo from its
         immediate parent (if any, the parent might have only non-inheritable
         ranges in which case the path simply inherits empty mergeinfo).

         So here we look at the root path of each subtree added during the
         merge and set explicit mergeinfo on it if it meets the aforementioned
         conditions. */
      if (range.start < range.end) /* Nothing to record on added subtrees
                                      resulting from reverse merges. */
        {
          SVN_ERR(record_mergeinfo_for_added_subtrees(
                    &range, mergeinfo_path, depth,
                    squelch_mergeinfo_notifications,
                    merge_b->added_abspaths, merge_b, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

/* Helper for do_merge() when the merge target is a directory.
 *
 * If any conflict is raised during the merge, set *CONFLICTED_RANGE to
 * the revision sub-range that raised the conflict.  In this case, the
 * merge will have ended at revision CONFLICTED_RANGE and mergeinfo will
 * have been recorded for all revision sub-ranges up to and including
 * CONFLICTED_RANGE.  Otherwise, set *CONFLICTED_RANGE to NULL.
 */
static svn_error_t *
do_directory_merge(svn_mergeinfo_catalog_t result_catalog,
                   single_range_conflict_report_t **conflict_report,
                   const merge_source_t *source,
                   const char *target_abspath,
                   const svn_diff_tree_processor_t *processor,
                   svn_depth_t depth,
                   svn_boolean_t squelch_mergeinfo_notifications,
                   merge_cmd_baton_t *merge_b,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  apr_array_header_t *children_with_mergeinfo;

  /* Initialize CHILDREN_WITH_MERGEINFO. See the comment
     'THE CHILDREN_WITH_MERGEINFO ARRAY' at the start of this file. */
  children_with_mergeinfo =
    apr_array_make(scratch_pool, 16, sizeof(svn_client__merge_path_t *));

  /* And make it read-only accessible from the baton */
  merge_b->notify_begin.nodes_with_mergeinfo = children_with_mergeinfo;

  /* If we are not honoring mergeinfo we can skip right to the
     business of merging changes! */
  if (HONOR_MERGEINFO(merge_b))
    SVN_ERR(do_mergeinfo_aware_dir_merge(result_catalog, conflict_report,
                                         source, target_abspath,
                                         children_with_mergeinfo,
                                         processor, depth,
                                         squelch_mergeinfo_notifications,
                                         merge_b, result_pool, scratch_pool));
  else
    SVN_ERR(do_mergeinfo_unaware_dir_merge(conflict_report,
                                           source, target_abspath,
                                           children_with_mergeinfo,
                                           processor, depth,
                                           merge_b, result_pool, scratch_pool));

  merge_b->notify_begin.nodes_with_mergeinfo = NULL;

  return SVN_NO_ERROR;
}

/** Ensure that *RA_SESSION is opened to URL, either by reusing
 * *RA_SESSION if it is non-null and already opened to URL's
 * repository, or by allocating a new *RA_SESSION in POOL.
 * (RA_SESSION itself cannot be null, of course.)
 *
 * CTX is used as for svn_client_open_ra_session().
 */
static svn_error_t *
ensure_ra_session_url(svn_ra_session_t **ra_session,
                      const char *url,
                      const char *wri_abspath,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;

  if (*ra_session)
    {
      err = svn_ra_reparent(*ra_session, url, pool);
    }

  /* SVN_ERR_RA_ILLEGAL_URL is raised when url doesn't point to the same
     repository as ra_session. */
  if (! *ra_session || (err && err->apr_err == SVN_ERR_RA_ILLEGAL_URL))
    {
      svn_error_clear(err);
      err = svn_client_open_ra_session2(ra_session, url, wri_abspath,
                                        ctx, pool, pool);
    }
  SVN_ERR(err);

  return SVN_NO_ERROR;
}

/* Drive a merge of MERGE_SOURCES into working copy node TARGET
   and possibly record mergeinfo describing the merge -- see
   RECORD_MERGEINFO().

   If MODIFIED_SUBTREES is not NULL and all the MERGE_SOURCES are 'ancestral'
   or REINTEGRATE_MERGE is true, then replace *MODIFIED_SUBTREES with a new
   hash containing all the paths that *MODIFIED_SUBTREES contained before,
   and also every path modified, skipped, added, or tree-conflicted
   by the merge.  Keys and values of the hash are both (const char *)
   absolute paths.  The contents of the hash are allocated in RESULT_POOL.

   If the merge raises any conflicts while merging a revision range, return
   early and set *CONFLICT_REPORT to describe the details.  (In this case,
   notify that the merge is complete if and only if this was the last
   revision range of the merge.)  If there are no conflicts, set
   *CONFLICT_REPORT to NULL.  A revision range here can be one specified
   in MERGE_SOURCES or an internally generated sub-range of one of those
   when merge tracking is in use.

   For every (const merge_source_t *) merge source in MERGE_SOURCES, if
   SOURCE->ANCESTRAL is set, then the "left" and "right" side are
   ancestrally related.  (See 'MERGEINFO MERGE SOURCE NORMALIZATION'
   for more on what that means and how it matters.)

   If SOURCES_RELATED is set, the "left" and "right" sides of the
   merge source are historically related (ancestors, uncles, second
   cousins thrice removed, etc...).  (This is passed through to
   do_file_merge() to simulate the history checks that the repository
   logic does in the directory case.)

   SAME_REPOS is TRUE iff the merge sources live in the same
   repository as the one from which the target working copy has been
   checked out.

   If mergeinfo is being recorded, SQUELCH_MERGEINFO_NOTIFICATIONS is FALSE,
   and CTX->NOTIFY_FUNC2 is not NULL, then call CTX->NOTIFY_FUNC2 with
   CTX->NOTIFY_BATON2 and a svn_wc_notify_merge_record_info_begin
   notification before any mergeinfo changes are made to describe the merge
   performed.

   If mergeinfo is being recorded to describe this merge, and RESULT_CATALOG
   is not NULL, then don't record the new mergeinfo on the WC, but instead
   record it in RESULT_CATALOG, where the keys are absolute working copy
   paths and the values are the new mergeinfos for each.  Allocate additions
   to RESULT_CATALOG in pool which RESULT_CATALOG was created in.

   FORCE_DELETE, DRY_RUN, RECORD_ONLY, DEPTH, MERGE_OPTIONS,
   and CTX are as described in the docstring for svn_client_merge_peg3().

   If IGNORE_MERGEINFO is true, disable merge tracking, by treating the two
   sources as unrelated even if they actually have a common ancestor.  See
   the macro HONOR_MERGEINFO().

   If DIFF_IGNORE_ANCESTRY is true, diff the 'left' and 'right' versions
   of a node (if they are the same kind) as if they were related, even if
   they are not related.  Otherwise, diff unrelated items as a deletion
   of one thing and the addition of another.

   If not NULL, RECORD_ONLY_PATHS is a hash of (const char *) paths mapped
   to the same.  If RECORD_ONLY is true and RECORD_ONLY_PATHS is not NULL,
   then record mergeinfo describing the merge only on subtrees which contain
   items from RECORD_ONLY_PATHS.  If RECORD_ONLY is true and RECORD_ONLY_PATHS
   is NULL, then record mergeinfo on every subtree with mergeinfo in
   TARGET.

   REINTEGRATE_MERGE is TRUE if this is a reintegrate merge.

   *USE_SLEEP will be set TRUE if a sleep is required to ensure timestamp
   integrity, *USE_SLEEP will be unchanged if no sleep is required.

   SCRATCH_POOL is used for all temporary allocations.
*/
static svn_error_t *
do_merge(apr_hash_t **modified_subtrees,
         svn_mergeinfo_catalog_t result_catalog,
         svn_client__conflict_report_t **conflict_report,
         svn_boolean_t *use_sleep,
         const apr_array_header_t *merge_sources,
         const merge_target_t *target,
         svn_ra_session_t *src_session,
         svn_boolean_t sources_related,
         svn_boolean_t same_repos,
         svn_boolean_t ignore_mergeinfo,
         svn_boolean_t diff_ignore_ancestry,
         svn_boolean_t force_delete,
         svn_boolean_t dry_run,
         svn_boolean_t record_only,
         apr_hash_t *record_only_paths,
         svn_boolean_t reintegrate_merge,
         svn_boolean_t squelch_mergeinfo_notifications,
         svn_depth_t depth,
         const apr_array_header_t *merge_options,
         svn_client_ctx_t *ctx,
         apr_pool_t *result_pool,
         apr_pool_t *scratch_pool)
{
  merge_cmd_baton_t merge_cmd_baton = { 0 };
  svn_config_t *cfg;
  const char *diff3_cmd;
  const char *preserved_exts_str;
  int i;
  svn_boolean_t checked_mergeinfo_capability = FALSE;
  svn_ra_session_t *ra_session1 = NULL, *ra_session2 = NULL;
  const char *old_src_session_url = NULL;
  apr_pool_t *iterpool;
  const svn_diff_tree_processor_t *processor;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(target->abspath));

  *conflict_report = NULL;

  /* Check from some special conditions when in record-only mode
     (which is a merge-tracking thing). */
  if (record_only)
    {
      svn_boolean_t sources_ancestral = TRUE;
      int j;

      /* Find out whether all of the sources are 'ancestral'. */
      for (j = 0; j < merge_sources->nelts; j++)
        if (! APR_ARRAY_IDX(merge_sources, j, merge_source_t *)->ancestral)
          {
            sources_ancestral = FALSE;
            break;
          }

      /* We can't do a record-only merge if the sources aren't related. */
      if (! sources_ancestral)
        return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                _("Use of two URLs is not compatible with "
                                  "mergeinfo modification"));

      /* We can't do a record-only merge if the sources aren't from
         the same repository as the target. */
      if (! same_repos)
        return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                _("Merge from foreign repository is not "
                                  "compatible with mergeinfo modification"));

      /* If this is a dry-run record-only merge, there's nothing to do. */
      if (dry_run)
        return SVN_NO_ERROR;
    }

  iterpool = svn_pool_create(scratch_pool);

  /* Ensure a known depth. */
  if (depth == svn_depth_unknown)
    depth = svn_depth_infinity;

  /* Set up the diff3 command, so various callers don't have to. */
  cfg = ctx->config
        ? svn_hash_gets(ctx->config, SVN_CONFIG_CATEGORY_CONFIG)
        : NULL;
  svn_config_get(cfg, &diff3_cmd, SVN_CONFIG_SECTION_HELPERS,
                 SVN_CONFIG_OPTION_DIFF3_CMD, NULL);

  if (diff3_cmd != NULL)
    SVN_ERR(svn_path_cstring_to_utf8(&diff3_cmd, diff3_cmd, scratch_pool));

    /* See which files the user wants to preserve the extension of when
     conflict files are made. */
  svn_config_get(cfg, &preserved_exts_str, SVN_CONFIG_SECTION_MISCELLANY,
                 SVN_CONFIG_OPTION_PRESERVED_CF_EXTS, "");

  /* Build the merge context baton (or at least the parts of it that
     don't need to be reset for each merge source).  */
  merge_cmd_baton.force_delete = force_delete;
  merge_cmd_baton.dry_run = dry_run;
  merge_cmd_baton.record_only = record_only;
  merge_cmd_baton.ignore_mergeinfo = ignore_mergeinfo;
  merge_cmd_baton.diff_ignore_ancestry = diff_ignore_ancestry;
  merge_cmd_baton.same_repos = same_repos;
  merge_cmd_baton.mergeinfo_capable = FALSE;
  merge_cmd_baton.ctx = ctx;
  merge_cmd_baton.reintegrate_merge = reintegrate_merge;
  merge_cmd_baton.target = target;
  merge_cmd_baton.pool = iterpool;
  merge_cmd_baton.merge_options = merge_options;
  merge_cmd_baton.diff3_cmd = diff3_cmd;
  merge_cmd_baton.ext_patterns = *preserved_exts_str
                          ? svn_cstring_split(preserved_exts_str, "\n\r\t\v ",
                                              FALSE, scratch_pool)
                          : NULL;

  merge_cmd_baton.use_sleep = use_sleep;

  /* Do we already know the specific subtrees with mergeinfo we want
     to record-only mergeinfo on? */
  if (record_only && record_only_paths)
    merge_cmd_baton.merged_abspaths = record_only_paths;
  else
    merge_cmd_baton.merged_abspaths = apr_hash_make(result_pool);

  merge_cmd_baton.skipped_abspaths = apr_hash_make(result_pool);
  merge_cmd_baton.added_abspaths = apr_hash_make(result_pool);
  merge_cmd_baton.tree_conflicted_abspaths = apr_hash_make(result_pool);

  {
    svn_diff_tree_processor_t *merge_processor;

    merge_processor = svn_diff__tree_processor_create(&merge_cmd_baton,
                                                      scratch_pool);

    merge_processor->dir_opened   = merge_dir_opened;
    merge_processor->dir_changed  = merge_dir_changed;
    merge_processor->dir_added    = merge_dir_added;
    merge_processor->dir_deleted  = merge_dir_deleted;
    merge_processor->dir_closed   = merge_dir_closed;

    merge_processor->file_opened  = merge_file_opened;
    merge_processor->file_changed = merge_file_changed;
    merge_processor->file_added   = merge_file_added;
    merge_processor->file_deleted = merge_file_deleted;
    /* Not interested in file_closed() */

    merge_processor->node_absent = merge_node_absent;

    processor = merge_processor;
  }

  if (src_session)
    {
      SVN_ERR(svn_ra_get_session_url(src_session, &old_src_session_url,
                                     scratch_pool));
      ra_session1 = src_session;
    }

  for (i = 0; i < merge_sources->nelts; i++)
    {
      svn_node_kind_t src1_kind;
      merge_source_t *source =
        APR_ARRAY_IDX(merge_sources, i, merge_source_t *);
      single_range_conflict_report_t *conflicted_range_report;

      svn_pool_clear(iterpool);

      /* Sanity check:  if our left- and right-side merge sources are
         the same, there's nothing to here. */
      if ((strcmp(source->loc1->url, source->loc2->url) == 0)
          && (source->loc1->rev == source->loc2->rev))
        continue;

      /* Establish RA sessions to our URLs, reuse where possible. */
      SVN_ERR(ensure_ra_session_url(&ra_session1, source->loc1->url,
                                    target->abspath, ctx, scratch_pool));
      SVN_ERR(ensure_ra_session_url(&ra_session2, source->loc2->url,
                                    target->abspath, ctx, scratch_pool));

      /* Populate the portions of the merge context baton that need to
         be reset for each merge source iteration. */
      merge_cmd_baton.merge_source = *source;
      merge_cmd_baton.implicit_src_gap = NULL;
      merge_cmd_baton.conflicted_paths = NULL;
      merge_cmd_baton.paths_with_new_mergeinfo = NULL;
      merge_cmd_baton.paths_with_deleted_mergeinfo = NULL;
      merge_cmd_baton.ra_session1 = ra_session1;
      merge_cmd_baton.ra_session2 = ra_session2;

      merge_cmd_baton.notify_begin.last_abspath = NULL;

      /* Populate the portions of the merge context baton that require
         an RA session to set, but shouldn't be reset for each iteration. */
      if (! checked_mergeinfo_capability)
        {
          SVN_ERR(svn_ra_has_capability(ra_session1,
                                        &merge_cmd_baton.mergeinfo_capable,
                                        SVN_RA_CAPABILITY_MERGEINFO,
                                        iterpool));
          checked_mergeinfo_capability = TRUE;
        }

      SVN_ERR(svn_ra_check_path(ra_session1, "", source->loc1->rev,
                                &src1_kind, iterpool));

      /* Run the merge; if there are conflicts, allow the callback to
       * resolve them, and if it resolves all of them, then run the
       * merge again with the remaining revision range, until it is all
       * done. */
      do
        {
          /* Merge as far as possible without resolving any conflicts */
          if (src1_kind != svn_node_dir)
            {
              SVN_ERR(do_file_merge(result_catalog, &conflicted_range_report,
                                    source, target->abspath,
                                    processor,
                                    sources_related,
                                    squelch_mergeinfo_notifications,
                                    &merge_cmd_baton, iterpool, iterpool));
            }
          else /* Directory */
            {
              SVN_ERR(do_directory_merge(result_catalog, &conflicted_range_report,
                                         source, target->abspath,
                                         processor,
                                         depth, squelch_mergeinfo_notifications,
                                         &merge_cmd_baton, iterpool, iterpool));
            }

          /* Give the conflict resolver callback the opportunity to
           * resolve any conflicts that were raised.  If it resolves all
           * of them, go around again to merge the next sub-range (if any). */
          if (conflicted_range_report && ctx->conflict_func2 && ! dry_run)
            {
              svn_boolean_t conflicts_remain;

              SVN_ERR(svn_client__resolve_conflicts(
                        &conflicts_remain, merge_cmd_baton.conflicted_paths,
                        ctx, iterpool));
              if (conflicts_remain)
                break;

              merge_cmd_baton.conflicted_paths = NULL;
              /* Caution: this source is in iterpool */
              source = conflicted_range_report->remaining_source;
              conflicted_range_report = NULL;
            }
          else
            break;
        }
      while (source);

      /* The final mergeinfo on TARGET_WCPATH may itself elide. */
      if (! dry_run)
        SVN_ERR(svn_client__elide_mergeinfo(target->abspath, NULL,
                                            ctx, iterpool));

      /* If conflicts occurred while merging any but the very last
       * range of a multi-pass merge, we raise an error that aborts
       * the merge. The user will be asked to resolve conflicts
       * before merging subsequent revision ranges. */
      if (conflicted_range_report)
        {
          *conflict_report = conflict_report_create(
                               target->abspath, conflicted_range_report->conflicted_range,
                               (i == merge_sources->nelts - 1
                                && ! conflicted_range_report->remaining_source),
                               result_pool);
          break;
        }
    }

  if (! *conflict_report || (*conflict_report)->was_last_range)
    {
      /* Let everyone know we're finished here. */
      notify_merge_completed(target->abspath, ctx, iterpool);
    }

  /* Does the caller want to know what the merge has done? */
  if (modified_subtrees)
    {
      *modified_subtrees =
          apr_hash_overlay(result_pool, *modified_subtrees,
                           merge_cmd_baton.merged_abspaths);
      *modified_subtrees =
          apr_hash_overlay(result_pool, *modified_subtrees,
                           merge_cmd_baton.added_abspaths);
      *modified_subtrees =
          apr_hash_overlay(result_pool, *modified_subtrees,
                           merge_cmd_baton.skipped_abspaths);
      *modified_subtrees =
          apr_hash_overlay(result_pool, *modified_subtrees,
                           merge_cmd_baton.tree_conflicted_abspaths);
    }

  if (src_session)
    SVN_ERR(svn_ra_reparent(src_session, old_src_session_url, iterpool));

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Perform a two-URL merge between URLs which are related, but neither
   is a direct ancestor of the other.  This first does a real two-URL
   merge (unless this is record-only), followed by record-only merges
   to represent the changed mergeinfo.

   Set *CONFLICT_REPORT to indicate if there were any conflicts, as in
   do_merge().

   The diff to be merged is between SOURCE->loc1 (in URL1_RA_SESSION1)
   and SOURCE->loc2 (in URL2_RA_SESSION2); YCA is their youngest
   common ancestor.

   SAME_REPOS must be true if and only if the source URLs are in the same
   repository as the target working copy.

   DIFF_IGNORE_ANCESTRY is as in do_merge().

   Other arguments are as in all of the public merge APIs.

   *USE_SLEEP will be set TRUE if a sleep is required to ensure timestamp
   integrity, *USE_SLEEP will be unchanged if no sleep is required.

   SCRATCH_POOL is used for all temporary allocations.
 */
static svn_error_t *
merge_cousins_and_supplement_mergeinfo(
  svn_client__conflict_report_t **conflict_report,
  svn_boolean_t *use_sleep,
  const merge_target_t *target,
  svn_ra_session_t *URL1_ra_session,
  svn_ra_session_t *URL2_ra_session,
  const merge_source_t *source,
  const svn_client__pathrev_t *yca,
  svn_boolean_t same_repos,
  svn_depth_t depth,
  svn_boolean_t diff_ignore_ancestry,
  svn_boolean_t force_delete,
  svn_boolean_t record_only,
  svn_boolean_t dry_run,
  const apr_array_header_t *merge_options,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool)
{
  apr_array_header_t *remove_sources, *add_sources;
  apr_hash_t *modified_subtrees = NULL;

  /* Sure we could use SCRATCH_POOL throughout this function, but since this
     is a wrapper around three separate merges we'll create a subpool we can
     clear between each of the three.  If the merge target has a lot of
     subtree mergeinfo, then this will help keep memory use in check. */
  apr_pool_t *subpool = svn_pool_create(scratch_pool);

  assert(session_url_is(URL1_ra_session, source->loc1->url, scratch_pool));
  assert(session_url_is(URL2_ra_session, source->loc2->url, scratch_pool));

  SVN_ERR_ASSERT(svn_dirent_is_absolute(target->abspath));
  SVN_ERR_ASSERT(! source->ancestral);

  SVN_ERR(normalize_merge_sources_internal(
            &remove_sources, source->loc1,
            svn_rangelist__initialize(source->loc1->rev, yca->rev, TRUE,
                                      scratch_pool),
            URL1_ra_session, ctx, scratch_pool, subpool));

  SVN_ERR(normalize_merge_sources_internal(
            &add_sources, source->loc2,
            svn_rangelist__initialize(yca->rev, source->loc2->rev, TRUE,
                                      scratch_pool),
            URL2_ra_session, ctx, scratch_pool, subpool));

  *conflict_report = NULL;

  /* If this isn't a record-only merge, we'll first do a stupid
     point-to-point merge... */
  if (! record_only)
    {
      apr_array_header_t *faux_sources =
        apr_array_make(scratch_pool, 1, sizeof(merge_source_t *));

      modified_subtrees = apr_hash_make(scratch_pool);
      APR_ARRAY_PUSH(faux_sources, const merge_source_t *) = source;
      SVN_ERR(do_merge(&modified_subtrees, NULL, conflict_report, use_sleep,
                       faux_sources, target,
                       URL1_ra_session, TRUE, same_repos,
                       FALSE /*ignore_mergeinfo*/, diff_ignore_ancestry,
                       force_delete, dry_run, FALSE, NULL, TRUE,
                       FALSE, depth, merge_options, ctx,
                       scratch_pool, subpool));
      if (*conflict_report)
        {
          *conflict_report = conflict_report_dup(*conflict_report, result_pool);
          if (! (*conflict_report)->was_last_range)
            return SVN_NO_ERROR;
        }
    }
  else if (! same_repos)
    {
      return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                              _("Merge from foreign repository is not "
                                "compatible with mergeinfo modification"));
    }

  /* ... and now, if we're doing the mergeinfo thang, we execute a
     pair of record-only merges using the real sources we've
     calculated.

     Issue #3648: We don't actually perform these two record-only merges
     on the WC at first, but rather see what each would do and store that
     in two mergeinfo catalogs.  We then merge the catalogs together and
     then record the result in the WC.  This prevents the second record
     only merge from removing legitimate mergeinfo history, from the same
     source, that was made in prior merges. */
  if (same_repos && !dry_run)
    {
      svn_mergeinfo_catalog_t add_result_catalog =
        apr_hash_make(scratch_pool);
      svn_mergeinfo_catalog_t remove_result_catalog =
        apr_hash_make(scratch_pool);

      notify_mergeinfo_recording(target->abspath, NULL, ctx, scratch_pool);
      svn_pool_clear(subpool);
      SVN_ERR(do_merge(NULL, add_result_catalog, conflict_report, use_sleep,
                       add_sources, target,
                       URL1_ra_session, TRUE, same_repos,
                       FALSE /*ignore_mergeinfo*/, diff_ignore_ancestry,
                       force_delete, dry_run, TRUE,
                       modified_subtrees, TRUE,
                       TRUE, depth, merge_options, ctx,
                       scratch_pool, subpool));
      if (*conflict_report)
        {
          *conflict_report = conflict_report_dup(*conflict_report, result_pool);
          if (! (*conflict_report)->was_last_range)
            return SVN_NO_ERROR;
        }
      svn_pool_clear(subpool);
      SVN_ERR(do_merge(NULL, remove_result_catalog, conflict_report, use_sleep,
                       remove_sources, target,
                       URL1_ra_session, TRUE, same_repos,
                       FALSE /*ignore_mergeinfo*/, diff_ignore_ancestry,
                       force_delete, dry_run, TRUE,
                       modified_subtrees, TRUE,
                       TRUE, depth, merge_options, ctx,
                       scratch_pool, subpool));
      if (*conflict_report)
        {
          *conflict_report = conflict_report_dup(*conflict_report, result_pool);
          if (! (*conflict_report)->was_last_range)
            return SVN_NO_ERROR;
        }
      SVN_ERR(svn_mergeinfo_catalog_merge(add_result_catalog,
                                          remove_result_catalog,
                                          scratch_pool, scratch_pool));
      SVN_ERR(svn_client__record_wc_mergeinfo_catalog(add_result_catalog,
                                                      ctx, scratch_pool));
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

/* Perform checks to determine whether the working copy at TARGET_ABSPATH
 * can safely be used as a merge target. Checks are performed according to
 * the ALLOW_MIXED_REV, ALLOW_LOCAL_MODS, and ALLOW_SWITCHED_SUBTREES
 * parameters. If any checks fail, raise SVN_ERR_CLIENT_NOT_READY_TO_MERGE.
 *
 * E.g. if all the ALLOW_* parameters are FALSE, TARGET_ABSPATH must
 * be a single-revision, pristine, unswitched working copy.
 * In other words, it must reflect a subtree of the repository as found
 * at single revision -- although sparse checkouts are permitted. */
static svn_error_t *
ensure_wc_is_suitable_merge_target(const char *target_abspath,
                                   svn_client_ctx_t *ctx,
                                   svn_boolean_t allow_mixed_rev,
                                   svn_boolean_t allow_local_mods,
                                   svn_boolean_t allow_switched_subtrees,
                                   apr_pool_t *scratch_pool)
{
  svn_node_kind_t target_kind;

  /* Check the target exists. */
  SVN_ERR(svn_io_check_path(target_abspath, &target_kind, scratch_pool));
  if (target_kind == svn_node_none)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("Path '%s' does not exist"),
                             svn_dirent_local_style(target_abspath,
                                                    scratch_pool));
  SVN_ERR(svn_wc_read_kind2(&target_kind, ctx->wc_ctx, target_abspath,
                            FALSE, FALSE, scratch_pool));
  if (target_kind != svn_node_dir && target_kind != svn_node_file)
    return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, NULL,
                             _("Merge target '%s' does not exist in the "
                               "working copy"), target_abspath);

  /* Perform the mixed-revision check first because it's the cheapest one. */
  if (! allow_mixed_rev)
    {
      svn_revnum_t min_rev;
      svn_revnum_t max_rev;

      SVN_ERR(svn_client_min_max_revisions(&min_rev, &max_rev, target_abspath,
                                           FALSE, ctx, scratch_pool));

      if (!(SVN_IS_VALID_REVNUM(min_rev) && SVN_IS_VALID_REVNUM(max_rev)))
        {
          svn_boolean_t is_added;

          /* Allow merge into added nodes. */
          SVN_ERR(svn_wc__node_is_added(&is_added, ctx->wc_ctx, target_abspath,
                                        scratch_pool));
          if (is_added)
            return SVN_NO_ERROR;
          else
            return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                                    _("Cannot determine revision of working "
                                      "copy"));
        }

      if (min_rev != max_rev)
        return svn_error_createf(SVN_ERR_CLIENT_MERGE_UPDATE_REQUIRED, NULL,
                                 _("Cannot merge into mixed-revision working "
                                   "copy [%ld:%ld]; try updating first"),
                                   min_rev, max_rev);
    }

  /* Next, check for switched subtrees. */
  if (! allow_switched_subtrees)
    {
      svn_boolean_t is_switched;

      SVN_ERR(svn_wc__has_switched_subtrees(&is_switched, ctx->wc_ctx,
                                            target_abspath, NULL,
                                            scratch_pool));
      if (is_switched)
        return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                                _("Cannot merge into a working copy "
                                  "with a switched subtree"));
    }

  /* This is the most expensive check, so it is performed last.*/
  if (! allow_local_mods)
    {
      svn_boolean_t is_modified;

      SVN_ERR(svn_wc__has_local_mods(&is_modified, ctx->wc_ctx,
                                     target_abspath, TRUE,
                                     ctx->cancel_func,
                                     ctx->cancel_baton,
                                     scratch_pool));
      if (is_modified)
        return svn_error_create(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                                _("Cannot merge into a working copy "
                                  "that has local modifications"));
    }

  return SVN_NO_ERROR;
}

/* Throw an error if PATH_OR_URL is a path and REVISION isn't a repository
 * revision. */
static svn_error_t *
ensure_wc_path_has_repo_revision(const char *path_or_url,
                                 const svn_opt_revision_t *revision,
                                 apr_pool_t *scratch_pool)
{
  if (revision->kind != svn_opt_revision_number
      && revision->kind != svn_opt_revision_date
      && revision->kind != svn_opt_revision_head
      && ! svn_path_is_url(path_or_url))
    return svn_error_createf(
      SVN_ERR_CLIENT_BAD_REVISION, NULL,
      _("Invalid merge source '%s'; a working copy path can only be "
        "used with a repository revision (a number, a date, or head)"),
      svn_dirent_local_style(path_or_url, scratch_pool));
  return SVN_NO_ERROR;
}

/* "Open" the target WC for a merge.  That means:
 *   - find out its exact repository location
 *   - check the WC for suitability (throw an error if unsuitable)
 *
 * Set *TARGET_P to a new, fully initialized, target description structure.
 *
 * ALLOW_MIXED_REV, ALLOW_LOCAL_MODS, ALLOW_SWITCHED_SUBTREES determine
 * whether the WC is deemed suitable; see ensure_wc_is_suitable_merge_target()
 * for details.
 *
 * If the node is locally added, the rev and URL will be null/invalid. Some
 * kinds of merge can use such a target; others can't.
 */
static svn_error_t *
open_target_wc(merge_target_t **target_p,
               const char *wc_abspath,
               svn_boolean_t allow_mixed_rev,
               svn_boolean_t allow_local_mods,
               svn_boolean_t allow_switched_subtrees,
               svn_client_ctx_t *ctx,
               apr_pool_t *result_pool,
               apr_pool_t *scratch_pool)
{
  merge_target_t *target = apr_palloc(result_pool, sizeof(*target));
  svn_client__pathrev_t *origin;

  target->abspath = apr_pstrdup(result_pool, wc_abspath);

  SVN_ERR(svn_client__wc_node_get_origin(&origin, wc_abspath, ctx,
                                         result_pool, scratch_pool));
  if (origin)
    {
      target->loc = *origin;
    }
  else
    {
      svn_error_t *err;
      /* The node has no location in the repository. It's unversioned or
       * locally added or locally deleted.
       *
       * If it's locally added or deleted, find the repository root
       * URL and UUID anyway, and leave the node URL and revision as NULL
       * and INVALID.  If it's unversioned, this will throw an error. */
      err = svn_wc__node_get_repos_info(NULL, NULL,
                                        &target->loc.repos_root_url,
                                        &target->loc.repos_uuid,
                                        ctx->wc_ctx, wc_abspath,
                                        result_pool, scratch_pool);

      if (err)
        {
          if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND
              && err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY
              && err->apr_err != SVN_ERR_WC_UPGRADE_REQUIRED)
            return svn_error_trace(err);

          return svn_error_createf(SVN_ERR_ILLEGAL_TARGET, err,
                                   _("Merge target '%s' does not exist in the "
                                     "working copy"),
                                   svn_dirent_local_style(wc_abspath,
                                                          scratch_pool));
        }

      target->loc.rev = SVN_INVALID_REVNUM;
      target->loc.url = NULL;
    }

  SVN_ERR(ensure_wc_is_suitable_merge_target(
            wc_abspath, ctx,
            allow_mixed_rev, allow_local_mods, allow_switched_subtrees,
            scratch_pool));

  *target_p = target;
  return SVN_NO_ERROR;
}

/*-----------------------------------------------------------------------*/

/*** Public APIs ***/

/* The body of svn_client_merge5(), which see for details.
 *
 * If SOURCE1 @ REVISION1 is related to SOURCE2 @ REVISION2 then use merge
 * tracking (subject to other constraints -- see HONOR_MERGEINFO());
 * otherwise disable merge tracking.
 *
 * IGNORE_MERGEINFO and DIFF_IGNORE_ANCESTRY are as in do_merge().
 */
svn_error_t *
svn_client__merge_locked(svn_client__conflict_report_t **conflict_report,
                         const char *source1,
                         const svn_opt_revision_t *revision1,
                         const char *source2,
                         const svn_opt_revision_t *revision2,
                         const char *target_abspath,
                         svn_depth_t depth,
                         svn_boolean_t ignore_mergeinfo,
                         svn_boolean_t diff_ignore_ancestry,
                         svn_boolean_t force_delete,
                         svn_boolean_t record_only,
                         svn_boolean_t dry_run,
                         svn_boolean_t allow_mixed_rev,
                         const apr_array_header_t *merge_options,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  merge_target_t *target;
  svn_client__pathrev_t *source1_loc, *source2_loc;
  svn_boolean_t sources_related = FALSE;
  svn_ra_session_t *ra_session1, *ra_session2;
  apr_array_header_t *merge_sources;
  svn_error_t *err;
  svn_boolean_t use_sleep = FALSE;
  svn_client__pathrev_t *yca = NULL;
  apr_pool_t *sesspool;
  svn_boolean_t same_repos;

  /* ### FIXME: This function really ought to do a history check on
     the left and right sides of the merge source, and -- if one is an
     ancestor of the other -- just call svn_client_merge_peg3() with
     the appropriate args. */

  SVN_ERR(open_target_wc(&target, target_abspath,
                         allow_mixed_rev, TRUE, TRUE,
                         ctx, scratch_pool, scratch_pool));

  /* Open RA sessions to both sides of our merge source, and resolve URLs
   * and revisions. */
  sesspool = svn_pool_create(scratch_pool);
  SVN_ERR(svn_client__ra_session_from_path2(
            &ra_session1, &source1_loc,
            source1, NULL, revision1, revision1, ctx, sesspool));
  SVN_ERR(svn_client__ra_session_from_path2(
            &ra_session2, &source2_loc,
            source2, NULL, revision2, revision2, ctx, sesspool));

  /* We can't do a diff between different repositories. */
  /* ### We should also insist that the root URLs of the two sources match,
   *     as we are only carrying around a single source-repos-root from now
   *     on, and URL calculations will go wrong if they differ.
   *     Alternatively, teach the code to cope with differing root URLs. */
  SVN_ERR(check_same_repos(source1_loc, source1_loc->url,
                           source2_loc, source2_loc->url,
                           FALSE /* strict_urls */, scratch_pool));

  /* Do our working copy and sources come from the same repository? */
  same_repos = is_same_repos(&target->loc, source1_loc, TRUE /* strict_urls */);

  /* Unless we're ignoring ancestry, see if the two sources are related.  */
  if (! ignore_mergeinfo)
    SVN_ERR(svn_client__get_youngest_common_ancestor(
                    &yca, source1_loc, source2_loc, ra_session1, ctx,
                    scratch_pool, scratch_pool));

  /* Check for a youngest common ancestor.  If we have one, we'll be
     doing merge tracking.

     So, given a requested merge of the differences between A and
     B, and a common ancestor of C, we will find ourselves in one of
     four positions, and four different approaches:

        A == B == C   there's nothing to merge

        A == C != B   we merge the changes between A (or C) and B

        B == C != A   we merge the changes between B (or C) and A

        A != B != C   we merge the changes between A and B without
                      merge recording, then record-only two merges:
                      from A to C, and from C to B
  */
  if (yca)
    {
      /* Note that our merge sources are related. */
      sources_related = TRUE;

      /* If the common ancestor matches the right side of our merge,
         then we only need to reverse-merge the left side. */
      if ((strcmp(yca->url, source2_loc->url) == 0)
          && (yca->rev == source2_loc->rev))
        {
          SVN_ERR(normalize_merge_sources_internal(
                    &merge_sources, source1_loc,
                    svn_rangelist__initialize(source1_loc->rev, yca->rev, TRUE,
                                              scratch_pool),
                    ra_session1, ctx, scratch_pool, scratch_pool));
        }
      /* If the common ancestor matches the left side of our merge,
         then we only need to merge the right side. */
      else if ((strcmp(yca->url, source1_loc->url) == 0)
               && (yca->rev == source1_loc->rev))
        {
          SVN_ERR(normalize_merge_sources_internal(
                    &merge_sources, source2_loc,
                    svn_rangelist__initialize(yca->rev, source2_loc->rev, TRUE,
                                              scratch_pool),
                    ra_session2, ctx, scratch_pool, scratch_pool));
        }
      /* And otherwise, we need to do both: reverse merge the left
         side, and merge the right. */
      else
        {
          merge_source_t source;

          source.loc1 = source1_loc;
          source.loc2 = source2_loc;
          source.ancestral = FALSE;

          err = merge_cousins_and_supplement_mergeinfo(conflict_report,
                                                       &use_sleep,
                                                       target,
                                                       ra_session1,
                                                       ra_session2,
                                                       &source,
                                                       yca,
                                                       same_repos,
                                                       depth,
                                                       diff_ignore_ancestry,
                                                       force_delete,
                                                       record_only, dry_run,
                                                       merge_options,
                                                       ctx,
                                                       result_pool,
                                                       scratch_pool);
          /* Close our temporary RA sessions (this could've happened
             after the second call to normalize_merge_sources() inside
             the merge_cousins_and_supplement_mergeinfo() routine). */
          svn_pool_destroy(sesspool);

          if (use_sleep)
            svn_io_sleep_for_timestamps(target->abspath, scratch_pool);

          SVN_ERR(err);
          return SVN_NO_ERROR;
        }
    }
  else
    {
      /* Build a single-item merge_source_t array. */
      merge_sources = apr_array_make(scratch_pool, 1, sizeof(merge_source_t *));
      APR_ARRAY_PUSH(merge_sources, merge_source_t *)
        = merge_source_create(source1_loc, source2_loc, FALSE, scratch_pool);
    }

  err = do_merge(NULL, NULL, conflict_report, &use_sleep,
                 merge_sources, target,
                 ra_session1, sources_related, same_repos,
                 ignore_mergeinfo, diff_ignore_ancestry, force_delete, dry_run,
                 record_only, NULL, FALSE, FALSE, depth, merge_options,
                 ctx, result_pool, scratch_pool);

  /* Close our temporary RA sessions. */
  svn_pool_destroy(sesspool);

  if (use_sleep)
    svn_io_sleep_for_timestamps(target->abspath, scratch_pool);

  SVN_ERR(err);
  return SVN_NO_ERROR;
}

/* Set *TARGET_ABSPATH to the absolute path of, and *LOCK_ABSPATH to
 the absolute path to lock for, TARGET_WCPATH. */
static svn_error_t *
get_target_and_lock_abspath(const char **target_abspath,
                            const char **lock_abspath,
                            const char *target_wcpath,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool)
{
  svn_node_kind_t kind;
  SVN_ERR(svn_dirent_get_absolute(target_abspath, target_wcpath,
                                  result_pool));
  SVN_ERR(svn_wc_read_kind2(&kind, ctx->wc_ctx, *target_abspath,
                            FALSE, FALSE, result_pool));
  if (kind == svn_node_dir)
    *lock_abspath = *target_abspath;
  else
    *lock_abspath = svn_dirent_dirname(*target_abspath, result_pool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_merge5(const char *source1,
                  const svn_opt_revision_t *revision1,
                  const char *source2,
                  const svn_opt_revision_t *revision2,
                  const char *target_wcpath,
                  svn_depth_t depth,
                  svn_boolean_t ignore_mergeinfo,
                  svn_boolean_t diff_ignore_ancestry,
                  svn_boolean_t force_delete,
                  svn_boolean_t record_only,
                  svn_boolean_t dry_run,
                  svn_boolean_t allow_mixed_rev,
                  const apr_array_header_t *merge_options,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{
  const char *target_abspath, *lock_abspath;
  svn_client__conflict_report_t *conflict_report;

  /* Sanity check our input -- we require specified revisions,
   * and either 2 paths or 2 URLs. */
  if ((revision1->kind == svn_opt_revision_unspecified)
      || (revision2->kind == svn_opt_revision_unspecified))
    return svn_error_create(SVN_ERR_CLIENT_BAD_REVISION, NULL,
                            _("Not all required revisions are specified"));
  if (svn_path_is_url(source1) != svn_path_is_url(source2))
    return svn_error_create(SVN_ERR_ILLEGAL_TARGET, NULL,
                            _("Merge sources must both be "
                              "either paths or URLs"));
  /* A WC path must be used with a repository revision, as we can't
   * (currently) use the WC itself as a source, we can only read the URL
   * from it and use that. */
  SVN_ERR(ensure_wc_path_has_repo_revision(source1, revision1, pool));
  SVN_ERR(ensure_wc_path_has_repo_revision(source2, revision2, pool));

  SVN_ERR(get_target_and_lock_abspath(&target_abspath, &lock_abspath,
                                      target_wcpath, ctx, pool));

  if (!dry_run)
    SVN_WC__CALL_WITH_WRITE_LOCK(
      svn_client__merge_locked(&conflict_report,
                               source1, revision1, source2, revision2,
                               target_abspath, depth, ignore_mergeinfo,
                               diff_ignore_ancestry,
                               force_delete, record_only, dry_run,
                               allow_mixed_rev, merge_options, ctx, pool, pool),
      ctx->wc_ctx, lock_abspath, FALSE /* lock_anchor */, pool);
  else
    SVN_ERR(svn_client__merge_locked(&conflict_report,
                                     source1, revision1, source2, revision2,
                                     target_abspath, depth, ignore_mergeinfo,
                                     diff_ignore_ancestry,
                                     force_delete, record_only, dry_run,
                                     allow_mixed_rev, merge_options, ctx, pool,
                                     pool));

  SVN_ERR(svn_client__make_merge_conflict_error(conflict_report, pool));
  return SVN_NO_ERROR;
}


/* Check if mergeinfo for a given path is described explicitly or via
   inheritance in a mergeinfo catalog.

   If REPOS_REL_PATH exists in CATALOG and has mergeinfo containing
   MERGEINFO, then set *IN_CATALOG to TRUE.  If REPOS_REL_PATH does
   not exist in CATALOG, then find its nearest parent which does exist.
   If the mergeinfo REPOS_REL_PATH would inherit from that parent
   contains MERGEINFO then set *IN_CATALOG to TRUE.  Set *IN_CATALOG
   to FALSE in all other cases.

   Set *CAT_KEY_PATH to the key path in CATALOG for REPOS_REL_PATH's
   explicit or inherited mergeinfo.  If no explicit or inherited mergeinfo
   is found for REPOS_REL_PATH then set *CAT_KEY_PATH to NULL.

   User RESULT_POOL to allocate *CAT_KEY_PATH.  Use SCRATCH_POOL for
   temporary allocations. */
static svn_error_t *
mergeinfo_in_catalog(svn_boolean_t *in_catalog,
                     const char **cat_key_path,
                     const char *repos_rel_path,
                     svn_mergeinfo_t mergeinfo,
                     svn_mergeinfo_catalog_t catalog,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  const char *walk_path = NULL;

  *in_catalog = FALSE;
  *cat_key_path = NULL;

  if (mergeinfo && catalog && apr_hash_count(catalog))
    {
      const char *path = repos_rel_path;

      /* Start with the assumption there is no explicit or inherited
         mergeinfo for REPOS_REL_PATH in CATALOG. */
      svn_mergeinfo_t mergeinfo_in_cat = NULL;

      while (1)
        {
          mergeinfo_in_cat = svn_hash_gets(catalog, path);

          if (mergeinfo_in_cat) /* Found it! */
            {
              *cat_key_path = apr_pstrdup(result_pool, path);
              break;
            }
          else /* Look for inherited mergeinfo. */
            {
              walk_path = svn_relpath_join(svn_relpath_basename(path,
                                                                scratch_pool),
                                           walk_path ? walk_path : "",
                                           scratch_pool);
              path = svn_relpath_dirname(path, scratch_pool);

              if (path[0] == '\0') /* No mergeinfo to inherit. */
                break;
            }
        }

      if (mergeinfo_in_cat)
        {
          if (walk_path)
            SVN_ERR(svn_mergeinfo__add_suffix_to_mergeinfo(&mergeinfo_in_cat,
                                                           mergeinfo_in_cat,
                                                           walk_path,
                                                           scratch_pool,
                                                           scratch_pool));
          SVN_ERR(svn_mergeinfo_intersect2(&mergeinfo_in_cat,
                                           mergeinfo_in_cat, mergeinfo,
                                           TRUE,
                                           scratch_pool, scratch_pool));
          SVN_ERR(svn_mergeinfo__equals(in_catalog, mergeinfo_in_cat,
                                        mergeinfo, TRUE, scratch_pool));
        }
    }

  return SVN_NO_ERROR;
}

/* A svn_log_entry_receiver_t baton for log_find_operative_revs(). */
typedef struct log_find_operative_baton_t
{
  /* The catalog of explicit mergeinfo on a reintegrate source. */
  svn_mergeinfo_catalog_t merged_catalog;

  /* The catalog of unmerged history from the reintegrate target to
     the source which we will create.  Allocated in RESULT_POOL. */
  svn_mergeinfo_catalog_t unmerged_catalog;

  /* The repository absolute path of the reintegrate target. */
  const char *target_fspath;

  /* The path of the reintegrate source relative to the repository root. */
  const char *source_repos_rel_path;

  apr_pool_t *result_pool;
} log_find_operative_baton_t;

/* A svn_log_entry_receiver_t callback for find_unsynced_ranges(). */
static svn_error_t *
log_find_operative_revs(void *baton,
                        svn_log_entry_t *log_entry,
                        apr_pool_t *pool)
{
  log_find_operative_baton_t *log_baton = baton;
  apr_hash_index_t *hi;
  svn_revnum_t revision;

  /* It's possible that authz restrictions on the merge source prevent us
     from knowing about any of the changes for LOG_ENTRY->REVISION. */
  if (!log_entry->changed_paths2)
    return SVN_NO_ERROR;

  revision = log_entry->revision;

  for (hi = apr_hash_first(pool, log_entry->changed_paths2);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *subtree_missing_this_rev;
      const char *path = apr_hash_this_key(hi);
      const char *rel_path;
      const char *source_rel_path;
      svn_boolean_t in_catalog;
      svn_mergeinfo_t log_entry_as_mergeinfo;

      rel_path = svn_fspath__skip_ancestor(log_baton->target_fspath, path);
      /* Easy out: The path is not within the tree of interest. */
      if (rel_path == NULL)
        continue;

      source_rel_path = svn_relpath_join(log_baton->source_repos_rel_path,
                                         rel_path, pool);

      SVN_ERR(svn_mergeinfo_parse(&log_entry_as_mergeinfo,
                                  apr_psprintf(pool, "%s:%ld",
                                               path, revision),
                                  pool));

      SVN_ERR(mergeinfo_in_catalog(&in_catalog, &subtree_missing_this_rev,
                                   source_rel_path, log_entry_as_mergeinfo,
                                   log_baton->merged_catalog,
                                   pool, pool));

      if (!in_catalog)
        {
          svn_mergeinfo_t unmerged_for_key;
          const char *suffix, *missing_path;

          /* If there is no mergeinfo on the source tree we'll say
             the "subtree" missing this revision is the root of the
             source. */
          if (!subtree_missing_this_rev)
            subtree_missing_this_rev = log_baton->source_repos_rel_path;

          suffix = svn_relpath_skip_ancestor(subtree_missing_this_rev,
                                             source_rel_path);
          if (suffix && suffix[0] != '\0')
            {
              missing_path = apr_pstrmemdup(pool, path,
                                            strlen(path) - strlen(suffix) - 1);
            }
          else
            {
              missing_path = path;
            }

          SVN_ERR(svn_mergeinfo_parse(&log_entry_as_mergeinfo,
                                      apr_psprintf(pool, "%s:%ld",
                                                   missing_path, revision),
                                      log_baton->result_pool));
          unmerged_for_key = svn_hash_gets(log_baton->unmerged_catalog,
                                           subtree_missing_this_rev);

          if (unmerged_for_key)
            {
              SVN_ERR(svn_mergeinfo_merge2(unmerged_for_key,
                                           log_entry_as_mergeinfo,
                                           log_baton->result_pool,
                                           pool));
            }
          else
            {
              svn_hash_sets(log_baton->unmerged_catalog,
                            apr_pstrdup(log_baton->result_pool,
                                        subtree_missing_this_rev),
                            log_entry_as_mergeinfo);
            }

        }
    }
  return SVN_NO_ERROR;
}

/* Determine if the mergeinfo on a reintegrate source SOURCE_LOC,
   reflects that the source is fully synced with the reintegrate target
   TARGET_LOC, even if a naive interpretation of the source's
   mergeinfo says otherwise -- See issue #3577.

   UNMERGED_CATALOG represents the history (as mergeinfo) from
   TARGET_LOC that is not represented in SOURCE_LOC's
   explicit/inherited mergeinfo as represented by MERGED_CATALOG.
   MERGED_CATALOG may be empty if the source has no explicit or inherited
   mergeinfo.

   Check that all of the unmerged revisions in UNMERGED_CATALOG's
   mergeinfos are "phantoms", that is, one of the following conditions holds:

     1) The revision affects no corresponding paths in SOURCE_LOC.

     2) The revision affects corresponding paths in SOURCE_LOC,
        but based on the mergeinfo in MERGED_CATALOG, the change was
        previously merged.

   Make a deep copy, allocated in RESULT_POOL, of any portions of
   UNMERGED_CATALOG that are not phantoms, to TRUE_UNMERGED_CATALOG.

   Note: The keys in all mergeinfo catalogs used here are relative to the
   root of the repository.

   RA_SESSION is an RA session open to the repository of TARGET_LOC; it may
   be temporarily reparented within this function.

   Use SCRATCH_POOL for all temporary allocations. */
static svn_error_t *
find_unsynced_ranges(const svn_client__pathrev_t *source_loc,
                     const svn_client__pathrev_t *target_loc,
                     svn_mergeinfo_catalog_t unmerged_catalog,
                     svn_mergeinfo_catalog_t merged_catalog,
                     svn_mergeinfo_catalog_t true_unmerged_catalog,
                     svn_ra_session_t *ra_session,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_rangelist_t *potentially_unmerged_ranges = NULL;

  /* Convert all the unmerged history to a rangelist. */
  if (apr_hash_count(unmerged_catalog))
    {
      apr_hash_index_t *hi_catalog;

      potentially_unmerged_ranges =
        apr_array_make(scratch_pool, 1, sizeof(svn_merge_range_t *));

      for (hi_catalog = apr_hash_first(scratch_pool, unmerged_catalog);
           hi_catalog;
           hi_catalog = apr_hash_next(hi_catalog))
        {
          svn_mergeinfo_t mergeinfo = apr_hash_this_val(hi_catalog);

          SVN_ERR(svn_rangelist__merge_many(potentially_unmerged_ranges,
                                            mergeinfo,
                                            scratch_pool, scratch_pool));
        }
    }

  /* Find any unmerged revisions which both affect the source and
     are not yet merged to it. */
  if (potentially_unmerged_ranges)
    {
      svn_revnum_t oldest_rev =
        (APR_ARRAY_IDX(potentially_unmerged_ranges,
                       0,
                       svn_merge_range_t *))->start + 1;
      svn_revnum_t youngest_rev =
        (APR_ARRAY_IDX(potentially_unmerged_ranges,
                       potentially_unmerged_ranges->nelts - 1,
                       svn_merge_range_t *))->end;
      log_find_operative_baton_t log_baton;
      const char *old_session_url = NULL;
      svn_error_t *err;

      log_baton.merged_catalog = merged_catalog;
      log_baton.unmerged_catalog = true_unmerged_catalog;
      log_baton.source_repos_rel_path
        = svn_client__pathrev_relpath(source_loc, scratch_pool);
      log_baton.target_fspath
        = svn_client__pathrev_fspath(target_loc, scratch_pool);
      log_baton.result_pool = result_pool;

      /* Reparent the session to TARGET_LOC if this target location
       * exists within the unmerged revision range. */
      if (target_loc->rev <= youngest_rev && target_loc->rev >= oldest_rev)
        SVN_ERR(svn_client__ensure_ra_session_url(
                  &old_session_url, ra_session, target_loc->url, scratch_pool));

      err = get_log(ra_session, "", youngest_rev, oldest_rev,
                    TRUE, /* discover_changed_paths */
                    log_find_operative_revs, &log_baton,
                    scratch_pool);
      if (old_session_url)
        err = svn_error_compose_create(err,
                                       svn_ra_reparent(ra_session,
                                                       old_session_url,
                                                       scratch_pool));
      SVN_ERR(err);
    }

  return SVN_NO_ERROR;
}


/* Find the youngest revision that has been merged from target to source.
 *
 * If any location in TARGET_HISTORY_AS_MERGEINFO is mentioned in
 * SOURCE_MERGEINFO, then we know that at least one merge was done from the
 * target to the source.  In that case, set *YOUNGEST_MERGED_REV to the
 * youngest revision of that intersection (unless *YOUNGEST_MERGED_REV is
 * already younger than that).  Otherwise, leave *YOUNGEST_MERGED_REV alone.
 */
static svn_error_t *
find_youngest_merged_rev(svn_revnum_t *youngest_merged_rev,
                         svn_mergeinfo_t target_history_as_mergeinfo,
                         svn_mergeinfo_t source_mergeinfo,
                         apr_pool_t *scratch_pool)
{
  svn_mergeinfo_t explicit_source_target_history_intersection;

  SVN_ERR(svn_mergeinfo_intersect2(
            &explicit_source_target_history_intersection,
            source_mergeinfo, target_history_as_mergeinfo, TRUE,
            scratch_pool, scratch_pool));
  if (apr_hash_count(explicit_source_target_history_intersection))
    {
      svn_revnum_t old_rev, young_rev;

      /* Keep track of the youngest revision merged from target to source. */
      SVN_ERR(svn_mergeinfo__get_range_endpoints(
                &young_rev, &old_rev,
                explicit_source_target_history_intersection, scratch_pool));
      if (!SVN_IS_VALID_REVNUM(*youngest_merged_rev)
          || (young_rev > *youngest_merged_rev))
        *youngest_merged_rev = young_rev;
    }

  return SVN_NO_ERROR;
}

/* Set *FILTERED_MERGEINFO_P to the parts of TARGET_HISTORY_AS_MERGEINFO
 * that are not present in the source branch.
 *
 * SOURCE_MERGEINFO is the explicit or inherited mergeinfo of the source
 * branch SOURCE_PATHREV.  Extend SOURCE_MERGEINFO, modifying it in
 * place, to include the natural history (implicit mergeinfo) of
 * SOURCE_PATHREV.  ### But make these additions in SCRATCH_POOL.
 *
 * SOURCE_RA_SESSION is an RA session open to the repository containing
 * SOURCE_PATHREV; it may be temporarily reparented within this function.
 *
 * ### [JAF] This function is named '..._subroutine' simply because I
 *     factored it out based on code similarity, without knowing what it's
 *     purpose is.  We should clarify its purpose and choose a better name.
 */
static svn_error_t *
find_unmerged_mergeinfo_subroutine(svn_mergeinfo_t *filtered_mergeinfo_p,
                                   svn_mergeinfo_t target_history_as_mergeinfo,
                                   svn_mergeinfo_t source_mergeinfo,
                                   const svn_client__pathrev_t *source_pathrev,
                                   svn_ra_session_t *source_ra_session,
                                   svn_client_ctx_t *ctx,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_mergeinfo_t source_history_as_mergeinfo;

  /* Get the source path's natural history and merge it into source
     path's explicit or inherited mergeinfo. */
  SVN_ERR(svn_client__get_history_as_mergeinfo(
            &source_history_as_mergeinfo, NULL /* has_rev_zero_history */,
            source_pathrev, source_pathrev->rev, SVN_INVALID_REVNUM,
            source_ra_session, ctx, scratch_pool));
  SVN_ERR(svn_mergeinfo_merge2(source_mergeinfo,
                               source_history_as_mergeinfo,
                               scratch_pool, scratch_pool));

  /* Now source_mergeinfo represents everything we know about
     source_path's history.  Now we need to know what part, if any, of the
     corresponding target's history is *not* part of source_path's total
     history; because it is neither shared history nor was it ever merged
     from the target to the source. */
  SVN_ERR(svn_mergeinfo_remove2(filtered_mergeinfo_p,
                                source_mergeinfo,
                                target_history_as_mergeinfo, TRUE,
                                result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

/* Helper for calculate_left_hand_side() which produces a mergeinfo catalog
   describing what parts of of the reintegrate target have not previously been
   merged to the reintegrate source.

   SOURCE_CATALOG is the collection of explicit mergeinfo on SOURCE_LOC and
   all its children, i.e. the mergeinfo catalog for the reintegrate source.

   TARGET_HISTORY_HASH is a hash of (const char *) paths mapped to
   svn_mergeinfo_t representing the location history.  Each of these
   path keys represent a path in the reintegrate target, relative to the
   repository root, which has explicit mergeinfo and/or is the reintegrate
   target itself.  The svn_mergeinfo_t's contain the natural history of each
   path@TARGET_REV.  Effectively this is the mergeinfo catalog on the
   reintegrate target.

   YC_ANCESTOR_REV is the revision of the youngest common ancestor of the
   reintegrate source and the reintegrate target.

   SOURCE_LOC is the reintegrate source.

   SOURCE_RA_SESSION is a session opened to the URL of SOURCE_LOC
   and TARGET_RA_SESSION is open to TARGET->loc.url.

   For each entry in TARGET_HISTORY_HASH check that the history it
   represents is contained in either the explicit mergeinfo for the
   corresponding path in SOURCE_CATALOG, the corresponding path's inherited
   mergeinfo (if no explicit mergeinfo for the path is found in
   SOURCE_CATALOG), or the corresponding path's natural history.  Populate
   *UNMERGED_TO_SOURCE_CATALOG with the corresponding source paths mapped to
   the mergeinfo from the target's natural history which is *not* found.  Also
   include any mergeinfo from SOURCE_CATALOG which explicitly describes the
   target's history but for which *no* entry was found in
   TARGET_HISTORY_HASH.

   If no part of TARGET_HISTORY_HASH is found in SOURCE_CATALOG set
   *YOUNGEST_MERGED_REV to SVN_INVALID_REVNUM; otherwise set it to the youngest
   revision previously merged from the target to the source, and filter
   *UNMERGED_TO_SOURCE_CATALOG so that it contains no ranges greater than
   *YOUNGEST_MERGED_REV.

   *UNMERGED_TO_SOURCE_CATALOG is (deeply) allocated in RESULT_POOL.
   SCRATCH_POOL is used for all temporary allocations.  */
static svn_error_t *
find_unmerged_mergeinfo(svn_mergeinfo_catalog_t *unmerged_to_source_catalog,
                        svn_revnum_t *youngest_merged_rev,
                        svn_revnum_t yc_ancestor_rev,
                        svn_mergeinfo_catalog_t source_catalog,
                        apr_hash_t *target_history_hash,
                        const svn_client__pathrev_t *source_loc,
                        const merge_target_t *target,
                        svn_ra_session_t *source_ra_session,
                        svn_ra_session_t *target_ra_session,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const char *source_repos_rel_path
    = svn_client__pathrev_relpath(source_loc, scratch_pool);
  const char *target_repos_rel_path
    = svn_client__pathrev_relpath(&target->loc, scratch_pool);
  apr_hash_index_t *hi;
  svn_mergeinfo_catalog_t new_catalog = apr_hash_make(result_pool);
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  assert(session_url_is(source_ra_session, source_loc->url, scratch_pool));
  assert(session_url_is(target_ra_session, target->loc.url, scratch_pool));

  *youngest_merged_rev = SVN_INVALID_REVNUM;

  /* Examine the natural history of each path in the reintegrate target
     with explicit mergeinfo. */
  for (hi = apr_hash_first(scratch_pool, target_history_hash);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *target_path = apr_hash_this_key(hi);
      svn_mergeinfo_t target_history_as_mergeinfo = apr_hash_this_val(hi);
      const char *path_rel_to_session
        = svn_relpath_skip_ancestor(target_repos_rel_path, target_path);
      const char *source_path;
      svn_client__pathrev_t *source_pathrev;
      svn_mergeinfo_t source_mergeinfo, filtered_mergeinfo;

      svn_pool_clear(iterpool);

      source_path = svn_relpath_join(source_repos_rel_path,
                                     path_rel_to_session, iterpool);
      source_pathrev = svn_client__pathrev_join_relpath(
                         source_loc, path_rel_to_session, iterpool);

      /* Remove any target history that is also part of the source's history,
         i.e. their common ancestry.  By definition this has already been
         "merged" from the target to the source.  If the source has explicit
         self referential mergeinfo it would intersect with the target's
         history below, making it appear that some merges had been done from
         the target to the source, when this might not actually be the case. */
      SVN_ERR(svn_mergeinfo__filter_mergeinfo_by_ranges(
        &target_history_as_mergeinfo, target_history_as_mergeinfo,
        source_loc->rev, yc_ancestor_rev, TRUE, iterpool, iterpool));

      /* Look for any explicit mergeinfo on the source path corresponding to
         the target path.  If we find any remove that from SOURCE_CATALOG.
         When this iteration over TARGET_HISTORY_HASH is complete all that
         should be left in SOURCE_CATALOG are subtrees that have explicit
         mergeinfo on the reintegrate source where there is no corresponding
         explicit mergeinfo on the reintegrate target. */
      source_mergeinfo = svn_hash_gets(source_catalog, source_path);
      if (source_mergeinfo)
        {
          svn_hash_sets(source_catalog, source_path, NULL);

          SVN_ERR(find_youngest_merged_rev(youngest_merged_rev,
                                           target_history_as_mergeinfo,
                                           source_mergeinfo,
                                           iterpool));
        }
      else
        {
          /* There is no mergeinfo on source_path *or* source_path doesn't
             exist at all.  If simply doesn't exist we can ignore it
             altogether. */
          svn_node_kind_t kind;

          SVN_ERR(svn_ra_check_path(source_ra_session,
                                    path_rel_to_session,
                                    source_loc->rev, &kind, iterpool));
          if (kind == svn_node_none)
              continue;
          /* Else source_path does exist though it has no explicit mergeinfo.
             Find its inherited mergeinfo.  If it doesn't have any then simply
             set source_mergeinfo to an empty hash. */
          SVN_ERR(svn_client__get_repos_mergeinfo(
                    &source_mergeinfo, source_ra_session,
                    source_pathrev->url, source_pathrev->rev,
                    svn_mergeinfo_inherited, FALSE /*squelch_incapable*/,
                    iterpool));
          if (!source_mergeinfo)
            source_mergeinfo = apr_hash_make(iterpool);
        }

      /* Use scratch_pool rather than iterpool because filtered_mergeinfo
         is going into new_catalog below and needs to last to the end of
         this function. */
      SVN_ERR(find_unmerged_mergeinfo_subroutine(
                &filtered_mergeinfo, target_history_as_mergeinfo,
                source_mergeinfo, source_pathrev,
                source_ra_session, ctx, scratch_pool, iterpool));
      svn_hash_sets(new_catalog, apr_pstrdup(scratch_pool, source_path),
                    filtered_mergeinfo);
    }

  /* Are there any subtrees with explicit mergeinfo still left in the merge
     source where there was no explicit mergeinfo for the corresponding path
     in the merge target?  If so, add the intersection of those path's
     mergeinfo and the corresponding target path's mergeinfo to
     new_catalog. */
  for (hi = apr_hash_first(scratch_pool, source_catalog);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *source_path = apr_hash_this_key(hi);
      const char *path_rel_to_session =
        svn_relpath_skip_ancestor(source_repos_rel_path, source_path);
      const char *source_url;
      svn_mergeinfo_t source_mergeinfo = apr_hash_this_val(hi);
      svn_mergeinfo_t filtered_mergeinfo;
      svn_client__pathrev_t *target_pathrev;
      svn_mergeinfo_t target_history_as_mergeinfo;
      svn_error_t *err;

      svn_pool_clear(iterpool);

      source_url = svn_path_url_add_component2(source_loc->url,
                                               path_rel_to_session, iterpool);
      target_pathrev = svn_client__pathrev_join_relpath(
                         &target->loc, path_rel_to_session, iterpool);
      err = svn_client__get_history_as_mergeinfo(&target_history_as_mergeinfo,
                                                 NULL /* has_rev_zero_history */,
                                                 target_pathrev,
                                                 target->loc.rev,
                                                 SVN_INVALID_REVNUM,
                                                 target_ra_session,
                                                 ctx, iterpool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_FS_NOT_FOUND
              || err->apr_err == SVN_ERR_RA_DAV_REQUEST_FAILED)
            {
              /* This path with explicit mergeinfo in the source doesn't
                 exist on the target. */
              svn_error_clear(err);
              err = NULL;
            }
          else
            {
              return svn_error_trace(err);
            }
        }
      else
        {
          svn_client__pathrev_t *pathrev;

          SVN_ERR(find_youngest_merged_rev(youngest_merged_rev,
                                           target_history_as_mergeinfo,
                                           source_mergeinfo,
                                           iterpool));

          /* Use scratch_pool rather than iterpool because filtered_mergeinfo
             is going into new_catalog below and needs to last to the end of
             this function. */
          /* ### Why looking at SOURCE_url at TARGET_rev? */
          SVN_ERR(svn_client__pathrev_create_with_session(
                    &pathrev, source_ra_session, target->loc.rev, source_url,
                    iterpool));
          SVN_ERR(find_unmerged_mergeinfo_subroutine(
                    &filtered_mergeinfo, target_history_as_mergeinfo,
                    source_mergeinfo, pathrev,
                    source_ra_session, ctx, scratch_pool, iterpool));
          if (apr_hash_count(filtered_mergeinfo))
            svn_hash_sets(new_catalog,
                          apr_pstrdup(scratch_pool, source_path),
                          filtered_mergeinfo);
        }
    }

  /* Limit new_catalog to the youngest revisions previously merged from
     the target to the source. */
  if (SVN_IS_VALID_REVNUM(*youngest_merged_rev))
    SVN_ERR(svn_mergeinfo__filter_catalog_by_ranges(&new_catalog,
                                                    new_catalog,
                                                    *youngest_merged_rev,
                                                    0, /* No oldest bound. */
                                                    TRUE,
                                                    scratch_pool,
                                                    scratch_pool));

  /* Make a shiny new copy before blowing away all the temporary pools. */
  *unmerged_to_source_catalog = svn_mergeinfo_catalog_dup(new_catalog,
                                                          result_pool);
  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Helper for svn_client_merge_reintegrate() which calculates the
   'left hand side' of the underlying two-URL merge that a --reintegrate
   merge actually performs.  If no merge should be performed, set
   *LEFT_P to NULL.

   TARGET->abspath is the absolute working copy path of the reintegrate
   merge.

   SOURCE_LOC is the reintegrate source.

   SUBTREES_WITH_MERGEINFO is a hash of (const char *) absolute paths mapped
   to (svn_mergeinfo_t *) mergeinfo values for each working copy path with
   explicit mergeinfo in TARGET->abspath.  Actually we only need to know the
   paths, not the mergeinfo.

   TARGET->loc.rev is the working revision the entire WC tree rooted at
   TARGET is at.

   Populate *UNMERGED_TO_SOURCE_CATALOG with the mergeinfo describing what
   parts of TARGET->loc have not been merged to SOURCE_LOC, up to the
   youngest revision ever merged from the TARGET->abspath to the source if
   such exists, see doc string for find_unmerged_mergeinfo().

   SOURCE_RA_SESSION is a session opened to the SOURCE_LOC
   and TARGET_RA_SESSION is open to TARGET->loc.url.

   *LEFT_P, *MERGED_TO_SOURCE_CATALOG , and *UNMERGED_TO_SOURCE_CATALOG are
   allocated in RESULT_POOL.  SCRATCH_POOL is used for all temporary
   allocations. */
static svn_error_t *
calculate_left_hand_side(svn_client__pathrev_t **left_p,
                         svn_mergeinfo_catalog_t *merged_to_source_catalog,
                         svn_mergeinfo_catalog_t *unmerged_to_source_catalog,
                         const merge_target_t *target,
                         apr_hash_t *subtrees_with_mergeinfo,
                         const svn_client__pathrev_t *source_loc,
                         svn_ra_session_t *source_ra_session,
                         svn_ra_session_t *target_ra_session,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_mergeinfo_catalog_t mergeinfo_catalog, unmerged_catalog;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_hash_index_t *hi;
  /* hash of paths mapped to arrays of svn_mergeinfo_t. */
  apr_hash_t *target_history_hash = apr_hash_make(scratch_pool);
  svn_revnum_t youngest_merged_rev;
  svn_client__pathrev_t *yc_ancestor;

  assert(session_url_is(source_ra_session, source_loc->url, scratch_pool));
  assert(session_url_is(target_ra_session, target->loc.url, scratch_pool));

  /* Initialize our return variables. */
  *left_p = NULL;

  /* TARGET->abspath may not have explicit mergeinfo and thus may not be
     contained within SUBTREES_WITH_MERGEINFO.  If this is the case then
     add a dummy item for TARGET->abspath so we get its history (i.e. implicit
     mergeinfo) below.  */
  if (!svn_hash_gets(subtrees_with_mergeinfo, target->abspath))
    svn_hash_sets(subtrees_with_mergeinfo, target->abspath,
                  apr_hash_make(result_pool));

  /* Get the history segments (as mergeinfo) for TARGET->abspath and any of
     its subtrees with explicit mergeinfo. */
  for (hi = apr_hash_first(scratch_pool, subtrees_with_mergeinfo);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *local_abspath = apr_hash_this_key(hi);
      svn_client__pathrev_t *target_child;
      const char *repos_relpath;
      svn_mergeinfo_t target_history_as_mergeinfo;

      svn_pool_clear(iterpool);

      /* Convert the absolute path with mergeinfo on it to a path relative
         to the session root. */
      SVN_ERR(svn_wc__node_get_repos_info(NULL, &repos_relpath, NULL, NULL,
                                          ctx->wc_ctx, local_abspath,
                                          scratch_pool, iterpool));
      target_child = svn_client__pathrev_create_with_relpath(
                       target->loc.repos_root_url, target->loc.repos_uuid,
                       target->loc.rev, repos_relpath, iterpool);
      SVN_ERR(svn_client__get_history_as_mergeinfo(&target_history_as_mergeinfo,
                                                   NULL /* has_rev_zero_hist */,
                                                   target_child,
                                                   target->loc.rev,
                                                   SVN_INVALID_REVNUM,
                                                   target_ra_session,
                                                   ctx, scratch_pool));

      svn_hash_sets(target_history_hash, repos_relpath,
                    target_history_as_mergeinfo);
    }

  /* Check that SOURCE_LOC and TARGET->loc are
     actually related, we can't reintegrate if they are not.  Also
     get an initial value for the YCA revision number. */
  SVN_ERR(svn_client__get_youngest_common_ancestor(
              &yc_ancestor, source_loc, &target->loc, target_ra_session, ctx,
              iterpool, iterpool));
  if (! yc_ancestor)
    return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                             _("'%s@%ld' must be ancestrally related to "
                               "'%s@%ld'"), source_loc->url, source_loc->rev,
                             target->loc.url, target->loc.rev);

  /* If the source revision is the same as the youngest common
     revision, then there can't possibly be any unmerged revisions
     that we need to apply to target. */
  if (source_loc->rev == yc_ancestor->rev)
    {
      svn_pool_destroy(iterpool);
      return SVN_NO_ERROR;
    }

  /* Get the mergeinfo from the source, including its descendants
     with differing explicit mergeinfo. */
  SVN_ERR(svn_client__get_repos_mergeinfo_catalog(
            &mergeinfo_catalog, source_ra_session,
            source_loc->url, source_loc->rev,
            svn_mergeinfo_inherited, FALSE /* squelch_incapable */,
            TRUE /* include_descendants */, iterpool, iterpool));

  if (!mergeinfo_catalog)
    mergeinfo_catalog = apr_hash_make(iterpool);

  *merged_to_source_catalog = svn_mergeinfo_catalog_dup(mergeinfo_catalog,
                                                        result_pool);

  /* Filter the source's mergeinfo catalog so that we are left with
     mergeinfo that describes what has *not* previously been merged from
     TARGET->loc to SOURCE_LOC. */
  SVN_ERR(find_unmerged_mergeinfo(&unmerged_catalog,
                                  &youngest_merged_rev,
                                  yc_ancestor->rev,
                                  mergeinfo_catalog,
                                  target_history_hash,
                                  source_loc,
                                  target,
                                  source_ra_session,
                                  target_ra_session,
                                  ctx,
                                  iterpool, iterpool));

  /* Simplify unmerged_catalog through elision then make a copy in POOL. */
  SVN_ERR(svn_client__elide_mergeinfo_catalog(unmerged_catalog,
                                              iterpool));
  *unmerged_to_source_catalog = svn_mergeinfo_catalog_dup(unmerged_catalog,
                                                          result_pool);

  if (youngest_merged_rev == SVN_INVALID_REVNUM)
    {
      /* We never merged to the source.  Just return the branch point. */
      *left_p = svn_client__pathrev_dup(yc_ancestor, result_pool);
    }
  else
    {
      /* We've previously merged some or all of the target, up to
         youngest_merged_rev, to the source.  Set
         *LEFT_P to cover the youngest part of this range. */
      SVN_ERR(svn_client__repos_location(left_p, target_ra_session,
                                         &target->loc, youngest_merged_rev,
                                         ctx, result_pool, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}

/* Determine the URLs and revisions needed to perform a reintegrate merge
 * from SOURCE_LOC into the working copy at TARGET.
 *
 * SOURCE_RA_SESSION and TARGET_RA_SESSION are RA sessions opened to the
 * URLs of SOURCE_LOC and TARGET->loc respectively.
 *
 * Set *SOURCE_P to
 * the source-left and source-right locations of the required merge.  Set
 * *YC_ANCESTOR_P to the location of the youngest ancestor.
 * Any of these output pointers may be NULL if not wanted.
 *
 * See svn_client_find_reintegrate_merge() for other details.
 */
static svn_error_t *
find_reintegrate_merge(merge_source_t **source_p,
                       svn_client__pathrev_t **yc_ancestor_p,
                       svn_ra_session_t *source_ra_session,
                       const svn_client__pathrev_t *source_loc,
                       svn_ra_session_t *target_ra_session,
                       const merge_target_t *target,
                       svn_client_ctx_t *ctx,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_client__pathrev_t *yc_ancestor;
  svn_client__pathrev_t *loc1;
  merge_source_t source;
  svn_mergeinfo_catalog_t unmerged_to_source_mergeinfo_catalog;
  svn_mergeinfo_catalog_t merged_to_source_mergeinfo_catalog;
  svn_error_t *err;
  apr_hash_t *subtrees_with_mergeinfo;

  assert(session_url_is(source_ra_session, source_loc->url, scratch_pool));
  assert(session_url_is(target_ra_session, target->loc.url, scratch_pool));

  /* As the WC tree is "pure", use its last-updated-to revision as
     the default revision for the left side of our merge, since that's
     what the repository sub-tree is required to be up to date with
     (with regard to the WC). */
  /* ### Bogus/obsolete comment? */

  /* Can't reintegrate to or from the root of the repository. */
  if (strcmp(source_loc->url, source_loc->repos_root_url) == 0
      || strcmp(target->loc.url, target->loc.repos_root_url) == 0)
    return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                             _("Neither the reintegrate source nor target "
                               "can be the root of the repository"));

  /* Find all the subtrees in TARGET_WCPATH that have explicit mergeinfo. */
  err = get_wc_explicit_mergeinfo_catalog(&subtrees_with_mergeinfo,
                                          target->abspath, svn_depth_infinity,
                                          ctx, scratch_pool, scratch_pool);
  /* Issue #3896: If invalid mergeinfo in the reintegrate target
     prevents us from proceeding, then raise the best error possible. */
  if (err && err->apr_err == SVN_ERR_CLIENT_INVALID_MERGEINFO_NO_MERGETRACKING)
    err = svn_error_quick_wrap(err, _("Reintegrate merge not possible"));
  SVN_ERR(err);

  SVN_ERR(calculate_left_hand_side(&loc1,
                                   &merged_to_source_mergeinfo_catalog,
                                   &unmerged_to_source_mergeinfo_catalog,
                                   target,
                                   subtrees_with_mergeinfo,
                                   source_loc,
                                   source_ra_session,
                                   target_ra_session,
                                   ctx,
                                   scratch_pool, scratch_pool));

  /* Did calculate_left_hand_side() decide that there was no merge to
     be performed here?  */
  if (! loc1)
    {
      if (source_p)
        *source_p = NULL;
      if (yc_ancestor_p)
        *yc_ancestor_p = NULL;
      return SVN_NO_ERROR;
    }

  source.loc1 = loc1;
  source.loc2 = source_loc;

  /* If the target was moved after the source was branched from it,
     it is possible that the left URL differs from the target's current
     URL.  If so, then adjust TARGET_RA_SESSION to point to the old URL. */
  if (strcmp(source.loc1->url, target->loc.url))
    SVN_ERR(svn_ra_reparent(target_ra_session, source.loc1->url, scratch_pool));

  SVN_ERR(svn_client__get_youngest_common_ancestor(
            &yc_ancestor, source.loc2, source.loc1, target_ra_session,
            ctx, scratch_pool, scratch_pool));

  if (! yc_ancestor)
    return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                             _("'%s@%ld' must be ancestrally related to "
                               "'%s@%ld'"),
                             source.loc1->url, source.loc1->rev,
                             source.loc2->url, source.loc2->rev);

  /* The source side of a reintegrate merge is not 'ancestral', except in
   * the degenerate case where source == YCA. */
  source.ancestral = (loc1->rev == yc_ancestor->rev);

  if (source.loc1->rev > yc_ancestor->rev)
    {
      /* Have we actually merged anything to the source from the
         target?  If so, make sure we've merged a contiguous
         prefix. */
      svn_mergeinfo_catalog_t final_unmerged_catalog = apr_hash_make(scratch_pool);

      SVN_ERR(find_unsynced_ranges(source_loc, &target->loc,
                                   unmerged_to_source_mergeinfo_catalog,
                                   merged_to_source_mergeinfo_catalog,
                                   final_unmerged_catalog,
                                   target_ra_session, scratch_pool,
                                   scratch_pool));

      if (apr_hash_count(final_unmerged_catalog))
        {
          svn_string_t *source_mergeinfo_cat_string;

          SVN_ERR(svn_mergeinfo__catalog_to_formatted_string(
            &source_mergeinfo_cat_string,
            final_unmerged_catalog,
            "  ", _("    Missing ranges: "), scratch_pool));
          return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE,
                                   NULL,
                                   _("Reintegrate can only be used if "
                                     "revisions %ld through %ld were "
                                     "previously merged from %s to the "
                                     "reintegrate source, but this is "
                                     "not the case:\n%s"),
                                   yc_ancestor->rev + 1, source.loc2->rev,
                                   target->loc.url,
                                   source_mergeinfo_cat_string->data);
        }
    }

  /* Left side: trunk@youngest-trunk-rev-merged-to-branch-at-specified-peg-rev
   * Right side: branch@specified-peg-revision */
  if (source_p)
    *source_p = merge_source_dup(&source, result_pool);

  if (yc_ancestor_p)
    *yc_ancestor_p = svn_client__pathrev_dup(yc_ancestor, result_pool);
  return SVN_NO_ERROR;
}

/* Resolve the source and target locations and open RA sessions to them, and
 * perform some checks appropriate for a reintegrate merge.
 *
 * Set *SOURCE_RA_SESSION_P and *SOURCE_LOC_P to a new session and the
 * repository location of SOURCE_PATH_OR_URL at SOURCE_PEG_REVISION.  Set
 * *TARGET_RA_SESSION_P and *TARGET_P to a new session and the repository
 * location of the WC at TARGET_ABSPATH.
 *
 * Throw a SVN_ERR_CLIENT_UNRELATED_RESOURCES error if the target WC node is
 * a locally added node or if the source and target are not in the same
 * repository.  Throw a SVN_ERR_CLIENT_NOT_READY_TO_MERGE error if the
 * target WC is not at a single revision without switched subtrees and
 * without local mods.
 *
 * Allocate all the outputs in RESULT_POOL.
 */
static svn_error_t *
open_reintegrate_source_and_target(svn_ra_session_t **source_ra_session_p,
                                   svn_client__pathrev_t **source_loc_p,
                                   svn_ra_session_t **target_ra_session_p,
                                   merge_target_t **target_p,
                                   const char *source_path_or_url,
                                   const svn_opt_revision_t *source_peg_revision,
                                   const char *target_abspath,
                                   svn_client_ctx_t *ctx,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_client__pathrev_t *source_loc;
  merge_target_t *target;

  /* Open the target WC.  A reintegrate merge requires the merge target to
   * reflect a subtree of the repository as found at a single revision. */
  SVN_ERR(open_target_wc(&target, target_abspath,
                         FALSE, FALSE, FALSE,
                         ctx, scratch_pool, scratch_pool));
  if (! target->loc.url)
    return svn_error_createf(SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
                             _("Can't reintegrate into '%s' because it is "
                               "locally added and therefore not related to "
                               "the merge source"),
                             svn_dirent_local_style(target->abspath,
                                                    scratch_pool));

  SVN_ERR(svn_client_open_ra_session2(target_ra_session_p,
                                      target->loc.url, target->abspath,
                                      ctx, result_pool, scratch_pool));

  SVN_ERR(svn_client__ra_session_from_path2(
            source_ra_session_p, &source_loc,
            source_path_or_url, NULL, source_peg_revision, source_peg_revision,
            ctx, result_pool));

  /* source_loc and target->loc are required to be in the same repository,
     as mergeinfo doesn't come into play for cross-repository merging. */
  SVN_ERR(check_same_repos(source_loc,
                           svn_dirent_local_style(source_path_or_url,
                                                  scratch_pool),
                           &target->loc,
                           svn_dirent_local_style(target->abspath,
                                                  scratch_pool),
                           TRUE /* strict_urls */, scratch_pool));

  *source_loc_p = source_loc;
  *target_p = target;
  return SVN_NO_ERROR;
}

/* The body of svn_client_merge_reintegrate(), which see for details. */
static svn_error_t *
merge_reintegrate_locked(svn_client__conflict_report_t **conflict_report,
                         const char *source_path_or_url,
                         const svn_opt_revision_t *source_peg_revision,
                         const char *target_abspath,
                         svn_boolean_t diff_ignore_ancestry,
                         svn_boolean_t dry_run,
                         const apr_array_header_t *merge_options,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_ra_session_t *target_ra_session, *source_ra_session;
  merge_target_t *target;
  svn_client__pathrev_t *source_loc;
  merge_source_t *source;
  svn_client__pathrev_t *yc_ancestor;
  svn_boolean_t use_sleep = FALSE;
  svn_error_t *err;

  SVN_ERR(open_reintegrate_source_and_target(
            &source_ra_session, &source_loc, &target_ra_session, &target,
            source_path_or_url, source_peg_revision, target_abspath,
            ctx, scratch_pool, scratch_pool));

  SVN_ERR(find_reintegrate_merge(&source, &yc_ancestor,
                                 source_ra_session, source_loc,
                                 target_ra_session, target,
                                 ctx, scratch_pool, scratch_pool));

  if (! source)
    {
      *conflict_report = NULL;
      return SVN_NO_ERROR;
    }

  /* Do the real merge! */
  /* ### TODO(reint): Make sure that one isn't the same line ancestor
     ### of the other (what's erroneously referred to as "ancestrally
     ### related" in this source file).  For now, we just say the source
     ### isn't "ancestral" even if it is (in the degenerate case where
     ### source-left equals YCA). */
  source->ancestral = FALSE;
  err = merge_cousins_and_supplement_mergeinfo(conflict_report,
                                               &use_sleep,
                                               target,
                                               target_ra_session,
                                               source_ra_session,
                                               source, yc_ancestor,
                                               TRUE /* same_repos */,
                                               svn_depth_infinity,
                                               diff_ignore_ancestry,
                                               FALSE /* force_delete */,
                                               FALSE /* record_only */,
                                               dry_run,
                                               merge_options,
                                               ctx,
                                               result_pool, scratch_pool);

  if (use_sleep)
    svn_io_sleep_for_timestamps(target_abspath, scratch_pool);

  SVN_ERR(err);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_merge_reintegrate(const char *source_path_or_url,
                             const svn_opt_revision_t *source_peg_revision,
                             const char *target_wcpath,
                             svn_boolean_t dry_run,
                             const apr_array_header_t *merge_options,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool)
{
  const char *target_abspath, *lock_abspath;
  svn_client__conflict_report_t *conflict_report;

  SVN_ERR(get_target_and_lock_abspath(&target_abspath, &lock_abspath,
                                      target_wcpath, ctx, pool));

  if (!dry_run)
    SVN_WC__CALL_WITH_WRITE_LOCK(
      merge_reintegrate_locked(&conflict_report,
                               source_path_or_url, source_peg_revision,
                               target_abspath,
                               FALSE /*diff_ignore_ancestry*/,
                               dry_run, merge_options, ctx, pool, pool),
      ctx->wc_ctx, lock_abspath, FALSE /* lock_anchor */, pool);
  else
    SVN_ERR(merge_reintegrate_locked(&conflict_report,
                                     source_path_or_url, source_peg_revision,
                                     target_abspath,
                                     FALSE /*diff_ignore_ancestry*/,
                                     dry_run, merge_options, ctx, pool, pool));

  SVN_ERR(svn_client__make_merge_conflict_error(conflict_report, pool));
  return SVN_NO_ERROR;
}


/* The body of svn_client_merge_peg5(), which see for details.
 *
 * IGNORE_MERGEINFO and DIFF_IGNORE_ANCESTRY are as in do_merge().
 */
static svn_error_t *
merge_peg_locked(svn_client__conflict_report_t **conflict_report,
                 const char *source_path_or_url,
                 const svn_opt_revision_t *source_peg_revision,
                 const svn_rangelist_t *ranges_to_merge,
                 const char *target_abspath,
                 svn_depth_t depth,
                 svn_boolean_t ignore_mergeinfo,
                 svn_boolean_t diff_ignore_ancestry,
                 svn_boolean_t force_delete,
                 svn_boolean_t record_only,
                 svn_boolean_t dry_run,
                 svn_boolean_t allow_mixed_rev,
                 const apr_array_header_t *merge_options,
                 svn_client_ctx_t *ctx,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  merge_target_t *target;
  svn_client__pathrev_t *source_loc;
  apr_array_header_t *merge_sources;
  svn_ra_session_t *ra_session;
  apr_pool_t *sesspool;
  svn_boolean_t use_sleep = FALSE;
  svn_error_t *err;
  svn_boolean_t same_repos;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(target_abspath));

  SVN_ERR(open_target_wc(&target, target_abspath,
                         allow_mixed_rev, TRUE, TRUE,
                         ctx, scratch_pool, scratch_pool));

  /* Create a short lived session pool */
  sesspool = svn_pool_create(scratch_pool);

  /* Open an RA session to our source URL, and determine its root URL. */
  SVN_ERR(svn_client__ra_session_from_path2(
            &ra_session, &source_loc,
            source_path_or_url, NULL, source_peg_revision, source_peg_revision,
            ctx, sesspool));

  /* Normalize our merge sources. */
  SVN_ERR(normalize_merge_sources(&merge_sources, source_path_or_url,
                                  source_loc,
                                  ranges_to_merge, ra_session, ctx,
                                  scratch_pool, scratch_pool));

  /* Check for same_repos. */
  same_repos = is_same_repos(&target->loc, source_loc, TRUE /* strict_urls */);

  /* Do the real merge!  (We say with confidence that our merge
     sources are both ancestral and related.) */
  if (getenv("SVN_ELEMENT_MERGE")
      && same_repos
      && (depth == svn_depth_infinity || depth == svn_depth_unknown)
      && ignore_mergeinfo
      && !record_only)
    {
      err = svn_client__merge_elements(&use_sleep,
                                       merge_sources, target, ra_session,
                                       diff_ignore_ancestry, force_delete,
                                       dry_run, merge_options,
                                       ctx, result_pool, scratch_pool);
      /* ### Currently this merge just errors out on any conflicts */
      *conflict_report = NULL;
    }
  else
  err = do_merge(NULL, NULL, conflict_report, &use_sleep,
                 merge_sources, target, ra_session,
                 TRUE /*sources_related*/, same_repos, ignore_mergeinfo,
                 diff_ignore_ancestry, force_delete, dry_run,
                 record_only, NULL, FALSE, FALSE, depth, merge_options,
                 ctx, result_pool, scratch_pool);

  /* We're done with our RA session. */
  svn_pool_destroy(sesspool);

  if (use_sleep)
    svn_io_sleep_for_timestamps(target_abspath, scratch_pool);

  SVN_ERR(err);
  return SVN_NO_ERROR;
}

/* Details of an automatic merge. */
typedef struct automatic_merge_t
{
  svn_client__pathrev_t *yca, *base, *right, *target;
  svn_boolean_t is_reintegrate_like;
  svn_boolean_t allow_mixed_rev, allow_local_mods, allow_switched_subtrees;
} automatic_merge_t;

static svn_error_t *
client_find_automatic_merge(automatic_merge_t **merge_p,
                            const char *source_path_or_url,
                            const svn_opt_revision_t *source_revision,
                            const char *target_abspath,
                            svn_boolean_t allow_mixed_rev,
                            svn_boolean_t allow_local_mods,
                            svn_boolean_t allow_switched_subtrees,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

static svn_error_t *
do_automatic_merge_locked(svn_client__conflict_report_t **conflict_report,
                          const automatic_merge_t *merge,
                          const char *target_abspath,
                          svn_depth_t depth,
                          svn_boolean_t diff_ignore_ancestry,
                          svn_boolean_t force_delete,
                          svn_boolean_t record_only,
                          svn_boolean_t dry_run,
                          const apr_array_header_t *merge_options,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

svn_error_t *
svn_client_merge_peg5(const char *source_path_or_url,
                      const apr_array_header_t *ranges_to_merge,
                      const svn_opt_revision_t *source_peg_revision,
                      const char *target_wcpath,
                      svn_depth_t depth,
                      svn_boolean_t ignore_mergeinfo,
                      svn_boolean_t diff_ignore_ancestry,
                      svn_boolean_t force_delete,
                      svn_boolean_t record_only,
                      svn_boolean_t dry_run,
                      svn_boolean_t allow_mixed_rev,
                      const apr_array_header_t *merge_options,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool)
{
  const char *target_abspath, *lock_abspath;
  svn_client__conflict_report_t *conflict_report;

  /* No ranges to merge?  No problem. */
  if (ranges_to_merge != NULL && ranges_to_merge->nelts == 0)
    return SVN_NO_ERROR;

  SVN_ERR(get_target_and_lock_abspath(&target_abspath, &lock_abspath,
                                      target_wcpath, ctx, pool));

  /* Do an automatic merge if no revision ranges are specified. */
  if (ranges_to_merge == NULL)
    {
      automatic_merge_t *merge;

      if (ignore_mergeinfo)
        return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                _("Cannot merge automatically while "
                                  "ignoring mergeinfo"));

      /* Find the details of the merge needed. */
      SVN_ERR(client_find_automatic_merge(
                                    &merge,
                                    source_path_or_url, source_peg_revision,
                                    target_abspath,
                                    allow_mixed_rev,
                                    TRUE /*allow_local_mods*/,
                                    TRUE /*allow_switched_subtrees*/,
                                    ctx, pool, pool));

      if (!dry_run)
        SVN_WC__CALL_WITH_WRITE_LOCK(
          do_automatic_merge_locked(&conflict_report,
                                    merge,
                                    target_abspath, depth,
                                    diff_ignore_ancestry,
                                    force_delete, record_only, dry_run,
                                    merge_options, ctx, pool, pool),
          ctx->wc_ctx, lock_abspath, FALSE /* lock_anchor */, pool);
      else
        SVN_ERR(do_automatic_merge_locked(&conflict_report,
                                    merge,
                                    target_abspath, depth,
                                    diff_ignore_ancestry,
                                    force_delete, record_only, dry_run,
                                    merge_options, ctx, pool, pool));
    }
  else if (!dry_run)
    SVN_WC__CALL_WITH_WRITE_LOCK(
      merge_peg_locked(&conflict_report,
                       source_path_or_url, source_peg_revision,
                       ranges_to_merge,
                       target_abspath, depth, ignore_mergeinfo,
                       diff_ignore_ancestry,
                       force_delete, record_only, dry_run,
                       allow_mixed_rev, merge_options, ctx, pool, pool),
      ctx->wc_ctx, lock_abspath, FALSE /* lock_anchor */, pool);
  else
    SVN_ERR(merge_peg_locked(&conflict_report,
                       source_path_or_url, source_peg_revision,
                       ranges_to_merge,
                       target_abspath, depth, ignore_mergeinfo,
                       diff_ignore_ancestry,
                       force_delete, record_only, dry_run,
                       allow_mixed_rev, merge_options, ctx, pool, pool));

  SVN_ERR(svn_client__make_merge_conflict_error(conflict_report, pool));
  return SVN_NO_ERROR;
}


/* The location-history of a branch.
 *
 * This structure holds the set of path-revisions occupied by a branch,
 * from an externally chosen 'tip' location back to its origin.  The
 * 'tip' location is the youngest location that we are considering on
 * the branch. */
typedef struct branch_history_t
{
  /* The tip location of the branch.  That is, the youngest location that's
   * in the repository and that we're considering.  If we're considering a
   * target branch right up to an uncommitted WC, then this is the WC base
   * (pristine) location. */
  svn_client__pathrev_t *tip;
  /* The location-segment history, as mergeinfo. */
  svn_mergeinfo_t history;
  /* Whether the location-segment history reached as far as (necessarily
     the root path in) revision 0 -- a fact that can't be represented as
     mergeinfo. */
  svn_boolean_t has_r0_history;
} branch_history_t;

/* Return the location on BRANCH_HISTORY at revision REV, or NULL if none. */
static svn_client__pathrev_t *
location_on_branch_at_rev(const branch_history_t *branch_history,
                          svn_revnum_t rev,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, branch_history->history); hi;
       hi = apr_hash_next(hi))
    {
      const char *fspath = apr_hash_this_key(hi);
      svn_rangelist_t *rangelist = apr_hash_this_val(hi);
      int i;

      for (i = 0; i < rangelist->nelts; i++)
        {
          svn_merge_range_t *r = APR_ARRAY_IDX(rangelist, i, svn_merge_range_t *);
          if (r->start < rev && rev <= r->end)
            {
              return svn_client__pathrev_create_with_relpath(
                       branch_history->tip->repos_root_url,
                       branch_history->tip->repos_uuid,
                       rev, fspath + 1, result_pool);
            }
        }
    }
  return NULL;
}

/* */
typedef struct source_and_target_t
{
  svn_client__pathrev_t *source;
  svn_ra_session_t *source_ra_session;
  branch_history_t source_branch;

  merge_target_t *target;
  svn_ra_session_t *target_ra_session;
  branch_history_t target_branch;

  /* Repos location of the youngest common ancestor of SOURCE and TARGET. */
  svn_client__pathrev_t *yca;
} source_and_target_t;

/* Set *INTERSECTION_P to the intersection of BRANCH_HISTORY with the
 * revision range OLDEST_REV to YOUNGEST_REV (inclusive).
 *
 * If the intersection is empty, the result will be a branch history object
 * containing an empty (not null) history.
 *
 * ### The 'tip' of the result is currently unchanged.
 */
static svn_error_t *
branch_history_intersect_range(branch_history_t **intersection_p,
                               const branch_history_t *branch_history,
                               svn_revnum_t oldest_rev,
                               svn_revnum_t youngest_rev,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  branch_history_t *result = apr_palloc(result_pool, sizeof(*result));

  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(oldest_rev));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(youngest_rev));
  SVN_ERR_ASSERT(oldest_rev >= 1);
  /* Allow a just-empty range (oldest = youngest + 1) but not an
   * arbitrary reverse range (such as oldest = youngest + 2). */
  SVN_ERR_ASSERT(oldest_rev <= youngest_rev + 1);

  if (oldest_rev <= youngest_rev)
    {
      SVN_ERR(svn_mergeinfo__filter_mergeinfo_by_ranges(
                &result->history, branch_history->history,
                youngest_rev, oldest_rev - 1, TRUE /* include_range */,
                result_pool, scratch_pool));
      result->history = svn_mergeinfo_dup(result->history, result_pool);
    }
  else
    {
      result->history = apr_hash_make(result_pool);
    }
  result->has_r0_history = FALSE;

  /* ### TODO: Set RESULT->tip to the tip of the intersection. */
  result->tip = svn_client__pathrev_dup(branch_history->tip, result_pool);

  *intersection_p = result;
  return SVN_NO_ERROR;
}

/* Set *OLDEST_P and *YOUNGEST_P to the oldest and youngest locations
 * (inclusive) along BRANCH.  OLDEST_P and/or YOUNGEST_P may be NULL if not
 * wanted.
 */
static svn_error_t *
branch_history_get_endpoints(svn_client__pathrev_t **oldest_p,
                             svn_client__pathrev_t **youngest_p,
                             const branch_history_t *branch,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_revnum_t youngest_rev, oldest_rev;

  SVN_ERR(svn_mergeinfo__get_range_endpoints(
            &youngest_rev, &oldest_rev,
            branch->history, scratch_pool));
  if (oldest_p)
    *oldest_p = location_on_branch_at_rev(
                  branch, oldest_rev + 1, result_pool, scratch_pool);
  if (youngest_p)
    *youngest_p = location_on_branch_at_rev(
                    branch, youngest_rev, result_pool, scratch_pool);
  return SVN_NO_ERROR;
}

/* Implements the svn_log_entry_receiver_t interface.

  Set *BATON to LOG_ENTRY->revision and return SVN_ERR_CEASE_INVOCATION. */
static svn_error_t *
operative_rev_receiver(void *baton,
                       svn_log_entry_t *log_entry,
                       apr_pool_t *pool)
{
  svn_revnum_t *operative_rev = baton;

  *operative_rev = log_entry->revision;

  /* We've found the youngest merged or oldest eligible revision, so
     we're done...

     ...but wait, shouldn't we care if LOG_ENTRY->NON_INHERITABLE is
     true?  Because if it is, then LOG_ENTRY->REVISION is only
     partially merged/elgibile!  And our only caller,
     find_last_merged_location (via short_circuit_mergeinfo_log) is
     interested in *fully* merged revisions.  That's all true, but if
     find_last_merged_location() finds the youngest merged revision it
     will also check for the oldest eligible revision.  So in the case
     the youngest merged rev is non-inheritable, the *same* non-inheritable
     rev will be found as the oldest eligible rev -- and
     find_last_merged_location() handles that situation. */
  return svn_error_create(SVN_ERR_CEASE_INVOCATION, NULL, NULL);
}

/* Wrapper around svn_client__mergeinfo_log. All arguments are as per
   that private API.  The discover_changed_paths, depth, and revprops args to
   svn_client__mergeinfo_log are always TRUE, svn_depth_infinity_t,
   and empty array respectively.

   If RECEIVER raises a SVN_ERR_CEASE_INVOCATION error, but still sets
   *REVISION to a valid revnum, then clear the error.  Otherwise return
   any error. */
static svn_error_t*
short_circuit_mergeinfo_log(svn_mergeinfo_catalog_t *target_mergeinfo_cat,
                            svn_boolean_t finding_merged,
                            const char *target_path_or_url,
                            const svn_opt_revision_t *target_peg_revision,
                            const char *source_path_or_url,
                            const svn_opt_revision_t *source_peg_revision,
                            const svn_opt_revision_t *source_start_revision,
                            const svn_opt_revision_t *source_end_revision,
                            svn_log_entry_receiver_t receiver,
                            svn_revnum_t *revision,
                            svn_client_ctx_t *ctx,
                            svn_ra_session_t *ra_session,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  apr_array_header_t *revprops;
  svn_error_t *err;
  const char *session_url;

  SVN_ERR(svn_ra_get_session_url(ra_session, &session_url, scratch_pool));

  revprops = apr_array_make(scratch_pool, 0, sizeof(const char *));
  err = svn_client__mergeinfo_log(finding_merged,
                                  target_path_or_url,
                                  target_peg_revision,
                                  target_mergeinfo_cat,
                                  source_path_or_url,
                                  source_peg_revision,
                                  source_start_revision,
                                  source_end_revision,
                                  receiver, revision,
                                  TRUE, svn_depth_infinity,
                                  revprops, ctx, ra_session,
                                  result_pool, scratch_pool);

  err = svn_error_compose_create(
                  err,
                  svn_ra_reparent(ra_session, session_url, scratch_pool));

  if (err)
    {
      /* We expect RECEIVER to short-circuit the (potentially expensive) log
         by raising an SVN_ERR_CEASE_INVOCATION -- see operative_rev_receiver.
         So we can ignore that error, but only as long as we actually found a
         valid revision. */
      if (SVN_IS_VALID_REVNUM(*revision)
          && err->apr_err == SVN_ERR_CEASE_INVOCATION)
        {
          svn_error_clear(err);
          err = NULL;
        }
      else
        {
          return svn_error_trace(err);
        }
    }
  return SVN_NO_ERROR;
}

/* Set *BASE_P to the last location on SOURCE_BRANCH such that all changes
 * on SOURCE_BRANCH after YCA up to and including *BASE_P have already
 * been fully merged into TARGET.
 *
 *               *BASE_P       TIP
 *          o-------o-----------o--- SOURCE_BRANCH
 *         /         \
 *   -----o     prev. \
 *     YCA \    merges \
 *          o-----------o----------- TARGET branch
 *
 * In terms of mergeinfo:
 *
 *     Source     a--...                     o=change, -=no-op revision
 *       branch  /   \
 *     YCA -->  o     a---o---o---o---o---   d=delete, a=add-as-a-copy
 *
 *     Eligible -.eee.eeeeeeeeeeeeeeeeeeee   .=not a source branch location
 *
 *     Tgt-mi   -.mmm.mm-mm-------m-------   m=merged to root of TARGET or
 *                                           subtree of TARGET with no
 *                                           operative changes outside of that
 *                                           subtree, -=not merged
 *
 *     Eligible -.---.--e--eeeeeee-eeeeeee
 *
 *     Next     --------^-----------------   BASE is just before here.
 *
 *             /         \
 *       -----o     prev. \
 *         YCA \    merges \
 *              o-----------o-------------
 *
 * If no revisions from SOURCE_BRANCH have been completely merged to TARGET,
 * then set *BASE_P to the YCA.
 */
static svn_error_t *
find_last_merged_location(svn_client__pathrev_t **base_p,
                          svn_client__pathrev_t *yca,
                          const branch_history_t *source_branch,
                          svn_client__pathrev_t *target,
                          svn_client_ctx_t *ctx,
                          svn_ra_session_t *ra_session,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_opt_revision_t source_peg_rev, source_start_rev, source_end_rev,
    target_opt_rev;
  svn_revnum_t youngest_merged_rev = SVN_INVALID_REVNUM;
  svn_mergeinfo_catalog_t target_mergeinfo_cat = NULL;

  /* Using a local subpool for 'target_mergeinfo_cat' can make a big
     reduction in overall memory usage. */
  apr_pool_t *tmic_pool = svn_pool_create(scratch_pool);

  source_peg_rev.kind = svn_opt_revision_number;
  source_peg_rev.value.number = source_branch->tip->rev;
  source_start_rev.kind = svn_opt_revision_number;
  source_start_rev.value.number = yca->rev;
  source_end_rev.kind = svn_opt_revision_number;
  source_end_rev.value.number = source_branch->tip->rev;
  target_opt_rev.kind = svn_opt_revision_number;
  target_opt_rev.value.number = target->rev;

  /* Find the youngest revision fully merged from SOURCE_BRANCH to TARGET,
     if such a revision exists. */
  SVN_ERR(short_circuit_mergeinfo_log(&target_mergeinfo_cat,
                                      TRUE, /* Find merged */
                                      target->url, &target_opt_rev,
                                      source_branch->tip->url,
                                      &source_peg_rev,
                                      &source_end_rev, &source_start_rev,
                                      operative_rev_receiver,
                                      &youngest_merged_rev,
                                      ctx, ra_session,
                                      tmic_pool, tmic_pool));

  if (!SVN_IS_VALID_REVNUM(youngest_merged_rev))
    {
      /* No revisions have been completely merged from SOURCE_BRANCH to
         TARGET so the base for the next merge is the YCA. */
      *base_p = yca;
    }
  else
    {
      /* One or more revisions have already been completely merged from
         SOURCE_BRANCH to TARGET, now find the oldest revision, older
         than the youngest merged revision, which is still eligible to
         be merged, if such exists. */
      branch_history_t *contiguous_source;
      svn_revnum_t base_rev;
      svn_revnum_t oldest_eligible_rev = SVN_INVALID_REVNUM;

      /* If the only revisions eligible are younger than the youngest merged
         revision we can simply assume that the youngest eligible revision
         is the youngest merged revision.  Obviously this may not be true!
         The revisions between the youngest merged revision and the tip of
         the branch may have several inoperative revisions -- they may *all*
         be inoperative revisions!  But for the purpose of this function
         (i.e. finding the youngest revision after the YCA where all revs have
         been merged) that doesn't matter. */
      source_end_rev.value.number = youngest_merged_rev;
      SVN_ERR(short_circuit_mergeinfo_log(&target_mergeinfo_cat,
                                          FALSE, /* Find eligible */
                                          target->url, &target_opt_rev,
                                          source_branch->tip->url,
                                          &source_peg_rev,
                                          &source_start_rev, &source_end_rev,
                                          operative_rev_receiver,
                                          &oldest_eligible_rev,
                                          ctx, ra_session,
                                          tmic_pool, tmic_pool));

      /* If there are revisions eligible for merging, use the oldest one
         to calculate the base.  Otherwise there are no operative revisions
         to merge and we can simple set the base to the youngest revision
         already merged. */
      if (SVN_IS_VALID_REVNUM(oldest_eligible_rev))
        base_rev = oldest_eligible_rev - 1;
      else
        base_rev = youngest_merged_rev;

      /* Find the branch location just before the oldest eligible rev.
         (We can't just use the base revs calculated above because the branch
         might have a gap there.) */
      SVN_ERR(branch_history_intersect_range(&contiguous_source,
                                             source_branch, yca->rev,
                                             base_rev,
                                             scratch_pool, scratch_pool));
      SVN_ERR(branch_history_get_endpoints(NULL, base_p, contiguous_source,
                                           result_pool, scratch_pool));
    }

  svn_pool_destroy(tmic_pool);
  return SVN_NO_ERROR;
}

/* Find a merge base location on the target branch, like in a sync
 * merge.
 *
 *                BASE          S_T->source
 *          o-------o-----------o---
 *         /         \           \
 *   -----o     prev. \           \  this
 *     YCA \    merge  \           \ merge
 *          o-----------o-----------o
 *                                  S_T->target
 *
 * Set *BASE_P to BASE, the youngest location in the history of S_T->source
 * (at or after the YCA) at which all revisions up to BASE are effectively
 * merged into S_T->target.
 *
 * If no locations on the history of S_T->source are effectively merged to
 * S_T->target, set *BASE_P to the YCA.
 */
static svn_error_t *
find_base_on_source(svn_client__pathrev_t **base_p,
                    source_and_target_t *s_t,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  SVN_ERR(find_last_merged_location(base_p,
                                    s_t->yca,
                                    &s_t->source_branch,
                                    s_t->target_branch.tip,
                                    ctx,
                                    s_t->source_ra_session,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

/* Find a merge base location on the target branch, like in a reintegrate
 * merge.
 *
 *                              S_T->source
 *          o-----------o-------o---
 *         /    prev.  /         \
 *   -----o     merge /           \  this
 *     YCA \         /             \ merge
 *          o-------o---------------o
 *                BASE              S_T->target
 *
 * Set *BASE_P to BASE, the youngest location in the history of S_T->target
 * (at or after the YCA) at which all revisions up to BASE are effectively
 * merged into S_T->source.
 *
 * If no locations on the history of S_T->target are effectively merged to
 * S_T->source, set *BASE_P to the YCA.
 */
static svn_error_t *
find_base_on_target(svn_client__pathrev_t **base_p,
                    source_and_target_t *s_t,
                    svn_client_ctx_t *ctx,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  SVN_ERR(find_last_merged_location(base_p,
                                    s_t->yca,
                                    &s_t->target_branch,
                                    s_t->source,
                                    ctx,
                                    s_t->target_ra_session,
                                    result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Find the last point at which the branch at S_T->source was completely
 * merged to the branch at S_T->target or vice-versa.
 *
 * Fill in S_T->source_branch and S_T->target_branch and S_T->yca.
 * Set *BASE_P to the merge base.  Set *IS_REINTEGRATE_LIKE to true if
 * an automatic merge from source to target would be a reintegration
 * merge: that is, if the last automatic merge was in the opposite
 * direction; or to false otherwise.
 *
 * If there is no youngest common ancestor, throw an error.
 */
static svn_error_t *
find_automatic_merge(svn_client__pathrev_t **base_p,
                     svn_boolean_t *is_reintegrate_like,
                     source_and_target_t *s_t,
                     svn_client_ctx_t *ctx,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_client__pathrev_t *base_on_source, *base_on_target;

  /* Get the location-history of each branch. */
  s_t->source_branch.tip = s_t->source;
  SVN_ERR(svn_client__get_history_as_mergeinfo(
            &s_t->source_branch.history, &s_t->source_branch.has_r0_history,
            s_t->source, SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
            s_t->source_ra_session, ctx, scratch_pool));
  s_t->target_branch.tip = &s_t->target->loc;
  SVN_ERR(svn_client__get_history_as_mergeinfo(
            &s_t->target_branch.history, &s_t->target_branch.has_r0_history,
            &s_t->target->loc, SVN_INVALID_REVNUM, SVN_INVALID_REVNUM,
            s_t->target_ra_session, ctx, scratch_pool));

  SVN_ERR(svn_client__calc_youngest_common_ancestor(
            &s_t->yca, s_t->source, s_t->source_branch.history,
            s_t->source_branch.has_r0_history,
            &s_t->target->loc, s_t->target_branch.history,
            s_t->target_branch.has_r0_history,
            result_pool, scratch_pool));

  if (! s_t->yca)
    return svn_error_createf(SVN_ERR_CLIENT_NOT_READY_TO_MERGE, NULL,
                             _("'%s@%ld' must be ancestrally related to "
                               "'%s@%ld'"),
                             s_t->source->url, s_t->source->rev,
                             s_t->target->loc.url, s_t->target->loc.rev);

  /* Find the latest revision of A synced to B and the latest
   * revision of B synced to A.
   *
   *   base_on_source = youngest_complete_synced_point(source, target)
   *   base_on_target = youngest_complete_synced_point(target, source)
   */
  SVN_ERR(find_base_on_source(&base_on_source, s_t,
                              ctx, scratch_pool, scratch_pool));
  SVN_ERR(find_base_on_target(&base_on_target, s_t,
                              ctx, scratch_pool, scratch_pool));

  /* Choose a base. */
  if (base_on_source->rev >= base_on_target->rev)
    {
      *base_p = base_on_source;
      *is_reintegrate_like = FALSE;
    }
  else
    {
      *base_p = base_on_target;
      *is_reintegrate_like = TRUE;
    }

  return SVN_NO_ERROR;
}

/** Find out what kind of automatic merge would be needed, when the target
 * is only known as a repository location rather than a WC.
 *
 * Like find_automatic_merge() except that the target is
 * specified by @a target_path_or_url at @a target_revision, which must
 * refer to a repository location, instead of by a WC path argument.
 *
 * Set *MERGE_P to a new structure with all fields filled in except the
 * 'allow_*' flags.
 */
static svn_error_t *
find_automatic_merge_no_wc(automatic_merge_t **merge_p,
                           const char *source_path_or_url,
                           const svn_opt_revision_t *source_revision,
                           const char *target_path_or_url,
                           const svn_opt_revision_t *target_revision,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  source_and_target_t *s_t = apr_palloc(scratch_pool, sizeof(*s_t));
  svn_client__pathrev_t *target_loc;
  automatic_merge_t *merge = apr_palloc(result_pool, sizeof(*merge));

  /* Source */
  SVN_ERR(svn_client__ra_session_from_path2(
            &s_t->source_ra_session, &s_t->source,
            source_path_or_url, NULL, source_revision, source_revision,
            ctx, result_pool));

  /* Target */
  SVN_ERR(svn_client__ra_session_from_path2(
            &s_t->target_ra_session, &target_loc,
            target_path_or_url, NULL, target_revision, target_revision,
            ctx, result_pool));
  s_t->target = apr_palloc(scratch_pool, sizeof(*s_t->target));
  s_t->target->abspath = NULL;  /* indicate the target is not a WC */
  s_t->target->loc = *target_loc;

  SVN_ERR(find_automatic_merge(&merge->base, &merge->is_reintegrate_like, s_t,
                               ctx, result_pool, scratch_pool));

  merge->right = s_t->source;
  merge->target = &s_t->target->loc;
  merge->yca = s_t->yca;
  *merge_p = merge;

  return SVN_NO_ERROR;
}

/* Find the information needed to merge all unmerged changes from a source
 * branch into a target branch.
 *
 * Set @a *merge_p to the information needed to merge all unmerged changes
 * (up to @a source_revision) from the source branch @a source_path_or_url
 * at @a source_revision into the target WC at @a target_abspath.
 *
 * The flags @a allow_mixed_rev, @a allow_local_mods and
 * @a allow_switched_subtrees enable merging into a WC that is in any or all
 * of the states described by their names, but only if this function decides
 * that the merge will be in the same direction as the last automatic merge.
 * If, on the other hand, the last automatic merge was in the opposite
 * direction, then such states of the WC are not allowed regardless
 * of these flags.  This function merely records these flags in the
 * @a *merge_p structure; do_automatic_merge_locked() checks the WC
 * state for compliance.
 *
 * Allocate the @a *merge_p structure in @a result_pool.
 */
static svn_error_t *
client_find_automatic_merge(automatic_merge_t **merge_p,
                            const char *source_path_or_url,
                            const svn_opt_revision_t *source_revision,
                            const char *target_abspath,
                            svn_boolean_t allow_mixed_rev,
                            svn_boolean_t allow_local_mods,
                            svn_boolean_t allow_switched_subtrees,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  source_and_target_t *s_t = apr_palloc(result_pool, sizeof(*s_t));
  automatic_merge_t *merge = apr_palloc(result_pool, sizeof(*merge));

  SVN_ERR_ASSERT(svn_dirent_is_absolute(target_abspath));

  /* "Open" the target WC.  Check the target WC for mixed-rev, local mods and
   * switched subtrees yet to faster exit and notify user before contacting
   * with server.  After we find out what kind of merge is required, then if a
   * reintegrate-like merge is required we'll do the stricter checks, in
   * do_automatic_merge_locked(). */
  SVN_ERR(open_target_wc(&s_t->target, target_abspath,
                         allow_mixed_rev,
                         allow_local_mods,
                         allow_switched_subtrees,
                         ctx, result_pool, scratch_pool));

  if (!s_t->target->loc.url)
    return svn_error_createf(SVN_ERR_CLIENT_UNRELATED_RESOURCES, NULL,
                             _("Can't perform automatic merge into '%s' "
                               "because it is locally added and therefore "
                               "not related to the merge source"),
                             svn_dirent_local_style(target_abspath,
                                                    scratch_pool));

  /* Open RA sessions to the source and target trees. */
  SVN_ERR(svn_client_open_ra_session2(&s_t->target_ra_session,
                                      s_t->target->loc.url,
                                      s_t->target->abspath,
                                      ctx, result_pool, scratch_pool));
  SVN_ERR(svn_client__ra_session_from_path2(
            &s_t->source_ra_session, &s_t->source,
            source_path_or_url, NULL, source_revision, source_revision,
            ctx, result_pool));

  /* Check source is in same repos as target. */
  SVN_ERR(check_same_repos(s_t->source, source_path_or_url,
                           &s_t->target->loc, target_abspath,
                           TRUE /* strict_urls */, scratch_pool));

  SVN_ERR(find_automatic_merge(&merge->base, &merge->is_reintegrate_like, s_t,
                               ctx, result_pool, scratch_pool));
  merge->yca = s_t->yca;
  merge->right = s_t->source;
  merge->target = &s_t->target->loc;
  merge->allow_mixed_rev = allow_mixed_rev;
  merge->allow_local_mods = allow_local_mods;
  merge->allow_switched_subtrees = allow_switched_subtrees;

  *merge_p = merge;

  /* TODO: Close the source and target sessions here? */

  return SVN_NO_ERROR;
}

/* Perform an automatic merge, given the information in MERGE which
 * must have come from calling client_find_automatic_merge().
 *
 * Four locations are inputs: YCA, BASE, RIGHT, TARGET, as shown
 * depending on whether the base is on the source branch or the target
 * branch of this merge.
 *
 *                            RIGHT     (is_reintegrate_like)
 *          o-----------o-------o---
 *         /    prev.  /         \
 *   -----o     merge /           \  this
 *     YCA \         /             \ merge
 *          o-------o---------------o
 *                BASE            TARGET
 *
 * or
 *
 *                BASE        RIGHT      (! is_reintegrate_like)
 *          o-------o-----------o---
 *         /         \           \
 *   -----o     prev. \           \  this
 *     YCA \    merge  \           \ merge
 *          o-----------o-----------o
 *                                TARGET
 *
 * ### TODO: The reintegrate-like code path does not yet
 * eliminate already-cherry-picked revisions from the source.
 */
static svn_error_t *
do_automatic_merge_locked(svn_client__conflict_report_t **conflict_report,
                          const automatic_merge_t *merge,
                          const char *target_abspath,
                          svn_depth_t depth,
                          svn_boolean_t diff_ignore_ancestry,
                          svn_boolean_t force_delete,
                          svn_boolean_t record_only,
                          svn_boolean_t dry_run,
                          const apr_array_header_t *merge_options,
                          svn_client_ctx_t *ctx,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  merge_target_t *target;
  svn_boolean_t reintegrate_like = merge->is_reintegrate_like;
  svn_boolean_t use_sleep = FALSE;
  svn_error_t *err;

  SVN_ERR(open_target_wc(&target, target_abspath,
                         merge->allow_mixed_rev && ! reintegrate_like,
                         merge->allow_local_mods && ! reintegrate_like,
                         merge->allow_switched_subtrees && ! reintegrate_like,
                         ctx, scratch_pool, scratch_pool));

  if (reintegrate_like)
    {
      merge_source_t source;
      svn_ra_session_t *base_ra_session = NULL;
      svn_ra_session_t *right_ra_session = NULL;
      svn_ra_session_t *target_ra_session = NULL;

      if (record_only)
        return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                _("The required merge is reintegrate-like, "
                                  "and the record-only option "
                                  "cannot be used with this kind of merge"));

      if (depth != svn_depth_unknown)
        return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                _("The required merge is reintegrate-like, "
                                  "and the depth option "
                                  "cannot be used with this kind of merge"));

      if (force_delete)
        return svn_error_create(SVN_ERR_INCORRECT_PARAMS, NULL,
                                _("The required merge is reintegrate-like, "
                                  "and the force_delete option "
                                  "cannot be used with this kind of merge"));

      SVN_ERR(ensure_ra_session_url(&base_ra_session, merge->base->url,
                                    target->abspath, ctx, scratch_pool));
      SVN_ERR(ensure_ra_session_url(&right_ra_session, merge->right->url,
                                    target->abspath, ctx, scratch_pool));
      SVN_ERR(ensure_ra_session_url(&target_ra_session, target->loc.url,
                                    target->abspath, ctx, scratch_pool));

      /* Check for and reject any abnormalities -- such as revisions that
       * have not yet been merged in the opposite direction -- that a
       * 'reintegrate' merge would have rejected. */
      {
        merge_source_t *source2;

        SVN_ERR(find_reintegrate_merge(&source2, NULL,
                                       right_ra_session, merge->right,
                                       target_ra_session, target,
                                       ctx, scratch_pool, scratch_pool));
      }

      source.loc1 = merge->base;
      source.loc2 = merge->right;
      source.ancestral = ! merge->is_reintegrate_like;

      err = merge_cousins_and_supplement_mergeinfo(conflict_report,
                                                   &use_sleep,
                                                   target,
                                                   base_ra_session,
                                                   right_ra_session,
                                                   &source, merge->yca,
                                                   TRUE /* same_repos */,
                                                   depth,
                                                   FALSE /*diff_ignore_ancestry*/,
                                                   force_delete, record_only,
                                                   dry_run,
                                                   merge_options,
                                                   ctx,
                                                   result_pool, scratch_pool);
    }
  else /* ! merge->is_reintegrate_like */
    {
      /* Ignoring the base that we found, we pass the YCA instead and let
         do_merge() work out which subtrees need which revision ranges to
         be merged.  This enables do_merge() to fill in revision-range
         gaps that are older than the base that we calculated (which is
         for the root path of the merge).

         An improvement would be to change find_automatic_merge() to
         find the base for each sutree, and then here use the oldest base
         among all subtrees. */
      apr_array_header_t *merge_sources;
      svn_ra_session_t *ra_session = NULL;

      /* Normalize our merge sources, do_merge() requires this.  See the
         'MERGEINFO MERGE SOURCE NORMALIZATION' global comment. */
      SVN_ERR(ensure_ra_session_url(&ra_session, merge->right->url,
                                    target->abspath, ctx, scratch_pool));
      SVN_ERR(normalize_merge_sources_internal(
        &merge_sources, merge->right,
        svn_rangelist__initialize(merge->yca->rev, merge->right->rev, TRUE,
                                  scratch_pool),
        ra_session, ctx, scratch_pool, scratch_pool));

      err = do_merge(NULL, NULL, conflict_report, &use_sleep,
                     merge_sources, target, ra_session,
                     TRUE /*related*/, TRUE /*same_repos*/,
                     FALSE /*ignore_mergeinfo*/, diff_ignore_ancestry,
                     force_delete, dry_run,
                     record_only, NULL, FALSE, FALSE, depth, merge_options,
                     ctx, result_pool, scratch_pool);
    }

  if (use_sleep)
    svn_io_sleep_for_timestamps(target_abspath, scratch_pool);

  SVN_ERR(err);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_client_get_merging_summary(svn_boolean_t *needs_reintegration,
                               const char **yca_url, svn_revnum_t *yca_rev,
                               const char **base_url, svn_revnum_t *base_rev,
                               const char **right_url, svn_revnum_t *right_rev,
                               const char **target_url, svn_revnum_t *target_rev,
                               const char **repos_root_url,
                               const char *source_path_or_url,
                               const svn_opt_revision_t *source_revision,
                               const char *target_path_or_url,
                               const svn_opt_revision_t *target_revision,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_boolean_t target_is_wc;
  automatic_merge_t *merge;

  target_is_wc = (! svn_path_is_url(target_path_or_url))
                 && (target_revision->kind == svn_opt_revision_unspecified
                     || target_revision->kind == svn_opt_revision_working
                     || target_revision->kind == svn_opt_revision_base);
  if (target_is_wc)
    {
      const char *target_abspath;

      SVN_ERR(svn_dirent_get_absolute(&target_abspath, target_path_or_url,
                                      scratch_pool));
      SVN_ERR(client_find_automatic_merge(
                &merge,
                source_path_or_url, source_revision,
                target_abspath,
                TRUE, TRUE, TRUE,  /* allow_* */
                ctx, scratch_pool, scratch_pool));
    }
  else
    SVN_ERR(find_automatic_merge_no_wc(
              &merge,
              source_path_or_url, source_revision,
              target_path_or_url, target_revision,
              ctx, scratch_pool, scratch_pool));

  if (needs_reintegration)
    *needs_reintegration = merge->is_reintegrate_like;
  if (yca_url)
    *yca_url = apr_pstrdup(result_pool, merge->yca->url);
  if (yca_rev)
    *yca_rev = merge->yca->rev;
  if (base_url)
    *base_url = apr_pstrdup(result_pool, merge->base->url);
  if (base_rev)
    *base_rev = merge->base->rev;
  if (right_url)
    *right_url = apr_pstrdup(result_pool, merge->right->url);
  if (right_rev)
    *right_rev = merge->right->rev;
  if (target_url)
    *target_url = apr_pstrdup(result_pool, merge->target->url);
  if (target_rev)
    *target_rev = merge->target->rev;
  if (repos_root_url)
    *repos_root_url = apr_pstrdup(result_pool, merge->yca->repos_root_url);

  return SVN_NO_ERROR;
}
