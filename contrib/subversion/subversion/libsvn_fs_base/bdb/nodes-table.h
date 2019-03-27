/* nodes-table.h : interface to `nodes' table
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

#ifndef SVN_LIBSVN_FS_NODES_TABLE_H
#define SVN_LIBSVN_FS_NODES_TABLE_H

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "svn_fs.h"
#include "../trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Creating and opening the `nodes' table.  */


/* Open a `nodes' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *NODES_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs_bdb__open_nodes_table(DB **nodes_p,
                                 DB_ENV *env,
                                 svn_boolean_t create);


/* Check FS's `nodes' table to find an unused node number, and set
   *ID_P to the ID of the first revision of an entirely new node in
   FS, with copy_id COPY_ID, created in transaction TXN_ID, as part
   of TRAIL.  Allocate the new ID, and do all temporary allocation,
   in POOL.  */
svn_error_t *svn_fs_bdb__new_node_id(svn_fs_id_t **id_p,
                                     svn_fs_t *fs,
                                     const char *copy_id,
                                     const char *txn_id,
                                     trail_t *trail,
                                     apr_pool_t *pool);


/* Delete node revision ID from FS's `nodes' table, as part of TRAIL.
   WARNING: This does not check that the node revision is mutable!
   Callers should do that check themselves.

   todo: Jim and Karl are both not sure whether it would be better for
   this to check mutability or not.  On the one hand, having the
   lowest level do that check would seem intuitively good.  On the
   other hand, we'll need a way to delete even immutable nodes someday
   -- for example, someone accidentally commits NDA-protected data to
   a public repository and wants to remove it.  Thoughts?  */
svn_error_t *svn_fs_bdb__delete_nodes_entry(svn_fs_t *fs,
                                            const svn_fs_id_t *id,
                                            trail_t *trail,
                                            apr_pool_t *pool);


/* Set *SUCCESSOR_P to the ID of an immediate successor to node
   revision ID in FS that does not exist yet, as part of TRAIL.
   Allocate *SUCCESSOR_P in POOL.

   Use the current Subversion transaction name TXN_ID, and optionally
   a copy id COPY_ID, in the determination of the new node revision
   ID.  */
svn_error_t *svn_fs_bdb__new_successor_id(svn_fs_id_t **successor_p,
                                          svn_fs_t *fs,
                                          const svn_fs_id_t *id,
                                          const char *copy_id,
                                          const char *txn_id,
                                          trail_t *trail,
                                          apr_pool_t *pool);


/* Set *NODEREV_P to the node-revision for the node ID in FS, as
   part of TRAIL.  Do any allocations in POOL.  Allow NODEREV_P
   to be NULL, in which case it is not used, and this function acts as
   an existence check for ID in FS. */
svn_error_t *svn_fs_bdb__get_node_revision(node_revision_t **noderev_p,
                                           svn_fs_t *fs,
                                           const svn_fs_id_t *id,
                                           trail_t *trail,
                                           apr_pool_t *pool);


/* Store NODEREV as the node-revision for the node whose id
   is ID in FS, as part of TRAIL.  Do any necessary temporary
   allocation in POOL.

   After this call, the node table manager assumes that NODE's
   contents will change frequently.  */
svn_error_t *svn_fs_bdb__put_node_revision(svn_fs_t *fs,
                                           const svn_fs_id_t *id,
                                           node_revision_t *noderev,
                                           trail_t *trail,
                                           apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_NODES_TABLE_H */
