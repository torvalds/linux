/* transaction.h --- transaction-related functions of FSX
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

#ifndef SVN_LIBSVN_FS_X_TRANSACTION_H
#define SVN_LIBSVN_FS_X_TRANSACTION_H

#include "fs.h"

/* Return the transaction ID of TXN.
 */
svn_fs_x__txn_id_t
svn_fs_x__txn_get_id(svn_fs_txn_t *txn);

/* Obtain a write lock on the filesystem FS in a subpool of SCRATCH_POOL,
   call BODY with BATON and that subpool, destroy the subpool (releasing the
   write lock) and return what BODY returned. */
svn_error_t *
svn_fs_x__with_write_lock(svn_fs_t *fs,
                          svn_error_t *(*body)(void *baton,
                                               apr_pool_t *scratch_pool),
                          void *baton,
                          apr_pool_t *scratch_pool);

/* Obtain a pack operation lock on the filesystem FS in a subpool of
   SCRATCH_POOL, call BODY with BATON and that subpool, destroy the subpool
   (releasing the write lock) and return what BODY returned. */
svn_error_t *
svn_fs_x__with_pack_lock(svn_fs_t *fs,
                         svn_error_t *(*body)(void *baton,
                                              apr_pool_t *scratch_pool),
                         void *baton,
                         apr_pool_t *scratch_pool);

/* Obtain the txn-current file lock on the filesystem FS in a subpool of
   SCRATCH_POOL, call BODY with BATON and that subpool, destroy the subpool
   (releasing the write lock) and return what BODY returned. */
svn_error_t *
svn_fs_x__with_txn_current_lock(svn_fs_t *fs,
                                svn_error_t *(*body)(void *baton,
                                                   apr_pool_t *scratch_pool),
                                void *baton,
                                apr_pool_t *scratch_pool);

/* Obtain all locks on the filesystem FS in a subpool of SCRATCH_POOL,
   call BODY with BATON and that subpool, destroy the subpool (releasing
   the locks) and return what BODY returned.

   This combines svn_fs_x__with_write_lock, svn_fs_x__with_pack_lock,
   and svn_fs_x__with_txn_current_lock, ensuring correct lock ordering. */
svn_error_t *
svn_fs_x__with_all_locks(svn_fs_t *fs,
                         svn_error_t *(*body)(void *baton,
                                              apr_pool_t *scratch_pool),
                         void *baton,
                         apr_pool_t *scratch_pool);

/* Return TRUE, iff NODEREV is the root node of a transaction that has not
   seen any modifications, yet. */
svn_boolean_t
svn_fs_x__is_fresh_txn_root(svn_fs_x__noderev_t *noderev);

/* Store NODEREV as the node-revision in the transaction defined by NODEREV's
   ID within FS.  Do any necessary temporary allocation in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__put_node_revision(svn_fs_t *fs,
                            svn_fs_x__noderev_t *noderev,
                            apr_pool_t *scratch_pool);

/* Find the paths which were changed in transaction TXN_ID of
   filesystem FS and store them in *CHANGED_PATHS_P.
   Get any temporary allocations from SCRATCH_POOL. */
svn_error_t *
svn_fs_x__txn_changes_fetch(apr_hash_t **changed_paths_p,
                            svn_fs_t *fs,
                            svn_fs_x__txn_id_t txn_id,
                            apr_pool_t *scratch_pool);

/* Set the transaction property NAME to the value VALUE in transaction
   TXN.  Perform temporary allocations from SCRATCH_POOL. */
svn_error_t *
svn_fs_x__change_txn_prop(svn_fs_txn_t *txn,
                          const char *name,
                          const svn_string_t *value,
                          apr_pool_t *scratch_pool);

/* Change transaction properties in transaction TXN based on PROPS.
   Perform temporary allocations from SCRATCH_POOL. */
svn_error_t *
svn_fs_x__change_txn_props(svn_fs_txn_t *txn,
                           const apr_array_header_t *props,
                           apr_pool_t *scratch_pool);

/* Store a transaction record in *TXN_P for the transaction identified
   by TXN_ID in filesystem FS.  Allocate everything from POOL. */
