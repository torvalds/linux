/*
 * client.h :  shared stuff internal to the client library.
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



#ifndef SVN_LIBSVN_CLIENT_H
#define SVN_LIBSVN_CLIENT_H


#include <apr_pools.h>

#include "svn_types.h"
#include "svn_opt.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_ra.h"
#include "svn_client.h"

#include "private/svn_magic.h"
#include "private/svn_client_private.h"
#include "private/svn_diff_tree.h"
#include "private/svn_editor.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Private client context.
 *
 * This is what is actually allocated by svn_client_create_context2(),
 * which then returns the address of the public_ctx member. */
typedef struct svn_client__private_ctx_t
{
  /* Reserved field, always zero, to detect misuse of the private
     context as a public client context. */
  apr_uint64_t magic_null;

  /* Reserved field, always set to a known magic number, to identify
     this struct as the private client context. */
  apr_uint64_t magic_id;

  /* Total number of bytes transferred over network across all RA sessions. */
  apr_off_t total_progress;

  /* The public context. */
  svn_client_ctx_t public_ctx;
} svn_client__private_ctx_t;


/* Given a public client context CTX, return the private context
   within which it is allocated. */
svn_client__private_ctx_t *
svn_client__get_private_ctx(svn_client_ctx_t *ctx);

/* Set *ORIGINAL_REPOS_RELPATH and *ORIGINAL_REVISION to the original location
   that served as the source of the copy from which PATH_OR_URL at REVISION was
   created, or NULL and SVN_INVALID_REVNUM (respectively) if PATH_OR_URL at
   REVISION was not the result of a copy operation.

   If RA_SESSION is not NULL it is an existing session to the repository that
   might be reparented temporarily to obtain the information.
   */
svn_error_t *
svn_client__get_copy_source(const char **original_repos_relpath,
                            svn_revnum_t *original_revision,
                            const char *path_or_url,
                            const svn_opt_revision_t *revision,
                            svn_ra_session_t *ra_session,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);

/* Set *START_URL and *START_REVISION (and maybe *END_URL
   and *END_REVISION) to the revisions and repository URLs of one
   (or two) points of interest along a particular versioned resource's
   line of history.  PATH as it exists in "peg revision"
   REVISION identifies that line of history, and START and END
   specify the point(s) of interest (typically the revisions referred
   to as the "operative range" for a given operation) along that history.

   START_REVISION and/or END_REVISION may be NULL if not wanted.
   END may be NULL or of kind svn_opt_revision_unspecified (in either case
   END_URL and END_REVISION are not touched by the function);
   START and REVISION may not.

   If PATH is a WC path and REVISION is of kind svn_opt_revision_working,
   then look at the PATH's copy-from URL instead of its base URL.

   RA_SESSION should be an open RA session pointing at the URL of PATH,
   or NULL, in which case this function will open its own temporary session.

   A NOTE ABOUT FUTURE REPORTING:

   If either START or END are greater than REVISION, then do a
   sanity check (since we cannot search future history yet): verify
   that PATH in the future revision(s) is the "same object" as the
   one pegged by REVISION.  In other words, all three objects must
   be connected by a single line of history which exactly passes
   through PATH at REVISION.  If this sanity check fails, return
   SVN_ERR_CLIENT_UNRELATED_RESOURCES.  If PATH doesn't exist in the future
   revision, SVN_ERR_FS_NOT_FOUND may also be returned.

   CTX is the client context baton.

   Use POOL for all allocations.  */
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
                            apr_pool_t *pool);

/* Trace a line of history of a particular versioned resource back to a
 * specific revision.
 *
 * Set *OP_LOC_P to the location that the object PEG_LOC had in
 * revision OP_REVNUM.
 *
 * RA_SESSION is an open RA session to the correct repository; it may be
 * temporarily reparented inside this function. */
svn_error_t *
svn_client__repos_location(svn_client__pathrev_t **op_loc_p,
                           svn_ra_session_t *ra_session,
                           const svn_client__pathrev_t *peg_loc,
                           svn_revnum_t op_revnum,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);


/* Set *SEGMENTS to an array of svn_location_segment_t * objects, each
   representing a reposition location segment for the history of URL
   in PEG_REVISION
   between END_REVISION and START_REVISION, ordered from oldest
   segment to youngest.  *SEGMENTS may be empty but it will never
   be NULL.

   This is basically a thin de-stream-ifying wrapper around the
   svn_ra_get_location_segments() interface, which see for the rules
   governing PEG_REVISION, START_REVISION, and END_REVISION.

   RA_SESSION is an RA session open to the repository of URL; it may be
   temporarily reparented within this function.

   CTX is the client context baton.

   Use POOL for all allocations.  */
svn_error_t *
svn_client__repos_location_segments(apr_array_header_t **segments,
                                    svn_ra_session_t *ra_session,
                                    const char *url,
                                    svn_revnum_t peg_revision,
                                    svn_revnum_t start_revision,
                                    svn_revnum_t end_revision,
                                    svn_client_ctx_t *ctx,
                                    apr_pool_t *pool);


/* Find the common ancestor of two locations in a repository.
   Ancestry is determined by the 'copy-from' relationship and the normal
   successor relationship.

   Set *ANCESTOR_P to the location of the youngest common ancestor of
   LOC1 and LOC2.  If the locations have no common ancestor (including if
   they don't have the same repository root URL), set *ANCESTOR_P to NULL.

   If SESSION is not NULL, use it for retrieving the common ancestor instead
   of creating a new session.

   Use the authentication baton cached in CTX to authenticate against
   the repository.  Use POOL for all allocations.

   See also svn_client__calc_youngest_common_ancestor() to find youngest
   common ancestor for already fetched history-as-mergeinfo information.

*/
svn_error_t *
svn_client__get_youngest_common_ancestor(svn_client__pathrev_t **ancestor_p,
                                         const svn_client__pathrev_t *loc1,
                                         const svn_client__pathrev_t *loc2,
                                         svn_ra_session_t *session,
                                         svn_client_ctx_t *ctx,
                                         apr_pool_t *result_pool,
                                         apr_pool_t *scratch_pool);

