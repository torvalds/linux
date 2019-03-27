/*
 * err.h : interface to routines for returning Berkeley DB errors
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



#ifndef SVN_LIBSVN_FS_ERR_H
#define SVN_LIBSVN_FS_ERR_H

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_fs.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/* Building common error objects.  */


/* SVN_ERR_FS_CORRUPT: the REVISION skel of revision REV in FS is corrupt.  */
svn_error_t *svn_fs_base__err_corrupt_fs_revision(svn_fs_t *fs,
                                                  svn_revnum_t rev);

/* SVN_ERR_FS_ID_NOT_FOUND: something in FS refers to node revision
   ID, but that node revision doesn't exist.  */
svn_error_t *svn_fs_base__err_dangling_id(svn_fs_t *fs,
                                          const svn_fs_id_t *id);

/* SVN_ERR_FS_CORRUPT: something in FS refers to filesystem revision REV,
   but that filesystem revision doesn't exist.  */
svn_error_t *svn_fs_base__err_dangling_rev(svn_fs_t *fs, svn_revnum_t rev);

/* SVN_ERR_FS_CORRUPT: the entry for TXN in the `transactions' table
   is corrupt.  */
svn_error_t *svn_fs_base__err_corrupt_txn(svn_fs_t *fs, const char *txn);

/* SVN_ERR_FS_CORRUPT: the entry for COPY_ID in the `copies' table
   is corrupt.  */
svn_error_t *svn_fs_base__err_corrupt_copy(svn_fs_t *fs, const char *copy_id);

/* SVN_ERR_FS_NO_SUCH_TRANSACTION: there is no transaction named TXN in FS.  */
svn_error_t *svn_fs_base__err_no_such_txn(svn_fs_t *fs, const char *txn);

/* SVN_ERR_FS_TRANSACTION_NOT_MUTABLE: trying to change the
   unchangeable transaction named TXN in FS.  */
svn_error_t *svn_fs_base__err_txn_not_mutable(svn_fs_t *fs, const char *txn);

/* SVN_ERR_FS_NO_SUCH_COPY: there is no copy with id COPY_ID in FS.  */
svn_error_t *svn_fs_base__err_no_such_copy(svn_fs_t *fs, const char *copy_id);

/* SVN_ERR_FS_BAD_LOCK_TOKEN: LOCK_TOKEN does not refer to a lock in FS.  */
svn_error_t *svn_fs_base__err_bad_lock_token(svn_fs_t *fs,
                                             const char *lock_token);

/* SVN_ERR_FS_NO_LOCK_TOKEN: no lock token given for PATH in FS.  */
svn_error_t *svn_fs_base__err_no_lock_token(svn_fs_t *fs, const char *path);

/* SVN_ERR_FS_CORRUPT: a lock in `locks' table is corrupt.  */
svn_error_t *svn_fs_base__err_corrupt_lock(svn_fs_t *fs,
                                           const char *lock_token);

/* SVN_ERR_FS_NO_SUCH_NODE_ORIGIN: no recorded node origin for NODE_ID
   in FS.  */
svn_error_t *svn_fs_base__err_no_such_node_origin(svn_fs_t *fs,
                                                  const char *node_id);

/* SVN_ERR_FS_NO_SUCH_CHECKSUM_REP: no recorded rep key for CHECKSUM in FS. */
svn_error_t *svn_fs_base__err_no_such_checksum_rep(svn_fs_t *fs,
                                                   svn_checksum_t *checksum);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_ERR_H */
