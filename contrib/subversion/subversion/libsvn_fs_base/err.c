/*
 * err.c : implementation of fs-private error functions
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



#include <stdlib.h>
#include <stdarg.h>

#include "svn_private_config.h"
#include "svn_fs.h"
#include "err.h"
#include "id.h"

#include "../libsvn_fs/fs-loader.h"



/* Building common error objects.  */


svn_error_t *
svn_fs_base__err_corrupt_fs_revision(svn_fs_t *fs, svn_revnum_t rev)
{
  return svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     _("Corrupt filesystem revision %ld in filesystem '%s'"),
     rev, fs->path);
}


svn_error_t *
svn_fs_base__err_dangling_id(svn_fs_t *fs, const svn_fs_id_t *id)
{
  svn_string_t *id_str = svn_fs_base__id_unparse(id, fs->pool);
  return svn_error_createf
    (SVN_ERR_FS_ID_NOT_FOUND, 0,
     _("Reference to non-existent node '%s' in filesystem '%s'"),
     id_str->data, fs->path);
}


svn_error_t *
svn_fs_base__err_dangling_rev(svn_fs_t *fs, svn_revnum_t rev)
{
  /* Log the UUID as this error may be reported to the client. */
  return svn_error_createf
    (SVN_ERR_FS_NO_SUCH_REVISION, 0,
     _("No such revision %ld in filesystem '%s'"),
     rev, fs->uuid);
}


svn_error_t *
svn_fs_base__err_corrupt_txn(svn_fs_t *fs,
                             const char *txn)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     _("Corrupt entry in 'transactions' table for '%s'"
       " in filesystem '%s'"), txn, fs->path);
}


svn_error_t *
svn_fs_base__err_corrupt_copy(svn_fs_t *fs, const char *copy_id)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     _("Corrupt entry in 'copies' table for '%s' in filesystem '%s'"),
     copy_id, fs->path);
}


svn_error_t *
svn_fs_base__err_no_such_txn(svn_fs_t *fs, const char *txn)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_SUCH_TRANSACTION, 0,
     _("No transaction named '%s' in filesystem '%s'"),
     txn, fs->path);
}


svn_error_t *
svn_fs_base__err_txn_not_mutable(svn_fs_t *fs, const char *txn)
{
  return
    svn_error_createf
    (SVN_ERR_FS_TRANSACTION_NOT_MUTABLE, 0,
     _("Cannot modify transaction named '%s' in filesystem '%s'"),
     txn, fs->path);
}


svn_error_t *
svn_fs_base__err_no_such_copy(svn_fs_t *fs, const char *copy_id)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_SUCH_COPY, 0,
     _("No copy with id '%s' in filesystem '%s'"), copy_id, fs->path);
}


svn_error_t *
svn_fs_base__err_bad_lock_token(svn_fs_t *fs, const char *lock_token)
{
  return
    svn_error_createf
    (SVN_ERR_FS_BAD_LOCK_TOKEN, 0,
     _("Token '%s' does not point to any existing lock in filesystem '%s'"),
     lock_token, fs->path);
}

svn_error_t *
svn_fs_base__err_no_lock_token(svn_fs_t *fs, const char *path)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_LOCK_TOKEN, 0,
     _("No token given for path '%s' in filesystem '%s'"), path, fs->path);
}

svn_error_t *
svn_fs_base__err_corrupt_lock(svn_fs_t *fs, const char *lock_token)
{
  return
    svn_error_createf
    (SVN_ERR_FS_CORRUPT, 0,
     _("Corrupt lock in 'locks' table for '%s' in filesystem '%s'"),
     lock_token, fs->path);
}

svn_error_t *
svn_fs_base__err_no_such_node_origin(svn_fs_t *fs, const char *node_id)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_SUCH_NODE_ORIGIN, 0,
     _("No record in 'node-origins' table for node id '%s' in "
       "filesystem '%s'"), node_id, fs->path);
}

svn_error_t *
svn_fs_base__err_no_such_checksum_rep(svn_fs_t *fs, svn_checksum_t *checksum)
{
  return
    svn_error_createf
    (SVN_ERR_FS_NO_SUCH_CHECKSUM_REP, 0,
     _("No record in 'checksum-reps' table for checksum '%s' in "
       "filesystem '%s'"), svn_checksum_to_cstring_display(checksum,
                                                           fs->pool),
                           fs->path);
}
