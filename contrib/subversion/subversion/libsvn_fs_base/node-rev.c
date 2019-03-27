/* node-rev.c --- storing and retrieving NODE-REVISION skels
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

#include <string.h>

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "svn_fs.h"
#include "fs.h"
#include "err.h"
#include "node-rev.h"
#include "reps-strings.h"
#include "id.h"
#include "../libsvn_fs/fs-loader.h"

#include "bdb/nodes-table.h"
#include "bdb/node-origins-table.h"


/* Creating completely new nodes.  */


svn_error_t *
svn_fs_base__create_node(const svn_fs_id_t **id_p,
                         svn_fs_t *fs,
                         node_revision_t *noderev,
                         const char *copy_id,
                         const char *txn_id,
                         trail_t *trail,
                         apr_pool_t *pool)
{
  svn_fs_id_t *id;
  base_fs_data_t *bfd = fs->fsap_data;

  /* Find an unused ID for the node.  */
  SVN_ERR(svn_fs_bdb__new_node_id(&id, fs, copy_id, txn_id, trail, pool));

  /* Store its NODE-REVISION skel.  */
  SVN_ERR(svn_fs_bdb__put_node_revision(fs, id, noderev, trail, pool));

  /* Add a record in the node origins index table if our format
     supports it.  */
  if (bfd->format >= SVN_FS_BASE__MIN_NODE_ORIGINS_FORMAT)
    {
      SVN_ERR(svn_fs_bdb__set_node_origin(fs, svn_fs_base__id_node_id(id),
                                          id, trail, pool));
    }

  *id_p = id;
  return SVN_NO_ERROR;
}



/* Creating new revisions of existing nodes.  */

svn_error_t *
svn_fs_base__create_successor(const svn_fs_id_t **new_id_p,
                              svn_fs_t *fs,
                              const svn_fs_id_t *old_id,
                              node_revision_t *new_noderev,
                              const char *copy_id,
                              const char *txn_id,
                              trail_t *trail,
                              apr_pool_t *pool)
{
  svn_fs_id_t *new_id;

  /* Choose an ID for the new node, and store it in the database.  */
  SVN_ERR(svn_fs_bdb__new_successor_id(&new_id, fs, old_id, copy_id,
                                       txn_id, trail, pool));

  /* Store the new skel under that ID.  */
  SVN_ERR(svn_fs_bdb__put_node_revision(fs, new_id, new_noderev,
                                        trail, pool));

  *new_id_p = new_id;
  return SVN_NO_ERROR;
}



/* Deleting a node revision. */

svn_error_t *
svn_fs_base__delete_node_revision(svn_fs_t *fs,
                                  const svn_fs_id_t *id,
                                  svn_boolean_t origin_also,
                                  trail_t *trail,
                                  apr_pool_t *pool)
{
  base_fs_data_t *bfd = fs->fsap_data;

  /* ### todo: here, we should adjust other nodes to compensate for
     the missing node. */

  /* Delete the node origin record, too, if asked to do so and our
     format supports it. */
  if (origin_also && (bfd->format >= SVN_FS_BASE__MIN_NODE_ORIGINS_FORMAT))
    {
      SVN_ERR(svn_fs_bdb__delete_node_origin(fs, svn_fs_base__id_node_id(id),
                                             trail, pool));
    }

  return svn_fs_bdb__delete_nodes_entry(fs, id, trail, pool);
}
