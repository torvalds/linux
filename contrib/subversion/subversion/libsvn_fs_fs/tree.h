/* tree.h : internal interface to tree node functions
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

#ifndef SVN_LIBSVN_FS_TREE_H
#define SVN_LIBSVN_FS_TREE_H

#include "fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* In POOL, create an instance of a DAG node 1st level cache.
   The POOL will be cleared at regular intervals. */
fs_fs_dag_cache_t*
svn_fs_fs__create_dag_cache(apr_pool_t *pool);

/* Set *ROOT_P to the root directory of revision REV in filesystem FS.
   Allocate the structure in POOL. */
svn_error_t *svn_fs_fs__revision_root(svn_fs_root_t **root_p, svn_fs_t *fs,
                                      svn_revnum_t rev, apr_pool_t *pool);

/* Does nothing, but included for Subversion 1.0.x compatibility. */
svn_error_t *svn_fs_fs__deltify(svn_fs_t *fs, svn_revnum_t rev,
                                apr_pool_t *pool);

/* Commit the transaction TXN as a new revision.  Return the new
   revision in *NEW_REV.  If the transaction conflicts with other
   changes return SVN_ERR_FS_CONFLICT and set *CONFLICT_P to a string
   that details the cause of the conflict. */
svn_error_t *svn_fs_fs__commit_txn(const char **conflict_p,
                                   svn_revnum_t *new_rev, svn_fs_txn_t *txn,
                                   apr_pool_t *pool);

/* Set ROOT_P to the root directory of transaction TXN.  Allocate the
   structure in POOL. */
svn_error_t *svn_fs_fs__txn_root(svn_fs_root_t **root_p, svn_fs_txn_t *txn,
                                 apr_pool_t *pool);


/* Set KIND_P to the node kind of the node at PATH in ROOT.
   Allocate the structure in POOL. */
svn_error_t *
svn_fs_fs__check_path(svn_node_kind_t *kind_p,
                      svn_fs_root_t *root,
                      const char *path,
                      apr_pool_t *pool);

/* Implement root_vtable_t.node_id(). */
svn_error_t *
svn_fs_fs__node_id(const svn_fs_id_t **id_p,
                   svn_fs_root_t *root,
                   const char *path,
                   apr_pool_t *pool);

/* Set *REVISION to the revision in which PATH under ROOT was created.
   Use POOL for any temporary allocations.  If PATH is in an
   uncommitted transaction, *REVISION will be set to
   SVN_INVALID_REVNUM. */
svn_error_t *
svn_fs_fs__node_created_rev(svn_revnum_t *revision,
                            svn_fs_root_t *root,
                            const char *path,
                            apr_pool_t *pool);

/* Verify metadata for ROOT.
   ### Currently only implemented for revision roots. */
svn_error_t *
svn_fs_fs__verify_root(svn_fs_root_t *root,
                       apr_pool_t *pool);

svn_error_t *
svn_fs_fs__info_format(int *fs_format,
                       svn_version_t **supports_version,
                       svn_fs_t *fs,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);


svn_error_t *
svn_fs_fs__info_config_files(apr_array_header_t **files,
                             svn_fs_t *fs,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_TREE_H */