/* Find the common ancestor of two locations in a repository using already
   fetched history-as-mergeinfo information.

   Ancestry is determined by the 'copy-from' relationship and the normal
   successor relationship.

   Set *ANCESTOR_P to the location of the youngest common ancestor of
   LOC1 and LOC2.  If the locations have no common ancestor (including if
   they don't have the same repository root URL), set *ANCESTOR_P to NULL.

   HISTORY1, HAS_REV_ZERO_HISTORY1, HISTORY2, HAS_REV_ZERO_HISTORY2 are
   history-as-mergeinfo information as returned by
   svn_client__get_history_as_mergeinfo() for LOC1 and LOC2 respectively.

   See also svn_client__get_youngest_common_ancestor().

*/
svn_error_t *
svn_client__calc_youngest_common_ancestor(svn_client__pathrev_t **ancestor_p,
                                          const svn_client__pathrev_t *loc1,
                                          apr_hash_t *history1,
                                          svn_boolean_t has_rev_zero_history1,
                                          const svn_client__pathrev_t *loc2,
                                          apr_hash_t *history2,
                                          svn_boolean_t has_rev_zero_history2,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool);

/* Ensure that RA_SESSION's session URL matches SESSION_URL,
   reparenting that session if necessary.
   Store the previous session URL in *OLD_SESSION_URL (so that if the
   reparenting is meant to be temporary, the caller can reparent the
   session back to where it was).

   If SESSION_URL is NULL, treat this as a magic value meaning "point
   the RA session to the root of the repository".

   NOTE: The typical usage pattern for this functions is:

       const char *old_session_url;
       SVN_ERR(svn_client__ensure_ra_session_url(&old_session_url,
                                                 ra_session,
                                                 new_session_url,
                                                 pool);

       [...]

       SVN_ERR(svn_ra_reparent(ra_session, old_session_url, pool));
*/
svn_error_t *
svn_client__ensure_ra_session_url(const char **old_session_url,
                                  svn_ra_session_t *ra_session,
                                  const char *session_url,
                                  apr_pool_t *pool);

/* ---------------------------------------------------------------- */


/*** RA callbacks ***/


/* CTX is of type "svn_client_ctx_t *". */
#define SVN_CLIENT__HAS_LOG_MSG_FUNC(ctx) \
        ((ctx)->log_msg_func3 || (ctx)->log_msg_func2 || (ctx)->log_msg_func)

/* Open an RA session, returning it in *RA_SESSION or a corrected URL
   in *CORRECTED_URL.  (This function mirrors svn_ra_open4(), which
   see, regarding the interpretation and handling of these two parameters.)

   The root of the session is specified by BASE_URL and BASE_DIR_ABSPATH.

   Additional control parameters:

      - COMMIT_ITEMS is an array of svn_client_commit_item_t *
        structures, present only for working copy commits, NULL otherwise.

      - WRITE_DAV_PROPS indicates that the RA layer can clear and write
        the DAV properties in the working copy of BASE_DIR_ABSPATH.

      - READ_DAV_PROPS indicates that the RA layer should not attempt to
        modify the WC props directly, but is still allowed to read them.

   BASE_DIR_ABSPATH may be NULL if the RA operation does not correspond to a
   working copy (in which case, WRITE_DAV_PROPS and READ_DAV_PROPS must be
   FALSE.

   If WRITE_DAV_PROPS and READ_DAV_PROPS are both FALSE the working copy may
   still be used for locating pristine files via their SHA1.

   The calling application's authentication baton is provided in CTX,
   and allocations related to this session are performed in POOL.

   NOTE: The reason for the _internal suffix of this function's name is to
   avoid confusion with the public API svn_client_open_ra_session(). */
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
                                     apr_pool_t *scratch_pool);


svn_error_t *
svn_client__ra_provide_base(svn_stream_t **contents,
                            svn_revnum_t *revision,
                            void *baton,
                            const char *repos_relpath,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);


svn_error_t *
svn_client__ra_provide_props(apr_hash_t **props,
                             svn_revnum_t *revision,
                             void *baton,
                             const char *repos_relpath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);


svn_error_t *
svn_client__ra_get_copysrc_kind(svn_node_kind_t *kind,
                                void *baton,
                                const char *repos_relpath,
                                svn_revnum_t src_revision,
                                apr_pool_t *scratch_pool);


void *
svn_client__ra_make_cb_baton(svn_wc_context_t *wc_ctx,
                             apr_hash_t *relpath_map,
                             apr_pool_t *result_pool);

/* ---------------------------------------------------------------- */


/*** Add/delete ***/

/* If AUTOPROPS is not null: Then read automatic properties matching PATH
   from AUTOPROPS.  AUTOPROPS is is a hash as per
   svn_client__get_all_auto_props.  Set *PROPERTIES to a hash containing
   propname/value pairs (const char * keys mapping to svn_string_t * values).

   If AUTOPROPS is null then set *PROPERTIES to an empty hash.

   If *MIMETYPE is null or "application/octet-stream" then check AUTOPROPS
   for a matching svn:mime-type.  If AUTOPROPS is null or no match is found
   and MAGIC_COOKIE is not NULL, then then try to detect the mime-type with
   libmagic.  If a mimetype is found then add it to *PROPERTIES and set
   *MIMETYPE to the mimetype value or NULL otherwise.

   Allocate the *PROPERTIES and its contents as well as *MIMETYPE, in
   RESULT_POOL.  Use SCRATCH_POOL for temporary allocations. */
