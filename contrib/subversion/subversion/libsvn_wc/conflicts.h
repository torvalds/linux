/*
 * conflicts.h: declarations related to conflicts
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

#ifndef SVN_WC_CONFLICTS_H
#define SVN_WC_CONFLICTS_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_wc.h"

#include "wc_db.h"
#include "private/svn_skel.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



#define SVN_WC__CONFLICT_OP_UPDATE "update"
#define SVN_WC__CONFLICT_OP_SWITCH "switch"
#define SVN_WC__CONFLICT_OP_MERGE "merge"
#define SVN_WC__CONFLICT_OP_PATCH "patch"

#define SVN_WC__CONFLICT_KIND_TEXT "text"
#define SVN_WC__CONFLICT_KIND_PROP "prop"
#define SVN_WC__CONFLICT_KIND_TREE "tree"
#define SVN_WC__CONFLICT_KIND_REJECT "reject"
#define SVN_WC__CONFLICT_KIND_OBSTRUCTED "obstructed"

#define SVN_WC__CONFLICT_SRC_SUBVERSION "subversion"

/* Return a new conflict skel, allocated in RESULT_POOL.

   Typically creating a conflict starts with calling this function and then
   collecting details via one or more calls to svn_wc__conflict_skel_add_*().

   The caller can then (when necessary) add operation details via
   svn_wc__conflict_skel_set_op_*() and store the resulting conflict together
   with the result of its operation in the working copy database.
*/
svn_skel_t *
svn_wc__conflict_skel_create(apr_pool_t *result_pool);

/* Return a boolean in *COMPLETE indicating whether CONFLICT_SKEL contains
   everything needed for installing in the working copy database.

   This typically checks if CONFLICT_SKEL contains at least one conflict
   and an operation.
 */
svn_error_t *
svn_wc__conflict_skel_is_complete(svn_boolean_t *complete,
                                  const svn_skel_t *conflict_skel);


/* Set 'update' as the conflicting operation in CONFLICT_SKEL.
   Allocate data stored in the skel in RESULT_POOL.

   ORIGINAL and TARGET specify the BASE node before and after updating.

   It is an error to set another operation to a conflict skel that
   already has an operation.

   Do temporary allocations in SCRATCH_POOL. The new skel data is
   completely stored in RESULT-POOL. */
svn_error_t *
svn_wc__conflict_skel_set_op_update(svn_skel_t *conflict_skel,
                                    const svn_wc_conflict_version_t *original,
                                    const svn_wc_conflict_version_t *target,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);


/* Set 'switch' as the conflicting operation in CONFLICT_SKEL.
   Allocate data stored in the skel in RESULT_POOL.

   ORIGINAL and TARGET specify the BASE node before and after switching.

   It is an error to set another operation to a conflict skel that
   already has an operation.

   Do temporary allocations in SCRATCH_POOL. */
svn_error_t *
svn_wc__conflict_skel_set_op_switch(svn_skel_t *conflict_skel,
                                    const svn_wc_conflict_version_t *original,
                                    const svn_wc_conflict_version_t *target,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);


/* Set 'merge' as conflicting operation in CONFLICT_SKEL.
   Allocate data stored in the skel in RESULT_POOL.

   LEFT and RIGHT paths are the merge-left and merge-right merge
   sources of the merge.

   It is an error to set another operation to a conflict skel that
   already has an operation.

   Do temporary allocations in SCRATCH_POOL. */
svn_error_t *
svn_wc__conflict_skel_set_op_merge(svn_skel_t *conflict_skel,
                                   const svn_wc_conflict_version_t *left,
                                   const svn_wc_conflict_version_t *right,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool);


