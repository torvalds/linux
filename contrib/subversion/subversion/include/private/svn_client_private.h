/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_client_private.h
 * @brief Subversion-internal client APIs.
 */

#ifndef SVN_CLIENT_PRIVATE_H
#define SVN_CLIENT_PRIVATE_H

#include <apr_pools.h>

#include "svn_ra.h"
#include "svn_client.h"
#include "svn_types.h"

#include "private/svn_diff_tree.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Set *REVNUM to the revision number identified by REVISION.

   If REVISION->kind is svn_opt_revision_number, just use
   REVISION->value.number, ignoring LOCAL_ABSPATH and RA_SESSION.

   Else if REVISION->kind is svn_opt_revision_committed,
   svn_opt_revision_previous, or svn_opt_revision_base, or
   svn_opt_revision_working, then the revision can be identified
   purely based on the working copy's administrative information for
   LOCAL_ABSPATH, so RA_SESSION is ignored.  If LOCAL_ABSPATH is not
   under revision control, return SVN_ERR_UNVERSIONED_RESOURCE, or if
   LOCAL_ABSPATH is null, return SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED.

   Else if REVISION->kind is svn_opt_revision_date or
   svn_opt_revision_head, then RA_SESSION is used to retrieve the
   revision from the repository (using REVISION->value.date in the
   former case), and LOCAL_ABSPATH is ignored.  If RA_SESSION is null,
   return SVN_ERR_CLIENT_RA_ACCESS_REQUIRED.

   Else if REVISION->kind is svn_opt_revision_unspecified, set
   *REVNUM to SVN_INVALID_REVNUM.

   If YOUNGEST_REV is non-NULL, it is an in/out parameter.  If
   *YOUNGEST_REV is valid, use it as the youngest revision in the
   repository (regardless of reality) -- don't bother to lookup the
   true value for HEAD, and don't return any value in *REVNUM greater
   than *YOUNGEST_REV.  If *YOUNGEST_REV is not valid, and a HEAD
   lookup is required to populate *REVNUM, then also populate
   *YOUNGEST_REV with the result.  This is useful for making multiple
   serialized calls to this function with a basically static view of
   the repository, avoiding race conditions which could occur between
   multiple invocations with HEAD lookup requests.

   Else return SVN_ERR_CLIENT_BAD_REVISION.

   Use SCRATCH_POOL for any temporary allocation.  */
svn_error_t *
svn_client__get_revision_number(svn_revnum_t *revnum,
                                svn_revnum_t *youngest_rev,
                                svn_wc_context_t *wc_ctx,
                                const char *local_abspath,
                                svn_ra_session_t *ra_session,
                                const svn_opt_revision_t *revision,
                                apr_pool_t *scratch_pool);

/* Return true if KIND is a revision kind that is dependent on the working
 * copy. Otherwise, return false. */
#define SVN_CLIENT__REVKIND_NEEDS_WC(kind)                                 \
  ((kind) == svn_opt_revision_base ||                                      \
   (kind) == svn_opt_revision_previous ||                                  \
   (kind) == svn_opt_revision_working ||                                   \
   (kind) == svn_opt_revision_committed)                                   \

/* Return true if KIND is a revision kind that the WC can supply without
 * contacting the repository. Otherwise, return false. */
#define SVN_CLIENT__REVKIND_IS_LOCAL_TO_WC(kind)                           \
  ((kind) == svn_opt_revision_base ||                                      \
   (kind) == svn_opt_revision_working ||                                   \
   (kind) == svn_opt_revision_committed)

/* A location in a repository. */
typedef struct svn_client__pathrev_t
{
  const char *repos_root_url;
  const char *repos_uuid;
  svn_revnum_t rev;
  const char *url;
} svn_client__pathrev_t;

/* Return a new path-rev structure, allocated in RESULT_POOL,
 * initialized with deep copies of REPOS_ROOT_URL, REPOS_UUID, REV and URL. */
svn_client__pathrev_t *
svn_client__pathrev_create(const char *repos_root_url,
                           const char *repos_uuid,
                           svn_revnum_t rev,
                           const char *url,
                           apr_pool_t *result_pool);

/* Return a new path-rev structure, allocated in RESULT_POOL,
 * initialized with deep copies of REPOS_ROOT_URL, REPOS_UUID, and REV,
 * and using the repository-relative RELPATH to construct the URL. */
