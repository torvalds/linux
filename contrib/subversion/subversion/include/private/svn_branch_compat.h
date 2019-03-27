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
 * @file svn_branch_compat.h
 * @brief Compatibility with svn_delta_editor_t etc.
 *
 * @since New in ???.
 */

#ifndef SVN_BRANCH_COMPAT_H
#define SVN_BRANCH_COMPAT_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_delta.h"
#include "svn_ra.h"
#include "private/svn_branch.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Callback to retrieve a node's kind and content.  This is
 * needed by the various editor shims in order to effect backwards
 * compatibility.
 *
 * Implementations should set @a *kind to the node kind of @a repos_relpath
 * in @a revision.
 *
 * Implementations should set @a *props to the hash of properties
 * associated with @a repos_relpath in @a revision, allocating that hash
 * and its contents in @a result_pool. Only the 'regular' props should be
 * included, not special props such as 'entry props'.
 *
 * Implementations should set @a *filename to the name of a file
 * suitable for use as a delta base for @a repos_relpath in @a revision
 * (allocating @a *filename from @a result_pool), or to @c NULL if the
 * base stream is empty.
 *
 * Any output argument may be NULL if the output is not wanted.
 *
 * @a baton is an implementation-specific closure.
 * @a repos_relpath is relative to the repository root.
 * The implementation should ensure that @a new_content, including any
 * file therein, lives at least for the life time of @a result_pool.
 * @a scratch_pool is provided for temporary allocations.
 */
typedef svn_error_t *(*svn_branch__compat_fetch_func_t)(
  svn_node_kind_t *kind,
  apr_hash_t **props,
  svn_stringbuf_t **file_text,
  apr_hash_t **children_names,
  void *baton,
  const char *repos_relpath,
  svn_revnum_t revision,
  apr_pool_t *result_pool,
  apr_pool_t *scratch_pool
  );

/*
 */
svn_error_t *
svn_branch__compat_fetch(svn_element__payload_t **payload_p,
                         svn_branch__txn_t *txn,
                         svn_element__branch_ref_t branch_ref,
                         svn_branch__compat_fetch_func_t fetch_func,
                         void *fetch_baton,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool);

/* An object for communicating out-of-band details between an Ev1-to-Ev3
 * shim and an Ev3-to-Ev1 shim. */
typedef struct svn_branch__compat_shim_connector_t svn_branch__compat_shim_connector_t;

/* Return an Ev3 editor in *EDITOR_P which will drive the Ev1 delta
 * editor DEDITOR/DEDIT_BATON.
 *
 * This editor buffers all the changes and then drives the Ev1 when the
 * returned editor's "close" method is called.
 *
 * This editor converts moves into copy-and-delete. It presently makes a
 * one-way (lossy) conversion.
 *
 *   TODO: Option to pass the 'move' information through as some sort of
 *   metadata so that it can be preserved in an Ev3-Ev1-Ev3 round-trip
 *   conversion.
 *     - Use 'entry-props'?
 *     - Send copy-and-delete with copy-from-rev = -1?
 *
 * This editor implements the "independent per-element changes" variant
 * of the Ev3 commit editor interface.
 *
 * Use *BRANCHING_TXN as the branching state info ...
 *
 * SHIM_CONNECTOR can be used to enable a more exact round-trip conversion
 * from an Ev1 drive to Ev3 and back to Ev1. The caller should pass the
 * returned *SHIM_CONNECTOR value to svn_delta__delta_from_ev3_for_commit().
 * SHIM_CONNECTOR may be null if not wanted.
 *
 * REPOS_ROOT_URL is the repository root URL.
 *
 * FETCH_FUNC/FETCH_BATON is a callback by which the shim may retrieve the
 * original or copy-from kind/properties/text for a path being committed.
 *
 * CANCEL_FUNC / CANCEL_BATON: The usual cancellation callback; folded
 * into the produced editor. May be NULL/NULL if not wanted.
 *
 * Allocate the new editor in RESULT_POOL, which may become large and must
 * live for the lifetime of the edit. Use SCRATCH_POOL for temporary
 * allocations.
 */