svn_error_t *
svn_fs_x__get_txn(svn_fs_x__transaction_t **txn_p,
                  svn_fs_t *fs,
                  svn_fs_x__txn_id_t txn_id,
                  apr_pool_t *pool);

/* Return the next available copy_id in *COPY_ID for the transaction
   TXN_ID in filesystem FS.  Allocate temporaries in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__reserve_copy_id(svn_fs_x__id_t *copy_id_p,
                          svn_fs_t *fs,
                          svn_fs_x__txn_id_t txn_id,
                          apr_pool_t *scratch_pool);

/* Create an entirely new mutable node in the filesystem FS, whose
   node-revision is NODEREV.  COPY_ID is the copy_id to use in the
   node revision ID.  TXN_ID is the Subversion transaction  under
   which this occurs.  Use SCRATCH_POOL for temporary allocations. */
svn_error_t *
svn_fs_x__create_node(svn_fs_t *fs,
                      svn_fs_x__noderev_t *noderev,
                      const svn_fs_x__id_t *copy_id,
                      svn_fs_x__txn_id_t txn_id,
                      apr_pool_t *scratch_pool);

/* Remove all references to the transaction TXN_ID from filesystem FS.
   Temporary allocations are from SCRATCH_POOL. */
svn_error_t *
svn_fs_x__purge_txn(svn_fs_t *fs,
                    const char *txn_id,
                    apr_pool_t *scratch_pool);

/* Abort the existing transaction TXN, performing any temporary
   allocations in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__abort_txn(svn_fs_txn_t *txn,
                    apr_pool_t *scratch_pool);

/* Add or set in filesystem FS, transaction TXN_ID, in directory
   PARENT_NODEREV a directory entry for NAME pointing to ID of type
   KIND.  The PARENT_NODEREV's DATA_REP will be redirected to the in-txn
   representation, if it had not been mutable before.

   If PARENT_NODEREV does not have a DATA_REP, allocate one in RESULT_POOL.
   Temporary allocations are done in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__set_entry(svn_fs_t *fs,
                    svn_fs_x__txn_id_t txn_id,
                    svn_fs_x__noderev_t *parent_noderev,
                    const char *name,
                    const svn_fs_x__id_t *id,
                    svn_node_kind_t kind,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);

/* Add a change to the changes record for filesystem FS in transaction
   TXN_ID.  Mark path PATH as changed according to the type in
   CHANGE_KIND.  If the text representation was changed set TEXT_MOD
   to TRUE, and likewise for PROP_MOD as well as MERGEINFO_MOD.
   If this change was the result of a copy, set COPYFROM_REV and
   COPYFROM_PATH to the revision and path of the copy source, otherwise
   they should be set to SVN_INVALID_REVNUM and NULL.  Perform any
   temporary allocations from SCRATCH_POOL. */
svn_error_t *
svn_fs_x__add_change(svn_fs_t *fs,
                     svn_fs_x__txn_id_t txn_id,
                     const char *path,
                     svn_fs_path_change_kind_t change_kind,
                     svn_boolean_t text_mod,
                     svn_boolean_t prop_mod,
                     svn_boolean_t mergeinfo_mod,
                     svn_node_kind_t node_kind,
                     svn_revnum_t copyfrom_rev,
                     const char *copyfrom_path,
                     apr_pool_t *scratch_pool);

/* Return a writable stream in *STREAM, allocated in RESULT_POOL, that
   allows storing the text representation of node-revision NODEREV in
   filesystem FS. */
svn_error_t *
svn_fs_x__set_contents(svn_stream_t **stream,
                       svn_fs_t *fs,
                       svn_fs_x__noderev_t *noderev,
                       apr_pool_t *result_pool);

/* Create a node revision in FS which is an immediate successor of
   NEW_NODEREV's predecessor.  Use SCRATCH_POOL for any temporary allocation.

   COPY_ID, is a key into the `copies' table, and
   indicates that this new node is being created as the result of a
   copy operation, and specifically which operation that was.

   TXN_ID is the Subversion transaction under which this occurs.

   After this call, the deltification code assumes that the new node's
   contents will change frequently, and will avoid representing other
   nodes as deltas against this node's contents.  */
svn_error_t *
svn_fs_x__create_successor(svn_fs_t *fs,
                           svn_fs_x__noderev_t *new_noderev,
                           const svn_fs_x__id_t *copy_id,
                           svn_fs_x__txn_id_t txn_id,
                           apr_pool_t *scratch_pool);

