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
 * @file ra_loader.h
 * @brief structures related to repository access, private to libsvn_ra and the
 * RA implementation libraries.
 */



#ifndef LIBSVN_RA_RA_LOADER_H
#define LIBSVN_RA_RA_LOADER_H

#include "svn_ra.h"

#include "private/svn_ra_private.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Prototype of most recent version of svn_ra_openX() api, optionally
   handed to the ra api to allow opening other ra sessions. */
typedef svn_error_t * (*svn_ra__open_func_t)(svn_ra_session_t **session_p,
                                              const char **corrected_url,
                                              const char *repos_URL,
                                              const char *uuid,
                                              const svn_ra_callbacks2_t *callbacks,
                                              void *callback_baton,
                                              apr_hash_t *config,
                                              apr_pool_t *pool);

/* The RA layer vtable. */
typedef struct svn_ra__vtable_t {
  /* This field should always remain first in the vtable. */
  const svn_version_t *(*get_version)(void);

  /* Return a short description of the RA implementation, as a localized
   * string. */
  const char *(*get_description)(apr_pool_t *pool);

  /* Return a list of actual URI schemes supported by this implementation.
   * The returned array is NULL-terminated. */
  const char * const *(*get_schemes)(apr_pool_t *pool);

  /* Implementations of the public API functions. */

  /* See svn_ra_open4(). */
  /* All fields in SESSION, except priv, have been initialized by the
     time this is called.  SESSION->priv may be set by this function. */
  svn_error_t *(*open_session)(svn_ra_session_t *session,
                               const char **corrected_url,
                               const char *session_URL,
                               const svn_ra_callbacks2_t *callbacks,
                               void *callback_baton,
                               svn_auth_baton_t *auth_baton,
                               apr_hash_t *config,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool);
  /* Backs svn_ra_dup_session */
  svn_error_t * (*dup_session)(svn_ra_session_t *new_session,
                               svn_ra_session_t *old_session,
                               const char *new_session_url,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool);
  /* See svn_ra_reparent(). */
  /* URL is guaranteed to have what get_repos_root() returns as a prefix. */
  svn_error_t *(*reparent)(svn_ra_session_t *session,
                           const char *url,
                           apr_pool_t *pool);
  /* See svn_ra_get_session_url(). */
  svn_error_t *(*get_session_url)(svn_ra_session_t *session,
                                  const char **url,
                                  apr_pool_t *pool);
  /* See svn_ra_get_latest_revnum(). */
  svn_error_t *(*get_latest_revnum)(svn_ra_session_t *session,
                                    svn_revnum_t *latest_revnum,
                                    apr_pool_t *pool);
  /* See svn_ra_get_dated_revision(). */
  svn_error_t *(*get_dated_revision)(svn_ra_session_t *session,
                                     svn_revnum_t *revision,
                                     apr_time_t tm,
                                     apr_pool_t *pool);
  /* See svn_ra_change_rev_prop2(). */
  svn_error_t *(*change_rev_prop)(svn_ra_session_t *session,
                                  svn_revnum_t rev,
                                  const char *name,
                                  const svn_string_t *const *old_value_p,
                                  const svn_string_t *value,
                                  apr_pool_t *pool);

  /* See svn_ra_rev_proplist(). */
  svn_error_t *(*rev_proplist)(svn_ra_session_t *session,
                               svn_revnum_t rev,
                               apr_hash_t **props,
                               apr_pool_t *pool);
  /* See svn_ra_rev_prop(). */
  svn_error_t *(*rev_prop)(svn_ra_session_t *session,
                           svn_revnum_t rev,
                           const char *name,
                           svn_string_t **value,
                           apr_pool_t *pool);
  /* See svn_ra_get_commit_editor3(). */
  svn_error_t *(*get_commit_editor)(svn_ra_session_t *session,
                                    const svn_delta_editor_t **editor,
                                    void **edit_baton,
                                    apr_hash_t *revprop_table,
                                    svn_commit_callback2_t callback,
                                    void *callback_baton,
                                    apr_hash_t *lock_tokens,
                                    svn_boolean_t keep_locks,
                                    apr_pool_t *pool);
  /* See svn_ra_get_file(). */
  svn_error_t *(*get_file)(svn_ra_session_t *session,
                           const char *path,
                           svn_revnum_t revision,
                           svn_stream_t *stream,
                           svn_revnum_t *fetched_rev,
                           apr_hash_t **props,
                           apr_pool_t *pool);
  /* See svn_ra_get_dir2(). */
  svn_error_t *(*get_dir)(svn_ra_session_t *session,
                          apr_hash_t **dirents,
                          svn_revnum_t *fetched_rev,
                          apr_hash_t **props,
                          const char *path,
                          svn_revnum_t revision,
                          apr_uint32_t dirent_fields,
                          apr_pool_t *pool);
  /* See svn_ra_get_mergeinfo(). */
  svn_error_t *(*get_mergeinfo)(svn_ra_session_t *session,
                                svn_mergeinfo_catalog_t *mergeinfo,
                                const apr_array_header_t *paths,
                                svn_revnum_t revision,
                                svn_mergeinfo_inheritance_t inherit,
                                svn_boolean_t include_merged_revisions,
                                apr_pool_t *pool);
  /* See svn_ra_do_update3(). */
  svn_error_t *(*do_update)(svn_ra_session_t *session,
                            const svn_ra_reporter3_t **reporter,
                            void **report_baton,
                            svn_revnum_t revision_to_update_to,
                            const char *update_target,
                            svn_depth_t depth,
                            svn_boolean_t send_copyfrom_args,
                            svn_boolean_t ignore_ancestry,
                            const svn_delta_editor_t *update_editor,
                            void *update_baton,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);
  /* See svn_ra_do_switch3(). */
  svn_error_t *(*do_switch)(svn_ra_session_t *session,
                            const svn_ra_reporter3_t **reporter,
                            void **report_baton,
                            svn_revnum_t revision_to_switch_to,
                            const char *switch_target,
                            svn_depth_t depth,
                            const char *switch_url,
                            svn_boolean_t send_copyfrom_args,
                            svn_boolean_t ignore_ancestry,
                            const svn_delta_editor_t *switch_editor,
                            void *switch_baton,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool);
  /* See svn_ra_do_status2(). */
  svn_error_t *(*do_status)(svn_ra_session_t *session,
                            const svn_ra_reporter3_t **reporter,
                            void **report_baton,
                            const char *status_target,
                            svn_revnum_t revision,
                            svn_depth_t depth,
                            const svn_delta_editor_t *status_editor,
                            void *status_baton,
                            apr_pool_t *pool);
  /* See svn_ra_do_diff3(). */
  svn_error_t *(*do_diff)(svn_ra_session_t *session,
                          const svn_ra_reporter3_t **reporter,
                          void **report_baton,
                          svn_revnum_t revision,
                          const char *diff_target,
                          svn_depth_t depth,
                          svn_boolean_t ignore_ancestry,
                          svn_boolean_t text_deltas,
                          const char *versus_url,
                          const svn_delta_editor_t *diff_editor,
                          void *diff_baton,
                          apr_pool_t *pool);
  /* See svn_ra_get_log2(). */
  svn_error_t *(*get_log)(svn_ra_session_t *session,
                          const apr_array_header_t *paths,
                          svn_revnum_t start,
                          svn_revnum_t end,
                          int limit,
                          svn_boolean_t discover_changed_paths,
                          svn_boolean_t strict_node_history,
                          svn_boolean_t include_merged_revisions,
                          const apr_array_header_t *revprops,
                          svn_log_entry_receiver_t receiver,
                          void *receiver_baton,
                          apr_pool_t *pool);
  /* See svn_ra_check_path(). */
  svn_error_t *(*check_path)(svn_ra_session_t *session,
                             const char *path,
                             svn_revnum_t revision,
                             svn_node_kind_t *kind,
                             apr_pool_t *pool);
  /* See svn_ra_stat(). */
  svn_error_t *(*stat)(svn_ra_session_t *session,
                       const char *path,
                       svn_revnum_t revision,
                       svn_dirent_t **dirent,
                       apr_pool_t *pool);
  /* See svn_ra_get_uuid2(). */
  svn_error_t *(*get_uuid)(svn_ra_session_t *session,
                           const char **uuid,
                           apr_pool_t *pool);
  /* See svn_ra_get_repos_root2(). */
  svn_error_t *(*get_repos_root)(svn_ra_session_t *session,
                                 const char **url,
                                 apr_pool_t *pool);
  /* See svn_ra_get_locations(). */
  svn_error_t *(*get_locations)(svn_ra_session_t *session,
                                apr_hash_t **locations,
                                const char *path,
                                svn_revnum_t peg_revision,
                                const apr_array_header_t *location_revisions,
                                apr_pool_t *pool);
  /* See svn_ra_get_location_segments(). */
  svn_error_t *(*get_location_segments)(svn_ra_session_t *session,
                                        const char *path,
                                        svn_revnum_t peg_revision,
                                        svn_revnum_t start_rev,
                                        svn_revnum_t end_rev,
                                        svn_location_segment_receiver_t rcvr,
                                        void *receiver_baton,
                                        apr_pool_t *pool);
  /* See svn_ra_get_file_revs2(). */
  svn_error_t *(*get_file_revs)(svn_ra_session_t *session,
                                const char *path,
                                svn_revnum_t start,
                                svn_revnum_t end,
                                svn_boolean_t include_merged_revisions,
                                svn_file_rev_handler_t handler,
                                void *handler_baton,
                                apr_pool_t *pool);
  /* See svn_ra_lock(). */
  svn_error_t *(*lock)(svn_ra_session_t *session,
                       apr_hash_t *path_revs,
                       const char *comment,
                       svn_boolean_t force,
                       svn_ra_lock_callback_t lock_func,
                       void *lock_baton,
                       apr_pool_t *pool);
  /* See svn_ra_unlock(). */
  svn_error_t *(*unlock)(svn_ra_session_t *session,
                         apr_hash_t *path_tokens,
                         svn_boolean_t force,
                         svn_ra_lock_callback_t lock_func,
                         void *lock_baton,
                         apr_pool_t *pool);
  /* See svn_ra_get_lock(). */
  svn_error_t *(*get_lock)(svn_ra_session_t *session,
                           svn_lock_t **lock,
                           const char *path,
                           apr_pool_t *pool);
  /* See svn_ra_get_locks2(). */
  svn_error_t *(*get_locks)(svn_ra_session_t *session,
                            apr_hash_t **locks,
                            const char *path,
                            svn_depth_t depth,
                            apr_pool_t *pool);
  /* See svn_ra_replay(). */
  svn_error_t *(*replay)(svn_ra_session_t *session,
                         svn_revnum_t revision,
                         svn_revnum_t low_water_mark,
                         svn_boolean_t text_deltas,
                         const svn_delta_editor_t *editor,
                         void *edit_baton,
                         apr_pool_t *pool);
  /* See svn_ra_has_capability(). */
  svn_error_t *(*has_capability)(svn_ra_session_t *session,
                                 svn_boolean_t *has,
                                 const char *capability,
                                 apr_pool_t *pool);
  /* See svn_ra_replay_range(). */
  svn_error_t *
  (*replay_range)(svn_ra_session_t *session,
                  svn_revnum_t start_revision,
                  svn_revnum_t end_revision,
                  svn_revnum_t low_water_mark,
                  svn_boolean_t text_deltas,
                  svn_ra_replay_revstart_callback_t revstart_func,
                  svn_ra_replay_revfinish_callback_t revfinish_func,
                  void *replay_baton,
                  apr_pool_t *pool);
  /* See svn_ra_get_deleted_rev(). */
  svn_error_t *(*get_deleted_rev)(svn_ra_session_t *session,
                                  const char *path,
                                  svn_revnum_t peg_revision,
                                  svn_revnum_t end_revision,
                                  svn_revnum_t *revision_deleted,
                                  apr_pool_t *pool);
  /* See svn_ra_get_inherited_props(). */
  svn_error_t *(*get_inherited_props)(svn_ra_session_t *session,
                                      apr_array_header_t **iprops,
                                      const char *path,
                                      svn_revnum_t revision,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool);
  /* If not NULL, receives a pointer to svn_ra_open, to alllow opening
     a new ra session from inside the ra layer without a circular
     library dependency*/
  svn_error_t *(*set_svn_ra_open)(svn_ra_session_t *session,
                                  svn_ra__open_func_t func);

  /* See svn_ra_list(). */
  svn_error_t *(*list)(svn_ra_session_t *session,
                       const char *path,
                       svn_revnum_t revision,
                       const apr_array_header_t *patterns,
                       svn_depth_t depth,
                       apr_uint32_t dirent_fields,
                       svn_ra_dirent_receiver_t receiver,
                       void *receiver_baton,
                       apr_pool_t *scratch_pool);

  /* Experimental support below here */

  /* See svn_ra__register_editor_shim_callbacks() */
  svn_error_t *(*register_editor_shim_callbacks)(svn_ra_session_t *session,
                                                 svn_delta_shim_callbacks_t *callbacks);
  /* See svn_ra__get_commit_ev2()  */
  svn_error_t *(*get_commit_ev2)(
    svn_editor_t **editor,
    svn_ra_session_t *session,
    apr_hash_t *revprop_table,
    svn_commit_callback2_t callback,
    void *callback_baton,
    apr_hash_t *lock_tokens,
    svn_boolean_t keep_locks,
    svn_ra__provide_base_cb_t provide_base_cb,
    svn_ra__provide_props_cb_t provide_props_cb,
    svn_ra__get_copysrc_kind_cb_t get_copysrc_kind_cb,
    void *cb_baton,
    svn_cancel_func_t cancel_func,
    void *cancel_baton,
    apr_pool_t *result_pool,
    apr_pool_t *scratch_pool);

  /* See svn_ra__replay_range_ev2() */
  svn_error_t *(*replay_range_ev2)(
    svn_ra_session_t *session,
    svn_revnum_t start_revision,
    svn_revnum_t end_revision,
    svn_revnum_t low_water_mark,
    svn_boolean_t send_deltas,
    svn_ra__replay_revstart_ev2_callback_t revstart_func,
    svn_ra__replay_revfinish_ev2_callback_t revfinish_func,
    void *replay_baton,
    apr_pool_t *scratch_pool);

} svn_ra__vtable_t;