svn_error_t *
svn_branch__compat_txn_from_delta_for_commit(
                        svn_branch__txn_t **txn_p,
                        svn_branch__compat_shim_connector_t **shim_connector,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        svn_branch__txn_t *branching_txn,
                        const char *repos_root_url,
                        svn_branch__compat_fetch_func_t fetch_func,
                        void *fetch_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Return a delta editor in DEDITOR/DEDITOR_BATON which will drive EDITOR.
 *
 * REPOS_ROOT_URL is the repository root URL, and BASE_RELPATH is the
 * relative path within the repository of the root directory of the edit.
 * (An Ev1 edit must be rooted at a directory, not at a file.)
 *
 * FETCH_FUNC/FETCH_BATON is a callback by which the shim may retrieve the
 * original or copy-from kind/properties/text for a path being committed.
 *
 * SHIM_CONNECTOR can be used to enable a more exact round-trip conversion
 * from an Ev1 drive to Ev3 and back to Ev1. It must live for the lifetime
 * of the edit. It may be null if not wanted.
 *
 * Allocate the new editor in RESULT_POOL, which may become large and must
 * live for the lifetime of the edit. Use SCRATCH_POOL for temporary
 * allocations.
 */
svn_error_t *
svn_branch__compat_delta_from_txn_for_commit(
                        const svn_delta_editor_t **deditor,
                        void **dedit_baton,
                        svn_branch__txn_t *edit_txn,
                        const char *repos_root_url,
                        const char *base_relpath,
                        svn_branch__compat_fetch_func_t fetch_func,
                        void *fetch_baton,
                        const svn_branch__compat_shim_connector_t *shim_connector,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Return in NEW_DEDITOR/NEW_DETIT_BATON a delta editor that wraps
 * OLD_DEDITOR/OLD_DEDIT_BATON, inserting a pair of shims that convert
 * Ev1 to Ev3 and back to Ev1.
 *
 * REPOS_ROOT_URL is the repository root URL, and BASE_RELPATH is the
 * relative path within the repository of the root directory of the edit.
 *
 * FETCH_FUNC/FETCH_BATON is a callback by which the shim may retrieve the
 * original or copy-from kind/properties/text for a path being committed.
 */
svn_error_t *
svn_branch__compat_insert_shims(
                        const svn_delta_editor_t **new_deditor,
                        void **new_dedit_baton,
                        const svn_delta_editor_t *old_deditor,
                        void *old_dedit_baton,
                        const char *repos_root,
                        const char *base_relpath,
                        svn_branch__compat_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* A callback for declaring the target revision of an update or switch.
 */
typedef svn_error_t *(*svn_branch__compat_set_target_revision_func_t)(
  void *baton,
  svn_revnum_t target_revision,
  apr_pool_t *scratch_pool);

/* An update (or switch) editor.
 *
 * This consists of a plain Ev3 editor and the additional methods or
 * resources needed for use as an update or switch editor.
 */
typedef struct svn_branch__compat_update_editor3_t {
  /* The txn we're driving. */
  svn_branch__txn_t *edit_txn;

  /* A method to communicate the target revision of the update (or switch),
   * to be called before driving the editor. It has its own baton, rather
   * than using the editor's baton, so that the editor can be replaced (by
   * a wrapper editor, typically) without having to wrap this callback. */
  svn_branch__compat_set_target_revision_func_t set_target_revision_func;
  void *set_target_revision_baton;
} svn_branch__compat_update_editor3_t;

/* Like svn_delta__ev3_from_delta_for_commit() but for an update editor.
 */
svn_error_t *
svn_branch__compat_txn_from_delta_for_update(
                        svn_branch__compat_update_editor3_t **editor_p,
                        const svn_delta_editor_t *deditor,
                        void *dedit_baton,
                        svn_branch__txn_t *branching_txn,
                        const char *repos_root_url,
                        const char *base_repos_relpath,
                        svn_branch__compat_fetch_func_t fetch_func,
                        void *fetch_baton,
                        svn_cancel_func_t cancel_func,
                        void *cancel_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* Like svn_delta__delta_from_ev3_for_commit() but for an update editor.
 */
svn_error_t *
svn_branch__compat_delta_from_txn_for_update(
                        const svn_delta_editor_t **deditor,
                        void **dedit_baton,
                        svn_branch__compat_update_editor3_t *update_editor,
                        const char *repos_root_url,
                        const char *base_repos_relpath,
                        svn_branch__compat_fetch_func_t fetch_func,
                        void *fetch_baton,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/* An Ev1 editor that drives (heuristically) a move-tracking editor.
 */
svn_error_t *
svn_branch__compat_get_migration_editor(
                        const svn_delta_editor_t **old_editor,
                        void **old_edit_baton,
                        svn_branch__txn_t *edit_txn,
                        svn_ra_session_t *from_session,
                        svn_revnum_t revision,
                        apr_pool_t *result_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_BRANCH_COMPAT_H */