/* Add a text conflict to CONFLICT_SKEL.
   Allocate data stored in the skel in RESULT_POOL.

   The DB, WRI_ABSPATH pair specifies in which working copy the conflict
   will be recorded. (Needed for making the paths relative).

   MINE_ABSPATH, THEIR_OLD_ABSPATH and THEIR_ABSPATH specify the marker
   files for this text conflict. Each of these values can be NULL to specify
   that the node doesn't exist in this case.

   ### It is expected that in a future version we will also want to store
   ### the sha1 checksum of these files to allow reinstalling the conflict
   ### markers from the pristine store.

   It is an error to add another text conflict to a conflict skel that
   already contains a text conflict.

   Do temporary allocations in SCRATCH_POOL.
*/
svn_error_t *
svn_wc__conflict_skel_add_text_conflict(svn_skel_t *conflict_skel,
                                        svn_wc__db_t *db,
                                        const char *wri_abspath,
                                        const char *mine_abspath,
                                        const char *their_old_abspath,
                                        const char *their_abspath,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool);


/* Add property conflict details to CONFLICT_SKEL.
   Allocate data stored in the skel in RESULT_POOL.

   The DB, WRI_ABSPATH pair specifies in which working copy the conflict
   will be recorded. (Needed for making the paths relative).

   The MARKER_ABSPATH is NULL when raising a conflict in v1.8+.  See below.

   The MINE_PROPS, THEIR_OLD_PROPS and THEIR_PROPS are hashes mapping a
   const char * property name to a const svn_string_t* value.

   The CONFLICTED_PROP_NAMES is a const char * property name value mapping
   to "", recording which properties aren't resolved yet in the current
   property values.
   ### Needed for creating the marker file from this conflict data.
   ### Would also allow per property marking as resolved.
   ### Maybe useful for calling (legacy) conflict resolvers that expect one
   ### property conflict per invocation.

   When raising a property conflict in the course of upgrading an old WC,
   MARKER_ABSPATH is the path to the file containing a human-readable
   description of the conflict, MINE_PROPS and THEIR_OLD_PROPS and
   THEIR_PROPS are all NULL, and CONFLICTED_PROP_NAMES is an empty hash.

   It is an error to add another prop conflict to a conflict skel that
   already contains a prop conflict.  (A single call to this function can
   record that multiple properties are in conflict.)

   Do temporary allocations in SCRATCH_POOL.
*/
svn_error_t *
svn_wc__conflict_skel_add_prop_conflict(svn_skel_t *conflict_skel,
                                        svn_wc__db_t *db,
                                        const char *wri_abspath,
                                        const char *marker_abspath,
                                        const apr_hash_t *mine_props,
                                        const apr_hash_t *their_old_props,
                                        const apr_hash_t *their_props,
                                        const apr_hash_t *conflicted_prop_names,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool);


/* Add a tree conflict to CONFLICT_SKEL.
   Allocate data stored in the skel in RESULT_POOL.

   LOCAL_CHANGE is the local tree change made to the node.
   INCOMING_CHANGE is the incoming change made to the node.

   MOVE_SRC_OP_ROOT_ABSPATH must be set when LOCAL_CHANGE is
   svn_wc_conflict_reason_moved_away and NULL otherwise and the operation
   is svn_wc_operation_update or svn_wc_operation_switch.  It should be
   set to the op-root of the move-away unless the move is inside a
   delete in which case it should be set to the op-root of the delete
   (the delete can be a replace). So given:
       A/B/C moved away (1)
       A deleted and replaced
       A/B/C moved away (2)
       A/B deleted
   MOVE_SRC_OP_ROOT_ABSPATH should be A for a conflict associated
   with (1), MOVE_SRC_OP_ROOT_ABSPATH should be A/B for a conflict
   associated with (2).

   It is an error to add another tree conflict to a conflict skel that
   already contains a tree conflict.  (It is not an error, at this level,
   to add a tree conflict to an existing text or property conflict skel.)

   Do temporary allocations in SCRATCH_POOL.
*/
svn_error_t *
svn_wc__conflict_skel_add_tree_conflict(svn_skel_t *conflict_skel,
                                        svn_wc__db_t *db,
                                        const char *wri_abspath,
                                        svn_wc_conflict_reason_t local_change,
                                        svn_wc_conflict_action_t incoming_change,
                                        const char *move_src_op_root_abspath,
                                        apr_pool_t *result_pool,
                                        apr_pool_t *scratch_pool);