/* The RA session object. */
struct svn_ra_session_t {
  const svn_ra__vtable_t *vtable;

  /* Cancellation handlers consumers may want to use. */
  svn_cancel_func_t cancel_func;
  void *cancel_baton;

  /* Pool used to manage this session. */
  apr_pool_t *pool;

  /* Private data for the RA implementation. */
  void *priv;
};

/* Each libsvn_ra_foo defines a function named svn_ra_foo__init of this type.
 *
 * The LOADER_VERSION parameter must remain first in the list, and the
 * function must use the C calling convention on all platforms, so that
 * the init functions can safely read the version parameter.
 *
 * POOL will be available as long as this module is being used.
 *
 * ### need to force this to be __cdecl on Windows... how??
 */
typedef svn_error_t *
(*svn_ra__init_func_t)(const svn_version_t *loader_version,
                       const svn_ra__vtable_t **vtable,
                       apr_pool_t *pool);

/* Declarations of the init functions for the available RA libraries. */
svn_error_t *svn_ra_local__init(const svn_version_t *loader_version,
                                const svn_ra__vtable_t **vtable,
                                apr_pool_t *pool);
svn_error_t *svn_ra_svn__init(const svn_version_t *loader_version,
                              const svn_ra__vtable_t **vtable,
                              apr_pool_t *pool);
