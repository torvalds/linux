/* node-rev.h : interface to node revision retrieval and storage
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

#ifndef SVN_LIBSVN_FS_NODE_REV_H
#define SVN_LIBSVN_FS_NODE_REV_H

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "svn_fs.h"
#include "trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*** Functions. ***/

/* Create an entirely new, mutable node in the filesystem FS, whose
   NODE-REVISION is NODEREV, as part of TRAIL.  Set *ID_P to the new
   node revision's ID.  Use POOL for any temporary allocation.

   COPY_ID is the copy_id to use in the node revision ID returned in
   *ID_P.

   TXN_ID is the Subversion transaction under which this occurs.

   After this call, the node table manager assumes that the new node's
   contents will change frequently.  */
svn_error_t *svn_fs_base__create_node(const svn_fs_id_t **id_p,
                                      svn_fs_t *fs,
                                      node_revision_t *noderev,
                                      const char *copy_id,
                                      const char *txn_id,
                                      trail_t *trail,
                                      apr_pool_t *pool);

/* Create a node revision in FS which is an immediate successor of
   OLD_ID, whose contents are NEW_NR, as part of TRAIL.  Set *NEW_ID_P
   to the new node revision's ID.  Use POOL for any temporary
   allocation.

   COPY_ID, if non-NULL, is a key into the `copies' table, and
   indicates that this new node is being created as the result of a
   copy operation, and specifically which operation that was.

   TXN_ID is the Subversion transaction under which this occurs.

   After this call, the deltification code assumes that the new node's
   contents will change frequently, and will avoid representing other
   nodes as deltas against this node's contents.  */
svn_error_t *svn_fs_base__create_successor(const svn_fs_id_t **new_id_p,
                                           svn_fs_t *fs,
                                           const svn_fs_id_t *old_id,
                                           node_revision_t *new_nr,
                                           const char *copy_id,
                                           const char *txn_id,
                                           trail_t *trail,
                                           apr_pool_t *pool);


/* Delete node revision ID from FS's `nodes' table, as part of TRAIL.
   If ORIGIN_ALSO is set, also delete the record for this ID's node ID
   from the `node-origins' index table (which is typically only done
   if the caller thinks that ID points to the only node revision ID in
   its line of history).

   WARNING: This does not check that the node revision is mutable!
   Callers should do that check themselves.  */
svn_error_t *svn_fs_base__delete_node_revision(svn_fs_t *fs,
                                               const svn_fs_id_t *id,
                                               svn_boolean_t origin_also,
                                               trail_t *trail,
                                               apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_NODE_REV_H */