/* Allows resolving specific conflicts stored in CONFLICT_SKEL.

   When RESOLVE_TEXT is TRUE and CONFLICT_SKEL contains a text conflict,
   resolve/remove the text conflict in CONFLICT_SKEL.

   When RESOLVE_PROP is "" and CONFLICT_SKEL contains a property conflict,
   resolve/remove all property conflicts in CONFLICT_SKEL.

   When RESOLVE_PROP is not NULL and not "", remove the property conflict on
   the property RESOLVE_PROP in CONFLICT_SKEL. When RESOLVE_PROP was the last
   property in CONFLICT_SKEL remove the property conflict info from
   CONFLICT_SKEL.

   When RESOLVE_TREE is TRUE and CONFLICT_SKEL contains a tree conflict,
   resolve/remove the tree conflict in CONFLICT_SKEL.

   If COMPLETELY_RESOLVED is not NULL, then set *COMPLETELY_RESOLVED to TRUE,
   when no conflict registration is left in CONFLICT_SKEL after editting,
   otherwise to FALSE.

   Allocate data stored in the skel in RESULT_POOL.

   This functions edits CONFLICT_SKEL. New skels might be created in
   RESULT_POOL. Temporary allocations will use SCRATCH_POOL.
 */
/* ### db, wri_abspath is currently unused. Remove? */
svn_error_t *
svn_wc__conflict_skel_resolve(svn_boolean_t *completely_resolved,
                              svn_skel_t *conflict_skel,
                              svn_wc__db_t *db,
                              const char *wri_abspath,
                              svn_boolean_t resolve_text,
                              const char *resolve_prop,
                              svn_boolean_t resolve_tree,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/*
 * -----------------------------------------------------------
 * Reading conflict skels. Maybe this can be made private later
 * -----------------------------------------------------------
 */

/* Read common information from CONFLICT_SKEL to determine the operation
 * and merge origins.
 *
 * Output arguments can be NULL if the value is not necessary.
 *
 * Set *LOCATIONS to an array of (svn_wc_conflict_version_t *).  For
 * conflicts written by current code, there are 2 elements: index [0] is
 * the 'old' or 'left' side and [1] is the 'new' or 'right' side.
 *
 * For conflicts written by 1.6 or 1.7 there are 2 locations for a tree
 * conflict, but none for a text or property conflict.
 *
 * TEXT_, PROP_ and TREE_CONFLICTED (when not NULL) will be set to TRUE
 * when the conflict contains the specified kind of conflict, otherwise
 * to false.
 *
 * Allocate the result in RESULT_POOL. Perform temporary allocations in
 * SCRATCH_POOL.
 */
svn_error_t *
svn_wc__conflict_read_info(svn_wc_operation_t *operation,
                           const apr_array_header_t **locations,
                           svn_boolean_t *text_conflicted,
                           svn_boolean_t *prop_conflicted,
                           svn_boolean_t *tree_conflicted,
                           svn_wc__db_t *db,
                           const char *wri_abspath,
                           const svn_skel_t *conflict_skel,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool);

/* Reads back the original data stored by svn_wc__conflict_skel_add_text_conflict()
 * in CONFLICT_SKEL for a node in DB, WRI_ABSPATH.
 *
 * Values as documented for svn_wc__conflict_skel_add_text_conflict().
 *
 * Output arguments can be NULL if the value is not necessary.
 *
 * Allocate the result in RESULT_POOL. Perform temporary allocations in
 * SCRATCH_POOL.
 */
svn_error_t *
svn_wc__conflict_read_text_conflict(const char **mine_abspath,
                                    const char **their_old_abspath,
                                    const char **their_abspath,
                                    svn_wc__db_t *db,
                                    const char *wri_abspath,
                                    const svn_skel_t *conflict_skel,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);

/* Reads back the original data stored by svn_wc__conflict_skel_add_prop_conflict()
 * in CONFLICT_SKEL for a node in DB, WRI_ABSPATH.
 *
 * Values as documented for svn_wc__conflict_skel_add_prop_conflict().
 *
 * Output arguments can be NULL if the value is not necessary
 * Allocate the result in RESULT_POOL. Perform temporary allocations in
 * SCRATCH_POOL.
 */
svn_error_t *
svn_wc__conflict_read_prop_conflict(const char **marker_abspath,
                                    apr_hash_t **mine_props,
                                    apr_hash_t **their_old_props,
                                    apr_hash_t **their_props,
                                    apr_hash_t **conflicted_prop_names,
                                    svn_wc__db_t *db,
                                    const char *wri_abspath,
                                    const svn_skel_t *conflict_skel,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);

/* Reads back the original data stored by svn_wc__conflict_skel_add_tree_conflict()
 * in CONFLICT_SKEL for a node in DB, WRI_ABSPATH.
 *
 * Values as documented for svn_wc__conflict_skel_add_tree_conflict().
 *
 * Output arguments can be NULL if the value is not necessary
 * Allocate the result in RESULT_POOL. Perform temporary allocations in
 * SCRATCH_POOL.
 */
svn_error_t *
svn_wc__conflict_read_tree_conflict(svn_wc_conflict_reason_t *local_change,
                                    svn_wc_conflict_action_t *incoming_change,
                                    const char **move_src_op_root_abspath,
                                    svn_wc__db_t *db,
                                    const char *wri_abspath,
                                    const svn_skel_t *conflict_skel,
                                    apr_pool_t *result_pool,
                                    apr_pool_t *scratch_pool);

/* Reads in *MARKERS a list of const char * absolute paths of the marker files
   referenced from CONFLICT_SKEL.
 * Allocate the result in RESULT_POOL. Perform temporary allocations in
 * SCRATCH_POOL.
 */
svn_error_t *
svn_wc__conflict_read_markers(const apr_array_header_t **markers,
                              svn_wc__db_t *db,
                              const char *wri_abspath,
                              const svn_skel_t *conflict_skel,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool);

/* Create the necessary marker files for the conflicts stored in
 * CONFLICT_SKEL and return the work items to fill the markers from
 * the work queue.
 *
 * Currently only used for property conflicts as text conflict markers
 * are just in-wc files.
 *
 * Allocate the result in RESULT_POOL. Perform temporary allocations in
 * SCRATCH_POOL.
 */
svn_error_t *
svn_wc__conflict_create_markers(svn_skel_t **work_item,
                                svn_wc__db_t *db,
                                const char *local_abspath,
                                svn_skel_t *conflict_skel,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool);

/* Call the conflict resolver RESOLVER_FUNC with RESOLVER_BATON for each
   of the conflicts on LOCAL_ABSPATH.  Depending on the results that
   the callback returns, perhaps resolve the conflicts, and perhaps mark
   them as resolved in the WC DB.

   Call RESOLVER_FUNC once for each property conflict, and again for any
   text conflict, and again for any tree conflict on the node.

   CONFLICT_SKEL contains the details of the conflicts on LOCAL_ABSPATH.

   Use MERGE_OPTIONS when the resolver requests a merge.

   Resolver actions are directly applied to the in-db state of LOCAL_ABSPATH,
   so the conflict and the state in CONFLICT_SKEL must already be installed in
   wc.db. */
svn_error_t *
svn_wc__conflict_invoke_resolver(svn_wc__db_t *db,
                                 const char *local_abspath,
                                 svn_node_kind_t kind,
                                 const svn_skel_t *conflict_skel,
                                 const apr_array_header_t *merge_options,
                                 svn_wc_conflict_resolver_func2_t resolver_func,
                                 void *resolver_baton,
                                 svn_cancel_func_t cancel_func,
                                 void *cancel_baton,
                                 apr_pool_t *scratch_pool);


/* Mark as resolved any text conflict on the node at DB/LOCAL_ABSPATH.  */
svn_error_t *
svn_wc__mark_resolved_text_conflict(svn_wc__db_t *db,
                                    const char *local_abspath,
                                    svn_cancel_func_t cancel_func,
                                    void *cancel_baton,
                                    apr_pool_t *scratch_pool);

/* Mark as resolved any prop conflicts on the node at DB/LOCAL_ABSPATH.  */
svn_error_t *
svn_wc__mark_resolved_prop_conflicts(svn_wc__db_t *db,
                                     const char *local_abspath,
                                     apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_CONFLICTS_H */