svn_error_t *svn_client__get_paths_auto_props(
  apr_hash_t **properties,
  const char **mimetype,
  const char *path,
  svn_magic__cookie_t *magic_cookie,
  apr_hash_t *autoprops,
  svn_client_ctx_t *ctx,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool);

/* Gather all auto-props from CTX->config (or none if auto-props are
   disabled) and all svn:auto-props explicitly set on or inherited
   by PATH_OR_URL.

   If PATH_OR_URL is an unversioned WC path then gather the
   svn:auto-props inherited by PATH_OR_URL's nearest versioned
   parent.

   If PATH_OR_URL is a URL ask for the properties @HEAD, if it is a WC
   path as sfor the working properties.

   Store both types of auto-props in *AUTOPROPS, a hash mapping const
   char * file patterns to another hash which maps const char * property
   names to const char *property values.

   If a given property name exists for the same pattern in both the config
   file and in an a svn:auto-props property, the latter overrides the
   former.  If a given property name exists for the same pattern in two
   different inherited svn:auto-props, then the closer path-wise
   property overrides the more distant. svn:auto-props explicitly set
   on PATH_OR_URL have the highest precedence and override inherited props
   and config file settings.

   Allocate *AUTOPROPS in RESULT_POOL.  Use SCRATCH_POOL for temporary
   allocations. */
svn_error_t *svn_client__get_all_auto_props(apr_hash_t **autoprops,
                                            const char *path_or_url,
                                            svn_client_ctx_t *ctx,
                                            apr_pool_t *result_pool,
                                            apr_pool_t *scratch_pool);

/* The main logic for client deletion from a working copy. Deletes PATH
   from CTX->WC_CTX.  If PATH (or any item below a directory PATH) is
   modified the delete will fail and return an error unless FORCE or KEEP_LOCAL
   is TRUE.

   If KEEP_LOCAL is TRUE then PATH is only scheduled from deletion from the
   repository and a local copy of PATH will be kept in the working copy.

   If DRY_RUN is TRUE all the checks are made to ensure that the delete can
   occur, but the working copy is not modified.  If NOTIFY_FUNC is not
   null, it is called with NOTIFY_BATON for each file or directory deleted. */
svn_error_t *
svn_client__wc_delete(const char *local_abspath,
                      svn_boolean_t force,
                      svn_boolean_t dry_run,
                      svn_boolean_t keep_local,
                      svn_wc_notify_func2_t notify_func,
                      void *notify_baton,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *pool);


/* Like svn_client__wc_delete(), but deletes multiple TARGETS efficiently. */
svn_error_t *
svn_client__wc_delete_many(const apr_array_header_t *targets,
                           svn_boolean_t force,
                           svn_boolean_t dry_run,
                           svn_boolean_t keep_local,
                           svn_wc_notify_func2_t notify_func,
                           void *notify_baton,
                           svn_client_ctx_t *ctx,
                           apr_pool_t *pool);


/* Make LOCAL_ABSPATH and add it to the working copy, optionally making all
   the intermediate parent directories if MAKE_PARENTS is TRUE. */
svn_error_t *
svn_client__make_local_parents(const char *local_abspath,
                               svn_boolean_t make_parents,
                               svn_client_ctx_t *ctx,
                               apr_pool_t *pool);

/* ---------------------------------------------------------------- */


/*** Checkout, update and switch ***/