svn_client__pathrev_t *
svn_client__pathrev_create_with_relpath(const char *repos_root_url,
                                        const char *repos_uuid,
                                        svn_revnum_t rev,
                                        const char *relpath,
                                        apr_pool_t *result_pool);

/* Set *PATHREV_P to a new path-rev structure, allocated in RESULT_POOL,
 * initialized with deep copies of the repository root URL and UUID from
 * RA_SESSION, and of REV and URL. */
svn_error_t *
svn_client__pathrev_create_with_session(svn_client__pathrev_t **pathrev_p,
                                        svn_ra_session_t *ra_session,
                                        svn_revnum_t rev,
                                        const char *url,
                                        apr_pool_t *result_pool);

/* Return a deep copy of PATHREV, allocated in RESULT_POOL. */
svn_client__pathrev_t *
svn_client__pathrev_dup(const svn_client__pathrev_t *pathrev,
                        apr_pool_t *result_pool);

/* Return a deep copy of PATHREV, with a URI-encoded representation of
 * RELPATH joined on to the URL.  Allocate the result in RESULT_POOL. */
svn_client__pathrev_t *
svn_client__pathrev_join_relpath(const svn_client__pathrev_t *pathrev,
                                 const char *relpath,
                                 apr_pool_t *result_pool);

/* Return the repository-relative relpath of PATHREV. */
const char *
svn_client__pathrev_relpath(const svn_client__pathrev_t *pathrev,
                            apr_pool_t *result_pool);

/* Return the repository-relative fspath of PATHREV. */
const char *
svn_client__pathrev_fspath(const svn_client__pathrev_t *pathrev,
                           apr_pool_t *result_pool);

/* Given PATH_OR_URL, which contains either a working copy path or an
   absolute URL, a peg revision PEG_REVISION, and a desired revision
   REVISION, create an RA connection to that object as it exists in
   that revision, following copy history if necessary.  If REVISION is
   younger than PEG_REVISION, then PATH_OR_URL will be checked to see
   that it is the same node in both PEG_REVISION and REVISION.  If it
   is not, then @c SVN_ERR_CLIENT_UNRELATED_RESOURCES is returned.

   BASE_DIR_ABSPATH is the working copy path the ra_session corresponds
   to. If provided it will be used to read and dav props. So if provided
   this directory MUST match the session anchor.

   If PEG_REVISION->kind is 'unspecified', the peg revision is 'head'
   for a URL or 'working' for a WC path.  If REVISION->kind is
   'unspecified', the operative revision is the peg revision.

   Store the resulting ra_session in *RA_SESSION_P.  Store the final
   resolved location of the object in *RESOLVED_LOC_P.  RESOLVED_LOC_P
   may be NULL if not wanted.

   Use authentication baton cached in CTX to authenticate against the
   repository.

   Use POOL for all allocations. */
svn_error_t *
svn_client__ra_session_from_path2(svn_ra_session_t **ra_session_p,
                                 svn_client__pathrev_t **resolved_loc_p,
                                 const char *path_or_url,
                                 const char *base_dir_abspath,
                                 const svn_opt_revision_t *peg_revision,
                                 const svn_opt_revision_t *revision,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool);

/* Given PATH_OR_URL, which contains either a working copy path or an
   absolute URL, a peg revision PEG_REVISION, and a desired revision
   REVISION, find the path at which that object exists in REVISION,
   following copy history if necessary.  If REVISION is younger than
   PEG_REVISION, then check that PATH_OR_URL is the same node in both
   PEG_REVISION and REVISION, and return @c
   SVN_ERR_CLIENT_UNRELATED_RESOURCES if it is not the same node.

   If PEG_REVISION->kind is 'unspecified', the peg revision is 'head'
   for a URL or 'working' for a WC path.  If REVISION->kind is
   'unspecified', the operative revision is the peg revision.

   Store the actual location of the object in *RESOLVED_LOC_P.

   RA_SESSION should be an open RA session pointing at the URL of
   PATH_OR_URL, or NULL, in which case this function will open its own
   temporary session.

   Use authentication baton cached in CTX to authenticate against the
   repository.

   Use POOL for all allocations. */
svn_error_t *
svn_client__resolve_rev_and_url(svn_client__pathrev_t **resolved_loc_p,
                                svn_ra_session_t *ra_session,
                                const char *path_or_url,
                                const svn_opt_revision_t *peg_revision,
                                const svn_opt_revision_t *revision,
                                svn_client_ctx_t *ctx,
                                apr_pool_t *pool);