svn_error_t *svn_ra_serf__init(const svn_version_t *loader_version,
                               const svn_ra__vtable_t **vtable,
                               apr_pool_t *pool);



/*** Compat Functions ***/

/**
 * Set *LOCATIONS to the locations (at the repository revisions
 * LOCATION_REVISIONS) of the file identified by PATH in PEG_REVISION.
 * PATH is relative to the URL to which SESSION was opened.
 * LOCATION_REVISIONS is an array of svn_revnum_t's.  *LOCATIONS will
 * be a mapping from the revisions to their appropriate absolute
 * paths.  If the file doesn't exist in a location_revision, that
 * revision will be ignored.
 *
 * Use POOL for all allocations.
 *
 * NOTE: This function uses the RA get_log interfaces to do its work,
 * as a fallback mechanism for servers which don't support the native
 * get_locations API.
 */
svn_error_t *
svn_ra__locations_from_log(svn_ra_session_t *session,
                           apr_hash_t **locations_p,
                           const char *path,
                           svn_revnum_t peg_revision,
                           const apr_array_header_t *location_revisions,
                           apr_pool_t *pool);

/**
 * Call RECEIVER (with RECEIVER_BATON) for each segment in the
 * location history of PATH in START_REV, working backwards in time
 * from START_REV to END_REV.
 *
 * END_REV may be SVN_INVALID_REVNUM to indicate that you want to
 * trace the history of the object to its origin.
 *
 * START_REV may be SVN_INVALID_REVNUM to indicate that you want to
 * trace the history of the object beginning in the HEAD revision.
 * Otherwise, START_REV must be younger than END_REV (unless END_REV
 * is SVN_INVALID_REVNUM).
 *
 * Use POOL for all allocations.
 *
 * NOTE: This function uses the RA get_log interfaces to do its work,
 * as a fallback mechanism for servers which don't support the native
 * get_location_segments API.
 */
