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
 * @file svn_client_mtcc.h
 * @brief Subversion multicommand client support
 *
 * Requires:  The working copy library and client library.
 * Provides:  High level multicommand api.
 * Used By:   Client programs, svnmucc.
 */

#ifndef SVN_CLIENT_MTCC_H
#define SVN_CLIENT_MTCC_H

#include "svn_client.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
 *
 * @defgroup clnt_mtcc Multi Command Context related functions
 *
 * @{
 *
 */

/** This is a structure which stores a list of repository commands
 * that can be played to a repository as a single operation
 *
 * Use svn_client__mtcc_create() to create instances
 *
 * @since New in 1.9.
 */
typedef struct svn_client__mtcc_t svn_client__mtcc_t;

/** Creates a new multicommand context for an operation on @a anchor_url and
 * its descendants.
 *
 * Allocate the context in @a result_pool and perform temporary allocations in
 * @a scratch_pool.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client__mtcc_create(svn_client__mtcc_t **mtcc,
                        const char *anchor_url,
                        svn_revnum_t base_revision,
                        svn_client_ctx_t *ctx,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool);

/** Adds a file add operation of @a relpath to @a mtcc. If @a src_checksum
 * is not null it will be provided to the repository to verify if the file
 * was transferred successfully.
 *
 * Perform temporary allocations in @a scratch_pool.
 *
 * @note The current implementation keeps @a src_stream open until @a mtcc
 * is committed.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client__mtcc_add_add_file(const char *relpath,
                              svn_stream_t *src_stream,
                              const svn_checksum_t *src_checksum,
                              svn_client__mtcc_t *mtcc,
                              apr_pool_t *scratch_pool);

/** Adds a copy operation of the node @a src_relpath at revision @a revision
 * to @a dst_relpath to @a mtcc.
 *
 * Perform temporary allocations in @a scratch_pool.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client__mtcc_add_copy(const char *src_relpath,
                          svn_revnum_t revision,
                          const char *dst_relpath,
                          svn_client__mtcc_t *mtcc,
                          apr_pool_t *scratch_pool);

/** Adds a delete of @a relpath to @a mtcc.
 *
 * Perform temporary allocations in @a scratch_pool.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client__mtcc_add_delete(const char *relpath,
                            svn_client__mtcc_t *mtcc,
                            apr_pool_t *scratch_pool);

/** Adds an mkdir operation of @a relpath to @a mtcc.
 *
 * Perform temporary allocations in @a scratch_pool.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client__mtcc_add_mkdir(const char *relpath,
                           svn_client__mtcc_t *mtcc,
                           apr_pool_t *scratch_pool);


/** Adds a move operation of the node @a src_relpath to @a dst_relpath to
 * @a mtcc.
 *
 * Perform temporary allocations in @a scratch_pool.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client__mtcc_add_move(const char *src_relpath,
                          const char *dst_relpath,
                          svn_client__mtcc_t *mtcc,
                          apr_pool_t *scratch_pool);

/** Adds a propset operation for the property @a propname to @a propval
 * (which can be NULL for a delete) on @a relpath to @a mtcc.
 *
 * If @a skip_checks is not FALSE Subversion defined properties are verified
 * for correctness like svn_client_propset_remote()
 *
 * Perform temporary allocations in @a scratch_pool.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client__mtcc_add_propset(const char *relpath,
                             const char *propname,
                             const svn_string_t *propval,
                             svn_boolean_t skip_checks,
                             svn_client__mtcc_t *mtcc,
                             apr_pool_t *scratch_pool);


/** Adds an update file operation for @a relpath to @a mtcc.
 *
 * The final version of the file is provided with @a src_stream. If @a
 * src_checksum is provided it will be provided to the repository to verify
 * the final result.
 *
 * If @a base_checksum is provided it will be used by the repository to verify
 * if the base file matches this checksum.
 *
 * If @a base_stream is not NULL only the binary diff from @a base_stream to
 * @a src_stream is written to the repository.
 *
 * Perform temporary allocations in @a scratch_pool.
 *
 * @note Callers should assume that the mtcc requires @a src_stream and @a
 * base_stream to be valid until @a mtcc is committed.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client__mtcc_add_update_file(const char *relpath,
                                 svn_stream_t *src_stream,
                                 const svn_checksum_t *src_checksum,
                                 svn_stream_t *base_stream,
                                 const svn_checksum_t *base_checksum,
                                 svn_client__mtcc_t *mtcc,
                                 apr_pool_t *scratch_pool);

/** Obtains the kind of node at @a relpath in the current state of @a mtcc.
 * This value might be from the cache (in case of modifications, copies)
 * or fetched from the repository.
 *
 * If @a check_repository is TRUE, verify the node type with the repository at
 * least once and cache the result for further checks.
 *
 * When a node does not exist this functions sets @a *kind to @c svn_node_node.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client__mtcc_check_path(svn_node_kind_t *kind,
                            const char *relpath,
                            svn_boolean_t check_repository,
                            svn_client__mtcc_t *mtcc,
                            apr_pool_t *scratch_pool);

/** Commits all operations stored in @a mtcc as a new revision and destroys
 * @a mtcc.
 *
 * @since New in 1.9.
 */
svn_error_t *
svn_client__mtcc_commit(apr_hash_t *revprop_table,
                        svn_commit_callback2_t commit_callback,
                        void *commit_baton,
                        svn_client__mtcc_t *mtcc,
                        apr_pool_t *scratch_pool);


/** @} end group: Multi Command Context related functions */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_CLIENT_MTCC_H */
