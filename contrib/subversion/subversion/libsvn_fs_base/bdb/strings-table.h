/* strings-table.h : internal interface to `strings' table
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

#ifndef SVN_LIBSVN_FS_STRINGS_TABLE_H
#define SVN_LIBSVN_FS_STRINGS_TABLE_H

#define SVN_WANT_BDB
#include "svn_private_config.h"

#include "svn_io.h"
#include "svn_fs.h"
#include "../trail.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* This interface provides raw access to the `strings' table.  It does
   not deal with deltification, undeltification, or skels.  It just
   reads and writes strings of bytes. */


/* Open a `strings' table in ENV.  If CREATE is non-zero, create
 * one if it doesn't exist.  Set *STRINGS_P to the new table.
 * Return a Berkeley DB error code.
 */
int svn_fs_bdb__open_strings_table(DB **strings_p,
                                   DB_ENV *env,
                                   svn_boolean_t create);


/* Read *LEN bytes into BUF from OFFSET in string KEY in FS, as part
 * of TRAIL.
 *
 * On return, *LEN is set to the number of bytes read.  If this value
 * is less than the number requested, the end of the string has been
 * reached (no error is returned on end-of-string).
 *
 * If OFFSET is past the end of the string, then *LEN will be set to
 * zero. Callers which are advancing OFFSET as they read portions of
 * the string can terminate their loop when *LEN is returned as zero
 * (which will occur when OFFSET == length(the string)).
 *
 * If string KEY does not exist, the error SVN_ERR_FS_NO_SUCH_STRING
 * is returned.
 */
svn_error_t *svn_fs_bdb__string_read(svn_fs_t *fs,
                                     const char *key,
                                     char *buf,
                                     svn_filesize_t offset,
                                     apr_size_t *len,
                                     trail_t *trail,
                                     apr_pool_t *pool);


/* Set *SIZE to the size in bytes of string KEY in FS, as part of
 * TRAIL.
 *
 * If string KEY does not exist, return SVN_ERR_FS_NO_SUCH_STRING.
 */
svn_error_t *svn_fs_bdb__string_size(svn_filesize_t *size,
                                     svn_fs_t *fs,
                                     const char *key,
                                     trail_t *trail,
                                     apr_pool_t *pool);


/* Append LEN bytes from BUF to string *KEY in FS, as part of TRAIL.
 *
 * If *KEY is null, then create a new string and store the new key in
 * *KEY (allocating it in POOL), and write LEN bytes from BUF
 * as the initial contents of the string.
 *
 * If *KEY is not null but there is no string named *KEY, return
 * SVN_ERR_FS_NO_SUCH_STRING.
 *
 * Note: to overwrite the old contents of a string, call
 * svn_fs_bdb__string_clear() and then svn_fs_bdb__string_append().  */
svn_error_t *svn_fs_bdb__string_append(svn_fs_t *fs,
                                       const char **key,
                                       apr_size_t len,
                                       const char *buf,
                                       trail_t *trail,
                                       apr_pool_t *pool);


/* Make string KEY in FS zero length, as part of TRAIL.
 * If the string does not exist, return SVN_ERR_FS_NO_SUCH_STRING.
 */
svn_error_t *svn_fs_bdb__string_clear(svn_fs_t *fs,
                                      const char *key,
                                      trail_t *trail,
                                      apr_pool_t *pool);


/* Delete string KEY from FS, as part of TRAIL.
 *
 * If string KEY does not exist, return SVN_ERR_FS_NO_SUCH_STRING.
 *
 * WARNING: Deleting a string renders unusable any representations
 * that refer to it.  Be careful.
 */
svn_error_t *svn_fs_bdb__string_delete(svn_fs_t *fs,
                                       const char *key,
                                       trail_t *trail,
                                       apr_pool_t *pool);


/* Copy the contents of the string referred to by KEY in FS into a new
 * record, returning the new record's key in *NEW_KEY.  All
 * allocations (including *NEW_KEY) occur in POOL.  */
svn_error_t *svn_fs_bdb__string_copy(svn_fs_t *fs,
                                     const char **new_key,
                                     const char *key,
                                     trail_t *trail,
                                     apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_LIBSVN_FS_STRINGS_TABLE_H */