/* Update a working copy LOCAL_ABSPATH to REVISION, and (if not NULL) set
   RESULT_REV to the update revision.

   If DEPTH is svn_depth_unknown, then use whatever depth is already
   set for LOCAL_ABSPATH, or @c svn_depth_infinity if LOCAL_ABSPATH does
   not exist.

   Else if DEPTH is svn_depth_infinity, then update fully recursively
   (resetting the existing depth of the working copy if necessary).
   Else if DEPTH is svn_depth_files, update all files under LOCAL_ABSPATH (if
   any), but exclude any subdirectories.  Else if DEPTH is
   svn_depth_immediates, update all files and include immediate
   subdirectories (at svn_depth_empty).  Else if DEPTH is
   svn_depth_empty, just update LOCAL_ABSPATH; if LOCAL_ABSPATH is a
   directory, that means touching only its properties not its entries.

   If DEPTH_IS_STICKY is set and DEPTH is not svn_depth_unknown, then
   in addition to updating LOCAL_ABSPATH, also set its sticky ambient depth
   value to DEPTH.

   If IGNORE_EXTERNALS is true, do no externals processing.

   Set *TIMESTAMP_SLEEP to TRUE if a sleep is required; otherwise do not
   change *TIMESTAMP_SLEEP.  The output will be valid even if the function
   returns an error.

   If ALLOW_UNVER_OBSTRUCTIONS is TRUE, unversioned children of LOCAL_ABSPATH
   that obstruct items added from the repos are tolerated; if FALSE,
   these obstructions cause the update to fail.

   If ADDS_AS_MODIFICATION is TRUE, local additions are handled as
   modifications on added nodes.

   If INNERUPDATE is true, no anchor check is performed on the update target.

   If MAKE_PARENTS is true, allow the update to calculate and checkout
   (with depth=empty) any parent directories of the requested update
   target which are missing from the working copy.

   If RA_SESSION is NOT NULL, it may be used to avoid creating a new
   session. The session may point to a different URL after returning.

   NOTE:  You may not specify both INNERUPDATE and MAKE_PARENTS as true.
*/
svn_error_t *
svn_client__update_internal(svn_revnum_t *result_rev,
                            svn_boolean_t *timestamp_sleep,
                            const char *local_abspath,
                            const svn_opt_revision_t *revision,
                            svn_depth_t depth,
                            svn_boolean_t depth_is_sticky,
                            svn_boolean_t ignore_externals,
                            svn_boolean_t allow_unver_obstructions,
                            svn_boolean_t adds_as_modification,
                            svn_boolean_t make_parents,
                            svn_boolean_t innerupdate,
                            svn_ra_session_t *ra_session,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

/* Checkout into LOCAL_ABSPATH a working copy of URL at REVISION, and (if not
   NULL) set RESULT_REV to the checked out revision.

   If DEPTH is svn_depth_infinity, then check out fully recursively.
   Else if DEPTH is svn_depth_files, checkout all files under LOCAL_ABSPATH (if
   any), but not subdirectories.  Else if DEPTH is
   svn_depth_immediates, check out all files and include immediate
   subdirectories (at svn_depth_empty).  Else if DEPTH is
   svn_depth_empty, just check out LOCAL_ABSPATH, with none of its entries.

   DEPTH must be a definite depth, not (e.g.) svn_depth_unknown.

   If IGNORE_EXTERNALS is true, do no externals processing.

   Set *TIMESTAMP_SLEEP to TRUE if a sleep is required; otherwise do not
   change *TIMESTAMP_SLEEP.  The output will be valid even if the function
   returns an error.

   If ALLOW_UNVER_OBSTRUCTIONS is TRUE,
   unversioned children of LOCAL_ABSPATH that obstruct items added from
   the repos are tolerated; if FALSE, these obstructions cause the checkout
   to fail.

   If RA_SESSION is NOT NULL, it may be used to avoid creating a new
   session. The session may point to a different URL after returning.
   */
svn_error_t *
svn_client__checkout_internal(svn_revnum_t *result_rev,
                              svn_boolean_t *timestamp_sleep,
                              const char *URL,
                              const char *local_abspath,
                              const svn_opt_revision_t *peg_revision,
                              const svn_opt_revision_t *revision,
                              svn_depth_t depth,
                              svn_boolean_t ignore_externals,
                              svn_boolean_t allow_unver_obstructions,
                              svn_ra_session_t *ra_session,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *pool);

/* Switch a working copy PATH to URL@PEG_REVISION at REVISION, and (if not
   NULL) set RESULT_REV to the switch revision. A write lock will be
   acquired and released if not held. Only switch as deeply as DEPTH
   indicates.

   Set *TIMESTAMP_SLEEP to TRUE if a sleep is required; otherwise do not
   change *TIMESTAMP_SLEEP.  The output will be valid even if the function
   returns an error.

   If IGNORE_EXTERNALS is true, don't process externals.

   If ALLOW_UNVER_OBSTRUCTIONS is TRUE, unversioned children of PATH
   that obstruct items added from the repos are tolerated; if FALSE,
   these obstructions cause the switch to fail.

   DEPTH and DEPTH_IS_STICKY behave as for svn_client__update_internal().

   If IGNORE_ANCESTRY is true, don't perform a common ancestry check
   between the PATH and URL; otherwise, do, and return
   SVN_ERR_CLIENT_UNRELATED_RESOURCES if they aren't related.
*/
svn_error_t *
svn_client__switch_internal(svn_revnum_t *result_rev,
                            const char *path,
                            const char *url,
                            const svn_opt_revision_t *peg_revision,
                            const svn_opt_revision_t *revision,
                            svn_depth_t depth,
                            svn_boolean_t depth_is_sticky,
                            svn_boolean_t ignore_externals,
                            svn_boolean_t allow_unver_obstructions,
                            svn_boolean_t ignore_ancestry,
                            svn_boolean_t *timestamp_sleep,
                            svn_client_ctx_t *ctx,
                            apr_pool_t *pool);

/* ---------------------------------------------------------------- */


/*** Inheritable Properties ***/

/* Convert any svn_prop_inherited_item_t elements in INHERITED_PROPS which
   have repository root relative path PATH_OR_URL structure members to URLs
   using REPOS_ROOT_URL.  Changes to the contents of INHERITED_PROPS are
   allocated in RESULT_POOL.  SCRATCH_POOL is used for temporary
   allocations. */
svn_error_t *
svn_client__iprop_relpaths_to_urls(apr_array_header_t *inherited_props,
                                   const char *repos_root_url,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool);

/* Fetch the inherited properties for the base of LOCAL_ABSPATH as well
   as any WC roots under LOCAL_ABSPATH (as limited by DEPTH) using
   RA_SESSION.  Store the results in *WCROOT_IPROPS, a hash mapping
   const char * absolute working copy paths to depth-first ordered arrays
   of svn_prop_inherited_item_t * structures.

   Any svn_prop_inherited_item_t->path_or_url members returned in
   *WCROOT_IPROPS are repository relative paths.

   If LOCAL_ABSPATH has no base then do nothing.

   RA_SESSION should be an open RA session pointing at the URL of PATH,
   or NULL, in which case this function will use its own temporary session.

   Allocate *WCROOT_IPROPS in RESULT_POOL, use SCRATCH_POOL for temporary
   allocations.

   If one or more of the paths are not available in the repository at the
   specified revision, these paths will not be added to the hashtable.
*/
svn_error_t *
svn_client__get_inheritable_props(apr_hash_t **wcroot_iprops,
                                  const char *local_abspath,
                                  svn_revnum_t revision,
                                  svn_depth_t depth,
                                  svn_ra_session_t *ra_session,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool);

/* ---------------------------------------------------------------- */


/*** Editor for repository diff ***/

/* Create an editor for a pure repository comparison, i.e. comparing one
   repository version against the other.

   DIFF_CALLBACKS/DIFF_CMD_BATON represent the callback that implements
   the comparison.

   DEPTH is the depth to recurse.

   RA_SESSION is an RA session through which this editor may fetch
   properties, file contents and directory listings of the 'old' side of the
   diff. It is a separate RA session from the one through which this editor
   is being driven. REVISION is the revision number of the 'old' side of
   the diff.

   If TEXT_DELTAS is FALSE, then do not expect text deltas from the edit
   drive, nor send the 'before' and 'after' texts to the diff callbacks;
   instead, send empty files to the diff callbacks if there was a change.
   This must be FALSE if the edit producer is not sending text deltas,
   otherwise the file content checksum comparisons will fail.

   EDITOR/EDIT_BATON return the newly created editor and baton.

   @since New in 1.8.
   */
svn_error_t *
svn_client__get_diff_editor2(const svn_delta_editor_t **editor,
                             void **edit_baton,
                             svn_ra_session_t *ra_session,
                             svn_depth_t depth,
                             svn_revnum_t revision,
                             svn_boolean_t text_deltas,
                             const svn_diff_tree_processor_t *processor,
                             svn_cancel_func_t cancel_func,
                             void *cancel_baton,
                             apr_pool_t *result_pool);

/* ---------------------------------------------------------------- */


/*** Editor for diff summary ***/

/* Set *DIFF_PROCESSOR to a diff processor that will report a diff summary
   to SUMMARIZE_FUNC.

   P_ROOT_RELPATH will return a pointer to a string that must be set to
   the root of the operation before the processor is called.

   ORIGINAL_PATH specifies the original path and will be used with
   **ANCHOR_PATH to create paths as the user originally provided them
   to the diff function.

   SUMMARIZE_FUNC is called with SUMMARIZE_BATON as parameter by the
   created callbacks for each changed item.
*/
svn_error_t *
svn_client__get_diff_summarize_callbacks(
                        const svn_diff_tree_processor_t **diff_processor,
                        const char ***p_root_relpath,
                        svn_client_diff_summarize_func_t summarize_func,
                        void *summarize_baton,
                        const char *original_target,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* ---------------------------------------------------------------- */


/*** Copy Stuff ***/

/* This structure is used to associate a specific copy or move SRC with a
   specific copy or move destination.  It also contains information which
   various helper functions may need.  Not every copy function uses every
   field.
*/
typedef struct svn_client__copy_pair_t
{
    /* The absolute source path or url. */
    const char *src_abspath_or_url;

    /* The base name of the object.  It should be the same for both src
       and dst. */
    const char *base_name;

    /* The node kind of the source */
    svn_node_kind_t src_kind;

    /* The original source name.  (Used when the source gets overwritten by a
       peg revision lookup.) */
    const char *src_original;

    /* The source operational revision. */
    svn_opt_revision_t src_op_revision;

    /* The source peg revision. */
    svn_opt_revision_t src_peg_revision;

    /* The source revision number. */
    svn_revnum_t src_revnum;

    /* The absolute destination path or url */
    const char *dst_abspath_or_url;

    /* The absolute source path or url of the destination's parent. */
    const char *dst_parent_abspath;
} svn_client__copy_pair_t;

/* ---------------------------------------------------------------- */


/*** Commit Stuff ***/

/* WARNING: This is all new, untested, un-peer-reviewed conceptual
   stuff.

   The day that 'svn switch' came into existence, our old commit
   crawler (svn_wc_crawl_local_mods) became obsolete.  It relied far
   too heavily on the on-disk hierarchy of files and directories, and
   simply had no way to support disjoint working copy trees or nest
   working copies.  The primary reason for this is that commit
   process, in order to guarantee atomicity, is a single drive of a
   commit editor which is based not on working copy paths, but on
   URLs.  With the completion of 'svn switch', it became all too
   likely that the on-disk working copy hierarchy would no longer be
   guaranteed to map to a similar in-repository hierarchy.

   Aside from this new brokenness of the old system, an unrelated
   feature request had cropped up -- the ability to know in advance of
   your commit, exactly what would be committed (so that log messages
   could be initially populated with this information).  Since the old
   crawler discovered commit candidates while in the process of
   committing, it was impossible to harvest this information upfront.
   As a workaround, svn_wc_statuses() was used to stat the whole
   working copy for changes before the commit started...and then the
   commit would again stat the whole tree for changes.

   Enter the new system.

   The primary goal of this system is very straightforward: harvest
   all commit candidate information up front, and cache enough info in
   the process to use this to drive a URL-sorted commit.

   *** END-OF-KNOWLEDGE ***

   The prototypes below are still in development.  In general, the
   idea is that commit-y processes ('svn mkdir URL', 'svn delete URL',
   'svn commit', 'svn copy WC_PATH URL', 'svn copy URL1 URL2', 'svn
   move URL1 URL2', others?) generate the cached commit candidate
   information, and hand this information off to a consumer which is
   responsible for driving the RA layer's commit editor in a
   URL-depth-first fashion and reporting back the post-commit
   information.

*/

/* Structure that contains an apr_hash_t * hash of apr_array_header_t *
   arrays of svn_client_commit_item3_t * structures; keyed by the
   canonical repository URLs. For faster lookup, it also provides
   an hash index keyed by the local absolute path. */
typedef struct svn_client__committables_t
{
  /* apr_array_header_t array of svn_client_commit_item3_t structures
     keyed by canonical repository URL */
  apr_hash_t *by_repository;

  /* svn_client_commit_item3_t structures keyed by local absolute path
     (path member in the respective structures).

     This member is for fast lookup only, i.e. whether there is an
     entry for the given path or not, but it will only allow for one
     entry per absolute path (in case of duplicate entries in the
     above arrays). The "canonical" data storage containing all item
     is by_repository. */
  apr_hash_t *by_path;

} svn_client__committables_t;

/* Callback for the commit harvester to check if a node exists at the specified
   url */
typedef svn_error_t *(*svn_client__check_url_kind_t)(void *baton,
                                                     svn_node_kind_t *kind,
                                                     const char *url,
                                                     svn_revnum_t revision,
                                                     apr_pool_t *scratch_pool);

/* Recursively crawl a set of working copy paths (BASE_DIR_ABSPATH + each
   item in the TARGETS array) looking for commit candidates, locking
   working copy directories as the crawl progresses.  For each
   candidate found:

     - create svn_client_commit_item3_t for the candidate.

     - add the structure to an apr_array_header_t array of commit
       items that are in the same repository, creating a new array if
       necessary.

     - add (or update) a reference to this array to the by_repository
       hash within COMMITTABLES and update the by_path member as well-

     - if the candidate has a lock token, add it to the LOCK_TOKENS hash.

     - if the candidate is a directory scheduled for deletion, crawl
       the directories children recursively for any lock tokens and
       add them to the LOCK_TOKENS array.

   At the successful return of this function, COMMITTABLES will point
   a new svn_client__committables_t*.  LOCK_TOKENS will point to a hash
   table with const char * lock tokens, keyed on const char * URLs.

   If DEPTH is specified, descend (or not) into each target in TARGETS
   as specified by DEPTH; the behavior is the same as that described
   for svn_client_commit4().

   If DEPTH_EMPTY_START is >= 0, all targets after index DEPTH_EMPTY_START
   in TARGETS are handled as having svn_depth_empty.

   If JUST_LOCKED is TRUE, treat unmodified items with lock tokens as
   commit candidates.

   If CHANGELISTS is non-NULL, it is an array of const char *
   changelist names used as a restrictive filter
   when harvesting committables; that is, don't add a path to
   COMMITTABLES unless it's a member of one of those changelists.

   If CTX->CANCEL_FUNC is non-null, it will be called with
   CTX->CANCEL_BATON while harvesting to determine if the client has
   cancelled the operation. */
svn_error_t *
svn_client__harvest_committables(svn_client__committables_t **committables,
                                 apr_hash_t **lock_tokens,
                                 const char *base_dir_abspath,
                                 const apr_array_header_t *targets,
                                 int depth_empty_start,
                                 svn_depth_t depth,
                                 svn_boolean_t just_locked,
                                 const apr_array_header_t *changelists,
                                 svn_client__check_url_kind_t check_url_func,
                                 void *check_url_baton,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);


/* Recursively crawl each absolute working copy path SRC in COPY_PAIRS,
   harvesting commit_items into a COMMITABLES structure as if every entry
   at or below the SRC was to be committed as a set of adds (mostly with
   history) to a new repository URL (DST in COPY_PAIRS).

   If CTX->CANCEL_FUNC is non-null, it will be called with
   CTX->CANCEL_BATON while harvesting to determine if the client has
   cancelled the operation.  */
svn_error_t *
svn_client__get_copy_committables(svn_client__committables_t **committables,
                                  const apr_array_header_t *copy_pairs,
                                  svn_client__check_url_kind_t check_url_func,
                                  void *check_url_baton,
                                  svn_client_ctx_t *ctx,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool);

/* Rewrite the COMMIT_ITEMS array to be sorted by URL.  Also, discover
   a common *BASE_URL for the items in the array, and rewrite those
   items' URLs to be relative to that *BASE_URL.

   COMMIT_ITEMS is an array of (svn_client_commit_item3_t *) items.

   Afterwards, some of the items in COMMIT_ITEMS may contain data
   allocated in POOL. */
svn_error_t *
svn_client__condense_commit_items(const char **base_url,
                                  apr_array_header_t *commit_items,
                                  apr_pool_t *pool);

/* Commit the items in the COMMIT_ITEMS array using EDITOR/EDIT_BATON
   to describe the committed local mods.  Prior to this call,
   COMMIT_ITEMS should have been run through (and BASE_URL generated
   by) svn_client__condense_commit_items().

   COMMIT_ITEMS is an array of (svn_client_commit_item3_t *) items.

   CTX->NOTIFY_FUNC/CTX->BATON will be called as the commit progresses, as
   a way of describing actions to the application layer (if non NULL).

   NOTIFY_PATH_PREFIX will be passed to CTX->notify_func2() as the
   common absolute path prefix of the committed paths.  It can be NULL.

   If SHA1_CHECKSUMS is not NULL, set *SHA1_CHECKSUMS to a hash containing,
   for each file transmitted, a mapping from the commit-item's (const
   char *) path to the (const svn_checksum_t *) SHA1 checksum of its new text
   base.

   Use RESULT_POOL for all allocating the resulting hashes and SCRATCH_POOL
   for temporary allocations.
   */
svn_error_t *
svn_client__do_commit(const char *base_url,
                      const apr_array_header_t *commit_items,
                      const svn_delta_editor_t *editor,
                      void *edit_baton,
                      const char *notify_path_prefix,
                      apr_hash_t **sha1_checksums,
                      svn_client_ctx_t *ctx,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);




/*** Externals (Modules) ***/

/* Handle changes to the svn:externals property described by EXTERNALS_NEW,
   and AMBIENT_DEPTHS.  The tree's top level directory
   is at TARGET_ABSPATH which has a root URL of REPOS_ROOT_URL.
   A write lock should be held.

   For each changed value of the property, discover the nature of the
   change and behave appropriately -- either check a new "external"
   subdir, or call svn_wc_remove_from_revision_control() on an
   existing one, or both.

   TARGET_ABSPATH is the root of the driving operation and
   REQUESTED_DEPTH is the requested depth of the driving operation
   (e.g., update, switch, etc).  If it is neither svn_depth_infinity
   nor svn_depth_unknown, then changes to svn:externals will have no
   effect.  If REQUESTED_DEPTH is svn_depth_unknown, then the ambient
   depth of each working copy directory holding an svn:externals value
   will determine whether that value is interpreted there (the ambient
   depth must be svn_depth_infinity).  If REQUESTED_DEPTH is
   svn_depth_infinity, then it is presumed to be expanding any
   shallower ambient depth, so changes to svn:externals values will be
   interpreted.

   Pass NOTIFY_FUNC with NOTIFY_BATON along to svn_client_checkout().

   Set *TIMESTAMP_SLEEP to TRUE if a sleep is required; otherwise do not
   change *TIMESTAMP_SLEEP.  The output will be valid even if the function
   returns an error.

   If RA_SESSION is NOT NULL, it may be used to avoid creating a new
   session. The session may point to a different URL after returning.

   Use POOL for temporary allocation. */
svn_error_t *
svn_client__handle_externals(apr_hash_t *externals_new,
                             apr_hash_t *ambient_depths,
                             const char *repos_root_url,
                             const char *target_abspath,
                             svn_depth_t requested_depth,
                             svn_boolean_t *timestamp_sleep,
                             svn_ra_session_t *ra_session,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool);


/* Export externals definitions described by EXTERNALS, a hash of the
   form returned by svn_wc_edited_externals() (which see). The external
   items will be exported instead of checked out -- they will have no
   administrative subdirectories.

   The checked out or exported tree's top level directory is at
   TO_ABSPATH and corresponds to FROM_URL URL in the repository, which
   has a root URL of REPOS_ROOT_URL.

   REQUESTED_DEPTH is the requested_depth of the driving operation; it
   behaves as for svn_client__handle_externals(), except that ambient
   depths are presumed to be svn_depth_infinity.

   NATIVE_EOL is the value passed as NATIVE_EOL when exporting.

   Use POOL for temporary allocation. */
svn_error_t *
svn_client__export_externals(apr_hash_t *externals,
                             const char *from_url,
                             const char *to_abspath,
                             const char *repos_root_url,
                             svn_depth_t requested_depth,
                             const char *native_eol,
                             svn_boolean_t ignore_keywords,
                             svn_client_ctx_t *ctx,
                             apr_pool_t *pool);

/* Baton for svn_client__dirent_fetcher */
struct svn_client__dirent_fetcher_baton_t
{
  svn_ra_session_t *ra_session;
  svn_revnum_t target_revision;
  const char *anchor_url;
};

/* Implements svn_wc_dirents_func_t for update and switch handling. Assumes
   a struct svn_client__dirent_fetcher_baton_t * baton */
svn_error_t *
svn_client__dirent_fetcher(void *baton,
                           apr_hash_t **dirents,
                           const char *repos_root_url,
                           const char *repos_relpath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Retrieve log messages using the first provided (non-NULL) callback
   in the set of *CTX->log_msg_func3, CTX->log_msg_func2, or
   CTX->log_msg_func.  Other arguments same as
   svn_client_get_commit_log3_t. */
svn_error_t *
svn_client__get_log_msg(const char **log_msg,
                        const char **tmp_file,
                        const apr_array_header_t *commit_items,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *pool);

/* Return the revision properties stored in REVPROP_TABLE_IN, adding
   LOG_MSG as SVN_PROP_REVISION_LOG in *REVPROP_TABLE_OUT, allocated in
   POOL.  *REVPROP_TABLE_OUT will map const char * property names to
   svn_string_t values.  If REVPROP_TABLE_IN is non-NULL, check that
   it doesn't contain any of the standard Subversion properties.  In
   that case, return SVN_ERR_CLIENT_PROPERTY_NAME. */
svn_error_t *
svn_client__ensure_revprop_table(apr_hash_t **revprop_table_out,
                                 const apr_hash_t *revprop_table_in,
                                 const char *log_msg,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *pool);

/* Return a potentially translated version of local file LOCAL_ABSPATH
   in NORMAL_STREAM.  REVISION must be one of the following: BASE, COMMITTED,
   WORKING.

   EXPAND_KEYWORDS operates as per the EXPAND argument to
   svn_subst_stream_translated, which see.  If NORMALIZE_EOLS is TRUE and
   LOCAL_ABSPATH requires translation, then normalize the line endings in
   *NORMAL_STREAM to "\n" if the stream has svn:eol-style set.

   Note that this IS NOT the repository normal form of the stream as that
   would use "\r\n" if set to CRLF and "\r" if set to CR.

   The stream is allocated in RESULT_POOL and temporary SCRATCH_POOL is
   used for temporary allocations. */
svn_error_t *
svn_client__get_normalized_stream(svn_stream_t **normal_stream,
                                  svn_wc_context_t *wc_ctx,
                                  const char *local_abspath,
                                  const svn_opt_revision_t *revision,
                                  svn_boolean_t expand_keywords,
                                  svn_boolean_t normalize_eols,
                                  svn_cancel_func_t cancel_func,
                                  void *cancel_baton,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool);

/* Return a set of callbacks to use with the Ev2 shims. */
svn_delta_shim_callbacks_t *
svn_client__get_shim_callbacks(svn_wc_context_t *wc_ctx,
                               apr_hash_t *relpath_map,
                               apr_pool_t *result_pool);

/* Return REVISION unless its kind is 'unspecified' in which case return
 * a pointer to a statically allocated revision structure of kind 'head'
 * if PATH_OR_URL is a URL or 'base' if it is a WC path. */
const svn_opt_revision_t *
svn_cl__rev_default_to_head_or_base(const svn_opt_revision_t *revision,
                                    const char *path_or_url);

/* Return REVISION unless its kind is 'unspecified' in which case return
 * a pointer to a statically allocated revision structure of kind 'head'
 * if PATH_OR_URL is a URL or 'working' if it is a WC path. */
const svn_opt_revision_t *
svn_cl__rev_default_to_head_or_working(const svn_opt_revision_t *revision,
                                       const char *path_or_url);

/* Return REVISION unless its kind is 'unspecified' in which case return
 * PEG_REVISION. */
const svn_opt_revision_t *
svn_cl__rev_default_to_peg(const svn_opt_revision_t *revision,
                           const svn_opt_revision_t *peg_revision);

/* Call the conflict resolver callback in CTX for each conflict recorded
 * in CONFLICTED_PATHS (const char *abspath keys; ignored values).  If
 * CONFLICTS_REMAIN is not NULL, then set *CONFLICTS_REMAIN to true if
 * there are any conflicts among CONFLICTED_PATHS remaining unresolved
 * at the end of this operation, else set it to false.
 */
svn_error_t *
svn_client__resolve_conflicts(svn_boolean_t *conflicts_remain,
                              apr_hash_t *conflicted_paths,
                              svn_client_ctx_t *ctx,
                              apr_pool_t *scratch_pool);

/* Produce a diff with depth DEPTH between two files or two directories at
 * LEFT_ABSPATH1 and RIGHT_ABSPATH, using the provided diff callbacks to
 * show changes in files. The files and directories involved may be part of
 * a working copy or they may be unversioned. For versioned files, show
 * property changes, too.
 *
 * If ANCHOR_ABSPATH is not null, set it to the anchor of the diff before
 * the first processor call. (The anchor is LEFT_ABSPATH or an ancestor of it)
 */
svn_error_t *
svn_client__arbitrary_nodes_diff(const char **root_relpath,
                                 svn_boolean_t *root_is_dir,
                                 const char *left_abspath,
                                 const char *right_abspath,
                                 svn_depth_t depth,
                                 const svn_diff_tree_processor_t *diff_processor,
                                 svn_client_ctx_t *ctx,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);


/* Helper for the remote case of svn_client_propget.
 *
 * If PROPS is not null, then get the value of property PROPNAME in
 * REVNUM, using RA_SESSION.  Store the value ('svn_string_t *') in
 * PROPS, under the path key "TARGET_PREFIX/TARGET_RELATIVE"
 * ('const char *').
 *
 * If INHERITED_PROPS is not null, then set *INHERITED_PROPS to a
 * depth-first ordered array of svn_prop_inherited_item_t * structures
 * representing the PROPNAME properties inherited by the target.  If
 * INHERITABLE_PROPS in not null and no inheritable properties are found,
 * then set *INHERITED_PROPS to an empty array.
 *
 * Recurse according to DEPTH, similarly to svn_client_propget3().
 *
 * KIND is the kind of the node at "TARGET_PREFIX/TARGET_RELATIVE".
 * Yes, caller passes this; it makes the recursion more efficient :-).
 *
 * Allocate PROPS and *INHERITED_PROPS in RESULT_POOL, but do all temporary
 * work in SCRATCH_POOL.  The two pools can be the same; recursive
 * calls may use a different SCRATCH_POOL, however.
 */
svn_error_t *
svn_client__remote_propget(apr_hash_t *props,
                           apr_array_header_t **inherited_props,
                           const char *propname,
                           const char *target_prefix,
                           const char *target_relative,
                           svn_node_kind_t kind,
                           svn_revnum_t revnum,
                           svn_ra_session_t *ra_session,
                           svn_depth_t depth,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* */
typedef struct merge_source_t
{
  /* "left" side URL and revision (inclusive iff youngest) */
  const svn_client__pathrev_t *loc1;

  /* "right" side URL and revision (inclusive iff youngest) */
  const svn_client__pathrev_t *loc2;

  /* True iff LOC1 is an ancestor of LOC2 or vice-versa (history-wise). */
  svn_boolean_t ancestral;
} merge_source_t;

/* Description of the merge target root node (a WC working node) */
typedef struct merge_target_t
{
  /* Absolute path to the WC node */
  const char *abspath;

  /* The repository location of the base node of the target WC.  If the node
   * is locally added, then URL & REV are NULL & SVN_INVALID_REVNUM.
   * REPOS_ROOT_URL and REPOS_UUID are always valid. */
  svn_client__pathrev_t loc;

} merge_target_t;

/*
 * Similar API to svn_client_merge_peg5().
 */
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
                           apr_pool_t *scratch_pool);

/* Data for reporting when a merge aborted because of raising conflicts.
 *
 * ### TODO: More info, including the ranges (or other parameters) the user
 *     needs to complete the merge.
 */
typedef struct svn_client__conflict_report_t
{
  const char *target_abspath;
  /* The revision range during which conflicts were raised */
  const merge_source_t *conflicted_range;
  /* Was the conflicted range the last range in the whole requested merge? */
  svn_boolean_t was_last_range;
} svn_client__conflict_report_t;

/* Create and return an error structure appropriate for the unmerged
   revisions range(s). */
svn_error_t *
svn_client__make_merge_conflict_error(svn_client__conflict_report_t *report,
                                      apr_pool_t *scratch_pool);

/* The body of svn_client_merge5(), which see for details. */
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
                         apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_CLIENT_H */