svn_error_t *
svn_ra__location_segments_from_log(svn_ra_session_t *session,
                                   const char *path,
                                   svn_revnum_t peg_revision,
                                   svn_revnum_t start_rev,
                                   svn_revnum_t end_rev,
                                   svn_location_segment_receiver_t receiver,
                                   void *receiver_baton,
                                   apr_pool_t *pool);

/**
 * Retrieve a subset of the interesting revisions of a file PATH
 * as seen in revision END (see svn_fs_history_prev() for a
 * definition of "interesting revisions").  Invoke HANDLER with
 * @a handler_baton as its first argument for each such revision.
 * @a session is an open RA session.  Use POOL for all allocations.
 *
 * If there is an interesting revision of the file that is less than or
 * equal to START, the iteration will begin at that revision.
 * Else, the iteration will begin at the first revision of the file in
 * the repository, which has to be less than or equal to END.  Note
 * that if the function succeeds, HANDLER will have been called at
 * least once.
 *
 * In a series of calls to HANDLER, the file contents for the first
 * interesting revision will be provided as a text delta against the
 * empty file.  In the following calls, the delta will be against the
 * fulltext contents for the previous call.
 *
 * NOTE: This function uses the RA get_log interfaces to do its work,
 * as a fallback mechanism for servers which don't support the native
 * get_location_segments API.
 */
