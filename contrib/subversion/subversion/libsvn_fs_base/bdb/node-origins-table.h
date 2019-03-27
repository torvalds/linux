/* node-origins-table.h : internal interface to ops on `node-origins' table
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

#ifndef SVN_LIBSVN_FS_NODE_ORIGINS_TABLE_H
#define SVN_LIBSVN_FS_NODE_ORIGINS_TABLE_H

#include "svn_fs.h"
#include "svn_error.h"
#include "../trail.h"
#include "../fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Open a `node-origins' table in ENV.  If CREATE is non-zero, create
   one if it doesn't exist.  Set *NODE_ORIGINS_P to the new table.
   Return a Berkeley DB error code.  */
int svn_fs_bdb__open_node_origins_table(DB **node_origins_p,
                                        DB_ENV *env,
                                        svn_boolean_t create);

/* Set *ORIGIN_ID to the node revision ID from which the history of
   all nodes in FS whose node ID is NODE_ID springs, as determined by
   a look in the `node-origins' table.  Do this as part of TRAIL.  Use
   POOL for allocations.

   If no such node revision ID is stored for NODE_ID, return
   SVN_ERR_FS_NO_SUCH_NODE_ORIGIN.  */
svn_error_t *svn_fs_bdb__get_node_origin(const svn_fs_id_t **origin_id,
                                         svn_fs_t *fs,
                                         const char *node_id,
                                         trail_t *trail,
                                         apr_pool_t *pool);

/* Store in the `node-origins' table a mapping of NODE_ID to original
   node revision ID ORIGIN_ID for FS.  Do this as part of TRAIL.  Use
   POOL for temporary allocations.  */
svn_error_t *svn_fs_bdb__set_node_origin(svn_fs_t *fs,
                                         const char *node_id,
                                         const svn_fs_id_t *origin_id,
                                         trail_t *trail,
                                         apr_pool_t *pool);

/* Delete from the `node-origins' table the record for NODE_ID in FS.
   Do this as part of TRAIL.  Use POOL for temporary allocations.  */
svn_error_t *svn_fs_bdb__delete_node_origin(svn_fs_t *fs,
                                            const char *node_id,
                                            trail_t *trail,
                                            apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_NODE_ORIGINS_TABLE_H */