/** Return @c SVN_ERR_ILLEGAL_TARGET if TARGETS contains a mixture of
 * URLs and paths; otherwise return SVN_NO_ERROR.
 *
 * @since New in 1.7.
 */
svn_error_t *
svn_client__assert_homogeneous_target_type(const apr_array_header_t *targets);


/* Create a svn_client_status_t structure *CST for LOCAL_ABSPATH, shallow
 * copying data from *STATUS wherever possible and retrieving the other values
 * where needed. Perform temporary allocations in SCRATCH_POOL and allocate the
 * result in RESULT_POOL
 */
svn_error_t *
svn_client__create_status(svn_client_status_t **cst,
                          svn_wc_context_t *wc_ctx,
                          const char *local_abspath,
                          const svn_wc_status3_t *status,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool);

/* Get the repository location of the base node at LOCAL_ABSPATH.
 *
 * A pathrev_t wrapper around svn_wc__node_get_base().
 *
 * Set *BASE_P to the location that this node was checked out at or last
 * updated/switched to, regardless of any uncommitted changes (delete,
 * replace and/or copy-here/move-here).
 *
 * If there is no base node at LOCAL_ABSPATH (such as when there is a
 * locally added/copied/moved-here node that is not part of a replace),
 * set *BASE_P to NULL.
 */
svn_error_t *
svn_client__wc_node_get_base(svn_client__pathrev_t **base_p,
                             const char *wc_abspath,
                             svn_wc_context_t *wc_ctx,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/* Get the original location of the WC node at LOCAL_ABSPATH.
 *
 * A pathrev_t wrapper around svn_wc__node_get_origin().
 *
 * Set *ORIGIN_P to the origin of the WC node at WC_ABSPATH.  If the node
 * is a local copy, give the copy-from location.  If the node is locally
 * added or deleted, set *ORIGIN_P to NULL.
 */
svn_error_t *
svn_client__wc_node_get_origin(svn_client__pathrev_t **origin_p,
                               const char *wc_abspath,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool);

/* Copy the file or directory on URL in some repository to DST_ABSPATH,
 * copying node information and properties. Resolve URL using PEG_REV and
 * REVISION.
 *
 * If URL specifies a directory, create the copy using depth DEPTH.
 *
 * If MAKE_PARENTS is TRUE and DST_ABSPATH doesn't have an added parent
 * create missing parent directories
 */
svn_error_t *
svn_client__copy_foreign(const char *url,
                         const char *dst_abspath,
                         svn_opt_revision_t *peg_revision,
                         svn_opt_revision_t *revision,
                         svn_depth_t depth,
                         svn_boolean_t make_parents,
                         svn_boolean_t already_locked,
                         svn_client_ctx_t *ctx,
                         apr_pool_t *scratch_pool);

/* Same as the public svn_client_mergeinfo_log2 API, except for the addition
 * of the TARGET_MERGEINFO_CATALOG and RESULT_POOL parameters.
 *
 * If TARGET_MERGEINFO_CATALOG is NULL then this acts exactly as the public
 * API.  If *TARGET_MERGEINFO_CATALOG is NULL, then *TARGET_MERGEINFO_CATALOG
 * is set to the a mergeinfo catalog representing the mergeinfo on
 * TARGET_PATH_OR_URL@TARGET_PEG_REVISION at DEPTH, (like the public API only
 * depths of svn_depth_empty or svn_depth_infinity are supported) allocated in
 * RESULT_POOL.  Finally, if *TARGET_MERGEINFO_CATALOG is non-NULL, then it is
 * assumed to be a mergeinfo catalog representing the mergeinfo on
 * TARGET_PATH_OR_URL@TARGET_PEG_REVISION at DEPTH.
 *
 * The keys for the subtree mergeinfo are the repository root-relative
 * paths of TARGET_PATH_OR_URL and/or its subtrees, regardless of whether
 * TARGET_PATH_OR_URL is a URL or WC path.
 *
 * If RA_SESSION is not NULL, use it to obtain merge information instead of
 * opening a new session. The session might be reparented after usage, so
 * callers should reparent the session back to their original location if
 * needed.
 */
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
                          apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CLIENT_PRIVATE_H */