svn_error_t *
svn_ra__file_revs_from_log(svn_ra_session_t *session,
                           const char *path,
                           svn_revnum_t start,
                           svn_revnum_t end,
                           svn_file_rev_handler_t handler,
                           void *handler_baton,
                           apr_pool_t *pool);


/**
 * Given a path REL_DELETED_PATH, relative to the URL of SESSION, which
 * exists at PEG_REVISION, and an END_REVISION > PEG_REVISION at which
 * REL_DELETED_PATH no longer exists, set *REVISION_DELETED to the revision
 * REL_DELETED_PATH was first deleted or replaced, within the inclusive
 * revision range defined by PEG_REVISION and END_REVISION.
 *
 * If REL_DELETED_PATH does not exist at PEG_REVISION or was not deleted prior
 * to END_REVISION within the specified range, then set *REVISION_DELETED to
 * SVN_INVALID_REVNUM.  If PEG_REVISION or END_REVISION are invalid or if
 * END_REVISION <= PEG_REVISION, then return SVN_ERR_CLIENT_BAD_REVISION.
 *
 * Use POOL for all allocations.
 *
 * NOTE: This function uses the RA get_log interfaces to do its work,
 * as a fallback mechanism for servers which don't support the native
 * get_deleted_rev API.
 */
svn_error_t *
svn_ra__get_deleted_rev_from_log(svn_ra_session_t *session,
                                 const char *rel_deleted_path,
                                 svn_revnum_t peg_revision,
                                 svn_revnum_t end_revision,
                                 svn_revnum_t *revision_deleted,
                                 apr_pool_t *pool);


/**
 * Fallback logic for svn_ra_get_inherited_props() when that API
 * need to find PATH's inherited properties on a legacy server that
 * doesn't have the SVN_RA_CAPABILITY_INHERITED_PROPS capability.
 *
 * All arguments are as per svn_ra_get_inherited_props().
 */
svn_error_t *
svn_ra__get_inherited_props_walk(svn_ra_session_t *session,
                                 const char *path,
                                 svn_revnum_t revision,
                                 apr_array_header_t **inherited_props,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool);

/* Utility function to provide a shim between a returned Ev2 and an RA
   provider's Ev1-based commit editor.

   See svn_ra__get_commit_ev2() for parameter semantics.  */
svn_error_t *
svn_ra__use_commit_shim(svn_editor_t **editor,
                        svn_ra_session_t *session,
                        apr_hash_t *revprop_table,
                        svn_commit_callback2_t callback,
                        void *callback_baton,
                        apr_hash_t *lock_tokens,
                        svn_boolean_t keep_locks,
                        svn_ra__provide_base_cb_t provide_base_cb,
                        svn_ra__provide_props_cb_t provide_props_cb,
                        svn_ra__get_copysrc_kind_cb_t get_copysrc_kind_cb,
                        void *cb_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Utility function to provide a shim between a returned Ev2 and an RA
   provider's Ev1-based commit editor.

   See svn_ra__replay_range_ev2() for parameter semantics.  */
svn_error_t *
svn_ra__use_replay_range_shim(svn_ra_session_t *session,
                              svn_revnum_t start_revision,
                              svn_revnum_t end_revision,
                              svn_revnum_t low_water_mark,
                              svn_boolean_t send_deltas,
                              svn_ra__replay_revstart_ev2_callback_t revstart_func,
                              svn_ra__replay_revfinish_ev2_callback_t revfinish_func,
                              void *replay_baton,
                              svn_ra__provide_base_cb_t provide_base_cb,
                              svn_ra__provide_props_cb_t provide_props_cb,
                              void *cb_baton,
                              apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif

#endif