/* Write a new property list PROPLIST for node-revision NODEREV in
   filesystem FS.  Perform any temporary allocations in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__set_proplist(svn_fs_t *fs,
                       svn_fs_x__noderev_t *noderev,
                       apr_hash_t *proplist,
                       apr_pool_t *scratch_pool);

/* Append the L2P and P2L indexes given by their proto index file names
 * L2P_PROTO_INDEX and P2L_PROTO_INDEX to the revision / pack FILE.
 * The latter contains revision(s) starting at REVISION in FS.
 * Use SCRATCH_POOL for temporary allocations.  */
svn_error_t *
svn_fs_x__add_index_data(svn_fs_t *fs,
                         apr_file_t *file,
                         const char *l2p_proto_index,
                         const char *p2l_proto_index,
                         svn_revnum_t revision,
                         apr_pool_t *scratch_pool);

/* Commit the transaction TXN in filesystem FS and return its new
   revision number in *REV.  If the transaction is out of date, return
   the error SVN_ERR_FS_TXN_OUT_OF_DATE. Use SCRATCH_POOL for temporary
   allocations. */
svn_error_t *
svn_fs_x__commit(svn_revnum_t *new_rev_p,
                 svn_fs_t *fs,
                 svn_fs_txn_t *txn,
                 apr_pool_t *scratch_pool);

/* Set *NAMES_P to an array of names which are all the active
   transactions in filesystem FS.  Allocate the array from POOL. */
svn_error_t *
svn_fs_x__list_transactions(apr_array_header_t **names_p,
                            svn_fs_t *fs,
                            apr_pool_t *pool);

/* Open the transaction named NAME in filesystem FS.  Set *TXN_P to
 * the transaction. If there is no such transaction, return
` * SVN_ERR_FS_NO_SUCH_TRANSACTION.  Allocate the new transaction in
 * POOL. */
svn_error_t *
svn_fs_x__open_txn(svn_fs_txn_t **txn_p,
                   svn_fs_t *fs,
                   const char *name,
                   apr_pool_t *pool);

/* Return the property list from transaction TXN and store it in
   *PROPLIST.  Allocate the property list from POOL. */
svn_error_t *
svn_fs_x__txn_proplist(apr_hash_t **table_p,
                       svn_fs_txn_t *txn,
                       apr_pool_t *pool);

/* Delete the mutable node-revision referenced by ID, along with any
   mutable props or directory contents associated with it.  Perform
   temporary allocations in SCRATCH_POOL. */
svn_error_t *
svn_fs_x__delete_node_revision(svn_fs_t *fs,
                               const svn_fs_x__id_t *id,
                               apr_pool_t *scratch_pool);

/* Retrieve information about the Subversion transaction TXN_ID from
   the `transactions' table of FS, using SCRATCH_POOL for temporary
   allocations.  Set *RENUM to the transaction's base revision.

   If there is no such transaction, SVN_ERR_FS_NO_SUCH_TRANSACTION is
   the error returned.

   Returns SVN_ERR_FS_TRANSACTION_NOT_MUTABLE if TXN_NAME refers to a
   transaction that has already been committed.  */
svn_error_t *
svn_fs_x__get_base_rev(svn_revnum_t *revnum,
                       svn_fs_t *fs,
                       svn_fs_x__txn_id_t txn_id,
                       apr_pool_t *scratch_pool);

/* Find the value of the property named PROPNAME in transaction TXN.
   Return the contents in *VALUE_P.  The contents will be allocated
   from POOL. */
svn_error_t *
svn_fs_x__txn_prop(svn_string_t **value_p,
                   svn_fs_txn_t *txn,
                   const char *propname,
                   apr_pool_t *pool);

/* Begin a new transaction in filesystem FS, based on existing
   revision REV.  The new transaction is returned in *TXN_P, allocated
   in RESULT_POOL.  Allocate temporaries from SCRATCH_POOL. */
svn_error_t *
svn_fs_x__begin_txn(svn_fs_txn_t **txn_p,
                    svn_fs_t *fs,
                    svn_revnum_t rev,
                    apr_uint32_t flags,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool);

#endif
